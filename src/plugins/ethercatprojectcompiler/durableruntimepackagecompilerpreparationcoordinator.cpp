// Copyright (C) 2026 Embed Labs

#include "durableruntimepackagecompilerpreparationcoordinator.h"

#include "runtimepackagecompilerpreparationjournal.h"

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/runtimepackagecompilercodec.h>
#include <ethercatcore/runtimepackagecompilerprovider.h>

#include <QHash>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <variant>

namespace EtherCAT::ProjectCompiler {

namespace {

using Phase = Core::RuntimePackageCompilerPreparationPhase;
using Disposition = Core::RuntimePackageCompilerPreparationDisposition;
using Record = Core::RuntimePackageCompilerPreparationRecord;
using Snapshot = Core::RuntimePackageCompilerPreparationSnapshot;
using StartRequest = Core::RuntimePackageCompilerPreparationStartRequest;

QString providerId(const Core::RuntimePackageCompilerProvider *provider)
{
    return provider ? QString::fromUtf8(provider->id().name()) : QString{};
}

QString terminalDetail(Data::RuntimePackageCompilerResultStatus status, QStringView stage)
{
    if (status == Data::RuntimePackageCompilerResultStatus::Canceled)
        return QStringLiteral("Compiler preparation was canceled.");
    return QStringLiteral("Compiler preparation failed during %1.").arg(stage);
}

std::optional<Data::RuntimePackageCompilerSha256> resultSha256(
    const std::optional<Data::RuntimePackageCompilerCompileResult> &result)
{
    return result ? std::optional(result->envelope.canonicalResult.sha256()) : std::nullopt;
}

std::optional<Data::RuntimePackageCompilerSha256> resultSha256(
    const std::optional<Data::RuntimePackageCompilerFinalizeResult> &result)
{
    return result ? std::optional(result->envelope.canonicalResult.sha256()) : std::nullopt;
}

std::optional<Data::RuntimePackageCompilerSha256> resultSha256(
    const std::optional<Data::RuntimePackageCompilerVerifyResult> &result)
{
    return result ? std::optional(result->envelope.canonicalResult.sha256()) : std::nullopt;
}

const RuntimePackageCompilerPreparationJournalEntry *findEntry(
    const RuntimePackageCompilerPreparationJournalState &state,
    const Data::RuntimePackageCompilerOperationId &operationId)
{
    const auto found
        = std::find_if(state.entries.cbegin(), state.entries.cend(), [&](const auto &entry) {
              return entry.compileOperationId == operationId;
          });
    return found == state.entries.cend() ? nullptr : &*found;
}

Record restartPlaceholder(const RuntimePackageCompilerPreparationJournalEntry &entry)
{
    Record record;
    record.compileOperationId = entry.compileOperationId;
    record.startRequestFingerprint = entry.startRequestFingerprint;
    record.verifyOperationId = entry.verifyOperationId;
    record.activationOperationId = entry.activationOperationId;
    record.compilerProviderId = entry.compilerProviderId;
    record.contractIdentity = entry.contractIdentity;
    record.revision = entry.revision;
    record.phase = entry.phase;
    if (Core::runtimePackageCompilerPreparationPhaseIsTerminal(entry.phase)) {
        record.terminalSummary = Core::RuntimePackageCompilerPreparationTerminalSummary{
            entry.compileResultSha256,
            entry.signRequestSha256,
            entry.finalizeResultSha256,
            entry.packageSha256,
            entry.verifyResultSha256,
        };
        record.detail = entry.detail.isEmpty()
                            ? QStringLiteral(
                                  "Terminal compiler preparation was restored after restart.")
                            : entry.detail;
    } else {
        record.phase = Phase::ReconciliationRequired;
        record.detail = entry.detail;
    }
    return record;
}

Data::RuntimePackageCompilerFinalizeRequest finalizeRequest(
    const StartRequest &start,
    const Data::RuntimePackageCompilerCompileResult &compileResult,
    const Data::RuntimePackageCompilerCanonicalJson &detachedResponse)
{
    if (!compileResult.signRequest || !compileResult.manifestSha256)
        return {};
    return {
        start.compileRequest.operationId,
        start.compileRequest.configurationId,
        start.compileRequest.contractIdentity,
        compileResult.envelope.requestSha256,
        compileResult.signRequest->sha256(),
        *compileResult.manifestSha256,
        start.compileRequest.targetProfile.signingKeyIdSha256,
        start.compileRequest.targetProfile.policyRevision,
        detachedResponse,
    };
}

Data::RuntimePackageCompilerVerifyRequest verifyRequest(
    const StartRequest &start, const Data::RuntimePackageCompilerFinalizeResult &finalizeResult)
{
    if (!finalizeResult.packageSha256)
        return {};
    return {
        start.verifyOperationId,
        start.compileRequest.contractIdentity,
        finalizeResult.packageBytes,
        *finalizeResult.packageSha256,
    };
}

} // namespace

class DurableRuntimePackageCompilerPreparationCoordinator::Private
{
public:
    enum class JobPurpose { Compile, ResumeQuery, Finalize, Verify };

    struct ActiveJob
    {
        QPointer<Core::RuntimePackageCompilerJob> job;
        quintptr identity = 0;
        JobPurpose purpose = JobPurpose::Compile;
    };

    struct OperationContext
    {
        QPointer<Core::RuntimePackageCompilerProvider> provider;
        std::optional<Data::RuntimePackageCompilerCanonicalJson> detachedSigningResponse;
        QMetaObject::Connection providerDestroyed;
        QMetaObject::Connection availabilityChanged;
    };

    struct CommittedTransition
    {
        Data::RuntimePackageCompilerOperationId compileOperationId;
        quint64 currentRevision = 0;
        quint64 snapshotSequence = 0;
        std::optional<Record> previous;
    };

