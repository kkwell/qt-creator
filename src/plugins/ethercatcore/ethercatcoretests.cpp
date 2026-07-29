// Copyright (C) 2026 Kvell

#include "ethercatcoretests.h"

#include "automationservice.h"
#include "ethercatcoreconstants.h"
#include "ethercatcoresettings.h"
#include "ethercatcoretr.h"
#include "providerregistry.h"
#include "providers.h"
#include "selectionservice.h"
#include "stateservice.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/nodeid.h>
#include <ethercatdata/offlineconfiguration.h>
#include <ethercatdata/projectsnapshot.h>

#include <QSignalSpy>
#include <QScopeGuard>
#include <QStringList>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <type_traits>

namespace EtherCAT::Core::Internal {

class TestAutomationService final : public AutomationService
{
public:
    using AutomationService::AutomationService;

    QList<AutomationContextSnapshot> contexts() const final
    {
        ++readCount;
        return snapshots;
    }

    mutable int readCount = 0;
    QList<AutomationContextSnapshot> snapshots;
};

class TestDeviceImportJob final : public DeviceImportJob
{
public:
    using DeviceImportJob::DeviceImportJob;

    void start()
    {
        setState(DeviceImportState::Running);
        setProgress(1, 2);
    }

    void cancel() final
    {
        setState(DeviceImportState::Canceling);
        Data::DeviceImportResult result;
        result.requestedFiles = 2;
        result.canceled = true;
        finish(result);
    }
};

static Data::DeviceAdapterManifest testDeviceAdapterManifest(
    Data::DeviceAdapterQualification qualification = Data::DeviceAdapterQualification::Qualified,
    const QString &version = "2.1.0")
{
    Data::DeviceSignalBinding binding;
    binding.kind = Data::DeviceSignalBindingKind::ProcessDataObject;
    binding.pdoDirection = Data::PdoDirection::Rx;
    binding.pdoIndex = 0x1600;
    binding.objectIndex = 0x7001;
    binding.objectSubIndex = 0;
    binding.physicalType = Data::EtherCATDataType::Integer32;
    binding.bitWidth = 32;
    binding.byteOrder = Data::DeviceByteOrder::LittleEndian;

    Data::SemanticSignalDefinition signal;
    signal.id = {"urn:example.test:signal/custom.axis.target-velocity"};
    signal.displayName = "Target velocity";
    signal.description = "Adapter-owned velocity command";
    signal.capabilities
        = {{"example.test.capability/axis.velocity"},
           {"example.test.capability/custom-diagnostics"}};
    signal.direction = Data::SemanticSignalDirection::Output;
    signal.access = Data::SemanticSignalAccess::WriteOnly;
    signal.bindings = {binding};
    signal.valueMetadata.unit = "rpm";
    signal.valueMetadata.scale = 0.1;
    signal.valueMetadata.offset = -1.0;
    signal.valueMetadata.hasMinimum = true;
    signal.valueMetadata.minimum = -3000.0;
    signal.valueMetadata.hasMaximum = true;
    signal.valueMetadata.maximum = 3000.0;
    signal.valueMetadata.hasStep = true;
    signal.valueMetadata.step = 0.1;
    signal.valueMetadata.enumValues = {{0, "stopped", "Stopped"}};
    signal.hasSafeValue = true;
    signal.safeValue = 0;
    signal.manualControl.policyId = "manual.axis.hold-to-run";
    signal.manualControl.allowed = true;
    signal.manualControl.requiresExclusiveControl = true;
    signal.manualControl.holdToRun = true;
    signal.manualControl.commandTimeoutMs = 250;
    signal.manualControl.timeoutAction = Data::ManualControlTimeoutAction::ControlledStop;

    Data::DeviceAdapterManifest manifest;
    manifest.id = {"org.example.test.adapter/custom-drive"};
    manifest.version = version;
    manifest.displayName = "Vendor Example Drive";
    manifest.description = "Test-only semantic adapter";
    manifest.qualification = qualification;
    manifest.matchPriority = 120;
    manifest.match.vendorId = 0x00a1b2c3;
    manifest.match.productCode = 0x01020304;
    manifest.match.minimumRevision = 0x00020003;
    manifest.match.maximumRevision = 0x00020003;
    manifest.match.exactEsiSha256 = QByteArray::fromHex(
        "1111111111111111111111111111111111111111111111111111111111111111");
    manifest.capabilities = signal.capabilities;
    manifest.semanticSignals = {signal};
    manifest.provenance.sourceId = "test.adapter.catalog";
    manifest.provenance.sourceVersion = "2026.07";
    manifest.provenance.sourceLocation = "tests/custom-drive.adapter.json";
    manifest.provenance.sourceSha256 = QByteArray::fromHex(
        "2222222222222222222222222222222222222222222222222222222222222222");
    manifest.contentSha256 = QByteArray::fromHex(
        "3333333333333333333333333333333333333333333333333333333333333333");
    manifest.evidenceSha256 = QByteArray::fromHex(
        "4444444444444444444444444444444444444444444444444444444444444444");
    manifest.signatureVerified = qualification == Data::DeviceAdapterQualification::Qualified;
    manifest.realHardwareAllowed = qualification == Data::DeviceAdapterQualification::Qualified;
    return manifest;
}

class TestDeviceAdapterProvider final : public DeviceAdapterProvider
{
public:
    explicit TestDeviceAdapterProvider(const QList<Data::DeviceAdapterManifest> &manifests)
        : DeviceAdapterProvider("EtherCAT.DeviceAdapter.Test", "Test device adapters")
        , m_manifests(manifests)
    {}

    QList<Data::DeviceAdapterManifest> adapterManifests() const final { return m_manifests; }

    std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &adapterId, const QString &version) const final
    {
        const auto found = std::find_if(
            m_manifests.cbegin(),
            m_manifests.cend(),
            [&adapterId, &version](const Data::DeviceAdapterManifest &manifest) {
                return manifest.id == adapterId && manifest.version == version;
            });
        if (found == m_manifests.cend())
            return std::nullopt;
        return *found;
    }

    Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &request) const final
    {
        lastResolutionRequest = request;
        ++resolutionCount;
        return resolutionResult;
    }

    void replaceManifests(const QList<Data::DeviceAdapterManifest> &manifests)
    {
        m_manifests = manifests;
        emit adapterManifestsChanged();
    }

    mutable Data::DeviceAdapterResolutionRequest lastResolutionRequest;
    mutable int resolutionCount = 0;
    Data::DeviceAdapterResolutionResult resolutionResult;

private:
    QList<Data::DeviceAdapterManifest> m_manifests;
};

void EtherCATCoreTests::testAutomationServiceValueLookup()
{
    TestAutomationService service;
    AutomationContextSnapshot snapshot;
    snapshot.scope = {Data::NodeId::create(), Data::NodeId::create()};
    snapshot.controllerId = automationControllerId(snapshot.scope);
    snapshot.identitySource = "ide-project-master";
    snapshot.project.id = snapshot.scope.projectId;
    snapshot.project.valid = true;
    snapshot.connection.scope = snapshot.scope;
    snapshot.mock = true;
    service.snapshots = {snapshot};

    const std::optional<AutomationContextSnapshot> first
        = service.context(snapshot.controllerId);
    QVERIFY(first);
    QCOMPARE(*first, snapshot);
    QCOMPARE(service.readCount, 1);

    service.snapshots.clear();
    QVERIFY(!service.context(snapshot.controllerId));
    QCOMPARE(service.readCount, 2);
    QCOMPARE(snapshot.identitySource, "ide-project-master");
}

class TestPropertyPageProvider final : public PropertyPageProvider
{
public:
    TestPropertyPageProvider()
        : PropertyPageProvider("EtherCAT.Test.Pages", "Test pages")
    {}

    QList<PropertyPageDescriptor> pages(const PropertyPageContext &context) const final
    {
        if (context.nodeKind != WorkbenchNodeKind::Device)
            return {};
        return {{Utils::Id("EtherCAT.Test.General"), "General", 10}};
    }

    QWidget *createPage(Utils::Id pageId, QWidget *parent) final
    {
        if (pageId != Utils::Id("EtherCAT.Test.General"))
            return nullptr;
        return new QWidget(parent);
    }

    void updatePage(Utils::Id pageId, QWidget *page, const PropertyPageContext &context) final
    {
        if (pageId == Utils::Id("EtherCAT.Test.General") && page)
            page->setObjectName(context.nodeId.toString());
    }
};

class TestControllerConnectionProvider final : public ControllerConnectionProvider
{
public:
    TestControllerConnectionProvider()
        : TestControllerConnectionProvider(
              "EtherCAT.Connection.Test",
              "Test controller",
              "192.168.3.101:15200",
              {"control", "events", "bulk"})
    {}

