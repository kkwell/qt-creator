// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatdata_global.h"
#include "projectsnapshot.h"
#include "semanticmappingattestation.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Data {

// Upper-layer activation identities are deliberately distinct from Product API
// RequestIds. One activation may span reconnects and multiple controller calls.
class ETHERCATDATA_EXPORT RuntimePackageActivationOperationId
{
public:
    RuntimePackageActivationOperationId() = default;
    explicit RuntimePackageActivationOperationId(QString value);

    const QString &value() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationOperationId &,
        const RuntimePackageActivationOperationId &) = default;

private:
    QString m_value;
};

inline size_t qHash(
    const RuntimePackageActivationOperationId &id, size_t seed = 0) noexcept
{
    return ::qHash(id.value(), seed);
}

// Opaque token minted by the project service for one exact document revision.
// Activation consumers compare it but never parse or derive it.
class ETHERCATDATA_EXPORT RuntimePackageActivationDocumentRevisionToken
{
public:
    RuntimePackageActivationDocumentRevisionToken() = default;
    explicit RuntimePackageActivationDocumentRevisionToken(QByteArray value);

    const QByteArray &value() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationDocumentRevisionToken &,
        const RuntimePackageActivationDocumentRevisionToken &) = default;

private:
    QByteArray m_value;
};

// Opaque token for the binding value observed with the document revision. It
// permits compare-and-set without exposing a second binding serialization or
// digest algorithm to Core. The same token type is used for the resulting
// binding returned by the project service.
class ETHERCATDATA_EXPORT RuntimePackageActivationOriginalBindingToken
{
public:
    RuntimePackageActivationOriginalBindingToken() = default;
    explicit RuntimePackageActivationOriginalBindingToken(QByteArray value);

    const QByteArray &value() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationOriginalBindingToken &,
        const RuntimePackageActivationOriginalBindingToken &) = default;

private:
    QByteArray m_value;
};

// One atomic project-service observation. The serialized bytes are the exact
// project bytes captured with the snapshot and both opaque compare-and-set
// tokens; consumers must not reserialize the snapshot to reconstruct them.
class ETHERCATDATA_EXPORT RuntimePackageActivationProjectCapture
{
public:
    RuntimePackageActivationProjectCapture() = default;
    RuntimePackageActivationProjectCapture(
        ProjectSnapshot snapshot,
        QByteArray serializedProject,
        RuntimePackageActivationDocumentRevisionToken documentRevision,
        RuntimePackageActivationOriginalBindingToken originalBinding);

    const ProjectSnapshot &snapshot() const;
    const QByteArray &serializedProject() const;
    const RuntimePackageActivationDocumentRevisionToken &documentRevision() const;
    const RuntimePackageActivationOriginalBindingToken &originalBinding() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationProjectCapture &,
        const RuntimePackageActivationProjectCapture &) = default;

private:
    ProjectSnapshot m_snapshot;
    QByteArray m_serializedProject;
    RuntimePackageActivationDocumentRevisionToken m_documentRevision;
    RuntimePackageActivationOriginalBindingToken m_originalBinding;
};

class ETHERCATDATA_EXPORT RuntimePackageActivationSha256
{
public:
    RuntimePackageActivationSha256() = default;
    explicit RuntimePackageActivationSha256(QByteArray value);

    const QByteArray &value() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationSha256 &,
        const RuntimePackageActivationSha256 &) = default;

private:
    QByteArray m_value;
};

class ETHERCATDATA_EXPORT RuntimePackageActivationIdentity
{
public:
    RuntimePackageActivationIdentity() = default;
    RuntimePackageActivationIdentity(
        RuntimePackageActivationOperationId operationId,
        ControllerConnectionScope scope,
        RuntimePackageActivationDocumentRevisionToken documentRevisionToken,
        RuntimePackageActivationOriginalBindingToken originalBindingToken,
        RuntimePackageActivationOriginalBindingToken targetBindingToken,
        QString bindingArtifactId,
        quint64 configurationId,
        quint64 expectedCatalogRevision,
        QByteArray expectedTopologyIdentity,
        RuntimePackageActivationSha256 packageSha256,
        RuntimePackageActivationSha256 compiledProjectSha256,
        RuntimePackageActivationSha256 effectiveProjectCompanionSha256,
        RuntimeSemanticMappingProof expectedMappingProof);

