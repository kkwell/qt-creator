// Copyright (C) 2026 Kvell

#pragma once

#include "deviceadapter.h"
#include "ethercatdata_global.h"
#include "runtimeoutputtransaction.h"
#include "runtimeresource.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QVariant>

#include <optional>

namespace EtherCAT::Data {

// Public callers address semantic signals or actions only. RuntimeResourceId and all physical
// binding coordinates remain confined to SemanticRuntimeBinding, which is produced and verified
// inside the IDE.
enum class SemanticRuntimeTargetKind { Signal, Action };

struct ETHERCATDATA_EXPORT SemanticRuntimeTarget
{
    QString controllerId;
    ControllerConnectionScope scope;
    NodeId deviceId;
    SemanticRuntimeTargetKind kind = SemanticRuntimeTargetKind::Signal;
    SemanticSignalId signalId;
    SemanticActionId actionId;

    friend bool operator==(const SemanticRuntimeTarget &, const SemanticRuntimeTarget &) = default;
};

struct ETHERCATDATA_EXPORT SemanticRuntimeDigest
{
    QString algorithm;
    QByteArray value;

    bool isValid() const { return !algorithm.isEmpty() && !value.isEmpty(); }

    friend bool operator==(const SemanticRuntimeDigest &, const SemanticRuntimeDigest &) = default;
};

enum class SemanticBindingVerificationState {
    Unverified,
    Verified,
    Invalid,
    Stale,
    Revoked,
};

struct ETHERCATDATA_EXPORT SemanticBindingVerification
{
    SemanticBindingVerificationState state = SemanticBindingVerificationState::Unverified;
    QString verifierId;
    SemanticRuntimeDigest signedManifestDigest;
    QDateTime verifiedAt;
    QString detail;