    TestControllerConnectionProvider(
        Utils::Id id,
        const QString &displayName,
        const QString &endpointSummary,
        const QStringList &channelIds,
        Data::NodeId profileId = {})
        : ControllerConnectionProvider(id, displayName)
        , m_channelIds(channelIds)
    {
        m_profile.id = profileId.isNull() ? Data::NodeId::create() : profileId;
        m_profile.displayName = displayName + " default";
        m_profile.endpointSummary = endpointSummary;
        m_profile.configured = true;
        m_profile.supported = true;
        m_profile.defaultProfile = true;
    }

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &scope) const final
    {
        if (scope.projectId.isNull() || scope.masterId.isNull())
            return {};
        return {m_profile};
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final { return m_snapshot; }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &request) final
    {
        if (!isAvailable())
            return Utils::ResultError("Connection provider is unavailable");
        if (request.scope.projectId.isNull() || request.scope.masterId.isNull()
            || request.profileId != m_profile.id || !m_profile.configured || !m_profile.supported)
            return Utils::ResultError("Invalid controller profile");
        if (m_snapshot.state != Data::ControllerConnectionState::Disconnected)
            return Utils::ResultError("Connection provider is busy");

        m_snapshot = {};
        m_snapshot.scope = request.scope;
        m_snapshot.profileId = request.profileId;
        m_snapshot.endpointSummary = m_profile.endpointSummary;
        m_snapshot.state = Data::ControllerConnectionState::Connecting;
        m_snapshot.readOnly = true;
        m_snapshot.sessionGeneration = ++m_generation;
        for (qsizetype index = 0; index < m_channelIds.size(); ++index) {
            const QString &channelId = m_channelIds.at(index);
            m_snapshot.channels.append(
                {channelId,
                 channelId.toUpper(),
                 index == 0 ? Data::ControllerChannelState::Connecting
                            : Data::ControllerChannelState::Disconnected,
                 channelId == "control" ? 4096 : 65536,
                 {},
                 index == 0 ? QString("Connecting") : QString()});
        }
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> disconnectFromController() final
    {
        if (m_snapshot.state == Data::ControllerConnectionState::Disconnected)
            return Utils::ResultOk;
        m_snapshot.state = Data::ControllerConnectionState::Disconnected;
        m_snapshot.sessionGeneration = ++m_generation;
        m_snapshot.session.reset();
        m_snapshot.lastHeartbeatAt = {};
        for (Data::ControllerChannelStatus &channel : m_snapshot.channels)
            channel.state = Data::ControllerChannelState::Disconnected;
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> refreshController() final
    {
        if (!isAvailable())
            return Utils::ResultError("Connection provider is unavailable");
        if (m_snapshot.state != Data::ControllerConnectionState::Connected
            && m_snapshot.state != Data::ControllerConnectionState::Degraded) {
            return Utils::ResultError("Controller is not connected");
        }
        m_snapshot.updatedAt = QDateTime::currentDateTimeUtc();
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    void setProfileConfigured(bool configured)
    {
        if (m_profile.configured == configured)
            return;
        m_profile.configured = configured;
        m_profile.configurationIssue = configured ? QString()
                                                  : QString("Controller profile is incomplete");
        emit connectionProfilesChanged();
    }

    void setProfileSupported(bool supported)
    {
        if (m_profile.supported == supported)
            return;
        m_profile.supported = supported;
        m_profile.configurationIssue = supported ? QString()
                                                 : QString("Controller profile is not supported");
        emit connectionProfilesChanged();
    }

    void completeHandshake()
    {
        m_snapshot.state = Data::ControllerConnectionState::Connected;
        m_snapshot.protocolVersion = Data::ControllerProtocolVersion{1, 9};
        m_snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        m_snapshot.updatedAt = m_snapshot.connectedAt;
        m_snapshot.lastHeartbeatAt = m_snapshot.connectedAt;
        m_snapshot.session = Data::ControllerSessionSummary{17, 42, 0, 5000, false};
        for (Data::ControllerChannelStatus &channel : m_snapshot.channels) {
            channel.state = Data::ControllerChannelState::Connected;
            channel.lastActivityAt = m_snapshot.connectedAt;
            channel.detail.clear();
        }

        Data::ControllerStateSummary state;
        state.serviceState = Data::ControllerServiceState::Shutdown;
        state.severity = Data::ControllerSeverity::None;
        state.ready = true;
        state.controllerBootId = 42;
        m_snapshot.controllerState = state;

        Data::ControllerCapabilitySummary capability;
        capability.maximumSlaves = 64;
        capability.minimumCycleTimeNs = 125000;
        capability.maximumCyclicFrames = 8;
        capability.maximumProcessInputBytes = 4096;
        capability.maximumProcessOutputBytes = 4096;
        capability.descriptorSha256 = QByteArray::fromHex(
            "74ea5e67b3e1d7ba575339b636abb5432ecf6790d3504d32ce1756bcfec49568");
        capability.controlLease = true;
        capability.resumablePush = true;
        capability.transactionalBulk = true;
        capability.capabilityQuery = true;
        capability.exactAlarmReplay = true;
        capability.linkDiagnostics = true;
        capability.timeCorrelation = true;
        capability.firmwareUpdate = true;
        capability.coe = true;
        capability.distributedClocks = true;
        m_snapshot.capability = capability;

        Data::ControllerPackageSummary package;
        package.stagedSlot = Data::ControllerSlot::A;
        package.stagedGeneration = 11;
        package.stagedConfigurationId = 810;
        package.activeSlot = Data::ControllerSlot::A;
        package.activeGeneration = 11;
        package.activeConfigurationId = 810;
        package.controllerState = Data::ControllerPackageState::Empty;
        package.controllerBootId = 42;
        m_snapshot.package = package;

        Data::ControllerFirmwareSummary firmware;
        firmware.state = Data::ControllerFirmwareState::Confirmed;
        firmware.activeSlot = Data::ControllerSlot::A;
        firmware.previousSlot = Data::ControllerSlot::B;
        firmware.targetSlot = Data::ControllerSlot::A;
        firmware.packageVerified = true;
        firmware.confirmed = true;
        firmware.progressPerMille = 1000;
        firmware.generation = 2517;
        firmware.updatedAt = m_snapshot.connectedAt;
        m_snapshot.firmware = firmware;
        emit connectionSnapshotChanged();
    }

    void degradePush()
    {
        m_snapshot.state = Data::ControllerConnectionState::Degraded;
        m_snapshot.channels[1].state = Data::ControllerChannelState::Failed;
        m_snapshot.channels[1].detail = "Push channel unavailable";
        emit connectionSnapshotChanged();
    }

    void failProtocol()
    {
        Data::ControllerOperationError error;
        error.source = Data::ControllerErrorSource::Protocol;
        error.channelId = "events";
        error.operation = Data::ControllerOperation::SubscribeEvents;
        error.codeName = "UNEXPECTED_PUSH_FRAME";
        error.requestId = 73;
        error.occurredAt = QDateTime::currentDateTimeUtc();
        error.retryDisposition = Data::ControllerRetryDisposition::Reconnect;
        error.summary = "Unexpected push frame";
        m_snapshot.state = Data::ControllerConnectionState::Failed;
        m_snapshot.lastError = error;
        emit connectionSnapshotChanged();
    }

private:
    Data::ControllerConnectionProfile m_profile;
    QStringList m_channelIds;
    Data::ControllerConnectionSnapshot m_snapshot;
    quint64 m_generation = 0;
};

class TestScanProvider final : public ScanProvider
{
public:
    TestScanProvider()
        : ScanProvider("EtherCAT.Scan.Test", "Test scanner")
    {}

    Data::ScanState scanState() const final { return m_progress.state; }
    Data::ScanProgress scanProgress() const final { return m_progress; }
    std::optional<Data::ScanResult> lastScanResult() const final { return m_result; }
    QString lastScanError() const final { return m_error; }

    Utils::Result<> startScan(const Data::ScanRequest &request) final
    {
        if (request.projectId.isNull() || request.masterId.isNull())
            return Utils::ResultError("Invalid scan request");
        if (m_progress.state != Data::ScanState::Idle)
            return Utils::ResultError("Scanner is busy");
        m_request = request;
        m_progress = {Data::ScanState::Preparing, 1, 4, 0, "Preparing"};
        emit scanStateChanged(m_progress.state);
        emit scanProgressChanged(m_progress);
        return Utils::ResultOk;
    }

    void complete()
    {
        Data::ScannedSlave slave;
        slave.id = Data::NodeId::create();
        slave.position = 0;
        slave.identity = {2, 0x1234, 1};
        slave.serialNumber = 17;
        slave.name = "Mock Slave";

        Data::ScanResult result;
        result.snapshot.id = Data::NodeId::create();
        result.snapshot.projectId = m_request.projectId;
        result.snapshot.masterId = m_request.masterId;
        result.snapshot.slaves = {slave};
        result.snapshot.complete = true;
        result.snapshot.mock = true;
        result.comparison.projectId = m_request.projectId;
        result.comparison.masterId = m_request.masterId;
        result.comparison.differences = {
            {Data::TopologyDifferenceKind::Added,
             Data::DifferenceSeverity::Information,
             {},
             slave.id,
             -1,
             0,
             "Added slave",
             "Mock Slave"}};
        result.comparison.acceptAllowed = true;
        m_result = result;
        m_progress = {Data::ScanState::Completed, 4, 4, 1, "Completed"};
        emit scanProgressChanged(m_progress);
        emit scanResultChanged();
        emit scanStateChanged(m_progress.state);
        emit scanFinished(m_progress.state);
    }

    void cancelScan() final
    {
        if (m_progress.state == Data::ScanState::Idle)
            return;
        m_progress.state = Data::ScanState::Cancelled;
        emit scanStateChanged(m_progress.state);
        emit scanFinished(m_progress.state);
    }

    void clearScanResult() final
    {
        m_result.reset();
        m_error.clear();
        m_progress = {};
        emit scanResultChanged();
        emit scanStateChanged(m_progress.state);
    }

private:
    Data::ScanProgress m_progress;
    Data::ScanRequest m_request;
    std::optional<Data::ScanResult> m_result;
    QString m_error;
};

class TestDiagnosticsProvider final : public DiagnosticsProvider
{
public:
    TestDiagnosticsProvider()
        : DiagnosticsProvider("EtherCAT.Diagnostics.Test", "Test diagnostics")
    {
        m_limits = {4, 5, 10, 100};
    }

    Data::DiagnosticsStreamState streamState() const final { return m_state; }
    Data::DiagnosticsRequest activeRequest() const final { return m_request; }
    std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const final { return m_snapshot; }
    QList<Data::DiagnosticEvent> events() const final { return m_events; }
    QList<Data::DiagnosticTrendSample> trendSamples() const final { return m_trend; }
    Data::DiagnosticsLimits limits() const final { return m_limits; }
    QString lastDiagnosticsError() const final { return m_error; }

    Utils::Result<> startMonitoring(const Data::DiagnosticsRequest &request) final
    {
        if (request.projectId.isNull() || request.masterId.isNull())
            return Utils::ResultError("Invalid diagnostics request");
        if (m_state != Data::DiagnosticsStreamState::Stopped)
            return Utils::ResultError("Diagnostics provider is busy");
        m_request = request;
        m_state = Data::DiagnosticsStreamState::Starting;
        emit streamStateChanged(m_state);
        m_state = Data::DiagnosticsStreamState::Running;
        emit streamStateChanged(m_state);
        return Utils::ResultOk;
    }

    void stopMonitoring() final
    {
        if (m_state == Data::DiagnosticsStreamState::Stopped)
            return;
        const bool alreadyFinished = m_state == Data::DiagnosticsStreamState::Failed;
        m_state = Data::DiagnosticsStreamState::Stopping;
        emit streamStateChanged(m_state);
        m_state = Data::DiagnosticsStreamState::Stopped;
        m_request = {};
        emit streamStateChanged(m_state);
        if (!alreadyFinished)
            emit monitoringStopped();
    }

    Utils::Result<> requestRunMode(Data::DiagnosticsRunMode mode) final
    {
        if (m_state != Data::DiagnosticsStreamState::Running || !m_snapshot)
            return Utils::ResultError("Diagnostics provider is not running");
        m_snapshot->runMode = mode;
        emit diagnosticsSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> acknowledgeAlarm(const Data::NodeId &eventId) final
    {
        const auto alarm = std::find_if(
            m_events.begin(), m_events.end(), [&eventId](const Data::DiagnosticEvent &event) {
                return event.id == eventId && event.lifecycle == Data::AlarmLifecycle::Active;
            });
        if (alarm == m_events.end())
            return Utils::ResultError("Active alarm not found");
        alarm->lifecycle = Data::AlarmLifecycle::Acknowledged;
        alarm->acknowledgedAt = QDateTime::currentDateTimeUtc();
        emit diagnosticEventsChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> clearRecoveredEvents() final
    {
        m_events.erase(
            std::remove_if(
                m_events.begin(),
                m_events.end(),
                [](const Data::DiagnosticEvent &event) {
                    return event.lifecycle == Data::AlarmLifecycle::Recovered;
                }),
            m_events.end());
        emit diagnosticEventsChanged();
        return Utils::ResultOk;
    }

    void publish()
    {
        const Data::NodeId slaveId = Data::NodeId::create();
        Data::DiagnosticsSnapshot snapshot;
        snapshot.projectId = m_request.projectId;
        snapshot.masterId = m_request.masterId;
        snapshot.generation = 7;
        snapshot.capturedAt = QDateTime::currentDateTimeUtc();
        snapshot.mock = true;
        snapshot.runMode = Data::DiagnosticsRunMode::Run;
        snapshot.masterState = Data::EtherCATState::Operational;
        snapshot.masterAlStatusText = "No error";
        snapshot.workingCounter = {4, 3, Data::WorkingCounterState::Incomplete, 2};
        snapshot.masterPorts = {{0, Data::LinkState::Up, true, 1, 0, 0}};
        snapshot.frameErrors = {1, 2, 3, 4, 5, 6};
        snapshot.distributedClock = {Data::DcSyncState::Synchronized, 17, 30, 4, 1};
        snapshot.cycle = {125000, 125010, 124990, 125020, 10, 20000, 8, 0};
        snapshot.slaves = {
            {slaveId,
             0,
             "Mock Slave",
             Data::EtherCATState::Operational,
             false,
             0,
             "No error",
             Data::WorkingCounterState::Valid,
             {{0, Data::LinkState::Up, true, 0, 0, 0}},
             {},
             {Data::DcSyncState::Synchronized, 5, 12, 2, 0},
             snapshot.capturedAt}};
        snapshot.activeAlarmCount = 1;
        snapshot.unacknowledgedAlarmCount = 1;
        snapshot.sourceSampleCount = 10;
        snapshot.coalescedSampleCount = 9;
        m_snapshot = snapshot;

        Data::DiagnosticEvent alarm;
        alarm.id = Data::NodeId::create();
        alarm.sequence = 11;
        alarm.occurredAt = snapshot.capturedAt;
        alarm.nodeId = slaveId;
        alarm.kind = Data::DiagnosticEventKind::Alarm;
        alarm.severity = Data::DiagnosticSeverity::Error;
        alarm.lifecycle = Data::AlarmLifecycle::Active;
        alarm.code = "MOCK-WKC";
        alarm.summary = "Working Counter mismatch";
        alarm.repeatCount = 2;
        m_events = {alarm};
        m_trend = {{12, snapshot.capturedAt, 3, 125010, 10, 20000, 17}};
        emit diagnosticsSnapshotChanged();
        emit diagnosticEventsChanged();
        emit diagnosticTrendChanged();
    }

    void recoverAlarm()
    {
        m_events[0].lifecycle = Data::AlarmLifecycle::Recovered;
        m_events[0].recoveredAt = QDateTime::currentDateTimeUtc();
        emit diagnosticEventsChanged();
    }

    void fail(const QString &error)
    {
        m_error = error;
        m_state = Data::DiagnosticsStreamState::Failed;
        emit streamStateChanged(m_state);
        emit monitoringStopped();
    }

private:
    Data::DiagnosticsStreamState m_state = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_request;
    std::optional<Data::DiagnosticsSnapshot> m_snapshot;
    QList<Data::DiagnosticEvent> m_events;
    QList<Data::DiagnosticTrendSample> m_trend;
    Data::DiagnosticsLimits m_limits;
    QString m_error;
};

void EtherCATCoreTests::testMetadataAndServices()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        "ethercatcore");
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATCore"));
    QVERIFY(!spec->version().isEmpty());
    QVERIFY(!spec->compatVersion().isEmpty());
    QVERIFY(!spec->hasError());
    const QList<ExtensionSystem::PluginDependency> dependencies = spec->dependencies();
    const bool hasCoreDependency = std::any_of(
        dependencies.cbegin(),
        dependencies.cend(),
        [](const ExtensionSystem::PluginDependency &dependency) {
            return dependency.id == "core"
                   && dependency.type == ExtensionSystem::PluginDependency::Required;
        });
    QVERIFY(hasCoreDependency);

    QVERIFY(ExtensionSystem::PluginManager::getObject<SelectionService>());
    QVERIFY(ExtensionSystem::PluginManager::getObject<StateService>());
    QVERIFY(ExtensionSystem::PluginManager::getObject<ProviderRegistry>());
}

void EtherCATCoreTests::testMockUiVisibility()
{
    static constexpr char environmentVariable[] = "QTC_ETHER_CAT_ENABLE_MOCK_UI";
    const bool wasSet = qEnvironmentVariableIsSet(environmentVariable);
    const QByteArray previousValue = qgetenv(environmentVariable);
    const QScopeGuard restoreEnvironment([wasSet, previousValue] {
        if (wasSet)
            qputenv(environmentVariable, previousValue);
        else
            qunsetenv(environmentVariable);
    });

    qunsetenv(environmentVariable);
    QVERIFY(isMockUiEnabled());

    qputenv(environmentVariable, "0");
    QVERIFY(!isMockUiEnabled());

    qputenv(environmentVariable, "1");
    QVERIFY(isMockUiEnabled());

    qputenv(environmentVariable, "true");
    QVERIFY(!isMockUiEnabled());
}

void EtherCATCoreTests::testNodeIdRoundTrip()
{
    const Data::NodeId created = Data::NodeId::create();
    QVERIFY(!created.isNull());
    QCOMPARE(Data::NodeId::fromString(created.toString()), created);
    QCOMPARE(qHash(Data::NodeId::fromString(created.toString())), qHash(created));
    QVERIFY(Data::NodeId::fromString("not-a-node-id").isNull());
}

void EtherCATCoreTests::testProjectSnapshotValueSemantics()
{
    using RenameStructuralNodeMethod = Utils::Result<> (ProjectService::*)(
        const Data::NodeId &, const Data::NodeId &, const QString &);
    static_assert(
        std::is_same_v<
            decltype(&ProjectService::renameStructuralNode), RenameStructuralNodeMethod>);
    using SetMasterConfigurationMethod = Utils::Result<> (ProjectService::*)(
        const Data::NodeId &,
        const Data::NodeId &,
        const Data::MasterConfiguration &);
    static_assert(
        std::is_same_v<
            decltype(&ProjectService::setMasterConfiguration),
            SetMasterConfigurationMethod>);

    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    Data::ProjectSnapshot snapshot{
        projectId,
        "Line 1",
        1,
        "Embed Labs 20.0.1",
        {{projectId, {}, Data::ProjectNodeKind::Project, "Line 1"},
         {targetId, projectId, Data::ProjectNodeKind::Target, "Target Controller"}},
        true,
        true,
        false,
        {},
        {},
        {},
    };

    const Data::ProjectSnapshot copy = snapshot;
    QCOMPARE(copy, snapshot);
    snapshot.nodes[1].name = "Offline Target";
    QVERIFY(copy != snapshot);
    QCOMPARE(copy.nodes[1].parentId, projectId);

    Data::OfflineSlaveConfiguration slave;
    slave.id = Data::NodeId::create();
    slave.masterId = targetId;
    slave.position = 0;
    slave.identity = {2, 0x1234, 1};
    slave.name = "Offline Slave";
    snapshot.slaves.append(slave);
    QVERIFY(copy != snapshot);
}

void EtherCATCoreTests::testProcessDataConfigurationPreview()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::MasterToSlave, true, 3},
        {Data::NodeId::create(), 3, "Inputs", Data::SyncManagerDirection::SlaveToMaster, true, 3},
    };

    Data::PdoConfiguration outputs;
    outputs.id = Data::NodeId::create();
    outputs.index = 0x1600;
    outputs.name = "Outputs";
    outputs.direction = Data::PdoDirection::Rx;
    outputs.syncManager = 2;
    outputs.selected = true;
    outputs.mandatory = true;
    outputs.defaultSelected = true;
    outputs.entries = {
        {Data::NodeId::create(), 0x7000, 1, "Enable", 1, Data::EtherCATDataType::Boolean, "BOOL"},
        {Data::NodeId::create(),
         0x7000,
         2,
         "Target",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT"},
    };

    Data::PdoConfiguration inputs;
    inputs.id = Data::NodeId::create();
    inputs.index = 0x1a00;
    inputs.name = "Inputs";
    inputs.direction = Data::PdoDirection::Tx;
    inputs.syncManager = 3;
    inputs.selected = true;
    inputs.entries = {
        {Data::NodeId::create(),
         0x6000,
         1,
         "Status",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         8}};
    configuration.pdos = {outputs, inputs};

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    QVERIFY(!validation.hasErrors());
    QVERIFY(validation.issues.isEmpty());
    QCOMPARE(validation.processImage.outputs.bitSize, 17);
    QCOMPARE(validation.processImage.outputs.byteSize, 3);
    QCOMPARE(validation.processImage.outputs.entries.size(), 2);
    QCOMPARE(validation.processImage.outputs.entries.at(0).bitOffset, 0);
    QCOMPARE(validation.processImage.outputs.entries.at(1).bitOffset, 1);
    QCOMPARE(validation.processImage.inputs.bitSize, 24);
    QCOMPARE(validation.processImage.inputs.byteSize, 3);
    QCOMPARE(validation.processImage.inputs.entries.first().bitOffset, 8);
}

