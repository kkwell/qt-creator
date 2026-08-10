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
#include <limits>
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

static bool explicitProjectDeviceTopologyMatches(
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
            || slave->position != int(signedDevice->position) || !slave->stationAddress
            || slave->stationAddress != signedDevice->stationAddress) {
            if (detail) {
                *detail = QStringLiteral(
                    "An explicit project-device mapping does not match its signed bus position "
                    "or station address.");
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

static bool runtimeBootstrapOperationInProgress(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    if (snapshot.controlProgress.state == Data::ControllerControlState::Pending)
        return true;

    switch (snapshot.packageDeploymentProgress.state) {
    case Data::ControllerPackageDeploymentState::Uploading:
    case Data::ControllerPackageDeploymentState::Committing:
    case Data::ControllerPackageDeploymentState::Validating:
    case Data::ControllerPackageDeploymentState::Activating:
    case Data::ControllerPackageDeploymentState::RollingBack:
    case Data::ControllerPackageDeploymentState::Canceling:
        return true;
    case Data::ControllerPackageDeploymentState::Idle:
    case Data::ControllerPackageDeploymentState::Succeeded:
    case Data::ControllerPackageDeploymentState::Canceled:
    case Data::ControllerPackageDeploymentState::Failed:
    case Data::ControllerPackageDeploymentState::OutcomeUnknown:
        return false;
    }
    return true;
}

static QByteArray runtimeBootstrapIdentityKey(
    const Data::ControllerConnectionScope &scope,
    const Data::ControllerConnectionSnapshot &snapshot,
    const Data::SemanticBindingArtifactReference &reference,
    const VerifiedRuntimePackageEvidence &evidence)
{
    QByteArray canonical = QByteArrayLiteral("embed-labs.semantic-runtime-bootstrap.v1");
    appendContextString(canonical, scope.projectId.toString());
    appendContextString(canonical, scope.masterId.toString());
    appendContextInteger(canonical, snapshot.sessionGeneration);
    appendContextInteger(canonical, snapshot.session ? snapshot.session->bootId : 0);
    appendContextString(canonical, reference.artifactId);
    appendContextBytes(canonical, reference.artifactSha256);
    appendContextBytes(canonical, reference.projectConfigurationSha256);

    const Data::RuntimeSemanticMappingProof &proof = evidence.semanticMappingProof();
    appendContextInteger(canonical, proof.formatVersion);
    appendContextInteger(canonical, proof.bindingCount);
    appendContextInteger(canonical, proof.packageSigned ? 1 : 0);
    appendContextInteger(canonical, proof.signatureVerified ? 1 : 0);
    appendContextInteger(canonical, proof.semanticBindingVerified ? 1 : 0);
    appendContextInteger(canonical, quint64(proof.trust));
    appendContextBytes(canonical, proof.packageSha256);
    appendContextBytes(canonical, proof.manifestSha256);
    appendContextBytes(canonical, proof.mappingSha256);
    appendContextBytes(canonical, proof.resourceRecordsSha256);
    appendContextBytes(canonical, proof.resourceSectionSha256);
    appendContextBytes(canonical, proof.topologySha256);
    appendContextBytes(canonical, proof.signingKeyIdSha256);

    if (snapshot.package) {
        appendContextInteger(canonical, quint64(snapshot.package->activeSlot));
        appendContextInteger(canonical, snapshot.package->activeGeneration);
        appendContextInteger(canonical, snapshot.package->activeConfigurationId);
        appendContextInteger(canonical, snapshot.package->controllerBootId);
    } else {
        appendContextInteger(canonical, 0);
        appendContextInteger(canonical, 0);
        appendContextInteger(canonical, 0);
        appendContextInteger(canonical, 0);
    }

    if (snapshot.controllerState) {
        appendContextInteger(canonical, snapshot.controllerState->applicationActive ? 1 : 0);
        appendContextInteger(canonical, snapshot.controllerState->busOperational ? 1 : 0);
        appendContextInteger(canonical, snapshot.controllerState->paused ? 1 : 0);
        appendContextInteger(canonical, snapshot.controllerState->controllerBootId);
    } else {
        appendContextInteger(canonical, 0);
        appendContextInteger(canonical, 0);
        appendContextInteger(canonical, 0);
        appendContextInteger(canonical, 0);
    }
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

static QByteArray runtimeBootstrapAttestationKey(
    QByteArrayView identityKey,
    const Data::RuntimeResourceCatalogEpoch &epoch,
    const Data::RuntimeSemanticMappingProof &proof)
{
    QByteArray canonical = QByteArrayLiteral("embed-labs.semantic-runtime-attestation.v1");
    appendContextBytes(canonical, identityKey);
    appendContextInteger(canonical, epoch.controllerBootId);
    appendContextInteger(canonical, quint64(epoch.activePackageSlot));
    appendContextInteger(canonical, epoch.activePackageGeneration);
    appendContextInteger(canonical, epoch.configurationId);
    appendContextInteger(canonical, epoch.topologyGeneration);
    appendContextInteger(canonical, epoch.runtimeGeneration);
    appendContextInteger(canonical, epoch.catalogRevision);
    appendContextBytes(canonical, epoch.topologyIdentity);
    appendContextBytes(canonical, proof.mappingSha256);
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
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
    QByteArray canonical = QByteArrayLiteral("embed-labs.semantic-runtime-context.v3");
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
        appendContextInteger(canonical, action.maximumTtlMs);
        appendContextInteger(canonical, action.maximumTtlCycles);
        appendContextInteger(canonical, action.definition.requiresDc ? 1 : 0);
        appendContextInteger(canonical, action.definition.holdToRun ? 1 : 0);
        appendContextInteger(canonical, action.definition.commandTtlMs);

        const auto appendFallbackIds =
            [&canonical](const QList<Data::SemanticActionId> &ids) {
                QStringList values;
                values.reserve(ids.size());
                for (const Data::SemanticActionId &id : ids)
                    values.append(id.value);
                std::sort(values.begin(), values.end());
                appendContextInteger(canonical, quint64(values.size()));
                for (const QString &value : std::as_const(values))
                    appendContextString(canonical, value);
            };
        appendFallbackIds(action.definition.allowedReleaseActionIds);
        appendFallbackIds(action.definition.allowedTimeoutActionIds);
        appendFallbackIds(action.definition.allowedFailureActionIds);

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

AvailableDeviceAdapterProviderList availableDeviceAdapterProviderList(
    const QPointer<Core::ProviderRegistry> &registry)
{
    return [registry] {
        QList<Core::DeviceAdapterProvider *> result;
        if (!registry)
            return result;
        const QList<Core::Provider *> providers = registry->providers(
            Core::ProviderKind::DeviceAdapter);
        if (!registry)
            return QList<Core::DeviceAdapterProvider *>{};
        QList<QPointer<Core::DeviceAdapterProvider>> guardedProviders;
        guardedProviders.reserve(providers.size());
        for (Core::Provider *provider : providers) {
            auto *adapterProvider = qobject_cast<Core::DeviceAdapterProvider *>(provider);
            QPointer<Core::DeviceAdapterProvider> guardedProvider(adapterProvider);
            if (!guardedProvider)
                return QList<Core::DeviceAdapterProvider *>{};
            guardedProviders.append(guardedProvider);
        }
        for (const QPointer<Core::DeviceAdapterProvider> &guardedProvider :
             std::as_const(guardedProviders)) {
            if (!guardedProvider)
                return QList<Core::DeviceAdapterProvider *>{};
            const bool available = guardedProvider->isAvailable();
            if (!guardedProvider)
                return QList<Core::DeviceAdapterProvider *>{};
            if (available)
                result.append(guardedProvider.data());
        }
        return result;
    };
}

enum class SemanticExecutionPhase {
    BeforeSnapshot,
    Policy,
    State,
    Apply,
    AfterSnapshot,
    AfterState,
    WaitSnapshot,
    RecoveryState,
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
    const Data::SemanticOperationId &operationId,
    QStringView phase,
    quint32 stepIndex = 0,
    quint32 attempt = 0)
{
    QByteArray material = operationId.value.toUtf8();
    material.append('\0');
    material.append(phase.toUtf8());
    material.append('\0');
    const quint32 bigEndianIndex = qToBigEndian(stepIndex);
    material.append(
        reinterpret_cast<const char *>(&bigEndianIndex), qsizetype(sizeof(bigEndianIndex)));
    const quint32 bigEndianAttempt = qToBigEndian(attempt);
    material.append(
        reinterpret_cast<const char *>(&bigEndianAttempt), qsizetype(sizeof(bigEndianAttempt)));
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

bool snapshotHasExactResourceIds(
    const Data::RuntimeResourceSnapshot &snapshot,
    const QList<Data::RuntimeResourceId> &resourceIds)
{
    if (!snapshot.complete || snapshot.samples.size() != resourceIds.size())
        return false;

    QList<QByteArray> expected;
    expected.reserve(resourceIds.size());
    for (const Data::RuntimeResourceId &resourceId : resourceIds) {
        if (!resourceId.isValid())
            return false;
        expected.append(resourceId.value);
    }
    std::sort(expected.begin(), expected.end());
    if (std::adjacent_find(expected.cbegin(), expected.cend()) != expected.cend())
        return false;

    QList<QByteArray> actual;
    actual.reserve(snapshot.samples.size());
    for (const Data::RuntimeResourceSample &sample : snapshot.samples) {
        if (!sample.resourceId.isValid())
            return false;
        actual.append(sample.resourceId.value);
    }
    std::sort(actual.begin(), actual.end());
    return actual == expected
           && std::adjacent_find(actual.cbegin(), actual.cend()) == actual.cend();
}

bool snapshotMatchesWrites(
    const Data::RuntimeResourceSnapshot &snapshot,
    const QList<Data::RuntimeOutputValueWrite> &writes)
{
    for (const Data::RuntimeOutputValueWrite &write : writes) {
        const auto sample = std::find_if(
            snapshot.samples.cbegin(),
            snapshot.samples.cend(),
            [&write](const Data::RuntimeResourceSample &candidate) {
                return candidate.resourceId == write.resourceId;
            });
        if (sample == snapshot.samples.cend() || sample->value != write.value)
            return false;
    }
    return true;
}

bool waitConditionMatches(
    const SemanticActionPlanStep &step,
    const Data::RuntimeResourceSample &sample)
{
    if (!step.waitBinding() || sample.resourceId != step.waitBinding()->resourceId
        || sample.value.primitiveType != step.waitBinding()->primitiveType
        || sample.value.typeIdentity != step.waitBinding()->valueTypeIdentity
        || !sample.value.opaqueRepresentation.isEmpty()) {
        return false;
    }

    const quint32 bitWidth = step.waitBinding()->bitWidth;
    if (!bitWidth || bitWidth > 64)
        return false;

    if (step.kind() == SemanticActionPlanStepKind::WaitMasked) {
        quint64 bits = 0;
        switch (sample.value.primitiveType) {
        case Data::RuntimeResourcePrimitiveType::Boolean:
            if (bitWidth != 1 || sample.value.value.metaType().id() != QMetaType::Bool)
                return false;
            bits = sample.value.value.toBool() ? 1 : 0;
            break;
        case Data::RuntimeResourcePrimitiveType::SignedInteger:
            if (sample.value.value.metaType().id() != QMetaType::LongLong)
                return false;
            bits = quint64(sample.value.value.toLongLong());
            if (bitWidth < 64)
                bits &= (quint64(1) << bitWidth) - 1;
            break;
        case Data::RuntimeResourcePrimitiveType::UnsignedInteger:
            if (sample.value.value.metaType().id() != QMetaType::ULongLong)
                return false;
            bits = sample.value.value.toULongLong();
            break;
        case Data::RuntimeResourcePrimitiveType::Opaque:
        case Data::RuntimeResourcePrimitiveType::FloatingPoint:
        case Data::RuntimeResourcePrimitiveType::Text:
        case Data::RuntimeResourcePrimitiveType::ByteArray:
            return false;
        }
        return (bits & step.mask()) == step.expectedValue();
    }

    if (step.kind() != SemanticActionPlanStepKind::WaitAbsoluteLimit
        || sample.value.primitiveType != Data::RuntimeResourcePrimitiveType::SignedInteger
        || sample.value.value.metaType().id() != QMetaType::LongLong) {
        return false;
    }
    const qint64 signedValue = sample.value.value.toLongLong();
    const quint64 magnitude = signedValue >= 0
                                  ? quint64(signedValue)
                                  : quint64(-(signedValue + 1)) + 1;
    return magnitude <= step.absoluteLimit();
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
    Data::SemanticLiveRefreshResult requestLiveRefresh(
        const Data::SemanticLiveRefreshRequest &request);

    bool handleLiveSnapshot(
        Core::ControllerConnectionProvider *provider,
        const Data::RuntimeResourceSnapshotResult &result);
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
    void projectRemoved(const Data::NodeId &projectId);
    void projectServiceRemoved();

private:
    struct ActiveExecution
    {
        ActiveExecution(
            const Data::SemanticOperationId &id,
            const Data::SemanticRuntimeActor &requestActor,
            SemanticActionPlan actionPlan,
            Core::ControllerConnectionProvider *connectionProvider,
            std::optional<SemanticActionAdapterAuthorizationAdmission> adapterAdmission)
            : operationId(id)
            , actor(requestActor)
            , plan(std::move(actionPlan))
            , provider(connectionProvider)
            , providerId(connectionProvider ? connectionProvider->id() : Utils::Id())
            , adapterAuthorization(std::move(adapterAdmission))
        {}

        Data::SemanticOperationId operationId;
        Data::SemanticRuntimeActor actor;
        SemanticActionPlan plan;
        QPointer<Core::ControllerConnectionProvider> provider;
        Utils::Id providerId;
        std::optional<SemanticActionAdapterAuthorizationAdmission> adapterAuthorization;
        SemanticExecutionPhase phase = SemanticExecutionPhase::BeforeSnapshot;
        QList<Data::RuntimeResourceId> resourceIds;
        qsizetype policyIndex = 0;
        QList<Data::RuntimeOutputGroupPolicy> policies;
        qsizetype stepIndex = 0;
        quint32 attempt = 0;
        quint32 waitAttempts = 0;
        quint64 waitDeadlineCycle = 0;
        quint64 lastCaptureCycle = 0;
        quint64 outputGeneration = 0;
        quint64 appliedCycle = 0;
        quint64 expiryCycle = 0;
        quint64 appliedOutputGeneration = 0;
        quint64 appliedControllerTimestampNs = 0;
        bool mutationMayHaveExecuted = false;
        bool applyOutcomePending = false;
        Data::SemanticOperationState completionState = Data::SemanticOperationState::Executing;
        Data::SemanticOperationState recoveryState = Data::SemanticOperationState::Failed;
        QString recoveryCode;
        QString recoveryDetail;
        std::optional<Data::ControllerOperationError> recoveryError;
        quint32 recoveryAttempts = 0;
        std::optional<Data::RuntimeResourceSnapshotRequest> snapshotRequest;
        std::optional<Data::RuntimeOutputGroupPolicyRequest> policyRequest;
        std::optional<Data::RuntimeOutputTransactionStateRequest> stateRequest;
        std::optional<Data::RuntimeOutputTransactionRequest> applyRequest;
        std::optional<Data::RuntimeOutputTransactionRequest> lastAppliedRequest;
        std::optional<Data::SemanticOperationSnapshot> pendingAfterSnapshot;
    };

    struct ControllerQueue
    {
        struct LiveRefresh
        {
            Data::SemanticLiveRefreshRequest request;
            Data::RuntimeResourceSnapshotRequest providerRequest;
            QList<Data::SemanticRuntimeBinding> bindings;
            QPointer<Core::ControllerConnectionProvider> provider;
            quint64 attemptNonce = 0;
        };

        QList<Data::SemanticOperationId> pending;
        std::optional<ActiveExecution> active;
        std::optional<LiveRefresh> liveRefresh;
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
    Utils::Result<SemanticActionAdapterResolution> currentAdapterResolution(
        const Data::SemanticOperationRequest &request) const;
    std::optional<CurrentExecution> validateCurrentExecution(
        const ActiveExecution &active, QString *error) const;

    void publishJournalResult(
        const SemanticOperationJournalResult &result, const QString &controllerId);
    void dispatchLiveRefresh(const QString &controllerId, quint64 attemptNonce);
    bool finishLiveRefresh(
        const QString &controllerId,
        quint64 attemptNonce,
        Data::SemanticLiveRefreshOutcome outcome,
        const QString &code,
        const QString &detail,
        quint64 captureCycle = 0);
    void enqueue(const Data::SemanticOperationRecord &record);
    void startNext(const QString &controllerId);
    void beginBeforeSnapshot(const QString &controllerId);
    void beginNextPolicy(const QString &controllerId);
    void beginState(const QString &controllerId);
    void beginCurrentStep(const QString &controllerId);
    void beginApply(const QString &controllerId);
    void beginWaitSnapshot(const QString &controllerId);
    void beginAfterSnapshot(const QString &controllerId);
    void beginAfterState(const QString &controllerId);
    void scheduleWaitRetry(const QString &controllerId);
    bool recordCurrentStep(
        const QString &controllerId,
        bool failed,
        const QString &detail,
        const std::optional<Data::ControllerOperationError> &error = {});
    void completeCurrentStep(const QString &controllerId);
    void beginRecovery(
        const QString &controllerId,
        Data::SemanticOperationState state,
        const QString &code,
        const QString &detail,
        const std::optional<Data::ControllerOperationError> &error = {});
    void beginRecoveryState(const QString &controllerId);
    void finishRecovery(const QString &controllerId);
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
    void timeOutActive(const QString &controllerId, const QString &code, const QString &detail);
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
    bool stateProvesLastOverride(
        const ActiveExecution &active,
        const Data::RuntimeOutputTransactionState &state,
        quint64 minimumControllerTimestampNs) const;
    bool stateProvesLastSafeHold(
        const ActiveExecution &active,
        const Data::RuntimeOutputTransactionState &state) const;

    SemanticRuntimeExecutor *q = nullptr;
    SemanticOperationJournal m_journal;
    QHash<QString, ControllerQueue> m_queues;
    QHash<QString, SemanticActionAdapterAuthorizationAdmission> m_adapterAuthorizations;
    quint64 m_nextLiveRefreshAttemptNonce = 0;
};

std::optional<Data::SemanticRuntimeContext> SemanticRuntimeExecutorExecution::currentContext(
    const Data::SemanticOperationRequest &request, QString *error) const
{
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    if (!q->m_projectService || !q->m_projectService->isAvailable()) {
        if (error)
            *error = QStringLiteral("The project service is unavailable.");
        return std::nullopt;
    }
    const std::optional<Data::ProjectSnapshot> project
        = q->m_projectService->project(request.target.scope.projectId);
    if (!guardedExecutor)
        return std::nullopt;
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
    if (!guardedExecutor)
        return std::nullopt;
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

Utils::Result<SemanticActionAdapterResolution>
SemanticRuntimeExecutorExecution::currentAdapterResolution(
    const Data::SemanticOperationRequest &request) const
{
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    if (!q->m_projectService || !q->m_providerRegistry)
        return Utils::ResultError("The adapter authorization provider is unavailable.");
    const std::optional<Data::ProjectSnapshot> project = q->m_projectService->project(
        request.target.scope.projectId);
    if (!guardedExecutor)
        return Utils::ResultError("The adapter authorization owner was removed.");
    if (!project || !project->valid)
        return Utils::ResultError("The operation project is unavailable.");

    QList<const Data::OfflineSlaveConfiguration *> slaves;
    for (const Data::OfflineSlaveConfiguration &slave : project->slaves) {
        if (slave.id == request.target.deviceId && slave.masterId == request.target.scope.masterId) {
            slaves.append(&slave);
        }
    }
    if (slaves.size() != 1)
        return Utils::ResultError("The operation adapter selection is ambiguous.");
    const AvailableDeviceAdapterProviders adapterProviders{
        q->m_availableAdapterProvidersOverride
            ? q->m_availableAdapterProvidersOverride
            : availableDeviceAdapterProviderList(q->m_providerRegistry),
        guardedExecutor,
        [guardedExecutor] {
            return guardedExecutor ? guardedExecutor->m_adapterProviderSignalGeneration
                                   : std::numeric_limits<quint64>::max();
        },
    };
    return resolveSemanticActionAdapter(*slaves.constFirst(), adapterProviders);
}

std::optional<SemanticRuntimeExecutorExecution::CurrentExecution>
SemanticRuntimeExecutorExecution::validateCurrentExecution(
    const ActiveExecution &active, QString *error) const
{
    const SemanticActionPlan plan = active.plan;
    const std::optional<SemanticActionAdapterAuthorizationAdmission> adapterAuthorization
        = active.adapterAuthorization;
    const QPointer<Core::ControllerConnectionProvider> expectedProvider = active.provider;
    const Data::SemanticOperationRequest request = plan.request();
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const std::optional<Data::SemanticRuntimeContext> context = currentContext(request, error);
    if (!guardedExecutor)
        return std::nullopt;
    if (!context)
        return std::nullopt;
    if (!context->complete || context->contextHash != plan.contextHash()
        || semanticRuntimeContextHash(*context) != plan.contextHash()
        || context->sessionGeneration != plan.sessionGeneration() || context->epoch != plan.epoch()
        || context->mappingDigest.value != plan.mappingDigest()
        || context->controllerMappingDigest.value != plan.mappingDigest()
        || context->actionDefinitionsDigest.value != plan.actionDefinitionsDigest()
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
        || actions.constFirst().actionBindingId != plan.actionBindingId()
        || actions.constFirst().actionDefinitionId != plan.actionDefinitionId()
        || actions.constFirst().actionDefinitionDigest.value != plan.actionDefinitionDigest()) {
        if (error)
            *error = QStringLiteral("The signed action is no longer executable.");
        return std::nullopt;
    }

    const Utils::Result<SemanticActionAdapterResolution> adapterResolution
        = currentAdapterResolution(request);
    if (!guardedExecutor)
        return std::nullopt;
    if (!adapterResolution || adapterResolution->authorization != adapterAuthorization) {
        if (error)
            *error = QStringLiteral("The adapter authorization changed during execution.");
        return std::nullopt;
    }

    Core::ControllerConnectionProvider *provider = currentProvider(plan.scope(), error);
    if (!guardedExecutor)
        return std::nullopt;
    if (!provider)
        return std::nullopt;
    if (provider != expectedProvider) {
        if (error)
            *error = QStringLiteral("The controller provider changed during execution.");
        return std::nullopt;
    }
    const Data::ControllerConnectionSnapshot connection = provider->connectionSnapshot();
    if (!isConnected(connection.state) || connection.scope != plan.scope()
        || connection.sessionGeneration != plan.sessionGeneration() || connection.readOnly
        || !connection.session || !connection.session->sessionId
        || !connection.session->ownsControlLease
        || connection.session->controlLeaseOwnerSessionId != connection.session->sessionId
        || !connection.controllerState
        || !allowedExecutionState(connection.controllerState->serviceState)
        || !provider->supportsRuntimeResources() || !provider->supportsRuntimeOutputTransactions()
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    if (result.record)
        emit q->operationChanged(result.record->request.operationId);
    if (!guardedExecutor)
        return;
    if (result.disposition != SemanticOperationJournalDisposition::Replayed
        && result.disposition != SemanticOperationJournalDisposition::NotFound
        && !controllerId.isEmpty()) {
        emit q->auditChanged(controllerId);
    }
}

Data::SemanticOperationRecord SemanticRuntimeExecutorExecution::submit(
    const Data::SemanticOperationRequest &request, const Data::SemanticRuntimeActor &actor)
{
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    QString error;
    Data::SemanticRuntimeContext context;
    if (const auto current = currentContext(request, &error)) {
        context = *current;
    } else {
        context.controllerId = request.target.controllerId;
        context.scope = request.target.scope;
        context.detail = error;
    }
    if (!guardedExecutor)
        return {};
    const Utils::Result<SemanticActionAdapterResolution> adapterResolution
        = context.complete
              ? currentAdapterResolution(request)
              : Utils::ResultError(QStringLiteral("The semantic context is incomplete."));
    if (!guardedExecutor)
        return {};
    if (!adapterResolution || !adapterResolution->authorization) {
        context.complete = false;
        context.contextHash.clear();
        if (context.detail.isEmpty()) {
            context.detail = adapterResolution
                                 ? QStringLiteral("Adapter authorization is unavailable.")
                                 : adapterResolution.error();
        }
    }
    const SemanticOperationJournalResult result = m_journal.submit(request, actor, context);
    if (result.record && result.disposition == SemanticOperationJournalDisposition::Created
        && adapterResolution && adapterResolution->authorization) {
        m_adapterAuthorizations.insert(request.operationId.value, *adapterResolution->authorization);
    }
    const Data::SemanticOperationRecord returned
        = result.record ? *result.record
                        : q->Core::SemanticRuntimeService::submit(request, actor);
    publishJournalResult(result, request.target.controllerId);
    if (!guardedExecutor)
        return returned;
    return returned;
}

Data::SemanticOperationRecord SemanticRuntimeExecutorExecution::approve(
    const Data::SemanticOperationApprovalRequest &approval,
    const Data::SemanticRuntimeActor &actor)
{
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
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
    if (!guardedExecutor)
        return *existing;
    const Utils::Result<SemanticActionAdapterResolution> adapterResolution
        = context.complete
              ? currentAdapterResolution(existing->request)
              : Utils::ResultError(QStringLiteral("The semantic context is incomplete."));
    if (!guardedExecutor)
        return *existing;
    const auto submittedAuthorization = m_adapterAuthorizations.constFind(
        approval.operationId.value);
    if (!adapterResolution || !adapterResolution->authorization
        || submittedAuthorization == m_adapterAuthorizations.cend()
        || *adapterResolution->authorization != *submittedAuthorization) {
        context.complete = false;
        context.contextHash.clear();
        context.detail = QStringLiteral(
            "The adapter authorization changed after the operation was submitted.");
    }
    const SemanticOperationJournalResult result = m_journal.approve(approval, actor, context);
    const Data::SemanticOperationRecord returned = result.record.value_or(*existing);
    publishJournalResult(result, existing->request.target.controllerId);
    if (!guardedExecutor)
        return returned;
    if (result.record
        && result.record->state == Data::SemanticOperationState::Approved
        && result.accepted()) {
        enqueue(*result.record);
    } else if (
        result.record
        && (result.record->state == Data::SemanticOperationState::Rejected
            || result.record->state == Data::SemanticOperationState::Succeeded
            || result.record->state == Data::SemanticOperationState::Failed
            || result.record->state == Data::SemanticOperationState::TimedOut
            || result.record->state == Data::SemanticOperationState::Canceled
            || result.record->state == Data::SemanticOperationState::Expired)) {
        m_adapterAuthorizations.remove(approval.operationId.value);
    }
    return returned;
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

Data::SemanticLiveRefreshResult SemanticRuntimeExecutorExecution::requestLiveRefresh(
    const Data::SemanticLiveRefreshRequest &request)
{
    const auto rejected = [&request](const QString &code, const QString &detail) {
        return Data::SemanticLiveRefreshResult{
            request.correlationId,
            Data::SemanticLiveRefreshOutcome::Rejected,
            code,
            detail,
            0,
        };
    };
    const auto deferred = [&request](const QString &code, const QString &detail) {
        return Data::SemanticLiveRefreshResult{
            request.correlationId,
            Data::SemanticLiveRefreshOutcome::Deferred,
            code,
            detail,
            0,
        };
    };
    if (!request.isValid())
        return rejected(
            QStringLiteral("semantic-live-refresh-invalid"),
            QStringLiteral("Semantic live refresh request is invalid."));
    if (q->m_projectsBeingRemoved.contains(request.scope.projectId))
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The refresh project is being removed."));
    if (!q->m_projectService || !q->m_projectService->isAvailable())
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The project service is unavailable."));

    const std::optional<Data::ProjectSnapshot> project = q->m_projectService->project(
        request.scope.projectId);
    if (!project || !project->valid)
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The refresh project is unavailable."));
    const Data::SemanticRuntimeContext context = q->buildContext(*project, request.scope.masterId);
    if (!context.complete || context.controllerId != request.controllerId
        || context.scope != request.scope || context.contextHash != request.expectedContextHash
        || semanticRuntimeContextHash(context) != request.expectedContextHash) {
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The verified semantic context changed."));
    }

    QList<Data::SemanticRuntimeBinding> bindings;
    for (const Data::SemanticLiveRefreshSignal &signal : request.targets) {
        QList<Data::SemanticRuntimeBinding> matches;
        for (const Data::SemanticSignalRuntimeState &state : context.signalStates) {
            if (state.target.controllerId == request.controllerId
                && state.target.scope == request.scope && state.target.deviceId == signal.deviceId
                && state.target.kind == Data::SemanticRuntimeTargetKind::Signal
                && state.target.signalId == signal.signalId && state.target.actionId.value.isEmpty()
                && state.binding) {
                matches.append(*state.binding);
            }
        }
        if (matches.size() != 1
            || !Core::validateSemanticRuntimeBinding(matches.constFirst()).accepted()
            || (matches.constFirst().access != Data::RuntimeResourceAccess::ReadOnly
                && matches.constFirst().access != Data::RuntimeResourceAccess::ReadWrite)) {
            return rejected(
                QStringLiteral("semantic-live-refresh-binding-invalid"),
                QStringLiteral("A requested semantic signal is not uniquely readable."));
        }
        bindings.append(matches.constFirst());
    }
    std::sort(
        bindings.begin(),
        bindings.end(),
        [](const Data::SemanticRuntimeBinding &left, const Data::SemanticRuntimeBinding &right) {
            return left.resourceId.value < right.resourceId.value;
        });
    if (std::adjacent_find(
            bindings.cbegin(),
            bindings.cend(),
            [](const Data::SemanticRuntimeBinding &left, const Data::SemanticRuntimeBinding &right) {
                return left.resourceId == right.resourceId;
            })
        != bindings.cend()) {
        return rejected(
            QStringLiteral("semantic-live-refresh-binding-invalid"),
            QStringLiteral("Requested semantic signals resolve ambiguously."));
    }

    QString providerError;
    Core::ControllerConnectionProvider *provider = currentProvider(request.scope, &providerError);
    if (!provider)
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            conciseExecutionText(providerError));
    const Data::ControllerConnectionSnapshot connection = provider->connectionSnapshot();
    if (!isConnected(connection.state) || connection.scope != request.scope
        || connection.sessionGeneration != context.sessionGeneration
        || !provider->supportsRuntimeResources()) {
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The controller runtime-resource session changed."));
    }
    const std::optional<Data::RuntimeResourceCatalog> catalog = provider->runtimeResourceCatalog();
    if (!catalog || catalog->scope != request.scope
        || catalog->sessionGeneration != context.sessionGeneration
        || catalog->epoch != context.epoch) {
        return rejected(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The runtime-resource catalog changed."));
    }

    ControllerQueue &queue = m_queues[request.controllerId];
    if (queue.active || !queue.pending.isEmpty() || queue.liveRefresh) {
        return deferred(
            QStringLiteral("semantic-live-refresh-busy"),
            QStringLiteral("Controller work is already in progress."));
    }

    quint64 attemptNonce = ++m_nextLiveRefreshAttemptNonce;
    if (!attemptNonce)
        attemptNonce = ++m_nextLiveRefreshAttemptNonce;

    Data::RuntimeResourceSnapshotRequest providerRequest;
    QByteArray correlationMaterial = QByteArrayLiteral(
        "embed-labs.semantic-live-refresh-attempt.v1");
    appendContextInteger(correlationMaterial, attemptNonce);
    appendContextString(correlationMaterial, request.correlationId);
    appendContextBytes(correlationMaterial, request.expectedContextHash);
    QList<Data::SemanticLiveRefreshSignal> canonicalTargets = request.targets;
    std::sort(
        canonicalTargets.begin(),
        canonicalTargets.end(),
        [](const Data::SemanticLiveRefreshSignal &left,
           const Data::SemanticLiveRefreshSignal &right) {
            const QString leftDevice = left.deviceId.toString();
            const QString rightDevice = right.deviceId.toString();
            if (leftDevice != rightDevice)
                return leftDevice < rightDevice;
            return left.signalId.value < right.signalId.value;
        });
    appendContextInteger(correlationMaterial, quint64(canonicalTargets.size()));
    for (const Data::SemanticLiveRefreshSignal &target : std::as_const(canonicalTargets)) {
        appendContextString(correlationMaterial, target.deviceId.toString());
        appendContextString(correlationMaterial, target.signalId.value);
    }
    for (const Data::SemanticRuntimeBinding &binding : std::as_const(bindings))
        appendContextBytes(correlationMaterial, binding.resourceId.value);
    providerRequest.correlationId
        = QStringLiteral("semantic-live-")
          + QString::fromLatin1(
              QCryptographicHash::hash(correlationMaterial, QCryptographicHash::Sha256)
                  .toHex()
                  .left(40));
    providerRequest.scope = request.scope;
    providerRequest.sessionGeneration = context.sessionGeneration;
    providerRequest.expectedEpoch = context.epoch;
    for (const Data::SemanticRuntimeBinding &binding : std::as_const(bindings))
        providerRequest.resourceIds.append(binding.resourceId);
    if (!providerRequest.isValid())
        return rejected(
            QStringLiteral("semantic-live-refresh-binding-invalid"),
            QStringLiteral("The verified runtime snapshot request is invalid."));

    queue.liveRefresh.emplace(
        ControllerQueue::LiveRefresh{request, providerRequest, bindings, provider, attemptNonce});
    QTimer::singleShot(0, q, [this, controllerId = request.controllerId, attemptNonce] {
        dispatchLiveRefresh(controllerId, attemptNonce);
    });
    return {
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Accepted,
        QStringLiteral("semantic-live-refresh-accepted"),
        {},
        0,
    };
}

void SemanticRuntimeExecutorExecution::dispatchLiveRefresh(
    const QString &controllerId, quint64 attemptNonce)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->liveRefresh
        || queue->liveRefresh->attemptNonce != attemptNonce) {
        return;
    }
    const QPointer<Core::ControllerConnectionProvider> provider = queue->liveRefresh->provider;
    const Data::RuntimeResourceSnapshotRequest providerRequest = queue->liveRefresh->providerRequest;
    const Data::SemanticLiveRefreshRequest request = queue->liveRefresh->request;
    if (!provider) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The controller provider was removed before live refresh started."));
        return;
    }

    QString lifecycleError;
    const std::optional<Data::ProjectSnapshot> project
        = q->m_projectService && q->m_projectService->isAvailable()
                  && !q->m_projectsBeingRemoved.contains(request.scope.projectId)
              ? q->m_projectService->project(request.scope.projectId)
              : std::nullopt;
    const Data::SemanticRuntimeContext context
        = project && project->valid ? q->buildContext(*project, request.scope.masterId)
                                    : Data::SemanticRuntimeContext{};
    Core::ControllerConnectionProvider *current
        = project ? currentProvider(request.scope, &lifecycleError) : nullptr;
    const Data::ControllerConnectionSnapshot connection = current
                                                              ? current->connectionSnapshot()
                                                              : Data::ControllerConnectionSnapshot{};
    const std::optional<Data::RuntimeResourceCatalog> catalog
        = current ? current->runtimeResourceCatalog() : std::nullopt;
    if (!project || !project->valid || current != provider || !context.complete
        || context.controllerId != request.controllerId || context.scope != request.scope
        || context.contextHash != request.expectedContextHash
        || semanticRuntimeContextHash(context) != request.expectedContextHash
        || !isConnected(connection.state) || connection.scope != request.scope
        || connection.sessionGeneration != context.sessionGeneration
        || !current->supportsRuntimeResources() || !catalog || catalog->scope != request.scope
        || catalog->sessionGeneration != context.sessionGeneration
        || catalog->epoch != context.epoch || providerRequest.scope != context.scope
        || providerRequest.sessionGeneration != context.sessionGeneration
        || providerRequest.expectedEpoch != context.epoch) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-context-changed"),
            lifecycleError.isEmpty()
                ? QStringLiteral("The semantic runtime lifecycle changed before dispatch.")
                : conciseExecutionText(lifecycleError));
        return;
    }

    QTimer::singleShot(q->m_liveRefreshTimeoutMs, q, [this, controllerId, attemptNonce] {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-timeout"),
            QStringLiteral("The controller did not return the live snapshot in time."));
    });
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started = provider->requestRuntimeResourceSnapshot(providerRequest);
    if (!guardedExecutor)
        return;
    if (!started) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The controller provider could not start the live snapshot."));
    }
}

bool SemanticRuntimeExecutorExecution::finishLiveRefresh(
    const QString &controllerId,
    quint64 attemptNonce,
    Data::SemanticLiveRefreshOutcome outcome,
    const QString &code,
    const QString &detail,
    quint64 captureCycle)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->liveRefresh
        || queue->liveRefresh->attemptNonce != attemptNonce) {
        return false;
    }
    const QString correlationId = queue->liveRefresh->request.correlationId;
    queue->liveRefresh.reset();
    SemanticRuntimeExecutor *executor = q;
    const Data::SemanticLiveRefreshResult
        completion{correlationId, outcome, code, detail, captureCycle};
    QTimer::singleShot(0, executor, [this, controllerId] { startNext(controllerId); });
    emit executor->liveRefreshCompleted(completion);
    return true;
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || queue->active || queue->liveRefresh || queue->pending.isEmpty())
        return;

    const Data::SemanticOperationId operationId = queue->pending.takeFirst();
    const std::optional<Data::SemanticOperationRecord> record = m_journal.operation(operationId);
    if (!record || record->state != Data::SemanticOperationState::Approved) {
        m_adapterAuthorizations.remove(operationId.value);
        QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
        return;
    }

    QString error;
    const std::optional<Data::SemanticRuntimeContext> context = currentContext(record->request, &error);
    if (!guardedExecutor)
        return;
    const std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence
        = context ? currentEvidence(record->request, &error) : nullptr;
    if (!guardedExecutor)
        return;
    QPointer<Core::ControllerConnectionProvider> provider
        = context ? currentProvider(record->request.target.scope, &error) : nullptr;
    if (!guardedExecutor)
        return;
    const Utils::Result<SemanticActionAdapterResolution> adapterResolution
        = context && evidence ? currentAdapterResolution(record->request)
                              : Utils::ResultError(conciseExecutionText(error));
    if (!guardedExecutor)
        return;
    Core::ControllerConnectionProvider *currentControllerProvider
        = currentProvider(record->request.target.scope, &error);
    if (!guardedExecutor)
        return;
    const bool providerAvailable = provider && provider->isAvailable();
    if (!guardedExecutor)
        return;
    const bool controllerProviderCurrent = providerAvailable
                                           && currentControllerProvider == provider;
    const auto submittedAuthorization = m_adapterAuthorizations.constFind(operationId.value);
    const std::optional<SemanticActionAdapterAuthorizationAdmission> submittedToken
        = submittedAuthorization != m_adapterAuthorizations.cend()
              ? std::optional<SemanticActionAdapterAuthorizationAdmission>(*submittedAuthorization)
              : std::nullopt;
    const bool adapterAuthorizationCurrent = adapterResolution && adapterResolution->authorization
                                             && submittedToken
                                             && *adapterResolution->authorization
                                                    == *submittedToken;
    const Utils::Result<SemanticActionPlan> plan = [&]() -> Utils::Result<SemanticActionPlan> {
        if (!context || !evidence || !adapterAuthorizationCurrent)
            return Utils::ResultError(
                adapterResolution
                    ? QStringLiteral("The adapter authorization changed after approval.")
                    : conciseExecutionText(adapterResolution.error()));
        if (!controllerProviderCurrent)
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
        QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
        publishJournalResult(failed, controllerId);
        if (!guardedExecutor)
            return;
        m_adapterAuthorizations.remove(operationId.value);
        QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
        return;
    }

    bool shapeValid = provider && !plan->steps().isEmpty() && plan->steps().size() <= 64
                      && !plan->groups().isEmpty() && plan->groups().size() <= 64;
    quint64 totalWaitCycles = 0;
    bool hasWriteStep = false;
    QSet<QByteArray> uniqueResources;
    QSet<QByteArray> uniqueGroups;
    if (shapeValid) {
        for (const SemanticActionPlanGroup &group : plan->groups()) {
            if (!group.consistencyGroupId().isValid()
                || uniqueGroups.contains(group.consistencyGroupId().value)
                || group.completeResourceIds().isEmpty()
                || group.completeResourceIds().size() > 64) {
                shapeValid = false;
                break;
            }
            uniqueGroups.insert(group.consistencyGroupId().value);
        }
    }
    for (qsizetype index = 0; shapeValid && index < plan->steps().size(); ++index) {
        const SemanticActionPlanStep &step = plan->steps().at(index);
        if (step.index() != quint32(index)) {
            shapeValid = false;
            break;
        }
        if (step.kind() == SemanticActionPlanStepKind::WriteGroup) {
            hasWriteStep = true;
            const auto group = std::find_if(
                plan->groups().cbegin(),
                plan->groups().cend(),
                [&step](const SemanticActionPlanGroup &candidate) {
                    return candidate.consistencyGroupId() == step.consistencyGroupId();
                });
            if (group == plan->groups().cend() || step.completeGroupWrites().isEmpty()
                || step.completeGroupWrites().size() > 64
                || !sameResourceIds(
                    group->completeResourceIds(), step.completeGroupWrites())) {
                shapeValid = false;
                break;
            }
            for (const Data::RuntimeOutputValueWrite &write : step.completeGroupWrites()) {
                if (!write.isValid()) {
                    shapeValid = false;
                    break;
                }
                uniqueResources.insert(write.resourceId.value);
            }
        } else if (step.kind() == SemanticActionPlanStepKind::WaitMasked
                   || step.kind() == SemanticActionPlanStepKind::WaitAbsoluteLimit) {
            const Data::RuntimeResourcePrimitiveType primitive
                = step.waitBinding()
                      ? step.waitBinding()->primitiveType
                      : Data::RuntimeResourcePrimitiveType::Opaque;
            const bool primitiveSupported
                = step.kind() == SemanticActionPlanStepKind::WaitMasked
                      ? (primitive == Data::RuntimeResourcePrimitiveType::Boolean
                         || primitive == Data::RuntimeResourcePrimitiveType::SignedInteger
                         || primitive == Data::RuntimeResourcePrimitiveType::UnsignedInteger)
                      : primitive == Data::RuntimeResourcePrimitiveType::SignedInteger;
            if (!step.waitBinding() || !step.waitBinding()->resourceId.isValid()
                || step.waitBinding()->bitWidth == 0 || step.waitBinding()->bitWidth > 64
                || step.waitBinding()->direction != Data::RuntimeResourceDirection::Input
                || step.waitBinding()->access != Data::RuntimeResourceAccess::ReadOnly
                || !primitiveSupported
                || (primitive == Data::RuntimeResourcePrimitiveType::Boolean
                    && step.waitBinding()->bitWidth != 1)
                || !step.timeoutCycles() || step.timeoutCycles() > 65535
                || totalWaitCycles > 65535 - step.timeoutCycles()) {
                shapeValid = false;
                break;
            }
            totalWaitCycles += step.timeoutCycles();
            uniqueResources.insert(step.waitBinding()->resourceId.value);
        } else {
            shapeValid = false;
        }
        if (uniqueResources.size() > 64)
            shapeValid = false;
    }
    shapeValid = shapeValid && hasWriteStep;
    if (shapeValid && plan->steps().size() > 1) {
        shapeValid = std::all_of(
            plan->groups().cbegin(),
            plan->groups().cend(),
            [](const SemanticActionPlanGroup &group) {
                return group.recoveryPolicy() == Data::RuntimeOutputRecoveryPolicy::HoldSafe;
            });
    }
    if (!shapeValid) {
        const SemanticOperationJournalResult failed = m_journal.transition(
            operationId,
            {
                Data::SemanticOperationState::Approved,
                Data::SemanticOperationState::Failed,
                executorActor(),
                QStringLiteral("action-shape-unsupported"),
                QStringLiteral(
                    "The signed action exceeds bounded multi-step execution limits."),
                {},
                0,
                0,
            });
        QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
        publishJournalResult(failed, controllerId);
        if (!guardedExecutor)
            return;
        m_adapterAuthorizations.remove(operationId.value);
        QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
        return;
    }

    ActiveExecution active(operationId, record->actor, *plan, provider.data(), *submittedToken);
    QList<QByteArray> sortedResourceIds = uniqueResources.values();
    std::sort(sortedResourceIds.begin(), sortedResourceIds.end());
    for (const QByteArray &resourceId : std::as_const(sortedResourceIds))
        active.resourceIds.append({resourceId});
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
    if (!guardedExecutor)
        return;
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
    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }

    Data::RuntimeResourceSnapshotRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(active.operationId, u"before", 0, attempt);
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started = current->provider->requestRuntimeResourceSnapshot(request);
    if (!guardedExecutor)
        return;
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

    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    const SemanticActionPlanGroup &group = active.plan.groups().at(active.policyIndex);
    Data::RuntimeOutputGroupPolicyRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(
        active.operationId, u"policy", quint32(active.policyIndex), attempt);
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started = current->provider->requestRuntimeOutputGroupPolicy(request);
    if (!guardedExecutor)
        return;
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
    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }

    Data::RuntimeOutputTransactionStateRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(active.operationId, u"state", 0, attempt);
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.expectedMappingDigest = active.plan.mappingDigest();
    active.phase = SemanticExecutionPhase::State;
    active.stateRequest = request;
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started
        = current->provider->requestRuntimeOutputTransactionState(request);
    if (!guardedExecutor)
        return;
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

