// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/semanticruntimeservice.h>

#include <QHash>
#include <QPointer>
#include <QSet>

#include <memory>
#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

class RuntimePackageEvidenceRepository;
class VerifiedRuntimePackageEvidence;
class SemanticRuntimeExecutorExecution;

enum class SemanticRuntimeContextIssue {
    ControllerProviderUnavailable,
    ControllerProviderAmbiguous,
    ControllerSessionUnavailable,
    BindingArtifactMissing,
    BindingArtifactInvalid,
    RuntimeResourcesUnsupported,
    RuntimeResourceCatalogUnavailable,
    RuntimeResourceCatalogStale,
    RuntimeResourceCatalogEmpty,
    RuntimeResourceEpochIncomplete,
    SemanticBindingProofUnavailable,
    RuntimePackageEvidenceUnavailable,
    SemanticBindingAttestationUnsupported,
    SemanticBindingAttestationUnavailable,
    SemanticBindingAttestationInvalid,
    SemanticBindingAttestationStale,
    SemanticBindingResolutionFailed,
    RuntimeResourceSnapshotUnavailable,
    RuntimeResourceSnapshotStale,
    RuntimeResourceSnapshotIncomplete,
    RuntimeResourceSnapshotInvalid,
};

QString semanticRuntimeContextIssueDetail(SemanticRuntimeContextIssue issue);
QByteArray semanticRuntimeContextHash(const Data::SemanticRuntimeContext &context);

class SemanticRuntimeExecutor final : public Core::SemanticRuntimeService
{
    Q_OBJECT

public:
    SemanticRuntimeExecutor(
        Core::ProjectService *projectService,
        Core::ProviderRegistry *providerRegistry,
        QObject *parent = nullptr,
        std::shared_ptr<const RuntimePackageEvidenceRepository> evidenceRepository = {});
    ~SemanticRuntimeExecutor() final;

    QList<Data::SemanticRuntimeContext> contexts() const final;
    Data::SemanticOperationRecord submit(
        const Data::SemanticOperationRequest &request,
        const Data::SemanticRuntimeActor &actor) final;
    Data::SemanticOperationRecord approve(
        const Data::SemanticOperationApprovalRequest &approval,
        const Data::SemanticRuntimeActor &actor) final;
    std::optional<Data::SemanticOperationRecord> operation(
        const Data::SemanticOperationId &operationId) const final;
    QList<Data::SemanticRuntimeAuditEvent> audit(
        const QString &controllerId, quint64 afterSequence = 0) const final;

private:
    struct RuntimeBootstrapState
    {
        QByteArray identityKey;
        QByteArray attestationAttemptKey;
        quint64 catalogSignalBaseline = 0;
        quint64 snapshotSignalBaseline = 0;
        quint64 refreshRetrySignalBaseline = 0;
        quint8 refreshAttemptCount = 0;
        bool refreshAccepted = false;
        bool attestationAccepted = false;
        bool attestationRetryUsed = false;
    };

    QList<Data::SemanticRuntimeContext> buildContexts() const;
    Data::SemanticRuntimeContext buildContext(
        const Data::ProjectSnapshot &project, const Data::NodeId &masterId) const;
    std::shared_ptr<const VerifiedRuntimePackageEvidence> cachedEvidence(
        const Data::SemanticBindingArtifactReference &reference, QString *error) const;
    void clearEvidenceCache();
    void publishContexts();
    void scheduleRuntimeBootstrap();
    void processRuntimeBootstrap();
    void trackProvider(Core::Provider *provider);
    void untrackProvider(Core::Provider *provider);

    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    std::shared_ptr<const RuntimePackageEvidenceRepository> m_evidenceRepository;
    QHash<Core::ControllerConnectionProvider *, QList<QMetaObject::Connection>> m_providerConnections;
    QHash<Core::DeviceAdapterProvider *, QList<QMetaObject::Connection>>
        m_adapterProviderConnections;
    QSet<Core::ControllerConnectionProvider *> m_providersBeingRemoved;
    QSet<Data::NodeId> m_projectsBeingRemoved;
    mutable QHash<QByteArray, std::shared_ptr<const VerifiedRuntimePackageEvidence>>
        m_evidenceCache;
    QHash<Core::ControllerConnectionProvider *, RuntimeBootstrapState> m_runtimeBootstrapStates;
    QHash<Core::ControllerConnectionProvider *, quint64> m_runtimeBootstrapSignalGenerations;
    QHash<Core::ControllerConnectionProvider *, quint64> m_runtimeCatalogSignalGenerations;
    QHash<Core::ControllerConnectionProvider *, quint64> m_runtimeSnapshotSignalGenerations;
    bool m_runtimeBootstrapScheduled = false;
    QList<Data::SemanticRuntimeContext> m_contexts;
    std::unique_ptr<SemanticRuntimeExecutorExecution> m_execution;

    friend class SemanticRuntimeExecutorExecution;
};

} // namespace EtherCAT::SemanticRuntime::Internal
