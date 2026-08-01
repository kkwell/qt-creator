// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/runtimepackageactivationservice.h>

#include <QHash>
#include <QPointer>

#include <functional>
#include <memory>

namespace EtherCAT::SemanticRuntime::Internal {

class RuntimePackageEvidenceRepository;
class VerifiedRuntimePackageEvidence;

// Production coordinator for one fail-closed package activation transaction.
// It owns no controller protocol and calls only the shared provider surface.
class TrustedRuntimePackageActivationService final
    : public Core::RuntimePackageActivationService
{
    Q_OBJECT

public:
    struct Operation;

    // A trusted compiler integration must independently prove that the exact
    // signed package embeds the caller-supplied companion bytes and digest,
    // recompute every API-042 project/master/device projection from the
    // compile-time project evidence, and bind that proof to this exact current
    // ProjectCapture (including its serialized bytes and opaque document
    // revision/original-binding tokens). Merely checking JSON shape or SHA-256
    // syntax is not sufficient.
    using ExactCompileTimeProjectProofVerifier = std::function<Utils::Result<>(
        const Core::RuntimePackageActivationPreparationRequest &,
        const Data::RuntimePackageActivationProjectCapture &,
        const VerifiedRuntimePackageEvidence &)>;

    TrustedRuntimePackageActivationService(
        Core::ProjectService *projectService,
        Core::ProviderRegistry *providerRegistry,
        std::shared_ptr<const RuntimePackageEvidenceRepository> evidenceRepository,
        QString journalRoot,
        QObject *parent = nullptr,
        int providerDeadlineMs = 15000,
        int deploymentProgressDeadlineMs = 45000,
        std::function<bool()> journalCommitShouldFail = {},
        ExactCompileTimeProjectProofVerifier projectProofVerifier = {});
    ~TrustedRuntimePackageActivationService() final;

    Core::RuntimePackageActivationPreparationResult prepare(
        const Core::RuntimePackageActivationPreparationRequest &request) final;
    Core::RuntimePackageActivationCommandResult start(
        const Data::RuntimePackageActivationRequest &request) final;
    Core::RuntimePackageActivationCommandResult reconcile(
        const Data::RuntimePackageActivationOperationId &operationId) final;
    Core::RuntimePackageActivationCommandResult cancel(
        const Data::RuntimePackageActivationCancelRequest &request) final;
    std::optional<Data::RuntimePackageActivationRecord> record(
        const Data::RuntimePackageActivationOperationId &operationId) const final;
    Data::RuntimePackageActivationSnapshot snapshot() const final;

    bool hasUnresolvedRecoveryBarrier() const;
    QString recoveryBarrierDetail() const;

private:
    void process(const QString &operationId);
    void handleProviderSnapshot(const QString &operationId);
    void handleAttestationResult(
        const QString &operationId,
        const Data::RuntimeSemanticMappingAttestationResult &result);
    void handleProviderUnavailable(const QString &operationId);
    void failRuntimeVerification(
        Operation &operation, const QString &code, const QString &detail);
    void beginAcquire(Operation &operation);
    void beginDeploy(Operation &operation);
    void beginRuntimeVerification(Operation &operation);
    void commitProjectBinding(Operation &operation);
    void scheduleCompleteExistingPackage(Operation &operation);
    void completeExistingPackage(Operation &operation);
    void bindProvider(
        Operation &operation,
        Core::ControllerConnectionProvider *provider);
    void beginRecoveryRelease(
        Operation &operation,
        Data::RuntimePackageActivationOutcome terminalOutcome);
    void completeRecoveryAfterRelease(
        Operation &operation,
        const Data::RuntimePackageActivationControllerEvidence &released);
    void armProviderDeadline(
        Operation &operation,
        Data::RuntimePackageActivationPhase phase,
        Data::RuntimePackageActivationProviderAction action,
        const QString &code,
        const QString &detail);
    void beginRelease(
        Operation &operation,
        Data::RuntimePackageActivationOutcome terminalOutcome);
    void finish(
        Operation &operation,
        Data::RuntimePackageActivationOutcome outcome,
        const QString &code,
        const QString &detail,
        std::optional<Data::RuntimePackageActivationSha256> evidence);
    void failBeforeControllerMutation(
        Operation &operation, const QString &code, const QString &detail);
    void freezeUnknown(
        Operation &operation, const QString &code, const QString &detail);
    void refreshRecord(Operation &operation);
    Utils::Result<> persistGuard(Operation &operation);
    void publish(Operation &operation);
    void discoverRecoveryBarriers();

    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    std::shared_ptr<const RuntimePackageEvidenceRepository> m_evidenceRepository;
    QString m_journalRoot;
    QHash<QString, std::shared_ptr<Operation>> m_operations;
    QHash<QString, Data::RuntimePackageActivationSha256>
        m_preparedFingerprints;
    QStringList m_recoveryBarriers;
    quint64 m_snapshotSequence = 0;
    int m_providerDeadlineMs = 15000;
    int m_deploymentProgressDeadlineMs = 45000;
    std::function<bool()> m_journalCommitShouldFail;
    ExactCompileTimeProjectProofVerifier m_projectProofVerifier;
};

} // namespace EtherCAT::SemanticRuntime::Internal