void SemanticRuntimeExecutorExecution::beginCurrentStep(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size()) {
        failActive(
            controllerId,
            QStringLiteral("execution-step-invalid"),
            QStringLiteral("The signed execution step index is invalid."));
        return;
    }

    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }

    const SemanticActionPlanStep &step = active.plan.steps().at(active.stepIndex);
    switch (step.kind()) {
    case SemanticActionPlanStepKind::WriteGroup:
        beginApply(controllerId);
        return;
    case SemanticActionPlanStepKind::WaitMasked:
    case SemanticActionPlanStepKind::WaitAbsoluteLimit:
        beginWaitSnapshot(controllerId);
        return;
    }
    failActive(
        controllerId,
        QStringLiteral("execution-step-unsupported"),
        QStringLiteral("The signed execution step kind is unsupported."));
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
    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
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

    if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size()) {
        failActive(
            controllerId,
            QStringLiteral("execution-step-invalid"),
            QStringLiteral("The signed write step is unavailable."));
        return;
    }
    const SemanticActionPlanStep &step = active.plan.steps().at(active.stepIndex);
    if (step.kind() != SemanticActionPlanStepKind::WriteGroup) {
        failActive(
            controllerId,
            QStringLiteral("execution-step-invalid"),
            QStringLiteral("The current signed step is not an output write."));
        return;
    }
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
    active.applyOutcomePending = true;
    const Data::SemanticOperationId semanticOperationId = active.operationId;
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started = current->provider->applyRuntimeOutputTransaction(request);
    if (!guardedExecutor)
        return;
    if (!started) {
        auto check = m_queues.find(controllerId);
        const auto record = m_journal.operation(semanticOperationId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::Apply
            && check->active->applyRequest == request && record
            && record->state == Data::SemanticOperationState::Executing
            && record->currentStep == quint32(check->active->stepIndex)) {
            check->active->applyOutcomePending = false;
            check->active->mutationMayHaveExecuted
                = check->active->lastAppliedRequest.has_value();
            failActive(
                controllerId,
                QStringLiteral("output-request-start-failed"),
                conciseExecutionText(started.error()));
        }
    }
}

