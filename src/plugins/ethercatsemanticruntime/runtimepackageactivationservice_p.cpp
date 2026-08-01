// Copyright (C) 2026 Embed Labs

#include "runtimepackageactivationservice_p.h"

#include "runtimepackageevidence_p.h"
#include "runtimepackageevidencerepository_p.h"
#include "runtimepackageactivationjournalcodec_p.h"
#include "semanticbindingartifact_p.h"

#include <utils/qtcassert.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

using Action = Data::RuntimePackageActivationProviderAction;
using AuditKind = Data::RuntimePackageActivationAuditKind;
using Outcome = Data::RuntimePackageActivationOutcome;
using Phase = Data::RuntimePackageActivationPhase;
using Reconciliation = Data::RuntimePackageActivationProviderReconciliation;

constexpr auto journalSuffix = ".pending.json";

QString concise(QString text)
{
    text = text.simplified();
    if (text.isEmpty())
        text = QStringLiteral("Runtime package activation failed.");
    if (text.size() > 2048)
        text = text.left(2045) + QStringLiteral("...");
    return text;
}

QDateTime nextEventTime(const QList<Data::RuntimePackageActivationAuditEvent> &audit)
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    if (!audit.isEmpty() && now < audit.constLast().occurredAt())
        now = audit.constLast().occurredAt();
    return now;
}

QByteArray u64BigEndian(quint64 value)
{
    QByteArray bytes(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, bytes.data());
    return bytes;
}

bool exactUnsignedInteger(const QJsonValue &value, quint64 expected)
{
    constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return expected <= maximumExactJsonInteger
           && number >= 0 && number <= double(maximumExactJsonInteger)
           && number == double(expected)
           && quint64(number) == expected;
}

std::optional<QByteArray> sha256JsonValue(
    const QJsonObject &object, QStringView key)
{
    const QString value = object.value(key).toString();
    if (value.size() != 64)
        return std::nullopt;
    const QByteArray bytes = QByteArray::fromHex(value.toLatin1());
    if (bytes.size() != QCryptographicHash::hashLength(
                            QCryptographicHash::Sha256)
        || QString::fromLatin1(bytes.toHex()) != value) {
        return std::nullopt;
    }
    quint8 aggregate = 0;
    for (char byte : bytes)
        aggregate |= quint8(byte);
    if (!aggregate)
        return std::nullopt;
    return bytes;
}

Utils::Result<> verifyEffectiveProjectCompanion(
    const Core::RuntimePackageActivationPreparationRequest &request,
    const Data::RuntimePackageActivationProjectCapture &capture,
    QByteArrayView compiledProjectSha256,
    const VerifiedRuntimePackageEvidence &evidence)
{
    const Data::RuntimePackageCompilerCanonicalJson canonical
        = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(
            request.effectiveProjectCompanion);
    if (!canonical.isValid()) {
        return Utils::ResultError(
            QStringLiteral(
                "The effective-project companion is not exact canonical API-042 JSON."));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        request.effectiveProjectCompanion, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Utils::ResultError(
            QStringLiteral("The effective-project companion is not a JSON object."));
    }
    const QJsonObject object = document.object();
    static const QSet<QString> expectedKeys{
        QStringLiteral("adapter_bundle_sha256"),
        QStringLiteral("compiled_project_sha256"),
        QStringLiteral("configuration_id"),
        QStringLiteral("devices"),
        QStringLiteral("document_revision"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("intent_sha256"),
        QStringLiteral("master"),
        QStringLiteral("master_node_id"),
        QStringLiteral("project_id"),
        QStringLiteral("target_profile_sha256"),
        QStringLiteral("topology_evidence_sha256"),
    };
    const QStringList objectKeys = object.keys();
    const QSet<QString> actualKeys(objectKeys.cbegin(), objectKeys.cend());
    if (actualKeys != expectedKeys
        || object.value(QStringLiteral("format")).toString()
               != QStringLiteral("ethercat-effective-project-companion-v1")
        || object.value(QStringLiteral("format_version")).toInt() != 1
        || object.value(QStringLiteral("project_id")).toString()
               != request.scope.projectId.toString()
        || object.value(QStringLiteral("master_node_id")).toString()
               != request.scope.masterId.toString()
        || !exactUnsignedInteger(
            object.value(QStringLiteral("document_revision")),
            capture.documentRevisionNumber())
        || !exactUnsignedInteger(
            object.value(QStringLiteral("configuration_id")),
            request.compilerVerification.envelope.configurationId)
        || !object.value(QStringLiteral("master")).isObject()
        || !object.value(QStringLiteral("devices")).isArray()) {
        return Utils::ResultError(
            QStringLiteral(
                "The effective-project companion does not identify the exact current "
                "project, master, document revision, or compiler configuration."));
    }

    const std::optional<QByteArray> companionProjectSha256
        = sha256JsonValue(object, u"compiled_project_sha256");
    const std::optional<QByteArray> intentSha256
        = sha256JsonValue(object, u"intent_sha256");
    const std::optional<QByteArray> targetProfileSha256
        = sha256JsonValue(object, u"target_profile_sha256");
    const std::optional<QByteArray> adapterBundleSha256
        = sha256JsonValue(object, u"adapter_bundle_sha256");
    const std::optional<QByteArray> topologyEvidenceSha256
        = sha256JsonValue(object, u"topology_evidence_sha256");
    if (!companionProjectSha256 || !intentSha256 || !targetProfileSha256
        || !adapterBundleSha256 || !topologyEvidenceSha256
        || *companionProjectSha256 != compiledProjectSha256
        || *intentSha256
               != request.compilerVerification.intentSha256.value()
        || *targetProfileSha256
               != request.compilerVerification.targetProfileSha256.value()
        || *adapterBundleSha256
               != request.compilerVerification.adapterBundleSha256.value()
        || *topologyEvidenceSha256
               != request.compilerVerification.topologyEvidenceSha256.value()) {
        return Utils::ResultError(
            QStringLiteral(
                "The effective-project companion does not bind the exact lower compiled "
                "project and compiler verification digests."));
    }

    const QJsonObject master
        = object.value(QStringLiteral("master")).toObject();
    static const QSet<QString> expectedMasterKeys{
        QStringLiteral("cycle_period_ns"),
        QStringLiteral("link_speed_mbps"),
        QStringLiteral("timing_mode"),
    };
    const QStringList masterKeys = master.keys();
    const QSet<QString> actualMasterKeys(
        masterKeys.cbegin(), masterKeys.cend());
    QString expectedTimingMode;
    switch (capture.snapshot().masterConfiguration.timingMode) {
    case Data::MasterTimingMode::FreeRun:
        expectedTimingMode = QStringLiteral("free_run");
        break;
    case Data::MasterTimingMode::DistributedClocks:
        expectedTimingMode = QStringLiteral("dc");
        break;
    case Data::MasterTimingMode::Unassigned:
        break;
    }
    if (actualMasterKeys != expectedMasterKeys
        || expectedTimingMode.isEmpty()
        || master.value(QStringLiteral("timing_mode")).toString()
               != expectedTimingMode
        || !exactUnsignedInteger(
            master.value(QStringLiteral("cycle_period_ns")),
            capture.snapshot().masterConfiguration.cyclePeriodNs)
        || !exactUnsignedInteger(
            master.value(QStringLiteral("link_speed_mbps")), 100)) {
        return Utils::ResultError(
            QStringLiteral(
                "The effective-project companion master timing differs from the exact "
                "current project."));
    }

    QList<const Data::OfflineSlaveConfiguration *> scopedSlaves;
    for (const Data::OfflineSlaveConfiguration &slave :
         capture.snapshot().slaves) {
        if (slave.masterId == request.scope.masterId)
            scopedSlaves.append(&slave);
    }
    const QJsonArray devices
        = object.value(QStringLiteral("devices")).toArray();
    if (devices.size() != scopedSlaves.size()
        || devices.size()
               != evidence.semanticBindingArtifact().devices.size()) {
        return Utils::ResultError(
            QStringLiteral(
                "The effective-project companion device count differs from the exact "
                "current project or signed package."));
    }

    static const QSet<QString> expectedDeviceKeys{
        QStringLiteral("adapter_id"),
        QStringLiteral("adapter_sha256"),
        QStringLiteral("adapter_version"),
        QStringLiteral("binding_identity_sha256"),
        QStringLiteral("dc_sha256"),
        QStringLiteral("esi_sha256"),
        QStringLiteral("identity_sha256"),
        QStringLiteral("manual_envelope_sha256"),
        QStringLiteral("module_assignments_sha256"),
        QStringLiteral("pdo_selection_sha256"),
        QStringLiteral("position"),
        QStringLiteral("project_device_id"),
        QStringLiteral("slave_node_id"),
        QStringLiteral("startup_sdo_sha256"),
        QStringLiteral("station_address"),
    };
    static const QStringList opaqueProjectDigestKeys{
        QStringLiteral("binding_identity_sha256"),
        QStringLiteral("dc_sha256"),
        QStringLiteral("identity_sha256"),
        QStringLiteral("manual_envelope_sha256"),
        QStringLiteral("module_assignments_sha256"),
        QStringLiteral("pdo_selection_sha256"),
        QStringLiteral("startup_sdo_sha256"),
    };
    QSet<QString> seenSlaveIds;
    QSet<QString> seenProjectDeviceIds;
    for (const QJsonValue &deviceValue : devices) {
        if (!deviceValue.isObject()) {
            return Utils::ResultError(
                QStringLiteral(
                    "The effective-project companion contains a non-object device."));
        }
        const QJsonObject device = deviceValue.toObject();
        const QStringList deviceKeys = device.keys();
        const QSet<QString> actualDeviceKeys(
            deviceKeys.cbegin(), deviceKeys.cend());
        const QString slaveId
            = device.value(QStringLiteral("slave_node_id")).toString();
        const QString projectDeviceId
            = device.value(QStringLiteral("project_device_id")).toString();
        const auto slave = std::find_if(
            scopedSlaves.cbegin(),
            scopedSlaves.cend(),
            [&slaveId](const Data::OfflineSlaveConfiguration *candidate) {
                return candidate->id.toString() == slaveId;
            });
        if (actualDeviceKeys != expectedDeviceKeys
            || slave == scopedSlaves.cend()
            || slaveId.isEmpty() || projectDeviceId.isEmpty()
            || seenSlaveIds.contains(slaveId)
            || seenProjectDeviceIds.contains(projectDeviceId)
            || !exactUnsignedInteger(
                device.value(QStringLiteral("position")),
                quint64((*slave)->position))
            || !exactUnsignedInteger(
                device.value(QStringLiteral("station_address")),
                (*slave)->stationAddress)) {
            return Utils::ResultError(
                QStringLiteral(
                    "The effective-project companion device identity differs from the "
                    "exact current project."));
        }
        seenSlaveIds.insert(slaveId);
        seenProjectDeviceIds.insert(projectDeviceId);

        const auto signedDevice = std::find_if(
            evidence.semanticBindingArtifact().devices.cbegin(),
            evidence.semanticBindingArtifact().devices.cend(),
            [&projectDeviceId, &device](const VerifiedSemanticDevice &candidate) {
                return candidate.projectDeviceId == projectDeviceId
                       && exactUnsignedInteger(
                           device.value(QStringLiteral("position")),
                           candidate.position)
                       && exactUnsignedInteger(
                           device.value(QStringLiteral("station_address")),
                           candidate.stationAddress);
            });
        const std::optional<QByteArray> adapterSha256
            = sha256JsonValue(device, u"adapter_sha256");
        const std::optional<QByteArray> esiSha256
            = sha256JsonValue(device, u"esi_sha256");
        if (signedDevice
                == evidence.semanticBindingArtifact().devices.cend()
            || !adapterSha256 || !esiSha256
            || device.value(QStringLiteral("adapter_id")).toString()
                   != signedDevice->adapterId
            || device.value(QStringLiteral("adapter_version")).toString()
                   != signedDevice->adapterVersion
            || *adapterSha256 != signedDevice->adapterSha256
            || *esiSha256 != signedDevice->esiSha256
            || *esiSha256 != (*slave)->esiSha256) {
            return Utils::ResultError(
                QStringLiteral(
                    "The effective-project companion device does not identify the exact "
                    "signed adapter and ESI."));
        }
        // These API-042 hashes are compiler-owned canonical projections. The
        // activation service validates their exact schema and compiler-bound
        // companion bytes; it does not reimplement the lower compiler's
        // PDO/SDO/DC/manual-envelope canonicalization.
        for (const QString &key : opaqueProjectDigestKeys) {
            if (!sha256JsonValue(device, key)) {
                return Utils::ResultError(
                    QStringLiteral(
                        "The effective-project companion contains an invalid device "
                        "projection digest."));
            }
        }
    }
    return Utils::ResultOk;
}

bool connected(Data::ControllerConnectionState state)
{
    return state == Data::ControllerConnectionState::Connected
           || state == Data::ControllerConnectionState::Degraded;
}

bool deploymentTerminal(Data::ControllerPackageDeploymentState state)
{
    using State = Data::ControllerPackageDeploymentState;
    return state == State::Succeeded || state == State::Canceled
           || state == State::Failed || state == State::OutcomeUnknown;
}

bool deploymentProgressCanRefreshDeadline(
    const Data::ControllerPackageDeploymentProgress &previous,
    const Data::ControllerPackageDeploymentProgress &current,
    const Data::RuntimePackageActivationRequest &request,
    const QDateTime &requestSentAt)
{
    if (current == previous
        || current.operationId
               != request.identity().operationId().value()
        || current.artifactSha256
               != request.identity().packageSha256().value()
        || current.totalBytes != request.packageBytes().size()
        || current.transferredBytes < 0
        || current.transferredBytes > current.totalBytes
        || !current.startedAt.isValid()
        || current.startedAt < requestSentAt
        || current.audit.isEmpty()) {
        return false;
    }
    quint64 previousSequence = 0;
    for (const Data::ControllerPackageDeploymentAuditEvent &event :
         current.audit) {
        if (!event.sequence || event.sequence <= previousSequence
            || !event.occurredAt.isValid()
            || event.occurredAt < current.startedAt) {
            return false;
        }
        previousSequence = event.sequence;
    }
    if (previous.operationId == current.operationId
        && (current.transferredBytes < previous.transferredBytes
            || current.audit.size() < previous.audit.size())) {
        return false;
    }
    return current.transferredBytes > previous.transferredBytes
           || current.audit.size() > previous.audit.size()
           || current.state != previous.state;
}

bool providerMutationRequestWasSent(
    const QList<Data::RuntimePackageActivationAuditEvent> &audit)
{
    return std::any_of(
        audit.cbegin(),
        audit.cend(),
        [](const Data::RuntimePackageActivationAuditEvent &event) {
            return event.kind()
                       == Data::RuntimePackageActivationAuditKind::
                           ProviderRequestSent
                   && event.providerAction()
                          != Data::RuntimePackageActivationProviderAction::None;
        });
}

Action outstandingProviderAction(
    const QList<Data::RuntimePackageActivationAuditEvent> &audit)
{
    Action outstanding = Action::None;
    for (const Data::RuntimePackageActivationAuditEvent &event : audit) {
        if (event.kind() == AuditKind::ProviderRequestSent) {
            outstanding = event.providerAction();
        } else if (
            (event.kind() == AuditKind::ProviderTerminalResponse
             || event.kind() == AuditKind::ProviderOutcomeReconciled)
            && event.providerAction() == outstanding) {
            outstanding = Action::None;
        }
    }
    return outstanding;
}

const Data::RuntimePackageActivationAuditEvent *outstandingProviderRequest(
    const QList<Data::RuntimePackageActivationAuditEvent> &audit,
    Action action)
{
    for (auto it = audit.crbegin(); it != audit.crend(); ++it) {
        if (it->kind() == AuditKind::ProviderRequestSent
            && it->providerAction() == action) {
            return &*it;
        }
    }
    return nullptr;
}

bool samePackageRuntime(
    const Data::RuntimePackageActivationControllerEvidence &left,
    const Data::RuntimePackageActivationControllerEvidence &right)
{
    return left.scope() == right.scope() && left.bootId() == right.bootId()
           && left.persistentActive() == right.persistentActive()
           && left.runtimePackageState() == right.runtimePackageState()
           && left.serviceState() == right.serviceState()
           && left.mappingAttestation() == right.mappingAttestation();
}

bool recordedLeaseStillHeld(
    const QList<Data::RuntimePackageActivationAuditEvent> &audit)
{
    bool held = false;
    for (const Data::RuntimePackageActivationAuditEvent &event : audit) {
        const bool terminalSuccess
            = event.kind() == AuditKind::ProviderTerminalResponse
              && event.providerStatus() == std::optional<qint32>(0)
              && event.providerOperationResult() == std::optional<qint32>(0);
        const bool reconciledSuccess
            = event.kind() == AuditKind::ProviderOutcomeReconciled
              && event.providerReconciliation() == Reconciliation::Applied;
        if (event.kind() == AuditKind::ControlLeaseReconciled) {
            held = false;
            continue;
        }
        if (!terminalSuccess && !reconciledSuccess)
            continue;
        if (event.providerAction() == Action::AcquireControl)
            held = true;
        else if (event.providerAction() == Action::ReleaseControl)
            held = false;
    }
    return held;
}

std::optional<Data::ControllerPackageSelector> activeSelector(
    const Data::ControllerConnectionSnapshot &snapshot)
{
    if (!snapshot.package)
        return std::nullopt;
    const Data::ControllerPackageSelector selector{
        snapshot.package->activeSlot,
        snapshot.package->activeGeneration,
        snapshot.package->activeConfigurationId,
    };
    return selector.isValid() ? std::optional(selector) : std::nullopt;
}

std::optional<Data::RuntimePackageActivationControllerEvidence> controllerEvidence(
    Core::ControllerConnectionProvider *provider,
    const Data::ControllerConnectionSnapshot &snapshot,
    QString *error)
{
    if (!provider || !connected(snapshot.state) || snapshot.scope.projectId.isNull()
        || snapshot.scope.masterId.isNull() || !snapshot.sessionGeneration
        || !snapshot.session || !snapshot.session->sessionId
        || !snapshot.session->bootId || !snapshot.controllerState
        || !snapshot.package
        || snapshot.controllerState->controllerBootId
               != snapshot.session->bootId
        || (snapshot.package->controllerBootId
            && snapshot.package->controllerBootId
                   != snapshot.session->bootId)
        || (snapshot.package->controllerState
                    == Data::ControllerPackageState::Active
            && snapshot.package->controllerBootId
                   != snapshot.session->bootId)) {
        if (error)
            *error = QStringLiteral("The controller identity snapshot is incomplete.");
        return std::nullopt;
    }

    std::optional<Data::RuntimeSemanticMappingAttestation> attestation;
    if (snapshot.package->controllerState == Data::ControllerPackageState::Active) {
        attestation = provider->runtimeSemanticMappingAttestation();
        if (!attestation) {
            if (error) {
                *error = QStringLiteral(
                    "The active controller package has no semantic mapping attestation.");
            }
            return std::nullopt;
        }
    }

    QDateTime observedAt = QDateTime::currentDateTimeUtc();
    if (attestation && observedAt < attestation->receivedAt)
        observedAt = attestation->receivedAt;
    Data::RuntimePackageActivationControllerEvidence evidence{
        snapshot.scope,
        snapshot.sessionGeneration,
        snapshot.session->sessionId,
        snapshot.session->controlLeaseOwnerSessionId,
        snapshot.session->bootId,
        activeSelector(snapshot),
        snapshot.package->controllerState,
        snapshot.controllerState->serviceState,
        attestation,
        observedAt,
    };
    if (!evidence.isValid()) {
        if (error)
            *error = QStringLiteral("The controller identity evidence is invalid.");
        return std::nullopt;
    }
    return evidence;
}

bool exactTopologyMatches(
    const Data::ControllerConnectionSnapshot &snapshot,
    const VerifiedSemanticBindingArtifact &artifact,
    QString *error)
{
    if (!snapshot.topology || !snapshot.session
        || !snapshot.topology->hasCompleteProvenance()
        || snapshot.topology->scope != snapshot.scope
        || snapshot.topology->sessionGeneration != snapshot.sessionGeneration
        || snapshot.topology->sessionId != snapshot.session->sessionId
        || snapshot.topology->bootId != snapshot.session->bootId
        || snapshot.topology->result != 0
        || snapshot.topology->respondingCount
               != quint32(snapshot.topology->slaves.size())
        || snapshot.topology->slaves.size() != artifact.topologyInstances.size()) {
        if (error) {
            *error = QStringLiteral(
                "The controller topology is missing, stale, or incomplete. Apply the current "
                "bus scan before activating this package.");
        }
        return false;
    }

    for (const SemanticBindingTopologyInstance &expected :
         artifact.topologyInstances) {
        const auto found = std::find_if(
            snapshot.topology->slaves.cbegin(),
            snapshot.topology->slaves.cend(),
            [&expected](const Data::ControllerTopologySlave &slave) {
                return slave.position == expected.position
                       && slave.stationAddress == expected.stationAddress;
            });
        if (found == snapshot.topology->slaves.cend()
            || found->vendorId != expected.vendorId
            || found->productCode != expected.productCode
            || (expected.revision && found->revision != *expected.revision)
            || (expected.serial && found->serial != *expected.serial)) {
            if (error) {
                *error = QStringLiteral(
                    "The live bus topology differs from the signed package at position %1 "
                    "(station 0x%2).")
                             .arg(expected.position)
                             .arg(expected.stationAddress, 4, 16, QLatin1Char('0'));
            }
            return false;
        }
    }
    return true;
}

QList<Data::DeviceAdapterManifest> availableAdapterManifests(
    const Core::ProviderRegistry *providerRegistry)
{
    QList<Data::DeviceAdapterManifest> manifests;
    if (!providerRegistry)
        return manifests;

    for (Core::Provider *candidate :
         providerRegistry->providers(Core::ProviderKind::DeviceAdapter)) {
        auto *provider = qobject_cast<Core::DeviceAdapterProvider *>(candidate);
        if (!provider || !provider->isAvailable())
            continue;
        for (const Data::DeviceAdapterManifest &manifest :
             provider->adapterManifests()) {
            if (!manifests.contains(manifest))
                manifests.append(manifest);
        }
    }
    return manifests;
}

bool upperAdapterMapsToSignedDevice(
    const Data::DeviceAdapterManifest &manifest,
    const Data::OfflineSlaveConfiguration &slave,
    const VerifiedSemanticDevice &device,
    const SemanticBindingTopologyInstance &topology)
{
    if (manifest.contractVersion != Data::DeviceAdapterContractVersion::V3
        || manifest.qualification != Data::DeviceAdapterQualification::Qualified
        || !manifest.signatureVerified
        || !manifest.realHardwareAllowed
        || manifest.id != slave.adapterSelection.adapterId
        || manifest.version != slave.adapterSelection.adapterVersion
        || manifest.contentSha256 != slave.adapterSelection.adapterContentSha256
        || manifest.match.vendorId != slave.identity.vendorId
        || manifest.match.productCode != slave.identity.productCode
        || slave.identity.revisionNumber < manifest.match.minimumRevision
        || slave.identity.revisionNumber > manifest.match.maximumRevision
        || manifest.match.exactEsiSha256 != slave.esiSha256
        || manifest.provenance.sourceSha256 != slave.esiSha256) {
        return false;
    }

    const Data::DeviceAdapterControllerTarget &target
        = manifest.controllerAdapterTarget;
    if (target.adapterId != device.adapterId
        || target.adapterVersion != device.adapterVersion
        || target.adapterSha256 != device.adapterSha256
        || target.esiSha256 != device.esiSha256
        || target.adapterId != topology.adapterId
        || target.adapterVersion != topology.adapterVersion
        || target.adapterSha256 != topology.adapterSha256
        || target.esiSha256 != topology.esiSha256
        || target.esiSha256 != slave.esiSha256) {
        return false;
    }

    QList<const Data::ProcessDataProfile *> selectedProfiles;
    for (const Data::ProcessDataProfile &profile :
         manifest.processDataProfiles) {
        if (profile.id == slave.adapterSelection.processDataProfileId)
            selectedProfiles.append(&profile);
    }
    const QString signedDcProfile
        = topology.dcProfile.value_or(QString());
    return selectedProfiles.size() == 1
           && selectedProfiles.constFirst()->signedPdoProfileId
                  == topology.pdoProfile
           && selectedProfiles.constFirst()->signedDcProfileId
                  == signedDcProfile;
}

QString contentAddressedBindingArtifactId(
    const VerifiedRuntimePackageEvidence &evidence)
{
    return QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(
            evidence.semanticMappingProof().mappingSha256.toHex()));
}

