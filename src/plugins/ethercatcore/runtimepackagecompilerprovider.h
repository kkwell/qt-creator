// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"
#include "providers.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

#include <QObject>

#include <optional>

namespace EtherCAT::Core {

enum class RuntimePackageCompilerJobState {
    Pending,
    Running,
    CancelRequested,
    Finished,
};

// A completion error is a provider-contract violation, not a compiler-domain
// result and not a scheduling failure. Scheduling failures return ResultError
// before a job exists; valid domain rejection and cancellation remain in
// RuntimePackageCompilerJobResult.
enum class RuntimePackageCompilerJobCompletionError {
    None,
    InvalidTerminalResult,
    CommandMismatch,
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
    virtual void requestCancellation() = 0;

private:
    void finishWithCompletionError(RuntimePackageCompilerJobCompletionError completionError);

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
// at all. Once a job is returned, compiler-domain rejection, cancellation, and
// unknown backend status are carried exclusively by its terminal result.
class ETHERCATCORE_EXPORT RuntimePackageCompilerProvider : public Provider
{
    Q_OBJECT

public:
    RuntimePackageCompilerProvider(
        Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual Utils::Result<RuntimePackageCompilerJob *> compile(
        const Data::RuntimePackageCompilerCompileRequest &request)
        = 0;
    virtual Utils::Result<RuntimePackageCompilerJob *> finalize(
        const Data::RuntimePackageCompilerFinalizeRequest &request)
        = 0;
    virtual Utils::Result<RuntimePackageCompilerJob *> query(
        const Data::RuntimePackageCompilerQueryRequest &request)
        = 0;
    virtual Utils::Result<RuntimePackageCompilerJob *> verify(
        const Data::RuntimePackageCompilerVerifyRequest &request)
        = 0;
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerJobState)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerJobCompletionError)