    Private(
        DurableRuntimePackageCompilerPreparationCoordinator *q,
        Core::ProviderRegistry *providerRegistry,
        const Utils::FilePath &journalRoot,
        RuntimePackageCompilerCurrentProjectCapture capture)
        : q(q)
        , providerRegistry(providerRegistry)
        , journal(journalRoot)
        , currentProjectCapture(std::move(capture))
    {
        if (!providerRegistry || providerRegistry->thread() != q->thread()) {
            initializationError = QStringLiteral("Compiler provider registry is unavailable.");
            return;
        }
        if (!currentProjectCapture) {
            initializationError = QStringLiteral("Current project capture callback is unavailable.");
            return;
        }
        providerRemovalConnection = QObject::connect(
            providerRegistry,
            &Core::ProviderRegistry::providerAboutToBeRemoved,
            q,
            [this](Core::Provider *provider) {
                QStringList affected;
                for (auto it = contexts.cbegin(); it != contexts.cend(); ++it) {
                    if (it->provider == provider)
                        affected.append(it.key());
                }
                for (const QString &key : std::as_const(affected)) {
                    requireReconciliation(
                        key, QStringLiteral("Frozen compiler provider was unregistered."));
                }
            });
        const Utils::Result<RuntimePackageCompilerPreparationJournalState> initialized
            = journal.initialize();
        if (!initialized) {
            initializationError = initialized.error();
            return;
        }
        journalState = *initialized;

        // No typed request is reconstructed from the journal. Every persisted
        // operation is durably reduced to a minimal restart placeholder and no
        // provider job is started by construction.
        const QList<RuntimePackageCompilerPreparationJournalEntry> loaded = journalState.entries;
        for (RuntimePackageCompilerPreparationJournalEntry entry : loaded) {
            if (!Core::runtimePackageCompilerPreparationPhaseIsTerminal(entry.phase)
                && entry.phase != Phase::ReconciliationRequired) {
                const RuntimePackageCompilerPreparationJournalEntry previousEntry = entry;
                entry.revision++;
                entry.phase = Phase::ReconciliationRequired;
                entry.detail = QStringLiteral(
                    "Exact compiler preparation input is required after restart.");
                const Utils::Result<RuntimePackageCompilerPreparationJournalState> committed
                    = journal.commit(entry, journalState.sequence, previousEntry);
                if (!committed) {
                    initializationError = committed.error();
                    records.clear();
                    return;
                }
                journalState = *committed;
            }
        }
        for (const RuntimePackageCompilerPreparationJournalEntry &entry : journalState.entries) {
            Record record = restartPlaceholder(entry);
            if (!record.isValid()) {
                initializationError = QStringLiteral(
                    "Compiler preparation restart placeholder is invalid.");
                records.clear();
                return;
            }
            records.insert(entry.compileOperationId.value(), std::move(record));
        }
    }

    ~Private() { shutdown(); }

    bool isReady() const { return initializationError.isEmpty() && !shuttingDown; }

    Utils::Result<> validateReady() const
    {
        if (!initializationError.isEmpty())
            return Utils::ResultError(initializationError);
        if (shuttingDown)
            return Utils::ResultError(
                QStringLiteral("Compiler preparation coordinator is shutting down."));
        return Utils::ResultOk;
    }

    bool providerIsCurrent(
        const Record &record,
        const QPointer<Core::RuntimePackageCompilerProvider> &provider) const
    {
        return provider && providerRegistry && provider->thread() == q->thread()
               && provider->isAvailable() && providerId(provider) == record.compilerProviderId
               && providerRegistry->provider(provider->id()) == provider;
    }

    Snapshot snapshot() const
    {
        Snapshot result;
        result.sequence = journalState.sequence;
        result.records = records.values();
        std::sort(
            result.records.begin(),
            result.records.end(),
            [](const Record &left, const Record &right) {
                return left.compileOperationId.value() < right.compileOperationId.value();
            });
        return result;
    }

    RuntimePackageCompilerPreparationJournalEntry journalEntry(
        const Record &record,
        const std::optional<Data::RuntimePackageCompilerCanonicalJson> &responseOverride
        = std::nullopt) const
    {
        const RuntimePackageCompilerPreparationJournalEntry *previous
            = findEntry(journalState, record.compileOperationId);
        RuntimePackageCompilerPreparationJournalEntry entry;
        if (previous)
            entry = *previous;
        entry.compileOperationId = record.compileOperationId;
        entry.startRequestFingerprint = record.startRequestFingerprint;
        entry.verifyOperationId = record.verifyOperationId;
        entry.activationOperationId = record.activationOperationId;
        entry.compilerProviderId = record.compilerProviderId;
        entry.contractIdentity = record.contractIdentity;
        entry.revision = record.revision;
        entry.phase = record.phase;
        entry.detail = record.detail;
        if (record.startRequest) {
            entry.configurationId = record.startRequest->compileRequest.configurationId;
            entry.buildTimestampNs = record.startRequest->compileRequest.buildTimestampNs;
        }

        if (record.compileResult) {
            entry.compileResultSha256 = resultSha256(record.compileResult);
            entry.signRequestSha256 = record.compileResult->signRequest
                                          ? std::optional(
                                                record.compileResult->signRequest->sha256())
                                          : std::nullopt;
        }
        if (responseOverride)
            entry.detachedSigningResponse = responseOverride;
        if (record.finalizeResult) {
            entry.finalizeResultSha256 = resultSha256(record.finalizeResult);
            entry.packageSha256 = record.finalizeResult->packageSha256;
        }
        if (record.verifyResult)
            entry.verifyResultSha256 = resultSha256(record.verifyResult);
        return entry;
    }

    Utils::Result<> commitRecord(
        const std::optional<Record> &previous,
        const Record &current,
        const std::optional<Data::RuntimePackageCompilerCanonicalJson> &responseOverride
        = std::nullopt)
    {
        if (!current.isValid())
            return Utils::ResultError(QStringLiteral("Compiler preparation record is invalid."));
        std::optional<RuntimePackageCompilerPreparationJournalEntry> expectedEntry;
        const auto liveRecord = records.constFind(current.compileOperationId.value());
        const RuntimePackageCompilerPreparationJournalEntry *durableEntry
            = findEntry(journalState, current.compileOperationId);
        if (previous) {
            if (liveRecord == records.cend() || *liveRecord != *previous || !durableEntry) {
                return Utils::ResultError(
                    QStringLiteral("Compiler preparation predecessor is not current."));
            }
            expectedEntry = *durableEntry;
        } else if (liveRecord != records.cend() || durableEntry) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation OperationId is already reserved."));
        }
        const RuntimePackageCompilerPreparationJournalEntry entry
            = journalEntry(current, responseOverride);
        const Utils::Result<RuntimePackageCompilerPreparationJournalState> committed
            = journal.commit(entry, journalState.sequence, expectedEntry);
        if (!committed)
            return Utils::ResultError(committed.error());
        journalState = *committed;
        committedTransition = CommittedTransition{
            current.compileOperationId,
            current.revision,
            journalState.sequence,
            previous,
        };
        records.insert(current.compileOperationId.value(), current);
        const Snapshot currentSnapshot = snapshot();
        return q->publishRecordTransition(current, currentSnapshot);
    }