Utils::Result<Data::SemanticBindingArtifactReference> targetReference(
    const Data::ControllerConnectionScope &scope,
    const QString &bindingArtifactId,
    const Data::RuntimePackageActivationProjectCapture &capture,
    const VerifiedRuntimePackageEvidence &evidence,
    const QList<Data::DeviceAdapterManifest> &adapterManifests)
{
    const Data::ProjectSnapshot &project = capture.snapshot();
    const VerifiedSemanticBindingArtifact &artifact
        = evidence.semanticBindingArtifact();

    const auto master = std::find_if(
        project.nodes.cbegin(),
        project.nodes.cend(),
        [&scope](const Data::ProjectNodeSnapshot &node) {
            return node.id == scope.masterId
                   && node.kind == Data::ProjectNodeKind::Master;
        });
    if (project.id != scope.projectId || master == project.nodes.cend()) {
        return Utils::ResultError(
            QStringLiteral("The captured project does not contain the requested master."));
    }

    QList<const Data::OfflineSlaveConfiguration *> projectSlaves;
    for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
        if (slave.masterId == scope.masterId)
            projectSlaves.append(&slave);
    }
    if (projectSlaves.size() != artifact.devices.size()
        || artifact.topologyInstances.size() != artifact.devices.size()) {
        return Utils::ResultError(
            QStringLiteral(
                "The current project bus is not the complete signed package topology."));
    }

    Data::SemanticBindingArtifactReference reference;
    reference.artifactId = bindingArtifactId;
    reference.artifactSha256 = evidence.semanticMappingProof().mappingSha256;
    reference.projectConfigurationSha256
        = evidence.projectConfigurationSha256();
    for (const VerifiedSemanticDevice &device : artifact.devices) {
        const auto signedTopology = std::find_if(
            artifact.topologyInstances.cbegin(),
            artifact.topologyInstances.cend(),
            [&device](const SemanticBindingTopologyInstance &candidate) {
                return candidate.position == device.position
                       && candidate.stationAddress == device.stationAddress;
            });
        const auto slave = std::find_if(
            projectSlaves.cbegin(),
            projectSlaves.cend(),
            [&device](const Data::OfflineSlaveConfiguration *candidate) {
                return candidate->position == int(device.position)
                       && candidate->stationAddress == device.stationAddress;
            });
        QList<const Data::DeviceAdapterManifest *> mappedAdapters;
        if (signedTopology != artifact.topologyInstances.cend()
            && slave != projectSlaves.cend()) {
            for (const Data::DeviceAdapterManifest &manifest :
                 adapterManifests) {
                if (upperAdapterMapsToSignedDevice(
                        manifest, **slave, device, *signedTopology)) {
                    mappedAdapters.append(&manifest);
                }
            }
        }
        if (signedTopology == artifact.topologyInstances.cend()
            || slave == projectSlaves.cend()
            || (*slave)->identity.vendorId != signedTopology->vendorId
            || (*slave)->identity.productCode != signedTopology->productCode
            || (signedTopology->revision
                && (*slave)->identity.revisionNumber != *signedTopology->revision)
            || (signedTopology->serial
                && (*slave)->serialNumber != *signedTopology->serial)
            || (*slave)->esiSha256 != device.esiSha256
            || mappedAdapters.size() != 1) {
            return Utils::ResultError(
                QStringLiteral(
                    "The current project slave at signed position %1/station 0x%2 has no unique "
                    "verified upper adapter mapping to the package controller target. Apply the "
                    "current bus and select the exact adapter/PDO/DC profile.")
                    .arg(device.position)
                    .arg(device.stationAddress, 4, 16, QLatin1Char('0')));
        }
        reference.projectDeviceBindings.append(
            {(*slave)->id, device.projectDeviceId});
    }
    return reference;
}

bool identityMatchesEvidence(
    const Data::RuntimePackageActivationRequest &request,
    const VerifiedRuntimePackageEvidence &evidence,
    QString *error)
{
    const auto &identity = request.identity();
    const auto &artifact = evidence.semanticBindingArtifact();
    const auto &proof = evidence.semanticMappingProof();
    const bool matches
        = evidence.isValid() && proof == identity.expectedMappingProof()
          && proof.trust == Data::RuntimeSemanticMappingTrust::Production
          && artifact.configurationId == identity.configurationId()
          && artifact.catalogRevision == identity.expectedCatalogRevision()
          && u64BigEndian(artifact.topologyIdentity)
                 == identity.expectedTopologyIdentity()
          && artifact.packageSha256 == identity.packageSha256().value()
          && evidence.projectConfigurationSha256()
                 == identity.compiledProjectSha256().value();
    if (!matches && error) {
        *error = QStringLiteral(
            "The verified production package identity differs from the activation request.");
    }
    return matches;
}

QString journalFileName(
    const QString &journalRoot,
    const Data::RuntimePackageActivationOperationId &operationId)
{
    const QByteArray digest = QCryptographicHash::hash(
        operationId.value().toUtf8(), QCryptographicHash::Sha256);
    return QDir(journalRoot)
        .filePath(QString::fromLatin1(digest.toHex()) + QString::fromLatin1(journalSuffix));
}

} // namespace

struct TrustedRuntimePackageActivationService::Operation
{
    Data::RuntimePackageActivationRequest request;
    Data::RuntimePackageActivationRecord record;
    QList<Data::RuntimePackageActivationAuditEvent> audit;
    Phase phase = Phase::Queued;
    Outcome outcome = Outcome::Pending;
    QString detail;
    QDateTime startedAt;
    QDateTime updatedAt;
    QDateTime completedAt;
    std::optional<Data::RuntimePackageActivationControllerEvidence> beforeController;
    std::optional<Data::RuntimePackageActivationControllerEvidence> afterController;
    std::optional<Data::RuntimePackageActivationProjectCommit> projectCommit;
    std::optional<Data::RuntimePackageActivationCancellation> cancellation;
    std::optional<Data::RuntimePackageActivationDeploymentEvidence> deploymentEvidence;
    QList<Data::RuntimePackageActivationControllerEvidence> controllerEvidenceHistory;
    std::optional<VerifiedRuntimePackageEvidence> evidence;
    std::optional<Data::RuntimePackageActivationProjectCapture> capture;
    std::optional<Data::SemanticBindingArtifactReference> targetReference;
    std::optional<Data::RuntimePackageActivationDocumentRevisionToken>
        persistedDocumentRevision;
    QPointer<Core::ControllerConnectionProvider> provider;
    QList<QMetaObject::Connection> providerConnections;
    Outcome releaseOutcome = Outcome::Pending;
    QString releaseUnknownCode;
    QString releaseUnknownDetail;
    bool existingPackage = false;
    bool attestationRequested = false;
    bool guardWritten = false;
    bool verificationTimeoutArmed = false;
    bool recovered = false;
    bool reconciliationInProgress = false;
    bool recoveryReleaseInProgress = false;
    quint64 acquireRequestId = 1;
    quint64 releaseRequestId = 2;
    Data::ControllerControlProgress acquireProgressBaseline;
    Data::ControllerControlProgress releaseProgressBaseline;
    QDateTime acquireRequestSentAt;
    Data::ControllerPackageDeploymentProgress deploymentProgressBaseline;
    QDateTime deploymentRequestSentAt;
    QDateTime releaseRequestSentAt;
    quint64 providerDeadlineGeneration = 0;
};

namespace {

Data::RuntimePackageActivationAuditEvent localEvent(
    const TrustedRuntimePackageActivationService::Operation &operation,
    AuditKind kind,
    Phase phase,
    Outcome outcome,
    const QString &code,
    const QString &detail,
    std::optional<Data::RuntimePackageActivationSha256> evidence = {},
    const QDateTime &occurredAt = {})
{
    return {
        quint64(operation.audit.size() + 1),
        kind,
        phase,
        outcome,
        Action::None,
        Reconciliation::None,
        {},
        0,
        0,
        0,
        {},
        {},
        {},
        std::move(evidence),
        concise(code),
        concise(detail),
        occurredAt.isValid() ? occurredAt : nextEventTime(operation.audit),
    };
}

Data::RuntimePackageActivationAuditEvent providerEvent(
    const TrustedRuntimePackageActivationService::Operation &operation,
    AuditKind kind,
    Action action,
    const QString &providerOperationId,
    std::optional<quint64> requestId,
    std::optional<qint32> status,
    std::optional<qint32> operationResult,
    std::optional<Data::RuntimePackageActivationSha256> evidence,
    const QString &code,
    const QString &detail)
{
    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    return {
        quint64(operation.audit.size() + 1),
        kind,
        operation.phase,
        operation.outcome,
        action,
        Reconciliation::None,
        providerOperationId,
        snapshot.sessionGeneration,
        snapshot.session ? snapshot.session->sessionId : 0,
        snapshot.session ? snapshot.session->bootId : 0,
        requestId,
        status,
        operationResult,
        std::move(evidence),
        concise(code),
        concise(detail),
        nextEventTime(operation.audit),
    };
}

Data::RuntimePackageActivationAuditEvent reconciliationEvent(
    const TrustedRuntimePackageActivationService::Operation &operation,
    const Data::RuntimePackageActivationAuditEvent &requestEvent,
    Reconciliation reconciliation,
    const Data::RuntimePackageActivationControllerEvidence &evidence,
    const QString &code,
    const QString &detail)
{
    return {
        quint64(operation.audit.size() + 1),
        AuditKind::ProviderOutcomeReconciled,
        Phase::Reconciling,
        Outcome::OutcomeUnknown,
        requestEvent.providerAction(),
        reconciliation,
        requestEvent.providerOperationId(),
        requestEvent.providerSessionGeneration(),
        requestEvent.providerSessionId(),
        requestEvent.providerBootId(),
        requestEvent.providerRequestId(),
        {},
        {},
        evidence.evidenceSha256(),
        concise(code),
        concise(detail),
        nextEventTime(operation.audit),
    };
}

} // namespace

TrustedRuntimePackageActivationService::TrustedRuntimePackageActivationService(
    Core::ProjectService *projectService,
    Core::ProviderRegistry *providerRegistry,
    std::shared_ptr<const RuntimePackageEvidenceRepository> evidenceRepository,
    QString journalRoot,
    QObject *parent,
    int providerDeadlineMs,
    int deploymentProgressDeadlineMs,
    std::function<bool()> journalCommitShouldFail,
    ExactCompileTimeProjectProofVerifier projectProofVerifier)
    : Core::RuntimePackageActivationService(parent)
    , m_projectService(projectService)
    , m_providerRegistry(providerRegistry)
    , m_evidenceRepository(std::move(evidenceRepository))
    , m_journalRoot(QDir::cleanPath(std::move(journalRoot)))
    , m_providerDeadlineMs(std::max(providerDeadlineMs, 1))
    , m_deploymentProgressDeadlineMs(
          std::max(deploymentProgressDeadlineMs, 1))
    , m_journalCommitShouldFail(std::move(journalCommitShouldFail))
    , m_projectProofVerifier(std::move(projectProofVerifier))
{
    discoverRecoveryBarriers();
}

TrustedRuntimePackageActivationService::~TrustedRuntimePackageActivationService() = default;

