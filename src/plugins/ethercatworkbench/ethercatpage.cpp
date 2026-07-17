// Copyright (C) 2026 Kvell

#include "ethercatpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <coreplugin/messagemanager.h>

#include <utils/stylehelper.h>

#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <limits>

namespace EtherCAT::Workbench::Internal {

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static QString autoIncrementAddress(int position)
{
    if (position < 0 || position > std::numeric_limits<quint16>::max())
        return Tr::tr("Not available");
    return hexValue(quint16(-qint64(position)), 4);
}

static QString typeName(const std::optional<Data::DeviceDescription> &device)
{
    if (!device)
        return Tr::tr("Unknown ESI device");
    return device->summary.typeName.isEmpty() ? device->summary.name : device->summary.typeName;
}

static QString syncManagerDirection(Data::SyncManagerDirection direction)
{
    if (direction == Data::SyncManagerDirection::MasterToSlave)
        return Tr::tr("Master to slave");
    if (direction == Data::SyncManagerDirection::SlaveToMaster)
        return Tr::tr("Slave to master");
    return Tr::tr("Unknown");
}

EtherCATPage::EtherCATPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_slaveForm(new QWidget(this))
    , m_type(new QLineEdit(m_slaveForm))
    , m_productRevision(new QLineEdit(m_slaveForm))
    , m_autoIncAddress(new QLineEdit(m_slaveForm))
    , m_ethercatAddress(new QLineEdit(m_slaveForm))
    , m_alias(new QSpinBox(m_slaveForm))
    , m_identificationValue(new QLineEdit(m_slaveForm))
    , m_previousPort(new QLineEdit(m_slaveForm))
    , m_advancedSettings(new QPushButton(Tr::tr("Advanced Settings..."), m_slaveForm))
    , m_tree(new QTreeWidget(this))
{
    m_summary->setObjectName("EtherCATWorkbenchPageSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_slaveForm->setObjectName("EtherCATEthercatSlaveForm");
    m_type->setObjectName("EtherCATEthercatType");
    m_productRevision->setObjectName("EtherCATEthercatProductRevision");
    m_autoIncAddress->setObjectName("EtherCATEthercatAutoIncAddress");
    m_ethercatAddress->setObjectName("EtherCATEthercatAddress");
    m_alias->setObjectName("EtherCATEthercatAlias");
    m_identificationValue->setObjectName("EtherCATEthercatIdentificationValue");
    m_previousPort->setObjectName("EtherCATEthercatPreviousPort");
    m_advancedSettings->setObjectName("EtherCATEthercatAdvancedSettings");

    m_type->setAccessibleName(Tr::tr("EtherCAT device type"));
    m_productRevision->setAccessibleName(Tr::tr("EtherCAT product and revision"));
    m_autoIncAddress->setAccessibleName(Tr::tr("EtherCAT auto-increment address"));
    m_ethercatAddress->setAccessibleName(Tr::tr("EtherCAT fixed address"));
    m_alias->setAccessibleName(Tr::tr("EtherCAT configured station alias"));
    m_identificationValue->setAccessibleName(Tr::tr("EtherCAT identification value"));
    m_previousPort->setAccessibleName(Tr::tr("EtherCAT previous port"));
    m_advancedSettings->setAccessibleName(Tr::tr("EtherCAT advanced settings"));

    for (QLineEdit *field :
         {m_type,
          m_productRevision,
          m_autoIncAddress,
          m_ethercatAddress,
          m_identificationValue,
          m_previousPort}) {
        field->setReadOnly(true);
    }
    m_alias->setRange(0, std::numeric_limits<quint16>::max());
    m_alias->setKeyboardTracking(false);
    m_alias->setSpecialValueText(Tr::tr("0 (disabled)"));
    m_alias->setToolTip(
        Tr::tr("Configured Station Alias stored in the offline project. Zero disables it."));
    m_ethercatAddress->setToolTip(
        Tr::tr("A fixed EtherCAT address is not represented by the phase-1 project model."));
    m_identificationValue->setToolTip(
        Tr::tr("Slave identification checking is not represented by the phase-1 project model."));
    m_previousPort->setToolTip(
        Tr::tr("The configured predecessor is known, but physical port data is not modeled."));
    m_advancedSettings->setToolTip(
        Tr::tr("Advanced slave settings require a separate bounded data-contract issue."));
    m_advancedSettings->setEnabled(false);

    auto form = new QFormLayout(m_slaveForm);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    form->addRow(Tr::tr("Type:"), m_type);
    form->addRow(Tr::tr("Product/Revision:"), m_productRevision);
    form->addRow(Tr::tr("Auto Inc Addr:"), m_autoIncAddress);
    form->addRow(Tr::tr("EtherCAT Addr:"), m_ethercatAddress);
    form->addRow(Tr::tr("Configured Station Alias:"), m_alias);
    form->addRow(Tr::tr("Identification Value:"), m_identificationValue);
    form->addRow(Tr::tr("Previous Port:"), m_previousPort);
    form->addRow(QString(), m_advancedSettings);

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
    layout->addWidget(m_slaveForm);
    layout->addWidget(m_tree, 1);

    connect(m_alias, &QSpinBox::editingFinished, this, &EtherCATPage::commitAlias);
    reset({}, {});
}

void EtherCATPage::setContext(const Core::PropertyPageContext &context)
{
    m_context = context;
    m_updating = true;

    const std::optional<Data::OfflineSlaveConfiguration> slave
        = m_controller ? m_controller->treeModel()->offlineSlave(context.nodeId) : std::nullopt;
    const std::optional<Data::DeviceDescription> device =
        [this, &context, &slave]() -> std::optional<Data::DeviceDescription> {
        if (!m_controller || !m_controller->deviceRepository())
            return std::nullopt;
        if (context.nodeKind == Core::WorkbenchNodeKind::Device)
            return m_controller->deviceRepository()->device(context.nodeId);
        if (slave && !slave->deviceDescriptionId.isNull())
            return m_controller->deviceRepository()->device(slave->deviceDescriptionId);
        return std::nullopt;
    }();

    const QStringList syncManagerHeaders
        = {Tr::tr("SM"),
           Tr::tr("Name"),
           Tr::tr("Direction"),
           Tr::tr("Address"),
           Tr::tr("Size"),
           Tr::tr("Control"),
           Tr::tr("Enabled")};

    if (context.nodeKind == Core::WorkbenchNodeKind::Master && m_controller) {
        const QList<Data::OfflineSlaveConfiguration> slaves
            = m_controller->treeModel()->offlineSlavesForMaster(context.nodeId);
        reset(
            slaves.isEmpty() ? Tr::tr("No slaves are configured on this offline master.")
                             : Tr::tr("Offline EtherCAT topology"),
            slaves.isEmpty() ? QStringList()
                             : QStringList{
                                   Tr::tr("Position"),
                                   Tr::tr("Name"),
                                   Tr::tr("Vendor"),
                                   Tr::tr("Product"),
                                   Tr::tr("Revision"),
                                   Tr::tr("Alias")});
        for (const Data::OfflineSlaveConfiguration &entry : slaves) {
            addRow(
                {QString::number(entry.position),
                 entry.name,
                 hexValue(entry.identity.vendorId, 8),
                 hexValue(entry.identity.productCode, 8),
                 hexValue(entry.identity.revisionNumber, 8),
                 QString::number(entry.alias)});
        }
    } else if (context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && slave) {
        reset(
            device ? Tr::tr(
                         "Offline EtherCAT settings for %1. ESI SyncManager defaults are shown "
                         "below.")
                         .arg(slave->name)
                   : Tr::tr(
                         "Offline EtherCAT settings for %1. No matching ESI SyncManager data is "
                         "available.")
                         .arg(slave->name),
            device ? syncManagerHeaders : QStringList());
        m_type->setText(typeName(device));
        m_productRevision->setText(
            Tr::tr("%1 / %2").arg(
                hexValue(slave->identity.productCode, 8),
                hexValue(slave->identity.revisionNumber, 8)));
        m_autoIncAddress->setText(autoIncrementAddress(slave->position));
        m_ethercatAddress->setText(Tr::tr("Automatic at startup (not stored)"));
        m_alias->setValue(slave->alias);
        m_alias->setEnabled(true);
        m_identificationValue->setText(Tr::tr("Not configured"));
        m_previousPort->setText(previousPortText(*slave));
        m_slaveForm->show();
        if (device)
            addSyncManagers(*device);
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        reset(
            device ? Tr::tr("SyncManager defaults from the imported ESI file")
                   : Tr::tr("No matching ESI SyncManager data is available."),
            device ? syncManagerHeaders : QStringList());
        if (device)
            addSyncManagers(*device);
    } else {
        reset(Tr::tr("EtherCAT properties are unavailable for this selection."), {});
    }

    m_updating = false;
}

void EtherCATPage::reset(const QString &summary, const QStringList &headers)
{
    m_summary->setText(summary);
    m_slaveForm->hide();
    m_type->clear();
    m_productRevision->clear();
    m_autoIncAddress->clear();
    m_ethercatAddress->clear();
    m_alias->setValue(0);
    m_alias->setEnabled(false);
    m_identificationValue->clear();
    m_previousPort->clear();
    m_tree->clear();
    m_tree->setColumnCount(qMax(1, headers.size()));
    m_tree->setHeaderLabels(headers);
    m_tree->setVisible(!headers.isEmpty());
}

void EtherCATPage::addRow(const QStringList &values)
{
    m_tree->addTopLevelItem(new QTreeWidgetItem(values));
}

void EtherCATPage::addSyncManagers(const Data::DeviceDescription &device)
{
    for (const Data::SyncManagerDescription &syncManager : device.syncManagers) {
        addRow(
            {QString::number(syncManager.index),
             syncManager.name,
             syncManagerDirection(syncManager.direction),
             hexValue(syncManager.startAddress, 4),
             QString::number(syncManager.defaultSize),
             hexValue(syncManager.controlByte, 2),
             syncManager.enabled ? Tr::tr("Yes") : Tr::tr("No")});
    }
}

void EtherCATPage::commitAlias()
{
    if (m_updating || !m_controller
        || m_context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave) {
        return;
    }
    const Utils::Result<> result = m_controller->setOfflineSlaveAlias(
        m_context.projectId, m_context.nodeId, quint16(m_alias->value()));
    if (!result) {
        ::Core::MessageManager::writeFlashing(
            Tr::tr("Cannot update the offline slave Alias: %1").arg(result.error()));
    }
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
}

QString EtherCATPage::previousPortText(const Data::OfflineSlaveConfiguration &slave) const
{
    if (!m_controller)
        return Tr::tr("Not available");
    const QList<Data::OfflineSlaveConfiguration> slaves
        = m_controller->treeModel()->offlineSlavesForMaster(slave.masterId);
    const Data::OfflineSlaveConfiguration *previous = nullptr;
    for (const Data::OfflineSlaveConfiguration &candidate : slaves) {
        if (candidate.position < slave.position
            && (!previous || candidate.position > previous->position)) {
            previous = &candidate;
        }
    }
    return previous ? Tr::tr("%1 (port not modeled)").arg(previous->name)
                    : Tr::tr("EtherCAT Master (port not modeled)");
}

} // namespace EtherCAT::Workbench::Internal
