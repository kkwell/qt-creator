// Copyright (C) 2026 Embed Labs

#include "semanticactionplan_p.h"

#include "semanticruntimeexecutor.h"

#include <ethercatcore/semanticruntimeservice.h>

#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <variant>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

Utils::ResultError planError(const QString &detail)
{
    return Utils::ResultError(QString::fromLatin1("Semantic action plan error: %1").arg(detail));
}

bool validSha256(QByteArrayView digest)
{
    return digest.size() == QCryptographicHash::hashLength(QCryptographicHash::Sha256);
}

QByteArray opaqueBigEndian(quint64 value)
{
    QByteArray result(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(result.data()));
    return result;
}

QByteArray opaqueBigEndian(quint32 value)
{
    QByteArray result(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(result.data()));
    return result;
}

std::optional<Data::RuntimeResourcePrimitiveType> runtimePrimitive(EcfgResourcePrimitive primitive)
{
    using RuntimePrimitive = Data::RuntimeResourcePrimitiveType;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return RuntimePrimitive::Boolean;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        return RuntimePrimitive::UnsignedInteger;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        return RuntimePrimitive::SignedInteger;
    case EcfgResourcePrimitive::Q32_32:
        return RuntimePrimitive::FloatingPoint;
    case EcfgResourcePrimitive::RawBits:
        return RuntimePrimitive::ByteArray;
    }
    return {};
}

std::optional<Data::RuntimeResourceDirection> runtimeDirection(EcfgResourceDirection direction)
{
    if (direction == EcfgResourceDirection::Input)
        return Data::RuntimeResourceDirection::Input;
    if (direction == EcfgResourceDirection::Output)
        return Data::RuntimeResourceDirection::Output;
    return {};
}

std::optional<Data::RuntimeResourceAccess> runtimeAccess(EcfgResourceAccess access)
{
    if (access == EcfgResourceAccess::Read)
        return Data::RuntimeResourceAccess::ReadOnly;
    if (access == EcfgResourceAccess::ReadWrite)
        return Data::RuntimeResourceAccess::ReadWrite;
    return {};
}

std::optional<Data::RuntimeOutputRecoveryPolicy> runtimeRecoveryPolicy(
    EcfgOutputRecoveryPolicy policy)
{
    if (policy == EcfgOutputRecoveryPolicy::ReturnTask)
        return Data::RuntimeOutputRecoveryPolicy::ReturnTask;
    if (policy == EcfgOutputRecoveryPolicy::HoldSafe)
        return Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    return {};
}

bool primitiveIsSigned(EcfgResourcePrimitive primitive)
{
    return primitive == EcfgResourcePrimitive::S8 || primitive == EcfgResourcePrimitive::S16
           || primitive == EcfgResourcePrimitive::S32 || primitive == EcfgResourcePrimitive::S64;
}

bool unsignedFits(quint64 value, quint16 bitWidth)
{
    return bitWidth && bitWidth <= 64 && (bitWidth == 64 || value < (quint64(1) << bitWidth));
}

bool signedFits(qint64 value, quint16 bitWidth)
{
    if (!bitWidth || bitWidth > 64)
        return false;
    if (bitWidth == 64)
        return true;
    const qint64 limit = qint64(1) << (bitWidth - 1);
    return value >= -limit && value < limit;
}

bool parameterValueIsValid(const VerifiedSemanticActionParameter &parameter, const QVariant &value)
{
    if (parameter.parameterId.isEmpty() || parameter.minimum > parameter.maximum)
        return false;

    switch (parameter.primitive) {
    case EcfgResourcePrimitive::Bool:
        return value.metaType().id() == QMetaType::Bool && parameter.minimum >= 0
               && parameter.maximum <= 1 && int(value.toBool()) >= parameter.minimum
               && int(value.toBool()) <= parameter.maximum;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        return value.metaType().id() == QMetaType::ULongLong && parameter.minimum >= 0
               && value.toULongLong() >= quint64(parameter.minimum)
               && value.toULongLong() <= quint64(parameter.maximum);
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        return value.metaType().id() == QMetaType::LongLong
               && value.toLongLong() >= parameter.minimum
               && value.toLongLong() <= parameter.maximum;
    case EcfgResourcePrimitive::Q32_32:
    case EcfgResourcePrimitive::RawBits:
        // The public operation contract has no exact parameter representation
        // for fixed-point or width-qualified raw-bit action parameters.
        return false;
    }
    return false;
}

