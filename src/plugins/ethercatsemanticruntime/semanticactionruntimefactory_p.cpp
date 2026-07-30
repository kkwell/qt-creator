// Copyright (C) 2026 Embed Labs

#include "semanticactionruntimefactory_p.h"

#include <ethercatcore/manualcontrolcontract.h>

#include <QtEndian>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <optional>
#include <variant>

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
            || topology->esiSha256 != device->esiSha256 || !slave->stationAddress
            || slave->stationAddress != device->stationAddress
            || slave->position != device->position
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

struct ManualActionAuthorization
{
    bool authorized = false;
    bool policyProjected = false;
    QString rejection;
    Data::DeviceControlAction definition;
    QList<Data::SemanticActionParameterRuntimeDefinition> parameters;
    quint32 maximumTtlMs = 0;
    quint32 maximumTtlCycles = 0;
};

std::optional<Data::EngineeringValueKind> rawEngineeringKind(EcfgResourcePrimitive primitive)
{
    using Kind = Data::EngineeringValueKind;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return Kind::Boolean;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        return Kind::UnsignedInteger;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
    case EcfgResourcePrimitive::Q32_32:
        return Kind::SignedInteger;
    case EcfgResourcePrimitive::RawBits:
        return {};
    }
    return {};
}

std::optional<std::variant<qint64, quint64>> exactRawValue(
    const Data::EngineeringValue &engineering,
    EcfgResourcePrimitive primitive,
    quint16 bitWidth,
    const Data::EngineeringTransform &transform)
{
    const std::optional<Data::EngineeringValueKind> rawKind = rawEngineeringKind(primitive);
    if (!rawKind)
        return {};

    const Core::EngineeringConversionResult converted = Core::convertEngineeringToRaw(
        engineering, *rawKind, bitWidth, transform);
    if (!converted.validation.accepted() || !converted.value)
        return {};

    switch (converted.value->kind) {
    case Data::EngineeringValueKind::Boolean:
        return std::variant<qint64, quint64>{quint64(converted.value->boolean)};
    case Data::EngineeringValueKind::SignedInteger:
        return std::variant<qint64, quint64>{converted.value->signedInteger};
    case Data::EngineeringValueKind::UnsignedInteger:
        return std::variant<qint64, quint64>{converted.value->unsignedInteger};
    case Data::EngineeringValueKind::Invalid:
    case Data::EngineeringValueKind::ExactRational:
    case Data::EngineeringValueKind::Enumeration:
        return {};
    }
    return {};
}

const Data::SemanticSignalDefinition *uniqueAdapterSignal(
    const Data::DeviceAdapterManifest &manifest, QStringView signalId)
{
    // Runtime authorization consumes explicit resolved signal identities. The
    // API-038 XB6 package expands its fixed slot-1 signals in semanticSignals;
    // future dynamic module slots must be resolved before reaching this gate.
    const Data::SemanticSignalDefinition *result = nullptr;
    for (const Data::SemanticSignalDefinition &signal : manifest.semanticSignals) {
        if (signal.id.value != signalId)
            continue;
        if (result)
            return nullptr;
        result = &signal;
    }
    for (const Data::DeviceModuleProfile &module : manifest.moduleProfiles) {
        for (const Data::SemanticSignalDefinition &signal : module.slotRelativeSignals) {
            if (signal.id.value != signalId)
                continue;
            if (result)
                return nullptr;
            result = &signal;
        }
    }
    return result;
}

std::optional<Data::SemanticSignalDirection> adapterDirection(EcfgResourceDirection direction)
{
    if (direction == EcfgResourceDirection::Input)
        return Data::SemanticSignalDirection::Input;
    if (direction == EcfgResourceDirection::Output)
        return Data::SemanticSignalDirection::Output;
    return {};
}

std::optional<Data::SemanticSignalAccess> adapterAccess(EcfgResourceAccess access)
{
    if (access == EcfgResourceAccess::Read)
        return Data::SemanticSignalAccess::ReadOnly;
    if (access == EcfgResourceAccess::ReadWrite)
        return Data::SemanticSignalAccess::ReadWrite;
    return {};
}

