// Copyright (C) 2026 Kvell

#include "productapicodec.h"

#include "ethercatproductapitr.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <limits>

namespace EtherCAT::ProductApi::Internal::Protocol {

namespace {

constexpr quint32 KnownFlags = 0x1f;
constexpr quint32 BaselineFeatures = 0x0000000f;
constexpr quint32 ControlLeaseFeature = 1U << 0;
constexpr quint32 ResumablePushFeature = 1U << 1;
constexpr quint32 TransactionalBulkFeature = 1U << 2;
constexpr quint32 CapabilityQueryFeature = 1U << 3;
constexpr quint32 SdoFailureDiagnosticFeature = 1U << 4;
constexpr quint32 ExactAlarmReplayFeature = 1U << 5;
constexpr quint32 LinkDiagnosticsFeature = 1U << 6;
constexpr quint32 TimeCorrelationFeature = 1U << 7;
constexpr quint32 ProcessInputSampleFeature = 1U << 8;
constexpr quint32 StructuredHelloErrorFeature = 1U << 9;
constexpr quint32 FirmwareUpdateFeature = 1U << 10;
constexpr quint32 ExplicitTimingModeStartFeature = 1U << 11;
constexpr quint32 ControllerStatusKnownMask = 0x000001ff;
constexpr quint64 ControllerFaultKnownMask = Data::ControllerFaultKnownMask;
static_assert(ControllerFaultKnownMask == 0x000000000001ffff);
constexpr quint32 ControllerAlKnownMask = 0x1f;
constexpr quint32 FirmwareFlagKnownMask = 0xff;
constexpr quint32 FirmwareChunkMaximumBytes = 65488;
constexpr quint32 AlarmRingCapacity = 32;
constexpr quint32 MaximumLeaseDurationMs = 30000;
constexpr qint32 StatusOk = 0;
constexpr qint32 StatusInternal = -16;
constexpr qint32 StatusEventGap = -25;
constexpr qint32 StatusSessionCapacity = -26;

template<typename T>
T readBigEndian(QByteArrayView bytes, qsizetype offset)
{
    return qFromBigEndian<T>(reinterpret_cast<const uchar *>(bytes.data() + offset));
}

template<typename T>
void writeBigEndian(QByteArray &bytes, qsizetype offset, T value)
{
    qToBigEndian<T>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

template<typename T>
void appendBigEndian(QByteArray &bytes, T value)
{
    const qsizetype oldSize = bytes.size();
    bytes.resize(oldSize + qsizetype(sizeof(T)));
    writeBigEndian(bytes, oldSize, value);
}

void clearError(Error *error)
{
    if (error)
        *error = {};
}

void setError(
    Error *error,
    ErrorCategory category,
    const QString &text,
    std::optional<qint32> status = {},
    std::optional<qint32> operationResult = {},
    std::optional<quint64> sourceDetail = {})
{
    if (!error)
        return;
    error->category = category;
    error->text = text;
    error->status = status;
    error->operationResult = operationResult;
    error->sourceDetail = sourceDetail;
}

bool validRole(Role role)
{
    return role == Role::Control || role == Role::Push || role == Role::Bulk;
}

bool validPackageSelector(QByteArrayView payload)
{
    if (payload.size() != 24)
        return false;
    const quint32 slot = readBigEndian<quint32>(payload, 0);
    return (slot == quint32('A') || slot == quint32('B'))
           && readBigEndian<quint32>(payload, 4) == 0
           && readBigEndian<quint64>(payload, 8)
           && readBigEndian<quint64>(payload, 16);
}

bool messageAllowedForRole(Role role, FrameDirection direction, MessageType type)
{
    if (direction == FrameDirection::ClientRequest) {
        if (type == MessageType::Hello)
            return true;
        if (role == Role::Control) {
            return type == MessageType::AcquireControl
                   || type == MessageType::ReleaseControl || type == MessageType::Start
                   || type == MessageType::StartFreeRun || type == MessageType::StartDc
                   || type == MessageType::Pause || type == MessageType::Resume
                   || type == MessageType::ControlledStop || type == MessageType::GetState
                   || type == MessageType::Heartbeat
                   || type == MessageType::EnterConfigurationMode
                   || type == MessageType::GetCapability
                   || type == MessageType::DiscoverTopology
                   || type == MessageType::GetPackageState
                   || type == MessageType::ValidatePackage
                   || type == MessageType::ActivatePackage
                   || type == MessageType::RollbackPackage
                   || type == MessageType::RestoreActivePackage
                   || type == MessageType::GetFirmwareState;
        }
        if (role == Role::Push)
            return type == MessageType::ResumeEvents;
        return role == Role::Bulk
               && (type == MessageType::GetCapability || type == MessageType::BulkBegin
                   || type == MessageType::BulkChunk || type == MessageType::BulkCommit
                   || type == MessageType::BulkAbort);
    }

    if (type == MessageType::HelloAck || type == MessageType::Error)
        return true;
    if (role == Role::Control) {
        return type == MessageType::CommandStatus || type == MessageType::ControllerState
               || type == MessageType::Capability || type == MessageType::TopologyResult
               || type == MessageType::PackageState || type == MessageType::FirmwareState;
    }
    if (role == Role::Push) {
        return type == MessageType::CommandStatus || type == MessageType::ControllerState
               || type == MessageType::AlarmRaised || type == MessageType::AlarmCleared
               || type == MessageType::PerformanceSnapshot || type == MessageType::PushHeartbeat
               || type == MessageType::FirmwareProgress || type == MessageType::ResumeEventsResult;
    }
    return role == Role::Bulk
           && (type == MessageType::BulkStatus || type == MessageType::Capability
               || type == MessageType::FirmwareStatus);
}

quint32 requiredFeatureMask(quint16 minor)
{
    quint32 mask = BaselineFeatures;
    if (minor >= 3)
        mask |= SdoFailureDiagnosticFeature;
    if (minor >= 4)
        mask |= ExactAlarmReplayFeature;
    if (minor >= 5)
        mask |= LinkDiagnosticsFeature;
    if (minor >= 6)
        mask |= TimeCorrelationFeature;
    if (minor >= 7)
        mask |= ProcessInputSampleFeature;
    if (minor >= 8)
        mask |= StructuredHelloErrorFeature;
    if (minor >= 9)
        mask |= FirmwareUpdateFeature;
    if (minor >= ExplicitTimingModeMinor)
        mask |= ExplicitTimingModeStartFeature;
    return mask;
}

quint32 frameCrc32c(QByteArrayView wire)
{
    quint32 crc = 0xffffffffU;
    for (qsizetype index = 0; index < wire.size(); ++index) {
        const quint8 byte = index >= 60 && index < 64 ? 0 : quint8(wire.at(index));
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0x82f63b78U : 0U);
    }
    return ~crc;
}

bool validateResponseRecord(
    const Frame &frame,
    MessageType expectedType,
    qsizetype expectedBytes,
    quint32 allowedFlags,
    Error *error)
{
    if (frame.header.messageType != expectedType) {
        setError(error, ErrorCategory::InvalidEnvelope, QStringLiteral("Unexpected message type."));
        return false;
    }
    if (frame.payload.size() != expectedBytes) {
        setError(error, ErrorCategory::InvalidPayload, QStringLiteral("Unexpected payload length."));
        return false;
    }
    if (frame.header.flags != allowedFlags) {
        setError(error, ErrorCategory::InvalidFlags, QStringLiteral("Unexpected response flags."));
        return false;
    }
    return true;
}

std::optional<Data::ControllerSlot> decodeSlot(quint32 value)
{
    if (value == 0)
        return Data::ControllerSlot::None;
    if (value == quint32('A'))
        return Data::ControllerSlot::A;
    if (value == quint32('B'))
        return Data::ControllerSlot::B;
    return {};
}

std::optional<Data::ControllerSlot> decodeByteSlot(quint8 value)
{
    return decodeSlot(value);
}

std::optional<Data::ControllerServiceState> decodeServiceState(quint32 value)
{
    using State = Data::ControllerServiceState;
    switch (value) {
    case 0:
        return State::Boot;
    case 1:
        return State::Configuring;
    case 2:
        return State::SafeOperational;
    case 3:
        return State::OperationalSafe;
    case 4:
        return State::Running;
    case 5:
        return State::Stopping;
    case 6:
        return State::Fault;
    case 7:
        return State::Recovering;
    case 8:
        return State::Shutdown;
    case 9:
        return State::Paused;
    }
    return {};
}

std::optional<Data::ControllerSeverity> decodeSeverity(quint32 value)
{
    using Severity = Data::ControllerSeverity;
    switch (value) {
    case 0:
        return Severity::None;
    case 1:
        return Severity::Information;
    case 2:
        return Severity::Warning;
    case 3:
        return Severity::Error;
    case 4:
        return Severity::Fatal;
    }
    return {};
}

std::optional<Data::ControllerPackageState> decodeCpuPackageState(quint32 value)
{
    using State = Data::ControllerPackageState;
    switch (value) {
    case 0:
        return State::Empty;
    case 1:
        return State::Staged;
    case 2:
        return State::Accepted;
    case 3:
        return State::Active;
    case 4:
        return State::Rejected;
    }
    return {};
}

std::optional<Data::ControllerFirmwareState> decodeFirmwareLifecycle(quint16 value)
{
    using State = Data::ControllerFirmwareState;
    switch (value) {
    case 0:
        return State::Idle;
    case 1:
        return State::Receiving;
    case 2:
        return State::Staged;
    case 3:
        return State::Verified;
    case 4:
        return State::Installing;
    case 5:
        return State::ReadyToReboot;
    case 6:
        return State::TrialBoot;
    case 7:
        return State::Confirmed;
    case 8:
        return State::Rejected;
    case 9:
        return State::RolledBack;
    }
    return {};
}

bool packageStatusAllowed(qint32 status)
{
    switch (status) {
    case 0:
    case -6:
    case -7:
    case -8:
    case -12:
    case -16:
    case -17:
    case -21:
    case -22:
    case -23:
    case -24:
        return true;
    default:
        return false;
    }
}

bool firmwareStateStatusAllowed(qint32 status)
{
    switch (status) {
    case 0:
    case -6:
    case -7:
    case -8:
    case -12:
    case -14:
    case -16:
    case -28:
    case -29:
    case -30:
    case -31:
    case -34:
        return true;
    default:
        return false;
    }
}

bool commandStatusAllowed(qint32 status)
{
    switch (status) {
    case 0:
    case -6:
    case -7:
    case -8:
    case -9:
    case -10:
    case -11:
    case -12:
    case -14:
    case -15:
    case -16:
    case -17:
    case -19:
    case -20:
    case -21:
    case -22:
    case -23:
    case -24:
    case -35:
        return true;
    default:
        return false;
    }
}

bool bulkStatusAllowed(qint32 status)
{
    switch (status) {
    case 0:
    case -4:
    case -6:
    case -7:
    case -8:
    case -9:
    case -11:
    case -12:
    case -15:
    case -16:
    case -20:
    case -21:
    case -22:
    case -23:
    case -24:
        return true;
    default:
        return false;
    }
}

bool firmwareStatusAllowed(qint32 status)
{
    switch (status) {
    case 0:
    case -6:
    case -7:
    case -8:
    case -9:
    case -11:
    case -12:
    case -14:
    case -16:
    case -27:
    case -28:
    case -29:
    case -30:
    case -31:
    case -32:
    case -33:
    case -34:
        return true;
    default:
        return false;
    }
}

bool commandStatusOriginalAllowed(quint16 originalType)
{
    return (originalType >= 0x0100 && originalType <= 0x010d) || originalType == 0x0210
           || (originalType >= 0x0400 && originalType <= 0x040a);
}

bool bulkStatusOriginalAllowed(quint16 originalType)
{
    return (originalType >= 0x0300 && originalType <= 0x0303) || originalType == 0x0400;
}

bool firmwareStatusOriginalAllowed(quint16 originalType)
{
    return (originalType >= 0x0500 && originalType <= 0x0503)
           || (originalType >= 0x0505 && originalType <= 0x0507);
}

bool validateStatusFlags(const Frame &frame, qint32 status, Error *error)
{
    const quint32 expectedFlags = flagValue(Flag::Response) | (status ? flagValue(Flag::Error) : 0);
    if (frame.header.flags == expectedFlags)
        return true;
    setError(
        error,
        ErrorCategory::InvalidFlags,
        QStringLiteral("Status flags disagree with the Product API status."));
    return false;
}

quint32 alarmSequenceDistance(quint32 after, quint32 latest)
{
    return quint32((quint64(latest) + 0xffffffffULL - after) % 0xffffffffULL);
}

} // namespace

FrameParser::FrameParser(Role role, FrameDirection direction)
    : m_role(role)
    , m_direction(direction)
{}

ParseResult FrameParser::append(QByteArrayView bytes)
{
    ParseResult result;
    if (m_error) {
        result.error = m_error;
        return result;
    }
    if (!bytes.isEmpty())
        m_buffer.append(bytes.data(), bytes.size());

    const auto returnFailure = [this, &result](ErrorCategory category, const QString &text) {
        ParseResult failure = fail(category, text);
        failure.frames = std::move(result.frames);
        return failure;
    };

    while (m_buffer.size() >= HeaderBytes) {
        const QByteArrayView header(m_buffer.constData(), HeaderBytes);
        if (readBigEndian<quint32>(header, 0) != Magic)
            return returnFailure(ErrorCategory::BadMagic, QStringLiteral("Invalid ECAP magic."));

        const quint16 major = readBigEndian<quint16>(header, 4);
        const quint16 minor = readBigEndian<quint16>(header, 6);
        if (major != CurrentMajor || minor < MinimumMinor || minor > CurrentMinor) {
            return returnFailure(
                ErrorCategory::IncompatibleVersion,
                QStringLiteral("Incompatible Product API protocol version."));
        }
        if (readBigEndian<quint16>(header, 8) != HeaderBytes) {
            return returnFailure(
                ErrorCategory::BadHeader, QStringLiteral("Product API header is not 64 bytes."));
        }
        if (m_negotiatedMinor && minor != *m_negotiatedMinor) {
            return returnFailure(
                ErrorCategory::IncompatibleVersion,
                QStringLiteral("Product API protocol minor changed on one channel."));
        }

        const quint32 payloadLength = readBigEndian<quint32>(header, 16);
        if (payloadLength > maximumPayloadBytes(m_role)) {
            return returnFailure(
                ErrorCategory::TooLarge,
                QStringLiteral("Product API payload exceeds the channel limit."));
        }
        const qsizetype totalBytes = HeaderBytes + qsizetype(payloadLength);
        if (m_buffer.size() < totalBytes)
            break;

        const QByteArrayView wire(m_buffer.constData(), totalBytes);
        const quint32 expectedCrc = readBigEndian<quint32>(header, 60);
        if (frameCrc32c(wire) != expectedCrc) {
            return returnFailure(
                ErrorCategory::BadCrc, QStringLiteral("Product API CRC32C mismatch."));
        }

        const auto type = static_cast<MessageType>(readBigEndian<quint16>(header, 10));
        const quint32 flags = readBigEndian<quint32>(header, 12);
        if (flags & ~KnownFlags) {
            return returnFailure(
                ErrorCategory::InvalidFlags, QStringLiteral("Product API frame has unknown flags."));
        }
        if ((m_direction == FrameDirection::ServerResponse && !(flags & flagValue(Flag::Response)))
            || (m_direction == FrameDirection::ClientRequest && flags != 0)) {
            return returnFailure(
                ErrorCategory::InvalidFlags,
                QStringLiteral("Product API frame flags have the wrong direction."));
        }
        if (!messageAllowedForRole(m_role, m_direction, type)) {
            return returnFailure(
                ErrorCategory::UnsupportedMessage,
                QStringLiteral("Product API message is not valid on this channel."));
        }

        const quint64 sequence = readBigEndian<quint64>(header, 36);
        if (!sequence || sequence <= m_lastSequence) {
            return returnFailure(
                ErrorCategory::BadSequence,
                QStringLiteral("Product API frame sequence is not strictly increasing."));
        }
        if (m_direction == FrameDirection::ClientRequest
            && readBigEndian<quint64>(header, 52) != 0) {
            return returnFailure(
                ErrorCategory::InvalidEnvelope,
                QStringLiteral("Client request timestamp must be zero."));
        }

        Frame frame;
        frame.header.protocolMajor = major;
        frame.header.protocolMinor = minor;
        frame.header.messageType = type;
        frame.header.flags = flags;
        frame.header.payloadLength = payloadLength;
        frame.header.sessionId = readBigEndian<quint64>(header, 20);
        frame.header.requestId = readBigEndian<quint64>(header, 28);
        frame.header.sequence = sequence;
        frame.header.bootId = readBigEndian<quint64>(header, 44);
        frame.header.controllerTimestampNs = readBigEndian<quint64>(header, 52);
        frame.payload = m_buffer.mid(HeaderBytes, payloadLength);

        m_buffer.remove(0, totalBytes);
        m_negotiatedMinor = minor;
        m_lastSequence = sequence;
        result.frames.append(std::move(frame));
    }
    return result;
}

void FrameParser::reset()
{
    m_buffer.clear();
    m_negotiatedMinor.reset();
    m_lastSequence = 0;
    m_error.reset();
}

Role FrameParser::role() const
{
    return m_role;
}

FrameDirection FrameParser::direction() const
{
    return m_direction;
}

std::optional<quint16> FrameParser::negotiatedMinor() const
{
    return m_negotiatedMinor;
}

quint64 FrameParser::lastSequence() const
{
    return m_lastSequence;
}

bool FrameParser::hasError() const
{
    return m_error.has_value();
}

std::optional<Error> FrameParser::error() const
{
    return m_error;
}

ParseResult FrameParser::fail(ErrorCategory category, const QString &text)
{
    m_error = Error{category, text, {}, {}, {}};
    return {{}, m_error};
}

quint32 maximumPayloadBytes(Role role)
{
    switch (role) {
    case Role::Control:
        return ControlMaximumPayloadBytes;
    case Role::Push:
        return PushMaximumPayloadBytes;
    case Role::Bulk:
        return BulkMaximumPayloadBytes;
    }
    return 0;
}

bool isReadOnlyRequest(MessageType type)
{
    return type == MessageType::GetState || type == MessageType::GetCapability
           || type == MessageType::GetPackageState || type == MessageType::GetFirmwareState
           || type == MessageType::ResumeEvents;
}

bool isPackageDeploymentRequest(MessageType type)
{
    return type == MessageType::BulkBegin || type == MessageType::BulkChunk
           || type == MessageType::BulkCommit || type == MessageType::BulkAbort
           || type == MessageType::ValidatePackage || type == MessageType::ActivatePackage
           || type == MessageType::RollbackPackage
           || type == MessageType::RestoreActivePackage;
}

bool isSupportedRequest(MessageType type)
{
    return type == MessageType::Hello || isReadOnlyRequest(type)
           || type == MessageType::AcquireControl || type == MessageType::ReleaseControl
           || type == MessageType::Start || type == MessageType::StartFreeRun
           || type == MessageType::StartDc || type == MessageType::Pause
           || type == MessageType::Resume || type == MessageType::ControlledStop
           || type == MessageType::Heartbeat
           || type == MessageType::EnterConfigurationMode
           || type == MessageType::DiscoverTopology
           || isPackageDeploymentRequest(type);
}

quint32 crc32c(QByteArrayView bytes)
{
    quint32 crc = 0xffffffffU;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0x82f63b78U : 0U);
    }
    return ~crc;
}