Core::RuntimePackageActivationPreparationResult
TrustedRuntimePackageActivationService::prepare(
    const Core::RuntimePackageActivationPreparationRequest &request)
{
    const auto failed = [](QString detail) {
        return Core::RuntimePackageActivationPreparationResult{
            {},
            concise(std::move(detail)),
        };
    };
    if (!request.isValid()) {
        return failed(
            QStringLiteral(
                "The verified compiler result or activation preparation input is invalid."));
    }
    if (!m_projectService || !m_projectService->isAvailable()
        || !m_providerRegistry || !m_evidenceRepository) {
        return failed(
            QStringLiteral(
                "Trusted runtime package activation infrastructure is unavailable."));
    }
    if (!m_projectProofVerifier) {
        return failed(
            QStringLiteral(
                "Compile-time project proof verification is unavailable; trusted activation "
                "is disabled."));
    }

    const QByteArray packageSha256 = QCryptographicHash::hash(
        request.packageBytes, QCryptographicHash::Sha256);
    const QByteArray companionSha256 = QCryptographicHash::hash(
        request.effectiveProjectCompanion, QCryptographicHash::Sha256);
    const QByteArray compiledProjectSha256 = QCryptographicHash::hash(
        request.compiledProjectSource, QCryptographicHash::Sha256);
    if (request.compilerVerification.packageSha256.value()
            != packageSha256
        || request.compilerVerification.effectiveProjectCompanionSha256
                   .value()
               != companionSha256) {
        return failed(
            QStringLiteral(
                "The finalized package or effective-project companion differs from the exact "
                "compiler verification result."));
    }

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> capture
        = m_projectService->captureRuntimePackageActivationProject(
            request.scope.projectId);
    if (!capture || !capture->isValid()) {
        return failed(
            capture
                ? QStringLiteral(
                      "The current project could not be captured for activation.")
                : capture.error());
    }

    const Utils::Result<VerifiedRuntimePackageEvidence> imported
        = m_evidenceRepository->import(
            request.packageBytes, request.compiledProjectSource);
    if (!imported) {
        return failed(imported.error());
    }
    const VerifiedSemanticBindingArtifact &artifact
        = imported->semanticBindingArtifact();
    if (request.compilerVerification.manifestFormatVersion != 2
        || request.compilerVerification.envelope.configurationId
               != artifact.configurationId
        || artifact.packageSha256 != packageSha256
        || imported->projectConfigurationSha256()
               != compiledProjectSha256
        || imported->semanticMappingProof().trust
               != Data::RuntimeSemanticMappingTrust::Production) {
        return failed(
            QStringLiteral(
                "The compiler verification result differs from the independently verified "
                "production package."));
    }
    const Utils::Result<> companionResult
        = verifyEffectiveProjectCompanion(
            request, *capture, compiledProjectSha256, *imported);
    if (!companionResult)
        return failed(companionResult.error());
    const Utils::Result<> projectProof
        = m_projectProofVerifier(request, *capture, *imported);
    if (!projectProof) {
        return failed(
            QStringLiteral(
                "Compile-time project proof verification failed: %1")
                .arg(projectProof.error()));
    }

    const QString bindingArtifactId
        = contentAddressedBindingArtifactId(*imported);
    const Utils::Result<Data::SemanticBindingArtifactReference> reference
        = targetReference(
            request.scope,
            bindingArtifactId,
            *capture,
            *imported,
            availableAdapterManifests(m_providerRegistry));
    if (!reference)
        return failed(reference.error());

    const Data::RuntimePackageActivationIdentity identity{
        request.operationId,
        request.scope,
        capture->documentRevision(),
        capture->originalBinding(),
        Data::runtimePackageActivationBindingToken(*reference),
        bindingArtifactId,
        artifact.configurationId,
        artifact.catalogRevision,
        u64BigEndian(artifact.topologyIdentity),
        Data::RuntimePackageActivationSha256{packageSha256},
        Data::RuntimePackageActivationSha256{
            imported->projectConfigurationSha256()},
        Data::RuntimePackageActivationSha256{companionSha256},
        imported->semanticMappingProof(),
    };
    Data::RuntimePackageActivationRequest prepared{
        identity,
        request.packageBytes,
        request.compiledProjectSource,
        request.effectiveProjectCompanion,
        request.rollbackOnActivationFailure,
    };
    QString evidenceError;
    if (!prepared.isValid()
        || !identityMatchesEvidence(prepared, *imported, &evidenceError)) {
        return failed(
            evidenceError.isEmpty()
                ? QStringLiteral(
                      "The derived activation request is invalid.")
                : evidenceError);
    }
    constexpr qsizetype maximumPreparedRequests = 64;
    const QString operationId = request.operationId.value();
    const auto existingOperation = m_operations.constFind(operationId);
    if (existingOperation != m_operations.cend()) {
        if ((*existingOperation)->request.fingerprint()
            != prepared.fingerprint()) {
            return failed(
                QStringLiteral(
                    "OperationId was already used for a different activation request."));
        }
        return {std::move(prepared), {}};
    }
    const auto existingPreparation
        = m_preparedFingerprints.constFind(operationId);
    if (existingPreparation != m_preparedFingerprints.cend()
        && *existingPreparation != prepared.fingerprint()) {
        return failed(
            QStringLiteral(
                "OperationId was already prepared for a different activation request."));
    }
    if (existingPreparation == m_preparedFingerprints.cend()
        && m_preparedFingerprints.size() >= maximumPreparedRequests) {
        return failed(
            QStringLiteral(
                "The prepared activation request capacity is exhausted."));
    }
    m_preparedFingerprints.insert(operationId, prepared.fingerprint());
    return {std::move(prepared), {}};
}

void TrustedRuntimePackageActivationService::bindProvider(
    Operation &operation,
    Core::ControllerConnectionProvider *provider)
{
    if (operation.provider == provider
        && !operation.providerConnections.isEmpty()) {
        return;
    }
    for (const QMetaObject::Connection &connection :
         std::as_const(operation.providerConnections)) {
        disconnect(connection);
    }
    operation.providerConnections.clear();
    operation.provider = provider;
    if (!provider || !m_providerRegistry)
        return;

    const QString operationId
        = operation.request.identity().operationId().value();
    operation.providerConnections.append(connect(
        provider,
        &QObject::destroyed,
        this,
        [this, operationId] { handleProviderUnavailable(operationId); }));
    operation.providerConnections.append(connect(
        provider,
        &Core::Provider::availabilityChanged,
        this,
        [this, operationId](bool available) {
            if (!available)
                handleProviderUnavailable(operationId);
        }));
    operation.providerConnections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        this,
        [this, operationId, provider](Core::Provider *candidate) {
            if (candidate == provider)
                handleProviderUnavailable(operationId);
        }));
    operation.providerConnections.append(connect(
        provider,
        &Core::ControllerConnectionProvider::connectionSnapshotChanged,
        this,
        [this, operationId] { handleProviderSnapshot(operationId); }));
    operation.providerConnections.append(connect(
        provider,
        &Core::ControllerConnectionProvider::runtimeResourceCatalogChanged,
        this,
        [this, operationId] { handleProviderSnapshot(operationId); }));
    operation.providerConnections.append(connect(
        provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationChanged,
        this,
        [this, operationId] { handleProviderSnapshot(operationId); }));
    operation.providerConnections.append(connect(
        provider,
        &Core::ControllerConnectionProvider::
            runtimeSemanticMappingAttestationRequestFinished,
        this,
        [this, operationId](
            const Data::RuntimeSemanticMappingAttestationResult &result) {
            handleAttestationResult(operationId, result);
        }));
}

void TrustedRuntimePackageActivationService::armProviderDeadline(
    Operation &operation,
    Phase phase,
    Action action,
    const QString &code,
    const QString &detail)
{
    const QString operationId
        = operation.request.identity().operationId().value();
    const quint64 deadlineGeneration
        = ++operation.providerDeadlineGeneration;
    const int deadlineMs
        = action == Action::DeployPackage
              ? m_deploymentProgressDeadlineMs
              : m_providerDeadlineMs;
    QTimer::singleShot(
        deadlineMs,
        this,
        [this,
         operationId,
         phase,
         action,
         code,
         detail,
         deadlineGeneration] {
        const auto found = m_operations.find(operationId);
        if (found == m_operations.end())
            return;
        Operation &pending = **found;
        if (pending.phase != phase
            || pending.providerDeadlineGeneration != deadlineGeneration
            || outstandingProviderAction(pending.audit) != action) {
            return;
        }
        if (phase == Phase::Reconciling) {
            if (pending.outcome != Outcome::OutcomeUnknown
                || !pending.recoveryReleaseInProgress) {
                return;
            }
            pending.recoveryReleaseInProgress = false;
            pending.reconciliationInProgress = false;
        } else if (pending.outcome != Outcome::Pending) {
            return;
        }
        freezeUnknown(pending, code, detail);
    });
}

Core::RuntimePackageActivationCommandResult
TrustedRuntimePackageActivationService::start(
    const Data::RuntimePackageActivationRequest &request)
{
    using Disposition = Core::RuntimePackageActivationCommandDisposition;
    if (!request.isValid()) {
        return {
            Disposition::InvalidRequest,
            {},
            QStringLiteral("The runtime package activation request is invalid."),
        };
    }
    const QString operationId = request.identity().operationId().value();
    const auto existing = m_operations.constFind(operationId);
    if (existing != m_operations.cend()) {
        if ((*existing)->request.fingerprint() == request.fingerprint()) {
            return {Disposition::IdempotentReplay, (*existing)->record, {}};
        }
        return {
            Disposition::Conflict,
            (*existing)->record,
            QStringLiteral("OperationId was already used for a different activation request."),
        };
    }
    const auto prepared = m_preparedFingerprints.constFind(operationId);
    if (prepared == m_preparedFingerprints.cend()
        || *prepared != request.fingerprint()) {
        return {
            Disposition::InvalidRequest,
            {},
            QStringLiteral(
                "The activation request was not produced by this service's exact prepare "
                "operation."),
        };
    }
    if (hasUnresolvedRecoveryBarrier()) {
        return {
            Disposition::InvalidRequest,
            {},
            recoveryBarrierDetail(),
        };
    }
    if (!m_projectService || !m_projectService->isAvailable()
        || !m_providerRegistry || !m_evidenceRepository) {
        return {
            Disposition::InvalidRequest,
            {},
            QStringLiteral("Trusted runtime package activation infrastructure is unavailable."),
        };
    }
    for (const auto &candidate : std::as_const(m_operations)) {
        if (!Data::runtimePackageActivationOutcomeIsTerminal(candidate->outcome)) {
            return {
                Disposition::InvalidRequest,
                {},
                QStringLiteral("Another runtime package activation is still in progress."),
            };
        }
    }

    auto operation = std::make_shared<Operation>();
    operation->request = request;
    operation->startedAt = QDateTime::currentDateTimeUtc();
    operation->updatedAt = operation->startedAt;
    operation->detail = QStringLiteral("Activation intent persisted.");
    operation->audit.append(localEvent(
        *operation,
        AuditKind::IntentPersisted,
        Phase::Queued,
        Outcome::Pending,
        QStringLiteral("activation-intent-persisted"),
        operation->detail,
        request.fingerprint(),
        operation->startedAt));
    operation->record = {
        request.identity(),
        request.fingerprint(),
        request.rollbackOnActivationFailure(),
        1,
        Phase::Queued,
        Outcome::Pending,
        operation->audit,
        operation->detail,
        operation->startedAt,
        operation->startedAt,
    };
    QTC_CHECK(operation->record.isValid());
    m_operations.insert(operationId, operation);
    m_preparedFingerprints.remove(operationId);
    publish(*operation);
    QTimer::singleShot(0, this, [this, operationId] { process(operationId); });
    return {Disposition::Accepted, operation->record, {}};
}

Core::RuntimePackageActivationCommandResult
TrustedRuntimePackageActivationService::reconcile(
    const Data::RuntimePackageActivationOperationId &operationId)
{
    using Disposition = Core::RuntimePackageActivationCommandDisposition;
    if (!operationId.isValid()) {
        return {
            Disposition::InvalidRequest,
            {},
            QStringLiteral("The activation OperationId is invalid."),
        };
    }
    const auto found = m_operations.constFind(operationId.value());
    if (found == m_operations.cend()) {
        return {
            Disposition::NotFound,
            {},
            hasUnresolvedRecoveryBarrier()
                ? recoveryBarrierDetail()
                : QStringLiteral("The activation operation was not found."),
        };
    }
    Operation &operation = **found;
    if (operation.reconciliationInProgress
        || (operation.phase == Phase::Reconciling
            && outstandingProviderAction(operation.audit) != Action::None)) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            QStringLiteral(
                "Authoritative activation reconciliation is already in progress."),
        };
    }
    if (!operation.record.needsReconciliation()
        && !operation.recovered) {
        return {
            Disposition::NotAllowed,
            operation.record,
            QStringLiteral("The activation operation does not require reconciliation."),
        };
    }
    if (!m_projectService || !m_projectService->isAvailable()
        || !m_providerRegistry || !m_evidenceRepository) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            QStringLiteral(
                "Activation recovery infrastructure is not currently available."),
        };
    }

    Utils::Result<VerifiedRuntimePackageEvidence> imported
        = m_evidenceRepository->import(
            operation.request.packageBytes(),
            operation.request.compiledProjectSource());
    QString evidenceError;
    if (!imported
        || !identityMatchesEvidence(
            operation.request, *imported, &evidenceError)) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            imported ? evidenceError : imported.error(),
        };
    }
    operation.evidence = *imported;
    operation.capture.reset();
    operation.targetReference.reset();

    const Utils::Result<Data::RuntimePackageActivationProjectCapture>
        projectCapture = m_projectService
                             ->captureRuntimePackageActivationProject(
                                 operation.request.identity().scope().projectId);
    if (projectCapture && projectCapture->isValid()) {
        operation.capture = *projectCapture;
        const Utils::Result<Data::SemanticBindingArtifactReference> reference
            = targetReference(
                operation.request.identity().scope(),
                operation.request.identity().bindingArtifactId(),
                *projectCapture,
                *operation.evidence,
                availableAdapterManifests(m_providerRegistry));
        if (reference
            && Data::runtimePackageActivationBindingToken(*reference)
                   == operation.request.identity().targetBindingToken()) {
            operation.targetReference = *reference;
        }
    }

    QList<Core::ControllerConnectionProvider *> providers;
    for (Core::Provider *candidate :
         m_providerRegistry->providers(
             Core::ProviderKind::ControllerConnection)) {
        auto *provider
            = qobject_cast<Core::ControllerConnectionProvider *>(candidate);
        if (provider && provider->isAvailable()
            && provider->connectionSnapshot().scope
                   == operation.request.identity().scope()) {
            providers.append(provider);
        }
    }
    if (providers.size() != 1) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            QStringLiteral(
                "Exactly one available controller provider is required for recovery."),
        };
    }
    bindProvider(operation, providers.constFirst());

    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    if (!connected(snapshot.state) || !snapshot.session
        || snapshot.scope != operation.request.identity().scope()) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            QStringLiteral(
                "The controller session is not ready for authoritative recovery."),
        };
    }
    const auto current
        = controllerEvidence(operation.provider, snapshot, &evidenceError);
    if (!current || !operation.beforeController
        || current->bootId() != operation.beforeController->bootId()) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            evidenceError.isEmpty()
                ? QStringLiteral(
                      "The controller BootId differs from the durable activation.")
                : evidenceError,
        };
    }

    const Action outstanding = outstandingProviderAction(operation.audit);
    const Data::RuntimePackageActivationAuditEvent *requestEvent
        = outstanding == Action::None
              ? nullptr
              : outstandingProviderRequest(operation.audit, outstanding);
    const std::optional<Data::RuntimePackageActivationAuditEvent>
        durableRequest = requestEvent
                             ? std::optional(*requestEvent)
                             : std::optional<
                                   Data::RuntimePackageActivationAuditEvent>();

    operation.reconciliationInProgress = true;
    operation.phase = Phase::Reconciling;
    operation.outcome = Outcome::OutcomeUnknown;
    operation.detail
        = QStringLiteral("Reconciling durable activation evidence with the controller.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::ReconciliationStarted,
        operation.phase,
        operation.outcome,
        QStringLiteral("activation-reconciliation-started"),
        operation.detail));
    if (operation.afterController
        && operation.afterController->evidenceSha256()
               != current->evidenceSha256()) {
        operation.controllerEvidenceHistory.append(
            *operation.afterController);
    }
    operation.afterController = *current;
    operation.audit.append(localEvent(
        operation,
        AuditKind::ControllerEvidenceCaptured,
        operation.phase,
        operation.outcome,
        QStringLiteral("activation-reconciliation-state"),
        QStringLiteral(
            "Captured authoritative controller state for activation recovery."),
        current->evidenceSha256()));

    bool reconciledAction = outstanding == Action::None;
    if (durableRequest
        && durableRequest->providerBootId() == current->bootId()) {
        std::optional<Reconciliation> disposition;
        switch (outstanding) {
        case Action::AcquireControl:
            if (current->controlLeaseOwnerSessionId()
                == durableRequest->providerSessionId()) {
                disposition = Reconciliation::Applied;
            } else if (samePackageRuntime(
                           *operation.beforeController, *current)) {
                disposition = Reconciliation::NotApplied;
            }
            break;
        case Action::DeployPackage:
            if (current->matchesActivatedIdentity(
                    operation.request.identity())) {
                disposition = Reconciliation::Applied;
            } else if (samePackageRuntime(
                           *operation.beforeController, *current)) {
                disposition = Reconciliation::NotApplied;
            }
            break;
        case Action::ReleaseControl:
            disposition
                = current->controlLeaseOwnerSessionId()
                          == durableRequest->providerSessionId()
                      ? Reconciliation::NotApplied
                      : Reconciliation::Applied;
            break;
        case Action::None:
            break;
        }
        if (disposition) {
            operation.audit.append(reconciliationEvent(
                operation,
                *durableRequest,
                *disposition,
                *current,
                *disposition == Reconciliation::Applied
                    ? QStringLiteral("provider-action-applied")
                    : QStringLiteral("provider-action-not-applied"),
                QStringLiteral(
                    "Authoritative controller state resolved the pending provider action.")));
            reconciledAction = true;
        }
    }
    if (!reconciledAction) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("activation-reconciliation-incomplete"),
            QStringLiteral(
                "The current controller state cannot yet prove whether the pending action "
                "was applied."));
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            operation.detail,
        };
    }

    const bool projectExact
        = operation.projectCommit && projectCapture && projectCapture->isValid()
          && operation.targetReference
          && operation.persistedDocumentRevision
          && projectCapture->documentRevision()
                 == *operation.persistedDocumentRevision
          && !projectCapture->snapshot().modified
          && projectCapture->originalBinding()
                 == operation.projectCommit->resultingBinding()
          && projectCapture->snapshot().masterBindingArtifact
                 == *operation.targetReference;
    const bool controllerMatchesTarget
        = current->matchesActivatedIdentity(
            operation.request.identity());
    const bool targetAttestationExact
        = controllerMatchesTarget && current->mappingAttestation()
          && operation.targetReference && operation.evidence
          && verifyRuntimeSemanticMappingAttestation(
              *current->mappingAttestation(),
              operation.request.identity().scope(),
              snapshot.sessionGeneration,
              current->mappingAttestation()->epoch,
              *operation.targetReference,
              *operation.evidence);

    const auto evidenceForEvent =
        [&operation](const Data::RuntimePackageActivationAuditEvent &event)
        -> const Data::RuntimePackageActivationControllerEvidence * {
        const auto matches =
            [&event](const Data::RuntimePackageActivationControllerEvidence &evidence) {
                return event.evidenceSha256()
                       && evidence.evidenceSha256()
                              == *event.evidenceSha256();
            };
        if (operation.beforeController
            && matches(*operation.beforeController))
            return &*operation.beforeController;
        if (operation.afterController
            && matches(*operation.afterController))
            return &*operation.afterController;
        for (const auto &evidence :
             std::as_const(operation.controllerEvidenceHistory)) {
            if (matches(evidence))
                return &evidence;
        }
        return nullptr;
    };
    const Data::RuntimePackageActivationAuditEvent *acquireEvent = nullptr;
    bool acquiredLeaseWasRecorded = false;
    for (const auto &event : std::as_const(operation.audit)) {
        if (event.providerAction() == Action::AcquireControl
            && (event.kind() == AuditKind::ProviderTerminalResponse
                || event.kind()
                       == AuditKind::ProviderOutcomeReconciled)) {
            const auto *evidence = evidenceForEvent(event);
            const bool applied
                = (event.kind() == AuditKind::ProviderTerminalResponse
                   && event.providerStatus() == std::optional<qint32>(0)
                   && event.providerOperationResult()
                          == std::optional<qint32>(0))
                  || (event.kind() == AuditKind::ProviderOutcomeReconciled
                      && event.providerReconciliation()
                             == Reconciliation::Applied)
                  || (evidence
                      && evidence->controlLeaseOwnerSessionId()
                             == event.providerSessionId());
            if (applied) {
                acquireEvent = &event;
                acquiredLeaseWasRecorded = true;
            }
        } else if (
            event.providerAction() == Action::ReleaseControl
            && ((event.kind() == AuditKind::ProviderTerminalResponse
                 && event.providerStatus() == std::optional<qint32>(0)
                 && event.providerOperationResult()
                        == std::optional<qint32>(0))
                || (event.kind() == AuditKind::ProviderOutcomeReconciled
                    && event.providerReconciliation()
                           == Reconciliation::Applied)
                || event.kind()
                       == AuditKind::ControlLeaseReconciled)) {
            acquiredLeaseWasRecorded = false;
        }
    }

    if (current->controlLeaseOwnerSessionId() != 0) {
        if (!acquiredLeaseWasRecorded || !acquireEvent
            || current->controlLeaseOwnerSessionId()
                   != acquireEvent->providerSessionId()
            || !current->ownsControlLease() || snapshot.readOnly
            || snapshot.mock) {
            operation.reconciliationInProgress = false;
            freezeUnknown(
                operation,
                QStringLiteral("recovery-lease-owner-mismatch"),
                QStringLiteral(
                    "A controller lease is owned by a session that this activation cannot "
                    "safely release."));
            return {
                Disposition::ReconciliationRequired,
                operation.record,
                operation.detail,
            };
        }
        operation.afterController = *current;
        Outcome releaseOutcome = Outcome::OutcomeUnknown;
        if (projectExact && targetAttestationExact) {
            releaseOutcome = operation.existingPackage
                                 ? Outcome::SucceededWithExistingPackage
                                 : Outcome::SucceededWithActivatedPackage;
        } else if (!operation.projectCommit) {
            releaseOutcome = samePackageRuntime(
                                 *operation.beforeController, *current)
                                 ? Outcome::FailedWithoutControllerChange
                                 : Outcome::FailedControllerChangedWithoutBinding;
        }
        if (releaseOutcome == Outcome::SucceededWithExistingPackage) {
            operation.reconciliationInProgress = false;
            freezeUnknown(
                operation,
                QStringLiteral("recovery-existing-package-lease"),
                QStringLiteral(
                    "An existing-package activation never releases a borrowed lease."));
            return {
                Disposition::ReconciliationRequired,
                operation.record,
                operation.detail,
            };
        }
        operation.releaseUnknownCode
            = QStringLiteral("activation-reconciliation-incomplete");
        operation.releaseUnknownDetail
            = QStringLiteral(
                "The lease will be released, but the final project/controller identity "
                "remains unresolved.");
        beginRecoveryRelease(operation, releaseOutcome);
        return {Disposition::Accepted, operation.record, {}};
    }

    if (acquiredLeaseWasRecorded && acquireEvent) {
        operation.audit.append(
            Data::RuntimePackageActivationAuditEvent{
                quint64(operation.audit.size() + 1),
                AuditKind::ControlLeaseReconciled,
                Phase::Reconciling,
                Outcome::OutcomeUnknown,
                Action::ReleaseControl,
                Reconciliation::Applied,
                operation.request.identity().operationId().value(),
                acquireEvent->providerSessionGeneration(),
                acquireEvent->providerSessionId(),
                acquireEvent->providerBootId(),
                {},
                {},
                {},
                current->evidenceSha256(),
                QStringLiteral("control-lease-no-longer-owned"),
                QStringLiteral(
                    "Authoritative state proves the activation lease is no longer owned."),
                nextEventTime(operation.audit),
            });
    }

    if (projectExact && targetAttestationExact) {
        if (!operation.existingPackage
            && (!operation.afterController
                || !operation.afterController->ownsControlLease())) {
            operation.reconciliationInProgress = false;
            freezeUnknown(
                operation,
                QStringLiteral("activation-reconciliation-incomplete"),
                QStringLiteral(
                    "The durable record does not contain the held-lease evidence required "
                    "to prove activated-package success."));
            return {
                Disposition::ReconciliationRequired,
                operation.record,
                operation.detail,
            };
        }
        if (operation.existingPackage) {
            if (operation.afterController
                && operation.afterController->evidenceSha256()
                       != current->evidenceSha256()) {
                operation.controllerEvidenceHistory.append(
                    *operation.afterController);
            }
            operation.afterController = *current;
        }
        finish(
            operation,
            operation.existingPackage
                ? Outcome::SucceededWithExistingPackage
                : Outcome::SucceededWithActivatedPackage,
            QStringLiteral("activation-reconciled"),
            QStringLiteral(
                "Authoritative controller and project evidence completed activation "
                "recovery."),
            operation.projectCommit->evidenceSha256());
        return {Disposition::Accepted, operation.record, {}};
    }
    if (!operation.projectCommit) {
        if (!providerMutationRequestWasSent(operation.audit)) {
            operation.afterController.reset();
            finish(
                operation,
                Outcome::FailedWithoutControllerChange,
                QStringLiteral("activation-reconciled-before-mutation"),
                QStringLiteral(
                    "Recovery proved that no activation mutation was sent."),
                operation.request.fingerprint());
        } else {
            operation.afterController = *current;
            const bool unchanged
                = samePackageRuntime(
                    *operation.beforeController, *current);
            finish(
                operation,
                unchanged ? Outcome::FailedWithoutControllerChange
                          : Outcome::FailedControllerChangedWithoutBinding,
                QStringLiteral("activation-reconciled"),
                unchanged
                    ? QStringLiteral(
                          "Recovery proved that the controller package did not change.")
                    : QStringLiteral(
                          "Recovery proved that the controller changed without a project "
                          "binding."),
                current->evidenceSha256());
        }
        return {Disposition::Accepted, operation.record, {}};
    }

    operation.afterController = *current;
    operation.reconciliationInProgress = false;
    freezeUnknown(
        operation,
        targetAttestationExact
            ? QStringLiteral("project-binding-drifted")
            : QStringLiteral("controller-target-drifted"),
        QStringLiteral(
            "The controller and project do not yet agree on the durable activated binding."));
    return {
        Disposition::ReconciliationRequired,
        operation.record,
        operation.detail,
    };
}

