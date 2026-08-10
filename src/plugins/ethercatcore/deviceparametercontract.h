// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/deviceparameters.h>

#include <QMetaType>
#include <QString>

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

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::DeviceParameterContractError)
Q_DECLARE_METATYPE(EtherCAT::Core::DeviceParameterContractValidation)