QByteArray encodeFrame(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.protocolMajor != CurrentMajor || frame.header.protocolMinor < MinimumMinor
        || frame.header.protocolMinor > CurrentMinor) {
        setError(
            error, ErrorCategory::IncompatibleVersion, QStringLiteral("Invalid protocol version."));
        return {};
    }
    if (frame.header.flags & ~KnownFlags) {
        setError(error, ErrorCategory::InvalidFlags, QStringLiteral("Frame has unknown flags."));
        return {};
    }
    if (!frame.header.sequence) {
        setError(error, ErrorCategory::BadSequence, QStringLiteral("Frame sequence must be nonzero."));
        return {};
    }
    if (frame.payload.size() > BulkMaximumPayloadBytes) {
        setError(
            error,
            ErrorCategory::TooLarge,
            QStringLiteral("Frame payload exceeds the protocol limit."));
        return {};
    }

    QByteArray wire(HeaderBytes + frame.payload.size(), '\0');
    writeBigEndian(wire, 0, Magic);
    writeBigEndian(wire, 4, frame.header.protocolMajor);
    writeBigEndian(wire, 6, frame.header.protocolMinor);
    writeBigEndian(wire, 8, quint16(HeaderBytes));
    writeBigEndian(wire, 10, quint16(frame.header.messageType));
    writeBigEndian(wire, 12, frame.header.flags);
    writeBigEndian(wire, 16, quint32(frame.payload.size()));
    writeBigEndian(wire, 20, frame.header.sessionId);
    writeBigEndian(wire, 28, frame.header.requestId);
    writeBigEndian(wire, 36, frame.header.sequence);
    writeBigEndian(wire, 44, frame.header.bootId);
    writeBigEndian(wire, 52, frame.header.controllerTimestampNs);
    if (!frame.payload.isEmpty())
        std::copy(frame.payload.cbegin(), frame.payload.cend(), wire.begin() + HeaderBytes);
    writeBigEndian(wire, 60, frameCrc32c(wire));
    return wire;
}

