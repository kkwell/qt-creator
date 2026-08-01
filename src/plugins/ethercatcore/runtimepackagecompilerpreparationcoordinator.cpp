// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilerpreparationcoordinator.h"

#include "providerregistry.h"
#include "runtimepackagecompilercodec.h"
#include "runtimepackagecompilerprovider.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <algorithm>

namespace EtherCAT::Core {

namespace {

bool hasTrimmedDetail(const QString &detail)
{
    return !detail.isEmpty() && detail == detail.trimmed();
}

std::optional<Data::RuntimePackageCompilerSha256> shaField(
    const Data::RuntimePackageCompilerCanonicalJson &document, const QString &name)
{
    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    const QJsonValue value = QJsonDocument::fromJson(document.exactBytes()).object().value(name);
    if (!value.isString() || !shaPattern.match(value.toString()).hasMatch())
        return std::nullopt;
    Data::RuntimePackageCompilerSha256 result{QByteArray::fromHex(value.toString().toLatin1())};
    return result.isValid() ? std::optional(result) : std::nullopt;
}

bool hasNoTypedEvidence(const RuntimePackageCompilerPreparationRecord &record)
{
    return !record.compileRequestSha256 && !record.compileResult && !record.detachedSigningRequest
           && !record.finalizeRequest && !record.finalizeRequestSha256 && !record.finalizeResult
           && !record.verifyRequest && !record.verifyRequestSha256 && !record.verifyResult
           && !record.preparation;
}

bool hasCoherentResultPrefix(const RuntimePackageCompilerPreparationRecord &record)
{
    if (!record.startRequest || !record.compileRequestSha256
        || !record.compileRequestSha256->isValid()) {
        return false;
    }
    const RuntimePackageCompilerPreparationStartRequest &start = *record.startRequest;
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> encodedCompileRequest
        = encodeRuntimePackageCompilerCompileRequest(start.compileRequest);
    if (!encodedCompileRequest || encodedCompileRequest->sha256() != *record.compileRequestSha256) {
        return false;
    }
    if (record.compileResult) {
        if (!record.compileResult->isValid()
            || record.compileResult->envelope.operationId != start.compileRequest.operationId
            || record.compileResult->envelope.configurationId != start.compileRequest.configurationId
            || record.compileResult->envelope.requestSha256 != *record.compileRequestSha256) {
            return false;
        }
    } else if (
        record.detachedSigningRequest || record.finalizeRequest || record.finalizeRequestSha256
        || record.finalizeResult || record.verifyRequest || record.verifyRequestSha256
        || record.verifyResult || record.preparation) {
        return false;
    }
    if (record.detachedSigningRequest) {
        if (!record.detachedSigningRequest->isValid() || !record.compileResult
            || !record.compileResult->isSuccess() || !record.compileResult->signRequest
            || *record.detachedSigningRequest != *record.compileResult->signRequest) {
            return false;
        }
    } else if (
        record.finalizeRequest || record.finalizeRequestSha256 || record.finalizeResult
        || record.verifyRequest || record.verifyRequestSha256 || record.verifyResult
        || record.preparation) {
        return false;
    }

    if (record.finalizeRequest.has_value() != record.finalizeRequestSha256.has_value())
        return false;
    if (record.finalizeRequest) {
        const Data::RuntimePackageCompilerFinalizeRequest &request = *record.finalizeRequest;
        const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> encodedRequest
            = encodeRuntimePackageCompilerFinalizeRequest(request);
        if (!record.compileResult || !record.compileResult->isSuccess()
            || !record.detachedSigningRequest || !request.isValid() || !encodedRequest
            || encodedRequest->sha256() != *record.finalizeRequestSha256
            || request.operationId != start.compileRequest.operationId
            || request.configurationId != start.compileRequest.configurationId
            || request.contractIdentity != record.contractIdentity
            || request.compileRequestSha256 != *record.compileRequestSha256
            || request.signRequestSha256 != record.detachedSigningRequest->sha256()
            || request.manifestSha256 != *record.compileResult->manifestSha256
            || request.signingKeyIdSha256 != start.compileRequest.targetProfile.signingKeyIdSha256
            || request.signingPolicyRevision != start.compileRequest.targetProfile.policyRevision) {
            return false;
        }
    } else if (
        record.finalizeResult || record.verifyRequest || record.verifyRequestSha256
        || record.verifyResult || record.preparation) {
        return false;
    }

    if (record.finalizeResult) {
        const Data::RuntimePackageCompilerFinalizeRequest &request = *record.finalizeRequest;
        if (!record.finalizeResult->isValid()
            || record.finalizeResult->envelope.operationId != request.operationId
            || record.finalizeResult->envelope.configurationId != request.configurationId
            || record.finalizeResult->envelope.requestSha256 != request.compileRequestSha256) {
            return false;
        }
        if (record.finalizeResult->isSuccess()) {
            const std::optional<Data::RuntimePackageCompilerSha256> receiptSha256
                = shaField(request.detachedSigningResponse, QStringLiteral("receipt_sha256"));
            if (!receiptSha256 || !record.finalizeResult->manifestSha256
                || *record.finalizeResult->manifestSha256 != request.manifestSha256
                || !record.finalizeResult->signingReceiptSha256
                || *record.finalizeResult->signingReceiptSha256 != *receiptSha256
                || !record.finalizeResult->packageSha256
                || QCryptographicHash::hash(
                       record.finalizeResult->packageBytes, QCryptographicHash::Sha256)
                       != record.finalizeResult->packageSha256->value()) {
                return false;
            }
        }
    } else if (
        record.verifyRequest || record.verifyRequestSha256 || record.verifyResult
        || record.preparation) {
        return false;
    }

    if (record.verifyRequest.has_value() != record.verifyRequestSha256.has_value())
        return false;
    if (record.verifyRequest) {
        const Data::RuntimePackageCompilerVerifyRequest &request = *record.verifyRequest;
        const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> encodedRequest
            = encodeRuntimePackageCompilerVerifyRequest(request);
        if (!record.finalizeResult || !record.finalizeResult->isSuccess() || !request.isValid()
            || !encodedRequest || encodedRequest->sha256() != *record.verifyRequestSha256
            || request.operationId != start.verifyOperationId
            || request.contractIdentity != record.contractIdentity
            || request.packageBytes != record.finalizeResult->packageBytes
            || request.packageSha256 != *record.finalizeResult->packageSha256) {
            return false;
        }
    } else if (record.verifyResult || record.preparation) {
        return false;
    }

    if (record.verifyResult) {
        const Data::RuntimePackageCompilerVerifyRequest &request = *record.verifyRequest;
        if (!record.verifyResult->isValid()
            || record.verifyResult->envelope.operationId != request.operationId
            || record.verifyResult->envelope.requestSha256 != request.packageSha256
            || record.verifyResult->packageSha256 != request.packageSha256
            || (record.verifyResult->isSuccess()
                && record.verifyResult->envelope.configurationId
                       != start.compileRequest.configurationId)) {
            return false;
        }
    }
    if (record.preparation) {
        if (!record.verifyResult || !record.verifyResult->isSuccess()
            || !record.preparation->isValid()
            || record.preparation->operationId != start.activationOperationId
            || record.preparation->scope != start.compileRequest.topologyEvidence.scope
            || record.preparation->rollbackOnActivationFailure != start.rollbackOnActivationFailure
            || record.preparation->compilerVerification != *record.verifyResult
            || !record.preparation->compilerActivationProof) {
            return false;
        }
        const Data::RuntimePackageCompilerActivationProof &proof
            = *record.preparation->compilerActivationProof;
        if (proof.compilerProviderId != record.compilerProviderId
            || proof.contractIdentity != record.contractIdentity
            || proof.compileRequest != start.compileRequest
            || proof.compileResult != *record.compileResult
            || proof.finalizeRequest != *record.finalizeRequest
            || proof.finalizeRequestSha256 != *record.finalizeRequestSha256
            || proof.finalizeResult != *record.finalizeResult
            || proof.verifyRequest != *record.verifyRequest
            || proof.verifyRequestSha256 != *record.verifyRequestSha256
            || proof.verifyResult != *record.verifyResult
            || record.preparation->packageBytes != proof.finalizeResult.packageBytes
            || record.preparation->compiledProjectSource != proof.compiledProjectSource
            || record.preparation->effectiveProjectCompanion != proof.effectiveProjectCompanion) {
            return false;
        }
    }
    return true;
}

void appendUnsigned64(QByteArray &bytes, quint64 value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.append(char((value >> shift) & 0xff));
}

void appendSizedBytes(QByteArray &bytes, QByteArrayView value)
{
    const quint32 size = quint32(value.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes.append(char((size >> shift) & 0xff));
    bytes.append(value.data(), value.size());
}

bool hasSuccessfulCompilePrefix(const RuntimePackageCompilerPreparationRecord &record)
{
    return record.compileResult && record.compileResult->isSuccess()
           && record.detachedSigningRequest;
}

bool hasFinalizeRequestPrefix(const RuntimePackageCompilerPreparationRecord &record)
{
    return hasSuccessfulCompilePrefix(record) && record.finalizeRequest
           && record.finalizeRequestSha256;
}

bool hasSuccessfulFinalizePrefix(const RuntimePackageCompilerPreparationRecord &record)
{
    return hasFinalizeRequestPrefix(record) && record.finalizeResult
           && record.finalizeResult->isSuccess();
}

bool hasVerifyRequestPrefix(const RuntimePackageCompilerPreparationRecord &record)
{
    return hasSuccessfulFinalizePrefix(record) && record.verifyRequest
           && record.verifyRequestSha256;
}

bool hasSuccessfulVerifyPrefix(const RuntimePackageCompilerPreparationRecord &record)
{
    return hasVerifyRequestPrefix(record) && record.verifyResult
           && record.verifyResult->isSuccess();
}

template<typename T>
bool preservesPresentEvidence(const std::optional<T> &previous, const std::optional<T> &current)
{
    return !previous || current == previous;
}

bool preservesDurableEvidencePrefix(
    const RuntimePackageCompilerPreparationRecord &previous,
    const RuntimePackageCompilerPreparationRecord &current)
{
    return preservesPresentEvidence(previous.compileRequestSha256, current.compileRequestSha256)
           && preservesPresentEvidence(previous.compileResult, current.compileResult)
           && preservesPresentEvidence(
               previous.detachedSigningRequest, current.detachedSigningRequest)
           && preservesPresentEvidence(previous.finalizeRequest, current.finalizeRequest)
           && preservesPresentEvidence(
               previous.finalizeRequestSha256, current.finalizeRequestSha256)
           && preservesPresentEvidence(previous.finalizeResult, current.finalizeResult)
           && preservesPresentEvidence(previous.verifyRequest, current.verifyRequest)
           && preservesPresentEvidence(previous.verifyRequestSha256, current.verifyRequestSha256)
           && preservesPresentEvidence(previous.verifyResult, current.verifyResult)
           && preservesPresentEvidence(previous.preparation, current.preparation);
}

} // namespace

bool runtimePackageCompilerPreparationPhaseIsTerminal(RuntimePackageCompilerPreparationPhase phase)
{
    return phase == RuntimePackageCompilerPreparationPhase::Ready
           || phase == RuntimePackageCompilerPreparationPhase::Canceled
           || phase == RuntimePackageCompilerPreparationPhase::Failed;
}

bool runtimePackageCompilerPreparationPhaseAllowsCancel(RuntimePackageCompilerPreparationPhase phase)
{
    switch (phase) {
    case RuntimePackageCompilerPreparationPhase::Reserved:
    case RuntimePackageCompilerPreparationPhase::Compiling:
    case RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature:
    case RuntimePackageCompilerPreparationPhase::Finalizing:
    case RuntimePackageCompilerPreparationPhase::Verifying:
    case RuntimePackageCompilerPreparationPhase::AssemblingProof:
    case RuntimePackageCompilerPreparationPhase::CancelRequested:
        return true;
    case RuntimePackageCompilerPreparationPhase::Idle:
    case RuntimePackageCompilerPreparationPhase::Ready:
    case RuntimePackageCompilerPreparationPhase::ReconciliationRequired:
    case RuntimePackageCompilerPreparationPhase::Canceled:
    case RuntimePackageCompilerPreparationPhase::Failed:
        return false;
    }
    return false;
}

bool runtimePackageCompilerPreparationTransitionIsAllowed(
    RuntimePackageCompilerPreparationPhase from, RuntimePackageCompilerPreparationPhase to)
{
    if (from == to)
        return false;
    if (from == RuntimePackageCompilerPreparationPhase::Idle)
        return to == RuntimePackageCompilerPreparationPhase::Reserved;
    if (runtimePackageCompilerPreparationPhaseIsTerminal(from)) {
        return false;
    }
    if (to == RuntimePackageCompilerPreparationPhase::ReconciliationRequired)
        return true;
    if (from == RuntimePackageCompilerPreparationPhase::ReconciliationRequired) {
        return to != RuntimePackageCompilerPreparationPhase::Idle
               && to != RuntimePackageCompilerPreparationPhase::Ready;
    }
    if (to == RuntimePackageCompilerPreparationPhase::CancelRequested)
        return runtimePackageCompilerPreparationPhaseAllowsCancel(from);
    if (to == RuntimePackageCompilerPreparationPhase::Failed)
        return true;
    if (to == RuntimePackageCompilerPreparationPhase::Canceled) {
        return from == RuntimePackageCompilerPreparationPhase::Reserved
               || from == RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature
               || from == RuntimePackageCompilerPreparationPhase::CancelRequested;
    }

    switch (from) {
    case RuntimePackageCompilerPreparationPhase::Reserved:
        return to == RuntimePackageCompilerPreparationPhase::Compiling;
    case RuntimePackageCompilerPreparationPhase::Compiling:
        return to == RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature;
    case RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature:
        return to == RuntimePackageCompilerPreparationPhase::Finalizing;
    case RuntimePackageCompilerPreparationPhase::Finalizing:
        return to == RuntimePackageCompilerPreparationPhase::Verifying;
    case RuntimePackageCompilerPreparationPhase::Verifying:
        return to == RuntimePackageCompilerPreparationPhase::AssemblingProof;
    case RuntimePackageCompilerPreparationPhase::AssemblingProof:
        return to == RuntimePackageCompilerPreparationPhase::Ready;
    case RuntimePackageCompilerPreparationPhase::Idle:
    case RuntimePackageCompilerPreparationPhase::Ready:
    case RuntimePackageCompilerPreparationPhase::CancelRequested:
    case RuntimePackageCompilerPreparationPhase::ReconciliationRequired:
    case RuntimePackageCompilerPreparationPhase::Canceled:
    case RuntimePackageCompilerPreparationPhase::Failed:
        return false;
    }
    return false;
}

bool RuntimePackageCompilerPreparationStartRequest::isValid() const
{
    if (!compileRequest.isValid() || !verifyOperationId.isValid()
        || !activationOperationId.isValid()) {
        return false;
    }
    const QString &compileId = compileRequest.operationId.value();
    const QString &verifyId = verifyOperationId.value();
    const QString &activationId = activationOperationId.value();
    return compileId != verifyId && compileId != activationId && verifyId != activationId;
}

Utils::Result<Data::RuntimePackageCompilerSha256>
runtimePackageCompilerPreparationStartRequestFingerprint(
    const RuntimePackageCompilerPreparationStartRequest &request)
{
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation request is invalid."));
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalCompileRequest
        = encodeRuntimePackageCompilerCompileRequest(request.compileRequest);
    if (!canonicalCompileRequest)
        return Utils::ResultError(canonicalCompileRequest.error());

    const Data::RuntimePackageCompilerProjectSnapshotEvidence &capture
        = request.compileRequest.projectSnapshotEvidence;
    QByteArray identity = QByteArrayLiteral("ethercat-runtime-package-preparation-start-v1");
    identity.append('\0');
    appendSizedBytes(identity, canonicalCompileRequest->sha256().value());
    appendSizedBytes(identity, capture.serializedProjectSha256().value());
    appendUnsigned64(identity, capture.documentRevisionNumber());
    appendSizedBytes(identity, capture.documentRevision().value());
    appendSizedBytes(identity, capture.originalBinding().value());
    appendSizedBytes(identity, request.verifyOperationId.value().toUtf8());
    appendSizedBytes(identity, request.activationOperationId.value().toUtf8());
    identity.append(request.rollbackOnActivationFailure ? '\x01' : '\x00');
    return Data::RuntimePackageCompilerSha256{
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256)};
}

bool RuntimePackageCompilerPreparationTerminalSummary::isValid() const
{
    const auto valid = [](const std::optional<Data::RuntimePackageCompilerSha256> &digest) {
        return !digest || digest->isValid();
    };
    if (!valid(compileResultSha256) || !valid(signingRequestSha256) || !valid(finalizeResultSha256)
        || !valid(packageSha256) || !valid(verifyResultSha256)) {
        return false;
    }
    return (!signingRequestSha256 || compileResultSha256)
           && (!finalizeResultSha256 || signingRequestSha256)
           && (!packageSha256 || finalizeResultSha256) && (!verifyResultSha256 || packageSha256);
}

bool RuntimePackageCompilerPreparationRecord::isValid() const
{
    static const QRegularExpression stableProviderId(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    if (!compileOperationId.isValid() || !startRequestFingerprint.isValid()
        || !verifyOperationId.isValid() || !activationOperationId.isValid()
        || compileOperationId.value() == verifyOperationId.value()
        || compileOperationId.value() == activationOperationId.value()
        || verifyOperationId.value() == activationOperationId.value()
        || compilerProviderId.isEmpty() || !stableProviderId.match(compilerProviderId).hasMatch()
        || revision == 0 || phase == RuntimePackageCompilerPreparationPhase::Idle
        || !contractIdentity.isValid()) {
        return false;
    }
    if (terminalSummary) {
        return terminalSummary->isValid() && runtimePackageCompilerPreparationPhaseIsTerminal(phase)
               && !startRequest && hasNoTypedEvidence(*this) && hasTrimmedDetail(detail)
               && (phase != RuntimePackageCompilerPreparationPhase::Ready
                   || terminalSummary->verifyResultSha256.has_value());
    }
    if (startRequest) {
        const Utils::Result<Data::RuntimePackageCompilerSha256> fingerprint
            = runtimePackageCompilerPreparationStartRequestFingerprint(*startRequest);
        if (!fingerprint || *fingerprint != startRequestFingerprint
            || startRequest->compileRequest.operationId != compileOperationId
            || startRequest->verifyOperationId != verifyOperationId
            || startRequest->activationOperationId != activationOperationId
            || startRequest->compileRequest.contractIdentity != contractIdentity) {
            return false;
        }
    } else if (
        phase != RuntimePackageCompilerPreparationPhase::ReconciliationRequired
        || !hasNoTypedEvidence(*this)) {
        return false;
    }
    if (startRequest && !hasCoherentResultPrefix(*this))
        return false;
    if (!detail.isEmpty() && detail != detail.trimmed())
        return false;

    switch (phase) {
    case RuntimePackageCompilerPreparationPhase::Reserved:
    case RuntimePackageCompilerPreparationPhase::Compiling:
        return !compileResult && !detachedSigningRequest && !finalizeRequest
               && !finalizeRequestSha256 && !finalizeResult && !verifyRequest
               && !verifyRequestSha256 && !verifyResult && !preparation && detail.isEmpty();
    case RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature:
        return hasSuccessfulCompilePrefix(*this) && !finalizeRequest && !finalizeRequestSha256
               && !finalizeResult && !verifyRequest && !verifyRequestSha256 && !verifyResult
               && !preparation && detail.isEmpty();
    case RuntimePackageCompilerPreparationPhase::Finalizing:
        return hasFinalizeRequestPrefix(*this) && !finalizeResult && !verifyRequest
               && !verifyRequestSha256 && !verifyResult && !preparation && detail.isEmpty();
    case RuntimePackageCompilerPreparationPhase::Verifying:
        return hasVerifyRequestPrefix(*this) && !verifyResult && !preparation && detail.isEmpty();
    case RuntimePackageCompilerPreparationPhase::AssemblingProof:
        return hasSuccessfulVerifyPrefix(*this) && !preparation && detail.isEmpty();
    case RuntimePackageCompilerPreparationPhase::Ready:
        return hasSuccessfulVerifyPrefix(*this) && preparation && detail.isEmpty();
    case RuntimePackageCompilerPreparationPhase::CancelRequested:
        return !preparation;
    case RuntimePackageCompilerPreparationPhase::ReconciliationRequired:
        return !preparation && hasTrimmedDetail(detail);
    case RuntimePackageCompilerPreparationPhase::Canceled:
    case RuntimePackageCompilerPreparationPhase::Failed:
        return !preparation && hasTrimmedDetail(detail);
    case RuntimePackageCompilerPreparationPhase::Idle:
        return false;
    }
    return false;
}

bool RuntimePackageCompilerPreparationSnapshot::isValid() const
{
    if (sequence == 0)
        return records.isEmpty();
    QSet<QString> operationIds;
    for (const RuntimePackageCompilerPreparationRecord &record : records) {
        if (!record.isValid())
            return false;
        const QString &operationId = record.compileOperationId.value();
        if (operationIds.contains(operationId))
            return false;
        operationIds.insert(operationId);
    }
    return true;
}

RuntimePackageCompilerPreparationCoordinator::RuntimePackageCompilerPreparationCoordinator(
    ProviderRegistry *providerRegistry, QObject *parent)
    : QObject(parent)
    , m_providerRegistry(providerRegistry)
{}

Utils::Result<> RuntimePackageCompilerPreparationCoordinator::publishRecordTransition(
    const RuntimePackageCompilerPreparationRecord &current,
    const RuntimePackageCompilerPreparationSnapshot &currentSnapshot)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    if (!current.isValid() || !currentSnapshot.isValid() || currentSnapshot.sequence == 0) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation transition evidence is invalid."));
    }
    if (current.terminalSummary) {
        return Utils::ResultError(
            QStringLiteral("A terminal restart summary cannot publish a live transition."));
    }