    void bindProvider(
        const Data::RuntimePackageCompilerOperationId &operationId,
        Core::RuntimePackageCompilerProvider *provider)
    {
        const QString key = operationId.value();
        unbindProvider(key);
        OperationContext context;
        context.provider = provider;
        if (const RuntimePackageCompilerPreparationJournalEntry *entry
            = findEntry(journalState, operationId)) {
            context.detachedSigningResponse = entry->detachedSigningResponse;
        }
        context.providerDestroyed = QObject::connect(provider, &QObject::destroyed, q, [this, key] {
            requireReconciliation(key, QStringLiteral("Frozen compiler provider disappeared."));
        });
        context.availabilityChanged = QObject::connect(
            provider, &Core::Provider::availabilityChanged, q, [this, key](bool available) {
                if (!available)
                    requireReconciliation(
                        key, QStringLiteral("Frozen compiler provider became unavailable."));
            });
        contexts.insert(key, std::move(context));
    }

    void unbindProvider(const QString &key)
    {
        auto found = contexts.find(key);
        if (found == contexts.end())
            return;
        QObject::disconnect(found->providerDestroyed);
        QObject::disconnect(found->availabilityChanged);
        contexts.erase(found);
    }

    void finishContextIfTerminal(const QString &key)
    {
        const auto found = records.constFind(key);
        if (found != records.cend()
            && Core::runtimePackageCompilerPreparationPhaseIsTerminal(found->phase)) {
            unbindProvider(key);
        }
    }

    std::optional<Record> currentRecord(const QString &key) const
    {
        const auto found = records.constFind(key);
        return found == records.cend() ? std::nullopt : std::optional(*found);
    }

    bool matchesFrozenCompileResult(
        const Data::RuntimePackageCompilerOperationId &operationId,
        const Data::RuntimePackageCompilerCompileResult &result) const
    {
        const RuntimePackageCompilerPreparationJournalEntry *entry
            = findEntry(journalState, operationId);
        if (!entry)
            return true;
        if (entry->compileResultSha256
            && *entry->compileResultSha256 != result.envelope.canonicalResult.sha256()) {
            return false;
        }
        return !entry->signRequestSha256
               || (result.signRequest && *entry->signRequestSha256 == result.signRequest->sha256());
    }

    bool matchesFrozenFinalizeResult(
        const Data::RuntimePackageCompilerOperationId &operationId,
        const Data::RuntimePackageCompilerFinalizeResult &result) const
    {
        const RuntimePackageCompilerPreparationJournalEntry *entry
            = findEntry(journalState, operationId);
        if (!entry)
            return true;
        if (entry->finalizeResultSha256
            && *entry->finalizeResultSha256 != result.envelope.canonicalResult.sha256()) {
            return false;
        }
        return !entry->packageSha256
               || (result.packageSha256 && *entry->packageSha256 == *result.packageSha256);
    }

    bool matchesFrozenVerifyResult(
        const Data::RuntimePackageCompilerOperationId &operationId,
        const Data::RuntimePackageCompilerVerifyResult &result) const
    {
        const RuntimePackageCompilerPreparationJournalEntry *entry
            = findEntry(journalState, operationId);
        return !entry || !entry->verifyResultSha256
               || *entry->verifyResultSha256 == result.envelope.canonicalResult.sha256();
    }

    Utils::Result<> transitionFailure(
        const QString &key,
        QString detail,
        std::optional<Data::RuntimePackageCompilerCompileResult> compile = std::nullopt,
        std::optional<Data::RuntimePackageCompilerFinalizeResult> finalize = std::nullopt,
        std::optional<Data::RuntimePackageCompilerVerifyResult> verify = std::nullopt)
    {
        const std::optional<Record> previous = currentRecord(key);
        if (!previous || Core::runtimePackageCompilerPreparationPhaseIsTerminal(previous->phase)
            || previous->phase == Phase::ReconciliationRequired) {
            return Utils::ResultOk;
        }
        Record failed = *previous;
        failed.revision++;
        failed.phase = Phase::Failed;
        if (compile)
            failed.compileResult = std::move(compile);
        if (finalize)
            failed.finalizeResult = std::move(finalize);
        if (verify)
            failed.verifyResult = std::move(verify);
        failed.preparation.reset();
        failed.detail = std::move(detail);
        const Utils::Result<> committed = commitRecord(previous, failed);
        finishContextIfTerminal(key);
        return committed;
    }

    Utils::Result<> transitionCanceled(
        const QString &key,
        std::optional<Data::RuntimePackageCompilerCompileResult> compile = std::nullopt,
        std::optional<Data::RuntimePackageCompilerFinalizeResult> finalize = std::nullopt,
        std::optional<Data::RuntimePackageCompilerVerifyResult> verify = std::nullopt)
    {
        const std::optional<Record> previous = currentRecord(key);
        if (!previous || Core::runtimePackageCompilerPreparationPhaseIsTerminal(previous->phase)
            || previous->phase == Phase::ReconciliationRequired) {
            return Utils::ResultOk;
        }
        Record canceled = *previous;
        canceled.revision++;
        canceled.phase = Phase::Canceled;
        if (compile)
            canceled.compileResult = std::move(compile);
        if (finalize)
            canceled.finalizeResult = std::move(finalize);
        if (verify)
            canceled.verifyResult = std::move(verify);
        canceled.preparation.reset();
        canceled.detail = QStringLiteral("Compiler preparation was canceled.");
        const Utils::Result<> committed = commitRecord(previous, canceled);
        finishContextIfTerminal(key);
        return committed;
    }

    void requireReconciliation(const QString &key, QString detail)
    {
        if (shuttingDown && records.isEmpty())
            return;
        const std::optional<Record> previous = currentRecord(key);
        if (!previous || Core::runtimePackageCompilerPreparationPhaseIsTerminal(previous->phase)
            || previous->phase == Phase::ReconciliationRequired) {
            return;
        }
        QPointer<Core::RuntimePackageCompilerJob> job;
        const auto active = activeJobs.find(key);
        if (active != activeJobs.end()) {
            job = active->job;
            activeJobs.erase(active);
        }
        Record reconciliation = *previous;
        reconciliation.revision++;
        reconciliation.phase = Phase::ReconciliationRequired;
        reconciliation.preparation.reset();
        reconciliation.detail = std::move(detail);
        const Utils::Result<> committed = commitRecord(previous, reconciliation);
        if (!committed && initializationError.isEmpty())
            initializationError = committed.error();
        unbindProvider(key);
        if (job)
            job->cancel();
    }

