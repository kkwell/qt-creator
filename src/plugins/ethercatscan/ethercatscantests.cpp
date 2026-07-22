// Copyright (C) 2026 Kvell

#include "ethercatscantests.h"

#include "ethercatscanconstants.h"
#include "mockscanprovider.h"
#include "scanpropertypages.h"
#include "scanworkflow.h"
#include "topologycomparison.h"

#include <coreplugin/actionmanager/actionmanager.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>
#include <ethercatcore/stateservice.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <utils/filepath.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWidget>

#include <algorithm>

namespace EtherCAT::Scan::Internal {

static Data::ProjectSnapshot projectSnapshot(
    const Data::NodeId &projectId,
    const Data::NodeId &masterId,
    const QList<Data::OfflineSlaveConfiguration> &slaves = {})
{
    const Data::NodeId targetId = Data::NodeId::create();
    Data::ProjectSnapshot project{projectId,
                                  "Scan Test",
                                  1,
                                  "EtherCATScanTests",
                                  {{projectId,
                                    {},
                                    Data::ProjectNodeKind::Project,
                                    "Scan Test"},
                                   {targetId,
                                    projectId,
                                    Data::ProjectNodeKind::Target,
                                    "Offline Controller"},
                                   {masterId,
                                    targetId,
                                    Data::ProjectNodeKind::Master,
                                    "EtherCAT Master"}},
                                  false,
                                  true,
                                  false,
                                  {},
                                  slaves};
    for (const Data::OfflineSlaveConfiguration &slave : slaves) {
        project.nodes.append(
            {slave.id, masterId, Data::ProjectNodeKind::Slave, slave.name});
    }
    return project;
}

static Data::OfflineSlaveConfiguration offlineSlave(
    const Data::NodeId &masterId,
    int position,
    quint32 productCode,
    quint32 revisionNumber,
    quint32 serialNumber)
{
    return {Data::NodeId::create(),
            masterId,
            position,
            {2, productCode, revisionNumber},
            serialNumber,
            0,
            QString("Offline Slave %1").arg(position),
            {},
            {},
            {},
            {}};
}

static Data::ScannedSlave scannedSlave(
    int position,
    quint32 productCode,
    quint32 revisionNumber,
    quint32 serialNumber)
{
    return {Data::NodeId::create(),
            position,
            {2, productCode, revisionNumber},
            serialNumber,
            0,
            QString("Scanned Slave %1").arg(position),
            {}};
}

static Data::ScanSnapshot scanSnapshot(
    const Data::NodeId &projectId,
    const Data::NodeId &masterId,
    const QList<Data::ScannedSlave> &slaves)
{
    return {Data::NodeId::create(),
            projectId,
            masterId,
            QDateTime::currentDateTimeUtc(),
            {},
            slaves,
            {},
            true,
            true,
            Data::ScanOperation::Slaves,
            {}};
}

static int differenceCount(
    const Data::TopologyComparison &comparison, Data::TopologyDifferenceKind kind)
{
    return int(std::count_if(
        comparison.differences.cbegin(),
        comparison.differences.cend(),
        [kind](const Data::TopologyDifference &difference) {
            return difference.kind == kind;
        }));
}

static QByteArray projectDocument(
    const Data::NodeId &projectId,
    const Data::NodeId &targetId,
    const Data::NodeId &masterId)
{
    QJsonObject project;
    project.insert("id", projectId.toString());
    project.insert("name", "Mock Scan Project");
    project.insert("createdBy", "EtherCATScanTests");
    QJsonObject target;
    target.insert("id", targetId.toString());
    target.insert("name", "Offline Controller");
    QJsonObject master;
    master.insert("id", masterId.toString());
    master.insert("name", "EtherCAT Master");
    master.insert("slaves", QJsonArray());
    QJsonObject root;
    root.insert("format", "ethercat-project");
    root.insert("formatVersion", 1);
    root.insert("project", project);
    root.insert("target", target);
    root.insert("master", master);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void EtherCATScanTests::testMetadataProvidersActionsAndPage()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        Constants::PLUGIN_ID);
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATScan"));
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
    QVERIFY(hasDependency("ethercatdevices"));
    QVERIFY(hasDependency("ethercatproject"));
    QVERIFY(hasDependency("ethercatworkbench"));

    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    auto *provider = ExtensionSystem::PluginManager::getObject<MockScanProvider>();
    auto *pages = ExtensionSystem::PluginManager::getObject<ScanPropertyPageProvider>();
    QVERIFY(registry);
    QVERIFY(provider);
    QVERIFY(pages);
    QCOMPARE(registry->provider(Constants::PROVIDER_ID), provider);
    QCOMPARE(provider->kind(), Core::ProviderKind::Scan);
    QVERIFY(provider->isAvailable());
    QCOMPARE(registry->provider(Constants::PAGE_PROVIDER_ID), pages);
    QCOMPARE(pages->kind(), Core::ProviderKind::PropertyPage);

