// Copyright (C) 2026 Kvell

#include "ethercatworkbenchtests.h"

#include "builtinpropertypages.h"
#include "detailsview.h"
#include "ethercatworkbenchconstants.h"
#include "workbenchcontroller.h"
#include "workbenchnavigation.h"
#include "workbenchtreemodel.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/imode.h>
#include <coreplugin/modemanager.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <utils/filepath.h>

#include <QAbstractButton>
#include <QAbstractItemModelTester>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>

#include <algorithm>

namespace EtherCAT::Workbench::Internal {

static Data::ProjectSnapshot projectSnapshot(const QString &name = "Packaging Line")
{
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    return {
        projectId,
        name,
        1,
        "Workbench Test",
        {{projectId, {}, Data::ProjectNodeKind::Project, name},
         {targetId, projectId, Data::ProjectNodeKind::Target, "Offline Controller"},
         {masterId, targetId, Data::ProjectNodeKind::Master, "EtherCAT Master"}},
        false,
        true,
        false,
        {},
        {}};
}

static Data::NodeId masterId(const Data::ProjectSnapshot &project)
{
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind == Data::ProjectNodeKind::Master)
            return node.id;
    }
    return {};
}

struct TestProjectFile
{
    Utils::FilePath path;
    Data::NodeId projectId;
    Data::NodeId masterId;
    Data::NodeId slaveId;
};

static TestProjectFile writeProjectWithSlave(
    const QTemporaryDir &directory, const Data::DeviceSummary &device)
{
    TestProjectFile result;
    result.path = Utils::FilePath::fromString(directory.path())
                      .canonicalPath()
                      .pathAppended("process-data.ecatproject");
    result.projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    result.masterId = Data::NodeId::create();
    result.slaveId = Data::NodeId::create();

    const QJsonObject processData{{"syncManagers", QJsonArray()}, {"pdos", QJsonArray()}};
    const QJsonObject startup{{"parameters", QJsonArray()}};
    const QJsonObject
        dc{{"enabled", false},
           {"modeName", ""},
           {"assignActivate", 0},
           {"sync0", QJsonObject{{"enabled", false}, {"cycleTimeNs", 0}, {"shiftTimeNs", 0}}},
           {"sync1", QJsonObject{{"enabled", false}, {"cycleTimeNs", 0}, {"shiftTimeNs", 0}}},
           {"potentialReferenceClock", false}};
    const QJsonObject slave{
        {"id", result.slaveId.toString()},
        {"name", "Configured Servo"},
        {"position", 0},
        {"vendorId", double(device.identity.vendorId)},
        {"productCode", double(device.identity.productCode)},
        {"revisionNumber", double(device.identity.revisionNumber)},
        {"serialNumber", 17},
        {"alias", 3},
        {"deviceDescriptionId", device.id.toString()},
        {"configuration",
         QJsonObject{{"processData", processData}, {"startup", startup}, {"dc", dc}}}};
    const QJsonObject root{
        {"format", "ethercat-project"},
        {"formatVersion", 2},
        {"project",
         QJsonObject{
             {"id", result.projectId.toString()},
             {"name", "Process Data Workflow"},
             {"createdBy", "Workbench Test"}}},
        {"target", QJsonObject{{"id", targetId.toString()}, {"name", "Offline Controller"}}},
        {"master",
         QJsonObject{
             {"id", result.masterId.toString()},
             {"name", "EtherCAT Master"},
             {"slaves", QJsonArray{slave}}}}};
    const Utils::Result<qint64> written = result.path.writeFileContents(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!written)
        return {};
    return result;
}

static int columnWithHeader(const QAbstractItemModel *model, const QString &header)
{
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() == header)
            return column;
    }
    return -1;
}

static QList<Data::DeviceSummary> deviceSummaries(int count)
{
    QList<Data::DeviceSummary> devices;
    devices.reserve(count);
    for (int index = 0; index < count; ++index) {
        devices.append(
            {Data::NodeId::create(),
             {2, quint32(0x1000 + index), 1},
             QString("Device %1").arg(index, 4, 10, QLatin1Char('0')),
             QString("T%1").arg(index),
             "I/O",
             index != 123});
    }
    return devices;
}

static QModelIndex findByKind(
    const QAbstractItemModel *model, Core::WorkbenchNodeKind kind, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>() == kind)
            return index;
        const QModelIndex child = findByKind(model, kind, index);
        if (child.isValid())
            return child;
    }
    return {};
}

static QModelIndex findById(
    const QAbstractItemModel *model, const Data::NodeId &nodeId, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>() == nodeId)
            return index;
        const QModelIndex child = findById(model, nodeId, index);
        if (child.isValid())
            return child;
    }
    return {};
}

static Data::DeviceImportResult waitForJob(Core::DeviceImportJob *job)
{
    QSignalSpy finished(job, &Core::DeviceImportJob::finished);
    if (job->state() != Core::DeviceImportState::Finished)
        finished.wait(15000);
    return job->result();
}

static QByteArray deviceEsi()
{
    return R"(<?xml version="1.0"?>
<EtherCATInfo><Vendor><Id>#x00000002</Id></Vendor><Descriptions>
<Groups><Group><Type>Drive</Type><Name>Drives</Name></Group></Groups><Devices><Device>
<Type ProductCode="#x00005678" RevisionNo="#x00000011">AX5000</Type>
<Name>Workbench Servo</Name><GroupType>Drive</GroupType>
<Sm StartAddress="#x1000" DefaultSize="32" ControlByte="#x26" Enable="1">Outputs</Sm>
<Sm StartAddress="#x1100" DefaultSize="32" ControlByte="#x22" Enable="1">Inputs</Sm>
<RxPdo Sm="0"><Index>#x1600</Index><Name>Command</Name><Entry><Index>#x6040</Index>
<SubIndex>0</SubIndex><BitLen>16</BitLen><Name>Controlword</Name><DataType>UINT</DataType>
</Entry></RxPdo><TxPdo Sm="1" Fixed="1" Mandatory="1"><Index>#x1A00</Index><Name>Status</Name>
<Entry><Index>#x6041</Index><SubIndex>0</SubIndex><BitLen>16</BitLen>
<Name>Statusword</Name><DataType>UINT</DataType></Entry></TxPdo>
<Mailbox><CoE><InitCmds><InitCmd><Transition>PS</Transition>
<Index>#x6060</Index><SubIndex>0</SubIndex><Data>08</Data><Comment>Mode</Comment>
</InitCmd><InitCmd><Transition>SO</Transition><Index>#x6072</Index><SubIndex>0</SubIndex>
<Data>3412</Data><Comment>Maximum torque</Comment></InitCmd>
<InitCmd><Transition>&lt;PS&gt;</Transition><Index>#x8000</Index><SubIndex>1</SubIndex>
<Data>00</Data><Comment>Fixed ESI request</Comment></InitCmd></InitCmds></CoE></Mailbox>
<Dc><OpMode><Name>Sync0</Name>
<AssignActivate>#x0300</AssignActivate><CycleTimeSync0>125000</CycleTimeSync0>
</OpMode><OpMode><Name>Sync0 + Sync1</Name><AssignActivate>#x0700</AssignActivate>
<CycleTimeSync0>250000</CycleTimeSync0><ShiftTimeSync0>-1000</ShiftTimeSync0>
<CycleTimeSync1>500000</CycleTimeSync1><ShiftTimeSync1>1000</ShiftTimeSync1>
</OpMode></Dc></Device></Devices></Descriptions></EtherCATInfo>)";
}

