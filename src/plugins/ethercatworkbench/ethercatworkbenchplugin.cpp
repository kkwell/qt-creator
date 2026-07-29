// Copyright (C) 2026 Kvell

#include "builtinpropertypages.h"
#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#ifdef WITH_TESTS
#include "ethercatworkbenchtests.h"
#endif
#include "workbenchautomationservice.h"
#include "workbenchcontroller.h"
#include "workbenchmode.h"
#include "workbenchnavigation.h"
#include "workbenchstatuswidget.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/statusbarmanager.h>

#include <debugger/debuggerconstants.h>

#include <ethercatcore/providers.h>
#include <ethercatcore/selectionservice.h>
#include <ethercatcore/stateservice.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorericons.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace EtherCAT::Workbench::Internal {

static QString quickConnectionStateName(Data::ControllerConnectionState state)
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

static QString quickServiceStateName(Data::ControllerServiceState state)
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

static QString quickControllerStateDescription(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    QStringList details{
        Tr::tr("Connection: %1").arg(quickConnectionStateName(snapshot.state)),
    };
    if (snapshot.controllerState) {
        details.append(
            Tr::tr("Service: %1")
                .arg(quickServiceStateName(snapshot.controllerState->serviceState)));
    }
    if (snapshot.session) {
        details.append(
            snapshot.session->ownsControlLease
                ? Tr::tr("Control: exclusive")
                : snapshot.session->controlLeaseOwnerSessionId
                      ? Tr::tr("Control: owned by another session")
                      : Tr::tr("Control: not acquired"));
    }
    return details.join(QStringLiteral("; "));
}

class EtherCATWorkbenchPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATWorkbench.json")

public:
    ~EtherCATWorkbenchPlugin() final;

    void initialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void setupActions();
    void setupQuickControllerActions();
    std::optional<Data::ControllerConnectionScope> activeControllerControlScope() const;
    QString activeControllerControlScopeUnavailableReason() const;
    void updateControllerControlContext();
    void updateQuickControllerActions();
    void triggerQuickControllerAction(ControllerQuickControlAction action);
    void scheduleProjectPresentation();
    void activateProjectPresentation();
    void shutdown();

    std::unique_ptr<WorkbenchController> m_controller;
    std::unique_ptr<WorkbenchAutomationService> m_automationService;
    std::unique_ptr<BuiltinPropertyPageProvider> m_builtinPages;
    std::unique_ptr<WorkbenchNavigationFactory> m_navigationFactory;
    std::unique_ptr<WorkbenchMode> m_mode;
    QPointer<WorkbenchStatusWidget> m_statusWidget;
    QPointer<QAction> m_runControllerAction;
    QPointer<QAction> m_debugControllerAction;
    QPointer<QAction> m_stopControllerAction;
    bool m_controllerControlContextActive = false;
    bool m_projectPresentationPending = false;
    bool m_projectPresentationScheduled = false;
    bool m_providerRegistered = false;
    bool m_automationServiceRegistered = false;
    bool m_shuttingDown = false;
};

EtherCATWorkbenchPlugin::~EtherCATWorkbenchPlugin()
{
    shutdown();
}

