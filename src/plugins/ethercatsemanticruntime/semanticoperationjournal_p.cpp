// Copyright (C) 2026 Embed Labs

#include "semanticoperationjournal_p.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <limits>

namespace EtherCAT::SemanticRuntime::Internal {
namespace {

using Disposition = SemanticOperationJournalDisposition;

QDateTime operationTime(const QDateTime &occurredAt)
{
    return occurredAt.isValid() ? occurredAt.toUTC() : QDateTime::currentDateTimeUtc();
}

Core::SemanticRuntimeValidation rejection(
    Core::SemanticRuntimeValidationError error, const QString &detail)
{
    return {error, detail};
}

QByteArray requestDigest(const Data::SemanticOperationRequest &request)
{
    const QByteArray canonical = Core::canonicalSemanticOperationRequest(request);
    if (canonical.isEmpty())
        return {};
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

bool requestsAreIdempotentReplays(
    const Data::SemanticOperationRequest &existing, const Data::SemanticOperationRequest &request)
{
    const QByteArray existingCanonical = Core::canonicalSemanticOperationRequest(existing);
    const QByteArray requestCanonical = Core::canonicalSemanticOperationRequest(request);
    if (!existingCanonical.isEmpty() || !requestCanonical.isEmpty())
        return !existingCanonical.isEmpty() && existingCanonical == requestCanonical;
    return existing == request;
}

SemanticOperationJournalResult result(
    Disposition disposition,
    const std::optional<Data::SemanticOperationRecord> &record,
    const Core::SemanticRuntimeValidation &validation = {})
{
    return {disposition, record, validation};
}

bool isTerminalState(Data::SemanticOperationState state)
{
    switch (state) {
    case Data::SemanticOperationState::Rejected:
    case Data::SemanticOperationState::Succeeded:
    case Data::SemanticOperationState::Failed:
    case Data::SemanticOperationState::TimedOut:
    case Data::SemanticOperationState::Canceled:
    case Data::SemanticOperationState::Expired:
        return true;
    case Data::SemanticOperationState::Submitted:
    case Data::SemanticOperationState::ApprovalRequired:
    case Data::SemanticOperationState::Approved:
    case Data::SemanticOperationState::Executing:
    case Data::SemanticOperationState::OutcomeUnknown:
        return false;
    }
    return true;
}

bool isAllowedTransition(
    Data::SemanticOperationState previousState, Data::SemanticOperationState state)
{
    using State = Data::SemanticOperationState;
    switch (previousState) {
    case State::Submitted:
        return state == State::ApprovalRequired || state == State::Rejected
               || state == State::Canceled || state == State::Expired;
    case State::ApprovalRequired:
        return state == State::Approved || state == State::Rejected || state == State::Canceled
               || state == State::Expired;
    case State::Approved:
        return state == State::Executing || state == State::Failed || state == State::Canceled
               || state == State::Expired;
    case State::Executing:
        return state == State::Succeeded || state == State::Failed || state == State::TimedOut
               || state == State::OutcomeUnknown || state == State::Canceled
               || state == State::Expired;
    case State::OutcomeUnknown:
        return state == State::Succeeded || state == State::Failed || state == State::Expired;
    case State::Rejected:
    case State::Succeeded:
    case State::Failed:
    case State::TimedOut:
    case State::Canceled:
    case State::Expired:
        return false;
    }
    return false;
}

bool actorIsValid(const Data::SemanticRuntimeActor &actor)
{
    const auto validText = [](const QString &text, qsizetype maximumSize) {
        return !text.isEmpty() && text == text.trimmed() && text.size() <= maximumSize
               && std::none_of(text.cbegin(), text.cend(), [](QChar character) {
                      return character.category() == QChar::Other_Control;
                  });
    };
    const bool kindKnown = actor.kind == Data::SemanticRuntimeActorKind::User
                           || actor.kind == Data::SemanticRuntimeActorKind::Automation
                           || actor.kind == Data::SemanticRuntimeActorKind::System;
    return kindKnown && validText(actor.id, 128) && validText(actor.origin, 256)
           && actor.authenticationDigest.size()
                  == QCryptographicHash::hashLength(QCryptographicHash::Sha256)
           && std::any_of(
               actor.authenticationDigest.cbegin(),
               actor.authenticationDigest.cend(),
               [](char byte) { return byte != 0; });
}

bool snapshotIsValid(const Data::SemanticOperationSnapshot &snapshot)
{
    return snapshot.complete
           && snapshot.digest.size() == QCryptographicHash::hashLength(QCryptographicHash::Sha256)
           && snapshot.controllerTimestampNs != 0;
}

Data::SemanticAuditEventKind auditKindForTransition(
    Data::SemanticOperationState previousState, Data::SemanticOperationState state)
{
    if (previousState == Data::SemanticOperationState::OutcomeUnknown
        && state != Data::SemanticOperationState::OutcomeUnknown) {
        return Data::SemanticAuditEventKind::OutcomeReconciled;
    }
    if (state == Data::SemanticOperationState::Rejected)
        return Data::SemanticAuditEventKind::Rejected;
    if (isTerminalState(state) || state == Data::SemanticOperationState::OutcomeUnknown)
        return Data::SemanticAuditEventKind::ExecutionResult;
    return Data::SemanticAuditEventKind::StateChanged;
}

bool stateUpdateMatches(
    const Data::SemanticOperationRecord &record, const SemanticOperationJournalStateUpdate &update)
{
    return record.state == update.state && record.resultCode == update.resultCode
           && record.detail == update.detail && record.controllerError == update.controllerError
           && record.appliedCycle == update.appliedCycle
           && record.appliedRuntimeGeneration == update.appliedRuntimeGeneration;
}

} // namespace

SemanticOperationJournalResult SemanticOperationJournal::submit(
    const Data::SemanticOperationRequest &request,
    const Data::SemanticRuntimeActor &actor,
    const Data::SemanticRuntimeContext &context,
    const QDateTime &occurredAt)
{
    const QDateTime now = operationTime(occurredAt);
    if (!actorIsValid(actor)) {
        return result(
            Disposition::Rejected,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation actor identity is invalid.")));
    }

    const auto existing = m_operations.constFind(request.operationId);
    if (existing != m_operations.cend()) {
        if (requestsAreIdempotentReplays(existing->request, request) && existing->actor == actor)
            return result(Disposition::Replayed, *existing);

        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("OperationId was already used for a different request or actor."));
        appendAudit(
            *existing,
            Data::SemanticAuditEventKind::Rejected,
            existing->state,
            actor,
            QStringLiteral("operation-id-conflict"),
            validation.detail,
            now);
        return result(Disposition::Conflict, *existing, validation);
    }

    const Core::SemanticRuntimeValidation validation
        = Core::validateSemanticOperationRequest(request, context);
    Data::SemanticOperationRecord record;
    record.request = request;
    record.actor = actor;
    record.canonicalRequestDigest = requestDigest(request);
    record.createdAt = now;
    record.updatedAt = now;

    if (!validation.accepted()) {
        record.state = Data::SemanticOperationState::Rejected;
        record.resultCode = QStringLiteral("semantic-operation-rejected");
        record.detail = validation.detail;
        if (Core::isCanonicalSemanticOperationId(request.operationId)) {
            m_operations.insert(request.operationId, record);
            appendAudit(
                record,
                Data::SemanticAuditEventKind::Rejected,
                Data::SemanticOperationState::Submitted,
                actor,
                record.resultCode,
                record.detail,
                now);
        }
        return result(Disposition::Rejected, record, validation);
    }

    record.approvalChallenge = approvalChallenge(request, context);
    if (record.canonicalRequestDigest.isEmpty() || record.approvalChallenge.isEmpty()) {
        const Core::SemanticRuntimeValidation challengeValidation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation approval evidence is incomplete."));
        record.state = Data::SemanticOperationState::Rejected;
        record.resultCode = QStringLiteral("approval-evidence-invalid");
        record.detail = challengeValidation.detail;
        m_operations.insert(request.operationId, record);
        appendAudit(
            record,
            Data::SemanticAuditEventKind::Rejected,
            Data::SemanticOperationState::Submitted,
            actor,
            record.resultCode,
            record.detail,
            now);
        return result(Disposition::Rejected, record, challengeValidation);
    }

    record.state = Data::SemanticOperationState::ApprovalRequired;
    record.resultCode = QStringLiteral("approval-required");
    record.detail = QStringLiteral("Waiting for an authenticated user approval.");
    m_operations.insert(request.operationId, record);
    appendAudit(
        record,
        Data::SemanticAuditEventKind::Submitted,
        Data::SemanticOperationState::Submitted,
        actor,
        record.resultCode,
        record.detail,
        now);
    return result(Disposition::Created, record);
}

SemanticOperationJournalResult SemanticOperationJournal::approve(
    const Data::SemanticOperationApprovalRequest &approval,
    const Data::SemanticRuntimeActor &actor,
    const Data::SemanticRuntimeContext &context,
    const QDateTime &occurredAt)
{
    auto found = m_operations.find(approval.operationId);
    if (found == m_operations.end()) {
        return result(
            Disposition::NotFound,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation was not found.")));
    }
    if (!actorIsValid(actor)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::ApprovalActorInvalid,
                QStringLiteral("Semantic operation approval actor identity is invalid.")));
    }

