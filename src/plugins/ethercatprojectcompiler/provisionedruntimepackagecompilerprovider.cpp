// Copyright (C) 2026 Embed Labs

#include "provisionedruntimepackagecompilerprovider.h"

#include "compileroperationstore.h"
#include "compilerprovisioningprofile.h"
#include "ethercatprojectcompilerconstants.h"
#include "ethercatprojectcompilertr.h"

#include <ethercatcore/runtimepackagecompilercodec.h>

#include <utils/commandline.h>
#include <utils/qtcprocess.h>

#include <QProcess>
#include <QSet>
#include <QTimer>

#include <functional>
#include <optional>
#include <utility>

namespace EtherCAT::ProjectCompiler {

namespace {

using JobResultDecoder = std::function<Utils::Result<Data::RuntimePackageCompilerJobResult>(
    const CompilerOperationLease &, const Core::RuntimePackageCompilerProcessOutput &)>;
using QueryResultDecoder = std::function<Utils::Result<Data::RuntimePackageCompilerQueryResult>(
    const CompilerOperationLease &, const Core::RuntimePackageCompilerProcessOutput &)>;

Utils::CommandLine commandLine(
    const Utils::FilePath &executable, const QString &command, const QStringList &arguments)
{
    QStringList complete{command};
    complete.append(arguments);
    return Utils::CommandLine(executable, complete);
}

class ProvisionedCompilerJob final : public Core::RuntimePackageCompilerJob
{
public:
    struct Setup
    {
        Data::RuntimePackageCompilerCommand command = Data::RuntimePackageCompilerCommand::Unknown;
        Utils::CommandLine primaryCommand;
        Utils::FilePath workingDirectory;
        std::chrono::milliseconds primaryTimeout;
        std::optional<Utils::CommandLine> reconciliationCommand;
        std::chrono::milliseconds reconciliationTimeout;
        std::chrono::milliseconds cancellationGrace;
        qsizetype maximumStandardOutputBytes = 0;
        qsizetype maximumStandardErrorBytes = 0;
        CompilerOperationLease lease;
        JobResultDecoder decodePrimary;
        QueryResultDecoder decodeReconciliation;
        std::function<Utils::Result<>(const CompilerOperationLease &)> validateProvisioning;
    };

    explicit ProvisionedCompilerJob(Setup setup, QObject *parent)
        : Core::RuntimePackageCompilerJob(setup.command, parent)
        , m_setup(std::move(setup))
    {
        m_timeout.setSingleShot(true);
        m_cancellationTimer.setSingleShot(true);
        connect(&m_process, &Utils::Process::started, this, [this] {
            if (m_phase == Phase::Primary)
                markRunning();
        });
        connect(&m_process, &Utils::Process::readyReadStandardOutput, this, [this] {
            appendOutput(m_process.readAllRawStandardOutput(), true);
        });
        connect(&m_process, &Utils::Process::readyReadStandardError, this, [this] {
            appendOutput(m_process.readAllRawStandardError(), false);
        });
        connect(&m_process, &Utils::Process::done, this, [this] { processDone(); });
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            m_timedOut = true;
            stopCurrentProcess();
        });
        connect(&m_cancellationTimer, &QTimer::timeout, this, [this] {
            if (m_process.state() != QProcess::NotRunning)
                m_process.stop();
        });
    }

    void start()
    {
        QTimer::singleShot(0, this, [this] {
            if (m_shutdown || m_phase != Phase::None
                || state() == Core::RuntimePackageCompilerJobState::Finished) {
                return;
            }
            if (state() == Core::RuntimePackageCompilerJobState::CancelRequested) {
                scheduleReconciliation();
                return;
            }
            beginProcess(Phase::Primary, m_setup.primaryCommand, m_setup.primaryTimeout);
        });
    }

