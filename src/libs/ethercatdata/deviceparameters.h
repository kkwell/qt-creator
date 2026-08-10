// Copyright (C) 2026 Kvell

#pragma once

#include "engineeringvalue.h"
#include "ethercatdata_global.h"

#include <QList>
#include <QMetaType>
#include <QString>

namespace EtherCAT::Data {

// These limits keep one project mutation, its undo command, and its serialized form bounded.
// Device parameter identifiers intentionally use the same 256-character ASCII grammar as signed
// v3 adapter parameter identifiers.
inline constexpr qsizetype maximumDeviceParametersPerConfiguration = 256;
inline constexpr qsizetype maximumDeviceParametersPerProject = 4096;
inline constexpr qsizetype maximumDeviceParameterIdentifierLength = 256;

// Project-owned configured values. The containing slave binds them to one exact ESI and adapter
// selection; a signed adapter definition must still qualify the identifiers, kinds, and ranges
// before a compiler or runtime may consume them.
// The list is canonical only when parameterId values are unique and strictly ascending.
struct ETHERCATDATA_EXPORT DeviceParameterValue
{
    QString parameterId;
    EngineeringValue value;

    friend bool operator==(const DeviceParameterValue &, const DeviceParameterValue &) = default;
};

struct ETHERCATDATA_EXPORT DeviceParameterConfiguration
{
    QList<DeviceParameterValue> values;

    friend bool operator==(
        const DeviceParameterConfiguration &, const DeviceParameterConfiguration &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::DeviceParameterValue)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceParameterConfiguration)