class TestPageProvider final : public Core::PropertyPageProvider
{
public:
    TestPageProvider()
        : PropertyPageProvider("EtherCAT.Workbench.TestPages", "Test pages")
    {
        setAvailable(true);
    }

    QList<Core::PropertyPageDescriptor> pages(const Core::PropertyPageContext &context) const final
    {
        if (context.nodeKind != Core::WorkbenchNodeKind::Project)
            return {};
        return {{Utils::Id("EtherCAT.Workbench.TestPage"), "Test Page", 150}};
    }

    QWidget *createPage(Utils::Id, QWidget *parent) final
    {
        auto label = new QLabel("Dynamic page", parent);
        label->setObjectName("EtherCATDynamicTestPage");
        return label;
    }

    void updatePage(Utils::Id, QWidget *page, const Core::PropertyPageContext &context) final
    {
        page->setToolTip(context.nodeId.toString());
    }
};

class AvailableScanProvider final : public Core::ScanProvider
{
public:
    AvailableScanProvider()
        : ScanProvider("EtherCAT.Workbench.TestScan", "Test scan provider")
    {}

    Data::ScanState scanState() const final { return Data::ScanState::Idle; }
    Data::ScanProgress scanProgress() const final { return {}; }
    std::optional<Data::ScanResult> lastScanResult() const final { return std::nullopt; }
    QString lastScanError() const final { return {}; }
    Utils::Result<> startScan(const Data::ScanRequest &) final { return Utils::ResultOk; }
    void cancelScan() final {}
    void clearScanResult() final {}
};

class AvailableDiagnosticsProvider final : public Core::DiagnosticsProvider
{
public:
    AvailableDiagnosticsProvider()
        : DiagnosticsProvider("EtherCAT.Workbench.TestDiagnostics", "Test diagnostics provider")
    {}

    Data::DiagnosticsStreamState streamState() const final
    {
        return Data::DiagnosticsStreamState::Stopped;
    }
    Data::DiagnosticsRequest activeRequest() const final { return {}; }
    std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const final { return std::nullopt; }
    QList<Data::DiagnosticEvent> events() const final { return {}; }
    QList<Data::DiagnosticTrendSample> trendSamples() const final { return {}; }
    Data::DiagnosticsLimits limits() const final { return {}; }
    QString lastDiagnosticsError() const final { return {}; }
    Utils::Result<> startMonitoring(const Data::DiagnosticsRequest &) final
    {
        return Utils::ResultOk;
    }
    void stopMonitoring() final {}
    Utils::Result<> requestRunMode(Data::DiagnosticsRunMode) final { return Utils::ResultOk; }
    Utils::Result<> acknowledgeAlarm(const Data::NodeId &) final { return Utils::ResultOk; }
    Utils::Result<> clearRecoveredEvents() final { return Utils::ResultOk; }
};

void EtherCATWorkbenchTests::testMetadataModeActionsAndProvider()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        Constants::PLUGIN_ID);
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATWorkbench"));
    QVERIFY(!spec->hasError());

    const QList<ExtensionSystem::PluginDependency> dependencies = spec->dependencies();
    const auto hasDependency = [&dependencies](const QString &id) {
        return std::any_of(dependencies.cbegin(), dependencies.cend(), [&id](const auto &dependency) {
            return dependency.id == id
                   && dependency.type == ExtensionSystem::PluginDependency::Required;
        });
    };
    QVERIFY(hasDependency("core"));
    QVERIFY(hasDependency("ethercatcore"));
    QVERIFY(hasDependency("ethercatdevices"));
    QVERIFY(hasDependency("ethercatproject"));

    Core::ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    Core::Provider *pages = registry->provider(Constants::BUILTIN_PAGE_PROVIDER_ID);
    QVERIFY(pages);
    QCOMPARE(pages->kind(), Core::ProviderKind::PropertyPage);
    QVERIFY(pages->isAvailable());

    QVERIFY(::Core::ActionManager::command(Constants::OPEN_ACTION_ID));
    QVERIFY(::Core::ActionManager::command(Constants::REFRESH_ACTION_ID));
    ::Core::ActionManager::command(Constants::OPEN_ACTION_ID)->action()->trigger();
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    QVERIFY(::Core::ModeManager::currentMode());
    QTRY_VERIFY(::Core::ModeManager::currentMode()->widget());
    QCOMPARE(
        ::Core::ModeManager::currentMode()->widget()->objectName(),
        QString("EtherCATWorkbenchModeWidget"));
}

void EtherCATWorkbenchTests::testTreeModelLargeIncrementalUpdate()
{
    WorkbenchTreeModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.setProjects({projectSnapshot()});
    QCOMPARE(reset.count(), 1);

    const QModelIndex master = findByKind(&model, Core::WorkbenchNodeKind::Master);
    QVERIFY(master.isValid());
    QCOMPARE(model.rowCount(master), 2);
    QVERIFY(findByKind(&model, Core::WorkbenchNodeKind::Diagnostics).isValid());

    const QModelIndex repository = findByKind(&model, Core::WorkbenchNodeKind::DeviceRepository);
    QVERIFY(repository.isValid());
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QList<Data::DeviceSummary> devices = deviceSummaries(500);
    model.syncDevices(devices);
    QCOMPARE(model.rowCount(repository), 500);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(reset.count(), 1);
    QVERIFY(model.firstUnsupportedDevice().isValid());

    const Data::NodeId retainedId = devices.at(42).id;
    devices.removeFirst();
    devices[41].name = "Renamed Device";
    devices.append({Data::NodeId::create(), {3, 7, 1}, "Additional Device", "New", "I/O", true});
    model.syncDevices(devices);
    QCOMPARE(model.rowCount(repository), 500);
    QVERIFY(removed.count() >= 1);
    QVERIFY(inserted.count() >= 2);
    QVERIFY(changed.count() >= 1);
    QCOMPARE(model.contextForNodeId(retainedId).displayName, QString("Renamed Device"));
    QCOMPARE(reset.count(), 1);
}

