// Copyright (C) 2026 Kvell

#include "productapisession.h"

#include "ethercatproductapiconstants.h"
#include "ethercatproductapitr.h"
#include "productapicodec.h"

#include <QDateTime>
#include <QHash>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

#include <QtEndian>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace EtherCAT::ProductApi::Internal {

namespace {

constexpr quint32 FeatureControlLease = 1U << 0;
constexpr quint32 FeatureResumablePush = 1U << 1;
constexpr quint32 FeatureTransactionalBulk = 1U << 2;
constexpr quint32 FeatureCapabilityQuery = 1U << 3;
constexpr quint32 FeatureSdoFailureDiagnostic = 1U << 4;
constexpr quint32 FeatureExactAlarmReplay = 1U << 5;
constexpr quint32 FeatureLinkDiagnostics = 1U << 6;
constexpr quint32 FeatureTimeCorrelation = 1U << 7;
constexpr quint32 FeatureProcessInputSample = 1U << 8;
constexpr quint32 FeatureStructuredHelloError = 1U << 9;
constexpr quint32 FeatureFirmwareUpdate = 1U << 10;
constexpr quint32 FeatureExplicitTimingModeStart = 1U << 11;
constexpr int SessionCapacityStatus = -26;
constexpr int EventGapStatus = -25;
constexpr int AlarmEventBytes = 64;

QString channelId(Protocol::Role role)
{
    switch (role) {
    case Protocol::Role::Control:
        return QStringLiteral("control");
    case Protocol::Role::Push:
        return QStringLiteral("push");
    case Protocol::Role::Bulk:
        return QStringLiteral("bulk");
    }
    return {};
}

QString channelDisplayName(Protocol::Role role)
{
    switch (role) {
    case Protocol::Role::Control:
        return Tr::tr("Control");
    case Protocol::Role::Push:
        return Tr::tr("Push");
    case Protocol::Role::Bulk:
        return Tr::tr("Bulk");
    }
    return {};
}

int roleMaximumPayloadBytes(Protocol::Role role)
{
    return role == Protocol::Role::Control ? 4096 : 65536;
}

quint32 requiredFeatureMask(quint16 minor)
{
    quint32 mask = FeatureControlLease | FeatureResumablePush | FeatureTransactionalBulk
                   | FeatureCapabilityQuery;
    if (minor >= 3)
        mask |= FeatureSdoFailureDiagnostic;
    if (minor >= 4)
        mask |= FeatureExactAlarmReplay;
    if (minor >= 5)
        mask |= FeatureLinkDiagnostics;
    if (minor >= 6)
        mask |= FeatureTimeCorrelation;
    if (minor >= 7)
        mask |= FeatureProcessInputSample;
    if (minor >= 8)
        mask |= FeatureStructuredHelloError;
    if (minor >= 9)
        mask |= FeatureFirmwareUpdate;
    if (minor >= Protocol::ExplicitTimingModeMinor)
        mask |= FeatureExplicitTimingModeStart;
    return mask;
}

quint32 readU32(QByteArrayView bytes, qsizetype offset)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.data() + offset));
}

QString statusName(qint32 status)
{
    static constexpr std::array<const char *, 36> names{
        "OK",
        "BAD_MAGIC",
        "BAD_VERSION",
        "BAD_HEADER",
        "TOO_LARGE",
        "BAD_CRC",
        "BAD_MESSAGE",
        "BAD_SESSION",
        "STALE_BOOT",
        "LEASE_REQUIRED",
        "LEASE_BUSY",
        "LEASE_EXPIRED",
        "BAD_SEQUENCE",
        "BACKPRESSURE",
        "UNSUPPORTED",
        "CPU1_REJECTED",
        "INTERNAL",
        "STALE_PACKAGE",
        "PACKAGE_NOT_READY",
        "ACTIVATION_FAILED",
        "CAPABILITY_MISMATCH",
        "ECPKG_INVALID",
        "PACKAGE_UNTRUSTED",
        "ENGINEERING_DISABLED",
        "LEGACY_PACKAGE_DISABLED",
        "EVENT_GAP",
        "SESSION_CAPACITY",
        "FIRMWARE_BUSY",
        "FIRMWARE_INVALID",
        "FIRMWARE_UNTRUSTED",
        "FIRMWARE_ROLLBACK_REJECTED",
        "FIRMWARE_NOT_READY",
        "FIRMWARE_UNSAFE_STATE",
        "FIRMWARE_STORAGE",
        "FIRMWARE_FIT_INVALID",
        "TIMING_MODE_MISMATCH",
    };
    if (status <= 0 && status >= -35)
        return QString::fromLatin1(names.at(size_t(-status)));
    return QStringLiteral("PRODUCT_API_STATUS_%1").arg(status);
}

QString timingModeName(quint32 mode)
{
    if (mode == 1)
        return QStringLiteral("FreeRun");
    if (mode == 2)
        return QStringLiteral("DC");
    return QString::number(mode);
}

Data::ControllerRetryDisposition retryDisposition(qint32 status)
{
    switch (status) {
    case -2:
    case -7:
    case -8:
    case -12:
    case -16:
        return Data::ControllerRetryDisposition::Reconnect;
    case -10:
    case -13:
    case -18:
    case EventGapStatus:
    case SessionCapacityStatus:
    case -27:
        return Data::ControllerRetryDisposition::Retryable;
    default:
        return Data::ControllerRetryDisposition::NotRetryable;
    }
}

bool isReconnectStatus(qint32 status)
{
    return retryDisposition(status) == Data::ControllerRetryDisposition::Reconnect;
}

bool invalidatesControlLease(qint32 status)
{
    return status == -7 || status == -8 || status == -9 || status == -11;
}

Data::ControllerOperation operationForCommand(Data::ControllerControlCommand command)
{
    using Command = Data::ControllerControlCommand;
    switch (command) {
    case Command::AcquireControl:
        return Data::ControllerOperation::AcquireControl;
    case Command::ReleaseControl:
        return Data::ControllerOperation::ReleaseControl;
    case Command::EnterConfigurationMode:
        return Data::ControllerOperation::EnterConfigurationMode;
    case Command::DiscoverTopology:
        return Data::ControllerOperation::DiscoverTopology;
    case Command::RestoreActivePackage:
        return Data::ControllerOperation::RestoreActivePackage;
    case Command::Start:
        return Data::ControllerOperation::Start;
    case Command::StartFreeRun:
        return Data::ControllerOperation::StartFreeRun;
    case Command::StartDistributedClocks:
        return Data::ControllerOperation::StartDistributedClocks;
    case Command::Pause:
        return Data::ControllerOperation::Pause;
    case Command::Resume:
        return Data::ControllerOperation::Resume;
    case Command::ControlledStop:
        return Data::ControllerOperation::ControlledStop;
    case Command::None:
        return Data::ControllerOperation::None;
    }
    return Data::ControllerOperation::None;
}

Protocol::MessageType messageTypeForCommand(Data::ControllerControlCommand command)
{
    using Command = Data::ControllerControlCommand;
    switch (command) {
    case Command::AcquireControl:
        return Protocol::MessageType::AcquireControl;
    case Command::ReleaseControl:
        return Protocol::MessageType::ReleaseControl;
    case Command::EnterConfigurationMode:
        return Protocol::MessageType::EnterConfigurationMode;
    case Command::DiscoverTopology:
        return Protocol::MessageType::DiscoverTopology;
    case Command::RestoreActivePackage:
        return Protocol::MessageType::RestoreActivePackage;
    case Command::Start:
        return Protocol::MessageType::Start;
    case Command::StartFreeRun:
        return Protocol::MessageType::StartFreeRun;
    case Command::StartDistributedClocks:
        return Protocol::MessageType::StartDc;
    case Command::Pause:
        return Protocol::MessageType::Pause;
    case Command::Resume:
        return Protocol::MessageType::Resume;
    case Command::ControlledStop:
        return Protocol::MessageType::ControlledStop;
    case Command::None:
        return Protocol::MessageType::Error;
    }
    return Protocol::MessageType::Error;
}

QString commandDisplayName(Data::ControllerControlCommand command)
{
    using Command = Data::ControllerControlCommand;
    switch (command) {
    case Command::AcquireControl:
        return Tr::tr("Acquire control");
    case Command::ReleaseControl:
        return Tr::tr("Release control");
    case Command::EnterConfigurationMode:
        return Tr::tr("Enter configuration mode");
    case Command::DiscoverTopology:
        return Tr::tr("Discover topology");
    case Command::RestoreActivePackage:
        return Tr::tr("Restore active package");
    case Command::Start:
        return Tr::tr("Start");
    case Command::StartFreeRun:
        return Tr::tr("Start free-run");
    case Command::StartDistributedClocks:
        return Tr::tr("Start distributed clocks");
    case Command::Pause:
        return Tr::tr("Pause");
    case Command::Resume:
        return Tr::tr("Resume");
    case Command::ControlledStop:
        return Tr::tr("Controlled stop");
    case Command::None:
        return Tr::tr("No operation");
    }
    return Tr::tr("Unknown operation");
}

} // namespace

class ProductApiSessionPrivate
{
public:
    enum class PendingKind {
        Hello,
        State,
        Capability,
        Package,
        Firmware,
        ResumeEvents,
        ResumeReplay,
        ControlCommand,
        Topology,
        RestorePackage,
        Heartbeat,
    };

    struct PendingRequest
    {
        PendingKind kind = PendingKind::Hello;
        Protocol::Role role = Protocol::Role::Control;
        Data::ControllerOperation operation = Data::ControllerOperation::None;
        quint64 generation = 0;
        quint64 channelEpoch = 0;
        Protocol::MessageType requestType = Protocol::MessageType::Hello;
        quint32 requestedAfterSequence = 0;
        quint32 expectedAlarmSequence = 0;
        quint32 latestAlarmSequence = 0;
        quint32 remainingReplayEvents = 0;
        Data::ControllerControlCommand controlCommand = Data::ControllerControlCommand::None;
        quint16 nextExpectedStage = 0;
        quint16 firstStationAddress = 0;
        int responseTimeoutMs = 0;
        bool terminalCommandStatus = false;
        QTimer *timer = nullptr;
    };

    struct PersistentPackageSelector
    {
        Data::ControllerSlot slot = Data::ControllerSlot::None;
        quint64 generation = 0;
        quint64 configurationId = 0;
        quint64 bootId = 0;
    };

    struct Channel
    {
        Protocol::Role role = Protocol::Role::Control;
        quint16 port = 0;
        quint64 epoch = 0;
        quint64 sendSequence = 0;
        quint64 helloRequestId = 0;
        quint64 helloResumeSessionId = 0;
        QTcpSocket *socket = nullptr;
        QTimer *phaseTimer = nullptr;
        std::unique_ptr<Protocol::FrameParser> parser;
        bool handshaken = false;
    };

    ProductApiSessionPrivate(
        ProductApiSession *q,
        const ProductApiSession::EndpointSet &endpoints,
        const ProductApiSession::Options &options)
        : q(q)
        , endpoints(endpoints)
        , options(options)
    {
        channels[0].role = Protocol::Role::Control;
        channels[0].port = endpoints.controlPort;
        channels[1].role = Protocol::Role::Push;
        channels[1].port = endpoints.pushPort;
        channels[2].role = Protocol::Role::Bulk;
        channels[2].port = endpoints.bulkPort;

        snapshot.endpointSummary = endpoints.endpointSummary;
        snapshot.readOnly = false;
        snapshot.mock = false;
        initializeChannelSnapshots();

        reconnectTimer = new QTimer(q);
        reconnectTimer->setSingleShot(true);
        reconnectTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(reconnectTimer, &QTimer::timeout, q, [this] {
            if (shuttingDown || userDisconnecting)
                return;
            openChannel(channel(Protocol::Role::Control));
        });

        heartbeatTimer = new QTimer(q);
        heartbeatTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(heartbeatTimer, &QTimer::timeout, q, [this] {
            sendHeartbeat();
        });

        nextRequestId = QRandomGenerator::system()->generate64()
                        & std::numeric_limits<qint64>::max();
        if (!nextRequestId)
            nextRequestId = 1;
    }