std::optional<QVariant> constantAsVariant(
    const std::variant<qint64, quint64> &constant, EcfgResourcePrimitive primitive, quint16 bitWidth)
{
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        if (!std::holds_alternative<quint64>(constant) || std::get<quint64>(constant) > 1
            || bitWidth != 1) {
            return {};
        }
        return QVariant(bool(std::get<quint64>(constant)));
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64: {
        if (!std::holds_alternative<quint64>(constant)
            || !unsignedFits(std::get<quint64>(constant), bitWidth)) {
            return {};
        }
        return QVariant::fromValue<qulonglong>(std::get<quint64>(constant));
    }
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64: {
        qint64 value = 0;
        if (std::holds_alternative<qint64>(constant)) {
            value = std::get<qint64>(constant);
        } else if (std::get<quint64>(constant) <= quint64(std::numeric_limits<qint64>::max())) {
            value = qint64(std::get<quint64>(constant));
        } else {
            return {};
        }
        if (!signedFits(value, bitWidth))
            return {};
        return QVariant::fromValue<qlonglong>(value);
    }
    case EcfgResourcePrimitive::RawBits: {
        if (!std::holds_alternative<quint64>(constant) || !bitWidth || bitWidth > 128)
            return {};
        const quint64 value = std::get<quint64>(constant);
        if (bitWidth < 64 && !unsignedFits(value, bitWidth))
            return {};
        QByteArray bytes(qsizetype((bitWidth + 7) / 8), '\0');
        const qsizetype encodedBytes = qMin(bytes.size(), qsizetype(sizeof(value)));
        for (qsizetype index = 0; index < encodedBytes; ++index)
            bytes[bytes.size() - index - 1] = char(value >> (index * 8));
        return bytes;
    }
    case EcfgResourcePrimitive::Q32_32:
        // RuntimeOutputValueWrite intentionally rejects floating-point writes
        // until an exact engineering-rational to Q32.32 contract exists.
        return {};
    }
    return {};
}

bool parameterMatchesRuntimeDefinition(
    const VerifiedSemanticActionParameter &signedParameter,
    const Data::SemanticActionParameterRuntimeDefinition &runtimeParameter)
{
    const auto primitive = runtimePrimitive(signedParameter.primitive);
    if (!primitive || runtimeParameter.id != signedParameter.parameterId
        || runtimeParameter.primitiveType != *primitive
        || runtimeParameter.unit != signedParameter.unit.value_or(QString())) {
        return false;
    }

    switch (signedParameter.primitive) {
    case EcfgResourcePrimitive::Bool:
        return runtimeParameter.minimum.metaType().id() == QMetaType::Bool
               && runtimeParameter.maximum.metaType().id() == QMetaType::Bool
               && runtimeParameter.minimum.toBool() == bool(signedParameter.minimum)
               && runtimeParameter.maximum.toBool() == bool(signedParameter.maximum);
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        return signedParameter.minimum >= 0
               && runtimeParameter.minimum.metaType().id() == QMetaType::ULongLong
               && runtimeParameter.maximum.metaType().id() == QMetaType::ULongLong
               && runtimeParameter.minimum.toULongLong() == quint64(signedParameter.minimum)
               && runtimeParameter.maximum.toULongLong() == quint64(signedParameter.maximum);
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        return runtimeParameter.minimum.metaType().id() == QMetaType::LongLong
               && runtimeParameter.maximum.metaType().id() == QMetaType::LongLong
               && runtimeParameter.minimum.toLongLong() == signedParameter.minimum
               && runtimeParameter.maximum.toLongLong() == signedParameter.maximum;
    case EcfgResourcePrimitive::Q32_32:
    case EcfgResourcePrimitive::RawBits:
        return false;
    }
    return false;
}

bool bindingReferenceMatchesArtifact(
    const VerifiedSemanticActionBindingReference &reference, const VerifiedSemanticBinding &binding)
{
    return reference.semanticSignalDefinitionId == binding.semanticSignalDefinitionId
           && reference.semanticBindingId == binding.semanticBindingId
           && reference.resourceId == binding.resourceId
           && reference.componentBindingId == binding.componentBindingId
           && reference.consistencyGroupId == binding.consistencyGroupId
           && reference.primitive == binding.primitive && reference.bitWidth == binding.bitWidth
           && reference.direction == binding.direction && reference.access == binding.access
           && reference.unit == binding.unit && reference.scaleNumerator == binding.scaleNumerator
           && reference.scaleDenominator == binding.scaleDenominator
           && reference.scaleOffset == binding.scaleOffset;
}

bool runtimeBindingMatchesSignedReference(
    const Data::SemanticRuntimeBinding &runtime,
    const VerifiedSemanticActionBindingReference &reference,
    const VerifiedSemanticBinding &binding,
    const Data::SemanticActionRuntimeState &runtimeAction,
    const Data::SemanticRuntimeContext &context)
{
    const auto primitive = runtimePrimitive(binding.primitive);
    const auto direction = runtimeDirection(binding.direction);
    const auto access = runtimeAccess(binding.access);
    const Core::SemanticRuntimeValidation validation = Core::validateSemanticRuntimeBinding(runtime);
    return primitive && direction && access && validation.accepted()
           && bindingReferenceMatchesArtifact(reference, binding)
           && runtime.target.controllerId == runtimeAction.target.controllerId
           && runtime.target.scope == runtimeAction.target.scope
           && runtime.target.deviceId == runtimeAction.target.deviceId
           && runtime.target.kind == Data::SemanticRuntimeTargetKind::Signal
           && runtime.target.signalId.value == reference.semanticSignalDefinitionId
           && runtime.target.actionId.value.isEmpty()
           && runtime.semanticBindingId == reference.semanticBindingId
           && runtime.componentBindingId == reference.componentBindingId
           && runtime.adapterId.value == binding.adapterId
           && runtime.adapterVersion == binding.adapterVersion
           && runtime.adapterContentSha256 == binding.adapterSha256
           && runtime.esiSha256 == binding.esiSha256
           && runtime.bindingArtifactSha256 == context.mappingDigest.value
           && runtime.sessionGeneration == context.sessionGeneration
           && runtime.epoch == context.epoch && runtime.mappingDigest == context.mappingDigest
           && runtime.controllerMappingDigest == context.controllerMappingDigest
           && runtime.verification == context.bindingVerification
           && runtime.resourceId.value == opaqueBigEndian(reference.resourceId)
           && runtime.componentInstanceId.value == opaqueBigEndian(binding.componentInstanceId)
           && runtime.consistencyGroupId.value == opaqueBigEndian(reference.consistencyGroupId)
           && runtime.primitiveType == *primitive
           && runtime.valueTypeIdentity
                  == QByteArray("ethercat.runtime.value/primitive-")
                         + QByteArray::number(quint8(reference.primitive)) + "/bits-"
                         + QByteArray::number(reference.bitWidth)
           && runtime.bitWidth == reference.bitWidth && runtime.direction == *direction
           && runtime.access == *access;
}

