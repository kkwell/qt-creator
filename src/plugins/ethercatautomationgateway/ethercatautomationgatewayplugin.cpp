// Copyright (C) 2026 Kvell

#include "ethercatautomationgatewayconstants.h"
#include "gatewayserver.h"
#ifdef WITH_TESTS
#include "ethercatautomationgatewaytests.h"
#endif

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <ethercatcore/automationservice.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/qtcsettings.h>

#include <QHostAddress>

#include <memory>

namespace EtherCAT::AutomationGateway::Internal {

class EtherCATAutomationGatewayPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATAutomationGateway.json")

public:
    void initialize() final
    {
        Core::AutomationService *automation
            = ExtensionSystem::PluginManager::getObject<Core::AutomationService>();
        if (!automation) {
            ::Core::MessageManager::writeDisrupting(
                "EtherCAT Automation Gateway has no IDE AutomationService.");
            return;
        }

        m_server = std::make_unique<GatewayServer>(automation, true);
        Utils::QtcSettings *settings = ::Core::ICore::settings();
        settings->beginGroup(Constants::SETTINGS_GROUP);
        const bool enabled = settings->value(Constants::SETTINGS_ENABLED, false).toBool();
        const quint16 mcpPort
            = settings->value(Constants::SETTINGS_MCP_PORT, Constants::DEFAULT_MCP_PORT)
                  .value<quint16>();
        const quint16 restPort
            = settings->value(Constants::SETTINGS_REST_PORT, Constants::DEFAULT_REST_PORT)
                  .value<quint16>();
        settings->endGroup();

        if (enabled) {
            const Utils::Result<> started
                = m_server->start(QHostAddress::LocalHost, mcpPort, restPort);
            if (!started) {
                ::Core::MessageManager::writeDisrupting(
                    QString("EtherCAT Automation Gateway did not start: %1").arg(started.error()));
            }
        }

#ifdef WITH_TESTS
        addTest<EtherCATAutomationGatewayTests>();
#endif
    }

    ShutdownFlag aboutToShutdown() final
    {
        m_server.reset();
        return SynchronousShutdown;
    }

private:
    std::unique_ptr<GatewayServer> m_server;
};

} // namespace EtherCAT::AutomationGateway::Internal

#include "ethercatautomationgatewayplugin.moc"