    const RuntimePackageActivationOperationId &operationId() const;
    const ControllerConnectionScope &scope() const;
    const RuntimePackageActivationDocumentRevisionToken &documentRevisionToken() const;
    const RuntimePackageActivationOriginalBindingToken &originalBindingToken() const;
    const RuntimePackageActivationOriginalBindingToken &targetBindingToken() const;
    const QString &bindingArtifactId() const;
    quint64 configurationId() const;
    quint64 expectedCatalogRevision() const;
    const QByteArray &expectedTopologyIdentity() const;
    const RuntimePackageActivationSha256 &packageSha256() const;
    const RuntimePackageActivationSha256 &compiledProjectSha256() const;
    const RuntimePackageActivationSha256 &effectiveProjectCompanionSha256() const;
    const RuntimeSemanticMappingProof &expectedMappingProof() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationIdentity &,
        const RuntimePackageActivationIdentity &) = default;

private:
    RuntimePackageActivationOperationId m_operationId;
    ControllerConnectionScope m_scope;
    RuntimePackageActivationDocumentRevisionToken m_documentRevisionToken;
    RuntimePackageActivationOriginalBindingToken m_originalBindingToken;
    RuntimePackageActivationOriginalBindingToken m_targetBindingToken;
    QString m_bindingArtifactId;
    quint64 m_configurationId = 0;
    quint64 m_expectedCatalogRevision = 0;
    QByteArray m_expectedTopologyIdentity;
    RuntimePackageActivationSha256 m_packageSha256;
    RuntimePackageActivationSha256 m_compiledProjectSha256;
    RuntimePackageActivationSha256 m_effectiveProjectCompanionSha256;
    RuntimeSemanticMappingProof m_expectedMappingProof;
};

// The fingerprint uses a fixed v1 domain and big-endian, length-prefixed
// encoding. It excludes OperationId (the idempotency key) and covers every
// other immutable identity field plus the rollback policy. Package, compiled
// project, and companion content are bound by their exact SHA-256 values. The
// opaque companion is never parsed or normalized here.
ETHERCATDATA_EXPORT RuntimePackageActivationSha256
runtimePackageActivationCanonicalRequestFingerprint(
    const RuntimePackageActivationIdentity &identity,
    bool rollbackOnActivationFailure);

// The effective-project companion remains opaque until its signed schema is
// frozen. Data and Core retain its exact bytes and SHA-256 only; they must not
// infer fields or implement a provisional canonicalization algorithm.
class ETHERCATDATA_EXPORT RuntimePackageActivationRequest
{
public:
    RuntimePackageActivationRequest() = default;
    RuntimePackageActivationRequest(
        RuntimePackageActivationIdentity identity,
        QByteArray packageBytes,
        QByteArray compiledProjectSource,
        QByteArray effectiveProjectCompanion,
        bool rollbackOnActivationFailure = true);

    const RuntimePackageActivationIdentity &identity() const;
    const QByteArray &packageBytes() const;
    const QByteArray &compiledProjectSource() const;
    const QByteArray &effectiveProjectCompanion() const;
    bool rollbackOnActivationFailure() const;
    const RuntimePackageActivationSha256 &fingerprint() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationRequest &,
        const RuntimePackageActivationRequest &) = default;

private:
    RuntimePackageActivationIdentity m_identity;
    QByteArray m_packageBytes;
    QByteArray m_compiledProjectSource;
    QByteArray m_effectiveProjectCompanion;
    bool m_rollbackOnActivationFailure = true;
    RuntimePackageActivationSha256 m_fingerprint;
};

enum class RuntimePackageActivationPhase {
    Idle,
    Queued,
    VerifyingInputs,
    CapturingProject,
    ProbingExistingPackage,
    AcquiringControl,
    DeployingPackage,
    VerifyingRuntimeIdentity,
    PersistingEvidence,
    CommittingProjectBinding,
    ReleasingControl,
    Canceling,
    Reconciling,
    AwaitingReconciliation,
    Finished,
};

