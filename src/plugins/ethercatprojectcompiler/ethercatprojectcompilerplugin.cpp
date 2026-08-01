// Copyright (C) 2026 Embed Labs

#include "provisionedruntimepackagecompilerprovider.h"

#ifdef WITH_TESTS
#include "ethercatprojectcompilertests.h"
#endif

#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

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
    bool m_registered = false;
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
    m_registered = true;

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
    if (!m_provider)
        return;
    m_provider->shutdown();
    if (m_registered) {
        ExtensionSystem::PluginManager::removeObject(m_provider.get());
        m_registered = false;
    }
    m_provider.reset();
}

} // namespace EtherCAT::ProjectCompiler::Internal

#include "ethercatprojectcompilerplugin.moc"