Core::RuntimePackageActivationCommandResult
TrustedRuntimePackageActivationService::cancel(
    const Data::RuntimePackageActivationCancelRequest &request)
{
    using Disposition = Core::RuntimePackageActivationCommandDisposition;
    if (!request.isValid()) {
        return {
            Disposition::InvalidRequest,
            {},
            QStringLiteral("The activation cancellation request is invalid."),
        };
    }
    const auto found = m_operations.find(request.operationId().value());
    if (found == m_operations.end()) {
        return {
            Disposition::NotFound,
            {},
            QStringLiteral("The activation operation was not found."),
        };
    }
    Operation &operation = **found;
    if (operation.outcome == Outcome::Canceled && operation.cancellation
        && operation.cancellation->requestFingerprint()
               == request.fingerprint()) {
        return {Disposition::IdempotentReplay, operation.record, {}};
    }
    if (operation.record.needsReconciliation()) {
        return {
            Disposition::ReconciliationRequired,
            operation.record,
            QStringLiteral("An outcome-unknown activation must be reconciled, not canceled."),
        };
    }
    if (!Data::runtimePackageActivationPhaseAllowsCancel(operation.phase)) {
        return {
            Disposition::TooLate,
            operation.record,
            QStringLiteral("The activation can no longer be canceled safely."),
        };
    }
    if (request.expectedRecordRevision() != operation.record.revision()
        || request.expectedDocumentRevision()
               != operation.request.identity().documentRevisionToken()
        || request.expectedBinding()
               != operation.request.identity().originalBindingToken()) {
        return {
            Disposition::StaleRevision,
            operation.record,
            QStringLiteral("The activation changed before cancellation was accepted."),
        };
    }

    const QDateTime requestedAt = nextEventTime(operation.audit);
    operation.cancellation = Data::RuntimePackageActivationCancellation{
        request.expectedRecordRevision(),
        request.expectedDocumentRevision(),
        request.expectedBinding(),
        request.fingerprint(),
        operation.phase,
        Data::RuntimePackageActivationCancellationControllerResult::NoControllerMutation,
        {},
        requestedAt,
    };
    operation.phase = Phase::Canceling;
    operation.detail = QStringLiteral("Activation canceled before controller mutation.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::CancellationRequested,
        operation.phase,
        operation.outcome,
        QStringLiteral("activation-canceled"),
        operation.detail,
        request.fingerprint(),
        requestedAt));
    finish(
        operation,
        Outcome::Canceled,
        QStringLiteral("activation-canceled"),
        operation.detail,
        request.fingerprint());
    return {Disposition::Accepted, operation.record, {}};
}

std::optional<Data::RuntimePackageActivationRecord>
TrustedRuntimePackageActivationService::record(
    const Data::RuntimePackageActivationOperationId &operationId) const
{
    const auto found = m_operations.constFind(operationId.value());
    return found == m_operations.cend()
               ? std::optional<Data::RuntimePackageActivationRecord>()
               : std::optional((*found)->record);
}

Data::RuntimePackageActivationSnapshot
TrustedRuntimePackageActivationService::snapshot() const
{
    QList<Data::RuntimePackageActivationRecord> records;
    records.reserve(m_operations.size());
    for (const auto &operation : m_operations)
        records.append(operation->record);
    std::sort(
        records.begin(),
        records.end(),
        [](const auto &left, const auto &right) {
            return left.identity().operationId().value()
                   < right.identity().operationId().value();
        });
    const quint64 sequence = m_snapshotSequence ? m_snapshotSequence : 1;
    return {sequence, records, QDateTime::currentDateTimeUtc()};
}

bool TrustedRuntimePackageActivationService::hasUnresolvedRecoveryBarrier() const
{
    return !m_recoveryBarriers.isEmpty();
}

QString TrustedRuntimePackageActivationService::recoveryBarrierDetail() const
{
    return m_recoveryBarriers.isEmpty()
               ? QString()
               : QStringLiteral(
                     "A previous runtime package activation has an unresolved durable outcome. "
                     "No controller mutation is permitted until its package, attestation, and "
                     "lease state are reconciled.");
}

void TrustedRuntimePackageActivationService::process(const QString &operationId)
{
    const auto found = m_operations.find(operationId);
    if (found == m_operations.end())
        return;
    Operation &operation = **found;
    if (operation.phase != Phase::Queued || operation.outcome != Outcome::Pending)
        return;

    operation.phase = Phase::VerifyingInputs;
    operation.detail = QStringLiteral("Verifying signed runtime package evidence.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("verifying-inputs"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::VerifyingInputs
        || operation.outcome != Outcome::Pending) {
        return;
    }

    Utils::Result<VerifiedRuntimePackageEvidence> imported
        = m_evidenceRepository->import(
            operation.request.packageBytes(),
            operation.request.compiledProjectSource());
    QString error;
    if (!imported
        || !identityMatchesEvidence(operation.request, *imported, &error)) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("package-evidence-invalid"),
            imported ? error : imported.error());
        return;
    }
    operation.evidence = *imported;

    operation.phase = Phase::CapturingProject;
    operation.detail = QStringLiteral("Capturing the exact current project revision.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("capturing-project"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::CapturingProject
        || operation.outcome != Outcome::Pending) {
        return;
    }

    Utils::Result<Data::RuntimePackageActivationProjectCapture> capture
        = m_projectService->captureRuntimePackageActivationProject(
            operation.request.identity().scope().projectId);
    if (!capture || !capture->isValid()
        || capture->documentRevision()
               != operation.request.identity().documentRevisionToken()
        || capture->originalBinding()
               != operation.request.identity().originalBindingToken()) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("project-capture-stale"),
            capture
                ? QStringLiteral(
                      "The current project revision or binding differs from the signed "
                      "activation request.")
                : capture.error());
        return;
    }
    operation.capture = *capture;
    Utils::Result<Data::SemanticBindingArtifactReference> reference
        = targetReference(
            operation.request.identity().scope(),
            operation.request.identity().bindingArtifactId(),
            *capture,
            *operation.evidence,
            availableAdapterManifests(m_providerRegistry));
    if (!reference
        || Data::runtimePackageActivationBindingToken(*reference)
               != operation.request.identity().targetBindingToken()) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("project-topology-mismatch"),
            reference
                ? QStringLiteral(
                      "The compiler target binding token does not match the exact current project "
                      "device mapping.")
                : reference.error());
        return;
    }
    operation.targetReference = *reference;

    operation.phase = Phase::ProbingExistingPackage;
    operation.detail = QStringLiteral("Checking the authoritative controller package state.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("probing-controller"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::ProbingExistingPackage
        || operation.outcome != Outcome::Pending) {
        return;
    }

    QList<Core::ControllerConnectionProvider *> providers;
    for (Core::Provider *candidate :
         m_providerRegistry->providers(Core::ProviderKind::ControllerConnection)) {
        auto *provider = qobject_cast<Core::ControllerConnectionProvider *>(candidate);
        if (provider && provider->isAvailable()
            && provider->connectionSnapshot().scope
                   == operation.request.identity().scope()) {
            providers.append(provider);
        }
    }
    if (providers.size() != 1) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-provider-unavailable"),
            providers.isEmpty()
                ? QStringLiteral(
                      "No available controller provider is bound to this project and master.")
                : QStringLiteral(
                      "More than one controller provider is bound to this project and master."));
        return;
    }
    bindProvider(operation, providers.constFirst());
    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    if (!connected(snapshot.state) || snapshot.mock || snapshot.readOnly
        || !operation.provider->supportsPackageDeployment()
        || !operation.provider->supportsControlCommand(
            Data::ControllerControlCommand::AcquireControl)
        || !operation.provider->supportsControlCommand(
            Data::ControllerControlCommand::ReleaseControl)
        || !operation.provider->supportsRuntimeSemanticMappingAttestation()
        || !exactTopologyMatches(
            snapshot, operation.evidence->semanticBindingArtifact(), &error)) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-preflight-failed"),
            !error.isEmpty()
                ? error
                : QStringLiteral(
                      "The controller session does not support trusted package activation."));
        return;
    }
    std::optional<Data::RuntimePackageActivationControllerEvidence> before
        = controllerEvidence(operation.provider, snapshot, &error);
    if (!before) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-evidence-invalid"),
            error);
        return;
    }
    operation.beforeController = *before;
    operation.audit.append(localEvent(
        operation,
        AuditKind::ControllerEvidenceCaptured,
        operation.phase,
        operation.outcome,
        QStringLiteral("controller-before"),
        QStringLiteral("Captured the pre-activation controller identity."),
        before->evidenceSha256()));
    publish(operation);
    if (operation.phase != Phase::ProbingExistingPackage
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (before->controlLeaseOwnerSessionId() != 0) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-lease-already-owned"),
            QStringLiteral(
                "Runtime package activation requires an unowned controller lease; an existing "
                "lease is never borrowed or released by this operation."));
        return;
    }
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(operationId);
        return;
    }

    if (before->matchesActivatedIdentity(operation.request.identity())) {
        operation.existingPackage = true;
        const Utils::Result<> existingGuard = persistGuard(operation);
        if (!existingGuard) {
            failBeforeControllerMutation(
                operation,
                QStringLiteral("activation-journal-failed"),
                existingGuard.error());
            return;
        }
        beginRuntimeVerification(operation);
        return;
    }
    if (before->serviceState() != Data::ControllerServiceState::Shutdown
        || before->runtimePackageState()
               == Data::ControllerPackageState::Active) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-not-shutdown"),
            QStringLiteral(
                "The controller must be in Shutdown with no active runtime before package "
                "activation."));
        return;
    }

    beginAcquire(operation);
}

void TrustedRuntimePackageActivationService::beginAcquire(Operation &operation)
{
    operation.phase = Phase::AcquiringControl;
    operation.detail = QStringLiteral("Acquiring the exclusive controller lease.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("acquiring-control"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::AcquiringControl
        || operation.outcome != Outcome::Pending) {
        return;
    }

    const Utils::Result<> initialGuard = persistGuard(operation);
    if (!initialGuard) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("activation-journal-failed"),
            initialGuard.error());
        return;
    }
    QString preflightError;
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(
            operation.request.identity().operationId().value());
        return;
    }
    const Data::ControllerConnectionSnapshot acquireSnapshot
        = operation.provider->connectionSnapshot();
    const auto acquireEvidence
        = controllerEvidence(operation.provider, acquireSnapshot, &preflightError);
    if (!acquireEvidence || !operation.beforeController
        || !operation.beforeController->describesSameControllerStateAs(
            *acquireEvidence)
        || acquireEvidence->controlLeaseOwnerSessionId() != 0
        || acquireSnapshot.readOnly || acquireSnapshot.mock
        || acquireEvidence->serviceState()
               != Data::ControllerServiceState::Shutdown
        || acquireEvidence->runtimePackageState()
               == Data::ControllerPackageState::Active
        || !exactTopologyMatches(
            acquireSnapshot,
            operation.evidence->semanticBindingArtifact(),
            &preflightError)) {
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-preflight-drifted"),
            preflightError.isEmpty()
                ? QStringLiteral(
                      "Controller state changed before the lease request could be sent.")
                : preflightError);
        return;
    }
    operation.acquireProgressBaseline
        = acquireSnapshot.controlProgress;
    operation.audit.append(providerEvent(
        operation,
        AuditKind::ProviderRequestSent,
        Action::AcquireControl,
        operation.request.identity().operationId().value(),
        operation.acquireRequestId,
        {},
        {},
        {},
        QStringLiteral("acquire-request-sent"),
        QStringLiteral("Exclusive control lease request sent.")));
    operation.acquireRequestSentAt = operation.audit.constLast().occurredAt();
    const Utils::Result<> requestGuard = persistGuard(operation);
    if (!requestGuard) {
        operation.audit.removeLast();
        operation.acquireRequestSentAt = {};
        failBeforeControllerMutation(
            operation,
            QStringLiteral("activation-journal-failed"),
            requestGuard.error());
        return;
    }

    Data::ControllerControlRequest request;
    request.command = Data::ControllerControlCommand::AcquireControl;
    request.leaseDurationMs = 30000;
    armProviderDeadline(
        operation,
        Phase::AcquiringControl,
        Action::AcquireControl,
        QStringLiteral("acquire-timeout"),
        QStringLiteral(
            "Timed out while waiting for the terminal control-lease response."));
    const Utils::Result<> result
        = operation.provider->executeControlCommand(request);
    if (operation.phase != Phase::AcquiringControl
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!result) {
        QString error;
        const auto after = controllerEvidence(
            operation.provider, operation.provider->connectionSnapshot(), &error);
        if (!after) {
            freezeUnknown(
                operation,
                QStringLiteral("acquire-outcome-unknown"),
                result.error());
            return;
        }
        operation.controllerEvidenceHistory.append(*after);
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("acquire-rejected"),
            result.error(),
            after->evidenceSha256()));
        if (!after->ownsControlLease()) {
            freezeUnknown(
                operation,
                QStringLiteral("acquire-outcome-unknown"),
                QStringLiteral(
                    "The acquire call failed before an authoritative terminal response; "
                    "controller state must be reconciled."));
            return;
        }
        operation.audit.append(providerEvent(
            operation,
            AuditKind::ProviderTerminalResponse,
            Action::AcquireControl,
            operation.request.identity().operationId().value(),
            operation.acquireRequestId,
            0,
            0,
            after->evidenceSha256(),
            QStringLiteral("lease-acquired-after-call-error"),
            QStringLiteral(
                "Authoritative lease state shows that control was acquired despite the "
                "provider call error.")));
        operation.afterController = *after;
        publish(operation);
        if (operation.phase != Phase::AcquiringControl
            || operation.outcome != Outcome::Pending) {
            return;
        }
        beginRelease(operation, Outcome::FailedWithoutControllerChange);
        return;
    }
    publish(operation);
}

