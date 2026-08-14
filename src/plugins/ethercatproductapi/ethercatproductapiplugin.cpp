// Copyright (C) 2026 Kvell

#include "productapiconnectionprovider.h"

#ifdef WITH_TESTS
#include "ethercatproductapitests.h"
#endif

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <memory>

namespace EtherCAT::ProductApi::Internal {

class EtherCATProductApiPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATProductApi.json")

public:
    ~EtherCATProductApiPlugin() final;

    void initialize() final
    {
        m_connectionProvider = std::make_unique<ProductApiConnectionProvider>();
        ExtensionSystem::PluginManager::addObject(m_connectionProvider.get());
        m_providerRegistered = true;

#ifdef WITH_TESTS
        addTest<EtherCATProductApiTests>();
#endif
    }

    ShutdownFlag aboutToShutdown() final;

private:
    void shutdown();

    std::unique_ptr<ProductApiConnectionProvider> m_connectionProvider;
    bool m_providerRegistered = false;
    bool m_shuttingDown = false;
};

EtherCATProductApiPlugin::~EtherCATProductApiPlugin()
{
    shutdown();
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATProductApiPlugin::aboutToShutdown()
{
    shutdown();
    return SynchronousShutdown;
}

void EtherCATProductApiPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (m_connectionProvider)
        m_connectionProvider->shutdown();
    if (m_providerRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_connectionProvider.get());
        m_providerRegistered = false;
    }
    m_connectionProvider.reset();
}

} // namespace EtherCAT::ProductApi::Internal

#include "ethercatproductapiplugin.moc"