    const QList<Utils::Id> actionIds = {Constants::SCAN_INTERFACES_ACTION_ID,
                                        Constants::SCAN_SLAVES_ACTION_ID,
                                        Constants::RESCAN_BRANCH_ACTION_ID,
                                        Constants::COMPARE_ACTION_ID,
                                        Constants::ACCEPT_ACTION_ID,
                                        Constants::KEEP_ACTION_ID,
                                        Constants::CANCEL_ACTION_ID};
    for (Utils::Id actionId : actionIds)
        QVERIFY(::Core::ActionManager::command(actionId));

    const Data::NodeId masterId = Data::NodeId::create();
    const Core::PropertyPageContext context{
        Data::NodeId::create(), masterId, Core::WorkbenchNodeKind::Master, "Master"};
    const QList<Core::PropertyPageDescriptor> descriptors = pages->pages(context);
    QCOMPARE(descriptors.size(), 1);
    QCOMPARE(descriptors.first().id, Utils::Id(Constants::PAGE_ID));
    QWidget parent;
    QWidget *page = pages->createPage(descriptors.first().id, &parent);
    QVERIFY(page);
    pages->updatePage(descriptors.first().id, page, context);
    QLabel *banner = page->findChild<QLabel *>("EtherCATMockScanBanner");
    QVERIFY(banner);
    QVERIFY(banner->text().contains("MOCK SCAN"));
    QVERIFY(page->findChild<QComboBox *>("EtherCATMockScanScenario"));
    QVERIFY(page->findChild<QProgressBar *>("EtherCATMockScanProgress"));
    QVERIFY(page->findChild<QTreeWidget *>("EtherCATMockScanDifferences"));
}

void EtherCATScanTests::testTopologyComparison()
{
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    QList<Data::OfflineSlaveConfiguration> offline{
        offlineSlave(masterId, 0, 0x1000, 1, 101),
        offlineSlave(masterId, 1, 0x2000, 1, 102)};
    offline[0].processData.syncManagers = {
        {Data::NodeId::create(),
         2,
         "Outputs",
         Data::SyncManagerDirection::MasterToSlave,
         true,
         1}};
    Data::PdoConfiguration pdo;
    pdo.id = Data::NodeId::create();
    pdo.index = 0x1600;
    pdo.name = "Outputs";
    pdo.syncManager = 2;
    pdo.selected = true;
    pdo.entries = {
        {Data::NodeId::create(),
         0x7000,
         1,
         "Enable",
         1,
         Data::EtherCATDataType::Boolean,
         "BOOL"}};
    offline[0].processData.pdos = {pdo};
    offline[0].startup.parameters = {
        {Data::NodeId::create(),
         true,
         0,
         "PS",
         0x8000,
         1,
         Data::EtherCATDataType::UnsignedInteger8,
         "USINT",
         QByteArray::fromHex("01"),
         "Enable"}};
    offline[0].dc = {true, "DC-Synchronous", 0x0300, {true, 125000, 0}, {}, true};
    const Data::ProjectSnapshot project = projectSnapshot(projectId, masterId, offline);

    const Data::ScanSnapshot exactSnapshot = scanSnapshot(
        projectId,
        masterId,
        {scannedSlave(0, 0x1000, 1, 101),
         scannedSlave(1, 0x2000, 1, 102)});
    Data::TopologyComparison exact = compareTopology(project, masterId, exactSnapshot);
    QVERIFY(exact.exactMatch);
    QVERIFY(exact.acceptAllowed);
    QCOMPARE(differenceCount(exact, Data::TopologyDifferenceKind::PdoConfiguration), 1);
    QCOMPARE(differenceCount(exact, Data::TopologyDifferenceKind::DcConfiguration), 1);
    const QList<Data::OfflineSlaveConfiguration> accepted
        = offlineConfigurationFromScan(project, masterId, exactSnapshot);
    QCOMPARE(accepted.first().processData, offline.first().processData);
    QCOMPARE(accepted.first().startup, offline.first().startup);
    QCOMPARE(accepted.first().dc, offline.first().dc);

    Data::TopologyComparison reordered = compareTopology(
        project,
        masterId,
        scanSnapshot(projectId,
                     masterId,
                     {scannedSlave(0, 0x2000, 1, 102),
                      scannedSlave(1, 0x1000, 1, 101)}));
    QCOMPARE(differenceCount(reordered, Data::TopologyDifferenceKind::PositionChanged), 2);
    QVERIFY(!reordered.exactMatch);
    QVERIFY(reordered.acceptAllowed);

    Data::TopologyComparison changed = compareTopology(
        project,
        masterId,
        scanSnapshot(projectId,
                     masterId,
                     {scannedSlave(0, 0x1000, 1, 101),
                      scannedSlave(2, 0x3000, 1, 103)}));
    QCOMPARE(differenceCount(changed, Data::TopologyDifferenceKind::Missing), 1);
    QCOMPARE(differenceCount(changed, Data::TopologyDifferenceKind::Added), 1);
    QVERIFY(changed.acceptAllowed);
}