Data::RuntimeOutputOperationId outputOperationId(
    const Data::SemanticOperationId &semanticOperationId,
    QByteArrayView canonicalRequestDigest,
    QStringView actionBindingId,
    quint32 stepIndex)
{
    QByteArray material = QByteArrayLiteral("embed-labs.runtime-output-operation-id.v1");
    material.append('\0');
    const auto appendBytes = [&material](QByteArrayView value) {
        const quint32 size = qToBigEndian(quint32(value.size()));
        material.append(reinterpret_cast<const char *>(&size), qsizetype(sizeof(size)));
        material.append(value.data(), value.size());
    };
    appendBytes(semanticOperationId.value.toUtf8());
    appendBytes(canonicalRequestDigest);
    appendBytes(actionBindingId.toString().toUtf8());
    const quint32 bigEndianIndex = qToBigEndian(stepIndex);
    material
        .append(reinterpret_cast<const char *>(&bigEndianIndex), qsizetype(sizeof(bigEndianIndex)));

    Data::RuntimeOutputOperationId result;
    result.value = QCryptographicHash::hash(material, QCryptographicHash::Sha256).first(16);
    return result;
}

} // namespace

class SemanticActionPlanBuilder
{
public:
    SemanticActionPlanBuilder(
        const VerifiedRuntimePackageEvidence &evidence,
        const Data::SemanticOperationRequest &request,
        const Data::SemanticRuntimeContext &context)
        : m_evidence(evidence)
        , m_request(request)
        , m_context(context)
    {}

    Utils::Result<SemanticActionPlan> build();

private:
    Utils::Result<> validateEvidenceAndContext();
    Utils::Result<> selectAction();
    Utils::Result<> validateParameters();
    Utils::Result<> resolveBindings();
    Utils::Result<> appendGroups(SemanticActionPlan &plan);
    Utils::Result<> appendSteps(SemanticActionPlan &plan);
    Utils::Result<Data::RuntimeResourceTypedValue> resolvedValue(
        const VerifiedSemanticActionAssignment &assignment,
        const VerifiedSemanticBinding &binding,
        const Data::SemanticRuntimeBinding &runtimeBinding) const;

    const VerifiedRuntimePackageEvidence &m_evidence;
    const Data::SemanticOperationRequest &m_request;
    const Data::SemanticRuntimeContext &m_context;
    const VerifiedSemanticAction *m_action = nullptr;
    const Data::SemanticActionRuntimeState *m_runtimeAction = nullptr;
    QByteArray m_canonicalRequestDigest;
    QHash<QString, const Data::SemanticRuntimeBinding *> m_bindings;
    QHash<quint32, const VerifiedSemanticActionGroup *> m_groups;
};

