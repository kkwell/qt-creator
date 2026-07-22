// Copyright (C) 2026 Kvell

#include "ethercatworkbenchtests.h"

#include "builtinpropertypages.h"
#include "detailsview.h"
#include "esiconfigurationfactory.h"
#include "esideviceselectiondialog.h"
#include "esirepositorypage.h"
#include "ethercatworkbenchconstants.h"
#include "generalpage.h"
#include "workbenchcontroller.h"
#include "workbenchnavigation.h"
#include "workbenchtreemodel.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/coreicons.h>
#include <coreplugin/icore.h>
#include <coreplugin/imode.h>
#include <coreplugin/modemanager.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>
#include <ethercatcore/stateservice.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <utils/filepath.h>
#include <utils/infolabel.h>
#include <utils/utilsicons.h>

#include <QAbstractButton>
#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>

#include <algorithm>
#include <functional>
#include <utility>

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

struct ProcessTreeFixture
{
    Data::ProjectSnapshot project;
    Data::NodeId slaveId;
    Data::NodeId rxPdoId;
    Data::NodeId txPdoId;
    Data::NodeId rxEntryId;
    Data::NodeId txEntryId;
    Data::NodeId unselectedEntryId;
};

static ProcessTreeFixture processTreeFixture()
{
    ProcessTreeFixture result;
    result.project = projectSnapshot("Process Tree");
    result.slaveId = Data::NodeId::create();
    result.rxPdoId = Data::NodeId::create();
    result.txPdoId = Data::NodeId::create();
    result.rxEntryId = Data::NodeId::create();
    result.txEntryId = Data::NodeId::create();
    result.unselectedEntryId = Data::NodeId::create();

    Data::ProcessDataConfiguration processData;
    processData.syncManagers = {
        {Data::NodeId::create(),
         2,
         "Outputs",
         Data::SyncManagerDirection::MasterToSlave,
         true,
         32},
        {Data::NodeId::create(),
         3,
         "Inputs",
         Data::SyncManagerDirection::SlaveToMaster,
         true,
         32},
    };
    processData.pdos = {
        {result.rxPdoId,
         0x1600,
         "Drive Command",
         Data::PdoDirection::Rx,
         2,
         true,
         false,
         false,
         true,
         true,
         "CSP",
         {{result.rxEntryId,
           0x6040,
           0,
           "Controlword",
           16,
           Data::EtherCATDataType::UnsignedInteger16,
           "UINT",
           -1,
           true,
           false}}},
        {result.txPdoId,
         0x1a00,
         "Drive Status",
         Data::PdoDirection::Tx,
         3,
         true,
         true,
         true,
         true,
         true,
         "CSP",
         {{result.txEntryId,
           0x6041,
           0,
           "Statusword",
           16,
           Data::EtherCATDataType::UnsignedInteger16,
           "UINT",
           -1,
           true,
           false}}},
        {Data::NodeId::create(),
         0x1601,
         "Optional Command",
         Data::PdoDirection::Rx,
         2,
         false,
         false,
         false,
         false,
         true,
         {},
         {{result.unselectedEntryId,
           0x6071,
           0,
           "Target torque",
           16,
           Data::EtherCATDataType::Integer16,
           "INT",
           -1,
           true,
           false}}},
    };

    const Data::NodeId master = masterId(result.project);
    result.project.slaves = {
        {result.slaveId,
         master,
         0,
         {2, 0x5678, 0x11},
         17,
         3,
         "Configured Servo",
         {},
         processData,
         {},
         {}},
    };
    result.project.nodes.append(
        {result.slaveId, master, Data::ProjectNodeKind::Slave, "Configured Servo"});
    return result;
}

struct TestProjectFile
{
    Utils::FilePath path;
    Data::NodeId projectId;
    Data::NodeId targetId;
    Data::NodeId masterId;
    Data::NodeId slaveId;
};

static TestProjectFile writeProjectWithSlave(
    const QTemporaryDir &directory,
    const Data::DeviceSummary &device,
    const QString &fileName = "process-data.ecatproject",
    const QString &projectName = "Process Data Workflow")
{
    TestProjectFile result;
    result.path = Utils::FilePath::fromString(directory.path())
                      .canonicalPath()
                      .pathAppended(fileName);
    result.projectId = Data::NodeId::create();
    result.targetId = Data::NodeId::create();
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
             {"name", projectName},
             {"createdBy", "Workbench Test"}}},
        {"target",
         QJsonObject{{"id", result.targetId.toString()}, {"name", "Offline Controller"}}},
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

static QModelIndex directChildByKind(
    const QAbstractItemModel *model,
    Core::WorkbenchNodeKind kind,
    const QModelIndex &parent)
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>() == kind)
            return index;
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

static QModelIndex findByDisplayText(
    const QAbstractItemModel *model, const QString &text, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data().toString() == text)
            return index;
        const QModelIndex child = findByDisplayText(model, text, index);
        if (child.isValid())
            return child;
    }
    return {};
}

static QModelIndex findBySourceId(
    const WorkbenchTreeModel *model,
    const Data::NodeId &sourceId,
    const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (model->sourceNodeId(index) == sourceId)
            return index;
        const QModelIndex child = findBySourceId(model, sourceId, index);
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
<Data>3412</Data><Comment>最大扭矩 / Maximum torque commissioning limit</Comment></InitCmd>
<InitCmd><Transition>&lt;PS&gt;</Transition><Index>#x8000</Index><SubIndex>1</SubIndex>
<Data>00</Data><Comment>Fixed ESI request</Comment></InitCmd></InitCmds></CoE></Mailbox>
<Dc><OpMode><Name>Sync0</Name>
<AssignActivate>#x0300</AssignActivate><CycleTimeSync0>125000</CycleTimeSync0>
</OpMode><OpMode><Name>Sync0 + Sync1</Name><AssignActivate>#x0700</AssignActivate>
<CycleTimeSync0>250000</CycleTimeSync0><ShiftTimeSync0>-1000</ShiftTimeSync0>
<CycleTimeSync1>500000</CycleTimeSync1><ShiftTimeSync1>1000</ShiftTimeSync1>
</OpMode></Dc></Device></Devices></Descriptions></EtherCATInfo>)";
}

static bool removeFirstXmlElement(
    QByteArray *xml, const QByteArray &openingPrefix, const QByteArray &closingTag)
{
    if (!xml)
        return false;
    const qsizetype start = xml->indexOf(openingPrefix);
    if (start < 0)
        return false;
    const qsizetype closingStart = xml->indexOf(closingTag, start);
    if (closingStart < 0)
        return false;
    xml->remove(start, closingStart + closingTag.size() - start);
    return true;
}

class TestPageLabel final : public QLabel
{
public:
    using QLabel::QLabel;

    std::function<void()> onHide;

private:
    void hideEvent(QHideEvent *event) final
    {
        QLabel::hideEvent(event);
        if (onHide)
            onHide();
    }
};

class TestPageProvider final : public Core::PropertyPageProvider
{
public:
    TestPageProvider(
        Utils::Id providerId,
        Utils::Id pageId,
        const QString &pageDisplayName,
        const QString &pageObjectName,
        int priority)
        : PropertyPageProvider(providerId, pageDisplayName + " provider")
        , m_pageId(pageId)
        , m_pageDisplayName(pageDisplayName)
        , m_pageObjectName(pageObjectName)
        , m_priority(priority)
    {
        setAvailable(true);
    }

    QList<Core::PropertyPageDescriptor> pages(const Core::PropertyPageContext &context) const final
    {
        if (context.nodeKind != Core::WorkbenchNodeKind::Project)
            return {};
        return {{m_pageId, m_pageDisplayName, m_priority}};
    }

    QWidget *createPage(Utils::Id pageId, QWidget *parent) final
    {
        if (pageId != m_pageId)
            return nullptr;
        auto label = new TestPageLabel(m_pageDisplayName, parent);
        label->setObjectName(m_pageObjectName);
        label->setFocusPolicy(Qt::StrongFocus);
        label->onHide = [this] {
            if (onHide) {
                const std::function<void()> callback = std::exchange(onHide, {});
                callback();
            }
        };
        return label;
    }

    void updatePage(Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context) final
    {
        if (pageId == m_pageId)
            page->setToolTip(context.nodeId.toString());
        if (onUpdate) {
            const std::function<void()> callback = onUpdate;
            onUpdate = {};
            callback();
        }
        if (onEveryUpdate)
            onEveryUpdate();
    }

    std::function<void()> onUpdate;
    std::function<void()> onEveryUpdate;
    std::function<void()> onHide;

private:
    const Utils::Id m_pageId;
    const QString m_pageDisplayName;
    const QString m_pageObjectName;
    const int m_priority;
};

class ControlledDeviceImportJob final : public Core::DeviceImportJob
{
public:
    explicit ControlledDeviceImportJob(QObject *parent)
        : Core::DeviceImportJob(parent)
    {
        setState(Core::DeviceImportState::Running);
        setProgress(1, 4);
    }

    void cancel() final
    {
        ++cancelCalls;
        setState(Core::DeviceImportState::Canceling);
        Data::DeviceImportResult result;
        result.requestedFiles = 4;
        result.canceled = true;
        finish(result);
    }

    int cancelCalls = 0;
};

class ControlledDeviceRepositoryProvider final : public Core::DeviceRepositoryProvider
{
public:
    ControlledDeviceRepositoryProvider()
        : DeviceRepositoryProvider("EtherCAT.Workbench.ControlledRepository", "Controlled")
    {
        setAvailable(true);
    }

    QList<Data::DeviceSummary> devices(const Data::DeviceFilter &) const final { return {}; }
    std::optional<Data::DeviceDescription> device(const Data::NodeId &) const final { return {}; }
    QByteArray originalXml(const Data::NodeId &) const final { return {}; }

    Core::DeviceImportJob *importFiles(const Utils::FilePaths &) final { return createJob(); }
    Core::DeviceImportJob *rebuildIndex() final { return createJob(); }
    bool isIndexing() const final
    {
        return job && job->state() != Core::DeviceImportState::Finished;
    }

    ControlledDeviceImportJob *createJob()
    {
        if (isIndexing())
            return job;
        job = new ControlledDeviceImportJob(this);
        emit indexingChanged(true);
        connect(job, &Core::DeviceImportJob::finished, this, [this] {
            emit indexingChanged(false);
        });
        return job;
    }

    ControlledDeviceImportJob *job = nullptr;
};

class AvailableScanProvider final : public Core::ScanProvider
{
public:
    explicit AvailableScanProvider(
        Utils::Id id = Utils::Id("EtherCAT.Workbench.TestScan"),
        const QString &displayName = "Local Mock test scanner")
        : ScanProvider(id, displayName)
    {}

    Data::ScanState scanState() const final { return m_state; }
    Data::ScanProgress scanProgress() const final { return m_progress; }
    std::optional<Data::ScanResult> lastScanResult() const final { return m_result; }
    QString lastScanError() const final { return m_error; }
    Utils::Result<> startScan(const Data::ScanRequest &) final { return Utils::ResultOk; }
    void cancelScan() final {}
    void clearScanResult() final {}

    void publishResult(const Data::ScanResult &result)
    {
        m_state = Data::ScanState::Completed;
        m_progress.state = m_state;
        m_result = result;
        m_error.clear();
        emit scanStateChanged(m_state);
        emit scanProgressChanged(m_progress);
        emit scanResultChanged();
    }

    void clearPublishedResult()
    {
        m_state = Data::ScanState::Idle;
        m_progress = {};
        m_result.reset();
        m_error.clear();
        emit scanStateChanged(m_state);
        emit scanProgressChanged(m_progress);
        emit scanResultChanged();
    }

private:
    Data::ScanState m_state = Data::ScanState::Idle;
    Data::ScanProgress m_progress;
    std::optional<Data::ScanResult> m_result;
    QString m_error;
};

class AvailableDiagnosticsProvider final : public Core::DiagnosticsProvider
{
public:
    explicit AvailableDiagnosticsProvider(
        Utils::Id id = Utils::Id("EtherCAT.Workbench.TestDiagnostics"),
        const QString &displayName = "Local Mock test diagnostics")
        : DiagnosticsProvider(id, displayName)
    {}

    Data::DiagnosticsStreamState streamState() const final { return m_state; }
    Data::DiagnosticsRequest activeRequest() const final { return m_request; }
    std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const final { return m_snapshot; }
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

    void publishSnapshot(const Data::DiagnosticsSnapshot &snapshot)
    {
        m_state = Data::DiagnosticsStreamState::Running;
        m_request = {snapshot.projectId, snapshot.masterId};
        m_snapshot = snapshot;
        emit streamStateChanged(m_state);
        emit diagnosticsSnapshotChanged();
    }

    void setStreamState(Data::DiagnosticsStreamState state)
    {
        m_state = state;
        emit streamStateChanged(m_state);
    }

    void beginRequest(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        Data::DiagnosticsStreamState state = Data::DiagnosticsStreamState::Starting)
    {
        m_state = state;
        m_request = {projectId, masterId};
        m_snapshot.reset();
        emit streamStateChanged(m_state);
        emit diagnosticsSnapshotChanged();
    }

private:
    Data::DiagnosticsStreamState m_state = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_request;
    std::optional<Data::DiagnosticsSnapshot> m_snapshot;
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
    QVERIFY(::Core::ActionManager::command(Constants::LOCATE_DIFFERENCE_ACTION_ID));
    QVERIFY(::Core::ActionManager::command(Constants::LOCATE_ISSUE_ACTION_ID));
    QVERIFY(::Core::ActionManager::command(Constants::OPEN_DIAGNOSTICS_ACTION_ID));
    ::Core::ActionManager::command(Constants::OPEN_ACTION_ID)->action()->trigger();
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    QVERIFY(::Core::ModeManager::currentMode());
    QTRY_VERIFY(::Core::ModeManager::currentMode()->widget());
    QCOMPARE(
        ::Core::ModeManager::currentMode()->widget()->objectName(),
        QString("EtherCATWorkbenchModeWidget"));
}

void EtherCATWorkbenchTests::testModeCommandStripMirrorsRegisteredActions()
{
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    QWidget *modeWidget = ::Core::ModeManager::currentMode()->widget();
    QVERIFY(modeWidget);

    QToolBar *commandStrip
        = modeWidget->findChild<QToolBar *>("EtherCATWorkbenchCommandStrip");
    QVERIFY(commandStrip);
    QCOMPARE(commandStrip->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QVERIFY(!commandStrip->accessibleName().isEmpty());
    QVERIFY(!commandStrip->accessibleDescription().isEmpty());
    const int expectedIconSize
        = commandStrip->style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, commandStrip);
    QCOMPARE(commandStrip->iconSize(), QSize(expectedIconSize, expectedIconSize));

    ::Core::ActionContainer *menu
        = ::Core::ActionManager::actionContainer(Constants::MENU_ID);
    QVERIFY(menu);
    QVERIFY(menu->menu());
    QAction *openAction
        = ::Core::ActionManager::command(Constants::OPEN_ACTION_ID)->action();
    const QList<QAction *> menuActions = menu->menu()->actions();
    const QList<QAction *> commandActions = commandStrip->actions();
    QVERIFY(!commandActions.contains(openAction));
    QCOMPARE(commandActions.size(), menuActions.size() - 1);
    for (QAction *action : menuActions) {
        if (action != openAction)
            QVERIFY(commandActions.contains(action));
    }
    for (QAction *action : commandActions) {
        QVERIFY(menuActions.contains(action));
        if (!action->isSeparator())
            QVERIFY(!action->toolTip().isEmpty());
    }

    for (const Utils::Id id :
         {Utils::Id(Constants::REFRESH_ACTION_ID),
          Utils::Id(Constants::EXPAND_ACTION_ID),
          Utils::Id(Constants::COLLAPSE_ACTION_ID),
          Utils::Id(Constants::LOCATE_DIFFERENCE_ACTION_ID),
          Utils::Id(Constants::LOCATE_ISSUE_ACTION_ID),
          Utils::Id(Constants::OPEN_DIAGNOSTICS_ACTION_ID)}) {
        ::Core::Command *command = ::Core::ActionManager::command(id);
        QVERIFY(command);
        QVERIFY(commandActions.contains(command->action()));
        QVERIFY(!command->action()->icon().isNull());
    }
    for (const Utils::Id id :
         {Utils::Id(Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID),
          Utils::Id(Constants::COPY_NODE_ID_ACTION_ID),
          Utils::Id(Constants::SET_ACTIVE_PROJECT_ACTION_ID),
          Utils::Id(Constants::ADD_DEVICE_TO_MASTER_ACTION_ID),
          Utils::Id(Constants::REMOVE_OFFLINE_SLAVE_ACTION_ID),
          Utils::Id(Constants::MOVE_OFFLINE_SLAVE_UP_ACTION_ID),
          Utils::Id(Constants::MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID)}) {
        ::Core::Command *command = ::Core::ActionManager::command(id);
        QVERIFY(command);
        QVERIFY(!commandActions.contains(command->action()));
    }

    QAction transientAction("Transient engineering command");
    menu->menu()->addAction(&transientAction);
    QTRY_VERIFY(commandStrip->actions().contains(&transientAction));
    menu->menu()->removeAction(&transientAction);
    QTRY_VERIFY(!commandStrip->actions().contains(&transientAction));
}

void EtherCATWorkbenchTests::testNavigationCommandsUseActionManager()
{
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    ::Core::Command *expandCommand = ::Core::ActionManager::command(Constants::EXPAND_ACTION_ID);
    ::Core::Command *collapseCommand = ::Core::ActionManager::command(Constants::COLLAPSE_ACTION_ID);
    ::Core::Command *unsupportedCommand = ::Core::ActionManager::command(
        Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID);
    ::Core::Command *copyCommand = ::Core::ActionManager::command(Constants::COPY_NODE_ID_ACTION_ID);
    QVERIFY(expandCommand);
    QVERIFY(collapseCommand);
    QVERIFY(unsupportedCommand);
    QVERIFY(copyCommand);
    QVERIFY(!unsupportedCommand->action()->icon().isNull());
    QVERIFY(!copyCommand->action()->icon().isNull());

    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Navigation commands");
    QList<Data::DeviceSummary> devices = deviceSummaries(2);
    devices.first().supported = false;
    controller.treeModel()->setProjects({project});
    controller.treeModel()->syncDevices(devices);

    WorkbenchNavigationFactory factory(&controller);
    const ::Core::NavigationView view = factory.createWidget();
    auto navigation = qobject_cast<WorkbenchNavigationWidget *>(view.widget);
    QVERIFY(navigation);
    QCOMPARE(view.dockToolBarWidgets.size(), 2);
    QCOMPARE(view.dockToolBarWidgets.at(0)->defaultAction(), expandCommand->action());
    QCOMPARE(view.dockToolBarWidgets.at(1)->defaultAction(), collapseCommand->action());

    emit controller.locateUnsupportedDeviceRequested();
    QTRY_COMPARE(
        navigation->treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        devices.first().id);
    QApplication::clipboard()->clear();
    emit controller.copyCurrentNodeIdRequested();
    QTRY_COMPARE(QApplication::clipboard()->text(), devices.first().id.toString());

    navigation->resize(900, 600);
    navigation->show();
    QTRY_VERIFY(navigation->isVisible());
    QList<QAction *> popupActions;
    bool popupSeen = false;
    QTimer::singleShot(0, navigation, [&popupActions, &popupSeen] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        popupSeen = true;
        popupActions = popup->actions();
        popup->close();
    });
    emit navigation->treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(popupSeen);
    QVERIFY(unsupportedCommand->action()->isEnabled());
    QVERIFY(copyCommand->action()->isEnabled());
    for (const Utils::Id id :
         {Utils::Id(Constants::EXPAND_ACTION_ID),
          Utils::Id(Constants::COLLAPSE_ACTION_ID),
          Utils::Id(Constants::LOCATE_DIFFERENCE_ACTION_ID),
          Utils::Id(Constants::LOCATE_ISSUE_ACTION_ID),
          Utils::Id(Constants::OPEN_DIAGNOSTICS_ACTION_ID),
          Utils::Id(Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID),
          Utils::Id(Constants::COPY_NODE_ID_ACTION_ID)}) {
        ::Core::Command *command = ::Core::ActionManager::command(id);
        QVERIFY(command);
        QVERIFY(popupActions.contains(command->action()));
    }

    const QModelIndex placeholder
        = findByKind(navigation->treeView()->model(), Core::WorkbenchNodeKind::Placeholder);
    QVERIFY(placeholder.isValid());
    navigation->treeView()->setCurrentIndex(placeholder);
    QList<QAction *> placeholderActions;
    bool placeholderPopupSeen = false;
    QTimer::singleShot(0, navigation, [&placeholderActions, &placeholderPopupSeen] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        placeholderPopupSeen = true;
        placeholderActions = popup->actions();
        popup->close();
    });
    emit navigation->treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(placeholderPopupSeen);
    QVERIFY(!placeholderActions.contains(copyCommand->action()));
    QVERIFY(copyCommand->action()->isEnabled());
    QCOMPARE(
        navigation->treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        devices.first().id);

    qDeleteAll(view.dockToolBarWidgets);
    delete navigation;
    controller.selectionService()->clear();
}

void EtherCATWorkbenchTests::testOfflineTopologyEditingWorkflow()
{
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));

    ::Core::Command *addCommand = ::Core::ActionManager::command(
        Constants::ADD_DEVICE_TO_MASTER_ACTION_ID);
    ::Core::Command *removeCommand = ::Core::ActionManager::command(
        Constants::REMOVE_OFFLINE_SLAVE_ACTION_ID);
    ::Core::Command *moveUpCommand = ::Core::ActionManager::command(
        Constants::MOVE_OFFLINE_SLAVE_UP_ACTION_ID);
    ::Core::Command *moveDownCommand = ::Core::ActionManager::command(
        Constants::MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID);
    ::Core::Command *openDiagnosticsCommand = ::Core::ActionManager::command(
        Constants::OPEN_DIAGNOSTICS_ACTION_ID);
    QVERIFY(addCommand);
    QVERIFY(removeCommand);
    QVERIFY(moveUpCommand);
    QVERIFY(moveDownCommand);
    QVERIFY(openDiagnosticsCommand);
    for (::Core::Command *command : {addCommand, removeCommand, moveUpCommand, moveDownCommand})
        QVERIFY(!command->action()->icon().isNull());

    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());
    QTRY_COMPARE(addCommand->action()->text(), QString("Add to Active Offline Master"));
    QVERIFY(addCommand->action()->toolTip().contains(
        "Activate a valid offline EtherCAT project"));
    QVERIFY(!addCommand->action()->isEnabled());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("offline-topology.xml");
    const Utils::Result<qint64> esiWritten = esiPath.writeFileContents(deviceEsi());
    QVERIFY_RESULT(esiWritten);
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0x5678;
    });
    QVERIFY(device != devices.cend());
    QVERIFY(device->supported);

    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    const TestProjectFile alternate = writeProjectWithSlave(
        directory,
        *device,
        "secondary-offline-topology.ecatproject",
        "Secondary EtherCAT Project");
    QVERIFY(!file.path.isEmpty());
    QVERIFY(!alternate.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    const ProjectExplorer::OpenProjectResult alternateOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(alternate.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QVERIFY2(alternateOpened, qPrintable(alternateOpened.errorMessage()));
    auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (projectService->project(alternate.projectId))
            ProjectExplorer::ProjectManager::removeProject(alternateOpened.project());
        if (projectService->project(file.projectId))
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(projectService->project(alternate.projectId).has_value());
    const Utils::Result<> activated = projectService->activateProject(file.projectId);
    QVERIFY_RESULT(activated);
    QTRY_COMPARE(projectService->activeProjectId(), file.projectId);
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(device->id).isValid());

    const QString expectedQuickAddText
        = "Add to \"Process Data Workflow\" / \"EtherCAT Master\"";
    QTRY_COMPARE(addCommand->action()->text(), expectedQuickAddText);
    QVERIFY(addCommand->hasAttribute(::Core::Command::CA_UpdateText));
    QVERIFY(addCommand->action()->toolTip().contains("EtherCAT Master"));
    QVERIFY(addCommand->action()->toolTip().contains("Process Data Workflow"));
    QVERIFY(addCommand->action()->toolTip().contains("local offline project"));
    QVERIFY(addCommand->action()->toolTip().contains("no controller or hardware"));
    QCOMPARE(addCommand->action()->statusTip(), addCommand->action()->toolTip());

    controller.selectionService()->setCurrentNodeId(device->id);
    QTRY_VERIFY(addCommand->action()->isEnabled());
    QVERIFY(!removeCommand->action()->isEnabled());
    const Data::ProjectSnapshot primaryBeforeQuickAdd = *projectService->project(file.projectId);
    QVERIFY_RESULT(controller.renameStructuralNode(
        alternate.projectId, alternate.masterId, "Secondary Master"));
    const std::optional<OfflineMasterTarget> primaryTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(primaryTarget);
    QCOMPARE(primaryTarget->projectId, file.projectId);
    QCOMPARE(primaryTarget->masterId, file.masterId);

    QVERIFY_RESULT(projectService->activateProject(alternate.projectId));
    QTRY_COMPARE(
        addCommand->action()->text(),
        QString(
            "Add to \"Secondary EtherCAT Project\" / \"Secondary Master\""));
    const QString placeholderProject = "Secondary %1 & Project";
    const QString placeholderMaster = "Master %2 & Offline";
    const QString placeholderQuickAddText
        = "Add to \"Secondary %1 && Project\" / \"Master %2 && Offline\"";
    QVERIFY_RESULT(controller.renameProject(alternate.projectId, placeholderProject));
    QVERIFY_RESULT(controller.renameStructuralNode(
        alternate.projectId, alternate.masterId, placeholderMaster));
    QTRY_COMPARE(addCommand->action()->text(), placeholderQuickAddText);
    QVERIFY(addCommand->action()->toolTip().contains(placeholderProject));
    QVERIFY(addCommand->action()->toolTip().contains(placeholderMaster));
    QVERIFY(!addCommand->action()->toolTip().contains("&&"));
    QCOMPARE(addCommand->action()->statusTip(), addCommand->action()->toolTip());
    const Data::ProjectSnapshot secondaryBeforeQuickAdd
        = *projectService->project(alternate.projectId);
    const Utils::Result<> staleQuickAdd = controller.addSelectedDeviceToMaster(*primaryTarget);
    QVERIFY(!staleQuickAdd);
    QVERIFY(staleQuickAdd.error().contains("no longer current"));
    QCOMPARE(*projectService->project(file.projectId), primaryBeforeQuickAdd);
    QCOMPARE(*projectService->project(alternate.projectId), secondaryBeforeQuickAdd);
    QTRY_VERIFY(addCommand->action()->isEnabled());
    addCommand->action()->trigger();
    QTRY_COMPARE(projectService->project(alternate.projectId)->slaves.size(), 2);
    QCOMPARE(*projectService->project(file.projectId), primaryBeforeQuickAdd);
    QVERIFY_RESULT(projectService->undoProject(alternate.projectId));
    QTRY_COMPARE(
        projectService->project(alternate.projectId)->slaves,
        secondaryBeforeQuickAdd.slaves);
    QCOMPARE(projectService->project(alternate.projectId)->name, secondaryBeforeQuickAdd.name);

    QVERIFY_RESULT(projectService->activateProject(file.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), file.projectId);
    controller.selectionService()->setCurrentNodeId(device->id);
    QTRY_COMPARE(addCommand->action()->text(), expectedQuickAddText);
    QTRY_VERIFY(addCommand->action()->isEnabled());

    addCommand->action()->trigger();
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);
    const Data::ProjectSnapshot afterAdd = *projectService->project(file.projectId);
    const auto added
        = std::find_if(afterAdd.slaves.cbegin(), afterAdd.slaves.cend(), [&file](const auto &slave) {
              return slave.id != file.slaveId;
          });
    QVERIFY(added != afterAdd.slaves.cend());
    const Data::NodeId addedId = added->id;
    QCOMPARE(added->masterId, file.masterId);
    QCOMPARE(added->position, 1);
    QCOMPARE(added->name, QString("Workbench Servo"));
    QCOMPARE(added->identity, device->identity);
    QCOMPARE(added->deviceDescriptionId, device->id);
    QCOMPARE(added->processData.pdos.size(), 2);
    QCOMPARE(added->startup.parameters.size(), 3);
    QVERIFY(added->dc.enabled);
    QCOMPARE(controller.selectionService()->currentNodeId(), addedId);
    QVERIFY(projectService->canUndoProject(file.projectId));

    const Utils::Result<> firstUndo = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(firstUndo);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 1);
    const Utils::Result<> firstRedo = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(firstRedo);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    controller.selectionService()->setCurrentNodeId(device->id);
    QTRY_VERIFY(addCommand->action()->isEnabled());
    addCommand->action()->trigger();
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 3);
    const Data::ProjectSnapshot afterRepeatedAdd = *projectService->project(file.projectId);
    const auto repeated = std::find_if(
        afterRepeatedAdd.slaves.cbegin(),
        afterRepeatedAdd.slaves.cend(),
        [&file, &addedId](const auto &slave) {
            return slave.id != file.slaveId && slave.id != addedId;
        });
    QVERIFY(repeated != afterRepeatedAdd.slaves.cend());
    QCOMPARE(repeated->name, QString("Workbench Servo (2)"));
    QCOMPARE(repeated->position, 2);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(900, 600);
    navigation.show();
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(navigation.isVisible());
    QTRY_VERIFY(navigation.treeView()->hasFocus());
    QTRY_COMPARE(::Core::ICore::currentContextWidget(), static_cast<QWidget *>(&navigation));

    const auto closeVisibleNavigationMenus = [&navigation] {
        const QList<QMenu *> menus = navigation.findChildren<QMenu *>();
        for (QMenu *menu : menus) {
            if (menu->isVisible())
                menu->close();
        }
    };

    controller.selectionService()->setCurrentNodeId(addedId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        addedId);
    const Data::NodeId diagnosticsNodeId
        = controller.treeModel()
              ->diagnosticsForProject(file.projectId)
              .data(WorkbenchTreeModel::NodeIdRole)
              .value<Data::NodeId>();
    QVERIFY(!diagnosticsNodeId.isNull());
    QTRY_VERIFY(openDiagnosticsCommand->action()->isEnabled());
    QObject::connect(
        openDiagnosticsCommand->action(),
        &QAction::triggered,
        &navigation,
        &WorkbenchNavigationWidget::openDiagnostics,
        Qt::SingleShotConnection);
    bool selectionActionPopupSeen = false;
    bool selectionActionPresent = false;
    bool selectionActionEnabled = false;
    bool selectionActionClosedPopup = false;
    QTimer selectionActionWatchdog;
    selectionActionWatchdog.setSingleShot(true);
    QObject::connect(
        &selectionActionWatchdog,
        &QTimer::timeout,
        &navigation,
        closeVisibleNavigationMenus);
    selectionActionWatchdog.start(1000);
    QTimer::singleShot(0, &navigation, [&] {
        QPointer<QMenu> popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        selectionActionWatchdog.stop();
        selectionActionPopupSeen = true;
        selectionActionPresent = popup->actions().contains(openDiagnosticsCommand->action());
        selectionActionEnabled = openDiagnosticsCommand->action()->isEnabled();
        openDiagnosticsCommand->action()->trigger();
        selectionActionClosedPopup = !popup->isVisible();
        if (popup->isVisible())
            popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(selectionActionPopupSeen);
    QVERIFY(selectionActionPresent);
    QVERIFY(selectionActionEnabled);
    QVERIFY(selectionActionClosedPopup);
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), diagnosticsNodeId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        diagnosticsNodeId);

    controller.selectionService()->setCurrentNodeId(addedId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        addedId);
    const Data::ProjectSnapshot beforeContextDrift = *projectService->project(file.projectId);
    const bool couldUndoBeforeContextDrift = projectService->canUndoProject(file.projectId);
    const bool couldRedoBeforeContextDrift = projectService->canRedoProject(file.projectId);
    bool driftPopupSeen = false;
    QTRY_VERIFY(moveDownCommand->action()->isEnabled());
    bool moveDownPresent = false;
    bool moveDownEnabledAfterDrift = false;
    bool driftClosedPopup = false;
    QTimer driftPopupWatchdog;
    driftPopupWatchdog.setSingleShot(true);
    QObject::connect(
        &driftPopupWatchdog,
        &QTimer::timeout,
        &navigation,
        closeVisibleNavigationMenus);
    driftPopupWatchdog.start(1000);
    QTimer::singleShot(0, &navigation, [&] {
        QPointer<QMenu> popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        driftPopupWatchdog.stop();
        driftPopupSeen = true;
        moveDownPresent = popup->actions().contains(moveDownCommand->action());
        controller.selectionService()->setCurrentNodeId(file.slaveId);
        driftClosedPopup = !popup->isVisible();
        moveDownEnabledAfterDrift = moveDownCommand->action()->isEnabled();
        if (popup->isVisible()) {
            moveDownCommand->action()->trigger();
            popup->close();
        }
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(driftPopupSeen);
    QVERIFY(moveDownPresent);
    QVERIFY(moveDownEnabledAfterDrift);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.slaveId);
    QCOMPARE(*projectService->project(file.projectId), beforeContextDrift);
    QCOMPARE(projectService->canUndoProject(file.projectId), couldUndoBeforeContextDrift);
    QCOMPARE(projectService->canRedoProject(file.projectId), couldRedoBeforeContextDrift);
    QVERIFY2(
        driftClosedPopup,
        "A Workbench context menu must close when the stable selection changes.");

    const Utils::Result<> repeatedAddUndo = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(repeatedAddUndo);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    controller.selectionService()->setCurrentNodeId(addedId);
    QTRY_VERIFY(moveUpCommand->action()->isEnabled());
    QVERIFY(!moveDownCommand->action()->isEnabled());

    moveUpCommand->action()->trigger();
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().id, addedId);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().position, 0);
    const QModelIndex masterAfterMoveUp = controller.treeModel()->indexForNodeId(file.masterId);
    QVERIFY(masterAfterMoveUp.isValid());
    QTRY_COMPARE(
        controller.treeModel()
            ->index(0, 0, masterAfterMoveUp)
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        addedId);
    QCOMPARE(controller.selectionService()->currentNodeId(), addedId);
    QVERIFY(!moveUpCommand->action()->isEnabled());
    QVERIFY(moveDownCommand->action()->isEnabled());

    moveDownCommand->action()->trigger();
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.last().id, addedId);
    QCOMPARE(projectService->project(file.projectId)->slaves.last().position, 1);
    const QModelIndex masterAfterMoveDown = controller.treeModel()->indexForNodeId(file.masterId);
    QVERIFY(masterAfterMoveDown.isValid());
    QTRY_COMPARE(
        controller.treeModel()
            ->index(1, 0, masterAfterMoveDown)
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        addedId);
    QCOMPARE(controller.selectionService()->currentNodeId(), addedId);

    const QString removalName = "Workbench %1 %2 Servo";
    const Utils::Result<> renamedForRemoval
        = controller.renameOfflineSlave(file.projectId, addedId, removalName);
    QVERIFY_RESULT(renamedForRemoval);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.last().name, removalName);

    controller.selectionService()->clear();
    controller.selectionService()->setCurrentNodeId(addedId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        addedId);

    const Data::ProjectSnapshot beforeRemoval = *projectService->project(file.projectId);
    const bool couldUndoBeforeRemoval = projectService->canUndoProject(file.projectId);
    const bool couldRedoBeforeRemoval = projectService->canRedoProject(file.projectId);
    QSignalSpy projectChanges(projectService, &Core::ProjectService::projectChanged);
    bool cancelPromptSeen = false;
    QString cancelPromptTitle;
    QString cancelPromptText;
    QString cancelPromptObjectName;
    bool cancelPromptIsPlainText = false;
    bool cancelDefaultIsNo = false;
    bool cancelEscapeIsNo = false;
    bool cancellationRenderSaved = qEnvironmentVariableIsEmpty(
        "ETHERCAT_WORKBENCH_REMOVE_CONFIRM_RENDER_PATH");
    QTimer cancelTimer;
    cancelTimer.setSingleShot(true);
    connect(&cancelTimer, &QTimer::timeout, removeCommand->action(), [&] {
        QMessageBox *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        cancelPromptSeen = true;
        cancelPromptTitle = messageBox->windowTitle();
        cancelPromptText = messageBox->text() + '\n' + messageBox->informativeText();
        cancelPromptObjectName = messageBox->objectName();
        cancelPromptIsPlainText = messageBox->textFormat() == Qt::PlainText;
        QAbstractButton *no = messageBox->button(QMessageBox::No);
        cancelDefaultIsNo = messageBox->defaultButton() == no;
        cancelEscapeIsNo = messageBox->escapeButton() == no;
        const QString renderPath
            = qEnvironmentVariable("ETHERCAT_WORKBENCH_REMOVE_CONFIRM_RENDER_PATH");
        if (!renderPath.isEmpty())
            cancellationRenderSaved = messageBox->grab().save(renderPath);
        if (no)
            no->click();
    });
    cancelTimer.start(0);
    removeCommand->action()->trigger();
    QTRY_VERIFY2(cancelPromptSeen, "Removing an offline slave must ask for confirmation first.");
    QVERIFY(cancellationRenderSaved);
    QVERIFY(cancelPromptTitle.isEmpty() || cancelPromptTitle == QString("Remove Offline Slave"));
    QCOMPARE(cancelPromptObjectName, QString("EtherCATOfflineSlaveRemovalConfirmation"));
    QVERIFY(cancelPromptIsPlainText);
    QVERIFY(cancelPromptText.contains(removalName));
    QVERIFY(cancelPromptText.contains(
        QString("(position %1)").arg(beforeRemoval.slaves.last().position + 1)));
    QVERIFY(cancelPromptText.contains("Process Data"));
    QVERIFY(cancelPromptText.contains("Startup"));
    QVERIFY(cancelPromptText.contains("Distributed Clocks"));
    QVERIFY(cancelPromptText.contains("undo", Qt::CaseInsensitive));
    QVERIFY(cancelDefaultIsNo);
    QVERIFY(cancelEscapeIsNo);
    QCOMPARE(removeCommand->action()->text(), QString("Remove from Offline Master..."));
    QVERIFY(removeCommand->action()->toolTip().contains("Process Data"));
    QVERIFY(removeCommand->action()->statusTip().contains("Distributed Clocks"));
    QVERIFY(*projectService->project(file.projectId) == beforeRemoval);
    QCOMPARE(controller.selectionService()->currentNodeId(), addedId);
    QCOMPARE(projectService->canUndoProject(file.projectId), couldUndoBeforeRemoval);
    QCOMPARE(projectService->canRedoProject(file.projectId), couldRedoBeforeRemoval);
    QCOMPARE(projectChanges.count(), 0);

    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(removeCommand->action()->isEnabled());
    bool changedSelectionPromptSeen = false;
    QTimer changedSelectionTimer;
    changedSelectionTimer.setSingleShot(true);
    connect(&changedSelectionTimer, &QTimer::timeout, removeCommand->action(), [&] {
        QMessageBox *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        changedSelectionPromptSeen = true;
        controller.selectionService()->setCurrentNodeId(file.slaveId);
        if (QAbstractButton *yes = messageBox->button(QMessageBox::Yes))
            yes->click();
    });
    changedSelectionTimer.start(0);
    removeCommand->action()->trigger();
    QTRY_VERIFY(changedSelectionPromptSeen);
    QVERIFY(*projectService->project(file.projectId) == beforeRemoval);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.slaveId);
    QCOMPARE(projectChanges.count(), 0);

    controller.selectionService()->setCurrentNodeId(addedId);
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(removeCommand->action()->isEnabled());
    bool confirmationPromptSeen = false;
    QTimer confirmationTimer;
    confirmationTimer.setSingleShot(true);
    connect(&confirmationTimer, &QTimer::timeout, removeCommand->action(), [&] {
        QMessageBox *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        confirmationPromptSeen = true;
        if (QAbstractButton *yes = messageBox->button(QMessageBox::Yes))
            yes->click();
    });
    confirmationTimer.start(0);
    removeCommand->action()->trigger();
    QTRY_VERIFY(confirmationPromptSeen);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 1);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().id, file.slaveId);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().position, 0);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.slaveId);
    const Utils::Result<> removeUndo = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(removeUndo);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);
    QCOMPARE(projectService->project(file.projectId)->slaves, beforeRemoval.slaves);
    const Utils::Result<> removeRedo = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(removeRedo);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 1);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().id, file.slaveId);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().position, 0);
    const Utils::Result<> removeRedoUndo = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(removeRedoUndo);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves, beforeRemoval.slaves);

    controller.selectionService()->setCurrentNodeId(device->id);
    QList<QAction *> deviceMenuActions;
    bool quickAddRenderSaved
        = qEnvironmentVariableIsEmpty("ETHERCAT_WORKBENCH_QUICK_ADD_RENDER_PATH");
    QTimer::singleShot(0, &navigation, [&deviceMenuActions, &quickAddRenderSaved] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        deviceMenuActions = popup->actions();
        const QString renderPath
            = qEnvironmentVariable("ETHERCAT_WORKBENCH_QUICK_ADD_RENDER_PATH");
        if (!renderPath.isEmpty())
            quickAddRenderSaved = popup->grab().save(renderPath);
        popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(quickAddRenderSaved);
    QVERIFY(deviceMenuActions.contains(addCommand->action()));
    QCOMPARE(addCommand->action()->text(), expectedQuickAddText);
    QVERIFY(!deviceMenuActions.contains(removeCommand->action()));

    controller.selectionService()->setCurrentNodeId(addedId);
    QList<QAction *> slaveMenuActions;
    QTimer::singleShot(0, &navigation, [&slaveMenuActions] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        slaveMenuActions = popup->actions();
        popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(!slaveMenuActions.contains(addCommand->action()));
    QVERIFY(slaveMenuActions.contains(removeCommand->action()));
    QVERIFY(slaveMenuActions.contains(moveUpCommand->action()));
    QVERIFY(slaveMenuActions.contains(moveDownCommand->action()));

    controller.selectionService()->setCurrentNodeId(addedId);
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(removeCommand->action()->isEnabled());
    bool projectClosePromptSeen = false;
    QTimer projectCloseTimer;
    projectCloseTimer.setSingleShot(true);
    connect(&projectCloseTimer, &QTimer::timeout, removeCommand->action(), [&] {
        QMessageBox *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        projectClosePromptSeen = true;
        ProjectExplorer::ProjectManager::removeProject(opened.project());
        if (QAbstractButton *yes = messageBox->button(QMessageBox::Yes))
            yes->click();
    });
    projectCloseTimer.start(0);
    removeCommand->action()->trigger();
    QTRY_VERIFY(projectClosePromptSeen);
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(controller.selectionService()->currentNodeId() != addedId);
    QTRY_VERIFY(!controller.treeModel()->indexForNodeId(addedId).isValid());
    QTRY_COMPARE(projectService->activeProjectId(), alternate.projectId);
    const std::optional<OfflineMasterTarget> fallbackQuickAddTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(fallbackQuickAddTarget);
    QCOMPARE(fallbackQuickAddTarget->projectName, placeholderProject);
    QTRY_VERIFY(addCommand->action()->text().contains("Secondary %1 && Project"));
    QVERIFY(addCommand->action()->toolTip().contains(fallbackQuickAddTarget->projectName));
    QVERIFY(addCommand->action()->toolTip().contains(fallbackQuickAddTarget->masterName));
    QVERIFY(!addCommand->action()->isEnabled());

    ProjectExplorer::ProjectManager::removeProject(alternateOpened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(projectService->projects().isEmpty());
    QTRY_VERIFY(projectService->activeProjectId().isNull());
    QTRY_COMPARE(addCommand->action()->text(), QString("Add to Active Offline Master"));
    QVERIFY(addCommand->action()->toolTip().contains(
        "Activate a valid offline EtherCAT project"));
    QVERIFY(!addCommand->action()->toolTip().contains(placeholderProject));
    QVERIFY(!addCommand->action()->isEnabled());
    projectCleanup.dismiss();
}

void EtherCATWorkbenchTests::testConfiguredSlaveTreePhysicalOrder()
{
    WorkbenchController controller;
    QAbstractItemModelTester modelTester(
        controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    controller.selectionService()->clear();
    const QScopeGuard selectionCleanup([&controller] {
        controller.selectionService()->clear();
    });

    Data::ProjectSnapshot project = projectSnapshot("Configured physical order");
    const Data::NodeId master = masterId(project);
    const Data::NodeId physicalFirstId = Data::NodeId::create();
    const Data::NodeId physicalSecondId = Data::NodeId::create();
    project.slaves = {
        {physicalFirstId,
         master,
         0,
         {2, 0x71000001, 1},
         11,
         0,
         "Zulu Physical First",
         {},
         {},
         {},
         {}},
        {physicalSecondId,
         master,
         1,
         {2, 0x71000002, 1},
         12,
         0,
         "Alpha Physical Second",
         {},
         {},
         {},
         {}},
    };
    project.nodes.append(
        {physicalFirstId, master, Data::ProjectNodeKind::Slave, "Zulu Physical First"});
    project.nodes.append(
        {physicalSecondId, master, Data::ProjectNodeKind::Slave, "Alpha Physical Second"});
    controller.treeModel()->setProjects({project});

    const auto configuredChildIds = [](QAbstractItemModel *model,
                                       const Data::NodeId &masterId) {
        QList<Data::NodeId> result;
        const QModelIndex masterIndex = findById(model, masterId);
        if (!masterIndex.isValid())
            return result;
        for (int row = 0; row < model->rowCount(masterIndex); ++row) {
            const QModelIndex child = model->index(row, 0, masterIndex);
            if (child.data(WorkbenchTreeModel::NodeKindRole)
                    .value<Core::WorkbenchNodeKind>()
                == Core::WorkbenchNodeKind::ConfiguredSlave) {
                result.append(
                    child.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>());
            }
        }
        return result;
    };
    const auto configuredChildNames = [](QAbstractItemModel *model,
                                         const Data::NodeId &masterId) {
        QStringList result;
        const QModelIndex masterIndex = findById(model, masterId);
        if (!masterIndex.isValid())
            return result;
        for (int row = 0; row < model->rowCount(masterIndex); ++row) {
            const QModelIndex child = model->index(row, 0, masterIndex);
            if (child.data(WorkbenchTreeModel::NodeKindRole)
                    .value<Core::WorkbenchNodeKind>()
                == Core::WorkbenchNodeKind::ConfiguredSlave) {
                result.append(child.data(Qt::DisplayRole).toString());
            }
        }
        return result;
    };
    const QList<Data::NodeId> initialPhysicalOrder{physicalFirstId, physicalSecondId};
    QCOMPARE(
        configuredChildNames(controller.treeModel(), master),
        QStringList({"Zulu Physical First", "Alpha Physical Second"}));
    QCOMPARE(configuredChildIds(controller.treeModel(), master), initialPhysicalOrder);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(720, 480);
    navigation.show();
    QTRY_VERIFY(navigation.isVisible());
    QAbstractItemModel *proxyModel = navigation.treeView()->model();
    QCOMPARE(configuredChildIds(proxyModel, master), initialPhysicalOrder);

    controller.selectionService()->setCurrentNodeId(physicalFirstId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        physicalFirstId);

    navigation.filterEdit()->setText("Physical");
    QTRY_COMPARE(configuredChildIds(proxyModel, master), initialPhysicalOrder);

    Data::ProjectSnapshot renamed = project;
    for (Data::OfflineSlaveConfiguration &slave : renamed.slaves) {
        if (slave.id == physicalFirstId)
            slave.name = "Aardvark Physical First";
    }
    for (Data::ProjectNodeSnapshot &node : renamed.nodes) {
        if (node.id == physicalFirstId)
            node.name = "Aardvark Physical First";
    }
    controller.treeModel()->setProjects({renamed});
    QTRY_COMPARE(configuredChildIds(controller.treeModel(), master), initialPhysicalOrder);
    QTRY_COMPARE(configuredChildIds(proxyModel, master), initialPhysicalOrder);
    QCOMPARE(navigation.filterEdit()->text(), QString("Physical"));
    QCOMPARE(controller.selectionService()->currentNodeId(), physicalFirstId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        physicalFirstId);

    Data::ProjectSnapshot reordered = renamed;
    for (Data::OfflineSlaveConfiguration &slave : reordered.slaves) {
        if (slave.id == physicalFirstId)
            slave.position = 1;
        else if (slave.id == physicalSecondId)
            slave.position = 0;
    }
    controller.treeModel()->setProjects({reordered});
    const QList<Data::NodeId> reorderedPhysicalOrder{physicalSecondId, physicalFirstId};
    QTRY_COMPARE(configuredChildIds(controller.treeModel(), master), reorderedPhysicalOrder);
    QTRY_COMPARE(configuredChildIds(proxyModel, master), reorderedPhysicalOrder);
    QCOMPARE(navigation.filterEdit()->text(), QString("Physical"));
    QCOMPARE(controller.selectionService()->currentNodeId(), physicalFirstId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        physicalFirstId);

    navigation.filterEdit()->clear();
    QTRY_COMPARE(configuredChildIds(proxyModel, master), reorderedPhysicalOrder);
    QCOMPARE(controller.selectionService()->currentNodeId(), physicalFirstId);

    Data::ProjectSnapshot duplicatePosition = project;
    duplicatePosition.slaves[1].position = 0;
    controller.treeModel()->setProjects({duplicatePosition});
    const QStringList duplicatePositionFallback{
        "Alpha Physical Second", "Zulu Physical First"};
    QCOMPARE(
        configuredChildNames(controller.treeModel(), master), duplicatePositionFallback);
    QTRY_COMPARE(configuredChildNames(proxyModel, master), duplicatePositionFallback);

    Data::ProjectSnapshot missingPosition = project;
    const Data::NodeId missingPositionId = Data::NodeId::create();
    missingPosition.nodes.append(
        {missingPositionId, master, Data::ProjectNodeKind::Slave, "Mike Missing Position"});
    controller.treeModel()->setProjects({missingPosition});
    const QStringList missingPositionFallback{
        "Alpha Physical Second", "Mike Missing Position", "Zulu Physical First"};
    QCOMPARE(
        configuredChildNames(controller.treeModel(), master), missingPositionFallback);
    QTRY_COMPARE(configuredChildNames(proxyModel, master), missingPositionFallback);
    QCOMPARE(controller.selectionService()->currentNodeId(), physicalFirstId);
}

void EtherCATWorkbenchTests::testTwinCatInsertDeviceWorkflow()
{
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));

    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    EsiDeviceSelectionDialog emptyDialog({});
    QLabel *emptyStatus
        = emptyDialog.findChild<QLabel *>("EtherCATEsiDeviceSelectionStatus");
    QDialogButtonBox *emptyButtons
        = emptyDialog.findChild<QDialogButtonBox *>("EtherCATEsiDeviceSelectionButtons");
    QVERIFY(emptyStatus);
    QVERIFY(emptyButtons);
    QVERIFY(emptyStatus->text().contains("No ESI devices", Qt::CaseInsensitive));
    QVERIFY(!emptyButtons->button(QDialogButtonBox::Ok)->isEnabled());
    QVERIFY(!emptyDialog.accessibleName().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto writeEsi = [&directory](
                              const QString &fileName,
                              quint32 productCode,
                              quint32 revision,
                              const QByteArray &name,
                              bool limited) {
        QByteArray esi = deviceEsi();
        const QByteArray productText = QByteArray("#x") + QByteArray::number(productCode, 16);
        const QByteArray revisionText = QByteArray("#x") + QByteArray::number(revision, 16);
        esi.replace("#x00005678", productText);
        esi.replace("#x00000011", revisionText);
        esi.replace("Workbench Servo", name);
        if (limited)
            esi.replace("</Device>", "<Modules/></Device>");
        const Utils::FilePath path = Utils::FilePath::fromString(directory.path())
                                         .canonicalPath()
                                         .pathAppended(fileName);
        const Utils::Result<qint64> written = path.writeFileContents(esi);
        return written ? path : Utils::FilePath();
    };
    const Utils::FilePath legacyPath = writeEsi(
        "insert-legacy.xml", 0x7a130001, 0x00000011, "Legacy Catalogue Servo", false);
    const Utils::FilePath currentPath = writeEsi(
        "insert-current.xml", 0x7a130001, 0x00000022, "Current Catalogue Servo", false);
    const Utils::FilePath limitedPath = writeEsi(
        "insert-limited.xml", 0x7a130002, 0x00000001, "Limited Modular Device", true);
    QVERIFY(!legacyPath.isEmpty());
    QVERIFY(!currentPath.isEmpty());
    QVERIFY(!limitedPath.isEmpty());
    const Data::DeviceImportResult importResult
        = waitForJob(repository->importFiles({legacyPath, currentPath, limitedPath}));
    QCOMPARE(importResult.requestedFiles, 3);
    QCOMPARE(importResult.importedDevices, 3);
    QCOMPARE(importResult.failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto legacy = std::find_if(devices.cbegin(), devices.cend(), [](const auto &device) {
        return device.identity.productCode == 0x7a130001
               && device.identity.revisionNumber == 0x00000011;
    });
    const auto current = std::find_if(devices.cbegin(), devices.cend(), [](const auto &device) {
        return device.identity.productCode == 0x7a130001
               && device.identity.revisionNumber == 0x00000022;
    });
    const auto limited = std::find_if(devices.cbegin(), devices.cend(), [](const auto &device) {
        return device.identity.productCode == 0x7a130002;
    });
    QVERIFY(legacy != devices.cend());
    QVERIFY(current != devices.cend());
    QVERIFY(limited != devices.cend());
    QVERIFY(legacy->supported);
    QVERIFY(current->supported);
    QVERIFY(!limited->supported);

    const TestProjectFile file = writeProjectWithSlave(directory, *current);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QVERIFY_RESULT(projectService->activateProject(file.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), file.projectId);
    controller.selectionService()->setCurrentNodeId(file.masterId);

    ::Core::Command *insertCommand = ::Core::ActionManager::command(
        Constants::INSERT_DEVICE_ACTION_ID);
    QVERIFY(insertCommand);
    QTRY_VERIFY(insertCommand->action()->isEnabled());
    QVERIFY(!insertCommand->action()->icon().isNull());

    bool dialogInspected = false;
    bool limitedSelectionBlocked = false;
    bool latestRevisionDefault = false;
    bool previousRevisionShown = false;
    bool extendedInformationShown = false;
    QTimer::singleShot(0, [&] {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATEsiDeviceSelectionDialog")
            return;
        QLineEdit *filter = dialog->findChild<QLineEdit *>("EtherCATEsiDeviceSelectionFilter");
        QCheckBox *extended
            = dialog->findChild<QCheckBox *>("EtherCATEsiDeviceSelectionExtendedInformation");
        QCheckBox *showPrevious
            = dialog->findChild<QCheckBox *>("EtherCATEsiDeviceSelectionShowPrevious");
        QTreeView *tree = dialog->findChild<QTreeView *>("EtherCATEsiDeviceSelectionTree");
        QLabel *selectionStatus
            = dialog->findChild<QLabel *>("EtherCATEsiDeviceSelectionStatus");
        QDialogButtonBox *buttons
            = dialog->findChild<QDialogButtonBox *>("EtherCATEsiDeviceSelectionButtons");
        if (!filter || !extended || !showPrevious || !tree || !selectionStatus || !buttons)
            return;
        QPushButton *add = buttons->button(QDialogButtonBox::Ok);
        if (!add)
            return;
        dialog->resize(1000, 640);

        const int revisionColumn = columnWithHeader(tree->model(), "Revision");
        if (revisionColumn < 0)
            return;
        const QModelIndex currentIndex
            = findByDisplayText(tree->model(), "Current Catalogue Servo");
        latestRevisionDefault = currentIndex.isValid()
                                && !findByDisplayText(
                                        tree->model(), "Legacy Catalogue Servo").isValid()
                                && tree->isColumnHidden(revisionColumn);

        showPrevious->setChecked(true);
        previousRevisionShown
            = findByDisplayText(tree->model(), "Legacy Catalogue Servo").isValid();
        extended->setChecked(true);
        extendedInformationShown = !tree->isColumnHidden(revisionColumn);

        filter->setText("Limited Modular Device");
        QModelIndex selection = findByDisplayText(tree->model(), "Limited Modular Device");
        if (!selection.isValid())
            return;
        tree->setCurrentIndex(selection);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        limitedSelectionBlocked = !add->isEnabled()
                                  && selectionStatus->text().contains(
                                      "limited", Qt::CaseInsensitive);

        filter->setText("Current Catalogue Servo");
        selection = findByDisplayText(tree->model(), "Current Catalogue Servo");
        if (!selection.isValid())
            return;
        tree->setCurrentIndex(selection);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!add->isEnabled())
            return;
        filter->clear();
        selection = findByDisplayText(tree->model(), "Current Catalogue Servo");
        if (!selection.isValid())
            return;
        tree->setCurrentIndex(selection);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QString renderPath
            = qEnvironmentVariable("ETHERCAT_WORKBENCH_INSERT_DEVICE_RENDER_PATH");
        if (!renderPath.isEmpty() && !dialog->grab().save(renderPath))
            return;
        dialogInspected = true;
        add->click();
    });
    insertCommand->action()->trigger();
    QVERIFY(dialogInspected);
    QVERIFY(limitedSelectionBlocked);
    QVERIFY(latestRevisionDefault);
    QVERIFY(previousRevisionShown);
    QVERIFY(extendedInformationShown);

    const Utils::Result<> limitedResult
        = controller.addDeviceToMaster(limited->id, file.masterId);
    QVERIFY(!limitedResult);
    QVERIFY(limitedResult.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(projectService->project(file.projectId)->slaves.size(), 2);
    const Utils::Result<> missingMasterResult
        = controller.addDeviceToMaster(current->id, Data::NodeId::create());
    QVERIFY(!missingMasterResult);
    QVERIFY(missingMasterResult.error().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(projectService->project(file.projectId)->slaves.size(), 2);
    const Utils::Result<> nullMasterResult
        = controller.addDeviceToMaster(current->id, {});
    QVERIFY(!nullMasterResult);
    QVERIFY(nullMasterResult.error().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);
    const Data::ProjectSnapshot afterAdd = *projectService->project(file.projectId);
    const auto added = std::find_if(
        afterAdd.slaves.cbegin(), afterAdd.slaves.cend(), [&file](const auto &slave) {
            return slave.id != file.slaveId;
        });
    QVERIFY(added != afterAdd.slaves.cend());
    QCOMPARE(added->masterId, file.masterId);
    QCOMPARE(added->position, 1);
    QCOMPARE(added->name, QString("Current Catalogue Servo"));
    QCOMPARE(added->deviceDescriptionId, current->id);
    QCOMPARE(controller.selectionService()->currentNodeId(), added->id);
    QVERIFY(projectService->canUndoProject(file.projectId));
    QVERIFY_RESULT(projectService->undoProject(file.projectId));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 1);
    QVERIFY_RESULT(projectService->redoProject(file.projectId));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    controller.selectionService()->setCurrentNodeId(file.masterId);
    QTRY_VERIFY(insertCommand->action()->isEnabled());
    bool cancelInspected = false;
    QTimer::singleShot(0, [&cancelInspected] {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATEsiDeviceSelectionDialog")
            return;
        QDialogButtonBox *buttons
            = dialog->findChild<QDialogButtonBox *>("EtherCATEsiDeviceSelectionButtons");
        if (!buttons)
            return;
        cancelInspected = true;
        buttons->button(QDialogButtonBox::Cancel)->click();
    });
    insertCommand->action()->trigger();
    QVERIFY(cancelInspected);
    QCOMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(900, 600);
    navigation.show();
    QTRY_VERIFY(navigation.isVisible());
    controller.selectionService()->clear();
    controller.selectionService()->setCurrentNodeId(file.masterId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        file.masterId);
    QList<QAction *> masterMenuActions;
    QTimer::singleShot(0, &navigation, [&masterMenuActions] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        masterMenuActions = popup->actions();
        popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(masterMenuActions.contains(insertCommand->action()));

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QVERIFY(!insertCommand->action()->isEnabled());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEsiDeviceSelectionCellAccessibility()
{
    const QString longName
        = QString::fromUtf8("支持设备 / Servo ") + QString(256, QChar(0x540d))
          + QString::fromUtf8(" / 日本語 / %1 / %2 / %%");
    const QString longType
        = QString::fromUtf8("EL-Accessible-Type-") + QString(256, QChar(0x578b));
    const QString longGroup
        = QString::fromUtf8("长设备组 / Group ") + QString(256, QChar(0x7ec4));
    const Data::DeviceSummary supported{
        Data::NodeId::create(),
        {0x00000002, 0x7a130004, 0x00000022},
        longName,
        longType,
        longGroup,
        true};
    const Data::DeviceSummary limited{
        Data::NodeId::create(),
        {0x00000003, 0x7a130005, 0x00000011},
        QString::fromUtf8("受限模块设备 / Limited Module"),
        QString::fromUtf8("Limited-Type"),
        QString::fromUtf8("Modules / 模块"),
        false};

    EsiDeviceSelectionDialog dialog({supported, limited});
    QTreeView *tree = dialog.findChild<QTreeView *>("EtherCATEsiDeviceSelectionTree");
    QDialogButtonBox *buttons
        = dialog.findChild<QDialogButtonBox *>("EtherCATEsiDeviceSelectionButtons");
    QVERIFY(tree);
    QVERIFY(buttons);
    QVERIFY(!tree->accessibleName().isEmpty());
    QVERIFY(!tree->accessibleDescription().isEmpty());
    QAbstractItemModelTester modelTester(
        tree->model(), QAbstractItemModelTester::FailureReportingMode::QtTest, &dialog);
    Q_UNUSED(modelTester)

    QAbstractItemModel *model = tree->model();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->columnCount(), 7);
    const QModelIndex supportedRow = findByDisplayText(model, longName);
    const QModelIndex limitedRow
        = findByDisplayText(model, QString::fromUtf8("受限模块设备 / Limited Module"));
    QVERIFY(supportedRow.isValid());
    QVERIFY(limitedRow.isValid());

    const auto verifyRow = [model](
                               const QModelIndex &rowIndex,
                               const QString &qualification,
                               const QString &operationText) {
        for (int column = 0; column < model->columnCount(); ++column) {
            const QModelIndex index = rowIndex.siblingAtColumn(column);
            const QString heading = model->headerData(column, Qt::Horizontal).toString();
            const QString display = index.data(Qt::DisplayRole).toString();
            const QVariant accessibleText = index.data(Qt::AccessibleTextRole);
            const QVariant accessibleDescription = index.data(Qt::AccessibleDescriptionRole);
            const QVariant toolTip = index.data(Qt::ToolTipRole);
            QVERIFY2(!heading.isEmpty(), qPrintable(QString("column %1").arg(column)));
            QCOMPARE(accessibleText.metaType().id(), int(QMetaType::QString));
            QCOMPARE(accessibleText.toString(), display);
            QCOMPARE(accessibleDescription.metaType().id(), int(QMetaType::QString));
            QVERIFY(accessibleDescription.toString().contains(heading));
            QVERIFY(accessibleDescription.toString().contains(display));
            QVERIFY(accessibleDescription.toString().contains(qualification));
            QVERIFY(accessibleDescription.toString().contains(operationText));
            QVERIFY(accessibleDescription.toString().contains("offline", Qt::CaseInsensitive));
            QVERIFY(accessibleDescription.toString().contains("controller", Qt::CaseInsensitive));
            QVERIFY(accessibleDescription.toString().contains("network", Qt::CaseInsensitive));
            QCOMPARE(toolTip.metaType().id(), int(QMetaType::QString));
            QCOMPARE(toolTip.toString(), accessibleDescription.toString());
        }
    };

    verifyRow(supportedRow, "Supported", "can be appended");
    verifyRow(limitedRow, "Limited", "cannot be appended");
    QCOMPARE(
        supportedRow.siblingAtColumn(columnWithHeader(model, "Device"))
            .data(Qt::AccessibleTextRole)
            .toString(),
        longName);
    QCOMPARE(
        supportedRow.siblingAtColumn(columnWithHeader(model, "Type"))
            .data(Qt::AccessibleTextRole)
            .toString(),
        longType);
    QCOMPARE(
        supportedRow.siblingAtColumn(columnWithHeader(model, "Group"))
            .data(Qt::AccessibleTextRole)
            .toString(),
        longGroup);

    tree->setCurrentIndex(limitedRow);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
    tree->setCurrentIndex(supportedRow);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
}

void EtherCATWorkbenchTests::testInsertDeviceDialogTargetLifecycle_data()
{
    QTest::addColumn<QString>("targetInvalidation");
    QTest::newRow("active-project-switch") << QString("active-project-switch");
    QTest::newRow("target-project-close") << QString("target-project-close");
    QTest::newRow("project-invalidated") << QString("project-invalidated");
    QTest::newRow("master-removed") << QString("master-removed");
}

void EtherCATWorkbenchTests::testInsertDeviceDialogTargetLifecycle()
{
    QFETCH(QString, targetInvalidation);

    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));

    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray esi = deviceEsi();
    esi.replace("#x00005678", "#x7a130003");
    esi.replace("Workbench Servo", "Lifecycle Catalogue Servo");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("insert-lifecycle.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(esi));
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0x7a130003;
    });
    QVERIFY(device != devices.cend());
    QVERIFY(device->supported);

    const TestProjectFile primary = writeProjectWithSlave(
        directory, *device, "insert-lifecycle-primary.ecatproject", "Lifecycle Primary");
    const TestProjectFile alternate = writeProjectWithSlave(
        directory, *device, "insert-lifecycle-alternate.ecatproject", "Lifecycle Alternate");
    QVERIFY(!primary.path.isEmpty());
    QVERIFY(!alternate.path.isEmpty());
    const ProjectExplorer::OpenProjectResult primaryOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(primary.path, false);
    const ProjectExplorer::OpenProjectResult alternateOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(alternate.path, false);
    QVERIFY2(primaryOpened, qPrintable(primaryOpened.errorMessage()));
    QVERIFY2(alternateOpened, qPrintable(alternateOpened.errorMessage()));
    auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (projectService->project(alternate.projectId))
            ProjectExplorer::ProjectManager::removeProject(alternateOpened.project());
        if (projectService->project(primary.projectId))
            ProjectExplorer::ProjectManager::removeProject(primaryOpened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(primary.projectId).has_value());
    QTRY_VERIFY(projectService->project(alternate.projectId).has_value());

    ::Core::Command *insertCommand = ::Core::ActionManager::command(
        Constants::INSERT_DEVICE_ACTION_ID);
    QVERIFY(insertCommand);
    const Data::ProjectSnapshot primaryBefore = *projectService->project(primary.projectId);
    const Data::ProjectSnapshot alternateBefore = *projectService->project(alternate.projectId);

    QVERIFY_RESULT(projectService->activateProject(primary.projectId));
    controller.selectionService()->setCurrentNodeId(primary.masterId);
    QTRY_VERIFY(insertCommand->action()->isEnabled());
    bool dialogSeen = false;
    bool invalidationCompleted = false;
    bool dialogRejected = false;
    QTimer::singleShot(0, [&] {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATEsiDeviceSelectionDialog")
            return;
        dialogSeen = true;
        QSignalSpy rejected(dialog, &QDialog::rejected);
        if (targetInvalidation == "target-project-close") {
            ProjectExplorer::ProjectManager::removeProject(primaryOpened.project());
            invalidationCompleted = !projectService->project(primary.projectId).has_value();
        } else if (targetInvalidation == "active-project-switch") {
            invalidationCompleted = bool(projectService->activateProject(alternate.projectId));
        } else {
            Data::ProjectSnapshot invalidatedProject = primaryBefore;
            if (targetInvalidation == "project-invalidated") {
                invalidatedProject.valid = false;
            } else {
                invalidatedProject.nodes.removeIf(
                    [&primary](const auto &node) { return node.id == primary.masterId; });
            }
            emit projectService->projectChanged(invalidatedProject);
            invalidationCompleted = true;
        }
        dialogRejected = !dialog->isVisible() && rejected.size() == 1;
        if (dialog->isVisible())
            dialog->reject();
    });
    insertCommand->action()->trigger();
    QVERIFY(dialogSeen);
    QVERIFY(invalidationCompleted);
    QVERIFY(dialogRejected);
    if (targetInvalidation != "target-project-close")
        QCOMPARE(*projectService->project(primary.projectId), primaryBefore);
    QCOMPARE(*projectService->project(alternate.projectId), alternateBefore);
    if (targetInvalidation == "active-project-switch"
        || targetInvalidation == "target-project-close") {
        QTRY_VERIFY(!insertCommand->action()->isEnabled());
    }
}

void EtherCATWorkbenchTests::testEsiDeviceDragDropWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto writeEsi = [&directory](
                              const QString &fileName,
                              quint32 productCode,
                              const QByteArray &name,
                              bool limited) {
        QByteArray esi = deviceEsi();
        const QByteArray productText = QByteArray("#x") + QByteArray::number(productCode, 16);
        esi.replace("#x00005678", productText);
        esi.replace("Workbench Servo", name);
        if (limited)
            esi.replace("</Device>", "<Modules/></Device>");
        const Utils::FilePath path = Utils::FilePath::fromString(directory.path())
                                         .canonicalPath()
                                         .pathAppended(fileName);
        const Utils::Result<qint64> written = path.writeFileContents(esi);
        return written ? path : Utils::FilePath();
    };
    const Utils::FilePath supportedPath
        = writeEsi("drag-supported.xml", 0x7a140001, "Drag Supported Servo", false);
    const Utils::FilePath limitedPath
        = writeEsi("drag-limited.xml", 0x7a140002, "Drag Limited Device", true);
    QVERIFY(!supportedPath.isEmpty());
    QVERIFY(!limitedPath.isEmpty());
    const Data::DeviceImportResult importResult
        = waitForJob(repository->importFiles({supportedPath, limitedPath}));
    QCOMPARE(importResult.requestedFiles, 2);
    QCOMPARE(importResult.importedDevices, 2);
    QCOMPARE(importResult.failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto supported = std::find_if(devices.cbegin(), devices.cend(), [](const auto &device) {
        return device.identity.productCode == 0x7a140001;
    });
    const auto limited = std::find_if(devices.cbegin(), devices.cend(), [](const auto &device) {
        return device.identity.productCode == 0x7a140002;
    });
    QVERIFY(supported != devices.cend());
    QVERIFY(limited != devices.cend());
    QVERIFY(supported->supported);
    QVERIFY(!limited->supported);

    const TestProjectFile file = writeProjectWithSlave(directory, *supported);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QVERIFY_RESULT(projectService->activateProject(file.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), file.projectId);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(900, 600);
    navigation.show();
    QTRY_VERIFY(navigation.isVisible());
    QTreeView *tree = navigation.treeView();
    QAbstractItemModel *model = tree->model();
    QVERIFY(tree->accessibleDescription().contains("drag", Qt::CaseInsensitive));
    QVERIFY(tree->dragEnabled());
    QVERIFY(tree->viewport()->acceptDrops());
    QVERIFY(tree->showDropIndicator());
    QCOMPARE(tree->dragDropMode(), QAbstractItemView::DragDrop);
    QCOMPARE(tree->defaultDropAction(), Qt::CopyAction);
    QCOMPARE(model->supportedDragActions(), Qt::CopyAction);
    QCOMPARE(model->supportedDropActions(), Qt::CopyAction);

    const QModelIndex supportedIndex = findById(model, supported->id);
    const QModelIndex limitedIndex = findById(model, limited->id);
    const QModelIndex masterIndex = findById(model, file.masterId);
    const QModelIndex slaveIndex = findById(model, file.slaveId);
    QVERIFY(supportedIndex.isValid());
    QVERIFY(limitedIndex.isValid());
    QVERIFY(masterIndex.isValid());
    QVERIFY(slaveIndex.isValid());
    QVERIFY(model->flags(supportedIndex) & Qt::ItemIsDragEnabled);
    QVERIFY(!(model->flags(limitedIndex) & Qt::ItemIsDragEnabled));
    QVERIFY(model->flags(masterIndex) & Qt::ItemIsDropEnabled);
    QVERIFY(!(model->flags(slaveIndex) & Qt::ItemIsDropEnabled));
    QVERIFY(masterIndex.data(Qt::ToolTipRole).toString().contains("drop", Qt::CaseInsensitive));

    const QStringList mimeTypes = model->mimeTypes();
    QCOMPARE(mimeTypes.size(), 1);
    const QString mimeType = mimeTypes.constFirst();
    QVERIFY(mimeType.contains("ethercat", Qt::CaseInsensitive));
    std::unique_ptr<QMimeData> supportedMime(model->mimeData({supportedIndex}));
    std::unique_ptr<QMimeData> limitedMime(model->mimeData({limitedIndex}));
    QVERIFY(supportedMime);
    QVERIFY(limitedMime);
    QVERIFY(supportedMime->hasFormat(mimeType));
    QVERIFY(!limitedMime->hasFormat(mimeType));
    QCOMPARE(
        Data::NodeId::fromString(QString::fromUtf8(supportedMime->data(mimeType))),
        supported->id);

    QVERIFY(model->canDropMimeData(
        supportedMime.get(), Qt::CopyAction, -1, -1, masterIndex));
    QVERIFY(!model->canDropMimeData(
        supportedMime.get(), Qt::MoveAction, -1, -1, masterIndex));
    QVERIFY(!model->canDropMimeData(
        supportedMime.get(), Qt::CopyAction, -1, -1, slaveIndex));
    QVERIFY(!model->canDropMimeData(
        supportedMime.get(), Qt::CopyAction, 0, -1, masterIndex));

    QMimeData forgedLimited;
    forgedLimited.setData(mimeType, limited->id.toString().toUtf8());
    QVERIFY(!model->canDropMimeData(
        &forgedLimited, Qt::CopyAction, -1, -1, masterIndex));
    QMimeData forgedUnknown;
    forgedUnknown.setData(mimeType, Data::NodeId::create().toString().toUtf8());
    QVERIFY(!model->canDropMimeData(
        &forgedUnknown, Qt::CopyAction, -1, -1, masterIndex));

    const int repositoryCount = repository->devices().size();
    QCOMPARE(projectService->project(file.projectId)->slaves.size(), 1);
    QVERIFY(model->dropMimeData(
        supportedMime.get(), Qt::CopyAction, -1, -1, masterIndex));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);
    QVERIFY(projectService->project(file.projectId)->modified);
    QCOMPARE(repository->devices().size(), repositoryCount);
    const Data::ProjectSnapshot afterDrop = *projectService->project(file.projectId);
    const auto added = std::find_if(
        afterDrop.slaves.cbegin(), afterDrop.slaves.cend(), [&file](const auto &slave) {
            return slave.id != file.slaveId;
        });
    QVERIFY(added != afterDrop.slaves.cend());
    QCOMPARE(added->masterId, file.masterId);
    QCOMPARE(added->position, 1);
    QCOMPARE(added->deviceDescriptionId, supported->id);
    QCOMPARE(added->name, QString("Drag Supported Servo"));
    QCOMPARE(controller.selectionService()->currentNodeId(), added->id);
    QVERIFY(projectService->canUndoProject(file.projectId));
    QVERIFY_RESULT(projectService->undoProject(file.projectId));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 1);
    QVERIFY_RESULT(projectService->redoProject(file.projectId));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEditableConfiguredSlaveGeneralWorkflow()
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
                                        .pathAppended("editable-general.xml");
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
    details.resize(1100, 720);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATGeneralName");
    QLineEdit *id = page->findChild<QLineEdit *>("EtherCATGeneralId");
    QLineEdit *objectId = page->findChild<QLineEdit *>("EtherCATGeneralObjectId");
    QLineEdit *type = page->findChild<QLineEdit *>("EtherCATGeneralType");
    QVERIFY(name);
    QVERIFY(title);
    QVERIFY(id);
    QVERIFY(objectId);
    QVERIFY(type);
    QCOMPARE(name->text(), QString("Configured Servo"));
    QCOMPARE(id->text(), QString("1"));
    QCOMPARE(objectId->text(), file.slaveId.toString());
    QCOMPARE(type->text(), QString("AX5000"));
    QVERIFY(!name->isReadOnly());
    QVERIFY(id->isReadOnly());
    QVERIFY(objectId->isReadOnly());
    QVERIFY(type->isReadOnly());
    QVERIFY(!name->accessibleName().isEmpty());

    const QString renamed = QString::fromUtf8("Axis X / 主轴");
    name->setText("  " + renamed + "  ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().name, renamed);
    QTRY_COMPARE(controller.treeModel()->indexForNodeId(file.slaveId).data().toString(), renamed);
    QTRY_COMPARE(title->text(), renamed);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.slaveId);
    QVERIFY(projectService->canUndoProject(file.projectId));

    const Utils::Result<> undoRename = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoRename);
    QTRY_COMPARE(
        projectService->project(file.projectId)->slaves.first().name, QString("Configured Servo"));
    QTRY_COMPARE(name->text(), QString("Configured Servo"));
    QTRY_COMPARE(title->text(), QString("Configured Servo"));
    const Utils::Result<> redoRename = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoRename);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().name, renamed);
    QTRY_COMPARE(name->text(), renamed);
    QTRY_COMPARE(title->text(), renamed);

    name->setText("   ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(name->text(), renamed);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().name, renamed);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testGeneralPropertyTreeAccessibility()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString longDeviceName
        = QString::fromUtf8(
              "\u8d85\u957f ESI \u8bbe\u5907 / \u9577\u3044\u30c7\u30d0\u30a4\u30b9 / \u03a9 "
              "/ %1 / %2 / %% \u2014 ")
          + QString(128, QChar(0x754c)) + QString::fromUtf8(" / \u5b8c\u6574\u5c3e\u90e8");
    const QString longSlaveName
        = QString::fromUtf8("\u914d\u7f6e\u4ece\u7ad9 / \u8ef8 / %1 / %2 / %% \u2014 ")
          + QString(128, QLatin1Char('S')) + QString::fromUtf8(" / \u5b8c\u6574\u5c3e\u90e8");
    const QString sourceFileName
        = QString::fromUtf8("general-property-\u8bbe\u5907-%1-%2-%%-")
          + QString(64, QLatin1Char('p')) + ".xml";
    const Utils::FilePath sourcePath = Utils::FilePath::fromString(directory.path())
                                           .canonicalPath()
                                           .pathAppended(sourceFileName);
    QByteArray esi = deviceEsi();
    esi.replace("#x00005678", "#xA11E0002");
    esi.replace("Workbench Servo", longDeviceName.toUtf8());
    QVERIFY_RESULT(sourcePath.writeFileContents(esi));
    QCOMPARE(waitForJob(repository->importFiles({sourcePath})).failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0xA11E0002u;
    });
    QVERIFY(device != devices.cend());
    const std::optional<Data::DeviceDescription> description = repository->device(device->id);
    QVERIFY(description);

    ProcessTreeFixture fixture = processTreeFixture();
    fixture.project.slaves.first().identity = device->identity;
    fixture.project.slaves.first().deviceDescriptionId = device->id;
    fixture.project.slaves.first().name = longSlaveName;
    for (Data::ProjectNodeSnapshot &node : fixture.project.nodes) {
        if (node.id == fixture.slaveId)
            node.name = longSlaveName;
    }
    controller.treeModel()->setProjects({fixture.project});

    const QModelIndex slave = controller.treeModel()->indexForNodeId(fixture.slaveId);
    QVERIFY(slave.isValid());
    const QModelIndex modules = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::Modules, slave);
    QVERIFY(modules.isValid());

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> generalPage(
        pages.createPage(Constants::GENERAL_PAGE_ID, nullptr));
    QVERIFY(generalPage);
    QTreeWidget *propertyTree
        = generalPage->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(propertyTree);
    const QStringList offlineBoundaries = {
        "read-only", "offline", "controller", "network", "hardware"};

    const auto verifyPropertyTree = [&](const QModelIndex &contextIndex) -> QString {
        pages.updatePage(
            Constants::GENERAL_PAGE_ID,
            generalPage.get(),
            controller.treeModel()->contextForIndex(contextIndex));
        if (propertyTree->isHidden())
            return "General property tree is hidden";
        if (propertyTree->accessibleName().isEmpty())
            return "General property tree accessible name is empty";
        const QString treeDescription = propertyTree->accessibleDescription();
        if (treeDescription.isEmpty())
            return "General property tree accessible description is empty";
        for (const QString &boundary : offlineBoundaries) {
            if (!treeDescription.contains(boundary, Qt::CaseInsensitive))
                return "General property tree description misses boundary: " + boundary;
        }

        const QAbstractItemModel *model = propertyTree->model();
        if (!model || model->rowCount() == 0 || model->columnCount() != 2)
            return "General property tree does not contain two-column data";
        for (int row = 0; row < model->rowCount(); ++row) {
            const QString property = model->index(row, 0).data(Qt::DisplayRole).toString();
            const QString value = model->index(row, 1).data(Qt::DisplayRole).toString();
            for (int column = 0; column < model->columnCount(); ++column) {
                const QModelIndex index = model->index(row, column);
                const QString header
                    = model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
                const QString display = index.data(Qt::DisplayRole).toString();
                const QVariant accessibleText = index.data(Qt::AccessibleTextRole);
                const QVariant accessibleDescription
                    = index.data(Qt::AccessibleDescriptionRole);
                const QVariant toolTip = index.data(Qt::ToolTipRole);
                const QString cell = QString("row %1 column %2").arg(row).arg(column);
                if (accessibleText.metaType().id() != QMetaType::QString)
                    return cell + " AccessibleTextRole is not a QString";
                if (accessibleText.toString() != display)
                    return cell + " accessible text differs from DisplayRole";
                if (accessibleDescription.metaType().id() != QMetaType::QString)
                    return cell + " AccessibleDescriptionRole is not a QString";
                const QString cellDescription = accessibleDescription.toString();
                if (!cellDescription.contains(header))
                    return cell + " description misses the column heading";
                if (!cellDescription.contains(property))
                    return cell + " description misses the property name";
                if (!value.isEmpty() && !cellDescription.contains(value))
                    return cell + " description misses the complete value";
                for (const QString &boundary : offlineBoundaries) {
                    if (!cellDescription.contains(boundary, Qt::CaseInsensitive))
                        return cell + " description misses boundary: " + boundary;
                }
                if (toolTip.metaType().id() != QMetaType::QString)
                    return cell + " ToolTipRole is not a QString";
                if (toolTip.toString() != cellDescription)
                    return cell + " tooltip differs from accessible description";
            }
        }
        return {};
    };

    const auto valueForProperty = [propertyTree](const QString &property) -> QString {
        for (int row = 0; row < propertyTree->topLevelItemCount(); ++row) {
            QTreeWidgetItem *item = propertyTree->topLevelItem(row);
            if (item->text(0) == property)
                return item->text(1);
        }
        return {};
    };

    QString verificationError = verifyPropertyTree(slave);
    QVERIFY2(verificationError.isEmpty(), qPrintable(verificationError));
    QCOMPARE(valueForProperty("ESI match"), longDeviceName);
    QCOMPARE(valueForProperty("Source"), description->sourcePath);

    verificationError = verifyPropertyTree(modules);
    QVERIFY2(verificationError.isEmpty(), qPrintable(verificationError));
    QCOMPARE(valueForProperty("Name"), QString("Modules / Channels"));
    QCOMPARE(valueForProperty("Owner slave"), longSlaveName);
    QCOMPARE(valueForProperty("ESI match"), longDeviceName);
    QCOMPARE(valueForProperty("Source"), description->sourcePath);

    const QString renderPath = qEnvironmentVariable(
        "ETHERCAT_WORKBENCH_GENERAL_PROPERTY_A11Y_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        generalPage->resize(1100, 720);
        generalPage->show();
        QTRY_VERIFY(generalPage->isVisible());
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(generalPage->grab().save(renderPath));
    }
}

void EtherCATWorkbenchTests::testEditableProjectGeneralWorkflow()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary
        device{Data::NodeId::create(), {2, 0x5678, 0x11}, "Mock Servo", "AX5000", "Drives", true};
    const TestProjectFile file = writeProjectWithSlave(directory, device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.projectId).isValid());

    controller.selectionService()->setCurrentNodeId(file.projectId);
    DetailsView details(&controller);
    details.resize(1100, 720);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QWidget *content = page->findChild<QWidget *>("EtherCATProjectGeneralContent");
    QWidget *identityForm = page->findChild<QWidget *>("EtherCATProjectGeneralForm");
    QGroupBox *summaryForm
        = page->findChild<QGroupBox *>("EtherCATProjectConfigurationSummary");
    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    QLineEdit *id = page->findChild<QLineEdit *>("EtherCATProjectGeneralId");
    QLineEdit *type = page->findChild<QLineEdit *>("EtherCATProjectGeneralType");
    QLineEdit *formatVersion
        = page->findChild<QLineEdit *>("EtherCATProjectGeneralFormatVersion");
    QLineEdit *createdBy = page->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QLineEdit *validity = page->findChild<QLineEdit *>("EtherCATProjectGeneralValidity");
    QLineEdit *migration = page->findChild<QLineEdit *>("EtherCATProjectGeneralMigration");
    QLineEdit *modified = page->findChild<QLineEdit *>("EtherCATProjectGeneralModified");
    QLineEdit *target = page->findChild<QLineEdit *>("EtherCATProjectGeneralTarget");
    QLineEdit *master = page->findChild<QLineEdit *>("EtherCATProjectGeneralMaster");
    QLineEdit *slaveCount = page->findChild<QLineEdit *>("EtherCATProjectGeneralSlaveCount");
    QTreeWidget *propertyTree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(content);
    QVERIFY(identityForm);
    QVERIFY(summaryForm);
    QVERIFY(title);
    QVERIFY(name);
    QVERIFY(id);
    QVERIFY(type);
    QVERIFY(formatVersion);
    QVERIFY(createdBy);
    QVERIFY(validity);
    QVERIFY(migration);
    QVERIFY(modified);
    QVERIFY(target);
    QVERIFY(master);
    QVERIFY(slaveCount);
    QVERIFY(propertyTree);

    QCOMPARE(name->text(), QString("Process Data Workflow"));
    QCOMPARE(id->text(), file.projectId.toString());
    QCOMPARE(type->text(), QString("Offline EtherCAT Engineering Project"));
    QCOMPARE(formatVersion->text(), QString("2"));
    QCOMPARE(createdBy->text(), QString("Workbench Test"));
    QCOMPARE(validity->text(), QString("Valid"));
    QCOMPARE(migration->text(), QString("Current format"));
    QCOMPARE(modified->text(), QString("No"));
    QCOMPARE(target->text(), QString("Offline Controller"));
    QCOMPARE(master->text(), QString("EtherCAT Master"));
    QCOMPARE(slaveCount->text(), QString("1"));
    QVERIFY(!name->isReadOnly());
    for (QLineEdit *field :
         {id, type, formatVersion, createdBy, validity, migration, modified, target, master,
          slaveCount}) {
        QVERIFY(field->isReadOnly());
        QVERIFY(!field->accessibleName().isEmpty());
    }
    QVERIFY(!name->accessibleName().isEmpty());
    QVERIFY(content->isVisible());
    QVERIFY(identityForm->isVisible());
    QVERIFY(summaryForm->isVisible());
    QVERIFY(!propertyTree->isVisible());
    QCOMPARE(propertyTree->topLevelItemCount(), 0);

    const QString renderPath = qEnvironmentVariable("ETHERCAT_WORKBENCH_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    const QString renamed = QString::fromUtf8("Packaging Cell / 包装线");
    name->setText("  " + renamed + "  ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(projectService->project(file.projectId)->name, renamed);
    QTRY_COMPARE(controller.treeModel()->indexForNodeId(file.projectId).data().toString(), renamed);
    QTRY_COMPARE(opened.project()->displayName(), renamed);
    QTRY_COMPARE(title->text(), renamed);
    QTRY_COMPARE(name->text(), renamed);
    QTRY_COMPARE(modified->text(), QString("Yes"));
    QCOMPARE(controller.selectionService()->currentNodeId(), file.projectId);
    QVERIFY(projectService->canUndoProject(file.projectId));

    QVERIFY_RESULT(projectService->undoProject(file.projectId));
    QTRY_COMPARE(projectService->project(file.projectId)->name, QString("Process Data Workflow"));
    QTRY_COMPARE(opened.project()->displayName(), QString("Process Data Workflow"));
    QTRY_COMPARE(name->text(), QString("Process Data Workflow"));
    QTRY_COMPARE(title->text(), QString("Process Data Workflow"));
    QTRY_COMPARE(modified->text(), QString("No"));
    QVERIFY_RESULT(projectService->redoProject(file.projectId));
    QTRY_COMPARE(projectService->project(file.projectId)->name, renamed);
    QTRY_COMPARE(name->text(), renamed);
    QTRY_COMPARE(title->text(), renamed);
    QTRY_COMPARE(modified->text(), QString("Yes"));

    name->setText("   ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(name->text(), renamed);
    QCOMPARE(projectService->project(file.projectId)->name, renamed);

    const Core::PropertyPageContext staleContext = details.currentContext();
    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> stalePage(pages.createPage(Constants::GENERAL_PAGE_ID, nullptr));
    pages.updatePage(Constants::GENERAL_PAGE_ID, stalePage.get(), staleContext);
    QLineEdit *staleName
        = stalePage->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    QLineEdit *staleValidity
        = stalePage->findChild<QLineEdit *>("EtherCATProjectGeneralValidity");
    QVERIFY(staleName);
    QVERIFY(staleValidity);
    QVERIFY(staleName->isReadOnly());
    QCOMPARE(staleValidity->text(), QString("Unavailable"));

    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEsiRepositoryEmptyGuidance()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);
    QTRY_VERIFY(!repository->isIndexing());
    controller.treeModel()->syncDevices({});

    QAbstractItemModelTester modelTester(
        controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QModelIndex repositoryIndex
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::DeviceRepository);
    QVERIFY(repositoryIndex.isValid());
    QCOMPARE(controller.treeModel()->rowCount(repositoryIndex), 1);
    const QModelIndex placeholder = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::Placeholder, repositoryIndex);
    QVERIFY(placeholder.isValid());

    const QString guidance
        = QString("Select Device Repository, then choose Import ESI Files...");
    QCOMPARE(placeholder.data().toString(), QString("No ESI devices imported"));
    QCOMPARE(placeholder.siblingAtColumn(1).data().toString(), guidance);
    QCOMPARE(placeholder.data(WorkbenchTreeModel::StatusRole).toString(), guidance);
    QCOMPARE(
        placeholder.data(WorkbenchTreeModel::SearchTextRole).toString(),
        QString("No ESI devices imported ") + guidance);
    QVERIFY(placeholder.data(Qt::ToolTipRole).toString().contains(guidance));
    QVERIFY(controller.treeModel()->flags(placeholder) & Qt::ItemIsEnabled);
    QVERIFY(!(controller.treeModel()->flags(placeholder) & Qt::ItemIsSelectable));

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(720, 360);
    navigation.show();
    QTRY_VERIFY(navigation.isVisible());
    navigation.treeView()->expandAll();
    QTRY_VERIFY(findByKind(
                    navigation.treeView()->model(), Core::WorkbenchNodeKind::Placeholder)
                    .isValid());

    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    const Data::NodeId repositoryId
        = repositoryIndex.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    QVERIFY(!repositoryId.isNull());
    controller.selectionService()->setCurrentNodeId(repositoryId);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QPushButton *importFiles
        = page->findChild<QPushButton *>("EtherCATEsiRepositoryImport");
    QVERIFY(importFiles);
    QCOMPARE(importFiles->text(), QString("Import ESI Files..."));
    QVERIFY(importFiles->isEnabled());
    QVERIFY(!importFiles->accessibleName().isEmpty());

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_ESI_EMPTY_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(navigation.grab().save(renderPath), qPrintable(renderPath));
    }
}

void EtherCATWorkbenchTests::testEsiRepositoryGeneralWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);
    QTRY_VERIFY(!repository->isIndexing());

    const QModelIndex repositoryIndex
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::DeviceRepository);
    QVERIFY(repositoryIndex.isValid());
    const Data::NodeId repositoryId
        = repositoryIndex.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    QVERIFY(!repositoryId.isNull());
    controller.selectionService()->setCurrentNodeId(repositoryId);

    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QWidget *content = page->findChild<QWidget *>("EtherCATEsiRepositoryContent");
    QLabel *title = page->findChild<QLabel *>("EtherCATEsiRepositoryTitle");
    QLabel *description = page->findChild<QLabel *>("EtherCATEsiRepositoryDescription");
    QGroupBox *summary = page->findChild<QGroupBox *>("EtherCATEsiRepositorySummary");
    QLineEdit *status = page->findChild<QLineEdit *>("EtherCATEsiRepositoryStatus");
    QLineEdit *deviceCount = page->findChild<QLineEdit *>("EtherCATEsiRepositoryDevices");
    QLineEdit *supportedCount = page->findChild<QLineEdit *>("EtherCATEsiRepositorySupported");
    QLineEdit *limitedCount = page->findChild<QLineEdit *>("EtherCATEsiRepositoryLimited");
    QLineEdit *vendorCount = page->findChild<QLineEdit *>("EtherCATEsiRepositoryVendors");
    QLineEdit *sourceCount = page->findChild<QLineEdit *>("EtherCATEsiRepositorySources");
    QPushButton *importFiles = page->findChild<QPushButton *>("EtherCATEsiRepositoryImport");
    QPushButton *reload = page->findChild<QPushButton *>("EtherCATEsiRepositoryReload");
    QPushButton *cancel = page->findChild<QPushButton *>("EtherCATEsiRepositoryCancel");
    QProgressBar *progress = page->findChild<QProgressBar *>("EtherCATEsiRepositoryProgress");
    QLabel *operation = page->findChild<QLabel *>("EtherCATEsiRepositoryOperation");
    QPlainTextEdit *result = page->findChild<QPlainTextEdit *>("EtherCATEsiRepositoryResult");
    QTreeWidget *propertyTree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(content);
    QVERIFY(title);
    QVERIFY(description);
    QVERIFY(summary);
    QVERIFY(status);
    QVERIFY(deviceCount);
    QVERIFY(supportedCount);
    QVERIFY(limitedCount);
    QVERIFY(vendorCount);
    QVERIFY(sourceCount);
    QVERIFY(importFiles);
    QVERIFY(reload);
    QVERIFY(cancel);
    QVERIFY(progress);
    QVERIFY(operation);
    QVERIFY(result);
    QVERIFY(propertyTree);

    const QList<Data::DeviceSummary> initialDevices = repository->devices();
    QSet<quint32> initialVendors;
    QSet<QString> initialSources;
    int initialSupported = 0;
    for (const Data::DeviceSummary &device : initialDevices) {
        initialVendors.insert(device.identity.vendorId);
        initialSupported += device.supported ? 1 : 0;
        const std::optional<Data::DeviceDescription> description = repository->device(device.id);
        if (description && !description->sourcePath.isEmpty())
            initialSources.insert(description->sourcePath);
    }
    QCOMPARE(title->text(), QString("ESI Device Repository"));
    QVERIFY(description->text().contains("offline", Qt::CaseInsensitive));
    QCOMPARE(status->text(), QString("Ready"));
    QCOMPARE(deviceCount->text(), QString::number(initialDevices.size()));
    QCOMPARE(supportedCount->text(), QString::number(initialSupported));
    QCOMPARE(limitedCount->text(), QString::number(initialDevices.size() - initialSupported));
    QCOMPARE(vendorCount->text(), QString::number(initialVendors.size()));
    QCOMPARE(sourceCount->text(), QString::number(initialSources.size()));
    QCOMPARE(importFiles->text(), QString("Import ESI Files..."));
    QCOMPARE(reload->text(), QString("Reload Device Descriptions"));
    QCOMPARE(cancel->text(), QString("Cancel"));
    QVERIFY(content->acceptDrops());
    QVERIFY(importFiles->isEnabled());
    QVERIFY(reload->isEnabled());
    QVERIFY(!cancel->isEnabled());
    QVERIFY(result->isReadOnly());
    QVERIFY(!content->accessibleName().isEmpty());
    QVERIFY(!importFiles->accessibleName().isEmpty());
    QVERIFY(!reload->accessibleName().isEmpty());
    QVERIFY(!cancel->accessibleName().isEmpty());
    QVERIFY(!progress->accessibleName().isEmpty());
    QVERIFY(!result->accessibleName().isEmpty());
    QVERIFY(content->isVisible());
    QVERIFY(!propertyTree->isVisible());
    QCOMPARE(propertyTree->topLevelItemCount(), 0);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray uniqueEsi = deviceEsi();
    uniqueEsi.replace("#x00005678", "#x7A110001");
    uniqueEsi.replace("#x00000011", "#x0000A501");
    uniqueEsi.replace("AX5000", "EL-REPOSITORY");
    uniqueEsi.replace("Workbench Servo", "Repository Servo / 设备库伺服");
    const Utils::FilePath validPath = Utils::FilePath::fromString(directory.path())
                                          .pathAppended("repository-valid.xml");
    const Utils::FilePath invalidPath = Utils::FilePath::fromString(directory.path())
                                            .pathAppended("repository-invalid.xml");
    QVERIFY_RESULT(validPath.writeFileContents(uniqueEsi));
    QVERIFY_RESULT(invalidPath.writeFileContents("<EtherCATInfo>"));

    QMimeData mimeData;
    mimeData.setUrls(
        {QUrl::fromLocalFile(validPath.toFSPathString()),
         QUrl::fromLocalFile(invalidPath.toFSPathString())});
    QDragEnterEvent dragEnter(
        QPoint(8, 8), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(content, &dragEnter);
    QVERIFY(dragEnter.isAccepted());
    QDropEvent drop(
        QPointF(8, 8), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(content, &drop);
    QVERIFY(drop.isAccepted());

    QTRY_VERIFY_WITH_TIMEOUT(
        operation->text().contains("Import complete", Qt::CaseInsensitive), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        result->toPlainText().contains("Requested files: 2"), 15000);
    QVERIFY(result->toPlainText().contains("Imported devices: 1"));
    QVERIFY(result->toPlainText().contains("Failed files: 1"));
    QVERIFY(result->toPlainText().contains("repository-invalid.xml"));
    QTRY_COMPARE(repository->devices().size(), initialDevices.size() + 1);
    QTRY_COMPARE(deviceCount->text(), QString::number(initialDevices.size() + 1));
    QVERIFY(importFiles->isEnabled());
    QVERIFY(reload->isEnabled());
    QVERIFY(!cancel->isEnabled());
    QVERIFY(progress->value() == progress->maximum());
    QTRY_VERIFY(findByDisplayText(controller.treeModel(), "Repository Servo / 设备库伺服").isValid());

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_REPOSITORY_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    reload->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        operation->text().contains("Reload complete", Qt::CaseInsensitive), 15000);
    QVERIFY(result->toPlainText().contains("Failed files: 0"));
    QCOMPARE(repository->devices().size(), initialDevices.size() + 1);
    QCOMPARE(deviceCount->text(), QString::number(initialDevices.size() + 1));
    QVERIFY(importFiles->isEnabled());
    QVERIFY(reload->isEnabled());
    QVERIFY(!cancel->isEnabled());

    ControlledDeviceRepositoryProvider controlledRepository;
    EsiRepositoryPage controlledPage(&controlledRepository);
    QPushButton *controlledReload
        = controlledPage.findChild<QPushButton *>("EtherCATEsiRepositoryReload");
    QPushButton *controlledCancel
        = controlledPage.findChild<QPushButton *>("EtherCATEsiRepositoryCancel");
    QProgressBar *controlledProgress
        = controlledPage.findChild<QProgressBar *>("EtherCATEsiRepositoryProgress");
    QLabel *controlledOperation
        = controlledPage.findChild<QLabel *>("EtherCATEsiRepositoryOperation");
    QPlainTextEdit *controlledResult
        = controlledPage.findChild<QPlainTextEdit *>("EtherCATEsiRepositoryResult");
    QVERIFY(controlledReload);
    QVERIFY(controlledCancel);
    QVERIFY(controlledProgress);
    QVERIFY(controlledOperation);
    QVERIFY(controlledResult);
    controlledReload->click();
    QVERIFY(controlledRepository.job);
    QCOMPARE(controlledProgress->minimum(), 0);
    QCOMPARE(controlledProgress->maximum(), 4);
    QCOMPARE(controlledProgress->value(), 1);
    QVERIFY(controlledCancel->isEnabled());
    controlledCancel->click();
    QCOMPARE(controlledRepository.job->cancelCalls, 1);
    QCOMPARE(controlledRepository.job->state(), Core::DeviceImportState::Finished);
    QCOMPARE(controlledOperation->text(), QString("Reload canceled"));
    QVERIFY(controlledResult->toPlainText().contains("Canceled: Yes"));
    QVERIFY(controlledReload->isEnabled());
    QVERIFY(!controlledCancel->isEnabled());

    controller.selectionService()->clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEsiDeviceGeneralWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);
    QTRY_VERIFY(!repository->isIndexing());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray uniqueEsi = deviceEsi();
    uniqueEsi.replace("#x00005678", "#x7A120001");
    uniqueEsi.replace("#x00000011", "#x0000A502");
    uniqueEsi.replace("AX5000", "EL-CATALOGUE");
    uniqueEsi.replace("Workbench Servo", "ESI Catalogue Servo / 设备说明");
    uniqueEsi.replace(
        "<CycleTimeSync0>125000</CycleTimeSync0>",
        "<CycleTimeSync0 Factor=\"1\">125000</CycleTimeSync0>");
    uniqueEsi.replace("</Device>", "<Modules/></Device>");
    const Utils::FilePath sourcePath = Utils::FilePath::fromString(directory.path())
                                           .pathAppended("esi-device-general.xml");
    QVERIFY_RESULT(sourcePath.writeFileContents(uniqueEsi));
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles({sourcePath}));
    QCOMPARE(importResult.requestedFiles, 1);
    QCOMPARE(importResult.importedDevices, 1);
    QCOMPARE(importResult.failedFiles, 0);
    QCOMPARE(importResult.affectedDeviceIds.size(), 1);

    const Data::NodeId deviceId = importResult.affectedDeviceIds.first();
    const std::optional<Data::DeviceDescription> device = repository->device(deviceId);
    QVERIFY(device);
    QVERIFY(!device->summary.supported);
    QCOMPARE(device->warnings.size(), 1);
    QCOMPARE(device->unsupportedFeatures.size(), 1);
    QTRY_VERIFY(findById(controller.treeModel(), deviceId).isValid());
    controller.selectionService()->setCurrentNodeId(deviceId);

    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QWidget *content = page->findChild<QWidget *>("EtherCATEsiDeviceGeneralContent");
    QLabel *description = page->findChild<QLabel *>("EtherCATEsiDeviceGeneralDescription");
    QGroupBox *identity = page->findChild<QGroupBox *>("EtherCATEsiDeviceGeneralIdentity");
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralName");
    QLineEdit *type = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralType");
    QLineEdit *objectId = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralObjectId");
    QLineEdit *vendor = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralVendor");
    QLineEdit *product = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralProduct");
    QLineEdit *revision = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralRevision");
    QLineEdit *group = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralGroup");
    QGroupBox *capabilities
        = page->findChild<QGroupBox *>("EtherCATEsiDeviceGeneralCapabilities");
    QLineEdit *syncManagers
        = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralSyncManagers");
    QLineEdit *rxPdos = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralRxPdos");
    QLineEdit *txPdos = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralTxPdos");
    QLineEdit *coe = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralCoe");
    QLabel *coeDetails = page->findChild<QLabel *>("EtherCATEsiDeviceGeneralCoeDetails");
    QLineEdit *startup = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralStartup");
    QLineEdit *dcModes = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralDcModes");
    QGroupBox *qualification
        = page->findChild<QGroupBox *>("EtherCATEsiDeviceGeneralQualification");
    QLineEdit *support = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralSupport");
    QLineEdit *warningCount
        = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralWarningCount");
    QLineEdit *unsupportedCount
        = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralUnsupportedCount");
    QLabel *warningDetails
        = page->findChild<QLabel *>("EtherCATEsiDeviceGeneralWarningDetails");
    QLabel *unsupportedDetails
        = page->findChild<QLabel *>("EtherCATEsiDeviceGeneralUnsupportedDetails");
    QGroupBox *source = page->findChild<QGroupBox *>("EtherCATEsiDeviceGeneralSource");
    QScrollArea *scrollArea
        = page->findChild<QScrollArea *>("EtherCATEsiDeviceGeneralScrollArea");
    QLineEdit *sourceFile = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralSourceFile");
    QLineEdit *sourceHash = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralSourceHash");
    QLineEdit *imported = page->findChild<QLineEdit *>("EtherCATEsiDeviceGeneralImported");
    QTreeWidget *propertyTree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(content);
    QVERIFY(description);
    QVERIFY(identity);
    QVERIFY(name);
    QVERIFY(type);
    QVERIFY(objectId);
    QVERIFY(vendor);
    QVERIFY(product);
    QVERIFY(revision);
    QVERIFY(group);
    QVERIFY(capabilities);
    QVERIFY(syncManagers);
    QVERIFY(rxPdos);
    QVERIFY(txPdos);
    QVERIFY(coe);
    QVERIFY(coeDetails);
    QVERIFY(startup);
    QVERIFY(dcModes);
    QVERIFY(qualification);
    QVERIFY(support);
    QVERIFY(warningCount);
    QVERIFY(unsupportedCount);
    QVERIFY(warningDetails);
    QVERIFY(unsupportedDetails);
    QVERIFY(source);
    QVERIFY(scrollArea);
    QVERIFY(sourceFile);
    QVERIFY(sourceHash);
    QVERIFY(imported);
    QVERIFY(propertyTree);

    QVERIFY(content->isVisible());
    QVERIFY(description->text().contains("read-only", Qt::CaseInsensitive));
    QCOMPARE(name->text(), QString("ESI Catalogue Servo / 设备说明"));
    QCOMPARE(type->text(), QString("EL-CATALOGUE"));
    QCOMPARE(objectId->text(), deviceId.toString());
    QCOMPARE(vendor->text(), QString("0x00000002"));
    QCOMPARE(product->text(), QString("0x7a120001"));
    QCOMPARE(revision->text(), QString("0x0000a502"));
    QCOMPARE(group->text(), QString("Drives"));
    QCOMPARE(syncManagers->text(), QString("2"));
    QVERIFY(rxPdos->text().contains("1 PDO"));
    QVERIFY(rxPdos->text().contains("1 entry"));
    QVERIFY(txPdos->text().contains("1 PDO"));
    QVERIFY(txPdos->text().contains("1 entry"));
    QCOMPARE(coe->text(), QString("Supported"));
    QVERIFY(coeDetails->text().contains("SDO Info: No"));
    QVERIFY(coeDetails->text().contains("PDO Assignment: No"));
    QVERIFY(coeDetails->text().contains("PDO Configuration: No"));
    QVERIFY(coeDetails->text().contains("Complete Access: No"));
    QCOMPARE(startup->text(), QString("3"));
    QCOMPARE(dcModes->text(), QString("2"));
    QCOMPARE(support->text(), QString("Limited"));
    QCOMPARE(warningCount->text(), QString("1"));
    QCOMPARE(unsupportedCount->text(), QString("1"));
    QVERIFY(warningDetails->text().contains("formula attributes"));
    QVERIFY(warningDetails->text().contains("source XML"));
    QVERIFY(unsupportedDetails->text().contains("Modules structure"));
    QVERIFY(unsupportedDetails->text().contains("not expanded"));
    QCOMPARE(sourceFile->text(), device->sourcePath);
    QCOMPARE(sourceHash->text(), QString::fromLatin1(device->sourceSha256.toHex()));
    QCOMPARE(imported->text(), device->importedAt.toLocalTime().toString(Qt::ISODate));
    QVERIFY(name->isReadOnly());
    QVERIFY(type->isReadOnly());
    QVERIFY(sourceFile->isReadOnly());
    QVERIFY(sourceHash->isReadOnly());
    QVERIFY(!content->accessibleName().isEmpty());
    QVERIFY(!name->accessibleName().isEmpty());
    QVERIFY(!warningDetails->accessibleName().isEmpty());
    QVERIFY(!propertyTree->isVisible());
    QCOMPARE(propertyTree->topLevelItemCount(), 0);
    QVERIFY(scrollArea->verticalScrollBar()->maximum() > 0);

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_ESI_DEVICE_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }
    const QString bottomRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_ESI_DEVICE_BOTTOM_RENDER_PATH");
    if (!bottomRenderPath.isEmpty()) {
        scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(bottomRenderPath), qPrintable(bottomRenderPath));
        scrollArea->verticalScrollBar()->setValue(0);
    }

    GeneralPage unavailablePage(&controller);
    unavailablePage.resize(900, 600);
    unavailablePage.setContext(
        {{}, Data::NodeId::create(), Core::WorkbenchNodeKind::Device, "Removed ESI device"});
    QLabel *unavailable = unavailablePage.findChild<QLabel *>(
        "EtherCATEsiDeviceGeneralUnavailable");
    QVERIFY(unavailable);
    QVERIFY(unavailable->isVisibleTo(&unavailablePage));
    QVERIFY(unavailable->text().contains("unavailable", Qt::CaseInsensitive));

    controller.selectionService()->clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEditableTargetGeneralWorkflow()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary
        device{Data::NodeId::create(), {2, 0x5678, 0x11}, "Mock Servo", "AX5000", "Drives", true};
    const TestProjectFile file = writeProjectWithSlave(directory, device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.targetId).isValid());

    controller.selectionService()->setCurrentNodeId(file.targetId);
    DetailsView details(&controller);
    details.resize(1100, 720);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QWidget *targetContent = page->findChild<QWidget *>("EtherCATTargetGeneralContent");
    QGroupBox *versionGroup = page->findChild<QGroupBox *>("EtherCATTargetGeneralVersion");
    QVERIFY(targetContent);
    QVERIFY(versionGroup);

    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATTargetGeneralName");
    QLabel *identity = page->findChild<QLabel *>("EtherCATTargetGeneralIdentity");
    QPushButton *chooseTarget
        = page->findChild<QPushButton *>("EtherCATTargetGeneralChooseTarget");
    QLabel *engineering = page->findChild<QLabel *>("EtherCATTargetGeneralEngineering");
    QLabel *targetRuntime = page->findChild<QLabel *>("EtherCATTargetGeneralTargetRuntime");
    QLabel *localRuntime = page->findChild<QLabel *>("EtherCATTargetGeneralLocalRuntime");
    QLabel *projectVersion = page->findChild<QLabel *>("EtherCATTargetGeneralProjectVersion");
    QCheckBox *pinVersion = page->findChild<QCheckBox *>("EtherCATTargetGeneralPinVersion");
    QTreeWidget *propertyTree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(title);
    QVERIFY(name);
    QVERIFY(identity);
    QVERIFY(chooseTarget);
    QVERIFY(engineering);
    QVERIFY(targetRuntime);
    QVERIFY(localRuntime);
    QVERIFY(projectVersion);
    QVERIFY(pinVersion);
    QVERIFY(propertyTree);

    QCOMPARE(name->text(), QString("Offline Controller"));
    QVERIFY(!name->isReadOnly());
    QVERIFY(identity->text().contains(file.targetId.toString()));
    QVERIFY(identity->text().contains(QString("Offline / Mock target")));
    QVERIFY(!chooseTarget->isEnabled());
    QVERIFY(!chooseTarget->accessibleDescription().isEmpty());
    QVERIFY(!engineering->text().isEmpty());
    QCOMPARE(targetRuntime->text(), QString("Not assigned (offline)"));
    QCOMPARE(localRuntime->text(), QString("Not available (phase 1)"));
    QCOMPARE(projectVersion->text(), QString("Format 2 · Workbench Test"));
    QVERIFY(!pinVersion->isEnabled());
    QVERIFY(!pinVersion->accessibleDescription().isEmpty());
    QVERIFY(!name->accessibleName().isEmpty());
    QVERIFY(targetContent->isVisible());
    QVERIFY(versionGroup->isVisible());
    QVERIFY(!propertyTree->isVisible());

    const QString renderPath = qEnvironmentVariable("ETHERCAT_WORKBENCH_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    const auto currentTargetName = [&]() {
        const std::optional<Data::ProjectSnapshot> project = projectService->project(file.projectId);
        if (!project)
            return QString();
        const auto target
            = std::find_if(project->nodes.cbegin(), project->nodes.cend(), [&file](const auto &node) {
                  return node.id == file.targetId;
              });
        return target == project->nodes.cend() ? QString() : target->name;
    };
    const QString renamed = QString::fromUtf8("Zynq-7020 Target / 控制器");
    name->setText("  " + renamed + "  ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(currentTargetName(), renamed);
    QTRY_COMPARE(controller.treeModel()->indexForNodeId(file.targetId).data().toString(), renamed);
    QTRY_COMPARE(title->text(), renamed);
    QTRY_COMPARE(name->text(), renamed);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.targetId);
    QVERIFY(projectService->project(file.projectId)->modified);
    QVERIFY(projectService->canUndoProject(file.projectId));

    const Utils::Result<> undoRename = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoRename);
    QTRY_COMPARE(currentTargetName(), QString("Offline Controller"));
    QTRY_COMPARE(name->text(), QString("Offline Controller"));
    QTRY_COMPARE(title->text(), QString("Offline Controller"));
    const Utils::Result<> redoRename = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoRename);
    QTRY_COMPARE(currentTargetName(), renamed);
    QTRY_COMPARE(name->text(), renamed);
    QTRY_COMPARE(title->text(), renamed);

    name->setText("   ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(name->text(), renamed);
    QCOMPARE(currentTargetName(), renamed);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testEditableMasterGeneralWorkflow()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary
        device{Data::NodeId::create(), {2, 0x5678, 0x11}, "Mock Servo", "AX5000", "Drives", true};
    const TestProjectFile file = writeProjectWithSlave(directory, device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.masterId).isValid());

    controller.selectionService()->setCurrentNodeId(file.masterId);
    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QWidget *masterForm = page->findChild<QWidget *>("EtherCATMasterGeneralForm");
    QWidget *summaryForm = page->findChild<QWidget *>("EtherCATMasterConfigurationSummary");
    QVERIFY(masterForm);
    QVERIFY(summaryForm);

    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATMasterGeneralName");
    QLineEdit *id = page->findChild<QLineEdit *>("EtherCATMasterGeneralId");
    QLineEdit *objectId = page->findChild<QLineEdit *>("EtherCATMasterGeneralObjectId");
    QLineEdit *type = page->findChild<QLineEdit *>("EtherCATMasterGeneralType");
    QPlainTextEdit *comment = page->findChild<QPlainTextEdit *>("EtherCATMasterGeneralComment");
    QCheckBox *disabled = page->findChild<QCheckBox *>("EtherCATMasterGeneralDisabled");
    QCheckBox *createSymbols = page->findChild<QCheckBox *>("EtherCATMasterGeneralCreateSymbols");
    QLineEdit *cycle = page->findChild<QLineEdit *>("EtherCATMasterGeneralCycle");
    QLineEdit *slaveCount = page->findChild<QLineEdit *>("EtherCATMasterGeneralSlaveCount");
    QLineEdit *status = page->findChild<QLineEdit *>("EtherCATMasterGeneralStatus");
    QTreeWidget *propertyTree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(title);
    QVERIFY(name);
    QVERIFY(id);
    QVERIFY(objectId);
    QVERIFY(type);
    QVERIFY(comment);
    QVERIFY(disabled);
    QVERIFY(createSymbols);
    QVERIFY(cycle);
    QVERIFY(slaveCount);
    QVERIFY(status);
    QVERIFY(propertyTree);

    const QString renderPath = qEnvironmentVariable("ETHERCAT_WORKBENCH_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    QCOMPARE(name->text(), QString("EtherCAT Master"));
    QCOMPARE(id->text(), QString("1"));
    QCOMPARE(objectId->text(), file.masterId.toString());
    QCOMPARE(type->text(), QString("EtherCAT Master"));
    QVERIFY(!name->isReadOnly());
    QVERIFY(id->isReadOnly());
    QVERIFY(objectId->isReadOnly());
    QVERIFY(type->isReadOnly());
    QVERIFY(comment->isReadOnly());
    QVERIFY(!disabled->isEnabled());
    QVERIFY(!createSymbols->isEnabled());
    QCOMPARE(cycle->text(), QString("Not assigned (offline)"));
    QCOMPARE(slaveCount->text(), QString("1"));
    QCOMPARE(
        status->text(),
        controller.treeModel()
            ->indexForNodeId(file.masterId)
            .data(WorkbenchTreeModel::StatusRole)
            .toString());
    QVERIFY(!name->accessibleName().isEmpty());
    QVERIFY(!cycle->accessibleName().isEmpty());
    QVERIFY(!status->accessibleName().isEmpty());
    QVERIFY(!comment->accessibleDescription().isEmpty());
    QVERIFY(!disabled->accessibleDescription().isEmpty());
    QVERIFY(!createSymbols->accessibleDescription().isEmpty());
    QVERIFY(masterForm->isVisible());
    QVERIFY(summaryForm->isVisible());
    QVERIFY(!propertyTree->isVisible());

    const auto currentMasterName = [&]() {
        const std::optional<Data::ProjectSnapshot> project = projectService->project(file.projectId);
        if (!project)
            return QString();
        const auto master
            = std::find_if(project->nodes.cbegin(), project->nodes.cend(), [&file](const auto &node) {
                  return node.id == file.masterId;
              });
        return master == project->nodes.cend() ? QString() : master->name;
    };
    const QString renamed = QString::fromUtf8("EtherCAT Device 1 / 主站");
    name->setText("  " + renamed + "  ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(currentMasterName(), renamed);
    QTRY_COMPARE(controller.treeModel()->indexForNodeId(file.masterId).data().toString(), renamed);
    QTRY_COMPARE(title->text(), renamed);
    QTRY_COMPARE(name->text(), renamed);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.masterId);
    QVERIFY(projectService->project(file.projectId)->modified);
    QVERIFY(projectService->canUndoProject(file.projectId));

    const Utils::Result<> undoRename = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoRename);
    QTRY_COMPARE(currentMasterName(), QString("EtherCAT Master"));
    QTRY_COMPARE(name->text(), QString("EtherCAT Master"));
    QTRY_COMPARE(title->text(), QString("EtherCAT Master"));
    const Utils::Result<> redoRename = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoRename);
    QTRY_COMPARE(currentMasterName(), renamed);
    QTRY_COMPARE(name->text(), renamed);
    QTRY_COMPARE(title->text(), renamed);

    name->setText("   ");
    QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
    QTRY_COMPARE(name->text(), renamed);
    QCOMPARE(currentMasterName(), renamed);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testTwinCatMasterEtherCATWorkflow()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary
        device{Data::NodeId::create(), {2, 0x5678, 0x11}, "Mock Servo", "AX5000", "Drives", true};
    const TestProjectFile file = writeProjectWithSlave(directory, device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.masterId).isValid());

    controller.selectionService()->setCurrentNodeId(file.masterId);
    DetailsView details(&controller);
    details.resize(1180, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::ETHERCAT_PAGE_ID).toString());
    QVERIFY(page);
    details.tabWidget()->setCurrentWidget(page);
    QTRY_VERIFY(page->isVisible());

    QWidget *masterForm = page->findChild<QWidget *>("EtherCATMasterEthercatForm");
    QLineEdit *netId = page->findChild<QLineEdit *>("EtherCATMasterEthercatNetId");
    QPushButton *advanced
        = page->findChild<QPushButton *>("EtherCATMasterEthercatAdvancedSettings");
    QPushButton *exportConfiguration
        = page->findChild<QPushButton *>("EtherCATMasterEthercatExportConfiguration");
    QPushButton *syncUnit
        = page->findChild<QPushButton *>("EtherCATMasterEthercatSyncUnitAssignment");
    QPushButton *topology
        = page->findChild<QPushButton *>("EtherCATMasterEthercatTopology");
    QLabel *frameState = page->findChild<QLabel *>("EtherCATMasterEthercatFrameState");
    QWidget *slaveForm = page->findChild<QWidget *>("EtherCATEthercatSlaveForm");
    QTreeWidget *frames = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(masterForm);
    QVERIFY(netId);
    QVERIFY(advanced);
    QVERIFY(exportConfiguration);
    QVERIFY(syncUnit);
    QVERIFY(topology);
    QVERIFY(frameState);
    QVERIFY(slaveForm);
    QVERIFY(frames);

    QCOMPARE(netId->text(), QString("Not assigned (offline)"));
    QVERIFY(netId->isReadOnly());
    QVERIFY(!netId->accessibleName().isEmpty());
    QVERIFY(!advanced->isEnabled());
    QVERIFY(!exportConfiguration->isEnabled());
    QVERIFY(!syncUnit->isEnabled());
    QVERIFY(topology->isEnabled());
    QVERIFY(!advanced->accessibleDescription().isEmpty());
    QVERIFY(!exportConfiguration->accessibleDescription().isEmpty());
    QVERIFY(!syncUnit->accessibleDescription().isEmpty());
    QVERIFY(!topology->accessibleDescription().isEmpty());
    QVERIFY(masterForm->isVisible());
    QVERIFY(slaveForm->isHidden());
    QVERIFY(frames->isVisible());
    QVERIFY(frameState->isVisible());
    QVERIFY(frameState->text().contains("not generated", Qt::CaseInsensitive));
    QVERIFY(!frames->accessibleName().isEmpty());
    QCOMPARE(frames->topLevelItemCount(), 0);
    QCOMPARE(frames->columnCount(), 10);
    const QStringList expectedHeaders = {
        "Frame",
        "Cmd",
        "Addr",
        "Len",
        "WC",
        "Sync Unit",
        "Cycle (ms)",
        "Utilization (%)",
        "Size / Duration (µs)",
        "Map Id",
    };
    QStringList actualHeaders;
    for (int column = 0; column < frames->columnCount(); ++column)
        actualHeaders.append(frames->headerItem()->text(column));
    QCOMPARE(actualHeaders, expectedHeaders);

    const QString renderPath = qEnvironmentVariable("ETHERCAT_WORKBENCH_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    bool populatedTopologyInspected = false;
    QTimer::singleShot(0, &details, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        QLabel *summary = dialog->findChild<QLabel *>("EtherCATMasterTopologySummary");
        QTreeWidget *table = dialog->findChild<QTreeWidget *>("EtherCATMasterTopologyTable");
        if (summary && table && summary->text().contains("offline", Qt::CaseInsensitive)
            && table->topLevelItemCount() == 1) {
            const QTreeWidgetItem *row = table->topLevelItem(0);
            populatedTopologyInspected
                = row->text(0) == "0" && row->text(1) == "Configured Servo"
                  && row->text(2) == "0x0000" && row->text(3) == "EtherCAT Master"
                  && row->text(4) == "Not modeled" && row->text(5) == "0x00000002"
                  && row->text(6) == "0x00005678" && row->text(7) == "0x00000011"
                  && row->text(8) == "3" && row->text(9) == "Offline configured";
        }
        const QString topologyRenderPath
            = qEnvironmentVariable("ETHERCAT_WORKBENCH_TOPOLOGY_RENDER_PATH");
        if (!topologyRenderPath.isEmpty())
            populatedTopologyInspected &= dialog->grab().save(topologyRenderPath);
        dialog->accept();
    });
    topology->click();
    QVERIFY(populatedTopologyInspected);

    QVERIFY_RESULT(projectService->replaceOfflineSlaves(file.projectId, file.masterId, {}));
    QTRY_VERIFY(projectService->project(file.projectId)->slaves.isEmpty());
    QTRY_VERIFY(topology->isEnabled());
    bool emptyTopologyInspected = false;
    QTimer::singleShot(0, &details, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        QLabel *summary = dialog->findChild<QLabel *>("EtherCATMasterTopologySummary");
        QTreeWidget *table = dialog->findChild<QTreeWidget *>("EtherCATMasterTopologyTable");
        emptyTopologyInspected = summary && table
                                 && summary->text().contains(
                                     "No configured slaves", Qt::CaseInsensitive)
                                 && table->topLevelItemCount() == 0;
        dialog->accept();
    });
    topology->click();
    QVERIFY(emptyTopologyInspected);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testMasterTopologyDialogBounds()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary
        device{Data::NodeId::create(), {2, 0x5678, 0x11}, "Mock Servo", "AX5000", "Drives", true};
    const TestProjectFile file = writeProjectWithSlave(directory, device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (projectService->project(file.projectId)) {
            ProjectExplorer::ProjectManager::removeProject(opened.project());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());

    const QString firstName = QString::fromUtf8("包装线一号轴站 Ω / ")
                              + QString(512, QChar(u'甲')) + " / First";
    const QString secondName = QString::fromUtf8("包装线二号轴站 Ω / ")
                               + QString(512, QLatin1Char('B')) + " / Second";
    QList<Data::OfflineSlaveConfiguration> slaves = projectService->project(file.projectId)->slaves;
    QCOMPARE(slaves.size(), 1);
    slaves[0].name = firstName;
    Data::OfflineSlaveConfiguration secondSlave = slaves.first();
    secondSlave.id = Data::NodeId::create();
    secondSlave.position = 1;
    secondSlave.serialNumber = 18;
    secondSlave.alias = 4;
    secondSlave.name = secondName;
    slaves.append(secondSlave);
    QVERIFY_RESULT(projectService->replaceOfflineSlaves(file.projectId, file.masterId, slaves));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    controller.selectionService()->setCurrentNodeId(file.masterId);
    DetailsView details(&controller);
    details.resize(1180, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::ETHERCAT_PAGE_ID).toString());
    QVERIFY(page);
    details.tabWidget()->setCurrentWidget(page);
    QTRY_VERIFY(page->isVisible());
    QPushButton *topology
        = page->findChild<QPushButton *>("EtherCATMasterEthercatTopology");
    QVERIFY(topology);

    bool dialogOpened = false;
    bool rowsPreserved = false;
    bool firstColumnVisible = false;
    bool lastColumnVisible = false;
    bool closeButtonUsable = false;
    bool dialogTimedOut = false;
    QSize dialogSize;
    QRect dialogGeometry;
    QRect dialogFrameGeometry;
    QRect availableGeometry;
    int tableMinimumWidth = -1;
    int horizontalMaximum = -1;
    QTimer inspectionTimer;
    inspectionTimer.setInterval(0);
    connect(&inspectionTimer, &QTimer::timeout, &details, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        inspectionTimer.stop();
        dialogOpened = true;
        QTreeWidget *table = dialog->findChild<QTreeWidget *>("EtherCATMasterTopologyTable");
        QDialogButtonBox *buttons
            = dialog->findChild<QDialogButtonBox *>("EtherCATMasterTopologyButtons");
        if (table && buttons) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QScreen *screen = dialog->screen();
            if (screen)
                availableGeometry = screen->availableGeometry();
            dialogSize = dialog->size();
            dialogGeometry = dialog->geometry();
            dialogFrameGeometry = dialog->frameGeometry();
            tableMinimumWidth = table->minimumWidth();
            horizontalMaximum = table->horizontalScrollBar()->maximum();
            if (table->topLevelItemCount() == 2) {
                const QTreeWidgetItem *first = table->topLevelItem(0);
                const QTreeWidgetItem *second = table->topLevelItem(1);
                rowsPreserved = first->text(0) == "0" && first->text(1) == firstName
                                && first->text(3) == "EtherCAT Master"
                                && first->text(4) == "Not modeled"
                                && first->text(9) == "Offline configured"
                                && second->text(0) == "1" && second->text(1) == secondName
                                && second->text(3) == firstName
                                && second->text(4) == "Not modeled"
                                && second->text(9) == "Offline configured";
            }

            table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->minimum());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QModelIndex firstIndex = table->model()->index(0, 0);
            firstColumnVisible = table->viewport()->rect().contains(table->visualRect(firstIndex));
            table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->maximum());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QModelIndex lastIndex = table->model()->index(1, 9);
            lastColumnVisible = table->viewport()->rect().contains(table->visualRect(lastIndex));

            QAbstractButton *closeButton = buttons->button(QDialogButtonBox::Close);
            closeButtonUsable = closeButton && closeButton->isVisible() && closeButton->isEnabled();
            const QString renderPath
                = qEnvironmentVariable("ETHERCAT_WORKBENCH_TOPOLOGY_BOUNDS_RENDER_PATH");
            if (!renderPath.isEmpty())
                rowsPreserved &= dialog->grab().save(renderPath);
        }
        dialog->reject();
    });
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &details, [&] {
        dialogTimedOut = true;
        if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    inspectionTimer.start();
    timeoutTimer.start(5000);
    topology->click();
    inspectionTimer.stop();
    timeoutTimer.stop();

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QVERIFY(dialogOpened);
    QVERIFY(!dialogTimedOut);
    QVERIFY(rowsPreserved);
    QVERIFY(availableGeometry.isValid());
    QVERIFY2(
        dialogSize.width() <= availableGeometry.width(),
        qPrintable(
            QString("Topology dialog width %1 exceeds available screen width %2")
                .arg(dialogSize.width())
                .arg(availableGeometry.width())));
    QVERIFY2(
        dialogSize.height() <= availableGeometry.height(),
        qPrintable(
            QString("Topology dialog height %1 exceeds available screen height %2")
                .arg(dialogSize.height())
                .arg(availableGeometry.height())));
    const auto rectText = [](const QRect &rect) {
        return QString("(%1,%2 %3x%4)")
            .arg(rect.x())
            .arg(rect.y())
            .arg(rect.width())
            .arg(rect.height());
    };
    QVERIFY2(
        availableGeometry.contains(dialogGeometry),
        qPrintable(
            QString("Topology dialog geometry %1 is outside available geometry %2")
                .arg(rectText(dialogGeometry))
                .arg(rectText(availableGeometry))));
    QVERIFY2(
        availableGeometry.contains(dialogFrameGeometry),
        qPrintable(
            QString("Topology dialog frame %1 is outside available geometry %2")
                .arg(rectText(dialogFrameGeometry))
                .arg(rectText(availableGeometry))));
    QVERIFY2(
        tableMinimumWidth <= availableGeometry.width(),
        qPrintable(
            QString("Topology table minimum width %1 exceeds available screen width %2")
                .arg(tableMinimumWidth)
                .arg(availableGeometry.width())));
    QVERIFY(horizontalMaximum > 0);
    QVERIFY(firstColumnVisible);
    QVERIFY(lastColumnVisible);
    QVERIFY(closeButtonUsable);
}

void EtherCATWorkbenchTests::testMasterTopologyCellAccessibility()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary
        device{Data::NodeId::create(), {2, 0x5678, 0x11}, "Mock Servo", "AX5000", "Drives", true};
    const TestProjectFile file = writeProjectWithSlave(directory, device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (projectService->project(file.projectId)) {
            ProjectExplorer::ProjectManager::removeProject(opened.project());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());

    const QString firstName
        = QString::fromUtf8("包装线一号轴站 / 長い名前 / Ω / %1 / %2 / %% / ")
          + QString(256, QChar(u'甲')) + " / First";
    const QString secondName
        = QString::fromUtf8("包装线二号轴站 / 長い名前 / Ω / %1 / %2 / %% / ")
          + QString(256, QLatin1Char('B')) + " / Second";
    QList<Data::OfflineSlaveConfiguration> slaves = projectService->project(file.projectId)->slaves;
    QCOMPARE(slaves.size(), 1);
    slaves[0].name = firstName;
    Data::OfflineSlaveConfiguration secondSlave = slaves.first();
    secondSlave.id = Data::NodeId::create();
    secondSlave.position = 1;
    secondSlave.serialNumber = 18;
    secondSlave.alias = 4;
    secondSlave.name = secondName;
    slaves.append(secondSlave);
    QVERIFY_RESULT(projectService->replaceOfflineSlaves(file.projectId, file.masterId, slaves));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.size(), 2);

    controller.selectionService()->setCurrentNodeId(file.masterId);
    DetailsView details(&controller);
    details.resize(1180, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::ETHERCAT_PAGE_ID).toString());
    QVERIFY(page);
    details.tabWidget()->setCurrentWidget(page);
    QTRY_VERIFY(page->isVisible());
    QPushButton *topology
        = page->findChild<QPushButton *>("EtherCATMasterEthercatTopology");
    QVERIFY(topology);

    const QStringList expectedHeaders = {
        "Position",
        "Name",
        "Auto Inc Addr",
        "Previous",
        "Port",
        "Vendor",
        "Product",
        "Revision",
        "Alias",
        "Status",
    };
    const QList<QStringList> expectedRows = {
        {"0",
         firstName,
         "0x0000",
         "EtherCAT Master",
         "Not modeled",
         "0x00000002",
         "0x00005678",
         "0x00000011",
         "3",
         "Offline configured"},
        {"1",
         secondName,
         "0xffff",
         firstName,
         "Not modeled",
         "0x00000002",
         "0x00005678",
         "0x00000011",
         "4",
         "Offline configured"},
    };

    bool dialogOpened = false;
    bool dialogTimedOut = false;
    QString verificationError;
    QTimer inspectionTimer;
    inspectionTimer.setInterval(0);
    connect(&inspectionTimer, &QTimer::timeout, &details, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        inspectionTimer.stop();
        dialogOpened = true;
        QTreeWidget *table = dialog->findChild<QTreeWidget *>("EtherCATMasterTopologyTable");
        const auto verifyTable = [&]() -> QString {
            if (!table)
                return "Topology table is missing";
            if (table->topLevelItemCount() != expectedRows.size())
                return "Topology table has the wrong row count";
            if (table->columnCount() != expectedHeaders.size())
                return "Topology table has the wrong column count";

            for (int column = 0; column < table->columnCount(); ++column) {
                if (table->headerItem()->text(column) != expectedHeaders.at(column))
                    return "Topology table has an unexpected column heading";
            }
            for (int row = 0; row < table->topLevelItemCount(); ++row) {
                const QTreeWidgetItem *item = table->topLevelItem(row);
                if (item->flags() & Qt::ItemIsEditable)
                    return QString("Topology row %1 is editable").arg(row);
                for (int column = 0; column < table->columnCount(); ++column) {
                    const QString header = table->headerItem()->text(column);
                    const QString displayed = item->data(column, Qt::DisplayRole).toString();
                    const QVariant accessibleText = item->data(column, Qt::AccessibleTextRole);
                    const QVariant accessibleDescription
                        = item->data(column, Qt::AccessibleDescriptionRole);
                    const QVariant toolTip = item->data(column, Qt::ToolTipRole);
                    const QString cell
                        = QString("Topology row %1 / %2").arg(row).arg(header);
                    if (displayed != expectedRows.at(row).at(column))
                        return cell + " has an unexpected DisplayRole";
                    if (accessibleText.metaType().id() != int(QMetaType::QString))
                        return cell + " AccessibleTextRole is not a QString";
                    if (accessibleDescription.metaType().id() != int(QMetaType::QString))
                        return cell + " AccessibleDescriptionRole is not a QString";
                    if (toolTip.metaType().id() != int(QMetaType::QString))
                        return cell + " ToolTipRole is not a QString";
                    if (accessibleText.toString() != displayed)
                        return cell + " accessible text does not match DisplayRole";
                    const QString description = accessibleDescription.toString();
                    if (!description.contains(header))
                        return cell + " description omits the column heading";
                    const QString configuredSlaveIdentity
                        = QString("Position %1 (%2)").arg(item->text(0), item->text(1));
                    if (!description.contains(configuredSlaveIdentity)) {
                        return cell + " description omits the configured slave identity";
                    }
                    if (!description.contains("Complete value: " + displayed))
                        return cell + " description omits the complete value";
                    const QStringList boundaries = {
                        "read-only",
                        "offline",
                        "Project",
                        "physical ports",
                        "controller",
                        "network",
                        "physical hardware",
                    };
                    for (const QString &boundary : boundaries) {
                        if (!description.contains(boundary, Qt::CaseInsensitive))
                            return cell + " description omits " + boundary;
                    }
                    if (toolTip.toString() != description)
                        return cell + " tooltip and accessible description differ";
                }
            }
            if (table->accessibleName().isEmpty())
                return "Topology table has an empty accessible name";
            const QString widgetDescription = table->accessibleDescription();
            const QStringList widgetBoundaries = {
                "read-only",
                "offline",
                "Project",
                "physical ports",
                "controller",
                "network",
                "physical hardware",
            };
            for (const QString &boundary : widgetBoundaries) {
                if (!widgetDescription.contains(boundary, Qt::CaseInsensitive))
                    return "Topology table description omits " + boundary;
            }
            return {};
        };
        verificationError = verifyTable();
        const QString renderPath
            = qEnvironmentVariable("ETHERCAT_WORKBENCH_TOPOLOGY_A11Y_RENDER_PATH");
        if (verificationError.isEmpty() && !renderPath.isEmpty()
            && !dialog->grab().save(renderPath)) {
            verificationError = "Cannot save the topology accessibility render";
        }
        dialog->reject();
    });
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &details, [&] {
        dialogTimedOut = true;
        if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    inspectionTimer.start();
    timeoutTimer.start(5000);
    topology->click();
    inspectionTimer.stop();
    timeoutTimer.stop();

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QVERIFY(dialogOpened);
    QVERIFY(!dialogTimedOut);
    QVERIFY2(verificationError.isEmpty(), qPrintable(verificationError));
}

void EtherCATWorkbenchTests::testEtherCATSyncManagerCellAccessibility()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    QVERIFY(repository);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString longName = QString::fromUtf8(
                                 "Outputs / \u8d85\u957f SyncManager \u540d\u79f0 / "
                                 "\u9577\u3044\u540d\u524d / \u03a9 / %1 / %2 / %% / ")
                             + QString(256, QChar(u'\u754c')) + " / End";
    QByteArray accessibilityEsi = deviceEsi();
    const QByteArray productCode = "#x00005678";
    const QByteArray outputName = ">Outputs</Sm>";
    const QByteArray inputName = ">Inputs</Sm>";
    QCOMPARE(accessibilityEsi.count(productCode), 1);
    QCOMPARE(accessibilityEsi.count(outputName), 1);
    QCOMPARE(accessibilityEsi.count(inputName), 1);
    accessibilityEsi.replace(productCode, "#xA11E0003");
    QByteArray replacementName = ">";
    replacementName += longName.toUtf8();
    replacementName += "</Sm>";
    accessibilityEsi.replace(outputName, replacementName);
    accessibilityEsi.replace(inputName, "></Sm>");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("syncmanager-cell-accessibility.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(accessibilityEsi));
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0xA11E0003;
    });
    QVERIFY(device != devices.cend());

    Data::ProjectSnapshot project = projectSnapshot("SyncManager accessibility");
    const Data::NodeId master = masterId(project);
    const Data::NodeId slaveId = Data::NodeId::create();
    project.slaves = {
        {slaveId,
         master,
         0,
         device->identity,
         17,
         3,
         "Configured accessibility servo",
         device->id,
         {},
         {},
         {}}};
    project.nodes.append(
        {slaveId, master, Data::ProjectNodeKind::Slave, "Configured accessibility servo"});
    controller.treeModel()->setProjects({project});

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::ETHERCAT_PAGE_ID, nullptr));
    QVERIFY(page);
    page->resize(1100, 720);
    page->show();

    QTreeWidget *tree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(tree);
    const QStringList expectedHeaders = {
        "SM", "Name", "Direction", "Address", "Size", "Control", "Enabled"};
    const QStringList firstRow = {
        "0", longName, "Master to slave", "0x1000", "32", "0x26", "Yes"};
    const QStringList secondRow = {
        "1", "", "Unknown", "0x1100", "32", "0x22", "Yes"};

    const auto verifyTree = [&](const QString &contextName) -> QString {
        if (!tree->isVisible())
            return contextName + " SyncManager tree is hidden";
        if (tree->columnCount() != expectedHeaders.size())
            return contextName + " has the wrong column count";
        if (tree->topLevelItemCount() != 2)
            return contextName + " has the wrong row count";

        for (int column = 0; column < tree->columnCount(); ++column) {
            if (tree->headerItem()->text(column) != expectedHeaders.at(column))
                return contextName + " has an unexpected column heading";
        }
        for (int row = 0; row < tree->topLevelItemCount(); ++row) {
            const QTreeWidgetItem *item = tree->topLevelItem(row);
            const QStringList expected = row == 0 ? firstRow : secondRow;
            if (item->flags() & Qt::ItemIsEditable)
                return contextName + " exposes an editable SyncManager row";
            for (int column = 0; column < tree->columnCount(); ++column) {
                const QString header = tree->headerItem()->text(column);
                const QString displayed = item->data(column, Qt::DisplayRole).toString();
                const QVariant accessibleText = item->data(column, Qt::AccessibleTextRole);
                const QVariant accessibleDescription
                    = item->data(column, Qt::AccessibleDescriptionRole);
                const QVariant toolTip = item->data(column, Qt::ToolTipRole);
                const QString cell = contextName + " row " + QString::number(row) + " / " + header;
                if (displayed != expected.at(column))
                    return cell + " has an unexpected DisplayRole";
                if (accessibleText.metaType().id() != int(QMetaType::QString))
                    return cell + " AccessibleTextRole is not a QString";
                if (accessibleDescription.metaType().id() != int(QMetaType::QString))
                    return cell + " AccessibleDescriptionRole is not a QString";
                if (toolTip.metaType().id() != int(QMetaType::QString))
                    return cell + " ToolTipRole is not a QString";
                if (accessibleText.toString() != displayed)
                    return cell + " accessible text does not match DisplayRole";
                const QString description = accessibleDescription.toString();
                if (!description.contains(header))
                    return cell + " description omits the column heading";
                if (item->text(1).isEmpty()) {
                    if (!description.contains("unnamed", Qt::CaseInsensitive))
                        return cell + " description omits the unnamed SyncManager identity";
                } else if (!description.contains(item->text(1))) {
                    return cell + " description omits the SyncManager identity";
                }
                if (displayed.isEmpty()) {
                    if (!accessibleText.toString().isEmpty())
                        return cell + " empty accessible text is not empty";
                    if (!description.contains("Empty", Qt::CaseInsensitive))
                        return cell + " description omits the explicit empty value";
                } else if (!description.contains(displayed)) {
                    return cell + " description omits the complete value";
                }
                if (!description.contains("read-only", Qt::CaseInsensitive))
                    return cell + " description omits the read-only boundary";
                if (!description.contains("offline", Qt::CaseInsensitive))
                    return cell + " description omits the offline boundary";
                if (!description.contains("ESI", Qt::CaseInsensitive))
                    return cell + " description omits the ESI source";
                if (!description.contains("controller", Qt::CaseInsensitive))
                    return cell + " description omits the controller boundary";
                if (!description.contains("network", Qt::CaseInsensitive))
                    return cell + " description omits the network boundary";
                if (!description.contains("physical hardware", Qt::CaseInsensitive))
                    return cell + " description omits the physical-hardware boundary";
                if (toolTip.toString() != description)
                    return cell + " tooltip and accessible description differ";
            }
        }
        if (tree->accessibleName().isEmpty())
            return contextName + " has an empty accessible name";
        const QString widgetDescription = tree->accessibleDescription();
        const QStringList boundaries = {"read-only", "offline", "ESI", "controller", "network"};
        for (const QString &boundary : boundaries) {
            if (!widgetDescription.contains(boundary, Qt::CaseInsensitive))
                return contextName + " widget description omits " + boundary;
        }
        if (!widgetDescription.contains("physical hardware", Qt::CaseInsensitive))
            return contextName + " widget description omits the physical-hardware boundary";
        return {};
    };

    const QModelIndex configuredSlave = controller.treeModel()->indexForNodeId(slaveId);
    QVERIFY(configuredSlave.isValid());
    pages.updatePage(
        Constants::ETHERCAT_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(configuredSlave));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString configuredError = verifyTree("Configured slave");
    QVERIFY2(configuredError.isEmpty(), qPrintable(configuredError));

    const QModelIndex repositoryDevice = controller.treeModel()->indexForNodeId(device->id);
    QVERIFY(repositoryDevice.isValid());
    const Core::PropertyPageContext repositoryContext
        = controller.treeModel()->contextForIndex(repositoryDevice);
    QCOMPARE(repositoryContext.nodeKind, Core::WorkbenchNodeKind::Device);
    QVERIFY(repositoryContext.projectId.isNull());
    pages.updatePage(Constants::ETHERCAT_PAGE_ID, page.get(), repositoryContext);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString deviceError = verifyTree("Repository device");
    QVERIFY2(deviceError.isEmpty(), qPrintable(deviceError));

    pages.updatePage(
        Constants::ETHERCAT_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(configuredSlave));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString restoredError = verifyTree("Restored configured slave");
    QVERIFY2(restoredError.isEmpty(), qPrintable(restoredError));

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_SYNCMANAGER_A11Y_RENDER_PATH");
    if (!renderPath.isEmpty())
        QVERIFY2(page->grab().save(renderPath), qPrintable(renderPath));
}

void EtherCATWorkbenchTests::testEtherCATRepositoryEmptyState()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto uniqueDevice = [](QByteArray esi,
                                 const QByteArray &productCode,
                                 const QByteArray &revision,
                                 const QByteArray &typeName,
                                 const QByteArray &name) {
        esi.replace("#x00005678", productCode);
        esi.replace("#x00000011", revision);
        esi.replace("AX5000", typeName);
        esi.replace("Workbench Servo", name);
        return esi;
    };

    QByteArray supportedEmptyEsi = deviceEsi();
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<Sm ", "</Sm>"));
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<Sm ", "</Sm>"));
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<RxPdo ", "</RxPdo>"));
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<TxPdo ", "</TxPdo>"));
    supportedEmptyEsi = uniqueDevice(
        supportedEmptyEsi,
        "#x7A1A0001",
        "#x0000B201",
        "EL-ETHERCAT-EMPTY",
        "EtherCAT Empty Servo / \u65e0 SyncManager");

    QByteArray unsupportedEmptyEsi = supportedEmptyEsi;
    unsupportedEmptyEsi.replace("#x7A1A0001", "#x7A1A0002");
    unsupportedEmptyEsi.replace("#x0000B201", "#x0000B202");
    unsupportedEmptyEsi.replace("EL-ETHERCAT-EMPTY", "EL-ETHERCAT-EMPTY-UNSUPPORTED");
    unsupportedEmptyEsi.replace("\u65e0 SyncManager", "\u65e0 SyncManager / \u4e0d\u652f\u6301");
    unsupportedEmptyEsi.replace("</Device>", "<Modules/></Device>");

    QByteArray supportedPopulatedEsi = uniqueDevice(
        deviceEsi(),
        "#x7A1A0003",
        "#x0000B203",
        "EL-ETHERCAT-POPULATED",
        "EtherCAT Populated Servo / \u5df2\u89e3\u6790 SyncManager");

    QByteArray unsupportedPopulatedEsi = supportedPopulatedEsi;
    unsupportedPopulatedEsi.replace("#x7A1A0003", "#x7A1A0004");
    unsupportedPopulatedEsi.replace("#x0000B203", "#x0000B204");
    unsupportedPopulatedEsi.replace("EL-ETHERCAT-POPULATED", "EL-ETHERCAT-POPULATED-UNSUPPORTED");
    unsupportedPopulatedEsi.replace(
        "\u5df2\u89e3\u6790 SyncManager", "\u5df2\u89e3\u6790 SyncManager / \u4e0d\u652f\u6301");
    unsupportedPopulatedEsi.replace("</Device>", "<Modules/></Device>");

    struct EsiFixture
    {
        QString fileName;
        QByteArray xml;
    };
    const QList<EsiFixture> fixtures = {
        {"ethercat-empty.xml", supportedEmptyEsi},
        {"ethercat-empty-unsupported.xml", unsupportedEmptyEsi},
        {"ethercat-populated.xml", supportedPopulatedEsi},
        {"ethercat-populated-unsupported.xml", unsupportedPopulatedEsi},
    };
    QList<Utils::FilePath> esiPaths;
    for (const EsiFixture &fixture : fixtures) {
        const Utils::FilePath path
            = Utils::FilePath::fromString(directory.path()).pathAppended(fixture.fileName);
        const Utils::Result<qint64> writeResult = path.writeFileContents(fixture.xml);
        QVERIFY_RESULT(writeResult);
        esiPaths.append(path);
    }
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles(esiPaths));
    QCOMPARE(importResult.requestedFiles, fixtures.size());
    QCOMPARE(importResult.importedDevices, fixtures.size());
    QCOMPARE(importResult.failedFiles, 0);

    const auto idForType = [repository](const QString &typeName) {
        const QList<Data::DeviceSummary> devices = repository->devices();
        const auto found = std::find_if(
            devices.cbegin(), devices.cend(), [&typeName](const Data::DeviceSummary &device) {
                return device.typeName == typeName;
            });
        return found == devices.cend() ? Data::NodeId() : found->id;
    };
    const Data::NodeId supportedEmptyId = idForType("EL-ETHERCAT-EMPTY");
    const Data::NodeId unsupportedEmptyId = idForType("EL-ETHERCAT-EMPTY-UNSUPPORTED");
    const Data::NodeId supportedPopulatedId = idForType("EL-ETHERCAT-POPULATED");
    const Data::NodeId unsupportedPopulatedId = idForType("EL-ETHERCAT-POPULATED-UNSUPPORTED");
    QVERIFY(!supportedEmptyId.isNull());
    QVERIFY(!unsupportedEmptyId.isNull());
    QVERIFY(!supportedPopulatedId.isNull());
    QVERIFY(!unsupportedPopulatedId.isNull());

    const std::optional<Data::DeviceDescription> supportedEmpty = repository->device(
        supportedEmptyId);
    const std::optional<Data::DeviceDescription> unsupportedEmpty = repository->device(
        unsupportedEmptyId);
    const std::optional<Data::DeviceDescription> supportedPopulated = repository->device(
        supportedPopulatedId);
    const std::optional<Data::DeviceDescription> unsupportedPopulated = repository->device(
        unsupportedPopulatedId);
    QVERIFY(supportedEmpty);
    QVERIFY(unsupportedEmpty);
    QVERIFY(supportedPopulated);
    QVERIFY(unsupportedPopulated);
    QVERIFY(supportedEmpty->summary.supported);
    QVERIFY(supportedEmpty->syncManagers.isEmpty());
    QVERIFY(!unsupportedEmpty->summary.supported);
    QVERIFY(unsupportedEmpty->syncManagers.isEmpty());
    QVERIFY(supportedPopulated->summary.supported);
    QCOMPARE(supportedPopulated->syncManagers.size(), 2);
    QVERIFY(!unsupportedPopulated->summary.supported);
    QCOMPARE(unsupportedPopulated->syncManagers.size(), 2);

    for (const Data::NodeId &deviceId :
         {supportedEmptyId, unsupportedEmptyId, supportedPopulatedId, unsupportedPopulatedId}) {
        QTRY_VERIFY(controller.treeModel()->indexForNodeId(deviceId).isValid());
    }

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::ETHERCAT_PAGE_ID, nullptr));
    QVERIFY(page);
    QLabel *summary = page->findChild<QLabel *>("EtherCATWorkbenchPageSummary");
    QTreeWidget *tree = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QWidget *slaveForm = page->findChild<QWidget *>("EtherCATEthercatSlaveForm");
    QVERIFY(summary);
    QVERIFY(tree);
    QVERIFY(slaveForm);

    const auto showDevice = [&](const Data::NodeId &deviceId) {
        const QModelIndex index = controller.treeModel()->indexForNodeId(deviceId);
        QVERIFY(index.isValid());
        pages.updatePage(
            Constants::ETHERCAT_PAGE_ID, page.get(), controller.treeModel()->contextForIndex(index));
    };
    const auto verifySummaryMetadata = [summary]() {
        QVERIFY(!summary->accessibleName().isEmpty());
        QCOMPARE(summary->accessibleDescription(), summary->text());
        QCOMPARE(summary->toolTip(), summary->text());
    };

    showDevice(supportedEmptyId);
    QVERIFY(summary->text().contains("No ESI SyncManager", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("will not fabricate", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("controller", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("network", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("physical hardware", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("defaults from", Qt::CaseInsensitive));
    QVERIFY(tree->isHidden());
    QCOMPARE(tree->topLevelItemCount(), 0);
    QVERIFY(slaveForm->isHidden());
    verifySummaryMetadata();

    showDevice(unsupportedEmptyId);
    QVERIFY(summary->text().contains("No ESI SyncManager", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("will not fabricate", Qt::CaseInsensitive));
    QVERIFY(tree->isHidden());
    QCOMPARE(tree->topLevelItemCount(), 0);
    verifySummaryMetadata();

    showDevice(unsupportedPopulatedId);
    QVERIFY(summary->text().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!tree->isHidden());
    QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY(tree->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(tree->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(tree->accessibleDescription().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(tree->toolTip(), tree->accessibleDescription());
    verifySummaryMetadata();

    const Core::PropertyPageContext missingContext{
        {}, Data::NodeId::create(), Core::WorkbenchNodeKind::Device, "Removed ESI Device"};
    pages.updatePage(Constants::ETHERCAT_PAGE_ID, page.get(), missingContext);
    QVERIFY(summary->text().contains("no longer available", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("No matching", Qt::CaseInsensitive));
    QVERIFY(tree->isHidden());
    QCOMPARE(tree->topLevelItemCount(), 0);
    QVERIFY(tree->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(tree->toolTip(), tree->accessibleDescription());
    verifySummaryMetadata();

    showDevice(supportedPopulatedId);
    QVERIFY(summary->text().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("no longer available", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("No ESI SyncManager", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("will not fabricate", Qt::CaseInsensitive));
    QVERIFY(!tree->isHidden());
    QCOMPARE(tree->columnCount(), 7);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY(tree->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(tree->accessibleDescription().contains("offline", Qt::CaseInsensitive));
    QVERIFY(tree->accessibleDescription().contains("ESI", Qt::CaseInsensitive));
    QVERIFY(!tree->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!tree->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!tree->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(tree->toolTip(), tree->accessibleDescription());
    verifySummaryMetadata();

    for (int row = 0; row < tree->topLevelItemCount(); ++row)
        QVERIFY(!(tree->topLevelItem(row)->flags() & Qt::ItemIsEditable));
    QVERIFY(projectService->projects().isEmpty());
}

void EtherCATWorkbenchTests::testEditableConfiguredSlaveEtherCATWorkflow()
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
                                        .pathAppended("editable-ethercat.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(deviceEsi()));
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

    controller.selectionService()->setCurrentNodeId(file.slaveId);
    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::ETHERCAT_PAGE_ID).toString());
    QVERIFY(page);
    details.tabWidget()->setCurrentWidget(page);
    QTRY_VERIFY(page->isVisible());
    QLineEdit *type = page->findChild<QLineEdit *>("EtherCATEthercatType");
    QLineEdit *productRevision = page->findChild<QLineEdit *>("EtherCATEthercatProductRevision");
    QLineEdit *autoIncAddress = page->findChild<QLineEdit *>("EtherCATEthercatAutoIncAddress");
    QLineEdit *ethercatAddress = page->findChild<QLineEdit *>("EtherCATEthercatAddress");
    QSpinBox *alias = page->findChild<QSpinBox *>("EtherCATEthercatAlias");
    QLineEdit *identification = page->findChild<QLineEdit *>("EtherCATEthercatIdentificationValue");
    QLineEdit *previousPort = page->findChild<QLineEdit *>("EtherCATEthercatPreviousPort");
    QPushButton *advanced = page->findChild<QPushButton *>("EtherCATEthercatAdvancedSettings");
    QTreeWidget *syncManagers = page->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(type);
    QVERIFY(productRevision);
    QVERIFY(autoIncAddress);
    QVERIFY(ethercatAddress);
    QVERIFY(alias);
    QVERIFY(identification);
    QVERIFY(previousPort);
    QVERIFY(advanced);
    QVERIFY(syncManagers);
    QCOMPARE(type->text(), QString("AX5000"));
    QCOMPARE(productRevision->text(), QString("0x00005678 / 0x00000011"));
    QCOMPARE(autoIncAddress->text(), QString("0x0000"));
    QCOMPARE(ethercatAddress->text(), QString("Automatic at startup (not stored)"));
    QCOMPARE(alias->minimum(), 0);
    QCOMPARE(alias->maximum(), 65535);
    QCOMPARE(alias->value(), 3);
    QCOMPARE(identification->text(), QString("Not configured"));
    QVERIFY(previousPort->text().contains("EtherCAT Master"));
    QVERIFY(previousPort->text().contains("port not modeled"));
    QVERIFY(type->isReadOnly());
    QVERIFY(productRevision->isReadOnly());
    QVERIFY(autoIncAddress->isReadOnly());
    QVERIFY(ethercatAddress->isReadOnly());
    QVERIFY(identification->isReadOnly());
    QVERIFY(previousPort->isReadOnly());
    QVERIFY(alias->isEnabled());
    QVERIFY(!advanced->isEnabled());
    QVERIFY(!alias->accessibleName().isEmpty());
    QCOMPARE(syncManagers->topLevelItemCount(), 2);

    alias->setValue(321);
    QVERIFY(QMetaObject::invokeMethod(alias, "editingFinished"));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().alias, quint16(321));
    QTRY_COMPARE(alias->value(), 321);
    QVERIFY(projectService->project(file.projectId)->modified);
    QCOMPARE(controller.selectionService()->currentNodeId(), file.slaveId);
    QVERIFY(projectService->canUndoProject(file.projectId));

    const Utils::Result<> undoAlias = projectService->undoProject(file.projectId);
    QVERIFY_RESULT(undoAlias);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().alias, quint16(3));
    QTRY_COMPARE(alias->value(), 3);
    const Utils::Result<> redoAlias = projectService->redoProject(file.projectId);
    QVERIFY_RESULT(redoAlias);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().alias, quint16(321));
    QTRY_COMPARE(alias->value(), 321);

    alias->setValue(0);
    QVERIFY(QMetaObject::invokeMethod(alias, "editingFinished"));
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().alias, quint16(0));
    QTRY_COMPARE(alias->value(), 0);
    QVERIFY(alias->specialValueText().contains("disabled", Qt::CaseInsensitive));

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testStatusBarTracksStateService()
{
    auto statusButton = ::Core::ICore::statusBar()->findChild<QToolButton *>(
        "EtherCATWorkbenchStatus");
    QVERIFY(statusButton);

    Core::StateService *stateService
        = ExtensionSystem::PluginManager::getObject<Core::StateService>();
    QVERIFY(stateService);
    const Utils::Id testSource("EtherCAT.Workbench.TestStatus");
    const Utils::Id busySource("EtherCAT.Workbench.TestBusyStatus");
    stateService->clearStatus(testSource);
    stateService->clearStatus(busySource);

    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_VERIFY(statusButton->isVisible());
    QVERIFY(statusButton->text().contains("Offline", Qt::CaseInsensitive));
    QVERIFY(!statusButton->icon().isNull());
    QVERIFY(!statusButton->accessibleName().isEmpty());
    QVERIFY(!statusButton->accessibleDescription().isEmpty());
    const int expectedIconSize
        = statusButton->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, statusButton);
    QCOMPARE(statusButton->iconSize(), QSize(expectedIconSize, expectedIconSize));

    QVERIFY(stateService->setStatus(
        {busySource, Core::StatusSeverity::Busy, "Mock scan running", "Building local snapshot"}));
    QTRY_COMPARE(statusButton->text(), QString("MOCK Busy"));

    QVERIFY(stateService->setStatus(
        {testSource,
         Core::StatusSeverity::Error,
         "Controller fault",
         "Deterministic local test status"}));
    QTRY_COMPARE(statusButton->text(), QString("MOCK Fault"));
    QVERIFY(statusButton->toolTip().contains("Controller fault"));
    QVERIFY(statusButton->toolTip().contains("Deterministic local test status"));
    QTRY_VERIFY(statusButton->width() >= statusButton->sizeHint().width());
    QCOMPARE(
        statusButton->icon().pixmap(expectedIconSize, expectedIconSize).toImage(),
        Utils::Icons::CRITICAL_TOOLBAR.icon().pixmap(expectedIconSize, expectedIconSize).toImage());
    QVERIFY(statusButton->menu());
    QVERIFY(statusButton->menu()->actions().size() >= 2);

    ::Core::ModeManager::activateMode(::Core::Constants::MODE_EDIT);
    QTRY_VERIFY(!statusButton->isVisible());
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_VERIFY(statusButton->isVisible());

    stateService->clearStatus(testSource);
    QTRY_COMPARE(statusButton->text(), QString("MOCK Busy"));
    stateService->clearStatus(busySource);
    QTRY_VERIFY(statusButton->text().contains("Offline", Qt::CaseInsensitive));
}

void EtherCATWorkbenchTests::testTreeModelLargeIncrementalUpdate()
{
    WorkbenchTreeModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    const Data::ProjectSnapshot project = projectSnapshot();
    model.setProjects({project});
    QCOMPARE(reset.count(), 1);

    QSignalSpy activeChanged(&model, &QAbstractItemModel::dataChanged);
    model.setActiveProjectId(project.id);
    QCOMPARE(activeChanged.count(), 1);
    const QList<int> activeRoles = activeChanged.first().at(2).value<QList<int>>();
    QVERIFY(activeRoles.contains(Qt::AccessibleTextRole));
    QVERIFY(activeRoles.contains(Qt::AccessibleDescriptionRole));
    const QModelIndex projectRoot = model.indexForNodeId(project.id);
    QCOMPARE(projectRoot.data(Qt::AccessibleTextRole), projectRoot.data(Qt::DisplayRole));
    QVERIFY(projectRoot.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("Active project | Offline"));

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

    const QModelIndex retained = model.indexForNodeId(devices.at(42).id);
    QCOMPARE(retained.data(Qt::AccessibleTextRole).toString(), devices.at(42).name);
    const QString retainedDescription = retained.data(Qt::AccessibleDescriptionRole).toString();
    QVERIFY(retainedDescription.contains("Vendor: 0x00000002"));
    QVERIFY(retainedDescription.contains("Product: 0x0000102a"));
    QVERIFY(retainedDescription.contains("Revision: 0x00000001"));

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
    QCOMPARE(
        model.indexForNodeId(retainedId).data(Qt::AccessibleTextRole).toString(),
        QString("Renamed Device"));
    QCOMPARE(reset.count(), 1);
}

void EtherCATWorkbenchTests::testConfiguredSlaveStateIcon()
{
    WorkbenchTreeModel model;
    Data::ProjectSnapshot project = projectSnapshot("State Icon");
    const Data::NodeId master = masterId(project);
    const Data::NodeId slaveId = Data::NodeId::create();
    project.slaves = {
        {slaveId,
         master,
         0,
         {0x00000002, 0x12345678, 0x00000001},
         7,
         0,
         "Configured Servo",
         Data::NodeId::create(),
         {},
         {},
         {}}};
    project.nodes.append({slaveId, master, Data::ProjectNodeKind::Slave, "Configured Servo"});
    model.setProjects({project});

    const QModelIndex slave = model.indexForNodeId(slaveId);
    QVERIFY(slave.isValid());
    const QIcon actual = slave.data(Qt::DecorationRole).value<QIcon>();
    QVERIFY(!actual.isNull());
    const int iconSize = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    QCOMPARE(
        actual.pixmap(iconSize, iconSize).toImage(),
        ::Core::Icons::DESKTOP_DEVICE_SMALL.icon().pixmap(iconSize, iconSize).toImage());

    QList<Data::DeviceSummary> repository = deviceSummaries(2);
    repository.first().supported = false;
    model.syncDevices(repository);
    const QModelIndex unsupported = model.indexForNodeId(repository.first().id);
    QCOMPARE(
        unsupported.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        Utils::Icons::BROKEN.icon().pixmap(iconSize, iconSize).toImage());
}

void EtherCATWorkbenchTests::testProviderStateTreeAndNavigation()
{
    WorkbenchController controller;
    QAbstractItemModelTester
        modelTester(controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    ProcessTreeFixture fixture = processTreeFixture();
    const Data::NodeId secondSlaveId = Data::NodeId::create();
    fixture.project.slaves.append(
        {secondSlaveId,
         masterId(fixture.project),
         1,
         {2, 0x6789, 0x21},
         18,
         0,
         "Configured I/O",
         {},
         {},
         {},
         {}});
    fixture.project.nodes.append(
        {secondSlaveId, masterId(fixture.project), Data::ProjectNodeKind::Slave, "Configured I/O"});
    controller.treeModel()->setProjects({fixture.project});

    const QModelIndex master = controller.treeModel()->indexForNodeId(masterId(fixture.project));
    const QModelIndex missing = controller.treeModel()->indexForNodeId(fixture.slaveId);
    const QModelIndex revision = controller.treeModel()->indexForNodeId(secondSlaveId);
    QCOMPARE(master.data(Qt::AccessibleTextRole).toString(), QString("EtherCAT Master"));
    QCOMPARE(
        master.data(Qt::AccessibleDescriptionRole).toString(),
        master.data(Qt::ToolTipRole).toString());

    AvailableScanProvider scan;
    AvailableDiagnosticsProvider diagnostics;
    scan.setAvailable(true);
    diagnostics.setAvailable(true);
    ExtensionSystem::PluginManager::addObject(&scan);
    ExtensionSystem::PluginManager::addObject(&diagnostics);
    bool providersRegistered = true;
    const QScopeGuard providerCleanup([&] {
        if (!providersRegistered)
            return;
        ExtensionSystem::PluginManager::removeObject(&diagnostics);
        ExtensionSystem::PluginManager::removeObject(&scan);
    });

    Data::ScanResult scanResult;
    scanResult.snapshot.projectId = fixture.project.id;
    scanResult.snapshot.masterId = masterId(fixture.project);
    scanResult.snapshot.mock = true;
    scanResult.snapshot.complete = true;
    scanResult.comparison.projectId = fixture.project.id;
    scanResult.comparison.masterId = masterId(fixture.project);
    scanResult.comparison.exactMatch = true;
    scan.publishResult(scanResult);

    const int iconSize = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    QTRY_VERIFY(master.siblingAtColumn(1).data().toString().contains("topology matches"));
    QCOMPARE(
        master.siblingAtColumn(1).data(Qt::AccessibleTextRole),
        master.siblingAtColumn(1).data(Qt::DisplayRole));
    QVERIFY(!controller.treeModel()->firstTopologyDifference().isValid());
    QTRY_COMPARE(
        master.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        Utils::Icons::OK.icon().pixmap(iconSize, iconSize).toImage());

    scanResult.comparison.exactMatch = false;
    scanResult.comparison.differences = {
        {Data::TopologyDifferenceKind::Missing,
         Data::DifferenceSeverity::Warning,
         fixture.slaveId,
         {},
         0,
         -1,
         "Missing slave",
         "Configured Servo"},
        {Data::TopologyDifferenceKind::RevisionMismatch,
         Data::DifferenceSeverity::Warning,
         secondSlaveId,
         Data::NodeId::create(),
         1,
         1,
         "Revision difference",
         "Offline 0x21, scanned 0x22"},
        {Data::TopologyDifferenceKind::VendorMismatch,
         Data::DifferenceSeverity::Blocking,
         secondSlaveId,
         Data::NodeId::create(),
         1,
         1,
         "Vendor mismatch",
         "Offline 0x2, scanned 0x3"},
        {Data::TopologyDifferenceKind::Added,
         Data::DifferenceSeverity::Information,
         {},
         Data::NodeId::create(),
         -1,
         2,
         "Added slave",
         "Unexpected I/O"},
    };
    QSignalSpy providerChanged(controller.treeModel(), &QAbstractItemModel::dataChanged);
    scan.publishResult(scanResult);

    QTRY_VERIFY(master.siblingAtColumn(1).data().toString().contains("MOCK"));
    QTRY_VERIFY(master.siblingAtColumn(1).data().toString().contains("4"));
    QTRY_VERIFY(master.siblingAtColumn(1).data().toString().contains("Added"));
    QTRY_VERIFY(missing.siblingAtColumn(1).data().toString().contains("Missing"));
    QTRY_VERIFY(revision.siblingAtColumn(1).data().toString().contains("Revision"));
    QTRY_VERIFY(revision.siblingAtColumn(1).data().toString().contains("Vendor"));
    QTRY_VERIFY(revision.data(Qt::AccessibleDescriptionRole)
                    .toString()
                    .contains("Offline 0x21, scanned 0x22"));
    const auto hasAccessibleRoleUpdate = [](const QSignalSpy &spy, const QModelIndex &target) {
        for (const QList<QVariant> &arguments : spy) {
            const QModelIndex topLeft = arguments.at(0).value<QModelIndex>();
            const QModelIndex bottomRight = arguments.at(1).value<QModelIndex>();
            const QList<int> roles = arguments.at(2).value<QList<int>>();
            if (topLeft.parent() == target.parent() && topLeft.row() <= target.row()
                && bottomRight.row() >= target.row() && topLeft.column() <= target.column()
                && bottomRight.column() >= target.column()
                && roles.contains(Qt::AccessibleTextRole)
                && roles.contains(Qt::AccessibleDescriptionRole)) {
                return true;
            }
        }
        return false;
    };
    QVERIFY(hasAccessibleRoleUpdate(providerChanged, revision));
    QVERIFY(controller.treeModel()->firstTopologyDifference().isValid());

    QTRY_COMPARE(
        revision.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        Utils::Icons::CRITICAL.icon().pixmap(iconSize, iconSize).toImage());

    WorkbenchNavigationWidget navigation(&controller);
    navigation.filterEdit()->setText("Offline 0x21, scanned 0x22");
    const QModelIndex proxyRevision = findById(navigation.treeView()->model(), secondSlaveId);
    QTRY_VERIFY(proxyRevision.isValid());
    QVERIFY(proxyRevision.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("Offline 0x21, scanned 0x22"));
    emit controller.locateFirstTopologyDifferenceRequested();
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        fixture.slaveId);

    scanResult.comparison.differences.removeAt(2);
    scan.publishResult(scanResult);
    QTRY_VERIFY(master.siblingAtColumn(1).data().toString().contains("3"));

    Data::DiagnosticsSnapshot diagnosticsSnapshot;
    diagnosticsSnapshot.projectId = fixture.project.id;
    diagnosticsSnapshot.masterId = masterId(fixture.project);
    diagnosticsSnapshot.mock = true;
    diagnosticsSnapshot.runMode = Data::DiagnosticsRunMode::Run;
    diagnosticsSnapshot.masterState = Data::EtherCATState::Operational;
    diagnosticsSnapshot.masterHasError = true;
    diagnosticsSnapshot.masterAlStatusText = "MOCK master fault";
    diagnosticsSnapshot.activeAlarmCount = 2;
    diagnosticsSnapshot.slaves = {
        {fixture.slaveId,
         0,
         "Configured Servo",
         Data::EtherCATState::SafeOperational,
         true,
         0x0011,
         "MOCK slave fault",
         Data::WorkingCounterState::Incomplete,
         {},
         {},
         {},
         {}},
    };
    diagnostics.publishSnapshot(diagnosticsSnapshot);

    const QModelIndex diagnosticsNode = controller.treeModel()->diagnosticsForProject(
        fixture.project.id);
    const QModelIndex proxyMissing = findById(navigation.treeView()->model(), fixture.slaveId);
    const QModelIndex proxyDiagnostics = findByKind(
        navigation.treeView()->model(), Core::WorkbenchNodeKind::Diagnostics);
    QVERIFY(proxyMissing.isValid());
    QVERIFY(proxyDiagnostics.isValid());
    QTRY_VERIFY(missing.siblingAtColumn(1).data().toString().contains("SAFEOP"));
    QTRY_VERIFY(missing.siblingAtColumn(1).data().toString().contains("Error"));
    QTRY_VERIFY(revision.siblingAtColumn(1).data().toString().contains("not present"));
    QTRY_VERIFY(diagnosticsNode.siblingAtColumn(1).data().toString().contains("2 active"));
    QTRY_VERIFY(missing.data(Qt::AccessibleDescriptionRole)
                    .toString()
                    .contains("MOCK slave fault"));
    QTRY_VERIFY(proxyMissing.data(Qt::AccessibleDescriptionRole)
                    .toString()
                    .contains("MOCK slave fault"));
    QTRY_VERIFY(diagnosticsNode.data(Qt::AccessibleDescriptionRole)
                    .toString()
                    .contains("2 active"));
    QTRY_COMPARE(
        missing.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        Utils::Icons::CRITICAL.icon().pixmap(iconSize, iconSize).toImage());

    navigation.filterEdit()->setText("MOCK slave fault");
    QTRY_VERIFY(findById(navigation.treeView()->model(), fixture.slaveId).isValid());
    diagnosticsSnapshot.slaves.append(
        {secondSlaveId,
         1,
         "Configured I/O",
         Data::EtherCATState::Operational,
         false,
         0,
         {},
         Data::WorkingCounterState::Valid,
         {},
         {},
         {},
         {}});
    diagnostics.publishSnapshot(diagnosticsSnapshot);
    QTRY_VERIFY(revision.siblingAtColumn(1).data().toString().contains("MOCK OP"));
    QTRY_VERIFY(!revision.siblingAtColumn(1).data().toString().contains("not present"));

    emit controller.locateFirstIssueRequested();
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        fixture.slaveId);
    emit controller.openDiagnosticsRequested();
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeKindRole)
            .value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Diagnostics);
    diagnostics.setStreamState(Data::DiagnosticsStreamState::Stopped);
    QTRY_VERIFY(master.siblingAtColumn(1).data().toString().contains("last"));
    QTRY_VERIFY(diagnosticsNode.siblingAtColumn(1).data().toString().contains("Stopped"));

    navigation.resize(1200, 800);
    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_TREE_ROW_A11Y_RENDER_PATH");
    if (!renderPath.isEmpty())
        QVERIFY2(navigation.grab().save(renderPath), qPrintable(renderPath));

    ExtensionSystem::PluginManager::removeObject(&diagnostics);
    ExtensionSystem::PluginManager::removeObject(&scan);
    providersRegistered = false;
    QTRY_VERIFY(missing.siblingAtColumn(1).data().toString().contains("Offline"));
    QTRY_VERIFY(diagnosticsNode.data(Qt::AccessibleDescriptionRole)
                    .toString()
                    .contains("No Diagnostics Provider registered"));
    QTRY_VERIFY(proxyDiagnostics.data(Qt::AccessibleDescriptionRole)
                    .toString()
                    .contains("No Diagnostics Provider registered"));
    QVERIFY(diagnosticsNode.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("Local Mock only"));
    QCOMPARE(
        missing.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        ::Core::Icons::DESKTOP_DEVICE_SMALL.icon().pixmap(iconSize, iconSize).toImage());
    controller.selectionService()->clear();
}

void EtherCATWorkbenchTests::testProjectScopedLocateNavigation()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary device = deviceSummaries(1).first();
    const TestProjectFile alpha = writeProjectWithSlave(
        directory, device, "alpha-locate.ecatproject", "Alpha Locate Project");
    const TestProjectFile beta = writeProjectWithSlave(
        directory, device, "beta-locate.ecatproject", "Beta Locate Project");
    QVERIFY(!alpha.path.isEmpty());
    QVERIFY(!beta.path.isEmpty());

    QPointer<ProjectExplorer::Project> alphaProjectObject;
    QPointer<ProjectExplorer::Project> betaProjectObject;
    AvailableScanProvider scan(
        Utils::Id("EtherCAT.Workbench.ProjectScopedLocateScan"),
        "Local Mock project-scoped locate scan");
    bool scanRegistered = false;
    const QScopeGuard cleanup([&] {
        controller.selectionService()->clear();
        if (scanRegistered)
            ExtensionSystem::PluginManager::removeObject(&scan);
        if (betaProjectObject
            && ProjectExplorer::ProjectManager::hasProject(betaProjectObject.data())) {
            ProjectExplorer::ProjectManager::removeProject(betaProjectObject.data());
        }
        if (alphaProjectObject
            && ProjectExplorer::ProjectManager::hasProject(alphaProjectObject.data())) {
            ProjectExplorer::ProjectManager::removeProject(alphaProjectObject.data());
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });

    const ProjectExplorer::OpenProjectResult alphaOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(alpha.path, false);
    QVERIFY2(alphaOpened, qPrintable(alphaOpened.errorMessage()));
    alphaProjectObject = alphaOpened.project();
    const ProjectExplorer::OpenProjectResult betaOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(beta.path, false);
    QVERIFY2(betaOpened, qPrintable(betaOpened.errorMessage()));
    betaProjectObject = betaOpened.project();
    QTRY_COMPARE(projectService->projects().size(), 2);
    QVERIFY_RESULT(projectService->replaceOfflineSlaves(beta.projectId, beta.masterId, {}));

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(900, 600);
    navigation.show();
    DetailsView details(&controller);
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(navigation.isVisible());
    QTRY_VERIFY(details.isVisible());
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(navigation.treeView()->hasFocus());
    QTRY_COMPARE(
        ::Core::ICore::currentContextWidget(), static_cast<QWidget *>(&navigation));

    ::Core::Command *locateDifferenceCommand
        = ::Core::ActionManager::command(Constants::LOCATE_DIFFERENCE_ACTION_ID);
    ::Core::Command *locateIssueCommand
        = ::Core::ActionManager::command(Constants::LOCATE_ISSUE_ACTION_ID);
    QVERIFY(locateDifferenceCommand);
    QVERIFY(locateIssueCommand);
    QAction *locateDifferenceContextAction
        = locateDifferenceCommand->actionForContext(Constants::CONTEXT_ID);
    QAction *locateIssueContextAction
        = locateIssueCommand->actionForContext(Constants::CONTEXT_ID);
    QVERIFY(locateDifferenceContextAction);
    QVERIFY(locateIssueContextAction);

    scan.setAvailable(true);
    ExtensionSystem::PluginManager::addObject(&scan);
    scanRegistered = true;
    Data::ScanResult scanResult;
    scanResult.snapshot.projectId = alpha.projectId;
    scanResult.snapshot.masterId = alpha.masterId;
    scanResult.snapshot.mock = true;
    scanResult.snapshot.complete = true;
    scanResult.comparison.projectId = alpha.projectId;
    scanResult.comparison.masterId = alpha.masterId;
    scanResult.comparison.exactMatch = false;
    scanResult.comparison.differences = {
        {Data::TopologyDifferenceKind::Missing,
         Data::DifferenceSeverity::Warning,
         alpha.slaveId,
         {},
         0,
         -1,
         "Missing Alpha slave",
         "Only Alpha has a local Mock topology difference"},
    };
    scan.publishResult(scanResult);
    QTRY_VERIFY(controller.treeModel()->firstTopologyDifference().isValid());
    QTRY_VERIFY(controller.treeModel()->firstIssue().isValid());
    QVERIFY(!controller.treeModel()->firstTopologyDifference(beta.projectId).isValid());
    QVERIFY(!controller.treeModel()->firstIssue(beta.projectId).isValid());
    QCOMPARE(
        controller.treeModel()
            ->firstTopologyDifference(alpha.projectId)
            .data(WorkbenchTreeModel::ProjectIdRole)
            .value<Data::NodeId>(),
        alpha.projectId);
    QCOMPARE(
        controller.treeModel()
            ->firstIssue(alpha.projectId)
            .data(WorkbenchTreeModel::ProjectIdRole)
            .value<Data::NodeId>(),
        alpha.projectId);

    controller.selectionService()->setCurrentNodeId(beta.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, beta.projectId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        beta.projectId);
    QTRY_VERIFY(!locateDifferenceCommand->action()->isEnabled());
    QTRY_VERIFY(!locateIssueCommand->action()->isEnabled());
    QTRY_VERIFY(!locateDifferenceContextAction->isEnabled());
    QTRY_VERIFY(!locateIssueContextAction->isEnabled());

    emit controller.locateFirstTopologyDifferenceRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), beta.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, beta.projectId);
    emit controller.locateFirstIssueRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), beta.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, beta.projectId);

    controller.selectionService()->setCurrentNodeId(alpha.projectId);
    QTRY_VERIFY(locateDifferenceCommand->action()->isEnabled());
    QTRY_VERIFY(locateIssueCommand->action()->isEnabled());
    QTRY_VERIFY(locateDifferenceContextAction->isEnabled());
    QTRY_VERIFY(locateIssueContextAction->isEnabled());

    const QModelIndex betaMasterProxy = findById(navigation.treeView()->model(), beta.masterId);
    QVERIFY(betaMasterProxy.isValid());
    const QModelIndex betaPlaceholderProxy = directChildByKind(
        navigation.treeView()->model(), Core::WorkbenchNodeKind::Placeholder, betaMasterProxy);
    QVERIFY(betaPlaceholderProxy.isValid());
    QCOMPARE(
        betaPlaceholderProxy.data(WorkbenchTreeModel::NodeKindRole)
            .value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Placeholder);
    navigation.treeView()->scrollTo(betaPlaceholderProxy);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool placeholderPopupSeen = false;
    bool placeholderDifferenceEnabled = true;
    bool placeholderIssueEnabled = true;
    QTimer::singleShot(
        0,
        &navigation,
        [&placeholderPopupSeen,
         &placeholderDifferenceEnabled,
         &placeholderIssueEnabled,
         locateDifferenceCommand,
         locateIssueCommand] {
            auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!popup)
                return;
            placeholderPopupSeen = true;
            placeholderDifferenceEnabled = locateDifferenceCommand->action()->isEnabled();
            placeholderIssueEnabled = locateIssueCommand->action()->isEnabled();
            popup->close();
        });
    emit navigation.treeView()->customContextMenuRequested(
        navigation.treeView()->visualRect(betaPlaceholderProxy).center());
    QVERIFY(placeholderPopupSeen);
    QVERIFY(!placeholderDifferenceEnabled);
    QVERIFY(!placeholderIssueEnabled);
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), alpha.projectId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        alpha.projectId);
    QTRY_VERIFY(locateDifferenceCommand->action()->isEnabled());
    QTRY_VERIFY(locateIssueCommand->action()->isEnabled());

    emit controller.locateFirstTopologyDifferenceRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), alpha.slaveId);
    QTRY_COMPARE(details.currentContext().projectId, alpha.projectId);
    controller.selectionService()->setCurrentNodeId(alpha.projectId);
    emit controller.locateFirstIssueRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), alpha.slaveId);
    QTRY_COMPARE(details.currentContext().projectId, alpha.projectId);

    const Data::NodeId unknownNodeId = Data::NodeId::create();
    controller.selectionService()->setCurrentNodeId(unknownNodeId);
    QTRY_VERIFY(!navigation.treeView()->currentIndex().isValid());
    QTRY_VERIFY(!locateDifferenceCommand->action()->isEnabled());
    QTRY_VERIFY(!locateIssueCommand->action()->isEnabled());
    emit controller.locateFirstTopologyDifferenceRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), unknownNodeId);
    QTRY_VERIFY(!navigation.treeView()->currentIndex().isValid());
    emit controller.locateFirstIssueRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), unknownNodeId);
    QTRY_VERIFY(!navigation.treeView()->currentIndex().isValid());

    controller.selectionService()->clear();
    QTRY_VERIFY(locateDifferenceCommand->action()->isEnabled());
    QTRY_VERIFY(locateIssueCommand->action()->isEnabled());
    emit controller.locateFirstTopologyDifferenceRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), alpha.slaveId);

    const QModelIndex repositoryProxy = findByKind(
        navigation.treeView()->model(), Core::WorkbenchNodeKind::DeviceRepository);
    QVERIFY(repositoryProxy.isValid());
    const Data::NodeId repositoryId
        = repositoryProxy.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    QVERIFY(!repositoryId.isNull());
    controller.selectionService()->setCurrentNodeId(repositoryId);
    QTRY_VERIFY(locateDifferenceCommand->action()->isEnabled());
    QTRY_VERIFY(locateIssueCommand->action()->isEnabled());
    emit controller.locateFirstIssueRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), alpha.slaveId);

    controller.selectionService()->setCurrentNodeId(beta.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, beta.projectId);
    navigation.treeView()->expandAll();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString treeRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_LOCATE_CONTEXT_TREE_RENDER_PATH");
    if (!treeRenderPath.isEmpty())
        QVERIFY2(navigation.grab().save(treeRenderPath), qPrintable(treeRenderPath));
    const QString detailsRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_LOCATE_CONTEXT_DETAILS_RENDER_PATH");
    if (!detailsRenderPath.isEmpty())
        QVERIFY2(details.grab().save(detailsRenderPath), qPrintable(detailsRenderPath));
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

void EtherCATWorkbenchTests::testNavigationExpansionStateLifecycle()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot alpha = projectSnapshot("Alpha EtherCAT Project");
    const ProcessTreeFixture fixture = processTreeFixture();
    controller.treeModel()->setProjects({alpha, fixture.project});

    WorkbenchNavigationWidget navigation(&controller);
    QTreeView *tree = navigation.treeView();
    QAbstractItemModel *model = tree->model();

    const Data::NodeId targetId = fixture.project.nodes.at(1).id;
    const Data::NodeId master = masterId(fixture.project);
    const QModelIndex sourceRxPdo
        = findBySourceId(controller.treeModel(), fixture.rxPdoId);
    QVERIFY(sourceRxPdo.isValid());
    const Data::NodeId rxPdoViewId
        = sourceRxPdo.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    const QModelIndex sourceRxPdoGroup = sourceRxPdo.parent();
    QVERIFY(sourceRxPdoGroup.isValid());
    const Data::NodeId rxPdoGroupViewId
        = sourceRxPdoGroup.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();

    const auto indexForId = [model](const Data::NodeId &id) { return findById(model, id); };
    const auto expand = [tree, &indexForId](const Data::NodeId &id) {
        const QModelIndex index = indexForId(id);
        QVERIFY(index.isValid());
        tree->expand(index);
    };

    tree->collapseAll();
    expand(fixture.project.id);
    expand(targetId);
    expand(master);
    expand(fixture.slaveId);
    expand(rxPdoGroupViewId);
    expand(rxPdoViewId);
    controller.selectionService()->setCurrentNodeId(master);
    QTRY_COMPARE(
        tree->currentIndex().data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>(),
        master);
    QVERIFY(!tree->isExpanded(indexForId(alpha.id)));
    QVERIFY(tree->isExpanded(indexForId(rxPdoGroupViewId)));
    QVERIFY(tree->isExpanded(indexForId(rxPdoViewId)));

    Data::ProjectSnapshot renamed = fixture.project;
    renamed.name = "Renamed Process Tree";
    controller.treeModel()->setProjects({alpha, renamed});

    QTRY_VERIFY(indexForId(alpha.id).isValid());
    QTRY_VERIFY(indexForId(rxPdoViewId).isValid());
    QVERIFY2(
        !tree->isExpanded(indexForId(alpha.id)),
        "An unrelated collapsed project must remain collapsed after a model reset");
    QVERIFY2(
        tree->isExpanded(indexForId(rxPdoGroupViewId)),
        "A surviving expanded process-data group must remain expanded after a model reset");
    QVERIFY2(
        tree->isExpanded(indexForId(rxPdoViewId)),
        "A surviving expanded PDO must remain expanded after a model reset");
    QCOMPARE(controller.selectionService()->currentNodeId(), master);
    QCOMPARE(
        tree->currentIndex().data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>(),
        master);

    tree->collapse(indexForId(renamed.id));
    QVERIFY(!tree->isExpanded(indexForId(renamed.id)));
    navigation.filterEdit()->setText("Drive Command");
    QTRY_VERIFY(indexForId(rxPdoViewId).isValid());
    QTRY_VERIFY(tree->isExpanded(indexForId(renamed.id)));
    navigation.filterEdit()->clear();
    QTRY_VERIFY(indexForId(renamed.id).isValid());
    QVERIFY2(
        !tree->isExpanded(indexForId(renamed.id)),
        "Clearing a transient filter must restore the pre-filter collapsed state");
    QVERIFY(tree->isExpanded(indexForId(rxPdoGroupViewId)));
    QVERIFY(tree->isExpanded(indexForId(rxPdoViewId)));

    const Data::ProjectSnapshot added = projectSnapshot("Zulu EtherCAT Project");
    const Data::NodeId addedTargetId = added.nodes.at(1).id;
    const Data::NodeId addedMasterId = masterId(added);
    controller.treeModel()->setProjects({alpha, renamed, added});
    QTRY_VERIFY(indexForId(added.id).isValid());
    QVERIFY(tree->isExpanded(indexForId(added.id)));
    QVERIFY(tree->isExpanded(indexForId(addedTargetId)));
    QVERIFY(tree->isExpanded(indexForId(addedMasterId)));
    QCOMPARE(controller.selectionService()->currentNodeId(), master);
    QCOMPARE(
        tree->currentIndex().data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>(),
        master);

    tree->collapse(indexForId(renamed.id));
    navigation.filterEdit()->setText("No matching navigation node");
    QTRY_COMPARE(model->rowCount(), 0);
    controller.selectionService()->setCurrentNodeId(rxPdoViewId);
    QTRY_VERIFY(navigation.filterEdit()->text().isEmpty());
    QTRY_COMPARE(
        tree->currentIndex().data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>(),
        rxPdoViewId);
    QVERIFY(tree->isExpanded(indexForId(renamed.id)));
    QVERIFY(tree->isExpanded(indexForId(rxPdoGroupViewId)));
}

void EtherCATWorkbenchTests::testNavigationActiveProjectLifecycle()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());
    QVERIFY(!controller.activeOfflineMasterTarget());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QList<Data::DeviceSummary> devices = deviceSummaries(2);
    const TestProjectFile first = writeProjectWithSlave(
        directory, devices.at(0), "alpha.ecatproject", "Alpha EtherCAT Project");
    const TestProjectFile second = writeProjectWithSlave(
        directory, devices.at(1), "beta.ecatproject", "Beta EtherCAT Project");
    QVERIFY(!first.path.isEmpty());
    QVERIFY(!second.path.isEmpty());

    const ProjectExplorer::OpenProjectResult firstOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(first.path, false);
    QVERIFY2(firstOpened, qPrintable(firstOpened.errorMessage()));
    const ProjectExplorer::OpenProjectResult secondOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(second.path, false);
    QVERIFY2(secondOpened, qPrintable(secondOpened.errorMessage()));
    QTRY_COMPARE(projectService->projects().size(), 2);
    QVERIFY_RESULT(projectService->activateProject(first.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), first.projectId);
    const std::optional<OfflineMasterTarget> firstTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(firstTarget);
    QCOMPARE(firstTarget->projectId, first.projectId);
    QCOMPARE(firstTarget->masterId, first.masterId);
    QCOMPARE(firstTarget->projectName, QString("Alpha EtherCAT Project"));
    QCOMPARE(firstTarget->masterName, QString("EtherCAT Master"));

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(720, 360);
    navigation.show();
    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(navigation.isVisible());
    QTRY_VERIFY(details.isVisible());
    QAbstractItemModelTester tester(
        controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);

    const auto projectStatus = [&controller](const Data::NodeId &projectId) {
        return controller.treeModel()
            ->indexForNodeId(projectId, 1)
            .data(Qt::DisplayRole)
            .toString();
    };
    QTRY_COMPARE(projectStatus(first.projectId), QString("Active project | Offline"));
    QCOMPARE(projectStatus(second.projectId), QString("Offline"));
    QCOMPARE(
        controller.treeModel()->indexForNodeId(first.projectId).data(WorkbenchTreeModel::StatusRole),
        QVariant("Active project | Offline"));
    QVERIFY(
        controller.treeModel()
            ->indexForNodeId(first.projectId)
            .data(WorkbenchTreeModel::SearchTextRole)
            .toString()
            .contains("Active project | Offline"));
    QVERIFY(
        controller.treeModel()
            ->indexForNodeId(first.projectId)
            .data(Qt::ToolTipRole)
            .toString()
            .contains("Active project | Offline"));
    QVERIFY(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(first.masterId))
            & Qt::ItemIsDropEnabled);
    QVERIFY(!(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(second.masterId))
              & Qt::ItemIsDropEnabled));
    QVERIFY(
        controller.treeModel()
            ->indexForNodeId(first.masterId)
            .data(Qt::ToolTipRole)
            .toString()
            .contains("Drop a supported ESI device here"));
    QVERIFY(
        !controller.treeModel()
             ->indexForNodeId(second.masterId)
             .data(Qt::ToolTipRole)
             .toString()
             .contains("Drop a supported ESI device here"));

    navigation.treeView()->collapseAll();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_ACTIVE_PROJECT_RENDER_PATH");
    if (!renderPath.isEmpty())
        QVERIFY2(navigation.grab().save(renderPath), qPrintable(renderPath));

    const QModelIndex secondProjectProxy
        = findById(navigation.treeView()->model(), second.projectId);
    QVERIFY(secondProjectProxy.isValid());
    navigation.treeView()->setCurrentIndex(secondProjectProxy);
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), second.projectId);
    QCOMPARE(projectService->activeProjectId(), first.projectId);

    controller.selectionService()->setCurrentNodeId(second.masterId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        second.masterId);
    QTRY_COMPARE(details.currentContext().nodeId, second.masterId);
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::Master);

    QSignalSpy modelResetSpy(controller.treeModel(), &QAbstractItemModel::modelReset);
    const QPersistentModelIndex firstPersistent(
        controller.treeModel()->indexForNodeId(first.projectId));
    const QPersistentModelIndex secondPersistent(
        controller.treeModel()->indexForNodeId(second.projectId));
    QVERIFY(firstPersistent.isValid());
    QVERIFY(secondPersistent.isValid());
    const int resetCount = modelResetSpy.count();

    QVERIFY_RESULT(projectService->activateProject(second.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), second.projectId);
    const std::optional<OfflineMasterTarget> secondTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(secondTarget);
    QCOMPARE(secondTarget->projectId, second.projectId);
    QCOMPARE(secondTarget->masterId, second.masterId);
    QTRY_COMPARE(projectStatus(first.projectId), QString("Offline"));
    QTRY_COMPARE(projectStatus(second.projectId), QString("Active project | Offline"));
    QCOMPARE(modelResetSpy.count(), resetCount);
    QVERIFY(firstPersistent.isValid());
    QVERIFY(secondPersistent.isValid());
    QCOMPARE(controller.selectionService()->currentNodeId(), second.masterId);
    QCOMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        second.masterId);
    QCOMPARE(details.currentContext().nodeId, second.masterId);
    QVERIFY(!(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(first.masterId))
              & Qt::ItemIsDropEnabled));
    QVERIFY(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(second.masterId))
            & Qt::ItemIsDropEnabled);
    QVERIFY(
        !controller.treeModel()
             ->indexForNodeId(first.masterId)
             .data(Qt::ToolTipRole)
             .toString()
             .contains("Drop a supported ESI device here"));
    QVERIFY(
        controller.treeModel()
            ->indexForNodeId(second.masterId)
            .data(Qt::ToolTipRole)
            .toString()
            .contains("Drop a supported ESI device here"));

    const QString renamedProject = "Beta %1 & EtherCAT Project";
    const QString renamedMaster = "Master %2 & Offline";
    QVERIFY_RESULT(controller.renameProject(second.projectId, renamedProject));
    QVERIFY_RESULT(controller.renameStructuralNode(
        second.projectId, second.masterId, renamedMaster));
    const std::optional<OfflineMasterTarget> renamedTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(renamedTarget);
    QCOMPARE(renamedTarget->projectName, renamedProject);
    QCOMPARE(renamedTarget->masterName, renamedMaster);

    navigation.filterEdit()->setText("Active project");
    QTRY_VERIFY(!findById(navigation.treeView()->model(), first.projectId).isValid());
    QTRY_VERIFY(findById(navigation.treeView()->model(), second.projectId).isValid());
    QVERIFY_RESULT(projectService->activateProject(first.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), first.projectId);
    const std::optional<OfflineMasterTarget> firstAgainTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(firstAgainTarget);
    QCOMPARE(firstAgainTarget->projectId, first.projectId);
    QCOMPARE(firstAgainTarget->projectName, QString("Alpha EtherCAT Project"));
    QTRY_VERIFY(findById(navigation.treeView()->model(), first.projectId).isValid());
    QTRY_VERIFY(!findById(navigation.treeView()->model(), second.projectId).isValid());
    QCOMPARE(navigation.filterEdit()->text(), QString("Active project"));
    QCOMPARE(controller.selectionService()->currentNodeId(), second.masterId);
    QCOMPARE(details.currentContext().nodeId, second.masterId);
    navigation.filterEdit()->clear();
    QTRY_VERIFY(findById(navigation.treeView()->model(), second.projectId).isValid());

    QVERIFY_RESULT(projectService->activateProject(second.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), second.projectId);
    const std::optional<OfflineMasterTarget> secondAgainTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(secondAgainTarget);
    QCOMPARE(secondAgainTarget->projectId, second.projectId);
    QCOMPARE(secondAgainTarget->projectName, renamedProject);
    QCOMPARE(secondAgainTarget->masterName, renamedMaster);
    controller.selectionService()->setCurrentNodeId(second.masterId);
    QTRY_COMPARE(details.currentContext().nodeId, second.masterId);
    ProjectExplorer::ProjectManager::removeProject(secondOpened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(!projectService->project(second.projectId).has_value());
    QTRY_COMPARE(projectService->activeProjectId(), first.projectId);
    QTRY_VERIFY(!controller.treeModel()->indexForNodeId(second.projectId).isValid());
    QTRY_VERIFY(controller.selectionService()->currentNodeId().isNull());
    QTRY_COMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QTRY_COMPARE(projectStatus(first.projectId), QString("Active project | Offline"));
    QTRY_VERIFY(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(first.masterId))
                & Qt::ItemIsDropEnabled);
    const std::optional<OfflineMasterTarget> fallbackTarget
        = controller.activeOfflineMasterTarget();
    QVERIFY(fallbackTarget);
    QCOMPARE(fallbackTarget->projectId, first.projectId);
    QCOMPARE(fallbackTarget->projectName, QString("Alpha EtherCAT Project"));

    ProjectExplorer::ProjectManager::removeProject(firstOpened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(projectService->projects().isEmpty());
    QTRY_VERIFY(projectService->activeProjectId().isNull());
    QTRY_VERIFY(!controller.treeModel()->indexForNodeId(first.projectId).isValid());
    QCOMPARE(
        controller.treeModel()->index(0, 0).data().toString(),
        QString("No EtherCAT project is open"));
    QVERIFY(controller.selectionService()->currentNodeId().isNull());
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QVERIFY(!controller.activeOfflineMasterTarget());
}

void EtherCATWorkbenchTests::testInvalidProjectPresentationAndLifecycle()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());
    QAbstractItemModelTester sourceModelTester(
        controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QPointer<ProjectExplorer::Project> validProjectObject;
    QPointer<ProjectExplorer::Project> secondValidProjectObject;
    QPointer<ProjectExplorer::Project> invalidProjectObject;
    const QScopeGuard cleanup([&] {
        controller.selectionService()->clear();
        if (invalidProjectObject
            && ProjectExplorer::ProjectManager::hasProject(invalidProjectObject.data())) {
            ProjectExplorer::ProjectManager::removeProject(invalidProjectObject.data());
        }
        if (secondValidProjectObject
            && ProjectExplorer::ProjectManager::hasProject(secondValidProjectObject.data())) {
            ProjectExplorer::ProjectManager::removeProject(secondValidProjectObject.data());
        }
        if (validProjectObject
            && ProjectExplorer::ProjectManager::hasProject(validProjectObject.data())) {
            ProjectExplorer::ProjectManager::removeProject(validProjectObject.data());
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    const TestProjectFile valid = writeProjectWithSlave(
        directory,
        deviceSummaries(1).first(),
        "valid-project.ecatproject",
        "Valid EtherCAT Project");
    QVERIFY(!valid.path.isEmpty());
    const Utils::FilePath invalidPath = Utils::FilePath::fromString(directory.path())
                                            .canonicalPath()
                                            .pathAppended("broken-project.ecatproject");
    QVERIFY_RESULT(invalidPath.writeFileContents("{broken"));

    const ProjectExplorer::OpenProjectResult validOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(valid.path, false);
    QVERIFY2(validOpened, qPrintable(validOpened.errorMessage()));
    validProjectObject = validOpened.project();
    const ProjectExplorer::OpenProjectResult invalidOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(invalidPath, false);
    QVERIFY2(invalidOpened, qPrintable(invalidOpened.errorMessage()));
    invalidProjectObject = invalidOpened.project();
    QTRY_COMPARE(projectService->projects().size(), 2);

    const QList<Data::ProjectSnapshot> projects = projectService->projects();
    const auto invalidProject = std::find_if(
        projects.cbegin(), projects.cend(), [](const Data::ProjectSnapshot &project) {
            return !project.valid;
        });
    QVERIFY(invalidProject != projects.cend());
    QVERIFY(!invalidProject->id.isNull());
    QVERIFY(!invalidProject->error.isEmpty());
    const Data::ProjectSnapshot invalid = *invalidProject;

    QVERIFY_RESULT(projectService->activateProject(valid.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), valid.projectId);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(900, 600);
    navigation.show();
    DetailsView details(&controller);
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(navigation.isVisible());
    QTRY_VERIFY(details.isVisible());
    QAbstractItemModelTester proxyModelTester(
        navigation.treeView()->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(navigation.treeView()->hasFocus());
    QTRY_COMPARE(
        ::Core::ICore::currentContextWidget(), static_cast<QWidget *>(&navigation));

    const QModelIndex invalidRoot = controller.treeModel()->indexForNodeId(invalid.id);
    QVERIFY(invalidRoot.isValid());
    QCOMPARE(invalidRoot.data().toString(), invalid.name);
    QCOMPARE(
        invalidRoot.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Project);
    QCOMPARE(
        invalidRoot.data(WorkbenchTreeModel::ProjectIdRole).value<Data::NodeId>(), invalid.id);
    QCOMPARE(
        invalidRoot.siblingAtColumn(1).data().toString(),
        QString("Invalid project | Offline data unavailable"));
    QCOMPARE(
        invalidRoot.data(WorkbenchTreeModel::StatusRole).toString(),
        QString("Invalid project | Offline data unavailable"));
    QVERIFY(invalidRoot.data(Qt::ToolTipRole).toString().contains(invalid.error));
    QCOMPARE(invalidRoot.data(Qt::AccessibleTextRole), invalidRoot.data(Qt::DisplayRole));
    QVERIFY(invalidRoot.data(Qt::AccessibleDescriptionRole).toString().contains(invalid.error));
    QVERIFY(invalidRoot.data(WorkbenchTreeModel::SearchTextRole).toString().contains(invalid.error));
    const int iconSize = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    QCOMPARE(
        invalidRoot.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        Utils::Icons::CRITICAL.icon().pixmap(iconSize, iconSize).toImage());
    QCOMPARE(
        controller.treeModel()
            ->firstIssue()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        invalid.id);

    controller.treeModel()->setProviderPresentations(
        {OptionalProviderState::Unavailable, "Unavailable Scan Probe"},
        {OptionalProviderState::Unavailable, "Unavailable Diagnostics Probe"},
        std::nullopt,
        Data::DiagnosticsStreamState::Stopped,
        {},
        std::nullopt);
    QCOMPARE(
        invalidRoot.siblingAtColumn(1).data().toString(),
        QString("Invalid project | Offline data unavailable"));
    QVERIFY(invalidRoot.data(Qt::ToolTipRole).toString().contains(invalid.error));
    QCOMPARE(
        invalidRoot.data(Qt::DecorationRole).value<QIcon>().pixmap(iconSize, iconSize).toImage(),
        Utils::Icons::CRITICAL.icon().pixmap(iconSize, iconSize).toImage());

    QCOMPARE(controller.treeModel()->rowCount(invalidRoot), 1);
    const QModelIndex recovery = controller.treeModel()->index(0, 0, invalidRoot);
    QVERIFY(recovery.isValid());
    QCOMPARE(recovery.data().toString(), QString("Project configuration unavailable"));
    QCOMPARE(
        recovery.siblingAtColumn(1).data().toString(),
        QString("Fix the project file and reopen it"));
    QCOMPARE(
        recovery.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Placeholder);
    QVERIFY(controller.treeModel()->flags(recovery) & Qt::ItemIsEnabled);
    QVERIFY(!(controller.treeModel()->flags(recovery) & Qt::ItemIsSelectable));
    for (const Data::ProjectNodeSnapshot &node : invalid.nodes) {
        if (node.kind != Data::ProjectNodeKind::Project)
            QVERIFY(!controller.treeModel()->indexForNodeId(node.id).isValid());
    }
    QVERIFY(!controller.treeModel()->diagnosticsForProject(invalid.id).isValid());
    QVERIFY(controller.treeModel()->indexForNodeId(valid.masterId).isValid());

    navigation.filterEdit()->setText(invalid.error);
    QTRY_VERIFY(findById(navigation.treeView()->model(), invalid.id).isValid());
    navigation.filterEdit()->clear();
    QTRY_VERIFY(findById(navigation.treeView()->model(), valid.projectId).isValid());

    ::Core::Command *copyCommand
        = ::Core::ActionManager::command(Constants::COPY_NODE_ID_ACTION_ID);
    ::Core::Command *openDiagnosticsCommand
        = ::Core::ActionManager::command(Constants::OPEN_DIAGNOSTICS_ACTION_ID);
    ::Core::Command *setActiveCommand
        = ::Core::ActionManager::command(Constants::SET_ACTIVE_PROJECT_ACTION_ID);
    QVERIFY(copyCommand);
    QVERIFY(openDiagnosticsCommand);
    QVERIFY(setActiveCommand);
    QAction *copyContextAction = copyCommand->actionForContext(Constants::CONTEXT_ID);
    QAction *openDiagnosticsContextAction
        = openDiagnosticsCommand->actionForContext(Constants::CONTEXT_ID);
    QAction *setActiveContextAction = setActiveCommand->actionForContext(Constants::CONTEXT_ID);
    QVERIFY(copyContextAction);
    QVERIFY(openDiagnosticsContextAction);
    QVERIFY(setActiveContextAction);

    controller.selectionService()->setCurrentNodeId(valid.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, valid.projectId);
    QTRY_VERIFY(copyCommand->action()->isEnabled());
    QTRY_VERIFY(copyContextAction->isEnabled());
    QTRY_VERIFY(openDiagnosticsCommand->action()->isEnabled());
    QTRY_VERIFY(openDiagnosticsContextAction->isEnabled());

    const QModelIndex invalidRootProxy
        = findById(navigation.treeView()->model(), invalid.id);
    QVERIFY(invalidRootProxy.isValid());
    const QModelIndex recoveryProxy
        = navigation.treeView()->model()->index(0, 0, invalidRootProxy);
    QVERIFY(recoveryProxy.isValid());
    navigation.treeView()->scrollTo(recoveryProxy);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool placeholderPopupSeen = false;
    bool placeholderDiagnosticsEnabled = true;
    QTimer::singleShot(
        0,
        &navigation,
        [&placeholderPopupSeen, &placeholderDiagnosticsEnabled, openDiagnosticsCommand] {
            auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!popup)
                return;
            placeholderPopupSeen = true;
            placeholderDiagnosticsEnabled = openDiagnosticsCommand->action()->isEnabled();
            popup->close();
        });
    emit navigation.treeView()->customContextMenuRequested(
        navigation.treeView()->visualRect(recoveryProxy).center());
    QVERIFY(placeholderPopupSeen);
    QVERIFY(!placeholderDiagnosticsEnabled);
    QTRY_VERIFY(openDiagnosticsCommand->action()->isEnabled());
    QTRY_VERIFY(openDiagnosticsContextAction->isEnabled());
    QTRY_VERIFY(copyCommand->action()->isEnabled());
    QTRY_VERIFY(copyContextAction->isEnabled());
    QCOMPARE(controller.selectionService()->currentNodeId(), valid.projectId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        valid.projectId);

    const Data::NodeId unknownNodeId = Data::NodeId::create();
    controller.selectionService()->setCurrentNodeId(unknownNodeId);
    QTRY_VERIFY(!openDiagnosticsCommand->action()->isEnabled());
    QTRY_VERIFY(!openDiagnosticsContextAction->isEnabled());
    QTRY_VERIFY(!navigation.treeView()->currentIndex().isValid());
    QList<QAction *> unknownPopupActions;
    bool unknownPopupSeen = false;
    QTimer::singleShot(0, &navigation, [&unknownPopupActions, &unknownPopupSeen] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        unknownPopupSeen = true;
        unknownPopupActions = popup->actions();
        popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(unknownPopupSeen);
    QVERIFY(unknownPopupActions.contains(openDiagnosticsCommand->action()));
    QVERIFY(!openDiagnosticsCommand->action()->isEnabled());
    emit controller.openDiagnosticsRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), unknownNodeId);
    QTRY_VERIFY(!navigation.treeView()->currentIndex().isValid());

    controller.selectionService()->setCurrentNodeId(invalid.id);
    QTRY_COMPARE(details.currentContext().nodeId, invalid.id);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        invalid.id);
    QTRY_VERIFY(!copyCommand->action()->isEnabled());
    QTRY_VERIFY(!copyContextAction->isEnabled());
    QTRY_VERIFY(!openDiagnosticsCommand->action()->isEnabled());
    QTRY_VERIFY(!openDiagnosticsContextAction->isEnabled());
    QTRY_VERIFY(!setActiveCommand->action()->isEnabled());
    QTRY_VERIFY(!setActiveContextAction->isEnabled());
    QVERIFY(!controller.canCopyNodeId(invalid.id));
    QApplication::clipboard()->setText("unchanged-invalid-project-clipboard");
    emit controller.copyCurrentNodeIdRequested();
    QTRY_COMPARE(
        QApplication::clipboard()->text(), QString("unchanged-invalid-project-clipboard"));
    QList<QAction *> invalidPopupActions;
    bool invalidPopupSeen = false;
    QTimer::singleShot(0, &navigation, [&invalidPopupActions, &invalidPopupSeen] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        invalidPopupSeen = true;
        invalidPopupActions = popup->actions();
        popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(invalidPopupSeen);
    QVERIFY(!invalidPopupActions.contains(copyCommand->action()));
    QVERIFY(!invalidPopupActions.contains(setActiveCommand->action()));
    QVERIFY(invalidPopupActions.contains(openDiagnosticsCommand->action()));
    QVERIFY(!openDiagnosticsCommand->action()->isEnabled());
    emit controller.openDiagnosticsRequested();
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), invalid.id);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        invalid.id);
    QVERIFY(!controller.canActivateSelectedProject());
    const Utils::Result<> activation = controller.activateSelectedProject();
    QVERIFY(!activation);
    QVERIFY(activation.error().contains("invalid", Qt::CaseInsensitive));
    QCOMPARE(projectService->activeProjectId(), valid.projectId);

    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    QLineEdit *id = page->findChild<QLineEdit *>("EtherCATProjectGeneralId");
    QLineEdit *type = page->findChild<QLineEdit *>("EtherCATProjectGeneralType");
    QLineEdit *formatVersion
        = page->findChild<QLineEdit *>("EtherCATProjectGeneralFormatVersion");
    QLineEdit *createdBy = page->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QLineEdit *validity = page->findChild<QLineEdit *>("EtherCATProjectGeneralValidity");
    QLineEdit *migration = page->findChild<QLineEdit *>("EtherCATProjectGeneralMigration");
    QLineEdit *modified = page->findChild<QLineEdit *>("EtherCATProjectGeneralModified");
    QLineEdit *target = page->findChild<QLineEdit *>("EtherCATProjectGeneralTarget");
    QLineEdit *master = page->findChild<QLineEdit *>("EtherCATProjectGeneralMaster");
    QLineEdit *slaveCount = page->findChild<QLineEdit *>("EtherCATProjectGeneralSlaveCount");
    for (QLineEdit *field :
         {name,
          id,
          type,
          formatVersion,
          createdBy,
          validity,
          migration,
          modified,
          target,
          master,
          slaveCount}) {
        QVERIFY(field);
    }
    QCOMPARE(name->text(), invalid.name);
    QVERIFY(name->isReadOnly());
    QCOMPARE(type->text(), QString("Offline EtherCAT Engineering Project"));
    QCOMPARE(validity->text(), QString("Invalid: %1").arg(invalid.error));
    const QString unavailable = "Unavailable";
    for (QLineEdit *field :
         {id, formatVersion, createdBy, migration, modified, target, master, slaveCount}) {
        QCOMPARE(field->text(), unavailable);
    }

    controller.selectionService()->setCurrentNodeId(valid.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, valid.projectId);
    page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    name = page->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    id = page->findChild<QLineEdit *>("EtherCATProjectGeneralId");
    formatVersion = page->findChild<QLineEdit *>("EtherCATProjectGeneralFormatVersion");
    validity = page->findChild<QLineEdit *>("EtherCATProjectGeneralValidity");
    target = page->findChild<QLineEdit *>("EtherCATProjectGeneralTarget");
    master = page->findChild<QLineEdit *>("EtherCATProjectGeneralMaster");
    slaveCount = page->findChild<QLineEdit *>("EtherCATProjectGeneralSlaveCount");
    for (QLineEdit *field : {name, id, formatVersion, validity, target, master, slaveCount})
        QVERIFY(field);
    QTRY_COMPARE(name->text(), QString("Valid EtherCAT Project"));
    QVERIFY(!name->isReadOnly());
    QCOMPARE(id->text(), valid.projectId.toString());
    QCOMPARE(formatVersion->text(), QString("2"));
    QCOMPARE(validity->text(), QString("Valid"));
    QCOMPARE(target->text(), QString("Offline Controller"));
    QCOMPARE(master->text(), QString("EtherCAT Master"));
    QCOMPARE(slaveCount->text(), QString("1"));
    QVERIFY(controller.canCopyNodeId(valid.projectId));

    controller.selectionService()->setCurrentNodeId(invalid.id);
    QTRY_COMPARE(details.currentContext().nodeId, invalid.id);
    page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QVERIFY(page);
    name = page->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    id = page->findChild<QLineEdit *>("EtherCATProjectGeneralId");
    validity = page->findChild<QLineEdit *>("EtherCATProjectGeneralValidity");
    for (QLineEdit *field : {name, id, validity})
        QVERIFY(field);
    QTRY_COMPARE(name->text(), invalid.name);
    QVERIFY(name->isReadOnly());
    QCOMPARE(id->text(), unavailable);
    QCOMPARE(validity->text(), QString("Invalid: %1").arg(invalid.error));
    QVERIFY(!controller.canCopyNodeId(invalid.id));

    ProjectExplorer::ProjectManager::setStartupProject(invalidOpened.project());
    QTRY_COMPARE(projectService->activeProjectId(), invalid.id);
    QTRY_COMPARE(
        controller.treeModel()->indexForNodeId(invalid.id, 1).data().toString(),
        QString("Active project | Invalid project | Offline data unavailable"));
    QVERIFY(!controller.canActivateSelectedProject());
    QTRY_VERIFY(!setActiveCommand->action()->isEnabled());
    QTRY_VERIFY(!setActiveContextAction->isEnabled());
    controller.selectionService()->setCurrentNodeId(valid.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, valid.projectId);
    QTRY_VERIFY(setActiveCommand->action()->isEnabled());
    QTRY_VERIFY(setActiveContextAction->isEnabled());
    QVERIFY(controller.canActivateSelectedProject());
    controller.selectionService()->setCurrentNodeId(invalid.id);
    QTRY_COMPARE(details.currentContext().nodeId, invalid.id);
    QTRY_VERIFY(!setActiveCommand->action()->isEnabled());
    QTRY_VERIFY(!setActiveContextAction->isEnabled());
    QVERIFY(controller.treeModel()->indexForNodeId(valid.masterId).isValid());
    for (const Data::ProjectNodeSnapshot &node : invalid.nodes) {
        if (node.kind == Data::ProjectNodeKind::Master)
            QVERIFY(!controller.treeModel()->indexForNodeId(node.id).isValid());
    }

    const QString treeRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_INVALID_PROJECT_TREE_RENDER_PATH");
    if (!treeRenderPath.isEmpty()) {
        navigation.treeView()->expandAll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(navigation.grab().save(treeRenderPath), qPrintable(treeRenderPath));
    }
    const QString detailsRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_INVALID_PROJECT_DETAILS_RENDER_PATH");
    if (!detailsRenderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(detailsRenderPath), qPrintable(detailsRenderPath));
    }

    ProjectExplorer::ProjectManager::removeProject(invalidOpened.project());
    invalidProjectObject = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(!projectService->project(invalid.id).has_value());
    QTRY_COMPARE(projectService->activeProjectId(), valid.projectId);
    QTRY_VERIFY(!controller.treeModel()->indexForNodeId(invalid.id).isValid());
    QTRY_VERIFY(controller.selectionService()->currentNodeId().isNull());
    QTRY_COMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QCOMPARE(
        controller.treeModel()->indexForNodeId(valid.projectId, 1).data().toString(),
        QString("Active project | Offline"));
    QVERIFY(controller.treeModel()->indexForNodeId(valid.masterId).isValid());

    QTRY_VERIFY(openDiagnosticsCommand->action()->isEnabled());
    QTRY_VERIFY(openDiagnosticsContextAction->isEnabled());
    emit controller.openDiagnosticsRequested();
    QTRY_COMPARE(
        controller.selectionService()->currentNodeId(),
        controller.treeModel()
            ->diagnosticsForProject({})
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>());

    const TestProjectFile secondValid = writeProjectWithSlave(
        directory,
        deviceSummaries(1).first(),
        "second-valid-project.ecatproject",
        "Second Valid EtherCAT Project");
    QVERIFY(!secondValid.path.isEmpty());
    const ProjectExplorer::OpenProjectResult secondValidOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(secondValid.path, false);
    QVERIFY2(secondValidOpened, qPrintable(secondValidOpened.errorMessage()));
    secondValidProjectObject = secondValidOpened.project();
    QTRY_COMPARE(projectService->projects().size(), 2);
    const Data::NodeId firstDiagnosticsProjectId
        = controller.treeModel()
              ->diagnosticsForProject({})
              .data(WorkbenchTreeModel::ProjectIdRole)
              .value<Data::NodeId>();
    const Data::NodeId nonFirstProjectId
        = firstDiagnosticsProjectId == valid.projectId ? secondValid.projectId : valid.projectId;
    QVERIFY(nonFirstProjectId != firstDiagnosticsProjectId);
    controller.selectionService()->setCurrentNodeId(nonFirstProjectId);
    QTRY_VERIFY(openDiagnosticsCommand->action()->isEnabled());
    emit controller.openDiagnosticsRequested();
    QTRY_COMPARE(
        controller.selectionService()->currentNodeId(),
        controller.treeModel()
            ->diagnosticsForProject(nonFirstProjectId)
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>());
    ProjectExplorer::ProjectManager::removeProject(secondValidOpened.project());
    secondValidProjectObject = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(projectService->projects().size(), 1);

    controller.selectionService()->setCurrentNodeId(valid.projectId);
    QVERIFY(controller.canCopyNodeId(valid.projectId));
    QTRY_VERIFY(openDiagnosticsCommand->action()->isEnabled());
    QTRY_VERIFY(openDiagnosticsContextAction->isEnabled());
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        valid.projectId);
    QList<QAction *> validPopupActions;
    bool validPopupSeen = false;
    QTimer::singleShot(0, &navigation, [&validPopupActions, &validPopupSeen] {
        auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
            return;
        validPopupSeen = true;
        validPopupActions = popup->actions();
        popup->close();
    });
    emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
    QVERIFY(validPopupSeen);
    QVERIFY(validPopupActions.contains(copyCommand->action()));
    QVERIFY(validPopupActions.contains(openDiagnosticsCommand->action()));
    QVERIFY(copyCommand->action()->isEnabled());
    QVERIFY(openDiagnosticsCommand->action()->isEnabled());
    emit controller.openDiagnosticsRequested();
    QTRY_COMPARE(
        controller.selectionService()->currentNodeId(),
        controller.treeModel()
            ->diagnosticsForProject(valid.projectId)
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>());
    controller.selectionService()->setCurrentNodeId(valid.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, valid.projectId);
    QApplication::clipboard()->clear();
    emit controller.copyCurrentNodeIdRequested();
    QTRY_COMPARE(QApplication::clipboard()->text(), valid.projectId.toString());

    ProjectExplorer::ProjectManager::removeProject(validOpened.project());
    validProjectObject = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(projectService->projects().isEmpty());
    QTRY_VERIFY(projectService->activeProjectId().isNull());
}

void EtherCATWorkbenchTests::testDetailsEmptyStateLifecycle()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    DetailsView details(&controller);
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(details.isVisible());

    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QLabel *emptyState = details.findChild<QLabel *>("EtherCATWorkbenchEmptyState");
    QTabWidget *tabs = details.findChild<QTabWidget *>("EtherCATWorkbenchPropertyTabs");
    QVERIFY(title);
    QVERIFY(emptyState);
    QVERIFY(tabs);
    QCOMPARE(title->text(), QString("EtherCAT Workbench"));
    QTRY_COMPARE(
        emptyState->text(),
        QString("No EtherCAT project is open. Create or open an EtherCAT project "
                "(.ecatproject), or select Device Repository to inspect local ESI "
                "descriptions."));
    QVERIFY(emptyState->isVisible());
    QVERIFY(!tabs->isVisible());
    QVERIFY(!details.accessibleName().isEmpty());
    QVERIFY(!details.accessibleDescription().isEmpty());
    QVERIFY(!title->accessibleName().isEmpty());
    QVERIFY(!title->accessibleDescription().isEmpty());
    QVERIFY(!emptyState->accessibleName().isEmpty());
    QVERIFY(!emptyState->accessibleDescription().isEmpty());
    QVERIFY(!tabs->accessibleName().isEmpty());
    QVERIFY(!tabs->accessibleDescription().isEmpty());

    const QString noProjectRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_DETAILS_NO_PROJECT_RENDER_PATH");
    if (!noProjectRenderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(noProjectRenderPath), qPrintable(noProjectRenderPath));
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const TestProjectFile file = writeProjectWithSlave(directory, deviceSummaries(1).first());
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_COMPARE(
        emptyState->text(),
        QString("Select an EtherCAT node in the tree to inspect its offline details."));
    QVERIFY(emptyState->isVisible());
    QVERIFY(!tabs->isVisible());

    controller.selectionService()->setCurrentNodeId(file.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, file.projectId);
    QTRY_COMPARE(title->accessibleName(), QString("Process Data Workflow"));
    QTRY_VERIFY(tabs->isVisible());
    QVERIFY(!emptyState->isVisible());

    controller.selectionService()->setCurrentNodeId({});
    QTRY_COMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QTRY_COMPARE(
        emptyState->text(),
        QString("Select an EtherCAT node in the tree to inspect its offline details."));
    QTRY_VERIFY(emptyState->isVisible());
    QVERIFY(!tabs->isVisible());

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_DETAILS_EMPTY_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(projectService->projects().isEmpty());
    QTRY_COMPARE(
        emptyState->text(),
        QString("No EtherCAT project is open. Create or open an EtherCAT project "
                "(.ecatproject), or select Device Repository to inspect local ESI "
                "descriptions."));
    QVERIFY(emptyState->isVisible());
    QVERIFY(!tabs->isVisible());
}

void EtherCATWorkbenchTests::testDetailsKeyboardFocusContinuity()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot alpha = projectSnapshot("Focus Alpha");
    const Data::ProjectSnapshot beta = projectSnapshot("Focus Beta");
    controller.treeModel()->setProjects({alpha, beta});
    controller.selectionService()->setCurrentNodeId(alpha.id);

    DetailsView details(&controller);
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QTRY_COMPARE(details.currentContext().nodeId, alpha.id);

    QTabWidget *tabs = details.tabWidget();
    QVERIFY(tabs);
    QTRY_VERIFY(tabs->isVisible());
    QWidget *alphaPage = tabs->currentWidget();
    QVERIFY(alphaPage);
    const QString pageKey = alphaPage->property("EtherCAT.PageKey").toString();
    QVERIFY(!pageKey.isEmpty());

    QLineEdit *alphaName = alphaPage->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    QLineEdit *alphaCreatedBy
        = alphaPage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(alphaName);
    QVERIFY(alphaCreatedBy);
    QCOMPARE(alphaName->text(), alpha.name);
    QPointer<QLineEdit> destroyedCreatedBy = alphaCreatedBy;
    alphaCreatedBy->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), alphaCreatedBy);

    controller.selectionService()->setCurrentNodeId(beta.id);
    QTRY_COMPARE(details.currentContext().nodeId, beta.id);
    QTRY_VERIFY(destroyedCreatedBy.isNull());
    QWidget *betaPage = tabs->currentWidget();
    QVERIFY(betaPage);
    QCOMPARE(betaPage->property("EtherCAT.PageKey").toString(), pageKey);
    QLineEdit *betaName = betaPage->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    QLineEdit *betaCreatedBy
        = betaPage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(betaName);
    QVERIFY(betaCreatedBy);
    QCOMPARE(betaName->text(), beta.name);
    QTRY_COMPARE(QApplication::focusWidget(), betaCreatedBy);

    const QString missingFocusObjectName("EtherCATProjectGeneralMissingFocusTarget");
    betaCreatedBy->setObjectName(missingFocusObjectName);
    betaCreatedBy->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), betaCreatedBy);
    controller.selectionService()->setCurrentNodeId(alpha.id);
    QTRY_COMPARE(details.currentContext().nodeId, alpha.id);
    QWidget *missingTargetPage = tabs->currentWidget();
    QVERIFY(missingTargetPage);
    QCOMPARE(missingTargetPage->property("EtherCAT.PageKey").toString(), pageKey);
    QTRY_VERIFY(QApplication::focusWidget());
    QWidget *missingFallbackFocus = QApplication::focusWidget();
    QVERIFY(missingTargetPage->isAncestorOf(missingFallbackFocus));
    QVERIFY(missingFallbackFocus->isVisible());
    QVERIFY(missingFallbackFocus->isEnabled());
    QVERIFY(missingFallbackFocus->focusPolicy() & Qt::TabFocus);
    QVERIFY(missingFallbackFocus->objectName() != missingFocusObjectName);
    missingFallbackFocus->setObjectName("EtherCATProjectGeneralCreatedBy");

    const Data::NodeId betaTargetId = beta.nodes.at(1).id;
    controller.selectionService()->setCurrentNodeId(betaTargetId);
    QTRY_COMPARE(details.currentContext().nodeId, betaTargetId);
    QWidget *targetPage = tabs->currentWidget();
    QVERIFY(targetPage);
    QCOMPARE(targetPage->property("EtherCAT.PageKey").toString(), pageKey);
    QLineEdit *hiddenCreatedBy
        = targetPage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(hiddenCreatedBy);
    QVERIFY(!hiddenCreatedBy->isVisible());
    QTRY_VERIFY(QApplication::focusWidget());
    QWidget *fallbackFocus = QApplication::focusWidget();
    QVERIFY(targetPage->isAncestorOf(fallbackFocus));
    QVERIFY(fallbackFocus->isVisible());
    QVERIFY(fallbackFocus->isEnabled());
    QVERIFY(fallbackFocus->focusPolicy() & Qt::TabFocus);
    QVERIFY(fallbackFocus->objectName() != hiddenCreatedBy->objectName());

    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QVERIFY(title);
    title->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), title);
    controller.selectionService()->setCurrentNodeId(alpha.id);
    QTRY_COMPARE(details.currentContext().nodeId, alpha.id);
    QTRY_COMPARE(QApplication::focusWidget(), title);

    const Utils::Id reentrantProviderId("EtherCAT.Workbench.FocusReentrantPages");
    const Utils::Id reentrantPageId("EtherCAT.Workbench.FocusReentrantPage");
    TestPageProvider reentrantProvider(
        reentrantProviderId,
        reentrantPageId,
        "Reentrant Focus Page",
        "EtherCATFocusReentrantPage",
        180);
    bool reentrantProviderRegistered = false;
    const QScopeGuard providerCleanup([&] {
        if (reentrantProviderRegistered)
            ExtensionSystem::PluginManager::removeObject(&reentrantProvider);
    });
    ExtensionSystem::PluginManager::addObject(&reentrantProvider);
    reentrantProviderRegistered = true;
    QTRY_VERIFY(tabs->count() >= 2);
    const int reentrantPageCount = tabs->count();

    const QString reentrantPageKey
        = reentrantProviderId.toString() + '/' + reentrantPageId.toString();
    const auto pageIndexForKey = [tabs](const QString &key) {
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->widget(index)->property("EtherCAT.PageKey").toString() == key)
                return index;
        }
        return -1;
    };
    QWidget *reentrantAlphaPage = tabs->currentWidget();
    QVERIFY(reentrantAlphaPage);
    QLineEdit *reentrantCreatedBy
        = reentrantAlphaPage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(reentrantCreatedBy);
    reentrantCreatedBy->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), reentrantCreatedBy);
    bool reentrantUpdateObserved = false;
    reentrantProvider.onUpdate = [&] {
        reentrantUpdateObserved = true;
        controller.selectionService()->setCurrentNodeId(alpha.id);
        title->setFocus(Qt::OtherFocusReason);
    };
    controller.selectionService()->setCurrentNodeId(beta.id);
    QVERIFY(reentrantUpdateObserved);
    QCOMPARE(details.currentContext().nodeId, alpha.id);
    QCOMPARE(tabs->count(), reentrantPageCount);
    QTRY_COMPARE(QApplication::focusWidget(), title);

    QWidget *reentrantPage
        = details.findChild<QWidget *>("EtherCATFocusReentrantPage");
    QVERIFY(reentrantPage);
    bool clearReentryObserved = false;
    connect(reentrantPage, &QObject::destroyed, &details, [&] {
        if (clearReentryObserved)
            return;
        clearReentryObserved = true;
        controller.selectionService()->setCurrentNodeId(alpha.id);
    });
    controller.selectionService()->setCurrentNodeId(beta.id);
    QVERIFY(clearReentryObserved);
    QCOMPARE(details.currentContext().nodeId, alpha.id);
    QCOMPARE(tabs->count(), reentrantPageCount);

    QWidget *destroyedFocusPage = tabs->currentWidget();
    QVERIFY(destroyedFocusPage);
    QLineEdit *destroyedFocusField
        = destroyedFocusPage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(destroyedFocusField);
    destroyedFocusField->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), destroyedFocusField);
    bool destroyedFocusMoved = false;
    connect(destroyedFocusPage, &QObject::destroyed, &details, [&] {
        destroyedFocusMoved = true;
        title->setFocus(Qt::OtherFocusReason);
    });
    controller.selectionService()->setCurrentNodeId(beta.id);
    QVERIFY(destroyedFocusMoved);
    QCOMPARE(details.currentContext().nodeId, beta.id);
    QCOMPARE(tabs->count(), reentrantPageCount);
    QTRY_COMPARE(QApplication::focusWidget(), title);
    controller.selectionService()->setCurrentNodeId(alpha.id);
    QCOMPARE(details.currentContext().nodeId, alpha.id);
    QTRY_COMPARE(QApplication::focusWidget(), title);

    const int reentrantIndex = pageIndexForKey(reentrantPageKey);
    QVERIFY(reentrantIndex >= 0);
    tabs->setCurrentIndex(reentrantIndex);
    QWidget *oldReentrantPage = tabs->currentWidget();
    QVERIFY(oldReentrantPage);
    QCOMPARE(oldReentrantPage->property("EtherCAT.PageKey").toString(), reentrantPageKey);
    oldReentrantPage->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), oldReentrantPage);
    QPointer<QWidget> destroyedReentrantPage = oldReentrantPage;
    bool nestedAvailabilityObserved = false;
    reentrantProvider.onUpdate = [&] {
        nestedAvailabilityObserved = true;
        reentrantProvider.setAvailable(false);
        reentrantProvider.setAvailable(true);
    };
    controller.selectionService()->setCurrentNodeId(beta.id);
    QVERIFY(nestedAvailabilityObserved);
    QTRY_VERIFY(destroyedReentrantPage.isNull());
    QCOMPARE(details.currentContext().nodeId, beta.id);
    QCOMPARE(tabs->count(), reentrantPageCount);
    QCOMPARE(
        tabs->currentWidget()->property("EtherCAT.PageKey").toString(), reentrantPageKey);
    QWidget *restoredReentrantPage = tabs->currentWidget();
    QVERIFY(restoredReentrantPage);
    QCOMPARE(restoredReentrantPage->objectName(), QString("EtherCATFocusReentrantPage"));
    QTRY_COMPARE(QApplication::focusWidget(), restoredReentrantPage);

    bool stickyFocusCancellationObserved = false;
    reentrantProvider.onUpdate = [&] {
        stickyFocusCancellationObserved = true;
        title->setFocus(Qt::OtherFocusReason);
        tabs->setFocus(Qt::OtherFocusReason);
        reentrantProvider.setAvailable(false);
        reentrantProvider.setAvailable(true);
    };
    controller.selectionService()->setCurrentNodeId(alpha.id);
    QVERIFY(stickyFocusCancellationObserved);
    QCOMPARE(details.currentContext().nodeId, alpha.id);
    QCOMPARE(tabs->count(), reentrantPageCount);
    QCOMPARE(
        tabs->currentWidget()->property("EtherCAT.PageKey").toString(), reentrantPageKey);
    QWidget *focusAfterCancellation = QApplication::focusWidget();
    QVERIFY(focusAfterCancellation);
    QVERIFY(focusAfterCancellation == tabs || tabs->isAncestorOf(focusAfterCancellation));
    QVERIFY(focusAfterCancellation != tabs->currentWidget());

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_DETAILS_FOCUS_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    const int stableFocusIndex = pageIndexForKey(pageKey);
    QVERIFY(stableFocusIndex >= 0);
    tabs->setCurrentIndex(stableFocusIndex);
    QWidget *stableFocusPage = tabs->currentWidget();
    QVERIFY(stableFocusPage);
    QLineEdit *stableFocusField
        = stableFocusPage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(stableFocusField);
    stableFocusField->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), stableFocusField);
    ExtensionSystem::PluginManager::removeObject(&reentrantProvider);
    reentrantProviderRegistered = false;
    tabs->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(
        QApplication::focusWidget() == tabs
        || tabs->isAncestorOf(QApplication::focusWidget()));
    QPointer<QWidget> explicitTabFocus = QApplication::focusWidget();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(QApplication::focusWidget(), explicitTabFocus.data());

    QLabel *emptyState = details.findChild<QLabel *>("EtherCATWorkbenchEmptyState");
    QVERIFY(emptyState);
    QLineEdit *emptySource = tabs->currentWidget()->findChild<QLineEdit *>(
        "EtherCATProjectGeneralCreatedBy");
    QVERIFY(emptySource);
    emptySource->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), emptySource);
    controller.selectionService()->clear();
    QTRY_COMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QTRY_VERIFY(emptyState->isVisible());
    QTRY_VERIFY(!tabs->isVisible());
    QVERIFY(emptyState->focusPolicy() & Qt::TabFocus);
    QTRY_COMPARE(QApplication::focusWidget(), emptyState);
}

void EtherCATWorkbenchTests::testProjectScopedDetailsRefreshPreservesGeneralDraft()
{
    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Data::DeviceSummary device = deviceSummaries(1).first();
    const TestProjectFile alpha = writeProjectWithSlave(
        directory, device, "details-draft-alpha.ecatproject", "Details Draft Alpha");
    const TestProjectFile beta = writeProjectWithSlave(
        directory, device, "details-draft-beta.ecatproject", "Details Draft Beta");
    QVERIFY(!alpha.path.isEmpty());
    QVERIFY(!beta.path.isEmpty());

    const ProjectExplorer::OpenProjectResult alphaOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(alpha.path, false);
    QVERIFY2(alphaOpened, qPrintable(alphaOpened.errorMessage()));
    const ProjectExplorer::OpenProjectResult betaOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(beta.path, false);
    QVERIFY2(betaOpened, qPrintable(betaOpened.errorMessage()));
    auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (projectService->project(beta.projectId))
            ProjectExplorer::ProjectManager::removeProject(betaOpened.project());
        if (projectService->project(alpha.projectId))
            ProjectExplorer::ProjectManager::removeProject(alphaOpened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(alpha.projectId).has_value());
    QTRY_VERIFY(projectService->project(beta.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(alpha.projectId).isValid());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(beta.projectId).isValid());

    controller.selectionService()->setCurrentNodeId(alpha.projectId);
    DetailsView details(&controller);
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QTRY_COMPARE(details.currentContext().nodeId, alpha.projectId);

    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::GENERAL_PAGE_ID).toString());
    QLabel *title = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QVERIFY(page);
    QVERIFY(title);
    QLineEdit *name = page->findChild<QLineEdit *>("EtherCATProjectGeneralName");
    QVERIFY(name);
    QCOMPARE(name->text(), QString("Details Draft Alpha"));

    const QString draft = QString::fromUtf8("Alpha pending draft %1 / 未保存草稿");
    name->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), name);
    name->setText(draft);
    name->setModified(true);

    const QString renamedBeta = QString::fromUtf8("Details Draft Beta / 外部更新");
    QVERIFY_RESULT(projectService->renameProject(beta.projectId, renamedBeta));
    QTRY_COMPARE(projectService->project(beta.projectId)->name, renamedBeta);
    QCOMPARE(projectService->project(alpha.projectId)->name, QString("Details Draft Alpha"));
    QCOMPARE(controller.selectionService()->currentNodeId(), alpha.projectId);
    QCOMPARE(details.currentContext().projectId, alpha.projectId);
    QCOMPARE(details.currentContext().nodeId, alpha.projectId);
    QCOMPARE(name->text(), draft);
    QVERIFY(name->isModified());
    QCOMPARE(QApplication::focusWidget(), name);
    QCOMPARE(title->text(), QString("Details Draft Alpha"));

    const QString renamedAlpha = QString::fromUtf8("Details Draft Alpha / 已持久化");
    QVERIFY_RESULT(projectService->renameProject(alpha.projectId, renamedAlpha));
    QTRY_COMPARE(projectService->project(alpha.projectId)->name, renamedAlpha);
    QTRY_COMPARE(name->text(), renamedAlpha);
    QVERIFY(!name->isModified());
    QTRY_COMPARE(title->text(), renamedAlpha);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(betaOpened.project());
    ProjectExplorer::ProjectManager::removeProject(alphaOpened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(!projectService->project(alpha.projectId).has_value());
    QTRY_VERIFY(!projectService->project(beta.projectId).has_value());
    projectCleanup.dismiss();
}

void EtherCATWorkbenchTests::testNavigationSetActiveProjectCommand()
{
    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    QTRY_COMPARE(::Core::ModeManager::currentModeId(), Utils::Id(Constants::MODE_ID));
    ::Core::Command *setActiveCommand
        = ::Core::ActionManager::command(Constants::SET_ACTIVE_PROJECT_ACTION_ID);
    QVERIFY(setActiveCommand);
    QAction *setActiveAction = setActiveCommand->action();
    QVERIFY(setActiveAction);
    QAction *setActiveContextAction
        = setActiveCommand->actionForContext(Constants::CONTEXT_ID);
    QVERIFY(setActiveContextAction);

    QWidget *modeWidget = ::Core::ModeManager::currentMode()->widget();
    QVERIFY(modeWidget);
    QToolBar *commandStrip
        = modeWidget->findChild<QToolBar *>("EtherCATWorkbenchCommandStrip");
    QVERIFY(commandStrip);
    QVERIFY(!commandStrip->actions().contains(setActiveAction));
    ::Core::ActionContainer *ethercatMenu
        = ::Core::ActionManager::actionContainer(Constants::MENU_ID);
    QVERIFY(ethercatMenu);
    QVERIFY(!ethercatMenu->menu()->actions().contains(setActiveAction));

    WorkbenchController controller;
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QList<Data::DeviceSummary> devices = deviceSummaries(2);
    const TestProjectFile first = writeProjectWithSlave(
        directory, devices.at(0), "alpha-command.ecatproject", "Alpha Command Project");
    const TestProjectFile second = writeProjectWithSlave(
        directory, devices.at(1), "beta-command.ecatproject", "Beta Command Project");
    const ProjectExplorer::OpenProjectResult firstOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(first.path, false);
    QVERIFY2(firstOpened, qPrintable(firstOpened.errorMessage()));
    const ProjectExplorer::OpenProjectResult secondOpened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(second.path, false);
    QVERIFY2(secondOpened, qPrintable(secondOpened.errorMessage()));
    QTRY_COMPARE(projectService->projects().size(), 2);
    QVERIFY_RESULT(projectService->activateProject(first.projectId));
    QTRY_COMPARE(projectService->activeProjectId(), first.projectId);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(760, 420);
    navigation.show();
    DetailsView details(&controller);
    details.resize(1100, 760);
    details.show();
    QTRY_VERIFY(navigation.isVisible());
    QTRY_VERIFY(details.isVisible());
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(navigation.treeView()->hasFocus());
    QTRY_COMPARE(
        ::Core::ICore::currentContextWidget(), static_cast<QWidget *>(&navigation));

    struct PopupCapture
    {
        QList<QAction *> actions;
        QStringList actionTexts;
        QList<bool> separators;
        bool seen = false;
        bool rendered = true;
    };
    const auto capturePopup = [&navigation](
                                  const QString &renderPath = {},
                                  const QString &triggerText = {}) {
        PopupCapture capture;
        capture.rendered = renderPath.isEmpty();
        QTimer::singleShot(0, &navigation, [&capture, renderPath, triggerText] {
            auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!popup)
                return;
            capture.seen = true;
            QAction *actionToTrigger = nullptr;
            for (QAction *action : popup->actions()) {
                const bool separator = action->isSeparator();
                capture.actionTexts.append(action->text());
                capture.separators.append(separator);
                if (!separator) {
                    capture.actions.append(action);
                    if (action->text() == triggerText)
                        actionToTrigger = action;
                }
            }
            if (!renderPath.isEmpty())
                capture.rendered = popup->grab().save(renderPath);
            if (actionToTrigger)
                actionToTrigger->trigger();
            popup->close();
        });
        emit navigation.treeView()->customContextMenuRequested(QPoint(-1, -1));
        return capture;
    };

    const QModelIndex firstProject = findById(navigation.treeView()->model(), first.projectId);
    const QModelIndex secondProject = findById(navigation.treeView()->model(), second.projectId);
    QVERIFY(firstProject.isValid());
    QVERIFY(secondProject.isValid());

    navigation.treeView()->setCurrentIndex(firstProject);
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), first.projectId);
    QCOMPARE(projectService->activeProjectId(), first.projectId);
    const PopupCapture activePopup = capturePopup();
    QVERIFY(activePopup.seen);
    QVERIFY(!activePopup.actions.contains(setActiveAction));
    QVERIFY(!setActiveAction->isEnabled());

    navigation.treeView()->setCurrentIndex(secondProject);
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), second.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, second.projectId);
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::Project);
    QCOMPARE(projectService->activeProjectId(), first.projectId);

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_SET_ACTIVE_PROJECT_RENDER_PATH");
    const PopupCapture inactivePopup = capturePopup(renderPath);
    QVERIFY(inactivePopup.seen);
    QVERIFY(inactivePopup.rendered);
    QVERIFY(setActiveAction->isEnabled());
    QTRY_VERIFY(setActiveContextAction->isEnabled());
    QCOMPARE(setActiveAction->text(), QString("Set as Active Project"));
    QVERIFY(!setActiveAction->toolTip().isEmpty());
    QVERIFY(!setActiveAction->statusTip().isEmpty());
    const int setActiveIndex = inactivePopup.actionTexts.indexOf(setActiveAction->text());
    QVERIFY(setActiveIndex > 0);
    QAction *popupSetActiveAction = nullptr;
    for (QAction *action : inactivePopup.actions) {
        if (action->text() == setActiveAction->text()) {
            popupSetActiveAction = action;
            break;
        }
    }
    QCOMPARE(popupSetActiveAction, setActiveAction);
    QVERIFY(inactivePopup.separators.at(setActiveIndex - 1));
    QVERIFY(setActiveIndex + 1 < inactivePopup.separators.size());
    QVERIFY(inactivePopup.separators.at(setActiveIndex + 1));
    ::Core::Command *copyCommand
        = ::Core::ActionManager::command(Constants::COPY_NODE_ID_ACTION_ID);
    QVERIFY(copyCommand);
    QVERIFY(setActiveIndex < inactivePopup.actionTexts.indexOf(copyCommand->action()->text()));

    QSignalSpy modelResetSpy(controller.treeModel(), &QAbstractItemModel::modelReset);
    const QPersistentModelIndex firstPersistent(
        controller.treeModel()->indexForNodeId(first.projectId));
    const QPersistentModelIndex secondPersistent(
        controller.treeModel()->indexForNodeId(second.projectId));
    QVERIFY(firstPersistent.isValid());
    QVERIFY(secondPersistent.isValid());
    const int resetCount = modelResetSpy.count();
    const std::optional<Data::ProjectSnapshot> firstBefore
        = projectService->project(first.projectId);
    const std::optional<Data::ProjectSnapshot> secondBefore
        = projectService->project(second.projectId);
    QVERIFY(firstBefore);
    QVERIFY(secondBefore);

    const PopupCapture activationPopup = capturePopup({}, setActiveAction->text());
    QVERIFY(activationPopup.seen);
    QVERIFY(activationPopup.actions.contains(setActiveAction));
    QTRY_COMPARE(projectService->activeProjectId(), second.projectId);
    QTRY_COMPARE(
        controller.treeModel()->indexForNodeId(first.projectId, 1).data().toString(),
        QString("Offline"));
    QTRY_COMPARE(
        controller.treeModel()->indexForNodeId(second.projectId, 1).data().toString(),
        QString("Active project | Offline"));
    QCOMPARE(modelResetSpy.count(), resetCount);
    QVERIFY(firstPersistent.isValid());
    QVERIFY(secondPersistent.isValid());
    QCOMPARE(controller.selectionService()->currentNodeId(), second.projectId);
    QCOMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        second.projectId);
    QCOMPARE(details.currentContext().nodeId, second.projectId);
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::Project);
    QVERIFY(!(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(first.masterId))
              & Qt::ItemIsDropEnabled));
    QVERIFY(controller.treeModel()->flags(controller.treeModel()->indexForNodeId(second.masterId))
            & Qt::ItemIsDropEnabled);
    QVERIFY(projectService->project(first.projectId) == firstBefore);
    QVERIFY(projectService->project(second.projectId) == secondBefore);
    const PopupCapture newActivePopup = capturePopup();
    QVERIFY(newActivePopup.seen);
    QVERIFY(!newActivePopup.actions.contains(setActiveAction));
    QVERIFY(!setActiveAction->isEnabled());

    navigation.filterEdit()->setText("Active project");
    QTRY_VERIFY(!findById(navigation.treeView()->model(), first.projectId).isValid());
    QTRY_VERIFY(findById(navigation.treeView()->model(), second.projectId).isValid());
    navigation.filterEdit()->clear();
    QTRY_VERIFY(findById(navigation.treeView()->model(), first.projectId).isValid());

    controller.selectionService()->setCurrentNodeId(second.masterId);
    QTRY_COMPARE(details.currentContext().nodeId, second.masterId);
    const PopupCapture masterPopup = capturePopup();
    QVERIFY(masterPopup.seen);
    QVERIFY(!masterPopup.actions.contains(setActiveAction));
    QVERIFY(!setActiveAction->isEnabled());

    controller.selectionService()->setCurrentNodeId(first.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, first.projectId);
    ::Core::ModeManager::activateMode(::Core::Constants::MODE_EDIT);
    QTRY_COMPARE(
        ::Core::ModeManager::currentModeId(), Utils::Id(::Core::Constants::MODE_EDIT));
    navigation.activateWindow();
    navigation.treeView()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(navigation.treeView()->hasFocus());
    const PopupCapture firstInactivePopup = capturePopup({}, setActiveAction->text());
    QVERIFY(firstInactivePopup.seen);
    QVERIFY(firstInactivePopup.actions.contains(setActiveAction));
    QTRY_COMPARE(projectService->activeProjectId(), first.projectId);
    QVERIFY(!setActiveAction->isEnabled());

    ProjectExplorer::ProjectManager::removeProject(firstOpened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(!projectService->project(first.projectId).has_value());
    QTRY_COMPARE(projectService->activeProjectId(), second.projectId);
    QTRY_VERIFY(controller.selectionService()->currentNodeId() != first.projectId);
    controller.selectionService()->setCurrentNodeId(second.projectId);
    QTRY_COMPARE(details.currentContext().nodeId, second.projectId);
    const PopupCapture fallbackActivePopup = capturePopup();
    QVERIFY(fallbackActivePopup.seen);
    QVERIFY(!fallbackActivePopup.actions.contains(setActiveAction));
    QVERIFY(!setActiveAction->isEnabled());

    ProjectExplorer::ProjectManager::removeProject(secondOpened.project());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(projectService->projects().isEmpty());
    QTRY_VERIFY(projectService->activeProjectId().isNull());
    QTRY_VERIFY(controller.selectionService()->currentNodeId().isNull());
    const PopupCapture emptyPopup = capturePopup();
    QVERIFY(emptyPopup.seen);
    QVERIFY(!emptyPopup.actions.contains(setActiveAction));
    QVERIFY(!setActiveAction->isEnabled());
}

void EtherCATWorkbenchTests::testNavigationFilterEmptyState()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Filter empty state");
    QList<Data::DeviceSummary> devices = deviceSummaries(2);
    controller.treeModel()->setProjects({project});
    controller.treeModel()->syncDevices(devices);

    WorkbenchNavigationWidget navigation(&controller);
    navigation.resize(420, 480);
    navigation.show();
    QTRY_VERIFY(navigation.isVisible());
    controller.selectionService()->setCurrentNodeId(devices.first().id);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        devices.first().id);

    const QModelIndex repositoryIndex = findByKind(
        navigation.treeView()->model(), Core::WorkbenchNodeKind::DeviceRepository);
    QVERIFY(repositoryIndex.isValid());
    navigation.treeView()->collapse(repositoryIndex);
    QVERIFY(!navigation.treeView()->isExpanded(repositoryIndex));

    QWidget *emptyState
        = navigation.findChild<QWidget *>("EtherCATWorkbenchFilterEmptyState");
    QLabel *emptyMessage
        = navigation.findChild<QLabel *>("EtherCATWorkbenchFilterEmptyMessage");
    QPushButton *clearFilter
        = navigation.findChild<QPushButton *>("EtherCATWorkbenchClearFilter");
    QVERIFY(emptyState);
    QVERIFY(emptyMessage);
    QVERIFY(clearFilter);
    QVERIFY(!navigation.filterEdit()->accessibleName().isEmpty());
    QVERIFY(!navigation.filterEdit()->accessibleDescription().isEmpty());
    QVERIFY(!emptyState->accessibleName().isEmpty());
    QVERIFY(!emptyState->accessibleDescription().isEmpty());
    QVERIFY(!emptyMessage->accessibleName().isEmpty());
    QVERIFY(!clearFilter->accessibleDescription().isEmpty());
    QVERIFY(navigation.treeView()->isVisible());
    QVERIFY(!emptyState->isVisible());

    const Data::NodeId selectedId = devices.first().id;
    const QString missingQuery
        = QString::fromUtf8("未匹配的超长 EtherCAT 设备 Ω — filter remains local and offline");
    navigation.filterEdit()->setText(missingQuery);
    QTRY_COMPARE(navigation.treeView()->model()->rowCount(), 0);
    QTRY_VERIFY(emptyState->isVisible());
    QVERIFY(!navigation.treeView()->isVisible());
    QCOMPARE(emptyMessage->text(), QString("No EtherCAT nodes match the current filter."));
    QCOMPARE(clearFilter->text(), QString("Clear Filter"));
    QCOMPARE(controller.selectionService()->currentNodeId(), selectedId);
    QCOMPARE(navigation.focusProxy(), clearFilter);
    navigation.setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), clearFilter);

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_NAV_FILTER_RENDER_PATH");
    if (!renderPath.isEmpty())
        QVERIFY(navigation.grab().save(renderPath));

    QTest::keyClick(clearFilter, Qt::Key_Space);
    QTRY_VERIFY(navigation.filterEdit()->text().isEmpty());
    QTRY_VERIFY(navigation.treeView()->isVisible());
    QVERIFY(!emptyState->isVisible());
    QCOMPARE(navigation.focusProxy(), navigation.treeView());
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        selectedId);
    QTRY_COMPARE(QApplication::focusWidget(), navigation.treeView());

    navigation.filterEdit()->setText(missingQuery);
    QTRY_VERIFY(emptyState->isVisible());
    Data::DeviceSummary lateMatch = deviceSummaries(1).first();
    lateMatch.name = missingQuery;
    devices.append(lateMatch);
    controller.treeModel()->syncDevices(devices);
    QTRY_VERIFY(navigation.treeView()->isVisible());
    QVERIFY(!emptyState->isVisible());
    const QModelIndex lateMatchIndex = findById(navigation.treeView()->model(), lateMatch.id);
    QVERIFY(lateMatchIndex.isValid());
    QTRY_VERIFY(navigation.treeView()->isExpanded(lateMatchIndex.parent()));
    QVERIFY(!navigation.treeView()->visualRect(lateMatchIndex).isEmpty());
    QCOMPARE(controller.selectionService()->currentNodeId(), selectedId);

    devices.removeLast();
    controller.treeModel()->syncDevices(devices);
    QTRY_VERIFY(emptyState->isVisible());
    const Data::NodeId externallySelectedId = devices.last().id;
    controller.selectionService()->setCurrentNodeId(externallySelectedId);
    QTRY_VERIFY(navigation.filterEdit()->text().isEmpty());
    QTRY_VERIFY(navigation.treeView()->isVisible());
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        externallySelectedId);
    controller.selectionService()->clear();
}

void EtherCATWorkbenchTests::testNavigationKeyboardFocus()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Keyboard navigation");
    controller.treeModel()->setProjects({project});
    controller.selectionService()->clear();

    WorkbenchNavigationWidget navigation(&controller);
    navigation.show();
    QTRY_VERIFY(navigation.isVisible());

    QCOMPARE(navigation.focusProxy(), navigation.treeView());
    navigation.setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), navigation.treeView());

    const QModelIndex projectIndex = findById(navigation.treeView()->model(), project.id);
    QVERIFY(projectIndex.isValid());
    navigation.treeView()->setCurrentIndex(projectIndex);
    QTRY_COMPARE(controller.selectionService()->currentNodeId(), project.id);

    QTest::keyClick(navigation.treeView(), Qt::Key_Down);
    const Data::NodeId currentId = navigation.treeView()
                                       ->currentIndex()
                                       .data(WorkbenchTreeModel::NodeIdRole)
                                       .value<Data::NodeId>();
    QCOMPARE(currentId, project.nodes.at(1).id);
    QCOMPARE(controller.selectionService()->currentNodeId(), currentId);
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
    QCOMPARE(provider.pages(context).size(), 7);

    std::unique_ptr<QWidget> ethercatPage(provider.createPage(Constants::ETHERCAT_PAGE_ID, nullptr));
    QVERIFY(ethercatPage);
    provider.updatePage(Constants::ETHERCAT_PAGE_ID, ethercatPage.get(), context);
    QWidget *slaveForm = ethercatPage->findChild<QWidget *>("EtherCATEthercatSlaveForm");
    QTreeWidget *ethercatTree = ethercatPage->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(slaveForm);
    QVERIFY(ethercatTree);
    QVERIFY(slaveForm->isHidden());
    QCOMPARE(ethercatTree->topLevelItemCount(), 2);

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
    QVERIFY(dcMode->isEnabled());
    QVERIFY(dcMode->lineEdit()->isReadOnly());
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
    QVERIFY(!directChildByKind(
                 controller.treeModel(), Core::WorkbenchNodeKind::Placeholder, masterIndex)
                 .isValid());

    BuiltinPropertyPageProvider pages(&controller);
    const Core::PropertyPageContext context = controller.treeModel()->contextForIndex(slave);
    QCOMPARE(pages.pages(context).size(), 7);
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

    std::unique_ptr<QWidget> ethercatPage(pages.createPage(Constants::ETHERCAT_PAGE_ID, nullptr));
    pages.updatePage(Constants::ETHERCAT_PAGE_ID, ethercatPage.get(), context);
    QSpinBox *alias = ethercatPage->findChild<QSpinBox *>("EtherCATEthercatAlias");
    QLineEdit *ethercatType = ethercatPage->findChild<QLineEdit *>("EtherCATEthercatType");
    QLineEdit *autoIncAddress = ethercatPage->findChild<QLineEdit *>(
        "EtherCATEthercatAutoIncAddress");
    QLineEdit *previousPort = ethercatPage->findChild<QLineEdit *>("EtherCATEthercatPreviousPort");
    QWidget *slaveForm = ethercatPage->findChild<QWidget *>("EtherCATEthercatSlaveForm");
    QTreeWidget *ethercatTree = ethercatPage->findChild<QTreeWidget *>("EtherCATWorkbenchPageTree");
    QVERIFY(alias);
    QVERIFY(ethercatType);
    QVERIFY(autoIncAddress);
    QVERIFY(previousPort);
    QVERIFY(slaveForm);
    QVERIFY(ethercatTree);
    QCOMPARE(alias->value(), 3);
    QCOMPARE(ethercatType->text(), QString("AX5000"));
    QCOMPARE(autoIncAddress->text(), QString("0x0000"));
    QCOMPARE(ethercatTree->topLevelItemCount(), 2);

    const Data::NodeId secondSlaveId = Data::NodeId::create();
    project.slaves.append(
        {secondSlaveId, master, 1, found->identity, 18, 4, "Configured I/O", found->id, {}, {}, {}});
    project.nodes.append({secondSlaveId, master, Data::ProjectNodeKind::Slave, "Configured I/O"});
    controller.treeModel()->setProjects({project});
    const QModelIndex secondSlave = controller.treeModel()->indexForNodeId(secondSlaveId);
    QVERIFY(secondSlave.isValid());
    pages.updatePage(
        Constants::ETHERCAT_PAGE_ID,
        ethercatPage.get(),
        controller.treeModel()->contextForIndex(secondSlave));
    QCOMPARE(autoIncAddress->text(), QString("0xffff"));
    QVERIFY(previousPort->text().contains("Configured Servo"));
    pages.updatePage(
        Constants::ETHERCAT_PAGE_ID,
        ethercatPage.get(),
        controller.treeModel()->contextForIndex(controller.treeModel()->indexForNodeId(master)));
    QVERIFY(slaveForm->isHidden());
    QVERIFY(!ethercatPage->findChild<QWidget *>("EtherCATMasterEthercatForm")->isHidden());
    QCOMPARE(ethercatTree->topLevelItemCount(), 0);
    QCOMPARE(ethercatTree->columnCount(), 10);

    project.slaves.first().deviceDescriptionId = {};
    controller.treeModel()->setProjects({project});
    const QModelIndex unreferencedSlave = controller.treeModel()->indexForNodeId(slaveId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        processPage.get(),
        controller.treeModel()->contextForIndex(unreferencedSlave));
    pages.updatePage(
        Constants::GENERAL_PAGE_ID,
        generalPage.get(),
        controller.treeModel()->contextForIndex(unreferencedSlave));
    pages.updatePage(
        Constants::ETHERCAT_PAGE_ID,
        ethercatPage.get(),
        controller.treeModel()->contextForIndex(unreferencedSlave));
    QCOMPARE(syncManagers->model()->rowCount(), 0);
    QVERIFY(processPage->findChild<QLabel *>("EtherCATProcessDataSummary")
                ->text()
                .contains("No Process Data", Qt::CaseInsensitive));
    QVERIFY(processPage->findChild<QPushButton *>("EtherCATProcessDataRestoreDefaults")->isHidden());
    QCOMPARE(
        generalPage->findChild<QLineEdit *>("EtherCATGeneralType")->text(),
        QString("Unknown ESI device"));
    QCOMPARE(ethercatType->text(), QString("Unknown ESI device"));
    QCOMPARE(ethercatTree->topLevelItemCount(), 0);
}

void EtherCATWorkbenchTests::testTwinCatProcessDataTree()
{
    const ProcessTreeFixture fixture = processTreeFixture();
    WorkbenchController controller;
    QAbstractItemModelTester modelTester(
        controller.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    controller.treeModel()->setProjects({fixture.project});

    const QModelIndex slave = findById(controller.treeModel(), fixture.slaveId);
    QVERIFY(slave.isValid());
    QCOMPARE(controller.treeModel()->rowCount(slave), 5);

    const QList<Core::WorkbenchNodeKind> expectedKinds = {
        Core::WorkbenchNodeKind::ProcessInputs,
        Core::WorkbenchNodeKind::ProcessOutputs,
        Core::WorkbenchNodeKind::RxPdoGroup,
        Core::WorkbenchNodeKind::TxPdoGroup,
        Core::WorkbenchNodeKind::Modules,
    };
    for (int row = 0; row < expectedKinds.size(); ++row) {
        const QModelIndex branch = controller.treeModel()->index(row, 0, slave);
        QCOMPARE(
            branch.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
            expectedKinds.at(row));
        QVERIFY(!branch.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>().isNull());
        QVERIFY(!branch.data(Qt::DecorationRole).value<QIcon>().isNull());
    }

    const QModelIndex inputs = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::ProcessInputs, slave);
    const QModelIndex outputs = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::ProcessOutputs, slave);
    const QModelIndex rxPdos = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::RxPdoGroup, slave);
    const QModelIndex txPdos = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::TxPdoGroup, slave);
    const QModelIndex modules = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::Modules, slave);
    QVERIFY(inputs.isValid());
    QVERIFY(outputs.isValid());
    QVERIFY(rxPdos.isValid());
    QVERIFY(txPdos.isValid());
    QVERIFY(modules.isValid());
    QCOMPARE(inputs.data().toString(), QString("Inputs"));
    QCOMPARE(outputs.data().toString(), QString("Outputs"));
    QCOMPARE(rxPdos.data().toString(), QString("RxPDO"));
    QCOMPARE(txPdos.data().toString(), QString("TxPDO"));
    QCOMPARE(modules.data().toString(), QString("Modules / Channels"));

    QCOMPARE(controller.treeModel()->rowCount(inputs), 1);
    QCOMPARE(controller.treeModel()->rowCount(outputs), 1);
    QCOMPARE(controller.treeModel()->rowCount(rxPdos), 1);
    QCOMPARE(controller.treeModel()->rowCount(txPdos), 1);
    QCOMPARE(controller.treeModel()->rowCount(modules), 1);

    const QModelIndex inputEntry = controller.treeModel()->index(0, 0, inputs);
    const QModelIndex outputEntry = controller.treeModel()->index(0, 0, outputs);
    QCOMPARE(inputEntry.data().toString(), QString("Statusword"));
    QCOMPARE(outputEntry.data().toString(), QString("Controlword"));
    QCOMPARE(
        inputEntry.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::PdoEntry);
    QCOMPARE(controller.treeModel()->sourceNodeId(inputEntry), fixture.txEntryId);
    QCOMPARE(controller.treeModel()->sourceNodeId(outputEntry), fixture.rxEntryId);

    const QModelIndex rxPdo = controller.treeModel()->index(0, 0, rxPdos);
    const QModelIndex txPdo = controller.treeModel()->index(0, 0, txPdos);
    QCOMPARE(rxPdo.data().toString(), QString("Drive Command"));
    QCOMPARE(txPdo.data().toString(), QString("Drive Status"));
    QCOMPARE(
        rxPdo.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Pdo);
    QCOMPARE(controller.treeModel()->sourceNodeId(rxPdo), fixture.rxPdoId);
    QCOMPARE(controller.treeModel()->sourceNodeId(txPdo), fixture.txPdoId);
    QCOMPARE(controller.treeModel()->rowCount(rxPdo), 1);
    QCOMPARE(controller.treeModel()->rowCount(txPdo), 1);
    QCOMPARE(controller.treeModel()->sourceNodeId(controller.treeModel()->index(0, 0, rxPdo)),
             fixture.rxEntryId);
    QCOMPARE(controller.treeModel()->sourceNodeId(controller.treeModel()->index(0, 0, txPdo)),
             fixture.txEntryId);

    const QModelIndex modulePlaceholder = controller.treeModel()->index(0, 0, modules);
    QCOMPARE(
        modulePlaceholder.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Placeholder);
    QVERIFY(!(controller.treeModel()->flags(modulePlaceholder) & Qt::ItemIsSelectable));
    QVERIFY(!findBySourceId(controller.treeModel(), fixture.unselectedEntryId).isValid());

    const Data::NodeId rxPdoViewId
        = rxPdo.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    const Data::NodeId outputEntryViewId
        = outputEntry.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    QVERIFY(rxPdoViewId != outputEntryViewId);
    QCOMPARE(controller.treeModel()->offlineSlave(rxPdoViewId)->id, fixture.slaveId);
    QCOMPARE(controller.treeModel()->offlineSlave(outputEntryViewId)->id, fixture.slaveId);
    QCOMPARE(controller.treeModel()->contextForIndex(rxPdo).projectId, fixture.project.id);

    BuiltinPropertyPageProvider pages(&controller);
    const Core::PropertyPageContext rxContext = controller.treeModel()->contextForIndex(rxPdo);
    QCOMPARE(
        pages.pages(rxContext),
        QList<Core::PropertyPageDescriptor>(
            {{Utils::Id(Constants::PROCESS_DATA_PAGE_ID), "Process Data", 300}}));
    std::unique_ptr<QWidget> processPage(
        pages.createPage(Constants::PROCESS_DATA_PAGE_ID, nullptr));
    pages.updatePage(Constants::PROCESS_DATA_PAGE_ID, processPage.get(), rxContext);
    QTableView *pdoList = processPage->findChild<QTableView *>("EtherCATProcessDataPdoList");
    QTableView *pdoContent = processPage->findChild<QTableView *>("EtherCATProcessDataPdoContent");
    QTableView *pdoAssignments = processPage->findChild<QTableView *>(
        "EtherCATProcessDataAssignments");
    QVERIFY(pdoList);
    QVERIFY(pdoContent);
    QVERIFY(pdoAssignments);
    QCOMPARE(pdoList->currentIndex().siblingAtColumn(2).data().toString(), QString("Drive Command"));
    QCOMPARE(pdoContent->model()->index(0, 4).data().toString(), QString("Controlword"));
    QVERIFY(!(
        pdoAssignments->model()->flags(pdoAssignments->model()->index(0, 0))
        & Qt::ItemIsUserCheckable));

    const Core::PropertyPageContext inputContext = controller.treeModel()->contextForIndex(inputEntry);
    pages.updatePage(Constants::PROCESS_DATA_PAGE_ID, processPage.get(), inputContext);
    QCOMPARE(pdoList->currentIndex().siblingAtColumn(2).data().toString(), QString("Drive Status"));
    QCOMPARE(pdoContent->model()->index(0, 4).data().toString(), QString("Statusword"));

    const Core::PropertyPageContext modulesContext = controller.treeModel()->contextForIndex(modules);
    QCOMPARE(
        pages.pages(modulesContext),
        QList<Core::PropertyPageDescriptor>(
            {{Utils::Id(Constants::GENERAL_PAGE_ID), "General", 100}}));

    WorkbenchNavigationWidget navigation(&controller);
    QCOMPARE(navigation.treeView()->textElideMode(), Qt::ElideNone);
    QCOMPARE(
        navigation.treeView()->header()->sectionResizeMode(0),
        QHeaderView::ResizeToContents);
    QCOMPARE(
        navigation.treeView()->header()->sectionResizeMode(1),
        QHeaderView::ResizeToContents);
    QVERIFY(!navigation.treeView()->accessibleName().isEmpty());
    QVERIFY(!navigation.treeView()->accessibleDescription().isEmpty());
    navigation.filterEdit()->setText("Controlword");
    QTRY_VERIFY(findById(navigation.treeView()->model(), outputEntryViewId).isValid());
    controller.selectionService()->setCurrentNodeId(outputEntryViewId);
    QTRY_COMPARE(
        navigation.treeView()
            ->currentIndex()
            .data(WorkbenchTreeModel::NodeIdRole)
            .value<Data::NodeId>(),
        outputEntryViewId);

    Data::ProjectSnapshot renamed = fixture.project;
    renamed.slaves.first().processData.pdos.first().name = "Renamed Command";
    controller.treeModel()->setProjects({renamed});
    QCOMPARE(controller.treeModel()->indexForNodeId(rxPdoViewId).data().toString(),
             QString("Renamed Command"));

    Data::ProjectSnapshot large = projectSnapshot("Large Process Tree");
    const Data::NodeId largeMaster = masterId(large);
    for (int position = 0; position < 128; ++position) {
        Data::OfflineSlaveConfiguration slaveConfiguration = fixture.project.slaves.first();
        slaveConfiguration.id = Data::NodeId::create();
        slaveConfiguration.masterId = largeMaster;
        slaveConfiguration.position = position;
        slaveConfiguration.name = QString("Servo %1").arg(position, 3, 10, QLatin1Char('0'));
        large.slaves.append(slaveConfiguration);
        large.nodes.append(
            {slaveConfiguration.id,
             largeMaster,
             Data::ProjectNodeKind::Slave,
             slaveConfiguration.name});
    }
    controller.treeModel()->setProjects({large});
    const QModelIndex largeMasterIndex = controller.treeModel()->indexForNodeId(largeMaster);
    QCOMPARE(controller.treeModel()->rowCount(largeMasterIndex), 129);
    for (const Data::OfflineSlaveConfiguration &slaveConfiguration : std::as_const(large.slaves)) {
        const QModelIndex configured = controller.treeModel()->indexForNodeId(slaveConfiguration.id);
        QVERIFY(configured.isValid());
        QCOMPARE(controller.treeModel()->rowCount(configured), 5);
    }
    QList<Data::NodeId> nodeIds;
    const auto collectIds
        = [&nodeIds, &controller](const auto &self, const QModelIndex &parent) -> void {
        for (int row = 0; row < controller.treeModel()->rowCount(parent); ++row) {
            const QModelIndex index = controller.treeModel()->index(row, 0, parent);
            nodeIds.append(index.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>());
            self(self, index);
        }
    };
    collectIds(collectIds, {});
    QSet<Data::NodeId> uniqueNodeIds;
    for (const Data::NodeId &nodeId : std::as_const(nodeIds))
        uniqueNodeIds.insert(nodeId);
    QCOMPARE(uniqueNodeIds.size(), nodeIds.size());
}

void EtherCATWorkbenchTests::testProcessDataTableAccessibility()
{
    ProcessTreeFixture fixture = processTreeFixture();
    const QString longName = QString::fromUtf8(
                                 "\u8d85\u957f Process Data \u540d\u79f0 \u03a9 \u2014 "
                                 "\u5b8c\u6574\u5185\u5bb9 \u2014 ")
                             + QString(256, QLatin1Char('W'));
    const QString longEntryName = QString::fromUtf8(
                                      "\u8d85\u957f PDO Entry \u540d\u79f0 \u03a9 \u2014 ")
                                  + QString(256, QLatin1Char('M'));
    fixture.project.slaves.first().processData.pdos.first().name = longName;
    fixture.project.slaves.first().processData.pdos.first().entries.first().name = longEntryName;
    fixture.project.slaves.first().processData.pdos[2].mappingSupported = false;

    WorkbenchController controller;
    controller.treeModel()->setProjects({fixture.project});
    const QModelIndex slave = controller.treeModel()->indexForNodeId(fixture.slaveId);
    QVERIFY(slave.isValid());

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> processPage(pages.createPage(Constants::PROCESS_DATA_PAGE_ID, nullptr));
    QVERIFY(processPage);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        processPage.get(),
        controller.treeModel()->contextForIndex(slave));

    const QList<QTableView *> tables = {
        processPage->findChild<QTableView *>("EtherCATProcessDataSyncManagers"),
        processPage->findChild<QTableView *>("EtherCATProcessDataAssignments"),
        processPage->findChild<QTableView *>("EtherCATProcessDataPdoList"),
        processPage->findChild<QTableView *>("EtherCATProcessDataPdoContent"),
        processPage->findChild<QTableView *>("EtherCATProcessDataImage"),
    };
    QSet<QString> accessibleNames;
    for (QTableView *table : tables) {
        QVERIFY(table);
        QVERIFY2(!table->accessibleName().isEmpty(), qPrintable(table->objectName()));
        QVERIFY2(!table->accessibleDescription().isEmpty(), qPrintable(table->objectName()));
        accessibleNames.insert(table->accessibleName());

        const QAbstractItemModel *model = table->model();
        QVERIFY(model);
        QVERIFY(model->rowCount() > 0);
        for (int row = 0; row < model->rowCount(); ++row) {
            for (int column = 0; column < model->columnCount(); ++column) {
                const QModelIndex index = model->index(row, column);
                const QString header
                    = model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
                const QString displayed = index.data(Qt::DisplayRole).toString();
                const QVariant accessibleTextData = index.data(Qt::AccessibleTextRole);
                const QString accessibleText = accessibleTextData.toString();
                const QString accessibleDescription
                    = index.data(Qt::AccessibleDescriptionRole).toString();
                const QString toolTip = index.data(Qt::ToolTipRole).toString();
                QVERIFY2(!header.isEmpty(), qPrintable(table->objectName()));
                QVERIFY2(accessibleTextData.isValid(), qPrintable(table->objectName()));
                if (!displayed.isEmpty())
                    QCOMPARE(accessibleText, displayed);
                QVERIFY2(!accessibleDescription.isEmpty(), qPrintable(table->objectName()));
                QVERIFY(accessibleDescription.contains(header));
                if (!accessibleText.isEmpty())
                    QVERIFY(accessibleDescription.contains(accessibleText));
                QVERIFY2(!toolTip.isEmpty(), qPrintable(table->objectName()));
                QVERIFY(toolTip.contains(header));
                if (!accessibleText.isEmpty())
                    QVERIFY(toolTip.contains(accessibleText));
            }
        }
    }
    QCOMPARE(accessibleNames.size(), tables.size());

    QTableView *assignments = tables.at(1);
    const int assignedColumn = columnWithHeader(assignments->model(), "Assigned");
    QVERIFY(assignedColumn >= 0);
    const QModelIndex assigned = assignments->model()->index(0, assignedColumn);
    QCOMPARE(assigned.data(Qt::AccessibleTextRole).toString(), QString("Assigned"));
    QVERIFY(assigned.data(Qt::AccessibleDescriptionRole).toString().contains(longName));
    QVERIFY(assigned.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!assigned.data(Qt::AccessibleDescriptionRole)
                 .toString()
                 .contains("Select whether", Qt::CaseInsensitive));
    const QModelIndex notAssigned = assignments->model()->index(1, assignedColumn);
    QCOMPARE(notAssigned.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QCOMPARE(notAssigned.data(Qt::AccessibleTextRole).toString(), QString("Not assigned"));
    QVERIFY(notAssigned.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("cannot be selected", Qt::CaseInsensitive));

    QTableView *syncManagers = tables.at(0);
    syncManagers->setCurrentIndex(syncManagers->model()->index(1, 0));
    QCOMPARE(assignments->model()->rowCount(), 1);
    const QModelIndex mandatory = assignments->model()->index(0, assignedColumn);
    QVERIFY(mandatory.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("Mandatory PDOs", Qt::CaseInsensitive));
    syncManagers->setCurrentIndex(syncManagers->model()->index(0, 0));
    QCOMPARE(assignments->model()->rowCount(), 2);

    QTableView *pdoList = tables.at(2);
    const int nameColumn = columnWithHeader(pdoList->model(), "Name");
    QVERIFY(nameColumn >= 0);
    const QModelIndex longNameIndex = pdoList->model()->index(0, nameColumn);
    QCOMPARE(longNameIndex.data(Qt::AccessibleTextRole).toString(), longName);
    QVERIFY(longNameIndex.data(Qt::AccessibleDescriptionRole).toString().contains(longName));
    QVERIFY(longNameIndex.data(Qt::ToolTipRole).toString().contains(longName));

    QTableView *pdoContent = tables.at(3);
    const int entryNameColumn = columnWithHeader(pdoContent->model(), "Name");
    QVERIFY(entryNameColumn >= 0);
    const QModelIndex longEntryNameIndex = pdoContent->model()->index(0, entryNameColumn);
    QCOMPARE(longEntryNameIndex.data(Qt::AccessibleTextRole).toString(), longEntryName);
    QVERIFY(
        longEntryNameIndex.data(Qt::AccessibleDescriptionRole).toString().contains(longEntryName));
    QVERIFY(longEntryNameIndex.data(Qt::ToolTipRole).toString().contains(longEntryName));

    QTableView *processImage = tables.at(4);
    const int offsetColumn = columnWithHeader(processImage->model(), "Offset (byte.bit)");
    QVERIFY(offsetColumn >= 0);
    QVERIFY(processImage->model()
                ->index(0, offsetColumn)
                .data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("bit range", Qt::CaseInsensitive));
}

void EtherCATWorkbenchTests::testProcessDataRepositoryEmptyState()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto uniqueDevice = [](QByteArray esi,
                                 const QByteArray &productCode,
                                 const QByteArray &revision,
                                 const QByteArray &typeName,
                                 const QByteArray &name) {
        esi.replace("#x00005678", productCode);
        esi.replace("#x00000011", revision);
        esi.replace("AX5000", typeName);
        esi.replace("Workbench Servo", name);
        return esi;
    };

    QByteArray supportedEmptyEsi = deviceEsi();
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<Sm ", "</Sm>"));
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<Sm ", "</Sm>"));
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<RxPdo ", "</RxPdo>"));
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<TxPdo ", "</TxPdo>"));
    supportedEmptyEsi = uniqueDevice(
        supportedEmptyEsi,
        "#x7A180001",
        "#x0000B001",
        "EL-PD-EMPTY",
        "Process Data Empty Servo / 无 PDO 映射");

    QByteArray unsupportedEmptyEsi = supportedEmptyEsi;
    unsupportedEmptyEsi.replace("#x7A180001", "#x7A180002");
    unsupportedEmptyEsi.replace("#x0000B001", "#x0000B002");
    unsupportedEmptyEsi.replace("EL-PD-EMPTY", "EL-PD-EMPTY-UNSUPPORTED");
    unsupportedEmptyEsi.replace("无 PDO 映射", "无 PDO 映射 / 不支持");
    unsupportedEmptyEsi.replace("</Device>", "<Modules/></Device>");

    QByteArray supportedPopulatedEsi = uniqueDevice(
        deviceEsi(),
        "#x7A180003",
        "#x0000B003",
        "EL-PD-POPULATED",
        "Process Data Populated Servo / 已解析 PDO");

    QByteArray unsupportedPopulatedEsi = supportedPopulatedEsi;
    unsupportedPopulatedEsi.replace("#x7A180003", "#x7A180004");
    unsupportedPopulatedEsi.replace("#x0000B003", "#x0000B004");
    unsupportedPopulatedEsi.replace("EL-PD-POPULATED", "EL-PD-POPULATED-UNSUPPORTED");
    unsupportedPopulatedEsi.replace("已解析 PDO", "已解析 PDO / 不支持");
    unsupportedPopulatedEsi.replace("</Device>", "<Modules/></Device>");

    QByteArray supportedInvalidEsi = supportedPopulatedEsi;
    supportedInvalidEsi.replace("#x7A180003", "#x7A180005");
    supportedInvalidEsi.replace("#x0000B003", "#x0000B005");
    supportedInvalidEsi.replace("EL-PD-POPULATED", "EL-PD-INVALID");
    supportedInvalidEsi.replace("已解析 PDO", "PDO 校验错误");
    QVERIFY(supportedInvalidEsi.contains("<BitLen>16</BitLen>"));
    supportedInvalidEsi.replace("<BitLen>16</BitLen>", "<BitLen>8</BitLen>");

    QByteArray unsupportedInvalidEsi = unsupportedPopulatedEsi;
    unsupportedInvalidEsi.replace("#x7A180004", "#x7A180006");
    unsupportedInvalidEsi.replace("#x0000B004", "#x0000B006");
    unsupportedInvalidEsi.replace(
        "EL-PD-POPULATED-UNSUPPORTED", "EL-PD-INVALID-UNSUPPORTED");
    unsupportedInvalidEsi.replace("已解析 PDO / 不支持", "PDO 校验错误 / 不支持");
    QVERIFY(unsupportedInvalidEsi.contains("<BitLen>16</BitLen>"));
    unsupportedInvalidEsi.replace("<BitLen>16</BitLen>", "<BitLen>8</BitLen>");

    struct EsiFixture
    {
        QString fileName;
        QByteArray xml;
    };
    const QList<EsiFixture> fixtures = {
        {"process-data-empty.xml", supportedEmptyEsi},
        {"process-data-empty-unsupported.xml", unsupportedEmptyEsi},
        {"process-data-populated.xml", supportedPopulatedEsi},
        {"process-data-populated-unsupported.xml", unsupportedPopulatedEsi},
        {"process-data-invalid.xml", supportedInvalidEsi},
        {"process-data-invalid-unsupported.xml", unsupportedInvalidEsi},
    };
    QList<Utils::FilePath> esiPaths;
    for (const EsiFixture &fixture : fixtures) {
        const Utils::FilePath path
            = Utils::FilePath::fromString(directory.path()).pathAppended(fixture.fileName);
        const Utils::Result<qint64> writeResult = path.writeFileContents(fixture.xml);
        QVERIFY_RESULT(writeResult);
        esiPaths.append(path);
    }
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles(esiPaths));
    QCOMPARE(importResult.requestedFiles, fixtures.size());
    QCOMPARE(importResult.importedDevices, fixtures.size());
    QCOMPARE(importResult.failedFiles, 0);
    QCOMPARE(importResult.affectedDeviceIds.size(), fixtures.size());

    const auto idForType = [repository](const QString &typeName) {
        const QList<Data::DeviceSummary> devices = repository->devices();
        const auto found = std::find_if(
            devices.cbegin(), devices.cend(), [&typeName](const Data::DeviceSummary &device) {
                return device.typeName == typeName;
            });
        return found == devices.cend() ? Data::NodeId() : found->id;
    };
    const Data::NodeId supportedEmptyId = idForType("EL-PD-EMPTY");
    const Data::NodeId unsupportedEmptyId = idForType("EL-PD-EMPTY-UNSUPPORTED");
    const Data::NodeId supportedPopulatedId = idForType("EL-PD-POPULATED");
    const Data::NodeId unsupportedPopulatedId = idForType("EL-PD-POPULATED-UNSUPPORTED");
    const Data::NodeId supportedInvalidId = idForType("EL-PD-INVALID");
    const Data::NodeId unsupportedInvalidId = idForType("EL-PD-INVALID-UNSUPPORTED");
    QVERIFY(!supportedEmptyId.isNull());
    QVERIFY(!unsupportedEmptyId.isNull());
    QVERIFY(!supportedPopulatedId.isNull());
    QVERIFY(!unsupportedPopulatedId.isNull());
    QVERIFY(!supportedInvalidId.isNull());
    QVERIFY(!unsupportedInvalidId.isNull());

    const std::optional<Data::DeviceDescription> supportedEmpty
        = repository->device(supportedEmptyId);
    const std::optional<Data::DeviceDescription> unsupportedEmpty
        = repository->device(unsupportedEmptyId);
    const std::optional<Data::DeviceDescription> supportedPopulated
        = repository->device(supportedPopulatedId);
    const std::optional<Data::DeviceDescription> unsupportedPopulated
        = repository->device(unsupportedPopulatedId);
    const std::optional<Data::DeviceDescription> supportedInvalid
        = repository->device(supportedInvalidId);
    const std::optional<Data::DeviceDescription> unsupportedInvalid
        = repository->device(unsupportedInvalidId);
    QVERIFY(supportedEmpty);
    QVERIFY(unsupportedEmpty);
    QVERIFY(supportedPopulated);
    QVERIFY(unsupportedPopulated);
    QVERIFY(supportedInvalid);
    QVERIFY(unsupportedInvalid);
    QVERIFY(supportedEmpty->summary.supported);
    QVERIFY(supportedEmpty->syncManagers.isEmpty());
    QVERIFY(supportedEmpty->rxPdos.isEmpty());
    QVERIFY(supportedEmpty->txPdos.isEmpty());
    QVERIFY(!unsupportedEmpty->summary.supported);
    QVERIFY(unsupportedEmpty->syncManagers.isEmpty());
    QVERIFY(unsupportedEmpty->rxPdos.isEmpty());
    QVERIFY(unsupportedEmpty->txPdos.isEmpty());
    QVERIFY(supportedPopulated->summary.supported);
    QVERIFY(!supportedPopulated->rxPdos.isEmpty());
    QVERIFY(!supportedPopulated->txPdos.isEmpty());
    QVERIFY(!unsupportedPopulated->summary.supported);
    QVERIFY(supportedInvalid->summary.supported);
    QVERIFY(!unsupportedInvalid->summary.supported);

    for (const Data::NodeId &deviceId : {supportedEmptyId,
                                         unsupportedEmptyId,
                                         supportedPopulatedId,
                                         unsupportedPopulatedId,
                                         supportedInvalidId,
                                         unsupportedInvalidId}) {
        QTRY_VERIFY(controller.treeModel()->indexForNodeId(deviceId).isValid());
    }

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::PROCESS_DATA_PAGE_ID, nullptr));
    QVERIFY(page);
    QLabel *summary = page->findChild<QLabel *>("EtherCATProcessDataSummary");
    Utils::InfoLabel *validation = dynamic_cast<Utils::InfoLabel *>(
        page->findChild<QLabel *>("EtherCATProcessDataValidation"));
    QPushButton *restoreDefaults = page->findChild<QPushButton *>(
        "EtherCATProcessDataRestoreDefaults");
    const QList<QTableView *> tables = {
        page->findChild<QTableView *>("EtherCATProcessDataSyncManagers"),
        page->findChild<QTableView *>("EtherCATProcessDataAssignments"),
        page->findChild<QTableView *>("EtherCATProcessDataPdoList"),
        page->findChild<QTableView *>("EtherCATProcessDataPdoContent"),
        page->findChild<QTableView *>("EtherCATProcessDataImage"),
    };
    QVERIFY(summary);
    QVERIFY(validation);
    QVERIFY(restoreDefaults);
    for (QTableView *table : tables)
        QVERIFY(table);
    const auto repositoryTableMutationFailure = [&tables]() -> QString {
        bool inspectedCell = false;
        for (QTableView *table : tables) {
            QAbstractItemModel *model = table->model();
            for (int row = 0; row < model->rowCount(); ++row) {
                for (int column = 0; column < model->columnCount(); ++column) {
                    const QModelIndex index = model->index(row, column);
                    if (!index.isValid())
                        continue;
                    inspectedCell = true;
                    const Qt::ItemFlags flags = model->flags(index);
                    if (flags & (Qt::ItemIsEditable | Qt::ItemIsUserCheckable)) {
                        return QString("%1 exposes mutable flags at %2,%3")
                            .arg(table->objectName())
                            .arg(row)
                            .arg(column);
                    }
                    const QVariant display = model->data(index, Qt::DisplayRole);
                    const QVariant checkState = model->data(index, Qt::CheckStateRole);
                    if (model->setData(index, QString("Repository mutation"), Qt::EditRole)) {
                        return QString("%1 accepted EditRole at %2,%3")
                            .arg(table->objectName())
                            .arg(row)
                            .arg(column);
                    }
                    if (model->data(index, Qt::DisplayRole) != display) {
                        return QString("%1 changed display data at %2,%3")
                            .arg(table->objectName())
                            .arg(row)
                            .arg(column);
                    }
                    if (model->setData(index, Qt::Checked, Qt::CheckStateRole)) {
                        return QString("%1 accepted CheckStateRole at %2,%3")
                            .arg(table->objectName())
                            .arg(row)
                            .arg(column);
                    }
                    if (model->data(index, Qt::CheckStateRole) != checkState) {
                        return QString("%1 changed check state at %2,%3")
                            .arg(table->objectName())
                            .arg(row)
                            .arg(column);
                    }
                }
            }
        }
        return inspectedCell ? QString() : QString("No repository table cell was inspected");
    };

    const QModelIndex supportedEmptyIndex
        = controller.treeModel()->indexForNodeId(supportedEmptyId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(supportedEmptyIndex));
    for (QTableView *table : tables)
        QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(summary->text().contains("No ESI Process Data mapping", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Select a Sync Manager", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("will not fabricate", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("can still be added", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("Configuration is valid", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("No ESI Process Data mapping", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Information);
    QVERIFY(restoreDefaults->isHidden());
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains(
            "No ESI Process Data mapping", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("will not fabricate", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("controller", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("network", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("physical hardware", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }

    const QModelIndex unsupportedEmptyIndex
        = controller.treeModel()->indexForNodeId(unsupportedEmptyId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(unsupportedEmptyIndex));
    for (QTableView *table : tables)
        QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(summary->text().contains("No ESI Process Data mapping", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("Configuration is valid", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("Device Repository", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }

    const QModelIndex unsupportedPopulatedIndex
        = controller.treeModel()->indexForNodeId(unsupportedPopulatedId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(unsupportedPopulatedIndex));
    QCOMPARE(tables.first()->model()->rowCount(), 2);
    QVERIFY(tables.at(2)->model()->rowCount() > 0);
    QVERIFY(summary->text().contains("read-only preview", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("read-only preview", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("Configuration is valid", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains("read-only preview", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }
    QString mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));

    const QModelIndex supportedInvalidIndex
        = controller.treeModel()->indexForNodeId(supportedInvalidId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(supportedInvalidIndex));
    QVERIFY(summary->text().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("configuration error", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("bit length", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Error);
    QCOMPARE(validation->toolTip().split('\n').size(), 2);
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains("validation error", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains(
            "Device Repository", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }
    mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));

    const QModelIndex unsupportedInvalidIndex
        = controller.treeModel()->indexForNodeId(unsupportedInvalidId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(unsupportedInvalidIndex));
    QVERIFY(summary->text().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("configuration error", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("bit length", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Error);
    QCOMPARE(validation->toolTip().split('\n').size(), 2);
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains("validation error", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains(
            "Device Repository", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }
    mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));

    const Core::PropertyPageContext missingContext{
        {}, Data::NodeId::create(), Core::WorkbenchNodeKind::Device, "Removed ESI Device"};
    pages.updatePage(Constants::PROCESS_DATA_PAGE_ID, page.get(), missingContext);
    for (QTableView *table : tables)
        QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(summary->text().contains("no longer available", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("unavailable", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("Configuration is valid", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
        QVERIFY(table->accessibleDescription().contains("Device Repository", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }

    const QModelIndex supportedPopulatedIndex
        = controller.treeModel()->indexForNodeId(supportedPopulatedId);
    pages.updatePage(
        Constants::PROCESS_DATA_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(supportedPopulatedIndex));
    QCOMPARE(tables.first()->model()->rowCount(), 2);
    QVERIFY(tables.at(2)->model()->rowCount() > 0);
    QVERIFY(summary->text().contains("Select a Sync Manager", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("No ESI Process Data mapping", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("will not fabricate", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("Configuration is valid", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Ok);
    QVERIFY(validation->toolTip().isEmpty());
    for (QTableView *table : tables) {
        QVERIFY(table->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
        QVERIFY(!table->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
        QVERIFY(!table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
        QVERIFY(!table->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
        QVERIFY(!table->accessibleDescription().contains("validation error", Qt::CaseInsensitive));
        QVERIFY(!table->accessibleDescription().contains(
            "No ESI Process Data mapping", Qt::CaseInsensitive));
        QVERIFY(!table->accessibleDescription().contains(
            "will not fabricate", Qt::CaseInsensitive));
        QCOMPARE(table->toolTip(), table->accessibleDescription());
    }
    mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));
    QVERIFY(projectService->projects().isEmpty());

    const TestProjectFile recoveryProject = writeProjectWithSlave(
        directory,
        supportedEmpty->summary,
        "process-data-repository-recovery.ecatproject",
        "Process Data Repository Recovery");
    QVERIFY(!recoveryProject.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(recoveryProject.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    QTRY_VERIFY(projectService->project(recoveryProject.projectId).has_value());
    const Utils::Result<> activationResult
        = projectService->activateProject(recoveryProject.projectId);
    QVERIFY_RESULT(activationResult);
    const Data::ProjectSnapshot beforeRecovery
        = *projectService->project(recoveryProject.projectId);
    const Data::NodeId activeProjectBeforeBrowsing = projectService->activeProjectId();
    const bool couldUndoBeforeBrowsing
        = projectService->canUndoProject(recoveryProject.projectId);
    const bool couldRedoBeforeBrowsing
        = projectService->canRedoProject(recoveryProject.projectId);
    for (const Data::NodeId &repositoryDeviceId : {supportedEmptyId,
                                                   unsupportedEmptyId,
                                                   supportedInvalidId,
                                                   supportedPopulatedId}) {
        const QModelIndex repositoryIndex
            = controller.treeModel()->indexForNodeId(repositoryDeviceId);
        QVERIFY(repositoryIndex.isValid());
        pages.updatePage(
            Constants::PROCESS_DATA_PAGE_ID,
            page.get(),
            controller.treeModel()->contextForIndex(repositoryIndex));
    }
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRecovery);
    QCOMPARE(projectService->activeProjectId(), activeProjectBeforeBrowsing);
    QCOMPARE(
        projectService->canUndoProject(recoveryProject.projectId), couldUndoBeforeBrowsing);
    QCOMPARE(
        projectService->canRedoProject(recoveryProject.projectId), couldRedoBeforeBrowsing);

    const Utils::Result<> supportedEmptyAddResult
        = controller.addDeviceToMaster(supportedEmptyId, recoveryProject.masterId);
    QVERIFY_RESULT(supportedEmptyAddResult);
    QTRY_COMPARE(
        projectService->project(recoveryProject.projectId)->slaves.size(),
        beforeRecovery.slaves.size() + 1);
    const Data::ProjectSnapshot afterSupportedEmptyAdd
        = *projectService->project(recoveryProject.projectId);
    const auto added = std::find_if(
        afterSupportedEmptyAdd.slaves.cbegin(),
        afterSupportedEmptyAdd.slaves.cend(),
        [&beforeRecovery](const Data::OfflineSlaveConfiguration &slave) {
            return std::none_of(
                beforeRecovery.slaves.cbegin(),
                beforeRecovery.slaves.cend(),
                [&slave](const Data::OfflineSlaveConfiguration &existing) {
                    return existing.id == slave.id;
                });
        });
    QVERIFY(added != afterSupportedEmptyAdd.slaves.cend());
    QVERIFY(added->processData.syncManagers.isEmpty());
    QVERIFY(added->processData.pdos.isEmpty());
    QVERIFY(projectService->canUndoProject(recoveryProject.projectId));
    const Utils::Result<> undoResult
        = projectService->undoProject(recoveryProject.projectId);
    QVERIFY_RESULT(undoResult);
    QTRY_COMPARE(
        projectService->project(recoveryProject.projectId)->slaves,
        beforeRecovery.slaves);
    const Data::ProjectSnapshot beforeRejectedAdds
        = *projectService->project(recoveryProject.projectId);

    const Utils::Result<> unsupportedEmptyAdd
        = controller.addDeviceToMaster(unsupportedEmptyId, recoveryProject.masterId);
    QVERIFY(!unsupportedEmptyAdd);
    QVERIFY(unsupportedEmptyAdd.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    const Utils::Result<> unsupportedPopulatedAdd
        = controller.addDeviceToMaster(unsupportedPopulatedId, recoveryProject.masterId);
    QVERIFY(!unsupportedPopulatedAdd);
    QVERIFY(unsupportedPopulatedAdd.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    const Utils::Result<> supportedInvalidAdd
        = controller.addDeviceToMaster(supportedInvalidId, recoveryProject.masterId);
    QVERIFY(!supportedInvalidAdd);
    QVERIFY(supportedInvalidAdd.error().contains("Process Data", Qt::CaseInsensitive));
    QVERIFY(supportedInvalidAdd.error().contains("invalid", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    const Utils::Result<> unsupportedInvalidAdd
        = controller.addDeviceToMaster(unsupportedInvalidId, recoveryProject.masterId);
    QVERIFY(!unsupportedInvalidAdd);
    QVERIFY(unsupportedInvalidAdd.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(recoveryProject.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
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
    const QModelIndex configuredSlave = controller.treeModel()->indexForNodeId(file.slaveId);
    const QModelIndex rxPdoBranch = directChildByKind(
        controller.treeModel(), Core::WorkbenchNodeKind::RxPdoGroup, configuredSlave);
    QVERIFY(rxPdoBranch.isValid());
    const QModelIndex configuredRxPdo = controller.treeModel()->index(0, 0, rxPdoBranch);
    QCOMPARE(
        configuredRxPdo.data(WorkbenchTreeModel::NodeKindRole).value<Core::WorkbenchNodeKind>(),
        Core::WorkbenchNodeKind::Pdo);
    const Data::NodeId configuredRxPdoId
        = configuredRxPdo.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    controller.selectionService()->setCurrentNodeId(configuredRxPdoId);
    QTRY_COMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::Pdo);
    QWidget *derivedPage = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::PROCESS_DATA_PAGE_ID).toString());
    QVERIFY(derivedPage);
    QTableView *derivedAssignments = derivedPage->findChild<QTableView *>(
        "EtherCATProcessDataAssignments");
    QTableView *derivedPdoList = derivedPage->findChild<QTableView *>(
        "EtherCATProcessDataPdoList");
    QLabel *derivedSummary = derivedPage->findChild<QLabel *>("EtherCATProcessDataSummary");
    QVERIFY(derivedAssignments);
    QVERIFY(derivedPdoList);
    QVERIFY(derivedSummary);
    QVERIFY(derivedSummary->text().contains("Read-only", Qt::CaseInsensitive));
    QCOMPARE(derivedPdoList->currentIndex().siblingAtColumn(2).data().toString(),
             QString("Command"));
    QVERIFY(!(
        derivedAssignments->model()->flags(derivedAssignments->model()->index(0, 0))
        & Qt::ItemIsUserCheckable));

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testCoeOnlineMockWorkflow()
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
                                        .pathAppended("coe-online.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(deviceEsi()));
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
    BuiltinPropertyPageProvider provider(&controller);
    const Utils::Id coePageId(Constants::COE_ONLINE_PAGE_ID);
    const QList<Core::PropertyPageDescriptor> descriptors = provider.pages(context);
    const auto coeDescriptor
        = std::find_if(descriptors.cbegin(), descriptors.cend(), [&coePageId](const auto &entry) {
              return entry.id == coePageId;
          });
    QVERIFY(coeDescriptor != descriptors.cend());
    QCOMPARE(coeDescriptor->displayName, QString("CoE Online"));
    QCOMPARE(coeDescriptor->priority, 350);

    std::unique_ptr<QWidget> page(provider.createPage(coePageId, nullptr));
    QVERIFY(page);
    provider.updatePage(coePageId, page.get(), context);
    page->resize(1100, 760);
    page->show();
    QTRY_VERIFY(page->isVisible());
    QLabel *banner = page->findChild<QLabel *>("EtherCATCoeMockBanner");
    QLabel *source = page->findChild<QLabel *>("EtherCATCoeDataSource");
    QLineEdit *filter = page->findChild<QLineEdit *>("EtherCATCoeFilter");
    QTreeView *dictionary = page->findChild<QTreeView *>("EtherCATCoeObjectDictionary");
    QWidget *filterEmptyState
        = page->findChild<QWidget *>("EtherCATCoeFilterEmptyState");
    QLabel *filterEmptyMessage
        = page->findChild<QLabel *>("EtherCATCoeFilterEmptyMessage");
    QPushButton *clearFilters = page->findChild<QPushButton *>("EtherCATCoeClearFilters");
    QPushButton *updateList = page->findChild<QPushButton *>("EtherCATCoeUpdateList");
    QPushButton *advanced = page->findChild<QPushButton *>("EtherCATCoeAdvanced");
    QPushButton *addToStartup = page->findChild<QPushButton *>("EtherCATCoeAddToStartup");
    QCheckBox *autoUpdate = page->findChild<QCheckBox *>("EtherCATCoeAutoUpdate");
    QCheckBox *singleUpdate = page->findChild<QCheckBox *>("EtherCATCoeSingleUpdate");
    QCheckBox *showOffline = page->findChild<QCheckBox *>("EtherCATCoeShowOffline");
    QLineEdit *moduleOd = page->findChild<QLineEdit *>("EtherCATCoeModuleOd");
    QVERIFY(banner);
    QVERIFY(source);
    QVERIFY(filter);
    QVERIFY(dictionary);
    QVERIFY(filterEmptyState);
    QVERIFY(filterEmptyMessage);
    QVERIFY(clearFilters);
    QAbstractItemModelTester dictionaryTester(
        dictionary->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(updateList);
    QVERIFY(advanced);
    QVERIFY(addToStartup);
    QVERIFY(autoUpdate);
    QVERIFY(singleUpdate);
    QVERIFY(showOffline);
    QVERIFY(moduleOd);
    QVERIFY(!filter->accessibleName().isEmpty());
    QVERIFY(!filter->accessibleDescription().isEmpty());
    QVERIFY(!filterEmptyState->accessibleName().isEmpty());
    QVERIFY(!filterEmptyState->accessibleDescription().isEmpty());
    QVERIFY(!filterEmptyMessage->accessibleName().isEmpty());
    QVERIFY(!clearFilters->accessibleDescription().isEmpty());
    QVERIFY(banner->text().contains("MOCK", Qt::CaseInsensitive));
    QVERIFY(banner->text().contains("controller", Qt::CaseInsensitive));
    QVERIFY(source->text().contains("Mock", Qt::CaseInsensitive));
    QVERIFY(!autoUpdate->isEnabled());
    QVERIFY(singleUpdate->isChecked());
    QVERIFY(!showOffline->isChecked());
    QCOMPARE(moduleOd->text(), QString("0"));
    QVERIFY(moduleOd->isReadOnly());
    QCOMPARE(columnWithHeader(dictionary->model(), "Index"), 0);
    QCOMPARE(columnWithHeader(dictionary->model(), "Name"), 1);
    const int flagsColumn = columnWithHeader(dictionary->model(), "Flags");
    const int valueColumn = columnWithHeader(dictionary->model(), "Value");
    QCOMPARE(columnWithHeader(dictionary->model(), "Unit"), 4);
    QVERIFY(flagsColumn >= 0);
    QVERIFY(valueColumn >= 0);
    const QModelIndex vendorId = findByDisplayText(dictionary->model(), "1018:01");
    QVERIFY(vendorId.isValid());
    QCOMPARE(vendorId.siblingAtColumn(valueColumn).data().toString(), QString("0x00000002 (2)"));

    bool advancedAccepted = false;
    QTimer::singleShot(0, [&advancedAccepted] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATCoeAdvancedDialog")
            return;
        QComboBox *range = dialog->findChild<QComboBox *>("EtherCATCoeAdvancedRange");
        if (!range)
            return;
        const int profileRange = range->findText("Profile-specific", Qt::MatchStartsWith);
        if (profileRange < 0)
            return;
        range->setCurrentIndex(profileRange);
        advancedAccepted = true;
        dialog->accept();
    });
    QTest::mouseClick(advanced, Qt::LeftButton);
    QTRY_VERIFY(advancedAccepted);
    QVERIFY(!findByDisplayText(dictionary->model(), "1018:00").isValid());

    QModelIndex mockObject = findByDisplayText(dictionary->model(), "6060:00");
    QVERIFY(mockObject.isValid());
    QVERIFY(mockObject.siblingAtColumn(flagsColumn).data().toString().contains("RW"));
    const QString initialValue = mockObject.siblingAtColumn(valueColumn).data().toString();
    QVERIFY(!initialValue.isEmpty());
    dictionary->setCurrentIndex(mockObject);
    QTRY_VERIFY(addToStartup->isEnabled());
    QVERIFY(!projectService->project(file.projectId)->modified);

    bool confirmationRejected = false;
    QTimer::singleShot(0, [&confirmationRejected] {
        if (auto messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (QAbstractButton *no = messageBox->button(QMessageBox::No)) {
                confirmationRejected = true;
                no->click();
            }
        }
    });
    QTest::mouseClick(addToStartup, Qt::LeftButton);
    QTRY_VERIFY(confirmationRejected);
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 0);
    QVERIFY(!projectService->project(file.projectId)->modified);

    QSignalSpy reset(dictionary->model(), &QAbstractItemModel::modelReset);
    QTest::mouseClick(updateList, Qt::LeftButton);
    QTRY_VERIFY(reset.count() >= 1);
    mockObject = findByDisplayText(dictionary->model(), "6060:00");
    QVERIFY(mockObject.isValid());
    QVERIFY(mockObject.siblingAtColumn(valueColumn).data().toString() != initialValue);
    QVERIFY(!dictionary->model()
                 ->setData(mockObject.siblingAtColumn(valueColumn), "not hex", Qt::EditRole));
    QVERIFY(
        dictionary->model()->setData(mockObject.siblingAtColumn(valueColumn), "5A", Qt::EditRole));
    QCOMPARE(mockObject.siblingAtColumn(valueColumn).data(Qt::EditRole).toString(), QString("5A"));

    dictionary->setCurrentIndex(mockObject);
    const QString editedValue = mockObject.siblingAtColumn(valueColumn).data().toString();
    const QString editedAddress = mockObject.data().toString();
    QVERIFY(!editedValue.isEmpty());
    filter->setText(editedValue);
    QTRY_COMPARE(dictionary->model()->rowCount(), 1);
    QModelIndex filteredMock = findByDisplayText(dictionary->model(), editedAddress);
    QVERIFY(filteredMock.isValid());
    QVERIFY(dictionary->model()->setData(
        filteredMock.siblingAtColumn(valueColumn), "0A", Qt::EditRole));
    QTRY_COMPARE(dictionary->model()->rowCount(), 0);
    QTRY_VERIFY(filterEmptyState->isVisible());
    QVERIFY(!dictionary->isVisible());
    filter->clear();
    QTRY_VERIFY(dictionary->isVisible());
    QTRY_COMPARE(dictionary->currentIndex().data().toString(), editedAddress);
    mockObject = findByDisplayText(dictionary->model(), editedAddress);
    QVERIFY(mockObject.isValid());
    QCOMPARE(mockObject.siblingAtColumn(valueColumn).data(Qt::EditRole).toString(), QString("0A"));

    filter->setText("最大扭矩");
    QTRY_VERIFY(findByDisplayText(dictionary->model(), "6072:00").isValid());
    QVERIFY(!findByDisplayText(dictionary->model(), "6060:00").isValid());
    filter->clear();
    QTRY_VERIFY(findByDisplayText(dictionary->model(), "6060:00").isValid());
    mockObject = findByDisplayText(dictionary->model(), "6060:00");
    dictionary->setCurrentIndex(mockObject);
    QTRY_VERIFY(addToStartup->isEnabled());

    const QString selectedAddress = mockObject.data().toString();
    filter->setText("6060");
    QTRY_VERIFY(findByDisplayText(dictionary->model(), selectedAddress).isValid());
    bool combinedAdvancedAccepted = false;
    QTimer::singleShot(0, [&combinedAdvancedAccepted] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATCoeAdvancedDialog")
            return;
        QComboBox *range = dialog->findChild<QComboBox *>("EtherCATCoeAdvancedRange");
        QCheckBox *hideStandard
            = dialog->findChild<QCheckBox *>("EtherCATCoeAdvancedHideStandard");
        QCheckBox *hidePdo = dialog->findChild<QCheckBox *>("EtherCATCoeAdvancedHidePdo");
        if (!range || !hideStandard || !hidePdo)
            return;
        const int communicationRange = range->findText("Communication", Qt::MatchStartsWith);
        if (communicationRange < 0)
            return;
        range->setCurrentIndex(communicationRange);
        hideStandard->setChecked(true);
        hidePdo->setChecked(true);
        combinedAdvancedAccepted = true;
        dialog->accept();
    });
    QTest::mouseClick(advanced, Qt::LeftButton);
    QTRY_VERIFY(combinedAdvancedAccepted);
    QTRY_COMPARE(dictionary->model()->rowCount(), 0);
    QTRY_VERIFY(filterEmptyState->isVisible());
    QVERIFY(!dictionary->isVisible());
    QCOMPARE(
        filterEmptyMessage->text(), QString("No CoE objects match the current filters."));
    QCOMPARE(clearFilters->text(), QString("Clear Filters"));
    QVERIFY(clearFilters->focusPolicy() & Qt::TabFocus);
    QVERIFY(!addToStartup->isEnabled());
    const QString filterEmptyRenderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_COE_FILTER_EMPTY_RENDER_PATH");
    if (!filterEmptyRenderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(page->grab().save(filterEmptyRenderPath), qPrintable(filterEmptyRenderPath));
    }

    page->activateWindow();
    clearFilters->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), clearFilters);
    QTest::keyClick(clearFilters, Qt::Key_Space);
    QTRY_VERIFY(filter->text().isEmpty());
    QTRY_VERIFY(dictionary->isVisible());
    QVERIFY(!filterEmptyState->isVisible());
    QTRY_VERIFY(findByDisplayText(dictionary->model(), "1018:00").isValid());
    QTRY_COMPARE(dictionary->currentIndex().data().toString(), selectedAddress);
    QTRY_COMPARE(QApplication::focusWidget(), dictionary);
    QTRY_VERIFY(addToStartup->isEnabled());

    QTimer::singleShot(0, [] {
        if (auto messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (QAbstractButton *yes = messageBox->button(QMessageBox::Yes))
                yes->click();
        }
    });
    QTest::mouseClick(addToStartup, Qt::LeftButton);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 1);
    const Data::StartupParameterConfiguration copied
        = projectService->project(file.projectId)->slaves.first().startup.parameters.first();
    QCOMPARE(copied.index, quint16(0x6060));
    QCOMPARE(copied.subIndex, quint8(0));
    QCOMPARE(copied.rawValue, QByteArray::fromHex("0A"));
    QCOMPARE(copied.transition, QString("PS"));
    QVERIFY(projectService->project(file.projectId)->modified);
    QVERIFY_RESULT(projectService->undoProject(file.projectId));
    QCOMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 0);

    showOffline->setChecked(true);
    QTRY_VERIFY(source->text().contains("Offline", Qt::CaseInsensitive));
    mockObject = findByDisplayText(dictionary->model(), "6060:00");
    dictionary->setCurrentIndex(mockObject);
    QVERIFY(!(
        dictionary->model()->flags(mockObject.siblingAtColumn(valueColumn)) & Qt::ItemIsEditable));
    QVERIFY(!addToStartup->isEnabled());

    const Core::PropertyPageContext
        deviceContext{{}, device->id, Core::WorkbenchNodeKind::Device, device->name};
    provider.updatePage(coePageId, page.get(), deviceContext);
    QVERIFY(dictionary->model()->rowCount() > 0);
    QVERIFY(!addToStartup->isEnabled());

    const Core::PropertyPageContext missingEsiContext{
        file.projectId, Data::NodeId::create(), Core::WorkbenchNodeKind::ConfiguredSlave, "Missing"};
    provider.updatePage(coePageId, page.get(), missingEsiContext);
    QVERIFY(banner->text().contains("no CoE", Qt::CaseInsensitive));
    QVERIFY(!addToStartup->isEnabled());

    controller.selectionService()->clear();
    ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(file.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testCoeDictionaryCellAccessibility()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray longRawValue(256, char(0xA5));
    const QString longName = QString::fromUtf8(
                                 "超长 CoE 对象名称 / 長いオブジェクト名 / Ω — "
                                 "%1 / %2 / %5 / %% — 完整内容 — ")
                             + QString(256, QChar(u'界')) + " / End";
    QByteArray accessibilityEsi = deviceEsi();
    const QByteArray productCode = "#x00005678";
    const QByteArray startupObject = "<Data>08</Data><Comment>Mode</Comment>";
    QCOMPARE(accessibilityEsi.count(productCode), 1);
    QCOMPARE(accessibilityEsi.count(startupObject), 1);
    accessibilityEsi.replace(productCode, "#xA11E0001");
    QByteArray startupReplacement = "<Data>";
    startupReplacement += longRawValue.toHex().toUpper();
    startupReplacement += "</Data><Comment>";
    startupReplacement += longName.toUtf8();
    startupReplacement += "</Comment>";
    accessibilityEsi.replace(startupObject, startupReplacement);
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("coe-cell-accessibility.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(accessibilityEsi));
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0xA11E0001;
    });
    QVERIFY(device != devices.cend());

    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (ProjectExplorer::ProjectManager::projects().contains(opened.project()))
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::COE_ONLINE_PAGE_ID, nullptr));
    QVERIFY(page);
    const QModelIndex configuredSlave = controller.treeModel()->indexForNodeId(file.slaveId);
    pages.updatePage(
        Constants::COE_ONLINE_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(configuredSlave));

    QTreeView *dictionary = page->findChild<QTreeView *>("EtherCATCoeObjectDictionary");
    QCheckBox *showOffline = page->findChild<QCheckBox *>("EtherCATCoeShowOffline");
    QPushButton *addToStartup = page->findChild<QPushButton *>("EtherCATCoeAddToStartup");
    QVERIFY(dictionary);
    QVERIFY(showOffline);
    QVERIFY(addToStartup);
    QVERIFY(!dictionary->accessibleName().isEmpty());
    QVERIFY(!dictionary->accessibleDescription().isEmpty());
    QAbstractItemModel *model = dictionary->model();
    QVERIFY(model);
    QAbstractItemModelTester modelTester(
        model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    const int indexColumn = columnWithHeader(model, "Index");
    const int nameColumn = columnWithHeader(model, "Name");
    const int flagsColumn = columnWithHeader(model, "Flags");
    const int valueColumn = columnWithHeader(model, "Value");
    const int unitColumn = columnWithHeader(model, "Unit");
    QCOMPARE(indexColumn, 0);
    QCOMPARE(nameColumn, 1);
    QCOMPARE(flagsColumn, 2);
    QCOMPARE(valueColumn, 3);
    QCOMPARE(unitColumn, 4);

    int inspectedCells = 0;
    const auto verifyCells = [&](const auto &self, const QModelIndex &parent) -> QString {
        for (int row = 0; row < model->rowCount(parent); ++row) {
            const QModelIndex addressIndex = model->index(row, indexColumn, parent);
            const QString objectAddress = addressIndex.data(Qt::DisplayRole).toString();
            if (objectAddress.isEmpty())
                return "A CoE object address is empty";
            for (int column = 0; column < model->columnCount(parent); ++column) {
                const QModelIndex index = model->index(row, column, parent);
                const QString header
                    = model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
                const QString displayed = index.data(Qt::DisplayRole).toString();
                const QVariant accessibleTextData = index.data(Qt::AccessibleTextRole);
                const QVariant accessibleDescriptionData
                    = index.data(Qt::AccessibleDescriptionRole);
                const QVariant toolTipData = index.data(Qt::ToolTipRole);
                const QString accessibleDescription = accessibleDescriptionData.toString();
                const QString toolTip = toolTipData.toString();
                const QString cell = objectAddress + " / " + header;
                if (header.isEmpty())
                    return objectAddress + " has an empty column heading";
                if (accessibleTextData.metaType().id() != int(QMetaType::QString))
                    return cell + " AccessibleTextRole is not a QString";
                if (accessibleDescriptionData.metaType().id() != int(QMetaType::QString))
                    return cell + " AccessibleDescriptionRole is not a QString";
                if (toolTipData.metaType().id() != int(QMetaType::QString))
                    return cell + " ToolTipRole is not a QString";
                if (accessibleTextData.toString() != displayed)
                    return cell + " accessible text does not match DisplayRole";
                if (!accessibleDescription.contains(objectAddress))
                    return cell + " description omits the object address";
                if (!accessibleDescription.contains(header))
                    return cell + " description omits the column heading";
                if (!displayed.isEmpty()) {
                    if (!accessibleDescription.contains(displayed))
                        return cell + " description omits the complete value";
                }
                if (!accessibleDescription.contains("Mock", Qt::CaseInsensitive))
                    return cell + " description omits the Mock source";
                if (!accessibleDescription.contains("controller", Qt::CaseInsensitive))
                    return cell + " description omits the controller boundary";
                if (!accessibleDescription.contains("SDO", Qt::CaseInsensitive))
                    return cell + " description omits the SDO boundary";
                if (toolTip != accessibleDescription)
                    return cell + " tooltip and accessible description differ";
                ++inspectedCells;
            }
            const QString childError = self(self, addressIndex);
            if (!childError.isEmpty())
                return childError;
        }
        return {};
    };
    const QString cellError = verifyCells(verifyCells, {});
    QVERIFY2(cellError.isEmpty(), qPrintable(cellError));
    QVERIFY(inspectedCells > 0);

    QModelIndex longObject = findByDisplayText(model, "6060:00");
    QVERIFY(longObject.isValid());
    const QModelIndex longNameCell = longObject.siblingAtColumn(nameColumn);
    const QModelIndex longValueCell = longObject.siblingAtColumn(valueColumn);
    const QModelIndex emptyUnitCell = longObject.siblingAtColumn(unitColumn);
    const QString longValueText
        = "0x" + QString::fromLatin1(longRawValue.toHex().toUpper());
    QCOMPARE(longNameCell.data(Qt::AccessibleTextRole).toString(), longName);
    QVERIFY(longNameCell.data(Qt::AccessibleDescriptionRole).toString().contains(longName));
    QVERIFY(longNameCell.data(Qt::ToolTipRole).toString().contains(longName));
    QCOMPARE(longValueCell.data(Qt::AccessibleTextRole).toString(), longValueText);
    QVERIFY(longValueCell.data(Qt::AccessibleDescriptionRole).toString().contains(longValueText));
    QVERIFY(longValueCell.data(Qt::ToolTipRole).toString().contains(longValueText));
    QVERIFY(longValueCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("edit", Qt::CaseInsensitive));
    QVERIFY(longValueCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("temporarily", Qt::CaseInsensitive));
    QCOMPARE(emptyUnitCell.data(Qt::AccessibleTextRole).metaType().id(), int(QMetaType::QString));
    QCOMPARE(emptyUnitCell.data(Qt::AccessibleTextRole).toString(), QString());

    dictionary->setCurrentIndex(longObject);
    QTRY_VERIFY(addToStartup->isEnabled());
    const Qt::ItemFlags editableFlags = model->flags(longValueCell);
    QVERIFY(editableFlags & Qt::ItemIsEditable);
    const Data::ProjectSnapshot projectBeforeEdit = *projectService->project(file.projectId);
    QSignalSpy valueChanged(model, &QAbstractItemModel::dataChanged);
    const QByteArray editedRawValue(256, char(0x5A));
    const QString editedRawText = QString::fromLatin1(editedRawValue.toHex().toUpper());
    QVERIFY(model->setData(longValueCell, editedRawText, Qt::EditRole));
    const QString editedValueText = "0x" + editedRawText;
    QCOMPARE(longValueCell.data(Qt::AccessibleTextRole).toString(), editedValueText);
    QVERIFY(longValueCell.data(Qt::AccessibleDescriptionRole).toString().contains(editedValueText));
    QVERIFY(longValueCell.data(Qt::ToolTipRole).toString().contains(editedValueText));
    QCOMPARE(model->flags(longValueCell), editableFlags);
    QCOMPARE(dictionary->currentIndex().siblingAtColumn(indexColumn).data().toString(),
             QString("6060:00"));
    QVERIFY(addToStartup->isEnabled());
    QCOMPARE(*projectService->project(file.projectId), projectBeforeEdit);

    bool foundCompleteRoleUpdate = false;
    for (const QList<QVariant> &arguments : std::as_const(valueChanged)) {
        const QModelIndex topLeft = arguments.at(0).value<QModelIndex>();
        const QModelIndex bottomRight = arguments.at(1).value<QModelIndex>();
        const QList<int> roles = arguments.at(2).value<QList<int>>();
        if (topLeft.parent() == longValueCell.parent() && topLeft.row() == longValueCell.row()
            && topLeft.column() <= valueColumn && bottomRight.column() >= valueColumn
            && roles.contains(Qt::DisplayRole) && roles.contains(Qt::EditRole)
            && roles.contains(Qt::AccessibleTextRole)
            && roles.contains(Qt::AccessibleDescriptionRole)
            && roles.contains(Qt::ToolTipRole)) {
            foundCompleteRoleUpdate = true;
            break;
        }
    }
    QVERIFY(foundCompleteRoleUpdate);

    showOffline->setChecked(true);
    QTRY_VERIFY(showOffline->isChecked());
    longObject = findByDisplayText(model, "6060:00");
    QVERIFY(longObject.isValid());
    const QModelIndex offlineValueCell = longObject.siblingAtColumn(valueColumn);
    QCOMPARE(offlineValueCell.data(Qt::AccessibleTextRole).toString(), longValueText);
    QVERIFY(offlineValueCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("offline", Qt::CaseInsensitive));
    QVERIFY(offlineValueCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains(longValueText));
    QVERIFY(offlineValueCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("read-only", Qt::CaseInsensitive));
    QVERIFY(offlineValueCell.data(Qt::ToolTipRole)
                .toString()
                .contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!(model->flags(offlineValueCell) & Qt::ItemIsEditable));
    QCOMPARE(*projectService->project(file.projectId), projectBeforeEdit);
}

void EtherCATWorkbenchTests::testCoeRepositoryReadOnlyWorkflow()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray repositoryEsi = deviceEsi();
    const QByteArray originalProductCode = "#x00005678";
    QCOMPARE(repositoryEsi.count(originalProductCode), 1);
    repositoryEsi.replace(originalProductCode, "#xC0E00001");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("coe-repository-read-only.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(repositoryEsi));
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0xC0E00001;
    });
    QVERIFY(device != devices.cend());

    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (ProjectExplorer::ProjectManager::projects().contains(opened.project()))
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(device->id).isValid());

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::COE_ONLINE_PAGE_ID, nullptr));
    QVERIFY(page);
    QTreeView *dictionary = page->findChild<QTreeView *>("EtherCATCoeObjectDictionary");
    QPushButton *addToStartup = page->findChild<QPushButton *>("EtherCATCoeAddToStartup");
    QCheckBox *showOffline = page->findChild<QCheckBox *>("EtherCATCoeShowOffline");
    QLabel *source = page->findChild<QLabel *>("EtherCATCoeDataSource");
    QVERIFY(dictionary);
    QVERIFY(addToStartup);
    QVERIFY(showOffline);
    QVERIFY(source);
    QAbstractItemModel *model = dictionary->model();
    QVERIFY(model);
    QAbstractItemModelTester modelTester(
        model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const int flagsColumn = columnWithHeader(model, "Flags");
    const int valueColumn = columnWithHeader(model, "Value");
    QCOMPARE(flagsColumn, 2);
    QCOMPARE(valueColumn, 3);

    const QModelIndex configuredSlave = controller.treeModel()->indexForNodeId(file.slaveId);
    const Core::PropertyPageContext configuredContext
        = controller.treeModel()->contextForIndex(configuredSlave);
    QCOMPARE(configuredContext.nodeKind, Core::WorkbenchNodeKind::ConfiguredSlave);
    pages.updatePage(Constants::COE_ONLINE_PAGE_ID, page.get(), configuredContext);
    QModelIndex object = findByDisplayText(model, "6060:00");
    QVERIFY(object.isValid());
    QModelIndex valueCell = object.siblingAtColumn(valueColumn);
    QVERIFY(model->flags(valueCell) & Qt::ItemIsEditable);
    QVERIFY(valueCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("temporarily", Qt::CaseInsensitive));
    dictionary->setCurrentIndex(object);
    QTRY_VERIFY(addToStartup->isEnabled());
    const Data::ProjectSnapshot projectBeforeContexts
        = *projectService->project(file.projectId);
    QVERIFY(model->setData(valueCell, "5A", Qt::EditRole));
    QCOMPARE(*projectService->project(file.projectId), projectBeforeContexts);

    const QModelIndex repositoryDevice = controller.treeModel()->indexForNodeId(device->id);
    const Core::PropertyPageContext repositoryContext
        = controller.treeModel()->contextForIndex(repositoryDevice);
    QCOMPARE(repositoryContext.nodeKind, Core::WorkbenchNodeKind::Device);
    QVERIFY(repositoryContext.projectId.isNull());
    pages.updatePage(Constants::COE_ONLINE_PAGE_ID, page.get(), repositoryContext);
    QVERIFY(!showOffline->isChecked());
    QVERIFY(source->text().contains("Mock", Qt::CaseInsensitive));
    object = findByDisplayText(model, "6060:00");
    QVERIFY(object.isValid());
    valueCell = object.siblingAtColumn(valueColumn);
    QVERIFY(object.siblingAtColumn(flagsColumn).data().toString().contains("RW"));
    dictionary->setCurrentIndex(object);
    QTRY_VERIFY(!addToStartup->isEnabled());

    QVERIFY2(
        !(model->flags(valueCell) & Qt::ItemIsEditable),
        "A repository Device CoE Value must not advertise ItemIsEditable");

    const QString repositoryValue = valueCell.data(Qt::EditRole).toString();
    const QVariant repositoryDisplay = valueCell.data(Qt::DisplayRole);
    const QVariant repositoryAccessibleText = valueCell.data(Qt::AccessibleTextRole);
    const QString repositoryDescription
        = valueCell.data(Qt::AccessibleDescriptionRole).toString();
    const QVariant repositoryToolTip = valueCell.data(Qt::ToolTipRole);
    QVERIFY(repositoryDescription.contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!repositoryDescription.contains("temporarily", Qt::CaseInsensitive));
    QVERIFY(repositoryDescription.contains("Mock", Qt::CaseInsensitive));
    QVERIFY(repositoryDescription.contains("controller", Qt::CaseInsensitive));
    QVERIFY(repositoryDescription.contains("SDO", Qt::CaseInsensitive));
    QCOMPARE(repositoryToolTip.toString(), repositoryDescription);
    QVERIFY(repositoryToolTip.toString().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!repositoryToolTip.toString().contains("temporarily", Qt::CaseInsensitive));
    QVERIFY(!model->setData(valueCell, "5A", Qt::EditRole));
    QCOMPARE(valueCell.data(Qt::EditRole).toString(), repositoryValue);
    QCOMPARE(valueCell.data(Qt::DisplayRole), repositoryDisplay);
    QCOMPARE(valueCell.data(Qt::AccessibleTextRole), repositoryAccessibleText);
    QCOMPARE(valueCell.data(Qt::AccessibleDescriptionRole).toString(), repositoryDescription);
    QCOMPARE(valueCell.data(Qt::ToolTipRole), repositoryToolTip);
    QCOMPARE(*projectService->project(file.projectId), projectBeforeContexts);

    pages.updatePage(Constants::COE_ONLINE_PAGE_ID, page.get(), configuredContext);
    object = findByDisplayText(model, "6060:00");
    QVERIFY(object.isValid());
    valueCell = object.siblingAtColumn(valueColumn);
    QVERIFY(model->flags(valueCell) & Qt::ItemIsEditable);
    dictionary->setCurrentIndex(object);
    QTRY_VERIFY(addToStartup->isEnabled());
    QVERIFY(model->setData(valueCell, "5A", Qt::EditRole));
    QCOMPARE(*projectService->project(file.projectId), projectBeforeContexts);

    pages.updatePage(Constants::COE_ONLINE_PAGE_ID, page.get(), repositoryContext);
    object = findByDisplayText(model, "6060:00");
    QVERIFY(object.isValid());
    valueCell = object.siblingAtColumn(valueColumn);
    QVERIFY(!(model->flags(valueCell) & Qt::ItemIsEditable));
    QVERIFY(!model->setData(valueCell, "5A", Qt::EditRole));
    QCOMPARE(*projectService->project(file.projectId), projectBeforeContexts);
}

void EtherCATWorkbenchTests::testCoeAdvancedDialogRepositoryRefresh()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray esi = deviceEsi();
    esi.replace("#x00005678", "#x7A1C0001");
    esi.replace("#x00000011", "#x0000B401");
    esi.replace("AX5000", "EL-COE-REFRESH");
    esi.replace("Workbench Servo", "CoE Refresh Servo / \u5237\u65b0");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("coe-advanced-refresh.xml");
    QByteArray updatedEsi = esi;
    updatedEsi.replace(
        "CoE Refresh Servo / \u5237\u65b0", "CoE Refresh Servo Updated / \u66f4\u65b0");
    const Utils::FilePath updatedEsiPath = Utils::FilePath::fromString(directory.path())
                                               .canonicalPath()
                                               .pathAppended("coe-advanced-refresh-updated.xml");
    const Utils::Result<qint64> writeResult = esiPath.writeFileContents(esi);
    QVERIFY_RESULT(writeResult);
    const Utils::Result<qint64> updatedWriteResult = updatedEsiPath.writeFileContents(updatedEsi);
    QVERIFY_RESULT(updatedWriteResult);
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles({esiPath}));
    QCOMPARE(importResult.requestedFiles, 1);
    QCOMPARE(importResult.importedDevices, 1);
    QCOMPARE(importResult.failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.typeName == "EL-COE-REFRESH";
    });
    QVERIFY(device != devices.cend());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(device->id).isValid());

    controller.selectionService()->setCurrentNodeId(device->id);
    DetailsView details(&controller);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::COE_ONLINE_PAGE_ID).toString());
    QVERIFY(page);
    QTreeView *dictionary = page->findChild<QTreeView *>("EtherCATCoeObjectDictionary");
    QPushButton *advanced = page->findChild<QPushButton *>("EtherCATCoeAdvanced");
    QCheckBox *showOffline = page->findChild<QCheckBox *>("EtherCATCoeShowOffline");
    QLineEdit *filter = page->findChild<QLineEdit *>("EtherCATCoeFilter");
    QLabel *dataSource = page->findChild<QLabel *>("EtherCATCoeDataSource");
    QVERIFY(dictionary);
    QVERIFY(advanced);
    QVERIFY(showOffline);
    QVERIFY(filter);
    QVERIFY(dataSource);
    QAbstractItemModel *model = dictionary->model();
    QVERIFY(model);
    QAbstractItemModelTester modelTester(
        model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    QCOMPARE(details.currentContext().nodeId, device->id);
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::Device);
    const int initialTopLevelRows = model->rowCount();
    QVERIFY(initialTopLevelRows > 0);
    QVERIFY(dictionary->currentIndex().isValid());
    QVERIFY(advanced->isEnabled());
    QVERIFY(!showOffline->isChecked());
    QVERIFY(filter->text().isEmpty());
    QVERIFY(dataSource->text().contains("Mock", Qt::CaseInsensitive));

    QSignalSpy devicesChanged(repository, &Core::DeviceRepositoryProvider::devicesChanged);
    Data::DeviceImportResult refreshResult;
    bool dialogSeen = false;
    bool repositoryRefreshSeen = false;
    bool dialogAcceptedAfterRefresh = false;
    QTimer dialogWatchdog;
    dialogWatchdog.setSingleShot(true);
    connect(&dialogWatchdog, &QTimer::timeout, page, [] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog && dialog->objectName() == "EtherCATCoeAdvancedDialog")
            dialog->reject();
    });
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        QPointer<QDialog> dialog
            = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATCoeAdvancedDialog")
            return;
        QAbstractButton *offline
            = dialog->findChild<QAbstractButton *>("EtherCATCoeAdvancedOfflineSource");
        QComboBox *range = dialog->findChild<QComboBox *>("EtherCATCoeAdvancedRange");
        QCheckBox *hideStandard
            = dialog->findChild<QCheckBox *>("EtherCATCoeAdvancedHideStandard");
        if (!offline || !range || !hideStandard)
            return;
        const int communicationRange = range->findText("Communication", Qt::MatchStartsWith);
        if (communicationRange < 0)
            return;
        dialogSeen = true;
        offline->setChecked(true);
        range->setCurrentIndex(communicationRange);
        hideStandard->setChecked(true);

        refreshResult = waitForJob(repository->importFiles({updatedEsiPath}));
        repositoryRefreshSeen = refreshResult.updatedDevices == 1
                                && refreshResult.affectedDeviceIds
                                       == QList<Data::NodeId>{device->id}
                                && !showOffline->isChecked() && filter->text().isEmpty()
                                && dataSource->text().contains("Mock", Qt::CaseInsensitive)
                                && model->rowCount() == initialTopLevelRows;
        if (!dialog)
            return;
        dialogAcceptedAfterRefresh = true;
        dialog->accept();
    });
    advanced->click();
    QTRY_VERIFY(dialogSeen);
    QTRY_VERIFY(repositoryRefreshSeen);
    QTRY_VERIFY(dialogAcceptedAfterRefresh);
    dialogWatchdog.stop();
    QCOMPARE(refreshResult.requestedFiles, 1);
    QCOMPARE(refreshResult.importedDevices, 0);
    QCOMPARE(refreshResult.updatedDevices, 1);
    QCOMPARE(refreshResult.failedFiles, 0);
    QCOMPARE(devicesChanged.count(), 1);
    const QList<QVariant> changedArguments = devicesChanged.takeFirst();
    QCOMPARE(changedArguments.size(), 1);
    QCOMPARE(
        changedArguments.first().value<QList<Data::NodeId>>(), QList<Data::NodeId>{device->id});

    QCOMPARE(details.currentContext().nodeId, device->id);
    QVERIFY(!showOffline->isChecked());
    QVERIFY(filter->text().isEmpty());
    QVERIFY(dataSource->text().contains("Mock", Qt::CaseInsensitive));
    QCOMPARE(model->rowCount(), initialTopLevelRows);
    QVERIFY(findByDisplayText(model, "6060:00").isValid());
    QVERIFY(projectService->projects().isEmpty());

    QPointer<QWidget> guardedPage(page);
    QPointer<QDialog> guardedDialog;
    bool removalDialogSeen = false;
    QTimer::singleShot(0, page, [&] {
        guardedDialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!guardedDialog || guardedDialog->objectName() != "EtherCATCoeAdvancedDialog")
            return;
        removalDialogSeen = true;
        controller.selectionService()->clear();
    });
    advanced->click();
    QTRY_VERIFY(removalDialogSeen);
    QTRY_VERIFY(guardedPage.isNull());
    QTRY_VERIFY(guardedDialog.isNull());
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
}

void EtherCATWorkbenchTests::testCoeAddConfirmationRepositoryRefresh()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray esi = deviceEsi();
    esi.replace("#x00005678", "#x7A1C0002");
    esi.replace("#x00000011", "#x0000B402");
    esi.replace("AX5000", "EL-COE-CONFIRM");
    esi.replace("Workbench Servo", "CoE Confirmation Servo / \u786e\u8ba4");
    QByteArray updatedEsi = esi;
    updatedEsi.replace(
        "CoE Confirmation Servo / \u786e\u8ba4",
        "CoE Confirmation Servo Updated / \u66f4\u65b0");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .canonicalPath()
                                        .pathAppended("coe-confirmation-refresh.xml");
    const Utils::FilePath updatedEsiPath = Utils::FilePath::fromString(directory.path())
                                               .canonicalPath()
                                               .pathAppended("coe-confirmation-refresh-updated.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(esi));
    QVERIFY_RESULT(updatedEsiPath.writeFileContents(updatedEsi));
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);

    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.typeName == "EL-COE-CONFIRM";
    });
    QVERIFY(device != devices.cend());
    const TestProjectFile file = writeProjectWithSlave(directory, *device);
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (ProjectExplorer::ProjectManager::projects().contains(opened.project()))
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    controller.selectionService()->setCurrentNodeId(file.slaveId);
    DetailsView details(&controller);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::COE_ONLINE_PAGE_ID).toString());
    QVERIFY(page);
    QTreeView *dictionary = page->findChild<QTreeView *>("EtherCATCoeObjectDictionary");
    QPushButton *addToStartup = page->findChild<QPushButton *>("EtherCATCoeAddToStartup");
    QVERIFY(dictionary);
    QVERIFY(addToStartup);
    QAbstractItemModel *model = dictionary->model();
    QVERIFY(model);
    QAbstractItemModelTester modelTester(
        model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QModelIndex writableObject = findByDisplayText(model, "6060:00");
    QVERIFY(writableObject.isValid());
    dictionary->setCurrentIndex(writableObject);
    QTRY_VERIFY(addToStartup->isEnabled());

    const Data::ProjectSnapshot projectBeforeConfirmation
        = *projectService->project(file.projectId);
    QSignalSpy devicesChanged(repository, &Core::DeviceRepositoryProvider::devicesChanged);
    Data::DeviceImportResult refreshResult;
    bool messageSeen = false;
    bool repositoryRefreshSeen = false;
    bool confirmationAcceptedAfterRefresh = false;
    QTimer dialogWatchdog;
    dialogWatchdog.setSingleShot(true);
    connect(&dialogWatchdog, &QTimer::timeout, page, [] {
        if (auto messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            messageBox->reject();
    });
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        QPointer<QMessageBox> messageBox
            = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        messageSeen = true;
        refreshResult = waitForJob(repository->importFiles({updatedEsiPath}));
        repositoryRefreshSeen = refreshResult.updatedDevices == 1
                                && refreshResult.affectedDeviceIds
                                       == QList<Data::NodeId>{device->id}
                                && details.currentContext().nodeId == file.slaveId
                                && *projectService->project(file.projectId)
                                       == projectBeforeConfirmation;
        if (!messageBox)
            return;
        if (QAbstractButton *yes = messageBox->button(QMessageBox::Yes)) {
            confirmationAcceptedAfterRefresh = true;
            yes->click();
        }
    });
    addToStartup->click();
    QTRY_VERIFY(messageSeen);
    QTRY_VERIFY(repositoryRefreshSeen);
    QTRY_VERIFY(confirmationAcceptedAfterRefresh);
    dialogWatchdog.stop();
    QCOMPARE(refreshResult.requestedFiles, 1);
    QCOMPARE(refreshResult.importedDevices, 0);
    QCOMPARE(refreshResult.updatedDevices, 1);
    QCOMPARE(refreshResult.failedFiles, 0);
    QCOMPARE(devicesChanged.count(), 1);
    const QList<QVariant> changedArguments = devicesChanged.takeFirst();
    QCOMPARE(changedArguments.size(), 1);
    QCOMPARE(
        changedArguments.first().value<QList<Data::NodeId>>(), QList<Data::NodeId>{device->id});
    QCOMPARE(*projectService->project(file.projectId), projectBeforeConfirmation);

    const QModelIndex refreshedWritableObject = findByDisplayText(model, "6060:00");
    QVERIFY(refreshedWritableObject.isValid());
    dictionary->setCurrentIndex(refreshedWritableObject);
    QTRY_VERIFY(addToStartup->isEnabled());
    QPointer<QWidget> guardedPage(page);
    QPointer<QMessageBox> guardedMessageBox;
    bool removalMessageSeen = false;
    QTimer::singleShot(0, page, [&] {
        guardedMessageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!guardedMessageBox)
            return;
        removalMessageSeen = true;
        controller.selectionService()->clear();
    });
    addToStartup->click();
    QTRY_VERIFY(removalMessageSeen);
    QTRY_VERIFY(guardedPage.isNull());
    QTRY_VERIFY(guardedMessageBox.isNull());
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QCOMPARE(*projectService->project(file.projectId), projectBeforeConfirmation);
}

void EtherCATWorkbenchTests::testStartupTableAccessibility()
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
                                        .pathAppended("startup-accessibility.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(deviceEsi()));
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
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (ProjectExplorer::ProjectManager::projects().contains(opened.project())) {
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());

    const QByteArray longRawValue(256, char(0xA5));
    const QString longComment = QString::fromUtf8(
                                    "超长 Startup 注释 / 長い起動コメント / Ω — 完整内容 — ")
                                + QString(256, QChar(u'甲')) + " / End";
    const Data::StartupParameterConfiguration enabled{
        Data::NodeId::create(),
        true,
        0,
        "PS",
        0x2000,
        0,
        Data::EtherCATDataType::OctetString,
        "OCTET_STRING",
        longRawValue,
        longComment};
    const Data::StartupParameterConfiguration disabled{
        Data::NodeId::create(),
        false,
        1,
        "SO",
        0x2001,
        1,
        Data::EtherCATDataType::UnsignedInteger8,
        "UINT8",
        QByteArray::fromHex("00"),
        "Disabled request"};
    const Data::StartupParameterConfiguration fixed{
        Data::NodeId::create(),
        true,
        2,
        "<PS>",
        0x8000,
        1,
        Data::EtherCATDataType::UnsignedInteger8,
        "UINT8",
        QByteArray::fromHex("00"),
        "Fixed ESI request"};
    QVERIFY_RESULT(projectService->setStartupConfiguration(
        file.projectId, file.slaveId, {{enabled, disabled, fixed}}));
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::STARTUP_PAGE_ID, nullptr));
    QVERIFY(page);
    const QModelIndex configuredSlave = controller.treeModel()->indexForNodeId(file.slaveId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(configuredSlave));

    QTableView *table = page->findChild<QTableView *>("EtherCATStartupTable");
    QVERIFY(table);
    QVERIFY(!table->accessibleName().isEmpty());
    QVERIFY(!table->accessibleDescription().isEmpty());
    QCOMPARE(table->model()->rowCount(), 3);

    const int enabledColumn = columnWithHeader(table->model(), "Enabled");
    const int protocolColumn = columnWithHeader(table->model(), "Protocol");
    const int dataColumn = columnWithHeader(table->model(), "Data");
    const int commentColumn = columnWithHeader(table->model(), "Comment");
    QVERIFY(enabledColumn >= 0);
    QVERIFY(protocolColumn >= 0);
    QVERIFY(dataColumn >= 0);
    QVERIFY(commentColumn >= 0);

    for (int row = 0; row < table->model()->rowCount(); ++row) {
        const QString address = row == 0 ? "0x2000:00" : row == 1 ? "0x2001:01" : "0x8000:01";
        for (int column = 0; column < table->model()->columnCount(); ++column) {
            const QModelIndex index = table->model()->index(row, column);
            const QString header
                = table->model()->headerData(column, Qt::Horizontal).toString();
            const QString displayed = index.data(Qt::DisplayRole).toString();
            const QVariant accessibleTextData = index.data(Qt::AccessibleTextRole);
            const QString accessibleText = accessibleTextData.toString();
            const QVariant accessibleDescriptionData
                = index.data(Qt::AccessibleDescriptionRole);
            const QString accessibleDescription = accessibleDescriptionData.toString();
            const QVariant toolTipData = index.data(Qt::ToolTipRole);
            const QString toolTip = toolTipData.toString();
            QVERIFY(!header.isEmpty());
            QVERIFY(accessibleTextData.isValid());
            QCOMPARE(accessibleTextData.metaType().id(), int(QMetaType::QString));
            QCOMPARE(accessibleDescriptionData.metaType().id(), int(QMetaType::QString));
            QCOMPARE(toolTipData.metaType().id(), int(QMetaType::QString));
            if (column != enabledColumn)
                QCOMPARE(accessibleText, displayed);
            QVERIFY(!accessibleDescription.isEmpty());
            QVERIFY(accessibleDescription.contains(address));
            QVERIFY(accessibleDescription.contains(header));
            if (!accessibleText.isEmpty())
                QVERIFY(accessibleDescription.contains(accessibleText));
            QVERIFY(!toolTip.isEmpty());
            QVERIFY(toolTip.contains(address));
            QVERIFY(toolTip.contains(header));
            if (!accessibleText.isEmpty())
                QVERIFY(toolTip.contains(accessibleText));
        }
    }

    const QModelIndex enabledCell = table->model()->index(0, enabledColumn);
    const QModelIndex disabledCell = table->model()->index(1, enabledColumn);
    QCOMPARE(enabledCell.data(Qt::DisplayRole).toString(), QString());
    QCOMPARE(disabledCell.data(Qt::DisplayRole).toString(), QString());
    QCOMPARE(enabledCell.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QCOMPARE(disabledCell.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QCOMPARE(enabledCell.data(Qt::AccessibleTextRole).toString(), QString("Enabled"));
    QCOMPARE(disabledCell.data(Qt::AccessibleTextRole).toString(), QString("Disabled"));
    QVERIFY(table->model()->flags(enabledCell) & Qt::ItemIsUserCheckable);

    const QString longRawValueText = QString::fromLatin1(longRawValue.toHex(' ').toUpper());
    const QModelIndex dataCell = table->model()->index(0, dataColumn);
    const QModelIndex commentCell = table->model()->index(0, commentColumn);
    QCOMPARE(dataCell.data(Qt::AccessibleTextRole).toString(), longRawValueText);
    QVERIFY(dataCell.data(Qt::AccessibleDescriptionRole).toString().contains(longRawValueText));
    QVERIFY(dataCell.data(Qt::ToolTipRole).toString().contains(longRawValueText));
    QCOMPARE(commentCell.data(Qt::AccessibleTextRole).toString(), longComment);
    QVERIFY(commentCell.data(Qt::AccessibleDescriptionRole).toString().contains(longComment));
    QVERIFY(commentCell.data(Qt::ToolTipRole).toString().contains(longComment));
    QVERIFY(commentCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("edit this value", Qt::CaseInsensitive));

    const QModelIndex protocolCell = table->model()->index(0, protocolColumn);
    QVERIFY(!(table->model()->flags(protocolCell) & Qt::ItemIsEditable));
    QVERIFY(protocolCell.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("read-only", Qt::CaseInsensitive));

    const QModelIndex fixedEnabled = table->model()->index(2, enabledColumn);
    const QModelIndex fixedComment = table->model()->index(2, commentColumn);
    QVERIFY(!(table->model()->flags(fixedEnabled) & Qt::ItemIsUserCheckable));
    QVERIFY(!(table->model()->flags(fixedComment) & Qt::ItemIsEditable));
    const QString fixedDescription = fixedComment.data(Qt::AccessibleDescriptionRole).toString();
    QVERIFY(fixedDescription.contains("fixed ESI", Qt::CaseInsensitive));
    QVERIFY(fixedDescription.contains("enabled or disabled", Qt::CaseInsensitive));
    QVERIFY(fixedDescription.contains("edited", Qt::CaseInsensitive));
    QVERIFY(fixedDescription.contains("deleted", Qt::CaseInsensitive));
    QVERIFY(fixedDescription.contains("moved", Qt::CaseInsensitive));

    const QModelIndex repositoryDevice = controller.treeModel()->indexForNodeId(device->id);
    QVERIFY(repositoryDevice.isValid());
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(repositoryDevice));
    QCOMPARE(table->model()->rowCount(), 3);
    const QModelIndex readOnlyComment = table->model()->index(0, commentColumn);
    QVERIFY(!(table->model()->flags(readOnlyComment) & Qt::ItemIsEditable));
    QVERIFY(readOnlyComment.data(Qt::AccessibleDescriptionRole)
                .toString()
                .contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!readOnlyComment.data(Qt::AccessibleDescriptionRole)
                 .toString()
                 .contains("edit this value", Qt::CaseInsensitive));
}

void EtherCATWorkbenchTests::testStartupRepositoryEmptyState()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto uniqueDevice = [](QByteArray esi,
                                 const QByteArray &productCode,
                                 const QByteArray &revision,
                                 const QByteArray &typeName,
                                 const QByteArray &name) {
        esi.replace("#x00005678", productCode);
        esi.replace("#x00000011", revision);
        esi.replace("AX5000", typeName);
        esi.replace("Workbench Servo", name);
        return esi;
    };

    QByteArray supportedEmptyEsi = deviceEsi();
    QVERIFY(removeFirstXmlElement(&supportedEmptyEsi, "<InitCmds>", "</InitCmds>"));
    supportedEmptyEsi = uniqueDevice(
        supportedEmptyEsi,
        "#x7A190001",
        "#x0000B101",
        "EL-STARTUP-EMPTY",
        "Startup Empty Servo / 无启动请求");

    QByteArray unsupportedEmptyEsi = supportedEmptyEsi;
    unsupportedEmptyEsi.replace("#x7A190001", "#x7A190002");
    unsupportedEmptyEsi.replace("#x0000B101", "#x0000B102");
    unsupportedEmptyEsi.replace("EL-STARTUP-EMPTY", "EL-STARTUP-EMPTY-UNSUPPORTED");
    unsupportedEmptyEsi.replace("无启动请求", "无启动请求 / 不支持");
    unsupportedEmptyEsi.replace("</Device>", "<Modules/></Device>");

    QByteArray supportedPopulatedEsi = uniqueDevice(
        deviceEsi(),
        "#x7A190003",
        "#x0000B103",
        "EL-STARTUP-POPULATED",
        "Startup Populated Servo / 已解析启动请求");

    QByteArray unsupportedPopulatedEsi = supportedPopulatedEsi;
    unsupportedPopulatedEsi.replace("#x7A190003", "#x7A190004");
    unsupportedPopulatedEsi.replace("#x0000B103", "#x0000B104");
    unsupportedPopulatedEsi.replace("EL-STARTUP-POPULATED", "EL-STARTUP-POPULATED-UNSUPPORTED");
    unsupportedPopulatedEsi.replace("已解析启动请求", "已解析启动请求 / 不支持");
    unsupportedPopulatedEsi.replace("</Device>", "<Modules/></Device>");

    QByteArray supportedInvalidEsi = supportedPopulatedEsi;
    supportedInvalidEsi.replace("#x7A190003", "#x7A190005");
    supportedInvalidEsi.replace("#x0000B103", "#x0000B105");
    supportedInvalidEsi.replace("EL-STARTUP-POPULATED", "EL-STARTUP-INVALID");
    supportedInvalidEsi.replace("已解析启动请求", "启动请求校验错误");
    QVERIFY(supportedInvalidEsi.contains("<Transition>PS</Transition>"));
    supportedInvalidEsi.replace("<Transition>PS</Transition>", "<Transition></Transition>");

    QByteArray unsupportedInvalidEsi = unsupportedPopulatedEsi;
    unsupportedInvalidEsi.replace("#x7A190004", "#x7A190006");
    unsupportedInvalidEsi.replace("#x0000B104", "#x0000B106");
    unsupportedInvalidEsi
        .replace("EL-STARTUP-POPULATED-UNSUPPORTED", "EL-STARTUP-INVALID-UNSUPPORTED");
    unsupportedInvalidEsi.replace("已解析启动请求 / 不支持", "启动请求校验错误 / 不支持");
    QVERIFY(unsupportedInvalidEsi.contains("<Transition>PS</Transition>"));
    unsupportedInvalidEsi.replace("<Transition>PS</Transition>", "<Transition></Transition>");

    struct EsiFixture
    {
        QString fileName;
        QByteArray xml;
    };
    const QList<EsiFixture> fixtures = {
        {"startup-empty.xml", supportedEmptyEsi},
        {"startup-empty-unsupported.xml", unsupportedEmptyEsi},
        {"startup-populated.xml", supportedPopulatedEsi},
        {"startup-populated-unsupported.xml", unsupportedPopulatedEsi},
        {"startup-invalid.xml", supportedInvalidEsi},
        {"startup-invalid-unsupported.xml", unsupportedInvalidEsi},
    };
    QList<Utils::FilePath> esiPaths;
    for (const EsiFixture &fixture : fixtures) {
        const Utils::FilePath path
            = Utils::FilePath::fromString(directory.path()).pathAppended(fixture.fileName);
        const Utils::Result<qint64> writeResult = path.writeFileContents(fixture.xml);
        QVERIFY_RESULT(writeResult);
        esiPaths.append(path);
    }
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles(esiPaths));
    QCOMPARE(importResult.requestedFiles, fixtures.size());
    QCOMPARE(importResult.importedDevices, fixtures.size());
    QCOMPARE(importResult.failedFiles, 0);
    QCOMPARE(importResult.affectedDeviceIds.size(), fixtures.size());

    const auto idForType = [repository](const QString &typeName) {
        const QList<Data::DeviceSummary> devices = repository->devices();
        const auto found = std::find_if(
            devices.cbegin(), devices.cend(), [&typeName](const Data::DeviceSummary &device) {
                return device.typeName == typeName;
            });
        return found == devices.cend() ? Data::NodeId() : found->id;
    };
    const Data::NodeId supportedEmptyId = idForType("EL-STARTUP-EMPTY");
    const Data::NodeId unsupportedEmptyId = idForType("EL-STARTUP-EMPTY-UNSUPPORTED");
    const Data::NodeId supportedPopulatedId = idForType("EL-STARTUP-POPULATED");
    const Data::NodeId unsupportedPopulatedId = idForType("EL-STARTUP-POPULATED-UNSUPPORTED");
    const Data::NodeId supportedInvalidId = idForType("EL-STARTUP-INVALID");
    const Data::NodeId unsupportedInvalidId = idForType("EL-STARTUP-INVALID-UNSUPPORTED");
    QVERIFY(!supportedEmptyId.isNull());
    QVERIFY(!unsupportedEmptyId.isNull());
    QVERIFY(!supportedPopulatedId.isNull());
    QVERIFY(!unsupportedPopulatedId.isNull());
    QVERIFY(!supportedInvalidId.isNull());
    QVERIFY(!unsupportedInvalidId.isNull());

    const std::optional<Data::DeviceDescription> supportedEmpty = repository->device(
        supportedEmptyId);
    const std::optional<Data::DeviceDescription> unsupportedEmpty = repository->device(
        unsupportedEmptyId);
    const std::optional<Data::DeviceDescription> supportedPopulated = repository->device(
        supportedPopulatedId);
    const std::optional<Data::DeviceDescription> unsupportedPopulated = repository->device(
        unsupportedPopulatedId);
    const std::optional<Data::DeviceDescription> supportedInvalid = repository->device(
        supportedInvalidId);
    const std::optional<Data::DeviceDescription> unsupportedInvalid = repository->device(
        unsupportedInvalidId);
    QVERIFY(supportedEmpty);
    QVERIFY(unsupportedEmpty);
    QVERIFY(supportedPopulated);
    QVERIFY(unsupportedPopulated);
    QVERIFY(supportedInvalid);
    QVERIFY(unsupportedInvalid);
    QVERIFY(supportedEmpty->summary.supported);
    QVERIFY(supportedEmpty->startupParameters.isEmpty());
    QVERIFY(!unsupportedEmpty->summary.supported);
    QVERIFY(unsupportedEmpty->startupParameters.isEmpty());
    QVERIFY(supportedPopulated->summary.supported);
    QCOMPARE(supportedPopulated->startupParameters.size(), 3);
    QVERIFY(!unsupportedPopulated->summary.supported);
    QCOMPARE(unsupportedPopulated->startupParameters.size(), 3);
    QVERIFY(supportedInvalid->summary.supported);
    QCOMPARE(supportedInvalid->startupParameters.size(), 3);
    QVERIFY(supportedInvalid->startupParameters.first().transition.isEmpty());
    QVERIFY(!unsupportedInvalid->summary.supported);
    QCOMPARE(unsupportedInvalid->startupParameters.size(), 3);
    QVERIFY(unsupportedInvalid->startupParameters.first().transition.isEmpty());

    for (const Data::NodeId &deviceId :
         {supportedEmptyId,
          unsupportedEmptyId,
          supportedPopulatedId,
          unsupportedPopulatedId,
          supportedInvalidId,
          unsupportedInvalidId}) {
        QTRY_VERIFY(controller.treeModel()->indexForNodeId(deviceId).isValid());
    }

    BuiltinPropertyPageProvider pages(&controller);
    std::unique_ptr<QWidget> page(pages.createPage(Constants::STARTUP_PAGE_ID, nullptr));
    QVERIFY(page);
    QLabel *summary = page->findChild<QLabel *>("EtherCATStartupSummary");
    Utils::InfoLabel *validation = dynamic_cast<Utils::InfoLabel *>(
        page->findChild<QLabel *>("EtherCATStartupValidation"));
    QTableView *table = page->findChild<QTableView *>("EtherCATStartupTable");
    QPushButton *restoreDefaults = page->findChild<QPushButton *>("EtherCATStartupRestoreDefaults");
    const QList<QPushButton *> editingButtons = {
        page->findChild<QPushButton *>("EtherCATStartupMoveUp"),
        page->findChild<QPushButton *>("EtherCATStartupMoveDown"),
        page->findChild<QPushButton *>("EtherCATStartupNew"),
        page->findChild<QPushButton *>("EtherCATStartupDelete"),
        page->findChild<QPushButton *>("EtherCATStartupEdit"),
    };
    QVERIFY(summary);
    QVERIFY(validation);
    QVERIFY(table);
    QVERIFY(restoreDefaults);
    for (QPushButton *button : editingButtons)
        QVERIFY(button);

    const auto repositoryTableMutationFailure = [table]() -> QString {
        bool inspectedCell = false;
        QAbstractItemModel *model = table->model();
        for (int row = 0; row < model->rowCount(); ++row) {
            for (int column = 0; column < model->columnCount(); ++column) {
                const QModelIndex index = model->index(row, column);
                if (!index.isValid())
                    continue;
                inspectedCell = true;
                const Qt::ItemFlags flags = model->flags(index);
                if (flags & (Qt::ItemIsEditable | Qt::ItemIsUserCheckable)) {
                    return QString("Startup table exposes mutable flags at %1,%2")
                        .arg(row)
                        .arg(column);
                }
                const QVariant display = model->data(index, Qt::DisplayRole);
                const QVariant checkState = model->data(index, Qt::CheckStateRole);
                if (model->setData(index, QString("Repository mutation"), Qt::EditRole)) {
                    return QString("Startup table accepted EditRole at %1,%2").arg(row).arg(column);
                }
                if (model->data(index, Qt::DisplayRole) != display) {
                    return QString("Startup table changed display data at %1,%2")
                        .arg(row)
                        .arg(column);
                }
                if (model->setData(index, Qt::Checked, Qt::CheckStateRole)) {
                    return QString("Startup table accepted CheckStateRole at %1,%2")
                        .arg(row)
                        .arg(column);
                }
                if (model->data(index, Qt::CheckStateRole) != checkState) {
                    return QString("Startup table changed check state at %1,%2").arg(row).arg(column);
                }
            }
        }
        return inspectedCell ? QString()
                             : QString("No repository Startup table cell was inspected");
    };
    const auto verifyRepositoryControls = [&editingButtons, restoreDefaults]() -> QString {
        if (!restoreDefaults->isHidden())
            return QString("Restore Defaults is visible for a repository Device");
        for (QPushButton *button : editingButtons) {
            if (button->isEnabled())
                return QString("%1 is enabled for a repository Device").arg(button->objectName());
        }
        return {};
    };

    const QModelIndex supportedEmptyIndex = controller.treeModel()->indexForNodeId(supportedEmptyId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(supportedEmptyIndex));
    QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(!table->currentIndex().isValid());
    QVERIFY(summary->text().contains("No ESI Startup", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("can still be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("manual", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("configuration is valid", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("No ESI Startup", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Information);
    QVERIFY(table->accessibleDescription().contains("No ESI Startup", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("will not fabricate", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("controller", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("network", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("physical hardware", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());
    QString controlFailure = verifyRepositoryControls();
    QVERIFY2(controlFailure.isEmpty(), qPrintable(controlFailure));

    const QModelIndex unsupportedEmptyIndex = controller.treeModel()->indexForNodeId(
        unsupportedEmptyId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(unsupportedEmptyIndex));
    QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(summary->text().contains("No ESI Startup", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("configuration is valid", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    QVERIFY(table->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());
    controlFailure = verifyRepositoryControls();
    QVERIFY2(controlFailure.isEmpty(), qPrintable(controlFailure));

    const QModelIndex unsupportedPopulatedIndex = controller.treeModel()->indexForNodeId(
        unsupportedPopulatedId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(unsupportedPopulatedIndex));
    QCOMPARE(table->model()->rowCount(), 3);
    QVERIFY(summary->text().contains("read-only preview", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    QVERIFY(validation->text().contains("data type is unknown", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QCOMPARE(validation->toolTip().split('\n').size(), 3);
    QVERIFY(table->accessibleDescription().contains("read-only preview", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());
    QString mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));

    const QModelIndex supportedInvalidIndex = controller.treeModel()->indexForNodeId(
        supportedInvalidId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(supportedInvalidIndex));
    QCOMPARE(table->model()->rowCount(), 3);
    QVERIFY(summary->text().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Error);
    QVERIFY(validation->text().contains("Startup configuration error", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("transition", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(validation->toolTip().split('\n').size(), 4);
    QVERIFY(validation->toolTip().contains("Startup transition must be specified."));
    QVERIFY(validation->toolTip().contains("Startup data type is unknown"));
    QVERIFY(table->accessibleDescription().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());
    mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));

    const QModelIndex unsupportedInvalidIndex = controller.treeModel()->indexForNodeId(
        unsupportedInvalidId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(unsupportedInvalidIndex));
    QCOMPARE(table->model()->rowCount(), 3);
    QVERIFY(summary->text().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Error);
    QVERIFY(validation->text().contains("transition", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QCOMPARE(validation->toolTip().split('\n').size(), 4);
    QVERIFY(validation->toolTip().contains("Startup transition must be specified."));
    QVERIFY(validation->toolTip().contains("Startup data type is unknown"));
    QVERIFY(table->accessibleDescription().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());
    mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));

    const Core::PropertyPageContext missingContext{
        {}, Data::NodeId::create(), Core::WorkbenchNodeKind::Device, "Removed ESI Device"};
    pages.updatePage(Constants::STARTUP_PAGE_ID, page.get(), missingContext);
    QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(!table->currentIndex().isValid());
    QVERIFY(summary->text().contains("no longer available", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Add the device", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    QVERIFY(validation->text().contains("unavailable", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("configuration is valid", Qt::CaseInsensitive));
    QVERIFY(validation->toolTip().isEmpty());
    QVERIFY(table->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
    QVERIFY(table->accessibleDescription().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());

    const QModelIndex supportedPopulatedIndex = controller.treeModel()->indexForNodeId(
        supportedPopulatedId);
    pages.updatePage(
        Constants::STARTUP_PAGE_ID,
        page.get(),
        controller.treeModel()->contextForIndex(supportedPopulatedIndex));
    QCOMPARE(table->model()->rowCount(), 3);
    QVERIFY(summary->text().contains("ESI Startup requests", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Add the device", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("No ESI Startup", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("no longer available", Qt::CaseInsensitive));
    QCOMPARE(validation->type(), Utils::InfoLabel::Warning);
    QVERIFY(validation->text().contains("data type is unknown", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QCOMPARE(validation->toolTip().split('\n').size(), 3);
    QVERIFY(table->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!table->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!table->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!table->accessibleDescription().contains("validation error", Qt::CaseInsensitive));
    QVERIFY(!table->accessibleDescription().contains("No ESI Startup", Qt::CaseInsensitive));
    QVERIFY(!table->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(table->toolTip(), table->accessibleDescription());
    mutationFailure = repositoryTableMutationFailure();
    QVERIFY2(mutationFailure.isEmpty(), qPrintable(mutationFailure));
    controlFailure = verifyRepositoryControls();
    QVERIFY2(controlFailure.isEmpty(), qPrintable(controlFailure));
    QVERIFY(projectService->projects().isEmpty());

    const TestProjectFile recoveryProject = writeProjectWithSlave(
        directory,
        supportedEmpty->summary,
        "startup-repository-recovery.ecatproject",
        "Startup Repository Recovery");
    QVERIFY(!recoveryProject.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(recoveryProject.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (ProjectExplorer::ProjectManager::projects().contains(opened.project()))
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(recoveryProject.projectId).has_value());
    const Utils::Result<> activationResult = projectService->activateProject(
        recoveryProject.projectId);
    QVERIFY_RESULT(activationResult);
    const Data::ProjectSnapshot beforeRecovery = *projectService->project(recoveryProject.projectId);
    const Data::NodeId activeProjectBeforeBrowsing = projectService->activeProjectId();
    const bool couldUndoBeforeBrowsing = projectService->canUndoProject(recoveryProject.projectId);
    const bool couldRedoBeforeBrowsing = projectService->canRedoProject(recoveryProject.projectId);
    for (const Data::NodeId &repositoryDeviceId :
         {supportedEmptyId,
          unsupportedEmptyId,
          supportedPopulatedId,
          unsupportedPopulatedId,
          supportedInvalidId,
          unsupportedInvalidId}) {
        const QModelIndex repositoryIndex = controller.treeModel()->indexForNodeId(
            repositoryDeviceId);
        QVERIFY(repositoryIndex.isValid());
        pages.updatePage(
            Constants::STARTUP_PAGE_ID,
            page.get(),
            controller.treeModel()->contextForIndex(repositoryIndex));
    }
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRecovery);
    QCOMPARE(projectService->activeProjectId(), activeProjectBeforeBrowsing);
    QCOMPARE(projectService->canUndoProject(recoveryProject.projectId), couldUndoBeforeBrowsing);
    QCOMPARE(projectService->canRedoProject(recoveryProject.projectId), couldRedoBeforeBrowsing);

    const Utils::Result<> supportedEmptyAdd
        = controller.addDeviceToMaster(supportedEmptyId, recoveryProject.masterId);
    QVERIFY_RESULT(supportedEmptyAdd);
    QTRY_COMPARE(
        projectService->project(recoveryProject.projectId)->slaves.size(),
        beforeRecovery.slaves.size() + 1);
    const Data::ProjectSnapshot afterSupportedEmptyAdd = *projectService->project(
        recoveryProject.projectId);
    const auto added = std::find_if(
        afterSupportedEmptyAdd.slaves.cbegin(),
        afterSupportedEmptyAdd.slaves.cend(),
        [&beforeRecovery](const Data::OfflineSlaveConfiguration &slave) {
            return std::none_of(
                beforeRecovery.slaves.cbegin(),
                beforeRecovery.slaves.cend(),
                [&slave](const Data::OfflineSlaveConfiguration &existing) {
                    return existing.id == slave.id;
                });
        });
    QVERIFY(added != afterSupportedEmptyAdd.slaves.cend());
    QVERIFY(added->startup.parameters.isEmpty());
    QVERIFY(projectService->canUndoProject(recoveryProject.projectId));
    const Utils::Result<> undoResult = projectService->undoProject(recoveryProject.projectId);
    QVERIFY_RESULT(undoResult);
    QTRY_COMPARE(*projectService->project(recoveryProject.projectId), beforeRecovery);
    QVERIFY(projectService->canRedoProject(recoveryProject.projectId));
    const Data::ProjectSnapshot beforeRejectedAdds = *projectService->project(
        recoveryProject.projectId);
    const bool canUndoBeforeRejectedAdds = projectService->canUndoProject(recoveryProject.projectId);
    const bool canRedoBeforeRejectedAdds = projectService->canRedoProject(recoveryProject.projectId);

    const Utils::Result<> unsupportedEmptyAdd
        = controller.addDeviceToMaster(unsupportedEmptyId, recoveryProject.masterId);
    QVERIFY(!unsupportedEmptyAdd);
    QVERIFY(unsupportedEmptyAdd.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    const Utils::Result<> unsupportedPopulatedAdd
        = controller.addDeviceToMaster(unsupportedPopulatedId, recoveryProject.masterId);
    QVERIFY(!unsupportedPopulatedAdd);
    QVERIFY(unsupportedPopulatedAdd.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    const Utils::Result<> supportedInvalidAdd
        = controller.addDeviceToMaster(supportedInvalidId, recoveryProject.masterId);
    QVERIFY(!supportedInvalidAdd);
    QVERIFY(supportedInvalidAdd.error().contains("Startup", Qt::CaseInsensitive));
    QVERIFY(supportedInvalidAdd.error().contains("invalid", Qt::CaseInsensitive));
    QVERIFY(supportedInvalidAdd.error().contains("transition", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    const Utils::Result<> unsupportedInvalidAdd
        = controller.addDeviceToMaster(unsupportedInvalidId, recoveryProject.masterId);
    QVERIFY(!unsupportedInvalidAdd);
    QVERIFY(unsupportedInvalidAdd.error().contains("unsupported", Qt::CaseInsensitive));
    QCOMPARE(*projectService->project(recoveryProject.projectId), beforeRejectedAdds);
    QCOMPARE(projectService->canUndoProject(recoveryProject.projectId), canUndoBeforeRejectedAdds);
    QCOMPARE(projectService->canRedoProject(recoveryProject.projectId), canRedoBeforeRejectedAdds);

    controller.selectionService()->clear();
    if (ProjectExplorer::ProjectManager::projects().contains(opened.project()))
        ProjectExplorer::ProjectManager::removeProject(opened.project());
    QTRY_VERIFY(!projectService->project(recoveryProject.projectId).has_value());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testStartupDialogProjectRefresh()
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
                                        .pathAppended("startup-dialog-refresh.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(deviceEsi()));
    QCOMPARE(waitForJob(repository->importFiles({esiPath})).failedFiles, 0);
    const QList<Data::DeviceSummary> devices = repository->devices();
    const auto device = std::find_if(devices.cbegin(), devices.cend(), [](const auto &entry) {
        return entry.identity.productCode == 0x5678;
    });
    QVERIFY(device != devices.cend());

    const TestProjectFile file = writeProjectWithSlave(
        directory, *device, "startup-dialog-refresh.ecatproject", "Startup Dialog Refresh");
    QVERIFY(!file.path.isEmpty());
    const ProjectExplorer::OpenProjectResult opened
        = ProjectExplorer::ProjectExplorerPlugin::openProject(file.path, false);
    QVERIFY2(opened, qPrintable(opened.errorMessage()));
    const auto projectCleanup = qScopeGuard([&] {
        controller.selectionService()->clear();
        if (ProjectExplorer::ProjectManager::projects().contains(opened.project()))
            ProjectExplorer::ProjectManager::removeProject(opened.project());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });
    QTRY_VERIFY(projectService->project(file.projectId).has_value());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(file.slaveId).isValid());

    controller.selectionService()->setCurrentNodeId(file.slaveId);
    DetailsView details(&controller);
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::STARTUP_PAGE_ID).toString());
    QVERIFY(page);
    QTableView *table = page->findChild<QTableView *>("EtherCATStartupTable");
    QPushButton *defaults = page->findChild<QPushButton *>("EtherCATStartupRestoreDefaults");
    QPushButton *add = page->findChild<QPushButton *>("EtherCATStartupNew");
    QPushButton *remove = page->findChild<QPushButton *>("EtherCATStartupDelete");
    QPushButton *edit = page->findChild<QPushButton *>("EtherCATStartupEdit");
    QVERIFY(table);
    QVERIFY(defaults);
    QVERIFY(add);
    QVERIFY(remove);
    QVERIFY(edit);
    QAbstractItemModelTester
        modelTester(table->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);

    QTimer dialogWatchdog;
    dialogWatchdog.setSingleShot(true);
    connect(&dialogWatchdog, &QTimer::timeout, &details, [] {
        if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });

    const std::optional<Data::DeviceDescription> deviceDescription = repository->device(device->id);
    QVERIFY(deviceDescription);
    const Data::StartupConfiguration esiDefaults
        = startupDefaultsFromDevice(*deviceDescription, file.slaveId);
    QVERIFY(!esiDefaults.parameters.isEmpty());
    QVERIFY(projectService->project(file.projectId)->slaves.first().startup.parameters.isEmpty());
    QCOMPARE(table->model()->rowCount(), esiDefaults.parameters.size());
    QCOMPARE(defaults->text(), QString("Store ESI Defaults"));

    bool proposalAddSeen = false;
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATStartupParameterDialog")
            return;
        proposalAddSeen = true;
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogComment")
            ->setText("Proposal-only New request");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    add->click();
    QTRY_VERIFY(proposalAddSeen);
    dialogWatchdog.stop();
    QTRY_COMPARE(
        projectService->project(file.projectId)->slaves.first().startup.parameters.size(),
        esiDefaults.parameters.size() + 1);
    const Data::StartupConfiguration storedProposal
        = projectService->project(file.projectId)->slaves.first().startup;
    for (int row = 0; row < esiDefaults.parameters.size(); ++row)
        QCOMPARE(storedProposal.parameters.at(row), esiDefaults.parameters.at(row));
    const Data::StartupParameterConfiguration proposalRequest = storedProposal.parameters.last();
    QVERIFY(std::none_of(
        esiDefaults.parameters.cbegin(),
        esiDefaults.parameters.cend(),
        [&proposalRequest](const auto &parameter) { return parameter.id == proposalRequest.id; }));
    QCOMPARE(proposalRequest.order, esiDefaults.parameters.size());
    QVERIFY(projectService->canUndoProject(file.projectId));
    QVERIFY_RESULT(projectService->undoProject(file.projectId));
    QTRY_VERIFY(
        projectService->project(file.projectId)->slaves.first().startup.parameters.isEmpty());
    QTRY_COMPARE(table->model()->rowCount(), esiDefaults.parameters.size());
    QCOMPARE(defaults->text(), QString("Store ESI Defaults"));
    QVERIFY(projectService->canRedoProject(file.projectId));

    Data::StartupConfiguration newRefreshConfiguration = esiDefaults;
    newRefreshConfiguration.parameters.first().comment = "New refresh wins";
    QSignalSpy newProjectChanges(projectService, &Core::ProjectService::projectChanged);
    bool newDialogSeen = false;
    bool newRefreshApplied = false;
    int changesAfterNewRefresh = 0;
    QString newRefreshError;
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        QPointer<QDialog> dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATStartupParameterDialog")
            return;
        newDialogSeen = true;
        const Utils::Result<> refresh
            = projectService
                  ->setStartupConfiguration(file.projectId, file.slaveId, newRefreshConfiguration);
        newRefreshApplied = bool(refresh);
        if (!refresh)
            newRefreshError = refresh.error();
        changesAfterNewRefresh = newProjectChanges.count();
        if (!dialog)
            return;
        dialog->findChild<QLineEdit *>("EtherCATStartupDialogComment")->setText("Stale New request");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    add->click();
    QTRY_VERIFY(newDialogSeen);
    QVERIFY2(newRefreshApplied, qPrintable(newRefreshError));
    dialogWatchdog.stop();
    QVERIFY(changesAfterNewRefresh > 0);
    QCOMPARE(newProjectChanges.count(), changesAfterNewRefresh);
    QVERIFY(
        projectService->project(file.projectId)->slaves.first().startup == newRefreshConfiguration);
    QVERIFY(projectService->canUndoProject(file.projectId));
    QVERIFY(!projectService->canRedoProject(file.projectId));

    table->setCurrentIndex(table->model()->index(0, 0));
    QTRY_VERIFY(edit->isEnabled());
    Data::StartupConfiguration refreshedConfiguration = newRefreshConfiguration;
    refreshedConfiguration.parameters.first().comment = "Project refresh wins";
    bool editDialogSeen = false;
    bool editRefreshApplied = false;
    QString editRefreshError;
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        QPointer<QDialog> dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "EtherCATStartupParameterDialog")
            return;
        editDialogSeen = true;
        const Utils::Result<> refresh
            = projectService
                  ->setStartupConfiguration(file.projectId, file.slaveId, refreshedConfiguration);
        editRefreshApplied = bool(refresh);
        if (!refresh)
            editRefreshError = refresh.error();
        if (!dialog)
            return;
        QLineEdit *comment = dialog->findChild<QLineEdit *>("EtherCATStartupDialogComment");
        QDialogButtonBox *buttons = dialog->findChild<QDialogButtonBox *>();
        if (!comment || !buttons)
            return;
        comment->setText("Stale dialog value");
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    edit->click();
    QTRY_VERIFY(editDialogSeen);
    QVERIFY2(editRefreshApplied, qPrintable(editRefreshError));
    dialogWatchdog.stop();
    QTRY_COMPARE(
        projectService->project(file.projectId)->slaves.first().startup.parameters.first().comment,
        QString("Project refresh wins"));
    QVERIFY(
        projectService->project(file.projectId)->slaves.first().startup == refreshedConfiguration);

    const Data::NodeId removedParameterId = refreshedConfiguration.parameters.first().id;
    Data::StartupConfiguration deleteRefresh = refreshedConfiguration;
    deleteRefresh.parameters.removeFirst();
    for (int row = 0; row < deleteRefresh.parameters.size(); ++row)
        deleteRefresh.parameters[row].order = row;
    table->setCurrentIndex(table->model()->index(0, 0));
    QTRY_VERIFY(remove->isEnabled());
    bool deletePromptSeen = false;
    bool deleteRefreshApplied = false;
    QString deleteRefreshError;
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        QPointer<QMessageBox> messageBox = qobject_cast<QMessageBox *>(
            QApplication::activeModalWidget());
        if (!messageBox)
            return;
        deletePromptSeen = true;
        const Utils::Result<> refresh
            = projectService->setStartupConfiguration(file.projectId, file.slaveId, deleteRefresh);
        deleteRefreshApplied = bool(refresh);
        if (!refresh)
            deleteRefreshError = refresh.error();
        if (!messageBox)
            return;
        if (QAbstractButton *yes = messageBox->button(QMessageBox::Yes))
            yes->click();
    });
    remove->click();
    QTRY_VERIFY(deletePromptSeen);
    QVERIFY2(deleteRefreshApplied, qPrintable(deleteRefreshError));
    dialogWatchdog.stop();
    const Data::StartupConfiguration afterDeleteRefresh
        = projectService->project(file.projectId)->slaves.first().startup;
    QCOMPARE(afterDeleteRefresh.parameters.size(), deleteRefresh.parameters.size());
    QVERIFY(afterDeleteRefresh == deleteRefresh);
    QVERIFY(std::none_of(
        afterDeleteRefresh.parameters.cbegin(),
        afterDeleteRefresh.parameters.cend(),
        [&removedParameterId](const auto &parameter) {
            return parameter.id == removedParameterId;
        }));

    QPointer<QWidget> guardedPage(page);
    QPointer<QDialog> guardedDialog;
    bool removalDialogSeen = false;
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, page, [&] {
        guardedDialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!guardedDialog || guardedDialog->objectName() != "EtherCATStartupParameterDialog") {
            return;
        }
        removalDialogSeen = true;
        controller.selectionService()->clear();
    });
    add->click();
    QTRY_VERIFY(removalDialogSeen);
    QTRY_VERIFY(guardedPage.isNull());
    QTRY_VERIFY(guardedDialog.isNull());
    dialogWatchdog.stop();
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QVERIFY(projectService->project(file.projectId)->slaves.first().startup == deleteRefresh);

    controller.selectionService()->setCurrentNodeId(file.slaveId);
    QTRY_COMPARE(details.currentContext().nodeId, file.slaveId);
    QWidget *deletePage = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::STARTUP_PAGE_ID).toString());
    QVERIFY(deletePage);
    QTableView *deleteTable = deletePage->findChild<QTableView *>("EtherCATStartupTable");
    QPushButton *deleteButton = deletePage->findChild<QPushButton *>("EtherCATStartupDelete");
    QVERIFY(deleteTable);
    QVERIFY(deleteButton);
    deleteTable->setCurrentIndex(deleteTable->model()->index(0, 0));
    QTRY_VERIFY(deleteButton->isEnabled());
    QPointer<QWidget> guardedDeletePage(deletePage);
    QPointer<QMessageBox> guardedMessageBox;
    bool removalMessageSeen = false;
    dialogWatchdog.start(5000);
    QTimer::singleShot(0, deletePage, [&] {
        guardedMessageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!guardedMessageBox)
            return;
        removalMessageSeen = true;
        controller.selectionService()->clear();
    });
    deleteButton->click();
    QTRY_VERIFY(removalMessageSeen);
    QTRY_VERIFY(guardedDeletePage.isNull());
    QTRY_VERIFY(guardedMessageBox.isNull());
    dialogWatchdog.stop();
    QCOMPARE(details.currentContext().nodeKind, Core::WorkbenchNodeKind::None);
    QVERIFY(projectService->project(file.projectId)->slaves.first().startup == deleteRefresh);
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
    QTRY_VERIFY(addedFromDialog);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 4);

    constexpr int startupStableIdRole = Qt::UserRole + 1;
    const Data::NodeId firstParameterId
        = table->model()->index(0, 0).data(startupStableIdRole).value<Data::NodeId>();
    const Data::NodeId deletedParameterId
        = table->model()->index(1, 0).data(startupStableIdRole).value<Data::NodeId>();
    const Data::NodeId expectedNextParameterId
        = table->model()->index(2, 0).data(startupStableIdRole).value<Data::NodeId>();
    const Data::NodeId addedParameterId
        = table->model()->index(3, 0).data(startupStableIdRole).value<Data::NodeId>();
    table->setCurrentIndex(table->model()->index(1, 0));
    bool deletionConfirmed = false;
    QTimer::singleShot(0, page, [&deletionConfirmed] {
        QMessageBox *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!messageBox)
            return;
        deletionConfirmed = true;
        messageBox->button(QMessageBox::Yes)->click();
    });
    remove->click();
    QTRY_VERIFY(deletionConfirmed);
    QTRY_COMPARE(projectService->project(file.projectId)->slaves.first().startup.parameters.size(), 3);
    QTRY_COMPARE(
        table->currentIndex().data(startupStableIdRole).value<Data::NodeId>(),
        expectedNextParameterId);
    QList<Data::StartupParameterConfiguration> afterDelete
        = projectService->project(file.projectId)->slaves.first().startup.parameters;
    std::stable_sort(afterDelete.begin(), afterDelete.end(), [](const auto &left, const auto &right) {
        return left.order < right.order;
    });
    QCOMPARE(afterDelete.at(0).id, firstParameterId);
    QCOMPARE(afterDelete.at(1).id, expectedNextParameterId);
    QCOMPARE(afterDelete.at(2).id, addedParameterId);
    for (int row = 0; row < afterDelete.size(); ++row)
        QCOMPARE(afterDelete.at(row).order, row);
    QVERIFY(std::none_of(
        afterDelete.cbegin(), afterDelete.cend(), [&deletedParameterId](const auto &parameter) {
            return parameter.id == deletedParameterId;
        }));
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
    QTRY_VERIFY(editedFromDialog);
    QTRY_COMPARE(
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

void EtherCATWorkbenchTests::testDcRepositoryModePreview()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray previewEsi = deviceEsi();
    previewEsi.replace("#x00005678", "#x7A170001");
    previewEsi.replace("#x00000011", "#x0000A507");
    previewEsi.replace("AX5000", "EL-DC-PREVIEW");
    previewEsi.replace("Workbench Servo", "DC Preview Servo / 设备库预览");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .pathAppended("dc-repository-preview.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(previewEsi));
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles({esiPath}));
    QCOMPARE(importResult.requestedFiles, 1);
    QCOMPARE(importResult.importedDevices, 1);
    QCOMPARE(importResult.failedFiles, 0);
    QCOMPARE(importResult.affectedDeviceIds.size(), 1);

    const Data::NodeId deviceId = importResult.affectedDeviceIds.first();
    const std::optional<Data::DeviceDescription> supportedPreviewDevice
        = repository->device(deviceId);
    QVERIFY(supportedPreviewDevice);
    QVERIFY(supportedPreviewDevice->summary.supported);
    QCOMPARE(supportedPreviewDevice->dcModes.size(), 2);
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(deviceId).isValid());
    controller.selectionService()->setCurrentNodeId(deviceId);

    DetailsView details(&controller);
    details.resize(980, 720);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::DC_PAGE_ID).toString());
    QVERIFY(page);
    details.tabWidget()->setCurrentWidget(page);
    QTRY_VERIFY(page->isVisible());
    QLabel *summary = page->findChild<QLabel *>("EtherCATDcSummary");
    QLabel *validation = page->findChild<QLabel *>("EtherCATDcValidation");
    QComboBox *mode = page->findChild<QComboBox *>("EtherCATDcOperationMode");
    QCheckBox *enabled = page->findChild<QCheckBox *>("EtherCATDcEnabled");
    QLineEdit *assignActivate = page->findChild<QLineEdit *>("EtherCATDcAssignActivate");
    QCheckBox *sync0Enabled = page->findChild<QCheckBox *>("EtherCATDcSync0Enabled");
    QLineEdit *sync0Cycle = page->findChild<QLineEdit *>("EtherCATDcSync0CycleNs");
    QLineEdit *sync0Shift = page->findChild<QLineEdit *>("EtherCATDcSync0ShiftNs");
    QCheckBox *sync1Enabled = page->findChild<QCheckBox *>("EtherCATDcSync1Enabled");
    QLineEdit *sync1Cycle = page->findChild<QLineEdit *>("EtherCATDcSync1CycleNs");
    QLineEdit *sync1Shift = page->findChild<QLineEdit *>("EtherCATDcSync1ShiftNs");
    QCheckBox *referenceClock = page->findChild<QCheckBox *>(
        "EtherCATDcPotentialReferenceClock");
    QVERIFY(summary);
    QVERIFY(validation);
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

    QCOMPARE(mode->count(), 2);
    QCOMPARE(mode->currentText(), QString("Sync0"));
    QVERIFY(mode->isEnabled());
    QVERIFY(summary->text().contains("preview", Qt::CaseInsensitive));
    QVERIFY(summary->text().contains("Add the device to an offline project", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(mode->lineEdit()->isReadOnly());
    QVERIFY(!mode->accessibleDescription().isEmpty());
    QVERIFY(mode->accessibleDescription().contains("read-only", Qt::CaseInsensitive));
    QVERIFY(!mode->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!mode->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!enabled->isEnabled());
    QVERIFY(assignActivate->isReadOnly());
    QVERIFY(!sync0Enabled->isEnabled());
    QVERIFY(sync0Cycle->isReadOnly());
    QVERIFY(sync0Shift->isReadOnly());
    QVERIFY(!sync1Enabled->isEnabled());
    QVERIFY(sync1Cycle->isReadOnly());
    QVERIFY(sync1Shift->isReadOnly());
    QVERIFY(!referenceClock->isEnabled());

    mode->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(mode->hasFocus());
    QTest::keyClick(mode, Qt::Key_Down);
    QCOMPARE(mode->currentText(), QString("Sync0 + Sync1"));
    QVERIFY(enabled->isChecked());
    QCOMPARE(assignActivate->text(), QString("0x0700"));
    QVERIFY(sync0Enabled->isChecked());
    QCOMPARE(sync0Cycle->text(), QString("250000"));
    QCOMPARE(sync0Shift->text(), QString("-1000"));
    QVERIFY(sync1Enabled->isChecked());
    QCOMPARE(sync1Cycle->text(), QString("500000"));
    QCOMPARE(sync1Shift->text(), QString("1000"));
    QVERIFY(!referenceClock->isChecked());
    QVERIFY(projectService->projects().isEmpty());

    const QPointer<QComboBox> previousMode = mode;
    controller.selectionService()->clear();
    QTRY_VERIFY(previousMode.isNull());
    controller.selectionService()->setCurrentNodeId(deviceId);
    QComboBox *resetMode = nullptr;
    QTRY_VERIFY((resetMode = details.findChild<QComboBox *>("EtherCATDcOperationMode")));
    QTRY_COMPARE(resetMode->currentText(), QString("Sync0"));
    QVERIFY(resetMode->isEnabled());
    QVERIFY(resetMode->lineEdit()->isReadOnly());
    QVERIFY(projectService->projects().isEmpty());
    controller.selectionService()->clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void EtherCATWorkbenchTests::testDcRepositoryModeEmptyState()
{
    WorkbenchController controller;
    Core::DeviceRepositoryProvider *repository = controller.deviceRepository();
    Core::ProjectService *projectService = controller.projectService();
    QVERIFY(repository);
    QVERIFY(projectService);
    QVERIFY(projectService->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray emptyDcEsi = deviceEsi();
    const qsizetype dcStart = emptyDcEsi.indexOf("<Dc>");
    const qsizetype dcEnd = emptyDcEsi.indexOf("</Dc>", dcStart);
    QVERIFY(dcStart >= 0);
    QVERIFY(dcEnd > dcStart);
    emptyDcEsi.remove(dcStart, dcEnd + QByteArray("</Dc>").size() - dcStart);
    emptyDcEsi.replace("#x00005678", "#x7A170002");
    emptyDcEsi.replace("#x00000011", "#x0000A508");
    emptyDcEsi.replace("AX5000", "EL-DC-EMPTY");
    emptyDcEsi.replace("Workbench Servo", "DC Empty Servo / 无 DC 模式");
    const Utils::FilePath esiPath = Utils::FilePath::fromString(directory.path())
                                        .pathAppended("dc-repository-empty.xml");
    QVERIFY_RESULT(esiPath.writeFileContents(emptyDcEsi));
    const Data::DeviceImportResult importResult = waitForJob(repository->importFiles({esiPath}));
    QCOMPARE(importResult.requestedFiles, 1);
    QCOMPARE(importResult.importedDevices, 1);
    QCOMPARE(importResult.failedFiles, 0);
    QCOMPARE(importResult.affectedDeviceIds.size(), 1);

    const Data::NodeId deviceId = importResult.affectedDeviceIds.first();
    const std::optional<Data::DeviceDescription> supportedEmptyDevice
        = repository->device(deviceId);
    QVERIFY(supportedEmptyDevice);
    QVERIFY(supportedEmptyDevice->summary.supported);
    QVERIFY(supportedEmptyDevice->dcModes.isEmpty());
    QTRY_VERIFY(controller.treeModel()->indexForNodeId(deviceId).isValid());
    controller.selectionService()->setCurrentNodeId(deviceId);

    DetailsView details(&controller);
    details.resize(980, 720);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QWidget *page = details.findChild<QWidget *>(
        "EtherCATWorkbenchPropertyPage_" + Utils::Id(Constants::DC_PAGE_ID).toString());
    QVERIFY(page);
    details.tabWidget()->setCurrentWidget(page);
    QTRY_VERIFY(page->isVisible());
    QLabel *summary = page->findChild<QLabel *>("EtherCATDcSummary");
    QLabel *validation = page->findChild<QLabel *>("EtherCATDcValidation");
    QComboBox *mode = page->findChild<QComboBox *>("EtherCATDcOperationMode");
    QCheckBox *enabled = page->findChild<QCheckBox *>("EtherCATDcEnabled");
    QLineEdit *assignActivate = page->findChild<QLineEdit *>("EtherCATDcAssignActivate");
    QCheckBox *sync0Enabled = page->findChild<QCheckBox *>("EtherCATDcSync0Enabled");
    QLineEdit *sync0Cycle = page->findChild<QLineEdit *>("EtherCATDcSync0CycleNs");
    QLineEdit *sync0Shift = page->findChild<QLineEdit *>("EtherCATDcSync0ShiftNs");
    QCheckBox *sync1Enabled = page->findChild<QCheckBox *>("EtherCATDcSync1Enabled");
    QLineEdit *sync1Cycle = page->findChild<QLineEdit *>("EtherCATDcSync1CycleNs");
    QLineEdit *sync1Shift = page->findChild<QLineEdit *>("EtherCATDcSync1ShiftNs");
    QCheckBox *referenceClock = page->findChild<QCheckBox *>(
        "EtherCATDcPotentialReferenceClock");
    QVERIFY(summary);
    QVERIFY(validation);
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

    QVERIFY2(
        summary->text().contains(
            "No ESI Distributed Clocks operation mode", Qt::CaseInsensitive),
        qPrintable(summary->text()));
    QVERIFY(summary->text().contains("Add the device to an offline Project"));
    QVERIFY(!summary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(!summary->text().contains("Select an operation mode", Qt::CaseInsensitive));
    QVERIFY(validation->text().contains(
        "No ESI Distributed Clocks operation mode", Qt::CaseInsensitive));
    QVERIFY(!validation->text().contains("configuration is valid", Qt::CaseInsensitive));
    QCOMPARE(mode->count(), 0);
    QCOMPARE(mode->currentIndex(), -1);
    QVERIFY(!mode->isEnabled());
    QVERIFY(mode->lineEdit()->isReadOnly());
    QVERIFY(mode->accessibleDescription().contains(
        "No ESI Distributed Clocks operation mode", Qt::CaseInsensitive));
    QVERIFY(mode->accessibleDescription().contains("offline Project", Qt::CaseInsensitive));
    QVERIFY(mode->accessibleDescription().contains("manual timing", Qt::CaseInsensitive));
    QVERIFY(!mode->accessibleDescription().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(!mode->accessibleDescription().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(mode->accessibleDescription().contains("controller", Qt::CaseInsensitive));
    QVERIFY(mode->accessibleDescription().contains("network", Qt::CaseInsensitive));
    QVERIFY(mode->accessibleDescription().contains("physical hardware", Qt::CaseInsensitive));
    QCOMPARE(mode->toolTip(), mode->accessibleDescription());
    QVERIFY(!enabled->isEnabled());
    QVERIFY(assignActivate->isReadOnly());
    QVERIFY(!sync0Enabled->isEnabled());
    QVERIFY(sync0Cycle->isReadOnly());
    QVERIFY(sync0Shift->isReadOnly());
    QVERIFY(!sync1Enabled->isEnabled());
    QVERIFY(sync1Cycle->isReadOnly());
    QVERIFY(sync1Shift->isReadOnly());
    QVERIFY(!referenceClock->isEnabled());
    QVERIFY(projectService->projects().isEmpty());

    const QPointer<QComboBox> previousMode = mode;
    controller.selectionService()->clear();
    QTRY_VERIFY(previousMode.isNull());
    controller.selectionService()->setCurrentNodeId(deviceId);
    QComboBox *resetMode = nullptr;
    QTRY_VERIFY((resetMode = details.findChild<QComboBox *>("EtherCATDcOperationMode")));
    QLabel *resetSummary = details.findChild<QLabel *>("EtherCATDcSummary");
    QLabel *resetValidation = details.findChild<QLabel *>("EtherCATDcValidation");
    QVERIFY(resetSummary);
    QVERIFY(resetValidation);
    QVERIFY(resetSummary->text().contains(
        "No ESI Distributed Clocks operation mode", Qt::CaseInsensitive));
    QVERIFY(resetValidation->text().contains(
        "No ESI Distributed Clocks operation mode", Qt::CaseInsensitive));
    QCOMPARE(resetMode->count(), 0);
    QVERIFY(!resetMode->isEnabled());
    QVERIFY(resetMode->lineEdit()->isReadOnly());
    QVERIFY(projectService->projects().isEmpty());
    controller.selectionService()->clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QByteArray unsupportedEmptyDcEsi = emptyDcEsi;
    unsupportedEmptyDcEsi.replace("#x7A170002", "#x7A170003");
    unsupportedEmptyDcEsi.replace("#x0000A508", "#x0000A509");
    unsupportedEmptyDcEsi.replace("EL-DC-EMPTY", "EL-DC-LIMITED");
    unsupportedEmptyDcEsi.replace(
        "DC Empty Servo / 无 DC 模式", "DC Limited Servo / 不支持 DC 空态");
    unsupportedEmptyDcEsi.replace("</Device>", "<Modules/></Device>");
    const Utils::FilePath unsupportedEsiPath = Utils::FilePath::fromString(directory.path())
                                                   .pathAppended("dc-repository-limited.xml");
    QVERIFY_RESULT(unsupportedEsiPath.writeFileContents(unsupportedEmptyDcEsi));
    const Data::DeviceImportResult unsupportedImportResult
        = waitForJob(repository->importFiles({unsupportedEsiPath}));
    QCOMPARE(unsupportedImportResult.requestedFiles, 1);
    QCOMPARE(unsupportedImportResult.importedDevices, 1);
    QCOMPARE(unsupportedImportResult.failedFiles, 0);
    QCOMPARE(unsupportedImportResult.affectedDeviceIds.size(), 1);

    const Data::NodeId unsupportedDeviceId
        = unsupportedImportResult.affectedDeviceIds.first();
    const std::optional<Data::DeviceDescription> unsupportedDevice
        = repository->device(unsupportedDeviceId);
    QVERIFY(unsupportedDevice);
    QVERIFY(!unsupportedDevice->summary.supported);
    QVERIFY(unsupportedDevice->dcModes.isEmpty());
    controller.selectionService()->setCurrentNodeId(unsupportedDeviceId);
    QComboBox *unsupportedMode = nullptr;
    QTRY_VERIFY((unsupportedMode = details.findChild<QComboBox *>(
                     "EtherCATDcOperationMode")));
    QLabel *unsupportedSummary = details.findChild<QLabel *>("EtherCATDcSummary");
    QLabel *unsupportedValidation = details.findChild<QLabel *>("EtherCATDcValidation");
    QVERIFY(unsupportedSummary);
    QVERIFY(unsupportedValidation);
    QVERIFY(unsupportedSummary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(unsupportedSummary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedSummary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(unsupportedValidation->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(unsupportedValidation->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedValidation->text().contains("Device Repository", Qt::CaseInsensitive));
    QCOMPARE(unsupportedMode->count(), 0);
    QCOMPARE(unsupportedMode->currentIndex(), -1);
    QVERIFY(!unsupportedMode->isEnabled());
    QVERIFY(unsupportedMode->lineEdit()->isReadOnly());
    QVERIFY(unsupportedMode->accessibleDescription().contains(
        "unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(unsupportedMode->accessibleDescription().contains(
        "cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedMode->accessibleDescription().contains(
        "Device Repository", Qt::CaseInsensitive));
    QVERIFY(unsupportedMode->accessibleDescription().contains("controller", Qt::CaseInsensitive));
    QVERIFY(unsupportedMode->accessibleDescription().contains("network", Qt::CaseInsensitive));
    QVERIFY(unsupportedMode->accessibleDescription().contains(
        "physical hardware", Qt::CaseInsensitive));
    QCOMPARE(unsupportedMode->toolTip(), unsupportedMode->accessibleDescription());
    QVERIFY(projectService->projects().isEmpty());
    controller.selectionService()->clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QByteArray unsupportedPreviewEsi = deviceEsi();
    unsupportedPreviewEsi.replace("#x00005678", "#x7A170004");
    unsupportedPreviewEsi.replace("#x00000011", "#x0000A50A");
    unsupportedPreviewEsi.replace("AX5000", "EL-DC-LIMITED-PREVIEW");
    unsupportedPreviewEsi.replace(
        "Workbench Servo", "DC Limited Preview Servo / 不支持 DC 预览");
    unsupportedPreviewEsi.replace(
        "<CycleTimeSync1>500000</CycleTimeSync1>",
        "<CycleTimeSync1>4294967296</CycleTimeSync1>");
    unsupportedPreviewEsi.replace("</Device>", "<Modules/></Device>");
    const Utils::FilePath unsupportedPreviewEsiPath
        = Utils::FilePath::fromString(directory.path())
              .pathAppended("dc-repository-limited-preview.xml");
    QVERIFY_RESULT(unsupportedPreviewEsiPath.writeFileContents(unsupportedPreviewEsi));
    const Data::DeviceImportResult unsupportedPreviewImportResult
        = waitForJob(repository->importFiles({unsupportedPreviewEsiPath}));
    QCOMPARE(unsupportedPreviewImportResult.requestedFiles, 1);
    QCOMPARE(unsupportedPreviewImportResult.importedDevices, 1);
    QCOMPARE(unsupportedPreviewImportResult.failedFiles, 0);
    QCOMPARE(unsupportedPreviewImportResult.affectedDeviceIds.size(), 1);

    const Data::NodeId unsupportedPreviewDeviceId
        = unsupportedPreviewImportResult.affectedDeviceIds.first();
    const std::optional<Data::DeviceDescription> unsupportedPreviewDevice
        = repository->device(unsupportedPreviewDeviceId);
    QVERIFY(unsupportedPreviewDevice);
    QVERIFY(!unsupportedPreviewDevice->summary.supported);
    QCOMPARE(unsupportedPreviewDevice->dcModes.size(), 2);
    controller.selectionService()->setCurrentNodeId(unsupportedPreviewDeviceId);
    QComboBox *unsupportedPreviewMode = nullptr;
    QTRY_VERIFY((unsupportedPreviewMode = details.findChild<QComboBox *>(
                     "EtherCATDcOperationMode")));
    QLabel *unsupportedPreviewSummary = details.findChild<QLabel *>("EtherCATDcSummary");
    QLabel *unsupportedPreviewValidation = details.findChild<QLabel *>("EtherCATDcValidation");
    QVERIFY(unsupportedPreviewSummary);
    QVERIFY(unsupportedPreviewValidation);
    QVERIFY(unsupportedPreviewSummary->text().contains("read-only preview", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewSummary->text().contains("unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewSummary->text().contains("cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewSummary->text().contains(
        "Device Repository", Qt::CaseInsensitive));
    QVERIFY(!unsupportedPreviewSummary->text().contains(
        "Add the device to an offline project", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewValidation->text().contains(
        "read-only preview", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewValidation->text().contains(
        "unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewValidation->text().contains(
        "cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewValidation->text().contains(
        "Device Repository", Qt::CaseInsensitive));
    QCOMPARE(unsupportedPreviewMode->count(), 2);
    QCOMPARE(unsupportedPreviewMode->currentIndex(), 0);
    QVERIFY(unsupportedPreviewMode->isEnabled());
    QVERIFY(unsupportedPreviewMode->lineEdit()->isReadOnly());
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "read-only", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "unsupported ESI", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "Device Repository", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "controller", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "network", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewMode->accessibleDescription().contains(
        "physical hardware", Qt::CaseInsensitive));
    QCOMPARE(unsupportedPreviewMode->toolTip(), unsupportedPreviewMode->accessibleDescription());
    unsupportedPreviewMode->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(unsupportedPreviewMode->hasFocus());
    QTest::keyClick(unsupportedPreviewMode, Qt::Key_Down);
    QCOMPARE(unsupportedPreviewMode->currentIndex(), 1);
    QTRY_VERIFY(unsupportedPreviewValidation->text().contains(
        "configuration error", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewValidation->text().contains(
        "cannot be added", Qt::CaseInsensitive));
    QVERIFY(unsupportedPreviewValidation->text().contains(
        "Device Repository", Qt::CaseInsensitive));
    QVERIFY(projectService->projects().isEmpty());
    controller.selectionService()->clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    BuiltinPropertyPageProvider provider(&controller);
    const Core::PropertyPageContext missingContext{
        {}, Data::NodeId::create(), Core::WorkbenchNodeKind::Device, "Removed ESI device"};
    std::unique_ptr<QWidget> missingPage(
        provider.createPage(Constants::DC_PAGE_ID, nullptr));
    QVERIFY(missingPage);
    provider.updatePage(Constants::DC_PAGE_ID, missingPage.get(), missingContext);
    QLabel *missingSummary = missingPage->findChild<QLabel *>("EtherCATDcSummary");
    QLabel *missingValidation = missingPage->findChild<QLabel *>("EtherCATDcValidation");
    QComboBox *missingMode = missingPage->findChild<QComboBox *>("EtherCATDcOperationMode");
    QVERIFY(missingSummary);
    QVERIFY(missingValidation);
    QVERIFY(missingMode);
    QVERIFY(missingSummary->text().contains("no longer available", Qt::CaseInsensitive));
    QVERIFY(missingSummary->text().contains("Device Repository", Qt::CaseInsensitive));
    QVERIFY(!missingSummary->text().contains("offline Project", Qt::CaseInsensitive));
    QVERIFY(missingValidation->text().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(missingMode->count(), 0);
    QCOMPARE(missingMode->currentIndex(), -1);
    QVERIFY(!missingMode->isEnabled());
    QVERIFY(missingMode->lineEdit()->isReadOnly());
    QVERIFY(missingMode->accessibleDescription().contains("unavailable", Qt::CaseInsensitive));
    QVERIFY(missingMode->accessibleDescription().contains(
        "Device Repository", Qt::CaseInsensitive));
    QVERIFY(missingMode->accessibleDescription().contains("controller", Qt::CaseInsensitive));
    QVERIFY(missingMode->accessibleDescription().contains("network", Qt::CaseInsensitive));
    QVERIFY(missingMode->accessibleDescription().contains(
        "physical hardware", Qt::CaseInsensitive));
    QCOMPARE(missingMode->toolTip(), missingMode->accessibleDescription());
    QVERIFY(projectService->projects().isEmpty());
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
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(details.isVisible());
    const int baselinePages = details.tabWidget()->count();
    QVERIFY(baselinePages >= 1);
    QWidget *baselineFirstPage = details.tabWidget()->widget(0);
    QVERIFY(baselineFirstPage);
    const QString baselineFirstKey = baselineFirstPage->property("EtherCAT.PageKey").toString();
    QVERIFY(!baselineFirstKey.isEmpty());

    const Utils::Id providerAId("EtherCAT.Workbench.TestPages.A");
    const Utils::Id providerAPageId("EtherCAT.Workbench.TestPage.A");
    const Utils::Id providerBId("EtherCAT.Workbench.TestPages.B");
    const Utils::Id providerBPageId("EtherCAT.Workbench.TestPage.B");
    TestPageProvider
        providerA(providerAId, providerAPageId, "Provider A Page", "EtherCATDynamicTestPageA", 150);
    TestPageProvider
        providerB(providerBId, providerBPageId, "Provider B Page", "EtherCATDynamicTestPageB", 160);
    bool providerARegistered = false;
    bool providerBRegistered = false;
    const QScopeGuard cleanup([&] {
        if (providerBRegistered)
            ExtensionSystem::PluginManager::removeObject(&providerB);
        if (providerARegistered)
            ExtensionSystem::PluginManager::removeObject(&providerA);
        controller.selectionService()->clear();
    });

    ExtensionSystem::PluginManager::addObject(&providerA);
    providerARegistered = true;
    ExtensionSystem::PluginManager::addObject(&providerB);
    providerBRegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QVERIFY(details.findChild<QWidget *>("EtherCATDynamicTestPageA"));
    QVERIFY(details.findChild<QWidget *>("EtherCATDynamicTestPageB"));

    const QString providerBKey = providerBId.toString() + '/' + providerBPageId.toString();
    const auto pageIndexForKey = [&details](const QString &pageKey) {
        for (int index = 0; index < details.tabWidget()->count(); ++index) {
            if (details.tabWidget()->widget(index)->property("EtherCAT.PageKey").toString()
                == pageKey) {
                return index;
            }
        }
        return -1;
    };
    const auto currentPageKey = [&details] {
        QWidget *currentPage = details.tabWidget()->currentWidget();
        return currentPage ? currentPage->property("EtherCAT.PageKey").toString() : QString();
    };
    const Data::NodeId stableContextId = details.currentContext().nodeId;
    QCOMPARE(stableContextId, project.id);

    const int stablePageIndex = pageIndexForKey(baselineFirstKey);
    QVERIFY(stablePageIndex >= 0);
    details.tabWidget()->setCurrentIndex(stablePageIndex);
    QWidget *stablePage = details.tabWidget()->currentWidget();
    QVERIFY(stablePage);
    QLineEdit *stableFocusField
        = stablePage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(stableFocusField);
    stableFocusField->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), stableFocusField);
    ExtensionSystem::PluginManager::removeObject(&providerA);
    providerARegistered = false;
    ExtensionSystem::PluginManager::removeObject(&providerB);
    providerBRegistered = false;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages);
    QTRY_COMPARE(currentPageKey(), baselineFirstKey);
    stablePage = details.tabWidget()->currentWidget();
    QVERIFY(stablePage);
    QLineEdit *restoredStableFocusField
        = stablePage->findChild<QLineEdit *>("EtherCATProjectGeneralCreatedBy");
    QVERIFY(restoredStableFocusField);
    QTRY_COMPARE(QApplication::focusWidget(), restoredStableFocusField);

    ExtensionSystem::PluginManager::addObject(&providerA);
    providerARegistered = true;
    ExtensionSystem::PluginManager::addObject(&providerB);
    providerBRegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    const int providerBIndex = pageIndexForKey(providerBKey);
    QVERIFY(providerBIndex >= 0);
    details.tabWidget()->setCurrentIndex(providerBIndex);
    QCOMPARE(currentPageKey(), providerBKey);

    int repeatedRefreshUpdates = 0;
    providerA.onEveryUpdate = [&] {
        ++repeatedRefreshUpdates;
        emit controller.diagnosticsProviderChanged(false);
    };
    emit controller.diagnosticsProviderChanged(false);
    QCOMPARE(repeatedRefreshUpdates, 1);
    providerA.onEveryUpdate = {};

    int repeatedRebuildUpdates = 0;
    QWidget *repeatedRebuildFocusPage = details.tabWidget()->currentWidget();
    QVERIFY(repeatedRebuildFocusPage);
    repeatedRebuildFocusPage->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), repeatedRebuildFocusPage);
    providerA.onEveryUpdate = [&] {
        ++repeatedRebuildUpdates;
        if (repeatedRebuildUpdates < 20)
            providerB.setAvailable(!providerB.isAvailable());
    };
    providerA.setAvailable(false);
    providerA.setAvailable(true);
    QVERIFY(repeatedRebuildUpdates > 0);
    QVERIFY(repeatedRebuildUpdates < 20);
    QVERIFY(QApplication::focusWidget());
    QVERIFY(details.tabWidget()->isAncestorOf(QApplication::focusWidget()));
    QTabBar *yieldedTabBar = details.tabWidget()->tabBar();
    QVERIFY(yieldedTabBar);
    yieldedTabBar->setFocus(Qt::OtherFocusReason);
    QCOMPARE(QApplication::focusWidget(), yieldedTabBar);
    QPointer<QWidget> explicitYieldTabFocus = QApplication::focusWidget();
    const QString yieldedUserPageKey = baselineFirstKey;
    const int yieldedUserPageIndex = pageIndexForKey(yieldedUserPageKey);
    QVERIFY(yieldedUserPageIndex >= 0);
    QTest::mouseClick(
        yieldedTabBar,
        Qt::LeftButton,
        Qt::NoModifier,
        yieldedTabBar->tabRect(yieldedUserPageIndex).center());
    QCOMPARE(currentPageKey(), yieldedUserPageKey);
    QCOMPARE(QApplication::focusWidget(), explicitYieldTabFocus.data());
    providerA.onEveryUpdate = {};
    providerB.setAvailable(true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QTRY_COMPARE(currentPageKey(), yieldedUserPageKey);
    QTRY_COMPARE(QApplication::focusWidget(), explicitYieldTabFocus.data());
    const int providerBAfterYieldIndex = pageIndexForKey(providerBKey);
    QVERIFY(providerBAfterYieldIndex >= 0);
    details.tabWidget()->setCurrentIndex(providerBAfterYieldIndex);
    QTRY_COMPARE(currentPageKey(), providerBKey);

    providerA.setAvailable(false);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QTRY_COMPARE(currentPageKey(), providerBKey);
    providerA.setAvailable(true);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QTRY_COMPARE(currentPageKey(), providerBKey);

    QPointer<QWidget> nestedClearProviderAPage
        = details.findChild<QWidget *>("EtherCATDynamicTestPageA");
    QPointer<QWidget> nestedClearProviderBPage
        = details.findChild<QWidget *>("EtherCATDynamicTestPageB");
    QVERIFY(nestedClearProviderAPage);
    QVERIFY(nestedClearProviderBPage);
    bool nestedClearRemovalObserved = false;
    bool nestedClearPageDestroyedBeforeReturn = false;
    connect(nestedClearProviderAPage, &QObject::destroyed, &details, [&] {
        nestedClearRemovalObserved = true;
        ExtensionSystem::PluginManager::removeObject(&providerB);
        providerBRegistered = false;
        nestedClearPageDestroyedBeforeReturn = nestedClearProviderBPage.isNull();
    });
    providerA.setAvailable(false);
    QVERIFY(nestedClearRemovalObserved);
    QVERIFY(nestedClearPageDestroyedBeforeReturn);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages);
    providerA.setAvailable(true);
    ExtensionSystem::PluginManager::addObject(&providerB);
    providerBRegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    const int providerBAfterNestedClearIndex = pageIndexForKey(providerBKey);
    QVERIFY(providerBAfterNestedClearIndex >= 0);
    details.tabWidget()->setCurrentIndex(providerBAfterNestedClearIndex);
    QTRY_COMPARE(currentPageKey(), providerBKey);

    QPointer<QWidget> pageUpdatedDuringRemoval
        = details.findChild<QWidget *>("EtherCATDynamicTestPageA");
    QPointer<QWidget> pageOwnedByRemovedProvider
        = details.findChild<QWidget *>("EtherCATDynamicTestPageB");
    QVERIFY(pageUpdatedDuringRemoval);
    QVERIFY(pageOwnedByRemovedProvider);
    bool updateCallbackActive = false;
    bool pageDestroyedInUpdateCallback = false;
    bool pageAliveAfterRemovalRequest = false;
    bool removedPageDestroyedBeforeReturn = false;
    connect(pageUpdatedDuringRemoval, &QObject::destroyed, &details, [&] {
        if (updateCallbackActive)
            pageDestroyedInUpdateCallback = true;
    });
    bool removalDuringUpdateObserved = false;
    bool removedProviderUpdated = false;
    providerA.onUpdate = [&] {
        removalDuringUpdateObserved = true;
        updateCallbackActive = true;
        providerB.onUpdate = [&] { removedProviderUpdated = true; };
        ExtensionSystem::PluginManager::removeObject(&providerB);
        providerBRegistered = false;
        pageAliveAfterRemovalRequest = !pageUpdatedDuringRemoval.isNull();
        removedPageDestroyedBeforeReturn = pageOwnedByRemovedProvider.isNull();
        if (pageUpdatedDuringRemoval)
            pageUpdatedDuringRemoval->setProperty("UpdateContinuedAfterReentry", true);
        updateCallbackActive = false;
    };
    emit controller.diagnosticsProviderChanged(false);
    QVERIFY(removalDuringUpdateObserved);
    QVERIFY(pageAliveAfterRemovalRequest);
    QVERIFY(removedPageDestroyedBeforeReturn);
    QVERIFY(pageOwnedByRemovedProvider.isNull());
    QVERIFY(!pageDestroyedInUpdateCallback);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_VERIFY(pageUpdatedDuringRemoval.isNull());
    QVERIFY(!removedProviderUpdated);
    QCOMPARE(details.tabWidget()->count(), baselinePages + 1);
    QVERIFY(details.findChild<QWidget *>("EtherCATDynamicTestPageA"));
    QVERIFY(!details.findChild<QWidget *>("EtherCATDynamicTestPageB"));
    providerB.onUpdate = {};
    ExtensionSystem::PluginManager::addObject(&providerB);
    providerBRegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    const int restoredProviderBIndex = pageIndexForKey(providerBKey);
    QVERIFY(restoredProviderBIndex >= 0);
    details.tabWidget()->setCurrentIndex(restoredProviderBIndex);
    QTRY_COMPARE(currentPageKey(), providerBKey);

    QPointer<QWidget> selfRemovingProviderPage
        = details.findChild<QWidget *>("EtherCATDynamicTestPageA");
    QVERIFY(selfRemovingProviderPage);
    bool selfRemovalCallbackActive = false;
    bool selfRemovalObserved = false;
    bool selfPageAliveAfterRemoveObject = false;
    bool selfPageDestroyedInsideCallback = false;
    connect(selfRemovingProviderPage, &QObject::destroyed, &details, [&] {
        if (selfRemovalCallbackActive)
            selfPageDestroyedInsideCallback = true;
    });
    providerA.onUpdate = [&] {
        selfRemovalObserved = true;
        selfRemovalCallbackActive = true;
        ExtensionSystem::PluginManager::removeObject(&providerA);
        providerARegistered = false;
        selfPageAliveAfterRemoveObject = !selfRemovingProviderPage.isNull();
        selfRemovalCallbackActive = false;
    };
    emit controller.diagnosticsProviderChanged(false);
    QVERIFY(selfRemovalObserved);
    QVERIFY(selfPageAliveAfterRemoveObject);
    QVERIFY(!selfPageDestroyedInsideCallback);
    QVERIFY(selfRemovingProviderPage.isNull());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QTRY_COMPARE(currentPageKey(), providerBKey);
    ExtensionSystem::PluginManager::addObject(&providerA);
    providerARegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QTRY_COMPARE(currentPageKey(), providerBKey);

    QWidget *providerBPage = details.tabWidget()->currentWidget();
    QLabel *detailsTitle = details.findChild<QLabel *>("EtherCATWorkbenchDetailsTitle");
    QVERIFY(providerBPage);
    QVERIFY(detailsTitle);
    providerBPage->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), providerBPage);
    QPointer<QWidget> destroyedProviderBPage = providerBPage;
    QWidget *providerAPage
        = details.findChild<QWidget *>("EtherCATDynamicTestPageA");
    QVERIFY(providerAPage);
    bool removalClearReentryObserved = false;
    connect(providerAPage, &QObject::destroyed, &details, [&] {
        removalClearReentryObserved = true;
        providerB.setAvailable(false);
        providerB.setAvailable(true);
    });
    ExtensionSystem::PluginManager::removeObject(&providerA);
    providerARegistered = false;
    QTRY_VERIFY(removalClearReentryObserved);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QTRY_COMPARE(currentPageKey(), providerBKey);
    QTRY_VERIFY(destroyedProviderBPage.isNull());
    QWidget *restoredProviderBPage
        = details.findChild<QWidget *>("EtherCATDynamicTestPageB");
    QVERIFY(restoredProviderBPage);
    QTRY_COMPARE(QApplication::focusWidget(), restoredProviderBPage);

    ExtensionSystem::PluginManager::addObject(&providerA);
    providerARegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QTRY_COMPARE(currentPageKey(), providerBKey);
    restoredProviderBPage = details.findChild<QWidget *>("EtherCATDynamicTestPageB");
    QVERIFY(restoredProviderBPage);
    restoredProviderBPage->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), restoredProviderBPage);
    bool externalFocusSet = false;
    const QMetaObject::Connection externalFocusConnection = connect(
        controller.providerRegistry(),
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        &details,
        [&](Core::Provider *departingProvider) {
            if (departingProvider == &providerA) {
                externalFocusSet = true;
                detailsTitle->setFocus(Qt::OtherFocusReason);
            }
        },
        Qt::DirectConnection);
    ExtensionSystem::PluginManager::removeObject(&providerA);
    providerARegistered = false;
    disconnect(externalFocusConnection);
    QVERIFY(externalFocusSet);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QTRY_COMPARE(currentPageKey(), providerBKey);
    QTRY_COMPARE(QApplication::focusWidget(), detailsTitle);
    QCOMPARE(details.currentContext().nodeId, stableContextId);
    QCOMPARE(controller.selectionService()->currentNodeId(), stableContextId);
    QVERIFY(!details.findChild<QWidget *>("EtherCATDynamicTestPageA"));
    QVERIFY(details.findChild<QWidget *>("EtherCATDynamicTestPageB"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QTRY_COMPARE(QApplication::focusWidget(), detailsTitle);

    QPointer<QWidget> stableProviderBPage = details.findChild<QWidget *>(
        "EtherCATDynamicTestPageB");
    QVERIFY(stableProviderBPage);
    providerA.setAvailable(false);
    providerA.setAvailable(true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(stableProviderBPage);
    QCOMPARE(details.findChild<QWidget *>("EtherCATDynamicTestPageB"), stableProviderBPage.data());
    QCOMPARE(currentPageKey(), providerBKey);

    ExtensionSystem::PluginManager::addObject(&providerA);
    providerARegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QTRY_COMPARE(currentPageKey(), providerBKey);

    const Data::NodeId projectMasterId = masterId(project);
    QVERIFY(!projectMasterId.isNull());
    bool reentrantRemovalObserved = false;
    connect(
        controller.providerRegistry(),
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        &details,
        [&](Core::Provider *departingProvider) {
            if (departingProvider != &providerA)
                return;
            reentrantRemovalObserved = true;
            controller.selectionService()->setCurrentNodeId(projectMasterId);
            controller.selectionService()->setCurrentNodeId(project.id);
            const int firstPageIndex = pageIndexForKey(baselineFirstKey);
            if (firstPageIndex >= 0)
                details.tabWidget()->setCurrentIndex(firstPageIndex);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        },
        Qt::DirectConnection);
    ExtensionSystem::PluginManager::removeObject(&providerA);
    providerARegistered = false;
    QVERIFY(reentrantRemovalObserved);
    QTRY_VERIFY(!details.findChild<QWidget *>("EtherCATDynamicTestPageA"));
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QCOMPARE(details.currentContext().nodeId, stableContextId);
    QCOMPARE(controller.selectionService()->currentNodeId(), stableContextId);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 1);
    QCOMPARE(currentPageKey(), baselineFirstKey);
    QCOMPARE(details.currentContext().nodeId, stableContextId);
    QCOMPARE(controller.selectionService()->currentNodeId(), stableContextId);

    const int providerBAfterRemovalIndex = pageIndexForKey(providerBKey);
    QVERIFY(providerBAfterRemovalIndex >= 0);
    details.tabWidget()->setCurrentIndex(providerBAfterRemovalIndex);
    QCOMPARE(currentPageKey(), providerBKey);

    const QString renderPath = qEnvironmentVariable(
        "ETHERCAT_WORKBENCH_DETAILS_CONTINUITY_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    ExtensionSystem::PluginManager::addObject(&providerA);
    providerARegistered = true;
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages + 2);
    QPointer<QWidget> nestedRemovalPageB
        = details.findChild<QWidget *>("EtherCATDynamicTestPageB");
    QPointer<QWidget> nestedRemovalPageA
        = details.findChild<QWidget *>("EtherCATDynamicTestPageA");
    QVERIFY(nestedRemovalPageB);
    QVERIFY(nestedRemovalPageA);
    bool nestedRegistryRemovalObserved = false;
    bool nestedHideRemovalObserved = false;
    bool nestedHidePageDestroyedBeforeReturn = false;
    bool nestedHideDrainObserved = false;
    bool outerPageAliveAfterNestedDrain = false;
    bool nestedProviderPageDestroyedBeforeReturn = false;
    const QMetaObject::Connection nestedHideDrainConnection = connect(
        controller.providerRegistry(),
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        &details,
        [&](Core::Provider *departingProvider) {
            if (departingProvider != &providerA)
                return;
            nestedHideDrainObserved = true;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            outerPageAliveAfterNestedDrain = !nestedRemovalPageB.isNull();
        },
        Qt::DirectConnection);
    providerB.onHide = [&] {
        nestedHideRemovalObserved = true;
        if (providerARegistered) {
            ExtensionSystem::PluginManager::removeObject(&providerA);
            providerARegistered = false;
            nestedHidePageDestroyedBeforeReturn = nestedRemovalPageA.isNull();
        }
    };
    connect(nestedRemovalPageB, &QObject::destroyed, &details, [&] {
        nestedRegistryRemovalObserved = true;
        if (providerARegistered) {
            ExtensionSystem::PluginManager::removeObject(&providerA);
            providerARegistered = false;
        }
        nestedProviderPageDestroyedBeforeReturn = nestedRemovalPageA.isNull();
    });
    bool nestedSignalDrainObserved = false;
    bool nestedRemovalCompletedInsideSignal = false;
    const QMetaObject::Connection nestedSignalDrainConnection = connect(
        controller.providerRegistry(),
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        &details,
        [&](Core::Provider *departingProvider) {
            if (departingProvider != &providerB)
                return;
            nestedSignalDrainObserved = true;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            nestedRemovalCompletedInsideSignal = nestedRegistryRemovalObserved;
        },
        Qt::DirectConnection);
    ExtensionSystem::PluginManager::removeObject(&providerB);
    providerBRegistered = false;
    disconnect(nestedHideDrainConnection);
    disconnect(nestedSignalDrainConnection);
    QVERIFY(nestedSignalDrainObserved);
    QVERIFY(nestedRemovalCompletedInsideSignal);
    QVERIFY(nestedHideRemovalObserved);
    QVERIFY(nestedHidePageDestroyedBeforeReturn);
    QVERIFY(nestedHideDrainObserved);
    QVERIFY(outerPageAliveAfterNestedDrain);
    QVERIFY(nestedProviderPageDestroyedBeforeReturn);
    QTRY_VERIFY(nestedRegistryRemovalObserved);
    QTRY_COMPARE(details.tabWidget()->count(), baselinePages);
    QCOMPARE(details.tabWidget()->currentIndex(), 0);
    QCOMPARE(currentPageKey(), baselineFirstKey);
    QVERIFY(!controller.providerRegistry()->provider(providerAId));
    QVERIFY(!controller.providerRegistry()->provider(providerBId));
    QVERIFY(!details.findChild<QWidget *>("EtherCATDynamicTestPageA"));
    QVERIFY(!details.findChild<QWidget *>("EtherCATDynamicTestPageB"));
    QCOMPARE(details.currentContext().nodeId, stableContextId);
}

void EtherCATWorkbenchTests::testOptionalProviderAvailabilityPresentation()
{
    WorkbenchController controller;
    const Data::ProjectSnapshot project = projectSnapshot("Provider Availability");
    controller.treeModel()->setProjects({project});

    const QModelIndex master = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::Master);
    const QModelIndex diagnostics
        = findByKind(controller.treeModel(), Core::WorkbenchNodeKind::Diagnostics);
    QVERIFY(master.isValid());
    QVERIFY(diagnostics.isValid());
    const QModelIndex noSlaves = controller.treeModel()->index(1, 0, master);
    QVERIFY(noSlaves.isValid());
    QSignalSpy modelResetSpy(controller.treeModel(), &QAbstractItemModel::modelReset);

    const auto status = [](const QModelIndex &index) {
        return index.siblingAtColumn(1).data().toString();
    };
    QCOMPARE(status(diagnostics), QString("No Diagnostics Provider registered | Local Mock only"));
    QCOMPARE(status(noSlaves), QString("No Scan Provider registered | Local Mock only"));
    QVERIFY(!diagnostics.data(WorkbenchTreeModel::SearchTextRole)
                 .toString()
                 .contains("installed", Qt::CaseInsensitive));
    QVERIFY(!noSlaves.data(Qt::ToolTipRole)
                 .toString()
                 .contains("installed", Qt::CaseInsensitive));

    controller.selectionService()->setCurrentNodeId(
        diagnostics.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>());
    DetailsView details(&controller);
    details.resize(900, 600);
    details.show();
    QTRY_VERIFY(details.isVisible());
    QLabel *emptyState = details.findChild<QLabel *>("EtherCATWorkbenchEmptyState");
    QVERIFY(emptyState);
    QTRY_VERIFY(details.tabWidget()->count() > 0);
    QLabel *summary = details.findChild<QLabel *>("EtherCATWorkbenchPageSummary");
    QVERIFY(summary);
    QVERIFY(summary->text().contains("No Diagnostics Provider is registered"));
    QVERIFY(summary->text().contains("local Mock"));
    QVERIFY(!summary->text().contains("installed", Qt::CaseInsensitive));
    QCOMPARE(summary->accessibleDescription(), summary->text());

    AvailableScanProvider scan;
    AvailableScanProvider backupScan(
        Utils::Id("EtherCAT.Workbench.TestScan.Backup"),
        "Backup Local Mock test scanner");
    AvailableDiagnosticsProvider diagnosticsProvider;
    AvailableDiagnosticsProvider backupDiagnostics(
        Utils::Id("EtherCAT.Workbench.TestDiagnostics.Backup"),
        "Backup Local Mock diagnostics");
    scan.setDisplayName("Local Mock test scanner");
    diagnosticsProvider.setDisplayName("Local Mock test diagnostics");
    bool scanRegistered = false;
    bool backupScanRegistered = false;
    bool diagnosticsRegistered = false;
    bool backupDiagnosticsRegistered = false;
    const QScopeGuard cleanup([&] {
        if (backupDiagnosticsRegistered)
            ExtensionSystem::PluginManager::removeObject(&backupDiagnostics);
        if (diagnosticsRegistered)
            ExtensionSystem::PluginManager::removeObject(&diagnosticsProvider);
        if (backupScanRegistered)
            ExtensionSystem::PluginManager::removeObject(&backupScan);
        if (scanRegistered)
            ExtensionSystem::PluginManager::removeObject(&scan);
        controller.selectionService()->clear();
    });

    ExtensionSystem::PluginManager::addObject(&scan);
    scanRegistered = true;
    ExtensionSystem::PluginManager::addObject(&backupScan);
    backupScanRegistered = true;
    ExtensionSystem::PluginManager::addObject(&diagnosticsProvider);
    diagnosticsRegistered = true;
    ExtensionSystem::PluginManager::addObject(&backupDiagnostics);
    backupDiagnosticsRegistered = true;
    QTRY_COMPARE(status(noSlaves), QString("Local Mock test scanner unavailable"));
    QTRY_COMPARE(status(diagnostics), QString("Local Mock test diagnostics unavailable"));
    QTRY_VERIFY(details.tabWidget()->count() > 0);
    summary = details.findChild<QLabel *>("EtherCATWorkbenchPageSummary");
    QVERIFY(summary);
    QTRY_VERIFY(summary->text().contains("Local Mock test diagnostics"));
    QVERIFY(summary->text().contains("registered but unavailable"));
    QVERIFY(!summary->text().contains("installed", Qt::CaseInsensitive));
    QCOMPARE(summary->accessibleDescription(), summary->text());

    BuiltinPropertyPageProvider builtinPages(&controller);
    QWidget onlineOwner;
    QWidget *onlinePage = builtinPages.createPage(
        Utils::Id(Constants::ONLINE_PAGE_ID), &onlineOwner);
    QVERIFY(onlinePage);
    builtinPages.updatePage(
        Utils::Id(Constants::ONLINE_PAGE_ID),
        onlinePage,
        controller.treeModel()->contextForIndex(master));
    QLabel *onlineSummary
        = onlinePage->findChild<QLabel *>("EtherCATWorkbenchPageSummary");
    QVERIFY(onlineSummary);
    QVERIFY(onlineSummary->text().contains("Local Mock test diagnostics"));
    QVERIFY(onlineSummary->text().contains("unavailable", Qt::CaseInsensitive));
    QVERIFY(!onlineSummary->text().contains("Scan", Qt::CaseInsensitive));
    QVERIFY(!onlineSummary->text().contains("installed", Qt::CaseInsensitive));

    const QString renderPath
        = qEnvironmentVariable("ETHERCAT_WORKBENCH_PROVIDER_STATE_RENDER_PATH");
    if (!renderPath.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(details.grab().save(renderPath), qPrintable(renderPath));
    }

    QPointer<QLabel> retainedSummary(summary);
    scan.setDisplayName("  ");
    QTRY_COMPARE(status(noSlaves), QString("Unnamed Scan Provider unavailable"));
    QVERIFY(retainedSummary);
    QCOMPARE(details.findChild<QLabel *>("EtherCATWorkbenchPageSummary"), retainedSummary.data());
    scan.setDisplayName("Renamed Local Mock test scanner");
    QTRY_COMPARE(status(noSlaves), QString("Renamed Local Mock test scanner unavailable"));
    QVERIFY(retainedSummary);
    QCOMPARE(details.findChild<QLabel *>("EtherCATWorkbenchPageSummary"), retainedSummary.data());

    diagnosticsProvider.setDisplayName("\t");
    QTRY_COMPARE(status(diagnostics), QString("Unnamed Diagnostics Provider unavailable"));
    QTRY_VERIFY(retainedSummary);
    QTRY_VERIFY(retainedSummary->text().contains("Unnamed Diagnostics Provider"));
    QCOMPARE(details.findChild<QLabel *>("EtherCATWorkbenchPageSummary"), retainedSummary.data());
    diagnosticsProvider.setDisplayName("Renamed Local Mock diagnostics");
    QTRY_VERIFY(retainedSummary);
    QTRY_VERIFY(retainedSummary->text().contains("Renamed Local Mock diagnostics"));
    QCOMPARE(details.findChild<QLabel *>("EtherCATWorkbenchPageSummary"), retainedSummary.data());

    backupScan.setAvailable(true);
    QTRY_COMPARE(status(noSlaves), QString("Backup Local Mock test scanner available"));
    scan.setAvailable(true);
    QTRY_COMPARE(status(noSlaves), QString("Renamed Local Mock test scanner available"));

    Data::ScanResult backupScanResult;
    backupScanResult.snapshot.projectId = project.id;
    backupScanResult.snapshot.masterId = masterId(project);
    backupScanResult.snapshot.mock = true;
    backupScanResult.snapshot.complete = true;
    backupScanResult.comparison.projectId = project.id;
    backupScanResult.comparison.masterId = masterId(project);
    backupScanResult.comparison.differences = {
        {Data::TopologyDifferenceKind::Added,
         Data::DifferenceSeverity::Information,
         {},
         Data::NodeId::create(),
         -1,
         0,
         "Backup scan difference",
         "Local Mock backup result"},
    };
    backupScan.publishResult(backupScanResult);
    QTRY_COMPARE(
        controller.scanProviderPresentation().displayName,
        QString("Backup Local Mock test scanner"));
    QTRY_COMPARE(status(noSlaves), QString("Backup Local Mock test scanner available"));
    QTRY_VERIFY(status(master).contains("1 topology difference"));
    backupScan.clearPublishedResult();
    QTRY_COMPARE(
        controller.scanProviderPresentation().displayName,
        QString("Renamed Local Mock test scanner"));
    QTRY_VERIFY(!status(master).contains("topology difference"));

    backupDiagnostics.setAvailable(true);
    QTRY_COMPARE(status(diagnostics), QString("Backup Local Mock diagnostics available"));
    diagnosticsProvider.setAvailable(true);
    QTRY_COMPARE(status(diagnostics), QString("Renamed Local Mock diagnostics available"));
    QTRY_COMPARE(details.tabWidget()->count(), 0);
    QTRY_VERIFY(emptyState->isVisible());
    QTRY_VERIFY(emptyState->text().contains("Renamed Local Mock diagnostics"));
    QVERIFY(emptyState->text().contains("available", Qt::CaseInsensitive));
    QVERIFY(emptyState->text().contains("does not provide", Qt::CaseInsensitive));
    QVERIFY(!emptyState->text().contains("installed", Qt::CaseInsensitive));
    QCOMPARE(emptyState->accessibleDescription(), emptyState->text());

    backupDiagnostics.setDisplayName("Backup diagnostics");
    backupDiagnostics.beginRequest(project.id, masterId(project));
    QTRY_COMPARE(
        controller.diagnosticsProviderPresentation().displayName,
        QString("Backup diagnostics"));
    QTRY_VERIFY(status(diagnostics).contains("Backup diagnostics"));
    QVERIFY(status(diagnostics).contains("Diagnostics Starting"));
    QVERIFY(!status(diagnostics).contains("Mock", Qt::CaseInsensitive));
    QTRY_VERIFY(emptyState->text().contains("Backup diagnostics"));

    Data::DiagnosticsSnapshot backupSnapshot;
    backupSnapshot.projectId = project.id;
    backupSnapshot.masterId = masterId(project);
    backupSnapshot.mock = true;
    backupSnapshot.runMode = Data::DiagnosticsRunMode::Run;
    backupSnapshot.masterState = Data::EtherCATState::Operational;
    backupSnapshot.activeAlarmCount = 2;
    backupDiagnostics.publishSnapshot(backupSnapshot);
    QTRY_COMPARE(
        controller.diagnosticsProviderPresentation().displayName,
        QString("Backup diagnostics"));
    QTRY_VERIFY(status(diagnostics).contains("2 active"));
    QVERIFY(status(diagnostics).contains("MOCK"));

    Data::DiagnosticsSnapshot primarySnapshot = backupSnapshot;
    primarySnapshot.activeAlarmCount = 1;
    diagnosticsProvider.publishSnapshot(primarySnapshot);
    QTRY_COMPARE(
        controller.diagnosticsProviderPresentation().displayName,
        QString("Renamed Local Mock diagnostics"));
    QTRY_VERIFY(status(diagnostics).contains("1 active"));
    QVERIFY(!status(diagnostics).contains("2 active"));

    ExtensionSystem::PluginManager::removeObject(&diagnosticsProvider);
    diagnosticsRegistered = false;
    QTRY_COMPARE(
        controller.diagnosticsProviderPresentation().displayName,
        QString("Backup diagnostics"));
    QTRY_VERIFY(status(diagnostics).contains("2 active"));
    ExtensionSystem::PluginManager::addObject(&diagnosticsProvider);
    diagnosticsRegistered = true;
    QTRY_COMPARE(
        controller.diagnosticsProviderPresentation().displayName,
        QString("Renamed Local Mock diagnostics"));
    QTRY_VERIFY(status(diagnostics).contains("1 active"));

    diagnosticsProvider.setAvailable(false);
    QTRY_COMPARE(
        controller.diagnosticsProviderPresentation().displayName,
        QString("Backup diagnostics"));
    QTRY_VERIFY(status(diagnostics).contains("2 active"));
    backupDiagnostics.setAvailable(false);
    QTRY_COMPARE(status(diagnostics), QString("Renamed Local Mock diagnostics unavailable"));
    QVERIFY(!status(diagnostics).contains("active"));
    QVERIFY(!status(diagnostics).contains("Running"));
    QTRY_VERIFY(details.tabWidget()->count() > 0);
    summary = details.findChild<QLabel *>("EtherCATWorkbenchPageSummary");
    QVERIFY(summary);
    QTRY_VERIFY(summary->text().contains("Renamed Local Mock diagnostics"));
    QVERIFY(summary->text().contains("registered but unavailable"));

    ExtensionSystem::PluginManager::removeObject(&diagnosticsProvider);
    diagnosticsRegistered = false;
    QTRY_COMPARE(status(diagnostics), QString("Backup diagnostics unavailable"));
    ExtensionSystem::PluginManager::removeObject(&backupDiagnostics);
    backupDiagnosticsRegistered = false;
    QTRY_COMPARE(
        status(diagnostics), QString("No Diagnostics Provider registered | Local Mock only"));
    ExtensionSystem::PluginManager::removeObject(&backupScan);
    backupScanRegistered = false;
    ExtensionSystem::PluginManager::removeObject(&scan);
    scanRegistered = false;
    QTRY_COMPARE(status(noSlaves), QString("No Scan Provider registered | Local Mock only"));
    QCOMPARE(modelResetSpy.count(), 0);
    QCOMPARE(
        controller.selectionService()->currentNodeId(),
        diagnostics.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>());
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
    QCOMPARE(
        diagnostics.siblingAtColumn(1).data().toString(),
        QString("No Diagnostics Provider registered | Local Mock only"));
    QCOMPARE(
        controller.treeModel()->index(1, 1, master).data().toString(),
        QString("No Scan Provider registered | Local Mock only"));

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
    QCOMPARE(
        diagnostics.siblingAtColumn(1).data().toString(),
        QString("Local Mock test diagnostics available"));
    QCOMPARE(
        controller.treeModel()->index(1, 1, master).data().toString(),
        QString("Local Mock test scanner available"));
    QCOMPARE(pages.pages(masterContext).size(), 2);

    diagnosticsProvider.setAvailable(false);
    QTRY_VERIFY(!controller.diagnosticsAvailable());
    QCOMPARE(
        diagnostics.siblingAtColumn(1).data().toString(),
        QString("Local Mock test diagnostics unavailable"));
    QCOMPARE(pages.pages(masterContext).size(), 4);
    diagnosticsProvider.setAvailable(true);
    QTRY_VERIFY(controller.diagnosticsAvailable());

    ExtensionSystem::PluginManager::removeObject(&diagnosticsProvider);
    ExtensionSystem::PluginManager::removeObject(&scan);
    QTRY_VERIFY(!controller.diagnosticsAvailable());
    QTRY_VERIFY(!controller.scanAvailable());
    QCOMPARE(
        diagnostics.siblingAtColumn(1).data().toString(),
        QString("No Diagnostics Provider registered | Local Mock only"));
    QCOMPARE(
        controller.treeModel()->index(1, 1, master).data().toString(),
        QString("No Scan Provider registered | Local Mock only"));
}

} // namespace EtherCAT::Workbench::Internal