QByteArray encodeRequest(
    MessageType type,
    QByteArrayView payload,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint64 bootId,
    quint16 protocolMinor,
    Error *error)
{
    clearError(error);
    if (!isSupportedRequest(type)) {
        setError(
            error,
            ErrorCategory::UnsupportedMessage,
            Tr::tr("The request message type is not supported."));
        return {};
    }
    bool payloadValid = false;
    if (type == MessageType::Hello) {
        payloadValid = payload.size() == 24;
    } else if (type == MessageType::ResumeEvents) {
        payloadValid = payload.size() == 4;
    } else if (type == MessageType::AcquireControl) {
        payloadValid = payload.isEmpty()
                       || (payload.size() == 4
                           && readBigEndian<quint32>(payload, 0) >= 1
                           && readBigEndian<quint32>(payload, 0) <= MaximumLeaseDurationMs);
    } else if (type == MessageType::DiscoverTopology) {
        payloadValid = payload.size() == 8 && readBigEndian<quint16>(payload, 0)
                       && readBigEndian<quint16>(payload, 2) >= 1
                       && readBigEndian<quint16>(payload, 2) <= 64
                       && readBigEndian<quint32>(payload, 4) == 0;
    } else if (type == MessageType::BulkBegin) {
        payloadValid = payload.size() == 24 && readBigEndian<quint64>(payload, 0)
                       && readBigEndian<quint32>(payload, 8)
                       && readBigEndian<quint32>(payload, 12) == 0
                       && readBigEndian<quint32>(payload, 16) == 0
                       && readBigEndian<quint32>(payload, 20) == PackageUploadMode;
    } else if (type == MessageType::BulkChunk) {
        payloadValid = payload.size() > BulkChunkHeaderBytes
                       && payload.size() <= BulkMaximumPayloadBytes
                       && readBigEndian<quint32>(payload, 0) == PackageObjectKind;
    } else if (
        type == MessageType::ValidatePackage || type == MessageType::ActivatePackage
        || type == MessageType::RollbackPackage
        || type == MessageType::RestoreActivePackage) {
        payloadValid = validPackageSelector(payload);
    } else {
        payloadValid = payload.isEmpty();
    }
    if (!payloadValid) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Request payload length is invalid."));
        return {};
    }
    if (!requestId || !sequence) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("Request identity must be nonzero."));
        return {};
    }
    if (type == MessageType::Hello) {
        if (sessionId || bootId) {
            setError(
                error,
                ErrorCategory::InvalidEnvelope,
                QStringLiteral("HELLO header identity must be zero."));
            return {};
        }
    } else if (!sessionId || !bootId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("Post-handshake identity must be nonzero."));
        return {};
    }
    if (type == MessageType::GetFirmwareState && protocolMinor < FirmwareMinor) {
        setError(
            error,
            ErrorCategory::IncompatibleVersion,
            QStringLiteral("Firmware state requires protocol v1.9."));
        return {};
    }
    if ((type == MessageType::StartFreeRun || type == MessageType::StartDc)
        && protocolMinor < ExplicitTimingModeMinor) {
        setError(
            error,
            ErrorCategory::IncompatibleVersion,
            Tr::tr("Explicit timing-mode start requires protocol v1.10."));
        return {};
    }

    Frame frame;
    frame.header.protocolMinor = protocolMinor;
    frame.header.messageType = type;
    frame.header.sessionId = sessionId;
    frame.header.requestId = requestId;
    frame.header.sequence = sequence;
    frame.header.bootId = bootId;
    frame.payload = QByteArray(payload.data(), payload.size());
    return encodeFrame(frame, error);
}

