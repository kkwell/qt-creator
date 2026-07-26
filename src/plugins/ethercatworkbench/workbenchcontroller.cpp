// Copyright (C) 2026 Kvell

#include "workbenchcontroller.h"

#include "esiconfigurationfactory.h"
#include "ethercatworkbenchtr.h"

#include <coreplugin/messagemanager.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <extensionsystem/pluginmanager.h>

#include <ethercatproject/ethercatprojectconstants.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/qtcassert.h>

#include <QScopedValueRollback>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace EtherCAT::Workbench::Internal {

struct ActiveMasterContext
{
    Data::ProjectSnapshot project;
    Data::NodeId masterId;
    QList<Data::OfflineSlaveConfiguration> slaves;
};

struct SelectedSlaveContext : ActiveMasterContext
{
    Data::NodeId slaveId;
    int index = -1;
};

static QList<Data::OfflineSlaveConfiguration> slavesForMaster(
    const Data::ProjectSnapshot &project, const Data::NodeId &masterId)
{
    QList<Data::OfflineSlaveConfiguration> result;
    for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
        if (slave.masterId == masterId)
            result.append(slave);
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return result;
}

static void normalizePositions(QList<Data::OfflineSlaveConfiguration> *slaves)
{
    for (int position = 0; position < slaves->size(); ++position)
        (*slaves)[position].position = position;
}

static std::optional<ActiveMasterContext> activeMasterContext(
    Core::ProjectService *projectService, const Data::NodeId &requestedMasterId = {})
{
    if (!projectService)
        return std::nullopt;
    const std::optional<Data::ProjectSnapshot> project = projectService->project(
        projectService->activeProjectId());
    if (!project || !project->valid)
        return std::nullopt;
    const auto master = std::find_if(
        project->nodes.cbegin(),
        project->nodes.cend(),
        [&requestedMasterId](const Data::ProjectNodeSnapshot &node) {
            return node.kind == Data::ProjectNodeKind::Master
                   && (requestedMasterId.isNull() || node.id == requestedMasterId);
        });
    if (master == project->nodes.cend())
        return std::nullopt;
    return ActiveMasterContext{*project, master->id, slavesForMaster(*project, master->id)};
}

static std::optional<SelectedSlaveContext> selectedSlaveContext(
    const WorkbenchTreeModel &treeModel,
    Core::SelectionService *selectionService,
    Core::ProjectService *projectService)
{
    if (!selectionService || !projectService)
        return std::nullopt;
    const Data::NodeId slaveId = selectionService->currentNodeId();
    const Core::PropertyPageContext context = treeModel.contextForNodeId(slaveId);
    if (context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave)
        return std::nullopt;
    const std::optional<Data::ProjectSnapshot> project = projectService->project(context.projectId);
    if (!project || !project->valid)
        return std::nullopt;
    const auto selected = std::find_if(
        project->slaves.cbegin(), project->slaves.cend(), [&slaveId](const auto &slave) {
            return slave.id == slaveId;
        });
    if (selected == project->slaves.cend())
        return std::nullopt;
    QList<Data::OfflineSlaveConfiguration> slaves = slavesForMaster(*project, selected->masterId);
    const auto inMaster = std::find_if(slaves.cbegin(), slaves.cend(), [&slaveId](const auto &slave) {
        return slave.id == slaveId;
    });
    if (inMaster == slaves.cend())
        return std::nullopt;
    return SelectedSlaveContext{
        {*project, selected->masterId, slaves}, slaveId, int(inMaster - slaves.cbegin())};
}

static std::optional<Data::ProjectSnapshot> selectedProject(
    const WorkbenchTreeModel &treeModel,
    Core::SelectionService *selectionService,
    Core::ProjectService *projectService)
{
    if (!selectionService || !projectService)
        return std::nullopt;
    const Core::PropertyPageContext context = treeModel.contextForNodeId(
        selectionService->currentNodeId());
    if (context.nodeKind != Core::WorkbenchNodeKind::Project)
        return std::nullopt;
    const std::optional<Data::ProjectSnapshot> project = projectService->project(context.projectId);
    if (!project || project->id != context.nodeId)
        return std::nullopt;
    return project;
}

static QString uniqueSlaveName(
    const QString &requestedName, const QList<Data::OfflineSlaveConfiguration> &slaves)
{
    const QString baseName = requestedName.trimmed().isEmpty() ? Tr::tr("EtherCAT Device")
                                                               : requestedName.trimmed();
    const auto isUsed = [&slaves](const QString &candidate) {
        return std::any_of(slaves.cbegin(), slaves.cend(), [&candidate](const auto &slave) {
            return slave.name.compare(candidate, Qt::CaseInsensitive) == 0;
        });
    };
    if (!isUsed(baseName))
        return baseName;
    for (int suffix = 2;; ++suffix) {
        const QString candidate = Tr::tr("%1 (%2)").arg(baseName).arg(suffix);
        if (!isUsed(candidate))
            return candidate;
    }
}

struct CurrentBusApplyPlan
{
    Data::ControllerConnectionScope scope;
    QList<Data::OfflineSlaveConfiguration> currentSlaves;
    QList<Data::OfflineSlaveConfiguration> candidateSlaves;
    int esiMatches = 0;
    int unknownDevices = 0;
    int ambiguousEsiMatches = 0;
    int unsupportedEsiMatches = 0;
    int preservedConfigurations = 0;
    int removedConfigurations = 0;
};

static Data::DeviceIdentity controllerIdentity(const Data::ControllerTopologySlave &slave)
{
    return {slave.vendorId, slave.productCode, slave.revision};
}

static bool configurationIsEmpty(const Data::OfflineSlaveConfiguration &slave)
{
    return slave.processData == Data::ProcessDataConfiguration{}
           && slave.startup == Data::StartupConfiguration{}
           && slave.dc == Data::DcConfiguration{};
}

static std::optional<Data::DeviceDescription> matchingDeviceDescription(
    Core::DeviceRepositoryProvider *repository,
    const QList<Data::DeviceSummary> &devices,
    const Data::ControllerTopologySlave &slave,
    const Data::OfflineSlaveConfiguration *existing,
    int *ambiguousMatches,
    int *unsupportedMatches)
{
    if (!repository)
        return std::nullopt;

    const Data::DeviceIdentity identity = controllerIdentity(slave);
    if (existing && !existing->deviceDescriptionId.isNull()) {
        const std::optional<Data::DeviceDescription> configured
            = repository->device(existing->deviceDescriptionId);
        if (configured && configured->summary.supported
            && configured->summary.identity == identity) {
            return configured;
        }
    }

    QList<Data::DeviceSummary> matches;
    for (const Data::DeviceSummary &device : devices) {
        if (device.identity == identity)
            matches.append(device);
    }
    if (matches.size() > 1) {
        if (ambiguousMatches)
            ++*ambiguousMatches;
        return std::nullopt;
    }
    if (matches.isEmpty())
        return std::nullopt;
    if (!matches.constFirst().supported) {
        if (unsupportedMatches)
            ++*unsupportedMatches;
        return std::nullopt;
    }
    return repository->device(matches.constFirst().id);
}

static Utils::Result<CurrentBusApplyPlan> currentBusApplyPlan(
    const Data::ControllerConnectionScope &scope,
    const Data::ProjectSnapshot &project,
    const Data::ControllerTopologySnapshot &topology,
    Core::DeviceRepositoryProvider *repository)
{
    if (!project.valid || project.id != scope.projectId) {
        return Utils::ResultError(
            Tr::tr("The selected EtherCAT project is invalid or no longer available."));
    }
    if (topology.result != 0) {
        return Utils::ResultError(
            Tr::tr("The current bus scan did not complete successfully."));
    }
    if (!topology.respondingCount || topology.slaves.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("The current bus scan contains no responding EtherCAT devices."));
    }
    if (topology.respondingCount != quint32(topology.slaves.size())) {
        return Utils::ResultError(
            Tr::tr("The current bus scan is incomplete and cannot configure the project."));
    }

    QList<Data::ControllerTopologySlave> sortedTopology = topology.slaves;
    std::sort(
        sortedTopology.begin(),
        sortedTopology.end(),
        [](const Data::ControllerTopologySlave &left,
           const Data::ControllerTopologySlave &right) { return left.position < right.position; });
    QSet<quint32> positions;
    for (const Data::ControllerTopologySlave &slave : std::as_const(sortedTopology)) {
        if (slave.position > quint32(std::numeric_limits<int>::max())
            || positions.contains(slave.position)) {
            return Utils::ResultError(
                Tr::tr("The current bus scan contains an invalid or duplicate position."));
        }
        if (!slave.vendorId || !slave.productCode) {
            return Utils::ResultError(
                Tr::tr("A detected EtherCAT device has an incomplete identity."));
        }
        positions.insert(slave.position);
    }

    CurrentBusApplyPlan plan;
    plan.scope = scope;
    plan.currentSlaves = slavesForMaster(project, scope.masterId);
    const QList<Data::DeviceSummary> devices = repository ? repository->devices()
                                                          : QList<Data::DeviceSummary>();

    for (const Data::ControllerTopologySlave &topologySlave : std::as_const(sortedTopology)) {
        const int position = int(topologySlave.position);
        const Data::DeviceIdentity identity = controllerIdentity(topologySlave);
        const auto existing = std::find_if(
            plan.currentSlaves.cbegin(),
            plan.currentSlaves.cend(),
            [position](const Data::OfflineSlaveConfiguration &slave) {
                return slave.position == position;
            });
        const bool preserveExisting = existing != plan.currentSlaves.cend()
                                      && existing->identity == identity;
        const Data::OfflineSlaveConfiguration *existingPointer
            = preserveExisting ? &*existing : nullptr;
        const std::optional<Data::DeviceDescription> device = matchingDeviceDescription(
            repository,
            devices,
            topologySlave,
            existingPointer,
            &plan.ambiguousEsiMatches,
            &plan.unsupportedEsiMatches);

        Data::OfflineSlaveConfiguration candidate;
        if (preserveExisting) {
            candidate = *existing;
            candidate.serialNumber = topologySlave.serial;
            ++plan.preservedConfigurations;
            if (device) {
                candidate.deviceDescriptionId = device->summary.id;
                if (configurationIsEmpty(candidate)) {
                    candidate.processData = processDataDefaultsFromDevice(*device, candidate.id);
                    candidate.startup = startupDefaultsFromDevice(*device, candidate.id);
                    candidate.dc = dcDefaultsFromDevice(*device);
                }
            } else {
                candidate.deviceDescriptionId = {};
            }
        } else if (device) {
            const QString requestedName = device->summary.name.isEmpty()
                                              ? device->summary.typeName
                                              : device->summary.name;
            const Data::NodeId slaveId = Data::NodeId::create();
            candidate = offlineSlaveFromDevice(
                *device,
                slaveId,
                scope.masterId,
                position,
                uniqueSlaveName(requestedName, plan.candidateSlaves));
            candidate.serialNumber = topologySlave.serial;
        } else {
            candidate.id = Data::NodeId::create();
            candidate.masterId = scope.masterId;
            candidate.position = position;
            candidate.identity = identity;
            candidate.serialNumber = topologySlave.serial;
            candidate.name = uniqueSlaveName(
                Tr::tr("Unknown EtherCAT Device %1").arg(position + 1),
                plan.candidateSlaves);
        }

        if (device)
            ++plan.esiMatches;
        else
            ++plan.unknownDevices;
        plan.candidateSlaves.append(candidate);
    }

    plan.removedConfigurations = plan.currentSlaves.size() - plan.preservedConfigurations;
    return plan;
}

static int optionalProviderScore(Core::Provider *provider, Core::ProviderKind kind)
{
    if (kind == Core::ProviderKind::Scan) {
        const auto scan = qobject_cast<Core::ScanProvider *>(provider);
        if (!scan)
            return -1;
        if (scan->lastScanResult())
            return 3;
        switch (scan->scanState()) {
        case Data::ScanState::Preparing:
        case Data::ScanState::ScanningMaster:
        case Data::ScanState::ScanningSlaves:
        case Data::ScanState::BuildingSnapshot:
        case Data::ScanState::Comparing:
            return 2;
        case Data::ScanState::Failed:
            return 1;
        default:
            return 0;
        }
    }

    const auto diagnostics = qobject_cast<Core::DiagnosticsProvider *>(provider);
    if (!diagnostics)
        return -1;
    if (diagnostics->latestSnapshot())
        return 2;
    return diagnostics->streamState() == Data::DiagnosticsStreamState::Stopped ? 0 : 1;
}

static Core::Provider *preferredOptionalProvider(
    Core::ProviderRegistry *registry,
    Core::ProviderKind kind,
    Core::Provider *excluding)
{
    if (!registry)
        return nullptr;

    QList<Core::Provider *> providers;
    for (Core::Provider *provider : registry->providers(kind)) {
        if (provider != excluding)
            providers.append(provider);
    }
    std::sort(providers.begin(), providers.end(), [kind](auto *left, auto *right) {
        const int leftScore = optionalProviderScore(left, kind);
        const int rightScore = optionalProviderScore(right, kind);
        const bool leftUsable = left->isAvailable() && leftScore >= 0;
        const bool rightUsable = right->isAvailable() && rightScore >= 0;
        if (leftUsable != rightUsable)
            return leftUsable;
        if (leftUsable && leftScore != rightScore)
            return leftScore > rightScore;
        return left->id().toString() < right->id().toString();
    });
    return providers.isEmpty() ? nullptr : providers.first();
}

static OptionalProviderPresentation optionalProviderPresentation(
    Core::Provider *provider, Core::ProviderKind kind)
{
    if (!provider)
        return {};
    const bool available = provider->isAvailable() && optionalProviderScore(provider, kind) >= 0;
    OptionalProviderPresentation presentation{
        available ? OptionalProviderState::Available : OptionalProviderState::Unavailable,
        provider->displayName().trimmed()};
    presentation.displayName = optionalProviderDisplayName(presentation, kind);
    return presentation;
}

static bool connectionProfileUsable(const Data::ControllerConnectionProfile &profile)
{
    return !profile.id.isNull() && profile.configured && profile.supported;
}

static bool connectionStateLocksSelection(Data::ControllerConnectionState state)
{
    return state != Data::ControllerConnectionState::Disconnected
           && state != Data::ControllerConnectionState::Failed;
}

static bool controllerPackageIsActive(const Data::ControllerConnectionSnapshot &snapshot)
{
    return snapshot.package && snapshot.package->activeSlot != Data::ControllerSlot::None
           && snapshot.package->controllerState == Data::ControllerPackageState::Active
           && snapshot.session
           && snapshot.package->controllerBootId == snapshot.session->bootId;
}

static bool hasUnmanagedEtherCATProject(Core::ProjectService *projectService)
{
    if (!projectService)
        return false;
    const QList<ProjectExplorer::Project *> projects = ProjectExplorer::ProjectManager::projects();
    return std::any_of(
        projects.cbegin(),
        projects.cend(),
        [projectService](const ProjectExplorer::Project *project) {
            return project
                   && project->type()
                          == Utils::Id(::EtherCAT::Project::Constants::PROJECT_ID)
                   && !projectService->managesProject(project);
        });
}

static bool controllerControlLeaseOwnershipUnverified(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    return snapshot.session && !snapshot.session->ownsControlLease
           && snapshot.session->controlLeaseOwnerSessionId != 0
           && snapshot.session->controlLeaseOwnerSessionId == snapshot.session->sessionId;
}

