// Copyright (C) 2026 Kvell

#pragma once

#include <utils/result.h>

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

namespace Utils {
class QtcSettings;
}

namespace EtherCAT::Core {
class AutomationService;
class SemanticRuntimeService;
} // namespace EtherCAT::Core

namespace EtherCAT::AutomationGateway::Internal {

class GatewayServer;

struct GatewayConfiguration
{
    bool enabled = false;
    int mcpPort = 0;
    int restPort = 0;

    bool operator==(const GatewayConfiguration &) const = default;
};

enum class GatewayRuntimeState {
    Unavailable,
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
};

enum class GatewayRuntimeEventKind {
    Started,
    Stopped,
    Failed,
};

struct GatewayRuntimeEvent
{
    GatewayRuntimeEventKind kind = GatewayRuntimeEventKind::Failed;
    QString message;
};

struct GatewayRuntimeSnapshot
{
    GatewayRuntimeState state = GatewayRuntimeState::Unavailable;
    GatewayConfiguration configuration;
    QUrl mcpEndpoint;
    QUrl restEndpoint;
    QString lastError;
    bool mockOnly = true;
    bool readOnly = true;
};

class GatewayRuntimeController final : public QObject
{
    Q_OBJECT

public:
    GatewayRuntimeController(
        Core::AutomationService *service,
        Core::SemanticRuntimeService *semanticRuntimeService,
        Utils::QtcSettings *settings,
        bool publishToMcpManager,
        QObject *parent = nullptr);
    ~GatewayRuntimeController() final;

    GatewayConfiguration configuration() const;
    GatewayRuntimeSnapshot snapshot() const;

    Utils::Result<> startFromStoredConfiguration();
    Utils::Result<> applyConfiguration(const GatewayConfiguration &candidate);
    void shutdown();

    static Utils::Result<> validateConfiguration(const GatewayConfiguration &candidate);

signals:
    void snapshotChanged(const GatewayRuntimeSnapshot &snapshot);
    void eventOccurred(const GatewayRuntimeEvent &event);

private:
    Utils::Result<GatewayConfiguration> readStoredConfiguration() const;
    Utils::Result<> persistConfiguration(const GatewayConfiguration &configuration);
    Utils::Result<> startAndPersist(const GatewayConfiguration &candidate, bool persist);
    Utils::Result<> failStart(
        const GatewayConfiguration &candidate, const QString &message, bool persistDisabled);
    void setState(GatewayRuntimeState state, const QString &lastError = {});
    void emitEvent(GatewayRuntimeEventKind kind, const QString &message);

    Utils::QtcSettings *const m_settings;
    std::unique_ptr<GatewayServer> m_server;
    GatewayConfiguration m_configuration;
    GatewayRuntimeState m_state = GatewayRuntimeState::Unavailable;
    QString m_lastError;
};

} // namespace EtherCAT::AutomationGateway::Internal

Q_DECLARE_METATYPE(EtherCAT::AutomationGateway::Internal::GatewayRuntimeEvent)
Q_DECLARE_METATYPE(EtherCAT::AutomationGateway::Internal::GatewayRuntimeSnapshot)