void TrustedRuntimePackageActivationService::handleProviderSnapshot(
    const QString &operationId)
{
    const auto found = m_operations.find(operationId);
    if (found == m_operations.end())
        return;
    Operation &operation = **found;
    const bool recoveryRelease
        = operation.phase == Phase::Reconciling
          && operation.outcome == Outcome::OutcomeUnknown
          && operation.reconciliationInProgress
          && operation.recoveryReleaseInProgress;
    if (!operation.provider
        || (operation.outcome != Outcome::Pending && !recoveryRelease)) {
        return;
    }
    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    const Data::RuntimePackageActivationAuditEvent *recoveryRequest
        = recoveryRelease
              ? outstandingProviderRequest(
                    operation.audit, Action::ReleaseControl)
              : nullptr;
    const bool sessionChanged
        = !connected(snapshot.state) || !snapshot.session
          || !operation.beforeController
          || snapshot.scope != operation.request.identity().scope()
          || (recoveryRelease
                  ? (!recoveryRequest
                     || snapshot.sessionGeneration
                            != recoveryRequest->providerSessionGeneration()
                     || snapshot.session->sessionId
                            != recoveryRequest->providerSessionId()
                     || snapshot.session->bootId
                            != recoveryRequest->providerBootId())
                  : (snapshot.sessionGeneration
                         != operation.beforeController->sessionGeneration()
                     || snapshot.session->sessionId
                            != operation.beforeController->observationSessionId()
                     || snapshot.session->bootId
                            != operation.beforeController->bootId()));
    if (sessionChanged) {
        if (recoveryRelease) {
            operation.recoveryReleaseInProgress = false;
            operation.reconciliationInProgress = false;
            freezeUnknown(
                operation,
                QStringLiteral("controller-session-changed"),
                QStringLiteral(
                    "The controller session changed while recovery lease cleanup was "
                    "unresolved."));
        } else if (
            outstandingProviderAction(operation.audit) == Action::None
            && !recordedLeaseStillHeld(operation.audit)
            && !(operation.afterController
                 && operation.afterController->ownsControlLease())
            && !operation.projectCommit) {
            failBeforeControllerMutation(
                operation,
                QStringLiteral("controller-session-changed"),
                QStringLiteral(
                    "The controller session changed before any activation mutation was sent."));
        } else {
            freezeUnknown(
                operation,
                QStringLiteral("controller-session-changed"),
                QStringLiteral(
                    "The controller session changed while activation outcome was unresolved."));
        }
        return;
    }

    if (operation.phase == Phase::AcquiringControl) {
        const auto &progress = snapshot.controlProgress;
        if (progress.command != Data::ControllerControlCommand::AcquireControl
            || !progress.final
            || progress == operation.acquireProgressBaseline
            || !progress.startedAt.isValid()
            || progress.startedAt < operation.acquireRequestSentAt)
            return;
        QString error;
        const auto acquired
            = controllerEvidence(operation.provider, snapshot, &error);
        if (!acquired) {
            freezeUnknown(
                operation,
                QStringLiteral("lease-evidence-invalid"),
                error);
            return;
        }
        operation.controllerEvidenceHistory.append(*acquired);
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("lease-evidence"),
            QStringLiteral("Captured the terminal lease state."),
            acquired->evidenceSha256()));
        const bool hasTerminalStatus
            = progress.status.has_value()
              && progress.operationResult.has_value();
        if ((progress.state != Data::ControllerControlState::Succeeded
             && progress.state != Data::ControllerControlState::Failed)
            || !hasTerminalStatus) {
            freezeUnknown(
                operation,
                QStringLiteral("acquire-outcome-unknown"),
                progress.detail.isEmpty()
                    ? QStringLiteral(
                          "The acquire terminal state does not prove whether the lease was "
                          "applied.")
                    : progress.detail);
            return;
        }

        const bool reportedSuccess
            = progress.state == Data::ControllerControlState::Succeeded
              && *progress.status == 0 && *progress.operationResult == 0;
        const bool responseMatchesEvidence
            = reportedSuccess == acquired->ownsControlLease();
        if (reportedSuccess && !acquired->ownsControlLease()) {
            operation.afterController = *acquired;
            freezeUnknown(
                operation,
                QStringLiteral("acquire-response-contradictory"),
                QStringLiteral(
                    "The acquire response reported success, but authoritative state does not "
                    "show the session owning the lease."));
            return;
        }

        operation.audit.append(providerEvent(
            operation,
            AuditKind::ProviderTerminalResponse,
            Action::AcquireControl,
            operation.request.identity().operationId().value(),
            operation.acquireRequestId,
            progress.status,
            progress.operationResult,
            acquired->evidenceSha256(),
            reportedSuccess
                ? QStringLiteral("lease-acquired")
                : QStringLiteral("lease-acquire-rejected"),
            progress.detail.isEmpty()
                ? (reportedSuccess
                       ? QStringLiteral("Exclusive controller lease acquired.")
                       : QStringLiteral("Exclusive controller lease request failed."))
                : progress.detail));
        publish(operation);
        if (operation.phase != Phase::AcquiringControl
            || operation.outcome != Outcome::Pending) {
            return;
        }

        if (acquired->ownsControlLease()) {
            if (!responseMatchesEvidence) {
                operation.afterController = *acquired;
                operation.releaseUnknownCode
                    = QStringLiteral("acquire-response-contradictory");
                operation.releaseUnknownDetail = QStringLiteral(
                    "The acquire response reported failure while authoritative state shows "
                    "that this session owns the lease.");
                beginRelease(operation, Outcome::OutcomeUnknown);
                return;
            }

            QString preflightError;
            const bool deployStillSafe
                = !snapshot.readOnly && !snapshot.mock
                  && acquired->serviceState()
                         == Data::ControllerServiceState::Shutdown
                  && acquired->runtimePackageState()
                         != Data::ControllerPackageState::Active
                  && exactTopologyMatches(
                      snapshot,
                      operation.evidence->semanticBindingArtifact(),
                      &preflightError);
            if (!deployStillSafe) {
                operation.afterController = *acquired;
                beginRelease(
                    operation, Outcome::FailedWithoutControllerChange);
                return;
            }
            beginDeploy(operation);
            return;
        }

        operation.afterController = *acquired;
        if (progress.state != Data::ControllerControlState::Failed
            || (*progress.status == 0 && *progress.operationResult == 0)) {
            freezeUnknown(
                operation,
                QStringLiteral("acquire-response-contradictory"),
                QStringLiteral(
                    "The acquire terminal state and status fields are contradictory."));
            return;
        }
        if (operation.beforeController->describesSameControllerStateAs(*acquired)) {
            finish(
                operation,
                Outcome::FailedWithoutControllerChange,
                QStringLiteral("lease-rejected"),
                operation.audit.constLast().detail(),
                acquired->evidenceSha256());
        } else {
            finish(
                operation,
                Outcome::FailedControllerChangedWithoutBinding,
                QStringLiteral("acquire-rejected-controller-changed"),
                QStringLiteral(
                    "The acquire request was rejected but the authoritative controller state "
                    "changed."),
                acquired->evidenceSha256());
        }
        return;
    }

    if (operation.phase == Phase::DeployingPackage) {
        const auto &progress = snapshot.packageDeploymentProgress;
        if (progress.operationId
            != operation.request.identity().operationId().value()) {
            return;
        }
        if (progress != operation.deploymentProgressBaseline) {
            if (!deploymentProgressCanRefreshDeadline(
                    operation.deploymentProgressBaseline,
                    progress,
                    operation.request,
                    operation.deploymentRequestSentAt)) {
                freezeUnknown(
                    operation,
                    QStringLiteral("deployment-progress-invalid"),
                    QStringLiteral(
                        "The provider returned regressive or mismatched package deployment "
                        "progress."));
                return;
            }
            operation.deploymentProgressBaseline = progress;
            if (!deploymentTerminal(progress.state)) {
                armProviderDeadline(
                    operation,
                    Phase::DeployingPackage,
                    Action::DeployPackage,
                    QStringLiteral("deployment-progress-timeout"),
                    QStringLiteral(
                        "Timed out after the last valid package deployment progress update."));
                return;
            }
        }
        if (!deploymentTerminal(progress.state))
            return;
        Data::RuntimePackageActivationDeploymentEvidence deployment{
            snapshot.sessionGeneration,
            snapshot.session->sessionId,
            snapshot.session->bootId,
            progress,
            QDateTime::currentDateTimeUtc(),
        };
        if (!deployment.isValid()) {
            freezeUnknown(
                operation,
                QStringLiteral("deployment-evidence-invalid"),
                QStringLiteral(
                    "The provider returned incomplete terminal package deployment evidence."));
            return;
        }
        operation.deploymentEvidence = deployment;
        operation.audit.append(providerEvent(
            operation,
            AuditKind::DeploymentEvidenceCaptured,
            Action::DeployPackage,
            progress.operationId,
            {},
            {},
            {},
            deployment.evidenceSha256(),
            QStringLiteral("deployment-evidence"),
            QStringLiteral("Captured terminal package deployment evidence.")));
        if (progress.state
            == Data::ControllerPackageDeploymentState::OutcomeUnknown) {
            freezeUnknown(
                operation,
                QStringLiteral("deployment-outcome-unknown"),
                progress.detail.isEmpty()
                    ? QStringLiteral(
                          "Package deployment ended without an authoritative outcome.")
                    : progress.detail);
            return;
        }
        operation.audit.append(providerEvent(
            operation,
            AuditKind::ProviderTerminalResponse,
            Action::DeployPackage,
            progress.operationId,
            {},
            progress.status.value_or(progress.state
                                             == Data::ControllerPackageDeploymentState::Succeeded
                                         ? 0
                                         : -1),
            progress.operationResult.value_or(
                progress.state
                        == Data::ControllerPackageDeploymentState::Succeeded
                    ? 0
                    : -1),
            deployment.evidenceSha256(),
            progress.state
                    == Data::ControllerPackageDeploymentState::Succeeded
                ? QStringLiteral("deployment-succeeded")
                : QStringLiteral("deployment-failed"),
            progress.detail.isEmpty()
                ? QStringLiteral("Package deployment reached a terminal state.")
                : progress.detail));
        publish(operation);
        if (operation.phase != Phase::DeployingPackage
            || operation.outcome != Outcome::Pending) {
            return;
        }
        beginRuntimeVerification(operation);
        return;
    }

    if (operation.phase == Phase::VerifyingRuntimeIdentity) {
        beginRuntimeVerification(operation);
        return;
    }

    if (operation.phase == Phase::ReleasingControl || recoveryRelease) {
        const auto &progress = snapshot.controlProgress;
        if (progress.command != Data::ControllerControlCommand::ReleaseControl
            || !progress.final
            || progress == operation.releaseProgressBaseline
            || !progress.startedAt.isValid()
            || progress.startedAt < operation.releaseRequestSentAt)
            return;
        QString error;
        const auto released
            = controllerEvidence(operation.provider, snapshot, &error);
        if (!released) {
            if (recoveryRelease) {
                operation.recoveryReleaseInProgress = false;
                operation.reconciliationInProgress = false;
            }
            freezeUnknown(
                operation,
                QStringLiteral("release-evidence-invalid"),
                error);
            return;
        }
        operation.controllerEvidenceHistory.append(*released);
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("release-evidence"),
            QStringLiteral("Captured the terminal lease release state."),
            released->evidenceSha256()));
        const bool hasTerminalStatus
            = progress.status.has_value()
              && progress.operationResult.has_value();
        const bool success
            = progress.state == Data::ControllerControlState::Succeeded
              && hasTerminalStatus && *progress.status == 0
              && *progress.operationResult == 0
              && !released->ownsControlLease();
        if (!hasTerminalStatus
            || (progress.state == Data::ControllerControlState::Succeeded
                && *progress.status == 0
                && *progress.operationResult == 0
                && released->ownsControlLease())) {
            if (recoveryRelease) {
                operation.recoveryReleaseInProgress = false;
                operation.reconciliationInProgress = false;
            }
            freezeUnknown(
                operation,
                QStringLiteral("release-outcome-unknown"),
                QStringLiteral(
                    "The terminal release response does not agree with authoritative lease "
                    "state."));
            return;
        }
        operation.audit.append(providerEvent(
            operation,
            AuditKind::ProviderTerminalResponse,
            Action::ReleaseControl,
            operation.request.identity().operationId().value(),
            operation.releaseRequestId,
            progress.status,
            progress.operationResult,
            released->evidenceSha256(),
            success ? QStringLiteral("lease-released")
                    : QStringLiteral("release-failed"),
            progress.detail.isEmpty()
                ? (success
                       ? QStringLiteral("Exclusive controller lease released.")
                       : QStringLiteral("Controller lease release failed."))
                : progress.detail));
        if (!success) {
            if (operation.afterController
                && operation.afterController->evidenceSha256()
                       != released->evidenceSha256()) {
                operation.controllerEvidenceHistory.append(
                    *operation.afterController);
            }
            operation.afterController = *released;
            if (recoveryRelease) {
                operation.recoveryReleaseInProgress = false;
                operation.reconciliationInProgress = false;
            }
            freezeUnknown(
                operation,
                QStringLiteral("release-outcome-unknown"),
                operation.audit.constLast().detail());
            return;
        }
        if (recoveryRelease) {
            publish(operation);
            if (operation.phase != Phase::Reconciling
                || operation.outcome != Outcome::OutcomeUnknown
                || !operation.reconciliationInProgress
                || !operation.recoveryReleaseInProgress) {
                return;
            }
            completeRecoveryAfterRelease(operation, *released);
            return;
        }
        if (operation.releaseOutcome
            == Outcome::SucceededWithActivatedPackage) {
            const Utils::Result<> attestation
                = released->matchesActivatedIdentity(
                      operation.request.identity())
                      ? verifyRuntimeSemanticMappingAttestation(
                            *released->mappingAttestation(),
                            operation.request.identity().scope(),
                            snapshot.sessionGeneration,
                            released->mappingAttestation()->epoch,
                            *operation.targetReference,
                            *operation.evidence)
                      : Utils::ResultError(
                            QStringLiteral(
                                "The active package changed during lease release."));
            if (!attestation) {
                if (operation.afterController
                    && operation.afterController->evidenceSha256()
                           != released->evidenceSha256()) {
                    operation.controllerEvidenceHistory.append(
                        *operation.afterController);
                }
                operation.afterController = *released;
                freezeUnknown(
                    operation,
                    QStringLiteral("controller-target-drifted"),
                    attestation.error());
                return;
            }
            const Utils::Result<Data::RuntimePackageActivationProjectCapture>
                persisted = m_projectService
                                ->captureRuntimePackageActivationProject(
                                    operation.request.identity().scope().projectId);
            if (!persisted || !persisted->isValid()
                || !operation.persistedDocumentRevision
                || persisted->documentRevision()
                       != *operation.persistedDocumentRevision
                || persisted->snapshot().modified
                || persisted->originalBinding()
                       != operation.projectCommit->resultingBinding()
                || persisted->snapshot().masterBindingArtifact
                       != *operation.targetReference) {
                if (operation.afterController
                    && operation.afterController->evidenceSha256()
                           != released->evidenceSha256()) {
                    operation.controllerEvidenceHistory.append(
                        *operation.afterController);
                }
                operation.afterController = *released;
                freezeUnknown(
                    operation,
                    QStringLiteral("project-binding-drifted"),
                    persisted
                        ? QStringLiteral(
                              "The project binding changed while the controller lease was "
                              "being released.")
                        : persisted.error());
                return;
            }
        }
        if (operation.releaseOutcome == Outcome::OutcomeUnknown) {
            if (operation.afterController
                && operation.afterController->evidenceSha256()
                       != released->evidenceSha256()) {
                operation.controllerEvidenceHistory.append(
                    *operation.afterController);
            }
            operation.afterController = *released;
            freezeUnknown(
                operation,
                operation.releaseUnknownCode.isEmpty()
                    ? QStringLiteral("project-persistence-outcome-unknown")
                    : operation.releaseUnknownCode,
                operation.releaseUnknownDetail.isEmpty()
                    ? QStringLiteral(
                          "The controller lease was released, but project persistence remains "
                          "unresolved.")
                    : operation.releaseUnknownDetail);
            return;
        }
        if (operation.releaseOutcome
            != Outcome::SucceededWithActivatedPackage) {
            if (operation.afterController
                && operation.afterController->evidenceSha256()
                       != released->evidenceSha256()) {
                operation.controllerEvidenceHistory.append(
                    *operation.afterController);
            }
            operation.afterController = *released;
            if (operation.beforeController
                && operation.beforeController->describesSameControllerStateAs(
                    *released)) {
                const bool rolledBack
                    = operation.deploymentEvidence
                      && std::any_of(
                          operation.deploymentEvidence->progress().audit.cbegin(),
                          operation.deploymentEvidence->progress().audit.cend(),
                          [](const Data::ControllerPackageDeploymentAuditEvent &event) {
                              return event.operation
                                         == Data::ControllerOperation::RollbackPackage
                                     && event.status == std::optional<qint32>(0)
                                     && event.operationResult
                                            == std::optional<qint32>(0);
                          });
                operation.releaseOutcome
                    = rolledBack ? Outcome::FailedAfterRollback
                                 : Outcome::FailedWithoutControllerChange;
            } else {
                operation.releaseOutcome
                    = Outcome::FailedControllerChangedWithoutBinding;
            }
        }
        const auto evidence
            = operation.releaseOutcome
                      == Outcome::SucceededWithActivatedPackage
                  && operation.projectCommit
                ? std::optional(operation.projectCommit->evidenceSha256())
                : std::optional(released->evidenceSha256());
        finish(
            operation,
            operation.releaseOutcome,
            operation.releaseOutcome
                    == Outcome::SucceededWithActivatedPackage
                ? QStringLiteral("activation-succeeded")
                : QStringLiteral("activation-failed"),
            operation.releaseOutcome
                    == Outcome::SucceededWithActivatedPackage
                ? QStringLiteral(
                      "Package activated, attested, attached to the project, and the lease "
                      "released.")
                : QStringLiteral(
                      "Activation failed and the exclusive controller lease was released."),
            evidence);
    }
}