    const auto persisted = checkedRecord(current.compileOperationId);
    if (!persisted || !*persisted || **persisted != current) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation record was not durably committed."));
    }
    const Utils::Result<RuntimePackageCompilerPreparationSnapshot> persistedSnapshot = doSnapshot();
    if (!persistedSnapshot || !persistedSnapshot->isValid()
        || *persistedSnapshot != currentSnapshot) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation snapshot was not durably committed."));
    }
    const auto exactRecord
        = std::find(currentSnapshot.records.cbegin(), currentSnapshot.records.cend(), current);
    if (exactRecord == currentSnapshot.records.cend()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation snapshot omits the current record."));
    }

    const Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>> previousResult
        = doPreviousRecordForCommittedTransition(
            current.compileOperationId, current.revision, currentSnapshot.sequence);
    if (!previousResult) {
        return Utils::ResultError(previousResult.error());
    }
    const std::optional<RuntimePackageCompilerPreparationRecord> &previous = *previousResult;
    if (previous) {
        if (!previous->isValid() || previous->compileOperationId != current.compileOperationId
            || current.revision != previous->revision + 1
            || current.startRequestFingerprint != previous->startRequestFingerprint
            || (previous->compileRequestSha256
                && current.compileRequestSha256 != previous->compileRequestSha256)
            || current.verifyOperationId != previous->verifyOperationId
            || current.activationOperationId != previous->activationOperationId
            || (previous->startRequest && current.startRequest != previous->startRequest)
            || current.compilerProviderId != previous->compilerProviderId
            || current.contractIdentity != previous->contractIdentity
            || (previous->phase
                    != RuntimePackageCompilerPreparationPhase::ReconciliationRequired
                && !preservesDurableEvidencePrefix(*previous, current))
            || !runtimePackageCompilerPreparationTransitionIsAllowed(previous->phase, current.phase)) {
            return Utils::ResultError(
                QStringLiteral("Durable compiler preparation transition is not allowed."));
        }
    } else if (current.revision != 1 || current.phase != RuntimePackageCompilerPreparationPhase::Reserved) {
        return Utils::ResultError(
            QStringLiteral("Initial compiler preparation record must be Reserved revision 1."));
    }

    const bool exactReplay = m_lastPublishedRecord && m_lastPublishedSnapshot
                             && *m_lastPublishedRecord == current
                             && *m_lastPublishedSnapshot == currentSnapshot;
    if (exactReplay)
        return Utils::ResultOk;
    if (m_lastPublishedSnapshotSequence != 0
        && currentSnapshot.sequence <= m_lastPublishedSnapshotSequence) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation snapshot sequence did not advance."));
    }

    m_lastPublishedSnapshotSequence = currentSnapshot.sequence;
    m_lastPublishedRecord = current;
    m_lastPublishedSnapshot = currentSnapshot;
    const QPointer<RuntimePackageCompilerPreparationCoordinator> guard(this);
    emit recordChanged(current);
    // A direct recordChanged slot may durably commit and publish N+1. Never
    // expose stale snapshot N after that nested publication returns.
    if (guard && guard->m_lastPublishedSnapshotSequence == currentSnapshot.sequence
        && guard->m_lastPublishedRecord && *guard->m_lastPublishedRecord == current
        && guard->m_lastPublishedSnapshot && *guard->m_lastPublishedSnapshot == currentSnapshot) {
        emit guard->snapshotChanged(currentSnapshot);
    }
    return Utils::ResultOk;
}

