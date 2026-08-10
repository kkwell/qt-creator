// Copyright (C) 2026 Kvell

#include "deviceparametercontract.h"

#include "manualcontrolcontract.h"

#include <QSet>

#include <algorithm>

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

} // namespace EtherCAT::Core
