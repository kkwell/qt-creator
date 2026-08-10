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

QByteArray cancelDigest(const Data::SemanticOperationCancelRequest &request)
{
    const QByteArray canonical = Core::canonicalSemanticOperationCancelRequest(request);
    if (canonical.isEmpty())
        return {};
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

bool advanceRevision(Data::SemanticOperationRecord *record)
{
    if (!record || record->revision == std::numeric_limits<quint64>::max())
        return false;
    ++record->revision;
    return true;
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
               || state == State::Expired;
    case State::ApprovalRequired:
        return state == State::Approved || state == State::Rejected || state == State::Expired;
    case State::Approved:
        return state == State::Executing || state == State::Failed || state == State::Expired;
    case State::Executing:
        return state == State::Succeeded || state == State::Failed || state == State::TimedOut
               || state == State::OutcomeUnknown || state == State::Expired;
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
    return kindKnown && validText(actor.id, 128)
           && (actor.displayName.isEmpty() || validText(actor.displayName, 128))
           && validText(actor.origin, 256)
           && actor.authenticationDigest.size()
                  == QCryptographicHash::hashLength(QCryptographicHash::Sha256)
           && std::any_of(
               actor.authenticationDigest.cbegin(),
               actor.authenticationDigest.cend(),
               [](char byte) { return byte != 0; });
}

Data::SemanticRuntimeActor rejectedCancellationAuditActor(const Data::SemanticRuntimeActor &actor)
{
    const auto textIsSafe = [](const QString &text, qsizetype maximumSize, bool allowEmpty) {
        return (allowEmpty || !text.isEmpty()) && text == text.trimmed()
               && text.size() <= maximumSize
               && std::none_of(text.cbegin(), text.cend(), [](QChar character) {
                      return character.category() == QChar::Other_Control;
                  });
    };
    if (actorIsValid(actor) && textIsSafe(actor.displayName, 128, true))
        return actor;

    Data::SemanticRuntimeActor rejectedActor;
    rejectedActor.id = QStringLiteral("invalid-cancel-actor");
    rejectedActor.displayName = QStringLiteral("Rejected cancellation actor");
    rejectedActor.kind = Data::SemanticRuntimeActorKind::System;
    rejectedActor.origin = QStringLiteral("semantic-operation-journal");
    rejectedActor.authenticationDigest = QCryptographicHash::hash(
        QByteArrayLiteral("embed-labs.semantic-operation.invalid-cancel-actor.v1"),
        QCryptographicHash::Sha256);
    return rejectedActor;
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

bool cancellationPhaseTransitionIsAllowed(
    Data::SemanticOperationCancellationPhase from, Data::SemanticOperationCancellationPhase to)
{
    using Phase = Data::SemanticOperationCancellationPhase;
    switch (from) {
    case Phase::Requested:
        return to == Phase::WaitingForApplyResult || to == Phase::ProvingSafeHold
               || to == Phase::Completed;
    case Phase::WaitingForApplyResult:
        return to == Phase::ProvingSafeHold || to == Phase::Completed;
    case Phase::ProvingSafeHold:
        return to == Phase::Completed;
    case Phase::Completed:
        return false;
    }
    return false;
}

bool cancellationUpdateMatches(
    const Data::SemanticOperationRecord &record,
    const SemanticOperationJournalCancellationUpdate &update)
{
    const auto safeHoldState = update.safeHoldState
                                   ? update.safeHoldState
                                   : (record.cancellation ? record.cancellation->safeHoldState
                                                          : std::nullopt);
    const auto pendingApplyRequest = update.pendingApplyRequest
                                         ? update.pendingApplyRequest
                                         : (record.cancellation
                                                ? record.cancellation->pendingApplyRequest
                                                : std::nullopt);
    const auto priorAppliedRequest = update.priorAppliedRequest
                                         ? update.priorAppliedRequest
                                         : (record.cancellation
                                                ? record.cancellation->priorAppliedRequest
                                                : std::nullopt);
    const auto safeHoldRequest = update.safeHoldRequest
                                     ? update.safeHoldRequest
                                     : (record.cancellation ? record.cancellation->safeHoldRequest
                                                            : std::nullopt);
    const auto terminalApplyResult = update.terminalApplyResult
                                         ? update.terminalApplyResult
                                         : (record.cancellation
                                                ? record.cancellation->terminalApplyResult
                                                : std::nullopt);
    if (!record.cancellation || record.cancellation->phase != update.phase
        || record.cancellation->safeHoldState != safeHoldState
        || record.cancellation->pendingApplyRequest != pendingApplyRequest
        || record.cancellation->priorAppliedRequest != priorAppliedRequest
        || record.cancellation->safeHoldRequest != safeHoldRequest
        || record.cancellation->terminalApplyResult != terminalApplyResult) {
        return false;
    }
    if (update.phase != Data::SemanticOperationCancellationPhase::Completed)
        return record.state == update.expectedState;
    return record.state == Data::SemanticOperationState::Canceled
           && record.resultCode == update.resultCode && record.detail == update.detail
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
    record.revision = 1;

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
    const QDateTime now = operationTime(occurredAt);

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
                now);
            return result(Disposition::Rejected, *found, replayValidation);
        }
        return result(Disposition::Replayed, *found);
    }
    if (found->cancellation) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Cancellation already owns the semantic operation outcome."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            actor,
            QStringLiteral("approval-cancellation-pending"),
            validation.detail,
            now);
        return result(Disposition::Conflict, *found, validation);
    }

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
    if (!advanceRevision(&*found)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation revision is exhausted.")));
    }
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