Utils::Result<> RuntimePackageCompilerPreparationCoordinator::publishDetachedSigningRequest(
    const RuntimePackageCompilerPreparationRecord &current)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    if (!current.isValid()
        || current.phase != RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature
        || !current.detachedSigningRequest) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation is not awaiting a detached signature."));
    }
    const auto persisted = checkedRecord(current.compileOperationId);
    if (!persisted || !*persisted || **persisted != current) {
        return Utils::ResultError(
            QStringLiteral("Detached signing request was not durably committed."));
    }
    emit detachedSigningRequested(current.compileOperationId, *current.detachedSigningRequest);
    return Utils::ResultOk;
}

Utils::Result<> RuntimePackageCompilerPreparationCoordinator::publishPreparationReady(
    const RuntimePackageCompilerPreparationRecord &current)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    if (!current.isValid() || current.phase != RuntimePackageCompilerPreparationPhase::Ready
        || !current.preparation) {
        return Utils::ResultError(QStringLiteral("Compiler activation preparation is not ready."));
    }
    const auto persisted = checkedRecord(current.compileOperationId);
    if (!persisted || !*persisted || **persisted != current) {
        return Utils::ResultError(
            QStringLiteral("Compiler activation preparation was not durably committed."));
    }
    emit preparationReady(*current.preparation);
    return Utils::ResultOk;
}

Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>>
RuntimePackageCompilerPreparationCoordinator::doPreviousRecordForCommittedTransition(
    const Data::RuntimePackageCompilerOperationId &, quint64, quint64) const
{
    return Utils::ResultError(
        QStringLiteral("Durable compiler preparation transition history is unavailable."));
}

