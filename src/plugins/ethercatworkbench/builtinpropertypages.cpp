// Copyright (C) 2026 Kvell

#include "builtinpropertypages.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "processdatapage.h"
#include "workbenchcontroller.h"

#include <utils/stylehelper.h>

#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

class BuiltinPageWidget final : public QWidget
{
public:
    explicit BuiltinPageWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , summary(new QLabel(this))
        , tree(new QTreeWidget(this))
    {
        setProperty("EtherCAT.Workbench.BuiltinPage", true);
        summary->setObjectName("EtherCATWorkbenchPageSummary");
        summary->setWordWrap(true);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        tree->setObjectName("EtherCATWorkbenchPageTree");
        tree->setAlternatingRowColors(true);
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(true);
        tree->header()->setStretchLastSection(true);

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM,
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM);
        layout->addWidget(summary);
        layout->addWidget(tree, 1);
    }

    void reset(const QString &text, const QStringList &headers)
    {
        summary->setText(text);
        tree->clear();
        tree->setColumnCount(qMax(1, headers.size()));
        tree->setHeaderLabels(headers);
        tree->setVisible(!headers.isEmpty());
    }

    void addRow(const QStringList &values)
    {
        tree->addTopLevelItem(new QTreeWidgetItem(values));
    }

    QLabel *summary;
    QTreeWidget *tree;
};

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static BuiltinPageWidget *pageWidget(QWidget *page)
{
    return page && page->property("EtherCAT.Workbench.BuiltinPage").toBool()
               ? static_cast<BuiltinPageWidget *>(page)
               : nullptr;
}

BuiltinPropertyPageProvider::BuiltinPropertyPageProvider(
    WorkbenchController *controller, QObject *parent)
    : Core::PropertyPageProvider(
          Constants::BUILTIN_PAGE_PROVIDER_ID, Tr::tr("Built-in EtherCAT pages"), parent)
    , m_controller(controller)
{
    setAvailable(true);
}

