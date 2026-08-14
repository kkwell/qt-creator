// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"
#include "runtimepackageactivationservice.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

#include <QList>
#include <QObject>
#include <QPointer>

#include <optional>

namespace EtherCAT::Core {

class ProviderRegistry;
class RuntimePackageCompilerProvider;

// This is an IDE-local compiler workflow. It never represents a controller,
// lease, deployment, or runtime operation. In particular, Ready means that a
// verified activation preparation was assembled; it does not mean that it was
// sent to RuntimePackageActivationService.
enum class RuntimePackageCompilerPreparationPhase {
    Idle,
    Reserved,
    Compiling,
    AwaitingDetachedSignature,
    Finalizing,
    Verifying,
    AssemblingProof,
    Ready,
    CancelRequested,
    ReconciliationRequired,
    Canceled,
    Failed,
};

ETHERCATCORE_EXPORT bool runtimePackageCompilerPreparationPhaseIsTerminal(
    RuntimePackageCompilerPreparationPhase phase);
ETHERCATCORE_EXPORT bool runtimePackageCompilerPreparationPhaseAllowsCancel(
    RuntimePackageCompilerPreparationPhase phase);
ETHERCATCORE_EXPORT bool runtimePackageCompilerPreparationTransitionIsAllowed(
    RuntimePackageCompilerPreparationPhase from, RuntimePackageCompilerPreparationPhase to);

// The compile, independent verify, and later activation operations have
// distinct durable identities. The activation ID is carried forward but is
// never executed by this coordinator.
struct ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationStartRequest
{
    Data::RuntimePackageCompilerCompileRequest compileRequest;
    Data::RuntimePackageCompilerOperationId verifyOperationId;
    Data::RuntimePackageActivationOperationId activationOperationId;
    bool rollbackOnActivationFailure = true;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPreparationStartRequest &,
        const RuntimePackageCompilerPreparationStartRequest &)
        = default;
};

// This digest is the restart-safe identity of the exact upper capture and
// lower canonical compiler request. The typed StartRequest is intentionally
// not serialized by Core; after restart, the caller must resupply it exactly.
ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerSha256>
runtimePackageCompilerPreparationStartRequestFingerprint(
    const RuntimePackageCompilerPreparationStartRequest &request);

// Durable terminal tombstone restored without reconstructing typed compiler
// requests or activation proof. Digests are an ordered summary only; they are
// never sufficient to publish preparationReady() or activate a package.
struct ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationTerminalSummary
{
    std::optional<Data::RuntimePackageCompilerSha256> compileResultSha256;
    std::optional<Data::RuntimePackageCompilerSha256> signingRequestSha256;
    std::optional<Data::RuntimePackageCompilerSha256> finalizeResultSha256;
    std::optional<Data::RuntimePackageCompilerSha256> packageSha256;
    std::optional<Data::RuntimePackageCompilerSha256> verifyResultSha256;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPreparationTerminalSummary &,
        const RuntimePackageCompilerPreparationTerminalSummary &)
        = default;
};