void EtherCATCoreTests::testProcessDataConfigurationValidation()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::SlaveToMaster, true, 1},
    };

    Data::PdoConfiguration first;
    first.id = Data::NodeId::create();
    first.index = 0x1600;
    first.name = "First";
    first.direction = Data::PdoDirection::Rx;
    first.syncManager = 2;
    first.selected = true;
    first.mappingSupported = false;
    first.entries = {
        {Data::NodeId::create(),
         0x7000,
         1,
         "Invalid width",
         12,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         0}};

    Data::PdoConfiguration duplicate = first;
    duplicate.id = Data::NodeId::create();
    duplicate.name = "Duplicate";
    duplicate.mappingSupported = true;
    duplicate.entries.first().id = Data::NodeId::create();
    duplicate.entries.first().bitLength = 16;
    duplicate.entries.first().requestedBitOffset = 8;

    Data::PdoConfiguration mandatory;
    mandatory.id = Data::NodeId::create();
    mandatory.index = 0x1601;
    mandatory.direction = Data::PdoDirection::Rx;
    mandatory.syncManager = 4;
    mandatory.mandatory = true;
    mandatory.selected = false;
    configuration.pdos = {first, duplicate, mandatory};

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    QVERIFY(validation.hasErrors());
    const auto hasIssue = [&validation](Data::ConfigurationIssueCode code) {
        return std::any_of(
            validation.issues.cbegin(),
            validation.issues.cend(),
            [code](const Data::ConfigurationIssue &issue) { return issue.code == code; });
    };
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::SyncManagerDirectionMismatch));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::UnsupportedPdoMapping));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DataTypeBitLengthMismatch));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DuplicatePdoAssignment));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DuplicatePdoEntry));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::ProcessImageOverlap));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::SyncManagerSizeExceeded));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::MandatoryPdoNotSelected));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::MissingSyncManager));
}

