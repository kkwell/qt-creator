// Copyright (C) 2026 Embed Labs

#include "runtimepackageactivation.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace EtherCAT::Data {

namespace {

bool isCanonicalText(const QString &value, qsizetype maximumSize)
{
    if (value.isEmpty() || value.size() > maximumSize || value != value.trimmed())
        return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.isNull() || character.category() == QChar::Other_Control;
    });
}

bool isValidOpaqueToken(const QByteArray &value)
{
    return !value.isEmpty() && value.size() <= 256;
}

bool isValidSha256(const QByteArray &value)
{
    if (value.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256))
        return false;
    quint8 aggregate = 0;
    for (char byte : value)
        aggregate |= quint8(byte);
    return aggregate != 0;
}

bool sha256Matches(const QByteArray &bytes, const RuntimePackageActivationSha256 &expected)
{
    return !bytes.isEmpty() && expected.isValid()
           && QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == expected.value();
}

bool phaseIsKnown(RuntimePackageActivationPhase phase)
{
    switch (phase) {
    case RuntimePackageActivationPhase::Idle:
    case RuntimePackageActivationPhase::Queued:
    case RuntimePackageActivationPhase::VerifyingInputs:
    case RuntimePackageActivationPhase::CapturingProject:
    case RuntimePackageActivationPhase::ProbingExistingPackage:
    case RuntimePackageActivationPhase::AcquiringControl:
    case RuntimePackageActivationPhase::DeployingPackage:
    case RuntimePackageActivationPhase::VerifyingRuntimeIdentity:
    case RuntimePackageActivationPhase::PersistingEvidence:
    case RuntimePackageActivationPhase::CommittingProjectBinding:
    case RuntimePackageActivationPhase::ReleasingControl:
    case RuntimePackageActivationPhase::Canceling:
    case RuntimePackageActivationPhase::Reconciling:
    case RuntimePackageActivationPhase::AwaitingReconciliation:
    case RuntimePackageActivationPhase::Finished:
        return true;
    }
    return false;
}

bool outcomeIsKnown(RuntimePackageActivationOutcome outcome)
{
    switch (outcome) {
    case RuntimePackageActivationOutcome::Pending:
    case RuntimePackageActivationOutcome::SucceededWithExistingPackage:
    case RuntimePackageActivationOutcome::SucceededWithActivatedPackage:
    case RuntimePackageActivationOutcome::Canceled:
    case RuntimePackageActivationOutcome::FailedWithoutControllerChange:
    case RuntimePackageActivationOutcome::FailedAfterRollback:
    case RuntimePackageActivationOutcome::FailedControllerChangedWithoutBinding:
    case RuntimePackageActivationOutcome::OutcomeUnknown:
        return true;
    }
    return false;
}

bool phaseOutcomePairIsValid(
    RuntimePackageActivationPhase phase, RuntimePackageActivationOutcome outcome)
{
    if (!phaseIsKnown(phase) || !outcomeIsKnown(outcome)
        || phase == RuntimePackageActivationPhase::Idle) {
        return false;
    }
    if (outcome == RuntimePackageActivationOutcome::OutcomeUnknown) {
        return phase == RuntimePackageActivationPhase::AwaitingReconciliation
               || phase == RuntimePackageActivationPhase::Reconciling;
    }
    if (runtimePackageActivationOutcomeIsTerminal(outcome))
        return phase == RuntimePackageActivationPhase::Finished;
    return phase != RuntimePackageActivationPhase::AwaitingReconciliation
           && phase != RuntimePackageActivationPhase::Reconciling
           && phase != RuntimePackageActivationPhase::Finished;
}

bool packageStateIsKnown(ControllerPackageState state)
{
    switch (state) {
    case ControllerPackageState::Empty:
    case ControllerPackageState::Staged:
    case ControllerPackageState::Accepted:
    case ControllerPackageState::Active:
    case ControllerPackageState::Rejected:
        return true;
    case ControllerPackageState::Unavailable:
        return false;
    }
    return false;
}

bool serviceStateIsKnown(ControllerServiceState state)
{
    switch (state) {
    case ControllerServiceState::Boot:
    case ControllerServiceState::Configuring:
    case ControllerServiceState::SafeOperational:
    case ControllerServiceState::OperationalSafe:
    case ControllerServiceState::Running:
    case ControllerServiceState::Stopping:
    case ControllerServiceState::Fault:
    case ControllerServiceState::Recovering:
    case ControllerServiceState::Shutdown:
    case ControllerServiceState::Paused:
        return true;
    case ControllerServiceState::Unknown:
        return false;
    }
    return false;
}

bool projectCommitDispositionIsKnown(RuntimePackageActivationProjectCommitDisposition disposition)
{
    switch (disposition) {
    case RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted:
    case RuntimePackageActivationProjectCommitDisposition::AlreadyExact:
        return true;
    }
    return false;
}

bool projectCompareAndSetDispositionIsKnown(
    RuntimePackageActivationProjectCompareAndSetDisposition disposition)
{
    switch (disposition) {
    case RuntimePackageActivationProjectCompareAndSetDisposition::Invalid:
        return false;
    case RuntimePackageActivationProjectCompareAndSetDisposition::Stale:
    case RuntimePackageActivationProjectCompareAndSetDisposition::CompareAndSetCommitted:
    case RuntimePackageActivationProjectCompareAndSetDisposition::AlreadyExact:
        return true;
    }
    return false;
}

bool cancellationResultIsKnown(RuntimePackageActivationCancellationControllerResult result)
{
    switch (result) {
    case RuntimePackageActivationCancellationControllerResult::Pending:
    case RuntimePackageActivationCancellationControllerResult::NoControllerMutation:
    case RuntimePackageActivationCancellationControllerResult::ControllerUnchanged:
    case RuntimePackageActivationCancellationControllerResult::RolledBack:
    case RuntimePackageActivationCancellationControllerResult::OutcomeUnknown:
        return true;
    }
    return false;
}

bool auditKindIsKnown(RuntimePackageActivationAuditKind kind)
{
    switch (kind) {
    case RuntimePackageActivationAuditKind::IntentPersisted:
    case RuntimePackageActivationAuditKind::PhaseTransition:
    case RuntimePackageActivationAuditKind::ProviderRequestSent:
    case RuntimePackageActivationAuditKind::ProviderTerminalResponse:
    case RuntimePackageActivationAuditKind::ProviderOutcomeReconciled:
    case RuntimePackageActivationAuditKind::ControlLeaseReconciled:
    case RuntimePackageActivationAuditKind::DeploymentEvidenceCaptured:
    case RuntimePackageActivationAuditKind::ControllerEvidenceCaptured:
    case RuntimePackageActivationAuditKind::ProjectCompareAndSet:
    case RuntimePackageActivationAuditKind::CancellationRequested:
    case RuntimePackageActivationAuditKind::ReconciliationStarted:
    case RuntimePackageActivationAuditKind::Completed:
        return true;
    }
    return false;
}

bool deploymentOutcomeIsKnown(RuntimePackageActivationDeploymentOutcome outcome)
{
    switch (outcome) {
    case RuntimePackageActivationDeploymentOutcome::Succeeded:
    case RuntimePackageActivationDeploymentOutcome::Failed:
    case RuntimePackageActivationDeploymentOutcome::OutcomeUnknown:
        return true;
    }
    return false;
}

bool providerReconciliationIsKnown(
    RuntimePackageActivationProviderReconciliation reconciliation)
{
    switch (reconciliation) {
    case RuntimePackageActivationProviderReconciliation::None:
    case RuntimePackageActivationProviderReconciliation::Applied:
    case RuntimePackageActivationProviderReconciliation::NotApplied:
        return true;
    }
    return false;
}

bool providerActionIsKnown(RuntimePackageActivationProviderAction action)
{
    switch (action) {
    case RuntimePackageActivationProviderAction::None:
    case RuntimePackageActivationProviderAction::AcquireControl:
    case RuntimePackageActivationProviderAction::ReleaseControl:
    case RuntimePackageActivationProviderAction::DeployPackage:
        return true;
    }
    return false;
}

bool operationMayChangeController(RuntimePackageActivationProviderAction action)
{
    switch (action) {
    case RuntimePackageActivationProviderAction::AcquireControl:
    case RuntimePackageActivationProviderAction::ReleaseControl:
    case RuntimePackageActivationProviderAction::DeployPackage:
        return true;
    default:
        return false;
    }
}

bool providerOperationIsAllowedInPhase(
    RuntimePackageActivationProviderAction action, RuntimePackageActivationPhase phase)
{
    switch (action) {
    case RuntimePackageActivationProviderAction::AcquireControl:
        return phase == RuntimePackageActivationPhase::AcquiringControl;
    case RuntimePackageActivationProviderAction::ReleaseControl:
        return phase == RuntimePackageActivationPhase::ReleasingControl
               || phase == RuntimePackageActivationPhase::Reconciling;
    case RuntimePackageActivationProviderAction::DeployPackage:
        return phase == RuntimePackageActivationPhase::DeployingPackage;
    default:
        return false;
    }
}

class ActivationFingerprintBuilder
{
public:
    void addRawBytes(QByteArrayView value)
    {
        m_hash.addData(value);
    }

    void addByte(quint8 value)
    {
        const char byte = char(value);
        m_hash.addData(QByteArrayView(&byte, 1));
    }

    void addU64(quint64 value)
    {
        char bytes[8];
        for (int index = 7; index >= 0; --index) {
            bytes[index] = char(value & 0xff);
            value >>= 8;
        }
        m_hash.addData(QByteArrayView(bytes, sizeof(bytes)));
    }

    void addI64(qint64 value)
    {
        addU64(quint64(value));
    }

    void addBool(bool value)
    {
        addByte(value ? 1 : 0);
    }

    void addBytes(QByteArrayView value)
    {
        addU64(quint64(value.size()));
        m_hash.addData(value);
    }

    void addString(const QString &value)
    {
        const QByteArray encoded = value.toUtf8();
        addBytes(encoded);
    }

    RuntimePackageActivationSha256 result()
    {
        return RuntimePackageActivationSha256{m_hash.result()};
    }

private:
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
};

void addScope(ActivationFingerprintBuilder &builder, const ControllerConnectionScope &scope)
{
    builder.addString(scope.projectId.toString());
    builder.addString(scope.masterId.toString());
}

void addSelector(
    ActivationFingerprintBuilder &builder,
    const std::optional<ControllerPackageSelector> &selector)
{
    builder.addBool(selector.has_value());
    if (!selector)
        return;
    builder.addU64(quint64(selector->slot));
    builder.addU64(selector->generation);
    builder.addU64(selector->configurationId);
}

void addProof(ActivationFingerprintBuilder &builder, const RuntimeSemanticMappingProof &proof)
{
    builder.addU64(proof.formatVersion);
    builder.addU64(proof.bindingCount);
    builder.addBool(proof.packageSigned);
    builder.addBool(proof.signatureVerified);
    builder.addBool(proof.semanticBindingVerified);
    builder.addU64(quint64(proof.trust));
    builder.addBytes(proof.packageSha256);
    builder.addBytes(proof.manifestSha256);
    builder.addBytes(proof.mappingSha256);
    builder.addBytes(proof.resourceRecordsSha256);
    builder.addBytes(proof.resourceSectionSha256);
    builder.addBytes(proof.topologySha256);
    builder.addBytes(proof.signingKeyIdSha256);
}

void addAttestation(
    ActivationFingerprintBuilder &builder,
    const std::optional<RuntimeSemanticMappingAttestation> &attestation)
{
    builder.addBool(attestation.has_value());
    if (!attestation)
        return;
    addScope(builder, attestation->scope);
    builder.addU64(attestation->sessionGeneration);
    builder.addU64(attestation->epoch.controllerBootId);
    builder.addU64(quint64(attestation->epoch.activePackageSlot));
    builder.addU64(attestation->epoch.activePackageGeneration);
    builder.addU64(attestation->epoch.configurationId);
    builder.addU64(attestation->epoch.topologyGeneration);
    builder.addU64(attestation->epoch.runtimeGeneration);
    builder.addU64(attestation->epoch.catalogRevision);
    builder.addBytes(attestation->epoch.topologyIdentity);
    addProof(builder, attestation->proof);
    builder.addI64(attestation->receivedAt.toMSecsSinceEpoch());
}

RuntimePackageActivationSha256 canonicalControllerEvidenceSha256(
    const ControllerConnectionScope &scope,
    quint64 sessionGeneration,
    quint64 observationSessionId,
    quint64 controlLeaseOwnerSessionId,
    quint64 bootId,
    const std::optional<ControllerPackageSelector> &persistentActive,
    ControllerPackageState runtimePackageState,
    ControllerServiceState serviceState,
    const std::optional<RuntimeSemanticMappingAttestation> &mappingAttestation,
    const QDateTime &observedAt)
{
    ActivationFingerprintBuilder builder;
    builder.addBytes(QByteArrayView("embed-labs.runtime-package-activation.controller-evidence.v1"));
    addScope(builder, scope);
    builder.addU64(sessionGeneration);
    builder.addU64(observationSessionId);
    builder.addU64(controlLeaseOwnerSessionId);
    builder.addU64(bootId);
    addSelector(builder, persistentActive);
    builder.addU64(quint64(runtimePackageState));
    builder.addU64(quint64(serviceState));
    addAttestation(builder, mappingAttestation);
    builder.addI64(observedAt.toMSecsSinceEpoch());
    return builder.result();
}

RuntimePackageActivationSha256 canonicalProjectCommitSha256(
    const RuntimePackageActivationDocumentRevisionToken &originalDocumentRevision,
    const RuntimePackageActivationOriginalBindingToken &originalBinding,
    const RuntimePackageActivationDocumentRevisionToken &resultingDocumentRevision,
    const RuntimePackageActivationOriginalBindingToken &resultingBinding,
    RuntimePackageActivationProjectCommitDisposition disposition,
    const QDateTime &committedAt)
{
    ActivationFingerprintBuilder builder;
    builder.addBytes(QByteArrayView("embed-labs.runtime-package-activation.project-commit.v1"));
    builder.addBytes(originalDocumentRevision.value());
    builder.addBytes(originalBinding.value());
    builder.addBytes(resultingDocumentRevision.value());
    builder.addBytes(resultingBinding.value());
    builder.addU64(quint64(disposition));
    builder.addI64(committedAt.toMSecsSinceEpoch());
    return builder.result();
}