static QString controllerControlStateUnavailableReason(
    const Data::ControllerConnectionSnapshot &snapshot, Data::ControllerControlCommand command)
{
    const bool ownsControlLease = snapshot.session && snapshot.session->ownsControlLease;
    const std::optional<Data::ControllerStateSummary> &controllerState = snapshot.controllerState;
    using Command = Data::ControllerControlCommand;
    using ServiceState = Data::ControllerServiceState;

    const auto requireOwnedLease = [&snapshot, ownsControlLease] {
        if (!snapshot.session)
            return Tr::tr("The controller session has not been established.");
        if (controllerControlLeaseOwnershipUnverified(snapshot)) {
            return Tr::tr(
                "The controller control lease ownership is unverified for this session.");
        }
        if (!ownsControlLease)
            return Tr::tr("Acquire the controller control lease first.");
        return QString();
    };
    const auto requireReadyState = [&controllerState] {
        if (!controllerState)
            return Tr::tr("The controller state is unavailable.");
        if (!controllerState->ready)
            return Tr::tr("The controller is not ready for control commands.");
        return QString();
    };
    const auto requireActivePackage = [&snapshot] {
        if (!controllerPackageIsActive(snapshot))
            return Tr::tr("The active controller package is not ready for this session.");
        return QString();
    };

    switch (command) {
    case Command::None:
        return Tr::tr("No controller control operation is selected.");
    case Command::AcquireControl:
        if (!snapshot.session)
            return Tr::tr("The controller session has not been established.");
        if (ownsControlLease)
            return Tr::tr("This session already owns the controller control lease.");
        if (controllerControlLeaseOwnershipUnverified(snapshot)) {
            return Tr::tr(
                "The controller control lease ownership is unverified for this session.");
        }
        if (snapshot.session->controlLeaseOwnerSessionId)
            return Tr::tr("The controller control lease is owned by another session.");
        return {};
    case Command::ReleaseControl:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        return {};
    case Command::EnterConfigurationMode:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (controllerState->serviceState != ServiceState::OperationalSafe
            && controllerState->serviceState != ServiceState::Running
            && controllerState->serviceState != ServiceState::Fault
            && controllerState->serviceState != ServiceState::Paused
            && controllerState->serviceState != ServiceState::Shutdown) {
            return Tr::tr("The controller service state does not allow configuration mode.");
        }
        return {};
    case Command::DiscoverTopology:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (!snapshot.package)
            return Tr::tr("The controller package state is unavailable.");
        if (snapshot.package->controllerState == Data::ControllerPackageState::Active)
            return Tr::tr("Deactivate the controller package before scanning the bus.");
        if (controllerState->serviceState != ServiceState::Shutdown)
            return Tr::tr("The controller must be in Shutdown before scanning the bus.");
        return {};
    case Command::RestoreActivePackage:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (!snapshot.package || snapshot.package->activeSlot == Data::ControllerSlot::None
            || !snapshot.package->activeGeneration
            || !snapshot.package->activeConfigurationId) {
            return Tr::tr("No restorable active controller package is available.");
        }
        if (controllerState->serviceState != ServiceState::OperationalSafe
            && controllerState->serviceState != ServiceState::Shutdown) {
            return Tr::tr("Restore is available only in OP_SAFE or Shutdown.");
        }
        return {};
    case Command::Start:
    case Command::StartFreeRun:
    case Command::StartDistributedClocks:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireActivePackage(); !reason.isEmpty())
            return reason;
        if (controllerState->serviceState != ServiceState::OperationalSafe)
            return Tr::tr("The controller must be in OP_SAFE before starting.");
        if (!controllerState->busOperational || !(controllerState->ethercatAlStateBits & 0x08))
            return Tr::tr("The EtherCAT bus has not reached Operational state.");
        if (!controllerState->expectedWorkingCounter)
            return Tr::tr("The expected EtherCAT working counter is unavailable.");
        if (controllerState->actualWorkingCounter
            != controllerState->expectedWorkingCounter) {
            return Tr::tr("The EtherCAT working counter does not match its expected value.");
        }
        if (controllerState->currentFaults || controllerState->latchedFaults)
            return Tr::tr("Clear current and latched controller faults before starting.");
        return {};
    case Command::Pause:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireActivePackage(); !reason.isEmpty())
            return reason;
        if (controllerState->serviceState != ServiceState::Running)
            return Tr::tr("Pause is available only while the controller is Running.");
        return {};
    case Command::Resume:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireActivePackage(); !reason.isEmpty())
            return reason;
        if (controllerState->serviceState != ServiceState::Paused)
            return Tr::tr("Resume is available only while the controller is Paused.");
        if (!controllerState->busOperational || !(controllerState->ethercatAlStateBits & 0x08))
            return Tr::tr("The EtherCAT bus has not reached Operational state.");
        if (!controllerState->expectedWorkingCounter)
            return Tr::tr("The expected EtherCAT working counter is unavailable.");
        if (controllerState->actualWorkingCounter
            != controllerState->expectedWorkingCounter) {
            return Tr::tr("The EtherCAT working counter does not match its expected value.");
        }
        if (controllerState->currentFaults || controllerState->latchedFaults)
            return Tr::tr("Clear current and latched controller faults before resuming.");
        return {};
    case Command::ControlledStop:
        if (const QString reason = requireOwnedLease(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireReadyState(); !reason.isEmpty())
            return reason;
        if (const QString reason = requireActivePackage(); !reason.isEmpty())
            return reason;
        if (controllerState->serviceState != ServiceState::Running
            && controllerState->serviceState != ServiceState::Paused) {
            return Tr::tr("Controlled Stop is available only while Running or Paused.");
        }
        return {};
    }
    return Tr::tr("The controller service state does not allow this operation.");
}

constexpr int controllerCleanupRefreshIntervalPolls = 5;
constexpr int controllerCleanupMaximumDisconnectAttempts = 5;
constexpr int controllerCleanupMaximumGenerationChanges = 3;

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

static QString controllerControlCommandName(Data::ControllerControlCommand command)
{
    using Command = Data::ControllerControlCommand;
    switch (command) {
    case Command::None:
        return Tr::tr("Controller control");
    case Command::AcquireControl:
        return Tr::tr("Acquire control");
    case Command::ReleaseControl:
        return Tr::tr("Release control");
    case Command::EnterConfigurationMode:
        return Tr::tr("Enter configuration");
    case Command::DiscoverTopology:
        return Tr::tr("Scan bus");
    case Command::RestoreActivePackage:
        return Tr::tr("Restore package");
    case Command::Start:
        return Tr::tr("Start");
    case Command::StartFreeRun:
        return Tr::tr("Start free-run");
    case Command::StartDistributedClocks:
        return Tr::tr("Start distributed clocks");
    case Command::Pause:
        return Tr::tr("Pause");
    case Command::Resume:
        return Tr::tr("Resume");
    case Command::ControlledStop:
        return Tr::tr("Controlled stop");
    }
    return Tr::tr("Controller control");
}

static QString controllerControlStateName(Data::ControllerControlState state)
{
    using State = Data::ControllerControlState;
    switch (state) {
    case State::Idle:
        return Tr::tr("idle");
    case State::Pending:
        return Tr::tr("in progress");
    case State::Succeeded:
        return Tr::tr("succeeded");
    case State::Failed:
        return Tr::tr("failed");
    }
    return Tr::tr("unknown");
}

static QString controllerPackageDeploymentStateName(Data::ControllerPackageDeploymentState state)
{
    using State = Data::ControllerPackageDeploymentState;
    switch (state) {
    case State::Idle:
        return Tr::tr("idle");
    case State::Uploading:
        return Tr::tr("uploading");
    case State::Committing:
        return Tr::tr("committing");
    case State::Validating:
        return Tr::tr("validating");
    case State::Activating:
        return Tr::tr("activating");
    case State::RollingBack:
        return Tr::tr("rolling back");
    case State::Canceling:
        return Tr::tr("canceling");
    case State::Succeeded:
        return Tr::tr("succeeded");
    case State::Canceled:
        return Tr::tr("canceled");
    case State::Failed:
        return Tr::tr("failed");
    case State::OutcomeUnknown:
        return Tr::tr("outcome unknown");
    }
    return Tr::tr("unknown");
}

static QString controllerOutputFingerprint(const Data::ControllerConnectionSnapshot &snapshot)
{
    QStringList fields{QString::number(int(snapshot.state))};
    if (snapshot.state != Data::ControllerConnectionState::Disconnected) {
        fields.append(QString::number(snapshot.sessionGeneration));
        fields.append(snapshot.endpointSummary);
    }
    for (const Data::ControllerChannelStatus &channel : snapshot.channels) {
        fields.append(
            QStringLiteral("%1:%2").arg(channel.id).arg(int(channel.state)));
    }
    if (snapshot.session) {
        fields.append(
            QStringLiteral("session:%1:%2:%3:%4")
                .arg(snapshot.session->sessionId)
                .arg(snapshot.session->bootId)
                .arg(snapshot.session->controlLeaseOwnerSessionId)
                .arg(snapshot.session->ownsControlLease));
    }
    if (snapshot.controllerState) {
        fields.append(
            QStringLiteral("state:%1:%2:%3:%4:%5:%6:%7:%8")
                .arg(int(snapshot.controllerState->serviceState))
                .arg(snapshot.controllerState->actualWorkingCounter)
                .arg(snapshot.controllerState->expectedWorkingCounter)
                .arg(snapshot.controllerState->busOperational)
                .arg(snapshot.controllerState->applicationActive)
                .arg(snapshot.controllerState->safeOutput)
                .arg(snapshot.controllerState->currentFaults)
                .arg(snapshot.controllerState->latchedFaults));
    }
    const Data::ControllerControlProgress &progress = snapshot.controlProgress;
    fields.append(
        QStringLiteral("control:%1:%2:%3:%4:%5:%6")
            .arg(int(progress.command))
            .arg(int(progress.state))
            .arg(progress.stage)
            .arg(progress.status ? QString::number(*progress.status) : QString())
            .arg(
                progress.operationResult ? QString::number(*progress.operationResult)
                                         : QString())
            .arg(progress.detail));
    const Data::ControllerPackageDeploymentProgress &deployment = snapshot.packageDeploymentProgress;
    fields.append(QStringLiteral("deployment:%1:%2:%3:%4:%5:%6:%7")
                      .arg(deployment.operationId)
                      .arg(int(deployment.state))
                      .arg(deployment.transferredBytes)
                      .arg(deployment.totalBytes)
                      .arg(deployment.status ? QString::number(*deployment.status) : QString())
                      .arg(
                          deployment.operationResult ? QString::number(*deployment.operationResult)
                                                     : QString())
                      .arg(deployment.detail));
    if (snapshot.topology) {
        fields.append(
            QStringLiteral("topology:%1:%2:%3:%4")
                .arg(snapshot.topology->respondingCount)
                .arg(snapshot.topology->slaves.size())
                .arg(snapshot.topology->result)
                .arg(snapshot.topology->discoveredAt.toMSecsSinceEpoch()));
    }
    if (snapshot.lastError) {
        fields.append(
            QStringLiteral("error:%1:%2:%3:%4")
                .arg(
                    snapshot.lastError->code ? QString::number(*snapshot.lastError->code)
                                             : QString())
                .arg(snapshot.lastError->codeName)
                .arg(snapshot.lastError->summary)
                .arg(snapshot.lastError->detail));
    }
    return fields.join(QLatin1Char('|'));
}

static ControllerOutputLevel controllerOutputLevel(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    if (snapshot.state == Data::ControllerConnectionState::Failed || snapshot.lastError
        || snapshot.controlProgress.state == Data::ControllerControlState::Failed
        || snapshot.packageDeploymentProgress.state == Data::ControllerPackageDeploymentState::Failed
        || snapshot.packageDeploymentProgress.state
               == Data::ControllerPackageDeploymentState::OutcomeUnknown
        || (snapshot.controllerState
            && (snapshot.controllerState->serviceState == Data::ControllerServiceState::Fault
                || snapshot.controllerState->currentFaults
                || snapshot.controllerState->latchedFaults))) {
        return ControllerOutputLevel::Error;
    }
    if (snapshot.state == Data::ControllerConnectionState::Degraded || snapshot.readOnly
        || snapshot.packageDeploymentProgress.state
               == Data::ControllerPackageDeploymentState::Canceled
        || (snapshot.state == Data::ControllerConnectionState::Connected
            && snapshot.session && !snapshot.session->ownsControlLease)) {
        return ControllerOutputLevel::Warning;
    }
    return ControllerOutputLevel::Information;
}

static QString controllerOutputMessage(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionSnapshot &snapshot)
{
    QString providerName = provider ? provider->displayName().trimmed() : QString();
    if (providerName.isEmpty())
        providerName = Tr::tr("Controller");
    QStringList fields{
        Tr::tr("%1: %2").arg(providerName, controllerConnectionStateName(snapshot.state)),
    };
    if (!snapshot.endpointSummary.isEmpty())
        fields.append(Tr::tr("Endpoint %1").arg(snapshot.endpointSummary));
    if (snapshot.session) {
        fields.append(
            snapshot.session->ownsControlLease
                ? Tr::tr("Exclusive control acquired")
                : snapshot.session->controlLeaseOwnerSessionId
                      ? Tr::tr("Control lease owner %1")
                            .arg(snapshot.session->controlLeaseOwnerSessionId)
                      : Tr::tr("Control lease not acquired"));
    }
    if (snapshot.controllerState) {
        fields.append(
            Tr::tr("Service %1")
                .arg(controllerServiceStateName(snapshot.controllerState->serviceState)));
        fields.append(
            Tr::tr("WKC %1/%2")
                .arg(snapshot.controllerState->actualWorkingCounter)
                .arg(snapshot.controllerState->expectedWorkingCounter));
    }
    if (snapshot.controlProgress.state != Data::ControllerControlState::Idle) {
        QString progress = Tr::tr("%1 %2")
                               .arg(
                                   controllerControlCommandName(snapshot.controlProgress.command),
                                   controllerControlStateName(snapshot.controlProgress.state));
        if (!snapshot.controlProgress.detail.isEmpty())
            progress += Tr::tr(" (%1)").arg(snapshot.controlProgress.detail);
        fields.append(progress);
    }
    if (snapshot.packageDeploymentProgress.state != Data::ControllerPackageDeploymentState::Idle) {
        const Data::ControllerPackageDeploymentProgress &deployment
            = snapshot.packageDeploymentProgress;
        QString progress = Tr::tr("Package deployment %1")
                               .arg(controllerPackageDeploymentStateName(deployment.state));
        if (!deployment.operationId.isEmpty())
            progress += Tr::tr(" [%1]").arg(deployment.operationId);
        if (deployment.totalBytes > 0) {
            progress += Tr::tr(" %1/%2 bytes")
                            .arg(deployment.transferredBytes)
                            .arg(deployment.totalBytes);
        }
        if (!deployment.detail.isEmpty())
            progress += Tr::tr(" (%1)").arg(deployment.detail);
        fields.append(progress);
    }
    if (snapshot.topology) {
        fields.append(
            Tr::tr("Bus scan %1 responding, %2 listed")
                .arg(snapshot.topology->respondingCount)
                .arg(snapshot.topology->slaves.size()));
    }
    if (snapshot.lastError) {
        QString error = snapshot.lastError->summary;
        if (error.isEmpty())
            error = snapshot.lastError->detail;
        if (!snapshot.lastError->codeName.isEmpty()) {
            error = error.isEmpty()
                        ? snapshot.lastError->codeName
                        : Tr::tr("%1 (%2)").arg(error, snapshot.lastError->codeName);
        }
        if (!error.isEmpty())
            fields.append(Tr::tr("Error: %1").arg(error));
    }
    return fields.join(QStringLiteral(" — "));
}