    ~ProductApiSessionPrivate()
    {
        shuttingDown = true;
        clearPendingRequests();
        teardownChannels();
        reconnectTimer->stop();
        heartbeatTimer->stop();
    }

    Channel &channel(Protocol::Role role)
    {
        return channels.at(static_cast<size_t>(quint32(role) - 1));
    }

    const Channel &channel(Protocol::Role role) const
    {
        return channels.at(static_cast<size_t>(quint32(role) - 1));
    }

    void initializeChannelSnapshots()
    {
        snapshot.channels.clear();
        for (const Channel &value : channels) {
            Data::ControllerChannelStatus status;
            status.id = channelId(value.role);
            status.displayName = channelDisplayName(value.role);
            status.maximumPayloadBytes = roleMaximumPayloadBytes(value.role);
            snapshot.channels.append(status);
        }
    }

    Data::ControllerChannelStatus *channelSnapshot(Protocol::Role role)
    {
        const QString id = channelId(role);
        auto found = std::find_if(
            snapshot.channels.begin(), snapshot.channels.end(), [&id](const auto &status) {
                return status.id == id;
            });
        return found == snapshot.channels.end() ? nullptr : &*found;
    }

    void setChannelState(
        Protocol::Role role, Data::ControllerChannelState state, const QString &detail = {})
    {
        if (Data::ControllerChannelStatus *status = channelSnapshot(role)) {
            status->state = state;
            status->detail = detail;
            if (state == Data::ControllerChannelState::Connected)
                status->lastActivityAt = QDateTime::currentDateTimeUtc();
        }
    }

    void touchChannel(Protocol::Role role)
    {
        if (Data::ControllerChannelStatus *status = channelSnapshot(role))
            status->lastActivityAt = QDateTime::currentDateTimeUtc();
    }

    void publish()
    {
        snapshot.updatedAt = QDateTime::currentDateTimeUtc();
        emit q->snapshotChanged();
    }

    void beginControlProgress(Data::ControllerControlCommand command)
    {
        snapshot.controlProgress = {};
        snapshot.controlProgress.command = command;
        snapshot.controlProgress.state = Data::ControllerControlState::Pending;
        snapshot.controlProgress.startedAt = QDateTime::currentDateTimeUtc();
        snapshot.lastError.reset();
        publish();
    }

    void finishControlProgress(
        Data::ControllerControlState state,
        quint16 stage,
        bool final,
        std::optional<qint32> status,
        std::optional<qint32> operationResult,
        const QString &detail)
    {
        snapshot.controlProgress.state = state;
        snapshot.controlProgress.stage = stage;
        snapshot.controlProgress.final = final;
        snapshot.controlProgress.status = status;
        snapshot.controlProgress.operationResult = operationResult;
        snapshot.controlProgress.detail = detail;
        if (state != Data::ControllerControlState::Pending)
            snapshot.controlProgress.completedAt = QDateTime::currentDateTimeUtc();
        publish();
    }

    quint64 allocateRequestId()
    {
        ++nextRequestId;
        if (!nextRequestId)
            ++nextRequestId;
        return nextRequestId;
    }

    void advanceGeneration()
    {
        ++generation;
        if (!generation)
            ++generation;
        snapshot.sessionGeneration = generation;
    }

    void clearLiveIdentity()
    {
        heartbeatTimer->stop();
        heartbeatRequestId = 0;
        snapshot.readOnly = false;
        snapshot.session.reset();
        snapshot.lastHeartbeatAt = {};
        sessionId = 0;
        bootId = 0;
        negotiatedMinor = 0;
        featureBits = 0;
    }

    void clearAlarmCheckpoint()
    {
        lastAlarmSequence = 0;
        alarmCheckpointEstablished = false;
    }

    void clearPendingRequests()
    {
        if (snapshot.controlProgress.state == Data::ControllerControlState::Pending) {
            snapshot.controlProgress.state = Data::ControllerControlState::Failed;
            snapshot.controlProgress.final = false;
            snapshot.controlProgress.detail = Tr::tr(
                "The controller operation was interrupted before a final response.");
            snapshot.controlProgress.completedAt = QDateTime::currentDateTimeUtc();
        }
        for (const PendingRequest &request : std::as_const(pendingRequests)) {
            if (request.timer) {
                request.timer->stop();
                request.timer->deleteLater();
            }
        }
        pendingRequests.clear();
        heartbeatRequestId = 0;
        disconnectReleaseRequestId = 0;
        controlRefreshPending = false;
        controlRefreshCompletesCommand = false;
        disconnectAfterRelease = false;
        stateBeforeDisconnectRelease.reset();
        refreshInProgress = false;
        refreshRejected = false;
    }

    void rememberPersistentPackageSelector(const Data::ControllerPackageSummary &package)
    {
        if (package.activeSlot == Data::ControllerSlot::None || !package.activeGeneration
            || !package.activeConfigurationId || !bootId) {
            return;
        }
        persistentPackageSelector = {
            package.activeSlot,
            package.activeGeneration,
            package.activeConfigurationId,
            bootId,
        };
    }

    void teardownChannel(Channel &value)
    {
        ++value.epoch;
        value.handshaken = false;
        value.sendSequence = 0;
        value.helloRequestId = 0;
        value.helloResumeSessionId = 0;
        value.parser.reset();
        if (value.phaseTimer) {
            value.phaseTimer->stop();
            value.phaseTimer->deleteLater();
            value.phaseTimer = nullptr;
        }
        if (value.socket) {
            value.socket->disconnect(q);
            value.socket->abort();
            value.socket->deleteLater();
            value.socket = nullptr;
        }
    }

    void teardownChannels()
    {
        clearPendingRequests();
        for (Channel &value : channels)
            teardownChannel(value);
    }

    void finalizeDisconnect()
    {
        disconnectAfterRelease = false;
        disconnectReleaseRequestId = 0;
        stateBeforeDisconnectRelease.reset();
        userDisconnecting = true;
        reconnectTimer->stop();
        snapshot.state = Data::ControllerConnectionState::Disconnecting;
        publish();
        advanceGeneration();
        teardownChannels();
        clearLiveIdentity();
        clearAlarmCheckpoint();
        clearResumeCandidate();
        markChannelsDisconnected();
        snapshot.state = Data::ControllerConnectionState::Disconnected;
        publish();
        userDisconnecting = false;
    }

    void sendBestEffortRelease()
    {
        if (!snapshot.session || !snapshot.session->ownsControlLease || !sessionId || !bootId)
            return;
        if (hasActiveControlOperation() || !snapshot.controllerState
            || !snapshot.controllerState->ready
            || (snapshot.controllerState->serviceState
                    != Data::ControllerServiceState::Shutdown
                && snapshot.controllerState->serviceState
                       != Data::ControllerServiceState::OperationalSafe)) {
            return;
        }
        Channel &control = channel(Protocol::Role::Control);
        if (!control.handshaken || !control.socket)
            return;
        heartbeatTimer->stop();
        Protocol::Error codecError;
        const QByteArray wire = Protocol::encodeRequest(
            Protocol::MessageType::ReleaseControl,
            {},
            sessionId,
            allocateRequestId(),
            ++control.sendSequence,
            bootId,
            negotiatedMinor,
            &codecError);
        if (wire.isEmpty())
            return;
        control.socket->write(wire);
        control.socket->flush();
        control.socket->waitForBytesWritten(100);
    }

    void armChannelTimer(Channel &value, int timeoutMs, const QString &summary)
    {
        if (value.phaseTimer) {
            value.phaseTimer->stop();
            value.phaseTimer->deleteLater();
        }
        value.phaseTimer = new QTimer(q);
        value.phaseTimer->setSingleShot(true);
        const quint64 expectedGeneration = generation;
        const quint64 expectedEpoch = value.epoch;
        const Protocol::Role role = value.role;
        QObject::connect(
            value.phaseTimer,
            &QTimer::timeout,
            q,
            [this, expectedGeneration, expectedEpoch, role, summary] {
                Channel &current = channel(role);
                if (generation != expectedGeneration || current.epoch != expectedEpoch)
                    return;
                failNetwork(role, Data::ControllerOperation::Connect, summary);
            });
        value.phaseTimer->start(timeoutMs);
    }

    void stopChannelTimer(Channel &value)
    {
        if (!value.phaseTimer)
            return;
        value.phaseTimer->stop();
        value.phaseTimer->deleteLater();
        value.phaseTimer = nullptr;
    }

    void setError(
        Data::ControllerErrorSource source,
        Protocol::Role role,
        Data::ControllerOperation operation,
        const QString &summary,
        const QString &detail = {},
        std::optional<qint32> code = {},
        std::optional<quint64> requestId = {},
        Data::ControllerRetryDisposition disposition = Data::ControllerRetryDisposition::Unknown)
    {
        Data::ControllerOperationError error;
        error.source = source;
        error.channelId = channelId(role);
        error.operation = operation;
        error.code = code;
        if (code)
            error.codeName = statusName(*code);
        error.requestId = requestId;
        error.occurredAt = QDateTime::currentDateTimeUtc();
        error.retryDisposition = disposition;
        error.summary = summary;
        error.detail = detail;
        snapshot.lastError = error;
    }

    void markChannelsDisconnected()
    {
        for (Channel &value : channels)
            setChannelState(value.role, Data::ControllerChannelState::Disconnected);
    }

    void terminalFailure(
        Data::ControllerErrorSource source,
        Protocol::Role role,
        Data::ControllerOperation operation,
        const QString &summary,
        const QString &detail = {},
        std::optional<qint32> code = {},
        std::optional<quint64> requestId = {})
    {
        advanceGeneration();
        teardownChannels();
        clearLiveIdentity();
        clearAlarmCheckpoint();
        markChannelsDisconnected();
        setChannelState(role, Data::ControllerChannelState::Failed, summary);
        snapshot.state = Data::ControllerConnectionState::Failed;
        setError(
            source,
            role,
            operation,
            summary,
            detail,
            code,
            requestId,
            code ? retryDisposition(*code) : Data::ControllerRetryDisposition::NotRetryable);
        publish();
    }

    void failProtocol(
        Protocol::Role role,
        Data::ControllerOperation operation,
        const QString &summary,
        const QString &detail = {},
        std::optional<quint64> requestId = {},
        bool requestMayHaveReachedController = true)
    {
        if (disconnectAfterRelease) {
            if (!requestMayHaveReachedController) {
                setError(
                    Data::ControllerErrorSource::Protocol,
                    role,
                    Data::ControllerOperation::ReleaseControl,
                    summary,
                    Tr::tr(
                        "The Release control request was not queued; the controller still "
                        "reports this session as the lease owner."),
                    {},
                    requestId,
                    Data::ControllerRetryDisposition::Retryable);
                disconnectAfterRelease = false;
                disconnectReleaseRequestId = 0;
                snapshot.state = stateBeforeDisconnectRelease.value_or(
                    Data::ControllerConnectionState::Connected);
                stateBeforeDisconnectRelease.reset();
                startHeartbeat();
                publish();
                return;
            }
            if (requestId
                && (!disconnectReleaseRequestId
                    || *requestId != disconnectReleaseRequestId)) {
                setError(
                    Data::ControllerErrorSource::Protocol,
                    role,
                    operation,
                    summary,
                    detail,
                    {},
                    requestId,
                    Data::ControllerRetryDisposition::NotRetryable);
                removePending(*requestId);
                publish();
                return;
            }
            setError(
                Data::ControllerErrorSource::Protocol,
                role,
                Data::ControllerOperation::ReleaseControl,
                summary,
                detail,
                {},
                requestId,
                Data::ControllerRetryDisposition::NotRetryable);
            finalizeDisconnect();
            return;
        }
        terminalFailure(
            Data::ControllerErrorSource::Protocol, role, operation, summary, detail, {}, requestId);
    }

