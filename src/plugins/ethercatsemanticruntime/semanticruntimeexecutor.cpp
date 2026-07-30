// Copyright (C) 2026 Embed Labs

#include "semanticruntimeexecutor.h"

#include "readonlysemanticbindingfactory_p.h"
#include "runtimepackageevidencerepository_p.h"
#include "semanticactionplan_p.h"
#include "semanticactionruntimefactory_p.h"
#include "semanticoperationjournal_p.h"

#include <ethercatcore/automationservice.h>

#include <QCryptographicHash>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

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

namespace {

enum class SemanticExecutionPhase {
    BeforeSnapshot,
    Policy,
    State,
    Apply,
    AfterSnapshot,
    AfterState,
};

QString conciseExecutionText(QString text)
{
    text = text.simplified();
    if (text.size() > 240)
        text = text.left(237) + QStringLiteral("...");
    return text;
}

QString providerFailureText(const QString &prefix, const Data::ControllerOperationError &error)
{
    QString reason = error.summary;
    if (reason.isEmpty())
        reason = error.detail;
    if (reason.isEmpty())
        reason = error.codeName;
    if (reason.isEmpty())
        reason = QStringLiteral("The controller provider rejected the request.");
    return conciseExecutionText(prefix + QStringLiteral(": ") + reason);
}

Data::SemanticRuntimeActor executorActor()
{
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("embed-labs-semantic-executor");
    actor.displayName = QStringLiteral("Embed Labs");
    actor.kind = Data::SemanticRuntimeActorKind::System;
    actor.origin = QStringLiteral("semantic-runtime");
    actor.authenticationDigest = QCryptographicHash::hash(
        QByteArrayLiteral("embed-labs.semantic-runtime.executor.v1"),
        QCryptographicHash::Sha256);
    return actor;
}

QString executionCorrelationId(
    const Data::SemanticOperationId &operationId, QStringView phase, quint32 index = 0)
{
    QByteArray material = operationId.value.toUtf8();
    material.append('\0');
    material.append(phase.toUtf8());
    material.append('\0');
    const quint32 bigEndianIndex = qToBigEndian(index);
    material.append(
        reinterpret_cast<const char *>(&bigEndianIndex), qsizetype(sizeof(bigEndianIndex)));
    return QStringLiteral("semantic-")
           + QString::fromLatin1(
               QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex())
           + QLatin1Char('-') + phase.toString();
}

bool allowedExecutionState(Data::ControllerServiceState state)
{
    return state == Data::ControllerServiceState::OperationalSafe
           || state == Data::ControllerServiceState::Running
           || state == Data::ControllerServiceState::Paused;
}

bool sameResourceIds(
    const QList<Data::RuntimeResourceId> &ids,
    const QList<Data::RuntimeOutputValueWrite> &writes)
{
    if (ids.size() != writes.size())
        return false;
    for (qsizetype index = 0; index < ids.size(); ++index) {
        if (ids.at(index) != writes.at(index).resourceId)
            return false;
    }
    return true;
}

QByteArray semanticOperationSnapshotDigest(
    const Data::SemanticOperationSnapshot &snapshot,
    const Data::RuntimeResourceCatalogEpoch &epoch,
    quint64 sessionGeneration)
{
    QByteArray canonical = QByteArrayLiteral("embed-labs.semantic-operation-snapshot.v1");
    appendContextInteger(canonical, sessionGeneration);
    appendContextInteger(canonical, epoch.controllerBootId);
    appendContextInteger(canonical, quint64(epoch.activePackageSlot));
    appendContextInteger(canonical, epoch.activePackageGeneration);
    appendContextInteger(canonical, epoch.configurationId);
    appendContextInteger(canonical, epoch.topologyGeneration);
    appendContextInteger(canonical, epoch.runtimeGeneration);
    appendContextInteger(canonical, epoch.catalogRevision);
    appendContextBytes(canonical, epoch.topologyIdentity);
    appendContextInteger(canonical, snapshot.captureCycle);
    appendContextInteger(canonical, snapshot.controllerTimestampNs);
    appendContextInteger(canonical, quint64(snapshot.observations.size()));
    for (const Data::SemanticOperationSignalObservation &observation :
         snapshot.observations) {
        appendContextString(canonical, observation.target.controllerId);
        appendContextString(canonical, observation.target.scope.projectId.toString());
        appendContextString(canonical, observation.target.scope.masterId.toString());
        appendContextString(canonical, observation.target.deviceId.toString());
        appendContextString(canonical, observation.target.signalId.value);
        appendContextInteger(canonical, quint64(observation.value.primitiveType));
        appendContextBytes(canonical, observation.value.typeIdentity);
        appendContextBytes(canonical, observation.value.opaqueRepresentation);
        if (!appendContextValue(canonical, observation.value.value))
            return {};
        appendContextInteger(canonical, quint64(observation.quality.state));
        appendContextInteger(canonical, observation.quality.flags);
        appendContextBytes(canonical, observation.quality.opaqueCode);
        appendContextInteger(canonical, observation.controllerTimestampNs);
    }
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

} // namespace

class SemanticRuntimeExecutorExecution final
{
public:
    explicit SemanticRuntimeExecutorExecution(SemanticRuntimeExecutor *executor)
        : q(executor)
    {}

    Data::SemanticOperationRecord submit(
        const Data::SemanticOperationRequest &request,
        const Data::SemanticRuntimeActor &actor);
    Data::SemanticOperationRecord approve(
        const Data::SemanticOperationApprovalRequest &approval,
        const Data::SemanticRuntimeActor &actor);
    std::optional<Data::SemanticOperationRecord> operation(
        const Data::SemanticOperationId &operationId) const;
    QList<Data::SemanticRuntimeAuditEvent> audit(
        const QString &controllerId, quint64 afterSequence) const;

    void handleSnapshot(
        Core::ControllerConnectionProvider *provider,
        const Data::RuntimeResourceSnapshotResult &result);
    void handlePolicy(
        Core::ControllerConnectionProvider *provider,
        const Data::RuntimeOutputGroupPolicyResult &result);
    void handleState(
        Core::ControllerConnectionProvider *provider,
        const Data::RuntimeOutputTransactionStateResult &result);
    void handleApply(
        Core::ControllerConnectionProvider *provider,
        const Data::RuntimeOutputTransactionResult &result);
    void providerRemoved(Core::ControllerConnectionProvider *provider);
    void providerRegistryRemoved();
    void projectServiceRemoved();

private:
    struct ActiveExecution
    {
        ActiveExecution(
            const Data::SemanticOperationId &id,
            const Data::SemanticRuntimeActor &requestActor,
            SemanticActionPlan actionPlan,
            Core::ControllerConnectionProvider *connectionProvider)
            : operationId(id)
            , actor(requestActor)
            , plan(std::move(actionPlan))
            , provider(connectionProvider)
            , providerId(connectionProvider ? connectionProvider->id() : Utils::Id())
        {}

