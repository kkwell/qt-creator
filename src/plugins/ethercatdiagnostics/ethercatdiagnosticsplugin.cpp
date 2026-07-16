// Copyright (C) 2026 Kvell

#include "diagnosticspropertypages.h"
#include "diagnosticsworkflow.h"
#ifdef WITH_TESTS
#include "ethercatdiagnosticstests.h"
#endif
#include "mockdiagnosticsprovider.h"

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <memory>

namespace EtherCAT::Diagnostics::Internal {

class EtherCATDiagnosticsPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATDiagnostics.json")

public:
    ~EtherCATDiagnosticsPlugin() final;

    void initialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void shutdown();

    std::unique_ptr<MockDiagnosticsProvider> m_diagnosticsProvider;
    std::unique_ptr<DiagnosticsWorkflow> m_workflow;
    std::unique_ptr<DiagnosticsPropertyPageProvider> m_pageProvider;
    bool m_diagnosticsProviderRegistered = false;
    bool m_pageProviderRegistered = false;
    bool m_shuttingDown = false;
};

EtherCATDiagnosticsPlugin::~EtherCATDiagnosticsPlugin()
{
    shutdown();
}

void EtherCATDiagnosticsPlugin::initialize()
{
    m_diagnosticsProvider = std::make_unique<MockDiagnosticsProvider>();
    ExtensionSystem::PluginManager::addObject(m_diagnosticsProvider.get());
    m_diagnosticsProviderRegistered = true;

    m_workflow = std::make_unique<DiagnosticsWorkflow>(m_diagnosticsProvider.get());
    m_workflow->setupActions();

    m_pageProvider = std::make_unique<DiagnosticsPropertyPageProvider>(
        m_diagnosticsProvider.get(), m_workflow.get());
    ExtensionSystem::PluginManager::addObject(m_pageProvider.get());
    m_pageProviderRegistered = true;

#ifdef WITH_TESTS
    addTest<EtherCATDiagnosticsTests>();
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATDiagnosticsPlugin::aboutToShutdown()
{
    shutdown();
    return SynchronousShutdown;
}

void EtherCATDiagnosticsPlugin::shutdown()
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
    if (m_diagnosticsProvider)
        m_diagnosticsProvider->shutdown();
    if (m_diagnosticsProviderRegistered) {
        ExtensionSystem::PluginManager::removeObject(m_diagnosticsProvider.get());
        m_diagnosticsProviderRegistered = false;
    }
    m_diagnosticsProvider.reset();
}

} // namespace EtherCAT::Diagnostics::Internal

#include "ethercatdiagnosticsplugin.moc"
