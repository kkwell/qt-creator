// Copyright (C) 2026 Embed Labs

#include "ecfgconfiguration_p.h"

#include <QCryptographicHash>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QString>

#include <array>
#include <limits>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr quint32 configurationMagic = 0x47464345;
constexpr quint16 configurationMajor = 1;
constexpr quint16 resourceTableMinor = 1;
constexpr quint16 outputPolicyMinor = 2;
constexpr quint16 configurationKind = 1;
constexpr quint16 configurationHeaderBytes = 128;
constexpr quint16 sectionEntryBytes = 16;
constexpr quint32 configurationCrcOffset = 116;

constexpr quint16 topologySection = 1;
constexpr quint16 startupSection = 2;
constexpr quint16 pdoSection = 3;
constexpr quint16 dcSection = 4;
constexpr quint16 frameSection = 5;
constexpr quint16 faultSection = 6;
constexpr quint16 resourceTableSection = 7;
constexpr quint16 outputPolicySection = 8;

constexpr quint32 topologyRecordBytes = 48;
constexpr quint32 startupRecordBytes = 48;
constexpr quint32 pdoRecordBytes = 40;
constexpr quint32 dcRecordBytes = 48;
constexpr quint32 frameHeaderBytes = 48;
constexpr quint32 frameRecordBytes = 32;
constexpr quint32 patchRecordBytes = 24;
constexpr quint32 faultHeaderBytes = 48;
constexpr quint32 resourceTableHeaderBytes = 80;
constexpr quint32 resourceRecordBytes = 64;
constexpr quint32 outputPolicyHeaderBytes = 64;
constexpr quint32 outputPolicyRecordBytes = 64;

constexpr quint32 resourceTableMagic = 0x4352534f;
constexpr quint16 resourceTableVersion = 1;
constexpr quint32 frameMagic = 0x50464345;
constexpr quint16 frameVersion = 1;
constexpr quint32 outputPolicyMagic = 0x50544f4f;
constexpr quint16 outputPolicyVersion = 1;

constexpr quint16 topologySerialConstraint = 1U << 0;
constexpr quint16 topologyAliasRequired = 1U << 1;
constexpr quint16 topologyKnownFlags = topologySerialConstraint | topologyAliasRequired;
constexpr quint16 pdoFmmuPhysicalStartBitMask = 0x0007;
constexpr quint16 processOutput = 1;
constexpr quint16 processInput = 2;
constexpr quint16 pdoSm = 1;
constexpr quint16 pdoFmmu = 2;
constexpr quint16 pdoField = 3;
constexpr quint16 resourceQualityKnown = 0x000f;
constexpr quint32 outputPolicyManualWrite = 1U << 0;

constexpr quint32 maximumTopologyRecords = 192;
constexpr quint32 maximumStartupRecords = 4096;
constexpr quint32 maximumPdoRecords = 65536;
constexpr quint32 maximumDcRecords = 192;
constexpr quint32 maximumFrameRecords = 8;
constexpr quint32 maximumPatchRecords = 8192;
constexpr quint32 maximumResourceRecords = 16384;
constexpr quint32 maximumProcessImageBits = 8192 * 8;
constexpr quint32 maximumOutputPolicyResources = 64;
constexpr quint32 maximumOutputTtlCycles = 65535;

struct WireSection
{
    quint16 type = 0;
    quint32 offset = 0;
    quint32 length = 0;
    quint32 count = 0;
};

Utils::ResultError invalidConfiguration(const char *detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Invalid configuration.ecfg: %1").arg(QString::fromLatin1(detail)));
}

bool containsRange(quint64 total, quint64 offset, quint64 length)
{
    return offset <= total && length <= total - offset;
}

bool checkedMultiply(quint64 left, quint64 right, quint64 *result)
{
    if (!result || (left && right > std::numeric_limits<quint64>::max() / left))
        return false;
    *result = left * right;
    return true;
}

quint16 readLe16(QByteArrayView bytes, quint64 offset)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.data()) + offset;
    return quint16(quint16(data[0]) | (quint16(data[1]) << 8));
}

quint32 readLe32(QByteArrayView bytes, quint64 offset)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.data()) + offset;
    return quint32(data[0]) | (quint32(data[1]) << 8) | (quint32(data[2]) << 16)
           | (quint32(data[3]) << 24);
}

quint64 readLe64(QByteArrayView bytes, quint64 offset)
{
    return quint64(readLe32(bytes, offset)) | (quint64(readLe32(bytes, offset + 4)) << 32);
}

bool bytesAreZero(QByteArrayView bytes, quint64 offset, quint64 length)
{
    if (!containsRange(quint64(bytes.size()), offset, length))
        return false;
    quint8 combined = 0;
    for (quint64 index = 0; index < length; ++index)
        combined |= quint8(bytes[qsizetype(offset + index)]);
    return combined == 0;
}

bool digestIsZero(QByteArrayView digest)
{
    quint8 combined = 0;
    for (char byte : digest)
        combined |= quint8(byte);
    return combined == 0;
}

