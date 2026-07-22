// Copyright (C) 2026 Kvell

#include "ethercatpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <coreplugin/messagemanager.h>

#include <utils/stylehelper.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStyle>
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
    , m_masterForm(new QWidget(this))
    , m_masterNetId(new QLineEdit(m_masterForm))
    , m_masterAdvancedSettings(new QPushButton(Tr::tr("Advanced Settings..."), m_masterForm))
    , m_masterExportConfiguration(
          new QPushButton(Tr::tr("Export Configuration File..."), m_masterForm))
    , m_masterSyncUnitAssignment(
          new QPushButton(Tr::tr("Sync Unit Assignment..."), m_masterForm))
    , m_masterTopology(new QPushButton(Tr::tr("Topology..."), m_masterForm))
    , m_masterFrameState(new QLabel(this))
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
    m_summary->setAccessibleName(Tr::tr("EtherCAT page summary"));

    m_masterForm->setObjectName("EtherCATMasterEthercatForm");
    m_masterNetId->setObjectName("EtherCATMasterEthercatNetId");
    m_masterAdvancedSettings->setObjectName("EtherCATMasterEthercatAdvancedSettings");
    m_masterExportConfiguration->setObjectName("EtherCATMasterEthercatExportConfiguration");
    m_masterSyncUnitAssignment->setObjectName("EtherCATMasterEthercatSyncUnitAssignment");
    m_masterTopology->setObjectName("EtherCATMasterEthercatTopology");
    m_masterFrameState->setObjectName("EtherCATMasterEthercatFrameState");

    m_masterForm->setAccessibleName(Tr::tr("EtherCAT master settings"));
    m_masterNetId->setAccessibleName(Tr::tr("EtherCAT master NetId"));
    m_masterNetId->setAccessibleDescription(
        Tr::tr("An ADS NetId is not assigned to the offline phase-1 master."));
    m_masterNetId->setToolTip(m_masterNetId->accessibleDescription());
    m_masterNetId->setReadOnly(true);

    const auto setUnavailable = [](QPushButton *button, const QString &description) {
        button->setAccessibleDescription(description);
        button->setToolTip(description);
        button->setEnabled(false);
    };
    m_masterAdvancedSettings->setAccessibleName(Tr::tr("EtherCAT master advanced settings"));
    setUnavailable(
        m_masterAdvancedSettings,
        Tr::tr("Advanced master settings require a separate bounded data-contract issue."));
    m_masterExportConfiguration->setAccessibleName(
        Tr::tr("Export EtherCAT master configuration"));
    setUnavailable(
        m_masterExportConfiguration,
        Tr::tr("Configuration export is unavailable until a versioned configuration format is "
               "defined."));
    m_masterSyncUnitAssignment->setAccessibleName(Tr::tr("EtherCAT Sync Unit assignment"));
    setUnavailable(
        m_masterSyncUnitAssignment,
        Tr::tr("Sync Unit assignment is unavailable because the offline model does not represent "
               "Sync Units."));
    m_masterTopology->setAccessibleName(Tr::tr("Open offline EtherCAT topology"));
    m_masterTopology->setAccessibleDescription(
        Tr::tr("Opens a read-only topology derived from the current offline project."));
    m_masterTopology->setToolTip(m_masterTopology->accessibleDescription());

    m_masterFrameState->setAccessibleName(Tr::tr("EtherCAT cyclic frame state"));
    m_masterFrameState->setWordWrap(true);
    m_masterFrameState->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto masterLayout = new QGridLayout(m_masterForm);
    masterLayout->setContentsMargins(0, 0, 0, 0);
    masterLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    masterLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    auto netIdLabel = new QLabel(Tr::tr("NetId:"), m_masterForm);
    netIdLabel->setBuddy(m_masterNetId);
    masterLayout->addWidget(netIdLabel, 0, 0);
    masterLayout->addWidget(m_masterNetId, 0, 1);
    masterLayout->addWidget(m_masterAdvancedSettings, 0, 2);
    masterLayout->addWidget(m_masterExportConfiguration, 1, 2);
    masterLayout->addWidget(m_masterSyncUnitAssignment, 2, 2);
    masterLayout->addWidget(m_masterTopology, 3, 2);
    masterLayout->setColumnStretch(1, 1);

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
    m_tree->setTextElideMode(Qt::ElideNone);
    m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_summary);
    layout->addWidget(m_masterForm);
    layout->addWidget(m_slaveForm);
    layout->addWidget(m_masterFrameState);
    layout->addWidget(m_tree, 1);

    connect(m_alias, &QSpinBox::editingFinished, this, &EtherCATPage::commitAlias);
    connect(m_masterTopology, &QPushButton::clicked, this, &EtherCATPage::showMasterTopology);
    reset({}, {});
}