    const auto existingApproval = std::find_if(
        found->approvals.cbegin(),
        found->approvals.cend(),
        [&approval, &actor](const Data::SemanticOperationApproval &entry) {
            return entry.request == approval && entry.actor == actor;
        });
    if (existingApproval != found->approvals.cend()) {
        Data::SemanticOperationRecord replayCandidate = *found;
        replayCandidate.state = Data::SemanticOperationState::ApprovalRequired;
        const Core::SemanticRuntimeValidation replayValidation
            = Core::validateSemanticOperationApproval(
                approval, actor, replayCandidate, context);
        if (!replayValidation.accepted()) {
            appendAudit(
                *found,
                Data::SemanticAuditEventKind::Rejected,
                found->state,
                actor,
                QStringLiteral("approval-replay-rejected"),
                replayValidation.detail,
                operationTime(occurredAt));
            return result(Disposition::Rejected, *found, replayValidation);
        }
        return result(Disposition::Replayed, *found);
    }

    const QDateTime now = operationTime(occurredAt);
    const Core::SemanticRuntimeValidation validation
        = Core::validateSemanticOperationApproval(approval, actor, *found, context);
    if (!validation.accepted()) {
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            actor,
            QStringLiteral("approval-rejected"),
            validation.detail,
            now);
        return result(Disposition::Rejected, *found, validation);
    }
    const Data::SemanticOperationState previousState = found->state;
    found->approvals.append({approval, actor, now});
    if (approval.decision == Data::SemanticApprovalDecision::Approved) {
        found->state = Data::SemanticOperationState::Approved;
        found->resultCode = QStringLiteral("approved");
        found->detail = approval.detail.isEmpty()
                            ? QStringLiteral("Semantic operation was approved.")
                            : approval.detail;
    } else {
        found->state = Data::SemanticOperationState::Rejected;
        found->resultCode = QStringLiteral("approval-denied");
        found->detail = approval.detail.isEmpty() ? QStringLiteral("Semantic operation was denied.")
                                                  : approval.detail;
    }
    found->updatedAt = now;
    appendAudit(
        *found,
        Data::SemanticAuditEventKind::ApprovalRecorded,
        previousState,
        actor,
        found->resultCode,
        found->detail,
        now);
    return result(Disposition::Updated, *found);
}

