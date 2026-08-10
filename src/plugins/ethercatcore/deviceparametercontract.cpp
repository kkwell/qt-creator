// Copyright (C) 2026 Kvell

#include "deviceparametercontract.h"

#include "manualcontrolcontract.h"

#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <limits>

namespace EtherCAT::Core {
namespace {

bool canonicalIdentifier(const QString &value)
{
    if (value.isEmpty() || value.size() > Data::maximumDeviceParameterIdentifierLength)
        return false;
    const auto asciiLetterOrDigit = [](QChar character) {
        const ushort value = character.unicode();
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')
               || (value >= '0' && value <= '9');
    };
    if (!asciiLetterOrDigit(value.front()))
        return false;
    return std::all_of(value.cbegin() + 1, value.cend(), [&](QChar character) {
        return asciiLetterOrDigit(character) || character == '.' || character == '_'
               || character == '-';
    });
}

DeviceParameterContractValidation rejection(
    DeviceParameterContractError error, const QString &detail)
{
    return {error, detail};
}

ConfiguredDeviceParameterValidation configuredRejection(
    ConfiguredDeviceParameterError error, const QString &parameterId, const QString &detail)
{
    return {error, parameterId, detail};
}

std::optional<Data::EngineeringValueKind> parameterObjectValueKind(
    Data::EtherCATDataType physicalType)
{
    switch (physicalType) {
    case Data::EtherCATDataType::Boolean:
        return Data::EngineeringValueKind::Boolean;
    case Data::EtherCATDataType::Integer8:
    case Data::EtherCATDataType::Integer16:
    case Data::EtherCATDataType::Integer32:
    case Data::EtherCATDataType::Integer64:
        return Data::EngineeringValueKind::SignedInteger;
    case Data::EtherCATDataType::UnsignedInteger8:
    case Data::EtherCATDataType::UnsignedInteger16:
    case Data::EtherCATDataType::UnsignedInteger32:
    case Data::EtherCATDataType::UnsignedInteger64:
        return Data::EngineeringValueKind::UnsignedInteger;
    case Data::EtherCATDataType::Unknown:
    case Data::EtherCATDataType::Real32:
    case Data::EtherCATDataType::Real64:
    case Data::EtherCATDataType::VisibleString:
    case Data::EtherCATDataType::OctetString:
        return std::nullopt;
    }
    return std::nullopt;
}

bool validParameterObjectBinding(
    const Data::DeviceParameterDefinition &definition,
    const Data::DeviceParameterObjectBinding &binding)
{
    const std::optional<Data::EngineeringValueKind> rawKind = parameterObjectValueKind(
        binding.physicalType);
    return binding.index != 0 && rawKind && *rawKind == definition.valueKind
           && binding.byteOrder == Data::DeviceByteOrder::LittleEndian
           && binding.engineeringTransform.rounding == Data::EngineeringRounding::RejectInexact
           && binding.engineeringTransform.unit == definition.unit
           && binding.engineeringTransform.constraint == definition.engineeringConstraint
           && validateEngineeringTransform(binding.engineeringTransform).accepted();
}

bool validParameterDefinition(const Data::DeviceParameterDefinition &definition)
{
    if (!canonicalIdentifier(definition.id)
        || definition.valueKind == Data::EngineeringValueKind::Invalid
        || definition.definitionSha256.size() != 32
        || std::all_of(
            definition.definitionSha256.cbegin(),
            definition.definitionSha256.cend(),
            [](char byte) { return byte == 0; })
        || !validateEngineeringConstraint(definition.engineeringConstraint).accepted()) {
        return false;
    }
    if (definition.engineeringDefaultValue
        && (definition.engineeringDefaultValue->kind != definition.valueKind
            || !validateEngineeringValueAgainstConstraint(
                    *definition.engineeringDefaultValue, definition.engineeringConstraint)
                    .accepted())) {
        return false;
    }
    const Data::DeviceParameterConfiguredProjection &projection = definition.configuredProjection;
    if (projection.kind == Data::DeviceParameterProjectionKind::ProjectOnly) {
        if (projection.reason.isEmpty() || !projection.transition.isEmpty() || projection.object)
            return false;
    } else if (projection.kind == Data::DeviceParameterProjectionKind::CoeStartupSdo) {
        if (!projection.reason.isEmpty() || projection.transition != QStringLiteral("PS")
            || !projection.object) {
            return false;
        }
    } else {
        return false;
    }
    if (projection.object && !validParameterObjectBinding(definition, *projection.object))
        return false;
    const Data::DeviceParameterObservedSource &observed = definition.observedSource;
    if (observed.kind == Data::DeviceParameterObservedSourceKind::Unavailable) {
        if (observed.reason.isEmpty() || observed.object)
            return false;
    } else if (observed.kind == Data::DeviceParameterObservedSourceKind::CoeSdoUpload) {
        if (!observed.reason.isEmpty() || !observed.object)
            return false;
    } else {
        return false;
    }
    if (observed.object && !validParameterObjectBinding(definition, *observed.object))
        return false;
    return !projection.object || !observed.object || *projection.object == *observed.object;
}

std::optional<quint32> physicalBitWidth(Data::EtherCATDataType physicalType)
{
    switch (physicalType) {
    case Data::EtherCATDataType::Boolean:
        return 1;
    case Data::EtherCATDataType::Integer8:
    case Data::EtherCATDataType::UnsignedInteger8:
        return 8;
    case Data::EtherCATDataType::Integer16:
    case Data::EtherCATDataType::UnsignedInteger16:
        return 16;
    case Data::EtherCATDataType::Integer32:
    case Data::EtherCATDataType::UnsignedInteger32:
        return 32;
    case Data::EtherCATDataType::Integer64:
    case Data::EtherCATDataType::UnsignedInteger64:
        return 64;
    case Data::EtherCATDataType::Unknown:
    case Data::EtherCATDataType::Real32:
    case Data::EtherCATDataType::Real64:
    case Data::EtherCATDataType::VisibleString:
    case Data::EtherCATDataType::OctetString:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Data::EngineeringValue> decodeRawParameterValue(
    Data::EtherCATDataType physicalType, const QByteArray &rawValue)
{
    switch (physicalType) {
    case Data::EtherCATDataType::Boolean:
        if (rawValue.size() == 1 && (rawValue.at(0) == 0 || rawValue.at(0) == 1))
            return Data::EngineeringValue::fromBoolean(rawValue.at(0) != 0);
        break;
    case Data::EtherCATDataType::Integer8:
        if (rawValue.size() == 1)
            return Data::EngineeringValue::fromSignedInteger(qint8(rawValue.at(0)));
        break;
    case Data::EtherCATDataType::UnsignedInteger8:
        if (rawValue.size() == 1)
            return Data::EngineeringValue::fromUnsignedInteger(quint8(rawValue.at(0)));
        break;
    case Data::EtherCATDataType::Integer16:
        if (rawValue.size() == 2) {
            return Data::EngineeringValue::fromSignedInteger(
                qint16(qFromLittleEndian<quint16>(rawValue.constData())));
        }
        break;
    case Data::EtherCATDataType::UnsignedInteger16:
        if (rawValue.size() == 2) {
            return Data::EngineeringValue::fromUnsignedInteger(
                qFromLittleEndian<quint16>(rawValue.constData()));
        }
        break;
    case Data::EtherCATDataType::Integer32:
        if (rawValue.size() == 4) {
            return Data::EngineeringValue::fromSignedInteger(
                qint32(qFromLittleEndian<quint32>(rawValue.constData())));
        }
        break;
    case Data::EtherCATDataType::UnsignedInteger32:
        if (rawValue.size() == 4) {
            return Data::EngineeringValue::fromUnsignedInteger(
                qFromLittleEndian<quint32>(rawValue.constData()));
        }
        break;
    case Data::EtherCATDataType::Integer64:
        if (rawValue.size() == 8) {
            return Data::EngineeringValue::fromSignedInteger(
                qint64(qFromLittleEndian<quint64>(rawValue.constData())));
        }
        break;
    case Data::EtherCATDataType::UnsignedInteger64:
        if (rawValue.size() == 8) {
            return Data::EngineeringValue::fromUnsignedInteger(
                qFromLittleEndian<quint64>(rawValue.constData()));
        }
        break;
    case Data::EtherCATDataType::Unknown:
    case Data::EtherCATDataType::Real32:
    case Data::EtherCATDataType::Real64:
    case Data::EtherCATDataType::VisibleString:
    case Data::EtherCATDataType::OctetString:
        break;
    }
    return std::nullopt;
}

std::optional<Data::EngineeringValue> normalizeObservedValue(
    const Data::EngineeringValue &value, Data::EngineeringValueKind kind)
{
    if (value.kind == kind)
        return value;
    if (value.kind != Data::EngineeringValueKind::ExactRational)
        return std::nullopt;
    if (kind == Data::EngineeringValueKind::SignedInteger && value.rational.denominator == 1)
        return Data::EngineeringValue::fromSignedInteger(value.rational.numerator);
    if (kind == Data::EngineeringValueKind::UnsignedInteger && value.rational.denominator == 1
        && value.rational.numerator >= 0) {
        return Data::EngineeringValue::fromUnsignedInteger(quint64(value.rational.numerator));
    }
    return std::nullopt;
}

DeviceParameterObservationResult unavailableObservation(const QString &detail)
{
    return {DeviceParameterObservationState::Unavailable, std::nullopt, detail};
}

} // namespace

DeviceParameterContractValidation validateDeviceParameterConfiguration(
    const Data::DeviceParameterConfiguration &configuration)
{
    if (configuration.values.size() > Data::maximumDeviceParametersPerConfiguration) {
        return rejection(
            DeviceParameterContractError::TooManyParameters,
            QStringLiteral("A device parameter configuration exceeds the supported limit."));
    }
    QSet<QString> identifiers;
    QString previousIdentifier;
    for (const Data::DeviceParameterValue &parameter : configuration.values) {
        if (!canonicalIdentifier(parameter.parameterId)) {
            return rejection(
                DeviceParameterContractError::InvalidIdentifier,
                QStringLiteral("A device parameter identifier is not canonical."));
        }
        if (identifiers.contains(parameter.parameterId)) {
            return rejection(
                DeviceParameterContractError::DuplicateIdentifier,
                QStringLiteral("Device parameter identifiers must be unique."));
        }
        if (!previousIdentifier.isEmpty() && previousIdentifier >= parameter.parameterId) {
            return rejection(
                DeviceParameterContractError::NonCanonicalIdentifierOrder,
                QStringLiteral("Device parameters must use canonical identifier order."));
        }
        if (!validateEngineeringValue(parameter.value).accepted()
            || (parameter.value.kind == Data::EngineeringValueKind::Enumeration
                && !canonicalIdentifier(parameter.value.enumerationName))) {
            return rejection(
                DeviceParameterContractError::InvalidValue,
                QStringLiteral("A device parameter engineering value is not canonical."));
        }
        identifiers.insert(parameter.parameterId);
        previousIdentifier = parameter.parameterId;
    }
    return {};
}

ConfiguredDeviceParameterValidation validateConfiguredDeviceParameters(
    const Data::DeviceAdapterManifest &manifest,
    const QByteArray &expectedEsiSha256,
    const Data::DeviceAdapterProjectSelection &expectedAdapterSelection,
    const Data::DeviceParameterConfiguration &configuration)
{
    const DeviceParameterContractValidation structural = validateDeviceParameterConfiguration(
        configuration);
    if (!structural.accepted()) {
        return configuredRejection(
            ConfiguredDeviceParameterError::InvalidConfiguration, {}, structural.detail);
    }
    if (manifest.contractVersion != Data::DeviceAdapterContractVersion::V4) {
        return configuredRejection(
            ConfiguredDeviceParameterError::UnsupportedAdapterContract,
            {},
            QStringLiteral("Configured device parameters require an Adapter v4 manifest."));
    }
    const bool profileExists = std::any_of(
        manifest.processDataProfiles.cbegin(),
        manifest.processDataProfiles.cend(),
        [&expectedAdapterSelection](const Data::ProcessDataProfile &profile) {
            return profile.id == expectedAdapterSelection.processDataProfileId;
        });
    if (expectedEsiSha256.size() != 32 || expectedEsiSha256 != manifest.match.exactEsiSha256
        || expectedAdapterSelection.adapterId != manifest.id
        || expectedAdapterSelection.adapterVersion != manifest.version
        || expectedAdapterSelection.adapterContentSha256 != manifest.contentSha256
        || expectedAdapterSelection.processDataProfileId.isEmpty() || !profileExists) {
        return configuredRejection(
            ConfiguredDeviceParameterError::AdapterIdentityMismatch,
            {},
            QStringLiteral(
                "The ESI or complete Adapter selection does not match the signed definition."));
    }
    if (manifest.qualification != Data::DeviceAdapterQualification::Qualified
        || !manifest.signatureVerified || !manifest.realHardwareAllowed) {
        return configuredRejection(
            ConfiguredDeviceParameterError::AdapterNotAuthorized,
            {},
            QStringLiteral("The Adapter parameter definitions are not independently authorized."));
    }
    if (manifest.parameterDefinitions.size() > Data::maximumDeviceParameterDefinitionsPerAdapter) {
        return configuredRejection(
            ConfiguredDeviceParameterError::InvalidAdapterDefinition,
            {},
            QStringLiteral("The Adapter contains too many parameter definitions."));
    }

    QHash<QString, const Data::DeviceParameterDefinition *> definitions;
    QString previousIdentifier;
    for (const Data::DeviceParameterDefinition &definition : manifest.parameterDefinitions) {
        if (!validParameterDefinition(definition)
            || (!previousIdentifier.isEmpty() && definition.id <= previousIdentifier)
            || definitions.contains(definition.id)) {
            return configuredRejection(
                ConfiguredDeviceParameterError::InvalidAdapterDefinition,
                definition.id,
                QStringLiteral(
                    "The signed Adapter parameter definition closure is not canonical."));
        }
        definitions.insert(definition.id, &definition);
        previousIdentifier = definition.id;
    }

    QSet<QString> configuredIds;
    for (const Data::DeviceParameterValue &parameter : configuration.values) {
        const Data::DeviceParameterDefinition *definition
            = definitions.value(parameter.parameterId, nullptr);
        if (!definition) {
            return configuredRejection(
                ConfiguredDeviceParameterError::UnknownParameter,
                parameter.parameterId,
                QStringLiteral("The configured parameter is absent from the signed Adapter."));
        }
        configuredIds.insert(parameter.parameterId);
        if (parameter.value.kind != definition->valueKind) {
            return configuredRejection(
                ConfiguredDeviceParameterError::ValueKindMismatch,
                parameter.parameterId,
                QStringLiteral(
                    "The configured parameter kind differs from its signed definition."));
        }
        const EngineeringContractValidation constrained = validateEngineeringValueAgainstConstraint(
            parameter.value, definition->engineeringConstraint);
        if (!constrained.accepted()) {
            return configuredRejection(
                ConfiguredDeviceParameterError::ValueOutsideConstraint,
                parameter.parameterId,
                constrained.detail);
        }
    }
    for (const Data::DeviceParameterDefinition &definition : manifest.parameterDefinitions) {
        if (definition.required && !configuredIds.contains(definition.id)) {
            return configuredRejection(
                ConfiguredDeviceParameterError::MissingRequiredParameter,
                definition.id,
                QStringLiteral("A required signed Adapter parameter is not configured."));
        }
    }
    return {};
}

DeviceParameterObservationResult evaluateDeviceParameterObservation(
    const Data::DeviceParameterDefinition &definition,
    const std::optional<Data::EngineeringValue> &configuredValue,
    const Data::AxisParameterEvidenceRecord &record)
{
    if (!validParameterDefinition(definition)
        || definition.observedSource.kind != Data::DeviceParameterObservedSourceKind::CoeSdoUpload
        || !definition.observedSource.object) {
        return unavailableObservation(
            QStringLiteral("The signed parameter definition has no observable CoE object."));
    }
    const Data::DeviceParameterObjectBinding &binding = *definition.observedSource.object;
    const std::optional<Data::EngineeringValueKind> rawKind = parameterObjectValueKind(
        binding.physicalType);
    if (!rawKind || *rawKind != definition.valueKind
        || binding.byteOrder != Data::DeviceByteOrder::LittleEndian
        || binding.engineeringTransform.unit != definition.unit
        || binding.engineeringTransform.constraint != definition.engineeringConstraint
        || binding.engineeringTransform.rounding != Data::EngineeringRounding::RejectInexact
        || !validateEngineeringTransform(binding.engineeringTransform).accepted()
        || !validateEngineeringConstraint(definition.engineeringConstraint).accepted()) {
        return unavailableObservation(
            QStringLiteral("The signed observed-value binding is invalid."));
    }

    const auto profileRecord = std::find_if(
        Data::FixedAxisParameterEvidenceRecords.cbegin(),
        Data::FixedAxisParameterEvidenceRecords.cend(),
        [&binding](const Data::FixedAxisParameterEvidenceRecord &candidate) {
            return candidate.index == binding.index && candidate.subIndex == binding.subIndex;
        });
    if (profileRecord == Data::FixedAxisParameterEvidenceRecords.cend()) {
        return unavailableObservation(
            QStringLiteral("The signed observed object is outside the fixed evidence profile."));
    }
    const qsizetype ordinal = profileRecord - Data::FixedAxisParameterEvidenceRecords.cbegin();
    const std::optional<quint32> bitWidth = physicalBitWidth(binding.physicalType);
    if (!bitWidth || *bitWidth != quint32(profileRecord->valueBytes) * 8
        || record.ordinal != ordinal || record.index != binding.index
        || record.subIndex != binding.subIndex || !record.isValid()) {
        return unavailableObservation(
            QStringLiteral("The observed record does not match the signed fixed-width binding."));
    }
    if (record.state != Data::AxisParameterEvidenceRecordState::Valid) {
        return unavailableObservation(
            QStringLiteral("The controller could not read the observed CoE object."));
    }

    const std::optional<Data::EngineeringValue> raw
        = decodeRawParameterValue(binding.physicalType, record.rawValue);
    if (!raw) {
        return unavailableObservation(
            QStringLiteral("The observed little-endian value could not be decoded."));
    }
    const EngineeringConversionResult converted
        = convertRawToEngineering(*raw, *bitWidth, binding.engineeringTransform);
    if (!converted.validation.accepted() || !converted.value) {
        return unavailableObservation(
            QStringLiteral("The observed value violates the signed engineering transform."));
    }
    const std::optional<Data::EngineeringValue> normalized
        = normalizeObservedValue(*converted.value, definition.valueKind);
    if (!normalized
        || !validateEngineeringValueAgainstConstraint(
                *normalized, definition.engineeringConstraint)
                .accepted()) {
        return unavailableObservation(
            QStringLiteral("The observed value cannot be represented by the signed definition."));
    }
    if (configuredValue
        && (configuredValue->kind != definition.valueKind
            || !validateEngineeringValueAgainstConstraint(
                    *configuredValue, definition.engineeringConstraint)
                    .accepted())) {
        return unavailableObservation(
            QStringLiteral("The Project configured value does not match the signed definition."));
    }

    if (!configuredValue) {
        return {
            DeviceParameterObservationState::NotConfigured,
            normalized,
            QStringLiteral("No Project-owned configured value is available for comparison."),
        };
    }
    if (*configuredValue == *normalized) {
        return {
            DeviceParameterObservationState::Match,
            normalized,
            QStringLiteral("The observed value matches the Project-owned configured value."),
        };
    }
    return {
        DeviceParameterObservationState::Mismatch,
        normalized,
        QStringLiteral("The observed value differs from the Project-owned configured value."),
    };
}

} // namespace EtherCAT::Core