QList<Core::PropertyPageDescriptor> BuiltinPropertyPageProvider::pages(
    const Core::PropertyPageContext &context) const
{
    using Kind = Core::WorkbenchNodeKind;
    switch (context.nodeKind) {
    case Kind::Project:
    case Kind::Target:
    case Kind::DeviceRepository:
        return {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
    case Kind::Master:
    {
        QList<Core::PropertyPageDescriptor> result = {
            {Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100},
            {Utils::Id(Constants::ETHERCAT_PAGE_ID), Tr::tr("EtherCAT"), 200}};
        if (!m_controller || !m_controller->diagnosticsAvailable()) {
            result.append(
                {Utils::Id(Constants::ONLINE_PAGE_ID), Tr::tr("Online"), 800});
            result.append(
                {Utils::Id(Constants::DIAGNOSTICS_PAGE_ID), Tr::tr("Diagnostics"), 900});
        }
        return result;
    }
    case Kind::Device:
    case Kind::ConfiguredSlave:
    {
        QList<Core::PropertyPageDescriptor> result = {
            {Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100},
            {Utils::Id(Constants::ETHERCAT_PAGE_ID), Tr::tr("EtherCAT"), 200},
            {Utils::Id(Constants::PROCESS_DATA_PAGE_ID), Tr::tr("Process Data"), 300},
            {Utils::Id(Constants::STARTUP_PAGE_ID), Tr::tr("Startup"), 400},
            {Utils::Id(Constants::DC_PAGE_ID), Tr::tr("DC"), 500}};
        if (!m_controller || !m_controller->diagnosticsAvailable())
            result.append({Utils::Id(Constants::ONLINE_PAGE_ID), Tr::tr("Online"), 800});
        return result;
    }
    case Kind::Diagnostics:
        if (m_controller && m_controller->diagnosticsAvailable())
            return {};
        return {{Utils::Id(Constants::DIAGNOSTICS_PAGE_ID), Tr::tr("Diagnostics"), 900}};
    default:
        return {};
    }
}

QWidget *BuiltinPropertyPageProvider::createPage(Utils::Id pageId, QWidget *parent)
{
    const QList<Utils::Id> knownPages = {
        Constants::GENERAL_PAGE_ID,
        Constants::ETHERCAT_PAGE_ID,
        Constants::PROCESS_DATA_PAGE_ID,
        Constants::STARTUP_PAGE_ID,
        Constants::DC_PAGE_ID,
        Constants::ONLINE_PAGE_ID,
        Constants::DIAGNOSTICS_PAGE_ID,
    };
    if (!knownPages.contains(pageId))
        return nullptr;
    if (pageId == Utils::Id(Constants::PROCESS_DATA_PAGE_ID)) {
        auto page = new ProcessDataPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    auto widget = new BuiltinPageWidget(parent);
    widget->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
    return widget;
}

void BuiltinPropertyPageProvider::updatePage(
    Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context)
{
    if (pageId == Utils::Id(Constants::PROCESS_DATA_PAGE_ID)) {
        if (auto processDataPage = qobject_cast<ProcessDataPage *>(page))
            processDataPage->setContext(context);
        return;
    }
    BuiltinPageWidget *widget = pageWidget(page);
    if (!widget || !m_controller)
        return;

    const std::optional<Data::DeviceDescription> device
        = [this, &context]() -> std::optional<Data::DeviceDescription> {
        if (!m_controller->deviceRepository())
            return std::nullopt;
        if (context.nodeKind == Core::WorkbenchNodeKind::Device)
            return m_controller->deviceRepository()->device(context.nodeId);
        if (context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave) {
            const std::optional<Data::OfflineSlaveConfiguration> slave
                = m_controller->treeModel()->offlineSlave(context.nodeId);
            if (slave && !slave->deviceDescriptionId.isNull())
                return m_controller->deviceRepository()->device(slave->deviceDescriptionId);
        }
        return std::nullopt;
    }();
    const std::optional<Data::OfflineSlaveConfiguration> offlineSlave
        = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
              ? m_controller->treeModel()->offlineSlave(context.nodeId)
              : std::nullopt;

    if (pageId == Utils::Id(Constants::GENERAL_PAGE_ID)) {
        widget->reset(
            Tr::tr("Offline properties for %1").arg(context.displayName),
            {Tr::tr("Property"), Tr::tr("Value")});
        widget->addRow({Tr::tr("Name"), context.displayName});
        widget->addRow({Tr::tr("Node ID"), context.nodeId.toString()});
        if (offlineSlave) {
            widget->addRow({Tr::tr("Position"), QString::number(offlineSlave->position)});
            widget->addRow({Tr::tr("Vendor ID"), hexValue(offlineSlave->identity.vendorId, 8)});
            widget->addRow(
                {Tr::tr("Product Code"), hexValue(offlineSlave->identity.productCode, 8)});
            widget->addRow(
                {Tr::tr("Revision"), hexValue(offlineSlave->identity.revisionNumber, 8)});
            widget->addRow(
                {Tr::tr("Serial Number"), QString::number(offlineSlave->serialNumber)});
            widget->addRow({Tr::tr("Alias"), QString::number(offlineSlave->alias)});
            widget->addRow(
                {Tr::tr("ESI match"),
                 device ? device->summary.name : Tr::tr("No matching ESI device")});
        } else if (device) {
            widget->addRow({Tr::tr("Vendor ID"), hexValue(device->summary.identity.vendorId, 8)});
            widget->addRow(
                {Tr::tr("Product Code"), hexValue(device->summary.identity.productCode, 8)});
            widget->addRow(
                {Tr::tr("Revision"), hexValue(device->summary.identity.revisionNumber, 8)});
        }
        if (device) {
            widget->addRow({Tr::tr("Type"), device->summary.typeName});
            widget->addRow({Tr::tr("Group"), device->summary.group});
            widget->addRow(
                {Tr::tr("Support"),
                 device->summary.supported ? Tr::tr("Supported") : Tr::tr("Limited")});
            widget->addRow({Tr::tr("Source"), device->sourcePath});
            widget->addRow(
                {Tr::tr("Imported"), device->importedAt.toLocalTime().toString(Qt::ISODate)});
        } else if (context.nodeKind == Core::WorkbenchNodeKind::DeviceRepository
                   && m_controller->deviceRepository()) {
            widget->addRow(
                {Tr::tr("Indexed devices"),
                 QString::number(m_controller->deviceRepository()->devices().size())});
            widget->addRow(
                {Tr::tr("Index state"),
                 m_controller->deviceRepository()->isIndexing() ? Tr::tr("Indexing")
                                                                : Tr::tr("Ready")});
        } else if (!context.projectId.isNull() && m_controller->projectService()) {
            const std::optional<Data::ProjectSnapshot> project
                = m_controller->projectService()->project(context.projectId);
            if (project) {
                widget->addRow(
                    {Tr::tr("Format version"), QString::number(project->formatVersion)});
                widget->addRow({Tr::tr("Created by"), project->createdBy});
                widget->addRow(
                    {Tr::tr("Modified"), project->modified ? Tr::tr("Yes") : Tr::tr("No")});
                if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
                    widget->addRow(
                        {Tr::tr("Configured slaves"),
                         QString::number(
                             m_controller->treeModel()
                                 ->offlineSlavesForMaster(context.nodeId)
                                 .size())});
                    widget->addRow({Tr::tr("Stage"), Tr::tr("Offline configuration")});
                } else if (context.nodeKind == Core::WorkbenchNodeKind::Target) {
                    widget->addRow({Tr::tr("Target type"), Tr::tr("Offline / Mock")});
                }
            }
        }
        return;
    }

    if (pageId == Utils::Id(Constants::ETHERCAT_PAGE_ID)) {
        if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
            const QList<Data::OfflineSlaveConfiguration> slaves
                = m_controller->treeModel()->offlineSlavesForMaster(context.nodeId);
            widget->reset(
                slaves.isEmpty() ? Tr::tr("No slaves are configured on this offline master.")
                                 : Tr::tr("Offline EtherCAT topology"),
                slaves.isEmpty()
                    ? QStringList()
                    : QStringList{Tr::tr("Position"),
                                  Tr::tr("Name"),
                                  Tr::tr("Vendor"),
                                  Tr::tr("Product"),
                                  Tr::tr("Revision"),
                                  Tr::tr("Alias")});
            for (const Data::OfflineSlaveConfiguration &slave : slaves) {
                widget->addRow({QString::number(slave.position),
                                slave.name,
                                hexValue(slave.identity.vendorId, 8),
                                hexValue(slave.identity.productCode, 8),
                                hexValue(slave.identity.revisionNumber, 8),
                                QString::number(slave.alias)});
            }
            return;
        }
        widget->reset(
            device ? Tr::tr("SyncManager defaults from the imported ESI file")
                   : Tr::tr("No matching ESI SyncManager data is available."),
            device ? QStringList{Tr::tr("SM"),
                                 Tr::tr("Name"),
                                 Tr::tr("Direction"),
                                 Tr::tr("Address"),
                                 Tr::tr("Size"),
                                 Tr::tr("Control"),
                                 Tr::tr("Enabled")}
                   : QStringList());
        if (device) {
            for (const Data::SyncManagerDescription &syncManager : device->syncManagers) {
                QString direction = Tr::tr("Unknown");
                if (syncManager.direction == Data::SyncManagerDirection::MasterToSlave)
                    direction = Tr::tr("Master to slave");
                else if (syncManager.direction == Data::SyncManagerDirection::SlaveToMaster)
                    direction = Tr::tr("Slave to master");
                widget->addRow({QString::number(syncManager.index),
                                syncManager.name,
                                direction,
                                hexValue(syncManager.startAddress, 4),
                                QString::number(syncManager.defaultSize),
                                hexValue(syncManager.controlByte, 2),
                                syncManager.enabled ? Tr::tr("Yes") : Tr::tr("No")});
            }
        }
        return;
    }

    if (pageId == Utils::Id(Constants::STARTUP_PAGE_ID)) {
        widget->reset(
            device ? Tr::tr("Offline CoE startup parameters from the ESI file")
                   : Tr::tr("No startup data is available."),
            {Tr::tr("Transition"),
             Tr::tr("Index"),
             Tr::tr("Subindex"),
             Tr::tr("Data"),
             Tr::tr("Comment")});
        if (device) {
            for (const Data::StartupParameterDescription &parameter : device->startupParameters) {
                widget->addRow({parameter.transition,
                                hexValue(parameter.index, 4),
                                QString::number(parameter.subIndex),
                                QString::fromLatin1(parameter.data.toHex(' ')),
                                parameter.comment});
            }
        }
        return;
    }

    if (pageId == Utils::Id(Constants::DC_PAGE_ID)) {
        widget->reset(
            device ? Tr::tr("Distributed Clocks defaults from the ESI file")
                   : Tr::tr("No DC data is available."),
            {Tr::tr("Mode"),
             Tr::tr("AssignActivate"),
             Tr::tr("Sync0 cycle"),
             Tr::tr("Sync0 shift"),
             Tr::tr("Sync1 cycle"),
             Tr::tr("Sync1 shift")});
        if (device) {
            for (const Data::DcModeDescription &mode : device->dcModes) {
                widget->addRow({mode.name,
                                hexValue(mode.assignActivate, 4),
                                QString::number(mode.cycleTimeSync0Ns),
                                QString::number(mode.shiftTimeSync0Ns),
                                QString::number(mode.cycleTimeSync1Ns),
                                QString::number(mode.shiftTimeSync1Ns)});
            }
        }
        return;
    }

    if (pageId == Utils::Id(Constants::ONLINE_PAGE_ID)) {
        widget->reset(
            Tr::tr("Online data is unavailable. The Scan and Diagnostics plugins are not "
                   "installed, and this stage contains no controller protocol."),
            {});
        return;
    }

    if (pageId == Utils::Id(Constants::DIAGNOSTICS_PAGE_ID)) {
        widget->reset(
            Tr::tr("Diagnostics plugin not installed. No WKC, DC, link, alarm, or live state data "
                   "is produced by Workbench."),
            {});
    }
}

} // namespace EtherCAT::Workbench::Internal