void EtherCATScanTests::testBlockingIdentityAndDuplicateDifferences()
{
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    const Data::ProjectSnapshot project = projectSnapshot(
        projectId, masterId, {offlineSlave(masterId, 0, 0x1000, 1, 101)});

    Data::ScannedSlave wrongIdentity = scannedSlave(0, 0x9000, 1, 101);
    wrongIdentity.identity.vendorId = 9;
    const Data::TopologyComparison identity = compareTopology(
        project, masterId, scanSnapshot(projectId, masterId, {wrongIdentity}));
    QCOMPARE(differenceCount(identity, Data::TopologyDifferenceKind::VendorMismatch), 1);
    QCOMPARE(differenceCount(identity, Data::TopologyDifferenceKind::ProductMismatch), 1);
    QVERIFY(!identity.acceptAllowed);

    const Data::ScannedSlave duplicate = scannedSlave(0, 0x1000, 1, 101);
    Data::ScannedSlave duplicateAtOne = duplicate;
    duplicateAtOne.id = Data::NodeId::create();
    duplicateAtOne.position = 1;
    const Data::TopologyComparison duplicateResult = compareTopology(
        project,
        masterId,
        scanSnapshot(projectId, masterId, {duplicate, duplicateAtOne}));
    QCOMPARE(
        differenceCount(duplicateResult, Data::TopologyDifferenceKind::DuplicateDevice), 1);
    QVERIFY(!duplicateResult.acceptAllowed);
}

