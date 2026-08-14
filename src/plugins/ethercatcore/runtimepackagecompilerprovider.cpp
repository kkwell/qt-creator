// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilerprovider.h"

#include "providerregistry.h"
#include "runtimepackageactivationservice.h"

#include <utils/qtcassert.h>

#include <QCryptographicHash>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>

namespace EtherCAT::Core {

RuntimePackageCompilerJob::RuntimePackageCompilerJob(
    Data::RuntimePackageCompilerCommand command, QObject *parent)
    : QObject(parent)
    , m_command(command)
{}

Data::RuntimePackageCompilerCommand RuntimePackageCompilerJob::command() const
{
    return m_command;
}

RuntimePackageCompilerJobState RuntimePackageCompilerJob::state() const
{
    return m_state;
}

const std::optional<Data::RuntimePackageCompilerJobResult> &RuntimePackageCompilerJob::result() const
{
    return m_result;
}

RuntimePackageCompilerJobCompletionError RuntimePackageCompilerJob::completionError() const
{
    return m_completionError;
}

void RuntimePackageCompilerJob::cancel()
{
    if (QThread::currentThread() != thread()) {
        const QPointer<RuntimePackageCompilerJob> guard(this);
        const bool queued = QMetaObject::invokeMethod(
            this,
            [guard] {
                if (guard)
                    guard->cancel();
            },
            Qt::QueuedConnection);
        QTC_CHECK(queued);
        return;
    }

    if (m_state == RuntimePackageCompilerJobState::CancelRequested
        || m_state == RuntimePackageCompilerJobState::Finished) {
        return;
    }

    m_state = RuntimePackageCompilerJobState::CancelRequested;
    const QPointer<RuntimePackageCompilerJob> guard(this);
    emit stateChanged(RuntimePackageCompilerJobState::CancelRequested);
    if (guard && m_state == RuntimePackageCompilerJobState::CancelRequested)
        guard->requestCancellation();
}

void RuntimePackageCompilerJob::markRunning()
{
    if (QThread::currentThread() != thread()) {
        const QPointer<RuntimePackageCompilerJob> guard(this);
        const bool queued = QMetaObject::invokeMethod(
            this,
            [guard] {
                if (guard)
                    guard->markRunning();
            },
            Qt::QueuedConnection);
        QTC_CHECK(queued);
        return;
    }

    if (m_state != RuntimePackageCompilerJobState::Pending)
        return;

    m_state = RuntimePackageCompilerJobState::Running;
    emit stateChanged(RuntimePackageCompilerJobState::Running);
}

void RuntimePackageCompilerJob::finish(const Data::RuntimePackageCompilerJobResult &result)
{
    if (QThread::currentThread() != thread()) {
        const QPointer<RuntimePackageCompilerJob> guard(this);
        const bool queued = QMetaObject::invokeMethod(
            this,
            [guard, result] {
                if (guard)
                    guard->finish(result);
            },
            Qt::QueuedConnection);
        QTC_CHECK(queued);
        return;
    }

    if (m_state == RuntimePackageCompilerJobState::Finished)
        return;

    QTC_CHECK(result.isValid());
    if (!result.isValid()) {
        finishWithCompletionError(RuntimePackageCompilerJobCompletionError::InvalidTerminalResult);
        return;
    }

    QTC_CHECK(result.command() == m_command);
    if (result.command() != m_command) {
        finishWithCompletionError(RuntimePackageCompilerJobCompletionError::CommandMismatch);
        return;
    }

    m_result = result;
    m_completionError = RuntimePackageCompilerJobCompletionError::None;
    m_state = RuntimePackageCompilerJobState::Finished;
    const Data::RuntimePackageCompilerJobResult terminalResult = result;
    const QPointer<RuntimePackageCompilerJob> guard(this);
    emit stateChanged(RuntimePackageCompilerJobState::Finished);
    if (guard)
        guard->finished(terminalResult);
}

void RuntimePackageCompilerJob::finishWithCompletionError(
    RuntimePackageCompilerJobCompletionError completionError)
{
    QTC_CHECK(completionError != RuntimePackageCompilerJobCompletionError::None);
    if (m_state == RuntimePackageCompilerJobState::Finished
        || completionError == RuntimePackageCompilerJobCompletionError::None) {
        return;
    }

    m_result.reset();
    m_completionError = completionError;
    m_state = RuntimePackageCompilerJobState::Finished;
    const QPointer<RuntimePackageCompilerJob> guard(this);
    emit stateChanged(RuntimePackageCompilerJobState::Finished);
    if (guard)
        guard->completionFailed(completionError);
}

RuntimePackageCompilerProvider::RuntimePackageCompilerProvider(
    Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::RuntimePackageCompiler, id, displayName, parent)
{}

QString RuntimePackageCompilerProvider::unavailableReason() const
{
    return {};
}

bool RuntimePackageCompilerActivationProofAssemblyRequest::isValid() const
{
    static const QRegularExpression stableId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    return stableId.match(compilerProviderId).hasMatch() && contractIdentity.isValid()
           && compileRequest.isValid() && compileResult.isSuccess() && finalizeRequest.isValid()
           && finalizeResult.isSuccess() && verifyRequest.isValid() && verifyResult.isSuccess()
           && compileRequest.contractIdentity == contractIdentity
           && finalizeRequest.contractIdentity == contractIdentity
           && verifyRequest.contractIdentity == contractIdentity
           && compileResult.envelope.operationId == compileRequest.operationId
           && finalizeRequest.operationId == compileRequest.operationId
           && finalizeResult.envelope.operationId == compileRequest.operationId
           && verifyResult.envelope.operationId == verifyRequest.operationId
           && verifyRequest.operationId != compileRequest.operationId;
}

Utils::Result<Data::RuntimePackageCompilerActivationProof>
RuntimePackageCompilerProvider::assembleActivationProof(
    const RuntimePackageCompilerActivationProofAssemblyRequest &) const
{
    return Utils::ResultError(
        QStringLiteral("This compiler provider cannot assemble activation proof provenance."));
}

Utils::Result<> RuntimePackageCompilerProvider::validateActivationProof(
    const Data::RuntimePackageCompilerActivationProof &) const
{
    return Utils::ResultError(
        QStringLiteral("This compiler provider cannot validate activation proof provenance."));
}

Utils::Result<> validateRuntimePackageCompilerActivationProof(
    const ProviderRegistry *providerRegistry,
    const RuntimePackageActivationPreparationRequest &request,
    const Data::RuntimePackageActivationProjectCapture &capture)
{
    if (!providerRegistry)
        return Utils::ResultError(QStringLiteral("Compiler provider registry is unavailable."));
    if (QThread::currentThread() != providerRegistry->thread()) {
        return Utils::ResultError(
            QStringLiteral("Compiler provider registry belongs to a different thread."));
    }
    if (!request.compilerActivationProof)
        return Utils::ResultError(QStringLiteral("Compiler activation proof is missing."));
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("Compiler activation input is invalid."));