void EtherCATPage::setContext(const Core::PropertyPageContext &context)
{
    if (m_masterTopologyDialog)
        m_masterTopologyDialog->reject();
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

    const QStringList cyclicFrameHeaders
        = {Tr::tr("Frame"),
           Tr::tr("Cmd"),
           Tr::tr("Addr"),
           Tr::tr("Len"),
           Tr::tr("WC"),
           Tr::tr("Sync Unit"),
           Tr::tr("Cycle (ms)"),
           Tr::tr("Utilization (%)"),
           Tr::tr("Size / Duration (µs)"),
           Tr::tr("Map Id")};

    const auto setTreePresentation = [this](const QString &name, const QString &description) {
        m_tree->setAccessibleName(name);
        m_tree->setAccessibleDescription(description);
        m_tree->setToolTip(description);
    };

    if (context.nodeKind == Core::WorkbenchNodeKind::Master && m_controller) {
        const QList<Data::OfflineSlaveConfiguration> slaves
            = m_controller->treeModel()->offlineSlavesForMaster(context.nodeId);
        reset(
            slaves.isEmpty() ? Tr::tr("No slaves are configured on this offline master.")
                             : Tr::tr(
                                   "Offline EtherCAT master with %n configured slave(s).",
                                   nullptr,
                                   slaves.size()),
            cyclicFrameHeaders);
        m_masterNetId->setText(Tr::tr("Not assigned (offline)"));
        m_masterForm->show();
        m_masterTopology->setEnabled(true);
        m_masterFrameState->setText(
            Tr::tr("Cyclic transfer frames are not generated: the offline phase-1 project has no "
                   "runtime task, frame scheduler, or Sync Unit model."));
        m_masterFrameState->show();
        setTreePresentation(
            Tr::tr("EtherCAT cyclic transfer frames"),
            Tr::tr("The TwinCAT-style frame columns are shown, but no runtime frame rows are "
                   "generated in the offline phase."));
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
        setTreePresentation(
            Tr::tr("EtherCAT SyncManager defaults"),
            Tr::tr("Read-only offline SyncManager defaults from the matched ESI device. No "
                   "controller, network, or physical hardware is accessed."));
        if (device)
            addSyncManagers(*device);
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        const bool syncManagersAvailable = device && !device->syncManagers.isEmpty();
        const bool deviceSupported = device && device->summary.supported;
        QString summary;
        QString treeDescription;
        if (!device) {
            summary = Tr::tr(
                "The ESI device description is no longer available. Return to Device Repository "
                "and select an available device before opening EtherCAT.");
            treeDescription = Tr::tr(
                "The ESI device description is unavailable, so no SyncManager configuration can "
                "be shown. Return to Device Repository and select an available device. This "
                "read-only page does not access a controller, network, or physical hardware.");
        } else if (!syncManagersAvailable && !deviceSupported) {
            summary = Tr::tr(
                "No ESI SyncManager configuration is available. This repository device also "
                "contains unsupported ESI structures and cannot be added to an offline Project. "
                "Review its support details in Device Repository; Workbench will not fabricate "
                "SyncManager rows, and no controller, network, or physical hardware is accessed.");
            treeDescription = summary;
        } else if (!syncManagersAvailable) {
            summary = Tr::tr(
                "No ESI SyncManager configuration is available for this repository device. "
                "Workbench will not fabricate SyncManager rows. Review its source and "
                "qualification in Device Repository or import a matching ESI description; no "
                "controller, network, or physical hardware is accessed.");
            treeDescription = summary;
        } else if (!deviceSupported) {
            summary = Tr::tr(
                "Imported ESI SyncManager configuration is available for read-only offline "
                "preview, but this repository device contains unsupported ESI structures and "
                "cannot be added to an offline Project. Review its support details in Device "
                "Repository; no controller, network, or physical hardware is accessed.");
            treeDescription = summary;
        } else {
            summary = Tr::tr(
                "Imported ESI SyncManager configuration is available for read-only offline "
                "preview. No Project is modified, and no controller, network, or physical "
                "hardware is accessed.");
            treeDescription = summary;
        }
        reset(summary, syncManagersAvailable ? syncManagerHeaders : QStringList());
        setTreePresentation(Tr::tr("EtherCAT SyncManager defaults"), treeDescription);
        if (syncManagersAvailable)
            addSyncManagers(*device);
    } else {
        reset(Tr::tr("EtherCAT properties are unavailable for this selection."), {});
    }

    m_updating = false;
}

