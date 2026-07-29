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
inline constexpr quint16 FirmwareMinor = 9;
inline constexpr quint16 ExplicitTimingModeMinor = 10;
inline constexpr quint16 ControlledFaultResetMinor = 11;
inline constexpr quint16 RuntimeResourceMinor = 12;
inline constexpr quint16 OutputTransactionMinor = 14;
inline constexpr quint16 CurrentMinor = OutputTransactionMinor;
inline constexpr quint32 RuntimeResourceFeature = 1U << 13;
inline constexpr quint32 OutputTransactionFeature = 1U << 15;
inline constexpr quint32 OutputTransactionMaximumTtlCycles = 65535;
inline constexpr quint32 ControlMaximumPayloadBytes = 4096;
inline constexpr quint32 PushMaximumPayloadBytes = 65536;
inline constexpr quint32 BulkMaximumPayloadBytes = 65536;
inline constexpr quint32 BulkChunkHeaderBytes = 8;
inline constexpr quint32 BulkChunkMaximumBytes
    = BulkMaximumPayloadBytes - BulkChunkHeaderBytes;
inline constexpr quint32 PackageObjectKind = 4;
inline constexpr quint32 PackageUploadMode = 1;

enum class Role : quint32 {
    Control = 1,
    Push = 2,
    Bulk = 3,
};