QByteArrayView subView(QByteArrayView bytes, quint64 offset, quint64 length)
{
    return QByteArrayView(bytes.data() + offset, qsizetype(length));
}

QByteArray sha256(QByteArrayView bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

quint32 crc32cWithZeroedChecksum(QByteArrayView bytes)
{
    quint32 crc = std::numeric_limits<quint32>::max();
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        const quint8 value = index >= configurationCrcOffset
                                     && index < configurationCrcOffset + qsizetype(sizeof(quint32))
                                 ? 0
                                 : quint8(bytes[index]);
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 lowBitMask = quint32(0) - (crc & 1U);
            crc = (crc >> 1) ^ (0x82f63b78U & lowBitMask);
        }
    }
    return ~crc;
}

bool fixedSectionHasSize(const WireSection &section, quint32 recordBytes, quint32 maximumRecords)
{
    quint64 expected = 0;
    return section.count > 0 && section.count <= maximumRecords
           && checkedMultiply(section.count, recordBytes, &expected) && expected == section.length;
}

const WireSection *findSection(const QList<WireSection> &sections, quint16 type)
{
    for (const WireSection &section : sections) {
        if (section.type == type)
            return &section;
    }
    return nullptr;
}

bool validateTopology(QByteArrayView wire, const WireSection &section, QSet<quint16> *stations)
{
    if (!stations || !fixedSectionHasSize(section, topologyRecordBytes, maximumTopologyRecords))
        return false;
    for (quint32 index = 0; index < section.count; ++index) {
        const quint64 offset = section.offset + quint64(index) * topologyRecordBytes;
        const quint16 order = readLe16(wire, offset);
        const quint16 station = readLe16(wire, offset + 2);
        const quint16 alias = readLe16(wire, offset + 4);
        const quint16 flags = readLe16(wire, offset + 6);
        const quint32 vendor = readLe32(wire, offset + 8);
        const quint32 product = readLe32(wire, offset + 12);
        const quint32 revisionMinimum = readLe32(wire, offset + 16);
        const quint32 revisionMaximum = readLe32(wire, offset + 20);
        const quint32 serialMinimum = readLe32(wire, offset + 24);
        const quint32 serialMaximum = readLe32(wire, offset + 28);
        const quint16 mailboxOutputStart = readLe16(wire, offset + 32);
        const quint16 mailboxOutputBytes = readLe16(wire, offset + 34);
        const quint16 mailboxInputStart = readLe16(wire, offset + 36);
        const quint16 mailboxInputBytes = readLe16(wire, offset + 38);

        if (order != index || !station || stations->contains(station) || !vendor || !product
            || revisionMinimum > revisionMaximum || (flags & ~topologyKnownFlags)
            || !bytesAreZero(wire, offset + 40, 8)
            || ((flags & topologySerialConstraint)
                && (serialMinimum > serialMaximum || (!serialMinimum && !serialMaximum)))
            || ((flags & topologyAliasRequired) && !alias)
            || ((!mailboxOutputStart) != (!mailboxOutputBytes))
            || ((!mailboxInputStart) != (!mailboxInputBytes))) {
            return false;
        }
        stations->insert(station);
    }
    return true;
}