void EtherCATPage::reset(const QString &summary, const QStringList &headers)
{
    m_summary->setText(summary);
    m_summary->setAccessibleDescription(summary);
    m_summary->setToolTip(summary);
    m_masterForm->hide();
    m_masterNetId->clear();
    m_masterTopology->setEnabled(false);
    m_masterFrameState->clear();
    m_masterFrameState->hide();
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
    m_tree->setAccessibleName({});
    m_tree->setAccessibleDescription({});
    m_tree->setToolTip({});
    m_tree->setColumnCount(qMax(1, headers.size()));
    m_tree->setHeaderLabels(headers);
    m_tree->setVisible(!headers.isEmpty());
}

void EtherCATPage::addSyncManagerRow(const QStringList &values)
{
    auto item = new QTreeWidgetItem(values);
    const QString syncManagerName
        = values.value(1).isEmpty() ? Tr::tr("unnamed") : values.value(1);
    const QString syncManagerIdentity
        = Tr::tr("SM %1 (%2)").arg(values.value(0), syncManagerName);
    for (int column = 0; column < item->columnCount(); ++column) {
        const QString displayed = item->text(column);
        const QString completeValue = displayed.isEmpty() ? Tr::tr("Empty") : displayed;
        const QString description
            = Tr::tr("%1 column for SyncManager %2. Complete value: %3. Read-only offline ESI "
                     "default; no controller, network, or physical hardware is accessed.")
                  .arg(m_tree->headerItem()->text(column), syncManagerIdentity, completeValue);
        item->setData(column, Qt::AccessibleTextRole, displayed);
        item->setData(column, Qt::AccessibleDescriptionRole, description);
        item->setData(column, Qt::ToolTipRole, description);
    }
    m_tree->addTopLevelItem(item);
}