void EtherCATWorkbenchTests::testNavigationSelectionAndFiltering()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Navigation Project");
    const QList<Data::DeviceSummary> devices = deviceSummaries(50);
    controller.treeModel()->setProjects({project});
    controller.treeModel()->syncDevices(devices);
    controller.selectionService()->clear();

    WorkbenchNavigationWidget navigation(&controller);
    controller.selectionService()->setCurrentNodeId(devices.at(42).id);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        devices.at(42).id);

    navigation.filterEdit()->setText("Device 0042");
    QTRY_VERIFY(findById(navigation.treeView()->model(), devices.at(42).id).isValid());
    QVERIFY(!findById(navigation.treeView()->model(), devices.at(7).id).isValid());

    controller.selectionService()->setCurrentNodeId(devices.at(7).id);
    QTRY_VERIFY(navigation.filterEdit()->text().isEmpty());
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        devices.at(7).id);

    const QModelIndex proxyIndex = findById(navigation.treeView()->model(), devices.at(7).id);
    QVERIFY(proxyIndex.isValid());
    navigation.treeView()->setCurrentIndex(proxyIndex);
    QCOMPARE(controller.selectionService()->currentNodeId(), devices.at(7).id);
    controller.selectionService()->clear();
}

void EtherCATWorkbenchTests::testBuiltInDevicePages()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath sourcePath = Utils::FilePath::fromString(directory.path())
                                           .canonicalPath()
                                           .pathAppended("workbench-device.xml");
    QVERIFY_RESULT(sourcePath.writeFileContents(deviceEsi()));
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles({sourcePath}));
    QCOMPARE(importResult.failedFiles, 0);

    const QList<Data::DeviceSummary> repositoryDevices = repository->devices();
    const auto found
        = std::find_if(repositoryDevices.cbegin(), repositoryDevices.cend(), [](const auto &device) {
              return device.identity.productCode == 0x5678;
          });
    QVERIFY(found != repositoryDevices.cend());
    const Core::PropertyPageContext
        context{{}, found->id, Core::WorkbenchNodeKind::Device, found->name};

    BuiltinPropertyPageProvider provider(&controller);
    QCOMPARE(provider.pages(context).size(), 6);

    std::unique_ptr<QWidget> processPage(
        provider.createPage(Constants::PROCESS_DATA_PAGE_ID, nullptr));
    QVERIFY(processPage);
    provider.updatePage(Constants::PROCESS_DATA_PAGE_ID, processPage.get(), context);
    QTableView *syncManagers = processPage->findChild<QTableView *>(
        "EtherCATProcessDataSyncManagers");
    QTableView *pdoAssignments = processPage->findChild<QTableView *>(
        "EtherCATProcessDataAssignments");
    QTableView *pdoList = processPage->findChild<QTableView *>("EtherCATProcessDataPdoList");
    QTableView *pdoContent = processPage->findChild<QTableView *>("EtherCATProcessDataPdoContent");
    QTableView *processImage = processPage->findChild<QTableView *>("EtherCATProcessDataImage");
    QVERIFY(syncManagers);
    QVERIFY(pdoAssignments);
    QVERIFY(pdoList);
    QVERIFY(pdoContent);
    QVERIFY(processImage);
    QCOMPARE(syncManagers->model()->rowCount(), 2);
    QCOMPARE(pdoAssignments->model()->rowCount(), 1);
    QCOMPARE(pdoList->model()->rowCount(), 1);
    QCOMPARE(pdoContent->model()->rowCount(), 1);
    QCOMPARE(processImage->model()->rowCount(), 2);
    QVERIFY(
        !(pdoAssignments->model()->flags(pdoAssignments->model()->index(0, 0))
          & Qt::ItemIsUserCheckable));

    std::unique_ptr<QWidget> startupPage(provider.createPage(Constants::STARTUP_PAGE_ID, nullptr));
    provider.updatePage(Constants::STARTUP_PAGE_ID, startupPage.get(), context);
    QTableView *startupTable = startupPage->findChild<QTableView *>("EtherCATStartupTable");
    QVERIFY(startupTable);
    QCOMPARE(startupTable->model()->rowCount(), 3);
    QVERIFY(columnWithHeader(startupTable->model(), "Enabled") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Order") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Transition") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Protocol") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Index") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Subindex") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Type") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Data") >= 0);
    QVERIFY(columnWithHeader(startupTable->model(), "Comment") >= 0);
    QVERIFY(!(
        startupTable->model()->flags(startupTable->model()->index(0, 0)) & Qt::ItemIsUserCheckable));

    std::unique_ptr<QWidget> dcPage(provider.createPage(Constants::DC_PAGE_ID, nullptr));
    provider.updatePage(Constants::DC_PAGE_ID, dcPage.get(), context);
    QComboBox *dcMode = dcPage->findChild<QComboBox *>("EtherCATDcOperationMode");
    QCheckBox *dcEnabled = dcPage->findChild<QCheckBox *>("EtherCATDcEnabled");
    QLineEdit *dcAssignActivate = dcPage->findChild<QLineEdit *>("EtherCATDcAssignActivate");
    QCheckBox *dcSync0Enabled = dcPage->findChild<QCheckBox *>("EtherCATDcSync0Enabled");
    QLineEdit *dcSync0Cycle = dcPage->findChild<QLineEdit *>("EtherCATDcSync0CycleNs");
    QLineEdit *dcSync0Shift = dcPage->findChild<QLineEdit *>("EtherCATDcSync0ShiftNs");
    QCheckBox *dcSync1Enabled = dcPage->findChild<QCheckBox *>("EtherCATDcSync1Enabled");
    QLineEdit *dcSync1Cycle = dcPage->findChild<QLineEdit *>("EtherCATDcSync1CycleNs");
    QLineEdit *dcSync1Shift = dcPage->findChild<QLineEdit *>("EtherCATDcSync1ShiftNs");
    QCheckBox *dcReferenceClock = dcPage->findChild<QCheckBox *>(
        "EtherCATDcPotentialReferenceClock");
    QVERIFY(dcMode);
    QVERIFY(dcEnabled);
    QVERIFY(dcAssignActivate);
    QVERIFY(dcSync0Enabled);
    QVERIFY(dcSync0Cycle);
    QVERIFY(dcSync0Shift);
    QVERIFY(dcSync1Enabled);
    QVERIFY(dcSync1Cycle);
    QVERIFY(dcSync1Shift);
    QVERIFY(dcReferenceClock);
    QCOMPARE(dcMode->count(), 2);
    QCOMPARE(dcMode->currentText(), QString("Sync0"));
    QVERIFY(dcEnabled->isChecked());
    QCOMPARE(dcAssignActivate->text(), QString("0x0300"));
    QVERIFY(dcSync0Enabled->isChecked());
    QCOMPARE(dcSync0Cycle->text(), QString("125000"));
    QCOMPARE(dcSync0Shift->text(), QString("0"));
    QVERIFY(!dcSync1Enabled->isChecked());
    QVERIFY(!dcMode->isEnabled());
    QVERIFY(!dcEnabled->isEnabled());
    QVERIFY(dcAssignActivate->isReadOnly());
    QVERIFY(dcSync0Cycle->isReadOnly());
    QVERIFY(dcSync0Shift->isReadOnly());
    QVERIFY(dcSync1Cycle->isReadOnly());
    QVERIFY(dcSync1Shift->isReadOnly());
    QVERIFY(!dcReferenceClock->isEnabled());

    std::unique_ptr<QWidget> onlinePage(provider.createPage(Constants::ONLINE_PAGE_ID, nullptr));
    provider.updatePage(Constants::ONLINE_PAGE_ID, onlinePage.get(), context);
    QVERIFY(onlinePage->findChild<QLabel *>("EtherCATWorkbenchPageSummary")
                ->text()
                .contains("unavailable", Qt::CaseInsensitive));
}