SemanticOperationJournalResult SemanticOperationJournal::transition(
    const Data::SemanticOperationId &operationId,
    const SemanticOperationJournalStateUpdate &update,
    const QDateTime &occurredAt)
{
    auto found = m_operations.find(operationId);
    if (found == m_operations.end()) {
        return result(
            Disposition::NotFound,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation was not found.")));
    }
    if (!actorIsValid(update.actor)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation transition actor identity is invalid.")));
    }
    if (stateUpdateMatches(*found, update))
        return result(Disposition::Replayed, *found);

    const QDateTime now = operationTime(occurredAt);
    if (found->state != update.expectedState) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation state changed before the update."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            update.actor,
            QStringLiteral("operation-state-conflict"),
            validation.detail,
            now);
        return result(Disposition::Conflict, *found, validation);
    }
    // Approval is a privileged journal mutation. It can only be produced by
    // approve(), after the shared Core validator authenticates a human user and
    // revalidates the challenge against the current runtime context.
    if (update.state == Data::SemanticOperationState::Approved
        || !isAllowedTransition(found->state, update.state)) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation state transition is not allowed."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            update.actor,
            QStringLiteral("operation-state-invalid"),
            validation.detail,
            now);
        return result(Disposition::Rejected, *found, validation);
    }
    if ((update.state == Data::SemanticOperationState::Succeeded
         && (!found->totalSteps || found->currentStep != found->totalSteps
             || found->failedStep))
        || (found->failedStep
            && update.state != Data::SemanticOperationState::Failed
            && update.state != Data::SemanticOperationState::TimedOut
            && update.state != Data::SemanticOperationState::Expired)) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation execution evidence is incomplete."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            update.actor,
            QStringLiteral("operation-evidence-incomplete"),
            validation.detail,
            now);
        return result(Disposition::Rejected, *found, validation);
    }

    const Data::SemanticOperationState previousState = found->state;
    found->state = update.state;
    found->resultCode = update.resultCode;
    found->detail = update.detail;
    found->controllerError = update.controllerError;
    found->appliedCycle = update.appliedCycle;
    found->appliedRuntimeGeneration = update.appliedRuntimeGeneration;
    found->executionAttempted = found->executionAttempted
                                || update.state == Data::SemanticOperationState::Executing
                                || previousState == Data::SemanticOperationState::Executing;
    found->updatedAt = now;
    appendAudit(
        *found,
        auditKindForTransition(previousState, found->state),
        previousState,
        update.actor,
        found->resultCode,
        found->detail,
        now,
        found->currentStep);
    return result(Disposition::Updated, *found);
}