enum class RuntimePackageActivationOutcome {
    Pending,
    SucceededWithExistingPackage,
    SucceededWithActivatedPackage,
    Canceled,
    FailedWithoutControllerChange,
    FailedAfterRollback,
    FailedControllerChangedWithoutBinding,
    OutcomeUnknown,
};

ETHERCATDATA_EXPORT bool runtimePackageActivationOutcomeIsTerminal(
    RuntimePackageActivationOutcome outcome);
ETHERCATDATA_EXPORT bool runtimePackageActivationOutcomeNeedsReconciliation(
    RuntimePackageActivationOutcome outcome);
ETHERCATDATA_EXPORT bool runtimePackageActivationStateNeedsReconciliation(
    RuntimePackageActivationPhase phase,
    RuntimePackageActivationOutcome outcome);
ETHERCATDATA_EXPORT bool runtimePackageActivationPhaseAllowsCancel(
    RuntimePackageActivationPhase phase);
ETHERCATDATA_EXPORT bool runtimePackageActivationTransitionIsAllowed(
    RuntimePackageActivationPhase fromPhase,
    RuntimePackageActivationOutcome fromOutcome,
    RuntimePackageActivationPhase toPhase,
    RuntimePackageActivationOutcome toOutcome);
ETHERCATDATA_EXPORT bool runtimePackageActivationProviderOperationIsAllowed(
    ControllerOperation operation);

enum class RuntimePackageActivationProviderAction {
    None,
    AcquireControl,
    ReleaseControl,
    DeployPackage,
};

// One immutable observation of the controller-side package/runtime identity.
// A non-Active runtime may still have a persistent selector, but an Active
// runtime must carry the complete v1.13+ semantic attestation and epoch.
class ETHERCATDATA_EXPORT RuntimePackageActivationControllerEvidence
{
public:
    RuntimePackageActivationControllerEvidence() = default;
    RuntimePackageActivationControllerEvidence(
        ControllerConnectionScope scope,
        quint64 sessionGeneration,
        quint64 observationSessionId,
        quint64 controlLeaseOwnerSessionId,
        quint64 bootId,
        std::optional<ControllerPackageSelector> persistentActive,
        ControllerPackageState runtimePackageState,
        ControllerServiceState serviceState,
        std::optional<RuntimeSemanticMappingAttestation> mappingAttestation,
        QDateTime observedAt);

    const ControllerConnectionScope &scope() const;
    quint64 sessionGeneration() const;
    quint64 observationSessionId() const;
    quint64 controlLeaseOwnerSessionId() const;
    bool ownsControlLease() const;
    quint64 bootId() const;
    const std::optional<ControllerPackageSelector> &persistentActive() const;
    ControllerPackageState runtimePackageState() const;
    ControllerServiceState serviceState() const;
    const std::optional<RuntimeSemanticMappingAttestation> &mappingAttestation() const;
    const RuntimePackageActivationSha256 &evidenceSha256() const;
    const QDateTime &observedAt() const;
    bool isValid() const;
    bool describesSameControllerStateAs(
        const RuntimePackageActivationControllerEvidence &other) const;
    bool matchesActivatedIdentity(const RuntimePackageActivationIdentity &identity) const;

    friend bool operator==(
        const RuntimePackageActivationControllerEvidence &,
        const RuntimePackageActivationControllerEvidence &) = default;

private:
    ControllerConnectionScope m_scope;
    quint64 m_sessionGeneration = 0;
    quint64 m_observationSessionId = 0;
    quint64 m_controlLeaseOwnerSessionId = 0;
    quint64 m_bootId = 0;
    std::optional<ControllerPackageSelector> m_persistentActive;
    ControllerPackageState m_runtimePackageState = ControllerPackageState::Unavailable;
    ControllerServiceState m_serviceState = ControllerServiceState::Unknown;
    std::optional<RuntimeSemanticMappingAttestation> m_mappingAttestation;
    RuntimePackageActivationSha256 m_evidenceSha256;
    QDateTime m_observedAt;
};

enum class RuntimePackageActivationDeploymentOutcome {
    Succeeded,
    Failed,
    OutcomeUnknown,
};