    void failNetwork(
        Protocol::Role role,
        Data::ControllerOperation operation,
        const QString &summary,
        bool requestMayHaveReachedController = true,
        std::optional<quint64> requestId = {})
    {
        if (shuttingDown || userDisconnecting)
            return;
        if (disconnectAfterRelease) {
            if (!requestMayHaveReachedController) {
                setError(
                    Data::ControllerErrorSource::Network,
                    role,
                    Data::ControllerOperation::ReleaseControl,
                    summary,
                    Tr::tr(
                        "The Release control request was not queued; the controller still reports "
                        "this session as the lease owner."),
                    {},
                    {},
                    Data::ControllerRetryDisposition::Retryable);
                disconnectAfterRelease = false;
                disconnectReleaseRequestId = 0;
                snapshot.state = stateBeforeDisconnectRelease.value_or(
                    Data::ControllerConnectionState::Connected);
                stateBeforeDisconnectRelease.reset();
                startHeartbeat();
                publish();
                return;
            }
            if (requestId
                && (!disconnectReleaseRequestId
                    || *requestId != disconnectReleaseRequestId)) {
                setError(
                    Data::ControllerErrorSource::Network,
                    role,
                    operation,
                    summary,
                    {},
                    {},
                    requestId,
                    Data::ControllerRetryDisposition::Retryable);
                removePending(*requestId);
                publish();
                return;
            }
            setError(
                Data::ControllerErrorSource::Network,
                role,
                Data::ControllerOperation::ReleaseControl,
                summary,
                Tr::tr(
                    "The Release control result is unknown because the connection ended before "
                    "confirmation."),
                {},
                {},
                Data::ControllerRetryDisposition::Reconnect);
            finalizeDisconnect();
            return;
        }
        setChannelState(role, Data::ControllerChannelState::Failed, summary);
        setError(
            Data::ControllerErrorSource::Network,
            role,
            operation,
            summary,
            {},
            {},
            {},
            Data::ControllerRetryDisposition::Reconnect);
        scheduleReconnect();
    }

    int reconnectDelayMs() const
    {
        qint64 delay = options.reconnectInitialDelayMs;
        const int shifts = std::min(reconnectAttempt - 1, 20);
        delay <<= std::max(0, shifts);
        return int(std::min<qint64>(delay, options.reconnectMaximumDelayMs));
    }

    void scheduleReconnect(int minimumDelayMs = 0)
    {
        if (shuttingDown || userDisconnecting)
            return;

        if (sessionId) {
            resumeSessionId = sessionId;
            resumeBootId = bootId;
        }
        const bool hasResumeIdentity = resumeSessionId && resumeBootId;
        advanceGeneration();
        teardownChannels();
        clearLiveIdentity();
        if (!hasResumeIdentity)
            clearAlarmCheckpoint();
        markChannelsDisconnected();

        ++reconnectAttempt;
        if (reconnectAttempt > options.reconnectAttempts) {
            snapshot.state = Data::ControllerConnectionState::Failed;
            publish();
            return;
        }

        snapshot.state = Data::ControllerConnectionState::Connecting;
        publish();
        reconnectTimer->start(std::max(reconnectDelayMs(), minimumDelayMs));
    }

    void openChannel(Channel &value)
    {
        if (shuttingDown || userDisconnecting || value.socket)
            return;
        ++value.epoch;
        value.sendSequence = 0;
        value.handshaken = false;
        value.parser = std::make_unique<Protocol::FrameParser>(value.role);
        value.socket = new QTcpSocket(q);
        value.socket->setReadBufferSize(Protocol::HeaderBytes + roleMaximumPayloadBytes(value.role));

        const quint64 expectedGeneration = generation;
        const quint64 expectedEpoch = value.epoch;
        const Protocol::Role role = value.role;
        QObject::connect(
            value.socket, &QTcpSocket::connected, q, [this, expectedGeneration, expectedEpoch, role] {
                Channel &current = channel(role);
                if (generation != expectedGeneration || current.epoch != expectedEpoch)
                    return;
                stopChannelTimer(current);
                setChannelState(role, Data::ControllerChannelState::Handshaking);
                snapshot.state = Data::ControllerConnectionState::Handshaking;
                publish();
                sendHello(current);
            });
        QObject::connect(
            value.socket, &QTcpSocket::readyRead, q, [this, expectedGeneration, expectedEpoch, role] {
                Channel &current = channel(role);
                if (generation != expectedGeneration || current.epoch != expectedEpoch
                    || !current.socket || !current.parser) {
                    return;
                }
                const QByteArray bytes = current.socket->readAll();
                const Protocol::ParseResult result = current.parser->append(bytes);
                if (result.error) {
                    failProtocol(
                        role,
                        Data::ControllerOperation::Handshake,
                        Tr::tr("The controller returned a malformed protocol frame."),
                        result.error->text);
                    return;
                }
                for (const Protocol::Frame &frame : result.frames) {
                    if (generation != expectedGeneration || current.epoch != expectedEpoch)
                        return;
                    touchChannel(role);
                    dispatchFrame(current, frame);
                }
            });
        QObject::connect(
            value.socket,
            &QTcpSocket::errorOccurred,
            q,
            [this, expectedGeneration, expectedEpoch, role](QAbstractSocket::SocketError) {
                Channel &current = channel(role);
                if (generation != expectedGeneration || current.epoch != expectedEpoch)
                    return;
                if (role == Protocol::Role::Control && current.helloResumeSessionId)
                    clearResumeCandidate();
                failNetwork(
                    role,
                    Data::ControllerOperation::Connect,
                    Tr::tr("The controller channel connection failed."));
            });
        QObject::connect(
            value.socket,
            &QTcpSocket::disconnected,
            q,
            [this, expectedGeneration, expectedEpoch, role] {
                Channel &current = channel(role);
                if (generation != expectedGeneration || current.epoch != expectedEpoch
                    || shuttingDown || userDisconnecting) {
                    return;
                }
                failNetwork(
                    role,
                    Data::ControllerOperation::Connect,
                    Tr::tr("The controller closed the channel."));
            });

        setChannelState(value.role, Data::ControllerChannelState::Connecting);
        publish();
        armChannelTimer(
            value, options.connectTimeoutMs, Tr::tr("The controller channel connection timed out."));
        value.socket->connectToHost(endpoints.host, value.port);
    }

    bool writeFrame(
        Channel &value,
        const QByteArray &wire,
        Protocol::Role role,
        Data::ControllerOperation operation)
    {
        if (!value.socket || wire.isEmpty())
            return false;
#ifdef WITH_TESTS
        if (failNextWriteForTests) {
            failNextWriteForTests = false;
            failNetwork(
                role,
                operation,
                Tr::tr("The controller request could not be queued."),
                false);
            return false;
        }
#endif
        if (value.socket->write(wire) != wire.size()) {
            failNetwork(
                role,
                operation,
                Tr::tr("The controller request could not be queued."),
                false);
            return false;
        }
        return true;
    }

    void addPending(quint64 requestId, PendingRequest request, int timeoutMs)
    {
        request.responseTimeoutMs = timeoutMs;
        request.timer = new QTimer(q);
        request.timer->setSingleShot(true);
        const quint64 expectedGeneration = request.generation;
        const quint64 expectedEpoch = request.channelEpoch;
        const Protocol::Role role = request.role;
        const Data::ControllerOperation operation = request.operation;
        const PendingKind kind = request.kind;
        QObject::connect(
            request.timer,
            &QTimer::timeout,
            q,
            [this, requestId, expectedGeneration, expectedEpoch, role, operation, kind] {
                const auto found = pendingRequests.constFind(requestId);
                if (found == pendingRequests.cend() || found->generation != expectedGeneration
                    || found->channelEpoch != expectedEpoch || generation != expectedGeneration) {
                    return;
                }
                if (kind == PendingKind::ControlCommand || kind == PendingKind::Topology
                    || kind == PendingKind::RestorePackage) {
                    finishControlProgress(
                        Data::ControllerControlState::Failed,
                        snapshot.controlProgress.stage,
                        false,
                        {},
                        {},
                        Tr::tr("The controller operation timed out; its final state is unknown."));
                }
                if (kind == PendingKind::Heartbeat) {
                    heartbeatRequestId = 0;
                    heartbeatTimer->stop();
                    if (!disconnectAfterRelease && snapshot.session)
                        snapshot.session->ownsControlLease = false;
                }
                failNetwork(
                    role,
                    operation,
                    Tr::tr("The controller response timed out."),
                    true,
                    requestId);
            });
        pendingRequests.insert(requestId, request);
        request.timer->start(timeoutMs);
    }

    void removePending(quint64 requestId)
    {
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return;
        if (found->timer) {
            found->timer->stop();
            found->timer->deleteLater();
        }
        if (requestId == heartbeatRequestId)
            heartbeatRequestId = 0;
        pendingRequests.erase(found);
    }

    void sendHello(Channel &value)
    {
        Protocol::Error codecError;
        const quint64 requestId = allocateRequestId();
        const quint64 resume = value.role == Protocol::Role::Control ? resumeSessionId : sessionId;
        value.helloRequestId = requestId;
        value.helloResumeSessionId = resume;
        const QByteArray wire = Protocol::encodeHello(
            value.role,
            resume,
            QRandomGenerator::system()->generate64(),
            requestId,
            ++value.sendSequence,
            Protocol::CurrentMinor,
            &codecError);
        if (wire.isEmpty()) {
            failProtocol(
                value.role,
                Data::ControllerOperation::Handshake,
                Tr::tr("The controller handshake could not be encoded."),
                codecError.text,
                requestId);
            return;
        }

        PendingRequest request;
        request.kind = PendingKind::Hello;
        request.role = value.role;
        request.operation = Data::ControllerOperation::Handshake;
        request.generation = generation;
        request.channelEpoch = value.epoch;
        request.requestType = Protocol::MessageType::Hello;
        addPending(requestId, request, options.handshakeTimeoutMs);
        if (!writeFrame(value, wire, value.role, Data::ControllerOperation::Handshake))
            removePending(requestId);
    }

    quint64 sendRequest(
        Channel &value,
        Protocol::MessageType type,
        PendingKind kind,
        Data::ControllerOperation operation,
        QByteArrayView payload = {},
        int responseTimeoutMs = 0)
    {
        Protocol::Error codecError;
        const quint64 requestId = allocateRequestId();
        const QByteArray wire = Protocol::encodeRequest(
            type,
            payload,
            sessionId,
            requestId,
            ++value.sendSequence,
            bootId,
            negotiatedMinor,
            &codecError);
        if (wire.isEmpty()) {
            failProtocol(
                value.role,
                operation,
                Tr::tr("The controller request could not be encoded."),
                codecError.text,
                requestId,
                false);
            return 0;
        }

        PendingRequest request;
        request.kind = kind;
        request.role = value.role;
        request.operation = operation;
        request.generation = generation;
        request.channelEpoch = value.epoch;
        request.requestType = type;
        addPending(
            requestId,
            request,
            responseTimeoutMs > 0 ? responseTimeoutMs : options.requestTimeoutMs);
        if (!writeFrame(value, wire, value.role, operation)) {
            removePending(requestId);
            return 0;
        }
        return requestId;
    }

    int heartbeatResponseTimeoutMs() const
    {
        int timeoutMs = options.requestTimeoutMs;
        for (const PendingRequest &request : std::as_const(pendingRequests)) {
            if ((request.kind == PendingKind::ControlCommand
                 || request.kind == PendingKind::Topology
                 || request.kind == PendingKind::RestorePackage)
                && request.responseTimeoutMs > options.requestTimeoutMs) {
                timeoutMs = std::max(timeoutMs, request.responseTimeoutMs);
            }
        }
        return timeoutMs;
    }

    void extendPendingHeartbeatTimeout(int timeoutMs)
    {
        if (!heartbeatRequestId || timeoutMs <= options.requestTimeoutMs)
            return;
        auto heartbeat = pendingRequests.find(heartbeatRequestId);
        if (heartbeat == pendingRequests.end() || heartbeat->kind != PendingKind::Heartbeat
            || heartbeat->responseTimeoutMs > timeoutMs) {
            return;
        }
        heartbeat->responseTimeoutMs = timeoutMs;
        if (heartbeat->timer)
            heartbeat->timer->start(timeoutMs);
    }