SemanticOperationJournalResult SemanticOperationJournal::recordStep(
    const Data::SemanticOperationId &operationId,
    const SemanticOperationJournalStepUpdate &update,
    const QDateTime &occurredAt)
{
    auto found = m_operations.find(operationId);
    if (found == m_operations.end()) {
        return result(
            Disposition::NotFound,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation was not found.")));
    }
    if (!actorIsValid(update.actor)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation step actor identity is invalid.")));
    }

    const quint32 failedStep = update.failed ? update.stepIndex : 0;
    if (found->state == update.expectedState && found->currentStep == update.stepIndex
        && found->totalSteps == update.totalSteps && found->failedStep == failedStep
        && found->detail == update.detail && found->controllerError == update.controllerError) {
        return result(Disposition::Replayed, *found);
    }

    const QDateTime now = operationTime(occurredAt);
    if (found->state != update.expectedState) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation state changed before the step update."));
        return result(Disposition::Conflict, *found, validation);
    }
    if (found->state != Data::SemanticOperationState::Executing || update.stepIndex == 0
        || update.totalSteps == 0 || update.stepIndex > update.totalSteps
        || found->failedStep != 0
        || (found->totalSteps != 0 && found->totalSteps != update.totalSteps)
        || update.stepIndex != found->currentStep + 1) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation step sequence is invalid."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            update.actor,
            QStringLiteral("operation-step-invalid"),
            validation.detail,
            now,
            update.stepIndex);
        return result(Disposition::Rejected, *found, validation);
    }

    found->currentStep = update.stepIndex;
    found->totalSteps = update.totalSteps;
    found->failedStep = failedStep;
    found->detail = update.detail;
    found->controllerError = update.controllerError;
    found->executionAttempted = true;
    found->updatedAt = now;
    appendAudit(
        *found,
        Data::SemanticAuditEventKind::ExecutionStep,
        found->state,
        update.actor,
        update.failed ? QStringLiteral("execution-step-failed")
                      : QStringLiteral("execution-step-recorded"),
        found->detail,
        now,
        update.stepIndex);
    return result(Disposition::Updated, *found);
}