    void retainError(const Utils::Result<> &result)
    {
        if (!result && initializationError.isEmpty())
            initializationError = result.error();
    }

    Utils::Result<> attachJob(
        const QString &key, Core::RuntimePackageCompilerJob *job, JobPurpose purpose)
    {
        if (!job || job->thread() != q->thread() || activeJobs.contains(key)) {
            return Utils::ResultError(
                QStringLiteral("Compiler provider returned an incompatible job."));
        }
        const quintptr identity = reinterpret_cast<quintptr>(job);
        activeJobs.insert(key, {job, identity, purpose});
        QObject::connect(
            job,
            &Core::RuntimePackageCompilerJob::finished,
            q,
            [this, key, identity](const Data::RuntimePackageCompilerJobResult &result) {
                handleJobResult(key, identity, result);
            });
        QObject::connect(
            job,
            &Core::RuntimePackageCompilerJob::completionFailed,
            q,
            [this, key, identity](Core::RuntimePackageCompilerJobCompletionError) {
                handleJobCompletionFailure(key, identity);
            });
        QObject::connect(job, &QObject::destroyed, q, [this, key, identity] {
            const auto active = activeJobs.constFind(key);
            if (active != activeJobs.cend() && active->identity == identity) {
                activeJobs.erase(active);
                requireReconciliation(
                    key, QStringLiteral("Compiler job disappeared before trusted completion."));
            }
        });
        if (job->state() == Core::RuntimePackageCompilerJobState::Finished) {
            if (job->result())
                handleJobResult(key, identity, *job->result());
            else
                handleJobCompletionFailure(key, identity);
        }
        return Utils::ResultOk;
    }

    Utils::Result<> scheduleCompile(
        const QString &key, Core::RuntimePackageCompilerProvider *provider)
    {
        const std::optional<Record> record = currentRecord(key);
        if (!record || record->phase != Phase::Compiling || !record->startRequest)
            return Utils::ResultOk;
        const Utils::Result<Core::RuntimePackageCompilerJob *> scheduled = provider->compile(
            record->startRequest->compileRequest);
        if (!scheduled) {
            return transitionFailure(
                key, QStringLiteral("Compiler preparation could not schedule compilation."));
        }
        const Utils::Result<> attached = attachJob(key, *scheduled, JobPurpose::Compile);
        if (!attached) {
            (*scheduled)->cancel();
            requireReconciliation(key, attached.error());
        }
        return Utils::ResultOk;
    }

    Utils::Result<> scheduleResumeQuery(
        const QString &key, Core::RuntimePackageCompilerProvider *provider)
    {
        const std::optional<Record> record = currentRecord(key);
        if (!record || record->phase != Phase::Compiling || !record->startRequest)
            return Utils::ResultOk;
        const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonical
            = Core::encodeRuntimePackageCompilerCompileRequest(record->startRequest->compileRequest);
        if (!canonical) {
            requireReconciliation(key, canonical.error());
            return Utils::ResultOk;
        }
        const Data::RuntimePackageCompilerQueryRequest request{
            record->compileOperationId,
            record->contractIdentity,
            canonical->sha256(),
        };
        const Utils::Result<Core::RuntimePackageCompilerJob *> scheduled = provider->query(request);
        if (!scheduled) {
            requireReconciliation(key, QStringLiteral("Compiler operation could not be reconciled."));
            return Utils::ResultOk;
        }
        const Utils::Result<> attached = attachJob(key, *scheduled, JobPurpose::ResumeQuery);
        if (!attached) {
            (*scheduled)->cancel();
            requireReconciliation(key, attached.error());
        }
        return Utils::ResultOk;
    }

    Utils::Result<> scheduleFinalize(
        const QString &key, Core::RuntimePackageCompilerProvider *provider)
    {
        const std::optional<Record> record = currentRecord(key);
        if (!record || record->phase != Phase::Finalizing || !record->startRequest
            || !record->compileResult || !record->finalizeRequest
            || !record->finalizeRequestSha256) {
            return Utils::ResultError(
                QStringLiteral("Compiler finalization evidence is incomplete."));
        }
        const Utils::Result<Core::RuntimePackageCompilerJob *> scheduled = provider->finalize(
            *record->finalizeRequest);
        if (!scheduled) {
            return transitionFailure(
                key, QStringLiteral("Compiler preparation could not schedule finalization."));
        }
        const Utils::Result<> attached = attachJob(key, *scheduled, JobPurpose::Finalize);
        if (!attached) {
            (*scheduled)->cancel();
            requireReconciliation(key, attached.error());
        }
        return Utils::ResultOk;
    }

    Utils::Result<> scheduleVerify(const QString &key, Core::RuntimePackageCompilerProvider *provider)
    {
        const std::optional<Record> record = currentRecord(key);
        if (!record || record->phase != Phase::Verifying || !record->startRequest
            || !record->finalizeResult || !record->verifyRequest || !record->verifyRequestSha256) {
            return Utils::ResultError(
                QStringLiteral("Compiler verification evidence is incomplete."));
        }
        const Utils::Result<Core::RuntimePackageCompilerJob *> scheduled = provider->verify(
            *record->verifyRequest);
        if (!scheduled) {
            return transitionFailure(
                key, QStringLiteral("Compiler preparation could not schedule verification."));
        }
        const Utils::Result<> attached = attachJob(key, *scheduled, JobPurpose::Verify);
        if (!attached) {
            (*scheduled)->cancel();
            requireReconciliation(key, attached.error());
        }
        return Utils::ResultOk;
    }

    void handleJobCompletionFailure(const QString &key, quintptr identity)
    {
        const auto active = activeJobs.constFind(key);
        if (active == activeJobs.cend() || active->identity != identity)
            return;
        activeJobs.erase(active);
        requireReconciliation(
            key, QStringLiteral("Compiler job ended without a trusted terminal result."));
    }