struct ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationRecord
{
    Data::RuntimePackageCompilerOperationId compileOperationId;
    Data::RuntimePackageCompilerSha256 startRequestFingerprint;
    Data::RuntimePackageCompilerOperationId verifyOperationId;
    Data::RuntimePackageActivationOperationId activationOperationId;
    // Missing only for a restart placeholder in ReconciliationRequired or an
    // explicit terminalSummary. The full typed request must be resupplied to
    // resume() and match the frozen fingerprint; the journal never
    // reconstructs it from canonical JSON.
    std::optional<RuntimePackageCompilerPreparationStartRequest> startRequest;
    // Present for every in-process record and recomputed from the exact typed
    // compile request. It may be absent only from a restart placeholder or
    // terminal summary that deliberately retains no typed request.
    std::optional<Data::RuntimePackageCompilerSha256> compileRequestSha256;
    QString compilerProviderId;
    Data::RuntimePackageCompilerContractIdentity contractIdentity;
    quint64 revision = 0;
    RuntimePackageCompilerPreparationPhase phase = RuntimePackageCompilerPreparationPhase::Idle;
    std::optional<Data::RuntimePackageCompilerCompileResult> compileResult;
    std::optional<Data::RuntimePackageCompilerCanonicalJson> detachedSigningRequest;
    std::optional<Data::RuntimePackageCompilerFinalizeRequest> finalizeRequest;
    std::optional<Data::RuntimePackageCompilerSha256> finalizeRequestSha256;
    std::optional<Data::RuntimePackageCompilerFinalizeResult> finalizeResult;
    std::optional<Data::RuntimePackageCompilerVerifyRequest> verifyRequest;
    std::optional<Data::RuntimePackageCompilerSha256> verifyRequestSha256;
    std::optional<Data::RuntimePackageCompilerVerifyResult> verifyResult;
    std::optional<RuntimePackageActivationPreparationRequest> preparation;
    // Set only when a durable terminal entry is restored after restart and
    // its full typed proof is intentionally unavailable.
    std::optional<RuntimePackageCompilerPreparationTerminalSummary> terminalSummary;
    QString detail;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPreparationRecord &,
        const RuntimePackageCompilerPreparationRecord &)
        = default;
};

struct ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationSnapshot
{
    quint64 sequence = 0;
    QList<RuntimePackageCompilerPreparationRecord> records;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPreparationSnapshot &,
        const RuntimePackageCompilerPreparationSnapshot &)
        = default;
};

enum class RuntimePackageCompilerPreparationDisposition {
    Started,
    Replayed,
    Accepted,
    AlreadyTerminal,
    ReconciliationRequired,
};

// Thread-safe-by-rejection coordinator contract. All public calls, including
// reads, must originate on this object's thread and the ProviderRegistry's
// thread. A concrete implementation owns durable journaling and job recovery;
// Core owns validation, exact replay admission, and unambiguous provider
// selection.
//
// start() selects exactly one available RuntimePackageCompilerProvider and
// passes it to doStart(). The concrete implementation must durably reserve the
// complete request and freeze provider.id() plus the contract identity before
// calling provider.compile(). It must not inspect CompileResult.outputDirectory
// or read compiler files directly.
//
// A successful compile always stops at AwaitingDetachedSignature and emits
// detachedSigningRequested(). The IDE holds no private key. Only the exact
// detached response supplied through submitDetachedSigningResponse() may be
// passed to finalize(). Verify uses startRequest.verifyOperationId. Proof
// assembly belongs to the frozen provider, after which preparationReady() may
// be emitted. No method in this class accesses a controller or hardware.
//
// A running cancellation only requests provider cancellation and remains
// nonterminal until provider truth arrives. Job loss, an untrusted terminal,
// or provider disappearance must be durably represented as
// ReconciliationRequired; resume() is admitted only for the exact original
// StartRequest and the exact frozen provider.
class ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit RuntimePackageCompilerPreparationCoordinator(
        ProviderRegistry *providerRegistry, QObject *parent = nullptr);

    Utils::Result<RuntimePackageCompilerPreparationDisposition> start(
        const RuntimePackageCompilerPreparationStartRequest &request);
    Utils::Result<RuntimePackageCompilerPreparationDisposition> submitDetachedSigningResponse(
        const Data::RuntimePackageCompilerOperationId &compileOperationId,
        const Data::RuntimePackageCompilerCanonicalJson &detachedSigningResponse);
    Utils::Result<RuntimePackageCompilerPreparationDisposition> cancel(
        const Data::RuntimePackageCompilerOperationId &compileOperationId);
    Utils::Result<RuntimePackageCompilerPreparationDisposition> resume(
        const RuntimePackageCompilerPreparationStartRequest &exactOriginalRequest);

    Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>> record(
        const Data::RuntimePackageCompilerOperationId &compileOperationId) const;
    Utils::Result<RuntimePackageCompilerPreparationSnapshot> snapshot() const;