void EtherCATCoreTests::testStartupAndDcConfigurationValidation()
{
    Data::StartupConfiguration startup;
    startup.parameters = {
        {Data::NodeId::create(),
         true,
         0,
         "IP",
         0x8000,
         1,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         QByteArray::fromHex("0100"),
         "Mode"},
        {Data::NodeId::create(),
         true,
         1,
         "PS",
         0x8000,
         2,
         Data::EtherCATDataType::Boolean,
         "BOOL",
         QByteArray::fromHex("01"),
         "Enable"},
    };
    QVERIFY(Data::validateStartupConfiguration(startup).isEmpty());

    startup.parameters[1].order = 0;
    startup.parameters[1].rawValue.clear();
    const QList<Data::ConfigurationIssue> startupIssues = Data::validateStartupConfiguration(
        startup);
    const auto hasStartupIssue = [&startupIssues](Data::ConfigurationIssueCode code) {
        return std::any_of(
            startupIssues.cbegin(),
            startupIssues.cend(),
            [code](const Data::ConfigurationIssue &issue) { return issue.code == code; });
    };
    QVERIFY(hasStartupIssue(Data::ConfigurationIssueCode::DuplicateStartupOrder));
    QVERIFY(hasStartupIssue(Data::ConfigurationIssueCode::InvalidStartupValueSize));

    Data::DcConfiguration dc;
    dc.enabled = true;
    dc.modeName = "DC-Synchronous";
    dc.assignActivate = 0x0300;
    dc.sync0 = {true, 125000, -1000};
    dc.sync1 = {true, 125000, 1000};
    QVERIFY(Data::validateDcConfiguration(dc).isEmpty());

    dc.sync0.shiftTimeNs = 126000;
    dc.sync1.cycleTimeNs = 0;
    const QList<Data::ConfigurationIssue> dcIssues = Data::validateDcConfiguration(dc);
    const auto hasDcIssue = [&dcIssues](Data::ConfigurationIssueCode code) {
        return std::any_of(
            dcIssues.cbegin(), dcIssues.cend(), [code](const Data::ConfigurationIssue &issue) {
                return issue.code == code;
            });
    };
    QVERIFY(hasDcIssue(Data::ConfigurationIssueCode::DcShiftOutOfRange));
    QVERIFY(hasDcIssue(Data::ConfigurationIssueCode::InvalidDcCycle));
}

void EtherCATCoreTests::testDeviceDescriptionAndImportJobContract()
{
    Data::DeviceDescription description;
    description.summary
        = {Data::NodeId::create(),
           {2, 0x12345678, 0x00010002},
           "Servo Drive",
           "Drive-X",
           "Drives",
           true};
    description.syncManagers.append(
        {2, "Outputs", Data::SyncManagerDirection::MasterToSlave, 0x1000, 32, 0x24, true});
    description.rxPdos.append(
        {0x1600,
         "Command",
         Data::PdoDirection::Rx,
         2,
         true,
         true,
         {{0x6040, 0, "Controlword", 16, Data::EtherCATDataType::UnsignedInteger16, "UINT"}}});
    description.coe = {true, true, true, true, false};
    description.dcModes.append({"DC-Synchronous", 0x0300, 125000, 0, 0, 0});

    const Data::DeviceDescription copy = description;
    QCOMPARE(copy, description);
    QCOMPARE(copy.rxPdos.first().entries.first().bitLength, 16);

    TestDeviceImportJob job;
    QSignalSpy stateSpy(&job, &DeviceImportJob::stateChanged);
    QSignalSpy progressSpy(&job, &DeviceImportJob::progressChanged);
    QSignalSpy finishedSpy(&job, &DeviceImportJob::finished);
    job.start();
    QCOMPARE(job.state(), DeviceImportState::Running);
    QCOMPARE(job.progressValue(), 1);
    QCOMPARE(job.progressMaximum(), 2);
    job.cancel();
    QCOMPARE(job.state(), DeviceImportState::Finished);
    QVERIFY(job.result().canceled);
    QCOMPARE(stateSpy.count(), 3);
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    job.cancel();
    QCOMPARE(finishedSpy.count(), 1);
}