Utils::Result<> SemanticActionPlanBuilder::validateEvidenceAndContext()
{
    if (!m_evidence.isValid() || !m_evidence.permitsWritableActions()
        || !m_evidence.actionDefinitions() || !m_evidence.actionDefinitions()->isValid()) {
        return planError(QString::fromLatin1("signed writable package evidence is unavailable"));
    }

    const Core::SemanticRuntimeValidation validation
        = Core::validateSemanticOperationRequest(m_request, m_context);
    if (!validation.accepted())
        return planError(validation.detail);
    if (m_request.kind != Data::SemanticOperationKind::InvokeAction
        || m_request.target.kind != Data::SemanticRuntimeTargetKind::Action) {
        return planError(QString::fromLatin1("only signed action invocation can produce a plan"));
    }

    const QByteArray canonical = Core::canonicalSemanticOperationRequest(m_request);
    if (canonical.isEmpty()) {
        return planError(QString::fromLatin1("the semantic request has no canonical encoding"));
    }
    m_canonicalRequestDigest = QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);

    const VerifiedSemanticBindingArtifact &artifact = m_evidence.semanticBindingArtifact();
    const Data::RuntimeSemanticMappingProof &proof = m_evidence.semanticMappingProof();
    const VerifiedSemanticActionDefinitions &definitions = *m_evidence.actionDefinitions();
    const QByteArray recalculatedContextHash = semanticRuntimeContextHash(m_context);
    if (!validSha256(m_canonicalRequestDigest) || !validSha256(recalculatedContextHash)
        || recalculatedContextHash != m_context.contextHash
        || m_context.mappingDigest.algorithm != QStringLiteral("sha256")
        || m_context.controllerMappingDigest.algorithm != QStringLiteral("sha256")
        || m_context.actionDefinitionsDigest.algorithm != QStringLiteral("sha256")
        || m_context.bindingVerification.signedManifestDigest.algorithm != QStringLiteral("sha256")
        || m_context.mappingDigest.value != proof.mappingSha256
        || m_context.controllerMappingDigest.value != proof.mappingSha256
        || m_context.actionDefinitionsDigest.value != definitions.definitionsSha256
        || m_context.bindingVerification.signedManifestDigest.value != proof.manifestSha256
        || m_context.cyclePeriodNs != m_evidence.cyclePeriodNs()
        || m_context.epoch.configurationId != artifact.configurationId
        || m_context.epoch.catalogRevision != artifact.catalogRevision
        || m_context.epoch.topologyIdentity != opaqueBigEndian(artifact.topologyIdentity)) {
        return planError(
            QString::fromLatin1("the public context differs from the verified package evidence"));
    }
    return Utils::ResultOk;
}

Utils::Result<> SemanticActionPlanBuilder::selectAction()
{
    QList<const Data::SemanticActionRuntimeState *> matches;
    for (const Data::SemanticActionRuntimeState &candidate : m_context.actionStates) {
        if (candidate.target == m_request.target)
            matches.append(&candidate);
    }
    if (matches.size() != 1) {
        return planError(
            QString::fromLatin1("the requested action is not unique in the public context"));
    }
    m_runtimeAction = matches.constFirst();

    const VerifiedSemanticBindingArtifact &artifact = m_evidence.semanticBindingArtifact();
    const VerifiedSemanticAction *signedAction = artifact.findAction(
        m_request.target.actionId.value);
    if (!signedAction || signedAction->actionBindingId != m_request.target.actionId.value
        || !signedAction->enabled
        || signedAction->qualification != VerifiedSemanticActionQualification::Qualified
        || signedAction->disabledReason) {
        return planError(QString::fromLatin1("the signed action is disabled or unqualified"));
    }
    const bool dcRuntimeActive = !signedAction->dcRequired
                                 || m_runtimeAction->availability
                                        == Data::SemanticActionAvailability::Ready;
    if (m_evidence.invocableAction(signedAction->actionBindingId, dcRuntimeActive) != signedAction) {
        return planError(QString::fromLatin1("the signed action is not invocable in this runtime"));
    }

    if (m_runtimeAction->availability != Data::SemanticActionAvailability::Ready
        || !m_runtimeAction->definition.enabled
        || m_runtimeAction->definition.id != m_runtimeAction->target.actionId
        || m_runtimeAction->actionBindingId != signedAction->actionBindingId
        || m_runtimeAction->actionDefinitionId != signedAction->actionDefinitionId
        || m_runtimeAction->actionDefinitionDigest.algorithm != QStringLiteral("sha256")
        || m_runtimeAction->actionDefinitionDigest.value != signedAction->actionDefinitionSha256
        || m_runtimeAction->qualification != Data::SemanticActionQualification::Qualified
        || !m_runtimeAction->disabledReason.isEmpty()
        || m_runtimeAction->requiresDc != signedAction->dcRequired
        || m_runtimeAction->requiresExclusiveControl != true
        || m_runtimeAction->requiresApproval != true || m_runtimeAction->holdToRun
        || signedAction->consistencyGroups.isEmpty() || signedAction->steps.isEmpty()) {
        return planError(
            QString::fromLatin1("the public action projection differs from signed evidence"));
    }

    quint32 strictMaximumTtl = std::numeric_limits<quint32>::max();
    QSet<quint32> groupIds;
    for (const VerifiedSemanticActionGroup &group : signedAction->consistencyGroups) {
        if (!group.consistencyGroupId || !group.maximumTtlCycles || group.maximumTtlCycles > 65535
            || groupIds.contains(group.consistencyGroupId)
            || !runtimeRecoveryPolicy(group.recoveryPolicy)) {
            return planError(
                QString::fromLatin1("the signed action contains an invalid output group"));
        }
        groupIds.insert(group.consistencyGroupId);
        strictMaximumTtl = qMin(strictMaximumTtl, group.maximumTtlCycles);
        m_groups.insert(group.consistencyGroupId, &group);
    }
    if (!m_request.ttlCycles || m_request.ttlCycles > strictMaximumTtl
        || m_runtimeAction->maximumTtlCycles != strictMaximumTtl) {
        return planError(
            QString::fromLatin1("the requested TTL differs from signed output-group limits"));
    }

    m_action = signedAction;
    return Utils::ResultOk;
}