void EtherCATWorkbenchTests::testConfiguredSlaveTreeAndPages()
{
    WorkbenchController controller;
    QAbstractItemModelTester
        modelTester(controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath sourcePath = Utils::FilePath::fromString(directory.path())
                                           .canonicalPath()
                                           .pathAppended("configured-slave.xml");
    QVERIFY_RESULT(sourcePath.writeFileContents(deviceEsi()));
    QCOMPARE(waitForJob(repository->importFiles({sourcePath})).failedFiles, 0);
    const QList<Data::DeviceSummary> repositoryDevices = repository->devices();
    const auto found
        = std::find_if(repositoryDevices.cbegin(), repositoryDevices.cend(), [](const auto &device) {
              return device.identity.productCode == 0x5678;
          });
    QVERIFY(found != repositoryDevices.cend());

    Data::ProjectSnapshot project = projectSnapshot("Configured Topology");
    const Data::NodeId master = masterId(project);
    const Data::NodeId slaveId = Data::NodeId::create();
    project.slaves = {
        {slaveId, master, 0, found->identity, 17, 3, "Configured Servo", found->id, {}, {}, {}}};
    project.nodes.append({slaveId, master, Data::ProjectNodeKind::Slave, "Configured Servo"});
    controller.treeModel()->setProjects({project});

    const QModelIndex slave
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::ConfiguredSlave);
    QVERIFY(slave.isValid());
    QCOMPARE(slave.data().toString(), QString("Configured Servo"));
    QCOMPARE(slave.siblingAtColumn(1).data().toString(), QString("Offline configured"));
    const QModelIndex masterIndex
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::Master);
    QCOMPARE(controller.treeModel()->rowCount(masterIndex), 2);
    QVERIFY(!findByKind(controller.treeModel(), Core::WorkbenchNodeKind::Placeholder, masterIndex)
                 .isValid());

    BuiltinPropertyPageProvider pages(&controller);
    const Core::PropertyPageContext context = controller.treeModel()->contextForIndex(slave);
    QCOMPARE(pages.pages(context).size(), 6);
    std::unique_ptr<QWidget> processPage(pages.createPage(Constants::PROCESS_DATA_PAGE_ID, nullptr));
    pages.updatePage(Constants::PROCESS_DATA_PAGE_ID, processPage.get(), context);
    QTableView *syncManagers = processPage->findChild<QTableView *>(
        "EtherCATProcessDataSyncManagers");
    QVERIFY(syncManagers);
    QCOMPARE(syncManagers->model()->rowCount(), 2);

    std::unique_ptr<QWidget> generalPage(pages.createPage(Constants::GENERAL_PAGE_ID, nullptr));
    pages.updatePage(Constants::GENERAL_PAGE_ID, generalPage.get(), context);
    QTreeWidget *generalTree = generalPage->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(generalTree);
    QVERIFY(generalTree->topLevelItemCount() >= 8);

    project.slaves.first().deviceDescriptionId = {};
    controller.treeModel()->setProjects({project});
    const QModelIndex unreferencedSlave
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::ConfiguredSlave);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        processPage.get(),
        controller.treeModel()->contextForIndex(unreferencedSlave));
    QCOMPARE(syncManagers->model()->rowCount(), 0);
    QVERIFY(processPage->findChild<QLabel *>("EtherCATProcessDataSummary")
                ->text()
                .contains("No Process Data", Qt::CaseInsensitive));
    QVERIFY(processPage->findChild<QPushButton *>("EtherCATProcessDataRestoreDefaults")->isHidden());
}