class ETHERCATDATA_EXPORT RuntimePackageActivationDeploymentEvidence
{
public:
    RuntimePackageActivationDeploymentEvidence() = default;
    RuntimePackageActivationDeploymentEvidence(
        quint64 providerSessionGeneration,
        quint64 providerSessionId,
        quint64 providerBootId,
        ControllerPackageDeploymentProgress progress,
        QDateTime observedAt);

    const QString &providerOperationId() const;
    quint64 providerSessionGeneration() const;
    quint64 providerSessionId() const;
    quint64 providerBootId() const;
    const RuntimePackageActivationSha256 &artifactSha256() const;
    const std::optional<ControllerPackageSelector> &previousActive() const;
    const std::optional<ControllerPackageSelector> &candidate() const;
    const ControllerPackageDeploymentProgress &progress() const;
    const RuntimePackageActivationSha256 &internalAuditSha256() const;
    const RuntimePackageActivationSha256 &evidenceSha256() const;
    RuntimePackageActivationDeploymentOutcome outcome() const;
    const QDateTime &observedAt() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationDeploymentEvidence &,
        const RuntimePackageActivationDeploymentEvidence &) = default;

private:
    quint64 m_providerSessionGeneration = 0;
    quint64 m_providerSessionId = 0;
    quint64 m_providerBootId = 0;
    ControllerPackageDeploymentProgress m_progress;
    RuntimePackageActivationSha256 m_artifactSha256;
    RuntimePackageActivationSha256 m_internalAuditSha256;
    RuntimePackageActivationSha256 m_evidenceSha256;
    RuntimePackageActivationDeploymentOutcome m_outcome
        = RuntimePackageActivationDeploymentOutcome::OutcomeUnknown;
    QDateTime m_observedAt;
};

enum class RuntimePackageActivationProjectCommitDisposition {
    CompareAndSetCommitted,
    AlreadyExact,
};

class ETHERCATDATA_EXPORT RuntimePackageActivationProjectCommit
{
public:
    RuntimePackageActivationProjectCommit() = default;
    RuntimePackageActivationProjectCommit(
        RuntimePackageActivationDocumentRevisionToken originalDocumentRevision,
        RuntimePackageActivationOriginalBindingToken originalBinding,
        RuntimePackageActivationDocumentRevisionToken resultingDocumentRevision,
        RuntimePackageActivationOriginalBindingToken resultingBinding,
        RuntimePackageActivationProjectCommitDisposition disposition,
        QDateTime committedAt);

    const RuntimePackageActivationDocumentRevisionToken &originalDocumentRevision() const;
    const RuntimePackageActivationOriginalBindingToken &originalBinding() const;
    const RuntimePackageActivationDocumentRevisionToken &resultingDocumentRevision() const;
    const RuntimePackageActivationOriginalBindingToken &resultingBinding() const;
    RuntimePackageActivationProjectCommitDisposition disposition() const;
    const RuntimePackageActivationSha256 &evidenceSha256() const;
    const QDateTime &committedAt() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationProjectCommit &,
        const RuntimePackageActivationProjectCommit &) = default;

private:
    RuntimePackageActivationDocumentRevisionToken m_originalDocumentRevision;
    RuntimePackageActivationOriginalBindingToken m_originalBinding;
    RuntimePackageActivationDocumentRevisionToken m_resultingDocumentRevision;
    RuntimePackageActivationOriginalBindingToken m_resultingBinding;
    RuntimePackageActivationProjectCommitDisposition m_disposition
        = RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted;
    RuntimePackageActivationSha256 m_evidenceSha256;
    QDateTime m_committedAt;
};

enum class RuntimePackageActivationProjectCompareAndSetDisposition {
    Invalid,
    Stale,
    CompareAndSetCommitted,
    AlreadyExact,
};

class ETHERCATDATA_EXPORT RuntimePackageActivationProjectCompareAndSetResult
{
public:
    RuntimePackageActivationProjectCompareAndSetResult() = default;
    RuntimePackageActivationProjectCompareAndSetResult(
        RuntimePackageActivationProjectCompareAndSetDisposition disposition,
        std::optional<RuntimePackageActivationProjectCommit> commit = {});