QByteArray encodeHello(
    Role role,
    quint64 resumeSessionId,
    quint64 nonce,
    quint64 requestId,
    quint64 sequence,
    quint16 protocolMinor,
    Error *error)
{
    clearError(error);
    if (!validRole(role)) {
        setError(error, ErrorCategory::InvalidRole, QStringLiteral("HELLO role is invalid."));
        return {};
    }
    if (role != Role::Control && !resumeSessionId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("Push and bulk channels must join a session."));
        return {};
    }
    QByteArray payload;
    payload.reserve(24);
    appendBigEndian(payload, quint32(role));
    appendBigEndian(payload, quint32(0));
    appendBigEndian(payload, resumeSessionId);
    appendBigEndian(payload, nonce);
    return encodeRequest(MessageType::Hello, payload, 0, requestId, sequence, 0, protocolMinor, error);
}

QByteArray encodeResumeEvents(
    quint32 afterSequence,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint64 bootId,
    quint16 protocolMinor,
    Error *error)
{
    QByteArray payload;
    payload.reserve(4);
    appendBigEndian(payload, afterSequence);
    return encodeRequest(
        MessageType::ResumeEvents,
        payload,
        sessionId,
        requestId,
        sequence,
        bootId,
        protocolMinor,
        error);
}

std::optional<HelloAck> decodeHelloAck(
    const Frame &frame, Role expectedRole, quint64 expectedResumeSessionId, Error *error)
{
    clearError(error);
    if (!validRole(expectedRole)) {
        setError(error, ErrorCategory::InvalidRole, QStringLiteral("Expected HELLO role is invalid."));
        return {};
    }
    if (!validateResponseRecord(frame, MessageType::HelloAck, 40, flagValue(Flag::Response), error)) {
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const auto role = static_cast<Role>(readBigEndian<quint32>(payload, 0));
    HelloAck result{
        role,
        readBigEndian<quint32>(payload, 4),
        readBigEndian<quint64>(payload, 8),
        readBigEndian<quint64>(payload, 16),
        readBigEndian<quint64>(payload, 24),
        readBigEndian<quint32>(payload, 32),
        readBigEndian<quint32>(payload, 36),
    };
    if (role != expectedRole) {
        setError(error, ErrorCategory::InvalidRole, QStringLiteral("HELLO_ACK role mismatch."));
        return {};
    }
    if (!result.sessionId || !result.bootId || result.sessionId != frame.header.sessionId
        || result.bootId != frame.header.bootId
        || (expectedResumeSessionId && result.sessionId != expectedResumeSessionId)) {
        setError(
            error, ErrorCategory::IdentityMismatch, QStringLiteral("HELLO_ACK identity mismatch."));
        return {};
    }
    if (result.maximumPayloadBytes != maximumPayloadBytes(role)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("HELLO_ACK channel limit is invalid."));
        return {};
    }
    const quint32 requiredFeatures = requiredFeatureMask(frame.header.protocolMinor);
    if ((result.featureBits & requiredFeatures) != requiredFeatures) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("HELLO_ACK is missing required features."));
        return {};
    }
    if (!result.defaultControlLeaseDurationMs
        || result.defaultControlLeaseDurationMs > MaximumLeaseDurationMs) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("HELLO_ACK lease duration is invalid."));
        return {};
    }
    return result;
}

std::optional<HelloCapacityError> decodeHelloCapacityError(
    const Frame &frame, Role expectedRole, quint64 expectedRequestId, Error *error)
{
    clearError(error);
    if (frame.header.protocolMajor != CurrentMajor || frame.header.protocolMinor < 8
        || frame.header.protocolMinor > CurrentMinor) {
        setError(
            error,
            ErrorCategory::IncompatibleVersion,
            QStringLiteral("Structured HELLO errors require Product API v1.8 or newer."));
        return {};
    }
    if (!validRole(expectedRole) || expectedRole != Role::Control) {
        setError(
            error,
            ErrorCategory::InvalidRole,
            QStringLiteral("Session-capacity errors are valid only for the control channel."));
        return {};
    }
    if (!validateResponseRecord(frame, MessageType::Error, 32, Flag::Response | Flag::Error, error)) {
        return {};
    }
    if (frame.header.sequence != 1) {
        setError(
            error,
            ErrorCategory::BadSequence,
            QStringLiteral("A terminal HELLO error must be the first response frame."));
        return {};
    }
    if (!frame.header.requestId
        || (expectedRequestId && frame.header.requestId != expectedRequestId)) {
        setError(
            error,
            ErrorCategory::IdentityMismatch,
            QStringLiteral("HELLO capacity-error RequestId mismatch."));
        return {};
    }
    const QByteArrayView payload(frame.payload);
    const auto role = static_cast<Role>(readBigEndian<quint32>(payload, 8));
    const quint32 capacity = readBigEndian<quint32>(payload, 12);
    const quint32 sessionsInUse = readBigEndian<quint32>(payload, 16);
    if (readBigEndian<quint16>(payload, 0) != quint16(MessageType::Hello)
        || readBigEndian<quint16>(payload, 2) != 32
        || readBigEndian<qint32>(payload, 4) != StatusSessionCapacity || role != expectedRole
        || role != Role::Control || !capacity || sessionsInUse != capacity
        || readBigEndian<quint64>(payload, 24) || frame.header.sessionId || !frame.header.bootId) {
        setError(
            error, ErrorCategory::InvalidPayload, QStringLiteral("Malformed HELLO capacity error."));
        return {};
    }
    return HelloCapacityError{
        role,
        capacity,
        sessionsInUse,
        readBigEndian<quint32>(payload, 20),
        frame.header.bootId,
    };
}

std::optional<HelloCapacityError> decodeHelloCapacityError(
    const Frame &frame, Role expectedRole, Error *error)
{
    return decodeHelloCapacityError(frame, expectedRole, 0, error);
}

std::optional<CommandStatus> decodeCommandStatus(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.messageType != MessageType::CommandStatus || frame.payload.size() != 40) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Expected exact 40-byte CommandStatus."));
        return {};
    }
    if (!frame.header.requestId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("CommandStatus must have a nonzero RequestId."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const quint16 originalType = readBigEndian<quint16>(payload, 0);
    const quint16 stage = readBigEndian<quint16>(payload, 2);
    const qint32 status = readBigEndian<qint32>(payload, 4);
    const qint32 operationResult = readBigEndian<qint32>(payload, 8);
    const quint32 serviceState = readBigEndian<quint32>(payload, 12);
    const quint64 detail = readBigEndian<quint64>(payload, 28);
    const quint32 finalValue = readBigEndian<quint32>(payload, 36);
    if (!validateStatusFlags(frame, status, error))
        return {};
    if (!commandStatusAllowed(status)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("CommandStatus carries a status not delivered by this response form."));
        return {};
    }
    if (!commandStatusOriginalAllowed(originalType)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("CommandStatus has an invalid original request type."));
        return {};
    }
    if (stage < 1 || stage > 4 || serviceState > 9 || finalValue > 1 || (status && !finalValue)
        || (!status && operationResult) || (!status && finalValue && stage != 4)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("CommandStatus fields violate the public protocol contract."));
        return {};
    }
    if (status == -35) {
        const quint32 requestedMode = quint32(detail >> 32);
        const quint32 actualMode = quint32(detail);
        const quint32 expectedRequestedMode
            = originalType == quint16(MessageType::StartFreeRun)
                  ? 1
                  : originalType == quint16(MessageType::StartDc) ? 2 : 0;
        if (frame.header.protocolMinor < ExplicitTimingModeMinor || stage != 2
            || !expectedRequestedMode || requestedMode != expectedRequestedMode
            || (actualMode != 1 && actualMode != 2) || actualMode == requestedMode) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                Tr::tr("TIMING_MODE_MISMATCH fields violate the v1.10 contract."));
            return {};
        }
    }

    return CommandStatus{
        originalType,
        stage,
        status,
        operationResult,
        serviceState,
        readBigEndian<quint32>(payload, 16),
        readBigEndian<quint64>(payload, 20),
        detail,
        bool(finalValue),
    };
}