void EtherCATScanTests::testMockProviderStateCancellationAndFailure()
{
    auto *provider = ExtensionSystem::PluginManager::getObject<MockScanProvider>();
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    QVERIFY(provider);
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    const Utils::FilePath filePath = Utils::FilePath::fromString(directory.path())
                                         .canonicalPath()
                                         .pathAppended("provider.ecatproject");
    QVERIFY(filePath.writeFileContents(projectDocument(projectId, targetId, masterId)));
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(filePath, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    ProjectExplorer::Project *project = opened.project();
    QVERIFY(project);
    ProjectExplorer::ProjectManager::setStartupProject(project);
    auto *pages = ExtensionSystem::PluginManager::getObject<ScanPropertyPageProvider>();
    QVERIFY(pages);
    QWidget pageParent;
    const Core::PropertyPageContext pageContext{
        projectId, masterId, Core::WorkbenchNodeKind::Master, "EtherCAT Master"};
    QWidget *page = pages->createPage(Constants::PAGE_ID, &pageParent);
    QVERIFY(page);
    pages->updatePage(Constants::PAGE_ID, page, pageContext);
    QTreeWidget *differences = page->findChild<QTreeWidget *>(
        "EtherCATMockScanDifferences");
    QVERIFY(differences);

    provider->clearScanResult();
    provider->setScenario(MockScanScenario::Normal);
    provider->setStepIntervalForTests(0);
    QSignalSpy states(provider, &Core::ScanProvider::scanStateChanged);
    QVERIFY(provider->startScan({projectId, masterId, Data::ScanOperation::Slaves, {}}));
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Completed, 5000);
    const QList<Data::ScanState> expectedStates = {Data::ScanState::Preparing,
                                                   Data::ScanState::ScanningMaster,
                                                   Data::ScanState::ScanningSlaves,
                                                   Data::ScanState::BuildingSnapshot,
                                                   Data::ScanState::Comparing,
                                                   Data::ScanState::Completed};
    QList<Data::ScanState> actualStates;
    for (const QList<QVariant> &arguments : states)
        actualStates.append(arguments.first().value<Data::ScanState>());
    QCOMPARE(actualStates, expectedStates);
    QVERIFY(provider->lastScanResult());
    QVERIFY(provider->lastScanResult()->snapshot.mock);
    QCOMPARE(provider->lastScanResult()->snapshot.slaves.size(), 4);
    QTRY_COMPARE(differences->topLevelItemCount(), 6);
    QVERIFY(::Core::ActionManager::command(Constants::COMPARE_ACTION_ID)
                ->action()
                ->isEnabled());
    QVERIFY(::Core::ActionManager::command(Constants::ACCEPT_ACTION_ID)
                ->action()
                ->isEnabled());

    provider->clearScanResult();
    QVERIFY(provider->startScan(
        {projectId, masterId, Data::ScanOperation::Interfaces, {}}));
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Completed, 5000);
    QVERIFY(provider->lastScanResult());
    QCOMPARE(provider->lastScanResult()->snapshot.interfaces.size(), 1);
    QVERIFY(provider->lastScanResult()->snapshot.slaves.isEmpty());
    QVERIFY(!provider->compareWithCurrentProject());
    QTRY_COMPARE(differences->topLevelItemCount(), 1);
    QVERIFY(!::Core::ActionManager::command(Constants::COMPARE_ACTION_ID)
                 ->action()
                 ->isEnabled());
    QVERIFY(!::Core::ActionManager::command(Constants::ACCEPT_ACTION_ID)
                 ->action()
                 ->isEnabled());

    provider->clearScanResult();
    provider->setScenario(MockScanScenario::Slow);
    provider->setStepIntervalForTests(-1);
    QVERIFY(provider->startScan({projectId, masterId, Data::ScanOperation::Slaves, {}}));
    QTest::qWait(100);
    QCOMPARE(provider->scanState(), Data::ScanState::Preparing);
    provider->cancelScan();
    QCOMPARE(provider->scanState(), Data::ScanState::Cancelled);
    QVERIFY(!provider->lastScanResult());

    provider->clearScanResult();
    provider->setScenario(MockScanScenario::PartialFailure);
    provider->setStepIntervalForTests(0);
    QVERIFY(provider->startScan({projectId, masterId, Data::ScanOperation::Slaves, {}}));
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Failed, 5000);
    QVERIFY(provider->lastScanError().contains("MOCK partial failure"));
    QVERIFY(!provider->lastScanResult());
    QTRY_COMPARE(differences->topLevelItemCount(), 1);
    QVERIFY(differences->topLevelItem(0)->text(4).contains("MOCK partial failure"));

    provider->clearScanResult();
    provider->setScenario(MockScanScenario::Normal);
    {
        MockScanProvider shutdownProvider;
        shutdownProvider.setScenario(MockScanScenario::Slow);
        QVERIFY(shutdownProvider.startScan(
            {projectId, masterId, Data::ScanOperation::Slaves, {}}));
        shutdownProvider.shutdown();
        QCOMPARE(shutdownProvider.scanState(), Data::ScanState::Cancelled);
        QVERIFY(!shutdownProvider.isAvailable());
    }
    QTest::qWait(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    ProjectExplorer::ProjectManager::removeProject(project);
    QTest::qWait(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(service->projects().isEmpty());
}

void EtherCATScanTests::testProjectCloseClearsOwnedScanLifecycle_data()
{
    QTest::addColumn<int>("targetStateValue");
    QTest::newRow("active") << int(Data::ScanState::Preparing);
    QTest::newRow("completed") << int(Data::ScanState::Completed);
    QTest::newRow("failed") << int(Data::ScanState::Failed);
    QTest::newRow("cancelled") << int(Data::ScanState::Cancelled);
}

void EtherCATScanTests::testProjectCloseClearsOwnedScanLifecycle()
{
    QFETCH(int, targetStateValue);
    const Data::ScanState targetState = Data::ScanState(targetStateValue);
    auto *provider = ExtensionSystem::PluginManager::getObject<MockScanProvider>();
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    auto *stateService = ExtensionSystem::PluginManager::getObject<Core::StateService>();
    auto *pages = ExtensionSystem::PluginManager::getObject<ScanPropertyPageProvider>();
    QVERIFY(provider);
    QVERIFY(service);
    QVERIFY(stateService);
    QVERIFY(pages);
    QVERIFY(service->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::NodeId ownerProjectId = Data::NodeId::create();
    const Data::NodeId ownerTargetId = Data::NodeId::create();
    const Data::NodeId ownerMasterId = Data::NodeId::create();
    const Data::NodeId otherProjectId = Data::NodeId::create();
    const Data::NodeId otherTargetId = Data::NodeId::create();
    const Data::NodeId otherMasterId = Data::NodeId::create();
    const Utils::FilePath ownerPath = Utils::FilePath::fromString(directory.path())
                                          .canonicalPath()
                                          .pathAppended("owner.ecatproject");
    const Utils::FilePath otherPath = Utils::FilePath::fromString(directory.path())
                                          .canonicalPath()
                                          .pathAppended("other.ecatproject");
    QVERIFY(ownerPath.writeFileContents(
        projectDocument(ownerProjectId, ownerTargetId, ownerMasterId)));
    QVERIFY(otherPath.writeFileContents(
        projectDocument(otherProjectId, otherTargetId, otherMasterId)));

    QPointer<ProjectExplorer::Project> ownerProject;
    QPointer<ProjectExplorer::Project> otherProject;
    QPointer<ProjectExplorer::Project> reopenedProject;
    const QScopeGuard cleanup([&] {
        provider->clearScanResult();
        provider->setScenario(MockScanScenario::Normal);
        provider->setStepIntervalForTests(-1);
        for (const QPointer<ProjectExplorer::Project> &project :
             {reopenedProject, otherProject, ownerProject}) {
            if (project && ProjectExplorer::ProjectManager::hasProject(project))
                ProjectExplorer::ProjectManager::removeProject(project);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });

    provider->clearScanResult();
    provider->setScenario(
        targetState == Data::ScanState::Failed ? MockScanScenario::PartialFailure
                                               : targetState == Data::ScanState::Completed
                                                     ? MockScanScenario::Normal
                                                     : MockScanScenario::Slow);
    provider->setStepIntervalForTests(
        targetState == Data::ScanState::Completed || targetState == Data::ScanState::Failed
            ? 0
            : 60000);

    const ProjectExplorer::OpenProjectResult ownerOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(ownerPath, false);
    QVERIFY2(ownerOpened, qPrintable(ownerOpened.errorMessage()));
    ownerProject = ownerOpened.project();
    QVERIFY(ownerProject);
    const ProjectExplorer::OpenProjectResult otherOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(otherPath, false);
    QVERIFY2(otherOpened, qPrintable(otherOpened.errorMessage()));
    otherProject = otherOpened.project();
    QVERIFY(otherProject);
    ProjectExplorer::ProjectManager::setStartupProject(ownerProject);
    QTRY_COMPARE(service->projects().size(), 2);
    const Data::ProjectSnapshot ownerBaseline = *service->project(ownerProjectId);
    QVERIFY(!service->canUndoProject(ownerProjectId));
    QVERIFY(!service->canRedoProject(ownerProjectId));

    QWidget pageParent;
    QWidget *page = pages->createPage(Constants::PAGE_ID, &pageParent);
    QVERIFY(page);
    pages->updatePage(
        Constants::PAGE_ID,
        page,
        {ownerProjectId,
         ownerMasterId,
         Core::WorkbenchNodeKind::Master,
         "EtherCAT Master"});
    QTreeWidget *differences = page->findChild<QTreeWidget *>(
        "EtherCATMockScanDifferences");
    QVERIFY(differences);

    const auto commandAction = [](Utils::Id id) {
        ::Core::Command *command = ::Core::ActionManager::command(id);
        return command ? command->action() : nullptr;
    };
    const auto scanStatus = [stateService]() -> std::optional<Core::StatusEntry> {
        const QList<Core::StatusEntry> statuses = stateService->statuses();
        const auto status = std::find_if(
            statuses.cbegin(), statuses.cend(), [](const Core::StatusEntry &entry) {
                return entry.sourceId == Utils::Id(Constants::STATUS_SOURCE_ID);
            });
        return status == statuses.cend() ? std::nullopt
                                        : std::optional<Core::StatusEntry>(*status);
    };

    QAction *compareAction = commandAction(Constants::COMPARE_ACTION_ID);
    QAction *acceptAction = commandAction(Constants::ACCEPT_ACTION_ID);
    QAction *keepAction = commandAction(Constants::KEEP_ACTION_ID);
    QAction *cancelAction = commandAction(Constants::CANCEL_ACTION_ID);
    QVERIFY(compareAction);
    QVERIFY(acceptAction);
    QVERIFY(keepAction);
    QVERIFY(cancelAction);

    QVERIFY(provider->startScan(
        {ownerProjectId, ownerMasterId, Data::ScanOperation::Slaves, {}}));
    if (targetState == Data::ScanState::Cancelled) {
        QCOMPARE(provider->scanState(), Data::ScanState::Preparing);
        provider->cancelScan();
    }
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), targetState, 5000);

    if (targetState == Data::ScanState::Completed) {
        QVERIFY(provider->lastScanResult());
        QCOMPARE(provider->lastScanResult()->snapshot.projectId, ownerProjectId);
        QTRY_VERIFY(differences->topLevelItemCount() > 0);
    } else if (targetState == Data::ScanState::Failed) {
        QVERIFY(!provider->lastScanResult());
        QVERIFY(!provider->lastScanError().isEmpty());
        QTRY_COMPARE(differences->topLevelItemCount(), 1);
    } else {
        QVERIFY(!provider->lastScanResult());
        QTRY_COMPARE(differences->topLevelItemCount(), 0);
    }

    const bool expectedCompare = targetState == Data::ScanState::Completed;
    const bool expectedAccept = targetState == Data::ScanState::Completed;
    const bool expectedKeep = targetState == Data::ScanState::Completed
                              || targetState == Data::ScanState::Failed
                              || targetState == Data::ScanState::Cancelled;
    const bool expectedCancel = targetState == Data::ScanState::Preparing;
    QTRY_COMPARE(compareAction->isEnabled(), expectedCompare);
    QTRY_COMPARE(acceptAction->isEnabled(), expectedAccept);
    QTRY_COMPARE(keepAction->isEnabled(), expectedKeep);
    QTRY_COMPARE(cancelAction->isEnabled(), expectedCancel);

    const std::optional<Core::StatusEntry> statusBeforeClose = scanStatus();
    if (targetState == Data::ScanState::Cancelled) {
        QVERIFY(!statusBeforeClose);
    } else {
        QVERIFY(statusBeforeClose);
        const Core::StatusSeverity expectedSeverity
            = targetState == Data::ScanState::Preparing
                  ? Core::StatusSeverity::Busy
                  : targetState == Data::ScanState::Failed ? Core::StatusSeverity::Error
                                                          : Core::StatusSeverity::Ready;
        QCOMPARE(statusBeforeClose->severity, expectedSeverity);
    }

    QCOMPARE(*service->project(ownerProjectId), ownerBaseline);
    QVERIFY(!service->canUndoProject(ownerProjectId));
    QVERIFY(!service->canRedoProject(ownerProjectId));
    const Data::ScanProgress progressBeforeClose = provider->scanProgress();
    const std::optional<Data::ScanResult> resultBeforeClose = provider->lastScanResult();
    const QString errorBeforeClose = provider->lastScanError();
    const int differencesBeforeClose = differences->topLevelItemCount();
    QSignalSpy stateChanges(provider, &Core::ScanProvider::scanStateChanged);
    QSignalSpy resultChanges(provider, &Core::ScanProvider::scanResultChanged);
    QSignalSpy finished(provider, &Core::ScanProvider::scanFinished);

    ProjectExplorer::ProjectManager::removeProject(otherProject);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(service->projects().size(), 1);
    QCOMPARE(provider->scanProgress(), progressBeforeClose);
    QCOMPARE(provider->lastScanResult().has_value(), resultBeforeClose.has_value());
    QCOMPARE(provider->lastScanError(), errorBeforeClose);
    QCOMPARE(differences->topLevelItemCount(), differencesBeforeClose);
    QCOMPARE(compareAction->isEnabled(), expectedCompare);
    QCOMPARE(acceptAction->isEnabled(), expectedAccept);
    QCOMPARE(keepAction->isEnabled(), expectedKeep);
    QCOMPARE(cancelAction->isEnabled(), expectedCancel);
    QVERIFY(scanStatus() == statusBeforeClose);
    QCOMPARE(stateChanges.count(), 0);
    QCOMPARE(resultChanges.count(), 0);
    QCOMPARE(finished.count(), 0);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const ProjectExplorer::OpenProjectResult otherReopened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(otherPath, false);
    QVERIFY2(otherReopened, qPrintable(otherReopened.errorMessage()));
    otherProject = otherReopened.project();
    QVERIFY(otherProject);
    QTRY_COMPARE(service->projects().size(), 2);

    ProjectExplorer::ProjectManager::removeProject(ownerProject);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(service->projects().size(), 1);
    QVERIFY(service->project(otherProjectId));
    QTRY_COMPARE(provider->scanState(), Data::ScanState::Idle);
    QCOMPARE(provider->scanProgress(), Data::ScanProgress());
    QVERIFY(!provider->lastScanResult());
    QVERIFY(provider->lastScanError().isEmpty());
    QTRY_COMPARE(differences->topLevelItemCount(), 0);
    QVERIFY(!compareAction->isEnabled());
    QVERIFY(!acceptAction->isEnabled());
    QVERIFY(!keepAction->isEnabled());
    QVERIFY(!cancelAction->isEnabled());
    QTRY_VERIFY(!scanStatus());

    const int expectedStateChanges
        = targetState == Data::ScanState::Preparing ? 2 : 1;
    QCOMPARE(stateChanges.count(), expectedStateChanges);
    if (targetState == Data::ScanState::Preparing) {
        QCOMPARE(
            stateChanges.at(0).at(0).value<Data::ScanState>(),
            Data::ScanState::Cancelled);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(
            finished.at(0).at(0).value<Data::ScanState>(),
            Data::ScanState::Cancelled);
    } else {
        QCOMPARE(finished.count(), 0);
    }
    QVERIFY(!resultChanges.isEmpty());
    QCOMPARE(
        stateChanges.constLast().at(0).value<Data::ScanState>(),
        Data::ScanState::Idle);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const ProjectExplorer::OpenProjectResult reopened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(ownerPath, false);
    QVERIFY2(reopened, qPrintable(reopened.errorMessage()));
    reopenedProject = reopened.project();
    QVERIFY(reopenedProject);
    ProjectExplorer::ProjectManager::setStartupProject(reopenedProject);
    QTRY_COMPARE(service->projects().size(), 2);
    QVERIFY(service->project(ownerProjectId));
    const Data::ProjectSnapshot reopenedBaseline = *service->project(ownerProjectId);
    QCOMPARE(reopenedBaseline.id, ownerBaseline.id);
    QCOMPARE(reopenedBaseline.name, ownerBaseline.name);
    QCOMPARE(reopenedBaseline.nodes, ownerBaseline.nodes);
    QCOMPARE(reopenedBaseline.slaves, ownerBaseline.slaves);
    QVERIFY(!service->canUndoProject(ownerProjectId));
    QVERIFY(!service->canRedoProject(ownerProjectId));
    QCOMPARE(provider->scanState(), Data::ScanState::Idle);
    QVERIFY(!provider->lastScanResult());
    QVERIFY(!scanStatus());

    provider->setScenario(MockScanScenario::Normal);
    provider->setStepIntervalForTests(0);
    QVERIFY(provider->startScan(
        {ownerProjectId, ownerMasterId, Data::ScanOperation::Slaves, {}}));
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Completed, 5000);
    QVERIFY(provider->lastScanResult());
    QCOMPARE(provider->lastScanResult()->snapshot.projectId, ownerProjectId);
    QCOMPARE(*service->project(ownerProjectId), reopenedBaseline);
    QVERIFY(!service->canUndoProject(ownerProjectId));
    QVERIFY(!service->canRedoProject(ownerProjectId));
    provider->clearScanResult();
    QCOMPARE(provider->scanState(), Data::ScanState::Idle);
}

