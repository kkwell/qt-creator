// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/controllerconnection.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>

namespace EtherCAT::ProductApi::Internal::Protocol {

inline constexpr qsizetype HeaderBytes = 64;
inline constexpr quint32 Magic = 0x45434150;
inline constexpr quint16 CurrentMajor = 1;
inline constexpr quint16 MinimumMinor = 1;
inline constexpr quint16 CurrentMinor = 9;
inline constexpr quint32 ControlMaximumPayloadBytes = 4096;
inline constexpr quint32 PushMaximumPayloadBytes = 65536;
inline constexpr quint32 BulkMaximumPayloadBytes = 65536;

enum class Role : quint32 {
    Control = 1,
    Push = 2,
    Bulk = 3,
};

enum class MessageType : quint16 {
    Hello = 0x0001,
    HelloAck = 0x0002,
    Error = 0x0003,
    GetState = 0x0108,
    CommandStatus = 0x0180,
    ControllerState = 0x0200,
    AlarmRaised = 0x0207,
    AlarmCleared = 0x0208,
    PerformanceSnapshot = 0x0209,
    PushHeartbeat = 0x020a,
    FirmwareProgress = 0x020b,
    ResumeEvents = 0x0210,
    ResumeEventsResult = 0x0280,
    BulkStatus = 0x0380,
    GetCapability = 0x0400,
    GetPackageState = 0x0403,
    Capability = 0x0480,
    PackageState = 0x0483,
    GetFirmwareState = 0x0504,
    FirmwareStatus = 0x0580,
    FirmwareState = 0x0581,
};

enum class Flag : quint32 {
    Response = 1U << 0,
    Error = 1U << 1,
    More = 1U << 2,
    Important = 1U << 3,
    Replaceable = 1U << 4,
};

constexpr quint32 flagValue(Flag flag)
{
    return static_cast<quint32>(flag);
}

constexpr quint32 operator|(Flag left, Flag right)
{
    return flagValue(left) | flagValue(right);
}

enum class FrameDirection {
    ClientRequest,
    ServerResponse,
};

struct FrameHeader
{
    quint16 protocolMajor = CurrentMajor;
    quint16 protocolMinor = CurrentMinor;
    MessageType messageType = MessageType::Error;
    quint32 flags = 0;
    quint32 payloadLength = 0;
    quint64 sessionId = 0;
    quint64 requestId = 0;
    quint64 sequence = 0;
    quint64 bootId = 0;
    quint64 controllerTimestampNs = 0;
};

struct Frame
{
    FrameHeader header;
    QByteArray payload;
};

enum class ErrorCategory {
    None,
    BadMagic,
    IncompatibleVersion,
    BadHeader,
    TooLarge,
    BadCrc,
    BadSequence,
    InvalidFlags,
    InvalidRole,
    InvalidEnvelope,
    InvalidPayload,
    IdentityMismatch,
    UnsupportedMessage,
    ControllerStatus,
};

struct Error
{
    ErrorCategory category = ErrorCategory::None;
    QString text;
    std::optional<qint32> status;
    std::optional<qint32> operationResult;
    std::optional<quint64> sourceDetail;

    explicit operator bool() const { return category != ErrorCategory::None; }
};

struct ParseResult
{
    QList<Frame> frames;
    std::optional<Error> error;
};

class FrameParser
{
public:
    explicit FrameParser(Role role, FrameDirection direction = FrameDirection::ServerResponse);

    ParseResult append(QByteArrayView bytes);
    void reset();

    Role role() const;
    FrameDirection direction() const;
    std::optional<quint16> negotiatedMinor() const;
    quint64 lastSequence() const;
    bool hasError() const;
    std::optional<Error> error() const;

private:
    ParseResult fail(ErrorCategory category, const QString &text);

