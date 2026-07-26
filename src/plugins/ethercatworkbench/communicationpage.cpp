// Copyright (C) 2026 Kvell

#include "communicationpage.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>

#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace EtherCAT::Workbench::Internal {

static QString connectionStateText(Data::ControllerConnectionState state)
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

static bool ownsControllerControlLease(const Data::ControllerConnectionSnapshot &snapshot)
{
    return snapshot.session && snapshot.session->ownsControlLease;
}

static bool controllerControlLeaseHeldByAnotherSession(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    return snapshot.session && !snapshot.session->ownsControlLease
           && snapshot.session->controlLeaseOwnerSessionId != 0
           && snapshot.session->controlLeaseOwnerSessionId != snapshot.session->sessionId;
}

static bool controllerControlLeaseOwnershipUnverified(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    return snapshot.session && !snapshot.session->ownsControlLease
           && snapshot.session->controlLeaseOwnerSessionId != 0
           && snapshot.session->controlLeaseOwnerSessionId == snapshot.session->sessionId;
}

static QString controllerAccessText(const Data::ControllerConnectionSnapshot &snapshot)
{
    if (snapshot.mock)
        return Tr::tr("Mock");
    if (snapshot.readOnly)
        return Tr::tr("Read-only");
    if (ownsControllerControlLease(snapshot))
        return Tr::tr("Exclusive control");
    if (controllerControlLeaseHeldByAnotherSession(snapshot))
        return Tr::tr("Lease held by another session");
    if (controllerControlLeaseOwnershipUnverified(snapshot))
        return Tr::tr("Control lease ownership unverified");
    return Tr::tr("Control available / lease not acquired");
}

static QString channelStateText(Data::ControllerChannelState state)
{
    using State = Data::ControllerChannelState;
    switch (state) {
    case State::Disconnected:
        return Tr::tr("Disconnected");
    case State::Connecting:
        return Tr::tr("Connecting");
    case State::Handshaking:
        return Tr::tr("Handshaking");
    case State::Connected:
        return Tr::tr("Connected");
    case State::Failed:
        return Tr::tr("Failed");
    }
    return Tr::tr("Unknown");
}

static QString serviceStateText(Data::ControllerServiceState state)
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

static QString yesNo(bool value)
{
    return value ? Tr::tr("Yes") : Tr::tr("No");
}

static QString dateTimeText(const QDateTime &value)
{
    return value.isValid() ? QLocale().toString(value.toLocalTime(), QLocale::ShortFormat)
                           : Tr::tr("Not available");
}

static QString slotText(Data::ControllerSlot slot)
{
    switch (slot) {
    case Data::ControllerSlot::None:
        return Tr::tr("None");
    case Data::ControllerSlot::A:
        return Tr::tr("Slot A");
    case Data::ControllerSlot::B:
        return Tr::tr("Slot B");
    }
    return Tr::tr("Unknown");
}

static QString firmwareStateText(Data::ControllerFirmwareState state)
{
    using State = Data::ControllerFirmwareState;
    switch (state) {
    case State::Unknown:
        return Tr::tr("Unknown");
    case State::Idle:
        return Tr::tr("Idle");
    case State::Receiving:
        return Tr::tr("Receiving");
    case State::Staged:
        return Tr::tr("Staged");
    case State::Verified:
        return Tr::tr("Verified");
    case State::Installing:
        return Tr::tr("Installing");
    case State::ReadyToReboot:
        return Tr::tr("Ready to reboot");
    case State::TrialBoot:
        return Tr::tr("Trial boot");
    case State::Confirmed:
        return Tr::tr("Confirmed");
    case State::Rejected:
        return Tr::tr("Rejected");
    case State::RolledBack:
        return Tr::tr("Rolled back");
    }
    return Tr::tr("Unknown");
}

