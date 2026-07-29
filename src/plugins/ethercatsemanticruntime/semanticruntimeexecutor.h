// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/semanticruntimeservice.h>

#include <QHash>
#include <QPointer>
#include <QSet>

#include <memory>

namespace EtherCAT::SemanticRuntime::Internal {

class RuntimePackageEvidenceRepository;
class VerifiedRuntimePackageEvidence;

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

class SemanticRuntimeExecutor final : public Core::SemanticRuntimeService
{
    Q_OBJECT

public:
    SemanticRuntimeExecutor(
        Core::ProjectService *projectService,
        Core::ProviderRegistry *providerRegistry,
        QObject *parent = nullptr,
        std::shared_ptr<const RuntimePackageEvidenceRepository> evidenceRepository = {});

    QList<Data::SemanticRuntimeContext> contexts() const final;

private:
    QList<Data::SemanticRuntimeContext> buildContexts() const;
    Data::SemanticRuntimeContext buildContext(
        const Data::ProjectSnapshot &project, const Data::NodeId &masterId) const;
    std::shared_ptr<const VerifiedRuntimePackageEvidence> cachedEvidence(
        const Data::SemanticBindingArtifactReference &reference, QString *error) const;
    void clearEvidenceCache();
    void publishContexts();
    void trackProvider(Core::Provider *provider);
    void untrackProvider(Core::Provider *provider);

    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    std::shared_ptr<const RuntimePackageEvidenceRepository> m_evidenceRepository;
    QHash<Core::ControllerConnectionProvider *, QList<QMetaObject::Connection>> m_providerConnections;
    QSet<Core::ControllerConnectionProvider *> m_providersBeingRemoved;
    QSet<Data::NodeId> m_projectsBeingRemoved;
    mutable QHash<QByteArray, std::shared_ptr<const VerifiedRuntimePackageEvidence>>
        m_evidenceCache;
    QList<Data::SemanticRuntimeContext> m_contexts;
};

} // namespace EtherCAT::SemanticRuntime::Internal
