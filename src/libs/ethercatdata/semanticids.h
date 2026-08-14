// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatdata_global.h"

#include <QMetaType>
#include <QString>

namespace EtherCAT::Data {

// These identifiers deliberately retain their complete, namespaced text. The data layer does not
// maintain a closed vocabulary for adapters, capabilities, semantic signals, or semantic actions.
struct ETHERCATDATA_EXPORT DeviceAdapterId
{
    QString value;

    friend bool operator==(const DeviceAdapterId &, const DeviceAdapterId &) = default;
};

struct ETHERCATDATA_EXPORT DeviceCapabilityId
{
    QString value;

    friend bool operator==(const DeviceCapabilityId &, const DeviceCapabilityId &) = default;
};

struct ETHERCATDATA_EXPORT SemanticSignalId
{
    QString value;

    friend bool operator==(const SemanticSignalId &, const SemanticSignalId &) = default;
};

struct ETHERCATDATA_EXPORT SemanticActionId
{
    QString value;

    friend bool operator==(const SemanticActionId &, const SemanticActionId &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterId)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceCapabilityId)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalId)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticActionId)
