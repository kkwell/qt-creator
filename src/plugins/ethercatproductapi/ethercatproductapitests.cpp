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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QByteArray semanticBindingAttestationQueryGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000d0040040d0000000000000040010203040506070841424344"
        "45464748000000000000000421222324252627280000000000000000091ab694"
        "0000004100000000000000000000000700000000000001000000000000000009"
        "000000000000000b000000000000000d60000000000000010000000000000000");
}

QByteArray semanticBindingAttestationGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000d004004890000000100000140010203040506070841424344"
        "45464748000000000000000521222324252627286162636465666768d4dc3ccd"
        "040d014000000000000000410001000000000017000000380000000000000007"
        "00000000000001000000000000000009000000000000000b000000000000000d"
        "60000000000000012122232425262728"
        "0101010101010101010101010101010101010101010101010101010101010101"
        "0202020202020202020202020202020202020202020202020202020202020202"
        "0303030303030303030303030303030303030303030303030303030303030303"
        "0404040404040404040404040404040404040404040404040404040404040404"
        "0505050505050505050505050505050505050505050505050505050505050505"
        "0606060606060606060606060606060606060606060606060606060606060606"
        "0707070707070707070707070707070707070707070707070707070707070707"
        "00000000000000000000000000000000");
}

QByteArray outputGroupPolicyQueryGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000e0040040e0000000000000068010203040506070841424344"
        "454647480000000000000004212223242526272800000000000000007e381a1c"
        "0000004100000000000000000000000700000000000001000000000000000009"
        "000000000000000b000000000000000d60000000000000010000005500000000"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "0000000000000000");
}

QByteArray outputGroupPolicyGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000e0040048a00000001000000b0010203040506070841424344"
        "45464748000000000000000521222324252627286162636465666768e17ff16b"
        "040e00b000000000000000410000000100000000000000070000000000000100"
        "0000000000000009000000000000000b000000000000000d6000000000000001"
        "21222324252627280000005500000001000003e8000000020000000000000009"
        "4444444444444444444444444444444444444444444444444444444444444444"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "00000000000000000000000000000000");
}

QByteArray outputTransactionStateQueryGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000e0040040f0000000000000060010203040506070841424344"
        "454647480000000000000006212223242526272800000000000000005d5ace9a"
        "0000004100000000000000000000000700000000000001000000000000000009"
        "000000000000000b000000000000000d6000000000000001"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "0000000000000000");
}

QByteArray outputTransactionStateGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000e0040048b00000001000000b0010203040506070841424344"
        "45464748000000000000000721222324252627286162636465666768add66a39"
        "040f00b000000000000000000401000000000000000000410000000000000000"
        "0000000000000000000000000000000700000000000001000000000000000009"
        "000000000000000b000000000000000d60000000000000010000000000000000"
        "0000000000000000000000000000000100000000000000000000000000000000"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "00000000000000006162636465666768");
}

QByteArray applyOutputTransactionGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000e0040010e00000000000000c0010203040506070841424344"
        "45464748000000000000000821222324252627280000000000000000205a29e4"
        "008000200000000000112233445566778899aabbccddeeff0000004100020000"
        "000000000000000700000000000001000000000000000009000000000000000b"
        "000000000000000d600000000000000100000000000000010000000500000055"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "0000000000001001020100080000000000000000000000000000000000000001"
        "0000000000001002020100080000000000000000000000000000000000000002");
}

QByteArray outputTransactionResultGoldenWire()
{
    return QByteArray::fromHex(
        "454341500001000e0040018100000001000000b0010203040506070841424344"
        "45464748000000000000000921222324252627286162636465666768f59c32f5"
        "010e00b000000000000000000401000100000002000000410011223344556677"
        "8899aabbccddeeff000000000000000700000000000001000000000000000009"
        "000000000000000b000000000000000d60000000000000010000000000000064"
        "0000000000000069000000000000000200000055000000050000000100020000"
        "3333333333333333333333333333333333333333333333333333333333333333"
        "00000000000000006162636465666768");
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

Protocol::RuntimeResourceBinding outputTransactionBinding()
{
    Protocol::RuntimeResourceBinding binding;
    binding.bootId = 0x2122232425262728;
    binding.activeSlot = quint32('A');
    binding.packageGeneration = 7;
    binding.configurationId = 0x100;
    binding.topologyGeneration = 9;
    binding.runtimeGeneration = 0xb;
    binding.catalogRevision = 0xd;
    binding.topologyIdentity = 0x6000000000000001;
    return binding;
}

Protocol::SemanticBindingAttestationQuery semanticBindingAttestationQuery()
{
    Protocol::SemanticBindingAttestationQuery query;
    query.binding = outputTransactionBinding();
    return query;
}

Protocol::OutputGroupPolicyQuery outputGroupPolicyQuery()
{
    Protocol::OutputGroupPolicyQuery query;
    query.binding = outputTransactionBinding();
    query.consistencyGroupId = 0x55;
    query.semanticMappingSha256 = QByteArray(32, char(0x33));
    return query;
}

Protocol::OutputTransactionStateQuery outputTransactionStateQuery()
{
    Protocol::OutputTransactionStateQuery query;
    query.binding = outputTransactionBinding();
    query.semanticMappingSha256 = QByteArray(32, char(0x33));
    return query;
}

Protocol::OutputTransactionRequest outputTransactionRequest()
{
    Protocol::OutputTransactionRequest request;
    request.binding = outputTransactionBinding();
    request.operationId = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    request.expectedCurrentOutputGeneration = 1;
    request.ttlCycles = 5;
    request.consistencyGroupId = 0x55;
    request.semanticMappingSha256 = QByteArray(32, char(0x33));
    request.values = {
        {0x1001, Protocol::RuntimeResourcePrimitive::Unsigned8, 8, QByteArray(1, char(1))},
        {0x1002, Protocol::RuntimeResourcePrimitive::Unsigned8, 8, QByteArray(1, char(2))},
    };
    return request;
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
    quint32 pageSize,
    bool secondOutputInGroup = false)
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
        const bool output = index == 1 || (secondOutputInGroup && index == 2);
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
        putU32(payload, offset + 44, output ? 2 : index + 1);
    }
    return payload;
}

QByteArray runtimeResourceSnapshotPayload(
    const Protocol::RuntimeResourceBinding &binding,
    const QList<quint64> &resourceIds,
    bool secondOutputInGroup = false)
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
        const bool output = resourceId == 0x1000000000000002
                            || (secondOutputInGroup && resourceId == 0x1000000000000003);
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

Data::RuntimeSemanticMappingProof runtimeSemanticMappingProof(
    Data::RuntimeSemanticMappingTrust trust = Data::RuntimeSemanticMappingTrust::Production)
{
    Data::RuntimeSemanticMappingProof proof;
    proof.formatVersion = 1;
    proof.bindingCount = 56;
    proof.packageSigned = true;
    proof.signatureVerified = true;
    proof.semanticBindingVerified = true;
    proof.trust = trust;
    proof.packageSha256 = QByteArray(32, char(0x11));
    proof.manifestSha256 = QByteArray(32, char(0x22));
    proof.mappingSha256 = QByteArray(32, char(0x33));
    proof.resourceRecordsSha256 = QByteArray(32, char(0x44));
    proof.resourceSectionSha256 = QByteArray(32, char(0x55));
    proof.topologySha256 = QByteArray(32, char(0x66));
    proof.signingKeyIdSha256 = QByteArray(32, char(0x77));
    return proof;
}

QByteArray semanticBindingAttestationPayload(
    const Protocol::RuntimeResourceBinding &binding,
    const Data::RuntimeSemanticMappingProof &proof,
    qint32 status = 0)
{
    QByteArray payload(320, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::QuerySemanticBindingAttestation));
    putU16(payload, 2, 320);
    putI32(payload, 4, status);
    if (status)
        return payload;

    putU32(payload, 8, binding.activeSlot);
    putU16(payload, 12, proof.formatVersion);
    quint32 securityFlags
        = Protocol::semanticBindingSecurityFlagValue(
              Protocol::SemanticBindingSecurityFlag::Signed)
          | Protocol::semanticBindingSecurityFlagValue(
              Protocol::SemanticBindingSecurityFlag::Verified)
          | Protocol::semanticBindingSecurityFlagValue(
              Protocol::SemanticBindingSecurityFlag::Binding);
    securityFlags
        |= proof.trust == Data::RuntimeSemanticMappingTrust::Production
               ? Protocol::semanticBindingSecurityFlagValue(
                     Protocol::SemanticBindingSecurityFlag::Production)
               : Protocol::semanticBindingSecurityFlagValue(
                     Protocol::SemanticBindingSecurityFlag::Engineering);
    putU32(payload, 16, securityFlags);
    putU32(payload, 20, proof.bindingCount);
    putU64(payload, 24, binding.packageGeneration);
    putU64(payload, 32, binding.configurationId);
    putU64(payload, 40, binding.topologyGeneration);
    putU64(payload, 48, binding.runtimeGeneration);
    putU64(payload, 56, binding.catalogRevision);
    putU64(payload, 64, binding.topologyIdentity);
    putU64(payload, 72, binding.bootId);
    payload.replace(80, 32, proof.packageSha256);
    payload.replace(112, 32, proof.manifestSha256);
    payload.replace(144, 32, proof.mappingSha256);
    payload.replace(176, 32, proof.resourceRecordsSha256);
    payload.replace(208, 32, proof.resourceSectionSha256);
    payload.replace(240, 32, proof.topologySha256);
    payload.replace(272, 32, proof.signingKeyIdSha256);
    return payload;
}

QByteArray outputGroupPolicyPayload(
    const Protocol::RuntimeResourceBinding &binding,
    const QByteArray &mappingDigest,
    quint32 groupId,
    quint64 outputGeneration,
    quint32 completeResourceCount = 1,
    qint32 status = 0,
    Protocol::OutputRecoveryPolicy recoveryPolicy = Protocol::OutputRecoveryPolicy::ReturnTask)
{
    QByteArray payload(176, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::QueryOutputGroupPolicy));
    putU16(payload, 2, 176);
    putI32(payload, 4, status);
    if (status)
        return payload;
    putU32(payload, 8, binding.activeSlot);
    putU32(payload, 12, quint32(Protocol::OutputGroupPolicyFlag::ManualWrite));
    putU64(payload, 16, binding.packageGeneration);
    putU64(payload, 24, binding.configurationId);
    putU64(payload, 32, binding.topologyGeneration);
    putU64(payload, 40, binding.runtimeGeneration);
    putU64(payload, 48, binding.catalogRevision);
    putU64(payload, 56, binding.topologyIdentity);
    putU64(payload, 64, binding.bootId);
    putU32(payload, 72, groupId);
    putU32(payload, 76, quint32(recoveryPolicy));
    putU32(payload, 80, 1000);
    putU32(payload, 84, completeResourceCount);
    putU64(payload, 88, outputGeneration);
    payload.replace(96, 32, QByteArray(32, char(0x44)));
    payload.replace(128, 32, mappingDigest);
    return payload;
}