    RuntimePackageActivationProjectCompareAndSetDisposition disposition() const;
    const std::optional<RuntimePackageActivationProjectCommit> &commit() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationProjectCompareAndSetResult &,
        const RuntimePackageActivationProjectCompareAndSetResult &) = default;

private:
    RuntimePackageActivationProjectCompareAndSetDisposition m_disposition
        = RuntimePackageActivationProjectCompareAndSetDisposition::Invalid;
    std::optional<RuntimePackageActivationProjectCommit> m_commit;
};

enum class RuntimePackageActivationCancellationControllerResult {
    Pending,
    NoControllerMutation,
    ControllerUnchanged,
    RolledBack,
    OutcomeUnknown,
};

ETHERCATDATA_EXPORT RuntimePackageActivationSha256
runtimePackageActivationCanonicalCancelFingerprint(
    const RuntimePackageActivationOperationId &operationId,
    quint64 expectedRecordRevision,
    const RuntimePackageActivationDocumentRevisionToken &expectedDocumentRevision,
    const RuntimePackageActivationOriginalBindingToken &expectedBinding);

class ETHERCATDATA_EXPORT RuntimePackageActivationCancelRequest
{
public:
    RuntimePackageActivationCancelRequest() = default;
    RuntimePackageActivationCancelRequest(
        RuntimePackageActivationOperationId operationId,
        quint64 expectedRecordRevision,
        RuntimePackageActivationDocumentRevisionToken expectedDocumentRevision,
        RuntimePackageActivationOriginalBindingToken expectedBinding);

    const RuntimePackageActivationOperationId &operationId() const;
    quint64 expectedRecordRevision() const;
    const RuntimePackageActivationDocumentRevisionToken &expectedDocumentRevision() const;
    const RuntimePackageActivationOriginalBindingToken &expectedBinding() const;
    const RuntimePackageActivationSha256 &fingerprint() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationCancelRequest &,
        const RuntimePackageActivationCancelRequest &) = default;

private:
    RuntimePackageActivationOperationId m_operationId;
    quint64 m_expectedRecordRevision = 0;
    RuntimePackageActivationDocumentRevisionToken m_expectedDocumentRevision;
    RuntimePackageActivationOriginalBindingToken m_expectedBinding;
    RuntimePackageActivationSha256 m_fingerprint;
};

class ETHERCATDATA_EXPORT RuntimePackageActivationCancellation
{
public:
    RuntimePackageActivationCancellation() = default;
    RuntimePackageActivationCancellation(
        quint64 expectedRecordRevision,
        RuntimePackageActivationDocumentRevisionToken expectedDocumentRevision,
        RuntimePackageActivationOriginalBindingToken expectedBinding,
        RuntimePackageActivationSha256 requestFingerprint,
        RuntimePackageActivationPhase requestedPhase,
        RuntimePackageActivationCancellationControllerResult controllerResult,
        std::optional<RuntimePackageActivationSha256> evidenceSha256,
        QDateTime requestedAt);

    quint64 expectedRecordRevision() const;
    const RuntimePackageActivationDocumentRevisionToken &expectedDocumentRevision() const;
    const RuntimePackageActivationOriginalBindingToken &expectedBinding() const;
    const RuntimePackageActivationSha256 &requestFingerprint() const;
    RuntimePackageActivationPhase requestedPhase() const;
    RuntimePackageActivationCancellationControllerResult controllerResult() const;
    const std::optional<RuntimePackageActivationSha256> &evidenceSha256() const;
    const QDateTime &requestedAt() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationCancellation &,
        const RuntimePackageActivationCancellation &) = default;

private:
    quint64 m_expectedRecordRevision = 0;
    RuntimePackageActivationDocumentRevisionToken m_expectedDocumentRevision;
    RuntimePackageActivationOriginalBindingToken m_expectedBinding;
    RuntimePackageActivationSha256 m_requestFingerprint;
    RuntimePackageActivationPhase m_requestedPhase = RuntimePackageActivationPhase::Idle;
    RuntimePackageActivationCancellationControllerResult m_controllerResult
        = RuntimePackageActivationCancellationControllerResult::Pending;
    std::optional<RuntimePackageActivationSha256> m_evidenceSha256;
    QDateTime m_requestedAt;
};

