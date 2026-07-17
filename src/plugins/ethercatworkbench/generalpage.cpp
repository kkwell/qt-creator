// Copyright (C) 2026 Kvell

#include "generalpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <coreplugin/messagemanager.h>

#include <utils/stylehelper.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMargins>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static int structuralNodeOrdinal(
    const Data::ProjectSnapshot &project, Data::ProjectNodeKind kind, const Data::NodeId &nodeId)
{
    int ordinal = 0;
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind != kind)
            continue;
        ++ordinal;
        if (node.id == nodeId)
            return ordinal;
    }
    return -1;
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
    , m_masterContent(new QWidget(this))
    , m_masterForm(new QWidget(m_masterContent))
    , m_masterName(new QLineEdit(m_masterForm))
    , m_masterId(new QLineEdit(m_masterForm))
    , m_masterObjectId(new QLineEdit(m_masterForm))
    , m_masterType(new QLineEdit(m_masterForm))
    , m_masterComment(new QPlainTextEdit(m_masterForm))
    , m_masterDisabled(new QCheckBox(Tr::tr("Disabled"), m_masterForm))
    , m_masterCreateSymbols(new QCheckBox(Tr::tr("Create symbols"), m_masterForm))
    , m_masterSummaryForm(new QGroupBox(Tr::tr("Offline configuration summary"), m_masterContent))
    , m_masterCycle(new QLineEdit(m_masterSummaryForm))
    , m_masterSlaveCount(new QLineEdit(m_masterSummaryForm))
    , m_masterStatus(new QLineEdit(m_masterSummaryForm))
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

    m_masterContent->setObjectName("EtherCATMasterGeneralContent");
    m_masterForm->setObjectName("EtherCATMasterGeneralForm");
    m_masterName->setObjectName("EtherCATMasterGeneralName");
    m_masterId->setObjectName("EtherCATMasterGeneralId");
    m_masterObjectId->setObjectName("EtherCATMasterGeneralObjectId");
    m_masterType->setObjectName("EtherCATMasterGeneralType");
    m_masterComment->setObjectName("EtherCATMasterGeneralComment");
    m_masterDisabled->setObjectName("EtherCATMasterGeneralDisabled");
    m_masterCreateSymbols->setObjectName("EtherCATMasterGeneralCreateSymbols");
    m_masterForm->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_masterName->setAccessibleName(Tr::tr("EtherCAT master name"));
    m_masterId->setAccessibleName(Tr::tr("EtherCAT master ID"));
    m_masterObjectId->setAccessibleName(Tr::tr("EtherCAT master object ID"));
    m_masterType->setAccessibleName(Tr::tr("EtherCAT master type"));
    m_masterComment->setAccessibleName(Tr::tr("EtherCAT master comment"));
    m_masterDisabled->setAccessibleName(Tr::tr("Disable EtherCAT master"));
    m_masterCreateSymbols->setAccessibleName(Tr::tr("Create EtherCAT master symbols"));
    m_masterId->setToolTip(Tr::tr("One-based identifier of this master in the project."));
    m_masterObjectId->setToolTip(Tr::tr("Stable project node identifier."));
    const QString unavailableTip = Tr::tr(
        "The current offline project contract does not store this TwinCAT-style setting.");
    m_masterComment->setToolTip(unavailableTip);
    m_masterComment->setAccessibleDescription(unavailableTip);
    m_masterComment->setPlaceholderText(Tr::tr("Not available in phase 1"));
    m_masterDisabled->setToolTip(unavailableTip);
    m_masterDisabled->setAccessibleDescription(unavailableTip);
    m_masterCreateSymbols->setToolTip(unavailableTip);
    m_masterCreateSymbols->setAccessibleDescription(unavailableTip);
    m_masterId->setReadOnly(true);
    m_masterObjectId->setReadOnly(true);
    m_masterType->setReadOnly(true);
    m_masterComment->setReadOnly(true);
    constexpr int commentVisibleLines = 5;
    const int commentHeight = m_masterComment->fontMetrics().lineSpacing() * commentVisibleLines
                              + 2
                                    * m_masterComment->style()->pixelMetric(
                                        QStyle::PM_DefaultFrameWidth);
    m_masterComment->setFixedHeight(commentHeight);
    m_masterComment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_masterDisabled->setEnabled(false);
    m_masterCreateSymbols->setEnabled(false);

    auto masterGrid = new QGridLayout(m_masterForm);
    masterGrid->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    masterGrid->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    masterGrid->setColumnStretch(1, 1);
    auto nameLabel = new QLabel(Tr::tr("Name:"), m_masterForm);
    nameLabel->setBuddy(m_masterName);
    auto idLabel = new QLabel(Tr::tr("Id:"), m_masterForm);
    idLabel->setBuddy(m_masterId);
    auto objectIdLabel = new QLabel(Tr::tr("Object Id:"), m_masterForm);
    objectIdLabel->setBuddy(m_masterObjectId);
    auto typeLabel = new QLabel(Tr::tr("Type:"), m_masterForm);
    typeLabel->setBuddy(m_masterType);
    auto commentLabel = new QLabel(Tr::tr("Comment:"), m_masterForm);
    commentLabel->setBuddy(m_masterComment);
    commentLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    masterGrid->addWidget(nameLabel, 0, 0);
    masterGrid->addWidget(m_masterName, 0, 1);
    masterGrid->addWidget(idLabel, 0, 2);
    masterGrid->addWidget(m_masterId, 0, 3);
    masterGrid->addWidget(objectIdLabel, 1, 0);
    masterGrid->addWidget(m_masterObjectId, 1, 1, 1, 3);
    masterGrid->addWidget(typeLabel, 2, 0);
    masterGrid->addWidget(m_masterType, 2, 1, 1, 3);
    masterGrid->addWidget(commentLabel, 3, 0);
    masterGrid->addWidget(m_masterComment, 3, 1, 1, 3);
    masterGrid->addWidget(m_masterDisabled, 4, 1);
    masterGrid->addWidget(m_masterCreateSymbols, 4, 3, Qt::AlignRight);

    m_masterSummaryForm->setObjectName("EtherCATMasterConfigurationSummary");
    m_masterSummaryForm->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_masterCycle->setObjectName("EtherCATMasterGeneralCycle");
    m_masterSlaveCount->setObjectName("EtherCATMasterGeneralSlaveCount");
    m_masterStatus->setObjectName("EtherCATMasterGeneralStatus");
    m_masterCycle->setAccessibleName(Tr::tr("EtherCAT master cycle time"));
    m_masterSlaveCount->setAccessibleName(Tr::tr("Configured EtherCAT slave count"));
    m_masterStatus->setAccessibleName(Tr::tr("EtherCAT master status summary"));
    m_masterCycle->setToolTip(
        Tr::tr("Cycle time is assigned by a real-time task in a later configuration stage."));
    m_masterStatus->setToolTip(
        Tr::tr("Summary from the shared Workbench scan and diagnostics presentation."));
    m_masterCycle->setReadOnly(true);
    m_masterSlaveCount->setReadOnly(true);
    m_masterStatus->setReadOnly(true);
    auto masterSummary = new QFormLayout(m_masterSummaryForm);
    masterSummary->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    masterSummary->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    masterSummary->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    masterSummary->addRow(Tr::tr("Cycle time:"), m_masterCycle);
    masterSummary->addRow(Tr::tr("Configured slaves:"), m_masterSlaveCount);
    masterSummary->addRow(Tr::tr("Status:"), m_masterStatus);

    auto masterContentLayout = new QVBoxLayout(m_masterContent);
    masterContentLayout->setContentsMargins(QMargins());
    masterContentLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    masterContentLayout->addWidget(m_masterForm);
    masterContentLayout->addWidget(m_masterSummaryForm);
    masterContentLayout->addStretch(1);

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
    layout->addWidget(m_masterContent, 1);
    layout->addWidget(m_identityForm);
    layout->addWidget(m_tree, 1);

    connect(m_name, &QLineEdit::editingFinished, this, &GeneralPage::commitName);
    connect(m_masterName, &QLineEdit::editingFinished, this, &GeneralPage::commitMasterName);
    if (m_controller) {
        connect(
            m_controller->treeModel(),
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
                refreshMasterSummary();
            });
        connect(
            m_controller->treeModel(),
            &QAbstractItemModel::modelReset,
            this,
            &GeneralPage::refreshMasterSummary);
    }
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
    const std::optional<Data::ProjectSnapshot> project
        = !context.projectId.isNull() && m_controller && m_controller->projectService()
              ? m_controller->projectService()->project(context.projectId)
              : std::nullopt;

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
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
        m_summary->setText(
            Tr::tr("Offline EtherCAT master properties for %1").arg(context.displayName));
        m_masterName->setText(context.displayName);
        const int id
            = project
                  ? structuralNodeOrdinal(*project, Data::ProjectNodeKind::Master, context.nodeId)
                  : -1;
        m_masterId->setText(id > 0 ? QString::number(id) : Tr::tr("Unavailable"));
        m_masterObjectId->setText(context.nodeId.toString());
        m_masterType->setText(Tr::tr("EtherCAT Master"));
        m_masterName->setReadOnly(!project || !project->valid);
        m_masterContent->show();
        m_masterForm->show();
        m_masterSummaryForm->show();
        m_tree->hide();
        refreshMasterSummary();
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
    } else if (project && context.nodeKind != Core::WorkbenchNodeKind::Master) {
        addRow({Tr::tr("Format version"), QString::number(project->formatVersion)});
        addRow({Tr::tr("Created by"), project->createdBy});
        addRow({Tr::tr("Modified"), project->modified ? Tr::tr("Yes") : Tr::tr("No")});
        if (context.nodeKind == Core::WorkbenchNodeKind::Target)
            addRow({Tr::tr("Target type"), Tr::tr("Offline / Mock")});
    }
    m_updating = false;
}