void EtherCATWorkbenchTests::testEditableProcessDataWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("editable-process-data.xml");
    const Utils::Result<qint64> esiWritten = esiPath.writeFileContents(deviceEsi());
    QVERIFY_RESULT(esiWritten);
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0x5678;
    });
    QVERIFY(device != devices.cend());

    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    const Core::PropertyPageContext context = controller.treeModel()->contextForNodeId(file.slaveId);
    QCOMPARE(context.nodeKind, Core::WorkbenchNodeKind::ConfiguredSlave);
    controller.selectionService()->setCurrentNodeId(file.slaveId);
    DetailsView details(&controller);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::PROCESS_DATA_PAGE_ID).toString());
    QVERIFY(page);

    QTableView *syncManagers = page->findChild<QTableView *>("EtherCATProcessDataSyncManagers");
    QTableView *assignments = page->findChild<QTableView *>("EtherCATProcessDataAssignments");
    QTableView *pdoList = page->findChild<QTableView *>("EtherCATProcessDataPdoList");
    QTableView *content = page->findChild<QTableView *>("EtherCATProcessDataPdoContent");
    QTableView *image = page->findChild<QTableView *>("EtherCATProcessDataImage");
    QLabel *validation = page->findChild<QLabel *>("EtherCATProcessDataValidation");
    QPushButton *restoreDefaults = page->findChild<QPushButton *>(
        "EtherCATProcessDataRestoreDefaults");
    QVERIFY(syncManagers);
    QVERIFY(assignments);
    QVERIFY(pdoList);
    QVERIFY(content);
    QVERIFY(image);
    QVERIFY(validation);
    QVERIFY(restoreDefaults);
    QCOMPARE(syncManagers->model()->rowCount(), 2);
    QCOMPARE(assignments->model()->rowCount(), 1);
    QCOMPARE(
        assignments->model()->data(assignments->model()->index(0, 0), Qt::CheckStateRole).toInt(),
        int(Qt::Checked));
    QVERIFY(
        assignments->model()->flags(assignments->model()->index(0, 0)) & Qt::ItemIsUserCheckable);
    QVERIFY(!projectService->project(file.projectId)->modified);

    syncManagers->setCurrentIndex(syncManagers->model()->index(1, 0));
    QTRY_COMPARE(assignments->model()->rowCount(), 1);
    QTRY_COMPARE(pdoList->model()->index(0, 2).data().toString(), QString("Status"));
    QTRY_COMPARE(content->model()->index(0, 4).data().toString(), QString("Statusword"));
    const int flagsColumn = columnWithHeader(pdoList->model(), "Flags");
    const int defaultColumn = columnWithHeader(pdoList->model(), "Default");
    const int typeColumn = columnWithHeader(content->model(), "Type");
    QVERIFY(flagsColumn >= 0);
    QVERIFY(defaultColumn >= 0);
    QVERIFY(typeColumn >= 0);
    QVERIFY(pdoList->model()->index(0, flagsColumn).data().toString().contains("F"));
    QVERIFY(pdoList->model()->index(0, flagsColumn).data().toString().contains("M"));
    QCOMPARE(pdoList->model()->index(0, defaultColumn).data().toString(), QString("Yes"));
    QVERIFY(!(
        assignments->model()->flags(assignments->model()->index(0, 0)) & Qt::ItemIsUserCheckable));
    QVERIFY(!(content->model()->flags(content->model()->index(0, typeColumn)) & Qt::ItemIsEditable));
    syncManagers->setCurrentIndex(syncManagers->model()->index(0, 0));
    QTRY_COMPARE(pdoList->model()->index(0, 2).data().toString(), QString("Command"));
    QVERIFY(content->model()->flags(content->model()->index(0, typeColumn)) & Qt::ItemIsEditable);

    QVERIFY(assignments->model()
                ->setData(assignments->model()->index(0, 0), Qt::Unchecked, Qt::CheckStateRole));
    const Data::ProjectSnapshot afterAssignment = *projectService->project(file.projectId);
    QVERIFY(afterAssignment.modified);
    QCOMPARE(afterAssignment.slaves.first().processData.pdos.size(), 2);
    QVERIFY(!afterAssignment.slaves.first().processData.pdos.first().selected);
    QCOMPARE(image->model()->rowCount(), 1);
    QVERIFY(projectService->canUndoProject(file.projectId));

    const Utils::Result<> undoAssignment = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoAssignment);
    QVERIFY(projectService->project(file.projectId)->slaves.first().processData.pdos.isEmpty());
    QTRY_COMPARE(
        assignments->model()->data(assignments->model()->index(0, 0), Qt::CheckStateRole).toInt(),
        int(Qt::Checked));

    restoreDefaults->click();
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().processData.pdos.size(), 2);
    QVERIFY(projectService->canUndoProject(file.projectId));
    const Utils::Result<> undoDefaults = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoDefaults);
    QVERIFY(projectService->project(file.projectId)->slaves.first().processData.pdos.isEmpty());
    const Utils::Result<> redoDefaults = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoDefaults);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().processData.pdos.size(), 2);
    QCOMPARE(
        projectService->project(file.projectId)
            ->slaves.first()
            .processData.pdos.first()
            .entries.first()
            .requestedBitOffset,
        -1);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().processData.pdos.size(), 2);
    QCOMPARE(content->model()->rowCount(), 1);
    QCOMPARE(image->model()->rowCount(), 2);

    QSignalSpy projectChanged(projectService, &Core::ProjectService::projectChanged);

    const int bitsColumn = columnWithHeader(content->model(), "Bits");
    const int offsetColumn = columnWithHeader(content->model(), "Bit Offset");
    QVERIFY(bitsColumn >= 0);
    QVERIFY(offsetColumn >= 0);
    QVERIFY(!content->model()->setData(content->model()->index(0, bitsColumn), 8));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));
    const Data::ProjectSnapshot afterRejectedEdit = *projectService->project(file.projectId);
    QCOMPARE(afterRejectedEdit.slaves.size(), 1);
    QCOMPARE(afterRejectedEdit.slaves.first().processData.pdos.size(), 2);
    QCOMPARE(afterRejectedEdit.slaves.first().processData.pdos.first().entries.size(), 1);
    QCOMPARE(afterRejectedEdit.slaves.first().processData.pdos.first().entries.first().bitLength, 16);

    const int changedBeforeOffset = projectChanged.count();
    QVERIFY(content->model()->setData(content->model()->index(0, offsetColumn), 8));
    QCOMPARE(projectChanged.count(), changedBeforeOffset + 1);
    const Data::ProjectSnapshot afterOffsetEdit = *projectService->project(file.projectId);
    QCOMPARE(afterOffsetEdit.slaves.size(), 1);
    QCOMPARE(afterOffsetEdit.slaves.first().processData.pdos.size(), 2);
    QCOMPARE(afterOffsetEdit.slaves.first().processData.pdos.first().entries.size(), 1);
    QCOMPARE(
        afterOffsetEdit.slaves.first().processData.pdos.first().entries.first().requestedBitOffset,
        8);
    const Utils::Result<> undoOffset = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoOffset);
    const Data::ProjectSnapshot afterUndo = *projectService->project(file.projectId);
    QCOMPARE(afterUndo.slaves.size(), 1);
    QCOMPARE(afterUndo.slaves.first().processData.pdos.size(), 2);
    QCOMPARE(afterUndo.slaves.first().processData.pdos.first().entries.size(), 1);
    QCOMPARE(afterUndo.slaves.first().processData.pdos.first().entries.first().requestedBitOffset, -1);
    const Utils::Result<> redoOffset = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoOffset);
    const Data::ProjectSnapshot afterRedo = *projectService->project(file.projectId);
    QCOMPARE(afterRedo.slaves.size(), 1);
    QCOMPARE(afterRedo.slaves.first().processData.pdos.size(), 2);
    QCOMPARE(afterRedo.slaves.first().processData.pdos.first().entries.size(), 1);
    QCOMPARE(afterRedo.slaves.first().processData.pdos.first().entries.first().requestedBitOffset, 8);

    QVERIFY(content->model()->setData(
        content->model()->index(0, typeColumn), int(Data::EtherCATDataType::Integer16)));
    QCOMPARE(
        projectService->project(file.projectId)
            ->slaves.first()
            .processData.pdos.first()
            .entries.first()
            .dataType,
        Data::EtherCATDataType::Integer16);
    const Utils::Result<> undoType = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoType);
    QCOMPARE(
        projectService->project(file.projectId)
            ->slaves.first()
            .processData.pdos.first()
            .entries.first()
            .dataType,
        Data::EtherCATDataType::UnsignedInteger16);
    const Utils::Result<> redoType = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoType);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEditableStartupWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("editable-startup.xml");
    const Utils::Result<qint64> esiWritten = esiPath.writeFileContents(deviceEsi());
    QVERIFY_RESULT(esiWritten);
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0x5678;
    });
    QVERIFY(device != devices.cend());

    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    controller.selectionService()->setCurrentNodeId(file.slaveId);
    DetailsView details(&controller);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::STARTUP_PAGE_ID).toString());
    QVERIFY(page);

    QTableView *table = page->findChild<QTableView *>("EtherCATStartupTable");
    QLabel *validation = page->findChild<QLabel *>("EtherCATStartupValidation");
    QPushButton *defaults = page->findChild<QPushButton *>("EtherCATStartupRestoreDefaults");
    QPushButton *moveUp = page->findChild<QPushButton *>("EtherCATStartupMoveUp");
    QPushButton *moveDown = page->findChild<QPushButton *>("EtherCATStartupMoveDown");
    QPushButton *add = page->findChild<QPushButton *>("EtherCATStartupNew");
    QPushButton *remove = page->findChild<QPushButton *>("EtherCATStartupDelete");
    QPushButton *edit = page->findChild<QPushButton *>("EtherCATStartupEdit");
    QVERIFY(table);
    QVERIFY(validation);
    QVERIFY(defaults);
    QVERIFY(moveUp);
    QVERIFY(moveDown);
    QVERIFY(add);
    QVERIFY(remove);
    QVERIFY(edit);
    QCOMPARE(table->model()->rowCount(), 3);
    QVERIFY(!projectService->project(file.projectId)->modified);
    QCOMPARE(defaults->text(), QString("Store ESI Defaults"));

    const int enabledColumn = columnWithHeader(table->model(), "Enabled");
    const int orderColumn = columnWithHeader(table->model(), "Order");
    const int transitionColumn = columnWithHeader(table->model(), "Transition");
    const int protocolColumn = columnWithHeader(table->model(), "Protocol");
    const int indexColumn = columnWithHeader(table->model(), "Index");
    const int subindexColumn = columnWithHeader(table->model(), "Subindex");
    const int typeColumn = columnWithHeader(table->model(), "Type");
    const int dataColumn = columnWithHeader(table->model(), "Data");
    const int commentColumn = columnWithHeader(table->model(), "Comment");
    QVERIFY(enabledColumn >= 0);
    QVERIFY(orderColumn >= 0);
    QVERIFY(transitionColumn >= 0);
    QVERIFY(protocolColumn >= 0);
    QVERIFY(indexColumn >= 0);
    QVERIFY(subindexColumn >= 0);
    QVERIFY(typeColumn >= 0);
    QVERIFY(dataColumn >= 0);
    QVERIFY(commentColumn >= 0);
    QCOMPARE(table->model()->index(0, transitionColumn).data().toString(), QString("PS"));
    QCOMPARE(table->model()->index(0, protocolColumn).data().toString(), QString("CoE"));
    QCOMPARE(table->model()->index(0, indexColumn).data().toString(), QString("0x6060"));
    QCOMPARE(table->model()->index(0, subindexColumn).data().toString(), QString("0x00"));
    QCOMPARE(table->model()->index(0, dataColumn).data().toString(), QString("08"));
    QVERIFY(table->model()->flags(table->model()->index(0, commentColumn)) & Qt::ItemIsEditable);

    defaults->click();
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 3);
    QVERIFY(projectService->canUndoProject(file.projectId));
    const Utils::Result<> undoDefaults = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoDefaults);
    QVERIFY(projectService->project(file.projectId)->slaves.first().startup.parameters.isEmpty());
    const Utils::Result<> redoDefaults = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoDefaults);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 3);

    QVERIFY(table->model()->setData(
        table->model()->index(0, typeColumn), int(Data::EtherCATDataType::UnsignedInteger8)));
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().startup.parameters.first().dataType,
        Data::EtherCATDataType::UnsignedInteger8);
    const Utils::Result<> undoType = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoType);
    QVERIFY(!table->model()->setData(
        table->model()->index(0, typeColumn), int(Data::EtherCATDataType::UnsignedInteger16)));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));
    QVERIFY(!table->model()->setData(table->model()->index(0, dataColumn), QString()));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));
    QVERIFY(!table->model()->setData(table->model()->index(0, transitionColumn), QString("<PS>")));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));

    table->setCurrentIndex(table->model()->index(0, 0));
    moveDown->click();
    const Data::StartupConfiguration afterMove
        = projectService->project(file.projectId)->slaves.first().startup;
    QCOMPARE(afterMove.parameters.size(), 3);
    QCOMPARE(afterMove.parameters.at(0).order, 1);
    QCOMPARE(afterMove.parameters.at(1).order, 0);
    const Utils::Result<> undoMove = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoMove);

    QVERIFY(!table->model()->setData(table->model()->index(0, indexColumn), QString("0x0000")));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().startup.parameters.first().index,
        quint16(0x6060));
    QVERIFY(
        table->model()
            ->setData(table->model()->index(0, enabledColumn), Qt::Unchecked, Qt::CheckStateRole));
    QVERIFY(
        !projectService->project(file.projectId)->slaves.first().startup.parameters.first().enabled);
    const Utils::Result<> undoEnabled = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoEnabled);

    table->setCurrentIndex(table->model()->index(2, 0));
    QVERIFY(!(table->model()->flags(table->model()->index(2, commentColumn)) & Qt::ItemIsEditable));
    QVERIFY(!remove->isEnabled());
    QVERIFY(!edit->isEnabled());

    bool addedFromDialog = false;
    QTimer::singleShot(0, page, [&addedFromDialog] {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        addedFromDialog = true;
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogIndex")->setText("0x6073");
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogSubindex")->setText("0");
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogData")->setText("01");
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogComment")->setText("Quick stop");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    add->click();
    QVERIFY(addedFromDialog);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 4);

    table->setCurrentIndex(table->model()->index(3, 0));
    bool deletionConfirmed = false;
    QTimer::singleShot(0, page, [&deletionConfirmed] {
        QMessageBox *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        deletionConfirmed = true;
        messageBox->button(QMessageBox::Yes)->click();
    });
    remove->click();
    QVERIFY(deletionConfirmed);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 3);
    const Utils::Result<> undoDelete = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoDelete);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 4);

    table->setCurrentIndex(table->model()->index(0, 0));
    bool editedFromDialog = false;
    QTimer::singleShot(0, page, [&editedFromDialog] {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        editedFromDialog = true;
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogComment")
            ->setText("Operation mode request");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    edit->click();
    QVERIFY(editedFromDialog);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().startup.parameters.first().comment,
        QString("Operation mode request"));

    defaults->click();
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 3);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().startup.parameters.first().comment,
        QString("Mode"));

    const Utils::Result<> clearStartup
        = projectService->setStartupConfiguration(file.projectId, file.slaveId, {});
    QVERIFY_RESULT(clearStartup);
    Data::OfflineSlaveConfiguration withoutEsi
        = projectService->project(file.projectId)->slaves.first();
    withoutEsi.deviceDescriptionId = {};
    const Utils::Result<> removeEsiReference
        = projectService->replaceOfflineSlaves(file.projectId, file.masterId, {withoutEsi});
    QVERIFY_RESULT(removeEsiReference);
    QTRY_COMPARE(table->model()->rowCount(), 0);
    QVERIFY(defaults->isHidden());
    QVERIFY(add->isEnabled());
    QVERIFY(page->findChild<QLabel *>("EtherCATStartupSummary")
                ->text()
                .contains("No Startup", Qt::CaseInsensitive));

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEditableDcWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("editable-dc.xml");
    const Utils::Result<qint64> esiWritten = esiPath.writeFileContents(deviceEsi());
    QVERIFY_RESULT(esiWritten);
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0x5678;
    });
    QVERIFY(device != devices.cend());

    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    controller.selectionService()->setCurrentNodeId(file.slaveId);
    DetailsView details(&controller);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::DC_PAGE_ID).toString());
    QVERIFY(page);

    QLabel *summary = page->findChild<QLabel *>("EtherCATDcSummary");
    QLabel *validation = page->findChild<QLabel *>("EtherCATDcValidation");
    QLabel *units = page->findChild<QLabel *>("EtherCATDcUnits");
    QPushButton *defaults = page->findChild<QPushButton *>("EtherCATDcRestoreDefaults");
    QComboBox *mode = page->findChild<QComboBox *>("EtherCATDcOperationMode");
    QCheckBox *enabled = page->findChild<QCheckBox *>("EtherCATDcEnabled");
    QLineEdit *assignActivate = page->findChild<QLineEdit *>("EtherCATDcAssignActivate");
    QCheckBox *sync0Enabled = page->findChild<QCheckBox *>("EtherCATDcSync0Enabled");
    QLineEdit *sync0Cycle = page->findChild<QLineEdit *>("EtherCATDcSync0CycleNs");
    QLineEdit *sync0Shift = page->findChild<QLineEdit *>("EtherCATDcSync0ShiftNs");
    QCheckBox *sync1Enabled = page->findChild<QCheckBox *>("EtherCATDcSync1Enabled");
    QLineEdit *sync1Cycle = page->findChild<QLineEdit *>("EtherCATDcSync1CycleNs");
    QLineEdit *sync1Shift = page->findChild<QLineEdit *>("EtherCATDcSync1ShiftNs");
    QCheckBox *referenceClock = page->findChild<QCheckBox *>("EtherCATDcPotentialReferenceClock");
    QVERIFY(summary);
    QVERIFY(validation);
    QVERIFY(units);
    QVERIFY(defaults);
    QVERIFY(mode);
    QVERIFY(enabled);
    QVERIFY(assignActivate);
    QVERIFY(sync0Enabled);
    QVERIFY(sync0Cycle);
    QVERIFY(sync0Shift);
    QVERIFY(sync1Enabled);
    QVERIFY(sync1Cycle);
    QVERIFY(sync1Shift);
    QVERIFY(referenceClock);
    QVERIFY(units->text().contains("nanoseconds", Qt::CaseInsensitive));
    QVERIFY(units->text().contains("ns", Qt::CaseInsensitive));

    QCOMPARE(mode->count(), 2);
    QCOMPARE(mode->currentText(), QString("Sync0"));
    QVERIFY(enabled->isChecked());
    QCOMPARE(assignActivate->text(), QString("0x0300"));
    QVERIFY(sync0Enabled->isChecked());
    QCOMPARE(sync0Cycle->text(), QString("125000"));
    QCOMPARE(sync0Shift->text(), QString("0"));
    QVERIFY(!sync1Enabled->isChecked());
    QCOMPARE(sync1Cycle->text(), QString("0"));
    QCOMPARE(sync1Shift->text(), QString("0"));
    QVERIFY(!referenceClock->isChecked());
    QVERIFY(!projectService->project(file.projectId)->modified);
    QCOMPARE(defaults->text(), QString("Store ESI Defaults"));

    defaults->click();
    QTRY_VERIFY(projectService->project(file.projectId)->slaves.first().dc.enabled);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Sync0"));
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.assignActivate, quint16(0x0300));
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.sync0.cycleTimeNs,
        qint64(125000));
    QVERIFY(projectService->canUndoProject(file.projectId));
    const Utils::Result<> undoDefaults = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoDefaults);
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.enabled);
    const Utils::Result<> redoDefaults = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoDefaults);
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.enabled);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Sync0"));

    mode->setCurrentIndex(1);
    QMetaObject::invokeMethod(mode, "activated", Qt::DirectConnection, Q_ARG(int, 1));
    const Data::DcConfiguration selectedMode
        = projectService->project(file.projectId)->slaves.first().dc;
    QCOMPARE(selectedMode.modeName, QString("Sync0 + Sync1"));
    QCOMPARE(selectedMode.assignActivate, quint16(0x0700));
    QVERIFY(selectedMode.sync0.enabled);
    QCOMPARE(selectedMode.sync0.cycleTimeNs, qint64(250000));
    QCOMPARE(selectedMode.sync0.shiftTimeNs, qint64(-1000));
    QVERIFY(selectedMode.sync1.enabled);
    QCOMPARE(selectedMode.sync1.cycleTimeNs, qint64(500000));
    QCOMPARE(selectedMode.sync1.shiftTimeNs, qint64(1000));
    const Utils::Result<> undoSelectedMode = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoSelectedMode);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Sync0"));
    const Utils::Result<> redoSelectedMode = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoSelectedMode);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.modeName,
        QString("Sync0 + Sync1"));
    const Utils::Result<> undoSelectedModeAgain = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoSelectedModeAgain);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Sync0"));

    const auto finishEditing = [](QLineEdit *editor) {
        editor->setModified(true);
        QMetaObject::invokeMethod(editor, "editingFinished", Qt::DirectConnection);
    };

    mode->setEditText("Custom DC");
    finishEditing(mode->lineEdit());
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Custom DC"));
    const Utils::Result<> undoMode = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoMode);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Sync0"));
    const Utils::Result<> redoMode = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoMode);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Custom DC"));
    const Utils::Result<> undoModeAgain = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoModeAgain);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Sync0"));

    assignActivate->setText("0x0700");
    finishEditing(assignActivate);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.assignActivate, quint16(0x0700));
    assignActivate->setText("0x10000");
    finishEditing(assignActivate);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.assignActivate, quint16(0x0700));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));

    sync0Cycle->setText("250000");
    finishEditing(sync0Cycle);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.sync0.cycleTimeNs,
        qint64(250000));
    sync0Shift->setText("-25000");
    finishEditing(sync0Shift);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.sync0.shiftTimeNs,
        qint64(-25000));
    sync0Shift->setText("250001");
    finishEditing(sync0Shift);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.sync0.shiftTimeNs,
        qint64(-25000));
    QVERIFY(validation->text().contains("not applied", Qt::CaseInsensitive));
    sync0Cycle->setText("0");
    finishEditing(sync0Cycle);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.sync0.cycleTimeNs,
        qint64(250000));

    sync1Cycle->setText("500000");
    finishEditing(sync1Cycle);
    sync1Shift->setText("1000");
    finishEditing(sync1Shift);
    sync1Enabled->click();
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.sync1.enabled);
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.sync1.cycleTimeNs,
        qint64(500000));
    const Utils::Result<> undoSync1 = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoSync1);
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.sync1.enabled);
    const Utils::Result<> redoSync1 = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoSync1);
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.sync1.enabled);
    sync0Enabled->click();
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.sync0.enabled);
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.sync1.enabled);
    const Utils::Result<> undoSync0 = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoSync0);
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.sync0.enabled);
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.sync1.enabled);

    referenceClock->click();
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.potentialReferenceClock);
    const Utils::Result<> undoReferenceClock = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoReferenceClock);
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.potentialReferenceClock);

    enabled->click();
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.enabled);
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.sync0.enabled);
    QVERIFY(!projectService->project(file.projectId)->slaves.first().dc.sync1.enabled);
    const Utils::Result<> undoDcEnabled = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoDcEnabled);
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.enabled);

    defaults->click();
    const Data::DcConfiguration restored
        = projectService->project(file.projectId)->slaves.first().dc;
    QVERIFY(restored.enabled);
    QCOMPARE(restored.modeName, QString("Sync0"));
    QCOMPARE(restored.assignActivate, quint16(0x0300));
    QVERIFY(restored.sync0.enabled);
    QCOMPARE(restored.sync0.cycleTimeNs, qint64(125000));
    QCOMPARE(restored.sync0.shiftTimeNs, qint64(0));
    QVERIFY(!restored.sync1.enabled);
    QVERIFY(!restored.potentialReferenceClock);

    const Utils::Result<> clearDc
        = projectService->setDcConfiguration(file.projectId, file.slaveId, {});
    QVERIFY_RESULT(clearDc);
    Data::OfflineSlaveConfiguration withoutEsi
        = projectService->project(file.projectId)->slaves.first();
    withoutEsi.deviceDescriptionId = {};
    const Utils::Result<> removeEsiReference
        = projectService->replaceOfflineSlaves(file.projectId, file.masterId, {withoutEsi});
    QVERIFY_RESULT(removeEsiReference);
    QTRY_VERIFY(defaults->isHidden());
    QVERIFY(summary->text().contains("No ESI", Qt::CaseInsensitive));
    QVERIFY(mode->isEditable());
    mode->setEditText("Manual DC");
    finishEditing(mode->lineEdit());
    QCOMPARE(
        projectService->project(file.projectId)->slaves.first().dc.modeName, QString("Manual DC"));
    enabled->click();
    QVERIFY(projectService->project(file.projectId)->slaves.first().dc.enabled);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testDynamicPropertyProviderRemoval()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Dynamic Pages");
    controller.treeModel()->setProjects({project});
    controller.selectionService()->setCurrentNodeId(project.id);

    DetailsView details(&controller);
    const int baselinePages = details.tabWidget()->count();
    QVERIFY(baselinePages >= 1);

    TestPageProvider provider;
    ExtensionSystem::PluginManager::addObject(&provider);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QVERIFY(details.findChild<QWidget *>("EtherCATDynamicTestPage"));

    ExtensionSystem::PluginManager::removeObject(&provider);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages);
    QVERIFY(!details.findChild<QWidget *>("EtherCATDynamicTestPage"));
    controller.selectionService()->clear();
}