    void handleJobResult(
        const QString &key, quintptr identity, const Data::RuntimePackageCompilerJobResult &result)
    {
        const auto active = activeJobs.find(key);
        if (active == activeJobs.end() || active->identity != identity)
            return;
        const JobPurpose purpose = active->purpose;
        activeJobs.erase(active);
        if (!result.isValid()) {
            requireReconciliation(key, QStringLiteral("Compiler terminal result is invalid."));
            return;
        }
        switch (purpose) {
        case JobPurpose::Compile:
            handleCompileResult(key, result);
            break;
        case JobPurpose::ResumeQuery:
            handleQueryResult(key, result);
            break;
        case JobPurpose::Finalize:
            handleFinalizeResult(key, result);
            break;
        case JobPurpose::Verify:
            handleVerifyResult(key, result);
            break;
        }
    }

    void handleCompileResult(
        const QString &key, const Data::RuntimePackageCompilerJobResult &jobResult)
    {
        const auto *result = std::get_if<Data::RuntimePackageCompilerCompileResult>(
            &jobResult.value());
        const std::optional<Record> previous = currentRecord(key);
        if (!result || !previous
            || (previous->phase != Phase::Compiling && previous->phase != Phase::CancelRequested)) {
            requireReconciliation(
                key, QStringLiteral("Compiler compile result mismatched its phase."));
            return;
        }
        if (!matchesFrozenCompileResult(previous->compileOperationId, *result)) {
            requireReconciliation(
                key, QStringLiteral("Compiler replay changed its durable compile evidence."));
            return;
        }
        if (!result->isSuccess()) {
            const QString detail
                = terminalDetail(result->envelope.status, QStringLiteral("compile"));
            const Utils::Result<> transitioned
                = previous->phase == Phase::CancelRequested
                          || result->envelope.status
                                 == Data::RuntimePackageCompilerResultStatus::Canceled
                      ? transitionCanceled(key, *result)
                      : transitionFailure(key, detail, *result);
            retainError(transitioned);
            return;
        }
        if (previous->phase == Phase::CancelRequested) {
            retainError(transitionCanceled(key, *result));
            return;
        }
        Record awaiting = *previous;
        awaiting.revision++;
        awaiting.phase = Phase::AwaitingDetachedSignature;
        awaiting.compileResult = *result;
        awaiting.detachedSigningRequest = *result->signRequest;
        awaiting.detail.clear();
        if (const Utils::Result<> committed = commitRecord(previous, awaiting); !committed) {
            requireReconciliation(key, committed.error());
            return;
        }

        const std::optional<Record> current = currentRecord(key);
        if (!current || current->phase != Phase::AwaitingDetachedSignature)
            return;
        if (const Utils::Result<> published = q->publishDetachedSigningRequest(*current);
            !published) {
            requireReconciliation(key, published.error());
            return;
        }
        const std::optional<Record> afterSignal = currentRecord(key);
        if (!afterSignal || afterSignal->phase != Phase::AwaitingDetachedSignature)
            return;
        const auto context = contexts.find(key);
        if (context != contexts.end() && context->detachedSigningResponse) {
            const Utils::Result<> finalized
                = beginFinalize(*afterSignal, *context->detachedSigningResponse, context->provider);
            if (!finalized)
                retainError(finalized);
        }
    }

    void handleQueryResult(const QString &key, const Data::RuntimePackageCompilerJobResult &jobResult)
    {
        const auto *result = std::get_if<Data::RuntimePackageCompilerQueryResult>(
            &jobResult.value());
        const std::optional<Record> record = currentRecord(key);
        if (record && record->phase == Phase::CancelRequested) {
            retainError(transitionCanceled(key));
            return;
        }
        const auto context = contexts.constFind(key);
        if (!result || !result->isSuccess() || !result->hasCompilerRecord()
            || context == contexts.cend() || !context->provider) {
            requireReconciliation(
                key, QStringLiteral("Compiler operation has no trusted replay record."));
            return;
        }
        if (result->hasSignerResponse()) {
            if (!context->detachedSigningResponse
                || *result->signerResponse != *context->detachedSigningResponse) {
                requireReconciliation(
                    key,
                    QStringLiteral("Compiler signer response differs from the durable journal."));
                return;
            }
        }
        const Utils::Result<> scheduled = scheduleCompile(key, context->provider);
        if (!scheduled) {
            requireReconciliation(key, scheduled.error());
            retainError(scheduled);
        }
    }

    void handleFinalizeResult(
        const QString &key, const Data::RuntimePackageCompilerJobResult &jobResult)
    {
        const auto *result = std::get_if<Data::RuntimePackageCompilerFinalizeResult>(
            &jobResult.value());
        const std::optional<Record> previous = currentRecord(key);
        if (!result || !previous
            || (previous->phase != Phase::Finalizing && previous->phase != Phase::CancelRequested)) {
            requireReconciliation(
                key, QStringLiteral("Compiler finalize result mismatched its phase."));
            return;
        }
        if (!matchesFrozenFinalizeResult(previous->compileOperationId, *result)) {
            requireReconciliation(
                key, QStringLiteral("Compiler replay changed its durable finalize evidence."));
            return;
        }
        if (!result->isSuccess()) {
            const QString detail
                = terminalDetail(result->envelope.status, QStringLiteral("finalize"));
            if (previous->phase == Phase::CancelRequested
                || result->envelope.status == Data::RuntimePackageCompilerResultStatus::Canceled) {
                retainError(transitionCanceled(key, std::nullopt, *result));
            } else {
                retainError(transitionFailure(key, detail, std::nullopt, *result));
            }
            return;
        }
        if (previous->phase == Phase::CancelRequested) {
            retainError(transitionCanceled(key, std::nullopt, *result));
            return;
        }
        const Data::RuntimePackageCompilerVerifyRequest request
            = verifyRequest(*previous->startRequest, *result);
        const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalRequest
            = Core::encodeRuntimePackageCompilerVerifyRequest(request);
        if (!request.isValid() || !canonicalRequest) {
            retainError(transitionFailure(
                key, QStringLiteral("Finalized package cannot be verified independently.")));
            return;
        }
        Record verifying = *previous;
        verifying.revision++;
        verifying.phase = Phase::Verifying;
        verifying.finalizeResult = *result;
        verifying.verifyRequest = request;
        verifying.verifyRequestSha256 = canonicalRequest->sha256();
        verifying.detail.clear();
        if (const Utils::Result<> committed = commitRecord(previous, verifying); !committed) {
            requireReconciliation(key, committed.error());
            return;
        }
        const std::optional<Record> current = currentRecord(key);
        if (!current || current->phase != Phase::Verifying)
            return;
        const auto context = contexts.constFind(key);
        if (context == contexts.cend() || !context->provider) {
            requireReconciliation(key, QStringLiteral("Frozen compiler provider disappeared."));
            return;
        }
        const Utils::Result<> scheduled = scheduleVerify(key, context->provider);
        if (!scheduled) {
            requireReconciliation(key, scheduled.error());
            retainError(scheduled);
        }
    }