QByteArray outputTransactionRecordPayload(
    Protocol::MessageType originalType,
    const Protocol::RuntimeResourceBinding &binding,
    const QByteArray &mappingDigest,
    quint64 outputGeneration,
    const QByteArray &operationId = {},
    quint32 resultFlags = 0,
    quint64 detail = 0,
    qint32 status = 0,
    qint32 operationResult = 0,
    Protocol::OutputRecoveryPolicy recoveryPolicy = Protocol::OutputRecoveryPolicy::ReturnTask)
{
    QByteArray payload(176, '\0');
    putU16(payload, 0, quint16(originalType));
    putU16(payload, 2, 176);
    putI32(payload, 4, status);
    putI32(payload, 8, operationResult);
    payload[12] = char(4);
    payload[13] = char(1);
    if (status)
        return payload;
    const bool active = operationId.size() == 16;
    putU16(
        payload,
        14,
        quint16(
            active ? Protocol::OutputTransactionState::OverrideActive
                   : Protocol::OutputTransactionState::Idle));
    putU32(payload, 16, resultFlags);
    putU32(payload, 20, binding.activeSlot);
    if (active)
        payload.replace(24, 16, operationId);
    putU64(payload, 40, binding.packageGeneration);
    putU64(payload, 48, binding.configurationId);
    putU64(payload, 56, binding.topologyGeneration);
    putU64(payload, 64, binding.runtimeGeneration);
    putU64(payload, 72, binding.catalogRevision);
    putU64(payload, 80, binding.topologyIdentity);
    if (active) {
        putU64(payload, 88, 100);
        putU64(payload, 96, 105);
    }
    putU64(payload, 104, outputGeneration);
    if (active) {
        putU32(payload, 112, 2);
        putU32(payload, 116, 5);
        putU32(payload, 120, quint32(recoveryPolicy));
        putU16(payload, 124, 1);
    }
    payload.replace(128, 32, mappingDigest);
    putU64(payload, 160, detail);
    putU64(payload, 168, 0x6162636465666768);
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
        CommandStatusResponse,
        FirmwareStatusResponse,
        BulkStatusNonzeroOperationResult,
    };
    enum class SemanticAttestationFailure {
        None,
        Typed,
        BulkStatus,
        WrongSession,
        WrongBoot,
        WrongEpoch,
        MalformedPayload,
        WrongResponse,
    };
    enum class OutputTransactionFailure {
        None,
        PolicyTyped,
        PolicyBulkStatus,
        ApplyStage2,
        ApplyStage3,
        HoldApplyResult,
    };
    enum class OutputStateReport {
        Current,
        NoAcceptedOperation,
        DifferentOperation,
        ReturnedTask,
        SafeHold,
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
        SemanticAttestation,
        OutputTransactions,
    };

    explicit LoopbackController(Behavior behavior = Behavior::Normal)
        : m_behavior(behavior)
    {
        if (m_behavior == Behavior::FaultReset) {
            m_serviceState = 6;
            m_controllerPackageActive = true;
            m_latchedFaults = quint64(1) << 15;
            m_lastAlarmSequence = TestAlarmSequence;
        } else if (
            m_behavior == Behavior::RuntimeResources || m_behavior == Behavior::SemanticAttestation
            || m_behavior == Behavior::OutputTransactions) {
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
    void includeSecondOutputInPolicyGroup()
    {
        m_runtimeResourceCount = 3;
        m_secondOutputInPolicyGroup = true;
    }
    void setRuntimeResourcePageSize(quint32 count) { m_runtimeResourcePageSize = count; }
    void setRuntimeSnapshotDelayMs(int delayMs) { m_runtimeSnapshotDelayMs = delayMs; }
    void holdNextRuntimeSnapshot() { m_holdNextRuntimeSnapshot = true; }
    void setRuntimeActivePackage(quint64 generation, quint64 configurationId)
    {
        m_runtimePackageGeneration = generation;
        m_runtimeConfigurationId = configurationId;
    }
    void setRuntimeEpoch(
        quint64 topologyGeneration,
        quint64 runtimeGeneration,
        quint64 catalogRevision,
        quint64 topologyIdentity)
    {
        m_runtimeTopologyGeneration = topologyGeneration;
        m_runtimeGeneration = runtimeGeneration;
        m_runtimeCatalogRevision = catalogRevision;
        m_runtimeTopologyIdentity = topologyIdentity;
    }
    void setSemanticMappingProof(const Data::RuntimeSemanticMappingProof &proof)
    {
        m_semanticMappingProof = proof;
    }
    void rejectNextSemanticAttestationTyped(qint32 status = -17)
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::Typed;
        m_nextSemanticAttestationStatus = status;
    }
    void rejectNextSemanticAttestationWithBulkStatus(qint32 status = -7)
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::BulkStatus;
        m_nextSemanticAttestationStatus = status;
    }
    void corruptNextSemanticAttestationSessionId()
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::WrongSession;
    }
    void corruptNextSemanticAttestationBootId()
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::WrongBoot;
    }
    void corruptNextSemanticAttestationEpoch()
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::WrongEpoch;
    }
    void corruptNextSemanticAttestationPayload()
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::MalformedPayload;
    }
    void sendWrongNextSemanticAttestationResponse()
    {
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::WrongResponse;
    }
    void setSemanticAttestationDelayMs(int delayMs)
    {
        m_semanticAttestationDelayMs = delayMs;
    }
    void rejectNextOutputPolicyTyped(qint32 status = -41)
    {
        m_nextOutputFailure = OutputTransactionFailure::PolicyTyped;
        m_nextOutputStatus = status;
    }
    void rejectNextOutputPolicyWithBulkStatus(qint32 status = -7)
    {
        m_nextOutputFailure = OutputTransactionFailure::PolicyBulkStatus;
        m_nextOutputStatus = status;
    }
    void rejectNextOutputStateTyped(qint32 status = -17) { m_nextOutputStateStatus = status; }
    void rejectNextOutputApply(quint16 stage, qint32 status, qint32 operationResult)
    {
        m_nextOutputFailure = stage == 2 ? OutputTransactionFailure::ApplyStage2
                                         : OutputTransactionFailure::ApplyStage3;
        m_nextOutputStatus = status;
        m_nextOutputOperationResult = operationResult;
    }
    void holdNextOutputApplyResult()
    {
        m_nextOutputFailure = OutputTransactionFailure::HoldApplyResult;
    }
    void holdNextOutputApplyResponseSequence()
    {
        m_holdNextOutputApplyResponseSequence = true;
    }
    bool hasHeldOutputApplyResponseSequence() const
    {
        return m_heldOutputApplyPeer && m_heldOutputApplyRequest.header.requestId;
    }
    void releaseHeldOutputApplyResponseSequence()
    {
        if (!hasHeldOutputApplyResponseSequence()) {
            m_violations.append(QStringLiteral("No held output response sequence was available."));
            return;
        }
        Peer *peer = m_heldOutputApplyPeer;
        const Protocol::Frame request = m_heldOutputApplyRequest;
        m_heldOutputApplyPeer = nullptr;
        m_heldOutputApplyRequest = {};
        handleOutputTransactionRequest(*peer, request);
    }
    void holdNextOutputStateResponse() { m_holdNextOutputStateResponse = true; }
    void reportNextOutputStateWithoutAcceptedOperation()
    {
        m_nextOutputStateReport = OutputStateReport::NoAcceptedOperation;
    }
    void reportNextOutputStateForDifferentOperation()
    {
        m_nextOutputStateReport = OutputStateReport::DifferentOperation;
    }
    void reportNextOutputStateAsReturnedTask()
    {
        m_nextOutputStateReport = OutputStateReport::ReturnedTask;
    }
    void reportNextOutputStateAsSafeHold()
    {
        m_nextOutputStateReport = OutputStateReport::SafeHold;
    }
    void setNextOutputRecoveryGenerationAdvance(quint64 advance)
    {
        m_nextOutputRecoveryGenerationAdvance = advance;
    }
    void setOutputRecoveryPolicy(Protocol::OutputRecoveryPolicy policy)
    {
        m_outputRecoveryPolicy = policy;
    }
    void setOutputGeneration(quint64 generation) { m_outputGeneration = generation; }
    void setOutputStateReportsReplayed(bool enabled) { m_stateReportsReplayed = enabled; }
    bool hasHeldOutputResult() const { return m_heldOutputPeer && m_heldOutputRequestId; }
    void releaseHeldOutputResult()
    {
        if (!hasHeldOutputResult()) {
            m_violations.append(QStringLiteral("No held output result was available."));
            return;
        }
        sendResponse(
            *m_heldOutputPeer,
            Protocol::MessageType::OutputTransactionResult,
            m_heldOutputRequestId,
            m_heldOutputPayload);
        m_heldOutputPeer = nullptr;
        m_heldOutputRequestId = 0;
        m_heldOutputPayload.clear();
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
    void sendNextRuntimeCommandStatusResponse()
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::CommandStatusResponse;
    }
    void sendNextRuntimeFirmwareStatusResponse()
    {
        m_nextRuntimeResourceFailure = RuntimeResourceFailure::FirmwareStatusResponse;
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
        case Protocol::MessageType::QuerySemanticBindingAttestation:
            handleSemanticBindingAttestationRequest(peer, frame);
            return;
        case Protocol::MessageType::QueryOutputGroupPolicy:
        case Protocol::MessageType::GetOutputTransactionState:
        case Protocol::MessageType::ApplyOutputTransaction:
            handleOutputTransactionRequest(peer, frame);
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
        Protocol::RuntimeResourceBinding binding = loopbackRuntimeResourceBinding(
            m_bootId, m_runtimePackageGeneration, m_runtimeConfigurationId);
        binding.topologyGeneration = m_runtimeTopologyGeneration;
        binding.runtimeGeneration = m_runtimeGeneration;
        binding.catalogRevision = m_runtimeCatalogRevision;
        binding.topologyIdentity = m_runtimeTopologyIdentity;
        return binding;
    }

    void handleRuntimeResourceRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::RuntimeResources && m_behavior != Behavior::SemanticAttestation
            && m_behavior != Behavior::OutputTransactions) {
            m_violations.append(
                QStringLiteral("A runtime resource request was emitted outside its test."));
            return;
        }
        if (peer.role != Protocol::Role::Bulk)
            m_violations.append(QStringLiteral("A runtime resource request used the wrong channel."));

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

            const QByteArray payload = runtimeResourceTablePagePayload(
                binding,
                cursor,
                m_runtimeResourceCount,
                m_runtimeResourcePageSize,
                m_secondOutputInPolicyGroup);
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
        if (failure == RuntimeResourceFailure::CommandStatusResponse) {
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                {});
            return;
        }
        if (failure == RuntimeResourceFailure::FirmwareStatusResponse) {
            sendResponse(
                peer,
                Protocol::MessageType::FirmwareStatus,
                request.header.requestId,
                {});
            return;
        }
        QByteArray snapshotPayload
            = runtimeResourceSnapshotPayload(binding, resourceIds, m_secondOutputInPolicyGroup);
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

    void handleSemanticBindingAttestationRequest(
        Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::SemanticAttestation
            && m_behavior != Behavior::OutputTransactions) {
            m_violations.append(
                QStringLiteral("A semantic attestation request was emitted outside its test."));
            return;
        }
        if (peer.role != Protocol::Role::Bulk) {
            m_violations.append(
                QStringLiteral("A semantic attestation request used the wrong channel."));
        }

        const Protocol::RuntimeResourceBinding binding = currentRuntimeResourceBinding();
        if (request.payload.size() != 64
            || readU32(request.payload, 0) != binding.activeSlot
            || readU32(request.payload, 4)
            || readU64(request.payload, 8) != binding.packageGeneration
            || readU64(request.payload, 16) != binding.configurationId
            || readU64(request.payload, 24) != binding.topologyGeneration
            || readU64(request.payload, 32) != binding.runtimeGeneration
            || readU64(request.payload, 40) != binding.catalogRevision
            || readU64(request.payload, 48) != binding.topologyIdentity
            || readU64(request.payload, 56)) {
            m_violations.append(
                QStringLiteral("A semantic attestation request was malformed."));
            return;
        }
        const SemanticAttestationFailure failure = m_nextSemanticAttestationFailure;
        m_nextSemanticAttestationFailure = SemanticAttestationFailure::None;
        if (failure == SemanticAttestationFailure::Typed) {
            const qint32 status = m_nextSemanticAttestationStatus;
            m_nextSemanticAttestationStatus = -17;
            sendResponse(
                peer,
                Protocol::MessageType::SemanticBindingAttestation,
                request.header.requestId,
                semanticBindingAttestationPayload(binding, m_semanticMappingProof, status),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        if (failure == SemanticAttestationFailure::BulkStatus) {
            QByteArray payload = bulkStatusPayload(
                quint16(Protocol::MessageType::QuerySemanticBindingAttestation));
            putI32(payload, 0, m_nextSemanticAttestationStatus);
            putI32(payload, 4, 0);
            m_nextSemanticAttestationStatus = -17;
            sendResponse(
                peer,
                Protocol::MessageType::BulkStatus,
                request.header.requestId,
                payload,
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }
        if (failure == SemanticAttestationFailure::WrongResponse) {
            sendResponse(
                peer,
                Protocol::MessageType::ResourceTablePage,
                request.header.requestId,
                {});
            return;
        }

        Protocol::RuntimeResourceBinding responseBinding = binding;
        if (failure == SemanticAttestationFailure::WrongEpoch)
            ++responseBinding.runtimeGeneration;
        QByteArray payload = semanticBindingAttestationPayload(
            responseBinding, m_semanticMappingProof);
        if (failure == SemanticAttestationFailure::MalformedPayload)
            payload[304] = char(1);
        if (failure == SemanticAttestationFailure::WrongSession
            || failure == SemanticAttestationFailure::WrongBoot) {
            Protocol::Frame frame = response(
                peer,
                Protocol::MessageType::SemanticBindingAttestation,
                request.header.requestId,
                payload);
            if (failure == SemanticAttestationFailure::WrongSession)
                frame.header.sessionId = TestSessionId + 1;
            else
                frame.header.bootId = m_bootId + 1;
            const QByteArray wire = wireFor(frame);
            if (!wire.isEmpty())
                peer.socket->write(wire);
            return;
        }
        if (m_semanticAttestationDelayMs > 0) {
            Peer *peerPointer = &peer;
            const quint64 requestId = request.header.requestId;
            const int delayMs = m_semanticAttestationDelayMs;
            QTimer::singleShot(
                delayMs,
                this,
                [this, peerPointer, requestId, payload] {
                    if (peerPointer->socket->state() == QAbstractSocket::ConnectedState) {
                        sendResponse(
                            *peerPointer,
                            Protocol::MessageType::SemanticBindingAttestation,
                            requestId,
                            payload);
                    }
                });
            return;
        }
        sendResponse(
            peer,
            Protocol::MessageType::SemanticBindingAttestation,
            request.header.requestId,
            payload);
    }

    void handleOutputTransactionRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::OutputTransactions) {
            m_violations.append(
                QStringLiteral("An output transaction request was emitted outside its test."));
            return;
        }
        const Protocol::RuntimeResourceBinding binding = currentRuntimeResourceBinding();
        const QByteArray mappingDigest = m_semanticMappingProof.mappingSha256;

        if (request.header.messageType == Protocol::MessageType::QueryOutputGroupPolicy) {
            if (peer.role != Protocol::Role::Bulk || request.payload.size() != 104
                || readU32(request.payload, 0) != binding.activeSlot || readU32(request.payload, 4)
                || readU64(request.payload, 8) != binding.packageGeneration
                || readU64(request.payload, 16) != binding.configurationId
                || readU64(request.payload, 24) != binding.topologyGeneration
                || readU64(request.payload, 32) != binding.runtimeGeneration
                || readU64(request.payload, 40) != binding.catalogRevision
                || readU64(request.payload, 48) != binding.topologyIdentity
                || readU32(request.payload, 56) != 2 || readU32(request.payload, 60)
                || request.payload.mid(64, 32) != mappingDigest || readU64(request.payload, 96)) {
                m_violations.append(QStringLiteral("The output policy request was malformed."));
                return;
            }
            const OutputTransactionFailure failure = m_nextOutputFailure;
            m_nextOutputFailure = OutputTransactionFailure::None;
            if (failure == OutputTransactionFailure::PolicyTyped) {
                const qint32 status = m_nextOutputStatus;
                m_nextOutputStatus = -41;
                sendResponse(
                    peer,
                    Protocol::MessageType::OutputGroupPolicy,
                    request.header.requestId,
                    outputGroupPolicyPayload(
                        binding,
                        mappingDigest,
                        2,
                        m_outputGeneration,
                        m_secondOutputInPolicyGroup ? 2 : 1,
                        status,
                        m_outputRecoveryPolicy),
                    Protocol::Flag::Response | Protocol::Flag::Error);
                return;
            }
            if (failure == OutputTransactionFailure::PolicyBulkStatus) {
                QByteArray payload = bulkStatusPayload(
                    quint16(Protocol::MessageType::QueryOutputGroupPolicy));
                putI32(payload, 0, m_nextOutputStatus);
                putI32(payload, 4, 0);
                m_nextOutputStatus = -41;
                sendResponse(
                    peer,
                    Protocol::MessageType::BulkStatus,
                    request.header.requestId,
                    payload,
                    Protocol::Flag::Response | Protocol::Flag::Error);
                return;
            }
            sendResponse(
                peer,
                Protocol::MessageType::OutputGroupPolicy,
                request.header.requestId,
                outputGroupPolicyPayload(
                    binding,
                    mappingDigest,
                    2,
                    m_outputGeneration,
                    m_secondOutputInPolicyGroup ? 2 : 1,
                    0,
                    m_outputRecoveryPolicy));
            return;
        }

        if (request.header.messageType == Protocol::MessageType::GetOutputTransactionState) {
            if (peer.role != Protocol::Role::Bulk || request.payload.size() != 96
                || readU32(request.payload, 0) != binding.activeSlot || readU32(request.payload, 4)
                || readU64(request.payload, 8) != binding.packageGeneration
                || readU64(request.payload, 16) != binding.configurationId
                || readU64(request.payload, 24) != binding.topologyGeneration
                || readU64(request.payload, 32) != binding.runtimeGeneration
                || readU64(request.payload, 40) != binding.catalogRevision
                || readU64(request.payload, 48) != binding.topologyIdentity
                || request.payload.mid(56, 32) != mappingDigest || readU64(request.payload, 88)) {
                m_violations.append(QStringLiteral("The output state request was malformed."));
                return;
            }
            if (m_holdNextOutputStateResponse) {
                m_holdNextOutputStateResponse = false;
                return;
            }
            if (m_nextOutputStateStatus) {
                const qint32 status = m_nextOutputStateStatus;
                m_nextOutputStateStatus = 0;
                sendResponse(
                    peer,
                    Protocol::MessageType::OutputTransactionState,
                    request.header.requestId,
                    outputTransactionRecordPayload(
                        Protocol::MessageType::GetOutputTransactionState,
                        binding,
                        mappingDigest,
                        0,
                        {},
                        0,
                        0,
                        status,
                        status == -14  ? -10
                        : status == -6 ? -1
                                       : -2),
                    Protocol::Flag::Response | Protocol::Flag::Error);
                return;
            }
            const OutputStateReport report = m_nextOutputStateReport;
            m_nextOutputStateReport = OutputStateReport::Current;
            const quint64 recoveryGenerationAdvance = m_nextOutputRecoveryGenerationAdvance;
            m_nextOutputRecoveryGenerationAdvance = 1;
            QByteArray operationId = m_outputOperationId;
            quint64 outputGeneration = m_outputGeneration;
            quint32 flags = operationId.isEmpty()
                                ? 0
                                : Protocol::outputTransactionResultFlagValue(
                                      Protocol::OutputTransactionResultFlag::OverrideActive);
            quint64 detail = 0;
            if (report == OutputStateReport::NoAcceptedOperation) {
                operationId.clear();
                flags = 0;
            } else if (report == OutputStateReport::DifferentOperation) {
                operationId = QByteArray::fromHex("ffeeddccbbaa99887766554433221100");
            } else if (report == OutputStateReport::ReturnedTask || report == OutputStateReport::SafeHold) {
                outputGeneration += recoveryGenerationAdvance;
                m_outputGeneration = outputGeneration;
                flags = Protocol::outputTransactionResultFlagValue(
                    report == OutputStateReport::ReturnedTask
                        ? Protocol::OutputTransactionResultFlag::ReturnedTask
                        : Protocol::OutputTransactionResultFlag::SafeHold);
                detail = 105;
            }
            if (!operationId.isEmpty() && m_stateReportsReplayed) {
                flags |= Protocol::outputTransactionResultFlagValue(
                    Protocol::OutputTransactionResultFlag::Replayed);
            }
            QByteArray payload = outputTransactionRecordPayload(
                Protocol::MessageType::GetOutputTransactionState,
                binding,
                mappingDigest,
                outputGeneration,
                operationId,
                flags,
                detail,
                0,
                0,
                m_outputRecoveryPolicy);
            if (report == OutputStateReport::ReturnedTask) {
                putU16(payload, 14, quint16(Protocol::OutputTransactionState::Idle));
            } else if (report == OutputStateReport::SafeHold) {
                putU16(payload, 14, quint16(Protocol::OutputTransactionState::SafeHold));
            }
            sendResponse(
                peer,
                Protocol::MessageType::OutputTransactionState,
                request.header.requestId,
                payload);
            return;
        }

        if (peer.role != Protocol::Role::Control || request.payload.size() != 160
            || readU16(request.payload, 0) != 128 || readU16(request.payload, 2) != 32
            || readU32(request.payload, 4) || readU32(request.payload, 24) != binding.activeSlot
            || readU16(request.payload, 28) != 1 || readU16(request.payload, 30)
            || readU64(request.payload, 32) != binding.packageGeneration
            || readU64(request.payload, 40) != binding.configurationId
            || readU64(request.payload, 48) != binding.topologyGeneration
            || readU64(request.payload, 56) != binding.runtimeGeneration
            || readU64(request.payload, 64) != binding.catalogRevision
            || readU64(request.payload, 72) != binding.topologyIdentity
            || readU32(request.payload, 88) != 5 || readU32(request.payload, 92) != 2
            || request.payload.mid(96, 32) != mappingDigest
            || readU64(request.payload, 128) != 0x1000000000000002
            || quint8(request.payload.at(136)) != quint8(Protocol::RuntimeResourcePrimitive::Boolean)
            || quint8(request.payload.at(137)) != 1 || readU16(request.payload, 138) != 1
            || readU32(request.payload, 140) || quint8(request.payload.at(159)) > 1) {
            m_violations.append(QStringLiteral("The output apply request was malformed."));
            return;
        }
        if (!m_leaseOwned || (m_serviceState != 3 && m_serviceState != 4 && m_serviceState != 9)) {
            m_violations.append(QStringLiteral("The output apply preconditions were invalid."));
            return;
        }
        if (m_holdNextOutputApplyResponseSequence) {
            m_holdNextOutputApplyResponseSequence = false;
            m_heldOutputApplyPeer = &peer;
            m_heldOutputApplyRequest = request;
            return;
        }
        const QByteArray operationId = request.payload.mid(8, 16);
        const bool exactReplay = operationId == m_outputOperationId
                                 && request.payload == m_lastOutputApplyPayload;
        if (!m_outputOperationId.isEmpty() && operationId == m_outputOperationId && !exactReplay) {
            for (quint16 stage = 1; stage <= 2; ++stage) {
                sendResponse(
                    peer,
                    Protocol::MessageType::CommandStatus,
                    request.header.requestId,
                    successfulCommandStatusPayload(
                        request.header.messageType, stage, m_serviceState, false));
            }
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                rejectedCommandStatusPayload(
                    request.header.messageType, 3, m_serviceState, -36, 0, -7),
                Protocol::Flag::Response | Protocol::Flag::Error);
            return;
        }

        if (!exactReplay && readU64(request.payload, 80) != m_outputGeneration
            && m_nextOutputFailure == OutputTransactionFailure::None) {
            m_nextOutputFailure = OutputTransactionFailure::ApplyStage3;
            m_nextOutputStatus = -37;
            m_nextOutputOperationResult = -8;
        }
        for (quint16 stage = 1; stage <= 3; ++stage) {
            if ((m_nextOutputFailure == OutputTransactionFailure::ApplyStage2 && stage == 2)
                || (m_nextOutputFailure == OutputTransactionFailure::ApplyStage3 && stage == 3)) {
                sendResponse(
                    peer,
                    Protocol::MessageType::CommandStatus,
                    request.header.requestId,
                    rejectedCommandStatusPayload(
                        request.header.messageType,
                        stage,
                        m_serviceState,
                        m_nextOutputStatus,
                        0,
                        m_nextOutputOperationResult),
                    Protocol::Flag::Response | Protocol::Flag::Error);
                m_nextOutputFailure = OutputTransactionFailure::None;
                m_nextOutputStatus = -41;
                m_nextOutputOperationResult = -10;
                return;
            }
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                successfulCommandStatusPayload(
                    request.header.messageType, stage, m_serviceState, false));
        }

        if (!exactReplay) {
            ++m_outputGeneration;
            m_outputOperationId = operationId;
            m_lastOutputApplyPayload = request.payload;
        }
        const quint32 flags = Protocol::outputTransactionResultFlagValue(
                                  Protocol::OutputTransactionResultFlag::OverrideActive)
                              | (exactReplay ? Protocol::outputTransactionResultFlagValue(
                                                   Protocol::OutputTransactionResultFlag::Replayed)
                                             : 0);
        const QByteArray payload = outputTransactionRecordPayload(
            Protocol::MessageType::ApplyOutputTransaction,
            binding,
            mappingDigest,
            m_outputGeneration,
            operationId,
            flags,
            0,
            0,
            0,
            m_outputRecoveryPolicy);
        if (m_nextOutputFailure == OutputTransactionFailure::HoldApplyResult) {
            m_nextOutputFailure = OutputTransactionFailure::None;
            m_heldOutputPeer = &peer;
            m_heldOutputRequestId = request.header.requestId;
            m_heldOutputPayload = payload;
            return;
        }
        sendResponse(
            peer, Protocol::MessageType::OutputTransactionResult, request.header.requestId, payload);
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
            QByteArray statePayload = m_behavior == Behavior::ControlLifecycle
                                              || m_behavior == Behavior::FaultReset
                                              || m_behavior == Behavior::PackageDeployment
                                              || m_behavior == Behavior::RuntimeResources
                                              || m_behavior == Behavior::SemanticAttestation
                                              || m_behavior == Behavior::OutputTransactions
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
        if (m_behavior == Behavior::RuntimeResources || m_behavior == Behavior::SemanticAttestation
            || m_behavior == Behavior::OutputTransactions) {
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
            && m_behavior != Behavior::PackageDeployment && m_behavior != Behavior::RuntimeResources
            && m_behavior != Behavior::SemanticAttestation
            && m_behavior != Behavior::OutputTransactions) {
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
            = m_protocolMinor >= Protocol::OutputTransactionMinor
                  ? 0xffff
              : m_protocolMinor >= Protocol::SemanticBindingAttestationMinor
                  ? 0x7fff
              : m_protocolMinor >= Protocol::RuntimeResourceMinor
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
    bool m_secondOutputInPolicyGroup = false;
    int m_runtimeSnapshotDelayMs = 0;
    bool m_holdNextRuntimeSnapshot = false;
    quint64 m_runtimePackageGeneration = 33;
    quint64 m_runtimeConfigurationId = 44;
    quint64 m_runtimeTopologyGeneration = 7;
    quint64 m_runtimeGeneration = 8;
    quint64 m_runtimeCatalogRevision = 9;
    quint64 m_runtimeTopologyIdentity = 0x3132333435363738;
    Data::RuntimeSemanticMappingProof m_semanticMappingProof
        = runtimeSemanticMappingProof();
    SemanticAttestationFailure m_nextSemanticAttestationFailure
        = SemanticAttestationFailure::None;
    qint32 m_nextSemanticAttestationStatus = -17;
    int m_semanticAttestationDelayMs = 0;
    OutputTransactionFailure m_nextOutputFailure = OutputTransactionFailure::None;
    qint32 m_nextOutputStatus = -41;
    qint32 m_nextOutputOperationResult = -10;
    qint32 m_nextOutputStateStatus = 0;
    quint64 m_outputGeneration = 1;
    QByteArray m_outputOperationId;
    QByteArray m_lastOutputApplyPayload;
    bool m_stateReportsReplayed = false;
    bool m_holdNextOutputStateResponse = false;
    bool m_holdNextOutputApplyResponseSequence = false;
    OutputStateReport m_nextOutputStateReport = OutputStateReport::Current;
    quint64 m_nextOutputRecoveryGenerationAdvance = 1;
    Protocol::OutputRecoveryPolicy m_outputRecoveryPolicy
        = Protocol::OutputRecoveryPolicy::ReturnTask;
    Peer *m_heldOutputPeer = nullptr;
    quint64 m_heldOutputRequestId = 0;
    QByteArray m_heldOutputPayload;
    Peer *m_heldOutputApplyPeer = nullptr;
    Protocol::Frame m_heldOutputApplyRequest;
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

Data::RuntimeResourceCatalogEpoch runtimeResourceEpochForTests(
    const Protocol::RuntimeResourceBinding &binding)
{
    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = binding.bootId;
    epoch.activePackageSlot
        = binding.activeSlot == quint32('A') ? Data::ControllerSlot::A
                                            : Data::ControllerSlot::B;
    epoch.activePackageGeneration = binding.packageGeneration;
    epoch.configurationId = binding.configurationId;
    epoch.topologyGeneration = binding.topologyGeneration;
    epoch.runtimeGeneration = binding.runtimeGeneration;
    epoch.catalogRevision = binding.catalogRevision;
    epoch.topologyIdentity.resize(8);
    qToBigEndian(
        binding.topologyIdentity,
        reinterpret_cast<uchar *>(epoch.topologyIdentity.data()));
    return epoch;
}

Data::RuntimeSemanticMappingAttestationRequest semanticAttestationRequest(
    const ProductApiConnectionProvider &provider,
    const Data::RuntimeSemanticMappingProof &proof = runtimeSemanticMappingProof(),
    const Protocol::RuntimeResourceBinding &binding = loopbackRuntimeResourceBinding(
        TestBootId, 33, 44),
    const QString &correlationId = QStringLiteral("semantic-attestation"))
{
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    Data::RuntimeSemanticMappingAttestationRequest request;
    request.correlationId = correlationId;
    request.scope = snapshot.scope;
    request.sessionGeneration = snapshot.sessionGeneration;
    request.expectedEpoch = runtimeResourceEpochForTests(binding);
    request.expectedProof = proof;
    return request;
}

Data::RuntimeOutputGroupPolicyRequest outputPolicyRequest(
    const ProductApiConnectionProvider &provider,
    const QString &correlationId = QStringLiteral("output-policy"))
{
    Data::RuntimeOutputGroupPolicyRequest request;
    const auto catalog = provider.runtimeResourceCatalog();
    const auto attestation = provider.runtimeSemanticMappingAttestation();
    if (!catalog || !attestation || catalog->resources.size() < 2)
        return request;
    request.correlationId = correlationId;
    request.scope = catalog->scope;
    request.sessionGeneration = catalog->sessionGeneration;
    request.expectedEpoch = catalog->epoch;
    request.consistencyGroupId = catalog->resources.at(1).consistencyGroupId;
    request.expectedMappingDigest = attestation->proof.mappingSha256;
    return request;
}

Data::RuntimeOutputTransactionStateRequest outputStateRequest(
    const ProductApiConnectionProvider &provider,
    const QString &correlationId = QStringLiteral("output-state"))
{
    Data::RuntimeOutputTransactionStateRequest request;
    const auto catalog = provider.runtimeResourceCatalog();
    const auto attestation = provider.runtimeSemanticMappingAttestation();
    if (!catalog || !attestation)
        return request;
    request.correlationId = correlationId;
    request.scope = catalog->scope;
    request.sessionGeneration = catalog->sessionGeneration;
    request.expectedEpoch = catalog->epoch;
    request.expectedMappingDigest = attestation->proof.mappingSha256;
    return request;
}

Data::RuntimeOutputTransactionRequest outputApplyRequest(
    const ProductApiConnectionProvider &provider,
    const QByteArray &operationId = QByteArray::fromHex("00112233445566778899aabbccddeeff"),
    Data::RuntimeOutputRecoveryPolicy recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::ReturnTask)
{
    Data::RuntimeOutputTransactionRequest request;
    const auto catalog = provider.runtimeResourceCatalog();
    const auto attestation = provider.runtimeSemanticMappingAttestation();
    if (!catalog || !attestation || catalog->resources.size() < 2)
        return request;
    const Data::RuntimeResourceDescriptor &descriptor = catalog->resources.at(1);
    request.operationId = {operationId};
    request.scope = catalog->scope;
    request.sessionGeneration = catalog->sessionGeneration;
    request.expectedEpoch = catalog->epoch;
    request.expectedMappingDigest = attestation->proof.mappingSha256;
    request.expectedCompleteGroupRecordDigest = QByteArray(32, char(0x44));
    request.expectedRecoveryPolicy = recoveryPolicy;
    request.expectedMaximumTtlCycles = 1000;
    request.expectedOutputGeneration = 1;
    request.ttlCycles = 5;
    request.consistencyGroupId = descriptor.consistencyGroupId;
    for (const Data::RuntimeResourceDescriptor &groupDescriptor : catalog->resources) {
        if (groupDescriptor.consistencyGroupId != descriptor.consistencyGroupId)
            continue;
        Data::RuntimeOutputValueWrite write;
        write.resourceId = groupDescriptor.id;
        write.bitWidth = quint16(groupDescriptor.bitWidth);
        write.value.primitiveType = groupDescriptor.primitiveType;
        write.value.typeIdentity = groupDescriptor.valueTypeIdentity;
        write.value.value = true;
        request.completeGroupWrites.append(write);
    }
    request.expectedCompleteResourceCount = quint32(request.completeGroupWrites.size());
    return request;
}

Data::RuntimeResourceSnapshotRequest targetedSnapshotRequest(
    const ProductApiConnectionProvider &provider,
    const QList<qsizetype> &catalogIndexes,
    const QString &correlationId = QStringLiteral("targeted-runtime-read"))
{
    Data::RuntimeResourceSnapshotRequest request;
    const auto catalog = provider.runtimeResourceCatalog();
    if (!catalog)
        return request;
    request.correlationId = correlationId;
    request.scope = catalog->scope;
    request.sessionGeneration = catalog->sessionGeneration;
    request.expectedEpoch = catalog->epoch;
    request.resourceIds.reserve(catalogIndexes.size());
    for (const qsizetype index : catalogIndexes) {
        if (index >= 0 && index < catalog->resources.size())
            request.resourceIds.append(catalog->resources.at(index).id);
    }
    return request;
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

struct Api038HardwareFixture
{
    QByteArray package;
    Data::RuntimeSemanticMappingProof proof;
    Data::RuntimeResourceId xb6Input;
    Data::RuntimeResourceId xb6Do0;
    Data::RuntimeConsistencyGroupId xb6OutputGroup;
    QList<Data::RuntimeResourceId> xb6Outputs;
};

QByteArray exactHexBytes(const QString &value, qsizetype byteCount)
{
    QString hex = value.trimmed();
    if (hex.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        hex.remove(0, 2);
    if (hex.size() != byteCount * 2)
        return {};
    const QByteArray decoded = QByteArray::fromHex(hex.toLatin1());
    if (decoded.size() != byteCount || QString::fromLatin1(decoded.toHex()) != hex.toLower())
        return {};
    return decoded;
}

QString readApi038FixtureFile(
    const QString &path, const QByteArray &expectedSha256, QByteArray *contents)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
    *contents = file.readAll();
    const QByteArray digest = QCryptographicHash::hash(*contents, QCryptographicHash::Sha256);
    if (digest != expectedSha256) {
        return QStringLiteral("%1 SHA-256 is %2, expected %3.")
            .arg(path, QString::fromLatin1(digest.toHex()),
                 QString::fromLatin1(expectedSha256.toHex()));
    }
    return {};
}

QString loadApi038HardwareFixture(
    const QString &packagePath, Api038HardwareFixture *fixture)
{
    const QByteArray expectedPackageSha
        = QByteArray::fromHex("d0b8edd70b5ecec53252ac4b09dc9665ac82b91178d18e2d94222b9a538165d0");
    const QByteArray expectedMappingSha
        = QByteArray::fromHex("a24a040d96a876fd4ac2753219aaa071d9fbbfb0a7db80d3adcb83872ec1f6e2");
    const QByteArray expectedActionDefinitionsSha
        = QByteArray::fromHex("f056912152f5c97e8306c5391118938fce93aee6768c7d35af2350533c72c210");

    const QFileInfo packageInfo(packagePath);
    if (!packageInfo.isAbsolute() || !packageInfo.isFile()) {
        return QStringLiteral(
                   "QTC_ETHER_CAT_API038_PACKAGE must name the absolute API-038 ECPKG path.");
    }
    QString error = readApi038FixtureFile(packagePath, expectedPackageSha, &fixture->package);
    if (!error.isEmpty())
        return error;
    if (fixture->package.size() != 391667)
        return QStringLiteral("The API-038 ECPKG size is not exactly 391667 bytes.");

    QByteArray mappingBytes;
    const QString mappingPath = packageInfo.dir().filePath(QStringLiteral("semantic-binding-v2.json"));
    error = readApi038FixtureFile(mappingPath, expectedMappingSha, &mappingBytes);
    if (!error.isEmpty())
        return error;

    QByteArray actionDefinitionBytes;
    const QString actionDefinitionsPath
        = packageInfo.dir().filePath(QStringLiteral("semantic-action-definitions-v1.json"));
    error = readApi038FixtureFile(
        actionDefinitionsPath, expectedActionDefinitionsSha, &actionDefinitionBytes);
    if (!error.isEmpty())
        return error;

    QJsonParseError parseError;
    const QJsonDocument mappingDocument = QJsonDocument::fromJson(mappingBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !mappingDocument.isObject())
        return QStringLiteral("The API-038 semantic binding artifact is not valid JSON.");
    const QJsonObject mapping = mappingDocument.object();
    if (mapping.value(QStringLiteral("format")).toString()
            != QStringLiteral("ethercat-semantic-binding-v2")
        || mapping.value(QStringLiteral("format_version")).toInt() != 2
        || mapping.value(QStringLiteral("binding_count")).toInt() != 56
        || mapping.value(QStringLiteral("device_count")).toInt() != 3
        || mapping.value(QStringLiteral("action_count")).toInt() != 8) {
        return QStringLiteral("The API-038 semantic binding envelope is not the frozen fixture.");
    }

    const QByteArray expectedGroup = QByteArray::fromHex("f0286f69");
    QSet<QByteArray> outputIds;
    int xb6InputCount = 0;
    for (const QJsonValue &value : mapping.value(QStringLiteral("bindings")).toArray()) {
        const QJsonObject binding = value.toObject();
        if (binding.value(QStringLiteral("project_device_id")).toString()
            != QStringLiteral("embedlabs:project:device:xb6")) {
            continue;
        }
        const QByteArray resourceId
            = exactHexBytes(binding.value(QStringLiteral("resource_id")).toString(), 8);
        const QByteArray groupId
            = exactHexBytes(binding.value(QStringLiteral("consistency_group_id")).toString(), 4);
        if (resourceId.isEmpty() || groupId.isEmpty())
            return QStringLiteral("An XB6 semantic binding has an invalid opaque identifier.");

        const QString definitionId
            = binding.value(QStringLiteral("semantic_signal_definition_id")).toString();
        if (definitionId == QStringLiteral("org.embedlabs.solidot.xb6.coupler.state")) {
            if (binding.value(QStringLiteral("direction")).toString() != QStringLiteral("input")
                || binding.value(QStringLiteral("access")).toString()
                       != QStringLiteral("read_only")
                || binding.value(QStringLiteral("primitive")).toString() != QStringLiteral("u16")
                || binding.value(QStringLiteral("bit_width")).toInt() != 16) {
                return QStringLiteral("The signed XB6 coupler input binding is inconsistent.");
            }
            fixture->xb6Input = {resourceId};
            ++xb6InputCount;
            continue;
        }

        if (!definitionId.startsWith(
                QStringLiteral("org.embedlabs.solidot.xb6.slot.1.digital-output.channel."))) {
            continue;
        }
        if (groupId != expectedGroup
            || binding.value(QStringLiteral("direction")).toString() != QStringLiteral("output")
            || binding.value(QStringLiteral("access")).toString()
                   != QStringLiteral("read_write")
            || binding.value(QStringLiteral("primitive")).toString() != QStringLiteral("bool")
            || binding.value(QStringLiteral("bit_width")).toInt() != 1
            || !binding.value(QStringLiteral("safe_value_declared")).toBool()
            || binding.value(QStringLiteral("safe_value")).toInt(-1) != 0
            || outputIds.contains(resourceId)) {
            return QStringLiteral("The signed XB6 DO16 group is incomplete or inconsistent.");
        }
        outputIds.insert(resourceId);
        if (definitionId.endsWith(QStringLiteral(".channel.0")))
            fixture->xb6Do0 = {resourceId};
    }
    if (xb6InputCount != 1 || !fixture->xb6Input.isValid() || !fixture->xb6Do0.isValid()
        || outputIds.size() != 16) {
        return QStringLiteral("The frozen XB6 input/DO16 bindings were not found exactly once.");
    }
    fixture->xb6OutputGroup = {expectedGroup};
    for (const QByteArray &resourceId : outputIds)
        fixture->xb6Outputs.append({resourceId});
    std::sort(
        fixture->xb6Outputs.begin(),
        fixture->xb6Outputs.end(),
        [](const Data::RuntimeResourceId &left, const Data::RuntimeResourceId &right) {
            return left.value < right.value;
        });

    int enabledXb6Actions = 0;
    int disabledSvActions = 0;
    for (const QJsonValue &value : mapping.value(QStringLiteral("actions")).toArray()) {
        const QJsonObject action = value.toObject();
        const QString deviceId = action.value(QStringLiteral("project_device_id")).toString();
        if (deviceId == QStringLiteral("embedlabs:project:device:xb6")) {
            if (!action.value(QStringLiteral("enabled")).toBool()
                || action.value(QStringLiteral("qualification")).toString()
                       != QStringLiteral("qualified")
                || !action.value(QStringLiteral("disabled_reason")).isNull()) {
                return QStringLiteral("An XB6 action is not signed as enabled and qualified.");
            }
            ++enabledXb6Actions;
        } else if (deviceId == QStringLiteral("embedlabs:project:device:axis0")
                   || deviceId == QStringLiteral("embedlabs:project:device:axis1")) {
            if (action.value(QStringLiteral("enabled")).toBool()
                || action.value(QStringLiteral("qualification")).toString()
                       != QStringLiteral("unqualified")
                || action.value(QStringLiteral("disabled_reason")).toString()
                       != QStringLiteral("reference_unit_to_rpm_conversion_not_bound")) {
                return QStringLiteral("An SV630N action is not signed fail closed.");
            }
            ++disabledSvActions;
        } else {
            return QStringLiteral("The frozen action artifact contains an unknown device.");
        }
    }
    if (enabledXb6Actions != 2 || disabledSvActions != 6) {
        return QStringLiteral("The frozen action instance set is not exactly 2 XB6 and 6 SV630N.");
    }

    parseError = {};
    const QJsonDocument actionDocument
        = QJsonDocument::fromJson(actionDefinitionBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !actionDocument.isObject())
        return QStringLiteral("The API-038 action definition companion is not valid JSON.");
    const QJsonObject definitions = actionDocument.object();
    if (definitions.value(QStringLiteral("format")).toString()
            != QStringLiteral("ethercat-semantic-action-definitions-v1")
        || definitions.value(QStringLiteral("format_version")).toInt() != 1
        || definitions.value(QStringLiteral("definition_count")).toInt() != 5
        || definitions.value(QStringLiteral("action_count")).toInt() != 8
        || definitions.value(QStringLiteral("semantic_binding_artifact_sha256")).toString()
               != QString::fromLatin1(expectedMappingSha.toHex())) {
        return QStringLiteral("The API-038 action definition envelope is not the frozen fixture.");
    }
    int enabledXb6Definitions = 0;
    int disabledSvDefinitions = 0;
    for (const QJsonValue &value : definitions.value(QStringLiteral("definitions")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QJsonObject definition = entry.value(QStringLiteral("definition")).toObject();
        const QString definitionId = definition.value(QStringLiteral("definition_id")).toString();
        if (definitionId.startsWith(QStringLiteral("org.embedlabs.solidot.xb6.action."))) {
            if (!definition.value(QStringLiteral("enabled")).toBool()
                || definition.value(QStringLiteral("qualification")).toString()
                       != QStringLiteral("qualified")
                || !definition.value(QStringLiteral("disabled_reason")).isNull()) {
                return QStringLiteral("An XB6 action definition is not enabled and qualified.");
            }
            ++enabledXb6Definitions;
        } else if (
            definitionId.startsWith(QStringLiteral("org.embedlabs.inovance.sv630n.action."))) {
            if (definition.value(QStringLiteral("enabled")).toBool()
                || definition.value(QStringLiteral("qualification")).toString()
                       != QStringLiteral("unqualified")
                || definition.value(QStringLiteral("disabled_reason")).toString()
                       != QStringLiteral("reference_unit_to_rpm_conversion_not_bound")) {
                return QStringLiteral("An SV630N definition is not signed fail closed.");
            }
            ++disabledSvDefinitions;
        } else {
            return QStringLiteral("The frozen companion contains an unknown action definition.");
        }
    }
    if (enabledXb6Definitions != 2 || disabledSvDefinitions != 3)
        return QStringLiteral("The frozen companion is not exactly 2 XB6 and 3 SV630N definitions.");

    fixture->proof.formatVersion = 2;
    fixture->proof.bindingCount = 56;
    fixture->proof.packageSigned = true;
    fixture->proof.signatureVerified = true;
    fixture->proof.semanticBindingVerified = true;
    fixture->proof.trust = Data::RuntimeSemanticMappingTrust::Production;
    fixture->proof.packageSha256 = expectedPackageSha;
    fixture->proof.manifestSha256
        = QByteArray::fromHex("930da674dd20f2d6d14dc37a39eff3d163687d330cfd898b8f36769d334729f7");
    fixture->proof.mappingSha256 = expectedMappingSha;
    fixture->proof.resourceRecordsSha256
        = QByteArray::fromHex("64d96130e7f9fa9f8443856c9277da9943e6da96f000d8d3b1f7e47e3f23fae7");
    fixture->proof.resourceSectionSha256
        = QByteArray::fromHex("59279a922261e014314f103a505da60939732c8fe43daf6ed3f61a20fa76ebee");
    fixture->proof.topologySha256
        = QByteArray::fromHex("ef5354449fc64c90ae2ef30fffebc5bdbe2f3792a3c14d7f8fd28847083c1f36");
    fixture->proof.signingKeyIdSha256
        = QByteArray::fromHex("eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6");
    if (!fixture->proof.isValid())
        return QStringLiteral("The frozen API-038 semantic attestation proof is invalid.");
    return {};
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

void EtherCATProductApiTests::testSemanticBindingAttestationGoldenFrames()
{
    constexpr quint64 sessionId = 0x0102030405060708;
    constexpr quint64 requestId = 0x4142434445464748;

    const Protocol::SemanticBindingAttestationQuery query
        = semanticBindingAttestationQuery();
    Protocol::Error error;
    const QByteArray request = Protocol::encodeQuerySemanticBindingAttestation(
        query,
        sessionId,
        requestId,
        4,
        Protocol::SemanticBindingAttestationMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(request.size(), Protocol::HeaderBytes + 64);
    QCOMPARE(readU32(request, 60), quint32(0x091ab694));
    QCOMPARE(request, semanticBindingAttestationQueryGoldenWire());

    const QByteArray response = semanticBindingAttestationGoldenWire();
    QCOMPARE(response.size(), Protocol::HeaderBytes + 320);
    QCOMPARE(readU32(response, 60), quint32(0xd4dc3ccd));
    Protocol::FrameParser parser(Protocol::Role::Bulk);
    const Protocol::ParseResult parsed = parser.append(response);
    QVERIFY(!parsed.error);
    QCOMPARE(parsed.frames.size(), 1);

    const auto attestation = Protocol::decodeSemanticBindingAttestation(
        parsed.frames.constFirst(), query, &error);
    QVERIFY(attestation);
    QVERIFY(!error);
    QCOMPARE(attestation->status, 0);
    QCOMPARE(attestation->binding.bootId, query.binding.bootId);
    QCOMPARE(attestation->binding.activeSlot, query.binding.activeSlot);
    QCOMPARE(attestation->binding.packageGeneration, query.binding.packageGeneration);
    QCOMPARE(attestation->binding.configurationId, query.binding.configurationId);
    QCOMPARE(attestation->binding.topologyGeneration, query.binding.topologyGeneration);
    QCOMPARE(attestation->binding.runtimeGeneration, query.binding.runtimeGeneration);
    QCOMPARE(attestation->binding.catalogRevision, query.binding.catalogRevision);
    QCOMPARE(attestation->binding.topologyIdentity, query.binding.topologyIdentity);
    QCOMPARE(attestation->formatVersion, quint16(1));
    QCOMPARE(
        attestation->securityFlags,
        Protocol::semanticBindingSecurityFlagValue(
            Protocol::SemanticBindingSecurityFlag::Signed)
            | Protocol::semanticBindingSecurityFlagValue(
                Protocol::SemanticBindingSecurityFlag::Verified)
            | Protocol::semanticBindingSecurityFlagValue(
                Protocol::SemanticBindingSecurityFlag::Production)
            | Protocol::semanticBindingSecurityFlagValue(
                Protocol::SemanticBindingSecurityFlag::Binding));
    QCOMPARE(attestation->bindingCount, quint32(56));
    QCOMPARE(attestation->packageSha256, QByteArray(32, char(0x01)));
    QCOMPARE(attestation->manifestSha256, QByteArray(32, char(0x02)));
    QCOMPARE(attestation->semanticMappingSha256, QByteArray(32, char(0x03)));
    QCOMPARE(attestation->resourceRecordsSha256, QByteArray(32, char(0x04)));
    QCOMPARE(attestation->resourceSectionSha256, QByteArray(32, char(0x05)));
    QCOMPARE(attestation->topologySha256, QByteArray(32, char(0x06)));
    QCOMPARE(attestation->signingKeyIdSha256, QByteArray(32, char(0x07)));

    QByteArray engineeringPayload = response.mid(Protocol::HeaderBytes);
    putU32(
        engineeringPayload,
        16,
        Protocol::semanticBindingSecurityFlagValue(
            Protocol::SemanticBindingSecurityFlag::Signed)
            | Protocol::semanticBindingSecurityFlagValue(
                Protocol::SemanticBindingSecurityFlag::Verified)
            | Protocol::semanticBindingSecurityFlagValue(
                Protocol::SemanticBindingSecurityFlag::Engineering)
            | Protocol::semanticBindingSecurityFlagValue(
                Protocol::SemanticBindingSecurityFlag::Binding));
    Protocol::Frame engineeringFrame = responseFrame(
        Protocol::MessageType::SemanticBindingAttestation, engineeringPayload);
    engineeringFrame.header.protocolMinor = Protocol::SemanticBindingAttestationMinor;
    engineeringFrame.header.bootId = query.binding.bootId;
    error = {};
    const auto engineering = Protocol::decodeSemanticBindingAttestation(
        engineeringFrame, query, &error);
    QVERIFY(engineering);
    QVERIFY(!error);

    const std::array<qint32, 9> typedFailures{
        -6,
        -14,
        -16,
        -17,
        -18,
        -21,
        -22,
        -23,
        -24,
    };
    for (const qint32 status : typedFailures) {
        QByteArray failurePayload(320, '\0');
        putU16(
            failurePayload,
            0,
            quint16(Protocol::MessageType::QuerySemanticBindingAttestation));
        putU16(failurePayload, 2, 320);
        putI32(failurePayload, 4, status);
        Protocol::Frame failureFrame = responseFrame(
            Protocol::MessageType::SemanticBindingAttestation,
            failurePayload,
            1,
            Protocol::Flag::Response | Protocol::Flag::Error);
        failureFrame.header.protocolMinor = Protocol::SemanticBindingAttestationMinor;
        failureFrame.header.bootId = query.binding.bootId;
        error = {};
        const auto failure = Protocol::decodeSemanticBindingAttestation(
            failureFrame, query, &error);
        QVERIFY2(failure, qPrintable(QString::number(status)));
        QVERIFY(!error);
        QCOMPARE(failure->status, status);
    }
}

void EtherCATProductApiTests::testSemanticBindingAttestationFormatV2()
{
    const Protocol::SemanticBindingAttestationQuery query
        = semanticBindingAttestationQuery();
    QByteArray payload
        = semanticBindingAttestationGoldenWire().mid(Protocol::HeaderBytes);
    putU16(payload, 12, 2);
    Protocol::Frame frame = responseFrame(
        Protocol::MessageType::SemanticBindingAttestation, payload);
    frame.header.protocolMinor = Protocol::SemanticBindingAttestationMinor;
    frame.header.bootId = query.binding.bootId;

    Protocol::Error error;
    const auto attestation
        = Protocol::decodeSemanticBindingAttestation(frame, query, &error);
    QVERIFY(attestation);
    QVERIFY(!error);
    QCOMPARE(attestation->formatVersion, quint16(2));
    QCOMPARE(
        attestation->semanticMappingSha256,
        QByteArray(32, char(0x03)));
}

void EtherCATProductApiTests::testSemanticBindingAttestationRejectsMalformed_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<quint32>("flags");

    const QByteArray valid
        = semanticBindingAttestationGoldenWire().mid(Protocol::HeaderBytes);
    const quint32 responseFlag = Protocol::flagValue(Protocol::Flag::Response);
    const quint32 errorFlags = Protocol::Flag::Response | Protocol::Flag::Error;

    QByteArray shortPayload = valid;
    shortPayload.chop(1);
    QTest::newRow("short") << shortPayload << responseFlag;

    QByteArray original = valid;
    putU16(original, 0, quint16(Protocol::MessageType::GetResourceSnapshot));
    QTest::newRow("original") << original << responseFlag;

    QByteArray structureBytes = valid;
    putU16(structureBytes, 2, 319);
    QTest::newRow("structure-bytes") << structureBytes << responseFlag;

    QByteArray status = valid;
    putI32(status, 4, -15);
    QTest::newRow("status") << status << errorFlags;

    QByteArray slot = valid;
    putU32(slot, 8, quint32('C'));
    QTest::newRow("slot") << slot << responseFlag;

    QByteArray format = valid;
    putU16(format, 12, 3);
    QTest::newRow("format") << format << responseFlag;

    QByteArray headerReserved = valid;
    putU16(headerReserved, 14, 1);
    QTest::newRow("header-reserved") << headerReserved << responseFlag;

    QByteArray unknownSecurity = valid;
    putU32(unknownSecurity, 16, 0x37);
    QTest::newRow("security-unknown") << unknownSecurity << responseFlag;

    for (const auto &[name, securityFlags] :
         std::array{
             std::pair{"security-missing-signed", quint32(0x16)},
             std::pair{"security-missing-verified", quint32(0x15)},
             std::pair{"security-missing-binding", quint32(0x07)},
             std::pair{"security-missing-environment", quint32(0x13)},
             std::pair{"security-both-environments", quint32(0x1f)},
         }) {
        QByteArray security = valid;
        putU32(security, 16, securityFlags);
        QTest::newRow(name) << security << responseFlag;
    }

    QByteArray bindingCount = valid;
    putU32(bindingCount, 20, 0);
    QTest::newRow("binding-count") << bindingCount << responseFlag;

    for (const qsizetype offset : std::array<qsizetype, 7>{80, 112, 144, 176, 208, 240, 272}) {
        QByteArray digest = valid;
        digest.replace(offset, 32, QByteArray(32, '\0'));
        QTest::newRow(qPrintable(QString::fromLatin1("digest-%1").arg(offset)))
            << digest << responseFlag;
    }

    for (const qsizetype offset :
         std::array<qsizetype, 7>{24, 32, 40, 48, 56, 64, 72}) {
        QByteArray binding = valid;
        putU64(binding, offset, readU64(binding, offset) + 1);
        QTest::newRow(qPrintable(QString::fromLatin1("binding-%1").arg(offset)))
            << binding << responseFlag;
    }

    QByteArray responseReserved = valid;
    responseReserved[319] = 1;
    QTest::newRow("response-reserved") << responseReserved << responseFlag;

    QTest::newRow("success-flags") << valid << errorFlags;

    QByteArray failure(320, '\0');
    putU16(
        failure, 0, quint16(Protocol::MessageType::QuerySemanticBindingAttestation));
    putU16(failure, 2, 320);
    putI32(failure, 4, -17);
    failure[319] = 1;
    QTest::newRow("failure-fields") << failure << errorFlags;

    failure[319] = 0;
    QTest::newRow("failure-flags") << failure << responseFlag;
}

void EtherCATProductApiTests::testSemanticBindingAttestationRejectsMalformed()
{
    const Protocol::SemanticBindingAttestationQuery validQuery
        = semanticBindingAttestationQuery();
    Protocol::Error error;
    QVERIFY(Protocol::encodeQuerySemanticBindingAttestation(
                validQuery,
                TestSessionId,
                1,
                1,
                Protocol::SemanticBindingAttestationMinor - 1,
                &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::IncompatibleVersion);
    QCOMPARE(error.status, std::optional<qint32>(-14));

    Protocol::SemanticBindingAttestationQuery invalidQuery = validQuery;
    invalidQuery.binding.configurationId = 0;
    error = {};
    QVERIFY(Protocol::encodeQuerySemanticBindingAttestation(
                invalidQuery, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray reservedRequest
        = semanticBindingAttestationQueryGoldenWire().mid(Protocol::HeaderBytes);
    putU64(reservedRequest, 56, 1);
    error = {};
    QVERIFY(Protocol::encodeRequest(
                Protocol::MessageType::QuerySemanticBindingAttestation,
                reservedRequest,
                TestSessionId,
                1,
                1,
                validQuery.binding.bootId,
                Protocol::CurrentMinor,
                &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::FrameParser requestRoleParser(
        Protocol::Role::Control, Protocol::FrameDirection::ClientRequest);
    const Protocol::ParseResult requestRole
        = requestRoleParser.append(semanticBindingAttestationQueryGoldenWire());
    QVERIFY(requestRole.error);
    QCOMPARE(requestRole.error->category, Protocol::ErrorCategory::UnsupportedMessage);

    Protocol::FrameParser responseRoleParser(Protocol::Role::Control);
    const Protocol::ParseResult responseRole
        = responseRoleParser.append(semanticBindingAttestationGoldenWire());
    QVERIFY(responseRole.error);
    QCOMPARE(responseRole.error->category, Protocol::ErrorCategory::UnsupportedMessage);

    for (const qint32 status : std::array<qint32, 4>{-6, -7, -8, -12}) {
        QByteArray preDispatch = bulkStatusPayload(
            quint16(Protocol::MessageType::QuerySemanticBindingAttestation));
        putI32(preDispatch, 0, status);
        putI32(preDispatch, 4, 0);
        error = {};
        const auto bulkStatus = Protocol::decodeBulkStatus(
            responseFrame(
                Protocol::MessageType::BulkStatus,
                preDispatch,
                1,
                Protocol::Flag::Response | Protocol::Flag::Error),
            &error);
        QVERIFY(bulkStatus);
        QVERIFY(!error);
        QCOMPARE(bulkStatus->status, status);
    }

    Protocol::Frame identityFrame = responseFrame(
        Protocol::MessageType::SemanticBindingAttestation,
        semanticBindingAttestationGoldenWire().mid(Protocol::HeaderBytes));
    identityFrame.header.protocolMinor = Protocol::SemanticBindingAttestationMinor;
    identityFrame.header.bootId = validQuery.binding.bootId + 1;
    error = {};
    QVERIFY(!Protocol::decodeSemanticBindingAttestation(
        identityFrame, validQuery, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::IdentityMismatch);

    identityFrame.header.bootId = validQuery.binding.bootId;
    identityFrame.header.requestId = 0;
    error = {};
    QVERIFY(!Protocol::decodeSemanticBindingAttestation(
        identityFrame, validQuery, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::IdentityMismatch);

    QFETCH(QByteArray, payload);
    QFETCH(quint32, flags);
    Protocol::Frame frame = responseFrame(
        Protocol::MessageType::SemanticBindingAttestation, payload, 1, flags);
    frame.header.protocolMinor = Protocol::SemanticBindingAttestationMinor;
    frame.header.bootId = validQuery.binding.bootId;
    error = {};
    QVERIFY(!Protocol::decodeSemanticBindingAttestation(frame, validQuery, &error));
    QVERIFY(error);
}

void EtherCATProductApiTests::testOutputTransactionGoldenFrames()
{
    constexpr quint64 sessionId = 0x0102030405060708;
    constexpr quint64 requestId = 0x4142434445464748;

    Protocol::Error error;
    const Protocol::OutputGroupPolicyQuery policyQuery = outputGroupPolicyQuery();
    const QByteArray policyRequest = Protocol::encodeQueryOutputGroupPolicy(
        policyQuery,
        sessionId,
        requestId,
        4,
        Protocol::OutputTransactionMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(policyRequest.size(), 168);
    QCOMPARE(readU32(policyRequest, 60), quint32(0x7e381a1c));
    QCOMPARE(policyRequest, outputGroupPolicyQueryGoldenWire());

    Protocol::FrameParser policyParser(Protocol::Role::Bulk);
    const Protocol::ParseResult policyParse
        = policyParser.append(outputGroupPolicyGoldenWire());
    QVERIFY(!policyParse.error);
    QCOMPARE(policyParse.frames.size(), 1);
    error = {};
    const auto policy
        = Protocol::decodeOutputGroupPolicy(policyParse.frames.constFirst(), policyQuery, &error);
    QVERIFY(policy);
    QVERIFY(!error);
    QCOMPARE(policy->status, 0);
    QCOMPARE(policy->binding.packageGeneration, quint64(7));
    QCOMPARE(policy->binding.configurationId, quint64(0x100));
    QCOMPARE(policy->consistencyGroupId, quint32(0x55));
    QCOMPARE(policy->recoveryPolicy, Protocol::OutputRecoveryPolicy::ReturnTask);
    QCOMPARE(policy->maximumTtlCycles, quint32(1000));
    QCOMPARE(policy->completeResourceCount, quint32(2));
    QCOMPARE(policy->currentOutputGeneration, quint64(9));
    QCOMPARE(policy->completeGroupRecordSha256, QByteArray(32, char(0x44)));
    QCOMPARE(policy->semanticMappingSha256, QByteArray(32, char(0x33)));

    const Protocol::OutputTransactionStateQuery stateQuery = outputTransactionStateQuery();
    error = {};
    const QByteArray stateRequest = Protocol::encodeGetOutputTransactionState(
        stateQuery,
        sessionId,
        requestId,
        6,
        Protocol::OutputTransactionMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(stateRequest.size(), 160);
    QCOMPARE(readU32(stateRequest, 60), quint32(0x5d5ace9a));
    QCOMPARE(stateRequest, outputTransactionStateQueryGoldenWire());

    Protocol::FrameParser stateParser(Protocol::Role::Bulk);
    const Protocol::ParseResult stateParse
        = stateParser.append(outputTransactionStateGoldenWire());
    QVERIFY(!stateParse.error);
    QCOMPARE(stateParse.frames.size(), 1);
    error = {};
    const auto state = Protocol::decodeOutputTransactionState(
        stateParse.frames.constFirst(), stateQuery, &error);
    QVERIFY(state);
    QVERIFY(!error);
    QCOMPARE(state->status, 0);
    QCOMPARE(state->state, Protocol::OutputTransactionState::Idle);
    QCOMPARE(state->binding.bootId, stateQuery.binding.bootId);
    QCOMPARE(state->binding.activeSlot, stateQuery.binding.activeSlot);
    QCOMPARE(state->binding.packageGeneration, stateQuery.binding.packageGeneration);
    QCOMPARE(state->binding.configurationId, stateQuery.binding.configurationId);
    QCOMPARE(state->binding.topologyGeneration, stateQuery.binding.topologyGeneration);
    QCOMPARE(state->binding.runtimeGeneration, stateQuery.binding.runtimeGeneration);
    QCOMPARE(state->binding.catalogRevision, stateQuery.binding.catalogRevision);
    QCOMPARE(state->binding.topologyIdentity, stateQuery.binding.topologyIdentity);
    QCOMPARE(state->outputGeneration, quint64(1));
    QCOMPARE(state->operationId, QByteArray(16, '\0'));
    QCOMPARE(state->semanticMappingSha256, QByteArray(32, char(0x33)));

    const Protocol::OutputTransactionRequest transaction = outputTransactionRequest();
    error = {};
    const QByteArray applyRequest = Protocol::encodeApplyOutputTransaction(
        transaction,
        sessionId,
        requestId,
        8,
        Protocol::OutputTransactionMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(applyRequest.size(), 256);
    QCOMPARE(readU32(applyRequest, 60), quint32(0x205a29e4));
    QCOMPARE(applyRequest, applyOutputTransactionGoldenWire());

    Protocol::FrameParser resultParser(Protocol::Role::Control);
    const Protocol::ParseResult resultParse
        = resultParser.append(outputTransactionResultGoldenWire());
    QVERIFY(!resultParse.error);
    QCOMPARE(resultParse.frames.size(), 1);
    error = {};
    const auto result = Protocol::decodeOutputTransactionResult(
        resultParse.frames.constFirst(), transaction, &error);
    QVERIFY(result);
    QVERIFY(!error);
    QCOMPARE(result->state, Protocol::OutputTransactionState::OverrideActive);
    QCOMPARE(
        result->resultFlags,
        Protocol::outputTransactionResultFlagValue(
            Protocol::OutputTransactionResultFlag::OverrideActive));
    QCOMPARE(result->operationId, transaction.operationId);
    QCOMPARE(result->appliedCycle, quint64(100));
    QCOMPARE(result->expiryCycle, quint64(105));
    QCOMPARE(result->outputGeneration, quint64(2));
    QCOMPARE(result->consistencyGroupId, quint32(0x55));
    QCOMPARE(result->ttlCycles, quint32(5));
    QCOMPARE(result->recoveryPolicy, quint32(Protocol::OutputRecoveryPolicy::ReturnTask));
    QCOMPARE(result->valueCount, quint16(2));
    QCOMPARE(result->controllerTimestampNs, quint64(0x6162636465666768));

    QByteArray replayedPayload
        = outputTransactionResultGoldenWire().mid(Protocol::HeaderBytes);
    putU32(
        replayedPayload,
        16,
        Protocol::outputTransactionResultFlagValue(
            Protocol::OutputTransactionResultFlag::Replayed)
            | Protocol::outputTransactionResultFlagValue(
                Protocol::OutputTransactionResultFlag::OverrideActive));
    putU64(replayedPayload, 160, 0x1122334455667788);
    Protocol::Frame replayedFrame = responseFrame(
        Protocol::MessageType::OutputTransactionResult, replayedPayload);
    replayedFrame.header.bootId = transaction.binding.bootId;
    error = {};
    const auto replayedResult = Protocol::decodeOutputTransactionResult(
        replayedFrame, transaction, &error);
    QVERIFY(replayedResult);
    QVERIFY(!error);
    QCOMPARE(replayedResult->detail, quint64(0x1122334455667788));

    QByteArray replayedStatePayload = replayedPayload;
    putU16(
        replayedStatePayload, 0, quint16(Protocol::MessageType::GetOutputTransactionState));
    Protocol::Frame replayedStateFrame = responseFrame(
        Protocol::MessageType::OutputTransactionState, replayedStatePayload);
    replayedStateFrame.header.bootId = stateQuery.binding.bootId;
    error = {};
    const auto replayedState = Protocol::decodeOutputTransactionState(
        replayedStateFrame, stateQuery, &error);
    QVERIFY(replayedState);
    QVERIFY(!error);
    QCOMPARE(replayedState->state, Protocol::OutputTransactionState::OverrideActive);
    QCOMPARE(
        replayedState->resultFlags,
        Protocol::outputTransactionResultFlagValue(
            Protocol::OutputTransactionResultFlag::Replayed)
            | Protocol::outputTransactionResultFlagValue(
                Protocol::OutputTransactionResultFlag::OverrideActive));
    QCOMPARE(replayedState->detail, quint64(0x1122334455667788));

    QByteArray safeHoldPayload
        = outputTransactionResultGoldenWire().mid(Protocol::HeaderBytes);
    putU16(safeHoldPayload, 0, quint16(Protocol::MessageType::GetOutputTransactionState));
    putU16(safeHoldPayload, 14, quint16(Protocol::OutputTransactionState::SafeHold));
    putU32(
        safeHoldPayload,
        16,
        Protocol::outputTransactionResultFlagValue(
            Protocol::OutputTransactionResultFlag::SafeHold));
    putU32(safeHoldPayload, 120, quint32(Protocol::OutputRecoveryPolicy::HoldSafe));
    Protocol::Frame safeHoldFrame = responseFrame(
        Protocol::MessageType::OutputTransactionState, safeHoldPayload);
    safeHoldFrame.header.bootId = stateQuery.binding.bootId;
    error = {};
    const auto safeHold = Protocol::decodeOutputTransactionState(
        safeHoldFrame, stateQuery, &error);
    QVERIFY(safeHold);
    QVERIFY(!error);
    QCOMPARE(safeHold->state, Protocol::OutputTransactionState::SafeHold);

    QByteArray returnedTaskPayload = safeHoldPayload;
    putU16(returnedTaskPayload, 14, quint16(Protocol::OutputTransactionState::Idle));
    putU32(
        returnedTaskPayload,
        16,
        Protocol::outputTransactionResultFlagValue(
            Protocol::OutputTransactionResultFlag::ReturnedTask));
    putU32(returnedTaskPayload, 120, quint32(Protocol::OutputRecoveryPolicy::ReturnTask));
    Protocol::Frame returnedTaskFrame = responseFrame(
        Protocol::MessageType::OutputTransactionState, returnedTaskPayload);
    returnedTaskFrame.header.bootId = stateQuery.binding.bootId;
    error = {};
    const auto returnedTask = Protocol::decodeOutputTransactionState(
        returnedTaskFrame, stateQuery, &error);
    QVERIFY(returnedTask);
    QVERIFY(!error);
    QCOMPARE(returnedTask->state, Protocol::OutputTransactionState::Idle);
}

void EtherCATProductApiTests::testOutputTransactionCodecRejectsMalformed_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<quint32>("flags");

    const QByteArray policyPayload
        = outputGroupPolicyGoldenWire().mid(Protocol::HeaderBytes);
    const QByteArray statePayload
        = outputTransactionStateGoldenWire().mid(Protocol::HeaderBytes);
    const QByteArray resultPayload
        = outputTransactionResultGoldenWire().mid(Protocol::HeaderBytes);

    QByteArray policyReserved = policyPayload;
    policyReserved[175] = 1;
    QTest::newRow("policy-reserved")
        << 0 << policyReserved << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray policyMapping = policyPayload;
    policyMapping[128] ^= 1;
    QTest::newRow("policy-mapping")
        << 0 << policyMapping << Protocol::flagValue(Protocol::Flag::Response);

    QTest::newRow("policy-flags")
        << 0 << policyPayload
        << quint32(Protocol::Flag::Response | Protocol::Flag::Error);

    QByteArray stateReplayed = statePayload;
    putU32(stateReplayed, 16, 1);
    QTest::newRow("state-replayed-without-operation")
        << 1 << stateReplayed << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray statePartial = statePayload;
    putU32(statePartial, 112, 0x55);
    QTest::newRow("state-partial")
        << 1 << statePartial << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray stateTimestamp = statePayload;
    putU64(stateTimestamp, 168, 0);
    QTest::newRow("state-zero-timestamp")
        << 1 << stateTimestamp << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray stateFailure(176, '\0');
    putU16(stateFailure, 0, quint16(Protocol::MessageType::GetOutputTransactionState));
    putU16(stateFailure, 2, 176);
    putI32(stateFailure, 4, -17);
    putI32(stateFailure, 8, -1);
    stateFailure[12] = 4;
    stateFailure[13] = 1;
    QTest::newRow("state-failure-result")
        << 1 << stateFailure
        << quint32(Protocol::Flag::Response | Protocol::Flag::Error);

    QByteArray resultOperation = resultPayload;
    resultOperation[24] ^= 1;
    QTest::newRow("result-operation-id")
        << 2 << resultOperation << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray resultGeneration = resultPayload;
    putU64(resultGeneration, 104, 3);
    QTest::newRow("result-generation")
        << 2 << resultGeneration << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray resultFlags = resultPayload;
    putU32(resultFlags, 16, 0x4);
    QTest::newRow("result-state-flags")
        << 2 << resultFlags << Protocol::flagValue(Protocol::Flag::Response);

    QByteArray resultTimestamp = resultPayload;
    putU64(resultTimestamp, 168, 0);
    QTest::newRow("result-zero-timestamp")
        << 2 << resultTimestamp << Protocol::flagValue(Protocol::Flag::Response);
}

void EtherCATProductApiTests::testOutputTransactionCodecRejectsMalformed()
{
    Protocol::Error error;
    Protocol::OutputGroupPolicyQuery policyQuery = outputGroupPolicyQuery();
    QVERIFY(Protocol::encodeQueryOutputGroupPolicy(
                policyQuery,
                TestSessionId,
                1,
                1,
                Protocol::OutputTransactionMinor - 1,
                &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::IncompatibleVersion);
    QCOMPARE(error.status, std::optional<qint32>(-14));

    policyQuery.semanticMappingSha256.fill('\0');
    error = {};
    QVERIFY(Protocol::encodeQueryOutputGroupPolicy(
                policyQuery, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::OutputTransactionRequest transaction = outputTransactionRequest();
    std::swap(transaction.values[0], transaction.values[1]);
    error = {};
    QVERIFY(Protocol::encodeApplyOutputTransaction(
                transaction, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    transaction = outputTransactionRequest();
    transaction.expectedCurrentOutputGeneration = std::numeric_limits<quint64>::max();
    error = {};
    QVERIFY(!Protocol::encodeApplyOutputTransaction(
                 transaction, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                 .isEmpty());
    QVERIFY(!error);

    transaction = outputTransactionRequest();
    transaction.values = {
        {0x1001,
         Protocol::RuntimeResourcePrimitive::FixedQ32_32,
         64,
         QByteArray::fromHex("0102030405060708")},
        {0x1002,
         Protocol::RuntimeResourcePrimitive::RawBits,
         9,
         QByteArray::fromHex("01ff")},
    };
    error = {};
    const QByteArray genericValues = Protocol::encodeApplyOutputTransaction(
        transaction, TestSessionId, 1, 1, Protocol::CurrentMinor, &error);
    QVERIFY(!genericValues.isEmpty());
    QVERIFY(!error);
    const QByteArray genericPayload = genericValues.mid(Protocol::HeaderBytes);
    QCOMPARE(genericPayload.mid(128 + 16, 8), QByteArray(8, '\0'));
    QCOMPARE(genericPayload.mid(128 + 24, 8), QByteArray::fromHex("0102030405060708"));
    QCOMPARE(genericPayload.mid(160 + 16, 14), QByteArray(14, '\0'));
    QCOMPARE(genericPayload.mid(160 + 30, 2), QByteArray::fromHex("01ff"));

    transaction.values[1].value = QByteArray::fromHex("02ff");
    error = {};
    QVERIFY(Protocol::encodeApplyOutputTransaction(
                transaction, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    transaction = outputTransactionRequest();
    transaction.values[0].primitive = Protocol::RuntimeResourcePrimitive::Boolean;
    transaction.values[0].bitWidth = 1;
    transaction.values[0].value = QByteArray(1, char(2));
    error = {};
    QVERIFY(Protocol::encodeApplyOutputTransaction(
                transaction, TestSessionId, 1, 1, Protocol::CurrentMinor, &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray rawApply = applyOutputTransactionGoldenWire().mid(Protocol::HeaderBytes);
    putU32(rawApply, 4, 1);
    error = {};
    QVERIFY(Protocol::encodeRequest(
                Protocol::MessageType::ApplyOutputTransaction,
                rawApply,
                TestSessionId,
                1,
                1,
                outputTransactionBinding().bootId,
                Protocol::CurrentMinor,
                &error)
                .isEmpty());
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    Protocol::FrameParser wrongPolicyRole(Protocol::Role::Control);
    const Protocol::ParseResult policyRole
        = wrongPolicyRole.append(outputGroupPolicyGoldenWire());
    QVERIFY(policyRole.error);
    QCOMPARE(policyRole.error->category, Protocol::ErrorCategory::UnsupportedMessage);

    for (const qint32 status : std::array<qint32, 4>{-6, -7, -8, -12}) {
        QByteArray preDispatch
            = bulkStatusPayload(quint16(Protocol::MessageType::QueryOutputGroupPolicy));
        putI32(preDispatch, 0, status);
        putI32(preDispatch, 4, 0);
        error = {};
        const auto bulkStatus = Protocol::decodeBulkStatus(
            responseFrame(
                Protocol::MessageType::BulkStatus,
                preDispatch,
                1,
                Protocol::Flag::Response | Protocol::Flag::Error),
            &error);
        QVERIFY(bulkStatus);
        QVERIFY(!error);
        QCOMPARE(bulkStatus->status, status);
    }

    for (quint16 stage = 1; stage <= 3; ++stage) {
        Protocol::Frame frame = responseFrame(
            Protocol::MessageType::CommandStatus,
            successfulCommandStatusPayload(
                Protocol::MessageType::ApplyOutputTransaction, stage, 3, false));
        frame.header.protocolMinor = Protocol::OutputTransactionMinor;
        error = {};
        const auto status = Protocol::decodeCommandStatus(frame, &error);
        QVERIFY(status);
        QVERIFY(!error);
        QVERIFY(!status->final);
    }

    struct Stage2Pair
    {
        qint32 status;
        qint32 operationResult;
        quint16 protocolMinor = Protocol::OutputTransactionMinor;
    };
    const std::array stage2Allowed{
        Stage2Pair{-14, -10, Protocol::OutputTransactionMinor - 1},
        Stage2Pair{-8, -3},
        Stage2Pair{-7, -2},
        Stage2Pair{-9, -2},
        Stage2Pair{-11, -2},
        Stage2Pair{-13, -2},
        Stage2Pair{-6, -2},
        Stage2Pair{-15, -2},
        Stage2Pair{-6, -1},
        Stage2Pair{-14, -1},
        Stage2Pair{-16, -1},
        Stage2Pair{-17, -1},
        Stage2Pair{-18, -1},
        Stage2Pair{-21, -1},
        Stage2Pair{-22, -1},
        Stage2Pair{-23, -1},
        Stage2Pair{-24, -1},
        Stage2Pair{-38, -1},
        Stage2Pair{-39, -1},
        Stage2Pair{-40, -1},
        Stage2Pair{-41, -1},
    };
    for (const Stage2Pair &pair : stage2Allowed) {
        Protocol::Frame frame = responseFrame(
            Protocol::MessageType::CommandStatus,
            rejectedCommandStatusPayload(
                Protocol::MessageType::ApplyOutputTransaction,
                2,
                3,
                pair.status,
                0,
                pair.operationResult),
            1,
            Protocol::Flag::Response | Protocol::Flag::Error);
        frame.header.protocolMinor = pair.protocolMinor;
        error = {};
        QVERIFY2(
            Protocol::decodeCommandStatus(frame, &error),
            qPrintable(
                QString::fromLatin1("stage2 status=%1 result=%2 minor=%3")
                    .arg(pair.status)
                    .arg(pair.operationResult)
                    .arg(pair.protocolMinor)));
        QVERIFY(!error);
    }

    const std::array stage2Rejected{
        Stage2Pair{-14, -10},
        Stage2Pair{-14, -2},
        Stage2Pair{-8, -2},
        Stage2Pair{-13, -1},
        Stage2Pair{-15, -1},
        Stage2Pair{-38, -2},
        Stage2Pair{-36, -7},
        Stage2Pair{-37, -8},
        Stage2Pair{-6, -10},
    };
    for (const Stage2Pair &pair : stage2Rejected) {
        Protocol::Frame frame = responseFrame(
            Protocol::MessageType::CommandStatus,
            rejectedCommandStatusPayload(
                Protocol::MessageType::ApplyOutputTransaction,
                2,
                3,
                pair.status,
                0,
                pair.operationResult),
            1,
            Protocol::Flag::Response | Protocol::Flag::Error);
        frame.header.protocolMinor = pair.protocolMinor;
        error = {};
        QVERIFY(!Protocol::decodeCommandStatus(frame, &error));
        QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    }

    Protocol::Frame nonLegacyMinor = responseFrame(
        Protocol::MessageType::CommandStatus,
        rejectedCommandStatusPayload(
            Protocol::MessageType::ApplyOutputTransaction, 2, 3, -8, 0, -3),
        1,
        Protocol::Flag::Response | Protocol::Flag::Error);
    nonLegacyMinor.header.protocolMinor = Protocol::OutputTransactionMinor - 1;
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(nonLegacyMinor, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::IncompatibleVersion);

    QByteArray cpu1Reject = rejectedCommandStatusPayload(
        Protocol::MessageType::ApplyOutputTransaction, 3, 3, -38, 0, -4);
    Protocol::Frame cpu1RejectFrame = responseFrame(
        Protocol::MessageType::CommandStatus,
        cpu1Reject,
        1,
        Protocol::Flag::Response | Protocol::Flag::Error);
    cpu1RejectFrame.header.protocolMinor = Protocol::OutputTransactionMinor;
    error = {};
    QVERIFY(Protocol::decodeCommandStatus(cpu1RejectFrame, &error));
    QVERIFY(!error);
    putI32(cpu1RejectFrame.payload, 4, -39);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(cpu1RejectFrame, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QByteArray unknownCpu1 = rejectedCommandStatusPayload(
        Protocol::MessageType::ApplyOutputTransaction, 3, 3, -16, 0, -99);
    Protocol::Frame unknownCpu1Frame = responseFrame(
        Protocol::MessageType::CommandStatus,
        unknownCpu1,
        1,
        Protocol::Flag::Response | Protocol::Flag::Error);
    unknownCpu1Frame.header.protocolMinor = Protocol::OutputTransactionMinor;
    error = {};
    QVERIFY(Protocol::decodeCommandStatus(unknownCpu1Frame, &error));
    QVERIFY(!error);
    putI32(unknownCpu1Frame.payload, 4, -38);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(unknownCpu1Frame, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);
    putI32(unknownCpu1Frame.payload, 4, -16);
    putI32(unknownCpu1Frame.payload, 8, 0);
    error = {};
    QVERIFY(!Protocol::decodeCommandStatus(unknownCpu1Frame, &error));
    QCOMPARE(error.category, Protocol::ErrorCategory::InvalidPayload);

    QFETCH(int, kind);
    QFETCH(QByteArray, payload);
    QFETCH(quint32, flags);

    const Protocol::MessageType type
        = kind == 0 ? Protocol::MessageType::OutputGroupPolicy
                    : kind == 1 ? Protocol::MessageType::OutputTransactionState
                                : Protocol::MessageType::OutputTransactionResult;
    Protocol::Frame frame = responseFrame(type, payload, 1, flags);
    frame.header.protocolMinor = Protocol::OutputTransactionMinor;
    frame.header.bootId = outputTransactionBinding().bootId;
    error = {};
    if (kind == 0) {
        QVERIFY(!Protocol::decodeOutputGroupPolicy(frame, outputGroupPolicyQuery(), &error));
    } else if (kind == 1) {
        QVERIFY(!Protocol::decodeOutputTransactionState(
            frame, outputTransactionStateQuery(), &error));
    } else {
        QVERIFY(!Protocol::decodeOutputTransactionResult(
            frame, outputTransactionRequest(), &error));
    }
    QVERIFY(error);
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
    QVERIFY(!capability->runtimeResources);
    QVERIFY(!capability->semanticMappingAttestation);
    QVERIFY(!capability->runtimeOutputTransactions);

    error = {};
    const auto runtimeCapability = Protocol::decodeCapability(
        responseFrame(Protocol::MessageType::Capability, descriptor), 0x3fff, &error);
    QVERIFY(runtimeCapability);
    QVERIFY(!error);
    QVERIFY(runtimeCapability->runtimeResources);
    QVERIFY(!runtimeCapability->semanticMappingAttestation);
    QVERIFY(!runtimeCapability->runtimeOutputTransactions);

    error = {};
    const auto semanticCapability = Protocol::decodeCapability(
        responseFrame(Protocol::MessageType::Capability, descriptor), 0x7fff, &error);
    QVERIFY(semanticCapability);
    QVERIFY(!error);
    QVERIFY(semanticCapability->runtimeResources);
    QVERIFY(semanticCapability->semanticMappingAttestation);
    QVERIFY(!semanticCapability->runtimeOutputTransactions);

    error = {};
    const auto outputCapability = Protocol::decodeCapability(
        responseFrame(Protocol::MessageType::Capability, descriptor), 0xffff, &error);
    QVERIFY(outputCapability);
    QVERIFY(!error);
    QVERIFY(outputCapability->runtimeResources);
    QVERIFY(outputCapability->semanticMappingAttestation);
    QVERIFY(outputCapability->runtimeOutputTransactions);

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
        Protocol::MessageType::QuerySemanticBindingAttestation,
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

void EtherCATProductApiTests::testPackageDeploymentMaximumAudit()
{
    LoopbackController controller(LoopbackController::Behavior::PackageDeployment);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);

    Data::ControllerControlRequest control;
    control.command = Data::ControllerControlCommand::AcquireControl;
    QVERIFY(provider.executeControlCommand(control));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controlProgress.state,
        Data::ControllerControlState::Succeeded,
        1000);

    Data::ControllerPackageDeploymentRequest request;
    request.operationId = QStringLiteral("deploy-maximum-audit");
    request.configurationId = 813;
    request.artifact = QByteArray(16 * 1024 * 1024, '\x5a');
    QVERIFY(provider.deployPackage(request));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().packageDeploymentProgress.state,
        Data::ControllerPackageDeploymentState::Succeeded,
        20000);

    const Data::ControllerPackageDeploymentProgress progress
        = provider.connectionSnapshot().packageDeploymentProgress;
    const qsizetype expectedChunkCount
        = (request.artifact.size() + qsizetype(Protocol::BulkChunkMaximumBytes) - 1)
          / qsizetype(Protocol::BulkChunkMaximumBytes);
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::BulkChunk),
        int(expectedChunkCount));
    // Every bulk request records queued and accepted evidence. The remaining
    // transcript consists of deployment start plus begin/commit (5 upload
    // events), validation (6 events), and activation (7 events).
    constexpr qsizetype ValidateAuditEvents = 6;
    constexpr qsizetype ActivateAuditEvents = 7;
    const qsizetype expectedUploadAuditEvents = 2 * expectedChunkCount + 5;
    const qsizetype expectedAuditEvents
        = expectedUploadAuditEvents + ValidateAuditEvents + ActivateAuditEvents;
    QCOMPARE(expectedChunkCount, qsizetype(257));
    QCOMPARE(progress.audit.size(), expectedAuditEvents);
    for (qsizetype index = 0; index < progress.audit.size(); ++index)
        QCOMPARE(progress.audit.at(index).sequence, quint64(index + 1));
    const auto countAuditOperation = [&progress](Data::ControllerOperation operation) {
        return qsizetype(std::count_if(
            progress.audit.cbegin(),
            progress.audit.cend(),
            [operation](const Data::ControllerPackageDeploymentAuditEvent &event) {
                return event.operation == operation;
            }));
    };
    QCOMPARE(
        countAuditOperation(Data::ControllerOperation::UploadPackage),
        expectedUploadAuditEvents);
    QCOMPARE(
        countAuditOperation(Data::ControllerOperation::ValidatePackage),
        ValidateAuditEvents);
    QCOMPARE(
        countAuditOperation(Data::ControllerOperation::ActivatePackage),
        ActivateAuditEvents);
    QCOMPARE(progress.audit.constFirst().operation, Data::ControllerOperation::UploadPackage);
    QCOMPARE(progress.audit.constLast().operation, Data::ControllerOperation::ActivatePackage);

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

void EtherCATProductApiTests::testApi038HardwareFixturePreflight()
{
    const QString packagePath = QFINDTESTDATA(
        "../ethercatsemanticruntime/testdata/api038/"
        "three-slave-manual-control-cfg3701.ecpkg");
    QVERIFY2(!packagePath.isEmpty(), "The tracked API-038 hardware fixture was not found.");
    Api038HardwareFixture fixture;
    const QString error = loadApi038HardwareFixture(packagePath, &fixture);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(fixture.package.size(), 391667);
    QVERIFY(fixture.proof.isValid());
    QCOMPARE(fixture.proof.formatVersion, quint16(2));
    QCOMPARE(fixture.proof.bindingCount, quint32(56));
    QCOMPARE(
        fixture.proof.packageSha256,
        QByteArray::fromHex(
            "d0b8edd70b5ecec53252ac4b09dc9665ac82b91178d18e2d94222b9a538165d0"));
    QCOMPARE(
        fixture.proof.mappingSha256,
        QByteArray::fromHex(
            "a24a040d96a876fd4ac2753219aaa071d9fbbfb0a7db80d3adcb83872ec1f6e2"));
    QCOMPARE(fixture.xb6OutputGroup.value, QByteArray::fromHex("f0286f69"));
    QCOMPARE(fixture.xb6Outputs.size(), 16);
    QVERIFY(fixture.xb6Input.isValid());
    QVERIFY(fixture.xb6Do0.isValid());
    QVERIFY(fixture.xb6Outputs.contains(fixture.xb6Do0));
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

// This is a Product API Provider transport gate, not a semantic-runtime end-to-end test.
// It must stay fail closed until the upper-layer signed ECPKG verifier can supply the exact
// section-8 output policy and sorted resource set without creating a reverse plugin dependency.
void EtherCATProductApiTests::testHardwareApi038ProviderAcceptance()
{
    constexpr auto EnableVariable = "QTC_ETHER_CAT_API038_HARDWARE";
    constexpr auto ConfirmationVariable = "QTC_ETHER_CAT_API038_CONFIRM";
    constexpr auto PackageVariable = "QTC_ETHER_CAT_API038_PACKAGE";
    constexpr auto ControllerImageProofVariable = "QTC_ETHER_CAT_API038_CONTROLLER_IMAGE_PROOF";
    constexpr auto Confirmation
        = "API038_CFG3701_D0B8EDD70B5ECEC53252AC4B09DC9665_XB6_DO16";
    constexpr auto ControllerImageProof
        = "CPU0_9390F8E60C9F2DB0C355D1C022020D5D8C115CA9304E1A9E38C0DE4CDE7757AB_"
          "CPU1_69317B41CA0F5066AD6C9177890396A21AF27050CCF85E9FC9F2FCD42BD531A7";
    constexpr int ConnectTimeoutMs = 20000;
    constexpr int OperationTimeoutMs = 60000;
    constexpr int ResourceTimeoutMs = 30000;
    constexpr quint64 ExpectedCatalogRevision = 0x9ffaf9e73061d964;
    const QByteArray expectedTopologyIdentity = QByteArray::fromHex("5b37fe1904c0300d");
    const QByteArray expectedCapabilitySha
        = QByteArray::fromHex("74ea5e67b3e1d7ba575339b636abb5432ecf6790d3504d32ce1756bcfec49568");

    if (qEnvironmentVariable(EnableVariable) != QStringLiteral("1")) {
        QSKIP("Provider-level API-038 hardware gate is disabled. Set "
              "QTC_ETHER_CAT_API038_HARDWARE=1 plus the exact confirmation variables only "
              "after an exclusive controller window is assigned.");
    }
    if (qEnvironmentVariable(ConfirmationVariable) != QString::fromLatin1(Confirmation)) {
        QFAIL("QTC_ETHER_CAT_API038_CONFIRM does not authorize the exact cfg3701/XB6 "
              "mutation fixture.");
    }
    if (qEnvironmentVariable(ControllerImageProofVariable)
        != QString::fromLatin1(ControllerImageProof)) {
        QFAIL("QTC_ETHER_CAT_API038_CONTROLLER_IMAGE_PROOF must identify the coordinated "
              "API-038 CPU0 and API-036 output-transaction CPU1 deployment.");
    }

    Api038HardwareFixture fixture;
    const QString fixtureError = loadApi038HardwareFixture(
        qEnvironmentVariable(PackageVariable).trimmed(), &fixture);
    if (!fixtureError.isEmpty())
        QFAIL(qPrintable(fixtureError));

    QSKIP("Provider-level API-038 hardware mutation remains blocked: EtherCATProductApi has no "
          "trusted signed-ECPKG/ECFG section-8 parser with which to compare the controller's "
          "completeGroupRecordDigest, policy, and exact sorted ResourceId set. Run the eventual "
          "RuntimePackageEvidenceRepository plus SemanticRuntime Executor acceptance gate; do "
          "not authorize this lower-level skeleton by trusting JSON or environment values.");

    qInfo().noquote()
        << "[Product API API-038] offline fixture gate passed; SV630N mutations=0"
        << "reason=reference_unit_to_rpm_conversion_not_bound";

    const ProductApiSession::EndpointSet endpoints{
        QStringLiteral("192.168.3.101"),
        15200,
        15201,
        15202,
        QStringLiteral("192.168.3.101:15200"),
    };
    ProductApiSession::Options options;
    options.connectTimeoutMs = 8000;
    options.handshakeTimeoutMs = 8000;
    options.requestTimeoutMs = 45000;
    options.reconnectAttempts = 0;
    options.liveStatePollIntervalMs = 250;
    options.runtimeResourceRefreshTimeoutMs = ResourceTimeoutMs;

    ProductApiConnectionProvider provider(endpoints, options);
    const Data::ControllerConnectionRequest connectionRequest = requestFor(provider);
    QString failure;
    QStringList cleanupFailures;
    bool connectionStarted = false;
    bool leaseAcquired = false;

    const auto recordFailure = [&failure](const QString &detail) {
        if (failure.isEmpty())
            failure = detail;
        qWarning().noquote() << "[Product API API-038] failure:" << detail;
    };
    const auto appendCleanupFailure = [&cleanupFailures](const QString &detail) {
        cleanupFailures.append(detail);
        qWarning().noquote() << "[Product API API-038] cleanup:" << detail;
    };
    const auto connected = [&provider] {
        const auto state = provider.connectionSnapshot().state;
        return state == Data::ControllerConnectionState::Connected
               || state == Data::ControllerConnectionState::Degraded;
    };
    const auto executeCommand = [&provider](
                                    Data::ControllerControlRequest request,
                                    const QString &label,
                                    int timeoutMs,
                                    QString *error) {
        const Utils::Result<> dispatch = provider.executeControlCommand(request);
        if (!dispatch) {
            *error = QStringLiteral("%1 could not be dispatched: %2")
                         .arg(hardwareCommandName(request.command), dispatch.error());
            return false;
        }
        const bool terminal = waitForHardwareCondition(
            [&provider, command = request.command] {
                const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
                return (snapshot.controlProgress.command == command
                        && (snapshot.controlProgress.state
                                == Data::ControllerControlState::Succeeded
                            || snapshot.controlProgress.state
                                   == Data::ControllerControlState::Failed))
                       || snapshot.state == Data::ControllerConnectionState::Disconnected
                       || snapshot.state == Data::ControllerConnectionState::Failed;
            },
            timeoutMs);
        logHardwareSnapshot(label, provider.connectionSnapshot());
        const Data::ControllerControlProgress progress
            = provider.connectionSnapshot().controlProgress;
        if (!terminal || progress.command != request.command
            || progress.state != Data::ControllerControlState::Succeeded) {
            const QString status
                = progress.status ? QString::number(*progress.status) : QStringLiteral("none");
            *error = QStringLiteral("%1 did not succeed (stage %2, status %3): %4")
                         .arg(hardwareCommandName(request.command))
                         .arg(progress.stage)
                         .arg(status, progress.detail);
            return false;
        }
        return true;
    };
    const auto mainCommand =
        [&failure, &recordFailure, &executeCommand](
            Data::ControllerControlRequest request, const QString &label) {
            if (!failure.isEmpty())
                return false;
            QString error;
            if (executeCommand(request, label, OperationTimeoutMs, &error))
                return true;
            recordFailure(error);
            return false;
        };

    const Utils::Result<> connectResult = provider.connectToController(connectionRequest);
    if (!connectResult) {
        recordFailure(QStringLiteral("Connection could not start: %1").arg(connectResult.error()));
    } else {
        connectionStarted = true;
        if (!waitForHardwareCondition(
                [&provider] {
                    const auto state = provider.connectionSnapshot().state;
                    return state == Data::ControllerConnectionState::Connected
                           || state == Data::ControllerConnectionState::Degraded
                           || state == Data::ControllerConnectionState::Failed;
                },
                ConnectTimeoutMs)) {
            recordFailure(QStringLiteral("The three-channel read-only handshake timed out."));
        }
    }

    if (failure.isEmpty()) {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        logHardwareSnapshot(QStringLiteral("initial read-only snapshot"), snapshot);
        if (!connected() || !snapshot.session || !snapshot.controllerState
            || !snapshot.capability || !snapshot.package) {
            recordFailure(QStringLiteral("The initial read-only snapshot is incomplete."));
        } else if (snapshot.session->controlLeaseOwnerSessionId
                   || snapshot.session->ownsControlLease) {
            recordFailure(
                QStringLiteral("Another session owns the controller lease; this gate will not "
                               "preempt it."));
        } else if (snapshot.protocolVersion.major != Protocol::CurrentMajor
                   || snapshot.protocolVersion.minor != Protocol::OutputTransactionMinor
                   || provider.sessionForTests()->negotiatedMinorForTests()
                          != Protocol::OutputTransactionMinor
                   || provider.sessionForTests()->controlFeatureBitsForTests() != 0x0000ffffU
                   || provider.sessionForTests()->pushFeatureBitsForTests() != 0x0000ffffU
                   || provider.sessionForTests()->bulkFeatureBitsForTests() != 0x0000ffffU) {
            recordFailure(
                QStringLiteral("The controller is not the exact Product API v1.14/0x0000ffff "
                               "three-channel runtime."));
        } else if (!snapshot.capability->transactionalBulk
                   || !snapshot.capability->runtimeResources
                   || !snapshot.capability->semanticMappingAttestation
                   || !snapshot.capability->runtimeOutputTransactions
                   || !snapshot.capability->distributedClocks
                   || snapshot.capability->descriptorSha256 != expectedCapabilitySha) {
            recordFailure(
                QStringLiteral("The controller capability does not match the signed cfg3701 "
                               "capability descriptor and v1.14 runtime services."));
        } else if (!snapshot.controllerState->ready
                   || snapshot.controllerState->serviceState
                          != Data::ControllerServiceState::Shutdown
                   || snapshot.controllerState->applicationActive
                   || snapshot.controllerState->busOperational
                   || snapshot.controllerState->currentFaults
                   || snapshot.controllerState->latchedFaults
                   || snapshot.package->controllerState == Data::ControllerPackageState::Active) {
            recordFailure(
                QStringLiteral("The controller must be fault-free READY/SHUTDOWN before API-038 "
                               "deployment; this gate never resets faults."));
        }
    }

    Data::ControllerControlRequest control;
    if (failure.isEmpty()) {
        control.command = Data::ControllerControlCommand::AcquireControl;
        control.leaseDurationMs = 30000;
        if (mainCommand(control, QStringLiteral("acquire exclusive lease"))) {
            const auto snapshot = provider.connectionSnapshot();
            leaseAcquired = snapshot.session && snapshot.session->ownsControlLease
                            && snapshot.session->controlLeaseOwnerSessionId
                                   == snapshot.session->sessionId;
            if (!leaseAcquired)
                recordFailure(QStringLiteral("Exclusive lease ownership was not confirmed."));
        }
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::EnterConfigurationMode;
        mainCommand(control, QStringLiteral("enter configuration"));
    }
    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::DiscoverTopology;
        control.firstStationAddress = 0x1001;
        control.topologyCapacity = 64;
        mainCommand(control, QStringLiteral("discover exact three-slave topology"));
    }
    if (failure.isEmpty()) {
        const auto topology = provider.connectionSnapshot().topology;
        const auto matchesSlave = [](const Data::ControllerTopologySlave &slave,
                                     quint32 position,
                                     quint16 station,
                                     quint32 vendor,
                                     quint32 product,
                                     quint32 revision) {
            return slave.position == position && slave.stationAddress == station
                   && slave.vendorId == vendor && slave.productCode == product
                   && slave.revision == revision;
        };
        if (!topology || topology->respondingCount != 3 || topology->slaves.size() != 3
            || !matchesSlave(topology->slaves.at(0), 0, 0x1001, 0x00884443, 0x000000b6, 1)
            || !matchesSlave(
                topology->slaves.at(1), 1, 0x1002, 0x00100000, 0x000c0112, 0x00010000)
            || !matchesSlave(
                topology->slaves.at(2), 2, 0x1003, 0x00100000, 0x000c0112, 0x00010000)) {
            recordFailure(
                QStringLiteral("The live topology is not exactly XB6 plus two SV630N rev "
                               "0x00010000 slaves."));
        }
    }

    if (failure.isEmpty()) {
        Data::ControllerPackageDeploymentRequest deployment;
        deployment.operationId
            = QStringLiteral("api038-cfg3701-d0b8edd70b5ecec5-single-session");
        deployment.artifact = fixture.package;
        deployment.configurationId = 3701;
        deployment.activate = true;
        deployment.rollbackOnActivationFailure = true;
        const Utils::Result<> result = provider.deployPackage(deployment);
        if (!result) {
            recordFailure(QStringLiteral("API-038 deployment could not start: %1").arg(
                result.error()));
        } else if (!waitForHardwareCondition(
                       [&provider] {
                           const auto state
                               = provider.connectionSnapshot().packageDeploymentProgress.state;
                           return state == Data::ControllerPackageDeploymentState::Succeeded
                                  || state == Data::ControllerPackageDeploymentState::Failed
                                  || state == Data::ControllerPackageDeploymentState::Canceled
                                  || state
                                         == Data::ControllerPackageDeploymentState::OutcomeUnknown;
                       },
                       120000)) {
            recordFailure(QStringLiteral("API-038 deployment did not reach a terminal outcome."));
        } else {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            const Data::ControllerPackageDeploymentProgress &progress
                = snapshot.packageDeploymentProgress;
            logHardwareSnapshot(QStringLiteral("API-038 deployment"), snapshot);
            if (progress.state != Data::ControllerPackageDeploymentState::Succeeded
                || progress.operationId != deployment.operationId
                || progress.artifactSha256 != fixture.proof.packageSha256
                || progress.totalBytes != fixture.package.size()
                || progress.transferredBytes != fixture.package.size() || !progress.candidate
                || progress.candidate->configurationId != 3701 || !snapshot.package
                || snapshot.package->activeSlot != progress.candidate->slot
                || snapshot.package->activeGeneration != progress.candidate->generation
                || snapshot.package->activeConfigurationId != 3701
                || snapshot.package->controllerState != Data::ControllerPackageState::Active
                || !snapshot.controllerState
                || snapshot.controllerState->serviceState
                       != Data::ControllerServiceState::OperationalSafe
                || snapshot.controllerState->currentFaults
                || snapshot.controllerState->latchedFaults) {
                recordFailure(
                    QStringLiteral("The exact signed cfg3701 package was not atomically "
                                   "activated into fault-free OP_SAFE."));
            }
        }
    }

    if (failure.isEmpty()) {
        const Utils::Result<> refresh = provider.refreshRuntimeResources();
        if (!refresh) {
            recordFailure(
                QStringLiteral("Runtime resource refresh could not start: %1").arg(refresh.error()));
        } else if (!waitForHardwareCondition(
                       [&provider] {
                           return provider.runtimeResourceCatalog().has_value()
                                  && provider.runtimeResourceSnapshot().has_value()
                                  && provider.sessionForTests()->pendingRequestCountForTests() == 0;
                       },
                       ResourceTimeoutMs)) {
            recordFailure(QStringLiteral("Runtime resource refresh did not complete."));
        }
    }

    QList<Data::RuntimeResourceDescriptor> xb6OutputDescriptors;
    if (failure.isEmpty()) {
        const auto catalog = provider.runtimeResourceCatalog();
        if (!catalog || catalog->resources.size() != 56 || catalog->epoch.configurationId != 3701
            || catalog->epoch.catalogRevision != ExpectedCatalogRevision
            || catalog->epoch.topologyIdentity != expectedTopologyIdentity) {
            recordFailure(QStringLiteral("The runtime catalog does not match the cfg3701 epoch."));
        } else {
            const auto findDescriptor = [&catalog](const Data::RuntimeResourceId &id)
                -> std::optional<Data::RuntimeResourceDescriptor> {
                const auto found = std::find_if(
                    catalog->resources.cbegin(),
                    catalog->resources.cend(),
                    [&id](const Data::RuntimeResourceDescriptor &descriptor) {
                        return descriptor.id == id;
                    });
                return found == catalog->resources.cend()
                           ? std::nullopt
                           : std::optional(*found);
            };
            const auto input = findDescriptor(fixture.xb6Input);
            if (!input || input->direction != Data::RuntimeResourceDirection::Input
                || input->access != Data::RuntimeResourceAccess::ReadOnly
                || input->primitiveType != Data::RuntimeResourcePrimitiveType::UnsignedInteger
                || input->bitWidth != 16) {
                recordFailure(QStringLiteral("The signed XB6 input is absent from the catalog."));
            }
            for (const Data::RuntimeResourceId &id : fixture.xb6Outputs) {
                const auto descriptor = findDescriptor(id);
                if (!descriptor
                    || descriptor->consistencyGroupId != fixture.xb6OutputGroup
                    || descriptor->direction != Data::RuntimeResourceDirection::Output
                    || descriptor->access != Data::RuntimeResourceAccess::ReadWrite
                    || descriptor->primitiveType != Data::RuntimeResourcePrimitiveType::Boolean
                    || descriptor->bitWidth != 1 || !descriptor->safeValue
                    || descriptor->safeValue->value.toBool()) {
                    recordFailure(
                        QStringLiteral("The complete signed XB6 DO16 group is absent or unsafe."));
                    break;
                }
                xb6OutputDescriptors.append(*descriptor);
            }
        }
    }

    if (failure.isEmpty()) {
        const auto catalog = *provider.runtimeResourceCatalog();
        Data::RuntimeSemanticMappingAttestationRequest request;
        request.correlationId = QStringLiteral("api038-exact-attestation");
        request.scope = catalog.scope;
        request.sessionGeneration = catalog.sessionGeneration;
        request.expectedEpoch = catalog.epoch;
        request.expectedProof = fixture.proof;
        QSignalSpy finished(
            &provider,
            &Core::ControllerConnectionProvider::
                runtimeSemanticMappingAttestationRequestFinished);
        const Utils::Result<> result
            = provider.requestRuntimeSemanticMappingAttestation(request);
        if (!result) {
            recordFailure(
                QStringLiteral("Semantic attestation could not start: %1").arg(result.error()));
        } else if (!waitForHardwareCondition([&finished] { return finished.count() == 1; },
                                             ResourceTimeoutMs)) {
            recordFailure(QStringLiteral("Semantic attestation timed out."));
        } else {
            const auto response = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
                finished.constFirst().constFirst());
            if (!response.isValid() || !response.attestation
                || response.attestation->proof != fixture.proof
                || response.attestation->epoch != catalog.epoch) {
                recordFailure(
                    QStringLiteral("The controller did not attest the exact signed API-038 "
                                   "package/mapping/epoch."));
            }
        }
    }

    const auto requestSnapshot =
        [&provider, &recordFailure, &failure](
            const QList<Data::RuntimeResourceId> &ids,
            const QString &correlationId) -> std::optional<Data::RuntimeResourceSnapshot> {
            if (!failure.isEmpty())
                return {};
            const auto catalog = provider.runtimeResourceCatalog();
            if (!catalog) {
                recordFailure(QStringLiteral("The runtime catalog disappeared."));
                return {};
            }
            Data::RuntimeResourceSnapshotRequest request;
            request.correlationId = correlationId;
            request.scope = catalog->scope;
            request.sessionGeneration = catalog->sessionGeneration;
            request.expectedEpoch = catalog->epoch;
            request.resourceIds = ids;
            QSignalSpy finished(
                &provider,
                &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
            const Utils::Result<> result = provider.requestRuntimeResourceSnapshot(request);
            if (!result) {
                recordFailure(
                    QStringLiteral("Targeted snapshot could not start: %1").arg(result.error()));
                return {};
            }
            if (!waitForHardwareCondition([&finished] { return finished.count() == 1; },
                                          ResourceTimeoutMs)) {
                recordFailure(QStringLiteral("Targeted runtime snapshot timed out."));
                return {};
            }
            const auto response = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
                finished.constFirst().constFirst());
            if (!response.isValid() || !response.snapshot) {
                recordFailure(QStringLiteral("Targeted runtime snapshot was rejected."));
                return {};
            }
            for (const Data::RuntimeResourceSample &sample : response.snapshot->samples) {
                if (sample.quality.state != Data::RuntimeResourceQualityState::Good) {
                    recordFailure(QStringLiteral("A targeted runtime sample is not Good."));
                    return {};
                }
            }
            return response.snapshot;
        };

    if (failure.isEmpty()) {
        const auto input = requestSnapshot(
            {fixture.xb6Input}, QStringLiteral("api038-xb6-input"));
        if (input) {
            qInfo().noquote() << "[Product API API-038] XB6 input snapshot value="
                              << input->samples.constFirst().value.value.toString()
                              << "captureCycle=" << input->captureCycle;
        }
    }

    std::optional<Data::RuntimeOutputGroupPolicy> policy;
    const auto queryPolicy =
        [&provider, &fixture, &recordFailure, &failure](
            const QString &correlationId) -> std::optional<Data::RuntimeOutputGroupPolicy> {
            if (!failure.isEmpty())
                return {};
            const auto catalog = provider.runtimeResourceCatalog();
            const auto attestation = provider.runtimeSemanticMappingAttestation();
            if (!catalog || !attestation) {
                recordFailure(QStringLiteral("The verified runtime context disappeared."));
                return {};
            }
            Data::RuntimeOutputGroupPolicyRequest request;
            request.correlationId = correlationId;
            request.scope = catalog->scope;
            request.sessionGeneration = catalog->sessionGeneration;
            request.expectedEpoch = catalog->epoch;
            request.consistencyGroupId = fixture.xb6OutputGroup;
            request.expectedMappingDigest = fixture.proof.mappingSha256;
            QSignalSpy finished(
                &provider,
                &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
            const Utils::Result<> result = provider.requestRuntimeOutputGroupPolicy(request);
            if (!result) {
                recordFailure(
                    QStringLiteral("Output policy query could not start: %1").arg(result.error()));
                return {};
            }
            if (!waitForHardwareCondition([&finished] { return finished.count() == 1; },
                                          ResourceTimeoutMs)) {
                recordFailure(QStringLiteral("Output policy query timed out."));
                return {};
            }
            const auto response = qvariant_cast<Data::RuntimeOutputGroupPolicyResult>(
                finished.constFirst().constFirst());
            if (!response.isValid() || !response.policy
                || response.policy->consistencyGroupId != fixture.xb6OutputGroup
                || response.policy->mappingDigest != fixture.proof.mappingSha256
                || response.policy->recoveryPolicy
                       != Data::RuntimeOutputRecoveryPolicy::HoldSafe
                || response.policy->maximumTtlCycles != 1000
                || response.policy->completeResourceCount != 16
                || !response.policy->manualWriteAllowed) {
                recordFailure(QStringLiteral("The signed XB6 HoldSafe output policy mismatched."));
                return {};
            }
            return response.policy;
        };

    const auto queryOutputState =
        [&provider, &fixture, &recordFailure, &failure](
            const QString &correlationId) -> std::optional<Data::RuntimeOutputTransactionState> {
            if (!failure.isEmpty())
                return {};
            const auto catalog = provider.runtimeResourceCatalog();
            if (!catalog) {
                recordFailure(QStringLiteral("The runtime catalog disappeared."));
                return {};
            }
            Data::RuntimeOutputTransactionStateRequest request;
            request.correlationId = correlationId;
            request.scope = catalog->scope;
            request.sessionGeneration = catalog->sessionGeneration;
            request.expectedEpoch = catalog->epoch;
            request.expectedMappingDigest = fixture.proof.mappingSha256;
            QSignalSpy finished(
                &provider,
                &Core::ControllerConnectionProvider::
                    runtimeOutputTransactionStateRequestFinished);
            const Utils::Result<> result
                = provider.requestRuntimeOutputTransactionState(request);
            if (!result) {
                recordFailure(
                    QStringLiteral("Output-state query could not start: %1").arg(result.error()));
                return {};
            }
            if (!waitForHardwareCondition([&finished] { return finished.count() == 1; },
                                          ResourceTimeoutMs)) {
                recordFailure(QStringLiteral("Output-state query timed out."));
                return {};
            }
            const auto response = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
                finished.constFirst().constFirst());
            if (!response.isValid() || !response.state) {
                recordFailure(QStringLiteral("Output-state query was rejected."));
                return {};
            }
            return response.state;
        };

    if (failure.isEmpty())
        policy = queryPolicy(QStringLiteral("api038-xb6-policy-before-run"));
    std::optional<Data::RuntimeOutputTransactionState> outputState;
    if (failure.isEmpty())
        outputState = queryOutputState(QStringLiteral("api038-output-state-before-run"));
    if (failure.isEmpty()
        && (!policy || !outputState
            || policy->currentOutputGeneration != outputState->outputGeneration)) {
        recordFailure(QStringLiteral("The output policy and state generations disagree."));
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::StartDistributedClocks;
        mainCommand(control, QStringLiteral("start cfg3701 DC runtime"));
    }
    quint64 dcCycleStart = 0;
    if (failure.isEmpty()) {
        const auto snapshot = provider.connectionSnapshot();
        if (!snapshot.controllerState
            || snapshot.controllerState->serviceState != Data::ControllerServiceState::Running
            || !snapshot.controllerState->applicationActive
            || !snapshot.controllerState->busOperational
            || !snapshot.controllerState->distributedClocksLocked
            || snapshot.controllerState->expectedWorkingCounter != 11
            || snapshot.controllerState->actualWorkingCounter != 11
            || snapshot.controllerState->currentFaults
            || snapshot.controllerState->latchedFaults) {
            recordFailure(QStringLiteral("cfg3701 did not enter fault-free DC RUNNING at WKC 11."));
        } else {
            dcCycleStart = snapshot.controllerState->cycleCount;
            if (!waitForHardwareCondition(
                    [&provider, dcCycleStart] {
                        const auto state = provider.connectionSnapshot().controllerState;
                        return state && state->cycleCount > dcCycleStart
                               && state->distributedClocksLocked;
                    },
                    2000)) {
                recordFailure(QStringLiteral("The DC cycle counter did not advance."));
            }
        }
    }

    const auto applyXb6 =
        [&provider,
         &fixture,
         &xb6OutputDescriptors,
         &recordFailure,
         &failure](
            const Data::RuntimeOutputGroupPolicy &currentPolicy,
            const Data::RuntimeOutputTransactionState &currentState,
            const QByteArray &operationId,
            bool setDo0) -> std::optional<Data::RuntimeOutputTransactionResult> {
            if (!failure.isEmpty())
                return {};
            Data::RuntimeOutputTransactionRequest request;
            request.operationId = {operationId};
            request.scope = currentPolicy.scope;
            request.sessionGeneration = currentPolicy.sessionGeneration;
            request.expectedEpoch = currentPolicy.epoch;
            request.expectedMappingDigest = currentPolicy.mappingDigest;
            request.expectedCompleteGroupRecordDigest
                = currentPolicy.completeGroupRecordDigest;
            request.expectedCompleteResourceCount = currentPolicy.completeResourceCount;
            request.expectedRecoveryPolicy = currentPolicy.recoveryPolicy;
            request.expectedMaximumTtlCycles = currentPolicy.maximumTtlCycles;
            request.expectedOutputGeneration = currentState.outputGeneration;
            request.ttlCycles = currentPolicy.maximumTtlCycles;
            request.consistencyGroupId = currentPolicy.consistencyGroupId;
            for (const Data::RuntimeResourceDescriptor &descriptor : xb6OutputDescriptors) {
                Data::RuntimeOutputValueWrite write;
                write.resourceId = descriptor.id;
                write.bitWidth = quint16(descriptor.bitWidth);
                write.value.primitiveType = descriptor.primitiveType;
                write.value.typeIdentity = descriptor.valueTypeIdentity;
                write.value.value = setDo0 && descriptor.id == fixture.xb6Do0;
                request.completeGroupWrites.append(write);
            }
            if (!request.isValid()) {
                recordFailure(QStringLiteral("The complete XB6 transaction is locally invalid."));
                return {};
            }
            QSignalSpy finished(
                &provider,
                &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
            const Utils::Result<> dispatch
                = provider.applyRuntimeOutputTransaction(request);
            if (!dispatch) {
                recordFailure(
                    QStringLiteral("Output transaction could not start: %1").arg(dispatch.error()));
                return {};
            }
            if (!waitForHardwareCondition([&finished] { return finished.count() == 1; },
                                          ResourceTimeoutMs)) {
                recordFailure(
                    QStringLiteral("Output transaction outcome is unknown; do not retry with a "
                                   "new OperationId."));
                return {};
            }
            const auto response = qvariant_cast<Data::RuntimeOutputTransactionResult>(
                finished.constFirst().constFirst());
            if (!response.isValid()
                || response.outcome != Data::RuntimeOutputTransactionOutcome::Applied
                || !response.finalResponseObserved || !response.state
                || response.state->operationId != request.operationId
                || response.state->scope != request.scope
                || response.state->sessionGeneration != request.sessionGeneration
                || response.state->epoch != request.expectedEpoch
                || response.state->mappingDigest != request.expectedMappingDigest
                || response.state->consistencyGroupId != request.consistencyGroupId
                || response.state->ttlCycles != request.ttlCycles
                || response.state->recoveryPolicy != request.expectedRecoveryPolicy
                || response.state->valueCount
                       != quint16(request.completeGroupWrites.size())
                || !response.state->appliedCycle
                || response.state->expiryCycle
                       != response.state->appliedCycle + request.ttlCycles
                || response.state->outputGeneration != request.expectedOutputGeneration + 1) {
                recordFailure(
                    QStringLiteral("The complete XB6 transaction was not proven atomically "
                                   "applied."));
                return {};
            }
            return response;
        };

    std::optional<Data::RuntimeOutputTransactionResult> setResult;
    if (failure.isEmpty()) {
        setResult = applyXb6(
            *policy,
            *outputState,
            QByteArray::fromHex("a038000000000001d0b8edd70b5ecec5"),
            true);
    }
    if (failure.isEmpty()) {
        const auto snapshot = requestSnapshot(
            fixture.xb6Outputs, QStringLiteral("api038-xb6-set-readback"));
        if (snapshot) {
            for (const Data::RuntimeResourceSample &sample : snapshot->samples) {
                const bool expected = sample.resourceId == fixture.xb6Do0;
                if (sample.value.primitiveType != Data::RuntimeResourcePrimitiveType::Boolean
                    || sample.value.value.toBool() != expected
                    || snapshot->captureCycle < setResult->state->appliedCycle) {
                    recordFailure(QStringLiteral("XB6 set readback did not match all 16 outputs."));
                    break;
                }
            }
        }
    }

    std::optional<Data::RuntimeOutputTransactionState> setRecovered;
    if (failure.isEmpty()) {
        if (!waitForHardwareCondition(
                [&provider, expiry = setResult->state->expiryCycle] {
                    const auto state = provider.connectionSnapshot().controllerState;
                    return state && state->cycleCount >= expiry;
                },
                2000)) {
            recordFailure(QStringLiteral("The controller did not reach the signed set expiryCycle."));
        }
    }
    if (failure.isEmpty()) {
        setRecovered = queryOutputState(QStringLiteral("api038-xb6-set-ttl-recovery"));
        if (setRecovered
            && (setRecovered->state != Data::RuntimeOutputState::SafeHold
                || !setRecovered->operationId
                || *setRecovered->operationId != setResult->request.operationId
                || !setRecovered->resultFlags.testFlag(
                    Data::RuntimeOutputTransactionResultFlag::SafeHold)
                || setRecovered->outputGeneration
                       != setResult->state->outputGeneration + 1)) {
            recordFailure(QStringLiteral("XB6 set TTL did not recover to signed HoldSafe."));
        }
    }
    if (failure.isEmpty()) {
        const auto snapshot = requestSnapshot(
            fixture.xb6Outputs, QStringLiteral("api038-xb6-set-safe-readback"));
        if (snapshot
            && (snapshot->samples.size() != 16
                || std::any_of(
                    snapshot->samples.cbegin(),
                    snapshot->samples.cend(),
                    [](const Data::RuntimeResourceSample &sample) {
                        return sample.value.value.toBool();
                    }))) {
            recordFailure(QStringLiteral("XB6 HoldSafe readback is not all false."));
        }
    }

    std::optional<Data::RuntimeOutputGroupPolicy> clearPolicy;
    if (failure.isEmpty())
        clearPolicy = queryPolicy(QStringLiteral("api038-xb6-policy-before-clear"));
    if (failure.isEmpty()
        && (!clearPolicy
            || clearPolicy->currentOutputGeneration != setRecovered->outputGeneration)) {
        recordFailure(QStringLiteral("The post-recovery output generation is stale."));
    }

    std::optional<Data::RuntimeOutputTransactionResult> clearResult;
    if (failure.isEmpty()) {
        clearResult = applyXb6(
            *clearPolicy,
            *setRecovered,
            QByteArray::fromHex("a038000000000002d0b8edd70b5ecec5"),
            false);
    }
    if (failure.isEmpty()) {
        const auto snapshot = requestSnapshot(
            fixture.xb6Outputs, QStringLiteral("api038-xb6-clear-readback"));
        if (snapshot
            && (snapshot->samples.size() != 16
                || std::any_of(
                    snapshot->samples.cbegin(),
                    snapshot->samples.cend(),
                    [](const Data::RuntimeResourceSample &sample) {
                        return sample.value.value.toBool();
                    }))) {
            recordFailure(QStringLiteral("XB6 clear readback is not all false."));
        }
    }
    if (failure.isEmpty()) {
        if (!waitForHardwareCondition(
                [&provider, expiry = clearResult->state->expiryCycle] {
                    const auto state = provider.connectionSnapshot().controllerState;
                    return state && state->cycleCount >= expiry;
                },
                2000)) {
            recordFailure(
                QStringLiteral("The controller did not reach the signed clear expiryCycle."));
        }
    }
    if (failure.isEmpty()) {
        const auto recovered = queryOutputState(QStringLiteral("api038-xb6-clear-ttl-recovery"));
        if (recovered
            && (recovered->state != Data::RuntimeOutputState::SafeHold
                || !recovered->operationId
                || *recovered->operationId != clearResult->request.operationId
                || recovered->outputGeneration
                       != clearResult->state->outputGeneration + 1)) {
            recordFailure(QStringLiteral("XB6 clear TTL did not end in signed HoldSafe."));
        }
        const auto snapshot = requestSnapshot(
            fixture.xb6Outputs, QStringLiteral("api038-xb6-final-safe-readback"));
        if (snapshot
            && (snapshot->samples.size() != 16
                || std::any_of(
                    snapshot->samples.cbegin(),
                    snapshot->samples.cend(),
                    [](const Data::RuntimeResourceSample &sample) {
                        return sample.value.value.toBool();
                    }))) {
            recordFailure(QStringLiteral("The final XB6 safe readback is not all false."));
        }
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::ControlledStop;
        mainCommand(control, QStringLiteral("controlled stop"));
    }
    if (failure.isEmpty()) {
        const auto state = provider.connectionSnapshot().controllerState;
        if (!state || state->serviceState != Data::ControllerServiceState::OperationalSafe
            || state->applicationActive || !state->busOperational || !state->safeOutput
            || state->currentFaults || state->latchedFaults) {
            recordFailure(QStringLiteral("Controlled stop did not return to fault-free OP_SAFE."));
        }
    }
    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::EnterConfigurationMode;
        mainCommand(control, QStringLiteral("final enter configuration"));
    }
    if (failure.isEmpty()) {
        const auto snapshot = provider.connectionSnapshot();
        if (!snapshot.controllerState
            || snapshot.controllerState->serviceState != Data::ControllerServiceState::Shutdown
            || snapshot.controllerState->applicationActive
            || snapshot.controllerState->busOperational || snapshot.controllerState->currentFaults
            || snapshot.controllerState->latchedFaults || !snapshot.package
            || snapshot.package->controllerState == Data::ControllerPackageState::Active) {
            recordFailure(
                QStringLiteral("Final state is not strict fault-free SHUTDOWN with no active "
                               "runtime."));
        }
    }
    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::ReleaseControl;
        if (mainCommand(control, QStringLiteral("release exclusive lease"))) {
            leaseAcquired = false;
            const auto session = provider.connectionSnapshot().session;
            if (!session || session->ownsControlLease
                || session->controlLeaseOwnerSessionId) {
                recordFailure(QStringLiteral("The lease was not authoritatively released."));
            }
        }
    }
    if (failure.isEmpty()) {
        const Utils::Result<> disconnectResult = provider.disconnectFromController();
        if (!disconnectResult
            || !waitForHardwareCondition(
                [&provider] {
                    return provider.connectionSnapshot().state
                           == Data::ControllerConnectionState::Disconnected;
                },
                ConnectTimeoutMs)) {
            recordFailure(QStringLiteral("Final disconnect did not complete."));
        }
    }

    if (!failure.isEmpty() && connectionStarted
        && provider.connectionSnapshot().state != Data::ControllerConnectionState::Disconnected) {
        qWarning().noquote() << "[Product API API-038] starting bounded safety cleanup";
        if (provider.connectionSnapshot().packageDeploymentProgress.state
                == Data::ControllerPackageDeploymentState::Uploading
            || provider.connectionSnapshot().packageDeploymentProgress.state
                   == Data::ControllerPackageDeploymentState::Committing) {
            const QString operationId
                = provider.connectionSnapshot().packageDeploymentProgress.operationId;
            const Utils::Result<> canceled = provider.cancelPackageDeployment(operationId);
            if (!canceled) {
                appendCleanupFailure(
                    QStringLiteral("Active upload could not be canceled: %1").arg(canceled.error()));
            } else {
                waitForHardwareCondition(
                    [&provider] {
                        const auto state
                            = provider.connectionSnapshot().packageDeploymentProgress.state;
                        return state == Data::ControllerPackageDeploymentState::Canceled
                               || state == Data::ControllerPackageDeploymentState::Failed;
                    },
                    OperationTimeoutMs);
            }
        }

        if (connected() && leaseAcquired) {
            const auto state = provider.connectionSnapshot().controllerState;
            if (state
                && (state->serviceState == Data::ControllerServiceState::Running
                    || state->serviceState == Data::ControllerServiceState::Paused)) {
                Data::ControllerControlRequest stop;
                stop.command = Data::ControllerControlCommand::ControlledStop;
                QString error;
                if (!executeCommand(stop, QStringLiteral("cleanup controlled stop"),
                                    OperationTimeoutMs, &error)) {
                    appendCleanupFailure(error);
                }
            }
            const auto afterStop = provider.connectionSnapshot().controllerState;
            if (afterStop
                && afterStop->serviceState != Data::ControllerServiceState::Fault
                && afterStop->serviceState != Data::ControllerServiceState::Shutdown) {
                Data::ControllerControlRequest configure;
                configure.command = Data::ControllerControlCommand::EnterConfigurationMode;
                QString error;
                if (!executeCommand(
                        configure, QStringLiteral("cleanup enter configuration"),
                        OperationTimeoutMs, &error)) {
                    appendCleanupFailure(error);
                }
            } else if (
                afterStop && afterStop->serviceState == Data::ControllerServiceState::Fault) {
                appendCleanupFailure(
                    QStringLiteral("Controller remains FAULT; ResetFault was deliberately not "
                                   "issued."));
            }

            Data::ControllerControlRequest release;
            release.command = Data::ControllerControlCommand::ReleaseControl;
            QString error;
            if (!executeCommand(
                    release, QStringLiteral("cleanup release lease"), OperationTimeoutMs, &error)) {
                appendCleanupFailure(error);
            } else {
                leaseAcquired = false;
                const auto session = provider.connectionSnapshot().session;
                if (!session || session->ownsControlLease
                    || session->controlLeaseOwnerSessionId) {
                    appendCleanupFailure(
                        QStringLiteral("Cleanup lease release was not authoritatively confirmed."));
                }
            }
        }

        if (connected()) {
            const Utils::Result<> disconnectResult = provider.disconnectFromController();
            if (!disconnectResult
                || !waitForHardwareCondition(
                    [&provider] {
                        return provider.connectionSnapshot().state
                               == Data::ControllerConnectionState::Disconnected;
                    },
                    ConnectTimeoutMs)) {
                appendCleanupFailure(
                    disconnectResult
                        ? QStringLiteral("Cleanup disconnect did not complete.")
                        : QStringLiteral("Cleanup disconnect could not start: %1")
                              .arg(disconnectResult.error()));
            }
        }
        if (provider.connectionSnapshot().state
            != Data::ControllerConnectionState::Disconnected) {
            provider.shutdown();
            appendCleanupFailure(
                QStringLiteral("The local session was force-closed after bounded cleanup."));
        }
        logHardwareSnapshot(QStringLiteral("cleanup final"), provider.connectionSnapshot());
    }

    if (!cleanupFailures.isEmpty()) {
        const QString detail = QStringLiteral("Cleanup issues: %1")
                                   .arg(cleanupFailures.join(QStringLiteral(" | ")));
        if (failure.isEmpty())
            failure = detail;
        else
            failure.append(QStringLiteral("\n") + detail);
    }
    if (failure.isEmpty()
        && (!provider.sessionForTests()->isIdleForTests()
            || provider.sessionForTests()->activeSocketCountForTests()
            || provider.sessionForTests()->pendingRequestCountForTests())) {
        failure = QStringLiteral(
            "The provider did not finish disconnected with zero sockets and pending requests.");
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

void EtherCATProductApiTests::testTargetedRuntimeResourceSnapshotLifecycle()
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
    const Data::RuntimeResourceSnapshot preview = *provider.runtimeResourceSnapshot();
    QVERIFY(!preview.complete);
    QCOMPARE(preview.samples.size(), 64);

    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    const Data::RuntimeResourceSnapshotRequest sixtyFifth
        = targetedSnapshotRequest(provider, {64}, "resource-65");
    QVERIFY(sixtyFifth.isValid());
    QVERIFY(provider.requestRuntimeResourceSnapshot(sixtyFifth));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    const Data::RuntimeResourceSnapshotResult sixtyFifthResult
        = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.at(0).constFirst());
    QVERIFY(sixtyFifthResult.isValid());
    QVERIFY(sixtyFifthResult.snapshot);
    QVERIFY(sixtyFifthResult.snapshot->complete);
    QCOMPARE(sixtyFifthResult.snapshot->samples.size(), 1);
    QCOMPARE(
        sixtyFifthResult.snapshot->samples.constFirst().resourceId,
        sixtyFifth.resourceIds.constFirst());
    QCOMPARE(provider.runtimeResourceSnapshot(), std::optional(preview));

    const Data::RuntimeResourceSnapshotRequest oneOfFirst64
        = targetedSnapshotRequest(provider, {10}, "resource-11");
    QVERIFY(provider.requestRuntimeResourceSnapshot(oneOfFirst64));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 1000);
    const Data::RuntimeResourceSnapshotResult first64Result
        = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.at(1).constFirst());
    QVERIFY(first64Result.isValid());
    QCOMPARE(
        first64Result.snapshot->samples.constFirst().resourceId,
        oneOfFirst64.resourceIds.constFirst());

    QList<qsizetype> indexes;
    indexes.reserve(64);
    for (qsizetype index = 0; index < 64; ++index)
        indexes.append(index);
    const Data::RuntimeResourceSnapshotRequest maximumBatch
        = targetedSnapshotRequest(provider, indexes, "resources-1-through-64");
    QVERIFY(provider.requestRuntimeResourceSnapshot(maximumBatch));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 3, 1000);
    const Data::RuntimeResourceSnapshotResult maximumResult
        = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.at(2).constFirst());
    QVERIFY(maximumResult.isValid());
    QCOMPARE(maximumResult.snapshot->samples.size(), 64);
    QCOMPARE(provider.runtimeResourceSnapshot(), std::optional(preview));

    QCOMPARE(controller.requestCount(Protocol::MessageType::GetResourceSnapshot), 4);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);
    QVERIFY(!controller.leaseOwned());
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testTargetedRuntimeResourceSnapshotLocalGuards()
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
    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    const int baselineRequests
        = controller.requestCount(Protocol::MessageType::GetResourceSnapshot);
    const auto rejectLocally = [&](const Data::RuntimeResourceSnapshotRequest &request) {
        QVERIFY(!provider.requestRuntimeResourceSnapshot(request));
        QCOMPARE(
            controller.requestCount(Protocol::MessageType::GetResourceSnapshot),
            baselineRequests);
        QCOMPARE(finishedSpy.count(), 0);
    };

    const Data::RuntimeResourceSnapshotRequest valid
        = targetedSnapshotRequest(provider, {0, 1}, "valid-local-guard");
    Data::RuntimeResourceSnapshotRequest invalid = valid;
    invalid.resourceIds.clear();
    rejectLocally(invalid);
    invalid = targetedSnapshotRequest(provider, {}, "empty");
    rejectLocally(invalid);
    invalid = valid;
    invalid.resourceIds.append(invalid.resourceIds.constLast());
    rejectLocally(invalid);
    invalid = valid;
    std::swap(invalid.resourceIds[0], invalid.resourceIds[1]);
    rejectLocally(invalid);

    QList<qsizetype> allIndexes;
    for (qsizetype index = 0; index < 65; ++index)
        allIndexes.append(index);
    rejectLocally(targetedSnapshotRequest(provider, allIndexes, "too-many"));

    invalid = valid;
    invalid.resourceIds = {{QByteArray::fromHex("ffffffffffffffff")}};
    rejectLocally(invalid);
    invalid = valid;
    ++invalid.expectedEpoch.catalogRevision;
    rejectLocally(invalid);
    invalid = valid;
    ++invalid.sessionGeneration;
    rejectLocally(invalid);
    invalid = valid;
    invalid.scope.masterId = Data::NodeId::create();
    rejectLocally(invalid);
    invalid = valid;
    invalid.resourceIds = {{QByteArray::fromHex("01020304050607")}};
    rejectLocally(invalid);
    invalid = valid;
    invalid.correlationId = QStringLiteral("bad\ncorrelation");
    rejectLocally(invalid);

    controller.setRuntimeSnapshotDelayMs(100);
    QVERIFY(provider.requestRuntimeResourceSnapshot(valid));
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.requestCount(Protocol::MessageType::GetResourceSnapshot),
        baselineRequests + 1,
        500);
    QVERIFY(!provider.requestRuntimeResourceSnapshot(valid));
    QVERIFY(!provider.refreshRuntimeResources());
    QVERIFY(!provider.refreshController());
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::GetResourceSnapshot),
        baselineRequests + 1);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QVERIFY(
        qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.constFirst().constFirst())
            .isValid());
    provider.sessionForTests()->failNextWriteForTests();
    QVERIFY(provider.requestRuntimeResourceSnapshot(valid));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 1000);
    const Data::RuntimeResourceSnapshotResult writeFailure
        = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.at(1).constFirst());
    QVERIFY(writeFailure.isValid());
    QCOMPARE(writeFailure.error->source, Data::ControllerErrorSource::Network);
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::GetResourceSnapshot),
        baselineRequests + 1);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        3000);
    QCOMPARE(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests(), 0);
    QCOMPARE(finishedSpy.count(), 2);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testTargetedRuntimeResourceSnapshotFailures()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    controller.setRuntimeResourceCount(65);
    QVERIFY(controller.start());

    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 50;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);
    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    const Data::RuntimeResourceSnapshotRequest request
        = targetedSnapshotRequest(provider, {64}, "failure-path");
    const Data::RuntimeResourceCatalog catalog = *provider.runtimeResourceCatalog();
    const Data::RuntimeResourceSnapshot preview = *provider.runtimeResourceSnapshot();

    controller.rejectNextRuntimeSnapshotTyped(-6);
    QVERIFY(provider.requestRuntimeResourceSnapshot(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    Data::RuntimeResourceSnapshotResult result
        = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.at(0).constFirst());
    QVERIFY(result.isValid());
    QCOMPARE(result.error->source, Data::ControllerErrorSource::Controller);
    QCOMPARE(result.error->code, std::optional<qint32>(-6));
    QCOMPARE(provider.runtimeResourceCatalog(), std::optional(catalog));
    QCOMPARE(provider.runtimeResourceSnapshot(), std::optional(preview));

    controller.rejectNextRuntimeSnapshotWithBulkStatus(-6);
    QVERIFY(provider.requestRuntimeResourceSnapshot(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 1000);
    result = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
        finishedSpy.at(1).constFirst());
    QVERIFY(result.isValid());
    QCOMPARE(result.error->code, std::optional<qint32>(-6));
    QCOMPARE(result.error->operationResult, std::optional<qint32>(0));
    QCOMPARE(provider.runtimeResourceCatalog(), std::optional(catalog));
    QCOMPARE(provider.runtimeResourceSnapshot(), std::optional(preview));

    controller.setRuntimeSnapshotDelayMs(150);
    QVERIFY(provider.requestRuntimeResourceSnapshot(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 3, 500);
    result = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
        finishedSpy.at(2).constFirst());
    QVERIFY(result.isValid());
    QCOMPARE(result.error->source, Data::ControllerErrorSource::Network);
    QCOMPARE(provider.runtimeResourceCatalog(), std::optional(catalog));
    QCOMPARE(provider.runtimeResourceSnapshot(), std::optional(preview));
    QVERIFY(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests() <= 64);
    QTest::qWait(200);
    QCOMPARE(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
    QCOMPARE(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests(), 0);
    QCOMPARE(finishedSpy.count(), 3);

    controller.setRuntimeSnapshotDelayMs(0);
    int expectedFinished = 3;
    for (const qint32 status : {-17, -18, -21}) {
        if (!provider.runtimeResourceCatalog()) {
            QVERIFY(provider.refreshRuntimeResources());
            QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);
        }
        const Data::RuntimeResourceSnapshotRequest staleRequest
            = targetedSnapshotRequest(
                provider,
                {64},
                QStringLiteral("invalidating-status-%1").arg(-status));
        controller.rejectNextRuntimeSnapshotTyped(status);
        QVERIFY(provider.requestRuntimeResourceSnapshot(staleRequest));
        ++expectedFinished;
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), expectedFinished, 1000);
        result = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.at(expectedFinished - 1).constFirst());
        QVERIFY(result.isValid());
        QCOMPARE(result.error->code, std::optional<qint32>(status));
        QTRY_VERIFY_WITH_TIMEOUT(
            !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 1000);
    }
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);
    const Data::RuntimeResourceSnapshotRequest bulkStaleRequest
        = targetedSnapshotRequest(provider, {64}, "bulk-status-stale");
    controller.rejectNextRuntimeSnapshotWithBulkStatus(-17);
    QVERIFY(provider.requestRuntimeResourceSnapshot(bulkStaleRequest));
    ++expectedFinished;
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), expectedFinished, 1000);
    result = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
        finishedSpy.at(expectedFinished - 1).constFirst());
    QVERIFY(result.isValid());
    // API-034 permits STALE_PACKAGE only in the typed ResourceSnapshot failure. A
    // pre-dispatch BulkStatus carrying it is protocol corruption, not a typed rejection.
    QCOMPARE(result.error->source, Data::ControllerErrorSource::Protocol);
    QVERIFY(!result.error->code);
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);

    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testTargetedRuntimeResourceProtocolFailures_data()
{
    QTest::addColumn<int>("failure");

    QTest::newRow("wrong-session-id") << 0;
    QTest::newRow("wrong-boot-id") << 1;
    QTest::newRow("malformed-payload") << 2;
    QTest::newRow("wrong-response-type") << 3;
    QTest::newRow("malformed-bulk-status") << 4;
    QTest::newRow("command-status-response") << 5;
    QTest::newRow("firmware-status-response") << 6;
}

