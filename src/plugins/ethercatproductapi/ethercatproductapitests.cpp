// Copyright (C) 2026 Kvell

#include "ethercatproductapitests.h"

#include "ethercatproductapiconstants.h"
#include "ethercatproductapitr.h"
#include "productapicodec.h"
#include "productapiconnectionprovider.h"
#include "productapisession.h"

#include <utils/qtcsettings.h>

#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

namespace EtherCAT::ProductApi::Internal {

namespace {

constexpr quint64 TestSessionId = 0x1020304050607080;
constexpr quint64 TestBootId = 0x8877665544332211;
constexpr quint64 CommandStatusDetail = 0x1122334455667788;
constexpr quint64 FirmwareStateDetail = 0x8877665544332211;
constexpr quint32 TestAlarmSequence = 1;
constexpr int AlarmEventBytes = 64;
constexpr int CapacityRetryAfterMs = 100;
constexpr qint32 InternalStatus = -16;
constexpr qint32 EventGapStatus = -25;

class SettingsValueGuard
{
public:
    explicit SettingsValueGuard(const Utils::Key &key)
        : m_key(key)
        , m_existed(Utils::userSettings().contains(key))
        , m_value(Utils::userSettings().value(key))
    {}

    ~SettingsValueGuard()
    {
        if (m_existed)
            Utils::userSettings().setValue(m_key, m_value);
        else
            Utils::userSettings().remove(m_key);
    }

private:
    Utils::Key m_key;
    bool m_existed = false;
    QVariant m_value;
};

void putU16(QByteArray &bytes, qsizetype offset, quint16 value)
{
    bytes[offset] = char(value >> 8);
    bytes[offset + 1] = char(value);
}

void putU32(QByteArray &bytes, qsizetype offset, quint32 value)
{
    bytes[offset] = char(value >> 24);
    bytes[offset + 1] = char(value >> 16);
    bytes[offset + 2] = char(value >> 8);
    bytes[offset + 3] = char(value);
}

void putU64(QByteArray &bytes, qsizetype offset, quint64 value)
{
    putU32(bytes, offset, quint32(value >> 32));
    putU32(bytes, offset + 4, quint32(value));
}

void putI32(QByteArray &bytes, qsizetype offset, qint32 value)
{
    putU32(bytes, offset, quint32(value));
}

quint16 readU16(QByteArrayView bytes, qsizetype offset)
{
    return (quint16(quint8(bytes[offset])) << 8) | quint16(quint8(bytes[offset + 1]));
}

quint32 readU32(QByteArrayView bytes, qsizetype offset)
{
    return (quint32(quint8(bytes[offset])) << 24)
           | (quint32(quint8(bytes[offset + 1])) << 16)
           | (quint32(quint8(bytes[offset + 2])) << 8)
           | quint32(quint8(bytes[offset + 3]));
}

quint64 readU64(QByteArrayView bytes, qsizetype offset)
{
    return (quint64(readU32(bytes, offset)) << 32) | readU32(bytes, offset + 4);
}

quint32 oracleCrc32c(QByteArrayView bytes)
{
    quint32 crc = 0xffffffffU;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0x82f63b78U : 0U);
    }
    return ~crc;
}

void rewriteCrc(QByteArray &wire)
{
    putU32(wire, 60, 0);
    putU32(wire, 60, oracleCrc32c(wire));
}

Protocol::Frame responseFrame(Protocol::MessageType type,
                              const QByteArray &payload,
                              quint64 sequence = 1,
                              quint32 flags = Protocol::flagValue(Protocol::Flag::Response))
{
    Protocol::Frame frame;
    frame.header.messageType = type;
    frame.header.flags = flags;
    frame.header.payloadLength = quint32(payload.size());
    frame.header.sessionId = TestSessionId;
    frame.header.requestId = 17;
    frame.header.sequence = sequence;
    frame.header.bootId = TestBootId;
    frame.header.controllerTimestampNs = 123456;
    frame.payload = payload;
    return frame;
}

QByteArray controllerStatePayload()
{
    QByteArray payload(80, '\0');
    putU32(payload, 0, 4);       // RUNNING
    putU32(payload, 4, 0x47);    // READY | BUS_OP | ACTIVE | DC_LOCKED
    putU32(payload, 8, 0);       // NONE
    putI32(payload, 12, 0);
    putU64(payload, 16, 0);
    putU64(payload, 24, 0);
    putU64(payload, 32, 101);
    putU64(payload, 40, 202);
    putU64(payload, 48, TestBootId);
    putU32(payload, 56, 0x08);
    putU32(payload, 60, 6);
    putU32(payload, 64, 6);
    putU32(payload, 68, 73);
    putU32(payload, 72, 0);
    putU32(payload, 76, 19);
    return payload;
}

QByteArray performanceSnapshotPayload()
{
    QByteArray payload(320, '\0');
    putU64(payload, 0, 100);
    putU64(payload, 8, 240);
    putU64(payload, 16, 17);
    putU32(payload, 24, 2);
    putU32(payload, 28, 3);
    putU32(payload, 32, 4);
    putU32(payload, 36, 5);
    putU32(payload, 40, 6);
    putU32(payload, 44, 7);
    putU32(payload, 48, 8);
    putU32(payload, 52, 9);
    putU32(payload, 56, 10);
    putU32(payload, 120, 0x11);
    putU32(payload, 124, 12);
    const std::array<quint32, 4> sampleWords{
        0x04030201,
        0x08070605,
        0x0c0b0a09,
        0x100f0e0d,
    };
    for (qsizetype index = 0; index < qsizetype(sampleWords.size()); ++index) {
        putU32(payload, 240 + index * 4, sampleWords.at(size_t(index)));
        putU32(payload, 256 + index * 4, sampleWords.at(size_t(index)));
    }
    putU32(payload, 272, 0x7); // valid | fresh | complete
    putU32(payload, 276, 0);
    putU32(payload, 280, 16);
    putU32(payload, 284, 16);
    putU32(payload, 288, 13);
    putU32(payload, 292, 128);
    putU64(payload, 296, 33);
    putU64(payload, 304, 44);
    putU64(payload, 312, 303);
    return payload;
}

QByteArray lifecycleControllerStatePayload(
    quint32 serviceState,
    quint64 cycleCount = 202,
    quint64 currentFaults = 0,
    quint64 latchedFaults = 0,
    quint32 latestAlarmSequence = 19,
    bool safeCyclicRuntime = false)
{
    QByteArray payload(80, '\0');
    quint32 status = 0x1; // READY
    quint32 severity = 0;
    quint32 alState = 0;
    quint32 workingCounter = 0;
    if (serviceState == 3) { // OP_SAFE
        status |= 0x2 | 0x8 | 0x40; // BUS_OP | SAFE_OUTPUT | DC_LOCKED
        alState = 0x08;
        workingCounter = 6;
    } else if (serviceState == 4) { // RUNNING
        status |= 0x2 | 0x4 | 0x40; // BUS_OP | ACTIVE | DC_LOCKED
        alState = 0x08;
        workingCounter = 6;
    } else if (serviceState == 9) { // PAUSED
        status |= 0x2 | 0x40 | 0x100; // BUS_OP | DC_LOCKED | PAUSED
        alState = 0x08;
        workingCounter = 6;
    } else if (serviceState == 6) { // FAULT
        status |= 0x10;
        severity = 4;
        if (safeCyclicRuntime) {
            status |= 0x2 | 0x8 | 0x40; // BUS_OP | SAFE_OUTPUT | DC_LOCKED
            alState = 0x08;
            workingCounter = 6;
        }
    }
    putU32(payload, 0, serviceState);
    putU32(payload, 4, status);
    putU32(payload, 8, severity);
    putU64(payload, 16, currentFaults);
    putU64(payload, 24, latchedFaults);
    putU64(payload, 32, 101);
    putU64(payload, 40, cycleCount);
    putU64(payload, 48, TestBootId);
    putU32(payload, 56, alState);
    putU32(payload, 60, workingCounter);
    putU32(payload, 64, workingCounter);
    putU32(payload, 68, status & 0x40 ? 73 : 0);
    putU32(payload, 76, latestAlarmSequence);
    return payload;
}

QByteArray alarmEventPayload(
    quint32 sequence,
    quint32 code = 11,
    quint32 severity = 4,
    quint32 source = 2,
    quint32 flags = 1,
    quint32 detail0 = 8719271,
    quint32 detail1 = 8719268,
    quint32 detail2 = 8719268,
    quint64 faultMask = quint64(1) << 15)
{
    QByteArray payload(AlarmEventBytes, '\0');
    putU32(payload, 0, sequence);
    putU32(payload, 4, code);
    putU32(payload, 8, severity);
    putU32(payload, 12, source);
    putU32(payload, 16, flags);
    putU32(payload, 20, detail0);
    putU32(payload, 24, detail1);
    putU32(payload, 28, detail2);
    putU64(payload, 32, 1097337956160);
    putU64(payload, 40, 8698496);
    putU64(payload, 48, faultMask);
    return payload;
}

QByteArray packageStatePayload(
    Protocol::MessageType originalType = Protocol::MessageType::GetPackageState,
    bool controllerPackageActive = true)
{
    QByteArray payload(72, '\0');
    putU16(payload, 0, quint16(originalType));
    putI32(payload, 4, 0);
    putI32(payload, 8, 0);
    putU32(payload, 12, 'A');
    putU64(payload, 16, 11);
    putU64(payload, 24, 22);
    putU32(payload, 32, 'B');
    putU64(payload, 40, 33);
    putU64(payload, 48, 44);
    if (controllerPackageActive) {
        putU32(payload, 36, 3); // ACTIVE
        putU32(payload, 60, 5);
        putU64(payload, 64, TestBootId);
    }
    return payload;
}

QByteArray successfulCommandStatusPayload(
    Protocol::MessageType originalType,
    quint16 stage,
    quint32 serviceState,
    bool final,
    quint64 detail = 0)
{
    QByteArray payload(40, '\0');
    putU16(payload, 0, quint16(originalType));
    putU16(payload, 2, stage);
    putU32(payload, 12, serviceState);
    putU32(payload, 16, 1);
    putU64(payload, 20, 5000000000);
    putU64(payload, 28, detail);
    putU32(payload, 36, final ? 1 : 0);
    return payload;
}

QByteArray rejectedCommandStatusPayload(
    Protocol::MessageType originalType,
    quint16 stage,
    quint32 serviceState,
    qint32 status = InternalStatus,
    quint64 detail = 0,
    qint32 operationResult = -1)
{
    QByteArray payload(40, '\0');
    putU16(payload, 0, quint16(originalType));
    putU16(payload, 2, stage);
    putI32(payload, 4, status);
    putI32(payload, 8, operationResult);
    putU32(payload, 12, serviceState);
    putU64(payload, 28, detail);
    putU32(payload, 36, 1);
    return payload;
}

QByteArray topologyResultPayload()
{
    QByteArray payload(32 + 2 * 24, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::DiscoverTopology));
    putU16(payload, 2, 24);
    putU16(payload, 8, 2);
    putU16(payload, 10, 2);
    putU32(payload, 12, 7);
    putU32(payload, 20, 0x01);
    putU64(payload, 24, 123456789);

    putU16(payload, 32, 0);
    putU16(payload, 34, 0x1001);
    putU16(payload, 36, 0x08);
    putU32(payload, 40, 0x00000002);
    putU32(payload, 44, 0x12345678);
    putU32(payload, 48, 0x00000011);
    putU32(payload, 52, 0x00000021);

    putU16(payload, 56, 1);
    putU16(payload, 58, 0x1002);
    putU16(payload, 60, 0x04);
    putU32(payload, 64, 0x00000003);
    putU32(payload, 68, 0x87654321);
    putU32(payload, 72, 0x00000012);
    putU32(payload, 76, 0x00000022);
    return payload;
}

QByteArray firmwareStatePayload(Protocol::MessageType originalType)
{
    QByteArray payload(192, '\0');
    putU16(payload, 0, quint16(originalType));
    putU16(payload, 2, 7); // CONFIRMED
    putI32(payload, 4, 0);
    putI32(payload, 8, 0);
    putU32(payload, 12, (1U << 1) | (1U << 2) | (1U << 5));
    payload[16] = 'A';
    payload[17] = 'B';
    putU32(payload, 20, 1);
    putU32(payload, 24, 3);
    putU32(payload, 28, 4096);
    putU32(payload, 32, 4096);
    putU32(payload, 36, 1000);
    putU64(payload, 40, 0x1234);
    putU32(payload, 48, 7);
    putU32(payload, 52, 7);
    putU32(payload, 56, 8);
    putU32(payload, 60, 0);
    putU64(payload, 64, 71);
    putU64(payload, 72, 71);
    putU64(payload, 80, 72);
    for (int index = 0; index < 64; ++index)
        payload[88 + index] = char(index);
    putI32(payload, 152, 0);
    putI32(payload, 156, 0);
    putU64(payload, 160, 41);
    putU64(payload, 168, 1700000000);
    return payload;
}

QByteArray commandStatusPayload(quint16 originalType = quint16(Protocol::MessageType::GetState))
{
    QByteArray payload(40, '\0');
    putU16(payload, 0, originalType);
    putU16(payload, 2, 2); // CPU0 validation
    putI32(payload, 4, -16); // INTERNAL
    putI32(payload, 8, -5);
    putU32(payload, 12, 8); // SHUTDOWN
    putU64(payload, 28, CommandStatusDetail);
    putU32(payload, 36, 1);
    return payload;
}

QByteArray bulkStatusPayload(quint16 originalType = quint16(Protocol::MessageType::GetCapability))
{
    QByteArray payload(40, '\0');
    putI32(payload, 0, -20); // CAPABILITY_MISMATCH
    putI32(payload, 4, -6);
    putU16(payload, 38, originalType);
    return payload;
}

QByteArray successfulBulkStatusPayload(
    Protocol::MessageType originalType,
    Data::ControllerSlot selectedSlot = Data::ControllerSlot::None,
    quint64 generation = 0,
    quint64 configurationId = 0)
{
    QByteArray payload(40, '\0');
    if (selectedSlot != Data::ControllerSlot::None) {
        putU32(payload, 8, selectedSlot == Data::ControllerSlot::A ? quint32('A') : quint32('B'));
        putU64(payload, 12, generation);
        putU64(payload, 20, configurationId);
        putU32(payload, 28, 125000);
        putU32(payload, 32, 62500);
    }
    putU16(payload, 38, quint16(originalType));
    return payload;
}

QByteArray deploymentPackageStatePayload(
    Protocol::MessageType originalType,
    Data::ControllerPackageState state,
    Data::ControllerSlot stagedSlot,
    quint64 stagedGeneration,
    quint64 stagedConfigurationId,
    Data::ControllerSlot activeSlot,
    quint64 activeGeneration,
    quint64 activeConfigurationId)
{
    QByteArray payload(72, '\0');
    putU16(payload, 0, quint16(originalType));
    if (stagedSlot != Data::ControllerSlot::None) {
        putU32(payload, 12, stagedSlot == Data::ControllerSlot::A ? quint32('A') : quint32('B'));
        putU64(payload, 16, stagedGeneration);
        putU64(payload, 24, stagedConfigurationId);
    }
    if (activeSlot != Data::ControllerSlot::None) {
        putU32(payload, 32, activeSlot == Data::ControllerSlot::A ? quint32('A') : quint32('B'));
        putU64(payload, 40, activeGeneration);
        putU64(payload, 48, activeConfigurationId);
    }
    quint32 stateValue = 0;
    switch (state) {
    case Data::ControllerPackageState::Empty:
        stateValue = 0;
        break;
    case Data::ControllerPackageState::Staged:
        stateValue = 1;
        break;
    case Data::ControllerPackageState::Accepted:
        stateValue = 2;
        break;
    case Data::ControllerPackageState::Active:
        stateValue = 3;
        break;
    case Data::ControllerPackageState::Rejected:
        stateValue = 4;
        break;
    case Data::ControllerPackageState::Unavailable:
        break;
    }
    putU32(payload, 36, stateValue);
    putU32(payload, 60, 6);
    putU64(payload, 64, TestBootId);
    return payload;
}

QByteArray packageStateErrorPayload(
    Protocol::MessageType originalType = Protocol::MessageType::GetPackageState,
    qint32 status = -17)
{
    QByteArray payload(72, '\0');
    putU16(payload, 0, quint16(originalType));
    putI32(payload, 4, status);
    putI32(payload, 8, -3);
    return payload;
}

QByteArray firmwareStateErrorPayload()
{
    QByteArray payload = firmwareStatePayload(Protocol::MessageType::GetFirmwareState);
    putI32(payload, 4, -28); // FIRMWARE_INVALID
    putI32(payload, 8, -4);
    putU64(payload, 176, FirmwareStateDetail);
    return payload;
}

QByteArray capacityErrorPayload(bool malformed)
{
    QByteArray payload(32, '\0');
    putU16(payload,
           0,
           malformed ? quint16(Protocol::MessageType::GetState)
                     : quint16(Protocol::MessageType::Hello));
    putU16(payload, 2, 32);
    putI32(payload, 4, -26);
    putU32(payload, 8, quint32(Protocol::Role::Control));
    putU32(payload, 12, 8);
    putU32(payload, 16, 8);
    putU32(payload, 20, CapacityRetryAfterMs);
    return payload;
}

QByteArray encodedResponse(Protocol::MessageType type,
                           const QByteArray &payload,
                           quint64 sequence = 1,
                           quint32 flags = Protocol::flagValue(Protocol::Flag::Response))
{
    Protocol::Error error;
    const QByteArray wire = Protocol::encodeFrame(responseFrame(type, payload, sequence, flags),
                                                  &error);
    if (error)
        return {};
    return wire;
}

QByteArray runtimeResourceTableQueryGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000c0040040b00000000000000400102030405060708"
        "1112131415161718000000000000000221222324252627280000000000000000"
        "09165ece00000041000200010000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "00000000");
}

QByteArray runtimeResourceTablePageGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000c0040048700000005000000b0010203040506070811121314"
        "151617180000000000000005212223242526272831323334353637385acfcbd1"
        "040b007000000000000000410001004000000000000000010000000000000100"
        "0000000000000002000000000000000311112222333344440000000200000001"
        "0000000100000000212223242526272855556666777788880000000000000000"
        "0000000000000000000000000000000010000000000000012000000000000001"
        "0000000000000000000000000000000000100010040101020000000f00000001"
        "00000000000000000000000000000000");
}

QByteArray runtimeResourceSnapshotQueryGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000c0040040c0000000000000048010203040506070841424344"
        "45464748000000000000000321222324252627280000000000000000279d92a3"
        "0000004100010000000000000000000100000000000001000000000000000002"
        "0000000000000003111122223333444455556666777788880000000000000000"
        "1000000000000001");
}

QByteArray runtimeResourceSnapshotGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000c004004880000000100000090010203040506070841424344"
        "45464748000000000000000621222324252627286162636465666768cd329e8a"
        "040c007000000000000000410001002000000000000000010000000000000100"
        "0000000000000002000000000000000311112222333344440000000000001000"
        "6162636465666768212223242526272855556666777788880000000000000000"
        "0000000000000000000000000000000010000000000000010401001000000003"
        "00000000000000000000000000001234");
}

QByteArray runtimeResourceSnapshotFailureGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000c004004880000000300000070010203040506070841424344"
        "4546474800000000000000072122232425262728616263646566677075b06256"
        "040c0070ffffffef000000000000002000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000");
}

Protocol::RuntimeResourceBinding runtimeResourceBinding()
{
    Protocol::RuntimeResourceBinding binding;
    binding.bootId = 0x2122232425262728;
    binding.activeSlot = quint32('A');
    binding.packageGeneration = 1;
    binding.configurationId = 0x100;
    binding.topologyGeneration = 2;
    binding.runtimeGeneration = 3;
    binding.catalogRevision = 0x1111222233334444;
    binding.topologyIdentity = 0x5555666677778888;
    return binding;
}

Protocol::RuntimeResourceBinding loopbackRuntimeResourceBinding(
    quint64 bootId, quint64 packageGeneration, quint64 configurationId)
{
    Protocol::RuntimeResourceBinding binding;
    binding.bootId = bootId;
    binding.activeSlot = quint32('B');
    binding.packageGeneration = packageGeneration;
    binding.configurationId = configurationId;
    binding.topologyGeneration = 7;
    binding.runtimeGeneration = 8;
    binding.catalogRevision = 9;
    binding.topologyIdentity = 0x3132333435363738;
    return binding;
}

void putRuntimeResourceBinding(
    QByteArray &payload, const Protocol::RuntimeResourceBinding &binding)
{
    putU32(payload, 8, binding.activeSlot);
    putU64(payload, 16, binding.packageGeneration);
    putU64(payload, 24, binding.configurationId);
    putU64(payload, 32, binding.topologyGeneration);
    putU64(payload, 40, binding.runtimeGeneration);
    putU64(payload, 48, binding.catalogRevision);
    putU64(payload, 72, binding.bootId);
    putU64(payload, 80, binding.topologyIdentity);
}

QByteArray runtimeResourceTablePagePayload(
    const Protocol::RuntimeResourceBinding &binding,
    quint32 cursor,
    quint32 totalCount,
    quint32 pageSize)
{
    const quint16 count = quint16(std::min(pageSize, totalCount - cursor));
    const bool more = cursor + count < totalCount;
    QByteArray payload(112 + qsizetype(count) * 64, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::QueryResourceTable));
    putU16(payload, 2, 112);
    putU32(payload, 4, 0);
    putRuntimeResourceBinding(payload, binding);
    putU16(payload, 12, count);
    putU16(payload, 14, 64);
    putU32(payload, 56, totalCount);
    putU32(
        payload,
        60,
        more ? cursor + count : std::numeric_limits<quint32>::max());
    putU32(payload, 64, more ? 1 : 0);

    for (quint16 record = 0; record < count; ++record) {
        const quint32 index = cursor + record;
        const qsizetype offset = 112 + qsizetype(record) * 64;
        const bool output = index == 1;
        const quint16 bitWidth = output ? 1 : 16;
        putU64(payload, offset, 0x1000000000000001 + index);
        putU64(payload, offset + 8, 0x2000000000000001 + index);
        putU32(payload, offset + 24, index);
        putU32(payload, offset + 28, index * 16);
        putU16(payload, offset + 32, bitWidth);
        putU16(payload, offset + 34, bitWidth);
        payload[offset + 36] = char(
            output ? Protocol::RuntimeResourcePrimitive::Boolean
                   : Protocol::RuntimeResourcePrimitive::Unsigned16);
        payload[offset + 37] = char(
            output ? Protocol::RuntimeResourceDirection::Output
                   : Protocol::RuntimeResourceDirection::Input);
        payload[offset + 38] = char(
            output ? Protocol::RuntimeResourceAccess::ReadWrite
                   : Protocol::RuntimeResourceAccess::Read);
        payload[offset + 39] = char(output ? 1 : 2);
        payload[offset + 40] = char(output ? 1 : 0);
        putU16(payload, offset + 42, 0x000f);
        putU32(payload, offset + 44, index + 1);
    }
    return payload;
}

QByteArray runtimeResourceSnapshotPayload(
    const Protocol::RuntimeResourceBinding &binding, const QList<quint64> &resourceIds)
{
    QByteArray payload(112 + resourceIds.size() * 32, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::GetResourceSnapshot));
    putU16(payload, 2, 112);
    putU32(payload, 4, 0);
    putRuntimeResourceBinding(payload, binding);
    putU16(payload, 12, quint16(resourceIds.size()));
    putU16(payload, 14, 32);
    putU64(payload, 56, 0x1234);
    putU64(payload, 64, 0x5152535455565758);

    for (qsizetype record = 0; record < resourceIds.size(); ++record) {
        const quint64 resourceId = resourceIds.at(record);
        const qsizetype offset = 112 + record * 32;
        const bool output = resourceId == 0x1000000000000002;
        putU64(payload, offset, resourceId);
        payload[offset + 8] = char(
            output ? Protocol::RuntimeResourcePrimitive::Boolean
                   : Protocol::RuntimeResourcePrimitive::Unsigned16);
        payload[offset + 9] = char(
            output ? Protocol::RuntimeResourceDirection::Output
                   : Protocol::RuntimeResourceDirection::Input);
        putU16(payload, offset + 10, output ? 1 : 16);
        putU32(payload, offset + 12, quint32(Protocol::RuntimeResourceQuality::Good));
        if (output) {
            payload[offset + 31] = char(1);
        } else {
            payload[offset + 30] = char(0x12);
            payload[offset + 31] = char(0x34 + record);
        }
    }
    return payload;
}

QByteArray runtimeResourceSnapshotFailurePayload(qint32 status)
{
    QByteArray payload(112, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::GetResourceSnapshot));
    putU16(payload, 2, 112);
    putI32(payload, 4, status);
    putU16(payload, 14, 32);
    return payload;
}

class LoopbackController final : public QObject
{
    struct Peer;
    enum class RuntimeResourceFailure {
        None,
        Typed,
        BulkStatus,
        WrongSession,
        WrongBoot,
        MalformedPayload,
        WrongResponse,
        BulkStatusNonzeroOperationResult,
    };

public:
    enum class Behavior {
        Normal,
        SilentHello,
        CommandError,
        BulkError,
        PackageError,
        FirmwareError,
        CommandWrongLength,
        CommandWrongFlags,
        CommandWrongOriginalType,
        CommandWrongStage,
        CommandWrongFinal,
        BulkWrongLength,
        BulkWrongFlags,
        BulkWrongOriginalType,
        BulkReserved,
        CapacityThenSuccess,
        MalformedCapacity,
        HoldCapability,
        ControlLifecycle,
        FaultReset,
        PackageDeployment,
        RuntimeResources,
    };

    explicit LoopbackController(Behavior behavior = Behavior::Normal)
        : m_behavior(behavior)
    {
        if (m_behavior == Behavior::FaultReset) {
            m_serviceState = 6;
            m_controllerPackageActive = true;
            m_latchedFaults = quint64(1) << 15;
            m_lastAlarmSequence = TestAlarmSequence;
        } else if (m_behavior == Behavior::RuntimeResources) {
            m_serviceState = 3;
            m_controllerPackageActive = true;
        }
    }

    bool start()
    {
        m_elapsed.start();
        const std::array<Protocol::Role, 3> roles{
            Protocol::Role::Control,
            Protocol::Role::Push,
            Protocol::Role::Bulk,
        };
        for (const Protocol::Role role : roles) {
            QTcpServer &server = serverFor(role);
            connect(&server, &QTcpServer::newConnection, this, [this, role] {
                acceptConnections(role);
            });
            if (!server.listen(QHostAddress::LocalHost, 0))
                return false;
        }
        return true;
    }

    ProductApiSession::EndpointSet endpoints() const
    {
        return {
            QStringLiteral("127.0.0.1"),
            m_control.serverPort(),
            m_push.serverPort(),
            m_bulk.serverPort(),
            QStringLiteral("127.0.0.1:%1").arg(m_control.serverPort()),
        };
    }

    QStringList violations() const { return m_violations; }
    quint32 serviceState() const { return m_serviceState; }
    bool leaseOwned() const { return m_leaseOwned; }
    quint64 cycleCount() const
    {
        return 202 + (m_serviceState == 4 && m_elapsed.isValid()
                          ? quint64(m_elapsed.elapsed())
                          : 0);
    }

    int acceptCount(Protocol::Role role) const
    {
        return m_acceptCounts.at(size_t(quint32(role) - 1));
    }

    int requestCount(Protocol::MessageType type) const
    {
        return int(std::count(m_requestTypes.cbegin(), m_requestTypes.cend(), type));
    }

    quint64 lastRequestId(Protocol::MessageType type) const
    {
        for (auto found = m_requests.crbegin(); found != m_requests.crend(); ++found) {
            if (found->first == type)
                return found->second;
        }
        return 0;
    }

    QList<quint64> controlHelloRequestIds() const { return m_controlHelloRequestIds; }
    QList<qint64> controlHelloTimesMs() const { return m_controlHelloTimesMs; }
    QList<quint32> resumeAfterSequences() const { return m_resumeAfterSequences; }
    bool hasHeldRequest() const { return m_heldPeer && m_heldRequestId; }

    void rejectNextResume(qint32 status) { m_nextResumeStatus = status; }
    void rejectNextState(qint32 status) { m_nextStateStatus = status; }
    void setDefaultLeaseDurationMs(quint32 durationMs) { m_defaultLeaseDurationMs = durationMs; }
    void setHelloLeaseOwnerSessionId(quint64 sessionId)
    {
        m_helloLeaseOwnerSessionId = sessionId;
    }
    void setProtocolMinor(quint16 minor) { m_protocolMinor = minor; }
    void setFeatureBits(quint32 featureBits) { m_featureBits = featureBits; }
    void setRoleFeatureBits(Protocol::Role role, quint32 featureBits)
    {
        m_roleFeatureBits.at(size_t(quint32(role) - 1)) = featureBits;
    }
    void setBootId(quint64 bootId) { m_bootId = bootId; }
    void setRuntimeResourceCount(quint32 count) { m_runtimeResourceCount = count; }
    void setRuntimeResourcePageSize(quint32 count) { m_runtimeResourcePageSize = count; }
    void setRuntimeSnapshotDelayMs(int delayMs) { m_runtimeSnapshotDelayMs = delayMs; }
    void holdNextRuntimeSnapshot() { m_holdNextRuntimeSnapshot = true; }
    void setRuntimeActivePackage(quint64 generation, quint64 configurationId)
    {
        m_runtimePackageGeneration = generation;
        m_runtimeConfigurationId = configurationId;
    }
    void rejectNextRuntimeSnapshotTyped(qint32 status = -17)
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::Typed;
        m_nextRuntimeTypedStatus = status;
    }
    void rejectNextRuntimeSnapshotWithBulkStatus(qint32 status = -6)
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::BulkStatus;
        m_nextRuntimeBulkStatus = status;
    }
    void corruptNextRuntimeSnapshotSessionId()
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::WrongSession;
    }
    void corruptNextRuntimeSnapshotBootId()
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::WrongBoot;
    }
    void corruptNextRuntimeSnapshotPayload()
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::MalformedPayload;
    }
    void sendWrongNextRuntimeSnapshotResponse()
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::WrongResponse;
    }
    void sendNextRuntimeBulkStatusWithOperationResult()
    {
        m_nextRuntimeResourceFailure
            = RuntimeResourceFailure::BulkStatusNonzeroOperationResult;
    }
    void setFaultResetSafeCyclicRuntime(bool active)
    {
        m_faultResetSafeCyclicRuntime = active;
        m_controllerPackageActive = active;
        m_faultResetTerminalServiceState = active ? 3 : 8;
    }
    void setFaultResetTerminalServiceState(quint32 state)
    {
        m_faultResetTerminalServiceState = state;
    }
    void setFaultResetControllerPackageActive(bool active)
    {
        m_controllerPackageActive = active;
    }

    void rejectNextControl(
        Protocol::MessageType type,
        qint32 status,
        qint32 operationResult = -1,
        quint64 detail = 0,
        quint16 stage = 0)
    {
        m_rejectedControlType = type;
        m_nextControlStatus = status;
        m_nextControlOperationResult = operationResult;
        m_nextControlDetail = detail;
        m_nextControlFailureStage = stage;
    }

    void omitFaultClearedEventOnce() { m_omitFaultClearedEventOnce = true; }

    void rejectNextDeployment(
        Protocol::MessageType type,
        qint32 status,
        quint16 stage = 1,
        bool final = true)
    {
        m_rejectedDeploymentType = type;
        m_nextDeploymentStatus = status;
        m_nextDeploymentFailureStage = stage;
        m_nextDeploymentFailureFinal = final;
    }

    void rejectNextDeploymentPackageState(Protocol::MessageType type, qint32 status)
    {
        m_rejectedDeploymentPackageStateType = type;
        m_nextDeploymentPackageStateStatus = status;
    }

    void holdNextHeartbeat() { m_holdNextHeartbeat = true; }
    void holdNextRelease() { m_holdNextRelease = true; }
    void holdNextRestore() { m_holdNextRestore = true; }
    bool hasHeldHeartbeat() const { return m_heldHeartbeatPeer && m_heldHeartbeatRequestId; }
    bool hasHeldRelease() const { return m_heldReleasePeer && m_heldReleaseRequestId; }
    bool hasHeldRestore() const { return m_heldRestorePeer && m_heldRestoreRequestId; }

    void rejectHeldHeartbeat(qint32 status)
    {
        if (!hasHeldHeartbeat()) {
            m_violations.append(QStringLiteral("No held Heartbeat was available to reject."));
            return;
        }
        sendResponse(
            *m_heldHeartbeatPeer,
            Protocol::MessageType::CommandStatus,
            m_heldHeartbeatRequestId,
            rejectedCommandStatusPayload(
                Protocol::MessageType::Heartbeat, 4, m_serviceState, status),
            Protocol::Flag::Response | Protocol::Flag::Error);
        if (status == -7 || status == -8 || status == -9 || status == -11)
            m_leaseOwned = false;
        m_heldHeartbeatPeer = nullptr;
        m_heldHeartbeatRequestId = 0;
    }

    void completeHeldHeartbeat()
    {
        if (!hasHeldHeartbeat()) {
            m_violations.append(QStringLiteral("No held Heartbeat was available to complete."));
            return;
        }
        sendResponse(
            *m_heldHeartbeatPeer,
            Protocol::MessageType::CommandStatus,
            m_heldHeartbeatRequestId,
            successfulCommandStatusPayload(
                Protocol::MessageType::Heartbeat, 4, m_serviceState, true));
        m_heldHeartbeatPeer = nullptr;
        m_heldHeartbeatRequestId = 0;
    }

    void completeHeldRelease()
    {
        if (!hasHeldRelease()) {
            m_violations.append(QStringLiteral("No held ReleaseControl was available to complete."));
            return;
        }
        sendResponse(
            *m_heldReleasePeer,
            Protocol::MessageType::CommandStatus,
            m_heldReleaseRequestId,
            successfulCommandStatusPayload(
                Protocol::MessageType::ReleaseControl, 4, m_serviceState, true));
        m_heldReleasePeer = nullptr;
        m_heldReleaseRequestId = 0;
    }

    void completeHeldRestore()
    {
        if (!hasHeldRestore()) {
            m_violations.append(
                QStringLiteral("No held RestoreActivePackage was available to complete."));
            return;
        }
        m_serviceState = 3;
        m_controllerPackageActive = true;
        sendResponse(
            *m_heldRestorePeer,
            Protocol::MessageType::CommandStatus,
            m_heldRestoreRequestId,
            successfulCommandStatusPayload(
                Protocol::MessageType::RestoreActivePackage, 4, m_serviceState, false));
        sendResponse(
            *m_heldRestorePeer,
            Protocol::MessageType::PackageState,
            m_heldRestoreRequestId,
            packageStatePayload(Protocol::MessageType::RestoreActivePackage, true));
        m_heldRestorePeer = nullptr;
        m_heldRestoreRequestId = 0;
    }

    bool sendLiveAlarm(quint32 sequence)
    {
        for (auto found = m_peers.rbegin(); found != m_peers.rend(); ++found) {
            Peer *peer = found->get();
            if (peer->role == Protocol::Role::Push && peer->handshaken
                && peer->socket->state() == QAbstractSocket::ConnectedState) {
                sendResponse(
                    *peer,
                    Protocol::MessageType::AlarmRaised,
                    0,
                    alarmEventPayload(sequence),
                    Protocol::Flag::Response | Protocol::Flag::Important);
                return true;
            }
        }
        m_violations.append(QStringLiteral("No connected Push channel could send an alarm."));
        return false;
    }

    void allowCapacitySuccess()
    {
        m_allowCapacitySuccess = true;
        if (!m_capacityPeer)
            return;
        sendHelloAck(*m_capacityPeer, m_capacityRequestId);
        m_capacityPeer = nullptr;
        m_capacityRequestId = 0;
    }

    void releaseHeldResponse()
    {
        if (!hasHeldRequest())
            return;
        sendResponse(*m_heldPeer,
                     Protocol::MessageType::Capability,
                     m_heldRequestId,
                     QByteArray("late-capability"));
        m_heldPeer = nullptr;
        m_heldRequestId = 0;
    }

    void dropChannel(Protocol::Role role)
    {
        for (auto found = m_peers.rbegin(); found != m_peers.rend(); ++found) {
            Peer *peer = found->get();
            if (peer->role == role && peer->handshaken
                && peer->socket->state() == QAbstractSocket::ConnectedState) {
                peer->socket->abort();
                return;
            }
        }
        m_violations.append(QStringLiteral("No connected channel was available to drop."));
    }

