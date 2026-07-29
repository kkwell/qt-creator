// Copyright (C) 2026 Kvell

#include "semanticruntimeservice.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace EtherCAT::Core {
namespace {

SemanticRuntimeValidation rejection(SemanticRuntimeValidationError error, const QString &detail)
{
    return {error, detail};
}

bool digestsMatch(const Data::SemanticRuntimeDigest &left, const Data::SemanticRuntimeDigest &right)
{
    if (!isCanonicalSha256Digest(left) || !isCanonicalSha256Digest(right)
        || left.algorithm != right.algorithm || left.value.size() != right.value.size()) {
        return false;
    }

    uchar difference = 0;
    for (qsizetype index = 0; index < left.value.size(); ++index) {
        difference |= static_cast<uchar>(left.value.at(index))
                      ^ static_cast<uchar>(right.value.at(index));
    }
    return difference == 0;
}

bool sha256BytesAreValid(const QByteArray &bytes)
{
    return bytes.size() == 32;
}

bool descriptorMatchesBinding(
    const Data::RuntimeResourceDescriptor &descriptor, const Data::SemanticRuntimeBinding &binding)
{
    return descriptor.id == binding.resourceId
           && descriptor.componentInstanceId == binding.componentInstanceId
           && descriptor.consistencyGroupId == binding.consistencyGroupId
           && descriptor.primitiveType == binding.primitiveType
           && descriptor.valueTypeIdentity == binding.valueTypeIdentity
           && descriptor.bitWidth == binding.bitWidth && descriptor.direction == binding.direction
           && descriptor.access == binding.access;
}

bool sampleMatchesBinding(
    const Data::RuntimeResourceSample &sample, const Data::SemanticRuntimeBinding &binding)
{
    return sample.resourceId == binding.resourceId
           && sample.consistencyGroupId == binding.consistencyGroupId
           && sample.value.primitiveType == binding.primitiveType
           && sample.value.typeIdentity == binding.valueTypeIdentity
           && isSemanticRuntimeValueCompatible(sample.value.value, binding);
}

bool semanticActionParameterValueIsCompatible(
    const QVariant &value, const Data::SemanticActionParameterRuntimeDefinition &parameter)
{
    if (!isAllowedSemanticRuntimeValue(value) || !parameter.minimum.isValid()
        || !parameter.maximum.isValid()) {
        return false;
    }

    switch (parameter.primitiveType) {
    case Data::RuntimeResourcePrimitiveType::Boolean:
        return value.metaType().id() == QMetaType::Bool
               && parameter.minimum.metaType().id() == QMetaType::Bool
               && parameter.maximum.metaType().id() == QMetaType::Bool
               && int(value.toBool()) >= int(parameter.minimum.toBool())
               && int(value.toBool()) <= int(parameter.maximum.toBool());
    case Data::RuntimeResourcePrimitiveType::SignedInteger:
        return value.metaType().id() == QMetaType::LongLong
               && parameter.minimum.metaType().id() == QMetaType::LongLong
               && parameter.maximum.metaType().id() == QMetaType::LongLong
               && value.toLongLong() >= parameter.minimum.toLongLong()
               && value.toLongLong() <= parameter.maximum.toLongLong();
    case Data::RuntimeResourcePrimitiveType::UnsignedInteger:
        return value.metaType().id() == QMetaType::ULongLong
               && parameter.minimum.metaType().id() == QMetaType::ULongLong
               && parameter.maximum.metaType().id() == QMetaType::ULongLong
               && value.toULongLong() >= parameter.minimum.toULongLong()
               && value.toULongLong() <= parameter.maximum.toULongLong();
    case Data::RuntimeResourcePrimitiveType::ByteArray:
        return value.metaType().id() == QMetaType::QByteArray
               && parameter.minimum.metaType().id() == QMetaType::QByteArray
               && parameter.maximum.metaType().id() == QMetaType::QByteArray
               && value.toByteArray() >= parameter.minimum.toByteArray()
               && value.toByteArray() <= parameter.maximum.toByteArray();
    case Data::RuntimeResourcePrimitiveType::FloatingPoint:
    case Data::RuntimeResourcePrimitiveType::Text:
    case Data::RuntimeResourcePrimitiveType::Opaque:
        return false;
    }
    return false;
}

SemanticRuntimeValidation validateBindingAgainstContext(
    const Data::SemanticRuntimeBinding &binding,
    const Data::SemanticRuntimeContext &context,
    const QString &subject)
{
    const SemanticRuntimeValidation bindingValidation = validateSemanticRuntimeBinding(binding);
    if (!bindingValidation.accepted())
        return bindingValidation;

    // SemanticBindingVerification is a value-semantic proof record. A binding must carry the
    // exact proof published by its context, including the verifier, signed manifest, verification
    // time, and revocation/detail state. Matching only the independently valid proof state would
    // allow a binding from another signed context to be spliced into this one.
    if (binding.verification != context.bindingVerification) {
        return rejection(
            SemanticRuntimeValidationError::BindingUnverified,
            QStringLiteral("%1 binding proof differs from the runtime context.").arg(subject));
    }
    if (binding.sessionGeneration != context.sessionGeneration || binding.epoch != context.epoch
        || !digestsMatch(binding.mappingDigest, context.mappingDigest)
        || !digestsMatch(binding.controllerMappingDigest, context.controllerMappingDigest)) {
        return rejection(
            SemanticRuntimeValidationError::EpochMismatch,
            QStringLiteral("%1 binding differs from the runtime context.").arg(subject));
    }
    return {};
}

void writeDigest(QDataStream &stream, const Data::SemanticRuntimeDigest &digest)
{
    stream << digest.algorithm << digest.value;
}

void writeTarget(QDataStream &stream, const Data::SemanticRuntimeTarget &target)
{
    stream << target.controllerId << target.scope.projectId.toString()
           << target.scope.masterId.toString() << target.deviceId.toString() << quint32(target.kind)
           << target.signalId.value << target.actionId.value;
}

void writeEpoch(QDataStream &stream, const Data::RuntimeResourceCatalogEpoch &epoch)
{
    stream << epoch.controllerBootId << quint32(epoch.activePackageSlot)
           << epoch.activePackageGeneration << epoch.configurationId << epoch.topologyGeneration
           << epoch.runtimeGeneration << epoch.catalogRevision << epoch.topologyIdentity;
}

Data::SemanticOperationRecord rejectedRecord(
    const Data::SemanticOperationRequest &request,
    const Data::SemanticRuntimeActor &actor,
    const QString &detail)
{
    Data::SemanticOperationRecord record;
    record.request = request;
    record.actor = actor;
    record.state = Data::SemanticOperationState::Rejected;
    const QByteArray canonical = canonicalSemanticOperationRequest(request);
    if (!canonical.isEmpty()) {
        record.canonicalRequestDigest
            = QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
    }
    record.createdAt = QDateTime::currentDateTimeUtc();
    record.updatedAt = record.createdAt;
    record.resultCode = QStringLiteral("semantic-runtime-unavailable");
    record.detail = detail;
    record.executionAttempted = false;
    return record;
}

} // namespace

