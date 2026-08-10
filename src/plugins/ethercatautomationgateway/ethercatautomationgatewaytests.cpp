// Copyright (C) 2026 Kvell

#include "ethercatautomationgatewaytests.h"

#include "automationdispatcher.h"
#include "ethercatautomationgatewayconstants.h"
#include "gatewayruntime.h"
#include "gatewayserver.h"
#include "gatewaysettingspage.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <ethercatcore/automationservice.h>
#include <ethercatcore/ethercatcoreconstants.h>
#include <ethercatcore/providers.h>
#include <ethercatcore/semanticruntimeservice.h>

#include <extensionsystem/pluginmanager.h>

#include <mcp/schemas/schema_2025_11_25.h>

#include <utils/qtcsettings.h>

#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace EtherCAT::AutomationGateway::Internal {

class FakeAutomationService final : public Core::AutomationService
{
public:
    using AutomationService::AutomationService;

    QList<Core::AutomationContextSnapshot> contexts() const final
    {
        ++readCount;
        return current;
    }

    mutable int readCount = 0;
    QList<Core::AutomationContextSnapshot> current;
};

class FakeSemanticRuntimeService final : public Core::SemanticRuntimeService
{
public:
    using SemanticRuntimeService::SemanticRuntimeService;

    QList<Data::SemanticRuntimeContext> contexts() const final
    {
        ++readCount;
        return current;
    }

    Data::SemanticOperationRecord submit(
        const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeActor &actor) final
    {
        ++submitCount;
        lastActor = actor;

        Data::SemanticOperationRecord record;
        record.request = request;
        record.actor = actor;
        record.createdAt = QDateTime::currentDateTimeUtc();
        record.updatedAt = record.createdAt;
        const auto context = std::find_if(
            current.cbegin(),
            current.cend(),
            [&request](const Data::SemanticRuntimeContext &candidate) {
                return candidate.controllerId == request.target.controllerId;
            });
        const Core::SemanticRuntimeValidation validation
            = context == current.cend()
                  ? Core::
                        SemanticRuntimeValidation{Core::SemanticRuntimeValidationError::InvalidTarget, QStringLiteral("Test semantic context is unavailable.")}
                  : Core::validateSemanticOperationRequest(request, *context);
        if (!validation.accepted()) {
            record.state = Data::SemanticOperationState::Rejected;
            record.resultCode = "fake-validation-rejected";
            record.detail = validation.detail;
        } else {
            record.state = Data::SemanticOperationState::ApprovalRequired;
            record.approvalChallenge = QCryptographicHash::hash(
                request.operationId.value.toUtf8(), QCryptographicHash::Sha256);
            record.resultCode = "approval-required";
            record.detail = "A verified user must approve this semantic operation.";
        }
        records.insert(request.operationId.value, record);
        return record;
    }

    Data::SemanticOperationRecord approve(
        const Data::SemanticOperationApprovalRequest &approval,
        const Data::SemanticRuntimeActor &actor) final
    {
        ++approveCount;
        return SemanticRuntimeService::approve(approval, actor);
    }

    std::optional<Data::SemanticOperationRecord> operation(
        const Data::SemanticOperationId &operationId) const final
    {
        ++operationReadCount;
        const auto found = records.constFind(operationId.value);
        if (found == records.cend())
            return std::nullopt;
        return *found;
    }

    mutable int readCount = 0;
    mutable int operationReadCount = 0;
    int submitCount = 0;
    int approveCount = 0;
    QList<Data::SemanticRuntimeContext> current;
    QHash<QString, Data::SemanticOperationRecord> records;
    Data::SemanticRuntimeActor lastActor;
};

class FailClosedSemanticRuntimeService final : public Core::SemanticRuntimeService
{
public:
    using SemanticRuntimeService::SemanticRuntimeService;

    QList<Data::SemanticRuntimeContext> contexts() const final { return current; }

    QList<Data::SemanticRuntimeContext> current;
};

struct FakeProviderCounters
{
    int connect = 0;
    int control = 0;
    int scan = 0;
    int write = 0;
};

class CountingConnectionProvider final : public Core::ControllerConnectionProvider
{
public:
    explicit CountingConnectionProvider(FakeProviderCounters *counters)
        : ControllerConnectionProvider(
              "EtherCAT.AutomationGateway.Tests.Connection",
              "Automation Gateway counting connection")
        , m_counters(counters)
    {
        setAvailable(true);
    }

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }

    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &, const Data::NodeId &, const QString &) final
    {
        ++m_counters->write;
        return Utils::ResultError("Counting Provider must not be called");
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final { return {}; }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &) final
    {
        ++m_counters->connect;
        return Utils::ResultError("Counting Provider must not be called");
    }

    Utils::Result<> disconnectFromController() final
    {
        ++m_counters->control;
        return Utils::ResultError("Counting Provider must not be called");
    }

    Utils::Result<> refreshController() final
    {
        ++m_counters->control;
        return Utils::ResultError("Counting Provider must not be called");
    }

    bool supportsControlCommand(Data::ControllerControlCommand) const final { return true; }

    Utils::Result<> executeControlCommand(const Data::ControllerControlRequest &) final
    {
        ++m_counters->control;
        return Utils::ResultError("Counting Provider must not be called");
    }

private:
    FakeProviderCounters *const m_counters;
};

class CountingScanProvider final : public Core::ScanProvider
{
public:
    explicit CountingScanProvider(FakeProviderCounters *counters)
        : ScanProvider("EtherCAT.AutomationGateway.Tests.Scan", "Automation Gateway counting scan")
        , m_counters(counters)
    {
        setAvailable(true);
    }

    Data::ScanState scanState() const final { return Data::ScanState::Idle; }
    Data::ScanProgress scanProgress() const final { return {}; }
    std::optional<Data::ScanResult> lastScanResult() const final { return std::nullopt; }
    QString lastScanError() const final { return {}; }

    Utils::Result<> startScan(const Data::ScanRequest &) final
    {
        ++m_counters->scan;
        return Utils::ResultError("Counting Provider must not be called");
    }

    void cancelScan() final { ++m_counters->scan; }
    void clearScanResult() final { ++m_counters->write; }

private:
    FakeProviderCounters *const m_counters;
};

static Core::AutomationContextSnapshot mockContext()
{
    Core::AutomationContextSnapshot context;
    context.scope = {Data::NodeId::create(), Data::NodeId::create()};
    context.controllerId = Core::automationControllerId(context.scope);
    context.identitySource = "ide-project-master";
    context.project.id = context.scope.projectId;
    context.project.name = "Mock Packaging Line";
    context.project.formatVersion = 8;
    context.project.createdBy = "Gateway Tests";
    context.project.valid = true;
    const Data::NodeId targetId = Data::NodeId::create();
    context.project.nodes = {
        {
            context.project.id,
            {},
            Data::ProjectNodeKind::Project,
            context.project.name,
        },
        {
            targetId,
            context.project.id,
            Data::ProjectNodeKind::Target,
            "Mock Controller",
        },
        {
            context.scope.masterId,
            targetId,
            Data::ProjectNodeKind::Master,
            "EtherCAT Master",
        },
    };

    const Data::NodeId descriptionId = Data::NodeId::create();
    Data::OfflineSlaveConfiguration configured;
    configured.id = Data::NodeId::create();
    configured.masterId = context.scope.masterId;
    configured.position = 0;
    configured.identity = {0x00000002, 0x10000001, 0x00000003};
    configured.serialNumber = 17;
    configured.alias = 4;
    configured.stationAddress = 0x1001;
    configured.name = "Mock Digital I/O";
    configured.deviceDescriptionId = descriptionId;
    context.project.slaves = {configured};
    context.project.nodes.append({
        configured.id,
        context.scope.masterId,
        Data::ProjectNodeKind::Slave,
        configured.name,
    });

    context.connection.scope = context.scope;
    context.connection.endpointSummary = "127.0.0.1:15200";
    context.connection.state = Data::ControllerConnectionState::Connected;
    context.connection.readOnly = true;
    context.connection.mock = true;
    context.connection.session = Data::ControllerSessionSummary{
        23,
        42,
        0,
        30000,
        false,
    };
    Data::ControllerStateSummary state;
    state.serviceState = Data::ControllerServiceState::Shutdown;
    state.ready = true;
    state.safeOutput = true;
    state.expectedWorkingCounter = 3;
    state.actualWorkingCounter = 3;
    context.connection.controllerState = state;
    Data::ControllerCapabilitySummary capability;
    capability.maximumSlaves = 64;
    capability.minimumCycleTimeNs = 125000;
    capability.maximumCyclicFrames = 8;
    capability.maximumProcessInputBytes = 4096;
    capability.maximumProcessOutputBytes = 4096;
    capability.coe = true;
    capability.distributedClocks = true;
    context.connection.capability = capability;

    Data::ScannedSlave scanned;
    scanned.id = Data::NodeId::create();
    scanned.position = 0;
    scanned.identity = configured.identity;
    scanned.serialNumber = configured.serialNumber;
    scanned.alias = configured.alias;
    scanned.name = configured.name;
    scanned.deviceDescriptionId = descriptionId;
    Data::ScanResult scan;
    scan.snapshot.id = Data::NodeId::create();
    scan.snapshot.projectId = context.scope.projectId;
    scan.snapshot.masterId = context.scope.masterId;
    scan.snapshot.capturedAt = QDateTime::currentDateTimeUtc();
    scan.snapshot.slaves = {scanned};
    scan.snapshot.complete = true;
    scan.snapshot.mock = true;
    scan.comparison.projectId = context.scope.projectId;
    scan.comparison.masterId = context.scope.masterId;
    scan.comparison.exactMatch = true;
    context.scan = scan;

    Data::DiagnosticsSnapshot diagnostics;
    diagnostics.projectId = context.scope.projectId;
    diagnostics.masterId = context.scope.masterId;
    diagnostics.generation = 9;
    diagnostics.capturedAt = QDateTime::currentDateTimeUtc();
    diagnostics.mock = true;
    diagnostics.masterState = Data::EtherCATState::SafeOperational;
    diagnostics.workingCounter = {
        3,
        3,
        Data::WorkingCounterState::Valid,
        0,
    };
    diagnostics.cycle.nominalCycleNs = 1000000;
    diagnostics.cycle.lastCycleNs = 1000100;
    diagnostics.cycle.jitterNs = 100;
    diagnostics.cycle.deadlineMarginNs = 700000;
    context.diagnostics = diagnostics;

    Data::DeviceDescription description;
    description.summary.id = descriptionId;
    description.summary.identity = configured.identity;
    description.summary.name = configured.name;
    description.summary.typeName = "EL1008";
    description.summary.group = "Digital I/O";
    description.summary.supported = true;
    description.sourceSha256 = QByteArray::fromHex(
        "74ea5e67b3e1d7ba575339b636abb5432ecf6790d3504d32ce1756bcfec49568");
    description.syncManagers = {
        {
            2,
            "Outputs",
            Data::SyncManagerDirection::MasterToSlave,
            0x1000,
            1,
            0,
            true,
        },
        {
            3,
            "Inputs",
            Data::SyncManagerDirection::SlaveToMaster,
            0x1100,
            1,
            0,
            true,
        },
    };
    description.rxPdos = {
        {
            0x1600,
            false,
            "Outputs",
            Data::PdoDirection::Rx,
            2,
            true,
            true,
            {{0x7000, false, 1, "Output 1", 1, Data::EtherCATDataType::Boolean, "BOOL"}},
        },
    };
    description.txPdos = {
        {
            0x1a00,
            false,
            "Inputs",
            Data::PdoDirection::Tx,
            3,
            true,
            true,
            {{0x6000, false, 1, "Input 1", 1, Data::EtherCATDataType::Boolean, "BOOL"}},
        },
    };
    description.coe.supported = true;
    context.deviceDescriptions = {description};
    context.mock = true;
    return context;
}

static Core::AutomationTopologyView realTopologyView(
    const Core::AutomationContextSnapshot &context,
    Core::TopologyEvidenceFreshness freshness = Core::TopologyEvidenceFreshness::Fresh)
{
    Core::AutomationTopologyView view;
    view.selection = {
        Core::TopologyEvidenceSource::RealController,
        Utils::Id("SECRET_REAL_TOPOLOGY_PROVIDER"),
        context.scope,
    };

    Data::ControllerTopologySlave slave;
    slave.position = 0;
    slave.stationAddress = 0x1001;
    slave.alState = 8;
    slave.vendorId = 0x00000002;
    slave.productCode = 0x20000001;
    slave.revision = 0x00000003;
    slave.serial = 29;

    Data::ControllerTopologySnapshot evidence;
    evidence.firstStationAddress = slave.stationAddress;
    evidence.respondingCount = 1;
    evidence.slaves = {slave};
    evidence.discoveredAt = QDateTime::fromString("2026-08-10T01:02:03.004Z", Qt::ISODateWithMs);
    evidence.scope = context.scope;
    evidence.sessionGeneration = 110001;
    evidence.sessionId = 220002;
    evidence.bootId = 330003;
    evidence.requestId = 440004;
    evidence.responseSequence = 550005;
    evidence.cpu1RequestSequence = 660006;
    evidence.topologyCaptureSequence = 770007;
    evidence.cpu1CompletedTimeNs = 880008;
    evidence.topologyCompletedTimeNs = 990009;
    evidence.receivedAt = QDateTime::fromString("2026-08-10T01:02:03.005Z", Qt::ISODateWithMs);

    Core::TopologySnapshot snapshot;
    snapshot.selection = view.selection;
    snapshot.freshness = freshness;
    snapshot.controllerEvidence = evidence;
    view.lookup = {Core::TopologyLookupStatus::Success, snapshot};
    return view;
}

