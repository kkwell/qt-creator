// Copyright (C) 2026 Embed Labs

#include "semanticactionruntimefactory_p.h"

#include <QtEndian>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

Utils::ResultError projectionError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Semantic action projection error: %1").arg(detail));
}

bool validControllerId(QStringView controllerId)
{
    if (controllerId.isEmpty() || controllerId != controllerId.trimmed())
        return false;
    return std::none_of(controllerId.cbegin(), controllerId.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

bool validSha256Digest(const Data::SemanticRuntimeDigest &digest)
{
    return digest.algorithm == QStringLiteral("sha256") && digest.value.size() == 32;
}

QByteArray opaqueBigEndian(quint64 value)
{
    QByteArray bytes(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QByteArray opaqueBigEndian(quint32 value)
{
    QByteArray bytes(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QByteArray valueTypeIdentity(EcfgResourcePrimitive primitive, quint16 bitWidth)
{
    return QByteArray("ethercat.runtime.value/primitive-") + QByteArray::number(quint8(primitive))
           + "/bits-" + QByteArray::number(bitWidth);
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

std::optional<Data::EtherCATDataType> publicDataType(EcfgResourcePrimitive primitive)
{
    using DataType = Data::EtherCATDataType;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return DataType::Boolean;
    case EcfgResourcePrimitive::U8:
        return DataType::UnsignedInteger8;
    case EcfgResourcePrimitive::S8:
        return DataType::Integer8;
    case EcfgResourcePrimitive::U16:
        return DataType::UnsignedInteger16;
    case EcfgResourcePrimitive::S16:
        return DataType::Integer16;
    case EcfgResourcePrimitive::U32:
        return DataType::UnsignedInteger32;
    case EcfgResourcePrimitive::S32:
        return DataType::Integer32;
    case EcfgResourcePrimitive::U64:
        return DataType::UnsignedInteger64;
    case EcfgResourcePrimitive::S64:
        return DataType::Integer64;
    case EcfgResourcePrimitive::Q32_32:
        return DataType::Real64;
    case EcfgResourcePrimitive::RawBits:
        return DataType::OctetString;
    }
    return {};
}

bool integerParameterBoundsAreValid(const VerifiedSemanticActionParameter &parameter)
{
    switch (parameter.primitive) {
    case EcfgResourcePrimitive::Bool:
        return parameter.minimum >= 0 && parameter.maximum <= 1;
    case EcfgResourcePrimitive::U8:
        return parameter.minimum >= 0 && quint64(parameter.maximum) <= 0xff;
    case EcfgResourcePrimitive::U16:
        return parameter.minimum >= 0 && quint64(parameter.maximum) <= 0xffff;
    case EcfgResourcePrimitive::U32:
        return parameter.minimum >= 0
               && quint64(parameter.maximum) <= std::numeric_limits<quint32>::max();
    case EcfgResourcePrimitive::U64:
        return parameter.minimum >= 0;
    case EcfgResourcePrimitive::S8:
        return parameter.minimum >= std::numeric_limits<qint8>::min()
               && parameter.maximum <= std::numeric_limits<qint8>::max();
    case EcfgResourcePrimitive::S16:
        return parameter.minimum >= std::numeric_limits<qint16>::min()
               && parameter.maximum <= std::numeric_limits<qint16>::max();
    case EcfgResourcePrimitive::S32:
        return parameter.minimum >= std::numeric_limits<qint32>::min()
               && parameter.maximum <= std::numeric_limits<qint32>::max();
    case EcfgResourcePrimitive::S64:
    case EcfgResourcePrimitive::Q32_32:
        return true;
    case EcfgResourcePrimitive::RawBits:
        return false;
    }
    return false;
}

Utils::Result<Data::SemanticActionParameterRuntimeDefinition> runtimeParameter(
    const VerifiedSemanticActionParameter &parameter)
{
    const auto primitive = runtimePrimitive(parameter.primitive);
    if (!primitive || !integerParameterBoundsAreValid(parameter)) {
        return projectionError(
            QString::fromLatin1("action parameter %1 cannot be represented")
                .arg(parameter.parameterId));
    }

    Data::SemanticActionParameterRuntimeDefinition result;
    result.id = parameter.parameterId;
    result.primitiveType = *primitive;
    result.unit = parameter.unit.value_or(QString());
    switch (parameter.primitive) {
    case EcfgResourcePrimitive::Bool:
        result.minimum = bool(parameter.minimum);
        result.maximum = bool(parameter.maximum);
        break;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        result.minimum = QVariant::fromValue<qulonglong>(quint64(parameter.minimum));
        result.maximum = QVariant::fromValue<qulonglong>(quint64(parameter.maximum));
        break;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        result.minimum = QVariant::fromValue<qlonglong>(parameter.minimum);
        result.maximum = QVariant::fromValue<qlonglong>(parameter.maximum);
        break;
    case EcfgResourcePrimitive::Q32_32:
        result.minimum = double(parameter.minimum) / 4294967296.0;
        result.maximum = double(parameter.maximum) / 4294967296.0;
        break;
    case EcfgResourcePrimitive::RawBits:
        return projectionError(
            QString::fromLatin1("raw-bit action parameters require an explicit width"));
    }
    return result;
}

Utils::Result<Data::DeviceControlActionParameter> publicParameter(
    const VerifiedSemanticActionParameter &parameter)
{
    const auto dataType = publicDataType(parameter.primitive);
    if (!dataType || !integerParameterBoundsAreValid(parameter)) {
        return projectionError(
            QString::fromLatin1("action parameter %1 has no public data type")
                .arg(parameter.parameterId));
    }

    Data::DeviceControlActionParameter result;
    result.id = parameter.parameterId;
    result.displayName = parameter.parameterId;
    result.dataType = *dataType;
    result.required = true;
    result.valueMetadata.unit = parameter.unit.value_or(QString());
    result.valueMetadata.hasMinimum = true;
    result.valueMetadata.minimum = parameter.primitive == EcfgResourcePrimitive::Q32_32
                                       ? double(parameter.minimum) / 4294967296.0
                                       : double(parameter.minimum);
    result.valueMetadata.hasMaximum = true;
    result.valueMetadata.maximum = parameter.primitive == EcfgResourcePrimitive::Q32_32
                                       ? double(parameter.maximum) / 4294967296.0
                                       : double(parameter.maximum);
    return result;
}

Utils::Result<QHash<QString, Data::NodeId>> explicitProjectDeviceMap(
    const Data::ProjectSnapshot &project,
    const Data::ControllerConnectionScope &scope,
    const VerifiedSemanticBindingArtifact &artifact)
{
    if (!project.valid || project.id.isNull() || project.id != scope.projectId
        || scope.masterId.isNull()) {
        return projectionError(QString::fromLatin1("the project or controller scope is invalid"));
    }

    const qsizetype matchingMasters = std::count_if(
        project.nodes.cbegin(),
        project.nodes.cend(),
        [&scope](const Data::ProjectNodeSnapshot &node) {
            return node.kind == Data::ProjectNodeKind::Master && node.id == scope.masterId;
        });
    if (matchingMasters != 1
        || project.masterBindingArtifact.projectDeviceBindings.size() != artifact.devices.size()) {
        return projectionError(
            QString::fromLatin1("the signed project device mapping is incomplete"));
    }

    QHash<QString, Data::NodeId> result;
    QSet<Data::NodeId> slaveIds;
    for (const Data::SemanticProjectDeviceBinding &mapping :
         project.masterBindingArtifact.projectDeviceBindings) {
        if (mapping.projectDeviceId.isEmpty()
            || mapping.projectDeviceId != mapping.projectDeviceId.trimmed()
            || mapping.slaveId.isNull() || result.contains(mapping.projectDeviceId)
            || slaveIds.contains(mapping.slaveId)) {
            return projectionError(
                QString::fromLatin1("the signed project device mapping is ambiguous"));
        }

        const VerifiedSemanticDevice *device = artifact.findDevice(mapping.projectDeviceId);
        const auto slave = std::find_if(
            project.slaves.cbegin(),
            project.slaves.cend(),
            [&mapping, &scope](const Data::OfflineSlaveConfiguration &candidate) {
                return candidate.id == mapping.slaveId && candidate.masterId == scope.masterId;
            });
        const auto topology = device
                                  ? std::find_if(
                                        artifact.topologyInstances.cbegin(),
                                        artifact.topologyInstances.cend(),
                                        [&device](const SemanticBindingTopologyInstance &instance) {
                                            return instance.position == device->position
                                                   && instance.stationAddress
                                                          == device->stationAddress;
                                        })
                                  : artifact.topologyInstances.cend();
        if (!device || slave == project.slaves.cend()
            || topology == artifact.topologyInstances.cend()
            || topology->adapterId != device->adapterId
            || topology->adapterVersion != device->adapterVersion
            || topology->adapterSha256 != device->adapterSha256
            || topology->esiSha256 != device->esiSha256 || slave->position != device->position
            || slave->identity.vendorId != topology->vendorId
            || slave->identity.productCode != topology->productCode
            || (topology->revision && slave->identity.revisionNumber != *topology->revision)
            || (topology->serial && slave->serialNumber != *topology->serial)
            || slave->esiSha256 != device->esiSha256) {
            return projectionError(
                QString::fromLatin1("a mapped slave differs from the signed device instance"));
        }

        result.insert(mapping.projectDeviceId, mapping.slaveId);
        slaveIds.insert(mapping.slaveId);
    }

    for (const VerifiedSemanticDevice &device : artifact.devices) {
        if (!result.contains(device.projectDeviceId)) {
            return projectionError(
                QString::fromLatin1("a signed device has no explicit project mapping"));
        }
    }
    return result;
}

bool candidateMatchesSignedBinding(
    const Data::SemanticRuntimeBinding &candidate,
    const VerifiedSemanticBinding &binding,
    QStringView controllerId,
    const Data::ControllerConnectionScope &scope,
    const Data::NodeId &deviceId,
    const ReadOnlySemanticBindingCandidates &candidates)
{
    const auto primitive = runtimePrimitive(binding.primitive);
    const auto direction = runtimeDirection(binding.direction);
    const auto access = runtimeAccess(binding.access);
    return primitive && direction && access && candidate.target.controllerId == controllerId
           && candidate.target.scope == scope && candidate.target.deviceId == deviceId
           && candidate.target.kind == Data::SemanticRuntimeTargetKind::Signal
           && candidate.target.signalId.value == binding.semanticSignalDefinitionId
           && candidate.target.actionId.value.isEmpty()
           && candidate.semanticBindingId == binding.semanticBindingId
           && candidate.componentBindingId == binding.componentBindingId
           && candidate.adapterId.value == binding.adapterId
           && candidate.adapterVersion == binding.adapterVersion
           && candidate.adapterContentSha256 == binding.adapterSha256
           && candidate.esiSha256 == binding.esiSha256
           && candidate.bindingArtifactSha256 == candidates.mappingDigest.value
           && candidate.mappingDigest == candidates.mappingDigest
           && candidate.controllerMappingDigest == candidates.controllerMappingDigest
           && candidate.verification == candidates.verification
           && candidate.resourceId.value == opaqueBigEndian(binding.resourceId)
           && candidate.componentInstanceId.value == opaqueBigEndian(binding.componentInstanceId)
           && candidate.consistencyGroupId.value == opaqueBigEndian(binding.consistencyGroupId)
           && candidate.primitiveType == *primitive
           && candidate.valueTypeIdentity == valueTypeIdentity(binding.primitive, binding.bitWidth)
           && candidate.bitWidth == binding.bitWidth && candidate.direction == *direction
           && candidate.access == *access;
}

Utils::Result<QHash<QString, const Data::SemanticRuntimeBinding *>> verifiedCandidateBindings(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const ReadOnlySemanticBindingCandidates &candidates,
    Data::ControllerConnectionScope *scope)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    const Data::RuntimeSemanticMappingProof &proof = evidence.semanticMappingProof();
    if (candidates.verification.state != Data::SemanticBindingVerificationState::Verified
        || !validSha256Digest(candidates.mappingDigest)
        || !validSha256Digest(candidates.controllerMappingDigest)
        || !validSha256Digest(candidates.verification.signedManifestDigest)
        || candidates.mappingDigest.value != proof.mappingSha256
        || candidates.controllerMappingDigest.value != proof.mappingSha256
        || candidates.verification.signedManifestDigest.value != proof.manifestSha256
        || project.masterBindingArtifact.artifactSha256 != proof.mappingSha256
        || project.masterBindingArtifact.projectConfigurationSha256
               != evidence.projectConfigurationSha256()
        || candidates.bindings.isEmpty() || candidates.bindings.size() != artifact.bindings.size()
        || candidates.signalStates.size() != candidates.bindings.size()) {
        return projectionError(
            QString::fromLatin1("the verified binding candidate set is incomplete"));
    }

    *scope = candidates.bindings.constFirst().target.scope;
    const Utils::Result<QHash<QString, Data::NodeId>> deviceMap
        = explicitProjectDeviceMap(project, *scope, artifact);
    if (!deviceMap)
        return projectionError(deviceMap.error());

    QHash<QString, const Data::SemanticRuntimeBinding *> result;
    result.reserve(candidates.bindings.size());
    quint64 sessionGeneration = 0;
    std::optional<Data::RuntimeResourceCatalogEpoch> epoch;
    QSet<QString> targetKeys;
    for (qsizetype index = 0; index < candidates.bindings.size(); ++index) {
        const Data::SemanticRuntimeBinding &candidate = candidates.bindings.at(index);
        const Data::SemanticSignalRuntimeState &signalState = candidates.signalStates.at(index);
        if (candidate.semanticBindingId.isEmpty() || result.contains(candidate.semanticBindingId)) {
            return projectionError(
                QString::fromLatin1("the verified binding candidate set is ambiguous"));
        }
        const VerifiedSemanticBinding *binding = artifact.findBySemanticSignalId(
            candidate.semanticBindingId);
        if (!binding) {
            return projectionError(
                QString::fromLatin1("a binding candidate is absent from the signed artifact"));
        }
        const auto device = deviceMap->constFind(binding->projectDeviceId);
        if (device == deviceMap->cend()
            || !candidateMatchesSignedBinding(
                candidate, *binding, controllerId, *scope, *device, candidates)) {
            return projectionError(
                QString::fromLatin1("binding candidate %1 differs from signed evidence")
                    .arg(candidate.semanticBindingId));
        }
        if (signalState.target != candidate.target || !signalState.binding
            || *signalState.binding != candidate
            || signalState.availability != Data::SemanticSignalAvailability::Unverified
            || signalState.value || signalState.snapshotComplete) {
            return projectionError(
                QString::fromLatin1("the semantic signal candidate set was modified"));
        }
        if (!candidate.sessionGeneration) {
            return projectionError(
                QString::fromLatin1("a binding candidate has no live session generation"));
        }
        if (!sessionGeneration) {
            sessionGeneration = candidate.sessionGeneration;
            epoch = candidate.epoch;
        } else if (candidate.sessionGeneration != sessionGeneration || candidate.epoch != *epoch) {
            return projectionError(
                QString::fromLatin1("binding candidates span different live epochs"));
        }

        const QString targetKey = candidate.target.deviceId.toString() + QLatin1Char('/')
                                  + candidate.target.signalId.value;
        if (targetKeys.contains(targetKey)) {
            return projectionError(
                QString::fromLatin1("binding candidates contain duplicate semantic targets"));
        }
        targetKeys.insert(targetKey);
        result.insert(candidate.semanticBindingId, &candidate);
    }
    if (!epoch || !epoch->controllerBootId || epoch->activePackageSlot == Data::ControllerSlot::None
        || !epoch->activePackageGeneration || !epoch->configurationId || !epoch->topologyGeneration
        || !epoch->runtimeGeneration || !epoch->catalogRevision
        || epoch->configurationId != artifact.configurationId
        || epoch->catalogRevision != artifact.catalogRevision
        || epoch->topologyIdentity != opaqueBigEndian(artifact.topologyIdentity)) {
        return projectionError(
            QString::fromLatin1("binding candidates have an incomplete package epoch"));
    }
    return result;
}

bool outputStateAllowsTransactions(Data::ControllerServiceState state)
{
    return state == Data::ControllerServiceState::OperationalSafe
           || state == Data::ControllerServiceState::Running
           || state == Data::ControllerServiceState::Paused;
}

std::optional<quint32> strictMaximumTtlCycles(const VerifiedSemanticAction &action)
{
    if (action.consistencyGroups.isEmpty())
        return {};

    quint32 maximum = std::numeric_limits<quint32>::max();
    for (const VerifiedSemanticActionGroup &group : action.consistencyGroups) {
        if (!group.maximumTtlCycles || group.maximumTtlCycles > 65535)
            return {};
        maximum = qMin(maximum, group.maximumTtlCycles);
    }
    return maximum;
}

} // namespace

Utils::Result<QList<Data::SemanticActionRuntimeState>> buildSemanticActionRuntimeStates(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const ReadOnlySemanticBindingCandidates &candidates,
    const SemanticActionRuntimeGates &gates)
{
    if (!validControllerId(controllerId))
        return projectionError(QString::fromLatin1("the controller ID is invalid"));
    if (!evidence.isValid() || !evidence.permitsWritableActions() || !evidence.actionDefinitions()) {
        return projectionError(
            QString::fromLatin1("signed writable action evidence is unavailable"));
    }

    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    if (artifact.actions.isEmpty()) {
        return projectionError(
            QString::fromLatin1("the signed package exposes no semantic actions"));
    }

    Data::ControllerConnectionScope scope;
    const Utils::Result<QHash<QString, const Data::SemanticRuntimeBinding *>> bindingById
        = verifiedCandidateBindings(controllerId, project, evidence, candidates, &scope);
    if (!bindingById)
        return projectionError(bindingById.error());

    const Utils::Result<QHash<QString, Data::NodeId>> deviceMap
        = explicitProjectDeviceMap(project, scope, artifact);
    if (!deviceMap)
        return projectionError(deviceMap.error());

    QList<Data::SemanticActionRuntimeState> result;
    result.reserve(artifact.actions.size());
    QSet<QString> actionBindingIds;
    QSet<QString> actionTargets;
    for (const VerifiedSemanticAction &action : artifact.actions) {
        const auto device = deviceMap->constFind(action.projectDeviceId);
        const std::optional<quint32> maximumTtlCycles = strictMaximumTtlCycles(action);
        if (action.actionBindingId.isEmpty() || action.actionDefinitionId.isEmpty()
            || action.actionDefinitionSha256.size() != 32 || device == deviceMap->cend()
            || !maximumTtlCycles || actionBindingIds.contains(action.actionBindingId)) {
            return projectionError(QString::fromLatin1("a signed action projection is incomplete"));
        }
        actionBindingIds.insert(action.actionBindingId);

        Data::SemanticActionRuntimeState state;
        state.target.controllerId = controllerId.toString();
        state.target.scope = scope;
        state.target.deviceId = *device;
        state.target.kind = Data::SemanticRuntimeTargetKind::Action;
        state.target.actionId = {action.actionBindingId};
        state.actionBindingId = action.actionBindingId;
        state.actionDefinitionId = action.actionDefinitionId;
        state.actionDefinitionDigest = {
            QStringLiteral("sha256"),
            action.actionDefinitionSha256,
        };
        state.qualification = action.qualification == VerifiedSemanticActionQualification::Qualified
                                  ? Data::SemanticActionQualification::Qualified
                                  : Data::SemanticActionQualification::Unqualified;
        state.disabledReason = action.disabledReason.value_or(QString());
        state.requiresApproval = true;
        state.requiresExclusiveControl = true;
        state.requiresDc = action.dcRequired;
        state.holdToRun = false;
        state.maximumTtlCycles = *maximumTtlCycles;

        state.definition.id = state.target.actionId;
        state.definition.displayName = action.actionDefinitionId;
        state.definition.enabled = action.enabled;
        state.definition.requiresExclusiveControl = true;
        state.definition.requiresDc = action.dcRequired;
        state.definition.holdToRun = false;

        const QString actionTarget = state.target.deviceId.toString() + QLatin1Char('/')
                                     + state.target.actionId.value;
        if (actionTargets.contains(actionTarget)) {
            return projectionError(QString::fromLatin1("the signed action target is ambiguous"));
        }
        actionTargets.insert(actionTarget);

        QSet<QString> actionBindingReferences;
        const auto appendBinding =
            [&actionBindingReferences,
             &bindingById,
             &state,
             &action](const VerifiedSemanticActionBindingReference &reference, bool required)
            -> Utils::Result<> {
            const auto candidate = bindingById->constFind(reference.semanticBindingId);
            const auto primitive = runtimePrimitive(reference.primitive);
            const auto direction = runtimeDirection(reference.direction);
            const auto access = runtimeAccess(reference.access);
            if (!primitive || !direction || !access || candidate == bindingById->cend()
                || actionBindingReferences.contains(reference.semanticBindingId)
                || (*candidate)->target.deviceId != state.target.deviceId
                || (*candidate)->target.signalId.value != reference.semanticSignalDefinitionId
                || (*candidate)->componentBindingId != reference.componentBindingId
                || (*candidate)->resourceId.value != opaqueBigEndian(reference.resourceId)
                || (*candidate)->consistencyGroupId.value
                       != opaqueBigEndian(reference.consistencyGroupId)
                || (*candidate)->primitiveType != *primitive
                || (*candidate)->bitWidth != reference.bitWidth
                || (*candidate)->direction != *direction || (*candidate)->access != *access) {
                return projectionError(
                    QString::fromLatin1("action %1 has a mismatched binding reference")
                        .arg(action.actionBindingId));
            }
            actionBindingReferences.insert(reference.semanticBindingId);
            state.bindings.append(**candidate);
            if (required) {
                state.definition.requiredSignals.append({reference.semanticSignalDefinitionId});
            }
            return Utils::ResultOk;
        };

        for (const VerifiedSemanticActionBindingReference &reference : action.requiredBindings) {
            const Utils::Result<> appendResult = appendBinding(reference, true);
            if (!appendResult)
                return projectionError(appendResult.error());
        }
        for (const VerifiedSemanticActionBindingReference &reference : action.optionalBindings) {
            const Utils::Result<> appendResult = appendBinding(reference, false);
            if (!appendResult)
                return projectionError(appendResult.error());
        }
        if (state.bindings.isEmpty()) {
            return projectionError(
                QString::fromLatin1("a signed action has no verified runtime bindings"));
        }

        QSet<QString> parameterIds;
        for (const VerifiedSemanticActionParameter &parameter : action.parameters) {
            if (parameter.parameterId.isEmpty() || parameterIds.contains(parameter.parameterId)) {
                return projectionError(
                    QString::fromLatin1("a signed action parameter is ambiguous"));
            }
            parameterIds.insert(parameter.parameterId);

            const Utils::Result<Data::SemanticActionParameterRuntimeDefinition> runtimeDefinition
                = runtimeParameter(parameter);
            if (!runtimeDefinition)
                return projectionError(runtimeDefinition.error());
            state.parameters.append(*runtimeDefinition);

            const Utils::Result<Data::DeviceControlActionParameter> definitionParameter
                = publicParameter(parameter);
            if (!definitionParameter)
                return projectionError(definitionParameter.error());
            state.definition.parameters.append(*definitionParameter);
        }

        if (!action.enabled
            || action.qualification != VerifiedSemanticActionQualification::Qualified
            || action.disabledReason) {
            state.availability = Data::SemanticActionAvailability::Rejected;
            if (state.disabledReason.isEmpty())
                state.disabledReason = QStringLiteral("signed_action_not_qualified");
            state.detail = state.disabledReason;
        } else if (!gates.outputTransactionsSupported) {
            state.availability = Data::SemanticActionAvailability::Unavailable;
            state.detail = QStringLiteral("runtime_output_transactions_unavailable");
        } else if (!gates.ownsExclusiveControl) {
            state.availability = Data::SemanticActionAvailability::Unavailable;
            state.detail = QStringLiteral("exclusive_control_not_owned");
        } else if (!outputStateAllowsTransactions(gates.serviceState)) {
            state.availability = Data::SemanticActionAvailability::Unavailable;
            state.detail = QStringLiteral("controller_state_disallows_output_transactions");
        } else if (action.dcRequired && !gates.dcRuntimeActive) {
            state.availability = Data::SemanticActionAvailability::Unavailable;
            state.detail = QStringLiteral("distributed_clocks_not_active");
        } else {
            if (evidence.invocableAction(action.actionBindingId, gates.dcRuntimeActive) != &action) {
                return projectionError(
                    QString::fromLatin1("the signed action invocation gate is inconsistent"));
            }
            state.availability = Data::SemanticActionAvailability::Ready;
        }

        // The signed execution plan remains private in VerifiedSemanticAction.
        // Public DeviceControlAction::steps is intentionally left empty.
        result.append(std::move(state));
    }
    return result;
}

} // namespace EtherCAT::SemanticRuntime::Internal