void EtherCATWorkbenchTests::testDynamicOptionalProviders()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Optional Providers");
    controller.treeModel()->setProjects({project});

    const QModelIndex master = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::Master);
    const QModelIndex diagnostics
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::Diagnostics);
    QVERIFY(master.isValid());
    QVERIFY(diagnostics.isValid());
    QCOMPARE(diagnostics.siblingAtColumn(1).data().toString(), QString("Plugin not installed"));
    QCOMPARE(
        controller.treeModel()->index(1, 1, master).data().toString(),
        QString("Scan plugin not installed"));

    BuiltinPropertyPageProvider pages(&controller);
    const Core::PropertyPageContext masterContext = controller.treeModel()->contextForIndex(master);
    QCOMPARE(pages.pages(masterContext).size(), 4);

    AvailableScanProvider scan;
    AvailableDiagnosticsProvider diagnosticsProvider;
    scan.setAvailable(true);
    diagnosticsProvider.setAvailable(true);
    ExtensionSystem::PluginManager::addObject(&scan);
    ExtensionSystem::PluginManager::addObject(&diagnosticsProvider);

    QTRY_VERIFY(controller.scanAvailable());
    QTRY_VERIFY(controller.diagnosticsAvailable());
    QCOMPARE(diagnostics.siblingAtColumn(1).data().toString(), QString("Provider available"));
    QCOMPARE(controller.treeModel()->index(1, 1, master).data().toString(), QString("Ready to scan"));
    QCOMPARE(pages.pages(masterContext).size(), 2);

    diagnosticsProvider.setAvailable(false);
    QTRY_VERIFY(!controller.diagnosticsAvailable());
    QCOMPARE(pages.pages(masterContext).size(), 4);
    diagnosticsProvider.setAvailable(true);
    QTRY_VERIFY(controller.diagnosticsAvailable());

    ExtensionSystem::PluginManager::removeObject(&diagnosticsProvider);
    ExtensionSystem::PluginManager::removeObject(&scan);
    QTRY_VERIFY(!controller.diagnosticsAvailable());
    QTRY_VERIFY(!controller.scanAvailable());
    QCOMPARE(diagnostics.siblingAtColumn(1).data().toString(), QString("Plugin not installed"));
    QCOMPARE(
        controller.treeModel()->index(1, 1, master).data().toString(),
        QString("Scan plugin not installed"));
}

} // namespace EtherCAT::Workbench::Internal
