// Copyright (C) 2026 Embed Labs

#pragma once

#include "controllerconnection.h"
#include "deviceadapter.h"
#include "ethercatdata_global.h"
#include "projectsnapshot.h"
#include "runtimepackageactivation.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>

#include <optional>
#include <variant>

namespace EtherCAT::Data {

class ETHERCATDATA_EXPORT RuntimePackageCompilerOperationId
{
public:
    RuntimePackageCompilerOperationId() = default;
    explicit RuntimePackageCompilerOperationId(QString value);

    const QString &value() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerOperationId &, const RuntimePackageCompilerOperationId &)
        = default;

private:
    QString m_value;
};

inline size_t qHash(const RuntimePackageCompilerOperationId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value(), seed);
}

class ETHERCATDATA_EXPORT RuntimePackageCompilerSha256
{
public:
    RuntimePackageCompilerSha256() = default;
    explicit RuntimePackageCompilerSha256(QByteArray value);

    const QByteArray &value() const;
    bool isValid() const;

    friend bool operator==(const RuntimePackageCompilerSha256 &, const RuntimePackageCompilerSha256 &)
        = default;

private:
    QByteArray m_value;
};

// The compiler owns canonicalization. The IDE preserves the exact bytes and
// independently verifies their digest without parsing or normalizing them.
class ETHERCATDATA_EXPORT RuntimePackageCompilerCanonicalJson
{
public:
    RuntimePackageCompilerCanonicalJson() = default;
    RuntimePackageCompilerCanonicalJson(QByteArray exactBytes, RuntimePackageCompilerSha256 sha256);

    static RuntimePackageCompilerCanonicalJson fromExactBytes(QByteArray exactBytes);

    const QByteArray &exactBytes() const;
    const RuntimePackageCompilerSha256 &sha256() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerCanonicalJson &, const RuntimePackageCompilerCanonicalJson &)
        = default;

private:
    QByteArray m_exactBytes;
    RuntimePackageCompilerSha256 m_sha256;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerContractIdentity
{
    QString contractId;
    quint32 contractVersion = 0;
    RuntimePackageCompilerSha256 schemaBundleSha256;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerContractIdentity &,
        const RuntimePackageCompilerContractIdentity &)
        = default;
};

enum class RuntimePackageCompilerSourceArtifactKind {
    Unknown,
    TopologyEvidence,
    TargetProfile,
    TargetProfileSignature,
    ProductionPublicKey,
    AdapterBundle,
    PolicyTemplate,
    ControllerFeatures,
    RuntimeSource,
    OriginalEsi,
    AdapterSourceFile,
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerSourceArtifact
{
    RuntimePackageCompilerSourceArtifactKind kind
        = RuntimePackageCompilerSourceArtifactKind::Unknown;
    QString relativePath;
    QByteArray exactBytes;
    RuntimePackageCompilerSha256 sha256;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerSourceArtifact &, const RuntimePackageCompilerSourceArtifact &)
        = default;
};

// Exact closed artifact set from ide-project-compiler-v1. Referenced lower
// adapter files remain bound by the canonical AdapterBundle artifact.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerSourceArtifacts
{
    RuntimePackageCompilerSourceArtifact topologyEvidence;
    RuntimePackageCompilerSourceArtifact targetProfile;
    RuntimePackageCompilerSourceArtifact targetProfileSignature;
    RuntimePackageCompilerSourceArtifact productionPublicKey;
    RuntimePackageCompilerSourceArtifact adapterBundle;
    RuntimePackageCompilerSourceArtifact policyTemplate;
    RuntimePackageCompilerSourceArtifact controllerFeatures;
    RuntimePackageCompilerSourceArtifact runtimeSource;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerSourceArtifacts &, const RuntimePackageCompilerSourceArtifacts &)
        = default;
};

enum class RuntimePackageCompilerPdoDirection { Unknown, Input, Output };

