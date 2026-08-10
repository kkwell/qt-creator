// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/axisparameterevidence.h>
#include <ethercatdata/deviceadapterselection.h>
#include <ethercatdata/deviceparameters.h>

#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Core {

enum class DeviceParameterContractError {
    None,
    TooManyParameters,
    InvalidIdentifier,
    DuplicateIdentifier,
    NonCanonicalIdentifierOrder,
    InvalidValue,
};

struct ETHERCATCORE_EXPORT DeviceParameterContractValidation
{
    DeviceParameterContractError error = DeviceParameterContractError::None;
    QString detail;

    bool accepted() const { return error == DeviceParameterContractError::None; }

    friend bool operator==(
        const DeviceParameterContractValidation &, const DeviceParameterContractValidation &)
        = default;
};

ETHERCATCORE_EXPORT DeviceParameterContractValidation validateDeviceParameterConfiguration(
    const Data::DeviceParameterConfiguration &configuration);

enum class ConfiguredDeviceParameterError {
    None,
    InvalidConfiguration,
    UnsupportedAdapterContract,
    AdapterIdentityMismatch,
    AdapterNotAuthorized,
    InvalidAdapterDefinition,
    UnknownParameter,
    MissingRequiredParameter,
    ValueKindMismatch,
    ValueOutsideConstraint,
};

struct ETHERCATCORE_EXPORT ConfiguredDeviceParameterValidation
{
    ConfiguredDeviceParameterError error = ConfiguredDeviceParameterError::None;
    QString parameterId;
    QString detail;

    bool accepted() const { return error == ConfiguredDeviceParameterError::None; }

    friend bool operator==(
        const ConfiguredDeviceParameterValidation &, const ConfiguredDeviceParameterValidation &)
        = default;
};

ETHERCATCORE_EXPORT ConfiguredDeviceParameterValidation validateConfiguredDeviceParameters(
    const Data::DeviceAdapterManifest &manifest,
    const QByteArray &expectedEsiSha256,
    const Data::DeviceAdapterProjectSelection &expectedAdapterSelection,
    const Data::DeviceParameterConfiguration &configuration);

enum class DeviceParameterObservationState {
    Match,
    Mismatch,
    NotConfigured,
    Unavailable,
};

struct ETHERCATCORE_EXPORT DeviceParameterObservationResult
{
    DeviceParameterObservationState state = DeviceParameterObservationState::Unavailable;
    std::optional<Data::EngineeringValue> observedValue;
    QString detail;

    bool isAvailable() const
    {
        return state != DeviceParameterObservationState::Unavailable && observedValue.has_value();
    }

    friend bool operator==(
        const DeviceParameterObservationResult &, const DeviceParameterObservationResult &)
        = default;
};

ETHERCATCORE_EXPORT DeviceParameterObservationResult evaluateDeviceParameterObservation(
    const Data::DeviceParameterDefinition &definition,
    const std::optional<Data::EngineeringValue> &configuredValue,
    const Data::AxisParameterEvidenceRecord &record);

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::DeviceParameterContractError)
Q_DECLARE_METATYPE(EtherCAT::Core::DeviceParameterContractValidation)
Q_DECLARE_METATYPE(EtherCAT::Core::ConfiguredDeviceParameterError)
Q_DECLARE_METATYPE(EtherCAT::Core::ConfiguredDeviceParameterValidation)
Q_DECLARE_METATYPE(EtherCAT::Core::DeviceParameterObservationState)
Q_DECLARE_METATYPE(EtherCAT::Core::DeviceParameterObservationResult)
