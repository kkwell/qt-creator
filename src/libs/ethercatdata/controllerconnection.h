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

enum class ControllerAlarmState { Raised, Cleared };

enum class ControllerAlarmSource {
    Unknown,
    Service,
    Transport,
    Protocol,
    DistributedClocks,
    Application,
};

// Providers translate native controller diagnostics into these shared categories.
enum class ControllerFault : quint64 {
    Configuration = quint64(1) << 0,
    LinkTimeout = quint64(1) << 1,
    ReceiveDrop = quint64(1) << 2,
    ReceiveOverflow = quint64(1) << 3,
    TransmitUnavailable = quint64(1) << 4,
    WorkingCounter = quint64(1) << 5,
    Protocol = quint64(1) << 6,
    CycleLate = quint64(1) << 7,
    AlStatus = quint64(1) << 8,
    Mailbox = quint64(1) << 9,
    Sdo = quint64(1) << 10,
    DistributedClocksConfiguration = quint64(1) << 11,
    DistributedClocksDrift = quint64(1) << 12,
    Command = quint64(1) << 13,
    Watchdog = quint64(1) << 14,
    Internal = quint64(1) << 15,
    NetworkQuickStop = quint64(1) << 16,
};

inline constexpr int ControllerFaultCount = 17;
inline constexpr quint64 ControllerFaultKnownMask
    = (quint64(1) << ControllerFaultCount) - 1;

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
    QueryRuntimeResourceCatalog,
    QueryRuntimeResourceSnapshot,
    QueryRuntimeSemanticMappingAttestation,
    QueryRuntimeOutputGroupPolicy,
    QueryRuntimeOutputTransactionState,
    ApplyRuntimeOutputTransaction,
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
    ResetFault,
    UploadPackage,
    AbortPackageUpload,
    ValidatePackage,
    ActivatePackage,
    RollbackPackage,
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
    ResetFault,
};

enum class ControllerControlState {
    Idle,
    Pending,
    Succeeded,
    Failed,
};

enum class ControllerPackageDeploymentState {
    Idle,
    Uploading,
    Committing,
    Validating,
    Activating,
    RollingBack,
    Canceling,
    Succeeded,
    Canceled,
    Failed,
    OutcomeUnknown,
};

enum class ControllerRetryDisposition { Unknown, Retryable, Reconnect, NotRetryable };

struct ETHERCATDATA_EXPORT ControllerControlRequest
{
    ControllerControlCommand command = ControllerControlCommand::None;
    int leaseDurationMs = 30000;
    quint16 firstStationAddress = 0x1001;
    quint16 topologyCapacity = 64;
    quint64 expectedLatchedFaults = 0;
    quint32 expectedAlarmSequence = 0;

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
    quint64 expectedLatchedFaults = 0;
    quint32 expectedAlarmSequence = 0;
    QString detail;
    QDateTime startedAt;
    QDateTime completedAt;

