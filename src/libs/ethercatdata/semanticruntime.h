// Copyright (C) 2026 Kvell

#pragma once

#include "deviceadapter.h"
#include "ethercatdata_global.h"
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

    friend bool operator==(
        const SemanticBindingVerification &, const SemanticBindingVerification &) = default;
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

    friend bool operator==(
        const SemanticRuntimeBinding &, const SemanticRuntimeBinding &) = default;
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
    QString detail;

    friend bool operator==(
        const SemanticSignalRuntimeState &, const SemanticSignalRuntimeState &) = default;
};

enum class SemanticActionAvailability {
    Unavailable,
    Unverified,
    Stale,
    Ready,
    AwaitingApproval,
    Rejected,
};

struct ETHERCATDATA_EXPORT SemanticActionRuntimeState
{
    SemanticRuntimeTarget target;
    DeviceControlAction definition;
    SemanticActionAvailability availability = SemanticActionAvailability::Unavailable;
    QList<SemanticRuntimeBinding> bindings;
    bool requiresApproval = true;
    bool requiresExclusiveControl = true;
    bool holdToRun = false;
    quint32 maximumTtlMs = 0;
    QString detail;

    friend bool operator==(
        const SemanticActionRuntimeState &, const SemanticActionRuntimeState &) = default;
};

struct ETHERCATDATA_EXPORT SemanticRuntimeContext
{
    QString controllerId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    SemanticRuntimeDigest mappingDigest;
    SemanticRuntimeDigest controllerMappingDigest;
    SemanticBindingVerification bindingVerification;
    QByteArray contextHash;
    QList<SemanticSignalRuntimeState> signalStates;
    QList<SemanticActionRuntimeState> actionStates;
    bool complete = false;
    bool mock = false;
    QString detail;

    friend bool operator==(
        const SemanticRuntimeContext &, const SemanticRuntimeContext &) = default;
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
    QByteArray expectedContextHash;
    QVariant value;
    QMap<QString, QVariant> parameters;
    quint32 ttlMs = 0;
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
    quint64 appliedCycle = 0;
    quint64 appliedRuntimeGeneration = 0;

    friend bool operator==(const SemanticOperationRecord &, const SemanticOperationRecord &)
        = default;
};

enum class SemanticAuditEventKind {
    Submitted,
    ApprovalRecorded,
    StateChanged,
    Rejected,
    ExecutionResult,
};

struct ETHERCATDATA_EXPORT SemanticRuntimeAuditEvent
{
    quint64 sequence = 0;
    QString controllerId;
    SemanticOperationId operationId;
    SemanticAuditEventKind kind = SemanticAuditEventKind::Rejected;
    SemanticOperationState state = SemanticOperationState::Rejected;
    SemanticRuntimeActor actor;
    QByteArray canonicalRequestDigest;
    QDateTime occurredAt;
    QString code;
    QString detail;

    friend bool operator==(
        const SemanticRuntimeAuditEvent &, const SemanticRuntimeAuditEvent &) = default;
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
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticActionRuntimeState)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeContext)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeActorKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeActor)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationId)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationState)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticApprovalDecision)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationApprovalRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationApproval)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticOperationRecord)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticAuditEventKind)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticRuntimeAuditEvent)
