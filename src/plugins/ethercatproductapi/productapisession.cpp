// Copyright (C) 2026 Kvell

#include "productapisession.h"

#include "ethercatproductapiconstants.h"
#include "ethercatproductapitr.h"
#include "productapicodec.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QRandomGenerator>
#include <QSet>
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
constexpr quint32 FeatureControlledFaultReset = 1U << 12;
constexpr int SessionCapacityStatus = -26;
constexpr int EventGapStatus = -25;
constexpr qsizetype AlarmHistoryCapacity = 32;
constexpr quint64 ControllerFaultKnownMask = (quint64(1) << 17) - 1;

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
    case Command::ResetFault:
        return Data::ControllerOperation::ResetFault;
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
    case Command::ResetFault:
        return Protocol::MessageType::ResetFault;
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
    case Command::ResetFault:
        return Tr::tr("Fault reset");
    case Command::None:
        return Tr::tr("No operation");
    }
    return Tr::tr("Unknown operation");
}

bool packageDeploymentIsActive(Data::ControllerPackageDeploymentState state)
{
    using State = Data::ControllerPackageDeploymentState;
    return state == State::Uploading || state == State::Committing || state == State::Validating
           || state == State::Activating || state == State::RollingBack
           || state == State::Canceling;
}

bool operationIsPackageDeployment(Data::ControllerOperation operation)
{
    using Operation = Data::ControllerOperation;
    return operation == Operation::UploadPackage || operation == Operation::AbortPackageUpload
           || operation == Operation::ValidatePackage || operation == Operation::ActivatePackage
           || operation == Operation::RollbackPackage;
}

constexpr qsizetype MaximumPackageBytes = 16 * 1024 * 1024;

template<typename T>
void appendBigEndian(QByteArray &bytes, T value)
{
    const qsizetype offset = bytes.size();
    bytes.resize(offset + qsizetype(sizeof(T)));
    qToBigEndian<T>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

QByteArray packageBeginPayload(quint64 configurationId, quint32 packageBytes)
{
    QByteArray payload;
    payload.reserve(24);
    appendBigEndian(payload, configurationId);
    appendBigEndian(payload, packageBytes);
    appendBigEndian(payload, quint32(0));
    appendBigEndian(payload, quint32(0));
    appendBigEndian(payload, Protocol::PackageUploadMode);
    return payload;
}

QByteArray packageChunkPayload(quint32 offset, QByteArrayView bytes)
{
    QByteArray payload;
    payload.reserve(Protocol::BulkChunkHeaderBytes + bytes.size());
    appendBigEndian(payload, Protocol::PackageObjectKind);
    appendBigEndian(payload, offset);
    payload.append(bytes.data(), bytes.size());
    return payload;
}

QByteArray packageSelectorPayload(const Data::ControllerPackageSelector &selector)
{
    QByteArray payload;
    payload.reserve(24);
    appendBigEndian(payload, selector.slot == Data::ControllerSlot::A ? quint32('A') : quint32('B'));
    appendBigEndian(payload, quint32(0));
    appendBigEndian(payload, selector.generation);
    appendBigEndian(payload, selector.configurationId);
    return payload;
}

QByteArray deploymentFingerprint(const Data::ControllerPackageDeploymentRequest &request)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(request.artifact);
    QByteArray metadata;
    appendBigEndian(metadata, request.configurationId);
    metadata.append(request.activate ? '\x01' : '\x00');
    metadata.append(request.rollbackOnActivationFailure ? '\x01' : '\x00');
    hash.addData(metadata);
    return hash.result();
}

bool operationIdIsValid(const QString &operationId)
{
    if (operationId.size() < 1 || operationId.size() > 128 || operationId.trimmed() != operationId) {
        return false;
    }
    return std::none_of(operationId.cbegin(), operationId.cend(), [](QChar character) {
        return character.isNull() || character.category() == QChar::Other_Control;
    });
}

std::optional<Data::ControllerPackageSelector> activePackageSelector(
    const Data::ControllerPackageSummary &package)
{
    const Data::ControllerPackageSelector selector{
        package.activeSlot,
        package.activeGeneration,
        package.activeConfigurationId,
    };
    return selector.isValid() ? std::optional(selector) : std::nullopt;
}

