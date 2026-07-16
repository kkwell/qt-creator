// Copyright (C) 2026 Kvell

#include "ethercatdiagnosticstests.h"

#include "diagnosticspropertypages.h"
#include "ethercatdiagnosticsconstants.h"
#include "mockdiagnosticsprovider.h"

#include <coreplugin/actionmanager/actionmanager.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <ethercatworkbench/ethercatworkbenchconstants.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <utils/filepath.h>

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWidget>

#include <algorithm>

namespace EtherCAT::Diagnostics::Internal {

struct OpenedTestProject
{
    Data::NodeId projectId;
    Data::NodeId targetId;
    Data::NodeId masterId;
    QList<Data::NodeId> slaveIds;
    ProjectExplorer::Project *project = nullptr;
};

static QByteArray projectDocument(const OpenedTestProject &fixture)
{
    QJsonObject project;
    project.insert("id", fixture.projectId.toString());
    project.insert("name", "Mock Diagnostics Project");
    project.insert("createdBy", "EtherCATDiagnosticsTests");
    QJsonObject target;
    target.insert("id", fixture.targetId.toString());
    target.insert("name", "Offline Controller");
    QJsonObject master;
    master.insert("id", fixture.masterId.toString());
    master.insert("name", "EtherCAT Master");
    QJsonArray slaves;
    for (int index = 0; index < fixture.slaveIds.size(); ++index) {
        QJsonObject slave;
        slave.insert("id", fixture.slaveIds.at(index).toString());
        slave.insert("name", QString("Configured Slave %1").arg(index + 1));
        slave.insert("position", index);
        slave.insert("vendorId", 2);
        slave.insert("productCode", 0x1000 + index);
        slave.insert("revisionNumber", 1);
        slave.insert("serialNumber", 101 + index);
        slave.insert("alias", 0);
        slaves.append(slave);
    }
    master.insert("slaves", slaves);
    QJsonObject root;
    root.insert("format", "ethercat-project");
    root.insert("formatVersion", 1);
    root.insert("project", project);
    root.insert("target", target);
    root.insert("master", master);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

static OpenedTestProject openProject(QTemporaryDir &directory, const QString &fileName)
{
    OpenedTestProject fixture{Data::NodeId::create(),
                              Data::NodeId::create(),
                              Data::NodeId::create(),
                              {Data::NodeId::create(), Data::NodeId::create()},
                              nullptr};
    const Utils::FilePath path = Utils::FilePath::fromString(directory.path())
                                     .canonicalPath()
                                     .pathAppended(fileName);
    if (!path.writeFileContents(projectDocument(fixture)))
        return {};
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(path, false);
    if (!opened)
        return {};
    fixture.project = opened.project();
    ProjectExplorer::ProjectManager::setStartupProject(fixture.project);
    return fixture;
}

static void closeProject(OpenedTestProject &fixture)
{
    if (!fixture.project)
        return;
    ProjectExplorer::ProjectManager::removeProject(fixture.project);
    fixture.project = nullptr;
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

static Data::DiagnosticEvent firstLifecycleEvent(
    const QList<Data::DiagnosticEvent> &events, Data::AlarmLifecycle lifecycle)
{
    const auto event = std::find_if(
        events.cbegin(), events.cend(), [lifecycle](const Data::DiagnosticEvent &candidate) {
            return candidate.lifecycle == lifecycle;
        });
    return event == events.cend() ? Data::DiagnosticEvent() : *event;
}

void EtherCATDiagnosticsTests::testMetadataProvidersActionsAndPages()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        Constants::PLUGIN_ID);
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATDiagnostics"));
    QVERIFY(!spec->hasError());
    const QList<ExtensionSystem::PluginDependency> dependencies = spec->dependencies();
    const auto hasDependency = [&dependencies](const QString &id) {
        return std::any_of(
            dependencies.cbegin(), dependencies.cend(), [&id](const auto &dependency) {
                return dependency.id == id
                       && dependency.type == ExtensionSystem::PluginDependency::Required;
            });
    };
    QVERIFY(hasDependency("core"));
    QVERIFY(hasDependency("ethercatcore"));
    QVERIFY(hasDependency("ethercatproject"));
    QVERIFY(hasDependency("ethercatworkbench"));

    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    auto *provider = ExtensionSystem::PluginManager::getObject<MockDiagnosticsProvider>();
    auto *pages = ExtensionSystem::PluginManager::getObject<DiagnosticsPropertyPageProvider>();
    QVERIFY(registry);
    QVERIFY(provider);
    QVERIFY(pages);
    QCOMPARE(registry->provider(Constants::PROVIDER_ID), provider);
    QCOMPARE(provider->kind(), Core::ProviderKind::Diagnostics);
    QVERIFY(provider->isAvailable());
    QCOMPARE(registry->provider(Constants::PAGE_PROVIDER_ID), pages);
    QCOMPARE(pages->kind(), Core::ProviderKind::PropertyPage);

    for (Utils::Id actionId : {Utils::Id(Constants::START_ACTION_ID),
                               Utils::Id(Constants::STOP_ACTION_ID),
                               Utils::Id(Constants::CONFIG_ACTION_ID),
                               Utils::Id(Constants::FREE_RUN_ACTION_ID),
                               Utils::Id(Constants::RUN_ACTION_ID),
                               Utils::Id(Constants::ACKNOWLEDGE_ACTION_ID),
                               Utils::Id(Constants::CLEAR_RECOVERED_ACTION_ID)}) {
        QVERIFY(::Core::ActionManager::command(actionId));
    }

    const Core::PropertyPageContext diagnosticsContext{
        Data::NodeId::create(),
        Data::NodeId::create(),
        Core::WorkbenchNodeKind::Diagnostics,
        "Diagnostics"};
    QCOMPARE(pages->pages(diagnosticsContext).size(), 8);
    const Core::PropertyPageContext masterContext{
        diagnosticsContext.projectId,
        Data::NodeId::create(),
        Core::WorkbenchNodeKind::Master,
        "Master"};
    QCOMPARE(pages->pages(masterContext).size(), 1);
    const Core::PropertyPageContext slaveContext{
        diagnosticsContext.projectId,
        Data::NodeId::create(),
        Core::WorkbenchNodeKind::ConfiguredSlave,
        "Slave"};
    QCOMPARE(pages->pages(slaveContext).size(), 1);

    QWidget parent;
    QWidget *page = pages->createPage(Constants::MASTER_PAGE_ID, &parent);
    QVERIFY(page);
    pages->updatePage(Constants::MASTER_PAGE_ID, page, masterContext);
    QLabel *banner = page->findChild<QLabel *>("EtherCATMockDiagnosticsBanner");
    QVERIFY(banner);
    QVERIFY(banner->text().contains("MOCK DIAGNOSTICS"));
    QVERIFY(page->findChild<QComboBox *>("EtherCATMockDiagnosticsScenario"));
    QTreeWidget *table = page->findChild<QTreeWidget *>("EtherCATMockDiagnosticsTable");
    QVERIFY(table);
    QVERIFY(table->topLevelItemCount() > 0);
}

void EtherCATDiagnosticsTests::testMockStreamModesAndFields()
{
    auto *provider = ExtensionSystem::PluginManager::getObject<MockDiagnosticsProvider>();
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    QVERIFY(provider);
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OpenedTestProject fixture = openProject(directory, "stream.ecatproject");
    QVERIFY(fixture.project);
    provider->stopMonitoring();
    provider->setScenario(MockDiagnosticsScenario::Normal);
    QSignalSpy states(provider, &Core::DiagnosticsProvider::streamStateChanged);
    const Utils::Result<> startResult
        = provider->startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(startResult);
    QVERIFY(provider->latestSnapshot());
    QVERIFY(provider->latestSnapshot()->mock);
    QCOMPARE(provider->latestSnapshot()->masterState, Data::EtherCATState::Init);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Running, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(provider->latestSnapshot()->generation > 1, 5000);
    const Data::DiagnosticsSnapshot config = *provider->latestSnapshot();
    QCOMPARE(config.runMode, Data::DiagnosticsRunMode::Config);
    QCOMPARE(config.masterState, Data::EtherCATState::PreOperational);
    QCOMPARE(config.workingCounter.expected, quint32(4));
    QCOMPARE(config.workingCounter.actual, quint32(4));
    QCOMPARE(config.slaves.size(), 2);
    QCOMPARE(config.masterPorts.size(), 2);
    QVERIFY(config.cycle.sampleCount > 0);
    QVERIFY(config.distributedClock.state == Data::DcSyncState::Synchronized);
    QVERIFY(config.sourceSampleCount > config.coalescedSampleCount);
    QVERIFY(!provider->trendSamples().isEmpty());

    const Utils::Result<> freeRun
        = provider->requestRunMode(Data::DiagnosticsRunMode::FreeRun);
    QVERIFY_RESULT(freeRun);
    QCOMPARE(provider->latestSnapshot()->masterState,
             Data::EtherCATState::SafeOperational);
    const Utils::Result<> run = provider->requestRunMode(Data::DiagnosticsRunMode::Run);
    QVERIFY_RESULT(run);
    QCOMPARE(provider->latestSnapshot()->masterState, Data::EtherCATState::Operational);

    provider->setScenario(MockDiagnosticsScenario::WorkingCounterMismatch);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider->latestSnapshot()->workingCounter.actual
            < provider->latestSnapshot()->workingCounter.expected,
        5000);
    QCOMPARE(provider->latestSnapshot()->workingCounter.state,
             Data::WorkingCounterState::Incomplete);
    QVERIFY(provider->latestSnapshot()->masterHasError);
    QVERIFY(provider->latestSnapshot()->slaves.first().hasError);
    QTRY_VERIFY_WITH_TIMEOUT(
        !firstLifecycleEvent(provider->events(), Data::AlarmLifecycle::Active).id.isNull(),
        5000);