void addDeploymentProgress(
    ActivationFingerprintBuilder &builder,
    const ControllerPackageDeploymentProgress &progress)
{
    builder.addString(progress.operationId);
    builder.addBytes(progress.artifactSha256);
    builder.addU64(quint64(progress.state));
    builder.addI64(progress.totalBytes);
    builder.addI64(progress.transferredBytes);
    addSelector(builder, progress.candidate);
    addSelector(builder, progress.previousActive);
    builder.addBool(progress.status.has_value());
    if (progress.status)
        builder.addI64(*progress.status);
    builder.addBool(progress.operationResult.has_value());
    if (progress.operationResult)
        builder.addI64(*progress.operationResult);
    builder.addString(progress.detail);
    builder.addU64(quint64(progress.audit.size()));
    for (const ControllerPackageDeploymentAuditEvent &event : progress.audit) {
        builder.addU64(event.sequence);
        builder.addU64(quint64(event.operation));
        builder.addBool(event.requestId.has_value());
        if (event.requestId)
            builder.addU64(*event.requestId);
        builder.addBool(event.status.has_value());
        if (event.status)
            builder.addI64(*event.status);
        builder.addBool(event.operationResult.has_value());
        if (event.operationResult)
            builder.addI64(*event.operationResult);
        builder.addString(event.detail);
        builder.addI64(event.occurredAt.toMSecsSinceEpoch());
    }
    builder.addI64(progress.startedAt.toMSecsSinceEpoch());
    builder.addI64(progress.completedAt.toMSecsSinceEpoch());
}

RuntimePackageActivationSha256 canonicalDeploymentAuditSha256(
    const ControllerPackageDeploymentProgress &progress)
{
    ActivationFingerprintBuilder builder;
    builder.addBytes(QByteArrayView("embed-labs.runtime-package-activation.deployment-audit.v1"));
    addDeploymentProgress(builder, progress);
    return builder.result();
}

RuntimePackageActivationSha256 canonicalDeploymentEvidenceSha256(
    quint64 providerSessionGeneration,
    quint64 providerSessionId,
    quint64 providerBootId,
    const ControllerPackageDeploymentProgress &progress,
    const QDateTime &observedAt)
{
    ActivationFingerprintBuilder builder;
    builder.addBytes(QByteArrayView("embed-labs.runtime-package-activation.deployment-evidence.v1"));
    builder.addU64(providerSessionGeneration);
    builder.addU64(providerSessionId);
    builder.addU64(providerBootId);
    addDeploymentProgress(builder, progress);
    builder.addI64(observedAt.toMSecsSinceEpoch());
    return builder.result();
}

bool optionalAttestationsDescribeSameState(
    const std::optional<RuntimeSemanticMappingAttestation> &left,
    const std::optional<RuntimeSemanticMappingAttestation> &right)
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return true;
    return left->scope == right->scope && left->epoch == right->epoch
           && left->proof == right->proof;
}

bool packageRuntimeStateMatches(
    const RuntimePackageActivationControllerEvidence &left,
    const RuntimePackageActivationControllerEvidence &right)
{
    return left.isValid() && right.isValid() && left.scope() == right.scope()
           && left.bootId() == right.bootId()
           && left.persistentActive() == right.persistentActive()
           && left.runtimePackageState() == right.runtimePackageState()
           && left.serviceState() == right.serviceState()
           && optionalAttestationsDescribeSameState(
               left.mappingAttestation(), right.mappingAttestation());
}

QString providerRequestKey(
    RuntimePackageActivationProviderAction action,
    const QString &operationId,
    quint64 sessionGeneration,
    quint64 sessionId,
    quint64 bootId,
    const std::optional<quint64> &requestId)
{
    return QString::number(int(action)) + QLatin1Char(':') + operationId
           + QLatin1Char(':') + QString::number(sessionGeneration)
           + QLatin1Char(':') + QString::number(sessionId) + QLatin1Char(':')
           + QString::number(bootId) + QLatin1Char(':')
           + (requestId ? QString::number(*requestId) : QStringLiteral("-"));
}

bool auditHasEvidence(
    const QList<RuntimePackageActivationAuditEvent> &audit,
    RuntimePackageActivationAuditKind kind,
    const RuntimePackageActivationSha256 &evidence)
{
    return std::any_of(audit.cbegin(), audit.cend(), [&](const auto &event) {
        return event.kind() == kind && event.evidenceSha256()
               && *event.evidenceSha256() == evidence;
    });
}

bool auditHasOperation(
    const QList<RuntimePackageActivationAuditEvent> &audit,
    RuntimePackageActivationProviderAction action)
{
    return std::any_of(audit.cbegin(), audit.cend(), [action](const auto &event) {
        return event.providerAction() == action;
    });
}

bool auditHasSuccessfulProviderCompletion(
    const QList<RuntimePackageActivationAuditEvent> &audit,
    RuntimePackageActivationProviderAction action)
{
    return std::any_of(audit.cbegin(), audit.cend(), [action](const auto &event) {
        if (event.providerAction() != action)
            return false;
        if (event.kind() == RuntimePackageActivationAuditKind::ProviderOutcomeReconciled) {
            return event.providerReconciliation()
                   == RuntimePackageActivationProviderReconciliation::Applied;
        }
        return event.kind() == RuntimePackageActivationAuditKind::ProviderTerminalResponse
               && event.providerStatus() && *event.providerStatus() == 0
               && event.providerOperationResult() && *event.providerOperationResult() == 0;
    });
}

bool auditHasFailedTerminal(
    const QList<RuntimePackageActivationAuditEvent> &audit,
    RuntimePackageActivationProviderAction action)
{
    return std::any_of(audit.cbegin(), audit.cend(), [action](const auto &event) {
        return event.kind() == RuntimePackageActivationAuditKind::ProviderTerminalResponse
               && event.providerAction() == action && event.providerStatus()
               && event.providerOperationResult()
               && (*event.providerStatus() != 0 || *event.providerOperationResult() != 0);
    });
}

bool deploymentProgressIsValid(const ControllerPackageDeploymentProgress &progress)
{
    constexpr qint64 MaximumPackageBytes = 16 * 1024 * 1024;
    constexpr qsizetype MaximumAuditEvents = 1024;
    if (!isCanonicalText(progress.operationId, 128)
        || !isValidSha256(progress.artifactSha256)
        || progress.totalBytes <= 0 || progress.totalBytes > MaximumPackageBytes
        || progress.transferredBytes < 0
        || progress.transferredBytes > progress.totalBytes
        || (progress.candidate && !progress.candidate->isValid())
        || (progress.previousActive && !progress.previousActive->isValid())
        || (progress.candidate && progress.previousActive
            && progress.candidate == progress.previousActive)
        || progress.status.has_value() != progress.operationResult.has_value()
        || !isCanonicalText(progress.detail, 2048)
        || progress.audit.isEmpty() || progress.audit.size() > MaximumAuditEvents
        || !progress.startedAt.isValid() || !progress.completedAt.isValid()
        || progress.completedAt < progress.startedAt) {
        return false;
    }

    switch (progress.state) {
    case ControllerPackageDeploymentState::Succeeded:
    case ControllerPackageDeploymentState::Canceled:
    case ControllerPackageDeploymentState::Failed:
    case ControllerPackageDeploymentState::OutcomeUnknown:
        break;
    case ControllerPackageDeploymentState::Idle:
    case ControllerPackageDeploymentState::Uploading:
    case ControllerPackageDeploymentState::Committing:
    case ControllerPackageDeploymentState::Validating:
    case ControllerPackageDeploymentState::Activating:
    case ControllerPackageDeploymentState::RollingBack:
    case ControllerPackageDeploymentState::Canceling:
        return false;
    }

    const ControllerPackageDeploymentAuditEvent &first = progress.audit.constFirst();
    if (first.sequence != 1 || first.operation != ControllerOperation::UploadPackage
        || first.requestId || first.status || first.operationResult) {
        return false;
    }

    struct RequestAudit
    {
        ControllerOperation operation = ControllerOperation::None;
        qsizetype count = 0;
        qsizetype responses = 0;
        bool failed = false;
    };
    QHash<quint64, RequestAudit> requests;
    QDateTime previousAt;
    int operationRank = 0;
    bool sawUploadRequest = false;
    bool sawValidate = false;
    bool sawActivate = false;
    bool sawAbort = false;
    bool sawRollback = false;
    quint64 rollbackRequestId = 0;
    for (qsizetype index = 0; index < progress.audit.size(); ++index) {
        const ControllerPackageDeploymentAuditEvent &event = progress.audit.at(index);
        if (event.sequence != quint64(index + 1)
            || event.status.has_value() != event.operationResult.has_value()
            || !isCanonicalText(event.detail, 2048) || !event.occurredAt.isValid()
            || event.occurredAt < progress.startedAt
            || event.occurredAt > progress.completedAt
            || (previousAt.isValid() && event.occurredAt < previousAt)) {
            return false;
        }
        previousAt = event.occurredAt;

        int rank = -1;
        switch (event.operation) {
        case ControllerOperation::UploadPackage:
            rank = 0;
            break;
        case ControllerOperation::AbortPackageUpload:
            rank = 1;
            sawAbort = true;
            break;
        case ControllerOperation::ValidatePackage:
            rank = 2;
            sawValidate = true;
            break;
        case ControllerOperation::ActivatePackage:
            rank = 3;
            sawActivate = true;
            break;
        case ControllerOperation::QueryPackageState:
            rank = 4;
            break;
        case ControllerOperation::RollbackPackage:
            rank = 5;
            sawRollback = true;
            break;
        default:
            return false;
        }
        if (rank < operationRank
            && !(event.operation == ControllerOperation::AbortPackageUpload
                 && operationRank == 0)) {
            return false;
        }
        operationRank = std::max(operationRank, rank);
        if (sawAbort
            && event.operation != ControllerOperation::UploadPackage
            && event.operation != ControllerOperation::AbortPackageUpload) {
            return false;
        }

        if (!event.requestId) {
            if (index != 0 && event.operation != ControllerOperation::UploadPackage
                && event.operation != ControllerOperation::AbortPackageUpload) {
                return false;
            }
            continue;
        }
        if (!*event.requestId)
            return false;
        RequestAudit &request = requests[*event.requestId];
        if (!request.count) {
            if (event.status || event.operationResult)
                return false;
            request.operation = event.operation;
            if (event.operation == ControllerOperation::UploadPackage)
                sawUploadRequest = true;
            if (event.operation == ControllerOperation::RollbackPackage) {
                if (rollbackRequestId && rollbackRequestId != *event.requestId)
                    return false;
                rollbackRequestId = *event.requestId;
            }
        } else {
            if (request.operation != event.operation || !event.status)
                return false;
            ++request.responses;
            request.failed = request.failed || *event.status != 0
                             || *event.operationResult != 0;
        }
        ++request.count;
    }

    if (!sawUploadRequest)
        return false;
    const bool unknown = progress.state == ControllerPackageDeploymentState::OutcomeUnknown;
    for (const RequestAudit &request : std::as_const(requests)) {
        if (!request.responses && !unknown)
            return false;
        if (progress.state == ControllerPackageDeploymentState::Succeeded
            && request.failed) {
            return false;
        }
        if (progress.state == ControllerPackageDeploymentState::Succeeded
            && (request.operation == ControllerOperation::ValidatePackage
                || request.operation == ControllerOperation::ActivatePackage)
            && request.count < 7) {
            return false;
        }
    }

    if (progress.candidate
        && progress.candidate->configurationId == 0) {
        return false;
    }
    if (progress.state == ControllerPackageDeploymentState::Succeeded) {
        return progress.candidate && progress.transferredBytes == progress.totalBytes
               && progress.status == std::optional<qint32>(0)
               && progress.operationResult == std::optional<qint32>(0)
               && sawValidate && sawActivate && !sawAbort && !sawRollback;
    }
    if (sawRollback
        && (!progress.candidate || !progress.previousActive || !sawActivate)) {
        return false;
    }
    if (!progress.candidate && (sawValidate || sawActivate || sawRollback))
        return false;
    if (progress.candidate && progress.transferredBytes != progress.totalBytes)
        return false;
    return true;
}

} // namespace

RuntimePackageActivationOperationId::RuntimePackageActivationOperationId(QString value)
    : m_value(std::move(value))
{}

const QString &RuntimePackageActivationOperationId::value() const
{
    return m_value;
}

bool RuntimePackageActivationOperationId::isValid() const
{
    return isCanonicalText(m_value, 128);
}

RuntimePackageActivationDocumentRevisionToken::
    RuntimePackageActivationDocumentRevisionToken(QByteArray value)
    : m_value(std::move(value))
{}

const QByteArray &RuntimePackageActivationDocumentRevisionToken::value() const
{
    return m_value;
}

bool RuntimePackageActivationDocumentRevisionToken::isValid() const
{
    return isValidOpaqueToken(m_value);
}

RuntimePackageActivationOriginalBindingToken::
    RuntimePackageActivationOriginalBindingToken(QByteArray value)
    : m_value(std::move(value))
{}

const QByteArray &RuntimePackageActivationOriginalBindingToken::value() const
{
    return m_value;
}

bool RuntimePackageActivationOriginalBindingToken::isValid() const
{
    return isValidOpaqueToken(m_value);
}