static QString hexadecimalValue(quint32 value, int width)
{
    return QStringLiteral("0x")
           + QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

static void addSummaryRow(QTreeWidget *tree, const QString &name, const QString &value)
{
    auto item = new QTreeWidgetItem({name, value});
    item->setToolTip(0, name);
    item->setToolTip(1, value);
    tree->addTopLevelItem(item);
}

CommunicationPage::CommunicationPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_provider(new QComboBox(this))
    , m_profile(new QComboBox(this))
    , m_endpoint(new QLineEdit(this))
    , m_saveEndpoint(new QToolButton(this))
    , m_connect(new QToolButton(this))
    , m_refresh(new QToolButton(this))
    , m_disconnect(new QToolButton(this))
    , m_acquireControl(new QToolButton(this))
    , m_enterConfiguration(new QToolButton(this))
    , m_scanBus(new QToolButton(this))
    , m_restorePackage(new QToolButton(this))
    , m_releaseControl(new QToolButton(this))
    , m_summary(new QTreeWidget(this))
    , m_channels(new QTreeWidget(this))
    , m_topologySummary(new QLabel(this))
    , m_actualBus(new QTreeWidget(this))
{
    setObjectName("EtherCATWorkbenchCommunicationPage");
    setAccessibleName(Tr::tr("Controller communication"));
    setAccessibleDescription(
        Tr::tr("Configures a controller adapter and controls its connected EtherCAT Master."));

    m_providerLabel = new QLabel(Tr::tr("Controller adapter:"), this);
    m_providerLabel->setBuddy(m_provider);
    m_profileLabel = new QLabel(Tr::tr("Connection profile:"), this);
    m_profileLabel->setBuddy(m_profile);
    m_endpointLabel = new QLabel(Tr::tr("Controller address:"), this);
    m_endpointLabel->setObjectName("EtherCATCommunicationEndpointLabel");
    m_endpointLabel->setBuddy(m_endpoint);
    m_provider->setObjectName("EtherCATCommunicationProvider");
    m_provider->setAccessibleName(Tr::tr("Controller adapter"));
    m_profile->setObjectName("EtherCATCommunicationProfile");
    m_profile->setAccessibleName(Tr::tr("Connection profile"));
    m_endpoint->setObjectName("EtherCATCommunicationEndpoint");
    m_endpoint->setAccessibleName(Tr::tr("Controller address"));
    m_endpoint->setClearButtonEnabled(true);
    m_saveEndpoint->setObjectName("EtherCATCommunicationSaveEndpoint");
    m_saveEndpoint->setAccessibleName(Tr::tr("Save controller address and connect"));
    m_saveEndpoint->setToolTip(
        Tr::tr("Save the controller address, then connect and scan automatically."));
    m_saveEndpoint->setIcon(Utils::Icons::LINK.icon());
    m_saveEndpoint->setAutoRaise(true);

    auto endpointWidget = new QWidget(this);
    auto endpointLayout = new QHBoxLayout(endpointWidget);
    endpointLayout->setContentsMargins(QMargins());
    endpointLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHS);
    endpointLayout->addWidget(m_endpoint, 1);
    endpointLayout->addWidget(m_saveEndpoint);

    auto form = new QFormLayout;
    form->setContentsMargins(QMargins());
    form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    form->addRow(m_providerLabel, m_provider);
    form->addRow(m_profileLabel, m_profile);
    form->addRow(m_endpointLabel, endpointWidget);

    const auto configureButton =
        [](QToolButton *button, const char *objectName, Utils::Id actionId) {
            button->setObjectName(objectName);
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            if (::Core::Command *command = ::Core::ActionManager::command(actionId))
                button->setDefaultAction(command->action());
            else
                button->setEnabled(false);
        };
    configureButton(
        m_connect,
        "EtherCATCommunicationConnect",
        Utils::Id(Constants::CONNECT_CONTROLLER_ACTION_ID));
    configureButton(
        m_refresh,
        "EtherCATCommunicationRefresh",
        Utils::Id(Constants::REFRESH_CONTROLLER_ACTION_ID));
    configureButton(
        m_disconnect,
        "EtherCATCommunicationDisconnect",
        Utils::Id(Constants::DISCONNECT_CONTROLLER_ACTION_ID));

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(QMargins());
    buttonLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    buttonLayout->addWidget(m_connect);
    buttonLayout->addWidget(m_refresh);
    buttonLayout->addWidget(m_disconnect);
    buttonLayout->addStretch();

    auto controlGroup = new QGroupBox(Tr::tr("Advanced controller control"), this);
    controlGroup->setObjectName("EtherCATCommunicationControllerControl");
    auto controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS,
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS);
    controlLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHS);
    controlLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    const auto configureControlButton =
        [this](QToolButton *button,
               const char *objectName,
               const QString &text,
               const QString &toolTip,
               Data::ControllerControlCommand command) {
            button->setObjectName(objectName);
            button->setText(text);
            button->setAccessibleName(text);
            button->setAccessibleDescription(toolTip);
            button->setToolTip(toolTip);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setEnabled(false);
            connect(button, &QToolButton::clicked, this, [this, command] {
                executeControllerControl(command);
            });
        };
    configureControlButton(
        m_acquireControl,
        "EtherCATCommunicationAcquireControl",
        Tr::tr("Acquire"),
        Tr::tr("Acquire the controller control lease for this EtherCAT Master."),
        Data::ControllerControlCommand::AcquireControl);
    configureControlButton(
        m_enterConfiguration,
        "EtherCATCommunicationEnterConfiguration",
        Tr::tr("Configuration"),
        Tr::tr("Stop active operation safely and enter controller configuration mode."),
        Data::ControllerControlCommand::EnterConfigurationMode);
    configureControlButton(
        m_scanBus,
        "EtherCATCommunicationScanBus",
        Tr::tr("Scan Bus"),
        Tr::tr("Scan the live EtherCAT bus. This does not modify the offline Project."),
        Data::ControllerControlCommand::DiscoverTopology);
    configureControlButton(
        m_restorePackage,
        "EtherCATCommunicationRestorePackage",
        Tr::tr("Restore Package"),
        Tr::tr("Restore the controller's exact persistent active package."),
        Data::ControllerControlCommand::RestoreActivePackage);
    configureControlButton(
        m_releaseControl,
        "EtherCATCommunicationReleaseControl",
        Tr::tr("Release"),
        Tr::tr(
            "Release this session's management lease without stopping an autonomous cyclic task."),
        Data::ControllerControlCommand::ReleaseControl);
    controlLayout->addWidget(m_acquireControl, 0, 0);
    controlLayout->addWidget(m_enterConfiguration, 0, 1);
    controlLayout->addWidget(m_scanBus, 0, 2);
    controlLayout->addWidget(m_restorePackage, 0, 3);
    controlLayout->addWidget(m_releaseControl, 0, 4);
    controlLayout->setColumnStretch(5, 1);

    m_summary->setObjectName("EtherCATCommunicationSummary");
    m_summary->setAccessibleName(Tr::tr("Controller connection summary"));
    m_summary->setColumnCount(2);
    m_summary->setHeaderLabels({Tr::tr("Property"), Tr::tr("Value")});
    m_summary->setRootIsDecorated(false);
    m_summary->setAlternatingRowColors(true);
    m_summary->setUniformRowHeights(true);
    m_summary->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_summary->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    m_channels->setObjectName("EtherCATCommunicationChannels");
    m_channels->setAccessibleName(Tr::tr("Controller communication channels"));
    m_channels->setColumnCount(5);
    m_channels->setHeaderLabels(
        {Tr::tr("Channel"),
         Tr::tr("State"),
         Tr::tr("Maximum payload"),
         Tr::tr("Last activity"),
         Tr::tr("Detail")});
    m_channels->setRootIsDecorated(false);
    m_channels->setAlternatingRowColors(true);
    m_channels->setUniformRowHeights(true);
    m_channels->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_channels->header()->setStretchLastSection(true);

    auto actualBusGroup = new QGroupBox(Tr::tr("Actual Bus"), this);
    actualBusGroup->setObjectName("EtherCATCommunicationActualBusGroup");
    auto actualBusLayout = new QVBoxLayout(actualBusGroup);
    actualBusLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS,
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS);
    actualBusLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    m_topologySummary->setObjectName("EtherCATCommunicationActualBusSummary");
    m_topologySummary->setTextFormat(Qt::PlainText);
    m_topologySummary->setWordWrap(true);
    m_actualBus->setObjectName("EtherCATCommunicationActualBus");
    m_actualBus->setAccessibleName(Tr::tr("Actual EtherCAT bus topology"));
    m_actualBus->setAccessibleDescription(
        Tr::tr("Read-only results from the most recent live controller bus scan."));
    m_actualBus->setColumnCount(7);
    m_actualBus->setHeaderLabels(
        {Tr::tr("Position"),
         Tr::tr("Station"),
         Tr::tr("AL"),
         Tr::tr("Vendor"),
         Tr::tr("Product"),
         Tr::tr("Revision"),
         Tr::tr("Serial")});
    m_actualBus->setRootIsDecorated(false);
    m_actualBus->setAlternatingRowColors(true);
    m_actualBus->setUniformRowHeights(true);
    m_actualBus->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_actualBus->header()->setStretchLastSection(true);
    actualBusLayout->addWidget(m_topologySummary);
    actualBusLayout->addWidget(m_actualBus);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addLayout(form);
    layout->addLayout(buttonLayout);
    layout->addWidget(controlGroup);
    layout->addWidget(m_summary, 1);
    layout->addWidget(m_channels, 1);
    layout->addWidget(actualBusGroup, 1);

    connect(m_provider, &QComboBox::currentIndexChanged, this, &CommunicationPage::selectProvider);
    connect(m_profile, &QComboBox::currentIndexChanged, this, &CommunicationPage::selectProfile);
    connect(m_endpoint, &QLineEdit::textEdited, this, [this] {
        m_endpointDirty = true;
        m_saveEndpoint->setEnabled(
            m_endpoint->isEnabled() && !m_endpoint->text().trimmed().isEmpty());
    });
    connect(m_endpoint, &QLineEdit::returnPressed, this, &CommunicationPage::saveEndpoint);
    connect(m_saveEndpoint, &QToolButton::clicked, this, &CommunicationPage::saveEndpoint);
    if (m_controller) {
        connect(
            m_controller,
            &WorkbenchController::controllerConnectionChanged,
            this,
            &CommunicationPage::refresh);
    }
}