struct ETHERCATDATA_EXPORT RuntimePackageCompilerPdoEntry
{
    QString fieldId;
    quint16 index = 0;
    quint8 subIndex = 0;
    quint16 bitLength = 0;
    QString dataType;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPdoEntry &, const RuntimePackageCompilerPdoEntry &) = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerPdoMapping
{
    QString id;
    RuntimePackageCompilerPdoDirection direction = RuntimePackageCompilerPdoDirection::Unknown;
    quint16 pdoIndex = 0;
    quint8 syncManager = 0;
    bool fixed = false;
    QList<RuntimePackageCompilerPdoEntry> entries;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPdoMapping &,
        const RuntimePackageCompilerPdoMapping &) = default;
};

enum class RuntimePackageCompilerStartupStage {
    Unknown,
    PreOperational,
    SafeOperational,
    Operational
};
enum class RuntimePackageCompilerStartupFailureAction { Unknown, Abort, Warn, Continue };

struct ETHERCATDATA_EXPORT RuntimePackageCompilerStartupSdo
{
    quint16 sequence = 0;
    QString id;
    bool enabled = true;
    RuntimePackageCompilerStartupStage stage = RuntimePackageCompilerStartupStage::Unknown;
    quint16 index = 0;
    quint8 subIndex = 0;
    std::variant<qint64, quint64> value = qint64(0);
    quint8 valueBytes = 0;
    bool completeAccess = false;
    quint64 timeoutNs = 0;
    quint8 retryCount = 0;
    RuntimePackageCompilerStartupFailureAction failureAction
        = RuntimePackageCompilerStartupFailureAction::Unknown;
    bool persistent = false;
    bool requiresPowerCycle = false;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerStartupSdo &,
        const RuntimePackageCompilerStartupSdo &) = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerDcProjection
{
    bool enabled = false;
    std::optional<QString> signedDcProfileId;
    std::optional<QString> mode;
    quint16 assignActivate = 0;
    quint64 sync0CycleNs = 0;
    qint64 sync0ShiftNs = 0;
    quint64 sync1CycleNs = 0;
    qint64 sync1ShiftNs = 0;
    bool referenceClock = false;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerDcProjection &,
        const RuntimePackageCompilerDcProjection &) = default;
};

enum class RuntimePackageCompilerManualRecoveryAction {
    Unknown,
    HoldSafe,
    ReturnToTask,
    Stop,
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerManualEnvelope
{
    bool enabled = false;
    quint16 maximumTtlCycles = 0;
    quint16 refreshCycles = 0;
    quint16 maximumHoldCycles = 0;
    RuntimePackageCompilerManualRecoveryAction timeoutAction
        = RuntimePackageCompilerManualRecoveryAction::Unknown;
    RuntimePackageCompilerManualRecoveryAction releaseAction
        = RuntimePackageCompilerManualRecoveryAction::Unknown;
    RuntimePackageCompilerManualRecoveryAction failureAction
        = RuntimePackageCompilerManualRecoveryAction::Unknown;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerManualEnvelope &,
        const RuntimePackageCompilerManualEnvelope &) = default;
};

enum class RuntimePackageCompilerSymbolMode { Unknown, ReportOnly, Requested, All };

// This is the complete, typed API-042 projection for one current Qt project
// slave. projectSlaveNodeId is the local immutable join key and is never
// encoded. slaveNodeId and projectDeviceId are independent signed identifiers;
// neither may be inferred from the other or from position/station address.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerDeviceProjection
{
    NodeId projectSlaveNodeId;
    QString slaveNodeId;
    QString projectDeviceId;
    int position = -1;
    quint16 stationAddress = 0;
    quint16 alias = 0;
    DeviceIdentity identity;
    quint32 serialNumber = 0;
    RuntimePackageCompilerSha256 esiSha256;
    QString targetProfileId;
    QString adapterId;
    QString adapterVersion;
    RuntimePackageCompilerSha256 adapterSha256;
    QString pdoProfileId;
    std::optional<QString> signedDcProfileId;
    QList<RuntimePackageCompilerPdoMapping> pdoMappings;
    QList<RuntimePackageCompilerStartupSdo> startupSdos;
    RuntimePackageCompilerDcProjection dc;
    QList<DeviceModuleAssignment> moduleAssignments;
    QMap<QString, QString> componentBindingIds;
    QMap<QString, QString> semanticBindingIds;
    QMap<QString, QString> semanticActionBindingIds;
    RuntimePackageCompilerSymbolMode symbolMode = RuntimePackageCompilerSymbolMode::Unknown;
    QMap<QString, QString> symbols;
    RuntimePackageCompilerManualEnvelope manualEnvelope;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerDeviceProjection &,
        const RuntimePackageCompilerDeviceProjection &) = default;
};