void SemanticRuntimeExecutorExecution::beginWaitSnapshot(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size()) {
        failActive(
            controllerId,
            QStringLiteral("execution-step-invalid"),
            QStringLiteral("The signed wait step is unavailable."));
        return;
    }
    const SemanticActionPlanStep &step = active.plan.steps().at(active.stepIndex);
    if ((step.kind() != SemanticActionPlanStepKind::WaitMasked
         && step.kind() != SemanticActionPlanStepKind::WaitAbsoluteLimit)
        || !step.waitBinding() || !step.waitBinding()->resourceId.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("wait-step-invalid"),
            QStringLiteral("The signed wait binding is invalid."));
        return;
    }

    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    if (!active.waitDeadlineCycle) {
        if (!active.lastCaptureCycle
            || active.lastCaptureCycle
                   > std::numeric_limits<quint64>::max() - step.timeoutCycles()) {
            failActive(
                controllerId,
                QStringLiteral("wait-deadline-invalid"),
                QStringLiteral("The signed wait deadline cannot be represented."));
            return;
        }
        active.waitDeadlineCycle = active.lastCaptureCycle + step.timeoutCycles();
    }
    if (active.waitAttempts >= step.timeoutCycles() + 1) {
        failActive(
            controllerId,
            QStringLiteral("wait-retry-limit-exceeded"),
            QStringLiteral("The controller wait capture did not advance within its bound."));
        return;
    }

    Data::RuntimeResourceSnapshotRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(
        active.operationId, u"wait", quint32(active.stepIndex), attempt);
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.resourceIds = active.resourceIds;
    if (!request.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("wait-snapshot-invalid"),
            QStringLiteral("The exact signed wait snapshot request is invalid."));
        return;
    }
    ++active.waitAttempts;
    active.phase = SemanticExecutionPhase::WaitSnapshot;
    active.snapshotRequest = request;
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started = current->provider->requestRuntimeResourceSnapshot(request);
    if (!guardedExecutor)
        return;
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check != m_queues.end() && check->active
            && check->active->phase == SemanticExecutionPhase::WaitSnapshot
            && check->active->snapshotRequest == request) {
            failActive(
                controllerId,
                QStringLiteral("wait-snapshot-start-failed"),
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
    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("execution-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size()
        || active.plan.steps().at(active.stepIndex).kind()
               != SemanticActionPlanStepKind::WriteGroup) {
        failActive(
            controllerId,
            QStringLiteral("after-snapshot-step-invalid"),
            QStringLiteral("The signed write step changed before confirmation."));
        return;
    }

    Data::RuntimeResourceSnapshotRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(
        active.operationId, u"after", quint32(active.stepIndex), attempt);
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.resourceIds = active.resourceIds;
    if (!request.isValid()) {
        failActive(
            controllerId,
            QStringLiteral("after-snapshot-invalid"),
            QStringLiteral("The exact post-write snapshot request is invalid."));
        return;
    }
    active.phase = SemanticExecutionPhase::AfterSnapshot;
    active.snapshotRequest = request;
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started = current->provider->requestRuntimeResourceSnapshot(request);
    if (!guardedExecutor)
        return;
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
    ActiveExecution *const activeIdentity = &active;
    const Data::SemanticOperationId operationId = active.operationId;
    const SemanticExecutionPhase phase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto currentQueue = m_queues.constFind(controllerId);
    if (currentQueue == m_queues.cend() || !currentQueue->active
        || &*currentQueue->active != activeIdentity
        || currentQueue->active->operationId != operationId
        || currentQueue->active->phase != phase) {
        return;
    }
    if (!current) {
        failActive(
            controllerId,
            QStringLiteral("post-state-precondition-changed"),
            conciseExecutionText(error));
        return;
    }
    if (!active.pendingAfterSnapshot || !active.lastAppliedRequest) {
        failActive(
            controllerId,
            QStringLiteral("post-state-evidence-missing"),
            QStringLiteral("The post-operation state evidence is incomplete."));
        return;
    }

    Data::RuntimeOutputTransactionStateRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(
        active.operationId, u"post-state", quint32(active.stepIndex), attempt);
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started
        = current->provider->requestRuntimeOutputTransactionState(request);
    if (!guardedExecutor)
        return;
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

void SemanticRuntimeExecutorExecution::scheduleWaitRetry(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    const Data::SemanticOperationId operationId = active.operationId;
    const qsizetype stepIndex = active.stepIndex;
    const quint32 attempt = active.attempt;
    QTimer::singleShot(1, q, [this, controllerId, operationId, stepIndex, attempt] {
        auto currentQueue = m_queues.find(controllerId);
        if (currentQueue == m_queues.end() || !currentQueue->active
            || currentQueue->active->operationId != operationId
            || currentQueue->active->stepIndex != stepIndex
            || currentQueue->active->attempt != attempt
            || currentQueue->active->phase != SemanticExecutionPhase::WaitSnapshot
            || currentQueue->active->snapshotRequest) {
            return;
        }
        beginWaitSnapshot(controllerId);
    });
}

bool SemanticRuntimeExecutorExecution::recordCurrentStep(
    const QString &controllerId,
    bool failed,
    const QString &detail,
    const std::optional<Data::ControllerOperationError> &error)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return false;
    ActiveExecution &active = *queue->active;
    if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size())
        return false;
    const quint32 stepNumber = active.plan.steps().at(active.stepIndex).index() + 1;
    const auto existing = m_journal.operation(active.operationId);
    if (!existing)
        return false;
    if (existing->currentStep == stepNumber)
        return !failed && existing->failedStep == 0;
    const SemanticOperationJournalResult recorded = m_journal.recordStep(
        active.operationId,
        {
            Data::SemanticOperationState::Executing,
            executorActor(),
            stepNumber,
            quint32(active.plan.steps().size()),
            failed,
            conciseExecutionText(detail),
            error,
        });
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    publishJournalResult(recorded, controllerId);
    if (!guardedExecutor)
        return false;
    if (!recorded.accepted()) {
        failActive(
            controllerId,
            QStringLiteral("execution-step-record-failed"),
            QStringLiteral("The signed execution step could not be recorded."));
        return false;
    }
    return true;
}