        Data::SemanticOperationId operationId;
        Data::SemanticRuntimeActor actor;
        SemanticActionPlan plan;
        QPointer<Core::ControllerConnectionProvider> provider;
        Utils::Id providerId;
        SemanticExecutionPhase phase = SemanticExecutionPhase::BeforeSnapshot;
        QList<Data::RuntimeResourceId> resourceIds;
        qsizetype policyIndex = 0;
        QList<Data::RuntimeOutputGroupPolicy> policies;
        quint64 outputGeneration = 0;
        quint64 appliedCycle = 0;
        quint64 expiryCycle = 0;
        quint64 appliedOutputGeneration = 0;
        bool mutationMayHaveExecuted = false;
        Data::SemanticOperationState completionState = Data::SemanticOperationState::Executing;
        std::optional<Data::RuntimeResourceSnapshotRequest> snapshotRequest;
        std::optional<Data::RuntimeOutputGroupPolicyRequest> policyRequest;
        std::optional<Data::RuntimeOutputTransactionStateRequest> stateRequest;
        std::optional<Data::RuntimeOutputTransactionRequest> applyRequest;
        std::optional<Data::SemanticOperationSnapshot> pendingAfterSnapshot;
    };

    struct ControllerQueue
    {
        QList<Data::SemanticOperationId> pending;
        std::optional<ActiveExecution> active;
    };

    struct CurrentExecution
    {
        Data::SemanticRuntimeContext context;
        Data::ControllerConnectionSnapshot connection;
        Core::ControllerConnectionProvider *provider = nullptr;
    };

    std::optional<Data::SemanticRuntimeContext> currentContext(
        const Data::SemanticOperationRequest &request, QString *error) const;
    Core::ControllerConnectionProvider *currentProvider(
        const Data::ControllerConnectionScope &scope, QString *error) const;
    std::shared_ptr<const VerifiedRuntimePackageEvidence> currentEvidence(
        const Data::SemanticOperationRequest &request, QString *error) const;
    std::optional<CurrentExecution> validateCurrentExecution(
        const ActiveExecution &active, QString *error) const;

    void publishJournalResult(
        const SemanticOperationJournalResult &result, const QString &controllerId);
    void enqueue(const Data::SemanticOperationRecord &record);
    void startNext(const QString &controllerId);
    void beginBeforeSnapshot(const QString &controllerId);
    void beginNextPolicy(const QString &controllerId);
    void beginState(const QString &controllerId);
    void beginApply(const QString &controllerId);
    void beginAfterSnapshot(const QString &controllerId);
    void beginAfterState(const QString &controllerId);
    void failActive(
        const QString &controllerId,
        const QString &code,
        const QString &detail,
        const std::optional<Data::ControllerOperationError> &error = {});
    void freezeUnknown(
        const QString &controllerId,
        const QString &code,
        const QString &detail,
        const std::optional<Data::ControllerOperationError> &error = {});
    void expireActive(const QString &controllerId, const QString &detail);
    void succeedActive(const QString &controllerId);
    void finishActive(const QString &controllerId);

    SemanticActionPlanGroup groupFor(
        const ActiveExecution &active,
        const Data::RuntimeConsistencyGroupId &groupId,
        bool *found) const;
    std::optional<Data::SemanticOperationSnapshot> operationSnapshot(
        const ActiveExecution &active,
        const CurrentExecution &current,
        const Data::RuntimeResourceSnapshot &snapshot,
        QString *error) const;
    bool afterSnapshotMatchesWrites(
        const ActiveExecution &active,
        const Data::RuntimeResourceSnapshot &snapshot) const;

