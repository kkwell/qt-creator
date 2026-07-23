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
    return mask;
}

quint32 readU32(QByteArrayView bytes, qsizetype offset)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.data() + offset));
}

QString statusName(qint32 status)
{
    static constexpr std::array<const char *, 35> names{
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
    };
    if (status <= 0 && status >= -34)
        return QString::fromLatin1(names.at(size_t(-status)));
    return QStringLiteral("PRODUCT_API_STATUS_%1").arg(status);
}

Data::ControllerRetryDisposition retryDisposition(qint32 status)
{
    switch (status) {
    case -2:
    case -7:
    case -8:
    case -12:
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
        QTimer *timer = nullptr;
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
        snapshot.readOnly = true;
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
        for (const PendingRequest &request : std::as_const(pendingRequests)) {
            if (request.timer) {
                request.timer->stop();
                request.timer->deleteLater();
            }
        }
        pendingRequests.clear();
        refreshInProgress = false;
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
        std::optional<quint64> requestId = {})
    {
        terminalFailure(
            Data::ControllerErrorSource::Protocol, role, operation, summary, detail, {}, requestId);
    }

    void failNetwork(Protocol::Role role, Data::ControllerOperation operation, const QString &summary)
    {
        if (shuttingDown || userDisconnecting)
            return;
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
        if (value.socket->write(wire) != wire.size()) {
            failNetwork(role, operation, Tr::tr("The controller request could not be queued."));
            return false;
        }
        return true;
    }

    void addPending(quint64 requestId, PendingRequest request, int timeoutMs)
    {
        request.timer = new QTimer(q);
        request.timer->setSingleShot(true);
        const quint64 expectedGeneration = request.generation;
        const quint64 expectedEpoch = request.channelEpoch;
        const Protocol::Role role = request.role;
        const Data::ControllerOperation operation = request.operation;
        QObject::connect(
            request.timer,
            &QTimer::timeout,
            q,
            [this, requestId, expectedGeneration, expectedEpoch, role, operation] {
                const auto found = pendingRequests.constFind(requestId);
                if (found == pendingRequests.cend() || found->generation != expectedGeneration
                    || found->channelEpoch != expectedEpoch || generation != expectedGeneration) {
                    return;
                }
                failNetwork(role, operation, Tr::tr("The controller response timed out."));
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
        QByteArrayView payload = {})
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
                requestId);
            return 0;
        }

        PendingRequest request;
        request.kind = kind;
        request.role = value.role;
        request.operation = operation;
        request.generation = generation;
        request.channelEpoch = value.epoch;
        request.requestType = type;
        addPending(requestId, request, options.requestTimeoutMs);
        if (!writeFrame(value, wire, value.role, operation)) {
            removePending(requestId);
            return 0;
        }
        return requestId;
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
            session.ownsControlLease = false;
            snapshot.session = session;
            snapshot.protocolVersion = {Protocol::CurrentMajor, negotiatedMinor};

            clearResumeCandidate();
            value.handshaken = true;
            setChannelState(value.role, Data::ControllerChannelState::Connected);
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
        const QString &detail = {})
    {
        setError(
            Data::ControllerErrorSource::Controller,
            request.role,
            request.operation,
            Tr::tr("The controller rejected a read-only request."),
            detail,
            status,
            requestId,
            retryDisposition(status));
        snapshot.lastError->operationResult = operationResult;
        snapshot.lastError->sourceDetail = sourceDetail;
        removePending(requestId);
        if (isReconnectStatus(status)) {
            scheduleReconnect();
        } else {
            snapshot.state = Data::ControllerConnectionState::Degraded;
            refreshInProgress = false;
            publish();
        }
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
            found->timer->start(options.requestTimeoutMs);
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
                found->timer->start(options.requestTimeoutMs);
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

    void beginRefresh()
    {
        if (refreshInProgress || shuttingDown || !sessionId)
            return;
        refreshInProgress = true;
        stateReceived = false;
        capabilityReceived = false;
        packageReceived = false;
        firmwareReceived = negotiatedMinor < 9 || !(featureBits & FeatureFirmwareUpdate);
        subscriptionReceived = negotiatedMinor < 4 || !(featureBits & FeatureExactAlarmReplay);
        subscriptionDegraded = false;
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
        snapshot.state = subscriptionDegraded ? Data::ControllerConnectionState::Degraded
                                              : Data::ControllerConnectionState::Connected;
        if (!snapshot.connectedAt.isValid())
            snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        publish();
    }

    ProductApiSession *q = nullptr;
    ProductApiSession::EndpointSet endpoints;
    ProductApiSession::Options options;
    Data::ControllerConnectionSnapshot snapshot;
    std::array<Channel, 3> channels;
    QHash<quint64, PendingRequest> pendingRequests;
    QTimer *reconnectTimer = nullptr;
    Data::ControllerConnectionRequest currentRequest;
    quint64 generation = 0;
    quint64 nextRequestId = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 resumeSessionId = 0;
    quint64 resumeBootId = 0;
    quint32 featureBits = 0;
    quint32 lastAlarmSequence = 0;
    quint16 negotiatedMinor = 0;
    int reconnectAttempt = 0;
    bool shuttingDown = false;
    bool userDisconnecting = false;
    bool refreshInProgress = false;
    bool stateReceived = false;
    bool capabilityReceived = false;
    bool packageReceived = false;
    bool firmwareReceived = false;
    bool subscriptionReceived = false;
    bool subscriptionDegraded = false;
    bool alarmCheckpointEstablished = false;
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

    d->userDisconnecting = true;
    d->reconnectTimer->stop();
    d->snapshot.state = Data::ControllerConnectionState::Disconnecting;
    d->publish();
    d->advanceGeneration();
    d->teardownChannels();
    d->clearLiveIdentity();
    d->clearAlarmCheckpoint();
    d->clearResumeCandidate();
    d->markChannelsDisconnected();
    d->snapshot.state = Data::ControllerConnectionState::Disconnected;
    d->publish();
    d->userDisconnecting = false;
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

void ProductApiSession::shutdown()
{
    if (d->shuttingDown)
        return;
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
#endif

} // namespace EtherCAT::ProductApi::Internal