    quint64 sendControlRequest(
        Data::ControllerControlCommand command,
        PendingKind kind,
        QByteArrayView payload,
        quint16 firstExpectedStage,
        bool terminalCommandStatus)
    {
        const int responseTimeoutMs
            = command == Data::ControllerControlCommand::EnterConfigurationMode
                      || command == Data::ControllerControlCommand::RestoreActivePackage
                  ? std::max(options.requestTimeoutMs, 40000)
                  : options.requestTimeoutMs;
        Channel &control = channel(Protocol::Role::Control);
        const quint64 requestId = sendRequest(
            control,
            messageTypeForCommand(command),
            kind,
            operationForCommand(command),
            payload,
            responseTimeoutMs);
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return 0;
        found->controlCommand = command;
        found->nextExpectedStage = firstExpectedStage;
        found->terminalCommandStatus = terminalCommandStatus;
        extendPendingHeartbeatTimeout(responseTimeoutMs);
        if (disconnectAfterRelease
            && command == Data::ControllerControlCommand::ReleaseControl) {
            disconnectReleaseRequestId = requestId;
        }
        return requestId;
    }

    void startHeartbeat()
    {
        if (!snapshot.session || !snapshot.session->ownsControlLease)
            return;
        const int defaultDuration = snapshot.session->defaultControlLeaseDurationMs;
        heartbeatTimer->setInterval(std::max(250, defaultDuration / 3));
        heartbeatTimer->start();
        sendHeartbeat();
    }

    void sendHeartbeat()
    {
        if (shuttingDown || userDisconnecting || disconnectAfterRelease || heartbeatRequestId
            || snapshot.state == Data::ControllerConnectionState::Disconnected
            || !snapshot.session || !snapshot.session->ownsControlLease) {
            return;
        }
        Channel &control = channel(Protocol::Role::Control);
        if (!control.handshaken)
            return;
        const quint64 requestId = sendRequest(
            control,
            Protocol::MessageType::Heartbeat,
            PendingKind::Heartbeat,
            Data::ControllerOperation::Heartbeat,
            {},
            heartbeatResponseTimeoutMs());
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return;
        found->nextExpectedStage = 4;
        found->terminalCommandStatus = true;
        heartbeatRequestId = requestId;
    }