void SemanticRuntimeExecutorExecution::completeCurrentStep(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    if (active.stepIndex < 0 || active.stepIndex + 1 >= active.plan.steps().size()) {
        failActive(
            controllerId,
            QStringLiteral("execution-step-sequence-invalid"),
            QStringLiteral("The executor cannot advance beyond the signed plan."));
        return;
    }
    ++active.stepIndex;
    active.waitAttempts = 0;
    active.waitDeadlineCycle = 0;
    active.snapshotRequest.reset();
    active.stateRequest.reset();
    active.applyRequest.reset();
    active.pendingAfterSnapshot.reset();
    const Data::SemanticOperationId operationId = active.operationId;
    QTimer::singleShot(0, q, [this, controllerId, operationId] {
        auto currentQueue = m_queues.find(controllerId);
        if (currentQueue == m_queues.end() || !currentQueue->active
            || currentQueue->active->operationId != operationId) {
            return;
        }
        beginCurrentStep(controllerId);
    });
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
    if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size())
        return false;
    const SemanticActionPlanStep &step = active.plan.steps().at(active.stepIndex);
    if (step.kind() != SemanticActionPlanStepKind::WriteGroup)
        return false;
    const QList<Data::RuntimeOutputValueWrite> &writes = step.completeGroupWrites();
    if (snapshot.samples.size() != active.resourceIds.size())
        return false;
    return snapshotMatchesWrites(snapshot, writes);
}

bool SemanticRuntimeExecutorExecution::stateProvesLastOverride(
    const ActiveExecution &active,
    const Data::RuntimeOutputTransactionState &state,
    quint64 minimumControllerTimestampNs) const
{
    if (!active.lastAppliedRequest)
        return false;
    const Data::RuntimeOutputTransactionRequest &apply = *active.lastAppliedRequest;
    return state.operationId && *state.operationId == apply.operationId
           && state.state == Data::RuntimeOutputState::OverrideActive
           && state.outputGeneration == active.appliedOutputGeneration
           && state.appliedCycle == active.appliedCycle
           && state.expiryCycle == active.expiryCycle
           && state.consistencyGroupId == apply.consistencyGroupId
           && state.ttlCycles == apply.ttlCycles
           && state.recoveryPolicy == apply.expectedRecoveryPolicy
           && state.valueCount == quint16(apply.completeGroupWrites.size())
           && state.controllerTimestampNs >= minimumControllerTimestampNs;
}

bool SemanticRuntimeExecutorExecution::stateProvesLastSafeHold(
    const ActiveExecution &active,
    const Data::RuntimeOutputTransactionState &state) const
{
    if (!active.lastAppliedRequest)
        return false;
    const Data::RuntimeOutputTransactionRequest &apply = *active.lastAppliedRequest;
    return apply.expectedRecoveryPolicy == Data::RuntimeOutputRecoveryPolicy::HoldSafe
           && state.operationId && *state.operationId == apply.operationId
           && state.state == Data::RuntimeOutputState::SafeHold
           && state.outputGeneration == apply.expectedOutputGeneration + 2
           && state.appliedCycle == active.appliedCycle
           && state.expiryCycle == active.expiryCycle
           && state.consistencyGroupId == apply.consistencyGroupId
           && state.ttlCycles == apply.ttlCycles
           && state.recoveryPolicy == Data::RuntimeOutputRecoveryPolicy::HoldSafe
           && state.valueCount == quint16(apply.completeGroupWrites.size())
           && state.controllerTimestampNs >= active.appliedControllerTimestampNs
           && state.controllerTimestampNs != 0;
}