void EtherCATCoreTests::testDeviceAdapterValueSemantics()
{
    const Data::DeviceAdapterManifest qualified = testDeviceAdapterManifest();
    const Data::DeviceAdapterManifest copy = qualified;
    QCOMPARE(copy, qualified);

    QCOMPARE(qualified.id.value, QString("org.example.test.adapter/custom-drive"));
    QCOMPARE(
        qualified.capabilities.at(1).value,
        QString("example.test.capability/custom-diagnostics"));
    QCOMPARE(
        qualified.semanticSignals.constFirst().id.value,
        QString("urn:example.test:signal/custom.axis.target-velocity"));
    QCOMPARE(qualified.qualification, Data::DeviceAdapterQualification::Qualified);
    QVERIFY(qualified.signatureVerified);
    QVERIFY(qualified.realHardwareAllowed);

    QCOMPARE(qualified.match.vendorId, quint32(0x00a1b2c3));
    QCOMPARE(qualified.match.productCode, quint32(0x01020304));
    QCOMPARE(qualified.match.minimumRevision, quint32(0x00020003));
    QCOMPARE(qualified.match.maximumRevision, quint32(0x00020003));
    QCOMPARE(qualified.match.exactEsiSha256.size(), 32);
    QCOMPARE(
        qualified.match.exactEsiSha256.toHex(),
        QByteArray("1111111111111111111111111111111111111111111111111111111111111111"));

    const Data::SemanticSignalDefinition &signal = qualified.semanticSignals.constFirst();
    QCOMPARE(signal.direction, Data::SemanticSignalDirection::Output);
    QCOMPARE(signal.access, Data::SemanticSignalAccess::WriteOnly);
    QCOMPARE(signal.bindings.size(), 1);
    const Data::DeviceSignalBinding &binding = signal.bindings.constFirst();
    QCOMPARE(binding.kind, Data::DeviceSignalBindingKind::ProcessDataObject);
    QCOMPARE(binding.pdoDirection, Data::PdoDirection::Rx);
    QCOMPARE(binding.pdoIndex, quint16(0x1600));
    QCOMPARE(binding.objectIndex, quint16(0x7001));
    QCOMPARE(binding.objectSubIndex, quint8(0));
    QCOMPARE(binding.physicalType, Data::EtherCATDataType::Integer32);
    QCOMPARE(binding.bitWidth, 32);
    QCOMPARE(binding.byteOrder, Data::DeviceByteOrder::LittleEndian);
    QCOMPARE(signal.valueMetadata.unit, QString("rpm"));
    QCOMPARE(signal.valueMetadata.scale, 0.1);
    QCOMPARE(signal.valueMetadata.offset, -1.0);
    QVERIFY(signal.valueMetadata.hasMinimum);
    QCOMPARE(signal.valueMetadata.minimum, -3000.0);
    QVERIFY(signal.valueMetadata.hasMaximum);
    QCOMPARE(signal.valueMetadata.maximum, 3000.0);
    QVERIFY(signal.valueMetadata.hasStep);
    QCOMPARE(signal.valueMetadata.step, 0.1);
    QCOMPARE(
        signal.valueMetadata.enumValues,
        QList<Data::SemanticEnumValue>({{0, "stopped", "Stopped"}}));
    QVERIFY(signal.hasSafeValue);
    QCOMPARE(signal.safeValue, QVariant(0));
    QVERIFY(signal.manualControl.allowed);
    QVERIFY(signal.manualControl.requiresExclusiveControl);
    QVERIFY(signal.manualControl.holdToRun);
    QCOMPARE(signal.manualControl.commandTimeoutMs, quint32(250));
    QCOMPARE(signal.manualControl.timeoutAction, Data::ManualControlTimeoutAction::ControlledStop);

    Data::DeviceAdapterManifest changedRevision = qualified;
    changedRevision.match.minimumRevision = 0x00020004;
    QVERIFY(changedRevision != qualified);
    Data::DeviceAdapterManifest changedEsi = qualified;
    changedEsi.match.exactEsiSha256[0] = char(changedEsi.match.exactEsiSha256.at(0) ^ char(0xff));
    QVERIFY(changedEsi != qualified);

    Data::DeviceAdapterManifest candidate
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::Candidate, "2.2.0-rc1");
    QVERIFY(candidate != qualified);
    QVERIFY(!candidate.signatureVerified);
    QVERIFY(!candidate.realHardwareAllowed);
    QCOMPARE(candidate.qualification, Data::DeviceAdapterQualification::Candidate);
    Data::DeviceAdapterManifest mock
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::MockOnly, "mock-1");
    QCOMPARE(mock.qualification, Data::DeviceAdapterQualification::MockOnly);
    QVERIFY(!mock.realHardwareAllowed);
    QVERIFY(Data::DeviceAdapterQualification::Unqualified != candidate.qualification);
    QVERIFY(candidate.qualification != qualified.qualification);
    QVERIFY(qualified.qualification != mock.qualification);

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device.summary.identity
        = {qualified.match.vendorId, qualified.match.productCode, qualified.match.minimumRevision};
    request.device.sourceSha256 = qualified.match.exactEsiSha256;
    request.allowCandidate = true;
    request.allowMock = false;
    request.requireRealHardwareQualification = false;
    const Data::NodeId pdoId = Data::NodeId::create();
    const Data::NodeId entryId = Data::NodeId::create();
    const Data::NodeId syncManagerId = Data::NodeId::create();
    request.processImage.outputs.bitSize = 32;
    request.processImage.outputs.byteSize = 4;
    request.processImage.outputs.entries = {
        {pdoId,
         entryId,
         syncManagerId,
         binding.pdoIndex,
         binding.objectIndex,
         binding.objectSubIndex,
         "Target velocity",
         binding.pdoDirection,
         2,
         96,
         binding.bitWidth,
         12,
         0,
         binding.physicalType}};
    QCOMPARE(Data::DeviceAdapterResolutionRequest(request), request);

    Data::BoundSemanticSignal boundSignal;
    boundSignal.definition = signal;
    boundSignal.binding = binding;
    boundSignal.processImageEntryId = entryId;
    boundSignal.processImageBitOffset = 96;
    boundSignal.processImageBitLength = 32;

    Data::ResolvedDeviceModel model;
    model.slaveId = request.slaveId;
    model.identity = request.device.summary.identity;
    model.esiSha256 = request.device.sourceSha256;
    model.adapterId = qualified.id;
    model.adapterVersion = qualified.version;
    model.qualification = qualified.qualification;
    model.capabilities = qualified.capabilities;
    model.boundSignals = {boundSignal};
    model.warnings = {"Test-only resolved model"};
    model.complete = true;
    QCOMPARE(Data::ResolvedDeviceModel(model), model);
    QCOMPARE(model.boundSignals.constFirst().processImageEntryId, entryId);
    QCOMPARE(model.boundSignals.constFirst().processImageBitOffset, qint64(96));
    QCOMPARE(model.identity.revisionNumber, qualified.match.minimumRevision);
    QCOMPARE(model.esiSha256, qualified.match.exactEsiSha256);

    const Data::DeviceAdapterResolutionResult result{true, model, {}};
    QCOMPARE(Data::DeviceAdapterResolutionResult(result), result);
    QVERIFY(result.resolved);
    QVERIFY(result.error.isEmpty());
}

void EtherCATCoreTests::testDeviceAdapterProviderContract()
{
    const Data::DeviceAdapterManifest qualified = testDeviceAdapterManifest();
    const Data::DeviceAdapterManifest candidate
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::Candidate, "2.2.0-rc1");
    TestDeviceAdapterProvider provider({qualified, candidate});

    QCOMPARE(int(ProviderKind::DeviceAdapter), 6);
    QCOMPARE(provider.kind(), ProviderKind::DeviceAdapter);
    QCOMPARE(provider.adapterManifests(), QList<Data::DeviceAdapterManifest>({qualified, candidate}));
    const std::optional<Data::DeviceAdapterManifest> exact
        = provider.adapterManifest(qualified.id, qualified.version);
    QVERIFY(exact);
    QCOMPARE(*exact, qualified);
    const std::optional<Data::DeviceAdapterManifest> candidateVersion
        = provider.adapterManifest(candidate.id, candidate.version);
    QVERIFY(candidateVersion);
    QCOMPARE(*candidateVersion, candidate);
    QVERIFY(!provider.adapterManifest(qualified.id, "missing-version"));
    QVERIFY(!provider.adapterManifest({"org.example.test.adapter/missing"}, qualified.version));

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device.summary.identity
        = {qualified.match.vendorId, qualified.match.productCode, qualified.match.minimumRevision};
    request.device.sourceSha256 = qualified.match.exactEsiSha256;
    request.allowCandidate = false;
    request.allowMock = false;
    request.requireRealHardwareQualification = true;
    provider.resolutionResult.resolved = true;
    provider.resolutionResult.model.slaveId = request.slaveId;
    provider.resolutionResult.model.identity = request.device.summary.identity;
    provider.resolutionResult.model.esiSha256 = request.device.sourceSha256;
    provider.resolutionResult.model.adapterId = qualified.id;
    provider.resolutionResult.model.adapterVersion = qualified.version;
    provider.resolutionResult.model.qualification = qualified.qualification;
    provider.resolutionResult.model.complete = true;
    const Data::DeviceAdapterResolutionResult resolution = provider.resolveDevice(request);
    QVERIFY(resolution.resolved);
    QCOMPARE(resolution, provider.resolutionResult);
    QCOMPARE(provider.lastResolutionRequest, request);
    QCOMPARE(provider.resolutionCount, 1);

    QSignalSpy manifestsSpy(&provider, &DeviceAdapterProvider::adapterManifestsChanged);
    provider.replaceManifests({qualified});
    QCOMPARE(manifestsSpy.count(), 1);
    QCOMPARE(provider.adapterManifests(), QList<Data::DeviceAdapterManifest>({qualified}));
    QVERIFY(!provider.adapterManifest(candidate.id, candidate.version));

    QSignalSpy availabilitySpy(&provider, &Provider::availabilityChanged);
    QVERIFY(!provider.isAvailable());
    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());
    QCOMPARE(availabilitySpy.count(), 1);

    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);
    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    ExtensionSystem::PluginManager::addObject(&provider);
    QScopeGuard removeProvider(
        [&provider] { ExtensionSystem::PluginManager::removeObject(&provider); });
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::DeviceAdapter), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    ExtensionSystem::PluginManager::removeObject(&provider);
    removeProvider.dismiss();
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(registry->providers(ProviderKind::DeviceAdapter).isEmpty());
    QCOMPARE(removedSpy.count(), 1);
}

