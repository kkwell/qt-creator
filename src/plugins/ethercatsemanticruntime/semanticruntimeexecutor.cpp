// Copyright (C) 2026 Embed Labs

#include "semanticruntimeexecutor.h"

#include "readonlysemanticbindingfactory_p.h"
#include "runtimepackageevidencerepository_p.h"
#include "semanticactionruntimefactory_p.h"

#include <ethercatcore/automationservice.h>

#include <QCryptographicHash>
#include <QtEndian>

#include <algorithm>
#include <cstring>

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
    case ContextIssue::RuntimePackageEvidenceUnavailable:
        return QStringLiteral(
            "runtime-package-evidence-unavailable: The referenced production package evidence "
            "could not be loaded and verified.");
    case ContextIssue::SemanticBindingAttestationUnsupported:
        return QStringLiteral(
            "semantic-binding-attestation-unsupported: The controller provider does not expose "
            "semantic mapping attestation.");
    case ContextIssue::SemanticBindingAttestationUnavailable:
        return QStringLiteral(
            "semantic-binding-attestation-unavailable: No controller semantic mapping "
            "attestation is available.");
    case ContextIssue::SemanticBindingAttestationInvalid:
        return QStringLiteral(
            "semantic-binding-attestation-invalid: The controller semantic mapping attestation "
            "is malformed.");
    case ContextIssue::SemanticBindingAttestationStale:
        return QStringLiteral(
            "semantic-binding-attestation-stale: The controller semantic mapping attestation "
            "does not match the current scope, session, or package epoch.");
    case ContextIssue::SemanticBindingResolutionFailed:
        return QStringLiteral(
            "semantic-binding-resolution-failed: The signed project-device and runtime-resource "
            "bindings could not be resolved exactly.");
    case ContextIssue::RuntimeResourceSnapshotUnavailable:
        return QStringLiteral(
            "runtime-resource-snapshot-unavailable: No complete runtime resource snapshot is "
            "available.");
    case ContextIssue::RuntimeResourceSnapshotStale:
        return QStringLiteral(
            "runtime-resource-snapshot-stale: The runtime resource snapshot does not match the "
            "current scope, session, or package epoch.");
    case ContextIssue::RuntimeResourceSnapshotIncomplete:
        return QStringLiteral(
            "runtime-resource-snapshot-incomplete: The runtime resource snapshot is not an exact "
            "complete set for the verified semantic bindings.");
    case ContextIssue::RuntimeResourceSnapshotInvalid:
        return QStringLiteral(
            "runtime-resource-snapshot-invalid: One or more runtime samples failed verified read "
            "validation.");
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

static QByteArray evidenceCacheKey(
    const Data::SemanticBindingArtifactReference &reference)
{
    QByteArray key = reference.artifactId.toUtf8();
    key.append('\0');
    key.append(reference.artifactSha256);
    key.append(reference.projectConfigurationSha256);
    return key;
}

static void rejectContext(
    Data::SemanticRuntimeContext &context, ContextIssue issue, const QString &additionalDetail = {})
{
    context.complete = false;
    context.mappingDigest = {};
    context.controllerMappingDigest = {};
    context.actionDefinitionsDigest = {};
    context.cyclePeriodNs = 0;
    context.contextHash.clear();
    context.signalStates.clear();
    context.actionStates.clear();
    context.bindingVerification = {};
    context.bindingVerification.state = Data::SemanticBindingVerificationState::Unverified;
    context.detail = semanticRuntimeContextIssueDetail(issue);
    if (!additionalDetail.isEmpty())
        context.detail += QStringLiteral(" ") + additionalDetail;
    context.bindingVerification.detail = context.detail;
}