// The encoded IDs remain API-042 stable strings while the local NodeIds bind
// the projection to exactly one captured Qt project and master.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerProjectProjection
{
    NodeId projectNodeId;
    NodeId masterProjectNodeId;
    QString projectId;
    QString masterNodeId;
    quint64 documentRevision = 0;
    MasterTimingMode timingMode = MasterTimingMode::Unassigned;
    quint64 cyclePeriodNs = 0;
    quint32 linkSpeedMbps = 0;
    QList<RuntimePackageCompilerDeviceProjection> devices;
    RuntimePackageCompilerCanonicalJson uiMetadata;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerProjectProjection &,
        const RuntimePackageCompilerProjectProjection &) = default;
};

// Exact lower source bytes used to prove that the ProjectSnapshot selection is
// present in the adapter bundle. This is evidence, not another project model.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerDeviceSourceEvidence
{
    NodeId projectSlaveNodeId;
    RuntimePackageCompilerSourceArtifact originalEsi;
    RuntimePackageCompilerSourceArtifact adapterSourceFile;

    // The project retains the upper V3 adapter selection used by Workbench.
    // The lower fields below identify the compiler adapter bundle entry. A
    // production request builder must derive this bridge from one exact,
    // content-addressed upper manifest's controllerAdapterTarget and selected
    // ProcessDataProfile; neither side may be inferred by ID spelling.
    DeviceAdapterContractVersion projectAdapterContractVersion
        = DeviceAdapterContractVersion::Unknown;
    DeviceAdapterId projectAdapterId;
    QString projectAdapterVersion;
    RuntimePackageCompilerSha256 projectAdapterContentSha256;
    DeviceAdapterControllerTarget projectControllerAdapterTarget;
    QString projectPdoProfileId;
    QString projectSignedPdoProfileId;
    QString projectSignedDcProfileId;

    // Lower compiler/controller identity.
    QString adapterId;
    QString adapterVersion;
    RuntimePackageCompilerSha256 adapterCanonicalSha256;
    QString pdoProfileId;
    std::optional<QString> signedDcProfileId;
    bool explicitNoDc = false;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerDeviceSourceEvidence &,
        const RuntimePackageCompilerDeviceSourceEvidence &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerTopologySlaveEvidence
{
    NodeId projectSlaveNodeId;
    int position = -1;
    quint16 stationAddress = 0;
    quint16 alias = 0;
    DeviceIdentity identity;
    quint32 serialNumber = 0;
    QList<DeviceModuleAssignment> moduleAssignments;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerTopologySlaveEvidence &,
        const RuntimePackageCompilerTopologySlaveEvidence &)
        = default;
};

// This is one indivisible controller observation. Its slaves may not be
// assembled from different scans, sessions, or controller boots.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerFreshTopologyEvidence
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    QString evidenceId;
    quint64 captureBootId = 0;
    quint64 captureSequence = 0;
    quint64 capturedAtNs = 0;
    quint64 expiresAtNs = 0;
    quint64 cyclePeriodNs = 0;
    quint32 linkSpeedMbps = 0;
    QList<RuntimePackageCompilerTopologySlaveEvidence> slaves;
    RuntimePackageCompilerCanonicalJson canonicalEvidence;

    bool isValid() const;
    bool isFreshAt(quint64 compileTimeNs) const;

    friend bool operator==(
        const RuntimePackageCompilerFreshTopologyEvidence &,
        const RuntimePackageCompilerFreshTopologyEvidence &)
        = default;
};

// The signed target/capability profile stays opaque to the IDE contract. ABI
// identifiers and the capability digest are retained as comparison evidence;
// signature validation remains the compiler/finalizer's responsibility.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerSignedTargetProfileEvidence
{
    QString profileId;
    quint64 policyRevision = 0;
    RuntimePackageCompilerCanonicalJson canonicalProfile;
    RuntimePackageCompilerSha256 capabilityDescriptorSha256;
    RuntimePackageCompilerSha256 controllerFeaturesSha256;
    RuntimePackageCompilerSha256 adapterBundleSha256;
    quint32 cpu1AbiVersion = 0;
    quint32 fpgaAbiVersion = 0;
    QByteArray signature;
    RuntimePackageCompilerSha256 signingKeyIdSha256;
    bool productionSigned = false;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerSignedTargetProfileEvidence &,
        const RuntimePackageCompilerSignedTargetProfileEvidence &)
        = default;
};

