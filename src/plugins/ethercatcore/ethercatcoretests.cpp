// Copyright (C) 2026 Kvell

#include "ethercatcoretests.h"

#include "ethercatcoreconstants.h"
#include "ethercatcoresettings.h"
#include "providerregistry.h"
#include "providers.h"
#include "selectionservice.h"
#include "stateservice.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <ethercatdata/nodeid.h>
#include <ethercatdata/offlineconfiguration.h>
#include <ethercatdata/projectsnapshot.h>

#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <algorithm>

namespace EtherCAT::Core::Internal {

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

    ExtensionSystem::PluginManager::addObject(&provider);
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::Scan), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());

    ExtensionSystem::PluginManager::removeObject(&provider);
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(registry->providers(ProviderKind::Scan).isEmpty());
    QCOMPARE(removedSpy.count(), 1);
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