static bool explicitProjectDevicePositionsMatch(
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    QString *detail)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    for (const Data::SemanticProjectDeviceBinding &mapping :
         project.masterBindingArtifact.projectDeviceBindings) {
        const VerifiedSemanticDevice *signedDevice = artifact.findDevice(mapping.projectDeviceId);
        const auto slave = std::find_if(
            project.slaves.cbegin(),
            project.slaves.cend(),
            [&mapping](const Data::OfflineSlaveConfiguration &candidate) {
                return candidate.id == mapping.slaveId;
            });
        if (!signedDevice || slave == project.slaves.cend() || slave->position < 0
            || slave->position != int(signedDevice->position)) {
            if (detail) {
                *detail = QStringLiteral(
                    "An explicit project-device mapping does not match its signed bus position.");
            }
            return false;
        }
    }
    return true;
}

static void appendContextBytes(QByteArray &canonical, QByteArrayView bytes)
{
    const quint32 size = quint32(bytes.size());
    const quint32 bigEndianSize = qToBigEndian(size);
    canonical.append(
        reinterpret_cast<const char *>(&bigEndianSize), qsizetype(sizeof(bigEndianSize)));
    canonical.append(bytes.data(), bytes.size());
}

static void appendContextString(QByteArray &canonical, const QString &value)
{
    appendContextBytes(canonical, value.toUtf8());
}

static void appendContextInteger(QByteArray &canonical, quint64 value)
{
    const quint64 bigEndianValue = qToBigEndian(value);
    canonical.append(
        reinterpret_cast<const char *>(&bigEndianValue), qsizetype(sizeof(bigEndianValue)));
}

static bool appendContextValue(QByteArray &canonical, const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        appendContextInteger(canonical, 1);
        appendContextInteger(canonical, value.toBool() ? 1 : 0);
        return true;
    case QMetaType::LongLong:
        appendContextInteger(canonical, 2);
        appendContextInteger(canonical, quint64(value.toLongLong()));
        return true;
    case QMetaType::ULongLong:
        appendContextInteger(canonical, 3);
        appendContextInteger(canonical, value.toULongLong());
        return true;
    case QMetaType::Double: {
        appendContextInteger(canonical, 4);
        quint64 bits = 0;
        const double number = value.toDouble();
        static_assert(sizeof(bits) == sizeof(number));
        std::memcpy(&bits, &number, sizeof(bits));
        appendContextInteger(canonical, bits);
        return true;
    }
    case QMetaType::QString:
        appendContextInteger(canonical, 5);
        appendContextString(canonical, value.toString());
        return true;
    case QMetaType::QByteArray:
        appendContextInteger(canonical, 6);
        appendContextBytes(canonical, value.toByteArray());
        return true;
    default:
        return false;
    }
}

static void appendBindingIdentity(
    QByteArray &canonical, const Data::SemanticRuntimeBinding &binding)
{
    appendContextString(canonical, binding.semanticBindingId);
    appendContextString(canonical, binding.componentBindingId);
    appendContextString(canonical, binding.adapterId.value);
    appendContextString(canonical, binding.adapterVersion);
    appendContextBytes(canonical, binding.adapterContentSha256);
    appendContextBytes(canonical, binding.esiSha256);
    appendContextBytes(canonical, binding.bindingArtifactSha256);
    appendContextBytes(canonical, binding.resourceId.value);
    appendContextBytes(canonical, binding.componentInstanceId.value);
    appendContextBytes(canonical, binding.consistencyGroupId.value);
    appendContextBytes(canonical, binding.valueTypeIdentity);
    appendContextInteger(canonical, binding.bitWidth);
    appendContextInteger(canonical, quint64(binding.direction));
    appendContextInteger(canonical, quint64(binding.access));
}