// The compiler receives one typed snapshot and its atomic CAS identity. Raw
// project-document bytes intentionally stop at this boundary so they cannot
// become a second execution source of truth; their digest preserves an exact
// capture identity for provider-local provenance checks.
class ETHERCATDATA_EXPORT RuntimePackageCompilerProjectSnapshotEvidence
{
public:
    RuntimePackageCompilerProjectSnapshotEvidence() = default;
    explicit RuntimePackageCompilerProjectSnapshotEvidence(
        const RuntimePackageActivationProjectCapture &capture);

    const ProjectSnapshot &snapshot() const;
    const RuntimePackageCompilerSha256 &serializedProjectSha256() const;
    quint64 documentRevisionNumber() const;
    const RuntimePackageActivationDocumentRevisionToken &documentRevision() const;
    const RuntimePackageActivationOriginalBindingToken &originalBinding() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerProjectSnapshotEvidence &,
        const RuntimePackageCompilerProjectSnapshotEvidence &)
        = default;

private:
    bool m_validCapture = false;
    ProjectSnapshot m_snapshot;
    RuntimePackageCompilerSha256 m_serializedProjectSha256;
    quint64 m_documentRevisionNumber = 0;
    RuntimePackageActivationDocumentRevisionToken m_documentRevision;
    RuntimePackageActivationOriginalBindingToken m_originalBinding;
};

enum class RuntimePackageCompilerCommand {
    Unknown,
    Compile,
    Finalize,
    Query,
    Verify,
};

enum class RuntimePackageCompilerDiagnosticCategory {
    Unknown,
    Input,
    Topology,
    Esi,
    Slave,
    Pdo,
    Sdo,
    Dc,
    Adapter,
    Capability,
    Timing,
    Signing,
    Configuration,
    Security,
    Canceled,
    Internal,
    Path,
    Idempotency,
};

enum class RuntimePackageCompilerDiagnosticSeverity {
    Information,
    Warning,
    Error,
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerDiagnostic
{
    RuntimePackageCompilerDiagnosticCategory category
        = RuntimePackageCompilerDiagnosticCategory::Unknown;
    RuntimePackageCompilerDiagnosticSeverity severity
        = RuntimePackageCompilerDiagnosticSeverity::Error;
    QString stage;
    QString code;
    QString path;
    QString message;
    bool retryable = false;
    RuntimePackageCompilerCanonicalJson canonicalJson;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerDiagnostic &, const RuntimePackageCompilerDiagnostic &)
        = default;
};