void EtherCATScanTests::testWorkflowAcceptUndoAndRedo()
{
    auto *provider = ExtensionSystem::PluginManager::getObject<MockScanProvider>();
    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    auto *selection = ExtensionSystem::PluginManager::getObject<Core::SelectionService>();
    QVERIFY(provider);
    QVERIFY(service);
    QVERIFY(selection);
    QVERIFY(service->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    const Utils::FilePath filePath = Utils::FilePath::fromString(directory.path())
                                         .canonicalPath()
                                         .pathAppended("workflow.ecatproject");
    QVERIFY(filePath.writeFileContents(projectDocument(projectId, targetId, masterId)));
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(filePath, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    ProjectExplorer::Project *project = opened.project();
    QVERIFY(project);
    ProjectExplorer::ProjectManager::setStartupProject(project);

    selection->setCurrentNodeId(masterId);
    provider->clearScanResult();
    provider->setScenario(MockScanScenario::Normal);
    provider->setStepIntervalForTests(0);
    ScanWorkflow workflow(provider);
    QVERIFY(workflow.scanInterfaces());
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Completed, 5000);
    QVERIFY(!workflow.acceptScan());
    QVERIFY(service->project(projectId)->slaves.isEmpty());
    provider->clearScanResult();
    QVERIFY(workflow.scanSlaves());
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Completed, 5000);
    const QList<Data::OfflineSlaveConfiguration> blocker{
        {Data::NodeId::create(),
         masterId,
         0,
         {9, 0x9000, 1},
         101,
         0,
         "Blocking Offline Slave",
         {},
         {},
         {},
         {}}};
    QVERIFY(service->replaceOfflineSlaves(projectId, masterId, blocker));
    QVERIFY(!workflow.acceptScan());
    QVERIFY(!provider->lastScanResult()->comparison.acceptAllowed);
    QVERIFY(service->undoProject(projectId));
    QVERIFY(service->project(projectId)->slaves.isEmpty());
    QVERIFY(workflow.acceptScan());
    QCOMPARE(service->project(projectId)->slaves.size(), 4);
    QVERIFY(service->canUndoProject(projectId));
    QVERIFY(provider->lastScanResult()->comparison.exactMatch);
    const Data::ProjectSnapshot baseline = *service->project(projectId);
    const Data::OfflineSlaveConfiguration selectedSlave = baseline.slaves.at(2);

    provider->clearScanResult();
    provider->setScenario(MockScanScenario::RevisionMismatch);
    selection->setCurrentNodeId(selectedSlave.id);
    QVERIFY(workflow.rescanSelectedBranch());
    QTRY_COMPARE_WITH_TIMEOUT(provider->scanState(), Data::ScanState::Completed, 5000);
    QCOMPARE(provider->lastScanResult()->snapshot.operation,
             Data::ScanOperation::SelectedBranch);
    QCOMPARE(provider->lastScanResult()->snapshot.branchNodeId, selectedSlave.id);
    QCOMPARE(provider->lastScanResult()->snapshot.slaves.size(), 1);
    QCOMPARE(
        differenceCount(
            provider->lastScanResult()->comparison,
            Data::TopologyDifferenceKind::RevisionMismatch),
        1);
    QVERIFY(provider->lastScanResult()->comparison.acceptAllowed);
    QVERIFY(workflow.acceptScan());
    QVERIFY(provider->lastScanResult()->comparison.exactMatch);
    const Data::ProjectSnapshot acceptedBranch = *service->project(projectId);
    QCOMPARE(acceptedBranch.slaves.size(), 4);
    const auto updatedSlave = std::find_if(
        acceptedBranch.slaves.cbegin(),
        acceptedBranch.slaves.cend(),
        [&selectedSlave](const Data::OfflineSlaveConfiguration &slave) {
            return slave.id == selectedSlave.id;
        });
    QVERIFY(updatedSlave != acceptedBranch.slaves.cend());
    QCOMPARE(updatedSlave->identity.revisionNumber,
             selectedSlave.identity.revisionNumber + 1);

    QVERIFY(service->undoProject(projectId));
    QCOMPARE(service->project(projectId)->slaves, baseline.slaves);
    QVERIFY(service->undoProject(projectId));
    QVERIFY(service->project(projectId)->slaves.isEmpty());
    QVERIFY(service->canRedoProject(projectId));
    QVERIFY(service->redoProject(projectId));
    QCOMPARE(service->project(projectId)->slaves, baseline.slaves);
    QVERIFY(service->redoProject(projectId));
    QCOMPARE(service->project(projectId)->slaves, acceptedBranch.slaves);

    workflow.shutdown();
    provider->clearScanResult();
    provider->setScenario(MockScanScenario::Normal);
    selection->clear();
    QTest::qWait(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    ProjectExplorer::ProjectManager::removeProject(project);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(service->projects().isEmpty());
}

} // namespace EtherCAT::Scan::Internal