Utils::Result<> RuntimePackageCompilerPreparationCoordinator::validateThreadAccess() const
{
    if (!m_providerRegistry)
        return Utils::ResultError(QStringLiteral("Compiler provider registry is unavailable."));
    if (QThread::currentThread() != thread()
        || QThread::currentThread() != m_providerRegistry->thread()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation coordinator belongs to a different thread."));
    }
    return Utils::ResultOk;
}

Utils::Result<RuntimePackageCompilerProvider *>
RuntimePackageCompilerPreparationCoordinator::uniqueAvailableProvider() const
{
    QList<Provider *> available;
    for (Provider *provider : m_providerRegistry->providers(ProviderKind::RuntimePackageCompiler)) {
        if (!provider)
            continue;
        if (provider->thread() != QThread::currentThread()) {
            return Utils::ResultError(
                QStringLiteral("A compiler provider belongs to a different thread."));
        }
        if (provider->isAvailable()) {
            available.append(provider);
        }
    }
    if (available.size() != 1) {
        return Utils::ResultError(
            available.isEmpty()
                ? QStringLiteral("No available compiler provider can start preparation.")
                : QStringLiteral("Compiler provider selection is ambiguous."));
    }
    auto *provider = qobject_cast<RuntimePackageCompilerProvider *>(available.constFirst());
    if (!provider || provider->thread() != thread()) {
        return Utils::ResultError(
            QStringLiteral("The selected compiler provider is incompatible."));
    }
    return provider;
}