void EtherCATProductApiTests::testTargetedRuntimeResourceProtocolFailures()
{
    QFETCH(int, failure);

    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    controller.setRuntimeResourceCount(65);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);

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
    case 5:
        controller.sendNextRuntimeCommandStatusResponse();
        break;
    case 6:
        controller.sendNextRuntimeFirmwareStatusResponse();
        break;
    default:
        QFAIL("Unknown targeted runtime resource protocol failure.");
    }

    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    const Data::RuntimeResourceSnapshotRequest request
        = targetedSnapshotRequest(provider, {64}, "protocol-failure");
    QVERIFY(provider.requestRuntimeResourceSnapshot(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    const Data::RuntimeResourceSnapshotResult result
        = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
            finishedSpy.constFirst().constFirst());
    QVERIFY(result.isValid());
    QCOMPARE(result.request, request);
    QCOMPARE(result.error->source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(
        result.error->operation,
        Data::ControllerOperation::QueryRuntimeResourceSnapshot);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        3000);
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testTargetedRuntimeResourceDisconnectAndControlInvalidation()
{
    {
        LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
        controller.setRuntimeResourceCount(65);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);

        controller.holdNextRuntimeSnapshot();
        QSignalSpy finishedSpy(
            &provider,
            &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
        const Data::RuntimeResourceSnapshotRequest request
            = targetedSnapshotRequest(provider, {64}, "disconnect-cancel");
        QVERIFY(provider.requestRuntimeResourceSnapshot(request));
        QVERIFY(provider.disconnectFromController());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
        const Data::RuntimeResourceSnapshotResult result
            = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
                finishedSpy.constFirst().constFirst());
        QVERIFY(result.isValid());
        QCOMPARE(result.request, request);
        QCOMPARE(result.error->source, Data::ControllerErrorSource::Network);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Disconnected,
            1000);
        QCOMPARE(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests(), 0);
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(controller.violations().isEmpty());
    }

    {
        LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
        controller.setRuntimeResourceCount(65);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            2000);
        Data::ControllerControlRequest acquire;
        acquire.command = Data::ControllerControlCommand::AcquireControl;
        QVERIFY(provider.executeControlCommand(acquire));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);

        controller.holdNextRuntimeSnapshot();
        QSignalSpy finishedSpy(
            &provider,
            &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
        const Data::RuntimeResourceSnapshotRequest request
            = targetedSnapshotRequest(provider, {64}, "control-invalidation");
        QVERIFY(provider.requestRuntimeResourceSnapshot(request));
        Data::ControllerControlRequest start;
        start.command = Data::ControllerControlCommand::Start;
        QVERIFY(provider.executeControlCommand(start));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
        const Data::RuntimeResourceSnapshotResult result
            = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
                finishedSpy.constFirst().constFirst());
        QVERIFY(result.isValid());
        QCOMPARE(result.request, request);
        QTRY_VERIFY_WITH_TIMEOUT(
            !provider.runtimeResourceCatalog() && !provider.runtimeResourceSnapshot(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().controlProgress.state,
            Data::ControllerControlState::Succeeded,
            2000);
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(controller.violations().isEmpty());
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }
}