bool signalContractMatchesSignedBinding(
    const Data::DeviceAdapterManifest &manifest,
    const VerifiedSemanticBindingArtifact &artifact,
    const VerifiedSemanticActionBindingReference &reference,
    QStringView projectDeviceId,
    bool requireSafeValue)
{
    const Data::SemanticSignalDefinition *signal = uniqueAdapterSignal(
        manifest, reference.semanticSignalDefinitionId);
    const VerifiedSemanticBinding *binding = artifact.findBySemanticSignalId(
        reference.semanticBindingId);
    const std::optional<Data::SemanticSignalDirection> direction = adapterDirection(
        reference.direction);
    const std::optional<Data::SemanticSignalAccess> access = adapterAccess(reference.access);
    const std::optional<Data::EtherCATDataType> dataType = publicDataType(reference.primitive);
    if (!signal || !binding || !direction || !access || !dataType
        || binding->projectDeviceId != projectDeviceId
        || binding->semanticBindingId != reference.semanticBindingId
        || binding->semanticSignalDefinitionId != reference.semanticSignalDefinitionId
        || binding->componentBindingId != reference.componentBindingId
        || binding->resourceId != reference.resourceId
        || binding->consistencyGroupId != reference.consistencyGroupId
        || binding->primitive != reference.primitive || binding->bitWidth != reference.bitWidth
        || binding->direction != reference.direction || binding->access != reference.access
        || binding->source
               != (reference.direction == EcfgResourceDirection::Output
                       ? EcfgResourceSource::PdoOutput
                       : EcfgResourceSource::PdoInput)
        || binding->unit != reference.unit
        || binding->scaleNumerator != reference.scaleNumerator
        || binding->scaleDenominator != reference.scaleDenominator
        || binding->scaleOffset != reference.scaleOffset || signal->direction != *direction
        || signal->access != *access || signal->bindings.size() != 1
        || !signal->engineeringTransform) {
        return false;
    }

    const Data::DeviceSignalBinding &deviceBinding = signal->bindings.constFirst();
    const Data::EngineeringTransform &transform = *signal->engineeringTransform;
    if (deviceBinding.physicalType != *dataType || deviceBinding.bitWidth != reference.bitWidth
        || deviceBinding.kind != Data::DeviceSignalBindingKind::ProcessDataObject
        || deviceBinding.pdoDirection
               != (reference.direction == EcfgResourceDirection::Output ? Data::PdoDirection::Rx
                                                                        : Data::PdoDirection::Tx)
        || transform.unit != reference.unit.value_or(QString())
        || transform.scale
               != Data::ExactRational{
                   reference.scaleNumerator,
                   reference.scaleDenominator,
               }
        || transform.offset != Data::ExactRational{reference.scaleOffset, 1}
        || transform.rounding != Data::EngineeringRounding::RejectInexact
        || !Core::validateEngineeringTransform(transform).accepted()) {
        return false;
    }

    if (!requireSafeValue)
        return true;
    if (reference.direction != EcfgResourceDirection::Output
        || reference.access != EcfgResourceAccess::ReadWrite || !binding->safeValueDeclared
        || !binding->safeValue || !signal->engineeringSafeValue) {
        return false;
    }
    const std::optional<std::variant<qint64, quint64>> adapterSafeValue = exactRawValue(
        *signal->engineeringSafeValue, reference.primitive, reference.bitWidth, transform);
    return adapterSafeValue && *adapterSafeValue == *binding->safeValue;
}

std::optional<Data::EngineeringConstraint> signedParameterConstraint(
    const VerifiedSemanticActionParameter &parameter)
{
    if (parameter.primitive == EcfgResourcePrimitive::RawBits)
        return {};

    qint64 denominator = 1;
    if (parameter.primitive == EcfgResourcePrimitive::Q32_32)
        denominator = qint64(1) << 32;
    const Core::ExactRationalResult minimum
        = Core::normalizedExactRational(parameter.minimum, denominator);
    const Core::ExactRationalResult maximum
        = Core::normalizedExactRational(parameter.maximum, denominator);
    if (!minimum.validation.accepted() || !maximum.validation.accepted())
        return {};

    Data::EngineeringConstraint result;
    result.minimum = minimum.value;
    result.maximum = maximum.value;
    return result;
}

std::optional<QVariant> runtimeBound(
    EcfgResourcePrimitive primitive, const Data::ExactRational &bound)
{
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        if (bound.denominator != 1 || bound.numerator < 0 || bound.numerator > 1)
            return {};
        return QVariant(bool(bound.numerator));
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        if (bound.denominator != 1 || bound.numerator < 0)
            return {};
        return QVariant::fromValue<qulonglong>(quint64(bound.numerator));
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        if (bound.denominator != 1)
            return {};
        return QVariant::fromValue<qlonglong>(bound.numerator);
    case EcfgResourcePrimitive::Q32_32:
    case EcfgResourcePrimitive::RawBits:
        // Q32.32 writes remain unsupported by the current execution plan,
        // and raw bits have no signed engineering-range contract.
        return {};
    }
    return {};
}