private:
    struct Peer
    {
        Peer(QTcpSocket *socket, Protocol::Role role)
            : socket(socket)
            , role(role)
            , parser(role, Protocol::FrameDirection::ClientRequest)
        {}

        QTcpSocket *socket = nullptr;
        Protocol::Role role = Protocol::Role::Control;
        Protocol::FrameParser parser;
        quint64 responseSequence = 0;
        bool handshaken = false;
    };

    QTcpServer &serverFor(Protocol::Role role)
    {
        switch (role) {
        case Protocol::Role::Control:
            return m_control;
        case Protocol::Role::Push:
            return m_push;
        case Protocol::Role::Bulk:
            return m_bulk;
        }
        return m_control;
    }

    void acceptConnections(Protocol::Role role)
    {
        QTcpServer &server = serverFor(role);
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            auto peer = std::make_unique<Peer>(socket, role);
            Peer *peerPointer = peer.get();
            m_peers.push_back(std::move(peer));
            ++m_acceptCounts.at(size_t(quint32(role) - 1));
            connect(socket, &QTcpSocket::readyRead, this, [this, peerPointer] {
                processInput(*peerPointer);
            });
        }
    }

    void processInput(Peer &peer)
    {
        const Protocol::ParseResult result = peer.parser.append(peer.socket->readAll());
        if (result.error) {
            m_violations.append(
                QStringLiteral("Client frame parser rejected input: %1").arg(result.error->text));
            peer.socket->abort();
            return;
        }
        for (const Protocol::Frame &frame : result.frames)
            handleRequest(peer, frame);
    }

    void handleRequest(Peer &peer, const Protocol::Frame &frame)
    {
        m_requestTypes.append(frame.header.messageType);
        m_requests.append({frame.header.messageType, frame.header.requestId});
        if (!frame.header.requestId)
            m_violations.append(QStringLiteral("A client request used RequestId zero."));
        else if (m_requestIds.contains(frame.header.requestId))
            m_violations.append(QStringLiteral("A client RequestId was reused."));
        else
            m_requestIds.insert(frame.header.requestId);

        if (!peer.handshaken) {
            handleHello(peer, frame);
            return;
        }

        if (frame.header.flags || frame.header.sessionId != TestSessionId
            || frame.header.bootId != m_bootId || frame.header.controllerTimestampNs) {
            m_violations.append(QStringLiteral("A request used a non-canonical envelope."));
            return;
        }
        if (!Protocol::isSupportedRequest(frame.header.messageType)) {
            m_violations.append(QStringLiteral("An unsupported request was emitted."));
            return;
        }

        switch (frame.header.messageType) {
        case Protocol::MessageType::GetState:
            requireRoleAndPayload(peer, Protocol::Role::Control, frame, 0);
            sendStateResponse(peer, frame);
            return;
        case Protocol::MessageType::GetCapability:
            requireRoleAndPayload(peer, Protocol::Role::Bulk, frame, 0);
            sendCapabilityResponse(peer, frame);
            return;
        case Protocol::MessageType::GetPackageState:
            requireRoleAndPayload(peer, Protocol::Role::Control, frame, 0);
            sendPackageResponse(peer, frame);
            return;
        case Protocol::MessageType::GetFirmwareState:
            requireRoleAndPayload(peer, Protocol::Role::Control, frame, 0);
            sendFirmwareResponse(peer, frame);
            return;
        case Protocol::MessageType::ResumeEvents:
            requireRoleAndPayload(peer, Protocol::Role::Push, frame, 4);
            sendProgressResumeAndHeartbeat(peer, frame);
            return;
        case Protocol::MessageType::BulkBegin:
        case Protocol::MessageType::BulkChunk:
        case Protocol::MessageType::BulkCommit:
        case Protocol::MessageType::BulkAbort:
            handlePackageBulkRequest(peer, frame);
            return;
        case Protocol::MessageType::QueryResourceTable:
        case Protocol::MessageType::GetResourceSnapshot:
            handleRuntimeResourceRequest(peer, frame);
            return;
        case Protocol::MessageType::ValidatePackage:
        case Protocol::MessageType::ActivatePackage:
        case Protocol::MessageType::RollbackPackage:
            handlePackageCommandRequest(peer, frame);
            return;
        case Protocol::MessageType::AcquireControl:
        case Protocol::MessageType::ReleaseControl:
        case Protocol::MessageType::Start:
        case Protocol::MessageType::StartFreeRun:
        case Protocol::MessageType::StartDc:
        case Protocol::MessageType::Pause:
        case Protocol::MessageType::Resume:
        case Protocol::MessageType::ControlledStop:
        case Protocol::MessageType::ResetFault:
        case Protocol::MessageType::Heartbeat:
        case Protocol::MessageType::EnterConfigurationMode:
        case Protocol::MessageType::DiscoverTopology:
        case Protocol::MessageType::RestoreActivePackage:
            handleControlRequest(peer, frame);
            return;
        default:
            m_violations.append(QStringLiteral("An unknown client request was emitted."));
            return;
        }
    }

    Protocol::RuntimeResourceBinding currentRuntimeResourceBinding() const
    {
        return loopbackRuntimeResourceBinding(
            m_bootId, m_runtimePackageGeneration, m_runtimeConfigurationId);
    }

    void handleRuntimeResourceRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::RuntimeResources) {
            m_violations.append(
                QStringLiteral("A runtime resource request was emitted outside its test."));
            return;
        }
        if (peer.role != Protocol::Role::Bulk)
            m_violations.append(QStringLiteral("A runtime resource request used the wrong channel."));
        if (m_leaseOwned)
            m_violations.append(QStringLiteral("A runtime resource query acquired a lease."));

        const Protocol::RuntimeResourceBinding binding = currentRuntimeResourceBinding();
        if (request.header.messageType == Protocol::MessageType::QueryResourceTable) {
            if (request.payload.size() != 64
                || readU32(request.payload, 0) != binding.activeSlot
                || readU16(request.payload, 4) != 64) {
                m_violations.append(
                    QStringLiteral("A runtime resource table request was malformed."));
                return;
            }
            const quint16 flags = readU16(request.payload, 6);
            const quint32 cursor = readU32(request.payload, 48);
            if ((flags == 1
                 && (cursor || !std::all_of(
                                   request.payload.cbegin() + 8,
                                   request.payload.cbegin() + 48,
                                   [](char value) { return value == 0; })
                     || readU64(request.payload, 56)))
                || (flags == 0
                    && (cursor == 0
                        || readU64(request.payload, 8) != binding.packageGeneration
                        || readU64(request.payload, 16) != binding.configurationId
                        || readU64(request.payload, 24) != binding.topologyGeneration
                        || readU64(request.payload, 32) != binding.runtimeGeneration
                        || readU64(request.payload, 40) != binding.catalogRevision
                        || readU64(request.payload, 56) != binding.topologyIdentity))
                || flags > 1 || cursor >= m_runtimeResourceCount) {
                m_violations.append(
                    QStringLiteral("A runtime resource table cursor was malformed."));
                return;
            }

            const QByteArray payload
                = runtimeResourceTablePagePayload(
                    binding, cursor, m_runtimeResourceCount, m_runtimeResourcePageSize);
            const quint32 endCursor
                = cursor
                  + std::min(m_runtimeResourcePageSize, m_runtimeResourceCount - cursor);
            const bool more = endCursor < m_runtimeResourceCount;
            sendResponse(
                peer,
                Protocol::MessageType::ResourceTablePage,
                request.header.requestId,
                payload,
                Protocol::flagValue(Protocol::Flag::Response)
                    | (more ? Protocol::flagValue(Protocol::Flag::More) : 0));
            return;
        }

        if (request.payload.size() < 72
            || readU32(request.payload, 0) != binding.activeSlot
            || readU64(request.payload, 8) != binding.packageGeneration
            || readU64(request.payload, 16) != binding.configurationId
            || readU64(request.payload, 24) != binding.topologyGeneration
            || readU64(request.payload, 32) != binding.runtimeGeneration
            || readU64(request.payload, 40) != binding.catalogRevision
            || readU64(request.payload, 48) != binding.topologyIdentity
            || readU64(request.payload, 56)) {
            m_violations.append(
                QStringLiteral("A runtime resource snapshot request was malformed."));
            return;
        }
        const quint16 count = readU16(request.payload, 4);
        if (!count || count > 64 || request.payload.size() != 64 + qsizetype(count) * 8
            || readU16(request.payload, 6)) {
            m_violations.append(
                QStringLiteral("A runtime resource snapshot request count was malformed."));
            return;
        }
        QList<quint64> resourceIds;
        resourceIds.reserve(count);
        quint64 previous = 0;
        for (quint16 index = 0; index < count; ++index) {
            const quint64 resourceId = readU64(request.payload, 64 + qsizetype(index) * 8);
            if (!resourceId || resourceId <= previous) {
                m_violations.append(
                    QStringLiteral("Runtime resource snapshot IDs were not ordered."));
                return;
            }
            resourceIds.append(resourceId);
            previous = resourceId;
        }

        if (m_holdNextRuntimeSnapshot) {
            m_holdNextRuntimeSnapshot = false;
            return;
        }

        const RuntimeResourceFailure failure = m_nextRuntimeResourceFailure;
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::None;
        if (failure == RuntimeResourceFailure::Typed) {
            const qint32 status = m_nextRuntimeTypedStatus;
            m_nextRuntimeTypedStatus = -17;
            sendResponse(
                peer,
                Protocol::MessageType::ResourceSnapshot,
                request.header.requestId,
                runtimeResourceSnapshotFailurePayload(status),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        if (failure == RuntimeResourceFailure::BulkStatus) {
            QByteArray payload
                = bulkStatusPayload(
                    quint16(Protocol::MessageType::GetResourceSnapshot));
            putI32(payload, 0, m_nextRuntimeBulkStatus);
            putI32(payload, 4, 0);
            m_nextRuntimeBulkStatus = -6;
            sendResponse(
                peer,
                Protocol::MessageType::BulkStatus,
                request.header.requestId,
                payload,
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        if (failure == RuntimeResourceFailure::BulkStatusNonzeroOperationResult) {
            QByteArray payload
                = bulkStatusPayload(
                    quint16(Protocol::MessageType::GetResourceSnapshot));
            putI32(payload, 0, -6);
            putI32(payload, 4, -1);
            sendResponse(
                peer,
                Protocol::MessageType::BulkStatus,
                request.header.requestId,
                payload,
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        if (failure == RuntimeResourceFailure::WrongResponse) {
            sendResponse(
                peer,
                Protocol::MessageType::ResourceTablePage,
                request.header.requestId,
                {});
            return;
        }
        QByteArray snapshotPayload = runtimeResourceSnapshotPayload(binding, resourceIds);
        if (failure == RuntimeResourceFailure::MalformedPayload)
            putU32(snapshotPayload, 112 + 12, 1);
        if (failure == RuntimeResourceFailure::WrongSession
            || failure == RuntimeResourceFailure::WrongBoot) {
            Protocol::Frame frame = response(
                peer,
                Protocol::MessageType::ResourceSnapshot,
                request.header.requestId,
                snapshotPayload);
            if (failure == RuntimeResourceFailure::WrongSession)
                frame.header.sessionId = TestSessionId + 1;
            else
                frame.header.bootId = m_bootId + 1;
            const QByteArray wire = wireFor(frame);
            if (!wire.isEmpty())
                peer.socket->write(wire);
            return;
        }
        if (m_runtimeSnapshotDelayMs > 0) {
            Peer *peerPointer = &peer;
            const quint64 requestId = request.header.requestId;
            QTimer::singleShot(
                m_runtimeSnapshotDelayMs,
                this,
                [this, peerPointer, requestId, snapshotPayload] {
                    if (peerPointer->socket->state() == QAbstractSocket::ConnectedState) {
                        sendResponse(
                            *peerPointer,
                            Protocol::MessageType::ResourceSnapshot,
                            requestId,
                            snapshotPayload);
                    }
                });
            return;
        }
        sendResponse(
            peer,
            Protocol::MessageType::ResourceSnapshot,
            request.header.requestId,
            snapshotPayload);
    }

    void sendStateResponse(Peer &peer, const Protocol::Frame &request)
    {
        if (m_nextStateStatus) {
            const qint32 status = m_nextStateStatus;
            m_nextStateStatus = 0;
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                rejectedCommandStatusPayload(
                    Protocol::MessageType::GetState, 2, m_serviceState, status),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        QByteArray payload = commandStatusPayload();
        quint32 flags = Protocol::Flag::Response | Protocol::Flag::Error;
        switch (m_behavior) {
        case Behavior::CommandError:
            break;
        case Behavior::CommandWrongLength:
            payload.chop(1);
            break;
        case Behavior::CommandWrongFlags:
            flags = Protocol::flagValue(Protocol::Flag::Response);
            break;
        case Behavior::CommandWrongOriginalType:
            putU16(payload, 0, quint16(Protocol::MessageType::GetPackageState));
            break;
        case Behavior::CommandWrongStage:
            putU16(payload, 2, 0);
            break;
        case Behavior::CommandWrongFinal:
            putU32(payload, 36, 0);
            break;
        default: {
            QByteArray statePayload
                = m_behavior == Behavior::ControlLifecycle
                          || m_behavior == Behavior::FaultReset
                          || m_behavior == Behavior::PackageDeployment
                          || m_behavior == Behavior::RuntimeResources
                      ? lifecycleControllerStatePayload(
                            m_serviceState,
                            cycleCount(),
                            m_currentFaults,
                            m_latchedFaults,
                            m_lastAlarmSequence,
                            m_behavior == Behavior::FaultReset
                                && m_faultResetSafeCyclicRuntime)
                      : controllerStatePayload();
            if (m_behavior == Behavior::FaultReset && m_serviceState == 6
                && !m_faultResetSafeCyclicRuntime) {
                putU32(statePayload, 4, 0x19); // READY | SAFE_OUTPUT | FAULT
            }
            putU64(statePayload, 48, m_bootId);
            sendResponse(
                peer,
                Protocol::MessageType::ControllerState,
                request.header.requestId,
                statePayload);
            return;
        }
        }
        sendResponse(
            peer, Protocol::MessageType::CommandStatus, request.header.requestId, payload, flags);
    }

    void sendCapabilityResponse(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior == Behavior::HoldCapability) {
            m_heldPeer = &peer;
            m_heldRequestId = request.header.requestId;
            return;
        }

        QByteArray payload = bulkStatusPayload();
        quint32 flags = Protocol::Flag::Response | Protocol::Flag::Error;
        switch (m_behavior) {
        case Behavior::BulkError:
            break;
        case Behavior::BulkWrongLength:
            payload.chop(1);
            break;
        case Behavior::BulkWrongFlags:
            flags = Protocol::flagValue(Protocol::Flag::Response);
            break;
        case Behavior::BulkWrongOriginalType:
            putU16(payload, 38, 0x0300);
            break;
        case Behavior::BulkReserved:
            putU16(payload, 36, 1);
            break;
        default:
            sendResponse(peer,
                         Protocol::MessageType::Capability,
                         request.header.requestId,
                         QByteArray("opaque-vendor-capability-v1"));
            return;
        }
        sendResponse(
            peer, Protocol::MessageType::BulkStatus, request.header.requestId, payload, flags);
    }

    void sendPackageResponse(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior == Behavior::PackageError) {
            sendResponse(peer,
                         Protocol::MessageType::PackageState,
                         request.header.requestId,
                         packageStateErrorPayload(),
                         Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        QByteArray payload;
        if (m_behavior == Behavior::RuntimeResources) {
            payload = packageStatePayload();
            putU64(payload, 40, m_runtimePackageGeneration);
            putU64(payload, 48, m_runtimeConfigurationId);
            putU64(payload, 64, m_bootId);
        } else if (m_behavior == Behavior::PackageDeployment && m_deploymentActivated) {
            payload = deploymentPackageStatePayload(
                Protocol::MessageType::GetPackageState,
                Data::ControllerPackageState::Active,
                m_candidateSlot,
                m_candidateGeneration,
                m_deploymentConfigurationId,
                m_candidateSlot,
                m_candidateGeneration,
                m_deploymentConfigurationId);
        } else if (
            m_behavior == Behavior::ControlLifecycle || m_behavior == Behavior::FaultReset
            || m_behavior == Behavior::PackageDeployment) {
            payload = packageStatePayload(
                Protocol::MessageType::GetPackageState, m_controllerPackageActive);
        } else {
            payload = packageStatePayload();
        }
        sendResponse(peer,
                     Protocol::MessageType::PackageState,
                     request.header.requestId, payload);
    }

    void sendFirmwareResponse(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior == Behavior::FirmwareError) {
            sendResponse(peer,
                         Protocol::MessageType::FirmwareState,
                         request.header.requestId,
                         firmwareStateErrorPayload(),
                         Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        sendResponse(peer,
                     Protocol::MessageType::FirmwareState,
                     request.header.requestId,
                     firmwareStatePayload(Protocol::MessageType::GetFirmwareState));
    }

    void sendCommandStages(
        Peer &peer, const Protocol::Frame &request, bool terminalCommandStatus)
    {
        for (quint16 stage = 1; stage <= 4; ++stage) {
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                successfulCommandStatusPayload(
                    request.header.messageType,
                    stage,
                    m_serviceState,
                    terminalCommandStatus && stage == 4));
        }
    }

    void sendLeaseCommandStatus(Peer &peer, const Protocol::Frame &request)
    {
        sendResponse(
            peer,
            Protocol::MessageType::CommandStatus,
            request.header.requestId,
            successfulCommandStatusPayload(
                request.header.messageType, 4, m_serviceState, true));
    }

    void handlePackageBulkRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::PackageDeployment) {
            m_violations.append(
                QStringLiteral("A package bulk request was emitted outside its test."));
            return;
        }
        if (peer.role != Protocol::Role::Bulk)
            m_violations.append(QStringLiteral("A package upload used the wrong channel."));
        if (!m_leaseOwned || m_serviceState != 8)
            m_violations.append(QStringLiteral("A package upload violated its preconditions."));
        if (m_nextDeploymentStatus && request.header.messageType == m_rejectedDeploymentType) {
            QByteArray payload = successfulBulkStatusPayload(request.header.messageType);
            putI32(payload, 0, m_nextDeploymentStatus);
            putI32(payload, 4, -1);
            m_nextDeploymentStatus = 0;
            m_rejectedDeploymentType = Protocol::MessageType::Error;
            m_nextDeploymentFailureStage = 1;
            m_nextDeploymentFailureFinal = true;
            sendResponse(
                peer,
                Protocol::MessageType::BulkStatus,
                request.header.requestId,
                payload,
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }

        switch (request.header.messageType) {
        case Protocol::MessageType::BulkBegin:
            if (request.payload.size() != 24 || !readU64(request.payload, 0)
                || !readU32(request.payload, 8) || readU32(request.payload, 12)
                || readU32(request.payload, 16)
                || readU32(request.payload, 20) != Protocol::PackageUploadMode) {
                m_violations.append(QStringLiteral("BulkBegin payload was invalid."));
            }
            m_deploymentConfigurationId = readU64(request.payload, 0);
            m_expectedPackageBytes = readU32(request.payload, 8);
            m_uploadedPackage.clear();
            break;
        case Protocol::MessageType::BulkChunk:
            if (request.payload.size() <= qsizetype(Protocol::BulkChunkHeaderBytes)
                || readU32(request.payload, 0) != Protocol::PackageObjectKind
                || readU32(request.payload, 4) != quint32(m_uploadedPackage.size())) {
                m_violations.append(QStringLiteral("BulkChunk offset or header was invalid."));
            } else {
                m_uploadedPackage.append(request.payload.sliced(Protocol::BulkChunkHeaderBytes));
            }
            break;
        case Protocol::MessageType::BulkCommit:
            if (!request.payload.isEmpty()
                || m_uploadedPackage.size() != qsizetype(m_expectedPackageBytes)) {
                m_violations.append(QStringLiteral("BulkCommit did not cover the full package."));
            }
            break;
        case Protocol::MessageType::BulkAbort:
            if (!request.payload.isEmpty())
                m_violations.append(QStringLiteral("BulkAbort payload was not empty."));
            m_uploadedPackage.clear();
            break;
        default:
            return;
        }

        const bool commit = request.header.messageType == Protocol::MessageType::BulkCommit;
        sendResponse(
            peer,
            Protocol::MessageType::BulkStatus,
            request.header.requestId,
            successfulBulkStatusPayload(
                request.header.messageType,
                commit ? m_candidateSlot : Data::ControllerSlot::None,
                commit ? m_candidateGeneration : 0,
                commit ? m_deploymentConfigurationId : 0));
    }

    void handlePackageCommandRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::PackageDeployment) {
            m_violations.append(QStringLiteral("A package command was emitted outside its test."));
            return;
        }
        requireRoleAndPayload(peer, Protocol::Role::Control, request, 24);
        const bool rollback = request.header.messageType == Protocol::MessageType::RollbackPackage;
        const quint32 expectedSlot = rollback                                     ? quint32('B')
                                     : m_candidateSlot == Data::ControllerSlot::A ? quint32('A')
                                                                                  : quint32('B');
        const quint64 expectedGeneration = rollback ? 33 : m_candidateGeneration;
        const quint64 expectedConfigurationId = rollback ? 44 : m_deploymentConfigurationId;
        if (!m_leaseOwned || readU32(request.payload, 0) != expectedSlot
            || readU32(request.payload, 4) || readU64(request.payload, 8) != expectedGeneration
            || readU64(request.payload, 16) != expectedConfigurationId) {
            m_violations.append(QStringLiteral("The package command selector was not exact."));
        }
        if (m_nextDeploymentStatus && request.header.messageType == m_rejectedDeploymentType) {
            const qint32 status = m_nextDeploymentStatus;
            const quint16 stage = m_nextDeploymentFailureStage;
            const bool final = m_nextDeploymentFailureFinal;
            m_nextDeploymentStatus = 0;
            m_rejectedDeploymentType = Protocol::MessageType::Error;
            m_nextDeploymentFailureStage = 1;
            m_nextDeploymentFailureFinal = true;
            QByteArray payload = rejectedCommandStatusPayload(
                request.header.messageType, stage, m_serviceState, status);
            putU32(payload, 36, final ? 1 : 0);
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                payload,
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }

        sendCommandStages(peer, request, false);
        if (m_nextDeploymentPackageStateStatus
            && request.header.messageType == m_rejectedDeploymentPackageStateType) {
            const qint32 status = m_nextDeploymentPackageStateStatus;
            m_nextDeploymentPackageStateStatus = 0;
            m_rejectedDeploymentPackageStateType = Protocol::MessageType::Error;
            sendResponse(
                peer,
                Protocol::MessageType::PackageState,
                request.header.requestId,
                packageStateErrorPayload(request.header.messageType, status),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        Data::ControllerPackageState state = Data::ControllerPackageState::Accepted;
        Data::ControllerSlot activeSlot = Data::ControllerSlot::B;
        quint64 activeGeneration = 33;
        quint64 activeConfigurationId = 44;
        if (request.header.messageType == Protocol::MessageType::ActivatePackage) {
            state = Data::ControllerPackageState::Active;
            activeSlot = m_candidateSlot;
            activeGeneration = m_candidateGeneration;
            activeConfigurationId = m_deploymentConfigurationId;
            m_controllerPackageActive = true;
            m_deploymentActivated = true;
            m_serviceState = 3;
        } else if (request.header.messageType == Protocol::MessageType::RollbackPackage) {
            state = Data::ControllerPackageState::Active;
            m_controllerPackageActive = true;
            m_serviceState = 3;
        }
        sendResponse(
            peer,
            Protocol::MessageType::PackageState,
            request.header.requestId,
            deploymentPackageStatePayload(
                request.header.messageType,
                state,
                m_candidateSlot,
                m_candidateGeneration,
                m_deploymentConfigurationId,
                activeSlot,
                activeGeneration,
                activeConfigurationId));
    }

    void handleControlRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::ControlLifecycle && m_behavior != Behavior::FaultReset
            && m_behavior != Behavior::PackageDeployment
            && m_behavior != Behavior::RuntimeResources) {
            m_violations.append(
                QStringLiteral("A control request was emitted outside the lifecycle test."));
            return;
        }
        requireRoleAndPayload(
            peer,
            Protocol::Role::Control,
            request,
            request.header.messageType == Protocol::MessageType::AcquireControl ? 4
            : request.header.messageType == Protocol::MessageType::ResetFault  ? 16
            : request.header.messageType == Protocol::MessageType::DiscoverTopology
                ? 8
            : request.header.messageType == Protocol::MessageType::RestoreActivePackage
                ? 24
                : 0);

        if (m_nextControlStatus && request.header.messageType == m_rejectedControlType) {
            const qint32 status = m_nextControlStatus;
            const qint32 operationResult = m_nextControlOperationResult;
            const quint64 detail = m_nextControlDetail;
            m_nextControlStatus = 0;
            m_nextControlOperationResult = -1;
            m_nextControlDetail = 0;
            m_rejectedControlType = Protocol::MessageType::Error;
            const quint16 stage
                = m_nextControlFailureStage
                      ? m_nextControlFailureStage
                  : request.header.messageType == Protocol::MessageType::AcquireControl
                          || request.header.messageType == Protocol::MessageType::ReleaseControl
                      ? 4
                      : 1;
            m_nextControlFailureStage = 0;
            const quint16 firstStage
                = request.header.messageType == Protocol::MessageType::AcquireControl
                          || request.header.messageType == Protocol::MessageType::ReleaseControl
                      ? 4
                      : 1;
            for (quint16 completedStage = firstStage; completedStage < stage; ++completedStage) {
                sendResponse(
                    peer,
                    Protocol::MessageType::CommandStatus,
                    request.header.requestId,
                    successfulCommandStatusPayload(
                        request.header.messageType,
                        completedStage,
                        m_serviceState,
                        false));
            }
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                rejectedCommandStatusPayload(
                    request.header.messageType,
                    stage,
                    m_serviceState,
                    status,
                    detail,
                    operationResult),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }

        switch (request.header.messageType) {
        case Protocol::MessageType::AcquireControl:
            if (readU32(request.payload, 0) != 30000)
                m_violations.append(QStringLiteral("AcquireControl duration was not 30000 ms."));
            m_leaseOwned = true;
            sendLeaseCommandStatus(peer, request);
            return;
        case Protocol::MessageType::Heartbeat:
            if (!m_leaseOwned)
                m_violations.append(QStringLiteral("Heartbeat was sent without a control lease."));
            if (m_holdNextHeartbeat) {
                m_holdNextHeartbeat = false;
                m_heldHeartbeatPeer = &peer;
                m_heldHeartbeatRequestId = request.header.requestId;
                return;
            }
            sendLeaseCommandStatus(peer, request);
            return;
        case Protocol::MessageType::ReleaseControl:
            if (!m_leaseOwned)
                m_violations.append(QStringLiteral("ReleaseControl was sent without ownership."));
            m_leaseOwned = false;
            if (m_holdNextRelease) {
                m_holdNextRelease = false;
                m_heldReleasePeer = &peer;
                m_heldReleaseRequestId = request.header.requestId;
                return;
            }
            sendLeaseCommandStatus(peer, request);
            return;
        case Protocol::MessageType::ResetFault: {
            if (!m_leaseOwned || m_serviceState != 6 || m_currentFaults
                || !m_latchedFaults || readU64(request.payload, 0) != m_latchedFaults
                || readU32(request.payload, 8) != m_lastAlarmSequence
                || readU32(request.payload, 12)) {
                m_violations.append(QStringLiteral("ResetFault confirmation was not exact."));
            }
            const quint64 confirmedFaults = m_latchedFaults;
            const quint32 clearedSequence
                = m_lastAlarmSequence == std::numeric_limits<quint32>::max()
                      ? 1
                      : m_lastAlarmSequence + 1;
            m_currentFaults = 0;
            m_latchedFaults = 0;
            m_lastAlarmSequence = clearedSequence;
            m_serviceState = m_faultResetTerminalServiceState;
            m_faultClearedMask = confirmedFaults;
            m_faultClearedEventSequence = clearedSequence;
            m_faultClearedEventAvailable = !m_omitFaultClearedEventOnce;
            m_omitFaultClearedEventOnce = false;
            for (quint16 stage = 1; stage <= 4; ++stage) {
                sendResponse(
                    peer,
                    Protocol::MessageType::CommandStatus,
                    request.header.requestId,
                    successfulCommandStatusPayload(
                        request.header.messageType,
                        stage,
                        m_serviceState,
                        stage == 4,
                        stage == 4 ? clearedSequence : 0));
            }
            return;
        }
        case Protocol::MessageType::EnterConfigurationMode:
            if (!m_leaseOwned)
                m_violations.append(QStringLiteral("Configuration mode requires the lease."));
            m_serviceState = 8;
            m_controllerPackageActive = false;
            sendCommandStages(peer, request, true);
            return;
        case Protocol::MessageType::DiscoverTopology:
            if (!m_leaseOwned || m_serviceState != 8 || m_controllerPackageActive
                || readU32(request.payload, 4) != 0
                || readU32(request.payload, 0) != 0x10010040) {
                m_violations.append(QStringLiteral("DiscoverTopology preconditions were invalid."));
            }
            sendCommandStages(peer, request, false);
            sendResponse(
                peer,
                Protocol::MessageType::TopologyResult,
                request.header.requestId,
                topologyResultPayload());
            return;
        case Protocol::MessageType::RestoreActivePackage:
            if (!m_leaseOwned || readU32(request.payload, 0) != quint32('B')
                || readU32(request.payload, 4) || readU64(request.payload, 8) != 33
                || readU64(request.payload, 16) != 44) {
                m_violations.append(
                    QStringLiteral("RestoreActivePackage selector was not exact."));
            }
            if (m_holdNextRestore) {
                m_holdNextRestore = false;
                m_serviceState = 2;
                for (quint16 stage = 1; stage <= 3; ++stage) {
                    sendResponse(
                        peer,
                        Protocol::MessageType::CommandStatus,
                        request.header.requestId,
                        successfulCommandStatusPayload(
                            request.header.messageType, stage, m_serviceState, false));
                }
                m_heldRestorePeer = &peer;
                m_heldRestoreRequestId = request.header.requestId;
                return;
            }
            m_serviceState = 3;
            m_controllerPackageActive = true;
            sendCommandStages(peer, request, false);
            sendResponse(
                peer,
                Protocol::MessageType::PackageState,
                request.header.requestId,
                packageStatePayload(Protocol::MessageType::RestoreActivePackage, true));
            return;
        case Protocol::MessageType::Start:
        case Protocol::MessageType::StartFreeRun:
            if (!m_leaseOwned || m_serviceState != 3 || !m_controllerPackageActive)
                m_violations.append(QStringLiteral("Start preconditions were invalid."));
            m_serviceState = 4;
            sendCommandStages(peer, request, true);
            return;
        case Protocol::MessageType::StartDc:
            if (!m_leaseOwned || m_serviceState != 3 || !m_controllerPackageActive)
                m_violations.append(QStringLiteral("StartDc preconditions were invalid."));
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                successfulCommandStatusPayload(
                    request.header.messageType, 1, m_serviceState, false));
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                rejectedCommandStatusPayload(
                    request.header.messageType,
                    2,
                    m_serviceState,
                    -35,
                    (quint64(2) << 32) | 1),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        case Protocol::MessageType::ControlledStop:
            if (!m_leaseOwned || (m_serviceState != 4 && m_serviceState != 9)
                || !m_controllerPackageActive) {
                m_violations.append(QStringLiteral("ControlledStop preconditions were invalid."));
            }
            m_serviceState = 3;
            sendCommandStages(peer, request, true);
            return;
        case Protocol::MessageType::Pause:
            if (!m_leaseOwned || m_serviceState != 4 || !m_controllerPackageActive)
                m_violations.append(QStringLiteral("Pause preconditions were invalid."));
            m_serviceState = 9;
            sendCommandStages(peer, request, true);
            return;
        case Protocol::MessageType::Resume:
            if (!m_leaseOwned || m_serviceState != 9 || !m_controllerPackageActive)
                m_violations.append(QStringLiteral("Resume preconditions were invalid."));
            m_serviceState = 4;
            sendCommandStages(peer, request, true);
            return;
        default:
            return;
        }
    }

    void handleHello(Peer &peer, const Protocol::Frame &frame)
    {
        if (frame.header.messageType != Protocol::MessageType::Hello
            || frame.header.flags || frame.header.sessionId || frame.header.bootId
            || frame.header.controllerTimestampNs || frame.payload.size() != 24
            || readU32(frame.payload, 0) != quint32(peer.role)
            || readU32(frame.payload, 4)) {
            m_violations.append(QStringLiteral("A channel sent an invalid HELLO."));
            return;
        }

        const quint64 resumeSessionId = readU64(frame.payload, 8);
        const bool validResume
            = peer.role == Protocol::Role::Control
                  ? (resumeSessionId == 0 || resumeSessionId == TestSessionId)
                  : resumeSessionId == TestSessionId;
        if (!validResume) {
            m_violations.append(QStringLiteral("HELLO used an invalid resume SessionId."));
            return;
        }
        if (peer.role == Protocol::Role::Control) {
            m_controlHelloRequestIds.append(frame.header.requestId);
            m_controlHelloTimesMs.append(m_elapsed.elapsed());
            if (m_behavior == Behavior::CapacityThenSuccess
                && m_controlHelloTimesMs.size() > 1
                && m_controlHelloTimesMs.constLast() - m_controlHelloTimesMs.constFirst()
                       < CapacityRetryAfterMs) {
                m_violations.append(
                    QStringLiteral("SESSION_CAPACITY retry occurred before retryAfterMs."));
            }
        }
        if (m_behavior == Behavior::SilentHello)
            return;
        if (peer.role == Protocol::Role::Control && m_controlHelloRequestIds.size() == 1
            && (m_behavior == Behavior::CapacityThenSuccess
                || m_behavior == Behavior::MalformedCapacity)) {
            sendCapacityError(peer,
                              frame.header.requestId,
                              m_behavior == Behavior::MalformedCapacity);
            return;
        }
        if (peer.role == Protocol::Role::Control
            && m_behavior == Behavior::CapacityThenSuccess && !m_allowCapacitySuccess) {
            m_capacityPeer = &peer;
            m_capacityRequestId = frame.header.requestId;
            return;
        }
        sendHelloAck(peer, frame.header.requestId);
    }

    void sendHelloAck(Peer &peer, quint64 requestId)
    {
        QByteArray payload(40, '\0');
        putU32(payload, 0, quint32(peer.role));
        putU32(payload, 4, Protocol::maximumPayloadBytes(peer.role));
        putU64(payload, 8, TestSessionId);
        putU64(payload, 16, m_bootId);
        putU64(
            payload,
            24,
            m_helloLeaseOwnerSessionId
                ? m_helloLeaseOwnerSessionId
                : (m_leaseOwned ? TestSessionId : 0));
        const quint32 defaultFeatureBits
            = m_protocolMinor >= Protocol::RuntimeResourceMinor
                  ? 0x3fff
              : m_protocolMinor >= Protocol::ControlledFaultResetMinor
                  ? 0x1fff
                  : m_protocolMinor >= Protocol::ExplicitTimingModeMinor ? 0x0fff : 0x07ff;
        const std::optional<quint32> roleFeatureBits
            = m_roleFeatureBits.at(size_t(quint32(peer.role) - 1));
        putU32(
            payload,
            32,
            roleFeatureBits.value_or(m_featureBits.value_or(defaultFeatureBits)));
        putU32(payload, 36, m_defaultLeaseDurationMs);
        peer.handshaken = true;
        Protocol::Frame frame
            = response(peer, Protocol::MessageType::HelloAck, requestId, payload);
        frame.header.protocolMinor = m_protocolMinor;
        const QByteArray wire = wireFor(frame);
        if (!wire.isEmpty())
            peer.socket->write(wire);
        if (peer.parser.negotiatedMinor() != std::optional<quint16>(m_protocolMinor))
            peer.parser.reset();
    }

    void sendCapacityError(Peer &peer, quint64 requestId, bool malformed)
    {
        Protocol::Frame frame = response(
            peer,
            Protocol::MessageType::Error,
            requestId,
            capacityErrorPayload(malformed),
            Protocol::Flag::Response | Protocol::Flag::Error);
        frame.header.sessionId = 0;
        const QByteArray wire = wireFor(frame);
        if (!wire.isEmpty())
            peer.socket->write(wire);
    }

    void requireRoleAndPayload(Peer &peer,
                               Protocol::Role role,
                               const Protocol::Frame &frame,
                               qsizetype payloadBytes)
    {
        if (peer.role != role)
            m_violations.append(QStringLiteral("A request used the wrong channel."));
        if (frame.payload.size() != payloadBytes)
            m_violations.append(QStringLiteral("A request used the wrong payload length."));
    }

    Protocol::Frame response(Peer &peer,
                             Protocol::MessageType type,
                             quint64 requestId,
                             const QByteArray &payload,
                             quint32 flags = Protocol::flagValue(Protocol::Flag::Response))
    {
        Protocol::Frame frame;
        frame.header.protocolMinor = m_protocolMinor;
        frame.header.messageType = type;
        frame.header.flags = flags;
        frame.header.payloadLength = quint32(payload.size());
        frame.header.sessionId = TestSessionId;
        frame.header.requestId = requestId;
        frame.header.sequence = ++peer.responseSequence;
        frame.header.bootId = m_bootId;
        frame.header.controllerTimestampNs = 900000 + peer.responseSequence;
        frame.payload = payload;
        return frame;
    }

    QByteArray wireFor(const Protocol::Frame &frame)
    {
        Protocol::Error error;
        const QByteArray wire = Protocol::encodeFrame(frame, &error);
        if (error)
            m_violations.append(QStringLiteral("A server response could not be encoded."));
        return wire;
    }

    void sendResponse(Peer &peer,
                      Protocol::MessageType type,
                      quint64 requestId,
                      const QByteArray &payload,
                      quint32 flags = Protocol::flagValue(Protocol::Flag::Response))
    {
        const QByteArray wire = wireFor(response(peer, type, requestId, payload, flags));
        if (!wire.isEmpty())
            peer.socket->write(wire);
    }

    void sendProgressResumeAndHeartbeat(Peer &peer, const Protocol::Frame &request)
    {
        const quint32 requestedAfterSequence = readU32(request.payload, 0);
        m_resumeAfterSequences.append(requestedAfterSequence);
        const qint32 resumeStatus = m_nextResumeStatus;
        m_nextResumeStatus = 0;
        const bool replayFaultCleared
            = !resumeStatus && m_behavior == Behavior::FaultReset && requestedAfterSequence
              && m_faultClearedEventAvailable
              && requestedAfterSequence != m_faultClearedEventSequence;

        Protocol::Frame progress = response(
            peer,
            Protocol::MessageType::FirmwareProgress,
            0,
            firmwareStatePayload(Protocol::MessageType::FirmwareProgress),
            Protocol::Flag::Response | Protocol::Flag::Replaceable);

        QByteArray resumePayload(32, '\0');
        putI32(resumePayload, 0, resumeStatus);
        putU32(resumePayload, 4, requestedAfterSequence);
        if (resumeStatus == InternalStatus) {
            putU32(resumePayload, 8, TestAlarmSequence);
            putU32(resumePayload, 12, TestAlarmSequence);
        } else if (!resumeStatus && !requestedAfterSequence) {
            putU32(resumePayload, 8, TestAlarmSequence);
            putU32(resumePayload, 12, TestAlarmSequence);
            putU32(resumePayload, 16, 1);
        } else if (replayFaultCleared) {
            putU32(resumePayload, 8, TestAlarmSequence);
            putU32(resumePayload, 12, m_faultClearedEventSequence);
            putU32(resumePayload, 16, 1);
        } else if (!resumeStatus && requestedAfterSequence) {
            putU32(resumePayload, 8, requestedAfterSequence);
            putU32(resumePayload, 12, requestedAfterSequence);
        }
        putU32(resumePayload, 20, 32);
        const Protocol::Frame resume = response(peer,
                                                Protocol::MessageType::ResumeEventsResult,
                                                request.header.requestId,
                                                resumePayload,
                                                resumeStatus
                                                    ? Protocol::Flag::Response
                                                          | Protocol::Flag::Error
                                                    : !requestedAfterSequence
                                                              || replayFaultCleared
                                                          ? Protocol::Flag::Response
                                                                | Protocol::Flag::More
                                                          : Protocol::flagValue(
                                                                Protocol::Flag::Response));

        QByteArray heartbeatPayload(16, '\0');
        putU64(heartbeatPayload, 0, 77);
        putU64(heartbeatPayload, 8, m_bootId);
        const Protocol::Frame heartbeat = response(
            peer,
            Protocol::MessageType::PushHeartbeat,
            0,
            heartbeatPayload,
            Protocol::Flag::Response | Protocol::Flag::Replaceable);

        QByteArray coalesced = wireFor(progress) + wireFor(resume) + wireFor(heartbeat);
        if (!resumeStatus && !requestedAfterSequence) {
            coalesced += wireFor(response(
                peer,
                Protocol::MessageType::AlarmRaised,
                request.header.requestId,
                alarmEventPayload(TestAlarmSequence),
                Protocol::Flag::Response | Protocol::Flag::Important));
        } else if (replayFaultCleared) {
            coalesced += wireFor(response(
                peer,
                Protocol::MessageType::AlarmCleared,
                request.header.requestId,
                alarmEventPayload(
                    m_faultClearedEventSequence,
                    5,
                    1,
                    1,
                    2,
                    0,
                    0,
                    0,
                    m_faultClearedMask),
                Protocol::Flag::Response | Protocol::Flag::Important));
            m_faultClearedEventAvailable = false;
        }
        if (!coalesced.isEmpty())
            peer.socket->write(coalesced);
    }

    QTcpServer m_control;
    QTcpServer m_push;
    QTcpServer m_bulk;
    std::vector<std::unique_ptr<Peer>> m_peers;
    std::array<int, 3> m_acceptCounts{0, 0, 0};
    QList<Protocol::MessageType> m_requestTypes;
    QList<QPair<Protocol::MessageType, quint64>> m_requests;
    QSet<quint64> m_requestIds;
    QList<quint64> m_controlHelloRequestIds;
    QList<qint64> m_controlHelloTimesMs;
    QList<quint32> m_resumeAfterSequences;
    QStringList m_violations;
    QElapsedTimer m_elapsed;
    Peer *m_heldPeer = nullptr;
    quint64 m_heldRequestId = 0;
    Peer *m_capacityPeer = nullptr;
    quint64 m_capacityRequestId = 0;
    Peer *m_heldHeartbeatPeer = nullptr;
    quint64 m_heldHeartbeatRequestId = 0;
    Peer *m_heldReleasePeer = nullptr;
    quint64 m_heldReleaseRequestId = 0;
    Peer *m_heldRestorePeer = nullptr;
    quint64 m_heldRestoreRequestId = 0;
    bool m_allowCapacitySuccess = false;
    bool m_holdNextHeartbeat = false;
    bool m_holdNextRelease = false;
    bool m_holdNextRestore = false;
    qint32 m_nextResumeStatus = 0;
    qint32 m_nextStateStatus = 0;
    qint32 m_nextControlStatus = 0;
    qint32 m_nextControlOperationResult = -1;
    quint64 m_nextControlDetail = 0;
    quint16 m_nextControlFailureStage = 0;
    qint32 m_nextDeploymentStatus = 0;
    qint32 m_nextDeploymentPackageStateStatus = 0;
    quint16 m_nextDeploymentFailureStage = 1;
    bool m_nextDeploymentFailureFinal = true;
    quint32 m_defaultLeaseDurationMs = 5000;
    quint64 m_helloLeaseOwnerSessionId = 0;
    quint64 m_bootId = TestBootId;
    quint16 m_protocolMinor = Protocol::CurrentMinor;
    std::optional<quint32> m_featureBits;
    std::array<std::optional<quint32>, 3> m_roleFeatureBits;
    RuntimeResourceFailure m_nextRuntimeResourceFailure = RuntimeResourceFailure::None;
    qint32 m_nextRuntimeTypedStatus = -17;
    qint32 m_nextRuntimeBulkStatus = -6;
    quint32 m_runtimeResourceCount = 2;
    quint32 m_runtimeResourcePageSize = 64;
    int m_runtimeSnapshotDelayMs = 0;
    bool m_holdNextRuntimeSnapshot = false;
    quint64 m_runtimePackageGeneration = 33;
    quint64 m_runtimeConfigurationId = 44;
    Protocol::MessageType m_rejectedControlType = Protocol::MessageType::Error;
    Protocol::MessageType m_rejectedDeploymentType = Protocol::MessageType::Error;
    Protocol::MessageType m_rejectedDeploymentPackageStateType = Protocol::MessageType::Error;
    Behavior m_behavior = Behavior::Normal;
    quint32 m_serviceState = 8;
    quint64 m_currentFaults = 0;
    quint64 m_latchedFaults = 0;
    quint32 m_lastAlarmSequence = 19;
    quint64 m_faultClearedMask = 0;
    quint32 m_faultClearedEventSequence = 0;
    quint32 m_faultResetTerminalServiceState = 3;
    bool m_faultResetSafeCyclicRuntime = true;
    bool m_controllerPackageActive = false;
    bool m_leaseOwned = false;
    bool m_deploymentActivated = false;
    bool m_faultClearedEventAvailable = false;
    bool m_omitFaultClearedEventOnce = false;
    Data::ControllerSlot m_candidateSlot = Data::ControllerSlot::A;
    quint64 m_candidateGeneration = 55;
    quint64 m_deploymentConfigurationId = 0;
    quint32 m_expectedPackageBytes = 0;
    QByteArray m_uploadedPackage;
};

ProductApiSession::Options testOptions()
{
    ProductApiSession::Options options;
    options.connectTimeoutMs = 500;
    options.handshakeTimeoutMs = 500;
    options.requestTimeoutMs = 500;
    options.reconnectInitialDelayMs = 20;
    options.reconnectMaximumDelayMs = 20;
    options.reconnectAttempts = 2;
    options.liveStatePollIntervalMs = 0;
    return options;
}

Data::ControllerConnectionRequest requestFor(ProductApiConnectionProvider &provider)
{
    const Data::ControllerConnectionScope scope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    const QList<Data::ControllerConnectionProfile> profiles
        = provider.connectionProfiles(scope);
    if (profiles.isEmpty())
        return {};
    return {scope, profiles.constFirst().id};
}

template<typename Predicate>
bool waitForHardwareCondition(Predicate &&predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    return predicate();
}

QString hardwareServiceStateName(Data::ControllerServiceState state)
{
    using State = Data::ControllerServiceState;
    switch (state) {
    case State::Unknown:
        return QStringLiteral("UNKNOWN");
    case State::Boot:
        return QStringLiteral("BOOT");
    case State::Configuring:
        return QStringLiteral("CONFIGURING");
    case State::SafeOperational:
        return QStringLiteral("SAFE_OPERATIONAL");
    case State::OperationalSafe:
        return QStringLiteral("OP_SAFE");
    case State::Running:
        return QStringLiteral("RUNNING");
    case State::Stopping:
        return QStringLiteral("STOPPING");
    case State::Fault:
        return QStringLiteral("FAULT");
    case State::Recovering:
        return QStringLiteral("RECOVERING");
    case State::Shutdown:
        return QStringLiteral("SHUTDOWN");
    case State::Paused:
        return QStringLiteral("PAUSED");
    }
    return QStringLiteral("INVALID");
}

QString hardwareCommandName(Data::ControllerControlCommand command)
{
    using Command = Data::ControllerControlCommand;
    switch (command) {
    case Command::None:
        return QStringLiteral("None");
    case Command::AcquireControl:
        return QStringLiteral("AcquireControl");
    case Command::ReleaseControl:
        return QStringLiteral("ReleaseControl");
    case Command::EnterConfigurationMode:
        return QStringLiteral("EnterConfigurationMode");
    case Command::DiscoverTopology:
        return QStringLiteral("DiscoverTopology");
    case Command::RestoreActivePackage:
        return QStringLiteral("RestoreActivePackage");
    case Command::Start:
        return QStringLiteral("Start");
    case Command::StartFreeRun:
        return QStringLiteral("StartFreeRun");
    case Command::StartDistributedClocks:
        return QStringLiteral("StartDc");
    case Command::Pause:
        return QStringLiteral("Pause");
    case Command::Resume:
        return QStringLiteral("Resume");
    case Command::ControlledStop:
        return QStringLiteral("ControlledStop");
    case Command::ResetFault:
        return QStringLiteral("ResetFault");
    }
    return QStringLiteral("Invalid");
}

QString hardwareSlotName(Data::ControllerSlot slot)
{
    switch (slot) {
    case Data::ControllerSlot::None:
        return QStringLiteral("none");
    case Data::ControllerSlot::A:
        return QStringLiteral("A");
    case Data::ControllerSlot::B:
        return QStringLiteral("B");
    }
    return QStringLiteral("invalid");
}

void logHardwareSnapshot(const QString &label, const Data::ControllerConnectionSnapshot &snapshot)
{
    qInfo().noquote() << "[Product API hardware]" << label << "connection=" << int(snapshot.state)
                      << "protocol="
                      << QStringLiteral("%1.%2")
                             .arg(snapshot.protocolVersion.major)
                             .arg(snapshot.protocolVersion.minor)
                      << "readOnly=" << snapshot.readOnly;
    if (snapshot.session) {
        qInfo().noquote() << "[Product API hardware]" << label
                          << "session=" << QString::number(snapshot.session->sessionId)
                          << "boot=" << QString::number(snapshot.session->bootId) << "leaseOwner="
                          << QString::number(snapshot.session->controlLeaseOwnerSessionId)
                          << "ownsLease=" << snapshot.session->ownsControlLease;
    }
    if (snapshot.controllerState) {
        const Data::ControllerStateSummary &state = *snapshot.controllerState;
        qInfo().noquote() << "[Product API hardware]" << label
                          << "service=" << hardwareServiceStateName(state.serviceState)
                          << "ready=" << state.ready << "busOp=" << state.busOperational
                          << "applicationActive=" << state.applicationActive
                          << "safeOutput=" << state.safeOutput
                          << "AL=" << QString::number(state.ethercatAlStateBits, 16) << "WKC="
                          << QStringLiteral("%1/%2")
                                 .arg(state.actualWorkingCounter)
                                 .arg(state.expectedWorkingCounter)
                          << "dcLocked=" << state.distributedClocksLocked
                          << "dcDifferenceNs=" << state.distributedClockDifferenceNs << "faults="
                          << QStringLiteral("%1/%2")
                                 .arg(state.currentFaults)
                                 .arg(state.latchedFaults);
    }
    for (const Data::ControllerAlarmSummary &alarm : snapshot.recentAlarms) {
        qInfo().noquote() << "[Product API hardware]" << label << "alarm=" << alarm.sequence
                          << "code=" << alarm.code << alarm.codeName
                          << "state=" << int(alarm.state) << "severity=" << int(alarm.severity)
                          << "source=" << int(alarm.source) << "latched=" << alarm.latched
                          << "faultMask="
                          << QStringLiteral("0x%1")
                                 .arg(alarm.faultMask, 16, 16, QLatin1Char('0'))
                          << "detail=" << alarm.detail << "raw="
                          << QStringLiteral("%1/%2/%3")
                                 .arg(alarm.detail0)
                                 .arg(alarm.detail1)
                                 .arg(alarm.detail2)
                          << "cycle=" << alarm.cycleCount;
    }
    if (snapshot.package) {
        const Data::ControllerPackageSummary &package = *snapshot.package;
        qInfo().noquote() << "[Product API hardware]" << label << "activeSelector="
                          << QStringLiteral("%1:%2:%3")
                                 .arg(hardwareSlotName(package.activeSlot))
                                 .arg(package.activeGeneration)
                                 .arg(package.activeConfigurationId)
                          << "CPU1PackageState=" << int(package.controllerState)
                          << "CPU1Boot=" << QString::number(package.controllerBootId);
    }
}

} // namespace