    SemanticRuntimeExecutor *q = nullptr;
    SemanticOperationJournal m_journal;
    QHash<QString, ControllerQueue> m_queues;
};

std::optional<Data::SemanticRuntimeContext> SemanticRuntimeExecutorExecution::currentContext(
    const Data::SemanticOperationRequest &request, QString *error) const
{
    if (!q->m_projectService || !q->m_projectService->isAvailable()) {
        if (error)
            *error = QStringLiteral("The project service is unavailable.");
        return std::nullopt;
    }
    const std::optional<Data::ProjectSnapshot> project
        = q->m_projectService->project(request.target.scope.projectId);
    if (!project || !project->valid) {
        if (error)
            *error = QStringLiteral("The operation project is unavailable.");
        return std::nullopt;
    }
    const auto master = std::find_if(
        project->nodes.cbegin(),
        project->nodes.cend(),
        [&request](const Data::ProjectNodeSnapshot &node) {
            return node.id == request.target.scope.masterId
                   && node.kind == Data::ProjectNodeKind::Master;
        });
    if (master == project->nodes.cend()) {
        if (error)
            *error = QStringLiteral("The operation master is unavailable.");
        return std::nullopt;
    }
    Data::SemanticRuntimeContext context
        = q->buildContext(*project, request.target.scope.masterId);
    if (context.controllerId != request.target.controllerId
        || context.scope != request.target.scope) {
        if (error)
            *error = QStringLiteral("The operation controller scope changed.");
        return std::nullopt;
    }
    return context;
}

Core::ControllerConnectionProvider *SemanticRuntimeExecutorExecution::currentProvider(
    const Data::ControllerConnectionScope &scope, QString *error) const
{
    QList<Core::ControllerConnectionProvider *> matches;
    if (q->m_providerRegistry) {
        for (Core::Provider *provider :
             q->m_providerRegistry->providers(Core::ProviderKind::ControllerConnection)) {
            auto *connection = qobject_cast<Core::ControllerConnectionProvider *>(provider);
            if (!connection || q->m_providersBeingRemoved.contains(connection)
                || !connection->isAvailable()) {
                continue;
            }
            if (connection->connectionSnapshot().scope == scope)
                matches.append(connection);
        }
    }
    if (matches.size() != 1) {
        if (error) {
            *error = matches.isEmpty()
                         ? QStringLiteral("The controller provider is unavailable.")
                         : QStringLiteral("The controller provider is ambiguous.");
        }
        return nullptr;
    }
    return matches.constFirst();
}

std::shared_ptr<const VerifiedRuntimePackageEvidence>
SemanticRuntimeExecutorExecution::currentEvidence(
    const Data::SemanticOperationRequest &request, QString *error) const
{
    if (!q->m_projectService || !q->m_evidenceRepository) {
        if (error)
            *error = QStringLiteral("Verified runtime package evidence is unavailable.");
        return {};
    }
    const std::optional<Data::ProjectSnapshot> project
        = q->m_projectService->project(request.target.scope.projectId);
    if (!project) {
        if (error)
            *error = QStringLiteral("The operation project is unavailable.");
        return {};
    }
    return q->cachedEvidence(project->masterBindingArtifact, error);
}

std::optional<SemanticRuntimeExecutorExecution::CurrentExecution>
SemanticRuntimeExecutorExecution::validateCurrentExecution(
    const ActiveExecution &active, QString *error) const
{
    const Data::SemanticOperationRequest &request = active.plan.request();
    const std::optional<Data::SemanticRuntimeContext> context = currentContext(request, error);
    if (!context)
        return std::nullopt;
    if (!context->complete || context->contextHash != active.plan.contextHash()
        || semanticRuntimeContextHash(*context) != active.plan.contextHash()
        || context->sessionGeneration != active.plan.sessionGeneration()
        || context->epoch != active.plan.epoch()
        || context->mappingDigest.value != active.plan.mappingDigest()
        || context->controllerMappingDigest.value != active.plan.mappingDigest()
        || context->actionDefinitionsDigest.value != active.plan.actionDefinitionsDigest()
        || !Core::validateSemanticOperationRequest(request, *context).accepted()) {
        if (error)
            *error = QStringLiteral("The verified action context changed.");
        return std::nullopt;
    }

    QList<Data::SemanticActionRuntimeState> actions;
    for (const Data::SemanticActionRuntimeState &action : context->actionStates) {
        if (action.target == request.target)
            actions.append(action);
    }
    if (actions.size() != 1
        || actions.constFirst().availability != Data::SemanticActionAvailability::Ready
        || actions.constFirst().actionBindingId != active.plan.actionBindingId()
        || actions.constFirst().actionDefinitionId != active.plan.actionDefinitionId()
        || actions.constFirst().actionDefinitionDigest.value
               != active.plan.actionDefinitionDigest()) {
        if (error)
            *error = QStringLiteral("The signed action is no longer executable.");
        return std::nullopt;
    }

    Core::ControllerConnectionProvider *provider = currentProvider(active.plan.scope(), error);
    if (!provider)
        return std::nullopt;
    if (provider != active.provider) {
        if (error)
            *error = QStringLiteral("The controller provider changed during execution.");
        return std::nullopt;
    }
    const Data::ControllerConnectionSnapshot connection = provider->connectionSnapshot();
    if (!isConnected(connection.state)
        || connection.scope != active.plan.scope()
        || connection.sessionGeneration != active.plan.sessionGeneration()
        || connection.readOnly || !connection.session || !connection.session->sessionId
        || !connection.session->ownsControlLease
        || connection.session->controlLeaseOwnerSessionId != connection.session->sessionId
        || !connection.controllerState
        || !allowedExecutionState(connection.controllerState->serviceState)
        || !provider->supportsRuntimeResources()
        || !provider->supportsRuntimeOutputTransactions()
        || !connection.capability || !connection.capability->runtimeResources
        || !connection.capability->runtimeOutputTransactions) {
        if (error)
            *error = QStringLiteral("The controller session, lease, or service state changed.");
        return std::nullopt;
    }
    if (actions.constFirst().requiresDc
        && (!connection.controllerState->busOperational
            || !connection.controllerState->distributedClocksLocked)) {
        if (error)
            *error = QStringLiteral("Distributed clocks are not locked for this action.");
        return std::nullopt;
    }
    return CurrentExecution{*context, connection, provider};
}

void SemanticRuntimeExecutorExecution::publishJournalResult(
    const SemanticOperationJournalResult &result, const QString &controllerId)
{
    if (result.record)
        emit q->operationChanged(result.record->request.operationId);
    if (result.disposition != SemanticOperationJournalDisposition::Replayed
        && result.disposition != SemanticOperationJournalDisposition::NotFound
        && !controllerId.isEmpty()) {
        emit q->auditChanged(controllerId);
    }
}

Data::SemanticOperationRecord SemanticRuntimeExecutorExecution::submit(
    const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeActor &actor)
{
    QString error;
    Data::SemanticRuntimeContext context;
    if (const auto current = currentContext(request, &error)) {
        context = *current;
    } else {
        context.controllerId = request.target.controllerId;
        context.scope = request.target.scope;
        context.detail = error;
    }
    const SemanticOperationJournalResult result = m_journal.submit(request, actor, context);
    publishJournalResult(result, request.target.controllerId);
    if (result.record)
        return *result.record;
    return q->Core::SemanticRuntimeService::submit(request, actor);
}

Data::SemanticOperationRecord SemanticRuntimeExecutorExecution::approve(
    const Data::SemanticOperationApprovalRequest &approval,
    const Data::SemanticRuntimeActor &actor)
{
    const std::optional<Data::SemanticOperationRecord> existing = m_journal.operation(
        approval.operationId);
    if (!existing)
        return q->Core::SemanticRuntimeService::approve(approval, actor);

    QString error;
    Data::SemanticRuntimeContext context;
    if (const auto current = currentContext(existing->request, &error)) {
        context = *current;
    } else {
        context.controllerId = existing->request.target.controllerId;
        context.scope = existing->request.target.scope;
        context.detail = error;
    }
    const SemanticOperationJournalResult result = m_journal.approve(approval, actor, context);
    publishJournalResult(result, existing->request.target.controllerId);
    if (result.record
        && result.record->state == Data::SemanticOperationState::Approved
        && result.accepted()) {
        enqueue(*result.record);
    }
    return result.record.value_or(*existing);
}

std::optional<Data::SemanticOperationRecord> SemanticRuntimeExecutorExecution::operation(
    const Data::SemanticOperationId &operationId) const
{
    return m_journal.operation(operationId);
}

QList<Data::SemanticRuntimeAuditEvent> SemanticRuntimeExecutorExecution::audit(
    const QString &controllerId, quint64 afterSequence) const
{
    return m_journal.audit(controllerId, afterSequence);
}

void SemanticRuntimeExecutorExecution::enqueue(const Data::SemanticOperationRecord &record)
{
    ControllerQueue &queue = m_queues[record.request.target.controllerId];
    if ((queue.active && queue.active->operationId == record.request.operationId)
        || queue.pending.contains(record.request.operationId)) {
        return;
    }
    queue.pending.append(record.request.operationId);
    QTimer::singleShot(0, q, [this, controllerId = record.request.target.controllerId] {
        startNext(controllerId);
    });
}

void SemanticRuntimeExecutorExecution::startNext(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || queue->active || queue->pending.isEmpty())
        return;

    const Data::SemanticOperationId operationId = queue->pending.takeFirst();
    const std::optional<Data::SemanticOperationRecord> record = m_journal.operation(operationId);
    if (!record || record->state != Data::SemanticOperationState::Approved) {
        QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
        return;
    }

    QString error;
    const std::optional<Data::SemanticRuntimeContext> context = currentContext(record->request, &error);
    const std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence
        = context ? currentEvidence(record->request, &error) : nullptr;
    Core::ControllerConnectionProvider *provider
        = context ? currentProvider(record->request.target.scope, &error) : nullptr;
    const Utils::Result<SemanticActionPlan> plan = [&]() -> Utils::Result<SemanticActionPlan> {
        if (!context || !evidence)
            return Utils::ResultError(conciseExecutionText(error));
        return buildSemanticActionPlan(*evidence, record->request, *context);
    }();
    if (!plan) {
        const SemanticOperationJournalResult failed = m_journal.transition(
            operationId,
            {
                Data::SemanticOperationState::Approved,
                Data::SemanticOperationState::Failed,
                executorActor(),
                QStringLiteral("action-plan-rejected"),
                conciseExecutionText(plan.error()),
                {},
                0,
                0,
            });
        publishJournalResult(failed, controllerId);
        QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
        return;
    }