    quint64 sendResumeEvents(Channel &value, quint32 afterSequence)
    {
        Protocol::Error codecError;
        const quint64 requestId = allocateRequestId();
        const QByteArray wire = Protocol::encodeResumeEvents(
            afterSequence,
            sessionId,
            requestId,
            ++value.sendSequence,
            bootId,
            negotiatedMinor,
            &codecError);
        if (wire.isEmpty()) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The event subscription could not be encoded."),
                codecError.text,
                requestId);
            return 0;
        }

        PendingRequest request;
        request.kind = PendingKind::ResumeEvents;
        request.role = value.role;
        request.operation = Data::ControllerOperation::SubscribeEvents;
        request.generation = generation;
        request.channelEpoch = value.epoch;
        request.requestType = Protocol::MessageType::ResumeEvents;
        request.requestedAfterSequence = afterSequence;
        addPending(requestId, request, options.requestTimeoutMs);
        if (!writeFrame(value, wire, value.role, request.operation)) {
            removePending(requestId);
            return 0;
        }
        return requestId;
    }

    void clearResumeCandidate()
    {
        resumeSessionId = 0;
        resumeBootId = 0;
    }

    void handleHello(Channel &value, const Protocol::Frame &frame, quint64 requestId)
    {
        const quint64 expectedResume = value.helloResumeSessionId;
        if (frame.header.messageType == Protocol::MessageType::Error) {
            Protocol::Error decodeError;
            const auto capacity = Protocol::decodeHelloCapacityError(
                frame, value.role, requestId, &decodeError);
            if (!capacity) {
                failProtocol(
                    value.role,
                    Data::ControllerOperation::Handshake,
                    Tr::tr("The controller returned an invalid handshake error."),
                    decodeError.text,
                    requestId);
                return;
            }
            const int retryAfterMs = int(std::min(
                capacity->retryAfterMs, quint32(options.reconnectMaximumDelayMs)));
            snapshot.lastError.reset();
            setError(
                Data::ControllerErrorSource::Controller,
                value.role,
                Data::ControllerOperation::Handshake,
                Tr::tr("The controller session capacity is exhausted."),
                {},
                SessionCapacityStatus,
                requestId,
                Data::ControllerRetryDisposition::Retryable);
            snapshot.lastError->retryAfterMs = retryAfterMs;
            scheduleReconnect(retryAfterMs);
            return;
        }

        Protocol::Error decodeError;
        const std::optional<Protocol::HelloAck> ack
            = Protocol::decodeHelloAck(frame, value.role, expectedResume, &decodeError);
        if (!ack) {
            failProtocol(
                value.role,
                Data::ControllerOperation::Handshake,
                Tr::tr("The controller returned an invalid handshake."),
                decodeError.text,
                requestId);
            return;
        }
        if ((ack->featureBits & requiredFeatureMask(frame.header.protocolMinor))
            != requiredFeatureMask(frame.header.protocolMinor)) {
            failProtocol(
                value.role,
                Data::ControllerOperation::Handshake,
                Tr::tr("The controller is missing required protocol features."),
                {},
                requestId);
            return;
        }
        if (ack->maximumPayloadBytes != quint32(roleMaximumPayloadBytes(value.role))) {
            failProtocol(
                value.role,
                Data::ControllerOperation::Handshake,
                Tr::tr("The controller returned an invalid channel payload limit."),
                {},
                requestId);
            return;
        }

        if (value.role == Protocol::Role::Control) {
            if (expectedResume && resumeBootId && ack->bootId != resumeBootId) {
                clearResumeCandidate();
                failProtocol(
                    value.role,
                    Data::ControllerOperation::Handshake,
                    Tr::tr("The resumed controller session changed BootId."),
                    {},
                    requestId);
                return;
            }
            sessionId = ack->sessionId;
            bootId = ack->bootId;
            negotiatedMinor = frame.header.protocolMinor;
            featureBits = ack->featureBits;

            Data::ControllerSessionSummary session;
            session.sessionId = ack->sessionId;
            session.bootId = ack->bootId;
            session.controlLeaseOwnerSessionId = ack->controlLeaseOwnerSessionId;
            session.defaultControlLeaseDurationMs = int(ack->defaultControlLeaseDurationMs);
            const bool resumedControlLease
                = expectedResume != 0 && ack->controlLeaseOwnerSessionId == ack->sessionId;
            session.ownsControlLease = resumedControlLease;
            snapshot.session = session;
            snapshot.readOnly = false;
            snapshot.protocolVersion = {Protocol::CurrentMajor, negotiatedMinor};
            value.handshaken = true;
            setChannelState(value.role, Data::ControllerChannelState::Connected);
            if (session.ownsControlLease)
                startHeartbeat();

            clearResumeCandidate();
            openChannel(channel(Protocol::Role::Push));
            openChannel(channel(Protocol::Role::Bulk));
        } else {
            if (ack->sessionId != sessionId || ack->bootId != bootId
                || frame.header.protocolMinor != negotiatedMinor) {
                failProtocol(
                    value.role,
                    Data::ControllerOperation::Handshake,
                    Tr::tr("The controller channels disagree on session identity."),
                    {},
                    requestId);
                return;
            }
            value.handshaken = true;
            setChannelState(value.role, Data::ControllerChannelState::Connected);
            if (channel(Protocol::Role::Push).handshaken
                && channel(Protocol::Role::Bulk).handshaken) {
                beginRefresh();
            }
        }
        publish();
    }

    bool validateEstablishedIdentity(
        const Protocol::Frame &frame, const PendingRequest &request, quint64 requestId)
    {
        if (frame.header.sessionId == sessionId && frame.header.bootId == bootId)
            return true;
        failProtocol(
            request.role,
            request.operation,
            Tr::tr("The controller response has a stale session identity."),
            {},
            requestId);
        return false;
    }

    void recordControllerRejection(
        const PendingRequest &request,
        quint64 requestId,
        qint32 status,
        std::optional<qint32> operationResult,
        std::optional<quint64> sourceDetail,
        const QString &detail = {},
        quint16 stage = 0)
    {
        QString rejectionDetail = detail;
        if (status == -35 && sourceDetail) {
            rejectionDetail = Tr::tr(
                                  "Timing mode mismatch: requested %1, but the active package is %2.")
                                  .arg(
                                      timingModeName(quint32(*sourceDetail >> 32)),
                                      timingModeName(quint32(*sourceDetail)));
        } else if (status == -20) {
            rejectionDetail = Tr::tr(
                "The package capability descriptor does not match this controller. Rebuild the "
                "package from the current Capability descriptor.");
        }
        const bool controlRequest = request.kind == PendingKind::ControlCommand
                                    || request.kind == PendingKind::Topology
                                    || request.kind == PendingKind::RestorePackage;
        const bool heartbeat = request.kind == PendingKind::Heartbeat;
        setError(
            Data::ControllerErrorSource::Controller,
            request.role,
            request.operation,
            controlRequest || heartbeat
                ? Tr::tr("The controller rejected the control request.")
                : Tr::tr("The controller rejected a read-only request."),
            rejectionDetail,
            status,
            requestId,
            retryDisposition(status));
        snapshot.lastError->operationResult = operationResult;
        snapshot.lastError->sourceDetail = sourceDetail;
        if (invalidatesControlLease(status)) {
            heartbeatTimer->stop();
            if (snapshot.session) {
                snapshot.session->controlLeaseOwnerSessionId = 0;
                snapshot.session->ownsControlLease = false;
            }
        }
        if (controlRequest) {
            finishControlProgress(
                Data::ControllerControlState::Failed,
                stage ? stage : snapshot.controlProgress.stage,
                true,
                status,
                operationResult,
                rejectionDetail.isEmpty()
                    ? Tr::tr("%1 was rejected by the controller.")
                          .arg(commandDisplayName(request.controlCommand))
                    : rejectionDetail);
        } else if (!heartbeat && refreshInProgress) {
            refreshRejected = true;
            snapshot.controllerState.reset();
            snapshot.package.reset();
            switch (request.kind) {
            case PendingKind::State:
                stateReceived = true;
                break;
            case PendingKind::Capability:
                capabilityReceived = true;
                break;
            case PendingKind::Package:
                packageReceived = true;
                break;
            case PendingKind::Firmware:
                firmwareReceived = true;
                break;
            case PendingKind::ResumeEvents:
                subscriptionReceived = true;
                break;
            default:
                break;
            }
        }
        removePending(requestId);
        const bool releaseRequest
            = request.controlCommand == Data::ControllerControlCommand::ReleaseControl;
        if (disconnectAfterRelease && releaseRequest) {
            if (invalidatesControlLease(status)) {
                finalizeDisconnect();
                return;
            }
            disconnectAfterRelease = false;
            disconnectReleaseRequestId = 0;
            snapshot.state = stateBeforeDisconnectRelease.value_or(
                Data::ControllerConnectionState::Connected);
            stateBeforeDisconnectRelease.reset();
            startHeartbeat();
            publish();
            return;
        }
        if (disconnectAfterRelease) {
            publish();
            return;
        }
        if (isReconnectStatus(status)) {
            scheduleReconnect();
        } else if (controlRequest && sessionId) {
            snapshot.state = Data::ControllerConnectionState::Degraded;
            controlRefreshPending = true;
            controlRefreshCompletesCommand = false;
            snapshot.controlProgress.state = Data::ControllerControlState::Pending;
            snapshot.controlProgress.final = false;
            snapshot.controlProgress.detail = Tr::tr(
                "The command was rejected; refreshing authoritative controller state.");
            beginRefresh(true);
        } else if (!controlRequest && !heartbeat) {
            if (refreshInProgress)
                finishRefreshIfReady();
            else {
                snapshot.controllerState.reset();
                snapshot.package.reset();
                snapshot.state = Data::ControllerConnectionState::Degraded;
            }
            publish();
        } else {
            publish();
        }
    }

    void completeTerminalCommand(
        const PendingRequest &request,
        const Protocol::CommandStatus &status,
        quint64 requestId)
    {
        const Data::ControllerControlCommand command = request.controlCommand;
        if (command == Data::ControllerControlCommand::AcquireControl) {
            if (snapshot.session) {
                snapshot.session->controlLeaseOwnerSessionId = sessionId;
                snapshot.session->ownsControlLease = true;
            }
            startHeartbeat();
        } else if (command == Data::ControllerControlCommand::ReleaseControl) {
            heartbeatTimer->stop();
            if (snapshot.session) {
                snapshot.session->controlLeaseOwnerSessionId = 0;
                snapshot.session->ownsControlLease = false;
            }
        }
        removePending(requestId);
        if (command != Data::ControllerControlCommand::ReleaseControl) {
            controlRefreshPending = true;
            controlRefreshCompletesCommand = true;
            finishControlProgress(
                Data::ControllerControlState::Pending,
                status.stage,
                false,
                status.status,
                status.operationResult,
                Tr::tr("%1 was accepted; confirming the resulting controller state.")
                    .arg(commandDisplayName(command)));
            beginRefresh();
            return;
        }
        finishControlProgress(
            Data::ControllerControlState::Succeeded,
            status.stage,
            true,
            status.status,
            status.operationResult,
            Tr::tr("%1 completed successfully.").arg(commandDisplayName(command)));
        if (command == Data::ControllerControlCommand::ReleaseControl && disconnectAfterRelease) {
            finalizeDisconnect();
            return;
        }
    }

    bool handleStatefulCommandStatus(
        const Protocol::Frame &frame,
        const PendingRequest &request,
        quint64 requestId,
        QString *protocolDetail)
    {
        Protocol::Error decodeError;
        const auto status = Protocol::decodeCommandStatus(frame, &decodeError);
        if (!status) {
            *protocolDetail = decodeError.text;
            return false;
        }
        if (status->originalType != quint16(request.requestType)) {
            *protocolDetail = Tr::tr(
                "CommandStatus does not match the pending control request.");
            return false;
        }
        if (status->status) {
            if (status->stage != request.nextExpectedStage) {
                *protocolDetail = Tr::tr(
                    "Failed CommandStatus skipped an expected control stage.");
                return false;
            }
            recordControllerRejection(
                request,
                requestId,
                status->status,
                status->operationResult,
                status->detail,
                {},
                status->stage);
            return true;
        }
        if (status->stage != request.nextExpectedStage) {
            *protocolDetail = Tr::tr(
                "CommandStatus stages are not contiguous for the control request.");
            return false;
        }
        const bool expectedFinal = request.terminalCommandStatus && status->stage == 4;
        if (status->final != expectedFinal) {
            *protocolDetail = Tr::tr(
                "CommandStatus final flag does not match the control request contract.");
            return false;
        }

        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return true;
        if (found->timer)
            found->timer->start(found->responseTimeoutMs);
        if (request.kind != PendingKind::Heartbeat)
            extendPendingHeartbeatTimeout(found->responseTimeoutMs);
        if (found->nextExpectedStage <= 4)
            ++found->nextExpectedStage;

        if (request.kind == PendingKind::Heartbeat) {
            if (!status->final) {
                *protocolDetail = Tr::tr("Heartbeat did not complete at stage 4.");
                return false;
            }
            snapshot.lastHeartbeatAt = QDateTime::currentDateTimeUtc();
            removePending(requestId);
            publish();
            return true;
        }

        if (status->final) {
            completeTerminalCommand(request, *status, requestId);
            return true;
        }

        finishControlProgress(
            Data::ControllerControlState::Pending,
            status->stage,
            false,
            status->status,
            status->operationResult,
            Tr::tr("%1: stage %2 of 4.")
                .arg(commandDisplayName(request.controlCommand))
                .arg(status->stage));
        return true;
    }

    bool handleControllerStatus(
        const Protocol::Frame &frame,
        const PendingRequest &request,
        quint64 requestId,
        QString *protocolDetail)
    {
        Protocol::Error decodeError;
        if (frame.header.messageType == Protocol::MessageType::CommandStatus) {
            const auto status = Protocol::decodeCommandStatus(frame, &decodeError);
            if (!status) {
                *protocolDetail = decodeError.text;
                return false;
            }
            if (!status->status || status->originalType != quint16(request.requestType)
                || request.role == Protocol::Role::Bulk) {
                *protocolDetail
                    = QStringLiteral("CommandStatus does not match the pending read-only request.");
                return false;
            }
            recordControllerRejection(
                request,
                requestId,
                status->status,
                status->operationResult,
                status->detail);
            return true;
        }
        if (frame.header.messageType == Protocol::MessageType::BulkStatus) {
            const auto status = Protocol::decodeBulkStatus(frame, &decodeError);
            if (!status) {
                *protocolDetail = decodeError.text;
                return false;
            }
            if (!status->status || status->originalType != quint16(request.requestType)
                || request.role != Protocol::Role::Bulk) {
                *protocolDetail
                    = QStringLiteral("BulkStatus does not match the pending read-only request.");
                return false;
            }
            recordControllerRejection(
                request, requestId, status->status, status->operationResult, {});
            return true;
        }
        if (frame.header.messageType == Protocol::MessageType::FirmwareStatus) {
            const auto status = Protocol::decodeFirmwareStatus(frame, &decodeError);
            *protocolDetail = status
                                  ? QStringLiteral(
                                        "FirmwareStatus cannot complete a read-only request.")
                                  : decodeError.text;
            return false;
        }
        *protocolDetail = QStringLiteral("Unexpected controller status response.");
        return false;
    }

    void dispatchFrame(Channel &value, const Protocol::Frame &frame)
    {
        const quint64 requestId = frame.header.requestId;
        if (!requestId) {
            handleUncorrelatedPush(value, frame);
            return;
        }

        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end()) {
            failProtocol(
                value.role,
                Data::ControllerOperation::None,
                Tr::tr("The controller returned an unknown RequestId."),
                {},
                requestId);
            return;
        }
        const PendingRequest request = *found;
        if (request.generation != generation || request.channelEpoch != value.epoch
            || request.role != value.role) {
            return;
        }

        if (request.kind == PendingKind::Hello) {
            removePending(requestId);
            stopChannelTimer(value);
            handleHello(value, frame, requestId);
            return;
        }
        if (!validateEstablishedIdentity(frame, request, requestId))
            return;
        const bool statefulRequest = request.kind == PendingKind::ControlCommand
                                     || request.kind == PendingKind::Topology
                                     || request.kind == PendingKind::RestorePackage
                                     || request.kind == PendingKind::Heartbeat;
        if (statefulRequest && frame.header.messageType == Protocol::MessageType::CommandStatus) {
            QString protocolDetail;
            if (!handleStatefulCommandStatus(frame, request, requestId, &protocolDetail)) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The controller returned a malformed control status."),
                    protocolDetail,
                    requestId);
            }
            return;
        }
        if (frame.header.messageType == Protocol::MessageType::CommandStatus
            || frame.header.messageType == Protocol::MessageType::BulkStatus
            || frame.header.messageType == Protocol::MessageType::FirmwareStatus) {
            QString protocolDetail;
            if (!handleControllerStatus(frame, request, requestId, &protocolDetail)) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The controller returned a malformed error status."),
                    protocolDetail,
                    requestId);
            }
            return;
        }

        Protocol::Error decodeError;
        switch (request.kind) {
        case PendingKind::State: {
            const auto state = Protocol::decodeControllerState(frame, &decodeError);
            if (!state)
                break;
            snapshot.controllerState = *state;
            stateReceived = true;
            removePending(requestId);
            finishRefreshIfReady();
            return;
        }
        case PendingKind::Capability: {
            const auto capability = Protocol::decodeCapability(frame, featureBits, &decodeError);
            if (!capability)
                break;
            snapshot.capability = *capability;
            capabilityReceived = true;
            removePending(requestId);
            finishRefreshIfReady();
            return;
        }
        case PendingKind::Package: {
            if (frame.payload.size() < 2
                || qFromBigEndian<quint16>(
                       reinterpret_cast<const uchar *>(frame.payload.constData()))
                       != quint16(request.requestType)) {
                failProtocol(
                    value.role,
                    request.operation,
                    Tr::tr("The package-state response does not match the request."),
                    {},
                    requestId);
                return;
            }
            const auto package = Protocol::decodePackageState(frame, &decodeError);
            if (!package)
                break;
            snapshot.package = *package;
            rememberPersistentPackageSelector(*package);
            packageReceived = true;
            removePending(requestId);
            finishRefreshIfReady();
            return;
        }
        case PendingKind::Firmware: {
            const auto firmware = Protocol::decodeFirmwareState(frame, &decodeError);
            if (!firmware)
                break;
            snapshot.firmware = *firmware;
            firmwareReceived = true;
            removePending(requestId);
            finishRefreshIfReady();
            return;
        }
        case PendingKind::ResumeEvents:
            handleResumeResult(value, frame, requestId, &decodeError);
            return;
        case PendingKind::ResumeReplay:
            handleReplayEvent(value, frame, requestId);
            return;
        case PendingKind::Topology: {
            if (request.nextExpectedStage != 5) {
                failProtocol(
                    value.role,
                    request.operation,
                    Tr::tr("The topology result arrived before all command stages completed."),
                    {},
                    requestId);
                return;
            }
            const auto topology = Protocol::decodeTopologyResult(frame, &decodeError);
            if (!topology)
                break;
            Data::ControllerTopologySnapshot result;
            result.firstStationAddress = request.firstStationAddress;
            result.respondingCount = topology->respondingCount;
            result.result = topology->result;
            result.discoveredAt = QDateTime::currentDateTimeUtc();
            result.slaves.reserve(topology->slaves.size());
            for (const Protocol::TopologySlave &slave : topology->slaves) {
                result.slaves.append(
                    {slave.position,
                     slave.stationAddress,
                     slave.alState,
                     slave.flags,
                     slave.vendorId,
                     slave.productCode,
                     slave.revision,
                     slave.serial});
            }
            snapshot.topology = result;
            removePending(requestId);
            controlRefreshPending = true;
            controlRefreshCompletesCommand = true;
            finishControlProgress(
                Data::ControllerControlState::Pending,
                4,
                false,
                0,
                0,
                Tr::tr("Discovered %1 EtherCAT devices.").arg(result.respondingCount));
            beginRefresh();
            return;
        }
        case PendingKind::RestorePackage: {
            if (request.nextExpectedStage != 5) {
                failProtocol(
                    value.role,
                    request.operation,
                    Tr::tr("The package result arrived before all command stages completed."),
                    {},
                    requestId);
                return;
            }
            if (frame.payload.size() < 2
                || qFromBigEndian<quint16>(
                       reinterpret_cast<const uchar *>(frame.payload.constData()))
                       != quint16(request.requestType)) {
                failProtocol(
                    value.role,
                    request.operation,
                    Tr::tr("The package restore result does not match the request."),
                    {},
                    requestId);
                return;
            }
            const auto package = Protocol::decodePackageState(frame, &decodeError);
            if (!package)
                break;
            snapshot.package = *package;
            rememberPersistentPackageSelector(*package);
            removePending(requestId);
            controlRefreshPending = true;
            controlRefreshCompletesCommand = true;
            finishControlProgress(
                Data::ControllerControlState::Pending,
                4,
                false,
                0,
                0,
                Tr::tr(
                    "The active controller package was restored; confirming the resulting "
                    "controller state."));
            beginRefresh();
            return;
        }
        case PendingKind::ControlCommand:
        case PendingKind::Heartbeat:
            break;
        case PendingKind::Hello:
            return;
        }

        if (decodeError.category == Protocol::ErrorCategory::ControllerStatus
            && decodeError.status) {
            recordControllerRejection(
                request,
                requestId,
                *decodeError.status,
                decodeError.operationResult,
                decodeError.sourceDetail,
                decodeError.text);
            return;
        }
        failProtocol(
            value.role,
            request.operation,
            Tr::tr("The controller returned an invalid operation result."),
            decodeError.text,
            requestId);
    }

    void handleResumeResult(
        Channel &value,
        const Protocol::Frame &frame,
        quint64 requestId,
        Protocol::Error *decodeError)
    {
        const auto result = Protocol::decodeResumeEventsResult(frame, decodeError);
        if (!result) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller returned an invalid event subscription result."),
                decodeError->text,
                requestId);
            return;
        }
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return;
        if (result->requestedAfterSequence != found->requestedAfterSequence) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The event subscription checkpoint does not match the request."),
                {},
                requestId);
            return;
        }
        if (result->status != 0) {
            removePending(requestId);
            subscriptionReceived = true;
            subscriptionDegraded = true;
            clearAlarmCheckpoint();
            setError(
                Data::ControllerErrorSource::Controller,
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                result->status == EventGapStatus
                    ? Tr::tr("The controller event history has a gap.")
                    : Tr::tr("The controller could not establish event recovery."),
                {},
                result->status,
                requestId,
                Data::ControllerRetryDisposition::Retryable);
            finishRefreshIfReady();
            return;
        }
        if (!result->replayCount) {
            lastAlarmSequence = result->latestSequence;
            alarmCheckpointEstablished = true;
            subscriptionReceived = true;
            removePending(requestId);
            finishRefreshIfReady();
            return;
        }

        found->kind = PendingKind::ResumeReplay;
        found->remainingReplayEvents = result->replayCount;
        found->latestAlarmSequence = result->latestSequence;
        found->expectedAlarmSequence = found->requestedAfterSequence
                                           ? nextAlarmSequence(found->requestedAfterSequence)
                                           : result->oldestSequence;
        if (found->timer)
            found->timer->start(found->responseTimeoutMs);
    }

    static quint32 nextAlarmSequence(quint32 value)
    {
        return value == std::numeric_limits<quint32>::max() ? 1 : value + 1;
    }

    void handleReplayEvent(Channel &value, const Protocol::Frame &frame, quint64 requestId)
    {
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return;
        if (frame.header.messageType != Protocol::MessageType::AlarmRaised
            && frame.header.messageType != Protocol::MessageType::AlarmCleared) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("An unrelated frame interrupted the event replay."),
                {},
                requestId);
            return;
        }
        if (frame.payload.size() != AlarmEventBytes) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller returned an invalid alarm event."),
                {},
                requestId);
            return;
        }
        const quint32 sequence = readU32(frame.payload, 0);
        const bool more = frame.header.flags & Protocol::flagValue(Protocol::Flag::More);
        const bool expectedMore = found->remainingReplayEvents > 1;
        if (sequence != found->expectedAlarmSequence || more != expectedMore) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller event replay is not contiguous."),
                {},
                requestId);
            return;
        }

        --found->remainingReplayEvents;
        if (found->remainingReplayEvents) {
            found->expectedAlarmSequence = nextAlarmSequence(sequence);
            if (found->timer)
                found->timer->start(found->responseTimeoutMs);
            return;
        }
        if (sequence != found->latestAlarmSequence) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller event replay ended at the wrong checkpoint."),
                {},
                requestId);
            return;
        }
        lastAlarmSequence = sequence;
        alarmCheckpointEstablished = true;
        subscriptionReceived = true;
        removePending(requestId);
        finishRefreshIfReady();
    }

    bool handleReplaceablePush(const Protocol::Frame &frame)
    {
        Protocol::Error decodeError;
        switch (frame.header.messageType) {
        case Protocol::MessageType::ControllerState: {
            const auto state = Protocol::decodeControllerState(frame, &decodeError);
            if (!state)
                break;
            snapshot.controllerState = *state;
            publish();
            return true;
        }
        case Protocol::MessageType::PerformanceSnapshot: {
            const int expectedBytes = frame.header.protocolMinor == 1
                                          ? 240
                                          : (frame.header.protocolMinor <= 6 ? 256 : 320);
            if (frame.payload.size() != expectedBytes)
                break;
            return true;
        }
        case Protocol::MessageType::PushHeartbeat: {
            const auto heartbeat = Protocol::decodePushHeartbeat(frame, &decodeError);
            if (!heartbeat || heartbeat->bootId != bootId)
                break;
            snapshot.lastHeartbeatAt = QDateTime::currentDateTimeUtc();
            publish();
            return true;
        }
        case Protocol::MessageType::FirmwareProgress: {
            const auto firmware = Protocol::decodeFirmwareState(frame, &decodeError);
            if (!firmware)
                break;
            snapshot.firmware = *firmware;
            publish();
            return true;
        }
        default:
            return false;
        }

        failProtocol(
            Protocol::Role::Push,
            Data::ControllerOperation::SubscribeEvents,
            Tr::tr("The controller returned an invalid replaceable push frame."),
            decodeError.text);
        return true;
    }

    void handleUncorrelatedPush(Channel &value, const Protocol::Frame &frame)
    {
        if (value.role != Protocol::Role::Push || !value.handshaken) {
            failProtocol(
                value.role,
                Data::ControllerOperation::None,
                Tr::tr("The controller returned an uncorrelated channel frame."));
            return;
        }
        if (frame.header.sessionId != sessionId || frame.header.bootId != bootId) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The push frame has a stale session identity."));
            return;
        }
        if (handleReplaceablePush(frame))
            return;

        if (frame.header.messageType == Protocol::MessageType::ResumeEventsResult) {
            Protocol::Error decodeError;
            const auto result = Protocol::decodeResumeEventsResult(frame, &decodeError);
            if (!result || result->status != EventGapStatus) {
                failProtocol(
                    value.role,
                    Data::ControllerOperation::SubscribeEvents,
                    Tr::tr("The controller returned an invalid asynchronous event status."),
                    decodeError.text);
                return;
            }
            clearAlarmCheckpoint();
            snapshot.state = Data::ControllerConnectionState::Degraded;
            setError(
                Data::ControllerErrorSource::Controller,
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller event history has a gap."),
                {},
                result->status,
                {},
                Data::ControllerRetryDisposition::Retryable);
            publish();
            return;
        }
        if (frame.header.messageType == Protocol::MessageType::AlarmRaised
            || frame.header.messageType == Protocol::MessageType::AlarmCleared) {
            if (!alarmCheckpointEstablished || frame.payload.size() != AlarmEventBytes) {
                failProtocol(
                    value.role,
                    Data::ControllerOperation::SubscribeEvents,
                    Tr::tr("A live alarm arrived before an event checkpoint."));
                return;
            }
            const quint32 sequence = readU32(frame.payload, 0);
            if (sequence != nextAlarmSequence(lastAlarmSequence)) {
                clearAlarmCheckpoint();
                snapshot.state = Data::ControllerConnectionState::Degraded;
                setError(
                    Data::ControllerErrorSource::Protocol,
                    value.role,
                    Data::ControllerOperation::SubscribeEvents,
                    Tr::tr("The live controller alarm sequence has a gap."),
                    {},
                    {},
                    {},
                    Data::ControllerRetryDisposition::Retryable);
                publish();
                return;
            }
            lastAlarmSequence = sequence;
            return;
        }

        failProtocol(
            value.role,
            Data::ControllerOperation::SubscribeEvents,
            Tr::tr("The controller returned an unknown push message."));
    }

    bool hasActiveControlOperation() const
    {
        return controlRefreshPending
               || std::any_of(
            pendingRequests.cbegin(), pendingRequests.cend(), [](const PendingRequest &request) {
                return request.kind == PendingKind::ControlCommand
                       || request.kind == PendingKind::Topology
                       || request.kind == PendingKind::RestorePackage;
            });
    }

    std::optional<QString> controlPostconditionError() const
    {
        using Command = Data::ControllerControlCommand;
        using PackageState = Data::ControllerPackageState;
        using ServiceState = Data::ControllerServiceState;

        const Data::ControllerControlCommand command = snapshot.controlProgress.command;
        const Data::ControllerStateSummary *state
            = snapshot.controllerState ? &*snapshot.controllerState : nullptr;
        const Data::ControllerPackageSummary *package = snapshot.package ? &*snapshot.package
                                                                         : nullptr;
        const bool ownsLease = snapshot.session && snapshot.session->ownsControlLease;
        const bool stateReady = state && state->ready;
        const bool packageActive = package && package->controllerState == PackageState::Active
                                   && package->controllerBootId == bootId;
        const bool operationalBus
            = state && state->busOperational && (state->ethercatAlStateBits & 0x08)
              && state->expectedWorkingCounter
              && state->actualWorkingCounter == state->expectedWorkingCounter;
        const bool noFaults = state && !state->currentFaults && !state->latchedFaults;

        bool satisfied = false;
        QString expected;
        switch (command) {
        case Command::AcquireControl:
            satisfied = snapshot.session && snapshot.session->ownsControlLease;
            expected = Tr::tr("the session to own the control lease");
            break;
        case Command::EnterConfigurationMode:
            satisfied = ownsLease && stateReady && state->serviceState == ServiceState::Shutdown
                        && package && package->controllerState != PackageState::Active;
            expected = Tr::tr("SHUTDOWN with no active CPU1 package");
            break;
        case Command::DiscoverTopology:
            satisfied = ownsLease && stateReady && state->serviceState == ServiceState::Shutdown
                        && package && package->controllerState != PackageState::Active;
            expected = Tr::tr("SHUTDOWN with no active CPU1 package");
            break;
        case Command::RestoreActivePackage:
            satisfied = ownsLease && stateReady
                        && state->serviceState == ServiceState::OperationalSafe && packageActive
                        && operationalBus && noFaults;
            expected = Tr::tr(
                "OP_SAFE with an active package, OP bus, matching working counters, and no faults");
            break;
        case Command::Start:
        case Command::StartFreeRun:
        case Command::StartDistributedClocks:
        case Command::Resume:
            satisfied = ownsLease && stateReady && state->serviceState == ServiceState::Running
                        && packageActive && operationalBus && noFaults;
            expected = Tr::tr(
                "RUNNING with an active package, OP bus, matching working counters, and no faults");
            break;
        case Command::Pause:
            satisfied = ownsLease && stateReady && state->serviceState == ServiceState::Paused
                        && packageActive && operationalBus;
            expected = Tr::tr(
                "PAUSED with an active package, OP bus, and matching working counters");
            break;
        case Command::ControlledStop:
            satisfied = ownsLease && stateReady
                        && state->serviceState == ServiceState::OperationalSafe && packageActive
                        && operationalBus;
            expected = Tr::tr(
                "OP_SAFE with an active package, OP bus, and matching working counters");
            break;
        case Command::ReleaseControl:
        case Command::None:
            return {};
        }
        if (satisfied)
            return {};
        return Tr::tr("%1 was accepted, but the refreshed controller state did not reach %2.")
            .arg(commandDisplayName(command), expected);
    }

    void beginRefresh(bool preserveError = false)
    {
        if (refreshInProgress || shuttingDown || !sessionId)
            return;
        refreshInProgress = true;
        refreshRejected = false;
        stateReceived = false;
        capabilityReceived = false;
        packageReceived = false;
        firmwareReceived = negotiatedMinor < 9 || !(featureBits & FeatureFirmwareUpdate);
        subscriptionReceived = negotiatedMinor < 4 || !(featureBits & FeatureExactAlarmReplay);
        subscriptionDegraded = false;
        if (!preserveError)
            snapshot.lastError.reset();

        Channel &control = channel(Protocol::Role::Control);
        Channel &bulk = channel(Protocol::Role::Bulk);
        Channel &push = channel(Protocol::Role::Push);
        if (!sendRequest(
                control,
                Protocol::MessageType::GetState,
                PendingKind::State,
                Data::ControllerOperation::QueryState)) {
            return;
        }
        if (!sendRequest(
                bulk,
                Protocol::MessageType::GetCapability,
                PendingKind::Capability,
                Data::ControllerOperation::QueryCapability)) {
            return;
        }
        if (!sendRequest(
                control,
                Protocol::MessageType::GetPackageState,
                PendingKind::Package,
                Data::ControllerOperation::QueryPackageState)) {
            return;
        }
        if (!firmwareReceived
            && !sendRequest(
                control,
                Protocol::MessageType::GetFirmwareState,
                PendingKind::Firmware,
                Data::ControllerOperation::QueryFirmwareState)) {
            return;
        }
        if (!subscriptionReceived && !sendResumeEvents(push, lastAlarmSequence))
            return;
        finishRefreshIfReady();
    }

    void finishRefreshIfReady()
    {
        if (!refreshInProgress || !stateReceived || !capabilityReceived || !packageReceived
            || !firmwareReceived || !subscriptionReceived) {
            return;
        }
        refreshInProgress = false;
        reconnectAttempt = 0;
        snapshot.state = subscriptionDegraded || refreshRejected
                             ? Data::ControllerConnectionState::Degraded
                             : Data::ControllerConnectionState::Connected;
        if (!snapshot.connectedAt.isValid())
            snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        if (controlRefreshPending) {
            const bool completesCommand = controlRefreshCompletesCommand;
            controlRefreshPending = false;
            controlRefreshCompletesCommand = false;
            if (!completesCommand) {
                const Data::ControllerControlCommand command = snapshot.controlProgress.command;
                if (refreshRejected) {
                    snapshot.controllerState.reset();
                    snapshot.package.reset();
                }
                const QString refreshDetail
                    = refreshRejected
                          ? Tr::tr(
                                "%1 was rejected; the authoritative controller state could not be "
                                "refreshed.")
                                .arg(commandDisplayName(command))
                          : Tr::tr(
                                "%1 was rejected; the authoritative controller state was "
                                "refreshed.")
                                .arg(commandDisplayName(command));
                const QString rejectionDetail
                    = snapshot.lastError ? snapshot.lastError->detail : QString();
                finishControlProgress(
                    Data::ControllerControlState::Failed,
                    snapshot.controlProgress.stage,
                    true,
                    snapshot.controlProgress.status,
                    snapshot.controlProgress.operationResult,
                    rejectionDetail.isEmpty()
                        ? refreshDetail
                        : QStringLiteral("%1\n%2").arg(rejectionDetail, refreshDetail));
                return;
            }
            if (refreshRejected) {
                snapshot.controllerState.reset();
                snapshot.package.reset();
                finishControlProgress(
                    Data::ControllerControlState::Failed,
                    snapshot.controlProgress.stage,
                    false,
                    snapshot.controlProgress.status,
                    snapshot.controlProgress.operationResult,
                    Tr::tr(
                        "The controller accepted the command, but its resulting state could not "
                        "be confirmed."));
                return;
            }
            const Data::ControllerControlCommand command = snapshot.controlProgress.command;
            if (const std::optional<QString> postconditionError = controlPostconditionError()) {
                snapshot.state = Data::ControllerConnectionState::Degraded;
                finishControlProgress(
                    Data::ControllerControlState::Failed,
                    snapshot.controlProgress.stage,
                    true,
                    snapshot.controlProgress.status,
                    snapshot.controlProgress.operationResult,
                    *postconditionError);
                return;
            }
            finishControlProgress(
                Data::ControllerControlState::Succeeded,
                snapshot.controlProgress.stage,
                true,
                snapshot.controlProgress.status,
                snapshot.controlProgress.operationResult,
                Tr::tr("%1 completed successfully and its resulting state was confirmed.")
                    .arg(commandDisplayName(command)));
            return;
        }
        if (refreshRejected) {
            snapshot.controllerState.reset();
            snapshot.package.reset();
        }
        publish();
    }

    ProductApiSession *q = nullptr;
    ProductApiSession::EndpointSet endpoints;
    ProductApiSession::Options options;
    Data::ControllerConnectionSnapshot snapshot;
    std::array<Channel, 3> channels;
    QHash<quint64, PendingRequest> pendingRequests;
    QTimer *reconnectTimer = nullptr;
    QTimer *heartbeatTimer = nullptr;
    Data::ControllerConnectionRequest currentRequest;
    std::optional<PersistentPackageSelector> persistentPackageSelector;
    quint64 generation = 0;
    quint64 nextRequestId = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 resumeSessionId = 0;
    quint64 resumeBootId = 0;
    quint32 featureBits = 0;
    quint64 heartbeatRequestId = 0;
    quint32 lastAlarmSequence = 0;
    quint16 negotiatedMinor = 0;
    int reconnectAttempt = 0;
    bool shuttingDown = false;
    bool userDisconnecting = false;
    bool refreshInProgress = false;
    bool refreshRejected = false;
    bool stateReceived = false;
    bool capabilityReceived = false;
    bool packageReceived = false;
    bool firmwareReceived = false;
    bool subscriptionReceived = false;
    bool subscriptionDegraded = false;
    bool alarmCheckpointEstablished = false;
    bool disconnectAfterRelease = false;
    quint64 disconnectReleaseRequestId = 0;
    std::optional<Data::ControllerConnectionState> stateBeforeDisconnectRelease;
    bool controlRefreshPending = false;
    bool controlRefreshCompletesCommand = false;