void TrustedRuntimePackageActivationService::beginDeploy(Operation &operation)
{
    operation.phase = Phase::DeployingPackage;
    operation.detail = QStringLiteral("Uploading, validating, and activating the signed package.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("deploying-package"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::DeployingPackage
        || operation.outcome != Outcome::Pending) {
        return;
    }
    QString deployPreflightError;
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(
            operation.request.identity().operationId().value());
        return;
    }
    const Data::ControllerConnectionSnapshot deploySnapshot
        = operation.provider->connectionSnapshot();
    const auto deployEvidence
        = controllerEvidence(operation.provider, deploySnapshot, &deployPreflightError);
    if (!deployEvidence || !deployEvidence->ownsControlLease()
        || deploySnapshot.readOnly || deploySnapshot.mock
        || deployEvidence->serviceState()
               != Data::ControllerServiceState::Shutdown
        || deployEvidence->runtimePackageState()
               == Data::ControllerPackageState::Active
        || !exactTopologyMatches(
            deploySnapshot,
            operation.evidence->semanticBindingArtifact(),
            &deployPreflightError)) {
        if (deployEvidence && deployEvidence->ownsControlLease()) {
            operation.afterController = *deployEvidence;
            operation.audit.append(localEvent(
                operation,
                AuditKind::ControllerEvidenceCaptured,
                operation.phase,
                operation.outcome,
                QStringLiteral("deployment-preflight-failed-state"),
                QStringLiteral(
                    "Captured controller state after deployment preflight changed."),
                deployEvidence->evidenceSha256()));
            beginRelease(
                operation, Outcome::FailedWithoutControllerChange);
        } else {
            freezeUnknown(
                operation,
                QStringLiteral("controller-preflight-drifted"),
                deployPreflightError.isEmpty()
                    ? QStringLiteral(
                          "Controller state changed before package deployment.")
                    : deployPreflightError);
        }
        return;
    }

    const Utils::Result<> deploymentGuard = persistGuard(operation);
    if (!deploymentGuard) {
        QString error;
        const auto after = controllerEvidence(
            operation.provider,
            operation.provider->connectionSnapshot(),
            &error);
        if (!after || !after->ownsControlLease()) {
            freezeUnknown(
                operation,
                QStringLiteral("activation-journal-failed"),
                QStringLiteral(
                    "The deployment guard failed and the held lease could not be proved for "
                    "cleanup."));
            return;
        }
        operation.afterController = *after;
        operation.phase = Phase::VerifyingRuntimeIdentity;
        operation.detail = QStringLiteral(
            "Package deployment was not sent because durable evidence could not be updated.");
        operation.audit.append(localEvent(
            operation,
            AuditKind::PhaseTransition,
            operation.phase,
            operation.outcome,
            QStringLiteral("runtime-verification-skipped"),
            operation.detail));
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("deployment-not-sent"),
            QStringLiteral(
                "Package deployment was not sent because its durable guard update failed."),
            after->evidenceSha256()));
        beginRelease(operation, Outcome::FailedWithoutControllerChange);
        return;
    }
    operation.audit.append(providerEvent(
        operation,
        AuditKind::ProviderRequestSent,
        Action::DeployPackage,
        operation.request.identity().operationId().value(),
        {},
        {},
        {},
        {},
        QStringLiteral("deployment-request-sent"),
        QStringLiteral("Transactional package deployment request sent.")));
    operation.deploymentProgressBaseline
        = deploySnapshot.packageDeploymentProgress;
    operation.deploymentRequestSentAt
        = operation.audit.constLast().occurredAt();
    const Utils::Result<> requestGuard = persistGuard(operation);
    if (!requestGuard) {
        operation.audit.removeLast();
        operation.deploymentProgressBaseline = {};
        operation.deploymentRequestSentAt = {};
        QString error;
        const auto after = controllerEvidence(
            operation.provider,
            operation.provider->connectionSnapshot(),
            &error);
        if (after && after->ownsControlLease()) {
            operation.afterController = *after;
            operation.audit.append(localEvent(
                operation,
                AuditKind::ControllerEvidenceCaptured,
                operation.phase,
                operation.outcome,
                QStringLiteral("deployment-not-sent"),
                QStringLiteral(
                    "Package deployment was not sent because its durable request guard "
                    "failed."),
                after->evidenceSha256()));
            beginRelease(
                operation, Outcome::FailedWithoutControllerChange);
        } else {
            freezeUnknown(
                operation,
                QStringLiteral("activation-journal-failed"),
                requestGuard.error());
        }
        return;
    }

    Data::ControllerPackageDeploymentRequest request;
    request.operationId = operation.request.identity().operationId().value();
    request.artifact = operation.request.packageBytes();
    request.compiledProjectSource
        = operation.request.compiledProjectSource();
    request.configurationId = operation.request.identity().configurationId();
    request.activate = true;
    request.rollbackOnActivationFailure
        = operation.request.rollbackOnActivationFailure();
    armProviderDeadline(
        operation,
        Phase::DeployingPackage,
        Action::DeployPackage,
        QStringLiteral("deployment-timeout"),
        QStringLiteral(
            "Timed out while waiting for terminal package deployment evidence."));
    const Utils::Result<> result = operation.provider->deployPackage(request);
    if (operation.phase != Phase::DeployingPackage
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!result) {
        freezeUnknown(
            operation,
            QStringLiteral("deployment-outcome-unknown"),
            result.error());
        return;
    }
    publish(operation);
}

void TrustedRuntimePackageActivationService::beginRuntimeVerification(
    Operation &operation)
{
    if (operation.phase != Phase::VerifyingRuntimeIdentity) {
        operation.phase = Phase::VerifyingRuntimeIdentity;
        operation.detail
            = QStringLiteral("Verifying the active package epoch and semantic attestation.");
        operation.audit.append(localEvent(
            operation,
            AuditKind::PhaseTransition,
            operation.phase,
            operation.outcome,
            QStringLiteral("verifying-runtime"),
            operation.detail));
        publish(operation);
    }
    if (operation.phase != Phase::VerifyingRuntimeIdentity
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(
            operation.request.identity().operationId().value());
        return;
    }
    if (!operation.verificationTimeoutArmed) {
        operation.verificationTimeoutArmed = true;
        const QString operationId
            = operation.request.identity().operationId().value();
        QTimer::singleShot(10000, this, [this, operationId] {
            const auto found = m_operations.find(operationId);
            if (found == m_operations.end())
                return;
            Operation &pending = **found;
            if (pending.phase != Phase::VerifyingRuntimeIdentity
                || pending.outcome != Outcome::Pending) {
                return;
            }
            failRuntimeVerification(
                pending,
                QStringLiteral("runtime-verification-timeout"),
                QStringLiteral(
                    "Timed out while verifying the active package and semantic attestation."));
        });
    }

    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    QString error;
    std::optional<Data::RuntimePackageActivationControllerEvidence> after
        = controllerEvidence(operation.provider, snapshot, &error);
    const bool deploymentSucceeded
        = !operation.deploymentEvidence
          || operation.deploymentEvidence->outcome()
                 == Data::RuntimePackageActivationDeploymentOutcome::Succeeded;
    if (deploymentSucceeded && after
        && after->matchesActivatedIdentity(operation.request.identity())) {
        const Utils::Result<> attestation = verifyRuntimeSemanticMappingAttestation(
            *after->mappingAttestation(),
            operation.request.identity().scope(),
            snapshot.sessionGeneration,
            after->mappingAttestation()->epoch,
            *operation.targetReference,
            *operation.evidence);
        if (!attestation) {
            operation.afterController = *after;
            operation.audit.append(localEvent(
                operation,
                AuditKind::ControllerEvidenceCaptured,
                operation.phase,
                operation.outcome,
                QStringLiteral("runtime-identity-rejected"),
                attestation.error(),
                after->evidenceSha256()));
            publish(operation);
            if (operation.phase != Phase::VerifyingRuntimeIdentity
                || operation.outcome != Outcome::Pending) {
                return;
            }
            beginRelease(
                operation,
                Outcome::FailedControllerChangedWithoutBinding);
            return;
        }
        operation.afterController = *after;
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("runtime-identity-verified"),
            QStringLiteral("The controller package epoch and mapping proof match."),
            after->evidenceSha256()));
        publish(operation);
        if (operation.phase != Phase::VerifyingRuntimeIdentity
            || operation.outcome != Outcome::Pending) {
            return;
        }
        operation.phase = Phase::PersistingEvidence;
        operation.detail = QStringLiteral("Verified package evidence is durably imported.");
        operation.audit.append(localEvent(
            operation,
            AuditKind::PhaseTransition,
            operation.phase,
            operation.outcome,
            QStringLiteral("evidence-persisted"),
            operation.detail));
        publish(operation);
        if (operation.phase != Phase::PersistingEvidence
            || operation.outcome != Outcome::Pending) {
            return;
        }
        commitProjectBinding(operation);
        return;
    }

    if (operation.deploymentEvidence
        && operation.deploymentEvidence->outcome()
               == Data::RuntimePackageActivationDeploymentOutcome::Failed) {
        if (!after) {
            freezeUnknown(
                operation,
                QStringLiteral("failed-deployment-state-unknown"),
                error);
            return;
        }
        operation.afterController = *after;
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("failed-deployment-state"),
            QStringLiteral("Captured controller state after failed deployment."),
            after->evidenceSha256()));
        publish(operation);
        if (operation.phase != Phase::VerifyingRuntimeIdentity
            || operation.outcome != Outcome::Pending) {
            return;
        }
        Outcome outcome
            = operation.beforeController->describesSameControllerStateAs(*after)
                  ? Outcome::FailedWithoutControllerChange
                  : Outcome::FailedControllerChangedWithoutBinding;
        if (outcome == Outcome::FailedWithoutControllerChange) {
            const bool rolledBack = std::any_of(
                operation.deploymentEvidence->progress().audit.cbegin(),
                operation.deploymentEvidence->progress().audit.cend(),
                [](const Data::ControllerPackageDeploymentAuditEvent &event) {
                    return event.operation == Data::ControllerOperation::RollbackPackage
                           && event.status == std::optional<qint32>(0)
                           && event.operationResult == std::optional<qint32>(0);
                });
            if (rolledBack)
                outcome = Outcome::FailedAfterRollback;
        }
        beginRelease(operation, outcome);
        return;
    }

    const std::optional<Data::RuntimeResourceCatalog> catalog
        = operation.provider->runtimeResourceCatalog();
    if (!catalog || catalog->scope != operation.request.identity().scope()
        || catalog->sessionGeneration != snapshot.sessionGeneration
        || !Data::isCompleteRuntimeSemanticMappingEpoch(catalog->epoch)
        || catalog->epoch.configurationId
               != operation.request.identity().configurationId()
        || catalog->epoch.catalogRevision
               != operation.request.identity().expectedCatalogRevision()
        || catalog->epoch.topologyIdentity
               != operation.request.identity().expectedTopologyIdentity()) {
        return;
    }
    if (operation.attestationRequested)
        return;
    Data::RuntimeSemanticMappingAttestationRequest request{
        operation.request.identity().operationId().value(),
        operation.request.identity().scope(),
        snapshot.sessionGeneration,
        catalog->epoch,
        operation.request.identity().expectedMappingProof(),
    };
    if (!request.isValid()) {
        freezeUnknown(
            operation,
            QStringLiteral("attestation-request-invalid"),
            QStringLiteral(
                "The active package epoch could not be bound to a valid attestation request."));
        return;
    }
    operation.attestationRequested = true;
    const Utils::Result<> result
        = operation.provider->requestRuntimeSemanticMappingAttestation(request);
    if (operation.phase != Phase::VerifyingRuntimeIdentity
        || operation.outcome != Outcome::Pending
        || !operation.attestationRequested) {
        return;
    }
    if (!result) {
        failRuntimeVerification(
            operation,
            QStringLiteral("attestation-outcome-unknown"),
            result.error());
    }
}

void TrustedRuntimePackageActivationService::handleAttestationResult(
    const QString &operationId,
    const Data::RuntimeSemanticMappingAttestationResult &result)
{
    const auto found = m_operations.find(operationId);
    if (found == m_operations.end())
        return;
    Operation &operation = **found;
    if (operation.phase != Phase::VerifyingRuntimeIdentity
        || !operation.attestationRequested
        || result.request.correlationId != operationId) {
        return;
    }
    operation.attestationRequested = false;
    if (!result.isValid() || result.error) {
        failRuntimeVerification(
            operation,
            QStringLiteral("attestation-outcome-unknown"),
            result.error
                ? result.error->summary
                : QStringLiteral(
                      "The controller returned an invalid semantic attestation result."));
        return;
    }
    beginRuntimeVerification(operation);
}

void TrustedRuntimePackageActivationService::failRuntimeVerification(
    Operation &operation, const QString &code, const QString &detail)
{
    if (operation.phase != Phase::VerifyingRuntimeIdentity
        || operation.outcome != Outcome::Pending) {
        return;
    }

    QString evidenceError;
    const auto after
        = operation.provider
              ? controllerEvidence(
                    operation.provider,
                    operation.provider->connectionSnapshot(),
                    &evidenceError)
              : std::optional<Data::RuntimePackageActivationControllerEvidence>();
    if (after) {
        if (operation.afterController
            && operation.afterController->evidenceSha256()
                   != after->evidenceSha256()) {
            operation.controllerEvidenceHistory.append(
                *operation.afterController);
        }
        operation.afterController = *after;
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("runtime-verification-failed-state"),
            QStringLiteral(
                "Captured controller state after runtime verification failed."),
            after->evidenceSha256()));
        publish(operation);
        if (operation.phase != Phase::VerifyingRuntimeIdentity
            || operation.outcome != Outcome::Pending) {
            return;
        }
    }

    if (operation.existingPackage) {
        if (after && operation.beforeController
            && samePackageRuntime(
                *operation.beforeController, *after)
            && !operation.beforeController->controlLeaseOwnerSessionId()
            && !after->controlLeaseOwnerSessionId()) {
            finish(
                operation,
                Outcome::FailedWithoutControllerChange,
                code,
                detail,
                after->evidenceSha256());
        } else if (after) {
            finish(
                operation,
                Outcome::FailedControllerChangedWithoutBinding,
                code,
                evidenceError.isEmpty() ? detail : evidenceError,
                after->evidenceSha256());
        } else {
            operation.afterController.reset();
            finish(
                operation,
                Outcome::FailedWithoutControllerChange,
                code,
                evidenceError.isEmpty() ? detail : evidenceError,
                operation.request.fingerprint());
        }
        return;
    }
    if (after && after->ownsControlLease()) {
        beginRelease(
            operation,
            Outcome::FailedControllerChangedWithoutBinding);
        return;
    }
    freezeUnknown(
        operation,
        code,
        evidenceError.isEmpty() ? detail : evidenceError);
}

void TrustedRuntimePackageActivationService::handleProviderUnavailable(
    const QString &operationId)
{
    const auto found = m_operations.find(operationId);
    if (found == m_operations.end())
        return;
    Operation &operation = **found;
    if (operation.phase == Phase::Reconciling
        && operation.outcome == Outcome::OutcomeUnknown
        && operation.reconciliationInProgress) {
        operation.recoveryReleaseInProgress = false;
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("controller-provider-unavailable"),
            QStringLiteral(
                "The controller provider disappeared during authoritative recovery."));
        return;
    }
    if (operation.outcome != Outcome::Pending)
        return;
    const bool unresolvedMutation
        = outstandingProviderAction(operation.audit) != Action::None
          || recordedLeaseStillHeld(operation.audit)
          || (operation.afterController
              && operation.afterController->ownsControlLease())
          || operation.projectCommit;
    if (!unresolvedMutation) {
        if (operation.beforeController && operation.afterController) {
            const bool unchanged
                = samePackageRuntime(
                      *operation.beforeController,
                      *operation.afterController)
                  && !operation.beforeController
                          ->controlLeaseOwnerSessionId()
                  && !operation.afterController
                          ->controlLeaseOwnerSessionId();
            finish(
                operation,
                unchanged ? Outcome::FailedWithoutControllerChange
                          : Outcome::FailedControllerChangedWithoutBinding,
                QStringLiteral("controller-provider-unavailable"),
                QStringLiteral(
                    "The controller provider disappeared after its last authoritative state "
                    "was recorded."),
                operation.afterController->evidenceSha256());
            return;
        }
        failBeforeControllerMutation(
            operation,
            QStringLiteral("controller-provider-unavailable"),
            QStringLiteral(
                "The controller provider disappeared before any controller mutation."));
        return;
    }
    freezeUnknown(
        operation,
        QStringLiteral("controller-provider-unavailable"),
        QStringLiteral(
            "The controller provider disappeared while a lease or mutation outcome was "
            "unresolved."));
}