static Core::AutomationTopologyView mockTopologyView(
    const Core::AutomationContextSnapshot &context,
    Core::TopologyEvidenceFreshness freshness = Core::TopologyEvidenceFreshness::Fresh)
{
    if (!context.scan)
        return {};

    Core::AutomationTopologyView view;
    view.selection = {
        Core::TopologyEvidenceSource::MockScan,
        Utils::Id("SECRET_MOCK_TOPOLOGY_PROVIDER"),
        context.scope,
    };
    Core::TopologySnapshot snapshot;
    snapshot.selection = view.selection;
    snapshot.freshness = freshness;
    snapshot.mockEvidence = *context.scan;
    view.lookup = {Core::TopologyLookupStatus::Success, snapshot};
    return view;
}

static Core::AutomationTopologyView missingTopologyView(
    const Core::AutomationContextSnapshot &context)
{
    Core::AutomationTopologyView view;
    view.selection = {
        Core::TopologyEvidenceSource::RealController,
        Utils::Id("SECRET_MISSING_TOPOLOGY_PROVIDER"),
        context.scope,
    };
    view.lookup.status = Core::TopologyLookupStatus::ProviderNotFound;
    return view;
}

static Data::SemanticRuntimeContext semanticRuntimeContext(
    const Core::AutomationContextSnapshot &automation)
{
    Data::SemanticRuntimeContext context;
    context.controllerId = automation.controllerId;
    context.scope = automation.scope;
    context.sessionGeneration = 7;
    context.epoch.controllerBootId = 11;
    context.epoch.activePackageSlot = Data::ControllerSlot::B;
    context.epoch.activePackageGeneration = 12;
    context.epoch.configurationId = 13;
    context.epoch.topologyGeneration = 14;
    context.epoch.runtimeGeneration = 15;
    context.epoch.catalogRevision = 16;
    context.epoch.topologyIdentity = QByteArray::fromHex("0102030405060708");
    context.mappingDigest = {"sha256", QByteArray(32, '\x5a')};
    context.controllerMappingDigest = context.mappingDigest;
    context.actionDefinitionsDigest = {"sha256", QByteArray(32, '\x6c')};
    context.cyclePeriodNs = 125000;
    context.bindingVerification.state = Data::SemanticBindingVerificationState::Verified;
    context.bindingVerification.verifierId = "gateway-test-verifier";
    context.bindingVerification.signedManifestDigest = {"sha256", QByteArray(32, '\x6b')};
    context.bindingVerification.verifiedAt = QDateTime::currentDateTimeUtc();
    context.contextHash = QByteArray(32, '\x7d');

    const Data::NodeId deviceId = automation.project.slaves.constFirst().id;
    Data::SemanticRuntimeTarget signalTarget;
    signalTarget.controllerId = context.controllerId;
    signalTarget.scope = context.scope;
    signalTarget.deviceId = deviceId;
    signalTarget.kind = Data::SemanticRuntimeTargetKind::Signal;
    signalTarget.signalId = {"embedlabs.test:manual.output.1"};

    Data::SemanticRuntimeBinding binding;
    binding.target = signalTarget;
    binding.semanticBindingId = "SECRET_SEMANTIC_BINDING";
    binding.componentBindingId = "SECRET_COMPONENT_BINDING";
    binding.adapterId = {"embedlabs.test:adapter"};
    binding.adapterVersion = "1.0.0";
    binding.adapterContentSha256 = QByteArray(32, '\x21');
    binding.esiSha256 = QByteArray(32, '\x32');
    binding.bindingArtifactSha256 = QByteArray(32, '\x43');
    binding.sessionGeneration = context.sessionGeneration;
    binding.epoch = context.epoch;
    binding.mappingDigest = context.mappingDigest;
    binding.controllerMappingDigest = context.controllerMappingDigest;
    binding.verification = context.bindingVerification;
    binding.resourceId = {QByteArrayLiteral("SECRET_RUNTIME_RESOURCE_ID")};
    binding.componentInstanceId = {QByteArrayLiteral("SECRET_COMPONENT_INSTANCE_ID")};
    binding.consistencyGroupId = {QByteArrayLiteral("SECRET_CONSISTENCY_GROUP_ID")};
    binding.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
    binding.valueTypeIdentity = "SECRET_VALUE_TYPE_IDENTITY";
    binding.bitWidth = 1;
    binding.direction = Data::RuntimeResourceDirection::Bidirectional;
    binding.access = Data::RuntimeResourceAccess::ReadWrite;

    Data::SemanticSignalRuntimeState signal;
    signal.target = signalTarget;
    signal.definition.id = signalTarget.signalId;
    signal.definition.direction = Data::SemanticSignalDirection::Bidirectional;
    signal.definition.access = Data::SemanticSignalAccess::ReadWrite;
    signal.definition.manualControl.allowed = true;
    signal.definition.manualControl.holdToRun = true;
    signal.definition.manualControl.commandTimeoutMs = 250;
    signal.availability = Data::SemanticSignalAvailability::Ready;
    signal.binding = binding;
    Data::RuntimeResourceTypedValue value;
    value.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
    value.typeIdentity = binding.valueTypeIdentity;
    value.value = true;
    signal.value = value;
    signal.quality.state = Data::RuntimeResourceQualityState::Good;
    signal.snapshotComplete = true;
    signal.captureCycle = 101;
    signal.controllerTimestampNs = 123456;
    context.signalStates = {signal};

    Data::SemanticActionRuntimeState disabledAction;
    disabledAction.target.controllerId = context.controllerId;
    disabledAction.target.scope = context.scope;
    disabledAction.target.deviceId = deviceId;
    disabledAction.target.kind = Data::SemanticRuntimeTargetKind::Action;
    disabledAction.target.actionId = {"embedlabs.test:manual.disabled-action"};
    disabledAction.definition.id = disabledAction.target.actionId;
    disabledAction.definition.enabled = false;
    disabledAction.availability = Data::SemanticActionAvailability::Rejected;
    disabledAction.requiresApproval = true;
    disabledAction.detail = "The adapter action is not qualified.";

    Data::SemanticActionRuntimeState readyAction;
    readyAction.target.controllerId = context.controllerId;
    readyAction.target.scope = context.scope;
    readyAction.target.deviceId = deviceId;
    readyAction.target.kind = Data::SemanticRuntimeTargetKind::Action;
    readyAction.target.actionId = {"embedlabs.test:manual.set-output"};
    readyAction.definition.id = readyAction.target.actionId;
    readyAction.definition.enabled = true;
    readyAction.actionBindingId = readyAction.target.actionId.value;
    readyAction.actionDefinitionId = "embedlabs.test:definition.set-output";
    readyAction.actionDefinitionDigest = {"sha256", QByteArray(32, '\x6d')};
    readyAction.qualification = Data::SemanticActionQualification::Qualified;
    readyAction.availability = Data::SemanticActionAvailability::Ready;
    readyAction.bindings = {binding};
    readyAction.requiresApproval = true;
    readyAction.maximumTtlCycles = 1000;
    context.actionStates = {disabledAction, readyAction};

    context.complete = true;
    context.mock = true;
    return context;
}

static QJsonObject withOperation(const QString &operationId, const QString &controllerId = {})
{
    QJsonObject result{{"operationId", operationId}};
    if (!controllerId.isEmpty())
        result.insert("controllerId", controllerId);
    return result;
}

struct HttpResult
{
    int status = 0;
    QByteArray sessionId;
    QByteArray body;
};

static HttpResult sendHttp(
    const QUrl &url,
    const QByteArray &method,
    const QByteArray &body = {},
    const QList<QPair<QByteArray, QByteArray>> &headers = {})
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    for (const auto &[name, value] : headers)
        request.setRawHeader(name, value);
    QNetworkReply *reply = nullptr;
    if (method == "GET")
        reply = manager.get(request);
    else
        reply = manager.sendCustomRequest(request, method, body);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(5000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    HttpResult result;
    if (reply->isFinished()) {
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.sessionId = reply->rawHeader("mcp-session-id");
        result.body = reply->readAll();
    }
    reply->deleteLater();
    return result;
}

static QList<QPair<QByteArray, QByteArray>> mcpHeaders(
    const QByteArray &sessionId = {}, const QByteArray &origin = {})
{
    QList<QPair<QByteArray, QByteArray>> result{
        {"Accept", "application/json, text/event-stream"},
        {"Content-Type", "application/json"},
        {"mcp-protocol-version", Constants::MCP_PROTOCOL_VERSION},
    };
    if (!sessionId.isEmpty())
        result.append({"mcp-session-id", sessionId});
    if (!origin.isEmpty())
        result.append({"Origin", origin});
    return result;
}

static QJsonObject jsonObject(const HttpResult &result)
{
    const QJsonDocument document = QJsonDocument::fromJson(result.body);
    return document.isObject() ? document.object() : QJsonObject{};
}

static quint16 unusedPort();

void EtherCATAutomationGatewayTests::testDefaultOffAndClosedToolCatalog()
{
    FakeAutomationService service;
    service.current = {mockContext()};
    GatewayServer server(&service);

    QVERIFY(!server.isRunning());
    QCOMPARE(server.mcpPort(), 0);
    QCOMPARE(server.restPort(), 0);
    QCOMPARE(server.registeredToolNames(), AutomationDispatcher::toolNames());

    const QStringList forbidden{
        "build",
        "debug",
        "shell",
        "controller.scan",
        "controller.connect",
        "controller.motion",
    };
    for (const QString &tool : forbidden)
        QVERIFY(!server.registeredToolNames().contains(tool));
}

static QVariant gatewaySetting(Utils::QtcSettings &settings, const Utils::Key &key)
{
    settings.beginGroup(Constants::SETTINGS_GROUP);
    const QVariant result = settings.value(key);
    settings.endGroup();
    return result;
}

void EtherCATAutomationGatewayTests::testPluginDiscoveryAndSettingsPageContract()
{
    const QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    QFile manifest(sourceDir.filePath("EtherCATAutomationGateway.json.in"));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QByteArray manifestText = manifest.readAll();
    QVERIFY(!manifestText.contains("DisabledByDefault"));
    QVERIFY(manifestText.contains("\"SoftLoadable\" : true"));

    ::Core::IOptionsPage *registeredPage = nullptr;
    for (::Core::IOptionsPage *page : ::Core::IOptionsPage::allOptionsPages()) {
        if (page->id() == Utils::Id(Constants::SETTINGS_PAGE_ID)) {
            registeredPage = page;
            break;
        }
    }
    QVERIFY(registeredPage);
    QCOMPARE(registeredPage->category(), Utils::Id(EtherCAT::Core::Constants::SETTINGS_CATEGORY));

    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    Utils::QtcSettings settings(settingsDirectory.filePath("gateway.ini"), QSettings::IniFormat);
    FakeAutomationService service;
    service.current = {mockContext()};
    GatewayRuntimeController runtime(&service, nullptr, &settings, false);
    const Utils::Result<> defaultStarted = runtime.startFromStoredConfiguration();
    QVERIFY_RESULT(defaultStarted);
    QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Stopped);
    QVERIFY(runtime.snapshot().mcpEndpoint.isEmpty());
    QVERIFY(runtime.snapshot().restEndpoint.isEmpty());
    QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());

    std::unique_ptr<::Core::IOptionsPage> page = createGatewaySettingsPage(&runtime);
    QCOMPARE(page->id(), Utils::Id(Constants::SETTINGS_PAGE_ID));
    QCOMPARE(page->category(), Utils::Id(EtherCAT::Core::Constants::SETTINGS_CATEGORY));
    std::unique_ptr<::Core::IOptionsPageWidget> widget(page->createWidget());
    QVERIFY(widget);
    QCOMPARE(widget->objectName(), Constants::SETTINGS_PAGE_OBJECT_NAME);
    QVERIFY(!widget->accessibleName().isEmpty());

    auto enabled = widget->findChild<QCheckBox *>(Constants::SETTINGS_ENABLED_OBJECT_NAME);
    auto address = widget->findChild<QLineEdit *>(Constants::SETTINGS_ADDRESS_OBJECT_NAME);
    auto mcpPort = widget->findChild<QSpinBox *>(Constants::SETTINGS_MCP_PORT_OBJECT_NAME);
    auto restPort = widget->findChild<QSpinBox *>(Constants::SETTINGS_REST_PORT_OBJECT_NAME);
    auto state = widget->findChild<QLineEdit *>(Constants::SETTINGS_STATE_OBJECT_NAME);
    auto mcpEndpoint = widget->findChild<QLineEdit *>(Constants::SETTINGS_MCP_ENDPOINT_OBJECT_NAME);
    auto restEndpoint = widget->findChild<QLineEdit *>(
        Constants::SETTINGS_REST_ENDPOINT_OBJECT_NAME);
    auto error = widget->findChild<QLabel *>(Constants::SETTINGS_ERROR_OBJECT_NAME);
    auto safety = widget->findChild<QLabel *>(Constants::SETTINGS_SAFETY_OBJECT_NAME);
    const QList<QWidget *> keyWidgets{
        enabled,
        address,
        mcpPort,
        restPort,
        state,
        mcpEndpoint,
        restEndpoint,
        error,
        safety,
    };
    for (QWidget *keyWidget : keyWidgets) {
        QVERIFY(keyWidget);
        QVERIFY2(!keyWidget->accessibleName().isEmpty(), qPrintable(keyWidget->objectName()));
    }
    QVERIFY(address->isReadOnly());
    QCOMPARE(address->text(), QString::fromLatin1(Constants::LOOPBACK_ADDRESS));
    QVERIFY(state->isReadOnly());
    QVERIFY(mcpEndpoint->isReadOnly());
    QVERIFY(restEndpoint->isReadOnly());
    QVERIFY(safety->text().contains("legacy controller views"));
    QVERIFY(safety->text().contains("Mock-only"));
    QVERIFY(safety->text().contains("Selected topology evidence"));
    QVERIFY(safety->text().contains("Real or Mock"));
    QVERIFY(safety->text().contains("never triggers a scan"));
    QVERIFY(safety->text().contains("read-only"));
    QCOMPARE(mcpPort->minimum(), 0);
    QCOMPARE(mcpPort->maximum(), 65535);
    QCOMPARE(restPort->minimum(), 0);
    QCOMPARE(restPort->maximum(), 65535);

    mcpPort->setValue(0);
    restPort->setValue(0);
    enabled->setChecked(true);
    widget->apply();
    QVERIFY2(
        runtime.snapshot().state == GatewayRuntimeState::Running,
        qPrintable(runtime.snapshot().lastError));
    QVERIFY(!runtime.snapshot().mcpEndpoint.isEmpty());
    QVERIFY(!runtime.snapshot().restEndpoint.isEmpty());
    QVERIFY(!mcpPort->isEnabled());
    QVERIFY(!restPort->isEnabled());
    QVERIFY(mcpEndpoint->text().contains("127.0.0.1"));
    QVERIFY(restEndpoint->text().contains("127.0.0.1"));
    QVERIFY(gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    QCOMPARE(gatewaySetting(settings, Constants::SETTINGS_MCP_PORT).toInt(), 0);
    QCOMPARE(gatewaySetting(settings, Constants::SETTINGS_REST_PORT).toInt(), 0);

    const quint16 actualMcpPort = runtime.snapshot().mcpEndpoint.port();
    const quint16 actualRestPort = runtime.snapshot().restEndpoint.port();
    QVERIFY(actualMcpPort != 0);
    QVERIFY(actualRestPort != 0);
    enabled->setChecked(false);
    widget->apply();
    QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Stopped);
    QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    QVERIFY(mcpPort->isEnabled());
    QVERIFY(restPort->isEnabled());
    QTcpServer mcpReuse;
    QTcpServer restReuse;
    QVERIFY(mcpReuse.listen(QHostAddress::LocalHost, actualMcpPort));
    QVERIFY(restReuse.listen(QHostAddress::LocalHost, actualRestPort));
}