std::optional<BulkStatus> decodeBulkStatus(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.messageType != MessageType::BulkStatus || frame.payload.size() != 40) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Expected exact 40-byte BulkStatus."));
        return {};
    }
    if (!frame.header.requestId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("BulkStatus must have a nonzero RequestId."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const qint32 status = readBigEndian<qint32>(payload, 0);
    const qint32 operationResult = readBigEndian<qint32>(payload, 4);
    const quint32 selectedSlot = readBigEndian<quint32>(payload, 8);
    const quint64 generation = readBigEndian<quint64>(payload, 12);
    const quint64 configurationId = readBigEndian<quint64>(payload, 20);
    const quint32 cyclePeriodNs = readBigEndian<quint32>(payload, 28);
    const quint32 minimumCyclePeriodNs = readBigEndian<quint32>(payload, 32);
    const quint16 reserved = readBigEndian<quint16>(payload, 36);
    const quint16 originalType = readBigEndian<quint16>(payload, 38);
    if (!validateStatusFlags(frame, status, error))
        return {};
    if (!bulkStatusAllowed(status)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("BulkStatus carries a status not delivered by this response form."));
        return {};
    }
    if (reserved || !bulkStatusOriginalAllowed(originalType) || (!status && operationResult)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("BulkStatus fields violate the public protocol contract."));
        return {};
    }
    if (selectedSlot) {
        if (status || originalType != 0x0302
            || (selectedSlot != quint32('A') && selectedSlot != quint32('B')) || !generation
            || !configurationId || !cyclePeriodNs || !minimumCyclePeriodNs
            || cyclePeriodNs < minimumCyclePeriodNs) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("BulkStatus has an invalid package selector."));
            return {};
        }
    } else if (generation || configurationId || cyclePeriodNs || minimumCyclePeriodNs) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("BulkStatus has a partial package selector."));
        return {};
    }

    return BulkStatus{
        status,
        operationResult,
        selectedSlot,
        generation,
        configurationId,
        cyclePeriodNs,
        minimumCyclePeriodNs,
        originalType,
    };
}

std::optional<FirmwareStatus> decodeFirmwareStatus(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.protocolMajor != CurrentMajor
        || frame.header.protocolMinor < FirmwareMinor
        || frame.header.protocolMinor > CurrentMinor) {
        setError(
            error,
            ErrorCategory::IncompatibleVersion,
            Tr::tr("FirmwareStatus requires Product API v1.9 or newer."));
        return {};
    }
    if (frame.header.messageType != MessageType::FirmwareStatus || frame.payload.size() != 96) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Expected exact 96-byte FirmwareStatus."));
        return {};
    }
    if (!frame.header.requestId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("FirmwareStatus must have a nonzero RequestId."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const quint16 originalType = readBigEndian<quint16>(payload, 0);
    const quint16 state = readBigEndian<quint16>(payload, 2);
    const qint32 status = readBigEndian<qint32>(payload, 4);
    const qint32 operationResult = readBigEndian<qint32>(payload, 8);
    const quint32 flags = readBigEndian<quint32>(payload, 12);
    const quint32 packageBytes = readBigEndian<quint32>(payload, 24);
    const quint32 durableOffset = readBigEndian<quint32>(payload, 28);
    const quint32 chunkLimit = readBigEndian<quint32>(payload, 32);
    const quint32 progressPerMille = readBigEndian<quint32>(payload, 36);
    const quint32 reserved = readBigEndian<quint32>(payload, 44);
    if (!validateStatusFlags(frame, status, error))
        return {};
    if (!firmwareStatusAllowed(status)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("FirmwareStatus carries a status not delivered by this response form."));
        return {};
    }
    if (!firmwareStatusOriginalAllowed(originalType) || state > 9 || flags & ~FirmwareFlagKnownMask
        || reserved || durableOffset > packageBytes || chunkLimit > FirmwareChunkMaximumBytes
        || progressPerMille > 1000 || (!status && operationResult)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("FirmwareStatus fields violate the Product API v1.9 contract."));
        return {};
    }

    return FirmwareStatus{
        originalType,
        state,
        status,
        operationResult,
        flags,
        readBigEndian<quint64>(payload, 16),
        packageBytes,
        durableOffset,
        chunkLimit,
        progressPerMille,
        readBigEndian<quint32>(payload, 40),
        readBigEndian<quint64>(payload, 48),
        frame.payload.mid(56, 32),
        readBigEndian<quint64>(payload, 88),
    };
}

std::optional<Data::ControllerStateSummary> decodeControllerState(const Frame &frame, Error *error)
{
    clearError(error);
    const quint32 response = flagValue(Flag::Response);
    const quint32 replaceable = Flag::Response | Flag::Replaceable;
    if (frame.header.messageType != MessageType::ControllerState || frame.payload.size() != 80) {
        setError(
            error, ErrorCategory::InvalidPayload, QStringLiteral("Expected exact ControllerState."));
        return {};
    }
    if (frame.header.flags != response && frame.header.flags != replaceable) {
        setError(
            error,
            ErrorCategory::InvalidFlags,
            QStringLiteral("ControllerState flags are invalid."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const quint32 serviceValue = readBigEndian<quint32>(payload, 0);
    const quint32 status = readBigEndian<quint32>(payload, 4);
    const quint32 severityValue = readBigEndian<quint32>(payload, 8);
    const qint32 commandResult = readBigEndian<qint32>(payload, 12);
    const quint64 currentFaults = readBigEndian<quint64>(payload, 16);
    const quint64 latchedFaults = readBigEndian<quint64>(payload, 24);
    const quint64 bootId = readBigEndian<quint64>(payload, 48);
    const quint32 alState = readBigEndian<quint32>(payload, 56);
    const auto serviceState = decodeServiceState(serviceValue);
    const auto severity = decodeSeverity(severityValue);
    if (!serviceState || !severity || status & ~ControllerStatusKnownMask || !(status & 0x1)
        || commandResult < -8 || commandResult > 0
        || (currentFaults | latchedFaults) & ~ControllerFaultKnownMask
        || alState & ~ControllerAlKnownMask) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("ControllerState fields are invalid."));
        return {};
    }
    if (!bootId || bootId != frame.header.bootId) {
        setError(
            error,
            ErrorCategory::IdentityMismatch,
            QStringLiteral("ControllerState BootId mismatch."));
        return {};
    }

    const bool active = status & 0x4;
    const bool safeOutput = status & 0x8;
    const bool fault = status & 0x10;
    const bool recovering = status & 0x20;
    const bool paused = status & 0x100;
    if (active != (serviceValue == 4) || paused != (serviceValue == 9)
        || recovering != (serviceValue == 7)
        || safeOutput
               != (serviceValue == 2 || serviceValue == 3 || serviceValue == 5
                   || serviceValue == 6)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("ControllerState status contradicts its service state."));
        return {};
    }
    const bool hasFault = currentFaults || latchedFaults;
    if (fault != hasFault || (!hasFault && severityValue != 0) || (hasFault && severityValue < 3)
        || ((status & 0x2)
            && !(
                serviceValue == 3 || serviceValue == 4 || serviceValue == 5 || serviceValue == 6
                || serviceValue == 9))) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("ControllerState status relationships are invalid."));
        return {};
    }

    Data::ControllerStateSummary result;
    result.serviceState = *serviceState;
    result.severity = *severity;
    result.ready = status & 0x1;
    result.busOperational = status & 0x2;
    result.applicationActive = active;
    result.safeOutput = safeOutput;
    result.fault = fault;
    result.recovering = recovering;
    result.distributedClocksLocked = status & 0x40;
    result.commandStale = status & 0x80;
    result.paused = paused;
    result.latestCommandResult = commandResult;
    result.currentFaults = currentFaults;
    result.latchedFaults = latchedFaults;
    result.controllerHeartbeat = readBigEndian<quint64>(payload, 32);
    result.cycleCount = readBigEndian<quint64>(payload, 40);
    result.controllerBootId = bootId;
    result.ethercatAlStateBits = alState;
    result.expectedWorkingCounter = readBigEndian<quint32>(payload, 60);
    result.actualWorkingCounter = readBigEndian<quint32>(payload, 64);
    result.distributedClockDifferenceNs = readBigEndian<quint32>(payload, 68);
    result.latestAlarmSequence = readBigEndian<quint32>(payload, 76);
    return result;
}