enum class RuntimePackageCompilerResultStatus {
    Unknown,
    Succeeded,
    DomainFailed,
    Canceled,
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerResultEnvelope
{
    // canonicalResult is the exact compiler response. The remaining fields are
    // its schema-validated projection; providers must reject any mismatch
    // before finishing a job.
    RuntimePackageCompilerCommand command = RuntimePackageCompilerCommand::Unknown;
    RuntimePackageCompilerResultStatus status = RuntimePackageCompilerResultStatus::Unknown;
    QString backendStatus;
    RuntimePackageCompilerOperationId operationId;
    quint64 configurationId = 0;
    RuntimePackageCompilerSha256 requestSha256;
    RuntimePackageCompilerCanonicalJson canonicalResult;
    QList<RuntimePackageCompilerDiagnostic> diagnostics;

    bool isValid() const;
    bool isSuccess() const;

    friend bool operator==(
        const RuntimePackageCompilerResultEnvelope &, const RuntimePackageCompilerResultEnvelope &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerCompileRequest
{
    // Typed IDE data is the sole request source of truth. A provider codec
    // creates and schema-validates the API-042 canonical request; callers do
    // not supply a second whole-request JSON representation.
    RuntimePackageCompilerOperationId operationId;
    QString intentId;
    quint64 configurationId = 0;
    quint64 buildTimestampNs = 0;
    quint64 compileTimeNs = 0;
    quint32 manifestFormatVersion = 2;
    RuntimePackageCompilerContractIdentity contractIdentity;
    RuntimePackageCompilerProjectSnapshotEvidence projectSnapshotEvidence;
    RuntimePackageCompilerProjectProjection projectProjection;
    RuntimePackageCompilerFreshTopologyEvidence topologyEvidence;
    RuntimePackageCompilerSourceArtifacts sourceArtifacts;
    QList<RuntimePackageCompilerDeviceSourceEvidence> deviceSourceEvidence;
    RuntimePackageCompilerSignedTargetProfileEvidence targetProfile;

    // These are the typed structural inputs needed before reservation. The
    // provider codec must still validate its encoded canonical request against
    // the frozen API-042 schema before it reserves OperationId/configurationId.
    bool hasValidReservationInputs() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerCompileRequest &, const RuntimePackageCompilerCompileRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerFinalizeRequest
{
    RuntimePackageCompilerOperationId operationId;
    quint64 configurationId = 0;
    RuntimePackageCompilerContractIdentity contractIdentity;
    RuntimePackageCompilerSha256 compileRequestSha256;
    RuntimePackageCompilerSha256 signRequestSha256;
    RuntimePackageCompilerSha256 manifestSha256;
    RuntimePackageCompilerSha256 signingKeyIdSha256;
    quint64 signingPolicyRevision = 0;
    RuntimePackageCompilerCanonicalJson detachedSigningResponse;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerFinalizeRequest &, const RuntimePackageCompilerFinalizeRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerQueryRequest
{
    RuntimePackageCompilerOperationId operationId;
    RuntimePackageCompilerContractIdentity contractIdentity;
    RuntimePackageCompilerSha256 compileRequestSha256;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerQueryRequest &, const RuntimePackageCompilerQueryRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerVerifyRequest
{
    RuntimePackageCompilerOperationId operationId;
    RuntimePackageCompilerContractIdentity contractIdentity;
    QByteArray packageBytes;
    RuntimePackageCompilerSha256 packageSha256;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerVerifyRequest &, const RuntimePackageCompilerVerifyRequest &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerCompileResult
{
    RuntimePackageCompilerResultEnvelope envelope;
    std::optional<RuntimePackageCompilerSha256> intentSha256;
    std::optional<RuntimePackageCompilerSha256> compiledProjectSha256;
    std::optional<RuntimePackageCompilerSha256> compileReportSha256;
    std::optional<RuntimePackageCompilerSha256> effectiveProjectCompanionSha256;
    std::optional<RuntimePackageCompilerCanonicalJson> signRequest;
    std::optional<RuntimePackageCompilerSha256> manifestSha256;
    std::optional<RuntimePackageCompilerSha256> targetProfileSha256;
    std::optional<RuntimePackageCompilerSha256> adapterBundleSha256;
    QString outputDirectory;

    bool isValid() const;
    bool isSuccess() const;

    friend bool operator==(
        const RuntimePackageCompilerCompileResult &, const RuntimePackageCompilerCompileResult &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerFinalizeResult
{
    RuntimePackageCompilerResultEnvelope envelope;
    QString packagePath;
    QByteArray packageBytes;
    std::optional<RuntimePackageCompilerSha256> packageSha256;
    std::optional<RuntimePackageCompilerSha256> manifestSha256;
    std::optional<RuntimePackageCompilerSha256> signingReceiptSha256;

    bool isValid() const;
    bool isSuccess() const;

    friend bool operator==(
        const RuntimePackageCompilerFinalizeResult &, const RuntimePackageCompilerFinalizeResult &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerQueryResult
{
    RuntimePackageCompilerResultEnvelope envelope;
    std::optional<RuntimePackageCompilerCanonicalJson> compilerRecord;
    std::optional<RuntimePackageCompilerCanonicalJson> signerResponse;

    bool isValid() const;
    bool isSuccess() const;
    bool hasRecoveredResult() const;
    bool hasCompilerRecord() const;
    bool hasSignerResponse() const;

    friend bool operator==(
        const RuntimePackageCompilerQueryResult &, const RuntimePackageCompilerQueryResult &)
        = default;
};

struct ETHERCATDATA_EXPORT RuntimePackageCompilerVerifyResult
{
    RuntimePackageCompilerResultEnvelope envelope;
    RuntimePackageCompilerSha256 packageSha256;
    quint32 manifestFormatVersion = 0;
    RuntimePackageCompilerSha256 intentSha256;
    RuntimePackageCompilerSha256 effectiveProjectCompanionSha256;
    RuntimePackageCompilerSha256 targetProfileSha256;
    RuntimePackageCompilerSha256 adapterBundleSha256;
    RuntimePackageCompilerSha256 topologyEvidenceSha256;
    bool trusted = false;

    bool isValid() const;
    bool isSuccess() const;

    friend bool operator==(
        const RuntimePackageCompilerVerifyResult &, const RuntimePackageCompilerVerifyResult &)
        = default;
};

// Complete typed evidence for one compile -> finalize -> verify chain. The
// compiler provider still owns provenance validation against its immutable
// operation store; this value type rejects incomplete, failed, or internally
// spliced evidence before that provider-specific check is attempted.
struct ETHERCATDATA_EXPORT RuntimePackageCompilerActivationProof
{
    QString compilerProviderId;
    RuntimePackageCompilerContractIdentity contractIdentity;
    RuntimePackageCompilerCompileRequest compileRequest;
    RuntimePackageCompilerCompileResult compileResult;
    RuntimePackageCompilerFinalizeRequest finalizeRequest;
    // Provider-store provenance digest for the exact canonical finalize
    // request. Data validates its shape; the provider must recompute it.
    RuntimePackageCompilerSha256 finalizeRequestSha256;
    RuntimePackageCompilerFinalizeResult finalizeResult;
    // Verify may use an independent OperationId. This is its provider-store
    // provenance digest; the provider must recompute it from the exact request.
    RuntimePackageCompilerVerifyRequest verifyRequest;
    RuntimePackageCompilerSha256 verifyRequestSha256;
    RuntimePackageCompilerVerifyResult verifyResult;
    QByteArray compiledProjectSource;
    QByteArray effectiveProjectCompanion;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerActivationProof &,
        const RuntimePackageCompilerActivationProof &) = default;
};

class ETHERCATDATA_EXPORT RuntimePackageCompilerJobResult
{
public:
    using Value = std::variant<
        std::monostate,
        RuntimePackageCompilerCompileResult,
        RuntimePackageCompilerFinalizeResult,
        RuntimePackageCompilerQueryResult,
        RuntimePackageCompilerVerifyResult>;

    RuntimePackageCompilerJobResult() = default;
    explicit RuntimePackageCompilerJobResult(RuntimePackageCompilerCompileResult result);
    explicit RuntimePackageCompilerJobResult(RuntimePackageCompilerFinalizeResult result);
    explicit RuntimePackageCompilerJobResult(RuntimePackageCompilerQueryResult result);
    explicit RuntimePackageCompilerJobResult(RuntimePackageCompilerVerifyResult result);

    RuntimePackageCompilerCommand command() const;
    const Value &value() const;
    bool isValid() const;
    bool isSuccess() const;

    friend bool operator==(
        const RuntimePackageCompilerJobResult &, const RuntimePackageCompilerJobResult &)
        = default;

private:
    Value m_value;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerOperationId)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerSha256)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerCanonicalJson)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerContractIdentity)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerSourceArtifactKind)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerSourceArtifact)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerSourceArtifacts)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerPdoDirection)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerPdoEntry)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerPdoMapping)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerStartupStage)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerStartupFailureAction)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerStartupSdo)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerDcProjection)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerManualRecoveryAction)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerManualEnvelope)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerSymbolMode)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerDeviceProjection)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerProjectProjection)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerDeviceSourceEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerTopologySlaveEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerFreshTopologyEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerSignedTargetProfileEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerProjectSnapshotEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerCommand)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerDiagnosticCategory)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerDiagnosticSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerDiagnostic)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerResultStatus)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerResultEnvelope)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerCompileRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerFinalizeRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerQueryRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerVerifyRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerCompileResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerFinalizeResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerQueryResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerVerifyResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerActivationProof)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageCompilerJobResult)