void EtherCATAutomationGatewayTests::testRuntimeValidationRollbackAndRestart()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    Utils::QtcSettings settings(settingsDirectory.filePath("gateway.ini"), QSettings::IniFormat);
    FakeAutomationService service;
    service.current = {mockContext()};

    {
        GatewayRuntimeController runtime(&service, nullptr, &settings, false);
        const Utils::Result<> defaultStarted = runtime.startFromStoredConfiguration();
        QVERIFY_RESULT(defaultStarted);
        QVERIFY(!runtime.applyConfiguration({true, 65536, 0}));
        QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Failed);
        QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
        QVERIFY(!runtime.applyConfiguration({true, 12000, 12000}));
        QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    }

    const quint16 candidateMcpPort = unusedPort();
    QVERIFY(candidateMcpPort != 0);
    QTcpServer blockedRest;
    QVERIFY(blockedRest.listen(QHostAddress::LocalHost, 0));
    {
        GatewayRuntimeController runtime(&service, nullptr, &settings, false);
        const Utils::Result<> failed = runtime.applyConfiguration(
            {true, candidateMcpPort, blockedRest.serverPort()});
        QVERIFY(!failed);
        QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Failed);
        QVERIFY(runtime.snapshot().mcpEndpoint.isEmpty());
        QVERIFY(runtime.snapshot().restEndpoint.isEmpty());
        QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    }
    QTcpServer rolledBackMcp;
    QVERIFY(rolledBackMcp.listen(QHostAddress::LocalHost, candidateMcpPort));
    rolledBackMcp.close();
    blockedRest.close();

    QTcpServer blockedMcp;
    QVERIFY(blockedMcp.listen(QHostAddress::LocalHost, 0));
    const quint16 candidateRestPort = unusedPort();
    QVERIFY(candidateRestPort != 0);
    {
        GatewayRuntimeController runtime(&service, nullptr, &settings, false);
        const Utils::Result<> failed = runtime.applyConfiguration(
            {true, blockedMcp.serverPort(), candidateRestPort});
        QVERIFY(!failed);
        QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Failed);
        QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    }
    QTcpServer untouchedRest;
    QVERIFY(untouchedRest.listen(QHostAddress::LocalHost, candidateRestPort));
    untouchedRest.close();
    blockedMcp.close();

    const QString blockedSettingsParent = settingsDirectory.filePath("settings-parent-is-a-file");
    QFile blockedSettingsFile(blockedSettingsParent);
    QVERIFY(blockedSettingsFile.open(QIODevice::WriteOnly));
    blockedSettingsFile.close();
    Utils::QtcSettings
        unwritableSettings(blockedSettingsParent + "/gateway.ini", QSettings::IniFormat);
    QTcpServer persistenceMcpProbe;
    QTcpServer persistenceRestProbe;
    QVERIFY(persistenceMcpProbe.listen(QHostAddress::LocalHost, 0));
    QVERIFY(persistenceRestProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 persistenceMcpPort = persistenceMcpProbe.serverPort();
    const quint16 persistenceRestPort = persistenceRestProbe.serverPort();
    persistenceMcpProbe.close();
    persistenceRestProbe.close();
    QVERIFY(persistenceMcpPort != 0);
    QVERIFY(persistenceRestPort != 0);
    QVERIFY(persistenceMcpPort != persistenceRestPort);
    {
        GatewayRuntimeController runtime(&service, nullptr, &unwritableSettings, false);
        const Utils::Result<> failed = runtime.applyConfiguration(
            {true, persistenceMcpPort, persistenceRestPort});
        QVERIFY(!failed);
        QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Failed);
        QVERIFY(runtime.snapshot().mcpEndpoint.isEmpty());
        QVERIFY(runtime.snapshot().restEndpoint.isEmpty());
        QVERIFY(!gatewaySetting(unwritableSettings, Constants::SETTINGS_ENABLED).toBool());
    }
    QTcpServer persistenceMcpReuse;
    QTcpServer persistenceRestReuse;
    QVERIFY(persistenceMcpReuse.listen(QHostAddress::LocalHost, persistenceMcpPort));
    QVERIFY(persistenceRestReuse.listen(QHostAddress::LocalHost, persistenceRestPort));

    settings.beginGroup(Constants::SETTINGS_GROUP);
    settings.setValue(Constants::SETTINGS_ENABLED, true);
    settings.setValue(Constants::SETTINGS_MCP_PORT, 0);
    settings.setValue(Constants::SETTINGS_REST_PORT, 0);
    settings.endGroup();
    settings.sync();
    QVERIFY(gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    QCOMPARE(gatewaySetting(settings, Constants::SETTINGS_MCP_PORT).toInt(), 0);
    QCOMPARE(gatewaySetting(settings, Constants::SETTINGS_REST_PORT).toInt(), 0);
    QCOMPARE(settings.group(), QString());
    settings.beginGroup(Constants::SETTINGS_GROUP);
    QVERIFY(settings.value(Constants::SETTINGS_ENABLED, false).toBool());
    settings.endGroup();

    quint16 actualMcpPort = 0;
    quint16 actualRestPort = 0;
    {
        GatewayRuntimeController restored(&service, nullptr, &settings, false);
        const Utils::Result<> restoredStarted = restored.startFromStoredConfiguration();
        QVERIFY_RESULT(restoredStarted);
        QCOMPARE(restored.snapshot().state, GatewayRuntimeState::Running);
        actualMcpPort = restored.snapshot().mcpEndpoint.port();
        actualRestPort = restored.snapshot().restEndpoint.port();
        QVERIFY(actualMcpPort != 0);
        QVERIFY(actualRestPort != 0);
        const Utils::Result<> repeatedStart = restored.startFromStoredConfiguration();
        QVERIFY_RESULT(repeatedStart);
        QCOMPARE(restored.snapshot().state, GatewayRuntimeState::Running);
        QCOMPARE(restored.snapshot().mcpEndpoint.port(), actualMcpPort);
        QCOMPARE(restored.snapshot().restEndpoint.port(), actualRestPort);
        restored.shutdown();
        QCOMPARE(restored.snapshot().state, GatewayRuntimeState::Stopped);
        QVERIFY(gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
    }
    QTcpServer releasedMcp;
    QTcpServer releasedRest;
    QVERIFY(releasedMcp.listen(QHostAddress::LocalHost, actualMcpPort));
    QVERIFY(releasedRest.listen(QHostAddress::LocalHost, actualRestPort));
}

void EtherCATAutomationGatewayTests::testUnavailableRuntimeStaysDisabled()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    Utils::QtcSettings settings(settingsDirectory.filePath("gateway.ini"), QSettings::IniFormat);
    settings.beginGroup(Constants::SETTINGS_GROUP);
    settings.setValue(Constants::SETTINGS_ENABLED, true);
    settings.setValue(Constants::SETTINGS_MCP_PORT, 0);
    settings.setValue(Constants::SETTINGS_REST_PORT, 0);
    settings.endGroup();

    GatewayRuntimeController runtime(nullptr, nullptr, &settings, false);
    QVERIFY(!runtime.startFromStoredConfiguration());
    QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Unavailable);
    QVERIFY(runtime.snapshot().mcpEndpoint.isEmpty());
    QVERIFY(runtime.snapshot().restEndpoint.isEmpty());
    QVERIFY(!gatewaySetting(settings, Constants::SETTINGS_ENABLED).toBool());
}

void EtherCATAutomationGatewayTests::testWorkbenchPublishesSingleAutomationService()
{
    Core::AutomationService *automation = nullptr;
    int automationServiceCount = 0;
    for (QObject *object : ExtensionSystem::PluginManager::allObjects()) {
        if (auto candidate = qobject_cast<Core::AutomationService *>(object)) {
            automation = candidate;
            ++automationServiceCount;
        }
    }
    QCOMPARE(automationServiceCount, 1);
    QVERIFY(automation);
    QCOMPARE(ExtensionSystem::PluginManager::getObject<Core::AutomationService>(), automation);
}

void EtherCATAutomationGatewayTests::testSharedSnapshotUpdatesWithoutGatewayCache()
{
    FakeAutomationService service;
    const Core::AutomationContextSnapshot initial = mockContext();
    service.current = {initial};
    AutomationDispatcher dispatcher(&service);

    const QJsonObject listed = dispatcher.dispatch(
        "controller.list", withOperation("snapshot-list"), {"ui-semantic-test", {}, {}, {}});
    QVERIFY(listed.value("ok").toBool());
    const QJsonObject controller
        = listed.value("data").toObject().value("controllers").toArray().at(0).toObject();
    QCOMPARE(controller.value("contextHash").toString(), AutomationDispatcher::snapshotHash(initial));
    QCOMPARE(service.readCount, 1);

    Core::AutomationContextSnapshot updated = initial;
    updated.project.name = "Updated by ProjectService";
    updated.project.modified = true;
    updated.connection.controllerState->cycleCount = 44;
    service.current = {updated};
    const QJsonObject state = dispatcher.dispatch(
        "controller.get-state",
        withOperation("snapshot-updated", initial.controllerId),
        {"ui-semantic-test", {}, {}, {}});
    QVERIFY(state.value("ok").toBool());
    QCOMPARE(
        state.value("data").toObject().value("contextHash").toString(),
        AutomationDispatcher::snapshotHash(updated));
    QVERIFY(
        state.value("data").toObject().value("contextHash").toString()
        != controller.value("contextHash").toString());
    QCOMPARE(service.readCount, 2);

    Core::AutomationContextSnapshot offlineStation = initial;
    offlineStation.scan.reset();
    Core::AutomationContextSnapshot changedStation = offlineStation;
    ++changedStation.project.slaves.first().stationAddress;
    QVERIFY(
        AutomationDispatcher::snapshotHash(offlineStation)
        != AutomationDispatcher::snapshotHash(changedStation));

    service.current.clear();
    const QJsonObject closed = dispatcher.dispatch(
        "controller.get-state",
        withOperation("snapshot-closed", initial.controllerId),
        {"ui-semantic-test", {}, {}, {}});
    QVERIFY(!closed.value("ok").toBool());
    QCOMPARE(closed.value("error").toObject().value("code").toString(), "CT009_NOT_FOUND");
    QCOMPARE(service.readCount, 3);
}

