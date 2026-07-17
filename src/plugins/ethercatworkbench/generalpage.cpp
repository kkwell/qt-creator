// Copyright (C) 2026 Kvell

#include "generalpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <coreplugin/messagemanager.h>

#include <utils/stylehelper.h>

#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

GeneralPage::GeneralPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_identityForm(new QWidget(this))
    , m_name(new QLineEdit(m_identityForm))
    , m_id(new QLineEdit(m_identityForm))
    , m_objectId(new QLineEdit(m_identityForm))
    , m_type(new QLineEdit(m_identityForm))
    , m_tree(new QTreeWidget(this))
{
    m_summary->setObjectName("EtherCATWorkbenchPageSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_identityForm->setObjectName("EtherCATGeneralIdentityForm");
    m_name->setObjectName("EtherCATGeneralName");
    m_id->setObjectName("EtherCATGeneralId");
    m_objectId->setObjectName("EtherCATGeneralObjectId");
    m_type->setObjectName("EtherCATGeneralType");
    m_name->setAccessibleName(Tr::tr("EtherCAT device name"));
    m_id->setAccessibleName(Tr::tr("EtherCAT device ID"));
    m_objectId->setAccessibleName(Tr::tr("EtherCAT object ID"));
    m_type->setAccessibleName(Tr::tr("EtherCAT device type"));
    m_id->setToolTip(Tr::tr("One-based identifier derived from the offline physical order."));
    m_objectId->setToolTip(Tr::tr("Stable project node identifier."));
    m_id->setReadOnly(true);
    m_objectId->setReadOnly(true);
    m_type->setReadOnly(true);

    auto form = new QFormLayout(m_identityForm);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    form->addRow(Tr::tr("Name:"), m_name);
    form->addRow(Tr::tr("Id:"), m_id);
    form->addRow(Tr::tr("Object Id:"), m_objectId);
    form->addRow(Tr::tr("Type:"), m_type);

    m_tree->setObjectName("EtherCATWorkbenchPageTree");
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_summary);
    layout->addWidget(m_identityForm);
    layout->addWidget(m_tree, 1);

    connect(m_name, &QLineEdit::editingFinished, this, &GeneralPage::commitName);
    reset({});
}