Utils::Result<RuntimePackageCompilerProvider *>
RuntimePackageCompilerPreparationCoordinator::frozenProvider(
    const RuntimePackageCompilerPreparationRecord &record) const
{
    Provider *candidate = m_providerRegistry->provider(
        Utils::Id::fromString(record.compilerProviderId));
    if (!candidate || candidate->thread() != QThread::currentThread()) {
        return Utils::ResultError(QStringLiteral("The frozen compiler provider is unavailable."));
    }
    auto *provider = qobject_cast<RuntimePackageCompilerProvider *>(candidate);
    if (!provider || !provider->isAvailable()) {
        return Utils::ResultError(QStringLiteral("The frozen compiler provider is unavailable."));
    }
    return provider;
}

Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>>
RuntimePackageCompilerPreparationCoordinator::checkedRecord(
    const Data::RuntimePackageCompilerOperationId &compileOperationId) const
{
    if (!compileOperationId.isValid())
        return Utils::ResultError(QStringLiteral("Compiler operation ID is invalid."));
    const Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>> found = doRecord(
        compileOperationId);
    if (!found)
        return Utils::ResultError(found.error());
    if (*found && (!(*found)->isValid() || (*found)->compileOperationId != compileOperationId)) {
        return Utils::ResultError(QStringLiteral("Compiler preparation record is invalid."));
    }
    return *found;
}