void GeneralPage::reset(const QString &summary)
{
    m_summary->setText(summary);
    m_identityForm->hide();
    m_masterContent->hide();
    m_masterForm->hide();
    m_masterSummaryForm->hide();
    m_tree->show();
    m_name->clear();
    m_id->clear();
    m_objectId->clear();
    m_type->clear();
    m_masterName->clear();
    m_masterName->setReadOnly(true);
    m_masterId->clear();
    m_masterObjectId->clear();
    m_masterType->clear();
    m_masterComment->clear();
    m_masterDisabled->setChecked(false);
    m_masterCreateSymbols->setChecked(false);
    m_masterCycle->clear();
    m_masterSlaveCount->clear();
    m_masterStatus->clear();
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

void GeneralPage::commitMasterName()
{
    if (m_updating || !m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;
    const Utils::Result<> result
        = m_controller
              ->renameStructuralNode(m_context.projectId, m_context.nodeId, m_masterName->text());
    if (!result) {
        ::Core::MessageManager::writeFlashing(
            Tr::tr("Cannot rename the EtherCAT master: %1").arg(result.error()));
    }
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
}

void GeneralPage::refreshMasterSummary()
{
    if (!m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;
    m_masterCycle->setText(Tr::tr("Not assigned (offline)"));
    m_masterSlaveCount->setText(QString::number(
        m_controller->treeModel()->offlineSlavesForMaster(m_context.nodeId).size()));
    const QModelIndex masterIndex = m_controller->treeModel()->indexForNodeId(m_context.nodeId);
    const QString status = masterIndex.data(WorkbenchTreeModel::StatusRole).toString();
    m_masterStatus->setText(status.isEmpty() ? Tr::tr("Offline configuration") : status);
}

} // namespace EtherCAT::Workbench::Internal