void EtherCATProductApiTests::testTargetedRuntimeResourceIgnoredRequestBound()
{
    LoopbackController controller(LoopbackController::Behavior::RuntimeResources);
    controller.setRuntimeResourceCount(65);
    QVERIFY(controller.start());

    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 20;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 3000);
    const Data::RuntimeResourceCatalog catalog = *provider.runtimeResourceCatalog();
    const Data::RuntimeResourceSnapshot preview = *provider.runtimeResourceSnapshot();

    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    for (int index = 0; index < 64; ++index) {
        controller.holdNextRuntimeSnapshot();
        const Data::RuntimeResourceSnapshotRequest request
            = targetedSnapshotRequest(
                provider, {64}, QStringLiteral("timeout-%1").arg(index));
        QVERIFY(provider.requestRuntimeResourceSnapshot(request));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), index + 1, 500);
        const Data::RuntimeResourceSnapshotResult result
            = qvariant_cast<Data::RuntimeResourceSnapshotResult>(
                finishedSpy.at(index).constFirst());
        QVERIFY(result.isValid());
        QCOMPARE(result.error->source, Data::ControllerErrorSource::Network);
        QVERIFY(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests() <= 64);
    }
    QCOMPARE(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests(), 64);
    const int sentRequests
        = controller.requestCount(Protocol::MessageType::GetResourceSnapshot);
    const Data::RuntimeResourceSnapshotRequest capacityRequest
        = targetedSnapshotRequest(provider, {64}, "timeout-capacity");
    QVERIFY(!provider.requestRuntimeResourceSnapshot(capacityRequest));
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::GetResourceSnapshot),
        sentRequests);
    QCOMPARE(finishedSpy.count(), 64);
    QCOMPARE(provider.runtimeResourceCatalog(), std::optional(catalog));
    QCOMPARE(provider.runtimeResourceSnapshot(), std::optional(preview));
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QCOMPARE(provider.sessionForTests()->ignoredRuntimeResourceRequestCountForTests(), 0);
}

