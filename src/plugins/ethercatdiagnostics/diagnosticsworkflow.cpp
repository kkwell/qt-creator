// Copyright (C) 2026 Kvell

#include "diagnosticsworkflow.h"

#include "ethercatdiagnosticsconstants.h"
#include "ethercatdiagnosticstr.h"
#include "mockdiagnosticsprovider.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/messagemanager.h>

#include <ethercatcore/selectionservice.h>
#include <ethercatcore/stateservice.h>

#include <ethercatworkbench/ethercatworkbenchconstants.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/utilsicons.h>

#include <QAction>

#include <algorithm>
#include <tuple>

namespace EtherCAT::Diagnostics::Internal {

DiagnosticsWorkflow::DiagnosticsWorkflow(
    MockDiagnosticsProvider *provider, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
{
    m_selectionService = ExtensionSystem::PluginManager::getObject<Core::SelectionService>();
    m_projectService = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    m_stateService = ExtensionSystem::PluginManager::getObject<Core::StateService>();
    if (m_selectionService) {
        connect(
            m_selectionService,
            &Core::SelectionService::currentNodeChanged,
            this,
            [this] { updateActions(); });
    }
    if (m_projectService) {
        connect(
            m_projectService,
            &Core::ProjectService::projectAdded,
            this,
            [this] { updateActions(); });
        connect(
            m_projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            [this] {
                QMetaObject::invokeMethod(
                    this, &DiagnosticsWorkflow::updateActions, Qt::QueuedConnection);
            });
        connect(
            m_projectService,
            &Core::ProjectService::projectChanged,
            this,
            [this] { updateActions(); });
        connect(
            m_projectService,
            &Core::ProjectService::activeProjectChanged,
            this,
            [this] { updateActions(); });
    }
    connect(
        provider,
        &Core::DiagnosticsProvider::streamStateChanged,
        this,
        [this] {
            updateStatus();
            updateActions();
        });
    connect(
        provider,
        &Core::DiagnosticsProvider::diagnosticsSnapshotChanged,
        this,
        [this] {
            updateStatus();
            updateActions();
        });
    connect(
        provider,
        &Core::DiagnosticsProvider::diagnosticEventsChanged,
        this,
        &DiagnosticsWorkflow::updateActions);
}

void DiagnosticsWorkflow::setupActions()
{
    ::Core::ActionContainer *menu = ::Core::ActionManager::actionContainer(
        Workbench::Constants::MENU_ID);
    if (!menu)
        return;

    const auto addAction = [this, menu](
                               const QString &text,
                               Utils::Id id,
                               const QIcon &icon,
                               const auto &callback) {
        auto action = new QAction(icon, text, this);
        ::Core::Command *command = ::Core::ActionManager::registerAction(
            action, id, ::Core::Context(Workbench::Constants::CONTEXT_ID));
        menu->addAction(command);
        connect(action, &QAction::triggered, this, callback);
        m_registeredActions.insert(id, action);
        return action;
    };

    addAction(
        Tr::tr("Start Mock Diagnostics"),
        Constants::START_ACTION_ID,
        Utils::Icons::RUN_SMALL.icon(),
        [this] {
            const Utils::Result<> result = startMonitoring();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Stop Mock Diagnostics"),
        Constants::STOP_ACTION_ID,
        Utils::Icons::STOP_SMALL.icon(),
        [this] { stopMonitoring(); });
    for (const auto &[text, id, mode] : {
             std::tuple{Tr::tr("Mock Config"),
                        Utils::Id(Constants::CONFIG_ACTION_ID),
                        Data::DiagnosticsRunMode::Config},
             std::tuple{Tr::tr("Mock FreeRun"),
                        Utils::Id(Constants::FREE_RUN_ACTION_ID),
                        Data::DiagnosticsRunMode::FreeRun},
             std::tuple{Tr::tr("Mock Run / OP"),
                        Utils::Id(Constants::RUN_ACTION_ID),
                        Data::DiagnosticsRunMode::Run}}) {
        QAction *modeAction = addAction(text, id, Utils::Icons::SETTINGS.icon(), [this, mode] {
            const Utils::Result<> result = requestMode(mode);
            if (!result)
                reportError(result.error());
        });
        modeAction->setCheckable(true);
    }
    addAction(
        Tr::tr("Acknowledge Selected Mock Alarm"),
        Constants::ACKNOWLEDGE_ACTION_ID,
        Utils::Icons::OK.icon(),
        [this] {
            const Utils::Result<> result = acknowledgeSelectedAlarm();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Clear Recovered Mock Events"),
        Constants::CLEAR_RECOVERED_ACTION_ID,
        Utils::Icons::CLEAN.icon(),
        [this] {
            const Utils::Result<> result = clearRecoveredEvents();
            if (!result)
                reportError(result.error());
        });
    updateActions();
}

void DiagnosticsWorkflow::setPageContext(const Core::PropertyPageContext &context)
{
    if (m_pageContext == context)
        return;
    m_pageContext = context;
    m_selectedAlarmId = {};
    updateActions();
}

void DiagnosticsWorkflow::setSelectedAlarmId(const Data::NodeId &eventId)
{
    if (m_selectedAlarmId == eventId)
        return;
    m_selectedAlarmId = eventId;
    updateActions();
}

Utils::Result<> DiagnosticsWorkflow::startMonitoring()
{
    if (!m_provider)
        return Utils::ResultError(Tr::tr("The local Mock diagnostics provider is unavailable."));
    const Utils::Result<Data::DiagnosticsRequest> request = requestForContext();
    if (!request)
        return Utils::ResultError(request.error());
    const Utils::Result<> result = m_provider->startMonitoring(*request);
    if (result) {
        ::Core::MessageManager::writeSilently(
            Tr::tr("[EtherCAT Mock Diagnostics] Local simulation started; "
                   "no controller or network was accessed."));
    }
    return result;
}

void DiagnosticsWorkflow::stopMonitoring()
{
    if (m_provider)
        m_provider->stopMonitoring();
}

Utils::Result<> DiagnosticsWorkflow::requestMode(Data::DiagnosticsRunMode mode)
{
    if (!m_provider) {
        return Utils::ResultError(
            Tr::tr("The local Mock diagnostics provider is unavailable."));
    }
    const Utils::Result<> result = m_provider->requestRunMode(mode);
    updateActions();
    return result;
}

Utils::Result<> DiagnosticsWorkflow::acknowledgeSelectedAlarm()
{
    if (!m_provider)
        return Utils::ResultError(Tr::tr("The local Mock diagnostics provider is unavailable."));
    Data::NodeId eventId = m_selectedAlarmId;
    const QList<Data::DiagnosticEvent> events = m_provider->events();
    const auto isActive = [&events](const Data::NodeId &id) {
        return std::any_of(
            events.cbegin(),
            events.cend(),
            [&id](const Data::DiagnosticEvent &event) {
                return event.id == id
                       && event.lifecycle == Data::AlarmLifecycle::Active;
            });
    };
    if (!isActive(eventId)) {
        const auto active = std::find_if(
            events.cbegin(),
            events.cend(),
            [](const Data::DiagnosticEvent &event) {
                return event.lifecycle == Data::AlarmLifecycle::Active;
            });
        if (active == events.cend())
            return Utils::ResultError(Tr::tr("No active Mock alarm is available."));
        eventId = active->id;
    }
    return m_provider->acknowledgeAlarm(eventId);
}

Utils::Result<> DiagnosticsWorkflow::clearRecoveredEvents()
{
    return m_provider
               ? m_provider->clearRecoveredEvents()
               : Utils::ResultError(
                     Tr::tr("The local Mock diagnostics provider is unavailable."));
}

QAction *DiagnosticsWorkflow::action(Utils::Id id) const
{
    ::Core::Command *command = ::Core::ActionManager::command(id);
    return command ? command->action() : nullptr;
}

QAction *DiagnosticsWorkflow::registeredAction(Utils::Id id) const
{
    return m_registeredActions.value(id);
}

void DiagnosticsWorkflow::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_provider)
        m_provider->stopMonitoring();
    if (m_stateService)
        m_stateService->clearStatus(Constants::STATUS_SOURCE_ID);
}

Utils::Result<Data::DiagnosticsRequest> DiagnosticsWorkflow::requestForContext() const
{
    if (!m_projectService)
        return Utils::ResultError(Tr::tr("The EtherCAT project service is unavailable."));

    Data::NodeId preferredProjectId = m_pageContext.projectId;
    Data::NodeId selectedId = m_pageContext.nodeId;
    if (preferredProjectId.isNull())
        preferredProjectId = m_projectService->activeProjectId();
    if (selectedId.isNull() && m_selectionService)
        selectedId = m_selectionService->currentNodeId();
    const QList<Data::ProjectSnapshot> projects = m_projectService->projects();
    if (preferredProjectId.isNull() && projects.size() == 1)
        preferredProjectId = projects.first().id;

    for (const Data::ProjectSnapshot &project : projects) {
        const auto selectedNode = std::find_if(
            project.nodes.cbegin(), project.nodes.cend(), [&selectedId](const auto &node) {
                return node.id == selectedId;
            });
        const bool contextProject = project.id == preferredProjectId;
        if (!contextProject && selectedNode == project.nodes.cend())
            continue;

        Data::NodeId masterId;
        if (selectedNode != project.nodes.cend()) {
            if (selectedNode->kind == Data::ProjectNodeKind::Master)
                masterId = selectedNode->id;
            else if (selectedNode->kind == Data::ProjectNodeKind::Slave)
                masterId = selectedNode->parentId;
        }
        if (masterId.isNull()) {
            const auto configuredSlave = std::find_if(
                project.slaves.cbegin(),
                project.slaves.cend(),
                [&selectedId](const auto &slave) { return slave.id == selectedId; });
            if (configuredSlave != project.slaves.cend())
                masterId = configuredSlave->masterId;
        }
        if (masterId.isNull()) {
            const auto master = std::find_if(
                project.nodes.cbegin(), project.nodes.cend(), [](const auto &node) {
                    return node.kind == Data::ProjectNodeKind::Master;
                });
            if (master != project.nodes.cend())
                masterId = master->id;
        }
        if (masterId.isNull())
            return Utils::ResultError(Tr::tr("The selected project has no EtherCAT master."));
        return Data::DiagnosticsRequest{project.id, masterId};
    }
    return Utils::ResultError(
        Tr::tr("Select an open EtherCAT project, master, slave, or Diagnostics node."));
}

void DiagnosticsWorkflow::updateActions()
{
    if (m_registeredActions.isEmpty() || !m_provider)
        return;
    const Data::DiagnosticsStreamState state = m_provider->streamState();
    const bool stopped = state == Data::DiagnosticsStreamState::Stopped;
    const bool running = state == Data::DiagnosticsStreamState::Running;
    const bool canStop = state != Data::DiagnosticsStreamState::Stopped;
    const bool haveRequest = bool(requestForContext());
    const std::optional<Data::DiagnosticsSnapshot> snapshot = m_provider->latestSnapshot();
    const QList<Data::DiagnosticEvent> events = m_provider->events();
    const auto hasLifecycle = [&events](Data::AlarmLifecycle lifecycle) {
        return std::any_of(
            events.cbegin(),
            events.cend(),
            [lifecycle](const Data::DiagnosticEvent &event) {
                return event.lifecycle == lifecycle;
            });
    };

    registeredAction(Constants::START_ACTION_ID)->setEnabled(stopped && haveRequest);
    registeredAction(Constants::STOP_ACTION_ID)->setEnabled(canStop);
    for (const auto &[id, mode] : {
             std::pair{Utils::Id(Constants::CONFIG_ACTION_ID),
                       Data::DiagnosticsRunMode::Config},
             std::pair{Utils::Id(Constants::FREE_RUN_ACTION_ID),
                       Data::DiagnosticsRunMode::FreeRun},
             std::pair{Utils::Id(Constants::RUN_ACTION_ID),
                       Data::DiagnosticsRunMode::Run}}) {
        QAction *modeAction = registeredAction(id);
        modeAction->setEnabled(running);
        modeAction->setChecked(snapshot && snapshot->runMode == mode);
    }
    registeredAction(Constants::ACKNOWLEDGE_ACTION_ID)
        ->setEnabled(running && hasLifecycle(Data::AlarmLifecycle::Active));
    registeredAction(Constants::CLEAR_RECOVERED_ACTION_ID)
        ->setEnabled(hasLifecycle(Data::AlarmLifecycle::Recovered));
    emit actionStateChanged();
}

void DiagnosticsWorkflow::updateStatus()
{
    if (!m_stateService || !m_provider)
        return;
    const Data::DiagnosticsStreamState state = m_provider->streamState();
    if (state == Data::DiagnosticsStreamState::Stopped) {
        m_stateService->clearStatus(Constants::STATUS_SOURCE_ID);
        return;
    }
    if (state == Data::DiagnosticsStreamState::Failed) {
        m_stateService->setStatus(
            {Constants::STATUS_SOURCE_ID,
             Core::StatusSeverity::Error,
             Tr::tr("Mock EtherCAT diagnostics failed"),
             m_provider->lastDiagnosticsError()});
        return;
    }
    if (state == Data::DiagnosticsStreamState::Starting
        || state == Data::DiagnosticsStreamState::Stopping) {
        m_stateService->setStatus(
            {Constants::STATUS_SOURCE_ID,
             Core::StatusSeverity::Busy,
             Tr::tr("Mock EtherCAT diagnostics changing state"),
             Tr::tr("Local simulation only")});
        return;
    }
    const std::optional<Data::DiagnosticsSnapshot> snapshot = m_provider->latestSnapshot();
    const bool fault = snapshot && snapshot->masterHasError;
    m_stateService->setStatus(
        {Constants::STATUS_SOURCE_ID,
         fault ? Core::StatusSeverity::Warning : Core::StatusSeverity::Ready,
         fault ? Tr::tr("Mock EtherCAT diagnostics warning")
               : Tr::tr("Mock EtherCAT diagnostics running"),
         snapshot
             ? Tr::tr("MOCK mode %1, WKC %2/%3")
                   .arg(int(snapshot->runMode))
                   .arg(snapshot->workingCounter.actual)
                   .arg(snapshot->workingCounter.expected)
             : Tr::tr("Local simulation only")});
}

void DiagnosticsWorkflow::reportError(const QString &error) const
{
    ::Core::MessageManager::writeDisrupting(
        Tr::tr("[EtherCAT Mock Diagnostics] %1").arg(error));
}

} // namespace EtherCAT::Diagnostics::Internal