void EtherCATCoreTests::testPropertyPageProviderContract()
{
    const PropertyPageContext
        context{Data::NodeId::create(), Data::NodeId::create(), WorkbenchNodeKind::Device, "Drive"};
    const PropertyPageContext copy = context;
    QCOMPARE(copy, context);

    TestPropertyPageProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::PropertyPage);
    const QList<PropertyPageDescriptor> pages = provider.pages(context);
    QCOMPARE(
        pages, QList<PropertyPageDescriptor>({{Utils::Id("EtherCAT.Test.General"), "General", 10}}));

    std::unique_ptr<QWidget> page(provider.createPage(pages.first().id, nullptr));
    QVERIFY(page);
    provider.updatePage(pages.first().id, page.get(), context);
    QCOMPARE(page->objectName(), context.nodeId.toString());

    PropertyPageContext projectContext = context;
    projectContext.nodeKind = WorkbenchNodeKind::Project;
    QVERIFY(provider.pages(projectContext).isEmpty());
}

void EtherCATCoreTests::testWorkbenchDerivedNodeKinds()
{
    QCOMPARE(int(WorkbenchNodeKind::None), 0);
    QCOMPARE(int(WorkbenchNodeKind::Project), 1);
    QCOMPARE(int(WorkbenchNodeKind::Target), 2);
    QCOMPARE(int(WorkbenchNodeKind::Master), 3);
    QCOMPARE(int(WorkbenchNodeKind::DeviceRepository), 4);
    QCOMPARE(int(WorkbenchNodeKind::Device), 5);
    QCOMPARE(int(WorkbenchNodeKind::ConfiguredSlave), 6);
    QCOMPARE(int(WorkbenchNodeKind::Diagnostics), 7);
    QCOMPARE(int(WorkbenchNodeKind::Placeholder), 8);

    QCOMPARE(int(WorkbenchNodeKind::ProcessInputs), 9);
    QCOMPARE(int(WorkbenchNodeKind::ProcessOutputs), 10);
    QCOMPARE(int(WorkbenchNodeKind::RxPdoGroup), 11);
    QCOMPARE(int(WorkbenchNodeKind::TxPdoGroup), 12);
    QCOMPARE(int(WorkbenchNodeKind::Pdo), 13);
    QCOMPARE(int(WorkbenchNodeKind::PdoEntry), 14);
    QCOMPARE(int(WorkbenchNodeKind::Modules), 15);
    QCOMPARE(int(WorkbenchNodeKind::Module), 16);
    QCOMPARE(int(WorkbenchNodeKind::Channel), 17);

    const PropertyPageContext context{Data::NodeId::create(),
                                      Data::NodeId::create(),
                                      WorkbenchNodeKind::PdoEntry,
                                      "Controlword"};
    QCOMPARE(PropertyPageContext(context), context);
}