WorkbenchController::WorkbenchController(QObject *parent)
    : QObject(parent)
{
    m_selectionService = ExtensionSystem::PluginManager::getObject<Core::SelectionService>();
    m_projectService = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    m_deviceRepository
        = ExtensionSystem::PluginManager::getObject<Core::DeviceRepositoryProvider>();
    m_providerRegistry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();

    QTC_ASSERT(m_selectionService, return);
    QTC_ASSERT(m_projectService, return);
    QTC_ASSERT(m_deviceRepository, return);
    QTC_ASSERT(m_providerRegistry, return);

    m_connections.append(connect(
        this,
        &WorkbenchController::controllerConnectionChanged,
        this,
        &WorkbenchController::scheduleControllerAutoAcquire));
    m_treeModel.setDeviceDropHandler(
        [this](const Data::NodeId &deviceId, const Data::NodeId &masterId) {
            const Utils::Result<> result = addDeviceToMaster(deviceId, masterId);
            if (result)
                return true;
            ::Core::MessageManager::writeFlashing(
                Tr::tr("Cannot drop the ESI device: %1").arg(result.error()));
            return false;
        });

    m_connections.append(connect(
        m_projectService,
        &Core::ProjectService::projectAdded,
        this,
        [this] { refreshProjects(); }));
    m_connections.append(connect(
        m_projectService,
        &Core::ProjectService::projectAboutToBeRemoved,
        this,
        [this](const Data::NodeId &projectId) {
            handleProjectAboutToBeRemoved(projectId);
            QMetaObject::invokeMethod(
                this, &WorkbenchController::refreshProjects, Qt::QueuedConnection);
        }));
    m_connections.append(connect(
        m_projectService,
        &Core::ProjectService::projectChanged,
        this,
        [this] { refreshProjects(); }));
    m_connections.append(connect(
        m_projectService,
        &Core::ProjectService::activeProjectChanged,
        this,
        [this] {
            refreshProjects();
            scheduleControllerAutoAcquire();
        }));
    m_connections.append(
        connect(m_selectionService, &Core::SelectionService::currentNodeChanged, this, [this] {
            handleControllerConnectionChanged();
        }));
    m_connections.append(connect(
        m_deviceRepository,
        &Core::DeviceRepositoryProvider::devicesReset,
        this,
        &WorkbenchController::refreshDevices));
    m_connections.append(connect(
        m_deviceRepository,
        &Core::DeviceRepositoryProvider::devicesChanged,
        this,
        [this] { refreshDevices(); }));
    m_connections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAdded,
        this,
        [this](Core::Provider *provider) {
            watchOptionalProvider(provider);
            watchControllerConnectionProvider(provider);
            refreshOptionalProviders();
            if (provider && provider->kind() == Core::ProviderKind::ControllerConnection)
                handleControllerConnectionChanged();
        }));
    m_connections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        this,
        [this](Core::Provider *provider) {
            refreshOptionalProviders(provider);
            if (provider && provider->kind() == Core::ProviderKind::ControllerConnection) {
                if (auto connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(
                        provider)) {
                    const auto cleanup = m_controllerCleanupStates.find(connectionProvider);
                    if (cleanup != m_controllerCleanupStates.end()) {
                        writeControllerOutput(
                            Tr::tr(
                                "Automatic controller cleanup stopped safely because the "
                                "controller "
                                "adapter was removed. The connection was not reported as "
                                "disconnected."),
                            ControllerOutputLevel::Error);
                        m_controllerCleanupStates.erase(cleanup);
                    }
                    if (m_controllerStartupStates.remove(connectionProvider)) {
                        writeControllerOutput(
                            Tr::tr(
                                "Automatic controller startup stopped because the controller "
                                "adapter was removed."),
                            ControllerOutputLevel::Error);
                    }
                    m_controllerConnectionProviderEpochs.remove(connectionProvider);
                    m_controllerAutoAcquireStates.remove(connectionProvider);
                    m_controllerOutputFingerprints.remove(connectionProvider);
                }
                handleControllerConnectionChanged();
            }
        }));
    for (Core::Provider *provider : m_providerRegistry->providers()) {
        watchOptionalProvider(provider);
        watchControllerConnectionProvider(provider);
    }
    refreshOptionalProviders();
    refresh();
}

WorkbenchController::~WorkbenchController()
{
    shutdown();
}

WorkbenchTreeModel *WorkbenchController::treeModel()
{
    return &m_treeModel;
}

Core::SelectionService *WorkbenchController::selectionService() const
{
    return m_selectionService;
}

Core::ProjectService *WorkbenchController::projectService() const
{
    return m_projectService;
}

Core::DeviceRepositoryProvider *WorkbenchController::deviceRepository() const
{
    return m_deviceRepository;
}

Core::ProviderRegistry *WorkbenchController::providerRegistry() const
{
    return m_providerRegistry;
}

OptionalProviderPresentation WorkbenchController::scanProviderPresentation() const
{
    return m_scanProvider;
}

OptionalProviderPresentation WorkbenchController::diagnosticsProviderPresentation() const
{
    return m_diagnosticsProvider;
}

DiagnosticsStatusPresentation WorkbenchController::diagnosticsStatusPresentation() const
{
    return m_diagnosticsStatus;
}

std::optional<Data::ScanResult> WorkbenchController::automationScanResult(
    const Data::ControllerConnectionScope &scope) const
{
    if (m_shuttingDown)
        return std::nullopt;
    Core::Provider *provider
        = preferredOptionalProvider(m_providerRegistry, Core::ProviderKind::Scan, nullptr);
    const auto scan = qobject_cast<Core::ScanProvider *>(provider);
    if (!scan || !scan->isAvailable())
        return std::nullopt;
    const std::optional<Data::ScanResult> result = scan->lastScanResult();
    if (!result || result->snapshot.projectId != scope.projectId
        || result->snapshot.masterId != scope.masterId) {
        return std::nullopt;
    }
    return result;
}

std::optional<Data::DiagnosticsSnapshot>
WorkbenchController::automationDiagnosticsSnapshot(
    const Data::ControllerConnectionScope &scope) const
{
    if (m_shuttingDown)
        return std::nullopt;
    Core::Provider *provider
        = preferredOptionalProvider(m_providerRegistry, Core::ProviderKind::Diagnostics, nullptr);
    const auto diagnostics = qobject_cast<Core::DiagnosticsProvider *>(provider);
    if (!diagnostics || !diagnostics->isAvailable())
        return std::nullopt;
    const std::optional<Data::DiagnosticsSnapshot> snapshot
        = diagnostics->latestSnapshot();
    if (!snapshot || snapshot->projectId != scope.projectId
        || snapshot->masterId != scope.masterId) {
        return std::nullopt;
    }
    return snapshot;
}

QList<Core::ControllerConnectionProvider *> WorkbenchController::controllerConnectionProviders() const
{
    QList<Core::ControllerConnectionProvider *> result;
    if (!m_providerRegistry)
        return result;
    for (Core::Provider *provider :
         m_providerRegistry->providers(Core::ProviderKind::ControllerConnection)) {
        if (auto connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider))
            result.append(connectionProvider);
    }
    std::sort(result.begin(), result.end(), [](const auto *left, const auto *right) {
        const int displayOrder
            = left->displayName().compare(right->displayName(), Qt::CaseInsensitive);
        if (displayOrder != 0)
            return displayOrder < 0;
        return left->id().toString() < right->id().toString();
    });
    return result;
}

std::optional<Data::ControllerConnectionScope>
WorkbenchController::uniqueMasterControllerConnectionScope(const Data::ProjectSnapshot &project)
{
    if (!project.valid || project.id.isNull())
        return std::nullopt;

    Data::NodeId masterId;
    int masterCount = 0;
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind != Data::ProjectNodeKind::Master)
            continue;
        ++masterCount;
        if (masterCount > 1)
            return std::nullopt;
        masterId = node.id;
    }
    if (masterCount != 1 || masterId.isNull())
        return std::nullopt;
    return Data::ControllerConnectionScope{project.id, masterId};
}

std::optional<Data::ControllerConnectionScope>
WorkbenchController::quickControllerControlScope() const
{
    if (m_shuttingDown || !m_selectionService || !m_projectService
        || hasUnmanagedEtherCATProject(m_projectService)) {
        return std::nullopt;
    }

    const Core::PropertyPageContext selectedContext = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (!selectedContext.projectId.isNull()) {
        const std::optional<Data::ProjectSnapshot> selectedProject
            = m_projectService->project(selectedContext.projectId);
        if (!selectedProject || selectedProject->id != selectedContext.projectId)
            return std::nullopt;
        return uniqueMasterControllerConnectionScope(*selectedProject);
    }

    const QList<Data::ProjectSnapshot> projects = m_projectService->projects();
    if (projects.size() != 1)
        return std::nullopt;
    return uniqueMasterControllerConnectionScope(projects.constFirst());
}

QString WorkbenchController::quickControllerControlScopeUnavailableReason() const
{
    if (!m_selectionService || !m_projectService)
        return Tr::tr("The EtherCAT project service is unavailable.");
    if (hasUnmanagedEtherCATProject(m_projectService)) {
        return Tr::tr(
            "An open EtherCAT project is not the registered owner of its project ID. Close the "
            "conflicting duplicate project or assign it a unique project ID.");
    }

    const Core::PropertyPageContext selectedContext = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (!selectedContext.projectId.isNull()) {
        const std::optional<Data::ProjectSnapshot> selectedProject
            = m_projectService->project(selectedContext.projectId);
        if (!selectedProject || selectedProject->id != selectedContext.projectId)
            return Tr::tr("The selected EtherCAT project is no longer available.");
        if (!selectedProject->valid) {
            return Tr::tr(
                "The selected EtherCAT project is invalid. Fix its reported errors before "
                "controlling the controller.");
        }
        return Tr::tr("The selected EtherCAT project must contain exactly one valid Master.");
    }

    const QList<Data::ProjectSnapshot> projects = m_projectService->projects();
    if (projects.isEmpty())
        return Tr::tr("Open a valid EtherCAT project before controlling the controller.");
    if (projects.size() > 1) {
        return Tr::tr(
            "Select a node in the EtherCAT project whose controller you want to control.");
    }
    if (!projects.constFirst().valid) {
        return Tr::tr(
            "The open EtherCAT project is invalid. Fix its reported errors before controlling "
            "the controller.");
    }
    return Tr::tr("The open EtherCAT project must contain exactly one valid Master.");
}

std::optional<Data::ControllerConnectionScope>
WorkbenchController::activeControllerConnectionScope() const
{
    if (m_shuttingDown || !m_projectService)
        return std::nullopt;
    const Data::NodeId activeProjectId = m_projectService->activeProjectId();
    if (activeProjectId.isNull())
        return std::nullopt;
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(activeProjectId);
    if (!project || project->id != activeProjectId)
        return std::nullopt;
    return uniqueMasterControllerConnectionScope(*project);
}

std::optional<Data::ControllerConnectionScope>
WorkbenchController::selectedControllerConnectionScope() const
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return std::nullopt;
    const Core::PropertyPageContext context = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (context.nodeKind != Core::WorkbenchNodeKind::Master || context.projectId.isNull()
        || context.nodeId.isNull()) {
        return std::nullopt;
    }
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(
        context.projectId);
    if (!project || !project->valid)
        return std::nullopt;
    const bool masterExists
        = std::any_of(project->nodes.cbegin(), project->nodes.cend(), [&context](const auto &node) {
              return node.id == context.nodeId && node.kind == Data::ProjectNodeKind::Master;
          });
    if (!masterExists)
        return std::nullopt;
    return Data::ControllerConnectionScope{context.projectId, context.nodeId};
}

bool WorkbenchController::controllerConnectionScopeIsValid(
    const Data::ControllerConnectionScope &scope) const
{
    if (!m_projectService || scope.projectId.isNull() || scope.masterId.isNull())
        return false;
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(scope.projectId);
    if (!project || !project->valid || project->id != scope.projectId)
        return false;
    return std::any_of(
        project->nodes.cbegin(),
        project->nodes.cend(),
        [&scope](const Data::ProjectNodeSnapshot &node) {
            return node.kind == Data::ProjectNodeKind::Master && node.id == scope.masterId;
        });
}

ControllerConnectionSelection WorkbenchController::prepareControllerConnection(
    const Data::ControllerConnectionScope &scope)
{
    ControllerConnectionSelection selection = controllerConnectionSelection(scope);
    if (m_shuttingDown || !controllerConnectionScopeIsValid(scope)
        || controllerConnectionSelectionLocked(scope)
        || (selection.providerExplicitlySelected && selection.profileExplicitlySelected)) {
        return selection;
    }

    const std::optional<ControllerConnectionSelection> automatic
        = automaticControllerConnectionSelection(scope, controllerConnectionProviders());
    if (!automatic)
        return selection;

    ControllerConnectionSelection *stored = mutableControllerConnectionSelection(scope);
    if (!stored) {
        ControllerConnectionSelection unselected;
        unselected.scope = scope;
        m_controllerConnectionSelections.append(unselected);
        stored = &m_controllerConnectionSelections.last();
    }
    if (*stored == *automatic)
        return *stored;
    *stored = *automatic;
    emit controllerConnectionChanged();
    return *stored;
}

std::optional<ControllerConnectionSelection>
WorkbenchController::automaticControllerConnectionSelection(
    const Data::ControllerConnectionScope &scope,
    const QList<Core::ControllerConnectionProvider *> &candidates)
{
    QList<Core::ControllerConnectionProvider *> providers;
    for (Core::ControllerConnectionProvider *provider : candidates) {
        if (provider && provider->isAvailable())
            providers.append(provider);
    }
    if (providers.size() != 1)
        return std::nullopt;

    Core::ControllerConnectionProvider *provider = providers.constFirst();
    QList<Data::ControllerConnectionProfile> profiles;
    for (const Data::ControllerConnectionProfile &profile : provider->connectionProfiles(scope)) {
        if (connectionProfileUsable(profile))
            profiles.append(profile);
    }
    if (profiles.size() != 1)
        return std::nullopt;
    return ControllerConnectionSelection{
        scope,
        provider->id(),
        profiles.constFirst().id,
        true,
        true,
    };
}

ControllerConnectionSelection WorkbenchController::controllerConnectionSelection(
    const Data::ControllerConnectionScope &scope) const
{
    const auto selection = std::find_if(
        m_controllerConnectionSelections.cbegin(),
        m_controllerConnectionSelections.cend(),
        [&scope](const ControllerConnectionSelection &candidate) {
            return candidate.scope == scope;
        });
    if (selection != m_controllerConnectionSelections.cend())
        return *selection;
    ControllerConnectionSelection unselected;
    unselected.scope = scope;
    return unselected;
}

Core::ControllerConnectionProvider *WorkbenchController::controllerConnectionProvider(
    const Data::ControllerConnectionScope &scope) const
{
    const Utils::Id providerId = controllerConnectionSelection(scope).providerId;
    if (!providerId.isValid())
        return nullptr;
    const QList<Core::ControllerConnectionProvider *> providers = controllerConnectionProviders();
    const auto provider
        = std::find_if(providers.cbegin(), providers.cend(), [providerId](const auto *candidate) {
              return candidate->id() == providerId;
          });
    return provider == providers.cend() ? nullptr : *provider;
}

QList<Data::ControllerConnectionProfile> WorkbenchController::controllerConnectionProfiles(
    const Data::ControllerConnectionScope &scope) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    return provider ? provider->connectionProfiles(scope)
                    : QList<Data::ControllerConnectionProfile>();
}

std::optional<Data::ControllerConnectionProfileConfiguration>
WorkbenchController::controllerConnectionProfileConfiguration(
    const Data::ControllerConnectionScope &scope, const Data::NodeId &profileId) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider || profileId.isNull())
        return std::nullopt;
    return provider->connectionProfileConfiguration(scope, profileId);
}

Data::ControllerConnectionSnapshot WorkbenchController::controllerConnectionSnapshot(
    const Data::ControllerConnectionScope &scope) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (provider)
        return provider->connectionSnapshot();
    Data::ControllerConnectionSnapshot result;
    result.scope = scope;
    return result;
}

bool WorkbenchController::controllerConnectionProjectIsOpen(
    const Data::ControllerConnectionScope &scope) const
{
    if (!m_projectService || scope.projectId.isNull())
        return false;
    return m_projectService->project(scope.projectId).has_value();
}

Utils::Result<> WorkbenchController::selectControllerConnectionProvider(
    const Data::ControllerConnectionScope &scope, Utils::Id providerId)
{
    if (m_shuttingDown)
        return Utils::ResultError(Tr::tr("The controller connection workflow is shutting down."));
    if (controllerConnectionSelectionLocked(scope)) {
        return Utils::ResultError(
            Tr::tr("Disconnect the current controller session before changing its adapter."));
    }

    if (providerId.isValid()) {
        const QList<Core::ControllerConnectionProvider *> providers
            = controllerConnectionProviders();
        const auto provider
            = std::find_if(providers.cbegin(), providers.cend(), [providerId](const auto *candidate) {
                  return candidate->id() == providerId;
              });
        if (provider == providers.cend())
            return Utils::ResultError(Tr::tr("The selected controller adapter is unavailable."));
    }

    ControllerConnectionSelection *selection = mutableControllerConnectionSelection(scope);
    const bool providerExplicitlySelected = providerId.isValid();
    if (selection && selection->providerId == providerId
        && selection->providerExplicitlySelected == providerExplicitlySelected) {
        return Utils::ResultOk;
    }
    if (!selection) {
        ControllerConnectionSelection unselected;
        unselected.scope = scope;
        m_controllerConnectionSelections.append(unselected);
        selection = &m_controllerConnectionSelections.last();
    }
    selection->providerId = providerId;
    selection->profileId = {};
    selection->providerExplicitlySelected = providerExplicitlySelected;
    selection->profileExplicitlySelected = false;
    emit controllerConnectionChanged();
    return Utils::ResultOk;
}

