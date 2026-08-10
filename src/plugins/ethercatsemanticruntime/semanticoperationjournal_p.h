// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/semanticruntimeservice.h>

#include <QDateTime>
#include <QHash>
#include <QList>

#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

enum class SemanticOperationJournalDisposition {
    Created,
    Replayed,
    Updated,
    Rejected,
    Conflict,
    NotFound,
    Unsupported,
    Stale,
};

struct SemanticOperationJournalResult
{
    SemanticOperationJournalDisposition disposition = SemanticOperationJournalDisposition::Rejected;
    std::optional<Data::SemanticOperationRecord> record;
    Core::SemanticRuntimeValidation validation;

    bool accepted() const
    {
        return disposition == SemanticOperationJournalDisposition::Created
               || disposition == SemanticOperationJournalDisposition::Replayed
               || disposition == SemanticOperationJournalDisposition::Updated;
    }

    bool changed() const
    {
        return disposition == SemanticOperationJournalDisposition::Created
               || disposition == SemanticOperationJournalDisposition::Updated;
    }
};

struct SemanticOperationJournalStateUpdate
{
    Data::SemanticOperationState expectedState = Data::SemanticOperationState::Rejected;
    Data::SemanticOperationState state = Data::SemanticOperationState::Rejected;
    Data::SemanticRuntimeActor actor;
    QString resultCode;
    QString detail;
    std::optional<Data::ControllerOperationError> controllerError;
    quint64 appliedCycle = 0;
    quint64 appliedRuntimeGeneration = 0;
};

struct SemanticOperationJournalStepUpdate
{
    Data::SemanticOperationState expectedState = Data::SemanticOperationState::Executing;
    Data::SemanticRuntimeActor actor;
    quint32 stepIndex = 0;
    quint32 totalSteps = 0;
    bool failed = false;
    QString detail;
    std::optional<Data::ControllerOperationError> controllerError;
};

struct SemanticOperationJournalCancellationUpdate
{
    Data::SemanticOperationState expectedState = Data::SemanticOperationState::Rejected;
    Data::SemanticOperationCancellationPhase expectedPhase
        = Data::SemanticOperationCancellationPhase::Requested;
    Data::SemanticOperationCancellationPhase phase
        = Data::SemanticOperationCancellationPhase::Requested;
    Data::SemanticRuntimeActor actor;
    QString resultCode;
    QString detail;
    std::optional<Data::RuntimeOutputTransactionState> safeHoldState;
    std::optional<Data::RuntimeOutputTransactionRequest> pendingApplyRequest;
    std::optional<Data::RuntimeOutputTransactionRequest> priorAppliedRequest;
    std::optional<Data::RuntimeOutputTransactionRequest> safeHoldRequest;
    std::optional<Data::RuntimeOutputTransactionResult> terminalApplyResult;
    quint64 appliedCycle = 0;
    quint64 appliedRuntimeGeneration = 0;
};

// This private journal is intentionally process-local and assumes every call is
// made on the Qt main thread. It records intent and execution evidence only; it
// never schedules work or calls a ControllerProvider.
class SemanticOperationJournal
{
public:
    SemanticOperationJournalResult submit(
        const Data::SemanticOperationRequest &request,
        const Data::SemanticRuntimeActor &actor,
        const Data::SemanticRuntimeContext &context,
        const QDateTime &occurredAt = {});
    SemanticOperationJournalResult approve(
        const Data::SemanticOperationApprovalRequest &approval,
        const Data::SemanticRuntimeActor &actor,
        const Data::SemanticRuntimeContext &context,
        const QDateTime &occurredAt = {});
    SemanticOperationJournalResult cancel(
        const Data::SemanticOperationCancelRequest &request,
        const Data::SemanticRuntimeActor &actor,
        const QDateTime &occurredAt = {});
    SemanticOperationJournalResult updateCancellation(
        const Data::SemanticOperationId &operationId,
        const SemanticOperationJournalCancellationUpdate &update,
        const QDateTime &occurredAt = {});

    SemanticOperationJournalResult transition(
        const Data::SemanticOperationId &operationId,
        const SemanticOperationJournalStateUpdate &update,
        const QDateTime &occurredAt = {});
    SemanticOperationJournalResult recordStep(
        const Data::SemanticOperationId &operationId,
        const SemanticOperationJournalStepUpdate &update,
        const QDateTime &occurredAt = {});
    SemanticOperationJournalResult recordBeforeSnapshot(
        const Data::SemanticOperationId &operationId,
        Data::SemanticOperationState expectedState,
        const Data::SemanticOperationSnapshot &snapshot,
        const Data::SemanticRuntimeActor &actor,
        const QDateTime &occurredAt = {});
    SemanticOperationJournalResult recordAfterSnapshot(
        const Data::SemanticOperationId &operationId,
        Data::SemanticOperationState expectedState,
        const Data::SemanticOperationSnapshot &snapshot,
        const Data::SemanticRuntimeActor &actor,
        const QDateTime &occurredAt = {});

    std::optional<Data::SemanticOperationRecord> operation(
        const Data::SemanticOperationId &operationId) const;
    QList<Data::SemanticRuntimeAuditEvent> audit(
        const QString &controllerId, quint64 afterSequence = 0) const;

    static QByteArray approvalChallenge(
        const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeContext &context);

private:
    void appendAudit(
        const Data::SemanticOperationRecord &record,
        Data::SemanticAuditEventKind kind,
        Data::SemanticOperationState previousState,
        const Data::SemanticRuntimeActor &actor,
        const QString &code,
        const QString &detail,
        const QDateTime &occurredAt,
        quint32 stepIndex = 0,
        const std::optional<Data::SemanticOperationCancelId> &attemptedCancelId = {},
        const QByteArray &attemptedCancelDigest = {},
        const QString &attemptedCancellationReason = {});

    QHash<Data::SemanticOperationId, Data::SemanticOperationRecord> m_operations;
    QHash<Data::SemanticOperationCancelId, Data::SemanticOperationId> m_cancellationsById;
    QHash<QString, QList<Data::SemanticRuntimeAuditEvent>> m_auditEvents;
    QHash<QString, quint64> m_lastAuditSequence;
};

} // namespace EtherCAT::SemanticRuntime::Internal