void EtherCATProductApiTests::testCrcAndGoldenFrame()
{
    QCOMPARE(Protocol::crc32c(QByteArrayView("123456789")), quint32(0xe3069283));

    Protocol::Error error;
    const QByteArray wire = Protocol::encodeHello(
        Protocol::Role::Control,
        0,
        0x1122334455667788,
        0x0102030405060708,
        1,
        Protocol::ExplicitTimingModeMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(
        wire,
        QByteArray::fromHex(
            "454341500001000a0040000100000000000000180000000000000000"
            "0102030405060708000000000000000100000000000000000000000000000000"
            "909715de000000010000000000000000000000001122334455667788"));
}

void EtherCATProductApiTests::testRuntimeResourceGoldenFrames()
{
    constexpr quint64 sessionId = 0x0102030405060708;
    constexpr quint64 bootId = 0x2122232425262728;

    const QByteArray tableQueryGolden = runtimeResourceTableQueryGoldenWire();
    QCOMPARE(tableQueryGolden.size(), Protocol::HeaderBytes + 64);
    QCOMPARE(readU32(tableQueryGolden, 60), quint32(0x09165ece));

    Protocol::RuntimeResourceTableQuery tableQuery;
    tableQuery.binding.bootId = bootId;
    tableQuery.binding.activeSlot = quint32('A');
    tableQuery.limit = 2;
    tableQuery.flags = 1;
    Protocol::Error error;
    const QByteArray tableQueryWire = Protocol::encodeQueryResourceTable(
        tableQuery,
        sessionId,
        0x1112131415161718,
        2,
        Protocol::RuntimeResourceMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(tableQueryWire, tableQueryGolden);

    const QByteArray tablePageGolden = runtimeResourceTablePageGoldenWire();
    QCOMPARE(tablePageGolden.size(), Protocol::HeaderBytes + 176);
    QCOMPARE(readU32(tablePageGolden, 60), quint32(0x5acfcbd1));
    Protocol::FrameParser tableParser(Protocol::Role::Bulk);
    const Protocol::ParseResult tableParse = tableParser.append(tablePageGolden);
    QVERIFY(!tableParse.error);
    QCOMPARE(tableParse.frames.size(), 1);
    const auto page
        = Protocol::decodeResourceTablePage(tableParse.frames.constFirst(), tableQuery, &error);
    QVERIFY(page);
    QVERIFY(!error);
    QCOMPARE(page->status, 0);
    QCOMPARE(page->recordCount, quint16(1));
    QCOMPARE(page->totalCount, quint32(2));
    QCOMPARE(page->nextCursor, quint32(1));
    QVERIFY(page->more);
    QCOMPARE(page->binding.bootId, bootId);
    QCOMPARE(page->binding.activeSlot, quint32('A'));
    QCOMPARE(page->binding.packageGeneration, quint64(1));
    QCOMPARE(page->binding.configurationId, quint64(0x100));
    QCOMPARE(page->binding.topologyGeneration, quint64(2));
    QCOMPARE(page->binding.runtimeGeneration, quint64(3));
    QCOMPARE(page->binding.catalogRevision, quint64(0x1111222233334444));
    QCOMPARE(page->binding.topologyIdentity, quint64(0x5555666677778888));
    QCOMPARE(page->resources.size(), 1);
    const Protocol::RuntimeResourceDescriptor &descriptor = page->resources.constFirst();
    QCOMPARE(descriptor.resourceId, quint64(0x1000000000000001));
    QCOMPARE(descriptor.componentId, quint64(0x2000000000000001));
    QCOMPARE(descriptor.parentId, quint64(0));
    QCOMPARE(descriptor.processImageBitOffset, quint32(0));
    QCOMPARE(descriptor.processImageBitLength, quint16(16));
    QCOMPARE(descriptor.valueBitWidth, quint16(16));
    QCOMPARE(descriptor.primitive, Protocol::RuntimeResourcePrimitive::Unsigned16);
    QCOMPARE(descriptor.direction, Protocol::RuntimeResourceDirection::Input);
    QCOMPARE(descriptor.access, Protocol::RuntimeResourceAccess::Read);
    QCOMPARE(descriptor.valueBytes, quint8(2));
    QCOMPARE(descriptor.qualityMask, quint16(0x000f));
    QCOMPARE(descriptor.groupId, quint32(1));
    QVERIFY(descriptor.safeValue.isEmpty());

    Protocol::RuntimeResourceSnapshotQuery snapshotQuery;
    snapshotQuery.binding = runtimeResourceBinding();
    snapshotQuery.resourceIds = {0x1000000000000001};
    const QByteArray snapshotQueryGolden = runtimeResourceSnapshotQueryGoldenWire();
    QCOMPARE(snapshotQueryGolden.size(), Protocol::HeaderBytes + 72);
    QCOMPARE(readU32(snapshotQueryGolden, 60), quint32(0x279d92a3));
    error = {};
    const QByteArray snapshotQueryWire = Protocol::encodeGetResourceSnapshot(
        snapshotQuery,
        sessionId,
        0x4142434445464748,
        3,
        Protocol::RuntimeResourceMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(snapshotQueryWire, snapshotQueryGolden);

    const QByteArray snapshotGolden = runtimeResourceSnapshotGoldenWire();
    QCOMPARE(snapshotGolden.size(), Protocol::HeaderBytes + 144);
    QCOMPARE(readU32(snapshotGolden, 60), quint32(0xcd329e8a));
    Protocol::FrameParser snapshotParser(Protocol::Role::Bulk);
    const Protocol::ParseResult snapshotParse = snapshotParser.append(snapshotGolden);
    QVERIFY(!snapshotParse.error);
    QCOMPARE(snapshotParse.frames.size(), 1);
    const auto snapshot = Protocol::decodeResourceSnapshot(
        snapshotParse.frames.constFirst(), snapshotQuery, &error);
    QVERIFY(snapshot);
    QVERIFY(!error);
    QCOMPARE(snapshot->status, 0);
    QVERIFY(snapshot->complete);
    QCOMPARE(snapshot->snapshotSequence, quint64(6));
    QCOMPARE(snapshot->captureCycle, quint64(0x1000));
    QCOMPARE(snapshot->controllerTimestampNs, quint64(0x6162636465666768));
    QCOMPARE(snapshot->samples.size(), 1);
    const Protocol::RuntimeResourceSample &sample = snapshot->samples.constFirst();
    QCOMPARE(sample.resourceId, quint64(0x1000000000000001));
    QCOMPARE(sample.primitive, Protocol::RuntimeResourcePrimitive::Unsigned16);
    QCOMPARE(sample.direction, Protocol::RuntimeResourceDirection::Input);
    QCOMPARE(sample.bitWidth, quint16(16));
    QCOMPARE(sample.quality, Protocol::RuntimeResourceQuality::Good);
    QCOMPARE(sample.value, QByteArray::fromHex("1234"));

    const QByteArray failureGolden = runtimeResourceSnapshotFailureGoldenWire();
    QCOMPARE(failureGolden.size(), Protocol::HeaderBytes + 112);
    QCOMPARE(readU32(failureGolden, 60), quint32(0x75b06256));
    Protocol::FrameParser failureParser(Protocol::Role::Bulk);
    const Protocol::ParseResult failureParse = failureParser.append(failureGolden);
    QVERIFY(!failureParse.error);
    QCOMPARE(failureParse.frames.size(), 1);
    error = {};
    const auto failure = Protocol::decodeResourceSnapshot(
        failureParse.frames.constFirst(), snapshotQuery, &error);
    QVERIFY(failure);
    QVERIFY(!error);
    QCOMPARE(failure->status, qint32(-17));
    QVERIFY(!failure->complete);
    QVERIFY(failure->samples.isEmpty());
}

void EtherCATProductApiTests::testRuntimeResourceCodecRejectsMalformed_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<quint32>("flags");

    const QByteArray validTablePayload
        = runtimeResourceTablePageGoldenWire().mid(Protocol::HeaderBytes);
    const QByteArray validSnapshotPayload
        = runtimeResourceSnapshotGoldenWire().mid(Protocol::HeaderBytes);

    QByteArray tableLength = validTablePayload;
    tableLength.chop(1);
    QTest::newRow("table-length")
        << 0 << tableLength << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QByteArray tableReserved = validTablePayload;
    putU32(tableReserved, 68, 1);
    QTest::newRow("table-reserved")
        << 0 << tableReserved << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QByteArray tableCount = validTablePayload;
    putU16(tableCount, 12, 0);
    QTest::newRow("table-count")
        << 0 << tableCount << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QTest::newRow("table-flags")
        << 0 << validTablePayload << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray tableOrder = validTablePayload;
    tableOrder.append(validTablePayload.mid(112, 64));
    putU16(tableOrder, 12, 2);
    putU32(tableOrder, 56, 3);
    putU32(tableOrder, 60, 2);
    QTest::newRow("table-resource-order")
        << 0 << tableOrder << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QByteArray tablePrimitive = validTablePayload;
    tablePrimitive[112 + 36] = char(0xff);
    QTest::newRow("table-primitive")
        << 0 << tablePrimitive << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QByteArray tableDirection = validTablePayload;
    tableDirection[112 + 37] = char(3);
    QTest::newRow("table-direction")
        << 0 << tableDirection << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QByteArray tableAccess = validTablePayload;
    tableAccess[112 + 38] = char(2);
    QTest::newRow("table-access")
        << 0 << tableAccess << quint32(Protocol::Flag::Response | Protocol::Flag::More);

    QByteArray snapshotQuality = validSnapshotPayload;
    putU32(snapshotQuality, 112 + 12, 1);
    QTest::newRow("snapshot-quality")
        << 1 << snapshotQuality << Protocol::flagValue(Protocol::Flag::Response);
}

void EtherCATProductApiTests::testRuntimeResourceCodecRejectsMalformed()
{
    Protocol::FrameParser wrongRoleParser(Protocol::Role::Control);
    const Protocol::ParseResult wrongRole
        = wrongRoleParser.append(runtimeResourceTablePageGoldenWire());
    QVERIFY(wrongRole.error);
    QCOMPARE(wrongRole.error->category, Protocol::ErrorCategory::UnsupportedMessage);

    QByteArray wrongDirection = runtimeResourceTableQueryGoldenWire();
    putU32(wrongDirection, 12, Protocol::flagValue(Protocol::Flag::Response));
    rewriteCrc(wrongDirection);
    Protocol::FrameParser requestParser(
        Protocol::Role::Bulk, Protocol::FrameDirection::ClientRequest);
    const Protocol::ParseResult wrongFlags = requestParser.append(wrongDirection);
    QVERIFY(wrongFlags.error);
    QCOMPARE(wrongFlags.error->category, Protocol::ErrorCategory::InvalidFlags);

    Protocol::RuntimeResourceSnapshotQuery unorderedQuery;
    unorderedQuery.binding = runtimeResourceBinding();
    unorderedQuery.resourceIds = {2, 1};
    Protocol::Error error;
    QVERIFY(Protocol::encodeGetResourceSnapshot(
                unorderedQuery, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray reservedQuery
        = runtimeResourceTableQueryGoldenWire().mid(Protocol::HeaderBytes);
    putU32(reservedQuery, 52, 1);
    error = {};
    QVERIFY(Protocol::encodeRequest(
                Protocol::MessageType::QueryResourceTable,
                reservedQuery,
                TestSessionId,
                1,
                1,
                0x2122232425262728,
                Protocol::CurrentMinor,
                &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray invalidRuntimeStatus
        = bulkStatusPayload(quint16(Protocol::MessageType::GetResourceSnapshot));
    putI32(invalidRuntimeStatus, 0, -6);
    putI32(invalidRuntimeStatus, 4, -1);
    error = {};
    QVERIFY(!Protocol::decodeBulkStatus(
        responseFrame(
            Protocol::MessageType::BulkStatus,
            invalidRuntimeStatus,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QFETCH(int, kind);
    QFETCH(QByteArray, payload);
    QFETCH(quint32, flags);

    Protocol::Frame frame = responseFrame(
        kind ? Protocol::MessageType::ResourceSnapshot
             : Protocol::MessageType::ResourceTablePage,
        payload,
        1,
        flags);
    frame.header.protocolMinor = Protocol::RuntimeResourceMinor;
    frame.header.bootId = 0x2122232425262728;

    error = {};
    if (kind) {
        Protocol::RuntimeResourceSnapshotQuery query;
        query.binding = runtimeResourceBinding();
        query.resourceIds = {0x1000000000000001};
        QVERIFY(!Protocol::decodeResourceSnapshot(frame, query, &error));
    } else {
        Protocol::RuntimeResourceTableQuery query;
        query.binding.bootId = 0x2122232425262728;
        query.binding.activeSlot = quint32('A');
        query.limit = 2;
        query.flags = 1;
        QVERIFY(!Protocol::decodeResourceTablePage(frame, query, &error));
    }
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
}

void EtherCATProductApiTests::testFrameStreamFragmentationAndCoalescing()
{
    const QByteArray firstWire = encodedResponse(
        Protocol::MessageType::ControllerState, controllerStatePayload(), 1);
    const QByteArray secondWire
        = encodedResponse(Protocol::MessageType::Capability, QByteArray("opaque"), 2);
    const QByteArray thirdWire
        = encodedResponse(Protocol::MessageType::PackageState, packageStatePayload(), 3);
    QVERIFY(!firstWire.isEmpty());
    QVERIFY(!secondWire.isEmpty());
    QVERIFY(!thirdWire.isEmpty());

    Protocol::FrameParser parser(Protocol::Role::Control);
    auto result = parser.append(QByteArrayView(firstWire.constData(), 13));
    QVERIFY(result.frames.isEmpty());
    QVERIFY(!result.error);

    result = parser.append(QByteArrayView(firstWire.constData() + 13, 59));
    QVERIFY(result.frames.isEmpty());
    QVERIFY(!result.error);

    result = parser.append(
        QByteArrayView(firstWire.constData() + 72, firstWire.size() - 72));
    QCOMPARE(result.frames.size(), 1);
    QCOMPARE(result.frames.constFirst().header.messageType,
             Protocol::MessageType::ControllerState);
    QCOMPARE(result.frames.constFirst().payload, controllerStatePayload());

    const QByteArray coalesced = secondWire + thirdWire;
    result = parser.append(coalesced);
    QVERIFY(!result.error);
    QCOMPARE(result.frames.size(), 2);
    QCOMPARE(result.frames.at(0).header.messageType, Protocol::MessageType::Capability);
    QCOMPARE(result.frames.at(1).header.messageType, Protocol::MessageType::PackageState);
    QCOMPARE(parser.lastSequence(), quint64(3));
    QCOMPARE(parser.negotiatedMinor(), std::optional<quint16>(Protocol::CurrentMinor));
}

void EtherCATProductApiTests::testFrameStreamRejectsMalformedInput_data()
{
    QTest::addColumn<QByteArray>("wire");
    QTest::addColumn<int>("expectedCategory");

    const QByteArray valid
        = encodedResponse(Protocol::MessageType::ControllerState, controllerStatePayload());
    QVERIFY(!valid.isEmpty());

    QByteArray badMagic = valid;
    badMagic[0] ^= char(0x01);
    QTest::newRow("bad-magic") << badMagic << int(Protocol::ErrorCategory::BadMagic);

    QByteArray badVersion = valid;
    putU16(badVersion, 4, 2);
    rewriteCrc(badVersion);
    QTest::newRow("bad-version")
        << badVersion << int(Protocol::ErrorCategory::IncompatibleVersion);

    QByteArray badHeader = valid;
    putU16(badHeader, 8, 63);
    rewriteCrc(badHeader);
    QTest::newRow("bad-header") << badHeader << int(Protocol::ErrorCategory::BadHeader);

    QByteArray tooLarge = valid.first(Protocol::HeaderBytes);
    putU32(tooLarge, 16, Protocol::ControlMaximumPayloadBytes + 1);
    rewriteCrc(tooLarge);
    QTest::newRow("too-large") << tooLarge << int(Protocol::ErrorCategory::TooLarge);

    QByteArray badCrc = valid;
    badCrc[60] ^= char(0x01);
    QTest::newRow("bad-crc") << badCrc << int(Protocol::ErrorCategory::BadCrc);

    QByteArray missingResponse = valid;
    putU32(missingResponse, 12, 0);
    rewriteCrc(missingResponse);
    QTest::newRow("missing-response")
        << missingResponse << int(Protocol::ErrorCategory::InvalidFlags);

    const QByteArray duplicateSequence
        = valid + encodedResponse(
                      Protocol::MessageType::Capability, QByteArray("opaque"), 1);
    QTest::newRow("duplicate-sequence")
        << duplicateSequence << int(Protocol::ErrorCategory::BadSequence);
}

void EtherCATProductApiTests::testFrameStreamRejectsMalformedInput()
{
    QFETCH(QByteArray, wire);
    QFETCH(int, expectedCategory);

    Protocol::FrameParser parser(Protocol::Role::Control);
    const Protocol::ParseResult result = parser.append(wire);
    QVERIFY(result.error);
    QCOMPARE(int(result.error->category), expectedCategory);
    QVERIFY(parser.hasError());

    const Protocol::ParseResult afterFailure = parser.append(QByteArrayView("ignored"));
    QVERIFY(afterFailure.error);
    QCOMPARE(afterFailure.error->category, result.error->category);
}

void EtherCATProductApiTests::testSemanticControllerState()
{
    Protocol::Error error;
    const Protocol::Frame frame
        = responseFrame(Protocol::MessageType::ControllerState, controllerStatePayload());
    const auto state = Protocol::decodeControllerState(frame, &error);
    QVERIFY(state);
    QVERIFY(!error);
    QCOMPARE(state->serviceState, Data::ControllerServiceState::Running);
    QCOMPARE(state->severity, Data::ControllerSeverity::None);
    QVERIFY(state->ready);
    QVERIFY(state->busOperational);
    QVERIFY(state->applicationActive);
    QVERIFY(state->distributedClocksLocked);
    QCOMPARE(state->controllerHeartbeat, quint64(101));
    QCOMPARE(state->cycleCount, quint64(202));
    QCOMPARE(state->controllerBootId, TestBootId);
    QCOMPARE(state->expectedWorkingCounter, quint32(6));
    QCOMPARE(state->actualWorkingCounter, quint32(6));
    QCOMPARE(state->latestAlarmSequence, quint32(19));

    QByteArray inconsistentPayload = controllerStatePayload();
    putU32(inconsistentPayload, 4, 0x4f); // SAFE_OUTPUT is invalid in RUNNING
    error = {};
    QVERIFY(!Protocol::decodeControllerState(
        responseFrame(Protocol::MessageType::ControllerState, inconsistentPayload), &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::Frame wrongEpoch = frame;
    wrongEpoch.header.bootId = TestBootId + 1;
    error = {};
    QVERIFY(!Protocol::decodeControllerState(wrongEpoch, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::IdentityMismatch);
}

void EtherCATProductApiTests::testSemanticPerformanceSnapshot()
{
    Protocol::Frame frame = responseFrame(
        Protocol::MessageType::PerformanceSnapshot,
        performanceSnapshotPayload(),
        1,
        Protocol::Flag::Response | Protocol::Flag::Replaceable);
    frame.header.requestId = 0;

    Protocol::Error error;
    const auto performance = Protocol::decodePerformanceSnapshot(frame, &error);
    QVERIFY(performance);
    QVERIFY(!error);
    QCOMPARE(performance->minimumExchangeTimeNs, quint64(100));
    QCOMPARE(performance->maximumExchangeTimeNs, quint64(240));
    QCOMPARE(performance->maximumSubmitLatenessNs, quint64(17));
    QCOMPARE(performance->timeoutCount, quint32(2));
    QCOMPARE(performance->cycleLateCount, quint32(6));
    QCOMPARE(performance->badWorkingCounterCount, quint32(7));
    QCOMPARE(performance->fpgaSnapshotSequence, quint32(12));
    QVERIFY(performance->processInputSampleValid);
    QVERIFY(performance->processInputSampleFresh);
    QVERIFY(performance->processInputSampleComplete);
    QCOMPARE(performance->processInputSample.size(), 16);
    QCOMPARE(performance->processInputSample.front(), char(0x01));
    QCOMPARE(performance->processInputSample.back(), char(0x10));
    QCOMPARE(performance->processInputSampleCycleCount, quint64(303));

    Protocol::Frame reserved = frame;
    putU32(reserved.payload, 164, 1);
    error = {};
    QVERIFY(!Protocol::decodePerformanceSnapshot(reserved, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::Frame stale = frame;
    putU32(stale.payload, 292, 129);
    error = {};
    QVERIFY(!Protocol::decodePerformanceSnapshot(stale, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::Frame legacy = frame;
    legacy.header.protocolMinor = 6;
    legacy.payload = legacy.payload.first(256);
    legacy.header.payloadLength = quint32(legacy.payload.size());
    error = {};
    const auto legacyPerformance = Protocol::decodePerformanceSnapshot(legacy, &error);
    QVERIFY(legacyPerformance);
    QVERIFY(!error);
    QVERIFY(!legacyPerformance->processInputSampleValid);
}

void EtherCATProductApiTests::testSemanticAuxiliaryRecords()
{
    const QByteArray descriptor("opaque-vendor-capability-v1");
    Protocol::Error error;
    const auto capability = Protocol::decodeCapability(
        responseFrame(Protocol::MessageType::Capability, descriptor), 0x1fff, &error);
    QVERIFY(capability);
    QVERIFY(!error);
    QCOMPARE(capability->descriptorSha256,
             QCryptographicHash::hash(descriptor, QCryptographicHash::Sha256));
    QCOMPARE(capability->maximumSlaves, 0);
    QVERIFY(capability->controlLease);
    QVERIFY(capability->resumablePush);
    QVERIFY(capability->transactionalBulk);
    QVERIFY(capability->capabilityQuery);
    QVERIFY(capability->structuredHandshakeError);
    QVERIFY(capability->firmwareUpdate);
    QVERIFY(capability->explicitTimingModeStart);
    QVERIFY(capability->faultReset);

    error = {};
    const auto package = Protocol::decodePackageState(
        responseFrame(Protocol::MessageType::PackageState, packageStatePayload()), &error);
    QVERIFY(package);
    QVERIFY(!error);
    QCOMPARE(package->stagedSlot, Data::ControllerSlot::A);
    QCOMPARE(package->stagedGeneration, quint64(11));
    QCOMPARE(package->stagedConfigurationId, quint64(22));
    QCOMPARE(package->activeSlot, Data::ControllerSlot::B);
    QCOMPARE(package->controllerState, Data::ControllerPackageState::Active);
    QCOMPARE(package->controllerBootId, TestBootId);

    error = {};
    const auto topology = Protocol::decodeTopologyResult(
        responseFrame(Protocol::MessageType::TopologyResult, topologyResultPayload()), &error);
    QVERIFY(topology);
    QVERIFY(!error);
    QCOMPARE(topology->respondingCount, quint16(2));
    QCOMPARE(topology->combinedAlState, quint32(0x01));
    QCOMPARE(topology->slaves.size(), 2);

    QByteArray invalidTopologyPayload = topologyResultPayload();
    putU32(invalidTopologyPayload, 20, 0x20);
    error = {};
    QVERIFY(!Protocol::decodeTopologyResult(
        responseFrame(Protocol::MessageType::TopologyResult, invalidTopologyPayload), &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    error = {};
    const auto firmware = Protocol::decodeFirmwareState(
        responseFrame(Protocol::MessageType::FirmwareState,
                      firmwareStatePayload(Protocol::MessageType::GetFirmwareState)),
        &error);
    QVERIFY(firmware);
    QVERIFY(!error);
    QCOMPARE(firmware->state, Data::ControllerFirmwareState::Confirmed);
    QCOMPARE(firmware->activeSlot, Data::ControllerSlot::A);
    QCOMPARE(firmware->previousSlot, Data::ControllerSlot::B);
    QVERIFY(firmware->packageVerified);
    QVERIFY(firmware->confirmed);
    QCOMPARE(firmware->progressPerMille, 1000);
    QCOMPARE(firmware->generation, quint64(41));
    QCOMPARE(firmware->updatedAt.toSecsSinceEpoch(), qint64(1700000000));

    QByteArray progressPayload = firmwareStatePayload(Protocol::MessageType::FirmwareProgress);
    putU16(progressPayload, 2, 1); // RECEIVING
    putU32(progressPayload, 12, 1); // UPLOAD_ACTIVE
    putU32(progressPayload, 36, 250);
    putU64(progressPayload, 160, 42);
    Protocol::Frame progressFrame = responseFrame(
        Protocol::MessageType::FirmwareProgress,
        progressPayload,
        2,
        Protocol::Flag::Response | Protocol::Flag::Replaceable);
    progressFrame.header.requestId = 0;
    const auto progress = Protocol::decodeFirmwareState(progressFrame, &error);
    QVERIFY(progress);
    QCOMPARE(progress->state, Data::ControllerFirmwareState::Receiving);
    QCOMPARE(progress->generation, quint64(42));

    const Protocol::Frame alarmFrame = responseFrame(
        Protocol::MessageType::AlarmRaised,
        alarmEventPayload(8, 3, 4, 1, 5, quint32(-2), 255, 0),
        3,
        Protocol::Flag::Response | Protocol::Flag::Important);
    const auto alarm = Protocol::decodeAlarmEvent(alarmFrame, &error);
    QVERIFY(alarm);
    QVERIFY(!error);
    QCOMPARE(alarm->sequence, quint32(8));
    QCOMPARE(alarm->code, quint32(3));
    QCOMPARE(alarm->codeName, Tr::tr("Runtime error"));
    QCOMPARE(alarm->state, Data::ControllerAlarmState::Raised);
    QCOMPARE(alarm->severity, Data::ControllerSeverity::Fatal);
    QCOMPARE(alarm->source, Data::ControllerAlarmSource::Service);
    QVERIFY(alarm->latched);
    QCOMPARE(qint32(alarm->detail0), qint32(-2));
    QCOMPARE(alarm->detail1, quint32(255));
    QCOMPARE(alarm->faultMask, quint64(1) << 15);
    QCOMPARE(
        alarm->detail,
        Tr::tr("OSL_ERR_TIMEOUT (%1), phase FAILED (%2)").arg(-2).arg(255));

    Protocol::Frame invalidAlarm = alarmFrame;
    invalidAlarm.header.flags = Protocol::flagValue(Protocol::Flag::Response);
    error = {};
    QVERIFY(!Protocol::decodeAlarmEvent(invalidAlarm, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidFlags);

    invalidAlarm = alarmFrame;
    putU64(invalidAlarm.payload, 56, 1);
    error = {};
    QVERIFY(!Protocol::decodeAlarmEvent(invalidAlarm, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    invalidAlarm = alarmFrame;
    putU32(invalidAlarm.payload, 28, 1);
    error = {};
    QVERIFY(!Protocol::decodeAlarmEvent(invalidAlarm, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    const auto clearedAlarm = Protocol::decodeAlarmEvent(
        responseFrame(
            Protocol::MessageType::AlarmCleared,
            alarmEventPayload(9, 11, 2, 2, 2),
            4,
            Protocol::Flag::Response | Protocol::Flag::Important),
        &error);
    QVERIFY(clearedAlarm);
    QCOMPARE(clearedAlarm->state, Data::ControllerAlarmState::Cleared);
    QVERIFY(!clearedAlarm->latched);

    const auto faultClearedAlarm = Protocol::decodeAlarmEvent(
        responseFrame(
            Protocol::MessageType::AlarmCleared,
            alarmEventPayload(10, 5, 1, 1, 2, 0, 0, 0),
            5,
            Protocol::Flag::Response | Protocol::Flag::Important),
        &error);
    QVERIFY(faultClearedAlarm);
    QCOMPARE(faultClearedAlarm->codeName, Tr::tr("Fault cleared"));
    QCOMPARE(faultClearedAlarm->severity, Data::ControllerSeverity::Information);
    QCOMPARE(faultClearedAlarm->source, Data::ControllerAlarmSource::Service);

    error = {};
    QVERIFY(!Protocol::decodeAlarmEvent(
        responseFrame(
            Protocol::MessageType::AlarmCleared,
            alarmEventPayload(10, 5, 2, 1, 2, 0, 0, 0),
            5,
            Protocol::Flag::Response | Protocol::Flag::Important),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    const auto wrappedCountersAlarm = Protocol::decodeAlarmEvent(
        responseFrame(
            Protocol::MessageType::AlarmRaised,
            alarmEventPayload(10, 11, 4, 2, 1, 3, 4, 4),
            5,
            Protocol::Flag::Response | Protocol::Flag::Important),
        &error);
    QVERIFY(wrappedCountersAlarm);
    QCOMPARE(
        wrappedCountersAlarm->detail,
        Tr::tr("TX %1, RX %2, pending unavailable, last frame %3").arg(3).arg(4).arg(4));

    error = {};
    QVERIFY(!Protocol::decodeAlarmEvent(
        responseFrame(
            Protocol::MessageType::AlarmCleared,
            alarmEventPayload(9, 11, 2, 2, 1),
            4,
            Protocol::Flag::Response | Protocol::Flag::Important),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    const auto unknownAlarm = Protocol::decodeAlarmEvent(
        responseFrame(
            Protocol::MessageType::AlarmRaised,
            alarmEventPayload(9, 99),
            4,
            Protocol::Flag::Response | Protocol::Flag::Important),
        &error);
    QVERIFY(unknownAlarm);
    QCOMPARE(unknownAlarm->code, quint32(99));

    QByteArray resumePayload(32, '\0');
    putU32(resumePayload, 20, 32);
    const auto resume = Protocol::decodeResumeEventsResult(
        responseFrame(Protocol::MessageType::ResumeEventsResult, resumePayload, 3), &error);
    QVERIFY(resume);
    QCOMPARE(resume->status, 0);
    QCOMPARE(resume->ringCapacity, quint32(32));

    QByteArray heartbeatPayload(16, '\0');
    putU64(heartbeatPayload, 0, 77);
    putU64(heartbeatPayload, 8, TestBootId);
    Protocol::Frame heartbeatFrame = responseFrame(
        Protocol::MessageType::PushHeartbeat,
        heartbeatPayload,
        4,
        Protocol::Flag::Response | Protocol::Flag::Replaceable);
    heartbeatFrame.header.requestId = 0;
    const auto heartbeat = Protocol::decodePushHeartbeat(heartbeatFrame, &error);
    QVERIFY(heartbeat);
    QCOMPARE(heartbeat->heartbeat, quint64(77));
    QCOMPARE(heartbeat->bootId, TestBootId);

    QByteArray commandPayload = commandStatusPayload();
    putI32(commandPayload, 4, -14); // UNSUPPORTED
    const auto commandStatus = Protocol::decodeCommandStatus(
        responseFrame(
            Protocol::MessageType::CommandStatus,
            commandPayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error);
    QVERIFY(commandStatus);
    QVERIFY(!error);
    QCOMPARE(commandStatus->status, qint32(-14));

    putI32(commandPayload, 4, -13); // not delivered in CommandStatus
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(
        responseFrame(
            Protocol::MessageType::CommandStatus,
            commandPayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray mismatchPayload = rejectedCommandStatusPayload(
        Protocol::MessageType::StartFreeRun,
        2,
        3,
        -35,
        (quint64(1) << 32) | 2);
    Protocol::Frame mismatchFrame = responseFrame(
        Protocol::MessageType::CommandStatus,
        mismatchPayload,
        1,
        Protocol::Flag::Response | Protocol::Flag::Error);
    const auto mismatch = Protocol::decodeCommandStatus(mismatchFrame, &error);
    QVERIFY(mismatch);
    QVERIFY(!error);
    QCOMPARE(mismatch->status, qint32(-35));
    QCOMPARE(mismatch->stage, quint16(2));
    QCOMPARE(mismatch->detail, (quint64(1) << 32) | 2);

    struct InvalidMismatch
    {
        const char *name;
        Protocol::MessageType originalType;
        quint16 stage;
        quint16 protocolMinor;
        quint64 detail;
        bool final;
    };
    const std::array invalidMismatches{
        InvalidMismatch{
            "minor-9",
            Protocol::MessageType::StartFreeRun,
            2,
            Protocol::ExplicitTimingModeMinor - 1,
            (quint64(1) << 32) | 2,
            true},
        InvalidMismatch{
            "legacy-start",
            Protocol::MessageType::Start,
            2,
            Protocol::CurrentMinor,
            (quint64(1) << 32) | 2,
            true},
        InvalidMismatch{
            "stage-1",
            Protocol::MessageType::StartFreeRun,
            1,
            Protocol::CurrentMinor,
            (quint64(1) << 32) | 2,
            true},
        InvalidMismatch{
            "wrong-requested-mode",
            Protocol::MessageType::StartFreeRun,
            2,
            Protocol::CurrentMinor,
            (quint64(2) << 32) | 1,
            true},
        InvalidMismatch{
            "actual-mode-zero",
            Protocol::MessageType::StartFreeRun,
            2,
            Protocol::CurrentMinor,
            quint64(1) << 32,
            true},
        InvalidMismatch{
            "same-mode",
            Protocol::MessageType::StartFreeRun,
            2,
            Protocol::CurrentMinor,
            (quint64(1) << 32) | 1,
            true},
        InvalidMismatch{
            "not-final",
            Protocol::MessageType::StartFreeRun,
            2,
            Protocol::CurrentMinor,
            (quint64(1) << 32) | 2,
            false},
    };
    for (const InvalidMismatch &invalid : invalidMismatches) {
        QByteArray payload = rejectedCommandStatusPayload(
            invalid.originalType, invalid.stage, 3, -35, invalid.detail);
        if (!invalid.final)
            putU32(payload, 36, 0);
        Protocol::Frame frame = responseFrame(
            Protocol::MessageType::CommandStatus,
            payload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error);
        frame.header.protocolMinor = invalid.protocolMinor;
        error = {};
        QVERIFY2(!Protocol::decodeCommandStatus(frame, &error), invalid.name);
        QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    }

    for (quint16 stage = 1; stage <= 4; ++stage) {
        Protocol::Frame frame = responseFrame(
            Protocol::MessageType::CommandStatus,
            successfulCommandStatusPayload(
                Protocol::MessageType::ResetFault,
                stage,
                3,
                stage == 4,
                stage == 4 ? 20 : 0));
        error = {};
        const auto resetStatus = Protocol::decodeCommandStatus(frame, &error);
        QVERIFY(resetStatus);
        QVERIFY(!error);
        QCOMPARE(resetStatus->originalType, quint16(Protocol::MessageType::ResetFault));
    }
    QByteArray invalidResetSuccess = successfulCommandStatusPayload(
        Protocol::MessageType::ResetFault, 4, 3, true, 0);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(
        responseFrame(Protocol::MessageType::CommandStatus, invalidResetSuccess), &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    QByteArray invalidResetIntermediateDetail = successfulCommandStatusPayload(
        Protocol::MessageType::ResetFault, 2, 3, false, 1);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(
        responseFrame(
            Protocol::MessageType::CommandStatus,
            invalidResetIntermediateDetail),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    for (const auto &[operationResult, detail] :
         QList<QPair<qint32, quint64>>{{-3, 0}, {-6, quint64(1) << 15}, {-9, 20}}) {
        const QByteArray payload = rejectedCommandStatusPayload(
            Protocol::MessageType::ResetFault,
            3,
            6,
            -15,
            detail,
            operationResult);
        error = {};
        const auto rejection = Protocol::decodeCommandStatus(
            responseFrame(
                Protocol::MessageType::CommandStatus,
                payload,
                1,
                Protocol::Flag::Response | Protocol::Flag::Error),
            &error);
        QVERIFY(rejection);
        QVERIFY(!error);
        QCOMPARE(rejection->operationResult, operationResult);
    }
    QByteArray invalidFaultActive = rejectedCommandStatusPayload(
        Protocol::MessageType::ResetFault,
        3,
        6,
        -15,
        quint64(1) << 17,
        -6);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(
        responseFrame(
            Protocol::MessageType::CommandStatus,
            invalidFaultActive,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    const QByteArray invalidStaleConfirmation = rejectedCommandStatusPayload(
        Protocol::MessageType::ResetFault, 3, 6, -15, 0, -9);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(
        responseFrame(
            Protocol::MessageType::CommandStatus,
            invalidStaleConfirmation,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::Frame legacyReset = responseFrame(
        Protocol::MessageType::CommandStatus,
        rejectedCommandStatusPayload(
            Protocol::MessageType::ResetFault, 2, 6, -14, 0, -7),
        1,
        Protocol::Flag::Response | Protocol::Flag::Error);
    legacyReset.header.protocolMinor = Protocol::ExplicitTimingModeMinor;
    error = {};
    QVERIFY(Protocol::decodeCommandStatus(legacyReset, &error));
    QVERIFY(!error);

    const QList<qint32> packageStatuses{-6, -7, -8, -12, -16};
    for (const qint32 status : packageStatuses) {
        QByteArray payload = packageStateErrorPayload();
        putI32(payload, 4, status);
        error = {};
        const auto package = Protocol::decodePackageState(
            responseFrame(
                Protocol::MessageType::PackageState,
                payload,
                1,
                Protocol::Flag::Response | Protocol::Flag::Error),
            &error);
        QVERIFY(!package);
        QCOMPARE(error.category, Protocol::ErrorCategory::ControllerStatus);
        QCOMPARE(error.status, std::optional<qint32>(status));
        QCOMPARE(error.operationResult, std::optional<qint32>(-3));
    }

    QByteArray packagePayload = packageStateErrorPayload();
    putI32(packagePayload, 4, -14); // not delivered in PackageState
    error = {};
    QVERIFY(!Protocol::decodePackageState(
        responseFrame(
            Protocol::MessageType::PackageState,
            packagePayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    putI32(packagePayload, 4, -35); // timing mismatch is CommandStatus-only
    error = {};
    QVERIFY(!Protocol::decodePackageState(
        responseFrame(
            Protocol::MessageType::PackageState,
            packagePayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray firmwarePayload = firmwareStateErrorPayload();
    putI32(firmwarePayload, 4, -12); // BAD_SEQUENCE
    error = {};
    QVERIFY(!Protocol::decodeFirmwareState(
        responseFrame(
            Protocol::MessageType::FirmwareState,
            firmwarePayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::ControllerStatus);
    QCOMPARE(error.status, std::optional<qint32>(-12));
    QCOMPARE(error.operationResult, std::optional<qint32>(-4));
    QCOMPARE(error.sourceDetail, std::optional<quint64>(FirmwareStateDetail));

    putI32(firmwarePayload, 4, -17); // not delivered in FirmwareState
    error = {};
    QVERIFY(!Protocol::decodeFirmwareState(
        responseFrame(
            Protocol::MessageType::FirmwareState,
            firmwarePayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error),
        &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
}

void EtherCATProductApiTests::testSupportedRequestPolicy()
{
    const QList<Protocol::MessageType> allowed{
        Protocol::MessageType::GetState,
        Protocol::MessageType::GetCapability,
        Protocol::MessageType::GetPackageState,
        Protocol::MessageType::GetFirmwareState,
        Protocol::MessageType::ResumeEvents,
    };
    for (const Protocol::MessageType type : allowed)
        QVERIFY(Protocol::isReadOnlyRequest(type));

    QByteArray lease(4, '\0');
    putU32(lease, 0, 30000);
    QByteArray topology(8, '\0');
    putU16(topology, 0, 0x1001);
    putU16(topology, 2, 64);
    QByteArray selector(24, '\0');
    putU32(selector, 0, 'A');
    putU64(selector, 8, 11);
    putU64(selector, 16, 22);
    QByteArray faultReset(16, '\0');
    putU64(faultReset, 0, quint64(1) << 15);
    putU32(faultReset, 8, 19);
    const QList<QPair<Protocol::MessageType, QByteArray>> controls{
        {Protocol::MessageType::AcquireControl, lease},
        {Protocol::MessageType::ReleaseControl, {}},
        {Protocol::MessageType::Start, {}},
        {Protocol::MessageType::StartFreeRun, {}},
        {Protocol::MessageType::StartDc, {}},
        {Protocol::MessageType::ControlledStop, {}},
        {Protocol::MessageType::ResetFault, faultReset},
        {Protocol::MessageType::Heartbeat, {}},
        {Protocol::MessageType::EnterConfigurationMode, {}},
        {Protocol::MessageType::DiscoverTopology, topology},
        {Protocol::MessageType::RestoreActivePackage, selector},
    };
    for (const auto &[type, payload] : controls) {
        QVERIFY(!Protocol::isReadOnlyRequest(type));
        QVERIFY(Protocol::isSupportedRequest(type));
        Protocol::Error error;
        QVERIFY(!Protocol::encodeRequest(
                     type,
                     payload,
                     TestSessionId,
                     1,
                     1,
                     TestBootId,
                     Protocol::CurrentMinor,
                     &error)
                     .isEmpty());
        QVERIFY(!error);
    }
    for (const Protocol::MessageType type :
         {Protocol::MessageType::StartFreeRun, Protocol::MessageType::StartDc}) {
        Protocol::Error error;
        QVERIFY(Protocol::encodeRequest(
                    type,
                    {},
                    TestSessionId,
                    1,
                    1,
                    TestBootId,
                    Protocol::ExplicitTimingModeMinor - 1,
                    &error)
                    .isEmpty());
        QCOMPARE(error.category, Protocol::ErrorCategory::IncompatibleVersion);
    }
    {
        Protocol::Error error;
        QVERIFY(Protocol::encodeRequest(
                    Protocol::MessageType::ResetFault,
                    faultReset,
                    TestSessionId,
                    1,
                    1,
                    TestBootId,
                    Protocol::ControlledFaultResetMinor - 1,
                    &error)
                    .isEmpty());
        QCOMPARE(error.category, Protocol::ErrorCategory::IncompatibleVersion);
        QCOMPARE(error.status, std::optional<qint32>(-14));
    }
    const auto rejectsFaultResetPayload = [](const QByteArray &payload) {
        Protocol::Error error;
        QVERIFY(Protocol::encodeRequest(
                    Protocol::MessageType::ResetFault,
                    payload,
                    TestSessionId,
                    1,
                    1,
                    TestBootId,
                    Protocol::CurrentMinor,
                    &error)
                    .isEmpty());
        QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    };
    rejectsFaultResetPayload({});
    QByteArray invalidFaultReset = faultReset;
    putU64(invalidFaultReset, 0, 0);
    rejectsFaultResetPayload(invalidFaultReset);
    invalidFaultReset = faultReset;
    putU64(invalidFaultReset, 0, quint64(1) << 17);
    rejectsFaultResetPayload(invalidFaultReset);
    invalidFaultReset = faultReset;
    putU32(invalidFaultReset, 8, 0);
    rejectsFaultResetPayload(invalidFaultReset);
    invalidFaultReset = faultReset;
    putU32(invalidFaultReset, 12, 1);
    rejectsFaultResetPayload(invalidFaultReset);

    const QList<Protocol::MessageType> forbidden{
        static_cast<Protocol::MessageType>(0x0500), // BeginFirmwareUpload
    };
    for (const Protocol::MessageType type : forbidden) {
        QVERIFY(!Protocol::isReadOnlyRequest(type));
        QVERIFY(!Protocol::isSupportedRequest(type));
        Protocol::Error error;
        QVERIFY(Protocol::encodeRequest(
                    type, {}, TestSessionId, 1, 1, TestBootId, Protocol::CurrentMinor, &error)
                    .isEmpty());
        QCOMPARE(error.category, Protocol::ErrorCategory::UnsupportedMessage);
    }

    Protocol::Error error;
    QVERIFY(Protocol::encodeRequest(Protocol::MessageType::GetState,
                                    QByteArrayView("not-empty"),
                                    TestSessionId,
                                    1,
                                    1,
                                    TestBootId,
                                    Protocol::CurrentMinor,
                                    &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
}

void EtherCATProductApiTests::testPackageDeploymentRequestPolicy()
{
    QCOMPARE(Protocol::BulkChunkMaximumBytes, quint32(65528));

    QByteArray begin(24, '\0');
    putU64(begin, 0, 813);
    putU32(begin, 8, 67648);
    putU32(begin, 20, Protocol::PackageUploadMode);

    QByteArray chunk(Protocol::BulkChunkHeaderBytes, '\0');
    putU32(chunk, 0, Protocol::PackageObjectKind);
    putU32(chunk, 4, 0);
    chunk.append("ecpkg");

    QByteArray selector(24, '\0');
    putU32(selector, 0, 'B');
    putU64(selector, 8, 12);
    putU64(selector, 16, 813);

    struct RequestCase
    {
        Protocol::MessageType type;
        Protocol::Role role;
        QByteArray payload;
    };
    const QList<RequestCase> requests{
        {Protocol::MessageType::BulkBegin, Protocol::Role::Bulk, begin},
        {Protocol::MessageType::BulkChunk, Protocol::Role::Bulk, chunk},
        {Protocol::MessageType::BulkCommit, Protocol::Role::Bulk, {}},
        {Protocol::MessageType::BulkAbort, Protocol::Role::Bulk, {}},
        {Protocol::MessageType::ValidatePackage, Protocol::Role::Control, selector},
        {Protocol::MessageType::ActivatePackage, Protocol::Role::Control, selector},
        {Protocol::MessageType::RollbackPackage, Protocol::Role::Control, selector},
        {Protocol::MessageType::RestoreActivePackage, Protocol::Role::Control, selector},
    };
    quint64 requestId = 1;
    for (const RequestCase &request : requests) {
        QVERIFY(Protocol::isPackageDeploymentRequest(request.type));
        QVERIFY(Protocol::isSupportedRequest(request.type));
        QVERIFY(!Protocol::isReadOnlyRequest(request.type));

        Protocol::Error error;
        const QByteArray wire = Protocol::encodeRequest(
            request.type,
            request.payload,
            TestSessionId,
            requestId,
            requestId,
            TestBootId,
            Protocol::CurrentMinor,
            &error);
        QVERIFY(!wire.isEmpty());
        QVERIFY(!error);

        Protocol::FrameParser parser(
            request.role, Protocol::FrameDirection::ClientRequest);
        const Protocol::ParseResult result = parser.append(wire);
        QVERIFY(!result.error);
        QCOMPARE(result.frames.size(), 1);
        QCOMPARE(result.frames.constFirst().header.messageType, request.type);
        QCOMPARE(result.frames.constFirst().payload, request.payload);

        const Protocol::Role wrongRole = request.role == Protocol::Role::Bulk
                                             ? Protocol::Role::Control
                                             : Protocol::Role::Bulk;
        Protocol::FrameParser wrongParser(
            wrongRole, Protocol::FrameDirection::ClientRequest);
        const Protocol::ParseResult wrongResult = wrongParser.append(wire);
        QVERIFY(wrongResult.error);
        QCOMPARE(
            wrongResult.error->category, Protocol::ErrorCategory::UnsupportedMessage);
        ++requestId;
    }

    const auto rejectsPayload = [](Protocol::MessageType type, const QByteArray &payload) {
        Protocol::Error error;
        QVERIFY(Protocol::encodeRequest(type,
                                        payload,
                                        TestSessionId,
                                        1,
                                        1,
                                        TestBootId,
                                        Protocol::CurrentMinor,
                                        &error)
                    .isEmpty());
        QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    };

    QByteArray invalid = begin;
    putU64(invalid, 0, 0);
    rejectsPayload(Protocol::MessageType::BulkBegin, invalid);
    invalid = begin;
    putU32(invalid, 8, 0);
    rejectsPayload(Protocol::MessageType::BulkBegin, invalid);
    invalid = begin;
    putU32(invalid, 12, 1);
    rejectsPayload(Protocol::MessageType::BulkBegin, invalid);
    invalid = begin;
    putU32(invalid, 16, 1);
    rejectsPayload(Protocol::MessageType::BulkBegin, invalid);
    invalid = begin;
    putU32(invalid, 20, 2);
    rejectsPayload(Protocol::MessageType::BulkBegin, invalid);

    invalid = chunk.left(Protocol::BulkChunkHeaderBytes);
    rejectsPayload(Protocol::MessageType::BulkChunk, invalid);
    invalid = chunk;
    putU32(invalid, 0, Protocol::PackageObjectKind - 1);
    rejectsPayload(Protocol::MessageType::BulkChunk, invalid);
    invalid = QByteArray(Protocol::BulkMaximumPayloadBytes + 1, '\0');
    putU32(invalid, 0, Protocol::PackageObjectKind);
    rejectsPayload(Protocol::MessageType::BulkChunk, invalid);

    rejectsPayload(Protocol::MessageType::BulkCommit, QByteArray("unexpected"));
    rejectsPayload(Protocol::MessageType::BulkAbort, QByteArray("unexpected"));

    for (const Protocol::MessageType type :
         {Protocol::MessageType::ValidatePackage,
          Protocol::MessageType::ActivatePackage,
          Protocol::MessageType::RollbackPackage,
          Protocol::MessageType::RestoreActivePackage}) {
        invalid = selector;
        putU32(invalid, 0, 'C');
        rejectsPayload(type, invalid);
        invalid = selector;
        putU32(invalid, 4, 1);
        rejectsPayload(type, invalid);
        invalid = selector;
        putU64(invalid, 8, 0);
        rejectsPayload(type, invalid);
        invalid = selector;
        putU64(invalid, 16, 0);
        rejectsPayload(type, invalid);
    }
}

void EtherCATProductApiTests::testConnectionProfileEndpointConfiguration_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("summary");
    QTest::addColumn<int>("controlPort");
    QTest::addColumn<int>("pushPort");
    QTest::addColumn<int>("bulkPort");

    QTest::newRow("default-port")
        << QStringLiteral("192.168.3.101") << true
        << QStringLiteral("192.168.3.101:15200") << 15200 << 15201 << 15202;
    QTest::newRow("trimmed")
        << QStringLiteral(" 10.20.30.41 ") << true
        << QStringLiteral("10.20.30.41:15200") << 15200 << 15201 << 15202;
    QTest::newRow("decimal-leading-zeroes")
        << QStringLiteral("010.020.030.040") << true
        << QStringLiteral("10.20.30.40:15200") << 15200 << 15201 << 15202;

    const QList<QString> invalidInputs{
        {},
        QStringLiteral("controller.local"),
        QStringLiteral("10.20.30.40:24000"),
        QStringLiteral("127.0.0.1:"),
        QStringLiteral(":15200"),
        QStringLiteral("127.0.0.1:0"),
        QStringLiteral("127.0.0.1:65534"),
        QStringLiteral("127.0.0.1:65535"),
        QStringLiteral("127.0.0.1:-1"),
        QStringLiteral("127.0.0.1:+1"),
        QStringLiteral("127.0.0.1:port"),
        QStringLiteral("127.0.0"),
        QStringLiteral("127.0.0.256"),
        QStringLiteral("127.0.0.a"),
        QStringLiteral("2001:db8::1"),
    };
    for (int index = 0; index < invalidInputs.size(); ++index) {
        QTest::newRow(qPrintable(QStringLiteral("invalid-%1").arg(index)))
            << invalidInputs.at(index) << false << QString() << 0 << 0 << 0;
    }
}

void EtherCATProductApiTests::testConnectionProfileEndpointConfiguration()
{
    QFETCH(QString, input);
    QFETCH(bool, valid);
    QFETCH(QString, summary);
    QFETCH(int, controlPort);
    QFETCH(int, pushPort);
    QFETCH(int, bulkPort);

    const ProductApiSession::EndpointSet initial
        = ProductApiSession::EndpointSet::productionDefaults();
    ProductApiConnectionProvider provider(initial, testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(!request.profileId.isNull());
    const Data::ControllerConnectionSnapshot before = provider.connectionSnapshot();
    const ProductApiSession::EndpointSet endpointsBefore
        = provider.sessionForTests()->endpointsForTests();
    QSignalSpy profilesChanged(
        &provider, &Core::ControllerConnectionProvider::connectionProfilesChanged);

    const Utils::Result<> result
        = provider.setConnectionProfileEndpoint(request.scope, request.profileId, input);
    QCOMPARE(bool(result), valid);
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);

    if (!valid) {
        QCOMPARE(provider.connectionSnapshot(), before);
        QCOMPARE(provider.sessionForTests()->endpointsForTests(), endpointsBefore);
        QCOMPARE(profilesChanged.count(), 0);
        return;
    }

    const ProductApiSession::EndpointSet endpoints
        = provider.sessionForTests()->endpointsForTests();
    QCOMPARE(endpoints.host, summary.section(QLatin1Char(':'), 0, 0));
    QCOMPARE(endpoints.controlPort, quint16(controlPort));
    QCOMPARE(endpoints.pushPort, quint16(pushPort));
    QCOMPARE(endpoints.bulkPort, quint16(bulkPort));
    QCOMPARE(endpoints.endpointSummary, summary);
    QCOMPARE(profilesChanged.count(), 1);

    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QCOMPARE(snapshot.state, Data::ControllerConnectionState::Disconnected);
    QCOMPARE(snapshot.endpointSummary, summary);
    QVERIFY(snapshot.sessionGeneration > before.sessionGeneration);
    QCOMPARE(snapshot.channels.size(), 3);
    for (const Data::ControllerChannelStatus &channel : snapshot.channels) {
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
        QVERIFY(channel.detail.isEmpty());
        QVERIFY(!channel.lastActivityAt.isValid());
    }

    const auto configuration
        = provider.connectionProfileConfiguration(request.scope, request.profileId);
    QVERIFY(configuration);
    QCOMPARE(configuration->endpoint, summary.section(QLatin1Char(':'), 0, 0));
    QVERIFY(configuration->editable);
    QCOMPARE(configuration->endpointLabel, Tr::tr("Controller IP:"));
    QCOMPARE(configuration->endpointAccessibleName, Tr::tr("Controller IP address"));
    QCOMPARE(
        configuration->endpointDescription,
        Tr::tr(
            "Enter only the IPv4 controller address. Control, Push, and Bulk always use ports "
            "15200, 15201, and 15202."));
}

void EtherCATProductApiTests::testConnectionProfileEndpointReconfigurationGuards()
{
    LoopbackController controller;
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    const ProductApiSession::EndpointSet connectedEndpoints
        = provider.sessionForTests()->endpointsForTests();
    const auto connectedConfiguration
        = provider.connectionProfileConfiguration(request.scope, request.profileId);
    QVERIFY(connectedConfiguration);
    QVERIFY(!connectedConfiguration->editable);
    const Utils::Result<> connectedChange = provider.setConnectionProfileEndpoint(
        request.scope, request.profileId, QStringLiteral("10.20.30.40"));
    QVERIFY(!connectedChange);
    QCOMPARE(provider.sessionForTests()->endpointsForTests(), connectedEndpoints);

    QVERIFY(provider.disconnectFromController());
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    const quint64 disconnectedGeneration = provider.connectionSnapshot().sessionGeneration;
    const std::array<int, 3> acceptCounts{
        controller.acceptCount(Protocol::Role::Control),
        controller.acceptCount(Protocol::Role::Push),
        controller.acceptCount(Protocol::Role::Bulk),
    };

    QVERIFY(provider.setConnectionProfileEndpoint(
        request.scope, request.profileId, QStringLiteral("10.20.30.40")));
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QCOMPARE(snapshot.state, Data::ControllerConnectionState::Disconnected);
    QCOMPARE(snapshot.endpointSummary, QStringLiteral("10.20.30.40:15200"));
    QVERIFY(snapshot.sessionGeneration > disconnectedGeneration);
    QVERIFY(snapshot.scope.projectId.isNull());
    QVERIFY(snapshot.scope.masterId.isNull());
    QVERIFY(snapshot.profileId.isNull());
    QCOMPARE(snapshot.protocolVersion, Data::ControllerProtocolVersion());
    QVERIFY(!snapshot.connectedAt.isValid());
    QVERIFY(!snapshot.lastHeartbeatAt.isValid());
    QVERIFY(!snapshot.readOnly);
    QVERIFY(!snapshot.mock);
    QVERIFY(!snapshot.session);
    QVERIFY(!snapshot.controllerState);
    QVERIFY(!snapshot.capability);
    QVERIFY(!snapshot.package);
    QVERIFY(!snapshot.firmware);
    QVERIFY(!snapshot.lastError);
    QCOMPARE(snapshot.controlProgress, Data::ControllerControlProgress());
    QVERIFY(!snapshot.topology);
    QCOMPARE(snapshot.channels.size(), 3);
    for (const Data::ControllerChannelStatus &channel : snapshot.channels) {
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
        QVERIFY(channel.detail.isEmpty());
        QVERIFY(!channel.lastActivityAt.isValid());
    }
    QVERIFY(provider.sessionForTests()->isIdleForTests());

    QTest::qWait(50);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), acceptCounts.at(0));
    QCOMPARE(controller.acceptCount(Protocol::Role::Push), acceptCounts.at(1));
    QCOMPARE(controller.acceptCount(Protocol::Role::Bulk), acceptCounts.at(2));

    LoopbackController silentController(LoopbackController::Behavior::SilentHello);
    QVERIFY(silentController.start());
    ProductApiSession::Options options = testOptions();
    options.handshakeTimeoutMs = 50;
    options.reconnectAttempts = 0;
    ProductApiConnectionProvider failedProvider(silentController.endpoints(), options);
    const Data::ControllerConnectionRequest failedRequest = requestFor(failedProvider);
    QVERIFY(failedProvider.connectToController(failedRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        failedProvider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QVERIFY(failedProvider.connectionSnapshot().lastError);
    QVERIFY(!failedProvider.connectionSnapshot().session);
    QVERIFY(!failedProvider.connectionSnapshot().controllerState);
    QVERIFY(!failedProvider.connectionSnapshot().topology);
    for (const Data::ControllerChannelStatus &channel :
         failedProvider.connectionSnapshot().channels) {
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
    }
    const auto failedConfiguration = failedProvider.connectionProfileConfiguration(
        failedRequest.scope, failedRequest.profileId);
    QVERIFY(failedConfiguration);
    QVERIFY(failedConfiguration->editable);
    QVERIFY(failedProvider.setConnectionProfileEndpoint(
        failedRequest.scope, failedRequest.profileId, QStringLiteral("10.20.30.41")));
    QCOMPARE(
        failedProvider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected);
    QCOMPARE(
        failedProvider.connectionSnapshot().endpointSummary,
        QStringLiteral("10.20.30.41:15200"));
    QVERIFY(failedProvider.sessionForTests()->isIdleForTests());
}

void EtherCATProductApiTests::testConnectionProfileEndpointPersistence()
{
    const Utils::Key settingsKey(Constants::BASE_ENDPOINT_SETTINGS_KEY);
    SettingsValueGuard settingsGuard(settingsKey);
    Utils::userSettings().setValue(settingsKey, QStringLiteral("203.0.113.7:24000"));

    {
        ProductApiConnectionProvider injectedProvider(
            ProductApiSession::EndpointSet::productionDefaults(), testOptions());
        const Data::ControllerConnectionRequest request = requestFor(injectedProvider);
        QVERIFY(injectedProvider.setConnectionProfileEndpoint(
            request.scope, request.profileId, QStringLiteral("10.20.30.40")));
        QCOMPARE(
            Utils::userSettings().value(settingsKey).toString(),
            QStringLiteral("203.0.113.7:24000"));
    }

    {
        ProductApiConnectionProvider defaultProvider;
        const ProductApiSession::EndpointSet loaded
            = defaultProvider.sessionForTests()->endpointsForTests();
        QCOMPARE(loaded.host, QStringLiteral("203.0.113.7"));
        QCOMPARE(loaded.controlPort, quint16(15200));
        QCOMPARE(loaded.pushPort, quint16(15201));
        QCOMPARE(loaded.bulkPort, quint16(15202));
        QCOMPARE(loaded.endpointSummary, QStringLiteral("203.0.113.7:15200"));

        const Data::ControllerConnectionRequest request = requestFor(defaultProvider);
        QVERIFY(defaultProvider.setConnectionProfileEndpoint(
            request.scope, request.profileId, QStringLiteral("10.20.30.41")));
        QCOMPARE(
            Utils::userSettings().value(settingsKey).toString(),
            QStringLiteral("10.20.30.41"));
    }
}

void EtherCATProductApiTests::testThreeChannelInitialSnapshot()
{
    LoopbackController controller;
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.isAvailable());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(!request.profileId.isNull());
    QSignalSpy snapshots(&provider, &Core::ControllerConnectionProvider::connectionSnapshotChanged);

    QVERIFY(provider.connectToController(request));
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QVERIFY(snapshots.count() > 0);

    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QCOMPARE(snapshot.scope, request.scope);
    QCOMPARE(snapshot.profileId, request.profileId);
    QCOMPARE(snapshot.endpointSummary, controller.endpoints().endpointSummary);
    QCOMPARE(snapshot.channels.size(), 3);
    for (const Data::ControllerChannelStatus &channel : snapshot.channels)
        QCOMPARE(channel.state, Data::ControllerChannelState::Connected);
    QCOMPARE(snapshot.protocolVersion.major, quint16(1));
    QCOMPARE(snapshot.protocolVersion.minor, Protocol::CurrentMinor);
    QVERIFY(!snapshot.readOnly);
    QVERIFY(!snapshot.mock);
    QVERIFY(snapshot.session);
    QCOMPARE(snapshot.session->sessionId, TestSessionId);
    QCOMPARE(snapshot.session->bootId, TestBootId);
    QVERIFY(snapshot.controllerState);
    QCOMPARE(snapshot.controllerState->serviceState, Data::ControllerServiceState::Running);
    QCOMPARE(snapshot.recentAlarms.size(), 1);
    QCOMPARE(snapshot.recentAlarms.constFirst().sequence, TestAlarmSequence);
    QCOMPARE(snapshot.recentAlarms.constFirst().codeName, Tr::tr("RX timeout"));
    QVERIFY(snapshot.recentAlarms.constFirst().detail.contains(Tr::tr("pending %1").arg(3)));
    QVERIFY(snapshot.capability);
    QCOMPARE(snapshot.capability->descriptorSha256,
             QCryptographicHash::hash(
                 QByteArray("opaque-vendor-capability-v1"), QCryptographicHash::Sha256));
    QVERIFY(snapshot.package);
    QCOMPARE(snapshot.package->controllerState, Data::ControllerPackageState::Active);
    QVERIFY(snapshot.firmware);
    QCOMPARE(snapshot.firmware->state, Data::ControllerFirmwareState::Confirmed);
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().lastHeartbeatAt.isValid(), 1000);

    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QCOMPARE(controller.acceptCount(Protocol::Role::Push), 1);
    QCOMPARE(controller.acceptCount(Protocol::Role::Bulk), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Hello), 3);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetState), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetCapability), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetPackageState), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetFirmwareState), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ResumeEvents), 1);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.refreshController());
    QTRY_COMPARE_WITH_TIMEOUT(provider.sessionForTests()->pendingRequestCountForTests(), 0, 2000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetState), 2);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetCapability), 2);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testLiveStatePolling()
{
    LoopbackController controller;
    QVERIFY(controller.start());

    ProductApiSession::Options options = testOptions();
    options.liveStatePollIntervalMs = 50;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(controller.requestCount(Protocol::MessageType::GetState) >= 1);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.requestCount(Protocol::MessageType::GetState) >= 2, 1000);
    QVERIFY(provider.connectionSnapshot().controllerState);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testLeaseOwnershipRequiresAcquireOrResume()
{
    {
        LoopbackController controller;
        controller.setHelloLeaseOwnerSessionId(TestSessionId);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        QVERIFY(snapshot.session);
        QCOMPARE(snapshot.session->controlLeaseOwnerSessionId, TestSessionId);
        QVERIFY(!snapshot.session->ownsControlLease);
        QCOMPARE(controller.requestCount(Protocol::MessageType::Heartbeat), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
        QVERIFY(controller.violations().isEmpty());

        const Utils::Result<> disconnectResult = provider.disconnectFromController();
        QVERIFY(!disconnectResult);
        QCOMPARE(
            disconnectResult.error(),
            Tr::tr(
                "The controller control lease ownership is unverified for this session."));
        QCOMPARE(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 0);
        QVERIFY(!provider.sessionForTests()->isIdleForTests());
    }

    {
        LoopbackController controller;
        controller.setHelloLeaseOwnerSessionId(TestSessionId + 1);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest acquire;
        acquire.command = Data::ControllerControlCommand::AcquireControl;
        const Utils::Result<> result = provider.executeControlCommand(acquire);
        QVERIFY(!result);
        QCOMPARE(
            result.error(),
            Tr::tr("The controller reports that the control lease is already owned."));
        QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::Heartbeat), 0);
        QVERIFY(controller.violations().isEmpty());

        QVERIFY(provider.disconnectFromController());
        QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 0);
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest acquire;
        acquire.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(acquire));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.connectionSnapshot().session);
        QVERIFY(provider.connectionSnapshot().session->ownsControlLease);
        const int heartbeatCount = controller.requestCount(Protocol::MessageType::Heartbeat);
        QVERIFY(heartbeatCount > 0);
        const quint64 generation = provider.connectionSnapshot().sessionGeneration;

        controller.dropChannel(Protocol::Role::Push);
        QTRY_VERIFY_WITH_TIMEOUT(
            provider.connectionSnapshot().sessionGeneration > generation, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
        QVERIFY(provider.connectionSnapshot().session);
        QVERIFY(provider.connectionSnapshot().session->ownsControlLease);
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.requestCount(Protocol::MessageType::Heartbeat) > heartbeatCount, 1000);
        QVERIFY(controller.violations().isEmpty());

        QVERIFY(provider.disconnectFromController());
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Disconnected,
            1000);
    }
}

void EtherCATProductApiTests::testProtocolMinorDowngrade()
{
    LoopbackController controller;
    controller.setProtocolMinor(Protocol::FirmwareMinor);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QCOMPARE(snapshot.protocolVersion.major, quint16(1));
    QCOMPARE(snapshot.protocolVersion.minor, Protocol::FirmwareMinor);
    QVERIFY(snapshot.capability);
    QVERIFY(snapshot.capability->firmwareUpdate);
    QVERIFY(!snapshot.capability->explicitTimingModeStart);
    QVERIFY(provider.supportsControlCommand(Data::ControllerControlCommand::Start));
    QVERIFY(!provider.supportsControlCommand(Data::ControllerControlCommand::StartFreeRun));
    QVERIFY(
        !provider.supportsControlCommand(Data::ControllerControlCommand::StartDistributedClocks));
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QCOMPARE(controller.acceptCount(Protocol::Role::Push), 1);
    QCOMPARE(controller.acceptCount(Protocol::Role::Bulk), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Hello), 3);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetFirmwareState), 1);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testControlLifecycle()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(!provider.connectionSnapshot().readOnly);
    QVERIFY(provider.connectionSnapshot().controllerState);
    QCOMPARE(
        provider.connectionSnapshot().controllerState->serviceState,
        Data::ControllerServiceState::Shutdown);
    QVERIFY(provider.connectionSnapshot().package);
    QCOMPARE(
        provider.connectionSnapshot().package->controllerState,
        Data::ControllerPackageState::Unavailable);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QVERIFY(provider.connectionSnapshot().session);
    QVERIFY(provider.connectionSnapshot().session->ownsControlLease);

    control.command = Data::ControllerControlCommand::EnterConfigurationMode;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Shutdown,
        1000);

    control.command = Data::ControllerControlCommand::DiscoverTopology;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QVERIFY(provider.connectionSnapshot().topology);
    QCOMPARE(provider.connectionSnapshot().topology->respondingCount, quint32(2));
    QCOMPARE(provider.connectionSnapshot().topology->slaves.size(), 2);
    QCOMPARE(provider.connectionSnapshot().topology->slaves.at(0).stationAddress, quint16(0x1001));
    QCOMPARE(provider.connectionSnapshot().topology->slaves.at(1).productCode, quint32(0x87654321));

    control.command = Data::ControllerControlCommand::RestoreActivePackage;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe
            && provider.connectionSnapshot().package
            && provider.connectionSnapshot().package->controllerState
                   == Data::ControllerPackageState::Active,
        1000);

    control.command = Data::ControllerControlCommand::StartDistributedClocks;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Failed,
        1000);
    QVERIFY(provider.connectionSnapshot().controlProgress.status);
    QCOMPARE(*provider.connectionSnapshot().controlProgress.status, qint32(-35));
    QVERIFY(provider.connectionSnapshot().lastError);
    QCOMPARE(provider.connectionSnapshot().lastError->codeName,
             QString("TIMING_MODE_MISMATCH"));
    QCOMPARE(provider.connectionSnapshot().lastError->sourceDetail,
             std::optional<quint64>((quint64(2) << 32) | 1));
    QVERIFY(provider.connectionSnapshot().lastError->detail.contains(QStringLiteral("DC")));
    QVERIFY(provider.connectionSnapshot().lastError->detail.contains(QStringLiteral("FreeRun")));
    QVERIFY(provider.connectionSnapshot().controlProgress.detail.contains(QStringLiteral("DC")));
    QVERIFY(
        provider.connectionSnapshot().controlProgress.detail.contains(QStringLiteral("FreeRun")));
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe,
        1000);

    control.command = Data::ControllerControlCommand::StartFreeRun;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Running,
        1000);

    control.command = Data::ControllerControlCommand::Pause;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Paused,
        1000);

    control.command = Data::ControllerControlCommand::Resume;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Running,
        1000);

    control.command = Data::ControllerControlCommand::ControlledStop;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe,
        1000);

    control.command = Data::ControllerControlCommand::EnterConfigurationMode;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Shutdown,
        1000);

    control.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QVERIFY(provider.connectionSnapshot().session);
    QVERIFY(!provider.connectionSnapshot().session->ownsControlLease);

    QVERIFY(provider.disconnectFromController());
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::DiscoverTopology), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::RestoreActivePackage), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Start), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::StartFreeRun), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::StartDc), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Pause), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Resume), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ControlledStop), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testFaultResetLifecycle()
{
    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.setProtocolMinor(Protocol::ExplicitTimingModeMinor);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        QCOMPARE(snapshot.protocolVersion.minor, Protocol::ExplicitTimingModeMinor);
        QVERIFY(snapshot.capability);
        QVERIFY(!snapshot.capability->faultReset);
        QVERIFY(!provider.supportsControlCommand(Data::ControllerControlCommand::ResetFault));

        Data::ControllerControlRequest reset;
        reset.command = Data::ControllerControlCommand::ResetFault;
        reset.expectedLatchedFaults = quint64(1) << 15;
        reset.expectedAlarmSequence = TestAlarmSequence;
        const Utils::Result<> result = provider.executeControlCommand(reset);
        QVERIFY(!result);
        QCOMPARE(
            result.error(),
            Tr::tr(
                "UNSUPPORTED (-14): controlled fault reset requires negotiated Product API "
                "v1.11 and feature bit 12; no controller request was sent."));
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Failed);
        QCOMPARE(provider.connectionSnapshot().controlProgress.stage, quint16(0));
        QVERIFY(provider.connectionSnapshot().controlProgress.final);
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.status,
            std::optional<qint32>(-14));
        QVERIFY(!provider.connectionSnapshot().controlProgress.operationResult);
        QVERIFY(provider.connectionSnapshot().lastError);
        QCOMPARE(
            provider.connectionSnapshot().lastError->operation,
            Data::ControllerOperation::ResetFault);
        QCOMPARE(provider.connectionSnapshot().lastError->codeName, QStringLiteral("UNSUPPORTED"));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 0);
        QVERIFY(controller.violations().isEmpty());

        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.setProtocolMinor(Protocol::ControlledFaultResetMinor);
        controller.setFeatureBits(0x0fff);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
        QCOMPARE(
            provider.connectionSnapshot().protocolVersion.minor,
            Protocol::ControlledFaultResetMinor);
        QVERIFY(provider.connectionSnapshot().capability);
        QVERIFY(!provider.connectionSnapshot().capability->faultReset);
        QVERIFY(!provider.supportsControlCommand(Data::ControllerControlCommand::ResetFault));

        Data::ControllerControlRequest reset;
        reset.command = Data::ControllerControlCommand::ResetFault;
        reset.expectedLatchedFaults = quint64(1) << 15;
        reset.expectedAlarmSequence = TestAlarmSequence;
        const Utils::Result<> result = provider.executeControlCommand(reset);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("UNSUPPORTED (-14)")));
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.status,
            std::optional<qint32>(-14));
        QVERIFY(provider.connectionSnapshot().lastError);
        QCOMPARE(provider.connectionSnapshot().lastError->codeName, QStringLiteral("UNSUPPORTED"));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 0);
        QVERIFY(controller.violations().isEmpty());

        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.setFaultResetSafeCyclicRuntime(false);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        const Data::ControllerConnectionSnapshot before = provider.connectionSnapshot();
        QVERIFY(before.controllerState);
        QVERIFY(before.package);
        QCOMPARE(
            before.controllerState->serviceState, Data::ControllerServiceState::Fault);
        QVERIFY(!before.controllerState->applicationActive);
        QVERIFY(!before.controllerState->busOperational);
        QVERIFY(before.controllerState->safeOutput);
        QVERIFY(before.package->controllerState != Data::ControllerPackageState::Active);

        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults = before.controllerState->latchedFaults;
        control.expectedAlarmSequence = before.controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        const Data::ControllerConnectionSnapshot after = provider.connectionSnapshot();
        QVERIFY(after.controllerState);
        QCOMPARE(
            after.controllerState->serviceState, Data::ControllerServiceState::Shutdown);
        QVERIFY(!after.controllerState->currentFaults);
        QVERIFY(!after.controllerState->latchedFaults);
        QVERIFY(!after.controllerState->applicationActive);
        QVERIFY(!after.controllerState->busOperational);
        QVERIFY(!after.controllerState->safeOutput);
        QCOMPARE(after.package, before.package);
        QVERIFY(std::any_of(
            after.recentAlarms.cbegin(),
            after.recentAlarms.cend(),
            [](const Data::ControllerAlarmSummary &alarm) {
                return alarm.sequence == TestAlarmSequence + 1 && alarm.code == 5
                       && alarm.state == Data::ControllerAlarmState::Cleared
                       && alarm.faultMask == (quint64(1) << 15);
            }));

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
        QVERIFY(provider.supportsControlCommand(Data::ControllerControlCommand::ResetFault));
        QVERIFY(provider.connectionSnapshot().capability);
        QVERIFY(provider.connectionSnapshot().capability->faultReset);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        const Data::ControllerConnectionSnapshot before = provider.connectionSnapshot();
        QVERIFY(before.session);
        QVERIFY(before.session->ownsControlLease);
        QVERIFY(before.controllerState);
        QVERIFY(before.package);
        QCOMPARE(
            before.controllerState->serviceState, Data::ControllerServiceState::Fault);
        QCOMPARE(before.controllerState->currentFaults, quint64(0));
        QCOMPARE(before.controllerState->latchedFaults, quint64(1) << 15);
        QCOMPARE(before.controllerState->latestAlarmSequence, TestAlarmSequence);
        QVERIFY(before.controllerState->busOperational);
        QVERIFY(before.controllerState->safeOutput);
        QVERIFY(!before.controllerState->applicationActive);

        QList<quint16> resetStages;
        connect(
            &provider,
            &Core::ControllerConnectionProvider::connectionSnapshotChanged,
            &provider,
            [&provider, &resetStages] {
                const Data::ControllerControlProgress progress
                    = provider.connectionSnapshot().controlProgress;
                if (progress.command != Data::ControllerControlCommand::ResetFault
                    || progress.state != Data::ControllerControlState::Pending
                    || !progress.stage) {
                    return;
                }
                if (resetStages.isEmpty() || resetStages.constLast() != progress.stage)
                    resetStages.append(progress.stage);
            });

        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults = before.controllerState->latchedFaults;
        control.expectedAlarmSequence = before.controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_VERIFY_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state
                != Data::ControllerControlState::Pending,
            1000);
        const Data::ControllerConnectionSnapshot resetResult = provider.connectionSnapshot();
        QStringList resetStageNames;
        for (const quint16 stage : std::as_const(resetStages))
            resetStageNames.append(QString::number(stage));
        const QString resetDiagnosticBase
            = QStringLiteral("%1 | stages=%2 | requests=%3 | violations=%4")
                  .arg(
                      resetResult.controlProgress.detail,
                      resetStageNames.join(QLatin1Char(',')),
                      QString::number(
                          controller.requestCount(Protocol::MessageType::ResetFault)),
                      controller.violations().join(QStringLiteral("; ")));
        const QString resetDiagnostic
            = resetResult.lastError
                  ? QStringLiteral("%1 | %2 | %3")
                        .arg(
                            resetDiagnosticBase,
                            resetResult.lastError->summary,
                            resetResult.lastError->detail)
                  : resetDiagnosticBase;
        QVERIFY2(
            resetResult.controlProgress.state == Data::ControllerControlState::Succeeded,
            qPrintable(resetDiagnostic));
        QCOMPARE(resetStages, QList<quint16>({1, 2, 3, 4}));
        QCOMPARE(provider.connectionSnapshot().controlProgress.stage, quint16(4));
        QVERIFY(provider.connectionSnapshot().controlProgress.final);
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.status,
            std::optional<qint32>(0));
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.operationResult,
            std::optional<qint32>(0));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);

        const Data::ControllerConnectionSnapshot after = provider.connectionSnapshot();
        QVERIFY(after.controllerState);
        QCOMPARE(
            after.controllerState->serviceState,
            Data::ControllerServiceState::OperationalSafe);
        QCOMPARE(after.controllerState->currentFaults, quint64(0));
        QCOMPARE(after.controllerState->latchedFaults, quint64(0));
        QCOMPARE(after.controllerState->latestAlarmSequence, TestAlarmSequence + 1);
        QCOMPARE(after.package, before.package);
        QCOMPARE(
            after.controllerState->applicationActive,
            before.controllerState->applicationActive);
        QCOMPARE(
            after.controllerState->busOperational,
            before.controllerState->busOperational);
        QCOMPARE(after.controllerState->safeOutput, before.controllerState->safeOutput);

        const auto cleared = std::find_if(
            after.recentAlarms.crbegin(),
            after.recentAlarms.crend(),
            [](const Data::ControllerAlarmSummary &alarm) {
                return alarm.sequence == TestAlarmSequence + 1 && alarm.code == 5;
            });
        QVERIFY(cleared != after.recentAlarms.crend());
        QCOMPARE(cleared->codeName, Tr::tr("Fault cleared"));
        QCOMPARE(cleared->state, Data::ControllerAlarmState::Cleared);
        QCOMPARE(cleared->severity, Data::ControllerSeverity::Information);
        QCOMPARE(cleared->source, Data::ControllerAlarmSource::Service);
        QVERIFY(!cleared->latched);
        QCOMPARE(cleared->detail0, quint32(0));
        QCOMPARE(cleared->detail1, quint32(0));
        QCOMPARE(cleared->detail2, quint32(0));
        QCOMPARE(cleared->faultMask, quint64(1) << 15);

        control.expectedLatchedFaults = 0;
        control.expectedAlarmSequence = 0;
        QVERIFY(provider.executeControlCommand(control));
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded);
        QCOMPARE(provider.connectionSnapshot().controlProgress.stage, quint16(0));
        QVERIFY(provider.connectionSnapshot().controlProgress.final);
        QVERIFY(
            provider.connectionSnapshot().controlProgress.detail.contains(
                QStringLiteral("no controller request was sent"),
                Qt::CaseInsensitive));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::EnterConfigurationMode), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::DiscoverTopology), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::Start), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ControlledStop), 0);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.connectionSnapshot().controllerState);

        const quint32 latestSequence = TestAlarmSequence + 1;
        controller.rejectNextControl(
            Protocol::MessageType::ResetFault,
            -15,
            -9,
            latestSequence,
            3);
        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults
            = provider.connectionSnapshot().controllerState->latchedFaults;
        control.expectedAlarmSequence
            = provider.connectionSnapshot().controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Failed,
            1000);

        const Data::ControllerConnectionSnapshot rejected = provider.connectionSnapshot();
        QCOMPARE(rejected.controlProgress.stage, quint16(3));
        QVERIFY(rejected.controlProgress.final);
        QCOMPARE(rejected.controlProgress.status, std::optional<qint32>(-15));
        QCOMPARE(rejected.controlProgress.operationResult, std::optional<qint32>(-9));
        QVERIFY(
            rejected.controlProgress.detail.contains(
                QStringLiteral("ERR_STALE_CONFIRMATION (-9)")));
        QVERIFY(rejected.controlProgress.detail.contains(QString::number(latestSequence)));
        QVERIFY(rejected.lastError);
        QCOMPARE(rejected.lastError->codeName, QStringLiteral("CPU1_REJECTED"));
        QCOMPARE(rejected.lastError->operationResult, std::optional<qint32>(-9));
        QCOMPARE(rejected.lastError->sourceDetail, std::optional<quint64>(latestSequence));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.setFaultResetSafeCyclicRuntime(false);
        controller.setFaultResetControllerPackageActive(true);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.connectionSnapshot().controllerState);
        QVERIFY(!provider.connectionSnapshot().controllerState->busOperational);
        QVERIFY(provider.connectionSnapshot().package);
        QCOMPARE(
            provider.connectionSnapshot().package->controllerState,
            Data::ControllerPackageState::Active);

        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults
            = provider.connectionSnapshot().controllerState->latchedFaults;
        control.expectedAlarmSequence
            = provider.connectionSnapshot().controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Failed,
            1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Degraded);
        QVERIFY(
            provider.connectionSnapshot().controlProgress.detail.contains(
                QStringLiteral("active package without a safe cyclic runtime")));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.setFaultResetSafeCyclicRuntime(false);
        controller.setFaultResetTerminalServiceState(3);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.connectionSnapshot().controllerState);
        QVERIFY(!provider.connectionSnapshot().controllerState->busOperational);
        QVERIFY(provider.connectionSnapshot().controllerState->safeOutput);
        QVERIFY(provider.connectionSnapshot().package);
        QVERIFY(
            provider.connectionSnapshot().package->controllerState
            != Data::ControllerPackageState::Active);

        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults
            = provider.connectionSnapshot().controllerState->latchedFaults;
        control.expectedAlarmSequence
            = provider.connectionSnapshot().controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Failed,
            1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Degraded);
        QVERIFY(
            provider.connectionSnapshot().controlProgress.detail.contains(
                QStringLiteral("expected SHUTDOWN state")));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.omitFaultClearedEventOnce();
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.connectionSnapshot().controllerState);

        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults
            = provider.connectionSnapshot().controllerState->latchedFaults;
        control.expectedAlarmSequence
            = provider.connectionSnapshot().controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Failed,
            1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Degraded);
        QCOMPARE(provider.connectionSnapshot().controlProgress.stage, quint16(4));
        QVERIFY(provider.connectionSnapshot().controlProgress.final);
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.status,
            std::optional<qint32>(0));
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.expectedLatchedFaults,
            control.expectedLatchedFaults);
        QCOMPARE(
            provider.connectionSnapshot().controlProgress.expectedAlarmSequence,
            control.expectedAlarmSequence);
        QVERIFY(
            provider.connectionSnapshot().controlProgress.detail.contains(
                QStringLiteral("matching AlarmCleared event was not confirmed")));
        QVERIFY(provider.connectionSnapshot().lastError);
        QCOMPARE(
            provider.connectionSnapshot().lastError->operation,
            Data::ControllerOperation::ResetFault);
        QCOMPARE(provider.connectionSnapshot().lastError->codeName, QStringLiteral("OK"));
        QCOMPARE(provider.connectionSnapshot().lastError->code, std::optional<qint32>(0));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::FaultReset);
        controller.setFaultResetTerminalServiceState(8);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.connectionSnapshot().controllerState);
        QVERIFY(provider.connectionSnapshot().controllerState->busOperational);
        QVERIFY(provider.connectionSnapshot().controllerState->safeOutput);
        QVERIFY(!provider.connectionSnapshot().controllerState->applicationActive);

        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults
            = provider.connectionSnapshot().controllerState->latchedFaults;
        control.expectedAlarmSequence
            = provider.connectionSnapshot().controllerState->latestAlarmSequence;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Failed,
            1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Degraded);
        QVERIFY(provider.connectionSnapshot().controllerState);
        QCOMPARE(
            provider.connectionSnapshot().controllerState->serviceState,
            Data::ControllerServiceState::Shutdown);
        QVERIFY(
            provider.connectionSnapshot().controlProgress.detail.contains(
                QStringLiteral("expected OP_SAFE state")));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ResetFault), 1);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }
}