    friend bool operator==(const SemanticBindingVerification &, const SemanticBindingVerification &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticRuntimeBinding
{
    SemanticRuntimeTarget target;
    QString semanticBindingId;
    QString componentBindingId;
    DeviceAdapterId adapterId;
    QString adapterVersion;
    QByteArray adapterContentSha256;
    QByteArray esiSha256;
    QByteArray bindingArtifactSha256;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    SemanticRuntimeDigest mappingDigest;
    SemanticRuntimeDigest controllerMappingDigest;
    SemanticBindingVerification verification;
    RuntimeResourceId resourceId;
    RuntimeComponentInstanceId componentInstanceId;
    RuntimeConsistencyGroupId consistencyGroupId;
    RuntimeResourcePrimitiveType primitiveType = RuntimeResourcePrimitiveType::Opaque;
    QByteArray valueTypeIdentity;
    quint32 bitWidth = 0;
    RuntimeResourceDirection direction = RuntimeResourceDirection::Unknown;
    RuntimeResourceAccess access = RuntimeResourceAccess::Unknown;

    friend bool operator==(const SemanticRuntimeBinding &, const SemanticRuntimeBinding &) = default;
};

enum class SemanticSignalAvailability {
    Unavailable,
    Unverified,
    Stale,
    Ready,
    Rejected,
};

struct ETHERCATDATA_EXPORT SemanticSignalRuntimeState
{
    SemanticRuntimeTarget target;
    SemanticSignalDefinition definition;
    SemanticSignalAvailability availability = SemanticSignalAvailability::Unavailable;
    std::optional<SemanticRuntimeBinding> binding;
    std::optional<RuntimeResourceTypedValue> value;
    RuntimeResourceQuality quality;
    bool snapshotComplete = false;
    quint64 captureCycle = 0;
    quint64 controllerTimestampNs = 0;
    QDateTime observedAt;
    QString detail;

    friend bool operator==(const SemanticSignalRuntimeState &, const SemanticSignalRuntimeState &)
        = default;
};

enum class SemanticActionAvailability {
    Unavailable,
    Unverified,
    Stale,
    Ready,
    AwaitingApproval,
    Rejected,
};

enum class SemanticActionQualification {
    Qualified,
    Unqualified,
};

struct ETHERCATDATA_EXPORT SemanticActionParameterRuntimeDefinition
{
    QString id;
    RuntimeResourcePrimitiveType primitiveType = RuntimeResourcePrimitiveType::Opaque;
    QString unit;
    QVariant minimum;
    QVariant maximum;

    friend bool operator==(
        const SemanticActionParameterRuntimeDefinition &,
        const SemanticActionParameterRuntimeDefinition &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticActionRuntimeState
{
    SemanticRuntimeTarget target;
    DeviceControlAction definition;
    QString actionBindingId;
    QString actionDefinitionId;
    SemanticRuntimeDigest actionDefinitionDigest;
    SemanticActionQualification qualification = SemanticActionQualification::Unqualified;
    QString disabledReason;
    SemanticActionAvailability availability = SemanticActionAvailability::Unavailable;
    QList<SemanticRuntimeBinding> bindings;
    QList<SemanticActionParameterRuntimeDefinition> parameters;
    bool requiresApproval = true;
    bool requiresExclusiveControl = true;
    bool requiresDc = false;
    bool holdToRun = false;
    quint32 maximumTtlMs = 0;
    quint32 maximumTtlCycles = 0;
    QString detail;

    friend bool operator==(const SemanticActionRuntimeState &, const SemanticActionRuntimeState &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticRuntimeContext
{
    QString controllerId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    SemanticRuntimeDigest mappingDigest;
    SemanticRuntimeDigest controllerMappingDigest;
    SemanticRuntimeDigest actionDefinitionsDigest;
    quint32 cyclePeriodNs = 0;
    SemanticBindingVerification bindingVerification;
    QByteArray contextHash;
    QList<SemanticSignalRuntimeState> signalStates;
    QList<SemanticActionRuntimeState> actionStates;
    bool complete = false;
    bool mock = false;
    QString detail;

    friend bool operator==(const SemanticRuntimeContext &, const SemanticRuntimeContext &) = default;
};

// A live refresh remains entirely in the semantic namespace. The service is
// responsible for resolving each signal to a verified runtime binding; public
// callers never supply resource IDs, PDO coordinates, or process-image offsets.
struct ETHERCATDATA_EXPORT SemanticLiveRefreshSignal
{
    NodeId deviceId;
    SemanticSignalId signalId;

    friend bool operator==(const SemanticLiveRefreshSignal &, const SemanticLiveRefreshSignal &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticLiveRefreshRequest
{
    QString controllerId;
    ControllerConnectionScope scope;
    QList<SemanticLiveRefreshSignal> targets;
    QByteArray expectedContextHash;
    QString correlationId;

    bool isValid() const
    {
        if (controllerId.isEmpty() || controllerId != controllerId.trimmed()
            || scope.projectId.isNull() || scope.masterId.isNull()
            || expectedContextHash.size() != 32 || correlationId.isEmpty()
            || correlationId.size() > 128 || correlationId != correlationId.trimmed()
            || targets.isEmpty() || targets.size() > 64) {
            return false;
        }
        for (const QChar character : controllerId) {
            if (character.category() == QChar::Other_Control)
                return false;
        }
        for (const QChar character : correlationId) {
            if (character.category() == QChar::Other_Control)
                return false;
        }

        QList<SemanticLiveRefreshSignal> uniqueSignals;
        for (const SemanticLiveRefreshSignal &signal : targets) {
            if (signal.deviceId.isNull() || signal.signalId.value.isEmpty()
                || signal.signalId.value != signal.signalId.value.trimmed()) {
                return false;
            }
            for (const QChar character : signal.signalId.value) {
                if (character.category() == QChar::Other_Control)
                    return false;
            }
            if (uniqueSignals.contains(signal))
                return false;
            uniqueSignals.append(signal);
        }
        return true;
    }

    friend bool operator==(const SemanticLiveRefreshRequest &, const SemanticLiveRefreshRequest &)
        = default;
};

enum class SemanticLiveRefreshOutcome {
    Accepted,
    Refreshed,
    Deferred,
    Unsupported,
    Rejected,
    Failed,
};

// Refreshed values are published through SemanticRuntimeContext. This result
// carries only admission/completion state and the immutable capture boundary;
// it deliberately cannot disclose provider-owned runtime coordinates.
struct ETHERCATDATA_EXPORT SemanticLiveRefreshResult
{
    QString correlationId;
    SemanticLiveRefreshOutcome outcome = SemanticLiveRefreshOutcome::Unsupported;
    QString code;
    QString detail;
    quint64 captureCycle = 0;

    bool isValid() const
    {
        if (correlationId.isEmpty() || correlationId.size() > 128
            || correlationId != correlationId.trimmed() || code.isEmpty() || code.size() > 128
            || code != code.trimmed()) {
            return false;
        }
        for (const QChar character : correlationId) {
            if (character.category() == QChar::Other_Control)
                return false;
        }
        for (const QChar character : code) {
            if (character.category() == QChar::Other_Control)
                return false;
        }

        const bool detailIsValid = !detail.isEmpty() && detail == detail.trimmed();
        switch (outcome) {
        case SemanticLiveRefreshOutcome::Accepted:
            return detail.isEmpty() && captureCycle == 0;
        case SemanticLiveRefreshOutcome::Refreshed:
            return detail.isEmpty() && captureCycle != 0;
        case SemanticLiveRefreshOutcome::Deferred:
        case SemanticLiveRefreshOutcome::Unsupported:
        case SemanticLiveRefreshOutcome::Rejected:
        case SemanticLiveRefreshOutcome::Failed:
            return detailIsValid && captureCycle == 0;
        }
        return false;
    }

    friend bool operator==(const SemanticLiveRefreshResult &, const SemanticLiveRefreshResult &)
        = default;
};

enum class SemanticRuntimeActorKind {
    User,
    Automation,
    System,
};

struct ETHERCATDATA_EXPORT SemanticRuntimeActor
{
    QString id;
    QString displayName;
    SemanticRuntimeActorKind kind = SemanticRuntimeActorKind::User;
    QString origin;
    QByteArray authenticationDigest;

    friend bool operator==(const SemanticRuntimeActor &, const SemanticRuntimeActor &) = default;
};

struct ETHERCATDATA_EXPORT SemanticOperationId
{
    QString value;

    friend bool operator==(const SemanticOperationId &, const SemanticOperationId &) = default;
};

inline size_t qHash(const SemanticOperationId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

enum class SemanticOperationKind {
    SetSignalValue,
    InvokeAction,
    ReleaseHold,
};

// Requests intentionally contain no RuntimeResourceId, PDO/SDO coordinates, or process-image
// offsets. The service resolves target semantic IDs through a verified SemanticRuntimeBinding.
struct ETHERCATDATA_EXPORT SemanticOperationRequest
{
    SemanticOperationId operationId;
    SemanticOperationKind kind = SemanticOperationKind::SetSignalValue;
    SemanticRuntimeTarget target;
    RuntimeResourceCatalogEpoch expectedEpoch;
    SemanticRuntimeDigest expectedMappingDigest;
    SemanticRuntimeDigest expectedControllerMappingDigest;
    SemanticRuntimeDigest expectedActionDefinitionDigest;
    QByteArray expectedContextHash;
    QVariant value;
    QMap<QString, QVariant> parameters;
    quint32 ttlMs = 0;
    quint32 ttlCycles = 0;
    QString reason;

    friend bool operator==(const SemanticOperationRequest &, const SemanticOperationRequest &)
        = default;
};

enum class SemanticOperationState {
    Rejected,
    Submitted,
    ApprovalRequired,
    Approved,
    Executing,
    Succeeded,
    Failed,
    TimedOut,
    OutcomeUnknown,
    Canceled,
    Expired,
};

enum class SemanticApprovalDecision {
    Pending,
    Approved,
    Denied,
};

struct ETHERCATDATA_EXPORT SemanticOperationApprovalRequest
{
    SemanticOperationId operationId;
    SemanticApprovalDecision decision = SemanticApprovalDecision::Pending;
    QByteArray challenge;
    QByteArray expectedRequestDigest;
    QByteArray expectedContextHash;
    QString detail;

    friend bool operator==(
        const SemanticOperationApprovalRequest &, const SemanticOperationApprovalRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticOperationApproval
{
    SemanticOperationApprovalRequest request;
    SemanticRuntimeActor actor;
    QDateTime decidedAt;

    friend bool operator==(const SemanticOperationApproval &, const SemanticOperationApproval &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticOperationCancelId
{
    QString value;

    friend bool operator==(const SemanticOperationCancelId &, const SemanticOperationCancelId &)
        = default;
};

inline size_t qHash(const SemanticOperationCancelId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

// Cancellation binds one idempotency key and CAS revision to the exact
// original operation intent. The authenticated actor is supplied separately
// to SemanticRuntimeService::cancel(). No current runtime context is carried:
// later authorization or topology drift must not block safe cleanup.
struct ETHERCATDATA_EXPORT SemanticOperationCancelRequest
{
    SemanticOperationCancelId cancelId;
    SemanticOperationId operationId;
    quint64 expectedRevision = 0;
    QByteArray expectedRequestDigest;
    QByteArray expectedContextHash;
    QString reason;

    friend bool operator==(
        const SemanticOperationCancelRequest &, const SemanticOperationCancelRequest &)
        = default;
};

enum class SemanticOperationCancellationPhase {
    Requested,
    WaitingForApplyResult,
    ProvingSafeHold,
    Completed,
};

struct ETHERCATDATA_EXPORT SemanticOperationCancellation
{
    SemanticOperationCancelRequest request;
    SemanticRuntimeActor actor;
    QByteArray canonicalCancelDigest;
    QDateTime requestedAt;
    SemanticOperationCancellationPhase phase = SemanticOperationCancellationPhase::Requested;
    // Completed without this proof means the CAS-bound cancellation occurred
    // before any provider mutation could have been sent. Once a write may have
    // executed, Completed requires the exact same transaction's SafeHold state.
    std::optional<RuntimeOutputTransactionState> safeHoldState;
    std::optional<RuntimeOutputTransactionRequest> pendingApplyRequest;
    std::optional<RuntimeOutputTransactionRequest> priorAppliedRequest;
    std::optional<RuntimeOutputTransactionRequest> safeHoldRequest;
    std::optional<RuntimeOutputTransactionResult> terminalApplyResult;

    friend bool operator==(
        const SemanticOperationCancellation &, const SemanticOperationCancellation &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticOperationSignalObservation
{
    SemanticRuntimeTarget target;
    RuntimeResourceTypedValue value;
    RuntimeResourceQuality quality;
    quint64 controllerTimestampNs = 0;

    friend bool operator==(
        const SemanticOperationSignalObservation &, const SemanticOperationSignalObservation &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticOperationSnapshot
{
    QByteArray digest;
    quint64 captureCycle = 0;
    quint64 controllerTimestampNs = 0;
    bool complete = false;
    QList<SemanticOperationSignalObservation> observations;

    friend bool operator==(const SemanticOperationSnapshot &, const SemanticOperationSnapshot &)
        = default;
};

struct ETHERCATDATA_EXPORT SemanticOperationRecord
{
    SemanticOperationRequest request;
    SemanticRuntimeActor actor;
    SemanticOperationState state = SemanticOperationState::Rejected;
    QList<SemanticOperationApproval> approvals;
    QByteArray canonicalRequestDigest;
    QByteArray approvalChallenge;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString resultCode;
    QString detail;
    bool executionAttempted = false;
    quint32 currentStep = 0;
    quint32 totalSteps = 0;
    quint32 failedStep = 0;
    std::optional<SemanticOperationSnapshot> beforeSnapshot;
    std::optional<SemanticOperationSnapshot> afterSnapshot;
    std::optional<ControllerOperationError> controllerError;
    quint64 appliedCycle = 0;
    quint64 appliedRuntimeGeneration = 0;
    quint64 revision = 0;
    std::optional<SemanticOperationCancellation> cancellation;

    friend bool operator==(const SemanticOperationRecord &, const SemanticOperationRecord &)
        = default;
};

enum class SemanticOperationCancelDisposition {
    Unsupported,
    Accepted,
    Replayed,
    NotFound,
    Stale,
    Conflict,
    Rejected,
};

struct ETHERCATDATA_EXPORT SemanticOperationCancelResult
{
    SemanticOperationCancelRequest request;
    SemanticOperationCancelDisposition disposition = SemanticOperationCancelDisposition::Rejected;
    std::optional<SemanticOperationRecord> record;
    QString code;
    QString detail;

    bool accepted() const
    {
        return disposition == SemanticOperationCancelDisposition::Accepted
               || disposition == SemanticOperationCancelDisposition::Replayed;
    }

    friend bool operator==(
        const SemanticOperationCancelResult &, const SemanticOperationCancelResult &)
        = default;
};

enum class SemanticAuditEventKind {
    Submitted,
    ApprovalRecorded,
    StateChanged,
    ExecutionStep,
    Rejected,
    ExecutionResult,
    OutcomeReconciled,
    CancellationRequested,
    CancellationProgressed,
};

struct ETHERCATDATA_EXPORT SemanticRuntimeAuditEvent
{
    quint64 sequence = 0;
    QString controllerId;
    SemanticOperationId operationId;
    SemanticAuditEventKind kind = SemanticAuditEventKind::Rejected;
    SemanticOperationState previousState = SemanticOperationState::Rejected;
    SemanticOperationState state = SemanticOperationState::Rejected;
    SemanticRuntimeActor actor;
    QByteArray canonicalRequestDigest;
    quint32 stepIndex = 0;
    QByteArray beforeSnapshotDigest;
    QByteArray afterSnapshotDigest;
    quint64 appliedCycle = 0;
    std::optional<ControllerOperationError> controllerError;
    QDateTime occurredAt;
    QString code;
    QString detail;
    std::optional<SemanticOperationCancelId> cancelId;
    QByteArray canonicalCancelDigest;
    QString cancellationReason;

    friend bool operator==(const SemanticRuntimeAuditEvent &, const SemanticRuntimeAuditEvent &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeTargetKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeTarget)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeDigest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticBindingVerificationState)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticBindingVerification)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeBinding)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalAvailability)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalRuntimeState)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticActionAvailability)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticActionQualification)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticActionParameterRuntimeDefinition)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticActionRuntimeState)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeContext)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticLiveRefreshSignal)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticLiveRefreshRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticLiveRefreshOutcome)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticLiveRefreshResult)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeActorKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeActor)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationId)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationState)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticApprovalDecision)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationApprovalRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationApproval)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationCancelId)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationCancelRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationCancellationPhase)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationCancellation)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationSignalObservation)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationSnapshot)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationRecord)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationCancelDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationCancelResult)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticAuditEventKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeAuditEvent)