void EtherCATAutomationGatewayTests::testOperationJournalAcrossTransports()
{
    FakeAutomationService service;
    const Core::AutomationContextSnapshot context = mockContext();
    service.current = {context};
    AutomationDispatcher dispatcher(&service);
    const QJsonObject invalidOperation
        = dispatcher.dispatch("controller.list", {{"operationId", ""}}, {"rest", {}, {}, {}});
    QVERIFY(!invalidOperation.value("ok").toBool());
    QCOMPARE(invalidOperation.value("error").toObject().value("code").toString(), "CT010_BAD_REQUEST");
    QCOMPARE(service.readCount, 0);

    const QJsonObject arguments = withOperation("shared-operation", context.controllerId);

    const QJsonObject fromMcp
        = dispatcher
              .dispatch("controller.get-state", arguments, {"mcp", "mcp-session", "client-a", "1"});
    const int readsAfterMcp = service.readCount;
    const QJsonObject fromRest = dispatcher.dispatch(
        "controller.get-state", arguments, {"rest", "rest-session", "client-b", "2"});
    QCOMPARE(fromRest, fromMcp);
    QCOMPARE(service.readCount, readsAfterMcp);

    QJsonObject conflictArguments = arguments;
    conflictArguments.insert("controllerId", "ide:other:controller");
    const QJsonObject conflict
        = dispatcher.dispatch("controller.get-state", conflictArguments, {"rest", {}, {}, {}});
    QVERIFY(!conflict.value("ok").toBool());
    QCOMPARE(
        conflict.value("error").toObject().value("details").toObject().value("reason").toString(),
        "operation-id-conflict");
    QCOMPARE(service.readCount, readsAfterMcp);
}

void EtherCATAutomationGatewayTests::testSelectedTopologyEvidenceOrderingAndRedaction()
{
    FakeAutomationService service;
    Core::AutomationContextSnapshot first = mockContext();
    Core::AutomationContextSnapshot second = mockContext();
    first.topologyViews = {mockTopologyView(first), realTopologyView(first)};
    second.topologyViews = {mockTopologyView(second), realTopologyView(second)};
    service.current = {second, first};
    AutomationDispatcher dispatcher(&service);

    const QJsonObject response = dispatcher.dispatch(
        "topology.list-selected",
        withOperation("selected-topology-order-a"),
        {"mcp", "topology-session", "topology-client", "1"});
    QVERIFY(response.value("ok").toBool());
    const QJsonObject data = response.value("data").toObject();
    QCOMPARE(data.value("apiVersion").toString(), "selected-topology-evidence/v1");
    QVERIFY(data.value("readOnly").toBool());
    QVERIFY(!data.value("scanTriggeredByRequest").toBool());
    const QJsonArray topologies = data.value("topologies").toArray();
    QCOMPARE(topologies.size(), 4);

    QStringList expectedControllers{first.controllerId, second.controllerId};
    expectedControllers.sort();
    for (int controllerIndex = 0; controllerIndex < expectedControllers.size(); ++controllerIndex) {
        const QJsonObject real = topologies.at(controllerIndex * 2).toObject();
        const QJsonObject mock = topologies.at(controllerIndex * 2 + 1).toObject();
        QCOMPARE(real.value("controllerId").toString(), expectedControllers.at(controllerIndex));
        QCOMPARE(mock.value("controllerId").toString(), expectedControllers.at(controllerIndex));
        QCOMPARE(real.value("source").toString(), "real-controller");
        QCOMPARE(mock.value("source").toString(), "mock-scan");
        QCOMPARE(real.value("lookupStatus").toString(), "success");
        QCOMPARE(mock.value("lookupStatus").toString(), "success");
        QCOMPARE(real.value("freshness").toString(), "fresh");
        QCOMPARE(mock.value("freshness").toString(), "fresh");
        QVERIFY(real.value("current").toBool());
        QVERIFY(mock.value("current").toBool());
        QCOMPARE(real.value("generationHash").toString().size(), 64);
        QCOMPARE(mock.value("generationHash").toString().size(), 64);
        QCOMPARE(real.value("evidenceHash").toString().size(), 64);
        QCOMPARE(mock.value("evidenceHash").toString().size(), 64);
        QCOMPARE(real.value("slaves").toArray().size(), 1);
        QCOMPARE(mock.value("slaves").toArray().size(), 1);
        QVERIFY(!real.value("slaves").toArray().first().toObject().contains("name"));
        QCOMPARE(
            mock.value("slaves").toArray().first().toObject().value("name").toString(),
            "Mock Digital I/O");
    }

    std::reverse(first.topologyViews.begin(), first.topologyViews.end());
    std::reverse(second.topologyViews.begin(), second.topologyViews.end());
    service.current = {first, second};
    const QJsonObject reordered = dispatcher.dispatch(
        "topology.list-selected",
        withOperation("selected-topology-order-b"),
        {"rest", "topology-session-2", "topology-client", "1"});
    QVERIFY(reordered.value("ok").toBool());
    QCOMPARE(reordered.value("data").toObject(), data);

    const QByteArray encoded = QJsonDocument(data).toJson(QJsonDocument::Compact);
    const QList<QByteArray> hiddenGenerationFields{
        "sessionGeneration",
        "sessionId",
        "bootId",
        "requestId",
        "responseSequence",
        "cpu1RequestSequence",
        "topologyCaptureSequence",
        "snapshotId",
        "providerId",
        "profileId",
        "endpoint",
    };
    for (const QByteArray &field : hiddenGenerationFields) {
        const QByteArray encodedField = QByteArrayLiteral("\"") + field + '"';
        QVERIFY2(!encoded.contains(encodedField), field.constData());
    }
    const QList<QByteArray> forbiddenValues{
        "SECRET_REAL_TOPOLOGY_PROVIDER",
        "SECRET_MOCK_TOPOLOGY_PROVIDER",
    };
    for (const QByteArray &value : forbiddenValues)
        QVERIFY2(!encoded.contains(value), value.constData());

    Core::AutomationContextSnapshot generationContext = first;
    Core::AutomationTopologyView baseView = realTopologyView(generationContext);
    generationContext.topologyViews = {baseView};
    service.current = {generationContext};
    const QJsonObject baseResponse = dispatcher.dispatch(
        "topology.list-selected", withOperation("selected-topology-generation-base"), {});
    QVERIFY(baseResponse.value("ok").toBool());
    const QString baseGenerationHash = baseResponse.value("data")
                                           .toObject()
                                           .value("topologies")
                                           .toArray()
                                           .first()
                                           .toObject()
                                           .value("generationHash")
                                           .toString();
    QCOMPARE(baseGenerationHash.size(), 64);

    for (int field = 0; field < 7; ++field) {
        Core::AutomationTopologyView changed = baseView;
        Data::ControllerTopologySnapshot &evidence
            = *changed.lookup.snapshot->controllerEvidence;
        switch (field) {
        case 0:
            ++evidence.sessionGeneration;
            break;
        case 1:
            ++evidence.sessionId;
            break;
        case 2:
            ++evidence.bootId;
            break;
        case 3:
            ++evidence.requestId;
            break;
        case 4:
            ++evidence.responseSequence;
            break;
        case 5:
            ++evidence.cpu1RequestSequence;
            break;
        case 6:
            ++evidence.topologyCaptureSequence;
            break;
        }
        generationContext.topologyViews = {changed};
        service.current = {generationContext};
        const QJsonObject changedResponse = dispatcher.dispatch(
            "topology.list-selected",
            withOperation(QString("selected-topology-generation-%1").arg(field)),
            {});
        QVERIFY(changedResponse.value("ok").toBool());
        const QJsonObject record = changedResponse.value("data")
                                       .toObject()
                                       .value("topologies")
                                       .toArray()
                                       .first()
                                       .toObject();
        QCOMPARE(record.value("generationHash").toString().size(), 64);
        QVERIFY(record.value("generationHash").toString() != baseGenerationHash);
    }
}

void EtherCATAutomationGatewayTests::testSelectedTopologyEvidenceFailureClosure()
{
    FakeAutomationService service;
    Core::AutomationContextSnapshot stale = mockContext();
    Core::AutomationContextSnapshot incomplete = mockContext();
    Core::AutomationContextSnapshot missing = mockContext();
    stale.topologyViews = {
        realTopologyView(stale, Core::TopologyEvidenceFreshness::Stale),
    };
    incomplete.topologyViews = {
        mockTopologyView(incomplete, Core::TopologyEvidenceFreshness::Incomplete),
    };
    missing.topologyViews = {missingTopologyView(missing)};
    service.current = {missing, incomplete, stale};
    AutomationDispatcher dispatcher(&service);

    const QJsonObject unavailable = dispatcher.dispatch(
        "topology.list-selected", withOperation("selected-topology-unavailable"), {});
    QVERIFY(unavailable.value("ok").toBool());
    const QJsonArray records
        = unavailable.value("data").toObject().value("topologies").toArray();
    QCOMPARE(records.size(), 3);
    QSet<QString> states;
    for (const QJsonValue &value : records) {
        const QJsonObject record = value.toObject();
        QVERIFY(!record.value("current").toBool());
        QVERIFY(!record.contains("slaves"));
        QVERIFY(!record.contains("generationHash"));
        QVERIFY(!record.contains("evidenceHash"));
        QVERIFY(!record.contains("observedAt"));
        QVERIFY(!record.contains("complete"));
        states.insert(
            record.value("lookupStatus").toString() + ':'
            + record.value("freshness").toString());
    }
    const QSet<QString> expectedStates{
        "success:stale",
        "success:incomplete",
        "provider-not-found:unavailable",
    };
    QCOMPARE(states, expectedStates);

    Core::AutomationContextSnapshot duplicateSource = mockContext();
    duplicateSource.topologyViews = {
        realTopologyView(duplicateSource),
        realTopologyView(duplicateSource),
    };
    service.current = {duplicateSource};
    const QJsonObject sourceRejected = dispatcher.dispatch(
        "topology.list-selected", withOperation("selected-topology-duplicate-source"), {});
    QVERIFY(!sourceRejected.value("ok").toBool());
    QCOMPARE(
        sourceRejected.value("error").toObject().value("code").toString(),
        "CT024_TOPOLOGY_EVIDENCE_UNAVAILABLE");
    QCOMPARE(
        sourceRejected.value("error")
            .toObject()
            .value("details")
            .toObject()
            .value("reason")
            .toString(),
        "invalid-selected-evidence");

    Core::AutomationContextSnapshot wrongScope = mockContext();
    Core::AutomationTopologyView wrongScopeView = realTopologyView(wrongScope);
    wrongScopeView.selection.scope.masterId = Data::NodeId::create();
    wrongScopeView.lookup.snapshot->selection = wrongScopeView.selection;
    wrongScopeView.lookup.snapshot->controllerEvidence->scope = wrongScopeView.selection.scope;
    QVERIFY(wrongScopeView.isValid());
    wrongScope.topologyViews = {wrongScopeView};
    service.current = {wrongScope};
    const QJsonObject scopeRejected = dispatcher.dispatch(
        "topology.list-selected", withOperation("selected-topology-wrong-scope"), {});
    QVERIFY(!scopeRejected.value("ok").toBool());
    QCOMPARE(
        scopeRejected.value("error").toObject().value("code").toString(),
        "CT024_TOPOLOGY_EVIDENCE_UNAVAILABLE");
    QCOMPARE(
        scopeRejected.value("error")
            .toObject()
            .value("details")
            .toObject()
            .value("reason")
            .toString(),
        "invalid-selected-evidence");

    Core::AutomationContextSnapshot duplicateController = mockContext();
    duplicateController.topologyViews = {mockTopologyView(duplicateController)};
    service.current = {duplicateController, duplicateController};
    const QJsonObject controllerRejected = dispatcher.dispatch(
        "topology.list-selected", withOperation("selected-topology-duplicate-controller"), {});
    QVERIFY(!controllerRejected.value("ok").toBool());
    QCOMPARE(
        controllerRejected.value("error").toObject().value("code").toString(),
        "CT024_TOPOLOGY_EVIDENCE_UNAVAILABLE");
    QCOMPARE(
        controllerRejected.value("error")
            .toObject()
            .value("details")
            .toObject()
            .value("reason")
            .toString(),
        "duplicate-controller-id");

    const auto expectRejected = [&service, &dispatcher](
                                    const Core::AutomationContextSnapshot &context,
                                    const QString &operationId,
                                    const QString &reason) {
        service.current = {context};
        const QJsonObject response = dispatcher.dispatch(
            "topology.list-selected", withOperation(operationId), {"mcp", {}, {}, {}});
        QVERIFY(!response.value("ok").toBool());
        QCOMPARE(
            response.value("error").toObject().value("code").toString(),
            "CT024_TOPOLOGY_EVIDENCE_UNAVAILABLE");
        QCOMPARE(
            response.value("error")
                .toObject()
                .value("details")
                .toObject()
                .value("reason")
                .toString(),
            reason);
    };

    Core::AutomationContextSnapshot invalidRealPosition = mockContext();
    Core::AutomationTopologyView invalidRealPositionView = realTopologyView(invalidRealPosition);
    invalidRealPositionView.lookup.snapshot->controllerEvidence->slaves[0].position = 65536;
    invalidRealPosition.topologyViews = {invalidRealPositionView};
    expectRejected(
        invalidRealPosition, "selected-topology-real-position", "evidence-field-invalid");

    Core::AutomationContextSnapshot invalidRealAlState = mockContext();
    Core::AutomationTopologyView invalidRealAlStateView = realTopologyView(invalidRealAlState);
    invalidRealAlStateView.lookup.snapshot->controllerEvidence->slaves[0].alState = 256;
    invalidRealAlState.topologyViews = {invalidRealAlStateView};
    expectRejected(
        invalidRealAlState, "selected-topology-real-al-state", "evidence-field-invalid");

    Core::AutomationContextSnapshot invalidMockPosition = mockContext();
    Core::AutomationTopologyView invalidMockPositionView = mockTopologyView(invalidMockPosition);
    invalidMockPositionView.lookup.snapshot->mockEvidence->snapshot.slaves[0].position = -1;
    invalidMockPosition.topologyViews = {invalidMockPositionView};
    expectRejected(
        invalidMockPosition, "selected-topology-mock-position-negative", "evidence-field-invalid");
    invalidMockPositionView.lookup.snapshot->mockEvidence->snapshot.slaves[0].position = 65536;
    invalidMockPosition.topologyViews = {invalidMockPositionView};
    expectRejected(
        invalidMockPosition, "selected-topology-mock-position-high", "evidence-field-invalid");

    Core::AutomationContextSnapshot failedRealResult = mockContext();
    Core::AutomationTopologyView failedRealResultView = realTopologyView(failedRealResult);
    failedRealResultView.lookup.snapshot->controllerEvidence->result = -1;
    failedRealResult.topologyViews = {failedRealResultView};
    expectRejected(failedRealResult, "selected-topology-real-result", "evidence-incomplete");

    Core::AutomationContextSnapshot incompleteRealCount = mockContext();
    Core::AutomationTopologyView incompleteRealCountView = realTopologyView(incompleteRealCount);
    incompleteRealCountView.lookup.snapshot->controllerEvidence->respondingCount = 2;
    incompleteRealCount.topologyViews = {incompleteRealCountView};
    expectRejected(
        incompleteRealCount, "selected-topology-real-count", "evidence-incomplete");

    Core::AutomationContextSnapshot boundary = mockContext();
    Core::AutomationTopologyView boundaryReal = realTopologyView(boundary);
    boundaryReal.lookup.snapshot->controllerEvidence->slaves[0].position = 65535;
    boundaryReal.lookup.snapshot->controllerEvidence->slaves[0].alState = 255;
    Core::AutomationTopologyView boundaryMock = mockTopologyView(boundary);
    boundaryMock.lookup.snapshot->mockEvidence->snapshot.slaves[0].position = 65535;
    boundary.topologyViews = {boundaryReal, boundaryMock};
    service.current = {boundary};
    const QJsonObject boundaryAccepted = dispatcher.dispatch(
        "topology.list-selected",
        withOperation("selected-topology-public-boundaries"),
        {"mcp", {}, {}, {}});
    QVERIFY(boundaryAccepted.value("ok").toBool());

    Core::AutomationContextSnapshot aggregate = mockContext();
    Core::AutomationTopologyView aggregateReal = realTopologyView(aggregate);
    Data::ControllerTopologySlave realSeed
        = aggregateReal.lookup.snapshot->controllerEvidence->slaves.constFirst();
    QList<Data::ControllerTopologySlave> realSlaves;
    realSlaves.reserve(2048);
    for (int position = 0; position < 2048; ++position) {
        realSeed.position = quint32(position);
        realSlaves.append(realSeed);
    }
    aggregateReal.lookup.snapshot->controllerEvidence->slaves = realSlaves;
    aggregateReal.lookup.snapshot->controllerEvidence->respondingCount = quint32(realSlaves.size());

    Core::AutomationTopologyView aggregateMock = mockTopologyView(aggregate);
    Data::ScannedSlave mockSeed
        = aggregateMock.lookup.snapshot->mockEvidence->snapshot.slaves.constFirst();
    QList<Data::ScannedSlave> mockSlaves;
    mockSlaves.reserve(2049);
    for (int position = 0; position < 2049; ++position) {
        mockSeed.position = position;
        mockSlaves.append(mockSeed);
    }
    aggregateMock.lookup.snapshot->mockEvidence->snapshot.slaves = mockSlaves;
    aggregate.topologyViews = {aggregateReal, aggregateMock};
    expectRejected(
        aggregate, "selected-topology-aggregate-limit", "aggregate-topology-limit-exceeded");

    Core::AutomationContextSnapshot oversizedResponse = mockContext();
    Core::AutomationTopologyView oversizedView = mockTopologyView(oversizedResponse);
    mockSeed = oversizedView.lookup.snapshot->mockEvidence->snapshot.slaves.constFirst();
    mockSeed.name = QString(256, QLatin1Char('\\'));
    mockSlaves.clear();
    mockSlaves.reserve(4096);
    for (int position = 0; position < 4096; ++position) {
        mockSeed.position = position;
        mockSlaves.append(mockSeed);
    }
    oversizedView.lookup.snapshot->mockEvidence->snapshot.slaves = mockSlaves;
    oversizedResponse.topologyViews = {oversizedView};
    expectRejected(
        oversizedResponse, "selected-topology-response-limit", "response-limit-exceeded");
}