void EtherCATProductApiTests::testSemanticAttestationCapabilityGuards_data()
{
    QTest::addColumn<int>("protocolMinor");
    QTest::addColumn<quint32>("controlFeatures");
    QTest::addColumn<quint32>("bulkFeatures");
    QTest::addColumn<int>("topologyIdentityBytes");
    QTest::addColumn<bool>("expectedSupport");

    QTest::newRow("minor-12")
        << int(Protocol::SemanticBindingAttestationMinor - 1) << quint32(0xffff)
        << quint32(0xffff) << 8 << false;
    QTest::newRow("control-feature-missing")
        << int(Protocol::CurrentMinor)
        << quint32(0xffff & ~Protocol::SemanticBindingAttestationFeature)
        << quint32(0xffff) << 8 << false;
    QTest::newRow("bulk-feature-missing")
        << int(Protocol::CurrentMinor) << quint32(0xffff)
        << quint32(0xffff & ~Protocol::SemanticBindingAttestationFeature) << 8
        << false;
    QTest::newRow("topology-identity-1-byte")
        << int(Protocol::CurrentMinor) << quint32(0xffff) << quint32(0xffff)
        << 1 << true;
    QTest::newRow("topology-identity-7-bytes")
        << int(Protocol::CurrentMinor) << quint32(0xffff) << quint32(0xffff)
        << 7 << true;
    QTest::newRow("topology-identity-9-bytes")
        << int(Protocol::CurrentMinor) << quint32(0xffff) << quint32(0xffff)
        << 9 << true;
}