void EtherCATWorkbenchPlugin::initialize()
{
    Core::StateService *stateService
        = ExtensionSystem::PluginManager::getObject<Core::StateService>();
    QTC_ASSERT(stateService, return);

    m_controller = std::make_unique<WorkbenchController>();
    m_automationService = std::make_unique<WorkbenchAutomationService>(m_controller.get());
    ExtensionSystem::PluginManager::addObject(m_automationService.get());
    m_automationServiceRegistered = true;
    connect(
        m_controller.get(),
        &WorkbenchController::controllerOutputRequested,
        this,
        [](const QString &message, ControllerOutputLevel level) {
            const Utils::Id channelId(Constants::CONTROLLER_OUTPUT_CHANNEL_ID);
            const Utils::OutputFormat format
                = level == ControllerOutputLevel::Error
                      ? Utils::ErrorMessageFormat
                      : Utils::NormalMessageFormat;
            ProjectExplorer::ProjectExplorerPlugin::postApplicationOutput(
                channelId, Tr::tr("Output"), message + QLatin1Char('\n'), format);
            ProjectExplorer::ProjectExplorerPlugin::showApplicationOutput(channelId);
        });
    m_builtinPages = std::make_unique<BuiltinPropertyPageProvider>(m_controller.get());
    ExtensionSystem::PluginManager::addObject(m_builtinPages.get());
    m_providerRegistered = true;

    m_navigationFactory = std::make_unique<WorkbenchNavigationFactory>(m_controller.get());
    m_mode = std::make_unique<WorkbenchMode>(m_controller.get());
    setupActions();
    if (!m_controller->projectService()->projects().isEmpty()) {
        m_projectPresentationPending = true;
        scheduleProjectPresentation();
    }
    m_statusWidget = new WorkbenchStatusWidget(stateService, m_controller.get());
    ::Core::StatusBarManager::addStatusBarWidget(
        m_statusWidget,
        ::Core::StatusBarManager::LastLeftAligned,
        ::Core::Context(Constants::CONTEXT_ID));

#ifdef WITH_TESTS
    addTest<EtherCATWorkbenchTests>();
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATWorkbenchPlugin::aboutToShutdown()
{
    shutdown();
    return SynchronousShutdown;
}

void EtherCATWorkbenchPlugin::setupQuickControllerActions()
{
    m_runControllerAction = new QAction(
        ProjectExplorer::Icons::RUN.icon(), Tr::tr("Run Controller"), this);
    m_runControllerAction->setObjectName("EtherCATWorkbenchRunController");
    ::Core::Command *runCommand = ::Core::ActionManager::registerAction(
        m_runControllerAction,
        ProjectExplorer::Constants::RUN,
        ::Core::Context(Constants::CONTROLLER_CONTROL_CONTEXT_ID));
    runCommand->setAttribute(::Core::Command::CA_UpdateText);
    runCommand->setAttribute(::Core::Command::CA_UpdateIcon);

    m_debugControllerAction = new QAction(
        ProjectExplorer::Icons::DEBUG_START.icon(), Tr::tr("Pause / Resume Controller"), this);
    m_debugControllerAction->setObjectName("EtherCATWorkbenchDebugController");
    ::Core::Command *debugCommand = ::Core::ActionManager::registerAction(
        m_debugControllerAction,
        Constants::DEBUG_ACTION_ID,
        ::Core::Context(Constants::CONTROLLER_CONTROL_CONTEXT_ID));
    debugCommand->setAttribute(::Core::Command::CA_UpdateText);
    debugCommand->setAttribute(::Core::Command::CA_UpdateIcon);

    m_stopControllerAction
        = new QAction(Utils::Icons::STOP_SMALL.icon(), Tr::tr("Stop Controller"), this);
    m_stopControllerAction->setObjectName("EtherCATWorkbenchControlledStop");
    ::Core::Command *stopCommand = ::Core::ActionManager::registerAction(
        m_stopControllerAction,
        Constants::CONTROLLED_STOP_ACTION_ID,
        ::Core::Context(Constants::CONTROLLER_CONTROL_CONTEXT_ID));
    stopCommand->setAttribute(::Core::Command::CA_Hide);
    stopCommand->setAttribute(::Core::Command::CA_UpdateText);
    stopCommand->setAttribute(::Core::Command::CA_UpdateIcon);
    ::Core::ModeManager::addAction(stopCommand->action(), 80);

    connect(
        m_runControllerAction,
        &QAction::triggered,
        this,
        [this] { triggerQuickControllerAction(ControllerQuickControlAction::Run); });
    connect(
        m_debugControllerAction,
        &QAction::triggered,
        this,
        [this] { triggerQuickControllerAction(ControllerQuickControlAction::Debug); });
    connect(
        m_stopControllerAction,
        &QAction::triggered,
        this,
        [this] { triggerQuickControllerAction(ControllerQuickControlAction::Stop); });

    connect(
        m_controller.get(),
        &WorkbenchController::controllerConnectionChanged,
        this,
        &EtherCATWorkbenchPlugin::updateQuickControllerActions);
    connect(
        m_controller->projectService(),
        &Core::ProjectService::projectAdded,
        this,
        [this] {
            updateQuickControllerActions();
            m_projectPresentationPending = true;
            scheduleProjectPresentation();
        });
    connect(
        m_controller->projectService(),
        &Core::ProjectService::projectChanged,
        this,
        [this] {
            updateQuickControllerActions();
            if (m_projectPresentationPending)
                scheduleProjectPresentation();
        });
    connect(
        m_controller->projectService(),
        &Core::ProjectService::activeProjectChanged,
        this,
        [this](const Data::NodeId &, const Data::NodeId &currentProjectId) {
            if (!currentProjectId.isNull()) {
                m_projectPresentationPending = true;
                scheduleProjectPresentation();
            }
        });
    connect(
        m_controller->projectService(),
        &Core::ProjectService::projectAboutToBeRemoved,
        this,
        [this] {
            QMetaObject::invokeMethod(
                this, &EtherCATWorkbenchPlugin::updateQuickControllerActions, Qt::QueuedConnection);
        });
    connect(
        m_controller->selectionService(),
        &Core::SelectionService::currentNodeChanged,
        this,
        [this] { updateQuickControllerActions(); });
    connect(
        ::Core::ModeManager::instance(),
        &::Core::ModeManager::currentModeChanged,
        this,
        [this] { updateQuickControllerActions(); });
    connect(
        ProjectExplorer::ProjectManager::instance(),
        &ProjectExplorer::ProjectManager::projectAdded,
        this,
        [this] {
            QMetaObject::invokeMethod(
                this, &EtherCATWorkbenchPlugin::updateQuickControllerActions, Qt::QueuedConnection);
        });
    connect(
        ProjectExplorer::ProjectManager::instance(),
        &ProjectExplorer::ProjectManager::projectRemoved,
        this,
        [this] { updateQuickControllerActions(); });
    updateQuickControllerActions();
}

void EtherCATWorkbenchPlugin::scheduleProjectPresentation()
{
    if (m_shuttingDown || m_projectPresentationScheduled)
        return;
    m_projectPresentationScheduled = true;
    QTimer::singleShot(0, this, [this] { activateProjectPresentation(); });
}

void EtherCATWorkbenchPlugin::activateProjectPresentation()
{
    m_projectPresentationScheduled = false;
    if (m_shuttingDown || !m_controller || !m_controller->projectService()
        || m_controller->projectService()->projects().isEmpty()) {
        return;
    }

    Core::ProjectService *projectService = m_controller->projectService();
    Core::SelectionService *selectionService = m_controller->selectionService();
    const QList<Data::ProjectSnapshot> projects = projectService->projects();
    Data::NodeId projectId = projectService->activeProjectId();
    if (projectId.isNull() && projects.size() == 1)
        projectId = projects.constFirst().id;
    const auto project = std::find_if(
        projects.cbegin(), projects.cend(), [&projectId](const Data::ProjectSnapshot &candidate) {
            return candidate.valid && candidate.id == projectId;
        });
    if (project == projects.cend())
        return;

    const Core::PropertyPageContext currentContext
        = selectionService
              ? m_controller->treeModel()->contextForNodeId(selectionService->currentNodeId())
              : Core::PropertyPageContext();
    Data::NodeId presentationNodeId
        = currentContext.projectId == projectId ? currentContext.nodeId : Data::NodeId();
    if (selectionService && currentContext.projectId != projectId) {
        const auto master = std::find_if(
            project->nodes.cbegin(),
            project->nodes.cend(),
            [](const Data::ProjectNodeSnapshot &node) {
                return node.kind == Data::ProjectNodeKind::Master;
            });
        if (master != project->nodes.cend())
            presentationNodeId = master->id;
    }
    m_projectPresentationPending = false;

    ::Core::ModeManager::activateMode(Constants::MODE_ID);
    if (selectionService && !presentationNodeId.isNull()
        && selectionService->currentNodeId() != presentationNodeId) {
        selectionService->setCurrentNodeId(presentationNodeId);
    }

    const auto hideMode = [](const Utils::Id &modeId) {
        const Utils::Id visibilityActionId
            = modeId.withPrefix("QtCreator.Modes.View.");
        ::Core::Command *command = ::Core::ActionManager::command(visibilityActionId);
        QAction *action = command ? command->action() : nullptr;
        if (action && action->isCheckable() && action->isChecked())
            action->trigger();
    };
    for (const Utils::Id &modeId :
         {Utils::Id(::Core::Constants::MODE_EDIT),
          Utils::Id(::Core::Constants::MODE_DESIGN),
          Utils::Id(::Debugger::Constants::MODE_DEBUG),
          Utils::Id(ProjectExplorer::Constants::MODE_SESSION),
          Utils::Id(::Core::Constants::MODE_EASYBOARD)}) {
        hideMode(modeId);
    }
}

std::optional<Data::ControllerConnectionScope>
EtherCATWorkbenchPlugin::activeControllerControlScope() const
{
    return m_controller ? m_controller->quickControllerControlScope() : std::nullopt;
}

QString EtherCATWorkbenchPlugin::activeControllerControlScopeUnavailableReason() const
{
    return m_controller ? m_controller->quickControllerControlScopeUnavailableReason()
                        : Tr::tr("The controller run control service is unavailable.");
}

void EtherCATWorkbenchPlugin::updateControllerControlContext()
{
    const bool shouldBeActive
        = !m_shuttingDown && m_controller && m_controller->projectService()
          && !m_controller->projectService()->projects().isEmpty();

    if (shouldBeActive == m_controllerControlContextActive)
        return;
    m_controllerControlContextActive = shouldBeActive;
    const ::Core::Context context(Constants::CONTROLLER_CONTROL_CONTEXT_ID);
    if (m_controllerControlContextActive) {
        ::Core::ICore::addAdditionalContext(
            context, ::Core::ICore::ContextPriority::High);
    } else {
        ::Core::ICore::removeAdditionalContext(context);
    }
}

void EtherCATWorkbenchPlugin::updateQuickControllerActions()
{
    if (!m_runControllerAction || !m_debugControllerAction || !m_stopControllerAction)
        return;

    updateControllerControlContext();
    const bool controllerContextActive = m_controllerControlContextActive;
    const std::optional<Data::ControllerConnectionScope> scope
        = activeControllerControlScope();
    const std::optional<Data::ControllerConnectionSnapshot> controllerSnapshot
        = scope && m_controller
              ? std::optional(m_controller->controllerConnectionSnapshot(*scope))
              : std::nullopt;
    const QString controllerStateDescription
        = controllerSnapshot
              ? quickControllerStateDescription(*controllerSnapshot)
              : QString();

    const auto updateAction =
        [this,
         controllerContextActive,
         &scope,
         &controllerSnapshot,
         &controllerStateDescription](
            QAction *action, ControllerQuickControlAction quickAction) {
            std::optional<Data::ControllerControlCommand> command;
            QString reason;
            if (!scope) {
                reason = activeControllerControlScopeUnavailableReason();
            } else if (!m_controller) {
                reason = Tr::tr("The controller run control service is unavailable.");
            } else {
                command = m_controller->quickControllerControlCommand(*scope, quickAction);
                reason = m_controller->quickControllerControlUnavailableReason(
                    *scope, quickAction);
            }

            QString text;
            QString availableDescription;
            const bool startupInProgress = scope && m_controller
                                           && m_controller->controllerStartupInProgress(*scope);
            const bool stopInProgress = scope && m_controller
                                        && m_controller->controllerStopInProgress(*scope);
            switch (quickAction) {
            case ControllerQuickControlAction::Run:
                action->setIcon(ProjectExplorer::Icons::RUN.icon());
                if (startupInProgress) {
                    action->setIcon(Utils::Icons::RELOAD.icon());
                    text = Tr::tr("Starting Controller...");
                } else if (command == Data::ControllerControlCommand::RestoreActivePackage) {
                    text = Tr::tr("Run Controller");
                    availableDescription = Tr::tr(
                        "Restore the saved controller package and start it using its configured "
                        "FreeRun or Distributed Clocks mode. Scan is available only from the "
                        "explicit Rescan command.");
                } else if (command == Data::ControllerControlCommand::Start) {
                    text = Tr::tr("Start Controller");
                    availableDescription = Tr::tr(
                        "Start the active controller package using its configured FreeRun or "
                        "Distributed Clocks timing mode.");
                } else if (command == Data::ControllerControlCommand::Resume) {
                    text = Tr::tr("Resume Controller");
                    availableDescription = Tr::tr("Resume the paused controller application.");
                } else if (
                    controllerSnapshot && controllerSnapshot->scope == *scope
                    && controllerSnapshot->state != Data::ControllerConnectionState::Disconnected) {
                    using State = Data::ControllerConnectionState;
                    const bool controllerFault
                        = controllerSnapshot->controllerState
                          && (controllerSnapshot->controllerState->serviceState
                                  == Data::ControllerServiceState::Fault
                              || controllerSnapshot->controllerState->currentFaults
                              || controllerSnapshot->controllerState->latchedFaults);
                    if (controllerFault) {
                        action->setIcon(Utils::Icons::CRITICAL_TOOLBAR.icon());
                    } else {
                        switch (controllerSnapshot->state) {
                        case State::Connected:
                            action->setIcon(Utils::Icons::LINK.icon());
                            break;
                        case State::Degraded:
                            action->setIcon(Utils::Icons::WARNING_TOOLBAR.icon());
                            break;
                        case State::Failed:
                            action->setIcon(Utils::Icons::CRITICAL_TOOLBAR.icon());
                            break;
                        default:
                            action->setIcon(Utils::Icons::RELOAD.icon());
                            break;
                        }
                    }
                    text = controllerFault
                               ? Tr::tr("%1 · %2")
                                     .arg(
                                         quickConnectionStateName(controllerSnapshot->state),
                                         Tr::tr("Fault"))
                               : Tr::tr("%1: %2")
                                     .arg(
                                         Tr::tr("Controller"),
                                         quickConnectionStateName(controllerSnapshot->state));
                } else {
                    text = Tr::tr("Run Controller");
                }
                break;
            case ControllerQuickControlAction::Debug:
                if (command == Data::ControllerControlCommand::Pause) {
                    action->setIcon(Utils::Icons::INTERRUPT_SMALL_TOOLBAR.icon());
                    text = Tr::tr("Pause Controller");
                    availableDescription = Tr::tr("Pause the running controller application.");
                } else if (command == Data::ControllerControlCommand::Resume) {
                    action->setIcon(ProjectExplorer::Icons::RUN.icon());
                    text = Tr::tr("Resume Controller");
                    availableDescription = Tr::tr("Resume the paused controller application.");
                } else {
                    action->setIcon(ProjectExplorer::Icons::DEBUG_START.icon());
                    text = Tr::tr("Pause / Resume Controller");
                }
                break;
            case ControllerQuickControlAction::Stop:
                action->setIcon(Utils::Icons::STOP_SMALL.icon());
                if (stopInProgress) {
                    action->setIcon(Utils::Icons::RELOAD.icon());
                    text = Tr::tr("Stopping Controller...");
                } else {
                    text = Tr::tr("Stop Controller");
                    availableDescription = Tr::tr(
                        "Stop the application safely, then enter configuration so cyclic "
                        "EtherCAT traffic and Distributed Clocks runtime stop. This is not an "
                        "emergency stop.");
                }
                break;
            }

            const bool enabled
                = controllerContextActive && m_controller && scope && command && reason.isEmpty()
                  && m_controller->canExecuteControllerControl(*scope, *command);
            if (!controllerContextActive && reason.isEmpty())
                reason = Tr::tr("Open the EtherCAT Workbench to use controller run controls.");
            QString description
                = enabled
                      ? availableDescription
                      : Tr::tr("%1 is unavailable: %2").arg(text, reason);
            if (!controllerStateDescription.isEmpty()) {
                description += QLatin1Char('\n') + controllerStateDescription;
            }
            action->setText(text);
            action->setToolTip(description);
            action->setStatusTip(description);
            action->setEnabled(enabled);
            if (quickAction == ControllerQuickControlAction::Stop)
                action->setVisible(controllerContextActive && scope.has_value());
            else
                action->setVisible(true);
        };

    updateAction(m_runControllerAction, ControllerQuickControlAction::Run);
    updateAction(m_debugControllerAction, ControllerQuickControlAction::Debug);
    updateAction(m_stopControllerAction, ControllerQuickControlAction::Stop);
}

void EtherCATWorkbenchPlugin::triggerQuickControllerAction(ControllerQuickControlAction action)
{
    if (!m_controller)
        return;
    const std::optional<Data::ControllerConnectionScope> scope
        = activeControllerControlScope();
    if (!scope) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot control the controller: %1")
                .arg(activeControllerControlScopeUnavailableReason()),
            ControllerOutputLevel::Error);
        updateQuickControllerActions();
        return;
    }

    if (const Utils::Result<> result = m_controller->executeQuickControllerControl(*scope, action);
        !result) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot control the controller: %1").arg(result.error()),
            ControllerOutputLevel::Error);
    }
    updateQuickControllerActions();
}