bool SemanticRuntimeExecutorExecution::handleLiveSnapshot(
    Core::ControllerConnectionProvider *provider, const Data::RuntimeResourceSnapshotResult &result)
{
    QList<QString> correlationMatches;
    for (auto candidate = m_queues.cbegin(); candidate != m_queues.cend(); ++candidate) {
        if (candidate->liveRefresh && candidate->liveRefresh->provider == provider
            && candidate->liveRefresh->providerRequest.correlationId
                   == result.request.correlationId) {
            correlationMatches.append(candidate.key());
        }
    }
    if (correlationMatches.isEmpty())
        return false;
    if (correlationMatches.size() != 1)
        return true;
    const QString controllerId = correlationMatches.constFirst();

    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->liveRefresh)
        return false;
    const ControllerQueue::LiveRefresh refresh = *queue->liveRefresh;
    const auto fail = [this, &refresh, &controllerId](const QString &code, const QString &detail) {
        finishLiveRefresh(
            controllerId,
            refresh.attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            code,
            detail);
    };

    if (result.request != refresh.providerRequest) {
        fail(
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The live snapshot response did not match its exact attempt."));
        return true;
    }

    if (!result.isValid()) {
        fail(
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The controller returned an invalid live snapshot."));
        return true;
    }
    if (result.error) {
        fail(
            QStringLiteral("semantic-live-refresh-provider-failed"),
            providerFailureText(QStringLiteral("Live refresh failed"), *result.error));
        return true;
    }
    if (!result.snapshot || !result.snapshot->captureCycle
        || !result.snapshot->controllerTimestampNs) {
        fail(
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The live snapshot has no capture boundary."));
        return true;
    }

    if (!q->m_projectService || !q->m_projectService->isAvailable()) {
        fail(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The refresh project is unavailable."));
        return true;
    }
    const std::optional<Data::ProjectSnapshot> project = q->m_projectService->project(
        refresh.request.scope.projectId);
    if (!project || !project->valid) {
        fail(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The refresh project changed."));
        return true;
    }
    const Data::SemanticRuntimeContext context
        = q->buildContext(*project, refresh.request.scope.masterId);
    if (!context.complete || context.controllerId != refresh.request.controllerId
        || context.scope != refresh.request.scope
        || context.contextHash != refresh.request.expectedContextHash
        || semanticRuntimeContextHash(context) != refresh.request.expectedContextHash
        || provider->connectionSnapshot().sessionGeneration != context.sessionGeneration) {
        fail(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The verified semantic context changed during refresh."));
        return true;
    }
    const std::optional<Data::RuntimeResourceCatalog> catalog = provider->runtimeResourceCatalog();
    if (!catalog || catalog->scope != context.scope
        || catalog->sessionGeneration != context.sessionGeneration
        || catalog->epoch != context.epoch
        || refresh.bindings.size() != result.snapshot->samples.size()) {
        fail(
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The runtime-resource catalog changed during refresh."));
        return true;
    }

    QList<Data::RuntimeResourceSample> verifiedSamples;
    verifiedSamples.reserve(refresh.bindings.size());
    for (const Data::RuntimeResourceSample &sample : result.snapshot->samples) {
        if (!sample.controllerTimestampNs
            || sample.controllerTimestampNs != result.snapshot->controllerTimestampNs) {
            fail(
                QStringLiteral("semantic-live-refresh-provider-failed"),
                QStringLiteral("Live samples do not share one controller capture."));
            return true;
        }
    }
    for (const Data::SemanticRuntimeBinding &binding : refresh.bindings) {
        const Core::SemanticRuntimeReadValidation validation
            = Core::validateSemanticRuntimeRead(binding, *catalog, *result.snapshot);
        if (!validation.validation.accepted() || !validation.sample) {
            fail(
                QStringLiteral("semantic-live-refresh-provider-failed"),
                QStringLiteral("A live semantic sample failed verified read validation."));
            return true;
        }
        verifiedSamples.append(*validation.sample);
    }

    const auto currentCache = q->m_liveRuntimeCaches.constFind(provider);
    const bool reuseCache = currentCache != q->m_liveRuntimeCaches.cend()
                            && currentCache->controllerId == context.controllerId
                            && currentCache->scope == context.scope
                            && currentCache->sessionGeneration == context.sessionGeneration
                            && currentCache->epoch == context.epoch
                            && currentCache->mappingDigest == context.mappingDigest
                            && currentCache->controllerMappingDigest
                                   == context.controllerMappingDigest
                            && currentCache->contextHash == context.contextHash;
    SemanticRuntimeExecutor::LiveRuntimeCache updated;
    if (reuseCache)
        updated = *currentCache;
    else {
        updated.controllerId = context.controllerId;
        updated.scope = context.scope;
        updated.sessionGeneration = context.sessionGeneration;
        updated.epoch = context.epoch;
        updated.mappingDigest = context.mappingDigest;
        updated.controllerMappingDigest = context.controllerMappingDigest;
        updated.contextHash = context.contextHash;
    }
    for (const Data::RuntimeResourceSample &sample : std::as_const(verifiedSamples)) {
        const auto previous = updated.samples.constFind(sample.resourceId);
        if (previous != updated.samples.cend()
            && (previous->captureCycle > result.snapshot->captureCycle
                || (previous->captureCycle == result.snapshot->captureCycle
                    && previous->sample != sample))) {
            fail(
                QStringLiteral("semantic-live-refresh-provider-failed"),
                QStringLiteral("The live snapshot capture order regressed."));
            return true;
        }
    }
    const QDateTime receivedAt = result.snapshot->receivedAt.isValid()
                                     ? result.snapshot->receivedAt
                                     : QDateTime::currentDateTimeUtc();
    for (const Data::RuntimeResourceSample &sample : std::as_const(verifiedSamples)) {
        updated.samples.insert(
            sample.resourceId,
            {
                sample,
                result.snapshot->captureCycle,
                result.snapshot->controllerTimestampNs,
                receivedAt,
            });
    }
    q->m_liveRuntimeCaches.insert(provider, updated);
    q->scheduleLiveCacheExpiry(provider, receivedAt);
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    q->publishContexts();
    if (!guardedExecutor)
        return true;
    finishLiveRefresh(
        controllerId,
        refresh.attemptNonce,
        Data::SemanticLiveRefreshOutcome::Refreshed,
        QStringLiteral("semantic-live-refresh-refreshed"),
        {},
        result.snapshot->captureCycle);
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
                && candidate->active->phase != SemanticExecutionPhase::AfterSnapshot
                && candidate->active->phase != SemanticExecutionPhase::WaitSnapshot)
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

    ActiveExecution *const validationIdentity = &active;
    const Data::SemanticOperationId validationOperationId = active.operationId;
    const SemanticExecutionPhase validationPhase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto validatedQueue = m_queues.constFind(controllerId);
    if (validatedQueue == m_queues.cend() || !validatedQueue->active
        || &*validatedQueue->active != validationIdentity
        || validatedQueue->active->operationId != validationOperationId
        || validatedQueue->active->phase != validationPhase) {
        return;
    }
    if (!current || !result.snapshot) {
        failActive(
            controllerId,
            QStringLiteral("snapshot-context-changed"),
            conciseExecutionText(
                error.isEmpty() ? QStringLiteral("The snapshot is unavailable.") : error));
        return;
    }
    if (!snapshotHasExactResourceIds(*result.snapshot, expected.resourceIds)) {
        failActive(
            controllerId,
            QStringLiteral("snapshot-resource-set-mismatch"),
            QStringLiteral("The controller snapshot is not the exact requested resource set."));
        return;
    }
    if (active.lastCaptureCycle
        && result.snapshot->captureCycle < active.lastCaptureCycle) {
        failActive(
            controllerId,
            QStringLiteral("snapshot-cycle-regressed"),
            QStringLiteral("The controller snapshot capture cycle regressed."));
        return;
    }
    if (active.phase == SemanticExecutionPhase::AfterSnapshot
        && (!active.appliedCycle || !active.expiryCycle
            || result.snapshot->captureCycle < active.appliedCycle)) {
        failActive(
            controllerId,
            QStringLiteral("after-snapshot-stale"),
            QStringLiteral("The post-operation snapshot predates the applied output."));
        return;
    }
    if (active.phase == SemanticExecutionPhase::AfterSnapshot
        && result.snapshot->captureCycle >= active.expiryCycle) {
        active.snapshotRequest.reset();
        beginRecovery(
            controllerId,
            Data::SemanticOperationState::Expired,
            QStringLiteral("output-override-expired"),
            QStringLiteral("The output TTL expired before post-write proof completed."));
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
    active.lastCaptureCycle = result.snapshot->captureCycle;

    if (active.phase == SemanticExecutionPhase::BeforeSnapshot) {
        const SemanticOperationJournalResult recorded = m_journal.recordBeforeSnapshot(
            active.operationId,
            Data::SemanticOperationState::Executing,
            *snapshot,
            executorActor());
        QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
        publishJournalResult(recorded, controllerId);
        if (!guardedExecutor)
            return;
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

    if (active.phase == SemanticExecutionPhase::WaitSnapshot) {
        if (active.stepIndex < 0 || active.stepIndex >= active.plan.steps().size()) {
            failActive(
                controllerId,
                QStringLiteral("wait-step-invalid"),
                QStringLiteral("The signed wait step changed during capture."));
            return;
        }
        const SemanticActionPlanStep &step = active.plan.steps().at(active.stepIndex);
        const auto waitSample = step.waitBinding()
                                    ? std::find_if(
                                        result.snapshot->samples.cbegin(),
                                        result.snapshot->samples.cend(),
                                        [&step](const Data::RuntimeResourceSample &sample) {
                                            return sample.resourceId
                                                   == step.waitBinding()->resourceId;
                                        })
                                    : result.snapshot->samples.cend();
        if (!step.waitBinding() || waitSample == result.snapshot->samples.cend()) {
            failActive(
                controllerId,
                QStringLiteral("wait-snapshot-binding-mismatch"),
                QStringLiteral("The wait snapshot does not match its signed target."));
            return;
        }
        active.snapshotRequest.reset();
        if (active.lastAppliedRequest
            && result.snapshot->captureCycle >= active.expiryCycle) {
            active.pendingAfterSnapshot = *snapshot;
            beginRecovery(
                controllerId,
                Data::SemanticOperationState::Expired,
                QStringLiteral("output-override-expired"),
                QStringLiteral("The output TTL expired while waiting for signed feedback."));
            return;
        }
        if (active.lastAppliedRequest
            && !snapshotMatchesWrites(
                *result.snapshot, active.lastAppliedRequest->completeGroupWrites)) {
            failActive(
                controllerId,
                QStringLiteral("wait-snapshot-output-mismatch"),
                QStringLiteral(
                    "The wait snapshot does not preserve the last complete signed write."));
            return;
        }
        if (waitConditionMatches(step, *waitSample)) {
            active.pendingAfterSnapshot = *snapshot;
            if (active.stepIndex + 1 == active.plan.steps().size()) {
                beginAfterState(controllerId);
                return;
            }
            if (!recordCurrentStep(
                    controllerId,
                    false,
                    QStringLiteral("The signed wait condition was satisfied."))) {
                return;
            }
            completeCurrentStep(controllerId);
            return;
        }
        if (result.snapshot->captureCycle >= active.waitDeadlineCycle) {
            if (active.lastAppliedRequest) {
                beginRecovery(
                    controllerId,
                    Data::SemanticOperationState::TimedOut,
                    QStringLiteral("wait-deadline-reached"),
                    QStringLiteral("The signed wait condition timed out."));
            } else {
                if (!recordCurrentStep(
                        controllerId,
                        true,
                        QStringLiteral("The signed wait condition timed out."))) {
                    return;
                }
                timeOutActive(
                    controllerId,
                    QStringLiteral("wait-deadline-reached"),
                    QStringLiteral("The signed wait condition timed out."));
            }
            return;
        }
        scheduleWaitRetry(controllerId);
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

    ActiveExecution *const validationIdentity = &active;
    const Data::SemanticOperationId validationOperationId = active.operationId;
    const SemanticExecutionPhase validationPhase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto validatedQueue = m_queues.constFind(controllerId);
    if (validatedQueue == m_queues.cend() || !validatedQueue->active
        || &*validatedQueue->active != validationIdentity
        || validatedQueue->active->operationId != validationOperationId
        || validatedQueue->active->phase != validationPhase) {
        return;
    }
    if (!current || !result.policy || active.policyIndex >= active.plan.groups().size()) {
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
                && candidate->active->phase != SemanticExecutionPhase::AfterState
                && candidate->active->phase != SemanticExecutionPhase::RecoveryState)
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
        if (active.phase == SemanticExecutionPhase::RecoveryState) {
            active.stateRequest.reset();
            freezeUnknown(
                controllerId,
                QStringLiteral("safe-recovery-response-invalid"),
                QStringLiteral("The controller returned an invalid SafeHold state response."));
            return;
        }
        failActive(
            controllerId,
            QStringLiteral("state-response-invalid"),
            QStringLiteral("The controller returned an invalid output state."));
        return;
    }
    if (result.error) {
        if (active.phase == SemanticExecutionPhase::RecoveryState) {
            active.stateRequest.reset();
            if (active.recoveryAttempts
                >= qMin<quint32>(active.plan.ttlCycles() + 1, 65535)) {
                freezeUnknown(
                    controllerId,
                    QStringLiteral("safe-recovery-unproven"),
                    providerFailureText(QStringLiteral("Safe recovery state failed"), *result.error),
                    result.error);
                return;
            }
            QTimer::singleShot(1, q, [this, controllerId] { beginRecoveryState(controllerId); });
            return;
        }
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

    if (active.phase == SemanticExecutionPhase::RecoveryState) {
        active.stateRequest.reset();
        if (!result.state) {
            freezeUnknown(
                controllerId,
                QStringLiteral("safe-recovery-unproven"),
                QStringLiteral("The controller returned no safe recovery state."));
            return;
        }
        if (stateProvesLastSafeHold(active, *result.state)) {
            finishRecovery(controllerId);
            return;
        }
        if (!stateProvesLastOverride(active, *result.state, active.appliedControllerTimestampNs)) {
            freezeUnknown(
                controllerId,
                QStringLiteral("safe-recovery-proof-mismatch"),
                QStringLiteral("The controller did not prove the last signed transaction safe."));
            return;
        }
        if (active.recoveryAttempts
            >= qMin<quint32>(active.plan.ttlCycles() + 1, 65535)) {
            freezeUnknown(
                controllerId,
                QStringLiteral("safe-recovery-unproven"),
                QStringLiteral("The controller did not reach signed SafeHold in time."));
            return;
        }
        QTimer::singleShot(1, q, [this, controllerId] { beginRecoveryState(controllerId); });
        return;
    }

    if (active.phase == SemanticExecutionPhase::AfterState) {
        ActiveExecution *const validationIdentity = &active;
        const Data::SemanticOperationId validationOperationId = active.operationId;
        const SemanticExecutionPhase validationPhase = active.phase;
        QPointer<SemanticRuntimeExecutor> validationGuard(q);
        QString executionError;
        const auto current = validateCurrentExecution(active, &executionError);
        if (!validationGuard)
            return;
        const auto validatedQueue = m_queues.constFind(controllerId);
        if (validatedQueue == m_queues.cend() || !validatedQueue->active
            || &*validatedQueue->active != validationIdentity
            || validatedQueue->active->operationId != validationOperationId
            || validatedQueue->active->phase != validationPhase) {
            return;
        }
        if (!current) {
            failActive(
                controllerId,
                QStringLiteral("post-state-precondition-changed"),
                conciseExecutionText(executionError));
            return;
        }
        if (!result.state || !active.lastAppliedRequest || !active.pendingAfterSnapshot) {
            failActive(
                controllerId,
                QStringLiteral("post-state-evidence-missing"),
                QStringLiteral("The post-operation state evidence is incomplete."));
            return;
        }
        const Data::RuntimeOutputTransactionState &state = *result.state;
        const bool provesActiveOverride = stateProvesLastOverride(
            active, state, active.pendingAfterSnapshot->controllerTimestampNs);
        if (!provesActiveOverride) {
            const Data::RuntimeOutputTransactionRequest &apply = *active.lastAppliedRequest;
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
                      || stateProvesLastSafeHold(active, state));
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
        active.outputGeneration = state.outputGeneration;
        active.stateRequest.reset();
        const bool stepWasAlreadyRecorded
            = active.completionState == Data::SemanticOperationState::OutcomeUnknown;
        if (!stepWasAlreadyRecorded
            && !recordCurrentStep(
                controllerId,
                false,
                active.plan.steps().at(active.stepIndex).kind()
                        == SemanticActionPlanStepKind::WriteGroup
                    ? QStringLiteral("The complete signed output group was applied and verified.")
                    : QStringLiteral("The signed wait condition was satisfied and verified."))) {
            return;
        }
        if (active.stepIndex + 1 != active.plan.steps().size()) {
            if (stepWasAlreadyRecorded) {
                beginRecovery(
                    controllerId,
                    Data::SemanticOperationState::Failed,
                    QStringLiteral("uncertain-intermediate-step"),
                    QStringLiteral(
                        "An uncertain intermediate write was reconciled; no later write is allowed."));
            } else {
                completeCurrentStep(controllerId);
            }
            return;
        }

        const SemanticOperationJournalResult recorded = m_journal.recordAfterSnapshot(
            active.operationId,
            active.completionState,
            *active.pendingAfterSnapshot,
            executorActor());
        QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
        publishJournalResult(recorded, controllerId);
        if (!guardedExecutor)
            return;
        if (!recorded.accepted()) {
            failActive(
                controllerId,
                QStringLiteral("after-snapshot-record-failed"),
                QStringLiteral("The post-operation snapshot could not be recorded."));
            return;
        }
        succeedActive(controllerId);
        return;
    }

    ActiveExecution *const validationIdentity = &active;
    const Data::SemanticOperationId validationOperationId = active.operationId;
    const SemanticExecutionPhase validationPhase = active.phase;
    QPointer<SemanticRuntimeExecutor> validationGuard(q);
    QString error;
    const auto current = validateCurrentExecution(active, &error);
    if (!validationGuard)
        return;
    const auto validatedQueue = m_queues.constFind(controllerId);
    if (validatedQueue == m_queues.cend() || !validatedQueue->active
        || &*validatedQueue->active != validationIdentity
        || validatedQueue->active->operationId != validationOperationId
        || validatedQueue->active->phase != validationPhase) {
        return;
    }
    if (!current || !result.state || result.state->outputGeneration != active.outputGeneration) {
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
    beginCurrentStep(controllerId);
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
    if (result.outcome == Data::RuntimeOutputTransactionOutcome::Rejected) {
        // A valid terminal rejection proves that the atomic mutation was not
        // applied, so the queue can safely advance after recording failure.
        active.applyOutcomePending = false;
        active.mutationMayHaveExecuted = active.lastAppliedRequest.has_value();
        if (state == Data::SemanticOperationState::Executing
            && !recordCurrentStep(
                controllerId,
                true,
                providerFailureText(
                    QStringLiteral("Output transaction rejected"), *result.error),
                result.error)) {
            return;
        }
        failActive(
            controllerId,
            QStringLiteral("output-transaction-rejected"),
            providerFailureText(QStringLiteral("Output transaction rejected"), *result.error),
            result.error);
        return;
    }
    if (result.outcome == Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered) {
        active.applyOutcomePending = false;
        active.lastAppliedRequest = expected;
        active.appliedCycle = result.state ? result.state->appliedCycle : 0;
        active.expiryCycle = result.state ? result.state->expiryCycle : 0;
        active.appliedOutputGeneration = expected.expectedOutputGeneration + 1;
        active.appliedControllerTimestampNs
            = result.state ? result.state->controllerTimestampNs : 0;
        if (state == Data::SemanticOperationState::Executing
            && !recordCurrentStep(
                controllerId,
                true,
                QStringLiteral("The signed output expired before confirmation."))) {
            return;
        }
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

    active.applyOutcomePending = false;
    active.lastAppliedRequest = expected;
    active.appliedCycle = result.state->appliedCycle;
    active.expiryCycle = result.state->expiryCycle;
    active.appliedOutputGeneration = result.state->outputGeneration;
    active.appliedControllerTimestampNs = result.state->controllerTimestampNs;
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
        if (active.applyOutcomePending) {
            freezeUnknown(controllerId, code, detail, error);
        } else {
            beginRecovery(
                controllerId,
                Data::SemanticOperationState::Failed,
                code,
                detail,
                error);
        }
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
        QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
        publishJournalResult(failed, controllerId);
        if (!guardedExecutor)
            return;
    }
    finishActive(controllerId);
}

void SemanticRuntimeExecutorExecution::beginRecovery(
    const QString &controllerId,
    Data::SemanticOperationState state,
    const QString &code,
    const QString &detail,
    const std::optional<Data::ControllerOperationError> &error)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    if (active.applyOutcomePending) {
        freezeUnknown(controllerId, code, detail, error);
        return;
    }
    if (!active.lastAppliedRequest
        || active.lastAppliedRequest->expectedRecoveryPolicy
               != Data::RuntimeOutputRecoveryPolicy::HoldSafe) {
        freezeUnknown(
            controllerId,
            QStringLiteral("safe-recovery-unavailable"),
            QStringLiteral("The last signed output transaction has no HoldSafe proof."),
            error);
        return;
    }
    if (active.phase == SemanticExecutionPhase::RecoveryState)
        return;

    active.recoveryState = state;
    active.recoveryCode = code;
    active.recoveryDetail = conciseExecutionText(detail);
    active.recoveryError = error;
    active.recoveryAttempts = 0;
    active.snapshotRequest.reset();
    active.policyRequest.reset();
    active.stateRequest.reset();
    active.applyRequest.reset();
    active.phase = SemanticExecutionPhase::RecoveryState;
    const Data::SemanticOperationId operationId = active.operationId;
    QTimer::singleShot(0, q, [this, controllerId, operationId] {
        auto currentQueue = m_queues.find(controllerId);
        if (currentQueue == m_queues.end() || !currentQueue->active
            || currentQueue->active->operationId != operationId
            || currentQueue->active->phase != SemanticExecutionPhase::RecoveryState) {
            return;
        }
        beginRecoveryState(controllerId);
    });
}

void SemanticRuntimeExecutorExecution::beginRecoveryState(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    if (active.phase != SemanticExecutionPhase::RecoveryState || active.stateRequest)
        return;
    if (!active.provider || !active.provider->isAvailable()
        || !active.provider->supportsRuntimeOutputTransactions()) {
        freezeUnknown(
            controllerId,
            QStringLiteral("safe-recovery-provider-unavailable"),
            QStringLiteral("The controller provider cannot prove signed SafeHold."));
        return;
    }
    const Data::ControllerConnectionSnapshot connection
        = active.provider->connectionSnapshot();
    if (!isConnected(connection.state) || connection.scope != active.plan.scope()
        || connection.sessionGeneration != active.plan.sessionGeneration()) {
        freezeUnknown(
            controllerId,
            QStringLiteral("safe-recovery-session-changed"),
            QStringLiteral("The controller session changed before SafeHold was proved."));
        return;
    }
    if (active.recoveryAttempts
        >= qMin<quint32>(active.plan.ttlCycles() + 1, 65535)) {
        freezeUnknown(
            controllerId,
            QStringLiteral("safe-recovery-unproven"),
            QStringLiteral("The controller did not prove signed SafeHold within its bound."));
        return;
    }

    Data::RuntimeOutputTransactionStateRequest request;
    const quint32 attempt = ++active.attempt;
    request.correlationId = executionCorrelationId(
        active.operationId, u"recovery-state", quint32(active.stepIndex), attempt);
    request.scope = active.plan.scope();
    request.sessionGeneration = active.plan.sessionGeneration();
    request.expectedEpoch = active.plan.epoch();
    request.expectedMappingDigest = active.plan.mappingDigest();
    if (!request.isValid()) {
        freezeUnknown(
            controllerId,
            QStringLiteral("safe-recovery-request-invalid"),
            QStringLiteral("The signed SafeHold state request is invalid."));
        return;
    }
    ++active.recoveryAttempts;
    active.stateRequest = request;
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    const Utils::Result<> started
        = active.provider->requestRuntimeOutputTransactionState(request);
    if (!guardedExecutor)
        return;
    if (!started) {
        auto check = m_queues.find(controllerId);
        if (check == m_queues.end() || !check->active
            || check->active->phase != SemanticExecutionPhase::RecoveryState
            || check->active->stateRequest != request) {
            return;
        }
        check->active->stateRequest.reset();
        if (check->active->recoveryAttempts
            >= qMin<quint32>(check->active->plan.ttlCycles() + 1, 65535)) {
            freezeUnknown(
                controllerId,
                QStringLiteral("safe-recovery-start-failed"),
                conciseExecutionText(started.error()));
            return;
        }
        QTimer::singleShot(1, q, [this, controllerId] { beginRecoveryState(controllerId); });
    }
}

void SemanticRuntimeExecutorExecution::finishRecovery(const QString &controllerId)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    ActiveExecution &active = *queue->active;
    const auto record = m_journal.operation(active.operationId);
    if (!record)
        return;
    if (record->state == Data::SemanticOperationState::Executing
        && record->currentStep == quint32(active.stepIndex)
        && !recordCurrentStep(
            controllerId,
            true,
            active.recoveryDetail,
            active.recoveryError)) {
        return;
    }
    const auto refreshed = m_journal.operation(active.operationId);
    if (!refreshed)
        return;
    Data::SemanticOperationState terminalState = active.recoveryState;
    if (refreshed->state == Data::SemanticOperationState::OutcomeUnknown
        && terminalState == Data::SemanticOperationState::TimedOut) {
        terminalState = Data::SemanticOperationState::Failed;
    }
    const SemanticOperationJournalResult terminal = m_journal.transition(
        active.operationId,
        {
            refreshed->state,
            terminalState,
            executorActor(),
            active.recoveryCode,
            active.recoveryDetail,
            active.recoveryError,
            active.appliedCycle,
            active.plan.epoch().runtimeGeneration,
        });
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    publishJournalResult(terminal, controllerId);
    if (!guardedExecutor)
        return;
    if (!terminal.accepted()
        && (!terminal.record || terminal.record->state != terminalState)) {
        return;
    }
    active.mutationMayHaveExecuted = false;
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

    if (active.applyOutcomePending
        && record->currentStep == quint32(active.stepIndex)) {
        const SemanticOperationJournalResult step = m_journal.recordStep(
            active.operationId,
            {
                Data::SemanticOperationState::Executing,
                executorActor(),
                active.plan.steps().at(active.stepIndex).index() + 1,
                quint32(active.plan.steps().size()),
                false,
                QStringLiteral("Waiting for the same output transaction to be reconciled."),
                error,
            });
        QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
        publishJournalResult(step, controllerId);
        if (!guardedExecutor)
            return;
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    publishJournalResult(expired, controllerId);
    if (!guardedExecutor)
        return;
    if (!expired.accepted()
        && (!expired.record
            || expired.record->state != Data::SemanticOperationState::Expired)) {
        return;
    }
    finishActive(controllerId);
}

void SemanticRuntimeExecutorExecution::timeOutActive(
    const QString &controllerId, const QString &code, const QString &detail)
{
    auto queue = m_queues.find(controllerId);
    if (queue == m_queues.end() || !queue->active)
        return;
    const ActiveExecution &active = *queue->active;
    const auto record = m_journal.operation(active.operationId);
    if (!record || record->state != Data::SemanticOperationState::Executing)
        return;
    const SemanticOperationJournalResult timedOut = m_journal.transition(
        active.operationId,
        {
            Data::SemanticOperationState::Executing,
            Data::SemanticOperationState::TimedOut,
            executorActor(),
            code,
            conciseExecutionText(detail),
            {},
            active.appliedCycle,
            active.plan.epoch().runtimeGeneration,
        });
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    publishJournalResult(timedOut, controllerId);
    if (!guardedExecutor)
        return;
    if (!timedOut.accepted()
        && (!timedOut.record
            || timedOut.record->state != Data::SemanticOperationState::TimedOut)) {
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    publishJournalResult(succeeded, controllerId);
    if (!guardedExecutor)
        return;
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
    if (queue->active)
        m_adapterAuthorizations.remove(queue->active->operationId.value);
    queue->active.reset();
    QTimer::singleShot(0, q, [this, controllerId] { startNext(controllerId); });
}

void SemanticRuntimeExecutorExecution::providerRemoved(
    Core::ControllerConnectionProvider *provider)
{
    QList<QString> affected;
    QList<QPair<QString, quint64>> refreshes;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active
            && (queue->active->provider == provider || queue->active->provider.isNull())) {
            affected.append(queue.key());
        }
        if (queue->liveRefresh
            && (queue->liveRefresh->provider == provider || queue->liveRefresh->provider.isNull())) {
            refreshes.append({queue.key(), queue->liveRefresh->attemptNonce});
        }
    }
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("controller-provider-removed"),
            QStringLiteral("The controller provider was removed during execution."));
        if (!guardedExecutor)
            return;
    }
    for (const auto &[controllerId, attemptNonce] : std::as_const(refreshes)) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The controller provider was removed during live refresh."));
        if (!guardedExecutor)
            return;
    }
}

void SemanticRuntimeExecutorExecution::providerRegistryRemoved()
{
    QList<QString> affected;
    QList<QPair<QString, quint64>> refreshes;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active)
            affected.append(queue.key());
        if (queue->liveRefresh)
            refreshes.append({queue.key(), queue->liveRefresh->attemptNonce});
    }
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("provider-registry-removed"),
            QStringLiteral("The controller provider registry was removed during execution."));
        if (!guardedExecutor)
            return;
    }
    for (const auto &[controllerId, attemptNonce] : std::as_const(refreshes)) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-provider-failed"),
            QStringLiteral("The controller provider registry was removed."));
        if (!guardedExecutor)
            return;
    }
}