std::optional<Data::ControllerAlarmSummary> decodeAlarmEvent(
    const Frame &frame, Error *error)
{
    clearError(error);
    const bool raised = frame.header.messageType == MessageType::AlarmRaised;
    const bool cleared = frame.header.messageType == MessageType::AlarmCleared;
    if ((!raised && !cleared) || frame.payload.size() != 64) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Expected an exact 64-byte AlarmEvent."));
        return {};
    }

    const quint32 allowedFrameFlags = flagValue(Flag::Response) | flagValue(Flag::Important)
                                      | flagValue(Flag::More);
    const quint32 requiredFrameFlags = flagValue(Flag::Response) | flagValue(Flag::Important);
    if ((frame.header.flags & ~allowedFrameFlags)
        || (frame.header.flags & requiredFrameFlags) != requiredFrameFlags) {
        setError(
            error,
            ErrorCategory::InvalidFlags,
            QStringLiteral("AlarmEvent frame flags are invalid."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const quint32 sequence = readBigEndian<quint32>(payload, 0);
    const quint32 code = readBigEndian<quint32>(payload, 4);
    const quint32 severityValue = readBigEndian<quint32>(payload, 8);
    const quint32 sourceValue = readBigEndian<quint32>(payload, 12);
    const quint32 eventFlags = readBigEndian<quint32>(payload, 16);
    const quint64 faultMask = readBigEndian<quint64>(payload, 48);
    const auto severity = decodeSeverity(severityValue);
    if (!sequence || !code || !severity || severityValue == 0 || sourceValue < 1
        || sourceValue > 5 || (eventFlags != 0x1 && eventFlags != 0x2 && eventFlags != 0x5)
        || (raised && !(eventFlags & 0x1)) || (cleared && eventFlags != 0x2)
        || faultMask & ~ControllerFaultKnownMask || readBigEndian<quint64>(payload, 56)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("AlarmEvent fields are invalid."));
        return {};
    }

    Data::ControllerAlarmSource source = Data::ControllerAlarmSource::Unknown;
    switch (sourceValue) {
    case 1:
        source = Data::ControllerAlarmSource::Service;
        break;
    case 2:
        source = Data::ControllerAlarmSource::Transport;
        break;
    case 3:
        source = Data::ControllerAlarmSource::Protocol;
        break;
    case 4:
        source = Data::ControllerAlarmSource::DistributedClocks;
        break;
    case 5:
        source = Data::ControllerAlarmSource::Application;
        break;
    }

    const quint32 detail0 = readBigEndian<quint32>(payload, 20);
    const quint32 detail1 = readBigEndian<quint32>(payload, 24);
    const quint32 detail2 = readBigEndian<quint32>(payload, 28);
    if (code == 3 && detail2) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("RuntimeError AlarmEvent has a nonzero reserved detail."));
        return {};
    }
    QString codeName = Tr::tr("Alarm code %1").arg(code);
    QString detail;
    if (code == 3) {
        codeName = Tr::tr("Runtime error");
        const qint32 signedResult = qint32(detail0);
        detail = signedResult == -2 && detail1 == 255
                     ? Tr::tr("OSL_ERR_TIMEOUT (%1), phase FAILED (%2)")
                           .arg(signedResult)
                           .arg(detail1)
                     : Tr::tr("result %1, phase %2").arg(signedResult).arg(detail1);
    } else if (code == 11) {
        codeName = Tr::tr("RX timeout");
        const quint32 pendingFrames = detail0 - detail1;
        detail = Tr::tr("TX %1, RX %2, pending %3, last frame %4")
                     .arg(detail0)
                     .arg(detail1)
                     .arg(pendingFrames)
                     .arg(detail2);
    }

    return Data::ControllerAlarmSummary{
        sequence,
        code,
        codeName,
        raised ? Data::ControllerAlarmState::Raised : Data::ControllerAlarmState::Cleared,
        *severity,
        source,
        bool(eventFlags & 0x4),
        detail0,
        detail1,
        detail2,
        readBigEndian<quint64>(payload, 32),
        readBigEndian<quint64>(payload, 40),
        faultMask,
        detail,
    };
}

std::optional<Data::ControllerPerformanceSummary> decodePerformanceSnapshot(
    const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.protocolMajor != CurrentMajor
        || frame.header.protocolMinor < MinimumMinor
        || frame.header.protocolMinor > CurrentMinor) {
        setError(
            error,
            ErrorCategory::IncompatibleVersion,
            QStringLiteral("PerformanceSnapshot uses an unsupported Product API version."));
        return {};
    }

    const qsizetype expectedBytes = frame.header.protocolMinor == 1
                                        ? 240
                                        : (frame.header.protocolMinor <= 6 ? 256 : 320);
    if (frame.header.messageType != MessageType::PerformanceSnapshot
        || frame.payload.size() != expectedBytes) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Expected exact PerformanceSnapshot payload."));
        return {};
    }
    if (frame.header.flags != (Flag::Response | Flag::Replaceable)) {
        setError(
            error,
            ErrorCategory::InvalidFlags,
            QStringLiteral("PerformanceSnapshot flags are invalid."));
        return {};
    }
    if (frame.header.requestId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            QStringLiteral("PerformanceSnapshot must have RequestId zero."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    if (readBigEndian<quint32>(payload, 164)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PerformanceSnapshot reserved bytes are nonzero."));
        return {};
    }

    Data::ControllerPerformanceSummary result;
    result.minimumExchangeTimeNs = readBigEndian<quint64>(payload, 0);
    result.maximumExchangeTimeNs = readBigEndian<quint64>(payload, 8);
    result.maximumSubmitLatenessNs = readBigEndian<quint64>(payload, 16);
    result.timeoutCount = readBigEndian<quint32>(payload, 24);
    result.receiveDropCount = readBigEndian<quint32>(payload, 28);
    result.receiveOverflowCount = readBigEndian<quint32>(payload, 32);
    result.transmitUnavailableCount = readBigEndian<quint32>(payload, 36);
    result.cycleLateCount = readBigEndian<quint32>(payload, 40);
    result.badWorkingCounterCount = readBigEndian<quint32>(payload, 44);
    result.protocolErrorCount = readBigEndian<quint32>(payload, 48);
    result.staleReceiveCount = readBigEndian<quint32>(payload, 52);
    result.handoffSkippedCycleCount = readBigEndian<quint32>(payload, 56);
    result.fpgaLatencyStatusFlags = readBigEndian<quint32>(payload, 120);
    result.fpgaSnapshotSequence = readBigEndian<quint32>(payload, 124);
    if (frame.header.protocolMinor < 7)
        return result;

    const std::array<quint32, 4> legacyWords{
        readBigEndian<quint32>(payload, 240),
        readBigEndian<quint32>(payload, 244),
        readBigEndian<quint32>(payload, 248),
        readBigEndian<quint32>(payload, 252),
    };
    const std::array<quint32, 4> sampleWords{
        readBigEndian<quint32>(payload, 256),
        readBigEndian<quint32>(payload, 260),
        readBigEndian<quint32>(payload, 264),
        readBigEndian<quint32>(payload, 268),
    };
    const quint32 flags = readBigEndian<quint32>(payload, 272);
    const quint32 offset = readBigEndian<quint32>(payload, 276);
    const quint32 count = readBigEndian<quint32>(payload, 280);
    const quint32 total = readBigEndian<quint32>(payload, 284);
    const quint32 sequence = readBigEndian<quint32>(payload, 288);
    const quint32 age = readBigEndian<quint32>(payload, 292);
    const quint64 generation = readBigEndian<quint64>(payload, 296);
    const quint64 configurationId = readBigEndian<quint64>(payload, 304);
    const quint64 cycleCount = readBigEndian<quint64>(payload, 312);
    constexpr quint32 Valid = 1U << 0;
    constexpr quint32 Fresh = 1U << 1;
    constexpr quint32 Complete = 1U << 2;
    constexpr quint32 KnownSampleFlags = Valid | Fresh | Complete;
    constexpr quint32 MaximumProcessInputBytes = 8192;
    constexpr quint32 FreshMaximumAgeCycles = 128;

    if (flags & ~KnownSampleFlags) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PerformanceSnapshot sample flags are invalid."));
        return {};
    }
    if (!(flags & Valid)) {
        for (qsizetype index = 256; index < expectedBytes; ++index) {
            if (payload[index]) {
                setError(
                    error,
                    ErrorCategory::InvalidPayload,
                    QStringLiteral("PerformanceSnapshot invalid sample is not zero."));
                return {};
            }
        }
        return result;
    }

    if (count < 1 || count > 16 || total < 1 || total > MaximumProcessInputBytes
        || count > total || offset > total - count || offset % 16
        || count != std::min<quint32>(16, total - offset) || !sequence || !generation
        || !configurationId) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PerformanceSnapshot sample bounds are invalid."));
        return {};
    }
    const bool complete = offset == 0 && count == total;
    const bool fresh = age <= FreshMaximumAgeCycles;
    if (bool(flags & Complete) != complete || bool(flags & Fresh) != fresh) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PerformanceSnapshot sample state is inconsistent."));
        return {};
    }

    QByteArray sampleBytes;
    sampleBytes.reserve(16);
    for (const quint32 word : sampleWords) {
        sampleBytes.append(char(word));
        sampleBytes.append(char(word >> 8));
        sampleBytes.append(char(word >> 16));
        sampleBytes.append(char(word >> 24));
    }
    for (qsizetype index = count; index < sampleBytes.size(); ++index) {
        if (sampleBytes.at(index)) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("PerformanceSnapshot sample padding is nonzero."));
            return {};
        }
    }
    if (!offset && sampleWords != legacyWords) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PerformanceSnapshot legacy and window samples disagree."));
        return {};
    }

    result.processInputSampleValid = true;
    result.processInputSampleFresh = fresh;
    result.processInputSampleComplete = complete;
    result.processInputSampleOffset = offset;
    result.processInputSampleTotalBytes = total;
    result.processInputSampleSequence = sequence;
    result.processInputSampleAgeCycles = age;
    result.processInputSampleGeneration = generation;
    result.processInputSampleConfigurationId = configurationId;
    result.processInputSampleCycleCount = cycleCount;
    result.processInputSample = sampleBytes.first(count);
    return result;
}

