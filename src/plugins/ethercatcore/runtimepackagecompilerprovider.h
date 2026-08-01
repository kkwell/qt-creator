// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"
#include "providers.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

#include <QObject>

#include <optional>

namespace EtherCAT::Core {

class ProviderRegistry;
struct RuntimePackageActivationPreparationRequest;

enum class RuntimePackageCompilerJobState {
    Pending,
    Running,
    CancelRequested,
    Finished,
};

// A completion error means no trustworthy compiler-domain terminal result was
// available. Scheduling failures return ResultError before a job exists;
// valid backend rejection and authoritative cancellation remain in
// RuntimePackageCompilerJobResult. Cancellation without a recoverable terminal
// record is CanceledAfterReconciliation.
enum class RuntimePackageCompilerJobCompletionError {
    None,
    InvalidTerminalResult,
    CommandMismatch,
    BackendProcessFailure,
    CanceledAfterReconciliation,
};

// Identifies one completed compile -> finalize -> independent verify chain
// without accepting caller-selected filesystem paths or caller-supplied
// compiled-project/companion bytes. The typed terminal records are retained so
// the provider can compare them with its exact immutable canonical evidence;
// any path fields inside those records are comparisons only and never read
// sources. Package bytes are likewise accepted only after byte-for-byte
// comparison with the provider's sealed package evidence.
struct ETHERCATCORE_EXPORT RuntimePackageCompilerActivationProofAssemblyRequest
{
    QString compilerProviderId;
    Data::RuntimePackageCompilerContractIdentity contractIdentity;
    Data::RuntimePackageCompilerCompileRequest compileRequest;
    Data::RuntimePackageCompilerCompileResult compileResult;
    Data::RuntimePackageCompilerFinalizeRequest finalizeRequest;
    Data::RuntimePackageCompilerFinalizeResult finalizeResult;
    Data::RuntimePackageCompilerVerifyRequest verifyRequest;
    Data::RuntimePackageCompilerVerifyResult verifyResult;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerActivationProofAssemblyRequest &,
        const RuntimePackageCompilerActivationProofAssemblyRequest &) = default;
};

class ETHERCATCORE_EXPORT RuntimePackageCompilerJob : public QObject
{
    Q_OBJECT

public:
    explicit RuntimePackageCompilerJob(
        Data::RuntimePackageCompilerCommand command, QObject *parent = nullptr);

    Data::RuntimePackageCompilerCommand command() const;
    RuntimePackageCompilerJobState state() const;
    const std::optional<Data::RuntimePackageCompilerJobResult> &result() const;
    RuntimePackageCompilerJobCompletionError completionError() const;

    // Cancellation is idempotent. It requests cancellation but does not
    // manufacture a terminal result; the provider still owns terminal truth.
    //
    // Lifecycle mutations are serialized on this object's QObject thread.
    // Calls from another thread are queued and return before the mutation.
    void cancel();

signals:
    void stateChanged(EtherCAT::Core::RuntimePackageCompilerJobState state);
    void finished(const EtherCAT::Data::RuntimePackageCompilerJobResult &result);
    void completionFailed(EtherCAT::Core::RuntimePackageCompilerJobCompletionError completionError);

protected:
    // These functions have the same thread boundary as cancel(): a provider
    // may call them from a worker thread, but the state change is then queued.
    void markRunning();
    void finish(const Data::RuntimePackageCompilerJobResult &result);
    void finishWithCompletionError(RuntimePackageCompilerJobCompletionError completionError);
    virtual void requestCancellation() = 0;

private:
    const Data::RuntimePackageCompilerCommand m_command;
    RuntimePackageCompilerJobState m_state = RuntimePackageCompilerJobState::Pending;
    std::optional<Data::RuntimePackageCompilerJobResult> m_result;
    RuntimePackageCompilerJobCompletionError m_completionError
        = RuntimePackageCompilerJobCompletionError::None;
};

// This provider schedules compiler-domain work only. It owns the canonical
// API-042 codec: typed IDE requests are encoded once, while exact backend
// responses are schema-validated and compared with their typed projection
// before a job may finish. A ResultError means the job could not be scheduled
// at all. Once a job is returned, compiler-domain rejection and unknown backend
// status are carried by its terminal result. Cancellation either preserves an
// authoritative backend result or ends with CanceledAfterReconciliation.
class ETHERCATCORE_EXPORT RuntimePackageCompilerProvider : public Provider
{
    Q_OBJECT

public:
    RuntimePackageCompilerProvider(
        Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual Utils::Result<RuntimePackageCompilerJob *> compile(
        const Data::RuntimePackageCompilerCompileRequest &request) = 0;
    virtual Utils::Result<RuntimePackageCompilerJob *> finalize(
        const Data::RuntimePackageCompilerFinalizeRequest &request) = 0;
    virtual Utils::Result<RuntimePackageCompilerJob *> query(
        const Data::RuntimePackageCompilerQueryRequest &request) = 0;
    virtual Utils::Result<RuntimePackageCompilerJob *> verify(
        const Data::RuntimePackageCompilerVerifyRequest &request) = 0;

    // Only the immutable operation-store owner can assemble a proof. The base
    // implementation rejects so a coordinator cannot manufacture provenance
    // from terminal values, paths, or artifact bytes.
    virtual Utils::Result<Data::RuntimePackageCompilerActivationProof> assembleActivationProof(
        const RuntimePackageCompilerActivationProofAssemblyRequest &request) const;

    // This is a read-only provider-provenance gate. It binds a proof to the
    // provider's immutable evidence and trusted external verifier result; it
    // is not an independent in-process cryptographic ECPKG verification. The
    // base implementation rejects because only the store owner can establish
    // provenance.
    virtual Utils::Result<> validateActivationProof(
        const Data::RuntimePackageCompilerActivationProof &proof) const;
};

// Binds one activation preparation to the exact compiler proof, selects one
// unambiguous available compiler provider, and delegates the read-only
// provenance decision to that provider. This does not parse or authenticate
// an ECPKG independently.
ETHERCATCORE_EXPORT Utils::Result<> validateRuntimePackageCompilerActivationProof(
    const ProviderRegistry *providerRegistry,
    const RuntimePackageActivationPreparationRequest &request,
    const Data::RuntimePackageActivationProjectCapture &capture);

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerJobState)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerJobCompletionError)