SemanticOperationJournalResult SemanticOperationJournal::cancel(
    const Data::SemanticOperationCancelRequest &request,
    const Data::SemanticRuntimeActor &actor,
    const QDateTime &occurredAt)
{
    const QDateTime now = operationTime(occurredAt);
    if (!Core::isCanonicalSemanticOperationCancelId(request.cancelId)) {
        return result(
            Disposition::Rejected,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation cancellation ID is invalid.")));
    }

    const QByteArray canonicalDigest = cancelDigest(request);
    const Data::SemanticRuntimeActor rejectedAuditActor = rejectedCancellationAuditActor(actor);
    const QString rejectedAuditReason = canonicalDigest.isEmpty() ? QString() : request.reason;
    const auto rejectedAttempt =
        [this, &request, &rejectedAuditActor, &rejectedAuditReason, &canonicalDigest, &now](
            Disposition disposition,
            const Data::SemanticOperationRecord &record,
            const Core::SemanticRuntimeValidation &validation,
            const QString &code) {
            appendAudit(
                record,
                Data::SemanticAuditEventKind::Rejected,
                record.state,
                rejectedAuditActor,
                code,
                validation.detail,
                now,
                record.currentStep,
                request.cancelId,
                canonicalDigest,
                rejectedAuditReason);
            return result(disposition, record, validation);
        };
    const auto boundOperation = m_cancellationsById.constFind(request.cancelId);
    if (boundOperation != m_cancellationsById.cend()) {
        const auto boundRecord = m_operations.constFind(*boundOperation);
        if (boundRecord != m_operations.cend() && boundRecord->cancellation
            && *boundOperation == request.operationId && !canonicalDigest.isEmpty()
            && boundRecord->cancellation->canonicalCancelDigest == canonicalDigest
            && boundRecord->cancellation->request == request
            && boundRecord->cancellation->actor == actor) {
            return result(Disposition::Replayed, *boundRecord);
        }
        const std::optional<Data::SemanticOperationRecord> conflictRecord
            = boundRecord != m_operations.cend()
                  ? std::optional<Data::SemanticOperationRecord>(*boundRecord)
                  : std::nullopt;
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Cancellation ID was already used for another request or actor."));
        if (conflictRecord) {
            return rejectedAttempt(
                Disposition::Conflict,
                *conflictRecord,
                validation,
                QStringLiteral("semantic-operation-cancel-id-conflict"));
        }
        return result(Disposition::Conflict, std::nullopt, validation);
    }

    auto found = m_operations.find(request.operationId);
    if (found == m_operations.end()) {
        return result(
            Disposition::NotFound,
            std::nullopt,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation was not found.")));
    }
    if (found->cancellation) {
        if (found->cancellation->request.cancelId == request.cancelId && !canonicalDigest.isEmpty()
            && found->cancellation->canonicalCancelDigest == canonicalDigest
            && found->cancellation->request == request && found->cancellation->actor == actor) {
            m_cancellationsById.insert(request.cancelId, request.operationId);
            return result(Disposition::Replayed, *found);
        }
        if (request.expectedRevision != found->revision) {
            const Core::SemanticRuntimeValidation validation = rejection(
                Core::SemanticRuntimeValidationError::CancelRevisionMismatch,
                QStringLiteral("Semantic operation revision changed before cancellation."));
            return rejectedAttempt(
                Disposition::Stale,
                *found,
                validation,
                QStringLiteral("semantic-operation-cancel-stale"));
        }
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation already has another cancellation request."));
        return rejectedAttempt(
            Disposition::Conflict,
            *found,
            validation,
            QStringLiteral("semantic-operation-cancel-conflict"));
    }
    if (!actorIsValid(actor) || actor.kind != Data::SemanticRuntimeActorKind::User) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::CancelActorInvalid,
            QStringLiteral("An authenticated local user must cancel the semantic operation."));
        return rejectedAttempt(
            Disposition::Rejected,
            *found,
            validation,
            QStringLiteral("semantic-operation-cancel-actor-rejected"));
    }
    if (request.expectedRevision != found->revision) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::CancelRevisionMismatch,
            QStringLiteral("Semantic operation revision changed before cancellation."));
        return rejectedAttempt(
            Disposition::Stale,
            *found,
            validation,
            QStringLiteral("semantic-operation-cancel-stale"));
    }
    if (found->request.kind != Data::SemanticOperationKind::InvokeAction
        || found->request.target.kind != Data::SemanticRuntimeTargetKind::Action
        || isTerminalState(found->state)
        || (found->state != Data::SemanticOperationState::ApprovalRequired
            && found->state != Data::SemanticOperationState::Approved
            && found->state != Data::SemanticOperationState::Executing
            && found->state != Data::SemanticOperationState::OutcomeUnknown)) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation cannot be canceled in its current state."));
        return rejectedAttempt(
            Disposition::Unsupported,
            *found,
            validation,
            QStringLiteral("semantic-operation-cancel-unsupported"));
    }

    const Core::SemanticRuntimeValidation validation
        = Core::validateSemanticOperationCancelRequest(request, actor, *found);
    if (!validation.accepted()) {
        const Disposition disposition
            = validation.error == Core::SemanticRuntimeValidationError::CancelRevisionMismatch
                  ? Disposition::Stale
                  : (validation.error == Core::SemanticRuntimeValidationError::CancelBindingMismatch
                         ? Disposition::Conflict
                         : Disposition::Rejected);
        return rejectedAttempt(
            disposition,
            *found,
            validation,
            disposition == Disposition::Conflict
                ? QStringLiteral("semantic-operation-cancel-binding-conflict")
                : QStringLiteral("semantic-operation-cancel-rejected"));
    }
    if (canonicalDigest.isEmpty() || !advanceRevision(&*found)) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation cancellation evidence is unavailable."));
        return rejectedAttempt(
            Disposition::Rejected,
            *found,
            validation,
            QStringLiteral("semantic-operation-cancel-evidence-rejected"));
    }

    found->cancellation = Data::SemanticOperationCancellation{
        request,
        actor,
        canonicalDigest,
        now,
        Data::SemanticOperationCancellationPhase::Requested,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
    };
    found->updatedAt = now;
    m_cancellationsById.insert(request.cancelId, request.operationId);
    appendAudit(
        *found,
        Data::SemanticAuditEventKind::CancellationRequested,
        found->state,
        actor,
        QStringLiteral("semantic-operation-cancel-requested"),
        request.reason,
        now,
        found->currentStep);
    return result(Disposition::Updated, *found);
}