    const Data::RuntimePackageCompilerActivationProof &proof = *request.compilerActivationProof;
    const Data::RuntimePackageCompilerSha256 packageSha256{
        QCryptographicHash::hash(request.packageBytes, QCryptographicHash::Sha256)};
    if (!proof.isValid())
        return Utils::ResultError(QStringLiteral("Compiler activation proof is invalid."));
    if (!capture.isValid()
        || proof.compileRequest.topologyEvidence.scope != request.scope
        || proof.compileRequest.projectSnapshotEvidence
               != Data::RuntimePackageCompilerProjectSnapshotEvidence{capture}
        || proof.verifyResult != request.compilerVerification
        || proof.finalizeResult.packageBytes != request.packageBytes
        || *proof.finalizeResult.packageSha256 != packageSha256
        || proof.verifyRequest.packageBytes != request.packageBytes
        || proof.verifyRequest.packageSha256 != packageSha256
        || proof.compiledProjectSource != request.compiledProjectSource
        || proof.effectiveProjectCompanion != request.effectiveProjectCompanion) {
        return Utils::ResultError(
            QStringLiteral(
                "Compiler activation proof differs from the project capture or activation input."));
    }

    QList<Provider *> availableProviders;
    for (Provider *candidate : providerRegistry->providers(
             ProviderKind::RuntimePackageCompiler)) {
        if (!candidate)
            continue;
        if (candidate->thread() != QThread::currentThread()) {
            return Utils::ResultError(
                QStringLiteral("A compiler provider belongs to a different thread."));
        }
        if (candidate->isAvailable()) {
            availableProviders.append(candidate);
        }
    }
    if (availableProviders.size() != 1) {
        return Utils::ResultError(
            availableProviders.isEmpty()
                ? QStringLiteral("No available compiler provider can validate the proof.")
                : QStringLiteral("Compiler provider selection is ambiguous."));
    }

    auto *provider = qobject_cast<RuntimePackageCompilerProvider *>(
        availableProviders.constFirst());
    if (!provider) {
        return Utils::ResultError(
            QStringLiteral("The selected compiler provider cannot validate activation proof."));
    }
    if (provider->id().toString() != proof.compilerProviderId) {
        return Utils::ResultError(
            QStringLiteral("The available compiler provider does not own this proof."));
    }
    return provider->validateActivationProof(proof);
}

} // namespace EtherCAT::Core