enum class RuntimePackageActivationAuditKind {
    IntentPersisted,
    PhaseTransition,
    ProviderRequestSent,
    ProviderTerminalResponse,
    ProviderOutcomeReconciled,
    ControlLeaseReconciled,
    DeploymentEvidenceCaptured,
    ControllerEvidenceCaptured,
    ProjectCompareAndSet,
    CancellationRequested,
    ReconciliationStarted,
    Completed,
};

enum class RuntimePackageActivationProviderReconciliation {
    None,
    Applied,
    NotApplied,
};

class ETHERCATDATA_EXPORT RuntimePackageActivationAuditEvent
{
public:
    RuntimePackageActivationAuditEvent() = default;
    RuntimePackageActivationAuditEvent(
        quint64 sequence,
        RuntimePackageActivationAuditKind kind,
        RuntimePackageActivationPhase phase,
        RuntimePackageActivationOutcome outcome,
        RuntimePackageActivationProviderAction providerAction,
        RuntimePackageActivationProviderReconciliation providerReconciliation,
        QString providerOperationId,
        quint64 providerSessionGeneration,
        quint64 providerSessionId,
        quint64 providerBootId,
        std::optional<quint64> providerRequestId,
        std::optional<qint32> providerStatus,
        std::optional<qint32> providerOperationResult,
        std::optional<RuntimePackageActivationSha256> evidenceSha256,
        QString code,
        QString detail,
        QDateTime occurredAt);

    quint64 sequence() const;
    RuntimePackageActivationAuditKind kind() const;
    RuntimePackageActivationPhase phase() const;
    RuntimePackageActivationOutcome outcome() const;
    RuntimePackageActivationProviderAction providerAction() const;
    RuntimePackageActivationProviderReconciliation providerReconciliation() const;
    const QString &providerOperationId() const;
    quint64 providerSessionGeneration() const;
    quint64 providerSessionId() const;
    quint64 providerBootId() const;
    const std::optional<quint64> &providerRequestId() const;
    const std::optional<qint32> &providerStatus() const;
    const std::optional<qint32> &providerOperationResult() const;
    const std::optional<RuntimePackageActivationSha256> &evidenceSha256() const;
    const QString &code() const;
    const QString &detail() const;
    const QDateTime &occurredAt() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationAuditEvent &,
        const RuntimePackageActivationAuditEvent &) = default;

private:
    quint64 m_sequence = 0;
    RuntimePackageActivationAuditKind m_kind = RuntimePackageActivationAuditKind::IntentPersisted;
    RuntimePackageActivationPhase m_phase = RuntimePackageActivationPhase::Idle;
    RuntimePackageActivationOutcome m_outcome = RuntimePackageActivationOutcome::Pending;
    RuntimePackageActivationProviderAction m_providerAction
        = RuntimePackageActivationProviderAction::None;
    RuntimePackageActivationProviderReconciliation m_providerReconciliation
        = RuntimePackageActivationProviderReconciliation::None;
    QString m_providerOperationId;
    quint64 m_providerSessionGeneration = 0;
    quint64 m_providerSessionId = 0;
    quint64 m_providerBootId = 0;
    std::optional<quint64> m_providerRequestId;
    std::optional<qint32> m_providerStatus;
    std::optional<qint32> m_providerOperationResult;
    std::optional<RuntimePackageActivationSha256> m_evidenceSha256;
    QString m_code;
    QString m_detail;
    QDateTime m_occurredAt;
};

class ETHERCATDATA_EXPORT RuntimePackageActivationRecord
{
public:
    RuntimePackageActivationRecord() = default;
    RuntimePackageActivationRecord(
        RuntimePackageActivationIdentity identity,
        RuntimePackageActivationSha256 requestFingerprint,
        bool rollbackOnActivationFailure,
        quint64 revision,
        RuntimePackageActivationPhase phase,
        RuntimePackageActivationOutcome outcome,
        QList<RuntimePackageActivationAuditEvent> audit,
        QString detail,
        QDateTime startedAt,
        QDateTime updatedAt,
        QDateTime completedAt = {},
        std::optional<RuntimePackageActivationControllerEvidence> beforeController = {},
        std::optional<RuntimePackageActivationControllerEvidence> afterController = {},
        std::optional<RuntimePackageActivationProjectCommit> projectCommit = {},
        std::optional<RuntimePackageActivationCancellation> cancellation = {},
        std::optional<RuntimePackageActivationDeploymentEvidence> deploymentEvidence = {},
        QList<RuntimePackageActivationControllerEvidence> controllerEvidenceHistory = {});