void EtherCATProductApiTests::testPackageDeploymentLifecycle()
{
    LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.supportsPackageDeployment());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    Data::ControllerPackageDeploymentRequest request;
    request.operationId = QStringLiteral("deploy-cfg813");
    request.configurationId = 813;
    request.artifact.resize(Protocol::BulkChunkMaximumBytes + 17);
    for (qsizetype index = 0; index < request.artifact.size(); ++index)
        request.artifact[index] = char(index & 0xff);

    QVERIFY(provider.deployPackage(request));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().packageDeploymentProgress.state,
        Data::ControllerPackageDeploymentState::Succeeded,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.sessionForTests()->refreshInProgressForTests(), 1000);

    const Data::ControllerPackageDeploymentProgress progress
        = provider.connectionSnapshot().packageDeploymentProgress;
    QCOMPARE(progress.operationId, request.operationId);
    QCOMPARE(
        progress.artifactSha256,
        QCryptographicHash::hash(request.artifact, QCryptographicHash::Sha256));
    QCOMPARE(progress.totalBytes, qint64(request.artifact.size()));
    QCOMPARE(progress.transferredBytes, qint64(request.artifact.size()));
    QVERIFY(progress.candidate);
    QCOMPARE(progress.candidate->slot, Data::ControllerSlot::A);
    QCOMPARE(progress.candidate->generation, quint64(55));
    QCOMPARE(progress.candidate->configurationId, request.configurationId);
    QVERIFY(progress.previousActive);
    QCOMPARE(progress.previousActive->slot, Data::ControllerSlot::B);
    QCOMPARE(progress.previousActive->generation, quint64(33));
    QCOMPARE(progress.previousActive->configurationId, quint64(44));
    QVERIFY(progress.startedAt.isValid());
    QVERIFY(progress.completedAt.isValid());
    QVERIFY(!progress.audit.isEmpty());
    for (qsizetype index = 0; index < progress.audit.size(); ++index)
        QCOMPARE(progress.audit.at(index).sequence, quint64(index + 1));

    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkChunk), 2);
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkCommit), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkAbort), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ValidatePackage), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ActivatePackage), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::RollbackPackage), 0);

    QVERIFY(provider.deployPackage(request));
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
    Data::ControllerPackageDeploymentRequest conflict = request;
    conflict.artifact.append('x');
    QVERIFY(!provider.deployPackage(conflict));
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);

    control.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testPackageDeploymentGuardsAndIdempotency()
{
    LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());

    Data::ControllerPackageDeploymentRequest request;
    request.operationId = QStringLiteral("validate-only");
    request.artifact = QByteArray("signed-controller-package");
    request.configurationId = 813;
    request.activate = false;
    QVERIFY(!provider.deployPackage(request));
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 0);

    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(!provider.deployPackage(request));
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 0);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    Data::ControllerPackageDeploymentRequest invalid = request;
    invalid.operationId = QStringLiteral(" bad-id");
    QVERIFY(!provider.deployPackage(invalid));
    invalid = request;
    invalid.artifact.clear();
    QVERIFY(!provider.deployPackage(invalid));
    invalid = request;
    invalid.artifact.resize(16 * 1024 * 1024 + 1);
    QVERIFY(!provider.deployPackage(invalid));
    invalid = request;
    invalid.configurationId = 0;
    QVERIFY(!provider.deployPackage(invalid));
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 0);

    QVERIFY(provider.deployPackage(request));
    QVERIFY(provider.deployPackage(request));
    Data::ControllerPackageDeploymentRequest conflict = request;
    conflict.configurationId = 814;
    QVERIFY(!provider.deployPackage(conflict));
    Data::ControllerPackageDeploymentRequest concurrent = request;
    concurrent.operationId = QStringLiteral("different-operation");
    QVERIFY(!provider.deployPackage(concurrent));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().packageDeploymentProgress.state,
        Data::ControllerPackageDeploymentState::Succeeded,
        2000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ValidatePackage), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ActivatePackage), 0);
    QVERIFY(provider.deployPackage(request));
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);

    Data::ControllerPackageDeploymentRequest second = request;
    second.operationId = QStringLiteral("validate-only-second");
    second.artifact.append("-second");
    second.configurationId = 815;
    QVERIFY(provider.deployPackage(second));
    QVERIFY(!provider.deployPackage(request));
    QCOMPARE(
        provider.connectionSnapshot().packageDeploymentProgress.operationId, second.operationId);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().packageDeploymentProgress.state,
        Data::ControllerPackageDeploymentState::Succeeded,
        2000);
    QCOMPARE(provider.connectionSnapshot().packageDeploymentProgress.operationId, second.operationId);
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 2);
    QVERIFY(provider.deployPackage(request));
    QCOMPARE(provider.connectionSnapshot().packageDeploymentProgress.operationId, request.operationId);
    QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 2);

    control.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testPackageDeploymentCancellationAndAbort()
{
    {
        LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        Data::ControllerPackageDeploymentRequest request;
        request.operationId = QStringLiteral("cancel-before-chunk");
        request.artifact = QByteArray("signed-controller-package");
        request.configurationId = 813;
        QVERIFY(provider.deployPackage(request));
        QVERIFY(provider.cancelPackageDeployment(request.operationId));
        QVERIFY(provider.cancelPackageDeployment(request.operationId));
        QVERIFY(
            provider.connectionSnapshot().packageDeploymentProgress.state
            == Data::ControllerPackageDeploymentState::Canceling);
        QVERIFY(!provider.disconnectFromController());
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().packageDeploymentProgress.state,
            Data::ControllerPackageDeploymentState::Canceled,
            1000);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkChunk), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkAbort), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkCommit), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ValidatePackage), 0);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
        QVERIFY(controller.violations().isEmpty());
    }

    {
        LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
        controller.rejectNextDeployment(Protocol::MessageType::BulkBegin, -21);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        Data::ControllerPackageDeploymentRequest request;
        request.operationId = QStringLiteral("begin-rejected");
        request.artifact = QByteArray("invalid-controller-package");
        request.configurationId = 818;
        QVERIFY(provider.deployPackage(request));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().packageDeploymentProgress.state,
            Data::ControllerPackageDeploymentState::Failed,
            1000);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkChunk), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkAbort), 0);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
        QVERIFY(controller.violations().isEmpty());
    }

    {
        LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
        controller.rejectNextDeployment(Protocol::MessageType::BulkCommit, -21);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        Data::ControllerPackageDeploymentRequest request;
        request.operationId = QStringLiteral("commit-rejected");
        request.artifact = QByteArray("invalid-controller-package");
        request.configurationId = 814;
        QVERIFY(provider.deployPackage(request));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().packageDeploymentProgress.state,
            Data::ControllerPackageDeploymentState::Failed,
            1000);
        const Data::ControllerPackageDeploymentProgress progress
            = provider.connectionSnapshot().packageDeploymentProgress;
        QCOMPARE(progress.status, std::optional<qint32>(-21));
        QVERIFY(progress.detail.contains(QStringLiteral("ECPKG_INVALID")));
        QVERIFY(provider.connectionSnapshot().lastError);
        QCOMPARE(provider.connectionSnapshot().lastError->codeName, QStringLiteral("ECPKG_INVALID"));
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkChunk), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkCommit), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkAbort), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ValidatePackage), 0);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
        QVERIFY(controller.violations().isEmpty());
    }

    {
        LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
        controller.rejectNextDeployment(
            Protocol::MessageType::ActivatePackage, -19, 3, true);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        Data::ControllerPackageDeploymentRequest request;
        request.operationId = QStringLiteral("activation-rejected");
        request.artifact = QByteArray("signed-controller-package");
        request.configurationId = 816;
        request.activate = true;
        request.rollbackOnActivationFailure = true;
        QVERIFY(provider.deployPackage(request));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().packageDeploymentProgress.state,
            Data::ControllerPackageDeploymentState::Failed,
            2000);
        const Data::ControllerPackageDeploymentProgress progress
            = provider.connectionSnapshot().packageDeploymentProgress;
        QCOMPARE(progress.status, std::optional<qint32>(-19));
        QVERIFY(progress.detail.contains(QStringLiteral("Rollback completed")));
        QVERIFY(progress.previousActive);
        QCOMPARE(progress.previousActive->slot, Data::ControllerSlot::B);
        QCOMPARE(progress.previousActive->generation, quint64(33));
        QCOMPARE(progress.previousActive->configurationId, quint64(44));
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkBegin), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkChunk), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkCommit), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ValidatePackage), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ActivatePackage), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::RollbackPackage), 1);
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.requestCount(Protocol::MessageType::GetPackageState) >= 3, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !provider.sessionForTests()->refreshInProgressForTests(), 1000);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
        QVERIFY(controller.violations().isEmpty());
    }

    {
        LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
        controller.rejectNextDeploymentPackageState(Protocol::MessageType::ActivatePackage, -16);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        Data::ControllerPackageDeploymentRequest request;
        request.operationId = QStringLiteral("activation-result-unknown");
        request.artifact = QByteArray("signed-controller-package");
        request.configurationId = 817;
        request.activate = true;
        request.rollbackOnActivationFailure = true;
        QVERIFY(provider.deployPackage(request));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().packageDeploymentProgress.state,
            Data::ControllerPackageDeploymentState::OutcomeUnknown,
            2000);
        QVERIFY(provider.connectionSnapshot().packageDeploymentProgress.detail.contains(
            QStringLiteral("final package state")));
        QCOMPARE(controller.requestCount(Protocol::MessageType::ActivatePackage), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::RollbackPackage), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.requestCount(Protocol::MessageType::GetPackageState) >= 2, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !provider.sessionForTests()->refreshInProgressForTests(), 1000);

        control.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
        QVERIFY(controller.violations().isEmpty());
    }

    const QList<QPair<quint16, bool>> malformedFailures{{0, true}, {1, false}};
    for (qsizetype index = 0; index < malformedFailures.size(); ++index) {
        const auto [stage, final] = malformedFailures.at(index);
        LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
        controller.rejectNextDeployment(
            Protocol::MessageType::ValidatePackage, -16, stage, final);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

        Data::ControllerControlRequest control;
        control.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(control));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            1000);

        Data::ControllerPackageDeploymentRequest request;
        request.operationId = QStringLiteral("malformed-failure-%1").arg(index);
        request.artifact = QByteArray("signed-controller-package");
        request.configurationId = 820 + index;
        QVERIFY(provider.deployPackage(request));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().packageDeploymentProgress.state,
            Data::ControllerPackageDeploymentState::OutcomeUnknown,
            2000);
        QCOMPARE(controller.requestCount(Protocol::MessageType::ValidatePackage), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::BulkAbort), 0);
        QCOMPARE(controller.requestCount(Protocol::MessageType::RollbackPackage), 0);

        provider.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
        QVERIFY(controller.violations().isEmpty());
    }
}

