// Copyright (C) 2026 Kvell

#include "gatewayserver.h"

#include "ethercatautomationgatewayconstants.h"

#include <coreplugin/mcp/mcpmanager.h>

#include <mcp/schemas/schema_2025_11_25.h>

#include <QFile>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace EtherCAT::AutomationGateway::Internal {

using namespace EtherCAT::AutomationGateway::Constants;

static Mcp::Schema::Implementation serverImplementation()
{
    return Mcp::Schema::Implementation()
        .name("ethercat-automation-gateway")
        .title("EtherCAT Automation Gateway")
        .version("1.0.0")
        .description("IDE-owned, loopback-only, mock-only controller-tools-v1 server");
}

static QHttpServerResponse jsonResponse(
    const QJsonObject &body, QHttpServerResponse::StatusCode status)
{
    return QHttpServerResponse(
        "application/json", QJsonDocument(body).toJson(QJsonDocument::Compact), status);
}

static QHttpServerResponse::StatusCode responseStatus(const QJsonObject &response)
{
    if (response.value("ok").toBool())
        return QHttpServerResponse::StatusCode::Ok;
    const QString code = response.value("error").toObject().value("code").toString();
    if (code == "CT008_READ_ONLY")
        return QHttpServerResponse::StatusCode::Forbidden;
    if (code == "CT009_NOT_FOUND")
        return QHttpServerResponse::StatusCode::NotFound;
    if (response.value("error").toObject().value("details").toObject().value("reason").toString()
        == "operation-id-conflict") {
        return QHttpServerResponse::StatusCode::Conflict;
    }
    return QHttpServerResponse::StatusCode::BadRequest;
}

static bool originIsLoopback(const QHttpServerRequest &request)
{
    if (!request.headers().contains("Origin"))
        return true;
    const QUrl origin(QString::fromUtf8(request.headers().value("Origin")));
    if (!origin.isValid() || origin.host().isEmpty())
        return false;
    const QHostAddress address(origin.host());
    return origin.host().compare("localhost", Qt::CaseInsensitive) == 0 || address.isLoopback();
}

static QJsonObject originRejected()
{
    return {
        {"apiVersion", API_VERSION},
        {"operationId", ""},
        {"ok", false},
        {"data", QJsonObject{}},
        {"warnings", QJsonArray{}},
        {"audit",
         QJsonObject{
             {"decision", "deny"},
             {"reason", "origin-not-loopback"},
         }},
        {"error",
         QJsonObject{
             {"code", "CT012_AUTH_NOT_IMPLEMENTED"},
             {"message", "A non-loopback Origin is not allowed."},
             {"path", "$.headers.Origin"},
             {"recovery",
              "Call the service from a loopback client; remote exposure is unsupported."},
         }},
    };
}

static QJsonObject argumentsFromQuery(const QHttpServerRequest &request)
{
    QJsonObject result;
    const QString operationId = request.query().queryItemValue("operationId");
    if (!operationId.isEmpty())
        result.insert("operationId", operationId);
    return result;
}