    provider->stopMonitoring();
    QCOMPARE(provider->streamState(), Data::DiagnosticsStreamState::Stopped);
    QVERIFY(provider->activeRequest().projectId.isNull());
    QVERIFY(!provider->samplerRunning());
    const quint64 stoppedGeneration = provider->latestSnapshot()->generation;
    QTest::qWait(provider->limits().publishPeriodMs * 2);
    QCOMPARE(provider->latestSnapshot()->generation, stoppedGeneration);
    QVERIFY(states.count() >= 4);
    closeProject(fixture);
    QVERIFY(service->projects().isEmpty());
}

void EtherCATDiagnosticsTests::testBoundedAggregationAndAlarmLifecycle()
{
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OpenedTestProject fixture = openProject(directory, "bounded.ecatproject");
    QVERIFY(fixture.project);

    MockDiagnosticsProvider provider({3, 4, 1, 5});
    provider.setScenario(MockDiagnosticsScenario::AlarmBurst);
    const Utils::Result<> burstStart
        = provider.startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(burstStart);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.streamState(), Data::DiagnosticsStreamState::Running, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.latestSnapshot() && provider.latestSnapshot()->droppedEventCount > 0
            && provider.latestSnapshot()->droppedTrendSampleCount > 0,
        5000);
    QCOMPARE(provider.events().size(), 3);
    QCOMPARE(provider.trendSamples().size(), 4);
    QVERIFY(provider.latestSnapshot()->coalescedSampleCount > 0);
    QVERIFY(provider.latestSnapshot()->sourceSampleCount
            > provider.latestSnapshot()->generation);
    provider.stopMonitoring();

    provider.setScenario(MockDiagnosticsScenario::WorkingCounterMismatch);
    const Utils::Result<> alarmStart
        = provider.startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(alarmStart);
    QTRY_VERIFY_WITH_TIMEOUT(
        !firstLifecycleEvent(provider.events(), Data::AlarmLifecycle::Active).id.isNull(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        firstLifecycleEvent(provider.events(), Data::AlarmLifecycle::Active).repeatCount > 1,
        5000);
    const Data::NodeId alarmId
        = firstLifecycleEvent(provider.events(), Data::AlarmLifecycle::Active).id;
    const Utils::Result<> acknowledge = provider.acknowledgeAlarm(alarmId);
    QVERIFY_RESULT(acknowledge);
    QCOMPARE(firstLifecycleEvent(provider.events(), Data::AlarmLifecycle::Acknowledged).id,
             alarmId);
    provider.setScenario(MockDiagnosticsScenario::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(
        firstLifecycleEvent(provider.events(), Data::AlarmLifecycle::Recovered).id,
        alarmId,
        5000);
    QVERIFY(provider.latestSnapshot()->activeAlarmCount == 0);
    const Utils::Result<> clear = provider.clearRecoveredEvents();
    QVERIFY_RESULT(clear);
    const QList<Data::DiagnosticEvent> remainingEvents = provider.events();
    QVERIFY(std::none_of(
        remainingEvents.cbegin(), remainingEvents.cend(), [](const auto &event) {
            return event.lifecycle == Data::AlarmLifecycle::Recovered;
        }));
    provider.stopMonitoring();
    closeProject(fixture);
}

void EtherCATDiagnosticsTests::testFailureConsumerAndProjectCleanup()
{
    auto *provider = ExtensionSystem::PluginManager::getObject<MockDiagnosticsProvider>();
    auto *pages = ExtensionSystem::PluginManager::getObject<DiagnosticsPropertyPageProvider>();
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    QVERIFY(provider);
    QVERIFY(pages);
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OpenedTestProject fixture = openProject(directory, "cleanup.ecatproject");
    QVERIFY(fixture.project);

    provider->stopMonitoring();
    provider->setScenario(MockDiagnosticsScenario::SourceFailure);
    QSignalSpy stopped(provider, &Core::DiagnosticsProvider::monitoringStopped);
    const Utils::Result<> failureStart
        = provider->startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(failureStart);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Failed, 5000);
    QVERIFY(provider->lastDiagnosticsError().contains("MOCK diagnostics source failure"));
    QVERIFY(!provider->samplerRunning());
    QCOMPARE(stopped.count(), 1);
    provider->stopMonitoring();
    QCOMPARE(provider->streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(stopped.count(), 1);

    provider->setScenario(MockDiagnosticsScenario::Normal);
    QWidget parent;
    const Core::PropertyPageContext masterContext{
        fixture.projectId, fixture.masterId, Core::WorkbenchNodeKind::Master, "Master"};
    QWidget *page = pages->createPage(Constants::MASTER_PAGE_ID, &parent);
    QVERIFY(page);
    pages->updatePage(Constants::MASTER_PAGE_ID, page, masterContext);
    QCOMPARE(provider->consumerCount(), 1);
    const Utils::Result<> consumerStart
        = provider->startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(consumerStart);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Running, 5000);
    delete page;
    QCOMPARE(provider->consumerCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Stopped, 5000);
    QVERIFY(!provider->samplerRunning());

    const Utils::Result<> changeStart
        = provider->startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(changeStart);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Running, 5000);
    const Utils::Result<> replace
        = service->replaceOfflineSlaves(fixture.projectId, fixture.masterId, {});
    QVERIFY_RESULT(replace);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Stopped, 5000);
    QVERIFY(!provider->samplerRunning());

    const Utils::Result<> removalStart
        = provider->startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(removalStart);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Running, 5000);
    closeProject(fixture);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Stopped, 5000);
    QVERIFY(!provider->samplerRunning());
    QVERIFY(service->projects().isEmpty());
}