void EtherCATWorkbenchPlugin::setupActions()
{
    ::Core::ActionContainer *menu = ::Core::ActionManager::actionContainer(Constants::MENU_ID);
    if (!menu) {
        menu = ::Core::ActionManager::createMenu(Constants::MENU_ID);
        menu->menu()->setTitle(Tr::tr("EtherCAT"));
        ::Core::ActionManager::actionContainer(::Core::Constants::M_TOOLS)->addMenu(menu);
    }

    setupQuickControllerActions();

    auto openAction = new QAction(Utils::Icons::SETTINGS.icon(), Tr::tr("Open Workbench"), this);
    ::Core::Command *openCommand = ::Core::ActionManager::registerAction(
        openAction, Constants::OPEN_ACTION_ID);
    menu->addAction(openCommand);
    connect(openAction, &QAction::triggered, this, [] {
        ::Core::ModeManager::activateMode(Constants::MODE_ID);
    });

    auto refreshAction = new QAction(Utils::Icons::RELOAD.icon(), Tr::tr("Refresh"), this);
    ::Core::Command *refreshCommand = ::Core::ActionManager::registerAction(
        refreshAction,
        Constants::REFRESH_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    menu->addAction(refreshCommand);
    connect(refreshAction, &QAction::triggered, m_controller.get(), &WorkbenchController::refresh);

    auto connectControllerAction
        = new QAction(Utils::Icons::LINK.icon(), Tr::tr("Connect Controller"), this);
    const QString connectControllerDescription = Tr::tr(
        "Establish the Control, Push, and Bulk channels, read the authoritative controller "
        "snapshot, then automatically request the exclusive control lease. Connecting never "
        "scans the bus or changes the controller state; use Rescan explicitly when the physical "
        "bus has changed.");
    connectControllerAction->setToolTip(connectControllerDescription);
    connectControllerAction->setStatusTip(connectControllerDescription);
    ::Core::Command *connectControllerCommand = ::Core::ActionManager::registerAction(
        connectControllerAction,
        Constants::CONNECT_CONTROLLER_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    connectControllerCommand->setDescription(connectControllerAction->text());
    menu->addAction(connectControllerCommand);
    connect(connectControllerAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->connectSelectedController(); !result) {
            m_controller->writeControllerOutput(
                Tr::tr("Cannot connect to the controller: %1").arg(result.error()),
                ControllerOutputLevel::Error);
        }
    });

    auto scanControllerAction
        = new QAction(Utils::Icons::NEWSEARCH_TOOLBAR.icon(), Tr::tr("Rescan Bus"), this);
    const QString scanControllerDescription = Tr::tr(
        "Scan the live EtherCAT bus connected to the selected Master. This does not modify the "
        "offline project.");
    scanControllerAction->setToolTip(scanControllerDescription);
    scanControllerAction->setStatusTip(scanControllerDescription);
    ::Core::Command *scanControllerCommand = ::Core::ActionManager::registerAction(
        scanControllerAction,
        Constants::SCAN_CONTROLLER_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    scanControllerCommand->setDescription(scanControllerAction->text());
    menu->addAction(scanControllerCommand);
    connect(scanControllerAction, &QAction::triggered, m_controller.get(), [this] {
        Data::ControllerControlRequest request;
        request.command = Data::ControllerControlCommand::DiscoverTopology;
        if (const Utils::Result<> result
            = m_controller->executeSelectedControllerControl(request);
            !result) {
            m_controller->writeControllerOutput(
                Tr::tr("Cannot scan the EtherCAT bus: %1").arg(result.error()),
                ControllerOutputLevel::Error);
        }
    });

    auto applyCurrentBusAction
        = new QAction(Utils::Icons::DOWNLOAD.icon(), Tr::tr("Apply Current Bus to Project"), this);
    const QString applyCurrentBusDescription = Tr::tr(
        "Replace the selected Master's local offline device list with the latest live bus scan. "
        "Existing parameter, Process Data, Startup, and Distributed Clocks settings are preserved "
        "when the same device remains at the same position. Unknown devices require matching ESI "
        "XML before detailed configuration is available. This action is undoable and does not "
        "write to the controller.");
    applyCurrentBusAction->setToolTip(applyCurrentBusDescription);
    applyCurrentBusAction->setStatusTip(applyCurrentBusDescription);
    ::Core::Command *applyCurrentBusCommand = ::Core::ActionManager::registerAction(
        applyCurrentBusAction,
        Constants::APPLY_CURRENT_BUS_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    applyCurrentBusCommand->setDescription(applyCurrentBusAction->text());
    menu->addAction(applyCurrentBusCommand);
    connect(applyCurrentBusAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->applyCurrentBusToProject(); !result) {
            m_controller->writeControllerOutput(
                Tr::tr("Cannot apply the current bus to the project: %1").arg(result.error()),
                ControllerOutputLevel::Error);
        }
    });

    menu->addSeparator();
    menu->addAction(::Core::ActionManager::command(ProjectExplorer::Constants::RUN));
    menu->addAction(::Core::ActionManager::command(Constants::DEBUG_ACTION_ID));
    menu->addAction(::Core::ActionManager::command(Constants::CONTROLLED_STOP_ACTION_ID));
    menu->addSeparator();

    auto refreshControllerAction
        = new QAction(Utils::Icons::RELOAD.icon(), Tr::tr("Refresh Controller Snapshot"), this);
    const QString refreshControllerDescription = Tr::tr(
        "Refresh the read-only state snapshot for the controller connected to the selected "
        "EtherCAT Master.");
    refreshControllerAction->setToolTip(refreshControllerDescription);
    refreshControllerAction->setStatusTip(refreshControllerDescription);
    ::Core::Command *refreshControllerCommand = ::Core::ActionManager::registerAction(
        refreshControllerAction,
        Constants::REFRESH_CONTROLLER_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    refreshControllerCommand->setDescription(refreshControllerAction->text());
    menu->addAction(refreshControllerCommand);
    connect(refreshControllerAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->refreshSelectedController(); !result) {
            m_controller->writeControllerOutput(
                Tr::tr("Cannot refresh the controller snapshot: %1").arg(result.error()),
                ControllerOutputLevel::Error);
        }
    });

    auto disconnectControllerAction
        = new QAction(Utils::Icons::STOP_SMALL.icon(), Tr::tr("Disconnect Controller"), this);
    const QString disconnectControllerDescription = Tr::tr(
        "Release this session's management lease and close its connection. An already running "
        "autonomous cyclic task continues on the controller.");
    disconnectControllerAction->setToolTip(disconnectControllerDescription);
    disconnectControllerAction->setStatusTip(disconnectControllerDescription);
    ::Core::Command *disconnectControllerCommand = ::Core::ActionManager::registerAction(
        disconnectControllerAction,
        Constants::DISCONNECT_CONTROLLER_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    disconnectControllerCommand->setDescription(disconnectControllerAction->text());
    menu->addAction(disconnectControllerCommand);
    connect(disconnectControllerAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->disconnectSelectedController(); !result) {
            m_controller->writeControllerOutput(
                Tr::tr("Cannot disconnect from the controller: %1").arg(result.error()),
                ControllerOutputLevel::Error);
        }
    });

    auto expandAction = new QAction(
        Utils::Icons::EXPAND_ALL_TOOLBAR.icon(), Tr::tr("Expand Device Tree"), this);
    ::Core::Command *expandCommand = ::Core::ActionManager::registerAction(
        expandAction,
        Constants::EXPAND_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    menu->addAction(expandCommand);
    connect(expandAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->expandAllRequested();
    });

    auto collapseAction = new QAction(
        Utils::Icons::COLLAPSE_TOOLBAR.icon(), Tr::tr("Collapse Device Tree"), this);
    ::Core::Command *collapseCommand = ::Core::ActionManager::registerAction(
        collapseAction,
        Constants::COLLAPSE_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    menu->addAction(collapseCommand);
    connect(collapseAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->collapseAllRequested();
    });

    auto locateDifferenceAction
        = new QAction(Utils::Icons::INFO.icon(), Tr::tr("Locate First Topology Difference"), this);
    ::Core::Command *locateDifferenceCommand = ::Core::ActionManager::registerAction(
        locateDifferenceAction,
        Constants::LOCATE_DIFFERENCE_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    menu->addAction(locateDifferenceCommand);
    connect(locateDifferenceAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->locateFirstTopologyDifferenceRequested();
    });

    auto locateIssueAction
        = new QAction(Utils::Icons::WARNING.icon(), Tr::tr("Locate First Issue"), this);
    ::Core::Command *locateIssueCommand = ::Core::ActionManager::registerAction(
        locateIssueAction,
        Constants::LOCATE_ISSUE_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    menu->addAction(locateIssueCommand);
    connect(locateIssueAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->locateFirstIssueRequested();
    });

    auto openDiagnosticsAction
        = new QAction(Utils::Icons::INFO.icon(), Tr::tr("Open Diagnostics"), this);
    ::Core::Command *openDiagnosticsCommand = ::Core::ActionManager::registerAction(
        openDiagnosticsAction,
        Constants::OPEN_DIAGNOSTICS_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    menu->addAction(openDiagnosticsCommand);
    connect(openDiagnosticsAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->openDiagnosticsRequested();
    });

    auto locateUnsupportedAction
        = new QAction(Utils::Icons::BROKEN.icon(), Tr::tr("Locate Unsupported Device"), this);
    ::Core::ActionManager::registerAction(
        locateUnsupportedAction,
        Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    connect(locateUnsupportedAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->locateUnsupportedDeviceRequested();
    });

    auto copyNodeIdAction = new QAction(Utils::Icons::COPY.icon(), Tr::tr("Copy Node ID"), this);
    ::Core::ActionManager::registerAction(
        copyNodeIdAction, Constants::COPY_NODE_ID_ACTION_ID, ::Core::Context(Constants::CONTEXT_ID));
    connect(copyNodeIdAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->copyCurrentNodeIdRequested();
    });

    auto setActiveProjectAction = new QAction(Tr::tr("Set as Active Project"), this);
    const QString setActiveProjectDescription = Tr::tr(
        "Use this open offline EtherCAT project for Workbench engineering commands. "
        "This does not activate a controller configuration.");
    setActiveProjectAction->setToolTip(setActiveProjectDescription);
    setActiveProjectAction->setStatusTip(setActiveProjectDescription);
    ::Core::Command *setActiveProjectCommand = ::Core::ActionManager::registerAction(
        setActiveProjectAction,
        Constants::SET_ACTIVE_PROJECT_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    setActiveProjectCommand->setDescription(setActiveProjectAction->text());
    connect(setActiveProjectAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->activateSelectedProject(); !result) {
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot set the active EtherCAT project: %1").arg(result.error()));
        }
    });

    auto insertDeviceAction
        = new QAction(Utils::Icons::PLUS.icon(), Tr::tr("Add New Item..."), this);
    ::Core::ActionManager::registerAction(
        insertDeviceAction,
        Constants::INSERT_DEVICE_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    connect(insertDeviceAction, &QAction::triggered, m_controller.get(), [this] {
        emit m_controller->insertDeviceRequested();
    });

    auto addDeviceAction
        = new QAction(Utils::Icons::PLUS.icon(), Tr::tr("Add to Active Offline Master"), this);
    ::Core::Command *addDeviceCommand = ::Core::ActionManager::registerAction(
        addDeviceAction,
        Constants::ADD_DEVICE_TO_MASTER_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    addDeviceCommand->setAttribute(::Core::Command::CA_UpdateText);
    addDeviceCommand->setDescription(Tr::tr("Add ESI Device to Active Offline Master"));
    const auto quickAddTarget = std::make_shared<std::optional<OfflineMasterTarget>>();
    connect(addDeviceAction, &QAction::triggered, m_controller.get(), [this, quickAddTarget] {
        if (!*quickAddTarget) {
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot add the ESI device: activate a valid offline EtherCAT project."));
            return;
        }
        const Utils::Result<> result
            = m_controller->addSelectedDeviceToMaster(**quickAddTarget);
        if (!result) {
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot add the ESI device: %1").arg(result.error()));
        }
    });

    auto removeSlaveAction
        = new QAction(Utils::Icons::MINUS.icon(), Tr::tr("Remove from Offline Master..."), this);
    const QString removeSlaveDescription = Tr::tr(
        "Remove the selected slave and its offline Process Data, Startup, and Distributed "
        "Clocks configuration after confirmation.");
    removeSlaveAction->setToolTip(removeSlaveDescription);
    removeSlaveAction->setStatusTip(removeSlaveDescription);
    ::Core::ActionManager::registerAction(
        removeSlaveAction,
        Constants::REMOVE_OFFLINE_SLAVE_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    connect(removeSlaveAction, &QAction::triggered, m_controller.get(), [this] {
        QPointer<WorkbenchController> controller = m_controller.get();
        const std::optional<OfflineSlaveRemovalCandidate> candidate
            = controller ? controller->selectedOfflineSlaveRemovalCandidate() : std::nullopt;
        if (!candidate) {
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot remove the offline slave: select a configured offline slave."));
            return;
        }

        auto *confirmation = new QMessageBox(::Core::ICore::dialogParent());
        confirmation->setAttribute(Qt::WA_DeleteOnClose);
        confirmation->setObjectName("EtherCATOfflineSlaveRemovalConfirmation");
        confirmation->setIcon(QMessageBox::Question);
        confirmation->setWindowTitle(Tr::tr("Remove Offline Slave"));
        confirmation->setTextFormat(Qt::PlainText);
        confirmation->setText(
            Tr::tr("Remove \"%1\" (position %2) from the offline EtherCAT Master?")
                .arg(candidate->name, QString::number(candidate->position + 1)));
        confirmation->setInformativeText(Tr::tr(
            "Its offline Process Data, Startup, and Distributed Clocks configuration will "
            "also be removed. You can undo this change."));
        confirmation->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirmation->setDefaultButton(QMessageBox::No);
        confirmation->setEscapeButton(QMessageBox::No);
        const OfflineSlaveRemovalCandidate capturedCandidate = *candidate;
        const QPointer<QMessageBox> guardedConfirmation = confirmation;
        const auto rejectIfCandidateIsStale
            = [controller, capturedCandidate, guardedConfirmation] {
                  if (!guardedConfirmation || !guardedConfirmation->isVisible())
                      return;
                  const std::optional<OfflineSlaveRemovalCandidate> currentCandidate
                      = controller ? controller->selectedOfflineSlaveRemovalCandidate()
                                   : std::nullopt;
                  if (currentCandidate
                      && currentCandidate->projectId == capturedCandidate.projectId
                      && currentCandidate->masterId == capturedCandidate.masterId
                      && currentCandidate->slaveId == capturedCandidate.slaveId
                      && currentCandidate->expectedSlave == capturedCandidate.expectedSlave) {
                      return;
                  }
                  guardedConfirmation->reject();
              };
        connect(controller->selectionService(),
                &Core::SelectionService::currentNodeChanged,
                confirmation,
                [rejectIfCandidateIsStale](const Data::NodeId &, const Data::NodeId &) {
                    rejectIfCandidateIsStale();
                });
        connect(controller->projectService(),
                &Core::ProjectService::projectAboutToBeRemoved,
                confirmation,
                [capturedCandidate, guardedConfirmation](const Data::NodeId &projectId) {
                    if (projectId == capturedCandidate.projectId && guardedConfirmation)
                        guardedConfirmation->reject();
                });
        connect(controller->projectService(),
                &Core::ProjectService::projectChanged,
                confirmation,
                [capturedCandidate, rejectIfCandidateIsStale](
                    const Data::ProjectSnapshot &project) {
                    if (project.id == capturedCandidate.projectId)
                        rejectIfCandidateIsStale();
                });
        connect(confirmation,
                &QMessageBox::finished,
                confirmation,
                [controller, candidate = capturedCandidate](int result) {
                    if (result != QMessageBox::Yes || !controller)
                        return;
                    if (const Utils::Result<> removal = controller->removeOfflineSlave(candidate);
                        !removal) {
                        ::Core::MessageManager::writeFlashing(
                            Tr::tr("Cannot remove the offline slave: %1")
                                .arg(removal.error()));
                    }
                });
        confirmation->open();
    });

    auto moveSlaveUpAction
        = new QAction(Utils::Icons::ARROW_UP.icon(), Tr::tr("Move Offline Slave Up"), this);
    ::Core::ActionManager::registerAction(
        moveSlaveUpAction,
        Constants::MOVE_OFFLINE_SLAVE_UP_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    connect(moveSlaveUpAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->moveSelectedOfflineSlaveUp(); !result) {
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot move the offline slave up: %1").arg(result.error()));
        }
    });

    auto moveSlaveDownAction
        = new QAction(Utils::Icons::ARROW_DOWN.icon(), Tr::tr("Move Offline Slave Down"), this);
    ::Core::ActionManager::registerAction(
        moveSlaveDownAction,
        Constants::MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID,
        ::Core::Context(Constants::CONTEXT_ID));
    connect(moveSlaveDownAction, &QAction::triggered, m_controller.get(), [this] {
        if (const Utils::Result<> result = m_controller->moveSelectedOfflineSlaveDown(); !result) {
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot move the offline slave down: %1").arg(result.error()));
        }
    });

    const auto updateQuickAddPresentation = [this, addDeviceAction, quickAddTarget] {
        *quickAddTarget = m_controller ? m_controller->activeOfflineMasterTarget() : std::nullopt;
        QString description;
        if (*quickAddTarget) {
            const OfflineMasterTarget &target = **quickAddTarget;
            addDeviceAction->setText(
                Tr::tr("Add to \"%1\" / \"%2\"")
                    .arg(
                        Utils::quoteAmpersands(target.projectName),
                        Utils::quoteAmpersands(target.masterName)));
            description = Tr::tr(
                              "Add the selected supported ESI device to offline master \"%1\" "
                              "in active project \"%2\". This changes only the local offline "
                              "project; no controller or hardware is contacted.")
                              .arg(target.masterName, target.projectName);
        } else {
            addDeviceAction->setText(Tr::tr("Add to Active Offline Master"));
            description = Tr::tr(
                "Activate a valid offline EtherCAT project before adding an ESI device. This "
                "action changes only a local offline project; it does not contact a controller "
                "or hardware.");
        }
        addDeviceAction->setToolTip(description);
        addDeviceAction->setStatusTip(description);
    };

    const auto updateNavigationActions =
        [this,
         updateQuickAddPresentation,
         connectControllerAction,
         scanControllerAction,
         applyCurrentBusAction,
         refreshControllerAction,
         disconnectControllerAction,
         locateDifferenceAction,
         locateIssueAction,
         openDiagnosticsAction,
         locateUnsupportedAction,
         copyNodeIdAction,
         setActiveProjectAction,
         insertDeviceAction,
         addDeviceAction,
         removeSlaveAction,
         moveSlaveUpAction,
         moveSlaveDownAction] {
            updateQuickAddPresentation();
            if (!m_controller) {
                connectControllerAction->setEnabled(false);
                scanControllerAction->setEnabled(false);
                applyCurrentBusAction->setEnabled(false);
                refreshControllerAction->setEnabled(false);
                disconnectControllerAction->setEnabled(false);
                locateDifferenceAction->setEnabled(false);
                locateIssueAction->setEnabled(false);
                openDiagnosticsAction->setEnabled(false);
                locateUnsupportedAction->setEnabled(false);
                copyNodeIdAction->setEnabled(false);
                setActiveProjectAction->setEnabled(false);
                insertDeviceAction->setEnabled(false);
                addDeviceAction->setEnabled(false);
                removeSlaveAction->setEnabled(false);
                moveSlaveUpAction->setEnabled(false);
                moveSlaveDownAction->setEnabled(false);
                return;
            }
            connectControllerAction->setEnabled(m_controller->canConnectSelectedController());
            scanControllerAction->setEnabled(
                m_controller->canExecuteSelectedControllerControl(
                    Data::ControllerControlCommand::DiscoverTopology));
            applyCurrentBusAction->setEnabled(m_controller->canApplyCurrentBusToProject());
            refreshControllerAction->setEnabled(m_controller->canRefreshSelectedController());
            disconnectControllerAction->setEnabled(m_controller->canDisconnectSelectedController());
            Core::SelectionService *selectionService = m_controller->selectionService();
            const Data::NodeId currentNodeId
                = selectionService ? selectionService->currentNodeId() : Data::NodeId();
            const Core::PropertyPageContext currentContext
                = selectionService
                      ? m_controller->treeModel()->contextForNodeId(currentNodeId)
                      : Core::PropertyPageContext();
            const bool currentSelectionIsKnown
                = currentNodeId.isNull() || !currentContext.nodeId.isNull();
            locateDifferenceAction->setEnabled(
                selectionService && currentSelectionIsKnown
                && m_controller->treeModel()
                       ->firstTopologyDifference(currentContext.projectId)
                       .isValid());
            locateIssueAction->setEnabled(
                selectionService && currentSelectionIsKnown
                && m_controller->treeModel()->firstIssue(currentContext.projectId).isValid());
            openDiagnosticsAction->setEnabled(
                selectionService && currentSelectionIsKnown
                && m_controller->treeModel()
                       ->diagnosticsForProject(currentContext.projectId)
                       .isValid());
            locateUnsupportedAction->setEnabled(
                m_controller->treeModel()->firstUnsupportedDevice().isValid());
            copyNodeIdAction->setEnabled(
                selectionService
                && m_controller->canCopyNodeId(currentNodeId));
            const bool canActivateSelectedProject
                = m_controller->canActivateSelectedProject();
            setActiveProjectAction->setEnabled(canActivateSelectedProject);
            insertDeviceAction->setEnabled(m_controller->canInsertDeviceOnSelectedMaster());
            addDeviceAction->setEnabled(m_controller->canAddSelectedDeviceToMaster());
            removeSlaveAction->setEnabled(m_controller->canRemoveSelectedOfflineSlave());
            moveSlaveUpAction->setEnabled(m_controller->canMoveSelectedOfflineSlaveUp());
            moveSlaveDownAction->setEnabled(m_controller->canMoveSelectedOfflineSlaveDown());
        };
    connect(
        m_controller->treeModel(), &QAbstractItemModel::dataChanged, this, updateNavigationActions);
    connect(m_controller->treeModel(), &QAbstractItemModel::modelReset, this, updateNavigationActions);
    connect(
        m_controller->treeModel(), &QAbstractItemModel::rowsInserted, this, updateNavigationActions);
    connect(
        m_controller->treeModel(), &QAbstractItemModel::rowsRemoved, this, updateNavigationActions);
    connect(
        m_controller->selectionService(),
        &Core::SelectionService::currentNodeChanged,
        this,
        updateNavigationActions);
    connect(
        m_controller.get(),
        &WorkbenchController::controllerConnectionChanged,
        this,
        updateNavigationActions);
    connect(
        m_controller->projectService(),
        &Core::ProjectService::projectChanged,
        this,
        updateNavigationActions);
    connect(
        m_controller->projectService(),
        &Core::ProjectService::activeProjectChanged,
        this,
        updateNavigationActions);
    updateNavigationActions();
}

void EtherCATWorkbenchPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    updateControllerControlContext();
    if (m_statusWidget) {
        ::Core::StatusBarManager::destroyStatusBarWidget(m_statusWidget);
        m_statusWidget = nullptr;
    }
    m_mode.reset();
    m_navigationFactory.reset();
    if (m_automationServiceRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_automationService.get());
        m_automationServiceRegistered = false;
    }
    m_automationService.reset();
    if (m_providerRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_builtinPages.get());
        m_providerRegistered = false;
    }
    m_builtinPages.reset();
    if (m_controller)
        m_controller->shutdown();
    m_controller.reset();
}

} // namespace EtherCAT::Workbench::Internal

#include "ethercatworkbenchplugin.moc"