void EtherCATProductApiTests::testHardwareControlLifecycle()
{
    constexpr auto EnableVariable = "QTC_ETHER_CAT_PRODUCT_API_HARDWARE";
    constexpr auto TimingModeVariable = "QTC_ETHER_CAT_TIMING_MODE";
    constexpr auto HostVariable = "QTC_ETHER_CAT_PRODUCT_API_HOST";
    constexpr auto PortVariable = "QTC_ETHER_CAT_PRODUCT_API_PORT";
    constexpr int ConnectStepTimeoutMs = 20000;
    constexpr int ControlStepTimeoutMs = 55000;
    constexpr int StateStepTimeoutMs = 15000;

    if (qEnvironmentVariable(EnableVariable) != QStringLiteral("1")) {
        QSKIP("Set QTC_ETHER_CAT_PRODUCT_API_HARDWARE=1 to run the real-controller "
              "lifecycle acceptance test.");
    }

    const QString timingMode = qEnvironmentVariable(TimingModeVariable).trimmed();
    if (timingMode != QStringLiteral("auto") && timingMode != QStringLiteral("free_run")
        && timingMode != QStringLiteral("dc") && timingMode != QStringLiteral("snapshot_only")
        && timingMode != QStringLiteral("scan_only")
        && timingMode != QStringLiteral("free_run_rejection")
        && timingMode != QStringLiteral("fault_reset")) {
        QFAIL(
            "QTC_ETHER_CAT_TIMING_MODE must be set to exactly auto, free_run, dc, "
            "snapshot_only, scan_only, free_run_rejection, or fault_reset before the hardware "
            "test can run.");
    }
    const bool snapshotOnly = timingMode == QStringLiteral("snapshot_only");
    const bool scanOnly = timingMode == QStringLiteral("scan_only");
    const bool expectFreeRunRejection = timingMode == QStringLiteral("free_run_rejection");
    const bool faultResetOnly = timingMode == QStringLiteral("fault_reset");

    QString host = qEnvironmentVariable(HostVariable).trimmed();
    if (host.isEmpty())
        host = QStringLiteral("192.168.3.101");
    quint32 basePort = 15200;
    const QString portOverride = qEnvironmentVariable(PortVariable).trimmed();
    if (!portOverride.isEmpty()) {
        bool validPort = false;
        basePort = portOverride.toUInt(&validPort);
        if (!validPort || basePort < 1 || basePort > 65533) {
            QFAIL("QTC_ETHER_CAT_PRODUCT_API_PORT must be a base port between 1 and "
                  "65533.");
        }
    }

    ProductApiSession::EndpointSet endpoints{
        host,
        quint16(basePort),
        quint16(basePort + 1),
        quint16(basePort + 2),
        QStringLiteral("%1:%2").arg(host).arg(basePort),
    };
    ProductApiSession::Options options;
    options.connectTimeoutMs = 8000;
    options.handshakeTimeoutMs = 8000;
    options.requestTimeoutMs = 45000;
    options.reconnectInitialDelayMs = 250;
    options.reconnectMaximumDelayMs = 1000;
    options.reconnectAttempts = 0;

    qInfo().noquote() << "[Product API hardware] enabled endpoint=" << endpoints.endpointSummary
                      << "pushPort=" << endpoints.pushPort << "bulkPort=" << endpoints.bulkPort
                      << "timingMode=" << timingMode;

    ProductApiConnectionProvider provider(endpoints, options);
    const Data::ControllerConnectionRequest connectionRequest = requestFor(provider);
    QString failure;
    QStringList cleanupFailures;
    bool connectionStarted = false;
    bool runtimeStartAttempted = false;
    quint64 initialBootId = 0;
    Data::ControllerSlot initialActiveSlot = Data::ControllerSlot::None;
    quint64 initialActiveGeneration = 0;
    quint64 initialActiveConfigurationId = 0;
    std::optional<Data::ControllerPackageSummary> initialPackage;
    bool initialApplicationActive = false;
    bool initialBusOperational = false;
    quint64 expectedLatchedFaults = 0;
    quint32 expectedAlarmSequence = 0;
    Data::ControllerServiceState expectedResetServiceState
        = Data::ControllerServiceState::Unknown;

    const auto recordFailure = [&failure](const QString &detail) {
        if (failure.isEmpty())
            failure = detail;
        qWarning().noquote() << "[Product API hardware] failure:" << detail;
    };
    const auto connectedOrDegraded = [&provider] {
        const Data::ControllerConnectionState state = provider.connectionSnapshot().state;
        return state == Data::ControllerConnectionState::Connected
               || state == Data::ControllerConnectionState::Degraded;
    };
    const auto executeCommand = [&provider](
                                    const Data::ControllerControlRequest &request,
                                    const QString &label,
                                    int timeoutMs,
                                    QString *error) {
        const QString command = hardwareCommandName(request.command);
        QList<Data::ControllerControlProgress> observedProgress;
        const QMetaObject::Connection evidenceConnection = QObject::connect(
            &provider,
            &Core::ControllerConnectionProvider::connectionSnapshotChanged,
            &provider,
            [&provider, &request, &label, &command, &observedProgress] {
                const Data::ControllerControlProgress progress
                    = provider.connectionSnapshot().controlProgress;
                if (progress.command != request.command
                    || progress.state == Data::ControllerControlState::Idle
                    || (!observedProgress.isEmpty() && observedProgress.constLast() == progress)) {
                    return;
                }
                observedProgress.append(progress);
                const QString status = progress.status ? QString::number(*progress.status)
                                                       : QStringLiteral("none");
                const QString operationResult = progress.operationResult
                                                    ? QString::number(*progress.operationResult)
                                                    : QStringLiteral("none");
                qInfo().noquote() << "[Product API hardware]" << label << "command=" << command
                                  << "orderedStage=" << progress.stage
                                  << "state=" << int(progress.state) << "final=" << progress.final
                                  << "status=" << status << "operationResult=" << operationResult
                                  << "detail=" << progress.detail;
            });
        qInfo().noquote() << "[Product API hardware]" << label << "dispatching" << command;
        const Utils::Result<> dispatch = provider.executeControlCommand(request);
        if (!dispatch) {
            QObject::disconnect(evidenceConnection);
            *error = QStringLiteral("%1 could not be dispatched: %2").arg(command, dispatch.error());
            return false;
        }
        const bool completed = waitForHardwareCondition(
            [&provider, expected = request.command] {
                const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
                if (snapshot.controlProgress.command == expected
                    && (snapshot.controlProgress.state == Data::ControllerControlState::Succeeded
                        || snapshot.controlProgress.state == Data::ControllerControlState::Failed)) {
                    return true;
                }
                return snapshot.state == Data::ControllerConnectionState::Disconnected
                       || snapshot.state == Data::ControllerConnectionState::Failed;
            },
            timeoutMs);
        QObject::disconnect(evidenceConnection);
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        logHardwareSnapshot(label, snapshot);
        if (!completed || snapshot.controlProgress.command != request.command
            || snapshot.controlProgress.state == Data::ControllerControlState::Pending
            || snapshot.controlProgress.state == Data::ControllerControlState::Idle) {
            *error = QStringLiteral("%1 did not reach a terminal result within %2 ms.")
                         .arg(command)
                         .arg(timeoutMs);
            return false;
        }
        const QString status = snapshot.controlProgress.status
                                   ? QString::number(*snapshot.controlProgress.status)
                                   : QStringLiteral("none");
        qInfo().noquote() << "[Product API hardware]" << label << "command=" << command
                          << "result=" << int(snapshot.controlProgress.state)
                          << "stage=" << snapshot.controlProgress.stage
                          << "final=" << snapshot.controlProgress.final << "status=" << status
                          << "detail=" << snapshot.controlProgress.detail;
        if (snapshot.controlProgress.state != Data::ControllerControlState::Succeeded) {
            QString controllerError;
            if (snapshot.lastError) {
                controllerError = QStringLiteral(" [%1: %2]")
                                      .arg(snapshot.lastError->codeName, snapshot.lastError->detail);
            }
            *error = QStringLiteral("%1 failed at stage %2 with status %3: %4%5")
                         .arg(command)
                         .arg(snapshot.controlProgress.stage)
                         .arg(status, snapshot.controlProgress.detail, controllerError);
            return false;
        }
        return true;
    };
    const auto executeMainCommand =
        [&failure,
         &recordFailure,
         &executeCommand](const Data::ControllerControlRequest &request, const QString &label) {
            if (!failure.isEmpty())
                return false;
            QString error;
            if (executeCommand(request, label, ControlStepTimeoutMs, &error))
                return true;
            recordFailure(error);
            return false;
        };
    const auto waitForMainGate = [&failure, &recordFailure](const QString &label, auto &&predicate) {
        if (!failure.isEmpty())
            return false;
        if (!waitForHardwareCondition(predicate, StateStepTimeoutMs)) {
            recordFailure(QStringLiteral("%1 did not reach its required safety gate within %2 ms.")
                              .arg(label)
                              .arg(StateStepTimeoutMs));
            return false;
        }
        qInfo().noquote() << "[Product API hardware]" << label << "safety gate confirmed";
        return true;
    };

    if (!provider.isAvailable()) {
        recordFailure(QStringLiteral("The production Product API provider is unavailable."));
    } else if (
        connectionRequest.scope.projectId.isNull() || connectionRequest.scope.masterId.isNull()
        || connectionRequest.profileId.isNull()) {
        recordFailure(QStringLiteral("The production connection profile could not be created."));
    }

    if (failure.isEmpty()) {
        const Utils::Result<> connectResult = provider.connectToController(connectionRequest);
        if (!connectResult) {
            recordFailure(
                QStringLiteral("Connection could not be started: %1").arg(connectResult.error()));
        } else {
            connectionStarted = true;
            qInfo().noquote()
                << "[Product API hardware] connecting without a visible application window";
            const bool connectionFinished = waitForHardwareCondition(
                [&provider] {
                    const Data::ControllerConnectionState state
                        = provider.connectionSnapshot().state;
                    return state == Data::ControllerConnectionState::Connected
                           || state == Data::ControllerConnectionState::Degraded
                           || state == Data::ControllerConnectionState::Failed;
                },
                ConnectStepTimeoutMs);
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            logHardwareSnapshot(QStringLiteral("initial snapshot"), snapshot);
            if (!connectionFinished) {
                recordFailure(
                    QStringLiteral("The three-channel connection did not complete within %1 ms.")
                        .arg(ConnectStepTimeoutMs));
            } else if (snapshot.state != Data::ControllerConnectionState::Connected) {
                const QString detail = snapshot.lastError
                                           ? snapshot.lastError->detail
                                           : QStringLiteral("no controller error was reported");
                recordFailure(
                    QStringLiteral("The initial snapshot is not fully connected: %1").arg(detail));
            }
        }
    }

    if (snapshotOnly && failure.isEmpty()) {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        if (!snapshot.session || !snapshot.session->sessionId || !snapshot.session->bootId) {
            recordFailure(QStringLiteral("The read-only snapshot has no complete session identity."));
        } else if (snapshot.session->ownsControlLease) {
            recordFailure(QStringLiteral("The snapshot-only session unexpectedly owns control."));
        } else if (!snapshot.controllerState) {
            recordFailure(QStringLiteral("The read-only snapshot has no controller state."));
        } else if (snapshot.controllerState->controllerBootId != snapshot.session->bootId) {
            recordFailure(
                QStringLiteral("The read-only controller-state BootId does not match the session."));
        } else {
            qInfo().noquote()
                << "[Product API hardware] snapshot-only gate confirmed without acquiring control";
        }
    }

    if (snapshotOnly && connectionStarted
        && provider.connectionSnapshot().state != Data::ControllerConnectionState::Disconnected) {
        const Utils::Result<> disconnectResult = provider.disconnectFromController();
        if (!disconnectResult) {
            recordFailure(
                QStringLiteral("Snapshot-only disconnect could not be started: %1")
                    .arg(disconnectResult.error()));
        } else if (!waitForHardwareCondition(
                       [&provider] {
                           return provider.connectionSnapshot().state
                                  == Data::ControllerConnectionState::Disconnected;
                       },
                       ConnectStepTimeoutMs)) {
            recordFailure(
                QStringLiteral("Snapshot-only disconnect did not finish within %1 ms.")
                    .arg(ConnectStepTimeoutMs));
        } else {
            qInfo().noquote()
                << "[Product API hardware] snapshot-only acceptance disconnected";
        }
    }

    Data::ControllerControlCommand startCommand = Data::ControllerControlCommand::Start;
    if (timingMode == QStringLiteral("free_run") || expectFreeRunRejection)
        startCommand = Data::ControllerControlCommand::StartFreeRun;
    else if (timingMode == QStringLiteral("dc"))
        startCommand = Data::ControllerControlCommand::StartDistributedClocks;
    if (failure.isEmpty() && !snapshotOnly) {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        const bool explicitTimingMode = timingMode == QStringLiteral("free_run")
                                        || timingMode == QStringLiteral("dc")
                                        || expectFreeRunRejection;
        if (explicitTimingMode
            && (snapshot.protocolVersion.major != 1
                || snapshot.protocolVersion.minor < Protocol::ExplicitTimingModeMinor)) {
            recordFailure(
                QStringLiteral("The controller negotiated Product API %1.%2; explicit timing start "
                               "requires at least 1.%3.")
                    .arg(snapshot.protocolVersion.major)
                    .arg(snapshot.protocolVersion.minor)
                    .arg(Protocol::ExplicitTimingModeMinor));
        } else if (
            explicitTimingMode
            && (!snapshot.capability || !snapshot.capability->explicitTimingModeStart)) {
            recordFailure(
                QStringLiteral("The controller did not advertise explicitTimingModeStart before "
                               "lease acquisition."));
        } else if (
            faultResetOnly
            && (snapshot.protocolVersion.major != 1
                || snapshot.protocolVersion.minor < Protocol::ControlledFaultResetMinor)) {
            recordFailure(
                QStringLiteral("The controller negotiated Product API %1.%2; controlled fault "
                               "reset requires at least 1.%3.")
                    .arg(snapshot.protocolVersion.major)
                    .arg(snapshot.protocolVersion.minor)
                    .arg(Protocol::ControlledFaultResetMinor));
        } else if (
            faultResetOnly && (!snapshot.capability || !snapshot.capability->faultReset)) {
            recordFailure(QStringLiteral(
                "The controller did not advertise controlled fault reset before lease "
                "acquisition."));
        } else if (
            scanOnly
            && (!provider.supportsControlCommand(
                    Data::ControllerControlCommand::AcquireControl)
                || !provider.supportsControlCommand(
                    Data::ControllerControlCommand::EnterConfigurationMode)
                || !provider.supportsControlCommand(
                    Data::ControllerControlCommand::DiscoverTopology)
                || !provider.supportsControlCommand(
                    Data::ControllerControlCommand::ReleaseControl))) {
            recordFailure(QStringLiteral(
                "The production session does not expose the scan-only command set."));
        } else if (
            faultResetOnly
            && !provider.supportsControlCommand(Data::ControllerControlCommand::ResetFault)) {
            recordFailure(
                QStringLiteral("The production session does not expose controlled fault reset."));
        } else if (!scanOnly && !faultResetOnly
                   && !provider.supportsControlCommand(startCommand)) {
            recordFailure(
                QStringLiteral("The production session does not expose the requested %1 command.")
                    .arg(hardwareCommandName(startCommand)));
        } else if (snapshot.readOnly) {
            recordFailure(
                QStringLiteral("The negotiated Product API session is unexpectedly read-only."));
        } else if (!snapshot.session || !snapshot.session->sessionId || !snapshot.session->bootId) {
            recordFailure(QStringLiteral("The controller session identity is incomplete."));
        } else if (snapshot.session->ownsControlLease || snapshot.session->controlLeaseOwnerSessionId) {
            recordFailure(QStringLiteral(
                "A control lease is already owned; the hardware test will not take it over."));
        } else if (!snapshot.controllerState || !snapshot.controllerState->ready) {
            recordFailure(QStringLiteral("The controller is not ready before lease acquisition."));
        } else if (snapshot.controllerState->controllerBootId != snapshot.session->bootId) {
            recordFailure(
                QStringLiteral("The controller-state BootId does not match the session epoch."));
        } else if (!snapshot.package) {
            recordFailure(QStringLiteral("No authoritative package snapshot was returned."));
        } else if (
            scanOnly
            && (snapshot.controllerState->serviceState
                    != Data::ControllerServiceState::Shutdown
                || snapshot.controllerState->applicationActive
                || snapshot.controllerState->busOperational
                || snapshot.controllerState->fault
                || snapshot.controllerState->currentFaults
                || snapshot.controllerState->latchedFaults
                || snapshot.controllerState->ethercatAlStateBits
                || snapshot.controllerState->expectedWorkingCounter
                || snapshot.controllerState->actualWorkingCounter
                || snapshot.package->controllerState == Data::ControllerPackageState::Active)) {
            recordFailure(QStringLiteral(
                "Scan-only acceptance requires fault-free SHUTDOWN with no active bus or runtime "
                "package."));
        } else if (
            faultResetOnly
            && (snapshot.controllerState->serviceState != Data::ControllerServiceState::Fault
                || snapshot.controllerState->currentFaults
                || !snapshot.controllerState->latchedFaults
                || !snapshot.controllerState->latestAlarmSequence)) {
            recordFailure(QStringLiteral(
                "Fault-reset acceptance requires FAULT with current_faults=0, a nonzero full "
                "latched mask, and a nonzero alarm checkpoint."));
        } else if (
            faultResetOnly
            && !((snapshot.package->controllerState == Data::ControllerPackageState::Active
                 && !snapshot.controllerState->applicationActive
                  && snapshot.controllerState->busOperational
                  && snapshot.controllerState->safeOutput)
                 || (snapshot.package->controllerState
                         != Data::ControllerPackageState::Active
                     && !snapshot.controllerState->applicationActive
                     && !snapshot.controllerState->busOperational))) {
            recordFailure(QStringLiteral(
                "Fault-reset preflight cannot classify the controller as a safe cyclic runtime "
                "or a stopped management runtime."));
        } else if (!scanOnly && !faultResetOnly
                   && (snapshot.package->activeSlot == Data::ControllerSlot::None
                       || !snapshot.package->activeGeneration
                       || !snapshot.package->activeConfigurationId)) {
            recordFailure(QStringLiteral(
                "No exact persistent active-package selector was returned by preflight."));
        } else {
            using ServiceState = Data::ControllerServiceState;
            const ServiceState state = snapshot.controllerState->serviceState;
            const bool canEnterConfiguration
                = faultResetOnly
                      ? state == ServiceState::Fault
                  : scanOnly
                      ? state == ServiceState::Shutdown
                      : state == ServiceState::OperationalSafe || state == ServiceState::Running
                            || state == ServiceState::Paused || state == ServiceState::Fault
                            || state == ServiceState::Shutdown;
            if (!canEnterConfiguration) {
                recordFailure(
                    QStringLiteral("The initial controller state %1 cannot enter configuration.")
                        .arg(hardwareServiceStateName(state)));
            } else {
                initialBootId = snapshot.session->bootId;
                initialPackage = snapshot.package;
                if (faultResetOnly) {
                    initialApplicationActive = snapshot.controllerState->applicationActive;
                    initialBusOperational = snapshot.controllerState->busOperational;
                    expectedLatchedFaults = snapshot.controllerState->latchedFaults;
                    expectedAlarmSequence = snapshot.controllerState->latestAlarmSequence;
                    expectedResetServiceState
                        = initialBusOperational && snapshot.controllerState->safeOutput
                                  && !initialApplicationActive
                                  && snapshot.package->controllerState
                                         == Data::ControllerPackageState::Active
                              ? ServiceState::OperationalSafe
                              : ServiceState::Shutdown;
                    qInfo().noquote()
                        << "[Product API hardware] fault-reset preflight mask="
                        << QStringLiteral("0x%1")
                               .arg(expectedLatchedFaults, 5, 16, QLatin1Char('0'))
                        << "alarm=" << expectedAlarmSequence << "expectedService="
                        << hardwareServiceStateName(expectedResetServiceState)
                        << "boot=" << QString::number(initialBootId);
                } else if (!scanOnly) {
                    initialActiveSlot = snapshot.package->activeSlot;
                    initialActiveGeneration = snapshot.package->activeGeneration;
                    initialActiveConfigurationId = snapshot.package->activeConfigurationId;
                    qInfo().noquote() << "[Product API hardware] saved initial selector="
                                      << QStringLiteral("%1:%2:%3")
                                             .arg(hardwareSlotName(initialActiveSlot))
                                             .arg(initialActiveGeneration)
                                             .arg(initialActiveConfigurationId)
                                      << "boot=" << QString::number(initialBootId);
                } else {
                    qInfo().noquote()
                        << "[Product API hardware] scan-only preflight confirmed boot="
                        << QString::number(initialBootId);
                }
            }
        }
    }

    const auto sameSessionEpoch = [&provider, initialBootId] {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        return snapshot.session && snapshot.session->bootId == initialBootId;
    };
    const auto ownsLease = [&provider, &sameSessionEpoch] {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        return sameSessionEpoch() && snapshot.session && snapshot.session->ownsControlLease;
    };
    const auto exactActivePackage = [&provider,
                                     &sameSessionEpoch,
                                     initialActiveSlot,
                                     initialActiveGeneration,
                                     initialActiveConfigurationId,
                                     initialBootId] {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        return sameSessionEpoch() && snapshot.package
               && snapshot.package->activeSlot == initialActiveSlot
               && snapshot.package->activeGeneration == initialActiveGeneration
               && snapshot.package->activeConfigurationId == initialActiveConfigurationId
               && snapshot.package->controllerState == Data::ControllerPackageState::Active
               && snapshot.package->controllerBootId == initialBootId;
    };
    const auto strictOperationalSafeGate =
        [&provider, &ownsLease, &exactActivePackage, initialBootId] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            if (!ownsLease() || !exactActivePackage() || !snapshot.controllerState)
                return false;
            const Data::ControllerStateSummary &state = *snapshot.controllerState;
            return state.serviceState == Data::ControllerServiceState::OperationalSafe
                   && state.ready && state.busOperational && !state.applicationActive
                   && state.safeOutput && state.controllerBootId == initialBootId
                   && (state.ethercatAlStateBits & 0x08) && state.expectedWorkingCounter
                   && state.actualWorkingCounter == state.expectedWorkingCounter
                   && !state.currentFaults && !state.latchedFaults;
        };
    const auto strictRunningGate =
        [&provider, &ownsLease, &exactActivePackage, &timingMode, initialBootId] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            if (!ownsLease() || !exactActivePackage() || !snapshot.controllerState)
                return false;
            const Data::ControllerStateSummary &state = *snapshot.controllerState;
            return state.serviceState == Data::ControllerServiceState::Running && state.ready
                   && state.busOperational && state.applicationActive && !state.safeOutput
                   && state.controllerBootId == initialBootId && (state.ethercatAlStateBits & 0x08)
                   && state.expectedWorkingCounter
                   && state.actualWorkingCounter == state.expectedWorkingCounter
                   && !state.currentFaults && !state.latchedFaults
                   && (timingMode != QStringLiteral("dc") || state.distributedClocksLocked);
        };
    const auto strictPausedGate = [&provider, &ownsLease, &exactActivePackage, initialBootId] {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        if (!ownsLease() || !exactActivePackage() || !snapshot.controllerState)
            return false;
        const Data::ControllerStateSummary &state = *snapshot.controllerState;
        return state.serviceState == Data::ControllerServiceState::Paused && state.ready
               && state.busOperational && state.paused && state.controllerBootId == initialBootId
               && (state.ethercatAlStateBits & 0x08) && state.expectedWorkingCounter
               && state.actualWorkingCounter == state.expectedWorkingCounter && !state.currentFaults
               && !state.latchedFaults;
    };
    const auto strictShutdownGate = [&provider, &ownsLease, &sameSessionEpoch, initialBootId] {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        return ownsLease() && sameSessionEpoch() && snapshot.controllerState
               && snapshot.controllerState->ready
               && snapshot.controllerState->serviceState == Data::ControllerServiceState::Shutdown
               && snapshot.controllerState->controllerBootId == initialBootId
               && !snapshot.controllerState->applicationActive && snapshot.package
               && snapshot.package->controllerState != Data::ControllerPackageState::Active;
    };
    const auto strictFaultResetGate =
        [&provider,
         &ownsLease,
         &sameSessionEpoch,
         initialBootId,
         &initialPackage,
         initialApplicationActive,
         initialBusOperational,
         expectedLatchedFaults,
         expectedAlarmSequence,
         expectedResetServiceState] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            if (!ownsLease() || !sameSessionEpoch() || !snapshot.controllerState
                || snapshot.package != initialPackage) {
                return false;
            }
            const Data::ControllerStateSummary &state = *snapshot.controllerState;
            quint32 expectedClearedSequence = expectedAlarmSequence + 1;
            if (!expectedClearedSequence)
                expectedClearedSequence = 1;
            if (!state.ready || state.controllerBootId != initialBootId
                || state.serviceState != expectedResetServiceState || state.fault
                || state.currentFaults || state.latchedFaults
                || state.latestAlarmSequence != expectedClearedSequence
                || state.applicationActive != initialApplicationActive
                || state.busOperational != initialBusOperational
                || state.safeOutput
                       != (expectedResetServiceState
                           == Data::ControllerServiceState::OperationalSafe)) {
                return false;
            }
            return std::any_of(
                snapshot.recentAlarms.cbegin(),
                snapshot.recentAlarms.cend(),
                [expectedClearedSequence, expectedLatchedFaults](
                    const Data::ControllerAlarmSummary &alarm) {
                    return alarm.sequence == expectedClearedSequence && alarm.code == 5
                           && alarm.state == Data::ControllerAlarmState::Cleared
                           && alarm.severity == Data::ControllerSeverity::Information
                           && alarm.source == Data::ControllerAlarmSource::Service
                           && !alarm.latched && !alarm.detail0 && !alarm.detail1 && !alarm.detail2
                           && alarm.faultMask == expectedLatchedFaults;
                });
        };

    Data::ControllerControlRequest control;
    if (failure.isEmpty() && !snapshotOnly) {
        control.command = Data::ControllerControlCommand::AcquireControl;
        control.leaseDurationMs = 30000;
        executeMainCommand(control, QStringLiteral("acquire 30000 ms lease"));
        waitForMainGate(QStringLiteral("lease ownership"), ownsLease);
    }

    if (failure.isEmpty() && faultResetOnly) {
        control = {};
        control.command = Data::ControllerControlCommand::ResetFault;
        control.expectedLatchedFaults = expectedLatchedFaults;
        control.expectedAlarmSequence = expectedAlarmSequence;
        executeMainCommand(control, QStringLiteral("confirmed fault reset"));
        if (failure.isEmpty()) {
            const Data::ControllerControlProgress progress
                = provider.connectionSnapshot().controlProgress;
            if (progress.command != Data::ControllerControlCommand::ResetFault
                || progress.state != Data::ControllerControlState::Succeeded
                || progress.stage != 4 || !progress.final || progress.status != 0
                || progress.operationResult != 0) {
                recordFailure(QStringLiteral(
                    "ResetFault did not preserve the exact successful terminal stage-4 "
                    "CommandStatus."));
            }
        }
        waitForMainGate(QStringLiteral("strict AlarmCleared fault-reset outcome"),
                        strictFaultResetGate);
    }

    if (failure.isEmpty() && !snapshotOnly && !faultResetOnly) {
        control = {};
        control.command = Data::ControllerControlCommand::EnterConfigurationMode;
        executeMainCommand(control, QStringLiteral("enter configuration"));
        waitForMainGate(QStringLiteral("initial SHUTDOWN"), strictShutdownGate);
    }

    if (failure.isEmpty() && !snapshotOnly && !faultResetOnly) {
        control = {};
        control.command = Data::ControllerControlCommand::DiscoverTopology;
        control.firstStationAddress = 0x1001;
        control.topologyCapacity = 64;
        executeMainCommand(control, QStringLiteral("discover topology"));
        waitForMainGate(
            QStringLiteral("non-empty discovered topology"), [&provider, &strictShutdownGate] {
                const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
                return strictShutdownGate() && snapshot.topology
                       && snapshot.topology->respondingCount && !snapshot.topology->slaves.isEmpty()
                       && snapshot.topology->slaves.size()
                              == qsizetype(snapshot.topology->respondingCount);
            });
        if (failure.isEmpty()) {
            const Data::ControllerTopologySnapshot &topology
                = *provider.connectionSnapshot().topology;
            qInfo().noquote() << "[Product API hardware] discovered" << topology.respondingCount
                              << "slaves from station"
                              << QStringLiteral("0x%1")
                                     .arg(topology.firstStationAddress, 4, 16, QLatin1Char('0'));
            for (const Data::ControllerTopologySlave &slave : topology.slaves) {
                qInfo().noquote()
                    << "[Product API hardware] slave position=" << slave.position << "station="
                    << QStringLiteral("0x%1").arg(slave.stationAddress, 4, 16, QLatin1Char('0'))
                    << "vendor="
                    << QStringLiteral("0x%1").arg(slave.vendorId, 8, 16, QLatin1Char('0'))
                    << "product="
                    << QStringLiteral("0x%1").arg(slave.productCode, 8, 16, QLatin1Char('0'))
                    << "revision="
                    << QStringLiteral("0x%1").arg(slave.revision, 8, 16, QLatin1Char('0'))
                    << "serial="
                    << QStringLiteral("0x%1").arg(slave.serial, 8, 16, QLatin1Char('0'))
                    << "al="
                    << QStringLiteral("0x%1").arg(slave.alState, 2, 16, QLatin1Char('0'))
                    << "flags="
                    << QStringLiteral("0x%1").arg(slave.flags, 4, 16, QLatin1Char('0'));
            }
        }
    }

    if (failure.isEmpty() && !snapshotOnly && !scanOnly && !faultResetOnly) {
        control = {};
        control.command = Data::ControllerControlCommand::RestoreActivePackage;
        executeMainCommand(control, QStringLiteral("restore exact active package"));
        waitForMainGate(QStringLiteral("restored OP_SAFE"), strictOperationalSafeGate);
    }

    if (failure.isEmpty() && !snapshotOnly && !scanOnly && !faultResetOnly) {
        runtimeStartAttempted = true;
        control = {};
        control.command = startCommand;
        QString startLabel = QStringLiteral("automatic package-mode start");
        if (expectFreeRunRejection)
            startLabel = QStringLiteral("expected incompatible FreeRun start");
        else if (timingMode == QStringLiteral("free_run"))
            startLabel = QStringLiteral("explicit FreeRun start");
        else if (timingMode == QStringLiteral("dc"))
            startLabel = QStringLiteral("explicit DC start");
        if (expectFreeRunRejection) {
            QString rejectionDetail;
            if (executeCommand(control, startLabel, ControlStepTimeoutMs, &rejectionDetail)) {
                recordFailure(QStringLiteral(
                    "StartFreeRun unexpectedly succeeded for the DC-only active package."));
            } else {
                const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
                const bool exactRejection
                    = snapshot.controlProgress.command
                          == Data::ControllerControlCommand::StartFreeRun
                      && snapshot.controlProgress.state == Data::ControllerControlState::Failed
                      && snapshot.controlProgress.stage == 2 && snapshot.controlProgress.final
                      && snapshot.controlProgress.status
                      && *snapshot.controlProgress.status == qint32(-35) && snapshot.lastError
                      && snapshot.lastError->codeName == QStringLiteral("TIMING_MODE_MISMATCH");
                if (!exactRejection) {
                    recordFailure(QStringLiteral(
                                      "StartFreeRun did not return the exact terminal stage-2 "
                                      "TIMING_MODE_MISMATCH(-35) contract: %1")
                                      .arg(rejectionDetail));
                } else if (!strictOperationalSafeGate()) {
                    recordFailure(QStringLiteral(
                        "The rejected StartFreeRun request did not preserve the exact "
                        "fault-free OP_SAFE package state."));
                } else {
                    qInfo().noquote()
                        << "[Product API hardware] FreeRun incompatibility gate confirmed:"
                        << "TIMING_MODE_MISMATCH(-35), OP_SAFE retained, no application start";
                }
            }
        } else {
            executeMainCommand(control, startLabel);
            waitForMainGate(QStringLiteral("RUNNING"), strictRunningGate);
        }
    }

    if (failure.isEmpty() && !snapshotOnly && !scanOnly && !faultResetOnly
        && !expectFreeRunRejection) {
        control = {};
        control.command = Data::ControllerControlCommand::Pause;
        executeMainCommand(control, QStringLiteral("pause"));
        waitForMainGate(QStringLiteral("PAUSED"), strictPausedGate);
    }

    if (failure.isEmpty() && !snapshotOnly && !scanOnly && !faultResetOnly
        && !expectFreeRunRejection) {
        control = {};
        control.command = Data::ControllerControlCommand::Resume;
        executeMainCommand(control, QStringLiteral("resume"));
        waitForMainGate(QStringLiteral("resumed RUNNING"), strictRunningGate);
    }

    if (failure.isEmpty() && !snapshotOnly && !scanOnly && !faultResetOnly
        && !expectFreeRunRejection) {
        control = {};
        control.command = Data::ControllerControlCommand::ControlledStop;
        executeMainCommand(control, QStringLiteral("controlled stop"));
        waitForMainGate(QStringLiteral("stopped OP_SAFE"), strictOperationalSafeGate);
    }

    if (failure.isEmpty() && !snapshotOnly && !scanOnly && !faultResetOnly) {
        control = {};
        control.command = Data::ControllerControlCommand::EnterConfigurationMode;
        executeMainCommand(control, QStringLiteral("final enter configuration"));
        waitForMainGate(QStringLiteral("final SHUTDOWN"), strictShutdownGate);
    }

    if (failure.isEmpty() && !snapshotOnly) {
        control = {};
        control.command = Data::ControllerControlCommand::ReleaseControl;
        executeMainCommand(control, QStringLiteral("release control"));
        waitForMainGate(QStringLiteral("lease released"), [&provider, &sameSessionEpoch] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            return sameSessionEpoch() && snapshot.session && !snapshot.session->ownsControlLease;
        });
    }

    if (failure.isEmpty() && !snapshotOnly) {
        const Utils::Result<> disconnectResult = provider.disconnectFromController();
        if (!disconnectResult) {
            recordFailure(
                QStringLiteral("Disconnect could not be started: %1").arg(disconnectResult.error()));
        } else if (!waitForHardwareCondition(
                       [&provider] {
                           return provider.connectionSnapshot().state
                                  == Data::ControllerConnectionState::Disconnected;
                       },
                       ConnectStepTimeoutMs)) {
            recordFailure(
                QStringLiteral("Disconnect did not finish within %1 ms.").arg(ConnectStepTimeoutMs));
        } else {
            qInfo().noquote()
                << (scanOnly ? "[Product API hardware] scan-only acceptance completed in "
                               "SHUTDOWN and disconnected"
                    : faultResetOnly
                        ? "[Product API hardware] controlled fault-reset acceptance "
                          "completed with the runtime state preserved and disconnected"
                    : expectFreeRunRejection
                        ? "[Product API hardware] FreeRun incompatibility acceptance "
                          "completed in SHUTDOWN and disconnected"
                        : "[Product API hardware] lifecycle completed in SHUTDOWN "
                          "and disconnected");
        }
    }

    const auto appendCleanupFailure = [&cleanupFailures](const QString &detail) {
        cleanupFailures.append(detail);
        qWarning().noquote() << "[Product API hardware] cleanup:" << detail;
    };
    const auto cleanupCommand =
        [&appendCleanupFailure,
         &executeCommand](const Data::ControllerControlRequest &request, const QString &label) {
            QString error;
            if (executeCommand(request, label, ControlStepTimeoutMs, &error))
                return true;
            appendCleanupFailure(error);
            return false;
        };
    const auto refreshForCleanup = [&provider, &appendCleanupFailure, &connectedOrDegraded] {
        if (!connectedOrDegraded())
            return false;
        if (provider.connectionSnapshot().controlProgress.state
            == Data::ControllerControlState::Pending) {
            if (!waitForHardwareCondition(
                    [&provider] {
                        return provider.connectionSnapshot().controlProgress.state
                               != Data::ControllerControlState::Pending;
                    },
                    ControlStepTimeoutMs)) {
                appendCleanupFailure(QStringLiteral(
                    "An in-flight controller operation did not terminate before cleanup."));
                return false;
            }
        }
        QSignalSpy
            snapshots(&provider, &Core::ControllerConnectionProvider::connectionSnapshotChanged);
        const Utils::Result<> refreshResult = provider.refreshController();
        if (!refreshResult) {
            appendCleanupFailure(QStringLiteral("Authoritative cleanup refresh could not start: %1")
                                     .arg(refreshResult.error()));
            return false;
        }
        const bool refreshed = waitForHardwareCondition(
            [&provider, &snapshots] {
                return !snapshots.isEmpty()
                       && provider.sessionForTests()->pendingRequestCountForTests() == 0
                       && (provider.connectionSnapshot().state
                               == Data::ControllerConnectionState::Connected
                           || provider.connectionSnapshot().state
                                  == Data::ControllerConnectionState::Degraded);
            },
            ControlStepTimeoutMs);
        if (!refreshed) {
            appendCleanupFailure(
                QStringLiteral("Authoritative cleanup refresh did not finish in time."));
            return false;
        }
        logHardwareSnapshot(QStringLiteral("cleanup refresh"), provider.connectionSnapshot());
        return true;
    };

    if (!failure.isEmpty() && connectionStarted
        && provider.connectionSnapshot().state != Data::ControllerConnectionState::Disconnected) {
        qWarning().noquote()
            << "[Product API hardware] starting explicit best-effort safety cleanup";
        refreshForCleanup();

        Data::ControllerConnectionSnapshot cleanupSnapshot = provider.connectionSnapshot();
        bool cleanupOwnsLease = cleanupSnapshot.session
                                && cleanupSnapshot.session->ownsControlLease;
        const bool cleanupRuntimeState = cleanupSnapshot.controllerState
                                         && (cleanupSnapshot.controllerState->serviceState
                                                 == Data::ControllerServiceState::Running
                                             || cleanupSnapshot.controllerState->serviceState
                                                    == Data::ControllerServiceState::Paused);

        if (!cleanupOwnsLease && runtimeStartAttempted && cleanupRuntimeState
            && cleanupSnapshot.session
            && (!cleanupSnapshot.session->controlLeaseOwnerSessionId
                || cleanupSnapshot.session->controlLeaseOwnerSessionId
                       == cleanupSnapshot.session->sessionId)) {
            Data::ControllerControlRequest acquire;
            acquire.command = Data::ControllerControlCommand::AcquireControl;
            acquire.leaseDurationMs = 30000;
            cleanupCommand(acquire, QStringLiteral("cleanup reacquire control"));
            cleanupOwnsLease = provider.connectionSnapshot().session
                               && provider.connectionSnapshot().session->ownsControlLease;
        }

        cleanupSnapshot = provider.connectionSnapshot();
        if (!faultResetOnly && cleanupOwnsLease && cleanupSnapshot.controllerState
            && (cleanupSnapshot.controllerState->serviceState
                    == Data::ControllerServiceState::Running
                || cleanupSnapshot.controllerState->serviceState
                       == Data::ControllerServiceState::Paused)) {
            Data::ControllerControlRequest stop;
            stop.command = Data::ControllerControlCommand::ControlledStop;
            if (cleanupCommand(stop, QStringLiteral("cleanup controlled stop"))) {
                if (!waitForHardwareCondition(
                        [&provider] {
                            const auto snapshot = provider.connectionSnapshot();
                            return snapshot.controllerState
                                   && snapshot.controllerState->serviceState
                                          == Data::ControllerServiceState::OperationalSafe;
                        },
                        StateStepTimeoutMs)) {
                    appendCleanupFailure(QStringLiteral("Cleanup stop did not confirm OP_SAFE."));
                }
            }
        }

        cleanupSnapshot = provider.connectionSnapshot();
        if (!faultResetOnly && cleanupOwnsLease && cleanupSnapshot.controllerState
            && (cleanupSnapshot.controllerState->serviceState
                    == Data::ControllerServiceState::OperationalSafe
                || cleanupSnapshot.controllerState->serviceState
                       == Data::ControllerServiceState::Fault)) {
            Data::ControllerControlRequest enterConfiguration;
            enterConfiguration.command = Data::ControllerControlCommand::EnterConfigurationMode;
            if (cleanupCommand(enterConfiguration, QStringLiteral("cleanup enter configuration"))) {
                if (!waitForHardwareCondition(
                        [&provider] {
                            const auto snapshot = provider.connectionSnapshot();
                            return snapshot.controllerState
                                   && snapshot.controllerState->serviceState
                                          == Data::ControllerServiceState::Shutdown
                                   && snapshot.package
                                   && snapshot.package->controllerState
                                          != Data::ControllerPackageState::Active;
                        },
                        StateStepTimeoutMs)) {
                    appendCleanupFailure(QStringLiteral("Cleanup did not confirm SHUTDOWN."));
                }
            }
        }

        cleanupSnapshot = provider.connectionSnapshot();
        cleanupOwnsLease = cleanupSnapshot.session && cleanupSnapshot.session->ownsControlLease;
        const bool canRelease
            = faultResetOnly
                  ? cleanupOwnsLease
                  : cleanupOwnsLease && cleanupSnapshot.controllerState
                        && cleanupSnapshot.controllerState->ready
                        && (cleanupSnapshot.controllerState->serviceState
                                == Data::ControllerServiceState::Shutdown
                            || cleanupSnapshot.controllerState->serviceState
                                   == Data::ControllerServiceState::OperationalSafe);
        if (canRelease) {
            Data::ControllerControlRequest release;
            release.command = Data::ControllerControlCommand::ReleaseControl;
            cleanupCommand(release, QStringLiteral("cleanup release control"));
        } else if (cleanupOwnsLease) {
            appendCleanupFailure(QStringLiteral(
                "Cleanup retained the lease because no releasable terminal state was confirmed."));
        } else if (runtimeStartAttempted && cleanupRuntimeState) {
            appendCleanupFailure(QStringLiteral(
                "The controller was still RUNNING or PAUSED without a recoverable lease."));
        }

        cleanupSnapshot = provider.connectionSnapshot();
        cleanupOwnsLease = cleanupSnapshot.session && cleanupSnapshot.session->ownsControlLease;
        if (!cleanupOwnsLease) {
            const Utils::Result<> disconnectResult = provider.disconnectFromController();
            if (!disconnectResult) {
                appendCleanupFailure(QStringLiteral("Cleanup disconnect could not start: %1")
                                         .arg(disconnectResult.error()));
            } else if (!waitForHardwareCondition(
                           [&provider] {
                               return provider.connectionSnapshot().state
                                      == Data::ControllerConnectionState::Disconnected;
                           },
                           ConnectStepTimeoutMs)) {
                appendCleanupFailure(QStringLiteral("Cleanup disconnect did not finish in time."));
            }
        }
        logHardwareSnapshot(QStringLiteral("cleanup final"), provider.connectionSnapshot());
    }

    if (!cleanupFailures.isEmpty()) {
        const QString cleanupSummary
            = QStringLiteral("Cleanup issues: %1").arg(cleanupFailures.join(QStringLiteral(" | ")));
        if (failure.isEmpty())
            failure = cleanupSummary;
        else
            failure.append(QStringLiteral("\n") + cleanupSummary);
    }
    if (!failure.isEmpty())
        QFAIL(qPrintable(failure));
}