    void handleVerifyResult(
        const QString &key, const Data::RuntimePackageCompilerJobResult &jobResult)
    {
        const auto *result = std::get_if<Data::RuntimePackageCompilerVerifyResult>(
            &jobResult.value());
        const std::optional<Record> previous = currentRecord(key);
        if (!result || !previous
            || (previous->phase != Phase::Verifying && previous->phase != Phase::CancelRequested)) {
            requireReconciliation(key, QStringLiteral("Compiler verify result mismatched its phase."));
            return;
        }
        if (!matchesFrozenVerifyResult(previous->compileOperationId, *result)) {
            requireReconciliation(
                key, QStringLiteral("Compiler replay changed its durable verify evidence."));
            return;
        }
        if (!result->isSuccess()) {
            const QString detail = terminalDetail(result->envelope.status, QStringLiteral("verify"));
            if (previous->phase == Phase::CancelRequested
                || result->envelope.status == Data::RuntimePackageCompilerResultStatus::Canceled) {
                retainError(transitionCanceled(key, std::nullopt, std::nullopt, *result));
            } else {
                retainError(transitionFailure(key, detail, std::nullopt, std::nullopt, *result));
            }
            return;
        }
        if (previous->phase == Phase::CancelRequested) {
            retainError(transitionCanceled(key, std::nullopt, std::nullopt, *result));
            return;
        }
        Record assembling = *previous;
        assembling.revision++;
        assembling.phase = Phase::AssemblingProof;
        assembling.verifyResult = *result;
        assembling.detail.clear();
        if (const Utils::Result<> committed = commitRecord(previous, assembling); !committed) {
            requireReconciliation(key, committed.error());
            return;
        }
        assembleProof(key);
    }

    void assembleProof(const QString &key)
    {
        const std::optional<Record> previous = currentRecord(key);
        const auto context = contexts.constFind(key);
        if (!previous || previous->phase != Phase::AssemblingProof || !previous->startRequest
            || !previous->compileResult || !previous->finalizeRequest
            || !previous->finalizeRequestSha256 || !previous->finalizeResult
            || !previous->verifyRequest || !previous->verifyRequestSha256 || !previous->verifyResult
            || context == contexts.cend() || !context->provider) {
            requireReconciliation(key, QStringLiteral("Compiler proof evidence is incomplete."));
            return;
        }
        const Core::RuntimePackageCompilerActivationProofAssemblyRequest assembly{
            previous->compilerProviderId,
            previous->contractIdentity,
            previous->startRequest->compileRequest,
            *previous->compileResult,
            *previous->finalizeRequest,
            *previous->finalizeResult,
            *previous->verifyRequest,
            *previous->verifyResult,
        };
        const Utils::Result<Data::RuntimePackageCompilerActivationProof> proof
            = context->provider->assembleActivationProof(assembly);
        if (!proof) {
            retainError(transitionFailure(
                key, QStringLiteral("Compiler provider rejected activation proof assembly.")));
            return;
        }

        const Data::NodeId projectId
            = previous->startRequest->compileRequest.topologyEvidence.scope.projectId;
        const Utils::Result<Data::RuntimePackageActivationProjectCapture> currentCapture
            = currentProjectCapture(projectId);
        if (!currentCapture || !currentCapture->isValid()) {
            retainError(
                transitionFailure(key, QStringLiteral("Current project capture is unavailable.")));
            return;
        }
        const Data::RuntimePackageCompilerProjectSnapshotEvidence currentEvidence{*currentCapture};
        if (currentEvidence != previous->startRequest->compileRequest.projectSnapshotEvidence) {
            retainError(transitionFailure(
                key, QStringLiteral("Project changed while the package was prepared.")));
            return;
        }

        Core::RuntimePackageActivationPreparationRequest preparation{
            previous->startRequest->activationOperationId,
            previous->startRequest->compileRequest.topologyEvidence.scope,
            proof->finalizeResult.packageBytes,
            proof->compiledProjectSource,
            proof->effectiveProjectCompanion,
            proof->verifyResult,
            previous->startRequest->rollbackOnActivationFailure,
            *proof,
        };
        if (!preparation.isValid()) {
            retainError(
                transitionFailure(key, QStringLiteral("Compiler activation preparation is invalid.")));
            return;
        }
        Record ready = *previous;
        ready.revision++;
        ready.phase = Phase::Ready;
        ready.preparation = std::move(preparation);
        ready.detail.clear();
        if (const Utils::Result<> committed = commitRecord(previous, ready); !committed) {
            requireReconciliation(key, committed.error());
            return;
        }
        const std::optional<Record> current = currentRecord(key);
        if (current && current->phase == Phase::Ready) {
            const Utils::Result<> published = q->publishPreparationReady(*current);
            retainError(published);
        }
        finishContextIfTerminal(key);
    }