Utils::Result<> WorkbenchController::selectControllerConnectionProfile(
    const Data::ControllerConnectionScope &scope, const Data::NodeId &profileId)
{
    if (m_shuttingDown)
        return Utils::ResultError(Tr::tr("The controller connection workflow is shutting down."));
    if (controllerConnectionSelectionLocked(scope)) {
        return Utils::ResultError(
            Tr::tr("Disconnect the current controller session before changing its profile."));
    }
    ControllerConnectionSelection *selection = mutableControllerConnectionSelection(scope);
    if (!selection || !selection->providerExplicitlySelected || !selection->providerId.isValid()) {
        return Utils::ResultError(Tr::tr("Select a controller adapter before selecting a profile."));
    }
    if (!profileId.isNull()) {
        const QList<Data::ControllerConnectionProfile> profiles = controllerConnectionProfiles(
            scope);
        const auto profile
            = std::find_if(profiles.cbegin(), profiles.cend(), [&profileId](const auto &candidate) {
                  return candidate.id == profileId;
              });
        if (profile == profiles.cend())
            return Utils::ResultError(Tr::tr("The selected connection profile is unavailable."));
    }
    const bool profileExplicitlySelected = !profileId.isNull();
    if (selection->profileId == profileId
        && selection->profileExplicitlySelected == profileExplicitlySelected) {
        return Utils::ResultOk;
    }
    selection->profileId = profileId;
    selection->profileExplicitlySelected = profileExplicitlySelected;
    emit controllerConnectionChanged();
    return Utils::ResultOk;
}

Utils::Result<> WorkbenchController::setControllerConnectionProfileEndpoint(
    const Data::ControllerConnectionScope &scope,
    const Data::NodeId &profileId,
    const QString &endpoint)
{
    if (m_shuttingDown)
        return Utils::ResultError(Tr::tr("The controller connection workflow is shutting down."));
    if (!controllerConnectionScopeIsValid(scope))
        return Utils::ResultError(Tr::tr("Select a valid EtherCAT Master before editing its endpoint."));
    if (controllerConnectionSelectionLocked(scope)) {
        return Utils::ResultError(
            Tr::tr("Disconnect the current controller session before changing its endpoint."));
    }

    const ControllerConnectionSelection selection = controllerConnectionSelection(scope);
    if (!selection.providerExplicitlySelected || !selection.profileExplicitlySelected
        || selection.profileId != profileId) {
        return Utils::ResultError(
            Tr::tr("Select the connection profile before changing its endpoint."));
    }
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return Utils::ResultError(Tr::tr("The selected controller adapter is unavailable."));
    return provider->setConnectionProfileEndpoint(scope, profileId, endpoint);
}

bool WorkbenchController::controllerConnectionSelectionLocked(
    const Data::ControllerConnectionScope &scope) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return false;
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    return snapshot.scope == scope && connectionStateLocksSelection(snapshot.state);
}

bool WorkbenchController::canConnectController(
    const Data::ControllerConnectionScope &scope) const
{
    if (!controllerConnectionScopeIsValid(scope))
        return false;
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider || !provider->isAvailable())
        return false;
    const ControllerConnectionSelection selection = controllerConnectionSelection(scope);
    if (!selection.providerExplicitlySelected || !selection.profileExplicitlySelected)
        return false;
    const QList<Data::ControllerConnectionProfile> profiles = provider->connectionProfiles(scope);
    const auto profile
        = std::find_if(profiles.cbegin(), profiles.cend(), [&selection](const auto &candidate) {
              return candidate.id == selection.profileId;
          });
    if (profile == profiles.cend() || !connectionProfileUsable(*profile))
        return false;
    const Data::ControllerConnectionState state = provider->connectionSnapshot().state;
    return state == Data::ControllerConnectionState::Disconnected
           || state == Data::ControllerConnectionState::Failed;
}

bool WorkbenchController::canConnectSelectedController() const
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    return scope && canConnectController(*scope);
}

bool WorkbenchController::canDisconnectSelectedController() const
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    if (!scope)
        return false;
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(*scope);
    if (!provider)
        return false;
    if (m_controllerStartupStates.contains(provider))
        return false;
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    return (snapshot.scope == *scope || !controllerConnectionProjectIsOpen(snapshot.scope))
           && snapshot.state != Data::ControllerConnectionState::Disconnected
           && snapshot.state != Data::ControllerConnectionState::Disconnecting
           && snapshot.controlProgress.state != Data::ControllerControlState::Pending
           && !controllerControlLeaseOwnershipUnverified(snapshot);
}

bool WorkbenchController::canRefreshSelectedController() const
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    if (!scope)
        return false;
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(*scope);
    if (!provider || !provider->isAvailable())
        return false;
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    return snapshot.scope == *scope
           && (snapshot.state == Data::ControllerConnectionState::Connected
               || snapshot.state == Data::ControllerConnectionState::Degraded)
           && snapshot.controlProgress.state != Data::ControllerControlState::Pending;
}

QString WorkbenchController::currentBusApplyUnavailableReason() const
{
    if (m_shuttingDown)
        return Tr::tr("The EtherCAT Workbench is shutting down.");
    if (!m_projectService || !m_deviceRepository)
        return Tr::tr("The EtherCAT project or ESI repository service is unavailable.");
    if (m_deviceRepository->isIndexing())
        return Tr::tr("Wait for the ESI repository update to finish.");

    const std::optional<Data::ControllerConnectionScope> scope = quickControllerControlScope();
    if (!scope)
        return quickControllerControlScopeUnavailableReason();
    const ControllerConnectionSelection selection = controllerConnectionSelection(*scope);
    if (!selection.providerExplicitlySelected || !selection.providerId.isValid())
        return Tr::tr("Select a controller adapter for this EtherCAT Master.");
    if (!selection.profileExplicitlySelected || selection.profileId.isNull())
        return Tr::tr("Select a controller connection profile for this EtherCAT Master.");

    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(*scope);
    if (!provider || !provider->isAvailable())
        return Tr::tr("The selected controller adapter is unavailable.");
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (snapshot.scope != *scope || snapshot.profileId != selection.profileId
        || (snapshot.state != Data::ControllerConnectionState::Connected
            && snapshot.state != Data::ControllerConnectionState::Degraded)) {
        return Tr::tr("Connect and scan this EtherCAT Master before applying the current bus.");
    }
    if (snapshot.mock)
        return Tr::tr("A Mock topology cannot configure a production EtherCAT project.");
    if (snapshot.controlProgress.state == Data::ControllerControlState::Pending)
        return Tr::tr("Wait for the current controller operation to finish.");
    if (!snapshot.topology)
        return Tr::tr("Scan the EtherCAT bus before applying it to the project.");

    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(scope->projectId);
    if (!project)
        return Tr::tr("The selected EtherCAT project is no longer available.");
    const Utils::Result<CurrentBusApplyPlan> plan
        = currentBusApplyPlan(*scope, *project, *snapshot.topology, m_deviceRepository);
    if (!plan)
        return plan.error();
    if (plan->candidateSlaves == plan->currentSlaves)
        return Tr::tr("The current bus already matches the offline project configuration.");
    return {};
}

bool WorkbenchController::canApplyCurrentBusToProject() const
{
    return currentBusApplyUnavailableReason().isEmpty();
}

Utils::Result<> WorkbenchController::applyCurrentBusToProject()
{
    const QString unavailableReason = currentBusApplyUnavailableReason();
    if (!unavailableReason.isEmpty())
        return Utils::ResultError(unavailableReason);

    const std::optional<Data::ControllerConnectionScope> scope = quickControllerControlScope();
    QTC_ASSERT(
        scope,
        return Utils::ResultError(
            Tr::tr("The EtherCAT project no longer has an available Master.")));
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(*scope);
    QTC_ASSERT(
        provider,
        return Utils::ResultError(Tr::tr("The selected controller adapter is unavailable.")));
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(scope->projectId);
    QTC_ASSERT(
        snapshot.topology && project,
        return Utils::ResultError(
            Tr::tr("The current bus or EtherCAT project is no longer available.")));

    const Utils::Result<CurrentBusApplyPlan> plan
        = currentBusApplyPlan(*scope, *project, *snapshot.topology, m_deviceRepository);
    if (!plan)
        return Utils::ResultError(plan.error());
    const Utils::Result<> applied = m_projectService->replaceOfflineSlaves(
        scope->projectId, scope->masterId, plan->candidateSlaves);
    if (!applied)
        return applied;

    QString message = Tr::tr(
                          "Applied the current bus to the offline project: %1 device(s), %2 ESI "
                          "match(es), %3 unknown device(s), %4 existing configuration(s) "
                          "preserved, and %5 configuration(s) removed or replaced.")
                          .arg(plan->candidateSlaves.size())
                          .arg(plan->esiMatches)
                          .arg(plan->unknownDevices)
                          .arg(plan->preservedConfigurations)
                          .arg(plan->removedConfigurations);
    if (plan->ambiguousEsiMatches) {
        message += Tr::tr(" %n device(s) have ambiguous ESI matches.",
                          nullptr,
                          plan->ambiguousEsiMatches);
    }
    if (plan->unsupportedEsiMatches) {
        message += Tr::tr(" %n matching ESI device(s) contain unsupported structures.",
                          nullptr,
                          plan->unsupportedEsiMatches);
    }
    if (plan->unknownDevices) {
        message += Tr::tr(
            " Import matching ESI XML files, then apply the current bus again to enable detailed "
            "Process Data, Startup, and Distributed Clocks configuration.");
    }
    writeControllerOutput(
        message,
        plan->unknownDevices ? ControllerOutputLevel::Warning
                             : ControllerOutputLevel::Information);
    return Utils::ResultOk;
}

QString WorkbenchController::controllerControlCommonUnavailableReason(
    const Data::ControllerConnectionScope &scope,
    Data::ControllerControlCommand command,
    Data::ControllerConnectionSnapshot *snapshot) const
{
    if (snapshot)
        *snapshot = {};
    if (m_shuttingDown)
        return Tr::tr("The controller connection workflow is shutting down.");
    if (!controllerConnectionScopeIsValid(scope))
        return Tr::tr("The requested EtherCAT Master is not available in an open project.");

    const ControllerConnectionSelection selection = controllerConnectionSelection(scope);
    if (!selection.providerExplicitlySelected || !selection.providerId.isValid())
        return Tr::tr("Select a controller adapter for the active EtherCAT Master.");
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return Tr::tr("The selected controller adapter is unavailable.");
    if (!provider->isAvailable())
        return Tr::tr("The selected controller adapter is not available.");
    if (m_controllerStartupStates.contains(provider))
        return Tr::tr("Automatic controller startup is in progress.");
    if (!selection.profileExplicitlySelected || selection.profileId.isNull())
        return Tr::tr("Select a controller connection profile for the active EtherCAT Master.");
    if (command != Data::ControllerControlCommand::None
        && !provider->supportsControlCommand(command)) {
        return Tr::tr("The connected controller does not support this control operation.");
    }

    const Data::ControllerConnectionSnapshot current = provider->connectionSnapshot();
    if (snapshot)
        *snapshot = current;
    if (current.state != Data::ControllerConnectionState::Connected
        && current.state != Data::ControllerConnectionState::Degraded) {
        return Tr::tr("Connect the controller before using controller run controls.");
    }
    if (current.scope != scope)
        return Tr::tr("The connected controller session belongs to a different EtherCAT Master.");
    if (current.profileId != selection.profileId)
        return Tr::tr("The connected controller session uses a different connection profile.");
    if (current.mock)
        return Tr::tr("Controller run controls are unavailable for a Mock connection.");
    if (current.readOnly)
        return Tr::tr("The controller connection is read-only.");
    if (current.controlProgress.state == Data::ControllerControlState::Pending)
        return Tr::tr("Wait for the current controller control operation to finish.");
    return {};
}

QString WorkbenchController::controllerControlUnavailableReason(
    const Data::ControllerConnectionScope &scope, Data::ControllerControlCommand command) const
{
    Data::ControllerConnectionSnapshot snapshot;
    if (const QString reason = controllerControlCommonUnavailableReason(scope, command, &snapshot);
        !reason.isEmpty()) {
        return reason;
    }
    return controllerControlStateUnavailableReason(snapshot, command);
}

bool WorkbenchController::canExecuteControllerControl(
    const Data::ControllerConnectionScope &scope,
    Data::ControllerControlCommand command) const
{
    return command != Data::ControllerControlCommand::None
           && controllerControlUnavailableReason(scope, command).isEmpty();
}

bool WorkbenchController::canExecuteSelectedControllerControl(
    Data::ControllerControlCommand command) const
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    return scope && canExecuteControllerControl(*scope, command);
}

std::optional<Data::ControllerControlCommand> WorkbenchController::quickControllerControlCommand(
    const Data::ControllerConnectionScope &scope, ControllerQuickControlAction action) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return std::nullopt;
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (snapshot.scope != scope || !snapshot.controllerState)
        return std::nullopt;

    using Command = Data::ControllerControlCommand;
    using ServiceState = Data::ControllerServiceState;
    const ServiceState serviceState = snapshot.controllerState->serviceState;
    switch (action) {
    case ControllerQuickControlAction::Run:
        if (serviceState == ServiceState::Shutdown)
            return Command::EnterConfigurationMode;
        if (serviceState == ServiceState::OperationalSafe)
            return Command::Start;
        if (serviceState == ServiceState::Paused)
            return Command::Resume;
        break;
    case ControllerQuickControlAction::Debug:
        if (serviceState == ServiceState::Running)
            return Command::Pause;
        if (serviceState == ServiceState::Paused)
            return Command::Resume;
        break;
    case ControllerQuickControlAction::Stop:
        if (serviceState == ServiceState::Running || serviceState == ServiceState::Paused)
            return Command::ControlledStop;
        break;
    }
    return std::nullopt;
}

QString WorkbenchController::quickControllerControlUnavailableReason(
    const Data::ControllerConnectionScope &scope, ControllerQuickControlAction action) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (provider && m_controllerStartupStates.contains(provider))
        return Tr::tr("Automatic controller startup is in progress.");

    Data::ControllerConnectionSnapshot snapshot;
    if (const QString reason = controllerControlCommonUnavailableReason(
            scope, Data::ControllerControlCommand::None, &snapshot);
        !reason.isEmpty()) {
        return reason;
    }

    const std::optional<Data::ControllerControlCommand> command
        = quickControllerControlCommand(scope, action);
    if (command == Data::ControllerControlCommand::EnterConfigurationMode)
        return controllerStartupUnavailableReason(provider, snapshot);
    if (command)
        return controllerControlUnavailableReason(scope, *command);
    if (!snapshot.controllerState)
        return Tr::tr("The controller state is unavailable.");

    switch (action) {
    case ControllerQuickControlAction::Run:
        return Tr::tr("Run is available while the controller is in Shutdown, OP_SAFE, or Paused.");
    case ControllerQuickControlAction::Debug:
        return Tr::tr("Pause or Resume is available only while the controller is Running or Paused.");
    case ControllerQuickControlAction::Stop:
        return Tr::tr("Controlled Stop is available only while the controller is Running or Paused.");
    }
    return Tr::tr("The controller service state does not allow this operation.");
}

bool WorkbenchController::controllerStartupInProgress(
    const Data::ControllerConnectionScope &scope) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return false;
    const auto state = m_controllerStartupStates.constFind(provider);
    return state != m_controllerStartupStates.cend() && state->scope == scope;
}

Utils::Result<> WorkbenchController::executeQuickControllerControl(
    const Data::ControllerConnectionScope &scope, ControllerQuickControlAction action)
{
    const QString unavailableReason = quickControllerControlUnavailableReason(scope, action);
    if (!unavailableReason.isEmpty())
        return Utils::ResultError(unavailableReason);

    const std::optional<Data::ControllerControlCommand> command
        = quickControllerControlCommand(scope, action);
    if (!command)
        return Utils::ResultError(
            Tr::tr("The controller service state does not allow this operation."));

    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return Utils::ResultError(Tr::tr("The selected controller adapter is unavailable."));
    if (*command == Data::ControllerControlCommand::EnterConfigurationMode)
        return beginControllerStartup(provider, provider->connectionSnapshot());

    Data::ControllerControlRequest request;
    request.command = *command;
    return executeControllerControl(scope, request);
}