bool exactActionParameters(
    const VerifiedSemanticAction &signedAction,
    const Data::DeviceControlAction &adapterAction,
    const Data::ManualActionEnvelope &manualAction,
    QList<Data::SemanticActionParameterRuntimeDefinition> *runtimeParameters)
{
    if (adapterAction.parameters.size() != signedAction.parameters.size()
        || manualAction.parameters.size() != signedAction.parameters.size()) {
        return false;
    }

    QSet<QString> adapterIds;
    QSet<QString> manualIds;
    for (const VerifiedSemanticActionParameter &parameter : signedAction.parameters) {
        const Utils::Result<Data::DeviceControlActionParameter> publicDefinition
            = publicParameter(parameter);
        const std::optional<Data::EngineeringConstraint> constraint
            = signedParameterConstraint(parameter);
        if (!publicDefinition || !constraint || adapterIds.contains(parameter.parameterId)
            || manualIds.contains(parameter.parameterId)) {
            return false;
        }

        QList<const Data::DeviceControlActionParameter *> adapterMatches;
        for (const Data::DeviceControlActionParameter &candidate : adapterAction.parameters) {
            if (candidate.id == parameter.parameterId)
                adapterMatches.append(&candidate);
        }
        QList<const Data::ManualActionParameterEnvelope *> manualMatches;
        for (const Data::ManualActionParameterEnvelope &candidate : manualAction.parameters) {
            if (candidate.parameterId == parameter.parameterId)
                manualMatches.append(&candidate);
        }
        if (adapterMatches.size() != 1 || manualMatches.size() != 1)
            return false;

        const Data::DeviceControlActionParameter &adapter = *adapterMatches.constFirst();
        const Data::ManualActionParameterEnvelope &manual = *manualMatches.constFirst();
        const bool manualRangeValid
            = Core::validateEngineeringConstraint(manual.allowedRange).accepted()
              && manual.allowedRange.minimum && manual.allowedRange.maximum
              && manual.allowedRange.enumeration.isEmpty()
              && Core::validateEngineeringValueAgainstConstraint(
                     Data::EngineeringValue::fromExactRational(
                         *manual.allowedRange.minimum),
                     *constraint)
                     .accepted()
              && Core::validateEngineeringValueAgainstConstraint(
                     Data::EngineeringValue::fromExactRational(
                         *manual.allowedRange.maximum),
                     *constraint)
                     .accepted();
        const std::optional<QVariant> minimum
            = manualRangeValid
                  ? runtimeBound(parameter.primitive, *manual.allowedRange.minimum)
                  : std::nullopt;
        const std::optional<QVariant> maximum
            = manualRangeValid
                  ? runtimeBound(parameter.primitive, *manual.allowedRange.maximum)
                  : std::nullopt;
        const bool adapterDefaultValid
            = adapter.engineeringConstraint
              && adapter.hasDefaultValue == adapter.engineeringDefaultValue.has_value()
              && (!adapter.engineeringDefaultValue
                  || Core::validateEngineeringValueAgainstConstraint(
                         *adapter.engineeringDefaultValue, *adapter.engineeringConstraint)
                         .accepted());
        if (!adapter.required || adapter.dataType != publicDefinition->dataType
            || adapter.valueMetadata.unit != publicDefinition->valueMetadata.unit
            || !adapter.engineeringConstraint
            || adapter.engineeringConstraint->minimum != constraint->minimum
            || adapter.engineeringConstraint->maximum != constraint->maximum
            || adapter.engineeringConstraint->step != Data::ExactRational{1, 1}
            || adapter.engineeringConstraint->stepOrigin != Data::ExactRational{0, 1}
            || !adapter.engineeringConstraint->enumeration.isEmpty()
            || adapter.defaultValue.isValid() || !adapterDefaultValid
            || !minimum || !maximum || manual.defaultValue
            || (manual.allowedRange.step
                && manual.allowedRange.step != adapter.engineeringConstraint->step)
            || (manual.allowedRange.stepOrigin
                && manual.allowedRange.stepOrigin != adapter.engineeringConstraint->stepOrigin)) {
            return false;
        }

        Data::SemanticActionParameterRuntimeDefinition runtime = *runtimeParameter(parameter);
        runtime.minimum = *minimum;
        runtime.maximum = *maximum;
        runtimeParameters->append(std::move(runtime));
        adapterIds.insert(parameter.parameterId);
        manualIds.insert(parameter.parameterId);
    }
    return adapterIds.size() == adapterAction.parameters.size()
           && manualIds.size() == manualAction.parameters.size();
}

bool exactActionSignalSets(
    const VerifiedSemanticAction &signedAction,
    const Data::DeviceControlAction &adapterAction)
{
    const auto signedIds =
        [](const QList<VerifiedSemanticActionBindingReference> &bindings)
        -> std::optional<QStringList> {
        QStringList result;
        result.reserve(bindings.size());
        for (const VerifiedSemanticActionBindingReference &binding : bindings)
            result.append(binding.semanticSignalDefinitionId);
        std::sort(result.begin(), result.end());
        if (std::adjacent_find(result.cbegin(), result.cend()) != result.cend())
            return {};
        return result;
    };
    const auto adapterIds =
        [](const QList<Data::SemanticSignalId> &signalIds) -> std::optional<QStringList> {
        QStringList result;
        result.reserve(signalIds.size());
        for (const Data::SemanticSignalId &signal : signalIds)
            result.append(signal.value);
        std::sort(result.begin(), result.end());
        if (std::adjacent_find(result.cbegin(), result.cend()) != result.cend())
            return {};
        return result;
    };

    const std::optional<QStringList> signedRequired = signedIds(signedAction.requiredBindings);
    const std::optional<QStringList> signedOptional = signedIds(signedAction.optionalBindings);
    const std::optional<QStringList> adapterRequired = adapterIds(adapterAction.requiredSignals);
    const std::optional<QStringList> adapterOptional = adapterIds(adapterAction.optionalSignals);
    if (!signedRequired || !signedOptional || !adapterRequired || !adapterOptional
        || *signedRequired != *adapterRequired || *signedOptional != *adapterOptional) {
        return false;
    }
    QSet<QString> requiredSet(signedRequired->cbegin(), signedRequired->cend());
    return std::none_of(
        signedOptional->cbegin(),
        signedOptional->cend(),
        [&requiredSet](const QString &id) { return requiredSet.contains(id); });
}

const VerifiedSemanticActionBindingReference *actionReferenceForDefinition(
    const VerifiedSemanticAction &action, QStringView definitionId)
{
    const VerifiedSemanticActionBindingReference *result = nullptr;
    for (const auto *bindings : {&action.requiredBindings, &action.optionalBindings}) {
        for (const VerifiedSemanticActionBindingReference &binding : *bindings) {
            if (binding.semanticSignalDefinitionId != definitionId)
                continue;
            if (result)
                return nullptr;
            result = &binding;
        }
    }
    return result;
}