void CommunicationPage::setContext(const Core::PropertyPageContext &context)
{
    if (m_context.projectId != context.projectId || m_context.nodeId != context.nodeId)
        m_endpointDirty = false;
    m_context = context;
    if (m_controller && context.nodeKind == Core::WorkbenchNodeKind::Master) {
        m_controller->prepareControllerConnection({context.projectId, context.nodeId});
    }
    refresh();
}

void CommunicationPage::refresh()
{
    if (m_updating)
        return;
    const QScopedValueRollback updating(m_updating, true);
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const bool validScope = m_controller && m_context.nodeKind == Core::WorkbenchNodeKind::Master
                            && !scope.projectId.isNull() && !scope.masterId.isNull();

    const QSignalBlocker providerBlocker(m_provider);
    const QSignalBlocker profileBlocker(m_profile);
    m_provider->clear();
    m_profile->clear();
    if (!validScope) {
        m_provider->setEnabled(false);
        m_profile->setEnabled(false);
        m_endpointDirty = false;
        m_endpoint->clear();
        m_endpoint->setPlaceholderText(Tr::tr("Not available"));
        m_endpoint->setEnabled(false);
        m_saveEndpoint->setEnabled(false);
        m_summary->clear();
        m_channels->clear();
        m_channels->hide();
        updateControllerControl({});
        updateTopology(std::nullopt);
        return;
    }

    const ControllerConnectionSelection selection = m_controller->prepareControllerConnection(scope);
    const QList<Core::ControllerConnectionProvider *> providers
        = m_controller->controllerConnectionProviders();
    if (!selection.providerExplicitlySelected || !selection.providerId.isValid())
        m_provider->addItem(Tr::tr("Select a controller adapter..."), QVariant());
    for (Core::ControllerConnectionProvider *provider : providers) {
        QString displayName = provider->displayName().trimmed();
        if (displayName.isEmpty())
            displayName = Tr::tr("Unnamed controller adapter");
        if (!provider->isAvailable())
            displayName = Tr::tr("%1 — unavailable").arg(displayName);
        m_provider->addItem(displayName, provider->id().toSetting());
        const int index = m_provider->count() - 1;
        m_provider->setItemData(index, provider->id().toString(), Qt::ToolTipRole);
    }
    int providerIndex = -1;
    for (int index = 0; index < m_provider->count(); ++index) {
        if (Utils::Id::fromSetting(m_provider->itemData(index)) == selection.providerId) {
            providerIndex = index;
            break;
        }
    }
    if (providerIndex < 0 && selection.providerId.isValid()) {
        m_provider->addItem(
            Tr::tr("Unavailable adapter — %1").arg(selection.providerId.toString()),
            selection.providerId.toSetting());
        providerIndex = m_provider->count() - 1;
    }
    m_provider->setCurrentIndex(providerIndex >= 0 ? providerIndex : 0);

    Core::ControllerConnectionProvider *provider = m_controller->controllerConnectionProvider(scope);
    const QList<Data::ControllerConnectionProfile> profiles
        = m_controller->controllerConnectionProfiles(scope);
    if (!selection.profileExplicitlySelected || selection.profileId.isNull())
        m_profile->addItem(Tr::tr("Select a connection profile..."), QVariant());
    for (const Data::ControllerConnectionProfile &profile : profiles) {
        QString displayName = profile.displayName.trimmed();
        if (displayName.isEmpty())
            displayName = Tr::tr("Unnamed connection profile");
        if (!profile.configured || !profile.supported)
            displayName = Tr::tr("%1 — unavailable").arg(displayName);
        m_profile->addItem(displayName, QVariant::fromValue(profile.id));
        const int index = m_profile->count() - 1;
        QString toolTip = profile.endpointSummary;
        if (!profile.configurationIssue.isEmpty())
            toolTip = toolTip.isEmpty() ? profile.configurationIssue
                                        : toolTip + "\n" + profile.configurationIssue;
        m_profile->setItemData(index, toolTip, Qt::ToolTipRole);
    }
    int profileIndex = -1;
    for (int index = 0; index < m_profile->count(); ++index) {
        if (m_profile->itemData(index).value<Data::NodeId>() == selection.profileId) {
            profileIndex = index;
            break;
        }
    }
    if (profileIndex < 0 && !selection.profileId.isNull()) {
        m_profile->addItem(
            Tr::tr("Unavailable profile — %1").arg(selection.profileId.toString()),
            QVariant::fromValue(selection.profileId));
        profileIndex = m_profile->count() - 1;
    }
    m_profile->setCurrentIndex(profileIndex >= 0 ? profileIndex : 0);

    const Data::ControllerConnectionSnapshot snapshot = m_controller->controllerConnectionSnapshot(
        scope);
    const bool locked = m_controller->controllerConnectionSelectionLocked(scope);
    const bool showProviderSelection
        = providers.size() != 1 || !selection.providerExplicitlySelected;
    const bool showProfileSelection
        = profiles.size() != 1 || !selection.profileExplicitlySelected;
    m_providerLabel->setVisible(showProviderSelection);
    m_provider->setVisible(showProviderSelection);
    m_profileLabel->setVisible(showProfileSelection);
    m_profile->setVisible(showProfileSelection);
    m_provider->setEnabled(!locked && !providers.isEmpty());
    m_profile->setEnabled(!locked && provider && !profiles.isEmpty());
    updateSummary(snapshot, provider);
    updateChannels(snapshot);
    updateControllerControl(snapshot);
    updateTopology(snapshot.scope == scope ? snapshot.topology : std::nullopt);

    QString endpoint;
    const auto profile
        = std::find_if(profiles.cbegin(), profiles.cend(), [&selection](const auto &candidate) {
              return candidate.id == selection.profileId;
          });
    if (profile != profiles.cend())
        endpoint = profile->endpointSummary;
    if (endpoint.isEmpty())
        endpoint = snapshot.endpointSummary;
    const std::optional<Data::ControllerConnectionProfileConfiguration> configuration
        = provider && !selection.profileId.isNull()
              ? m_controller->controllerConnectionProfileConfiguration(
                    scope, selection.profileId)
              : std::nullopt;
    m_endpointLabel->setText(
        configuration && !configuration->endpointLabel.isEmpty()
            ? configuration->endpointLabel
            : Tr::tr("Controller address:"));
    m_endpoint->setAccessibleName(
        configuration && !configuration->endpointAccessibleName.isEmpty()
            ? configuration->endpointAccessibleName
            : Tr::tr("Controller address"));
    if (!m_endpointDirty) {
        m_endpoint->setText(configuration ? configuration->endpoint : endpoint);
    }
    m_endpoint->setPlaceholderText(
        configuration && !configuration->placeholder.isEmpty()
            ? configuration->placeholder
            : Tr::tr("Not available"));
    const bool endpointEditable = configuration && configuration->editable && !locked;
    m_endpoint->setEnabled(endpointEditable);
    m_saveEndpoint->setEnabled(
        endpointEditable && m_endpointDirty && !m_endpoint->text().trimmed().isEmpty());
    m_endpoint->setToolTip(
        endpointEditable
            ? (configuration && !configuration->endpointDescription.isEmpty()
                   ? configuration->endpointDescription
                   : Tr::tr("Enter the controller address required by the selected adapter."))
            : m_endpoint->text());
}