Utils::Result<RuntimePackageCompilerPreparationDisposition>
RuntimePackageCompilerPreparationCoordinator::start(
    const RuntimePackageCompilerPreparationStartRequest &request)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation request is invalid."));
    const Utils::Result<Data::RuntimePackageCompilerSha256> fingerprint
        = runtimePackageCompilerPreparationStartRequestFingerprint(request);
    if (!fingerprint)
        return Utils::ResultError(fingerprint.error());

    const auto existing = checkedRecord(request.compileRequest.operationId);
    if (!existing)
        return Utils::ResultError(existing.error());
    if (*existing) {
        if ((*existing)->startRequestFingerprint != *fingerprint
            || (*existing)->verifyOperationId != request.verifyOperationId
            || (*existing)->activationOperationId != request.activationOperationId
            || (*existing)->contractIdentity != request.compileRequest.contractIdentity
            || ((*existing)->startRequest && *(*existing)->startRequest != request)) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation OperationId conflicts with its record."));
        }
        if ((*existing)->phase == RuntimePackageCompilerPreparationPhase::ReconciliationRequired) {
            return RuntimePackageCompilerPreparationDisposition::ReconciliationRequired;
        }
        if (runtimePackageCompilerPreparationPhaseIsTerminal((*existing)->phase))
            return RuntimePackageCompilerPreparationDisposition::AlreadyTerminal;
        return RuntimePackageCompilerPreparationDisposition::Replayed;
    }

    const Utils::Result<RuntimePackageCompilerProvider *> provider = uniqueAvailableProvider();
    if (!provider)
        return Utils::ResultError(provider.error());
    return doStart(request, *provider);
}