    void forceShutdown()
    {
        if (m_shutdown)
            return;
        m_shutdown = true;
        m_timeout.stop();
        m_cancellationTimer.stop();
        while (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            if (m_process.state() != QProcess::NotRunning)
                m_process.waitForFinished(QDeadlineTimer::Forever);
        }
        if (state() != Core::RuntimePackageCompilerJobState::Finished)
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
    }

protected:
    void requestCancellation() final
    {
        if (m_phase == Phase::ReconciliationPending || m_phase == Phase::Reconciliation)
            return;
        if (m_process.state() == QProcess::NotRunning) {
            if (m_setup.reconciliationCommand)
                scheduleReconciliation();
            else
                finishWithError(
                    Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
            return;
        }
        m_process.interrupt();
        m_cancellationTimer.start(m_setup.cancellationGrace);
    }

private:
    enum class Phase { None, Primary, ReconciliationPending, Reconciliation };

    void beginProcess(
        Phase phase, const Utils::CommandLine &command, std::chrono::milliseconds timeout)
    {
        if (!m_setup.validateProvisioning || !m_setup.validateProvisioning(m_setup.lease)) {
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
            return;
        }
        m_phase = phase;
        m_standardOutput.clear();
        m_standardError.clear();
        m_outputOverflow = false;
        m_timedOut = false;
        m_process.setCommand(command);
        m_process.setWorkingDirectory(m_setup.workingDirectory);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.setAbortOnMetaChars(true);
        m_process.start();
        m_timeout.start(timeout);
    }

    void scheduleReconciliation()
    {
        if (!m_setup.reconciliationCommand) {
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
            return;
        }
        if (m_phase == Phase::ReconciliationPending || m_phase == Phase::Reconciliation)
            return;
        m_phase = Phase::ReconciliationPending;
        QTimer::singleShot(0, this, [this] {
            if (m_shutdown || state() == Core::RuntimePackageCompilerJobState::Finished)
                return;
            beginReconciliation();
        });
    }

    void beginReconciliation()
    {
        if (m_phase != Phase::ReconciliationPending || !m_setup.reconciliationCommand)
            return;
        m_cancellationTimer.stop();
        beginProcess(
            Phase::Reconciliation, *m_setup.reconciliationCommand, m_setup.reconciliationTimeout);
    }

    void appendOutput(const QByteArray &bytes, bool standardOutput)
    {
        QByteArray &target = standardOutput ? m_standardOutput : m_standardError;
        const qsizetype limit = standardOutput ? m_setup.maximumStandardOutputBytes
                                               : m_setup.maximumStandardErrorBytes;
        if (bytes.size() > limit - target.size()) {
            const qsizetype remaining = qMax<qsizetype>(0, limit - target.size());
            target.append(bytes.constData(), remaining);
            m_outputOverflow = true;
            stopCurrentProcess();
            return;
        }
        target.append(bytes);
    }

    void stopCurrentProcess()
    {
        if (m_process.state() == QProcess::NotRunning)
            return;
        m_process.interrupt();
        m_cancellationTimer.start(m_setup.cancellationGrace);
    }

    Core::RuntimePackageCompilerProcessOutput processOutput() const
    {
        return {
            m_process.exitStatus() == QProcess::NormalExit,
            m_process.exitCode(),
            m_standardOutput,
            m_standardError,
        };
    }

    void processDone()
    {
        m_timeout.stop();
        m_cancellationTimer.stop();
        appendOutput(m_process.readAllRawStandardOutput(), true);
        appendOutput(m_process.readAllRawStandardError(), false);
        if (m_shutdown)
            return;
        const Core::RuntimePackageCompilerProcessOutput output = processOutput();

        if (m_phase == Phase::Reconciliation) {
            reconciliationDone(output);
            return;
        }

        Utils::Result<Data::RuntimePackageCompilerJobResult> decoded = Utils::ResultError(
            Tr::tr("Compiler process did not produce a terminal result."));
        if (!m_outputOverflow && !m_timedOut)
            decoded = m_setup.decodePrimary(m_setup.lease, output);
        if (decoded)
            m_primaryResult = *decoded;

        if (state() == Core::RuntimePackageCompilerJobState::CancelRequested
            && m_setup.reconciliationCommand) {
            scheduleReconciliation();
            return;
        }
        if (m_outputOverflow || m_timedOut || !output.exitedNormally) {
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
            return;
        }
        if (!decoded) {
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::InvalidTerminalResult);
            return;
        }
        finishWithResult(*decoded);
    }