    bool shapeValid = provider && plan->groups().size() == 1 && plan->steps().size() == 1
                      && plan->steps().constFirst().kind()
                             == SemanticActionPlanStepKind::WriteGroup
                      && !plan->steps().constFirst().completeGroupWrites().isEmpty()
                      && plan->steps().constFirst().completeGroupWrites().size() <= 64
                      && plan->groups().constFirst().consistencyGroupId()
                             == plan->steps().constFirst().consistencyGroupId()
                      && sameResourceIds(
                          plan->groups().constFirst().completeResourceIds(),
                          plan->steps().constFirst().completeGroupWrites());
    if (!shapeValid) {
        const SemanticOperationJournalResult failed = m_journal.transition(
            operationId,
            {
                Data::SemanticOperationState::Approved,
                Data::SemanticOperationState::Failed,
                executorActor(),
                QStringLiteral("action-shape-unsupported"),
                QStringLiteral(
                    "Only one complete signed output-group write is currently executable."),
                {},
                0,
                0,
            });
        publishJournalResult(failed, controllerId);
        QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
        return;
    }

    ActiveExecution active(operationId, record->actor, *plan, provider);
    for (const Data::RuntimeOutputValueWrite &write :
         active.plan.steps().constFirst().completeGroupWrites()) {
        active.resourceIds.append(write.resourceId);
    }
    queue->active.emplace(std::move(active));
    const SemanticOperationJournalResult executing = m_journal.transition(
        operationId,
        {
            Data::SemanticOperationState::Approved,
            Data::SemanticOperationState::Executing,
            executorActor(),
            QStringLiteral("execution-started"),
            QStringLiteral("Executing the verified signed action."),
            {},
            0,
            0,
        });
    publishJournalResult(executing, controllerId);
    if (!executing.accepted()) {
        finishActive(controllerId);
        return;
    }
    beginBeforeSnapshot(controllerId);
}