Utils::Result<> WorkbenchController::connectController(
    const Data::ControllerConnectionScope &scope)
{
    if (!controllerConnectionScopeIsValid(scope))
        return Utils::ResultError(Tr::tr("Select a valid EtherCAT Master before connecting."));
    if (!canConnectController(scope))
        return Utils::ResultError(Tr::tr("The selected controller connection is not ready."));
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    QTC_ASSERT(provider, return Utils::ResultError(Tr::tr("The controller adapter is unavailable.")));
    return provider->connectToController({scope, controllerConnectionSelection(scope).profileId});
}

Utils::Result<> WorkbenchController::connectSelectedController()
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    if (!scope)
        return Utils::ResultError(Tr::tr("Select a valid EtherCAT Master before connecting."));
    return connectController(*scope);
}

Utils::Result<> WorkbenchController::disconnectSelectedController()
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    if (!scope)
        return Utils::ResultError(Tr::tr("Select the connected EtherCAT Master first."));
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(*scope);
    if (provider
        && controllerControlLeaseOwnershipUnverified(provider->connectionSnapshot())) {
        return Utils::ResultError(
            Tr::tr("The controller control lease ownership is unverified for this session."));
    }
    if (!canDisconnectSelectedController()) {
        return Utils::ResultError(
            Tr::tr("The selected EtherCAT Master does not own an active connection."));
    }
    if (!provider)
        return Utils::ResultError(Tr::tr("The selected controller adapter is unavailable."));
    return provider->disconnectFromController();
}

Utils::Result<> WorkbenchController::refreshSelectedController()
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    if (!scope)
        return Utils::ResultError(Tr::tr("Select the connected EtherCAT Master first."));
    if (!canRefreshSelectedController())
        return Utils::ResultError(Tr::tr("The controller connection is not ready to refresh."));
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(*scope);
    QTC_ASSERT(provider, return Utils::ResultError(Tr::tr("The controller adapter is unavailable.")));
    return provider->refreshController();
}

Utils::Result<> WorkbenchController::executeControllerControl(
    const Data::ControllerConnectionScope &scope,
    const Data::ControllerControlRequest &request)
{
    if (request.command == Data::ControllerControlCommand::None)
        return Utils::ResultError(Tr::tr("Select a controller control operation."));
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    const QString unavailableReason = controllerControlUnavailableReason(scope, request.command);
    if (!unavailableReason.isEmpty())
        return Utils::ResultError(unavailableReason);
    QTC_ASSERT(provider, return Utils::ResultError(Tr::tr("The controller adapter is unavailable.")));
    return provider->executeControlCommand(request);
}

Utils::Result<> WorkbenchController::executeSelectedControllerControl(
    const Data::ControllerControlRequest &request)
{
    const std::optional<Data::ControllerConnectionScope> scope = selectedControllerConnectionScope();
    if (!scope)
        return Utils::ResultError(Tr::tr("Select the connected EtherCAT Master first."));
    return executeControllerControl(*scope, request);
}

QString WorkbenchController::packageDeploymentUnavailableReason(
    const Data::ControllerConnectionScope &scope) const
{
    if (m_shuttingDown)
        return Tr::tr("The controller connection workflow is shutting down.");
    if (!controllerConnectionScopeIsValid(scope))
        return Tr::tr("The requested EtherCAT Master is not available in an open project.");

    const ControllerConnectionSelection selection = controllerConnectionSelection(scope);
    if (!selection.providerExplicitlySelected || !selection.providerId.isValid())
        return Tr::tr("Select a controller adapter for the active EtherCAT Master.");
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider)
        return Tr::tr("The selected controller adapter is unavailable.");
    if (!provider->isAvailable())
        return Tr::tr("The selected controller adapter is not available.");
    if (!selection.profileExplicitlySelected || selection.profileId.isNull()) {
        return Tr::tr(
            "Select a controller connection profile for the active EtherCAT Master.");
    }
    if (!provider->supportsPackageDeployment())
        return Tr::tr("The connected controller adapter does not support package deployment.");

    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (snapshot.state != Data::ControllerConnectionState::Connected
        && snapshot.state != Data::ControllerConnectionState::Degraded) {
        return Tr::tr("Connect the controller before deploying a package.");
    }
    if (snapshot.scope != scope)
        return Tr::tr("The connected controller session belongs to a different EtherCAT Master.");
    if (snapshot.profileId != selection.profileId) {
        return Tr::tr(
            "The connected controller session uses a different connection profile.");
    }
    if (snapshot.mock)
        return Tr::tr("Controller package deployment is unavailable for a Mock connection.");
    if (snapshot.readOnly)
        return Tr::tr("The controller connection is read-only.");
    if (!snapshot.session || !snapshot.session->sessionId || !snapshot.session->bootId)
        return Tr::tr("The controller session identity is not available.");
    if (!snapshot.session->ownsControlLease)
        return Tr::tr("Acquire the control lease before deploying a package.");
    if (!snapshot.capability || !snapshot.capability->transactionalBulk) {
        return Tr::tr(
            "The controller does not support transactional package upload.");
    }
    if (snapshot.controlProgress.state == Data::ControllerControlState::Pending)
        return Tr::tr("Wait for the current controller control operation to finish.");

    using DeploymentState = Data::ControllerPackageDeploymentState;
    switch (snapshot.packageDeploymentProgress.state) {
    case DeploymentState::Uploading:
    case DeploymentState::Committing:
    case DeploymentState::Validating:
    case DeploymentState::Activating:
    case DeploymentState::RollingBack:
    case DeploymentState::Canceling:
        return Tr::tr("Wait for the current package deployment to finish.");
    case DeploymentState::OutcomeUnknown:
        return Tr::tr(
            "Reconnect and verify the authoritative package state before starting another "
            "deployment.");
    case DeploymentState::Idle:
    case DeploymentState::Succeeded:
    case DeploymentState::Canceled:
    case DeploymentState::Failed:
        break;
    }

    if (!snapshot.controllerState || !snapshot.controllerState->ready
        || snapshot.controllerState->serviceState != Data::ControllerServiceState::Shutdown) {
        return Tr::tr(
            "Enter configuration mode before deploying a controller package.");
    }
    return {};
}

bool WorkbenchController::canDeployControllerPackage(
    const Data::ControllerConnectionScope &scope) const
{
    return packageDeploymentUnavailableReason(scope).isEmpty();
}

bool WorkbenchController::canCancelControllerPackageDeployment(
    const Data::ControllerConnectionScope &scope) const
{
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    if (!provider || !provider->isAvailable() || !provider->supportsPackageDeployment())
        return false;
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (snapshot.scope != scope)
        return false;
    using DeploymentState = Data::ControllerPackageDeploymentState;
    return snapshot.packageDeploymentProgress.state == DeploymentState::Uploading
           || snapshot.packageDeploymentProgress.state == DeploymentState::Committing;
}

Utils::Result<> WorkbenchController::deployControllerPackage(
    const Data::ControllerConnectionScope &scope,
    const Data::ControllerPackageDeploymentRequest &request)
{
    const QString unavailableReason = packageDeploymentUnavailableReason(scope);
    if (!unavailableReason.isEmpty())
        return Utils::ResultError(unavailableReason);
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    QTC_ASSERT(provider, return Utils::ResultError(Tr::tr("The controller adapter is unavailable.")));
    return provider->deployPackage(request);
}

Utils::Result<> WorkbenchController::cancelControllerPackageDeployment(
    const Data::ControllerConnectionScope &scope, const QString &operationId)
{
    if (!canCancelControllerPackageDeployment(scope)) {
        return Utils::ResultError(
            Tr::tr("Package deployment can only be canceled during upload or commit."));
    }
    Core::ControllerConnectionProvider *provider = controllerConnectionProvider(scope);
    QTC_ASSERT(provider, return Utils::ResultError(Tr::tr("The controller adapter is unavailable.")));
    return provider->cancelPackageDeployment(operationId);
}

void WorkbenchController::writeControllerOutput(
    const QString &message, ControllerOutputLevel level)
{
    const QString normalized = message.trimmed();
    if (!normalized.isEmpty())
        emit controllerOutputRequested(normalized, level);
}

bool WorkbenchController::scanAvailable() const
{
    return m_scanProvider.isAvailable();
}

bool WorkbenchController::diagnosticsAvailable() const
{
    return m_diagnosticsProvider.isAvailable();
}

bool WorkbenchController::canInsertDeviceOnSelectedMaster() const
{
    return !m_shuttingDown && !selectedOfflineMasterId().isNull();
}

bool WorkbenchController::canAddSelectedDeviceToMaster() const
{
    if (m_shuttingDown || !m_selectionService || !m_deviceRepository)
        return false;
    const Core::PropertyPageContext context = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (context.nodeKind != Core::WorkbenchNodeKind::Device)
        return false;
    const std::optional<Data::DeviceDescription> device = m_deviceRepository->device(context.nodeId);
    return device && device->summary.supported && activeOfflineMasterTarget().has_value();
}

bool WorkbenchController::canRemoveSelectedOfflineSlave() const
{
    return !m_shuttingDown
           && selectedSlaveContext(m_treeModel, m_selectionService, m_projectService).has_value();
}

bool WorkbenchController::canMoveSelectedOfflineSlaveUp() const
{
    if (m_shuttingDown)
        return false;
    const std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    return selected && selected->index > 0;
}

bool WorkbenchController::canMoveSelectedOfflineSlaveDown() const
{
    if (m_shuttingDown)
        return false;
    const std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    return selected && selected->index + 1 < selected->slaves.size();
}

bool WorkbenchController::canActivateSelectedProject() const
{
    if (m_shuttingDown)
        return false;
    const std::optional<Data::ProjectSnapshot> project
        = selectedProject(m_treeModel, m_selectionService, m_projectService);
    return project && project->valid && project->id != m_projectService->activeProjectId();
}

bool WorkbenchController::canCopyNodeId(const Data::NodeId &nodeId) const
{
    if (m_shuttingDown || nodeId.isNull() || !m_projectService)
        return false;
    const Core::PropertyPageContext context = m_treeModel.contextForNodeId(nodeId);
    if (context.nodeKind == Core::WorkbenchNodeKind::None
        || context.nodeKind == Core::WorkbenchNodeKind::Placeholder) {
        return false;
    }
    if (context.projectId.isNull())
        return true;
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(
        context.projectId);
    return project && project->valid;
}

std::optional<OfflineMasterTarget> WorkbenchController::activeOfflineMasterTarget() const
{
    if (m_shuttingDown)
        return std::nullopt;
    const std::optional<ActiveMasterContext> target = activeMasterContext(m_projectService);
    if (!target)
        return std::nullopt;
    const auto master = std::find_if(
        target->project.nodes.cbegin(),
        target->project.nodes.cend(),
        [&target](const Data::ProjectNodeSnapshot &node) {
            return node.kind == Data::ProjectNodeKind::Master && node.id == target->masterId;
        });
    QTC_ASSERT(master != target->project.nodes.cend(), return std::nullopt);
    return OfflineMasterTarget{
        target->project.id, target->masterId, target->project.name, master->name};
}

Utils::Result<> WorkbenchController::addSelectedDeviceToMaster(
    const OfflineMasterTarget &target)
{
    if (m_shuttingDown || !m_selectionService || !m_deviceRepository || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    const std::optional<ActiveMasterContext> current = activeMasterContext(
        m_projectService, target.masterId);
    if (!current || current->project.id != target.projectId) {
        return Utils::ResultError(Tr::tr(
            "The displayed active offline master is no longer current. Review the updated "
            "target and try again."));
    }
    const Core::PropertyPageContext context = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (context.nodeKind != Core::WorkbenchNodeKind::Device)
        return Utils::ResultError(Tr::tr("Select an ESI device before adding it."));
    return addDeviceToMaster(context.nodeId, target.masterId);
}

Data::NodeId WorkbenchController::selectedOfflineMasterId() const
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return {};
    const Core::PropertyPageContext context = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (context.nodeKind != Core::WorkbenchNodeKind::Master)
        return {};
    const std::optional<ActiveMasterContext> target = activeMasterContext(
        m_projectService, context.nodeId);
    if (!target || target->project.id != context.projectId)
        return {};
    return target->masterId;
}

Utils::Result<> WorkbenchController::addDeviceToMaster(
    const Data::NodeId &deviceId, const Data::NodeId &masterId)
{
    if (m_shuttingDown || !m_selectionService || !m_deviceRepository || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    if (masterId.isNull())
        return Utils::ResultError(Tr::tr("The selected offline EtherCAT Master is unavailable."));
    const std::optional<Data::DeviceDescription> device = m_deviceRepository->device(deviceId);
    if (!device)
        return Utils::ResultError(Tr::tr("The selected ESI device is no longer available."));
    if (!device->summary.supported) {
        return Utils::ResultError(
            Tr::tr("The selected ESI device has unsupported structures and cannot be added."));
    }
    std::optional<ActiveMasterContext> target = activeMasterContext(m_projectService, masterId);
    if (!target)
        return Utils::ResultError(Tr::tr("The selected offline EtherCAT Master is unavailable."));

    const Data::NodeId slaveId = Data::NodeId::create();
    const QString requestedName = device->summary.name.isEmpty() ? device->summary.typeName
                                                                 : device->summary.name;
    int nextPosition = 0;
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(target->slaves))
        nextPosition = qMax(nextPosition, slave.position + 1);
    target->slaves.append(offlineSlaveFromDevice(
        *device,
        slaveId,
        target->masterId,
        nextPosition,
        uniqueSlaveName(requestedName, target->slaves)));
    const Utils::Result<> result
        = m_projectService
              ->replaceOfflineSlaves(target->project.id, target->masterId, target->slaves);
    if (!result)
        return result;
    m_selectionService->setCurrentNodeId(slaveId);
    return Utils::ResultOk;
}

std::optional<OfflineSlaveRemovalCandidate>
WorkbenchController::selectedOfflineSlaveRemovalCandidate() const
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return std::nullopt;
    const std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    if (!selected)
        return std::nullopt;
    const Data::OfflineSlaveConfiguration &slave = selected->slaves.at(selected->index);
    return OfflineSlaveRemovalCandidate{
        selected->project.id, selected->masterId, slave.id, slave.name, slave.position, slave};
}

Utils::Result<> WorkbenchController::removeOfflineSlave(
    const OfflineSlaveRemovalCandidate &candidate)
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    if (m_selectionService->currentNodeId() != candidate.slaveId) {
        return Utils::ResultError(
            Tr::tr("The selected offline slave changed while confirmation was open; "
                   "nothing was removed."));
    }
    std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    if (!selected || selected->project.id != candidate.projectId
        || selected->masterId != candidate.masterId || selected->slaveId != candidate.slaveId) {
        return Utils::ResultError(
            Tr::tr("The offline slave is no longer available; nothing was removed."));
    }
    if (selected->slaves.at(selected->index) != candidate.expectedSlave) {
        return Utils::ResultError(Tr::tr(
            "The offline slave configuration changed while confirmation was open; nothing was "
            "removed. Confirm the current configuration again."));
    }

    selected->slaves.removeAt(selected->index);
    normalizePositions(&selected->slaves);
    const Data::NodeId nextSelection
        = selected->slaves.isEmpty()
              ? selected->masterId
              : selected->slaves.at(qMin(selected->index, selected->slaves.size() - 1)).id;
    const Utils::Result<> result
        = m_projectService
              ->replaceOfflineSlaves(selected->project.id, selected->masterId, selected->slaves);
    if (!result)
        return result;
    m_selectionService->setCurrentNodeId(nextSelection);
    return Utils::ResultOk;
}

Utils::Result<> WorkbenchController::moveSelectedOfflineSlaveUp()
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    if (!selected || selected->index <= 0)
        return Utils::ResultError(Tr::tr("The selected offline slave cannot move up."));
    selected->slaves.swapItemsAt(selected->index, selected->index - 1);
    normalizePositions(&selected->slaves);
    return m_projectService
        ->replaceOfflineSlaves(selected->project.id, selected->masterId, selected->slaves);
}

