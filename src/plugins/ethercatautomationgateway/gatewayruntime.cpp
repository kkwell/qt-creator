// Copyright (C) 2026 Kvell

#include "gatewayruntime.h"

#include "ethercatautomationgatewayconstants.h"
#include "ethercatautomationgatewaytr.h"
#include "gatewayserver.h"

#include <utils/qtcsettings.h>

#include <QHostAddress>
#include <QSettings>

namespace EtherCAT::AutomationGateway::Internal {

using namespace EtherCAT::AutomationGateway::Constants;

GatewayRuntimeController::GatewayRuntimeController(
    Core::AutomationService *service,
    Utils::QtcSettings *settings,
    bool publishToMcpManager,
    QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_server(service ? std::make_unique<GatewayServer>(service, publishToMcpManager) : nullptr)
    , m_configuration{false, DEFAULT_MCP_PORT, DEFAULT_REST_PORT}
    , m_state(service ? GatewayRuntimeState::Stopped : GatewayRuntimeState::Unavailable)
{}

GatewayRuntimeController::~GatewayRuntimeController()
{
    shutdown();
}

GatewayConfiguration GatewayRuntimeController::configuration() const
{
    return m_configuration;
}

GatewayRuntimeSnapshot GatewayRuntimeController::snapshot() const
{
    GatewayRuntimeSnapshot result;
    result.state = m_state;
    result.configuration = m_configuration;
    result.lastError = m_lastError;
    if (m_server && m_server->isRunning()) {
        result.mcpEndpoint = m_server->mcpEndpoint();
        result.restEndpoint = m_server->restEndpoint();
    }
    return result;
}

Utils::Result<> GatewayRuntimeController::validateConfiguration(
    const GatewayConfiguration &candidate)
{
    if (candidate.mcpPort < 0 || candidate.mcpPort > 65535) {
        return Utils::ResultError(
            Tr::tr("The MCP port must be between 0 and 65535."));
    }
    if (candidate.restPort < 0 || candidate.restPort > 65535) {
        return Utils::ResultError(
            Tr::tr("The REST port must be between 0 and 65535."));
    }
    if (candidate.mcpPort != 0 && candidate.mcpPort == candidate.restPort) {
        return Utils::ResultError(
            Tr::tr("MCP and REST ports must differ when both are non-zero."));
    }
    return Utils::ResultOk;
}

Utils::Result<GatewayConfiguration> GatewayRuntimeController::readStoredConfiguration() const
{
    if (!m_settings)
        return Utils::ResultError(Tr::tr("IDE settings are unavailable."));

    m_settings->beginGroup(SETTINGS_GROUP);
    const bool enabled = m_settings->value(SETTINGS_ENABLED, false).toBool();
    bool mcpOk = false;
    bool restOk = false;
    const int mcpPort = m_settings->value(SETTINGS_MCP_PORT, DEFAULT_MCP_PORT).toInt(&mcpOk);
    const int restPort = m_settings->value(SETTINGS_REST_PORT, DEFAULT_REST_PORT).toInt(&restOk);
    m_settings->endGroup();

    if (!mcpOk || !restOk) {
        return Utils::ResultError(
            Tr::tr("The stored Gateway port settings are invalid."));
    }

    const GatewayConfiguration configuration{enabled, mcpPort, restPort};
    const Utils::Result<> valid = validateConfiguration(configuration);
    if (!valid)
        return Utils::ResultError(valid.error());
    return configuration;
}

Utils::Result<> GatewayRuntimeController::persistConfiguration(
    const GatewayConfiguration &configuration)
{
    if (!m_settings)
        return Utils::ResultError(Tr::tr("IDE settings are unavailable."));

    m_settings->beginGroup(SETTINGS_GROUP);
    m_settings->setValue(SETTINGS_MCP_PORT, configuration.mcpPort);
    m_settings->setValue(SETTINGS_REST_PORT, configuration.restPort);
    m_settings->setValue(SETTINGS_ENABLED, configuration.enabled);
    m_settings->endGroup();
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        return Utils::ResultError(
            Tr::tr("The Gateway settings could not be saved."));
    }
    return Utils::ResultOk;
}

Utils::Result<> GatewayRuntimeController::startFromStoredConfiguration()
{
    const Utils::Result<GatewayConfiguration> stored = readStoredConfiguration();
    if (!stored) {
        const GatewayConfiguration safe{false, DEFAULT_MCP_PORT, DEFAULT_REST_PORT};
        m_configuration = safe;
        const Utils::Result<> persisted = persistConfiguration(safe);
        QString message = stored.error();
        if (!persisted)
            message += QLatin1Char(' ')
                       + Tr::tr("Safe settings could not be persisted: %1")
                             .arg(persisted.error());
        setState(GatewayRuntimeState::Failed, message);
        emitEvent(GatewayRuntimeEventKind::Failed, message);
        return Utils::ResultError(message);
    }

    if (stored->enabled && m_server && m_server->isRunning()) {
        if (m_configuration == *stored) {
            setState(GatewayRuntimeState::Running);
            return Utils::ResultOk;
        }
        return Utils::ResultError(
            Tr::tr("Disable the running Gateway before changing its ports."));
    }

    m_configuration = *stored;
    if (!stored->enabled) {
        if (m_server)
            m_server->stop();
        setState(m_server ? GatewayRuntimeState::Stopped : GatewayRuntimeState::Unavailable);
        return Utils::ResultOk;
    }
    return startAndPersist(*stored, false);
}

Utils::Result<> GatewayRuntimeController::applyConfiguration(
    const GatewayConfiguration &candidate)
{
    const Utils::Result<> valid = validateConfiguration(candidate);
    if (!valid) {
        if (!m_server || !m_server->isRunning()) {
            GatewayConfiguration safe = m_configuration;
            safe.enabled = false;
            m_configuration = safe;
            persistConfiguration(safe);
            setState(m_server ? GatewayRuntimeState::Failed : GatewayRuntimeState::Unavailable,
                     valid.error());
        } else {
            m_lastError = valid.error();
            emit snapshotChanged(snapshot());
        }
        emitEvent(GatewayRuntimeEventKind::Failed, valid.error());
        return valid;
    }

    if (!m_server) {
        const QString message = Tr::tr("The IDE AutomationService is unavailable.");
        GatewayConfiguration safe = candidate;
        safe.enabled = false;
        m_configuration = safe;
        persistConfiguration(safe);
        setState(GatewayRuntimeState::Unavailable, message);
        emitEvent(GatewayRuntimeEventKind::Failed, message);
        return Utils::ResultError(message);
    }

    if (!candidate.enabled) {
        const bool wasRunning = m_server->isRunning();
        if (wasRunning)
            setState(GatewayRuntimeState::Stopping);
        m_server->stop();
        GatewayConfiguration safe = candidate;
        safe.enabled = false;
        const Utils::Result<> persisted = persistConfiguration(safe);
        m_configuration = safe;
        if (!persisted) {
            setState(GatewayRuntimeState::Failed, persisted.error());
            emitEvent(GatewayRuntimeEventKind::Failed, persisted.error());
            return persisted;
        }
        setState(GatewayRuntimeState::Stopped);
        if (wasRunning) {
            emitEvent(
                GatewayRuntimeEventKind::Stopped,
                Tr::tr("Stopped; both loopback listeners were released."));
        }
        return Utils::ResultOk;
    }

    if (m_server->isRunning()) {
        if (candidate == m_configuration) {
            m_lastError.clear();
            emit snapshotChanged(snapshot());
            return Utils::ResultOk;
        }
        const QString message
            = Tr::tr("Disable the running Gateway before changing its ports.");
        m_lastError = message;
        emit snapshotChanged(snapshot());
        emitEvent(GatewayRuntimeEventKind::Failed, message);
        return Utils::ResultError(message);
    }

    return startAndPersist(candidate, true);
}

Utils::Result<> GatewayRuntimeController::startAndPersist(
    const GatewayConfiguration &candidate, bool persist)
{
    if (!m_server) {
        return failStart(
            candidate, Tr::tr("The IDE AutomationService is unavailable."), true);
    }

    setState(GatewayRuntimeState::Starting);
    const Utils::Result<> started = m_server->start(
        QHostAddress::LocalHost,
        static_cast<quint16>(candidate.mcpPort),
        static_cast<quint16>(candidate.restPort));
    if (!started) {
        return failStart(
            candidate,
            Tr::tr("Could not start the loopback listeners: %1").arg(started.error()),
            true);
    }

    if (persist) {
        const Utils::Result<> persisted = persistConfiguration(candidate);
        if (!persisted) {
            m_server->stop();
            return failStart(
                candidate,
                Tr::tr("The listeners were stopped because settings could not be saved: %1")
                    .arg(persisted.error()),
                true);
        }
    }

    m_configuration = candidate;
    setState(GatewayRuntimeState::Running);
    emitEvent(
        GatewayRuntimeEventKind::Started,
        Tr::tr("Listening on MCP %1 and REST %2 (Mock-only, read-only).")
            .arg(m_server->mcpEndpoint().toString(), m_server->restEndpoint().toString()));
    return Utils::ResultOk;
}

Utils::Result<> GatewayRuntimeController::failStart(
    const GatewayConfiguration &candidate, const QString &message, bool persistDisabled)
{
    if (m_server)
        m_server->stop();
    GatewayConfiguration safe = candidate;
    safe.enabled = false;
    m_configuration = safe;
    QString reported = message;
    if (persistDisabled) {
        const Utils::Result<> persisted = persistConfiguration(safe);
        if (!persisted) {
            reported += QLatin1Char(' ')
                        + Tr::tr("Safe disabled state could not be persisted: %1")
                              .arg(persisted.error());
        }
    }
    setState(m_server ? GatewayRuntimeState::Failed : GatewayRuntimeState::Unavailable, reported);
    emitEvent(GatewayRuntimeEventKind::Failed, reported);
    return Utils::ResultError(reported);
}

void GatewayRuntimeController::shutdown()
{
    if (!m_server || !m_server->isRunning())
        return;
    setState(GatewayRuntimeState::Stopping);
    m_server->stop();
    setState(GatewayRuntimeState::Stopped);
    emitEvent(
        GatewayRuntimeEventKind::Stopped,
        Tr::tr("Stopped; both loopback listeners were released."));
}

void GatewayRuntimeController::setState(GatewayRuntimeState state, const QString &lastError)
{
    m_state = state;
    m_lastError = lastError;
    emit snapshotChanged(snapshot());
}

void GatewayRuntimeController::emitEvent(
    GatewayRuntimeEventKind kind, const QString &message)
{
    emit eventOccurred({kind, message});
}

} // namespace EtherCAT::AutomationGateway::Internal
