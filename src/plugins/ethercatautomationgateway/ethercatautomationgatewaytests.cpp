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

#include <extensionsystem/pluginmanager.h>

#include <utils/qtcsettings.h>

#include <QCheckBox>
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
#include <QSpinBox>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

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
        const Data::ControllerConnectionScope &,
        const Data::NodeId &,
        const QString &) final
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
        : ScanProvider(
              "EtherCAT.AutomationGateway.Tests.Scan",
              "Automation Gateway counting scan")
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
    context.project.formatVersion = 1;
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
            "Outputs",
            Data::PdoDirection::Rx,
            2,
            true,
            true,
            {{0x7000, 1, "Output 1", 1, Data::EtherCATDataType::Boolean, "BOOL"}},
        },
    };
    description.txPdos = {
        {
            0x1a00,
            "Inputs",
            Data::PdoDirection::Tx,
            3,
            true,
            true,
            {{0x6000, 1, "Input 1", 1, Data::EtherCATDataType::Boolean, "BOOL"}},
        },
    };
    description.coe.supported = true;
    context.deviceDescriptions = {description};
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
    QCOMPARE(
        registeredPage->category(),
        Utils::Id(EtherCAT::Core::Constants::SETTINGS_CATEGORY));

    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    Utils::QtcSettings settings(
        settingsDirectory.filePath("gateway.ini"), QSettings::IniFormat);
    FakeAutomationService service;
    service.current = {mockContext()};
    GatewayRuntimeController runtime(&service, &settings, false);
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

    auto enabled
        = widget->findChild<QCheckBox *>(Constants::SETTINGS_ENABLED_OBJECT_NAME);
    auto address = widget->findChild<QLineEdit *>(Constants::SETTINGS_ADDRESS_OBJECT_NAME);
    auto mcpPort = widget->findChild<QSpinBox *>(Constants::SETTINGS_MCP_PORT_OBJECT_NAME);
    auto restPort = widget->findChild<QSpinBox *>(Constants::SETTINGS_REST_PORT_OBJECT_NAME);
    auto state = widget->findChild<QLineEdit *>(Constants::SETTINGS_STATE_OBJECT_NAME);
    auto mcpEndpoint
        = widget->findChild<QLineEdit *>(Constants::SETTINGS_MCP_ENDPOINT_OBJECT_NAME);
    auto restEndpoint
        = widget->findChild<QLineEdit *>(Constants::SETTINGS_REST_ENDPOINT_OBJECT_NAME);
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
    QVERIFY(safety->text().contains("Mock-only"));
    QVERIFY(safety->text().contains("read-only"));
    QCOMPARE(mcpPort->minimum(), 0);
    QCOMPARE(mcpPort->maximum(), 65535);
    QCOMPARE(restPort->minimum(), 0);
    QCOMPARE(restPort->maximum(), 65535);

    mcpPort->setValue(0);
    restPort->setValue(0);
    enabled->setChecked(true);
    widget->apply();
    QCOMPARE(runtime.snapshot().state, GatewayRuntimeState::Running);
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
    Utils::QtcSettings settings(
        settingsDirectory.filePath("gateway.ini"), QSettings::IniFormat);
    FakeAutomationService service;
    service.current = {mockContext()};

    {
        GatewayRuntimeController runtime(&service, &settings, false);
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
        GatewayRuntimeController runtime(&service, &settings, false);
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
        GatewayRuntimeController runtime(&service, &settings, false);
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

    const QString blockedSettingsParent
        = settingsDirectory.filePath("settings-parent-is-a-file");
    QFile blockedSettingsFile(blockedSettingsParent);
    QVERIFY(blockedSettingsFile.open(QIODevice::WriteOnly));
    blockedSettingsFile.close();
    Utils::QtcSettings unwritableSettings(
        blockedSettingsParent + "/gateway.ini", QSettings::IniFormat);
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
        GatewayRuntimeController runtime(&service, &unwritableSettings, false);
        const Utils::Result<> failed
            = runtime.applyConfiguration({true, persistenceMcpPort, persistenceRestPort});
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
        GatewayRuntimeController restored(&service, &settings, false);
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
    Utils::QtcSettings settings(
        settingsDirectory.filePath("gateway.ini"), QSettings::IniFormat);
    settings.beginGroup(Constants::SETTINGS_GROUP);
    settings.setValue(Constants::SETTINGS_ENABLED, true);
    settings.setValue(Constants::SETTINGS_MCP_PORT, 0);
    settings.setValue(Constants::SETTINGS_REST_PORT, 0);
    settings.endGroup();

    GatewayRuntimeController runtime(nullptr, &settings, false);
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
    const QJsonObject invalidOperation = dispatcher.dispatch(
        "controller.list", {{"operationId", ""}}, {"rest", {}, {}, {}});
    QVERIFY(!invalidOperation.value("ok").toBool());
    QCOMPARE(
        invalidOperation.value("error").toObject().value("code").toString(),
        "CT010_BAD_REQUEST");
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
    QStringList toolNames;
    for (const QJsonValue &tool : tools)
        toolNames.append(tool.toObject().value("name").toString());
    toolNames.sort();
    QCOMPARE(toolNames, AutomationDispatcher::toolNames());

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

    GatewayServer server(&service);
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
        &probe,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        &loop,
        &QEventLoop::quit);
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
    QCOMPARE(report.object().value("toolCount").toInt(), 9);
    QCOMPARE(report.object().value("mutationsRejected").toInt(), 7);
    QCOMPARE(report.object().value("topologySource").toString(),
             "ide-workbench-scan-snapshot");
    QVERIFY(report.object().value("diagnosticsAvailable").toBool());
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
    QVERIFY(pluginText.contains("[AI Gateway]"));
    QVERIFY(pluginText.contains("postApplicationOutput"));
    QVERIFY(!pluginText.contains("MessageManager"));
    QVERIFY(!pluginText.contains("QMessageBox"));

    const QDir repositoryRoot(sourceDir.filePath("../../.."));
    QFile translation(
        repositoryRoot.filePath("share/qtcreator/translations/qtcreator_zh_CN.ts"));
    QVERIFY(translation.open(QIODevice::ReadOnly));
    const QByteArray translationText = translation.readAll();
    QVERIFY(translationText.contains("<name>QtC::EtherCATAutomationGateway</name>"));
    QVERIFY(translationText.contains("<source>Automation Gateway</source>"));
    QVERIFY(translationText.contains("<translation>自动化网关</translation>"));
    QVERIFY(translationText.contains("仅限 Mock"));
}

} // namespace EtherCAT::AutomationGateway::Internal