void EtherCATPage::addSyncManagers(const Data::DeviceDescription &device)
{
    for (const Data::SyncManagerDescription &syncManager : device.syncManagers) {
        addSyncManagerRow(
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

void EtherCATPage::showMasterTopology()
{
    if (!m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;
    if (m_masterTopologyDialog) {
        m_masterTopologyDialog->raise();
        m_masterTopologyDialog->activateWindow();
        return;
    }

    const QList<Data::OfflineSlaveConfiguration> slaves
        = m_controller->treeModel()->offlineSlavesForMaster(m_context.nodeId);

    auto dialog = new QDialog(this);
    m_masterTopologyDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (m_masterTopologyDialog == dialog)
            m_masterTopologyDialog = nullptr;
    });
    dialog->setObjectName("EtherCATMasterTopologyDialog");
    dialog->setWindowTitle(Tr::tr("Offline EtherCAT Topology"));
    dialog->setSizeGripEnabled(true);

    auto summary = new QLabel(dialog);
    summary->setObjectName("EtherCATMasterTopologySummary");
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setText(
        slaves.isEmpty()
            ? Tr::tr("No configured slaves are available in the current offline project.")
            : Tr::tr("Read-only offline topology for %n configured slave(s). Physical ports are "
                     "not modeled.",
                     nullptr,
                     slaves.size()));

    auto table = new QTreeWidget(dialog);
    table->setObjectName("EtherCATMasterTopologyTable");
    table->setAccessibleName(Tr::tr("Offline EtherCAT topology"));
    table->setAccessibleDescription(
        Tr::tr("Read-only configured slave order and identities from the current offline Project. "
               "Physical ports are not modeled; no controller, network, or physical hardware is "
               "accessed."));
    table->setAlternatingRowColors(true);
    table->setRootIsDecorated(false);
    table->setUniformRowHeights(true);
    table->setTextElideMode(Qt::ElideNone);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const QStringList topologyHeaders = {
        Tr::tr("Position"),
        Tr::tr("Name"),
        Tr::tr("Auto Inc Addr"),
        Tr::tr("Previous"),
        Tr::tr("Port"),
        Tr::tr("Vendor"),
        Tr::tr("Product"),
        Tr::tr("Revision"),
        Tr::tr("Alias"),
        Tr::tr("Status"),
    };
    table->setHeaderLabels(topologyHeaders);
    table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->header()->setStretchLastSection(true);

    for (int index = 0; index < slaves.size(); ++index) {
        const Data::OfflineSlaveConfiguration &slave = slaves.at(index);
        const QString previous = index == 0 ? Tr::tr("EtherCAT Master")
                                            : slaves.at(index - 1).name;
        auto item = new QTreeWidgetItem(
            {QString::number(slave.position),
             slave.name,
             autoIncrementAddress(slave.position),
             previous,
             Tr::tr("Not modeled"),
             hexValue(slave.identity.vendorId, 8),
             hexValue(slave.identity.productCode, 8),
             hexValue(slave.identity.revisionNumber, 8),
             QString::number(slave.alias),
             Tr::tr("Offline configured")});
        for (int column = 0; column < item->columnCount(); ++column) {
            const QString displayed = item->text(column);
            const QString description
                = Tr::tr("%1 column for configured slave at Position %2 (%3). Complete value: "
                         "%4. Read-only offline Project topology; physical ports are not modeled, "
                         "and no controller, network, or physical hardware is accessed.")
                      .arg(topologyHeaders.at(column),
                           QString::number(slave.position),
                           slave.name,
                           displayed);
            item->setData(column, Qt::AccessibleTextRole, displayed);
            item->setData(column, Qt::AccessibleDescriptionRole, description);
            item->setData(column, Qt::ToolTipRole, description);
        }
        table->addTopLevelItem(item);
    }
    table->header()->resizeSections(QHeaderView::ResizeToContents);
    const int topologyWidth
        = table->header()->length()
          + table->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, table)
          + table->frameWidth() * 2;

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName("EtherCATMasterTopologyButtons");
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(summary);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);

    layout->activate();
    QSize preferredSize = dialog->sizeHint();
    const QMargins layoutMargins = layout->contentsMargins();
    preferredSize.setWidth(
        qMax(preferredSize.width(),
             topologyWidth + layoutMargins.left() + layoutMargins.right()));
    if (const QScreen *screen = this->screen()) {
        const QRect availableGeometry = screen->availableGeometry().marginsRemoved(
            {Utils::StyleHelper::SpacingTokens::PaddingHM,
             Utils::StyleHelper::SpacingTokens::PaddingVM,
             Utils::StyleHelper::SpacingTokens::PaddingHM,
             Utils::StyleHelper::SpacingTokens::PaddingVM});
        QRect dialogGeometry(QPoint(), preferredSize.boundedTo(availableGeometry.size()));
        dialogGeometry.moveCenter(availableGeometry.center());
        dialog->setGeometry(dialogGeometry);
    } else {
        dialog->resize(preferredSize);
    }

    dialog->open();
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