Utils::Result<> SemanticActionPlanBuilder::validateParameters()
{
    if (!m_action || !m_runtimeAction || m_request.parameters.size() != m_action->parameters.size()
        || m_runtimeAction->parameters.size() != m_action->parameters.size()) {
        return planError(QString::fromLatin1("the action parameter set is incomplete"));
    }

    QHash<QString, const Data::SemanticActionParameterRuntimeDefinition *> runtimeParameters;
    for (const Data::SemanticActionParameterRuntimeDefinition &parameter :
         m_runtimeAction->parameters) {
        if (parameter.id.isEmpty() || runtimeParameters.contains(parameter.id)) {
            return planError(QString::fromLatin1("the public action parameter set is ambiguous"));
        }
        runtimeParameters.insert(parameter.id, &parameter);
    }

    QSet<QString> signedParameterIds;
    for (const VerifiedSemanticActionParameter &parameter : m_action->parameters) {
        const auto value = m_request.parameters.constFind(parameter.parameterId);
        const auto runtimeParameter = runtimeParameters.constFind(parameter.parameterId);
        if (parameter.parameterId.isEmpty() || signedParameterIds.contains(parameter.parameterId)
            || value == m_request.parameters.cend() || runtimeParameter == runtimeParameters.cend()
            || !parameterValueIsValid(parameter, *value)
            || !parameterMatchesRuntimeDefinition(parameter, **runtimeParameter)) {
            return planError(
                QString::fromLatin1("an action parameter differs from its signed definition"));
        }
        signedParameterIds.insert(parameter.parameterId);
    }
    return Utils::ResultOk;
}

Utils::Result<> SemanticActionPlanBuilder::resolveBindings()
{
    if (!m_action || !m_runtimeAction)
        return planError(QString::fromLatin1("the signed action is unavailable"));

    for (const Data::SemanticRuntimeBinding &binding : m_runtimeAction->bindings) {
        if (binding.semanticBindingId.isEmpty() || m_bindings.contains(binding.semanticBindingId)) {
            return planError(QString::fromLatin1("the public action bindings are ambiguous"));
        }
        m_bindings.insert(binding.semanticBindingId, &binding);
    }

    const qsizetype expectedCount = m_action->requiredBindings.size()
                                    + m_action->optionalBindings.size();
    if (m_bindings.size() != expectedCount) {
        return planError(QString::fromLatin1("the public action binding set is incomplete"));
    }

    QSet<QString> signedBindingIds;
    const auto validateReference =
        [this, &signedBindingIds](
            const VerifiedSemanticActionBindingReference &reference) -> Utils::Result<> {
        const auto runtime = m_bindings.constFind(reference.semanticBindingId);
        const VerifiedSemanticBinding *binding
            = m_evidence.semanticBindingArtifact().findBySemanticSignalId(
                reference.semanticBindingId);
        if (reference.semanticBindingId.isEmpty()
            || signedBindingIds.contains(reference.semanticBindingId)
            || runtime == m_bindings.cend() || !binding
            || !runtimeBindingMatchesSignedReference(
                **runtime, reference, *binding, *m_runtimeAction, m_context)) {
            return planError(
                QString::fromLatin1("an action binding differs from the signed runtime mapping"));
        }
        signedBindingIds.insert(reference.semanticBindingId);
        return Utils::ResultOk;
    };

    for (const VerifiedSemanticActionBindingReference &reference : m_action->requiredBindings) {
        const Utils::Result<> result = validateReference(reference);
        if (!result)
            return result;
    }
    for (const VerifiedSemanticActionBindingReference &reference : m_action->optionalBindings) {
        const Utils::Result<> result = validateReference(reference);
        if (!result)
            return result;
    }
    return Utils::ResultOk;
}

Utils::Result<> SemanticActionPlanBuilder::appendGroups(SemanticActionPlan &plan)
{
    QSet<QByteArray> opaqueGroupIds;
    const QList<EcfgOutputGroupPolicy> &policies = m_evidence.outputPolicies();
    for (const VerifiedSemanticActionGroup &signedGroup : m_action->consistencyGroups) {
        const auto recovery = runtimeRecoveryPolicy(signedGroup.recoveryPolicy);
        const auto policy = std::find_if(
            policies.cbegin(),
            policies.cend(),
            [&signedGroup](const EcfgOutputGroupPolicy &candidate) {
                return candidate.consistencyGroupId == signedGroup.consistencyGroupId;
            });
        SemanticActionPlanGroup group;
        group.m_consistencyGroupId = {
            opaqueBigEndian(signedGroup.consistencyGroupId),
        };
        group.m_recoveryPolicy = recovery.value_or(Data::RuntimeOutputRecoveryPolicy::Unknown);
        group.m_maximumTtlCycles = signedGroup.maximumTtlCycles;
        if (!recovery || policy == policies.cend()
            || policy->recoveryPolicy != signedGroup.recoveryPolicy
            || policy->maximumTtlCycles != signedGroup.maximumTtlCycles
            || policy->resourceCount == 0 || policy->resourceCount > 64
            || policy->resourceIds.size() != qsizetype(policy->resourceCount)
            || !validSha256(policy->groupResourceRecordsSha256)
            || !group.m_consistencyGroupId.isValid()
            || opaqueGroupIds.contains(group.m_consistencyGroupId.value)
            || !group.m_maximumTtlCycles || plan.m_ttlCycles > group.m_maximumTtlCycles) {
            return planError(QString::fromLatin1("a signed consistency group cannot be planned"));
        }

        QByteArray previousResourceId;
        QSet<QByteArray> resourceIds;
        for (quint64 signedResourceId : policy->resourceIds) {
            Data::RuntimeResourceId resourceId{opaqueBigEndian(signedResourceId)};
            if (!signedResourceId || !resourceId.isValid()
                || (!previousResourceId.isEmpty() && resourceId.value <= previousResourceId)
                || resourceIds.contains(resourceId.value)) {
                return planError(QString::fromLatin1(
                    "a signed consistency group resource set is not canonical"));
            }
            previousResourceId = resourceId.value;
            resourceIds.insert(resourceId.value);
            group.m_completeResourceIds.append(std::move(resourceId));
        }
        group.m_completeGroupRecordDigest = policy->groupResourceRecordsSha256;
        group.m_completeResourceCount = policy->resourceCount;
        opaqueGroupIds.insert(group.m_consistencyGroupId.value);
        plan.m_groups.append(std::move(group));
    }
    return Utils::ResultOk;
}