    Utils::Result<> beginFinalize(
        const Record &record,
        const Data::RuntimePackageCompilerCanonicalJson &response,
        Core::RuntimePackageCompilerProvider *provider)
    {
        const QPointer<Core::RuntimePackageCompilerProvider> providerGuard(provider);
        if (record.phase != Phase::AwaitingDetachedSignature || !record.startRequest
            || !record.compileResult) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation is not awaiting signing."));
        }
        const Data::RuntimePackageCompilerFinalizeRequest request
            = finalizeRequest(*record.startRequest, *record.compileResult, response);
        if (!request.isValid()) {
            return Utils::ResultError(
                QStringLiteral("Detached signing response does not match the compile result."));
        }
        const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalRequest
            = Core::encodeRuntimePackageCompilerFinalizeRequest(request);
        if (!canonicalRequest)
            return Utils::ResultError(canonicalRequest.error());
        auto context = contexts.find(record.compileOperationId.value());
        if (context == contexts.end() || context->provider != providerGuard
            || !providerIsCurrent(record, providerGuard)) {
            requireReconciliation(
                record.compileOperationId.value(),
                QStringLiteral("Frozen compiler provider is unavailable before finalization."));
            return Utils::ResultError(QStringLiteral("Frozen compiler provider is unavailable."));
        }
        context->detachedSigningResponse = response;
        Record finalizing = record;
        finalizing.revision++;
        finalizing.phase = Phase::Finalizing;
        finalizing.finalizeRequest = request;
        finalizing.finalizeRequestSha256 = canonicalRequest->sha256();
        finalizing.detail.clear();
        const Utils::Result<> committed = commitRecord(record, finalizing, response);
        if (!committed)
            return Utils::ResultError(committed.error());
        const std::optional<Record> current = currentRecord(record.compileOperationId.value());
        context = contexts.find(record.compileOperationId.value());
        if (!current || current->phase != Phase::Finalizing || context == contexts.end()
            || context->provider != providerGuard
            || !providerIsCurrent(*current, context->provider)) {
            requireReconciliation(
                record.compileOperationId.value(),
                QStringLiteral("Compiler finalization lost its frozen provider context."));
            return Utils::ResultOk;
        }
        const Utils::Result<> scheduled = scheduleFinalize(
            record.compileOperationId.value(), context->provider.data());
        if (!scheduled)
            requireReconciliation(record.compileOperationId.value(), scheduled.error());
        return scheduled;
    }

    void shutdown()
    {
        if (shuttingDown)
            return;
        shuttingDown = true;
        const QList<ActiveJob> jobs = activeJobs.values();
        const QStringList keys = records.keys();
        for (const QString &key : keys) {
            const std::optional<Record> record = currentRecord(key);
            if (!record || Core::runtimePackageCompilerPreparationPhaseIsTerminal(record->phase)
                || record->phase == Phase::ReconciliationRequired) {
                continue;
            }
            requireReconciliation(
                key, QStringLiteral("Compiler preparation stopped before trusted completion."));
        }
        activeJobs.clear();
        for (const ActiveJob &active : jobs) {
            if (active.job)
                active.job->cancel();
        }
        const QStringList contextKeys = contexts.keys();
        for (const QString &key : contextKeys)
            unbindProvider(key);
    }

    DurableRuntimePackageCompilerPreparationCoordinator *q;
    QPointer<Core::ProviderRegistry> providerRegistry;
    QMetaObject::Connection providerRemovalConnection;
    RuntimePackageCompilerPreparationJournal journal;
    RuntimePackageCompilerCurrentProjectCapture currentProjectCapture;
    RuntimePackageCompilerPreparationJournalState journalState;
    QHash<QString, Record> records;
    QHash<QString, OperationContext> contexts;
    QHash<QString, ActiveJob> activeJobs;
    std::optional<CommittedTransition> committedTransition;
    QString initializationError;
    bool shuttingDown = false;
};

DurableRuntimePackageCompilerPreparationCoordinator::DurableRuntimePackageCompilerPreparationCoordinator(
    Core::ProviderRegistry *providerRegistry,
    const Utils::FilePath &journalRoot,
    RuntimePackageCompilerCurrentProjectCapture currentProjectCapture,
    QObject *parent)
    : Core::RuntimePackageCompilerPreparationCoordinator(providerRegistry, parent)
    , d(std::make_unique<Private>(
          this, providerRegistry, journalRoot, std::move(currentProjectCapture)))
{}

DurableRuntimePackageCompilerPreparationCoordinator::
    ~DurableRuntimePackageCompilerPreparationCoordinator()
{
    shutdown();
}

QString DurableRuntimePackageCompilerPreparationCoordinator::initializationError() const
{
    return d->initializationError;
}

void DurableRuntimePackageCompilerPreparationCoordinator::shutdown()
{
    d->shutdown();
}

Utils::Result<Disposition> DurableRuntimePackageCompilerPreparationCoordinator::doStart(
    const StartRequest &request, Core::RuntimePackageCompilerProvider *frozenProvider)
{
    const QPointer<Core::RuntimePackageCompilerProvider> providerGuard(frozenProvider);
    if (const Utils::Result<> ready = d->validateReady(); !ready)
        return Utils::ResultError(ready.error());
    const Utils::Result<Data::RuntimePackageCompilerSha256> fingerprint
        = Core::runtimePackageCompilerPreparationStartRequestFingerprint(request);
    if (!fingerprint)
        return Utils::ResultError(fingerprint.error());
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalCompileRequest
        = Core::encodeRuntimePackageCompilerCompileRequest(request.compileRequest);
    if (!canonicalCompileRequest)
        return Utils::ResultError(canonicalCompileRequest.error());
    Record reserved;
    reserved.compileOperationId = request.compileRequest.operationId;
    reserved.startRequestFingerprint = *fingerprint;
    reserved.verifyOperationId = request.verifyOperationId;
    reserved.activationOperationId = request.activationOperationId;
    reserved.startRequest = request;
    reserved.compileRequestSha256 = canonicalCompileRequest->sha256();
    reserved.compilerProviderId = providerId(frozenProvider);
    reserved.contractIdentity = request.compileRequest.contractIdentity;
    reserved.revision = 1;
    reserved.phase = Phase::Reserved;
    if (const Utils::Result<> committed = d->commitRecord(std::nullopt, reserved); !committed)
        return Utils::ResultError(committed.error());

    const std::optional<Record> afterReserved = d->currentRecord(
        request.compileRequest.operationId.value());
    if (!afterReserved || afterReserved->phase != Phase::Reserved)
        return Disposition::Started;
    if (!d->providerIsCurrent(*afterReserved, providerGuard)) {
        d->requireReconciliation(
            request.compileRequest.operationId.value(),
            QStringLiteral("Frozen compiler provider disappeared before compilation."));
        return Disposition::Started;
    }
    d->bindProvider(request.compileRequest.operationId, providerGuard.data());
    Record compiling = *afterReserved;
    compiling.revision++;
    compiling.phase = Phase::Compiling;
    if (const Utils::Result<> committed = d->commitRecord(afterReserved, compiling); !committed) {
        d->requireReconciliation(request.compileRequest.operationId.value(), committed.error());
        return Disposition::Started;
    }
    const std::optional<Record> current = d->currentRecord(
        request.compileRequest.operationId.value());
    if (!current || current->phase != Phase::Compiling)
        return Disposition::Started;
    if (!d->providerIsCurrent(*current, providerGuard)) {
        d->requireReconciliation(
            request.compileRequest.operationId.value(),
            QStringLiteral("Frozen compiler provider disappeared before compilation."));
        return Disposition::Started;
    }
    const Utils::Result<> scheduled = d->scheduleCompile(
        request.compileRequest.operationId.value(), providerGuard.data());
    if (!scheduled) {
        d->requireReconciliation(request.compileRequest.operationId.value(), scheduled.error());
        d->retainError(scheduled);
    }
    return Disposition::Started;
}