void TrustedRuntimePackageActivationService::commitProjectBinding(
    Operation &operation)
{
    operation.phase = Phase::CommittingProjectBinding;
    operation.detail = QStringLiteral("Attaching the exact verified binding to the project.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("committing-project-binding"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::CommittingProjectBinding
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(
            operation.request.identity().operationId().value());
        return;
    }
    const Data::ControllerConnectionSnapshot commitSnapshot
        = operation.provider->connectionSnapshot();
    QString commitEvidenceError;
    const auto commitEvidence
        = controllerEvidence(
            operation.provider, commitSnapshot, &commitEvidenceError);
    const bool leaseStateExpected
        = commitEvidence
          && (operation.existingPackage
                  ? commitEvidence->controlLeaseOwnerSessionId() == 0
                  : commitEvidence->ownsControlLease());
    const Utils::Result<> commitAttestation
        = commitEvidence
              && commitEvidence->matchesActivatedIdentity(
                  operation.request.identity())
          ? verifyRuntimeSemanticMappingAttestation(
                *commitEvidence->mappingAttestation(),
                operation.request.identity().scope(),
                commitSnapshot.sessionGeneration,
                commitEvidence->mappingAttestation()->epoch,
                *operation.targetReference,
                *operation.evidence)
          : Utils::ResultError(
                commitEvidenceError.isEmpty()
                    ? QStringLiteral(
                          "The active package changed before project commit.")
                    : commitEvidenceError);
    if (!commitEvidence || !leaseStateExpected || !commitAttestation
        || commitSnapshot.readOnly || commitSnapshot.mock) {
        if (commitEvidence) {
            if (operation.afterController
                && operation.afterController->evidenceSha256()
                       != commitEvidence->evidenceSha256()) {
                operation.controllerEvidenceHistory.append(
                    *operation.afterController);
            }
            operation.afterController = *commitEvidence;
            operation.audit.append(localEvent(
                operation,
                AuditKind::ControllerEvidenceCaptured,
                operation.phase,
                operation.outcome,
                QStringLiteral("project-commit-controller-drift"),
                QStringLiteral(
                    "Captured controller state after the project-commit fence changed."),
                commitEvidence->evidenceSha256()));
        }
        operation.releaseUnknownCode
            = QStringLiteral("controller-target-drifted");
        operation.releaseUnknownDetail
            = commitAttestation
                  ? QStringLiteral(
                        "The controller lease state changed before project commit.")
                  : commitAttestation.error();
        if (!operation.existingPackage && commitEvidence
            && commitEvidence->ownsControlLease()) {
            beginRelease(operation, Outcome::OutcomeUnknown);
        } else {
            freezeUnknown(
                operation,
                operation.releaseUnknownCode,
                operation.releaseUnknownDetail);
        }
        return;
    }
    if (operation.afterController
        && operation.afterController->evidenceSha256()
               != commitEvidence->evidenceSha256()) {
        operation.controllerEvidenceHistory.append(
            *operation.afterController);
    }
    operation.afterController = *commitEvidence;
    operation.audit.append(localEvent(
        operation,
        AuditKind::ControllerEvidenceCaptured,
        operation.phase,
        operation.outcome,
        QStringLiteral("project-commit-controller-fence"),
        QStringLiteral(
            "Revalidated the active package immediately before project commit."),
        commitEvidence->evidenceSha256()));

    const Utils::Result<> projectGuard = persistGuard(operation);
    if (!projectGuard) {
        if (operation.existingPackage) {
            failBeforeControllerMutation(
                operation,
                QStringLiteral("activation-journal-failed"),
                projectGuard.error());
        } else {
            freezeUnknown(
                operation,
                QStringLiteral("activation-journal-failed"),
                projectGuard.error());
        }
        return;
    }

    Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult> result
        = m_projectService->compareAndSetMasterBindingArtifact(
            operation.request.identity().scope().projectId,
            operation.request.identity().documentRevisionToken(),
            operation.request.identity().originalBindingToken(),
            *operation.targetReference);
    if (!result || !result->isValid()
        || result->disposition()
               == Data::RuntimePackageActivationProjectCompareAndSetDisposition::Stale
        || !result->commit()
        || result->commit()->resultingBinding()
               != operation.request.identity().targetBindingToken()) {
        if (operation.existingPackage) {
            finish(
                operation,
                Outcome::FailedWithoutControllerChange,
                QStringLiteral("project-cas-stale"),
                result ? QStringLiteral(
                             "The project changed before its verified binding could be attached.")
                       : result.error(),
                operation.afterController->evidenceSha256());
        } else {
            beginRelease(
                operation, Outcome::FailedControllerChangedWithoutBinding);
        }
        return;
    }
    operation.projectCommit = *result->commit();
    operation.audit.append(localEvent(
        operation,
        AuditKind::ProjectCompareAndSet,
        operation.phase,
        operation.outcome,
        QStringLiteral("project-binding-committed"),
        QStringLiteral("The verified semantic binding is attached to the project."),
        operation.projectCommit->evidenceSha256(),
        operation.projectCommit->committedAt()));
    publish(operation);
    if (operation.phase != Phase::CommittingProjectBinding
        || operation.outcome != Outcome::Pending) {
        return;
    }

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> beforeSave
        = m_projectService->captureRuntimePackageActivationProject(
            operation.request.identity().scope().projectId);
    const bool alreadyExact
        = operation.projectCommit->disposition()
          == Data::RuntimePackageActivationProjectCommitDisposition::
              AlreadyExact;
    if (!beforeSave || !beforeSave->isValid()
        || beforeSave->documentRevision()
               != operation.projectCommit->resultingDocumentRevision()
        || beforeSave->originalBinding()
               != operation.projectCommit->resultingBinding()
        || beforeSave->snapshot().masterBindingArtifact
               != *operation.targetReference
        || (alreadyExact ? beforeSave->snapshot().modified
                         : !beforeSave->snapshot().modified)) {
        operation.releaseUnknownCode
            = QStringLiteral("project-cas-fence-broken");
        operation.releaseUnknownDetail
            = beforeSave
                  ? QStringLiteral(
                        "The project changed after its activation compare-and-set and before "
                        "the exact revision could be saved.")
                  : beforeSave.error();
        if (operation.existingPackage) {
            freezeUnknown(
                operation,
                operation.releaseUnknownCode,
                operation.releaseUnknownDetail);
        } else {
            beginRelease(operation, Outcome::OutcomeUnknown);
        }
        return;
    }
    if (alreadyExact) {
        operation.persistedDocumentRevision
            = beforeSave->documentRevision();
        const Utils::Result<> persistedRevisionGuard
            = persistGuard(operation);
        if (!persistedRevisionGuard) {
            operation.releaseUnknownCode
                = QStringLiteral("activation-journal-failed");
            operation.releaseUnknownDetail
                = persistedRevisionGuard.error();
            if (operation.existingPackage) {
                freezeUnknown(
                    operation,
                    operation.releaseUnknownCode,
                    operation.releaseUnknownDetail);
            } else {
                beginRelease(operation, Outcome::OutcomeUnknown);
            }
            return;
        }
        if (operation.existingPackage) {
            scheduleCompleteExistingPackage(operation);
        } else {
            beginRelease(
                operation, Outcome::SucceededWithActivatedPackage);
        }
        return;
    }

    const Utils::Result<> saved = m_projectService->saveProject(
        operation.request.identity().scope().projectId);
    if (!saved) {
        operation.releaseUnknownCode
            = QStringLiteral("project-save-outcome-unknown");
        operation.releaseUnknownDetail = saved.error();
        if (operation.existingPackage) {
            freezeUnknown(
                operation,
                operation.releaseUnknownCode,
                operation.releaseUnknownDetail);
        } else {
            beginRelease(operation, Outcome::OutcomeUnknown);
        }
        return;
    }
    const Utils::Result<Data::RuntimePackageActivationProjectCapture> persisted
        = m_projectService->captureRuntimePackageActivationProject(
            operation.request.identity().scope().projectId);
    if (!persisted || !persisted->isValid()
        || persisted->snapshot().modified
        || persisted->documentRevisionNumber()
               != beforeSave->documentRevisionNumber() + 1
        || persisted->documentRevision() == beforeSave->documentRevision()
        || persisted->serializedProject() != beforeSave->serializedProject()
        || persisted->originalBinding()
               != operation.projectCommit->resultingBinding()
        || persisted->snapshot().masterBindingArtifact
               != *operation.targetReference) {
        operation.releaseUnknownCode
            = QStringLiteral("project-save-verification-failed");
        operation.releaseUnknownDetail
            = persisted
                  ? QStringLiteral(
                        "The saved project does not retain the exact activated binding.")
                  : persisted.error();
        if (operation.existingPackage) {
            freezeUnknown(
                operation,
                operation.releaseUnknownCode,
                operation.releaseUnknownDetail);
        } else {
            beginRelease(operation, Outcome::OutcomeUnknown);
        }
        return;
    }
    operation.persistedDocumentRevision = persisted->documentRevision();

    const Utils::Result<> persistedRevisionGuard
        = persistGuard(operation);
    if (!persistedRevisionGuard) {
        operation.releaseUnknownCode
            = QStringLiteral("activation-journal-failed");
        operation.releaseUnknownDetail
            = persistedRevisionGuard.error();
        if (operation.existingPackage) {
            freezeUnknown(
                operation,
                operation.releaseUnknownCode,
                operation.releaseUnknownDetail);
        } else {
            beginRelease(operation, Outcome::OutcomeUnknown);
        }
        return;
    }
    if (operation.existingPackage) {
        scheduleCompleteExistingPackage(operation);
        return;
    }
    beginRelease(operation, Outcome::SucceededWithActivatedPackage);
}

void TrustedRuntimePackageActivationService::scheduleCompleteExistingPackage(
    Operation &operation)
{
    const QString operationId
        = operation.request.identity().operationId().value();
    QTimer::singleShot(0, this, [this, operationId] {
        const auto found = m_operations.find(operationId);
        if (found == m_operations.end())
            return;
        Operation &pending = **found;
        if (!pending.existingPackage
            || !pending.persistedDocumentRevision
            || pending.phase != Phase::CommittingProjectBinding
            || pending.outcome != Outcome::Pending) {
            return;
        }
        completeExistingPackage(pending);
    });
}

void TrustedRuntimePackageActivationService::completeExistingPackage(
    Operation &operation)
{
    if (operation.phase != Phase::CommittingProjectBinding
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(
            operation.request.identity().operationId().value());
        return;
    }

    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    QString evidenceError;
    const auto current
        = controllerEvidence(operation.provider, snapshot, &evidenceError);
    const Utils::Result<> attestation
        = current
              && current->matchesActivatedIdentity(
                  operation.request.identity())
          ? verifyRuntimeSemanticMappingAttestation(
                *current->mappingAttestation(),
                operation.request.identity().scope(),
                snapshot.sessionGeneration,
                current->mappingAttestation()->epoch,
                *operation.targetReference,
                *operation.evidence)
          : Utils::ResultError(
                evidenceError.isEmpty()
                    ? QStringLiteral(
                          "The active package changed before activation completed.")
                    : evidenceError);
    const Utils::Result<Data::RuntimePackageActivationProjectCapture>
        persisted = m_projectService
                        ->captureRuntimePackageActivationProject(
                            operation.request.identity().scope().projectId);
    const bool projectStillExact
        = persisted && persisted->isValid()
          && operation.persistedDocumentRevision
          && persisted->documentRevision()
                 == *operation.persistedDocumentRevision
          && !persisted->snapshot().modified
          && persisted->originalBinding()
                 == operation.projectCommit->resultingBinding()
          && persisted->snapshot().masterBindingArtifact
                 == *operation.targetReference;
    if (!current || !attestation || current->controlLeaseOwnerSessionId() != 0
        || snapshot.readOnly || snapshot.mock || !projectStillExact) {
        if (current) {
            if (operation.afterController
                && operation.afterController->evidenceSha256()
                       != current->evidenceSha256()) {
                operation.controllerEvidenceHistory.append(
                    *operation.afterController);
            }
            operation.afterController = *current;
            operation.audit.append(localEvent(
                operation,
                AuditKind::ControllerEvidenceCaptured,
                operation.phase,
                operation.outcome,
                QStringLiteral("existing-package-terminal-drift"),
                QStringLiteral(
                    "Captured controller state after the existing-package terminal fence "
                    "changed."),
                current->evidenceSha256()));
        }
        freezeUnknown(
            operation,
            !attestation
                ? QStringLiteral("controller-target-drifted")
                : QStringLiteral("project-binding-drifted"),
            !attestation
                ? attestation.error()
                : (persisted
                       ? QStringLiteral(
                             "The saved project changed before activation completed.")
                       : persisted.error()));
        return;
    }

    if (operation.afterController
        && operation.afterController->evidenceSha256()
               != current->evidenceSha256()) {
        operation.controllerEvidenceHistory.append(
            *operation.afterController);
    }
    operation.afterController = *current;
    operation.audit.append(localEvent(
        operation,
        AuditKind::ControllerEvidenceCaptured,
        operation.phase,
        operation.outcome,
        QStringLiteral("existing-package-terminal-fence"),
        QStringLiteral(
            "Revalidated the active package and project before completion."),
        current->evidenceSha256()));
    finish(
        operation,
        Outcome::SucceededWithExistingPackage,
        QStringLiteral("existing-package-attached"),
        QStringLiteral(
            "The already-active verified package is attached to the current project."),
        operation.projectCommit->evidenceSha256());
}

void TrustedRuntimePackageActivationService::beginRecoveryRelease(
    Operation &operation, Outcome terminalOutcome)
{
    if (operation.phase != Phase::Reconciling
        || operation.outcome != Outcome::OutcomeUnknown
        || !operation.reconciliationInProgress
        || operation.recoveryReleaseInProgress
        || outstandingProviderAction(operation.audit) != Action::None) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("activation-recovery-release-invalid"),
            QStringLiteral(
                "The durable activation is not in a state where its lease can be safely "
                "released."));
        return;
    }
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())
        || !operation.provider->supportsControlCommand(
            Data::ControllerControlCommand::ReleaseControl)) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("controller-provider-unavailable"),
            QStringLiteral(
                "The controller provider is unavailable for recovery lease cleanup."));
        return;
    }

    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    QString evidenceError;
    const auto held = controllerEvidence(
        operation.provider, snapshot, &evidenceError);
    std::optional<Data::RuntimePackageActivationAuditEvent> acquireEvent;
    bool leaseRecorded = false;
    for (const auto &event : std::as_const(operation.audit)) {
        const bool success
            = (event.kind() == AuditKind::ProviderTerminalResponse
               && event.providerStatus() == std::optional<qint32>(0)
               && event.providerOperationResult()
                      == std::optional<qint32>(0))
              || (event.kind() == AuditKind::ProviderOutcomeReconciled
                  && event.providerReconciliation()
                         == Reconciliation::Applied);
        if (event.kind() == AuditKind::ControlLeaseReconciled
            || (success && event.providerAction() == Action::ReleaseControl)) {
            acquireEvent.reset();
            leaseRecorded = false;
        } else if (success
                   && event.providerAction() == Action::AcquireControl) {
            acquireEvent = event;
            leaseRecorded = true;
        }
    }
    if (!held || !held->ownsControlLease() || !leaseRecorded
        || !acquireEvent
        || held->controlLeaseOwnerSessionId()
               != acquireEvent->providerSessionId()
        || held->bootId() != acquireEvent->providerBootId()
        || snapshot.readOnly || snapshot.mock) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("recovery-lease-owner-mismatch"),
            evidenceError.isEmpty()
                ? QStringLiteral(
                      "The current lease cannot be proven to belong to this activation.")
                : evidenceError);
        return;
    }

    if (operation.afterController
        && operation.afterController->evidenceSha256()
               != held->evidenceSha256()) {
        operation.controllerEvidenceHistory.append(
            *operation.afterController);
    }
    operation.afterController = *held;
    operation.audit.append(localEvent(
        operation,
        AuditKind::ControllerEvidenceCaptured,
        operation.phase,
        operation.outcome,
        QStringLiteral("recovery-release-preflight"),
        QStringLiteral(
            "Revalidated the activation-owned lease before recovery cleanup."),
        held->evidenceSha256()));

    operation.releaseOutcome = terminalOutcome;
    operation.recoveryReleaseInProgress = true;
    operation.releaseProgressBaseline = snapshot.controlProgress;
    quint64 maximumRequestId = 0;
    for (const auto &event : std::as_const(operation.audit)) {
        if (event.providerRequestId())
            maximumRequestId = std::max(
                maximumRequestId, *event.providerRequestId());
    }
    if (maximumRequestId == std::numeric_limits<quint64>::max()) {
        operation.recoveryReleaseInProgress = false;
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("recovery-request-id-exhausted"),
            QStringLiteral(
                "No unique provider RequestId remains for recovery lease cleanup."));
        return;
    }
    operation.releaseRequestId = maximumRequestId + 1;
    operation.audit.append(providerEvent(
        operation,
        AuditKind::ProviderRequestSent,
        Action::ReleaseControl,
        operation.request.identity().operationId().value(),
        operation.releaseRequestId,
        {},
        {},
        {},
        QStringLiteral("recovery-release-request-sent"),
        QStringLiteral(
            "Recovery sent one controlled release for the activation-owned lease.")));
    operation.releaseRequestSentAt = operation.audit.constLast().occurredAt();
    const Utils::Result<> guard = persistGuard(operation);
    if (!guard) {
        operation.audit.removeLast();
        operation.releaseRequestSentAt = {};
        operation.recoveryReleaseInProgress = false;
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("activation-journal-failed"),
            guard.error());
        return;
    }

    armProviderDeadline(
        operation,
        Phase::Reconciling,
        Action::ReleaseControl,
        QStringLiteral("recovery-release-timeout"),
        QStringLiteral(
            "Timed out while waiting for authoritative recovery lease release."));
    Data::ControllerControlRequest request;
    request.command = Data::ControllerControlCommand::ReleaseControl;
    const Utils::Result<> result
        = operation.provider->executeControlCommand(request);
    if (!operation.recoveryReleaseInProgress
        || operation.phase != Phase::Reconciling
        || operation.outcome != Outcome::OutcomeUnknown) {
        return;
    }
    if (!result) {
        operation.recoveryReleaseInProgress = false;
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("release-outcome-unknown"),
            result.error());
        return;
    }
    publish(operation);
}

