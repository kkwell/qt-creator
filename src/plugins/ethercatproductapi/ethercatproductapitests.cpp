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

QByteArray lifecycleControllerStatePayload(quint32 serviceState)
{
    QByteArray payload(80, '\0');
    quint32 status = 0x1; // READY
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
    }
    putU32(payload, 0, serviceState);
    putU32(payload, 4, status);
    putU64(payload, 32, 101);
    putU64(payload, 40, 202);
    putU64(payload, 48, TestBootId);
    putU32(payload, 56, alState);
    putU32(payload, 60, workingCounter);
    putU32(payload, 64, workingCounter);
    putU32(payload, 68, status & 0x40 ? 73 : 0);
    putU32(payload, 76, 19);
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
    Protocol::MessageType originalType, quint16 stage, quint32 serviceState, bool final)
{
    QByteArray payload(40, '\0');
    putU16(payload, 0, quint16(originalType));
    putU16(payload, 2, stage);
    putU32(payload, 12, serviceState);
    putU32(payload, 16, 1);
    putU64(payload, 20, 5000000000);
    putU32(payload, 36, final ? 1 : 0);
    return payload;
}

QByteArray rejectedCommandStatusPayload(
    Protocol::MessageType originalType,
    quint16 stage,
    quint32 serviceState,
    qint32 status = InternalStatus,
    quint64 detail = 0)
{
    QByteArray payload(40, '\0');
    putU16(payload, 0, quint16(originalType));
    putU16(payload, 2, stage);
    putI32(payload, 4, status);
    putI32(payload, 8, -1);
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

QByteArray packageStateErrorPayload()
{
    QByteArray payload(72, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::GetPackageState));
    putI32(payload, 4, -17); // STALE_PACKAGE
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

class LoopbackController final : public QObject
{
    struct Peer;

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
    };

    explicit LoopbackController(Behavior behavior = Behavior::Normal)
        : m_behavior(behavior)
    {}

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

    void rejectNextControl(Protocol::MessageType type, qint32 status)
    {
        m_rejectedControlType = type;
        m_nextControlStatus = status;
    }

    void holdNextHeartbeat() { m_holdNextHeartbeat = true; }
    void holdNextRelease() { m_holdNextRelease = true; }
    bool hasHeldHeartbeat() const { return m_heldHeartbeatPeer && m_heldHeartbeatRequestId; }
    bool hasHeldRelease() const { return m_heldReleasePeer && m_heldReleaseRequestId; }

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

    bool sendLiveAlarm(quint32 sequence)
    {
        for (auto found = m_peers.rbegin(); found != m_peers.rend(); ++found) {
            Peer *peer = found->get();
            if (peer->role == Protocol::Role::Push && peer->handshaken
                && peer->socket->state() == QAbstractSocket::ConnectedState) {
                QByteArray payload(AlarmEventBytes, '\0');
                putU32(payload, 0, sequence);
                sendResponse(*peer, Protocol::MessageType::AlarmRaised, 0, payload);
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
            || frame.header.bootId != TestBootId || frame.header.controllerTimestampNs) {
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
        case Protocol::MessageType::AcquireControl:
        case Protocol::MessageType::ReleaseControl:
        case Protocol::MessageType::Start:
        case Protocol::MessageType::StartFreeRun:
        case Protocol::MessageType::StartDc:
        case Protocol::MessageType::Pause:
        case Protocol::MessageType::Resume:
        case Protocol::MessageType::ControlledStop:
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
        default:
            sendResponse(peer,
                         Protocol::MessageType::ControllerState,
                         request.header.requestId,
                         m_behavior == Behavior::ControlLifecycle
                             ? lifecycleControllerStatePayload(m_serviceState)
                             : controllerStatePayload());
            return;
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
        sendResponse(peer,
                     Protocol::MessageType::PackageState,
                     request.header.requestId,
                     m_behavior == Behavior::ControlLifecycle
                         ? packageStatePayload(
                               Protocol::MessageType::GetPackageState, m_controllerPackageActive)
                         : packageStatePayload());
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

    void handleControlRequest(Peer &peer, const Protocol::Frame &request)
    {
        if (m_behavior != Behavior::ControlLifecycle) {
            m_violations.append(
                QStringLiteral("A control request was emitted outside the lifecycle test."));
            return;
        }
        requireRoleAndPayload(
            peer,
            Protocol::Role::Control,
            request,
            request.header.messageType == Protocol::MessageType::AcquireControl ? 4
            : request.header.messageType == Protocol::MessageType::DiscoverTopology
                ? 8
            : request.header.messageType == Protocol::MessageType::RestoreActivePackage
                ? 24
                : 0);

        if (m_nextControlStatus && request.header.messageType == m_rejectedControlType) {
            const qint32 status = m_nextControlStatus;
            m_nextControlStatus = 0;
            m_rejectedControlType = Protocol::MessageType::Error;
            const quint16 stage
                = request.header.messageType == Protocol::MessageType::AcquireControl
                          || request.header.messageType == Protocol::MessageType::ReleaseControl
                      ? 4
                      : 1;
            sendResponse(
                peer,
                Protocol::MessageType::CommandStatus,
                request.header.requestId,
                rejectedCommandStatusPayload(
                    request.header.messageType, stage, m_serviceState, status),
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
        putU64(payload, 16, TestBootId);
        putU64(
            payload,
            24,
            m_helloLeaseOwnerSessionId
                ? m_helloLeaseOwnerSessionId
                : (m_leaseOwned ? TestSessionId : 0));
        putU32(payload, 32, m_protocolMinor >= Protocol::ExplicitTimingModeMinor ? 0xfff : 0x7ff);
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
        frame.header.bootId = TestBootId;
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
                                                    : Protocol::flagValue(
                                                          Protocol::Flag::Response));

        QByteArray heartbeatPayload(16, '\0');
        putU64(heartbeatPayload, 0, 77);
        putU64(heartbeatPayload, 8, TestBootId);
        const Protocol::Frame heartbeat = response(
            peer,
            Protocol::MessageType::PushHeartbeat,
            0,
            heartbeatPayload,
            Protocol::Flag::Response | Protocol::Flag::Replaceable);

        QByteArray coalesced = wireFor(progress) + wireFor(resume) + wireFor(heartbeat);
        if (!resumeStatus && !requestedAfterSequence) {
            QByteArray alarmPayload(AlarmEventBytes, '\0');
            putU32(alarmPayload, 0, TestAlarmSequence);
            coalesced += wireFor(response(
                peer, Protocol::MessageType::AlarmRaised, 0, alarmPayload));
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
    bool m_allowCapacitySuccess = false;
    bool m_holdNextHeartbeat = false;
    bool m_holdNextRelease = false;
    qint32 m_nextResumeStatus = 0;
    qint32 m_nextStateStatus = 0;
    qint32 m_nextControlStatus = 0;
    quint32 m_defaultLeaseDurationMs = 5000;
    quint64 m_helloLeaseOwnerSessionId = 0;
    quint16 m_protocolMinor = Protocol::CurrentMinor;
    Protocol::MessageType m_rejectedControlType = Protocol::MessageType::Error;
    Behavior m_behavior = Behavior::Normal;
    quint32 m_serviceState = 8;
    bool m_controllerPackageActive = false;
    bool m_leaseOwned = false;
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
        Protocol::CurrentMinor,
        &error);
    QVERIFY(!error);
    QCOMPARE(
        wire,
        QByteArray::fromHex(
            "454341500001000a0040000100000000000000180000000000000000"
            "0102030405060708000000000000000100000000000000000000000000000000"
            "909715de000000010000000000000000000000001122334455667788"));
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

void EtherCATProductApiTests::testSemanticAuxiliaryRecords()
{
    const QByteArray descriptor("opaque-vendor-capability-v1");
    Protocol::Error error;
    const auto capability = Protocol::decodeCapability(
        responseFrame(Protocol::MessageType::Capability, descriptor), 0xfff, &error);
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
    const QList<QPair<Protocol::MessageType, QByteArray>> controls{
        {Protocol::MessageType::AcquireControl, lease},
        {Protocol::MessageType::ReleaseControl, {}},
        {Protocol::MessageType::Start, {}},
        {Protocol::MessageType::StartFreeRun, {}},
        {Protocol::MessageType::StartDc, {}},
        {Protocol::MessageType::ControlledStop, {}},
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

    const QList<Protocol::MessageType> forbidden{
        static_cast<Protocol::MessageType>(0x0300), // BulkBegin
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
        failedProvider.connectionSnapshot().state, Data::ControllerConnectionState::Failed, 1000);
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
    control.command = Data::ControllerControlCommand::ReleaseControl;
    QVERIFY(!provider.executeControlCommand(control));
    QVERIFY(!provider.disconnectFromController());

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
        && timingMode != QStringLiteral("dc")) {
        QFAIL("QTC_ETHER_CAT_TIMING_MODE must be set to exactly auto, free_run, or dc before "
              "the hardware test can run.");
    }

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

    Data::ControllerControlCommand startCommand = Data::ControllerControlCommand::Start;
    if (timingMode == QStringLiteral("free_run"))
        startCommand = Data::ControllerControlCommand::StartFreeRun;
    else if (timingMode == QStringLiteral("dc"))
        startCommand = Data::ControllerControlCommand::StartDistributedClocks;
    if (failure.isEmpty()) {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        const bool explicitTimingMode = timingMode != QStringLiteral("auto");
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
        } else if (!provider.supportsControlCommand(startCommand)) {
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
        } else if (
            !snapshot.package || snapshot.package->activeSlot == Data::ControllerSlot::None
            || !snapshot.package->activeGeneration || !snapshot.package->activeConfigurationId) {
            recordFailure(QStringLiteral(
                "No exact persistent active-package selector was returned by preflight."));
        } else {
            using ServiceState = Data::ControllerServiceState;
            const ServiceState state = snapshot.controllerState->serviceState;
            const bool canEnterConfiguration = state == ServiceState::OperationalSafe
                                               || state == ServiceState::Running
                                               || state == ServiceState::Paused
                                               || state == ServiceState::Fault
                                               || state == ServiceState::Shutdown;
            if (!canEnterConfiguration) {
                recordFailure(
                    QStringLiteral("The initial controller state %1 cannot enter configuration.")
                        .arg(hardwareServiceStateName(state)));
            } else {
                initialBootId = snapshot.session->bootId;
                initialActiveSlot = snapshot.package->activeSlot;
                initialActiveGeneration = snapshot.package->activeGeneration;
                initialActiveConfigurationId = snapshot.package->activeConfigurationId;
                qInfo().noquote() << "[Product API hardware] saved initial selector="
                                  << QStringLiteral("%1:%2:%3")
                                         .arg(hardwareSlotName(initialActiveSlot))
                                         .arg(initialActiveGeneration)
                                         .arg(initialActiveConfigurationId)
                                  << "boot=" << QString::number(initialBootId);
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

    Data::ControllerControlRequest control;
    if (failure.isEmpty()) {
        control.command = Data::ControllerControlCommand::AcquireControl;
        control.leaseDurationMs = 30000;
        executeMainCommand(control, QStringLiteral("acquire 30000 ms lease"));
        waitForMainGate(QStringLiteral("lease ownership"), ownsLease);
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::EnterConfigurationMode;
        executeMainCommand(control, QStringLiteral("enter configuration"));
        waitForMainGate(QStringLiteral("initial SHUTDOWN"), strictShutdownGate);
    }

    if (failure.isEmpty()) {
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
                    << QStringLiteral("0x%1").arg(slave.revision, 8, 16, QLatin1Char('0'));
            }
        }
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::RestoreActivePackage;
        executeMainCommand(control, QStringLiteral("restore exact active package"));
        waitForMainGate(QStringLiteral("restored OP_SAFE"), strictOperationalSafeGate);
    }

    if (failure.isEmpty()) {
        runtimeStartAttempted = true;
        control = {};
        control.command = startCommand;
        QString startLabel = QStringLiteral("automatic package-mode start");
        if (timingMode == QStringLiteral("free_run"))
            startLabel = QStringLiteral("explicit FreeRun start");
        else if (timingMode == QStringLiteral("dc"))
            startLabel = QStringLiteral("explicit DC start");
        executeMainCommand(control, startLabel);
        waitForMainGate(QStringLiteral("RUNNING"), strictRunningGate);
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::Pause;
        executeMainCommand(control, QStringLiteral("pause"));
        waitForMainGate(QStringLiteral("PAUSED"), strictPausedGate);
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::Resume;
        executeMainCommand(control, QStringLiteral("resume"));
        waitForMainGate(QStringLiteral("resumed RUNNING"), strictRunningGate);
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::ControlledStop;
        executeMainCommand(control, QStringLiteral("controlled stop"));
        waitForMainGate(QStringLiteral("stopped OP_SAFE"), strictOperationalSafeGate);
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::EnterConfigurationMode;
        executeMainCommand(control, QStringLiteral("final enter configuration"));
        waitForMainGate(QStringLiteral("final SHUTDOWN"), strictShutdownGate);
    }

    if (failure.isEmpty()) {
        control = {};
        control.command = Data::ControllerControlCommand::ReleaseControl;
        executeMainCommand(control, QStringLiteral("release control"));
        waitForMainGate(QStringLiteral("lease released"), [&provider, &sameSessionEpoch] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            return sameSessionEpoch() && snapshot.session && !snapshot.session->ownsControlLease;
        });
    }

    if (failure.isEmpty()) {
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
                << "[Product API hardware] lifecycle completed in SHUTDOWN and disconnected";
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
        if (cleanupOwnsLease && cleanupSnapshot.controllerState
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
        if (cleanupOwnsLease && cleanupSnapshot.controllerState
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
        const bool canRelease = cleanupOwnsLease && cleanupSnapshot.controllerState
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
                "Cleanup retained the lease because SHUTDOWN or OP_SAFE was not confirmed."));
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

void EtherCATProductApiTests::testShutdownReleaseSafety_data()
{
    QTest::addColumn<int>("targetState");

    QTest::newRow("shutdown") << int(Data::ControllerServiceState::Shutdown);
    QTest::newRow("running") << int(Data::ControllerServiceState::Running);
    QTest::newRow("paused") << int(Data::ControllerServiceState::Paused);
    QTest::newRow("pending-release") << -1;
}

void EtherCATProductApiTests::testShutdownReleaseSafety()
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

    provider.shutdown();
    QTest::qWait(100);
    const int expectedReleaseCount
        = targetState == int(Data::ControllerServiceState::Shutdown) || targetState == -1 ? 1 : 0;
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), expectedReleaseCount);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.sessionForTests()->isIdleForTests());
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

    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    QCOMPARE(snapshot.state, Data::ControllerConnectionState::Connected);
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

void EtherCATProductApiTests::testRejectedControlRefreshAndReleaseGates()
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
    QVERIFY(!provider.executeControlCommand(control));
    QVERIFY(!provider.disconnectFromController());
    QCOMPARE(controller.requestCount(Protocol::MessageType::ReleaseControl), 0);
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
        << qulonglong(CommandStatusDetail) << int(Data::ControllerConnectionState::Failed);
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
                              Data::ControllerConnectionState::Failed,
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
                              Data::ControllerConnectionState::Failed,
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
                              Data::ControllerConnectionState::Failed,
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

    const quint64 initialGeneration = provider.connectionSnapshot().sessionGeneration;
    controller.dropChannel(Protocol::Role::Push);
    QTRY_VERIFY_WITH_TIMEOUT(
        provider.connectionSnapshot().sessionGeneration > initialGeneration, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QCOMPARE(provider.connectionSnapshot().session->sessionId, TestSessionId);
    QCOMPARE(provider.connectionSnapshot().session->bootId, TestBootId);
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

    const qsizetype requestsBeforeRecovery = controller.resumeAfterSequences().size();
    QVERIFY(provider.refreshController());
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.resumeAfterSequences().size() > requestsBeforeRecovery, 1000);
    QCOMPARE(controller.resumeAfterSequences().constLast(), quint32(0));
    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Connected,
                              2000);
    QVERIFY(controller.violations().isEmpty());

    QVERIFY(provider.disconnectFromController());
    QTRY_VERIFY_WITH_TIMEOUT(provider.sessionForTests()->isIdleForTests(), 1000);
}

} // namespace EtherCAT::ProductApi::Internal
