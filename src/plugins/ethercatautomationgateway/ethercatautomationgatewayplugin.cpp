// Copyright (C) 2026 Kvell

#include "ethercatautomationgatewayconstants.h"
#include "ethercatautomationgatewaytr.h"
#include "gatewayruntime.h"
#include "gatewaysettingspage.h"
#ifdef WITH_TESTS
#include "ethercatautomationgatewaytests.h"
#endif

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <ethercatcore/automationservice.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/projectexplorer.h>

#include <utils/id.h>
#include <utils/outputformat.h>

#include <memory>

namespace EtherCAT::AutomationGateway::Internal {

static void writeGatewayEvent(const GatewayRuntimeEvent &event)
{
    const Utils::Id channelId(Constants::CONTROLLER_OUTPUT_CHANNEL_ID);
    const Utils::OutputFormat format = event.kind == GatewayRuntimeEventKind::Failed
                                           ? Utils::ErrorMessageFormat
                                           : Utils::NormalMessageFormat;
    const QString message = QString("[AI Gateway] %1\n").arg(event.message);
    ProjectExplorer::ProjectExplorerPlugin::postApplicationOutput(
        channelId, Tr::tr("Output"), message, format);
    if (event.kind == GatewayRuntimeEventKind::Failed)
        ProjectExplorer::ProjectExplorerPlugin::showApplicationOutput(channelId);
}

class EtherCATAutomationGatewayPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATAutomationGateway.json")

public:
    void initialize() final
    {
        Core::AutomationService *automation
            = ExtensionSystem::PluginManager::getObject<Core::AutomationService>();
        m_runtime = std::make_unique<GatewayRuntimeController>(
            automation, ::Core::ICore::settings(), true);
        connect(
            m_runtime.get(),
            &GatewayRuntimeController::eventOccurred,
            this,
            &writeGatewayEvent);
        m_settingsPage = createGatewaySettingsPage(m_runtime.get());
        m_runtime->startFromStoredConfiguration();

#ifdef WITH_TESTS
        addTest<EtherCATAutomationGatewayTests>();
#endif
    }

    ShutdownFlag aboutToShutdown() final
    {
        m_settingsPage.reset();
        if (m_runtime)
            m_runtime->shutdown();
        m_runtime.reset();
        return SynchronousShutdown;
    }

private:
    std::unique_ptr<GatewayRuntimeController> m_runtime;
    std::unique_ptr<::Core::IOptionsPage> m_settingsPage;
};

} // namespace EtherCAT::AutomationGateway::Internal

#include "ethercatautomationgatewayplugin.moc"