void EtherCATAutomationGatewayTests::testSelectedTopologyJournalAndLegacyIsolation()
{
    FakeAutomationService service;
    Core::AutomationContextSnapshot real = mockContext();
    real.mock = false;
    real.connection.mock = false;
    real.topologyViews = {realTopologyView(real)};
    service.current = {real};

    FakeProviderCounters provider;
    CountingConnectionProvider connectionProvider(&provider);
    CountingScanProvider scanProvider(&provider);
    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    ExtensionSystem::PluginManager::addObject(&scanProvider);
    const QScopeGuard removeProviders([&] {
        ExtensionSystem::PluginManager::removeObject(&scanProvider);
        ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    });

    AutomationDispatcher dispatcher(&service);
    const QJsonObject arguments = withOperation("selected-topology-replay");
    const QJsonObject first = dispatcher.dispatch(
        "topology.list-selected",
        arguments,
        {"mcp", "selected-topology-session", "selected-topology-client", "1"});
    QVERIFY(first.value("ok").toBool());
    const int readsAfterFirst = service.readCount;
    const QJsonObject replayed = dispatcher.dispatch(
        "topology.list-selected",
        arguments,
        {"rest", "different-session", "different-client", "2"});
    QCOMPARE(replayed, first);
    QCOMPARE(service.readCount, readsAfterFirst);

    const QJsonObject listed
        = dispatcher.dispatch("controller.list", withOperation("legacy-real-list"), {});
    QVERIFY(listed.value("ok").toBool());
    QVERIFY(listed.value("data").toObject().value("controllers").toArray().isEmpty());

    const QStringList legacyTools{
        "controller.get-capabilities",
        "controller.get-device",
        "controller.get-diagnostics",
        "controller.get-state",
        "controller.get-topology",
    };
    for (const QString &tool : legacyTools) {
        QJsonObject legacyArguments
            = withOperation("legacy-real-" + tool, real.controllerId);
        if (tool == "controller.get-device")
            legacyArguments.insert("position", 0);
        const QJsonObject rejected = dispatcher.dispatch(tool, legacyArguments, {});
        QVERIFY(!rejected.value("ok").toBool());
        QCOMPARE(
            rejected.value("error").toObject().value("code").toString(), "CT008_READ_ONLY");
        QCOMPARE(
            rejected.value("error")
                .toObject()
                .value("details")
                .toObject()
                .value("reason")
                .toString(),
            "approval-required");
    }

    const QByteArray encoded
        = QJsonDocument(first.value("data").toObject()).toJson(QJsonDocument::Compact);
    QVERIFY(!encoded.contains("SECRET_REAL_TOPOLOGY_PROVIDER"));
    QVERIFY(!encoded.contains("sessionGeneration"));
    QVERIFY(!encoded.contains("sessionId"));
    QVERIFY(!encoded.contains("bootId"));
    QVERIFY(!encoded.contains("requestId"));

    Core::AutomationContextSnapshot journalContext = mockContext();
    Core::AutomationTopologyView journalView = mockTopologyView(journalContext);
    Data::ScannedSlave journalSlave
        = journalView.lookup.snapshot->mockEvidence->snapshot.slaves.constFirst();
    journalSlave.name = QString(256, QLatin1Char('\\'));
    QList<Data::ScannedSlave> journalSlaves;
    journalSlaves.reserve(1024);
    for (int position = 0; position < 1024; ++position) {
        journalSlave.position = position;
        journalSlaves.append(journalSlave);
    }
    journalView.lookup.snapshot->mockEvidence->snapshot.slaves = journalSlaves;
    journalContext.topologyViews = {journalView};
    service.current = {journalContext};

    const QString firstBudgetOperation = "selected-topology-budget-0";
    const QJsonObject firstBudgetResponse = dispatcher.dispatch(
        "topology.list-selected",
        withOperation(firstBudgetOperation),
        {"mcp", "bounded-journal", "topology-client", "1"});
    QVERIFY(firstBudgetResponse.value("ok").toBool());
    const qsizetype budgetResponseBytes
        = QJsonDocument(firstBudgetResponse).toJson(QJsonDocument::Compact).size();
    QVERIFY(budgetResponseBytes > 0);
    QVERIFY(budgetResponseBytes < 2 * 1024 * 1024);
    const int additionalBudgetResponses = int((8 * 1024 * 1024) / budgetResponseBytes) + 2;
    QVERIFY(additionalBudgetResponses < 128);

    QString latestBudgetOperation;
    QJsonObject latestBudgetResponse;
    for (int index = 1; index <= additionalBudgetResponses; ++index) {
        latestBudgetOperation = QString("selected-topology-budget-%1").arg(index);
        latestBudgetResponse = dispatcher.dispatch(
            "topology.list-selected",
            withOperation(latestBudgetOperation),
            {"mcp", "bounded-journal", "topology-client", "1"});
        QVERIFY(latestBudgetResponse.value("ok").toBool());
    }

    const int readsBeforeLatestReplay = service.readCount;
    QCOMPARE(
        dispatcher.dispatch(
            "topology.list-selected",
            withOperation(latestBudgetOperation),
            {"rest", "different-session", "different-client", "2"}),
        latestBudgetResponse);
    QCOMPARE(service.readCount, readsBeforeLatestReplay);

    const int readsBeforeEvictedReplay = service.readCount;
    const QJsonObject recomputedFirst = dispatcher.dispatch(
        "topology.list-selected",
        withOperation(firstBudgetOperation),
        {"mcp", "bounded-journal", "topology-client", "1"});
    QVERIFY(recomputedFirst.value("ok").toBool());
    QCOMPARE(service.readCount, readsBeforeEvictedReplay + 1);

    QCOMPARE(provider.connect, 0);
    QCOMPARE(provider.control, 0);
    QCOMPARE(provider.scan, 0);
    QCOMPARE(provider.write, 0);
}

void EtherCATAutomationGatewayTests::testMutationsAreRejectedWithoutProviderCalls()
{
    FakeAutomationService service;
    service.current = {mockContext()};
    FakeProviderCounters provider;
    CountingConnectionProvider connectionProvider(&provider);
    CountingScanProvider scanProvider(&provider);
    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    ExtensionSystem::PluginManager::addObject(&scanProvider);
    const QScopeGuard removeProviders([&] {
        ExtensionSystem::PluginManager::removeObject(&scanProvider);
        ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    });
    AutomationDispatcher dispatcher(&service);
    const QStringList mutations{
        "controller.ip.configure",
        "controller.connect",
        "controller.lease.acquire",
        "controller.scan",
        "controller.configuration.apply",
        "controller.deploy",
        "controller.motion",
    };
    int operation = 0;
    for (const QString &mutation : mutations) {
        const QJsonObject response = dispatcher.rejectMutation(
            mutation, withOperation(QString("deny-%1").arg(++operation)), {"rest", {}, "test", "1"});
        QVERIFY(!response.value("ok").toBool());
        QCOMPARE(response.value("error").toObject().value("code").toString(), "CT008_READ_ONLY");
        QCOMPARE(
            response.value("error").toObject().value("details").toObject().value("reason").toString(),
            "approval-required");
    }
    QCOMPARE(provider.connect, 0);
    QCOMPARE(provider.control, 0);
    QCOMPARE(provider.scan, 0);
    QCOMPARE(provider.write, 0);
    QCOMPARE(service.readCount, 0);
}