#ifdef WITH_TESTS
    bool failNextWriteForTests = false;
#endif
};

ProductApiSession::EndpointSet ProductApiSession::EndpointSet::productionDefaults()
{
    return {
        QStringLiteral("192.168.3.101"),
        15200,
        15201,
        15202,
        QStringLiteral("192.168.3.101:15200"),
    };
}

bool ProductApiSession::EndpointSet::isValid() const
{
    return !host.trimmed().isEmpty() && controlPort && pushPort && bulkPort
           && !endpointSummary.trimmed().isEmpty();
}

bool ProductApiSession::Options::isValid() const
{
    return connectTimeoutMs > 0 && handshakeTimeoutMs > 0 && requestTimeoutMs > 0
           && reconnectInitialDelayMs > 0
           && reconnectMaximumDelayMs >= reconnectInitialDelayMs && reconnectAttempts >= 0;
}

ProductApiSession::ProductApiSession(QObject *parent)
    : ProductApiSession(EndpointSet::productionDefaults(), Options(), parent)
{}

ProductApiSession::ProductApiSession(
    const EndpointSet &endpoints, const Options &options, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<ProductApiSessionPrivate>(this, endpoints, options))
{}

ProductApiSession::~ProductApiSession()
{
    shutdown();
}

Data::ControllerConnectionSnapshot ProductApiSession::snapshot() const
{
    return d->snapshot;
}