    void reconciliationDone(const Core::RuntimePackageCompilerProcessOutput &output)
    {
        if (m_outputOverflow || m_timedOut || !output.exitedNormally) {
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
            return;
        }
        const Utils::Result<Data::RuntimePackageCompilerQueryResult> reconciled
            = m_setup.decodeReconciliation(m_setup.lease, output);
        if (!reconciled) {
            finishWithError(Core::RuntimePackageCompilerJobCompletionError::InvalidTerminalResult);
            return;
        }
        if (m_primaryResult) {
            finishWithResult(*m_primaryResult);
            return;
        }
        finishWithError(Core::RuntimePackageCompilerJobCompletionError::CanceledAfterReconciliation);
    }

    void finishWithResult(const Data::RuntimePackageCompilerJobResult &result)
    {
        m_setup.lease = {};
        finish(result);
    }

    void finishWithError(Core::RuntimePackageCompilerJobCompletionError error)
    {
        m_setup.lease = {};
        finishWithCompletionError(error);
    }

    Setup m_setup;
    Utils::Process m_process;
    QTimer m_timeout;
    QTimer m_cancellationTimer;
    Phase m_phase = Phase::None;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    bool m_outputOverflow = false;
    bool m_timedOut = false;
    bool m_shutdown = false;
    std::optional<Data::RuntimePackageCompilerJobResult> m_primaryResult;
};

} // namespace

bool RuntimePackageCompilerProcessLimits::isValid() const
{
    return maximumStandardOutputBytes > 0 && maximumStandardErrorBytes > 0
           && maximumArtifactBytes > 0 && commandTimeout > std::chrono::milliseconds::zero()
           && queryTimeout > std::chrono::milliseconds::zero()
           && cancellationGrace > std::chrono::milliseconds::zero();
}

class ProvisionedRuntimePackageCompilerProvider::Private
{
public:
    Private(
        ProvisionedRuntimePackageCompilerProvider *q,
        const Utils::FilePath &provisioningFile,
        const Utils::FilePath &compilerRoot,
        RuntimePackageCompilerProcessLimits processLimits)
        : q(q)
        , store(compilerRoot)
        , limits(processLimits)
    {
        if (!limits.isValid()) {
            error = Tr::tr("Compiler process limits are invalid.");
            return;
        }
        const Utils::Result<CompilerProvisioningProfile> loaded = CompilerProvisioningProfile::load(
            provisioningFile);
        if (!loaded) {
            error = loaded.error();
            return;
        }
        profile = *loaded;
        const Utils::Result<> initialized = store.initialize();
        if (!initialized) {
            error = initialized.error();
            profile.reset();
            return;
        }
        Utils::Result<CompilerOperationLease> lease = store.acquireLease();
        if (!lease) {
            error = lease.error();
            profile.reset();
            return;
        }
        const Utils::Result<Utils::FilePath> pinnedExecutable = store.pinProvisionedFile(
            *lease,
            profile->exactExecutableBytes(),
            profile->executableSha256(),
            CompilerProvisionedFileKind::Executable);
        if (!pinnedExecutable) {
            error = pinnedExecutable.error();
            profile.reset();
            return;
        }
        const Utils::Result<Utils::FilePath> pinnedPublicKey = store.pinProvisionedFile(
            *lease,
            profile->exactProductionPublicKeyBytes(),
            profile->productionPublicKeySha256(),
            CompilerProvisionedFileKind::ProductionPublicKey);
        if (!pinnedPublicKey) {
            error = pinnedPublicKey.error();
            profile.reset();
            return;
        }
        if (const Utils::Result<> pinned
            = profile->usePinnedFiles(*pinnedExecutable, *pinnedPublicKey);
            !pinned) {
            error = pinned.error();
            profile.reset();
            return;
        }
        q->setAvailable(true);
    }

    Utils::Result<> validateProfile(const Data::RuntimePackageCompilerContractIdentity &identity)
    {
        if (shuttingDown)
            return Utils::ResultError(Tr::tr("Compiler provider is shutting down."));
        if (!profile)
            return Utils::ResultError(error);
        const Utils::Result<> current = profile->validateCurrent();
        if (!current) {
            error = current.error();
            q->setAvailable(false);
            return Utils::ResultError(error);
        }
        if (identity != profile->contractIdentity()) {
            return Utils::ResultError(
                Tr::tr("Compiler request contract does not match provisioning."));
        }
        return Utils::ResultOk;
    }

