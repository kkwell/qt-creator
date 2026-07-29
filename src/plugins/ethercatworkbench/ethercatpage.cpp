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
#include <QScopedValueRollback>
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

static QString controllerConnectionStateName(Data::ControllerConnectionState state)
{
    using State = Data::ControllerConnectionState;
    switch (state) {
    case State::Disconnected:
        return Tr::tr("Disconnected");
    case State::Connecting:
        return Tr::tr("Connecting");
    case State::Handshaking:
        return Tr::tr("Handshaking");
    case State::Connected:
        return Tr::tr("Connected");
    case State::Degraded:
        return Tr::tr("Degraded");
    case State::Disconnecting:
        return Tr::tr("Disconnecting");
    case State::Failed:
        return Tr::tr("Failed");
    }
    return Tr::tr("Unknown");
}

static QString controllerServiceStateName(Data::ControllerServiceState state)
{
    using State = Data::ControllerServiceState;
    switch (state) {
    case State::Unknown:
        return Tr::tr("Unknown");
    case State::Boot:
        return Tr::tr("Boot");
    case State::Configuring:
        return Tr::tr("Configuring");
    case State::SafeOperational:
        return Tr::tr("Safe operational");
    case State::OperationalSafe:
        return Tr::tr("Operational safe");
    case State::Running:
        return Tr::tr("Running");
    case State::Stopping:
        return Tr::tr("Stopping");
    case State::Fault:
        return Tr::tr("Fault");
    case State::Recovering:
        return Tr::tr("Recovering");
    case State::Shutdown:
        return Tr::tr("Shutdown");
    case State::Paused:
        return Tr::tr("Paused");
    }
    return Tr::tr("Unknown");
}

static QString controllerAlStateName(quint32 alState)
{
    if (alState & 0x08)
        return Tr::tr("OP");
    if (alState & 0x04)
        return Tr::tr("SAFEOP");
    if (alState & 0x02)
        return Tr::tr("PREOP");
    if (alState & 0x01)
        return Tr::tr("INIT");
    return hexValue(alState, 4);
}

class AliasSpinBox final : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;

    QLineEdit *editor() const { return lineEdit(); }
};

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
    , m_alias(new AliasSpinBox(m_slaveForm))
    , m_aliasEditor(static_cast<AliasSpinBox *>(m_alias)->editor())
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
    if (m_controller) {
        connect(
            m_controller,
            &WorkbenchController::controllerConnectionChanged,
            this,
            &EtherCATPage::updateMasterPresentation);
    }
    reset({}, {});
}

void EtherCATPage::setContext(const Core::PropertyPageContext &context)
{
    if (m_masterTopologyDialog)
        m_masterTopologyDialog->reject();

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
    const bool configuredSlave = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
                                 && slave.has_value();
    const bool preserveAlias
        = !m_forceAuthoritativeAliasReload && configuredSlave && m_alias->isEnabled()
          && (m_aliasEditor->isModified() || m_alias->hasFocus() || m_aliasEditor->hasFocus())
          && m_context.projectId == context.projectId && m_context.nodeId == context.nodeId
          && m_context.nodeKind == context.nodeKind
          && m_aliasBaselineProjectId == context.projectId
          && m_aliasBaselineNodeId == context.nodeId && m_aliasBaselineKind == context.nodeKind
          && m_aliasBaseline == slave->alias;

    m_context = context;
    m_updating = true;

    const QStringList syncManagerHeaders
        = {Tr::tr("SM"),
           Tr::tr("Name"),
           Tr::tr("Direction"),
           Tr::tr("Address"),
           Tr::tr("Size"),
           Tr::tr("Control"),
           Tr::tr("Enabled")};

    const auto setTreePresentation = [this](const QString &name, const QString &description) {
        m_tree->setAccessibleName(name);
        m_tree->setAccessibleDescription(description);
        m_tree->setToolTip(description);
    };

    if (context.nodeKind == Core::WorkbenchNodeKind::Master && m_controller) {
        updateMasterPresentation();
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
            device ? syncManagerHeaders : QStringList(),
            preserveAlias);
        m_type->setText(typeName(device));
        m_productRevision->setText(
            Tr::tr("%1 / %2").arg(
                hexValue(slave->identity.productCode, 8),
                hexValue(slave->identity.revisionNumber, 8)));
        m_autoIncAddress->setText(autoIncrementAddress(slave->position));
        m_ethercatAddress->setText(Tr::tr("Automatic at startup (not stored)"));
        if (!preserveAlias)
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

    if (configuredSlave) {
        m_aliasBaselineProjectId = context.projectId;
        m_aliasBaselineNodeId = context.nodeId;
        m_aliasBaselineKind = context.nodeKind;
        m_aliasBaseline = slave->alias;
    } else {
        m_aliasBaselineProjectId = {};
        m_aliasBaselineNodeId = {};
        m_aliasBaselineKind = Core::WorkbenchNodeKind::None;
        m_aliasBaseline = 0;
    }

    m_updating = false;
}