Utils::Result<Data::RuntimeResourceTypedValue> SemanticActionPlanBuilder::resolvedValue(
    const VerifiedSemanticActionAssignment &assignment,
    const VerifiedSemanticBinding &binding,
    const Data::SemanticRuntimeBinding &runtimeBinding) const
{
    if (assignment.constantValue.has_value() == assignment.parameterId.has_value())
        return planError(QString::fromLatin1("an assignment has an invalid value source"));

    QVariant resolved;
    if (assignment.constantValue) {
        const std::optional<QVariant> value
            = constantAsVariant(*assignment.constantValue, binding.primitive, binding.bitWidth);
        if (!value)
            return planError(QString::fromLatin1("an assignment constant is not representable"));
        resolved = *value;
    } else {
        const auto parameter = std::find_if(
            m_action->parameters.cbegin(),
            m_action->parameters.cend(),
            [&assignment](const VerifiedSemanticActionParameter &candidate) {
                return candidate.parameterId == *assignment.parameterId;
            });
        const auto value = m_request.parameters.constFind(*assignment.parameterId);
        if (parameter == m_action->parameters.cend() || value == m_request.parameters.cend()
            || parameter->primitive != binding.primitive || parameter->unit != binding.unit
            || !parameterValueIsValid(*parameter, *value)) {
            return planError(QString::fromLatin1("an assignment parameter is not type compatible"));
        }
        resolved = *value;
    }

    Data::RuntimeResourceTypedValue result;
    result.primitiveType = runtimeBinding.primitiveType;
    result.value = resolved;
    result.typeIdentity = runtimeBinding.valueTypeIdentity;
    return result;
}