void EtherCATProductApiTests::testSemanticAttestationCapabilityGuards()
{
    QFETCH(int, protocolMinor);
    QFETCH(quint32, controlFeatures);
    QFETCH(quint32, bulkFeatures);
    QFETCH(int, topologyIdentityBytes);
    QFETCH(bool, expectedSupport);

    LoopbackController controller(LoopbackController::Behavior::SemanticAttestation);
    controller.setProtocolMinor(quint16(protocolMinor));
    controller.setFeatureBits(controlFeatures);
    controller.setRoleFeatureBits(Protocol::Role::Bulk, bulkFeatures);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);
    QCOMPARE(provider.supportsRuntimeSemanticMappingAttestation(), expectedSupport);
    QCOMPARE(
        provider.connectionSnapshot().capability->semanticMappingAttestation,
        expectedSupport);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());

    Data::RuntimeSemanticMappingAttestationRequest request
        = semanticAttestationRequest(provider);
    request.expectedEpoch.topologyIdentity = QByteArray(topologyIdentityBytes, 'x');
    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationRequestFinished);
    const int sentBefore = controller.requestCount(
        Protocol::MessageType::QuerySemanticBindingAttestation);
    QVERIFY(!provider.requestRuntimeSemanticMappingAttestation(request));
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::QuerySemanticBindingAttestation),
        sentBefore);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testSemanticAttestationLoopbackLifecycle()
{
    LoopbackController controller(LoopbackController::Behavior::SemanticAttestation);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);
    QVERIFY(provider.supportsRuntimeSemanticMappingAttestation());

    QSignalSpy changedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationChanged);
    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationRequestFinished);

    const Data::RuntimeSemanticMappingAttestationRequest productionRequest
        = semanticAttestationRequest(provider);
    QVERIFY(productionRequest.isValid());
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(productionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 1, 1000);
    Data::RuntimeSemanticMappingAttestationResult result
        = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
            finishedSpy.constFirst().constFirst());
    QVERIFY(result.isValid());
    QCOMPARE(result.request, productionRequest);
    QCOMPARE(result.attestation, provider.runtimeSemanticMappingAttestation());
    QCOMPARE(
        result.attestation->proof.trust,
        Data::RuntimeSemanticMappingTrust::Production);
    QCOMPARE(
        controller.requestCount(
            Protocol::MessageType::QuerySemanticBindingAttestation),
        1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::AcquireControl), 0);

    Data::RuntimeSemanticMappingProof engineeringProof = runtimeSemanticMappingProof(
        Data::RuntimeSemanticMappingTrust::Engineering);
    controller.setSemanticMappingProof(engineeringProof);
    controller.setSemanticAttestationDelayMs(50);
    const Data::RuntimeSemanticMappingAttestationRequest engineeringRequest
        = semanticAttestationRequest(
            provider,
            engineeringProof,
            loopbackRuntimeResourceBinding(TestBootId, 33, 44),
            QStringLiteral("engineering-attestation"));
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(engineeringRequest));
    QVERIFY(!provider.requestRuntimeSemanticMappingAttestation(engineeringRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.requestCount(
            Protocol::MessageType::QuerySemanticBindingAttestation),
        2,
        1000);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 2, 1000);
    result = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
        finishedSpy.constLast().constFirst());
    QVERIFY(result.isValid());
    QCOMPARE(
        provider.runtimeSemanticMappingAttestation()->proof.trust,
        Data::RuntimeSemanticMappingTrust::Engineering);

    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());
    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(
        !provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testSemanticAttestationFailures()
{
    LoopbackController controller(LoopbackController::Behavior::SemanticAttestation);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);
    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationRequestFinished);

    const Data::RuntimeSemanticMappingAttestationRequest request
        = semanticAttestationRequest(provider);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());

    controller.rejectNextSemanticAttestationTyped(-22);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 1000);
    Data::RuntimeSemanticMappingAttestationResult failure
        = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
            finishedSpy.constLast().constFirst());
    QVERIFY(failure.isValid());
    QVERIFY(!failure.attestation);
    QCOMPARE(failure.error->source, Data::ControllerErrorSource::Controller);
    QCOMPARE(
        failure.error->operation,
        Data::ControllerOperation::QueryRuntimeSemanticMappingAttestation);
    QCOMPARE(failure.error->channelId, QStringLiteral("bulk"));
    QCOMPARE(failure.error->code, std::optional<qint32>(-22));
    QCOMPARE(failure.error->codeName, QStringLiteral("PACKAGE_UNTRUSTED"));
    QCOMPARE(failure.error->requestId.has_value(), true);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QCOMPARE(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected);

    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 3, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());

    controller.rejectNextSemanticAttestationWithBulkStatus(-7);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 4, 1000);
    failure = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
        finishedSpy.constLast().constFirst());
    QVERIFY(failure.isValid());
    QCOMPARE(failure.error->source, Data::ControllerErrorSource::Controller);
    QCOMPARE(failure.error->code, std::optional<qint32>(-7));
    QCOMPARE(failure.error->codeName, QStringLiteral("BAD_SESSION"));
    QCOMPARE(failure.error->operationResult, std::optional<qint32>(0));
    QCOMPARE(
        failure.error->retryDisposition,
        Data::ControllerRetryDisposition::Reconnect);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testSemanticAttestationProtocolFailures_data()
{
    QTest::addColumn<int>("failure");
    QTest::newRow("wrong-session") << 0;
    QTest::newRow("wrong-boot") << 1;
    QTest::newRow("wrong-epoch") << 2;
    QTest::newRow("malformed-payload") << 3;
    QTest::newRow("wrong-type") << 4;
    QTest::newRow("digest-mismatch") << 5;
}

