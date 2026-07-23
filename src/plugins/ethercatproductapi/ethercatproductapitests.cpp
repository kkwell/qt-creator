// Copyright (C) 2026 Kvell

#include "ethercatproductapitests.h"

#include "productapicodec.h"
#include "productapiconnectionprovider.h"
#include "productapisession.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSet>
#include <QSignalSpy>
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

QByteArray packageStatePayload()
{
    QByteArray payload(72, '\0');
    putU16(payload, 0, quint16(Protocol::MessageType::GetPackageState));
    putI32(payload, 4, 0);
    putI32(payload, 8, 0);
    putU32(payload, 12, 'A');
    putU64(payload, 16, 11);
    putU64(payload, 24, 22);
    putU32(payload, 32, 'B');
    putU32(payload, 36, 3); // ACTIVE
    putU64(payload, 40, 33);
    putU64(payload, 48, 44);
    putI32(payload, 56, 0);
    putU32(payload, 60, 5);
    putU64(payload, 64, TestBootId);
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
        if (!Protocol::isReadOnlyRequest(frame.header.messageType)) {
            m_violations.append(QStringLiteral("A non-read-only request was emitted."));
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
        default:
            m_violations.append(QStringLiteral("An unknown client request was emitted."));
            return;
        }
    }

    void sendStateResponse(Peer &peer, const Protocol::Frame &request)
    {
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
                         controllerStatePayload());
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
                     packageStatePayload());
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
        putU64(payload, 24, 0);
        putU32(payload, 32, 0x7ff);
        putU32(payload, 36, 5000);
        peer.handshaken = true;
        sendResponse(peer, Protocol::MessageType::HelloAck, requestId, payload);
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
        frame.header.protocolMinor = peer.parser.negotiatedMinor().value_or(
            Protocol::CurrentMinor);
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
    bool m_allowCapacitySuccess = false;
    qint32 m_nextResumeStatus = 0;
    Behavior m_behavior = Behavior::Normal;
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

} // namespace

void EtherCATProductApiTests::testCrcAndGoldenFrame()
{
    QCOMPARE(Protocol::crc32c(QByteArrayView("123456789")), quint32(0xe3069283));

    Protocol::Error error;
    const QByteArray wire = Protocol::encodeHello(Protocol::Role::Control,
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
            "45434150000100090040000100000000000000180000000000000000"
            "0102030405060708000000000000000100000000000000000000000000000000"
            "67f685c3000000010000000000000000000000001122334455667788"));
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
        responseFrame(Protocol::MessageType::Capability, descriptor), 0x7ff, &error);
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

void EtherCATProductApiTests::testReadOnlyRequestWhitelist()
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

    const QList<Protocol::MessageType> forbidden{
        static_cast<Protocol::MessageType>(0x0100), // AcquireControl
        static_cast<Protocol::MessageType>(0x0102), // Start
        static_cast<Protocol::MessageType>(0x0300), // BulkBegin
        static_cast<Protocol::MessageType>(0x0500), // BeginFirmwareUpload
    };
    for (const Protocol::MessageType type : forbidden) {
        QVERIFY(!Protocol::isReadOnlyRequest(type));
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
    QCOMPARE(snapshot.protocolVersion.minor, quint16(9));
    QVERIFY(snapshot.readOnly);
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

    QTest::newRow("command-status")
        << int(LoopbackController::Behavior::CommandError)
        << int(Data::ControllerOperation::QueryState)
        << int(Protocol::MessageType::GetState) << -16 << -5 << QString("control") << true
        << qulonglong(CommandStatusDetail);
    QTest::newRow("bulk-status")
        << int(LoopbackController::Behavior::BulkError)
        << int(Data::ControllerOperation::QueryCapability)
        << int(Protocol::MessageType::GetCapability) << -20 << -6 << QString("bulk") << false
        << qulonglong(0);
    QTest::newRow("package-state-error")
        << int(LoopbackController::Behavior::PackageError)
        << int(Data::ControllerOperation::QueryPackageState)
        << int(Protocol::MessageType::GetPackageState) << -17 << -3 << QString("control") << false
        << qulonglong(0);
    QTest::newRow("firmware-state-error")
        << int(LoopbackController::Behavior::FirmwareError)
        << int(Data::ControllerOperation::QueryFirmwareState)
        << int(Protocol::MessageType::GetFirmwareState) << -28 << -4 << QString("control") << true
        << qulonglong(FirmwareStateDetail);
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

    LoopbackController controller(static_cast<LoopbackController::Behavior>(behavior));
    QVERIFY(controller.start());
    ProductApiConnectionProvider provider(controller.endpoints(), testOptions());
    const Data::ControllerConnectionRequest request = requestFor(provider);
    QVERIFY(provider.connectToController(request));

    QTRY_COMPARE_WITH_TIMEOUT(provider.connectionSnapshot().state,
                              Data::ControllerConnectionState::Degraded,
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
