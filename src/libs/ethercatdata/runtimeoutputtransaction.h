// Copyright (C) 2026 Kvell

#pragma once

#include "runtimeresource.h"

#include <QByteArray>
#include <QDateTime>
#include <QFlags>
#include <QList>
#include <QMetaType>
#include <QString>

#include <algorithm>
#include <limits>
#include <optional>

namespace EtherCAT::Data {

// These types form the controller Provider's internal resource-level contract. Workbench,
// automation, and other public consumers must continue to use verified semantic signals/actions
// and must never accept RuntimeResourceId as a user-facing control coordinate.

// The semantic executor generates this 128-bit identifier. Providers retain and replay it but
// must not derive transport request IDs, controller addresses, or resource identities from it.
struct ETHERCATDATA_EXPORT RuntimeOutputOperationId
{
    QByteArray value;

    bool isValid() const
    {
        return value.size() == 16
               && std::any_of(value.cbegin(), value.cend(), [](char byte) { return byte != 0; });
    }

    friend bool operator==(const RuntimeOutputOperationId &, const RuntimeOutputOperationId &)
        = default;
};

inline size_t qHash(const RuntimeOutputOperationId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

enum class RuntimeOutputRecoveryPolicy {
    Unknown,
    ReturnTask,
    HoldSafe,
};

enum class RuntimeOutputState {
    Unknown,
    Idle,
    OverrideActive,
    SafeHold,
};

enum class RuntimeOutputTransactionResultFlag : quint32 {
    Replayed = quint32(1) << 0,
    OverrideActive = quint32(1) << 1,
    SafeHold = quint32(1) << 2,
    ReturnedTask = quint32(1) << 3,
};
Q_DECLARE_FLAGS(RuntimeOutputTransactionResultFlags, RuntimeOutputTransactionResultFlag)

inline constexpr quint32 RuntimeOutputTransactionResultKnownMask
    = quint32(RuntimeOutputTransactionResultFlag::Replayed)
      | quint32(RuntimeOutputTransactionResultFlag::OverrideActive)
      | quint32(RuntimeOutputTransactionResultFlag::SafeHold)
      | quint32(RuntimeOutputTransactionResultFlag::ReturnedTask);

enum class RuntimeOutputTransactionOutcome {
    Unknown,
    Applied,
    Rejected,
    OutcomeUnknown,
};

inline bool isValidRuntimeOutputDigest(const QByteArray &digest)
{
    return digest.size() == 32
           && std::any_of(digest.cbegin(), digest.cend(), [](char byte) { return byte != 0; });
}

inline bool isCompleteRuntimeOutputEpoch(const RuntimeResourceCatalogEpoch &epoch)
{
    return epoch.controllerBootId
           && (epoch.activePackageSlot == ControllerSlot::A
               || epoch.activePackageSlot == ControllerSlot::B)
           && epoch.activePackageGeneration && epoch.configurationId && epoch.topologyGeneration
           && epoch.runtimeGeneration && epoch.catalogRevision && !epoch.topologyIdentity.isEmpty();
}

inline bool isValidRuntimeOutputScope(
    const ControllerConnectionScope &scope, quint64 sessionGeneration)
{
    return !scope.projectId.isNull() && !scope.masterId.isNull() && sessionGeneration;
}

inline bool isValidRuntimeOutputCorrelationId(const QString &correlationId)
{
    if (correlationId.isEmpty() || correlationId.size() > 128
        || correlationId != correlationId.trimmed()) {
        return false;
    }
    return std::none_of(correlationId.cbegin(), correlationId.cend(), [](const QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

struct ETHERCATDATA_EXPORT RuntimeOutputGroupPolicyRequest
{
    QString correlationId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch expectedEpoch;
    RuntimeConsistencyGroupId consistencyGroupId;
    QByteArray expectedMappingDigest;

    bool isValid() const
    {
        return isValidRuntimeOutputCorrelationId(correlationId)
               && isValidRuntimeOutputScope(scope, sessionGeneration)
               && isCompleteRuntimeOutputEpoch(expectedEpoch) && consistencyGroupId.isValid()
               && isValidRuntimeOutputDigest(expectedMappingDigest);
    }

    friend bool operator==(
        const RuntimeOutputGroupPolicyRequest &, const RuntimeOutputGroupPolicyRequest &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputGroupPolicy
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    RuntimeConsistencyGroupId consistencyGroupId;
    QByteArray mappingDigest;
    QByteArray completeGroupRecordDigest;
    RuntimeOutputRecoveryPolicy recoveryPolicy = RuntimeOutputRecoveryPolicy::Unknown;
    quint32 maximumTtlCycles = 0;
    quint32 completeResourceCount = 0;
    quint64 currentOutputGeneration = 0;
    bool manualWriteAllowed = false;
    QDateTime receivedAt;

    bool isValid() const
    {
        return isValidRuntimeOutputScope(scope, sessionGeneration)
               && isCompleteRuntimeOutputEpoch(epoch) && consistencyGroupId.isValid()
               && isValidRuntimeOutputDigest(mappingDigest)
               && isValidRuntimeOutputDigest(completeGroupRecordDigest)
               && (recoveryPolicy == RuntimeOutputRecoveryPolicy::ReturnTask
                   || recoveryPolicy == RuntimeOutputRecoveryPolicy::HoldSafe)
               && maximumTtlCycles && maximumTtlCycles <= 65535 && completeResourceCount
               && completeResourceCount <= 64 && currentOutputGeneration && manualWriteAllowed;
    }

    friend bool operator==(const RuntimeOutputGroupPolicy &, const RuntimeOutputGroupPolicy &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputGroupPolicyResult
{
    RuntimeOutputGroupPolicyRequest request;
    std::optional<RuntimeOutputGroupPolicy> policy;
    std::optional<ControllerOperationError> error;

    bool isValid() const
    {
        if (!request.isValid() || policy.has_value() == error.has_value())
            return false;
        if (error)
            return error->operation == ControllerOperation::QueryRuntimeOutputGroupPolicy;
        return policy->isValid() && policy->scope == request.scope
               && policy->sessionGeneration == request.sessionGeneration
               && policy->epoch == request.expectedEpoch
               && policy->consistencyGroupId == request.consistencyGroupId
               && policy->mappingDigest == request.expectedMappingDigest;
    }

    friend bool operator==(
        const RuntimeOutputGroupPolicyResult &, const RuntimeOutputGroupPolicyResult &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputTransactionStateRequest
{
    QString correlationId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch expectedEpoch;
    QByteArray expectedMappingDigest;

    bool isValid() const
    {
        return isValidRuntimeOutputCorrelationId(correlationId)
               && isValidRuntimeOutputScope(scope, sessionGeneration)
               && isCompleteRuntimeOutputEpoch(expectedEpoch)
               && isValidRuntimeOutputDigest(expectedMappingDigest);
    }

    friend bool operator==(
        const RuntimeOutputTransactionStateRequest &,
        const RuntimeOutputTransactionStateRequest &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputTransactionState
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    QByteArray mappingDigest;
    RuntimeOutputState state = RuntimeOutputState::Unknown;
    RuntimeOutputTransactionResultFlags resultFlags;
    std::optional<RuntimeOutputOperationId> operationId;
    quint64 appliedCycle = 0;
    quint64 expiryCycle = 0;
    quint64 outputGeneration = 0;
    RuntimeConsistencyGroupId consistencyGroupId;
    quint32 ttlCycles = 0;
    RuntimeOutputRecoveryPolicy recoveryPolicy = RuntimeOutputRecoveryPolicy::Unknown;
    quint16 valueCount = 0;
    quint64 providerDetail = 0;
    quint64 controllerTimestampNs = 0;
    QDateTime receivedAt;

    bool isValid() const
    {
        if (!isValidRuntimeOutputScope(scope, sessionGeneration)
            || !isCompleteRuntimeOutputEpoch(epoch) || !isValidRuntimeOutputDigest(mappingDigest)
            || !outputGeneration || state == RuntimeOutputState::Unknown
            || (quint32(resultFlags.toInt()) & ~RuntimeOutputTransactionResultKnownMask)) {
            return false;
        }

        const bool replayed = resultFlags.testFlag(RuntimeOutputTransactionResultFlag::Replayed);
        const bool overrideActive = resultFlags.testFlag(
            RuntimeOutputTransactionResultFlag::OverrideActive);
        const bool safeHold = resultFlags.testFlag(RuntimeOutputTransactionResultFlag::SafeHold);
        const bool returnedTask = resultFlags.testFlag(
            RuntimeOutputTransactionResultFlag::ReturnedTask);

        if (!operationId) {
            return state == RuntimeOutputState::Idle && !replayed && !overrideActive && !safeHold
                   && !returnedTask && !appliedCycle && !expiryCycle
                   && !consistencyGroupId.isValid() && !ttlCycles
                   && recoveryPolicy == RuntimeOutputRecoveryPolicy::Unknown && !valueCount;
        }

        if (!operationId->isValid() || !appliedCycle || !consistencyGroupId.isValid() || !ttlCycles
            || ttlCycles > 65535 || !valueCount || valueCount > 64
            || appliedCycle > std::numeric_limits<quint64>::max() - ttlCycles
            || expiryCycle != appliedCycle + ttlCycles) {
            return false;
        }

        switch (state) {
        case RuntimeOutputState::Idle:
            return !overrideActive && !safeHold && returnedTask
                   && recoveryPolicy == RuntimeOutputRecoveryPolicy::ReturnTask;
        case RuntimeOutputState::OverrideActive:
            return overrideActive && !safeHold && !returnedTask
                   && (recoveryPolicy == RuntimeOutputRecoveryPolicy::ReturnTask
                       || recoveryPolicy == RuntimeOutputRecoveryPolicy::HoldSafe);
        case RuntimeOutputState::SafeHold:
            return !overrideActive && safeHold && !returnedTask
                   && recoveryPolicy == RuntimeOutputRecoveryPolicy::HoldSafe;
        case RuntimeOutputState::Unknown:
            return false;
        }
        return false;
    }

    friend bool operator==(
        const RuntimeOutputTransactionState &, const RuntimeOutputTransactionState &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputTransactionStateResult
{
    RuntimeOutputTransactionStateRequest request;
    std::optional<RuntimeOutputTransactionState> state;
    std::optional<ControllerOperationError> error;

    bool isValid() const
    {
        if (!request.isValid() || state.has_value() == error.has_value())
            return false;
        if (error)
            return error->operation == ControllerOperation::QueryRuntimeOutputTransactionState;
        return state->isValid() && state->scope == request.scope
               && state->sessionGeneration == request.sessionGeneration
               && state->epoch == request.expectedEpoch
               && state->mappingDigest == request.expectedMappingDigest;
    }

    friend bool operator==(
        const RuntimeOutputTransactionStateResult &,
        const RuntimeOutputTransactionStateResult &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputValueWrite
{
    RuntimeResourceId resourceId;
    quint16 bitWidth = 0;
    RuntimeResourceTypedValue value;

    bool isValid() const
    {
        if (!resourceId.isValid() || !bitWidth || bitWidth > 128 || !value.value.isValid()
            || !value.opaqueRepresentation.isEmpty() || value.typeIdentity.size() > 128) {
            return false;
        }

        const int typeId = value.value.metaType().id();
        switch (value.primitiveType) {
        case RuntimeResourcePrimitiveType::Boolean:
            return bitWidth == 1 && typeId == QMetaType::Bool;
        case RuntimeResourcePrimitiveType::SignedInteger: {
            if (bitWidth > 64 || typeId != QMetaType::LongLong) {
                return false;
            }
            if (bitWidth == 64)
                return true;
            const qint64 signedValue = value.value.toLongLong();
            const qint64 limit = qint64(1) << (bitWidth - 1);
            return signedValue >= -limit && signedValue < limit;
        }
        case RuntimeResourcePrimitiveType::UnsignedInteger: {
            if (bitWidth > 64 || typeId != QMetaType::ULongLong) {
                return false;
            }
            if (bitWidth == 64)
                return true;
            return value.value.toULongLong() < (quint64(1) << bitWidth);
        }
        case RuntimeResourcePrimitiveType::FloatingPoint:
            // A controller primitive described as Q32.32 is not an IEEE floating-point value.
            // Until an exact engineering-rational to Q32.32 contract is available, accepting
            // float or double here would introduce silent rounding at the safety boundary.
            return false;
        case RuntimeResourcePrimitiveType::ByteArray: {
            if (typeId != QMetaType::QByteArray)
                return false;
            const QByteArray bytes = value.value.toByteArray();
            const qsizetype expectedBytes = (bitWidth + 7) / 8;
            if (bytes.size() != expectedBytes)
                return false;
            const int unusedHighBits = int(expectedBytes * 8 - bitWidth);
            if (!unusedHighBits)
                return true;
            const quint8 highByte = quint8(bytes.at(0));
            return !(highByte & quint8(0xff << (8 - unusedHighBits)));
        }
        case RuntimeResourcePrimitiveType::Opaque:
        case RuntimeResourcePrimitiveType::Text:
            return false;
        }
        return false;
    }

    friend bool operator==(const RuntimeOutputValueWrite &, const RuntimeOutputValueWrite &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputTransactionRequest
{
    RuntimeOutputOperationId operationId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch expectedEpoch;
    QByteArray expectedMappingDigest;
    QByteArray expectedCompleteGroupRecordDigest;
    quint32 expectedCompleteResourceCount = 0;
    RuntimeOutputRecoveryPolicy expectedRecoveryPolicy = RuntimeOutputRecoveryPolicy::Unknown;
    quint32 expectedMaximumTtlCycles = 0;
    quint64 expectedOutputGeneration = 0;
    quint32 ttlCycles = 0;
    RuntimeConsistencyGroupId consistencyGroupId;
    // This must contain every resource from the signed policy group exactly once. Structural
    // validation enforces canonical order; the provider must verify completeness against policy.
    QList<RuntimeOutputValueWrite> completeGroupWrites;

    bool isValid() const
    {
        if (!operationId.isValid() || !isValidRuntimeOutputScope(scope, sessionGeneration)
            || !isCompleteRuntimeOutputEpoch(expectedEpoch)
            || !isValidRuntimeOutputDigest(expectedMappingDigest)
            || !isValidRuntimeOutputDigest(expectedCompleteGroupRecordDigest)
            || !expectedCompleteResourceCount || expectedCompleteResourceCount > 64
            || (expectedRecoveryPolicy != RuntimeOutputRecoveryPolicy::ReturnTask
                && expectedRecoveryPolicy != RuntimeOutputRecoveryPolicy::HoldSafe)
            || !expectedMaximumTtlCycles || expectedMaximumTtlCycles > 65535
            || !expectedOutputGeneration
            || expectedOutputGeneration == std::numeric_limits<quint64>::max() || !ttlCycles
            || ttlCycles > expectedMaximumTtlCycles
            || !consistencyGroupId.isValid()
            || quint32(completeGroupWrites.size()) != expectedCompleteResourceCount) {
            return false;
        }

        QByteArray previous;
        for (const RuntimeOutputValueWrite &write : completeGroupWrites) {
            if (!write.isValid() || (!previous.isEmpty() && write.resourceId.value <= previous)) {
                return false;
            }
            previous = write.resourceId.value;
        }
        return true;
    }

    friend bool operator==(
        const RuntimeOutputTransactionRequest &, const RuntimeOutputTransactionRequest &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeOutputTransactionResult
{
    RuntimeOutputTransactionRequest request;
    RuntimeOutputTransactionOutcome outcome = RuntimeOutputTransactionOutcome::Unknown;
    // True only after the provider decoded a valid terminal controller response. A disconnect,
    // timeout, or framing failure after transmission leaves this false and the outcome unknown;
    // callers must reconcile with the same OperationId instead of issuing a new mutation.
    bool finalResponseObserved = false;
    std::optional<RuntimeOutputTransactionState> state;
    std::optional<ControllerOperationError> error;

    bool isValid() const
    {
        if (!request.isValid())
            return false;

        if (outcome == RuntimeOutputTransactionOutcome::Applied) {
            if (!finalResponseObserved || !state || error || !state->isValid()
                || state->state != RuntimeOutputState::OverrideActive || !state->operationId
                || *state->operationId != request.operationId || state->scope != request.scope
                || state->sessionGeneration != request.sessionGeneration
                || state->epoch != request.expectedEpoch
                || state->mappingDigest != request.expectedMappingDigest
                || state->consistencyGroupId != request.consistencyGroupId
                || state->recoveryPolicy != request.expectedRecoveryPolicy
                || state->ttlCycles != request.ttlCycles
                || state->valueCount != quint16(request.completeGroupWrites.size())
                || state->outputGeneration != request.expectedOutputGeneration + 1) {
                return false;
            }
            return true;
        }

        if (outcome == RuntimeOutputTransactionOutcome::Rejected
            || outcome == RuntimeOutputTransactionOutcome::OutcomeUnknown) {
            return !state && error
                   && finalResponseObserved
                          == (outcome == RuntimeOutputTransactionOutcome::Rejected)
                   && error->operation == ControllerOperation::ApplyRuntimeOutputTransaction;
        }
        return false;
    }

    friend bool operator==(
        const RuntimeOutputTransactionResult &, const RuntimeOutputTransactionResult &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_OPERATORS_FOR_FLAGS(EtherCAT::Data::RuntimeOutputTransactionResultFlags)

Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputOperationId)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputRecoveryPolicy)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputState)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionResultFlag)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionResultFlags)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionOutcome)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputGroupPolicyRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputGroupPolicy)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputGroupPolicyResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionStateRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionState)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionStateResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputValueWrite)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeOutputTransactionResult)