void EtherCATProductApiTests::testSemanticAttestationProtocolFailures()
{
    QFETCH(int, failure);
    LoopbackController controller(LoopbackController::Behavior::SemanticAttestation);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);
    const Data::RuntimeSemanticMappingAttestationRequest request
        = semanticAttestationRequest(provider);
    switch (failure) {
    case 0:
        controller.corruptNextSemanticAttestationSessionId();
        break;
    case 1:
        controller.corruptNextSemanticAttestationBootId();
        break;
    case 2:
        controller.corruptNextSemanticAttestationEpoch();
        break;
    case 3:
        controller.corruptNextSemanticAttestationPayload();
        break;
    case 4:
        controller.sendWrongNextSemanticAttestationResponse();
        break;
    case 5: {
        Data::RuntimeSemanticMappingProof mismatch = runtimeSemanticMappingProof();
        mismatch.mappingSha256[0] ^= char(1);
        controller.setSemanticMappingProof(mismatch);
        break;
    }
    default:
        QFAIL("Unknown semantic attestation failure.");
    }

    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    const Data::RuntimeSemanticMappingAttestationResult result
        = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
            finishedSpy.constFirst().constFirst());
    QVERIFY(result.isValid());
    QVERIFY(!result.attestation);
    QCOMPARE(result.error->source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(
        result.error->operation,
        Data::ControllerOperation::QueryRuntimeSemanticMappingAttestation);
    QVERIFY(result.error->requestId);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Disconnected,
        1000);
    QVERIFY(controller.violations().isEmpty());
}

