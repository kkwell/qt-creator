// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/semanticruntimeservice.h>

#include <QHash>
#include <QPointer>
#include <QSet>

namespace EtherCAT::SemanticRuntime::Internal {

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
};

QString semanticRuntimeContextIssueDetail(SemanticRuntimeContextIssue issue);

class SemanticRuntimeExecutor final : public Core::SemanticRuntimeService
{
    Q_OBJECT

public:
    SemanticRuntimeExecutor(
        Core::ProjectService *projectService,
        Core::ProviderRegistry *providerRegistry,
        QObject *parent = nullptr);

    QList<Data::SemanticRuntimeContext> contexts() const final;

private:
    QList<Data::SemanticRuntimeContext> buildContexts() const;
    Data::SemanticRuntimeContext buildContext(
        const Data::ProjectSnapshot &project, const Data::NodeId &masterId) const;
    void publishContexts();
    void trackProvider(Core::Provider *provider);
    void untrackProvider(Core::Provider *provider);

    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    QHash<Core::ControllerConnectionProvider *, QList<QMetaObject::Connection>> m_providerConnections;
    QSet<Core::ControllerConnectionProvider *> m_providersBeingRemoved;
    QSet<Data::NodeId> m_projectsBeingRemoved;
    QList<Data::SemanticRuntimeContext> m_contexts;
};

} // namespace EtherCAT::SemanticRuntime::Internal
