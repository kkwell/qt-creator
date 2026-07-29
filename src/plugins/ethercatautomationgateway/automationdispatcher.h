// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/automationservice.h>
#include <ethercatcore/semanticruntimeservice.h>

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>

namespace EtherCAT::AutomationGateway::Internal {

struct AutomationActor
{
    QString transport;
    QString sessionId;
    QString clientName;
    QString clientVersion;
};

class AutomationDispatcher final
{
public:
    explicit AutomationDispatcher(
        Core::AutomationService *service,
        Core::SemanticRuntimeService *semanticRuntimeService = nullptr);

    QJsonObject dispatch(
        const QString &tool, const QJsonObject &arguments, const AutomationActor &actor);
    QJsonObject rejectMutation(
        const QString &operation, const QJsonObject &arguments, const AutomationActor &actor);

    static QStringList toolNames();
    static QJsonObject safeContextSnapshot(const Core::AutomationContextSnapshot &context);
    static QString snapshotHash(const Core::AutomationContextSnapshot &context);
    static QByteArray canonicalJson(const QJsonValue &value);

private:
    struct JournalEntry
    {
        QString requestHash;
        QJsonObject response;
    };

    QJsonObject dispatchUnjournaled(
        const QString &tool,
        const QJsonObject &arguments,
        const QString &operationId,
        const QString &requestHash,
        const AutomationActor &actor);
    QJsonObject makeSuccess(
        const QString &operationId,
        const QJsonObject &data,
        const QStringList &warnings,
        const QString &requestHash,
        const QString &beforeStateHash,
        const QString &afterStateHash,
        const AutomationActor &actor,
        const QString &auditDecision = QStringLiteral("allow-read-only")) const;
    QJsonObject makeError(
        const QString &operationId,
        const QString &code,
        const QString &message,
        const QString &path,
        const QString &recovery,
        const QJsonObject &details,
        const QString &requestHash,
        const AutomationActor &actor) const;
    std::optional<Core::AutomationContextSnapshot> currentContext(const QString &controllerId) const;
    QList<Core::AutomationContextSnapshot> mockContexts() const;
    std::optional<Data::SemanticRuntimeContext> semanticContext(const QString &controllerId) const;
    void remember(
        const QString &operationId, const QString &requestHash, const QJsonObject &response);

    QPointer<Core::AutomationService> m_service;
    QPointer<Core::SemanticRuntimeService> m_semanticRuntimeService;
    QHash<QString, JournalEntry> m_journal;
    QStringList m_journalOrder;
};

} // namespace EtherCAT::AutomationGateway::Internal
