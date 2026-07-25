// Copyright (C) 2026 Kvell

#pragma once

#include "automationdispatcher.h"

#include <mcp/server/mcpserver.h>

#include <utils/result.h>

#include <QHostAddress>
#include <QHttpServer>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QHttpServerRequest;
class QHttpServerResponse;
class QTcpServer;
QT_END_NAMESPACE

namespace EtherCAT::AutomationGateway::Internal {

class GatewayServer final
{
public:
    explicit GatewayServer(Core::AutomationService *service, bool publishToMcpManager = false);
    ~GatewayServer();

    Utils::Result<> start(const QHostAddress &address, quint16 mcpPort, quint16 restPort);
    void stop();

    bool isRunning() const;
    quint16 mcpPort() const;
    quint16 restPort() const;
    QStringList registeredToolNames() const;

private:
    void loadAndRegisterTools();
    void configureRestRoutes();
    QHttpServerResponse restDispatch(
        const QString &tool, QJsonObject arguments, const QHttpServerRequest &request);
    QHttpServerResponse restMutation(
        const QString &operation, QJsonObject arguments, const QHttpServerRequest &request);
    QHttpServerResponse restOpenApi(const QHttpServerRequest &request) const;
    static AutomationActor restActor(const QHttpServerRequest &request);

    AutomationDispatcher m_dispatcher;
    Mcp::Server m_mcpServer;
    QHttpServer m_restServer;
    QPointer<QTcpServer> m_restTcpServer;
    QStringList m_registeredToolNames;
    QString m_contractError;
    const bool m_publishToMcpManager;
    bool m_registeredWithMcpManager = false;
};

} // namespace EtherCAT::AutomationGateway::Internal
