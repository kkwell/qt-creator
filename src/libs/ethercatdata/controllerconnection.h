// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Data {

enum class ControllerConnectionState {
    Disconnected,
    Connecting,
    Handshaking,
    Connected,
    Degraded,
    Disconnecting,
    Failed,
};

enum class ControllerChannelState {
    Disconnected,
    Connecting,
    Handshaking,
    Connected,
    Failed,
};

enum class ControllerServiceState {
    Unknown,
    Boot,
    Configuring,
    SafeOperational,
    OperationalSafe,
    Running,
    Stopping,
    Fault,
    Recovering,
    Shutdown,
    Paused,
};

enum class ControllerSeverity { None, Information, Warning, Error, Fatal };

enum class ControllerSlot { None, A, B };

enum class ControllerPackageState {
    Unavailable,
    Empty,
    Staged,
    Accepted,
    Active,
    Rejected,
};

enum class ControllerFirmwareState {
    Unknown,
    Idle,
    Receiving,
    Staged,
    Verified,
    Installing,
    ReadyToReboot,
    TrialBoot,
    Confirmed,
    Rejected,
    RolledBack,
};

enum class ControllerErrorSource {
    ClientConfiguration,
    Network,
    Protocol,
    Controller,
    EtherCATBus,
    DeviceDescription,
    Unknown,
};

enum class ControllerOperation {
    None,
    Connect,
    Handshake,
    QueryState,
    QueryCapability,
    QueryPackageState,
    QueryFirmwareState,
    SubscribeEvents,
    Refresh,
    Disconnect,
    AcquireControl,
    ReleaseControl,
    Heartbeat,
    EnterConfigurationMode,
    DiscoverTopology,
    RestoreActivePackage,
    Start,
    StartFreeRun,
    StartDistributedClocks,
    Pause,
    Resume,
    ControlledStop,
};

enum class ControllerControlCommand {
    None,
    AcquireControl,
    ReleaseControl,
    EnterConfigurationMode,
    DiscoverTopology,
    RestoreActivePackage,
    Start,
    StartFreeRun,
    StartDistributedClocks,
    Pause,
    Resume,
    ControlledStop,
};

enum class ControllerControlState {
    Idle,
    Pending,
    Succeeded,
    Failed,
};

enum class ControllerRetryDisposition { Unknown, Retryable, Reconnect, NotRetryable };

struct ETHERCATDATA_EXPORT ControllerControlRequest
{
    ControllerControlCommand command = ControllerControlCommand::None;
    int leaseDurationMs = 30000;
    quint16 firstStationAddress = 0x1001;
    quint16 topologyCapacity = 64;