Utils::Result<Disposition>
DurableRuntimePackageCompilerPreparationCoordinator::doSubmitDetachedSigningResponse(
    const Record &record,
    const Data::RuntimePackageCompilerCanonicalJson &detachedSigningResponse,
    Core::RuntimePackageCompilerProvider *frozenProvider)
{
    if (const Utils::Result<> ready = d->validateReady(); !ready)
        return Utils::ResultError(ready.error());
    const Utils::Result<> started
        = d->beginFinalize(record, detachedSigningResponse, frozenProvider);
    if (!started)
        return Utils::ResultError(started.error());
    return Disposition::Accepted;
}

Utils::Result<Disposition> DurableRuntimePackageCompilerPreparationCoordinator::doCancel(
    const Record &record)
{
    if (const Utils::Result<> ready = d->validateReady(); !ready)
        return Utils::ResultError(ready.error());
    const QString key = record.compileOperationId.value();
    const auto active = d->activeJobs.find(key);
    if (active == d->activeJobs.end()) {
        if (record.phase == Phase::Reserved || record.phase == Phase::AwaitingDetachedSignature) {
            const Utils::Result<> canceled = d->transitionCanceled(key);
            if (!canceled)
                return Utils::ResultError(canceled.error());
            return Disposition::Accepted;
        }
        Record requested = record;
        requested.revision++;
        requested.phase = Phase::CancelRequested;
        requested.preparation.reset();
        requested.detail.clear();
        if (const Utils::Result<> committed = d->commitRecord(record, requested); !committed)
            return Utils::ResultError(committed.error());
        const Utils::Result<> canceled = d->transitionCanceled(key);
        if (!canceled)
            return Utils::ResultError(canceled.error());
        return Disposition::Accepted;
    }
    const QPointer<Core::RuntimePackageCompilerJob> job = active->job;

    Record requested = record;
    requested.revision++;
    requested.phase = Phase::CancelRequested;
    requested.preparation.reset();
    requested.detail.clear();
    if (const Utils::Result<> committed = d->commitRecord(record, requested); !committed)
        return Utils::ResultError(committed.error());
    if (job)
        job->cancel();
    else
        d->requireReconciliation(key, QStringLiteral("Compiler job disappeared during cancellation."));
    return Disposition::Accepted;
}

Utils::Result<Disposition> DurableRuntimePackageCompilerPreparationCoordinator::doResume(
    const Record &record,
    const StartRequest &exactOriginalRequest,
    Core::RuntimePackageCompilerProvider *frozenProvider)
{
    const QPointer<Core::RuntimePackageCompilerProvider> providerGuard(frozenProvider);
    if (const Utils::Result<> ready = d->validateReady(); !ready)
        return Utils::ResultError(ready.error());
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalCompileRequest
        = Core::encodeRuntimePackageCompilerCompileRequest(exactOriginalRequest.compileRequest);
    if (!canonicalCompileRequest)
        return Utils::ResultError(canonicalCompileRequest.error());
    Record compiling = record;
    compiling.revision++;
    compiling.phase = Phase::Compiling;
    compiling.startRequest = exactOriginalRequest;
    compiling.compileRequestSha256 = canonicalCompileRequest->sha256();
    compiling.compileResult.reset();
    compiling.detachedSigningRequest.reset();
    compiling.finalizeRequest.reset();
    compiling.finalizeRequestSha256.reset();
    compiling.finalizeResult.reset();
    compiling.verifyRequest.reset();
    compiling.verifyRequestSha256.reset();
    compiling.verifyResult.reset();
    compiling.preparation.reset();
    compiling.terminalSummary.reset();
    compiling.detail.clear();
    if (const Utils::Result<> committed = d->commitRecord(record, compiling); !committed)
        return Utils::ResultError(committed.error());
    const std::optional<Record> current = d->currentRecord(record.compileOperationId.value());
    if (!current || current->phase != Phase::Compiling)
        return Disposition::Accepted;
    if (!d->providerIsCurrent(*current, providerGuard)) {
        d->requireReconciliation(
            record.compileOperationId.value(),
            QStringLiteral("Frozen compiler provider disappeared before reconciliation."));
        return Disposition::Accepted;
    }
    d->bindProvider(record.compileOperationId, providerGuard.data());
    const Utils::Result<> scheduled = d->scheduleResumeQuery(
        record.compileOperationId.value(), providerGuard.data());
    if (!scheduled) {
        d->requireReconciliation(record.compileOperationId.value(), scheduled.error());
        d->retainError(scheduled);
    }
    return Disposition::Accepted;
}

Utils::Result<std::optional<Record>> DurableRuntimePackageCompilerPreparationCoordinator::doRecord(
    const Data::RuntimePackageCompilerOperationId &compileOperationId) const
{
    if (!d->initializationError.isEmpty())
        return Utils::ResultError(d->initializationError);
    const auto found = d->records.constFind(compileOperationId.value());
    return found == d->records.cend() ? std::optional<Record>{} : std::optional<Record>{*found};
}

Utils::Result<Snapshot> DurableRuntimePackageCompilerPreparationCoordinator::doSnapshot() const
{
    if (!d->initializationError.isEmpty())
        return Utils::ResultError(d->initializationError);
    const Snapshot result = d->snapshot();
    return result.isValid() ? Utils::Result<Snapshot>{result}
                            : Utils::Result<Snapshot>{Utils::ResultError(
                                  QStringLiteral("Compiler preparation snapshot is invalid."))};
}

Utils::Result<std::optional<Record>>
DurableRuntimePackageCompilerPreparationCoordinator::doPreviousRecordForCommittedTransition(
    const Data::RuntimePackageCompilerOperationId &compileOperationId,
    quint64 currentRevision,
    quint64 snapshotSequence) const
{
    if (!d->committedTransition || d->committedTransition->compileOperationId != compileOperationId
        || d->committedTransition->currentRevision != currentRevision
        || d->committedTransition->snapshotSequence != snapshotSequence) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation predecessor is not the committed CAS value."));
    }
    return d->committedTransition->previous;
}

} // namespace EtherCAT::ProjectCompiler