static QJsonObject bodyObject(const QHttpServerRequest &request)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(request.body(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

GatewayServer::GatewayServer(Core::AutomationService *service, bool publishToMcpManager)
    : m_dispatcher(service)
    , m_mcpServer(serverImplementation())
    , m_publishToMcpManager(publishToMcpManager)
{
    loadAndRegisterTools();
    configureRestRoutes();
}

GatewayServer::~GatewayServer()
{
    stop();
}

void GatewayServer::loadAndRegisterTools()
{
    QFile contract(MCP_RESOURCE_PATH);
    if (!contract.open(QIODevice::ReadOnly)) {
        m_contractError = QString("Cannot open %1").arg(MCP_RESOURCE_PATH);
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contract.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_contractError = "controller-tools-v1 MCP catalog is invalid JSON";
        return;
    }

    const QJsonArray tools = document.object().value("tools").toArray();
    QStringList parsedNames;
    QList<Mcp::Schema::Tool> parsedTools;
    for (const QJsonValue &value : tools) {
        const Utils::Result<Mcp::Schema::Tool> parsed = Mcp::Schema::fromJson<Mcp::Schema::Tool>(
            value);
        if (!parsed) {
            m_contractError = QString("Invalid MCP tool contract: %1").arg(parsed.error());
            return;
        }
        parsedNames.append(parsed->name());
        parsedTools.append(*parsed);
    }
    parsedNames.sort();
    const QStringList expected = AutomationDispatcher::toolNames();
    if (parsedNames != expected) {
        m_contractError
            = "The embedded MCP catalog does not match the closed controller-tools-v1 set";
        return;
    }

    for (const Mcp::Schema::Tool &tool : parsedTools) {
        m_mcpServer.addTool(
            tool,
            [this](
                const Mcp::Schema::CallToolRequestParams &params,
                const Mcp::ToolInterface &toolInterface) {
                const Mcp::Schema::Implementation &client = toolInterface.clientInfo();
                const AutomationActor actor{
                    "mcp",
                    toolInterface.sessionId(),
                    client.name(),
                    client.version(),
                };
                const QJsonObject response
                    = m_dispatcher.dispatch(params.name(), params.argumentsAsObject(), actor);
                toolInterface.finish(Mcp::Schema::CallToolResult()
                                         .isError(!response.value("ok").toBool())
                                         .structuredContent(response));
                return Utils::ResultOk;
            });
        m_registeredToolNames.append(tool.name());
    }
    m_registeredToolNames.sort();
}

void GatewayServer::configureRestRoutes()
{
    m_restServer.route(
        "/api/controller-tools/v1/protocol",
        QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &request) {
            return restDispatch("gateway.get-protocol", argumentsFromQuery(request), request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers",
        QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &request) {
            return restDispatch("controller.list", argumentsFromQuery(request), request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/adapters",
        QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &request) {
            return restDispatch("adapter.list", argumentsFromQuery(request), request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/openapi.json",
        QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &request) { return restOpenApi(request); });
    m_restServer.route(
        "/api/controller-tools/v1/artifacts/validate",
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest &request) {
            return restDispatch("artifact.validate", bodyObject(request), request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/capabilities",
        QHttpServerRequest::Method::Get,
        [this](const QString &controllerId, const QHttpServerRequest &request) {
            QJsonObject arguments = argumentsFromQuery(request);
            arguments.insert("controllerId", controllerId);
            return restDispatch("controller.get-capabilities", arguments, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/state",
        QHttpServerRequest::Method::Get,
        [this](const QString &controllerId, const QHttpServerRequest &request) {
            QJsonObject arguments = argumentsFromQuery(request);
            arguments.insert("controllerId", controllerId);
            return restDispatch("controller.get-state", arguments, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/topology",
        QHttpServerRequest::Method::Get,
        [this](const QString &controllerId, const QHttpServerRequest &request) {
            QJsonObject arguments = argumentsFromQuery(request);
            arguments.insert("controllerId", controllerId);
            return restDispatch("controller.get-topology", arguments, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/diagnostics",
        QHttpServerRequest::Method::Get,
        [this](const QString &controllerId, const QHttpServerRequest &request) {
            QJsonObject arguments = argumentsFromQuery(request);
            arguments.insert("controllerId", controllerId);
            return restDispatch("controller.get-diagnostics", arguments, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/devices/<arg>",
        QHttpServerRequest::Method::Get,
        [this](const QString &controllerId, int position, const QHttpServerRequest &request) {
            QJsonObject arguments = argumentsFromQuery(request);
            arguments.insert("controllerId", controllerId);
            arguments.insert("position", position);
            return restDispatch("controller.get-device", arguments, request);
        });

    const auto mutation = [this](
                              const QString &operation,
                              const QString &controllerId,
                              const QHttpServerRequest &request) {
        QJsonObject arguments = bodyObject(request);
        arguments.insert("controllerId", controllerId);
        return restMutation(operation, arguments, request);
    };
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/connect",
        QHttpServerRequest::Method::Post,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.connect", controllerId, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/lease",
        QHttpServerRequest::Method::Post,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.lease.acquire", controllerId, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/scan",
        QHttpServerRequest::Method::Post,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.scan", controllerId, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/configuration",
        QHttpServerRequest::Method::Post,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.configuration.apply", controllerId, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/deploy",
        QHttpServerRequest::Method::Post,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.deploy", controllerId, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/motion",
        QHttpServerRequest::Method::Post,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.motion", controllerId, request);
        });
    m_restServer.route(
        "/api/controller-tools/v1/controllers/<arg>/ip",
        QHttpServerRequest::Method::Put,
        [mutation](const QString &controllerId, const QHttpServerRequest &request) {
            return mutation("controller.ip.configure", controllerId, request);
        });
}

AutomationActor GatewayServer::restActor(const QHttpServerRequest &request)
{
    return {
        "rest",
        QString::fromUtf8(request.headers().value("x-session-id")),
        QString::fromUtf8(request.headers().value("user-agent")),
        {},
    };
}

QHttpServerResponse GatewayServer::restDispatch(
    const QString &tool, QJsonObject arguments, const QHttpServerRequest &request)
{
    if (!originIsLoopback(request)) {
        return jsonResponse(originRejected(), QHttpServerResponse::StatusCode::Forbidden);
    }
    const QJsonObject response = m_dispatcher.dispatch(tool, arguments, restActor(request));
    return jsonResponse(response, responseStatus(response));
}

QHttpServerResponse GatewayServer::restMutation(
    const QString &operation, QJsonObject arguments, const QHttpServerRequest &request)
{
    if (!originIsLoopback(request)) {
        return jsonResponse(originRejected(), QHttpServerResponse::StatusCode::Forbidden);
    }
    const QJsonObject response
        = m_dispatcher.rejectMutation(operation, arguments, restActor(request));
    return jsonResponse(response, responseStatus(response));
}

QHttpServerResponse GatewayServer::restOpenApi(const QHttpServerRequest &request) const
{
    if (!originIsLoopback(request)) {
        return jsonResponse(originRejected(), QHttpServerResponse::StatusCode::Forbidden);
    }
    QFile file(OPENAPI_RESOURCE_PATH);
    if (!file.open(QIODevice::ReadOnly)) {
        return QHttpServerResponse(
            "text/plain",
            "OpenAPI contract unavailable",
            QHttpServerResponse::StatusCode::InternalServerError);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QHttpServerResponse(
            "text/plain",
            "OpenAPI contract is invalid",
            QHttpServerResponse::StatusCode::InternalServerError);
    }
    QJsonObject openApi = document.object();
    openApi.insert(
        "servers",
        QJsonArray{
            QJsonObject{
                {"url", QString("http://127.0.0.1:%1").arg(restPort())},
                {"description", "IDE loopback REST listener"},
            },
        });
    QJsonObject paths = openApi.value("paths").toObject();
    paths.remove("/mcp");
    openApi.insert("paths", paths);
    openApi.insert(
        "x-mcp-streamable-http",
        QJsonObject{
            {"url", QString("http://127.0.0.1:%1/").arg(mcpPort())},
            {"protocolVersion", MCP_PROTOCOL_VERSION},
        });
    return QHttpServerResponse(
        "application/json",
        QJsonDocument(openApi).toJson(QJsonDocument::Compact),
        QHttpServerResponse::StatusCode::Ok);
}

Utils::Result<> GatewayServer::start(const QHostAddress &address, quint16 mcpPort, quint16 restPort)
{
    if (isRunning())
        return Utils::ResultError("EtherCAT Automation Gateway is already running");
    if (!address.isLoopback())
        return Utils::ResultError("EtherCAT Automation Gateway only binds to loopback");
    if (mcpPort != 0 && mcpPort == restPort)
        return Utils::ResultError("MCP and REST ports must differ when both are non-zero");
    if (!m_contractError.isEmpty())
        return Utils::ResultError(m_contractError);

    auto mcpTcp = new QTcpServer;
    if (!mcpTcp->listen(address, mcpPort)) {
        const QString error = mcpTcp->errorString();
        delete mcpTcp;
        return Utils::ResultError(
            QString("Cannot bind the MCP loopback listener: %1").arg(error));
    }
    if (!m_mcpServer.bind(mcpTcp)) {
        delete mcpTcp;
        return Utils::ResultError("Cannot register the MCP loopback listener");
    }

    auto restTcp = new QTcpServer;
    if (!restTcp->listen(address, restPort)) {
        const QString error = restTcp->errorString();
        delete restTcp;
        stop();
        return Utils::ResultError(
            QString("Cannot bind the REST loopback listener: %1").arg(error));
    }
    if (!m_restServer.bind(restTcp)) {
        delete restTcp;
        stop();
        return Utils::ResultError("Cannot register the REST loopback listener");
    }
    m_restTcpServer = restTcp;

    if (m_publishToMcpManager) {
        const QUrl url(QString("http://127.0.0.1:%1").arg(this->mcpPort()));
        const ::Core::McpManager::ServerInfo info{
            MCP_MANAGER_ID,
            "EtherCAT controller-tools-v1",
            ::Core::McpManager::ConnectionType::Streamable_Http,
            url,
            {},
            {},
        };
        if (!::Core::McpManager::registerMcpServer(info)) {
            stop();
            return Utils::ResultError("Cannot publish the Gateway in the IDE MCP registry");
        }
        m_registeredWithMcpManager = true;
    }
    return Utils::ResultOk;
}

void GatewayServer::stop()
{
    if (m_registeredWithMcpManager) {
        ::Core::McpManager::removeMcpServer(MCP_MANAGER_ID);
        m_registeredWithMcpManager = false;
    }
    if (m_restTcpServer) {
        m_restTcpServer->close();
        delete m_restTcpServer;
        m_restTcpServer = nullptr;
    }
    const QList<QTcpServer *> servers = m_mcpServer.boundTcpServers();
    for (QTcpServer *server : servers) {
        server->close();
        delete server;
    }
}

bool GatewayServer::isRunning() const
{
    return !m_mcpServer.boundTcpServers().isEmpty() && m_restTcpServer
           && m_restTcpServer->isListening();
}

quint16 GatewayServer::mcpPort() const
{
    const QList<QTcpServer *> servers = m_mcpServer.boundTcpServers();
    return servers.isEmpty() ? 0 : servers.constFirst()->serverPort();
}

quint16 GatewayServer::restPort() const
{
    return m_restTcpServer ? m_restTcpServer->serverPort() : 0;
}

QUrl GatewayServer::mcpEndpoint() const
{
    return mcpPort() == 0 ? QUrl{}
                          : QUrl(QString("http://127.0.0.1:%1/").arg(mcpPort()));
}

QUrl GatewayServer::restEndpoint() const
{
    return restPort() == 0 ? QUrl{}
                            : QUrl(QString("http://127.0.0.1:%1").arg(restPort()));
}

QStringList GatewayServer::registeredToolNames() const
{
    return m_registeredToolNames;
}

} // namespace EtherCAT::AutomationGateway::Internal