bool isValidSemanticRuntimeTarget(const Data::SemanticRuntimeTarget &target)
{
    if (target.controllerId.isEmpty() || target.scope.projectId.isNull()
        || target.scope.masterId.isNull() || target.deviceId.isNull()) {
        return false;
    }

    switch (target.kind) {
    case Data::SemanticRuntimeTargetKind::Signal:
        return !target.signalId.value.isEmpty() && target.actionId.value.isEmpty();
    case Data::SemanticRuntimeTargetKind::Action:
        return target.signalId.value.isEmpty() && !target.actionId.value.isEmpty();
    }
    return false;
}

bool isCompleteRuntimeResourceCatalogEpoch(const Data::RuntimeResourceCatalogEpoch &epoch)
{
    return epoch.controllerBootId != 0
           && (epoch.activePackageSlot == Data::ControllerSlot::A
               || epoch.activePackageSlot == Data::ControllerSlot::B)
           && epoch.activePackageGeneration != 0 && epoch.configurationId != 0
           && epoch.topologyGeneration != 0 && epoch.runtimeGeneration != 0
           && epoch.catalogRevision != 0 && !epoch.topologyIdentity.isEmpty();
}

bool isCanonicalSha256Digest(const Data::SemanticRuntimeDigest &digest)
{
    return digest.algorithm == QStringLiteral("sha256") && sha256BytesAreValid(digest.value);
}