SemanticOperationJournalResult SemanticOperationJournal::recordBeforeSnapshot(
    const Data::SemanticOperationId &operationId,
    Data::SemanticOperationState expectedState,
    const Data::SemanticOperationSnapshot &snapshot,
    const Data::SemanticRuntimeActor &actor,
    const QDateTime &occurredAt)
{
    auto found = m_operations.find(operationId);
    if (found == m_operations.end()) {
        return result(
            Disposition::NotFound,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation was not found.")));
    }
    if (!actorIsValid(actor)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation snapshot actor identity is invalid.")));
    }
    if (found->beforeSnapshot == snapshot)
        return result(Disposition::Replayed, *found);

    const QDateTime now = operationTime(occurredAt);
    if (found->state != expectedState) {
        return result(
            Disposition::Conflict,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation state changed before snapshot capture.")));
    }
    if (found->beforeSnapshot || !snapshotIsValid(snapshot)) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation before snapshot is invalid or already recorded."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            actor,
            QStringLiteral("before-snapshot-invalid"),
            validation.detail,
            now,
            found->currentStep);
        return result(Disposition::Rejected, *found, validation);
    }

    found->beforeSnapshot = snapshot;
    found->updatedAt = now;
    appendAudit(
        *found,
        Data::SemanticAuditEventKind::ExecutionStep,
        found->state,
        actor,
        QStringLiteral("before-snapshot-recorded"),
        QStringLiteral("Captured the pre-operation semantic snapshot."),
        now,
        found->currentStep);
    return result(Disposition::Updated, *found);
}

SemanticOperationJournalResult SemanticOperationJournal::recordAfterSnapshot(
    const Data::SemanticOperationId &operationId,
    Data::SemanticOperationState expectedState,
    const Data::SemanticOperationSnapshot &snapshot,
    const Data::SemanticRuntimeActor &actor,
    const QDateTime &occurredAt)
{
    auto found = m_operations.find(operationId);
    if (found == m_operations.end()) {
        return result(
            Disposition::NotFound,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation was not found.")));
    }
    if (!actorIsValid(actor)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation snapshot actor identity is invalid.")));
    }
    if (found->afterSnapshot == snapshot)
        return result(Disposition::Replayed, *found);

    const QDateTime now = operationTime(occurredAt);
    if (found->state != expectedState) {
        return result(
            Disposition::Conflict,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation state changed before snapshot capture.")));
    }
    if (found->afterSnapshot || !snapshotIsValid(snapshot)) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation after snapshot is invalid or already recorded."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            actor,
            QStringLiteral("after-snapshot-invalid"),
            validation.detail,
            now,
            found->currentStep);
        return result(Disposition::Rejected, *found, validation);
    }

    found->afterSnapshot = snapshot;
    found->updatedAt = now;
    appendAudit(
        *found,
        Data::SemanticAuditEventKind::ExecutionStep,
        found->state,
        actor,
        QStringLiteral("after-snapshot-recorded"),
        QStringLiteral("Captured the post-operation semantic snapshot."),
        now,
        found->currentStep);
    return result(Disposition::Updated, *found);
}

