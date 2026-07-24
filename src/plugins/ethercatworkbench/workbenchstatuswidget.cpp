// Copyright (C) 2026 Kvell

#include "workbenchstatuswidget.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/modemanager.h>

#include <ethercatcore/providers.h>
#include <ethercatcore/stateservice.h>

#include <utils/utilsicons.h>

#include <QAction>
#include <QMenu>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>

#include <algorithm>
#include <optional>

namespace EtherCAT::Workbench::Internal {

static QIcon statusIcon(Core::StatusSeverity severity)
{
    switch (severity) {
    case Core::StatusSeverity::Ready:
        return Utils::Icons::OK.icon();
    case Core::StatusSeverity::Busy:
        return Utils::Icons::RUN_SMALL_TOOLBAR.icon();
    case Core::StatusSeverity::Warning:
        return Utils::Icons::WARNING_TOOLBAR.icon();
    case Core::StatusSeverity::Error:
        return Utils::Icons::CRITICAL_TOOLBAR.icon();
    }
    return Utils::Icons::NOTLOADED.icon();
}

static QString statusText(Core::StatusSeverity severity)
{
    switch (severity) {
    case Core::StatusSeverity::Ready:
        return Tr::tr("MOCK Ready");
    case Core::StatusSeverity::Busy:
        return Tr::tr("MOCK Busy");
    case Core::StatusSeverity::Warning:
        return Tr::tr("MOCK Warning");
    case Core::StatusSeverity::Error:
        return Tr::tr("MOCK Fault");
    }
    return Tr::tr("Unknown");
}

static QString diagnosticsStatusText(Core::StatusSeverity severity, bool mock)
{
    if (mock)
        return statusText(severity);
    switch (severity) {
    case Core::StatusSeverity::Ready:
        return Tr::tr("Diagnostics Ready");
    case Core::StatusSeverity::Busy:
        return Tr::tr("Diagnostics Busy");
    case Core::StatusSeverity::Warning:
        return Tr::tr("Diagnostics Warning");
    case Core::StatusSeverity::Error:
        return Tr::tr("Diagnostics Fault");
    }
    return Tr::tr("Unknown");
}

static QString runModeName(Data::DiagnosticsRunMode mode)
{
    switch (mode) {
    case Data::DiagnosticsRunMode::Offline:
        return Tr::tr("Offline");
    case Data::DiagnosticsRunMode::Config:
        return Tr::tr("Config");
    case Data::DiagnosticsRunMode::FreeRun:
        return Tr::tr("FreeRun");
    case Data::DiagnosticsRunMode::Run:
        return Tr::tr("Run");
    }
    return {};
}

static QString etherCATStateName(Data::EtherCATState state)
{
    switch (state) {
    case Data::EtherCATState::Unknown:
        return Tr::tr("Unknown");
    case Data::EtherCATState::Init:
        return Tr::tr("INIT");
    case Data::EtherCATState::PreOperational:
        return Tr::tr("PREOP");
    case Data::EtherCATState::SafeOperational:
        return Tr::tr("SAFEOP");
    case Data::EtherCATState::Operational:
        return Tr::tr("OP");
    case Data::EtherCATState::Bootstrap:
        return Tr::tr("BOOT");
    }
    return {};
}

static QString streamStateName(Data::DiagnosticsStreamState state)
{
    switch (state) {
    case Data::DiagnosticsStreamState::Stopped:
        return Tr::tr("Stopped");
    case Data::DiagnosticsStreamState::Starting:
        return Tr::tr("Starting");
    case Data::DiagnosticsStreamState::Running:
        return Tr::tr("Running");
    case Data::DiagnosticsStreamState::Stopping:
        return Tr::tr("Stopping");
    case Data::DiagnosticsStreamState::Failed:
        return Tr::tr("Failed");
    }
    return {};
}

static QString diagnosticsModeText(const DiagnosticsStatusPresentation &diagnostics)
{
    if (!diagnostics.provider.isAvailable()
        || diagnostics.streamState != Data::DiagnosticsStreamState::Running
        || !diagnostics.runMode || !diagnostics.masterState) {
        return {};
    }
    return Tr::tr("%1 %2 / %3")
        .arg(
            diagnostics.mock ? Tr::tr("MOCK") : Tr::tr("Diagnostics"),
            runModeName(*diagnostics.runMode),
            etherCATStateName(*diagnostics.masterState));
}

static std::optional<Core::StatusSeverity> diagnosticsSeverity(
    const DiagnosticsStatusPresentation &diagnostics)
{
    if (!diagnostics.provider.isAvailable())
        return std::nullopt;
    switch (diagnostics.streamState) {
    case Data::DiagnosticsStreamState::Stopped:
        return std::nullopt;
    case Data::DiagnosticsStreamState::Starting:
    case Data::DiagnosticsStreamState::Stopping:
        return Core::StatusSeverity::Busy;
    case Data::DiagnosticsStreamState::Failed:
        return Core::StatusSeverity::Error;
    case Data::DiagnosticsStreamState::Running:
        if (!diagnostics.runMode || !diagnostics.masterState)
            return Core::StatusSeverity::Busy;
        if (diagnostics.masterHasError || diagnostics.activeAlarmCount > 0)
            return Core::StatusSeverity::Warning;
        return Core::StatusSeverity::Ready;
    }
    return std::nullopt;
}

static QString diagnosticsDetails(const DiagnosticsStatusPresentation &diagnostics)
{
    if (!diagnostics.provider.isAvailable()
        || (diagnostics.streamState == Data::DiagnosticsStreamState::Stopped
            && !diagnostics.runMode)) {
        return {};
    }

    QStringList details;
    const QString mode = diagnosticsModeText(diagnostics);
    details.append(
        mode.isEmpty()
            ? Tr::tr("%1 — Diagnostics %2")
                  .arg(diagnostics.provider.displayName, streamStateName(diagnostics.streamState))
            : Tr::tr("%1 — %2").arg(diagnostics.provider.displayName, mode));
    details.append(
        diagnostics.mock
            ? Tr::tr("Local Mock diagnostics only. No controller or physical hardware is connected.")
            : Tr::tr(
                  "Provider-reported diagnostics only. The Workbench does not infer a controller "
                  "or physical-hardware connection."));
    if (!diagnostics.request.projectId.isNull())
        details.append(Tr::tr("Project: %1").arg(diagnostics.request.projectId.toString()));
    if (!diagnostics.request.masterId.isNull())
        details.append(Tr::tr("Master: %1").arg(diagnostics.request.masterId.toString()));
    return details.join('\n');
}

struct ControllerConnectionStatusPresentation
{
    QString providerName;
    Data::ControllerConnectionSnapshot snapshot;
};

static int controllerConnectionPriority(Data::ControllerConnectionState state)
{
    switch (state) {
    case Data::ControllerConnectionState::Disconnected:
        return 0;
    case Data::ControllerConnectionState::Connected:
        return 1;
    case Data::ControllerConnectionState::Connecting:
    case Data::ControllerConnectionState::Handshaking:
    case Data::ControllerConnectionState::Disconnecting:
        return 2;
    case Data::ControllerConnectionState::Degraded:
        return 3;
    case Data::ControllerConnectionState::Failed:
        return 4;
    }
    return 0;
}

static QString controllerConnectionStateText(Data::ControllerConnectionState state)
{
    switch (state) {
    case Data::ControllerConnectionState::Disconnected:
        return Tr::tr("Disconnected");
    case Data::ControllerConnectionState::Connecting:
        return Tr::tr("Connecting");
    case Data::ControllerConnectionState::Handshaking:
        return Tr::tr("Handshaking");
    case Data::ControllerConnectionState::Connected:
        return Tr::tr("Connected");
    case Data::ControllerConnectionState::Degraded:
        return Tr::tr("Degraded");
    case Data::ControllerConnectionState::Disconnecting:
        return Tr::tr("Disconnecting");
    case Data::ControllerConnectionState::Failed:
        return Tr::tr("Failed");
    }
    return {};
}

static std::optional<Core::StatusSeverity> controllerConnectionSeverity(
    const std::optional<ControllerConnectionStatusPresentation> &connection)
{
    if (!connection)
        return std::nullopt;
    switch (connection->snapshot.state) {
    case Data::ControllerConnectionState::Disconnected:
        return std::nullopt;
    case Data::ControllerConnectionState::Connected:
        return Core::StatusSeverity::Ready;
    case Data::ControllerConnectionState::Connecting:
    case Data::ControllerConnectionState::Handshaking:
    case Data::ControllerConnectionState::Disconnecting:
        return Core::StatusSeverity::Busy;
    case Data::ControllerConnectionState::Degraded:
        return Core::StatusSeverity::Warning;
    case Data::ControllerConnectionState::Failed:
        return Core::StatusSeverity::Error;
    }
    return std::nullopt;
}

static std::optional<ControllerConnectionStatusPresentation> controllerConnectionStatus(
    WorkbenchController *controller)
{
    if (!controller)
        return std::nullopt;

    std::optional<ControllerConnectionStatusPresentation> result;
    int resultPriority = 0;
    for (Core::ControllerConnectionProvider *provider :
         controller->controllerConnectionProviders()) {
        const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
        const int priority = controllerConnectionPriority(snapshot.state);
        if (priority <= resultPriority)
            continue;
        QString providerName = provider->displayName().trimmed();
        if (providerName.isEmpty())
            providerName = Tr::tr("Unnamed controller adapter");
        result = ControllerConnectionStatusPresentation{providerName, snapshot};
        resultPriority = priority;
    }
    return result;
}

static QString controllerConnectionSummary(
    const ControllerConnectionStatusPresentation &connection)
{
    return Tr::tr("Controller %1").arg(controllerConnectionStateText(connection.snapshot.state));
}

static QString controllerConnectionDetails(
    const ControllerConnectionStatusPresentation &connection)
{
    const Data::ControllerConnectionSnapshot &snapshot = connection.snapshot;
    QStringList details{
        Tr::tr("%1 — %2")
            .arg(connection.providerName, controllerConnectionStateText(snapshot.state)),
    };
    if (snapshot.state == Data::ControllerConnectionState::Connected
        || snapshot.state == Data::ControllerConnectionState::Degraded) {
        details.append(
            snapshot.mock
                ? Tr::tr("Mock controller connection")
                : (snapshot.readOnly ? Tr::tr("Read-only real controller")
                                     : Tr::tr("Controlled real controller")));
    }
    if (!snapshot.endpointSummary.isEmpty())
        details.append(Tr::tr("Endpoint: %1").arg(snapshot.endpointSummary));
    return details.join('\n');
}

static const Core::StatusEntry *primaryStatus(const QList<Core::StatusEntry> &statuses)
{
    if (statuses.isEmpty())
        return nullptr;
    return &*std::max_element(
        statuses.cbegin(), statuses.cend(), [](const auto &left, const auto &right) {
            return int(left.severity) < int(right.severity);
        });
}

WorkbenchStatusWidget::WorkbenchStatusWidget(
    Core::StateService *stateService, WorkbenchController *controller, QWidget *parent)
    : QToolButton(parent)
    , m_stateService(stateService)
    , m_controller(controller)
    , m_menu(new QMenu(this))
{
    setObjectName("EtherCATWorkbenchStatus");
    setAutoRaise(true);
    setPopupMode(QToolButton::InstantPopup);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const int iconExtent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    setIconSize(QSize(iconExtent, iconExtent));
    setAccessibleName(Tr::tr("EtherCAT workbench status"));
    setMenu(m_menu);

    connect(
        ::Core::ModeManager::instance(),
        &::Core::ModeManager::currentModeChanged,
        this,
        [this](Utils::Id modeId) { setVisible(modeId == Constants::MODE_ID); });
    setVisible(::Core::ModeManager::currentModeId() == Constants::MODE_ID);

    if (m_stateService) {
        connect(m_stateService, &Core::StateService::statusChanged, this, [this] {
            updateStatus();
        });
    }
    if (m_controller) {
        connect(m_controller, &WorkbenchController::diagnosticsStatusChanged, this, [this] {
            updateStatus();
        });
        connect(m_controller, &WorkbenchController::controllerConnectionChanged, this, [this] {
            updateStatus();
        });
    }
    updateStatus();
}

void WorkbenchStatusWidget::updateStatus()
{
    const QList<Core::StatusEntry> statuses = m_stateService ? m_stateService->statuses()
                                                             : QList<Core::StatusEntry>();
    const Core::StatusEntry *primary = primaryStatus(statuses);
    const DiagnosticsStatusPresentation diagnostics
        = m_controller ? m_controller->diagnosticsStatusPresentation()
                       : DiagnosticsStatusPresentation();
    const std::optional<ControllerConnectionStatusPresentation> connection
        = controllerConnectionStatus(m_controller);
    const std::optional<Core::StatusSeverity> connectionSeverity
        = controllerConnectionSeverity(connection);
    const std::optional<Core::StatusSeverity> providerSeverity = diagnosticsSeverity(diagnostics);
    std::optional<Core::StatusSeverity> severity = primary
                                                       ? std::optional(primary->severity)
                                                       : std::nullopt;
    bool connectionDefinesSeverity = false;
    if (connectionSeverity && (!severity || int(*connectionSeverity) >= int(*severity))) {
        severity = connectionSeverity;
        connectionDefinesSeverity = true;
    }
    bool providerDefinesSeverity = false;
    if (providerSeverity && (!severity || int(*providerSeverity) > int(*severity))) {
        severity = providerSeverity;
        connectionDefinesSeverity = false;
        providerDefinesSeverity = true;
    } else if (providerSeverity && severity && *providerSeverity == *severity
               && !diagnostics.mock && !connectionDefinesSeverity) {
        connectionDefinesSeverity = false;
        providerDefinesSeverity = true;
    }
    const QString mode = diagnosticsModeText(diagnostics);
    const QString providerDetails = diagnosticsDetails(diagnostics);
    const QString connectionDetails
        = connection ? controllerConnectionDetails(*connection) : QString();

    m_menu->clear();
    if (!severity) {
        const QString summary = Tr::tr("Offline");
        const QString details = Tr::tr(
            "No local Mock scan or diagnostics workflow is active. No controller is connected.");
        setText(summary);
        setIcon(Utils::Icons::NOTLOADED.icon());
        setToolTip(details);
        setAccessibleDescription(details);
        QAction *offline = m_menu->addAction(Utils::Icons::NOTLOADED.icon(), summary);
        offline->setToolTip(details);
        offline->setEnabled(false);
        updateMinimumWidth();
        return;
    }

    if (connectionDefinesSeverity)
        setText(controllerConnectionSummary(*connection));
    else if (*severity == Core::StatusSeverity::Ready && !mode.isEmpty())
        setText(mode);
    else if (providerDefinesSeverity)
        setText(diagnosticsStatusText(*severity, diagnostics.mock));
    else
        setText(statusText(*severity));
    setIcon(statusIcon(*severity));

    QStringList details;
    if (connection) {
        details.append(connectionDetails);
        QAction *connectionEntry = m_menu->addAction(
            statusIcon(*connectionSeverity), controllerConnectionSummary(*connection));
        connectionEntry->setToolTip(connectionDetails);
        connectionEntry->setEnabled(false);
    }
    if (!providerDetails.isEmpty()) {
        details.append(providerDetails);
        const QString providerSummary
            = mode.isEmpty()
                  ? Tr::tr("Diagnostics %1").arg(streamStateName(diagnostics.streamState))
                  : mode;
        const QIcon providerIcon = providerSeverity
                                       ? statusIcon(*providerSeverity)
                                       : Utils::Icons::NOTLOADED.icon();
        QAction *providerEntry = m_menu->addAction(providerIcon, providerSummary);
        providerEntry->setToolTip(providerDetails);
        providerEntry->setEnabled(false);
    }
    for (const Core::StatusEntry &status : statuses) {
        const QString line = status.details.isEmpty()
                                 ? status.summary
                                 : Tr::tr("%1 — %2").arg(status.summary, status.details);
        details.append(line);
        QAction *entry = m_menu->addAction(statusIcon(status.severity), status.summary);
        entry->setToolTip(status.details);
        entry->setEnabled(false);
    }
    const QString tooltip = details.join('\n');
    setToolTip(tooltip);
    setAccessibleDescription(tooltip);
    updateMinimumWidth();
}

void WorkbenchStatusWidget::updateMinimumWidth()
{
    setMinimumWidth(sizeHint().width());
    updateGeometry();
}

} // namespace EtherCAT::Workbench::Internal
