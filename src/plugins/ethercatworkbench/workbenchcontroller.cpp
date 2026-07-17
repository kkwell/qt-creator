// Copyright (C) 2026 Kvell

#include "workbenchcontroller.h"

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>

#include <algorithm>
#include <utility>

namespace EtherCAT::Workbench::Internal {

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
            refreshProviderPresentation();
        }));
    m_connections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        this,
        [this](Core::Provider *provider) {
            refreshOptionalProviders(provider);
            refreshProviderPresentation(provider);
        }));
    for (Core::Provider *provider : m_providerRegistry->providers())
        watchOptionalProvider(provider);
    refreshOptionalProviders();
    refreshProviderPresentation();
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

bool WorkbenchController::scanAvailable() const
{
    return m_scanAvailable;
}

bool WorkbenchController::diagnosticsAvailable() const
{
    return m_diagnosticsAvailable;
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
    m_treeModel.clear();
}

void WorkbenchController::refreshProjects()
{
    if (m_shuttingDown || !m_projectService)
        return;
    const Data::NodeId selectedId = m_selectionService ? m_selectionService->currentNodeId()
                                                       : Data::NodeId();
    m_treeModel.setProjects(m_projectService->projects());
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

    if (auto scan = qobject_cast<Core::ScanProvider *>(provider)) {
        for (const QMetaObject::Connection &scanConnection :
             {connect(
                  scan,
                  &Core::ScanProvider::scanStateChanged,
                  this,
                  &WorkbenchController::handleProviderPresentationChanged,
                  Qt::UniqueConnection),
              connect(
                  scan,
                  &Core::ScanProvider::scanProgressChanged,
                  this,
                  &WorkbenchController::handleProviderPresentationChanged,
                  Qt::UniqueConnection),
              connect(
                  scan,
                  &Core::ScanProvider::scanResultChanged,
                  this,
                  &WorkbenchController::handleProviderPresentationChanged,
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
                  &WorkbenchController::handleProviderPresentationChanged,
                  Qt::UniqueConnection),
              connect(
                  diagnostics,
                  &Core::DiagnosticsProvider::diagnosticsSnapshotChanged,
                  this,
                  &WorkbenchController::handleProviderPresentationChanged,
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
    const auto isAvailable = [this, excluding](Core::ProviderKind kind) {
        for (Core::Provider *provider : m_providerRegistry->providers(kind)) {
            if (provider != excluding && provider->isAvailable())
                return true;
        }
        return false;
    };
    const bool scanAvailable = isAvailable(Core::ProviderKind::Scan);
    const bool diagnosticsAvailable = isAvailable(Core::ProviderKind::Diagnostics);
    if (m_scanAvailable == scanAvailable && m_diagnosticsAvailable == diagnosticsAvailable)
        return;
    m_scanAvailable = scanAvailable;
    m_diagnosticsAvailable = diagnosticsAvailable;
    m_treeModel.setOptionalProviders(m_scanAvailable, m_diagnosticsAvailable);
    emit optionalProvidersChanged();
}

void WorkbenchController::refreshProviderPresentation(Core::Provider *excluding)
{
    if (m_shuttingDown || !m_providerRegistry)
        return;

    QList<Core::ScanProvider *> scanProviders;
    for (Core::Provider *provider : m_providerRegistry->providers(Core::ProviderKind::Scan)) {
        auto scan = qobject_cast<Core::ScanProvider *>(provider);
        if (scan && scan != excluding && scan->isAvailable())
            scanProviders.append(scan);
    }
    std::sort(scanProviders.begin(), scanProviders.end(), [](const auto *left, const auto *right) {
        const auto score = [](const Core::ScanProvider *provider) {
            if (provider->lastScanResult())
                return 3;
            switch (provider->scanState()) {
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
        };
        const int leftScore = score(left);
        const int rightScore = score(right);
        if (leftScore != rightScore)
            return leftScore > rightScore;
        return left->id().toString() < right->id().toString();
    });
    if (scanProviders.isEmpty()) {
        m_treeModel.setScanPresentation(std::nullopt);
    } else {
        Core::ScanProvider *scan = scanProviders.first();
        m_treeModel.setScanPresentation(scan->lastScanResult());
    }

    QList<Core::DiagnosticsProvider *> diagnosticsProviders;
    for (Core::Provider *provider : m_providerRegistry->providers(Core::ProviderKind::Diagnostics)) {
        auto diagnostics = qobject_cast<Core::DiagnosticsProvider *>(provider);
        if (diagnostics && diagnostics != excluding && diagnostics->isAvailable())
            diagnosticsProviders.append(diagnostics);
    }
    std::sort(
        diagnosticsProviders.begin(),
        diagnosticsProviders.end(),
        [](const auto *left, const auto *right) {
            const auto score = [](const Core::DiagnosticsProvider *provider) {
                if (provider->latestSnapshot())
                    return 2;
                return provider->streamState() == Data::DiagnosticsStreamState::Stopped ? 0 : 1;
            };
            const int leftScore = score(left);
            const int rightScore = score(right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left->id().toString() < right->id().toString();
        });
    if (diagnosticsProviders.isEmpty()) {
        m_treeModel
            .setDiagnosticsPresentation(Data::DiagnosticsStreamState::Stopped, {}, std::nullopt);
    } else {
        Core::DiagnosticsProvider *diagnostics = diagnosticsProviders.first();
        m_treeModel.setDiagnosticsPresentation(
            diagnostics->streamState(), diagnostics->activeRequest(), diagnostics->latestSnapshot());
    }
}

void WorkbenchController::handleOptionalAvailabilityChanged()
{
    refreshOptionalProviders();
    refreshProviderPresentation();
}

void WorkbenchController::handleProviderPresentationChanged()
{
    refreshProviderPresentation();
}

} // namespace EtherCAT::Workbench::Internal