Utils::Result<> SemanticActionPlanBuilder::appendSteps(SemanticActionPlan &plan)
{
    QSet<quint32> usedGroupIds;
    for (qsizetype index = 0; index < m_action->steps.size(); ++index) {
        const VerifiedSemanticActionStep &signedStep = m_action->steps.at(index);
        SemanticActionPlanStep step;
        step.m_index = quint32(index);

        if (signedStep.kind == VerifiedSemanticActionStepKind::WriteGroup) {
            step.m_kind = SemanticActionPlanStepKind::WriteGroup;
            const auto group = m_groups.constFind(signedStep.consistencyGroupId);
            if (group == m_groups.cend() || signedStep.assignments.isEmpty()
                || signedStep.assignments.size() > 64) {
                return planError(QString::fromLatin1("a write step references an invalid group"));
            }
            step.m_consistencyGroupId = {
                opaqueBigEndian(signedStep.consistencyGroupId),
            };
            step.m_outputOperationId = outputOperationId(
                m_request.operationId,
                m_canonicalRequestDigest,
                m_action->actionBindingId,
                step.m_index);
            if (!step.m_outputOperationId.isValid())
                return planError(QString::fromLatin1("an output OperationId is invalid"));

            QSet<QString> assignmentIds;
            QSet<QByteArray> resourceIds;
            for (const VerifiedSemanticActionAssignment &assignment : signedStep.assignments) {
                const auto runtime = m_bindings.constFind(assignment.semanticBindingId);
                const VerifiedSemanticBinding *binding
                    = m_evidence.semanticBindingArtifact().findBySemanticSignalId(
                        assignment.semanticBindingId);
                if (assignment.semanticBindingId.isEmpty()
                    || assignmentIds.contains(assignment.semanticBindingId)
                    || runtime == m_bindings.cend() || !binding
                    || binding->consistencyGroupId != signedStep.consistencyGroupId
                    || binding->direction != EcfgResourceDirection::Output
                    || binding->access != EcfgResourceAccess::ReadWrite
                    || !binding->safeValueDeclared) {
                    return planError(QString::fromLatin1(
                        "a write assignment is outside its complete signed group"));
                }

                const Utils::Result<Data::RuntimeResourceTypedValue> value
                    = resolvedValue(assignment, *binding, **runtime);
                if (!value)
                    return planError(value.error());
                Data::RuntimeOutputValueWrite write;
                write.resourceId = (*runtime)->resourceId;
                write.bitWidth = (*runtime)->bitWidth;
                write.value = *value;
                if (!write.isValid() || resourceIds.contains(write.resourceId.value)) {
                    return planError(QString::fromLatin1(
                        "a write assignment cannot be represented generically"));
                }
                assignmentIds.insert(assignment.semanticBindingId);
                resourceIds.insert(write.resourceId.value);
                step.m_completeGroupWrites.append(std::move(write));
            }
            std::sort(
                step.m_completeGroupWrites.begin(),
                step.m_completeGroupWrites.end(),
                [](const Data::RuntimeOutputValueWrite &left,
                   const Data::RuntimeOutputValueWrite &right) {
                    return left.resourceId.value < right.resourceId.value;
                });
            const auto planGroup = std::find_if(
                plan.m_groups.cbegin(),
                plan.m_groups.cend(),
                [&step](const SemanticActionPlanGroup &candidate) {
                    return candidate.consistencyGroupId() == step.m_consistencyGroupId;
                });
            QList<Data::RuntimeResourceId> writtenResourceIds;
            writtenResourceIds.reserve(step.m_completeGroupWrites.size());
            for (const Data::RuntimeOutputValueWrite &write :
                 std::as_const(step.m_completeGroupWrites)) {
                writtenResourceIds.append(write.resourceId);
            }
            if (step.m_completeGroupWrites.size() != signedStep.assignments.size()
                || planGroup == plan.m_groups.cend()
                || writtenResourceIds != planGroup->completeResourceIds()
                || std::adjacent_find(
                       step.m_completeGroupWrites.cbegin(),
                       step.m_completeGroupWrites.cend(),
                       [](const Data::RuntimeOutputValueWrite &left,
                          const Data::RuntimeOutputValueWrite &right) {
                           return left.resourceId == right.resourceId;
                       })
                       != step.m_completeGroupWrites.cend()) {
                return planError(
                    QString::fromLatin1("a write step is not a canonical complete group"));
            }
            usedGroupIds.insert(signedStep.consistencyGroupId);
        } else {
            const auto runtime = m_bindings.constFind(signedStep.semanticBindingId);
            const VerifiedSemanticBinding *binding
                = m_evidence.semanticBindingArtifact().findBySemanticSignalId(
                    signedStep.semanticBindingId);
            if (runtime == m_bindings.cend() || !binding
                || binding->direction != EcfgResourceDirection::Input
                || binding->access != EcfgResourceAccess::Read || binding->bitWidth > 64
                || !signedStep.timeoutCycles) {
                return planError(
                    QString::fromLatin1("a wait step has no valid signed input binding"));
            }
            step.m_waitBinding = **runtime;
            step.m_timeoutCycles = signedStep.timeoutCycles;

            if (signedStep.kind == VerifiedSemanticActionStepKind::WaitMasked) {
                if (!isValidSemanticMaskedWaitCondition(
                        signedStep.mask, signedStep.value, binding->bitWidth)) {
                    return planError(QString::fromLatin1("a masked wait condition is invalid"));
                }
                step.m_kind = SemanticActionPlanStepKind::WaitMasked;
                step.m_mask = signedStep.mask;
                step.m_expectedValue = signedStep.value;
            } else if (signedStep.kind == VerifiedSemanticActionStepKind::WaitAbsoluteLimit) {
                if (!primitiveIsSigned(binding->primitive) || !binding->bitWidth
                    || binding->bitWidth > 64) {
                    return planError(QString::fromLatin1(
                        "an absolute-limit wait is outside its signed integer range"));
                }
                const quint64 maximumAbsoluteLimit
                    = binding->bitWidth == 64
                          ? quint64(std::numeric_limits<qint64>::max())
                          : (quint64(1) << (binding->bitWidth - 1)) - 1;
                if (signedStep.absoluteLimit > maximumAbsoluteLimit) {
                    return planError(QString::fromLatin1(
                        "an absolute-limit wait is outside its signed integer range"));
                }
                step.m_kind = SemanticActionPlanStepKind::WaitAbsoluteLimit;
                step.m_absoluteLimit = signedStep.absoluteLimit;
            } else {
                return planError(QString::fromLatin1("an action step kind is unsupported"));
            }
        }
        plan.m_steps.append(std::move(step));
    }

    if (usedGroupIds.size() != m_groups.size()) {
        return planError(
            QString::fromLatin1("not every signed consistency group is used by the plan"));
    }
    return Utils::ResultOk;
}