void EtherCATAutomationGatewayTests::testVendorDetailsAreNotProjected()
{
    FakeAutomationService service;
    Core::AutomationContextSnapshot context = mockContext();
    Data::ControllerOperationError error;
    error.channelId = "SECRET_PRODUCT_API_CHANNEL";
    error.codeName = "SECRET_VENDOR_ERROR";
    error.summary = "SECRET_PROVIDER_CLASS";
    error.detail = "SECRET_RAW_REGISTER";
    context.connection.lastError = error;
    context.scan.reset();
    context.deviceDescriptions[0].sourcePath = "SECRET_ESI_LOCAL_PATH";
    context.deviceDescriptions[0].startupParameters = {
        {"PS", 0x2000, 1, QByteArray::fromHex("deadbeef"), "SECRET_SDO"},
    };
    service.current = {context};
    AutomationDispatcher dispatcher(&service);

    QJsonArray responses;
    const QStringList redactedTools{
        "controller.get-state",
        "controller.get-topology",
        "controller.get-diagnostics",
    };
    for (const QString &tool : redactedTools) {
        responses.append(dispatcher.dispatch(
            tool, withOperation("redaction-" + tool, context.controllerId), {"mcp", {}, {}, {}}));
    }
    QJsonObject deviceArguments = withOperation("redaction-device", context.controllerId);
    deviceArguments.insert("position", 0);
    responses.append(
        dispatcher.dispatch("controller.get-device", deviceArguments, {"mcp", {}, {}, {}}));
    const QJsonObject topology = responses.at(1)
                                     .toObject()
                                     .value("data")
                                     .toObject()
                                     .value("topology")
                                     .toObject();
    QCOMPARE(
        topology.value("slaves").toArray().first().toObject().value("stationAddress").toInt(),
        0x1001);
    const QJsonObject device = responses.last()
                                   .toObject()
                                   .value("data")
                                   .toObject()
                                   .value("device")
                                   .toObject();
    QCOMPARE(device.value("stationAddress").toInt(), 0x1001);
    const QByteArray encoded = QJsonDocument(responses).toJson(QJsonDocument::Compact);
    const QList<QByteArray> forbiddenValues{
        "SECRET_PRODUCT_API_CHANNEL",
        "SECRET_VENDOR_ERROR",
        "SECRET_PROVIDER_CLASS",
        "SECRET_RAW_REGISTER",
        "SECRET_ESI_LOCAL_PATH",
        "SECRET_SDO",
        "deadbeef",
        "EtherCATProductApi",
    };
    for (const QByteArray &forbidden : forbiddenValues) {
        QVERIFY2(!encoded.contains(forbidden), forbidden.constData());
    }
}

void EtherCATAutomationGatewayTests::testSemanticRuntimeFailsClosedAndRedactsBindings()
{
    FakeAutomationService automation;
    const Core::AutomationContextSnapshot controller = mockContext();
    automation.current = {controller};

    AutomationDispatcher absent(&automation);
    const QJsonObject protocol = absent.dispatch(
        "gateway.get-protocol",
        {{"operationId", "semantic-service-protocol"}},
        {"mcp", "session-a", "test-client", "1"});
    QVERIFY(protocol.value("ok").toBool());
    QVERIFY(!protocol.value("data")
                 .toObject()
                 .value("semanticRuntime")
                 .toObject()
                 .value("available")
                 .toBool());
    const QJsonObject unavailable = absent.dispatch(
        "runtime.get-context",
        withOperation("semantic-service-absent", controller.controllerId),
        {"mcp", "session-a", "test-client", "1"});
    QVERIFY(!unavailable.value("ok").toBool());
    QCOMPARE(
        unavailable.value("error").toObject().value("code").toString(),
        "CT020_SEMANTIC_RUNTIME_UNAVAILABLE");
    QCOMPARE(
        unavailable.value("error")
            .toObject()
            .value("details")
            .toObject()
            .value("providerCalls")
            .toInt(),
        0);

    FakeSemanticRuntimeService semantic;
    Data::SemanticRuntimeContext context = semanticRuntimeContext(controller);
    context.bindingVerification.state = Data::SemanticBindingVerificationState::Unverified;
    semantic.current = {context};
    AutomationDispatcher unverified(&automation, &semantic);
    const QJsonObject bindingRejected = unverified.dispatch(
        "runtime.get-context",
        withOperation("semantic-binding-unverified", controller.controllerId),
        {"rest", "session-b", "test-client", "1"});
    QVERIFY(!bindingRejected.value("ok").toBool());
    QCOMPARE(
        bindingRejected.value("error").toObject().value("code").toString(),
        "CT021_SEMANTIC_BINDING_UNVERIFIED");
    QCOMPARE(
        bindingRejected.value("error")
            .toObject()
            .value("details")
            .toObject()
            .value("providerCalls")
            .toInt(),
        0);

    context = semanticRuntimeContext(controller);
    semantic.current = {context};
    AutomationDispatcher dispatcher(&automation, &semantic);
    const QJsonObject exposed = dispatcher.dispatch(
        "runtime.get-context",
        withOperation("semantic-context", controller.controllerId),
        {"mcp", "session-c", "test-client", "1"});
    QVERIFY(exposed.value("ok").toBool());
    const QByteArray contextJson = QJsonDocument(exposed).toJson(QJsonDocument::Compact);
    QVERIFY(contextJson.contains("embedlabs.test:manual.output.1"));
    QVERIFY(contextJson.contains("semantic-action-rejected"));
    QVERIFY(contextJson.contains(context.contextHash.toHex()));
    const QList<QByteArray> forbidden{
        "SECRET_",
        "actionDefinitionDigest",
        "resourceId",
        "componentInstanceId",
        "consistencyGroupId",
        "processImage",
        "pdo",
        "127.0.0.1",
    };
    for (const QByteArray &value : forbidden)
        QVERIFY2(!contextJson.contains(value), value.constData());

    QJsonObject readArguments{
        {"operationId", "semantic-read"},
        {"controllerId", controller.controllerId},
        {"deviceId", context.signalStates.constFirst().target.deviceId.toString()},
        {"signalId", context.signalStates.constFirst().target.signalId.value},
        {"contextHash", QString::fromLatin1(context.contextHash.toHex())},
    };
    const QJsonObject read
        = dispatcher
              .dispatch("runtime.read", readArguments, {"rest", "session-c", "test-client", "1"});
    QVERIFY(read.value("ok").toBool());
    const QJsonObject signal = read.value("data").toObject().value("signal").toObject();
    QCOMPARE(signal.value("quality").toString(), "good");
    QCOMPARE(signal.value("captureCycle").toString(), "101");
    QCOMPARE(signal.value("value").toObject().value("value").toBool(), true);

    readArguments.insert("operationId", "semantic-read-stale");
    readArguments.insert("contextHash", QString(64, '0'));
    const QJsonObject stale
        = dispatcher
              .dispatch("runtime.read", readArguments, {"rest", "session-c", "test-client", "1"});
    QVERIFY(!stale.value("ok").toBool());
    QCOMPARE(stale.value("error").toObject().value("code").toString(), "CT022_CONTEXT_STALE");
    QCOMPARE(semantic.submitCount, 0);
}

void EtherCATAutomationGatewayTests::testSemanticRuntimeOperationIntentAndJournal()
{
    FakeAutomationService automation;
    const Core::AutomationContextSnapshot controller = mockContext();
    automation.current = {controller};
    const Data::SemanticRuntimeContext context = semanticRuntimeContext(controller);

    FakeSemanticRuntimeService semantic;
    semantic.current = {context};
    FakeProviderCounters provider;
    CountingConnectionProvider connectionProvider(&provider);
    CountingScanProvider scanProvider(&provider);
    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    ExtensionSystem::PluginManager::addObject(&scanProvider);
    const QScopeGuard removeProviders([&] {
        ExtensionSystem::PluginManager::removeObject(&scanProvider);
        ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    });

    AutomationDispatcher dispatcher(&automation, &semantic);
    QJsonObject request{
        {"operationId", "semantic-operation-shared"},
        {"controllerId", controller.controllerId},
        {"deviceId", context.actionStates.constLast().target.deviceId.toString()},
        {"actionId", context.actionStates.constLast().target.actionId.value},
        {"parameters", QJsonObject{}},
        {"ttlCycles", 200},
        {"contextHash", QString::fromLatin1(context.contextHash.toHex())},
    };
    const AutomationActor mcpActor{"mcp", "mcp-session-17", "semantic-client", "2.0"};
    const QJsonObject submitted
        = dispatcher.dispatch("runtime.operation.request", request, mcpActor);
    QVERIFY(submitted.value("ok").toBool());
    const QJsonObject operation = submitted.value("data").toObject().value("operation").toObject();
    QCOMPARE(operation.value("operationId").toString(), "semantic-operation-shared");
    QCOMPARE(operation.value("state").toString(), "approval-required");
    QVERIFY(!operation.value("approvalChallenge").toString().isEmpty());
    QCOMPARE(operation.keys(), QStringList({"approvalChallenge", "operationId", "state"}));
    QCOMPARE(semantic.submitCount, 1);
    QCOMPARE(semantic.lastActor.kind, Data::SemanticRuntimeActorKind::Automation);
    QCOMPARE(semantic.lastActor.authenticationDigest.size(), 32);
    const Data::SemanticOperationRequest submittedRequest
        = semantic.records.value("semantic-operation-shared").request;
    QCOMPARE(submittedRequest.kind, Data::SemanticOperationKind::InvokeAction);
    QCOMPARE(submittedRequest.ttlMs, 0);
    QCOMPARE(submittedRequest.ttlCycles, 200);
    QCOMPARE(
        submittedRequest.expectedActionDefinitionDigest,
        context.actionStates.constLast().actionDefinitionDigest);

    QJsonObject staleRequest = request;
    staleRequest.insert("operationId", "semantic-operation-stale");
    staleRequest.insert("contextHash", QString(64, '0'));
    const QJsonObject stale
        = dispatcher.dispatch("runtime.operation.request", staleRequest, {"rest", {}, {}, {}});
    QVERIFY(!stale.value("ok").toBool());
    QCOMPARE(stale.value("error").toObject().value("code").toString(), "CT022_CONTEXT_STALE");
    QCOMPARE(semantic.submitCount, 1);

    const QJsonObject actorIdentity{
        {"transport", mcpActor.transport},
        {"sessionId", mcpActor.sessionId},
        {"clientName", mcpActor.clientName},
        {"clientVersion", mcpActor.clientVersion},
    };
    QCOMPARE(
        semantic.lastActor.authenticationDigest,
        QCryptographicHash::hash(
            AutomationDispatcher::canonicalJson(actorIdentity), QCryptographicHash::Sha256));

    const QJsonObject replayed = dispatcher.dispatch(
        "runtime.operation.request",
        request,
        {"rest", "rest-session-18", "different-client", "3.0"});
    QCOMPARE(replayed, submitted);
    QCOMPARE(semantic.submitCount, 1);

    QJsonObject conflictRequest = request;
    conflictRequest.insert("ttlCycles", 201);
    const QJsonObject conflict
        = dispatcher.dispatch("runtime.operation.request", conflictRequest, {"rest", {}, {}, {}});
    QVERIFY(!conflict.value("ok").toBool());
    QCOMPARE(
        conflict.value("error").toObject().value("details").toObject().value("reason").toString(),
        "operation-id-conflict");
    QCOMPARE(semantic.submitCount, 1);

    QJsonObject legacyTtl = request;
    legacyTtl.insert("operationId", "semantic-operation-legacy-ttl");
    legacyTtl.remove("ttlCycles");
    legacyTtl.insert("ttlMs", 200);
    const QJsonObject legacyTtlRejected
        = dispatcher.dispatch("runtime.operation.request", legacyTtl, mcpActor);
    QVERIFY(!legacyTtlRejected.value("ok").toBool());
    QCOMPARE(
        legacyTtlRejected.value("error").toObject().value("code").toString(), "CT010_BAD_REQUEST");
    QCOMPARE(legacyTtlRejected.value("error").toObject().value("path").toString(), "$.ttlMs");

    QJsonObject signalWrite = request;
    signalWrite.insert("operationId", "semantic-operation-signal-write");
    signalWrite.remove("actionId");
    signalWrite.insert("signalId", context.signalStates.constFirst().target.signalId.value);
    signalWrite.insert("value", false);
    const QJsonObject signalWriteRejected
        = dispatcher.dispatch("runtime.operation.request", signalWrite, mcpActor);
    QVERIFY(!signalWriteRejected.value("ok").toBool());
    QCOMPARE(
        signalWriteRejected.value("error").toObject().value("code").toString(), "CT010_BAD_REQUEST");
    QCOMPARE(signalWriteRejected.value("error").toObject().value("path").toString(), "$.signalId");

    QJsonObject digestInjection = request;
    digestInjection.insert("operationId", "semantic-operation-digest-injection");
    digestInjection.insert("expectedActionDefinitionDigest", QString(64, 'a'));
    const QJsonObject digestInjectionRejected
        = dispatcher.dispatch("runtime.operation.request", digestInjection, mcpActor);
    QVERIFY(!digestInjectionRejected.value("ok").toBool());
    QCOMPARE(
        digestInjectionRejected.value("error").toObject().value("code").toString(),
        "CT010_BAD_REQUEST");
    QCOMPARE(
        digestInjectionRejected.value("error").toObject().value("path").toString(),
        "$.expectedActionDefinitionDigest");

    QJsonObject unqualifiedAction = request;
    unqualifiedAction.insert("operationId", "semantic-operation-unqualified");
    unqualifiedAction.insert("actionId", context.actionStates.constFirst().target.actionId.value);
    const QJsonObject unqualifiedRejected
        = dispatcher.dispatch("runtime.operation.request", unqualifiedAction, mcpActor);
    QVERIFY(!unqualifiedRejected.value("ok").toBool());
    QCOMPARE(
        unqualifiedRejected.value("error").toObject().value("code").toString(),
        "CT023_SEMANTIC_TARGET_UNAVAILABLE");

    QJsonObject excessiveTtl = request;
    excessiveTtl.insert("operationId", "semantic-operation-excessive-ttl");
    excessiveTtl.insert("ttlCycles", 1001);
    const QJsonObject excessiveTtlRejected
        = dispatcher.dispatch("runtime.operation.request", excessiveTtl, mcpActor);
    QVERIFY(!excessiveTtlRejected.value("ok").toBool());
    QCOMPARE(
        excessiveTtlRejected.value("error").toObject().value("code").toString(),
        "CT023_SEMANTIC_TARGET_UNAVAILABLE");
    QCOMPARE(semantic.submitCount, 1);

    const QJsonObject fetched = dispatcher.dispatch(
        "runtime.operation.get",
        {
            {"operationId", "semantic-operation-query"},
            {"targetOperationId", "semantic-operation-shared"},
        },
        {"rest", "rest-session-18", "semantic-client", "3.0"});
    QVERIFY(fetched.value("ok").toBool());
    QCOMPARE(
        fetched.value("data").toObject().value("operation").toObject().value("state").toString(),
        "approval-required");

    Data::SemanticOperationRecord uiOperation = semantic.records.value("semantic-operation-shared");
    uiOperation.request.operationId = {"ui-operation-record"};
    uiOperation.actor.kind = Data::SemanticRuntimeActorKind::User;
    uiOperation.actor.id = "user:test";
    uiOperation.state = Data::SemanticOperationState::Approved;
    uiOperation.detail = "SECRET_RUNTIME_RESOURCE_ID";
    semantic.records.insert(uiOperation.request.operationId.value, uiOperation);
    const QJsonObject sharedUiRecord = dispatcher.dispatch(
        "runtime.operation.get",
        {
            {"operationId", "semantic-ui-operation-query"},
            {"targetOperationId", "ui-operation-record"},
        },
        {"mcp", "mcp-session-17", "semantic-client", "2.0"});
    QVERIFY(sharedUiRecord.value("ok").toBool());
    QCOMPARE(
        sharedUiRecord.value("data").toObject().value("operation").toObject().value("state").toString(),
        "approved");
    QVERIFY(!QJsonDocument(sharedUiRecord)
                 .toJson(QJsonDocument::Compact)
                 .contains("SECRET_RUNTIME_RESOURCE_ID"));

    QJsonObject actorInjection = request;
    actorInjection.insert("operationId", "semantic-actor-injection");
    actorInjection.insert("authenticationDigest", "attacker-controlled");
    const QJsonObject actorRejected
        = dispatcher.dispatch("runtime.operation.request", actorInjection, {"rest", {}, {}, {}});
    QVERIFY(!actorRejected.value("ok").toBool());
    QCOMPARE(actorRejected.value("error").toObject().value("code").toString(), "CT010_BAD_REQUEST");
    QCOMPARE(semantic.submitCount, 1);

    QVERIFY(!AutomationDispatcher::toolNames().contains("runtime.operation.approve"));
    const QJsonObject approvalRejected = dispatcher.dispatch(
        "runtime.operation.approve",
        {{"operationId", "automation-must-not-approve"}},
        {"mcp", "mcp-session-17", "semantic-client", "2.0"});
    QVERIFY(!approvalRejected.value("ok").toBool());
    QCOMPARE(
        approvalRejected.value("error").toObject().value("code").toString(),
        "CT011_PROTOCOL_UNSUPPORTED");
    QCOMPARE(semantic.approveCount, 0);

    Data::SemanticRuntimeContext parameterContext = context;
    Data::SemanticActionParameterRuntimeDefinition velocity;
    velocity.id = "velocity";
    velocity.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    velocity.unit = "reference-unit-per-second";
    velocity.minimum = QVariant::fromValue<qlonglong>(-1000);
    velocity.maximum = QVariant::fromValue<qlonglong>(1000);
    parameterContext.actionStates.last().parameters = {velocity};
    FakeSemanticRuntimeService parameterSemantic;
    parameterSemantic.current = {parameterContext};
    AutomationDispatcher parameterDispatcher(&automation, &parameterSemantic);
    QJsonObject parameterRequest = request;
    parameterRequest.insert("operationId", "semantic-operation-parameter");
    parameterRequest.insert("parameters", QJsonObject{{"velocity", 25}});
    const QJsonObject parameterSubmitted
        = parameterDispatcher.dispatch("runtime.operation.request", parameterRequest, mcpActor);
    QVERIFY(parameterSubmitted.value("ok").toBool());
    const QVariant submittedVelocity = parameterSemantic.records
                                           .value("semantic-operation-parameter")
                                           .request.parameters.value("velocity");
    QCOMPARE(submittedVelocity.metaType().id(), QMetaType::LongLong);
    QCOMPARE(submittedVelocity.toLongLong(), 25);
    QCOMPARE(parameterSemantic.submitCount, 1);
    QCOMPARE(parameterSemantic.approveCount, 0);

    FailClosedSemanticRuntimeService failClosed;
    failClosed.current = {context};
    AutomationDispatcher failClosedDispatcher(&automation, &failClosed);
    request.insert("operationId", "semantic-base-fail-closed");
    const QJsonObject baseRejected
        = failClosedDispatcher.dispatch("runtime.operation.request", request, mcpActor);
    QVERIFY(baseRejected.value("ok").toBool());
    const QJsonObject baseOperation
        = baseRejected.value("data").toObject().value("operation").toObject();
    QCOMPARE(baseOperation.value("state").toString(), "rejected");
    QCOMPARE(baseOperation.keys(), QStringList({"operationId", "state"}));

    QCOMPARE(provider.connect, 0);
    QCOMPARE(provider.control, 0);
    QCOMPARE(provider.scan, 0);
    QCOMPARE(provider.write, 0);
}