void EtherCATCoreTests::testControllerConnectionProviderContract()
{
    const Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    QCOMPARE(Data::ControllerConnectionScope(scope), scope);
    TestControllerConnectionProvider provider;
    QCOMPARE(int(ProviderKind::ControllerConnection), 5);
    QCOMPARE(provider.kind(), ProviderKind::ControllerConnection);
    const QList<Data::ControllerConnectionProfile> profiles = provider.connectionProfiles(scope);
    QCOMPARE(profiles.size(), 1);
    const Data::ControllerConnectionProfile profile = profiles.constFirst();
    QVERIFY(!profile.id.isNull());
    QVERIFY(profile.configured);
    QVERIFY(profile.supported);
    QVERIFY(profile.defaultProfile);
    QCOMPARE(profile.endpointSummary, QString("192.168.3.101:15200"));
    QCOMPARE(Data::ControllerConnectionProfile(profile), profile);
    const Data::ControllerConnectionRequest request{scope, profile.id};
    QCOMPARE(Data::ControllerConnectionRequest(request), request);
    Data::ControllerConnectionProfileConfiguration configuration;
    configuration.profileId = profile.id;
    configuration.endpoint = profile.endpointSummary;
    configuration.placeholder = QStringLiteral("192.168.3.101:15200");
    configuration.editable = true;
    QCOMPARE(Data::ControllerConnectionProfileConfiguration(configuration), configuration);
    QVERIFY(!provider.connectionProfileConfiguration(scope, profile.id));
    const Utils::Result<> unsupportedEndpointEdit
        = provider.setConnectionProfileEndpoint(scope, profile.id, QStringLiteral("192.0.2.1"));
    QVERIFY(!unsupportedEndpointEdit);
    QCOMPARE(
        unsupportedEndpointEdit.error(),
        Tr::tr("This controller provider does not support editing connection profiles."));
    QVERIFY(!provider.supportsPackageDeployment());
    Data::ControllerPackageDeploymentRequest deploymentRequest;
    deploymentRequest.operationId = QStringLiteral("core-contract");
    deploymentRequest.artifact = QByteArray("signed-controller-package");
    deploymentRequest.configurationId = 813;
    QCOMPARE(Data::ControllerPackageDeploymentRequest(deploymentRequest), deploymentRequest);
    const Utils::Result<> unsupportedDeployment = provider.deployPackage(deploymentRequest);
    QVERIFY(!unsupportedDeployment);
    QCOMPARE(
        unsupportedDeployment.error(),
        Tr::tr("This controller provider does not support package deployment."));
    const Utils::Result<> unsupportedCancel = provider.cancelPackageDeployment(
        deploymentRequest.operationId);
    QVERIFY(!unsupportedCancel);
    QCOMPARE(
        unsupportedCancel.error(),
        Tr::tr("This controller provider does not support canceling package deployment."));

    Data::ControllerPackageDeploymentProgress deploymentProgress;
    deploymentProgress.operationId = deploymentRequest.operationId;
    deploymentProgress.artifactSha256 = QByteArray(32, '\x5a');
    deploymentProgress.state = Data::ControllerPackageDeploymentState::Uploading;
    deploymentProgress.totalBytes = deploymentRequest.artifact.size();
    deploymentProgress.transferredBytes = 7;
    deploymentProgress.candidate = Data::ControllerPackageSelector{
        Data::ControllerSlot::B, 12, deploymentRequest.configurationId};
    deploymentProgress.previousActive
        = Data::ControllerPackageSelector{Data::ControllerSlot::A, 11, 810};
    Data::ControllerPackageDeploymentAuditEvent auditEvent;
    auditEvent.sequence = 1;
    auditEvent.operation = Data::ControllerOperation::UploadPackage;
    auditEvent.requestId = 42;
    auditEvent.detail = QStringLiteral("BulkBegin queued");
    auditEvent.occurredAt = QDateTime::currentDateTimeUtc();
    deploymentProgress.audit.append(auditEvent);
    QCOMPARE(
        Data::ControllerPackageSelector(*deploymentProgress.candidate),
        *deploymentProgress.candidate);
    QVERIFY(deploymentProgress.candidate->isValid());
    QCOMPARE(
        Data::ControllerPackageDeploymentAuditEvent(deploymentProgress.audit.constFirst()),
        auditEvent);
    QCOMPARE(Data::ControllerPackageDeploymentProgress(deploymentProgress), deploymentProgress);

    TestControllerConnectionProvider alternateProvider(
        "EtherCAT.Connection.VendorIpc",
        "Vendor IPC controller",
        "ipc://controller-1",
        {"primary"},
        profile.id);
    const Data::ControllerConnectionProfile alternateProfile
        = alternateProvider.connectionProfiles(scope).constFirst();
    QCOMPARE(alternateProfile.id, profile.id);
    alternateProvider.setAvailable(true);
    QVERIFY_RESULT(alternateProvider.connectToController(request));
    QCOMPARE(alternateProvider.connectionSnapshot().channels.size(), 1);
    QCOMPARE(alternateProvider.connectionSnapshot().channels.constFirst().id, QString("primary"));
    QCOMPARE(alternateProvider.connectionSnapshot().endpointSummary, QString("ipc://controller-1"));
    QVERIFY_RESULT(alternateProvider.disconnectFromController());

    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.connectionSnapshot().readOnly);
    QVERIFY(!provider.refreshController());

    QSignalSpy profilesSpy(&provider, &ControllerConnectionProvider::connectionProfilesChanged);
    QSignalSpy snapshotSpy(&provider, &ControllerConnectionProvider::connectionSnapshotChanged);
    QVERIFY(!provider.isAvailable());
    QVERIFY(!provider.connectToController(request));
    QCOMPARE(snapshotSpy.count(), 0);
    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());
    QVERIFY(!provider.connectToController({}));
    Data::ControllerConnectionRequest invalidRequest = request;
    invalidRequest.scope.projectId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest = request;
    invalidRequest.scope.masterId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest = request;
    invalidRequest.profileId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest.profileId = Data::NodeId::create();
    QVERIFY(invalidRequest.profileId != profile.id);
    QVERIFY(!provider.connectToController(invalidRequest));
    provider.setProfileConfigured(false);
    QCOMPARE(profilesSpy.count(), 1);
    const Data::ControllerConnectionProfile incompleteProfile
        = provider.connectionProfiles(scope).constFirst();
    QVERIFY(!incompleteProfile.configured);
    QVERIFY(!incompleteProfile.configurationIssue.isEmpty());
    QVERIFY(!provider.connectToController(request));
    provider.setProfileConfigured(true);
    QCOMPARE(profilesSpy.count(), 2);
    provider.setProfileSupported(false);
    QCOMPARE(profilesSpy.count(), 3);
    QVERIFY(!provider.connectionProfiles(scope).constFirst().supported);
    QVERIFY(!provider.connectToController(request));
    provider.setProfileSupported(true);
    QCOMPARE(profilesSpy.count(), 4);
    QVERIFY(provider.connectionProfiles({}).isEmpty());
    QCOMPARE(snapshotSpy.count(), 0);

    const Utils::Result<> connectResult = provider.connectToController(request);
    QVERIFY_RESULT(connectResult);
    const Data::ControllerConnectionSnapshot connecting = provider.connectionSnapshot();
    QCOMPARE(connecting.scope, request.scope);
    QCOMPARE(connecting.profileId, request.profileId);
    QCOMPARE(connecting.endpointSummary, profile.endpointSummary);
    QCOMPARE(connecting.state, Data::ControllerConnectionState::Connecting);
    QCOMPARE(connecting.sessionGeneration, quint64(1));
    QCOMPARE(connecting.channels.size(), 3);
    QCOMPARE(connecting.channels.at(0).id, QString("control"));
    QCOMPARE(connecting.channels.at(0).displayName, QString("CONTROL"));
    QCOMPARE(connecting.channels.at(0).state, Data::ControllerChannelState::Connecting);
    QCOMPARE(connecting.channels.at(0).maximumPayloadBytes, 4096);
    QCOMPARE(connecting.channels.at(1).id, QString("events"));
    QCOMPARE(connecting.channels.at(1).maximumPayloadBytes, 65536);
    QCOMPARE(connecting.channels.at(2).id, QString("bulk"));
    QCOMPARE(connecting.channels.at(2).maximumPayloadBytes, 65536);
    QVERIFY(connecting.readOnly);
    QCOMPARE(snapshotSpy.count(), 1);
    QVERIFY(!provider.connectToController(request));

    provider.completeHandshake();
    const Data::ControllerConnectionSnapshot connected = provider.connectionSnapshot();
    QCOMPARE(connected.state, Data::ControllerConnectionState::Connected);
    QCOMPARE(connected.protocolVersion, Data::ControllerProtocolVersion(1, 9));
    QVERIFY(connected.session);
    QCOMPARE(connected.session->sessionId, quint64(17));
    QCOMPARE(connected.session->bootId, quint64(42));
    QVERIFY(!connected.session->ownsControlLease);
    QVERIFY(connected.controllerState);
    QCOMPARE(connected.controllerState->serviceState, Data::ControllerServiceState::Shutdown);
    QVERIFY(connected.controllerState->ready);
    QVERIFY(connected.capability);
    QCOMPARE(connected.capability->maximumSlaves, 64);
    QCOMPARE(connected.capability->descriptorSha256.size(), 32);
    QVERIFY(connected.capability->controlLease);
    QVERIFY(connected.capability->resumablePush);
    QVERIFY(connected.capability->transactionalBulk);
    QVERIFY(connected.capability->firmwareUpdate);
    QVERIFY(connected.capability->coe);
    QVERIFY(connected.capability->distributedClocks);
    QVERIFY(connected.package);
    QCOMPARE(connected.package->activeSlot, Data::ControllerSlot::A);
    QCOMPARE(connected.package->activeConfigurationId, quint64(810));
    QCOMPARE(connected.package->controllerState, Data::ControllerPackageState::Empty);
    QVERIFY(connected.firmware);
    QCOMPARE(connected.firmware->state, Data::ControllerFirmwareState::Confirmed);
    QCOMPARE(connected.firmware->generation, quint64(2517));
    QCOMPARE(Data::ControllerConnectionSnapshot(connected), connected);
    QCOMPARE(snapshotSpy.count(), 2);

    provider.setAvailable(false);
    QVERIFY(!provider.refreshController());
    QCOMPARE(snapshotSpy.count(), 2);
    provider.setAvailable(true);
    const Utils::Result<> refreshResult = provider.refreshController();
    QVERIFY_RESULT(refreshResult);
    QVERIFY(provider.connectionSnapshot().updatedAt.isValid());
    QCOMPARE(snapshotSpy.count(), 3);

    provider.degradePush();
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Degraded);
    QCOMPARE(provider.connectionSnapshot().channels.at(1).state, Data::ControllerChannelState::Failed);
    QCOMPARE(snapshotSpy.count(), 4);
    const Utils::Result<> degradedRefreshResult = provider.refreshController();
    QVERIFY_RESULT(degradedRefreshResult);
    QCOMPARE(snapshotSpy.count(), 5);

    provider.failProtocol();
    const Data::ControllerConnectionSnapshot failed = provider.connectionSnapshot();
    QCOMPARE(failed.state, Data::ControllerConnectionState::Failed);
    QVERIFY(failed.lastError);
    QCOMPARE(failed.lastError->source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(failed.lastError->channelId, QString("events"));
    QCOMPARE(failed.lastError->operation, Data::ControllerOperation::SubscribeEvents);
    QVERIFY(!failed.lastError->code);
    QVERIFY(!failed.lastError->operationResult);
    QCOMPARE(failed.lastError->codeName, QString("UNEXPECTED_PUSH_FRAME"));
    QVERIFY(!failed.lastError->sourceDetail);
    QVERIFY(failed.lastError->requestId);
    QCOMPARE(*failed.lastError->requestId, quint64(73));
    QCOMPARE(failed.lastError->retryDisposition, Data::ControllerRetryDisposition::Reconnect);
    QCOMPARE(snapshotSpy.count(), 6);

    Data::ControllerOperationError controllerError;
    controllerError.source = Data::ControllerErrorSource::Controller;
    controllerError.channelId = "control";
    controllerError.operation = Data::ControllerOperation::QueryState;
    controllerError.code = -6;
    controllerError.operationResult = -2;
    controllerError.sourceDetail = 17;
    controllerError.codeName = "BAD_MESSAGE";
    controllerError.retryAfterMs = 250;
    const Data::ControllerOperationError controllerErrorCopy = controllerError;
    QCOMPARE(controllerErrorCopy, controllerError);
    QCOMPARE(*controllerErrorCopy.sourceDetail, quint64(17));
    QCOMPARE(*controllerErrorCopy.retryAfterMs, 250);

    provider.setAvailable(false);
    const Utils::Result<> failedDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(failedDisconnectResult);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    for (const Data::ControllerChannelStatus &channel : provider.connectionSnapshot().channels)
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
    QCOMPARE(provider.connectionSnapshot().scope, request.scope);
    QCOMPARE(provider.connectionSnapshot().profileId, request.profileId);
    QVERIFY(!provider.connectionSnapshot().session);
    QVERIFY(!provider.connectionSnapshot().lastHeartbeatAt.isValid());
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(2));
    QCOMPARE(snapshotSpy.count(), 7);
    QVERIFY(!provider.connectToController(request));
    QCOMPARE(snapshotSpy.count(), 7);

    provider.setAvailable(true);
    const Utils::Result<> reconnectResult = provider.connectToController(request);
    QVERIFY_RESULT(reconnectResult);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(3));
    QCOMPARE(snapshotSpy.count(), 8);
    const Utils::Result<> reconnectDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(reconnectDisconnectResult);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(4));
    QCOMPARE(snapshotSpy.count(), 9);
    const Utils::Result<> repeatedDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(repeatedDisconnectResult);
    QCOMPARE(snapshotSpy.count(), 9);
}

