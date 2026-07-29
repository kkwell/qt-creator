// Copyright (C) 2026 Embed Labs

#include "semanticruntimeexecutor.h"

#include <ethercatcore/automationservice.h>

#include <algorithm>

namespace EtherCAT::SemanticRuntime::Internal {

using ContextIssue = SemanticRuntimeContextIssue;

QString semanticRuntimeContextIssueDetail(ContextIssue issue)
{
    switch (issue) {
    case ContextIssue::ControllerProviderUnavailable:
        return QStringLiteral(
            "controller-provider-unavailable: No available controller provider is bound to this "
            "project and master.");
    case ContextIssue::ControllerProviderAmbiguous:
        return QStringLiteral(
            "controller-provider-ambiguous: More than one available controller provider is bound "
            "to this project and master.");
    case ContextIssue::ControllerSessionUnavailable:
        return QStringLiteral(
            "controller-session-unavailable: The controller session is not connected.");
    case ContextIssue::BindingArtifactMissing:
        return QStringLiteral(
            "semantic-binding-artifact-missing: No semantic binding artifact is attached to this "
            "project.");
    case ContextIssue::BindingArtifactInvalid:
        return QStringLiteral(
            "semantic-binding-artifact-invalid: The semantic binding artifact reference is "
            "incomplete or invalid.");
    case ContextIssue::RuntimeResourcesUnsupported:
        return QStringLiteral(
            "runtime-resources-unsupported: The controller provider does not expose runtime "
            "resources.");
    case ContextIssue::RuntimeResourceCatalogUnavailable:
        return QStringLiteral(
            "runtime-resource-catalog-unavailable: The runtime resource catalog is unavailable.");
    case ContextIssue::RuntimeResourceCatalogStale:
        return QStringLiteral(
            "runtime-resource-catalog-stale: The runtime resource catalog does not match the "
            "current controller scope or session.");
    case ContextIssue::RuntimeResourceCatalogEmpty:
        return QStringLiteral(
            "runtime-resource-catalog-empty: The runtime resource catalog contains no resources.");
    case ContextIssue::RuntimeResourceEpochIncomplete:
        return QStringLiteral(
            "runtime-resource-epoch-incomplete: The runtime resource catalog epoch is incomplete.");
    case ContextIssue::SemanticBindingProofUnavailable:
        return QStringLiteral(
            "semantic-binding-proof-unavailable: A signed semantic binding and controller mapping "
            "proof are required.");
    }
    return {};
}

static bool isConnected(Data::ControllerConnectionState state)
{
    return state == Data::ControllerConnectionState::Connected
           || state == Data::ControllerConnectionState::Degraded;
}

static bool bindingArtifactIsEmpty(const Data::SemanticBindingArtifactReference &reference)
{
    return reference.artifactId.isEmpty() && reference.artifactSha256.isEmpty()
           && reference.projectConfigurationSha256.isEmpty();
}

static bool bindingArtifactIsValid(const Data::SemanticBindingArtifactReference &reference)
{
    return !reference.artifactId.isEmpty() && reference.artifactId == reference.artifactId.trimmed()
           && reference.artifactSha256.size() == 32
           && reference.projectConfigurationSha256.size() == 32;
}

static void rejectContext(Data::SemanticRuntimeContext &context, ContextIssue issue)
{
    context.complete = false;
    context.mappingDigest = {};
    context.controllerMappingDigest = {};
    context.contextHash.clear();
    context.signalStates.clear();
    context.actionStates.clear();
    context.bindingVerification = {};
    context.bindingVerification.state = Data::SemanticBindingVerificationState::Unverified;
    context.detail = semanticRuntimeContextIssueDetail(issue);
    context.bindingVerification.detail = context.detail;
}

SemanticRuntimeExecutor::SemanticRuntimeExecutor(
    Core::ProjectService *projectService, Core::ProviderRegistry *providerRegistry, QObject *parent)
    : SemanticRuntimeService(parent)
    , m_projectService(projectService)
    , m_providerRegistry(providerRegistry)
{
    if (m_projectService) {
        connect(m_projectService, &Core::Provider::availabilityChanged, this, [this] {
            publishContexts();
        });
        connect(
            m_projectService,
            &Core::ProjectService::projectAdded,
            this,
            [this](const Data::ProjectSnapshot &project) {
                m_projectsBeingRemoved.remove(project.id);
                publishContexts();
            });
        connect(
            m_projectService,
            &Core::ProjectService::projectChanged,
            this,
            [this](const Data::ProjectSnapshot &project) {
                m_projectsBeingRemoved.remove(project.id);
                publishContexts();
            });
        connect(
            m_projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            [this](const Data::NodeId &projectId) {
                m_projectsBeingRemoved.insert(projectId);
                publishContexts();
            });
        connect(m_projectService, &Core::ProjectService::activeProjectChanged, this, [this] {
            publishContexts();
        });
        connect(m_projectService, &QObject::destroyed, this, [this] {
            m_projectService = nullptr;
            publishContexts();
        });
    }

    if (m_providerRegistry) {
        connect(
            m_providerRegistry,
            &Core::ProviderRegistry::providerAdded,
            this,
            [this](Core::Provider *provider) {
                if (auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(
                        provider)) {
                    m_providersBeingRemoved.remove(connectionProvider);
                }
                trackProvider(provider);
                publishContexts();
            });
        connect(
            m_providerRegistry,
            &Core::ProviderRegistry::providerAboutToBeRemoved,
            this,
            [this](Core::Provider *provider) {
                if (auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(
                        provider)) {
                    m_providersBeingRemoved.insert(connectionProvider);
                }
                untrackProvider(provider);
                publishContexts();
            });
        connect(m_providerRegistry, &QObject::destroyed, this, [this] {
            m_providerRegistry = nullptr;
            m_providerConnections.clear();
            publishContexts();
        });
        for (Core::Provider *provider : m_providerRegistry->providers())
            trackProvider(provider);
    }

    m_contexts = buildContexts();
}

QList<Data::SemanticRuntimeContext> SemanticRuntimeExecutor::contexts() const
{
    return m_contexts;
}

QList<Data::SemanticRuntimeContext> SemanticRuntimeExecutor::buildContexts() const
{
    if (!m_projectService || !m_projectService->isAvailable())
        return {};

    QList<Data::ProjectSnapshot> projects = m_projectService->projects();
    std::sort(
        projects.begin(),
        projects.end(),
        [](const Data::ProjectSnapshot &left, const Data::ProjectSnapshot &right) {
            return left.id.toString() < right.id.toString();
        });

    QList<Data::SemanticRuntimeContext> contexts;
    for (const Data::ProjectSnapshot &project : std::as_const(projects)) {
        if (!project.valid || project.id.isNull() || m_projectsBeingRemoved.contains(project.id))
            continue;

        QList<Data::NodeId> masterIds;
        for (const Data::ProjectNodeSnapshot &node : project.nodes) {
            if (node.kind == Data::ProjectNodeKind::Master && !node.id.isNull()
                && !masterIds.contains(node.id)) {
                masterIds.append(node.id);
            }
        }
        std::sort(
            masterIds.begin(),
            masterIds.end(),
            [](const Data::NodeId &left, const Data::NodeId &right) {
                return left.toString() < right.toString();
            });
        for (const Data::NodeId &masterId : std::as_const(masterIds))
            contexts.append(buildContext(project, masterId));
    }
    return contexts;
}

Data::SemanticRuntimeContext SemanticRuntimeExecutor::buildContext(
    const Data::ProjectSnapshot &project, const Data::NodeId &masterId) const
{
    Data::SemanticRuntimeContext context;
    context.scope = {project.id, masterId};
    context.controllerId = Core::automationControllerId(context.scope);

    struct Candidate
    {
        Core::ControllerConnectionProvider *provider = nullptr;
        Data::ControllerConnectionSnapshot snapshot;
    };
    QList<Candidate> candidates;
    if (m_providerRegistry) {
        for (Core::Provider *provider :
             m_providerRegistry->providers(Core::ProviderKind::ControllerConnection)) {
            auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider);
            if (!connectionProvider || m_providersBeingRemoved.contains(connectionProvider)
                || !connectionProvider->isAvailable()) {
                continue;
            }
            const Data::ControllerConnectionSnapshot snapshot
                = connectionProvider->connectionSnapshot();
            if (snapshot.scope == context.scope)
                candidates.append({connectionProvider, snapshot});
        }
    }