    Role m_role;
    FrameDirection m_direction;
    QByteArray m_buffer;
    std::optional<quint16> m_negotiatedMinor;
    quint64 m_lastSequence = 0;
    std::optional<Error> m_error;
};

struct HelloAck
{
    Role role = Role::Control;
    quint32 maximumPayloadBytes = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 controlLeaseOwnerSessionId = 0;
    quint32 featureBits = 0;
    quint32 defaultControlLeaseDurationMs = 0;
};

struct HelloCapacityError
{
    Role role = Role::Control;
    quint32 capacity = 0;
    quint32 sessionsInUse = 0;
    quint32 retryAfterMs = 0;
    quint64 bootId = 0;
};

struct CommandStatus
{
    quint16 originalType = 0;
    quint16 stage = 0;
    qint32 status = 0;
    qint32 operationResult = 0;
    quint32 serviceState = 0;
    quint32 controlLeaseGeneration = 0;
    quint64 controlLeaseExpiryNs = 0;
    quint64 detail = 0;
    bool final = false;
};

struct BulkStatus
{
    qint32 status = 0;
    qint32 operationResult = 0;
    quint32 selectedSlot = 0;
    quint64 generation = 0;
    quint64 configurationId = 0;
    quint32 cyclePeriodNs = 0;
    quint32 minimumCyclePeriodNs = 0;
    quint16 originalType = 0;
};

struct FirmwareStatus
{
    quint16 originalType = 0;
    quint16 state = 0;
    qint32 status = 0;
    qint32 operationResult = 0;
    quint32 flags = 0;
    quint64 uploadId = 0;
    quint32 packageBytes = 0;
    quint32 durableOffset = 0;
    quint32 chunkLimit = 0;
    quint32 progressPerMille = 0;
    quint32 stagedSecurityEpoch = 0;
    quint64 stagedReleaseSequence = 0;
    QByteArray packageSha256;
    quint64 detail = 0;
};

struct ResumeEventsResult
{
    qint32 status = 0;
    quint32 requestedAfterSequence = 0;
    quint32 oldestSequence = 0;
    quint32 latestSequence = 0;
    quint32 replayCount = 0;
    quint32 ringCapacity = 0;
};

struct PushHeartbeat
{
    quint64 heartbeat = 0;
    quint64 bootId = 0;
};

quint32 maximumPayloadBytes(Role role);
bool isReadOnlyRequest(MessageType type);
quint32 crc32c(QByteArrayView bytes);

QByteArray encodeFrame(const Frame &frame, Error *error = nullptr);
QByteArray encodeRequest(
    MessageType type,
    QByteArrayView payload,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint64 bootId,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);
QByteArray encodeHello(
    Role role,
    quint64 resumeSessionId,
    quint64 nonce,
    quint64 requestId,
    quint64 sequence,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);
QByteArray encodeResumeEvents(
    quint32 afterSequence,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint64 bootId,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);

std::optional<HelloAck> decodeHelloAck(
    const Frame &frame,
    Role expectedRole,
    quint64 expectedResumeSessionId = 0,
    Error *error = nullptr);
std::optional<HelloCapacityError> decodeHelloCapacityError(
    const Frame &frame, Role expectedRole, quint64 expectedRequestId, Error *error = nullptr);
std::optional<HelloCapacityError> decodeHelloCapacityError(
    const Frame &frame, Role expectedRole, Error *error = nullptr);
std::optional<CommandStatus> decodeCommandStatus(const Frame &frame, Error *error = nullptr);
std::optional<BulkStatus> decodeBulkStatus(const Frame &frame, Error *error = nullptr);
std::optional<FirmwareStatus> decodeFirmwareStatus(const Frame &frame, Error *error = nullptr);
std::optional<Data::ControllerStateSummary> decodeControllerState(
    const Frame &frame, Error *error = nullptr);
std::optional<Data::ControllerCapabilitySummary> decodeCapability(
    const Frame &frame, quint32 featureBits, Error *error = nullptr);
std::optional<Data::ControllerPackageSummary> decodePackageState(
    const Frame &frame, Error *error = nullptr);
std::optional<Data::ControllerFirmwareSummary> decodeFirmwareState(
    const Frame &frame, Error *error = nullptr);
std::optional<ResumeEventsResult> decodeResumeEventsResult(
    const Frame &frame, Error *error = nullptr);
std::optional<PushHeartbeat> decodePushHeartbeat(const Frame &frame, Error *error = nullptr);

} // namespace EtherCAT::ProductApi::Internal::Protocol