std::optional<Data::ControllerCapabilitySummary> decodeCapability(
    const Frame &frame, quint32 featureBits, Error *error)
{
    clearError(error);
    if (frame.header.messageType != MessageType::Capability) {
        setError(
            error, ErrorCategory::InvalidEnvelope, QStringLiteral("Expected Capability response."));
        return {};
    }
    if (frame.header.flags != flagValue(Flag::Response)) {
        setError(error, ErrorCategory::InvalidFlags, QStringLiteral("Capability flags are invalid."));
        return {};
    }
    Data::ControllerCapabilitySummary result;
    result.descriptorSha256 = QCryptographicHash::hash(frame.payload, QCryptographicHash::Sha256);
    result.controlLease = featureBits & ControlLeaseFeature;
    result.resumablePush = featureBits & ResumablePushFeature;
    result.transactionalBulk = featureBits & TransactionalBulkFeature;
    result.capabilityQuery = featureBits & CapabilityQueryFeature;
    result.sdoFailureDiagnostics = featureBits & SdoFailureDiagnosticFeature;
    result.exactAlarmReplay = featureBits & ExactAlarmReplayFeature;
    result.linkDiagnostics = featureBits & LinkDiagnosticsFeature;
    result.timeCorrelation = featureBits & TimeCorrelationFeature;
    result.processInputSample = featureBits & ProcessInputSampleFeature;
    result.structuredHandshakeError = featureBits & StructuredHelloErrorFeature;
    result.firmwareUpdate = featureBits & FirmwareUpdateFeature;
    result.explicitTimingModeStart = featureBits & ExplicitTimingModeStartFeature;
    return result;
}

std::optional<Data::ControllerPackageSummary> decodePackageState(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.messageType != MessageType::PackageState || frame.payload.size() != 72) {
        setError(error, ErrorCategory::InvalidPayload, QStringLiteral("Expected exact PackageState."));
        return {};
    }
    const QByteArrayView payload(frame.payload);
    const quint16 originalType = readBigEndian<quint16>(payload, 0);
    const qint32 status = readBigEndian<qint32>(payload, 4);
    const qint32 operationResult = readBigEndian<qint32>(payload, 8);
    const quint32 expectedFlags = flagValue(Flag::Response) | (status ? flagValue(Flag::Error) : 0);
    if (frame.header.flags != expectedFlags) {
        setError(
            error, ErrorCategory::InvalidFlags, QStringLiteral("PackageState flags are invalid."));
        return {};
    }
    if (readBigEndian<quint16>(payload, 2) || originalType < quint16(MessageType::GetPackageState)
        || originalType > 0x0407 || !packageStatusAllowed(status) || (!status && operationResult)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PackageState status fields are invalid."));
        return {};
    }
    if (status) {
        setError(
            error,
            ErrorCategory::ControllerStatus,
            QStringLiteral("Controller rejected the package-state request."),
            status,
            operationResult);
        return {};
    }

    const quint32 stagedSlotValue = readBigEndian<quint32>(payload, 12);
    const quint64 stagedGeneration = readBigEndian<quint64>(payload, 16);
    const quint64 stagedConfigurationId = readBigEndian<quint64>(payload, 24);
    const quint32 activeSlotValue = readBigEndian<quint32>(payload, 32);
    const quint64 activeGeneration = readBigEndian<quint64>(payload, 40);
    const quint64 activeConfigurationId = readBigEndian<quint64>(payload, 48);
    const auto stagedSlot = decodeSlot(stagedSlotValue);
    const auto activeSlot = decodeSlot(activeSlotValue);
    if (!stagedSlot || !activeSlot
        || ((*stagedSlot == Data::ControllerSlot::None)
            != (!stagedGeneration && !stagedConfigurationId))
        || ((*activeSlot == Data::ControllerSlot::None)
            != (!activeGeneration && !activeConfigurationId))
        || (*stagedSlot != Data::ControllerSlot::None
            && (!stagedGeneration || !stagedConfigurationId))
        || (*activeSlot != Data::ControllerSlot::None
            && (!activeGeneration || !activeConfigurationId))) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PackageState selector is invalid."));
        return {};
    }

    const quint32 cpuStateValue = readBigEndian<quint32>(payload, 36);
    const qint32 cpuResult = readBigEndian<qint32>(payload, 56);
    const quint32 cpuSequence = readBigEndian<quint32>(payload, 60);
    const quint64 cpuBootId = readBigEndian<quint64>(payload, 64);
    const auto cpuState = decodeCpuPackageState(cpuStateValue);
    if (!cpuState) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("PackageState CPU1 state is invalid."));
        return {};
    }
    if (!cpuBootId) {
        if (cpuStateValue || cpuResult || cpuSequence) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("PackageState has a partial unavailable tuple."));
            return {};
        }
    } else if (cpuBootId != frame.header.bootId) {
        setError(
            error,
            ErrorCategory::IdentityMismatch,
            QStringLiteral("PackageState CPU1 BootId mismatch."));
        return {};
    }

    Data::ControllerPackageSummary result;
    result.stagedSlot = *stagedSlot;
    result.stagedGeneration = stagedGeneration;
    result.stagedConfigurationId = stagedConfigurationId;
    result.activeSlot = *activeSlot;
    result.activeGeneration = activeGeneration;
    result.activeConfigurationId = activeConfigurationId;
    result.controllerState = cpuBootId ? *cpuState : Data::ControllerPackageState::Unavailable;
    result.controllerResult = cpuResult;
    result.controllerRequestSequence = cpuSequence;
    result.controllerBootId = cpuBootId;
    return result;
}

std::optional<TopologyResult> decodeTopologyResult(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.messageType != MessageType::TopologyResult || frame.payload.size() < 32) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            Tr::tr("Expected a complete TopologyResult."));
        return {};
    }
    if (frame.header.flags != flagValue(Flag::Response) || !frame.header.requestId) {
        setError(
            error,
            ErrorCategory::InvalidEnvelope,
            Tr::tr("TopologyResult has an invalid response envelope."));
        return {};
    }

    const QByteArrayView payload(frame.payload);
    const quint16 originalType = readBigEndian<quint16>(payload, 0);
    const quint16 recordBytes = readBigEndian<quint16>(payload, 2);
    const qint32 result = readBigEndian<qint32>(payload, 4);
    const quint16 count = readBigEndian<quint16>(payload, 8);
    const quint16 responding = readBigEndian<quint16>(payload, 10);
    const quint32 flags = readBigEndian<quint32>(payload, 16);
    const quint32 combinedAlState = readBigEndian<quint32>(payload, 20);
    const qsizetype expectedBytes = 32 + qsizetype(count) * 24;
    if (originalType != quint16(MessageType::DiscoverTopology) || recordBytes != 24 || result
        || count != responding || count > 64 || flags
        || combinedAlState & ~ControllerAlKnownMask
        || frame.payload.size() != expectedBytes) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            Tr::tr("TopologyResult fields violate the discovery contract."));
        return {};
    }

    TopologyResult topology;
    topology.result = result;
    topology.respondingCount = responding;
    topology.combinedAlState = combinedAlState;
    topology.slaves.reserve(count);
    for (quint16 index = 0; index < count; ++index) {
        const qsizetype offset = 32 + qsizetype(index) * 24;
        TopologySlave slave;
        slave.position = readBigEndian<quint16>(payload, offset);
        slave.stationAddress = readBigEndian<quint16>(payload, offset + 2);
        slave.alState = readBigEndian<quint16>(payload, offset + 4);
        slave.flags = readBigEndian<quint16>(payload, offset + 6);
        slave.vendorId = readBigEndian<quint32>(payload, offset + 8);
        slave.productCode = readBigEndian<quint32>(payload, offset + 12);
        slave.revision = readBigEndian<quint32>(payload, offset + 16);
        slave.serial = readBigEndian<quint32>(payload, offset + 20);
        if (!slave.stationAddress) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                Tr::tr("TopologyResult contains an invalid station address."));
            return {};
        }
        topology.slaves.append(slave);
    }
    return topology;
}