    if (candidates.isEmpty()) {
        rejectContext(context, ContextIssue::ControllerProviderUnavailable);
        return context;
    }
    if (candidates.size() != 1) {
        rejectContext(context, ContextIssue::ControllerProviderAmbiguous);
        return context;
    }

    const Candidate &candidate = candidates.constFirst();
    context.mock = candidate.snapshot.mock;
    if (!isConnected(candidate.snapshot.state) || !candidate.snapshot.sessionGeneration) {
        rejectContext(context, ContextIssue::ControllerSessionUnavailable);
        return context;
    }
    context.sessionGeneration = candidate.snapshot.sessionGeneration;

    if (bindingArtifactIsEmpty(project.masterBindingArtifact)) {
        rejectContext(context, ContextIssue::BindingArtifactMissing);
        return context;
    }
    if (!bindingArtifactIsValid(project.masterBindingArtifact)) {
        rejectContext(context, ContextIssue::BindingArtifactInvalid);
        return context;
    }
    if (!candidate.provider->supportsRuntimeResources()) {
        rejectContext(context, ContextIssue::RuntimeResourcesUnsupported);
        return context;
    }

    const std::optional<Data::RuntimeResourceCatalog> catalog
        = candidate.provider->runtimeResourceCatalog();
    if (!catalog) {
        rejectContext(context, ContextIssue::RuntimeResourceCatalogUnavailable);
        return context;
    }
    if (catalog->scope != context.scope || catalog->sessionGeneration != context.sessionGeneration) {
        rejectContext(context, ContextIssue::RuntimeResourceCatalogStale);
        return context;
    }
    if (catalog->resources.isEmpty()) {
        rejectContext(context, ContextIssue::RuntimeResourceCatalogEmpty);
        return context;
    }
    if (!Core::isCompleteRuntimeResourceCatalogEpoch(catalog->epoch)) {
        rejectContext(context, ContextIssue::RuntimeResourceEpochIncomplete);
        return context;
    }