Utils::Result<RuntimePackageCompilerPreparationDisposition>
RuntimePackageCompilerPreparationCoordinator::submitDetachedSigningResponse(
    const Data::RuntimePackageCompilerOperationId &compileOperationId,
    const Data::RuntimePackageCompilerCanonicalJson &detachedSigningResponse)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    if (!detachedSigningResponse.isValid()) {
        return Utils::ResultError(
            QStringLiteral("Detached signing response is not exact canonical JSON."));
    }
    const auto found = checkedRecord(compileOperationId);
    if (!found)
        return Utils::ResultError(found.error());
    if (!*found)
        return Utils::ResultError(QStringLiteral("Compiler preparation was not found."));
    const RuntimePackageCompilerPreparationRecord &record = **found;
    if (record.phase == RuntimePackageCompilerPreparationPhase::ReconciliationRequired)
        return RuntimePackageCompilerPreparationDisposition::ReconciliationRequired;
    if (runtimePackageCompilerPreparationPhaseIsTerminal(record.phase))
        return RuntimePackageCompilerPreparationDisposition::AlreadyTerminal;
    if (record.finalizeRequest) {
        if (record.finalizeRequest->detachedSigningResponse == detachedSigningResponse)
            return RuntimePackageCompilerPreparationDisposition::Replayed;
        return Utils::ResultError(
            QStringLiteral("Detached signing response conflicts with its durable request."));
    }
    if (record.phase != RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation is not awaiting a detached signature."));
    }
    const Utils::Result<RuntimePackageCompilerProvider *> provider = frozenProvider(record);
    if (!provider)
        return Utils::ResultError(provider.error());
    return doSubmitDetachedSigningResponse(record, detachedSigningResponse, *provider);
}