void SemanticRuntimeExecutorExecution::projectRemoved(const Data::NodeId &projectId)
{
    QList<QString> affected;
    QList<QPair<QString, quint64>> refreshes;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active) {
            const std::optional<Data::SemanticOperationRecord> record = m_journal.operation(
                queue->active->operationId);
            if (record && record->request.target.scope.projectId == projectId)
                affected.append(queue.key());
        }
        if (queue->liveRefresh && queue->liveRefresh->request.scope.projectId == projectId) {
            refreshes.append({queue.key(), queue->liveRefresh->attemptNonce});
        }
    }
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("project-removed"),
            QStringLiteral("The project was removed during execution."));
        if (!guardedExecutor)
            return;
    }
    for (const auto &[controllerId, attemptNonce] : std::as_const(refreshes)) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The refresh project was removed."));
        if (!guardedExecutor)
            return;
    }
    for (auto cache = q->m_liveRuntimeCaches.begin(); cache != q->m_liveRuntimeCaches.end();) {
        if (cache->scope.projectId == projectId)
            cache = q->m_liveRuntimeCaches.erase(cache);
        else
            ++cache;
    }
}

void SemanticRuntimeExecutorExecution::projectServiceRemoved()
{
    QList<QString> affected;
    QList<QPair<QString, quint64>> refreshes;
    for (auto queue = m_queues.cbegin(); queue != m_queues.cend(); ++queue) {
        if (queue->active)
            affected.append(queue.key());
        if (queue->liveRefresh)
            refreshes.append({queue.key(), queue->liveRefresh->attemptNonce});
    }
    QPointer<SemanticRuntimeExecutor> guardedExecutor(q);
    for (const QString &controllerId : std::as_const(affected)) {
        failActive(
            controllerId,
            QStringLiteral("project-service-removed"),
            QStringLiteral("The project service was removed during execution."));
        if (!guardedExecutor)
            return;
    }
    for (const auto &[controllerId, attemptNonce] : std::as_const(refreshes)) {
        finishLiveRefresh(
            controllerId,
            attemptNonce,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-context-changed"),
            QStringLiteral("The project service was removed during live refresh."));
        if (!guardedExecutor)
            return;
    }
}

