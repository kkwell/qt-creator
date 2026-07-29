// Copyright (C) 2026 Embed Labs

#include "readonlysemanticbindingfactory_p.h"

#include <QtEndian>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <variant>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

Utils::ResultError candidateError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Read-only semantic binding error: %1").arg(detail));
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

std::optional<Data::RuntimeResourcePrimitiveType> primitiveType(EcfgResourcePrimitive primitive)
{
    using DataType = Data::RuntimeResourcePrimitiveType;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return DataType::Boolean;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        return DataType::UnsignedInteger;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        return DataType::SignedInteger;
    case EcfgResourcePrimitive::Q32_32:
        return DataType::FloatingPoint;
    case EcfgResourcePrimitive::RawBits:
        return DataType::ByteArray;
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

std::optional<Data::SemanticSignalDirection> semanticDirection(EcfgResourceDirection direction)
{
    if (direction == EcfgResourceDirection::Input)
        return Data::SemanticSignalDirection::Input;
    if (direction == EcfgResourceDirection::Output)
        return Data::SemanticSignalDirection::Output;
    return {};
}

std::optional<Data::SemanticSignalAccess> semanticAccess(EcfgResourceAccess access)
{
    if (access == EcfgResourceAccess::Read)
        return Data::SemanticSignalAccess::ReadOnly;
    if (access == EcfgResourceAccess::ReadWrite)
        return Data::SemanticSignalAccess::ReadWrite;
    return {};
}

std::optional<Data::RuntimeResourceTypedValue> signedSafeValue(const VerifiedSemanticBinding &binding)
{
    if (!binding.safeValueDeclared)
        return std::nullopt;
    if (!binding.safeValue || binding.safeValueLittleEndian.isEmpty()
        || binding.safeValueLittleEndian.size() != binding.safeValueBytes) {
        return std::nullopt;
    }

    const auto type = primitiveType(binding.primitive);
    if (!type)
        return std::nullopt;

    QByteArray bigEndian = binding.safeValueLittleEndian;
    std::reverse(bigEndian.begin(), bigEndian.end());

    Data::RuntimeResourceTypedValue value;
    value.primitiveType = *type;
    value.typeIdentity = valueTypeIdentity(binding.primitive, binding.bitWidth);
    const auto unsignedValue = [&binding]() -> quint64 {
        if (std::holds_alternative<quint64>(*binding.safeValue))
            return std::get<quint64>(*binding.safeValue);
        return quint64(std::get<qint64>(*binding.safeValue));
    };
    const auto signedValue = [&binding]() -> qint64 {
        if (std::holds_alternative<qint64>(*binding.safeValue))
            return std::get<qint64>(*binding.safeValue);
        return qint64(std::get<quint64>(*binding.safeValue));
    };

    switch (binding.primitive) {
    case EcfgResourcePrimitive::Bool:
        value.value = bool(unsignedValue());
        break;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        value.value = QVariant::fromValue<qulonglong>(unsignedValue());
        break;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        value.value = QVariant::fromValue<qlonglong>(signedValue());
        break;
    case EcfgResourcePrimitive::Q32_32:
        value.value = double(signedValue()) / 4294967296.0;
        value.opaqueRepresentation = bigEndian;
        break;
    case EcfgResourcePrimitive::RawBits:
        value.value = bigEndian;
        break;
    }
    return value;
}

bool descriptorMatchesSignedBinding(
    const Data::RuntimeResourceDescriptor &descriptor, const VerifiedSemanticBinding &binding)
{
    const auto type = primitiveType(binding.primitive);
    const auto direction = runtimeDirection(binding.direction);
    const auto access = runtimeAccess(binding.access);
    if (!type || !direction || !access)
        return false;

    const QByteArray expectedParent = binding.parentInstanceId
                                          ? opaqueBigEndian(binding.parentInstanceId)
                                          : QByteArray();
    const bool safeValueDeclared = descriptor.safeValue.has_value();
    if (binding.safeValueDeclared != safeValueDeclared)
        return false;
    if (safeValueDeclared) {
        const auto expectedSafeValue = signedSafeValue(binding);
        if (!expectedSafeValue || descriptor.safeValue != expectedSafeValue)
            return false;
    }

    // displayName and description are provider presentation fields. Unit is not carried by the
    // Product API descriptor; when a provider does supply one it may not contradict the signed
    // unit. None of these fields participates in resource lookup.
    const QString expectedUnit = binding.unit.value_or(QString());
    if (!descriptor.unit.isEmpty() && descriptor.unit != expectedUnit)
        return false;

    return descriptor.id.value == opaqueBigEndian(binding.resourceId)
           && descriptor.componentInstanceId.value == opaqueBigEndian(binding.componentInstanceId)
           && descriptor.parentInstanceId.value == expectedParent
           && descriptor.instanceOrdinal == binding.instanceOrdinal
           && descriptor.consistencyGroupId.value == opaqueBigEndian(binding.consistencyGroupId)
           && descriptor.primitiveType == *type
           && descriptor.valueTypeIdentity == valueTypeIdentity(binding.primitive, binding.bitWidth)
           && descriptor.bitWidth == binding.bitWidth && descriptor.direction == *direction
           && descriptor.access == *access
           && descriptor.processImageBitOffset == qint64(binding.processImageBitOffset)
           && descriptor.processImageBitLength == binding.processImageBitLength
           && descriptor.qualityMask == binding.qualityMask;
}

Utils::Result<QHash<QString, Data::NodeId>> projectDeviceMap(
    const Data::ProjectSnapshot &project,
    const Data::ControllerConnectionScope &scope,
    const VerifiedSemanticBindingArtifact &artifact)
{
    if (!project.valid || project.id.isNull() || project.id != scope.projectId)
        return candidateError(QString::fromLatin1("the project snapshot or scope is invalid"));

    const qsizetype matchingMasters = std::count_if(
        project.nodes.cbegin(),
        project.nodes.cend(),
        [&scope](const Data::ProjectNodeSnapshot &node) {
            return node.kind == Data::ProjectNodeKind::Master && node.id == scope.masterId;
        });
    if (matchingMasters != 1)
        return candidateError(QString::fromLatin1("the controller master is not unique"));

    const Data::SemanticBindingArtifactReference &reference = project.masterBindingArtifact;
    if (reference.projectDeviceBindings.size() != artifact.devices.size())
        return candidateError(QString::fromLatin1("the project device mapping is incomplete"));
    const qsizetype masterSlaveCount = std::count_if(
        project.slaves.cbegin(),
        project.slaves.cend(),
        [&scope](const Data::OfflineSlaveConfiguration &slave) {
            return slave.masterId == scope.masterId;
        });
    if (masterSlaveCount != artifact.devices.size()) {
        return candidateError(
            QString::fromLatin1("the project topology differs from the signed package"));
    }

    QHash<QString, Data::NodeId> result;
    QSet<Data::NodeId> slaveIds;
    for (const Data::SemanticProjectDeviceBinding &mapping : reference.projectDeviceBindings) {
        if (mapping.slaveId.isNull() || mapping.projectDeviceId.isEmpty()
            || mapping.projectDeviceId != mapping.projectDeviceId.trimmed()
            || result.contains(mapping.projectDeviceId) || slaveIds.contains(mapping.slaveId)) {
            return candidateError(QString::fromLatin1("the project device mapping is ambiguous"));
        }

        const VerifiedSemanticDevice *device = artifact.findDevice(mapping.projectDeviceId);
        if (!device)
            return candidateError(QString::fromLatin1("the project maps an unsigned device"));

        const auto slave = std::find_if(
            project.slaves.cbegin(),
            project.slaves.cend(),
            [&mapping, &scope](const Data::OfflineSlaveConfiguration &candidate) {
                return candidate.id == mapping.slaveId && candidate.masterId == scope.masterId;
            });
        if (slave == project.slaves.cend())
            return candidateError(QString::fromLatin1("the mapped slave is outside the master"));

        const auto topology = std::find_if(
            artifact.topologyInstances.cbegin(),
            artifact.topologyInstances.cend(),
            [&device](const SemanticBindingTopologyInstance &instance) {
                return instance.position == device->position
                       && instance.stationAddress == device->stationAddress;
            });
        if (topology == artifact.topologyInstances.cend()
            || topology->adapterId != device->adapterId
            || topology->adapterVersion != device->adapterVersion
            || topology->adapterSha256 != device->adapterSha256
            || topology->esiSha256 != device->esiSha256) {
            return candidateError(
                QString::fromLatin1("the signed device topology is inconsistent"));
        }
        if (slave->position != device->position
            || slave->identity.vendorId != topology->vendorId
            || slave->identity.productCode != topology->productCode
            || (topology->revision
                && slave->identity.revisionNumber != *topology->revision)
            || (topology->serial && slave->serialNumber != *topology->serial)
            || slave->esiSha256 != device->esiSha256) {
            return candidateError(
                QString::fromLatin1(
                    "the explicitly mapped slave differs from the signed device instance"));
        }

        result.insert(mapping.projectDeviceId, mapping.slaveId);
        slaveIds.insert(mapping.slaveId);
    }

    for (const VerifiedSemanticDevice &device : artifact.devices) {
        if (!result.contains(device.projectDeviceId)) {
            return candidateError(
                QString::fromLatin1("a signed project device has no explicit slave mapping"));
        }
    }
    return result;
}

Data::SemanticSignalDefinition signalDefinition(
    const VerifiedSemanticBinding &binding, const Data::RuntimeResourceTypedValue *safeValue)
{
    Data::SemanticSignalDefinition definition;
    definition.id = {binding.semanticSignalDefinitionId};
    // No unsigned display-name vocabulary is invented. The signed definition ID is the only
    // available stable presentation string at this layer.
    definition.displayName = binding.semanticSignalDefinitionId;
    definition.direction = *semanticDirection(binding.direction);
    definition.access = *semanticAccess(binding.access);
    definition.exposure = Data::SemanticSignalExposure::Internal;
    definition.requiredForComplete = false;
    if (binding.unit)
        definition.valueMetadata.unit = *binding.unit;
    if (safeValue) {
        definition.hasSafeValue = true;
        definition.safeValue = safeValue->value;
    }
    definition.manualControl.allowed = false;
    definition.manualControl.requiresExclusiveControl = true;
    definition.manualControl.timeoutAction = Data::ManualControlTimeoutAction::RejectFurtherWrites;
    return definition;
}

bool validControllerId(QStringView controllerId)
{
    if (controllerId.isEmpty() || controllerId != controllerId.trimmed())
        return false;
    return std::none_of(controllerId.cbegin(), controllerId.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

} // namespace

Utils::Result<ReadOnlySemanticBindingCandidates> buildReadOnlySemanticBindingCandidates(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const Data::RuntimeResourceCatalog &catalog,
    const Data::RuntimeSemanticMappingAttestation &attestation)
{
    if (!validControllerId(controllerId))
        return candidateError(QString::fromLatin1("the controller ID is invalid"));
    if (!evidence.isValid())
        return candidateError(QString::fromLatin1("the local package evidence is invalid"));

    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    const Data::RuntimeSemanticMappingProof &proof = evidence.semanticMappingProof();
    if (artifact.formatVersion != 2 || proof.formatVersion != 2
        || proof.trust != Data::RuntimeSemanticMappingTrust::Production) {
        return candidateError(
            QString::fromLatin1("semantic-binding-v2 production evidence is required"));
    }
    if (catalog.scope.projectId.isNull() || catalog.scope.masterId.isNull()
        || !catalog.sessionGeneration || !catalog.receivedAt.isValid()
        || catalog.resources.isEmpty() || catalog.resources.size() != artifact.bindings.size()) {
        return candidateError(QString::fromLatin1("the runtime resource catalog is incomplete"));
    }

    const Utils::Result<> attestationResult = verifyRuntimeSemanticMappingAttestation(
        attestation,
        catalog.scope,
        catalog.sessionGeneration,
        catalog.epoch,
        project.masterBindingArtifact,
        evidence);
    if (!attestationResult)
        return candidateError(attestationResult.error());

    const Utils::Result<QHash<QString, Data::NodeId>> deviceMap
        = projectDeviceMap(project, catalog.scope, artifact);
    if (!deviceMap)
        return candidateError(deviceMap.error());

    QHash<QByteArray, const Data::RuntimeResourceDescriptor *> catalogById;
    catalogById.reserve(catalog.resources.size());
    for (const Data::RuntimeResourceDescriptor &descriptor : catalog.resources) {
        if (!descriptor.id.isValid() || catalogById.contains(descriptor.id.value)) {
            return candidateError(QString::fromLatin1("the runtime resource catalog is ambiguous"));
        }
        catalogById.insert(descriptor.id.value, &descriptor);
    }

    ReadOnlySemanticBindingCandidates result;
    result.mappingDigest = {QStringLiteral("sha256"), proof.mappingSha256};
    result.controllerMappingDigest = {QStringLiteral("sha256"), attestation.proof.mappingSha256};
    result.verification.state = Data::SemanticBindingVerificationState::Verified;
    result.verification.verifierId = QStringLiteral("embed-labs.semantic-runtime.signed-ecpkg-v2");
    result.verification.signedManifestDigest = {QStringLiteral("sha256"), proof.manifestSha256};
    result.verification.verifiedAt = attestation.receivedAt;
    result.verification.detail = QString::fromLatin1(
        "Signed semantic-binding-v2 and controller proof verified.");
    result.bindings.reserve(artifact.bindings.size());
    result.signalStates.reserve(artifact.bindings.size());

    QList<Data::SemanticRuntimeTarget> targets;
    for (const VerifiedSemanticBinding &signedBinding : artifact.bindings) {
        const auto mappedDevice = deviceMap->constFind(signedBinding.projectDeviceId);
        if (mappedDevice == deviceMap->cend()) {
            return candidateError(
                QString::fromLatin1("a semantic binding has no explicit project device"));
        }

        const QByteArray resourceId = opaqueBigEndian(signedBinding.resourceId);
        const auto descriptor = catalogById.constFind(resourceId);
        if (descriptor == catalogById.cend()
            || !descriptorMatchesSignedBinding(**descriptor, signedBinding)) {
            return candidateError(
                QString::fromLatin1("runtime descriptor differs from signed binding %1")
                    .arg(signedBinding.semanticBindingId));
        }

        const auto type = primitiveType(signedBinding.primitive);
        const auto direction = runtimeDirection(signedBinding.direction);
        const auto access = runtimeAccess(signedBinding.access);
        const auto signalDirectionValue = semanticDirection(signedBinding.direction);
        const auto signalAccessValue = semanticAccess(signedBinding.access);
        if (!type || !direction || !access || !signalDirectionValue || !signalAccessValue) {
            return candidateError(
                QString::fromLatin1("a signed binding uses an unsupported signal type"));
        }

        Data::SemanticRuntimeTarget target;
        target.controllerId = controllerId.toString();
        target.scope = catalog.scope;
        target.deviceId = *mappedDevice;
        target.kind = Data::SemanticRuntimeTargetKind::Signal;
        target.signalId = {signedBinding.semanticSignalDefinitionId};
        if (targets.contains(target)) {
            return candidateError(QString::fromLatin1("a semantic signal target is ambiguous"));
        }
        targets.append(target);

        Data::SemanticRuntimeBinding binding;
        binding.target = target;
        binding.semanticBindingId = signedBinding.semanticBindingId;
        binding.componentBindingId = signedBinding.componentBindingId;
        binding.adapterId = {signedBinding.adapterId};
        binding.adapterVersion = signedBinding.adapterVersion;
        binding.adapterContentSha256 = signedBinding.adapterSha256;
        binding.esiSha256 = signedBinding.esiSha256;
        binding.bindingArtifactSha256 = artifact.artifactSha256;
        binding.sessionGeneration = catalog.sessionGeneration;
        binding.epoch = catalog.epoch;
        binding.mappingDigest = result.mappingDigest;
        binding.controllerMappingDigest = result.controllerMappingDigest;
        binding.verification = result.verification;
        binding.resourceId = {resourceId};
        binding.componentInstanceId = {opaqueBigEndian(signedBinding.componentInstanceId)};
        binding.consistencyGroupId = {opaqueBigEndian(signedBinding.consistencyGroupId)};
        binding.primitiveType = *type;
        binding.valueTypeIdentity
            = valueTypeIdentity(signedBinding.primitive, signedBinding.bitWidth);
        binding.bitWidth = signedBinding.bitWidth;
        binding.direction = *direction;
        binding.access = *access;

        const auto safeValue = signedSafeValue(signedBinding);
        Data::SemanticSignalRuntimeState state;
        state.target = target;
        state.definition = signalDefinition(signedBinding, safeValue ? &*safeValue : nullptr);
        state.availability = Data::SemanticSignalAvailability::Unverified;
        state.binding = binding;
        state.snapshotComplete = false;
        state.detail = signedBinding.access == EcfgResourceAccess::ReadWrite
                           ? QString::fromLatin1(
                                 "Manual write is disabled pending writable action evidence.")
                           : QString::fromLatin1("Runtime sample has not been captured.");

        result.bindings.append(std::move(binding));
        result.signalStates.append(std::move(state));
    }
    return result;
}

} // namespace EtherCAT::SemanticRuntime::Internal