    const RuntimePackageActivationIdentity &identity() const;
    const RuntimePackageActivationSha256 &requestFingerprint() const;
    bool rollbackOnActivationFailure() const;
    quint64 revision() const;
    RuntimePackageActivationPhase phase() const;
    RuntimePackageActivationOutcome outcome() const;
    const QList<RuntimePackageActivationAuditEvent> &audit() const;
    const QString &detail() const;
    const QDateTime &startedAt() const;
    const QDateTime &updatedAt() const;
    const QDateTime &completedAt() const;
    const std::optional<RuntimePackageActivationControllerEvidence> &beforeController() const;
    const std::optional<RuntimePackageActivationControllerEvidence> &afterController() const;
    const std::optional<RuntimePackageActivationProjectCommit> &projectCommit() const;
    const std::optional<RuntimePackageActivationCancellation> &cancellation() const;
    const std::optional<RuntimePackageActivationDeploymentEvidence> &
    deploymentEvidence() const;
    const QList<RuntimePackageActivationControllerEvidence> &
    controllerEvidenceHistory() const;
    bool needsReconciliation() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationRecord &,
        const RuntimePackageActivationRecord &) = default;

private:
    RuntimePackageActivationIdentity m_identity;
    RuntimePackageActivationSha256 m_requestFingerprint;
    bool m_rollbackOnActivationFailure = true;
    quint64 m_revision = 0;
    RuntimePackageActivationPhase m_phase = RuntimePackageActivationPhase::Idle;
    RuntimePackageActivationOutcome m_outcome = RuntimePackageActivationOutcome::Pending;
    QList<RuntimePackageActivationAuditEvent> m_audit;
    QString m_detail;
    QDateTime m_startedAt;
    QDateTime m_updatedAt;
    QDateTime m_completedAt;
    std::optional<RuntimePackageActivationControllerEvidence> m_beforeController;
    std::optional<RuntimePackageActivationControllerEvidence> m_afterController;
    std::optional<RuntimePackageActivationProjectCommit> m_projectCommit;
    std::optional<RuntimePackageActivationCancellation> m_cancellation;
    std::optional<RuntimePackageActivationDeploymentEvidence> m_deploymentEvidence;
    QList<RuntimePackageActivationControllerEvidence> m_controllerEvidenceHistory;
};

class ETHERCATDATA_EXPORT RuntimePackageActivationSnapshot
{
public:
    RuntimePackageActivationSnapshot() = default;
    RuntimePackageActivationSnapshot(
        quint64 sequence,
        QList<RuntimePackageActivationRecord> records,
        QDateTime capturedAt);

    quint64 sequence() const;
    const QList<RuntimePackageActivationRecord> &records() const;
    const QDateTime &capturedAt() const;
    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationSnapshot &,
        const RuntimePackageActivationSnapshot &) = default;

private:
    quint64 m_sequence = 0;
    QList<RuntimePackageActivationRecord> m_records;
    QDateTime m_capturedAt;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationOperationId)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationDocumentRevisionToken)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationOriginalBindingToken)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationProjectCapture)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationSha256)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationIdentity)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationPhase)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationOutcome)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationProviderAction)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationControllerEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationDeploymentOutcome)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationDeploymentEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationProjectCommitDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationProjectCommit)
Q_DECLARE_METATYPE(
    EtherCAT::Data::RuntimePackageActivationProjectCompareAndSetDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationProjectCompareAndSetResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationCancellationControllerResult)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationCancelRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationCancellation)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationAuditKind)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationProviderReconciliation)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationAuditEvent)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationRecord)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimePackageActivationSnapshot)