void CommunicationPage::selectProvider(int index)
{
    if (m_updating || !m_controller || index < 0)
        return;
    m_endpointDirty = false;
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Utils::Result<> result = m_controller->selectControllerConnectionProvider(
        scope, Utils::Id::fromSetting(m_provider->itemData(index)));
    if (!result) {
        refresh();
        m_controller->writeControllerOutput(result.error(), ControllerOutputLevel::Error);
        return;
    }
}

void CommunicationPage::selectProfile(int index)
{
    if (m_updating || !m_controller || index < 0)
        return;
    m_endpointDirty = false;
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Utils::Result<> result = m_controller->selectControllerConnectionProfile(
        scope, m_profile->itemData(index).value<Data::NodeId>());
    if (!result) {
        refresh();
        m_controller->writeControllerOutput(result.error(), ControllerOutputLevel::Error);
        return;
    }
}

void CommunicationPage::saveEndpoint()
{
    if (!m_controller || !m_endpointDirty)
        return;

    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Data::NodeId profileId = m_profile->currentData().value<Data::NodeId>();
    const QString endpoint = m_endpoint->text().trimmed();
    const Utils::Result<> result
        = m_controller->setControllerConnectionProfileEndpoint(scope, profileId, endpoint);
    if (!result) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot save the controller address: %1").arg(result.error()),
            ControllerOutputLevel::Error);
        return;
    }

    m_endpointDirty = false;
    const std::optional<Data::ControllerConnectionProfileConfiguration> savedConfiguration
        = m_controller->controllerConnectionProfileConfiguration(scope, profileId);
    const QString savedEndpoint
        = savedConfiguration ? savedConfiguration->endpoint.trimmed() : QString();
    m_controller->writeControllerOutput(
        savedEndpoint.isEmpty()
            ? Tr::tr("Controller address saved.")
            : Tr::tr("Controller address saved: %1").arg(savedEndpoint));
    refresh();
    if (!m_controller->canConnectController(scope))
        return;
    if (const Utils::Result<> connected = m_controller->connectController(scope); !connected) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot connect to the controller: %1").arg(connected.error()),
            ControllerOutputLevel::Error);
    }
}