void SemanticRuntimeExecutorExecution::beginBeforeSnapshot(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }

    Data::RuntimeResourceSnapshotRequest request;
    request.correlationId = executionCorrelationId(active.operationId, u"before");
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.resourceIds = active.resourceIds;
    if (!request.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("before-snapshot-invalid"),
            QStringLiteral("The exact pre-operation snapshot request is invalid."));
        return;
    }
    active.phase = SemanticExecutionPhase::BeforeSnapshot;
    active.snapshotRequest = request;
    const Utils::Result<> started = current->provider->requestRuntimeResourceSnapshot(request);
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::BeforeSnapshot
            && check->active->snapshotRequest == request) {
            failActive(
                controllerId,
                QStringLiteral("before-snapshot-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

void SemanticRuntimeExecutorExecution::beginNextPolicy(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    if (active.policyIndex >= active.plan.groups().size()) {
        beginState(controllerId);
        return;
    }

    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    const SemanticActionPlanGroup &group = active.plan.groups().at(active.policyIndex);
    Data::RuntimeOutputGroupPolicyRequest request;
    request.correlationId = executionCorrelationId(
        active.operationId, u"policy", quint32(active.policyIndex));
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.consistencyGroupId = group.consistencyGroupId();
    request.expectedMappingDigest = active.plan.mappingDigest();
    if (!request.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("policy-request-invalid"),
            QStringLiteral("A signed output-group policy request is invalid."));
        return;
    }
    active.phase = SemanticExecutionPhase::Policy;
    active.policyRequest = request;
    const Utils::Result<> started = current->provider->requestRuntimeOutputGroupPolicy(request);
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::Policy
            && check->active->policyRequest == request) {
            failActive(
                controllerId,
                QStringLiteral("policy-request-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

void SemanticRuntimeExecutorExecution::beginState(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }

    Data::RuntimeOutputTransactionStateRequest request;
    request.correlationId = executionCorrelationId(active.operationId, u"state");
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.expectedMappingDigest = active.plan.mappingDigest();
    active.phase = SemanticExecutionPhase::State;
    active.stateRequest = request;
    const Utils::Result<> started
        = current->provider->requestRuntimeOutputTransactionState(request);
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::State
            && check->active->stateRequest == request) {
            failActive(
                controllerId,
                QStringLiteral("state-request-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

SemanticActionPlanGroup SemanticRuntimeExecutorExecution::groupFor(
    const ActiveExecution &active,
    const Data::RuntimeConsistencyGroupId &groupId,
    bool *found) const
{
    for (const SemanticActionPlanGroup &group : active.plan.groups()) {
        if (group.consistencyGroupId() == groupId) {
            if (found)
                *found = true;
            return group;
        }
    }
    if (found)
        *found = false;
    return {};
}

void SemanticRuntimeExecutorExecution::beginApply(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    for (auto other = m_queues.cbegin(); other != m_queues.cend(); ++other) {
        if (other.key() == controllerId || !other->active
            || !other->active->mutationMayHaveExecuted) {
            continue;
        }
        if (other->active->provider == current->provider
            || (active.providerId.isValid()
                && other->active->providerId == active.providerId)) {
            failActive(
                controllerId,
                QStringLiteral("provider-mutation-blocked"),
                QStringLiteral(
                    "An earlier output transaction on this provider is still unresolved."));
            return;
        }
    }

    const SemanticActionPlanStep &step = active.plan.steps().constFirst();
    bool found = false;
    const SemanticActionPlanGroup group = groupFor(
        active, step.consistencyGroupId(), &found);
    if (!found) {
        failActive(
            controllerId,
            QStringLiteral("signed-group-missing"),
            QStringLiteral("The signed output group is unavailable."));
        return;
    }

    Data::RuntimeOutputTransactionRequest request;
    request.operationId = step.outputOperationId();
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.expectedMappingDigest = active.plan.mappingDigest();
    request.expectedCompleteGroupRecordDigest = group.completeGroupRecordDigest();
    request.expectedCompleteResourceCount = group.completeResourceCount();
    request.expectedRecoveryPolicy = group.recoveryPolicy();
    request.expectedMaximumTtlCycles = group.maximumTtlCycles();
    request.expectedOutputGeneration = active.outputGeneration;
    request.ttlCycles = active.plan.ttlCycles();
    request.consistencyGroupId = group.consistencyGroupId();
    request.completeGroupWrites = step.completeGroupWrites();
    if (!request.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("output-request-invalid"),
            QStringLiteral("The complete signed output transaction is invalid."));
        return;
    }
    active.phase = SemanticExecutionPhase::Apply;
    active.applyRequest = request;
    // Once dispatch begins, a transport or decoder failure can no longer prove
    // that the controller did not accept the mutation.
    active.mutationMayHaveExecuted = true;
    const Data::SemanticOperationId semanticOperationId = active.operationId;
    const Utils::Result<> started = current->provider->applyRuntimeOutputTransaction(request);
    if (!started) {
        auto check = m_queues.find(controllerId);
        const auto record = m_journal.operation(semanticOperationId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::Apply
            && check->active->applyRequest == request && record
            && record->state == Data::SemanticOperationState::Executing
            && record->currentStep == 0) {
            check->active->mutationMayHaveExecuted = false;
            failActive(
                controllerId,
                QStringLiteral("output-request-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

void SemanticRuntimeExecutorExecution::beginAfterSnapshot(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }

    Data::RuntimeResourceSnapshotRequest request;
    request.correlationId = executionCorrelationId(active.operationId, u"after");
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.resourceIds = active.resourceIds;
    active.phase = SemanticExecutionPhase::AfterSnapshot;
    active.snapshotRequest = request;
    const Utils::Result<> started = current->provider->requestRuntimeResourceSnapshot(request);
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::AfterSnapshot
            && check->active->snapshotRequest == request) {
            failActive(
                controllerId,
                QStringLiteral("after-snapshot-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

void SemanticRuntimeExecutorExecution::beginAfterState(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("post-state-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    if (!active.pendingAfterSnapshot || !active.applyRequest) {
        failActive(
            controllerId,
            QStringLiteral("post-state-evidence-missing"),
            QStringLiteral("The post-operation state evidence is incomplete."));
        return;
    }

    Data::RuntimeOutputTransactionStateRequest request;
    request.correlationId = executionCorrelationId(active.operationId, u"post-state");
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.expectedMappingDigest = active.plan.mappingDigest();
    if (!request.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("post-state-request-invalid"),
            QStringLiteral("The post-operation state request is invalid."));
        return;
    }
    active.phase = SemanticExecutionPhase::AfterState;
    active.stateRequest = request;
    const Utils::Result<> started
        = current->provider->requestRuntimeOutputTransactionState(request);
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::AfterState
            && check->active->stateRequest == request) {
            failActive(
                controllerId,
                QStringLiteral("post-state-request-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

std::optional<Data::SemanticOperationSnapshot>
SemanticRuntimeExecutorExecution::operationSnapshot(
    const ActiveExecution &active,
    const CurrentExecution &current,
    const Data::RuntimeResourceSnapshot &snapshot,
    QString *error) const
{
    const std::optional<Data::RuntimeResourceCatalog> catalog
        = current.provider->runtimeResourceCatalog();
    if (!catalog || catalog->scope != active.plan.scope()
        || catalog->sessionGeneration != active.plan.sessionGeneration()
        || catalog->epoch != active.plan.epoch()) {
        if (error)
            *error = QStringLiteral("The runtime resource catalog changed.");
        return std::nullopt;
    }

    Data::SemanticOperationSnapshot result;
    result.captureCycle = snapshot.captureCycle;
    result.controllerTimestampNs = snapshot.controllerTimestampNs;
    result.complete = snapshot.complete;
    if (!result.captureCycle || !result.controllerTimestampNs) {
        if (error)
            *error = QStringLiteral("The runtime snapshot has no cycle or controller timestamp.");
        return std::nullopt;
    }
    for (const Data::RuntimeResourceSample &sample : snapshot.samples) {
        if (!sample.controllerTimestampNs
            || sample.controllerTimestampNs != snapshot.controllerTimestampNs) {
            if (error) {
                *error = QStringLiteral(
                    "The runtime samples are not from one consistent controller capture.");
            }
            return std::nullopt;
        }
        QList<const Data::SemanticSignalRuntimeState *> matches;
        for (const Data::SemanticSignalRuntimeState &state : current.context.signalStates) {
            if (state.binding && state.binding->resourceId == sample.resourceId)
                matches.append(&state);
        }
        if (matches.size() != 1 || !matches.constFirst()->binding) {
            if (error)
                *error = QStringLiteral("A snapshot resource has no unique signed signal binding.");
            return std::nullopt;
        }
        const Core::SemanticRuntimeReadValidation validation
            = Core::validateSemanticRuntimeRead(
                *matches.constFirst()->binding, *catalog, snapshot);
        if (!validation.validation.accepted() || !validation.sample
            || validation.sample->quality.state != Data::RuntimeResourceQualityState::Good) {
            if (error)
                *error = QStringLiteral("A snapshot resource is not fresh and good.");
            return std::nullopt;
        }
        Data::SemanticOperationSignalObservation observation;
        observation.target = matches.constFirst()->target;
        observation.value = validation.sample->value;
        observation.quality = validation.sample->quality;
        observation.controllerTimestampNs = validation.sample->controllerTimestampNs;
        result.observations.append(observation);
    }
    result.digest = semanticOperationSnapshotDigest(
        result, active.plan.epoch(), active.plan.sessionGeneration());
    if (!result.complete || !result.controllerTimestampNs || result.digest.size() != 32) {
        if (error)
            *error = QStringLiteral("The semantic snapshot evidence is incomplete.");
        return std::nullopt;
    }
    return result;
}

bool SemanticRuntimeExecutorExecution::afterSnapshotMatchesWrites(
    const ActiveExecution &active, const Data::RuntimeResourceSnapshot &snapshot) const
{
    const QList<Data::RuntimeOutputValueWrite> &writes
        = active.plan.steps().constFirst().completeGroupWrites();
    if (snapshot.samples.size() != writes.size())
        return false;
    for (qsizetype index = 0; index < writes.size(); ++index) {
        if (snapshot.samples.at(index).resourceId != writes.at(index).resourceId
            || snapshot.samples.at(index).value != writes.at(index).value) {
            return false;
        }
    }
    return true;
}

void SemanticRuntimeExecutorExecution::handleSnapshot(
    Core::ControllerConnectionProvider *provider,
    const Data::RuntimeResourceSnapshotResult &result)
{
    QList<QString> exactMatches;
    QList<QString> correlationMatches;
    for (auto candidate = m_queues.cbegin(); candidate != m_queues.cend(); ++candidate) {
        if (!candidate->active || candidate->active->provider != provider
            || (candidate->active->phase != SemanticExecutionPhase::BeforeSnapshot
                && candidate->active->phase != SemanticExecutionPhase::AfterSnapshot)
            || !candidate->active->snapshotRequest) {
            continue;
        }
        if (*candidate->active->snapshotRequest == result.request)
            exactMatches.append(candidate.key());
        else if (
            candidate->active->snapshotRequest->correlationId
            == result.request.correlationId) {
            correlationMatches.append(candidate.key());
        }
    }
    const QString controllerId
        = exactMatches.size() == 1
              ? exactMatches.constFirst()
              : (exactMatches.isEmpty() && correlationMatches.size() == 1
                     ? correlationMatches.constFirst()
                     : QString());
    if (controllerId.isEmpty())
        return;
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active || !queue->active->snapshotRequest)
        return;
    ActiveExecution &active = *queue->active;
    const Data::RuntimeResourceSnapshotRequest expected = *active.snapshotRequest;
    if (result.request.correlationId != expected.correlationId)
        return;
    if (result.request != expected || !result.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("snapshot-response-invalid"),
            QStringLiteral("The controller returned an invalid snapshot response."));
        return;
    }
    if (result.error) {
        failActive(
            controllerId,
            active.phase == SemanticExecutionPhase::BeforeSnapshot
                ? QStringLiteral("before-snapshot-failed")
                : QStringLiteral("after-snapshot-failed"),
            providerFailureText(QStringLiteral("Snapshot failed"), *result.error),
            result.error);
        return;
    }

    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!current || !result.snapshot) {
        failActive(
            controllerId,
            QStringLiteral("snapshot-context-changed"),
            conciseExecutionText(
                error.isEmpty() ? QStringLiteral("The snapshot is unavailable.") : error));
        return;
    }
    if (active.phase == SemanticExecutionPhase::AfterSnapshot
        && (!active.appliedCycle || !active.expiryCycle
            || result.snapshot->captureCycle < active.appliedCycle
            || result.snapshot->captureCycle >= active.expiryCycle)) {
        failActive(
            controllerId,
            QStringLiteral("after-snapshot-stale"),
            QStringLiteral("The post-operation snapshot is outside the active TTL window."));
        return;
    }
    const std::optional<Data::SemanticOperationSnapshot> snapshot
        = operationSnapshot(active, *current, *result.snapshot, &error);
    if (!snapshot) {
        failActive(
            controllerId,
            QStringLiteral("snapshot-verification-failed"),
            conciseExecutionText(error));
        return;
    }

    if (active.phase == SemanticExecutionPhase::BeforeSnapshot) {
        const SemanticOperationJournalResult recorded = m_journal.recordBeforeSnapshot(
            active.operationId,
            Data::SemanticOperationState::Executing,
            *snapshot,
            executorActor());
        publishJournalResult(recorded, controllerId);
        if (!recorded.accepted()) {
            failActive(
                controllerId,
                QStringLiteral("before-snapshot-record-failed"),
                QStringLiteral("The pre-operation snapshot could not be recorded."));
            return;
        }
        active.snapshotRequest.reset();
        beginNextPolicy(controllerId);
        return;
    }

    if (!afterSnapshotMatchesWrites(active, *result.snapshot)) {
        failActive(
            controllerId,
            QStringLiteral("after-snapshot-value-mismatch"),
            QStringLiteral("The post-operation values do not match the signed write."));
        return;
    }
    active.pendingAfterSnapshot = *snapshot;
    active.snapshotRequest.reset();
    beginAfterState(controllerId);
}

void SemanticRuntimeExecutorExecution::handlePolicy(
    Core::ControllerConnectionProvider *provider,
    const Data::RuntimeOutputGroupPolicyResult &result)
{
    QList<QString> exactMatches;
    QList<QString> correlationMatches;
    for (auto candidate = m_queues.cbegin(); candidate != m_queues.cend(); ++candidate) {
        if (!candidate->active || candidate->active->provider != provider
            || candidate->active->phase != SemanticExecutionPhase::Policy
            || !candidate->active->policyRequest) {
            continue;
        }
        if (*candidate->active->policyRequest == result.request)
            exactMatches.append(candidate.key());
        else if (
            candidate->active->policyRequest->correlationId
            == result.request.correlationId) {
            correlationMatches.append(candidate.key());
        }
    }
    const QString controllerId
        = exactMatches.size() == 1
              ? exactMatches.constFirst()
              : (exactMatches.isEmpty() && correlationMatches.size() == 1
                     ? correlationMatches.constFirst()
                     : QString());
    if (controllerId.isEmpty())
        return;
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active || !queue->active->policyRequest)
        return;
    ActiveExecution &active = *queue->active;
    const Data::RuntimeOutputGroupPolicyRequest expected = *active.policyRequest;
    if (result.request.correlationId != expected.correlationId)
        return;
    if (result.request != expected || !result.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("policy-response-invalid"),
            QStringLiteral("The controller returned an invalid output policy."));
        return;
    }
    if (result.error) {
        failActive(
            controllerId,
            QStringLiteral("policy-request-failed"),
            providerFailureText(QStringLiteral("Output policy failed"), *result.error),
            result.error);
        return;
    }

    QString error;
    if (!validateCurrentExecution(active, &error) || !result.policy
        || active.policyIndex >= active.plan.groups().size()) {
        failActive(
            controllerId,
            QStringLiteral("policy-context-changed"),
            conciseExecutionText(
                error.isEmpty() ? QStringLiteral("The signed policy is unavailable.") : error));
        return;
    }
    const SemanticActionPlanGroup &group = active.plan.groups().at(active.policyIndex);
    const Data::RuntimeOutputGroupPolicy &policy = *result.policy;
    if (policy.consistencyGroupId != group.consistencyGroupId()
        || policy.completeGroupRecordDigest != group.completeGroupRecordDigest()
        || policy.recoveryPolicy != group.recoveryPolicy()
        || policy.maximumTtlCycles != group.maximumTtlCycles()
        || policy.completeResourceCount != group.completeResourceCount()
        || policy.mappingDigest != active.plan.mappingDigest()
        || !policy.manualWriteAllowed
        || (active.outputGeneration
            && active.outputGeneration != policy.currentOutputGeneration)) {
        failActive(
            controllerId,
            QStringLiteral("policy-proof-mismatch"),
            QStringLiteral("The controller output policy differs from signed package evidence."));
        return;
    }
    active.outputGeneration = policy.currentOutputGeneration;
    active.policies.append(policy);
    ++active.policyIndex;
    active.policyRequest.reset();
    beginNextPolicy(controllerId);
}

void SemanticRuntimeExecutorExecution::handleState(
    Core::ControllerConnectionProvider *provider,
    const Data::RuntimeOutputTransactionStateResult &result)
{
    QList<QString> exactMatches;
    QList<QString> correlationMatches;
    for (auto candidate = m_queues.cbegin(); candidate != m_queues.cend(); ++candidate) {
        if (!candidate->active || candidate->active->provider != provider
            || (candidate->active->phase != SemanticExecutionPhase::State
                && candidate->active->phase != SemanticExecutionPhase::AfterState)
            || !candidate->active->stateRequest) {
            continue;
        }
        if (*candidate->active->stateRequest == result.request)
            exactMatches.append(candidate.key());
        else if (
            candidate->active->stateRequest->correlationId
            == result.request.correlationId) {
            correlationMatches.append(candidate.key());
        }
    }
    const QString controllerId
        = exactMatches.size() == 1
              ? exactMatches.constFirst()
              : (exactMatches.isEmpty() && correlationMatches.size() == 1
                     ? correlationMatches.constFirst()
                     : QString());
    if (controllerId.isEmpty())
        return;
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active || !queue->active->stateRequest)
        return;
    ActiveExecution &active = *queue->active;
    const Data::RuntimeOutputTransactionStateRequest expected = *active.stateRequest;
    if (result.request.correlationId != expected.correlationId)
        return;
    if (result.request != expected || !result.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("state-response-invalid"),
            QStringLiteral("The controller returned an invalid output state."));
        return;
    }
    if (result.error) {
        failActive(
            controllerId,
            active.phase == SemanticExecutionPhase::AfterState
                ? QStringLiteral("post-state-request-failed")
                : QStringLiteral("state-request-failed"),
            providerFailureText(
                active.phase == SemanticExecutionPhase::AfterState
                    ? QStringLiteral("Post-operation state failed")
                    : QStringLiteral("Output state failed"),
                *result.error),
            result.error);
        return;
    }

    if (active.phase == SemanticExecutionPhase::AfterState) {
        if (!result.state || !active.applyRequest || !active.pendingAfterSnapshot) {
            failActive(
                controllerId,
                QStringLiteral("post-state-evidence-missing"),
                QStringLiteral("The post-operation state evidence is incomplete."));
            return;
        }
        const Data::RuntimeOutputTransactionState &state = *result.state;
        const Data::RuntimeOutputTransactionRequest &apply = *active.applyRequest;
        const bool provesActiveOverride
            = state.operationId && *state.operationId == apply.operationId
              && state.state == Data::RuntimeOutputState::OverrideActive
              && state.outputGeneration == active.appliedOutputGeneration
              && state.appliedCycle == active.appliedCycle
              && state.expiryCycle == active.expiryCycle
              && state.consistencyGroupId == apply.consistencyGroupId
              && state.ttlCycles == apply.ttlCycles
              && state.recoveryPolicy == apply.expectedRecoveryPolicy
              && state.valueCount == quint16(apply.completeGroupWrites.size())
              && state.controllerTimestampNs
                     >= active.pendingAfterSnapshot->controllerTimestampNs;
        if (!provesActiveOverride) {
            const bool provesRecovered
                = state.operationId && *state.operationId == apply.operationId
                  && state.outputGeneration == apply.expectedOutputGeneration + 2
                  && state.appliedCycle == active.appliedCycle
                  && state.expiryCycle == active.expiryCycle
                  && state.consistencyGroupId == apply.consistencyGroupId
                  && state.ttlCycles == apply.ttlCycles
                  && state.recoveryPolicy == apply.expectedRecoveryPolicy
                  && state.valueCount == quint16(apply.completeGroupWrites.size())
                  && state.controllerTimestampNs
                         >= active.pendingAfterSnapshot->controllerTimestampNs
                  && ((apply.expectedRecoveryPolicy
                           == Data::RuntimeOutputRecoveryPolicy::ReturnTask
                       && state.state == Data::RuntimeOutputState::Idle)
                      || (apply.expectedRecoveryPolicy
                              == Data::RuntimeOutputRecoveryPolicy::HoldSafe
                          && state.state == Data::RuntimeOutputState::SafeHold));
            if (provesRecovered) {
                expireActive(
                    controllerId,
                    QStringLiteral(
                        "The output override expired before post-operation confirmation."));
            } else {
                failActive(
                    controllerId,
                    QStringLiteral("post-state-proof-mismatch"),
                    QStringLiteral(
                        "The controller did not prove the signed override is still active."));
            }
            return;
        }

        const SemanticOperationJournalResult recorded = m_journal.recordAfterSnapshot(
            active.operationId,
            active.completionState,
            *active.pendingAfterSnapshot,
            executorActor());
        publishJournalResult(recorded, controllerId);
        if (!recorded.accepted()) {
            failActive(
                controllerId,
                QStringLiteral("after-snapshot-record-failed"),
                QStringLiteral("The post-operation snapshot could not be recorded."));
            return;
        }
        active.stateRequest.reset();
        succeedActive(controllerId);
        return;
    }

    QString error;
    if (!validateCurrentExecution(active, &error) || !result.state
        || result.state->outputGeneration != active.outputGeneration) {
        failActive(
            controllerId,
            QStringLiteral("output-generation-changed"),
            conciseExecutionText(
                error.isEmpty()
                    ? QStringLiteral("The output generation changed during policy preflight.")
                    : error));
        return;
    }
    active.stateRequest.reset();
    beginApply(controllerId);
}

void SemanticRuntimeExecutorExecution::handleApply(
    Core::ControllerConnectionProvider *provider,
    const Data::RuntimeOutputTransactionResult &result)
{
    QList<QString> exactMatches;
    QList<QString> operationMatches;
    for (auto candidate = m_queues.cbegin(); candidate != m_queues.cend(); ++candidate) {
        if (!candidate->active || candidate->active->provider != provider
            || candidate->active->phase != SemanticExecutionPhase::Apply
            || !candidate->active->applyRequest) {
            continue;
        }
        if (*candidate->active->applyRequest == result.request)
            exactMatches.append(candidate.key());
        else if (
            candidate->active->applyRequest->operationId
            == result.request.operationId) {
            operationMatches.append(candidate.key());
        }
    }
    const QString controllerId
        = exactMatches.size() == 1
              ? exactMatches.constFirst()
              : (exactMatches.isEmpty() && operationMatches.size() == 1
                     ? operationMatches.constFirst()
                     : QString());
    if (controllerId.isEmpty())
        return;
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active || !queue->active->applyRequest)
        return;
    ActiveExecution &active = *queue->active;
    const Data::RuntimeOutputTransactionRequest expected = *active.applyRequest;
    if (result.request != expected || !result.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("output-response-invalid"),
            QStringLiteral("The controller returned an invalid output result."));
        return;
    }
    const auto currentRecord = m_journal.operation(active.operationId);
    if (!currentRecord)
        return;
    if (result.outcome == Data::RuntimeOutputTransactionOutcome::OutcomeUnknown) {
        freezeUnknown(
            controllerId,
            QStringLiteral("output-outcome-unknown"),
            providerFailureText(QStringLiteral("Output result is unknown"), *result.error),
            result.error);
        return;
    }

    const Data::SemanticOperationState state = currentRecord->state;
    if (state != Data::SemanticOperationState::Executing
        && state != Data::SemanticOperationState::OutcomeUnknown) {
        return;
    }
    if (state == Data::SemanticOperationState::Executing && currentRecord->currentStep == 0) {
        const bool failed
            = result.outcome == Data::RuntimeOutputTransactionOutcome::Rejected;
        const SemanticOperationJournalResult step = m_journal.recordStep(
            active.operationId,
            {
                Data::SemanticOperationState::Executing,
                executorActor(),
                active.plan.steps().constFirst().index() + 1,
                quint32(active.plan.steps().size()),
                failed,
                failed ? providerFailureText(
                             QStringLiteral("Output transaction rejected"), *result.error)
                       : QStringLiteral("The complete signed output group was applied."),
                result.error,
            });
        publishJournalResult(step, controllerId);
        if (!step.accepted()) {
            failActive(
                controllerId,
                QStringLiteral("output-step-record-failed"),
                QStringLiteral("The output execution step could not be recorded."));
            return;
        }
    }

    if (result.outcome == Data::RuntimeOutputTransactionOutcome::Rejected) {
        // A valid terminal rejection proves that the atomic mutation was not
        // applied, so the queue can safely advance after recording failure.
        active.mutationMayHaveExecuted = false;
        failActive(
            controllerId,
            QStringLiteral("output-transaction-rejected"),
            providerFailureText(QStringLiteral("Output transaction rejected"), *result.error),
            result.error);
        return;
    }
    if (result.outcome == Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered) {
        active.appliedCycle = result.state ? result.state->appliedCycle : 0;
        active.expiryCycle = result.state ? result.state->expiryCycle : 0;
        active.appliedOutputGeneration = result.state ? result.state->outputGeneration : 0;
        expireActive(
            controllerId,
            QStringLiteral("The output override expired before completion was observed."));
        return;
    }
    if (result.outcome != Data::RuntimeOutputTransactionOutcome::Applied || !result.state) {
        failActive(
            controllerId,
            QStringLiteral("output-result-unsupported"),
            QStringLiteral("The controller returned an unsupported output result."));
        return;
    }

    active.appliedCycle = result.state->appliedCycle;
    active.expiryCycle = result.state->expiryCycle;
    active.appliedOutputGeneration = result.state->outputGeneration;
    active.completionState = state;
    beginAfterSnapshot(controllerId);
}

void SemanticRuntimeExecutorExecution::failActive(
    const QString &controllerId,
    const QString &code,
    const QString &detail,
    const std::optional<Data::ControllerOperationError> &error)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    const ActiveExecution &active = *queue->active;
    if (active.mutationMayHaveExecuted) {
        freezeUnknown(controllerId, code, detail, error);
        return;
    }
    const auto record = m_journal.operation(active.operationId);
    if (record
        && (record->state == Data::SemanticOperationState::Executing
            || record->state == Data::SemanticOperationState::OutcomeUnknown)) {
        const SemanticOperationJournalResult failed = m_journal.transition(
            active.operationId,
            {
                record->state,
                Data::SemanticOperationState::Failed,
                executorActor(),
                code,
                conciseExecutionText(detail),
                error,
                active.appliedCycle,
                active.plan.epoch().runtimeGeneration,
            });
        publishJournalResult(failed, controllerId);
    }
    finishActive(controllerId);
}

void SemanticRuntimeExecutorExecution::freezeUnknown(
    const QString &controllerId,
    const QString &code,
    const QString &detail,
    const std::optional<Data::ControllerOperationError> &error)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    const auto record = m_journal.operation(active.operationId);
    if (!record)
        return;
    if (record->state == Data::SemanticOperationState::OutcomeUnknown)
        return;
    if (record->state != Data::SemanticOperationState::Executing)
        return;

    if (record->currentStep == 0) {
        const SemanticOperationJournalResult step = m_journal.recordStep(
            active.operationId,
            {
                Data::SemanticOperationState::Executing,
                executorActor(),
                active.plan.steps().constFirst().index() + 1,
                quint32(active.plan.steps().size()),
                false,
                QStringLiteral("Waiting for the same output transaction to be reconciled."),
                error,
            });
        publishJournalResult(step, controllerId);
        if (!step.accepted())
            return;
    }
    const SemanticOperationJournalResult unknown = m_journal.transition(
        active.operationId,
        {
            Data::SemanticOperationState::Executing,
            Data::SemanticOperationState::OutcomeUnknown,
            executorActor(),
            code,
            conciseExecutionText(detail),
            error,
            active.appliedCycle,
            active.plan.epoch().runtimeGeneration,
        });
    publishJournalResult(unknown, controllerId);
    // Keep the active request and output OperationId intact. No later queued
    // mutation is allowed until the same transaction is authoritatively
    // reconciled.
}

void SemanticRuntimeExecutorExecution::expireActive(
    const QString &controllerId, const QString &detail)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    const ActiveExecution &active = *queue->active;
    const auto record = m_journal.operation(active.operationId);
    if (!record)
        return;
    const SemanticOperationJournalResult expired = m_journal.transition(
        active.operationId,
        {
            record->state,
            Data::SemanticOperationState::Expired,
            executorActor(),
            QStringLiteral("output-override-expired"),
            conciseExecutionText(detail),
            {},
            active.appliedCycle,
            active.plan.epoch().runtimeGeneration,
        });
    publishJournalResult(expired, controllerId);
    if (!expired.accepted()
        && (!expired.record
            || expired.record->state != Data::SemanticOperationState::Expired)) {
        return;
    }
    finishActive(controllerId);
}

void SemanticRuntimeExecutorExecution::succeedActive(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    const ActiveExecution &active = *queue->active;
    const auto record = m_journal.operation(active.operationId);
    if (!record)
        return;
    const SemanticOperationJournalResult succeeded = m_journal.transition(
        active.operationId,
        {
            record->state,
            Data::SemanticOperationState::Succeeded,
            executorActor(),
            QStringLiteral("action-succeeded"),
            QStringLiteral("The signed action completed and was verified."),
            {},
            active.appliedCycle,
            active.plan.epoch().runtimeGeneration,
        });
    publishJournalResult(succeeded, controllerId);
    if (!succeeded.accepted()
        && (!succeeded.record
            || succeeded.record->state != Data::SemanticOperationState::Succeeded)) {
        return;
    }
    finishActive(controllerId);
}

void SemanticRuntimeExecutorExecution::finishActive(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end())
        return;
    queue->active.reset();
    QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
}

void SemanticRuntimeExecutorExecution::providerRemoved(
    Core::ControllerConnectionProvider *provider)
{
    QList<QString> affected;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active
            && (queue->active->provider == provider || queue->active->provider.isNull())) {
            affected.append(queue.key());
        }
    }
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("controller-provider-removed"),
            QStringLiteral("The controller provider was removed during execution."));
    }
}