    Utils::Result<Core::RuntimePackageCompilerJob *> schedule(ProvisionedCompilerJob::Setup setup)
    {
        if (shuttingDown)
            return Utils::ResultError(Tr::tr("Compiler provider is shutting down."));
        setup.validateProvisioning = [this](const CompilerOperationLease &lease) -> Utils::Result<> {
            if (!profile)
                return Utils::ResultError(error);
            if (const Utils::Result<> executable = store.validatePinnedProvisionedFile(
                    lease,
                    profile->executable(),
                    profile->executableSha256(),
                    CompilerProvisionedFileKind::Executable);
                !executable) {
                return executable;
            }
            return store.validatePinnedProvisionedFile(
                lease,
                profile->productionPublicKey(),
                profile->productionPublicKeySha256(),
                CompilerProvisionedFileKind::ProductionPublicKey);
        };
        auto *job = new ProvisionedCompilerJob(std::move(setup), q);
        jobs.insert(job);
        QObject::connect(job, &QObject::destroyed, q, [this, job] { jobs.remove(job); });
        job->start();
        return job;
    }

    QueryResultDecoder queryDecoder(const Data::RuntimePackageCompilerQueryRequest &request)
    {
        return [this, request](
                   const CompilerOperationLease &lease,
                   const Core::RuntimePackageCompilerProcessOutput &output)
                   -> Utils::Result<Data::RuntimePackageCompilerQueryResult> {
            const Utils::Result<Data::RuntimePackageCompilerQueryResult> decoded
                = Core::decodeRuntimePackageCompilerQueryResult(request, output);
            if (!decoded)
                return Utils::ResultError(decoded.error());
            const Utils::Result<Utils::FilePath> persisted = store.persistCanonicalResponse(
                lease,
                request.operationId,
                CompilerCanonicalEvidenceKind::QueryResponse,
                decoded->envelope.canonicalResult);
            if (!persisted)
                return Utils::ResultError(persisted.error());
            return *decoded;
        };
    }

    std::optional<Utils::CommandLine> reconciliationCommand(
        const Data::RuntimePackageCompilerOperationId &operationId) const
    {
        if (!profile)
            return std::nullopt;
        return commandLine(
            profile->executable(),
            QStringLiteral("query"),
            {QStringLiteral("--operation-id"),
             operationId.value(),
             QStringLiteral("--ledger"),
             store.compilerLedger().toFSPathString()});
    }

    ProvisionedRuntimePackageCompilerProvider *q;
    CompilerOperationStore store;
    RuntimePackageCompilerProcessLimits limits;
    std::optional<CompilerProvisioningProfile> profile;
    QString error;
    QSet<ProvisionedCompilerJob *> jobs;
    bool shuttingDown = false;
};

ProvisionedRuntimePackageCompilerProvider::ProvisionedRuntimePackageCompilerProvider(
    const Utils::FilePath &provisioningFile,
    const Utils::FilePath &compilerRoot,
    RuntimePackageCompilerProcessLimits limits,
    QObject *parent)
    : Core::RuntimePackageCompilerProvider(
          Constants::PROJECT_COMPILER_PROVIDER_ID, Tr::tr("Provisioned project compiler"), parent)
    , d(std::make_unique<Private>(this, provisioningFile, compilerRoot, limits))
{}

ProvisionedRuntimePackageCompilerProvider::~ProvisionedRuntimePackageCompilerProvider()
{
    shutdown();
}

QString ProvisionedRuntimePackageCompilerProvider::provisioningError() const
{
    return d->error;
}

Utils::FilePath ProvisionedRuntimePackageCompilerProvider::compilerRoot() const
{
    return d->store.compilerRoot();
}

Utils::FilePath ProvisionedRuntimePackageCompilerProvider::operationRoot(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return d->store.operationRoot(operationId);
}