void EtherCATAutomationGatewayTests::testMcpToolSchemaRoundTrip()
{
    const QJsonObject inputSchema{
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"type", "object"},
        {"properties", QJsonObject{{"operationId", QJsonObject{{"type", "string"}}}}},
        {"additionalProperties", false},
        {"unevaluatedProperties", false},
    };
    const QJsonObject outputSchema{
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"type", "object"},
        {"$defs", QJsonObject{{"payload", QJsonObject{{"type", "object"}}}}},
        {"oneOf", QJsonArray{QJsonObject{{"$ref", "#/$defs/payload"}}}},
        {"allOf", QJsonArray{QJsonObject{{"type", "object"}}}},
        {"x-controller-tools-round-trip", QJsonObject{{"strict", true}}},
    };
    const QJsonObject tool{
        {"name", "schema.round-trip"},
        {"inputSchema", inputSchema},
        {"outputSchema", outputSchema},
    };

    const Utils::Result<Mcp::Schema::Tool> parsed
        = Mcp::Schema::fromJson<Mcp::Schema::Tool>(tool);
    QVERIFY_RESULT(parsed);
    QCOMPARE(Mcp::Schema::toJson(*parsed), tool);
}

static quint16 unusedPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    return probe.serverPort();
}

void EtherCATAutomationGatewayTests::testListenerLifecycleAndAtomicRollback()
{
    FakeAutomationService service;
    service.current = {mockContext()};
    GatewayServer server(&service);
    const Utils::Result<> started = server.start(QHostAddress::LocalHost, 0, 0);
    QVERIFY_RESULT(started);
    QVERIFY(server.isRunning());
    const quint16 mcpPort = server.mcpPort();
    const quint16 restPort = server.restPort();
    QVERIFY(mcpPort != 0);
    QVERIFY(restPort != 0);
    server.stop();
    QVERIFY(!server.isRunning());
    QCOMPARE(server.mcpPort(), 0);
    QCOMPARE(server.restPort(), 0);

    QTcpServer mcpReuse;
    QTcpServer restReuse;
    QVERIFY(mcpReuse.listen(QHostAddress::LocalHost, mcpPort));
    QVERIFY(restReuse.listen(QHostAddress::LocalHost, restPort));
    mcpReuse.close();
    restReuse.close();

    const quint16 candidateMcpPort = unusedPort();
    QVERIFY(candidateMcpPort != 0);
    QTcpServer blockedRest;
    QVERIFY(blockedRest.listen(QHostAddress::LocalHost, 0));
    const Utils::Result<> failed
        = server.start(QHostAddress::LocalHost, candidateMcpPort, blockedRest.serverPort());
    QVERIFY(!failed);
    QVERIFY(!server.isRunning());
    QTcpServer rolledBackMcp;
    QVERIFY(rolledBackMcp.listen(QHostAddress::LocalHost, candidateMcpPort));
}

void EtherCATAutomationGatewayTests::testMcpRestIntegrationAndOriginBoundary()
{
    FakeAutomationService service;
    service.current = {mockContext()};
    FakeProviderCounters provider;
    GatewayServer server(&service);
    const Utils::Result<> started = server.start(QHostAddress::LocalHost, 0, 0);
    QVERIFY_RESULT(started);
    const QUrl mcpUrl(QString("http://127.0.0.1:%1/").arg(server.mcpPort()));
    const QString restBase = QString("http://127.0.0.1:%1").arg(server.restPort());

    QUrl restList(restBase + "/api/controller-tools/v1/controllers");
    QUrlQuery restQuery;
    restQuery.addQueryItem("operationId", "cross-http-operation");
    restList.setQuery(restQuery);
    const HttpResult rest = sendHttp(restList, "GET", {}, {{"Origin", "http://localhost"}});
    QCOMPARE(rest.status, 200);
    const QJsonObject restEnvelope = jsonObject(rest);
    QVERIFY(restEnvelope.value("ok").toBool());

    const QJsonObject initialize{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params",
         QJsonObject{
             {"protocolVersion", Constants::MCP_PROTOCOL_VERSION},
             {"capabilities", QJsonObject{}},
             {"clientInfo",
              QJsonObject{
                  {"name", "gateway-audit-test"},
                  {"version", "7.5"},
              }},
         }},
    };
    const HttpResult initialized = sendHttp(
        mcpUrl,
        "POST",
        QJsonDocument(initialize).toJson(QJsonDocument::Compact),
        mcpHeaders({}, "http://localhost"));
    QCOMPARE(initialized.status, 200);
    QVERIFY(!initialized.sessionId.isEmpty());

    const QJsonObject listTools{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
        {"params", QJsonObject{}},
    };
    const HttpResult listed = sendHttp(
        mcpUrl,
        "POST",
        QJsonDocument(listTools).toJson(QJsonDocument::Compact),
        mcpHeaders(initialized.sessionId, "http://127.0.0.1"));
    QCOMPARE(listed.status, 200);
    QJsonArray tools = jsonObject(listed).value("result").toObject().value("tools").toArray();
    QFile contract(Constants::MCP_RESOURCE_PATH);
    QVERIFY(contract.open(QIODevice::ReadOnly));
    const QJsonDocument contractDocument = QJsonDocument::fromJson(contract.readAll());
    QVERIFY(contractDocument.isObject());
    QHash<QString, QJsonObject> expectedTools;
    for (const QJsonValue &value : contractDocument.object().value("tools").toArray()) {
        const QJsonObject tool = value.toObject();
        expectedTools.insert(tool.value("name").toString(), tool);
    }
    QCOMPARE(expectedTools.size(), tools.size());
    QStringList toolNames;
    QJsonObject selectedTopologyTool;
    for (const QJsonValue &tool : tools) {
        const QJsonObject toolObject = tool.toObject();
        const QString toolName = toolObject.value("name").toString();
        toolNames.append(toolName);
        QCOMPARE(toolObject, expectedTools.take(toolName));
        if (toolName == "topology.list-selected")
            selectedTopologyTool = toolObject;
    }
    QVERIFY(expectedTools.isEmpty());
    toolNames.sort();
    QCOMPARE(toolNames, AutomationDispatcher::toolNames());
    QVERIFY(!selectedTopologyTool.isEmpty());

    const QJsonObject inputSchema = selectedTopologyTool.value("inputSchema").toObject();
    QCOMPARE(
        inputSchema.keys(),
        QStringList({"$schema", "additionalProperties", "properties", "type"}));
    QCOMPARE(inputSchema.value("type").toString(), "object");
    QCOMPARE(inputSchema.value("additionalProperties"), QJsonValue(false));

    const QJsonObject outputSchema = selectedTopologyTool.value("outputSchema").toObject();
    QCOMPARE(outputSchema.keys(), QStringList({"$defs", "$schema", "oneOf", "type"}));
    QCOMPARE(outputSchema.value("type").toString(), "object");
    QCOMPARE(outputSchema.value("oneOf").toArray().size(), 2);
    const QJsonObject definitions = outputSchema.value("$defs").toObject();
    QVERIFY(definitions.contains("successEnvelope"));
    QVERIFY(definitions.contains("errorEnvelope"));
    QCOMPARE(
        definitions.value("successEnvelope").toObject().value("additionalProperties"),
        QJsonValue(false));
    QCOMPARE(
        definitions.value("errorEnvelope").toObject().value("additionalProperties"),
        QJsonValue(false));
    const QJsonObject freshRecord = definitions.value("freshRecord").toObject();
    QCOMPARE(freshRecord.value("additionalProperties"), QJsonValue(false));
    QVERIFY(!freshRecord.value("allOf").toArray().isEmpty());

    const QJsonObject replayCall{
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "tools/call"},
        {"params",
         QJsonObject{
             {"name", "controller.list"},
             {"arguments", QJsonObject{{"operationId", "cross-http-operation"}}},
         }},
    };
    const HttpResult replayed = sendHttp(
        mcpUrl,
        "POST",
        QJsonDocument(replayCall).toJson(QJsonDocument::Compact),
        mcpHeaders(initialized.sessionId, "http://localhost"));
    QCOMPARE(replayed.status, 200);
    const QJsonObject replayEnvelope
        = jsonObject(replayed).value("result").toObject().value("structuredContent").toObject();
    QCOMPARE(replayEnvelope, restEnvelope);

    const QJsonObject protocolCall{
        {"jsonrpc", "2.0"},
        {"id", 4},
        {"method", "tools/call"},
        {"params",
         QJsonObject{
             {"name", "gateway.get-protocol"},
             {"arguments", QJsonObject{{"operationId", "audit-mcp"}}},
         }},
    };
    const HttpResult protocol = sendHttp(
        mcpUrl,
        "POST",
        QJsonDocument(protocolCall).toJson(QJsonDocument::Compact),
        mcpHeaders(initialized.sessionId, "http://localhost"));
    QCOMPARE(protocol.status, 200);
    const QJsonObject audit = jsonObject(protocol)
                                  .value("result")
                                  .toObject()
                                  .value("structuredContent")
                                  .toObject()
                                  .value("audit")
                                  .toObject();
    QCOMPARE(audit.value("sessionId").toString(), initialized.sessionId);
    QCOMPARE(audit.value("client").toObject().value("name").toString(), "gateway-audit-test");
    QCOMPARE(audit.value("client").toObject().value("version").toString(), "7.5");

    const HttpResult rejectedMcpOrigin = sendHttp(
        mcpUrl,
        "POST",
        QJsonDocument(initialize).toJson(QJsonDocument::Compact),
        mcpHeaders({}, "https://attacker.example"));
    QCOMPARE(rejectedMcpOrigin.status, 400);
    QVERIFY(rejectedMcpOrigin.body.contains("Invalid origin"));

    const HttpResult rejectedRestOrigin = sendHttp(
        QUrl(restBase + "/api/controller-tools/v1/protocol"),
        "GET",
        {},
        {{"Origin", "https://attacker.example"}});
    QCOMPARE(rejectedRestOrigin.status, 403);

    const HttpResult openApi = sendHttp(
        QUrl(restBase + "/api/controller-tools/v1/openapi.json"),
        "GET",
        {},
        {{"Origin", "http://localhost"}});
    QCOMPARE(openApi.status, 200);
    const QJsonObject openApiObject = jsonObject(openApi);
    QCOMPARE(
        openApiObject.value("servers").toArray().at(0).toObject().value("url").toString(), restBase);
    QCOMPARE(
        openApiObject.value("x-mcp-streamable-http").toObject().value("url").toString(),
        mcpUrl.toString());
    QVERIFY(!openApiObject.value("paths").toObject().contains("/mcp"));

    const QJsonObject mutationBody{{"operationId", "deny-http-scan"}};
    const HttpResult rejectedMutation = sendHttp(
        QUrl(restBase + "/api/controller-tools/v1/controllers/mock/scan"),
        "POST",
        QJsonDocument(mutationBody).toJson(QJsonDocument::Compact),
        {
            {"Content-Type", "application/json"},
            {"Origin", "http://localhost"},
        });
    QCOMPARE(rejectedMutation.status, 403);
    QCOMPARE(
        jsonObject(rejectedMutation)
            .value("error")
            .toObject()
            .value("details")
            .toObject()
            .value("reason")
            .toString(),
        "approval-required");
    QCOMPARE(provider.connect, 0);
    QCOMPARE(provider.control, 0);
    QCOMPARE(provider.scan, 0);
    QCOMPARE(provider.write, 0);

    const quint16 stoppedMcpPort = server.mcpPort();
    const quint16 stoppedRestPort = server.restPort();
    server.stop();
    QTcpServer mcpReuse;
    QTcpServer restReuse;
    QVERIFY(mcpReuse.listen(QHostAddress::LocalHost, stoppedMcpPort));
    QVERIFY(restReuse.listen(QHostAddress::LocalHost, stoppedRestPort));
}