void SemanticRuntimeExecutorExecution::providerRegistryRemoved()
{
    QList<QString> affected;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active)
            affected.append(queue.key());
    }
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("provider-registry-removed"),
            QStringLiteral("The controller provider registry was removed during execution."));
    }
}

void SemanticRuntimeExecutorExecution::projectServiceRemoved()
{
    QList<QString> affected;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active)
            affected.append(queue.key());
    }
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("project-service-removed"),
            QStringLiteral("The project service was removed during execution."));
    }
}

SemanticRuntimeExecutor::~SemanticRuntimeExecutor() = default;

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
    m_execution = std::make_unique<SemanticRuntimeExecutorExecution>(this);

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
            m_execution->projectServiceRemoved();
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
                    m_execution->providerRemoved(connectionProvider);
                }
                untrackProvider(provider);
                publishContexts();
            });
        connect(m_providerRegistry, &QObject::destroyed, this, [this] {
            m_execution->providerRegistryRemoved();
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

Data::SemanticOperationRecord SemanticRuntimeExecutor::submit(
    const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeActor &actor)
{
    return m_execution->submit(request, actor);
}

Data::SemanticOperationRecord SemanticRuntimeExecutor::approve(
    const Data::SemanticOperationApprovalRequest &approval,
    const Data::SemanticRuntimeActor &actor)
{
    return m_execution->approve(approval, actor);
}

std::optional<Data::SemanticOperationRecord> SemanticRuntimeExecutor::operation(
    const Data::SemanticOperationId &operationId) const
{
    return m_execution->operation(operationId);
}

QList<Data::SemanticRuntimeAuditEvent> SemanticRuntimeExecutor::audit(
    const QString &controllerId, quint64 afterSequence) const
{
    return m_execution->audit(controllerId, afterSequence);
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
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished,
        this,
        [this, connectionProvider](const Data::RuntimeResourceSnapshotResult &result) {
            m_execution->handleSnapshot(connectionProvider, result);
        }));
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
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished,
        this,
        [this, connectionProvider](const Data::RuntimeOutputGroupPolicyResult &result) {
            m_execution->handlePolicy(connectionProvider, result);
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished,
        this,
        [this, connectionProvider](const Data::RuntimeOutputTransactionStateResult &result) {
            m_execution->handleState(connectionProvider, result);
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished,
        this,
        [this, connectionProvider](const Data::RuntimeOutputTransactionResult &result) {
            m_execution->handleApply(connectionProvider, result);
        }));
    connections.append(
        connect(connectionProvider, &QObject::destroyed, this, [this, connectionProvider] {
            m_execution->providerRemoved(connectionProvider);
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