SemanticOperationJournalResult SemanticOperationJournal::updateCancellation(
    const Data::SemanticOperationId &operationId,
    const SemanticOperationJournalCancellationUpdate &update,
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
    if (!actorIsValid(update.actor) || !found->cancellation
        || update.actor != found->cancellation->actor) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation cancellation evidence is incomplete.")));
    }
    const auto replacesStoredEvidence = [](const auto &stored, const auto &supplied) {
        return stored && supplied && stored != supplied;
    };
    if (replacesStoredEvidence(found->cancellation->safeHoldState, update.safeHoldState)
        || replacesStoredEvidence(found->cancellation->pendingApplyRequest, update.pendingApplyRequest)
        || replacesStoredEvidence(found->cancellation->priorAppliedRequest, update.priorAppliedRequest)
        || replacesStoredEvidence(found->cancellation->safeHoldRequest, update.safeHoldRequest)
        || replacesStoredEvidence(
            found->cancellation->terminalApplyResult, update.terminalApplyResult)) {
        return result(
            Disposition::Conflict,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation cancellation evidence changed identity.")));
    }
    if (cancellationUpdateMatches(*found, update))
        return result(Disposition::Replayed, *found);
    if (found->state != update.expectedState || found->cancellation->phase != update.expectedPhase) {
        return result(
            Disposition::Conflict,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation changed before cancellation progressed.")));
    }
    if (!cancellationPhaseTransitionIsAllowed(update.expectedPhase, update.phase)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation cancellation phase is invalid.")));
    }

    using Phase = Data::SemanticOperationCancellationPhase;
    const bool completed = update.phase == Phase::Completed;
    const std::optional<Data::RuntimeOutputTransactionState> safeHoldState
        = update.safeHoldState ? update.safeHoldState : found->cancellation->safeHoldState;
    const std::optional<Data::RuntimeOutputTransactionRequest> pendingApplyRequest
        = update.pendingApplyRequest ? update.pendingApplyRequest
                                     : found->cancellation->pendingApplyRequest;
    const std::optional<Data::RuntimeOutputTransactionRequest> priorAppliedRequest
        = update.priorAppliedRequest ? update.priorAppliedRequest
                                     : found->cancellation->priorAppliedRequest;
    const std::optional<Data::RuntimeOutputTransactionRequest> safeHoldRequest
        = update.safeHoldRequest ? update.safeHoldRequest : found->cancellation->safeHoldRequest;
    const bool pendingApplyRequestValid = pendingApplyRequest && pendingApplyRequest->isValid();
    const bool priorAppliedRequestValid = priorAppliedRequest && priorAppliedRequest->isValid();
    const bool safeHoldRequestValid = safeHoldRequest && safeHoldRequest->isValid()
                                      && safeHoldRequest->expectedRecoveryPolicy
                                             == Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    const std::optional<Data::RuntimeOutputTransactionResult> terminalApplyResult
        = update.terminalApplyResult ? update.terminalApplyResult
                                     : found->cancellation->terminalApplyResult;
    const bool safeHoldValid
        = safeHoldState && safeHoldRequestValid && safeHoldState->isValid()
          && safeHoldState->state == Data::RuntimeOutputState::SafeHold
          && safeHoldState->resultFlags.testFlag(Data::RuntimeOutputTransactionResultFlag::SafeHold)
          && safeHoldState->operationId
          && *safeHoldState->operationId == safeHoldRequest->operationId
          && safeHoldState->scope == safeHoldRequest->scope
          && safeHoldState->sessionGeneration == safeHoldRequest->sessionGeneration
          && safeHoldState->epoch == safeHoldRequest->expectedEpoch
          && safeHoldState->mappingDigest == safeHoldRequest->expectedMappingDigest
          && safeHoldState->consistencyGroupId == safeHoldRequest->consistencyGroupId
          && safeHoldState->recoveryPolicy == safeHoldRequest->expectedRecoveryPolicy
          && safeHoldState->ttlCycles == safeHoldRequest->ttlCycles
          && safeHoldState->valueCount == quint16(safeHoldRequest->completeGroupWrites.size())
          && safeHoldState->outputGeneration == safeHoldRequest->expectedOutputGeneration + 2
          && safeHoldState->controllerTimestampNs;
    const bool terminalRejectionValid = terminalApplyResult && pendingApplyRequestValid
                                        && terminalApplyResult->isValid()
                                        && terminalApplyResult->request == *pendingApplyRequest
                                        && terminalApplyResult->outcome
                                               == Data::RuntimeOutputTransactionOutcome::Rejected
                                        && terminalApplyResult->finalResponseObserved
                                        && !terminalApplyResult->state;
    const bool provingFromPendingApply = !completed && update.phase == Phase::ProvingSafeHold
                                         && update.expectedPhase == Phase::WaitingForApplyResult
                                         && safeHoldRequestValid && pendingApplyRequestValid
                                         && *safeHoldRequest == *pendingApplyRequest
                                         && !terminalApplyResult;
    const bool provingPriorAfterRejectedApply = !completed && update.phase == Phase::ProvingSafeHold
                                                && update.expectedPhase
                                                       == Phase::WaitingForApplyResult
                                                && safeHoldRequestValid && priorAppliedRequestValid
                                                && *safeHoldRequest == *priorAppliedRequest
                                                && pendingApplyRequestValid
                                                && terminalRejectionValid;
    const bool provingKnownAppliedRequest = !completed && update.phase == Phase::ProvingSafeHold
                                            && update.expectedPhase == Phase::Requested
                                            && safeHoldRequestValid && priorAppliedRequestValid
                                            && *safeHoldRequest == *priorAppliedRequest
                                            && !pendingApplyRequest && !terminalApplyResult;
    // This process-local journal is called only by the main-thread executor.
    // Executing is accepted without controller proof only after that executor
    // has established its private mutationMayHaveExecuted flag is still false.
    // The flag is set before every provider mutation virtual call.
    const bool preDispatchCompletionAllowed
        = completed && !safeHoldState && update.expectedPhase == Phase::Requested
          && !pendingApplyRequest && !priorAppliedRequest && !safeHoldRequest
          && !terminalApplyResult
          && (found->state == Data::SemanticOperationState::ApprovalRequired
              || found->state == Data::SemanticOperationState::Approved
              || found->state == Data::SemanticOperationState::Executing);
    const bool rejectedApplyCompletionAllowed = completed && !safeHoldState
                                                && terminalRejectionValid
                                                && update.expectedPhase
                                                       == Phase::WaitingForApplyResult
                                                && !priorAppliedRequest && !safeHoldRequest;
    const bool safeHoldCompletionAllowed
        = completed && safeHoldValid && safeHoldRequest
          && ((pendingApplyRequest && *safeHoldRequest == *pendingApplyRequest
               && !terminalApplyResult)
              || (priorAppliedRequest && *safeHoldRequest == *priorAppliedRequest
                  && ((!pendingApplyRequest && !terminalApplyResult) || terminalRejectionValid)));
    const bool completionTextValid = !update.resultCode.isEmpty()
                                     && update.resultCode == update.resultCode.trimmed()
                                     && update.resultCode.size() <= 128 && !update.detail.isEmpty()
                                     && update.detail == update.detail.trimmed()
                                     && update.detail.size() <= 240;
    const bool completionNumbersValid
        = !completed
          || (safeHoldValid ? (update.appliedCycle == safeHoldState->appliedCycle
                               && update.appliedRuntimeGeneration
                                      == safeHoldRequest->expectedEpoch.runtimeGeneration)
                            : (!update.appliedCycle && !update.appliedRuntimeGeneration));
    if ((!completed && safeHoldState)
        || (update.phase == Phase::WaitingForApplyResult && !pendingApplyRequestValid)
        || (update.priorAppliedRequest && !priorAppliedRequestValid)
        || (update.phase == Phase::ProvingSafeHold && !provingFromPendingApply
            && !provingPriorAfterRejectedApply && !provingKnownAppliedRequest)
        || (update.safeHoldRequest && !safeHoldRequestValid)
        || (update.phase != Phase::WaitingForApplyResult && !completed && update.pendingApplyRequest)
        || (update.phase != Phase::WaitingForApplyResult && update.phase != Phase::ProvingSafeHold
            && !completed && update.priorAppliedRequest)
        || (update.phase != Phase::ProvingSafeHold && update.phase != Phase::WaitingForApplyResult
            && !completed && update.safeHoldRequest)
        || (!completed && terminalApplyResult && !provingPriorAfterRejectedApply)
        || (completed && safeHoldState && !safeHoldRequestValid)
        || (completed && safeHoldState.has_value() != safeHoldValid)
        || (completed && !preDispatchCompletionAllowed && !rejectedApplyCompletionAllowed
            && !safeHoldCompletionAllowed)
        || (completed && (!completionTextValid || !completionNumbersValid))
        || (!completed && (!update.resultCode.isEmpty() || !update.detail.isEmpty()))) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation cancellation proof is invalid.")));
    }
    if (!advanceRevision(&*found)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation revision is exhausted.")));
    }

    const Data::SemanticOperationState previousState = found->state;
    found->cancellation->phase = update.phase;
    found->cancellation->safeHoldState = safeHoldState;
    if (update.pendingApplyRequest) {
        found->cancellation->pendingApplyRequest = update.pendingApplyRequest;
    }
    if (update.priorAppliedRequest)
        found->cancellation->priorAppliedRequest = update.priorAppliedRequest;
    if (update.safeHoldRequest)
        found->cancellation->safeHoldRequest = update.safeHoldRequest;
    if (terminalApplyResult)
        found->cancellation->terminalApplyResult = terminalApplyResult;
    found->updatedAt = operationTime(occurredAt);
    if (completed) {
        found->state = Data::SemanticOperationState::Canceled;
        found->resultCode = update.resultCode;
        found->detail = update.detail;
        found->appliedCycle = update.appliedCycle;
        found->appliedRuntimeGeneration = update.appliedRuntimeGeneration;
    }
    appendAudit(
        *found,
        Data::SemanticAuditEventKind::CancellationProgressed,
        previousState,
        update.actor,
        completed ? update.resultCode : QStringLiteral("semantic-operation-cancel-progressed"),
        completed ? update.detail : found->cancellation->request.reason,
        found->updatedAt,
        found->currentStep);
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
    if (update.state == Data::SemanticOperationState::Approved
        || update.state == Data::SemanticOperationState::Canceled) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation state transition is privileged."));
        return result(Disposition::Rejected, *found, validation);
    }
    if (stateUpdateMatches(*found, update))
        return result(Disposition::Replayed, *found);

    const QDateTime now = operationTime(occurredAt);
    if (found->cancellation
        && found->cancellation->phase != Data::SemanticOperationCancellationPhase::Completed
        && update.state != Data::SemanticOperationState::OutcomeUnknown) {
        const Core::SemanticRuntimeValidation validation = rejection(
            Core::SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Cancellation owns the semantic operation terminal transition."));
        appendAudit(
            *found,
            Data::SemanticAuditEventKind::Rejected,
            found->state,
            update.actor,
            QStringLiteral("operation-cancellation-pending"),
            validation.detail,
            now);
        return result(Disposition::Conflict, *found, validation);
    }
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
    if (!isAllowedTransition(found->state, update.state)) {
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
    if (!advanceRevision(&*found)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation revision is exhausted.")));
    }
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

    if (!advanceRevision(&*found)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation revision is exhausted.")));
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

    if (!advanceRevision(&*found)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation revision is exhausted.")));
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

    if (!advanceRevision(&*found)) {
        return result(
            Disposition::Rejected,
            *found,
            rejection(
                Core::SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic operation revision is exhausted.")));
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
    quint32 stepIndex,
    const std::optional<Data::SemanticOperationCancelId> &attemptedCancelId,
    const QByteArray &attemptedCancelDigest,
    const QString &attemptedCancellationReason)
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
    if (attemptedCancelId) {
        event.cancelId = attemptedCancelId;
        event.canonicalCancelDigest = attemptedCancelDigest;
        event.cancellationReason = attemptedCancellationReason;
    } else if (record.cancellation) {
        event.cancelId = record.cancellation->request.cancelId;
        event.canonicalCancelDigest = record.cancellation->canonicalCancelDigest;
        event.cancellationReason = record.cancellation->request.reason;
    }
    m_auditEvents[controllerId].append(event);
}

} // namespace EtherCAT::SemanticRuntime::Internal
