// Copyright (C) 2026 Kvell

#include "scanworkflow.h"

#include "ethercatscanconstants.h"
#include "ethercatscantr.h"
#include "mockscanprovider.h"
#include "topologycomparison.h"

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

namespace EtherCAT::Scan::Internal {

ScanWorkflow::ScanWorkflow(MockScanProvider *provider, QObject *parent)
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
                    this, &ScanWorkflow::updateActions, Qt::QueuedConnection);
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
        &Core::ScanProvider::scanStateChanged,
        this,
        [this](Data::ScanState state) {
            updateStatus(state);
            updateActions();
        });
    connect(provider, &Core::ScanProvider::scanResultChanged, this, &ScanWorkflow::updateActions);
}

void ScanWorkflow::setupActions()
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
        m_actions.append(action);
    };

    addAction(
        Tr::tr("Scan Mock Interfaces"),
        Constants::SCAN_INTERFACES_ACTION_ID,
        Utils::Icons::MAGNIFIER.icon(),
        [this] {
            const Utils::Result<> result = scanInterfaces();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Scan Mock Slaves"),
        Constants::SCAN_SLAVES_ACTION_ID,
        Utils::Icons::MAGNIFIER.icon(),
        [this] {
            const Utils::Result<> result = scanSlaves();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Rescan Selected Mock Branch"),
        Constants::RESCAN_BRANCH_ACTION_ID,
        Utils::Icons::RELOAD.icon(),
        [this] {
            const Utils::Result<> result = rescanSelectedBranch();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Compare Mock Scan With Project"),
        Constants::COMPARE_ACTION_ID,
        Utils::Icons::SNAPSHOT.icon(),
        [this] {
            const Utils::Result<> result = compareWithProject();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Accept Mock Scan as Offline Configuration"),
        Constants::ACCEPT_ACTION_ID,
        Utils::Icons::PLUS.icon(),
        [this] {
            const Utils::Result<> result = acceptScan();
            if (!result)
                reportError(result.error());
        });
    addAction(
        Tr::tr("Keep Existing Offline Configuration"),
        Constants::KEEP_ACTION_ID,
        Utils::Icons::RESET.icon(),
        [this] { keepExistingConfiguration(); });
    addAction(
        Tr::tr("Cancel Mock Scan"),
        Constants::CANCEL_ACTION_ID,
        Utils::Icons::STOP_SMALL.icon(),
        [this] { cancelScan(); });
    updateActions();
}

Utils::Result<> ScanWorkflow::scanInterfaces()
{
    return start(Data::ScanOperation::Interfaces);
}

Utils::Result<> ScanWorkflow::scanSlaves()
{
    return start(Data::ScanOperation::Slaves);
}

Utils::Result<> ScanWorkflow::rescanSelectedBranch()
{
    return start(Data::ScanOperation::SelectedBranch);
}

Utils::Result<> ScanWorkflow::compareWithProject()
{
    if (!m_provider)
        return Utils::ResultError(Tr::tr("The local Mock scanner is unavailable."));
    return m_provider->compareWithCurrentProject();
}

Utils::Result<> ScanWorkflow::acceptScan()
{
    if (!m_provider || !m_projectService)
        return Utils::ResultError(Tr::tr("The Mock scan workflow is unavailable."));
    std::optional<Data::ScanResult> result = m_provider->lastScanResult();
    if (!result || !result->snapshot.mock || !result->snapshot.complete)
        return Utils::ResultError(Tr::tr("Only a complete local Mock scan can be accepted."));
    if (result->snapshot.operation == Data::ScanOperation::Interfaces) {
        return Utils::ResultError(
            Tr::tr("A Mock interface scan cannot replace the offline slave topology."));
    }
    const Utils::Result<> comparison = m_provider->compareWithCurrentProject();
    if (!comparison)
        return comparison;
    result = m_provider->lastScanResult();
    if (!result)
        return Utils::ResultError(Tr::tr("The Mock scan result is no longer available."));
    if (!result->comparison.acceptAllowed) {
        return Utils::ResultError(
            Tr::tr("Resolve blocking Mock topology differences before accepting the scan."));
    }
    const std::optional<Data::ProjectSnapshot> project
        = m_projectService->project(result->snapshot.projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The scanned EtherCAT project is no longer open."));
    QList<Data::OfflineSlaveConfiguration> accepted = offlineConfigurationFromScan(
        *project, result->snapshot.masterId, result->snapshot);
    if (result->snapshot.operation == Data::ScanOperation::SelectedBranch
        && result->snapshot.branchNodeId != result->snapshot.masterId) {
        QList<Data::OfflineSlaveConfiguration> merged;
        for (const Data::OfflineSlaveConfiguration &slave : project->slaves) {
            if (slave.masterId == result->snapshot.masterId
                && slave.id != result->snapshot.branchNodeId) {
                merged.append(slave);
            }
        }
        merged.append(accepted);
        accepted = merged;
    }
    const Utils::Result<> replaceResult = m_projectService->replaceOfflineSlaves(
        result->snapshot.projectId, result->snapshot.masterId, accepted);
    if (!replaceResult)
        return replaceResult;
    const Utils::Result<> refreshed = m_provider->compareWithCurrentProject();
    if (!refreshed)
        return refreshed;
    ::Core::MessageManager::writeFlashing(
        Tr::tr("[EtherCAT Mock Scan] Offline topology accepted. Save the project to persist it."));
    updateActions();
    return Utils::ResultOk;
}

void ScanWorkflow::keepExistingConfiguration()
{
    if (m_provider)
        m_provider->clearScanResult();
    ::Core::MessageManager::writeSilently(
        Tr::tr("[EtherCAT Mock Scan] Scan result discarded; offline topology was not changed."));
}

void ScanWorkflow::cancelScan()
{
    if (m_provider)
        m_provider->cancelScan();
}

void ScanWorkflow::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_provider)
        m_provider->cancelScan();
    if (m_stateService)
        m_stateService->clearStatus(Constants::STATUS_SOURCE_ID);
}

QAction *ScanWorkflow::action(Utils::Id id) const
{
    ::Core::Command *command = ::Core::ActionManager::command(id);
    return command ? command->action() : nullptr;
}

Utils::Result<Data::ScanRequest> ScanWorkflow::requestForSelection(
    Data::ScanOperation operation) const
{
    if (!m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("EtherCAT selection or project service is unavailable."));

    Data::NodeId selectedId = m_selectionService->currentNodeId();
    Data::NodeId preferredProjectId = m_projectService->activeProjectId();
    const QList<Data::ProjectSnapshot> projects = m_projectService->projects();
    if (selectedId.isNull() && preferredProjectId.isNull() && projects.size() == 1)
        preferredProjectId = projects.first().id;

    for (const Data::ProjectSnapshot &project : projects) {
        const auto selectedNode = std::find_if(
            project.nodes.cbegin(), project.nodes.cend(), [&selectedId](const auto &node) {
                return node.id == selectedId;
            });
        if (!selectedId.isNull() && selectedNode == project.nodes.cend())
            continue;
        if (selectedId.isNull() && project.id != preferredProjectId)
            continue;

        Data::NodeId masterId;
        Data::NodeId branchId;
        if (selectedNode != project.nodes.cend()) {
            switch (selectedNode->kind) {
            case Data::ProjectNodeKind::Master:
                masterId = selectedNode->id;
                branchId = selectedNode->id;
                break;
            case Data::ProjectNodeKind::Slave:
                masterId = selectedNode->parentId;
                branchId = selectedNode->id;
                break;
            case Data::ProjectNodeKind::Project:
            case Data::ProjectNodeKind::Target:
                break;
            }
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
        if (operation == Data::ScanOperation::SelectedBranch && branchId.isNull()) {
            return Utils::ResultError(
                Tr::tr("Select an EtherCAT master or configured slave to rescan its branch."));
        }
        return Data::ScanRequest{project.id, masterId, operation, branchId};
    }
    return Utils::ResultError(Tr::tr("Select an open EtherCAT project, master, or slave."));
}

Utils::Result<> ScanWorkflow::start(Data::ScanOperation operation)
{
    if (!m_provider)
        return Utils::ResultError(Tr::tr("The local Mock scanner is unavailable."));
    if (m_provider->scanState() != Data::ScanState::Idle) {
        if (m_provider->scanState() == Data::ScanState::Preparing
            || m_provider->scanState() == Data::ScanState::ScanningMaster
            || m_provider->scanState() == Data::ScanState::ScanningSlaves
            || m_provider->scanState() == Data::ScanState::BuildingSnapshot
            || m_provider->scanState() == Data::ScanState::Comparing) {
            return Utils::ResultError(Tr::tr("A local Mock scan is already running."));
        }
        m_provider->clearScanResult();
    }
    const Utils::Result<Data::ScanRequest> request = requestForSelection(operation);
    if (!request)
        return Utils::ResultError(request.error());
    const Utils::Result<> result = m_provider->startScan(*request);
    if (result) {
        ::Core::MessageManager::writeSilently(
            Tr::tr("[EtherCAT Mock Scan] Started local simulation; no network is accessed."));
    }
    return result;
}

void ScanWorkflow::updateActions()
{
    if (m_actions.isEmpty() || !m_provider)
        return;
    const Data::ScanState state = m_provider->scanState();
    const bool active = state == Data::ScanState::Preparing
                        || state == Data::ScanState::ScanningMaster
                        || state == Data::ScanState::ScanningSlaves
                        || state == Data::ScanState::BuildingSnapshot
                        || state == Data::ScanState::Comparing;
    const bool haveTarget = bool(requestForSelection(Data::ScanOperation::Slaves));
    const bool haveBranch = bool(requestForSelection(Data::ScanOperation::SelectedBranch));
    const std::optional<Data::ScanResult> result = m_provider->lastScanResult();
    const bool topologyResult
        = result && result->snapshot.operation != Data::ScanOperation::Interfaces;

    action(Constants::SCAN_INTERFACES_ACTION_ID)->setEnabled(!active && haveTarget);
    action(Constants::SCAN_SLAVES_ACTION_ID)->setEnabled(!active && haveTarget);
    action(Constants::RESCAN_BRANCH_ACTION_ID)->setEnabled(!active && haveBranch);
    action(Constants::COMPARE_ACTION_ID)
        ->setEnabled(!active && topologyResult && result->snapshot.complete);
    action(Constants::ACCEPT_ACTION_ID)
        ->setEnabled(
            !active && topologyResult && result->snapshot.complete
            && result->comparison.acceptAllowed
            && !result->comparison.exactMatch);
    action(Constants::KEEP_ACTION_ID)
        ->setEnabled(!active && state != Data::ScanState::Idle);
    action(Constants::CANCEL_ACTION_ID)->setEnabled(active);
    emit actionStateChanged();
}

void ScanWorkflow::updateStatus(Data::ScanState state)
{
    if (!m_stateService || !m_provider)
        return;
    if (state == Data::ScanState::Idle || state == Data::ScanState::Cancelled) {
        m_stateService->clearStatus(Constants::STATUS_SOURCE_ID);
        return;
    }
    if (state == Data::ScanState::Failed) {
        m_stateService->setStatus(
            {Constants::STATUS_SOURCE_ID,
             Core::StatusSeverity::Error,
             Tr::tr("Mock EtherCAT scan failed"),
             m_provider->lastScanError()});
        return;
    }
    if (state == Data::ScanState::Completed) {
        m_stateService->setStatus(
            {Constants::STATUS_SOURCE_ID,
             Core::StatusSeverity::Ready,
             Tr::tr("Mock EtherCAT scan completed"),
             Tr::tr("Local simulation only")});
        return;
    }
    m_stateService->setStatus(
        {Constants::STATUS_SOURCE_ID,
         Core::StatusSeverity::Busy,
         Tr::tr("Mock EtherCAT scan running"),
         m_provider->scanProgress().detail});
}

void ScanWorkflow::reportError(const QString &error) const
{
    ::Core::MessageManager::writeDisrupting(
        Tr::tr("[EtherCAT Mock Scan] %1").arg(error));
}

} // namespace EtherCAT::Scan::Internal