const VerifiedSemanticActionBindingReference *actionReferenceForBinding(
    const VerifiedSemanticAction &action, QStringView semanticBindingId)
{
    const VerifiedSemanticActionBindingReference *result = nullptr;
    for (const auto *bindings : {&action.requiredBindings, &action.optionalBindings}) {
        for (const VerifiedSemanticActionBindingReference &binding : *bindings) {
            if (binding.semanticBindingId != semanticBindingId)
                continue;
            if (result)
                return nullptr;
            result = &binding;
        }
    }
    return result;
}

bool exactActionSignalContracts(
    const Data::DeviceAdapterManifest &manifest,
    const VerifiedSemanticBindingArtifact &artifact,
    const VerifiedSemanticAction &action)
{
    QSet<QString> definitionIds;
    QSet<QString> bindingIds;
    for (const auto *bindings : {&action.requiredBindings, &action.optionalBindings}) {
        for (const VerifiedSemanticActionBindingReference &reference : *bindings) {
            if (definitionIds.contains(reference.semanticSignalDefinitionId)
                || bindingIds.contains(reference.semanticBindingId)
                || !signalContractMatchesSignedBinding(
                    manifest, artifact, reference, action.projectDeviceId, false)) {
                return false;
            }
            definitionIds.insert(reference.semanticSignalDefinitionId);
            bindingIds.insert(reference.semanticBindingId);
        }
    }
    return !definitionIds.isEmpty();
}

std::optional<EcfgOutputRecoveryPolicy> signedRecovery(
    Data::DeviceControlGroupRecovery recovery)
{
    if (recovery == Data::DeviceControlGroupRecovery::ReturnTask)
        return EcfgOutputRecoveryPolicy::ReturnTask;
    if (recovery == Data::DeviceControlGroupRecovery::HoldSafe)
        return EcfgOutputRecoveryPolicy::HoldSafe;
    return {};
}

bool exactConsistencyGroups(
    const Data::DeviceAdapterManifest &manifest,
    const VerifiedSemanticBindingArtifact &artifact,
    const VerifiedSemanticAction &signedAction,
    const Data::DeviceControlAction &adapterAction,
    QHash<QString, quint32> *numericGroupByLocalGroup)
{
    if (adapterAction.consistencyGroups.size() != signedAction.consistencyGroups.size()
        || adapterAction.consistencyGroups.isEmpty()) {
        return false;
    }

    QHash<quint32, const VerifiedSemanticActionGroup *> signedGroups;
    for (const VerifiedSemanticActionGroup &group : signedAction.consistencyGroups) {
        if (!group.consistencyGroupId || signedGroups.contains(group.consistencyGroupId))
            return false;
        signedGroups.insert(group.consistencyGroupId, &group);
    }

    QSet<quint32> mappedGroups;
    for (const Data::DeviceControlConsistencyGroup &adapterGroup :
         adapterAction.consistencyGroups) {
        if (adapterGroup.id.isEmpty() || numericGroupByLocalGroup->contains(adapterGroup.id)
            || adapterGroup.members.isEmpty()) {
            return false;
        }

        quint32 numericGroupId = 0;
        QSet<QString> adapterMembers;
        for (const Data::SemanticSignalId &member : adapterGroup.members) {
            const VerifiedSemanticActionBindingReference *reference
                = actionReferenceForDefinition(signedAction, member.value);
            if (!reference || adapterMembers.contains(member.value)
                || reference->direction != EcfgResourceDirection::Output
                || reference->access != EcfgResourceAccess::ReadWrite
                || !reference->consistencyGroupId
                || (numericGroupId && numericGroupId != reference->consistencyGroupId)
                || !signalContractMatchesSignedBinding(
                    manifest,
                    artifact,
                    *reference,
                    signedAction.projectDeviceId,
                    true)) {
                return false;
            }
            numericGroupId = reference->consistencyGroupId;
            adapterMembers.insert(member.value);
        }

        const auto signedGroup = signedGroups.constFind(numericGroupId);
        const std::optional<EcfgOutputRecoveryPolicy> recovery = signedRecovery(
            adapterGroup.recovery);
        if (!numericGroupId || signedGroup == signedGroups.cend() || !recovery
            || (*signedGroup)->recoveryPolicy != *recovery
            || (*signedGroup)->maximumTtlCycles != adapterGroup.maximumTtlCycles
            || mappedGroups.contains(numericGroupId)) {
            return false;
        }

        QSet<QString> signedMembers;
        QSet<QString> signedBindingIds;
        for (const auto *bindings :
             {&signedAction.requiredBindings, &signedAction.optionalBindings}) {
            for (const VerifiedSemanticActionBindingReference &reference : *bindings) {
                if (reference.consistencyGroupId == numericGroupId
                    && reference.direction == EcfgResourceDirection::Output
                    && reference.access == EcfgResourceAccess::ReadWrite) {
                    signedMembers.insert(reference.semanticSignalDefinitionId);
                    signedBindingIds.insert(reference.semanticBindingId);
                }
            }
        }
        if (signedMembers != adapterMembers)
            return false;

        QSet<QString> completeControllerMembers;
        QSet<QString> completeControllerBindingIds;
        for (const VerifiedSemanticBinding &binding : artifact.bindings) {
            if (binding.projectDeviceId == signedAction.projectDeviceId
                && binding.consistencyGroupId == numericGroupId
                && binding.direction == EcfgResourceDirection::Output
                && binding.access == EcfgResourceAccess::ReadWrite) {
                completeControllerMembers.insert(binding.semanticSignalDefinitionId);
                completeControllerBindingIds.insert(binding.semanticBindingId);
            }
        }
        if (completeControllerMembers != signedMembers
            || completeControllerBindingIds != signedBindingIds) {
            return false;
        }

        numericGroupByLocalGroup->insert(adapterGroup.id, numericGroupId);
        mappedGroups.insert(numericGroupId);
    }
    return mappedGroups.size() == signedGroups.size();
}

