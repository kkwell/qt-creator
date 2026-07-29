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

// Requests an exact, provider-neutral subset from one immutable runtime capture. Callers must use
// IDs from the catalog identified by expectedEpoch; they must never infer IDs from names or
// process-image coordinates.
struct ETHERCATDATA_EXPORT RuntimeResourceSnapshotRequest
{
    QString correlationId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch expectedEpoch;
    QList<RuntimeResourceId> resourceIds;

    bool isValid() const
    {
        if (correlationId.isEmpty() || correlationId.size() > 128
            || correlationId != correlationId.trimmed() || scope.projectId.isNull()
            || scope.masterId.isNull()
            || !sessionGeneration || !expectedEpoch.controllerBootId
            || (expectedEpoch.activePackageSlot != ControllerSlot::A
                && expectedEpoch.activePackageSlot != ControllerSlot::B)
            || !expectedEpoch.activePackageGeneration || !expectedEpoch.configurationId
            || !expectedEpoch.topologyGeneration || !expectedEpoch.runtimeGeneration
            || !expectedEpoch.catalogRevision || expectedEpoch.topologyIdentity.isEmpty()
            || resourceIds.isEmpty() || resourceIds.size() > 64) {
            return false;
        }
        for (const QChar character : correlationId) {
            if (character.category() == QChar::Other_Control)
                return false;
        }

        QByteArray previous;
        for (const RuntimeResourceId &id : resourceIds) {
            if (!id.isValid() || (!previous.isEmpty() && id.value <= previous))
                return false;
            previous = id.value;
        }
        return true;
    }

    friend bool operator==(
        const RuntimeResourceSnapshotRequest &, const RuntimeResourceSnapshotRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimeResourceSnapshotResult
{
    RuntimeResourceSnapshotRequest request;
    std::optional<RuntimeResourceSnapshot> snapshot;
    std::optional<ControllerOperationError> error;

    bool isValid() const
    {
        if (!request.isValid() || snapshot.has_value() == error.has_value())
            return false;
        if (error) {
            return error->operation
                   == ControllerOperation::QueryRuntimeResourceSnapshot;
        }
        if (!snapshot->complete || snapshot->scope != request.scope
            || snapshot->sessionGeneration != request.sessionGeneration
            || snapshot->epoch != request.expectedEpoch
            || snapshot->samples.size() != request.resourceIds.size()) {
            return false;
        }
        for (qsizetype index = 0; index < request.resourceIds.size(); ++index) {
            if (snapshot->samples.at(index).resourceId
                != request.resourceIds.at(index)) {
                return false;
            }
        }
        return true;
    }

    friend bool operator==(
        const RuntimeResourceSnapshotResult &, const RuntimeResourceSnapshotResult &)
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
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceSnapshotRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeResourceSnapshotResult)
