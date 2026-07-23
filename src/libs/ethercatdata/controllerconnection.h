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

enum class ControllerChannel { Unknown, Control, Push, Bulk };

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
};

enum class ControllerRetryDisposition { Unknown, Retryable, Reconnect, NotRetryable };

struct ETHERCATDATA_EXPORT ControllerEndpoint
{
    QString host;
    quint16 controlPort = 0;
    quint16 pushPort = 0;
    quint16 bulkPort = 0;

    friend bool operator==(const ControllerEndpoint &, const ControllerEndpoint &) = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionRequest
{
    NodeId projectId;
    NodeId masterId;
    ControllerEndpoint endpoint;

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
    ControllerChannel channel = ControllerChannel::Unknown;
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
    ControllerChannel channel = ControllerChannel::Unknown;
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
    NodeId projectId;
    NodeId masterId;
    ControllerEndpoint endpoint;
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

    friend bool operator==(const ControllerConnectionSnapshot &, const ControllerConnectionSnapshot &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerChannel)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerChannelState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerServiceState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSlot)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerFirmwareState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerErrorSource)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerOperation)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerRetryDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerEndpoint)
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