void CommunicationPage::updateSummary(
    const Data::ControllerConnectionSnapshot &snapshot, Core::ControllerConnectionProvider *provider)
{
    m_summary->clear();
    addSummaryRow(
        m_summary, Tr::tr("Adapter"), provider ? provider->displayName() : Tr::tr("Not available"));
    Data::ControllerConnectionScope displayScope = snapshot.scope;
    if (snapshot.state == Data::ControllerConnectionState::Disconnected
        && snapshot.scope.projectId.isNull() && snapshot.scope.masterId.isNull()) {
        displayScope = {m_context.projectId, m_context.nodeId};
    }
    if (!displayScope.projectId.isNull())
        addSummaryRow(m_summary, Tr::tr("Project ID"), displayScope.projectId.toString());
    if (!displayScope.masterId.isNull())
        addSummaryRow(m_summary, Tr::tr("Master ID"), displayScope.masterId.toString());
    addSummaryRow(m_summary, Tr::tr("Connection"), connectionStateText(snapshot.state));
    const bool hasConnectionEvidence
        = provider
          && (snapshot.state != Data::ControllerConnectionState::Disconnected
              || !snapshot.scope.projectId.isNull() || !snapshot.scope.masterId.isNull()
              || !snapshot.profileId.isNull() || snapshot.sessionGeneration != 0
              || snapshot.connectedAt.isValid() || snapshot.updatedAt.isValid()
              || !snapshot.channels.isEmpty());
    addSummaryRow(
        m_summary,
        Tr::tr("Access"),
        hasConnectionEvidence ? controllerAccessText(snapshot) : Tr::tr("Not available"));
    addSummaryRow(
        m_summary,
        Tr::tr("Evidence"),
        snapshot.state == Data::ControllerConnectionState::Connected
                || snapshot.state == Data::ControllerConnectionState::Degraded
            ? (snapshot.mock ? Tr::tr("Mock") : Tr::tr("Real controller"))
            : Tr::tr("No live controller evidence"));
    addSummaryRow(
        m_summary,
        Tr::tr("Protocol"),
        snapshot.protocolVersion.major
            ? Tr::tr("v%1.%2").arg(snapshot.protocolVersion.major).arg(snapshot.protocolVersion.minor)
            : Tr::tr("Not negotiated"));
    addSummaryRow(m_summary, Tr::tr("Connected at"), dateTimeText(snapshot.connectedAt));
    addSummaryRow(m_summary, Tr::tr("Updated at"), dateTimeText(snapshot.updatedAt));
    addSummaryRow(m_summary, Tr::tr("Last heartbeat"), dateTimeText(snapshot.lastHeartbeatAt));
    addSummaryRow(
        m_summary, Tr::tr("Session generation"), QString::number(snapshot.sessionGeneration));

    if (snapshot.session) {
        addSummaryRow(m_summary, Tr::tr("Session ID"), QString::number(snapshot.session->sessionId));
        addSummaryRow(m_summary, Tr::tr("Boot ID"), QString::number(snapshot.session->bootId));
        addSummaryRow(
            m_summary,
            Tr::tr("Control lease owner"),
            QString::number(snapshot.session->controlLeaseOwnerSessionId));
        addSummaryRow(
            m_summary, Tr::tr("Owns control lease"), yesNo(snapshot.session->ownsControlLease));
        addSummaryRow(
            m_summary,
            Tr::tr("Default lease duration"),
            Tr::tr("%1 ms").arg(snapshot.session->defaultControlLeaseDurationMs));
    }
    if (snapshot.controllerState) {
        addSummaryRow(
            m_summary,
            Tr::tr("Controller state"),
            serviceStateText(snapshot.controllerState->serviceState));
        addSummaryRow(m_summary, Tr::tr("Ready"), yesNo(snapshot.controllerState->ready));
        addSummaryRow(
            m_summary,
            Tr::tr("Working counter"),
            Tr::tr("%1 / %2")
                .arg(snapshot.controllerState->actualWorkingCounter)
                .arg(snapshot.controllerState->expectedWorkingCounter));
        addSummaryRow(
            m_summary,
            Tr::tr("Distributed clocks locked"),
            yesNo(snapshot.controllerState->distributedClocksLocked));
        addSummaryRow(
            m_summary, Tr::tr("Bus operational"), yesNo(snapshot.controllerState->busOperational));
        addSummaryRow(
            m_summary,
            Tr::tr("Application active"),
            yesNo(snapshot.controllerState->applicationActive));
        addSummaryRow(m_summary, Tr::tr("Safe output"), yesNo(snapshot.controllerState->safeOutput));
        addSummaryRow(
            m_summary,
            Tr::tr("DC difference"),
            Tr::tr("%1 ns").arg(snapshot.controllerState->distributedClockDifferenceNs));
        addSummaryRow(
            m_summary,
            Tr::tr("Current faults"),
            QStringLiteral("0x%1").arg(snapshot.controllerState->currentFaults, 0, 16));
        addSummaryRow(
            m_summary,
            Tr::tr("Latched faults"),
            QStringLiteral("0x%1").arg(snapshot.controllerState->latchedFaults, 0, 16));
    }
    if (snapshot.package) {
        addSummaryRow(
            m_summary,
            Tr::tr("Active package"),
            Tr::tr("%1, generation %2")
                .arg(slotText(snapshot.package->activeSlot))
                .arg(snapshot.package->activeGeneration));
        addSummaryRow(
            m_summary,
            Tr::tr("Staged package"),
            Tr::tr("%1, generation %2")
                .arg(slotText(snapshot.package->stagedSlot))
                .arg(snapshot.package->stagedGeneration));
    }
    if (snapshot.firmware) {
        addSummaryRow(
            m_summary, Tr::tr("Firmware state"), firmwareStateText(snapshot.firmware->state));
        addSummaryRow(m_summary, Tr::tr("Firmware slot"), slotText(snapshot.firmware->activeSlot));
        addSummaryRow(
            m_summary,
            Tr::tr("Firmware generation"),
            QString::number(snapshot.firmware->generation));
    }
}