std::optional<Data::ControllerFirmwareSummary> decodeFirmwareState(const Frame &frame, Error *error)
{
    clearError(error);
    const bool progress = frame.header.messageType == MessageType::FirmwareProgress;
    if ((!progress && frame.header.messageType != MessageType::FirmwareState)
        || frame.payload.size() != 192) {
        setError(
            error, ErrorCategory::InvalidPayload, QStringLiteral("Expected exact FirmwareState."));
        return {};
    }
    const QByteArrayView payload(frame.payload);
    const quint16 originalType = readBigEndian<quint16>(payload, 0);
    const quint16 lifecycleValue = readBigEndian<quint16>(payload, 2);
    const qint32 status = readBigEndian<qint32>(payload, 4);
    const qint32 operationResult = readBigEndian<qint32>(payload, 8);
    const quint32 flags = readBigEndian<quint32>(payload, 12);
    quint32 expectedFlags = flagValue(Flag::Response);
    if (progress)
        expectedFlags |= flagValue(Flag::Replaceable);
    else if (status)
        expectedFlags |= flagValue(Flag::Error);
    if (frame.header.flags != expectedFlags) {
        setError(
            error, ErrorCategory::InvalidFlags, QStringLiteral("FirmwareState flags are invalid."));
        return {};
    }
    if ((progress
         && (frame.header.requestId || status || operationResult
             || originalType != quint16(MessageType::FirmwareProgress)))
        || (!progress && originalType != quint16(MessageType::GetFirmwareState))
        || !firmwareStateStatusAllowed(status)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("FirmwareState envelope is invalid."));
        return {};
    }
    const auto lifecycle = decodeFirmwareLifecycle(lifecycleValue);
    const auto activeSlot = decodeByteSlot(quint8(payload.at(16)));
    const auto previousSlot = decodeByteSlot(quint8(payload.at(17)));
    const auto targetSlot = decodeByteSlot(quint8(payload.at(18)));
    const quint32 packageBytes = readBigEndian<quint32>(payload, 28);
    const quint32 durableOffset = readBigEndian<quint32>(payload, 32);
    const quint32 progressPerMille = readBigEndian<quint32>(payload, 36);
    const quint32 failureStage = readBigEndian<quint32>(payload, 60);
    const quint64 updatedEpoch = readBigEndian<quint64>(payload, 168);
    if (!lifecycle || !activeSlot || !previousSlot || !targetSlot || quint8(payload.at(19))
        || flags & ~FirmwareFlagKnownMask || failureStage > 11 || durableOffset > packageBytes
        || progressPerMille > 1000 || readBigEndian<quint64>(payload, 184)
        || (!status && operationResult)
        || updatedEpoch > quint64(std::numeric_limits<qint64>::max())) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("FirmwareState fields are invalid."));
        return {};
    }
    if (status) {
        setError(
            error,
            ErrorCategory::ControllerStatus,
            QStringLiteral("Controller rejected the firmware-state request."),
            status,
            operationResult,
            readBigEndian<quint64>(payload, 176));
        return {};
    }

    Data::ControllerFirmwareSummary result;
    result.state = *lifecycle;
    result.activeSlot = *activeSlot;
    result.previousSlot = *previousSlot;
    result.targetSlot = *targetSlot;
    result.packageVerified = flags & (1U << 1);
    result.rebootRequired = flags & (1U << 3);
    result.confirmed = flags & (1U << 5);
    result.rolledBack = flags & (1U << 6);
    result.progressPerMille = int(progressPerMille);
    result.failureStage = int(failureStage);
    result.lastFailure = readBigEndian<qint32>(payload, 152);
    result.lastSystemError = readBigEndian<qint32>(payload, 156);
    result.generation = readBigEndian<quint64>(payload, 160);
    result.updatedAt = QDateTime::fromSecsSinceEpoch(qint64(updatedEpoch), Qt::UTC);
    return result;
}

std::optional<ResumeEventsResult> decodeResumeEventsResult(const Frame &frame, Error *error)
{
    clearError(error);
    if (frame.header.protocolMinor < 4
        || frame.header.messageType != MessageType::ResumeEventsResult
        || frame.payload.size() != 32) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("Expected exact ResumeEventsResult."));
        return {};
    }
    const quint32 allowedFlags = flagValue(Flag::Response) | flagValue(Flag::Error)
                                 | flagValue(Flag::More);
    if ((frame.header.flags & ~allowedFlags) || !(frame.header.flags & flagValue(Flag::Response))) {
        setError(
            error,
            ErrorCategory::InvalidFlags,
            QStringLiteral("ResumeEventsResult flags are invalid."));
        return {};
    }
    const QByteArrayView payload(frame.payload);
    ResumeEventsResult result{
        readBigEndian<qint32>(payload, 0),
        readBigEndian<quint32>(payload, 4),
        readBigEndian<quint32>(payload, 8),
        readBigEndian<quint32>(payload, 12),
        readBigEndian<quint32>(payload, 16),
        readBigEndian<quint32>(payload, 20),
    };
    const bool success = result.status == StatusOk;
    if ((!success && result.status != StatusInternal && result.status != StatusEventGap)
        || result.ringCapacity != AlarmRingCapacity || result.replayCount > result.ringCapacity
        || readBigEndian<quint64>(payload, 24)
        || bool(frame.header.flags & flagValue(Flag::Error)) == success
        || bool(frame.header.flags & flagValue(Flag::More)) != (success && result.replayCount > 0)) {
        setError(
            error,
            ErrorCategory::InvalidPayload,
            QStringLiteral("ResumeEventsResult fields are invalid."));
        return {};
    }
    if (result.status == StatusInternal) {
        if (result.replayCount) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("Failed replay result contains events."));
            return {};
        }
    } else if (!result.latestSequence) {
        const qint32 expectedStatus = result.requestedAfterSequence ? StatusEventGap : StatusOk;
        if (result.oldestSequence || result.replayCount || result.status != expectedStatus) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("Empty replay range is inconsistent."));
            return {};
        }
    } else {
        if (!result.oldestSequence) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("Replay range has no oldest sequence."));
            return {};
        }
        const quint32 available = alarmSequenceDistance(result.oldestSequence, result.latestSequence)
                                  + 1;
        if (available > result.ringCapacity) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("Replay range exceeds the ring."));
            return {};
        }
        if (success) {
            quint32 expectedCount = 0;
            if (!result.requestedAfterSequence)
                expectedCount = available;
            else if (result.requestedAfterSequence != result.latestSequence) {
                expectedCount
                    = alarmSequenceDistance(result.requestedAfterSequence, result.latestSequence);
                if (expectedCount > available
                    || (expectedCount == available && available < result.ringCapacity)) {
                    setError(
                        error,
                        ErrorCategory::InvalidPayload,
                        QStringLiteral("Replay result acknowledges a sequence gap."));
                    return {};
                }
            }
            if (result.replayCount != expectedCount) {
                setError(
                    error,
                    ErrorCategory::InvalidPayload,
                    QStringLiteral("Replay count is inconsistent."));
                return {};
            }
        } else if (
            result.status == StatusEventGap
            && (!result.requestedAfterSequence || result.replayCount)) {
            setError(
                error,
                ErrorCategory::InvalidPayload,
                QStringLiteral("Replay gap result is invalid."));
            return {};
        }
    }
    return result;
}

std::optional<PushHeartbeat> decodePushHeartbeat(const Frame &frame, Error *error)
{
    clearError(error);
    if (!validateResponseRecord(
            frame, MessageType::PushHeartbeat, 16, Flag::Response | Flag::Replaceable, error)) {
        return {};
    }
    const QByteArrayView payload(frame.payload);
    const quint64 bootId = readBigEndian<quint64>(payload, 8);
    if (frame.header.requestId || !bootId || bootId != frame.header.bootId) {
        setError(
            error,
            ErrorCategory::IdentityMismatch,
            QStringLiteral("PushHeartbeat identity mismatch."));
        return {};
    }
    return PushHeartbeat{readBigEndian<quint64>(payload, 0), bootId};
}

} // namespace EtherCAT::ProductApi::Internal::Protocol