    friend bool operator==(const ControllerControlProgress &,
                           const ControllerControlProgress &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerPackageSelector
{
    ControllerSlot slot = ControllerSlot::None;
    quint64 generation = 0;
    quint64 configurationId = 0;

    bool isValid() const
    {
        return (slot == ControllerSlot::A || slot == ControllerSlot::B) && generation
               && configurationId;
    }

    friend bool operator==(const ControllerPackageSelector &, const ControllerPackageSelector &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerPackageDeploymentRequest
{
    QString operationId;
    QByteArray artifact;
    // Reserved for IDE-local evidence verification. Providers must neither
    // serialize these bytes nor include them in controller-wire identities.
    QByteArray compiledProjectSource;
    quint64 configurationId = 0;
    bool activate = true;
    bool rollbackOnActivationFailure = true;

    friend bool operator==(
        const ControllerPackageDeploymentRequest &, const ControllerPackageDeploymentRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerPackageDeploymentAuditEvent
{
    quint64 sequence = 0;
    ControllerOperation operation = ControllerOperation::None;
    std::optional<quint64> requestId;
    std::optional<qint32> status;
    std::optional<qint32> operationResult;
    QString detail;
    QDateTime occurredAt;

    friend bool operator==(
        const ControllerPackageDeploymentAuditEvent &, const ControllerPackageDeploymentAuditEvent &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerPackageDeploymentProgress
{
    QString operationId;
    QByteArray artifactSha256;
    ControllerPackageDeploymentState state = ControllerPackageDeploymentState::Idle;
    qint64 totalBytes = 0;
    qint64 transferredBytes = 0;
    std::optional<ControllerPackageSelector> candidate;
    std::optional<ControllerPackageSelector> previousActive;
    std::optional<qint32> status;
    std::optional<qint32> operationResult;
    QString detail;
    QList<ControllerPackageDeploymentAuditEvent> audit;
    QDateTime startedAt;
    QDateTime completedAt;

    friend bool operator==(
        const ControllerPackageDeploymentProgress &, const ControllerPackageDeploymentProgress &)
        = default;
};

struct ETHERCATDATA_EXPORT ControllerConnectionScope
{
    NodeId projectId;
    NodeId masterId;

    friend bool operator==(const ControllerConnectionScope &, const ControllerConnectionScope &)
        = default;
};

enum class ControllerTopologyEvidenceValidity {
    Unknown,
    Valid,
    Unavailable,
};

enum class ControllerTopologyEvidenceProvenance {
    None,
    Observed,
    DeviceReported,
    ProjectSelected,
    EsiDerived,
};

enum class ControllerTopologyEvidenceSource {
    None,
    EscStationAlias,
    SiiMailbox,
    CoeDetectedModules,
};

struct ETHERCATDATA_EXPORT ControllerTopologyModuleEvidence
{
    quint16 slot = 0;
    quint32 moduleIdent = 0;
    ControllerTopologyEvidenceValidity validity
        = ControllerTopologyEvidenceValidity::Unknown;
    ControllerTopologyEvidenceProvenance provenance
        = ControllerTopologyEvidenceProvenance::None;
    ControllerTopologyEvidenceSource source = ControllerTopologyEvidenceSource::None;

    friend bool operator==(
        const ControllerTopologyModuleEvidence &, const ControllerTopologyModuleEvidence &)
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
    quint16 alias = 0;
    ControllerTopologyEvidenceValidity aliasValidity
        = ControllerTopologyEvidenceValidity::Unknown;
    ControllerTopologyEvidenceProvenance aliasProvenance
        = ControllerTopologyEvidenceProvenance::None;
    ControllerTopologyEvidenceSource aliasSource = ControllerTopologyEvidenceSource::None;
    ControllerTopologyEvidenceValidity moduleValidity
        = ControllerTopologyEvidenceValidity::Unknown;
    ControllerTopologyEvidenceProvenance moduleProvenance
        = ControllerTopologyEvidenceProvenance::None;
    ControllerTopologyEvidenceSource moduleSource = ControllerTopologyEvidenceSource::None;
    QList<ControllerTopologyModuleEvidence> modules;

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
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 requestId = 0;
    // This is the Product API response frame sequence, not a controller-side
    // topology capture sequence.
    quint64 responseSequence = 0;
    // This is the CPU1 request_sequence carried by the TopologyResult payload,
    // not the Product API response frame sequence.
    quint32 cpu1RequestSequence = 0;
    // Product API v1.15 topology-evidence capture sequence. It is independent
    // of the ECAP response frame sequence and the legacy CPU1 request sequence.
    quint32 topologyCaptureSequence = 0;
    // This is the ECAP response send timestamp, not a topology capture time.
    // Preserve zero because the current wire contract does not forbid it.
    quint64 controllerTimestampNs = 0;
    // This is the CPU1 completed_time_ns carried by the TopologyResult payload,
    // not the ECAP response send timestamp.
    quint64 cpu1CompletedTimeNs = 0;
    // Product API v1.15 topology-evidence completion time.
    quint64 topologyCompletedTimeNs = 0;
    QDateTime receivedAt;

    bool hasCompleteProvenance() const
    {
        return !scope.projectId.isNull() && !scope.masterId.isNull() && sessionGeneration
               && sessionId && bootId && requestId && responseSequence
               && ((cpu1RequestSequence && cpu1CompletedTimeNs)
                   || (topologyCaptureSequence && topologyCompletedTimeNs))
               && receivedAt.isValid();
    }

    friend bool operator==(const ControllerTopologySnapshot &,
                           const ControllerTopologySnapshot &)
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
    QString endpointLabel;
    QString endpointAccessibleName;
    QString endpointDescription;

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

struct ETHERCATDATA_EXPORT ControllerAlarmSummary
{
    quint32 sequence = 0;
    quint32 code = 0;
    QString codeName;
    ControllerAlarmState state = ControllerAlarmState::Raised;
    ControllerSeverity severity = ControllerSeverity::None;
    ControllerAlarmSource source = ControllerAlarmSource::Unknown;
    bool latched = false;
    quint32 detail0 = 0;
    quint32 detail1 = 0;
    quint32 detail2 = 0;
    quint64 controllerTimestampNs = 0;
    quint64 cycleCount = 0;
    quint64 faultMask = 0;
    QString detail;

    friend bool operator==(const ControllerAlarmSummary &, const ControllerAlarmSummary &) = default;
};

struct ETHERCATDATA_EXPORT ControllerPerformanceSummary
{
    quint64 minimumExchangeTimeNs = 0;
    quint64 maximumExchangeTimeNs = 0;
    quint64 maximumSubmitLatenessNs = 0;
    quint32 timeoutCount = 0;
    quint32 receiveDropCount = 0;
    quint32 receiveOverflowCount = 0;
    quint32 transmitUnavailableCount = 0;
    quint32 cycleLateCount = 0;
    quint32 badWorkingCounterCount = 0;
    quint32 protocolErrorCount = 0;
    quint32 staleReceiveCount = 0;
    quint32 handoffSkippedCycleCount = 0;
    quint32 fpgaLatencyStatusFlags = 0;
    quint32 fpgaSnapshotSequence = 0;
    bool processInputSampleValid = false;
    bool processInputSampleFresh = false;
    bool processInputSampleComplete = false;
    quint32 processInputSampleOffset = 0;
    quint32 processInputSampleTotalBytes = 0;
    quint32 processInputSampleSequence = 0;
    quint32 processInputSampleAgeCycles = 0;
    quint64 processInputSampleGeneration = 0;
    quint64 processInputSampleConfigurationId = 0;
    quint64 processInputSampleCycleCount = 0;
    QByteArray processInputSample;

    friend bool operator==(
        const ControllerPerformanceSummary &, const ControllerPerformanceSummary &)
        = default;
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
    bool faultReset = false;
    bool runtimeResources = false;
    bool semanticMappingAttestation = false;
    bool runtimeOutputTransactions = false;
    bool topologyEvidence = false;
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
    QList<ControllerAlarmSummary> recentAlarms;
    std::optional<ControllerPerformanceSummary> performance;
    std::optional<ControllerCapabilitySummary> capability;
    std::optional<ControllerPackageSummary> package;
    std::optional<ControllerFirmwareSummary> firmware;
    std::optional<ControllerOperationError> lastError;
    ControllerControlProgress controlProgress;
    ControllerPackageDeploymentProgress packageDeploymentProgress;
    std::optional<ControllerTopologySnapshot> topology;

    friend bool operator==(const ControllerConnectionSnapshot &, const ControllerConnectionSnapshot &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerChannelState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerServiceState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerAlarmState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerAlarmSource)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerSlot)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerFirmwareState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerErrorSource)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerOperation)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlCommand)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageDeploymentState)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerRetryDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerControlProgress)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageSelector)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageDeploymentRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageDeploymentAuditEvent)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageDeploymentProgress)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerTopologyEvidenceValidity)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerTopologyEvidenceProvenance)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerTopologyEvidenceSource)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerTopologyModuleEvidence)
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
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerAlarmSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPerformanceSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerCapabilitySummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerPackageSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerFirmwareSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerOperationError)
Q_DECLARE_METATYPE(EtherCAT::Data::ControllerConnectionSnapshot)