Utils::Result<Core::RuntimePackageCompilerJob *> ProvisionedRuntimePackageCompilerProvider::compile(
    const Data::RuntimePackageCompilerCompileRequest &request)
{
    if (const Utils::Result<> current = d->validateProfile(request.contractIdentity); !current)
        return Utils::ResultError(current.error());
    if (!request.isValid()
        || request.sourceArtifacts.productionPublicKey.sha256
               != d->profile->productionPublicKeySha256()
        || request.targetProfile.signingKeyIdSha256 != d->profile->productionPublicKeySha256()) {
        return Utils::ResultError(
            Tr::tr("Compile request does not match the provisioned production key."));
    }
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonical
        = Core::encodeRuntimePackageCompilerCompileRequest(request);
    if (!canonical)
        return Utils::ResultError(canonical.error());
    Utils::Result<CompilerOperationLease> lease = d->store.acquireLease();
    if (!lease)
        return Utils::ResultError(lease.error());
    const Utils::Result<CompilerOperationPaths> paths
        = d->store.reserveCompile(*lease, request, *canonical);
    if (!paths)
        return Utils::ResultError(paths.error());

    const Data::RuntimePackageCompilerQueryRequest queryRequest{
        request.operationId,
        request.contractIdentity,
        canonical->sha256(),
    };
    const qsizetype maximumArtifactBytes = d->limits.maximumArtifactBytes;
    JobResultDecoder decoder = [store = &d->store, request, paths = *paths, maximumArtifactBytes](
                                   const CompilerOperationLease &lease,
                                   const Core::RuntimePackageCompilerProcessOutput &output)
        -> Utils::Result<Data::RuntimePackageCompilerJobResult> {
        Data::RuntimePackageCompilerCanonicalJson signRequest;
        if (output.exitedNormally && output.exitCode == 0) {
            const Utils::Result<QByteArray> bytes = store->readProviderFile(
                lease, paths.outputDir / "sign-request.json", maximumArtifactBytes);
            if (!bytes)
                return Utils::ResultError(bytes.error());
            signRequest = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(*bytes);
        }
        const Utils::Result<Data::RuntimePackageCompilerCompileResult> decoded
            = Core::decodeRuntimePackageCompilerCompileResult(request, output, signRequest);
        if (!decoded)
            return Utils::ResultError(decoded.error());
        if (decoded->isSuccess()) {
            if (Utils::FilePath::fromString(decoded->outputDirectory).toFSPathString()
                    != paths.outputDir.toFSPathString()
                || !decoded->compiledProjectSha256 || !decoded->compileReportSha256
                || !decoded->effectiveProjectCompanionSha256 || !decoded->manifestSha256) {
                return Utils::ResultError(
                    Tr::tr("Compiler result references an unowned output path."));
            }
            const QList<std::pair<Utils::FilePath, Data::RuntimePackageCompilerSha256>> evidence{
                {paths.outputDir / "project.json", *decoded->compiledProjectSha256},
                {paths.outputDir / "packages" / "compile_report.json",
                 *decoded->compileReportSha256},
                {paths.outputDir / "effective-project-companion-v1.json",
                 *decoded->effectiveProjectCompanionSha256},
                {paths.outputDir / "signing_stage" / "manifest.json", *decoded->manifestSha256},
                {paths.outputDir / "sign-request.json", decoded->signRequest->sha256()},
            };
            for (const auto &[file, digest] : evidence) {
                if (const Utils::Result<Utils::FilePath> persisted = store->persistFileEvidence(
                        lease, request.operationId, file, digest, maximumArtifactBytes);
                    !persisted) {
                    return Utils::ResultError(persisted.error());
                }
            }
            if (const Utils::Result<Utils::FilePath> persisted = store->persistCanonicalResponse(
                    lease,
                    request.operationId,
                    CompilerCanonicalEvidenceKind::SignRequest,
                    *decoded->signRequest);
                !persisted) {
                return Utils::ResultError(persisted.error());
            }
        }
        if (const Utils::Result<Utils::FilePath> persisted = store->persistCanonicalResponse(
                lease,
                request.operationId,
                CompilerCanonicalEvidenceKind::CompilerRecord,
                decoded->envelope.canonicalResult);
            !persisted) {
            return Utils::ResultError(persisted.error());
        }
        return Data::RuntimePackageCompilerJobResult{*decoded};
    };

    return d->schedule({
        Data::RuntimePackageCompilerCommand::Compile,
        commandLine(
            d->profile->executable(),
            QStringLiteral("compile"),
            {QStringLiteral("--request"),
             paths->compileRequest.toFSPathString(),
             QStringLiteral("--artifact-root"),
             paths->artifactRoot.toFSPathString(),
             QStringLiteral("--output-dir"),
             paths->outputDir.toFSPathString(),
             QStringLiteral("--ledger"),
             paths->compilerLedger.toFSPathString()}),
        paths->operationRoot,
        d->limits.commandTimeout,
        d->reconciliationCommand(request.operationId),
        d->limits.queryTimeout,
        d->limits.cancellationGrace,
        d->limits.maximumStandardOutputBytes,
        d->limits.maximumStandardErrorBytes,
        std::move(*lease),
        std::move(decoder),
        d->queryDecoder(queryRequest),
        {},
    });
}