    context.epoch = catalog->epoch;
    rejectContext(context, ContextIssue::SemanticBindingProofUnavailable);
    return context;
}

void SemanticRuntimeExecutor::publishContexts()
{
    const QList<Data::SemanticRuntimeContext> next = buildContexts();
    if (next == m_contexts)
        return;
    m_contexts = next;
    emit contextsChanged();
}

void SemanticRuntimeExecutor::trackProvider(Core::Provider *provider)
{
    auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider);
    if (!connectionProvider || m_providerConnections.contains(connectionProvider))
        return;

    QList<QMetaObject::Connection> connections;
    connections.append(
        connect(connectionProvider, &Core::Provider::availabilityChanged, this, [this] {
            publishContexts();
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::connectionSnapshotChanged,
        this,
        [this] { publishContexts(); }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceCatalogChanged,
        this,
        [this] { publishContexts(); }));
    connections.append(
        connect(connectionProvider, &QObject::destroyed, this, [this, connectionProvider] {
            m_providerConnections.remove(connectionProvider);
            m_providersBeingRemoved.remove(connectionProvider);
            publishContexts();
        }));
    m_providerConnections.insert(connectionProvider, connections);
}

void SemanticRuntimeExecutor::untrackProvider(Core::Provider *provider)
{
    auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider);
    if (!connectionProvider)
        return;
    const QList<QMetaObject::Connection> connections = m_providerConnections.take(
        connectionProvider);
    for (const QMetaObject::Connection &connection : connections)
        disconnect(connection);
}

} // namespace EtherCAT::SemanticRuntime::Internal
