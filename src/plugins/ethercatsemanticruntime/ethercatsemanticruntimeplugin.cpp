// Copyright (C) 2026 Embed Labs

#include "semanticruntimeexecutor.h"
#include "runtimepackageevidencerepository_p.h"

#ifdef WITH_TESTS
#include "ethercatsemanticruntimetests.h"
#endif

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <coreplugin/icore.h>

#include <utils/qtcassert.h>

#include <memory>

namespace EtherCAT::SemanticRuntime::Internal {

class EtherCATSemanticRuntimePlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATSemanticRuntime.json")

public:
    ~EtherCATSemanticRuntimePlugin() final;

    void initialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void shutdown();

    std::unique_ptr<SemanticRuntimeExecutor> m_runtime;
    std::shared_ptr<RuntimePackageEvidenceRepository> m_evidenceRepository;
    bool m_registered = false;
};

EtherCATSemanticRuntimePlugin::~EtherCATSemanticRuntimePlugin()
{
    shutdown();
}

void EtherCATSemanticRuntimePlugin::initialize()
{
    auto *projectService = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    auto *providerRegistry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QTC_ASSERT(projectService, return);
    QTC_ASSERT(providerRegistry, return);
    QTC_ASSERT(!ExtensionSystem::PluginManager::getObject<Core::SemanticRuntimeService>(), return);

    qRegisterMetaType<Data::SemanticOperationId>();
    qRegisterMetaType<Data::SemanticRuntimeContext>();

    const Utils::FilePath evidenceRoot
        = ::Core::ICore::userResourcePath("ethercat/runtime-evidence");
    const Utils::FilePath productionTrustDirectory
        = ::Core::ICore::resourcePath("ethercat/production-trust");
    m_evidenceRepository = std::make_shared<RuntimePackageEvidenceRepository>(
        (evidenceRoot / "verified-packages").toFSPathString(),
        productionTrustDirectory.toFSPathString(),
        (evidenceRoot / "compiled-projects").toFSPathString());
    m_runtime = std::make_unique<SemanticRuntimeExecutor>(
        projectService, providerRegistry, nullptr, m_evidenceRepository);
    ExtensionSystem::PluginManager::addObject(m_runtime.get());
    m_registered = true;

#ifdef WITH_TESTS
    addTest<EtherCATSemanticRuntimeTests>();
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATSemanticRuntimePlugin::aboutToShutdown()
{
    shutdown();
    return SynchronousShutdown;
}

void EtherCATSemanticRuntimePlugin::shutdown()
{
    if (m_registered) {
        ExtensionSystem::PluginManager::removeObject(m_runtime.get());
        m_registered = false;
    }
    m_runtime.reset();
    m_evidenceRepository.reset();
}

} // namespace EtherCAT::SemanticRuntime::Internal

#include "ethercatsemanticruntimeplugin.moc"