void EtherCATProductApiTests::testShutdownReleasePreservesAutonomousRuntime_data()
{
    QTest::addColumn<int>("targetState");

    QTest::newRow("shutdown") << int(Data::ControllerServiceState::Shutdown);
    QTest::newRow("running") << int(Data::ControllerServiceState::Running);
    QTest::newRow("paused") << int(Data::ControllerServiceState::Paused);
    QTest::newRow("pending-release") << -1;
}

void EtherCATProductApiTests::testShutdownReleasePreservesAutonomousRuntime()
{
    QFETCH(int, targetState);

    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    const auto execute = [&provider](Data::ControllerControlCommand command) {
        Data::ControllerControlRequest request;
        request.command = command;
        if (!provider.executeControlCommand(request))
            return false;
        return waitForHardwareCondition(
            [&provider, command] {
                const Data::ControllerControlProgress progress
                    = provider.connectionSnapshot().controlProgress;
                return progress.command == command
                       && progress.state == Data::ControllerControlState::Succeeded;
            },
            2000);
    };

    QVERIFY(execute(Data::ControllerControlCommand::AcquireControl));
    if (targetState == int(Data::ControllerServiceState::Running)
        || targetState == int(Data::ControllerServiceState::Paused)) {
        QVERIFY(execute(Data::ControllerControlCommand::EnterConfigurationMode));
        QVERIFY(execute(Data::ControllerControlCommand::RestoreActivePackage));
        QVERIFY(execute(Data::ControllerControlCommand::StartFreeRun));
        if (targetState == int(Data::ControllerServiceState::Paused))
            QVERIFY(execute(Data::ControllerControlCommand::Pause));
    } else if (targetState == -1) {
        controller.holdNextRelease();
        Data::ControllerControlRequest release;
        release.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(release));
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRelease(), 1000);
    }

    if (targetState >= 0) {
        QTRY_VERIFY_WITH_TIMEOUT(
            provider.connectionSnapshot().controllerState.has_value(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controllerState->serviceState,
            static_cast<Data::ControllerServiceState>(targetState),
            1000);
    }

    quint32 expectedServiceState = 8;
    if (targetState == int(Data::ControllerServiceState::Running))
        expectedServiceState = 4;
    else if (targetState == int(Data::ControllerServiceState::Paused))
        expectedServiceState = 9;
    const quint64 cycleBeforeShutdown = controller.cycleCount();
    provider.shutdown();
    QTest::qWait(100);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ControlledStop), 0);
    QCOMPARE(controller.serviceState(), expectedServiceState);
    QVERIFY(!controller.leaseOwned());
    if (expectedServiceState == 4)
        QVERIFY(controller.cycleCount() > cycleBeforeShutdown);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testDisconnectPreservesAutonomousRuntime()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    const auto execute = [&provider](Data::ControllerControlCommand command) {
        Data::ControllerControlRequest request;
        request.command = command;
        if (!provider.executeControlCommand(request))
            return false;
        return waitForHardwareCondition(
            [&provider, command] {
                const Data::ControllerControlProgress progress
                    = provider.connectionSnapshot().controlProgress;
                return progress.command == command
                       && progress.state == Data::ControllerControlState::Succeeded;
            },
            2000);
    };

    QVERIFY(execute(Data::ControllerControlCommand::AcquireControl));
    QVERIFY(execute(Data::ControllerControlCommand::EnterConfigurationMode));
    QVERIFY(execute(Data::ControllerControlCommand::RestoreActivePackage));
    QVERIFY(execute(Data::ControllerControlCommand::StartFreeRun));
    QCOMPARE(controller.serviceState(), quint32(4));
    QVERIFY(controller.leaseOwned());

    controller.holdNextRelease();
    const quint64 cycleBeforeDisconnect = controller.cycleCount();
    QVERIFY(provider.disconnectFromController());
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnecting);
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRelease(), 1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ControlledStop), 0);
    QCOMPARE(controller.serviceState(), quint32(4));
    QTest::qWait(20);
    QVERIFY(controller.cycleCount() > cycleBeforeDisconnect);

    controller.completeHeldRelease();
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QCOMPARE(controller.serviceState(), quint32(4));
    QVERIFY(!controller.leaseOwned());
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ControlledStop), 0);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testLeaseExpiryPreservesAutonomousRuntime()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    controller.setDefaultLeaseDurationMs(300);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 800;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    const auto execute = [&provider](Data::ControllerControlCommand command) {
        Data::ControllerControlRequest request;
        request.command = command;
        if (!provider.executeControlCommand(request))
            return false;
        return waitForHardwareCondition(
            [&provider, command] {
                const Data::ControllerControlProgress progress
                    = provider.connectionSnapshot().controlProgress;
                return progress.command == command
                       && progress.state == Data::ControllerControlState::Succeeded;
            },
            2000);
    };

    QVERIFY(execute(Data::ControllerControlCommand::AcquireControl));
    QVERIFY(execute(Data::ControllerControlCommand::EnterConfigurationMode));
    QVERIFY(execute(Data::ControllerControlCommand::RestoreActivePackage));
    QVERIFY(execute(Data::ControllerControlCommand::StartFreeRun));
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Running,
        1000);

    controller.holdNextHeartbeat();
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldHeartbeat(), 1000);
    const quint64 cycleBeforeExpiry = controller.cycleCount();
    controller.rejectHeldHeartbeat(-11);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().session
            && !provider.connectionSnapshot().session->ownsControlLease,
        1000);
    const Data::ControllerConnectionSnapshot expired = provider.connectionSnapshot();
    QVERIFY(expired.lastError);
    QCOMPARE(expired.lastError->codeName, QString("LEASE_EXPIRED"));
    QVERIFY(expired.controllerState);
    QCOMPARE(
        expired.controllerState->serviceState, Data::ControllerServiceState::Running);
    QCOMPARE(controller.serviceState(), quint32(4));
    QVERIFY(!controller.leaseOwned());
    QTest::qWait(20);
    QVERIFY(controller.cycleCount() > cycleBeforeExpiry);

    Data::ControllerControlRequest stop;
    stop.command = Data::ControllerControlCommand::ControlledStop;
    const Utils::Result<> stopWithoutLease = provider.executeControlCommand(stop);
    QVERIFY(!stopWithoutLease);
    QCOMPARE(
        stopWithoutLease.error(), Tr::tr("Acquire the control lease before this operation."));
    QCOMPARE(controller.requestCount(Protocol::MessageType::ControlledStop), 0);

    QVERIFY(execute(Data::ControllerControlCommand::AcquireControl));
    QVERIFY(execute(Data::ControllerControlCommand::ControlledStop));
    QCOMPARE(controller.serviceState(), quint32(3));
    QVERIFY(provider.disconnectFromController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testDisconnectWaitsForRelease()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    controller.setDefaultLeaseDurationMs(300);
    controller.holdNextHeartbeat();
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 1500;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldHeartbeat(), 1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Heartbeat), 1);

    controller.holdNextRelease();
    QVERIFY(provider.disconnectFromController());
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnecting);
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRelease(), 1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);

    controller.rejectHeldHeartbeat(InternalStatus);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnecting,
        1000);
    QTest::qWait(300);
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnecting);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Heartbeat), 1);

    controller.completeHeldRelease();
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QTest::qWait(100);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testHeartbeatTimeoutDoesNotPreemptDisconnectRelease()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    controller.setDefaultLeaseDurationMs(300);
    controller.holdNextHeartbeat();
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 800;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldHeartbeat(), 1000);
    QTest::qWait(300);

    controller.holdNextRelease();
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRelease(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().lastError
            && provider.connectionSnapshot().lastError->operation
                   == Data::ControllerOperation::Heartbeat
            && provider.connectionSnapshot().lastError->summary
                   == Tr::tr("The controller response timed out."),
        1000);

    const Data::ControllerConnectionSnapshot heartbeatTimeoutSnapshot
        = provider.connectionSnapshot();
    QCOMPARE(
        heartbeatTimeoutSnapshot.state,
        Data::ControllerConnectionState::Disconnecting);
    QVERIFY(heartbeatTimeoutSnapshot.session);
    QVERIFY(heartbeatTimeoutSnapshot.session->ownsControlLease);
    QCOMPARE(
        heartbeatTimeoutSnapshot.controlProgress.command,
        Data::ControllerControlCommand::ReleaseControl);
    QCOMPARE(
        heartbeatTimeoutSnapshot.controlProgress.state,
        Data::ControllerControlState::Pending);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 1);

    controller.completeHeldHeartbeat();
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().lastError
            && provider.connectionSnapshot().lastError->summary
                   == Tr::tr("The controller returned an unknown RequestId."),
        1000);
    const Data::ControllerConnectionSnapshot lateHeartbeatSnapshot
        = provider.connectionSnapshot();
    QCOMPARE(
        lateHeartbeatSnapshot.state,
        Data::ControllerConnectionState::Disconnecting);
    QVERIFY(lateHeartbeatSnapshot.session);
    QVERIFY(lateHeartbeatSnapshot.session->ownsControlLease);
    QCOMPARE(
        lateHeartbeatSnapshot.controlProgress.command,
        Data::ControllerControlCommand::ReleaseControl);
    QCOMPARE(
        lateHeartbeatSnapshot.controlProgress.state,
        Data::ControllerControlState::Pending);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);

    controller.completeHeldRelease();
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testLongControlKeepsHeartbeatResponseWindow_data()
{
    QTest::addColumn<bool>("heartbeatAlreadyPending");

    QTest::newRow("heartbeat-before-restore") << true;
    QTest::newRow("heartbeat-during-restore") << false;
}