void CommunicationPage::updateChannels(const Data::ControllerConnectionSnapshot &snapshot)
{
    m_channels->clear();
    for (const Data::ControllerChannelStatus &channel : snapshot.channels) {
        const QString maximumPayload = channel.maximumPayloadBytes > 0
                                           ? Tr::tr("%1 bytes").arg(channel.maximumPayloadBytes)
                                           : Tr::tr("Not available");
        auto item = new QTreeWidgetItem(
            {channel.displayName.isEmpty() ? channel.id : channel.displayName,
             channelStateText(channel.state),
             maximumPayload,
             dateTimeText(channel.lastActivityAt),
             channel.detail});
        for (int column = 0; column < item->columnCount(); ++column)
            item->setToolTip(column, item->text(column));
        m_channels->addTopLevelItem(item);
    }
    m_channels->setVisible(!snapshot.channels.isEmpty());
}

void CommunicationPage::updateControllerControl(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    const Data::ControllerConnectionScope contextScope{m_context.projectId, m_context.nodeId};
    const bool matchingMasterContext = m_context.nodeKind == Core::WorkbenchNodeKind::Master
                                       && !contextScope.projectId.isNull()
                                       && !contextScope.masterId.isNull()
                                       && snapshot.scope == contextScope;
    const QList<std::pair<QToolButton *, Data::ControllerControlCommand>> buttons{
        {m_acquireControl, Data::ControllerControlCommand::AcquireControl},
        {m_enterConfiguration, Data::ControllerControlCommand::EnterConfigurationMode},
        {m_scanBus, Data::ControllerControlCommand::DiscoverTopology},
        {m_restorePackage, Data::ControllerControlCommand::RestoreActivePackage},
        {m_releaseControl, Data::ControllerControlCommand::ReleaseControl},
    };
    for (const auto &[button, command] : buttons) {
        button->setEnabled(matchingMasterContext && m_controller
                           && m_controller->canExecuteControllerControl(contextScope, command));
    }
}