void EtherCATAutomationGatewayTests::testExternalProcessProbe()
{
    FakeAutomationService service;
    const Core::AutomationContextSnapshot controller = mockContext();
    service.current = {controller};
    FakeSemanticRuntimeService semantic;
    semantic.current = {semanticRuntimeContext(controller)};
    FakeProviderCounters provider;
    CountingConnectionProvider connectionProvider(&provider);
    CountingScanProvider scanProvider(&provider);
    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    ExtensionSystem::PluginManager::addObject(&scanProvider);
    const QScopeGuard removeProviders([&] {
        ExtensionSystem::PluginManager::removeObject(&scanProvider);
        ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    });

    GatewayServer server(&service, &semantic);
    const Utils::Result<> started = server.start(QHostAddress::LocalHost, 0, 0);
    QVERIFY_RESULT(started);

    const QString python = QStandardPaths::findExecutable("python3");
    QVERIFY2(!python.isEmpty(), "python3 is required for the client-only process probe");
    const QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    const QString probePath = sourceDir.filePath("tests/controller_tools_v1_probe.py");
    QVERIFY(QFileInfo::exists(probePath));

    QProcess probe;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("CRASH_REPORTER_DISABLE", "1");
    probe.setProcessEnvironment(environment);
    probe.setProgram(python);
    probe.setArguments({
        probePath,
        "--mcp-url",
        server.mcpEndpoint().toString(),
        "--rest-url",
        server.restEndpoint().toString(),
    });

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(30000);
    connect(
        &probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    probe.start();
    QVERIFY(probe.waitForStarted());
    loop.exec();
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished();
        QFAIL("controller-tools-v1 process probe timed out");
    }

    const QByteArray standardOutput = probe.readAllStandardOutput().trimmed();
    const QByteArray standardError = probe.readAllStandardError().trimmed();
    QVERIFY2(
        probe.exitStatus() == QProcess::NormalExit,
        standardError.isEmpty() ? standardOutput.constData() : standardError.constData());
    QVERIFY2(
        probe.exitCode() == 0,
        standardError.isEmpty() ? standardOutput.constData() : standardError.constData());
    const QJsonDocument report = QJsonDocument::fromJson(standardOutput);
    QVERIFY2(report.isObject(), standardOutput.constData());
    QVERIFY(report.object().value("ok").toBool());
    QCOMPARE(report.object().value("toolCount").toInt(), 14);
    QCOMPARE(report.object().value("mutationsRejected").toInt(), 7);
    QCOMPARE(report.object().value("topologySource").toString(), "ide-workbench-scan-snapshot");
    QVERIFY(report.object().value("diagnosticsAvailable").toBool());
    QVERIFY(report.object().value("semanticContextVerified").toBool());
    QCOMPARE(report.object().value("semanticQuality").toString(), "good");
    QCOMPARE(report.object().value("semanticOperationState").toString(), "approval-required");
    QCOMPARE(semantic.submitCount, 1);
    QCOMPARE(semantic.approveCount, 0);
    QCOMPARE(provider.connect, 0);
    QCOMPARE(provider.control, 0);
    QCOMPARE(provider.scan, 0);
    QCOMPARE(provider.write, 0);
}

void EtherCATAutomationGatewayTests::testArtifactValidationAndBuildSystemSync()
{
    FakeAutomationService service;
    AutomationDispatcher dispatcher(&service);
    const QJsonObject invalid = dispatcher.dispatch(
        "artifact.validate",
        {
            {"operationId", "invalid-artifact"},
            {"artifact", QJsonObject{{"kind", "ControlIntent"}}},
        },
        {"rest", {}, {}, {}});
    QVERIFY(invalid.value("ok").toBool());
    QVERIFY(!invalid.value("data").toObject().value("valid").toBool());
    QVERIFY(!invalid.value("data").toObject().value("errors").toArray().isEmpty());

    const QJsonObject valid = dispatcher.dispatch(
        "artifact.validate",
        {
            {"operationId", "valid-artifact"},
            {"artifact",
             QJsonObject{
                 {"apiVersion", "controller.embed-labs.dev/v1"},
                 {"kind", "ControlIntent"},
                 {"metadata", QJsonObject{}},
                 {"spec", QJsonObject{}},
             }},
        },
        {"mcp", {}, {}, {}});
    QVERIFY(valid.value("ok").toBool());
    QVERIFY(valid.value("data").toObject().value("valid").toBool());
    QCOMPARE(
        valid.value("data").toObject().value("validationLevel").toString(), "gateway-structural-v1");

    const QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    QFile cmake(sourceDir.filePath("CMakeLists.txt"));
    QFile qbs(sourceDir.filePath("ethercatautomationgateway.qbs"));
    QFile resources(sourceDir.filePath("controller-tools-v1-contracts.qrc"));
    QFile pluginSource(sourceDir.filePath("ethercatautomationgatewayplugin.cpp"));
    QVERIFY(cmake.open(QIODevice::ReadOnly));
    QVERIFY(qbs.open(QIODevice::ReadOnly));
    QVERIFY(resources.open(QIODevice::ReadOnly));
    QVERIFY(pluginSource.open(QIODevice::ReadOnly));
    const QByteArray cmakeText = cmake.readAll();
    const QByteArray qbsText = qbs.readAll();
    const QByteArray resourceText = resources.readAll();
    const QByteArray pluginText = pluginSource.readAll();
    const QStringList synchronizedFiles{
        "automationdispatcher.cpp",
        "automationdispatcher.h",
        "controller-tools-v1-contracts.qrc",
        "ethercatautomationgatewayconstants.h",
        "ethercatautomationgatewayplugin.cpp",
        "ethercatautomationgatewaytr.h",
        "ethercatautomationgatewaytests.cpp",
        "ethercatautomationgatewaytests.h",
        "gatewayruntime.cpp",
        "gatewayruntime.h",
        "gatewayserver.cpp",
        "gatewayserver.h",
        "gatewaysettingspage.cpp",
        "gatewaysettingspage.h",
        "tests/controller_tools_v1_probe.py",
    };
    for (const QString &file : synchronizedFiles) {
        QVERIFY2(cmakeText.contains(file.toUtf8()), qPrintable(file));
        QVERIFY2(qbsText.contains(file.toUtf8()), qPrintable(file));
    }
    const QList<QPair<QByteArray, QByteArray>> synchronizedDependencies{
        {"EtherCATData", "EtherCATData"},
        {"ExtensionSystem", "ExtensionSystem"},
        {"McpServerLib", "McpServerLib"},
        {"Qt::Core", "\"core\""},
        {"Qt::HttpServer", "\"httpserver\""},
        {"Qt::Network", "\"network\""},
        {"Qt::Widgets", "\"widgets\""},
        {"Utils", "Utils"},
        {"Core", "\"Core\""},
        {"EtherCATCore", "EtherCATCore"},
        {"EtherCATWorkbench", "EtherCATWorkbench"},
        {"ProjectExplorer", "ProjectExplorer"},
    };
    for (const auto &[cmakeDependency, qbsDependency] : synchronizedDependencies) {
        QVERIFY2(cmakeText.contains(cmakeDependency), cmakeDependency.constData());
        QVERIFY2(qbsText.contains(qbsDependency), qbsDependency.constData());
    }
    QVERIFY(resourceText.contains("controller-tools-v1.mcp-tools.json"));
    QVERIFY(resourceText.contains("controller-tools-v1.openapi.json"));
    QVERIFY(pluginText.contains("CONTROLLER_OUTPUT_CHANNEL_ID"));
    QVERIFY(pluginText.contains("getObject<Core::SemanticRuntimeService>"));
    QVERIFY(pluginText.contains("[AI Gateway]"));
    QVERIFY(pluginText.contains("postApplicationOutput"));
    QVERIFY(!pluginText.contains("MessageManager"));
    QVERIFY(!pluginText.contains("QMessageBox"));

    const QDir repositoryRoot(sourceDir.filePath("../../.."));
    QFile mcpContract(
        repositoryRoot.filePath("ethercat-ai-controller/api/controller-tools-v1.mcp-tools.json"));
    QFile openApiContract(
        repositoryRoot.filePath("ethercat-ai-controller/api/controller-tools-v1.openapi.json"));
    QVERIFY(mcpContract.open(QIODevice::ReadOnly));
    QVERIFY(openApiContract.open(QIODevice::ReadOnly));
    const QByteArray mcpContractText = mcpContract.readAll();
    const QByteArray openApiContractText = openApiContract.readAll();
    const QList<QByteArray> publicContractTexts{mcpContractText, openApiContractText};
    for (const QByteArray &contractText : publicContractTexts) {
        QVERIFY(contractText.contains("\"ttlCycles\""));
        QVERIFY(!contractText.contains("\"ttlMs\""));
        QVERIFY(!contractText.contains("\"expectedActionDefinitionDigest\""));
        QVERIFY(!contractText.contains("\"resourceId\""));
        QVERIFY(!contractText.contains("\"consistencyGroupId\""));
        QVERIFY(!contractText.contains("\"pdo\""));
    }

    QFile translation(repositoryRoot.filePath("share/qtcreator/translations/qtcreator_zh_CN.ts"));
    QVERIFY(translation.open(QIODevice::ReadOnly));
    const QByteArray translationText = translation.readAll();
    QVERIFY(translationText.contains("<name>QtC::EtherCATAutomationGateway</name>"));
    QVERIFY(translationText.contains("<source>Automation Gateway</source>"));
    QVERIFY(translationText.contains("<translation>自动化网关</translation>"));
    QVERIFY(translationText.contains("仅限 Mock"));
}

} // namespace EtherCAT::AutomationGateway::Internal