template<typename T>
QByteArray opaqueBigEndian(T value)
{
    QByteArray bytes(qsizetype(sizeof(T)), '\0');
    qToBigEndian<T>(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

std::optional<Data::ControllerSlot> runtimeResourceSlot(quint32 slot)
{
    if (slot == quint32('A'))
        return Data::ControllerSlot::A;
    if (slot == quint32('B'))
        return Data::ControllerSlot::B;
    return {};
}

quint32 runtimeResourceSlot(Data::ControllerSlot slot)
{
    if (slot == Data::ControllerSlot::A)
        return quint32('A');
    if (slot == Data::ControllerSlot::B)
        return quint32('B');
    return 0;
}

Data::RuntimeResourceCatalogEpoch runtimeResourceEpoch(
    const Protocol::RuntimeResourceBinding &binding)
{
    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = binding.bootId;
    epoch.activePackageSlot
        = runtimeResourceSlot(binding.activeSlot).value_or(Data::ControllerSlot::None);
    epoch.activePackageGeneration = binding.packageGeneration;
    epoch.configurationId = binding.configurationId;
    epoch.topologyGeneration = binding.topologyGeneration;
    epoch.runtimeGeneration = binding.runtimeGeneration;
    epoch.catalogRevision = binding.catalogRevision;
    epoch.topologyIdentity = opaqueBigEndian(binding.topologyIdentity);
    return epoch;
}

std::optional<Data::RuntimeResourcePrimitiveType> runtimeResourcePrimitiveType(
    Protocol::RuntimeResourcePrimitive primitive)
{
    using DataType = Data::RuntimeResourcePrimitiveType;
    using ProtocolType = Protocol::RuntimeResourcePrimitive;
    switch (primitive) {
    case ProtocolType::Boolean:
        return DataType::Boolean;
    case ProtocolType::Unsigned8:
    case ProtocolType::Unsigned16:
    case ProtocolType::Unsigned32:
    case ProtocolType::Unsigned64:
        return DataType::UnsignedInteger;
    case ProtocolType::Signed8:
    case ProtocolType::Signed16:
    case ProtocolType::Signed32:
    case ProtocolType::Signed64:
        return DataType::SignedInteger;
    case ProtocolType::FixedQ32_32:
        return DataType::FloatingPoint;
    case ProtocolType::RawBits:
        return DataType::ByteArray;
    }
    return {};
}

std::optional<Data::RuntimeResourceDirection> runtimeResourceDirection(
    Protocol::RuntimeResourceDirection direction)
{
    if (direction == Protocol::RuntimeResourceDirection::Input)
        return Data::RuntimeResourceDirection::Input;
    if (direction == Protocol::RuntimeResourceDirection::Output)
        return Data::RuntimeResourceDirection::Output;
    return {};
}

std::optional<Data::RuntimeResourceAccess> runtimeResourceAccess(
    Protocol::RuntimeResourceAccess access)
{
    if (access == Protocol::RuntimeResourceAccess::Read)
        return Data::RuntimeResourceAccess::ReadOnly;
    if (access == Protocol::RuntimeResourceAccess::ReadWrite)
        return Data::RuntimeResourceAccess::ReadWrite;
    return {};
}

template<typename T>
std::optional<T> decodeExactBigEndian(QByteArrayView bytes)
{
    if (bytes.size() != qsizetype(sizeof(T)))
        return {};
    return qFromBigEndian<T>(reinterpret_cast<const uchar *>(bytes.data()));
}

QByteArray runtimeResourceValueTypeIdentity(
    Protocol::RuntimeResourcePrimitive primitive, quint16 bitWidth)
{
    return QByteArray("ethercat.runtime.value/primitive-") + QByteArray::number(quint8(primitive))
           + "/bits-" + QByteArray::number(bitWidth);
}

std::optional<Data::RuntimeResourceTypedValue> runtimeResourceTypedValue(
    Protocol::RuntimeResourcePrimitive primitive, quint16 bitWidth, QByteArrayView bytes)
{
    Data::RuntimeResourceTypedValue result;
    const auto primitiveType = runtimeResourcePrimitiveType(primitive);
    if (!primitiveType)
        return {};
    result.primitiveType = *primitiveType;
    result.typeIdentity = runtimeResourceValueTypeIdentity(primitive, bitWidth);

    using ProtocolType = Protocol::RuntimeResourcePrimitive;
    switch (primitive) {
    case ProtocolType::Boolean:
        if (bytes.size() != 1 || (quint8(bytes.front()) != 0 && quint8(bytes.front()) != 1))
            return {};
        result.value = bool(quint8(bytes.front()));
        break;
    case ProtocolType::Unsigned8:
        if (bytes.size() != 1)
            return {};
        result.value = QVariant::fromValue<qulonglong>(quint8(bytes.front()));
        break;
    case ProtocolType::Signed8:
        if (bytes.size() != 1)
            return {};
        result.value = QVariant::fromValue<qlonglong>(qint8(bytes.front()));
        break;
    case ProtocolType::Unsigned16: {
        const auto value = decodeExactBigEndian<quint16>(bytes);
        if (!value)
            return {};
        result.value = QVariant::fromValue<qulonglong>(*value);
        break;
    }
    case ProtocolType::Signed16: {
        const auto value = decodeExactBigEndian<qint16>(bytes);
        if (!value)
            return {};
        result.value = QVariant::fromValue<qlonglong>(*value);
        break;
    }
    case ProtocolType::Unsigned32: {
        const auto value = decodeExactBigEndian<quint32>(bytes);
        if (!value)
            return {};
        result.value = QVariant::fromValue<qulonglong>(*value);
        break;
    }
    case ProtocolType::Signed32: {
        const auto value = decodeExactBigEndian<qint32>(bytes);
        if (!value)
            return {};
        result.value = QVariant::fromValue<qlonglong>(*value);
        break;
    }
    case ProtocolType::Unsigned64: {
        const auto value = decodeExactBigEndian<quint64>(bytes);
        if (!value)
            return {};
        result.value = QVariant::fromValue<qulonglong>(*value);
        break;
    }
    case ProtocolType::Signed64: {
        const auto value = decodeExactBigEndian<qint64>(bytes);
        if (!value)
            return {};
        result.value = QVariant::fromValue<qlonglong>(*value);
        break;
    }
    case ProtocolType::FixedQ32_32: {
        const auto value = decodeExactBigEndian<qint64>(bytes);
        if (!value)
            return {};
        result.value = double(*value) / 4294967296.0;
        result.opaqueRepresentation = QByteArray(bytes.data(), bytes.size());
        break;
    }
    case ProtocolType::RawBits:
        if (bytes.isEmpty() || bytes.size() > 16)
            return {};
        result.value = QByteArray(bytes.data(), bytes.size());
        break;
    }
    return result;
}

std::optional<Data::RuntimeResourceQuality> runtimeResourceQuality(
    Protocol::RuntimeResourceQuality quality)
{
    Data::RuntimeResourceQuality result;
    switch (quality) {
    case Protocol::RuntimeResourceQuality::Good:
        result.state = Data::RuntimeResourceQualityState::Good;
        break;
    case Protocol::RuntimeResourceQuality::Unavailable:
        result.state = Data::RuntimeResourceQualityState::Unavailable;
        break;
    default:
        return {};
    }
    result.flags = quint32(quality);
    result.opaqueCode = opaqueBigEndian(quint32(quality));
    return result;
}

bool sameRuntimeResourceBinding(
    const Protocol::RuntimeResourceBinding &left, const Protocol::RuntimeResourceBinding &right)
{
    return left.bootId == right.bootId && left.activeSlot == right.activeSlot
           && left.packageGeneration == right.packageGeneration
           && left.configurationId == right.configurationId
           && left.topologyGeneration == right.topologyGeneration
           && left.runtimeGeneration == right.runtimeGeneration
           && left.catalogRevision == right.catalogRevision
           && left.topologyIdentity == right.topologyIdentity;
}

bool runtimeResourceBindingMatchesBase(
    const Protocol::RuntimeResourceBinding &binding,
    quint64 bootId,
    const std::optional<Data::ControllerPackageSummary> &package)
{
    if (!package || binding.bootId != bootId)
        return false;
    return binding.activeSlot == runtimeResourceSlot(package->activeSlot)
           && binding.packageGeneration == package->activeGeneration
           && binding.configurationId == package->activeConfigurationId;
}

std::optional<Data::RuntimeResourceDescriptor> runtimeResourceDescriptor(
    const Protocol::RuntimeResourceDescriptor &descriptor)
{
    const auto primitiveType = runtimeResourcePrimitiveType(descriptor.primitive);
    const auto direction = runtimeResourceDirection(descriptor.direction);
    const auto access = runtimeResourceAccess(descriptor.access);
    if (!primitiveType || !direction || !access)
        return {};

    Data::RuntimeResourceDescriptor result;
    result.id = {opaqueBigEndian(descriptor.resourceId)};
    result.componentInstanceId = {opaqueBigEndian(descriptor.componentId)};
    if (descriptor.parentId)
        result.parentInstanceId = {opaqueBigEndian(descriptor.parentId)};
    result.instanceOrdinal = descriptor.ordinal;
    result.consistencyGroupId = {opaqueBigEndian(descriptor.groupId)};
    result.primitiveType = *primitiveType;
    result.valueTypeIdentity
        = runtimeResourceValueTypeIdentity(descriptor.primitive, descriptor.valueBitWidth);
    result.bitWidth = descriptor.valueBitWidth;
    result.direction = *direction;
    result.access = *access;
    result.processImageBitOffset = descriptor.processImageBitOffset;
    result.processImageBitLength = descriptor.processImageBitLength;
    result.qualityMask = descriptor.qualityMask;
    if (!descriptor.safeValue.isEmpty()) {
        const auto safeValue = runtimeResourceTypedValue(
            descriptor.primitive, descriptor.valueBitWidth, descriptor.safeValue);
        if (!safeValue)
            return {};
        result.safeValue = *safeValue;
    }
    return result;
}

} // namespace

class ProductApiSessionPrivate
{
public:
    enum class PendingKind {
        Hello,
        State,
        LiveState,
        Capability,
        Package,
        Firmware,
        RuntimeResourceCatalog,
        RuntimeResourceSnapshot,
        ResumeEvents,
        ResumeReplay,
        ControlCommand,
        Topology,
        RestorePackage,
        Heartbeat,
        PackageBulk,
        PackageCommand,
        PackageRecovery,
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
        quint32 deploymentChunkBytes = 0;
        int responseTimeoutMs = 0;
        bool terminalCommandStatus = false;
        std::optional<Protocol::RuntimeResourceTableQuery> runtimeResourceTableQuery;
        std::optional<Protocol::RuntimeResourceSnapshotQuery> runtimeResourceSnapshotQuery;
        QTimer *timer = nullptr;
    };

    static bool isRuntimeResourceRequest(PendingKind kind)
    {
        return kind == PendingKind::RuntimeResourceCatalog
               || kind == PendingKind::RuntimeResourceSnapshot;
    }

    struct PersistentPackageSelector
    {
        Data::ControllerSlot slot = Data::ControllerSlot::None;
        quint64 generation = 0;
        quint64 configurationId = 0;
        quint64 bootId = 0;
    };

    struct FaultResetConfirmation
    {
        quint64 expectedLatchedFaults = 0;
        quint32 expectedAlarmSequence = 0;
        quint32 clearedAlarmSequence = 0;
        std::optional<Data::ControllerPackageSummary> package;
        Data::ControllerServiceState expectedServiceState
            = Data::ControllerServiceState::Unknown;
        bool applicationActive = false;
        bool busOperational = false;
    };

    struct DeploymentJournalEntry
    {
        QByteArray fingerprint;
        Data::ControllerPackageDeploymentProgress progress;
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

        liveStateTimer = new QTimer(q);
        liveStateTimer->setTimerType(Qt::CoarseTimer);
        QObject::connect(liveStateTimer, &QTimer::timeout, q, [this] {
            sendLiveStateQuery();
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
        liveStateTimer->stop();
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

    void notifyRuntimeResourceCatalogChanged()
    {
        QMetaObject::invokeMethod(
            q, [this] { emit q->runtimeResourceCatalogChanged(); }, Qt::QueuedConnection);
    }

    void notifyRuntimeResourceSnapshotChanged()
    {
        QMetaObject::invokeMethod(
            q, [this] { emit q->runtimeResourceSnapshotChanged(); }, Qt::QueuedConnection);
    }

    void clearRuntimeResourceCache()
    {
        const bool hadCatalog = runtimeCatalog.has_value();
        const bool hadSnapshot = runtimeSnapshot.has_value();
        runtimeCatalog.reset();
        runtimeSnapshot.reset();
        if (hadCatalog)
            notifyRuntimeResourceCatalogChanged();
        if (hadSnapshot)
            notifyRuntimeResourceSnapshotChanged();
    }

    void resetRuntimeResourceRefresh()
    {
        runtimeRefreshInProgress = false;
        runtimeResourceRefreshTimer.invalidate();
        runtimeCatalogResources.clear();
        runtimeCatalogIds.clear();
        runtimeCatalogBinding.reset();
        runtimeCatalogTotalCount = 0;
    }

    void invalidateRuntimeResources()
    {
        resetRuntimeResourceRefresh();
        clearRuntimeResourceCache();
    }

    void invalidateRuntimeResourcesIfBaseChanged()
    {
        if (!runtimeCatalog && !runtimeSnapshot)
            return;
        const Data::RuntimeResourceCatalogEpoch *epoch = nullptr;
        Data::ControllerConnectionScope scope;
        quint64 cacheGeneration = 0;
        if (runtimeCatalog) {
            epoch = &runtimeCatalog->epoch;
            scope = runtimeCatalog->scope;
            cacheGeneration = runtimeCatalog->sessionGeneration;
        } else {
            epoch = &runtimeSnapshot->epoch;
            scope = runtimeSnapshot->scope;
            cacheGeneration = runtimeSnapshot->sessionGeneration;
        }
        const auto activePackage = snapshot.package ? activePackageSelector(*snapshot.package)
                                                    : std::nullopt;
        if (!epoch || scope != snapshot.scope || cacheGeneration != generation
            || epoch->controllerBootId != bootId || !activePackage
            || epoch->activePackageSlot != activePackage->slot
            || epoch->activePackageGeneration != activePackage->generation
            || epoch->configurationId != activePackage->configurationId) {
            invalidateRuntimeResources();
        }
    }

    void failRuntimeResourceQuery(
        Data::ControllerErrorSource source,
        Protocol::Role role,
        Data::ControllerOperation operation,
        const QString &summary,
        const QString &detail = {},
        std::optional<qint32> code = {},
        std::optional<qint32> operationResult = {},
        std::optional<quint64> requestId = {})
    {
        if (requestId)
            removePending(*requestId);
        invalidateRuntimeResources();
        setError(
            source,
            role,
            operation,
            summary,
            detail,
            code,
            requestId,
            code ? retryDisposition(*code) : Data::ControllerRetryDisposition::NotRetryable);
        if (snapshot.lastError)
            snapshot.lastError->operationResult = operationResult;
        if (code && isReconnectStatus(*code)) {
            scheduleReconnect();
            return;
        }
        publish();
    }

    static Data::ControllerOperation deploymentOperation(Protocol::MessageType type)
    {
        switch (type) {
        case Protocol::MessageType::BulkBegin:
        case Protocol::MessageType::BulkChunk:
        case Protocol::MessageType::BulkCommit:
            return Data::ControllerOperation::UploadPackage;
        case Protocol::MessageType::BulkAbort:
            return Data::ControllerOperation::AbortPackageUpload;
        case Protocol::MessageType::ValidatePackage:
            return Data::ControllerOperation::ValidatePackage;
        case Protocol::MessageType::ActivatePackage:
            return Data::ControllerOperation::ActivatePackage;
        case Protocol::MessageType::RollbackPackage:
            return Data::ControllerOperation::RollbackPackage;
        default:
            return Data::ControllerOperation::None;
        }
    }

    void appendDeploymentAudit(
        Data::ControllerOperation operation,
        const QString &detail,
        std::optional<quint64> requestId = {},
        std::optional<qint32> status = {},
        std::optional<qint32> operationResult = {})
    {
        Data::ControllerPackageDeploymentAuditEvent event;
        event.sequence = ++deploymentAuditSequence;
        event.operation = operation;
        event.requestId = requestId;
        event.status = status;
        event.operationResult = operationResult;
        event.detail = detail;
        event.occurredAt = QDateTime::currentDateTimeUtc();
        auto &audit = snapshot.packageDeploymentProgress.audit;
        audit.append(event);
        constexpr qsizetype MaximumAuditEvents = 256;
        if (audit.size() > MaximumAuditEvents)
            audit.remove(0, audit.size() - MaximumAuditEvents);
    }

    void rememberDeploymentJournal()
    {
        const Data::ControllerPackageDeploymentProgress &progress
            = snapshot.packageDeploymentProgress;
        if (progress.operationId.isEmpty() || deploymentFingerprintValue.isEmpty())
            return;
        if (!deploymentJournal.contains(progress.operationId))
            deploymentJournalOrder.append(progress.operationId);
        deploymentJournal.insert(progress.operationId, {deploymentFingerprintValue, progress});
        constexpr qsizetype MaximumJournalEntries = 32;
        while (deploymentJournalOrder.size() > MaximumJournalEntries) {
            const QString oldest = deploymentJournalOrder.takeFirst();
            deploymentJournal.remove(oldest);
        }
    }

    void setDeploymentState(
        Data::ControllerPackageDeploymentState state,
        const QString &detail,
        std::optional<qint32> status = {},
        std::optional<qint32> operationResult = {},
        Data::ControllerOperation operation = Data::ControllerOperation::None,
        std::optional<quint64> requestId = {})
    {
        Data::ControllerPackageDeploymentProgress &progress = snapshot.packageDeploymentProgress;
        progress.state = state;
        progress.status = status;
        progress.operationResult = operationResult;
        progress.detail = detail;
        if (operation != Data::ControllerOperation::None) {
            appendDeploymentAudit(operation, detail, requestId, status, operationResult);
        }
        if (!packageDeploymentIsActive(state)) {
            progress.completedAt = QDateTime::currentDateTimeUtc();
            deploymentArtifact.clear();
            activeDeploymentRequestId = 0;
            deploymentCancelRequested = false;
        }
        rememberDeploymentJournal();
        publish();
    }

    void beginDeployment(
        const Data::ControllerPackageDeploymentRequest &request, const QByteArray &fingerprint)
    {
        deploymentFingerprintValue = fingerprint;
        deploymentArtifact = request.artifact;
        deploymentConfigurationId = request.configurationId;
        deploymentActivate = request.activate;
        deploymentRollbackOnActivationFailure = request.rollbackOnActivationFailure;
        deploymentOffset = 0;
        deploymentCancelRequested = false;
        deploymentFailureStatus.reset();
        deploymentFailureOperationResult.reset();
        deploymentFailureDetail.clear();
        deploymentRollbackRequestSelector.reset();
        deploymentAuditSequence = 0;
        activeDeploymentRequestId = 0;
        snapshot.lastError.reset();

        Data::ControllerPackageDeploymentProgress progress;
        progress.operationId = request.operationId;
        progress.artifactSha256
            = QCryptographicHash::hash(request.artifact, QCryptographicHash::Sha256);
        progress.state = Data::ControllerPackageDeploymentState::Uploading;
        progress.totalBytes = request.artifact.size();
        progress.startedAt = QDateTime::currentDateTimeUtc();
        if (snapshot.package && snapshot.package->activeSlot != Data::ControllerSlot::None
            && snapshot.package->activeGeneration && snapshot.package->activeConfigurationId) {
            progress.previousActive = Data::ControllerPackageSelector{
                snapshot.package->activeSlot,
                snapshot.package->activeGeneration,
                snapshot.package->activeConfigurationId,
            };
        }
        snapshot.packageDeploymentProgress = progress;
        appendDeploymentAudit(
            Data::ControllerOperation::UploadPackage,
            Tr::tr("Package deployment started for operation %1.").arg(request.operationId));
        rememberDeploymentJournal();
        publish();
    }

    void markDeploymentOutcomeUnknown(
        const QString &detail,
        Data::ControllerOperation operation = Data::ControllerOperation::UploadPackage,
        std::optional<quint64> requestId = {})
    {
        if (!packageDeploymentIsActive(snapshot.packageDeploymentProgress.state))
            return;
        setDeploymentState(
            Data::ControllerPackageDeploymentState::OutcomeUnknown,
            detail,
            {},
            {},
            operation,
            requestId                   ? requestId
            : activeDeploymentRequestId ? std::optional(activeDeploymentRequestId)
                                        : std::nullopt);
    }

    void beginControlProgress(Data::ControllerControlCommand command)
    {
        if (command != Data::ControllerControlCommand::ResetFault)
            faultResetConfirmation.reset();
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

    void clearLiveIdentity(bool preserveAlarmHistory = false)
    {
        heartbeatTimer->stop();
        liveStateTimer->stop();
        invalidateRuntimeResources();
        heartbeatRequestId = 0;
        liveStatePollingDegraded = false;
        snapshot.readOnly = false;
        snapshot.session.reset();
        snapshot.protocolVersion = {};
        snapshot.connectedAt = {};
        snapshot.controllerState.reset();
        if (!preserveAlarmHistory)
            snapshot.recentAlarms.clear();
        snapshot.performance.reset();
        snapshot.capability.reset();
        snapshot.package.reset();
        snapshot.firmware.reset();
        snapshot.topology.reset();
        snapshot.lastHeartbeatAt = {};
        sessionId = 0;
        bootId = 0;
        negotiatedMinor = 0;
        featureBits = 0;
        bulkFeatureBits = 0;
        faultResetConfirmation.reset();
    }

    void clearAlarmCheckpoint()
    {
        lastAlarmSequence = 0;
        alarmCheckpointEstablished = false;
        snapshot.recentAlarms.clear();
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
        resetRuntimeResourceRefresh();
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
        if (hasActiveControlOperation()) {
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

    void sendBestEffortDeploymentAbort()
    {
        if (!hasActiveDeployment() || !sessionId || !bootId)
            return;
        const Data::ControllerPackageDeploymentState state
            = snapshot.packageDeploymentProgress.state;
        const bool bulkTransactionMayBeActive
            = state == Data::ControllerPackageDeploymentState::Uploading
              || state == Data::ControllerPackageDeploymentState::Committing
              || state == Data::ControllerPackageDeploymentState::Canceling;
        Channel &bulk = channel(Protocol::Role::Bulk);
        if (bulkTransactionMayBeActive && bulk.handshaken && bulk.socket) {
            Protocol::Error codecError;
            const quint64 requestId = allocateRequestId();
            const QByteArray wire = Protocol::encodeRequest(
                Protocol::MessageType::BulkAbort,
                {},
                sessionId,
                requestId,
                ++bulk.sendSequence,
                bootId,
                negotiatedMinor,
                &codecError);
            if (!wire.isEmpty()) {
                appendDeploymentAudit(
                    Data::ControllerOperation::AbortPackageUpload,
                    Tr::tr("A best-effort package upload abort was queued during shutdown."),
                    requestId);
                bulk.socket->write(wire);
                bulk.socket->flush();
                bulk.socket->waitForBytesWritten(100);
            }
        }
        Data::ControllerPackageDeploymentProgress &progress = snapshot.packageDeploymentProgress;
        progress.state = Data::ControllerPackageDeploymentState::OutcomeUnknown;
        progress.detail
            = bulkTransactionMayBeActive
                  ? Tr::tr("The session shut down before the package upload abort was confirmed.")
                  : Tr::tr(
                        "The session shut down before the package operation result was confirmed.");
        progress.completedAt = QDateTime::currentDateTimeUtc();
        deploymentArtifact.clear();
        activeDeploymentRequestId = 0;
        deploymentCancelRequested = false;
        rememberDeploymentJournal();
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
        clearResumeCandidate();
        markChannelsDisconnected();
        snapshot.state = Data::ControllerConnectionState::Disconnected;
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
        if (operationIsPackageDeployment(operation) || hasActiveDeployment()) {
            if (requestMayHaveReachedController) {
                markDeploymentOutcomeUnknown(
                    Tr::tr(
                        "The deployment result is unknown because the controller returned an "
                        "invalid protocol response."),
                    operation,
                    requestId);
            } else {
                setDeploymentState(
                    Data::ControllerPackageDeploymentState::Failed,
                    Tr::tr("The deployment request was not queued."),
                    {},
                    {},
                    operation,
                    requestId);
            }
        }
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
        if (operationIsPackageDeployment(operation) || hasActiveDeployment()) {
            if (requestMayHaveReachedController) {
                markDeploymentOutcomeUnknown(
                    Tr::tr(
                        "The deployment result is unknown because the connection ended before "
                        "confirmation."),
                    operation,
                    requestId);
            } else {
                setDeploymentState(
                    Data::ControllerPackageDeploymentState::Failed,
                    Tr::tr("The deployment request was not queued because the connection failed."),
                    {},
                    {},
                    operation,
                    requestId);
            }
        }
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
        clearLiveIdentity(hasResumeIdentity);
        if (!hasResumeIdentity)
            clearAlarmCheckpoint();
        markChannelsDisconnected();

        ++reconnectAttempt;
        if (reconnectAttempt > options.reconnectAttempts) {
            clearResumeCandidate();
            snapshot.recentAlarms.clear();
            snapshot.state = Data::ControllerConnectionState::Disconnected;
            publish();
            return;
        }

        snapshot.state = Data::ControllerConnectionState::Connecting;
        reconnectTimer->start(std::max(reconnectDelayMs(), minimumDelayMs));
        publish();
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
                if (isRuntimeResourceRequest(kind)) {
                    failRuntimeResourceQuery(
                        Data::ControllerErrorSource::Network,
                        role,
                        operation,
                        Tr::tr("The runtime resource query timed out."),
                        {},
                        {},
                        {},
                        requestId);
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

    bool sendRuntimeResourceTableQuery(const Protocol::RuntimeResourceTableQuery &query)
    {
        Channel &bulk = channel(Protocol::Role::Bulk);
        if (!bulk.handshaken || !bulk.socket) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Network,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceCatalog,
                Tr::tr("The controller Bulk channel is not connected."));
            return false;
        }
        const qint64 remainingMs
            = runtimeResourceRefreshTimer.isValid()
                  ? qint64(options.runtimeResourceRefreshTimeoutMs)
                        - runtimeResourceRefreshTimer.elapsed()
                  : 0;
        if (remainingMs <= 0) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Network,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceCatalog,
                Tr::tr("The runtime resource refresh exceeded its total time limit."));
            return false;
        }
        Protocol::Error codecError;
        const quint64 requestId = allocateRequestId();
        const QByteArray wire = Protocol::encodeQueryResourceTable(
            query, sessionId, requestId, ++bulk.sendSequence, negotiatedMinor, &codecError);
        if (wire.isEmpty()) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::ClientConfiguration,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceCatalog,
                Tr::tr("The runtime resource catalog request could not be encoded."),
                codecError.text,
                {},
                {},
                requestId);
            return false;
        }

        PendingRequest request;
        request.kind = PendingKind::RuntimeResourceCatalog;
        request.role = Protocol::Role::Bulk;
        request.operation = Data::ControllerOperation::QueryRuntimeResourceCatalog;
        request.generation = generation;
        request.channelEpoch = bulk.epoch;
        request.requestType = Protocol::MessageType::QueryResourceTable;
        request.runtimeResourceTableQuery = query;
        addPending(
            requestId,
            request,
            int(std::min<qint64>(options.requestTimeoutMs, remainingMs)));
        if (!writeFrame(
                bulk,
                wire,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceCatalog)) {
            return false;
        }
        return true;
    }

    bool sendRuntimeResourceSnapshotQuery(const Protocol::RuntimeResourceSnapshotQuery &query)
    {
        Channel &bulk = channel(Protocol::Role::Bulk);
        if (!bulk.handshaken || !bulk.socket) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Network,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceSnapshot,
                Tr::tr("The controller Bulk channel is not connected."));
            return false;
        }
        const qint64 remainingMs
            = runtimeResourceRefreshTimer.isValid()
                  ? qint64(options.runtimeResourceRefreshTimeoutMs)
                        - runtimeResourceRefreshTimer.elapsed()
                  : 0;
        if (remainingMs <= 0) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Network,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceSnapshot,
                Tr::tr("The runtime resource refresh exceeded its total time limit."));
            return false;
        }
        Protocol::Error codecError;
        const quint64 requestId = allocateRequestId();
        const QByteArray wire = Protocol::encodeGetResourceSnapshot(
            query, sessionId, requestId, ++bulk.sendSequence, negotiatedMinor, &codecError);
        if (wire.isEmpty()) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::ClientConfiguration,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceSnapshot,
                Tr::tr("The runtime resource snapshot request could not be encoded."),
                codecError.text,
                {},
                {},
                requestId);
            return false;
        }

        PendingRequest request;
        request.kind = PendingKind::RuntimeResourceSnapshot;
        request.role = Protocol::Role::Bulk;
        request.operation = Data::ControllerOperation::QueryRuntimeResourceSnapshot;
        request.generation = generation;
        request.channelEpoch = bulk.epoch;
        request.requestType = Protocol::MessageType::GetResourceSnapshot;
        request.runtimeResourceSnapshotQuery = query;
        addPending(
            requestId,
            request,
            int(std::min<qint64>(options.requestTimeoutMs, remainingMs)));
        if (!writeFrame(
                bulk,
                wire,
                Protocol::Role::Bulk,
                Data::ControllerOperation::QueryRuntimeResourceSnapshot)) {
            return false;
        }
        return true;
    }

    bool beginRuntimeResourceRefresh()
    {
        if (runtimeRefreshInProgress || !snapshot.package || !sessionId || !bootId)
            return false;
        const auto activePackage = activePackageSelector(*snapshot.package);
        if (!activePackage)
            return false;

        runtimeRefreshInProgress = true;
        runtimeResourceRefreshTimer.start();
        runtimeCatalogResources.clear();
        runtimeCatalogIds.clear();
        runtimeCatalogBinding.reset();
        runtimeCatalogTotalCount = 0;
        const bool clearedError
            = snapshot.lastError
              && (snapshot.lastError->operation
                      == Data::ControllerOperation::QueryRuntimeResourceCatalog
                  || snapshot.lastError->operation
                         == Data::ControllerOperation::QueryRuntimeResourceSnapshot);
        if (clearedError)
            snapshot.lastError.reset();

        Protocol::RuntimeResourceTableQuery query;
        query.binding.bootId = bootId;
        query.binding.activeSlot = runtimeResourceSlot(activePackage->slot);
        query.limit = 64;
        query.flags = 1;
        query.cursor = 0;
        const bool sent = sendRuntimeResourceTableQuery(query);
        if (sent && clearedError)
            publish();
        return sent;
    }

    quint64 sendDeploymentRequest(
        Channel &value, Protocol::MessageType type, PendingKind kind, QByteArrayView payload)
    {
        const Data::ControllerOperation operation = deploymentOperation(type);
        const int timeoutMs = std::max(options.requestTimeoutMs, 40000);
        const quint64 requestId = sendRequest(value, type, kind, operation, payload, timeoutMs);
        if (!requestId)
            return 0;
        activeDeploymentRequestId = requestId;
        if (value.role == Protocol::Role::Control)
            extendPendingHeartbeatTimeout(timeoutMs);
        appendDeploymentAudit(
            operation,
            Tr::tr("%1 request queued.").arg(QString::number(quint16(type), 16)),
            requestId);
        publish();
        return requestId;
    }

    bool sendDeploymentBegin()
    {
        const QByteArray payload
            = packageBeginPayload(deploymentConfigurationId, quint32(deploymentArtifact.size()));
        return sendDeploymentRequest(
                   channel(Protocol::Role::Bulk),
                   Protocol::MessageType::BulkBegin,
                   PendingKind::PackageBulk,
                   payload)
               != 0;
    }

    bool sendDeploymentChunk()
    {
        if (deploymentOffset < 0 || deploymentOffset >= deploymentArtifact.size())
            return false;
        const qsizetype chunkBytes = std::min(
            qsizetype(Protocol::BulkChunkMaximumBytes),
            deploymentArtifact.size() - deploymentOffset);
        const QByteArray payload = packageChunkPayload(
            quint32(deploymentOffset),
            QByteArrayView(deploymentArtifact).sliced(deploymentOffset, chunkBytes));
        const quint64 requestId = sendDeploymentRequest(
            channel(Protocol::Role::Bulk),
            Protocol::MessageType::BulkChunk,
            PendingKind::PackageBulk,
            payload);
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return false;
        found->deploymentChunkBytes = quint32(chunkBytes);
        return true;
    }

    bool sendDeploymentCommit()
    {
        snapshot.packageDeploymentProgress.state
            = Data::ControllerPackageDeploymentState::Committing;
        snapshot.packageDeploymentProgress.detail = Tr::tr(
            "Committing the uploaded controller package.");
        publish();
        return sendDeploymentRequest(
                   channel(Protocol::Role::Bulk),
                   Protocol::MessageType::BulkCommit,
                   PendingKind::PackageBulk,
                   {})
               != 0;
    }

    bool sendDeploymentCommand(
        Protocol::MessageType type,
        std::optional<Data::ControllerPackageSelector> requestedSelector = {})
    {
        const auto selector = requestedSelector ? requestedSelector
                                                : snapshot.packageDeploymentProgress.candidate;
        if (!selector || !selector->isValid())
            return false;
        switch (type) {
        case Protocol::MessageType::ValidatePackage:
            snapshot.packageDeploymentProgress.state
                = Data::ControllerPackageDeploymentState::Validating;
            snapshot.packageDeploymentProgress.detail = Tr::tr(
                "Validating the staged controller package.");
            break;
        case Protocol::MessageType::ActivatePackage:
            snapshot.packageDeploymentProgress.state
                = Data::ControllerPackageDeploymentState::Activating;
            snapshot.packageDeploymentProgress.detail = Tr::tr(
                "Activating the validated controller package.");
            break;
        case Protocol::MessageType::RollbackPackage:
            snapshot.packageDeploymentProgress.state
                = Data::ControllerPackageDeploymentState::RollingBack;
            snapshot.packageDeploymentProgress.detail = Tr::tr(
                "Rolling back after package activation failed.");
            break;
        default:
            return false;
        }
        publish();
        const QByteArray payload = packageSelectorPayload(*selector);
        const quint64 requestId = sendDeploymentRequest(
            channel(Protocol::Role::Control), type, PendingKind::PackageCommand, payload);
        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return false;
        found->nextExpectedStage = 1;
        return true;
    }

    bool sendDeploymentRecoveryQuery()
    {
        snapshot.packageDeploymentProgress.state
            = Data::ControllerPackageDeploymentState::RollingBack;
        snapshot.packageDeploymentProgress.detail = Tr::tr(
            "Activation failed; confirming the exact active package before rollback.");
        publish();
        const int timeoutMs = std::max(options.requestTimeoutMs, 10000);
        const quint64 requestId = sendRequest(
            channel(Protocol::Role::Control),
            Protocol::MessageType::GetPackageState,
            PendingKind::PackageRecovery,
            Data::ControllerOperation::QueryPackageState,
            {},
            timeoutMs);
        if (!requestId)
            return false;
        activeDeploymentRequestId = requestId;
        extendPendingHeartbeatTimeout(timeoutMs);
        appendDeploymentAudit(
            Data::ControllerOperation::QueryPackageState,
            Tr::tr("Authoritative package-state recovery query queued."),
            requestId);
        publish();
        return true;
    }

    bool sendDeploymentAbort(bool userCanceled)
    {
        deploymentCancelRequested = userCanceled;
        snapshot.packageDeploymentProgress.state = Data::ControllerPackageDeploymentState::Canceling;
        snapshot.packageDeploymentProgress.detail
            = userCanceled ? Tr::tr("Canceling the package upload.")
                           : Tr::tr("Aborting the failed package upload.");
        publish();
        return sendDeploymentRequest(
                   channel(Protocol::Role::Bulk),
                   Protocol::MessageType::BulkAbort,
                   PendingKind::PackageBulk,
                   {})
               != 0;
    }

    int heartbeatResponseTimeoutMs() const
    {
        int timeoutMs = options.requestTimeoutMs;
        for (const PendingRequest &request : std::as_const(pendingRequests)) {
            if ((request.kind == PendingKind::ControlCommand
                 || request.kind == PendingKind::Topology
                 || request.kind == PendingKind::RestorePackage
                 || request.kind == PendingKind::PackageCommand
                 || request.kind == PendingKind::PackageRecovery)
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
            if (value.role == Protocol::Role::Bulk)
                bulkFeatureBits = ack->featureBits;
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
        } else if (
            request.controlCommand == Data::ControllerControlCommand::ResetFault
            && status == -15 && operationResult == -6 && sourceDetail) {
            rejectionDetail
                = Tr::tr(
                      "ERR_FAULT_ACTIVE (-6): current fault mask 0x%1 is still active. Resolve "
                      "the cause before confirming the latch.")
                      .arg(*sourceDetail, 5, 16, QLatin1Char('0'));
        } else if (
            request.controlCommand == Data::ControllerControlCommand::ResetFault
            && status == -15 && operationResult == -9 && sourceDetail) {
            rejectionDetail
                = Tr::tr(
                      "ERR_STALE_CONFIRMATION (-9): the latest alarm sequence is %1. Refresh "
                      "diagnostics and confirm the new snapshot.")
                      .arg(*sourceDetail);
        } else if (
            request.controlCommand == Data::ControllerControlCommand::ResetFault
            && status == -15 && operationResult == -3) {
            rejectionDetail = Tr::tr(
                "ERR_STATE (-3): fault reset requires the controller to remain in Fault state.");
        } else if (
            request.controlCommand == Data::ControllerControlCommand::ResetFault
            && status == -6 && operationResult == -4) {
            rejectionDetail = Tr::tr(
                "ERR_ARGUMENT (-4): the controller rejected the fault confirmation payload.");
        } else if (
            request.controlCommand == Data::ControllerControlCommand::ResetFault
            && status == -14 && operationResult == -7) {
            rejectionDetail = Tr::tr(
                "ERR_UNSUPPORTED (-7): the controller does not support controlled fault reset.");
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
            invalidateRuntimeResources();
            snapshot.controllerState.reset();
            snapshot.package.reset();
            switch (request.kind) {
            case PendingKind::State:
                stateReceived = true;
                break;
            case PendingKind::LiveState:
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
        } else if (request.kind == PendingKind::LiveState) {
            liveStatePollingDegraded = true;
            snapshot.state = Data::ControllerConnectionState::Degraded;
            publish();
        } else if (!controlRequest && !heartbeat) {
            if (refreshInProgress)
                finishRefreshIfReady();
            else {
                invalidateRuntimeResources();
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
        } else if (
            command == Data::ControllerControlCommand::ResetFault
            && faultResetConfirmation) {
            faultResetConfirmation->clearedAlarmSequence = quint32(status.detail);
        }
        if (command != Data::ControllerControlCommand::AcquireControl
            && command != Data::ControllerControlCommand::ReleaseControl) {
            invalidateRuntimeResources();
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

    void finishDeploymentFailure(
        const PendingRequest &request,
        quint64 requestId,
        qint32 status,
        qint32 operationResult,
        const QString &detail)
    {
        setError(
            Data::ControllerErrorSource::Controller,
            request.role,
            request.operation,
            Tr::tr("The controller rejected the package deployment request."),
            detail,
            status,
            requestId,
            retryDisposition(status));
        snapshot.lastError->operationResult = operationResult;
        deploymentFailureStatus = status;
        deploymentFailureOperationResult = operationResult;
        deploymentFailureDetail
            = detail.isEmpty()
                  ? Tr::tr("Package deployment failed with %1.").arg(statusName(status))
                  : detail;
        if (invalidatesControlLease(status)) {
            heartbeatTimer->stop();
            if (snapshot.session) {
                snapshot.session->controlLeaseOwnerSessionId = 0;
                snapshot.session->ownsControlLease = false;
            }
        }
        removePending(requestId);
        activeDeploymentRequestId = 0;

        if (isReconnectStatus(status)) {
            markDeploymentOutcomeUnknown(
                Tr::tr(
                    "The deployment result is unknown because the controller session became "
                    "stale."));
            scheduleReconnect();
            return;
        }

        if (request.kind == PendingKind::PackageBulk
            && request.requestType != Protocol::MessageType::BulkBegin
            && request.requestType != Protocol::MessageType::BulkAbort
            && channel(Protocol::Role::Bulk).handshaken) {
            if (sendDeploymentAbort(false))
                return;
        }

        if (request.requestType == Protocol::MessageType::ActivatePackage
            && deploymentRollbackOnActivationFailure
            && snapshot.packageDeploymentProgress.previousActive
            && snapshot.packageDeploymentProgress.previousActive->isValid() && snapshot.session
            && snapshot.session->ownsControlLease && channel(Protocol::Role::Control).handshaken) {
            if (sendDeploymentRecoveryQuery())
                return;
            deploymentFailureDetail
                = Tr::tr("%1 The active package could not be confirmed, so no rollback was sent.")
                      .arg(deploymentFailureDetail);
        }

        setDeploymentState(
            Data::ControllerPackageDeploymentState::Failed,
            deploymentFailureDetail,
            status,
            operationResult,
            request.operation,
            requestId);
    }

    bool handlePackageBulkStatus(
        const Protocol::Frame &frame,
        const PendingRequest &request,
        quint64 requestId,
        QString *protocolDetail)
    {
        Protocol::Error decodeError;
        const auto status = Protocol::decodeBulkStatus(frame, &decodeError);
        if (!status) {
            *protocolDetail = decodeError.text;
            return false;
        }
        if (status->originalType != quint16(request.requestType)) {
            *protocolDetail = Tr::tr("BulkStatus does not match the deployment request.");
            return false;
        }
        if (status->status) {
            finishDeploymentFailure(
                request,
                requestId,
                status->status,
                status->operationResult,
                Tr::tr("%1 was rejected by the controller.").arg(statusName(status->status)));
            return true;
        }

        removePending(requestId);
        activeDeploymentRequestId = 0;
        appendDeploymentAudit(
            request.operation,
            Tr::tr("The controller accepted the package deployment request."),
            requestId,
            status->status,
            status->operationResult);

        if (request.requestType == Protocol::MessageType::BulkAbort) {
            const bool canceled = deploymentCancelRequested;
            setDeploymentState(
                canceled ? Data::ControllerPackageDeploymentState::Canceled
                         : Data::ControllerPackageDeploymentState::Failed,
                canceled ? Tr::tr("Package deployment was canceled before activation.")
                         : deploymentFailureDetail,
                deploymentFailureStatus,
                deploymentFailureOperationResult,
                Data::ControllerOperation::AbortPackageUpload,
                requestId);
            return true;
        }

        if (request.requestType == Protocol::MessageType::BulkBegin) {
            return deploymentCancelRequested ? sendDeploymentAbort(true) : sendDeploymentChunk();
        }

        if (request.requestType == Protocol::MessageType::BulkChunk) {
            deploymentOffset += request.deploymentChunkBytes;
            snapshot.packageDeploymentProgress.transferredBytes = deploymentOffset;
            snapshot.packageDeploymentProgress.detail = Tr::tr("Uploaded %1 of %2 bytes.")
                                                            .arg(deploymentOffset)
                                                            .arg(deploymentArtifact.size());
            publish();
            if (deploymentCancelRequested)
                return sendDeploymentAbort(true);
            if (deploymentOffset < deploymentArtifact.size())
                return sendDeploymentChunk();
            return sendDeploymentCommit();
        }

        if (request.requestType == Protocol::MessageType::BulkCommit) {
            const Data::ControllerSlot slot = status->selectedSlot == quint32('A')
                                                  ? Data::ControllerSlot::A
                                              : status->selectedSlot == quint32('B')
                                                  ? Data::ControllerSlot::B
                                                  : Data::ControllerSlot::None;
            const Data::ControllerPackageSelector selector{
                slot,
                status->generation,
                status->configurationId,
            };
            if (!selector.isValid() || selector.configurationId != deploymentConfigurationId) {
                *protocolDetail = Tr::tr("BulkCommit returned an invalid package selector.");
                return false;
            }
            snapshot.packageDeploymentProgress.candidate = selector;
            snapshot.packageDeploymentProgress.transferredBytes
                = snapshot.packageDeploymentProgress.totalBytes;
            if (deploymentCancelRequested)
                return sendDeploymentAbort(true);
            if (!sendDeploymentCommand(Protocol::MessageType::ValidatePackage)) {
                *protocolDetail = Tr::tr(
                    "The committed package could not be queued for validation.");
                return false;
            }
            return true;
        }

        *protocolDetail = Tr::tr("An unexpected bulk deployment response was received.");
        return false;
    }

    bool handlePackageCommandStatus(
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
            *protocolDetail = Tr::tr("CommandStatus does not match the package request.");
            return false;
        }
        if (status->status) {
            finishDeploymentFailure(
                request,
                requestId,
                status->status,
                status->operationResult,
                Tr::tr("%1 was rejected by the controller.").arg(statusName(status->status)));
            return true;
        }
        if (status->stage != request.nextExpectedStage || status->final) {
            *protocolDetail = Tr::tr(
                "Package CommandStatus stages violate the deployment contract.");
            return false;
        }

        auto found = pendingRequests.find(requestId);
        if (found == pendingRequests.end())
            return true;
        if (found->timer)
            found->timer->start(found->responseTimeoutMs);
        extendPendingHeartbeatTimeout(found->responseTimeoutMs);
        if (found->nextExpectedStage <= 4)
            ++found->nextExpectedStage;
        appendDeploymentAudit(
            request.operation,
            Tr::tr("Package command stage %1 of 4 completed.").arg(status->stage),
            requestId,
            status->status,
            status->operationResult);
        publish();
        return true;
    }

    bool handleDeploymentRecoveryPackageState(
        const Protocol::Frame &frame,
        const PendingRequest &request,
        quint64 requestId,
        QString *protocolDetail)
    {
        if (frame.payload.size() < 2
            || qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(frame.payload.constData()))
                   != quint16(Protocol::MessageType::GetPackageState)) {
            *protocolDetail = Tr::tr("The recovery PackageState does not match GetPackageState.");
            return false;
        }
        Protocol::Error decodeError;
        const auto package = Protocol::decodePackageState(frame, &decodeError);
        if (!package) {
            if (decodeError.category != Protocol::ErrorCategory::ControllerStatus
                || !decodeError.status) {
                *protocolDetail = decodeError.text;
                return false;
            }
            setError(
                Data::ControllerErrorSource::Controller,
                request.role,
                request.operation,
                Tr::tr("The controller rejected the package-state recovery query."),
                decodeError.text,
                *decodeError.status,
                requestId,
                retryDisposition(*decodeError.status));
            snapshot.lastError->operationResult = decodeError.operationResult.value_or(0);
            removePending(requestId);
            activeDeploymentRequestId = 0;
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Failed,
                Tr::tr("%1 The active package could not be confirmed, so no rollback was sent.")
                    .arg(deploymentFailureDetail),
                deploymentFailureStatus,
                deploymentFailureOperationResult,
                Data::ControllerOperation::QueryPackageState,
                requestId);
            return true;
        }

        snapshot.package = *package;
        invalidateRuntimeResourcesIfBaseChanged();
        rememberPersistentPackageSelector(*package);
        removePending(requestId);
        activeDeploymentRequestId = 0;

        const auto currentActive = activePackageSelector(*package);
        const auto candidate = snapshot.packageDeploymentProgress.candidate;
        const auto previousActive = snapshot.packageDeploymentProgress.previousActive;
        if (!currentActive || !candidate || !previousActive) {
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Failed,
                Tr::tr(
                    "%1 No exact active/fallback package pair is available, so no rollback was "
                    "sent.")
                    .arg(deploymentFailureDetail),
                deploymentFailureStatus,
                deploymentFailureOperationResult,
                Data::ControllerOperation::QueryPackageState,
                requestId);
            return true;
        }

        const bool candidateConfirmedActive = *currentActive == *candidate
                                              && package->controllerState
                                                     == Data::ControllerPackageState::Active;
        const bool previousStillActive = *currentActive == *previousActive;
        if (!candidateConfirmedActive && !previousStillActive) {
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Failed,
                Tr::tr(
                    "%1 The current active package is neither the candidate nor the confirmed "
                    "fallback, so no rollback was sent.")
                    .arg(deploymentFailureDetail),
                deploymentFailureStatus,
                deploymentFailureOperationResult,
                Data::ControllerOperation::QueryPackageState,
                requestId);
            return true;
        }

        deploymentRollbackRequestSelector = currentActive;
        appendDeploymentAudit(
            Data::ControllerOperation::RollbackPackage,
            Tr::tr("Confirmed the exact active selector before rollback."),
            requestId,
            0,
            package->controllerResult);
        if (!sendDeploymentCommand(
                Protocol::MessageType::RollbackPackage, deploymentRollbackRequestSelector)) {
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Failed,
                Tr::tr("%1 The confirmed rollback request could not be queued.")
                    .arg(deploymentFailureDetail),
                deploymentFailureStatus,
                deploymentFailureOperationResult,
                Data::ControllerOperation::RollbackPackage);
        }
        return true;
    }

    bool handlePackageStateResult(
        const Protocol::Frame &frame,
        const PendingRequest &request,
        quint64 requestId,
        QString *protocolDetail)
    {
        if (request.nextExpectedStage != 5) {
            *protocolDetail = Tr::tr(
                "PackageState arrived before all package command stages completed.");
            return false;
        }
        if (frame.payload.size() < 2
            || qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(frame.payload.constData()))
                   != quint16(request.requestType)) {
            *protocolDetail = Tr::tr("PackageState does not match the package request.");
            return false;
        }
        Protocol::Error decodeError;
        const auto package = Protocol::decodePackageState(frame, &decodeError);
        if (!package) {
            if (decodeError.category == Protocol::ErrorCategory::ControllerStatus
                && decodeError.status) {
                setError(
                    Data::ControllerErrorSource::Controller,
                    request.role,
                    request.operation,
                    Tr::tr("The controller could not return a coherent final package state."),
                    decodeError.text,
                    *decodeError.status,
                    requestId,
                    retryDisposition(*decodeError.status));
                snapshot.lastError->operationResult = decodeError.operationResult.value_or(0);
                removePending(requestId);
                activeDeploymentRequestId = 0;
                markDeploymentOutcomeUnknown(
                    Tr::tr(
                        "All package command stages completed, but the final package state was "
                        "not coherent. Authoritative state is being refreshed before any further "
                        "action."),
                    request.operation,
                    requestId);
                beginRefresh(true);
                return true;
            }
            *protocolDetail = decodeError.text;
            return false;
        }

        const auto candidate = snapshot.packageDeploymentProgress.candidate;
        if (!candidate || !candidate->isValid()) {
            *protocolDetail = Tr::tr("No committed package selector is available.");
            return false;
        }
        snapshot.package = *package;
        invalidateRuntimeResourcesIfBaseChanged();
        rememberPersistentPackageSelector(*package);
        removePending(requestId);
        activeDeploymentRequestId = 0;
        appendDeploymentAudit(
            request.operation,
            Tr::tr("The controller returned the authoritative package state."),
            requestId,
            0,
            package->controllerResult);

        if (request.requestType == Protocol::MessageType::ValidatePackage) {
            const bool candidateAccepted
                = package->stagedSlot == candidate->slot
                  && package->stagedGeneration == candidate->generation
                  && package->stagedConfigurationId == candidate->configurationId
                  && (package->controllerState == Data::ControllerPackageState::Accepted
                      || package->controllerState == Data::ControllerPackageState::Active);
            if (!candidateAccepted) {
                *protocolDetail = Tr::tr(
                    "The validated package state does not match the committed package.");
                return false;
            }
            if (deploymentActivate)
                return sendDeploymentCommand(Protocol::MessageType::ActivatePackage);
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Succeeded,
                Tr::tr("The controller package was uploaded and validated."),
                0,
                package->controllerResult,
                Data::ControllerOperation::ValidatePackage,
                requestId);
            return true;
        }

        if (request.requestType == Protocol::MessageType::ActivatePackage) {
            const bool candidateActive = package->activeSlot == candidate->slot
                                         && package->activeGeneration == candidate->generation
                                         && package->activeConfigurationId
                                                == candidate->configurationId
                                         && package->controllerState
                                                == Data::ControllerPackageState::Active;
            if (!candidateActive) {
                *protocolDetail = Tr::tr(
                    "The active package state does not match the deployed package.");
                return false;
            }
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Succeeded,
                Tr::tr("The controller package was uploaded, validated, and activated."),
                0,
                package->controllerResult,
                Data::ControllerOperation::ActivatePackage,
                requestId);
            beginRefresh();
            return true;
        }

        if (request.requestType == Protocol::MessageType::RollbackPackage) {
            const auto restored = activePackageSelector(*package);
            const auto previousActive = snapshot.packageDeploymentProgress.previousActive;
            if (!restored || !previousActive || *restored != *previousActive
                || package->controllerState != Data::ControllerPackageState::Active) {
                *protocolDetail = Tr::tr(
                    "Rollback did not restore the exact previously confirmed active package.");
                return false;
            }
            setDeploymentState(
                Data::ControllerPackageDeploymentState::Failed,
                deploymentFailureDetail.isEmpty()
                    ? Tr::tr("Package activation failed and rollback completed.")
                    : Tr::tr("%1 Rollback completed.").arg(deploymentFailureDetail),
                deploymentFailureStatus,
                deploymentFailureOperationResult,
                Data::ControllerOperation::RollbackPackage,
                requestId);
            beginRefresh(true);
            return true;
        }

        *protocolDetail = Tr::tr("An unexpected package-state response was received.");
        return false;
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

    void handleRuntimeResourceStatus(
        const Protocol::Frame &frame, const PendingRequest &request, quint64 requestId)
    {
        Protocol::Error decodeError;
        const auto status = Protocol::decodeBulkStatus(frame, &decodeError);
        if (!status || !status->status || status->originalType != quint16(request.requestType)
            || request.role != Protocol::Role::Bulk) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The controller returned a malformed runtime resource status."),
                decodeError.text,
                requestId);
            return;
        }
        failRuntimeResourceQuery(
            Data::ControllerErrorSource::Controller,
            request.role,
            request.operation,
            Tr::tr("The controller rejected the runtime resource query."),
            {},
            status->status,
            status->operationResult,
            requestId);
    }

    void handleRuntimeResourceCatalog(
        const Protocol::Frame &frame, const PendingRequest &request, quint64 requestId)
    {
        if (!runtimeRefreshInProgress || !request.runtimeResourceTableQuery) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The runtime resource catalog request context is missing."),
                {},
                requestId);
            return;
        }

        Protocol::Error decodeError;
        const auto page = Protocol::decodeResourceTablePage(
            frame, *request.runtimeResourceTableQuery, &decodeError);
        if (!page) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The controller returned an invalid runtime resource catalog page."),
                decodeError.text,
                requestId);
            return;
        }
        if (page->status) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Controller,
                request.role,
                request.operation,
                Tr::tr("The controller rejected the runtime resource catalog query."),
                {},
                page->status,
                {},
                requestId);
            return;
        }
        if (!runtimeResourceRefreshTimer.isValid()
            || runtimeResourceRefreshTimer.hasExpired(
                options.runtimeResourceRefreshTimeoutMs)) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Network,
                request.role,
                request.operation,
                Tr::tr("The runtime resource refresh exceeded its total time limit."),
                {},
                {},
                {},
                requestId);
            return;
        }
        if (page->totalCount > options.maximumRuntimeResourceCount) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::ClientConfiguration,
                request.role,
                request.operation,
                Tr::tr("The runtime resource catalog exceeds the client resource limit."),
                {},
                {},
                {},
                requestId);
            return;
        }
        if (!runtimeResourceBindingMatchesBase(page->binding, bootId, snapshot.package)) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The runtime resource catalog no longer matches the active package."),
                {},
                requestId);
            return;
        }

        const Protocol::RuntimeResourceTableQuery query = *request.runtimeResourceTableQuery;
        if ((!runtimeCatalogBinding && query.cursor)
            || (runtimeCatalogBinding
                && !sameRuntimeResourceBinding(*runtimeCatalogBinding, page->binding))
            || (runtimeCatalogTotalCount && runtimeCatalogTotalCount != page->totalCount)
            || quint32(runtimeCatalogResources.size()) != query.cursor) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The runtime resource catalog changed during pagination."),
                {},
                requestId);
            return;
        }

        if (!runtimeCatalogBinding) {
            const Data::RuntimeResourceCatalogEpoch epoch = runtimeResourceEpoch(page->binding);
            const bool publishedBindingChanged = runtimeCatalog
                                                 && (runtimeCatalog->scope != snapshot.scope
                                                     || runtimeCatalog->sessionGeneration
                                                            != generation
                                                     || runtimeCatalog->epoch != epoch);
            if (publishedBindingChanged)
                clearRuntimeResourceCache();
            runtimeCatalogBinding = page->binding;
            runtimeCatalogTotalCount = page->totalCount;
        }

        quint64 previousResourceId = 0;
        if (!runtimeCatalogResources.isEmpty()) {
            previousResourceId = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(
                runtimeCatalogResources.constLast().id.value.constData()));
        }
        for (const Protocol::RuntimeResourceDescriptor &protocolDescriptor : page->resources) {
            const auto descriptor = runtimeResourceDescriptor(protocolDescriptor);
            const QByteArray id = opaqueBigEndian(protocolDescriptor.resourceId);
            if (!descriptor || protocolDescriptor.resourceId <= previousResourceId
                || runtimeCatalogIds.contains(id)) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The runtime resource catalog contains inconsistent descriptors."),
                    {},
                    requestId);
                return;
            }
            runtimeCatalogResources.append(*descriptor);
            runtimeCatalogIds.insert(id);
            previousResourceId = protocolDescriptor.resourceId;
        }

        removePending(requestId);
        if (page->more) {
            Protocol::RuntimeResourceTableQuery nextQuery;
            nextQuery.binding = page->binding;
            nextQuery.limit = 64;
            nextQuery.flags = 0;
            nextQuery.cursor = page->nextCursor;
            sendRuntimeResourceTableQuery(nextQuery);
            return;
        }

        if (quint32(runtimeCatalogResources.size()) != runtimeCatalogTotalCount
            || !runtimeCatalogBinding) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The runtime resource catalog is incomplete."),
                {},
                requestId);
            return;
        }

        Data::RuntimeResourceCatalog catalog;
        catalog.scope = snapshot.scope;
        catalog.sessionGeneration = generation;
        catalog.epoch = runtimeResourceEpoch(*runtimeCatalogBinding);
        catalog.receivedAt = QDateTime::currentDateTimeUtc();
        catalog.resources = runtimeCatalogResources;

        const bool hadSnapshot = runtimeSnapshot.has_value();
        runtimeCatalog = std::move(catalog);
        runtimeSnapshot.reset();
        notifyRuntimeResourceCatalogChanged();
        if (hadSnapshot)
            notifyRuntimeResourceSnapshotChanged();

        Protocol::RuntimeResourceSnapshotQuery snapshotQuery;
        snapshotQuery.binding = *runtimeCatalogBinding;
        const qsizetype requestedCount = std::min<qsizetype>(64, runtimeCatalog->resources.size());
        snapshotQuery.resourceIds.reserve(requestedCount);
        for (qsizetype index = 0; index < requestedCount; ++index) {
            const QByteArray &id = runtimeCatalog->resources.at(index).id.value;
            if (id.size() != qsizetype(sizeof(quint64))) {
                failProtocol(
                    request.role,
                    Data::ControllerOperation::QueryRuntimeResourceSnapshot,
                    Tr::tr("The runtime resource catalog contains an invalid resource ID."),
                    {},
                    requestId);
                return;
            }
            snapshotQuery.resourceIds.append(
                qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(id.constData())));
        }
        sendRuntimeResourceSnapshotQuery(snapshotQuery);
    }

    void handleRuntimeResourceSnapshot(
        const Protocol::Frame &frame, const PendingRequest &request, quint64 requestId)
    {
        if (!runtimeRefreshInProgress || !request.runtimeResourceSnapshotQuery || !runtimeCatalog
            || !runtimeCatalogBinding) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The runtime resource snapshot request context is missing."),
                {},
                requestId);
            return;
        }

        Protocol::Error decodeError;
        const auto protocolSnapshot = Protocol::decodeResourceSnapshot(
            frame, *request.runtimeResourceSnapshotQuery, &decodeError);
        if (!protocolSnapshot) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The controller returned an invalid runtime resource snapshot."),
                decodeError.text,
                requestId);
            return;
        }
        if (protocolSnapshot->status) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Controller,
                request.role,
                request.operation,
                Tr::tr("The controller rejected the runtime resource snapshot query."),
                {},
                protocolSnapshot->status,
                {},
                requestId);
            return;
        }
        if (!runtimeResourceRefreshTimer.isValid()
            || runtimeResourceRefreshTimer.hasExpired(
                options.runtimeResourceRefreshTimeoutMs)) {
            failRuntimeResourceQuery(
                Data::ControllerErrorSource::Network,
                request.role,
                request.operation,
                Tr::tr("The runtime resource refresh exceeded its total time limit."),
                {},
                {},
                {},
                requestId);
            return;
        }
        if (!sameRuntimeResourceBinding(protocolSnapshot->binding, *runtimeCatalogBinding)
            || !runtimeResourceBindingMatchesBase(protocolSnapshot->binding, bootId, snapshot.package)
            || runtimeCatalog->scope != snapshot.scope
            || runtimeCatalog->sessionGeneration != generation
            || runtimeCatalog->epoch != runtimeResourceEpoch(protocolSnapshot->binding)) {
            failProtocol(
                request.role,
                request.operation,
                Tr::tr("The runtime resource snapshot binding changed before publication."),
                {},
                requestId);
            return;
        }

        Data::RuntimeResourceSnapshot result;
        result.scope = runtimeCatalog->scope;
        result.sessionGeneration = runtimeCatalog->sessionGeneration;
        result.epoch = runtimeCatalog->epoch;
        result.snapshotSequence = protocolSnapshot->snapshotSequence;
        result.captureCycle = protocolSnapshot->captureCycle;
        result.controllerTimestampNs = protocolSnapshot->controllerTimestampNs;
        result.receivedAt = QDateTime::currentDateTimeUtc();
        result.complete = protocolSnapshot->complete && runtimeCatalog->resources.size() <= 64
                          && protocolSnapshot->samples.size() == runtimeCatalog->resources.size();
        result.samples.reserve(protocolSnapshot->samples.size());

        for (const Protocol::RuntimeResourceSample &protocolSample : protocolSnapshot->samples) {
            const QByteArray id = opaqueBigEndian(protocolSample.resourceId);
            const auto descriptor = std::find_if(
                runtimeCatalog->resources.cbegin(),
                runtimeCatalog->resources.cend(),
                [&id](const Data::RuntimeResourceDescriptor &candidate) {
                    return candidate.id.value == id;
                });
            const auto primitiveType = runtimeResourcePrimitiveType(protocolSample.primitive);
            const auto direction = runtimeResourceDirection(protocolSample.direction);
            const auto value = runtimeResourceTypedValue(
                protocolSample.primitive, protocolSample.bitWidth, protocolSample.value);
            const auto quality = runtimeResourceQuality(protocolSample.quality);
            if (descriptor == runtimeCatalog->resources.cend() || !primitiveType || !direction
                || !value || !quality || descriptor->primitiveType != *primitiveType
                || descriptor->direction != *direction
                || descriptor->bitWidth != protocolSample.bitWidth) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The runtime resource sample does not match its catalog descriptor."),
                    {},
                    requestId);
                return;
            }

            Data::RuntimeResourceSample sample;
            sample.resourceId = descriptor->id;
            sample.consistencyGroupId = descriptor->consistencyGroupId;
            sample.value = *value;
            sample.quality = *quality;
            sample.valueSequence = protocolSnapshot->snapshotSequence;
            sample.controllerTimestampNs = protocolSnapshot->controllerTimestampNs;
            result.samples.append(std::move(sample));
        }

        removePending(requestId);
        runtimeSnapshot = std::move(result);
        resetRuntimeResourceRefresh();
        notifyRuntimeResourceSnapshotChanged();
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
        if (request.kind == PendingKind::PackageBulk) {
            QString protocolDetail;
            if (frame.header.messageType != Protocol::MessageType::BulkStatus
                || !handlePackageBulkStatus(frame, request, requestId, &protocolDetail)) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The controller returned a malformed package upload status."),
                    protocolDetail,
                    requestId);
            }
            return;
        }
        if (request.kind == PendingKind::PackageRecovery) {
            QString protocolDetail;
            if (frame.header.messageType != Protocol::MessageType::PackageState
                || !handleDeploymentRecoveryPackageState(frame, request, requestId, &protocolDetail)) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The controller returned a malformed recovery package state."),
                    protocolDetail,
                    requestId);
            }
            return;
        }
        if (request.kind == PendingKind::PackageCommand) {
            QString protocolDetail;
            const bool handled
                = frame.header.messageType == Protocol::MessageType::CommandStatus
                      ? handlePackageCommandStatus(frame, request, requestId, &protocolDetail)
                  : frame.header.messageType == Protocol::MessageType::PackageState
                      ? handlePackageStateResult(frame, request, requestId, &protocolDetail)
                      : false;
            if (!handled) {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The controller returned a malformed package command response."),
                    protocolDetail,
                    requestId);
            }
            return;
        }
        if (isRuntimeResourceRequest(request.kind)
            && (frame.header.messageType == Protocol::MessageType::CommandStatus
                || frame.header.messageType == Protocol::MessageType::BulkStatus
                || frame.header.messageType == Protocol::MessageType::FirmwareStatus)) {
            if (frame.header.messageType == Protocol::MessageType::BulkStatus) {
                handleRuntimeResourceStatus(frame, request, requestId);
            } else {
                failProtocol(
                    request.role,
                    request.operation,
                    Tr::tr("The controller returned an invalid runtime resource status."),
                    {},
                    requestId);
            }
            return;
        }
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
            invalidateRuntimeResourcesIfBaseChanged();
            stateReceived = true;
            removePending(requestId);
            finishRefreshIfReady();
            return;
        }
        case PendingKind::LiveState: {
            const auto state = Protocol::decodeControllerState(frame, &decodeError);
            if (!state)
                break;
            snapshot.controllerState = *state;
            invalidateRuntimeResourcesIfBaseChanged();
            removePending(requestId);
            if (liveStatePollingDegraded) {
                liveStatePollingDegraded = false;
                if (snapshot.lastError
                    && snapshot.lastError->operation == Data::ControllerOperation::QueryState) {
                    snapshot.lastError.reset();
                }
                if (snapshot.state == Data::ControllerConnectionState::Degraded
                    && !subscriptionDegraded) {
                    snapshot.state = Data::ControllerConnectionState::Connected;
                }
            }
            publish();
            return;
        }
        case PendingKind::Capability: {
            auto capability = Protocol::decodeCapability(frame, featureBits, &decodeError);
            if (!capability)
                break;
            capability->runtimeResources
                = negotiatedMinor >= Protocol::RuntimeResourceMinor
                  && (featureBits & Protocol::RuntimeResourceFeature)
                  && (bulkFeatureBits & Protocol::RuntimeResourceFeature);
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
            invalidateRuntimeResourcesIfBaseChanged();
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
        case PendingKind::RuntimeResourceCatalog:
            handleRuntimeResourceCatalog(frame, request, requestId);
            return;
        case PendingKind::RuntimeResourceSnapshot:
            handleRuntimeResourceSnapshot(frame, request, requestId);
            return;
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
            invalidateRuntimeResources();
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
            invalidateRuntimeResources();
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
        case PendingKind::PackageBulk:
        case PendingKind::PackageCommand:
        case PendingKind::PackageRecovery:
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

    void appendAlarm(const Data::ControllerAlarmSummary &alarm)
    {
        if (!snapshot.recentAlarms.isEmpty()
            && snapshot.recentAlarms.constLast().sequence == alarm.sequence) {
            snapshot.recentAlarms.last() = alarm;
            return;
        }
        snapshot.recentAlarms.append(alarm);
        while (snapshot.recentAlarms.size() > AlarmHistoryCapacity)
            snapshot.recentAlarms.removeFirst();
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
        Protocol::Error decodeError;
        const auto alarm = Protocol::decodeAlarmEvent(frame, &decodeError);
        if (!alarm) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller returned an invalid alarm event."),
                decodeError.text,
                requestId);
            return;
        }
        const quint32 sequence = alarm->sequence;
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
        if (found->remainingReplayEvents == 1 && sequence != found->latestAlarmSequence) {
            failProtocol(
                value.role,
                Data::ControllerOperation::SubscribeEvents,
                Tr::tr("The controller event replay ended at the wrong checkpoint."),
                {},
                requestId);
            return;
        }

        appendAlarm(*alarm);
        --found->remainingReplayEvents;
        if (found->remainingReplayEvents) {
            found->expectedAlarmSequence = nextAlarmSequence(sequence);
            if (found->timer)
                found->timer->start(found->responseTimeoutMs);
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
            invalidateRuntimeResourcesIfBaseChanged();
            publish();
            return true;
        }
        case Protocol::MessageType::PerformanceSnapshot: {
            const auto performance = Protocol::decodePerformanceSnapshot(frame, &decodeError);
            if (!performance)
                break;
            snapshot.performance = *performance;
            publish();
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
            Protocol::Error decodeError;
            const auto alarm = Protocol::decodeAlarmEvent(frame, &decodeError);
            if (!alarmCheckpointEstablished || !alarm) {
                failProtocol(
                    value.role,
                    Data::ControllerOperation::SubscribeEvents,
                    alarmCheckpointEstablished
                        ? Tr::tr("The controller returned an invalid live alarm.")
                        : Tr::tr("A live alarm arrived before an event checkpoint."),
                    decodeError.text);
                return;
            }
            const quint32 sequence = alarm->sequence;
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
            appendAlarm(*alarm);
            lastAlarmSequence = sequence;
            publish();
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

    bool hasActiveDeployment() const
    {
        return packageDeploymentIsActive(snapshot.packageDeploymentProgress.state);
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
        case Command::ResetFault: {
            if (!faultResetConfirmation) {
                return Tr::tr(
                    "Fault reset was accepted, but its confirmation snapshot is unavailable.");
            }
            const FaultResetConfirmation &confirmation = *faultResetConfirmation;
            if (!ownsLease)
                return Tr::tr("Fault reset completed, but this session no longer owns the lease.");
            if (!state) {
                return Tr::tr(
                    "Fault reset was accepted, but the refreshed controller state is unavailable.");
            }
            if (state->currentFaults || state->latchedFaults || state->fault) {
                return Tr::tr(
                           "Fault reset was accepted, but the refreshed fault state is current "
                           "0x%1, latched 0x%2.")
                    .arg(state->currentFaults, 0, 16)
                    .arg(state->latchedFaults, 0, 16);
            }
            if (confirmation.expectedServiceState == ServiceState::Shutdown
                && confirmation.package
                && confirmation.package->controllerState
                       == Data::ControllerPackageState::Active) {
                return Tr::tr(
                    "Fault reset was accepted for an active package without a safe cyclic "
                    "runtime.");
            }
            if (!state->ready
                || state->serviceState != confirmation.expectedServiceState) {
                const QString expectedState
                    = confirmation.expectedServiceState == ServiceState::OperationalSafe
                          ? Tr::tr("OP_SAFE")
                          : Tr::tr("SHUTDOWN");
                return Tr::tr(
                           "Fault reset was accepted, but the controller did not reach the "
                           "expected %1 state.")
                    .arg(expectedState);
            }
            const quint32 expectedClearedSequence
                = nextAlarmSequence(confirmation.expectedAlarmSequence);
            if (!confirmation.clearedAlarmSequence
                || confirmation.clearedAlarmSequence != expectedClearedSequence
                || state->latestAlarmSequence != expectedClearedSequence) {
                return Tr::tr(
                           "Fault reset was accepted, but AlarmCleared sequence %1 was expected "
                           "and sequence %2 was confirmed.")
                    .arg(expectedClearedSequence)
                    .arg(confirmation.clearedAlarmSequence);
            }
            const auto cleared = std::find_if(
                snapshot.recentAlarms.crbegin(),
                snapshot.recentAlarms.crend(),
                [&confirmation, expectedClearedSequence](
                    const Data::ControllerAlarmSummary &alarm) {
                    return alarm.sequence == expectedClearedSequence && alarm.code == 5
                           && alarm.state == Data::ControllerAlarmState::Cleared
                           && alarm.severity == Data::ControllerSeverity::Information
                           && alarm.source == Data::ControllerAlarmSource::Service
                           && !alarm.latched && !alarm.detail0 && !alarm.detail1 && !alarm.detail2
                           && alarm.faultMask == confirmation.expectedLatchedFaults;
                });
            if (cleared == snapshot.recentAlarms.crend()) {
                return Tr::tr(
                    "Fault reset was accepted, but the matching AlarmCleared event was not "
                    "confirmed.");
            }
            if (confirmation.package != snapshot.package) {
                return Tr::tr(
                    "Fault reset cleared the latch, but the controller package state changed.");
            }
            if (state->applicationActive != confirmation.applicationActive
                || state->busOperational != confirmation.busOperational
                || state->safeOutput
                       != (confirmation.expectedServiceState
                           == ServiceState::OperationalSafe)) {
                return Tr::tr(
                    "Fault reset cleared the latch, but the controller runtime state changed.");
            }
            if (confirmation.expectedServiceState == ServiceState::Shutdown
                && (state->applicationActive || state->busOperational || state->safeOutput)) {
                return Tr::tr(
                    "Fault reset reported SHUTDOWN while a controller runtime remained active.");
            }
            return {};
        }
        case Command::ReleaseControl:
        case Command::None:
            return {};
        }
        if (satisfied)
            return {};
        return Tr::tr("%1 was accepted, but the refreshed controller state did not reach %2.")
            .arg(commandDisplayName(command), expected);
    }

    void startLiveStatePolling()
    {
        if (options.liveStatePollIntervalMs <= 0)
            return;
        liveStateTimer->setInterval(options.liveStatePollIntervalMs);
        liveStateTimer->start();
    }

    void sendLiveStateQuery()
    {
        if (shuttingDown || userDisconnecting || refreshInProgress || hasActiveControlOperation()
            || hasActiveDeployment() || !sessionId
            || (snapshot.state != Data::ControllerConnectionState::Connected
                && snapshot.state != Data::ControllerConnectionState::Degraded)) {
            return;
        }
        if (std::any_of(
                pendingRequests.cbegin(),
                pendingRequests.cend(),
                [](const PendingRequest &request) {
                    return request.kind == PendingKind::LiveState;
                })) {
            return;
        }
        Channel &control = channel(Protocol::Role::Control);
        if (!control.handshaken || !control.socket)
            return;
        sendRequest(
            control,
            Protocol::MessageType::GetState,
            PendingKind::LiveState,
            Data::ControllerOperation::QueryState);
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
        startLiveStatePolling();
        if (!snapshot.connectedAt.isValid())
            snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        if (controlRefreshPending) {
            const bool completesCommand = controlRefreshCompletesCommand;
            controlRefreshPending = false;
            controlRefreshCompletesCommand = false;
            if (!completesCommand) {
                const Data::ControllerControlCommand command = snapshot.controlProgress.command;
                if (refreshRejected) {
                    invalidateRuntimeResources();
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
            const Data::ControllerControlCommand command = snapshot.controlProgress.command;
            const auto recordResetVerificationError = [this, command](const QString &detail) {
                if (command != Data::ControllerControlCommand::ResetFault)
                    return;
                setError(
                    Data::ControllerErrorSource::Protocol,
                    Protocol::Role::Control,
                    Data::ControllerOperation::ResetFault,
                    Tr::tr("Fault reset outcome could not be verified."),
                    detail,
                    snapshot.controlProgress.status,
                    {},
                    Data::ControllerRetryDisposition::NotRetryable);
                if (snapshot.lastError) {
                    snapshot.lastError->operationResult
                        = snapshot.controlProgress.operationResult;
                }
            };
            if (refreshRejected) {
                invalidateRuntimeResources();
                snapshot.controllerState.reset();
                snapshot.package.reset();
                const QString detail = Tr::tr(
                    "The controller accepted the command, but its resulting state could not "
                    "be confirmed.");
                recordResetVerificationError(detail);
                finishControlProgress(
                    Data::ControllerControlState::Failed,
                    snapshot.controlProgress.stage,
                    false,
                    snapshot.controlProgress.status,
                    snapshot.controlProgress.operationResult,
                    detail);
                return;
            }
            if (const std::optional<QString> postconditionError = controlPostconditionError()) {
                snapshot.state = Data::ControllerConnectionState::Degraded;
                recordResetVerificationError(*postconditionError);
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
            invalidateRuntimeResources();
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
    QTimer *liveStateTimer = nullptr;
    Data::ControllerConnectionRequest currentRequest;
    std::optional<Data::RuntimeResourceCatalog> runtimeCatalog;
    std::optional<Data::RuntimeResourceSnapshot> runtimeSnapshot;
    QList<Data::RuntimeResourceDescriptor> runtimeCatalogResources;
    QSet<QByteArray> runtimeCatalogIds;
    std::optional<Protocol::RuntimeResourceBinding> runtimeCatalogBinding;
    std::optional<PersistentPackageSelector> persistentPackageSelector;
    std::optional<FaultResetConfirmation> faultResetConfirmation;
    quint64 generation = 0;
    quint64 nextRequestId = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 resumeSessionId = 0;
    quint64 resumeBootId = 0;
    quint32 featureBits = 0;
    quint32 bulkFeatureBits = 0;
    quint64 heartbeatRequestId = 0;
    quint32 lastAlarmSequence = 0;
    quint16 negotiatedMinor = 0;
    int reconnectAttempt = 0;
    bool shuttingDown = false;
    bool userDisconnecting = false;
    bool refreshInProgress = false;
    bool refreshRejected = false;
    bool liveStatePollingDegraded = false;
    bool stateReceived = false;
    bool capabilityReceived = false;
    bool packageReceived = false;
    bool firmwareReceived = false;
    bool subscriptionReceived = false;
    bool subscriptionDegraded = false;
    bool runtimeRefreshInProgress = false;
    quint32 runtimeCatalogTotalCount = 0;
    QElapsedTimer runtimeResourceRefreshTimer;
    bool alarmCheckpointEstablished = false;
    bool disconnectAfterRelease = false;
    quint64 disconnectReleaseRequestId = 0;
    std::optional<Data::ControllerConnectionState> stateBeforeDisconnectRelease;
    bool controlRefreshPending = false;
    bool controlRefreshCompletesCommand = false;
    QByteArray deploymentFingerprintValue;
    QByteArray deploymentArtifact;
    quint64 deploymentConfigurationId = 0;
    quint64 deploymentAuditSequence = 0;
    quint64 activeDeploymentRequestId = 0;
    qsizetype deploymentOffset = 0;
    bool deploymentActivate = true;
    bool deploymentRollbackOnActivationFailure = true;
    bool deploymentCancelRequested = false;
    std::optional<qint32> deploymentFailureStatus;
    std::optional<qint32> deploymentFailureOperationResult;
    QString deploymentFailureDetail;
    std::optional<Data::ControllerPackageSelector> deploymentRollbackRequestSelector;
    QHash<QString, DeploymentJournalEntry> deploymentJournal;
    QList<QString> deploymentJournalOrder;
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
           && reconnectMaximumDelayMs >= reconnectInitialDelayMs && reconnectAttempts >= 0
           && (liveStatePollIntervalMs == 0 || liveStatePollIntervalMs >= 50)
           && runtimeResourceRefreshTimeoutMs > 0 && maximumRuntimeResourceCount > 0;
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
    d->snapshot.performance.reset();
    d->snapshot.capability.reset();
    d->snapshot.package.reset();
    d->snapshot.firmware.reset();
    d->snapshot.controlProgress = {};
    d->snapshot.packageDeploymentProgress = {};
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
    if (d->hasActiveDeployment())
        return Utils::ResultError(Tr::tr("Wait for the package deployment to finish."));
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
    d->liveStateTimer->stop();
    if (d->snapshot.session && d->snapshot.session->ownsControlLease) {
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
                d->startLiveStatePolling();
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
    if (d->runtimeRefreshInProgress)
        return Utils::ResultError(Tr::tr("Wait for the runtime resource refresh to finish."));
    if (d->refreshInProgress || !d->pendingRequests.isEmpty())
        return Utils::ResultError(Tr::tr("A controller refresh is already active."));
    if (d->hasActiveDeployment())
        return Utils::ResultError(Tr::tr("Wait for the package deployment to finish."));
    d->beginRefresh();
    return {};
}

bool ProductApiSession::supportsRuntimeResources() const
{
    return d->negotiatedMinor >= Protocol::RuntimeResourceMinor
           && (d->featureBits & Protocol::RuntimeResourceFeature)
           && (d->bulkFeatureBits & Protocol::RuntimeResourceFeature);
}

std::optional<Data::RuntimeResourceCatalog> ProductApiSession::runtimeResourceCatalog() const
{
    return d->runtimeCatalog;
}

std::optional<Data::RuntimeResourceSnapshot> ProductApiSession::runtimeResourceSnapshot() const
{
    return d->runtimeSnapshot;
}

Utils::Result<> ProductApiSession::refreshRuntimeResources()
{
    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (d->snapshot.state != Data::ControllerConnectionState::Connected
        && d->snapshot.state != Data::ControllerConnectionState::Degraded) {
        return Utils::ResultError(
            Tr::tr("Connect to the controller before refreshing runtime resources."));
    }
    if (!supportsRuntimeResources()) {
        return Utils::ResultError(
            Tr::tr(
                "Runtime resources require Product API v1.12 and feature bit 13; no controller "
                "request was sent."));
    }
    if (d->runtimeRefreshInProgress)
        return Utils::ResultError(Tr::tr("A runtime resource refresh is already active."));
    if (d->refreshInProgress)
        return Utils::ResultError(Tr::tr("Wait for the controller refresh to finish."));
    if (d->hasActiveControlOperation())
        return Utils::ResultError(Tr::tr("Wait for the controller operation to finish."));
    if (d->hasActiveDeployment())
        return Utils::ResultError(Tr::tr("Wait for the package deployment to finish."));
    if (!d->channel(Protocol::Role::Bulk).handshaken || !d->channel(Protocol::Role::Bulk).socket) {
        return Utils::ResultError(Tr::tr("The controller Bulk channel is not connected."));
    }
    if (!d->snapshot.package || !activePackageSelector(*d->snapshot.package)) {
        d->invalidateRuntimeResources();
        return Utils::ResultError(
            Tr::tr("No active controller package is available for runtime resources."));
    }
    if (!d->beginRuntimeResourceRefresh())
        return Utils::ResultError(Tr::tr("The runtime resource query could not be sent."));
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
    case Command::ResetFault:
        return d->negotiatedMinor >= Protocol::ControlledFaultResetMinor
               && (d->featureBits & FeatureControlledFaultReset);
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

    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (d->snapshot.state != Data::ControllerConnectionState::Connected
        && d->snapshot.state != Data::ControllerConnectionState::Degraded) {
        return Utils::ResultError(Tr::tr("Connect to the controller before sending commands."));
    }
    if (!supportsControlCommand(request.command)) {
        if (request.command == Command::ResetFault) {
            const QString detail = Tr::tr(
                "UNSUPPORTED (-14): controlled fault reset requires negotiated Product API "
                "v1.11 and feature bit 12; no controller request was sent.");
            d->faultResetConfirmation.reset();
            d->beginControlProgress(request.command);
            d->snapshot.controlProgress.expectedLatchedFaults
                = request.expectedLatchedFaults;
            d->snapshot.controlProgress.expectedAlarmSequence
                = request.expectedAlarmSequence;
            d->setError(
                Data::ControllerErrorSource::ClientConfiguration,
                Protocol::Role::Control,
                Data::ControllerOperation::ResetFault,
                Tr::tr("Fault reset is not supported by this controller connection."),
                detail,
                -14,
                {},
                Data::ControllerRetryDisposition::NotRetryable);
            d->finishControlProgress(
                Data::ControllerControlState::Failed, 0, true, -14, {}, detail);
            return Utils::ResultError(detail);
        }
        return Utils::ResultError(Tr::tr("The requested controller command is not supported."));
    }
    if (!d->sessionId || !d->bootId || !d->snapshot.session)
        return Utils::ResultError(Tr::tr("The controller session identity is not available."));
    if (d->runtimeRefreshInProgress && request.command != Command::ReleaseControl) {
        return Utils::ResultError(Tr::tr("Wait for the runtime resource refresh to finish."));
    }
    if (d->refreshInProgress)
        return Utils::ResultError(Tr::tr("Wait for the controller refresh to finish."));
    if (d->hasActiveControlOperation())
        return Utils::ResultError(Tr::tr("Another controller operation is already active."));
    if (d->hasActiveDeployment())
        return Utils::ResultError(Tr::tr("Wait for the package deployment to finish."));

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
    if (request.command == Command::ResetFault && d->snapshot.controllerState
        && !d->snapshot.controllerState->currentFaults
        && !d->snapshot.controllerState->latchedFaults) {
        d->faultResetConfirmation.reset();
        d->beginControlProgress(request.command);
        d->snapshot.controlProgress.expectedLatchedFaults
            = request.expectedLatchedFaults;
        d->snapshot.controlProgress.expectedAlarmSequence
            = request.expectedAlarmSequence;
        d->finishControlProgress(
            Data::ControllerControlState::Succeeded,
            0,
            true,
            0,
            0,
            Tr::tr("No latched fault remains; no controller request was sent."));
        return {};
    }
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
        && request.command != Command::ReleaseControl
        && request.command != Command::ResetFault
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
    case Command::ResetFault: {
        QTC_ASSERT(
            d->snapshot.controllerState,
            return Utils::ResultError(Tr::tr("The controller state is unavailable.")));
        const Data::ControllerStateSummary &controllerState = *d->snapshot.controllerState;
        if (controllerState.serviceState != ServiceState::Fault) {
            return Utils::ResultError(
                Tr::tr("Fault reset is available only while the controller is in Fault state."));
        }
        if (controllerState.currentFaults) {
            return Utils::ResultError(
                Tr::tr("The current fault is still active (0x%1). Resolve its cause first.")
                    .arg(controllerState.currentFaults, 0, 16));
        }
        if (!controllerState.latchedFaults || !controllerState.latestAlarmSequence) {
            return Utils::ResultError(
                Tr::tr("Refresh the controller before confirming the latched fault."));
        }
        if (!request.expectedLatchedFaults || !request.expectedAlarmSequence
            || (request.expectedLatchedFaults & ~ControllerFaultKnownMask)) {
            return Utils::ResultError(
                Tr::tr("The fault confirmation snapshot is invalid."));
        }
        if (request.expectedLatchedFaults != controllerState.latchedFaults
            || request.expectedAlarmSequence != controllerState.latestAlarmSequence) {
            return Utils::ResultError(
                Tr::tr(
                    "The controller fault changed before confirmation. Refresh diagnostics and "
                    "review the latest alarm."));
        }
        break;
    }
    case Command::AcquireControl:
    case Command::None:
    case Command::ReleaseControl:
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
    } else if (request.command == Command::ResetFault) {
        appendU64(request.expectedLatchedFaults);
        appendU32(request.expectedAlarmSequence);
        appendU32(0);
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
    if (request.command == Command::ResetFault) {
        const Data::ControllerStateSummary &controllerState = *d->snapshot.controllerState;
        d->snapshot.controlProgress.expectedLatchedFaults
            = request.expectedLatchedFaults;
        d->snapshot.controlProgress.expectedAlarmSequence
            = request.expectedAlarmSequence;
        const bool safeCyclicRuntime
            = !controllerState.applicationActive && controllerState.busOperational
              && controllerState.safeOutput && d->snapshot.package
              && d->snapshot.package->controllerState == Data::ControllerPackageState::Active;
        d->faultResetConfirmation = ProductApiSessionPrivate::FaultResetConfirmation{
            request.expectedLatchedFaults,
            request.expectedAlarmSequence,
            0,
            d->snapshot.package,
            safeCyclicRuntime ? Data::ControllerServiceState::OperationalSafe
                              : Data::ControllerServiceState::Shutdown,
            controllerState.applicationActive,
            controllerState.busOperational,
        };
    }
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

bool ProductApiSession::supportsPackageDeployment() const
{
    return true;
}

Utils::Result<> ProductApiSession::deployPackage(
    const Data::ControllerPackageDeploymentRequest &request)
{
    if (!supportsPackageDeployment())
        return Utils::ResultError(Tr::tr("Package deployment is not supported."));
    if (d->shuttingDown)
        return Utils::ResultError(Tr::tr("The controller session is shutting down."));
    if (!operationIdIsValid(request.operationId)) {
        return Utils::ResultError(
            Tr::tr("OperationId must contain between 1 and 128 non-control characters."));
    }
    if (request.artifact.isEmpty() || request.artifact.size() > MaximumPackageBytes) {
        return Utils::ResultError(
            Tr::tr("The controller package must contain between 1 and 16777216 bytes."));
    }
    if (!request.configurationId)
        return Utils::ResultError(Tr::tr("The package configuration ID must be nonzero."));

    const QByteArray fingerprint = deploymentFingerprint(request);
    const Data::ControllerPackageDeploymentProgress &current = d->snapshot.packageDeploymentProgress;
    if (d->hasActiveDeployment() && current.operationId != request.operationId) {
        return Utils::ResultError(Tr::tr("Another package deployment is already active."));
    }
    const auto journalEntry = d->deploymentJournal.constFind(request.operationId);
    if (journalEntry != d->deploymentJournal.cend()) {
        if (journalEntry->fingerprint != fingerprint) {
            return Utils::ResultError(
                Tr::tr("OperationId is already bound to a different package deployment."));
        }
        if (current.operationId != request.operationId) {
            d->deploymentFingerprintValue = fingerprint;
            d->snapshot.packageDeploymentProgress = journalEntry->progress;
            d->snapshot.lastError.reset();
            d->publish();
        }
        return {};
    }

    if (d->snapshot.state != Data::ControllerConnectionState::Connected
        && d->snapshot.state != Data::ControllerConnectionState::Degraded) {
        return Utils::ResultError(Tr::tr("Connect to the controller before deploying a package."));
    }
    if (!d->sessionId || !d->bootId || !d->snapshot.session)
        return Utils::ResultError(Tr::tr("The controller session identity is not available."));
    if (!d->snapshot.session->ownsControlLease)
        return Utils::ResultError(Tr::tr("Acquire the control lease before deploying a package."));
    if (!d->snapshot.capability || !d->snapshot.capability->transactionalBulk) {
        return Utils::ResultError(
            Tr::tr("The controller does not support transactional package upload."));
    }
    if (!d->snapshot.controllerState || !d->snapshot.controllerState->ready
        || d->snapshot.controllerState->serviceState != Data::ControllerServiceState::Shutdown) {
        return Utils::ResultError(
            Tr::tr("Enter configuration mode before deploying a controller package."));
    }
    if (d->refreshInProgress)
        return Utils::ResultError(Tr::tr("Wait for the controller refresh to finish."));
    if (d->runtimeRefreshInProgress)
        return Utils::ResultError(Tr::tr("Wait for the runtime resource refresh to finish."));
    if (d->hasActiveControlOperation())
        return Utils::ResultError(Tr::tr("Another controller operation is already active."));
    if (d->hasActiveDeployment())
        return Utils::ResultError(Tr::tr("Another package deployment is already active."));

    d->beginDeployment(request, fingerprint);
    if (!d->sendDeploymentBegin()) {
        if (d->hasActiveDeployment()) {
            d->setDeploymentState(
                Data::ControllerPackageDeploymentState::Failed,
                Tr::tr("The package upload could not be started."),
                {},
                {},
                Data::ControllerOperation::UploadPackage);
        }
        return Utils::ResultError(Tr::tr("The package upload could not be started."));
    }
    return {};
}

Utils::Result<> ProductApiSession::cancelPackageDeployment(const QString &operationId)
{
    const Data::ControllerPackageDeploymentProgress &progress
        = d->snapshot.packageDeploymentProgress;
    if (operationId.isEmpty() || progress.operationId != operationId)
        return Utils::ResultError(Tr::tr("The package deployment OperationId does not match."));
    if (progress.state == Data::ControllerPackageDeploymentState::Canceled)
        return {};
    if (progress.state == Data::ControllerPackageDeploymentState::Canceling)
        return {};
    if (progress.state != Data::ControllerPackageDeploymentState::Uploading
        && progress.state != Data::ControllerPackageDeploymentState::Committing) {
        return Utils::ResultError(
            Tr::tr("Package deployment can only be canceled before validation begins."));
    }

    d->deploymentCancelRequested = true;
    d->snapshot.packageDeploymentProgress.state = Data::ControllerPackageDeploymentState::Canceling;
    d->snapshot.packageDeploymentProgress.detail = Tr::tr("Canceling the package upload.");
    d->appendDeploymentAudit(
        Data::ControllerOperation::AbortPackageUpload,
        Tr::tr("Package deployment cancellation requested."));
    d->publish();
    if (!d->activeDeploymentRequestId && !d->sendDeploymentAbort(true)) {
        return Utils::ResultError(Tr::tr("The package upload cancellation could not be sent."));
    }
    return {};
}

void ProductApiSession::shutdown()
{
    if (d->shuttingDown)
        return;
    d->sendBestEffortDeploymentAbort();
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
           && !d->reconnectTimer->isActive() && !d->refreshInProgress
           && !d->runtimeRefreshInProgress;
}

bool ProductApiSession::refreshInProgressForTests() const
{
    return d->refreshInProgress;
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