void CommunicationPage::updateTopology(
    const std::optional<Data::ControllerTopologySnapshot> &topology)
{
    m_actualBus->clear();
    if (!topology) {
        m_topologySummary->clear();
        m_topologySummary->hide();
        m_actualBus->hide();
        return;
    }

    QString summary = Tr::tr("%1 responding device(s), %2 displayed")
                          .arg(topology->respondingCount)
                          .arg(topology->slaves.size());
    if (topology->discoveredAt.isValid()) {
        summary += Tr::tr(" — scanned %1").arg(dateTimeText(topology->discoveredAt));
    }
    if (topology->result)
        summary += Tr::tr(" — result %1").arg(topology->result);
    m_topologySummary->setText(summary);
    m_topologySummary->setToolTip(summary);
    m_topologySummary->show();
    m_actualBus->show();

    for (const Data::ControllerTopologySlave &slave : topology->slaves) {
        auto item = new QTreeWidgetItem(
            {QString::number(slave.position),
             hexadecimalValue(slave.stationAddress, 4),
             hexadecimalValue(slave.alState, 4),
             hexadecimalValue(slave.vendorId, 8),
             hexadecimalValue(slave.productCode, 8),
             hexadecimalValue(slave.revision, 8),
             hexadecimalValue(slave.serial, 8)});
        for (int column = 0; column < item->columnCount(); ++column)
            item->setToolTip(column, item->text(column));
        m_actualBus->addTopLevelItem(item);
    }
}

void CommunicationPage::executeControllerControl(Data::ControllerControlCommand command)
{
    if (!m_controller)
        return;
    const Data::ControllerConnectionScope contextScope{m_context.projectId, m_context.nodeId};
    Data::ControllerControlRequest request;
    request.command = command;
    const Utils::Result<> result = m_controller->executeControllerControl(contextScope, request);
    if (result)
        return;
    m_controller->writeControllerOutput(
        Tr::tr("Cannot control the controller: %1").arg(result.error()),
        ControllerOutputLevel::Error);
}

} // namespace EtherCAT::Workbench::Internal
