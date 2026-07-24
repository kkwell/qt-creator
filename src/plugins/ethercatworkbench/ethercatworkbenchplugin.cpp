// Copyright (C) 2026 Kvell

#include "builtinpropertypages.h"
#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#ifdef WITH_TESTS
#include "ethercatworkbenchtests.h"
#endif
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

#include <ethercatcore/providers.h>
#include <ethercatcore/selectionservice.h>
#include <ethercatcore/stateservice.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>

#include <memory>

namespace EtherCAT::Workbench::Internal {

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
    void shutdown();

    std::unique_ptr<WorkbenchController> m_controller;
    std::unique_ptr<BuiltinPropertyPageProvider> m_builtinPages;
    std::unique_ptr<WorkbenchNavigationFactory> m_navigationFactory;
    std::unique_ptr<WorkbenchMode> m_mode;
    QPointer<WorkbenchStatusWidget> m_statusWidget;
    bool m_providerRegistered = false;
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
    m_builtinPages = std::make_unique<BuiltinPropertyPageProvider>(m_controller.get());
    ExtensionSystem::PluginManager::addObject(m_builtinPages.get());
    m_providerRegistered = true;

    m_navigationFactory = std::make_unique<WorkbenchNavigationFactory>(m_controller.get());
    m_mode = std::make_unique<WorkbenchMode>(m_controller.get());
    setupActions();
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

void EtherCATWorkbenchPlugin::setupActions()
{
    ::Core::ActionContainer *menu = ::Core::ActionManager::actionContainer(Constants::MENU_ID);
    if (!menu) {
        menu = ::Core::ActionManager::createMenu(Constants::MENU_ID);
        menu->menu()->setTitle(Tr::tr("EtherCAT"));
        ::Core::ActionManager::actionContainer(::Core::Constants::M_TOOLS)->addMenu(menu);
    }

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
        = new QAction(Utils::Icons::LINK.icon(), Tr::tr("Connect Controller (Read-only)"), this);
    const QString connectControllerDescription = Tr::tr(
        "Connect the selected controller profile and query its read-only state. This does not "
        "acquire control, scan the bus, change controller state, or write configuration.");
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
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot connect to the controller: %1").arg(result.error()));
        }
    });

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
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot refresh the controller snapshot: %1").arg(result.error()));
        }
    });

    auto disconnectControllerAction
        = new QAction(Utils::Icons::STOP_SMALL.icon(), Tr::tr("Disconnect Controller"), this);
    const QString disconnectControllerDescription = Tr::tr(
        "Close the read-only controller connection for the selected EtherCAT Master.");
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
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot disconnect from the controller: %1").arg(result.error()));
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
    if (m_statusWidget) {
        ::Core::StatusBarManager::destroyStatusBarWidget(m_statusWidget);
        m_statusWidget = nullptr;
    }
    m_mode.reset();
    m_navigationFactory.reset();
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
