// Copyright (C) 2026 Kvell

#include "communicationpage.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>

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

static QString retryDispositionText(Data::ControllerRetryDisposition disposition)
{
    using Disposition = Data::ControllerRetryDisposition;
    switch (disposition) {
    case Disposition::Unknown:
        return Tr::tr("No retry guidance");
    case Disposition::Retryable:
        return Tr::tr("Retry is allowed");
    case Disposition::Reconnect:
        return Tr::tr("Reconnect before retrying");
    case Disposition::NotRetryable:
        return Tr::tr("Do not retry automatically");
    }
    return Tr::tr("No retry guidance");
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
    , m_banner(new Utils::InfoLabel(this))
    , m_safety(new QLabel(this))
    , m_provider(new QComboBox(this))
    , m_profile(new QComboBox(this))
    , m_endpoint(new QLabel(this))
    , m_connect(new QToolButton(this))
    , m_refresh(new QToolButton(this))
    , m_disconnect(new QToolButton(this))
    , m_summary(new QTreeWidget(this))
    , m_channels(new QTreeWidget(this))
    , m_error(new QLabel(this))
{
    setObjectName("EtherCATWorkbenchCommunicationPage");
    setAccessibleName(Tr::tr("Controller communication"));
    setAccessibleDescription(
        Tr::tr("Configures a controller adapter and shows its current read-only connection."));

    m_banner->setObjectName("EtherCATCommunicationBanner");
    m_banner->setFilled(true);
    m_safety->setObjectName("EtherCATCommunicationSafety");
    m_safety->setWordWrap(true);
    m_safety->setTextFormat(Qt::PlainText);
    m_safety->setText(
        Tr::tr(
            "Connect and Refresh are read-only. They do not acquire control, scan the bus, "
            "change controller state, or write configuration and outputs."));
    m_safety->setAccessibleName(Tr::tr("Read-only safety boundary"));

    auto providerLabel = new QLabel(Tr::tr("Controller adapter:"), this);
    providerLabel->setBuddy(m_provider);
    auto profileLabel = new QLabel(Tr::tr("Connection profile:"), this);
    profileLabel->setBuddy(m_profile);
    auto endpointLabel = new QLabel(Tr::tr("Endpoint:"), this);
    m_provider->setObjectName("EtherCATCommunicationProvider");
    m_provider->setAccessibleName(Tr::tr("Controller adapter"));
    m_profile->setObjectName("EtherCATCommunicationProfile");
    m_profile->setAccessibleName(Tr::tr("Connection profile"));
    m_endpoint->setObjectName("EtherCATCommunicationEndpoint");
    m_endpoint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_endpoint->setWordWrap(true);

    auto form = new QFormLayout;
    form->setContentsMargins(QMargins());
    form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    form->addRow(providerLabel, m_provider);
    form->addRow(profileLabel, m_profile);
    form->addRow(endpointLabel, m_endpoint);

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

    m_error->setObjectName("EtherCATCommunicationError");
    m_error->setWordWrap(true);
    m_error->setTextFormat(Qt::PlainText);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_error->setAccessibleName(Tr::tr("Controller connection error"));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_banner);
    layout->addWidget(m_safety);
    layout->addLayout(form);
    layout->addLayout(buttonLayout);
    layout->addWidget(m_summary, 1);
    layout->addWidget(m_channels, 1);
    layout->addWidget(m_error);

    connect(m_provider, &QComboBox::currentIndexChanged, this, &CommunicationPage::selectProvider);
    connect(m_profile, &QComboBox::currentIndexChanged, this, &CommunicationPage::selectProfile);
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
        m_banner->setType(Utils::InfoLabel::Warning);
        m_banner->setText(Tr::tr("Select a valid EtherCAT Master."));
        m_provider->setEnabled(false);
        m_profile->setEnabled(false);
        m_endpoint->setText(Tr::tr("Not available"));
        m_summary->clear();
        m_channels->clear();
        m_channels->hide();
        m_error->clear();
        m_error->hide();
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
    m_provider->setEnabled(!locked && !providers.isEmpty());
    m_profile->setEnabled(!locked && provider && !profiles.isEmpty());
    updateSummary(snapshot, provider);
    updateChannels(snapshot);

    QString endpoint;
    const auto profile
        = std::find_if(profiles.cbegin(), profiles.cend(), [&selection](const auto &candidate) {
              return candidate.id == selection.profileId;
          });
    if (profile != profiles.cend())
        endpoint = profile->endpointSummary;
    if (endpoint.isEmpty())
        endpoint = snapshot.endpointSummary;
    m_endpoint->setText(endpoint.isEmpty() ? Tr::tr("Not available") : endpoint);
    m_endpoint->setToolTip(m_endpoint->text());

    const QString state = connectionStateText(snapshot.state);
    const bool activeSnapshot = snapshot.state != Data::ControllerConnectionState::Disconnected
                                && snapshot.state != Data::ControllerConnectionState::Failed;
    const bool disconnectableSnapshot = snapshot.state
                                            != Data::ControllerConnectionState::Disconnected
                                        && snapshot.state
                                               != Data::ControllerConnectionState::Disconnecting;
    const bool snapshotHasScope = !snapshot.scope.projectId.isNull()
                                  || !snapshot.scope.masterId.isNull();
    const bool foreignSnapshot = snapshotHasScope && snapshot.scope != scope;
    const bool orphanedSnapshot = disconnectableSnapshot
                                  && !m_controller->controllerConnectionProjectIsOpen(
                                      snapshot.scope);
    const auto showSnapshotEvidence = [this, &snapshot, &state] {
        switch (snapshot.state) {
        case Data::ControllerConnectionState::Connected:
            m_banner->setType(Utils::InfoLabel::Ok);
            break;
        case Data::ControllerConnectionState::Degraded:
            m_banner->setType(Utils::InfoLabel::Warning);
            break;
        default:
            m_banner->setType(Utils::InfoLabel::Information);
            break;
        }
        QString evidence = Tr::tr("No live controller evidence");
        if (snapshot.state == Data::ControllerConnectionState::Connected
            || snapshot.state == Data::ControllerConnectionState::Degraded) {
            if (snapshot.mock) {
                evidence = Tr::tr("Mock");
            } else if (snapshot.readOnly) {
                evidence = Tr::tr("Read-only real controller");
            } else {
                evidence = Tr::tr("Controlled real controller");
                m_banner->setType(Utils::InfoLabel::Warning);
            }
        }
        m_banner->setText(Tr::tr("%1 — %2").arg(state, evidence));
    };
    if (!selection.providerExplicitlySelected || !selection.providerId.isValid()) {
        m_banner->setType(Utils::InfoLabel::Information);
        m_banner->setText(Tr::tr("Select a controller adapter."));
    } else if (!provider) {
        m_banner->setType(Utils::InfoLabel::Warning);
        m_banner->setText(Tr::tr("The selected controller adapter is no longer available."));
    } else if (!provider->isAvailable()) {
        m_banner->setType(Utils::InfoLabel::Warning);
        m_banner->setText(Tr::tr("%1 — the controller adapter is unavailable.").arg(state));
    } else if (foreignSnapshot && orphanedSnapshot) {
        m_banner->setType(Utils::InfoLabel::Warning);
        m_banner->setText(
            Tr::tr(
                "This adapter is still connected for a Project that is no longer open. Click "
                "Disconnect to finish cleanup."));
    } else if (foreignSnapshot && activeSnapshot) {
        m_banner->setType(Utils::InfoLabel::Warning);
        m_banner->setText(
            Tr::tr(
                "This adapter is connected for another EtherCAT Master. Select that Master to "
                "refresh or disconnect it."));
    } else if (activeSnapshot) {
        showSnapshotEvidence();
    } else if (foreignSnapshot) {
        m_banner->setType(Utils::InfoLabel::Warning);
        m_banner->setText(
            Tr::tr("This adapter is showing a historical snapshot for another EtherCAT Master."));
    } else if (snapshot.state == Data::ControllerConnectionState::Failed) {
        m_banner->setType(Utils::InfoLabel::Error);
        m_banner->setText(Tr::tr("%1 — the read-only connection failed.").arg(state));
    } else if (!selection.profileExplicitlySelected || selection.profileId.isNull()) {
        m_banner->setType(Utils::InfoLabel::Information);
        m_banner->setText(Tr::tr("Select a connection profile."));
    } else if (profile == profiles.cend() || !profile->configured || !profile->supported) {
        m_banner->setType(Utils::InfoLabel::Warning);
        const QString issue = profile != profiles.cend() ? profile->configurationIssue : QString();
        m_banner->setText(
            issue.isEmpty() ? Tr::tr("The selected connection profile is unavailable.") : issue);
    } else {
        showSnapshotEvidence();
    }

    if (snapshot.lastError) {
        QStringList errorLines;
        if (!snapshot.lastError->summary.isEmpty())
            errorLines.append(snapshot.lastError->summary);
        if (!snapshot.lastError->detail.isEmpty())
            errorLines.append(snapshot.lastError->detail);
        QString code = snapshot.lastError->codeName;
        if (snapshot.lastError->code)
            code = code.isEmpty() ? QString::number(*snapshot.lastError->code)
                                  : Tr::tr("%1 (%2)").arg(code).arg(*snapshot.lastError->code);
        if (!code.isEmpty())
            errorLines.append(Tr::tr("Code: %1").arg(code));
        if (!snapshot.lastError->channelId.isEmpty()) {
            errorLines.append(Tr::tr("Channel: %1").arg(snapshot.lastError->channelId));
        }
        QString retry = retryDispositionText(snapshot.lastError->retryDisposition);
        if (snapshot.lastError->retryAfterMs) {
            retry = Tr::tr("%1 after %2 ms").arg(retry).arg(*snapshot.lastError->retryAfterMs);
        }
        errorLines.append(Tr::tr("Recovery: %1").arg(retry));
        const QString errorText = errorLines.join('\n');
        m_error->setText(errorText);
        m_error->setToolTip(errorText);
        m_error->show();
    } else {
        m_error->clear();
        m_error->hide();
    }
}

void CommunicationPage::selectProvider(int index)
{
    if (m_updating || !m_controller || index < 0)
        return;
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Utils::Result<> result = m_controller->selectControllerConnectionProvider(
        scope, Utils::Id::fromSetting(m_provider->itemData(index)));
    if (!result) {
        refresh();
        m_banner->setType(Utils::InfoLabel::Error);
        m_banner->setText(result.error());
    }
}

void CommunicationPage::selectProfile(int index)
{
    if (m_updating || !m_controller || index < 0)
        return;
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Utils::Result<> result = m_controller->selectControllerConnectionProfile(
        scope, m_profile->itemData(index).value<Data::NodeId>());
    if (!result) {
        refresh();
        m_banner->setType(Utils::InfoLabel::Error);
        m_banner->setText(result.error());
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
        hasConnectionEvidence
            ? (snapshot.readOnly ? Tr::tr("Read-only") : Tr::tr("Controlled"))
            : Tr::tr("Not available"));
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

} // namespace EtherCAT::Workbench::Internal