SemanticRuntimeExecutor::~SemanticRuntimeExecutor() = default;

SemanticRuntimeExecutor::SemanticRuntimeExecutor(
    Core::ProjectService *projectService,
    Core::ProviderRegistry *providerRegistry,
    QObject *parent,
    std::shared_ptr<const RuntimePackageEvidenceRepository> evidenceRepository,
    int liveRefreshTimeoutMs,
    int liveSampleFreshnessMs,
    std::function<QDateTime()> utcNow,
    std::function<QList<Core::DeviceAdapterProvider *>()> availableAdapterProvidersOverride)
    : SemanticRuntimeService(parent)
    , m_projectService(projectService)
    , m_providerRegistry(providerRegistry)
    , m_evidenceRepository(std::move(evidenceRepository))
    , m_liveRefreshTimeoutMs(qMax(1, liveRefreshTimeoutMs))
    , m_liveSampleFreshnessMs(qMax(1, liveSampleFreshnessMs))
    , m_utcNow(utcNow ? std::move(utcNow) : [] { return QDateTime::currentDateTimeUtc(); })
    , m_availableAdapterProvidersOverride(std::move(availableAdapterProvidersOverride))
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
                QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
                m_execution->projectRemoved(projectId);
                if (!guardedExecutor)
                    return;
                clearEvidenceCache();
                publishContexts();
            });
        connect(m_projectService, &Core::ProjectService::activeProjectChanged, this, [this] {
            publishContexts();
        });
        connect(m_projectService, &QObject::destroyed, this, [this] {
            QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
            m_execution->projectServiceRemoved();
            if (!guardedExecutor)
                return;
            m_projectService = nullptr;
            m_liveRuntimeCaches.clear();
            publishContexts();
        });
    }

    if (m_providerRegistry) {
        connect(
            m_providerRegistry,
            &Core::ProviderRegistry::providerAdded,
            this,
            [this](Core::Provider *provider) {
                if (qobject_cast<Core::DeviceAdapterProvider *>(provider))
                    ++m_adapterProviderSignalGeneration;
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
                QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
                if (qobject_cast<Core::DeviceAdapterProvider *>(provider))
                    ++m_adapterProviderSignalGeneration;
                if (auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(
                        provider)) {
                    m_providersBeingRemoved.insert(connectionProvider);
                    m_execution->providerRemoved(connectionProvider);
                    if (!guardedExecutor)
                        return;
                }
                untrackProvider(provider);
                publishContexts();
            });
        connect(m_providerRegistry, &QObject::destroyed, this, [this] {
            QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
            m_execution->providerRegistryRemoved();
            if (!guardedExecutor)
                return;
            m_providerRegistry = nullptr;
            ++m_adapterProviderSignalGeneration;
            for (const QList<QMetaObject::Connection> &connections :
                 std::as_const(m_providerConnections)) {
                for (const QMetaObject::Connection &connection : connections)
                    disconnect(connection);
            }
            for (const QList<QMetaObject::Connection> &connections :
                 std::as_const(m_adapterProviderConnections)) {
                for (const QMetaObject::Connection &connection : connections)
                    disconnect(connection);
            }
            m_providerConnections.clear();
            m_adapterProviderConnections.clear();
            m_runtimeBootstrapStates.clear();
            m_runtimeBootstrapSignalGenerations.clear();
            m_runtimeCatalogSignalGenerations.clear();
            m_runtimeSnapshotSignalGenerations.clear();
            m_liveRuntimeCaches.clear();
            publishContexts();
        });
        for (Core::Provider *provider : m_providerRegistry->providers())
            trackProvider(provider);
    }

    m_contexts = buildContexts();
    scheduleRuntimeBootstrap();
}

QList<Data::SemanticRuntimeContext> SemanticRuntimeExecutor::contexts() const
{
    return m_contexts;
}

QDateTime SemanticRuntimeExecutor::utcNow() const
{
    const QDateTime now = m_utcNow ? m_utcNow() : QDateTime::currentDateTimeUtc();
    return now.isValid() ? now.toUTC() : QDateTime::currentDateTimeUtc();
}

bool SemanticRuntimeExecutor::liveSampleIsFresh(const QDateTime &receivedAt) const
{
    if (!receivedAt.isValid())
        return false;
    const qint64 ageMs = receivedAt.toUTC().msecsTo(utcNow());
    return ageMs >= 0 && ageMs <= m_liveSampleFreshnessMs;
}

void SemanticRuntimeExecutor::scheduleLiveCacheExpiry(
    Core::ControllerConnectionProvider *provider, const QDateTime &receivedAt)
{
    const qint64 ageMs = receivedAt.isValid() ? receivedAt.toUTC().msecsTo(utcNow()) : -1;
    const int delayMs = ageMs < 0
                            ? 1
                            : int(qMax<qint64>(1, qint64(m_liveSampleFreshnessMs) - ageMs + 1));
    QPointer<Core::ControllerConnectionProvider> guardedProvider(provider);
    QTimer::singleShot(delayMs, this, [this, guardedProvider] {
        if (guardedProvider && m_liveRuntimeCaches.contains(guardedProvider))
            publishContexts();
    });
}

Data::SemanticLiveRefreshResult SemanticRuntimeExecutor::requestLiveRefresh(
    const Data::SemanticLiveRefreshRequest &request)
{
    return m_execution->requestLiveRefresh(request);
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
    QPointer<SemanticRuntimeExecutor> guardedExecutor(const_cast<SemanticRuntimeExecutor *>(this));
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
        for (const Data::NodeId &masterId : std::as_const(masterIds)) {
            const Data::SemanticRuntimeContext context = buildContext(project, masterId);
            if (!guardedExecutor)
                return {};
            contexts.append(context);
        }
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
        QPointer<Core::ControllerConnectionProvider> provider;
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
    if (!explicitProjectDeviceTopologyMatches(project, *evidence, &projectDeviceDetail)) {
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
        QPointer<SemanticRuntimeExecutor> guardedExecutor(
            const_cast<SemanticRuntimeExecutor *>(this));
        const AvailableDeviceAdapterProviders adapterProviders{
            m_availableAdapterProvidersOverride
                ? m_availableAdapterProvidersOverride
                : availableDeviceAdapterProviderList(m_providerRegistry),
            guardedExecutor,
            [guardedExecutor] {
                return guardedExecutor ? guardedExecutor->m_adapterProviderSignalGeneration
                                       : std::numeric_limits<quint64>::max();
            },
        };
        const Utils::Result<QList<Data::SemanticActionRuntimeState>> actions
            = buildSemanticActionRuntimeStates(
                context.controllerId,
                project,
                *evidence,
                *bindingCandidates,
                adapterProviders,
                gates);
        if (!guardedExecutor || !candidate.provider)
            return context;
        QList<QPointer<Core::ControllerConnectionProvider>> currentProviders;
        if (m_providerRegistry) {
            const QList<Core::Provider *> registered = m_providerRegistry->providers(
                Core::ProviderKind::ControllerConnection);
            if (!guardedExecutor || !m_providerRegistry)
                return context;
            for (Core::Provider *provider : registered) {
                QPointer<Core::ControllerConnectionProvider> connectionProvider
                    = qobject_cast<Core::ControllerConnectionProvider *>(provider);
                if (!connectionProvider)
                    return context;
                currentProviders.append(connectionProvider);
            }
        }
        QList<QPointer<Core::ControllerConnectionProvider>> exactProviders;
        for (const QPointer<Core::ControllerConnectionProvider> &provider :
             std::as_const(currentProviders)) {
            if (!provider || m_providersBeingRemoved.contains(provider.data()))
                return context;
            const bool available = provider->isAvailable();
            if (!guardedExecutor || !provider)
                return context;
            const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
            if (!guardedExecutor || !provider)
                return context;
            if (available && snapshot.scope == context.scope)
                exactProviders.append(provider);
        }
        if (exactProviders.size() != 1 || exactProviders.constFirst() != candidate.provider) {
            rejectContext(context, ContextIssue::ControllerProviderAmbiguous);
            return context;
        }
        const Data::ControllerConnectionSnapshot currentControllerSnapshot
            = candidate.provider->connectionSnapshot();
        if (!guardedExecutor || !candidate.provider)
            return context;
        if (currentControllerSnapshot != candidate.snapshot) {
            rejectContext(context, ContextIssue::ControllerSessionUnavailable);
            return context;
        }
        if (!guardedExecutor || !candidate.provider)
            return context;
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

    if (!candidate.provider)
        return context;
    const std::optional<Data::RuntimeResourceSnapshot> snapshot
        = candidate.provider->runtimeResourceSnapshot();
    if (!candidate.provider)
        return context;
    ContextIssue snapshotIssue = ContextIssue::RuntimeResourceSnapshotUnavailable;
    const bool snapshotIdentityMatches = snapshot && snapshot->scope == context.scope
                                         && snapshot->sessionGeneration == context.sessionGeneration
                                         && snapshot->epoch == context.epoch;
    const bool completeSnapshot = snapshotIdentityMatches
                                  && snapshotHasExactBindingSet(*snapshot, *bindingCandidates);
    if (snapshot && !snapshotIdentityMatches)
        snapshotIssue = ContextIssue::RuntimeResourceSnapshotStale;
    else if (snapshotIdentityMatches && !completeSnapshot)
        snapshotIssue = ContextIssue::RuntimeResourceSnapshotIncomplete;

    const auto liveCache = m_liveRuntimeCaches.constFind(candidate.provider.data());
    const bool liveCacheMatches = liveCache != m_liveRuntimeCaches.cend()
                                  && liveCache->controllerId == context.controllerId
                                  && liveCache->scope == context.scope
                                  && liveCache->sessionGeneration == context.sessionGeneration
                                  && liveCache->epoch == context.epoch
                                  && liveCache->mappingDigest == context.mappingDigest
                                  && liveCache->controllerMappingDigest
                                         == context.controllerMappingDigest
                                  && liveCache->contextHash == context.contextHash;

    for (qsizetype index = 0; index < bindingCandidates->bindings.size(); ++index) {
        Data::SemanticSignalRuntimeState &state = context.signalStates[index];
        const Data::SemanticRuntimeBinding &binding = bindingCandidates->bindings.at(index);
        state.availability = Data::SemanticSignalAvailability::Unverified;
        state.value.reset();
        state.quality = {};
        state.snapshotComplete = false;
        state.captureCycle = 0;
        state.controllerTimestampNs = 0;
        state.observedAt = {};
        state.detail = semanticRuntimeContextIssueDetail(snapshotIssue);

        const auto applySnapshot = [&state, &binding, &catalog](
                                       const Data::RuntimeResourceSnapshot &candidateSnapshot,
                                       const QDateTime &observedAt) {
            const Core::SemanticRuntimeReadValidation validation
                = Core::validateSemanticRuntimeRead(binding, *catalog, candidateSnapshot);
            if (!validation.validation.accepted() || !validation.sample)
                return false;
            state.availability = Data::SemanticSignalAvailability::Ready;
            state.value = validation.sample->value;
            state.quality = validation.sample->quality;
            state.snapshotComplete = true;
            state.captureCycle = candidateSnapshot.captureCycle;
            state.controllerTimestampNs = validation.sample->controllerTimestampNs;
            state.observedAt = observedAt;
            state.detail = QStringLiteral("Verified live runtime sample.");
            return true;
        };

        if (completeSnapshot) {
            if (!applySnapshot(*snapshot, snapshot->receivedAt))
                state.detail = semanticRuntimeContextIssueDetail(
                    ContextIssue::RuntimeResourceSnapshotInvalid);
        }

        if (liveCacheMatches) {
            const auto liveSample = liveCache->samples.constFind(binding.resourceId);
            if (liveSample != liveCache->samples.cend()
                && liveSample->captureCycle >= state.captureCycle) {
                Data::RuntimeResourceSnapshot targeted;
                targeted.scope = context.scope;
                targeted.sessionGeneration = context.sessionGeneration;
                targeted.epoch = context.epoch;
                targeted.captureCycle = liveSample->captureCycle;
                targeted.controllerTimestampNs = liveSample->controllerTimestampNs;
                targeted.receivedAt = liveSample->receivedAt;
                targeted.complete = true;
                targeted.samples = {liveSample->sample};
                if (applySnapshot(targeted, liveSample->receivedAt)) {
                    if (!liveSampleIsFresh(liveSample->receivedAt)) {
                        state.availability = Data::SemanticSignalAvailability::Stale;
                        state.quality.state = Data::RuntimeResourceQualityState::Stale;
                        state.quality.detail = QStringLiteral(
                            "The verified live sample exceeded its freshness budget.");
                        state.snapshotComplete = false;
                        state.detail = QStringLiteral(
                            "semantic-live-sample-stale: The verified live sample is stale.");
                    }
                } else {
                    state.availability = Data::SemanticSignalAvailability::Unverified;
                    state.value.reset();
                    state.quality = {};
                    state.snapshotComplete = false;
                    state.captureCycle = 0;
                    state.controllerTimestampNs = 0;
                    state.observedAt = {};
                    state.detail = semanticRuntimeContextIssueDetail(
                        ContextIssue::RuntimeResourceSnapshotInvalid);
                }
            }
        }
    }

    bool allSignalsReady = !context.signalStates.isEmpty();
    bool captureCohortCoherent = true;
    std::optional<QPair<quint64, quint64>> readyCohort;
    for (const Data::SemanticSignalRuntimeState &state : std::as_const(context.signalStates)) {
        if (state.availability != Data::SemanticSignalAvailability::Ready || !state.snapshotComplete
            || !state.captureCycle || !state.controllerTimestampNs
            || state.quality.state != Data::RuntimeResourceQualityState::Good || !state.value) {
            allSignalsReady = false;
            continue;
        }
        const QPair<quint64, quint64> cohort{state.captureCycle, state.controllerTimestampNs};
        if (!readyCohort)
            readyCohort = cohort;
        else if (*readyCohort != cohort)
            captureCohortCoherent = false;
    }
    const bool allSamplesReady = allSignalsReady && captureCohortCoherent;
    context.detail
        = allSamplesReady
              ? QStringLiteral(
                    "semantic-runtime-ready: Signed bindings and live samples are verified.")
              : (allSignalsReady
                     ? QStringLiteral(
                           "runtime-resource-snapshot-incoherent: Live samples do not share one "
                           "capture boundary.")
                     : semanticRuntimeContextIssueDetail(
                           ContextIssue::RuntimeResourceSnapshotInvalid));
    return context;
}

void SemanticRuntimeExecutor::publishContexts()
{
    if (m_publishingContexts) {
        m_publishContextsPending = true;
        return;
    }
    m_publishingContexts = true;
    m_publishContextsPending = false;
    QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
    const quint64 adapterGeneration = m_adapterProviderSignalGeneration;
    scheduleRuntimeBootstrap();
    const QList<Data::SemanticRuntimeContext> next = buildContexts();
    if (!guardedExecutor)
        return;
    const bool stale = m_publishContextsPending
                       || adapterGeneration != m_adapterProviderSignalGeneration;
    if (stale) {
        if (!m_contexts.isEmpty()) {
            m_contexts.clear();
            emit contextsChanged();
            if (!guardedExecutor)
                return;
        }
        m_publishingContexts = false;
        m_publishContextsPending = false;
        QTimer::singleShot(0, this, [this] { publishContexts(); });
        return;
    }
    if (next != m_contexts) {
        m_contexts = next;
        emit contextsChanged();
        if (!guardedExecutor)
            return;
    }
    m_publishingContexts = false;
    if (m_publishContextsPending) {
        m_publishContextsPending = false;
        QTimer::singleShot(0, this, [this] { publishContexts(); });
    }
}

void SemanticRuntimeExecutor::scheduleRuntimeBootstrap()
{
    if (m_runtimeBootstrapScheduled)
        return;
    m_runtimeBootstrapScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_runtimeBootstrapScheduled = false;
        processRuntimeBootstrap();
    });
}