bool isCanonicalSemanticOperationId(const Data::SemanticOperationId &operationId)
{
    if (operationId.value.isEmpty() || operationId.value.size() > 128)
        return false;
    return std::none_of(operationId.value.cbegin(), operationId.value.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

bool isAllowedSemanticRuntimeValue(const QVariant &value)
{
    if (!value.isValid())
        return false;

    switch (value.metaType().id()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QString:
    case QMetaType::QByteArray:
        return true;
    case QMetaType::Float:
    case QMetaType::Double:
        return std::isfinite(value.toDouble());
    default:
        return false;
    }
}

bool isSemanticRuntimeValueCompatible(
    const QVariant &value, const Data::SemanticRuntimeBinding &binding)
{
    if (!isAllowedSemanticRuntimeValue(value) || binding.bitWidth == 0)
        return false;

    switch (binding.primitiveType) {
    case Data::RuntimeResourcePrimitiveType::Boolean:
        return binding.bitWidth == 1 && value.metaType().id() == QMetaType::Bool;
    case Data::RuntimeResourcePrimitiveType::SignedInteger: {
        if (binding.bitWidth > 64 || value.metaType().id() != QMetaType::LongLong)
            return false;
        if (binding.bitWidth == 64)
            return true;
        const qint64 magnitude = qint64(1) << (binding.bitWidth - 1);
        const qint64 number = value.toLongLong();
        return number >= -magnitude && number < magnitude;
    }
    case Data::RuntimeResourcePrimitiveType::UnsignedInteger: {
        if (binding.bitWidth > 64 || value.metaType().id() != QMetaType::ULongLong)
            return false;
        if (binding.bitWidth == 64)
            return true;
        const quint64 maximum = (quint64(1) << binding.bitWidth) - 1;
        return value.toULongLong() <= maximum;
    }
    case Data::RuntimeResourcePrimitiveType::FloatingPoint:
        return (binding.bitWidth == 32 || binding.bitWidth == 64)
               && value.metaType().id() == QMetaType::Double && std::isfinite(value.toDouble());
    case Data::RuntimeResourcePrimitiveType::Text:
        return value.metaType().id() == QMetaType::QString;
    case Data::RuntimeResourcePrimitiveType::ByteArray:
        return binding.bitWidth <= 128 && value.metaType().id() == QMetaType::QByteArray
               && value.toByteArray().size() == qsizetype((binding.bitWidth + 7) / 8);
    case Data::RuntimeResourcePrimitiveType::Opaque:
        return false;
    }
    return false;
}

QByteArray canonicalSemanticOperationRequest(const Data::SemanticOperationRequest &request)
{
    if (!isCanonicalSemanticOperationId(request.operationId)
        || !isValidSemanticRuntimeTarget(request.target)
        || (request.value.isValid() && !isAllowedSemanticRuntimeValue(request.value))
        || !std::all_of(
            request.parameters.cbegin(), request.parameters.cend(), [](const QVariant &value) {
                return isAllowedSemanticRuntimeValue(value);
            })) {
        return {};
    }

    QByteArray canonical;
    QDataStream stream(&canonical, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setVersion(QDataStream::Qt_6_0);
    // This is a local IDE journal encoding, not a transport or persistent file format. The
    // OperationId is deliberately excluded, matching the Gateway request-fingerprint contract.
    stream << QByteArrayLiteral("embed-labs-semantic-operation-v2");
    stream << quint32(request.kind);
    writeTarget(stream, request.target);
    writeEpoch(stream, request.expectedEpoch);
    writeDigest(stream, request.expectedMappingDigest);
    writeDigest(stream, request.expectedControllerMappingDigest);
    writeDigest(stream, request.expectedActionDefinitionDigest);
    stream << request.expectedContextHash << request.value << request.parameters << request.ttlMs
           << request.ttlCycles << request.reason;
    if (stream.status() != QDataStream::Ok)
        return {};
    return canonical;
}

bool semanticOperationRequestsCanonicallyEqual(
    const Data::SemanticOperationRequest &left, const Data::SemanticOperationRequest &right)
{
    const QByteArray leftCanonical = canonicalSemanticOperationRequest(left);
    return !leftCanonical.isEmpty() && leftCanonical == canonicalSemanticOperationRequest(right);
}

SemanticRuntimeValidation validateSemanticRuntimeEpoch(
    const Data::RuntimeResourceCatalogEpoch &expected,
    const Data::RuntimeResourceCatalogEpoch &actual)
{
    if (!isCompleteRuntimeResourceCatalogEpoch(expected)
        || !isCompleteRuntimeResourceCatalogEpoch(actual)) {
        return rejection(
            SemanticRuntimeValidationError::InvalidEpoch,
            QStringLiteral("Runtime resource epoch is incomplete."));
    }
    if (expected != actual) {
        return rejection(
            SemanticRuntimeValidationError::EpochMismatch,
            QStringLiteral("Runtime resource epoch changed."));
    }
    return {};
}

SemanticRuntimeValidation validateSemanticRuntimeBinding(const Data::SemanticRuntimeBinding &binding)
{
    if (!isValidSemanticRuntimeTarget(binding.target)
        || binding.target.kind != Data::SemanticRuntimeTargetKind::Signal
        || binding.semanticBindingId.isEmpty() || binding.componentBindingId.isEmpty()
        || binding.adapterId.value.isEmpty() || binding.adapterVersion.isEmpty()
        || !sha256BytesAreValid(binding.adapterContentSha256)
        || !sha256BytesAreValid(binding.esiSha256)
        || !sha256BytesAreValid(binding.bindingArtifactSha256) || binding.sessionGeneration == 0
        || !binding.resourceId.isValid() || !binding.componentInstanceId.isValid()
        || !binding.consistencyGroupId.isValid() || binding.valueTypeIdentity.isEmpty()
        || binding.bitWidth == 0 || binding.direction == Data::RuntimeResourceDirection::Unknown
        || binding.access == Data::RuntimeResourceAccess::Unknown) {
        return rejection(
            SemanticRuntimeValidationError::InvalidBinding,
            QStringLiteral("Semantic runtime binding is incomplete."));
    }

    const SemanticRuntimeValidation epochValidation
        = validateSemanticRuntimeEpoch(binding.epoch, binding.epoch);
    if (!epochValidation.accepted())
        return epochValidation;

    if (binding.verification.state != Data::SemanticBindingVerificationState::Verified
        || binding.verification.verifierId.isEmpty()
        || !isCanonicalSha256Digest(binding.verification.signedManifestDigest)) {
        return rejection(
            SemanticRuntimeValidationError::BindingUnverified,
            QStringLiteral("Semantic runtime binding is not verified."));
    }
    if (!isCanonicalSha256Digest(binding.mappingDigest)
        || !isCanonicalSha256Digest(binding.controllerMappingDigest)) {
        return rejection(
            SemanticRuntimeValidationError::MappingDigestMissing,
            QStringLiteral("Semantic mapping proof is incomplete."));
    }
    if (!digestsMatch(binding.mappingDigest, binding.controllerMappingDigest)) {
        return rejection(
            SemanticRuntimeValidationError::MappingDigestMismatch,
            QStringLiteral("IDE and controller semantic mapping digests differ."));
    }
    return {};
}

SemanticRuntimeValidation validateSemanticOperationRequest(
    const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeContext &context)
{
    if (!isCanonicalSemanticOperationId(request.operationId)) {
        return rejection(
            SemanticRuntimeValidationError::InvalidOperationId,
            QStringLiteral(
                "OperationId must contain 1 to 128 characters and no control characters."));
    }
    if (!isValidSemanticRuntimeTarget(request.target)) {
        return rejection(
            SemanticRuntimeValidationError::InvalidTarget,
            QStringLiteral("Semantic operation target is invalid."));
    }
    const bool operationKindKnown = request.kind == Data::SemanticOperationKind::SetSignalValue
                                    || request.kind == Data::SemanticOperationKind::InvokeAction
                                    || request.kind == Data::SemanticOperationKind::ReleaseHold;
    const bool parametersValid = std::all_of(
        request.parameters.cbegin(), request.parameters.cend(), [](const QVariant &value) {
            return isAllowedSemanticRuntimeValue(value);
        });
    if (!operationKindKnown || !sha256BytesAreValid(request.expectedContextHash) || !parametersValid
        || (request.kind == Data::SemanticOperationKind::SetSignalValue
            && (request.target.kind != Data::SemanticRuntimeTargetKind::Signal
                || !isAllowedSemanticRuntimeValue(request.value) || request.ttlMs == 0
                || request.ttlCycles != 0 || !request.parameters.isEmpty()
                || request.expectedActionDefinitionDigest != Data::SemanticRuntimeDigest{}))
        || (request.kind == Data::SemanticOperationKind::InvokeAction
            && (request.target.kind != Data::SemanticRuntimeTargetKind::Action
                || request.value.isValid() || request.ttlMs != 0 || request.ttlCycles == 0
                || !isCanonicalSha256Digest(request.expectedActionDefinitionDigest)))
        || request.kind == Data::SemanticOperationKind::ReleaseHold) {
        return rejection(
            SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation request is incomplete."));
    }
    if (!context.complete || context.controllerId.isEmpty() || context.sessionGeneration == 0
        || !sha256BytesAreValid(context.contextHash)
        || context.bindingVerification.state != Data::SemanticBindingVerificationState::Verified
        || context.bindingVerification.verifierId.isEmpty()
        || !isCanonicalSha256Digest(context.bindingVerification.signedManifestDigest)) {
        return rejection(
            SemanticRuntimeValidationError::BindingUnverified,
            QStringLiteral("Semantic runtime context is not verified and complete."));
    }
    if (request.target.controllerId != context.controllerId
        || request.target.scope != context.scope) {
        return rejection(
            SemanticRuntimeValidationError::ScopeMismatch,
            QStringLiteral("Semantic operation target is outside the runtime context."));
    }

    const SemanticRuntimeValidation epochValidation
        = validateSemanticRuntimeEpoch(request.expectedEpoch, context.epoch);
    if (!epochValidation.accepted())
        return epochValidation;

    if (!isCanonicalSha256Digest(request.expectedMappingDigest)
        || !isCanonicalSha256Digest(request.expectedControllerMappingDigest)
        || !isCanonicalSha256Digest(context.mappingDigest)
        || !isCanonicalSha256Digest(context.controllerMappingDigest)) {
        return rejection(
            SemanticRuntimeValidationError::MappingDigestMissing,
            QStringLiteral("Semantic operation mapping proof is incomplete."));
    }
    if (!digestsMatch(context.mappingDigest, context.controllerMappingDigest)
        || !digestsMatch(request.expectedMappingDigest, context.mappingDigest)
        || !digestsMatch(request.expectedControllerMappingDigest, context.controllerMappingDigest)) {
        return rejection(
            SemanticRuntimeValidationError::MappingDigestMismatch,
            QStringLiteral("Semantic operation mapping proof changed."));
    }
    if (request.expectedContextHash != context.contextHash) {
        return rejection(
            SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic runtime context changed."));
    }

    if (request.target.kind == Data::SemanticRuntimeTargetKind::Signal) {
        QList<Data::SemanticSignalRuntimeState> matches;
        std::copy_if(
            context.signalStates.cbegin(),
            context.signalStates.cend(),
            std::back_inserter(matches),
            [&request](const Data::SemanticSignalRuntimeState &state) {
                return state.target == request.target;
            });
        if (matches.size() != 1
            || matches.constFirst().availability != Data::SemanticSignalAvailability::Ready
            || !matches.constFirst().binding) {
            return rejection(
                SemanticRuntimeValidationError::InvalidTarget,
                QStringLiteral("Semantic signal is not uniquely bound and ready."));
        }
        const Data::SemanticSignalRuntimeState &state = matches.constFirst();
        const Data::SemanticRuntimeBinding &binding = *state.binding;
        if (state.definition.id != state.target.signalId) {
            return rejection(
                SemanticRuntimeValidationError::InvalidTarget,
                QStringLiteral("Semantic signal definition differs from its target."));
        }
        if (binding.target != state.target) {
            return rejection(
                SemanticRuntimeValidationError::InvalidBinding,
                QStringLiteral("Semantic signal binding differs from its target."));
        }
        const SemanticRuntimeValidation bindingValidation
            = validateBindingAgainstContext(binding, context, QStringLiteral("Semantic signal"));
        if (!bindingValidation.accepted())
            return bindingValidation;
        if (request.kind == Data::SemanticOperationKind::SetSignalValue
            && ((binding.direction != Data::RuntimeResourceDirection::Output
                 && binding.direction != Data::RuntimeResourceDirection::Bidirectional)
                || (binding.access != Data::RuntimeResourceAccess::WriteOnly
                    && binding.access != Data::RuntimeResourceAccess::ReadWrite)
                || !isSemanticRuntimeValueCompatible(request.value, binding)
                || !state.definition.manualControl.allowed
                || state.definition.manualControl.commandTimeoutMs == 0
                || request.ttlMs > state.definition.manualControl.commandTimeoutMs)) {
            return rejection(
                SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic signal is not approved for manual writes."));
        }
        if (request.kind == Data::SemanticOperationKind::ReleaseHold
            && !state.definition.manualControl.holdToRun) {
            return rejection(
                SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic signal has no hold-to-run operation."));
        }
    } else {
        QList<Data::SemanticActionRuntimeState> matches;
        std::copy_if(
            context.actionStates.cbegin(),
            context.actionStates.cend(),
            std::back_inserter(matches),
            [&request](const Data::SemanticActionRuntimeState &state) {
                return state.target == request.target;
            });
        if (matches.size() != 1
            || matches.constFirst().availability != Data::SemanticActionAvailability::Ready
            || !matches.constFirst().definition.enabled
            || !isCanonicalSha256Digest(context.actionDefinitionsDigest)
            || context.cyclePeriodNs == 0
            || matches.constFirst().qualification != Data::SemanticActionQualification::Qualified
            || !matches.constFirst().disabledReason.isEmpty()
            || matches.constFirst().actionBindingId != request.target.actionId.value
            || matches.constFirst().actionDefinitionId.isEmpty()
            || !isCanonicalSha256Digest(matches.constFirst().actionDefinitionDigest)
            || !digestsMatch(
                request.expectedActionDefinitionDigest,
                matches.constFirst().actionDefinitionDigest)
            || matches.constFirst().bindings.isEmpty()
            || matches.constFirst().maximumTtlCycles == 0
            || matches.constFirst().maximumTtlCycles > 65535) {
            return rejection(
                SemanticRuntimeValidationError::InvalidTarget,
                QStringLiteral("Semantic action is not uniquely bound and ready."));
        }
        const Data::SemanticActionRuntimeState &state = matches.constFirst();
        if (state.definition.id != state.target.actionId) {
            return rejection(
                SemanticRuntimeValidationError::InvalidTarget,
                QStringLiteral("Semantic action definition differs from its target."));
        }
        if (request.kind == Data::SemanticOperationKind::InvokeAction
            && request.ttlCycles > state.maximumTtlCycles) {
            return rejection(
                SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic action TTL exceeds the signed cycle limit."));
        }
        if (request.kind == Data::SemanticOperationKind::ReleaseHold && !state.holdToRun) {
            return rejection(
                SemanticRuntimeValidationError::InvalidRequest,
                QStringLiteral("Semantic action has no hold-to-run operation."));
        }
        if (request.kind == Data::SemanticOperationKind::InvokeAction) {
            if (request.parameters.size() != state.parameters.size()) {
                return rejection(
                    SemanticRuntimeValidationError::InvalidRequest,
                    QStringLiteral("Semantic action parameters are incomplete."));
            }
            QSet<QString> parameterIds;
            for (const Data::SemanticActionParameterRuntimeDefinition &parameter :
                 state.parameters) {
                const auto value = request.parameters.constFind(parameter.id);
                if (parameter.id.isEmpty() || parameterIds.contains(parameter.id)
                    || value == request.parameters.cend()
                    || !semanticActionParameterValueIsCompatible(*value, parameter)) {
                    return rejection(
                        SemanticRuntimeValidationError::InvalidRequest,
                        QStringLiteral(
                            "Semantic action parameters differ from the signed definition."));
                }
                parameterIds.insert(parameter.id);
            }
        }
        QList<Data::RuntimeResourceId> resourceIds;
        QList<Data::SemanticRuntimeTarget> bindingTargets;
        for (const Data::SemanticRuntimeBinding &binding : state.bindings) {
            if (binding.target.controllerId != state.target.controllerId
                || binding.target.scope != state.target.scope
                || binding.target.deviceId != state.target.deviceId
                || resourceIds.contains(binding.resourceId)
                || bindingTargets.contains(binding.target)) {
                return rejection(
                    SemanticRuntimeValidationError::InvalidBinding,
                    QStringLiteral("Semantic action contains a mismatched or duplicate binding."));
            }
            resourceIds.append(binding.resourceId);
            bindingTargets.append(binding.target);
            const SemanticRuntimeValidation bindingValidation
                = validateBindingAgainstContext(binding, context, QStringLiteral("Semantic action"));
            if (!bindingValidation.accepted())
                return bindingValidation;
        }
    }
    return {};
}

SemanticRuntimeValidation validateSemanticOperationApproval(
    const Data::SemanticOperationApprovalRequest &approval,
    const Data::SemanticRuntimeActor &actor,
    const Data::SemanticOperationRecord &operation,
    const Data::SemanticRuntimeContext &context)
{
    if (operation.state != Data::SemanticOperationState::ApprovalRequired) {
        return rejection(
            SemanticRuntimeValidationError::ApprovalNotRequired,
            QStringLiteral("Semantic operation is not waiting for approval."));
    }
    if (!isCanonicalSemanticOperationId(approval.operationId)
        || approval.operationId != operation.request.operationId
        || (approval.decision != Data::SemanticApprovalDecision::Approved
            && approval.decision != Data::SemanticApprovalDecision::Denied)) {
        return rejection(
            SemanticRuntimeValidationError::InvalidRequest,
            QStringLiteral("Semantic operation approval is invalid."));
    }
    if (actor.kind != Data::SemanticRuntimeActorKind::User || actor.id.isEmpty()
        || !sha256BytesAreValid(actor.authenticationDigest)) {
        return rejection(
            SemanticRuntimeValidationError::ApprovalActorInvalid,
            QStringLiteral("A verified human user must approve the semantic operation."));
    }
    const QByteArray canonicalRequest = canonicalSemanticOperationRequest(operation.request);
    const QByteArray currentRequestDigest
        = canonicalRequest.isEmpty()
              ? QByteArray()
              : QCryptographicHash::hash(canonicalRequest, QCryptographicHash::Sha256);
    if (!sha256BytesAreValid(operation.approvalChallenge)
        || !sha256BytesAreValid(approval.challenge)
        || operation.approvalChallenge != approval.challenge
        || !sha256BytesAreValid(operation.canonicalRequestDigest)
        || currentRequestDigest != operation.canonicalRequestDigest
        || !sha256BytesAreValid(approval.expectedRequestDigest)
        || approval.expectedRequestDigest != operation.canonicalRequestDigest
        || !sha256BytesAreValid(approval.expectedContextHash)
        || approval.expectedContextHash != operation.request.expectedContextHash
        || approval.expectedContextHash != context.contextHash) {
        return rejection(
            SemanticRuntimeValidationError::ApprovalChallengeMismatch,
            QStringLiteral("Semantic operation approval challenge or context changed."));
    }
    return validateSemanticOperationRequest(operation.request, context);
}

SemanticRuntimeReadValidation validateSemanticRuntimeRead(
    const Data::SemanticRuntimeBinding &binding,
    const Data::RuntimeResourceCatalog &catalog,
    const Data::RuntimeResourceSnapshot &snapshot)
{
    const SemanticRuntimeValidation bindingValidation = validateSemanticRuntimeBinding(binding);
    if (!bindingValidation.accepted())
        return {bindingValidation, std::nullopt};

    if (catalog.scope != binding.target.scope || snapshot.scope != binding.target.scope) {
        return {
            rejection(
                SemanticRuntimeValidationError::ScopeMismatch,
                QStringLiteral("Runtime resource scope changed.")),
            std::nullopt,
        };
    }
    if (catalog.sessionGeneration != binding.sessionGeneration
        || snapshot.sessionGeneration != binding.sessionGeneration) {
        return {
            rejection(
                SemanticRuntimeValidationError::SessionMismatch,
                QStringLiteral("Runtime resource session changed.")),
            std::nullopt,
        };
    }

    const SemanticRuntimeValidation catalogEpoch
        = validateSemanticRuntimeEpoch(binding.epoch, catalog.epoch);
    if (!catalogEpoch.accepted())
        return {catalogEpoch, std::nullopt};
    const SemanticRuntimeValidation snapshotEpoch
        = validateSemanticRuntimeEpoch(binding.epoch, snapshot.epoch);
    if (!snapshotEpoch.accepted())
        return {snapshotEpoch, std::nullopt};

    QList<Data::RuntimeResourceDescriptor> descriptors;
    std::copy_if(
        catalog.resources.cbegin(),
        catalog.resources.cend(),
        std::back_inserter(descriptors),
        [&binding](const Data::RuntimeResourceDescriptor &descriptor) {
            return descriptor.id == binding.resourceId;
        });
    if (descriptors.isEmpty()) {
        return {
            rejection(
                SemanticRuntimeValidationError::ResourceNotFound,
                QStringLiteral("Verified runtime resource is absent from the catalog.")),
            std::nullopt,
        };
    }
    if (descriptors.size() != 1 || !descriptorMatchesBinding(descriptors.constFirst(), binding)) {
        return {
            rejection(
                SemanticRuntimeValidationError::DescriptorMismatch,
                QStringLiteral("Runtime resource descriptor differs from the verified binding.")),
            std::nullopt,
        };
    }
    if (!snapshot.complete) {
        return {
            rejection(
                SemanticRuntimeValidationError::SnapshotIncomplete,
                QStringLiteral("Runtime resource snapshot is incomplete.")),
            std::nullopt,
        };
    }
    // Process-image direction describes where the value is exchanged, not
    // whether the runtime catalog may expose its current image. Output
    // resources with READ|WRITE access are readable snapshots too.
    if (binding.access != Data::RuntimeResourceAccess::ReadOnly
        && binding.access != Data::RuntimeResourceAccess::ReadWrite) {
        return {
            rejection(
                SemanticRuntimeValidationError::InvalidBinding,
                QStringLiteral("Semantic runtime binding is not readable.")),
            std::nullopt,
        };
    }

    QList<Data::RuntimeResourceSample> samples;
    std::copy_if(
        snapshot.samples.cbegin(),
        snapshot.samples.cend(),
        std::back_inserter(samples),
        [&binding](const Data::RuntimeResourceSample &sample) {
            return sample.resourceId == binding.resourceId;
        });
    if (samples.isEmpty()) {
        return {
            rejection(
                SemanticRuntimeValidationError::SampleNotFound,
                QStringLiteral("Verified runtime resource has no sample.")),
            std::nullopt,
        };
    }
    if (samples.size() != 1) {
        return {
            rejection(
                SemanticRuntimeValidationError::SampleAmbiguous,
                QStringLiteral("Runtime resource snapshot contains duplicate samples.")),
            std::nullopt,
        };
    }

    const Data::RuntimeResourceSample &sample = samples.constFirst();
    if (!sampleMatchesBinding(sample, binding)) {
        return {
            rejection(
                SemanticRuntimeValidationError::SampleValueMismatch,
                QStringLiteral("Runtime resource sample differs from the verified binding.")),
            std::nullopt,
        };
    }
    if (sample.quality.state != Data::RuntimeResourceQualityState::Good) {
        return {
            rejection(
                SemanticRuntimeValidationError::SampleQualityNotGood,
                QStringLiteral("Runtime resource sample quality is not good.")),
            std::nullopt,
        };
    }
    return {{}, sample};
}

std::optional<Data::SemanticRuntimeContext> SemanticRuntimeService::context(
    const QString &controllerId) const
{
    const QList<Data::SemanticRuntimeContext> current = contexts();
    const auto found = std::find_if(
        current.cbegin(),
        current.cend(),
        [&controllerId](const Data::SemanticRuntimeContext &candidate) {
            return candidate.controllerId == controllerId;
        });
    if (found == current.cend())
        return std::nullopt;
    return *found;
}

Data::SemanticOperationRecord SemanticRuntimeService::submit(
    const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeActor &actor)
{
    return rejectedRecord(
        request, actor, QStringLiteral("No semantic runtime executor is registered."));
}

Data::SemanticOperationRecord SemanticRuntimeService::approve(
    const Data::SemanticOperationApprovalRequest &approval, const Data::SemanticRuntimeActor &actor)
{
    Data::SemanticOperationRequest request;
    request.operationId = approval.operationId;
    Data::SemanticOperationRecord record = rejectedRecord(
        request, actor, QStringLiteral("No semantic runtime approval executor is registered."));
    record.approvals = {{approval, actor, QDateTime::currentDateTimeUtc()}};
    return record;
}

std::optional<Data::SemanticOperationRecord> SemanticRuntimeService::operation(
    const Data::SemanticOperationId &) const
{
    return std::nullopt;
}

QList<Data::SemanticRuntimeAuditEvent> SemanticRuntimeService::audit(const QString &, quint64) const
{
    return {};
}

} // namespace EtherCAT::Core
