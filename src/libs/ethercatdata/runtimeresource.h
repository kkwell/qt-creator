// Copyright (C) 2026 Kvell

#pragma once

#include "controllerconnection.h"
#include "ethercatdata_global.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariant>

#include <optional>

namespace EtherCAT::Data {

// Runtime identifiers are provider-owned opaque byte strings. Consumers compare and retain them
// but must not infer transport addresses, process-image offsets, or device semantics from their
// content.
struct ETHERCATDATA_EXPORT RuntimeResourceId
{
    QByteArray value;

    bool isValid() const { return !value.isEmpty(); }

    friend bool operator==(const RuntimeResourceId &, const RuntimeResourceId &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeComponentInstanceId
{
    QByteArray value;

    bool isValid() const { return !value.isEmpty(); }

    friend bool operator==(const RuntimeComponentInstanceId &, const RuntimeComponentInstanceId &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeConsistencyGroupId
{
    QByteArray value;

    bool isValid() const { return !value.isEmpty(); }

    friend bool operator==(const RuntimeConsistencyGroupId &, const RuntimeConsistencyGroupId &)
        = default;
};

inline size_t qHash(const RuntimeResourceId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

inline size_t qHash(const RuntimeComponentInstanceId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

inline size_t qHash(const RuntimeConsistencyGroupId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

enum class RuntimeResourcePrimitiveType {
    Opaque,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Text,
    ByteArray,
};

enum class RuntimeResourceDirection {
    Unknown,
    Input,
    Output,
    Bidirectional,
};

enum class RuntimeResourceAccess {
    Unknown,
    ReadOnly,
    WriteOnly,
    ReadWrite,
};

enum class RuntimeResourceQualityState {
    Unknown,
    Good,
    Uncertain,
    Bad,
    Stale,
    Unavailable,
};

struct ETHERCATDATA_EXPORT RuntimeResourceTypedValue
{
    RuntimeResourcePrimitiveType primitiveType = RuntimeResourcePrimitiveType::Opaque;
    QVariant value;
    QByteArray typeIdentity;
    // Retains values whose type is unknown to the consumer. Core treats this representation as
    // provider-owned data and never decodes it.
    QByteArray opaqueRepresentation;

    friend bool operator==(const RuntimeResourceTypedValue &, const RuntimeResourceTypedValue &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceCatalogEpoch
{
    quint64 controllerBootId = 0;
    ControllerSlot activePackageSlot = ControllerSlot::None;
    quint64 activePackageGeneration = 0;
    quint64 configurationId = 0;
    quint64 topologyGeneration = 0;
    quint64 runtimeGeneration = 0;
    quint64 catalogRevision = 0;
    QByteArray topologyIdentity;

    friend bool operator==(const RuntimeResourceCatalogEpoch &, const RuntimeResourceCatalogEpoch &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceDescriptor
{
    RuntimeResourceId id;
    RuntimeComponentInstanceId componentInstanceId;
    RuntimeComponentInstanceId parentInstanceId;
    quint32 instanceOrdinal = 0;
    RuntimeConsistencyGroupId consistencyGroupId;
    QString displayName;
    QString description;
    RuntimeResourcePrimitiveType primitiveType = RuntimeResourcePrimitiveType::Opaque;
    QByteArray valueTypeIdentity;
    quint32 bitWidth = 0;
    RuntimeResourceDirection direction = RuntimeResourceDirection::Unknown;
    RuntimeResourceAccess access = RuntimeResourceAccess::Unknown;
    // These fields are internal binding coordinates. UI and automation consumers operate only on
    // the opaque resource ID and must not treat these offsets as a control API.
    qint64 processImageBitOffset = -1;
    quint32 processImageBitLength = 0;
    quint64 qualityMask = 0;
    QString unit;
    std::optional<RuntimeResourceTypedValue> safeValue;

    friend bool operator==(const RuntimeResourceDescriptor &, const RuntimeResourceDescriptor &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceQuality
{
    RuntimeResourceQualityState state = RuntimeResourceQualityState::Unknown;
    quint64 flags = 0;
    QByteArray opaqueCode;
    QString detail;

    friend bool operator==(const RuntimeResourceQuality &, const RuntimeResourceQuality &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceSample
{
    RuntimeResourceId resourceId;
    RuntimeConsistencyGroupId consistencyGroupId;
    RuntimeResourceTypedValue value;
    RuntimeResourceQuality quality;
    quint64 valueSequence = 0;
    quint64 controllerTimestampNs = 0;

    friend bool operator==(const RuntimeResourceSample &, const RuntimeResourceSample &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceCatalog
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    QDateTime receivedAt;
    QList<RuntimeResourceDescriptor> resources;

    friend bool operator==(const RuntimeResourceCatalog &, const RuntimeResourceCatalog &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceSnapshot
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    quint64 snapshotSequence = 0;
    quint64 captureCycle = 0;
    quint64 controllerTimestampNs = 0;
    QDateTime receivedAt;
    bool complete = false;
    QList<RuntimeResourceSample> samples;

    friend bool operator==(const RuntimeResourceSnapshot &, const RuntimeResourceSnapshot &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceId)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeComponentInstanceId)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeConsistencyGroupId)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourcePrimitiveType)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceDirection)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceAccess)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceQualityState)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceTypedValue)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceCatalogEpoch)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceDescriptor)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceQuality)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceSample)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceCatalog)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceSnapshot)