void EtherCATProductApiTests::testLongControlKeepsHeartbeatResponseWindow()
{
    QFETCH(bool, heartbeatAlreadyPending);

    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    controller.setDefaultLeaseDurationMs(300);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 120;
    options.reconnectAttempts = 0;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    control.command = Data::ControllerControlCommand::EnterConfigurationMode;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    if (heartbeatAlreadyPending) {
        controller.holdNextHeartbeat();
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldHeartbeat(), 1000);
    }
    controller.holdNextRestore();
    control.command = Data::ControllerControlCommand::RestoreActivePackage;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRestore(), 1000);
    if (!heartbeatAlreadyPending) {
        controller.holdNextHeartbeat();
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldHeartbeat(), 1000);
    }

    QTest::qWait(options.requestTimeoutMs * 2);
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
    QCOMPARE(
        provider.connectionSnapshot().controlProgress.command,
        Data::ControllerControlCommand::RestoreActivePackage);
    QCOMPARE(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Pending);
    QVERIFY(provider.connectionSnapshot().session);
    QVERIFY(provider.connectionSnapshot().session->ownsControlLease);

    controller.completeHeldRestore();
    controller.completeHeldHeartbeat();
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe,
        2000);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testDisconnectRejectedReleasePreservesSession()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QTRY_COMPARE_WITH_TIMEOUT(provider.sessionForTests()->pendingRequestCountForTests(), 0, 1000);
    const int heartbeatCount = controller.requestCount(Protocol::MessageType::Heartbeat);
    QVERIFY(heartbeatCount > 0);

    controller.rejectNextControl(Protocol::MessageType::ReleaseControl, InternalStatus);
    QVERIFY(provider.disconnectFromController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Failed,
        1000);

    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 1000);
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QVERIFY(snapshot.session);
    QVERIFY(snapshot.session->ownsControlLease);
    QCOMPARE(snapshot.session->controlLeaseOwnerSessionId, snapshot.session->sessionId);
    QVERIFY(snapshot.lastError);
    QCOMPARE(snapshot.lastError->code, std::optional<qint32>(InternalStatus));
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.requestCount(Protocol::MessageType::Heartbeat) > heartbeatCount, 1000);
    QVERIFY(!provider.sessionForTests()->isIdleForTests());

    QVERIFY(provider.disconnectFromController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 2);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testDisconnectReleaseWriteFailure()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    const int heartbeatCount = controller.requestCount(Protocol::MessageType::Heartbeat);
    QVERIFY(heartbeatCount > 0);

    provider.sessionForTests()->failNextWriteForTests();
    QVERIFY(!provider.disconnectFromController());
    const Data::ControllerConnectionSnapshot failedSnapshot = provider.connectionSnapshot();
    QCOMPARE(failedSnapshot.state, Data::ControllerConnectionState::Connected);
    QVERIFY(failedSnapshot.session);
    QVERIFY(failedSnapshot.session->ownsControlLease);
    QCOMPARE(
        failedSnapshot.session->controlLeaseOwnerSessionId,
        failedSnapshot.session->sessionId);
    QVERIFY(failedSnapshot.lastError);
    QCOMPARE(failedSnapshot.lastError->source, Data::ControllerErrorSource::Network);
    QCOMPARE(
        failedSnapshot.lastError->operation,
        Data::ControllerOperation::ReleaseControl);
    QCOMPARE(
        failedSnapshot.lastError->summary,
        Tr::tr("The controller request could not be queued."));
    QCOMPARE(
        failedSnapshot.lastError->detail,
        Tr::tr(
            "The Release control request was not queued; the controller still reports this "
            "session as the lease owner."));
    QVERIFY(!provider.sessionForTests()->isIdleForTests());
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.requestCount(Protocol::MessageType::Heartbeat) > heartbeatCount,
        1000);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 0);

    QVERIFY(provider.disconnectFromController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testDisconnectReleaseTimeoutPreservesEvidence()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 100;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    controller.holdNextRelease();
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRelease(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);

    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QVERIFY(snapshot.lastError);
    QCOMPARE(snapshot.lastError->source, Data::ControllerErrorSource::Network);
    QCOMPARE(snapshot.lastError->operation, Data::ControllerOperation::ReleaseControl);
    QCOMPARE(snapshot.lastError->summary, Tr::tr("The controller response timed out."));
    QCOMPARE(
        snapshot.lastError->detail,
        Tr::tr(
            "The Release control result is unknown because the connection ended before "
            "confirmation."));
    QCOMPARE(snapshot.controlProgress.state, Data::ControllerControlState::Failed);
    QVERIFY(
        snapshot.controlProgress.detail.contains(
            Tr::tr("The controller operation timed out; its final state is unknown.")));
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testRejectedControlRefreshStillAllowsRelease()
{
    LoopbackController controller(LoopbackController::Behavior::ControlLifecycle);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    controller.rejectNextControl(Protocol::MessageType::EnterConfigurationMode, -14);
    controller.rejectNextState(-14);
    control.command = Data::ControllerControlCommand::EnterConfigurationMode;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Failed,
        1000);

    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QCOMPARE(snapshot.state, Data::ControllerConnectionState::Degraded);
    QVERIFY(snapshot.controlProgress.final);
    QVERIFY(snapshot.controlProgress.status);
    QCOMPARE(*snapshot.controlProgress.status, qint32(-14));
    QCOMPARE(
        snapshot.controlProgress.detail,
        Tr::tr(
            "%1 was rejected; the authoritative controller state could not be refreshed.")
            .arg(Tr::tr("Enter configuration mode")));
    QVERIFY(!snapshot.controllerState);
    QVERIFY(!snapshot.package);

    control.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);
    QVERIFY(provider.connectionSnapshot().session);
    QVERIFY(!provider.connectionSnapshot().session->ownsControlLease);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QVERIFY(provider.disconnectFromController());
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testControllerErrorAttribution_data()
{
    QTest::addColumn<int>("behavior");
    QTest::addColumn<int>("operation");
    QTest::addColumn<int>("requestType");
    QTest::addColumn<int>("code");
    QTest::addColumn<int>("operationResult");
    QTest::addColumn<QString>("channelId");
    QTest::addColumn<bool>("hasDetail");
    QTest::addColumn<qulonglong>("detail");
    QTest::addColumn<int>("expectedConnectionState");

    QTest::newRow("command-status")
        << int(LoopbackController::Behavior::CommandError)
        << int(Data::ControllerOperation::QueryState)
        << int(Protocol::MessageType::GetState) << -16 << -5 << QString("control") << true
        << qulonglong(CommandStatusDetail) << int(Data::ControllerConnectionState::Disconnected);
    QTest::newRow("bulk-status")
        << int(LoopbackController::Behavior::BulkError)
        << int(Data::ControllerOperation::QueryCapability)
        << int(Protocol::MessageType::GetCapability) << -20 << -6 << QString("bulk") << false
        << qulonglong(0) << int(Data::ControllerConnectionState::Degraded);
    QTest::newRow("package-state-error")
        << int(LoopbackController::Behavior::PackageError)
        << int(Data::ControllerOperation::QueryPackageState)
        << int(Protocol::MessageType::GetPackageState) << -17 << -3 << QString("control") << false
        << qulonglong(0) << int(Data::ControllerConnectionState::Degraded);
    QTest::newRow("firmware-state-error")
        << int(LoopbackController::Behavior::FirmwareError)
        << int(Data::ControllerOperation::QueryFirmwareState)
        << int(Protocol::MessageType::GetFirmwareState) << -28 << -4 << QString("control") << true
        << qulonglong(FirmwareStateDetail) << int(Data::ControllerConnectionState::Degraded);
}

