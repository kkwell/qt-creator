// Copyright (C) 2026 Kvell

#include "workbenchcontroller.h"

#include "esiconfigurationfactory.h"
#include "ethercatworkbenchtr.h"

#include <coreplugin/messagemanager.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>

#include <algorithm>
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
        [this] { refreshProjects(); },
        Qt::QueuedConnection));
    m_connections.append(connect(
        m_projectService,
        &Core::ProjectService::projectChanged,
        this,
        [this] { refreshProjects(); }));
    m_connections.append(connect(
        m_projectService,
        &Core::ProjectService::activeProjectChanged,
        this,
        [this] { refreshProjects(); }));
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
            refreshOptionalProviders();
        }));
    m_connections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        this,
        [this](Core::Provider *provider) {
            refreshOptionalProviders(provider);
        }));
    for (Core::Provider *provider : m_providerRegistry->providers())
        watchOptionalProvider(provider);
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
    return device && device->summary.supported && activeMasterContext(m_projectService).has_value();
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
    return project && project->id != m_projectService->activeProjectId();
}

Utils::Result<> WorkbenchController::addSelectedDeviceToMaster()
{
    if (m_shuttingDown || !m_selectionService || !m_deviceRepository || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    const Core::PropertyPageContext context = m_treeModel.contextForNodeId(
        m_selectionService->currentNodeId());
    if (context.nodeKind != Core::WorkbenchNodeKind::Device)
        return Utils::ResultError(Tr::tr("Select an ESI device before adding it."));
    const std::optional<ActiveMasterContext> target = activeMasterContext(m_projectService);
    if (!target)
        return Utils::ResultError(Tr::tr("Open and activate an offline EtherCAT project first."));
    return addDeviceToMaster(context.nodeId, target->masterId);
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

Utils::Result<> WorkbenchController::removeSelectedOfflineSlave()
{
    if (m_shuttingDown || !m_selectionService || !m_projectService)
        return Utils::ResultError(Tr::tr("The offline topology services are unavailable."));
    std::optional<SelectedSlaveContext> selected
        = selectedSlaveContext(m_treeModel, m_selectionService, m_projectService);
    if (!selected)
        return Utils::ResultError(Tr::tr("Select a configured offline slave to remove."));

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
}

void WorkbenchController::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
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

    const bool diagnosticsChanged = m_diagnosticsProvider != diagnostics;
    const bool diagnosticsAvailabilityChanged
        = m_diagnosticsProvider.isAvailable() != diagnostics.isAvailable();
    m_scanProvider = scan;
    m_diagnosticsProvider = diagnostics;
    m_treeModel.setProviderPresentations(
        m_scanProvider,
        m_diagnosticsProvider,
        scanResult,
        diagnosticsState,
        diagnosticsRequest,
        diagnosticsSnapshot);
    if (diagnosticsChanged)
        emit diagnosticsProviderChanged(diagnosticsAvailabilityChanged);
}

void WorkbenchController::handleOptionalAvailabilityChanged()
{
    refreshOptionalProviders();
}

} // namespace EtherCAT::Workbench::Internal