void EtherCATProductApiTests::testSemanticAttestationInvalidationAndStaleResponse()
{
    LoopbackController controller(LoopbackController::Behavior::SemanticAttestation);
    QVERIFY(controller.start());

    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 30;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    const Data::ControllerConnectionRequest connectionRequest = requestFor(provider);
    QVERIFY(provider.connectToController(connectionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);

    QSignalSpy changedSpy(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationChanged);
    QSignalSpy finishedSpy(
        &provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationRequestFinished);
    Data::RuntimeSemanticMappingAttestationRequest request
        = semanticAttestationRequest(provider);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());

    controller.setRuntimeEpoch(70, 80, 90, 0x4142434445464748);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.runtimeResourceCatalog()
            && provider.runtimeResourceCatalog()->epoch.topologyGeneration == 70,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(!provider.runtimeSemanticMappingAttestation(), 1000);

    Protocol::RuntimeResourceBinding changedBinding
        = loopbackRuntimeResourceBinding(TestBootId, 33, 44);
    changedBinding.topologyGeneration = 70;
    changedBinding.runtimeGeneration = 80;
    changedBinding.catalogRevision = 90;
    changedBinding.topologyIdentity = 0x4142434445464748;
    request = semanticAttestationRequest(
        provider,
        runtimeSemanticMappingProof(),
        changedBinding,
        QStringLiteral("changed-epoch"));
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());

    controller.setSemanticAttestationDelayMs(80);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 3, 1000);
    Data::RuntimeSemanticMappingAttestationResult timedOut
        = qvariant_cast<Data::RuntimeSemanticMappingAttestationResult>(
            finishedSpy.constLast().constFirst());
    QVERIFY(timedOut.isValid());
    QCOMPARE(timedOut.error->source, Data::ControllerErrorSource::Network);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QTest::qWait(100);
    QCOMPARE(finishedSpy.count(), 3);
    QCOMPARE(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected);

    controller.setSemanticAttestationDelayMs(0);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 4, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());
    const quint64 oldGeneration = provider.connectionSnapshot().sessionGeneration;
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    controller.setBootId(TestBootId + 1);
    QVERIFY(provider.connectToController(connectionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state,
        Data::ControllerConnectionState::Connected,
        2000);
    QVERIFY(provider.connectionSnapshot().sessionGeneration > oldGeneration);
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QVERIFY(changedSpy.count() >= 4);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionCapabilityGuards_data()
{
    QTest::addColumn<int>("protocolMinor");
    QTest::addColumn<quint32>("controlFeatures");
    QTest::addColumn<quint32>("bulkFeatures");
    QTest::addColumn<bool>("expectedSupport");

    QTest::newRow("minor-13") << int(Protocol::OutputTransactionMinor - 1) << quint32(0xffff)
                              << quint32(0xffff) << false;
    QTest::newRow("control-feature-missing")
        << int(Protocol::CurrentMinor) << quint32(0xffff & ~Protocol::OutputTransactionFeature)
        << quint32(0xffff) << false;
    QTest::newRow("bulk-feature-missing")
        << int(Protocol::CurrentMinor) << quint32(0xffff)
        << quint32(0xffff & ~Protocol::OutputTransactionFeature) << false;
    QTest::newRow("supported") << int(Protocol::CurrentMinor) << quint32(0xffff) << quint32(0xffff)
                               << true;
}

void EtherCATProductApiTests::testOutputTransactionCapabilityGuards()
{
    QFETCH(int, protocolMinor);
    QFETCH(quint32, controlFeatures);
    QFETCH(quint32, bulkFeatures);
    QFETCH(bool, expectedSupport);

    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    controller.setProtocolMinor(quint16(protocolMinor));
    controller.setFeatureBits(controlFeatures);
    controller.setRoleFeatureBits(Protocol::Role::Bulk, bulkFeatures);
    QVERIFY(controller.start());

    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QCOMPARE(provider.supportsRuntimeOutputTransactions(), expectedSupport);
    QCOMPARE(provider.connectionSnapshot().capability->runtimeOutputTransactions, expectedSupport);

    Data::RuntimeOutputGroupPolicyRequest request;
    request.correlationId = QStringLiteral("unsupported-output-policy");
    request.scope = provider.connectionSnapshot().scope;
    request.sessionGeneration = provider.connectionSnapshot().sessionGeneration;
    request.expectedEpoch = runtimeResourceEpochForTests(
        loopbackRuntimeResourceBinding(TestBootId, 33, 44));
    request.consistencyGroupId.value.resize(4);
    qToBigEndian(quint32(2), reinterpret_cast<uchar *>(request.consistencyGroupId.value.data()));
    request.expectedMappingDigest = runtimeSemanticMappingProof().mappingSha256;
    QVERIFY(request.isValid());
    const int before = controller.requestCount(Protocol::MessageType::QueryOutputGroupPolicy);
    QVERIFY(!provider.requestRuntimeOutputGroupPolicy(request));
    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryOutputGroupPolicy), before);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionLoopbackLifecycle()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.supportsRuntimeOutputTransactions());

    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
    QVERIFY(provider.runtimeSemanticMappingAttestation());

    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    const Data::RuntimeOutputGroupPolicyRequest policyRequest = outputPolicyRequest(provider);
    QVERIFY(policyRequest.isValid());
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(policyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
    const auto policyResult = qvariant_cast<Data::RuntimeOutputGroupPolicyResult>(
        policyFinished.constFirst().constFirst());
    QVERIFY(policyResult.isValid());
    QCOMPARE(policyResult.policy->currentOutputGeneration, quint64(1));
    QCOMPARE(policyResult.policy->completeResourceCount, quint32(1));

    QSignalSpy stateChanged(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionStateChanged);
    QSignalSpy stateInvalidated(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateInvalidated);
    QSignalSpy stateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    const Data::RuntimeOutputTransactionStateRequest stateRequest = outputStateRequest(provider);
    QVERIFY(provider.requestRuntimeOutputTransactionState(stateRequest));
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
    QCOMPARE(stateChanged.count(), 1);
    auto stateResult = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
        stateFinished.constFirst().constFirst());
    QVERIFY(stateResult.isValid());
    QCOMPARE(stateResult.state->state, Data::RuntimeOutputState::Idle);
    QVERIFY(!stateResult.state->operationId);
    QCOMPARE(stateResult.state->outputGeneration, quint64(1));

    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

    QSignalSpy applyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
    const Data::RuntimeOutputTransactionRequest applyRequest = outputApplyRequest(provider);
    QVERIFY(applyRequest.isValid());
    QVERIFY(provider.applyRuntimeOutputTransaction(applyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
    auto applyResult = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constFirst().constFirst());
    QVERIFY(applyResult.isValid());
    QCOMPARE(applyResult.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
    QVERIFY(applyResult.finalResponseObserved);
    QCOMPARE(applyResult.state->outputGeneration, quint64(2));
    QCOMPARE(stateChanged.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(stateInvalidated.count(), 1, 1000);

    QVERIFY(provider.applyRuntimeOutputTransaction(applyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 2, 1000);
    applyResult = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(applyResult.isValid());
    QVERIFY(applyResult.finalResponseObserved);
    QVERIFY(applyResult.state->resultFlags.testFlag(
        Data::RuntimeOutputTransactionResultFlag::Replayed));

    controller.setOutputStateReportsReplayed(true);
    QVERIFY(provider.requestRuntimeOutputTransactionState(stateRequest));
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 2, 1000);
    stateResult = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
        stateFinished.constLast().constFirst());
    QVERIFY(stateResult.isValid());
    QVERIFY(stateResult.state->resultFlags.testFlag(
        Data::RuntimeOutputTransactionResultFlag::Replayed));
    QCOMPARE(stateResult.state->providerDetail, quint64(0));
    QVERIFY(stateResult.state->controllerTimestampNs);
    QCOMPARE(stateChanged.count(), 2);
    QCOMPARE(stateInvalidated.count(), 1);

    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryOutputGroupPolicy), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ApplyOutputTransaction), 2);
    QVERIFY(controller.violations().isEmpty());
    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionFailuresAndGuards()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);

    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    const Data::RuntimeOutputGroupPolicyRequest policyRequest = outputPolicyRequest(provider);
    controller.rejectNextOutputPolicyTyped(-41);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(policyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
    auto policyFailure = qvariant_cast<Data::RuntimeOutputGroupPolicyResult>(
        policyFinished.constLast().constFirst());
    QVERIFY(policyFailure.isValid());
    QCOMPARE(policyFailure.error->code, std::optional<qint32>(-41));
    QCOMPARE(policyFailure.error->codeName, QStringLiteral("OUTPUT_POLICY_UNAVAILABLE"));

    QVERIFY(provider.requestRuntimeOutputGroupPolicy(policyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 2, 1000);
    const Data::RuntimeOutputTransactionRequest applyRequest = outputApplyRequest(provider);
    const int applyBefore = controller.requestCount(Protocol::MessageType::ApplyOutputTransaction);
    QVERIFY(!provider.applyRuntimeOutputTransaction(applyRequest));
    QCOMPARE(controller.requestCount(Protocol::MessageType::ApplyOutputTransaction), applyBefore);

    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);
    QSignalSpy applyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);

    controller.rejectNextOutputApply(2, -41, -1);
    QVERIFY(provider.applyRuntimeOutputTransaction(applyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
    auto rejected = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(rejected.isValid());
    QCOMPARE(rejected.outcome, Data::RuntimeOutputTransactionOutcome::Rejected);
    QCOMPARE(rejected.error->code, std::optional<qint32>(-41));

    controller.rejectNextOutputApply(3, -36, -7);
    QVERIFY(provider.applyRuntimeOutputTransaction(applyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 2, 1000);
    rejected = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(rejected.isValid());
    QCOMPARE(rejected.error->code, std::optional<qint32>(-36));
    QCOMPARE(rejected.error->codeName, QStringLiteral("OPERATION_CONFLICT"));

    controller.rejectNextOutputApply(3, -37, -8);
    QVERIFY(provider.applyRuntimeOutputTransaction(applyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 3, 1000);
    rejected = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(rejected.isValid());
    QCOMPARE(rejected.error->code, std::optional<qint32>(-37));

    controller.rejectNextOutputApply(3, -40, -6);
    QVERIFY(provider.applyRuntimeOutputTransaction(applyRequest));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 4, 1000);
    rejected = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(rejected.isValid());
    QCOMPARE(rejected.error->code, std::optional<qint32>(-40));

    Data::RuntimeOutputTransactionRequest conflicting = applyRequest;
    conflicting.ttlCycles = 6;
    const int sentBeforeConflict = controller.requestCount(
        Protocol::MessageType::ApplyOutputTransaction);
    QVERIFY(!provider.applyRuntimeOutputTransaction(conflicting));
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::ApplyOutputTransaction), sentBeforeConflict);

    QSignalSpy stateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    controller.rejectNextOutputStateTyped(-17);
    QVERIFY(provider.requestRuntimeOutputTransactionState(outputStateRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
    const auto stateFailure = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
        stateFinished.constFirst().constFirst());
    QVERIFY(stateFailure.isValid());
    QCOMPARE(stateFailure.error->code, std::optional<qint32>(-17));
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(controller.violations().isEmpty());
    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionCompleteGroupGuards()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    controller.includeSecondOutputInPolicyGroup();
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
    const auto policyResult = qvariant_cast<Data::RuntimeOutputGroupPolicyResult>(
        policyFinished.constFirst().constFirst());
    QVERIFY(policyResult.isValid());
    QCOMPARE(policyResult.policy->completeResourceCount, quint32(2));

    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

    const Data::RuntimeOutputTransactionRequest complete = outputApplyRequest(provider);
    QVERIFY(complete.isValid());
    QCOMPARE(complete.completeGroupWrites.size(), 2);
    const int applyBefore = controller.requestCount(Protocol::MessageType::ApplyOutputTransaction);

    Data::RuntimeOutputTransactionRequest differentType = complete;
    differentType.completeGroupWrites[0].value.typeIdentity.append("-different");
    QVERIFY(differentType.isValid());
    QVERIFY(!provider.applyRuntimeOutputTransaction(differentType));

    Data::RuntimeOutputTransactionRequest missing = complete;
    missing.completeGroupWrites.removeLast();
    missing.expectedCompleteResourceCount = 1;
    QVERIFY(missing.isValid());
    QVERIFY(!provider.applyRuntimeOutputTransaction(missing));

    const auto catalog = provider.runtimeResourceCatalog();
    QVERIFY(catalog);
    QVERIFY(catalog->resources.size() >= 3);
    Data::RuntimeOutputValueWrite substituted;
    substituted.resourceId = catalog->resources.constFirst().id;
    substituted.bitWidth = quint16(catalog->resources.constFirst().bitWidth);
    substituted.value.primitiveType = catalog->resources.constFirst().primitiveType;
    substituted.value.typeIdentity = catalog->resources.constFirst().valueTypeIdentity;
    substituted.value.value = QVariant::fromValue<qulonglong>(1);
    Data::RuntimeOutputTransactionRequest exchanged = complete;
    exchanged.completeGroupWrites = {substituted, complete.completeGroupWrites.constFirst()};
    QVERIFY(exchanged.isValid());
    QVERIFY(!provider.applyRuntimeOutputTransaction(exchanged));

    Data::RuntimeOutputTransactionRequest saturated = complete;
    saturated.expectedOutputGeneration = std::numeric_limits<quint64>::max();
    QVERIFY(!saturated.isValid());
    QVERIFY(!provider.applyRuntimeOutputTransaction(saturated));
    saturated.expectedOutputGeneration = std::numeric_limits<quint64>::max() - quint64(1);
    QVERIFY(!saturated.isValid());
    QVERIFY(!provider.applyRuntimeOutputTransaction(saturated));

    QCOMPARE(controller.requestCount(Protocol::MessageType::ApplyOutputTransaction), applyBefore);
    QVERIFY(controller.violations().isEmpty());
    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionTimeoutReconciliation()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 30;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

    QSignalSpy applyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
    QSignalSpy stateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    controller.setOutputStateReportsReplayed(true);
    controller.holdNextOutputApplyResult();
    const Data::RuntimeOutputTransactionRequest request = outputApplyRequest(provider);
    QVERIFY(provider.applyRuntimeOutputTransaction(request));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 2, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
    QVERIFY(controller.hasHeldOutputResult());

    const auto unknown = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.at(0).constFirst());
    QVERIFY(unknown.isValid());
    QCOMPARE(unknown.outcome, Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);
    QVERIFY(!unknown.finalResponseObserved);
    const auto reconciled = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.at(1).constFirst());
    QVERIFY(reconciled.isValid());
    QCOMPARE(reconciled.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
    QVERIFY(!reconciled.finalResponseObserved);
    QCOMPARE(reconciled.request.operationId, request.operationId);
    QVERIFY(!provider.connectionSnapshot().lastError);
    QCOMPARE(controller.requestCount(Protocol::MessageType::ApplyOutputTransaction), 1);
    QCOMPARE(controller.requestCount(Protocol::MessageType::GetOutputTransactionState), 1);

    controller.releaseHeldOutputResult();
    QTest::qWait(80);
    QCOMPARE(applyFinished.count(), 2);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);

    controller.holdNextOutputApplyResponseSequence();
    Data::RuntimeOutputTransactionRequest delayedSequence = outputApplyRequest(
        provider, QByteArray::fromHex("11112233445566778899aabbccddeeff"));
    delayedSequence.expectedOutputGeneration = 2;
    QVERIFY(delayedSequence.isValid());
    QVERIFY(provider.applyRuntimeOutputTransaction(delayedSequence));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 3, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 2, 1000);
    QVERIFY(controller.hasHeldOutputApplyResponseSequence());
    const auto sequenceUnknown = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(sequenceUnknown.isValid());
    QCOMPARE(
        sequenceUnknown.outcome,
        Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);

    controller.releaseHeldOutputApplyResponseSequence();
    QTest::qWait(80);
    QCOMPARE(applyFinished.count(), 3);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
    QVERIFY(provider.applyRuntimeOutputTransaction(delayedSequence));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 4, 1000);
    const auto delayedReplay = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(delayedReplay.isValid());
    QCOMPARE(delayedReplay.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
    QVERIFY(delayedReplay.finalResponseObserved);
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::ApplyOutputTransaction),
        3);
    QVERIFY(controller.violations().isEmpty());
    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionControlledStopPreemptsRead()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 30;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    QVERIFY(provider.connectToController(requestFor(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
        semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);

    QSignalSpy rejectedStopStateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    controller.holdNextOutputStateResponse();
    QVERIFY(provider.requestRuntimeOutputTransactionState(outputStateRequest(provider)));
    Data::ControllerControlRequest ineligibleStop;
    ineligibleStop.command = Data::ControllerControlCommand::ControlledStop;
    QVERIFY(!provider.executeControlCommand(ineligibleStop));
    QVERIFY(!provider.requestRuntimeOutputTransactionState(outputStateRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(rejectedStopStateFinished.count(), 1, 1000);

    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);
    Data::ControllerControlRequest start;
    start.command = Data::ControllerControlCommand::Start;
    QVERIFY(provider.executeControlCommand(start));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState->serviceState,
        Data::ControllerServiceState::Running,
        1000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
        semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 2, 1000);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 2, 1000);

    QSignalSpy applyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
    QSignalSpy stateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    bool stopAttempted = false;
    bool stopAccepted = false;
    QObject::connect(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished,
        &provider,
        [&provider, &stopAttempted, &stopAccepted](
            const Data::RuntimeOutputTransactionResult &result) {
            if (result.outcome != Data::RuntimeOutputTransactionOutcome::OutcomeUnknown)
                return;
            QTimer::singleShot(0, &provider, [&provider, &stopAttempted, &stopAccepted] {
                Data::ControllerControlRequest stop;
                stop.command = Data::ControllerControlCommand::ControlledStop;
                stopAttempted = true;
                stopAccepted = bool(provider.executeControlCommand(stop));
            });
        });

    controller.holdNextOutputStateResponse();
    controller.holdNextOutputApplyResult();
    QVERIFY(provider.applyRuntimeOutputTransaction(outputApplyRequest(provider)));
    QTRY_VERIFY_WITH_TIMEOUT(stopAttempted, 1000);
    QVERIFY(stopAccepted);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().controllerState->serviceState,
        Data::ControllerServiceState::OperationalSafe,
        1000);
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
    const auto canceledRead = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
        stateFinished.constFirst().constFirst());
    QVERIFY(canceledRead.isValid());
    QVERIFY(canceledRead.error);
    QCOMPARE(canceledRead.error->source, Data::ControllerErrorSource::Network);

    QVERIFY(controller.hasHeldOutputResult());
    controller.releaseHeldOutputResult();
    QTest::qWait(80);
    QCOMPARE(applyFinished.count(), 1);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected);
    QVERIFY(controller.violations().isEmpty());

    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionReconciliationBoundaries()
{
    {
        LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
        QVERIFY(controller.start());
        ProductApiSession::Options options = testOptions();
        options.requestTimeoutMs = 30;
        ProductApiConnectionProvider provider(controller.endpoints(), options);
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
        QSignalSpy attestationFinished(
            &provider,
            &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
        QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
            semanticAttestationRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
        QSignalSpy policyFinished(
            &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
        QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
        Data::ControllerControlRequest acquire;
        acquire.command = Data::ControllerControlCommand::AcquireControl;
        acquire.leaseDurationMs = 30000;
        QVERIFY(provider.executeControlCommand(acquire));
        QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

        QSignalSpy applyFinished(
            &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
        QSignalSpy stateFinished(
            &provider,
            &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
        QSignalSpy stateChanged(
            &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionStateChanged);
        controller.setOutputStateReportsReplayed(true);
        controller.reportNextOutputStateAsReturnedTask();
        controller.holdNextOutputApplyResult();
        const Data::RuntimeOutputTransactionRequest request = outputApplyRequest(provider);
        QVERIFY(provider.applyRuntimeOutputTransaction(request));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 2, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
        QCOMPARE(stateChanged.count(), 1);
        const auto unknown = qvariant_cast<Data::RuntimeOutputTransactionResult>(
            applyFinished.constFirst().constFirst());
        QVERIFY(unknown.isValid());
        QCOMPARE(unknown.outcome, Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);
        const auto returned = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
            stateFinished.constFirst().constFirst());
        QVERIFY(returned.isValid());
        QCOMPARE(returned.state->state, Data::RuntimeOutputState::Idle);
        QVERIFY(returned.state->resultFlags.testFlag(
            Data::RuntimeOutputTransactionResultFlag::ReturnedTask));
        QVERIFY(returned.state->resultFlags.testFlag(
            Data::RuntimeOutputTransactionResultFlag::Replayed));
        QCOMPARE(returned.state->providerDetail, quint64(105));
        const auto recovered = qvariant_cast<Data::RuntimeOutputTransactionResult>(
            applyFinished.constLast().constFirst());
        QVERIFY(recovered.isValid());
        QCOMPARE(recovered.outcome, Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered);
        QVERIFY(!recovered.finalResponseObserved);
        QCOMPARE(recovered.state, returned.state);
        QCOMPARE(stateChanged.count(), 1);
        controller.releaseHeldOutputResult();
        QTest::qWait(60);
        QCOMPARE(applyFinished.count(), 2);

        controller.setOutputRecoveryPolicy(Protocol::OutputRecoveryPolicy::HoldSafe);
        QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 2, 1000);
        controller.setOutputStateReportsReplayed(false);
        controller.reportNextOutputStateAsSafeHold();
        controller.holdNextOutputApplyResult();
        Data::RuntimeOutputTransactionRequest safeHoldRequest = outputApplyRequest(
            provider,
            QByteArray::fromHex("10112233445566778899aabbccddeeff"),
            Data::RuntimeOutputRecoveryPolicy::HoldSafe);
        safeHoldRequest.expectedOutputGeneration = 3;
        QVERIFY(safeHoldRequest.isValid());
        QVERIFY(provider.applyRuntimeOutputTransaction(safeHoldRequest));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 4, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 2, 1000);
        const auto safeRecovered = qvariant_cast<Data::RuntimeOutputTransactionResult>(
            applyFinished.constLast().constFirst());
        QVERIFY(safeRecovered.isValid());
        QCOMPARE(safeRecovered.outcome, Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered);
        QVERIFY(!safeRecovered.finalResponseObserved);
        QCOMPARE(safeRecovered.state->state, Data::RuntimeOutputState::SafeHold);
        QCOMPARE(safeRecovered.state->outputGeneration, quint64(5));
        controller.releaseHeldOutputResult();
        QTest::qWait(60);
        QCOMPARE(applyFinished.count(), 4);
        QVERIFY2(
            controller.violations().isEmpty(),
            qPrintable(controller.violations().join(QStringLiteral("; "))));
        Data::ControllerControlRequest release;
        release.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(release));
        QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }

    {
        LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
        QVERIFY(controller.start());
        ProductApiSession::Options options = testOptions();
        options.requestTimeoutMs = 30;
        ProductApiConnectionProvider provider(controller.endpoints(), options);
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
        QSignalSpy attestationFinished(
            &provider,
            &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
        QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
            semanticAttestationRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
        QSignalSpy policyFinished(
            &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
        QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
        Data::ControllerControlRequest acquire;
        acquire.command = Data::ControllerControlCommand::AcquireControl;
        acquire.leaseDurationMs = 30000;
        QVERIFY(provider.executeControlCommand(acquire));
        QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

        QSignalSpy applyFinished(
            &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
        QSignalSpy stateFinished(
            &provider,
            &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
        controller.setOutputStateReportsReplayed(true);
        controller.setNextOutputRecoveryGenerationAdvance(0);
        controller.reportNextOutputStateAsReturnedTask();
        controller.holdNextOutputApplyResult();
        const Data::RuntimeOutputTransactionRequest request = outputApplyRequest(provider);
        QVERIFY(provider.applyRuntimeOutputTransaction(request));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
        const auto wrongGeneration = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
            stateFinished.constFirst().constFirst());
        QVERIFY(wrongGeneration.isValid());
        QCOMPARE(wrongGeneration.state->state, Data::RuntimeOutputState::Idle);
        QCOMPARE(wrongGeneration.state->outputGeneration, quint64(2));
        QVERIFY(wrongGeneration.state->resultFlags.testFlag(
            Data::RuntimeOutputTransactionResultFlag::Replayed));
        QTest::qWait(60);
        QCOMPARE(applyFinished.count(), 1);
        QCOMPARE(
            qvariant_cast<Data::RuntimeOutputTransactionResult>(
                applyFinished.constFirst().constFirst())
                .outcome,
            Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);

        const Data::RuntimeOutputTransactionRequest differentOperation
            = outputApplyRequest(provider, QByteArray::fromHex("20112233445566778899aabbccddeeff"));
        QVERIFY(!provider.applyRuntimeOutputTransaction(differentOperation));
        QVERIFY(provider.applyRuntimeOutputTransaction(request));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 2, 1000);
        const auto directReplay = qvariant_cast<Data::RuntimeOutputTransactionResult>(
            applyFinished.constLast().constFirst());
        QCOMPARE(directReplay.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
        QVERIFY(directReplay.finalResponseObserved);
        controller.releaseHeldOutputResult();
        QTest::qWait(60);
        QCOMPARE(applyFinished.count(), 2);

        controller.setOutputStateReportsReplayed(false);
        controller.rejectNextOutputStateTyped(-41);
        controller.holdNextOutputApplyResult();
        Data::RuntimeOutputTransactionRequest typedFailureRequest
            = outputApplyRequest(provider, QByteArray::fromHex("30112233445566778899aabbccddeeff"));
        typedFailureRequest.expectedOutputGeneration = 2;
        QVERIFY(typedFailureRequest.isValid());
        QVERIFY(provider.applyRuntimeOutputTransaction(typedFailureRequest));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 3, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 2, 1000);
        const auto stateFailure = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
            stateFinished.constLast().constFirst());
        QVERIFY(stateFailure.isValid());
        QCOMPARE(stateFailure.error->code, std::optional<qint32>(-41));
        QCOMPARE(
            qvariant_cast<Data::RuntimeOutputTransactionResult>(
                applyFinished.constLast().constFirst())
                .outcome,
            Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);

        Data::ControllerControlRequest configurationMode;
        configurationMode.command = Data::ControllerControlCommand::EnterConfigurationMode;
        const int configurationRequests = controller.requestCount(
            Protocol::MessageType::EnterConfigurationMode);
        QVERIFY(!provider.executeControlCommand(configurationMode));
        QCOMPARE(
            controller.requestCount(Protocol::MessageType::EnterConfigurationMode),
            configurationRequests);

        controller.rejectNextSemanticAttestationTyped(-6);
        const Data::RuntimeSemanticMappingAttestationRequest attestationRequest
            = semanticAttestationRequest(provider);
        QVERIFY(provider.requestRuntimeSemanticMappingAttestation(attestationRequest));
        QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 2, 1000);
        QVERIFY(!provider.runtimeSemanticMappingAttestation());
        QVERIFY(provider.requestRuntimeSemanticMappingAttestation(attestationRequest));
        QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 3, 1000);
        QVERIFY(provider.runtimeSemanticMappingAttestation());
        QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 2, 1000);

        const Data::RuntimeOutputTransactionRequest nextOperation
            = outputApplyRequest(provider, QByteArray::fromHex("40112233445566778899aabbccddeeff"));
        QVERIFY(!provider.applyRuntimeOutputTransaction(nextOperation));
        QVERIFY(provider.applyRuntimeOutputTransaction(typedFailureRequest));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 4, 1000);
        const auto typedReplay = qvariant_cast<Data::RuntimeOutputTransactionResult>(
            applyFinished.constLast().constFirst());
        QCOMPARE(typedReplay.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
        QVERIFY(typedReplay.finalResponseObserved);
        controller.releaseHeldOutputResult();
        QTest::qWait(60);
        QCOMPARE(applyFinished.count(), 4);
        QVERIFY2(
            controller.violations().isEmpty(),
            qPrintable(controller.violations().join(QStringLiteral("; "))));
        Data::ControllerControlRequest release;
        release.command = Data::ControllerControlCommand::ReleaseControl;
        QVERIFY(provider.executeControlCommand(release));
        QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
        QVERIFY(provider.disconnectFromController());
        QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    }
}

void EtherCATProductApiTests::testOutputTransactionUnknownSurvivesReconnect()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 30;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    const Data::ControllerConnectionRequest connectionRequest = requestFor(provider);
    QVERIFY(provider.connectToController(connectionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);

    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    QSignalSpy stateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    QSignalSpy applyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);

    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
        semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

    controller.holdNextOutputStateResponse();
    controller.holdNextOutputApplyResult();
    const Data::RuntimeOutputTransactionRequest original = outputApplyRequest(provider);
    QVERIFY(provider.applyRuntimeOutputTransaction(original));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
    QCOMPARE(
        qvariant_cast<Data::RuntimeOutputTransactionResult>(
            applyFinished.constFirst().constFirst())
            .outcome,
        Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);

    const quint64 originalSessionGeneration
        = provider.connectionSnapshot().sessionGeneration;
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    Data::ControllerConnectionRequest reboundConnectionRequest = connectionRequest;
    reboundConnectionRequest.scope = {
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    QVERIFY(reboundConnectionRequest.scope != connectionRequest.scope);
    QVERIFY(provider.connectToController(reboundConnectionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.connectionSnapshot().sessionGeneration > originalSessionGeneration);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
        semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 2, 1000);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 2, 1000);
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

    Data::RuntimeOutputTransactionRequest newOperation = outputApplyRequest(
        provider, QByteArray::fromHex("55112233445566778899aabbccddeeff"));
    newOperation.expectedOutputGeneration = 2;
    QVERIFY(newOperation.isValid());
    const int applyRequestsBeforeReconciliation = controller.requestCount(
        Protocol::MessageType::ApplyOutputTransaction);
    QVERIFY(!provider.applyRuntimeOutputTransaction(newOperation));
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::ApplyOutputTransaction),
        applyRequestsBeforeReconciliation);

    QVERIFY(provider.requestRuntimeOutputTransactionState(outputStateRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 2, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 2, 1000);
    const auto reconciled = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(reconciled.isValid());
    QCOMPARE(reconciled.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
    QVERIFY(!reconciled.finalResponseObserved);
    QCOMPARE(
        reconciled.request.sessionGeneration,
        provider.connectionSnapshot().sessionGeneration);
    QCOMPARE(reconciled.request.scope, reboundConnectionRequest.scope);

    QVERIFY(provider.applyRuntimeOutputTransaction(newOperation));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 3, 1000);
    const auto applied = qvariant_cast<Data::RuntimeOutputTransactionResult>(
        applyFinished.constLast().constFirst());
    QVERIFY(applied.isValid());
    QCOMPARE(applied.outcome, Data::RuntimeOutputTransactionOutcome::Applied);
    QVERIFY(applied.finalResponseObserved);
    QVERIFY(controller.violations().isEmpty());

    Data::ControllerControlRequest release;
    release.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(provider.executeControlCommand(release));
    QTRY_VERIFY_WITH_TIMEOUT(!provider.connectionSnapshot().session->ownsControlLease, 1000);
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

void EtherCATProductApiTests::testOutputTransactionGenerationRegression()
{
    for (const bool policyRegression : {false, true}) {
        LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
        QVERIFY(controller.start());
        ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
        QVERIFY(provider.connectToController(requestFor(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Connected,
            2000);
        QVERIFY(provider.refreshRuntimeResources());
        QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
        QSignalSpy attestationFinished(
            &provider,
            &Core::ControllerConnectionProvider::
                runtimeSemanticMappingAttestationRequestFinished);
        QSignalSpy policyFinished(
            &provider,
            &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
        QSignalSpy stateFinished(
            &provider,
            &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
        QSignalSpy applyFinished(
            &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
        QVERIFY(provider.requestRuntimeSemanticMappingAttestation(
            semanticAttestationRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
        QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);

        Data::ControllerControlRequest acquire;
        acquire.command = Data::ControllerControlCommand::AcquireControl;
        acquire.leaseDurationMs = 30000;
        QVERIFY(provider.executeControlCommand(acquire));
        QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);
        QVERIFY(provider.applyRuntimeOutputTransaction(outputApplyRequest(provider)));
        QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
        controller.setOutputGeneration(1);

        if (policyRegression) {
            QVERIFY(provider.requestRuntimeOutputGroupPolicy(outputPolicyRequest(provider)));
            QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 2, 1000);
            const auto failure = qvariant_cast<Data::RuntimeOutputGroupPolicyResult>(
                policyFinished.constLast().constFirst());
            QVERIFY(failure.isValid());
            QCOMPARE(failure.error->source, Data::ControllerErrorSource::Protocol);
        } else {
            QVERIFY(provider.requestRuntimeOutputTransactionState(
                outputStateRequest(provider)));
            QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
            const auto failure = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
                stateFinished.constLast().constFirst());
            QVERIFY(failure.isValid());
            QCOMPARE(failure.error->source, Data::ControllerErrorSource::Protocol);
        }
        QTRY_COMPARE_WITH_TIMEOUT(
            provider.connectionSnapshot().state,
            Data::ControllerConnectionState::Disconnected,
            1000);
        QVERIFY(controller.violations().isEmpty());
    }
}

void EtherCATProductApiTests::testOutputTransactionReconnectInvalidation()
{
    LoopbackController controller(LoopbackController::Behavior::OutputTransactions);
    QVERIFY(controller.start());
    ProductApiSession::Options options = testOptions();
    options.requestTimeoutMs = 30;
    ProductApiConnectionProvider provider(controller.endpoints(), options);
    const Data::ControllerConnectionRequest connectionRequest = requestFor(provider);
    QVERIFY(provider.connectToController(connectionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(provider.runtimeResourceSnapshot().has_value(), 2000);
    QSignalSpy attestationFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    QVERIFY(provider.requestRuntimeSemanticMappingAttestation(semanticAttestationRequest(provider)));
    QTRY_COMPARE_WITH_TIMEOUT(attestationFinished.count(), 1, 1000);
    QSignalSpy policyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    const Data::RuntimeOutputGroupPolicyRequest oldPolicy = outputPolicyRequest(provider);
    QVERIFY(provider.requestRuntimeOutputGroupPolicy(oldPolicy));
    QTRY_COMPARE_WITH_TIMEOUT(policyFinished.count(), 1, 1000);
    Data::ControllerControlRequest acquire;
    acquire.command = Data::ControllerControlCommand::AcquireControl;
    acquire.leaseDurationMs = 30000;
    QVERIFY(provider.executeControlCommand(acquire));
    QTRY_VERIFY_WITH_TIMEOUT(provider.connectionSnapshot().session->ownsControlLease, 1000);

    QSignalSpy applyFinished(
        &provider, &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished);
    QSignalSpy stateFinished(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    controller.holdNextOutputStateResponse();
    controller.holdNextOutputApplyResult();
    const Data::RuntimeOutputTransactionRequest oldTransaction = outputApplyRequest(provider);
    QVERIFY(provider.applyRuntimeOutputTransaction(oldTransaction));
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(stateFinished.count(), 1, 1000);
    QCOMPARE(
        qvariant_cast<Data::RuntimeOutputTransactionResult>(applyFinished.constFirst().constFirst())
            .outcome,
        Data::RuntimeOutputTransactionOutcome::OutcomeUnknown);
    const auto stateTimeout = qvariant_cast<Data::RuntimeOutputTransactionStateResult>(
        stateFinished.constFirst().constFirst());
    QVERIFY(stateTimeout.isValid());
    QVERIFY(stateTimeout.error);

    controller.setRuntimeEpoch(7, 10, 11, 0x4142434445464748);
    QVERIFY(provider.refreshRuntimeResources());
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.runtimeResourceCatalog()
            && provider.runtimeResourceCatalog()->epoch.runtimeGeneration == 10,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(!provider.runtimeSemanticMappingAttestation(), 1000);
    const int applyBeforeEpochGuard = controller.requestCount(
        Protocol::MessageType::ApplyOutputTransaction);
    QVERIFY(!provider.applyRuntimeOutputTransaction(oldTransaction));
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::ApplyOutputTransaction),
        applyBeforeEpochGuard);
    const quint64 oldGeneration = provider.connectionSnapshot().sessionGeneration;

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
    QVERIFY(provider.connectToController(connectionRequest));
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.connectionSnapshot().state, Data::ControllerConnectionState::Connected, 2000);
    QVERIFY(provider.connectionSnapshot().sessionGeneration > oldGeneration);
    const int before = controller.requestCount(Protocol::MessageType::QueryOutputGroupPolicy);
    QVERIFY(!provider.requestRuntimeOutputGroupPolicy(oldPolicy));
    QCOMPARE(controller.requestCount(Protocol::MessageType::QueryOutputGroupPolicy), before);
    QVERIFY(!provider.applyRuntimeOutputTransaction(oldTransaction));
    QCOMPARE(
        controller.requestCount(Protocol::MessageType::ApplyOutputTransaction),
        applyBeforeEpochGuard);
    QVERIFY(controller.violations().isEmpty());
    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

} // namespace EtherCAT::ProductApi::Internal