void TrustedRuntimePackageActivationService::completeRecoveryAfterRelease(
    Operation &operation,
    const Data::RuntimePackageActivationControllerEvidence &released)
{
    operation.recoveryReleaseInProgress = false;
    if (operation.phase != Phase::Reconciling
        || operation.outcome != Outcome::OutcomeUnknown
        || !operation.reconciliationInProgress
        || !operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("controller-provider-unavailable"),
            QStringLiteral(
                "The controller provider changed before recovery could be finalized."));
        return;
    }

    const Data::ControllerConnectionSnapshot snapshot
        = operation.provider->connectionSnapshot();
    QString evidenceError;
    const auto current = controllerEvidence(
        operation.provider, snapshot, &evidenceError);
    if (!current || current->ownsControlLease()
        || current->bootId() != released.bootId()
        || !samePackageRuntime(released, *current)) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            QStringLiteral("release-outcome-unknown"),
            evidenceError.isEmpty()
                ? QStringLiteral(
                      "The controller changed before recovery release could be finalized.")
                : evidenceError);
        return;
    }
    if (current->evidenceSha256() != released.evidenceSha256()) {
        operation.controllerEvidenceHistory.append(*current);
        operation.audit.append(localEvent(
            operation,
            AuditKind::ControllerEvidenceCaptured,
            operation.phase,
            operation.outcome,
            QStringLiteral("recovery-release-terminal-fence"),
            QStringLiteral(
                "Revalidated controller identity after recovery lease release."),
            current->evidenceSha256()));
    }

    const Utils::Result<Data::RuntimePackageActivationProjectCapture>
        projectCapture = m_projectService
                             ->captureRuntimePackageActivationProject(
                                 operation.request.identity().scope().projectId);
    const bool projectExact
        = operation.projectCommit && projectCapture
          && projectCapture->isValid()
          && operation.persistedDocumentRevision
          && operation.targetReference
          && projectCapture->documentRevision()
                 == *operation.persistedDocumentRevision
          && !projectCapture->snapshot().modified
          && projectCapture->originalBinding()
                 == operation.projectCommit->resultingBinding()
          && projectCapture->snapshot().masterBindingArtifact
                 == *operation.targetReference;
    const bool controllerTargetExact
        = current->matchesActivatedIdentity(
              operation.request.identity())
          && current->mappingAttestation()
          && operation.targetReference && operation.evidence
          && verifyRuntimeSemanticMappingAttestation(
              *current->mappingAttestation(),
              operation.request.identity().scope(),
              snapshot.sessionGeneration,
              current->mappingAttestation()->epoch,
              *operation.targetReference,
              *operation.evidence);

    if (operation.releaseOutcome
        == Outcome::SucceededWithActivatedPackage) {
        if (!projectExact || !controllerTargetExact) {
            operation.reconciliationInProgress = false;
            freezeUnknown(
                operation,
                controllerTargetExact
                    ? QStringLiteral("project-binding-drifted")
                    : QStringLiteral("controller-target-drifted"),
                projectCapture
                    ? QStringLiteral(
                          "The controller and saved project no longer match the recovered "
                          "activation.")
                    : projectCapture.error());
            return;
        }
    } else if (operation.releaseOutcome == Outcome::OutcomeUnknown) {
        operation.reconciliationInProgress = false;
        freezeUnknown(
            operation,
            operation.releaseUnknownCode.isEmpty()
                ? QStringLiteral("activation-reconciliation-incomplete")
                : operation.releaseUnknownCode,
            operation.releaseUnknownDetail.isEmpty()
                ? QStringLiteral(
                      "The activation-owned lease was released, but the final durable "
                      "outcome remains unresolved.")
                : operation.releaseUnknownDetail);
        return;
    } else {
        if (!samePackageRuntime(
                *operation.beforeController, *current)) {
            operation.releaseOutcome
                = Outcome::FailedControllerChangedWithoutBinding;
        } else if (operation.releaseOutcome
                   == Outcome::FailedControllerChangedWithoutBinding) {
            operation.releaseOutcome
                = Outcome::FailedWithoutControllerChange;
        }
        if (operation.afterController
            && operation.afterController->evidenceSha256()
                   != current->evidenceSha256()) {
            operation.controllerEvidenceHistory.append(
                *operation.afterController);
        }
        operation.afterController = *current;
    }

    const auto evidence
        = operation.releaseOutcome
                  == Outcome::SucceededWithActivatedPackage
              && operation.projectCommit
            ? std::optional(operation.projectCommit->evidenceSha256())
            : std::optional(current->evidenceSha256());
    finish(
        operation,
        operation.releaseOutcome,
        operation.releaseOutcome
                == Outcome::SucceededWithActivatedPackage
            ? QStringLiteral("activation-reconciled")
            : QStringLiteral("activation-recovery-failed"),
        operation.releaseOutcome
                == Outcome::SucceededWithActivatedPackage
            ? QStringLiteral(
                  "Recovered activation identity, project binding, and lease cleanup were "
                  "verified.")
            : QStringLiteral(
                  "Recovery released the activation-owned lease and proved the final "
                  "controller state."),
        evidence);
}

void TrustedRuntimePackageActivationService::beginRelease(
    Operation &operation, Outcome terminalOutcome)
{
    if (operation.phase == Phase::AcquiringControl
        || operation.phase == Phase::DeployingPackage) {
        operation.phase = Phase::VerifyingRuntimeIdentity;
        operation.detail = QStringLiteral(
            "Controller mutation failed before runtime identity verification.");
        operation.audit.append(localEvent(
            operation,
            AuditKind::PhaseTransition,
            operation.phase,
            operation.outcome,
            QStringLiteral("runtime-verification-skipped"),
            operation.detail));
    }
    if (operation.phase == Phase::VerifyingRuntimeIdentity) {
        operation.phase = Phase::PersistingEvidence;
        operation.detail = QStringLiteral("Activation failure evidence retained.");
        operation.audit.append(localEvent(
            operation,
            AuditKind::PhaseTransition,
            operation.phase,
            operation.outcome,
            QStringLiteral("failure-evidence-persisted"),
            operation.detail));
        operation.phase = Phase::CommittingProjectBinding;
        operation.detail = QStringLiteral("Project binding commit skipped.");
        operation.audit.append(localEvent(
            operation,
            AuditKind::PhaseTransition,
            operation.phase,
            operation.outcome,
            QStringLiteral("project-binding-skipped"),
            operation.detail));
    }
    operation.releaseOutcome = terminalOutcome;
    operation.phase = Phase::ReleasingControl;
    operation.detail = QStringLiteral("Releasing the exclusive controller lease.");
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        QStringLiteral("releasing-control"),
        operation.detail));
    publish(operation);
    if (operation.phase != Phase::ReleasingControl
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!operation.provider || !operation.provider->isAvailable()
        || !m_providerRegistry
        || !m_providerRegistry
                ->providers(Core::ProviderKind::ControllerConnection)
                .contains(operation.provider.data())) {
        handleProviderUnavailable(
            operation.request.identity().operationId().value());
        return;
    }

    const Data::ControllerConnectionSnapshot releaseSnapshot
        = operation.provider->connectionSnapshot();
    QString releaseEvidenceError;
    const auto releaseEvidence
        = controllerEvidence(
            operation.provider, releaseSnapshot, &releaseEvidenceError);
    if (!releaseEvidence || !releaseEvidence->ownsControlLease()
        || releaseSnapshot.readOnly || releaseSnapshot.mock
        || !operation.beforeController
        || releaseEvidence->sessionGeneration()
               != operation.beforeController->sessionGeneration()
        || releaseEvidence->observationSessionId()
               != operation.beforeController->observationSessionId()
        || releaseEvidence->bootId() != operation.beforeController->bootId()) {
        freezeUnknown(
            operation,
            QStringLiteral("release-preflight-failed"),
            releaseEvidenceError.isEmpty()
                ? QStringLiteral(
                      "The held lease could not be proved immediately before release.")
                : releaseEvidenceError);
        return;
    }

    const bool releaseEvidenceMatchesTarget
        = releaseEvidence->matchesActivatedIdentity(
            operation.request.identity());
    if (terminalOutcome == Outcome::SucceededWithActivatedPackage) {
        const Utils::Result<> attestation
            = releaseEvidenceMatchesTarget
                  ? verifyRuntimeSemanticMappingAttestation(
                        *releaseEvidence->mappingAttestation(),
                        operation.request.identity().scope(),
                        releaseSnapshot.sessionGeneration,
                        releaseEvidence->mappingAttestation()->epoch,
                        *operation.targetReference,
                        *operation.evidence)
                  : Utils::ResultError(
                        QStringLiteral(
                            "The active package changed before lease release."));
        const Utils::Result<Data::RuntimePackageActivationProjectCapture>
            persisted = m_projectService
                            ->captureRuntimePackageActivationProject(
                                operation.request.identity().scope().projectId);
        const bool projectStillExact
            = persisted && persisted->isValid()
              && operation.persistedDocumentRevision
              && persisted->documentRevision()
                     == *operation.persistedDocumentRevision
              && !persisted->snapshot().modified
              && persisted->originalBinding()
                     == operation.projectCommit->resultingBinding()
              && persisted->snapshot().masterBindingArtifact
                     == *operation.targetReference;
        if (!attestation || !projectStillExact) {
            operation.releaseOutcome = Outcome::OutcomeUnknown;
            operation.releaseUnknownCode
                = !attestation
                      ? QStringLiteral("controller-target-drifted")
                      : QStringLiteral("project-binding-drifted");
            operation.releaseUnknownDetail
                = !attestation
                      ? attestation.error()
                      : (persisted
                             ? QStringLiteral(
                                   "The project changed before the controller lease was "
                                   "released.")
                             : persisted.error());
        }
    }

    if (operation.afterController
        && operation.afterController->evidenceSha256()
               != releaseEvidence->evidenceSha256()) {
        operation.controllerEvidenceHistory.append(*releaseEvidence);
    } else {
        operation.afterController = *releaseEvidence;
    }
    operation.audit.append(localEvent(
        operation,
        AuditKind::ControllerEvidenceCaptured,
        operation.phase,
        operation.outcome,
        QStringLiteral("release-preflight-state"),
        QStringLiteral(
            "Captured authoritative controller state immediately before lease release."),
        releaseEvidence->evidenceSha256()));
    publish(operation);
    if (operation.phase != Phase::ReleasingControl
        || operation.outcome != Outcome::Pending) {
        return;
    }

    operation.releaseProgressBaseline = releaseSnapshot.controlProgress;
    operation.audit.append(providerEvent(
        operation,
        AuditKind::ProviderRequestSent,
        Action::ReleaseControl,
        operation.request.identity().operationId().value(),
        operation.releaseRequestId,
        {},
        {},
        {},
        QStringLiteral("release-request-sent"),
        QStringLiteral("Exclusive control lease release request sent.")));
    operation.releaseRequestSentAt = operation.audit.constLast().occurredAt();
    const Utils::Result<> requestGuard = persistGuard(operation);
    if (!requestGuard) {
        operation.audit.removeLast();
        operation.releaseRequestSentAt = {};
        freezeUnknown(
            operation,
            QStringLiteral("activation-journal-failed"),
            requestGuard.error());
        return;
    }

    Data::ControllerControlRequest request;
    request.command = Data::ControllerControlCommand::ReleaseControl;
    armProviderDeadline(
        operation,
        Phase::ReleasingControl,
        Action::ReleaseControl,
        QStringLiteral("release-timeout"),
        QStringLiteral(
            "Timed out while waiting for the terminal control-lease release response."));
    const Utils::Result<> result
        = operation.provider->executeControlCommand(request);
    if (operation.phase != Phase::ReleasingControl
        || operation.outcome != Outcome::Pending) {
        return;
    }
    if (!result) {
        freezeUnknown(
            operation,
            QStringLiteral("release-outcome-unknown"),
            result.error());
        return;
    }
    publish(operation);
}

void TrustedRuntimePackageActivationService::finish(
    Operation &operation,
    Outcome outcome,
    const QString &code,
    const QString &detail,
    std::optional<Data::RuntimePackageActivationSha256> evidence)
{
    if (operation.guardWritten) {
        const QString guardPath = journalFileName(
            m_journalRoot, operation.request.identity().operationId());
        if (QFile::exists(guardPath) && !QFile::remove(guardPath)) {
            if (!m_recoveryBarriers.contains(guardPath))
                m_recoveryBarriers.append(guardPath);
            operation.reconciliationInProgress = false;
            operation.recoveryReleaseInProgress = false;
            freezeUnknown(
                operation,
                QStringLiteral("activation-guard-delete-failed"),
                QStringLiteral(
                    "The durable activation guard could not be removed; the final outcome "
                    "requires local recovery."));
            return;
        }
        operation.guardWritten = false;
        m_recoveryBarriers.removeAll(guardPath);
    }
    operation.recovered = false;
    operation.reconciliationInProgress = false;
    operation.recoveryReleaseInProgress = false;
    operation.phase = Phase::Finished;
    operation.outcome = outcome;
    operation.detail = concise(detail);
    operation.completedAt = nextEventTime(operation.audit);
    operation.audit.append(localEvent(
        operation,
        AuditKind::Completed,
        operation.phase,
        operation.outcome,
        code,
        operation.detail,
        std::move(evidence),
        operation.completedAt));
    publish(operation);
}

void TrustedRuntimePackageActivationService::failBeforeControllerMutation(
    Operation &operation, const QString &code, const QString &detail)
{
    finish(
        operation,
        Outcome::FailedWithoutControllerChange,
        code,
        detail,
        operation.request.fingerprint());
}

void TrustedRuntimePackageActivationService::freezeUnknown(
    Operation &operation, const QString &code, const QString &detail)
{
    operation.phase = Phase::AwaitingReconciliation;
    operation.outcome = Outcome::OutcomeUnknown;
    operation.detail = concise(detail);
    operation.audit.append(localEvent(
        operation,
        AuditKind::PhaseTransition,
        operation.phase,
        operation.outcome,
        code,
        operation.detail));
    publish(operation);
}

void TrustedRuntimePackageActivationService::refreshRecord(
    Operation &operation)
{
    operation.detail = operation.audit.constLast().detail();
    operation.updatedAt = operation.audit.constLast().occurredAt();
    operation.record = {
        operation.request.identity(),
        operation.request.fingerprint(),
        operation.request.rollbackOnActivationFailure(),
        quint64(operation.audit.size()),
        operation.phase,
        operation.outcome,
        operation.audit,
        operation.detail,
        operation.startedAt,
        operation.updatedAt,
        operation.completedAt,
        operation.beforeController,
        operation.afterController,
        operation.projectCommit,
        operation.cancellation,
        operation.deploymentEvidence,
        operation.controllerEvidenceHistory,
    };
    QTC_CHECK(operation.record.isValid());
}

Utils::Result<> TrustedRuntimePackageActivationService::persistGuard(
    Operation &operation)
{
    refreshRecord(operation);
    const RuntimePackageActivationJournalState state{
        operation.request,
        operation.phase,
        outstandingProviderAction(operation.audit),
        operation.beforeController,
        operation.afterController,
        operation.controllerEvidenceHistory,
        operation.projectCommit,
        operation.persistedDocumentRevision,
        operation.record,
    };
    const Utils::Result<QByteArray> serialized
        = serializeRuntimePackageActivationJournal(state);
    if (!serialized)
        return Utils::ResultError(serialized.error());
    if (m_journalCommitShouldFail && m_journalCommitShouldFail()) {
        return Utils::ResultError(
            QStringLiteral("The activation journal commit was rejected."));
    }
    if (!QDir().mkpath(m_journalRoot)) {
        return Utils::ResultError(
            QStringLiteral("The activation journal directory could not be created."));
    }
    const QFileInfo root(m_journalRoot);
    if (!root.isDir() || root.isSymLink()) {
        return Utils::ResultError(
            QStringLiteral("The activation journal root is not a safe directory."));
    }
    const QString path = journalFileName(
        m_journalRoot, operation.request.identity().operationId());
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(*serialized) != serialized->size()
        || !file.commit()) {
        return Utils::ResultError(
            QStringLiteral("The activation journal could not be committed."));
    }
    operation.guardWritten = true;
    if (!m_recoveryBarriers.contains(path))
        m_recoveryBarriers.append(path);
    return Utils::ResultOk;
}

void TrustedRuntimePackageActivationService::publish(Operation &operation)
{
    refreshRecord(operation);
    if (operation.guardWritten) {
        const Utils::Result<> persisted = persistGuard(operation);
        if (!persisted && operation.outcome == Outcome::Pending) {
            operation.phase = Phase::AwaitingReconciliation;
            operation.outcome = Outcome::OutcomeUnknown;
            operation.detail = concise(persisted.error());
            operation.audit.append(localEvent(
                operation,
                AuditKind::PhaseTransition,
                operation.phase,
                operation.outcome,
                QStringLiteral("activation-journal-update-failed"),
                operation.detail));
            refreshRecord(operation);
            const QString path = journalFileName(
                m_journalRoot,
                operation.request.identity().operationId());
            if (!m_recoveryBarriers.contains(path))
                m_recoveryBarriers.append(path);
        }
    }
    ++m_snapshotSequence;
    emit recordChanged(operation.record);
    emit statusChanged(
        operation.request.identity().operationId(),
        operation.phase,
        operation.outcome);
    emit snapshotChanged(snapshot());
}

void TrustedRuntimePackageActivationService::discoverRecoveryBarriers()
{
    m_recoveryBarriers.clear();
    const QFileInfo root(m_journalRoot);
    if (!root.exists())
        return;
    if (!root.isDir() || root.isSymLink()) {
        m_recoveryBarriers.append(m_journalRoot);
        return;
    }
    const QFileInfoList entries = QDir(m_journalRoot).entryInfoList(
        {QStringLiteral("*") + QString::fromLatin1(journalSuffix)},
        QDir::Files | QDir::NoSymLinks,
        QDir::Name);
    for (const QFileInfo &entry : entries) {
        m_recoveryBarriers.append(entry.absoluteFilePath());
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        const Utils::Result<RuntimePackageActivationJournalState> state
            = parseRuntimePackageActivationJournal(bytes);
        if (!state)
            continue;
        const QString operationId
            = state->request.identity().operationId().value();
        if (QDir::cleanPath(entry.absoluteFilePath())
            != QDir::cleanPath(
                journalFileName(
                    m_journalRoot,
                    state->request.identity().operationId()))) {
            continue;
        }
        if (m_operations.contains(operationId))
            continue;

        auto operation = std::make_shared<Operation>();
        operation->request = state->request;
        operation->record = state->record;
        operation->audit = state->record.audit();
        operation->phase = state->phase;
        operation->outcome = state->record.outcome();
        operation->detail = state->record.detail();
        operation->startedAt = state->record.startedAt();
        operation->updatedAt = state->record.updatedAt();
        operation->completedAt = state->record.completedAt();
        operation->beforeController = state->beforeController;
        operation->afterController = state->afterController;
        operation->projectCommit = state->projectCommit;
        operation->cancellation = state->record.cancellation();
        operation->deploymentEvidence
            = state->record.deploymentEvidence();
        operation->controllerEvidenceHistory
            = state->controllerEvidenceHistory;
        operation->persistedDocumentRevision
            = state->persistedDocumentRevision;
        operation->existingPackage
            = operation->beforeController
              && operation->beforeController->matchesActivatedIdentity(
                  operation->request.identity())
              && !providerMutationRequestWasSent(operation->audit);
        operation->guardWritten = true;
        operation->recovered = true;
        if (operation->phase != Phase::AwaitingReconciliation
            || operation->outcome != Outcome::OutcomeUnknown) {
            operation->phase = Phase::AwaitingReconciliation;
            operation->outcome = Outcome::OutcomeUnknown;
            operation->detail = QStringLiteral(
                "Recovered a durable activation that requires authoritative reconciliation.");
            operation->audit.append(localEvent(
                *operation,
                AuditKind::PhaseTransition,
                operation->phase,
                operation->outcome,
                QStringLiteral("activation-recovered"),
                operation->detail));
            const Utils::Result<> normalized = persistGuard(*operation);
            QTC_CHECK(normalized);
        }
        m_operations.insert(operationId, operation);
        ++m_snapshotSequence;
    }
}

} // namespace EtherCAT::SemanticRuntime::Internal