Utils::Result<> WorkbenchController::moveSelectedOfflineSlaveDown()
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    if (!selected || selected->index + 1 >= selected->slaves.size())
        return Utils::ResultError(Tr::tr("The selected offline slave cannot move down."));
    selected->slaves.swapItemsAt(selected->index, selected->index + 1);
    normalizePositions(&selected->slaves);
    return m_projectService
        ->replaceOfflineSlaves(selected->project.id, selected->masterId, selected->slaves);
}

Utils::Result<> WorkbenchController::activateSelectedProject()
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline project service is unavailable."));
    const std::optional<Data::ProjectSnapshot> project
        = selectedProject(m_treeModel, m_selectionService, m_projectService);
    if (!project)
        return Utils::ResultError(Tr::tr("Select an open EtherCAT project first."));
    if (!project->valid) {
        return Utils::ResultError(
            Tr::tr("The selected EtherCAT project is invalid and cannot be activated."));
    }
    if (project->id == m_projectService->activeProjectId())
        return Utils::ResultError(Tr::tr("The selected EtherCAT project is already active."));
    return m_projectService->activateProject(project->id);
}

Utils::Result<> WorkbenchController::renameProject(
    const Data::NodeId &projectId, const QString &name)
{
    if (m_shuttingDown || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline project service is unavailable."));
    return m_projectService->renameProject(projectId, name);
}

Utils::Result<> WorkbenchController::renameOfflineSlave(
    const Data::NodeId &projectId, const Data::NodeId &slaveId, const QString &name)
{
    if (m_shuttingDown || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return Utils::ResultError(Tr::tr("Offline slave name cannot be empty."));
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(projectId);
    if (!project || !project->valid)
        return Utils::ResultError(Tr::tr("The EtherCAT project is not available."));
    const auto selected = std::find_if(
        project->slaves.cbegin(), project->slaves.cend(), [&slaveId](const auto &slave) {
            return slave.id == slaveId;
        });
    if (selected == project->slaves.cend())
        return Utils::ResultError(Tr::tr("The offline slave is not available."));
    if (selected->name == trimmedName)
        return Utils::ResultOk;

    QList<Data::OfflineSlaveConfiguration> slaves = slavesForMaster(*project, selected->masterId);
    const auto editable = std::find_if(slaves.begin(), slaves.end(), [&slaveId](const auto &slave) {
        return slave.id == slaveId;
    });
    QTC_ASSERT(
        editable != slaves.end(),
        return Utils::ResultError(Tr::tr("The offline slave is not part of its EtherCAT master.")));
    editable->name = trimmedName;
    return m_projectService->replaceOfflineSlaves(projectId, selected->masterId, slaves);
}

Utils::Result<> WorkbenchController::renameStructuralNode(
    const Data::NodeId &projectId, const Data::NodeId &nodeId, const QString &name)
{
    if (m_shuttingDown || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    return m_projectService->renameStructuralNode(projectId, nodeId, name);
}

Utils::Result<> WorkbenchController::setMasterConfiguration(
    const Data::NodeId &projectId,
    const Data::NodeId &masterId,
    const Data::MasterConfiguration &configuration)
{
    if (m_shuttingDown || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    return m_projectService->setMasterConfiguration(projectId, masterId, configuration);
}

Utils::Result<> WorkbenchController::setOfflineSlaveAlias(
    const Data::NodeId &projectId, const Data::NodeId &slaveId, quint16 alias)
{
    if (m_shuttingDown || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    const std::optional<Data::ProjectSnapshot> project = m_projectService->project(projectId);
    if (!project || !project->valid)
        return Utils::ResultError(Tr::tr("The EtherCAT project is not available."));
    const auto selected = std::find_if(
        project->slaves.cbegin(), project->slaves.cend(), [&slaveId](const auto &slave) {
            return slave.id == slaveId;
        });
    if (selected == project->slaves.cend())
        return Utils::ResultError(Tr::tr("The offline slave is not available."));
    if (selected->alias == alias)
        return Utils::ResultOk;

    QList<Data::OfflineSlaveConfiguration> slaves = slavesForMaster(*project, selected->masterId);
    const auto editable = std::find_if(slaves.begin(), slaves.end(), [&slaveId](const auto &slave) {
        return slave.id == slaveId;
    });
    QTC_ASSERT(
        editable != slaves.end(),
        return Utils::ResultError(Tr::tr("The offline slave is not part of its EtherCAT master.")));
    editable->alias = alias;
    return m_projectService->replaceOfflineSlaves(projectId, selected->masterId, slaves);
}

void WorkbenchController::refresh()
{
    if (m_shuttingDown)
        return;
    refreshProjects();
    refreshDevices();
    refreshControllerConnectionPresentation();
}

void WorkbenchController::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_controllerAutoAcquireStates.clear();
    m_controllerStartupStates.clear();
    m_controllerCleanupStates.clear();
    m_treeModel.setDeviceDropHandler({});
    m_treeModel.clear();
}

void WorkbenchController::refreshProjects()
{
    if (m_shuttingDown || !m_projectService)
        return;
    const Data::NodeId selectedId = m_selectionService ? m_selectionService->currentNodeId()
                                                       : Data::NodeId();
    m_treeModel.setProjects(m_projectService->projects());
    m_treeModel.setActiveProjectId(m_projectService->activeProjectId());
    const std::optional<ActiveMasterContext> active = activeMasterContext(m_projectService);
    m_treeModel.setDropTargetMasterId(active ? active->masterId : Data::NodeId());
    if (m_selectionService && !selectedId.isNull()
        && !m_treeModel.indexForNodeId(selectedId).isValid()) {
        m_selectionService->clear();
    }
}

void WorkbenchController::refreshDevices()
{
    if (m_shuttingDown || !m_deviceRepository)
        return;
    const Data::NodeId selectedId = m_selectionService ? m_selectionService->currentNodeId()
                                                       : Data::NodeId();
    m_treeModel.syncDevices(m_deviceRepository->devices());
    if (m_selectionService && !selectedId.isNull()
        && !m_treeModel.indexForNodeId(selectedId).isValid()) {
        m_selectionService->clear();
    }
}

void WorkbenchController::watchOptionalProvider(Core::Provider *provider)
{
    if (!provider
        || (provider->kind() != Core::ProviderKind::Scan
            && provider->kind() != Core::ProviderKind::Diagnostics)) {
        return;
    }
    const QMetaObject::Connection connection = connect(
        provider,
        &Core::Provider::availabilityChanged,
        this,
        &WorkbenchController::handleOptionalAvailabilityChanged,
        Qt::UniqueConnection);
    if (connection)
        m_connections.append(connection);
    const QMetaObject::Connection displayNameConnection = connect(
        provider,
        &Core::Provider::displayNameChanged,
        this,
        &WorkbenchController::handleOptionalAvailabilityChanged,
        Qt::UniqueConnection);
    if (displayNameConnection)
        m_connections.append(displayNameConnection);

    if (auto scan = qobject_cast<Core::ScanProvider *>(provider)) {
        for (const QMetaObject::Connection &scanConnection :
             {connect(
                  scan,
                  &Core::ScanProvider::scanStateChanged,
                  this,
                  &WorkbenchController::handleOptionalAvailabilityChanged,
                  Qt::UniqueConnection),
              connect(
                  scan,
                  &Core::ScanProvider::scanProgressChanged,
                  this,
                  &WorkbenchController::handleOptionalAvailabilityChanged,
                  Qt::UniqueConnection),
              connect(
                  scan,
                  &Core::ScanProvider::scanResultChanged,
                  this,
                  &WorkbenchController::handleOptionalAvailabilityChanged,
                  Qt::UniqueConnection)}) {
            if (scanConnection)
                m_connections.append(scanConnection);
        }
    }
    if (auto diagnostics = qobject_cast<Core::DiagnosticsProvider *>(provider)) {
        for (const QMetaObject::Connection &diagnosticsConnection :
             {connect(
                  diagnostics,
                  &Core::DiagnosticsProvider::streamStateChanged,
                  this,
                  &WorkbenchController::handleOptionalAvailabilityChanged,
                  Qt::UniqueConnection),
              connect(
                  diagnostics,
                  &Core::DiagnosticsProvider::diagnosticsSnapshotChanged,
                  this,
                  &WorkbenchController::handleOptionalAvailabilityChanged,
                  Qt::UniqueConnection)}) {
            if (diagnosticsConnection)
                m_connections.append(diagnosticsConnection);
        }
    }
}

void WorkbenchController::watchControllerConnectionProvider(Core::Provider *provider)
{
    auto connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider);
    if (!connectionProvider)
        return;
    ++m_nextControllerConnectionProviderEpoch;
    if (!m_nextControllerConnectionProviderEpoch)
        ++m_nextControllerConnectionProviderEpoch;
    m_controllerConnectionProviderEpochs
        .insert(connectionProvider, m_nextControllerConnectionProviderEpoch);
    m_controllerAutoAcquireStates.remove(connectionProvider);
    m_controllerStartupStates.remove(connectionProvider);
    m_controllerCleanupStates.remove(connectionProvider);
    for (const QMetaObject::Connection &connection :
         {connect(
              connectionProvider,
              &Core::Provider::availabilityChanged,
              this,
              &WorkbenchController::handleControllerConnectionChanged,
              Qt::UniqueConnection),
          connect(
              connectionProvider,
              &Core::Provider::displayNameChanged,
              this,
              &WorkbenchController::handleControllerConnectionChanged,
              Qt::UniqueConnection),
          connect(
              connectionProvider,
              &Core::ControllerConnectionProvider::connectionProfilesChanged,
              this,
              &WorkbenchController::handleControllerConnectionChanged,
              Qt::UniqueConnection),
          connect(
              connectionProvider,
              &Core::ControllerConnectionProvider::connectionSnapshotChanged,
              this,
              &WorkbenchController::handleControllerConnectionChanged,
              Qt::UniqueConnection)}) {
        if (connection)
            m_connections.append(connection);
    }
}

void WorkbenchController::refreshOptionalProviders(Core::Provider *excluding)
{
    if (m_shuttingDown || !m_providerRegistry)
        return;

    Core::Provider *scanObject = preferredOptionalProvider(
        m_providerRegistry, Core::ProviderKind::Scan, excluding);
    Core::Provider *diagnosticsObject = preferredOptionalProvider(
        m_providerRegistry, Core::ProviderKind::Diagnostics, excluding);
    const OptionalProviderPresentation scan = optionalProviderPresentation(
        scanObject, Core::ProviderKind::Scan);
    const OptionalProviderPresentation diagnostics = optionalProviderPresentation(
        diagnosticsObject, Core::ProviderKind::Diagnostics);

    std::optional<Data::ScanResult> scanResult;
    if (scan.isAvailable()) {
        const auto scanProvider = qobject_cast<Core::ScanProvider *>(scanObject);
        QTC_ASSERT(scanProvider, return);
        scanResult = scanProvider->lastScanResult();
    }

    Data::DiagnosticsStreamState diagnosticsState = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest diagnosticsRequest;
    std::optional<Data::DiagnosticsSnapshot> diagnosticsSnapshot;
    if (diagnostics.isAvailable()) {
        const auto diagnosticsProvider = qobject_cast<Core::DiagnosticsProvider *>(
            diagnosticsObject);
        QTC_ASSERT(diagnosticsProvider, return);
        diagnosticsState = diagnosticsProvider->streamState();
        diagnosticsRequest = diagnosticsProvider->activeRequest();
        diagnosticsSnapshot = diagnosticsProvider->latestSnapshot();
    }

    DiagnosticsStatusPresentation diagnosticsStatus;
    diagnosticsStatus.provider = diagnostics;
    diagnosticsStatus.streamState = diagnosticsState;
    diagnosticsStatus.request = diagnosticsRequest;
    if (diagnosticsSnapshot) {
        diagnosticsStatus.request = {
            diagnosticsSnapshot->projectId,
            diagnosticsSnapshot->masterId,
        };
        diagnosticsStatus.runMode = diagnosticsSnapshot->runMode;
        diagnosticsStatus.masterState = diagnosticsSnapshot->masterState;
        diagnosticsStatus.mock = diagnosticsSnapshot->mock;
        diagnosticsStatus.masterHasError = diagnosticsSnapshot->masterHasError;
        diagnosticsStatus.activeAlarmCount = diagnosticsSnapshot->activeAlarmCount;
    }

    const bool diagnosticsChanged = m_diagnosticsProvider != diagnostics;
    const bool diagnosticsAvailabilityChanged
        = m_diagnosticsProvider.isAvailable() != diagnostics.isAvailable();
    const bool statusPresentationChanged = m_diagnosticsStatus != diagnosticsStatus;
    m_scanProvider = scan;
    m_diagnosticsProvider = diagnostics;
    m_diagnosticsStatus = diagnosticsStatus;
    m_treeModel.setProviderPresentations(
        m_scanProvider,
        m_diagnosticsProvider,
        scanResult,
        diagnosticsState,
        diagnosticsRequest,
        diagnosticsSnapshot);
    if (diagnosticsChanged)
        emit diagnosticsProviderChanged(diagnosticsAvailabilityChanged);
    if (statusPresentationChanged)
        emit diagnosticsStatusChanged();
}

void WorkbenchController::handleOptionalAvailabilityChanged()
{
    refreshOptionalProviders();
}

void WorkbenchController::refreshControllerConnectionPresentation()
{
    QList<Data::ControllerConnectionSnapshot> snapshots;
    for (Core::ControllerConnectionProvider *provider : controllerConnectionProviders()) {
        if (provider && m_controllerConnectionProviderEpochs.contains(provider))
            snapshots.append(provider->connectionSnapshot());
    }
    m_treeModel.setControllerConnections(snapshots);
}

void WorkbenchController::handleProjectAboutToBeRemoved(const Data::NodeId &projectId)
{
    if (m_shuttingDown || projectId.isNull())
        return;

    const QScopedValueRollback suppressChanges(m_suppressControllerConnectionChanges, true);
    bool changed = false;
    for (Core::ControllerConnectionProvider *provider : controllerConnectionProviders()) {
        const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
        if (snapshot.scope.projectId != projectId
            || snapshot.state == Data::ControllerConnectionState::Disconnected) {
            continue;
        }
        changed = true;
        if (m_controllerStartupStates.remove(provider)) {
            writeControllerOutput(
                Tr::tr("Automatic controller startup was canceled because its project closed."),
                ControllerOutputLevel::Warning);
        }
        beginControllerCleanup(provider, snapshot);
    }
    changed = m_controllerConnectionSelections.removeIf(
                  [this, &projectId](const ControllerConnectionSelection &selection) {
                      return selection.scope.projectId == projectId
                             && !controllerCleanupBindingIsRetained(selection.scope);
                  })
                  > 0
              || changed;
    if (changed)
        emit controllerConnectionChanged();
}

void WorkbenchController::handleControllerConnectionChanged()
{
    if (m_shuttingDown)
        return;
    const QList<Core::ControllerConnectionProvider *> cleanupProviders
        = m_controllerCleanupStates.keys();
    for (Core::ControllerConnectionProvider *provider : cleanupProviders)
        scheduleControllerCleanup(provider);
    const QList<Core::ControllerConnectionProvider *> startupProviders
        = m_controllerStartupStates.keys();
    for (Core::ControllerConnectionProvider *provider : startupProviders)
        scheduleControllerStartup(provider);
    if (m_suppressControllerConnectionChanges)
        return;

    refreshControllerConnectionPresentation();
    for (Core::ControllerConnectionProvider *provider : controllerConnectionProviders()) {
        if (!provider)
            continue;
        const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
        const QString fingerprint = controllerOutputFingerprint(snapshot);
        auto previous = m_controllerOutputFingerprints.find(provider);
        if (previous == m_controllerOutputFingerprints.end()) {
            m_controllerOutputFingerprints.insert(provider, fingerprint);
            if (snapshot.state == Data::ControllerConnectionState::Disconnected
                && snapshot.controlProgress.state == Data::ControllerControlState::Idle
                && !snapshot.lastError && !snapshot.topology) {
                continue;
            }
        } else {
            if (*previous == fingerprint)
                continue;
            *previous = fingerprint;
        }
        writeControllerOutput(
            controllerOutputMessage(provider, snapshot), controllerOutputLevel(snapshot));
    }
    emit controllerConnectionChanged();
}

void WorkbenchController::scheduleControllerAutoAcquire()
{
    if (m_shuttingDown || m_suppressControllerConnectionChanges)
        return;

    for (Core::ControllerConnectionProvider *provider : controllerConnectionProviders()) {
        if (!provider)
            continue;
        const quint64 providerEpoch = m_controllerConnectionProviderEpochs.value(provider);
        if (!providerEpoch)
            continue;

        const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
        const Data::ControllerConnectionScope scope = snapshot.scope;
        if (!controllerConnectionScopeIsValid(scope))
            continue;
        const ControllerConnectionSelection selection = controllerConnectionSelection(scope);
        if (!selection.providerExplicitlySelected || selection.providerId != provider->id()
            || controllerConnectionProvider(scope) != provider
            || !selection.profileExplicitlySelected || selection.profileId.isNull()
            || snapshot.profileId != selection.profileId || !snapshot.sessionGeneration
            || !snapshot.session || !snapshot.session->sessionId || !snapshot.session->bootId) {
            continue;
        }

        ControllerAutoAcquireState &state = m_controllerAutoAcquireStates[provider];
        if (state.providerEpoch != providerEpoch || state.scope != scope
            || state.profileId != selection.profileId
            || state.sessionGeneration != snapshot.sessionGeneration) {
            state = {
                providerEpoch,
                scope,
                selection.profileId,
                snapshot.sessionGeneration,
                false,
                false,
                false,
                false,
            };
        }

        if (snapshot.session->ownsControlLease) {
            if (!state.acquireAttempted || snapshot.topology || state.discoveryQueued
                || state.discoveryAttempted
                || !canExecuteControllerControl(
                    scope, Data::ControllerControlCommand::DiscoverTopology)) {
                continue;
            }

            state.discoveryQueued = true;
            const QPointer<Core::ControllerConnectionProvider> guardedProvider(provider);
            const Data::NodeId expectedProfileId = selection.profileId;
            const quint64 expectedGeneration = snapshot.sessionGeneration;
            QMetaObject::invokeMethod(
                this,
                [this,
                 guardedProvider,
                 scope,
                 expectedProfileId,
                 expectedGeneration,
                 providerEpoch] {
                    executeControllerAutoDiscovery(
                        guardedProvider,
                        scope,
                        expectedProfileId,
                        expectedGeneration,
                        providerEpoch);
                },
                Qt::QueuedConnection);
            continue;
        }

        if (state.acquireQueued || state.acquireAttempted
            || !canExecuteControllerControl(
                scope, Data::ControllerControlCommand::AcquireControl)) {
            continue;
        }

        state.acquireQueued = true;
        const QPointer<Core::ControllerConnectionProvider> guardedProvider(provider);
        const Data::NodeId expectedProfileId = selection.profileId;
        const quint64 expectedGeneration = snapshot.sessionGeneration;
        QMetaObject::invokeMethod(
            this,
            [this,
             guardedProvider,
             scope,
             expectedProfileId,
             expectedGeneration,
             providerEpoch] {
                executeControllerAutoAcquire(
                    guardedProvider,
                    scope,
                    expectedProfileId,
                    expectedGeneration,
                    providerEpoch);
            },
            Qt::QueuedConnection);
    }
}

void WorkbenchController::executeControllerAutoAcquire(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionScope &expectedScope,
    const Data::NodeId &expectedProfileId,
    quint64 expectedGeneration,
    quint64 expectedProviderEpoch)
{
    if (m_shuttingDown || !provider
        || m_controllerConnectionProviderEpochs.value(provider) != expectedProviderEpoch) {
        return;
    }

    auto state = m_controllerAutoAcquireStates.find(provider);
    if (state == m_controllerAutoAcquireStates.end() || !state->acquireQueued
        || state->acquireAttempted
        || state->providerEpoch != expectedProviderEpoch || state->scope != expectedScope
        || state->profileId != expectedProfileId
        || state->sessionGeneration != expectedGeneration) {
        return;
    }
    state->acquireQueued = false;

    if (!controllerConnectionScopeIsValid(expectedScope)
        || controllerConnectionProvider(expectedScope) != provider) {
        return;
    }
    const ControllerConnectionSelection selection = controllerConnectionSelection(expectedScope);
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (!selection.providerExplicitlySelected || selection.providerId != provider->id()
        || !selection.profileExplicitlySelected || selection.profileId != expectedProfileId
        || snapshot.scope != expectedScope || snapshot.profileId != expectedProfileId
        || snapshot.sessionGeneration != expectedGeneration || !snapshot.session
        || !snapshot.session->sessionId || !snapshot.session->bootId
        || !canExecuteControllerControl(
            expectedScope, Data::ControllerControlCommand::AcquireControl)) {
        return;
    }

    state->acquireAttempted = true;
    Data::ControllerControlRequest request;
    request.command = Data::ControllerControlCommand::AcquireControl;
    const Utils::Result<> result = executeControllerControl(expectedScope, request);
    if (!result) {
        writeControllerOutput(
            Tr::tr("Cannot automatically acquire controller control: %1").arg(result.error()),
            ControllerOutputLevel::Error);
        return;
    }
}

void WorkbenchController::executeControllerAutoDiscovery(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionScope &expectedScope,
    const Data::NodeId &expectedProfileId,
    quint64 expectedGeneration,
    quint64 expectedProviderEpoch)
{
    if (m_shuttingDown || !provider
        || m_controllerConnectionProviderEpochs.value(provider) != expectedProviderEpoch) {
        return;
    }

    auto state = m_controllerAutoAcquireStates.find(provider);
    if (state == m_controllerAutoAcquireStates.end() || !state->discoveryQueued
        || state->discoveryAttempted || !state->acquireAttempted
        || state->providerEpoch != expectedProviderEpoch || state->scope != expectedScope
        || state->profileId != expectedProfileId
        || state->sessionGeneration != expectedGeneration) {
        return;
    }
    state->discoveryQueued = false;

    if (!controllerConnectionScopeIsValid(expectedScope)
        || controllerConnectionProvider(expectedScope) != provider) {
        return;
    }
    const ControllerConnectionSelection selection = controllerConnectionSelection(expectedScope);
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (!selection.providerExplicitlySelected || selection.providerId != provider->id()
        || !selection.profileExplicitlySelected || selection.profileId != expectedProfileId
        || snapshot.scope != expectedScope || snapshot.profileId != expectedProfileId
        || snapshot.sessionGeneration != expectedGeneration || !snapshot.session
        || !snapshot.session->ownsControlLease || snapshot.topology
        || !canExecuteControllerControl(
            expectedScope, Data::ControllerControlCommand::DiscoverTopology)) {
        return;
    }

    state->discoveryAttempted = true;
    Data::ControllerControlRequest request;
    request.command = Data::ControllerControlCommand::DiscoverTopology;
    const Utils::Result<> result = executeControllerControl(expectedScope, request);
    if (!result) {
        writeControllerOutput(
            Tr::tr("Cannot automatically scan the EtherCAT bus: %1").arg(result.error()),
            ControllerOutputLevel::Error);
    }
}

QString WorkbenchController::controllerStartupUnavailableReason(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionSnapshot &snapshot) const
{
    if (!provider)
        return Tr::tr("The selected controller adapter is unavailable.");
    if (m_controllerStartupStates.contains(provider))
        return Tr::tr("Automatic controller startup is in progress.");

    using Command = Data::ControllerControlCommand;
    for (const Command command :
         {Command::EnterConfigurationMode,
          Command::DiscoverTopology,
          Command::RestoreActivePackage,
          Command::Start}) {
        if (!provider->supportsControlCommand(command)) {
            return Tr::tr(
                "The controller does not support the complete automatic startup sequence.");
        }
    }

    if (const QString reason
        = controllerControlStateUnavailableReason(snapshot, Command::EnterConfigurationMode);
        !reason.isEmpty()) {
        return reason;
    }
    if (!snapshot.package || snapshot.package->activeSlot == Data::ControllerSlot::None
        || !snapshot.package->activeGeneration || !snapshot.package->activeConfigurationId) {
        return Tr::tr("No restorable active controller package is available.");
    }
    return {};
}

Utils::Result<> WorkbenchController::beginControllerStartup(
    Core::ControllerConnectionProvider *provider, const Data::ControllerConnectionSnapshot &snapshot)
{
    const QString unavailableReason = controllerStartupUnavailableReason(provider, snapshot);
    if (!unavailableReason.isEmpty())
        return Utils::ResultError(unavailableReason);
    QTC_ASSERT(
        provider,
        return Utils::ResultError(Tr::tr("The selected controller adapter is unavailable.")));
    if (!snapshot.session || !snapshot.session->sessionId || !snapshot.session->bootId
        || !snapshot.sessionGeneration) {
        return Utils::ResultError(Tr::tr("The controller session identity is incomplete."));
    }
    const quint64 providerEpoch = m_controllerConnectionProviderEpochs.value(provider);
    if (!providerEpoch)
        return Utils::ResultError(Tr::tr("The controller adapter instance is unavailable."));

    ControllerStartupState state;
    state.providerEpoch = providerEpoch;
    state.scope = snapshot.scope;
    state.profileId = snapshot.profileId;
    state.sessionGeneration = snapshot.sessionGeneration;
    state.sessionId = snapshot.session->sessionId;
    state.bootId = snapshot.session->bootId;
    state.phase = ControllerStartupPhase::WaitingForConfiguration;
    state.remainingPolls = controllerStartupMaximumPhasePolls;
    m_controllerStartupStates.insert(provider, state);

    writeControllerOutput(
        Tr::tr(
            "Starting controller: enter configuration, scan the bus, restore the active package, "
            "then start runtime."));

    Data::ControllerControlRequest request;
    request.command = Data::ControllerControlCommand::EnterConfigurationMode;
    const Utils::Result<> result = provider->executeControlCommand(request);
    if (!result) {
        m_controllerStartupStates.remove(provider);
        return result;
    }
    scheduleControllerStartup(provider, controllerStartupPollIntervalMs);
    emit controllerConnectionChanged();
    return {};
}

void WorkbenchController::scheduleControllerStartup(
    Core::ControllerConnectionProvider *provider, int delayMs)
{
    if (m_shuttingDown || !provider)
        return;
    auto state = m_controllerStartupStates.find(provider);
    if (state == m_controllerStartupStates.end() || state->scheduled)
        return;

    state->scheduled = true;
    const QPointer<Core::ControllerConnectionProvider> guardedProvider(provider);
    const Data::ControllerConnectionScope expectedScope = state->scope;
    const Data::NodeId expectedProfileId = state->profileId;
    const quint64 expectedGeneration = state->sessionGeneration;
    const quint64 expectedProviderEpoch = state->providerEpoch;
    QTimer::singleShot(
        qMax(0, delayMs),
        this,
        [this,
         guardedProvider,
         expectedScope,
         expectedProfileId,
         expectedGeneration,
         expectedProviderEpoch] {
            advanceControllerStartup(
                guardedProvider,
                expectedScope,
                expectedProfileId,
                expectedGeneration,
                expectedProviderEpoch);
        });
}

void WorkbenchController::advanceControllerStartup(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionScope &expectedScope,
    const Data::NodeId &expectedProfileId,
    quint64 expectedGeneration,
    quint64 expectedProviderEpoch)
{
    if (m_shuttingDown || !provider)
        return;
    auto state = m_controllerStartupStates.find(provider);
    if (state == m_controllerStartupStates.end() || state->scope != expectedScope
        || state->profileId != expectedProfileId || state->sessionGeneration != expectedGeneration
        || state->providerEpoch != expectedProviderEpoch) {
        return;
    }
    state->scheduled = false;

    if (m_controllerConnectionProviderEpochs.value(provider) != expectedProviderEpoch) {
        failControllerStartup(
            provider, Tr::tr("The controller adapter instance changed during startup."));
        return;
    }

    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (!controllerConnectionScopeIsValid(expectedScope) || snapshot.scope != expectedScope
        || snapshot.profileId != expectedProfileId
        || snapshot.sessionGeneration != expectedGeneration || !snapshot.session
        || snapshot.session->sessionId != state->sessionId
        || snapshot.session->bootId != state->bootId) {
        failControllerStartup(
            provider, Tr::tr("The controller session changed during automatic startup."));
        return;
    }
    if (snapshot.state != Data::ControllerConnectionState::Connected
        && snapshot.state != Data::ControllerConnectionState::Degraded) {
        failControllerStartup(
            provider, Tr::tr("The controller disconnected during automatic startup."));
        return;
    }
    if (snapshot.mock || snapshot.readOnly || !snapshot.session->ownsControlLease
        || snapshot.session->controlLeaseOwnerSessionId != snapshot.session->sessionId) {
        failControllerStartup(
            provider, Tr::tr("Exclusive controller control was lost during automatic startup."));
        return;
    }
    if (state->remainingPolls <= 0) {
        failControllerStartup(
            provider, Tr::tr("Timed out while waiting for automatic controller startup."));
        return;
    }
    --state->remainingPolls;

    using Command = Data::ControllerControlCommand;
    const Command expectedCommand = [phase = state->phase] {
        switch (phase) {
        case ControllerStartupPhase::WaitingForConfiguration:
            return Command::EnterConfigurationMode;
        case ControllerStartupPhase::WaitingForTopology:
            return Command::DiscoverTopology;
        case ControllerStartupPhase::WaitingForRestore:
            return Command::RestoreActivePackage;
        case ControllerStartupPhase::WaitingForRunning:
            return Command::Start;
        }
        return Command::None;
    }();
    const Data::ControllerControlProgress &progress = snapshot.controlProgress;
    if (progress.state == Data::ControllerControlState::Pending) {
        if (progress.command != expectedCommand) {
            failControllerStartup(
                provider, Tr::tr("A different controller operation replaced automatic startup."));
            return;
        }
        scheduleControllerStartup(provider, controllerStartupPollIntervalMs);
        return;
    }
    if (progress.command == expectedCommand
        && progress.state == Data::ControllerControlState::Failed) {
        failControllerStartup(
            provider,
            progress.detail.isEmpty() ? Tr::tr("A controller startup step failed.")
                                      : progress.detail);
        return;
    }

    const bool expectedCommandSucceeded = progress.command == expectedCommand
                                          && progress.state
                                                 == Data::ControllerControlState::Succeeded;
    const auto waitForSnapshot = [this, provider, &state] {
        if (state->refreshCooldown <= 0) {
            state->refreshCooldown = 5;
            provider->refreshController();
        } else {
            --state->refreshCooldown;
        }
        scheduleControllerStartup(provider, controllerStartupPollIntervalMs);
    };
    const auto dispatch = [this,
                           provider,
                           &state,
                           &snapshot](Command command, ControllerStartupPhase nextPhase) {
        if (!provider->supportsControlCommand(command)) {
            failControllerStartup(
                provider,
                Tr::tr("The controller does not support the complete automatic startup sequence."));
            return;
        }
        if (const QString reason = controllerControlStateUnavailableReason(snapshot, command);
            !reason.isEmpty()) {
            failControllerStartup(provider, reason);
            return;
        }
        state->phase = nextPhase;
        state->remainingPolls = controllerStartupMaximumPhasePolls;
        state->refreshCooldown = 0;
        Data::ControllerControlRequest request;
        request.command = command;
        const Utils::Result<> result = provider->executeControlCommand(request);
        if (!result) {
            failControllerStartup(provider, result.error());
            return;
        }
        scheduleControllerStartup(provider, controllerStartupPollIntervalMs);
    };

    if (!expectedCommandSucceeded) {
        waitForSnapshot();
        return;
    }

    const std::optional<Data::ControllerStateSummary> &controllerState = snapshot.controllerState;
    switch (state->phase) {
    case ControllerStartupPhase::WaitingForConfiguration:
        if (!controllerState || !controllerState->ready
            || controllerState->serviceState != Data::ControllerServiceState::Shutdown
            || !snapshot.package
            || snapshot.package->controllerState == Data::ControllerPackageState::Active) {
            waitForSnapshot();
            return;
        }
        dispatch(Command::DiscoverTopology, ControllerStartupPhase::WaitingForTopology);
        return;
    case ControllerStartupPhase::WaitingForTopology:
        if (!snapshot.topology || snapshot.topology->result
            || !snapshot.topology->respondingCount
            || snapshot.topology->respondingCount != quint32(snapshot.topology->slaves.size())) {
            waitForSnapshot();
            return;
        }
        dispatch(Command::RestoreActivePackage, ControllerStartupPhase::WaitingForRestore);
        return;
    case ControllerStartupPhase::WaitingForRestore:
        if (!controllerState
            || controllerState->serviceState != Data::ControllerServiceState::OperationalSafe
            || !controllerPackageIsActive(snapshot)) {
            waitForSnapshot();
            return;
        }
        dispatch(Command::Start, ControllerStartupPhase::WaitingForRunning);
        return;
    case ControllerStartupPhase::WaitingForRunning:
        if (!controllerState || !controllerState->ready
            || controllerState->serviceState != Data::ControllerServiceState::Running
            || !controllerState->applicationActive || !controllerState->busOperational
            || !(controllerState->ethercatAlStateBits & 0x08) || !controllerState->cycleCount
            || !controllerState->expectedWorkingCounter
            || controllerState->actualWorkingCounter != controllerState->expectedWorkingCounter
            || controllerState->currentFaults || controllerState->latchedFaults) {
            waitForSnapshot();
            return;
        }
        finishControllerStartup(provider);
        return;
    }
}

void WorkbenchController::finishControllerStartup(Core::ControllerConnectionProvider *provider)
{
    if (!provider || !m_controllerStartupStates.remove(provider))
        return;
    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (snapshot.controllerState) {
        writeControllerOutput(
            Tr::tr("Controller startup completed: Running, WKC %1/%2, cycle %3.")
                .arg(snapshot.controllerState->actualWorkingCounter)
                .arg(snapshot.controllerState->expectedWorkingCounter)
                .arg(snapshot.controllerState->cycleCount));
    } else {
        writeControllerOutput(Tr::tr("Controller startup completed."));
    }
    emit controllerConnectionChanged();
}

void WorkbenchController::failControllerStartup(
    Core::ControllerConnectionProvider *provider, const QString &reason)
{
    if (!provider || !m_controllerStartupStates.remove(provider))
        return;
    writeControllerOutput(
        Tr::tr("Controller startup failed: %1").arg(reason), ControllerOutputLevel::Error);
    emit controllerConnectionChanged();
}

void WorkbenchController::beginControllerCleanup(
    Core::ControllerConnectionProvider *provider, const Data::ControllerConnectionSnapshot &snapshot)
{
    if (m_shuttingDown || !provider)
        return;
    m_controllerStartupStates.remove(provider);
    const quint64 providerEpoch = m_controllerConnectionProviderEpochs.value(provider);
    if (!providerEpoch)
        return;

    ControllerCleanupState state;
    state.providerEpoch = providerEpoch;
    state.scope = snapshot.scope;
    state.profileId = snapshot.profileId;
    state.sessionGeneration = snapshot.sessionGeneration;
    if (snapshot.session) {
        state.sessionId = snapshot.session->sessionId;
        state.bootId = snapshot.session->bootId;
    }
    state.remainingPolls = controllerCleanupMaximumPolls;
    if (snapshot.state == Data::ControllerConnectionState::Disconnecting) {
        state.phase = ControllerCleanupPhase::WaitingForDisconnect;
        state.disconnectAccepted = true;
    }
    m_controllerCleanupStates.insert(provider, state);
    scheduleControllerCleanup(provider);
}

void WorkbenchController::scheduleControllerCleanup(
    Core::ControllerConnectionProvider *provider, int delayMs)
{
    if (m_shuttingDown || !provider)
        return;
    auto state = m_controllerCleanupStates.find(provider);
    if (state == m_controllerCleanupStates.end() || state->phase == ControllerCleanupPhase::Failed
        || state->scheduled) {
        return;
    }

    state->scheduled = true;
    const QPointer<Core::ControllerConnectionProvider> guardedProvider(provider);
    const Data::ControllerConnectionScope expectedScope = state->scope;
    const Data::NodeId expectedProfileId = state->profileId;
    const quint64 expectedGeneration = state->sessionGeneration;
    const quint64 expectedProviderEpoch = state->providerEpoch;
    QTimer::singleShot(
        qMax(0, delayMs),
        this,
        [this,
         guardedProvider,
         expectedScope,
         expectedProfileId,
         expectedGeneration,
         expectedProviderEpoch] {
            advanceControllerCleanup(
                guardedProvider,
                expectedScope,
                expectedProfileId,
                expectedGeneration,
                expectedProviderEpoch);
        });
}

void WorkbenchController::advanceControllerCleanup(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionScope &expectedScope,
    const Data::NodeId &expectedProfileId,
    quint64 expectedGeneration,
    quint64 expectedProviderEpoch)
{
    if (m_shuttingDown || !provider)
        return;
    auto stateIt = m_controllerCleanupStates.find(provider);
    if (stateIt == m_controllerCleanupStates.end() || stateIt->scope != expectedScope
        || stateIt->profileId != expectedProfileId
        || stateIt->sessionGeneration != expectedGeneration
        || stateIt->providerEpoch != expectedProviderEpoch) {
        return;
    }
    stateIt->scheduled = false;
    if (m_controllerConnectionProviderEpochs.value(provider) != expectedProviderEpoch) {
        failControllerCleanup(
            provider, Tr::tr("The controller adapter instance changed during cleanup."));
        return;
    }

    const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
    if (snapshot.state == Data::ControllerConnectionState::Disconnected) {
        finishControllerCleanup(provider);
        return;
    }
    if (snapshot.scope != stateIt->scope || snapshot.profileId != stateIt->profileId) {
        failControllerCleanup(
            provider, Tr::tr("The controller connection scope or profile changed during cleanup."));
        return;
    }
    if (snapshot.sessionGeneration != stateIt->sessionGeneration) {
        if (stateIt->generationChanges >= controllerCleanupMaximumGenerationChanges) {
            failControllerCleanup(
                provider, Tr::tr("The controller session changed repeatedly during cleanup."));
            return;
        }
        ControllerCleanupState next;
        next.providerEpoch = stateIt->providerEpoch;
        next.scope = stateIt->scope;
        next.profileId = stateIt->profileId;
        next.sessionGeneration = snapshot.sessionGeneration;
        next.generationChanges = stateIt->generationChanges + 1;
        next.remainingPolls = stateIt->remainingPolls;
        if (snapshot.session) {
            next.sessionId = snapshot.session->sessionId;
            next.bootId = snapshot.session->bootId;
        }
        if (snapshot.state == Data::ControllerConnectionState::Disconnecting) {
            next.phase = ControllerCleanupPhase::WaitingForDisconnect;
            next.disconnectAccepted = true;
        }
        *stateIt = next;
        scheduleControllerCleanup(provider);
        return;
    }
    if (stateIt->sessionId) {
        if (!snapshot.session || snapshot.session->sessionId != stateIt->sessionId
            || snapshot.session->bootId != stateIt->bootId) {
            failControllerCleanup(
                provider,
                Tr::tr("The controller session identity changed without a new generation."));
            return;
        }
    } else if (snapshot.session) {
        stateIt->sessionId = snapshot.session->sessionId;
        stateIt->bootId = snapshot.session->bootId;
    }

    if (stateIt->remainingPolls <= 0) {
        QString reason = Tr::tr("Timed out while waiting for safe controller cleanup.");
        if (!stateIt->failure.isEmpty())
            reason += Tr::tr(" Last error: %1").arg(stateIt->failure);
        failControllerCleanup(provider, reason);
        return;
    }
    --stateIt->remainingPolls;

    const auto waitForSnapshot = [this, provider, &stateIt](bool refresh) {
        if (refresh && stateIt->refreshCooldown <= 0) {
            stateIt->refreshCooldown = controllerCleanupRefreshIntervalPolls;
            const Utils::Result<> result = provider->refreshController();
            if (!result)
                stateIt->failure = result.error();
        } else if (stateIt->refreshCooldown > 0) {
            --stateIt->refreshCooldown;
        }
        scheduleControllerCleanup(provider, controllerCleanupPollIntervalMs);
    };
    const auto dispatchControl =
        [this,
         provider,
         &stateIt](Data::ControllerControlCommand command, ControllerCleanupPhase waitingPhase) {
            if (!provider->supportsControlCommand(command)) {
                failControllerCleanup(
                    provider,
                    Tr::tr("The controller adapter does not support the required cleanup command."));
                return;
            }
            Data::ControllerControlRequest request;
            request.command = command;
            stateIt->phase = waitingPhase;
            stateIt->refreshCooldown = 0;
            const Utils::Result<> result = provider->executeControlCommand(request);
            if (!result) {
                failControllerCleanup(provider, result.error());
                return;
            }
            scheduleControllerCleanup(provider, controllerCleanupPollIntervalMs);
        };
    const auto dispatchDisconnect = [this, provider, &stateIt] {
        if (stateIt->disconnectAttempts >= controllerCleanupMaximumDisconnectAttempts) {
            failControllerCleanup(
                provider, Tr::tr("The controller adapter repeatedly rejected disconnect."));
            return;
        }
        ++stateIt->disconnectAttempts;
        stateIt->phase = ControllerCleanupPhase::WaitingForDisconnect;
        stateIt->disconnectAccepted = false;
        const Utils::Result<> result = provider->disconnectFromController();
        if (!result) {
            stateIt->failure = result.error();
            if (stateIt->disconnectAttempts >= controllerCleanupMaximumDisconnectAttempts) {
                failControllerCleanup(provider, result.error());
                return;
            }
            stateIt->phase = ControllerCleanupPhase::Evaluate;
        } else {
            stateIt->disconnectAccepted = true;
        }
        scheduleControllerCleanup(provider, controllerCleanupPollIntervalMs);
    };

    if (!provider->isAvailable()) {
        failControllerCleanup(
            provider, Tr::tr("The controller adapter became unavailable during cleanup."));
        return;
    }
    if (snapshot.state == Data::ControllerConnectionState::Disconnecting) {
        stateIt->phase = ControllerCleanupPhase::WaitingForDisconnect;
        stateIt->disconnectAccepted = true;
        waitForSnapshot(false);
        return;
    }
    if (snapshot.controlProgress.state == Data::ControllerControlState::Pending) {
        if (stateIt->phase == ControllerCleanupPhase::Evaluate
            || stateIt->phase == ControllerCleanupPhase::WaitingForStableState
            || stateIt->phase == ControllerCleanupPhase::WaitingForPending) {
            stateIt->phase = ControllerCleanupPhase::WaitingForPending;
        }
        waitForSnapshot(false);
        return;
    }

    switch (stateIt->phase) {
    case ControllerCleanupPhase::WaitingForPending:
        stateIt->phase = ControllerCleanupPhase::Evaluate;
        scheduleControllerCleanup(provider);
        return;
    case ControllerCleanupPhase::WaitingForRelease:
        if (snapshot.session && !snapshot.session->ownsControlLease) {
            stateIt->phase = ControllerCleanupPhase::Evaluate;
            scheduleControllerCleanup(provider);
            return;
        }
        if (snapshot.controlProgress.command == Data::ControllerControlCommand::ReleaseControl
            && snapshot.controlProgress.state == Data::ControllerControlState::Failed) {
            failControllerCleanup(
                provider,
                snapshot.controlProgress.detail.isEmpty()
                    ? Tr::tr("Release control failed during project cleanup.")
                    : snapshot.controlProgress.detail);
            return;
        }
        waitForSnapshot(true);
        return;
    case ControllerCleanupPhase::WaitingForDisconnect:
        if (stateIt->disconnectAccepted) {
            waitForSnapshot(false);
            return;
        }
        stateIt->phase = ControllerCleanupPhase::Evaluate;
        break;
    case ControllerCleanupPhase::WaitingForStableState:
        stateIt->phase = ControllerCleanupPhase::Evaluate;
        break;
    case ControllerCleanupPhase::Evaluate:
        break;
    case ControllerCleanupPhase::Failed:
        return;
    }

    if (snapshot.state == Data::ControllerConnectionState::Connecting
        || snapshot.state == Data::ControllerConnectionState::Handshaking) {
        if (snapshot.session) {
            stateIt->phase = ControllerCleanupPhase::WaitingForStableState;
            waitForSnapshot(true);
        } else {
            dispatchDisconnect();
        }
        return;
    }
    if (snapshot.state == Data::ControllerConnectionState::Failed) {
        failControllerCleanup(
            provider, Tr::tr("The controller connection failed before safe cleanup was confirmed."));
        return;
    }
    if (snapshot.state != Data::ControllerConnectionState::Connected
        && snapshot.state != Data::ControllerConnectionState::Degraded) {
        failControllerCleanup(
            provider, Tr::tr("The controller connection entered an unknown cleanup state."));
        return;
    }
    if (!snapshot.session) {
        stateIt->phase = ControllerCleanupPhase::WaitingForStableState;
        stateIt->failure = Tr::tr("The controller session identity is unavailable.");
        waitForSnapshot(true);
        return;
    }

    const Data::ControllerSessionSummary &session = *snapshot.session;
    if (!session.sessionId || !session.bootId) {
        stateIt->phase = ControllerCleanupPhase::WaitingForStableState;
        stateIt->failure = Tr::tr("The controller session identity is incomplete.");
        waitForSnapshot(true);
        return;
    }
    if (!session.ownsControlLease) {
        if (session.controlLeaseOwnerSessionId == session.sessionId) {
            stateIt->phase = ControllerCleanupPhase::WaitingForStableState;
            stateIt->failure = Tr::tr("The controller lease ownership snapshot is inconsistent.");
            waitForSnapshot(true);
            return;
        }
        dispatchDisconnect();
        return;
    }
    if (snapshot.readOnly || snapshot.mock
        || session.controlLeaseOwnerSessionId != session.sessionId) {
        stateIt->phase = ControllerCleanupPhase::WaitingForStableState;
        stateIt->failure = Tr::tr("The controller lease ownership snapshot is inconsistent.");
        waitForSnapshot(true);
        return;
    }
    using Command = Data::ControllerControlCommand;
    const QString reason
        = controllerControlStateUnavailableReason(snapshot, Command::ReleaseControl);
    if (!reason.isEmpty()) {
        stateIt->phase = ControllerCleanupPhase::WaitingForStableState;
        stateIt->failure = reason;
        waitForSnapshot(true);
        return;
    }
    dispatchControl(Command::ReleaseControl, ControllerCleanupPhase::WaitingForRelease);
}

void WorkbenchController::finishControllerCleanup(Core::ControllerConnectionProvider *provider)
{
    const auto state = m_controllerCleanupStates.find(provider);
    if (state == m_controllerCleanupStates.end())
        return;
    const Data::ControllerConnectionScope scope = state->scope;
    m_controllerCleanupStates.erase(state);
    if (!controllerCleanupBindingIsRetained(scope)) {
        m_controllerConnectionSelections.removeIf(
            [&scope](const ControllerConnectionSelection &selection) {
                return selection.scope == scope;
            });
    }
    emit controllerConnectionChanged();
}

void WorkbenchController::failControllerCleanup(
    Core::ControllerConnectionProvider *provider, const QString &reason)
{
    auto state = m_controllerCleanupStates.find(provider);
    if (state == m_controllerCleanupStates.end() || state->phase == ControllerCleanupPhase::Failed) {
        return;
    }
    state->phase = ControllerCleanupPhase::Failed;
    state->scheduled = false;
    state->failure = reason;
    writeControllerOutput(
        Tr::tr(
            "Automatic controller cleanup stopped safely: %1 The connection remains unchanged and "
            "was not reported as disconnected.")
            .arg(reason),
        ControllerOutputLevel::Error);
    emit controllerConnectionChanged();
}

bool WorkbenchController::controllerCleanupBindingIsRetained(
    const Data::ControllerConnectionScope &scope) const
{
    return std::any_of(
        m_controllerCleanupStates.cbegin(),
        m_controllerCleanupStates.cend(),
        [&scope](const ControllerCleanupState &state) { return state.scope == scope; });
}

ControllerConnectionSelection *WorkbenchController::mutableControllerConnectionSelection(
    const Data::ControllerConnectionScope &scope)
{
    const auto selection = std::find_if(
        m_controllerConnectionSelections.begin(),
        m_controllerConnectionSelections.end(),
        [&scope](const ControllerConnectionSelection &candidate) {
            return candidate.scope == scope;
        });
    return selection == m_controllerConnectionSelections.end() ? nullptr : &*selection;
}

} // namespace EtherCAT::Workbench::Internal