enum class MessageType : quint16 {
    Hello = 0x0001,
    HelloAck = 0x0002,
    Error = 0x0003,
    AcquireControl = 0x0100,
    ReleaseControl = 0x0101,
    Start = 0x0102,
    Pause = 0x0103,
    Resume = 0x0104,
    ControlledStop = 0x0105,
    ResetFault = 0x0107,
    GetState = 0x0108,
    Heartbeat = 0x0109,
    EnterConfigurationMode = 0x010b,
    StartFreeRun = 0x010c,
    StartDc = 0x010d,
    ApplyOutputTransaction = 0x010e,
    CommandStatus = 0x0180,
    OutputTransactionResult = 0x0181,
    ControllerState = 0x0200,
    AlarmRaised = 0x0207,
    AlarmCleared = 0x0208,
    PerformanceSnapshot = 0x0209,
    PushHeartbeat = 0x020a,
    FirmwareProgress = 0x020b,
    ResumeEvents = 0x0210,
    ResumeEventsResult = 0x0280,
    BulkBegin = 0x0300,
    BulkChunk = 0x0301,
    BulkCommit = 0x0302,
    BulkAbort = 0x0303,
    BulkStatus = 0x0380,
    GetCapability = 0x0400,
    DiscoverTopology = 0x0401,
    GetPackageState = 0x0403,
    ValidatePackage = 0x0404,
    ActivatePackage = 0x0405,
    RollbackPackage = 0x0406,
    RestoreActivePackage = 0x0407,
    QueryResourceTable = 0x040b,
    GetResourceSnapshot = 0x040c,
    QueryOutputGroupPolicy = 0x040e,
    GetOutputTransactionState = 0x040f,
    Capability = 0x0480,
    TopologyResult = 0x0481,
    PackageState = 0x0483,
    ResourceTablePage = 0x0487,
    ResourceSnapshot = 0x0488,
    OutputGroupPolicy = 0x048a,
    OutputTransactionState = 0x048b,
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

struct TopologySlave
{
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint16 alState = 0;
    quint16 flags = 0;
    quint32 vendorId = 0;
    quint32 productCode = 0;
    quint32 revision = 0;
    quint32 serial = 0;
};

struct TopologyResult
{
    qint32 result = 0;
    quint16 respondingCount = 0;
    quint32 combinedAlState = 0;
    QList<TopologySlave> slaves;
};

enum class RuntimeResourcePrimitive : quint8 {
    Boolean = 1,
    Unsigned8 = 2,
    Signed8 = 3,
    Unsigned16 = 4,
    Signed16 = 5,
    Unsigned32 = 6,
    Signed32 = 7,
    Unsigned64 = 8,
    Signed64 = 9,
    FixedQ32_32 = 10,
    RawBits = 11,
};

enum class RuntimeResourceDirection : quint8 {
    Input = 1,
    Output = 2,
};

enum class RuntimeResourceAccess : quint8 {
    Read = 1,
    ReadWrite = 3,
};

enum class RuntimeResourceQuality : quint32 {
    Good = 0x03,
    Unavailable = 0x08,
};

struct RuntimeResourceBinding
{
    quint64 bootId = 0;
    quint32 activeSlot = 0;
    quint64 packageGeneration = 0;
    quint64 configurationId = 0;
    quint64 topologyGeneration = 0;
    quint64 runtimeGeneration = 0;
    quint64 catalogRevision = 0;
    quint64 topologyIdentity = 0;
};

struct RuntimeResourceTableQuery
{
    RuntimeResourceBinding binding;
    quint16 limit = 64;
    quint16 flags = 1;
    quint32 cursor = 0;
};

struct RuntimeResourceDescriptor
{
    quint64 resourceId = 0;
    quint64 componentId = 0;
    quint64 parentId = 0;
    quint32 ordinal = 0;
    quint32 processImageBitOffset = 0;
    quint16 processImageBitLength = 0;
    quint16 valueBitWidth = 0;
    RuntimeResourcePrimitive primitive = RuntimeResourcePrimitive::Boolean;
    RuntimeResourceDirection direction = RuntimeResourceDirection::Input;
    RuntimeResourceAccess access = RuntimeResourceAccess::Read;
    quint8 valueBytes = 0;
    quint16 qualityMask = 0;
    quint32 groupId = 0;
    QByteArray safeValue;
};

struct RuntimeResourceTablePage
{
    qint32 status = 0;
    RuntimeResourceBinding binding;
    quint16 recordCount = 0;
    quint32 totalCount = 0;
    quint32 nextCursor = 0;
    quint32 pageFlags = 0;
    bool more = false;
    QList<RuntimeResourceDescriptor> resources;
};

struct RuntimeResourceSnapshotQuery
{
    RuntimeResourceBinding binding;
    QList<quint64> resourceIds;
};

struct RuntimeResourceSample
{
    quint64 resourceId = 0;
    RuntimeResourcePrimitive primitive = RuntimeResourcePrimitive::Boolean;
    RuntimeResourceDirection direction = RuntimeResourceDirection::Input;
    quint16 bitWidth = 0;
    RuntimeResourceQuality quality = RuntimeResourceQuality::Unavailable;
    QByteArray value;
};

struct RuntimeResourceSnapshot
{
    qint32 status = 0;
    RuntimeResourceBinding binding;
    quint64 snapshotSequence = 0;
    quint64 captureCycle = 0;
    quint64 controllerTimestampNs = 0;
    bool complete = false;
    QList<RuntimeResourceSample> samples;
};

enum class OutputTransactionApiStatus : qint32 {
    Ok = 0,
    OperationConflict = -36,
    StaleOutput = -37,
    OutputGroupInvalid = -38,
    OutputValueInvalid = -39,
    OutputTtlInvalid = -40,
    OutputPolicyUnavailable = -41,
};

enum class OutputTransactionOperationResult : qint32 {
    Ok = 0,
    Argument = -1,
    State = -2,
    Epoch = -3,
    Group = -4,
    Value = -5,
    Ttl = -6,
    Conflict = -7,
    StaleOutput = -8,
    Abi = -9,
    Policy = -10,
};

enum class OutputGroupPolicyFlag : quint32 {
    ManualWrite = 1U << 0,
};

enum class OutputTransactionState : quint16 {
    Idle = 0,
    OverrideActive = 1,
    SafeHold = 2,
};

enum class OutputRecoveryPolicy : quint32 {
    ReturnTask = 1,
    HoldSafe = 2,
};

enum class OutputTransactionResultFlag : quint32 {
    Replayed = 1U << 0,
    OverrideActive = 1U << 1,
    SafeHold = 1U << 2,
    ReturnedTask = 1U << 3,
};

constexpr quint32 outputTransactionResultFlagValue(OutputTransactionResultFlag flag)
{
    return static_cast<quint32>(flag);
}

struct OutputGroupPolicyQuery
{
    RuntimeResourceBinding binding;
    quint32 consistencyGroupId = 0;
    QByteArray semanticMappingSha256;
};

struct OutputGroupPolicy
{
    qint32 status = 0;
    RuntimeResourceBinding binding;
    quint32 policyFlags = 0;
    quint32 consistencyGroupId = 0;
    OutputRecoveryPolicy recoveryPolicy = OutputRecoveryPolicy::ReturnTask;
    quint32 maximumTtlCycles = 0;
    quint32 completeResourceCount = 0;
    quint64 currentOutputGeneration = 0;
    QByteArray completeGroupRecordSha256;
    QByteArray semanticMappingSha256;
};

struct OutputTransactionStateQuery
{
    RuntimeResourceBinding binding;
    QByteArray semanticMappingSha256;
};

struct OutputTransactionValue
{
    quint64 resourceId = 0;
    RuntimeResourcePrimitive primitive = RuntimeResourcePrimitive::Boolean;
    quint16 bitWidth = 0;
    QByteArray value;
};

struct OutputTransactionRequest
{
    RuntimeResourceBinding binding;
    QByteArray operationId;
    quint64 expectedCurrentOutputGeneration = 0;
    quint32 ttlCycles = 0;
    quint32 consistencyGroupId = 0;
    QByteArray semanticMappingSha256;
    QList<OutputTransactionValue> values;
};

struct OutputTransactionRecord
{
    quint16 originalType = 0;
    qint32 status = 0;
    qint32 operationResult = 0;
    quint8 stage = 0;
    bool final = false;
    OutputTransactionState state = OutputTransactionState::Idle;
    quint32 resultFlags = 0;
    RuntimeResourceBinding binding;
    QByteArray operationId;
    quint64 appliedCycle = 0;
    quint64 expiryCycle = 0;
    quint64 outputGeneration = 0;
    quint32 consistencyGroupId = 0;
    quint32 ttlCycles = 0;
    quint32 recoveryPolicy = 0;
    quint16 valueCount = 0;
    QByteArray semanticMappingSha256;
    quint64 detail = 0;
    quint64 controllerTimestampNs = 0;
};

quint32 maximumPayloadBytes(Role role);
bool isReadOnlyRequest(MessageType type);
bool isPackageDeploymentRequest(MessageType type);
bool isSupportedRequest(MessageType type);
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
QByteArray encodeQueryResourceTable(
    const RuntimeResourceTableQuery &query,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);
QByteArray encodeGetResourceSnapshot(
    const RuntimeResourceSnapshotQuery &query,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);
QByteArray encodeQueryOutputGroupPolicy(
    const OutputGroupPolicyQuery &query,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);
QByteArray encodeGetOutputTransactionState(
    const OutputTransactionStateQuery &query,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
    quint16 protocolMinor = CurrentMinor,
    Error *error = nullptr);
QByteArray encodeApplyOutputTransaction(
    const OutputTransactionRequest &request,
    quint64 sessionId,
    quint64 requestId,
    quint64 sequence,
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
std::optional<Data::ControllerAlarmSummary> decodeAlarmEvent(
    const Frame &frame, Error *error = nullptr);
std::optional<Data::ControllerPerformanceSummary> decodePerformanceSnapshot(
    const Frame &frame, Error *error = nullptr);
std::optional<Data::ControllerCapabilitySummary> decodeCapability(
    const Frame &frame, quint32 featureBits, Error *error = nullptr);
std::optional<Data::ControllerPackageSummary> decodePackageState(
    const Frame &frame, Error *error = nullptr);
std::optional<TopologyResult> decodeTopologyResult(
    const Frame &frame, Error *error = nullptr);
std::optional<RuntimeResourceTablePage> decodeResourceTablePage(
    const Frame &frame, const RuntimeResourceTableQuery &query, Error *error = nullptr);
std::optional<RuntimeResourceSnapshot> decodeResourceSnapshot(
    const Frame &frame, const RuntimeResourceSnapshotQuery &query, Error *error = nullptr);
std::optional<OutputGroupPolicy> decodeOutputGroupPolicy(
    const Frame &frame, const OutputGroupPolicyQuery &query, Error *error = nullptr);
std::optional<OutputTransactionRecord> decodeOutputTransactionState(
    const Frame &frame, const OutputTransactionStateQuery &query, Error *error = nullptr);
std::optional<OutputTransactionRecord> decodeOutputTransactionResult(
    const Frame &frame, const OutputTransactionRequest &request, Error *error = nullptr);
std::optional<Data::ControllerFirmwareSummary> decodeFirmwareState(
    const Frame &frame, Error *error = nullptr);
std::optional<ResumeEventsResult> decodeResumeEventsResult(
    const Frame &frame, Error *error = nullptr);
std::optional<PushHeartbeat> decodePushHeartbeat(const Frame &frame, Error *error = nullptr);

} // namespace EtherCAT::ProductApi::Internal::Protocol