void SemanticRuntimeExecutor::processRuntimeBootstrap()
{
    struct Candidate
    {
        Core::ControllerConnectionProvider *provider = nullptr;
        Data::ControllerConnectionScope scope;
        Data::ControllerConnectionSnapshot snapshot;
        std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence;
        QByteArray identityKey;
    };

    QList<Candidate> candidates;
    QSet<Core::ControllerConnectionProvider *> eligibleProviders;
    if (m_projectService && m_projectService->isAvailable() && m_providerRegistry
        && m_evidenceRepository) {
        for (const Data::ProjectSnapshot &project : m_projectService->projects()) {
            if (!project.valid || project.id.isNull()
                || m_projectsBeingRemoved.contains(project.id)
                || !bindingArtifactIsValid(project.masterBindingArtifact)) {
                continue;
            }

            QString evidenceError;
            const std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence
                = cachedEvidence(project.masterBindingArtifact, &evidenceError);
            if (!evidence || !evidence->isValid())
                continue;

            QList<Data::NodeId> masterIds;
            for (const Data::ProjectNodeSnapshot &node : project.nodes) {
                if (node.kind == Data::ProjectNodeKind::Master && !node.id.isNull()
                    && !masterIds.contains(node.id)) {
                    masterIds.append(node.id);
                }
            }
            for (const Data::NodeId &masterId : std::as_const(masterIds)) {
                const Data::ControllerConnectionScope scope{project.id, masterId};
                QList<Core::ControllerConnectionProvider *> matchingProviders;
                QHash<Core::ControllerConnectionProvider *, Data::ControllerConnectionSnapshot>
                    snapshots;
                for (Core::Provider *provider :
                     m_providerRegistry->providers(Core::ProviderKind::ControllerConnection)) {
                    auto *connectionProvider
                        = qobject_cast<Core::ControllerConnectionProvider *>(provider);
                    if (!connectionProvider
                        || m_providersBeingRemoved.contains(connectionProvider)
                        || !connectionProvider->isAvailable()) {
                        continue;
                    }
                    const Data::ControllerConnectionSnapshot snapshot
                        = connectionProvider->connectionSnapshot();
                    if (snapshot.scope != scope)
                        continue;
                    matchingProviders.append(connectionProvider);
                    snapshots.insert(connectionProvider, snapshot);
                }
                if (matchingProviders.size() != 1)
                    continue;

                Core::ControllerConnectionProvider *provider
                    = matchingProviders.constFirst();
                const Data::ControllerConnectionSnapshot snapshot = snapshots.value(provider);
                if (!isConnected(snapshot.state) || !snapshot.sessionGeneration
                    || runtimeBootstrapOperationInProgress(snapshot)
                    || !provider->supportsRuntimeResources()
                    || !provider->supportsRuntimeSemanticMappingAttestation()) {
                    continue;
                }

                Candidate candidate;
                candidate.provider = provider;
                candidate.scope = scope;
                candidate.snapshot = snapshot;
                candidate.evidence = evidence;
                candidate.identityKey = runtimeBootstrapIdentityKey(
                    scope, snapshot, project.masterBindingArtifact, *evidence);
                candidates.append(std::move(candidate));
                eligibleProviders.insert(provider);
            }
        }
    }

    for (auto state = m_runtimeBootstrapStates.begin();
         state != m_runtimeBootstrapStates.end();) {
        if (!eligibleProviders.contains(state.key()))
            state = m_runtimeBootstrapStates.erase(state);
        else
            ++state;
    }

    for (const Candidate &candidate : std::as_const(candidates)) {
        QPointer<Core::ControllerConnectionProvider> provider(candidate.provider);
        RuntimeBootstrapState &state = m_runtimeBootstrapStates[candidate.provider];
        if (state.identityKey != candidate.identityKey) {
            state = {};
            state.identityKey = candidate.identityKey;
            state.catalogSignalBaseline
                = m_runtimeCatalogSignalGenerations.value(candidate.provider);
            state.snapshotSignalBaseline
                = m_runtimeSnapshotSignalGenerations.value(candidate.provider);
            state.refreshRetrySignalBaseline
                = m_runtimeBootstrapSignalGenerations.value(candidate.provider);
        }

        const quint64 bootstrapSignalGeneration
            = m_runtimeBootstrapSignalGenerations.value(candidate.provider);
        const bool firstRefresh = state.refreshAttemptCount == 0;
        const bool retryRejectedRefresh
            = !state.refreshAccepted && state.refreshAttemptCount == 1
              && bootstrapSignalGeneration > state.refreshRetrySignalBaseline;
        if (firstRefresh || retryRejectedRefresh) {
            if (firstRefresh) {
                state.catalogSignalBaseline
                    = m_runtimeCatalogSignalGenerations.value(candidate.provider);
                state.snapshotSignalBaseline
                    = m_runtimeSnapshotSignalGenerations.value(candidate.provider);
            }
            ++state.refreshAttemptCount;
            state.refreshRetrySignalBaseline = bootstrapSignalGeneration;
            const QByteArray identityKey = state.identityKey;
            QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
            const Utils::Result<> accepted = candidate.provider->refreshRuntimeResources();
            if (!guardedExecutor)
                return;
            if (provider) {
                const auto current = m_runtimeBootstrapStates.find(candidate.provider);
                if (current != m_runtimeBootstrapStates.end()
                    && current->identityKey == identityKey && accepted) {
                    current->refreshAccepted = true;
                }
            }
            continue;
        }
        if (!provider
            || m_runtimeCatalogSignalGenerations.value(candidate.provider)
                   <= state.catalogSignalBaseline) {
            continue;
        }

        const std::optional<Data::RuntimeResourceCatalog> catalog
            = candidate.provider->runtimeResourceCatalog();
        if (!catalog || catalog->scope != candidate.scope
            || catalog->sessionGeneration != candidate.snapshot.sessionGeneration
            || catalog->resources.isEmpty()
            || !Core::isCompleteRuntimeResourceCatalogEpoch(catalog->epoch)) {
            continue;
        }

        const Data::RuntimeSemanticMappingProof &proof
            = candidate.evidence->semanticMappingProof();
        const QByteArray attestationKey = runtimeBootstrapAttestationKey(
            candidate.identityKey, catalog->epoch, proof);
        bool issueRequest = false;
        if (state.attestationAttemptKey != attestationKey) {
            state.attestationAttemptKey = attestationKey;
            state.attestationAccepted = false;
            state.attestationRetryUsed = false;
            state.snapshotSignalBaseline
                = m_runtimeSnapshotSignalGenerations.value(candidate.provider);
            issueRequest = true;
        } else if (!state.attestationAccepted && !state.attestationRetryUsed
                   && m_runtimeSnapshotSignalGenerations.value(candidate.provider)
                          > state.snapshotSignalBaseline) {
            // Product API publishes the catalog before its initial resource snapshot. The first
            // attestation call can therefore be rejected locally as busy; retry exactly once
            // after that same refresh publishes its snapshot.
            state.attestationRetryUsed = true;
            issueRequest = true;
        }
        if (!issueRequest)
            continue;

        Data::RuntimeSemanticMappingAttestationRequest request;
        request.correlationId = QStringLiteral("semantic-runtime-bootstrap/%1")
                                    .arg(QString::fromLatin1(attestationKey.toHex()));
        request.scope = candidate.scope;
        request.sessionGeneration = candidate.snapshot.sessionGeneration;
        request.expectedEpoch = catalog->epoch;
        request.expectedProof = proof;
        if (!request.isValid())
            continue;

        QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
        const Utils::Result<> accepted
            = candidate.provider->requestRuntimeSemanticMappingAttestation(request);
        if (!guardedExecutor)
            return;
        if (provider && accepted) {
            const auto current = m_runtimeBootstrapStates.find(candidate.provider);
            if (current != m_runtimeBootstrapStates.end()
                && current->identityKey == candidate.identityKey
                && current->attestationAttemptKey == attestationKey) {
                current->attestationAccepted = true;
            }
        }
    }
}

void SemanticRuntimeExecutor::trackProvider(Core::Provider *provider)
{
    if (auto *adapterProvider = qobject_cast<Core::DeviceAdapterProvider *>(provider)) {
        if (m_adapterProviderConnections.contains(adapterProvider))
            return;

        QList<QMetaObject::Connection> connections;
        connections.append(
            connect(adapterProvider, &Core::Provider::availabilityChanged, this, [this] {
                ++m_adapterProviderSignalGeneration;
                publishContexts();
            }));
        connections.append(connect(
            adapterProvider, &Core::DeviceAdapterProvider::adapterManifestsChanged, this, [this] {
                ++m_adapterProviderSignalGeneration;
                publishContexts();
            }));
        connections.append(
            connect(adapterProvider, &QObject::destroyed, this, [this, adapterProvider] {
                ++m_adapterProviderSignalGeneration;
                m_adapterProviderConnections.remove(adapterProvider);
                publishContexts();
            }));
        m_adapterProviderConnections.insert(adapterProvider, connections);
        // Provider has one immutable ProviderKind, so a DeviceAdapterProvider
        // cannot also be a ControllerConnectionProvider.
        return;
    }

    auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider);
    if (!connectionProvider || m_providerConnections.contains(connectionProvider))
        return;

    m_runtimeBootstrapSignalGenerations.tryInsert(connectionProvider, 0);
    m_runtimeCatalogSignalGenerations.tryInsert(connectionProvider, 0);
    m_runtimeSnapshotSignalGenerations.tryInsert(connectionProvider, 0);

    QList<QMetaObject::Connection> connections;
    connections.append(connect(
        connectionProvider,
        &Core::Provider::availabilityChanged,
        this,
        [this, connectionProvider] {
            ++m_runtimeBootstrapSignalGenerations[connectionProvider];
            publishContexts();
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::connectionSnapshotChanged,
        this,
        [this, connectionProvider] {
            ++m_runtimeBootstrapSignalGenerations[connectionProvider];
            publishContexts();
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceCatalogChanged,
        this,
        [this, connectionProvider] {
            ++m_runtimeCatalogSignalGenerations[connectionProvider];
            publishContexts();
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotChanged,
        this,
        [this, connectionProvider] {
            ++m_runtimeBootstrapSignalGenerations[connectionProvider];
            ++m_runtimeSnapshotSignalGenerations[connectionProvider];
            publishContexts();
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished,
        this,
        [this, connectionProvider](const Data::RuntimeResourceSnapshotResult &) {
            ++m_runtimeBootstrapSignalGenerations[connectionProvider];
            publishContexts();
        }));
    connections.append(connect(
        connectionProvider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished,
        this,
        [this, connectionProvider](const Data::RuntimeResourceSnapshotResult &result) {
            if (!m_execution->handleLiveSnapshot(connectionProvider, result))
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
            QPointer<SemanticRuntimeExecutor> guardedExecutor(this);
            m_execution->providerRemoved(connectionProvider);
            if (!guardedExecutor)
                return;
            m_providerConnections.remove(connectionProvider);
            m_providersBeingRemoved.remove(connectionProvider);
            m_runtimeBootstrapStates.remove(connectionProvider);
            m_runtimeBootstrapSignalGenerations.remove(connectionProvider);
            m_runtimeCatalogSignalGenerations.remove(connectionProvider);
            m_runtimeSnapshotSignalGenerations.remove(connectionProvider);
            m_liveRuntimeCaches.remove(connectionProvider);
            publishContexts();
        }));
    m_providerConnections.insert(connectionProvider, connections);
}

void SemanticRuntimeExecutor::untrackProvider(Core::Provider *provider)
{
    if (auto *adapterProvider = qobject_cast<Core::DeviceAdapterProvider *>(provider)) {
        ++m_adapterProviderSignalGeneration;
        const QList<QMetaObject::Connection> connections
            = m_adapterProviderConnections.take(adapterProvider);
        for (const QMetaObject::Connection &connection : connections)
            disconnect(connection);
        return;
    }

    auto *connectionProvider = qobject_cast<Core::ControllerConnectionProvider *>(provider);
    if (!connectionProvider)
        return;
    const QList<QMetaObject::Connection> connections = m_providerConnections.take(
        connectionProvider);
    for (const QMetaObject::Connection &connection : connections)
        disconnect(connection);
    m_runtimeBootstrapStates.remove(connectionProvider);
    m_runtimeBootstrapSignalGenerations.remove(connectionProvider);
    m_runtimeCatalogSignalGenerations.remove(connectionProvider);
    m_runtimeSnapshotSignalGenerations.remove(connectionProvider);
    m_liveRuntimeCaches.remove(connectionProvider);
}

} // namespace EtherCAT::SemanticRuntime::Internal