Utils::Result<RuntimePackageCompilerPreparationDisposition>
RuntimePackageCompilerPreparationCoordinator::cancel(
    const Data::RuntimePackageCompilerOperationId &compileOperationId)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    const auto found = checkedRecord(compileOperationId);
    if (!found)
        return Utils::ResultError(found.error());
    if (!*found)
        return Utils::ResultError(QStringLiteral("Compiler preparation was not found."));
    const RuntimePackageCompilerPreparationRecord &record = **found;
    if (record.phase == RuntimePackageCompilerPreparationPhase::ReconciliationRequired)
        return RuntimePackageCompilerPreparationDisposition::ReconciliationRequired;
    if (runtimePackageCompilerPreparationPhaseIsTerminal(record.phase))
        return RuntimePackageCompilerPreparationDisposition::AlreadyTerminal;
    if (record.phase == RuntimePackageCompilerPreparationPhase::CancelRequested)
        return RuntimePackageCompilerPreparationDisposition::Replayed;
    if (!runtimePackageCompilerPreparationPhaseAllowsCancel(record.phase)) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation cannot be canceled in its current phase."));
    }
    return doCancel(record);
}

Utils::Result<RuntimePackageCompilerPreparationDisposition>
RuntimePackageCompilerPreparationCoordinator::resume(
    const RuntimePackageCompilerPreparationStartRequest &exactOriginalRequest)
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    if (!exactOriginalRequest.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation request is invalid."));
    const Utils::Result<Data::RuntimePackageCompilerSha256> fingerprint
        = runtimePackageCompilerPreparationStartRequestFingerprint(exactOriginalRequest);
    if (!fingerprint)
        return Utils::ResultError(fingerprint.error());
    const auto found = checkedRecord(exactOriginalRequest.compileRequest.operationId);
    if (!found)
        return Utils::ResultError(found.error());
    if (!*found)
        return Utils::ResultError(QStringLiteral("Compiler preparation was not found."));
    const RuntimePackageCompilerPreparationRecord &record = **found;
    if (record.startRequestFingerprint != *fingerprint
        || record.verifyOperationId != exactOriginalRequest.verifyOperationId
        || record.activationOperationId != exactOriginalRequest.activationOperationId
        || record.contractIdentity != exactOriginalRequest.compileRequest.contractIdentity
        || (record.startRequest && *record.startRequest != exactOriginalRequest)) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation resume input differs from the original."));
    }
    if (runtimePackageCompilerPreparationPhaseIsTerminal(record.phase))
        return RuntimePackageCompilerPreparationDisposition::AlreadyTerminal;
    if (record.phase != RuntimePackageCompilerPreparationPhase::ReconciliationRequired)
        return RuntimePackageCompilerPreparationDisposition::Replayed;
    const Utils::Result<RuntimePackageCompilerProvider *> provider = frozenProvider(record);
    if (!provider)
        return RuntimePackageCompilerPreparationDisposition::ReconciliationRequired;
    return doResume(record, exactOriginalRequest, *provider);
}

Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>>
RuntimePackageCompilerPreparationCoordinator::record(
    const Data::RuntimePackageCompilerOperationId &compileOperationId) const
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    return checkedRecord(compileOperationId);
}

Utils::Result<RuntimePackageCompilerPreparationSnapshot>
RuntimePackageCompilerPreparationCoordinator::snapshot() const
{
    const Utils::Result<> access = validateThreadAccess();
    if (!access)
        return Utils::ResultError(access.error());
    const Utils::Result<RuntimePackageCompilerPreparationSnapshot> current = doSnapshot();
    if (!current)
        return Utils::ResultError(current.error());
    if (!current->isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation snapshot is invalid."));
    return *current;
}

} // namespace EtherCAT::Core
