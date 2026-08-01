// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/runtimepackagecompilerpreparationcoordinator.h>

#include <ethercatdata/runtimepackageactivation.h>

#include <utils/filepath.h>

#include <functional>
#include <memory>

namespace EtherCAT::ProjectCompiler {

using RuntimePackageCompilerCurrentProjectCapture = std::function<
    Utils::Result<Data::RuntimePackageActivationProjectCapture>(const Data::NodeId &projectId)>;

// Concrete IDE-local orchestration for compile -> detached sign -> finalize ->
// independent verify -> provider-owned proof assembly. It owns no private key,
// reads no compiler-returned path, and never accesses a controller.
class DurableRuntimePackageCompilerPreparationCoordinator final
    : public Core::RuntimePackageCompilerPreparationCoordinator
{
    Q_OBJECT

public:
    DurableRuntimePackageCompilerPreparationCoordinator(
        Core::ProviderRegistry *providerRegistry,
        const Utils::FilePath &journalRoot,
        RuntimePackageCompilerCurrentProjectCapture currentProjectCapture,
        QObject *parent = nullptr);
    ~DurableRuntimePackageCompilerPreparationCoordinator() final;

    QString initializationError() const;
    void shutdown();

protected:
    Utils::Result<Core::RuntimePackageCompilerPreparationDisposition> doStart(
        const Core::RuntimePackageCompilerPreparationStartRequest &request,
        Core::RuntimePackageCompilerProvider *frozenProvider) final;
    Utils::Result<Core::RuntimePackageCompilerPreparationDisposition> doSubmitDetachedSigningResponse(
        const Core::RuntimePackageCompilerPreparationRecord &record,
        const Data::RuntimePackageCompilerCanonicalJson &detachedSigningResponse,
        Core::RuntimePackageCompilerProvider *frozenProvider) final;
    Utils::Result<Core::RuntimePackageCompilerPreparationDisposition> doCancel(
        const Core::RuntimePackageCompilerPreparationRecord &record) final;
    Utils::Result<Core::RuntimePackageCompilerPreparationDisposition> doResume(
        const Core::RuntimePackageCompilerPreparationRecord &record,
        const Core::RuntimePackageCompilerPreparationStartRequest &exactOriginalRequest,
        Core::RuntimePackageCompilerProvider *frozenProvider) final;
    Utils::Result<std::optional<Core::RuntimePackageCompilerPreparationRecord>> doRecord(
        const Data::RuntimePackageCompilerOperationId &compileOperationId) const final;
    Utils::Result<Core::RuntimePackageCompilerPreparationSnapshot> doSnapshot() const final;
    Utils::Result<std::optional<Core::RuntimePackageCompilerPreparationRecord>>
    doPreviousRecordForCommittedTransition(
        const Data::RuntimePackageCompilerOperationId &compileOperationId,
        quint64 currentRevision,
        quint64 snapshotSequence) const final;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace EtherCAT::ProjectCompiler
