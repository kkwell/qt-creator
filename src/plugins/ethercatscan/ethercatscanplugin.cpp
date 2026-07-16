// Copyright (C) 2026 Kvell

#include "mockscanprovider.h"
#include "scanpropertypages.h"
#include "scanworkflow.h"
#ifdef WITH_TESTS
#include "ethercatscantests.h"
#endif

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <memory>

namespace EtherCAT::Scan::Internal {

class EtherCATScanPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATScan.json")

public:
    ~EtherCATScanPlugin() final;

    void initialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void shutdown();

    std::unique_ptr<MockScanProvider> m_scanProvider;
    std::unique_ptr<ScanWorkflow> m_workflow;
    std::unique_ptr<ScanPropertyPageProvider> m_pageProvider;
    bool m_scanProviderRegistered = false;
    bool m_pageProviderRegistered = false;
    bool m_shuttingDown = false;
};

EtherCATScanPlugin::~EtherCATScanPlugin()
{
    shutdown();
}

void EtherCATScanPlugin::initialize()
{
    m_scanProvider = std::make_unique<MockScanProvider>();
    ExtensionSystem::PluginManager::addObject(m_scanProvider.get());
    m_scanProviderRegistered = true;

    m_workflow = std::make_unique<ScanWorkflow>(m_scanProvider.get());
    m_workflow->setupActions();

    m_pageProvider = std::make_unique<ScanPropertyPageProvider>(
        m_scanProvider.get(), m_workflow.get());
    ExtensionSystem::PluginManager::addObject(m_pageProvider.get());
    m_pageProviderRegistered = true;

#ifdef WITH_TESTS
    addTest<EtherCATScanTests>();
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATScanPlugin::aboutToShutdown()
{
    shutdown();
    return SynchronousShutdown;
}

void EtherCATScanPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_pageProviderRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_pageProvider.get());
        m_pageProviderRegistered = false;
    }
    m_pageProvider.reset();
    if (m_workflow)
        m_workflow->shutdown();
    m_workflow.reset();
    if (m_scanProvider)
        m_scanProvider->shutdown();
    if (m_scanProviderRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_scanProvider.get());
        m_scanProviderRegistered = false;
    }
    m_scanProvider.reset();
}

} // namespace EtherCAT::Scan::Internal

#include "ethercatscanplugin.moc"