Utils::Result<> ProductApiSession::setEndpoints(const EndpointSet &endpoints)
{
    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (!endpoints.isValid())
        return Utils::ResultError(Tr::tr("The controller endpoint is not valid."));
    if (d->snapshot.state != Data::ControllerConnectionState::Disconnected
        && d->snapshot.state != Data::ControllerConnectionState::Failed) {
        return Utils::ResultError(
            Tr::tr("Disconnect the controller before changing its endpoint."));
    }

    d->userDisconnecting = true;
    d->reconnectTimer->stop();
    d->heartbeatTimer->stop();
    d->advanceGeneration();
    d->teardownChannels();
    d->clearLiveIdentity();
    d->clearAlarmCheckpoint();
    d->clearResumeCandidate();
    d->persistentPackageSelector.reset();
    d->currentRequest = {};
    d->reconnectAttempt = 0;
    d->stateReceived = false;
    d->capabilityReceived = false;
    d->packageReceived = false;
    d->firmwareReceived = false;
    d->subscriptionReceived = false;
    d->subscriptionDegraded = false;
    d->endpoints = endpoints;
    d->channels[0].port = endpoints.controlPort;
    d->channels[1].port = endpoints.pushPort;
    d->channels[2].port = endpoints.bulkPort;
    d->snapshot = {};
    d->snapshot.endpointSummary = endpoints.endpointSummary;
    d->snapshot.sessionGeneration = d->generation;
    d->snapshot.readOnly = false;
    d->snapshot.mock = false;
    d->initializeChannelSnapshots();
    d->publish();
    d->userDisconnecting = false;
    return {};
}

Utils::Result<> ProductApiSession::connectToController(
    const Data::ControllerConnectionRequest &request)
{
    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (!d->endpoints.isValid() || !d->options.isValid())
        return Utils::ResultError(Tr::tr("The controller endpoint is not configured."));
    if (request.scope.projectId.isNull() || request.scope.masterId.isNull()
        || request.profileId.isNull()) {
        return Utils::ResultError(Tr::tr("The controller connection request is incomplete."));
    }
    if (d->snapshot.state != Data::ControllerConnectionState::Disconnected
        && d->snapshot.state != Data::ControllerConnectionState::Failed) {
        return Utils::ResultError(Tr::tr("A controller connection operation is already active."));
    }

    d->userDisconnecting = false;
    d->reconnectTimer->stop();
    d->advanceGeneration();
    d->teardownChannels();
    d->clearLiveIdentity();
    d->clearAlarmCheckpoint();
    d->clearResumeCandidate();
    d->persistentPackageSelector.reset();
    d->currentRequest = request;
    d->snapshot.scope = request.scope;
    d->snapshot.profileId = request.profileId;
    d->snapshot.endpointSummary = d->endpoints.endpointSummary;
    d->snapshot.state = Data::ControllerConnectionState::Connecting;
    d->snapshot.connectedAt = {};
    d->snapshot.lastError.reset();
    d->snapshot.controllerState.reset();
    d->snapshot.capability.reset();
    d->snapshot.package.reset();
    d->snapshot.firmware.reset();
    d->snapshot.controlProgress = {};
    d->snapshot.topology.reset();
    d->reconnectAttempt = 0;
    d->markChannelsDisconnected();
    d->publish();
    d->openChannel(d->channel(Protocol::Role::Control));
    return {};
}

Utils::Result<> ProductApiSession::disconnectFromController()
{
    if (d->shuttingDown)
        return {};
    if (d->snapshot.state == Data::ControllerConnectionState::Disconnected)
        return {};
    if (d->hasActiveControlOperation())
        return Utils::ResultError(Tr::tr("Wait for the active controller operation to finish."));
    if (d->snapshot.session) {
        const Data::ControllerSessionSummary &session = *d->snapshot.session;
        if (session.controlLeaseOwnerSessionId
            && session.controlLeaseOwnerSessionId == session.sessionId
            && !session.ownsControlLease) {
            return Utils::ResultError(
                Tr::tr(
                    "The controller control lease ownership is unverified for this session."));
        }
    }
    if (d->snapshot.session && d->snapshot.session->ownsControlLease) {
        if (!d->snapshot.controllerState || !d->snapshot.controllerState->ready
            || (d->snapshot.controllerState->serviceState
                    != Data::ControllerServiceState::Shutdown
                && d->snapshot.controllerState->serviceState
                       != Data::ControllerServiceState::OperationalSafe)) {
            return Utils::ResultError(
                Tr::tr(
                    "Disconnect requires a ready controller in SHUTDOWN or OP_SAFE before "
                    "releasing control."));
        }
        Data::ControllerControlRequest release;
        release.command = Data::ControllerControlCommand::ReleaseControl;
        d->disconnectAfterRelease = true;
        d->disconnectReleaseRequestId = 0;
        d->stateBeforeDisconnectRelease = d->snapshot.state;
        d->heartbeatTimer->stop();
        const Utils::Result<> result = executeControlCommand(release);
        if (!result) {
            if (d->snapshot.state != Data::ControllerConnectionState::Disconnected) {
                d->disconnectAfterRelease = false;
                d->disconnectReleaseRequestId = 0;
                d->stateBeforeDisconnectRelease.reset();
                if (d->snapshot.session && d->snapshot.session->ownsControlLease)
                    d->startHeartbeat();
            }
            return result;
        }
        d->snapshot.state = Data::ControllerConnectionState::Disconnecting;
        d->publish();
        return {};
    }
    d->finalizeDisconnect();
    return {};
}

