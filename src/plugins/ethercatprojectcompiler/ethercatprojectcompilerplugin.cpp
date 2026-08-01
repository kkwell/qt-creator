// Copyright (C) 2026 Embed Labs

#include "durableruntimepackagecompilerpreparationcoordinator.h"
#include "provisionedruntimepackagecompilerprovider.h"

#ifdef WITH_TESTS
#include "ethercatprojectcompilertests.h"
#endif

#include <coreplugin/icore.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/providers.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <QPointer>
#include <QThread>

#include <memory>

namespace EtherCAT::ProjectCompiler::Internal {

class EtherCATProjectCompilerPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATProjectCompiler.json")

public:
    ~EtherCATProjectCompilerPlugin() final;

    void initialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void shutdown();

    std::unique_ptr<ProvisionedRuntimePackageCompilerProvider> m_provider;
    std::unique_ptr<DurableRuntimePackageCompilerPreparationCoordinator> m_coordinator;
    bool m_providerRegistered = false;
    bool m_coordinatorRegistered = false;
};

EtherCATProjectCompilerPlugin::~EtherCATProjectCompilerPlugin()
{
    shutdown();
}

void EtherCATProjectCompilerPlugin::initialize()
{
    const Utils::FilePath compilerRoot = ::Core::ICore::userResourcePath("ethercat/compiler");
    m_provider = std::make_unique<ProvisionedRuntimePackageCompilerProvider>(
        compilerRoot / "provisioning.json", compilerRoot);
    ExtensionSystem::PluginManager::addObject(m_provider.get());
    m_providerRegistered = true;

    auto *providerRegistry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    if (providerRegistry) {
        const QPointer<Core::ProviderRegistry> registryGuard(providerRegistry);
        const RuntimePackageCompilerCurrentProjectCapture currentProjectCapture =
            [registryGuard](const Data::NodeId &projectId)
            -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
            if (!registryGuard || QThread::currentThread() != registryGuard->thread()) {
                return Utils::ResultError(
                    QStringLiteral("Project provider registry belongs to another thread."));
            }
            QList<Core::ProjectService *> matches;
            for (Core::Provider *provider :
                 registryGuard->providers(Core::ProviderKind::Project)) {
                auto *projectService = qobject_cast<Core::ProjectService *>(provider);
                if (projectService && projectService->isAvailable()
                    && projectService->project(projectId)) {
                    matches.append(projectService);
                }
            }
            if (matches.size() != 1) {
                return Utils::ResultError(
                    QStringLiteral("The EtherCAT project owner is unavailable or ambiguous."));
            }
            return matches.constFirst()->captureRuntimePackageActivationProject(projectId);
        };
        m_coordinator = std::make_unique<DurableRuntimePackageCompilerPreparationCoordinator>(
            providerRegistry, compilerRoot / "preparations", currentProjectCapture);
        ExtensionSystem::PluginManager::addObject(m_coordinator.get());
        m_coordinatorRegistered = true;
    }

#ifdef WITH_TESTS
    addTest<EtherCATProjectCompilerTests>();
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATProjectCompilerPlugin::aboutToShutdown()
{
    shutdown();
    return SynchronousShutdown;
}

void EtherCATProjectCompilerPlugin::shutdown()
{
    if (m_coordinator) {
        m_coordinator->shutdown();
        if (m_coordinatorRegistered) {
            ExtensionSystem::PluginManager::removeObject(m_coordinator.get());
            m_coordinatorRegistered = false;
        }
        m_coordinator.reset();
    }
    if (!m_provider)
        return;
    m_provider->shutdown();
    if (m_providerRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_provider.get());
        m_providerRegistered = false;
    }
    m_provider.reset();
}

} // namespace EtherCAT::ProjectCompiler::Internal

#include "ethercatprojectcompilerplugin.moc"