void EtherCATCoreTests::testScanProviderContract()
{
    TestScanProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::Scan);
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
    QVERIFY(!provider.lastScanResult());

    QSignalSpy stateSpy(&provider, &ScanProvider::scanStateChanged);
    QSignalSpy progressSpy(&provider, &ScanProvider::scanProgressChanged);
    QSignalSpy finishedSpy(&provider, &ScanProvider::scanFinished);
    const Data::ScanRequest
        request{Data::NodeId::create(), Data::NodeId::create(), Data::ScanOperation::Slaves, {}};
    QVERIFY_RESULT(provider.startScan(request));
    QCOMPARE(provider.scanState(), Data::ScanState::Preparing);
    QCOMPARE(provider.scanProgress().maximum, 4);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(progressSpy.count(), 1);

    QVERIFY(!provider.startScan(request));
    provider.complete();
    QCOMPARE(provider.scanState(), Data::ScanState::Completed);
    QVERIFY(provider.lastScanResult());
    QVERIFY(provider.lastScanResult()->snapshot.mock);
    QCOMPARE(provider.lastScanResult()->snapshot.slaves.first().serialNumber, quint32(17));
    QCOMPARE(provider.lastScanResult()->comparison.differences.size(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    provider.clearScanResult();
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
    QVERIFY_RESULT(provider.startScan(request));
    provider.cancelScan();
    QCOMPARE(provider.scanState(), Data::ScanState::Cancelled);
    QCOMPARE(finishedSpy.count(), 2);
    provider.clearScanResult();
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
}

void EtherCATCoreTests::testDiagnosticsProviderContract()
{
    TestDiagnosticsProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::Diagnostics);
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(provider.limits(), Data::DiagnosticsLimits(4, 5, 10, 100));
    QVERIFY(!provider.latestSnapshot());
    QVERIFY(provider.events().isEmpty());
    QVERIFY(provider.trendSamples().isEmpty());

    QSignalSpy stateSpy(&provider, &DiagnosticsProvider::streamStateChanged);
    QSignalSpy snapshotSpy(&provider, &DiagnosticsProvider::diagnosticsSnapshotChanged);
    QSignalSpy eventSpy(&provider, &DiagnosticsProvider::diagnosticEventsChanged);
    QSignalSpy trendSpy(&provider, &DiagnosticsProvider::diagnosticTrendChanged);
    QSignalSpy stoppedSpy(&provider, &DiagnosticsProvider::monitoringStopped);
    QVERIFY(!provider.startMonitoring({}));

    const Data::DiagnosticsRequest request{Data::NodeId::create(), Data::NodeId::create()};
    const Utils::Result<> startResult = provider.startMonitoring(request);
    QVERIFY_RESULT(startResult);
    QCOMPARE(provider.activeRequest(), request);
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Running);
    QCOMPARE(stateSpy.count(), 2);
    QVERIFY(!provider.startMonitoring(request));

    provider.publish();
    QVERIFY(provider.latestSnapshot());
    const Data::DiagnosticsSnapshot snapshot = *provider.latestSnapshot();
    const Data::DiagnosticsSnapshot snapshotCopy = snapshot;
    QCOMPARE(snapshotCopy, snapshot);
    QVERIFY(snapshot.mock);
    QCOMPARE(snapshot.masterState, Data::EtherCATState::Operational);
    QCOMPARE(snapshot.workingCounter.expected, quint32(4));
    QCOMPARE(snapshot.workingCounter.actual, quint32(3));
    QCOMPARE(snapshot.frameErrors.overflows, quint64(6));
    QCOMPARE(snapshot.distributedClock.offsetNs, qint64(17));
    QCOMPARE(snapshot.cycle.nominalCycleNs, qint64(125000));
    QCOMPARE(snapshot.slaves.size(), 1);
    QCOMPARE(provider.events().size(), 1);
    QCOMPARE(provider.events().first().repeatCount, quint32(2));
    QCOMPARE(provider.trendSamples().size(), 1);
    QCOMPARE(snapshotSpy.count(), 1);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(trendSpy.count(), 1);

    const Utils::Result<> modeResult = provider.requestRunMode(Data::DiagnosticsRunMode::Config);
    QVERIFY_RESULT(modeResult);
    QCOMPARE(provider.latestSnapshot()->runMode, Data::DiagnosticsRunMode::Config);
    QCOMPARE(snapshotSpy.count(), 2);

    const Data::NodeId alarmId = provider.events().first().id;
    const Utils::Result<> acknowledgeResult = provider.acknowledgeAlarm(alarmId);
    QVERIFY_RESULT(acknowledgeResult);
    QCOMPARE(provider.events().first().lifecycle, Data::AlarmLifecycle::Acknowledged);
    QVERIFY(!provider.acknowledgeAlarm(alarmId));
    provider.recoverAlarm();
    const Utils::Result<> clearResult = provider.clearRecoveredEvents();
    QVERIFY_RESULT(clearResult);
    QVERIFY(provider.events().isEmpty());
    QCOMPARE(eventSpy.count(), 4);

    provider.stopMonitoring();
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(provider.activeRequest(), Data::DiagnosticsRequest());
    QCOMPARE(stateSpy.count(), 4);
    QCOMPARE(stoppedSpy.count(), 1);
    provider.stopMonitoring();
    QCOMPARE(stoppedSpy.count(), 1);
    QVERIFY(!provider.requestRunMode(Data::DiagnosticsRunMode::Run));

    const Utils::Result<> restartResult = provider.startMonitoring(request);
    QVERIFY_RESULT(restartResult);
    provider.fail("Mock diagnostics source failed");
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Failed);
    QCOMPARE(provider.lastDiagnosticsError(), QString("Mock diagnostics source failed"));
    QCOMPARE(stoppedSpy.count(), 2);
    provider.stopMonitoring();
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(stoppedSpy.count(), 2);
}

void EtherCATCoreTests::testSelectionServicePublishesStableIds()
{
    SelectionService *service = ExtensionSystem::PluginManager::getObject<SelectionService>();
    QVERIFY(service);
    service->clear();

    QSignalSpy changedSpy(service, &SelectionService::currentNodeChanged);
    const Data::NodeId selectedId = Data::NodeId::create();
    service->setCurrentNodeId(selectedId);
    QCOMPARE(service->currentNodeId(), selectedId);
    QCOMPARE(changedSpy.count(), 1);

    service->setCurrentNodeId(selectedId);
    QCOMPARE(changedSpy.count(), 1);

    service->clear();
    QVERIFY(service->currentNodeId().isNull());
    QCOMPARE(changedSpy.count(), 2);
}

void EtherCATCoreTests::testStateServiceAggregatesContributions()
{
    StateService *service = ExtensionSystem::PluginManager::getObject<StateService>();
    QVERIFY(service);
    service->clearAll();

    QSignalSpy aggregateSpy(service, &StateService::aggregateSeverityChanged);
    QVERIFY(!service->setStatus({}));
    QVERIFY(service->setStatus({"EtherCAT.Project", StatusSeverity::Busy, "Opening", {}}));
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Busy);
    QVERIFY(service->setStatus({"EtherCAT.Scan", StatusSeverity::Error, "Failed", "Mock"}));
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Error);
    QCOMPARE(service->statuses().size(), 2);
    QCOMPARE(service->statuses().at(0).sourceId, Utils::Id("EtherCAT.Project"));
    QCOMPARE(aggregateSpy.count(), 2);

    service->clearStatus("EtherCAT.Scan");
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Busy);
    service->clearAll();
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Ready);
    QVERIFY(service->statuses().isEmpty());
}

void EtherCATCoreTests::testProviderRegistryTracksObjectPool()
{
    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);

    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    TestScanProvider provider;
    TestPropertyPageProvider reentrantProvider;
    TestControllerConnectionProvider connectionProvider;
    TestControllerConnectionProvider alternateConnectionProvider(
        "EtherCAT.Connection.VendorIpc", "Vendor IPC controller", "ipc://controller-1", {"primary"});

    ExtensionSystem::PluginManager::addObject(&provider);
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::Scan), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());

    ExtensionSystem::PluginManager::addObject(&reentrantProvider);
    QCOMPARE(registry->provider(reentrantProvider.id()), &reentrantProvider);
    QCOMPARE(addedSpy.count(), 2);

    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    QCOMPARE(registry->provider(connectionProvider.id()), &connectionProvider);
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&connectionProvider}));
    QCOMPARE(addedSpy.count(), 3);

    ExtensionSystem::PluginManager::addObject(&alternateConnectionProvider);
    QCOMPARE(registry->provider(alternateConnectionProvider.id()), &alternateConnectionProvider);
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&connectionProvider, &alternateConnectionProvider}));
    QCOMPARE(addedSpy.count(), 4);

    bool departingProviderUnlinked = false;
    bool nestedRemovalObserved = false;
    connect(
        registry,
        &ProviderRegistry::providerAboutToBeRemoved,
        &reentrantProvider,
        [&](Provider *departingProvider) {
            if (departingProvider != &reentrantProvider)
                return;
            departingProviderUnlinked = !registry->provider(reentrantProvider.id());
            nestedRemovalObserved = true;
            ExtensionSystem::PluginManager::removeObject(&provider);
        },
        Qt::DirectConnection);
    ExtensionSystem::PluginManager::removeObject(&reentrantProvider);
    QVERIFY(departingProviderUnlinked);
    QVERIFY(nestedRemovalObserved);
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(!registry->provider(reentrantProvider.id()));
    QVERIFY(registry->providers(ProviderKind::Scan).isEmpty());
    QVERIFY(!registry->providers(ProviderKind::PropertyPage).contains(&reentrantProvider));
    QCOMPARE(removedSpy.count(), 2);

    ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    QVERIFY(!registry->provider(connectionProvider.id()));
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&alternateConnectionProvider}));
    QCOMPARE(removedSpy.count(), 3);

    ExtensionSystem::PluginManager::removeObject(&alternateConnectionProvider);
    QVERIFY(!registry->provider(alternateConnectionProvider.id()));
    QVERIFY(registry->providers(ProviderKind::ControllerConnection).isEmpty());
    QCOMPARE(removedSpy.count(), 4);
}

void EtherCATCoreTests::testSettingsPageIsRegistered()
{
    const QList<::Core::IOptionsPage *> pages = ::Core::IOptionsPage::allOptionsPages();
    const auto found = std::find_if(pages.cbegin(), pages.cend(), [](const auto *page) {
        return page->id() == Constants::SETTINGS_GENERAL;
    });
    QVERIFY(found != pages.cend());
    QCOMPARE((*found)->category(), Utils::Id(Constants::SETTINGS_CATEGORY));
    QCOMPARE((*found)->displayCategory(), QString("EtherCAT"));
    QVERIFY((*found)->aspects().has_value());

    std::unique_ptr<::Core::IOptionsPageWidget> widget((*found)->createWidget());
    QVERIFY(widget);
    QVERIFY(!settings().showAdvancedProperties());
    QCOMPARE(settings().maximumRecentEvents(), 1000);
}

} // namespace EtherCAT::Core::Internal
