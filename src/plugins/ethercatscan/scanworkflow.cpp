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

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/scanproviderselectionservice.h>
#include <ethercatcore/selectionservice.h>
#include <ethercatcore/stateservice.h>
#include <ethercatcore/topologyservice.h>

#include <ethercatworkbench/ethercatworkbenchconstants.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/utilsicons.h>

#include <QAction>

#include <algorithm>
#include <utility>

namespace EtherCAT::Scan::Internal {

namespace {

bool scanIsActive(Data::ScanState state)
{
    switch (state) {
    case Data::ScanState::Preparing:
    case Data::ScanState::ScanningMaster:
    case Data::ScanState::ScanningSlaves:
    case Data::ScanState::BuildingSnapshot:
    case Data::ScanState::Comparing:
        return true;
    case Data::ScanState::Idle:
    case Data::ScanState::Completed:
    case Data::ScanState::Cancelled:
    case Data::ScanState::Failed:
        return false;
    }
    return false;
}

bool isSameProjectRevision(
    const Data::RuntimePackageActivationProjectCapture &left,
    const Data::RuntimePackageActivationProjectCapture &right)
{
    return left.documentRevisionNumber() == right.documentRevisionNumber()
           && left.documentRevision() == right.documentRevision();
}

bool containsMaster(const Data::ProjectSnapshot &project, const Data::NodeId &masterId)
{
    return std::any_of(project.nodes.cbegin(), project.nodes.cend(), [&masterId](const auto &node) {
        return node.id == masterId && node.kind == Data::ProjectNodeKind::Master;
    });
}

} // namespace

ScanWorkflow::ScanWorkflow(MockScanProvider *provider, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
{
    m_providerRegistry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    m_selectionService = ExtensionSystem::PluginManager::getObject<Core::SelectionService>();
    m_projectService = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    m_scanProviderSelectionService
        = ExtensionSystem::PluginManager::getObject<Core::ScanProviderSelectionService>();
    m_stateService = ExtensionSystem::PluginManager::getObject<Core::StateService>();
    m_topologyService = ExtensionSystem::PluginManager::getObject<Core::TopologyService>();
    if (m_selectionService) {
        connect(m_selectionService, &Core::SelectionService::currentNodeChanged, this, [this] {
            updateActions();
        });
    }
    if (m_projectService) {
        connect(m_projectService, &Core::ProjectService::projectAdded, this, [this] {
            updateActions();
        });
        connect(m_projectService, &Core::ProjectService::projectAboutToBeRemoved, this, [this] {
            QMetaObject::invokeMethod(this, &ScanWorkflow::updateActions, Qt::QueuedConnection);
        });
        connect(m_projectService, &Core::ProjectService::projectChanged, this, [this] {
            updateActions();
        });
        connect(m_projectService, &Core::ProjectService::activeProjectChanged, this, [this] {
            updateActions();
        });
    }
    if (m_scanProviderSelectionService) {
        connect(
            m_scanProviderSelectionService,
            &Core::ScanProviderSelectionService::selectionChanged,
            this,
            [this] { updateActions(); });
        connect(
            m_scanProviderSelectionService,
            &Core::ScanProviderSelectionService::selectionValidityChanged,
            this,
            [this] { updateActions(); });
    }
    if (m_topologyService) {
        connect(
            m_topologyService,
            &Core::TopologyService::topologyChanged,
            this,
            [this](Core::TopologyEvidenceSource source, Utils::Id providerId) {
                if (source == Core::TopologyEvidenceSource::MockScan && m_provider
                    && providerId == m_provider->id()) {
                    updateActions();
                }
            });
    }
    connect(provider, &Core::ScanProvider::scanStateChanged, this, [this](Data::ScanState state) {
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

    const auto addAction =
        [this, menu](const QString &text, Utils::Id id, const QIcon &icon, const auto &callback) {
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
        [this] {
            const Utils::Result<> result = keepExistingConfiguration();
            if (!result)
                reportError(result.error());
        });
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

    const std::optional<Data::ScanResult> current = m_provider->lastScanResult();
    if (!current) {
        return Utils::ResultError(Tr::tr("No completed Mock topology is available to compare."));
    }
    const Data::ControllerConnectionScope
        scope{current->snapshot.projectId, current->snapshot.masterId};
    const Utils::Result<Data::ScanResult> locked = freshMockResult(scope);
    if (!locked)
        return Utils::ResultError(locked.error());
    if (locked->snapshot.operation == Data::ScanOperation::Interfaces) {
        return Utils::ResultError(
            Tr::tr("A Mock interface scan has no slave topology to compare."));
    }

    const Data::NodeId snapshotId = locked->snapshot.id;
    const Utils::Result<> comparison = m_provider->compareWithCurrentProject();
    if (!comparison)
        return comparison;
    const Utils::Result<Data::ScanResult> stillLocked = freshMockResult(scope, snapshotId);
    if (!stillLocked)
        return Utils::ResultError(stillLocked.error());
    return Utils::ResultOk;
}

Utils::Result<> ScanWorkflow::acceptScan()
{
    if (!m_provider || !m_projectService)
        return Utils::ResultError(Tr::tr("The Mock scan workflow is unavailable."));

    const std::optional<Data::ScanResult> current = m_provider->lastScanResult();
    if (!current) {
        return Utils::ResultError(Tr::tr("No completed Mock topology is available to accept."));
    }
    const Data::ControllerConnectionScope
        scope{current->snapshot.projectId, current->snapshot.masterId};
    Utils::Result<Data::ScanResult> result = freshMockResult(scope);
    if (!result)
        return Utils::ResultError(result.error());
    const Data::NodeId snapshotId = result->snapshot.id;
    if (result->snapshot.operation == Data::ScanOperation::Interfaces) {
        return Utils::ResultError(
            Tr::tr("A Mock interface scan cannot replace the offline slave topology."));
    }

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> beforeComparison
        = m_projectService->captureRuntimePackageActivationProject(scope.projectId);
    if (!beforeComparison)
        return Utils::ResultError(beforeComparison.error());
    if (beforeComparison->snapshot().id != scope.projectId
        || !containsMaster(beforeComparison->snapshot(), scope.masterId)) {
        return Utils::ResultError(
            Tr::tr("The scanned EtherCAT project or master is no longer open."));
    }

    const Utils::Result<> comparison = m_provider->compareWithCurrentProject();
    if (!comparison)
        return comparison;

    result = freshMockResult(scope, snapshotId);
    if (!result)
        return Utils::ResultError(result.error());
    const Utils::Result<Data::RuntimePackageActivationProjectCapture> afterComparison
        = m_projectService->captureRuntimePackageActivationProject(scope.projectId);
    if (!afterComparison)
        return Utils::ResultError(afterComparison.error());
    if (!isSameProjectRevision(*beforeComparison, *afterComparison)) {
        return Utils::ResultError(
            Tr::tr("The EtherCAT project changed while the Mock topology was compared. "
                   "Compare it again before accepting."));
    }
    if (!result->comparison.acceptAllowed) {
        return Utils::ResultError(
            Tr::tr("Resolve blocking Mock topology differences before accepting the scan."));
    }

    const Data::ProjectSnapshot &project = afterComparison->snapshot();
    QList<Data::OfflineSlaveConfiguration> accepted
        = offlineConfigurationFromScan(project, result->snapshot.masterId, result->snapshot);
    if (result->snapshot.operation == Data::ScanOperation::SelectedBranch
        && result->snapshot.branchNodeId != result->snapshot.masterId) {
        QList<Data::OfflineSlaveConfiguration> merged;
        for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
            if (slave.masterId == result->snapshot.masterId
                && slave.id != result->snapshot.branchNodeId) {
                merged.append(slave);
            }
        }
        merged.append(accepted);
        accepted = merged;
    }

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> projectBeforeWrite
        = m_projectService->captureRuntimePackageActivationProject(scope.projectId);
    if (!projectBeforeWrite)
        return Utils::ResultError(projectBeforeWrite.error());
    if (!isSameProjectRevision(*afterComparison, *projectBeforeWrite)) {
        return Utils::ResultError(
            Tr::tr("The EtherCAT project changed before the Mock topology could be accepted. "
                   "Compare it again before accepting."));
    }
    const Utils::Result<Data::ScanResult> evidenceBeforeWrite = freshMockResult(scope, snapshotId);
    if (!evidenceBeforeWrite)
        return Utils::ResultError(evidenceBeforeWrite.error());

    // ProjectService exposes no offline-topology CAS operation. The final
    // capture, evidence check, and replace are synchronous GUI-thread calls,
    // with no signal-producing operation between the checks and the write.
    const Utils::Result<> replaceResult
        = m_projectService->replaceOfflineSlaves(scope.projectId, scope.masterId, accepted);
    if (!replaceResult)
        return replaceResult;

    const auto acceptedButRefreshFailed = [](const QString &detail) {
        return Utils::ResultError(
            Tr::tr("The offline EtherCAT project was updated, but the locked Mock scan evidence "
                   "could not be refreshed: %1")
                .arg(detail));
    };
    const Utils::Result<Data::ScanResult> afterWrite = freshMockResult(scope, snapshotId);
    if (!afterWrite)
        return acceptedButRefreshFailed(afterWrite.error());
    const Utils::Result<> refreshed = m_provider->compareWithCurrentProject();
    if (!refreshed)
        return acceptedButRefreshFailed(refreshed.error());
    const Utils::Result<Data::ScanResult> finalResult = freshMockResult(scope, snapshotId);
    if (!finalResult)
        return acceptedButRefreshFailed(finalResult.error());
    if (!finalResult->comparison.exactMatch || !finalResult->comparison.acceptAllowed) {
        return Utils::ResultError(
            Tr::tr("The offline EtherCAT project was updated, but it changed again before the "
                   "locked Mock topology could be verified."));
    }
    ::Core::MessageManager::writeFlashing(
        Tr::tr("[EtherCAT Mock Scan] Offline topology accepted. Save the project to persist it."));
    updateActions();
    return Utils::ResultOk;
}

Utils::Result<> ScanWorkflow::keepExistingConfiguration()
{
    if (!m_provider)
        return Utils::ResultError(Tr::tr("The local Mock scanner is unavailable."));
    if (scanIsActive(m_provider->scanState())) {
        return Utils::ResultError(
            Tr::tr("Cancel the active Mock scan before discarding its result."));
    }
    const std::optional<Data::ScanResult> result = m_provider->lastScanResult();
    if (!result)
        return Utils::ResultError(Tr::tr("No Mock scan result is available to discard."));
    const Data::ControllerConnectionScope
        scope{result->snapshot.projectId, result->snapshot.masterId};
    const Utils::Result<> binding = validateExactProviderSelection(scope);
    if (!binding)
        return binding;

    m_provider->clearScanResult();
    ::Core::MessageManager::writeSilently(
        Tr::tr("[EtherCAT Mock Scan] Scan result discarded; offline topology was not changed."));
    updateActions();
    return Utils::ResultOk;
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
    updateActions();
}

QAction *ScanWorkflow::action(Utils::Id id) const
{
    ::Core::Command *command = ::Core::ActionManager::command(id);
    return command ? command->action() : nullptr;
}

Utils::Result<Data::ScanRequest> ScanWorkflow::requestForSelection(Data::ScanOperation operation) const
{
    if (!m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("EtherCAT selection or project service is unavailable."));

    const Data::NodeId selectedId = m_selectionService->currentNodeId();
    const Data::NodeId activeProjectId = m_projectService->activeProjectId();
    const QList<Data::ProjectSnapshot> projects = m_projectService->projects();

    const Data::ProjectSnapshot *selectedProject = nullptr;
    const Data::ProjectNodeSnapshot *selectedNode = nullptr;
    if (!selectedId.isNull()) {
        QList<std::pair<const Data::ProjectSnapshot *, const Data::ProjectNodeSnapshot *>> matches;
        for (const Data::ProjectSnapshot &project : projects) {
            for (const Data::ProjectNodeSnapshot &node : project.nodes) {
                if (node.id == selectedId)
                    matches.append({&project, &node});
            }
        }

        if (!activeProjectId.isNull()) {
            const auto activeMatches = std::count_if(
                matches.cbegin(), matches.cend(), [&activeProjectId](const auto &match) {
                    return match.first->id == activeProjectId;
                });
            if (activeMatches > 1) {
                return Utils::ResultError(
                    Tr::tr("The selected EtherCAT node is ambiguous in the active project."));
            }
            if (activeMatches == 1) {
                const auto activeMatch = std::find_if(
                    matches.cbegin(), matches.cend(), [&activeProjectId](const auto &match) {
                        return match.first->id == activeProjectId;
                    });
                selectedProject = activeMatch->first;
                selectedNode = activeMatch->second;
            }
        }
        if (!selectedProject) {
            if (matches.size() > 1) {
                return Utils::ResultError(
                    Tr::tr("The selected EtherCAT node exists in multiple open projects. "
                           "Activate the intended project before scanning."));
            }
            if (matches.size() == 1) {
                selectedProject = matches.constFirst().first;
                selectedNode = matches.constFirst().second;
            }
        }
    } else if (!activeProjectId.isNull()) {
        const auto activeProject = std::find_if(
            projects.cbegin(), projects.cend(), [&activeProjectId](const auto &project) {
                return project.id == activeProjectId;
            });
        if (activeProject != projects.cend())
            selectedProject = &*activeProject;
    } else if (projects.size() == 1) {
        selectedProject = &projects.constFirst();
    }

    if (!selectedProject) {
        return Utils::ResultError(
            Tr::tr("Select an unambiguous open EtherCAT project, master, or slave."));
    }
    if (!selectedProject->valid)
        return Utils::ResultError(Tr::tr("The selected EtherCAT project is invalid."));

    Data::NodeId masterId;
    Data::NodeId branchId;
    if (selectedNode) {
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
        QList<Data::NodeId> masterIds;
        for (const Data::ProjectNodeSnapshot &node : selectedProject->nodes) {
            if (node.kind == Data::ProjectNodeKind::Master)
                masterIds.append(node.id);
        }
        if (masterIds.isEmpty())
            return Utils::ResultError(Tr::tr("The selected project has no EtherCAT master."));
        if (masterIds.size() != 1) {
            return Utils::ResultError(
                Tr::tr("Select one EtherCAT master before scanning this project."));
        }
        masterId = masterIds.constFirst();
    }
    if (!containsMaster(*selectedProject, masterId)) {
        return Utils::ResultError(
            Tr::tr("The selected EtherCAT slave does not belong to an open master."));
    }
    if (operation == Data::ScanOperation::SelectedBranch && branchId.isNull()) {
        return Utils::ResultError(
            Tr::tr("Select an EtherCAT master or configured slave to rescan its branch."));
    }
    return Data::ScanRequest{selectedProject->id, masterId, operation, branchId};
}

Utils::Result<> ScanWorkflow::validateExactProviderSelection(
    const Data::ControllerConnectionScope &scope) const
{
    if (m_shuttingDown)
        return Utils::ResultError(Tr::tr("The Mock scan workflow is shutting down."));
    if (!m_provider || !m_providerRegistry || !m_scanProviderSelectionService) {
        return Utils::ResultError(
            Tr::tr("The Mock scan provider selection service is unavailable."));
    }
    if (scope.projectId.isNull() || scope.masterId.isNull())
        return Utils::ResultError(Tr::tr("The Mock scan scope is invalid."));
    if (m_providerRegistry->provider(m_provider->id()) != m_provider.data()) {
        return Utils::ResultError(
            Tr::tr("The selected local Mock scanner instance is no longer registered."));
    }

    const std::optional<Core::ScanProviderSelection> selection
        = m_scanProviderSelectionService->selection(scope);
    if (!selection) {
        return Utils::ResultError(
            Tr::tr("Select a Mock Scan Provider for this EtherCAT master before scanning."));
    }
    if (selection->scope != scope || selection->providerId != m_provider->id()) {
        return Utils::ResultError(
            Tr::tr("This EtherCAT master is assigned to a different Scan Provider."));
    }
    if (!m_scanProviderSelectionService->selectionIsAvailable(scope) || !m_provider->isAvailable()) {
        return Utils::ResultError(Tr::tr("The selected local Mock scanner is unavailable."));
    }
    return Utils::ResultOk;
}

Utils::Result<Data::ScanResult> ScanWorkflow::freshMockResult(
    const Data::ControllerConnectionScope &scope, const Data::NodeId &expectedSnapshotId) const
{
    const Utils::Result<> binding = validateExactProviderSelection(scope);
    if (!binding)
        return Utils::ResultError(binding.error());
    if (!m_topologyService)
        return Utils::ResultError(Tr::tr("The EtherCAT topology service is unavailable."));
    if (m_provider->scanState() != Data::ScanState::Completed) {
        return Utils::ResultError(
            Tr::tr("The selected Mock scan does not expose completed topology evidence."));
    }

    const Core::TopologySelection
        selection{Core::TopologyEvidenceSource::MockScan, m_provider->id(), scope};
    const Core::TopologyLookupResult lookup = m_topologyService->topology(selection);
    if (!lookup.hasFreshProviderEvidence() || !lookup.snapshot
        || lookup.snapshot->selection != selection || !lookup.snapshot->mockEvidence) {
        return Utils::ResultError(
            Tr::tr("The selected Mock topology evidence is unavailable, incomplete, or stale."));
    }

    const Data::ScanResult result = *lookup.snapshot->mockEvidence;
    if (!result.snapshot.mock || !result.snapshot.complete || !result.snapshot.capturedAt.isValid()
        || result.snapshot.id.isNull() || result.snapshot.projectId != scope.projectId
        || result.snapshot.masterId != scope.masterId
        || result.comparison.projectId != scope.projectId
        || result.comparison.masterId != scope.masterId) {
        return Utils::ResultError(
            Tr::tr("The selected Mock topology evidence is invalid for this EtherCAT master."));
    }
    if (!expectedSnapshotId.isNull() && result.snapshot.id != expectedSnapshotId) {
        return Utils::ResultError(
            Tr::tr("The Mock scan generation changed. Compare the current result again."));
    }
    const Utils::Result<> stillBound = validateExactProviderSelection(scope);
    if (!stillBound)
        return Utils::ResultError(stillBound.error());
    return result;
}

Utils::Result<> ScanWorkflow::start(Data::ScanOperation operation)
{
    if (!m_provider)
        return Utils::ResultError(Tr::tr("The local Mock scanner is unavailable."));
    const Utils::Result<Data::ScanRequest> request = requestForSelection(operation);
    if (!request)
        return Utils::ResultError(request.error());
    const Data::ControllerConnectionScope scope{request->projectId, request->masterId};
    const Utils::Result<> binding = validateExactProviderSelection(scope);
    if (!binding)
        return binding;
    if (scanIsActive(m_provider->scanState()))
        return Utils::ResultError(Tr::tr("A local Mock scan is already running."));
    if (m_provider->scanState() != Data::ScanState::Idle) {
        m_provider->clearScanResult();
        const Utils::Result<> bindingAfterClear = validateExactProviderSelection(scope);
        if (!bindingAfterClear)
            return bindingAfterClear;
    }

    const Utils::Result<> result = m_provider->startScan(*request);
    if (result) {
        const Utils::Result<> startedBinding = validateExactProviderSelection(scope);
        if (!startedBinding) {
            m_provider->cancelScan();
            return Utils::ResultError(
                Tr::tr("The Mock scan was cancelled because its explicit provider binding "
                       "changed: %1")
                    .arg(startedBinding.error()));
        }
        ::Core::MessageManager::writeSilently(
            Tr::tr("[EtherCAT Mock Scan] Started local simulation; no network is accessed."));
    }
    return result;
}

void ScanWorkflow::updateActions()
{
    if (m_actions.isEmpty())
        return;
    if (!m_provider || m_shuttingDown) {
        for (QAction *action : std::as_const(m_actions))
            action->setEnabled(false);
        emit actionStateChanged();
        return;
    }

    const Data::ScanState state = m_provider->scanState();
    const bool active = scanIsActive(state);
    const Utils::Result<Data::ScanRequest> targetRequest = requestForSelection(
        Data::ScanOperation::Slaves);
    const bool haveTarget = targetRequest
                            && validateExactProviderSelection(
                                {targetRequest->projectId, targetRequest->masterId});
    const Utils::Result<Data::ScanRequest> branchRequest = requestForSelection(
        Data::ScanOperation::SelectedBranch);
    const bool haveBranch = branchRequest
                            && validateExactProviderSelection(
                                {branchRequest->projectId, branchRequest->masterId});
    const std::optional<Data::ScanResult> result = m_provider->lastScanResult();
    const Data::ControllerConnectionScope resultScope
        = result
              ? Data::ControllerConnectionScope{result->snapshot.projectId, result->snapshot.masterId}
              : Data::ControllerConnectionScope{};
    const Utils::Result<Data::ScanResult> freshResult = [&] {
        if (result)
            return freshMockResult(resultScope, result->snapshot.id);
        return Utils::Result<Data::ScanResult>(
            Utils::ResultError(Tr::tr("No Mock scan result is available.")));
    }();
    const bool topologyResult = freshResult
                                && freshResult->snapshot.operation
                                       != Data::ScanOperation::Interfaces;
    const bool discardable = result && !active && bool(validateExactProviderSelection(resultScope));

    action(Constants::SCAN_INTERFACES_ACTION_ID)->setEnabled(!active && haveTarget);
    action(Constants::SCAN_SLAVES_ACTION_ID)->setEnabled(!active && haveTarget);
    action(Constants::RESCAN_BRANCH_ACTION_ID)->setEnabled(!active && haveBranch);
    action(Constants::COMPARE_ACTION_ID)->setEnabled(!active && topologyResult);
    action(Constants::ACCEPT_ACTION_ID)
        ->setEnabled(
            !active && topologyResult && freshResult->comparison.acceptAllowed
            && !freshResult->comparison.exactMatch);
    action(Constants::KEEP_ACTION_ID)->setEnabled(discardable);
    action(Constants::CANCEL_ACTION_ID)->setEnabled(active);
    emit actionStateChanged();
}

void ScanWorkflow::updateStatus(Data::ScanState state)
{
    if (!m_stateService || !m_provider)
        return;
    if (m_shuttingDown) {
        m_stateService->clearStatus(Constants::STATUS_SOURCE_ID);
        return;
    }
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
    ::Core::MessageManager::writeDisrupting(Tr::tr("[EtherCAT Mock Scan] %1").arg(error));
}

} // namespace EtherCAT::Scan::Internal