void GeneralPage::setContext(const Core::PropertyPageContext &context)
{
    m_context = context;
    m_updating = true;

    const std::optional<Data::OfflineSlaveConfiguration> offlineSlave
        = m_controller ? m_controller->treeModel()->offlineSlave(context.nodeId) : std::nullopt;
    const std::optional<Data::DeviceDescription> device =
        [this, &context, &offlineSlave]() -> std::optional<Data::DeviceDescription> {
        if (!m_controller || !m_controller->deviceRepository())
            return std::nullopt;
        if (context.nodeKind == Core::WorkbenchNodeKind::Device)
            return m_controller->deviceRepository()->device(context.nodeId);
        if (offlineSlave && !offlineSlave->deviceDescriptionId.isNull()) {
            return m_controller->deviceRepository()->device(offlineSlave->deviceDescriptionId);
        }
        return std::nullopt;
    }();

    reset(Tr::tr("Offline properties for %1").arg(context.displayName));
    const bool configuredSlave = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
                                 && offlineSlave.has_value();
    if (configuredSlave) {
        const QString typeName = !device ? Tr::tr("Unknown ESI device")
                                 : !device->summary.typeName.isEmpty() ? device->summary.typeName
                                                                       : device->summary.name;
        m_name->setText(offlineSlave->name);
        m_id->setText(QString::number(offlineSlave->position + 1));
        m_objectId->setText(offlineSlave->id.toString());
        m_type->setText(typeName);
        m_identityForm->show();
    } else {
        addRow({Tr::tr("Name"), context.displayName});
        addRow({Tr::tr("Node ID"), context.nodeId.toString()});
    }

    if (offlineSlave) {
        if (!configuredSlave)
            addRow({Tr::tr("Owner slave"), offlineSlave->name});
        addRow({Tr::tr("Position"), QString::number(offlineSlave->position)});
        addRow({Tr::tr("Vendor ID"), hexValue(offlineSlave->identity.vendorId, 8)});
        addRow({Tr::tr("Product Code"), hexValue(offlineSlave->identity.productCode, 8)});
        addRow({Tr::tr("Revision"), hexValue(offlineSlave->identity.revisionNumber, 8)});
        addRow({Tr::tr("Serial Number"), QString::number(offlineSlave->serialNumber)});
        addRow({Tr::tr("Alias"), QString::number(offlineSlave->alias)});
        addRow(
            {Tr::tr("ESI match"), device ? device->summary.name : Tr::tr("No matching ESI device")});
    } else if (device) {
        addRow({Tr::tr("Vendor ID"), hexValue(device->summary.identity.vendorId, 8)});
        addRow({Tr::tr("Product Code"), hexValue(device->summary.identity.productCode, 8)});
        addRow({Tr::tr("Revision"), hexValue(device->summary.identity.revisionNumber, 8)});
    }
    if (device) {
        if (!configuredSlave)
            addRow({Tr::tr("Type"), device->summary.typeName});
        addRow({Tr::tr("Group"), device->summary.group});
        addRow(
            {Tr::tr("Support"),
             device->summary.supported ? Tr::tr("Supported") : Tr::tr("Limited")});
        addRow({Tr::tr("Source"), device->sourcePath});
        addRow({Tr::tr("Imported"), device->importedAt.toLocalTime().toString(Qt::ISODate)});
    } else if (
        context.nodeKind == Core::WorkbenchNodeKind::DeviceRepository && m_controller
        && m_controller->deviceRepository()) {
        addRow(
            {Tr::tr("Indexed devices"),
             QString::number(m_controller->deviceRepository()->devices().size())});
        addRow(
            {Tr::tr("Index state"),
             m_controller->deviceRepository()->isIndexing() ? Tr::tr("Indexing") : Tr::tr("Ready")});
    } else if (!context.projectId.isNull() && m_controller && m_controller->projectService()) {
        const std::optional<Data::ProjectSnapshot> project
            = m_controller->projectService()->project(context.projectId);
        if (project) {
            addRow({Tr::tr("Format version"), QString::number(project->formatVersion)});
            addRow({Tr::tr("Created by"), project->createdBy});
            addRow({Tr::tr("Modified"), project->modified ? Tr::tr("Yes") : Tr::tr("No")});
            if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
                addRow(
                    {Tr::tr("Configured slaves"),
                     QString::number(
                         m_controller->treeModel()->offlineSlavesForMaster(context.nodeId).size())});
                addRow({Tr::tr("Stage"), Tr::tr("Offline configuration")});
            } else if (context.nodeKind == Core::WorkbenchNodeKind::Target) {
                addRow({Tr::tr("Target type"), Tr::tr("Offline / Mock")});
            }
        }
    }
    m_updating = false;
}

void GeneralPage::reset(const QString &summary)
{
    m_summary->setText(summary);
    m_identityForm->hide();
    m_name->clear();
    m_id->clear();
    m_objectId->clear();
    m_type->clear();
    m_tree->clear();
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({Tr::tr("Property"), Tr::tr("Value")});
}

void GeneralPage::addRow(const QStringList &values)
{
    m_tree->addTopLevelItem(new QTreeWidgetItem(values));
}

void GeneralPage::commitName()
{
    if (m_updating || !m_controller
        || m_context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave) {
        return;
    }
    const Utils::Result<> result
        = m_controller->renameOfflineSlave(m_context.projectId, m_context.nodeId, m_name->text());
    if (!result) {
        ::Core::MessageManager::writeFlashing(
            Tr::tr("Cannot rename the offline slave: %1").arg(result.error()));
    }
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
}

} // namespace EtherCAT::Workbench::Internal