Utils::Result<> ProductApiSession::refreshController()
{
    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (d->snapshot.state != Data::ControllerConnectionState::Connected
        && d->snapshot.state != Data::ControllerConnectionState::Degraded) {
        return Utils::ResultError(Tr::tr("Connect to the controller before refreshing."));
    }
    if (d->refreshInProgress || !d->pendingRequests.isEmpty())
        return Utils::ResultError(Tr::tr("A controller refresh is already active."));
    d->beginRefresh();
    return {};
}

bool ProductApiSession::supportsControlCommand(Data::ControllerControlCommand command) const
{
    using Command = Data::ControllerControlCommand;
    switch (command) {
    case Command::AcquireControl:
    case Command::ReleaseControl:
    case Command::EnterConfigurationMode:
    case Command::DiscoverTopology:
    case Command::RestoreActivePackage:
    case Command::Start:
    case Command::Pause:
    case Command::Resume:
    case Command::ControlledStop:
        return true;
    case Command::StartFreeRun:
    case Command::StartDistributedClocks:
        return d->negotiatedMinor >= Protocol::ExplicitTimingModeMinor
               && (d->featureBits & FeatureExplicitTimingModeStart);
    case Command::None:
        return false;
    }
    return false;
}

Utils::Result<> ProductApiSession::executeControlCommand(
    const Data::ControllerControlRequest &request)
{
    using Command = Data::ControllerControlCommand;
    using ServiceState = Data::ControllerServiceState;

    if (!supportsControlCommand(request.command))
        return Utils::ResultError(Tr::tr("The requested controller command is not supported."));
    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (d->snapshot.state != Data::ControllerConnectionState::Connected
        && d->snapshot.state != Data::ControllerConnectionState::Degraded) {
        return Utils::ResultError(Tr::tr("Connect to the controller before sending commands."));
    }
    if (!d->sessionId || !d->bootId || !d->snapshot.session)
        return Utils::ResultError(Tr::tr("The controller session identity is not available."));
    if (d->refreshInProgress)
        return Utils::ResultError(Tr::tr("Wait for the controller refresh to finish."));
    if (d->hasActiveControlOperation())
        return Utils::ResultError(Tr::tr("Another controller operation is already active."));

    const bool ownsLease = d->snapshot.session->ownsControlLease;
    const auto state = d->snapshot.controllerState
                           ? std::optional(d->snapshot.controllerState->serviceState)
                           : std::optional<ServiceState>();
    const auto stateIs = [&state](std::initializer_list<ServiceState> allowed) {
        return state && std::find(allowed.begin(), allowed.end(), *state) != allowed.end();
    };
    const bool controllerPackageActive
        = d->snapshot.package
          && d->snapshot.package->controllerState == Data::ControllerPackageState::Active
          && d->snapshot.package->controllerBootId == d->bootId;
    const bool hasPersistentPackageSelector
        = d->persistentPackageSelector
          && d->persistentPackageSelector->bootId == d->bootId
          && d->persistentPackageSelector->slot != Data::ControllerSlot::None
          && d->persistentPackageSelector->generation
          && d->persistentPackageSelector->configurationId;
    if (request.command == Command::AcquireControl) {
        if (ownsLease)
            return Utils::ResultError(Tr::tr("This session already owns the control lease."));
        if (d->snapshot.session->controlLeaseOwnerSessionId) {
            return Utils::ResultError(
                Tr::tr("The controller reports that the control lease is already owned."));
        }
        if (request.leaseDurationMs < 1 || request.leaseDurationMs > 30000) {
            return Utils::ResultError(
                Tr::tr("The control lease duration must be between 1 and 30000 ms."));
        }
    } else if (!ownsLease) {
        return Utils::ResultError(Tr::tr("Acquire the control lease before this operation."));
    }
    if (request.command != Command::AcquireControl
        && (!d->snapshot.controllerState || !d->snapshot.controllerState->ready)) {
        return Utils::ResultError(
            Tr::tr("The controller is not ready for the selected control operation."));
    }

    switch (request.command) {
    case Command::EnterConfigurationMode:
        if (!stateIs(
                {ServiceState::OperationalSafe,
                 ServiceState::Running,
                 ServiceState::Fault,
                 ServiceState::Paused,
                 ServiceState::Shutdown})) {
            return Utils::ResultError(
                Tr::tr("Configuration mode is not allowed from the current controller state."));
        }
        break;
    case Command::DiscoverTopology:
        if (!stateIs({ServiceState::Shutdown})) {
            return Utils::ResultError(
                Tr::tr("Enter configuration mode before scanning the EtherCAT bus."));
        }
        if (!request.firstStationAddress || !request.topologyCapacity
            || request.topologyCapacity > 64) {
            return Utils::ResultError(Tr::tr("The topology scan range is invalid."));
        }
        if (!d->snapshot.package
            || d->snapshot.package->controllerState == Data::ControllerPackageState::Active) {
            return Utils::ResultError(
                Tr::tr("The active controller package must be stopped before scanning the bus."));
        }
        break;
    case Command::RestoreActivePackage:
        if (!stateIs({ServiceState::OperationalSafe, ServiceState::Shutdown})) {
            return Utils::ResultError(
                Tr::tr("The active package cannot be restored from the current state."));
        }
        if (!hasPersistentPackageSelector) {
            return Utils::ResultError(
                Tr::tr("No exact persistent active-package selector is available."));
        }
        break;
    case Command::Start:
    case Command::StartFreeRun:
    case Command::StartDistributedClocks:
        if (!stateIs({ServiceState::OperationalSafe}) || !d->snapshot.controllerState
            || !controllerPackageActive
            || !d->snapshot.controllerState->busOperational
            || !(d->snapshot.controllerState->ethercatAlStateBits & 0x08)
            || !d->snapshot.controllerState->expectedWorkingCounter
            || d->snapshot.controllerState->actualWorkingCounter
                   != d->snapshot.controllerState->expectedWorkingCounter
            || d->snapshot.controllerState->currentFaults
            || d->snapshot.controllerState->latchedFaults) {
            return Utils::ResultError(
                Tr::tr(
                    "Starting requires OP_SAFE, an active package, an operational bus with matching "
                    "working counters, and no faults."));
        }
        break;
    case Command::Pause:
        if (!stateIs({ServiceState::Running}) || !controllerPackageActive) {
            return Utils::ResultError(
                Tr::tr("Pause requires a running controller with an active package."));
        }
        break;
    case Command::Resume:
        if (!stateIs({ServiceState::Paused}) || !controllerPackageActive
            || !d->snapshot.controllerState
            || !d->snapshot.controllerState->busOperational
            || !(d->snapshot.controllerState->ethercatAlStateBits & 0x08)
            || !d->snapshot.controllerState->expectedWorkingCounter
            || d->snapshot.controllerState->actualWorkingCounter
                   != d->snapshot.controllerState->expectedWorkingCounter
            || d->snapshot.controllerState->currentFaults
            || d->snapshot.controllerState->latchedFaults) {
            return Utils::ResultError(
                Tr::tr(
                    "Resume requires PAUSED, an operational bus with matching working counters, "
                    "and no faults."));
        }
        break;
    case Command::ControlledStop:
        if (!stateIs({ServiceState::Running, ServiceState::Paused})
            || !controllerPackageActive) {
            return Utils::ResultError(
                Tr::tr(
                    "Controlled stop requires a running or paused controller with an active "
                    "package."));
        }
        break;
    case Command::AcquireControl:
    case Command::None:
        break;
    case Command::ReleaseControl:
        if (!stateIs({ServiceState::Shutdown, ServiceState::OperationalSafe})) {
            return Utils::ResultError(
                Tr::tr("Release control requires a ready controller in SHUTDOWN or OP_SAFE."));
        }
        break;
    }

    QByteArray payload;
    const auto appendU16 = [&payload](quint16 value) {
        const qsizetype offset = payload.size();
        payload.resize(offset + qsizetype(sizeof(value)));
        qToBigEndian(value, reinterpret_cast<uchar *>(payload.data() + offset));
    };
    const auto appendU32 = [&payload](quint32 value) {
        const qsizetype offset = payload.size();
        payload.resize(offset + qsizetype(sizeof(value)));
        qToBigEndian(value, reinterpret_cast<uchar *>(payload.data() + offset));
    };
    const auto appendU64 = [&payload](quint64 value) {
        const qsizetype offset = payload.size();
        payload.resize(offset + qsizetype(sizeof(value)));
        qToBigEndian(value, reinterpret_cast<uchar *>(payload.data() + offset));
    };
    if (request.command == Command::AcquireControl) {
        appendU32(quint32(request.leaseDurationMs));
    } else if (request.command == Command::DiscoverTopology) {
        appendU16(request.firstStationAddress);
        appendU16(request.topologyCapacity);
        appendU32(0);
    } else if (request.command == Command::RestoreActivePackage) {
        const ProductApiSessionPrivate::PersistentPackageSelector &selector
            = *d->persistentPackageSelector;
        appendU32(
            selector.slot == Data::ControllerSlot::A ? quint32('A') : quint32('B'));
        appendU32(0);
        appendU64(selector.generation);
        appendU64(selector.configurationId);
    }

    ProductApiSessionPrivate::PendingKind kind
        = ProductApiSessionPrivate::PendingKind::ControlCommand;
    bool terminalCommandStatus = true;
    if (request.command == Command::DiscoverTopology) {
        kind = ProductApiSessionPrivate::PendingKind::Topology;
        terminalCommandStatus = false;
        d->snapshot.topology.reset();
    } else if (request.command == Command::RestoreActivePackage) {
        kind = ProductApiSessionPrivate::PendingKind::RestorePackage;
        terminalCommandStatus = false;
    }
    const quint16 firstExpectedStage
        = request.command == Command::AcquireControl || request.command == Command::ReleaseControl
              ? 4
              : 1;
    d->beginControlProgress(request.command);
    const quint64 requestId = d->sendControlRequest(
        request.command,
        kind,
        QByteArrayView(payload),
        firstExpectedStage,
        terminalCommandStatus);
    if (!requestId) {
        d->finishControlProgress(
            Data::ControllerControlState::Failed,
            0,
            false,
            {},
            {},
            Tr::tr("The controller command could not be sent."));
        return Utils::ResultError(Tr::tr("The controller command could not be sent."));
    }
    if (kind == ProductApiSessionPrivate::PendingKind::Topology) {
        auto found = d->pendingRequests.find(requestId);
        if (found != d->pendingRequests.end())
            found->firstStationAddress = request.firstStationAddress;
    }
    return {};
}

void ProductApiSession::shutdown()
{
    if (d->shuttingDown)
        return;
    d->sendBestEffortRelease();
    d->shuttingDown = true;
    d->userDisconnecting = true;
    d->reconnectTimer->stop();
    d->advanceGeneration();
    d->teardownChannels();
    d->clearLiveIdentity();
    d->clearAlarmCheckpoint();
    d->clearResumeCandidate();
    d->markChannelsDisconnected();
    d->snapshot.state = Data::ControllerConnectionState::Disconnected;
}

#ifdef WITH_TESTS
ProductApiSession::EndpointSet ProductApiSession::endpointsForTests() const
{
    return d->endpoints;
}

bool ProductApiSession::isIdleForTests() const
{
    return activeSocketCountForTests() == 0 && pendingRequestCountForTests() == 0
           && !d->reconnectTimer->isActive() && !d->refreshInProgress;
}

int ProductApiSession::activeSocketCountForTests() const
{
    return int(std::count_if(d->channels.cbegin(), d->channels.cend(), [](const auto &value) {
        return value.socket != nullptr;
    }));
}

int ProductApiSession::pendingRequestCountForTests() const
{
    return d->pendingRequests.size();
}

void ProductApiSession::failNextWriteForTests()
{
    d->failNextWriteForTests = true;
}
#endif

} // namespace EtherCAT::ProductApi::Internal