RuntimePackageActivationOriginalBindingToken
runtimePackageActivationBindingToken(
    const SemanticBindingArtifactReference &reference)
{
    ActivationFingerprintBuilder builder;
    builder.addRawBytes(
        QByteArrayView("embed-labs.runtime-package-activation.project-binding-token.v1"));
    builder.addString(reference.artifactId);
    builder.addBytes(reference.artifactSha256);
    builder.addBytes(reference.projectConfigurationSha256);
    builder.addU64(quint64(reference.projectDeviceBindings.size()));
    for (const SemanticProjectDeviceBinding &binding :
         reference.projectDeviceBindings) {
        builder.addString(binding.slaveId.toString());
        builder.addString(binding.projectDeviceId);
    }
    return RuntimePackageActivationOriginalBindingToken{builder.result().value()};
}

RuntimePackageActivationProjectCapture::RuntimePackageActivationProjectCapture(
    ProjectSnapshot snapshot,
    QByteArray serializedProject,
    quint64 documentRevisionNumber,
    RuntimePackageActivationDocumentRevisionToken documentRevision,
    RuntimePackageActivationOriginalBindingToken originalBinding)
    : m_snapshot(std::move(snapshot))
    , m_serializedProject(std::move(serializedProject))
    , m_documentRevisionNumber(documentRevisionNumber)
    , m_documentRevision(std::move(documentRevision))
    , m_originalBinding(std::move(originalBinding))
{}

const ProjectSnapshot &RuntimePackageActivationProjectCapture::snapshot() const
{
    return m_snapshot;
}

const QByteArray &RuntimePackageActivationProjectCapture::serializedProject() const
{
    return m_serializedProject;
}