    friend bool operator==(const ControllerControlRequest &,
                           const ControllerControlRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerControlProgress
{
    ControllerControlCommand command = ControllerControlCommand::None;
    ControllerControlState state = ControllerControlState::Idle;
    quint16 stage = 0;
    bool final = false;
    std::optional<qint32> status;
    std::optional<qint32> operationResult;
    QString detail;
    QDateTime startedAt;
    QDateTime completedAt;

    friend bool operator==(const ControllerControlProgress &,
                           const ControllerControlProgress &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerTopologySlave
{
    quint32 position = 0;
    quint16 stationAddress = 0;
    quint16 alState = 0;
    quint32 flags = 0;
    quint32 vendorId = 0;
    quint32 productCode = 0;
    quint32 revision = 0;
    quint32 serial = 0;

    friend bool operator==(const ControllerTopologySlave &,
                           const ControllerTopologySlave &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerTopologySnapshot
{
    quint16 firstStationAddress = 0;
    quint32 respondingCount = 0;
    qint32 result = 0;
    QList<ControllerTopologySlave> slaves;
    QDateTime discoveredAt;

    friend bool operator==(const ControllerTopologySnapshot &,
                           const ControllerTopologySnapshot &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionScope
{
    NodeId projectId;
    NodeId masterId;

    friend bool operator==(const ControllerConnectionScope &, const ControllerConnectionScope &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionProfile
{
    NodeId id;
    QString displayName;
    QString endpointSummary;
    bool configured = false;
    bool supported = false;
    bool defaultProfile = false;
    QString configurationIssue;

    friend bool operator==(const ControllerConnectionProfile &, const ControllerConnectionProfile &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionProfileConfiguration
{
    NodeId profileId;
    QString endpoint;
    QString placeholder;
    bool editable = false;

    friend bool operator==(
        const ControllerConnectionProfileConfiguration &,
        const ControllerConnectionProfileConfiguration &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionRequest
{
    ControllerConnectionScope scope;
    NodeId profileId;

    friend bool operator==(const ControllerConnectionRequest &, const ControllerConnectionRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerProtocolVersion
{
    quint16 major = 0;
    quint16 minor = 0;

    friend bool operator==(const ControllerProtocolVersion &, const ControllerProtocolVersion &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerChannelStatus
{
    QString id;
    QString displayName;
    ControllerChannelState state = ControllerChannelState::Disconnected;
    int maximumPayloadBytes = 0;
    QDateTime lastActivityAt;
    QString detail;

    friend bool operator==(const ControllerChannelStatus &, const ControllerChannelStatus &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerSessionSummary
{
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 controlLeaseOwnerSessionId = 0;
    int defaultControlLeaseDurationMs = 0;
    bool ownsControlLease = false;

    friend bool operator==(const ControllerSessionSummary &, const ControllerSessionSummary &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerStateSummary
{
    ControllerServiceState serviceState = ControllerServiceState::Unknown;
    ControllerSeverity severity = ControllerSeverity::None;
    bool ready = false;
    bool busOperational = false;
    bool applicationActive = false;
    bool safeOutput = false;
    bool fault = false;
    bool recovering = false;
    bool distributedClocksLocked = false;
    bool commandStale = false;
    bool paused = false;
    qint32 latestCommandResult = 0;
    quint64 currentFaults = 0;
    quint64 latchedFaults = 0;
    quint64 controllerHeartbeat = 0;
    quint64 cycleCount = 0;
    quint64 controllerBootId = 0;
    quint32 ethercatAlStateBits = 0;
    quint32 expectedWorkingCounter = 0;
    quint32 actualWorkingCounter = 0;
    quint32 distributedClockDifferenceNs = 0;
    quint32 latestAlarmSequence = 0;

    friend bool operator==(const ControllerStateSummary &, const ControllerStateSummary &) = default;
};

struct ETHERCATDATA_EXPORT ControllerCapabilitySummary
{
    int maximumSlaves = 0;
    qint64 minimumCycleTimeNs = 0;
    int maximumCyclicFrames = 0;
    int maximumProcessInputBytes = 0;
    int maximumProcessOutputBytes = 0;
    QByteArray descriptorSha256;
    bool controlLease = false;
    bool resumablePush = false;
    bool transactionalBulk = false;
    bool capabilityQuery = false;
    bool sdoFailureDiagnostics = false;
    bool exactAlarmReplay = false;
    bool linkDiagnostics = false;
    bool timeCorrelation = false;
    bool processInputSample = false;
    bool structuredHandshakeError = false;
    bool firmwareUpdate = false;
    bool explicitTimingModeStart = false;
    bool coe = false;
    bool distributedClocks = false;
    bool multiSlaveDistributedClocks = false;
    bool multiFrame = false;
    bool runtimeProgram = false;

    friend bool operator==(const ControllerCapabilitySummary &, const ControllerCapabilitySummary &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerPackageSummary
{
    ControllerSlot stagedSlot = ControllerSlot::None;
    quint64 stagedGeneration = 0;
    quint64 stagedConfigurationId = 0;
    ControllerSlot activeSlot = ControllerSlot::None;
    quint64 activeGeneration = 0;
    quint64 activeConfigurationId = 0;
    ControllerPackageState controllerState = ControllerPackageState::Unavailable;
    qint32 controllerResult = 0;
    quint32 controllerRequestSequence = 0;
    quint64 controllerBootId = 0;

    friend bool operator==(const ControllerPackageSummary &, const ControllerPackageSummary &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerFirmwareSummary
{
    ControllerFirmwareState state = ControllerFirmwareState::Unknown;
    ControllerSlot activeSlot = ControllerSlot::None;
    ControllerSlot previousSlot = ControllerSlot::None;
    ControllerSlot targetSlot = ControllerSlot::None;
    bool packageVerified = false;
    bool rebootRequired = false;
    bool confirmed = false;
    bool rolledBack = false;
    int progressPerMille = 0;
    int failureStage = 0;
    qint32 lastFailure = 0;
    qint32 lastSystemError = 0;
    quint64 generation = 0;
    QDateTime updatedAt;

    friend bool operator==(const ControllerFirmwareSummary &, const ControllerFirmwareSummary &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerOperationError
{
    ControllerErrorSource source = ControllerErrorSource::Unknown;
    QString channelId;
    ControllerOperation operation = ControllerOperation::None;
    std::optional<qint32> code;
    std::optional<qint32> operationResult;
    std::optional<quint64> sourceDetail;
    QString codeName;
    std::optional<quint64> requestId;
    std::optional<int> retryAfterMs;
    QDateTime occurredAt;
    ControllerRetryDisposition retryDisposition = ControllerRetryDisposition::Unknown;
    QString summary;
    QString detail;

    friend bool operator==(const ControllerOperationError &, const ControllerOperationError &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionSnapshot
{
    ControllerConnectionScope scope;
    NodeId profileId;
    QString endpointSummary;
    ControllerConnectionState state = ControllerConnectionState::Disconnected;
    QList<ControllerChannelStatus> channels;
    ControllerProtocolVersion protocolVersion;
    quint64 sessionGeneration = 0;
    QDateTime connectedAt;
    QDateTime updatedAt;
    QDateTime lastHeartbeatAt;
    bool readOnly = true;
    bool mock = false;
    std::optional<ControllerSessionSummary> session;
    std::optional<ControllerStateSummary> controllerState;
    std::optional<ControllerCapabilitySummary> capability;
    std::optional<ControllerPackageSummary> package;
    std::optional<ControllerFirmwareSummary> firmware;
    std::optional<ControllerOperationError> lastError;
    ControllerControlProgress controlProgress;
    std::optional<ControllerTopologySnapshot> topology;

    friend bool operator==(const ControllerConnectionSnapshot &, const ControllerConnectionSnapshot &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerChannelState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerServiceState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSlot)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerFirmwareState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerErrorSource)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerOperation)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlCommand)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerRetryDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlProgress)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerTopologySlave)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerTopologySnapshot)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionScope)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionProfile)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionProfileConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerProtocolVersion)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerChannelStatus)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSessionSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerStateSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerCapabilitySummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerFirmwareSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerOperationError)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionSnapshot)