bool exactActionSteps(
    const Data::DeviceAdapterManifest &manifest,
    const VerifiedSemanticAction &signedAction,
    const Data::DeviceControlAction &adapterAction,
    const QHash<QString, quint32> &numericGroupByLocalGroup)
{
    if (adapterAction.steps.size() != signedAction.steps.size() || adapterAction.steps.isEmpty())
        return false;

    const auto rawValueForReference =
        [&manifest](
            const VerifiedSemanticActionBindingReference &reference,
            const std::optional<Data::EngineeringValue> &engineering)
        -> std::optional<std::variant<qint64, quint64>> {
        const Data::SemanticSignalDefinition *signal = uniqueAdapterSignal(
            manifest, reference.semanticSignalDefinitionId);
        if (!signal || !signal->engineeringTransform || !engineering)
            return {};
        return exactRawValue(
            *engineering, reference.primitive, reference.bitWidth, *signal->engineeringTransform);
    };
    const auto nonnegativeRaw =
        [](const std::variant<qint64, quint64> &raw) -> std::optional<quint64> {
        if (std::holds_alternative<quint64>(raw))
            return std::get<quint64>(raw);
        const qint64 value = std::get<qint64>(raw);
        return value < 0 ? std::nullopt : std::optional<quint64>(quint64(value));
    };
    const auto neutralValue = [](const Data::DeviceControlValue &value) {
        return value.source == Data::DeviceControlValueSource::Invalid
               && !value.literalValue.isValid() && !value.engineeringLiteralValue
               && value.parameterId.isEmpty();
    };
    const auto literalValue = [](const Data::DeviceControlValue &value) {
        return value.source == Data::DeviceControlValueSource::Literal
               && !value.literalValue.isValid() && value.engineeringLiteralValue
               && value.parameterId.isEmpty();
    };
    const auto parameterValue = [](const Data::DeviceControlValue &value) {
        return value.source == Data::DeviceControlValueSource::Parameter
               && !value.literalValue.isValid() && !value.engineeringLiteralValue
               && !value.parameterId.isEmpty();
    };

    for (qsizetype index = 0; index < signedAction.steps.size(); ++index) {
        const VerifiedSemanticActionStep &signedStep = signedAction.steps.at(index);
        const Data::DeviceControlStep &adapterStep = adapterAction.steps.at(index);
        if (adapterStep.timeoutMs)
            return false;

        if (signedStep.kind == VerifiedSemanticActionStepKind::WriteGroup) {
            const auto group = numericGroupByLocalGroup.constFind(
                adapterStep.consistencyGroupId);
            if (adapterStep.kind != Data::DeviceControlStepKind::WriteGroup
                || group == numericGroupByLocalGroup.cend()
                || *group != signedStep.consistencyGroupId || adapterStep.timeoutCycles
                || !adapterStep.signalId.value.isEmpty()
                || !neutralValue(adapterStep.value) || !neutralValue(adapterStep.mask)
                || adapterStep.assignments.size() != signedStep.assignments.size()) {
                return false;
            }

            QHash<QString, const VerifiedSemanticActionAssignment *> signedAssignments;
            for (const VerifiedSemanticActionAssignment &assignment : signedStep.assignments) {
                const VerifiedSemanticActionBindingReference *reference
                    = actionReferenceForBinding(signedAction, assignment.semanticBindingId);
                if (!reference
                    || signedAssignments.contains(reference->semanticSignalDefinitionId)) {
                    return false;
                }
                signedAssignments.insert(
                    reference->semanticSignalDefinitionId, &assignment);
            }
            QSet<QString> adapterAssignments;
            for (const Data::DeviceControlGroupAssignment &assignment :
                 adapterStep.assignments) {
                const auto signedAssignment = signedAssignments.constFind(
                    assignment.signalId.value);
                const VerifiedSemanticActionBindingReference *reference
                    = actionReferenceForDefinition(signedAction, assignment.signalId.value);
                if (signedAssignment == signedAssignments.cend() || !reference
                    || adapterAssignments.contains(assignment.signalId.value)) {
                    return false;
                }
                adapterAssignments.insert(assignment.signalId.value);

                if (assignment.value.source == Data::DeviceControlValueSource::Literal) {
                    const std::optional<std::variant<qint64, quint64>> raw = rawValueForReference(
                        *reference, assignment.value.engineeringLiteralValue);
                    if (!literalValue(assignment.value) || !(*signedAssignment)->constantValue
                        || (*signedAssignment)->parameterId || !raw
                        || *raw != *(*signedAssignment)->constantValue) {
                        return false;
                    }
                } else if (
                    assignment.value.source == Data::DeviceControlValueSource::Parameter) {
                    if (!parameterValue(assignment.value)
                        || (*signedAssignment)->constantValue
                        || !(*signedAssignment)->parameterId
                        || assignment.value.parameterId != *(*signedAssignment)->parameterId) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            if (adapterAssignments.size() != signedAssignments.size())
                return false;
            continue;
        }

        const VerifiedSemanticActionBindingReference *reference = actionReferenceForBinding(
            signedAction, signedStep.semanticBindingId);
        if (!reference || adapterStep.signalId.value != reference->semanticSignalDefinitionId
            || !adapterStep.consistencyGroupId.isEmpty() || !adapterStep.assignments.isEmpty()
            || adapterStep.timeoutCycles != signedStep.timeoutCycles) {
            return false;
        }
        const Data::SemanticSignalDefinition *signal = uniqueAdapterSignal(
            manifest, reference->semanticSignalDefinitionId);
        if (!signal || !signal->engineeringTransform)
            return false;

        if (signedStep.kind == VerifiedSemanticActionStepKind::WaitMasked) {
            if (adapterStep.kind != Data::DeviceControlStepKind::WaitMaskedEquals
                || !literalValue(adapterStep.value) || !literalValue(adapterStep.mask)
                || signal->engineeringTransform->scale != Data::ExactRational{1, 1}
                || signal->engineeringTransform->offset != Data::ExactRational{0, 1}) {
                return false;
            }
            const std::optional<std::variant<qint64, quint64>> value = rawValueForReference(
                *reference, adapterStep.value.engineeringLiteralValue);
            const std::optional<std::variant<qint64, quint64>> mask = rawValueForReference(
                *reference, adapterStep.mask.engineeringLiteralValue);
            if (!value || !mask || !std::holds_alternative<quint64>(*value)
                || !std::holds_alternative<quint64>(*mask)
                || std::get<quint64>(*value) != signedStep.value
                || std::get<quint64>(*mask) != signedStep.mask) {
                return false;
            }
            continue;
        }

        if (signedStep.kind != VerifiedSemanticActionStepKind::WaitAbsoluteLimit
            || adapterStep.kind != Data::DeviceControlStepKind::WaitAbsoluteAtMost
            || !literalValue(adapterStep.value) || !neutralValue(adapterStep.mask)
            || signal->engineeringTransform->scale.denominator <= 0
            || signal->engineeringTransform->scale.numerator <= 0
            || signal->engineeringTransform->offset != Data::ExactRational{0, 1}) {
            return false;
        }
        const std::optional<std::variant<qint64, quint64>> limit = rawValueForReference(
            *reference, adapterStep.value.engineeringLiteralValue);
        const std::optional<quint64> unsignedLimit
            = limit ? nonnegativeRaw(*limit) : std::nullopt;
        if (!unsignedLimit || *unsignedLimit != signedStep.absoluteLimit) {
            return false;
        }
    }
    return true;
}

ManualActionAuthorization authorizeManualAction(
    const Data::OfflineSlaveConfiguration &slave,
    const VerifiedSemanticAction &action,
    const VerifiedSemanticBindingArtifact &artifact,
    const QList<Data::DeviceAdapterManifest> &adapterManifests,
    quint32 cyclePeriodNs,
    quint32 signedMaximumTtlCycles)
{
    ManualActionAuthorization result;
    result.rejection = QStringLiteral("manual_adapter_not_authorized");

    QList<const Data::DeviceAdapterManifest *> manifests;
    for (const Data::DeviceAdapterManifest &manifest : adapterManifests) {
        if (manifest.id == slave.adapterSelection.adapterId
            && manifest.version == slave.adapterSelection.adapterVersion
            && manifest.contentSha256 == slave.adapterSelection.adapterContentSha256) {
            manifests.append(&manifest);
        }
    }
    if (manifests.size() != 1)
        return result;

    const Data::DeviceAdapterManifest &manifest = *manifests.constFirst();
    if (manifest.contractVersion != Data::DeviceAdapterContractVersion::V3) {
        result.rejection = QStringLiteral("manual_adapter_contract_version_unsupported");
        return result;
    }
    // Bundled V3 adapters are upper-layer descriptions, not controller trust
    // anchors. Authorization comes from exact equivalence to the production
    // signed ECPKG evidence checked below.
    if (manifest.qualification != Data::DeviceAdapterQualification::Qualified
        || manifest.match.vendorId != slave.identity.vendorId
        || manifest.match.productCode != slave.identity.productCode
        || slave.identity.revisionNumber < manifest.match.minimumRevision
        || slave.identity.revisionNumber > manifest.match.maximumRevision
        || manifest.match.exactEsiSha256 != slave.esiSha256
        || manifest.provenance.sourceSha256 != slave.esiSha256) {
        result.rejection = QStringLiteral("manual_adapter_identity_mismatch");
        return result;
    }

    const VerifiedSemanticDevice *device = artifact.findDevice(action.projectDeviceId);
    QList<const SemanticBindingTopologyInstance *> topologyInstances;
    if (device) {
        for (const SemanticBindingTopologyInstance &candidate : artifact.topologyInstances) {
            if (candidate.position == device->position
                && candidate.stationAddress == device->stationAddress) {
                topologyInstances.append(&candidate);
            }
        }
    }
    if (!device || topologyInstances.size() != 1) {
        result.rejection = QStringLiteral("manual_signed_device_identity_mismatch");
        return result;
    }
    const SemanticBindingTopologyInstance &topology = *topologyInstances.constFirst();
    const Data::DeviceAdapterControllerTarget &target = manifest.controllerAdapterTarget;
    if (target.adapterId != action.adapterId || target.adapterVersion != action.adapterVersion
        || target.adapterSha256 != action.adapterSha256 || target.esiSha256 != action.esiSha256
        || target.adapterId != device->adapterId || target.adapterVersion != device->adapterVersion
        || target.adapterSha256 != device->adapterSha256 || target.esiSha256 != device->esiSha256
        || target.adapterId != topology.adapterId
        || target.adapterVersion != topology.adapterVersion
        || target.adapterSha256 != topology.adapterSha256
        || target.esiSha256 != topology.esiSha256 || target.esiSha256 != slave.esiSha256) {
        result.rejection = QStringLiteral("manual_controller_target_mismatch");
        return result;
    }

    QList<const Data::ProcessDataProfile *> selectedProfiles;
    for (const Data::ProcessDataProfile &profile : manifest.processDataProfiles) {
        if (profile.id == slave.adapterSelection.processDataProfileId)
            selectedProfiles.append(&profile);
    }
    if (selectedProfiles.size() != 1) {
        result.rejection = QStringLiteral("manual_pdo_profile_mismatch");
        return result;
    }
    const Data::ProcessDataProfile &selectedProfile = *selectedProfiles.constFirst();
    if (selectedProfile.signedPdoProfileId != topology.pdoProfile) {
        result.rejection = QStringLiteral("manual_pdo_profile_mismatch");
        return result;
    }
    if (!processDataProfileMatchesSignedTopology(
            selectedProfile, topology, action.dcRequired)) {
        result.rejection = QStringLiteral("manual_dc_profile_mismatch");
        return result;
    }

    QList<const Data::DeviceControlAction *> adapterActions;
    for (const Data::DeviceControlAction &candidate : manifest.controlActions) {
        if (candidate.id.value == action.actionDefinitionId)
            adapterActions.append(&candidate);
    }
    if (adapterActions.size() != 1) {
        result.rejection = QStringLiteral("manual_action_not_declared");
        return result;
    }
    const Data::DeviceControlAction &adapterAction = *adapterActions.constFirst();

    // The API-038 companion authorizes one exact PDO profile and the most
    // conservative local failure disposition. Broader profile/policy choices
    // remain fail-closed until they are exposed by verified signed evidence.
    if (adapterAction.enabled != action.enabled
        || adapterAction.signedQualification != Data::DeviceControlActionQualification::Qualified
        || !adapterAction.disabledReason.isEmpty()
        || adapterAction.expectedSignedDefinitionSha256 != action.actionDefinitionSha256
        || adapterAction.signedPdoProfileIds.size() != 1
        || adapterAction.signedPdoProfileIds.constFirst() != topology.pdoProfile
        || !adapterAction.requiresExclusiveControl || adapterAction.holdToRun
        || adapterAction.commandTtlMs || !adapterAction.runtimeConditions.isEmpty()
        || adapterAction.failureAction != Data::ManualControlTimeoutAction::ControlledStop
        || adapterAction.timeoutAction != Data::ManualControlTimeoutAction::ControlledStop
        || adapterAction.requiresDc != action.dcRequired
        || adapterAction.failureDisposition
               != Data::DeviceControlFailureDisposition::HoldOperationalFault
        || !exactActionSignalSets(action, adapterAction)) {
        result.rejection = QStringLiteral("manual_action_definition_mismatch");
        return result;
    }

    QSet<QString> profileSignals;
    for (const Data::SemanticSignalId &signal : selectedProfile.requiredSignals) {
        if (profileSignals.contains(signal.value)) {
            result.rejection = QStringLiteral("manual_pdo_profile_mismatch");
            return result;
        }
        profileSignals.insert(signal.value);
    }
    for (const auto *bindings : {&action.requiredBindings, &action.optionalBindings}) {
        for (const VerifiedSemanticActionBindingReference &reference : *bindings) {
            if (!profileSignals.contains(reference.semanticSignalDefinitionId)) {
                result.rejection = QStringLiteral("manual_pdo_profile_mismatch");
                return result;
            }
        }
    }

    if (!exactActionSignalContracts(manifest, artifact, action)) {
        result.rejection = QStringLiteral("manual_signal_contract_mismatch");
        return result;
    }

    QHash<QString, quint32> numericGroupByLocalGroup;
    if (!exactConsistencyGroups(
            manifest,
            artifact,
            action,
            adapterAction,
            &numericGroupByLocalGroup)) {
        result.rejection = QStringLiteral("manual_group_contract_mismatch");
        return result;
    }
    if (!exactActionSteps(manifest, action, adapterAction, numericGroupByLocalGroup)) {
        result.rejection = QStringLiteral("manual_step_contract_mismatch");
        return result;
    }

    if (!slave.manualControlEnvelope.enabled) {
        result.rejection = QStringLiteral("manual_control_disabled");
        return result;
    }

    QList<const Data::ManualActionEnvelope *> manualActions;
    for (const Data::ManualActionEnvelope &candidate :
         slave.manualControlEnvelope.actionEnvelopes) {
        if (candidate.enabled && candidate.actionId.value == action.actionDefinitionId)
            manualActions.append(&candidate);
    }
    if (manualActions.size() != 1) {
        result.rejection = QStringLiteral("manual_action_not_authorized");
        return result;
    }
    const Data::ManualActionEnvelope &manualAction = *manualActions.constFirst();

    if (adapterAction.holdToRun || manualAction.holdToRun
        || manualAction.timing.refreshTimeoutMs
        || manualAction.timing.maxContinuousHoldMs) {
        result.rejection = QStringLiteral("manual_action_hold_unsupported");
        return result;
    }
    if (!manualAction.releaseActionId.value.isEmpty()
        || !manualAction.timeoutActionId.value.isEmpty()
        || !manualAction.failureActionId.value.isEmpty()
        || !adapterAction.allowedReleaseActionIds.isEmpty()
        || !adapterAction.allowedTimeoutActionIds.isEmpty()
        || !adapterAction.allowedFailureActionIds.isEmpty()) {
        result.rejection = QStringLiteral("manual_action_fallback_unsupported");
        return result;
    }

    const Core::ManualControlContractValidation contract
        = Core::validateManualControlEnvelope(
            slave.manualControlEnvelope,
            manifest.semanticSignals,
            manifest.controlActions,
            Core::ManualControlFallbackContract::SignedControllerRecovery);
    if (!contract.accepted()) {
        result.rejection = QStringLiteral("manual_control_envelope_invalid");
        return result;
    }
    if (!exactActionParameters(
            action, adapterAction, manualAction, &result.parameters)) {
        result.rejection = QStringLiteral("manual_parameter_contract_mismatch");
        return result;
    }

    if (!cyclePeriodNs || !signedMaximumTtlCycles || !manualAction.timing.commandTtlMs) {
        result.rejection = QStringLiteral("manual_action_ttl_invalid");
        return result;
    }
    const quint64 projectCycles
        = (quint64(manualAction.timing.commandTtlMs) * 1000000ULL) / cyclePeriodNs;
    if (!projectCycles) {
        result.rejection = QStringLiteral("manual_action_ttl_invalid");
        return result;
    }

    result.definition = adapterAction;
    result.definition.id = {action.actionBindingId};
    result.definition.enabled = true;
    result.definition.holdToRun = false;
    result.definition.commandTtlMs = 0;
    result.definition.allowedReleaseActionIds.clear();
    result.definition.allowedTimeoutActionIds.clear();
    result.definition.allowedFailureActionIds.clear();
    result.definition.steps.clear();
    result.maximumTtlCycles = quint32(qMin<quint64>(signedMaximumTtlCycles, projectCycles));
    if (!result.maximumTtlCycles) {
        result.rejection = QStringLiteral("manual_action_ttl_invalid");
    } else {
        const quint64 maximumDurationNs
            = quint64(result.maximumTtlCycles) * cyclePeriodNs;
        result.maximumTtlMs = quint32(
            qMin<quint64>(
                manualAction.timing.commandTtlMs,
                maximumDurationNs / 1000000ULL));
        if (!result.maximumTtlMs) {
            result.rejection = QStringLiteral("manual_action_ttl_invalid");
        } else {
            result.definition.commandTtlMs = result.maximumTtlMs;
            result.policyProjected = true;
            result.authorized = true;
            result.rejection.clear();
        }
    }
    return result;
}

} // namespace

bool processDataProfileMatchesSignedTopology(
    const Data::ProcessDataProfile &profile,
    const SemanticBindingTopologyInstance &topology,
    bool actionRequiresDc)
{
    const QString signedTopologyDcProfile = topology.dcProfile.value_or(QString());
    return profile.signedPdoProfileId == topology.pdoProfile
           && profile.signedDcProfileId == signedTopologyDcProfile
           && (!actionRequiresDc || !signedTopologyDcProfile.isEmpty());
}

Utils::Result<QList<Data::SemanticActionRuntimeState>> buildSemanticActionRuntimeStates(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const ReadOnlySemanticBindingCandidates &candidates,
    const QList<Data::DeviceAdapterManifest> &adapterManifests,
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
            state.definition.enabled = false;
        } else {
            const auto slave = std::find_if(
                project.slaves.cbegin(),
                project.slaves.cend(),
                [&state, &scope](const Data::OfflineSlaveConfiguration &candidate) {
                    return candidate.id == state.target.deviceId
                           && candidate.masterId == scope.masterId;
                });
            if (slave == project.slaves.cend()) {
                return projectionError(
                    QString::fromLatin1("a signed action has no mapped project slave"));
            }
            const ManualActionAuthorization authorization = authorizeManualAction(
                *slave,
                action,
                artifact,
                adapterManifests,
                evidence.cyclePeriodNs(),
                *maximumTtlCycles);
            if (authorization.policyProjected) {
                state.definition = authorization.definition;
                state.parameters = authorization.parameters;
                state.maximumTtlMs = authorization.maximumTtlMs;
                state.maximumTtlCycles = authorization.maximumTtlCycles;
            }
            if (authorization.authorized) {
                state.availability = Data::SemanticActionAvailability::Unavailable;
                state.detail.clear();
                state.definition.enabled = true;
            } else {
                state.availability = Data::SemanticActionAvailability::Rejected;
                state.detail = authorization.rejection;
                state.definition.enabled = false;
            }
            if (!authorization.authorized && !authorization.policyProjected) {
                state.maximumTtlMs = 0;
                state.maximumTtlCycles = 0;
            }
        }

        if (state.availability == Data::SemanticActionAvailability::Rejected) {
            // Signed and project authorization failures are terminal. Live
            // connection gates must never make them appear transient.
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