Utils::Result<Core::RuntimePackageCompilerJob *> ProvisionedRuntimePackageCompilerProvider::finalize(
    const Data::RuntimePackageCompilerFinalizeRequest &request)
{
    if (const Utils::Result<> current = d->validateProfile(request.contractIdentity); !current)
        return Utils::ResultError(current.error());
    Utils::Result<CompilerOperationLease> lease = d->store.acquireLease();
    if (!lease)
        return Utils::ResultError(lease.error());
    const Utils::Result<CompilerOperationPaths> paths = d->store.validateFinalize(*lease, request);
    if (!paths)
        return Utils::ResultError(paths.error());
    const Utils::Result<Utils::FilePath> signResponse = d->store.persistCanonicalResponse(
        *lease,
        request.operationId,
        CompilerCanonicalEvidenceKind::SignResponse,
        request.detachedSigningResponse);
    if (!signResponse)
        return Utils::ResultError(signResponse.error());

    const Data::RuntimePackageCompilerQueryRequest queryRequest{
        request.operationId,
        request.contractIdentity,
        request.compileRequestSha256,
    };
    const qsizetype maximumArtifactBytes = d->limits.maximumArtifactBytes;
    JobResultDecoder decoder = [store = &d->store, request, paths = *paths, maximumArtifactBytes](
                                   const CompilerOperationLease &lease,
                                   const Core::RuntimePackageCompilerProcessOutput &output)
        -> Utils::Result<Data::RuntimePackageCompilerJobResult> {
        QByteArray packageBytes;
        if (output.exitedNormally && output.exitCode == 0) {
            const Utils::Result<QByteArray> bytes
                = store->readProviderFile(lease, paths.package, maximumArtifactBytes);
            if (!bytes)
                return Utils::ResultError(bytes.error());
            packageBytes = *bytes;
        }
        const Utils::Result<Data::RuntimePackageCompilerFinalizeResult> decoded
            = Core::decodeRuntimePackageCompilerFinalizeResult(request, output, packageBytes);
        if (!decoded)
            return Utils::ResultError(decoded.error());
        if (decoded->isSuccess()) {
            if (Utils::FilePath::fromString(decoded->packagePath).toFSPathString()
                != paths.package.toFSPathString()) {
                return Utils::ResultError(
                    Tr::tr("Finalizer result references an unowned package path."));
            }
            const Utils::Result<Utils::FilePath> persisted = store->persistPackage(
                lease, request.operationId, decoded->packageBytes, *decoded->packageSha256);
            if (!persisted)
                return Utils::ResultError(persisted.error());
        }
        if (const Utils::Result<Utils::FilePath> persisted = store->persistCanonicalResponse(
                lease,
                request.operationId,
                CompilerCanonicalEvidenceKind::CompilerRecord,
                decoded->envelope.canonicalResult);
            !persisted) {
            return Utils::ResultError(persisted.error());
        }
        return Data::RuntimePackageCompilerJobResult{*decoded};
    };

    return d->schedule({
        Data::RuntimePackageCompilerCommand::Finalize,
        commandLine(
            d->profile->executable(),
            QStringLiteral("finalize"),
            {QStringLiteral("--request"),
             paths->compileRequest.toFSPathString(),
             QStringLiteral("--output-dir"),
             paths->outputDir.toFSPathString(),
             QStringLiteral("--ledger"),
             paths->compilerLedger.toFSPathString(),
             QStringLiteral("--sign-response"),
             paths->signResponse.toFSPathString(),
             QStringLiteral("--public-key"),
             d->profile->productionPublicKey().toFSPathString(),
             QStringLiteral("--output"),
             paths->package.toFSPathString()}),
        paths->operationRoot,
        d->limits.commandTimeout,
        d->reconciliationCommand(request.operationId),
        d->limits.queryTimeout,
        d->limits.cancellationGrace,
        d->limits.maximumStandardOutputBytes,
        d->limits.maximumStandardErrorBytes,
        std::move(*lease),
        std::move(decoder),
        d->queryDecoder(queryRequest),
        {},
    });
}