void EtherCATDiagnosticsTests::testLivePageUpdatesAndShutdown()
{
    auto *provider = ExtensionSystem::PluginManager::getObject<MockDiagnosticsProvider>();
    auto *pages = ExtensionSystem::PluginManager::getObject<DiagnosticsPropertyPageProvider>();
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    QVERIFY(provider);
    QVERIFY(pages);
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OpenedTestProject fixture = openProject(directory, "pages.ecatproject");
    QVERIFY(fixture.project);

    QWidget parent;
    const Core::PropertyPageContext diagnosticsContext{
        fixture.projectId,
        Data::NodeId::create(),
        Core::WorkbenchNodeKind::Diagnostics,
        "Diagnostics"};
    QWidget *eventsPage = pages->createPage(Constants::EVENTS_PAGE_ID, &parent);
    QWidget *performancePage = pages->createPage(Constants::PERFORMANCE_PAGE_ID, &parent);
    QWidget *slavesPage = pages->createPage(Constants::SLAVES_PAGE_ID, &parent);
    QVERIFY(eventsPage);
    QVERIFY(performancePage);
    QVERIFY(slavesPage);
    pages->updatePage(Constants::EVENTS_PAGE_ID, eventsPage, diagnosticsContext);
    pages->updatePage(Constants::PERFORMANCE_PAGE_ID, performancePage, diagnosticsContext);
    pages->updatePage(Constants::SLAVES_PAGE_ID, slavesPage, diagnosticsContext);
    QCOMPARE(provider->consumerCount(), 3);

    provider->stopMonitoring();
    provider->setScenario(MockDiagnosticsScenario::DeadlinePressure);
    QAction *startAction = ::Core::ActionManager::command(Constants::START_ACTION_ID)
                               ->actionForContext(Workbench::Constants::CONTEXT_ID);
    QVERIFY(startAction);
    QVERIFY(startAction->isEnabled());
    startAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->streamState(), Data::DiagnosticsStreamState::Running, 5000);
    QAction *runAction = ::Core::ActionManager::command(Constants::RUN_ACTION_ID)
                             ->actionForContext(Workbench::Constants::CONTEXT_ID);
    QVERIFY(runAction);
    QVERIFY(runAction->isEnabled());
    runAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(
        provider->latestSnapshot()->runMode, Data::DiagnosticsRunMode::Run, 5000);
    QTreeWidget *events = eventsPage->findChild<QTreeWidget *>(
        "EtherCATMockDiagnosticsTable");
    QTreeWidget *performance = performancePage->findChild<QTreeWidget *>(
        "EtherCATMockDiagnosticsTable");
    QTreeWidget *slaves = slavesPage->findChild<QTreeWidget *>(
        "EtherCATMockDiagnosticsTable");
    QVERIFY(events);
    QVERIFY(performance);
    QVERIFY(slaves);
    QVERIFY(performancePage->findChild<QWidget *>("EtherCATMockDiagnosticsTrend"));
    QTRY_VERIFY_WITH_TIMEOUT(events->topLevelItemCount() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(performance->topLevelItemCount() >= 9, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(slaves->topLevelItemCount(), 2, 5000);
    const quint64 initialGeneration = provider->latestSnapshot()->generation;
    QTest::qWait(provider->limits().publishPeriodMs * 5);
    QVERIFY(provider->latestSnapshot()->generation > initialGeneration);
    QVERIFY(provider->trendSamples().size() <= provider->limits().trendCapacity);
    QVERIFY(provider->events().size() <= provider->limits().eventCapacity);
    QAction *stopAction = ::Core::ActionManager::command(Constants::STOP_ACTION_ID)
                              ->actionForContext(Workbench::Constants::CONTEXT_ID);
    QVERIFY(stopAction);
    QVERIFY(stopAction->isEnabled());
    stopAction->trigger();
    QCOMPARE(provider->streamState(), Data::DiagnosticsStreamState::Stopped);

    MockDiagnosticsProvider shutdownProvider({4, 4, 1, 5});
    shutdownProvider.setScenario(MockDiagnosticsScenario::Normal);
    const Utils::Result<> shutdownStart
        = shutdownProvider.startMonitoring({fixture.projectId, fixture.masterId});
    QVERIFY_RESULT(shutdownStart);
    QTRY_COMPARE_WITH_TIMEOUT(
        shutdownProvider.streamState(), Data::DiagnosticsStreamState::Running, 5000);
    shutdownProvider.shutdown();
    QVERIFY(!shutdownProvider.isAvailable());
    QVERIFY(!shutdownProvider.samplerRunning());
    QCOMPARE(shutdownProvider.streamState(), Data::DiagnosticsStreamState::Stopped);

    delete eventsPage;
    delete performancePage;
    delete slavesPage;
    QCOMPARE(provider->consumerCount(), 0);
    closeProject(fixture);
    QVERIFY(service->projects().isEmpty());
}

} // namespace EtherCAT::Diagnostics::Internal