void EtherCATPage::reset(
    const QString &summary, const QStringList &headers, bool preserveAlias)
{
    m_summary->setText(summary);
    m_summary->setAccessibleDescription(summary);
    m_summary->setToolTip(summary);
    m_masterForm->hide();
    m_masterNetId->clear();
    m_masterTopology->setEnabled(false);
    m_masterFrameState->clear();
    m_masterFrameState->hide();
    if (!preserveAlias)
        m_slaveForm->hide();
    m_type->clear();
    m_productRevision->clear();
    m_autoIncAddress->clear();
    m_ethercatAddress->clear();
    if (!preserveAlias) {
        m_alias->setValue(0);
        m_alias->setEnabled(false);
    }
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

void EtherCATPage::updateMasterPresentation()
{
    if (!m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;

    const QStringList offlineHeaders
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
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Data::ControllerConnectionSnapshot snapshot = m_controller->controllerConnectionSnapshot(
        scope);
    const bool controllerSessionVisible = snapshot.scope == scope
                                          && snapshot.state
                                                 != Data::ControllerConnectionState::Disconnected;
    const bool controllerStateIsLive = snapshot.state == Data::ControllerConnectionState::Connected
                                       || snapshot.state
                                              == Data::ControllerConnectionState::Degraded;
    if (!controllerSessionVisible) {
        const QList<Data::OfflineSlaveConfiguration> slaves
            = m_controller->treeModel()->offlineSlavesForMaster(m_context.nodeId);
        reset(
            slaves.isEmpty() ? Tr::tr("No slaves are configured on this offline master.")
                             : Tr::tr(
                                   "Offline EtherCAT master with %n configured slave(s).",
                                   nullptr,
                                   slaves.size()),
            offlineHeaders);
        m_masterNetId->setText(Tr::tr("Not assigned (offline)"));
        m_masterNetId->setAccessibleDescription(
            Tr::tr("An ADS NetId is not assigned to the offline phase-1 master."));
        m_masterNetId->setToolTip(m_masterNetId->accessibleDescription());
        m_masterForm->show();
        m_masterTopology->setEnabled(true);
        m_masterFrameState->setText(
            Tr::tr(
                "Cyclic transfer frames are not generated: the offline phase-1 project has no "
                "runtime task, frame scheduler, or Sync Unit model."));
        m_masterFrameState->show();
        m_tree->setAccessibleName(Tr::tr("EtherCAT cyclic transfer frames"));
        m_tree->setAccessibleDescription(
            Tr::tr(
                "The TwinCAT-style frame columns are shown, but no runtime frame rows are "
                "generated in the offline phase."));
        m_tree->setToolTip(m_tree->accessibleDescription());
        return;
    }

    QString summary = Tr::tr("Controller %1").arg(controllerConnectionStateName(snapshot.state));
    if (controllerStateIsLive && snapshot.controllerState) {
        const Data::ControllerStateSummary &state = *snapshot.controllerState;
        summary = Tr::tr("Online EtherCAT master — %1, AL %2, WKC %3/%4.")
                      .arg(
                          controllerServiceStateName(state.serviceState),
                          controllerAlStateName(state.ethercatAlStateBits))
                      .arg(state.actualWorkingCounter)
                      .arg(state.expectedWorkingCounter);
    }
    if (snapshot.topology) {
        summary += Tr::tr(
            " Last scan: %n responding device(s).",
            nullptr,
            int(snapshot.topology->respondingCount));
    }
    reset(summary, {Tr::tr("Live metric"), Tr::tr("Value"), Tr::tr("Source")});
    m_masterNetId->setText(
        snapshot.endpointSummary.isEmpty() ? Tr::tr("Not available") : snapshot.endpointSummary);
    m_masterNetId->setAccessibleDescription(Tr::tr("Connected controller Product API endpoint."));
    m_masterNetId->setToolTip(m_masterNetId->accessibleDescription());
    m_masterForm->show();
    m_masterTopology->setEnabled(true);
    m_masterFrameState->show();
    m_tree->setAccessibleName(Tr::tr("Live EtherCAT cyclic telemetry"));
    const QString protocolVersion
        = snapshot.protocolVersion.major
              ? QStringLiteral("%1.%2")
                    .arg(snapshot.protocolVersion.major)
                    .arg(snapshot.protocolVersion.minor)
              : Tr::tr("unknown");
    const QString telemetryDescription
        = Tr::tr(
              "Live aggregate cyclic telemetry from controller protocol v%1. The protocol does "
              "not expose individual cyclic frame descriptors.")
              .arg(protocolVersion);
    m_tree->setAccessibleDescription(telemetryDescription);
    m_tree->setToolTip(telemetryDescription);

    if (!controllerStateIsLive || !snapshot.controllerState) {
        m_masterFrameState->setText(
            controllerStateIsLive
                ? Tr::tr("The controller is connected; waiting for its authoritative runtime state.")
                : Tr::tr("Live cyclic telemetry is unavailable while the controller is %1.")
                      .arg(controllerConnectionStateName(snapshot.state)));
        addControllerTelemetryRow(
            Tr::tr("Connection"),
            controllerConnectionStateName(snapshot.state),
            Tr::tr("Controller session"));
        return;
    }

    const Data::ControllerStateSummary &state = *snapshot.controllerState;
    const quint64 liveCycleCount = state.cycleCount;
    if (state.serviceState == Data::ControllerServiceState::Running) {
        m_masterFrameState->setText(
            Tr::tr(
                "Live cyclic transfer is running. Cycle counter %1, WKC %2/%3. Protocol v%4 "
                "provides aggregate cyclic evidence but not individual frame descriptors.")
                .arg(liveCycleCount)
                .arg(state.actualWorkingCounter)
                .arg(state.expectedWorkingCounter)
                .arg(protocolVersion));
    } else if (state.busOperational && state.expectedWorkingCounter) {
        m_masterFrameState->setText(
            Tr::tr(
                "The EtherCAT bus is operational while the runtime is %1. Cycle counter %2, "
                "WKC %3/%4.")
                .arg(controllerServiceStateName(state.serviceState))
                .arg(liveCycleCount)
                .arg(state.actualWorkingCounter)
                .arg(state.expectedWorkingCounter));
    } else {
        m_masterFrameState->setText(
            Tr::tr("The controller is online in %1; cyclic process-data transfer is not active.")
                .arg(controllerServiceStateName(state.serviceState)));
    }

    addControllerTelemetryRow(
        Tr::tr("Cycle counter"), QString::number(liveCycleCount), Tr::tr("Controller state"));
    addControllerTelemetryRow(
        Tr::tr("Working counter"),
        Tr::tr("%1 / %2").arg(state.actualWorkingCounter).arg(state.expectedWorkingCounter),
        Tr::tr("Controller state"));
    addControllerTelemetryRow(
        Tr::tr("EtherCAT AL state"),
        Tr::tr("%1 (%2)").arg(
            controllerAlStateName(state.ethercatAlStateBits),
            hexValue(state.ethercatAlStateBits, 4)),
        Tr::tr("Controller state"));
    addControllerTelemetryRow(
        Tr::tr("Bus operational"),
        state.busOperational ? Tr::tr("Yes") : Tr::tr("No"),
        Tr::tr("Controller state"));
    addControllerTelemetryRow(
        Tr::tr("Distributed Clocks"),
        state.distributedClocksLocked
            ? Tr::tr("Locked — difference %1 ns").arg(state.distributedClockDifferenceNs)
            : Tr::tr("Not locked — difference %1 ns").arg(state.distributedClockDifferenceNs),
        Tr::tr("Controller state"));
    if (snapshot.performance) {
        const Data::ControllerPerformanceSummary &performance = *snapshot.performance;
        addControllerTelemetryRow(
            Tr::tr("Cycle timing"),
            Tr::tr("%1–%2 ns · submit late %3 ns")
                .arg(performance.minimumExchangeTimeNs)
                .arg(performance.maximumExchangeTimeNs)
                .arg(performance.maximumSubmitLatenessNs),
            Tr::tr("Performance push"));
        addControllerTelemetryRow(
            Tr::tr("Cyclic alerts"),
            Tr::tr("Late %1 · WKC %2 · timeout %3")
                .arg(performance.cycleLateCount)
                .arg(performance.badWorkingCounterCount)
                .arg(performance.timeoutCount),
            Tr::tr("Performance push"));
        if (performance.processInputSampleValid) {
            addControllerTelemetryRow(
                Tr::tr("Process sample"),
                Tr::tr("%1 bytes · capture cycle %2 · age %3")
                    .arg(performance.processInputSample.size())
                    .arg(performance.processInputSampleCycleCount)
                    .arg(performance.processInputSampleAgeCycles),
                Tr::tr("Performance push"));
        }
    }
}

void EtherCATPage::addControllerTelemetryRow(
    const QString &metric, const QString &value, const QString &source)
{
    auto item = new QTreeWidgetItem({metric, value, source});
    for (int column = 0; column < item->columnCount(); ++column) {
        const QString displayed = item->text(column);
        const QString description
            = Tr::tr("%1. Current value: %2. Source: %3.").arg(metric, value, source);
        item->setData(column, Qt::AccessibleTextRole, displayed);
        item->setData(column, Qt::AccessibleDescriptionRole, description);
        item->setData(column, Qt::ToolTipRole, description);
    }
    m_tree->addTopLevelItem(item);
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
    const QScopedValueRollback forceReload(m_forceAuthoritativeAliasReload, true);
    m_aliasEditor->setModified(false);
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