signals:
    void recordChanged(const EtherCAT::Core::RuntimePackageCompilerPreparationRecord &record);
    void snapshotChanged(const EtherCAT::Core::RuntimePackageCompilerPreparationSnapshot &snapshot);
    void detachedSigningRequested(
        const EtherCAT::Data::RuntimePackageCompilerOperationId &compileOperationId,
        const EtherCAT::Data::RuntimePackageCompilerCanonicalJson &signingRequest);
    void preparationReady(
        const EtherCAT::Core::RuntimePackageActivationPreparationRequest &preparation);

protected:
    // Concrete implementations call these only after the record and snapshot
    // have been durably committed. They re-read that state, validate the exact
    // transition, and emit only trusted public signals. An exact same-record
    // replay is accepted without emitting duplicate signals.
    Utils::Result<> publishRecordTransition(
        const RuntimePackageCompilerPreparationRecord &current,
        const RuntimePackageCompilerPreparationSnapshot &snapshot);
    Utils::Result<> publishDetachedSigningRequest(
        const RuntimePackageCompilerPreparationRecord &record);
    Utils::Result<> publishPreparationReady(const RuntimePackageCompilerPreparationRecord &record);

    virtual Utils::Result<RuntimePackageCompilerPreparationDisposition> doStart(
        const RuntimePackageCompilerPreparationStartRequest &request,
        RuntimePackageCompilerProvider *frozenProvider)
        = 0;
    virtual Utils::Result<RuntimePackageCompilerPreparationDisposition>
    doSubmitDetachedSigningResponse(
        const RuntimePackageCompilerPreparationRecord &record,
        const Data::RuntimePackageCompilerCanonicalJson &detachedSigningResponse,
        RuntimePackageCompilerProvider *frozenProvider)
        = 0;
    virtual Utils::Result<RuntimePackageCompilerPreparationDisposition> doCancel(
        const RuntimePackageCompilerPreparationRecord &record)
        = 0;
    virtual Utils::Result<RuntimePackageCompilerPreparationDisposition> doResume(
        const RuntimePackageCompilerPreparationRecord &record,
        const RuntimePackageCompilerPreparationStartRequest &exactOriginalRequest,
        RuntimePackageCompilerProvider *frozenProvider)
        = 0;
    virtual Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>> doRecord(
        const Data::RuntimePackageCompilerOperationId &compileOperationId) const
        = 0;
    virtual Utils::Result<RuntimePackageCompilerPreparationSnapshot> doSnapshot() const = 0;

    // Return the durable predecessor captured by the same successful CAS that
    // committed currentRevision/snapshotSequence. The default fails closed so
    // an implementation cannot publish until its journal exposes trustworthy
    // transition history. Null is valid only for Reserved revision 1.
    virtual Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>>
    doPreviousRecordForCommittedTransition(
        const Data::RuntimePackageCompilerOperationId &compileOperationId,
        quint64 currentRevision,
        quint64 snapshotSequence) const;

private:
    Utils::Result<> validateThreadAccess() const;
    Utils::Result<RuntimePackageCompilerProvider *> uniqueAvailableProvider() const;
    Utils::Result<RuntimePackageCompilerProvider *> frozenProvider(
        const RuntimePackageCompilerPreparationRecord &record) const;
    Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>> checkedRecord(
        const Data::RuntimePackageCompilerOperationId &compileOperationId) const;

    QPointer<ProviderRegistry> m_providerRegistry;
    quint64 m_lastPublishedSnapshotSequence = 0;
    std::optional<RuntimePackageCompilerPreparationRecord> m_lastPublishedRecord;
    std::optional<RuntimePackageCompilerPreparationSnapshot> m_lastPublishedSnapshot;
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerPreparationPhase)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerPreparationStartRequest)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerPreparationRecord)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerPreparationSnapshot)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerPreparationDisposition)