Utils::Result<SemanticActionPlan> SemanticActionPlanBuilder::build()
{
    const Utils::Result<> evidenceResult = validateEvidenceAndContext();
    if (!evidenceResult)
        return planError(evidenceResult.error());
    const Utils::Result<> actionResult = selectAction();
    if (!actionResult)
        return planError(actionResult.error());
    const Utils::Result<> parameterResult = validateParameters();
    if (!parameterResult)
        return planError(parameterResult.error());
    const Utils::Result<> bindingResult = resolveBindings();
    if (!bindingResult)
        return planError(bindingResult.error());

    SemanticActionPlan plan;
    plan.m_request = m_request;
    plan.m_canonicalRequestDigest = m_canonicalRequestDigest;
    plan.m_actionBindingId = m_action->actionBindingId;
    plan.m_actionDefinitionId = m_action->actionDefinitionId;
    plan.m_actionDefinitionDigest = m_action->actionDefinitionSha256;
    plan.m_actionDefinitionsDigest = m_evidence.actionDefinitions()->definitionsSha256;
    plan.m_scope = m_context.scope;
    plan.m_sessionGeneration = m_context.sessionGeneration;
    plan.m_epoch = m_context.epoch;
    plan.m_mappingDigest = m_context.mappingDigest.value;
    plan.m_contextHash = m_context.contextHash;
    plan.m_cyclePeriodNs = m_context.cyclePeriodNs;
    plan.m_ttlCycles = m_request.ttlCycles;

    const Utils::Result<> groupResult = appendGroups(plan);
    if (!groupResult)
        return planError(groupResult.error());
    const Utils::Result<> stepResult = appendSteps(plan);
    if (!stepResult)
        return planError(stepResult.error());
    return plan;
}

const Data::RuntimeConsistencyGroupId &SemanticActionPlanGroup::consistencyGroupId() const
{
    return m_consistencyGroupId;
}

Data::RuntimeOutputRecoveryPolicy SemanticActionPlanGroup::recoveryPolicy() const
{
    return m_recoveryPolicy;
}

quint32 SemanticActionPlanGroup::maximumTtlCycles() const
{
    return m_maximumTtlCycles;
}

const QByteArray &SemanticActionPlanGroup::completeGroupRecordDigest() const
{
    return m_completeGroupRecordDigest;
}

quint32 SemanticActionPlanGroup::completeResourceCount() const
{
    return m_completeResourceCount;
}

const QList<Data::RuntimeResourceId> &SemanticActionPlanGroup::completeResourceIds() const
{
    return m_completeResourceIds;
}

quint32 SemanticActionPlanStep::index() const
{
    return m_index;
}

SemanticActionPlanStepKind SemanticActionPlanStep::kind() const
{
    return m_kind;
}

const Data::RuntimeConsistencyGroupId &SemanticActionPlanStep::consistencyGroupId() const
{
    return m_consistencyGroupId;
}

const Data::RuntimeOutputOperationId &SemanticActionPlanStep::outputOperationId() const
{
    return m_outputOperationId;
}

const QList<Data::RuntimeOutputValueWrite> &SemanticActionPlanStep::completeGroupWrites() const
{
    return m_completeGroupWrites;
}

const std::optional<Data::SemanticRuntimeBinding> &SemanticActionPlanStep::waitBinding() const
{
    return m_waitBinding;
}

quint64 SemanticActionPlanStep::mask() const
{
    return m_mask;
}

quint64 SemanticActionPlanStep::expectedValue() const
{
    return m_expectedValue;
}

quint64 SemanticActionPlanStep::absoluteLimit() const
{
    return m_absoluteLimit;
}

quint32 SemanticActionPlanStep::timeoutCycles() const
{
    return m_timeoutCycles;
}

const Data::SemanticOperationRequest &SemanticActionPlan::request() const
{
    return m_request;
}

const QByteArray &SemanticActionPlan::canonicalRequestDigest() const
{
    return m_canonicalRequestDigest;
}

const QString &SemanticActionPlan::actionBindingId() const
{
    return m_actionBindingId;
}

const QString &SemanticActionPlan::actionDefinitionId() const
{
    return m_actionDefinitionId;
}

const QByteArray &SemanticActionPlan::actionDefinitionDigest() const
{
    return m_actionDefinitionDigest;
}

const QByteArray &SemanticActionPlan::actionDefinitionsDigest() const
{
    return m_actionDefinitionsDigest;
}

const Data::ControllerConnectionScope &SemanticActionPlan::scope() const
{
    return m_scope;
}

quint64 SemanticActionPlan::sessionGeneration() const
{
    return m_sessionGeneration;
}

const Data::RuntimeResourceCatalogEpoch &SemanticActionPlan::epoch() const
{
    return m_epoch;
}

const QByteArray &SemanticActionPlan::mappingDigest() const
{
    return m_mappingDigest;
}

const QByteArray &SemanticActionPlan::contextHash() const
{
    return m_contextHash;
}

quint32 SemanticActionPlan::cyclePeriodNs() const
{
    return m_cyclePeriodNs;
}

quint32 SemanticActionPlan::ttlCycles() const
{
    return m_ttlCycles;
}

const QList<SemanticActionPlanGroup> &SemanticActionPlan::groups() const
{
    return m_groups;
}

const QList<SemanticActionPlanStep> &SemanticActionPlan::steps() const
{
    return m_steps;
}

Utils::Result<SemanticActionPlan> buildSemanticActionPlan(
    const VerifiedRuntimePackageEvidence &evidence,
    const Data::SemanticOperationRequest &request,
    const Data::SemanticRuntimeContext &context)
{
    return SemanticActionPlanBuilder(evidence, request, context).build();
}

} // namespace EtherCAT::SemanticRuntime::Internal
