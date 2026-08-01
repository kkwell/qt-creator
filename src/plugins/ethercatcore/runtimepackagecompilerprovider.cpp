// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilerprovider.h"

#include <utils/qtcassert.h>

#include <QMetaObject>
#include <QPointer>
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

Utils::Result<> RuntimePackageCompilerProvider::validateActivationProof(
    const Data::RuntimePackageCompilerActivationProof &) const
{
    return Utils::ResultError(
        QStringLiteral("This compiler provider cannot validate activation proof provenance."));
}

} // namespace EtherCAT::Core