Utils::Result<Core::RuntimePackageCompilerJob *> ProvisionedRuntimePackageCompilerProvider::query(
    const Data::RuntimePackageCompilerQueryRequest &request)
{
    if (const Utils::Result<> current = d->validateProfile(request.contractIdentity); !current)
        return Utils::ResultError(current.error());
    Utils::Result<CompilerOperationLease> lease = d->store.acquireLease();
    if (!lease)
        return Utils::ResultError(lease.error());
    const Utils::Result<CompilerOperationPaths> paths = d->store.validateQuery(*lease, request);
    if (!paths)
        return Utils::ResultError(paths.error());
    JobResultDecoder decoder = [decode = d->queryDecoder(request)](
                                   const CompilerOperationLease &lease,
                                   const Core::RuntimePackageCompilerProcessOutput &output)
        -> Utils::Result<Data::RuntimePackageCompilerJobResult> {
        const Utils::Result<Data::RuntimePackageCompilerQueryResult> result = decode(lease, output);
        if (!result)
            return Utils::ResultError(result.error());
        return Data::RuntimePackageCompilerJobResult{*result};
    };
    return d->schedule({
        Data::RuntimePackageCompilerCommand::Query,
        *d->reconciliationCommand(request.operationId),
        paths->operationRoot,
        d->limits.queryTimeout,
        std::nullopt,
        d->limits.queryTimeout,
        d->limits.cancellationGrace,
        d->limits.maximumStandardOutputBytes,
        d->limits.maximumStandardErrorBytes,
        std::move(*lease),
        std::move(decoder),
        {},
        {},
    });
}

Utils::Result<Core::RuntimePackageCompilerJob *> ProvisionedRuntimePackageCompilerProvider::verify(
    const Data::RuntimePackageCompilerVerifyRequest &request)
{
    if (const Utils::Result<> current = d->validateProfile(request.contractIdentity); !current)
        return Utils::ResultError(current.error());
    Utils::Result<CompilerOperationLease> lease = d->store.acquireLease();
    if (!lease)
        return Utils::ResultError(lease.error());
    const Utils::Result<CompilerOperationPaths> paths = d->store.reserveVerify(*lease, request);
    if (!paths)
        return Utils::ResultError(paths.error());
    JobResultDecoder decoder = [store = &d->store, request](
                                   const CompilerOperationLease &lease,
                                   const Core::RuntimePackageCompilerProcessOutput &output)
        -> Utils::Result<Data::RuntimePackageCompilerJobResult> {
        const Utils::Result<Data::RuntimePackageCompilerVerifyResult> decoded
            = Core::decodeRuntimePackageCompilerVerifyResult(request, output);
        if (!decoded)
            return Utils::ResultError(decoded.error());
        const Utils::Result<Utils::FilePath> persisted = store->persistCanonicalResponse(
            lease,
            request.operationId,
            CompilerCanonicalEvidenceKind::VerifyResponse,
            decoded->envelope.canonicalResult);
        if (!persisted)
            return Utils::ResultError(persisted.error());
        return Data::RuntimePackageCompilerJobResult{*decoded};
    };
    return d->schedule({
        Data::RuntimePackageCompilerCommand::Verify,
        commandLine(
            d->profile->executable(),
            QStringLiteral("verify"),
            {QStringLiteral("--package"),
             paths->package.toFSPathString(),
             QStringLiteral("--public-key"),
             d->profile->productionPublicKey().toFSPathString()}),
        paths->operationRoot,
        d->limits.commandTimeout,
        std::nullopt,
        d->limits.queryTimeout,
        d->limits.cancellationGrace,
        d->limits.maximumStandardOutputBytes,
        d->limits.maximumStandardErrorBytes,
        std::move(*lease),
        std::move(decoder),
        {},
        {},
    });
}

void ProvisionedRuntimePackageCompilerProvider::shutdown()
{
    if (!d || d->shuttingDown)
        return;
    d->shuttingDown = true;
    d->error = Tr::tr("Compiler provider has shut down.");
    setAvailable(false);
    const QSet<ProvisionedCompilerJob *> jobs = d->jobs;
    for (ProvisionedCompilerJob *job : jobs) {
        if (job)
            job->forceShutdown();
    }
    d->jobs.clear();
}

} // namespace EtherCAT::ProjectCompiler
