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
#include <QProgressBar>
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
    const QList<Data::OfflineSlaveConfiguration> offline{
        offlineSlave(masterId, 0, 0x1000, 1, 101),
        offlineSlave(masterId, 1, 0x2000, 1, 102)};
    const Data::ProjectSnapshot project = projectSnapshot(projectId, masterId, offline);

    Data::TopologyComparison exact = compareTopology(
        project,
        masterId,
        scanSnapshot(projectId,
                     masterId,
                     {scannedSlave(0, 0x1000, 1, 101),
                      scannedSlave(1, 0x2000, 1, 102)}));
    QVERIFY(exact.exactMatch);
    QVERIFY(exact.acceptAllowed);
    QCOMPARE(differenceCount(exact, Data::TopologyDifferenceKind::PdoConfiguration), 1);
    QCOMPARE(differenceCount(exact, Data::TopologyDifferenceKind::DcConfiguration), 1);

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