bool validateStartup(QByteArrayView wire, const WireSection &section, const QSet<quint16> &stations)
{
    if (!fixedSectionHasSize(section, startupRecordBytes, maximumStartupRecords))
        return false;
    for (quint32 index = 0; index < section.count; ++index) {
        const quint64 offset = section.offset + quint64(index) * startupRecordBytes;
        const quint16 kind = readLe16(wire, offset);
        const quint16 station = readLe16(wire, offset + 4);
        const quint16 targetState = readLe16(wire, offset + 6);
        const quint16 objectIndex = readLe16(wire, offset + 8);
        const quint8 valueBytes = quint8(wire[qsizetype(offset + 11)]);
        const quint16 retryCount = readLe16(wire, offset + 12);
        const quint16 syncManager = readLe16(wire, offset + 14);
        const quint32 timeoutNs = readLe32(wire, offset + 16);
        const quint16 syncManagerStart = readLe16(wire, offset + 32);
        const quint16 syncManagerLength = readLe16(wire, offset + 34);
        const quint8 syncManagerEnable = quint8(wire[qsizetype(offset + 37)]);

        if (!stations.contains(station) || !timeoutNs || retryCount > 255
            || !bytesAreZero(wire, offset + 38, 10)) {
            return false;
        }
        if (kind == 1) {
            if (syncManager > 7 || !syncManagerStart || (!syncManagerLength && syncManagerEnable)
                || syncManagerEnable > 1 || objectIndex || valueBytes || targetState) {
                return false;
            }
        } else if (kind == 2) {
            if (!objectIndex || !valueBytes || valueBytes > 4 || targetState || syncManager
                || syncManagerStart || syncManagerLength) {
                return false;
            }
        } else if (kind == 3) {
            if ((targetState != 1 && targetState != 2 && targetState != 4 && targetState != 8)
                || objectIndex || valueBytes || syncManager || syncManagerStart
                || syncManagerLength) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool validatePdo(
    QByteArrayView wire,
    const WireSection &section,
    const QSet<quint16> &stations,
    quint32 *inputBits,
    quint32 *outputBits)
{
    if (!inputBits || !outputBits
        || !fixedSectionHasSize(section, pdoRecordBytes, maximumPdoRecords)) {
        return false;
    }
    quint32 maximumInput = 0;
    quint32 maximumOutput = 0;
    for (quint32 index = 0; index < section.count; ++index) {
        const quint64 offset = section.offset + quint64(index) * pdoRecordBytes;
        const quint16 kind = readLe16(wire, offset);
        const quint16 flags = readLe16(wire, offset + 2);
        const quint16 station = readLe16(wire, offset + 4);
        const quint16 kindIndex = readLe16(wire, offset + 6);
        const quint32 logicalAddress = readLe32(wire, offset + 8);
        const quint16 physicalAddress = readLe16(wire, offset + 12);
        const quint16 byteLength = readLe16(wire, offset + 14);
        const quint32 bitOffset = readLe32(wire, offset + 16);
        const quint32 bitLength = readLe32(wire, offset + 20);
        const quint8 direction = quint8(wire[qsizetype(offset + 30)]);
        const quint8 syncManager = quint8(wire[qsizetype(offset + 31)]);

        if (!stations.contains(station) || !bytesAreZero(wire, offset + 36, 4))
            return false;
        if (kind == pdoSm) {
            if (syncManager > 7 || !byteLength
                || (direction != processOutput && direction != processInput)) {
                return false;
            }
        } else if (kind == pdoFmmu) {
            if (!byteLength || !physicalAddress || kindIndex > 7
                || (flags & ~pdoFmmuPhysicalStartBitMask) || bitOffset > 7 || !bitLength
                || bitLength > std::numeric_limits<quint32>::max() - bitOffset
                || (direction != processOutput && direction != processInput)) {
                return false;
            }
            const quint64 logicalBytes = (quint64(bitOffset) + bitLength + 7) / 8;
            if (logicalBytes > byteLength
                || logicalBytes > std::numeric_limits<quint32>::max() - logicalAddress) {
                return false;
            }
        } else if (kind == pdoField) {
            if (!bitLength || bitLength > std::numeric_limits<quint32>::max() - bitOffset
                || (direction != processOutput && direction != processInput)) {
                return false;
            }
            const quint32 end = bitOffset + bitLength;
            if (direction == processInput)
                maximumInput = qMax(maximumInput, end);
            else
                maximumOutput = qMax(maximumOutput, end);
        } else {
            return false;
        }
    }
    if (maximumInput > maximumProcessImageBits || maximumOutput > maximumProcessImageBits)
        return false;
    *inputBits = maximumInput;
    *outputBits = maximumOutput;
    return true;
}

bool validateFrameSection(QByteArrayView wire, const WireSection &section, quint32 *cyclePeriodNs)
{
    if (!cyclePeriodNs || section.length < frameHeaderBytes)
        return false;
    const quint64 offset = section.offset;
    const quint32 cycle = readLe32(wire, offset + 8);
    const quint16 linkSpeedMbps = readLe16(wire, offset + 12);
    const quint16 frameCount = readLe16(wire, offset + 14);
    const quint32 patchCount = readLe32(wire, offset + 16);
    const quint32 framesOffset = readLe32(wire, offset + 20);
    const quint32 patchesOffset = readLe32(wire, offset + 24);
    const quint32 templatesOffset = readLe32(wire, offset + 28);
    const quint32 templateBytes = readLe32(wire, offset + 32);
    quint64 frameArrayBytes = 0;
    quint64 patchArrayBytes = 0;
    if (readLe32(wire, offset) != frameMagic || readLe16(wire, offset + 4) != frameVersion
        || readLe16(wire, offset + 6) != frameHeaderBytes || !cycle || linkSpeedMbps != 100
        || !frameCount || frameCount != section.count || frameCount > maximumFrameRecords
        || patchCount > maximumPatchRecords
        || !checkedMultiply(frameCount, frameRecordBytes, &frameArrayBytes)
        || !checkedMultiply(patchCount, patchRecordBytes, &patchArrayBytes)
        || framesOffset != frameHeaderBytes || patchesOffset != frameHeaderBytes + frameArrayBytes
        || templatesOffset != patchesOffset + patchArrayBytes
        || quint64(templatesOffset) + templateBytes != section.length || !templateBytes) {
        return false;
    }

    QSet<quint32> frameIds;
    quint64 expectedTemplateOffset = 0;
    for (quint32 index = 0; index < frameCount; ++index) {
        const quint64 record = offset + framesOffset + quint64(index) * frameRecordBytes;
        const quint32 frameId = readLe32(wire, record);
        const quint32 templateOffset = readLe32(wire, record + 4);
        const quint16 frameLength = readLe16(wire, record + 8);
        const quint16 firstPatch = readLe16(wire, record + 10);
        const quint16 ownedPatches = readLe16(wire, record + 12);
        const quint16 expectedWkc = readLe16(wire, record + 14);
        const quint32 transmitSlot = readLe32(wire, record + 16);
        const quint32 submitLead = readLe32(wire, record + 20);
        const quint32 responseTimeout = readLe32(wire, record + 24);
        if (!frameId || frameIds.contains(frameId) || templateOffset != expectedTemplateOffset
            || frameLength < 60 || frameLength > 1518
            || frameLength > templateBytes - templateOffset || firstPatch > patchCount
            || ownedPatches > patchCount - firstPatch || !expectedWkc || transmitSlot >= cycle
            || !submitLead || !responseTimeout) {
            return false;
        }
        frameIds.insert(frameId);
        expectedTemplateOffset += frameLength;
    }
    if (expectedTemplateOffset != templateBytes)
        return false;

    for (quint32 index = 0; index < patchCount; ++index) {
        const quint64 record = offset + patchesOffset + quint64(index) * patchRecordBytes;
        const quint16 type = readLe16(wire, record);
        const quint16 frameIndex = readLe16(wire, record + 4);
        const quint16 width = readLe16(wire, record + 6);
        if (type < 1 || type > 7 || frameIndex >= frameCount
            || (width != 1 && width != 2 && width != 4 && width != 8) || (type == 3 && width != 1)
            || ((type == 2 || type == 6) && width != 4)) {
            return false;
        }
    }
    *cyclePeriodNs = cycle;
    return true;
}

bool validateDc(
    QByteArrayView wire,
    const WireSection &section,
    const QSet<quint16> &stations,
    quint32 cyclePeriodNs)
{
    if (!fixedSectionHasSize(section, dcRecordBytes, maximumDcRecords))
        return false;
    quint32 referenceClocks = 0;
    for (quint32 index = 0; index < section.count; ++index) {
        const quint64 offset = section.offset + quint64(index) * dcRecordBytes;
        const quint16 station = readLe16(wire, offset);
        const quint16 flags = readLe16(wire, offset + 2);
        const quint16 assignActivate = readLe16(wire, offset + 4);
        const quint32 sync0Cycle = readLe32(wire, offset + 8);
        const quint32 convergence = readLe32(wire, offset + 24);
        const quint32 warmupCycles = readLe32(wire, offset + 28);
        const quint32 faultThreshold = readLe32(wire, offset + 32);
        const quint32 stableCycles = readLe32(wire, offset + 36);
        if (!stations.contains(station) || (flags & ~quint16(1)) || !assignActivate
            || sync0Cycle != cyclePeriodNs || !convergence || !warmupCycles || !faultThreshold
            || !stableCycles || !bytesAreZero(wire, offset + 40, 8)) {
            return false;
        }
        if (flags & 1)
            ++referenceClocks;
    }
    return referenceClocks == 1;
}

bool validateFault(QByteArrayView wire, const WireSection &section, quint32 processOutputBits)
{
    if (section.count != 1 || section.length < faultHeaderBytes)
        return false;
    const quint64 offset = section.offset;
    const quint32 safeOffset = readLe32(wire, offset + 32);
    const quint32 safeLength = readLe32(wire, offset + 36);
    if (readLe16(wire, offset) != 1 || readLe16(wire, offset + 2) != faultHeaderBytes
        || !bytesAreZero(wire, offset + 14, 2) || !bytesAreZero(wire, offset + 44, 4)
        || quint64(safeOffset) + safeLength != section.length
        || safeLength != (quint64(processOutputBits) + 7) / 8
        || (safeLength ? safeOffset != faultHeaderBytes : safeOffset != 0)) {
        return false;
    }
    for (quint64 field = offset + 8; field < offset + 14; ++field) {
        const quint8 action = quint8(wire[qsizetype(field)]);
        if (action < 1 || action > 5)
            return false;
    }
    return true;
}

quint16 fixedPrimitiveWidth(EcfgResourcePrimitive primitive)
{
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return 1;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::S8:
        return 8;
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::S16:
        return 16;
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::S32:
        return 32;
    case EcfgResourcePrimitive::U64:
    case EcfgResourcePrimitive::S64:
    case EcfgResourcePrimitive::Q32_32:
        return 64;
    case EcfgResourcePrimitive::RawBits:
        return 0;
    }
    return 0;
}

bool validPrimitive(quint8 value)
{
    return value >= quint8(EcfgResourcePrimitive::Bool)
           && value <= quint8(EcfgResourcePrimitive::RawBits);
}

Utils::Result<void> parseResourceTable(
    QByteArrayView wire,
    const WireSection &section,
    quint32 processInputBits,
    quint32 processOutputBits,
    EcfgConfiguration *configuration,
    QHash<quint32, QList<quint32>> *groupResourceIndexes)
{
    if (!configuration || !groupResourceIndexes || section.length < resourceTableHeaderBytes)
        return invalidConfiguration("the runtime resource table header is truncated");
    const quint64 offset = section.offset;
    const quint32 count = readLe32(wire, offset + 12);
    const quint64 catalogRevision = readLe64(wire, offset + 16);
    const quint64 topologyIdentity = readLe64(wire, offset + 24);
    const quint32 recordsOffset = readLe32(wire, offset + 32);
    const quint32 recordsBytes = readLe32(wire, offset + 36);
    quint64 expectedRecordsBytes = 0;
    if (readLe32(wire, offset) != resourceTableMagic
        || readLe16(wire, offset + 4) != resourceTableVersion
        || readLe16(wire, offset + 6) != resourceTableHeaderBytes
        || readLe16(wire, offset + 8) != resourceRecordBytes || readLe16(wire, offset + 10)
        || !count || count != section.count || count > maximumResourceRecords
        || !checkedMultiply(count, resourceRecordBytes, &expectedRecordsBytes)
        || recordsOffset != resourceTableHeaderBytes || recordsBytes != expectedRecordsBytes
        || quint64(recordsOffset) + recordsBytes != section.length || !catalogRevision
        || !topologyIdentity || !bytesAreZero(wire, offset + 72, 8)) {
        return invalidConfiguration("the runtime resource table header is noncanonical");
    }

    const QByteArrayView records = subView(wire, offset + recordsOffset, recordsBytes);
    const QByteArray recordsSha256 = sha256(records);
    if (recordsSha256 != subView(wire, offset + 40, 32))
        return invalidConfiguration("the runtime resource record digest does not match");
    const quint64 rawComputedRevision = readLe64(recordsSha256, 0);
    const quint64 computedRevision = rawComputedRevision ? rawComputedRevision : quint64(1);
    if (computedRevision != catalogRevision)
        return invalidConfiguration("the runtime resource catalog revision does not match");

    quint64 computedTopologyIdentity = 0xcbf29ce484222325ULL;
    quint64 previousResourceId = 0;
    QHash<quint64, QPair<quint64, quint32>> componentBindings;
    configuration->resources.reserve(qsizetype(count));
    for (quint32 index = 0; index < count; ++index) {
        const quint64 recordOffset = offset + recordsOffset + quint64(index) * resourceRecordBytes;
        EcfgRuntimeResource resource;
        resource.resourceId = readLe64(wire, recordOffset);
        resource.componentInstanceId = readLe64(wire, recordOffset + 8);
        resource.parentInstanceId = readLe64(wire, recordOffset + 16);
        resource.instanceOrdinal = readLe32(wire, recordOffset + 24);
        resource.processImageBitOffset = readLe32(wire, recordOffset + 28);
        resource.bitWidth = readLe16(wire, recordOffset + 32);
        resource.processImageBitLength = readLe16(wire, recordOffset + 34);
        const quint8 primitive = quint8(wire[qsizetype(recordOffset + 36)]);
        const quint8 direction = quint8(wire[qsizetype(recordOffset + 37)]);
        const quint8 access = quint8(wire[qsizetype(recordOffset + 38)]);
        const quint8 source = quint8(wire[qsizetype(recordOffset + 39)]);
        resource.valueBytes = quint8(wire[qsizetype(recordOffset + 40)]);
        resource.safeValueBytes = quint8(wire[qsizetype(recordOffset + 41)]);
        resource.qualityMask = readLe16(wire, recordOffset + 42);
        resource.consistencyGroupId = readLe32(wire, recordOffset + 44);

        if (!validPrimitive(primitive)
            || (direction != quint8(EcfgResourceDirection::Input)
                && direction != quint8(EcfgResourceDirection::Output))
            || (access != quint8(EcfgResourceAccess::Read)
                && access != quint8(EcfgResourceAccess::ReadWrite))
            || (source != quint8(EcfgResourceSource::PdoInput)
                && source != quint8(EcfgResourceSource::PdoOutput))) {
            return invalidConfiguration("a runtime resource enum is invalid");
        }
        resource.primitive = EcfgResourcePrimitive(primitive);
        resource.direction = EcfgResourceDirection(direction);
        resource.access = EcfgResourceAccess(access);
        resource.source = EcfgResourceSource(source);
        const quint16 fixedWidth = fixedPrimitiveWidth(resource.primitive);
        const quint32 processBits = resource.source == EcfgResourceSource::PdoInput
                                        ? processInputBits
                                        : processOutputBits;
        if (!resource.resourceId || resource.resourceId <= previousResourceId
            || !resource.componentInstanceId
            || resource.parentInstanceId == resource.componentInstanceId || !resource.bitWidth
            || resource.bitWidth > 128 || (fixedWidth && resource.bitWidth != fixedWidth)
            || resource.processImageBitLength != resource.bitWidth
            || resource.valueBytes != (resource.bitWidth + 7) / 8
            || (resource.safeValueBytes != 0 && resource.safeValueBytes != resource.valueBytes)
            || resource.qualityMask != resourceQualityKnown || !resource.consistencyGroupId
            || ((resource.direction == EcfgResourceDirection::Input)
                != (resource.source == EcfgResourceSource::PdoInput))
            || ((resource.direction == EcfgResourceDirection::Output)
                != (resource.source == EcfgResourceSource::PdoOutput))
            || (resource.access == EcfgResourceAccess::ReadWrite
                && (resource.direction != EcfgResourceDirection::Output
                    || resource.safeValueBytes != resource.valueBytes))
            || (resource.access == EcfgResourceAccess::Read && resource.safeValueBytes)
            || !bytesAreZero(
                wire, recordOffset + 48 + resource.safeValueBytes, 16 - resource.safeValueBytes)
            || resource.processImageBitOffset > processBits
            || resource.bitWidth > processBits - resource.processImageBitOffset) {
            return invalidConfiguration("a runtime resource record is noncanonical");
        }
        if (resource.bitWidth % 8 && resource.safeValueBytes) {
            const quint8 highByte = quint8(
                wire[qsizetype(recordOffset + 48 + resource.safeValueBytes - 1)]);
            const quint8 allowed = quint8((1U << (resource.bitWidth % 8)) - 1U);
            if (highByte & ~allowed)
                return invalidConfiguration("a runtime resource safe value is noncanonical");
        }

        const QPair<quint64, quint32>
            componentBinding{resource.parentInstanceId, resource.instanceOrdinal};
        if (componentBindings.contains(resource.componentInstanceId)
            && componentBindings.value(resource.componentInstanceId) != componentBinding) {
            return invalidConfiguration("a component instance binding is inconsistent");
        }
        componentBindings.insert(resource.componentInstanceId, componentBinding);

        for (quint64 byte = 8; byte < 28; ++byte) {
            computedTopologyIdentity ^= quint8(wire[qsizetype(recordOffset + byte)]);
            computedTopologyIdentity *= 0x100000001b3ULL;
        }
        if (resource.safeValueBytes) {
            resource.safeValueLittleEndian
                = QByteArray(wire.data() + recordOffset + 48, resource.safeValueBytes);
        }
        groupResourceIndexes->operator[](resource.consistencyGroupId).append(index);
        configuration->resources.append(std::move(resource));
        previousResourceId = configuration->resources.constLast().resourceId;
    }
    if (!computedTopologyIdentity)
        computedTopologyIdentity = 1;
    if (computedTopologyIdentity != topologyIdentity)
        return invalidConfiguration("the runtime resource topology identity does not match");

    configuration->catalogRevision = catalogRevision;
    configuration->topologyIdentity = topologyIdentity;
    configuration->resourceTableSectionSha256 = sha256(subView(wire, offset, section.length));
    configuration->resourceRecordsSha256 = recordsSha256;
    return {};
}

Utils::Result<void> parseOutputPolicies(
    QByteArrayView wire,
    const WireSection &section,
    const QHash<quint32, QList<quint32>> &groupResourceIndexes,
    EcfgConfiguration *configuration)
{
    if (!configuration || section.length < outputPolicyHeaderBytes)
        return invalidConfiguration("the output policy header is truncated");
    const quint64 offset = section.offset;
    const quint32 count = readLe32(wire, offset + 12);
    const quint32 recordsOffset = readLe32(wire, offset + 16);
    const quint32 recordsBytes = readLe32(wire, offset + 20);
    quint64 expectedRecordsBytes = 0;
    if (readLe32(wire, offset) != outputPolicyMagic
        || readLe16(wire, offset + 4) != outputPolicyVersion
        || readLe16(wire, offset + 6) != outputPolicyHeaderBytes
        || readLe16(wire, offset + 8) != outputPolicyRecordBytes || readLe16(wire, offset + 10)
        || !count || count != section.count || count > quint32(configuration->resources.size())
        || !checkedMultiply(count, outputPolicyRecordBytes, &expectedRecordsBytes)
        || recordsOffset != outputPolicyHeaderBytes || recordsBytes != expectedRecordsBytes
        || quint64(recordsOffset) + recordsBytes != section.length
        || !bytesAreZero(wire, offset + 56, 8)) {
        return invalidConfiguration("the output policy header is noncanonical");
    }

    const QByteArrayView records = subView(wire, offset + recordsOffset, recordsBytes);
    const QByteArray recordsSha256 = sha256(records);
    if (recordsSha256 != subView(wire, offset + 24, 32))
        return invalidConfiguration("the output policy record digest does not match");

    quint32 previousGroupId = 0;
    configuration->outputPolicies.reserve(qsizetype(count));
    for (quint32 index = 0; index < count; ++index) {
        const quint64 recordOffset = offset + recordsOffset
                                     + quint64(index) * outputPolicyRecordBytes;
        EcfgOutputGroupPolicy policy;
        policy.consistencyGroupId = readLe32(wire, recordOffset);
        policy.flags = readLe32(wire, recordOffset + 4);
        const quint8 recovery = quint8(wire[qsizetype(recordOffset + 8)]);
        policy.maximumTtlCycles = readLe32(wire, recordOffset + 12);
        policy.resourceCount = readLe32(wire, recordOffset + 16);
        policy.groupResourceRecordsSha256 = QByteArray(wire.data() + recordOffset + 32, 32);
        const QList<quint32> resourceIndexes = groupResourceIndexes.value(policy.consistencyGroupId);
        if (!policy.consistencyGroupId || policy.consistencyGroupId <= previousGroupId
            || policy.flags != outputPolicyManualWrite
            || (recovery != quint8(EcfgOutputRecoveryPolicy::ReturnTask)
                && recovery != quint8(EcfgOutputRecoveryPolicy::HoldSafe))
            || !policy.maximumTtlCycles || policy.maximumTtlCycles > maximumOutputTtlCycles
            || !policy.resourceCount || policy.resourceCount > maximumOutputPolicyResources
            || policy.resourceCount != quint32(resourceIndexes.size())
            || !bytesAreZero(wire, recordOffset + 9, 3)
            || !bytesAreZero(wire, recordOffset + 20, 12)) {
            return invalidConfiguration("an output policy record is noncanonical");
        }
        policy.recoveryPolicy = EcfgOutputRecoveryPolicy(recovery);

        QCryptographicHash groupHash(QCryptographicHash::Sha256);
        for (quint32 resourceIndex : resourceIndexes) {
            if (resourceIndex >= quint32(configuration->resources.size()))
                return invalidConfiguration("an output policy resource index is invalid");
            const EcfgRuntimeResource &resource = configuration->resources.at(resourceIndex);
            if (resource.direction != EcfgResourceDirection::Output
                || resource.source != EcfgResourceSource::PdoOutput
                || resource.access != EcfgResourceAccess::ReadWrite
                || resource.safeValueBytes != resource.valueBytes) {
                return invalidConfiguration(
                    "an output policy references a non-writable runtime resource");
            }
            quint32 resourceTableOffset = 0;
            for (const EcfgSectionDescriptor &descriptor : configuration->sections) {
                if (descriptor.type == resourceTableSection) {
                    resourceTableOffset = descriptor.offset;
                    break;
                }
            }
            if (!resourceTableOffset)
                return invalidConfiguration("the runtime resource section is unavailable");
            const quint64 resourceWireOffset = resourceTableOffset + resourceTableHeaderBytes
                                               + quint64(resourceIndex) * resourceRecordBytes;
            groupHash.addData(subView(wire, resourceWireOffset, resourceRecordBytes));
            policy.resourceIds.append(resource.resourceId);
        }
        if (groupHash.result() != policy.groupResourceRecordsSha256)
            return invalidConfiguration("an output policy group digest does not match");
        configuration->outputPolicies.append(std::move(policy));
        previousGroupId = configuration->outputPolicies.constLast().consistencyGroupId;
    }

    configuration->outputPolicySectionSha256 = sha256(subView(wire, offset, section.length));
    configuration->outputPolicyRecordsSha256 = recordsSha256;
    return {};
}

} // namespace

Utils::Result<EcfgConfiguration> parseStrictEcfgConfiguration(
    QByteArrayView wire, qsizetype maximumConfigurationBytes)
{
    if (maximumConfigurationBytes <= 0)
        return invalidConfiguration("the configured size limit is invalid");
    if (wire.isEmpty())
        return invalidConfiguration("the configuration is empty");
    if (wire.size() > defaultMaximumEcfgConfigurationBytes)
        return invalidConfiguration("the configuration exceeds the ECFG size limit");
    if (wire.size() > maximumConfigurationBytes)
        return invalidConfiguration("the configuration exceeds the configured size limit");
    if (wire.size() < configurationHeaderBytes)
        return invalidConfiguration("the fixed header is truncated");

    const quint64 wireBytes = quint64(wire.size());
    const quint16 major = readLe16(wire, 4);
    const quint16 minor = readLe16(wire, 6);
    const quint16 sectionCount = readLe16(wire, 104);
    const quint32 sectionTableOffset = readLe32(wire, 100);
    const quint16 entryBytes = readLe16(wire, 106);
    const quint32 payloadOffset = readLe32(wire, 108);
    const quint32 payloadBytes = readLe32(wire, 112);
    quint64 sectionTableBytes = 0;
    if (readLe32(wire, 0) != configurationMagic || major != configurationMajor
        || (minor != resourceTableMinor && minor != outputPolicyMinor)
        || readLe16(wire, 8) != configurationHeaderBytes || readLe16(wire, 10) != configurationKind
        || readLe32(wire, 12) != wireBytes || !readLe64(wire, 20) || !readLe64(wire, 28)
        || digestIsZero(subView(wire, 36, 32)) || sectionTableOffset != configurationHeaderBytes
        || !sectionCount || sectionCount > outputPolicySection || entryBytes != sectionEntryBytes
        || !checkedMultiply(sectionCount, entryBytes, &sectionTableBytes)
        || payloadOffset != sectionTableOffset + sectionTableBytes || !payloadBytes
        || quint64(payloadOffset) + payloadBytes != wireBytes || !bytesAreZero(wire, 120, 8)) {
        return invalidConfiguration("the fixed header is noncanonical");
    }
    if (readLe32(wire, configurationCrcOffset) != crc32cWithZeroedChecksum(wire))
        return invalidConfiguration("the CRC32C does not match");
    const QByteArray computedPayloadSha256 = sha256(subView(wire, payloadOffset, payloadBytes));
    if (computedPayloadSha256 != subView(wire, 68, 32))
        return invalidConfiguration("the payload digest does not match");

    QList<WireSection> wireSections;
    wireSections.reserve(sectionCount);
    quint16 previousType = 0;
    quint64 nextSectionOffset = payloadOffset;
    for (quint16 index = 0; index < sectionCount; ++index) {
        const quint64 entryOffset = sectionTableOffset + quint64(index) * entryBytes;
        WireSection section;
        section.type = readLe16(wire, entryOffset);
        const quint16 sectionFlags = readLe16(wire, entryOffset + 2);
        section.offset = readLe32(wire, entryOffset + 4);
        section.length = readLe32(wire, entryOffset + 8);
        section.count = readLe32(wire, entryOffset + 12);
        const quint16 maximumType = minor >= outputPolicyMinor ? outputPolicySection
                                                               : resourceTableSection;
        if (section.type <= previousType || section.type > maximumType || sectionFlags
            || !section.length || !section.count || section.offset != nextSectionOffset
            || !containsRange(wireBytes, section.offset, section.length)) {
            return invalidConfiguration("the section table is noncanonical");
        }
        nextSectionOffset = quint64(section.offset) + section.length;
        previousType = section.type;
        wireSections.append(section);
    }
    if (nextSectionOffset != wireBytes)
        return invalidConfiguration("the section payloads are not contiguous");
    for (quint16 required : std::array<quint16, 5>{
             topologySection, pdoSection, frameSection, faultSection, resourceTableSection}) {
        if (!findSection(wireSections, required))
            return invalidConfiguration("a required section is missing");
    }
    const bool hasOutputPolicy = findSection(wireSections, outputPolicySection);
    if (hasOutputPolicy != (minor == outputPolicyMinor))
        return invalidConfiguration("the format minor and output policy section disagree");

    const WireSection *topology = findSection(wireSections, topologySection);
    const WireSection *pdo = findSection(wireSections, pdoSection);
    const WireSection *frames = findSection(wireSections, frameSection);
    const WireSection *fault = findSection(wireSections, faultSection);
    QSet<quint16> stations;
    quint32 inputBits = 0;
    quint32 outputBits = 0;
    quint32 cyclePeriodNs = 0;
    if (!validateTopology(wire, *topology, &stations))
        return invalidConfiguration("the topology section is invalid");
    if (const WireSection *startup = findSection(wireSections, startupSection);
        startup && !validateStartup(wire, *startup, stations)) {
        return invalidConfiguration("the startup section is invalid");
    }
    if (!validatePdo(wire, *pdo, stations, &inputBits, &outputBits))
        return invalidConfiguration("the PDO section is invalid");
    if (!validateFrameSection(wire, *frames, &cyclePeriodNs))
        return invalidConfiguration("the cyclic frame section is invalid");
    if (const WireSection *dc = findSection(wireSections, dcSection);
        dc && !validateDc(wire, *dc, stations, cyclePeriodNs)) {
        return invalidConfiguration("the DC section is invalid");
    }
    if (!validateFault(wire, *fault, outputBits))
        return invalidConfiguration("the fault policy section is invalid");

    EcfgConfiguration configuration;
    configuration.formatMajor = major;
    configuration.formatMinor = minor;
    configuration.flags = readLe32(wire, 16);
    configuration.configurationId = readLe64(wire, 20);
    configuration.buildTimestamp = readLe64(wire, 28);
    configuration.configurationSha256 = sha256(wire);
    configuration.capabilitySha256 = QByteArray(wire.data() + 36, 32);
    configuration.payloadSha256 = computedPayloadSha256;
    configuration.processInputBits = inputBits;
    configuration.processOutputBits = outputBits;
    configuration.sections.reserve(sectionCount);
    for (const WireSection &section : std::as_const(wireSections)) {
        configuration.sections.append({section.type, section.offset, section.length, section.count});
    }

    QHash<quint32, QList<quint32>> groupResourceIndexes;
    const WireSection *resources = findSection(wireSections, resourceTableSection);
    const Utils::Result<void> resourceError = parseResourceTable(
        wire, *resources, inputBits, outputBits, &configuration, &groupResourceIndexes);
    if (!resourceError)
        return Utils::ResultError(resourceError.error());
    if (const WireSection *policies = findSection(wireSections, outputPolicySection)) {
        const Utils::Result<void> policyError
            = parseOutputPolicies(wire, *policies, groupResourceIndexes, &configuration);
        if (!policyError)
            return Utils::ResultError(policyError.error());
    }
    return configuration;
}

} // namespace EtherCAT::SemanticRuntime::Internal