std::optional<Data::SemanticOperationRecord> SemanticOperationJournal::operation(
    const Data::SemanticOperationId &operationId) const
{
    const auto found = m_operations.constFind(operationId);
    if (found == m_operations.cend())
        return std::nullopt;
    return *found;
}

QList<Data::SemanticRuntimeAuditEvent> SemanticOperationJournal::audit(
    const QString &controllerId, quint64 afterSequence) const
{
    QList<Data::SemanticRuntimeAuditEvent> result;
    const auto events = m_auditEvents.constFind(controllerId);
    if (events == m_auditEvents.cend())
        return result;

    for (const Data::SemanticRuntimeAuditEvent &event : *events) {
        if (event.sequence > afterSequence)
            result.append(event);
    }
    return result;
}

QByteArray SemanticOperationJournal::approvalChallenge(
    const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeContext &context)
{
    if (!Core::validateSemanticOperationRequest(request, context).accepted())
        return {};

    const QByteArray canonicalRequestDigest = requestDigest(request);
    if (canonicalRequestDigest.isEmpty())
        return {};

    QString actionBindingId;
    Data::SemanticRuntimeDigest actionDefinitionDigest;
    if (request.target.kind == Data::SemanticRuntimeTargetKind::Action) {
        QList<Data::SemanticActionRuntimeState> matches;
        for (const Data::SemanticActionRuntimeState &action : context.actionStates) {
            if (action.target == request.target)
                matches.append(action);
        }
        if (matches.size() != 1)
            return {};
        actionBindingId = matches.constFirst().actionBindingId;
        actionDefinitionDigest = matches.constFirst().actionDefinitionDigest;
        if (actionBindingId.isEmpty() || !Core::isCanonicalSha256Digest(actionDefinitionDigest)) {
            return {};
        }
    }

    QByteArray challengeMaterial;
    QDataStream stream(&challengeMaterial, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << QByteArrayLiteral("embed-labs-semantic-approval-challenge-v1")
           << request.operationId.value << canonicalRequestDigest << context.contextHash
           << actionBindingId << actionDefinitionDigest.algorithm << actionDefinitionDigest.value;
    if (stream.status() != QDataStream::Ok)
        return {};
    return QCryptographicHash::hash(challengeMaterial, QCryptographicHash::Sha256);
}

void SemanticOperationJournal::appendAudit(
    const Data::SemanticOperationRecord &record,
    Data::SemanticAuditEventKind kind,
    Data::SemanticOperationState previousState,
    const Data::SemanticRuntimeActor &actor,
    const QString &code,
    const QString &detail,
    const QDateTime &occurredAt,
    quint32 stepIndex)
{
    const QString &controllerId = record.request.target.controllerId;
    if (controllerId.isEmpty())
        return;

    quint64 &lastSequence = m_lastAuditSequence[controllerId];
    if (lastSequence == std::numeric_limits<quint64>::max())
        return;

    Data::SemanticRuntimeAuditEvent event;
    event.sequence = ++lastSequence;
    event.controllerId = controllerId;
    event.operationId = record.request.operationId;
    event.kind = kind;
    event.previousState = previousState;
    event.state = record.state;
    event.actor = actor;
    event.canonicalRequestDigest = record.canonicalRequestDigest;
    event.stepIndex = stepIndex;
    if (record.beforeSnapshot)
        event.beforeSnapshotDigest = record.beforeSnapshot->digest;
    if (record.afterSnapshot)
        event.afterSnapshotDigest = record.afterSnapshot->digest;
    event.appliedCycle = record.appliedCycle;
    event.controllerError = record.controllerError;
    event.occurredAt = occurredAt;
    event.code = code;
    event.detail = detail;
    m_auditEvents[controllerId].append(event);
}

} // namespace EtherCAT::SemanticRuntime::Internal