void EtherCATProductApiTests::testControllerErrorAttribution()
{
    QFETCH(int, behavior);
    QFETCH(int, operation);
    QFETCH(int, requestType);
    QFETCH(int, code);
    QFETCH(int, operationResult);
    QFETCH(QString, channelId);
    QFETCH(bool, hasDetail);
    QFETCH(qulonglong, detail);
    QFETCH(int, expectedConnectionState);

    LoopbackController controller(static_cast<LoopbackController::Behavior>(behavior));
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));

    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              static_cast<Data::ControllerConnectionState>(
                                  expectedConnectionState),
                              2000);
    QTRY_COMPARE_WITH_TIMEOUT(provider.sessionForTests()->pendingRequestCountForTests(), 0, 1000);
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QVERIFY(snapshot.lastError);
    const Data::ControllerOperationError &error = *snapshot.lastError;
    QCOMPARE(error.source, Data::ControllerErrorSource::Controller);
    QCOMPARE(error.operation, static_cast<Data::ControllerOperation>(operation));
    QCOMPARE(error.channelId, channelId);
    QVERIFY(error.code);
    QCOMPARE(*error.code, qint32(code));
    QVERIFY(error.operationResult);
    QCOMPARE(*error.operationResult, qint32(operationResult));
    QVERIFY(error.requestId);
    QCOMPARE(*error.requestId,
             controller.lastRequestId(static_cast<Protocol::MessageType>(requestType)));
    QCOMPARE(error.sourceDetail.has_value(), hasDetail);
    if (hasDetail)
        QCOMPARE(*error.sourceDetail, quint64(detail));
    QVERIFY(!error.codeName.isEmpty());
    if (code == -20) {
        QCOMPARE(
            error.detail,
            Tr::tr(
                "The package capability descriptor does not match this controller. Rebuild the "
                "package from the current Capability descriptor."));
    }
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
}

void EtherCATProductApiTests::testMalformedControllerStatus_data()
{
    QTest::addColumn<int>("behavior");
    QTest::addColumn<int>("operation");
    QTest::addColumn<int>("requestType");

    QTest::newRow("command-length")
        << int(LoopbackController::Behavior::CommandWrongLength)
        << int(Data::ControllerOperation::QueryState) << int(Protocol::MessageType::GetState);
    QTest::newRow("command-flags")
        << int(LoopbackController::Behavior::CommandWrongFlags)
        << int(Data::ControllerOperation::QueryState) << int(Protocol::MessageType::GetState);
    QTest::newRow("command-original-type")
        << int(LoopbackController::Behavior::CommandWrongOriginalType)
        << int(Data::ControllerOperation::QueryState) << int(Protocol::MessageType::GetState);
    QTest::newRow("command-stage")
        << int(LoopbackController::Behavior::CommandWrongStage)
        << int(Data::ControllerOperation::QueryState) << int(Protocol::MessageType::GetState);
    QTest::newRow("command-final")
        << int(LoopbackController::Behavior::CommandWrongFinal)
        << int(Data::ControllerOperation::QueryState) << int(Protocol::MessageType::GetState);
    QTest::newRow("bulk-length")
        << int(LoopbackController::Behavior::BulkWrongLength)
        << int(Data::ControllerOperation::QueryCapability)
        << int(Protocol::MessageType::GetCapability);
    QTest::newRow("bulk-flags")
        << int(LoopbackController::Behavior::BulkWrongFlags)
        << int(Data::ControllerOperation::QueryCapability)
        << int(Protocol::MessageType::GetCapability);
    QTest::newRow("bulk-original-type")
        << int(LoopbackController::Behavior::BulkWrongOriginalType)
        << int(Data::ControllerOperation::QueryCapability)
        << int(Protocol::MessageType::GetCapability);
    QTest::newRow("bulk-reserved")
        << int(LoopbackController::Behavior::BulkReserved)
        << int(Data::ControllerOperation::QueryCapability)
        << int(Protocol::MessageType::GetCapability);
}

void EtherCATProductApiTests::testMalformedControllerStatus()
{
    QFETCH(int, behavior);
    QFETCH(int, operation);
    QFETCH(int, requestType);

    LoopbackController controller(static_cast<LoopbackController::Behavior>(behavior));
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));

    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Disconnected,
                              2000);
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QVERIFY(snapshot.lastError);
    const Data::ControllerOperationError &error = *snapshot.lastError;
    QCOMPARE(error.source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(error.operation, static_cast<Data::ControllerOperation>(operation));
    QVERIFY(!error.code);
    QVERIFY(!error.operationResult);
    QVERIFY(!error.sourceDetail);
    QVERIFY(error.requestId);
    QCOMPARE(*error.requestId,
             controller.lastRequestId(static_cast<Protocol::MessageType>(requestType)));
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
}

void EtherCATProductApiTests::testSessionCapacityRetry()
{
    LoopbackController controller(LoopbackController::Behavior::CapacityThenSuccess);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.reconnectInitialDelayMs = 10;
    options.reconnectMaximumDelayMs = 250;
    options.reconnectAttempts = 1;

    ProductApiConnectionProvider provider(controller.endpoints(), options);
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));

    QTRY_COMPARE_WITH_TIMEOUT(controller.controlHelloRequestIds().size(), 1, 500);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().lastError
            && provider.connectionSnapshot().lastError->code == std::optional<qint32>(-26),
        500);
    const Data::ControllerOperationError capacityError
        = *provider.connectionSnapshot().lastError;
    QCOMPARE(capacityError.source, Data::ControllerErrorSource::Controller);
    QCOMPARE(capacityError.operation, Data::ControllerOperation::Handshake);
    QCOMPARE(capacityError.retryDisposition, Data::ControllerRetryDisposition::Retryable);
    QVERIFY(capacityError.retryAfterMs);
    QCOMPARE(*capacityError.retryAfterMs, CapacityRetryAfterMs);
    QVERIFY(capacityError.requestId);
    QCOMPARE(*capacityError.requestId, controller.controlHelloRequestIds().constFirst());

    QTRY_COMPARE_WITH_TIMEOUT(controller.controlHelloRequestIds().size(), 2, 1000);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 2);
    QCOMPARE(controller.acceptCount(Protocol::Role::Push), 0);
    QCOMPARE(controller.acceptCount(Protocol::Role::Bulk), 0);
    QVERIFY(controller.controlHelloRequestIds().at(0)
            != controller.controlHelloRequestIds().at(1));
    QVERIFY(controller.controlHelloTimesMs().at(1) - controller.controlHelloTimesMs().at(0)
            >= CapacityRetryAfterMs);
    QVERIFY(controller.violations().isEmpty());

    controller.allowCapacitySuccess();
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
}

void EtherCATProductApiTests::testMalformedSessionCapacity()
{
    LoopbackController controller(LoopbackController::Behavior::MalformedCapacity);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));

    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Disconnected,
                              1000);
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QVERIFY(snapshot.lastError);
    QCOMPARE(snapshot.lastError->source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(snapshot.lastError->operation, Data::ControllerOperation::Handshake);
    QVERIFY(!snapshot.lastError->code);
    QVERIFY(!snapshot.lastError->operationResult);
    QVERIFY(snapshot.lastError->requestId);
    QCOMPARE(*snapshot.lastError->requestId,
             controller.controlHelloRequestIds().constFirst());
    QTest::qWait(CapacityRetryAfterMs + 50);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
}

void EtherCATProductApiTests::testSessionTimeoutAndShutdown()
{
    LoopbackController controller(LoopbackController::Behavior::SilentHello);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.handshakeTimeoutMs = 40;
    options.reconnectAttempts = 0;

    ProductApiConnectionProvider provider(controller.endpoints(), options);
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Disconnected,
                              1000);
    const Data::ControllerConnectionSnapshot failed = provider.connectionSnapshot();
    QVERIFY(failed.lastError);
    QCOMPARE(failed.lastError->source, Data::ControllerErrorSource::Network);
    QCOMPARE(failed.lastError->operation, Data::ControllerOperation::Handshake);
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QVERIFY(controller.violations().isEmpty());

    provider.shutdown();
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QVERIFY(!provider.isAvailable());
    provider.shutdown();
}

void EtherCATProductApiTests::testInFlightShutdown()
{
    LoopbackController controller(LoopbackController::Behavior::HoldCapability);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 80;

    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QSignalSpy snapshots(&provider, &Core::ControllerConnectionProvider::connectionSnapshotChanged);
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));

    QTRY_VERIFY_WITH_TIMEOUT(controller.hasHeldRequest(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(provider.sessionForTests()->pendingRequestCountForTests(), 1, 1000);
    const quint64 generationBeforeShutdown
        = provider.connectionSnapshot().sessionGeneration;
    const std::array<int, 3> acceptCountsBeforeShutdown{
        controller.acceptCount(Protocol::Role::Control),
        controller.acceptCount(Protocol::Role::Push),
        controller.acceptCount(Protocol::Role::Bulk),
    };

    provider.shutdown();
    const Data::ControllerConnectionSnapshot shutdownSnapshot = provider.connectionSnapshot();
    QCOMPARE(shutdownSnapshot.state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(shutdownSnapshot.sessionGeneration > generationBeforeShutdown);
    QVERIFY(!shutdownSnapshot.session);
    QVERIFY(!shutdownSnapshot.lastHeartbeatAt.isValid());
    for (const Data::ControllerChannelStatus &channel : shutdownSnapshot.channels)
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QVERIFY(!provider.isAvailable());

    const int snapshotCountAfterShutdown = snapshots.count();
    const quint64 generationAfterShutdown = shutdownSnapshot.sessionGeneration;
    controller.releaseHeldResponse();
    QTest::qWait(options.requestTimeoutMs * 2);

    QCOMPARE(snapshots.count(), snapshotCountAfterShutdown);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, generationAfterShutdown);
    QCOMPARE(provider.connectionSnapshot().state,
             Data::ControllerConnectionState::Disconnected);
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
    QCOMPARE(controller.acceptCount(Protocol::Role::Control),
             acceptCountsBeforeShutdown.at(0));
    QCOMPARE(controller.acceptCount(Protocol::Role::Push),
             acceptCountsBeforeShutdown.at(1));
    QCOMPARE(controller.acceptCount(Protocol::Role::Bulk),
             acceptCountsBeforeShutdown.at(2));
    QVERIFY(controller.violations().isEmpty());

    provider.shutdown();
    QCOMPARE(snapshots.count(), snapshotCountAfterShutdown);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, generationAfterShutdown);
}

void EtherCATProductApiTests::testSessionReconnectAndGeneration()
{
    LoopbackController controller;
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QCOMPARE(provider.connectionSnapshot().recentAlarms.size(), 1);
    const Data::ControllerAlarmSummary initialAlarm
        = provider.connectionSnapshot().recentAlarms.constFirst();

    const quint64 initialGeneration = provider.connectionSnapshot().sessionGeneration;
    controller.dropChannel(Protocol::Role::Push);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().sessionGeneration > initialGeneration, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QCOMPARE(provider.connectionSnapshot().session->sessionId, TestSessionId);
    QCOMPARE(provider.connectionSnapshot().session->bootId, TestBootId);
    QCOMPARE(provider.connectionSnapshot().recentAlarms.size(), 1);
    QCOMPARE(provider.connectionSnapshot().recentAlarms.constFirst(), initialAlarm);
    QVERIFY(controller.acceptCount(Protocol::Role::Control) >= 2);
    QVERIFY(controller.acceptCount(Protocol::Role::Push) >= 2);
    QVERIFY(controller.acceptCount(Protocol::Role::Bulk) >= 2);
    QVERIFY(controller.requestCount(Protocol::MessageType::GetState) >= 2);
    QTRY_VERIFY_WITH_TIMEOUT(controller.resumeAfterSequences().size() >= 2, 1000);
    QCOMPARE(controller.resumeAfterSequences().constFirst(), quint32(0));
    QCOMPARE(controller.resumeAfterSequences().constLast(), TestAlarmSequence);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.connectionSnapshot().recentAlarms.isEmpty());
}

void EtherCATProductApiTests::testInvalidAlarmCheckpointRefresh_data()
{
    QTest::addColumn<int>("resumeStatus");
    QTest::addColumn<bool>("liveSequenceGap");

    QTest::newRow("event-gap-result") << int(EventGapStatus) << false;
    QTest::newRow("internal-result") << int(InternalStatus) << false;
    QTest::newRow("live-sequence-gap") << 0 << true;
}

void EtherCATProductApiTests::testInvalidAlarmCheckpointRefresh()
{
    QFETCH(int, resumeStatus);
    QFETCH(bool, liveSequenceGap);

    LoopbackController controller;
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.resumeAfterSequences().isEmpty(), 1000);
    QCOMPARE(controller.resumeAfterSequences().constLast(), quint32(0));

    if (liveSequenceGap) {
        QVERIFY(controller.sendLiveAlarm(TestAlarmSequence + 2));
    } else {
        controller.rejectNextResume(qint32(resumeStatus));
        QVERIFY(provider.refreshController());
    }
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Degraded,
                              2000);
    QVERIFY(provider.connectionSnapshot().recentAlarms.isEmpty());

    const qsizetype requestsBeforeRecovery = controller.resumeAfterSequences().size();
    QVERIFY(provider.refreshController());
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.resumeAfterSequences().size() > requestsBeforeRecovery, 1000);
    QCOMPARE(controller.resumeAfterSequences().constLast(), quint32(0));
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QCOMPARE(provider.connectionSnapshot().recentAlarms.size(), 1);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testRuntimeResourceRefreshCapabilityGuards_data()
{
    QTest::addColumn<int>("protocolMinor");
    QTest::addColumn<quint32>("featureBits");
    QTest::addColumn<bool>("bulkFeatureMissing");

    QTest::newRow("minor-11")
        << int(Protocol::ControlledFaultResetMinor) << quint32(0x1fff) << false;
    QTest::newRow("minor-12-without-feature-13")
        << int(Protocol::RuntimeResourceMinor) << quint32(0x1fff) << false;
    QTest::newRow("minor-12-bulk-without-feature-13")
        << int(Protocol::RuntimeResourceMinor) << quint32(0x3fff) << true;
}

void EtherCATProductApiTests::testRuntimeResourceRefreshCapabilityGuards()
{
    QFETCH(int, protocolMinor);
    QFETCH(quint32, featureBits);
    QFETCH(bool, bulkFeatureMissing);

    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    controller.setProtocolMinor(quint16(protocolMinor));
    controller.setFeatureBits(featureBits);
    if (bulkFeatureMissing)
        controller.setRoleFeatureBits(Protocol::Role::Bulk, 0x1fff);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(!provider.supportsRuntimeResources());
    QVERIFY(provider.connectionSnapshot().capability);
    QVERIFY(!provider.connectionSnapshot().capability->runtimeResources);
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());

    const Utils::Result<> result = provider.refreshRuntimeResources();
    QVERIFY(!result);
    QCOMPARE(
        result.error(),
        Tr::tr(
            "Runtime resources require Product API v1.12 and feature bit 13; no controller "
            "request was sent."));
    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
    QVERIFY(!controller.leaseOwned());
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testRuntimeResourceLoopbackLifecycle()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    controller.setRuntimeResourcePageSize(1);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QSignalSpy catalogChanges(
        &provider, &Core::ControllerConnectionProvider::runtimeResourceCatalogChanged);
    QSignalSpy snapshotChanges(
        &provider, &Core::ControllerConnectionProvider::runtimeResourceSnapshotChanged);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.supportsRuntimeResources());
    QVERIFY(provider.connectionSnapshot().capability);
    QVERIFY(provider.connectionSnapshot().capability->runtimeResources);

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QVERIFY(provider.runtimeResourceCatalog());
    QTRY_VERIFY_WITH_TIMEOUT(catalogChanges.count() >= 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(snapshotChanges.count() >= 1, 1000);

    const Data::RuntimeResourceCatalog catalog = *provider.runtimeResourceCatalog();
    QCOMPARE(catalog.scope, provider.connectionSnapshot().scope);
    QCOMPARE(catalog.sessionGeneration, provider.connectionSnapshot().sessionGeneration);
    QCOMPARE(catalog.epoch.controllerBootId, TestBootId);
    QCOMPARE(catalog.epoch.activePackageSlot, Data::ControllerSlot::B);
    QCOMPARE(catalog.epoch.activePackageGeneration, quint64(33));
    QCOMPARE(catalog.epoch.configurationId, quint64(44));
    QCOMPARE(catalog.epoch.topologyGeneration, quint64(7));
    QCOMPARE(catalog.epoch.runtimeGeneration, quint64(8));
    QCOMPARE(catalog.epoch.catalogRevision, quint64(9));
    QCOMPARE(
        catalog.epoch.topologyIdentity, QByteArray::fromHex("3132333435363738"));
    QCOMPARE(catalog.resources.size(), 2);

    const Data::RuntimeResourceDescriptor &input = catalog.resources.at(0);
    QCOMPARE(input.id.value, QByteArray::fromHex("1000000000000001"));
    QCOMPARE(
        input.componentInstanceId.value, QByteArray::fromHex("2000000000000001"));
    QCOMPARE(input.consistencyGroupId.value, QByteArray::fromHex("00000001"));
    QCOMPARE(input.primitiveType, Data::RuntimeResourcePrimitiveType::UnsignedInteger);
    QCOMPARE(input.direction, Data::RuntimeResourceDirection::Input);
    QCOMPARE(input.access, Data::RuntimeResourceAccess::ReadOnly);
    QCOMPARE(input.bitWidth, quint32(16));
    QVERIFY(!input.safeValue);

    const Data::RuntimeResourceDescriptor &output = catalog.resources.at(1);
    QCOMPARE(output.id.value, QByteArray::fromHex("1000000000000002"));
    QCOMPARE(output.primitiveType, Data::RuntimeResourcePrimitiveType::Boolean);
    QCOMPARE(output.direction, Data::RuntimeResourceDirection::Output);
    QCOMPARE(output.access, Data::RuntimeResourceAccess::ReadWrite);
    QCOMPARE(output.bitWidth, quint32(1));
    QVERIFY(output.safeValue);
    QCOMPARE(output.safeValue->primitiveType, Data::RuntimeResourcePrimitiveType::Boolean);
    QCOMPARE(output.safeValue->value, QVariant(false));

    const Data::RuntimeResourceSnapshot snapshot = *provider.runtimeResourceSnapshot();
    QCOMPARE(snapshot.scope, catalog.scope);
    QCOMPARE(snapshot.sessionGeneration, catalog.sessionGeneration);
    QCOMPARE(snapshot.epoch, catalog.epoch);
    QVERIFY(snapshot.complete);
    QCOMPARE(snapshot.captureCycle, quint64(0x1234));
    QCOMPARE(snapshot.controllerTimestampNs, quint64(0x5152535455565758));
    QCOMPARE(snapshot.samples.size(), 2);
    QCOMPARE(snapshot.samples.at(0).resourceId, input.id);
    QCOMPARE(snapshot.samples.at(0).value.primitiveType,
             Data::RuntimeResourcePrimitiveType::UnsignedInteger);
    QCOMPARE(snapshot.samples.at(0).value.value.toULongLong(), qulonglong(0x1234));
    QCOMPARE(snapshot.samples.at(0).quality.state, Data::RuntimeResourceQualityState::Good);
    QCOMPARE(snapshot.samples.at(1).resourceId, output.id);
    QCOMPARE(snapshot.samples.at(1).value.value, QVariant(true));

    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 2);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Heartbeat), 0);
    QVERIFY(!controller.leaseOwned());
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testRuntimeResourceFailureAndInvalidation()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);

    controller.rejectNextRuntimeSnapshotTyped();
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().lastError.has_value(), 1000);
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
    QCOMPARE(
        provider.connectionSnapshot().lastError->operation,
        Data::ControllerOperation::QueryRuntimeResourceSnapshot);
    QCOMPARE(provider.connectionSnapshot().lastError->code, std::optional<qint32>(-17));

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);

    controller.rejectNextRuntimeSnapshotWithBulkStatus();
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().lastError.has_value(), 1000);
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
    QCOMPARE(provider.connectionSnapshot().lastError->code, std::optional<qint32>(-6));
    QCOMPARE(
        provider.connectionSnapshot().lastError->operationResult,
        std::optional<qint32>(0));

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    controller.setRuntimeActivePackage(34, 45);
    QVERIFY(provider.refreshController());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.sessionForTests()->pendingRequestCountForTests(), 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 2000);
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QCOMPARE(
        provider.runtimeResourceCatalog()->epoch.activePackageGeneration, quint64(34));
    QCOMPARE(provider.runtimeResourceCatalog()->epoch.configurationId, quint64(45));

    for (const qint32 reconnectStatus : {-7, -8, -12}) {
        const quint64 generation = provider.connectionSnapshot().sessionGeneration;
        controller.rejectNextRuntimeSnapshotWithBulkStatus(reconnectStatus);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(
            provider.connectionSnapshot().sessionGeneration > generation, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            3000);
        QVERIFY(provider.connectionSnapshot().session);
        QCOMPARE(provider.connectionSnapshot().session->bootId, TestBootId);

        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    }

    const quint64 generation = provider.connectionSnapshot().sessionGeneration;
    controller.rejectNextRuntimeSnapshotTyped(-16);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().sessionGeneration > generation, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        3000);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
}

void EtherCATProductApiTests::testRuntimeResourceProtocolFailures_data()
{
    QTest::addColumn<int>("failure");

    QTest::newRow("wrong-session-id") << 0;
    QTest::newRow("wrong-boot-id") << 1;
    QTest::newRow("malformed-payload") << 2;
    QTest::newRow("wrong-response-type") << 3;
    QTest::newRow("bulk-status-nonzero-store-result") << 4;
}

void EtherCATProductApiTests::testRuntimeResourceProtocolFailures()
{
    QFETCH(int, failure);

    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    switch (failure) {
    case 0:
        controller.corruptNextRuntimeSnapshotSessionId();
        break;
    case 1:
        controller.corruptNextRuntimeSnapshotBootId();
        break;
    case 2:
        controller.corruptNextRuntimeSnapshotPayload();
        break;
    case 3:
        controller.sendWrongNextRuntimeSnapshotResponse();
        break;
    case 4:
        controller.sendNextRuntimeBulkStatusWithOperationResult();
        break;
    default:
        QFAIL("Unknown runtime resource protocol failure.");
    }
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        3000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testRuntimeResourceSignalDisconnectReentrancy()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    bool disconnectAttempted = false;
    bool disconnectAccepted = false;
    connect(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceCatalogChanged,
        &provider,
        [&] {
            if (disconnectAttempted || !provider.runtimeResourceCatalog())
                return;
            disconnectAttempted = true;
            disconnectAccepted = bool(provider.disconnectFromController());
        },
        Qt::DirectConnection);

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(disconnectAttempted, 2000);
    QVERIFY(disconnectAccepted);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testRuntimeResourceReconnectSignalDisconnectReentrancy()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);

    bool disconnectAttempted = false;
    bool disconnectAccepted = false;
    connect(
        &provider,
        &Core::ControllerConnectionProvider::connectionSnapshotChanged,
        &provider,
        [&] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            if (disconnectAttempted || !snapshot.lastError
                || snapshot.lastError->code != std::optional<qint32>(-16)) {
                return;
            }
            disconnectAttempted = true;
            disconnectAccepted = bool(provider.disconnectFromController());
        },
        Qt::DirectConnection);

    controller.rejectNextRuntimeSnapshotTyped(-16);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(disconnectAttempted, 1000);
    QVERIFY(disconnectAccepted);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QTest::qWait(100);
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    QCOMPARE(provider.sessionForTests()->activeSocketCountForTests(), 0);
    QCOMPARE(provider.sessionForTests()->pendingRequestCountForTests(), 0);
    QCOMPARE(controller.acceptCount(Protocol::Role::Control), 1);
    QCOMPARE(controller.acceptCount(Protocol::Role::Push), 1);
    QCOMPARE(controller.acceptCount(Protocol::Role::Bulk), 1);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testRuntimeResourceBounds()
{
    {
        LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
        controller.setRuntimeResourcePageSize(1);
        QVERIFY(controller.start());

        ProductApiSession::Options options = testOptions();
        options.maximumRuntimeResourceCount = 1;
        ProductApiConnectionProvider provider(controller.endpoints(), options);
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().lastError.has_value(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.sessionForTests()->pendingRequestCountForTests(), 0, 1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected);
        QCOMPARE(
            provider.connectionSnapshot().lastError->operation,
            Data::ControllerOperation::QueryRuntimeResourceCatalog);
        QVERIFY(!provider.runtimeResourceCatalog());
        QVERIFY(!provider.runtimeResourceSnapshot());
        QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 0);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
        controller.setRuntimeSnapshotDelayMs(150);
        QVERIFY(controller.start());

        ProductApiSession::Options options = testOptions();
        options.runtimeResourceRefreshTimeoutMs = 100;
        ProductApiConnectionProvider provider(controller.endpoints(), options);
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().lastError.has_value(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.sessionForTests()->pendingRequestCountForTests(), 0, 1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected);
        QCOMPARE(
            provider.connectionSnapshot().lastError->operation,
            Data::ControllerOperation::QueryRuntimeResourceSnapshot);
        QVERIFY(!provider.runtimeResourceCatalog());
        QVERIFY(!provider.runtimeResourceSnapshot());
        QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 1);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
        controller.holdNextRuntimeSnapshot();
        QVERIFY(controller.start());

        ProductApiSession::Options options = testOptions();
        options.requestTimeoutMs = 800;
        options.runtimeResourceRefreshTimeoutMs = 50;
        ProductApiConnectionProvider provider(controller.endpoints(), options);
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            2000);

        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().lastError.has_value(), 400);
        QVERIFY(elapsed.elapsed() < options.requestTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.sessionForTests()->pendingRequestCountForTests(), 0, 1000);
        QCOMPARE(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected);
        QCOMPARE(
            provider.connectionSnapshot().lastError->operation,
            Data::ControllerOperation::QueryRuntimeResourceSnapshot);
        QVERIFY(!provider.runtimeResourceCatalog());
        QVERIFY(!provider.runtimeResourceSnapshot());
        QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 1);
        QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 1);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }
}

void EtherCATProductApiTests::testRuntimeResourceControlInvalidation()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe,
        2000);

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceCatalog().has_value(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);

    const auto execute = [&provider](Data::ControllerControlCommand command) {
        Data::ControllerControlRequest request;
        request.command = command;
        if (!provider.executeControlCommand(request))
            return false;
        return waitForHardwareCondition(
            [&provider, command] {
                const Data::ControllerControlProgress progress
                    = provider.connectionSnapshot().controlProgress;
                return progress.command == command
                       && progress.state == Data::ControllerControlState::Succeeded;
            },
            2000);
    };

    QVERIFY(execute(Data::ControllerControlCommand::AcquireControl));
    QVERIFY(provider.runtimeResourceCatalog());
    QVERIFY(provider.runtimeResourceSnapshot());

    QVERIFY(execute(Data::ControllerControlCommand::ReleaseControl));
    QVERIFY(provider.runtimeResourceCatalog());
    QVERIFY(provider.runtimeResourceSnapshot());

    QVERIFY(execute(Data::ControllerControlCommand::AcquireControl));
    QVERIFY(provider.runtimeResourceCatalog());
    QVERIFY(provider.runtimeResourceSnapshot());

    QVERIFY(execute(Data::ControllerControlCommand::Start));
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState
            && provider.connectionSnapshot().controllerState->serviceState
                   == Data::ControllerServiceState::Running,
        2000);
    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 2);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::Start), 1);
    QVERIFY(controller.leaseOwned());
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(execute(Data::ControllerControlCommand::ReleaseControl));
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    QVERIFY(!controller.leaseOwned());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testRuntimeResourceIncompleteSnapshot()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    controller.setRuntimeResourceCount(65);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);

    QVERIFY(provider.runtimeResourceCatalog());
    QCOMPARE(provider.runtimeResourceCatalog()->resources.size(), 65);
    QCOMPARE(provider.runtimeResourceSnapshot()->samples.size(), 64);
    QVERIFY(!provider.runtimeResourceSnapshot()->complete);
    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryResourceTable), 2);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
    QVERIFY(!controller.leaseOwned());
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

} // namespace EtherCAT::ProductApi::Internal