quint64 RuntimePackageActivationProjectCapture::documentRevisionNumber() const
{
    return m_documentRevisionNumber;
}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageActivationProjectCapture::documentRevision() const
{
    return m_documentRevision;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationProjectCapture::originalBinding() const
{
    return m_originalBinding;
}

bool RuntimePackageActivationProjectCapture::isValid() const
{
    return m_snapshot.valid && !m_snapshot.id.isNull() && !m_serializedProject.isEmpty()
           && m_documentRevisionNumber != 0 && m_documentRevision.isValid()
           && m_originalBinding.isValid();
}

RuntimePackageActivationSha256::RuntimePackageActivationSha256(QByteArray value)
    : m_value(std::move(value))
{}

const QByteArray &RuntimePackageActivationSha256::value() const
{
    return m_value;
}

bool RuntimePackageActivationSha256::isValid() const
{
    return isValidSha256(m_value);
}

RuntimePackageActivationIdentity::RuntimePackageActivationIdentity(
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
    RuntimeSemanticMappingProof expectedMappingProof)
    : m_operationId(std::move(operationId))
    , m_scope(std::move(scope))
    , m_documentRevisionToken(std::move(documentRevisionToken))
    , m_originalBindingToken(std::move(originalBindingToken))
    , m_targetBindingToken(std::move(targetBindingToken))
    , m_bindingArtifactId(std::move(bindingArtifactId))
    , m_configurationId(configurationId)
    , m_expectedCatalogRevision(expectedCatalogRevision)
    , m_expectedTopologyIdentity(std::move(expectedTopologyIdentity))
    , m_packageSha256(std::move(packageSha256))
    , m_compiledProjectSha256(std::move(compiledProjectSha256))
    , m_effectiveProjectCompanionSha256(std::move(effectiveProjectCompanionSha256))
    , m_expectedMappingProof(std::move(expectedMappingProof))
{}

const RuntimePackageActivationOperationId &RuntimePackageActivationIdentity::operationId() const
{
    return m_operationId;
}

const ControllerConnectionScope &RuntimePackageActivationIdentity::scope() const
{
    return m_scope;
}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageActivationIdentity::documentRevisionToken() const
{
    return m_documentRevisionToken;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationIdentity::originalBindingToken() const
{
    return m_originalBindingToken;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationIdentity::targetBindingToken() const
{
    return m_targetBindingToken;
}

const QString &RuntimePackageActivationIdentity::bindingArtifactId() const
{
    return m_bindingArtifactId;
}

quint64 RuntimePackageActivationIdentity::configurationId() const
{
    return m_configurationId;
}

quint64 RuntimePackageActivationIdentity::expectedCatalogRevision() const
{
    return m_expectedCatalogRevision;
}

const QByteArray &RuntimePackageActivationIdentity::expectedTopologyIdentity() const
{
    return m_expectedTopologyIdentity;
}

const RuntimePackageActivationSha256 &RuntimePackageActivationIdentity::packageSha256() const
{
    return m_packageSha256;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationIdentity::compiledProjectSha256() const
{
    return m_compiledProjectSha256;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationIdentity::effectiveProjectCompanionSha256() const
{
    return m_effectiveProjectCompanionSha256;
}

const RuntimeSemanticMappingProof &
RuntimePackageActivationIdentity::expectedMappingProof() const
{
    return m_expectedMappingProof;
}

bool RuntimePackageActivationIdentity::isValid() const
{
    return m_operationId.isValid() && !m_scope.projectId.isNull() && !m_scope.masterId.isNull()
           && m_documentRevisionToken.isValid() && m_originalBindingToken.isValid()
           && m_targetBindingToken.isValid()
           && isCanonicalText(m_bindingArtifactId, 256) && m_configurationId
           && m_expectedCatalogRevision && !m_expectedTopologyIdentity.isEmpty()
           && m_expectedTopologyIdentity.size() <= 256
           && m_packageSha256.isValid() && m_compiledProjectSha256.isValid()
           && m_effectiveProjectCompanionSha256.isValid()
           && m_expectedMappingProof.isValid()
           && m_expectedMappingProof.packageSha256 == m_packageSha256.value();
}

RuntimePackageActivationSha256 runtimePackageActivationCanonicalRequestFingerprint(
    const RuntimePackageActivationIdentity &identity,
    bool rollbackOnActivationFailure)
{
    if (!identity.isValid())
        return {};

    ActivationFingerprintBuilder builder;
    builder.addBytes(QByteArrayView("embed-labs.runtime-package-activation.request.v1"));
    builder.addString(identity.scope().projectId.toString());
    builder.addString(identity.scope().masterId.toString());
    builder.addBytes(identity.documentRevisionToken().value());
    builder.addBytes(identity.originalBindingToken().value());
    builder.addBytes(identity.targetBindingToken().value());
    builder.addString(identity.bindingArtifactId());
    builder.addU64(identity.configurationId());
    builder.addU64(identity.expectedCatalogRevision());
    builder.addBytes(identity.expectedTopologyIdentity());
    builder.addBytes(identity.packageSha256().value());
    builder.addBytes(identity.compiledProjectSha256().value());
    builder.addBytes(identity.effectiveProjectCompanionSha256().value());
    const RuntimeSemanticMappingProof &proof = identity.expectedMappingProof();
    builder.addU64(proof.formatVersion);
    builder.addU64(proof.bindingCount);
    builder.addByte(proof.packageSigned ? 1 : 0);
    builder.addByte(proof.signatureVerified ? 1 : 0);
    builder.addByte(proof.semanticBindingVerified ? 1 : 0);
    builder.addU64(quint64(proof.trust));
    builder.addBytes(proof.packageSha256);
    builder.addBytes(proof.manifestSha256);
    builder.addBytes(proof.mappingSha256);
    builder.addBytes(proof.resourceRecordsSha256);
    builder.addBytes(proof.resourceSectionSha256);
    builder.addBytes(proof.topologySha256);
    builder.addBytes(proof.signingKeyIdSha256);
    builder.addByte(rollbackOnActivationFailure ? 1 : 0);
    return builder.result();
}

RuntimePackageActivationRequest::RuntimePackageActivationRequest(
    RuntimePackageActivationIdentity identity,
    QByteArray packageBytes,
    QByteArray compiledProjectSource,
    QByteArray effectiveProjectCompanion,
    bool rollbackOnActivationFailure)
    : m_identity(std::move(identity))
    , m_packageBytes(std::move(packageBytes))
    , m_compiledProjectSource(std::move(compiledProjectSource))
    , m_effectiveProjectCompanion(std::move(effectiveProjectCompanion))
    , m_rollbackOnActivationFailure(rollbackOnActivationFailure)
    , m_fingerprint(runtimePackageActivationCanonicalRequestFingerprint(
          m_identity, m_rollbackOnActivationFailure))
{}

const RuntimePackageActivationIdentity &RuntimePackageActivationRequest::identity() const
{
    return m_identity;
}

const QByteArray &RuntimePackageActivationRequest::packageBytes() const
{
    return m_packageBytes;
}

const QByteArray &RuntimePackageActivationRequest::compiledProjectSource() const
{
    return m_compiledProjectSource;
}

const QByteArray &RuntimePackageActivationRequest::effectiveProjectCompanion() const
{
    return m_effectiveProjectCompanion;
}

bool RuntimePackageActivationRequest::rollbackOnActivationFailure() const
{
    return m_rollbackOnActivationFailure;
}

const RuntimePackageActivationSha256 &RuntimePackageActivationRequest::fingerprint() const
{
    return m_fingerprint;
}

bool RuntimePackageActivationRequest::isValid() const
{
    return m_identity.isValid() && m_fingerprint.isValid()
           && m_fingerprint
                  == runtimePackageActivationCanonicalRequestFingerprint(
                      m_identity, m_rollbackOnActivationFailure)
           && sha256Matches(m_packageBytes, m_identity.packageSha256())
           && sha256Matches(m_compiledProjectSource, m_identity.compiledProjectSha256())
           && sha256Matches(
               m_effectiveProjectCompanion,
               m_identity.effectiveProjectCompanionSha256());
}

bool runtimePackageActivationOutcomeIsTerminal(RuntimePackageActivationOutcome outcome)
{
    switch (outcome) {
    case RuntimePackageActivationOutcome::SucceededWithExistingPackage:
    case RuntimePackageActivationOutcome::SucceededWithActivatedPackage:
    case RuntimePackageActivationOutcome::Canceled:
    case RuntimePackageActivationOutcome::FailedWithoutControllerChange:
    case RuntimePackageActivationOutcome::FailedAfterRollback:
    case RuntimePackageActivationOutcome::FailedControllerChangedWithoutBinding:
        return true;
    case RuntimePackageActivationOutcome::Pending:
    case RuntimePackageActivationOutcome::OutcomeUnknown:
        return false;
    }
    return false;
}

bool runtimePackageActivationOutcomeNeedsReconciliation(
    RuntimePackageActivationOutcome outcome)
{
    return outcome == RuntimePackageActivationOutcome::OutcomeUnknown;
}

bool runtimePackageActivationStateNeedsReconciliation(
    RuntimePackageActivationPhase phase,
    RuntimePackageActivationOutcome outcome)
{
    return runtimePackageActivationOutcomeNeedsReconciliation(outcome)
           && (phase == RuntimePackageActivationPhase::AwaitingReconciliation
               || phase == RuntimePackageActivationPhase::Reconciling);
}

bool runtimePackageActivationPhaseAllowsCancel(RuntimePackageActivationPhase phase)
{
    switch (phase) {
    case RuntimePackageActivationPhase::Queued:
    case RuntimePackageActivationPhase::VerifyingInputs:
    case RuntimePackageActivationPhase::CapturingProject:
    case RuntimePackageActivationPhase::ProbingExistingPackage:
        return true;
    case RuntimePackageActivationPhase::Idle:
    case RuntimePackageActivationPhase::AcquiringControl:
    case RuntimePackageActivationPhase::DeployingPackage:
    case RuntimePackageActivationPhase::VerifyingRuntimeIdentity:
    case RuntimePackageActivationPhase::PersistingEvidence:
    case RuntimePackageActivationPhase::CommittingProjectBinding:
    case RuntimePackageActivationPhase::ReleasingControl:
    case RuntimePackageActivationPhase::Canceling:
    case RuntimePackageActivationPhase::Reconciling:
    case RuntimePackageActivationPhase::AwaitingReconciliation:
    case RuntimePackageActivationPhase::Finished:
        return false;
    }
    return false;
}

bool runtimePackageActivationTransitionIsAllowed(
    RuntimePackageActivationPhase fromPhase,
    RuntimePackageActivationOutcome fromOutcome,
    RuntimePackageActivationPhase toPhase,
    RuntimePackageActivationOutcome toOutcome)
{
    if (!phaseOutcomePairIsValid(fromPhase, fromOutcome)
        || !phaseOutcomePairIsValid(toPhase, toOutcome)
        || runtimePackageActivationOutcomeIsTerminal(fromOutcome)) {
        return false;
    }
    if (fromPhase == toPhase && fromOutcome == toOutcome)
        return true;

    if (fromOutcome == RuntimePackageActivationOutcome::OutcomeUnknown) {
        if (toOutcome == RuntimePackageActivationOutcome::OutcomeUnknown) {
            return (fromPhase == RuntimePackageActivationPhase::AwaitingReconciliation
                    && toPhase == RuntimePackageActivationPhase::Reconciling)
                   || (fromPhase == RuntimePackageActivationPhase::Reconciling
                       && toPhase == RuntimePackageActivationPhase::AwaitingReconciliation);
        }
        return fromPhase == RuntimePackageActivationPhase::Reconciling
               && toPhase == RuntimePackageActivationPhase::Finished
               && runtimePackageActivationOutcomeIsTerminal(toOutcome);
    }

    if (toOutcome == RuntimePackageActivationOutcome::OutcomeUnknown) {
        return toPhase == RuntimePackageActivationPhase::AwaitingReconciliation;
    }
    if (runtimePackageActivationOutcomeIsTerminal(toOutcome)) {
        if (toPhase != RuntimePackageActivationPhase::Finished)
            return false;
        switch (toOutcome) {
        case RuntimePackageActivationOutcome::SucceededWithExistingPackage:
            return fromPhase == RuntimePackageActivationPhase::CommittingProjectBinding;
        case RuntimePackageActivationOutcome::SucceededWithActivatedPackage:
            return fromPhase == RuntimePackageActivationPhase::ReleasingControl;
        case RuntimePackageActivationOutcome::Canceled:
            return fromPhase == RuntimePackageActivationPhase::Canceling
                   || fromPhase == RuntimePackageActivationPhase::ReleasingControl;
        case RuntimePackageActivationOutcome::FailedAfterRollback:
            return fromPhase == RuntimePackageActivationPhase::ReleasingControl;
        case RuntimePackageActivationOutcome::FailedControllerChangedWithoutBinding:
            return fromPhase == RuntimePackageActivationPhase::VerifyingRuntimeIdentity
                   || fromPhase == RuntimePackageActivationPhase::PersistingEvidence
                   || fromPhase == RuntimePackageActivationPhase::CommittingProjectBinding
                   || fromPhase == RuntimePackageActivationPhase::ReleasingControl;
        case RuntimePackageActivationOutcome::FailedWithoutControllerChange:
            return fromPhase != RuntimePackageActivationPhase::Queued;
        case RuntimePackageActivationOutcome::Pending:
        case RuntimePackageActivationOutcome::OutcomeUnknown:
            return false;
        }
        return false;
    }
    if (toOutcome != RuntimePackageActivationOutcome::Pending)
        return false;

    switch (fromPhase) {
    case RuntimePackageActivationPhase::Queued:
        return toPhase == RuntimePackageActivationPhase::VerifyingInputs
               || toPhase == RuntimePackageActivationPhase::Canceling;
    case RuntimePackageActivationPhase::VerifyingInputs:
        return toPhase == RuntimePackageActivationPhase::CapturingProject
               || toPhase == RuntimePackageActivationPhase::Canceling;
    case RuntimePackageActivationPhase::CapturingProject:
        return toPhase == RuntimePackageActivationPhase::ProbingExistingPackage
               || toPhase == RuntimePackageActivationPhase::Canceling;
    case RuntimePackageActivationPhase::ProbingExistingPackage:
        return toPhase == RuntimePackageActivationPhase::AcquiringControl
               || toPhase == RuntimePackageActivationPhase::VerifyingRuntimeIdentity
               || toPhase == RuntimePackageActivationPhase::Canceling;
    case RuntimePackageActivationPhase::AcquiringControl:
        return toPhase == RuntimePackageActivationPhase::DeployingPackage
               || toPhase == RuntimePackageActivationPhase::VerifyingRuntimeIdentity;
    case RuntimePackageActivationPhase::DeployingPackage:
        return toPhase == RuntimePackageActivationPhase::VerifyingRuntimeIdentity;
    case RuntimePackageActivationPhase::VerifyingRuntimeIdentity:
        return toPhase == RuntimePackageActivationPhase::PersistingEvidence;
    case RuntimePackageActivationPhase::PersistingEvidence:
        return toPhase == RuntimePackageActivationPhase::CommittingProjectBinding;
    case RuntimePackageActivationPhase::CommittingProjectBinding:
        return toPhase == RuntimePackageActivationPhase::ReleasingControl;
    case RuntimePackageActivationPhase::Canceling:
        return toPhase == RuntimePackageActivationPhase::ReleasingControl;
    case RuntimePackageActivationPhase::ReleasingControl:
    case RuntimePackageActivationPhase::Reconciling:
    case RuntimePackageActivationPhase::AwaitingReconciliation:
    case RuntimePackageActivationPhase::Finished:
    case RuntimePackageActivationPhase::Idle:
        return false;
    }
    return false;
}

bool runtimePackageActivationProviderOperationIsAllowed(ControllerOperation operation)
{
    switch (operation) {
    case ControllerOperation::AcquireControl:
    case ControllerOperation::ReleaseControl:
        return true;
    default:
        return false;
    }
}

RuntimePackageActivationControllerEvidence::RuntimePackageActivationControllerEvidence(
    ControllerConnectionScope scope,
    quint64 sessionGeneration,
    quint64 observationSessionId,
    quint64 controlLeaseOwnerSessionId,
    quint64 bootId,
    std::optional<ControllerPackageSelector> persistentActive,
    ControllerPackageState runtimePackageState,
    ControllerServiceState serviceState,
    std::optional<RuntimeSemanticMappingAttestation> mappingAttestation,
    QDateTime observedAt)
    : m_scope(std::move(scope))
    , m_sessionGeneration(sessionGeneration)
    , m_observationSessionId(observationSessionId)
    , m_controlLeaseOwnerSessionId(controlLeaseOwnerSessionId)
    , m_bootId(bootId)
    , m_persistentActive(std::move(persistentActive))
    , m_runtimePackageState(runtimePackageState)
    , m_serviceState(serviceState)
    , m_mappingAttestation(std::move(mappingAttestation))
    , m_evidenceSha256(canonicalControllerEvidenceSha256(
          m_scope,
          m_sessionGeneration,
          m_observationSessionId,
          m_controlLeaseOwnerSessionId,
          m_bootId,
          m_persistentActive,
          m_runtimePackageState,
          m_serviceState,
          m_mappingAttestation,
          observedAt))
    , m_observedAt(std::move(observedAt))
{}

const ControllerConnectionScope &RuntimePackageActivationControllerEvidence::scope() const
{
    return m_scope;
}

quint64 RuntimePackageActivationControllerEvidence::sessionGeneration() const
{
    return m_sessionGeneration;
}

quint64 RuntimePackageActivationControllerEvidence::observationSessionId() const
{
    return m_observationSessionId;
}

quint64 RuntimePackageActivationControllerEvidence::controlLeaseOwnerSessionId() const
{
    return m_controlLeaseOwnerSessionId;
}

bool RuntimePackageActivationControllerEvidence::ownsControlLease() const
{
    return m_controlLeaseOwnerSessionId
           && m_controlLeaseOwnerSessionId == m_observationSessionId;
}

quint64 RuntimePackageActivationControllerEvidence::bootId() const
{
    return m_bootId;
}

const std::optional<ControllerPackageSelector> &
RuntimePackageActivationControllerEvidence::persistentActive() const
{
    return m_persistentActive;
}

ControllerPackageState RuntimePackageActivationControllerEvidence::runtimePackageState() const
{
    return m_runtimePackageState;
}

ControllerServiceState RuntimePackageActivationControllerEvidence::serviceState() const
{
    return m_serviceState;
}

const std::optional<RuntimeSemanticMappingAttestation> &
RuntimePackageActivationControllerEvidence::mappingAttestation() const
{
    return m_mappingAttestation;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationControllerEvidence::evidenceSha256() const
{
    return m_evidenceSha256;
}

const QDateTime &RuntimePackageActivationControllerEvidence::observedAt() const
{
    return m_observedAt;
}

bool RuntimePackageActivationControllerEvidence::isValid() const
{
    if (m_scope.projectId.isNull() || m_scope.masterId.isNull() || !m_sessionGeneration
        || !m_observationSessionId || !m_bootId
        || !packageStateIsKnown(m_runtimePackageState) || !serviceStateIsKnown(m_serviceState)
        || !m_evidenceSha256.isValid() || !m_observedAt.isValid()
        || m_evidenceSha256
               != canonicalControllerEvidenceSha256(
                   m_scope,
                   m_sessionGeneration,
                   m_observationSessionId,
                   m_controlLeaseOwnerSessionId,
                   m_bootId,
                   m_persistentActive,
                   m_runtimePackageState,
                   m_serviceState,
                   m_mappingAttestation,
                   m_observedAt)
        || (m_persistentActive && !m_persistentActive->isValid())) {
        return false;
    }

    const bool runtimeActive = m_runtimePackageState == ControllerPackageState::Active;
    if (runtimeActive != m_mappingAttestation.has_value())
        return false;
    if (!m_mappingAttestation)
        return true;
    if (!m_persistentActive || !m_mappingAttestation->isValid()
        || m_mappingAttestation->scope != m_scope
        || m_mappingAttestation->sessionGeneration != m_sessionGeneration
        || m_mappingAttestation->epoch.controllerBootId != m_bootId
        || m_mappingAttestation->epoch.activePackageSlot != m_persistentActive->slot
        || m_mappingAttestation->epoch.activePackageGeneration != m_persistentActive->generation
        || m_mappingAttestation->epoch.configurationId != m_persistentActive->configurationId
        || m_mappingAttestation->receivedAt > m_observedAt) {
        return false;
    }
    return true;
}

bool RuntimePackageActivationControllerEvidence::describesSameControllerStateAs(
    const RuntimePackageActivationControllerEvidence &other) const
{
    return isValid() && other.isValid() && m_scope == other.m_scope
           && m_sessionGeneration == other.m_sessionGeneration
           && m_observationSessionId == other.m_observationSessionId
           && m_bootId == other.m_bootId
           && m_controlLeaseOwnerSessionId == other.m_controlLeaseOwnerSessionId
           && m_persistentActive == other.m_persistentActive
           && m_runtimePackageState == other.m_runtimePackageState
           && m_serviceState == other.m_serviceState
           && optionalAttestationsDescribeSameState(
               m_mappingAttestation, other.m_mappingAttestation);
}

bool RuntimePackageActivationControllerEvidence::matchesActivatedIdentity(
    const RuntimePackageActivationIdentity &identity) const
{
    return isValid() && identity.isValid() && m_scope == identity.scope()
           && m_runtimePackageState == ControllerPackageState::Active
           && (m_serviceState == ControllerServiceState::OperationalSafe
               || m_serviceState == ControllerServiceState::Running
               || m_serviceState == ControllerServiceState::Paused)
           && m_persistentActive
           && m_persistentActive->configurationId == identity.configurationId()
           && m_mappingAttestation
           && m_mappingAttestation->epoch.catalogRevision
                  == identity.expectedCatalogRevision()
           && m_mappingAttestation->epoch.topologyIdentity
                  == identity.expectedTopologyIdentity()
           && m_mappingAttestation->proof == identity.expectedMappingProof();
}

RuntimePackageActivationDeploymentEvidence::RuntimePackageActivationDeploymentEvidence(
    quint64 providerSessionGeneration,
    quint64 providerSessionId,
    quint64 providerBootId,
    ControllerPackageDeploymentProgress progress,
    QDateTime observedAt)
    : m_providerSessionGeneration(providerSessionGeneration)
    , m_providerSessionId(providerSessionId)
    , m_providerBootId(providerBootId)
    , m_progress(std::move(progress))
    , m_artifactSha256(m_progress.artifactSha256)
    , m_internalAuditSha256(canonicalDeploymentAuditSha256(m_progress))
    , m_evidenceSha256(canonicalDeploymentEvidenceSha256(
          m_providerSessionGeneration,
          m_providerSessionId,
          m_providerBootId,
          m_progress,
          observedAt))
    , m_outcome(
          m_progress.state == ControllerPackageDeploymentState::Succeeded
              ? RuntimePackageActivationDeploymentOutcome::Succeeded
          : m_progress.state == ControllerPackageDeploymentState::OutcomeUnknown
              ? RuntimePackageActivationDeploymentOutcome::OutcomeUnknown
              : RuntimePackageActivationDeploymentOutcome::Failed)
    , m_observedAt(std::move(observedAt))
{}

const QString &RuntimePackageActivationDeploymentEvidence::providerOperationId() const
{
    return m_progress.operationId;
}

quint64 RuntimePackageActivationDeploymentEvidence::providerSessionGeneration() const
{
    return m_providerSessionGeneration;
}

quint64 RuntimePackageActivationDeploymentEvidence::providerSessionId() const
{
    return m_providerSessionId;
}

quint64 RuntimePackageActivationDeploymentEvidence::providerBootId() const
{
    return m_providerBootId;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationDeploymentEvidence::artifactSha256() const
{
    return m_artifactSha256;
}

const std::optional<ControllerPackageSelector> &
RuntimePackageActivationDeploymentEvidence::previousActive() const
{
    return m_progress.previousActive;
}

const std::optional<ControllerPackageSelector> &
RuntimePackageActivationDeploymentEvidence::candidate() const
{
    return m_progress.candidate;
}

const ControllerPackageDeploymentProgress &
RuntimePackageActivationDeploymentEvidence::progress() const
{
    return m_progress;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationDeploymentEvidence::internalAuditSha256() const
{
    return m_internalAuditSha256;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationDeploymentEvidence::evidenceSha256() const
{
    return m_evidenceSha256;
}

RuntimePackageActivationDeploymentOutcome
RuntimePackageActivationDeploymentEvidence::outcome() const
{
    return m_outcome;
}

const QDateTime &RuntimePackageActivationDeploymentEvidence::observedAt() const
{
    return m_observedAt;
}

bool RuntimePackageActivationDeploymentEvidence::isValid() const
{
    const RuntimePackageActivationDeploymentOutcome expectedOutcome
        = m_progress.state == ControllerPackageDeploymentState::Succeeded
              ? RuntimePackageActivationDeploymentOutcome::Succeeded
          : m_progress.state == ControllerPackageDeploymentState::OutcomeUnknown
              ? RuntimePackageActivationDeploymentOutcome::OutcomeUnknown
              : RuntimePackageActivationDeploymentOutcome::Failed;
    return m_providerSessionGeneration && m_providerSessionId && m_providerBootId
           && deploymentProgressIsValid(m_progress)
           && m_artifactSha256
                  == RuntimePackageActivationSha256{m_progress.artifactSha256}
           && m_internalAuditSha256 == canonicalDeploymentAuditSha256(m_progress)
           && m_evidenceSha256
                  == canonicalDeploymentEvidenceSha256(
                      m_providerSessionGeneration,
                      m_providerSessionId,
                      m_providerBootId,
                      m_progress,
                      m_observedAt)
           && m_internalAuditSha256.isValid() && m_evidenceSha256.isValid()
           && deploymentOutcomeIsKnown(m_outcome) && m_outcome == expectedOutcome
           && m_observedAt.isValid() && m_observedAt >= m_progress.completedAt;
}

RuntimePackageActivationProjectCommit::RuntimePackageActivationProjectCommit(
    RuntimePackageActivationDocumentRevisionToken originalDocumentRevision,
    RuntimePackageActivationOriginalBindingToken originalBinding,
    RuntimePackageActivationDocumentRevisionToken resultingDocumentRevision,
    RuntimePackageActivationOriginalBindingToken resultingBinding,
    RuntimePackageActivationProjectCommitDisposition disposition,
    QDateTime committedAt)
    : m_originalDocumentRevision(std::move(originalDocumentRevision))
    , m_originalBinding(std::move(originalBinding))
    , m_resultingDocumentRevision(std::move(resultingDocumentRevision))
    , m_resultingBinding(std::move(resultingBinding))
    , m_disposition(disposition)
    , m_evidenceSha256(canonicalProjectCommitSha256(
          m_originalDocumentRevision,
          m_originalBinding,
          m_resultingDocumentRevision,
          m_resultingBinding,
          m_disposition,
          committedAt))
    , m_committedAt(std::move(committedAt))
{}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageActivationProjectCommit::originalDocumentRevision() const
{
    return m_originalDocumentRevision;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationProjectCommit::originalBinding() const
{
    return m_originalBinding;
}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageActivationProjectCommit::resultingDocumentRevision() const
{
    return m_resultingDocumentRevision;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationProjectCommit::resultingBinding() const
{
    return m_resultingBinding;
}

RuntimePackageActivationProjectCommitDisposition
RuntimePackageActivationProjectCommit::disposition() const
{
    return m_disposition;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationProjectCommit::evidenceSha256() const
{
    return m_evidenceSha256;
}

const QDateTime &RuntimePackageActivationProjectCommit::committedAt() const
{
    return m_committedAt;
}

bool RuntimePackageActivationProjectCommit::isValid() const
{
    if (!m_originalDocumentRevision.isValid() || !m_originalBinding.isValid()
        || !m_resultingDocumentRevision.isValid() || !m_resultingBinding.isValid()
        || !projectCommitDispositionIsKnown(m_disposition) || !m_evidenceSha256.isValid()
        || m_evidenceSha256
               != canonicalProjectCommitSha256(
                   m_originalDocumentRevision,
                   m_originalBinding,
                   m_resultingDocumentRevision,
                   m_resultingBinding,
                   m_disposition,
                   m_committedAt)
        || !m_committedAt.isValid()) {
        return false;
    }
    switch (m_disposition) {
    case RuntimePackageActivationProjectCommitDisposition::AlreadyExact:
        return m_resultingDocumentRevision == m_originalDocumentRevision
               && m_resultingBinding == m_originalBinding;
    case RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted:
        return m_resultingDocumentRevision != m_originalDocumentRevision
               && m_resultingBinding != m_originalBinding;
    }
    return false;
}

RuntimePackageActivationProjectCompareAndSetResult::
    RuntimePackageActivationProjectCompareAndSetResult(
        RuntimePackageActivationProjectCompareAndSetDisposition disposition,
        std::optional<RuntimePackageActivationProjectCommit> commit)
    : m_disposition(disposition)
    , m_commit(std::move(commit))
{}

RuntimePackageActivationProjectCompareAndSetDisposition
RuntimePackageActivationProjectCompareAndSetResult::disposition() const
{
    return m_disposition;
}

const std::optional<RuntimePackageActivationProjectCommit> &
RuntimePackageActivationProjectCompareAndSetResult::commit() const
{
    return m_commit;
}

bool RuntimePackageActivationProjectCompareAndSetResult::isValid() const
{
    if (!projectCompareAndSetDispositionIsKnown(m_disposition))
        return false;
    if (m_disposition == RuntimePackageActivationProjectCompareAndSetDisposition::Stale)
        return !m_commit;
    if (!m_commit || !m_commit->isValid())
        return false;
    if (m_disposition
        == RuntimePackageActivationProjectCompareAndSetDisposition::CompareAndSetCommitted) {
        return m_commit->disposition()
               == RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted;
    }
    return m_commit->disposition()
           == RuntimePackageActivationProjectCommitDisposition::AlreadyExact;
}

RuntimePackageActivationCancelRequest::RuntimePackageActivationCancelRequest(
    RuntimePackageActivationOperationId operationId,
    quint64 expectedRecordRevision,
    RuntimePackageActivationDocumentRevisionToken expectedDocumentRevision,
    RuntimePackageActivationOriginalBindingToken expectedBinding)
    : m_operationId(std::move(operationId))
    , m_expectedRecordRevision(expectedRecordRevision)
    , m_expectedDocumentRevision(std::move(expectedDocumentRevision))
    , m_expectedBinding(std::move(expectedBinding))
    , m_fingerprint(runtimePackageActivationCanonicalCancelFingerprint(
          m_operationId,
          m_expectedRecordRevision,
          m_expectedDocumentRevision,
          m_expectedBinding))
{}

RuntimePackageActivationSha256 runtimePackageActivationCanonicalCancelFingerprint(
    const RuntimePackageActivationOperationId &operationId,
    quint64 expectedRecordRevision,
    const RuntimePackageActivationDocumentRevisionToken &expectedDocumentRevision,
    const RuntimePackageActivationOriginalBindingToken &expectedBinding)
{
    if (!operationId.isValid() || !expectedRecordRevision
        || !expectedDocumentRevision.isValid() || !expectedBinding.isValid()) {
        return {};
    }
    ActivationFingerprintBuilder builder;
    builder.addBytes(QByteArrayView("embed-labs.runtime-package-activation.cancel.v1"));
    builder.addString(operationId.value());
    builder.addU64(expectedRecordRevision);
    builder.addBytes(expectedDocumentRevision.value());
    builder.addBytes(expectedBinding.value());
    return builder.result();
}

const RuntimePackageActivationOperationId &
RuntimePackageActivationCancelRequest::operationId() const
{
    return m_operationId;
}

quint64 RuntimePackageActivationCancelRequest::expectedRecordRevision() const
{
    return m_expectedRecordRevision;
}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageActivationCancelRequest::expectedDocumentRevision() const
{
    return m_expectedDocumentRevision;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationCancelRequest::expectedBinding() const
{
    return m_expectedBinding;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationCancelRequest::fingerprint() const
{
    return m_fingerprint;
}

bool RuntimePackageActivationCancelRequest::isValid() const
{
    return m_operationId.isValid() && m_expectedRecordRevision
           && m_expectedDocumentRevision.isValid() && m_expectedBinding.isValid()
           && m_fingerprint.isValid()
           && m_fingerprint
                  == runtimePackageActivationCanonicalCancelFingerprint(
                      m_operationId,
                      m_expectedRecordRevision,
                      m_expectedDocumentRevision,
                      m_expectedBinding);
}

RuntimePackageActivationCancellation::RuntimePackageActivationCancellation(
    quint64 expectedRecordRevision,
    RuntimePackageActivationDocumentRevisionToken expectedDocumentRevision,
    RuntimePackageActivationOriginalBindingToken expectedBinding,
    RuntimePackageActivationSha256 requestFingerprint,
    RuntimePackageActivationPhase requestedPhase,
    RuntimePackageActivationCancellationControllerResult controllerResult,
    std::optional<RuntimePackageActivationSha256> evidenceSha256,
    QDateTime requestedAt)
    : m_expectedRecordRevision(expectedRecordRevision)
    , m_expectedDocumentRevision(std::move(expectedDocumentRevision))
    , m_expectedBinding(std::move(expectedBinding))
    , m_requestFingerprint(std::move(requestFingerprint))
    , m_requestedPhase(requestedPhase)
    , m_controllerResult(controllerResult)
    , m_evidenceSha256(std::move(evidenceSha256))
    , m_requestedAt(std::move(requestedAt))
{}

quint64 RuntimePackageActivationCancellation::expectedRecordRevision() const
{
    return m_expectedRecordRevision;
}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageActivationCancellation::expectedDocumentRevision() const
{
    return m_expectedDocumentRevision;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageActivationCancellation::expectedBinding() const
{
    return m_expectedBinding;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationCancellation::requestFingerprint() const
{
    return m_requestFingerprint;
}

RuntimePackageActivationPhase RuntimePackageActivationCancellation::requestedPhase() const
{
    return m_requestedPhase;
}

RuntimePackageActivationCancellationControllerResult
RuntimePackageActivationCancellation::controllerResult() const
{
    return m_controllerResult;
}

const std::optional<RuntimePackageActivationSha256> &
RuntimePackageActivationCancellation::evidenceSha256() const
{
    return m_evidenceSha256;
}

const QDateTime &RuntimePackageActivationCancellation::requestedAt() const
{
    return m_requestedAt;
}

bool RuntimePackageActivationCancellation::isValid() const
{
    if (!m_expectedRecordRevision || !m_expectedDocumentRevision.isValid()
        || !m_expectedBinding.isValid() || !m_requestFingerprint.isValid()
        || !runtimePackageActivationPhaseAllowsCancel(m_requestedPhase)
        || !cancellationResultIsKnown(m_controllerResult) || !m_requestedAt.isValid()) {
        return false;
    }
    const bool requiresEvidence
        = m_controllerResult
              != RuntimePackageActivationCancellationControllerResult::Pending
          && m_controllerResult
                 != RuntimePackageActivationCancellationControllerResult::NoControllerMutation;
    return requiresEvidence == m_evidenceSha256.has_value()
           && (!m_evidenceSha256 || m_evidenceSha256->isValid());
}

RuntimePackageActivationAuditEvent::RuntimePackageActivationAuditEvent(
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
    QDateTime occurredAt)
    : m_sequence(sequence)
    , m_kind(kind)
    , m_phase(phase)
    , m_outcome(outcome)
    , m_providerAction(providerAction)
    , m_providerReconciliation(providerReconciliation)
    , m_providerOperationId(std::move(providerOperationId))
    , m_providerSessionGeneration(providerSessionGeneration)
    , m_providerSessionId(providerSessionId)
    , m_providerBootId(providerBootId)
    , m_providerRequestId(providerRequestId)
    , m_providerStatus(providerStatus)
    , m_providerOperationResult(providerOperationResult)
    , m_evidenceSha256(std::move(evidenceSha256))
    , m_code(std::move(code))
    , m_detail(std::move(detail))
    , m_occurredAt(std::move(occurredAt))
{}

quint64 RuntimePackageActivationAuditEvent::sequence() const
{
    return m_sequence;
}

RuntimePackageActivationAuditKind RuntimePackageActivationAuditEvent::kind() const
{
    return m_kind;
}

RuntimePackageActivationPhase RuntimePackageActivationAuditEvent::phase() const
{
    return m_phase;
}

RuntimePackageActivationOutcome RuntimePackageActivationAuditEvent::outcome() const
{
    return m_outcome;
}

RuntimePackageActivationProviderAction
RuntimePackageActivationAuditEvent::providerAction() const
{
    return m_providerAction;
}

RuntimePackageActivationProviderReconciliation
RuntimePackageActivationAuditEvent::providerReconciliation() const
{
    return m_providerReconciliation;
}

const QString &RuntimePackageActivationAuditEvent::providerOperationId() const
{
    return m_providerOperationId;
}

quint64 RuntimePackageActivationAuditEvent::providerSessionGeneration() const
{
    return m_providerSessionGeneration;
}

quint64 RuntimePackageActivationAuditEvent::providerSessionId() const
{
    return m_providerSessionId;
}

quint64 RuntimePackageActivationAuditEvent::providerBootId() const
{
    return m_providerBootId;
}

const std::optional<quint64> &RuntimePackageActivationAuditEvent::providerRequestId() const
{
    return m_providerRequestId;
}

const std::optional<qint32> &RuntimePackageActivationAuditEvent::providerStatus() const
{
    return m_providerStatus;
}

const std::optional<qint32> &RuntimePackageActivationAuditEvent::providerOperationResult() const
{
    return m_providerOperationResult;
}

const std::optional<RuntimePackageActivationSha256> &
RuntimePackageActivationAuditEvent::evidenceSha256() const
{
    return m_evidenceSha256;
}

const QString &RuntimePackageActivationAuditEvent::code() const
{
    return m_code;
}

const QString &RuntimePackageActivationAuditEvent::detail() const
{
    return m_detail;
}

const QDateTime &RuntimePackageActivationAuditEvent::occurredAt() const
{
    return m_occurredAt;
}

bool RuntimePackageActivationAuditEvent::isValid() const
{
    if (!m_sequence || !auditKindIsKnown(m_kind)
        || !providerActionIsKnown(m_providerAction)
        || !providerReconciliationIsKnown(m_providerReconciliation)
        || !phaseOutcomePairIsValid(m_phase, m_outcome)
        || isCanonicalText(m_code, 128) == false
        || isCanonicalText(m_detail, 2048) == false || !m_occurredAt.isValid()
        || (m_evidenceSha256 && !m_evidenceSha256->isValid())) {
        return false;
    }

    switch (m_kind) {
    case RuntimePackageActivationAuditKind::ProviderRequestSent:
        return m_providerAction != RuntimePackageActivationProviderAction::None
               && isCanonicalText(m_providerOperationId, 128)
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && providerOperationIsAllowedInPhase(m_providerAction, m_phase)
               && m_providerSessionGeneration && m_providerSessionId && m_providerBootId
               && ((m_providerAction
                        == RuntimePackageActivationProviderAction::DeployPackage
                    && !m_providerRequestId)
                   || (m_providerAction
                           != RuntimePackageActivationProviderAction::DeployPackage
                       && m_providerRequestId && *m_providerRequestId))
               && !m_providerStatus
               && !m_providerOperationResult && !m_evidenceSha256;
    case RuntimePackageActivationAuditKind::ProviderTerminalResponse:
        return m_providerAction != RuntimePackageActivationProviderAction::None
               && isCanonicalText(m_providerOperationId, 128)
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && (providerOperationIsAllowedInPhase(m_providerAction, m_phase)
                   || (m_providerAction
                           == RuntimePackageActivationProviderAction::DeployPackage
                       && m_phase == RuntimePackageActivationPhase::Canceling)
                   || m_phase == RuntimePackageActivationPhase::Reconciling)
               && m_providerSessionGeneration && m_providerSessionId && m_providerBootId
               && ((m_providerAction
                        == RuntimePackageActivationProviderAction::DeployPackage
                    && !m_providerRequestId)
                   || (m_providerAction
                           != RuntimePackageActivationProviderAction::DeployPackage
                       && m_providerRequestId && *m_providerRequestId))
               && m_providerStatus
               && m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::ProviderOutcomeReconciled:
        return m_phase == RuntimePackageActivationPhase::Reconciling
               && m_outcome == RuntimePackageActivationOutcome::OutcomeUnknown
               && isCanonicalText(m_providerOperationId, 128)
               && (m_providerAction
                       == RuntimePackageActivationProviderAction::DeployPackage
                   || m_providerAction
                          == RuntimePackageActivationProviderAction::AcquireControl
                   || m_providerAction
                          == RuntimePackageActivationProviderAction::ReleaseControl)
               && m_providerReconciliation
                      != RuntimePackageActivationProviderReconciliation::None
               && m_providerSessionGeneration && m_providerSessionId && m_providerBootId
               && ((m_providerAction
                        == RuntimePackageActivationProviderAction::DeployPackage
                    && !m_providerRequestId)
                   || (m_providerAction
                           != RuntimePackageActivationProviderAction::DeployPackage
                       && m_providerRequestId && *m_providerRequestId))
               && !m_providerStatus
               && !m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::ControlLeaseReconciled:
        return m_phase == RuntimePackageActivationPhase::Reconciling
               && m_outcome == RuntimePackageActivationOutcome::OutcomeUnknown
               && m_providerAction
                      == RuntimePackageActivationProviderAction::ReleaseControl
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::Applied
               && isCanonicalText(m_providerOperationId, 128)
               && m_providerSessionGeneration && m_providerSessionId && m_providerBootId
               && !m_providerRequestId && !m_providerStatus && !m_providerOperationResult
               && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::DeploymentEvidenceCaptured:
        return (m_phase == RuntimePackageActivationPhase::DeployingPackage
                || m_phase == RuntimePackageActivationPhase::Reconciling)
               && m_providerAction
                      == RuntimePackageActivationProviderAction::DeployPackage
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && isCanonicalText(m_providerOperationId, 128)
               && m_providerSessionGeneration && m_providerSessionId && m_providerBootId
               && !m_providerRequestId && !m_providerStatus && !m_providerOperationResult
               && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::ControllerEvidenceCaptured:
        return (m_phase == RuntimePackageActivationPhase::ProbingExistingPackage
                || m_phase == RuntimePackageActivationPhase::AcquiringControl
                || m_phase == RuntimePackageActivationPhase::DeployingPackage
                || m_phase == RuntimePackageActivationPhase::VerifyingRuntimeIdentity
                || m_phase == RuntimePackageActivationPhase::PersistingEvidence
                || m_phase == RuntimePackageActivationPhase::CommittingProjectBinding
                || m_phase == RuntimePackageActivationPhase::Canceling
                || m_phase == RuntimePackageActivationPhase::ReleasingControl
                || m_phase == RuntimePackageActivationPhase::Reconciling)
               && m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::ProjectCompareAndSet:
        return (m_phase == RuntimePackageActivationPhase::CommittingProjectBinding
               || m_phase == RuntimePackageActivationPhase::Reconciling)
               && m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::Completed:
        return m_phase == RuntimePackageActivationPhase::Finished
               && runtimePackageActivationOutcomeIsTerminal(m_outcome)
               && m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::IntentPersisted:
        return m_phase == RuntimePackageActivationPhase::Queued
               && m_outcome == RuntimePackageActivationOutcome::Pending
               && m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::PhaseTransition:
        return m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && !m_evidenceSha256;
    case RuntimePackageActivationAuditKind::CancellationRequested:
        return m_phase == RuntimePackageActivationPhase::Canceling
               && m_outcome == RuntimePackageActivationOutcome::Pending
               && m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && m_evidenceSha256;
    case RuntimePackageActivationAuditKind::ReconciliationStarted:
        return m_phase == RuntimePackageActivationPhase::Reconciling
               && m_outcome == RuntimePackageActivationOutcome::OutcomeUnknown
               && m_providerAction == RuntimePackageActivationProviderAction::None
               && m_providerOperationId.isEmpty()
               && m_providerReconciliation
                      == RuntimePackageActivationProviderReconciliation::None
               && !m_providerSessionGeneration && !m_providerSessionId && !m_providerBootId
               && !m_providerRequestId
               && !m_providerStatus && !m_providerOperationResult && !m_evidenceSha256;
    }
    return false;
}

RuntimePackageActivationRecord::RuntimePackageActivationRecord(
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
    QDateTime completedAt,
    std::optional<RuntimePackageActivationControllerEvidence> beforeController,
    std::optional<RuntimePackageActivationControllerEvidence> afterController,
    std::optional<RuntimePackageActivationProjectCommit> projectCommit,
    std::optional<RuntimePackageActivationCancellation> cancellation,
    std::optional<RuntimePackageActivationDeploymentEvidence> deploymentEvidence,
    QList<RuntimePackageActivationControllerEvidence> controllerEvidenceHistory)
    : m_identity(std::move(identity))
    , m_requestFingerprint(std::move(requestFingerprint))
    , m_rollbackOnActivationFailure(rollbackOnActivationFailure)
    , m_revision(revision)
    , m_phase(phase)
    , m_outcome(outcome)
    , m_audit(std::move(audit))
    , m_detail(std::move(detail))
    , m_startedAt(std::move(startedAt))
    , m_updatedAt(std::move(updatedAt))
    , m_completedAt(std::move(completedAt))
    , m_beforeController(std::move(beforeController))
    , m_afterController(std::move(afterController))
    , m_projectCommit(std::move(projectCommit))
    , m_cancellation(std::move(cancellation))
    , m_deploymentEvidence(std::move(deploymentEvidence))
    , m_controllerEvidenceHistory(std::move(controllerEvidenceHistory))
{}

const RuntimePackageActivationIdentity &RuntimePackageActivationRecord::identity() const
{
    return m_identity;
}

const RuntimePackageActivationSha256 &
RuntimePackageActivationRecord::requestFingerprint() const
{
    return m_requestFingerprint;
}

bool RuntimePackageActivationRecord::rollbackOnActivationFailure() const
{
    return m_rollbackOnActivationFailure;
}

quint64 RuntimePackageActivationRecord::revision() const
{
    return m_revision;
}

RuntimePackageActivationPhase RuntimePackageActivationRecord::phase() const
{
    return m_phase;
}

RuntimePackageActivationOutcome RuntimePackageActivationRecord::outcome() const
{
    return m_outcome;
}

const QList<RuntimePackageActivationAuditEvent> &RuntimePackageActivationRecord::audit() const
{
    return m_audit;
}

const QString &RuntimePackageActivationRecord::detail() const
{
    return m_detail;
}

const QDateTime &RuntimePackageActivationRecord::startedAt() const
{
    return m_startedAt;
}

const QDateTime &RuntimePackageActivationRecord::updatedAt() const
{
    return m_updatedAt;
}

const QDateTime &RuntimePackageActivationRecord::completedAt() const
{
    return m_completedAt;
}

const std::optional<RuntimePackageActivationControllerEvidence> &
RuntimePackageActivationRecord::beforeController() const
{
    return m_beforeController;
}

const std::optional<RuntimePackageActivationControllerEvidence> &
RuntimePackageActivationRecord::afterController() const
{
    return m_afterController;
}

const std::optional<RuntimePackageActivationProjectCommit> &
RuntimePackageActivationRecord::projectCommit() const
{
    return m_projectCommit;
}

const std::optional<RuntimePackageActivationCancellation> &
RuntimePackageActivationRecord::cancellation() const
{
    return m_cancellation;
}

const std::optional<RuntimePackageActivationDeploymentEvidence> &
RuntimePackageActivationRecord::deploymentEvidence() const
{
    return m_deploymentEvidence;
}

const QList<RuntimePackageActivationControllerEvidence> &
RuntimePackageActivationRecord::controllerEvidenceHistory() const
{
    return m_controllerEvidenceHistory;
}

bool RuntimePackageActivationRecord::needsReconciliation() const
{
    return runtimePackageActivationStateNeedsReconciliation(m_phase, m_outcome);
}

bool RuntimePackageActivationRecord::isValid() const
{
    if (!m_identity.isValid() || !m_requestFingerprint.isValid()
        || m_requestFingerprint
               != runtimePackageActivationCanonicalRequestFingerprint(
                   m_identity, m_rollbackOnActivationFailure)
        || !m_revision || m_revision != quint64(m_audit.size())
        || !phaseOutcomePairIsValid(m_phase, m_outcome)
        || !isCanonicalText(m_detail, 2048) || !m_startedAt.isValid()
        || !m_updatedAt.isValid() || m_updatedAt < m_startedAt || m_audit.isEmpty()) {
        return false;
    }

    const bool terminal = runtimePackageActivationOutcomeIsTerminal(m_outcome);
    if (terminal != m_completedAt.isValid()
        || (terminal && m_completedAt != m_updatedAt)
        || needsReconciliation()
               != runtimePackageActivationOutcomeNeedsReconciliation(m_outcome)) {
        return false;
    }

    QHash<QString, RuntimePackageActivationProviderAction> outstandingProviderRequests;
    bool outstandingControllerMutation = false;
    bool controllerMutationWasSent = false;
    bool acquiredControlStillHeld = false;
    quint64 acquiredControlSessionGeneration = 0;
    quint64 acquiredControlProviderSessionId = 0;
    quint64 acquiredControlBootId = 0;
    quint64 acquiredControlOwnerSessionId = 0;
    std::optional<RuntimePackageActivationSha256> acquireNotAppliedEvidence;
    std::optional<RuntimePackageActivationSha256> deployNotAppliedEvidence;
    qsizetype cancellationRequestIndex = -1;
    qsizetype projectCommitIndex = -1;
    const auto evidenceForDigest =
        [this](const RuntimePackageActivationSha256 &digest)
        -> const RuntimePackageActivationControllerEvidence * {
        for (const auto *evidence : {m_beforeController ? &*m_beforeController : nullptr,
                                     m_afterController ? &*m_afterController : nullptr}) {
            if (evidence && evidence->evidenceSha256() == digest)
                return evidence;
        }
        for (const RuntimePackageActivationControllerEvidence &evidence :
             m_controllerEvidenceHistory) {
            if (evidence.evidenceSha256() == digest)
                return &evidence;
        }
        return nullptr;
    };
    QDateTime previousOccurredAt;
    for (qsizetype index = 0; index < m_audit.size(); ++index) {
        const RuntimePackageActivationAuditEvent &event = m_audit.at(index);
        if (!event.isValid() || event.sequence() != quint64(index + 1)
            || event.occurredAt() < m_startedAt || event.occurredAt() > m_updatedAt
            || (previousOccurredAt.isValid() && event.occurredAt() < previousOccurredAt)) {
            return false;
        }
        if (index > 0) {
            const RuntimePackageActivationAuditEvent &previous = m_audit.at(index - 1);
            if (!runtimePackageActivationTransitionIsAllowed(
                    previous.phase(),
                    previous.outcome(),
                    event.phase(),
                    event.outcome())) {
                return false;
            }
        }
        previousOccurredAt = event.occurredAt();
        if (event.kind() == RuntimePackageActivationAuditKind::IntentPersisted && index != 0)
            return false;
        if (event.kind() == RuntimePackageActivationAuditKind::CancellationRequested) {
            if (index == 0 || cancellationRequestIndex >= 0)
                return false;
            cancellationRequestIndex = index;
        }
        if (event.kind() == RuntimePackageActivationAuditKind::ProjectCompareAndSet) {
            if (projectCommitIndex >= 0)
                return false;
            projectCommitIndex = index;
        }

        if (event.kind() == RuntimePackageActivationAuditKind::ProviderRequestSent) {
            const QString key = providerRequestKey(
                event.providerAction(),
                event.providerOperationId(),
                event.providerSessionGeneration(),
                event.providerSessionId(),
                event.providerBootId(),
                event.providerRequestId());
            if (outstandingProviderRequests.contains(key))
                return false;
            if (operationMayChangeController(event.providerAction())
                && std::any_of(
                    outstandingProviderRequests.cbegin(),
                    outstandingProviderRequests.cend(),
                    [](RuntimePackageActivationProviderAction action) {
                        return operationMayChangeController(action);
                    })) {
                return false;
            }
            outstandingProviderRequests.insert(key, event.providerAction());
            controllerMutationWasSent
                = controllerMutationWasSent
                  || operationMayChangeController(event.providerAction());
        } else if (
            event.kind() == RuntimePackageActivationAuditKind::ProviderTerminalResponse
            || event.kind()
                   == RuntimePackageActivationAuditKind::ProviderOutcomeReconciled) {
            const RuntimePackageActivationControllerEvidence *authoritativeEvidence = nullptr;
            if (event.kind()
                == RuntimePackageActivationAuditKind::ProviderOutcomeReconciled) {
                bool evidenceWasRecorded = false;
                for (qsizetype evidenceIndex = 0; evidenceIndex < index; ++evidenceIndex) {
                    const auto &candidateEvent = m_audit.at(evidenceIndex);
                    const auto &candidate = candidateEvent.evidenceSha256();
                    if (candidateEvent.kind()
                            == RuntimePackageActivationAuditKind::
                                ControllerEvidenceCaptured
                        && candidate && *candidate == *event.evidenceSha256()) {
                        evidenceWasRecorded = true;
                        break;
                    }
                }
                if (!evidenceWasRecorded)
                    return false;
                authoritativeEvidence = evidenceForDigest(*event.evidenceSha256());
                if (!authoritativeEvidence || !authoritativeEvidence->isValid()
                    || authoritativeEvidence->bootId() != event.providerBootId()) {
                    return false;
                }
                const bool applied
                    = event.providerReconciliation()
                      == RuntimePackageActivationProviderReconciliation::Applied;
                const bool samePackageAsBefore
                    = m_beforeController
                      && packageRuntimeStateMatches(
                          *m_beforeController, *authoritativeEvidence);
                const bool matchesTarget
                    = authoritativeEvidence->matchesActivatedIdentity(m_identity);
                bool evidenceMatchesDisposition = false;
                switch (event.providerAction()) {
                case RuntimePackageActivationProviderAction::AcquireControl:
                    evidenceMatchesDisposition
                        = applied
                              ? authoritativeEvidence->controlLeaseOwnerSessionId()
                                    == event.providerSessionId()
                              : samePackageAsBefore
                                    && authoritativeEvidence->controlLeaseOwnerSessionId()
                                           != event.providerSessionId();
                    if (!applied && evidenceMatchesDisposition)
                        acquireNotAppliedEvidence = event.evidenceSha256();
                    break;
                case RuntimePackageActivationProviderAction::ReleaseControl:
                    evidenceMatchesDisposition
                        = applied
                              ? authoritativeEvidence->controlLeaseOwnerSessionId()
                                    != event.providerSessionId()
                              : authoritativeEvidence->controlLeaseOwnerSessionId()
                                    == event.providerSessionId();
                    break;
                case RuntimePackageActivationProviderAction::DeployPackage:
                    evidenceMatchesDisposition
                        = applied ? matchesTarget : samePackageAsBefore;
                    break;
                default:
                    return false;
                }
                if (!evidenceMatchesDisposition)
                    return false;
                if (event.providerAction()
                        == RuntimePackageActivationProviderAction::DeployPackage
                    && event.providerReconciliation()
                           == RuntimePackageActivationProviderReconciliation::NotApplied) {
                    deployNotAppliedEvidence = event.evidenceSha256();
                }
            }
            const QString key = providerRequestKey(
                event.providerAction(),
                event.providerOperationId(),
                event.providerSessionGeneration(),
                event.providerSessionId(),
                event.providerBootId(),
                event.providerRequestId());
            const auto outstanding = outstandingProviderRequests.constFind(key);
            if (outstanding == outstandingProviderRequests.cend()
                || *outstanding != event.providerAction()) {
                return false;
            }
            outstandingProviderRequests.erase(outstanding);
            const bool applied
                = event.kind() == RuntimePackageActivationAuditKind::ProviderOutcomeReconciled
                      ? event.providerReconciliation()
                            == RuntimePackageActivationProviderReconciliation::Applied
                      : *event.providerStatus() == 0
                            && *event.providerOperationResult() == 0;
            if (event.kind()
                == RuntimePackageActivationAuditKind::ProviderTerminalResponse) {
                if (event.providerAction()
                        == RuntimePackageActivationProviderAction::AcquireControl
                    || event.providerAction()
                           == RuntimePackageActivationProviderAction::ReleaseControl) {
                    authoritativeEvidence = evidenceForDigest(*event.evidenceSha256());
                    bool evidenceWasRecorded = false;
                    for (qsizetype evidenceIndex = 0; evidenceIndex < index; ++evidenceIndex) {
                        const auto &candidateEvent = m_audit.at(evidenceIndex);
                        if (candidateEvent.kind()
                                == RuntimePackageActivationAuditKind::
                                    ControllerEvidenceCaptured
                            && candidateEvent.evidenceSha256()
                            && *candidateEvent.evidenceSha256()
                                   == *event.evidenceSha256()) {
                            evidenceWasRecorded = true;
                            break;
                        }
                    }
                    if (!authoritativeEvidence || !evidenceWasRecorded
                        || authoritativeEvidence->sessionGeneration()
                               != event.providerSessionGeneration()
                        || authoritativeEvidence->observationSessionId()
                               != event.providerSessionId()
                        || authoritativeEvidence->bootId()
                               != event.providerBootId()) {
                        return false;
                    }
                    if (applied
                        && ((event.providerAction()
                                 == RuntimePackageActivationProviderAction::AcquireControl
                             && authoritativeEvidence->controlLeaseOwnerSessionId()
                                    != event.providerSessionId())
                            || (event.providerAction()
                                    == RuntimePackageActivationProviderAction::ReleaseControl
                                && authoritativeEvidence->controlLeaseOwnerSessionId()
                                       == event.providerSessionId()))) {
                        return false;
                    }
                } else if (
                    event.providerAction()
                    == RuntimePackageActivationProviderAction::DeployPackage) {
                    if (!m_deploymentEvidence
                        || *event.evidenceSha256()
                               != m_deploymentEvidence->evidenceSha256()
                        || m_deploymentEvidence->providerSessionGeneration()
                               != event.providerSessionGeneration()
                        || m_deploymentEvidence->providerSessionId()
                               != event.providerSessionId()
                        || m_deploymentEvidence->providerBootId()
                               != event.providerBootId()
                        || (applied
                            && m_deploymentEvidence->outcome()
                                   != RuntimePackageActivationDeploymentOutcome::Succeeded)
                        || (!applied
                            && m_deploymentEvidence->outcome()
                                   == RuntimePackageActivationDeploymentOutcome::Succeeded)) {
                        return false;
                    }
                }
            }
            const bool acquireLeaseObserved
                = event.providerAction()
                      == RuntimePackageActivationProviderAction::AcquireControl
                  && (applied
                      || (event.kind()
                              == RuntimePackageActivationAuditKind::
                                  ProviderTerminalResponse
                          && authoritativeEvidence
                          && authoritativeEvidence->controlLeaseOwnerSessionId()
                                 == event.providerSessionId()));
            if (acquireLeaseObserved) {
                acquiredControlStillHeld = true;
                acquiredControlSessionGeneration = event.providerSessionGeneration();
                acquiredControlProviderSessionId = event.providerSessionId();
                acquiredControlBootId = event.providerBootId();
                acquiredControlOwnerSessionId = event.providerSessionId();
            }
            if (applied
                && event.providerAction()
                       == RuntimePackageActivationProviderAction::ReleaseControl) {
                if (event.kind()
                    == RuntimePackageActivationAuditKind::ProviderOutcomeReconciled) {
                    if (!acquiredControlStillHeld
                        || event.providerSessionGeneration()
                               != acquiredControlSessionGeneration
                        || event.providerSessionId()
                               != acquiredControlProviderSessionId
                        || event.providerBootId() != acquiredControlBootId
                        || authoritativeEvidence->controlLeaseOwnerSessionId()
                               == acquiredControlOwnerSessionId) {
                        return false;
                    }
                    acquiredControlStillHeld = false;
                } else if (
                    acquiredControlStillHeld
                    && event.providerSessionGeneration() == acquiredControlSessionGeneration
                    && event.providerSessionId() == acquiredControlProviderSessionId
                    && event.providerBootId() == acquiredControlBootId
                    && authoritativeEvidence
                    && authoritativeEvidence->controlLeaseOwnerSessionId()
                           != acquiredControlOwnerSessionId) {
                    acquiredControlStillHeld = false;
                } else {
                    return false;
                }
            }
        } else if (
            event.kind() == RuntimePackageActivationAuditKind::ControlLeaseReconciled) {
            bool evidenceWasRecorded = false;
            for (qsizetype evidenceIndex = 0; evidenceIndex < index; ++evidenceIndex) {
                const auto &candidateEvent = m_audit.at(evidenceIndex);
                const auto &candidate = candidateEvent.evidenceSha256();
                if (candidateEvent.kind()
                        == RuntimePackageActivationAuditKind::
                            ControllerEvidenceCaptured
                    && candidate && *candidate == *event.evidenceSha256()) {
                    evidenceWasRecorded = true;
                    break;
                }
            }
            const auto *authoritativeEvidence = evidenceForDigest(*event.evidenceSha256());
            if (!evidenceWasRecorded || !authoritativeEvidence
                || !authoritativeEvidence->isValid() || !acquiredControlStillHeld
                || event.providerSessionGeneration() != acquiredControlSessionGeneration
                || event.providerSessionId() != acquiredControlProviderSessionId
                || event.providerBootId() != acquiredControlBootId
                || authoritativeEvidence->bootId() != acquiredControlBootId
                || authoritativeEvidence->controlLeaseOwnerSessionId()
                       == acquiredControlOwnerSessionId) {
                return false;
            }
            acquiredControlStillHeld = false;
        }
    }
    for (RuntimePackageActivationProviderAction action :
         std::as_const(outstandingProviderRequests)) {
        if (operationMayChangeController(action)) {
            outstandingControllerMutation = true;
            break;
        }
    }
    const bool unresolvedProjectPersistence
        = m_projectCommit && m_beforeController && m_afterController
          && m_afterController->matchesActivatedIdentity(m_identity);

    const RuntimePackageActivationAuditEvent &first = m_audit.constFirst();
    const RuntimePackageActivationAuditEvent &last = m_audit.constLast();
    const bool unresolvedDurableCleanup
        = last.code() == QStringLiteral("activation-guard-delete-failed")
          && m_beforeController && m_afterController;
    const bool unresolvedProviderContradiction
        = (last.code() == QStringLiteral("acquire-response-contradictory")
           || last.code() == QStringLiteral("release-outcome-unknown")
           || last.code() == QStringLiteral("controller-provider-unavailable")
           || last.code() == QStringLiteral("recovery-lease-owner-mismatch")
           || last.code() == QStringLiteral("recovery-existing-package-lease")
           || last.code() == QStringLiteral("activation-recovery-release-invalid")
           || last.code() == QStringLiteral("recovery-request-id-exhausted")
           || last.code() == QStringLiteral("activation-reconciliation-incomplete"))
          && m_beforeController && m_afterController;
    const bool unresolvedControllerDrift
        = last.code() == QStringLiteral("controller-target-drifted")
          && m_beforeController && m_afterController;
    const bool unresolvedDurableRecovery
        = (last.code() == QStringLiteral("activation-recovered")
           || last.code()
                  == QStringLiteral("activation-journal-update-failed"))
          && m_beforeController;
    const RuntimePackageActivationControllerEvidence *releaseEvidence
        = last.evidenceSha256()
              ? evidenceForDigest(*last.evidenceSha256())
              : nullptr;
    const bool recoveryReleaseAwaitingFinalization
        = m_phase == RuntimePackageActivationPhase::Reconciling
          && last.kind()
                 == RuntimePackageActivationAuditKind::ProviderTerminalResponse
          && last.providerAction()
                 == RuntimePackageActivationProviderAction::ReleaseControl
          && last.providerStatus() == std::optional<qint32>(0)
          && last.providerOperationResult() == std::optional<qint32>(0)
          && releaseEvidence && !releaseEvidence->ownsControlLease();
    if (first.kind() != RuntimePackageActivationAuditKind::IntentPersisted
        || first.phase() != RuntimePackageActivationPhase::Queued
        || first.outcome() != RuntimePackageActivationOutcome::Pending
        || first.occurredAt() != m_startedAt || last.phase() != m_phase
        || last.outcome() != m_outcome || last.detail() != m_detail
        || last.occurredAt() != m_updatedAt
        || (terminal && last.kind() != RuntimePackageActivationAuditKind::Completed)
        || (terminal && !outstandingProviderRequests.isEmpty())
        || (terminal && acquiredControlStillHeld)
        || (m_outcome == RuntimePackageActivationOutcome::OutcomeUnknown
            && !outstandingControllerMutation && !acquiredControlStillHeld
            && !unresolvedProjectPersistence && !unresolvedDurableCleanup
            && !unresolvedProviderContradiction
            && !unresolvedControllerDrift
            && !unresolvedDurableRecovery
            && !recoveryReleaseAwaitingFinalization)) {
        return false;
    }

    const qsizetype deploymentEvidenceCaptureCount = std::count_if(
        m_audit.cbegin(), m_audit.cend(), [](const RuntimePackageActivationAuditEvent &event) {
            return event.kind()
                   == RuntimePackageActivationAuditKind::DeploymentEvidenceCaptured;
        });
    if ((m_beforeController && !m_beforeController->isValid())
        || (m_afterController && !m_afterController->isValid())
        || (m_projectCommit && !m_projectCommit->isValid())
        || (m_cancellation && !m_cancellation->isValid())
        || (m_deploymentEvidence && !m_deploymentEvidence->isValid())
        || m_projectCommit.has_value() != (projectCommitIndex >= 0)
        || m_cancellation.has_value() != (cancellationRequestIndex >= 0)
        || deploymentEvidenceCaptureCount > 1
        || m_deploymentEvidence.has_value()
               != (deploymentEvidenceCaptureCount == 1)) {
        return false;
    }
    QList<const RuntimePackageActivationControllerEvidence *> allControllerEvidence;
    if (m_beforeController)
        allControllerEvidence.append(&*m_beforeController);
    if (m_afterController)
        allControllerEvidence.append(&*m_afterController);
    for (const RuntimePackageActivationControllerEvidence &evidence :
         m_controllerEvidenceHistory) {
        allControllerEvidence.append(&evidence);
    }
    QSet<QByteArray> controllerEvidenceDigests;
    for (const RuntimePackageActivationControllerEvidence *evidence :
         std::as_const(allControllerEvidence)) {
        if (evidence
            && (evidence->scope() != m_identity.scope()
                || evidence->observedAt() < m_startedAt
                || evidence->observedAt() > m_updatedAt
                || !auditHasEvidence(
                    m_audit,
                    RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                    evidence->evidenceSha256()))) {
            return false;
        }
        controllerEvidenceDigests.insert(evidence->evidenceSha256().value());
    }
    for (const RuntimePackageActivationAuditEvent &event : m_audit) {
        if (event.kind() == RuntimePackageActivationAuditKind::ControllerEvidenceCaptured
            && (!event.evidenceSha256()
                || !controllerEvidenceDigests.contains(
                    event.evidenceSha256()->value()))) {
            return false;
        }
    }
    if (m_projectCommit
        && (m_projectCommit->originalDocumentRevision()
                != m_identity.documentRevisionToken()
            || m_projectCommit->originalBinding() != m_identity.originalBindingToken()
            || m_projectCommit->resultingBinding() != m_identity.targetBindingToken()
            || m_projectCommit->committedAt() < m_startedAt
            || m_projectCommit->committedAt() > m_updatedAt
            || m_audit.at(projectCommitIndex).occurredAt()
                   != m_projectCommit->committedAt()
            || *m_audit.at(projectCommitIndex).evidenceSha256()
                   != m_projectCommit->evidenceSha256()
            || !auditHasEvidence(
                m_audit,
                RuntimePackageActivationAuditKind::ProjectCompareAndSet,
                m_projectCommit->evidenceSha256()))) {
        return false;
    }
    if (m_cancellation
        && (m_cancellation->expectedRecordRevision() >= m_revision
            || m_cancellation->expectedRecordRevision()
                   != quint64(m_audit.at(cancellationRequestIndex).sequence() - 1)
            || m_cancellation->expectedDocumentRevision()
                   != m_identity.documentRevisionToken()
            || m_cancellation->expectedBinding() != m_identity.originalBindingToken()
            || m_cancellation->requestFingerprint()
                   != runtimePackageActivationCanonicalCancelFingerprint(
                       m_identity.operationId(),
                       m_cancellation->expectedRecordRevision(),
                       m_cancellation->expectedDocumentRevision(),
                       m_cancellation->expectedBinding())
            || *m_audit.at(cancellationRequestIndex).evidenceSha256()
                   != m_cancellation->requestFingerprint()
            || m_audit.at(cancellationRequestIndex).occurredAt()
                   != m_cancellation->requestedAt()
            || m_audit.at(cancellationRequestIndex - 1).phase()
                   != m_cancellation->requestedPhase()
            || m_cancellation->requestedAt() < m_startedAt
            || m_cancellation->requestedAt() > m_updatedAt)) {
        return false;
    }
    if (m_deploymentEvidence) {
        if (m_deploymentEvidence->artifactSha256() != m_identity.packageSha256()
            || (m_deploymentEvidence->candidate()
                && m_deploymentEvidence->candidate()->configurationId
                       != m_identity.configurationId())
            || m_deploymentEvidence->observedAt() < m_startedAt
            || m_deploymentEvidence->observedAt() > m_updatedAt
            || !m_beforeController
            || m_deploymentEvidence->previousActive()
                   != m_beforeController->persistentActive()
            || !auditHasEvidence(
                m_audit,
                RuntimePackageActivationAuditKind::DeploymentEvidenceCaptured,
                m_deploymentEvidence->evidenceSha256())) {
            return false;
        }
        for (const RuntimePackageActivationAuditEvent &event : m_audit) {
            if (event.providerAction()
                    == RuntimePackageActivationProviderAction::DeployPackage
                && (event.providerOperationId()
                        != m_deploymentEvidence->providerOperationId()
                    || event.providerSessionGeneration()
                           != m_deploymentEvidence->providerSessionGeneration()
                    || event.providerSessionId()
                           != m_deploymentEvidence->providerSessionId()
                    || event.providerBootId()
                           != m_deploymentEvidence->providerBootId())) {
                return false;
            }
        }
        switch (m_deploymentEvidence->outcome()) {
        case RuntimePackageActivationDeploymentOutcome::Succeeded:
            if (!auditHasSuccessfulProviderCompletion(
                    m_audit, RuntimePackageActivationProviderAction::DeployPackage)) {
                return false;
            }
            break;
        case RuntimePackageActivationDeploymentOutcome::Failed:
            if (!auditHasFailedTerminal(
                    m_audit, RuntimePackageActivationProviderAction::DeployPackage)) {
                return false;
            }
            break;
        case RuntimePackageActivationDeploymentOutcome::OutcomeUnknown:
            if (m_outcome
                    != RuntimePackageActivationOutcome::OutcomeUnknown
                && !std::any_of(
                    m_audit.cbegin(),
                    m_audit.cend(),
                    [](const RuntimePackageActivationAuditEvent &event) {
                        return event.kind()
                                   == RuntimePackageActivationAuditKind::
                                       ProviderOutcomeReconciled
                               && event.providerAction()
                                      == RuntimePackageActivationProviderAction::
                                          DeployPackage;
                    })) {
                return false;
            }
            break;
        }
    }

    const bool hasBeforeAndAfter = m_beforeController && m_afterController;
    const bool controllerUnchanged = hasBeforeAndAfter
                                     && m_beforeController->describesSameControllerStateAs(
                                         *m_afterController);
    const bool controllerRuntimeUnchanged
        = hasBeforeAndAfter
          && packageRuntimeStateMatches(
              *m_beforeController, *m_afterController)
          && !m_beforeController->controlLeaseOwnerSessionId()
          && !m_afterController->controlLeaseOwnerSessionId();
    const bool afterMatchesTarget
        = m_afterController && m_afterController->matchesActivatedIdentity(m_identity);
    const bool authoritativeDeployNotApplied
        = deployNotAppliedEvidence && m_beforeController && m_afterController
          && *deployNotAppliedEvidence == m_afterController->evidenceSha256()
          && packageRuntimeStateMatches(*m_beforeController, *m_afterController)
          && !acquiredControlStillHeld
          && m_afterController->controlLeaseOwnerSessionId()
                 != acquiredControlOwnerSessionId;
    const bool authoritativeAcquireNotApplied
        = acquireNotAppliedEvidence && m_beforeController && m_afterController
          && *acquireNotAppliedEvidence
                 == m_afterController->evidenceSha256()
          && packageRuntimeStateMatches(
              *m_beforeController, *m_afterController)
          && !acquiredControlStillHeld;
    qsizetype acquireSuccessIndex = -1;
    qsizetype deploySuccessIndex = -1;
    qsizetype releaseSuccessIndex = -1;
    for (qsizetype index = 0; index < m_audit.size(); ++index) {
        const RuntimePackageActivationAuditEvent &event = m_audit.at(index);
        const bool success
            = (event.kind() == RuntimePackageActivationAuditKind::ProviderTerminalResponse
               && event.providerStatus() == std::optional<qint32>(0)
               && event.providerOperationResult() == std::optional<qint32>(0))
              || (event.kind()
                      == RuntimePackageActivationAuditKind::ProviderOutcomeReconciled
                  && event.providerReconciliation()
                         == RuntimePackageActivationProviderReconciliation::Applied);
        if (event.kind()
                == RuntimePackageActivationAuditKind::ControlLeaseReconciled
            && event.providerAction()
                   == RuntimePackageActivationProviderAction::ReleaseControl
            && event.providerReconciliation()
                   == RuntimePackageActivationProviderReconciliation::Applied) {
            if (releaseSuccessIndex >= 0)
                return false;
            releaseSuccessIndex = index;
            continue;
        }
        if (!success)
            continue;
        switch (event.providerAction()) {
        case RuntimePackageActivationProviderAction::AcquireControl:
            if (acquireSuccessIndex >= 0)
                return false;
            acquireSuccessIndex = index;
            break;
        case RuntimePackageActivationProviderAction::DeployPackage:
            if (deploySuccessIndex >= 0)
                return false;
            deploySuccessIndex = index;
            break;
        case RuntimePackageActivationProviderAction::ReleaseControl:
            if (releaseSuccessIndex >= 0)
                return false;
            releaseSuccessIndex = index;
            break;
        default:
            break;
        }
    }
    const bool completeActivatedSequence
        = acquireSuccessIndex >= 0 && deploySuccessIndex > acquireSuccessIndex
          && projectCommitIndex > deploySuccessIndex
          && releaseSuccessIndex > projectCommitIndex && m_deploymentEvidence
          && m_deploymentEvidence->outcome()
                 == RuntimePackageActivationDeploymentOutcome::Succeeded
          && m_deploymentEvidence->candidate() && m_beforeController
          && m_beforeController->serviceState() == ControllerServiceState::Shutdown
          && m_afterController && m_afterController->ownsControlLease()
          && m_afterController->persistentActive()
                 == m_deploymentEvidence->candidate()
          && m_afterController->mappingAttestation()
          && m_afterController->mappingAttestation()->epoch.activePackageSlot
                 == m_deploymentEvidence->candidate()->slot
          && m_afterController->mappingAttestation()->epoch.activePackageGeneration
                 == m_deploymentEvidence->candidate()->generation
          && m_afterController->mappingAttestation()->epoch.configurationId
                 == m_deploymentEvidence->candidate()->configurationId
          && m_afterController->sessionGeneration()
                 == m_deploymentEvidence->providerSessionGeneration()
          && m_afterController->observationSessionId()
                 == m_deploymentEvidence->providerSessionId()
          && m_afterController->bootId() == m_deploymentEvidence->providerBootId();
    switch (m_outcome) {
    case RuntimePackageActivationOutcome::Pending:
        if (m_phase == RuntimePackageActivationPhase::CommittingProjectBinding
            && (!hasBeforeAndAfter || !afterMatchesTarget)) {
            return false;
        }
        if (m_phase == RuntimePackageActivationPhase::ReleasingControl) {
            if (!hasBeforeAndAfter || !acquiredControlStillHeld)
                return false;
            if (m_projectCommit) {
                if (!afterMatchesTarget)
                    return false;
            } else if (!controllerMutationWasSent) {
                return false;
            }
        }
        return true;
    case RuntimePackageActivationOutcome::OutcomeUnknown:
        if (!m_beforeController
            || (m_cancellation
                && m_cancellation->controllerResult()
                       != RuntimePackageActivationCancellationControllerResult::OutcomeUnknown)) {
            return false;
        }
        if (!m_projectCommit)
            return true;
        return hasBeforeAndAfter
               && ((afterMatchesTarget
                    && (acquiredControlStillHeld
                        || unresolvedProjectPersistence))
                   || unresolvedControllerDrift);
    case RuntimePackageActivationOutcome::SucceededWithExistingPackage:
        return hasBeforeAndAfter
               && m_beforeController->matchesActivatedIdentity(m_identity)
               && controllerRuntimeUnchanged && afterMatchesTarget
               && m_projectCommit
               && last.evidenceSha256()
               && *last.evidenceSha256() == m_projectCommit->evidenceSha256()
               && !auditHasOperation(
                   m_audit, RuntimePackageActivationProviderAction::DeployPackage)
               && acquireSuccessIndex < 0 && releaseSuccessIndex < 0
               && !m_cancellation;
    case RuntimePackageActivationOutcome::SucceededWithActivatedPackage:
        return hasBeforeAndAfter && afterMatchesTarget && m_projectCommit
               && completeActivatedSequence
               && last.evidenceSha256()
               && *last.evidenceSha256() == m_projectCommit->evidenceSha256()
               && auditHasSuccessfulProviderCompletion(
                   m_audit, RuntimePackageActivationProviderAction::DeployPackage)
               && !m_cancellation;
    case RuntimePackageActivationOutcome::Canceled:
        return !m_projectCommit && m_cancellation && !controllerMutationWasSent
               && !m_afterController
               && m_cancellation->controllerResult()
                      == RuntimePackageActivationCancellationControllerResult::
                          NoControllerMutation;
    case RuntimePackageActivationOutcome::FailedWithoutControllerChange:
        if (m_projectCommit || m_cancellation)
            return false;
        if (!controllerMutationWasSent)
            return !m_afterController
                   || (hasBeforeAndAfter
                       && controllerRuntimeUnchanged
                       && last.evidenceSha256()
                       && *last.evidenceSha256()
                              == m_afterController->evidenceSha256());
        return hasBeforeAndAfter
               && (controllerUnchanged || authoritativeDeployNotApplied
                   || authoritativeAcquireNotApplied)
               && last.evidenceSha256()
               && *last.evidenceSha256() == m_afterController->evidenceSha256();
    case RuntimePackageActivationOutcome::FailedAfterRollback:
        return hasBeforeAndAfter && controllerUnchanged && !m_projectCommit
               && last.evidenceSha256()
               && *last.evidenceSha256() == m_afterController->evidenceSha256()
               && m_deploymentEvidence
               && m_deploymentEvidence->outcome()
                      == RuntimePackageActivationDeploymentOutcome::Failed
               && std::any_of(
                   m_deploymentEvidence->progress().audit.cbegin(),
                   m_deploymentEvidence->progress().audit.cend(),
                   [](const ControllerPackageDeploymentAuditEvent &event) {
                       return event.operation == ControllerOperation::RollbackPackage
                              && event.status == std::optional<qint32>(0)
                              && event.operationResult == std::optional<qint32>(0);
                   });
    case RuntimePackageActivationOutcome::FailedControllerChangedWithoutBinding:
        return hasBeforeAndAfter && !controllerUnchanged && !m_projectCommit
               && last.evidenceSha256()
               && *last.evidenceSha256() == m_afterController->evidenceSha256();
    }
    return false;
}

RuntimePackageActivationSnapshot::RuntimePackageActivationSnapshot(
    quint64 sequence,
    QList<RuntimePackageActivationRecord> records,
    QDateTime capturedAt)
    : m_sequence(sequence)
    , m_records(std::move(records))
    , m_capturedAt(std::move(capturedAt))
{}

quint64 RuntimePackageActivationSnapshot::sequence() const
{
    return m_sequence;
}

const QList<RuntimePackageActivationRecord> &RuntimePackageActivationSnapshot::records() const
{
    return m_records;
}

const QDateTime &RuntimePackageActivationSnapshot::capturedAt() const
{
    return m_capturedAt;
}

bool RuntimePackageActivationSnapshot::isValid() const
{
    if (!m_sequence || !m_capturedAt.isValid())
        return false;
    QSet<QString> operationIds;
    for (const RuntimePackageActivationRecord &record : m_records) {
        if (!record.isValid() || record.updatedAt() > m_capturedAt)
            return false;
        const QString operationId = record.identity().operationId().value();
        if (operationIds.contains(operationId))
            return false;
        operationIds.insert(operationId);
    }
    return true;
}

} // namespace EtherCAT::Data
