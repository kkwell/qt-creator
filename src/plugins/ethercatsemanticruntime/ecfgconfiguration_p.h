// Copyright (C) 2026 Embed Labs

#pragma once

#include <utils/result.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

namespace EtherCAT::SemanticRuntime::Internal {

constexpr qsizetype defaultMaximumEcfgConfigurationBytes = 4 * 1024 * 1024;

enum class EcfgResourcePrimitive : quint8 {
    Bool = 1,
    U8 = 2,
    S8 = 3,
    U16 = 4,
    S16 = 5,
    U32 = 6,
    S32 = 7,
    U64 = 8,
    S64 = 9,
    Q32_32 = 10,
    RawBits = 11,
};

enum class EcfgResourceDirection : quint8 {
    Input = 1,
    Output = 2,
};

enum class EcfgResourceAccess : quint8 {
    Read = 1,
    ReadWrite = 3,
};

enum class EcfgResourceSource : quint8 {
    PdoInput = 1,
    PdoOutput = 2,
};

enum class EcfgOutputRecoveryPolicy : quint8 {
    ReturnTask = 1,
    HoldSafe = 2,
};

struct EcfgSectionDescriptor
{
    quint16 type = 0;
    quint32 offset = 0;
    quint32 length = 0;
    quint32 recordCount = 0;
};

struct EcfgRuntimeResource
{
    quint64 resourceId = 0;
    quint64 componentInstanceId = 0;
    quint64 parentInstanceId = 0;
    quint32 instanceOrdinal = 0;
    quint32 processImageBitOffset = 0;
    quint16 bitWidth = 0;
    quint16 processImageBitLength = 0;
    EcfgResourcePrimitive primitive = EcfgResourcePrimitive::Bool;
    EcfgResourceDirection direction = EcfgResourceDirection::Input;
    EcfgResourceAccess access = EcfgResourceAccess::Read;
    EcfgResourceSource source = EcfgResourceSource::PdoInput;
    quint8 valueBytes = 0;
    quint8 safeValueBytes = 0;
    quint16 qualityMask = 0;
    quint32 consistencyGroupId = 0;
    QByteArray safeValueLittleEndian;
};

struct EcfgOutputGroupPolicy
{
    quint32 consistencyGroupId = 0;
    quint32 flags = 0;
    EcfgOutputRecoveryPolicy recoveryPolicy = EcfgOutputRecoveryPolicy::ReturnTask;
    quint32 maximumTtlCycles = 0;
    quint32 resourceCount = 0;
    QByteArray groupResourceRecordsSha256;
    QList<quint64> resourceIds;
};

struct EcfgDcRecord
{
    quint16 stationAddress = 0;
    quint16 assignActivate = 0;
    quint32 sync0CycleNs = 0;
    bool referenceClock = false;
};

struct EcfgConfiguration
{
    quint16 formatMajor = 0;
    quint16 formatMinor = 0;
    quint32 flags = 0;
    quint64 configurationId = 0;
    quint64 buildTimestamp = 0;
    QByteArray configurationSha256;
    QByteArray capabilitySha256;
    QByteArray payloadSha256;
    QList<EcfgSectionDescriptor> sections;
    quint32 cyclePeriodNs = 0;
    QList<EcfgDcRecord> dcRecords;
    quint32 processInputBits = 0;
    quint32 processOutputBits = 0;
    quint64 catalogRevision = 0;
    quint64 topologyIdentity = 0;
    QByteArray resourceTableSectionSha256;
    QByteArray resourceRecordsSha256;
    QList<EcfgRuntimeResource> resources;
    QByteArray outputPolicySectionSha256;
    QByteArray outputPolicyRecordsSha256;
    QList<EcfgOutputGroupPolicy> outputPolicies;
};

Utils::Result<EcfgConfiguration> parseStrictEcfgConfiguration(
    QByteArrayView wire, qsizetype maximumConfigurationBytes = defaultMaximumEcfgConfigurationBytes);

} // namespace EtherCAT::SemanticRuntime::Internal