QByteArray semanticRuntimeContextHash(const Data::SemanticRuntimeContext &context)
{
    QByteArray canonical = QByteArrayLiteral("embed-labs.semantic-runtime-context.v2");
    appendContextString(canonical, context.controllerId);
    appendContextString(canonical, context.scope.projectId.toString());
    appendContextString(canonical, context.scope.masterId.toString());
    appendContextInteger(canonical, context.mock ? 1 : 0);
    appendContextInteger(canonical, context.sessionGeneration);
    appendContextInteger(canonical, context.epoch.controllerBootId);
    appendContextInteger(canonical, quint64(context.epoch.activePackageSlot));
    appendContextInteger(canonical, context.epoch.activePackageGeneration);
    appendContextInteger(canonical, context.epoch.configurationId);
    appendContextInteger(canonical, context.epoch.topologyGeneration);
    appendContextInteger(canonical, context.epoch.runtimeGeneration);
    appendContextInteger(canonical, context.epoch.catalogRevision);
    appendContextBytes(canonical, context.epoch.topologyIdentity);
    appendContextBytes(canonical, context.mappingDigest.value);
    appendContextBytes(canonical, context.controllerMappingDigest.value);
    appendContextBytes(canonical, context.actionDefinitionsDigest.value);
    appendContextInteger(canonical, context.cyclePeriodNs);
    appendContextString(canonical, context.bindingVerification.verifierId);
    appendContextBytes(canonical, context.bindingVerification.signedManifestDigest.value);

    QList<Data::SemanticSignalRuntimeState> states = context.signalStates;
    std::sort(
        states.begin(),
        states.end(),
        [](const Data::SemanticSignalRuntimeState &left,
           const Data::SemanticSignalRuntimeState &right) {
            if (left.target.deviceId != right.target.deviceId) {
                return left.target.deviceId.toString() < right.target.deviceId.toString();
            }
            return left.target.signalId.value < right.target.signalId.value;
        });
    appendContextInteger(canonical, quint64(states.size()));
    QSet<QString> signalTargets;
    for (const Data::SemanticSignalRuntimeState &state : std::as_const(states)) {
        const QString targetKey
            = state.target.deviceId.toString() + QLatin1Char('/') + state.target.signalId.value;
        if (signalTargets.contains(targetKey))
            return {};
        signalTargets.insert(targetKey);
        appendContextString(canonical, state.target.deviceId.toString());
        appendContextString(canonical, state.target.signalId.value);
        appendContextInteger(canonical, state.binding.has_value() ? 1 : 0);
        if (!state.binding)
            continue;
        appendBindingIdentity(canonical, *state.binding);
        appendContextString(canonical, state.definition.manualControl.policyId);
        appendContextInteger(canonical, state.definition.manualControl.allowed ? 1 : 0);
        appendContextInteger(
            canonical, state.definition.manualControl.requiresExclusiveControl ? 1 : 0);
        appendContextInteger(canonical, state.definition.manualControl.holdToRun ? 1 : 0);
        appendContextInteger(canonical, state.definition.manualControl.commandTimeoutMs);
        appendContextInteger(
            canonical, quint64(state.definition.manualControl.timeoutAction));
        appendContextInteger(canonical, state.definition.hasSafeValue ? 1 : 0);
        if (state.definition.hasSafeValue
            && !appendContextValue(canonical, state.definition.safeValue)) {
            return {};
        }
    }

    QList<Data::SemanticActionRuntimeState> actions = context.actionStates;
    std::sort(
        actions.begin(),
        actions.end(),
        [](const Data::SemanticActionRuntimeState &left,
           const Data::SemanticActionRuntimeState &right) {
            if (left.target.deviceId != right.target.deviceId) {
                return left.target.deviceId.toString() < right.target.deviceId.toString();
            }
            return left.target.actionId.value < right.target.actionId.value;
        });
    appendContextInteger(canonical, quint64(actions.size()));
    QSet<QString> actionTargets;
    for (Data::SemanticActionRuntimeState &action : actions) {
        const QString targetKey
            = action.target.deviceId.toString() + QLatin1Char('/') + action.target.actionId.value;
        if (actionTargets.contains(targetKey))
            return {};
        actionTargets.insert(targetKey);
        appendContextString(canonical, action.target.deviceId.toString());
        appendContextString(canonical, action.target.actionId.value);
        appendContextString(canonical, action.actionBindingId);
        appendContextString(canonical, action.actionDefinitionId);
        appendContextBytes(canonical, action.actionDefinitionDigest.value);
        appendContextInteger(canonical, quint64(action.qualification));
        appendContextInteger(canonical, action.definition.enabled ? 1 : 0);
        appendContextString(canonical, action.disabledReason);
        appendContextInteger(canonical, action.requiresApproval ? 1 : 0);
        appendContextInteger(canonical, action.requiresExclusiveControl ? 1 : 0);
        appendContextInteger(canonical, action.requiresDc ? 1 : 0);
        appendContextInteger(canonical, action.holdToRun ? 1 : 0);
        appendContextInteger(canonical, action.maximumTtlCycles);

        std::sort(
            action.parameters.begin(),
            action.parameters.end(),
            [](const Data::SemanticActionParameterRuntimeDefinition &left,
               const Data::SemanticActionParameterRuntimeDefinition &right) {
                return left.id < right.id;
            });
        appendContextInteger(canonical, quint64(action.parameters.size()));
        QSet<QString> parameterIds;
        for (const Data::SemanticActionParameterRuntimeDefinition &parameter :
             std::as_const(action.parameters)) {
            if (parameter.id.isEmpty() || parameterIds.contains(parameter.id))
                return {};
            parameterIds.insert(parameter.id);
            appendContextString(canonical, parameter.id);
            appendContextInteger(canonical, quint64(parameter.primitiveType));
            appendContextString(canonical, parameter.unit);
            if (!appendContextValue(canonical, parameter.minimum)
                || !appendContextValue(canonical, parameter.maximum)) {
                return {};
            }
        }

        std::sort(
            action.bindings.begin(),
            action.bindings.end(),
            [](const Data::SemanticRuntimeBinding &left,
               const Data::SemanticRuntimeBinding &right) {
                return left.semanticBindingId < right.semanticBindingId;
            });
        appendContextInteger(canonical, quint64(action.bindings.size()));
        QSet<QString> bindingIds;
        for (const Data::SemanticRuntimeBinding &binding : std::as_const(action.bindings)) {
            if (binding.semanticBindingId.isEmpty()
                || bindingIds.contains(binding.semanticBindingId)) {
                return {};
            }
            bindingIds.insert(binding.semanticBindingId);
            appendBindingIdentity(canonical, binding);
        }
    }
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

static bool snapshotHasExactBindingSet(
    const Data::RuntimeResourceSnapshot &snapshot,
    const ReadOnlySemanticBindingCandidates &candidates)
{
    if (!snapshot.complete || snapshot.samples.size() != candidates.bindings.size())
        return false;

    QList<QByteArray> expectedIds;
    expectedIds.reserve(candidates.bindings.size());
    for (const Data::SemanticRuntimeBinding &binding : candidates.bindings) {
        if (!binding.resourceId.isValid())
            return false;
        expectedIds.append(binding.resourceId.value);
    }
    std::sort(expectedIds.begin(), expectedIds.end());
    if (std::adjacent_find(expectedIds.cbegin(), expectedIds.cend()) != expectedIds.cend())
        return false;

    QList<QByteArray> actualIds;
    actualIds.reserve(snapshot.samples.size());
    for (const Data::RuntimeResourceSample &sample : snapshot.samples) {
        if (!sample.resourceId.isValid())
            return false;
        actualIds.append(sample.resourceId.value);
    }
    std::sort(actualIds.begin(), actualIds.end());
    return actualIds == expectedIds
           && std::adjacent_find(actualIds.cbegin(), actualIds.cend()) == actualIds.cend();
}

SemanticRuntimeExecutor::SemanticRuntimeExecutor(
    Core::ProjectService *projectService,
    Core::ProviderRegistry *providerRegistry,
    QObject *parent,
    std::shared_ptr<const RuntimePackageEvidenceRepository> evidenceRepository)
    : SemanticRuntimeService(parent)
    , m_projectService(projectService)
    , m_providerRegistry(providerRegistry)
    , m_evidenceRepository(std::move(evidenceRepository))
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
                clearEvidenceCache();
                publishContexts();
            });
        connect(
            m_projectService,
            &Core::ProjectService::projectChanged,
            this,
            [this](const Data::ProjectSnapshot &project) {
                m_projectsBeingRemoved.remove(project.id);
                clearEvidenceCache();
                publishContexts();
            });
        connect(
            m_projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            [this](const Data::NodeId &projectId) {
                m_projectsBeingRemoved.insert(projectId);
                clearEvidenceCache();
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

std::shared_ptr<const VerifiedRuntimePackageEvidence>
SemanticRuntimeExecutor::cachedEvidence(
    const Data::SemanticBindingArtifactReference &reference, QString *error) const
{
    const QByteArray key = evidenceCacheKey(reference);
    const auto cached = m_evidenceCache.constFind(key);
    if (cached != m_evidenceCache.cend())
        return *cached;

    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = m_evidenceRepository->load(reference);
    if (!evidence) {
        if (error)
            *error = evidence.error();
        return {};
    }

    auto verified = std::make_shared<const VerifiedRuntimePackageEvidence>(*evidence);
    m_evidenceCache.insert(key, verified);
    return verified;
}

void SemanticRuntimeExecutor::clearEvidenceCache()
{
    m_evidenceCache.clear();
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
    if (!m_evidenceRepository) {
        // Preserve the original fail-closed behavior for hosts that have not yet
        // configured the application-owned evidence stores.
        rejectContext(context, ContextIssue::SemanticBindingProofUnavailable);
        return context;
    }

    QString evidenceError;
    const std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence
        = cachedEvidence(project.masterBindingArtifact, &evidenceError);
    if (!evidence) {
        rejectContext(
            context, ContextIssue::RuntimePackageEvidenceUnavailable, evidenceError);
        return context;
    }

    QString projectDeviceDetail;
    if (!explicitProjectDevicePositionsMatch(project, *evidence, &projectDeviceDetail)) {
        rejectContext(
            context, ContextIssue::SemanticBindingResolutionFailed, projectDeviceDetail);
        return context;
    }

    if (!candidate.provider->supportsRuntimeSemanticMappingAttestation()) {
        rejectContext(context, ContextIssue::SemanticBindingAttestationUnsupported);
        return context;
    }
    const std::optional<Data::RuntimeSemanticMappingAttestation> attestation
        = candidate.provider->runtimeSemanticMappingAttestation();
    if (!attestation) {
        rejectContext(context, ContextIssue::SemanticBindingAttestationUnavailable);
        return context;
    }
    if (!attestation->isValid()) {
        rejectContext(context, ContextIssue::SemanticBindingAttestationInvalid);
        return context;
    }
    if (attestation->scope != context.scope
        || attestation->sessionGeneration != context.sessionGeneration
        || attestation->epoch != context.epoch) {
        rejectContext(context, ContextIssue::SemanticBindingAttestationStale);
        return context;
    }

    const Utils::Result<ReadOnlySemanticBindingCandidates> bindingCandidates
        = buildReadOnlySemanticBindingCandidates(
            context.controllerId, project, *evidence, *catalog, *attestation);
    if (!bindingCandidates || bindingCandidates->bindings.isEmpty()
        || bindingCandidates->bindings.size() != bindingCandidates->signalStates.size()) {
        rejectContext(
            context,
            ContextIssue::SemanticBindingResolutionFailed,
            bindingCandidates
                ? QStringLiteral("The verified binding set is empty or inconsistent.")
                : bindingCandidates.error());
        return context;
    }

    context.mappingDigest = bindingCandidates->mappingDigest;
    context.controllerMappingDigest = bindingCandidates->controllerMappingDigest;
    context.cyclePeriodNs = evidence->cyclePeriodNs();
    context.bindingVerification = bindingCandidates->verification;
    context.signalStates = bindingCandidates->signalStates;
    if (evidence->actionDefinitions()) {
        context.actionDefinitionsDigest = {
            QStringLiteral("sha256"),
            evidence->actionDefinitions()->definitionsSha256,
        };
        SemanticActionRuntimeGates gates;
        gates.outputTransactionsSupported
            = candidate.provider->supportsRuntimeOutputTransactions()
              && candidate.snapshot.capability
              && candidate.snapshot.capability->runtimeOutputTransactions;
        gates.ownsExclusiveControl
            = !candidate.snapshot.readOnly && candidate.snapshot.session
              && candidate.snapshot.session->sessionId
              && candidate.snapshot.session->ownsControlLease
              && candidate.snapshot.session->controlLeaseOwnerSessionId
                     == candidate.snapshot.session->sessionId;
        if (candidate.snapshot.controllerState) {
            gates.serviceState = candidate.snapshot.controllerState->serviceState;
            gates.dcRuntimeActive
                = candidate.snapshot.controllerState->busOperational
                  && candidate.snapshot.controllerState->distributedClocksLocked;
        }
        const Utils::Result<QList<Data::SemanticActionRuntimeState>> actions
            = buildSemanticActionRuntimeStates(
                context.controllerId, project, *evidence, *bindingCandidates, gates);
        if (!actions) {
            rejectContext(
                context, ContextIssue::SemanticBindingResolutionFailed, actions.error());
            return context;
        }
        context.actionStates = *actions;
    } else {
        context.actionDefinitionsDigest = {};
        context.actionStates.clear();
    }
    context.complete = true;
    context.contextHash = semanticRuntimeContextHash(context);

    const std::optional<Data::RuntimeResourceSnapshot> snapshot
        = candidate.provider->runtimeResourceSnapshot();
    if (!snapshot) {
        context.detail = semanticRuntimeContextIssueDetail(
            ContextIssue::RuntimeResourceSnapshotUnavailable);
        return context;
    }
    if (snapshot->scope != context.scope
        || snapshot->sessionGeneration != context.sessionGeneration
        || snapshot->epoch != context.epoch) {
        context.detail = semanticRuntimeContextIssueDetail(
            ContextIssue::RuntimeResourceSnapshotStale);
        for (Data::SemanticSignalRuntimeState &state : context.signalStates)
            state.detail = context.detail;
        return context;
    }
    if (!snapshotHasExactBindingSet(*snapshot, *bindingCandidates)) {
        context.detail = semanticRuntimeContextIssueDetail(
            ContextIssue::RuntimeResourceSnapshotIncomplete);
        for (Data::SemanticSignalRuntimeState &state : context.signalStates)
            state.detail = context.detail;
        return context;
    }

    bool allSamplesReady = true;
    for (qsizetype index = 0; index < bindingCandidates->bindings.size(); ++index) {
        Data::SemanticSignalRuntimeState &state = context.signalStates[index];
        const Core::SemanticRuntimeReadValidation validation
            = Core::validateSemanticRuntimeRead(
                bindingCandidates->bindings.at(index), *catalog, *snapshot);
        if (!validation.validation.accepted() || !validation.sample) {
            allSamplesReady = false;
            state.availability = Data::SemanticSignalAvailability::Unverified;
            state.value.reset();
            state.quality = {};
            state.snapshotComplete = false;
            state.captureCycle = 0;
            state.controllerTimestampNs = 0;
            state.detail = validation.validation.detail;
            continue;
        }

        state.availability = Data::SemanticSignalAvailability::Ready;
        state.value = validation.sample->value;
        state.quality = validation.sample->quality;
        state.snapshotComplete = true;
        state.captureCycle = snapshot->captureCycle;
        state.controllerTimestampNs = validation.sample->controllerTimestampNs;
        state.detail = QStringLiteral("Verified live runtime sample.");
    }

    context.detail
        = allSamplesReady
              ? QStringLiteral(
                    "semantic-runtime-ready: Signed bindings and live samples are verified.")
              : semanticRuntimeContextIssueDetail(ContextIssue::RuntimeResourceSnapshotInvalid);
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
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotChanged,
        this,
        [this] { publishContexts(); }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished,
        this,
        [this](const Data::RuntimeResourceSnapshotResult &) { publishContexts(); }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationChanged,
        this,
        [this] { publishContexts(); }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished,
        this,
        [this](const Data::RuntimeSemanticMappingAttestationResult &) { publishContexts(); }));
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
