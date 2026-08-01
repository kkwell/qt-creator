// Copyright (C) 2026 Kvell

#include "ethercatcoretests.h"

#include "automationservice.h"
#include "ethercatcoreconstants.h"
#include "ethercatcoresettings.h"
#include "ethercatcoretr.h"
#include "manualcontrolcontract.h"
#include "providerregistry.h"
#include "providers.h"
#include "runtimepackageactivationservice.h"
#include "runtimepackagecompilercodec.h"
#include "runtimepackagecompilerpreparationcoordinator.h"
#include "runtimepackagecompilerprovider.h"
#include "selectionservice.h"
#include "semanticruntimeservice.h"
#include "stateservice.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/engineeringvalue.h>
#include <ethercatdata/manualcontrol.h>
#include <ethercatdata/nodeid.h>
#include <ethercatdata/offlineconfiguration.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/runtimepackageactivation.h>
#include <ethercatdata/runtimepackagecompiler.h>
#include <ethercatdata/runtimeoutputtransaction.h>
#include <ethercatdata/runtimeresource.h>
#include <ethercatdata/semanticmappingattestation.h>
#include <ethercatdata/semanticruntime.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <array>
#include <functional>
#include <future>
#include <limits>
#include <type_traits>

namespace EtherCAT::Core::Internal {

class TestAutomationService final : public AutomationService
{
public:
    using AutomationService::AutomationService;

    QList<AutomationContextSnapshot> contexts() const final
    {
        ++readCount;
        return snapshots;
    }

    mutable int readCount = 0;
    QList<AutomationContextSnapshot> snapshots;
};

class TestSemanticRuntimeService final : public SemanticRuntimeService
{
public:
    using SemanticRuntimeService::SemanticRuntimeService;

    QList<Data::SemanticRuntimeContext> contexts() const final
    {
        ++readCount;
        return snapshots;
    }

    mutable int readCount = 0;
    QList<Data::SemanticRuntimeContext> snapshots;
};

class TestRuntimePackageActivationService final : public RuntimePackageActivationService
{
public:
    using RuntimePackageActivationService::RuntimePackageActivationService;

    RuntimePackageActivationPreparationResult prepare(
        const RuntimePackageActivationPreparationRequest &request) final
    {
        if (!request.isValid()) {
            return {
                {},
                QStringLiteral("The activation preparation request is invalid."),
            };
        }
        return {
            {},
            QStringLiteral(
                "The Core contract test service has no production package verifier."),
        };
    }

    RuntimePackageActivationCommandResult start(
        const Data::RuntimePackageActivationRequest &request) final
    {
        if (!request.isValid()) {
            return {
                RuntimePackageActivationCommandDisposition::InvalidRequest,
                {},
                QStringLiteral("The activation request is invalid."),
            };
        }
        const auto operationId = request.identity().operationId();
        const auto existing = m_records.constFind(operationId);
        if (existing != m_records.cend()) {
            if (existing->requestFingerprint() == request.fingerprint()) {
                return {
                    RuntimePackageActivationCommandDisposition::IdempotentReplay,
                    *existing,
                    {},
                };
            }
            return {
                RuntimePackageActivationCommandDisposition::Conflict,
                *existing,
                QStringLiteral("The activation OperationId has a different request fingerprint."),
            };
        }
        const auto prepared = m_prepared.constFind(operationId);
        if (prepared == m_prepared.cend()
            || *prepared != request.fingerprint()) {
            return {
                RuntimePackageActivationCommandDisposition::InvalidRequest,
                {},
                QStringLiteral("The activation request was not prepared."),
            };
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const Data::RuntimePackageActivationAuditEvent event{
            1,
            Data::RuntimePackageActivationAuditKind::IntentPersisted,
            Data::RuntimePackageActivationPhase::Queued,
            Data::RuntimePackageActivationOutcome::Pending,
            Data::RuntimePackageActivationProviderAction::None,
            Data::RuntimePackageActivationProviderReconciliation::None,
            {},
            0,
            0,
            0,
            {},
            {},
            {},
            request.fingerprint(),
            QStringLiteral("queued"),
            QStringLiteral("Activation queued for offline contract testing."),
            now,
        };
        const Data::RuntimePackageActivationRecord created{
            request.identity(),
            request.fingerprint(),
            request.rollbackOnActivationFailure(),
            1,
            event.phase(),
            event.outcome(),
            {event},
            event.detail(),
            now,
            now,
        };
        m_prepared.remove(operationId);
        publish(created);
        return {
            RuntimePackageActivationCommandDisposition::Accepted,
            created,
            {},
        };
    }

    RuntimePackageActivationCommandResult reconcile(
        const Data::RuntimePackageActivationOperationId &operationId) final
    {
        const auto found = m_records.constFind(operationId);
        if (!operationId.isValid()) {
            return {
                RuntimePackageActivationCommandDisposition::InvalidRequest,
                {},
                QStringLiteral("The activation OperationId is invalid."),
            };
        }
        if (found == m_records.cend()) {
            return {
                RuntimePackageActivationCommandDisposition::NotFound,
                {},
                QStringLiteral("The activation operation does not exist."),
            };
        }
        if (found->phase() == Data::RuntimePackageActivationPhase::Reconciling
            || Data::runtimePackageActivationOutcomeIsTerminal(found->outcome())) {
            return {
                RuntimePackageActivationCommandDisposition::IdempotentReplay,
                *found,
                {},
            };
        }
        if (!found->needsReconciliation()) {
            return {
                RuntimePackageActivationCommandDisposition::NotAllowed,
                *found,
                QStringLiteral("The activation outcome does not require reconciliation."),
            };
        }
        return transition(
            *found,
            Data::RuntimePackageActivationPhase::Reconciling,
            Data::RuntimePackageActivationOutcome::OutcomeUnknown,
            Data::RuntimePackageActivationAuditKind::ReconciliationStarted,
            QStringLiteral("reconciling"),
            found->cancellation());
    }

    RuntimePackageActivationCommandResult cancel(
        const Data::RuntimePackageActivationCancelRequest &request) final
    {
        if (!request.isValid()) {
            return {
                RuntimePackageActivationCommandDisposition::InvalidRequest,
                {},
                QStringLiteral("The activation cancel request is invalid."),
            };
        }
        const auto found = m_records.constFind(request.operationId());
        if (found == m_records.cend()) {
            return {
                RuntimePackageActivationCommandDisposition::NotFound,
                {},
                QStringLiteral("The activation operation does not exist."),
            };
        }
        if (found->cancellation()
            && found->cancellation()->expectedRecordRevision()
                   == request.expectedRecordRevision()
            && found->cancellation()->expectedDocumentRevision()
                   == request.expectedDocumentRevision()
            && found->cancellation()->expectedBinding() == request.expectedBinding()) {
            return {
                RuntimePackageActivationCommandDisposition::IdempotentReplay,
                *found,
                {},
            };
        }
        if (found->needsReconciliation()) {
            return {
                RuntimePackageActivationCommandDisposition::ReconciliationRequired,
                *found,
                QStringLiteral("Reconcile the unknown controller outcome before continuing."),
            };
        }
        if (Data::runtimePackageActivationOutcomeIsTerminal(found->outcome())) {
            return {
                RuntimePackageActivationCommandDisposition::TooLate,
                *found,
                QStringLiteral("The activation operation is already complete."),
            };
        }
        if (request.expectedRecordRevision() != found->revision()
            || request.expectedDocumentRevision()
                   != found->identity().documentRevisionToken()
            || request.expectedBinding() != found->identity().originalBindingToken()) {
            return {
                RuntimePackageActivationCommandDisposition::StaleRevision,
                *found,
                QStringLiteral("The activation or project revision changed before cancellation."),
            };
        }
        if (!Data::runtimePackageActivationPhaseAllowsCancel(found->phase())) {
            return {
                RuntimePackageActivationCommandDisposition::TooLate,
                *found,
                QStringLiteral("Cancellation is too late for the current activation phase."),
            };
        }
        const Data::RuntimePackageActivationCancellation cancellation{
            request.expectedRecordRevision(),
            request.expectedDocumentRevision(),
            request.expectedBinding(),
            request.fingerprint(),
            found->phase(),
            Data::RuntimePackageActivationCancellationControllerResult::Pending,
            {},
            nextTimestamp(*found),
        };
        return transition(
            *found,
            Data::RuntimePackageActivationPhase::Canceling,
            Data::RuntimePackageActivationOutcome::Pending,
            Data::RuntimePackageActivationAuditKind::CancellationRequested,
            QStringLiteral("canceling"),
            cancellation);
    }

    std::optional<Data::RuntimePackageActivationRecord> record(
        const Data::RuntimePackageActivationOperationId &operationId) const final
    {
        const auto found = m_records.constFind(operationId);
        return found == m_records.cend() ? std::nullopt
                                        : std::optional(*found);
    }

    Data::RuntimePackageActivationSnapshot snapshot() const final
    {
        QDateTime capturedAt = QDateTime::currentDateTimeUtc();
        for (const Data::RuntimePackageActivationRecord &record : m_records) {
            if (capturedAt < record.updatedAt())
                capturedAt = record.updatedAt();
        }
        return {
            m_sequence ? m_sequence : 1,
            m_records.values(),
            capturedAt,
        };
    }

    void replaceForTest(const Data::RuntimePackageActivationRecord &record)
    {
        m_records.insert(record.identity().operationId(), record);
    }

    void authorizeForTest(
        const Data::RuntimePackageActivationRequest &request)
    {
        m_prepared.insert(
            request.identity().operationId(), request.fingerprint());
    }

private:
    static QDateTime nextTimestamp(const Data::RuntimePackageActivationRecord &record)
    {
        return std::max(
            QDateTime::currentDateTimeUtc(), record.updatedAt().addMSecs(1));
    }

    RuntimePackageActivationCommandResult transition(
        const Data::RuntimePackageActivationRecord &current,
        Data::RuntimePackageActivationPhase phase,
        Data::RuntimePackageActivationOutcome outcome,
        Data::RuntimePackageActivationAuditKind kind,
        const QString &code,
        std::optional<Data::RuntimePackageActivationCancellation> cancellation)
    {
        const QDateTime now = cancellation ? cancellation->requestedAt()
                                           : nextTimestamp(current);
        QList<Data::RuntimePackageActivationAuditEvent> audit = current.audit();
        const std::optional<Data::RuntimePackageActivationSha256> eventEvidence
            = kind == Data::RuntimePackageActivationAuditKind::CancellationRequested
                      && cancellation
                  ? std::optional(cancellation->requestFingerprint())
                  : std::nullopt;
        audit.append({
            audit.constLast().sequence() + 1,
            kind,
            phase,
            outcome,
            Data::RuntimePackageActivationProviderAction::None,
            Data::RuntimePackageActivationProviderReconciliation::None,
            {},
            0,
            0,
            0,
            {},
            {},
            {},
            eventEvidence,
            code,
            code,
            now,
        });
        const Data::RuntimePackageActivationRecord updated{
            current.identity(),
            current.requestFingerprint(),
            current.rollbackOnActivationFailure(),
            current.revision() + 1,
            phase,
            outcome,
            audit,
            code,
            current.startedAt(),
            now,
            {},
            current.beforeController(),
            current.afterController(),
            current.projectCommit(),
            std::move(cancellation),
            current.deploymentEvidence(),
            current.controllerEvidenceHistory(),
        };
        publish(updated);
        return {
            RuntimePackageActivationCommandDisposition::Accepted,
            updated,
            {},
        };
    }

    void publish(const Data::RuntimePackageActivationRecord &record)
    {
        m_records.insert(record.identity().operationId(), record);
        ++m_sequence;
        emit recordChanged(record);
        emit statusChanged(
            record.identity().operationId(), record.phase(), record.outcome());
        emit snapshotChanged(snapshot());
    }

    QHash<
        Data::RuntimePackageActivationOperationId,
        Data::RuntimePackageActivationRecord>
        m_records;
    QHash<
        Data::RuntimePackageActivationOperationId,
        Data::RuntimePackageActivationSha256>
        m_prepared;
    quint64 m_sequence = 0;
};

struct RuntimePackageCompilerFixture
{
    Data::NodeId projectId = Data::NodeId::fromString("11111111-1111-4111-8111-111111111111");
    Data::NodeId masterId = Data::NodeId::fromString("22222222-2222-4222-8222-222222222222");
    Data::NodeId slaveId = Data::NodeId::fromString("33333333-3333-4333-8333-333333333333");
    QByteArray esiBytes{"<EtherCATInfo/>"};
    QByteArray adapterBytes{"format: ethercat-device-adapter-v1\n"};
    QByteArray topologyJson;
    QByteArray targetJson;
    QByteArray adapterBundleJson;
    QByteArray controllerFeaturesJson{"{\"format\":\"controller-features-v1\"}\n"};
    QByteArray publicKey = QByteArray(32, '\x31');
    QByteArray signature = QByteArray(64, '\x32');
    Data::RuntimePackageCompilerCompileRequest request;

    static Data::RuntimePackageCompilerSha256 sha256(QByteArrayView bytes)
    {
        return Data::RuntimePackageCompilerSha256{
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
    }

    static Data::RuntimePackageCompilerCanonicalJson canonical(QByteArray exactBytes)
    {
        return Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(std::move(exactBytes));
    }

    static Data::RuntimePackageCompilerSourceArtifact artifact(
        Data::RuntimePackageCompilerSourceArtifactKind kind,
        const QString &relativePath,
        const QByteArray &bytes)
    {
        return {kind, relativePath, bytes, sha256(bytes)};
    }

    RuntimePackageCompilerFixture()
    {
        const auto adapterFileSha256 = sha256(adapterBytes);
        adapterBundleJson
            = QByteArray("{\"bundle_id\":\"embedlabs.fixture.adapters\","
                         "\"entries\":[{\"adapter_id\":\"solidot.xb6.fixture\","
                         "\"adapter_version\":\"1.0.0\",\"bytes\":")
              + QByteArray::number(adapterBytes.size())
              + QByteArray(",\"canonical_sha256\":\"")
              + adapterFileSha256.value().toHex()
              + QByteArray("\",\"file_sha256\":\"")
              + adapterFileSha256.value().toHex()
              + QByteArray("\",\"relative_path\":\"adapter_bundle/xb6.ecdev.yaml\"}],"
                           "\"format\":\"ethercat-lower-adapter-bundle-v1\","
                           "\"format_version\":1}\n");
        const auto adapterBundleSha256 = sha256(adapterBundleJson);
        const auto capabilityDescriptorSha256 = sha256("capability");
        const auto controllerFeaturesSha256 = sha256(controllerFeaturesJson);
        const auto signingKeyIdSha256 = sha256(publicKey);
        targetJson
            = QByteArray("{\"adapter_bundle_sha256\":\"")
              + adapterBundleSha256.value().toHex()
              + QByteArray("\",\"capability\":{\"fixture\":true},"
                           "\"capability_descriptor_sha256\":\"")
              + capabilityDescriptorSha256.value().toHex()
              + QByteArray("\",\"controller_features_sha256\":\"")
              + controllerFeaturesSha256.value().toHex()
              + QByteArray("\",\"cpu1_abi_version\":2,\"cycle_periods_ns\":[125000],"
                           "\"format\":\"ethercat-target-capability-profile-v1\","
                           "\"format_version\":1,\"fpga_abi_version\":1,"
                           "\"limits\":{\"max_config_package_bytes\":1048576,"
                           "\"max_cyclic_frames\":8,\"max_pdo_input_bytes\":4096,"
                           "\"max_pdo_output_bytes\":4096,"
                           "\"max_runtime_package_bytes\":1048576,"
                           "\"max_sdo_startup_ops\":1024,\"max_slaves\":64},"
                           "\"manifest_versions\":[2],\"policy_revision\":1,"
                           "\"profile_id\":\"embedlabs.zynq.fixture\","
                           "\"signing_key_id\":\"")
              + signingKeyIdSha256.value().toHex() + QByteArray("\"}\n");

        Data::ProjectSnapshot project;
        project.id = projectId;
        project.name = QStringLiteral("Compiler contract fixture");
        project.formatVersion = 7;
        project.valid = true;
        project.nodes = {
            {projectId, {}, Data::ProjectNodeKind::Project, QStringLiteral("Project")},
            {masterId, projectId, Data::ProjectNodeKind::Master, QStringLiteral("Master")},
            {slaveId, masterId, Data::ProjectNodeKind::Slave, QStringLiteral("Slave")},
        };
        project.masterConfiguration.timingMode = Data::MasterTimingMode::FreeRun;
        project.masterConfiguration.cyclePeriodNs = 125000;

        Data::OfflineSlaveConfiguration slave;
        slave.id = slaveId;
        slave.masterId = masterId;
        slave.position = 0;
        slave.identity = {0x00884443, 0x000000b6, 0x00000001};
        slave.name = QStringLiteral("XB6 fixture");
        slave.stationAddress = 0x1001;
        slave.esiSha256 = sha256(esiBytes).value();
        slave.adapterSelection.adapterId = {
            "org.embedlabs.adapter.solidot.xb6-fixture",
        };
        slave.adapterSelection.adapterVersion = QStringLiteral("0.3.0");
        slave.adapterSelection.adapterContentSha256
            = sha256("upper-v3-adapter-manifest").value();
        slave.adapterSelection.processDataProfileId
            = QStringLiteral("org.embedlabs.solidot.xb6.do16");
        Data::PdoEntryConfiguration pdoEntry;
        pdoEntry.id = Data::NodeId::create();
        pdoEntry.index = 0x7000;
        pdoEntry.subIndex = 1;
        pdoEntry.name = QStringLiteral("do0");
        pdoEntry.bitLength = 1;
        pdoEntry.dataType = Data::EtherCATDataType::Boolean;
        Data::PdoConfiguration pdo;
        pdo.id = Data::NodeId::create();
        pdo.index = 0x1600;
        pdo.name = QStringLiteral("do16");
        pdo.direction = Data::PdoDirection::Rx;
        pdo.syncManager = 2;
        pdo.selected = true;
        pdo.fixed = true;
        pdo.entries = {pdoEntry};
        slave.processData.pdos = {pdo};
        slave.dc.enabled = false;
        project.slaves = {slave};

        const Data::RuntimePackageActivationProjectCapture capture{
            project,
            QByteArray("{\"format\":\"embed-labs-ethercat-project\"}\n"),
            7,
            Data::RuntimePackageActivationDocumentRevisionToken{"revision-7"},
            Data::RuntimePackageActivationOriginalBindingToken{"binding-empty"},
        };

        Data::RuntimePackageCompilerFreshTopologyEvidence topology;
        topology.scope = {projectId, masterId};
        topology.sessionGeneration = 7;
        topology.sessionId = 9;
        topology.evidenceId = QStringLiteral("discover:fixture:sequence-57");
        topology.captureBootId = 0x4f536f408aafbc51;
        topology.captureSequence = 57;
        topology.capturedAtNs = 1'785'542'400'000'000'000ULL;
        topology.expiresAtNs = topology.capturedAtNs + 3'600'000'000'000ULL;
        topology.cyclePeriodNs = 125000;
        topology.linkSpeedMbps = 100;
        topology.slaves = {
            {slaveId,
             slave.position,
             slave.stationAddress,
             slave.alias,
             slave.identity,
             slave.serialNumber,
             slave.adapterSelection.moduleAssignments},
        };
        topologyJson
            = QByteArray("{\"capture_boot_id\":\"0x4f536f408aafbc51\","
                         "\"capture_sequence\":57,\"captured_at_ns\":")
              + QByteArray::number(topology.capturedAtNs)
              + QByteArray(",\"evidence_id\":\"discover:fixture:sequence-57\","
                           "\"expires_at_ns\":")
              + QByteArray::number(topology.expiresAtNs)
              + QByteArray(",\"format\":\"ethercat-discover-topology-evidence-v1\","
                           "\"format_version\":1,\"matched_scan\":{"
                           "\"cycle_period_ns\":125000,"
                           "\"format\":\"ethercat-esi-match-v1\","
                           "\"link_speed_mbps\":100,\"slaves\":[{\"alias\":0,"
                           "\"identity\":{\"product_code\":182,\"revision\":1,"
                           "\"vendor_id\":8930371},\"modules\":[],\"position\":0,"
                           "\"serial\":0,\"station_address\":4097}]}}\n");
        topology.canonicalEvidence = canonical(topologyJson);

        Data::RuntimePackageCompilerSourceArtifacts sources;
        sources.topologyEvidence = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::TopologyEvidence,
            QStringLiteral("topology-evidence-v1.json"),
            topologyJson);
        sources.targetProfile = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::TargetProfile,
            QStringLiteral("target-capability-profile-v1.json"),
            targetJson);
        sources.targetProfileSignature = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::TargetProfileSignature,
            QStringLiteral("target-capability-profile-v1.sig"),
            signature);
        sources.productionPublicKey = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::ProductionPublicKey,
            QStringLiteral("production.pub"),
            publicKey);
        sources.adapterBundle = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::AdapterBundle,
            QStringLiteral("adapter-bundle-v1.json"),
            adapterBundleJson);
        sources.policyTemplate = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::PolicyTemplate,
            QStringLiteral("policy-template.json"),
            QByteArray("{\"format\":\"policy-template-v1\"}\n"));
        sources.controllerFeatures = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::ControllerFeatures,
            QStringLiteral("controller-features-v1.json"),
            controllerFeaturesJson);
        sources.runtimeSource = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::RuntimeSource,
            QStringLiteral("runtime.st"),
            QByteArray("PROGRAM PLC_PRG\nEND_PROGRAM\n"));

        Data::RuntimePackageCompilerDeviceSourceEvidence deviceSource;
        deviceSource.projectSlaveNodeId = slaveId;
        deviceSource.originalEsi = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::OriginalEsi,
            QStringLiteral("esi/xb6.xml"),
            esiBytes);
        deviceSource.adapterSourceFile = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::AdapterSourceFile,
            QStringLiteral("adapter_bundle/xb6.ecdev.yaml"),
            adapterBytes);
        deviceSource.projectAdapterContractVersion
            = Data::DeviceAdapterContractVersion::V3;
        deviceSource.projectAdapterId = slave.adapterSelection.adapterId;
        deviceSource.projectAdapterVersion
            = slave.adapterSelection.adapterVersion;
        deviceSource.projectAdapterContentSha256
            = Data::RuntimePackageCompilerSha256{
                slave.adapterSelection.adapterContentSha256};
        deviceSource.projectControllerAdapterTarget = {
            QStringLiteral("solidot.xb6.fixture"),
            QStringLiteral("1.0.0"),
            adapterFileSha256.value(),
            sha256(esiBytes).value(),
        };
        deviceSource.projectPdoProfileId
            = slave.adapterSelection.processDataProfileId;
        deviceSource.projectSignedPdoProfileId = QStringLiteral("do16");
        deviceSource.adapterId = QStringLiteral("solidot.xb6.fixture");
        deviceSource.adapterVersion = QStringLiteral("1.0.0");
        deviceSource.adapterCanonicalSha256 = adapterFileSha256;
        deviceSource.pdoProfileId = QStringLiteral("do16");
        deviceSource.explicitNoDc = true;

        Data::RuntimePackageCompilerSignedTargetProfileEvidence target;
        target.profileId = QStringLiteral("embedlabs.zynq.fixture");
        target.policyRevision = 1;
        target.canonicalProfile = canonical(targetJson);
        target.capabilityDescriptorSha256 = capabilityDescriptorSha256;
        target.controllerFeaturesSha256 = controllerFeaturesSha256;
        target.adapterBundleSha256 = adapterBundleSha256;
        target.cpu1AbiVersion = 2;
        target.fpgaAbiVersion = 1;
        target.signature = signature;
        target.signingKeyIdSha256 = signingKeyIdSha256;
        target.productionSigned = true;

        Data::RuntimePackageCompilerDeviceProjection deviceProjection;
        deviceProjection.projectSlaveNodeId = slaveId;
        deviceProjection.slaveNodeId = QStringLiteral("ide:slave:xb6-fixture");
        deviceProjection.projectDeviceId = QStringLiteral("embedlabs:project:device:xb6-fixture");
        deviceProjection.position = slave.position;
        deviceProjection.stationAddress = slave.stationAddress;
        deviceProjection.alias = slave.alias;
        deviceProjection.identity = slave.identity;
        deviceProjection.serialNumber = slave.serialNumber;
        deviceProjection.esiSha256 = sha256(esiBytes);
        deviceProjection.targetProfileId = target.profileId;
        deviceProjection.adapterId = deviceSource.adapterId;
        deviceProjection.adapterVersion = deviceSource.adapterVersion;
        deviceProjection.adapterSha256 = deviceSource.adapterCanonicalSha256;
        deviceProjection.pdoProfileId = deviceSource.pdoProfileId;
        deviceProjection.pdoMappings = {
            {QStringLiteral("do16"),
             Data::RuntimePackageCompilerPdoDirection::Output,
             pdo.index,
             quint8(pdo.syncManager),
             pdo.fixed,
             {{QStringLiteral("do0"),
               pdoEntry.index,
               pdoEntry.subIndex,
               quint16(pdoEntry.bitLength),
               QStringLiteral("BOOL")}}},
        };
        deviceProjection.dc = {};
        deviceProjection.componentBindingIds
            .insert(QStringLiteral("0"), QStringLiteral("embedlabs:project:component:xb6-fixture"));
        deviceProjection.semanticBindingIds.insert(
            QStringLiteral("io.digital_output"),
            QStringLiteral("embedlabs:project:binding:xb6-fixture:do0"));
        deviceProjection.symbolMode = Data::RuntimePackageCompilerSymbolMode::ReportOnly;
        deviceProjection.manualEnvelope = {
            false,
            1,
            1,
            1,
            Data::RuntimePackageCompilerManualRecoveryAction::HoldSafe,
            Data::RuntimePackageCompilerManualRecoveryAction::HoldSafe,
            Data::RuntimePackageCompilerManualRecoveryAction::HoldSafe,
        };

        Data::RuntimePackageCompilerProjectProjection projectProjection;
        projectProjection.projectNodeId = projectId;
        projectProjection.masterProjectNodeId = masterId;
        projectProjection.projectId = QStringLiteral("embedlabs:project:compiler-fixture");
        projectProjection.masterNodeId = QStringLiteral("ide:master:compiler-fixture");
        projectProjection.documentRevision = 7;
        projectProjection.timingMode = Data::MasterTimingMode::FreeRun;
        projectProjection.cyclePeriodNs = 125000;
        projectProjection.linkSpeedMbps = 100;
        projectProjection.devices = {deviceProjection};
        projectProjection.uiMetadata = canonical(QByteArray("{}\n"));

        request.operationId = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000001")};
        request.intentId = QStringLiteral("embedlabs:compile-intent:test");
        request.configurationId = 4201;
        request.buildTimestampNs = topology.capturedAtNs;
        request.compileTimeNs = topology.capturedAtNs + 1'000'000'000ULL;
        request.contractIdentity = {
            QStringLiteral("ethercat-ide-project-compiler-contract-v1"),
            1,
            sha256("api042-schema-bundle"),
        };
        request.projectSnapshotEvidence
            = Data::RuntimePackageCompilerProjectSnapshotEvidence{capture};
        request.projectProjection = projectProjection;
        request.topologyEvidence = topology;
        request.sourceArtifacts = sources;
        request.deviceSourceEvidence = {deviceSource};
        request.targetProfile = target;
    }
};

static QByteArray readExactFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

static QByteArray api042TestData(const QString &fileName)
{
    const QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    return readExactFile(sourceDir.filePath(QStringLiteral("testdata/api042/") + fileName));
}

static QByteArray api042RepositoryData(const QString &relativePath)
{
    const QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    return readExactFile(sourceDir.filePath(QStringLiteral("../../../") + relativePath));
}

static Data::RuntimePackageCompilerSha256 api042Sha(const QString &hex)
{
    return Data::RuntimePackageCompilerSha256{QByteArray::fromHex(hex.toLatin1())};
}

static Data::EtherCATDataType api042DataType(const QString &name)
{
    static const QHash<QString, Data::EtherCATDataType> types{
        {QStringLiteral("BOOL"), Data::EtherCATDataType::Boolean},
        {QStringLiteral("SINT"), Data::EtherCATDataType::Integer8},
        {QStringLiteral("USINT"), Data::EtherCATDataType::UnsignedInteger8},
        {QStringLiteral("INT"), Data::EtherCATDataType::Integer16},
        {QStringLiteral("UINT"), Data::EtherCATDataType::UnsignedInteger16},
        {QStringLiteral("DINT"), Data::EtherCATDataType::Integer32},
        {QStringLiteral("UDINT"), Data::EtherCATDataType::UnsignedInteger32},
        {QStringLiteral("LINT"), Data::EtherCATDataType::Integer64},
        {QStringLiteral("ULINT"), Data::EtherCATDataType::UnsignedInteger64},
    };
    return types.value(name, Data::EtherCATDataType::Unknown);
}

static Data::RuntimePackageCompilerStartupStage api042StartupStage(const QString &name)
{
    if (name == QStringLiteral("preop"))
        return Data::RuntimePackageCompilerStartupStage::PreOperational;
    if (name == QStringLiteral("safeop"))
        return Data::RuntimePackageCompilerStartupStage::SafeOperational;
    if (name == QStringLiteral("op"))
        return Data::RuntimePackageCompilerStartupStage::Operational;
    return Data::RuntimePackageCompilerStartupStage::Unknown;
}

static Data::RuntimePackageCompilerStartupFailureAction api042FailureAction(const QString &name)
{
    if (name == QStringLiteral("abort"))
        return Data::RuntimePackageCompilerStartupFailureAction::Abort;
    if (name == QStringLiteral("warn"))
        return Data::RuntimePackageCompilerStartupFailureAction::Warn;
    if (name == QStringLiteral("continue"))
        return Data::RuntimePackageCompilerStartupFailureAction::Continue;
    return Data::RuntimePackageCompilerStartupFailureAction::Unknown;
}

static Data::RuntimePackageCompilerManualRecoveryAction api042RecoveryAction(const QString &name)
{
    if (name == QStringLiteral("hold_safe"))
        return Data::RuntimePackageCompilerManualRecoveryAction::HoldSafe;
    if (name == QStringLiteral("return_to_task"))
        return Data::RuntimePackageCompilerManualRecoveryAction::ReturnToTask;
    if (name == QStringLiteral("stop"))
        return Data::RuntimePackageCompilerManualRecoveryAction::Stop;
    return Data::RuntimePackageCompilerManualRecoveryAction::Unknown;
}

static QMap<QString, QString> api042StringMap(const QJsonObject &object)
{
    QMap<QString, QString> result;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        result.insert(it.key(), it.value().toString());
    return result;
}

static QByteArray api042StartupBytes(qint64 value, quint8 valueBytes)
{
    QByteArray result(valueBytes, '\0');
    quint64 bits = quint64(value);
    for (qsizetype index = result.size(); index > 0; --index) {
        result[index - 1] = char(bits & 0xff);
        bits >>= 8;
    }
    return result;
}

static Data::RuntimePackageCompilerSourceArtifact api042Artifact(
    Data::RuntimePackageCompilerSourceArtifactKind kind,
    const QString &relativePath,
    const QByteArray &bytes)
{
    return {kind, relativePath, bytes, RuntimePackageCompilerFixture::sha256(bytes)};
}

static Data::RuntimePackageCompilerCompileRequest api042GoldenCompileRequest()
{
    const QByteArray requestBytes = api042TestData(QStringLiteral("compile-request.json"));
    const QJsonObject requestObject = QJsonDocument::fromJson(requestBytes).object();
    const QJsonObject projectObject
        = requestObject.value(QStringLiteral("project_snapshot")).toObject();
    const QJsonObject masterObject = projectObject.value(QStringLiteral("master")).toObject();
    const QJsonArray deviceObjects = projectObject.value(QStringLiteral("devices")).toArray();

    const Data::NodeId projectNodeId = Data::NodeId::fromString(
        QStringLiteral("11111111-1111-4111-8111-111111111111"));
    const Data::NodeId masterNodeId = Data::NodeId::fromString(
        QStringLiteral("22222222-2222-4222-8222-222222222222"));
    const QList<Data::NodeId> slaveNodeIds{
        Data::NodeId::fromString(QStringLiteral("33333333-3333-4333-8333-333333333333")),
        Data::NodeId::fromString(QStringLiteral("44444444-4444-4444-8444-444444444444")),
        Data::NodeId::fromString(QStringLiteral("55555555-5555-4555-8555-555555555555")),
    };

    Data::ProjectSnapshot project;
    project.id = projectNodeId;
    project.name = QStringLiteral("API-042 golden project");
    project.formatVersion = 7;
    project.valid = true;
    project.masterConfiguration.timingMode = Data::MasterTimingMode::DistributedClocks;
    project.masterConfiguration.cyclePeriodNs = quint32(
        masterObject.value(QStringLiteral("cycle_period_ns")).toInteger());
    project.nodes = {
        {projectNodeId, {}, Data::ProjectNodeKind::Project, QStringLiteral("Project")},
        {masterNodeId, projectNodeId, Data::ProjectNodeKind::Master, QStringLiteral("Master")},
    };

    Data::RuntimePackageCompilerProjectProjection projectProjection;
    projectProjection.projectNodeId = projectNodeId;
    projectProjection.masterProjectNodeId = masterNodeId;
    projectProjection.projectId = projectObject.value(QStringLiteral("project_id")).toString();
    projectProjection.masterNodeId
        = projectObject.value(QStringLiteral("master_node_id")).toString();
    projectProjection.documentRevision = quint64(
        projectObject.value(QStringLiteral("document_revision")).toInteger());
    projectProjection.timingMode = Data::MasterTimingMode::DistributedClocks;
    projectProjection.cyclePeriodNs = project.masterConfiguration.cyclePeriodNs;
    projectProjection.linkSpeedMbps = quint32(
        masterObject.value(QStringLiteral("link_speed_mbps")).toInteger());
    projectProjection.uiMetadata = RuntimePackageCompilerFixture::canonical(QByteArray(
        "{\"canvas_zoom_percent\":125,\"display_name\":\"Excluded from execution intent\"}\n"));

    QList<Data::RuntimePackageCompilerDeviceSourceEvidence> deviceSources;
    QList<Data::RuntimePackageCompilerTopologySlaveEvidence> topologySlaves;
    const QString targetProfileId = QStringLiteral(
        "embedlabs.zynq.cpu1v2-fpgav1-capability-20260730");
    for (qsizetype deviceIndex = 0; deviceIndex < deviceObjects.size(); ++deviceIndex) {
        const QJsonObject object = deviceObjects.at(deviceIndex).toObject();
        const QJsonObject identityObject = object.value(QStringLiteral("identity")).toObject();
        const QJsonObject targetObject
            = object.value(QStringLiteral("controller_adapter_target")).toObject();
        const QJsonObject pdoObject = object.value(QStringLiteral("pdo")).toObject();
        const QJsonObject dcObject = object.value(QStringLiteral("dc")).toObject();
        const QJsonObject manualObject = object.value(QStringLiteral("manual_envelope")).toObject();

        Data::RuntimePackageCompilerDeviceProjection projection;
        projection.projectSlaveNodeId = slaveNodeIds.at(deviceIndex);
        projection.slaveNodeId = object.value(QStringLiteral("slave_node_id")).toString();
        projection.projectDeviceId = object.value(QStringLiteral("project_device_id")).toString();
        projection.position = int(object.value(QStringLiteral("position")).toInteger());
        projection.stationAddress = quint16(
            object.value(QStringLiteral("station_address")).toInteger());
        projection.alias = quint16(object.value(QStringLiteral("alias")).toInteger());
        projection.identity = {
            quint32(identityObject.value(QStringLiteral("vendor_id")).toInteger()),
            quint32(identityObject.value(QStringLiteral("product_code")).toInteger()),
            quint32(identityObject.value(QStringLiteral("revision")).toInteger()),
        };
        projection.serialNumber = quint32(
            identityObject.value(QStringLiteral("serial")).toInteger());
        projection.esiSha256 = api042Sha(object.value(QStringLiteral("esi_sha256")).toString());
        projection.targetProfileId
            = targetObject.value(QStringLiteral("target_profile_id")).toString();
        projection.adapterId = targetObject.value(QStringLiteral("adapter_id")).toString();
        projection.adapterVersion = targetObject.value(QStringLiteral("adapter_version")).toString();
        projection.adapterSha256 = api042Sha(
            targetObject.value(QStringLiteral("adapter_sha256")).toString());
        projection.pdoProfileId = targetObject.value(QStringLiteral("pdo_profile_id")).toString();
        if (!targetObject.value(QStringLiteral("signed_dc_profile_id")).isNull()) {
            projection.signedDcProfileId
                = targetObject.value(QStringLiteral("signed_dc_profile_id")).toString();
        }

        Data::OfflineSlaveConfiguration slave;
        slave.id = projection.projectSlaveNodeId;
        slave.masterId = masterNodeId;
        slave.position = projection.position;
        slave.identity = projection.identity;
        slave.serialNumber = projection.serialNumber;
        slave.alias = projection.alias;
        slave.name = projection.slaveNodeId;
        slave.stationAddress = projection.stationAddress;
        slave.esiSha256 = projection.esiSha256.value();
        slave.manualControlEnvelope.enabled = manualObject.value(QStringLiteral("enabled")).toBool();

        const QJsonArray mappings = pdoObject.value(QStringLiteral("mappings")).toArray();
        for (const QJsonValue &mappingValue : mappings) {
            const QJsonObject mappingObject = mappingValue.toObject();
            Data::RuntimePackageCompilerPdoMapping mapping;
            mapping.id = mappingObject.value(QStringLiteral("id")).toString();
            mapping.direction = mappingObject.value(QStringLiteral("direction")).toString()
                                        == QStringLiteral("input")
                                    ? Data::RuntimePackageCompilerPdoDirection::Input
                                    : Data::RuntimePackageCompilerPdoDirection::Output;
            mapping.pdoIndex = quint16(mappingObject.value(QStringLiteral("pdo_index")).toInteger());
            mapping.syncManager = quint8(mappingObject.value(QStringLiteral("sm")).toInteger());
            mapping.fixed = mappingObject.value(QStringLiteral("fixed")).toBool();

            Data::PdoConfiguration pdo;
            pdo.id = Data::NodeId::create();
            pdo.index = mapping.pdoIndex;
            pdo.name = mapping.id;
            pdo.direction = mapping.direction == Data::RuntimePackageCompilerPdoDirection::Input
                                ? Data::PdoDirection::Tx
                                : Data::PdoDirection::Rx;
            pdo.syncManager = mapping.syncManager;
            pdo.selected = true;
            pdo.fixed = mapping.fixed;
            for (const QJsonValue &entryValue :
                 mappingObject.value(QStringLiteral("entries")).toArray()) {
                const QJsonObject entryObject = entryValue.toObject();
                Data::RuntimePackageCompilerPdoEntry entry;
                entry.fieldId = entryObject.value(QStringLiteral("field_id")).toString();
                entry.index = quint16(entryObject.value(QStringLiteral("index")).toInteger());
                entry.subIndex = quint8(entryObject.value(QStringLiteral("subindex")).toInteger());
                entry.bitLength = quint16(
                    entryObject.value(QStringLiteral("bit_length")).toInteger());
                entry.dataType = entryObject.value(QStringLiteral("data_type")).toString();
                mapping.entries.append(entry);

                Data::PdoEntryConfiguration pdoEntry;
                pdoEntry.id = Data::NodeId::create();
                pdoEntry.index = entry.index;
                pdoEntry.subIndex = entry.subIndex;
                pdoEntry.name = entry.fieldId;
                pdoEntry.bitLength = entry.bitLength;
                pdoEntry.dataType = api042DataType(entry.dataType);
                if (pdoEntry.dataType == Data::EtherCATDataType::Unknown)
                    pdoEntry.rawDataType = entry.dataType;
                pdo.entries.append(pdoEntry);
            }
            projection.pdoMappings.append(mapping);
            slave.processData.pdos.append(pdo);
        }

        for (const QJsonValue &startupValue :
             object.value(QStringLiteral("startup_sdos")).toArray()) {
            const QJsonObject startupObject = startupValue.toObject();
            Data::RuntimePackageCompilerStartupSdo startup;
            startup.sequence = quint16(startupObject.value(QStringLiteral("sequence")).toInteger());
            startup.id = startupObject.value(QStringLiteral("id")).toString();
            startup.enabled = startupObject.value(QStringLiteral("enabled")).toBool();
            startup.stage = api042StartupStage(
                startupObject.value(QStringLiteral("stage")).toString());
            startup.index = quint16(startupObject.value(QStringLiteral("index")).toInteger());
            startup.subIndex = quint8(startupObject.value(QStringLiteral("subindex")).toInteger());
            const qint64 signedValue = startupObject.value(QStringLiteral("value")).toInteger();
            startup.value = signedValue;
            startup.valueBytes = quint8(
                startupObject.value(QStringLiteral("value_bytes")).toInteger());
            startup.completeAccess = startupObject.value(QStringLiteral("complete_access")).toBool();
            startup.timeoutNs = quint64(
                startupObject.value(QStringLiteral("timeout_ns")).toInteger());
            startup.retryCount = quint8(
                startupObject.value(QStringLiteral("retry_count")).toInteger());
            startup.failureAction = api042FailureAction(
                startupObject.value(QStringLiteral("failure_action")).toString());
            startup.persistent = startupObject.value(QStringLiteral("persistent")).toBool();
            startup.requiresPowerCycle
                = startupObject.value(QStringLiteral("requires_power_cycle")).toBool();
            projection.startupSdos.append(startup);

            Data::StartupParameterConfiguration parameter;
            parameter.id = Data::NodeId::create();
            parameter.enabled = startup.enabled;
            parameter.order = startup.sequence;
            parameter.transition = startupObject.value(QStringLiteral("stage")).toString();
            parameter.index = startup.index;
            parameter.subIndex = startup.subIndex;
            parameter.rawValue = api042StartupBytes(signedValue, startup.valueBytes);
            slave.startup.parameters.append(parameter);
        }

        projection.dc.enabled = dcObject.value(QStringLiteral("enabled")).toBool();
        if (!dcObject.value(QStringLiteral("signed_dc_profile_id")).isNull()) {
            projection.dc.signedDcProfileId
                = dcObject.value(QStringLiteral("signed_dc_profile_id")).toString();
        }
        if (!dcObject.value(QStringLiteral("mode")).isNull())
            projection.dc.mode = dcObject.value(QStringLiteral("mode")).toString();
        projection.dc.assignActivate = quint16(
            dcObject.value(QStringLiteral("assign_activate")).toInteger());
        projection.dc.sync0CycleNs = quint64(
            dcObject.value(QStringLiteral("sync0_cycle_ns")).toInteger());
        projection.dc.sync0ShiftNs = dcObject.value(QStringLiteral("sync0_shift_ns")).toInteger();
        projection.dc.sync1CycleNs = quint64(
            dcObject.value(QStringLiteral("sync1_cycle_ns")).toInteger());
        projection.dc.sync1ShiftNs = dcObject.value(QStringLiteral("sync1_shift_ns")).toInteger();
        projection.dc.referenceClock = dcObject.value(QStringLiteral("reference_clock")).toBool();
        slave.dc.enabled = projection.dc.enabled;
        slave.dc.modeName = projection.dc.mode.value_or(QString());
        slave.dc.assignActivate = projection.dc.assignActivate;
        slave.dc.sync0 = {
            projection.dc.sync0CycleNs != 0,
            qint64(projection.dc.sync0CycleNs),
            projection.dc.sync0ShiftNs,
        };
        slave.dc.sync1 = {
            projection.dc.sync1CycleNs != 0,
            qint64(projection.dc.sync1CycleNs),
            projection.dc.sync1ShiftNs,
        };
        slave.dc.potentialReferenceClock = projection.dc.referenceClock;

        for (const QJsonValue &moduleValue :
             object.value(QStringLiteral("module_assignments")).toArray()) {
            const QJsonObject moduleObject = moduleValue.toObject();
            projection.moduleAssignments.append(
                {int(moduleObject.value(QStringLiteral("slot")).toInteger()),
                 quint32(moduleObject.value(QStringLiteral("module_ident")).toInteger()),
                 0,
                 0});
        }
        slave.adapterSelection.moduleAssignments = projection.moduleAssignments;
        projection.componentBindingIds = api042StringMap(
            object.value(QStringLiteral("component_binding_ids")).toObject());
        projection.semanticBindingIds = api042StringMap(
            object.value(QStringLiteral("semantic_binding_ids")).toObject());
        projection.semanticActionBindingIds = api042StringMap(
            object.value(QStringLiteral("semantic_action_binding_ids")).toObject());
        const QString symbolMode = object.value(QStringLiteral("symbol_mode")).toString();
        projection.symbolMode = symbolMode == QStringLiteral("requested")
                                    ? Data::RuntimePackageCompilerSymbolMode::Requested
                                : symbolMode == QStringLiteral("all")
                                    ? Data::RuntimePackageCompilerSymbolMode::All
                                    : Data::RuntimePackageCompilerSymbolMode::ReportOnly;
        projection.symbols = api042StringMap(object.value(QStringLiteral("symbols")).toObject());
        projection.manualEnvelope = {
            manualObject.value(QStringLiteral("enabled")).toBool(),
            quint16(manualObject.value(QStringLiteral("max_ttl_cycles")).toInteger()),
            quint16(manualObject.value(QStringLiteral("refresh_cycles")).toInteger()),
            quint16(manualObject.value(QStringLiteral("max_hold_cycles")).toInteger()),
            api042RecoveryAction(manualObject.value(QStringLiteral("timeout_action")).toString()),
            api042RecoveryAction(manualObject.value(QStringLiteral("release_action")).toString()),
            api042RecoveryAction(manualObject.value(QStringLiteral("failure_action")).toString()),
        };

        const QByteArray upperAdapterIdentity = QByteArray("api042-upper:")
                                                + projection.projectDeviceId.toUtf8();
        slave.adapterSelection.adapterId = {
            QStringLiteral("org.embedlabs.adapter.api042.%1").arg(deviceIndex),
        };
        slave.adapterSelection.adapterVersion = QStringLiteral("1.0.0");
        slave.adapterSelection.adapterContentSha256
            = RuntimePackageCompilerFixture::sha256(upperAdapterIdentity).value();
        slave.adapterSelection.processDataProfileId
            = QStringLiteral("api042.project.profile.%1").arg(deviceIndex);

        const bool isXb6 = deviceIndex == 0;
        const QByteArray esiBytes = api042RepositoryData(
            isXb6 ? QStringLiteral(
                        "share/qtcreator/ethercat/esi/"
                        "EcatTerminal-XB6_V3.22_ENUM.xml")
                  : QStringLiteral(
                        "share/qtcreator/ethercat/esi/"
                        "INOVANCE_SV630N_1Axis_V16.xml"));
        const QByteArray adapterBytes = api042TestData(
            isXb6 ? QStringLiteral("xb6.ecdev.yaml") : QStringLiteral("sv630n.ecdev.yaml"));
        const QString adapterRelativePath
            = isXb6 ? QStringLiteral("adapter_bundle/xb6_ec0002_rev1_do16_manual.ecdev.yaml")
                    : QStringLiteral("adapter_bundle/sv630n_1axis_rev00010000_manual.ecdev.yaml");
        Data::RuntimePackageCompilerDeviceSourceEvidence source;
        source.projectSlaveNodeId = projection.projectSlaveNodeId;
        source.originalEsi = api042Artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::OriginalEsi,
            isXb6 ? QStringLiteral("esi/EcatTerminal-XB6_V3.22_ENUM.xml")
                  : QStringLiteral("esi/INOVANCE_SV630N_1Axis_V16.xml"),
            esiBytes);
        source.adapterSourceFile = api042Artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::AdapterSourceFile,
            adapterRelativePath,
            adapterBytes);
        source.projectAdapterContractVersion = Data::DeviceAdapterContractVersion::V3;
        source.projectAdapterId = slave.adapterSelection.adapterId;
        source.projectAdapterVersion = slave.adapterSelection.adapterVersion;
        source.projectAdapterContentSha256 = Data::RuntimePackageCompilerSha256{
            slave.adapterSelection.adapterContentSha256};
        source.projectControllerAdapterTarget = {
            projection.adapterId,
            projection.adapterVersion,
            projection.adapterSha256.value(),
            projection.esiSha256.value(),
        };
        source.projectPdoProfileId = slave.adapterSelection.processDataProfileId;
        source.projectSignedPdoProfileId = projection.pdoProfileId;
        source.projectSignedDcProfileId = projection.signedDcProfileId.value_or(QString());
        source.adapterId = projection.adapterId;
        source.adapterVersion = projection.adapterVersion;
        source.adapterCanonicalSha256 = projection.adapterSha256;
        source.pdoProfileId = projection.pdoProfileId;
        source.signedDcProfileId = projection.signedDcProfileId;
        source.explicitNoDc = !projection.dc.enabled;

        project.nodes.append({slave.id, masterNodeId, Data::ProjectNodeKind::Slave, slave.name});
        project.slaves.append(slave);
        projectProjection.devices.append(projection);
        deviceSources.append(source);
        topologySlaves.append(
            {projection.projectSlaveNodeId,
             projection.position,
             projection.stationAddress,
             projection.alias,
             projection.identity,
             projection.serialNumber,
             projection.moduleAssignments});
    }

    const QByteArray topologyBytes = api042TestData(QStringLiteral("topology-evidence.json"));
    Data::RuntimePackageCompilerFreshTopologyEvidence topology;
    topology.scope = {projectNodeId, masterNodeId};
    topology.sessionGeneration = 1;
    topology.sessionId = 1;
    topology.evidenceId = QStringLiteral("discover:boot-4f536f408aafbc51:sequence-00000057");
    topology.captureBootId = 0x4f536f408aafbc51ULL;
    topology.captureSequence = 57;
    topology.capturedAtNs = 1'785'542'400'000'000'000ULL;
    topology.expiresAtNs = 1'785'546'000'000'000'000ULL;
    topology.cyclePeriodNs = projectProjection.cyclePeriodNs;
    topology.linkSpeedMbps = projectProjection.linkSpeedMbps;
    topology.slaves = topologySlaves;
    topology.canonicalEvidence = RuntimePackageCompilerFixture::canonical(topologyBytes);

    const QByteArray targetBytes = api042TestData(QStringLiteral("target-profile.json"));
    const QJsonObject targetObject = QJsonDocument::fromJson(targetBytes).object();
    const QByteArray signature = QByteArray::fromHex(
        "ccf21cb63e4b8c392046fb1deb4996de6dd84281cbcfd9e33632c12b904ae9b7"
        "1cfa5869414bc29412b852e88983fc829f9d086d5376e1d4ed82d7f90980c20e");
    const QByteArray publicKey = QByteArray::fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f36d3d87986b77f19d056a7da4845f7063");
    Data::RuntimePackageCompilerSourceArtifacts sources;
    sources.topologyEvidence = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::TopologyEvidence,
        QStringLiteral("topology-evidence-v1.json"),
        topologyBytes);
    sources.targetProfile = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::TargetProfile,
        QStringLiteral("target-capability-profile-v1.json"),
        targetBytes);
    sources.targetProfileSignature = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::TargetProfileSignature,
        QStringLiteral("target-capability-profile-v1.sig"),
        signature);
    sources.productionPublicKey = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::ProductionPublicKey,
        QStringLiteral("production.pub"),
        publicKey);
    sources.adapterBundle = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::AdapterBundle,
        QStringLiteral("adapter-bundle-v1.json"),
        api042TestData(QStringLiteral("adapter-bundle.json")));
    sources.policyTemplate = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::PolicyTemplate,
        QStringLiteral("policy-template.json"),
        api042TestData(QStringLiteral("policy-template.json")));
    sources.controllerFeatures = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::ControllerFeatures,
        QStringLiteral("controller-features-v1.json"),
        api042TestData(QStringLiteral("controller-features.json")));
    sources.runtimeSource = api042Artifact(
        Data::RuntimePackageCompilerSourceArtifactKind::RuntimeSource,
        QStringLiteral("runtime.st"),
        api042TestData(QStringLiteral("runtime.st")));

    Data::RuntimePackageCompilerSignedTargetProfileEvidence target;
    target.profileId = targetProfileId;
    target.policyRevision = quint64(
        targetObject.value(QStringLiteral("policy_revision")).toInteger());
    target.canonicalProfile = RuntimePackageCompilerFixture::canonical(targetBytes);
    target.capabilityDescriptorSha256 = api042Sha(
        targetObject.value(QStringLiteral("capability_descriptor_sha256")).toString());
    target.controllerFeaturesSha256 = api042Sha(
        targetObject.value(QStringLiteral("controller_features_sha256")).toString());
    target.adapterBundleSha256 = api042Sha(
        targetObject.value(QStringLiteral("adapter_bundle_sha256")).toString());
    target.cpu1AbiVersion = quint32(
        targetObject.value(QStringLiteral("cpu1_abi_version")).toInteger());
    target.fpgaAbiVersion = quint32(
        targetObject.value(QStringLiteral("fpga_abi_version")).toInteger());
    target.signature = signature;
    target.signingKeyIdSha256 = api042Sha(
        targetObject.value(QStringLiteral("signing_key_id")).toString());
    target.productionSigned = true;

    const Data::RuntimePackageActivationProjectCapture capture{
        project,
        QByteArray("{\"format\":\"embed-labs-ethercat-project\"}\n"),
        projectProjection.documentRevision,
        Data::RuntimePackageActivationDocumentRevisionToken{"api042-revision-7"},
        Data::RuntimePackageActivationOriginalBindingToken{"api042-binding-empty"},
    };
    Data::RuntimePackageCompilerCompileRequest request;
    request.operationId = Data::RuntimePackageCompilerOperationId{
        requestObject.value(QStringLiteral("operation_id")).toString()};
    request.intentId = requestObject.value(QStringLiteral("intent_id")).toString();
    request.configurationId = 4201;
    request.buildTimestampNs = topology.capturedAtNs;
    request.compileTimeNs = topology.capturedAtNs + 1'000'000'000ULL;
    request.manifestFormatVersion = 2;
    request.contractIdentity = {
        QStringLiteral("ethercat-ide-project-compiler-contract-v1"),
        1,
        RuntimePackageCompilerFixture::sha256("api042-schema-bundle"),
    };
    request.projectSnapshotEvidence = Data::RuntimePackageCompilerProjectSnapshotEvidence{capture};
    request.projectProjection = projectProjection;
    request.topologyEvidence = topology;
    request.sourceArtifacts = sources;
    request.deviceSourceEvidence = deviceSources;
    request.targetProfile = target;
    return request;
}

static Data::RuntimePackageCompilerDiagnostic compilerDiagnostic(
    Data::RuntimePackageCompilerDiagnosticCategory category,
    const QString &code,
    const QString &stage = QStringLiteral("input"))
{
    const QByteArray canonicalDiagnostic
        = QStringLiteral("{\"code\":\"%1\",\"format\":\"ethercat-ide-compiler-diagnostic-v1\","
                         "\"message\":\"Compiler contract fixture diagnostic.\",\"path\":\"$\","
                         "\"retryable\":false,\"severity\":\"error\",\"stage\":\"%2\"}\n")
              .arg(code, stage)
              .toLatin1();
    return {
        category,
        Data::RuntimePackageCompilerDiagnosticSeverity::Error,
        stage,
        code,
        QStringLiteral("$"),
        QStringLiteral("Compiler contract fixture diagnostic."),
        false,
        RuntimePackageCompilerFixture::canonical(canonicalDiagnostic),
    };
}

static Data::RuntimePackageCompilerResultEnvelope compilerEnvelope(
    Data::RuntimePackageCompilerCommand command,
    Data::RuntimePackageCompilerResultStatus status,
    const QString &backendStatus,
    const Data::RuntimePackageCompilerOperationId &operationId,
    quint64 configurationId,
    const Data::RuntimePackageCompilerSha256 &requestSha256,
    QList<Data::RuntimePackageCompilerDiagnostic> diagnostics = {},
    std::optional<Data::RuntimePackageCompilerCanonicalJson> exactCanonicalResult = std::nullopt)
{
    Data::RuntimePackageCompilerCanonicalJson canonicalResult;
    if (exactCanonicalResult) {
        canonicalResult = *exactCanonicalResult;
    } else if (
        status == Data::RuntimePackageCompilerResultStatus::DomainFailed
        || status == Data::RuntimePackageCompilerResultStatus::Canceled) {
        QByteArray exactBytes("{\"diagnostics\":[");
        for (qsizetype index = 0; index < diagnostics.size(); ++index) {
            if (index != 0)
                exactBytes += ',';
            QByteArray diagnosticBytes = diagnostics.at(index).canonicalJson.exactBytes();
            diagnosticBytes.chop(1);
            exactBytes += diagnosticBytes;
        }
        exactBytes += "],\"status\":\"fail\"}\n";
        canonicalResult = RuntimePackageCompilerFixture::canonical(std::move(exactBytes));
    } else {
        canonicalResult = RuntimePackageCompilerFixture::canonical(
            QStringLiteral("{\"status\":\"%1\"}\n").arg(backendStatus).toLatin1());
    }
    return {
        command,
        status,
        backendStatus,
        operationId,
        configurationId,
        requestSha256,
        canonicalResult,
        std::move(diagnostics),
    };
}

static Data::RuntimePackageCompilerSha256 compilerRequestSha256(
    const Data::RuntimePackageCompilerCompileRequest &request)
{
    QByteArray identity("compile\n");
    identity += request.operationId.value().toUtf8();
    identity += '\n';
    identity += request.intentId.toUtf8();
    identity += '\n';
    identity += QByteArray::number(request.configurationId);
    identity += '\n';
    identity += request.targetProfile.canonicalProfile.sha256().value();
    identity += request.sourceArtifacts.adapterBundle.sha256.value();
    return RuntimePackageCompilerFixture::sha256(identity);
}

static Data::RuntimePackageCompilerSha256 compilerRequestSha256(
    const Data::RuntimePackageCompilerFinalizeRequest &request)
{
    QByteArray identity("finalize\n");
    identity += request.operationId.value().toUtf8();
    identity += '\n';
    identity += QByteArray::number(request.configurationId);
    identity += request.compileRequestSha256.value();
    identity += request.signRequestSha256.value();
    identity += request.manifestSha256.value();
    identity += request.signingKeyIdSha256.value();
    identity += QByteArray::number(request.signingPolicyRevision);
    identity += request.detachedSigningResponse.sha256().value();
    return RuntimePackageCompilerFixture::sha256(identity);
}

static Data::RuntimePackageCompilerSha256 compilerRequestSha256(
    const Data::RuntimePackageCompilerQueryRequest &request)
{
    QByteArray identity("query\n");
    identity += request.operationId.value().toUtf8();
    return RuntimePackageCompilerFixture::sha256(identity);
}

static Data::RuntimePackageCompilerSha256 compilerRequestSha256(
    const Data::RuntimePackageCompilerVerifyRequest &request)
{
    QByteArray identity("verify\n");
    identity += request.operationId.value().toUtf8();
    identity += request.packageSha256.value();
    return RuntimePackageCompilerFixture::sha256(identity);
}

static Data::RuntimePackageCompilerJobResult failedCompilerJobResult(
    Data::RuntimePackageCompilerCommand command,
    const Data::RuntimePackageCompilerOperationId &operationId,
    quint64 configurationId,
    const Data::RuntimePackageCompilerSha256 &requestSha256,
    Data::RuntimePackageCompilerDiagnosticCategory category,
    const QString &code,
    const Data::RuntimePackageCompilerSha256 &packageSha256 = {})
{
    const auto envelope = compilerEnvelope(
        command,
        Data::RuntimePackageCompilerResultStatus::DomainFailed,
        QStringLiteral("fail"),
        operationId,
        configurationId,
        requestSha256,
        {compilerDiagnostic(category, code)});
    switch (command) {
    case Data::RuntimePackageCompilerCommand::Compile:
        return Data::RuntimePackageCompilerJobResult{
            Data::RuntimePackageCompilerCompileResult{envelope, {}, {}, {}, {}, {}, {}, {}, {}, {}}};
    case Data::RuntimePackageCompilerCommand::Finalize:
        return Data::RuntimePackageCompilerJobResult{
            Data::RuntimePackageCompilerFinalizeResult{envelope, {}, {}, {}, {}, {}}};
    case Data::RuntimePackageCompilerCommand::Query:
        return Data::RuntimePackageCompilerJobResult{Data::RuntimePackageCompilerQueryResult{
            envelope, {}, {}}};
    case Data::RuntimePackageCompilerCommand::Verify:
        return Data::RuntimePackageCompilerJobResult{Data::RuntimePackageCompilerVerifyResult{
            envelope, packageSha256, 0, {}, {}, {}, {}, {}, false}};
    case Data::RuntimePackageCompilerCommand::Unknown:
        return {};
    }
    return {};
}

static Data::RuntimePackageCompilerJobResult canceledCompilerJobResult(
    Data::RuntimePackageCompilerCommand command,
    const Data::RuntimePackageCompilerOperationId &operationId,
    quint64 configurationId,
    const Data::RuntimePackageCompilerSha256 &requestSha256,
    const Data::RuntimePackageCompilerSha256 &packageSha256 = {})
{
    const auto envelope = compilerEnvelope(
        command,
        Data::RuntimePackageCompilerResultStatus::Canceled,
        QStringLiteral("cancelled"),
        operationId,
        configurationId,
        requestSha256,
        {compilerDiagnostic(
            Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
            QStringLiteral("ECOMP-CANCELLED"),
            QStringLiteral("cancelled"))});
    switch (command) {
    case Data::RuntimePackageCompilerCommand::Compile:
        return Data::RuntimePackageCompilerJobResult{
            Data::RuntimePackageCompilerCompileResult{envelope, {}, {}, {}, {}, {}, {}, {}, {}, {}}};
    case Data::RuntimePackageCompilerCommand::Finalize:
        return Data::RuntimePackageCompilerJobResult{
            Data::RuntimePackageCompilerFinalizeResult{envelope, {}, {}, {}, {}, {}}};
    case Data::RuntimePackageCompilerCommand::Query:
        return Data::RuntimePackageCompilerJobResult{Data::RuntimePackageCompilerQueryResult{
            envelope, {}, {}}};
    case Data::RuntimePackageCompilerCommand::Verify:
        return Data::RuntimePackageCompilerJobResult{Data::RuntimePackageCompilerVerifyResult{
            envelope, packageSha256, 0, {}, {}, {}, {}, {}, false}};
    case Data::RuntimePackageCompilerCommand::Unknown:
        return {};
    }
    return {};
}

static Data::RuntimePackageCompilerCompileResult successfulCompileResult(
    const Data::RuntimePackageCompilerCompileRequest &request)
{
    const auto digest = [](QByteArrayView bytes) {
        return RuntimePackageCompilerFixture::sha256(bytes);
    };
    const auto intentSha256 = compilerRequestSha256(request);
    const auto compiledProjectSha256 = digest("compiled-project");
    const auto compileReportSha256 = digest("compile-report");
    const auto effectiveProjectCompanionSha256 = digest("effective-project-companion");
    const auto manifestSha256 = digest("manifest");
    const auto targetProfileSha256 = request.targetProfile.canonicalProfile.sha256();
    const auto adapterBundleSha256 = request.sourceArtifacts.adapterBundle.sha256;
    const auto signRequest = RuntimePackageCompilerFixture::canonical(
        QStringLiteral("{\"effective_project_companion_sha256\":\"%1\","
                       "\"format\":\"ethercat-ecpkg-sign-request-v1\",\"format_version\":1,"
                       "\"intent_sha256\":\"%2\",\"manifest_sha256\":\"%3\","
                       "\"operation_id\":\"%4\",\"policy_revision\":%5,"
                       "\"signing_key_id\":\"%6\",\"target_profile_sha256\":\"%7\"}\n")
            .arg(
                QString::fromLatin1(effectiveProjectCompanionSha256.value().toHex()),
                QString::fromLatin1(intentSha256.value().toHex()),
                QString::fromLatin1(manifestSha256.value().toHex()),
                request.operationId.value(),
                QString::number(request.targetProfile.policyRevision),
                QString::fromLatin1(request.targetProfile.signingKeyIdSha256.value().toHex()),
                QString::fromLatin1(targetProfileSha256.value().toHex()))
            .toLatin1());
    const QString outputDirectory = QStringLiteral("/tmp/embed-labs-compiler-output");
    const QByteArray exactResult
        = QStringLiteral("{\"adapter_bundle_sha256\":\"%1\",\"compile_report_sha256\":\"%2\","
                         "\"compiled_project_sha256\":\"%3\",\"configuration_id\":%4,"
                         "\"effective_project_companion_sha256\":\"%5\","
                         "\"format\":\"ethercat-ide-project-compiler-result-v1\","
                         "\"format_version\":1,\"intent_sha256\":\"%6\","
                         "\"manifest_sha256\":\"%7\",\"operation_id\":\"%8\","
                         "\"output_dir\":\"%9\",\"sign_request_sha256\":\"%10\","
                         "\"status\":\"awaiting_signature\",\"target_profile_sha256\":\"%11\"}\n")
              .arg(
                  QString::fromLatin1(adapterBundleSha256.value().toHex()),
                  QString::fromLatin1(compileReportSha256.value().toHex()),
                  QString::fromLatin1(compiledProjectSha256.value().toHex()),
                  QString::number(request.configurationId),
                  QString::fromLatin1(effectiveProjectCompanionSha256.value().toHex()),
                  QString::fromLatin1(intentSha256.value().toHex()),
                  QString::fromLatin1(manifestSha256.value().toHex()),
                  request.operationId.value(),
                  outputDirectory,
                  QString::fromLatin1(signRequest.sha256().value().toHex()),
                  QString::fromLatin1(targetProfileSha256.value().toHex()))
              .toLatin1();
    const auto envelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Compile,
        Data::RuntimePackageCompilerResultStatus::Succeeded,
        QStringLiteral("awaiting_signature"),
        request.operationId,
        request.configurationId,
        compilerRequestSha256(request),
        {},
        RuntimePackageCompilerFixture::canonical(exactResult));
    return {
        envelope,
        intentSha256,
        compiledProjectSha256,
        compileReportSha256,
        effectiveProjectCompanionSha256,
        signRequest,
        manifestSha256,
        targetProfileSha256,
        adapterBundleSha256,
        outputDirectory,
    };
}

static Data::RuntimePackageCompilerCanonicalJson detachedSigningResponse(
    const Data::RuntimePackageCompilerCompileRequest &request,
    const Data::RuntimePackageCompilerCompileResult &compileResult)
{
    const QString manifestSha256 = QString::fromLatin1(
        compileResult.manifestSha256->value().toHex());
    const QString signRequestSha256 = QString::fromLatin1(
        compileResult.signRequest->sha256().value().toHex());
    const QString signature = QString::fromLatin1(QByteArray(64, '\x44').toHex());
    const QString signingKeyIdSha256 = QString::fromLatin1(
        request.targetProfile.signingKeyIdSha256.value().toHex());
    const QByteArray receiptProjection
        = QStringLiteral(
              "{\"format\":\"ethercat-ecpkg-sign-response-v1\",\"format_version\":1,"
              "\"manifest_sha256\":\"%1\",\"operation_id\":\"%2\","
              "\"policy_revision\":%3,\"request_sha256\":\"%4\","
              "\"signature_hex\":\"%5\",\"signing_key_id\":\"%6\"}\n")
              .arg(
                  manifestSha256,
                  request.operationId.value(),
                  QString::number(request.targetProfile.policyRevision),
                  signRequestSha256,
                  signature,
                  signingKeyIdSha256)
              .toLatin1();
    const QString receiptSha256 = QString::fromLatin1(
        RuntimePackageCompilerFixture::sha256(receiptProjection).value().toHex());
    return RuntimePackageCompilerFixture::canonical(
        QStringLiteral(
            "{\"format\":\"ethercat-ecpkg-sign-response-v1\",\"format_version\":1,"
            "\"manifest_sha256\":\"%1\",\"operation_id\":\"%2\","
            "\"policy_revision\":%3,\"receipt_sha256\":\"%4\","
            "\"request_sha256\":\"%5\",\"signature_hex\":\"%6\","
            "\"signing_key_id\":\"%7\"}\n")
            .arg(
                manifestSha256,
                request.operationId.value(),
                QString::number(request.targetProfile.policyRevision),
                receiptSha256,
                signRequestSha256,
                signature,
                signingKeyIdSha256)
            .toLatin1());
}

static Data::RuntimePackageCompilerFinalizeResult successfulFinalizeResult(
    const Data::RuntimePackageCompilerFinalizeRequest &request)
{
    const QByteArray packageBytes("deterministic-production-ecpkg");
    const auto packageSha256 = RuntimePackageCompilerFixture::sha256(packageBytes);
    const QJsonObject signingResponse
        = QJsonDocument::fromJson(request.detachedSigningResponse.exactBytes()).object();
    const Data::RuntimePackageCompilerSha256 signingReceiptSha256{QByteArray::fromHex(
        signingResponse.value(QStringLiteral("receipt_sha256")).toString().toLatin1())};
    const QString packagePath = QStringLiteral("/tmp/embed-labs-compiler-output/project.ecpkg");
    const QByteArray exactResult
        = QStringLiteral("{\"configuration_id\":%1,"
                         "\"format\":\"ethercat-ide-project-compiler-result-v1\","
                         "\"format_version\":1,\"manifest_sha256\":\"%2\","
                         "\"operation_id\":\"%3\",\"package_bytes\":%4,"
                         "\"package_path\":\"%5\",\"package_sha256\":\"%6\","
                         "\"signing_receipt_sha256\":\"%7\",\"status\":\"complete\"}\n")
              .arg(
                  QString::number(request.configurationId),
                  QString::fromLatin1(request.manifestSha256.value().toHex()),
                  request.operationId.value(),
                  QString::number(packageBytes.size()),
                  packagePath,
                  QString::fromLatin1(packageSha256.value().toHex()),
                  QString::fromLatin1(signingReceiptSha256.value().toHex()))
              .toLatin1();
    const auto envelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Finalize,
        Data::RuntimePackageCompilerResultStatus::Succeeded,
        QStringLiteral("complete"),
        request.operationId,
        request.configurationId,
        request.compileRequestSha256,
        {},
        RuntimePackageCompilerFixture::canonical(exactResult));
    return {
        envelope,
        packagePath,
        packageBytes,
        packageSha256,
        request.manifestSha256,
        signingReceiptSha256,
    };
}

static Data::RuntimePackageCompilerVerifyResult successfulVerifyResult(
    const Data::RuntimePackageCompilerVerifyRequest &request,
    const Data::RuntimePackageCompilerCompileRequest &compileRequest,
    const Data::RuntimePackageCompilerCompileResult &compileResult)
{
    const QByteArray exactResult
        = QStringLiteral("{\"adapter_bundle_sha256\":\"%1\",\"configuration_id\":%2,"
                         "\"effective_project_companion_sha256\":\"%3\","
                         "\"format\":\"ethercat-ide-project-compiler-verification-v1\","
                         "\"intent_sha256\":\"%4\",\"manifest_format_version\":2,"
                         "\"package_sha256\":\"%5\",\"status\":\"pass\","
                         "\"target_profile_sha256\":\"%6\","
                         "\"topology_evidence_sha256\":\"%7\"}\n")
              .arg(
                  QString::fromLatin1(compileResult.adapterBundleSha256->value().toHex()),
                  QString::number(compileRequest.configurationId),
                  QString::fromLatin1(
                      compileResult.effectiveProjectCompanionSha256->value().toHex()),
                  QString::fromLatin1(compileResult.intentSha256->value().toHex()),
                  QString::fromLatin1(request.packageSha256.value().toHex()),
                  QString::fromLatin1(compileResult.targetProfileSha256->value().toHex()),
                  QString::fromLatin1(
                      compileRequest.topologyEvidence.canonicalEvidence.sha256().value().toHex()))
              .toLatin1();
    return {
        compilerEnvelope(
            Data::RuntimePackageCompilerCommand::Verify,
            Data::RuntimePackageCompilerResultStatus::Succeeded,
            QStringLiteral("pass"),
            request.operationId,
            compileRequest.configurationId,
            request.packageSha256,
            {},
            RuntimePackageCompilerFixture::canonical(exactResult)),
        request.packageSha256,
        compileRequest.manifestFormatVersion,
        *compileResult.intentSha256,
        *compileResult.effectiveProjectCompanionSha256,
        *compileResult.targetProfileSha256,
        *compileResult.adapterBundleSha256,
        compileRequest.topologyEvidence.canonicalEvidence.sha256(),
        true,
    };
}

static Data::RuntimePackageCompilerActivationProof successfulActivationProof(
    Data::RuntimePackageCompilerCompileRequest compileRequest)
{
    const Data::RuntimePackageCompilerCompileResult compileResult
        = successfulCompileResult(compileRequest);
    const Data::RuntimePackageCompilerFinalizeRequest finalizeRequest{
        compileRequest.operationId,
        compileRequest.configurationId,
        compileRequest.contractIdentity,
        compileResult.envelope.requestSha256,
        compileResult.signRequest->sha256(),
        *compileResult.manifestSha256,
        compileRequest.targetProfile.signingKeyIdSha256,
        compileRequest.targetProfile.policyRevision,
        detachedSigningResponse(compileRequest, compileResult),
    };
    const Data::RuntimePackageCompilerFinalizeResult finalizeResult
        = successfulFinalizeResult(finalizeRequest);
    const Data::RuntimePackageCompilerVerifyRequest verifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000099")},
        compileRequest.contractIdentity,
        finalizeResult.packageBytes,
        *finalizeResult.packageSha256,
    };
    const Data::RuntimePackageCompilerVerifyResult verifyResult
        = successfulVerifyResult(verifyRequest, compileRequest, compileResult);
    return {
        QStringLiteral("org.embedlabs.runtime-package-compiler.api042"),
        compileRequest.contractIdentity,
        compileRequest,
        compileResult,
        finalizeRequest,
        compilerRequestSha256(finalizeRequest),
        finalizeResult,
        verifyRequest,
        compilerRequestSha256(verifyRequest),
        verifyResult,
        QByteArray("compiled-project"),
        QByteArray("effective-project-companion"),
    };
}

Data::RuntimePackageCompilerActivationProof
syntheticRuntimePackageCompilerActivationProof()
{
    RuntimePackageCompilerFixture fixture;
    return successfulActivationProof(fixture.request);
}

static Data::RuntimePackageCompilerCanonicalJson compilerLedgerRecord(
    const Data::RuntimePackageCompilerCompileRequest &request,
    const std::optional<Data::RuntimePackageCompilerCompileResult> &compileResult,
    const std::optional<Data::RuntimePackageCompilerFinalizeResult> &finalizeResult)
{
    QJsonObject record{
        {QStringLiteral("build_timestamp_ns"), qint64(request.buildTimestampNs)},
        {QStringLiteral("configuration_id"), qint64(request.configurationId)},
        {QStringLiteral("intent_sha256"),
         QString::fromLatin1(
             (compileResult && compileResult->intentSha256
                  ? compileResult->intentSha256->value()
                  : compilerRequestSha256(request).value())
                 .toHex())},
        {QStringLiteral("state"), QStringLiteral("reserved")},
    };
    if (compileResult) {
        record.insert(
            QStringLiteral("compile_report_sha256"),
            QString::fromLatin1(compileResult->compileReportSha256->value().toHex()));
        record.insert(
            QStringLiteral("effective_project_companion_sha256"),
            QString::fromLatin1(
                compileResult->effectiveProjectCompanionSha256->value().toHex()));
        record.insert(
            QStringLiteral("manifest_sha256"),
            QString::fromLatin1(compileResult->manifestSha256->value().toHex()));
        record.insert(QStringLiteral("output_dir"), compileResult->outputDirectory);
        record.insert(
            QStringLiteral("sign_request_sha256"),
            QString::fromLatin1(compileResult->signRequest->sha256().value().toHex()));
        record.insert(QStringLiteral("state"), QStringLiteral("prepared"));
    }
    if (finalizeResult) {
        record.insert(QStringLiteral("package_bytes"), finalizeResult->packageBytes.size());
        record.insert(QStringLiteral("package_path"), finalizeResult->packagePath);
        record.insert(
            QStringLiteral("package_sha256"),
            QString::fromLatin1(finalizeResult->packageSha256->value().toHex()));
        record.insert(QStringLiteral("state"), QStringLiteral("finalized"));
    }
    return RuntimePackageCompilerFixture::canonical(
        QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n');
}

class TestRuntimePackageCompilerJob final : public RuntimePackageCompilerJob
{
public:
    TestRuntimePackageCompilerJob(
        Data::RuntimePackageCompilerJobResult plannedResult,
        Data::RuntimePackageCompilerJobResult canceledResult,
        QObject *parent)
        : RuntimePackageCompilerJob(plannedResult.command(), parent)
        , m_plannedResult(std::move(plannedResult))
        , m_canceledResult(std::move(canceledResult))
    {}

    void startAsynchronously()
    {
        QTimer::singleShot(0, this, [this] {
            if (state() == RuntimePackageCompilerJobState::Pending)
                markRunning();
            QTimer::singleShot(0, this, [this] {
                finish(
                    state() == RuntimePackageCompilerJobState::CancelRequested ? m_canceledResult
                                                                               : m_plannedResult);
            });
        });
    }

    int cancellationRequestCount() const { return m_cancellationRequestCount; }

    void finishAgainForTest() { finish(m_plannedResult); }
    void finishForTest(const Data::RuntimePackageCompilerJobResult &result) { finish(result); }

protected:
    void requestCancellation() final { ++m_cancellationRequestCount; }

private:
    Data::RuntimePackageCompilerJobResult m_plannedResult;
    Data::RuntimePackageCompilerJobResult m_canceledResult;
    int m_cancellationRequestCount = 0;
};

class TestRuntimePackageCompilerProvider final : public RuntimePackageCompilerProvider
{
public:
    explicit TestRuntimePackageCompilerProvider(
        Utils::Id id = Utils::Id("EtherCAT.Compiler.Test"))
        : RuntimePackageCompilerProvider(id, QStringLiteral("Test runtime package compiler"))
    {}

    Utils::Result<RuntimePackageCompilerJob *> compile(
        const Data::RuntimePackageCompilerCompileRequest &request) final
    {
        if (failScheduling)
            return Utils::ResultError(QStringLiteral("Compiler scheduler is unavailable."));
        if (!request.operationId.isValid() || !request.contractIdentity.isValid()) {
            return Utils::ResultError(
                QStringLiteral("Compiler request cannot be assigned a durable identity."));
        }

        const auto canceled = canceledCompilerJobResult(
            Data::RuntimePackageCompilerCommand::Compile,
            request.operationId,
            request.configurationId,
            compilerRequestSha256(request));

        if (!request.hasValidReservationInputs()) {
            return schedule(
                failedCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Compile,
                    request.operationId,
                    request.configurationId,
                    compilerRequestSha256(request),
                    Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
                    QStringLiteral("ECOMP-REQUEST-SCHEMA")),
                canceled);
        }

        const auto existing = m_compileRequests.constFind(request.operationId);
        if (existing != m_compileRequests.cend()) {
            if (*existing != request) {
                return schedule(
                    failedCompilerJobResult(
                        Data::RuntimePackageCompilerCommand::Compile,
                        request.operationId,
                        request.configurationId,
                        compilerRequestSha256(request),
                        Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
                        QStringLiteral("ECOMP-OPERATION-CONFLICT")),
                    canceled);
            }
            if (const auto finished = m_compileResults.constFind(request.operationId);
                finished != m_compileResults.cend()) {
                return schedule(Data::RuntimePackageCompilerJobResult{*finished}, canceled);
            }
            if (RuntimePackageCompilerJob *running = m_compileJobs.value(request.operationId))
                return running;
        } else {
            const auto configurationOwner = m_configurationOwners.constFind(
                request.configurationId);
            if (configurationOwner != m_configurationOwners.cend()
                && *configurationOwner != request.operationId) {
                return schedule(
                    failedCompilerJobResult(
                        Data::RuntimePackageCompilerCommand::Compile,
                        request.operationId,
                        request.configurationId,
                        compilerRequestSha256(request),
                        Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
                        QStringLiteral("ECOMP-CONFIGURATION-REPLAY")),
                    canceled);
            }

            m_compileRequests.insert(request.operationId, request);
            m_configurationOwners.insert(request.configurationId, request.operationId);
            ++ledgerMutationCount;
        }

        Data::RuntimePackageCompilerJobResult plannedResult;
        if (!request.isValid()) {
            Data::RuntimePackageCompilerDiagnosticCategory category
                = Data::RuntimePackageCompilerDiagnosticCategory::Capability;
            QString code = QStringLiteral("ECOMP-CAPABILITY-PROFILE");
            if (!request.topologyEvidence.isFreshAt(request.compileTimeNs)) {
                category = Data::RuntimePackageCompilerDiagnosticCategory::Slave;
                code = QStringLiteral("ECOMP-TOPOLOGY-STALE");
            } else if (!request.sourceArtifacts.adapterBundle.isValid()) {
                category = Data::RuntimePackageCompilerDiagnosticCategory::Adapter;
                code = QStringLiteral("ECOMP-ADAPTER-BUNDLE-MISMATCH");
            } else if (
                request.deviceSourceEvidence.isEmpty()
                || !request.deviceSourceEvidence.constFirst().originalEsi.isValid()) {
                category = Data::RuntimePackageCompilerDiagnosticCategory::Slave;
                code = QStringLiteral("ECOMP-ESI-MISMATCH");
            } else if (!request.deviceSourceEvidence.constFirst().isValid()) {
                category = Data::RuntimePackageCompilerDiagnosticCategory::Dc;
                code = QStringLiteral("ECOMP-DC-UNSUPPORTED");
            }
            plannedResult = failedCompilerJobResult(
                Data::RuntimePackageCompilerCommand::Compile,
                request.operationId,
                request.configurationId,
                compilerRequestSha256(request),
                category,
                code);
        } else {
            plannedResult = Data::RuntimePackageCompilerJobResult{
                successfulCompileResult(request)};
        }

        RuntimePackageCompilerJob *job = schedule(
            plannedResult,
            canceled,
            [this, operationId = request.operationId](
                const Data::RuntimePackageCompilerJobResult &terminal) {
                const auto &compileResult
                    = std::get<Data::RuntimePackageCompilerCompileResult>(terminal.value());
                if (compileResult.envelope.status
                    == Data::RuntimePackageCompilerResultStatus::Succeeded) {
                    m_compileResults.insert(operationId, compileResult);
                    ++ledgerMutationCount;
                }
                m_compileJobs.remove(operationId);
            });
        m_compileJobs.insert(request.operationId, job);
        return job;
    }

    Utils::Result<RuntimePackageCompilerJob *> finalize(
        const Data::RuntimePackageCompilerFinalizeRequest &request) final
    {
        if (failScheduling)
            return Utils::ResultError(QStringLiteral("Compiler scheduler is unavailable."));
        if (!request.operationId.isValid() || !request.contractIdentity.isValid()) {
            return Utils::ResultError(
                QStringLiteral("Finalize request cannot be assigned a durable identity."));
        }

        const auto canceled = canceledCompilerJobResult(
            Data::RuntimePackageCompilerCommand::Finalize,
            request.operationId,
            request.configurationId,
            compilerRequestSha256(request));
        const auto compile = m_compileRequests.constFind(request.operationId);
        const auto compileResult = m_compileResults.constFind(request.operationId);
        if (!request.isValid() || compile == m_compileRequests.cend()
            || compileResult == m_compileResults.cend()
            || request.configurationId != compile->configurationId
            || request.compileRequestSha256 != compilerRequestSha256(*compile)
            || !compileResult->manifestSha256
            || request.manifestSha256 != *compileResult->manifestSha256) {
            return schedule(
                failedCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Finalize,
                    request.operationId,
                    request.configurationId,
                    compilerRequestSha256(request),
                    Data::RuntimePackageCompilerDiagnosticCategory::Signing,
                    QStringLiteral("ECOMP-SIGN-RESPONSE-STALE")),
                canceled);
        }

        const auto existing = m_finalizeRequests.constFind(request.operationId);
        if (existing != m_finalizeRequests.cend()) {
            if (*existing != request) {
                return schedule(
                    failedCompilerJobResult(
                        Data::RuntimePackageCompilerCommand::Finalize,
                        request.operationId,
                        request.configurationId,
                        compilerRequestSha256(request),
                        Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
                        QStringLiteral("ECOMP-OPERATION-CONFLICT")),
                    canceled);
            }
            if (const auto finished = m_finalizeResults.constFind(request.operationId);
                finished != m_finalizeResults.cend()) {
                return schedule(Data::RuntimePackageCompilerJobResult{*finished}, canceled);
            }
            if (RuntimePackageCompilerJob *running = m_finalizeJobs.value(request.operationId))
                return running;
        } else {
            m_finalizeRequests.insert(request.operationId, request);
            ++ledgerMutationCount;
        }

        const Data::RuntimePackageCompilerFinalizeResult result = successfulFinalizeResult(request);
        RuntimePackageCompilerJob *job = schedule(
            Data::RuntimePackageCompilerJobResult{result},
            canceled,
            [this, operationId = request.operationId](
                const Data::RuntimePackageCompilerJobResult &terminal) {
                const auto &finalizeResult
                    = std::get<Data::RuntimePackageCompilerFinalizeResult>(terminal.value());
                if (finalizeResult.envelope.status
                    == Data::RuntimePackageCompilerResultStatus::Succeeded) {
                    m_finalizeResults.insert(operationId, finalizeResult);
                    ++ledgerMutationCount;
                }
                m_finalizeJobs.remove(operationId);
            });
        m_finalizeJobs.insert(request.operationId, job);
        return job;
    }

    Utils::Result<RuntimePackageCompilerJob *> query(
        const Data::RuntimePackageCompilerQueryRequest &request) final
    {
        if (failScheduling)
            return Utils::ResultError(QStringLiteral("Compiler scheduler is unavailable."));
        if (!request.operationId.isValid() || !request.contractIdentity.isValid()) {
            return Utils::ResultError(
                QStringLiteral("Query request cannot be assigned a durable identity."));
        }
        if (!request.isValid()) {
            return schedule(
                failedCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Query,
                    request.operationId,
                    0,
                    compilerRequestSha256(request),
                    Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
                    QStringLiteral("ECOMP-QUERY-INVALID")),
                canceledCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Query,
                    request.operationId,
                    0,
                    compilerRequestSha256(request)));
        }

        const auto compileRequest = m_compileRequests.constFind(request.operationId);
        const auto finalizeRequest = m_finalizeRequests.constFind(request.operationId);
        if (compileRequest == m_compileRequests.cend()
            && finalizeRequest == m_finalizeRequests.cend()) {
            return schedule(
                failedCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Query,
                    request.operationId,
                    0,
                    compilerRequestSha256(request),
                    Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
                    QStringLiteral("ECOMP-OPERATION-UNKNOWN")),
                canceledCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Query,
                    request.operationId,
                    0,
                    compilerRequestSha256(request)));
        }

        quint64 configurationId = 0;
        std::optional<Data::RuntimePackageCompilerCanonicalJson> compilerRecord;
        std::optional<Data::RuntimePackageCompilerCanonicalJson> signerResponse;
        if (compileRequest != m_compileRequests.cend()) {
            configurationId = compileRequest->configurationId;
            std::optional<Data::RuntimePackageCompilerCompileResult> compileResult;
            if (const auto found = m_compileResults.constFind(request.operationId);
                found != m_compileResults.cend()) {
                compileResult = *found;
            }
            std::optional<Data::RuntimePackageCompilerFinalizeResult> finalizeResult;
            if (const auto found = m_finalizeResults.constFind(request.operationId);
                found != m_finalizeResults.cend()) {
                finalizeResult = *found;
            }
            compilerRecord = compilerLedgerRecord(
                *compileRequest, compileResult, finalizeResult);
        }
        if (finalizeRequest != m_finalizeRequests.cend())
            signerResponse = finalizeRequest->detachedSigningResponse;

        QJsonObject canonicalStateObject{
            {QStringLiteral("compiler"),
             compilerRecord
                 ? QJsonValue(
                       QJsonDocument::fromJson(compilerRecord->exactBytes()).object())
                 : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("format"), QStringLiteral("ethercat-ide-compiler-operation-state-v1")},
            {QStringLiteral("operation_id"), request.operationId.value()},
            {QStringLiteral("signer_response"),
             signerResponse
                 ? QJsonValue(
                       QJsonDocument::fromJson(signerResponse->exactBytes()).object())
                 : QJsonValue(QJsonValue::Null)},
        };
        const Data::RuntimePackageCompilerCanonicalJson canonicalState
            = RuntimePackageCompilerFixture::canonical(
                QJsonDocument(canonicalStateObject).toJson(QJsonDocument::Compact) + '\n');
        const auto envelope = compilerEnvelope(
            Data::RuntimePackageCompilerCommand::Query,
            Data::RuntimePackageCompilerResultStatus::Succeeded,
            QStringLiteral("state"),
            request.operationId,
            configurationId,
            compilerRequestSha256(request),
            {},
            canonicalState);
        const Data::RuntimePackageCompilerQueryResult result{
            envelope,
            compilerRecord,
            signerResponse,
        };
        return schedule(
            Data::RuntimePackageCompilerJobResult{result},
            canceledCompilerJobResult(
                Data::RuntimePackageCompilerCommand::Query,
                request.operationId,
                0,
                compilerRequestSha256(request)));
    }

    Utils::Result<RuntimePackageCompilerJob *> verify(
        const Data::RuntimePackageCompilerVerifyRequest &request) final
    {
        if (failScheduling)
            return Utils::ResultError(QStringLiteral("Compiler scheduler is unavailable."));
        if (!request.operationId.isValid() || !request.contractIdentity.isValid()) {
            return Utils::ResultError(
                QStringLiteral("Verify request cannot be assigned a durable identity."));
        }
        const Data::RuntimePackageCompilerSha256 transportablePackageSha256
            = request.packageSha256.isValid() ? request.packageSha256
                                              : Data::RuntimePackageCompilerSha256{};
        const auto canceled = canceledCompilerJobResult(
            Data::RuntimePackageCompilerCommand::Verify,
            request.operationId,
            0,
            compilerRequestSha256(request),
            transportablePackageSha256);
        if (!request.isValid()) {
            return schedule(
                failedCompilerJobResult(
                    Data::RuntimePackageCompilerCommand::Verify,
                    request.operationId,
                    0,
                    compilerRequestSha256(request),
                    Data::RuntimePackageCompilerDiagnosticCategory::Signing,
                    QStringLiteral("ECOMP-SIGNATURE-INVALID"),
                    transportablePackageSha256),
                canceled);
        }

        ++verifyCallCount;
        const auto intentSha256 = RuntimePackageCompilerFixture::sha256("intent");
        const auto effectiveProjectCompanionSha256 = RuntimePackageCompilerFixture::sha256(
            "effective-project-companion");
        const auto targetProfileSha256 = RuntimePackageCompilerFixture::sha256("target-profile");
        const auto adapterBundleSha256 = RuntimePackageCompilerFixture::sha256("adapter-bundle");
        const auto topologyEvidenceSha256 = RuntimePackageCompilerFixture::sha256(
            "topology-evidence");
        const QByteArray exactResult
            = QStringLiteral(
                  "{\"adapter_bundle_sha256\":\"%1\",\"configuration_id\":4201,"
                  "\"effective_project_companion_sha256\":\"%2\","
                  "\"format\":\"ethercat-ide-project-compiler-verification-v1\","
                  "\"intent_sha256\":\"%3\",\"manifest_format_version\":2,"
                  "\"package_sha256\":\"%4\",\"status\":\"pass\","
                  "\"target_profile_sha256\":\"%5\",\"topology_evidence_sha256\":\"%6\"}\n")
                  .arg(
                      QString::fromLatin1(adapterBundleSha256.value().toHex()),
                      QString::fromLatin1(effectiveProjectCompanionSha256.value().toHex()),
                      QString::fromLatin1(intentSha256.value().toHex()),
                      QString::fromLatin1(request.packageSha256.value().toHex()),
                      QString::fromLatin1(targetProfileSha256.value().toHex()),
                      QString::fromLatin1(topologyEvidenceSha256.value().toHex()))
                  .toLatin1();
        const auto envelope = compilerEnvelope(
            Data::RuntimePackageCompilerCommand::Verify,
            Data::RuntimePackageCompilerResultStatus::Succeeded,
            QStringLiteral("pass"),
            request.operationId,
            4201,
            compilerRequestSha256(request),
            {},
            RuntimePackageCompilerFixture::canonical(exactResult));
        const Data::RuntimePackageCompilerVerifyResult result{
            envelope,
            request.packageSha256,
            2,
            intentSha256,
            effectiveProjectCompanionSha256,
            targetProfileSha256,
            adapterBundleSha256,
            topologyEvidenceSha256,
            true,
        };
        return schedule(Data::RuntimePackageCompilerJobResult{result}, canceled);
    }

    Utils::Result<> validateActivationProof(
        const Data::RuntimePackageCompilerActivationProof &proof) const final
    {
        ++activationProofValidationCount;
        activationProofs.append(proof);
        if (activationProofValidationError)
            return Utils::ResultError(*activationProofValidationError);
        if (expectedActivationProof && proof != *expectedActivationProof) {
            return Utils::ResultError(
                QStringLiteral("Compiler activation proof provenance differs."));
        }
        return Utils::ResultOk;
    }

    bool failScheduling = false;
    int ledgerMutationCount = 0;
    int verifyCallCount = 0;
    std::optional<Data::RuntimePackageCompilerActivationProof> expectedActivationProof;
    std::optional<QString> activationProofValidationError;
    mutable int activationProofValidationCount = 0;
    mutable QList<Data::RuntimePackageCompilerActivationProof> activationProofs;

private:
    RuntimePackageCompilerJob *schedule(
        Data::RuntimePackageCompilerJobResult result,
        Data::RuntimePackageCompilerJobResult canceledResult,
        std::function<void(const Data::RuntimePackageCompilerJobResult &)> terminalCallback = {})
    {
        auto *job
            = new TestRuntimePackageCompilerJob(std::move(result), std::move(canceledResult), this);
        if (terminalCallback) {
            connect(
                job,
                &RuntimePackageCompilerJob::finished,
                this,
                [callback = std::move(terminalCallback)](
                    const Data::RuntimePackageCompilerJobResult &terminal) { callback(terminal); });
        }
        job->startAsynchronously();
        return job;
    }

    QHash<Data::RuntimePackageCompilerOperationId, Data::RuntimePackageCompilerCompileRequest>
        m_compileRequests;
    QHash<Data::RuntimePackageCompilerOperationId, Data::RuntimePackageCompilerCompileResult>
        m_compileResults;
    QHash<Data::RuntimePackageCompilerOperationId, RuntimePackageCompilerJob *> m_compileJobs;
    QHash<Data::RuntimePackageCompilerOperationId, Data::RuntimePackageCompilerFinalizeRequest>
        m_finalizeRequests;
    QHash<Data::RuntimePackageCompilerOperationId, Data::RuntimePackageCompilerFinalizeResult>
        m_finalizeResults;
    QHash<Data::RuntimePackageCompilerOperationId, RuntimePackageCompilerJob *> m_finalizeJobs;
    QHash<quint64, Data::RuntimePackageCompilerOperationId> m_configurationOwners;
};

class TestRuntimePackageCompilerPreparationCoordinator final
    : public RuntimePackageCompilerPreparationCoordinator
{
public:
    explicit TestRuntimePackageCompilerPreparationCoordinator(ProviderRegistry *registry)
        : RuntimePackageCompilerPreparationCoordinator(registry)
    {}

    void setRecordForTest(
        std::optional<RuntimePackageCompilerPreparationRecord> record)
    {
        m_record = std::move(record);
    }

    Utils::Result<> publishTransitionForTest(
        const std::optional<RuntimePackageCompilerPreparationRecord> &previous,
        const RuntimePackageCompilerPreparationRecord &current,
        const RuntimePackageCompilerPreparationSnapshot &snapshot)
    {
        m_previousRecordForTransition = previous;
        return publishRecordTransition(current, snapshot);
    }

    Utils::Result<> publishSigningRequestForTest(
        const RuntimePackageCompilerPreparationRecord &record)
    {
        return publishDetachedSigningRequest(record);
    }

    Utils::Result<> publishPreparationForTest(
        const RuntimePackageCompilerPreparationRecord &record)
    {
        return publishPreparationReady(record);
    }

    RuntimePackageCompilerProvider *lastProvider = nullptr;
    int startCount = 0;
    int submitCount = 0;
    int cancelCount = 0;
    int resumeCount = 0;
    bool returnInvalidSnapshot = false;
    std::optional<quint64> snapshotSequenceOverride;
    std::optional<RuntimePackageCompilerPreparationStartRequest> lastResumeRequest;

protected:
    Utils::Result<RuntimePackageCompilerPreparationDisposition> doStart(
        const RuntimePackageCompilerPreparationStartRequest &request,
        RuntimePackageCompilerProvider *frozenProvider) final
    {
        ++startCount;
        lastProvider = frozenProvider;
        const auto fingerprint
            = runtimePackageCompilerPreparationStartRequestFingerprint(request);
        if (!fingerprint)
            return Utils::ResultError(fingerprint.error());
        RuntimePackageCompilerPreparationRecord record;
        record.compileOperationId = request.compileRequest.operationId;
        record.startRequestFingerprint = *fingerprint;
        record.verifyOperationId = request.verifyOperationId;
        record.activationOperationId = request.activationOperationId;
        record.startRequest = request;
        const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalRequest
            = encodeRuntimePackageCompilerCompileRequest(request.compileRequest);
        if (!canonicalRequest)
            return Utils::ResultError(canonicalRequest.error());
        record.compileRequestSha256 = canonicalRequest->sha256();
        record.compilerProviderId = frozenProvider->id().toString();
        record.contractIdentity = request.compileRequest.contractIdentity;
        record.revision = 1;
        record.phase = RuntimePackageCompilerPreparationPhase::Reserved;
        m_record = record;
        return RuntimePackageCompilerPreparationDisposition::Started;
    }

    Utils::Result<RuntimePackageCompilerPreparationDisposition>
    doSubmitDetachedSigningResponse(
        const RuntimePackageCompilerPreparationRecord &,
        const Data::RuntimePackageCompilerCanonicalJson &,
        RuntimePackageCompilerProvider *frozenProvider) final
    {
        ++submitCount;
        lastProvider = frozenProvider;
        return RuntimePackageCompilerPreparationDisposition::Accepted;
    }

    Utils::Result<RuntimePackageCompilerPreparationDisposition> doCancel(
        const RuntimePackageCompilerPreparationRecord &) final
    {
        ++cancelCount;
        return RuntimePackageCompilerPreparationDisposition::Accepted;
    }

    Utils::Result<RuntimePackageCompilerPreparationDisposition> doResume(
        const RuntimePackageCompilerPreparationRecord &,
        const RuntimePackageCompilerPreparationStartRequest &exactOriginalRequest,
        RuntimePackageCompilerProvider *frozenProvider) final
    {
        ++resumeCount;
        lastProvider = frozenProvider;
        lastResumeRequest = exactOriginalRequest;
        return RuntimePackageCompilerPreparationDisposition::Accepted;
    }

    Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>> doRecord(
        const Data::RuntimePackageCompilerOperationId &operationId) const final
    {
        if (m_record && m_record->compileOperationId == operationId) {
            return m_record;
        }
        return std::optional<RuntimePackageCompilerPreparationRecord>{};
    }

    Utils::Result<RuntimePackageCompilerPreparationSnapshot> doSnapshot() const final
    {
        if (returnInvalidSnapshot) {
            return RuntimePackageCompilerPreparationSnapshot{
                0, m_record ? QList{*m_record} : QList<RuntimePackageCompilerPreparationRecord>{}};
        }
        return RuntimePackageCompilerPreparationSnapshot{
            snapshotSequenceOverride.value_or(
                m_record ? m_record->revision : quint64(0)),
            m_record ? QList{*m_record} : QList<RuntimePackageCompilerPreparationRecord>{}};
    }

    Utils::Result<std::optional<RuntimePackageCompilerPreparationRecord>>
    doPreviousRecordForCommittedTransition(
        const Data::RuntimePackageCompilerOperationId &,
        quint64,
        quint64) const final
    {
        return m_previousRecordForTransition;
    }

private:
    std::optional<RuntimePackageCompilerPreparationRecord> m_record;
    std::optional<RuntimePackageCompilerPreparationRecord> m_previousRecordForTransition;
};

struct SemanticRuntimeFixture
{
    Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    Data::NodeId deviceId = Data::NodeId::create();
    Data::RuntimeResourceCatalogEpoch epoch;
    Data::SemanticRuntimeDigest digest;
    Data::SemanticRuntimeTarget target;
    Data::SemanticRuntimeBinding binding;
    Data::RuntimeResourceCatalog catalog;
    Data::RuntimeResourceSnapshot snapshot;
    Data::SemanticRuntimeContext context;
    Data::SemanticRuntimeActor actor;
    Data::SemanticOperationRequest request;

    SemanticRuntimeFixture()
    {
        epoch.controllerBootId = 11;
        epoch.activePackageSlot = Data::ControllerSlot::B;
        epoch.activePackageGeneration = 12;
        epoch.configurationId = 13;
        epoch.topologyGeneration = 14;
        epoch.runtimeGeneration = 15;
        epoch.catalogRevision = 16;
        epoch.topologyIdentity = QByteArray::fromHex("0102030405060708");

        digest.algorithm = "sha256";
        digest.value = QByteArray(32, '\x5a');

        target.controllerId = "ide:test-controller";
        target.scope = scope;
        target.deviceId = deviceId;
        target.kind = Data::SemanticRuntimeTargetKind::Signal;
        target.signalId = {"urn:example.test:signal/input.1"};

        binding.target = target;
        binding.semanticBindingId = "binding:test:input.1";
        binding.componentBindingId = "component:test:device.1";
        binding.adapterId = {"org.example.test.adapter/runtime"};
        binding.adapterVersion = "1.0.0";
        binding.adapterContentSha256 = QByteArray(32, '\x21');
        binding.esiSha256 = QByteArray(32, '\x32');
        binding.bindingArtifactSha256 = QByteArray(32, '\x43');
        binding.sessionGeneration = 7;
        binding.epoch = epoch;
        binding.mappingDigest = digest;
        binding.controllerMappingDigest = digest;
        binding.verification.state = Data::SemanticBindingVerificationState::Verified;
        binding.verification.verifierId = "test-verifier";
        binding.verification.signedManifestDigest = {"sha256", QByteArray(32, '\x6b')};
        binding.verification.verifiedAt = QDateTime::currentDateTimeUtc();
        binding.resourceId = {QByteArray::fromHex("1000000000000001")};
        binding.componentInstanceId = {QByteArray::fromHex("2000000000000001")};
        binding.consistencyGroupId = {QByteArray::fromHex("3000000000000001")};
        binding.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
        binding.valueTypeIdentity = "ethercat.runtime.value/primitive-1/bits-1";
        binding.bitWidth = 1;
        binding.direction = Data::RuntimeResourceDirection::Bidirectional;
        binding.access = Data::RuntimeResourceAccess::ReadWrite;

        Data::RuntimeResourceDescriptor descriptor;
        descriptor.id = binding.resourceId;
        descriptor.componentInstanceId = binding.componentInstanceId;
        descriptor.consistencyGroupId = binding.consistencyGroupId;
        descriptor.displayName = "Input 1";
        descriptor.primitiveType = binding.primitiveType;
        descriptor.valueTypeIdentity = binding.valueTypeIdentity;
        descriptor.bitWidth = binding.bitWidth;
        descriptor.direction = binding.direction;
        descriptor.access = binding.access;
        descriptor.processImageBitOffset = 23;
        descriptor.processImageBitLength = 1;

        catalog.scope = scope;
        catalog.sessionGeneration = binding.sessionGeneration;
        catalog.epoch = epoch;
        catalog.receivedAt = QDateTime::currentDateTimeUtc();
        catalog.resources = {descriptor};

        Data::RuntimeResourceSample sample;
        sample.resourceId = binding.resourceId;
        sample.consistencyGroupId = binding.consistencyGroupId;
        sample.value.primitiveType = binding.primitiveType;
        sample.value.typeIdentity = binding.valueTypeIdentity;
        sample.value.value = true;
        sample.quality.state = Data::RuntimeResourceQualityState::Good;
        sample.valueSequence = 9;
        sample.controllerTimestampNs = 123456;

        snapshot.scope = scope;
        snapshot.sessionGeneration = binding.sessionGeneration;
        snapshot.epoch = epoch;
        snapshot.snapshotSequence = 10;
        snapshot.captureCycle = 101;
        snapshot.controllerTimestampNs = sample.controllerTimestampNs;
        snapshot.receivedAt = catalog.receivedAt;
        snapshot.complete = true;
        snapshot.samples = {sample};

        Data::SemanticSignalRuntimeState signal;
        signal.target = target;
        signal.definition.id = target.signalId;
        signal.definition.displayName = "Input 1";
        signal.definition.direction = Data::SemanticSignalDirection::Bidirectional;
        signal.definition.access = Data::SemanticSignalAccess::ReadWrite;
        signal.definition.manualControl.allowed = true;
        signal.definition.manualControl.holdToRun = true;
        signal.definition.manualControl.commandTimeoutMs = 250;
        signal.availability = Data::SemanticSignalAvailability::Ready;
        signal.binding = binding;
        signal.value = sample.value;
        signal.quality = sample.quality;
        signal.snapshotComplete = true;
        signal.captureCycle = snapshot.captureCycle;
        signal.controllerTimestampNs = snapshot.controllerTimestampNs;

        context.controllerId = target.controllerId;
        context.scope = scope;
        context.sessionGeneration = binding.sessionGeneration;
        context.epoch = epoch;
        context.mappingDigest = digest;
        context.controllerMappingDigest = digest;
        context.bindingVerification = binding.verification;
        context.contextHash = QByteArray(32, '\x7d');
        context.signalStates = {signal};
        context.complete = true;
        context.mock = false;

        actor.id = "user:test";
        actor.displayName = "Test User";
        actor.kind = Data::SemanticRuntimeActorKind::User;
        actor.origin = "qt-test";
        actor.authenticationDigest = QByteArray(32, '\x19');

        request.operationId = {"gateway-operation-001"};
        request.kind = Data::SemanticOperationKind::SetSignalValue;
        request.target = target;
        request.expectedEpoch = epoch;
        request.expectedMappingDigest = digest;
        request.expectedControllerMappingDigest = digest;
        request.expectedContextHash = context.contextHash;
        request.value = true;
        request.ttlMs = 250;
        request.reason = "test";
    }
};

struct RuntimeOutputFixture
{
    Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    Data::RuntimeResourceCatalogEpoch epoch;
    Data::RuntimeConsistencyGroupId groupId{QByteArray::fromHex("00000055")};
    QByteArray mappingDigest = QByteArray(32, '\x33');
    QByteArray groupRecordDigest = QByteArray(32, '\x44');
    Data::RuntimeOutputOperationId operationId{
        QByteArray::fromHex("00112233445566778899aabbccddeeff")};
    Data::RuntimeOutputGroupPolicyRequest policyRequest;
    Data::RuntimeOutputGroupPolicy policy;
    Data::RuntimeOutputTransactionStateRequest stateRequest;
    Data::RuntimeOutputTransactionState idleState;
    Data::RuntimeOutputTransactionRequest transactionRequest;
    Data::RuntimeOutputTransactionState appliedState;

    RuntimeOutputFixture()
    {
        epoch.controllerBootId = 0x2122232425262728;
        epoch.activePackageSlot = Data::ControllerSlot::A;
        epoch.activePackageGeneration = 7;
        epoch.configurationId = 0x100;
        epoch.topologyGeneration = 9;
        epoch.runtimeGeneration = 11;
        epoch.catalogRevision = 13;
        epoch.topologyIdentity = QByteArray::fromHex("6000000000000001");

        policyRequest.correlationId = "output-policy-1";
        policyRequest.scope = scope;
        policyRequest.sessionGeneration = 3;
        policyRequest.expectedEpoch = epoch;
        policyRequest.consistencyGroupId = groupId;
        policyRequest.expectedMappingDigest = mappingDigest;

        policy.scope = scope;
        policy.sessionGeneration = policyRequest.sessionGeneration;
        policy.epoch = epoch;
        policy.consistencyGroupId = groupId;
        policy.mappingDigest = mappingDigest;
        policy.completeGroupRecordDigest = groupRecordDigest;
        policy.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::ReturnTask;
        policy.maximumTtlCycles = 1000;
        policy.completeResourceCount = 2;
        policy.currentOutputGeneration = 9;
        policy.manualWriteAllowed = true;
        policy.receivedAt = QDateTime::currentDateTimeUtc();

        stateRequest.correlationId = "output-state-1";
        stateRequest.scope = scope;
        stateRequest.sessionGeneration = policyRequest.sessionGeneration;
        stateRequest.expectedEpoch = epoch;
        stateRequest.expectedMappingDigest = mappingDigest;

        idleState.scope = scope;
        idleState.sessionGeneration = stateRequest.sessionGeneration;
        idleState.epoch = epoch;
        idleState.mappingDigest = mappingDigest;
        idleState.state = Data::RuntimeOutputState::Idle;
        idleState.outputGeneration = policy.currentOutputGeneration;
        idleState.controllerTimestampNs = 1000;
        idleState.receivedAt = policy.receivedAt;

        Data::RuntimeOutputValueWrite first;
        first.resourceId.value = QByteArray::fromHex("1000000000000001");
        first.bitWidth = 8;
        first.value.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
        first.value.value = QVariant::fromValue<qulonglong>(1);
        first.value.typeIdentity = "u8";

        Data::RuntimeOutputValueWrite second = first;
        second.resourceId.value = QByteArray::fromHex("1000000000000002");
        second.value.value = QVariant::fromValue<qulonglong>(2);

        transactionRequest.operationId = operationId;
        transactionRequest.scope = scope;
        transactionRequest.sessionGeneration = policyRequest.sessionGeneration;
        transactionRequest.expectedEpoch = epoch;
        transactionRequest.expectedMappingDigest = mappingDigest;
        transactionRequest.expectedCompleteGroupRecordDigest = groupRecordDigest;
        transactionRequest.expectedCompleteResourceCount = 2;
        transactionRequest.expectedRecoveryPolicy = policy.recoveryPolicy;
        transactionRequest.expectedMaximumTtlCycles = policy.maximumTtlCycles;
        transactionRequest.expectedOutputGeneration = policy.currentOutputGeneration;
        transactionRequest.ttlCycles = 5;
        transactionRequest.consistencyGroupId = groupId;
        transactionRequest.completeGroupWrites = {first, second};

        appliedState = idleState;
        appliedState.state = Data::RuntimeOutputState::OverrideActive;
        appliedState.resultFlags = Data::RuntimeOutputTransactionResultFlag::OverrideActive;
        appliedState.operationId = operationId;
        appliedState.appliedCycle = 100;
        appliedState.expiryCycle = 105;
        appliedState.outputGeneration = 10;
        appliedState.consistencyGroupId = groupId;
        appliedState.ttlCycles = transactionRequest.ttlCycles;
        appliedState.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::ReturnTask;
        appliedState.valueCount = 2;
        appliedState.controllerTimestampNs = 2000;
    }
};

class TestDeviceImportJob final : public DeviceImportJob
{
public:
    using DeviceImportJob::DeviceImportJob;

    void start()
    {
        setState(DeviceImportState::Running);
        setProgress(1, 2);
    }

    void cancel() final
    {
        setState(DeviceImportState::Canceling);
        Data::DeviceImportResult result;
        result.requestedFiles = 2;
        result.canceled = true;
        finish(result);
    }
};

static Data::DeviceAdapterManifest testDeviceAdapterManifest(
    Data::DeviceAdapterQualification qualification = Data::DeviceAdapterQualification::Qualified,
    const QString &version = "2.1.0")
{
    Data::DeviceSignalBinding binding;
    binding.kind = Data::DeviceSignalBindingKind::ProcessDataObject;
    binding.pdoDirection = Data::PdoDirection::Rx;
    binding.pdoIndex = 0x1600;
    binding.objectIndex = 0x7001;
    binding.objectSubIndex = 0;
    binding.physicalType = Data::EtherCATDataType::Integer32;
    binding.bitWidth = 32;
    binding.byteOrder = Data::DeviceByteOrder::LittleEndian;

    Data::SemanticSignalDefinition signal;
    signal.id = {"urn:example.test:signal/custom.axis.target-velocity"};
    signal.displayName = "Target velocity";
    signal.description = "Adapter-owned velocity command";
    signal.capabilities
        = {{"example.test.capability/axis.velocity"},
           {"example.test.capability/custom-diagnostics"}};
    signal.direction = Data::SemanticSignalDirection::Output;
    signal.access = Data::SemanticSignalAccess::WriteOnly;
    signal.bindings = {binding};
    signal.valueMetadata.unit = "rpm";
    signal.valueMetadata.scale = 0.1;
    signal.valueMetadata.offset = -1.0;
    signal.valueMetadata.hasMinimum = true;
    signal.valueMetadata.minimum = -3000.0;
    signal.valueMetadata.hasMaximum = true;
    signal.valueMetadata.maximum = 3000.0;
    signal.valueMetadata.hasStep = true;
    signal.valueMetadata.step = 0.1;
    signal.valueMetadata.enumValues = {{0, "stopped", "Stopped"}};
    signal.hasSafeValue = true;
    signal.safeValue = 0;
    signal.manualControl.policyId = "manual.axis.hold-to-run";
    signal.manualControl.allowed = true;
    signal.manualControl.requiresExclusiveControl = true;
    signal.manualControl.holdToRun = true;
    signal.manualControl.commandTimeoutMs = 250;
    signal.manualControl.timeoutAction = Data::ManualControlTimeoutAction::ControlledStop;

    Data::DeviceAdapterManifest manifest;
    manifest.id = {"org.example.test.adapter/custom-drive"};
    manifest.version = version;
    manifest.displayName = "Vendor Example Drive";
    manifest.description = "Test-only semantic adapter";
    manifest.qualification = qualification;
    manifest.matchPriority = 120;
    manifest.match.vendorId = 0x00a1b2c3;
    manifest.match.productCode = 0x01020304;
    manifest.match.minimumRevision = 0x00020003;
    manifest.match.maximumRevision = 0x00020003;
    manifest.match.exactEsiSha256 = QByteArray::fromHex(
        "1111111111111111111111111111111111111111111111111111111111111111");
    manifest.capabilities = signal.capabilities;
    manifest.semanticSignals = {signal};
    manifest.provenance.sourceId = "test.adapter.catalog";
    manifest.provenance.sourceVersion = "2026.07";
    manifest.provenance.sourceLocation = "tests/custom-drive.adapter.json";
    manifest.provenance.sourceSha256 = QByteArray::fromHex(
        "2222222222222222222222222222222222222222222222222222222222222222");
    manifest.contentSha256 = QByteArray::fromHex(
        "3333333333333333333333333333333333333333333333333333333333333333");
    manifest.evidenceSha256 = QByteArray::fromHex(
        "4444444444444444444444444444444444444444444444444444444444444444");
    manifest.signatureVerified = qualification == Data::DeviceAdapterQualification::Qualified;
    manifest.realHardwareAllowed = qualification == Data::DeviceAdapterQualification::Qualified;
    return manifest;
}

class TestDeviceAdapterProvider final : public DeviceAdapterProvider
{
public:
    explicit TestDeviceAdapterProvider(const QList<Data::DeviceAdapterManifest> &manifests)
        : DeviceAdapterProvider("EtherCAT.DeviceAdapter.Test", "Test device adapters")
        , m_manifests(manifests)
    {}

    QList<Data::DeviceAdapterManifest> adapterManifests() const final { return m_manifests; }

    std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &adapterId, const QString &version) const final
    {
        const auto found = std::find_if(
            m_manifests.cbegin(),
            m_manifests.cend(),
            [&adapterId, &version](const Data::DeviceAdapterManifest &manifest) {
                return manifest.id == adapterId && manifest.version == version;
            });
        if (found == m_manifests.cend())
            return std::nullopt;
        return *found;
    }

    Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &request) const final
    {
        lastResolutionRequest = request;
        ++resolutionCount;
        return resolutionResult;
    }

    void replaceManifests(const QList<Data::DeviceAdapterManifest> &manifests)
    {
        m_manifests = manifests;
        emit adapterManifestsChanged();
    }

    mutable Data::DeviceAdapterResolutionRequest lastResolutionRequest;
    mutable int resolutionCount = 0;
    Data::DeviceAdapterResolutionResult resolutionResult;

private:
    QList<Data::DeviceAdapterManifest> m_manifests;
};

void EtherCATCoreTests::testAutomationServiceValueLookup()
{
    TestAutomationService service;
    AutomationContextSnapshot snapshot;
    snapshot.scope = {Data::NodeId::create(), Data::NodeId::create()};
    snapshot.controllerId = automationControllerId(snapshot.scope);
    snapshot.identitySource = "ide-project-master";
    snapshot.project.id = snapshot.scope.projectId;
    snapshot.project.valid = true;
    snapshot.connection.scope = snapshot.scope;
    snapshot.mock = true;
    service.snapshots = {snapshot};

    const std::optional<AutomationContextSnapshot> first = service.context(snapshot.controllerId);
    QVERIFY(first);
    QCOMPARE(*first, snapshot);
    QCOMPARE(service.readCount, 1);

    service.snapshots.clear();
    QVERIFY(!service.context(snapshot.controllerId));
    QCOMPARE(service.readCount, 2);
    QCOMPARE(snapshot.identitySource, "ide-project-master");
}

class TestPropertyPageProvider final : public PropertyPageProvider
{
public:
    TestPropertyPageProvider()
        : PropertyPageProvider("EtherCAT.Test.Pages", "Test pages")
    {}

    QList<PropertyPageDescriptor> pages(const PropertyPageContext &context) const final
    {
        if (context.nodeKind != WorkbenchNodeKind::Device)
            return {};
        return {{Utils::Id("EtherCAT.Test.General"), "General", 10}};
    }

    QWidget *createPage(Utils::Id pageId, QWidget *parent) final
    {
        if (pageId != Utils::Id("EtherCAT.Test.General"))
            return nullptr;
        return new QWidget(parent);
    }

    void updatePage(Utils::Id pageId, QWidget *page, const PropertyPageContext &context) final
    {
        if (pageId == Utils::Id("EtherCAT.Test.General") && page)
            page->setObjectName(context.nodeId.toString());
    }
};

class TestControllerConnectionProvider final : public ControllerConnectionProvider
{
public:
    TestControllerConnectionProvider()
        : TestControllerConnectionProvider(
              "EtherCAT.Connection.Test",
              "Test controller",
              "192.168.3.101:15200",
              {"control", "events", "bulk"})
    {}

    TestControllerConnectionProvider(
        Utils::Id id,
        const QString &displayName,
        const QString &endpointSummary,
        const QStringList &channelIds,
        Data::NodeId profileId = {})
        : ControllerConnectionProvider(id, displayName)
        , m_channelIds(channelIds)
    {
        m_profile.id = profileId.isNull() ? Data::NodeId::create() : profileId;
        m_profile.displayName = displayName + " default";
        m_profile.endpointSummary = endpointSummary;
        m_profile.configured = true;
        m_profile.supported = true;
        m_profile.defaultProfile = true;
    }

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &scope) const final
    {
        if (scope.projectId.isNull() || scope.masterId.isNull())
            return {};
        return {m_profile};
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final { return m_snapshot; }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &request) final
    {
        if (!isAvailable())
            return Utils::ResultError("Connection provider is unavailable");
        if (request.scope.projectId.isNull() || request.scope.masterId.isNull()
            || request.profileId != m_profile.id || !m_profile.configured || !m_profile.supported)
            return Utils::ResultError("Invalid controller profile");
        if (m_snapshot.state != Data::ControllerConnectionState::Disconnected)
            return Utils::ResultError("Connection provider is busy");

        m_snapshot = {};
        m_snapshot.scope = request.scope;
        m_snapshot.profileId = request.profileId;
        m_snapshot.endpointSummary = m_profile.endpointSummary;
        m_snapshot.state = Data::ControllerConnectionState::Connecting;
        m_snapshot.readOnly = true;
        m_snapshot.sessionGeneration = ++m_generation;
        for (qsizetype index = 0; index < m_channelIds.size(); ++index) {
            const QString &channelId = m_channelIds.at(index);
            m_snapshot.channels.append(
                {channelId,
                 channelId.toUpper(),
                 index == 0 ? Data::ControllerChannelState::Connecting
                            : Data::ControllerChannelState::Disconnected,
                 channelId == "control" ? 4096 : 65536,
                 {},
                 index == 0 ? QString("Connecting") : QString()});
        }
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> disconnectFromController() final
    {
        if (m_snapshot.state == Data::ControllerConnectionState::Disconnected)
            return Utils::ResultOk;
        m_snapshot.state = Data::ControllerConnectionState::Disconnected;
        m_snapshot.sessionGeneration = ++m_generation;
        m_snapshot.session.reset();
        m_snapshot.lastHeartbeatAt = {};
        for (Data::ControllerChannelStatus &channel : m_snapshot.channels)
            channel.state = Data::ControllerChannelState::Disconnected;
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> refreshController() final
    {
        if (!isAvailable())
            return Utils::ResultError("Connection provider is unavailable");
        if (m_snapshot.state != Data::ControllerConnectionState::Connected
            && m_snapshot.state != Data::ControllerConnectionState::Degraded) {
            return Utils::ResultError("Controller is not connected");
        }
        m_snapshot.updatedAt = QDateTime::currentDateTimeUtc();
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    void setProfileConfigured(bool configured)
    {
        if (m_profile.configured == configured)
            return;
        m_profile.configured = configured;
        m_profile.configurationIssue = configured ? QString()
                                                  : QString("Controller profile is incomplete");
        emit connectionProfilesChanged();
    }

    void setProfileSupported(bool supported)
    {
        if (m_profile.supported == supported)
            return;
        m_profile.supported = supported;
        m_profile.configurationIssue = supported ? QString()
                                                 : QString("Controller profile is not supported");
        emit connectionProfilesChanged();
    }

    void completeHandshake()
    {
        m_snapshot.state = Data::ControllerConnectionState::Connected;
        m_snapshot.protocolVersion = Data::ControllerProtocolVersion{1, 9};
        m_snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        m_snapshot.updatedAt = m_snapshot.connectedAt;
        m_snapshot.lastHeartbeatAt = m_snapshot.connectedAt;
        m_snapshot.session = Data::ControllerSessionSummary{17, 42, 0, 5000, false};
        for (Data::ControllerChannelStatus &channel : m_snapshot.channels) {
            channel.state = Data::ControllerChannelState::Connected;
            channel.lastActivityAt = m_snapshot.connectedAt;
            channel.detail.clear();
        }

        Data::ControllerStateSummary state;
        state.serviceState = Data::ControllerServiceState::Shutdown;
        state.severity = Data::ControllerSeverity::None;
        state.ready = true;
        state.controllerBootId = 42;
        m_snapshot.controllerState = state;

        Data::ControllerCapabilitySummary capability;
        capability.maximumSlaves = 64;
        capability.minimumCycleTimeNs = 125000;
        capability.maximumCyclicFrames = 8;
        capability.maximumProcessInputBytes = 4096;
        capability.maximumProcessOutputBytes = 4096;
        capability.descriptorSha256 = QByteArray::fromHex(
            "74ea5e67b3e1d7ba575339b636abb5432ecf6790d3504d32ce1756bcfec49568");
        capability.controlLease = true;
        capability.resumablePush = true;
        capability.transactionalBulk = true;
        capability.capabilityQuery = true;
        capability.exactAlarmReplay = true;
        capability.linkDiagnostics = true;
        capability.timeCorrelation = true;
        capability.firmwareUpdate = true;
        capability.coe = true;
        capability.distributedClocks = true;
        m_snapshot.capability = capability;

        Data::ControllerPackageSummary package;
        package.stagedSlot = Data::ControllerSlot::A;
        package.stagedGeneration = 11;
        package.stagedConfigurationId = 810;
        package.activeSlot = Data::ControllerSlot::A;
        package.activeGeneration = 11;
        package.activeConfigurationId = 810;
        package.controllerState = Data::ControllerPackageState::Empty;
        package.controllerBootId = 42;
        m_snapshot.package = package;

        Data::ControllerFirmwareSummary firmware;
        firmware.state = Data::ControllerFirmwareState::Confirmed;
        firmware.activeSlot = Data::ControllerSlot::A;
        firmware.previousSlot = Data::ControllerSlot::B;
        firmware.targetSlot = Data::ControllerSlot::A;
        firmware.packageVerified = true;
        firmware.confirmed = true;
        firmware.progressPerMille = 1000;
        firmware.generation = 2517;
        firmware.updatedAt = m_snapshot.connectedAt;
        m_snapshot.firmware = firmware;
        emit connectionSnapshotChanged();
    }

    void degradePush()
    {
        m_snapshot.state = Data::ControllerConnectionState::Degraded;
        m_snapshot.channels[1].state = Data::ControllerChannelState::Failed;
        m_snapshot.channels[1].detail = "Push channel unavailable";
        emit connectionSnapshotChanged();
    }

    void failProtocol()
    {
        Data::ControllerOperationError error;
        error.source = Data::ControllerErrorSource::Protocol;
        error.channelId = "events";
        error.operation = Data::ControllerOperation::SubscribeEvents;
        error.codeName = "UNEXPECTED_PUSH_FRAME";
        error.requestId = 73;
        error.occurredAt = QDateTime::currentDateTimeUtc();
        error.retryDisposition = Data::ControllerRetryDisposition::Reconnect;
        error.summary = "Unexpected push frame";
        m_snapshot.state = Data::ControllerConnectionState::Failed;
        m_snapshot.lastError = error;
        emit connectionSnapshotChanged();
    }

private:
    Data::ControllerConnectionProfile m_profile;
    QStringList m_channelIds;
    Data::ControllerConnectionSnapshot m_snapshot;
    quint64 m_generation = 0;
};

class TestScanProvider final : public ScanProvider
{
public:
    TestScanProvider()
        : ScanProvider("EtherCAT.Scan.Test", "Test scanner")
    {}

    Data::ScanState scanState() const final { return m_progress.state; }
    Data::ScanProgress scanProgress() const final { return m_progress; }
    std::optional<Data::ScanResult> lastScanResult() const final { return m_result; }
    QString lastScanError() const final { return m_error; }

    Utils::Result<> startScan(const Data::ScanRequest &request) final
    {
        if (request.projectId.isNull() || request.masterId.isNull())
            return Utils::ResultError("Invalid scan request");
        if (m_progress.state != Data::ScanState::Idle)
            return Utils::ResultError("Scanner is busy");
        m_request = request;
        m_progress = {Data::ScanState::Preparing, 1, 4, 0, "Preparing"};
        emit scanStateChanged(m_progress.state);
        emit scanProgressChanged(m_progress);
        return Utils::ResultOk;
    }

    void complete()
    {
        Data::ScannedSlave slave;
        slave.id = Data::NodeId::create();
        slave.position = 0;
        slave.identity = {2, 0x1234, 1};
        slave.serialNumber = 17;
        slave.name = "Mock Slave";

        Data::ScanResult result;
        result.snapshot.id = Data::NodeId::create();
        result.snapshot.projectId = m_request.projectId;
        result.snapshot.masterId = m_request.masterId;
        result.snapshot.slaves = {slave};
        result.snapshot.complete = true;
        result.snapshot.mock = true;
        result.comparison.projectId = m_request.projectId;
        result.comparison.masterId = m_request.masterId;
        result.comparison.differences = {
            {Data::TopologyDifferenceKind::Added,
             Data::DifferenceSeverity::Information,
             {},
             slave.id,
             -1,
             0,
             "Added slave",
             "Mock Slave"}};
        result.comparison.acceptAllowed = true;
        m_result = result;
        m_progress = {Data::ScanState::Completed, 4, 4, 1, "Completed"};
        emit scanProgressChanged(m_progress);
        emit scanResultChanged();
        emit scanStateChanged(m_progress.state);
        emit scanFinished(m_progress.state);
    }

    void cancelScan() final
    {
        if (m_progress.state == Data::ScanState::Idle)
            return;
        m_progress.state = Data::ScanState::Cancelled;
        emit scanStateChanged(m_progress.state);
        emit scanFinished(m_progress.state);
    }

    void clearScanResult() final
    {
        m_result.reset();
        m_error.clear();
        m_progress = {};
        emit scanResultChanged();
        emit scanStateChanged(m_progress.state);
    }

private:
    Data::ScanProgress m_progress;
    Data::ScanRequest m_request;
    std::optional<Data::ScanResult> m_result;
    QString m_error;
};

class TestDiagnosticsProvider final : public DiagnosticsProvider
{
public:
    TestDiagnosticsProvider()
        : DiagnosticsProvider("EtherCAT.Diagnostics.Test", "Test diagnostics")
    {
        m_limits = {4, 5, 10, 100};
    }

    Data::DiagnosticsStreamState streamState() const final { return m_state; }
    Data::DiagnosticsRequest activeRequest() const final { return m_request; }
    std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const final { return m_snapshot; }
    QList<Data::DiagnosticEvent> events() const final { return m_events; }
    QList<Data::DiagnosticTrendSample> trendSamples() const final { return m_trend; }
    Data::DiagnosticsLimits limits() const final { return m_limits; }
    QString lastDiagnosticsError() const final { return m_error; }

    Utils::Result<> startMonitoring(const Data::DiagnosticsRequest &request) final
    {
        if (request.projectId.isNull() || request.masterId.isNull())
            return Utils::ResultError("Invalid diagnostics request");
        if (m_state != Data::DiagnosticsStreamState::Stopped)
            return Utils::ResultError("Diagnostics provider is busy");
        m_request = request;
        m_state = Data::DiagnosticsStreamState::Starting;
        emit streamStateChanged(m_state);
        m_state = Data::DiagnosticsStreamState::Running;
        emit streamStateChanged(m_state);
        return Utils::ResultOk;
    }

    void stopMonitoring() final
    {
        if (m_state == Data::DiagnosticsStreamState::Stopped)
            return;
        const bool alreadyFinished = m_state == Data::DiagnosticsStreamState::Failed;
        m_state = Data::DiagnosticsStreamState::Stopping;
        emit streamStateChanged(m_state);
        m_state = Data::DiagnosticsStreamState::Stopped;
        m_request = {};
        emit streamStateChanged(m_state);
        if (!alreadyFinished)
            emit monitoringStopped();
    }

    Utils::Result<> requestRunMode(Data::DiagnosticsRunMode mode) final
    {
        if (m_state != Data::DiagnosticsStreamState::Running || !m_snapshot)
            return Utils::ResultError("Diagnostics provider is not running");
        m_snapshot->runMode = mode;
        emit diagnosticsSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> acknowledgeAlarm(const Data::NodeId &eventId) final
    {
        const auto alarm = std::find_if(
            m_events.begin(), m_events.end(), [&eventId](const Data::DiagnosticEvent &event) {
                return event.id == eventId && event.lifecycle == Data::AlarmLifecycle::Active;
            });
        if (alarm == m_events.end())
            return Utils::ResultError("Active alarm not found");
        alarm->lifecycle = Data::AlarmLifecycle::Acknowledged;
        alarm->acknowledgedAt = QDateTime::currentDateTimeUtc();
        emit diagnosticEventsChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> clearRecoveredEvents() final
    {
        m_events.erase(
            std::remove_if(
                m_events.begin(),
                m_events.end(),
                [](const Data::DiagnosticEvent &event) {
                    return event.lifecycle == Data::AlarmLifecycle::Recovered;
                }),
            m_events.end());
        emit diagnosticEventsChanged();
        return Utils::ResultOk;
    }

    void publish()
    {
        const Data::NodeId slaveId = Data::NodeId::create();
        Data::DiagnosticsSnapshot snapshot;
        snapshot.projectId = m_request.projectId;
        snapshot.masterId = m_request.masterId;
        snapshot.generation = 7;
        snapshot.capturedAt = QDateTime::currentDateTimeUtc();
        snapshot.mock = true;
        snapshot.runMode = Data::DiagnosticsRunMode::Run;
        snapshot.masterState = Data::EtherCATState::Operational;
        snapshot.masterAlStatusText = "No error";
        snapshot.workingCounter = {4, 3, Data::WorkingCounterState::Incomplete, 2};
        snapshot.masterPorts = {{0, Data::LinkState::Up, true, 1, 0, 0}};
        snapshot.frameErrors = {1, 2, 3, 4, 5, 6};
        snapshot.distributedClock = {Data::DcSyncState::Synchronized, 17, 30, 4, 1};
        snapshot.cycle = {125000, 125010, 124990, 125020, 10, 20000, 8, 0};
        snapshot.slaves = {
            {slaveId,
             0,
             "Mock Slave",
             Data::EtherCATState::Operational,
             false,
             0,
             "No error",
             Data::WorkingCounterState::Valid,
             {{0, Data::LinkState::Up, true, 0, 0, 0}},
             {},
             {Data::DcSyncState::Synchronized, 5, 12, 2, 0},
             snapshot.capturedAt}};
        snapshot.activeAlarmCount = 1;
        snapshot.unacknowledgedAlarmCount = 1;
        snapshot.sourceSampleCount = 10;
        snapshot.coalescedSampleCount = 9;
        m_snapshot = snapshot;

        Data::DiagnosticEvent alarm;
        alarm.id = Data::NodeId::create();
        alarm.sequence = 11;
        alarm.occurredAt = snapshot.capturedAt;
        alarm.nodeId = slaveId;
        alarm.kind = Data::DiagnosticEventKind::Alarm;
        alarm.severity = Data::DiagnosticSeverity::Error;
        alarm.lifecycle = Data::AlarmLifecycle::Active;
        alarm.code = "MOCK-WKC";
        alarm.summary = "Working Counter mismatch";
        alarm.repeatCount = 2;
        m_events = {alarm};
        m_trend = {{12, snapshot.capturedAt, 3, 125010, 10, 20000, 17}};
        emit diagnosticsSnapshotChanged();
        emit diagnosticEventsChanged();
        emit diagnosticTrendChanged();
    }

    void recoverAlarm()
    {
        m_events[0].lifecycle = Data::AlarmLifecycle::Recovered;
        m_events[0].recoveredAt = QDateTime::currentDateTimeUtc();
        emit diagnosticEventsChanged();
    }

    void fail(const QString &error)
    {
        m_error = error;
        m_state = Data::DiagnosticsStreamState::Failed;
        emit streamStateChanged(m_state);
        emit monitoringStopped();
    }

private:
    Data::DiagnosticsStreamState m_state = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_request;
    std::optional<Data::DiagnosticsSnapshot> m_snapshot;
    QList<Data::DiagnosticEvent> m_events;
    QList<Data::DiagnosticTrendSample> m_trend;
    Data::DiagnosticsLimits m_limits;
    QString m_error;
};

void EtherCATCoreTests::testMetadataAndServices()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        "ethercatcore");
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATCore"));
    QVERIFY(!spec->version().isEmpty());
    QVERIFY(!spec->compatVersion().isEmpty());
    QVERIFY(!spec->hasError());
    const QList<ExtensionSystem::PluginDependency> dependencies = spec->dependencies();
    const bool hasCoreDependency = std::any_of(
        dependencies.cbegin(),
        dependencies.cend(),
        [](const ExtensionSystem::PluginDependency &dependency) {
            return dependency.id == "core"
                   && dependency.type == ExtensionSystem::PluginDependency::Required;
        });
    QVERIFY(hasCoreDependency);

    QVERIFY(ExtensionSystem::PluginManager::getObject<SelectionService>());
    QVERIFY(ExtensionSystem::PluginManager::getObject<StateService>());
    QVERIFY(ExtensionSystem::PluginManager::getObject<ProviderRegistry>());
}

void EtherCATCoreTests::testMockUiVisibility()
{
    static constexpr char environmentVariable[] = "QTC_ETHER_CAT_ENABLE_MOCK_UI";
    const bool wasSet = qEnvironmentVariableIsSet(environmentVariable);
    const QByteArray previousValue = qgetenv(environmentVariable);
    const QScopeGuard restoreEnvironment([wasSet, previousValue] {
        if (wasSet)
            qputenv(environmentVariable, previousValue);
        else
            qunsetenv(environmentVariable);
    });

    qunsetenv(environmentVariable);
    QVERIFY(isMockUiEnabled());

    qputenv(environmentVariable, "0");
    QVERIFY(!isMockUiEnabled());

    qputenv(environmentVariable, "1");
    QVERIFY(isMockUiEnabled());

    qputenv(environmentVariable, "true");
    QVERIFY(!isMockUiEnabled());
}

void EtherCATCoreTests::testNodeIdRoundTrip()
{
    const Data::NodeId created = Data::NodeId::create();
    QVERIFY(!created.isNull());
    QCOMPARE(Data::NodeId::fromString(created.toString()), created);
    QCOMPARE(qHash(Data::NodeId::fromString(created.toString())), qHash(created));
    QVERIFY(Data::NodeId::fromString("not-a-node-id").isNull());
}

void EtherCATCoreTests::testProjectSnapshotValueSemantics()
{
    using RenameStructuralNodeMethod = Utils::Result<> (
        ProjectService::*)(const Data::NodeId &, const Data::NodeId &, const QString &);
    static_assert(
        std::is_same_v<decltype(&ProjectService::renameStructuralNode), RenameStructuralNodeMethod>);
    using SetMasterConfigurationMethod = Utils::Result<> (ProjectService::*)(
        const Data::NodeId &, const Data::NodeId &, const Data::MasterConfiguration &);
    static_assert(std::is_same_v<
                  decltype(&ProjectService::setMasterConfiguration),
                  SetMasterConfigurationMethod>);
    using SetDeviceAdapterSelectionMethod = Utils::Result<> (ProjectService::*)(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &);
    static_assert(std::is_same_v<
                  decltype(&ProjectService::setDeviceAdapterSelection),
                  SetDeviceAdapterSelectionMethod>);
    using SetMasterBindingArtifactMethod = Utils::Result<> (
        ProjectService::*)(const Data::NodeId &, const Data::SemanticBindingArtifactReference &);
    static_assert(std::is_same_v<
                  decltype(&ProjectService::setMasterBindingArtifact),
                  SetMasterBindingArtifactMethod>);

    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId slaveId = Data::NodeId::create();
    Data::ProjectSnapshot snapshot{
        projectId,
        "Line 1",
        1,
        "Embed Labs 20.0.1",
        {{projectId, {}, Data::ProjectNodeKind::Project, "Line 1"},
         {targetId, projectId, Data::ProjectNodeKind::Target, "Target Controller"}},
        true,
        true,
        false,
        {},
        {},
        {},
        {"binding/test",
         QByteArray(32, '\x31'),
         QByteArray(32, '\x32'),
         {{slaveId, "embedlabs:project:device:test"}}},
    };

    const Data::ProjectSnapshot copy = snapshot;
    QCOMPARE(copy, snapshot);
    snapshot.nodes[1].name = "Offline Target";
    QVERIFY(copy != snapshot);
    QCOMPARE(copy.nodes[1].parentId, projectId);
    QCOMPARE(
        copy.masterBindingArtifact.projectDeviceBindings.first().slaveId,
        slaveId);
    QCOMPARE(
        copy.masterBindingArtifact.projectDeviceBindings.first().projectDeviceId,
        QString("embedlabs:project:device:test"));

    Data::OfflineSlaveConfiguration slave;
    slave.id = slaveId;
    slave.masterId = targetId;
    slave.position = 0;
    slave.identity = {2, 0x1234, 1};
    slave.name = "Offline Slave";
    slave.esiSha256 = QByteArray(32, '\x41');
    slave.adapterSelection = {
        Data::DeviceAdapterId{"com.embedlabs.test.adapter"},
        "1.0.0",
        QByteArray(32, '\x42'),
        "default",
        {{0, 0x00010001, 0, 0}},
    };
    snapshot.slaves.append(slave);
    QVERIFY(copy != snapshot);
    QCOMPARE(copy.masterBindingArtifact.artifactId, QString("binding/test"));
}

void EtherCATCoreTests::testRuntimeResourceValueSemantics()
{
    const Data::ControllerConnectionScope scope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    const Data::RuntimeResourceId resourceId{QByteArray::fromHex("1020304050607080")};
    const Data::RuntimeComponentInstanceId componentId{QByteArray::fromHex("90a0b0c0d0e0f000")};
    const Data::RuntimeComponentInstanceId parentId{QByteArray::fromHex("1011121314151617")};
    const Data::RuntimeConsistencyGroupId groupId{QByteArray::fromHex("0102030405060708")};
    QVERIFY(resourceId.isValid());
    QVERIFY(componentId.isValid());
    QVERIFY(groupId.isValid());
    QCOMPARE(qHash(Data::RuntimeResourceId(resourceId)), qHash(resourceId));

    Data::RuntimeResourceDescriptor descriptor;
    descriptor.id = resourceId;
    descriptor.componentInstanceId = componentId;
    descriptor.parentInstanceId = parentId;
    descriptor.instanceOrdinal = 3;
    descriptor.consistencyGroupId = groupId;
    descriptor.displayName = "Primary sample";
    descriptor.description = "Provider-neutral runtime value";
    descriptor.primitiveType = Data::RuntimeResourcePrimitiveType::Opaque;
    descriptor.valueTypeIdentity = "org.example.runtime/custom-u24";
    descriptor.bitWidth = 24;
    descriptor.direction = Data::RuntimeResourceDirection::Input;
    descriptor.access = Data::RuntimeResourceAccess::ReadOnly;
    descriptor.processImageBitOffset = 72;
    descriptor.processImageBitLength = 24;
    descriptor.qualityMask = 0x000f;
    descriptor.unit = "unit";

    Data::RuntimeResourceTypedValue safeValue;
    safeValue.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    safeValue.value = QVariant::fromValue<qulonglong>(0);
    descriptor.safeValue = safeValue;

    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = 41;
    epoch.activePackageSlot = Data::ControllerSlot::B;
    epoch.activePackageGeneration = 79;
    epoch.configurationId = 83;
    epoch.topologyGeneration = 2;
    epoch.runtimeGeneration = 4;
    epoch.catalogRevision = 5;
    epoch.topologyIdentity = QByteArray::fromHex("abcdef0123456789");

    Data::RuntimeResourceCatalog catalog;
    catalog.scope = scope;
    catalog.sessionGeneration = 7;
    catalog.epoch = epoch;
    catalog.receivedAt = QDateTime::currentDateTimeUtc();
    catalog.resources = {descriptor};
    const Data::RuntimeResourceCatalog catalogCopy = catalog;
    QCOMPARE(catalogCopy, catalog);

    Data::RuntimeResourceTypedValue opaqueValue;
    opaqueValue.primitiveType = Data::RuntimeResourcePrimitiveType::Opaque;
    opaqueValue.typeIdentity = descriptor.valueTypeIdentity;
    opaqueValue.opaqueRepresentation = QByteArray::fromHex("123456");

    Data::RuntimeResourceQuality quality;
    quality.state = Data::RuntimeResourceQualityState::Uncertain;
    quality.flags = 0x0005;
    quality.opaqueCode = QByteArray::fromHex("8001");
    quality.detail = "Provider quality retained";

    Data::RuntimeResourceSample sample;
    sample.resourceId = resourceId;
    sample.consistencyGroupId = groupId;
    sample.value = opaqueValue;
    sample.quality = quality;
    sample.valueSequence = 9;
    sample.controllerTimestampNs = 123456789;

    Data::RuntimeResourceSnapshot snapshot;
    snapshot.scope = scope;
    snapshot.sessionGeneration = catalog.sessionGeneration;
    snapshot.epoch = catalog.epoch;
    snapshot.snapshotSequence = 11;
    snapshot.captureCycle = 101;
    snapshot.controllerTimestampNs = sample.controllerTimestampNs;
    snapshot.receivedAt = catalog.receivedAt;
    snapshot.complete = true;
    snapshot.samples = {sample};

    const Data::RuntimeResourceSnapshot snapshotCopy = snapshot;
    QCOMPARE(snapshotCopy, snapshot);
    QCOMPARE(
        snapshotCopy.samples.constFirst().value.opaqueRepresentation, QByteArray::fromHex("123456"));
    QCOMPARE(snapshotCopy.samples.constFirst().quality.opaqueCode, QByteArray::fromHex("8001"));

    snapshot.samples.first().value.primitiveType
        = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    snapshot.samples.first().value.value = QVariant::fromValue<qulonglong>(0x123456);
    snapshot.samples.first().value.opaqueRepresentation.clear();
    QVERIFY(snapshot != snapshotCopy);

    QVERIFY(QMetaType::fromType<Data::RuntimeResourceCatalog>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeResourceSnapshot>().isValid());
}

void EtherCATCoreTests::testRuntimeResourceSnapshotRequestContract()
{
    Data::RuntimeResourceSnapshotRequest request;
    request.correlationId = "targeted-read-1";
    request.scope = {Data::NodeId::create(), Data::NodeId::create()};
    request.sessionGeneration = 7;
    request.expectedEpoch.controllerBootId = 11;
    request.expectedEpoch.activePackageSlot = Data::ControllerSlot::B;
    request.expectedEpoch.activePackageGeneration = 12;
    request.expectedEpoch.configurationId = 13;
    request.expectedEpoch.topologyGeneration = 14;
    request.expectedEpoch.runtimeGeneration = 15;
    request.expectedEpoch.catalogRevision = 16;
    request.expectedEpoch.topologyIdentity = QByteArray::fromHex("0102030405060708");
    request.resourceIds = {
        {QByteArray::fromHex("1000000000000001")},
        {QByteArray::fromHex("1000000000000041")},
    };
    QVERIFY(request.isValid());
    QCOMPARE(Data::RuntimeResourceSnapshotRequest(request), request);

    Data::RuntimeResourceSnapshotResult success;
    success.request = request;
    Data::RuntimeResourceSnapshot snapshot;
    snapshot.scope = request.scope;
    snapshot.sessionGeneration = request.sessionGeneration;
    snapshot.epoch = request.expectedEpoch;
    snapshot.complete = true;
    Data::RuntimeResourceSample firstSample;
    firstSample.resourceId = request.resourceIds.at(0);
    Data::RuntimeResourceSample secondSample;
    secondSample.resourceId = request.resourceIds.at(1);
    snapshot.samples = {firstSample, secondSample};
    success.snapshot = snapshot;
    QVERIFY(success.isValid());
    QCOMPARE(Data::RuntimeResourceSnapshotResult(success), success);
    Data::RuntimeResourceSnapshotResult invalidSuccess = success;
    invalidSuccess.snapshot->complete = false;
    QVERIFY(!invalidSuccess.isValid());
    invalidSuccess = success;
    invalidSuccess.snapshot->scope.masterId = Data::NodeId::create();
    QVERIFY(!invalidSuccess.isValid());
    invalidSuccess = success;
    std::swap(invalidSuccess.snapshot->samples[0], invalidSuccess.snapshot->samples[1]);
    QVERIFY(!invalidSuccess.isValid());

    Data::RuntimeResourceSnapshotResult failure;
    failure.request = request;
    Data::ControllerOperationError error;
    error.operation = Data::ControllerOperation::QueryRuntimeResourceSnapshot;
    failure.error = error;
    QVERIFY(failure.isValid());
    Data::RuntimeResourceSnapshotResult invalidFailure = failure;
    invalidFailure.error->operation = Data::ControllerOperation::Refresh;
    QVERIFY(!invalidFailure.isValid());
    failure.snapshot = Data::RuntimeResourceSnapshot{};
    QVERIFY(!failure.isValid());

    Data::RuntimeResourceSnapshotRequest invalid = request;
    invalid.correlationId.clear();
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.resourceIds.clear();
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.resourceIds.append({QByteArray::fromHex("1000000000000042")});
    for (int index = invalid.resourceIds.size(); index < 65; ++index) {
        invalid.resourceIds.append({QByteArray::number(index).rightJustified(8, '\0')});
    }
    QVERIFY(!invalid.isValid());
    invalid = request;
    std::swap(invalid.resourceIds[0], invalid.resourceIds[1]);
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.resourceIds[1] = invalid.resourceIds[0];
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.correlationId = QStringLiteral("targeted\nread");
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.correlationId = QString(129, QLatin1Char('a'));
    QVERIFY(!invalid.isValid());

    QVERIFY(QMetaType::fromType<Data::RuntimeResourceSnapshotRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeResourceSnapshotResult>().isValid());
}

void EtherCATCoreTests::testRuntimeSemanticMappingAttestationContract()
{
    using SupportsMethod = bool (ControllerConnectionProvider::*)() const;
    using CachedMethod = std::optional<Data::RuntimeSemanticMappingAttestation> (
        ControllerConnectionProvider::*)() const;
    using RequestMethod = Utils::Result<> (ControllerConnectionProvider::*)(
        const Data::RuntimeSemanticMappingAttestationRequest &);
    static_assert(std::is_same_v<
                  decltype(&ControllerConnectionProvider::
                               supportsRuntimeSemanticMappingAttestation),
                  SupportsMethod>);
    static_assert(std::is_same_v<
                  decltype(&ControllerConnectionProvider::runtimeSemanticMappingAttestation),
                  CachedMethod>);
    static_assert(std::is_same_v<
                  decltype(&ControllerConnectionProvider::
                               requestRuntimeSemanticMappingAttestation),
                  RequestMethod>);

    Data::RuntimeSemanticMappingProof proof;
    proof.formatVersion = 1;
    proof.bindingCount = 56;
    proof.packageSigned = true;
    proof.signatureVerified = true;
    proof.semanticBindingVerified = true;
    proof.trust = Data::RuntimeSemanticMappingTrust::Production;
    proof.packageSha256 = QByteArray(32, '\x11');
    proof.manifestSha256 = QByteArray(32, '\x22');
    proof.mappingSha256 = QByteArray(32, '\x33');
    proof.resourceRecordsSha256 = QByteArray(32, '\x44');
    proof.resourceSectionSha256 = QByteArray(32, '\x55');
    proof.topologySha256 = QByteArray(32, '\x66');
    proof.signingKeyIdSha256 = QByteArray(32, '\x77');
    QVERIFY(proof.isValid());
    QCOMPARE(Data::RuntimeSemanticMappingProof(proof), proof);
    QVERIFY(Data::isValidRuntimeSemanticMappingDigest(proof.packageSha256));
    QVERIFY(Data::runtimeSemanticMappingDigestsEqual(
        proof.packageSha256, QByteArray(32, '\x11')));
    QVERIFY(!Data::runtimeSemanticMappingDigestsEqual(
        proof.packageSha256, QByteArray(32, '\x12')));
    QVERIFY(!Data::runtimeSemanticMappingDigestsEqual(
        proof.packageSha256, QByteArray(31, '\x11')));

    Data::RuntimeSemanticMappingAttestationRequest request;
    request.correlationId = "semantic-attestation-1";
    request.scope = {Data::NodeId::create(), Data::NodeId::create()};
    request.sessionGeneration = 7;
    request.expectedEpoch.controllerBootId = 11;
    request.expectedEpoch.activePackageSlot = Data::ControllerSlot::B;
    request.expectedEpoch.activePackageGeneration = 12;
    request.expectedEpoch.configurationId = 13;
    request.expectedEpoch.topologyGeneration = 14;
    request.expectedEpoch.runtimeGeneration = 15;
    request.expectedEpoch.catalogRevision = 16;
    request.expectedEpoch.topologyIdentity = QByteArray::fromHex("0102030405060708");
    request.expectedProof = proof;
    QVERIFY(request.isValid());
    QVERIFY(Data::isCompleteRuntimeSemanticMappingEpoch(request.expectedEpoch));
    QVERIFY(Data::isValidRuntimeSemanticMappingCorrelationId(request.correlationId));
    QCOMPARE(Data::RuntimeSemanticMappingAttestationRequest(request), request);

    Data::RuntimeSemanticMappingAttestation attestation;
    attestation.scope = request.scope;
    attestation.sessionGeneration = request.sessionGeneration;
    attestation.epoch = request.expectedEpoch;
    attestation.proof = proof;
    attestation.receivedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(attestation.isValid());
    QCOMPARE(Data::RuntimeSemanticMappingAttestation(attestation), attestation);

    Data::RuntimeSemanticMappingAttestationResult success;
    success.request = request;
    success.attestation = attestation;
    QVERIFY(success.isValid());
    QCOMPARE(Data::RuntimeSemanticMappingAttestationResult(success), success);

    Data::RuntimeSemanticMappingAttestationResult failure;
    failure.request = request;
    Data::ControllerOperationError error;
    error.operation = Data::ControllerOperation::QueryRuntimeSemanticMappingAttestation;
    failure.error = error;
    QVERIFY(failure.isValid());
    failure.error->operation = Data::ControllerOperation::QueryRuntimeResourceCatalog;
    QVERIFY(!failure.isValid());
    failure = success;
    failure.error = error;
    QVERIFY(!failure.isValid());

    Data::RuntimeSemanticMappingProof invalidProof = proof;
    invalidProof.formatVersion = 3;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.bindingCount = 0;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.packageSigned = false;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.signatureVerified = false;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.semanticBindingVerified = false;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.trust = Data::RuntimeSemanticMappingTrust::Unknown;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.trust = Data::RuntimeSemanticMappingTrust::Engineering;
    QVERIFY(invalidProof.isValid());

    using DigestMember = QByteArray Data::RuntimeSemanticMappingProof::*;
    constexpr std::array<DigestMember, 7> digestMembers{
        &Data::RuntimeSemanticMappingProof::packageSha256,
        &Data::RuntimeSemanticMappingProof::manifestSha256,
        &Data::RuntimeSemanticMappingProof::mappingSha256,
        &Data::RuntimeSemanticMappingProof::resourceRecordsSha256,
        &Data::RuntimeSemanticMappingProof::resourceSectionSha256,
        &Data::RuntimeSemanticMappingProof::topologySha256,
        &Data::RuntimeSemanticMappingProof::signingKeyIdSha256,
    };
    for (DigestMember member : digestMembers) {
        invalidProof = proof;
        (invalidProof.*member).resize(31);
        QVERIFY(!invalidProof.isValid());
        invalidProof = proof;
        (invalidProof.*member).append('\x01');
        QVERIFY(!invalidProof.isValid());
        invalidProof = proof;
        (invalidProof.*member).fill('\0');
        QVERIFY(!invalidProof.isValid());

        Data::RuntimeSemanticMappingAttestationResult mismatch = success;
        (mismatch.attestation->proof.*member)[31] ^= '\x01';
        QVERIFY(!mismatch.isValid());
    }

    Data::RuntimeSemanticMappingAttestationRequest invalidRequest = request;
    invalidRequest.correlationId.clear();
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.correlationId.prepend(' ');
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.correlationId = QStringLiteral("semantic\nattestation");
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.correlationId = QString(129, QLatin1Char('a'));
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.scope.projectId = {};
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.scope.masterId = {};
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.sessionGeneration = 0;
    QVERIFY(!invalidRequest.isValid());

    QList<Data::RuntimeResourceCatalogEpoch> invalidEpochs;
    Data::RuntimeResourceCatalogEpoch epoch = request.expectedEpoch;
    epoch.controllerBootId = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.activePackageSlot = Data::ControllerSlot::None;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.activePackageGeneration = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.configurationId = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.topologyGeneration = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.runtimeGeneration = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.catalogRevision = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.topologyIdentity.clear();
    invalidEpochs.append(epoch);
    for (const Data::RuntimeResourceCatalogEpoch &invalidEpoch : std::as_const(invalidEpochs)) {
        invalidRequest = request;
        invalidRequest.expectedEpoch = invalidEpoch;
        QVERIFY(!invalidRequest.isValid());
    }

    Data::RuntimeSemanticMappingAttestationResult mismatch = success;
    mismatch.attestation->scope.projectId = Data::NodeId::create();
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->scope.masterId = Data::NodeId::create();
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->sessionGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.controllerBootId;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->epoch.activePackageSlot = Data::ControllerSlot::A;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.activePackageGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.configurationId;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.topologyGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.runtimeGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.catalogRevision;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->epoch.topologyIdentity[0] ^= '\x01';
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->proof.bindingCount;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->proof.trust = Data::RuntimeSemanticMappingTrust::Engineering;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->receivedAt = {};
    QVERIFY(!mismatch.isValid());

    Data::ControllerCapabilitySummary capability;
    QVERIFY(!capability.semanticMappingAttestation);
    capability.semanticMappingAttestation = true;
    QCOMPARE(Data::ControllerCapabilitySummary(capability), capability);

    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingTrust>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingProof>().isValid());
    QVERIFY(
        QMetaType::fromType<Data::RuntimeSemanticMappingAttestationRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingAttestation>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingAttestationResult>().isValid());
}

void EtherCATCoreTests::testRuntimeOutputTransactionContract()
{
    RuntimeOutputFixture fixture;

    QVERIFY(fixture.operationId.isValid());
    QCOMPARE(qHash(Data::RuntimeOutputOperationId(fixture.operationId)), qHash(fixture.operationId));
    QVERIFY(Data::isCompleteRuntimeOutputEpoch(fixture.epoch));
    QVERIFY(Data::isValidRuntimeOutputDigest(fixture.mappingDigest));
    QVERIFY(Data::isValidRuntimeOutputCorrelationId(fixture.policyRequest.correlationId));
    QVERIFY(fixture.policyRequest.isValid());
    QVERIFY(fixture.policy.isValid());
    QVERIFY(fixture.stateRequest.isValid());
    QVERIFY(fixture.idleState.isValid());
    QVERIFY(fixture.transactionRequest.isValid());
    QVERIFY(fixture.appliedState.isValid());

    Data::RuntimeOutputGroupPolicyResult policySuccess;
    policySuccess.request = fixture.policyRequest;
    policySuccess.policy = fixture.policy;
    QVERIFY(policySuccess.isValid());
    QCOMPARE(Data::RuntimeOutputGroupPolicyResult(policySuccess), policySuccess);

    Data::RuntimeOutputGroupPolicyResult policyFailure;
    policyFailure.request = fixture.policyRequest;
    Data::ControllerOperationError policyError;
    policyError.operation = Data::ControllerOperation::QueryRuntimeOutputGroupPolicy;
    policyFailure.error = policyError;
    QVERIFY(policyFailure.isValid());
    policyFailure.error->operation = Data::ControllerOperation::QueryRuntimeResourceCatalog;
    QVERIFY(!policyFailure.isValid());
    policyFailure = policySuccess;
    policyFailure.policy->mappingDigest = QByteArray(32, '\x45');
    QVERIFY(!policyFailure.isValid());
    policyFailure = policySuccess;
    policyFailure.error = policyError;
    QVERIFY(!policyFailure.isValid());

    Data::RuntimeOutputGroupPolicy invalidPolicy = fixture.policy;
    invalidPolicy.manualWriteAllowed = false;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.completeResourceCount = 65;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.maximumTtlCycles = 65536;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.completeGroupRecordDigest.fill('\0');
    QVERIFY(!invalidPolicy.isValid());

    Data::RuntimeOutputGroupPolicyRequest invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.correlationId = " output-policy-1";
    QVERIFY(!invalidPolicyRequest.isValid());
    invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.correlationId = "output\npolicy";
    QVERIFY(!invalidPolicyRequest.isValid());
    invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.consistencyGroupId = {};
    QVERIFY(!invalidPolicyRequest.isValid());
    invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.expectedEpoch.runtimeGeneration = 0;
    QVERIFY(!invalidPolicyRequest.isValid());

    Data::RuntimeOutputTransactionStateResult stateSuccess;
    stateSuccess.request = fixture.stateRequest;
    stateSuccess.state = fixture.idleState;
    QVERIFY(stateSuccess.isValid());
    QCOMPARE(Data::RuntimeOutputTransactionStateResult(stateSuccess), stateSuccess);

    Data::RuntimeOutputTransactionStateResult stateFailure;
    stateFailure.request = fixture.stateRequest;
    Data::ControllerOperationError stateError;
    stateError.operation = Data::ControllerOperation::QueryRuntimeOutputTransactionState;
    stateFailure.error = stateError;
    QVERIFY(stateFailure.isValid());
    stateFailure.error->operation = Data::ControllerOperation::QueryRuntimeResourceSnapshot;
    QVERIFY(!stateFailure.isValid());
    stateFailure = stateSuccess;
    stateFailure.state->sessionGeneration++;
    QVERIFY(!stateFailure.isValid());

    Data::RuntimeOutputTransactionState invalidState = fixture.idleState;
    invalidState.operationId = fixture.operationId;
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.resultFlags = static_cast<Data::RuntimeOutputTransactionResultFlag>(
        quint32(1) << 4);
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.expiryCycle++;
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.ttlCycles = 65536;
    invalidState.expiryCycle = invalidState.appliedCycle + invalidState.ttlCycles;
    QVERIFY(!invalidState.isValid());

    Data::RuntimeOutputTransactionState returnedTask = fixture.appliedState;
    returnedTask.state = Data::RuntimeOutputState::Idle;
    returnedTask.resultFlags = Data::RuntimeOutputTransactionResultFlag::ReturnedTask;
    returnedTask.outputGeneration++;
    QVERIFY(returnedTask.isValid());

    Data::RuntimeOutputTransactionState safeHold = fixture.appliedState;
    safeHold.state = Data::RuntimeOutputState::SafeHold;
    safeHold.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
    safeHold.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    safeHold.outputGeneration++;
    QVERIFY(safeHold.isValid());
    safeHold.resultFlags |= Data::RuntimeOutputTransactionResultFlag::OverrideActive;
    QVERIFY(!safeHold.isValid());
    safeHold.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
    QVERIFY(safeHold.isValid());

    Data::RuntimeOutputValueWrite booleanWrite;
    booleanWrite.resourceId.value = QByteArray::fromHex("01");
    booleanWrite.bitWidth = 1;
    booleanWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
    booleanWrite.value.value = true;
    QVERIFY(booleanWrite.isValid());
    booleanWrite.bitWidth = 2;
    QVERIFY(!booleanWrite.isValid());

    Data::RuntimeOutputValueWrite signedWrite;
    signedWrite.resourceId.value = QByteArray::fromHex("02");
    signedWrite.bitWidth = 4;
    signedWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    signedWrite.value.value = QVariant::fromValue<qlonglong>(-8);
    QVERIFY(signedWrite.isValid());
    signedWrite.value.value = QVariant::fromValue<qlonglong>(-9);
    QVERIFY(!signedWrite.isValid());
    signedWrite.value.value = QVariant::fromValue<qint8>(-8);
    QVERIFY(!signedWrite.isValid());

    Data::RuntimeOutputValueWrite unsignedWrite;
    unsignedWrite.resourceId.value = QByteArray::fromHex("03");
    unsignedWrite.bitWidth = 4;
    unsignedWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    unsignedWrite.value.value = QVariant::fromValue<qulonglong>(15);
    QVERIFY(unsignedWrite.isValid());
    unsignedWrite.value.value = QVariant::fromValue<qulonglong>(16);
    QVERIFY(!unsignedWrite.isValid());
    unsignedWrite.value.value = QVariant::fromValue<quint8>(15);
    QVERIFY(!unsignedWrite.isValid());

    Data::RuntimeOutputValueWrite floatingWrite;
    floatingWrite.resourceId.value = QByteArray::fromHex("0350");
    floatingWrite.bitWidth = 64;
    floatingWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::FloatingPoint;
    floatingWrite.value.value = 1.0;
    QVERIFY(!floatingWrite.isValid());

    Data::RuntimeOutputValueWrite bytesWrite;
    bytesWrite.resourceId.value = QByteArray::fromHex("04");
    bytesWrite.bitWidth = 12;
    bytesWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::ByteArray;
    bytesWrite.value.value = QByteArray::fromHex("0fff");
    QVERIFY(bytesWrite.isValid());
    bytesWrite.value.value = QByteArray::fromHex("1fff");
    QVERIFY(!bytesWrite.isValid());
    bytesWrite.value.value = QByteArray::fromHex("0f");
    QVERIFY(!bytesWrite.isValid());

    Data::RuntimeOutputValueWrite opaqueWrite = bytesWrite;
    opaqueWrite.bitWidth = 8;
    opaqueWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::Opaque;
    opaqueWrite.value.value = QByteArray::fromHex("01");
    QVERIFY(!opaqueWrite.isValid());

    Data::RuntimeOutputTransactionRequest invalidRequest = fixture.transactionRequest;
    invalidRequest.operationId.value.fill('\0');
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.expectedMappingDigest.resize(31);
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.expectedCompleteResourceCount = 1;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.expectedRecoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.ttlCycles = invalidRequest.expectedMaximumTtlCycles + 1;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.completeGroupWrites.removeLast();
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    std::swap(invalidRequest.completeGroupWrites[0], invalidRequest.completeGroupWrites[1]);
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.completeGroupWrites[1].resourceId
        = invalidRequest.completeGroupWrites[0].resourceId;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.completeGroupWrites[0].value.opaqueRepresentation = "wire-bytes";
    QVERIFY(!invalidRequest.isValid());

    Data::RuntimeOutputTransactionResult applied;
    applied.request = fixture.transactionRequest;
    applied.outcome = Data::RuntimeOutputTransactionOutcome::Applied;
    applied.finalResponseObserved = true;
    applied.state = fixture.appliedState;
    QVERIFY(applied.isValid());
    QCOMPARE(Data::RuntimeOutputTransactionResult(applied), applied);
    applied.finalResponseObserved = false;
    QVERIFY(applied.isValid());
    applied.finalResponseObserved = true;
    applied.state->outputGeneration++;
    QVERIFY(!applied.isValid());
    applied.state = fixture.appliedState;
    applied.state->recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    QVERIFY(!applied.isValid());
    applied.state = fixture.appliedState;
    applied.state->state = Data::RuntimeOutputState::Idle;
    applied.state->resultFlags = Data::RuntimeOutputTransactionResultFlag::ReturnedTask;
    QVERIFY(!applied.isValid());

    Data::RuntimeOutputTransactionResult recovered;
    recovered.request = fixture.transactionRequest;
    recovered.outcome = Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered;
    recovered.state = returnedTask;
    QVERIFY(recovered.isValid());
    recovered.finalResponseObserved = true;
    QVERIFY(!recovered.isValid());
    recovered.finalResponseObserved = false;
    recovered.state = safeHold;
    QVERIFY(!recovered.isValid());
    recovered.request.expectedRecoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    recovered.state->recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    QVERIFY(recovered.isValid());
    recovered.state->outputGeneration--;
    QVERIFY(!recovered.isValid());
    recovered.state = safeHold;
    recovered.state->operationId = Data::RuntimeOutputOperationId{QByteArray(16, '\x55')};
    QVERIFY(!recovered.isValid());
    recovered.state = safeHold;
    recovered.state->resultFlags |= Data::RuntimeOutputTransactionResultFlag::Replayed;
    QVERIFY(recovered.isValid());
    const auto rejectRecoveredMutation =
        [&recovered](const auto &mutation) {
            Data::RuntimeOutputTransactionResult invalid = recovered;
            mutation(invalid);
            QVERIFY(!invalid.isValid());
        };
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->scope.masterId = Data::NodeId::create();
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        ++invalid.state->sessionGeneration;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        ++invalid.state->epoch.runtimeGeneration;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->mappingDigest[0] ^= 1;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->consistencyGroupId.value[0] ^= 1;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        ++invalid.state->ttlCycles;
        invalid.state->expiryCycle = invalid.state->appliedCycle + invalid.state->ttlCycles;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        --invalid.state->valueCount;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->operationId.reset();
        invalid.state->state = Data::RuntimeOutputState::Idle;
        invalid.state->resultFlags = {};
        invalid.state->appliedCycle = 0;
        invalid.state->expiryCycle = 0;
        invalid.state->consistencyGroupId = {};
        invalid.state->ttlCycles = 0;
        invalid.state->recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
        invalid.state->valueCount = 0;
    });

    Data::RuntimeOutputTransactionResult rejected;
    rejected.request = fixture.transactionRequest;
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::Rejected;
    rejected.finalResponseObserved = true;
    Data::ControllerOperationError applyError;
    applyError.operation = Data::ControllerOperation::ApplyRuntimeOutputTransaction;
    rejected.error = applyError;
    QVERIFY(rejected.isValid());
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::OutcomeUnknown;
    QVERIFY(!rejected.isValid());
    rejected.finalResponseObserved = false;
    QVERIFY(rejected.isValid());
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::Unknown;
    QVERIFY(!rejected.isValid());
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::Rejected;
    rejected.state = fixture.appliedState;
    QVERIFY(!rejected.isValid());

    QVERIFY(!Data::RuntimeOutputOperationId{QByteArray(16, '\0')}.isValid());
    QVERIFY(!Data::RuntimeOutputOperationId{QByteArray(15, '\x01')}.isValid());
    QVERIFY(!Data::isValidRuntimeOutputDigest(QByteArray(32, '\0')));
    Data::RuntimeOutputTransactionRequest saturatedGeneration = fixture.transactionRequest;
    saturatedGeneration.expectedOutputGeneration = std::numeric_limits<quint64>::max();
    QVERIFY(!saturatedGeneration.isValid());
    saturatedGeneration.expectedOutputGeneration = std::numeric_limits<quint64>::max() - quint64(1);
    QVERIFY(!saturatedGeneration.isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputOperationId>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputGroupPolicyResult>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputTransactionStateResult>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputTransactionRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputTransactionResult>().isValid());
}

void EtherCATCoreTests::testRuntimePackageActivationContract()
{
    const auto sha256 = [](QByteArrayView bytes) {
        return Data::RuntimePackageActivationSha256{
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256),
        };
    };
    const auto digest = [](char value) { return QByteArray(32, value); };
    const QDateTime startedAt
        = QDateTime::fromMSecsSinceEpoch(1'800'000'000'000, Qt::UTC);
    const QByteArray packageBytes{"signed-ecpkg"};
    const QByteArray compiledProject{"compiled-project"};
    const QByteArray companion{"opaque-companion"};
    const Data::ControllerConnectionScope scope{
        Data::NodeId::fromString("11111111-1111-1111-1111-111111111111"),
        Data::NodeId::fromString("22222222-2222-2222-2222-222222222222"),
    };
    constexpr quint64 sessionGeneration = 9;
    constexpr quint64 sessionId = 100;
    constexpr quint64 bootId = 10;

    Data::RuntimeSemanticMappingProof targetProof;
    targetProof.formatVersion = 2;
    targetProof.bindingCount = 56;
    targetProof.packageSigned = true;
    targetProof.signatureVerified = true;
    targetProof.semanticBindingVerified = true;
    targetProof.trust = Data::RuntimeSemanticMappingTrust::Production;
    targetProof.packageSha256 = sha256(packageBytes).value();
    targetProof.manifestSha256 = digest('\x21');
    targetProof.mappingSha256 = digest('\x22');
    targetProof.resourceRecordsSha256 = digest('\x23');
    targetProof.resourceSectionSha256 = digest('\x24');
    targetProof.topologySha256 = digest('\x25');
    targetProof.signingKeyIdSha256 = digest('\x26');

    const Data::RuntimePackageActivationIdentity identity{
        Data::RuntimePackageActivationOperationId{"activation-1"},
        scope,
        Data::RuntimePackageActivationDocumentRevisionToken{"document-revision-7"},
        Data::RuntimePackageActivationOriginalBindingToken{"original-binding-empty"},
        Data::RuntimePackageActivationOriginalBindingToken{"binding-runtime-api038"},
        QStringLiteral("runtime/api038/manual-control"),
        3701,
        99,
        QByteArray("topology-identity"),
        sha256(packageBytes),
        sha256(compiledProject),
        sha256(companion),
        targetProof,
    };
    const Data::RuntimePackageActivationRequest request{
        identity,
        packageBytes,
        compiledProject,
        companion,
    };
    QVERIFY(identity.isValid());
    QVERIFY(request.isValid());
    const auto compilerSha256 = [](QByteArrayView bytes) {
        return RuntimePackageCompilerFixture::sha256(bytes);
    };
    const Data::RuntimePackageCompilerSha256 compilerPackageSha256
        = compilerSha256(packageBytes);
    const Data::RuntimePackageCompilerSha256 compilerCompanionSha256
        = compilerSha256(companion);
    const Data::RuntimePackageCompilerSha256 compilerIntentSha256
        = compilerSha256("activation-intent");
    const Data::RuntimePackageCompilerSha256 compilerTargetSha256
        = compilerSha256("target-profile");
    const Data::RuntimePackageCompilerSha256 compilerAdapterSha256
        = compilerSha256("adapter-bundle");
    const Data::RuntimePackageCompilerSha256 compilerTopologySha256
        = compilerSha256("topology-evidence");
    const QByteArray compilerResult
        = QStringLiteral(
              "{\"adapter_bundle_sha256\":\"%1\",\"configuration_id\":3701,"
              "\"effective_project_companion_sha256\":\"%2\","
              "\"format\":\"ethercat-ide-project-compiler-verification-v1\","
              "\"intent_sha256\":\"%3\",\"manifest_format_version\":2,"
              "\"package_sha256\":\"%4\",\"status\":\"pass\","
              "\"target_profile_sha256\":\"%5\",\"topology_evidence_sha256\":\"%6\"}\n")
              .arg(
                  QString::fromLatin1(
                      compilerAdapterSha256.value().toHex()),
                  QString::fromLatin1(
                      compilerCompanionSha256.value().toHex()),
                  QString::fromLatin1(
                      compilerIntentSha256.value().toHex()),
                  QString::fromLatin1(
                      compilerPackageSha256.value().toHex()),
                  QString::fromLatin1(
                      compilerTargetSha256.value().toHex()),
                  QString::fromLatin1(
                      compilerTopologySha256.value().toHex()))
              .toLatin1();
    const Data::RuntimePackageCompilerOperationId compilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000370")};
    const Data::RuntimePackageCompilerVerifyResult compilerVerification{
        compilerEnvelope(
            Data::RuntimePackageCompilerCommand::Verify,
            Data::RuntimePackageCompilerResultStatus::Succeeded,
            QStringLiteral("pass"),
            compilerOperationId,
            3701,
            compilerSha256("verify-request"),
            {},
            RuntimePackageCompilerFixture::canonical(compilerResult)),
        compilerPackageSha256,
        2,
        compilerIntentSha256,
        compilerCompanionSha256,
        compilerTargetSha256,
        compilerAdapterSha256,
        compilerTopologySha256,
        true,
    };
    QVERIFY(compilerVerification.isSuccess());
    const RuntimePackageActivationPreparationRequest preparation{
        identity.operationId(),
        scope,
        packageBytes,
        compiledProject,
        companion,
        compilerVerification,
        true,
        syntheticRuntimePackageCompilerActivationProof(),
    };
    QVERIFY(preparation.isValid());
    RuntimePackageActivationPreparationRequest missingProof = preparation;
    missingProof.compilerActivationProof.reset();
    QVERIFY(!missingProof.isValid());
    RuntimePackageActivationPreparationRequest missingLower = preparation;
    missingLower.compiledProjectSource.clear();
    QVERIFY(!missingLower.isValid());
    const Data::RuntimePackageActivationRequest alteredPackage{
        identity,
        QByteArray("altered-package"),
        compiledProject,
        companion,
    };
    QVERIFY(!alteredPackage.isValid());
    const Data::RuntimePackageActivationRequest noRollbackRequest{
        identity,
        packageBytes,
        compiledProject,
        companion,
        false,
    };
    QVERIFY(noRollbackRequest.isValid());
    QVERIFY(noRollbackRequest.fingerprint() != request.fingerprint());
    const Data::RuntimePackageActivationIdentity changedCatalogIdentity{
        identity.operationId(),
        identity.scope(),
        identity.documentRevisionToken(),
        identity.originalBindingToken(),
        identity.targetBindingToken(),
        identity.bindingArtifactId(),
        identity.configurationId(),
        identity.expectedCatalogRevision() + 1,
        identity.expectedTopologyIdentity(),
        identity.packageSha256(),
        identity.compiledProjectSha256(),
        identity.effectiveProjectCompanionSha256(),
        identity.expectedMappingProof(),
    };
    QVERIFY(changedCatalogIdentity.isValid());
    QVERIFY(
        Data::runtimePackageActivationCanonicalRequestFingerprint(
            changedCatalogIdentity, true)
        != request.fingerprint());

    const Data::ControllerPackageSelector previous{
        Data::ControllerSlot::B, 33, 44};
    const Data::ControllerPackageSelector candidate{
        Data::ControllerSlot::A, 55, identity.configurationId()};
    Data::RuntimeSemanticMappingProof previousProof = targetProof;
    previousProof.packageSha256 = digest('\x31');

    const auto makeAttestation = [&](
                                     const Data::ControllerPackageSelector &selector,
                                     const Data::RuntimeSemanticMappingProof &proof,
                                     quint64 generation,
                                     const QDateTime &at) {
        Data::RuntimeSemanticMappingAttestation value;
        value.scope = scope;
        value.sessionGeneration = generation;
        value.epoch.controllerBootId = bootId;
        value.epoch.activePackageSlot = selector.slot;
        value.epoch.activePackageGeneration = selector.generation;
        value.epoch.configurationId = selector.configurationId;
        value.epoch.topologyGeneration = 77;
        value.epoch.runtimeGeneration = 88;
        value.epoch.catalogRevision = 99;
        value.epoch.topologyIdentity = QByteArray("topology-identity");
        value.proof = proof;
        value.receivedAt = at.addMSecs(-1);
        return value;
    };
    const auto makeEvidence = [&](
                                  const Data::ControllerPackageSelector &selector,
                                  const Data::RuntimeSemanticMappingProof &proof,
                                  Data::ControllerServiceState serviceState,
                                  quint64 leaseOwner,
                                  const QDateTime &at,
                                  quint64 generation = sessionGeneration,
                                  quint64 observer = sessionId) {
        return Data::RuntimePackageActivationControllerEvidence{
            scope,
            generation,
            observer,
            leaseOwner,
            bootId,
            selector,
            Data::ControllerPackageState::Active,
            serviceState,
            makeAttestation(selector, proof, generation, at),
            at,
        };
    };

    const auto before = makeEvidence(
        previous,
        previousProof,
        Data::ControllerServiceState::Shutdown,
        0,
        startedAt.addMSecs(4));
    const auto acquired = makeEvidence(
        previous,
        previousProof,
        Data::ControllerServiceState::Shutdown,
        sessionId,
        startedAt.addMSecs(7));
    const auto afterHeld = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        sessionId,
        startedAt.addMSecs(14));
    const auto afterReleased = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        0,
        startedAt.addMSecs(20));
    QVERIFY(before.isValid());
    QVERIFY(acquired.isValid());
    QVERIFY(afterHeld.isValid());
    QVERIFY(afterReleased.isValid());
    QVERIFY(afterHeld.matchesActivatedIdentity(identity));

    const auto faultSnapshot = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::Fault,
        0,
        startedAt.addMSecs(8));
    const auto shutdownSnapshot = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::Shutdown,
        0,
        startedAt.addMSecs(8));
    QVERIFY(faultSnapshot.isValid());
    QVERIFY(shutdownSnapshot.isValid());
    QVERIFY(!faultSnapshot.matchesActivatedIdentity(identity));
    QVERIFY(!shutdownSnapshot.matchesActivatedIdentity(identity));
    auto wrongCatalogAttestation = *afterHeld.mappingAttestation();
    ++wrongCatalogAttestation.epoch.catalogRevision;
    const Data::RuntimePackageActivationControllerEvidence wrongCatalogSnapshot{
        scope,
        sessionGeneration,
        sessionId,
        sessionId,
        bootId,
        candidate,
        Data::ControllerPackageState::Active,
        Data::ControllerServiceState::OperationalSafe,
        wrongCatalogAttestation,
        startedAt.addMSecs(15),
    };
    QVERIFY(wrongCatalogSnapshot.isValid());
    QVERIFY(!wrongCatalogSnapshot.matchesActivatedIdentity(identity));
    auto wrongTopologyAttestation = *afterHeld.mappingAttestation();
    wrongTopologyAttestation.epoch.topologyIdentity.append("-external");
    const Data::RuntimePackageActivationControllerEvidence wrongTopologySnapshot{
        scope,
        sessionGeneration,
        sessionId,
        sessionId,
        bootId,
        candidate,
        Data::ControllerPackageState::Active,
        Data::ControllerServiceState::OperationalSafe,
        wrongTopologyAttestation,
        startedAt.addMSecs(15),
    };
    QVERIFY(wrongTopologySnapshot.isValid());
    QVERIFY(!wrongTopologySnapshot.matchesActivatedIdentity(identity));

    const auto changedEvidence = makeEvidence(
        {candidate.slot, candidate.generation + 1, candidate.configurationId},
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        sessionId,
        afterHeld.observedAt());
    QVERIFY(changedEvidence.isValid());
    QVERIFY(changedEvidence.evidenceSha256() != afterHeld.evidenceSha256());

    const auto progressEvent = [](
                                   quint64 sequence,
                                   Data::ControllerOperation operation,
                                   std::optional<quint64> requestId,
                                   std::optional<qint32> status,
                                   const QDateTime &at) {
        Data::ControllerPackageDeploymentAuditEvent event;
        event.sequence = sequence;
        event.operation = operation;
        event.requestId = requestId;
        event.status = status;
        if (status)
            event.operationResult = 0;
        event.detail = QStringLiteral("deployment-event-%1").arg(sequence);
        event.occurredAt = at;
        return event;
    };
    const auto makeSuccessProgress = [&](
                                         qint64 bytes,
                                         qsizetype chunks,
                                         const QByteArray &artifactDigest) {
        Data::ControllerPackageDeploymentProgress progress;
        progress.operationId = QStringLiteral("deploy-op");
        progress.artifactSha256 = artifactDigest;
        progress.state = Data::ControllerPackageDeploymentState::Succeeded;
        progress.totalBytes = bytes;
        progress.transferredBytes = bytes;
        progress.candidate = candidate;
        progress.previousActive = previous;
        progress.status = 0;
        progress.operationResult = 0;
        progress.detail = QStringLiteral("deployment-succeeded");
        progress.startedAt = startedAt.addMSecs(5);
        quint64 sequence = 1;
        progress.audit.append(progressEvent(
            sequence,
            Data::ControllerOperation::UploadPackage,
            {},
            {},
            progress.startedAt));
        const auto exchange = [&](
                                  Data::ControllerOperation operation,
                                  quint64 requestId,
                                  qsizetype responses) {
            progress.audit.append(progressEvent(
                ++sequence,
                operation,
                requestId,
                {},
                progress.startedAt));
            for (qsizetype index = 0; index < responses; ++index) {
                progress.audit.append(progressEvent(
                    ++sequence,
                    operation,
                    requestId,
                    0,
                    progress.startedAt));
            }
        };
        exchange(Data::ControllerOperation::UploadPackage, 1, 1);
        for (qsizetype index = 0; index < chunks; ++index)
            exchange(Data::ControllerOperation::UploadPackage, quint64(index + 2), 1);
        exchange(Data::ControllerOperation::UploadPackage, quint64(chunks + 2), 1);
        exchange(Data::ControllerOperation::ValidatePackage, quint64(chunks + 3), 6);
        exchange(Data::ControllerOperation::ActivatePackage, quint64(chunks + 4), 6);
        progress.completedAt = progress.startedAt.addMSecs(1);
        return progress;
    };

    const auto progress = makeSuccessProgress(
        packageBytes.size(), 1, identity.packageSha256().value());
    const Data::RuntimePackageActivationDeploymentEvidence deployment{
        sessionGeneration,
        sessionId,
        bootId,
        progress,
        startedAt.addMSecs(12),
    };
    QVERIFY(deployment.isValid());
    QCOMPARE(deployment.progress(), progress);
    QCOMPARE(deployment.candidate(), std::optional(candidate));

    Data::ControllerPackageDeploymentProgress truncatedProgress = progress;
    truncatedProgress.audit.removeLast();
    const Data::RuntimePackageActivationDeploymentEvidence truncatedDeployment{
        sessionGeneration,
        sessionId,
        bootId,
        truncatedProgress,
        startedAt.addMSecs(12),
    };
    QVERIFY(!truncatedDeployment.isValid());

    Data::ControllerPackageDeploymentProgress changedProgress = progress;
    changedProgress.detail = QStringLiteral("changed-complete-progress");
    const Data::RuntimePackageActivationDeploymentEvidence changedDeployment{
        sessionGeneration,
        sessionId,
        bootId,
        changedProgress,
        startedAt.addMSecs(12),
    };
    QVERIFY(changedDeployment.isValid());
    QVERIFY(changedDeployment.evidenceSha256() != deployment.evidenceSha256());

    Data::ControllerPackageDeploymentProgress precommitFailure;
    precommitFailure.operationId = QStringLiteral("precommit-failure");
    precommitFailure.artifactSha256 = identity.packageSha256().value();
    precommitFailure.state = Data::ControllerPackageDeploymentState::Failed;
    precommitFailure.totalBytes = packageBytes.size();
    precommitFailure.status = -21;
    precommitFailure.operationResult = 0;
    precommitFailure.detail = QStringLiteral("bulk-begin-failed");
    precommitFailure.startedAt = startedAt.addMSecs(1);
    precommitFailure.audit = {
        progressEvent(1, Data::ControllerOperation::UploadPackage, {}, {},
                      precommitFailure.startedAt),
        progressEvent(2, Data::ControllerOperation::UploadPackage, 1, {},
                      precommitFailure.startedAt.addMSecs(1)),
        progressEvent(3, Data::ControllerOperation::UploadPackage, 1, -21,
                      precommitFailure.startedAt.addMSecs(2)),
    };
    precommitFailure.completedAt = precommitFailure.startedAt.addMSecs(3);
    const Data::RuntimePackageActivationDeploymentEvidence precommitEvidence{
        sessionGeneration,
        sessionId,
        bootId,
        precommitFailure,
        precommitFailure.completedAt,
    };
    QVERIFY(precommitEvidence.isValid());
    QVERIFY(!precommitEvidence.candidate());

    Data::ControllerPackageDeploymentProgress precommitUnknown = precommitFailure;
    precommitUnknown.operationId = QStringLiteral("precommit-unknown");
    precommitUnknown.state = Data::ControllerPackageDeploymentState::OutcomeUnknown;
    precommitUnknown.status.reset();
    precommitUnknown.operationResult.reset();
    precommitUnknown.detail = QStringLiteral("bulk-begin-unknown");
    precommitUnknown.audit.removeLast();
    const Data::RuntimePackageActivationDeploymentEvidence unknownEvidence{
        sessionGeneration,
        sessionId,
        bootId,
        precommitUnknown,
        precommitUnknown.completedAt,
    };
    QVERIFY(unknownEvidence.isValid());
    QVERIFY(!unknownEvidence.candidate());

    Data::ControllerPackageDeploymentProgress rollbackProgress = progress;
    rollbackProgress.state = Data::ControllerPackageDeploymentState::Failed;
    rollbackProgress.status = -19;
    rollbackProgress.operationResult = 0;
    rollbackProgress.detail = QStringLiteral("internal-rollback-succeeded");
    quint64 rollbackSequence = quint64(rollbackProgress.audit.size());
    rollbackProgress.audit.append(progressEvent(
        ++rollbackSequence,
        Data::ControllerOperation::RollbackPackage,
        600,
        {},
        rollbackProgress.completedAt));
    rollbackProgress.audit.append(progressEvent(
        ++rollbackSequence,
        Data::ControllerOperation::RollbackPackage,
        600,
        0,
        rollbackProgress.completedAt.addMSecs(1)));
    rollbackProgress.completedAt = rollbackProgress.completedAt.addMSecs(2);
    const Data::RuntimePackageActivationDeploymentEvidence rollbackEvidence{
        sessionGeneration,
        sessionId,
        bootId,
        rollbackProgress,
        rollbackProgress.completedAt,
    };
    QVERIFY(rollbackEvidence.isValid());

    auto duplicateRollbackProgress = rollbackProgress;
    duplicateRollbackProgress.audit.append(progressEvent(
        ++rollbackSequence,
        Data::ControllerOperation::RollbackPackage,
        601,
        {},
        duplicateRollbackProgress.completedAt));
    duplicateRollbackProgress.audit.append(progressEvent(
        ++rollbackSequence,
        Data::ControllerOperation::RollbackPackage,
        601,
        0,
        duplicateRollbackProgress.completedAt.addMSecs(1)));
    duplicateRollbackProgress.completedAt
        = duplicateRollbackProgress.completedAt.addMSecs(2);
    const Data::RuntimePackageActivationDeploymentEvidence duplicateRollback{
        sessionGeneration,
        sessionId,
        bootId,
        duplicateRollbackProgress,
        duplicateRollbackProgress.completedAt,
    };
    QVERIFY(!duplicateRollback.isValid());

    const auto maximumProgress = makeSuccessProgress(
        16 * 1024 * 1024,
        257,
        sha256(QByteArrayView("maximum-package")).value());
    QCOMPARE(maximumProgress.audit.size(), 533);
    const Data::RuntimePackageActivationDeploymentEvidence maximumDeployment{
        sessionGeneration,
        sessionId,
        bootId,
        maximumProgress,
        maximumProgress.completedAt,
    };
    QVERIFY(maximumDeployment.isValid());

    const Data::RuntimePackageActivationProjectCommit projectCommit{
        identity.documentRevisionToken(),
        identity.originalBindingToken(),
        Data::RuntimePackageActivationDocumentRevisionToken{"document-revision-8"},
        identity.targetBindingToken(),
        Data::RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted,
        startedAt.addMSecs(17),
    };
    const Data::RuntimePackageActivationProjectCommit changedCommit{
        identity.documentRevisionToken(),
        identity.originalBindingToken(),
        Data::RuntimePackageActivationDocumentRevisionToken{"document-revision-9"},
        identity.targetBindingToken(),
        Data::RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted,
        startedAt.addMSecs(17),
    };
    QVERIFY(projectCommit.isValid());
    QVERIFY(changedCommit.isValid());
    QVERIFY(changedCommit.evidenceSha256() != projectCommit.evidenceSha256());

    Data::ProjectSnapshot capturedSnapshot;
    capturedSnapshot.id = scope.projectId;
    capturedSnapshot.valid = true;
    const Data::RuntimePackageActivationProjectCapture projectCapture{
        capturedSnapshot,
        QByteArray("{\"project\":\"captured\"}\n"),
        8,
        identity.documentRevisionToken(),
        identity.originalBindingToken(),
    };
    QVERIFY(projectCapture.isValid());
    QVERIFY(!Data::RuntimePackageActivationProjectCapture().isValid());

    const Data::RuntimePackageActivationProjectCompareAndSetResult staleProjectCommit{
        Data::RuntimePackageActivationProjectCompareAndSetDisposition::Stale};
    const Data::RuntimePackageActivationProjectCompareAndSetResult committedProjectCommit{
        Data::RuntimePackageActivationProjectCompareAndSetDisposition::
            CompareAndSetCommitted,
        projectCommit};
    const Data::RuntimePackageActivationProjectCommit alreadyExactCommit{
        identity.documentRevisionToken(),
        identity.originalBindingToken(),
        identity.documentRevisionToken(),
        identity.originalBindingToken(),
        Data::RuntimePackageActivationProjectCommitDisposition::AlreadyExact,
        startedAt.addMSecs(18),
    };
    const Data::RuntimePackageActivationProjectCompareAndSetResult alreadyExactProjectCommit{
        Data::RuntimePackageActivationProjectCompareAndSetDisposition::AlreadyExact,
        alreadyExactCommit};
    QVERIFY(staleProjectCommit.isValid());
    QVERIFY(committedProjectCommit.isValid());
    QVERIFY(alreadyExactProjectCommit.isValid());
    QVERIFY(!Data::RuntimePackageActivationProjectCompareAndSetResult().isValid());
    const Data::RuntimePackageActivationProjectCompareAndSetResult
        mismatchedProjectCommit{
            Data::RuntimePackageActivationProjectCompareAndSetDisposition::AlreadyExact,
            projectCommit};
    QVERIFY(!mismatchedProjectCommit.isValid());

    const auto localEvent = [](
                                quint64 sequence,
                                Data::RuntimePackageActivationAuditKind kind,
                                Data::RuntimePackageActivationPhase phase,
                                Data::RuntimePackageActivationOutcome outcome,
                                QString code,
                                const QDateTime &at,
                                std::optional<Data::RuntimePackageActivationSha256> evidence = {}) {
        return Data::RuntimePackageActivationAuditEvent{
            sequence, kind, phase, outcome,
            Data::RuntimePackageActivationProviderAction::None,
            Data::RuntimePackageActivationProviderReconciliation::None,
            {}, 0, 0, 0, {}, {}, {}, std::move(evidence),
            code, code, at,
        };
    };
    const auto providerEvent = [](
                                   quint64 sequence,
                                   Data::RuntimePackageActivationAuditKind kind,
                                   Data::RuntimePackageActivationPhase phase,
                                   Data::RuntimePackageActivationOutcome outcome,
                                   Data::RuntimePackageActivationProviderAction action,
                                   Data::RuntimePackageActivationProviderReconciliation reconciliation,
                                   QString operationId,
                                   quint64 generation,
                                   std::optional<quint64> requestId,
                                   std::optional<qint32> status,
                                   std::optional<qint32> operationResult,
                                   std::optional<Data::RuntimePackageActivationSha256> evidence,
                                   const QDateTime &at) {
        return Data::RuntimePackageActivationAuditEvent{
            sequence, kind, phase, outcome, action, reconciliation,
            std::move(operationId), generation, sessionId, bootId,
            requestId, status, operationResult, std::move(evidence),
            QStringLiteral("provider-event"),
            QStringLiteral("provider-event"),
            at,
        };
    };
    const auto queued = localEvent(
        1,
        Data::RuntimePackageActivationAuditKind::IntentPersisted,
        Data::RuntimePackageActivationPhase::Queued,
        Data::RuntimePackageActivationOutcome::Pending,
        QStringLiteral("queued"),
        startedAt,
        request.fingerprint());

    QList<Data::RuntimePackageActivationAuditEvent> audit{
        queued,
        localEvent(2, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::VerifyingInputs,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("verify"), startedAt.addMSecs(1)),
        localEvent(3, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::CapturingProject,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("capture"), startedAt.addMSecs(2)),
        localEvent(4, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::ProbingExistingPackage,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("probe"), startedAt.addMSecs(3)),
        localEvent(5, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                   Data::RuntimePackageActivationPhase::ProbingExistingPackage,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("before"), startedAt.addMSecs(4),
                   before.evidenceSha256()),
        localEvent(6, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::AcquiringControl,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("acquire"), startedAt.addMSecs(5)),
        providerEvent(7, Data::RuntimePackageActivationAuditKind::ProviderRequestSent,
                      Data::RuntimePackageActivationPhase::AcquiringControl,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::AcquireControl,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("acquire-op"), sessionGeneration, 41, {}, {}, {},
                      startedAt.addMSecs(6)),
        localEvent(8, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                   Data::RuntimePackageActivationPhase::AcquiringControl,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("lease"), startedAt.addMSecs(7),
                   acquired.evidenceSha256()),
        providerEvent(9, Data::RuntimePackageActivationAuditKind::ProviderTerminalResponse,
                      Data::RuntimePackageActivationPhase::AcquiringControl,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::AcquireControl,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("acquire-op"), sessionGeneration, 41, 0, 0,
                      acquired.evidenceSha256(), startedAt.addMSecs(8)),
        localEvent(10, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::DeployingPackage,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("deploy"), startedAt.addMSecs(9)),
        providerEvent(11, Data::RuntimePackageActivationAuditKind::ProviderRequestSent,
                      Data::RuntimePackageActivationPhase::DeployingPackage,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::DeployPackage,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("deploy-op"), sessionGeneration, {}, {}, {}, {},
                      startedAt.addMSecs(10)),
        providerEvent(12, Data::RuntimePackageActivationAuditKind::DeploymentEvidenceCaptured,
                      Data::RuntimePackageActivationPhase::DeployingPackage,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::DeployPackage,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("deploy-op"), sessionGeneration, {}, {}, {},
                      deployment.evidenceSha256(), startedAt.addMSecs(11)),
        providerEvent(13, Data::RuntimePackageActivationAuditKind::ProviderTerminalResponse,
                      Data::RuntimePackageActivationPhase::DeployingPackage,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::DeployPackage,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("deploy-op"), sessionGeneration, {}, 0, 0,
                      deployment.evidenceSha256(), startedAt.addMSecs(12)),
        localEvent(14, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("verify-runtime"), startedAt.addMSecs(13)),
        localEvent(15, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                   Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("after"), startedAt.addMSecs(14),
                   afterHeld.evidenceSha256()),
        localEvent(16, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::PersistingEvidence,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("persist"), startedAt.addMSecs(15)),
        localEvent(17, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::CommittingProjectBinding,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("commit"), startedAt.addMSecs(16)),
        localEvent(18, Data::RuntimePackageActivationAuditKind::ProjectCompareAndSet,
                   Data::RuntimePackageActivationPhase::CommittingProjectBinding,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("project-cas"), projectCommit.committedAt(),
                   projectCommit.evidenceSha256()),
        localEvent(19, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::ReleasingControl,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("release"), startedAt.addMSecs(18)),
        providerEvent(20, Data::RuntimePackageActivationAuditKind::ProviderRequestSent,
                      Data::RuntimePackageActivationPhase::ReleasingControl,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::ReleaseControl,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("release-op"), sessionGeneration, 42, {}, {}, {},
                      startedAt.addMSecs(19)),
        localEvent(21, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                   Data::RuntimePackageActivationPhase::ReleasingControl,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("released"), startedAt.addMSecs(20),
                   afterReleased.evidenceSha256()),
        providerEvent(22, Data::RuntimePackageActivationAuditKind::ProviderTerminalResponse,
                      Data::RuntimePackageActivationPhase::ReleasingControl,
                      Data::RuntimePackageActivationOutcome::Pending,
                      Data::RuntimePackageActivationProviderAction::ReleaseControl,
                      Data::RuntimePackageActivationProviderReconciliation::None,
                      QStringLiteral("release-op"), sessionGeneration, 42, 0, 0,
                      afterReleased.evidenceSha256(), startedAt.addMSecs(21)),
        localEvent(23, Data::RuntimePackageActivationAuditKind::Completed,
                   Data::RuntimePackageActivationPhase::Finished,
                   Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage,
                   QStringLiteral("succeeded"), startedAt.addMSecs(22),
                   projectCommit.evidenceSha256()),
    };

    const auto activatedRecord = [&](
                                     const QList<Data::RuntimePackageActivationAuditEvent> &events,
                                     const Data::RuntimePackageActivationControllerEvidence &after,
                                     quint64 revision) {
        return Data::RuntimePackageActivationRecord{
            identity,
            request.fingerprint(),
            true,
            revision,
            Data::RuntimePackageActivationPhase::Finished,
            Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage,
            events,
            events.constLast().detail(),
            startedAt,
            events.constLast().occurredAt(),
            events.constLast().occurredAt(),
            before,
            after,
            projectCommit,
            {},
            deployment,
            {acquired, afterReleased},
        };
    };
    const auto activatedSuccess = activatedRecord(audit, afterHeld, quint64(audit.size()));
    QVERIFY(activatedSuccess.isValid());

    auto wrongGenerationAudit = audit;
    wrongGenerationAudit[8] = providerEvent(
        9, Data::RuntimePackageActivationAuditKind::ProviderTerminalResponse,
        Data::RuntimePackageActivationPhase::AcquiringControl,
        Data::RuntimePackageActivationOutcome::Pending,
        Data::RuntimePackageActivationProviderAction::AcquireControl,
        Data::RuntimePackageActivationProviderReconciliation::None,
        QStringLiteral("acquire-op"), sessionGeneration + 1, 41, 0, 0,
        acquired.evidenceSha256(), startedAt.addMSecs(8));
    QVERIFY(!activatedRecord(
        wrongGenerationAudit, afterHeld, quint64(wrongGenerationAudit.size())).isValid());

    const auto noLeaseAfter = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        0,
        afterHeld.observedAt());
    auto noLeaseAudit = audit;
    noLeaseAudit[14] = localEvent(
        15, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
        Data::RuntimePackageActivationOutcome::Pending,
        QStringLiteral("no-lease"), afterHeld.observedAt(),
        noLeaseAfter.evidenceSha256());
    QVERIFY(!activatedRecord(
        noLeaseAudit, noLeaseAfter, quint64(noLeaseAudit.size())).isValid());

    const auto wrongCandidateAfter = makeEvidence(
        {candidate.slot, candidate.generation + 1, candidate.configurationId},
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        sessionId,
        afterHeld.observedAt());
    QVERIFY(wrongCandidateAfter.matchesActivatedIdentity(identity));
    auto wrongCandidateAudit = audit;
    wrongCandidateAudit[14] = localEvent(
        15, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
        Data::RuntimePackageActivationOutcome::Pending,
        QStringLiteral("wrong-candidate"), afterHeld.observedAt(),
        wrongCandidateAfter.evidenceSha256());
    QVERIFY(!activatedRecord(
        wrongCandidateAudit,
        wrongCandidateAfter,
        quint64(wrongCandidateAudit.size())).isValid());
    auto orphanEvidenceAudit = audit;
    orphanEvidenceAudit[14] = localEvent(
        15,
        Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
        Data::RuntimePackageActivationOutcome::Pending,
        QStringLiteral("orphan-evidence"),
        afterHeld.observedAt(),
        sha256(QByteArrayView("unparseable-evidence")));
    QVERIFY(!activatedRecord(
        orphanEvidenceAudit,
        afterHeld,
        quint64(orphanEvidenceAudit.size())).isValid());
    QVERIFY(!activatedRecord(
        audit, afterHeld, quint64(audit.size() + 1)).isValid());

    const auto existingBefore = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        0,
        startedAt.addMSecs(4));
    const auto existingAfter = makeEvidence(
        candidate,
        targetProof,
        Data::ControllerServiceState::OperationalSafe,
        0,
        startedAt.addMSecs(6));
    QList<Data::RuntimePackageActivationAuditEvent> existingAudit{
        queued,
        audit.at(1),
        audit.at(2),
        audit.at(3),
        localEvent(5, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                   Data::RuntimePackageActivationPhase::ProbingExistingPackage,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("existing-before"), startedAt.addMSecs(4),
                   existingBefore.evidenceSha256()),
        localEvent(6, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("verify-existing"), startedAt.addMSecs(5)),
        localEvent(7, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
                   Data::RuntimePackageActivationPhase::VerifyingRuntimeIdentity,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("existing-after"), startedAt.addMSecs(6),
                   existingAfter.evidenceSha256()),
        localEvent(8, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::PersistingEvidence,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("persist-existing"), startedAt.addMSecs(7)),
        localEvent(9, Data::RuntimePackageActivationAuditKind::PhaseTransition,
                   Data::RuntimePackageActivationPhase::CommittingProjectBinding,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("commit-existing"), startedAt.addMSecs(8)),
        localEvent(10, Data::RuntimePackageActivationAuditKind::ProjectCompareAndSet,
                   Data::RuntimePackageActivationPhase::CommittingProjectBinding,
                   Data::RuntimePackageActivationOutcome::Pending,
                   QStringLiteral("cas-existing"), projectCommit.committedAt(),
                   projectCommit.evidenceSha256()),
        localEvent(11, Data::RuntimePackageActivationAuditKind::Completed,
                   Data::RuntimePackageActivationPhase::Finished,
                   Data::RuntimePackageActivationOutcome::SucceededWithExistingPackage,
                   QStringLiteral("existing-succeeded"), startedAt.addMSecs(18),
                   projectCommit.evidenceSha256()),
    };
    const Data::RuntimePackageActivationRecord existingSuccess{
        identity, request.fingerprint(), true, quint64(existingAudit.size()),
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::SucceededWithExistingPackage,
        existingAudit, existingAudit.constLast().detail(), startedAt,
        existingAudit.constLast().occurredAt(), existingAudit.constLast().occurredAt(),
        existingBefore, existingAfter, projectCommit,
    };
    QVERIFY(existingSuccess.isValid());

    auto externalExistingAudit = existingAudit;
    externalExistingAudit[4] = localEvent(
        5, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::ProbingExistingPackage,
        Data::RuntimePackageActivationOutcome::Pending,
        QStringLiteral("external-before"), startedAt.addMSecs(4),
        before.evidenceSha256());
    const Data::RuntimePackageActivationRecord externalExisting{
        identity, request.fingerprint(), true, quint64(externalExistingAudit.size()),
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::SucceededWithExistingPackage,
        externalExistingAudit, externalExistingAudit.constLast().detail(), startedAt,
        externalExistingAudit.constLast().occurredAt(),
        externalExistingAudit.constLast().occurredAt(),
        before, existingAfter, projectCommit,
    };
    QVERIFY(!externalExisting.isValid());

    const Data::RuntimePackageActivationRecord deployPending{
        identity, request.fingerprint(), true, 11,
        Data::RuntimePackageActivationPhase::DeployingPackage,
        Data::RuntimePackageActivationOutcome::Pending,
        audit.mid(0, 11), audit.at(10).detail(), startedAt,
        audit.at(10).occurredAt(), {}, before, {}, {}, {}, {}, {acquired},
    };
    QVERIFY(deployPending.isValid());

    auto deployUnknownAudit = audit.mid(0, 11);
    deployUnknownAudit.append(localEvent(
        12, Data::RuntimePackageActivationAuditKind::PhaseTransition,
        Data::RuntimePackageActivationPhase::AwaitingReconciliation,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("deploy-unknown"), startedAt.addMSecs(11)));
    const Data::RuntimePackageActivationRecord deployUnknown{
        identity, request.fingerprint(), true, quint64(deployUnknownAudit.size()),
        Data::RuntimePackageActivationPhase::AwaitingReconciliation,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        deployUnknownAudit, deployUnknownAudit.constLast().detail(), startedAt,
        deployUnknownAudit.constLast().occurredAt(), {}, before, {}, {}, {}, {},
        {acquired},
    };
    QVERIFY(deployUnknown.isValid());

    auto appliedWithoutProgressAudit = deployUnknownAudit;
    appliedWithoutProgressAudit.append(localEvent(
        13, Data::RuntimePackageActivationAuditKind::ReconciliationStarted,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("reconcile-deploy"), startedAt.addMSecs(12)));
    appliedWithoutProgressAudit.append(localEvent(
        14, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("target-after-reconnect"), startedAt.addMSecs(14),
        afterHeld.evidenceSha256()));
    appliedWithoutProgressAudit.append(providerEvent(
        15, Data::RuntimePackageActivationAuditKind::ProviderOutcomeReconciled,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        Data::RuntimePackageActivationProviderAction::DeployPackage,
        Data::RuntimePackageActivationProviderReconciliation::Applied,
        QStringLiteral("deploy-op"), sessionGeneration, {}, {}, {},
        afterHeld.evidenceSha256(), startedAt.addMSecs(15)));
    appliedWithoutProgressAudit.append(localEvent(
        16, Data::RuntimePackageActivationAuditKind::ProjectCompareAndSet,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("cas-after-reconnect"), projectCommit.committedAt(),
        projectCommit.evidenceSha256()));
    appliedWithoutProgressAudit.append(localEvent(
        17, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("lease-cleared-after-reconnect"), startedAt.addMSecs(20),
        afterReleased.evidenceSha256()));
    appliedWithoutProgressAudit.append(providerEvent(
        18, Data::RuntimePackageActivationAuditKind::ControlLeaseReconciled,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        Data::RuntimePackageActivationProviderAction::ReleaseControl,
        Data::RuntimePackageActivationProviderReconciliation::Applied,
        QStringLiteral("lease-reconcile"), sessionGeneration, {}, {}, {},
        afterReleased.evidenceSha256(), startedAt.addMSecs(21)));
    appliedWithoutProgressAudit.append(localEvent(
        19, Data::RuntimePackageActivationAuditKind::Completed,
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage,
        QStringLiteral("forged-success-without-progress"), startedAt.addMSecs(22),
        projectCommit.evidenceSha256()));
    const Data::RuntimePackageActivationRecord appliedWithoutInternalProgress{
        identity, request.fingerprint(), true,
        quint64(appliedWithoutProgressAudit.size()),
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage,
        appliedWithoutProgressAudit,
        appliedWithoutProgressAudit.constLast().detail(), startedAt,
        appliedWithoutProgressAudit.constLast().occurredAt(),
        appliedWithoutProgressAudit.constLast().occurredAt(),
        before, afterHeld, projectCommit, {}, {}, {acquired, afterReleased},
    };
    QVERIFY(!appliedWithoutInternalProgress.isValid());

    const auto reconnectedBefore = makeEvidence(
        previous,
        previousProof,
        Data::ControllerServiceState::Shutdown,
        0,
        startedAt.addMSecs(14),
        sessionGeneration + 1,
        sessionId + 1);
    auto notAppliedAudit = deployUnknownAudit;
    notAppliedAudit.append(localEvent(
        13, Data::RuntimePackageActivationAuditKind::ReconciliationStarted,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("reconcile-not-applied"), startedAt.addMSecs(12)));
    notAppliedAudit.append(localEvent(
        14, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("unchanged-after-reconnect"), startedAt.addMSecs(14),
        reconnectedBefore.evidenceSha256()));
    notAppliedAudit.append(providerEvent(
        15, Data::RuntimePackageActivationAuditKind::ProviderOutcomeReconciled,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        Data::RuntimePackageActivationProviderAction::DeployPackage,
        Data::RuntimePackageActivationProviderReconciliation::NotApplied,
        QStringLiteral("deploy-op"), sessionGeneration, {}, {}, {},
        reconnectedBefore.evidenceSha256(), startedAt.addMSecs(15)));
    notAppliedAudit.append(providerEvent(
        16, Data::RuntimePackageActivationAuditKind::ControlLeaseReconciled,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        Data::RuntimePackageActivationProviderAction::ReleaseControl,
        Data::RuntimePackageActivationProviderReconciliation::Applied,
        QStringLiteral("lease-reconcile"), sessionGeneration, {}, {}, {},
        reconnectedBefore.evidenceSha256(), startedAt.addMSecs(16)));
    notAppliedAudit.append(localEvent(
        17, Data::RuntimePackageActivationAuditKind::Completed,
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange,
        QStringLiteral("deploy-not-applied"), startedAt.addMSecs(17),
        reconnectedBefore.evidenceSha256()));
    const Data::RuntimePackageActivationRecord notAppliedAfterReconnect{
        identity, request.fingerprint(), true, quint64(notAppliedAudit.size()),
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange,
        notAppliedAudit, notAppliedAudit.constLast().detail(), startedAt,
        notAppliedAudit.constLast().occurredAt(),
        notAppliedAudit.constLast().occurredAt(),
        before, reconnectedBefore, {}, {}, {}, {acquired},
    };
    QVERIFY(notAppliedAfterReconnect.isValid());

    const Data::RuntimePackageActivationDeploymentEvidence uncapturedDeployment{
        sessionGeneration,
        sessionId,
        bootId,
        progress,
        audit.at(10).occurredAt(),
    };
    QVERIFY(uncapturedDeployment.isValid());
    const Data::RuntimePackageActivationRecord deploymentWithoutCapture{
        identity, request.fingerprint(), true, 11,
        Data::RuntimePackageActivationPhase::DeployingPackage,
        Data::RuntimePackageActivationOutcome::Pending,
        audit.mid(0, 11), audit.at(10).detail(), startedAt,
        audit.at(10).occurredAt(), {}, before, {}, {}, {}, uncapturedDeployment,
        {acquired},
    };
    QVERIFY(!deploymentWithoutCapture.isValid());

    const Data::RuntimePackageActivationRecord commitCapturedPending{
        identity, request.fingerprint(), true, 18,
        Data::RuntimePackageActivationPhase::CommittingProjectBinding,
        Data::RuntimePackageActivationOutcome::Pending,
        audit.mid(0, 18), audit.at(17).detail(), startedAt,
        audit.at(17).occurredAt(), {}, before, afterHeld, projectCommit, {},
        deployment, {acquired},
    };
    QVERIFY(commitCapturedPending.isValid());

    const Data::RuntimePackageActivationRecord commitCrash{
        identity, request.fingerprint(), true, 19,
        Data::RuntimePackageActivationPhase::ReleasingControl,
        Data::RuntimePackageActivationOutcome::Pending,
        audit.mid(0, 19), audit.at(18).detail(), startedAt,
        audit.at(18).occurredAt(), {}, before, afterHeld, projectCommit, {},
        deployment, {acquired},
    };
    QVERIFY(commitCrash.isValid());

    auto unknownAudit = audit.mid(0, 20);
    unknownAudit.append(localEvent(
        21, Data::RuntimePackageActivationAuditKind::PhaseTransition,
        Data::RuntimePackageActivationPhase::AwaitingReconciliation,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("release-unknown"), startedAt.addMSecs(20)));
    const Data::RuntimePackageActivationRecord unknownRelease{
        identity, request.fingerprint(), true, quint64(unknownAudit.size()),
        Data::RuntimePackageActivationPhase::AwaitingReconciliation,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        unknownAudit, unknownAudit.constLast().detail(), startedAt,
        unknownAudit.constLast().occurredAt(), {}, before, afterHeld,
        projectCommit, {}, deployment, {acquired},
    };
    QVERIFY(unknownRelease.isValid());

    auto recoveredAudit = unknownAudit;
    recoveredAudit.append(localEvent(
        22, Data::RuntimePackageActivationAuditKind::ReconciliationStarted,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("reconciling"), startedAt.addMSecs(21)));
    recoveredAudit.append(localEvent(
        23, Data::RuntimePackageActivationAuditKind::ControllerEvidenceCaptured,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        QStringLiteral("released-after-crash"), startedAt.addMSecs(22),
        afterReleased.evidenceSha256()));
    recoveredAudit.append(providerEvent(
        24, Data::RuntimePackageActivationAuditKind::ProviderOutcomeReconciled,
        Data::RuntimePackageActivationPhase::Reconciling,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        Data::RuntimePackageActivationProviderAction::ReleaseControl,
        Data::RuntimePackageActivationProviderReconciliation::Applied,
        QStringLiteral("release-op"), sessionGeneration, 42, {}, {},
        afterReleased.evidenceSha256(), startedAt.addMSecs(23)));
    recoveredAudit.append(localEvent(
        25, Data::RuntimePackageActivationAuditKind::Completed,
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage,
        QStringLiteral("recovered"), startedAt.addMSecs(24),
        projectCommit.evidenceSha256()));
    const Data::RuntimePackageActivationRecord recovered{
        identity, request.fingerprint(), true, quint64(recoveredAudit.size()),
        Data::RuntimePackageActivationPhase::Finished,
        Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage,
        recoveredAudit, recoveredAudit.constLast().detail(), startedAt,
        recoveredAudit.constLast().occurredAt(),
        recoveredAudit.constLast().occurredAt(),
        before, afterHeld, projectCommit, {}, deployment, {acquired, afterReleased},
    };
    QVERIFY(recovered.isValid());

    QVERIFY(Data::runtimePackageActivationPhaseAllowsCancel(
        Data::RuntimePackageActivationPhase::ProbingExistingPackage));
    QVERIFY(!Data::runtimePackageActivationPhaseAllowsCancel(
        Data::RuntimePackageActivationPhase::AcquiringControl));
    QVERIFY(!Data::runtimePackageActivationPhaseAllowsCancel(
        Data::RuntimePackageActivationPhase::DeployingPackage));
    for (Data::ControllerOperation denied :
         {Data::ControllerOperation::QueryState,
          Data::ControllerOperation::QueryCapability,
          Data::ControllerOperation::QueryPackageState,
          Data::ControllerOperation::QueryRuntimeResourceCatalog,
          Data::ControllerOperation::QueryRuntimeResourceSnapshot,
          Data::ControllerOperation::QueryRuntimeSemanticMappingAttestation,
          Data::ControllerOperation::Refresh,
          Data::ControllerOperation::Start,
          Data::ControllerOperation::StartFreeRun,
          Data::ControllerOperation::StartDistributedClocks,
          Data::ControllerOperation::ActivatePackage,
          Data::ControllerOperation::RollbackPackage}) {
        QVERIFY(!Data::runtimePackageActivationProviderOperationIsAllowed(denied));
    }
    const Data::RuntimePackageActivationAuditEvent orphanQueryTerminal{
        1,
        Data::RuntimePackageActivationAuditKind::ProviderTerminalResponse,
        Data::RuntimePackageActivationPhase::ProbingExistingPackage,
        Data::RuntimePackageActivationOutcome::Pending,
        Data::RuntimePackageActivationProviderAction::None,
        Data::RuntimePackageActivationProviderReconciliation::None,
        QStringLiteral("query-state"),
        sessionGeneration,
        sessionId,
        bootId,
        91,
        0,
        0,
        sha256(QByteArrayView("orphan-query-evidence")),
        QStringLiteral("orphan-query-terminal"),
        QStringLiteral("orphan-query-terminal"),
        startedAt,
    };
    QVERIFY(!orphanQueryTerminal.isValid());

    TestRuntimePackageActivationService service;
    QCOMPARE(
        service.start(request).disposition,
        RuntimePackageActivationCommandDisposition::InvalidRequest);
    service.authorizeForTest(request);
    const RuntimePackageActivationCommandResult started = service.start(request);
    QVERIFY(started.isValid());
    QCOMPARE(started.disposition, RuntimePackageActivationCommandDisposition::Accepted);
    QCOMPARE(service.start(request).disposition,
             RuntimePackageActivationCommandDisposition::IdempotentReplay);
    QCOMPARE(service.start(noRollbackRequest).disposition,
             RuntimePackageActivationCommandDisposition::Conflict);
    const Data::RuntimePackageActivationCancelRequest cancelRequest{
        identity.operationId(), started.record->revision(),
        identity.documentRevisionToken(), identity.originalBindingToken()};
    QCOMPARE(service.cancel(cancelRequest).disposition,
             RuntimePackageActivationCommandDisposition::Accepted);

    const Data::RuntimePackageActivationRecord acquirePending{
        identity, request.fingerprint(), true, 7,
        Data::RuntimePackageActivationPhase::AcquiringControl,
        Data::RuntimePackageActivationOutcome::Pending,
        audit.mid(0, 7), audit.at(6).detail(), startedAt,
        audit.at(6).occurredAt(), {}, before,
    };
    QVERIFY(acquirePending.isValid());
    TestRuntimePackageActivationService pendingService;
    pendingService.replaceForTest(acquirePending);
    const Data::RuntimePackageActivationCancelRequest unsafeCancel{
        identity.operationId(), acquirePending.revision(),
        identity.documentRevisionToken(), identity.originalBindingToken()};
    QCOMPARE(pendingService.cancel(unsafeCancel).disposition,
             RuntimePackageActivationCommandDisposition::TooLate);

    const Data::RuntimePackageActivationSnapshot snapshot{
        1, {activatedSuccess}, activatedSuccess.updatedAt()};
    QVERIFY(snapshot.isValid());

    QVERIFY(QMetaType::fromType<Data::RuntimePackageActivationControllerEvidence>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageActivationDeploymentEvidence>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageActivationProjectCapture>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageActivationProjectCommit>().isValid());
    QVERIFY(
        QMetaType::fromType<
            Data::RuntimePackageActivationProjectCompareAndSetResult>()
            .isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageActivationRecord>().isValid());
    QVERIFY(QMetaType::fromType<RuntimePackageActivationCommandResult>().isValid());
}

void EtherCATCoreTests::testRuntimePackageCompilerValueSemantics()
{
    RuntimePackageCompilerFixture fixture;
    QVERIFY(fixture.request.isValid());
    QVERIFY(fixture.request.hasValidReservationInputs());
    QCOMPARE(
        fixture.request.projectSnapshotEvidence.documentRevisionNumber(), quint64(7));
    const auto &selectedSlave
        = fixture.request.projectSnapshotEvidence.snapshot().slaves.constFirst();
    const auto &deviceSource
        = fixture.request.deviceSourceEvidence.constFirst();
    QCOMPARE(deviceSource.projectAdapterId, selectedSlave.adapterSelection.adapterId);
    QCOMPARE(
        deviceSource.projectAdapterContentSha256.value(),
        selectedSlave.adapterSelection.adapterContentSha256);
    QCOMPARE(
        deviceSource.projectControllerAdapterTarget.adapterId,
        deviceSource.adapterId);
    QCOMPARE(
        deviceSource.projectSignedPdoProfileId,
        deviceSource.pdoProfileId);
    QVERIFY(
        deviceSource.adapterId
        != selectedSlave.adapterSelection.adapterId.value);
    const Data::RuntimePackageCompilerCompileRequest requestCopy = fixture.request;
    QCOMPARE(requestCopy, fixture.request);
    const Data::RuntimePackageActivationProjectCapture invalidSourceCapture{
        fixture.request.projectSnapshotEvidence.snapshot(),
        {},
        fixture.request.projectSnapshotEvidence.documentRevisionNumber(),
        fixture.request.projectSnapshotEvidence.documentRevision(),
        fixture.request.projectSnapshotEvidence.originalBinding(),
    };
    QVERIFY(!invalidSourceCapture.isValid());
    QVERIFY(
        !Data::RuntimePackageCompilerProjectSnapshotEvidence{invalidSourceCapture}
             .isValid());
    QVERIFY(Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000001")}
                .isValid());
    QVERIFY(!Data::RuntimePackageCompilerOperationId{QStringLiteral("not-a-uuid")}.isValid());

    const auto preservedJson = RuntimePackageCompilerFixture::canonical(
        QByteArray("{\"unknown\":{\"future\":true},\"z\":1}\n"));
    QVERIFY(preservedJson.isValid());
    QCOMPARE(
        preservedJson.exactBytes(),
        QByteArray("{\"unknown\":{\"future\":true},\"z\":1}\n"));
    QVERIFY(!RuntimePackageCompilerFixture::canonical(
                 QByteArray("{\"z\":1,\"unknown\":{\"future\":true}}\n"))
                 .isValid());
    QVERIFY(!RuntimePackageCompilerFixture::canonical(
                 QByteArray("{\"duplicate\":1,\"duplicate\":2}\n"))
                 .isValid());
    QVERIFY(!RuntimePackageCompilerFixture::canonical(QByteArray("{\"number\":1.0}\n"))
                 .isValid());
    const QList<QByteArray> nonCanonicalJson{
        QByteArray("{\"a\":1}\r\n"),
        QByteArray("{\"a\":1}\n\n"),
        QByteArray(" {\"a\":1}\n"),
        QByteArray("{\"a\": 1}\n"),
        QByteArray("[]\n"),
        QByteArray("{\"number\":1e0}\n"),
        QByteArray("{\"number\":-0}\n"),
        QByteArray("{\"number\":01}\n"),
        QByteArray("{\"escaped\":\"\\u4E2D\"}\n"),
        QByteArray("{\"escaped\":\"\\u0061\"}\n"),
        QByteArray("{\"nested\":{\"a\":1,\"a\":2}}\n"),
        QByteArray("{\"escaped\":\"") + QByteArray::fromHex("e4b8ad") + QByteArray("\"}\n"),
    };
    for (const QByteArray &json : nonCanonicalJson)
        QVERIFY(!RuntimePackageCompilerFixture::canonical(json).isValid());
    QVERIFY(
        RuntimePackageCompilerFixture::canonical(QByteArray("{\"escaped\":\"\\u4e2d\"}\n"))
            .isValid());
    QVERIFY(RuntimePackageCompilerFixture::canonical(
                QByteArray(
                    "{\"array\":[-1,0,1,{\"nested\":\"\\ud83d\\ude00\"}],"
                    "\"escaped\":\"line\\nquote\\\"slash\\\\\"}\n"))
                .isValid());
    const Data::RuntimePackageCompilerCanonicalJson
        changedJson(QByteArray("{\"z\":2}\n"), preservedJson.sha256());
    QVERIFY(!changedJson.isValid());
    QVERIFY(!Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(QByteArray("{\"z\":1}"))
                 .isValid());

    Data::RuntimePackageCompilerSourceArtifact unsafePath
        = fixture.request.sourceArtifacts.adapterBundle;
    unsafePath.relativePath = QStringLiteral("../adapter-bundle-v1.json");
    QVERIFY(!unsafePath.isValid());
    for (const QString &path :
         {QStringLiteral("adapter//bundle.json"),
          QStringLiteral("adapter/bundle/"),
          QStringLiteral("adapter/./bundle.json")}) {
        unsafePath.relativePath = path;
        QVERIFY(!unsafePath.isValid());
    }

    Data::RuntimePackageCompilerCompileRequest missingCapture = fixture.request;
    missingCapture.projectSnapshotEvidence = {};
    QVERIFY(!missingCapture.isValid());

    Data::RuntimePackageCompilerCompileRequest staleTopology = fixture.request;
    staleTopology.compileTimeNs = staleTopology.topologyEvidence.expiresAtNs + 1;
    QVERIFY(!staleTopology.isValid());
    staleTopology = fixture.request;
    staleTopology.topologyEvidence.expiresAtNs = staleTopology.topologyEvidence.capturedAtNs;
    QVERIFY(!staleTopology.isValid());

    Data::RuntimePackageCompilerCompileRequest missingEsi = fixture.request;
    missingEsi.deviceSourceEvidence[0].originalEsi.exactBytes.clear();
    QVERIFY(!missingEsi.isValid());

    Data::RuntimePackageCompilerCompileRequest missingBundle = fixture.request;
    missingBundle.sourceArtifacts.adapterBundle = {};
    QVERIFY(!missingBundle.isValid());

    Data::RuntimePackageCompilerCompileRequest missingDcDecision = fixture.request;
    missingDcDecision.deviceSourceEvidence[0].explicitNoDc = false;
    QVERIFY(!missingDcDecision.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperAdapter
        = fixture.request;
    changedUpperAdapter.deviceSourceEvidence[0].projectAdapterId.value.append(
        QStringLiteral(".different"));
    QVERIFY(!changedUpperAdapter.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperAdapterVersion
        = fixture.request;
    changedUpperAdapterVersion.deviceSourceEvidence[0].projectAdapterVersion
        .append(QStringLiteral(".different"));
    QVERIFY(!changedUpperAdapterVersion.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperAdapterContent
        = fixture.request;
    changedUpperAdapterContent.deviceSourceEvidence[0]
        .projectAdapterContentSha256
        = RuntimePackageCompilerFixture::sha256("different-upper-manifest");
    QVERIFY(!changedUpperAdapterContent.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperProfile
        = fixture.request;
    changedUpperProfile.deviceSourceEvidence[0].projectPdoProfileId.append(
        QStringLiteral(".different"));
    QVERIFY(!changedUpperProfile.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperControllerTarget
        = fixture.request;
    changedUpperControllerTarget.deviceSourceEvidence[0]
        .projectControllerAdapterTarget.adapterId.append(
            QStringLiteral(".different"));
    QVERIFY(!changedUpperControllerTarget.isValid());

    Data::RuntimePackageCompilerCompileRequest
        changedUpperControllerTargetVersion = fixture.request;
    changedUpperControllerTargetVersion.deviceSourceEvidence[0]
        .projectControllerAdapterTarget.adapterVersion.append(
            QStringLiteral(".different"));
    QVERIFY(!changedUpperControllerTargetVersion.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperControllerSha
        = fixture.request;
    changedUpperControllerSha.deviceSourceEvidence[0]
        .projectControllerAdapterTarget.adapterSha256[0] ^= 1;
    QVERIFY(!changedUpperControllerSha.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperControllerEsi
        = fixture.request;
    changedUpperControllerEsi.deviceSourceEvidence[0]
        .projectControllerAdapterTarget.esiSha256[0] ^= 1;
    QVERIFY(!changedUpperControllerEsi.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperPdoBinding
        = fixture.request;
    changedUpperPdoBinding.deviceSourceEvidence[0]
        .projectSignedPdoProfileId.append(QStringLiteral("_different"));
    QVERIFY(!changedUpperPdoBinding.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperDcBinding
        = fixture.request;
    changedUpperDcBinding.deviceSourceEvidence[0].projectSignedDcProfileId
        = QStringLiteral("sync0_125us");
    QVERIFY(!changedUpperDcBinding.isValid());

    Data::RuntimePackageCompilerCompileRequest changedUpperContract
        = fixture.request;
    changedUpperContract.deviceSourceEvidence[0]
        .projectAdapterContractVersion = Data::DeviceAdapterContractVersion::V2;
    QVERIFY(!changedUpperContract.isValid());

    Data::RuntimePackageCompilerCompileRequest changedLowerAdapter
        = fixture.request;
    changedLowerAdapter.deviceSourceEvidence[0].adapterId.append(
        QStringLiteral(".different"));
    QVERIFY(!changedLowerAdapter.isValid());

    Data::RuntimePackageCompilerCompileRequest changedLowerAdapterVersion
        = fixture.request;
    changedLowerAdapterVersion.deviceSourceEvidence[0].adapterVersion.append(
        QStringLiteral(".different"));
    QVERIFY(!changedLowerAdapterVersion.isValid());

    Data::RuntimePackageCompilerCompileRequest changedLowerAdapterSha
        = fixture.request;
    changedLowerAdapterSha.deviceSourceEvidence[0].adapterCanonicalSha256
        = RuntimePackageCompilerFixture::sha256("different-lower-adapter");
    QVERIFY(!changedLowerAdapterSha.isValid());

    Data::RuntimePackageCompilerCompileRequest changedLowerPdo
        = fixture.request;
    changedLowerPdo.deviceSourceEvidence[0].pdoProfileId.append(
        QStringLiteral("_different"));
    QVERIFY(!changedLowerPdo.isValid());

    Data::RuntimePackageCompilerCompileRequest dcRequest = fixture.request;
    Data::ProjectSnapshot dcProject
        = dcRequest.projectSnapshotEvidence.snapshot();
    dcProject.masterConfiguration.timingMode
        = Data::MasterTimingMode::DistributedClocks;
    dcProject.slaves[0].dc.enabled = true;
    dcProject.slaves[0].dc.modeName = QStringLiteral("DC");
    dcProject.slaves[0].dc.assignActivate = 0x0300;
    dcProject.slaves[0].dc.sync0 = {true, 125000, 0};
    dcRequest.projectSnapshotEvidence
        = Data::RuntimePackageCompilerProjectSnapshotEvidence{
            Data::RuntimePackageActivationProjectCapture{
                dcProject,
                QByteArray("{\"format\":\"embed-labs-ethercat-project\"}\n"),
                7,
                Data::RuntimePackageActivationDocumentRevisionToken{
                    "revision-7"},
                Data::RuntimePackageActivationOriginalBindingToken{
                    "binding-empty"},
            }};
    dcRequest.deviceSourceEvidence[0].explicitNoDc = false;
    dcRequest.deviceSourceEvidence[0].projectSignedDcProfileId
        = QStringLiteral("sync0_125us");
    dcRequest.deviceSourceEvidence[0].signedDcProfileId
        = QStringLiteral("sync0_125us");
    dcRequest.projectProjection.timingMode = Data::MasterTimingMode::DistributedClocks;
    dcRequest.projectProjection.devices[0].signedDcProfileId = QStringLiteral("sync0_125us");
    dcRequest.projectProjection.devices[0].dc = {
        true,
        QStringLiteral("sync0_125us"),
        QStringLiteral("DC"),
        0x0300,
        125000,
        0,
        0,
        0,
        false,
    };
    QVERIFY(dcRequest.isValid());

    Data::RuntimePackageCompilerCompileRequest mismatchedLowerDc = dcRequest;
    mismatchedLowerDc.deviceSourceEvidence[0].signedDcProfileId
        = QStringLiteral("sync0_250us");
    QVERIFY(!mismatchedLowerDc.isValid());

    Data::RuntimePackageCompilerCompileRequest missingUpperDc = dcRequest;
    missingUpperDc.deviceSourceEvidence[0].projectSignedDcProfileId.clear();
    QVERIFY(!missingUpperDc.isValid());

    Data::RuntimePackageCompilerCompileRequest ambiguousNoDc = fixture.request;
    ambiguousNoDc.deviceSourceEvidence[0].signedDcProfileId
        = QStringLiteral("sync0_125us");
    QVERIFY(!ambiguousNoDc.isValid());

    Data::RuntimePackageCompilerCompileRequest missingTarget = fixture.request;
    missingTarget.targetProfile = {};
    QVERIFY(!missingTarget.isValid());

    Data::RuntimePackageCompilerCompileRequest changedStation = fixture.request;
    ++changedStation.topologyEvidence.slaves[0].stationAddress;
    QVERIFY(!changedStation.isValid());

    Data::RuntimePackageCompilerCompileRequest changedCanonicalTopology = fixture.request;
    QByteArray changedTopologyBytes
        = changedCanonicalTopology.topologyEvidence.canonicalEvidence.exactBytes();
    changedTopologyBytes.replace(
        QByteArray("\"station_address\":4097"),
        QByteArray("\"station_address\":4098"));
    changedCanonicalTopology.topologyEvidence.canonicalEvidence
        = RuntimePackageCompilerFixture::canonical(changedTopologyBytes);
    changedCanonicalTopology.sourceArtifacts.topologyEvidence.exactBytes
        = changedTopologyBytes;
    changedCanonicalTopology.sourceArtifacts.topologyEvidence.sha256
        = changedCanonicalTopology.topologyEvidence.canonicalEvidence.sha256();
    QVERIFY(!changedCanonicalTopology.isValid());

    Data::RuntimePackageCompilerCompileRequest changedCanonicalTarget = fixture.request;
    QByteArray changedTargetBytes
        = changedCanonicalTarget.targetProfile.canonicalProfile.exactBytes();
    changedTargetBytes.replace(
        QByteArray("\"policy_revision\":1"),
        QByteArray("\"policy_revision\":2"));
    changedCanonicalTarget.targetProfile.canonicalProfile
        = RuntimePackageCompilerFixture::canonical(changedTargetBytes);
    changedCanonicalTarget.sourceArtifacts.targetProfile.exactBytes = changedTargetBytes;
    changedCanonicalTarget.sourceArtifacts.targetProfile.sha256
        = changedCanonicalTarget.targetProfile.canonicalProfile.sha256();
    QVERIFY(!changedCanonicalTarget.isValid());

    Data::RuntimePackageCompilerCompileRequest changedAdapterSource = fixture.request;
    auto &adapterSource = changedAdapterSource.deviceSourceEvidence[0].adapterSourceFile;
    adapterSource.exactBytes.append("# changed\n");
    adapterSource.sha256 = RuntimePackageCompilerFixture::sha256(adapterSource.exactBytes);
    QVERIFY(adapterSource.isValid());
    QVERIFY(!changedAdapterSource.isValid());

    const QList<Data::RuntimePackageCompilerDiagnosticCategory> categories{
        Data::RuntimePackageCompilerDiagnosticCategory::Slave,
        Data::RuntimePackageCompilerDiagnosticCategory::Pdo,
        Data::RuntimePackageCompilerDiagnosticCategory::Sdo,
        Data::RuntimePackageCompilerDiagnosticCategory::Dc,
        Data::RuntimePackageCompilerDiagnosticCategory::Adapter,
        Data::RuntimePackageCompilerDiagnosticCategory::Capability,
        Data::RuntimePackageCompilerDiagnosticCategory::Timing,
        Data::RuntimePackageCompilerDiagnosticCategory::Signing,
        Data::RuntimePackageCompilerDiagnosticCategory::Path,
        Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
    };
    for (Data::RuntimePackageCompilerDiagnosticCategory category : categories)
        QVERIFY(compilerDiagnostic(category, QStringLiteral("ECOMP-TEST")).isValid());
    auto mismatchedDiagnostic = compilerDiagnostic(
        Data::RuntimePackageCompilerDiagnosticCategory::Capability, QStringLiteral("ECOMP-TEST"));
    mismatchedDiagnostic.message = QStringLiteral("Typed and canonical diagnostics diverged.");
    QVERIFY(!mismatchedDiagnostic.isValid());
    QVERIFY(
        !compilerDiagnostic(
             Data::RuntimePackageCompilerDiagnosticCategory::Capability, QStringLiteral("ECOMP-"))
             .isValid());
    QVERIFY(!compilerDiagnostic(
                 Data::RuntimePackageCompilerDiagnosticCategory::Capability,
                 QStringLiteral("ecomp-test"))
                 .isValid());
    QVERIFY(!compilerDiagnostic(
                 Data::RuntimePackageCompilerDiagnosticCategory::Capability,
                 QStringLiteral("ECOMP-TEST"),
                 QStringLiteral("future"))
                 .isValid());

    const Data::RuntimePackageCompilerCompileResult compileResult = successfulCompileResult(
        fixture.request);
    QVERIFY(compileResult.isValid());
    QVERIFY(compileResult.isSuccess());
    auto mismatchedCompileResult = compileResult;
    mismatchedCompileResult.outputDirectory = QStringLiteral("/tmp/different-output");
    QVERIFY(!mismatchedCompileResult.isValid());
    auto extraFieldCompileResult = compileResult;
    QJsonObject extraFieldObject = QJsonDocument::fromJson(
                                       compileResult.envelope.canonicalResult.exactBytes())
                                       .object();
    extraFieldObject.insert(QStringLiteral("unexpected"), true);
    extraFieldCompileResult.envelope.canonicalResult
        = RuntimePackageCompilerFixture::canonical(
            QJsonDocument(extraFieldObject).toJson(QJsonDocument::Compact) + '\n');
    QVERIFY(!extraFieldCompileResult.isValid());
    QCOMPARE(
        Data::RuntimePackageCompilerJobResult{compileResult},
        Data::RuntimePackageCompilerJobResult{compileResult});

    const auto unknownEnvelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Compile,
        Data::RuntimePackageCompilerResultStatus::Unknown,
        QStringLiteral("future_backend_status"),
        fixture.request.operationId,
        fixture.request.configurationId,
        compilerRequestSha256(fixture.request));
    const Data::RuntimePackageCompilerCompileResult
        unknownResult{unknownEnvelope, {}, {}, {}, {}, {}, {}, {}, {}, {}};
    QVERIFY(unknownResult.isValid());
    QVERIFY(!unknownResult.isSuccess());
    QVERIFY(!Data::RuntimePackageCompilerJobResult{unknownResult}.isSuccess());

    auto mislabeledSuccess = unknownEnvelope;
    mislabeledSuccess.status = Data::RuntimePackageCompilerResultStatus::Succeeded;
    QVERIFY(!mislabeledSuccess.isValid());

    const auto malformedFailureEnvelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Compile,
        Data::RuntimePackageCompilerResultStatus::Canceled,
        QStringLiteral("cancelled"),
        fixture.request.operationId,
        fixture.request.configurationId,
        compilerRequestSha256(fixture.request),
        {},
        RuntimePackageCompilerFixture::canonical(
            QByteArray("{\"diagnostics\":{},\"status\":\"fail\"}\n")));
    QVERIFY(!malformedFailureEnvelope.isValid());

    auto largeDiagnostic = compilerDiagnostic(
        Data::RuntimePackageCompilerDiagnosticCategory::Idempotency,
        QStringLiteral("ECOMP-LARGE-DETAIL"));
    QByteArray largeDiagnosticBytes
        = largeDiagnostic.canonicalJson.exactBytes();
    largeDiagnosticBytes.replace(
        QByteArray("\"format\""),
        QByteArray("\"details\":{\"value\":9223372036854775808},\"format\""));
    largeDiagnostic.canonicalJson
        = RuntimePackageCompilerFixture::canonical(largeDiagnosticBytes);
    QVERIFY(largeDiagnostic.isValid());
    QByteArray mismatchedDiagnosticBytes = largeDiagnosticBytes;
    mismatchedDiagnosticBytes.replace(
        QByteArray("9223372036854775808"),
        QByteArray("9223372036854775809"));
    mismatchedDiagnosticBytes.chop(1);
    const auto lossyFailureEnvelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Compile,
        Data::RuntimePackageCompilerResultStatus::DomainFailed,
        QStringLiteral("fail"),
        fixture.request.operationId,
        fixture.request.configurationId,
        compilerRequestSha256(fixture.request),
        {largeDiagnostic},
        RuntimePackageCompilerFixture::canonical(
            QByteArray("{\"diagnostics\":[") + mismatchedDiagnosticBytes
            + QByteArray("],\"status\":\"fail\"}\n")));
    QVERIFY(!lossyFailureEnvelope.isValid());

    const Data::RuntimePackageCompilerFinalizeRequest finalizeRequest{
        fixture.request.operationId,
        fixture.request.configurationId,
        fixture.request.contractIdentity,
        compilerRequestSha256(fixture.request),
        compileResult.signRequest->sha256(),
        *compileResult.manifestSha256,
        fixture.request.targetProfile.signingKeyIdSha256,
        fixture.request.targetProfile.policyRevision,
        detachedSigningResponse(fixture.request, compileResult),
    };
    QVERIFY(finalizeRequest.isValid());
    const Data::RuntimePackageCompilerFinalizeResult finalizeResult = successfulFinalizeResult(
        finalizeRequest);
    QVERIFY(finalizeResult.isValid());
    QVERIFY(finalizeResult.isSuccess());

    const Data::RuntimePackageCompilerQueryRequest queryRequest{
        fixture.request.operationId,
        fixture.request.contractIdentity,
        compilerRequestSha256(fixture.request),
    };
    QVERIFY(queryRequest.isValid());
    const auto signerOnlyResponse = finalizeRequest.detachedSigningResponse;
    const QJsonObject signerOnlyObject{
        {QStringLiteral("compiler"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("format"), QStringLiteral("ethercat-ide-compiler-operation-state-v1")},
        {QStringLiteral("operation_id"), queryRequest.operationId.value()},
        {QStringLiteral("signer_response"),
         QJsonDocument::fromJson(signerOnlyResponse.exactBytes()).object()},
    };
    const auto signerOnlyCanonical = RuntimePackageCompilerFixture::canonical(
        QJsonDocument(signerOnlyObject).toJson(QJsonDocument::Compact) + '\n');
    const auto signerOnlyEnvelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Query,
        Data::RuntimePackageCompilerResultStatus::Succeeded,
        QStringLiteral("state"),
        queryRequest.operationId,
        0,
        compilerRequestSha256(queryRequest),
        {},
        signerOnlyCanonical);
    const Data::RuntimePackageCompilerQueryResult signerOnlyQuery{
        signerOnlyEnvelope, {}, signerOnlyResponse};
    QVERIFY(signerOnlyQuery.isValid());
    QVERIFY(signerOnlyQuery.isSuccess());
    QVERIFY(!signerOnlyQuery.hasCompilerRecord());
    QVERIFY(signerOnlyQuery.hasSignerResponse());
    auto mismatchedSignerOnlyQuery = signerOnlyQuery;
    mismatchedSignerOnlyQuery.signerResponse = RuntimePackageCompilerFixture::canonical(
        QByteArray("{\"format\":\"different-signer-response\"}\n"));
    QVERIFY(!mismatchedSignerOnlyQuery.isValid());

    const auto largeCompilerRecord = RuntimePackageCompilerFixture::canonical(
        QByteArray(
            "{\"configuration_id\":9223372036854775808,\"format\":\"record\"}\n"));
    const auto lossyQueryCanonical = RuntimePackageCompilerFixture::canonical(
        QStringLiteral(
            "{\"compiler\":{\"configuration_id\":9223372036854775809,"
            "\"format\":\"record\"},"
            "\"format\":\"ethercat-ide-compiler-operation-state-v1\","
            "\"operation_id\":\"%1\",\"signer_response\":null}\n")
            .arg(queryRequest.operationId.value())
            .toLatin1());
    QVERIFY(largeCompilerRecord.isValid());
    QVERIFY(lossyQueryCanonical.isValid());
    const Data::RuntimePackageCompilerQueryResult lossyQuery{
        compilerEnvelope(
            Data::RuntimePackageCompilerCommand::Query,
            Data::RuntimePackageCompilerResultStatus::Succeeded,
            QStringLiteral("state"),
            queryRequest.operationId,
            0,
            compilerRequestSha256(queryRequest),
            {},
            lossyQueryCanonical),
        largeCompilerRecord,
        {},
    };
    QVERIFY(!lossyQuery.isValid());

    const Data::RuntimePackageCompilerVerifyRequest verifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000099")},
        fixture.request.contractIdentity,
        finalizeResult.packageBytes,
        *finalizeResult.packageSha256,
    };
    QVERIFY(verifyRequest.isValid());

    const auto invalidVerifyEnvelope = compilerEnvelope(
        Data::RuntimePackageCompilerCommand::Verify,
        Data::RuntimePackageCompilerResultStatus::Succeeded,
        QStringLiteral("pass"),
        verifyRequest.operationId,
        0,
        compilerRequestSha256(verifyRequest));
    const Data::RuntimePackageCompilerVerifyResult invalidVerifyResult{
        invalidVerifyEnvelope,
        verifyRequest.packageSha256,
        2,
        RuntimePackageCompilerFixture::sha256("intent"),
        RuntimePackageCompilerFixture::sha256("effective-project-companion"),
        RuntimePackageCompilerFixture::sha256("target-profile"),
        RuntimePackageCompilerFixture::sha256("adapter-bundle"),
        RuntimePackageCompilerFixture::sha256("topology-evidence"),
        true,
    };
    QVERIFY(!invalidVerifyResult.isValid());

    const Data::RuntimePackageCompilerActivationProof activationProof
        = successfulActivationProof(fixture.request);
    QVERIFY(activationProof.isValid());
    QCOMPARE(activationProof, activationProof);
    QVERIFY(
        activationProof.verifyRequest.operationId
        != activationProof.compileRequest.operationId);

    Data::RuntimePackageCompilerActivationProof invalidActivationProof;
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compilerProviderId.clear();
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compilerProviderId = QStringLiteral(" provider");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.contractIdentity.contractId.append(QStringLiteral(".different"));
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compileRequest.operationId
        = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000002")};
    QVERIFY(invalidActivationProof.compileRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    ++invalidActivationProof.compileRequest.configurationId;
    QVERIFY(invalidActivationProof.compileRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compileRequest.contractIdentity.contractId.append(
        QStringLiteral(".different"));
    QVERIFY(invalidActivationProof.compileRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compileResult.envelope.requestSha256
        = RuntimePackageCompilerFixture::sha256("different-compile-request");
    QVERIFY(invalidActivationProof.compileResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compileResult.envelope.status
        = Data::RuntimePackageCompilerResultStatus::DomainFailed;
    QVERIFY(!invalidActivationProof.isValid());

    const auto proofWithCoordinatedSignRequestMutation =
        [&](const QString &key, const QJsonValue &value) {
            Data::RuntimePackageCompilerActivationProof changed = activationProof;
            QJsonObject signRequestObject = QJsonDocument::fromJson(
                                                changed.compileResult.signRequest->exactBytes())
                                                .object();
            signRequestObject.insert(key, value);
            changed.compileResult.signRequest = RuntimePackageCompilerFixture::canonical(
                QJsonDocument(signRequestObject).toJson(QJsonDocument::Compact) + '\n');

            QJsonObject compileResultObject = QJsonDocument::fromJson(
                                                  changed.compileResult.envelope.canonicalResult
                                                      .exactBytes())
                                                  .object();
            compileResultObject.insert(
                QStringLiteral("sign_request_sha256"),
                QString::fromLatin1(
                    changed.compileResult.signRequest->sha256().value().toHex()));
            changed.compileResult.envelope.canonicalResult
                = RuntimePackageCompilerFixture::canonical(
                    QJsonDocument(compileResultObject).toJson(QJsonDocument::Compact) + '\n');

            changed.finalizeRequest.signRequestSha256
                = changed.compileResult.signRequest->sha256();
            changed.finalizeRequest.detachedSigningResponse
                = detachedSigningResponse(changed.compileRequest, changed.compileResult);
            const QJsonObject signingResponse = QJsonDocument::fromJson(
                                                    changed.finalizeRequest
                                                        .detachedSigningResponse.exactBytes())
                                                    .object();
            changed.finalizeResult.signingReceiptSha256 = Data::RuntimePackageCompilerSha256{
                QByteArray::fromHex(
                    signingResponse.value(QStringLiteral("receipt_sha256"))
                        .toString()
                        .toLatin1())};
            QJsonObject finalizeResultObject = QJsonDocument::fromJson(
                                                   changed.finalizeResult.envelope.canonicalResult
                                                       .exactBytes())
                                                   .object();
            finalizeResultObject.insert(
                QStringLiteral("signing_receipt_sha256"),
                QString::fromLatin1(
                    changed.finalizeResult.signingReceiptSha256->value().toHex()));
            changed.finalizeResult.envelope.canonicalResult
                = RuntimePackageCompilerFixture::canonical(
                    QJsonDocument(finalizeResultObject).toJson(QJsonDocument::Compact) + '\n');
            return changed;
        };

    invalidActivationProof = proofWithCoordinatedSignRequestMutation(
        QStringLiteral("signing_key_id"),
        QString::fromLatin1(
            RuntimePackageCompilerFixture::sha256("different-signing-key").value().toHex()));
    QVERIFY(invalidActivationProof.compileResult.isSuccess());
    QVERIFY(invalidActivationProof.finalizeRequest.isValid());
    QVERIFY(invalidActivationProof.finalizeResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = proofWithCoordinatedSignRequestMutation(
        QStringLiteral("policy_revision"), 2);
    QVERIFY(invalidActivationProof.compileResult.isSuccess());
    QVERIFY(invalidActivationProof.finalizeRequest.isValid());
    QVERIFY(invalidActivationProof.finalizeResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeRequest.contractIdentity.contractId.append(
        QStringLiteral(".different"));
    QVERIFY(invalidActivationProof.finalizeRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeRequest.compileRequestSha256
        = RuntimePackageCompilerFixture::sha256("different-compile-request");
    QVERIFY(invalidActivationProof.finalizeRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeRequestSha256 = {};
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeRequestSha256
        = RuntimePackageCompilerFixture::sha256("different-finalize-request");
    QVERIFY(invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeResult.envelope.requestSha256
        = RuntimePackageCompilerFixture::sha256("different-finalize-request");
    QVERIFY(invalidActivationProof.finalizeResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeResult.envelope.status
        = Data::RuntimePackageCompilerResultStatus::DomainFailed;
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeResult.manifestSha256
        = RuntimePackageCompilerFixture::sha256("different-manifest");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyRequest.operationId
        = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000098")};
    QVERIFY(invalidActivationProof.verifyRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyRequest.contractIdentity.contractId.append(
        QStringLiteral(".different"));
    QVERIFY(invalidActivationProof.verifyRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyRequest.packageBytes.append("-different");
    invalidActivationProof.verifyRequest.packageSha256
        = RuntimePackageCompilerFixture::sha256(
            invalidActivationProof.verifyRequest.packageBytes);
    QVERIFY(invalidActivationProof.verifyRequest.isValid());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyRequestSha256 = {};
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyRequestSha256
        = RuntimePackageCompilerFixture::sha256("different-verify-request");
    QVERIFY(invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.envelope.requestSha256
        = RuntimePackageCompilerFixture::sha256("different-verify-request");
    QVERIFY(invalidActivationProof.verifyResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.envelope.operationId
        = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000098")};
    QVERIFY(invalidActivationProof.verifyResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.trusted = false;
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.intentSha256
        = RuntimePackageCompilerFixture::sha256("different-intent");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.effectiveProjectCompanionSha256
        = RuntimePackageCompilerFixture::sha256("different-companion");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.targetProfileSha256
        = RuntimePackageCompilerFixture::sha256("different-target");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.adapterBundleSha256
        = RuntimePackageCompilerFixture::sha256("different-bundle");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult.topologyEvidenceSha256
        = RuntimePackageCompilerFixture::sha256("different-topology");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compiledProjectSource.append("-changed");
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.effectiveProjectCompanion.append("-changed");
    QVERIFY(!invalidActivationProof.isValid());

    Data::RuntimePackageCompilerCompileRequest alternateRequest = fixture.request;
    alternateRequest.intentId.append(QStringLiteral(".alternate"));
    QVERIFY(alternateRequest.isValid());
    const Data::RuntimePackageCompilerActivationProof alternateProof
        = successfulActivationProof(alternateRequest);
    QVERIFY(alternateProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compileResult = alternateProof.compileResult;
    QVERIFY(invalidActivationProof.compileResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.finalizeResult = alternateProof.finalizeResult;
    QVERIFY(invalidActivationProof.finalizeResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.verifyResult = alternateProof.verifyResult;
    QVERIFY(invalidActivationProof.verifyResult.isSuccess());
    QVERIFY(!invalidActivationProof.isValid());

    invalidActivationProof = activationProof;
    invalidActivationProof.compileResult = alternateProof.compileResult;
    invalidActivationProof.finalizeResult = alternateProof.finalizeResult;
    invalidActivationProof.verifyResult = alternateProof.verifyResult;
    QVERIFY(!invalidActivationProof.isValid());

    Data::RuntimePackageCompilerActivationProof independentVerifyProof = activationProof;
    independentVerifyProof.verifyRequest.operationId
        = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000097")};
    independentVerifyProof.verifyRequestSha256
        = compilerRequestSha256(independentVerifyProof.verifyRequest);
    independentVerifyProof.verifyResult = successfulVerifyResult(
        independentVerifyProof.verifyRequest,
        independentVerifyProof.compileRequest,
        independentVerifyProof.compileResult);
    QVERIFY(independentVerifyProof.isValid());
    QVERIFY(
        independentVerifyProof.verifyRequest.operationId
        != independentVerifyProof.compileRequest.operationId);

    QVERIFY(QMetaType::fromType<Data::RuntimePackageCompilerCompileRequest>().isValid());
    QVERIFY(
        QMetaType::fromType<Data::RuntimePackageCompilerProjectSnapshotEvidence>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageCompilerFinalizeRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageCompilerQueryRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageCompilerVerifyRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageCompilerActivationProof>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimePackageCompilerJobResult>().isValid());
    QVERIFY(QMetaType::fromType<RuntimePackageCompilerJobState>().isValid());
    QVERIFY(QMetaType::fromType<RuntimePackageCompilerJobCompletionError>().isValid());
}

void EtherCATCoreTests::testRuntimePackageCompilerCodec()
{
    const Data::RuntimePackageCompilerCompileRequest request = api042GoldenCompileRequest();
    QVERIFY(request.isValid());

    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> encoded
        = encodeRuntimePackageCompilerCompileRequest(request);
    QVERIFY_RESULT(encoded);
    const QByteArray goldenRequest = api042TestData(QStringLiteral("compile-request.json"));
    QCOMPARE(encoded->exactBytes(), goldenRequest);
    QCOMPARE(
        encoded->sha256().value().toHex(),
        QByteArray("fcc2821e811c2ec38faa7fe85f3c613db8ded31291a7e953ed3535c431f72887"));
    QVERIFY(encoded->exactBytes().contains("1785542400000000000"));
    QVERIFY(encoded->exactBytes().contains("1785542401000000000"));
    QVERIFY(!encoded->exactBytes().contains("1.7855424e+18"));
    QVERIFY(encoded->exactBytes().contains("\"slave_node_id\":\"ide:slave:xb6\""));
    QVERIFY(
        encoded->exactBytes().contains("\"project_device_id\":\"embedlabs:project:device:xb6\""));

    const auto rejectsCompileRequest = [](Data::RuntimePackageCompilerCompileRequest changed) {
        QVERIFY(!changed.isValid());
        QVERIFY(!encodeRuntimePackageCompilerCompileRequest(changed));
    };
    Data::RuntimePackageCompilerCompileRequest missing = request;
    missing.projectProjection.devices[0].projectDeviceId.clear();
    rejectsCompileRequest(missing);
    missing = request;
    missing.projectProjection.devices[0].pdoMappings.clear();
    rejectsCompileRequest(missing);
    missing = request;
    missing.projectProjection.devices[0].semanticBindingIds.clear();
    rejectsCompileRequest(missing);
    missing = request;
    missing.projectProjection.devices[1].startupSdos[0].timeoutNs = 0;
    rejectsCompileRequest(missing);
    missing = request;
    missing.projectProjection.devices[1].manualEnvelope.maximumTtlCycles = 0;
    rejectsCompileRequest(missing);

    const Data::RuntimePackageCompilerCanonicalJson signRequest
        = RuntimePackageCompilerFixture::canonical(
            api042TestData(QStringLiteral("sign-request.json")));
    QVERIFY(signRequest.isValid());
    const RuntimePackageCompilerProcessOutput compileOutput{
        true,
        0,
        api042TestData(QStringLiteral("compile-result.json")),
        {},
    };
    const Utils::Result<Data::RuntimePackageCompilerCompileResult> compileResult
        = decodeRuntimePackageCompilerCompileResult(request, compileOutput, signRequest);
    QVERIFY_RESULT(compileResult);
    QVERIFY(compileResult->isSuccess());
    QCOMPARE(compileResult->envelope.requestSha256, encoded->sha256());
    QCOMPARE(compileResult->signRequest->exactBytes(), signRequest.exactBytes());

    const QString staleSha256 = QString::fromLatin1(
        RuntimePackageCompilerFixture::sha256("stale-evidence").value().toHex());
    const auto changedSignRequest = [&](const QString &key, const QJsonValue &value) {
        QJsonObject object = QJsonDocument::fromJson(signRequest.exactBytes()).object();
        if (value.isUndefined())
            object.remove(key);
        else
            object.insert(key, value);
        return RuntimePackageCompilerFixture::canonical(
            QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    };
    const auto outputForSignRequest = [&](const Data::RuntimePackageCompilerCanonicalJson &changed) {
        RuntimePackageCompilerProcessOutput output = compileOutput;
        const QByteArray originalSha256 = signRequest.sha256().value().toHex();
        output.standardOutput.replace(originalSha256, changed.sha256().value().toHex());
        return output;
    };
    const auto rejectsSignRequest = [&](const Data::RuntimePackageCompilerCanonicalJson &changed) {
        QVERIFY(changed.isValid());
        QVERIFY(!decodeRuntimePackageCompilerCompileResult(
            request, outputForSignRequest(changed), changed));
    };

    const auto emptySignRequest = RuntimePackageCompilerFixture::canonical(QByteArray("{}\n"));
    rejectsSignRequest(emptySignRequest);
    auto malformedTypedCompileResult = *compileResult;
    const RuntimePackageCompilerProcessOutput emptySignRequestOutput = outputForSignRequest(
        emptySignRequest);
    malformedTypedCompileResult.signRequest = emptySignRequest;
    malformedTypedCompileResult.envelope.canonicalResult = RuntimePackageCompilerFixture::canonical(
        emptySignRequestOutput.standardOutput);
    QVERIFY(!malformedTypedCompileResult.isValid());

    rejectsSignRequest(changedSignRequest(
        QStringLiteral("effective_project_companion_sha256"), QJsonValue(QJsonValue::Undefined)));
    rejectsSignRequest(changedSignRequest(QStringLiteral("unexpected"), true));
    rejectsSignRequest(changedSignRequest(
        QStringLiteral("operation_id"), QStringLiteral("04204204-2001-4000-8000-000000000002")));
    for (const QString &key :
         {QStringLiteral("manifest_sha256"),
          QStringLiteral("intent_sha256"),
          QStringLiteral("effective_project_companion_sha256"),
          QStringLiteral("signing_key_id")}) {
        rejectsSignRequest(changedSignRequest(key, staleSha256));
    }
    rejectsSignRequest(changedSignRequest(QStringLiteral("policy_revision"), 2));
    QByteArray largePolicySignRequest = signRequest.exactBytes();
    largePolicySignRequest.replace(
        QByteArray("\"policy_revision\":1"), QByteArray("\"policy_revision\":9223372036854775808"));
    rejectsSignRequest(RuntimePackageCompilerFixture::canonical(largePolicySignRequest));

    const auto staleTargetSignRequest
        = changedSignRequest(QStringLiteral("target_profile_sha256"), staleSha256);
    RuntimePackageCompilerProcessOutput staleTargetOutput = outputForSignRequest(
        staleTargetSignRequest);
    staleTargetOutput.standardOutput
        .replace(compileResult->targetProfileSha256->value().toHex(), staleSha256.toLatin1());
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(
        request, staleTargetOutput, staleTargetSignRequest));
    RuntimePackageCompilerProcessOutput staleAdapterOutput = compileOutput;
    staleAdapterOutput.standardOutput
        .replace(compileResult->adapterBundleSha256->value().toHex(), staleSha256.toLatin1());
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, staleAdapterOutput, signRequest));

    const QByteArray packageBytes("api042-codec-package");
    const Data::RuntimePackageCompilerSha256 packageSha256 = RuntimePackageCompilerFixture::sha256(
        packageBytes);
    const Data::RuntimePackageCompilerSha256 receiptSha256 = api042Sha(
        QStringLiteral("1583f194e24542768a1f999824aff6bd563b54c84c264ff048c3b9751db20f0e"));
    const Data::RuntimePackageCompilerFinalizeRequest finalizeRequest{
        request.operationId,
        request.configurationId,
        request.contractIdentity,
        encoded->sha256(),
        compileResult->signRequest->sha256(),
        *compileResult->manifestSha256,
        request.targetProfile.signingKeyIdSha256,
        request.targetProfile.policyRevision,
        RuntimePackageCompilerFixture::canonical(
            api042TestData(QStringLiteral("sign-response.json"))),
    };
    QVERIFY(finalizeRequest.isValid());
    const QByteArray finalizeJson
        = QStringLiteral(
              "{\"configuration_id\":4201,"
              "\"format\":\"ethercat-ide-project-compiler-result-v1\","
              "\"format_version\":1,\"manifest_sha256\":\"%1\","
              "\"operation_id\":\"%2\",\"package_bytes\":%3,"
              "\"package_path\":\"api042-codec-package.ecpkg\","
              "\"package_sha256\":\"%4\",\"signing_receipt_sha256\":\"%5\","
              "\"status\":\"complete\"}\n")
              .arg(
                  QString::fromLatin1(compileResult->manifestSha256->value().toHex()),
                  request.operationId.value(),
                  QString::number(packageBytes.size()),
                  QString::fromLatin1(packageSha256.value().toHex()),
                  QString::fromLatin1(receiptSha256.value().toHex()))
              .toLatin1();
    const Utils::Result<Data::RuntimePackageCompilerFinalizeResult> finalizeResult
        = decodeRuntimePackageCompilerFinalizeResult(
            finalizeRequest, {true, 0, finalizeJson, {}}, packageBytes);
    QVERIFY_RESULT(finalizeResult);
    QVERIFY(finalizeResult->isSuccess());
    QCOMPARE(finalizeResult->packageBytes, packageBytes);

    const auto changedSigningResponse = [&](const QString &key, const QJsonValue &value) {
        QJsonObject object
            = QJsonDocument::fromJson(finalizeRequest.detachedSigningResponse.exactBytes()).object();
        if (value.isUndefined())
            object.remove(key);
        else
            object.insert(key, value);
        return RuntimePackageCompilerFixture::canonical(
            QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    };
    const auto rejectsFinalizeResponse =
        [&](const Data::RuntimePackageCompilerCanonicalJson &changed) {
            QVERIFY(changed.isValid());
            Data::RuntimePackageCompilerFinalizeRequest changedRequest = finalizeRequest;
            changedRequest.detachedSigningResponse = changed;
            QVERIFY(!changedRequest.isValid());
            QVERIFY(!decodeRuntimePackageCompilerFinalizeResult(
                changedRequest, {true, 0, finalizeJson, {}}, packageBytes));
        };
    rejectsFinalizeResponse(RuntimePackageCompilerFixture::canonical(QByteArray("{}\n")));
    rejectsFinalizeResponse(
        changedSigningResponse(QStringLiteral("request_sha256"), QJsonValue(QJsonValue::Undefined)));
    rejectsFinalizeResponse(changedSigningResponse(QStringLiteral("unexpected"), true));
    rejectsFinalizeResponse(changedSigningResponse(
        QStringLiteral("operation_id"), QStringLiteral("04204204-2001-4000-8000-000000000002")));
    for (const QString &key :
         {QStringLiteral("request_sha256"),
          QStringLiteral("manifest_sha256"),
          QStringLiteral("signing_key_id")}) {
        rejectsFinalizeResponse(changedSigningResponse(key, staleSha256));
    }
    rejectsFinalizeResponse(changedSigningResponse(QStringLiteral("policy_revision"), 2));
    QString upperCaseSignature = QJsonDocument::fromJson(
                                     finalizeRequest.detachedSigningResponse.exactBytes())
                                     .object()
                                     .value(QStringLiteral("signature_hex"))
                                     .toString();
    upperCaseSignature[0] = upperCaseSignature.at(0).toUpper();
    rejectsFinalizeResponse(
        changedSigningResponse(QStringLiteral("signature_hex"), upperCaseSignature));
    rejectsFinalizeResponse(
        changedSigningResponse(QStringLiteral("receipt_sha256"), QString(64, QLatin1Char('0'))));

    Data::RuntimePackageCompilerFinalizeRequest changedReceiptRequest = finalizeRequest;
    changedReceiptRequest.detachedSigningResponse
        = changedSigningResponse(QStringLiteral("receipt_sha256"), staleSha256);
    QVERIFY(!changedReceiptRequest.isValid());
    QByteArray changedReceiptFinalizeJson = finalizeJson;
    changedReceiptFinalizeJson.replace(receiptSha256.value().toHex(), staleSha256.toLatin1());
    QVERIFY(!decodeRuntimePackageCompilerFinalizeResult(
        changedReceiptRequest, {true, 0, changedReceiptFinalizeJson, {}}, packageBytes));

    QByteArray staleFinalizeJson = finalizeJson;
    staleFinalizeJson.replace(compileResult->manifestSha256->value().toHex(), staleSha256.toLatin1());
    QVERIFY(!decodeRuntimePackageCompilerFinalizeResult(
        finalizeRequest, {true, 0, staleFinalizeJson, {}}, packageBytes));
    staleFinalizeJson = finalizeJson;
    staleFinalizeJson.replace(receiptSha256.value().toHex(), staleSha256.toLatin1());
    QVERIFY(!decodeRuntimePackageCompilerFinalizeResult(
        finalizeRequest, {true, 0, staleFinalizeJson, {}}, packageBytes));
    QVERIFY(!decodeRuntimePackageCompilerFinalizeResult(
        finalizeRequest, {true, 0, finalizeJson, {}}, packageBytes + '!'));

    const Data::RuntimePackageCompilerQueryRequest queryRequest{
        request.operationId,
        request.contractIdentity,
        encoded->sha256(),
    };
    QVERIFY(queryRequest.isValid());
    const QByteArray queryJson = QStringLiteral(
                                     "{\"compiler\":{\"configuration_id\":9223372036854775809,"
                                     "\"state\":\"finalized\"},"
                                     "\"format\":\"ethercat-ide-compiler-operation-state-v1\","
                                     "\"operation_id\":\"%1\",\"signer_response\":null}\n")
                                     .arg(request.operationId.value())
                                     .toLatin1();
    const Utils::Result<Data::RuntimePackageCompilerQueryResult> queryResult
        = decodeRuntimePackageCompilerQueryResult(queryRequest, {true, 0, queryJson, {}});
    QVERIFY_RESULT(queryResult);
    QVERIFY(queryResult->isSuccess());
    QVERIFY(queryResult->compilerRecord);
    QCOMPARE(
        queryResult->compilerRecord->exactBytes(),
        QByteArray(
            "{\"configuration_id\":9223372036854775809,"
            "\"state\":\"finalized\"}\n"));

    const Data::RuntimePackageCompilerVerifyRequest verifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000099")},
        request.contractIdentity,
        packageBytes,
        packageSha256,
    };
    QVERIFY(verifyRequest.isValid());
    const QByteArray verifyJson
        = QStringLiteral(
              "{\"adapter_bundle_sha256\":\"%1\",\"configuration_id\":4201,"
              "\"effective_project_companion_sha256\":\"%2\","
              "\"format\":\"ethercat-ide-project-compiler-verification-v1\","
              "\"intent_sha256\":\"%3\",\"manifest_format_version\":2,"
              "\"package_sha256\":\"%4\",\"status\":\"pass\","
              "\"target_profile_sha256\":\"%5\","
              "\"topology_evidence_sha256\":\"%6\"}\n")
              .arg(
                  QString::fromLatin1(compileResult->adapterBundleSha256->value().toHex()),
                  QString::fromLatin1(
                      compileResult->effectiveProjectCompanionSha256->value().toHex()),
                  QString::fromLatin1(compileResult->intentSha256->value().toHex()),
                  QString::fromLatin1(packageSha256.value().toHex()),
                  QString::fromLatin1(compileResult->targetProfileSha256->value().toHex()),
                  QString::fromLatin1(
                      request.sourceArtifacts.topologyEvidence.sha256.value().toHex()))
              .toLatin1();
    const Utils::Result<Data::RuntimePackageCompilerVerifyResult> verifyResult
        = decodeRuntimePackageCompilerVerifyResult(verifyRequest, {true, 0, verifyJson, {}});
    QVERIFY_RESULT(verifyResult);
    QVERIFY(verifyResult->isSuccess());
    QCOMPARE(verifyResult->packageSha256, packageSha256);

    const Data::RuntimePackageCompilerCompileResult proofCompileSeed
        = successfulCompileResult(request);
    const Utils::Result<Data::RuntimePackageCompilerCompileResult> proofCompileResult
        = decodeRuntimePackageCompilerCompileResult(
            request,
            {true, 0, proofCompileSeed.envelope.canonicalResult.exactBytes(), {}},
            *proofCompileSeed.signRequest);
    QVERIFY_RESULT(proofCompileResult);
    QCOMPARE(proofCompileResult->envelope.requestSha256, encoded->sha256());

    const Data::RuntimePackageCompilerFinalizeRequest proofFinalizeRequest{
        request.operationId,
        request.configurationId,
        request.contractIdentity,
        proofCompileResult->envelope.requestSha256,
        proofCompileResult->signRequest->sha256(),
        *proofCompileResult->manifestSha256,
        request.targetProfile.signingKeyIdSha256,
        request.targetProfile.policyRevision,
        detachedSigningResponse(request, *proofCompileResult),
    };
    const Data::RuntimePackageCompilerFinalizeResult proofFinalizeSeed
        = successfulFinalizeResult(proofFinalizeRequest);
    const Utils::Result<Data::RuntimePackageCompilerFinalizeResult> proofFinalizeResult
        = decodeRuntimePackageCompilerFinalizeResult(
            proofFinalizeRequest,
            {true, 0, proofFinalizeSeed.envelope.canonicalResult.exactBytes(), {}},
            proofFinalizeSeed.packageBytes);
    QVERIFY_RESULT(proofFinalizeResult);
    QCOMPARE(
        proofFinalizeResult->envelope.requestSha256,
        proofFinalizeRequest.compileRequestSha256);

    const Data::RuntimePackageCompilerVerifyRequest proofVerifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000096")},
        request.contractIdentity,
        proofFinalizeResult->packageBytes,
        *proofFinalizeResult->packageSha256,
    };
    const Data::RuntimePackageCompilerVerifyResult proofVerifySeed
        = successfulVerifyResult(proofVerifyRequest, request, *proofCompileResult);
    const Utils::Result<Data::RuntimePackageCompilerVerifyResult> proofVerifyResult
        = decodeRuntimePackageCompilerVerifyResult(
            proofVerifyRequest,
            {true, 0, proofVerifySeed.envelope.canonicalResult.exactBytes(), {}});
    QVERIFY_RESULT(proofVerifyResult);
    QCOMPARE(proofVerifyResult->envelope.requestSha256, proofVerifyRequest.packageSha256);

    const Data::RuntimePackageCompilerActivationProof codecActivationProof{
        QStringLiteral("org.embedlabs.runtime-package-compiler.api042"),
        request.contractIdentity,
        request,
        *proofCompileResult,
        proofFinalizeRequest,
        RuntimePackageCompilerFixture::sha256("finalize-store-evidence"),
        *proofFinalizeResult,
        proofVerifyRequest,
        RuntimePackageCompilerFixture::sha256("verify-store-evidence"),
        *proofVerifyResult,
        QByteArray("compiled-project"),
        QByteArray("effective-project-companion"),
    };
    QVERIFY(codecActivationProof.isValid());

    const QByteArray failureJson(
        "{\"diagnostics\":[{\"code\":\"ECOMP-REQUEST-SCHEMA\","
        "\"format\":\"ethercat-ide-compiler-diagnostic-v1\","
        "\"message\":\"Request failed schema validation.\",\"path\":\"$\","
        "\"retryable\":false,\"severity\":\"error\",\"stage\":\"input\"}],"
        "\"status\":\"fail\"}\n");
    const RuntimePackageCompilerProcessOutput failureOutput{true, 1, {}, failureJson};
    const auto compileFailure
        = decodeRuntimePackageCompilerCompileResult(request, failureOutput, {});
    QVERIFY_RESULT(compileFailure);
    QCOMPARE(compileFailure->envelope.status, Data::RuntimePackageCompilerResultStatus::DomainFailed);
    QVERIFY_RESULT(decodeRuntimePackageCompilerFinalizeResult(finalizeRequest, failureOutput, {}));
    QVERIFY_RESULT(decodeRuntimePackageCompilerQueryResult(queryRequest, failureOutput));
    QVERIFY_RESULT(decodeRuntimePackageCompilerVerifyResult(verifyRequest, failureOutput));

    RuntimePackageCompilerProcessOutput malformed = compileOutput;
    malformed.standardOutput.append("{}\n");
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.standardError = "unexpected diagnostic\n";
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.standardOutput.replace("\":", "\": ");
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.standardOutput.chop(2);
    malformed.standardOutput.append(",\"unexpected\":true}\n");
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.standardOutput.replace("\"status\":\"awaiting_signature\"", "\"status\":\"future\"");
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    const qsizetype manifestStart = malformed.standardOutput.indexOf(",\"manifest_sha256\":");
    const qsizetype operationStart
        = malformed.standardOutput.indexOf(",\"operation_id\":", manifestStart);
    QVERIFY(manifestStart > 0);
    QVERIFY(operationStart > manifestStart);
    malformed.standardOutput.remove(manifestStart, operationStart - manifestStart);
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.standardOutput.chop(2);
    malformed.standardOutput.append(
        ",\"target_profile_sha256\":"
        "\"874fa218d7383c5e7aa086f251f65e7ee313eae163ede57a05f6994929ed4ab6\"}\n");
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.exitedNormally = false;
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
    malformed = compileOutput;
    malformed.exitCode = 2;
    QVERIFY(!decodeRuntimePackageCompilerCompileResult(request, malformed, signRequest));
}

void EtherCATCoreTests::testRuntimePackageCompilerProviderContract()
{
    RuntimePackageCompilerFixture fixture;
    TestRuntimePackageCompilerProvider provider;
    QCOMPARE(int(ProviderKind::RuntimePackageCompiler), 7);
    QCOMPARE(provider.kind(), ProviderKind::RuntimePackageCompiler);
    QVERIFY(!provider.isAvailable());
    QVERIFY(!provider.assembleActivationProof({}));
    provider.setAvailable(true);

    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);
    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    ExtensionSystem::PluginManager::addObject(&provider);
    auto removeProvider = qScopeGuard(
        [&] { ExtensionSystem::PluginManager::removeObject(&provider); });
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(
        registry->providers(ProviderKind::RuntimePackageCompiler), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    const auto awaitResult = [](RuntimePackageCompilerJob *job) {
        QSignalSpy finishedSpy(job, &RuntimePackageCompilerJob::finished);
        if (job->state() != RuntimePackageCompilerJobState::Finished)
            finishedSpy.wait(2000);
        return job->result().value_or(Data::RuntimePackageCompilerJobResult{});
    };
    const auto makeRequest = [&](quint64 sequence, quint64 configurationId) {
        Data::RuntimePackageCompilerCompileRequest request = fixture.request;
        request.operationId = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-%1").arg(sequence, 12, 10, QLatin1Char('0'))};
        request.configurationId = configurationId;
        request.intentId = QStringLiteral("embedlabs:compile-intent:test-%1").arg(sequence);
        return request;
    };

    const Data::RuntimePackageCompilerCompileRequest firstRequest = makeRequest(1, 4201);
    const Utils::Result<RuntimePackageCompilerJob *> firstScheduled = provider.compile(firstRequest);
    QVERIFY_RESULT(firstScheduled);
    auto *firstJob = static_cast<TestRuntimePackageCompilerJob *>(*firstScheduled);
    QVERIFY(firstJob);
    QCOMPARE(firstJob->state(), RuntimePackageCompilerJobState::Pending);
    QSignalSpy firstStateSpy(firstJob, &RuntimePackageCompilerJob::stateChanged);
    QSignalSpy firstFinishedSpy(firstJob, &RuntimePackageCompilerJob::finished);
    QVERIFY(firstFinishedSpy.wait(2000));
    QCOMPARE(firstJob->state(), RuntimePackageCompilerJobState::Finished);
    QCOMPARE(firstFinishedSpy.count(), 1);
    QCOMPARE(firstStateSpy.count(), 2);
    QVERIFY(firstJob->result());
    QVERIFY(firstJob->result()->isSuccess());
    firstJob->finishAgainForTest();
    QCOMPARE(firstFinishedSpy.count(), 1);
    const int firstMutationCount = provider.ledgerMutationCount;

    const auto canceledFirstResult = canceledCompilerJobResult(
        Data::RuntimePackageCompilerCommand::Compile,
        firstRequest.operationId,
        firstRequest.configurationId,
        compilerRequestSha256(firstRequest));
    auto *invalidTerminalJob = new TestRuntimePackageCompilerJob(
        *firstJob->result(), canceledFirstResult, &provider);
    QSignalSpy invalidTerminalFinishedSpy(
        invalidTerminalJob, &RuntimePackageCompilerJob::finished);
    QSignalSpy invalidTerminalFailedSpy(
        invalidTerminalJob, &RuntimePackageCompilerJob::completionFailed);
    invalidTerminalJob->finishForTest({});
    QCOMPARE(invalidTerminalJob->state(), RuntimePackageCompilerJobState::Finished);
    QVERIFY(!invalidTerminalJob->result());
    QCOMPARE(
        invalidTerminalJob->completionError(),
        RuntimePackageCompilerJobCompletionError::InvalidTerminalResult);
    QCOMPARE(invalidTerminalFinishedSpy.count(), 0);
    QCOMPARE(invalidTerminalFailedSpy.count(), 1);
    invalidTerminalJob->finishForTest(*firstJob->result());
    QCOMPARE(invalidTerminalFailedSpy.count(), 1);

    const auto &firstCompileForLifecycle
        = std::get<Data::RuntimePackageCompilerCompileResult>(
            firstJob->result()->value());
    const Data::RuntimePackageCompilerFinalizeRequest lifecycleFinalizeRequest{
        firstRequest.operationId,
        firstRequest.configurationId,
        firstRequest.contractIdentity,
        firstCompileForLifecycle.envelope.requestSha256,
        firstCompileForLifecycle.signRequest->sha256(),
        *firstCompileForLifecycle.manifestSha256,
        firstRequest.targetProfile.signingKeyIdSha256,
        firstRequest.targetProfile.policyRevision,
        detachedSigningResponse(firstRequest, firstCompileForLifecycle),
    };
    auto *wrongCommandJob = new TestRuntimePackageCompilerJob(
        *firstJob->result(), canceledFirstResult, &provider);
    QSignalSpy wrongCommandFailedSpy(
        wrongCommandJob, &RuntimePackageCompilerJob::completionFailed);
    wrongCommandJob->finishForTest(Data::RuntimePackageCompilerJobResult{
        successfulFinalizeResult(lifecycleFinalizeRequest)});
    QCOMPARE(wrongCommandJob->state(), RuntimePackageCompilerJobState::Finished);
    QVERIFY(!wrongCommandJob->result());
    QCOMPARE(
        wrongCommandJob->completionError(),
        RuntimePackageCompilerJobCompletionError::CommandMismatch);
    QCOMPARE(wrongCommandFailedSpy.count(), 1);

    auto *deletionJob = new TestRuntimePackageCompilerJob(
        *firstJob->result(), canceledFirstResult, nullptr);
    QPointer<TestRuntimePackageCompilerJob> deletionGuard(deletionJob);
    connect(
        deletionJob,
        &RuntimePackageCompilerJob::stateChanged,
        deletionJob,
        [deletionJob](RuntimePackageCompilerJobState state) {
            if (state == RuntimePackageCompilerJobState::Finished)
                delete deletionJob;
        });
    deletionJob->finishForTest(*firstJob->result());
    QVERIFY(!deletionGuard);

    const Utils::Result<RuntimePackageCompilerJob *> exactReplay = provider.compile(firstRequest);
    QVERIFY_RESULT(exactReplay);
    QVERIFY(awaitResult(*exactReplay).isSuccess());
    QCOMPARE(provider.ledgerMutationCount, firstMutationCount);

    Data::RuntimePackageCompilerCompileRequest operationConflict = firstRequest;
    operationConflict.intentId = QStringLiteral("embedlabs:compile-intent:changed");
    const Utils::Result<RuntimePackageCompilerJob *> conflictScheduled = provider.compile(
        operationConflict);
    QVERIFY_RESULT(conflictScheduled);
    const auto conflictResult = awaitResult(*conflictScheduled);
    QVERIFY(conflictResult.isValid());
    QVERIFY(!conflictResult.isSuccess());
    const auto &conflictCompile = std::get<Data::RuntimePackageCompilerCompileResult>(
        conflictResult.value());
    QCOMPARE(
        conflictCompile.envelope.diagnostics.constFirst().category,
        Data::RuntimePackageCompilerDiagnosticCategory::Idempotency);

    const Data::RuntimePackageCompilerCompileRequest configurationReplay
        = makeRequest(2, firstRequest.configurationId);
    const Utils::Result<RuntimePackageCompilerJob *> configurationReplayScheduled
        = provider.compile(configurationReplay);
    QVERIFY_RESULT(configurationReplayScheduled);
    QVERIFY(!awaitResult(*configurationReplayScheduled).isSuccess());
    QCOMPARE(provider.ledgerMutationCount, firstMutationCount);

    const auto expectCompileFailure = [&](Data::RuntimePackageCompilerCompileRequest request) {
        const Utils::Result<RuntimePackageCompilerJob *> scheduled = provider.compile(request);
        QVERIFY_RESULT(scheduled);
        const Data::RuntimePackageCompilerJobResult result = awaitResult(*scheduled);
        QVERIFY(result.isValid());
        QVERIFY(!result.isSuccess());
    };

    Data::RuntimePackageCompilerCompileRequest missingCas = makeRequest(10, 4210);
    missingCas.projectSnapshotEvidence = {};
    expectCompileFailure(missingCas);
    const Utils::Result<RuntimePackageCompilerJob *> reusedUnreservedConfiguration
        = provider.compile(makeRequest(110, 4210));
    QVERIFY_RESULT(reusedUnreservedConfiguration);
    QVERIFY(awaitResult(*reusedUnreservedConfiguration).isSuccess());

    Data::RuntimePackageCompilerCompileRequest zeroConfiguration = makeRequest(111, 4216);
    zeroConfiguration.configurationId = 0;
    const Utils::Result<RuntimePackageCompilerJob *> zeroConfigurationScheduled
        = provider.compile(zeroConfiguration);
    QVERIFY_RESULT(zeroConfigurationScheduled);
    const auto zeroConfigurationResult = awaitResult(*zeroConfigurationScheduled);
    QVERIFY(zeroConfigurationResult.isValid());
    const auto &zeroConfigurationFailure
        = std::get<Data::RuntimePackageCompilerCompileResult>(
            zeroConfigurationResult.value());
    QCOMPARE(zeroConfigurationFailure.envelope.configurationId, quint64(0));
    QCOMPARE(
        zeroConfigurationFailure.envelope.diagnostics.constFirst().code,
        QStringLiteral("ECOMP-REQUEST-SCHEMA"));

    Data::RuntimePackageCompilerCompileRequest staleTopology = makeRequest(11, 4211);
    staleTopology.compileTimeNs = staleTopology.topologyEvidence.expiresAtNs + 1;
    expectCompileFailure(staleTopology);
    const Utils::Result<RuntimePackageCompilerJob *> staleRetry = provider.compile(
        staleTopology);
    QVERIFY_RESULT(staleRetry);
    QVERIFY(!awaitResult(*staleRetry).isSuccess());
    const Utils::Result<RuntimePackageCompilerJob *> reservedConfigurationConflict
        = provider.compile(makeRequest(112, staleTopology.configurationId));
    QVERIFY_RESULT(reservedConfigurationConflict);
    const auto reservedConfigurationConflictResult = awaitResult(
        *reservedConfigurationConflict);
    const auto &reservedConfigurationFailure
        = std::get<Data::RuntimePackageCompilerCompileResult>(
            reservedConfigurationConflictResult.value());
    QCOMPARE(
        reservedConfigurationFailure.envelope.diagnostics.constFirst().code,
        QStringLiteral("ECOMP-CONFIGURATION-REPLAY"));

    Data::RuntimePackageCompilerCompileRequest missingEsi = makeRequest(12, 4212);
    missingEsi.deviceSourceEvidence[0].originalEsi = {};
    expectCompileFailure(missingEsi);

    Data::RuntimePackageCompilerCompileRequest missingBundle = makeRequest(13, 4213);
    missingBundle.sourceArtifacts.adapterBundle = {};
    expectCompileFailure(missingBundle);

    Data::RuntimePackageCompilerCompileRequest missingDc = makeRequest(14, 4214);
    missingDc.deviceSourceEvidence[0].explicitNoDc = false;
    expectCompileFailure(missingDc);

    Data::RuntimePackageCompilerCompileRequest missingTarget = makeRequest(15, 4215);
    missingTarget.targetProfile.productionSigned = false;
    expectCompileFailure(missingTarget);

    const Data::RuntimePackageCompilerCompileRequest canceledRequest = makeRequest(20, 4220);
    const Utils::Result<RuntimePackageCompilerJob *> canceledScheduled = provider.compile(
        canceledRequest);
    QVERIFY_RESULT(canceledScheduled);
    auto *canceledJob = static_cast<TestRuntimePackageCompilerJob *>(*canceledScheduled);
    QVERIFY(canceledJob);
    canceledJob->cancel();
    canceledJob->cancel();
    QCOMPARE(canceledJob->cancellationRequestCount(), 1);
    const auto canceledResult = awaitResult(canceledJob);
    QVERIFY(canceledResult.isValid());
    QVERIFY(!canceledResult.isSuccess());
    QCOMPARE(canceledJob->state(), RuntimePackageCompilerJobState::Finished);
    canceledJob->cancel();
    QCOMPARE(canceledJob->cancellationRequestCount(), 1);
    QCOMPARE(
        std::get<Data::RuntimePackageCompilerCompileResult>(canceledResult.value()).envelope.status,
        Data::RuntimePackageCompilerResultStatus::Canceled);

    const int afterCanceledMutations = provider.ledgerMutationCount;
    const Data::RuntimePackageCompilerQueryRequest canceledQuery{
        canceledRequest.operationId,
        canceledRequest.contractIdentity,
        compilerRequestSha256(canceledRequest),
    };
    const Utils::Result<RuntimePackageCompilerJob *> canceledQueryScheduled = provider.query(
        canceledQuery);
    QVERIFY_RESULT(canceledQueryScheduled);
    const auto canceledQueryJobResult = awaitResult(*canceledQueryScheduled);
    QVERIFY(canceledQueryJobResult.isSuccess());
    const auto &canceledQueryResult = std::get<Data::RuntimePackageCompilerQueryResult>(
        canceledQueryJobResult.value());
    QVERIFY(canceledQueryResult.hasRecoveredResult());
    QVERIFY(canceledQueryResult.hasCompilerRecord());
    QVERIFY(!canceledQueryResult.hasSignerResponse());
    QCOMPARE(
        QJsonDocument::fromJson(canceledQueryResult.compilerRecord->exactBytes())
            .object()
            .value(QStringLiteral("state"))
            .toString(),
        QStringLiteral("reserved"));
    QCOMPARE(provider.ledgerMutationCount, afterCanceledMutations);

    const Utils::Result<RuntimePackageCompilerJob *> canceledReplay = provider.compile(
        canceledRequest);
    QVERIFY_RESULT(canceledReplay);
    const auto canceledReplayResult = awaitResult(*canceledReplay);
    QVERIFY(canceledReplayResult.isSuccess());
    QCOMPARE(provider.ledgerMutationCount, afterCanceledMutations + 1);

    const Data::RuntimePackageCompilerCompileRequest runningCanceledRequest = makeRequest(22, 4222);
    const Utils::Result<RuntimePackageCompilerJob *> runningCanceledScheduled = provider.compile(
        runningCanceledRequest);
    QVERIFY_RESULT(runningCanceledScheduled);
    auto *runningCanceledJob = static_cast<TestRuntimePackageCompilerJob *>(
        *runningCanceledScheduled);
    connect(
        runningCanceledJob,
        &RuntimePackageCompilerJob::stateChanged,
        runningCanceledJob,
        [runningCanceledJob](RuntimePackageCompilerJobState state) {
            if (state == RuntimePackageCompilerJobState::Running)
                runningCanceledJob->cancel();
        });
    const auto runningCanceledResult = awaitResult(runningCanceledJob);
    QCOMPARE(runningCanceledJob->cancellationRequestCount(), 1);
    QCOMPARE(
        std::get<Data::RuntimePackageCompilerCompileResult>(runningCanceledResult.value())
            .envelope.status,
        Data::RuntimePackageCompilerResultStatus::Canceled);
    const Utils::Result<RuntimePackageCompilerJob *> runningCanceledRetry = provider.compile(
        runningCanceledRequest);
    QVERIFY_RESULT(runningCanceledRetry);
    QVERIFY(awaitResult(*runningCanceledRetry).isSuccess());

    const Data::RuntimePackageCompilerCompileRequest completedRequest = makeRequest(21, 4221);
    const Utils::Result<RuntimePackageCompilerJob *> completedScheduled = provider.compile(
        completedRequest);
    QVERIFY_RESULT(completedScheduled);
    auto *completedJob = static_cast<TestRuntimePackageCompilerJob *>(*completedScheduled);
    QVERIFY(completedJob);
    QVERIFY(awaitResult(completedJob).isSuccess());
    completedJob->cancel();
    QCOMPARE(completedJob->cancellationRequestCount(), 0);

    const Data::RuntimePackageCompilerQueryRequest queryRequest{
        firstRequest.operationId,
        firstRequest.contractIdentity,
        compilerRequestSha256(firstRequest),
    };
    const int beforeQueryMutations = provider.ledgerMutationCount;
    const Utils::Result<RuntimePackageCompilerJob *> queryScheduled = provider.query(queryRequest);
    QVERIFY_RESULT(queryScheduled);
    const auto queryResult = awaitResult(*queryScheduled);
    QVERIFY(queryResult.isSuccess());
    const auto &query = std::get<Data::RuntimePackageCompilerQueryResult>(queryResult.value());
    QVERIFY(query.hasRecoveredResult());
    QVERIFY(query.hasCompilerRecord());
    QVERIFY(!query.hasSignerResponse());
    QCOMPARE(
        QJsonDocument::fromJson(query.compilerRecord->exactBytes())
            .object()
            .value(QStringLiteral("state"))
            .toString(),
        QStringLiteral("prepared"));
    QCOMPARE(provider.ledgerMutationCount, beforeQueryMutations);

    const Data::RuntimePackageCompilerQueryRequest missingQuery{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000098")},
        firstRequest.contractIdentity,
        compilerRequestSha256(firstRequest),
    };
    const Utils::Result<RuntimePackageCompilerJob *> missingQueryScheduled = provider.query(
        missingQuery);
    QVERIFY_RESULT(missingQueryScheduled);
    const auto missingQueryJobResult = awaitResult(*missingQueryScheduled);
    QVERIFY(missingQueryJobResult.isValid());
    QVERIFY(!missingQueryJobResult.isSuccess());
    const auto &missingQueryResult = std::get<Data::RuntimePackageCompilerQueryResult>(
        missingQueryJobResult.value());
    QVERIFY(!missingQueryResult.hasRecoveredResult());
    QCOMPARE(
        missingQueryResult.envelope.diagnostics.constFirst().code,
        QStringLiteral("ECOMP-OPERATION-UNKNOWN"));

    const auto firstCompileResult = std::get<Data::RuntimePackageCompilerCompileResult>(
        firstJob->result()->value());
    const Data::RuntimePackageCompilerFinalizeRequest finalizeRequest{
        firstRequest.operationId,
        firstRequest.configurationId,
        firstRequest.contractIdentity,
        firstCompileResult.envelope.requestSha256,
        firstCompileResult.signRequest->sha256(),
        *firstCompileResult.manifestSha256,
        firstRequest.targetProfile.signingKeyIdSha256,
        firstRequest.targetProfile.policyRevision,
        detachedSigningResponse(firstRequest, firstCompileResult),
    };
    Data::RuntimePackageCompilerFinalizeRequest zeroConfigurationFinalize = finalizeRequest;
    zeroConfigurationFinalize.configurationId = 0;
    const Utils::Result<RuntimePackageCompilerJob *> zeroConfigurationFinalizeScheduled
        = provider.finalize(zeroConfigurationFinalize);
    QVERIFY_RESULT(zeroConfigurationFinalizeScheduled);
    const auto zeroConfigurationFinalizeResult = awaitResult(
        *zeroConfigurationFinalizeScheduled);
    QVERIFY(zeroConfigurationFinalizeResult.isValid());
    const auto &zeroConfigurationFinalizeFailure
        = std::get<Data::RuntimePackageCompilerFinalizeResult>(
            zeroConfigurationFinalizeResult.value());
    QCOMPARE(zeroConfigurationFinalizeFailure.envelope.configurationId, quint64(0));

    const Utils::Result<RuntimePackageCompilerJob *> finalizeScheduled = provider.finalize(
        finalizeRequest);
    QVERIFY_RESULT(finalizeScheduled);
    auto *canceledFinalizeJob = static_cast<TestRuntimePackageCompilerJob *>(
        *finalizeScheduled);
    canceledFinalizeJob->cancel();
    const auto canceledFinalizeResult = awaitResult(canceledFinalizeJob);
    QCOMPARE(
        std::get<Data::RuntimePackageCompilerFinalizeResult>(
            canceledFinalizeResult.value())
            .envelope.status,
        Data::RuntimePackageCompilerResultStatus::Canceled);
    const Utils::Result<RuntimePackageCompilerJob *> queryAfterCanceledFinalize
        = provider.query(queryRequest);
    QVERIFY_RESULT(queryAfterCanceledFinalize);
    const auto queryAfterCanceledFinalizeJobResult = awaitResult(
        *queryAfterCanceledFinalize);
    const auto &queryAfterCanceledFinalizeResult
        = std::get<Data::RuntimePackageCompilerQueryResult>(
            queryAfterCanceledFinalizeJobResult.value());
    QVERIFY(queryAfterCanceledFinalizeResult.hasCompilerRecord());
    QVERIFY(queryAfterCanceledFinalizeResult.hasSignerResponse());
    QCOMPARE(
        QJsonDocument::fromJson(
            queryAfterCanceledFinalizeResult.compilerRecord->exactBytes())
            .object()
            .value(QStringLiteral("state"))
            .toString(),
        QStringLiteral("prepared"));

    const Utils::Result<RuntimePackageCompilerJob *> finalizeRetry = provider.finalize(
        finalizeRequest);
    QVERIFY_RESULT(finalizeRetry);
    const auto finalizeResult = awaitResult(*finalizeRetry);
    QVERIFY(finalizeResult.isSuccess());
    const int afterFinalizeMutations = provider.ledgerMutationCount;

    const Utils::Result<RuntimePackageCompilerJob *> queryAfterFinalize = provider.query(
        queryRequest);
    QVERIFY_RESULT(queryAfterFinalize);
    const auto queryAfterFinalizeJobResult = awaitResult(*queryAfterFinalize);
    const auto &queryAfterFinalizeResult = std::get<Data::RuntimePackageCompilerQueryResult>(
        queryAfterFinalizeJobResult.value());
    QVERIFY(queryAfterFinalizeResult.hasCompilerRecord());
    QVERIFY(queryAfterFinalizeResult.hasSignerResponse());
    QCOMPARE(
        QJsonDocument::fromJson(queryAfterFinalizeResult.compilerRecord->exactBytes())
            .object()
            .value(QStringLiteral("state"))
            .toString(),
        QStringLiteral("finalized"));

    const Utils::Result<RuntimePackageCompilerJob *> finalizeReplay = provider.finalize(
        finalizeRequest);
    QVERIFY_RESULT(finalizeReplay);
    QVERIFY(awaitResult(*finalizeReplay).isSuccess());
    QCOMPARE(provider.ledgerMutationCount, afterFinalizeMutations);

    Data::RuntimePackageCompilerFinalizeRequest finalizeConflict = finalizeRequest;
    QByteArray changedSigningResponse = finalizeConflict.detachedSigningResponse.exactBytes();
    const qsizetype signatureOffset = changedSigningResponse.indexOf("\"signature_hex\":\"") + 17;
    QVERIFY(signatureOffset >= 17);
    changedSigningResponse[signatureOffset] = '5';
    finalizeConflict.detachedSigningResponse = RuntimePackageCompilerFixture::canonical(
        std::move(changedSigningResponse));
    const Utils::Result<RuntimePackageCompilerJob *> finalizeConflictScheduled = provider.finalize(
        finalizeConflict);
    QVERIFY_RESULT(finalizeConflictScheduled);
    QVERIFY(!awaitResult(*finalizeConflictScheduled).isSuccess());
    QCOMPARE(provider.ledgerMutationCount, afterFinalizeMutations);

    const auto &finalized = std::get<Data::RuntimePackageCompilerFinalizeResult>(
        finalizeResult.value());
    const Data::RuntimePackageCompilerVerifyRequest missingDigestVerifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000097")},
        firstRequest.contractIdentity,
        {},
        {},
    };
    const Utils::Result<RuntimePackageCompilerJob *> missingDigestVerifyScheduled
        = provider.verify(missingDigestVerifyRequest);
    QVERIFY_RESULT(missingDigestVerifyScheduled);
    const auto missingDigestVerifyResult = awaitResult(*missingDigestVerifyScheduled);
    QVERIFY(missingDigestVerifyResult.isValid());
    QVERIFY(!missingDigestVerifyResult.isSuccess());
    QCOMPARE(
        (*missingDigestVerifyScheduled)->completionError(),
        RuntimePackageCompilerJobCompletionError::None);
    auto malformedFailedVerify
        = std::get<Data::RuntimePackageCompilerVerifyResult>(
            missingDigestVerifyResult.value());
    malformedFailedVerify.intentSha256
        = Data::RuntimePackageCompilerSha256{QByteArray(31, '\x55')};
    QVERIFY(!malformedFailedVerify.isValid());

    const Data::RuntimePackageCompilerVerifyRequest malformedDigestVerifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000096")},
        firstRequest.contractIdentity,
        finalized.packageBytes,
        Data::RuntimePackageCompilerSha256{QByteArray(31, '\x55')},
    };
    const Utils::Result<RuntimePackageCompilerJob *> malformedDigestVerifyScheduled
        = provider.verify(malformedDigestVerifyRequest);
    QVERIFY_RESULT(malformedDigestVerifyScheduled);
    const auto malformedDigestVerifyResult = awaitResult(*malformedDigestVerifyScheduled);
    QVERIFY(malformedDigestVerifyResult.isValid());
    QVERIFY(!malformedDigestVerifyResult.isSuccess());
    QCOMPARE(
        (*malformedDigestVerifyScheduled)->completionError(),
        RuntimePackageCompilerJobCompletionError::None);
    QVERIFY(
        std::get<Data::RuntimePackageCompilerVerifyResult>(
            malformedDigestVerifyResult.value())
            .packageSha256.value()
            .isEmpty());

    const Data::RuntimePackageCompilerVerifyRequest verifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000099")},
        firstRequest.contractIdentity,
        finalized.packageBytes,
        *finalized.packageSha256,
    };
    const int beforeVerifyMutations = provider.ledgerMutationCount;
    const Utils::Result<RuntimePackageCompilerJob *> verifyScheduled = provider.verify(
        verifyRequest);
    QVERIFY_RESULT(verifyScheduled);
    QVERIFY(awaitResult(*verifyScheduled).isSuccess());
    QCOMPARE(provider.ledgerMutationCount, beforeVerifyMutations);
    QCOMPARE(provider.verifyCallCount, 1);

    provider.failScheduling = true;
    const int beforeSchedulingFailure = provider.ledgerMutationCount;
    const Utils::Result<RuntimePackageCompilerJob *> schedulingFailure = provider.compile(
        makeRequest(30, 4230));
    QVERIFY(!schedulingFailure);
    QCOMPARE(provider.ledgerMutationCount, beforeSchedulingFailure);
    provider.failScheduling = false;

    Data::RuntimePackageCompilerCompileRequest malformedIdentity = makeRequest(31, 4231);
    malformedIdentity.operationId = {};
    const Utils::Result<RuntimePackageCompilerJob *> malformedScheduling = provider.compile(
        malformedIdentity);
    QVERIFY(!malformedScheduling);

    ExtensionSystem::PluginManager::removeObject(&provider);
    removeProvider.dismiss();
    QVERIFY(!registry->provider(provider.id()));
    QCOMPARE(removedSpy.count(), 1);
}

void EtherCATCoreTests::testRuntimePackageCompilerPreparationCoordinatorContract()
{
    RuntimePackageCompilerFixture fixture;
    TestRuntimePackageCompilerProvider provider;
    Data::RuntimePackageCompilerActivationProof proof
        = successfulActivationProof(fixture.request);
    proof.compilerProviderId = provider.id().toString();
    const auto encodedCompileRequest
        = encodeRuntimePackageCompilerCompileRequest(proof.compileRequest);
    QVERIFY_RESULT(encodedCompileRequest);
    proof.compileResult.envelope.requestSha256 = encodedCompileRequest->sha256();
    proof.finalizeRequest.compileRequestSha256 = encodedCompileRequest->sha256();
    proof.finalizeResult.envelope.requestSha256 = encodedCompileRequest->sha256();
    const auto encodedFinalizeRequest
        = encodeRuntimePackageCompilerFinalizeRequest(proof.finalizeRequest);
    QVERIFY_RESULT(encodedFinalizeRequest);
    proof.finalizeRequestSha256 = encodedFinalizeRequest->sha256();
    const auto encodedVerifyRequest = encodeRuntimePackageCompilerVerifyRequest(
        proof.verifyRequest);
    QVERIFY_RESULT(encodedVerifyRequest);
    proof.verifyRequestSha256 = encodedVerifyRequest->sha256();
    QVERIFY(proof.isValid());

    const RuntimePackageCompilerPreparationStartRequest startRequest{
        proof.compileRequest,
        proof.verifyRequest.operationId,
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/compiler-preparation-001")},
        true,
    };
    QVERIFY(startRequest.isValid());
    const auto startRequestFingerprint
        = runtimePackageCompilerPreparationStartRequestFingerprint(startRequest);
    QVERIFY_RESULT(startRequestFingerprint);

    RuntimePackageCompilerPreparationStartRequest reusedVerifyId = startRequest;
    reusedVerifyId.verifyOperationId = reusedVerifyId.compileRequest.operationId;
    QVERIFY(!reusedVerifyId.isValid());
    RuntimePackageCompilerPreparationStartRequest reusedActivationId = startRequest;
    reusedActivationId.activationOperationId = Data::RuntimePackageActivationOperationId{
        startRequest.verifyOperationId.value()};
    QVERIFY(!reusedActivationId.isValid());

    const auto makeRecord = [&](RuntimePackageCompilerPreparationPhase phase) {
        RuntimePackageCompilerPreparationRecord record;
        record.compileOperationId = startRequest.compileRequest.operationId;
        record.startRequestFingerprint = *startRequestFingerprint;
        record.verifyOperationId = startRequest.verifyOperationId;
        record.activationOperationId = startRequest.activationOperationId;
        record.startRequest = startRequest;
        record.compileRequestSha256 = encodedCompileRequest->sha256();
        record.compilerProviderId = provider.id().toString();
        record.contractIdentity = startRequest.compileRequest.contractIdentity;
        record.revision = 1;
        record.phase = phase;
        switch (phase) {
        case RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature:
            record.compileResult = proof.compileResult;
            record.detachedSigningRequest = *proof.compileResult.signRequest;
            break;
        case RuntimePackageCompilerPreparationPhase::Finalizing:
            record.compileResult = proof.compileResult;
            record.detachedSigningRequest = *proof.compileResult.signRequest;
            record.finalizeRequest = proof.finalizeRequest;
            record.finalizeRequestSha256 = proof.finalizeRequestSha256;
            break;
        case RuntimePackageCompilerPreparationPhase::Verifying:
            record.compileResult = proof.compileResult;
            record.detachedSigningRequest = *proof.compileResult.signRequest;
            record.finalizeRequest = proof.finalizeRequest;
            record.finalizeRequestSha256 = proof.finalizeRequestSha256;
            record.finalizeResult = proof.finalizeResult;
            record.verifyRequest = proof.verifyRequest;
            record.verifyRequestSha256 = proof.verifyRequestSha256;
            break;
        case RuntimePackageCompilerPreparationPhase::AssemblingProof:
            record.compileResult = proof.compileResult;
            record.detachedSigningRequest = *proof.compileResult.signRequest;
            record.finalizeRequest = proof.finalizeRequest;
            record.finalizeRequestSha256 = proof.finalizeRequestSha256;
            record.finalizeResult = proof.finalizeResult;
            record.verifyRequest = proof.verifyRequest;
            record.verifyRequestSha256 = proof.verifyRequestSha256;
            record.verifyResult = proof.verifyResult;
            break;
        case RuntimePackageCompilerPreparationPhase::Ready:
            record.compileResult = proof.compileResult;
            record.detachedSigningRequest = *proof.compileResult.signRequest;
            record.finalizeRequest = proof.finalizeRequest;
            record.finalizeRequestSha256 = proof.finalizeRequestSha256;
            record.finalizeResult = proof.finalizeResult;
            record.verifyRequest = proof.verifyRequest;
            record.verifyRequestSha256 = proof.verifyRequestSha256;
            record.verifyResult = proof.verifyResult;
            record.preparation = RuntimePackageActivationPreparationRequest{
                startRequest.activationOperationId,
                proof.compileRequest.topologyEvidence.scope,
                proof.finalizeResult.packageBytes,
                proof.compiledProjectSource,
                proof.effectiveProjectCompanion,
                proof.verifyResult,
                startRequest.rollbackOnActivationFailure,
                proof,
            };
            break;
        case RuntimePackageCompilerPreparationPhase::ReconciliationRequired:
            record.detail = QStringLiteral("Compiler terminal state requires reconciliation.");
            break;
        case RuntimePackageCompilerPreparationPhase::Canceled:
            record.detail = QStringLiteral("Compiler preparation was canceled.");
            break;
        case RuntimePackageCompilerPreparationPhase::Failed:
            record.detail = QStringLiteral("Compiler preparation failed.");
            break;
        case RuntimePackageCompilerPreparationPhase::Idle:
        case RuntimePackageCompilerPreparationPhase::Reserved:
        case RuntimePackageCompilerPreparationPhase::Compiling:
        case RuntimePackageCompilerPreparationPhase::CancelRequested:
            break;
        }
        return record;
    };

    for (RuntimePackageCompilerPreparationPhase phase : {
             RuntimePackageCompilerPreparationPhase::Reserved,
             RuntimePackageCompilerPreparationPhase::Compiling,
             RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature,
             RuntimePackageCompilerPreparationPhase::Finalizing,
             RuntimePackageCompilerPreparationPhase::Verifying,
             RuntimePackageCompilerPreparationPhase::AssemblingProof,
             RuntimePackageCompilerPreparationPhase::Ready,
             RuntimePackageCompilerPreparationPhase::CancelRequested,
             RuntimePackageCompilerPreparationPhase::ReconciliationRequired,
             RuntimePackageCompilerPreparationPhase::Canceled,
             RuntimePackageCompilerPreparationPhase::Failed,
         }) {
        QVERIFY(makeRecord(phase).isValid());
    }
    QVERIFY(!makeRecord(RuntimePackageCompilerPreparationPhase::Idle).isValid());

    RuntimePackageCompilerPreparationRecord malformed = makeRecord(
        RuntimePackageCompilerPreparationPhase::Reserved);
    malformed.compilerProviderId = QStringLiteral(" invalid provider");
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Reserved);
    malformed.contractIdentity.contractId.append(QStringLiteral(".different"));
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    malformed.detachedSigningRequest = RuntimePackageCompilerFixture::canonical(
        QByteArray("{\"different\":true}\n"));
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Ready);
    malformed.preparation->operationId = Data::RuntimePackageActivationOperationId{
        QStringLiteral("operation/compiler-preparation-different")};
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Ready);
    malformed.preparation->packageBytes.append("-different");
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Ready);
    malformed.preparation->compiledProjectSource.append("-different");
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Ready);
    malformed.preparation->effectiveProjectCompanion.append("-different");
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    malformed.compileResult->envelope.requestSha256
        = RuntimePackageCompilerFixture::sha256("different-compile-request");
    QVERIFY(malformed.compileResult->isValid());
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Finalizing);
    malformed.finalizeRequestSha256
        = RuntimePackageCompilerFixture::sha256("different-finalize-request");
    QVERIFY(malformed.finalizeRequest->isValid());
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Verifying);
    malformed.finalizeResult->envelope.requestSha256
        = RuntimePackageCompilerFixture::sha256("different-finalize-parent");
    QVERIFY(malformed.finalizeResult->isValid());
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::Verifying);
    malformed.verifyRequestSha256
        = RuntimePackageCompilerFixture::sha256("different-verify-request");
    QVERIFY(malformed.verifyRequest->isValid());
    QVERIFY(!malformed.isValid());
    malformed = makeRecord(RuntimePackageCompilerPreparationPhase::AssemblingProof);
    malformed.verifyResult->envelope.requestSha256
        = RuntimePackageCompilerFixture::sha256("different-verify-package");
    QVERIFY(malformed.verifyResult->isValid());
    QVERIFY(!malformed.isValid());

    QVERIFY(runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::Idle,
        RuntimePackageCompilerPreparationPhase::Reserved));
    QVERIFY(runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::Compiling,
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    QVERIFY(runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature,
        RuntimePackageCompilerPreparationPhase::Canceled));
    QVERIFY(runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::Reserved,
        RuntimePackageCompilerPreparationPhase::Canceled));
    QVERIFY(runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::Finalizing,
        RuntimePackageCompilerPreparationPhase::ReconciliationRequired));
    QVERIFY(runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::ReconciliationRequired,
        RuntimePackageCompilerPreparationPhase::Verifying));
    QVERIFY(!runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::Ready,
        RuntimePackageCompilerPreparationPhase::Reserved));
    QVERIFY(!runtimePackageCompilerPreparationTransitionIsAllowed(
        RuntimePackageCompilerPreparationPhase::Compiling,
        RuntimePackageCompilerPreparationPhase::Compiling));
    QVERIFY(runtimePackageCompilerPreparationPhaseAllowsCancel(
        RuntimePackageCompilerPreparationPhase::Compiling));
    QVERIFY(!runtimePackageCompilerPreparationPhaseAllowsCancel(
        RuntimePackageCompilerPreparationPhase::ReconciliationRequired));

    const RuntimePackageCompilerPreparationRecord ready = makeRecord(
        RuntimePackageCompilerPreparationPhase::Ready);
    const RuntimePackageCompilerPreparationTerminalSummary completeTerminalSummary{
        proof.compileResult.envelope.canonicalResult.sha256(),
        proof.compileResult.signRequest->sha256(),
        proof.finalizeResult.envelope.canonicalResult.sha256(),
        proof.finalizeResult.packageSha256,
        proof.verifyResult.envelope.canonicalResult.sha256(),
    };
    QVERIFY(completeTerminalSummary.isValid());
    RuntimePackageCompilerPreparationRecord readyRestartSummary = ready;
    readyRestartSummary.startRequest.reset();
    readyRestartSummary.compileRequestSha256.reset();
    readyRestartSummary.compileResult.reset();
    readyRestartSummary.detachedSigningRequest.reset();
    readyRestartSummary.finalizeRequest.reset();
    readyRestartSummary.finalizeRequestSha256.reset();
    readyRestartSummary.finalizeResult.reset();
    readyRestartSummary.verifyRequest.reset();
    readyRestartSummary.verifyRequestSha256.reset();
    readyRestartSummary.verifyResult.reset();
    readyRestartSummary.preparation.reset();
    readyRestartSummary.terminalSummary = completeTerminalSummary;
    readyRestartSummary.detail
        = QStringLiteral("Ready terminal was restored without activation proof.");
    QVERIFY(readyRestartSummary.isValid());
    RuntimePackageCompilerPreparationRecord invalidReadyRestart = readyRestartSummary;
    invalidReadyRestart.terminalSummary->verifyResultSha256.reset();
    QVERIFY(!invalidReadyRestart.isValid());
    RuntimePackageCompilerPreparationRecord canceledRestartSummary = readyRestartSummary;
    canceledRestartSummary.phase = RuntimePackageCompilerPreparationPhase::Canceled;
    canceledRestartSummary.terminalSummary
        = RuntimePackageCompilerPreparationTerminalSummary{};
    canceledRestartSummary.detail = QStringLiteral("Canceled terminal was restored.");
    QVERIFY(canceledRestartSummary.isValid());
    RuntimePackageCompilerPreparationRecord failedRestartSummary = canceledRestartSummary;
    failedRestartSummary.phase = RuntimePackageCompilerPreparationPhase::Failed;
    failedRestartSummary.detail = QStringLiteral("Failed terminal was restored.");
    QVERIFY(failedRestartSummary.isValid());
    RuntimePackageCompilerPreparationTerminalSummary invalidTerminalSummary;
    invalidTerminalSummary.verifyResultSha256
        = proof.verifyResult.envelope.canonicalResult.sha256();
    QVERIFY(!invalidTerminalSummary.isValid());
    const RuntimePackageCompilerPreparationSnapshot validSnapshot{1, {ready}};
    QVERIFY(validSnapshot.isValid());
    QVERIFY(RuntimePackageCompilerPreparationSnapshot{}.isValid());
    QVERIFY((!RuntimePackageCompilerPreparationSnapshot{0, {ready}}.isValid()));
    QVERIFY((!RuntimePackageCompilerPreparationSnapshot{1, {ready, ready}}.isValid()));

    ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);
    QVERIFY(registry->providers(ProviderKind::RuntimePackageCompiler).isEmpty());
    TestRuntimePackageCompilerPreparationCoordinator coordinator(registry);

    QVERIFY(!coordinator.start(startRequest));
    QCOMPARE(coordinator.startCount, 0);

    ExtensionSystem::PluginManager::addObject(&provider);
    auto removeProvider = qScopeGuard(
        [&] { ExtensionSystem::PluginManager::removeObject(&provider); });
    QVERIFY(!coordinator.start(startRequest));
    QCOMPARE(coordinator.startCount, 0);

    provider.setAvailable(true);
    const auto started = coordinator.start(startRequest);
    QVERIFY_RESULT(started);
    QCOMPARE(*started, RuntimePackageCompilerPreparationDisposition::Started);
    QCOMPARE(coordinator.startCount, 1);
    QCOMPARE(coordinator.lastProvider, &provider);

    const auto replay = coordinator.start(startRequest);
    QVERIFY_RESULT(replay);
    QCOMPARE(*replay, RuntimePackageCompilerPreparationDisposition::Replayed);
    QCOMPARE(coordinator.startCount, 1);

    RuntimePackageCompilerPreparationStartRequest conflict = startRequest;
    conflict.rollbackOnActivationFailure = false;
    QVERIFY(conflict.isValid());
    QVERIFY(!coordinator.start(conflict));
    QCOMPARE(coordinator.startCount, 1);

    const auto currentRecord = coordinator.record(startRequest.compileRequest.operationId);
    QVERIFY_RESULT(currentRecord);
    QVERIFY(*currentRecord);
    QCOMPARE((*currentRecord)->phase, RuntimePackageCompilerPreparationPhase::Reserved);
    const auto currentSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(currentSnapshot);
    QCOMPARE(currentSnapshot->records.size(), 1);

    QSignalSpy recordChangedSpy(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::recordChanged);
    QSignalSpy snapshotChangedSpy(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::snapshotChanged);
    QCOMPARE(recordChangedSpy.count(), 0);
    QCOMPARE(snapshotChangedSpy.count(), 0);
    const Utils::Result<> publishedInitial = coordinator.publishTransitionForTest(
        std::nullopt, **currentRecord, *currentSnapshot);
    QVERIFY_RESULT(publishedInitial);
    QCOMPARE(recordChangedSpy.count(), 1);
    QCOMPARE(snapshotChangedSpy.count(), 1);
    const Utils::Result<> publishedReplay = coordinator.publishTransitionForTest(
        std::nullopt, **currentRecord, *currentSnapshot);
    QVERIFY_RESULT(publishedReplay);
    QCOMPARE(recordChangedSpy.count(), 1);
    QCOMPARE(snapshotChangedSpy.count(), 1);

    RuntimePackageCompilerPreparationRecord compiling = **currentRecord;
    compiling.revision = 2;
    compiling.phase = RuntimePackageCompilerPreparationPhase::Compiling;
    QVERIFY(compiling.isValid());
    coordinator.setRecordForTest(compiling);
    const auto compilingSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(compilingSnapshot);
    const Utils::Result<> publishedCompiling = coordinator.publishTransitionForTest(
        **currentRecord, compiling, *compilingSnapshot);
    QVERIFY_RESULT(publishedCompiling);
    QCOMPARE(recordChangedSpy.count(), 2);
    QCOMPARE(snapshotChangedSpy.count(), 2);

    RuntimePackageCompilerPreparationRecord evidencePrevious = makeRecord(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    evidencePrevious.revision = 3;
    RuntimePackageCompilerPreparationRecord replacedEvidence = makeRecord(
        RuntimePackageCompilerPreparationPhase::Finalizing);
    replacedEvidence.revision = 4;
    const QString originalOutputDirectory = replacedEvidence.compileResult->outputDirectory;
    const QString changedOutputDirectory
        = QStringLiteral("/tmp/embed-labs-compiler-output-replaced");
    QByteArray changedCompileResult
        = replacedEvidence.compileResult->envelope.canonicalResult.exactBytes();
    QVERIFY(changedCompileResult.contains(originalOutputDirectory.toUtf8()));
    changedCompileResult.replace(
        originalOutputDirectory.toUtf8(), changedOutputDirectory.toUtf8());
    replacedEvidence.compileResult->outputDirectory = changedOutputDirectory;
    replacedEvidence.compileResult->envelope.canonicalResult
        = RuntimePackageCompilerFixture::canonical(std::move(changedCompileResult));
    QVERIFY(evidencePrevious.isValid());
    QVERIFY(replacedEvidence.isValid());
    TestRuntimePackageCompilerPreparationCoordinator evidenceMutationCoordinator(registry);
    evidenceMutationCoordinator.setRecordForTest(replacedEvidence);
    const RuntimePackageCompilerPreparationSnapshot replacedEvidenceSnapshot{
        4, {replacedEvidence}};
    QVERIFY(!evidenceMutationCoordinator.publishTransitionForTest(
        evidencePrevious, replacedEvidence, replacedEvidenceSnapshot));

    RuntimePackageCompilerPreparationRecord evidenceReconciliation = evidencePrevious;
    evidenceReconciliation.phase
        = RuntimePackageCompilerPreparationPhase::ReconciliationRequired;
    evidenceReconciliation.revision = 5;
    evidenceReconciliation.detail
        = QStringLiteral("Compiler evidence requires explicit reconstruction.");
    RuntimePackageCompilerPreparationRecord rebuiltCompiling = makeRecord(
        RuntimePackageCompilerPreparationPhase::Compiling);
    rebuiltCompiling.revision = 6;
    QVERIFY(evidenceReconciliation.isValid());
    QVERIFY(rebuiltCompiling.isValid());
    TestRuntimePackageCompilerPreparationCoordinator reconciliationCoordinator(registry);
    reconciliationCoordinator.setRecordForTest(rebuiltCompiling);
    const RuntimePackageCompilerPreparationSnapshot rebuiltCompilingSnapshot{
        6, {rebuiltCompiling}};
    QVERIFY_RESULT(reconciliationCoordinator.publishTransitionForTest(
        evidenceReconciliation, rebuiltCompiling, rebuiltCompilingSnapshot));

    RuntimePackageCompilerPreparationRecord reentrantAwaiting = makeRecord(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    reentrantAwaiting.revision = 3;
    RuntimePackageCompilerPreparationRecord reentrantFinalizing = makeRecord(
        RuntimePackageCompilerPreparationPhase::Finalizing);
    reentrantFinalizing.revision = 4;
    QVERIFY(reentrantAwaiting.isValid());
    QVERIFY(reentrantFinalizing.isValid());
    QList<quint64> reentrantSnapshotSequences;
    const QMetaObject::Connection snapshotSequenceConnection = connect(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::snapshotChanged,
        &coordinator,
        [&reentrantSnapshotSequences](
            const RuntimePackageCompilerPreparationSnapshot &published) {
            reentrantSnapshotSequences.append(published.sequence);
        });
    bool reentered = false;
    Utils::Result<> nestedPublication = Utils::ResultError(
        QStringLiteral("Nested publication did not run."));
    const QMetaObject::Connection reentrantConnection = connect(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::recordChanged,
        &coordinator,
        [&](const RuntimePackageCompilerPreparationRecord &published) {
            if (reentered || published != reentrantAwaiting)
                return;
            reentered = true;
            coordinator.setRecordForTest(reentrantFinalizing);
            const RuntimePackageCompilerPreparationSnapshot nestedSnapshot{
                4, {reentrantFinalizing}};
            nestedPublication = coordinator.publishTransitionForTest(
                reentrantAwaiting, reentrantFinalizing, nestedSnapshot);
        },
        Qt::DirectConnection);
    coordinator.setRecordForTest(reentrantAwaiting);
    const RuntimePackageCompilerPreparationSnapshot reentrantAwaitingSnapshot{
        3, {reentrantAwaiting}};
    const Utils::Result<> outerPublication = coordinator.publishTransitionForTest(
        compiling, reentrantAwaiting, reentrantAwaitingSnapshot);
    QVERIFY_RESULT(outerPublication);
    QVERIFY(reentered);
    QVERIFY_RESULT(nestedPublication);
    QCOMPARE(recordChangedSpy.count(), 4);
    QCOMPARE(snapshotChangedSpy.count(), 3);
    QCOMPARE(reentrantSnapshotSequences, QList<quint64>{4});
    disconnect(reentrantConnection);
    disconnect(snapshotSequenceConnection);

    RuntimePackageCompilerPreparationRecord sequenceRollback = makeRecord(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    sequenceRollback.revision = 3;
    QVERIFY(sequenceRollback.isValid());
    coordinator.setRecordForTest(sequenceRollback);
    coordinator.snapshotSequenceOverride = 2;
    const auto sequenceRollbackSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(sequenceRollbackSnapshot);
    QVERIFY(!coordinator.publishTransitionForTest(
        compiling, sequenceRollback, *sequenceRollbackSnapshot));
    QCOMPARE(recordChangedSpy.count(), 4);
    QCOMPARE(snapshotChangedSpy.count(), 3);
    coordinator.snapshotSequenceOverride.reset();

    RuntimePackageCompilerPreparationRecord badRevision = compiling;
    badRevision.revision = 3;
    badRevision.phase = RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature;
    badRevision.compileResult = proof.compileResult;
    badRevision.detachedSigningRequest = *proof.compileResult.signRequest;
    QVERIFY(badRevision.isValid());
    coordinator.setRecordForTest(badRevision);
    const auto badRevisionSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(badRevisionSnapshot);
    QVERIFY(!coordinator.publishTransitionForTest(
        **currentRecord, badRevision, *badRevisionSnapshot));

    RuntimePackageCompilerPreparationRecord changedProvider = compiling;
    changedProvider.compilerProviderId = QStringLiteral("EtherCAT.Compiler.Different");
    QVERIFY(changedProvider.isValid());
    coordinator.setRecordForTest(changedProvider);
    const auto changedProviderSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(changedProviderSnapshot);
    QVERIFY(!coordinator.publishTransitionForTest(
        **currentRecord, changedProvider, *changedProviderSnapshot));

    RuntimePackageCompilerPreparationRecord changedSamePhase = **currentRecord;
    changedSamePhase.revision = 2;
    QVERIFY(changedSamePhase.isValid());
    coordinator.setRecordForTest(changedSamePhase);
    const auto changedSamePhaseSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(changedSamePhaseSnapshot);
    QVERIFY(!coordinator.publishTransitionForTest(
        **currentRecord, changedSamePhase, *changedSamePhaseSnapshot));

    RuntimePackageCompilerPreparationRecord wrongInitial = **currentRecord;
    wrongInitial.revision = 2;
    coordinator.setRecordForTest(wrongInitial);
    const auto wrongInitialSnapshot = coordinator.snapshot();
    QVERIFY_RESULT(wrongInitialSnapshot);
    QVERIFY(!coordinator.publishTransitionForTest(
        std::nullopt, wrongInitial, *wrongInitialSnapshot));

    coordinator.setRecordForTest(compiling);
    RuntimePackageCompilerPreparationSnapshot mismatchedSnapshot = *compilingSnapshot;
    ++mismatchedSnapshot.sequence;
    QVERIFY(mismatchedSnapshot.isValid());
    QVERIFY(!coordinator.publishTransitionForTest(
        **currentRecord, compiling, mismatchedSnapshot));

    RuntimePackageCompilerPreparationRecord awaiting = makeRecord(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    coordinator.setRecordForTest(awaiting);
    int signingSignalCount = 0;
    connect(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::detachedSigningRequested,
        &coordinator,
        [&signingSignalCount](const auto &, const auto &) { ++signingSignalCount; });
    QSignalSpy signingRequestedSpy(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::detachedSigningRequested);
    QCOMPARE(signingRequestedSpy.count(), 0);
    const Utils::Result<> publishedSigningRequest
        = coordinator.publishSigningRequestForTest(awaiting);
    QVERIFY_RESULT(publishedSigningRequest);
    QCOMPARE(signingSignalCount, 1);
    QCOMPARE(signingRequestedSpy.count(), 1);
    coordinator.setRecordForTest(compiling);
    QVERIFY(!coordinator.publishSigningRequestForTest(compiling));
    QCOMPARE(signingRequestedSpy.count(), 1);

    coordinator.setRecordForTest(ready);
    QSignalSpy preparationReadySpy(
        &coordinator,
        &RuntimePackageCompilerPreparationCoordinator::preparationReady);
    const Utils::Result<> publishedPreparation
        = coordinator.publishPreparationForTest(ready);
    QVERIFY_RESULT(publishedPreparation);
    QCOMPARE(preparationReadySpy.count(), 1);
    coordinator.setRecordForTest(awaiting);
    QVERIFY(!coordinator.publishPreparationForTest(awaiting));
    QCOMPARE(preparationReadySpy.count(), 1);

    coordinator.setRecordForTest(readyRestartSummary);
    QVERIFY(!coordinator.publishPreparationForTest(readyRestartSummary));
    QCOMPARE(preparationReadySpy.count(), 1);
    const auto terminalRestartStart = coordinator.start(startRequest);
    QVERIFY_RESULT(terminalRestartStart);
    QCOMPARE(
        *terminalRestartStart,
        RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);
    const auto terminalRestartCancel = coordinator.cancel(
        startRequest.compileRequest.operationId);
    QVERIFY_RESULT(terminalRestartCancel);
    QCOMPARE(
        *terminalRestartCancel,
        RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);
    const auto terminalRestartResume = coordinator.resume(startRequest);
    QVERIFY_RESULT(terminalRestartResume);
    QCOMPARE(
        *terminalRestartResume,
        RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);
    const auto terminalRestartSubmit = coordinator.submitDetachedSigningResponse(
        startRequest.compileRequest.operationId,
        proof.finalizeRequest.detachedSigningResponse);
    QVERIFY_RESULT(terminalRestartSubmit);
    QCOMPARE(
        *terminalRestartSubmit,
        RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);

    std::future<Utils::Result<RuntimePackageCompilerPreparationDisposition>> wrongThreadStart
        = std::async(std::launch::async, [&coordinator, startRequest] {
              return coordinator.start(startRequest);
          });
    QVERIFY(!wrongThreadStart.get());
    std::future<Utils::Result<RuntimePackageCompilerPreparationSnapshot>> wrongThreadSnapshot
        = std::async(std::launch::async, [&coordinator] { return coordinator.snapshot(); });
    QVERIFY(!wrongThreadSnapshot.get());
    QCOMPARE(coordinator.startCount, 1);

    coordinator.setRecordForTest(makeRecord(
        RuntimePackageCompilerPreparationPhase::Compiling));
    QVERIFY(!coordinator.submitDetachedSigningResponse(
        startRequest.compileRequest.operationId,
        proof.finalizeRequest.detachedSigningResponse));
    QCOMPARE(coordinator.submitCount, 0);

    coordinator.setRecordForTest(makeRecord(
        RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    const auto submitted = coordinator.submitDetachedSigningResponse(
        startRequest.compileRequest.operationId,
        proof.finalizeRequest.detachedSigningResponse);
    QVERIFY_RESULT(submitted);
    QCOMPARE(*submitted, RuntimePackageCompilerPreparationDisposition::Accepted);
    QCOMPARE(coordinator.submitCount, 1);
    QCOMPARE(coordinator.lastProvider, &provider);

    coordinator.setRecordForTest(makeRecord(
        RuntimePackageCompilerPreparationPhase::Finalizing));
    const auto lostResponseReplay = coordinator.submitDetachedSigningResponse(
        startRequest.compileRequest.operationId,
        proof.finalizeRequest.detachedSigningResponse);
    QVERIFY_RESULT(lostResponseReplay);
    QCOMPARE(*lostResponseReplay, RuntimePackageCompilerPreparationDisposition::Replayed);
    QCOMPARE(coordinator.submitCount, 1);
    QVERIFY(!coordinator.submitDetachedSigningResponse(
        startRequest.compileRequest.operationId,
        RuntimePackageCompilerFixture::canonical("{\"different\":true}\n")));
    QCOMPARE(coordinator.submitCount, 1);

    coordinator.setRecordForTest(makeRecord(
        RuntimePackageCompilerPreparationPhase::Compiling));
    const auto canceled = coordinator.cancel(startRequest.compileRequest.operationId);
    QVERIFY_RESULT(canceled);
    QCOMPARE(*canceled, RuntimePackageCompilerPreparationDisposition::Accepted);
    QCOMPARE(coordinator.cancelCount, 1);
    coordinator.setRecordForTest(makeRecord(
        RuntimePackageCompilerPreparationPhase::CancelRequested));
    const auto cancelReplay = coordinator.cancel(startRequest.compileRequest.operationId);
    QVERIFY_RESULT(cancelReplay);
    QCOMPARE(*cancelReplay, RuntimePackageCompilerPreparationDisposition::Replayed);
    QCOMPARE(coordinator.cancelCount, 1);
    coordinator.setRecordForTest(ready);
    const auto terminalCancel = coordinator.cancel(startRequest.compileRequest.operationId);
    QVERIFY_RESULT(terminalCancel);
    QCOMPARE(
        *terminalCancel,
        RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);

    const RuntimePackageCompilerPreparationRecord reconciliation = makeRecord(
        RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    coordinator.setRecordForTest(reconciliation);
    const auto reconcileCancel = coordinator.cancel(startRequest.compileRequest.operationId);
    QVERIFY_RESULT(reconcileCancel);
    QCOMPARE(
        *reconcileCancel,
        RuntimePackageCompilerPreparationDisposition::ReconciliationRequired);
    QVERIFY(!coordinator.resume(conflict));
    QCOMPARE(coordinator.resumeCount, 0);
    const auto resumed = coordinator.resume(startRequest);
    QVERIFY_RESULT(resumed);
    QCOMPARE(*resumed, RuntimePackageCompilerPreparationDisposition::Accepted);
    QCOMPARE(coordinator.resumeCount, 1);
    QCOMPARE(coordinator.lastResumeRequest, startRequest);

    RuntimePackageCompilerPreparationRecord restartPlaceholder = reconciliation;
    restartPlaceholder.startRequest.reset();
    restartPlaceholder.compileRequestSha256.reset();
    restartPlaceholder.revision = 9;
    QVERIFY(restartPlaceholder.isValid());
    coordinator.setRecordForTest(restartPlaceholder);
    const auto placeholderStart = coordinator.start(startRequest);
    QVERIFY_RESULT(placeholderStart);
    QCOMPARE(
        *placeholderStart,
        RuntimePackageCompilerPreparationDisposition::ReconciliationRequired);
    const auto placeholderResume = coordinator.resume(startRequest);
    QVERIFY_RESULT(placeholderResume);
    QCOMPARE(*placeholderResume, RuntimePackageCompilerPreparationDisposition::Accepted);
    QCOMPARE(coordinator.resumeCount, 2);
    QCOMPARE(coordinator.lastResumeRequest, startRequest);
    RuntimePackageCompilerPreparationStartRequest mismatchedResume = startRequest;
    mismatchedResume.rollbackOnActivationFailure = false;
    QVERIFY(mismatchedResume.isValid());
    QVERIFY(!coordinator.resume(mismatchedResume));
    QCOMPARE(coordinator.resumeCount, 2);

    RuntimePackageCompilerPreparationRecord resumedCompiling = makeRecord(
        RuntimePackageCompilerPreparationPhase::Compiling);
    resumedCompiling.revision = 10;
    coordinator.setRecordForTest(resumedCompiling);
    const RuntimePackageCompilerPreparationSnapshot resumedCompilingSnapshot{
        10, {resumedCompiling}};
    const Utils::Result<> publishedResumedCompiling = coordinator.publishTransitionForTest(
        restartPlaceholder, resumedCompiling, resumedCompilingSnapshot);
    QVERIFY_RESULT(publishedResumedCompiling);

    RuntimePackageCompilerPreparationRecord changedFingerprintPlaceholder
        = restartPlaceholder;
    QByteArray changedFingerprint
        = changedFingerprintPlaceholder.startRequestFingerprint.value();
    changedFingerprint[0] ^= 1;
    changedFingerprintPlaceholder.startRequestFingerprint
        = Data::RuntimePackageCompilerSha256{changedFingerprint};
    QVERIFY(changedFingerprintPlaceholder.isValid());
    coordinator.setRecordForTest(changedFingerprintPlaceholder);
    QVERIFY(!coordinator.resume(startRequest));
    QCOMPARE(coordinator.resumeCount, 2);

    coordinator.setRecordForTest(restartPlaceholder);

    provider.setAvailable(false);
    const auto frozenProviderUnavailable = coordinator.resume(startRequest);
    QVERIFY_RESULT(frozenProviderUnavailable);
    QCOMPARE(
        *frozenProviderUnavailable,
        RuntimePackageCompilerPreparationDisposition::ReconciliationRequired);
    QCOMPARE(coordinator.resumeCount, 2);
    provider.setAvailable(true);

    QThread compilerProviderThread;
    compilerProviderThread.start();
    provider.moveToThread(&compilerProviderThread);
    QCOMPARE(provider.thread(), &compilerProviderThread);
    const auto wrongProviderThreadResume = coordinator.resume(startRequest);
    QVERIFY_RESULT(wrongProviderThreadResume);
    QCOMPARE(
        *wrongProviderThreadResume,
        RuntimePackageCompilerPreparationDisposition::ReconciliationRequired);
    QCOMPARE(coordinator.resumeCount, 2);
    coordinator.setRecordForTest(std::nullopt);
    QVERIFY(!coordinator.start(startRequest));
    QCOMPARE(coordinator.startCount, 1);
    QThread *coordinatorThread = QThread::currentThread();
    bool movedProviderBack = false;
    QVERIFY(QMetaObject::invokeMethod(
        &provider,
        [&provider, coordinatorThread, &movedProviderBack] {
            provider.moveToThread(coordinatorThread);
            movedProviderBack = provider.thread() == coordinatorThread;
        },
        Qt::BlockingQueuedConnection));
    QVERIFY(movedProviderBack);
    compilerProviderThread.quit();
    QVERIFY(compilerProviderThread.wait(2000));

    coordinator.setRecordForTest(restartPlaceholder);
    coordinator.returnInvalidSnapshot = true;
    QVERIFY(!coordinator.snapshot());
    coordinator.returnInvalidSnapshot = false;

    coordinator.setRecordForTest(std::nullopt);
    Data::RuntimePackageCompilerCompileRequest alternateCompile = fixture.request;
    alternateCompile.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000088")};
    alternateCompile.intentId = QStringLiteral("embedlabs:compile-intent:alternate");
    alternateCompile.configurationId = 4288;
    QVERIFY(alternateCompile.isValid());
    const RuntimePackageCompilerPreparationStartRequest alternateStart{
        alternateCompile,
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000089")},
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/compiler-preparation-088")},
        true,
    };
    QVERIFY(alternateStart.isValid());
    TestRuntimePackageCompilerProvider secondProvider(
        Utils::Id("EtherCAT.Compiler.SecondPreparation"));
    secondProvider.setAvailable(true);
    ExtensionSystem::PluginManager::addObject(&secondProvider);
    auto removeSecondProvider = qScopeGuard(
        [&] { ExtensionSystem::PluginManager::removeObject(&secondProvider); });
    QVERIFY(!coordinator.start(alternateStart));
    QCOMPARE(coordinator.startCount, 1);

    QVERIFY(QMetaType::fromType<RuntimePackageCompilerPreparationPhase>().isValid());
    QVERIFY(
        QMetaType::fromType<RuntimePackageCompilerPreparationStartRequest>().isValid());
    QVERIFY(QMetaType::fromType<RuntimePackageCompilerPreparationRecord>().isValid());
    QVERIFY(QMetaType::fromType<RuntimePackageCompilerPreparationSnapshot>().isValid());
    QVERIFY(QMetaType::fromType<RuntimePackageCompilerPreparationDisposition>().isValid());
    QVERIFY(QMetaType::fromName(
                "EtherCAT::Core::RuntimePackageCompilerPreparationPhase")
                .isValid());
    QVERIFY(QMetaType::fromName(
                "EtherCAT::Core::RuntimePackageCompilerPreparationStartRequest")
                .isValid());
    QVERIFY(QMetaType::fromName(
                "EtherCAT::Core::RuntimePackageCompilerPreparationRecord")
                .isValid());
    QVERIFY(QMetaType::fromName(
                "EtherCAT::Core::RuntimePackageCompilerPreparationSnapshot")
                .isValid());
    QVERIFY(QMetaType::fromName(
                "EtherCAT::Core::RuntimePackageCompilerPreparationDisposition")
                .isValid());
}

void EtherCATCoreTests::testRuntimePackageCompilerActivationProofRouting()
{
    RuntimePackageCompilerFixture fixture;
    TestRuntimePackageCompilerProvider provider;
    Data::RuntimePackageCompilerActivationProof proof
        = successfulActivationProof(fixture.request);
    proof.compilerProviderId = provider.id().toString();
    QVERIFY(proof.isValid());
    provider.expectedActivationProof = proof;

    const Data::RuntimePackageCompilerProjectSnapshotEvidence &snapshotEvidence
        = proof.compileRequest.projectSnapshotEvidence;
    const Data::RuntimePackageActivationProjectCapture capture{
        snapshotEvidence.snapshot(),
        QByteArray("{\"format\":\"embed-labs-ethercat-project\"}\n"),
        snapshotEvidence.documentRevisionNumber(),
        snapshotEvidence.documentRevision(),
        snapshotEvidence.originalBinding(),
    };
    QVERIFY(capture.isValid());

    RuntimePackageActivationPreparationRequest request{
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/runtime-compiler-proof-routing")},
        proof.compileRequest.topologyEvidence.scope,
        proof.finalizeResult.packageBytes,
        proof.compiledProjectSource,
        proof.effectiveProjectCompanion,
        proof.verifyResult,
        true,
        proof,
    };
    QVERIFY(request.isValid());

    ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);
    QVERIFY(registry->providers(ProviderKind::RuntimePackageCompiler).isEmpty());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, request, capture));
    QCOMPARE(provider.activationProofValidationCount, 0);

    ExtensionSystem::PluginManager::addObject(&provider);
    auto removeProvider = qScopeGuard(
        [&] { ExtensionSystem::PluginManager::removeObject(&provider); });
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, request, capture));
    QCOMPARE(provider.activationProofValidationCount, 0);

    provider.setAvailable(true);
    const Utils::Result<> accepted
        = validateRuntimePackageCompilerActivationProof(registry, request, capture);
    QVERIFY_RESULT(accepted);
    QCOMPARE(provider.activationProofValidationCount, 1);
    QCOMPARE(provider.activationProofs.constLast(), proof);

    QThread proofProviderThread;
    proofProviderThread.start();
    provider.moveToThread(&proofProviderThread);
    QCOMPARE(provider.thread(), &proofProviderThread);
    const Utils::Result<> wrongProviderThread
        = validateRuntimePackageCompilerActivationProof(registry, request, capture);
    QVERIFY(!wrongProviderThread);
    QCOMPARE(
        wrongProviderThread.error(),
        QStringLiteral("A compiler provider belongs to a different thread."));
    QCOMPARE(provider.activationProofValidationCount, 1);
    QThread *testThread = QThread::currentThread();
    bool movedProviderBack = false;
    QVERIFY(QMetaObject::invokeMethod(
        &provider,
        [&provider, testThread, &movedProviderBack] {
            provider.moveToThread(testThread);
            movedProviderBack = provider.thread() == testThread;
        },
        Qt::BlockingQueuedConnection));
    QVERIFY(movedProviderBack);
    proofProviderThread.quit();
    QVERIFY(proofProviderThread.wait(2000));

    std::future<Utils::Result<>> crossThreadValidation = std::async(
        std::launch::async,
        [registry, request, capture] {
            return validateRuntimePackageCompilerActivationProof(
                registry, request, capture);
        });
    const Utils::Result<> wrongThread = crossThreadValidation.get();
    QVERIFY(!wrongThread);
    QCOMPARE(
        wrongThread.error(),
        QStringLiteral("Compiler provider registry belongs to a different thread."));
    QCOMPARE(provider.activationProofValidationCount, 1);

    RuntimePackageActivationPreparationRequest missingProof = request;
    missingProof.compilerActivationProof.reset();
    QVERIFY(!missingProof.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, missingProof, capture));
    QCOMPARE(provider.activationProofValidationCount, 1);

    RuntimePackageActivationPreparationRequest invalidInput = request;
    invalidInput.operationId = {};
    QVERIFY(!invalidInput.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, invalidInput, capture));
    QCOMPARE(provider.activationProofValidationCount, 1);

    provider.activationProofValidationError
        = QStringLiteral("Provisioned compiler key evidence was rejected.");
    const Utils::Result<> providerRejected
        = validateRuntimePackageCompilerActivationProof(registry, request, capture);
    QVERIFY(!providerRejected);
    QCOMPARE(
        providerRejected.error(),
        QStringLiteral("Provisioned compiler key evidence was rejected."));
    QCOMPARE(provider.activationProofValidationCount, 2);
    provider.activationProofValidationError.reset();

    RuntimePackageActivationPreparationRequest changedProof = request;
    changedProof.compilerActivationProof->finalizeRequestSha256
        = RuntimePackageCompilerFixture::sha256("changed-finalize-request");
    QVERIFY(changedProof.compilerActivationProof->isValid());
    QVERIFY(changedProof.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, changedProof, capture));
    QCOMPARE(provider.activationProofValidationCount, 3);
    QCOMPARE(provider.activationProofs.constLast(), *changedProof.compilerActivationProof);

    RuntimePackageActivationPreparationRequest changedCompileRequest = request;
    changedCompileRequest.compilerActivationProof->compileRequest.intentId.append(
        QStringLiteral(".changed"));
    QVERIFY(changedCompileRequest.compilerActivationProof->isValid());
    QVERIFY(changedCompileRequest.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, changedCompileRequest, capture));
    QCOMPARE(provider.activationProofValidationCount, 4);
    QCOMPARE(
        provider.activationProofs.constLast(),
        *changedCompileRequest.compilerActivationProof);

    RuntimePackageActivationPreparationRequest changedVerifyRequest = request;
    changedVerifyRequest.compilerActivationProof->verifyRequestSha256
        = RuntimePackageCompilerFixture::sha256("changed-verify-request");
    QVERIFY(changedVerifyRequest.compilerActivationProof->isValid());
    QVERIFY(changedVerifyRequest.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, changedVerifyRequest, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);
    QCOMPARE(
        provider.activationProofs.constLast(),
        *changedVerifyRequest.compilerActivationProof);

    Data::RuntimePackageCompilerCompileRequest alternateCompileRequest = fixture.request;
    alternateCompileRequest.intentId.append(QStringLiteral(".alternate"));
    const Data::RuntimePackageCompilerActivationProof alternateProof
        = successfulActivationProof(alternateCompileRequest);
    QVERIFY(alternateProof.isValid());
    RuntimePackageActivationPreparationRequest changedVerification = request;
    changedVerification.compilerVerification = alternateProof.verifyResult;
    QVERIFY(changedVerification.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, changedVerification, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    RuntimePackageActivationPreparationRequest changedPackage = request;
    changedPackage.packageBytes.append("-changed");
    QVERIFY(changedPackage.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, changedPackage, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    RuntimePackageActivationPreparationRequest changedProject = request;
    changedProject.compiledProjectSource.append("-changed");
    QVERIFY(changedProject.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, changedProject, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    RuntimePackageActivationPreparationRequest changedCompanion = request;
    changedCompanion.effectiveProjectCompanion.append("-changed");
    QVERIFY(changedCompanion.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, changedCompanion, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    RuntimePackageActivationPreparationRequest changedProjectScope = request;
    changedProjectScope.scope.projectId = Data::NodeId::create();
    QVERIFY(changedProjectScope.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, changedProjectScope, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    RuntimePackageActivationPreparationRequest changedMasterScope = request;
    changedMasterScope.scope.masterId = Data::NodeId::create();
    QVERIFY(changedMasterScope.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, changedMasterScope, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    QByteArray changedSerializedProject = capture.serializedProject();
    changedSerializedProject.append(' ');
    const Data::RuntimePackageActivationProjectCapture changedSerializedProjectCapture{
        capture.snapshot(),
        changedSerializedProject,
        capture.documentRevisionNumber(),
        capture.documentRevision(),
        capture.originalBinding(),
    };
    QVERIFY(changedSerializedProjectCapture.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, request, changedSerializedProjectCapture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    const Data::RuntimePackageActivationProjectCapture changedRevisionNumberCapture{
        capture.snapshot(),
        capture.serializedProject(),
        capture.documentRevisionNumber() + 1,
        capture.documentRevision(),
        capture.originalBinding(),
    };
    QVERIFY(changedRevisionNumberCapture.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, request, changedRevisionNumberCapture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    const Data::RuntimePackageActivationProjectCapture changedDocumentRevisionCapture{
        capture.snapshot(),
        capture.serializedProject(),
        capture.documentRevisionNumber(),
        Data::RuntimePackageActivationDocumentRevisionToken{"changed-document-revision"},
        capture.originalBinding(),
    };
    QVERIFY(changedDocumentRevisionCapture.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, request, changedDocumentRevisionCapture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    const Data::RuntimePackageActivationProjectCapture changedOriginalBindingCapture{
        capture.snapshot(),
        capture.serializedProject(),
        capture.documentRevisionNumber(),
        capture.documentRevision(),
        Data::RuntimePackageActivationOriginalBindingToken{"changed-original-binding"},
    };
    QVERIFY(changedOriginalBindingCapture.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(
        registry, request, changedOriginalBindingCapture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    RuntimePackageActivationPreparationRequest foreignProvider = request;
    foreignProvider.compilerActivationProof->compilerProviderId
        = QStringLiteral("EtherCAT.Compiler.Foreign");
    QVERIFY(foreignProvider.compilerActivationProof->isValid());
    QVERIFY(foreignProvider.isValid());
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, foreignProvider, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);

    provider.setAvailable(false);
    {
        Provider incompatibleProvider(
            ProviderKind::RuntimePackageCompiler,
            Utils::Id("EtherCAT.Compiler.Incompatible"),
            QStringLiteral("Incompatible compiler provider"));
        incompatibleProvider.setAvailable(true);
        ExtensionSystem::PluginManager::addObject(&incompatibleProvider);
        auto removeIncompatibleProvider = qScopeGuard(
            [&] { ExtensionSystem::PluginManager::removeObject(&incompatibleProvider); });
        QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, request, capture));
        QCOMPARE(provider.activationProofValidationCount, 5);
    }
    provider.setAvailable(true);

    TestRuntimePackageCompilerProvider secondProvider(Utils::Id("EtherCAT.Compiler.Second"));
    secondProvider.setAvailable(true);
    ExtensionSystem::PluginManager::addObject(&secondProvider);
    auto removeSecondProvider = qScopeGuard(
        [&] { ExtensionSystem::PluginManager::removeObject(&secondProvider); });
    QVERIFY(!validateRuntimePackageCompilerActivationProof(registry, request, capture));
    QCOMPARE(provider.activationProofValidationCount, 5);
    QCOMPARE(secondProvider.activationProofValidationCount, 0);
}

void EtherCATCoreTests::testSemanticRuntimeValueSemantics()
{
    SemanticRuntimeFixture fixture;

    const Data::SemanticRuntimeContext contextCopy = fixture.context;
    QCOMPARE(contextCopy, fixture.context);
    QCOMPARE(contextCopy.signalStates.constFirst().binding, fixture.binding);
    QCOMPARE(
        contextCopy.signalStates.constFirst().binding->mappingDigest,
        contextCopy.signalStates.constFirst().binding->controllerMappingDigest);
    QCOMPARE(contextCopy.signalStates.constFirst().definition.id, fixture.target.signalId);
    QVERIFY(!contextCopy.mock);

    fixture.context.signalStates.first().value->value = false;
    QVERIFY(fixture.context != contextCopy);

    Data::SemanticActionRuntimeState action;
    action.target = fixture.target;
    action.target.kind = Data::SemanticRuntimeTargetKind::Action;
    action.target.signalId = {};
    action.target.actionId = {"urn:example.test:action/controlled-stop"};
    action.definition.id = action.target.actionId;
    action.definition.displayName = "Controlled stop";
    action.actionBindingId = action.target.actionId.value;
    action.actionDefinitionId = "urn:example.test:action-definition/controlled-stop";
    action.actionDefinitionDigest = {"sha256", QByteArray(32, '\x71')};
    action.qualification = Data::SemanticActionQualification::Qualified;
    action.availability = Data::SemanticActionAvailability::AwaitingApproval;
    action.bindings = {fixture.binding};
    action.requiresApproval = true;
    action.requiresExclusiveControl = true;
    action.holdToRun = true;
    action.maximumTtlMs = 250;
    action.maximumTtlCycles = 2000;
    QCOMPARE(action.definition.id, action.target.actionId);

    Data::SemanticOperationApproval approval;
    approval.request.operationId = fixture.request.operationId;
    approval.request.decision = Data::SemanticApprovalDecision::Approved;
    approval.request.challenge = QByteArray(32, '\x4d');
    approval.request.expectedRequestDigest = QByteArray(32, '\x7c');
    approval.request.expectedContextHash = fixture.context.contextHash;
    approval.actor = fixture.actor;
    approval.decidedAt = QDateTime::currentDateTimeUtc();

    Data::SemanticOperationRecord record;
    record.request = fixture.request;
    record.actor = fixture.actor;
    record.state = Data::SemanticOperationState::ApprovalRequired;
    record.approvals = {approval};
    record.canonicalRequestDigest = QByteArray(32, '\x7c');
    record.approvalChallenge = approval.request.challenge;
    record.createdAt = approval.decidedAt;
    record.updatedAt = approval.decidedAt;
    const Data::SemanticOperationRecord recordCopy = record;
    QCOMPARE(recordCopy, record);
    QVERIFY(Data::SemanticOperationState::TimedOut != Data::SemanticOperationState::OutcomeUnknown);

    Data::SemanticRuntimeAuditEvent auditEvent;
    auditEvent.sequence = 1;
    auditEvent.controllerId = fixture.target.controllerId;
    auditEvent.operationId = fixture.request.operationId;
    auditEvent.kind = Data::SemanticAuditEventKind::ApprovalRecorded;
    auditEvent.state = record.state;
    auditEvent.actor = approval.actor;
    auditEvent.canonicalRequestDigest = record.canonicalRequestDigest;
    auditEvent.occurredAt = approval.decidedAt;
    QVERIFY(auditEvent == Data::SemanticRuntimeAuditEvent(auditEvent));

    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeTarget>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeBinding>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticSignalRuntimeState>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticActionQualification>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticActionParameterRuntimeDefinition>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticActionRuntimeState>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeContext>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticLiveRefreshSignal>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticLiveRefreshRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticLiveRefreshOutcome>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticLiveRefreshResult>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationApprovalRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationSignalObservation>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationSnapshot>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationRecord>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeAuditEvent>().isValid());
}

void EtherCATCoreTests::testSemanticLiveRefreshContract()
{
    const SemanticRuntimeFixture fixture;
    Data::SemanticLiveRefreshRequest request;
    request.controllerId = fixture.context.controllerId;
    request.scope = fixture.context.scope;
    request.expectedContextHash = fixture.context.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh-001");
    request.targets = {{fixture.deviceId, fixture.target.signalId}};
    QVERIFY(request.isValid());
    QCOMPARE(request, Data::SemanticLiveRefreshRequest(request));

    Data::SemanticLiveRefreshRequest maximum = request;
    maximum.targets.clear();
    for (int index = 0; index < 64; ++index) {
        maximum.targets.append(
            {fixture.deviceId, {QStringLiteral("urn:example.test:signal/input.%1").arg(index)}});
    }
    QVERIFY(maximum.isValid());

    Data::SemanticLiveRefreshRequest tooMany = maximum;
    tooMany.targets.append({fixture.deviceId, {QStringLiteral("urn:example.test:signal/input.64")}});
    QVERIFY(!tooMany.isValid());

    Data::SemanticLiveRefreshRequest duplicate = request;
    duplicate.targets.append(duplicate.targets.constFirst());
    QVERIFY(!duplicate.isValid());

    Data::SemanticLiveRefreshRequest missingSignal = request;
    missingSignal.targets.first().signalId = {};
    QVERIFY(!missingSignal.isValid());
    Data::SemanticLiveRefreshRequest missingDevice = request;
    missingDevice.targets.first().deviceId = {};
    QVERIFY(!missingDevice.isValid());
    Data::SemanticLiveRefreshRequest wrongContextHash = request;
    wrongContextHash.expectedContextHash.chop(1);
    QVERIFY(!wrongContextHash.isValid());
    Data::SemanticLiveRefreshRequest invalidCorrelation = request;
    invalidCorrelation.correlationId.append(QChar::LineFeed);
    QVERIFY(!invalidCorrelation.isValid());
    Data::SemanticLiveRefreshRequest paddedController = request;
    paddedController.controllerId.prepend(' ');
    QVERIFY(!paddedController.isValid());
    Data::SemanticLiveRefreshRequest controlledController = request;
    controlledController.controllerId.append(QChar::LineFeed);
    QVERIFY(!controlledController.isValid());
    Data::SemanticLiveRefreshRequest paddedSignal = request;
    paddedSignal.targets.first().signalId.value.append(' ');
    QVERIFY(!paddedSignal.isValid());
    Data::SemanticLiveRefreshRequest controlledSignal = request;
    controlledSignal.targets.first().signalId.value.append(QChar::LineFeed);
    QVERIFY(!controlledSignal.isValid());

    const Data::SemanticLiveRefreshResult accepted{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Accepted,
        QStringLiteral("semantic-live-refresh-accepted"),
        {},
        0,
    };
    QVERIFY(accepted.isValid());
    const Data::SemanticLiveRefreshResult refreshed{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Refreshed,
        QStringLiteral("semantic-live-refresh-refreshed"),
        {},
        fixture.snapshot.captureCycle,
    };
    QVERIFY(refreshed.isValid());
    QCOMPARE(refreshed, Data::SemanticLiveRefreshResult(refreshed));

    const Data::SemanticLiveRefreshResult unsupported{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Unsupported,
        QStringLiteral("semantic-live-refresh-unsupported"),
        QStringLiteral("Semantic live refresh is unsupported."),
        0,
    };
    QVERIFY(unsupported.isValid());
    const Data::SemanticLiveRefreshResult deferred{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Deferred,
        QStringLiteral("semantic-live-refresh-backpressure"),
        QStringLiteral("The semantic refresh queue is full."),
        0,
    };
    QVERIFY(deferred.isValid());
    const Data::SemanticLiveRefreshResult busy{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Deferred,
        QStringLiteral("semantic-live-refresh-busy"),
        QStringLiteral("Another semantic refresh is still in progress."),
        0,
    };
    QVERIFY(busy.isValid());
    const Data::SemanticLiveRefreshResult contextChanged{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Rejected,
        QStringLiteral("semantic-live-refresh-context-changed"),
        QStringLiteral("The expected semantic runtime context changed."),
        0,
    };
    QVERIFY(contextChanged.isValid());
    const Data::SemanticLiveRefreshResult providerFailed{
        request.correlationId,
        Data::SemanticLiveRefreshOutcome::Failed,
        QStringLiteral("semantic-live-refresh-provider-failed"),
        QStringLiteral("The controller provider could not refresh the semantic values."),
        0,
    };
    QVERIFY(providerFailed.isValid());
    Data::SemanticLiveRefreshResult deferredWithCapture = deferred;
    deferredWithCapture.captureCycle = fixture.snapshot.captureCycle;
    QVERIFY(!deferredWithCapture.isValid());
    Data::SemanticLiveRefreshResult leakedCapture = unsupported;
    leakedCapture.captureCycle = fixture.snapshot.captureCycle;
    QVERIFY(!leakedCapture.isValid());
    Data::SemanticLiveRefreshResult missingDetail = unsupported;
    missingDetail.detail.clear();
    QVERIFY(!missingDetail.isValid());
    Data::SemanticLiveRefreshResult missingCode = accepted;
    missingCode.code.clear();
    QVERIFY(!missingCode.isValid());
}

void EtherCATCoreTests::testSemanticRuntimeEpochValidation()
{
    const SemanticRuntimeFixture fixture;
    QVERIFY(isCompleteRuntimeResourceCatalogEpoch(fixture.epoch));
    QVERIFY(validateSemanticRuntimeEpoch(fixture.epoch, fixture.epoch).accepted());

    Data::RuntimeResourceCatalogEpoch incomplete = fixture.epoch;
    incomplete.catalogRevision = 0;
    QCOMPARE(
        validateSemanticRuntimeEpoch(fixture.epoch, incomplete).error,
        SemanticRuntimeValidationError::InvalidEpoch);

    QList<Data::RuntimeResourceCatalogEpoch> changedEpochs;
    Data::RuntimeResourceCatalogEpoch changed = fixture.epoch;
    ++changed.controllerBootId;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    changed.activePackageSlot = Data::ControllerSlot::A;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.activePackageGeneration;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.configurationId;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.topologyGeneration;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.runtimeGeneration;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.catalogRevision;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    changed.topologyIdentity.append('\x09');
    changedEpochs.append(changed);

    QCOMPARE(changedEpochs.size(), 8);
    for (const Data::RuntimeResourceCatalogEpoch &candidate : changedEpochs) {
        QCOMPARE(
            validateSemanticRuntimeEpoch(fixture.epoch, candidate).error,
            SemanticRuntimeValidationError::EpochMismatch);
    }

    QVERIFY(validateSemanticRuntimeBinding(fixture.binding).accepted());
    QVERIFY(isCanonicalSha256Digest(fixture.binding.mappingDigest));
    Data::SemanticRuntimeBinding shortAdapterHash = fixture.binding;
    shortAdapterHash.adapterContentSha256.chop(1);
    QCOMPARE(
        validateSemanticRuntimeBinding(shortAdapterHash).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticRuntimeBinding unverified = fixture.binding;
    unverified.verification.state = Data::SemanticBindingVerificationState::Unverified;
    QCOMPARE(
        validateSemanticRuntimeBinding(unverified).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeBinding mismatchedDigest = fixture.binding;
    mismatchedDigest.controllerMappingDigest.value[0] ^= '\x01';
    QCOMPARE(
        validateSemanticRuntimeBinding(mismatchedDigest).error,
        SemanticRuntimeValidationError::MappingDigestMismatch);
}

void EtherCATCoreTests::testSemanticRuntimeReadValidation()
{
    const SemanticRuntimeFixture fixture;

    const SemanticRuntimeReadValidation good
        = validateSemanticRuntimeRead(fixture.binding, fixture.catalog, fixture.snapshot);
    QVERIFY(good.validation.accepted());
    QVERIFY(good.sample);
    QCOMPARE(good.sample->value.value.toBool(), true);

    Data::SemanticRuntimeBinding outputBinding = fixture.binding;
    outputBinding.direction = Data::RuntimeResourceDirection::Output;
    outputBinding.access = Data::RuntimeResourceAccess::ReadWrite;
    Data::RuntimeResourceCatalog outputCatalog = fixture.catalog;
    outputCatalog.resources.first().direction = outputBinding.direction;
    outputCatalog.resources.first().access = outputBinding.access;
    const SemanticRuntimeReadValidation outputRead
        = validateSemanticRuntimeRead(outputBinding, outputCatalog, fixture.snapshot);
    QVERIFY(outputRead.validation.accepted());
    QVERIFY(outputRead.sample);

    Data::SemanticRuntimeBinding writeOnlyBinding = outputBinding;
    writeOnlyBinding.access = Data::RuntimeResourceAccess::WriteOnly;
    Data::RuntimeResourceCatalog writeOnlyCatalog = outputCatalog;
    writeOnlyCatalog.resources.first().access = writeOnlyBinding.access;
    QCOMPARE(
        validateSemanticRuntimeRead(
            writeOnlyBinding, writeOnlyCatalog, fixture.snapshot)
            .validation.error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::RuntimeResourceSnapshot incomplete = fixture.snapshot;
    incomplete.complete = false;
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, incomplete).validation.error,
        SemanticRuntimeValidationError::SnapshotIncomplete);

    Data::RuntimeResourceSnapshot stale = fixture.snapshot;
    stale.samples.first().quality.state = Data::RuntimeResourceQualityState::Stale;
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, stale).validation.error,
        SemanticRuntimeValidationError::SampleQualityNotGood);

    Data::RuntimeResourceSnapshot changedEpoch = fixture.snapshot;
    ++changedEpoch.epoch.runtimeGeneration;
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, changedEpoch).validation.error,
        SemanticRuntimeValidationError::EpochMismatch);

    // A descriptor with the same presentation and PI coordinates is never a fallback for the
    // exact verified ResourceId.
    Data::RuntimeResourceCatalog lookalikeCatalog = fixture.catalog;
    lookalikeCatalog.resources.first().id = {QByteArray::fromHex("1000000000000002")};
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, lookalikeCatalog, fixture.snapshot)
            .validation.error,
        SemanticRuntimeValidationError::ResourceNotFound);

    Data::RuntimeResourceSnapshot duplicate = fixture.snapshot;
    duplicate.samples.append(duplicate.samples.constFirst());
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, duplicate).validation.error,
        SemanticRuntimeValidationError::SampleAmbiguous);

    Data::RuntimeResourceCatalog mismatchedDescriptor = fixture.catalog;
    mismatchedDescriptor.resources.first().consistencyGroupId = {
        QByteArray::fromHex("3000000000000002")};
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, mismatchedDescriptor, fixture.snapshot)
            .validation.error,
        SemanticRuntimeValidationError::DescriptorMismatch);

    Data::RuntimeResourceSnapshot mismatchedValueType = fixture.snapshot;
    mismatchedValueType.samples.first().value.value = QVariant::fromValue<qulonglong>(1);
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, mismatchedValueType)
            .validation.error,
        SemanticRuntimeValidationError::SampleValueMismatch);

    Data::SemanticRuntimeBinding mismatchedBitWidth = fixture.binding;
    mismatchedBitWidth.bitWidth = 8;
    mismatchedBitWidth.valueTypeIdentity = "ethercat.runtime.value/primitive-1/bits-8";
    Data::RuntimeResourceCatalog mismatchedBitWidthCatalog = fixture.catalog;
    mismatchedBitWidthCatalog.resources.first().bitWidth = mismatchedBitWidth.bitWidth;
    mismatchedBitWidthCatalog.resources.first().valueTypeIdentity
        = mismatchedBitWidth.valueTypeIdentity;
    Data::RuntimeResourceSnapshot mismatchedBitWidthSnapshot = fixture.snapshot;
    mismatchedBitWidthSnapshot.samples.first().value.typeIdentity
        = mismatchedBitWidth.valueTypeIdentity;
    QCOMPARE(
        validateSemanticRuntimeRead(
            mismatchedBitWidth, mismatchedBitWidthCatalog, mismatchedBitWidthSnapshot)
            .validation.error,
        SemanticRuntimeValidationError::SampleValueMismatch);
}

void EtherCATCoreTests::testSemanticRuntimeOperationContract()
{
    using SubmitMethod = Data::SemanticOperationRecord (SemanticRuntimeService::*)(
        const Data::SemanticOperationRequest &, const Data::SemanticRuntimeActor &);
    using ApproveMethod = Data::SemanticOperationRecord (SemanticRuntimeService::*)(
        const Data::SemanticOperationApprovalRequest &, const Data::SemanticRuntimeActor &);
    static_assert(std::is_same_v<decltype(&SemanticRuntimeService::submit), SubmitMethod>);
    static_assert(std::is_same_v<decltype(&SemanticRuntimeService::approve), ApproveMethod>);

    const SemanticRuntimeFixture fixture;

    QVERIFY(isCanonicalSemanticOperationId(fixture.request.operationId));
    QVERIFY(!canonicalSemanticOperationRequest(fixture.request).isEmpty());
    QVERIFY(validateSemanticOperationRequest(fixture.request, fixture.context).accepted());

    const Data::SemanticOperationRequest requestCopy = fixture.request;
    QVERIFY(semanticOperationRequestsCanonicallyEqual(fixture.request, requestCopy));
    QCOMPARE(
        canonicalSemanticOperationRequest(fixture.request),
        canonicalSemanticOperationRequest(requestCopy));

    Data::SemanticOperationRequest differentId = fixture.request;
    differentId.operationId.value = "gateway-operation-002";
    QVERIFY(semanticOperationRequestsCanonicallyEqual(fixture.request, differentId));

    Data::SemanticOperationRequest changed = fixture.request;
    changed.value = false;
    QVERIFY(!semanticOperationRequestsCanonicallyEqual(fixture.request, changed));

    Data::SemanticOperationRequest gatewayStyleId = fixture.request;
    gatewayStyleId.operationId.value = "read:controller/device-17";
    QVERIFY(isCanonicalSemanticOperationId(gatewayStyleId.operationId));
    QVERIFY(!canonicalSemanticOperationRequest(gatewayStyleId).isEmpty());

    Data::SemanticOperationRequest invalidId = fixture.request;
    invalidId.operationId.value = "line-one\nline-two";
    QVERIFY(!isCanonicalSemanticOperationId(invalidId.operationId));
    QVERIFY(canonicalSemanticOperationRequest(invalidId).isEmpty());
    QCOMPARE(
        validateSemanticOperationRequest(invalidId, fixture.context).error,
        SemanticRuntimeValidationError::InvalidOperationId);

    invalidId.operationId.value = QString(129, 'x');
    QVERIFY(!isCanonicalSemanticOperationId(invalidId.operationId));

    Data::SemanticOperationRequest staleEpoch = fixture.request;
    ++staleEpoch.expectedEpoch.catalogRevision;
    QCOMPARE(
        validateSemanticOperationRequest(staleEpoch, fixture.context).error,
        SemanticRuntimeValidationError::EpochMismatch);

    Data::SemanticOperationRequest staleDigest = fixture.request;
    staleDigest.expectedMappingDigest.value[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(staleDigest, fixture.context).error,
        SemanticRuntimeValidationError::MappingDigestMismatch);

    Data::SemanticOperationRequest staleContext = fixture.request;
    staleContext.expectedContextHash[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(staleContext, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest invalidKind = fixture.request;
    invalidKind.kind = Data::SemanticOperationKind::InvokeAction;
    QCOMPARE(
        validateSemanticOperationRequest(invalidKind, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest releaseHold = fixture.request;
    releaseHold.kind = Data::SemanticOperationKind::ReleaseHold;
    releaseHold.value = {};
    releaseHold.parameters.clear();
    releaseHold.ttlMs = 0;
    QCOMPARE(
        validateSemanticOperationRequest(releaseHold, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest resourceInjection = fixture.request;
    resourceInjection.value = QVariant::fromValue(fixture.binding.resourceId);
    QVERIFY(!isAllowedSemanticRuntimeValue(resourceInjection.value));
    QVERIFY(canonicalSemanticOperationRequest(resourceInjection).isEmpty());
    QCOMPARE(
        validateSemanticOperationRequest(resourceInjection, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest wrongBooleanType = fixture.request;
    wrongBooleanType.value = QVariant::fromValue<qulonglong>(1);
    QVERIFY(!isSemanticRuntimeValueCompatible(wrongBooleanType.value, fixture.binding));
    QCOMPARE(
        validateSemanticOperationRequest(wrongBooleanType, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticRuntimeBinding signedEight = fixture.binding;
    signedEight.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    signedEight.bitWidth = 8;
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant::fromValue<qlonglong>(-128), signedEight));
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant::fromValue<qlonglong>(127), signedEight));
    QVERIFY(!isSemanticRuntimeValueCompatible(QVariant::fromValue<qlonglong>(128), signedEight));

    Data::SemanticRuntimeBinding unsignedEight = fixture.binding;
    unsignedEight.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    unsignedEight.bitWidth = 8;
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant::fromValue<qulonglong>(255), unsignedEight));
    QVERIFY(!isSemanticRuntimeValueCompatible(QVariant::fromValue<qulonglong>(256), unsignedEight));

    Data::SemanticRuntimeBinding fixedPoint = fixture.binding;
    fixedPoint.primitiveType = Data::RuntimeResourcePrimitiveType::FloatingPoint;
    fixedPoint.bitWidth = 64;
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant(1.25), fixedPoint));
    QVERIFY(!isAllowedSemanticRuntimeValue(QVariant(std::numeric_limits<double>::quiet_NaN())));
    QVERIFY(!isSemanticRuntimeValueCompatible(
        QVariant(std::numeric_limits<double>::infinity()), fixedPoint));

    Data::SemanticRuntimeBinding rawBits = fixture.binding;
    rawBits.primitiveType = Data::RuntimeResourcePrimitiveType::ByteArray;
    rawBits.bitWidth = 9;
    QVERIFY(isSemanticRuntimeValueCompatible(QByteArray(2, '\0'), rawBits));
    QVERIFY(!isSemanticRuntimeValueCompatible(QByteArray(1, '\0'), rawBits));

    Data::SemanticRuntimeContext incomplete = fixture.context;
    incomplete.complete = false;
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, incomplete).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext verifierMissing = fixture.context;
    verifierMissing.bindingVerification.verifierId.clear();
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, verifierMissing).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext definitionMismatch = fixture.context;
    definitionMismatch.signalStates.first().definition.id = {"urn:example.test:signal/another"};
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, definitionMismatch).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeContext bindingTargetMismatch = fixture.context;
    bindingTargetMismatch.signalStates.first().binding->target.signalId = {
        "urn:example.test:signal/another"};
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, bindingTargetMismatch).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticRuntimeContext duplicateSignal = fixture.context;
    duplicateSignal.signalStates.append(duplicateSignal.signalStates.constFirst());
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, duplicateSignal).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeContext verifierMismatch = fixture.context;
    verifierMismatch.signalStates.first().binding->verification.verifierId = "other-verifier";
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, verifierMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext manifestMismatch = fixture.context;
    manifestMismatch.signalStates.first().binding->verification.signedManifestDigest.value[0]
        ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, manifestMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext proofRecordMismatch = fixture.context;
    proofRecordMismatch.signalStates.first().binding->verification.detail = "different proof";
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, proofRecordMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticActionRuntimeState action;
    action.target = fixture.target;
    action.target.kind = Data::SemanticRuntimeTargetKind::Action;
    action.target.signalId = {};
    action.target.actionId = {"urn:example.test:action/manual"};
    action.definition.id = action.target.actionId;
    action.definition.enabled = true;
    action.actionBindingId = action.target.actionId.value;
    action.actionDefinitionId = "urn:example.test:action-definition/manual";
    action.actionDefinitionDigest = {"sha256", QByteArray(32, '\x72')};
    action.qualification = Data::SemanticActionQualification::Qualified;
    action.availability = Data::SemanticActionAvailability::Ready;
    action.bindings = {fixture.binding};
    action.requiresApproval = true;
    action.requiresExclusiveControl = true;
    action.holdToRun = true;
    action.maximumTtlMs = 250;
    action.maximumTtlCycles = 2000;
    Data::SemanticActionParameterRuntimeDefinition actionParameter;
    actionParameter.id = "enable";
    actionParameter.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
    actionParameter.minimum = false;
    actionParameter.maximum = true;
    action.parameters = {actionParameter};

    Data::SemanticOperationRequest actionRequest = fixture.request;
    actionRequest.kind = Data::SemanticOperationKind::InvokeAction;
    actionRequest.target = action.target;
    actionRequest.value = {};
    actionRequest.ttlMs = 0;
    actionRequest.ttlCycles = 1000;
    actionRequest.expectedActionDefinitionDigest = action.actionDefinitionDigest;
    actionRequest.parameters = {{"enable", true}};

    Data::SemanticRuntimeContext actionContext = fixture.context;
    actionContext.actionDefinitionsDigest = {"sha256", QByteArray(32, '\x73')};
    actionContext.cyclePeriodNs = 125000;
    actionContext.actionStates = {action};
    QVERIFY(validateSemanticOperationRequest(actionRequest, actionContext).accepted());

    Data::SemanticRuntimeContext missingActionEvidence = actionContext;
    missingActionEvidence.actionDefinitionsDigest = {};
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, missingActionEvidence).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticOperationRequest missingActionParameter = actionRequest;
    missingActionParameter.parameters.clear();
    QCOMPARE(
        validateSemanticOperationRequest(missingActionParameter, actionContext).error,
        SemanticRuntimeValidationError::InvalidRequest);
    Data::SemanticOperationRequest extraActionParameter = actionRequest;
    extraActionParameter.parameters.insert("unexpected", true);
    QCOMPARE(
        validateSemanticOperationRequest(extraActionParameter, actionContext).error,
        SemanticRuntimeValidationError::InvalidRequest);
    Data::SemanticOperationRequest wrongActionParameterType = actionRequest;
    wrongActionParameterType.parameters["enable"] = QVariant::fromValue<qulonglong>(1);
    QCOMPARE(
        validateSemanticOperationRequest(wrongActionParameterType, actionContext).error,
        SemanticRuntimeValidationError::InvalidRequest);
    Data::SemanticOperationRequest excessiveActionTtl = actionRequest;
    excessiveActionTtl.ttlCycles = action.maximumTtlCycles + 1;
    QCOMPARE(
        validateSemanticOperationRequest(excessiveActionTtl, actionContext).error,
        SemanticRuntimeValidationError::InvalidRequest);
    Data::SemanticOperationRequest changedActionDigest = actionRequest;
    changedActionDigest.expectedActionDefinitionDigest.value[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(changedActionDigest, actionContext).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeBinding secondBinding = fixture.binding;
    secondBinding.target.signalId = {"urn:example.test:signal/velocity"};
    secondBinding.semanticBindingId = "binding:test:velocity";
    secondBinding.resourceId = {QByteArray::fromHex("1000000000000002")};
    secondBinding.consistencyGroupId = {QByteArray::fromHex("3000000000000002")};
    secondBinding.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    secondBinding.valueTypeIdentity = "ethercat.runtime.value/primitive-7/bits-32";
    secondBinding.bitWidth = 32;
    actionContext.actionStates.first().bindings.append(secondBinding);
    QVERIFY(validateSemanticOperationRequest(actionRequest, actionContext).accepted());

    Data::SemanticRuntimeContext actionDefinitionMismatch = actionContext;
    actionDefinitionMismatch.actionStates.first().definition.id = {
        "urn:example.test:action/another"};
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, actionDefinitionMismatch).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeContext actionProofMismatch = actionContext;
    actionProofMismatch.actionStates.first().bindings[1].verification.verifierId = "other-verifier";
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, actionProofMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext actionBindingTargetMismatch = actionContext;
    actionBindingTargetMismatch.actionStates.first().bindings[1].target.deviceId
        = Data::NodeId::create();
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, actionBindingTargetMismatch).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticRuntimeContext duplicateActionBinding = actionContext;
    duplicateActionBinding.actionStates = {action};
    duplicateActionBinding.actionStates.first().bindings.append(fixture.binding);
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, duplicateActionBinding).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticOperationRecord approvalOperation;
    approvalOperation.request = fixture.request;
    approvalOperation.actor = fixture.actor;
    approvalOperation.state = Data::SemanticOperationState::ApprovalRequired;
    approvalOperation.canonicalRequestDigest = QCryptographicHash::hash(
        canonicalSemanticOperationRequest(approvalOperation.request), QCryptographicHash::Sha256);
    approvalOperation.approvalChallenge = QByteArray(32, '\x2a');

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = fixture.request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = approvalOperation.approvalChallenge;
    approval.expectedRequestDigest = approvalOperation.canonicalRequestDigest;
    approval.expectedContextHash = fixture.context.contextHash;
    QVERIFY(
        validateSemanticOperationApproval(approval, fixture.actor, approvalOperation, fixture.context)
            .accepted());

    Data::SemanticRuntimeActor automation = fixture.actor;
    automation.kind = Data::SemanticRuntimeActorKind::Automation;
    QCOMPARE(
        validateSemanticOperationApproval(approval, automation, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::ApprovalActorInvalid);

    Data::SemanticOperationApprovalRequest wrongChallenge = approval;
    wrongChallenge.challenge[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationApproval(
            wrongChallenge, fixture.actor, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::ApprovalChallengeMismatch);

    Data::SemanticOperationApprovalRequest wrongRequestDigest = approval;
    wrongRequestDigest.expectedRequestDigest[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationApproval(
            wrongRequestDigest, fixture.actor, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::ApprovalChallengeMismatch);

    Data::SemanticOperationRecord changedApprovedRequest = approvalOperation;
    changedApprovedRequest.request.ttlMs += 1;
    QCOMPARE(
        validateSemanticOperationApproval(
            approval, fixture.actor, changedApprovedRequest, fixture.context)
            .error,
        SemanticRuntimeValidationError::ApprovalChallengeMismatch);

    Data::SemanticOperationApprovalRequest pending = approval;
    pending.decision = Data::SemanticApprovalDecision::Pending;
    QCOMPARE(
        validateSemanticOperationApproval(pending, fixture.actor, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::InvalidRequest);
}

void EtherCATCoreTests::testSemanticRuntimeServiceFailsClosed()
{
    using RefreshMethod = Data::SemanticLiveRefreshResult (SemanticRuntimeService::*)(
        const Data::SemanticLiveRefreshRequest &);
    static_assert(
        std::is_same_v<decltype(&SemanticRuntimeService::requestLiveRefresh), RefreshMethod>);

    SemanticRuntimeFixture fixture;
    TestSemanticRuntimeService service;
    service.snapshots = {fixture.context};

    const std::optional<Data::SemanticRuntimeContext> context = service.context(
        fixture.context.controllerId);
    QVERIFY(context);
    QCOMPARE(*context, fixture.context);
    QCOMPARE(service.readCount, 1);
    QVERIFY(!service.context("missing"));
    QCOMPARE(service.readCount, 2);

    QSignalSpy operationSpy(&service, &SemanticRuntimeService::operationChanged);
    QSignalSpy auditSpy(&service, &SemanticRuntimeService::auditChanged);
    QSignalSpy refreshSpy(&service, &SemanticRuntimeService::liveRefreshCompleted);

    Data::SemanticLiveRefreshRequest refreshRequest;
    refreshRequest.controllerId = fixture.context.controllerId;
    refreshRequest.scope = fixture.context.scope;
    refreshRequest.targets = {{fixture.deviceId, fixture.target.signalId}};
    refreshRequest.expectedContextHash = fixture.context.contextHash;
    refreshRequest.correlationId = QStringLiteral("semantic-refresh-fail-closed");
    QVERIFY(refreshRequest.isValid());
    const Data::SemanticLiveRefreshResult refreshResult = service.requestLiveRefresh(refreshRequest);
    QVERIFY(refreshResult.isValid());
    QCOMPARE(refreshResult.correlationId, refreshRequest.correlationId);
    QCOMPARE(refreshResult.outcome, Data::SemanticLiveRefreshOutcome::Unsupported);
    QCOMPARE(refreshResult.code, QStringLiteral("semantic-live-refresh-unsupported"));
    QCOMPARE(refreshResult.detail, QStringLiteral("Semantic live refresh is unsupported."));
    QCOMPARE(refreshResult.captureCycle, quint64(0));
    QCOMPARE(refreshSpy.count(), 0);

    const Data::SemanticOperationRecord submission = service.submit(fixture.request, fixture.actor);
    QCOMPARE(submission.state, Data::SemanticOperationState::Rejected);
    QCOMPARE(submission.resultCode, "semantic-runtime-unavailable");
    QCOMPARE(submission.actor, fixture.actor);
    QVERIFY(!submission.canonicalRequestDigest.isEmpty());
    QVERIFY(!submission.executionAttempted);
    QCOMPARE(submission.appliedCycle, quint64(0));
    QCOMPARE(submission.appliedRuntimeGeneration, quint64(0));
    QVERIFY(!service.operation(fixture.request.operationId));
    QVERIFY(service.audit(fixture.context.controllerId).isEmpty());

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = fixture.request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = QByteArray(32, '\x2a');
    approval.expectedContextHash = fixture.context.contextHash;
    const Data::SemanticOperationRecord approvalResult = service.approve(approval, fixture.actor);
    QCOMPARE(approvalResult.state, Data::SemanticOperationState::Rejected);
    QCOMPARE(approvalResult.resultCode, "semantic-runtime-unavailable");
    QVERIFY(!approvalResult.executionAttempted);
    QCOMPARE(approvalResult.actor, fixture.actor);
    QCOMPARE(approvalResult.approvals.size(), 1);
    QCOMPARE(approvalResult.approvals.constFirst().request, approval);
    QCOMPARE(approvalResult.approvals.constFirst().actor, fixture.actor);

    QCOMPARE(operationSpy.count(), 0);
    QCOMPARE(auditSpy.count(), 0);
    QCOMPARE(refreshSpy.count(), 0);
}

void EtherCATCoreTests::testExactEngineeringRationalContract()
{
    const ExactRationalResult half = normalizedExactRational(6, 12);
    QVERIFY(half.validation.accepted());
    QCOMPARE(half.value, std::optional<Data::ExactRational>({1, 2}));
    QCOMPARE(normalizedExactRational(0, 999).value, std::optional<Data::ExactRational>({0, 1}));

    const ExactRationalResult invalidDenominator = normalizedExactRational(1, 0);
    QCOMPARE(invalidDenominator.validation.error, EngineeringContractError::InvalidRational);
    QVERIFY(!invalidDenominator.value);
    QCOMPARE(validateExactRational({2, 4}).error, EngineeringContractError::InvalidRational);
    QCOMPARE(validateExactRational({0, 2}).error, EngineeringContractError::InvalidRational);
    QCOMPARE(validateExactRational({1, -2}).error, EngineeringContractError::InvalidRational);
    QVERIFY(validateExactRational({std::numeric_limits<qint64>::min(), 1}).accepted());

    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromBoolean(true)).accepted());
    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromSignedInteger(-17)).accepted());
    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromUnsignedInteger(17)).accepted());
    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromExactRational({-3, 7})).accepted());
    QVERIFY(
        validateEngineeringValue(Data::EngineeringValue::fromEnumeration("mode.ready")).accepted());

    Data::EngineeringValue polluted = Data::EngineeringValue::fromSignedInteger(7);
    polluted.unsignedInteger = 1;
    QCOMPARE(validateEngineeringValue(polluted).error, EngineeringContractError::InvalidValue);
}

void EtherCATCoreTests::testExactEngineeringConversionContract()
{
    Data::EngineeringTransform transform;
    transform.scale = {1, 10};
    transform.offset = {-1, 1};
    transform.rounding = Data::EngineeringRounding::RejectInexact;
    transform.constraint.minimum = {-100, 1};
    transform.constraint.maximum = {100, 1};

    const EngineeringConversionResult engineering
        = convertRawToEngineering(Data::EngineeringValue::fromSignedInteger(20), 16, transform);
    QVERIFY(engineering.validation.accepted());
    QCOMPARE(
        engineering.value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromExactRational({1, 1})));

    const EngineeringConversionResult raw = convertEngineeringToRaw(
        Data::EngineeringValue::fromExactRational({3, 2}),
        Data::EngineeringValueKind::SignedInteger,
        16,
        transform);
    QVERIFY(raw.validation.accepted());
    QCOMPARE(
        raw.value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(25)));

    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({1, 3}),
            Data::EngineeringValueKind::SignedInteger,
            16,
            transform)
            .validation.error,
        EngineeringContractError::InexactConversion);

    Data::EngineeringTransform rounded;
    rounded.scale = {1, 1};
    rounded.offset = {0, 1};
    rounded.rounding = Data::EngineeringRounding::NearestTiesToEven;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({5, 2}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            rounded)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(2)));
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({7, 2}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            rounded)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(4)));
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({-5, 2}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            rounded)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(-2)));

    Data::EngineeringTransform towardZero = rounded;
    towardZero.rounding = Data::EngineeringRounding::TowardZero;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({-7, 3}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            towardZero)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(-2)));
    Data::EngineeringTransform towardNegative = rounded;
    towardNegative.rounding = Data::EngineeringRounding::TowardNegativeInfinity;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({-7, 3}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            towardNegative)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(-3)));
    Data::EngineeringTransform towardPositive = rounded;
    towardPositive.rounding = Data::EngineeringRounding::TowardPositiveInfinity;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({7, 3}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            towardPositive)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(3)));

    QVERIFY(convertRawToEngineering(Data::EngineeringValue::fromSignedInteger(127), 8, rounded)
                .validation.accepted());
    QCOMPARE(
        convertRawToEngineering(Data::EngineeringValue::fromSignedInteger(128), 8, rounded)
            .validation.error,
        EngineeringContractError::RawValueOutOfRange);
    QVERIFY(convertRawToEngineering(Data::EngineeringValue::fromUnsignedInteger(255), 8, rounded)
                .validation.accepted());
    QCOMPARE(
        convertRawToEngineering(Data::EngineeringValue::fromUnsignedInteger(256), 8, rounded)
            .validation.error,
        EngineeringContractError::RawValueOutOfRange);
    QCOMPARE(
        convertRawToEngineering(
            Data::EngineeringValue::fromUnsignedInteger(std::numeric_limits<quint64>::max()),
            64,
            rounded)
            .validation.error,
        EngineeringContractError::ArithmeticOverflow);

    Data::EngineeringTransform booleanTransform = rounded;
    const EngineeringConversionResult booleanEngineering
        = convertRawToEngineering(Data::EngineeringValue::fromBoolean(true), 1, booleanTransform);
    QCOMPARE(
        booleanEngineering.value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromBoolean(true)));
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromBoolean(false),
            Data::EngineeringValueKind::Boolean,
            1,
            booleanTransform)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromBoolean(false)));

    Data::EngineeringTransform missingRounding = rounded;
    missingRounding.rounding.reset();
    QCOMPARE(
        validateEngineeringTransform(missingRounding).error,
        EngineeringContractError::InvalidTransform);
    Data::EngineeringTransform zeroScale = rounded;
    zeroScale.scale = {0, 1};
    QCOMPARE(validateEngineeringTransform(zeroScale).error, EngineeringContractError::ScaleIsZero);
}

void EtherCATCoreTests::testExactEngineeringConstraintContract()
{
    Data::EngineeringConstraint stepped;
    stepped.minimum = {-2, 1};
    stepped.maximum = {2, 1};
    stepped.step = {1, 2};
    stepped.stepOrigin = {0, 1};
    QVERIFY(validateEngineeringConstraint(stepped).accepted());
    QVERIFY(validateEngineeringValueAgainstConstraint(
                Data::EngineeringValue::fromExactRational({3, 2}), stepped)
                .accepted());
    QCOMPARE(
        validateEngineeringValueAgainstConstraint(
            Data::EngineeringValue::fromExactRational({1, 4}), stepped)
            .error,
        EngineeringContractError::ConstraintStepViolation);
    QCOMPARE(
        validateEngineeringValueAgainstConstraint(Data::EngineeringValue::fromSignedInteger(3), stepped)
            .error,
        EngineeringContractError::ConstraintRangeViolation);

    Data::EngineeringConstraint enumeration;
    enumeration.minimum = {0, 1};
    enumeration.maximum = {1, 1};
    enumeration.enumeration = {
        {"mode.off", "Off", {0, 1}},
        {"mode.on", "On", {1, 1}},
    };
    QVERIFY(validateEngineeringConstraint(enumeration).accepted());
    QVERIFY(validateEngineeringValueAgainstConstraint(
                Data::EngineeringValue::fromEnumeration("mode.on"), enumeration)
                .accepted());
    QCOMPARE(
        validateEngineeringValueAgainstConstraint(
            Data::EngineeringValue::fromEnumeration("mode.unknown"), enumeration)
            .error,
        EngineeringContractError::ConstraintEnumerationViolation);

    Data::EngineeringConstraint unsorted = enumeration;
    std::reverse(unsorted.enumeration.begin(), unsorted.enumeration.end());
    QCOMPARE(
        validateEngineeringConstraint(unsorted).error, EngineeringContractError::InvalidConstraint);
    Data::EngineeringConstraint missingOrigin;
    missingOrigin.step = {1, 1};
    QCOMPARE(
        validateEngineeringConstraint(missingOrigin).error,
        EngineeringContractError::InvalidConstraint);
}

void EtherCATCoreTests::testManualControlEnvelopeContract()
{
    Data::EngineeringTransform signalTransform;
    signalTransform.scale = {1, 1};
    signalTransform.offset = {0, 1};
    signalTransform.rounding = Data::EngineeringRounding::RejectInexact;
    signalTransform.constraint.minimum = {0, 1};
    signalTransform.constraint.maximum = {1, 1};
    signalTransform.constraint.step = {1, 1};
    signalTransform.constraint.stepOrigin = {0, 1};

    Data::SemanticSignalDefinition signalDefinition;
    signalDefinition.id = {"urn:test:signal/output"};
    signalDefinition.direction = Data::SemanticSignalDirection::Output;
    signalDefinition.access = Data::SemanticSignalAccess::WriteOnly;
    signalDefinition.exposure = Data::SemanticSignalExposure::Public;
    signalDefinition.engineeringTransform = signalTransform;

    Data::EngineeringConstraint speedConstraint;
    speedConstraint.minimum = {-100, 1};
    speedConstraint.maximum = {100, 1};
    speedConstraint.step = {1, 1};
    speedConstraint.stepOrigin = {0, 1};

    Data::SemanticSignalDefinition speedSignalDefinition;
    speedSignalDefinition.id = {"urn:test:signal/target-speed"};
    speedSignalDefinition.direction = Data::SemanticSignalDirection::Output;
    speedSignalDefinition.access = Data::SemanticSignalAccess::WriteOnly;
    speedSignalDefinition.exposure = Data::SemanticSignalExposure::ActionOnly;
    Data::EngineeringTransform speedTransform;
    speedTransform.scale = {1, 1};
    speedTransform.offset = {0, 1};
    speedTransform.rounding = Data::EngineeringRounding::RejectInexact;
    speedTransform.constraint = speedConstraint;
    speedSignalDefinition.engineeringTransform = speedTransform;
    QList<Data::SemanticSignalDefinition> signalDefinitions{
        signalDefinition,
        speedSignalDefinition,
    };

    const auto terminalAction = [&signalDefinition](const QString &id) {
        Data::DeviceControlAction action;
        action.id = {id};
        action.enabled = true;
        action.holdToRun = false;
        Data::DeviceControlStep step;
        step.kind = Data::DeviceControlStepKind::WriteSignal;
        step.signalId = signalDefinition.id;
        step.value.source = Data::DeviceControlValueSource::Literal;
        step.value.engineeringLiteralValue = Data::EngineeringValue::fromSignedInteger(0);
        action.steps = {step};
        return action;
    };
    Data::DeviceControlAction mainAction;
    mainAction.id = {"urn:test:action/move"};
    mainAction.enabled = true;
    mainAction.holdToRun = true;
    mainAction.allowedReleaseActionIds = {{"urn:test:action/release"}};
    mainAction.allowedTimeoutActionIds = {{"urn:test:action/timeout"}};
    mainAction.allowedFailureActionIds = {{"urn:test:action/fail"}};
    Data::DeviceControlActionParameter speedParameter;
    speedParameter.id = "speed";
    speedParameter.required = true;
    speedParameter.engineeringConstraint = speedConstraint;
    mainAction.parameters = {speedParameter};
    Data::DeviceControlStep mainStep;
    mainStep.kind = Data::DeviceControlStepKind::WriteSignal;
    mainStep.signalId = speedSignalDefinition.id;
    mainStep.value.source = Data::DeviceControlValueSource::Parameter;
    mainStep.value.parameterId = speedParameter.id;
    mainAction.steps = {mainStep};

    QList<Data::DeviceControlAction> actionDefinitions{
        mainAction,
        terminalAction("urn:test:action/fail"),
        terminalAction("urn:test:action/release"),
        terminalAction("urn:test:action/timeout"),
    };

    Data::ManualSignalEnvelope signal;
    signal.signalId = signalDefinition.id;
    signal.enabled = true;
    signal.holdToRun = false;
    signal.timing.commandTtlMs = 100;
    signal.allowedRange = signalTransform.constraint;
    signal.safeValue = Data::EngineeringValue::fromSignedInteger(0);

    Data::ManualActionParameterEnvelope parameter;
    parameter.parameterId = "speed";
    parameter.allowedRange = speedConstraint;
    parameter.defaultValue = Data::EngineeringValue::fromSignedInteger(0);

    Data::ManualActionEnvelope action;
    action.actionId = mainAction.id;
    action.enabled = true;
    action.holdToRun = true;
    action.timing.commandTtlMs = 100;
    action.timing.refreshTimeoutMs = 250;
    action.timing.maxContinuousHoldMs = 5000;
    action.parameters = {parameter};
    action.releaseActionId = {"urn:test:action/release"};
    action.timeoutActionId = {"urn:test:action/timeout"};
    action.failureActionId = {"urn:test:action/fail"};

    Data::ManualControlEnvelope envelope;
    envelope.enabled = true;
    envelope.signalEnvelopes = {signal};
    envelope.actionEnvelopes = {action};
    QVERIFY(
        validateManualControlEnvelope(envelope, signalDefinitions, actionDefinitions).accepted());

    QVERIFY(QMetaType::fromType<Data::ExactRational>().isValid());
    QVERIFY(QMetaType::fromType<Data::EngineeringTransform>().isValid());
    QVERIFY(QMetaType::fromType<Data::ManualControlEnvelope>().isValid());
    QVERIFY(QMetaType::fromType<EngineeringConversionResult>().isValid());
    QVERIFY(QMetaType::fromType<ManualControlContractValidation>().isValid());

    Data::SemanticSignalDefinition legacySignal = signalDefinition;
    legacySignal.engineeringTransform.reset();
    QList<Data::SemanticSignalDefinition> legacySignals = signalDefinitions;
    legacySignals[0] = legacySignal;
    QCOMPARE(
        validateManualControlEnvelope(envelope, legacySignals, actionDefinitions).error,
        ManualControlContractError::ExactTransformMissing);

    Data::SemanticSignalDefinition actionOnly = signalDefinition;
    actionOnly.exposure = Data::SemanticSignalExposure::ActionOnly;
    QList<Data::SemanticSignalDefinition> actionOnlySignals = signalDefinitions;
    actionOnlySignals[0] = actionOnly;
    QCOMPARE(
        validateManualControlEnvelope(envelope, actionOnlySignals, actionDefinitions).error,
        ManualControlContractError::SignalNotPublic);

    Data::ManualControlEnvelope noTtl = envelope;
    noTtl.signalEnvelopes[0].timing.commandTtlMs = 0;
    QCOMPARE(
        validateManualControlEnvelope(noTtl, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::InvalidTiming);

    Data::ManualControlEnvelope duplicateSignal = envelope;
    duplicateSignal.signalEnvelopes.append(signal);
    QCOMPARE(
        validateManualControlEnvelope(duplicateSignal, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::DuplicateIdentifier);

    Data::ManualControlEnvelope broaderRange = envelope;
    broaderRange.signalEnvelopes[0].allowedRange.minimum.reset();
    QCOMPARE(
        validateManualControlEnvelope(broaderRange, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::ConstraintNotSubset);

    Data::ManualControlEnvelope unsignedEnumeration = envelope;
    unsignedEnumeration.signalEnvelopes[0].allowedRange.enumeration = {
        {"project.custom", "Custom", {0, 1}},
    };
    QCOMPARE(
        validateManualControlEnvelope(unsignedEnumeration, signalDefinitions, actionDefinitions)
            .error,
        ManualControlContractError::ConstraintNotSubset);

    Data::SemanticSignalDefinition alphabeticSignal = signalDefinition;
    alphabeticSignal.id = {"urn:test:signal/alpha"};
    Data::ManualSignalEnvelope alphabeticEnvelope = signal;
    alphabeticEnvelope.signalId = alphabeticSignal.id;
    alphabeticEnvelope.enabled = false;
    Data::ManualControlEnvelope unsortedSignals = envelope;
    unsortedSignals.signalEnvelopes.append(alphabeticEnvelope);
    QList<Data::SemanticSignalDefinition> definitionsWithAlpha = signalDefinitions;
    definitionsWithAlpha.append(alphabeticSignal);
    QCOMPARE(
        validateManualControlEnvelope(unsortedSignals, definitionsWithAlpha, actionDefinitions).error,
        ManualControlContractError::NonCanonicalIdentifierOrder);

    Data::ManualControlEnvelope unsortedActions = envelope;
    Data::ManualActionEnvelope alphabeticAction;
    alphabeticAction.actionId = {"urn:test:action/alpha"};
    unsortedActions.actionEnvelopes.append(alphabeticAction);
    QCOMPARE(
        validateManualControlEnvelope(unsortedActions, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::NonCanonicalIdentifierOrder);

    Data::SemanticSignalDefinition inputPeer = signalDefinition;
    inputPeer.id = {"urn:test:signal/input-peer"};
    inputPeer.direction = Data::SemanticSignalDirection::Input;
    inputPeer.access = Data::SemanticSignalAccess::ReadOnly;
    Data::ManualControlEnvelope unsafeGroup = envelope;
    unsafeGroup.signalEnvelopes[0].consistencyGroupSafeValues = {
        {inputPeer.id, Data::EngineeringValue::fromSignedInteger(0)},
    };
    QList<Data::SemanticSignalDefinition> definitionsWithInput = signalDefinitions;
    definitionsWithInput.append(inputPeer);
    QCOMPARE(
        validateManualControlEnvelope(unsafeGroup, definitionsWithInput, actionDefinitions).error,
        ManualControlContractError::InvalidSafeValue);

    Data::SemanticSignalDefinition zuluPeer = signalDefinition;
    zuluPeer.id = {"urn:test:signal/zulu-peer"};
    Data::ManualControlEnvelope unsortedGroup = envelope;
    unsortedGroup.signalEnvelopes[0].consistencyGroupSafeValues = {
        {zuluPeer.id, Data::EngineeringValue::fromSignedInteger(0)},
        {alphabeticSignal.id, Data::EngineeringValue::fromSignedInteger(0)},
    };
    QList<Data::SemanticSignalDefinition> definitionsForGroup = definitionsWithAlpha;
    definitionsForGroup.append(zuluPeer);
    QCOMPARE(
        validateManualControlEnvelope(unsortedGroup, definitionsForGroup, actionDefinitions).error,
        ManualControlContractError::InvalidSafeValue);

    Data::ManualControlEnvelope selfFallback = envelope;
    selfFallback.actionEnvelopes[0].failureActionId = mainAction.id;
    QCOMPARE(
        validateManualControlEnvelope(selfFallback, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::SelfReferentialFallback);

    QList<Data::DeviceControlAction> disabledMain = actionDefinitions;
    disabledMain[0].enabled = false;
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, disabledMain).error,
        ManualControlContractError::InexactActionDefinition);
    QList<Data::DeviceControlAction> emptyMain = actionDefinitions;
    emptyMain[0].steps.clear();
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, emptyMain).error,
        ManualControlContractError::InexactActionDefinition);
    QList<Data::DeviceControlAction> inexactLiteral = actionDefinitions;
    inexactLiteral[0].steps[0].value.source = Data::DeviceControlValueSource::Literal;
    inexactLiteral[0].steps[0].value.literalValue = 0;
    inexactLiteral[0].steps[0].value.parameterId.clear();
    inexactLiteral[0].steps[0].value.engineeringLiteralValue.reset();
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, inexactLiteral).error,
        ManualControlContractError::InexactActionDefinition);
    QList<Data::DeviceControlAction> broadParameter = actionDefinitions;
    broadParameter[0].parameters[0].engineeringConstraint->minimum = {-200, 1};
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, broadParameter).error,
        ManualControlContractError::InexactActionDefinition);

    QList<Data::DeviceControlAction> nonterminalFallbacks = actionDefinitions;
    nonterminalFallbacks[1].allowedFailureActionIds = {{"urn:test:action/timeout"}};
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, nonterminalFallbacks).error,
        ManualControlContractError::FallbackNotTerminal);

    QList<Data::DeviceControlAction> emptyFallback = actionDefinitions;
    emptyFallback[1].steps.clear();
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, emptyFallback).error,
        ManualControlContractError::FallbackNotTerminal);

    QList<Data::DeviceControlAction> fallbackMissingDefault = actionDefinitions;
    Data::DeviceControlActionParameter fallbackParameter;
    fallbackParameter.id = "required";
    fallbackParameter.required = true;
    fallbackParameter.engineeringConstraint = speedConstraint;
    fallbackMissingDefault[1].parameters = {fallbackParameter};
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, fallbackMissingDefault).error,
        ManualControlContractError::FallbackNotTerminal);

    QList<Data::DeviceControlAction> unsortedAllowList = actionDefinitions;
    unsortedAllowList[0].allowedFailureActionIds = {
        {"urn:test:action/timeout"},
        {"urn:test:action/fail"},
    };
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, unsortedAllowList).error,
        ManualControlContractError::NonCanonicalIdentifierOrder);

    QList<Data::DeviceControlAction> parameterDefinitions = actionDefinitions;
    Data::DeviceControlActionParameter alphaParameter;
    alphaParameter.id = "alpha";
    alphaParameter.required = false;
    alphaParameter.engineeringConstraint = speedConstraint;
    parameterDefinitions[0].parameters.append(alphaParameter);
    Data::ManualControlEnvelope unsortedParameters = envelope;
    Data::ManualActionParameterEnvelope alphaEnvelope;
    alphaEnvelope.parameterId = alphaParameter.id;
    alphaEnvelope.allowedRange = speedConstraint;
    unsortedParameters.actionEnvelopes[0].parameters.append(alphaEnvelope);
    QCOMPARE(
        validateManualControlEnvelope(unsortedParameters, signalDefinitions, parameterDefinitions)
            .error,
        ManualControlContractError::InvalidParameter);

    Data::SemanticSignalDefinition statusSignal = speedSignalDefinition;
    statusSignal.id = {"urn:test:signal/status"};
    statusSignal.direction = Data::SemanticSignalDirection::Input;
    statusSignal.access = Data::SemanticSignalAccess::ReadOnly;
    statusSignal.engineeringTransform->constraint.minimum = {0, 1};
    statusSignal.engineeringTransform->constraint.maximum = {65535, 1};

    Data::SemanticSignalDefinition actualSpeedSignal = speedSignalDefinition;
    actualSpeedSignal.id = {"urn:test:signal/actual-speed"};
    actualSpeedSignal.direction = Data::SemanticSignalDirection::Input;
    actualSpeedSignal.access = Data::SemanticSignalAccess::ReadOnly;
    QList<Data::SemanticSignalDefinition> signedSignals = signalDefinitions;
    signedSignals.append(actualSpeedSignal);
    signedSignals.append(statusSignal);

    Data::DeviceControlAction signedAction;
    signedAction.id = {"urn:test:action/signed-group"};
    signedAction.enabled = true;
    signedAction.holdToRun = false;
    signedAction.parameters = {speedParameter};
    Data::DeviceControlConsistencyGroup signedGroup;
    signedGroup.id = "manual_velocity";
    signedGroup.members = {speedSignalDefinition.id};
    signedGroup.recovery = Data::DeviceControlGroupRecovery::HoldSafe;
    signedGroup.maximumTtlCycles = 1000;
    signedAction.consistencyGroups = {signedGroup};

    Data::DeviceControlGroupAssignment speedAssignment;
    speedAssignment.signalId = speedSignalDefinition.id;
    speedAssignment.value.source = Data::DeviceControlValueSource::Parameter;
    speedAssignment.value.parameterId = speedParameter.id;
    Data::DeviceControlStep signedWrite;
    signedWrite.kind = Data::DeviceControlStepKind::WriteGroup;
    signedWrite.consistencyGroupId = signedGroup.id;
    signedWrite.assignments = {speedAssignment};

    Data::DeviceControlStep signedMaskedWait;
    signedMaskedWait.kind = Data::DeviceControlStepKind::WaitMaskedEquals;
    signedMaskedWait.signalId = statusSignal.id;
    signedMaskedWait.value.source = Data::DeviceControlValueSource::Literal;
    signedMaskedWait.value.engineeringLiteralValue
        = Data::EngineeringValue::fromUnsignedInteger(0x27);
    signedMaskedWait.mask.source = Data::DeviceControlValueSource::Literal;
    signedMaskedWait.mask.engineeringLiteralValue
        = Data::EngineeringValue::fromUnsignedInteger(0x6f);
    signedMaskedWait.timeoutCycles = 1000;

    Data::DeviceControlStep signedAbsoluteWait;
    signedAbsoluteWait.kind = Data::DeviceControlStepKind::WaitAbsoluteAtMost;
    signedAbsoluteWait.signalId = actualSpeedSignal.id;
    signedAbsoluteWait.value.source = Data::DeviceControlValueSource::Literal;
    signedAbsoluteWait.value.engineeringLiteralValue
        = Data::EngineeringValue::fromSignedInteger(10);
    signedAbsoluteWait.timeoutCycles = 8000;

    Data::DeviceControlStep signedCycleWait;
    signedCycleWait.kind = Data::DeviceControlStepKind::WaitCycles;
    signedCycleWait.timeoutCycles = 2;
    signedAction.steps = {
        signedWrite,
        signedMaskedWait,
        signedAbsoluteWait,
        signedCycleWait,
    };

    Data::ManualActionParameterEnvelope signedParameter;
    signedParameter.parameterId = speedParameter.id;
    signedParameter.allowedRange = speedConstraint;
    Data::ManualActionEnvelope signedActionEnvelope;
    signedActionEnvelope.actionId = signedAction.id;
    signedActionEnvelope.enabled = true;
    signedActionEnvelope.holdToRun = false;
    signedActionEnvelope.timing.commandTtlMs = 100;
    signedActionEnvelope.parameters = {signedParameter};
    Data::ManualControlEnvelope signedEnvelope;
    signedEnvelope.enabled = true;
    signedEnvelope.actionEnvelopes = {signedActionEnvelope};
    QVERIFY(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {signedAction},
            ManualControlFallbackContract::SignedControllerRecovery)
            .accepted());

    Data::DeviceControlAction partialGroup = signedAction;
    partialGroup.consistencyGroups[0].members = {
        signalDefinition.id,
        speedSignalDefinition.id,
    };
    QCOMPARE(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {partialGroup},
            ManualControlFallbackContract::SignedControllerRecovery)
            .error,
        ManualControlContractError::InexactActionDefinition);

    Data::DeviceControlAction duplicateAssignment = signedAction;
    duplicateAssignment.consistencyGroups[0].members = {
        signalDefinition.id,
        speedSignalDefinition.id,
    };
    duplicateAssignment.steps[0].assignments.append(speedAssignment);
    QCOMPARE(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {duplicateAssignment},
            ManualControlFallbackContract::SignedControllerRecovery)
            .error,
        ManualControlContractError::InexactActionDefinition);

    Data::DeviceControlAction unknownAssignment = signedAction;
    unknownAssignment.consistencyGroups[0].members[0] = {"urn:test:signal/unknown"};
    unknownAssignment.steps[0].assignments[0].signalId = {"urn:test:signal/unknown"};
    QCOMPARE(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {unknownAssignment},
            ManualControlFallbackContract::SignedControllerRecovery)
            .error,
        ManualControlContractError::InexactActionDefinition);

    Data::DeviceControlAction malformedWrite = signedAction;
    malformedWrite.steps[0].signalId = speedSignalDefinition.id;
    QCOMPARE(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {malformedWrite},
            ManualControlFallbackContract::SignedControllerRecovery)
            .error,
        ManualControlContractError::InexactActionDefinition);

    Data::DeviceControlAction legacySignedWrite = signedAction;
    legacySignedWrite.steps = {mainStep};
    QCOMPARE(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {legacySignedWrite},
            ManualControlFallbackContract::SignedControllerRecovery)
            .error,
        ManualControlContractError::InexactActionDefinition);

    Data::DeviceControlAction legacyTimedWait = signedAction;
    legacyTimedWait.steps[1].timeoutCycles = 0;
    legacyTimedWait.steps[1].timeoutMs = 100;
    QCOMPARE(
        validateManualControlEnvelope(
            signedEnvelope,
            signedSignals,
            {legacyTimedWait},
            ManualControlFallbackContract::SignedControllerRecovery)
            .error,
        ManualControlContractError::InexactActionDefinition);

    Data::ManualControlEnvelope disabled;
    QVERIFY(
        validateManualControlEnvelope(disabled, signalDefinitions, actionDefinitions).accepted());
}

void EtherCATCoreTests::testProcessDataConfigurationPreview()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::MasterToSlave, true, 3},
        {Data::NodeId::create(), 3, "Inputs", Data::SyncManagerDirection::SlaveToMaster, true, 3},
    };

    Data::PdoConfiguration outputs;
    outputs.id = Data::NodeId::create();
    outputs.index = 0x1600;
    outputs.name = "Outputs";
    outputs.direction = Data::PdoDirection::Rx;
    outputs.syncManager = 2;
    outputs.selected = true;
    outputs.mandatory = true;
    outputs.defaultSelected = true;
    outputs.entries = {
        {Data::NodeId::create(), 0x7000, 1, "Enable", 1, Data::EtherCATDataType::Boolean, "BOOL"},
        {Data::NodeId::create(),
         0x7000,
         2,
         "Target",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT"},
    };

    Data::PdoConfiguration inputs;
    inputs.id = Data::NodeId::create();
    inputs.index = 0x1a00;
    inputs.name = "Inputs";
    inputs.direction = Data::PdoDirection::Tx;
    inputs.syncManager = 3;
    inputs.selected = true;
    inputs.entries = {
        {Data::NodeId::create(),
         0x6000,
         1,
         "Status",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         8}};
    configuration.pdos = {outputs, inputs};

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    QVERIFY(!validation.hasErrors());
    QVERIFY(validation.issues.isEmpty());
    QCOMPARE(validation.processImage.outputs.bitSize, 17);
    QCOMPARE(validation.processImage.outputs.byteSize, 3);
    QCOMPARE(validation.processImage.outputs.entries.size(), 2);
    QCOMPARE(validation.processImage.outputs.entries.at(0).bitOffset, 0);
    QCOMPARE(validation.processImage.outputs.entries.at(1).bitOffset, 1);
    QCOMPARE(validation.processImage.inputs.bitSize, 24);
    QCOMPARE(validation.processImage.inputs.byteSize, 3);
    QCOMPARE(validation.processImage.inputs.entries.first().bitOffset, 8);
}

void EtherCATCoreTests::testProcessDataConfigurationValidation()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::SlaveToMaster, true, 1},
    };

    Data::PdoConfiguration first;
    first.id = Data::NodeId::create();
    first.index = 0x1600;
    first.name = "First";
    first.direction = Data::PdoDirection::Rx;
    first.syncManager = 2;
    first.selected = true;
    first.mappingSupported = false;
    first.entries = {
        {Data::NodeId::create(),
         0x7000,
         1,
         "Invalid width",
         12,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         0}};

    Data::PdoConfiguration duplicate = first;
    duplicate.id = Data::NodeId::create();
    duplicate.name = "Duplicate";
    duplicate.mappingSupported = true;
    duplicate.entries.first().id = Data::NodeId::create();
    duplicate.entries.first().bitLength = 16;
    duplicate.entries.first().requestedBitOffset = 8;

    Data::PdoConfiguration mandatory;
    mandatory.id = Data::NodeId::create();
    mandatory.index = 0x1601;
    mandatory.direction = Data::PdoDirection::Rx;
    mandatory.syncManager = 4;
    mandatory.mandatory = true;
    mandatory.selected = false;
    configuration.pdos = {first, duplicate, mandatory};

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    QVERIFY(validation.hasErrors());
    const auto hasIssue = [&validation](Data::ConfigurationIssueCode code) {
        return std::any_of(
            validation.issues.cbegin(),
            validation.issues.cend(),
            [code](const Data::ConfigurationIssue &issue) { return issue.code == code; });
    };
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::SyncManagerDirectionMismatch));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::UnsupportedPdoMapping));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DataTypeBitLengthMismatch));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DuplicatePdoAssignment));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DuplicatePdoEntry));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::ProcessImageOverlap));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::SyncManagerSizeExceeded));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::MandatoryPdoNotSelected));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::MissingSyncManager));
}

void EtherCATCoreTests::testStartupAndDcConfigurationValidation()
{
    Data::StartupConfiguration startup;
    startup.parameters = {
        {Data::NodeId::create(),
         true,
         0,
         "IP",
         0x8000,
         1,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         QByteArray::fromHex("0100"),
         "Mode"},
        {Data::NodeId::create(),
         true,
         1,
         "PS",
         0x8000,
         2,
         Data::EtherCATDataType::Boolean,
         "BOOL",
         QByteArray::fromHex("01"),
         "Enable"},
    };
    QVERIFY(Data::validateStartupConfiguration(startup).isEmpty());

    startup.parameters[1].order = 0;
    startup.parameters[1].rawValue.clear();
    const QList<Data::ConfigurationIssue> startupIssues = Data::validateStartupConfiguration(
        startup);
    const auto hasStartupIssue = [&startupIssues](Data::ConfigurationIssueCode code) {
        return std::any_of(
            startupIssues.cbegin(),
            startupIssues.cend(),
            [code](const Data::ConfigurationIssue &issue) { return issue.code == code; });
    };
    QVERIFY(hasStartupIssue(Data::ConfigurationIssueCode::DuplicateStartupOrder));
    QVERIFY(hasStartupIssue(Data::ConfigurationIssueCode::InvalidStartupValueSize));

    Data::DcConfiguration dc;
    dc.enabled = true;
    dc.modeName = "DC-Synchronous";
    dc.assignActivate = 0x0300;
    dc.sync0 = {true, 125000, -1000};
    dc.sync1 = {true, 125000, 1000};
    QVERIFY(Data::validateDcConfiguration(dc).isEmpty());

    dc.sync0.shiftTimeNs = 126000;
    dc.sync1.cycleTimeNs = 0;
    const QList<Data::ConfigurationIssue> dcIssues = Data::validateDcConfiguration(dc);
    const auto hasDcIssue = [&dcIssues](Data::ConfigurationIssueCode code) {
        return std::any_of(
            dcIssues.cbegin(), dcIssues.cend(), [code](const Data::ConfigurationIssue &issue) {
                return issue.code == code;
            });
    };
    QVERIFY(hasDcIssue(Data::ConfigurationIssueCode::DcShiftOutOfRange));
    QVERIFY(hasDcIssue(Data::ConfigurationIssueCode::InvalidDcCycle));
}

void EtherCATCoreTests::testDeviceDescriptionAndImportJobContract()
{
    Data::DeviceDescription description;
    description.summary
        = {Data::NodeId::create(),
           {2, 0x12345678, 0x00010002},
           "Servo Drive",
           "Drive-X",
           "Drives",
           true};
    description.syncManagers.append(
        {2, "Outputs", Data::SyncManagerDirection::MasterToSlave, 0x1000, 32, 0x24, true});
    Data::PdoDescription commandPdo;
    commandPdo.index = 0x1600;
    commandPdo.name = "Command";
    commandPdo.direction = Data::PdoDirection::Rx;
    commandPdo.syncManager = 2;
    commandPdo.fixed = true;
    commandPdo.mandatory = true;
    Data::PdoEntryDescription controlwordEntry;
    controlwordEntry.index = 0x6040;
    controlwordEntry.name = "Controlword";
    controlwordEntry.bitLength = 16;
    controlwordEntry.dataType = Data::EtherCATDataType::UnsignedInteger16;
    controlwordEntry.rawDataType = "UINT";
    commandPdo.entries.append(controlwordEntry);
    description.rxPdos.append(commandPdo);
    description.coe = {true, true, true, true, false};
    description.dcModes.append({"DC-Synchronous", 0x0300, 125000, 0, 0, 0});

    const Data::DeviceDescription copy = description;
    QCOMPARE(copy, description);
    QCOMPARE(copy.rxPdos.first().entries.first().bitLength, 16);

    TestDeviceImportJob job;
    QSignalSpy stateSpy(&job, &DeviceImportJob::stateChanged);
    QSignalSpy progressSpy(&job, &DeviceImportJob::progressChanged);
    QSignalSpy finishedSpy(&job, &DeviceImportJob::finished);
    job.start();
    QCOMPARE(job.state(), DeviceImportState::Running);
    QCOMPARE(job.progressValue(), 1);
    QCOMPARE(job.progressMaximum(), 2);
    job.cancel();
    QCOMPARE(job.state(), DeviceImportState::Finished);
    QVERIFY(job.result().canceled);
    QCOMPARE(stateSpy.count(), 3);
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    job.cancel();
    QCOMPARE(finishedSpy.count(), 1);
}

void EtherCATCoreTests::testDeviceAdapterValueSemantics()
{
    const Data::DeviceAdapterManifest qualified = testDeviceAdapterManifest();
    const Data::DeviceAdapterManifest copy = qualified;
    QCOMPARE(copy, qualified);

    QCOMPARE(qualified.id.value, QString("org.example.test.adapter/custom-drive"));
    QCOMPARE(
        qualified.capabilities.at(1).value, QString("example.test.capability/custom-diagnostics"));
    QCOMPARE(
        qualified.semanticSignals.constFirst().id.value,
        QString("urn:example.test:signal/custom.axis.target-velocity"));
    QCOMPARE(qualified.qualification, Data::DeviceAdapterQualification::Qualified);
    QVERIFY(qualified.signatureVerified);
    QVERIFY(qualified.realHardwareAllowed);

    QCOMPARE(qualified.match.vendorId, quint32(0x00a1b2c3));
    QCOMPARE(qualified.match.productCode, quint32(0x01020304));
    QCOMPARE(qualified.match.minimumRevision, quint32(0x00020003));
    QCOMPARE(qualified.match.maximumRevision, quint32(0x00020003));
    QCOMPARE(qualified.match.exactEsiSha256.size(), 32);
    QCOMPARE(
        qualified.match.exactEsiSha256.toHex(),
        QByteArray("1111111111111111111111111111111111111111111111111111111111111111"));

    const Data::SemanticSignalDefinition &signal = qualified.semanticSignals.constFirst();
    QCOMPARE(signal.direction, Data::SemanticSignalDirection::Output);
    QCOMPARE(signal.access, Data::SemanticSignalAccess::WriteOnly);
    QCOMPARE(signal.bindings.size(), 1);
    const Data::DeviceSignalBinding &binding = signal.bindings.constFirst();
    QCOMPARE(binding.kind, Data::DeviceSignalBindingKind::ProcessDataObject);
    QCOMPARE(binding.pdoDirection, Data::PdoDirection::Rx);
    QCOMPARE(binding.pdoIndex, quint16(0x1600));
    QCOMPARE(binding.objectIndex, quint16(0x7001));
    QCOMPARE(binding.objectSubIndex, quint8(0));
    QCOMPARE(binding.physicalType, Data::EtherCATDataType::Integer32);
    QCOMPARE(binding.bitWidth, 32);
    QCOMPARE(binding.byteOrder, Data::DeviceByteOrder::LittleEndian);
    QCOMPARE(signal.valueMetadata.unit, QString("rpm"));
    QCOMPARE(signal.valueMetadata.scale, 0.1);
    QCOMPARE(signal.valueMetadata.offset, -1.0);
    QVERIFY(signal.valueMetadata.hasMinimum);
    QCOMPARE(signal.valueMetadata.minimum, -3000.0);
    QVERIFY(signal.valueMetadata.hasMaximum);
    QCOMPARE(signal.valueMetadata.maximum, 3000.0);
    QVERIFY(signal.valueMetadata.hasStep);
    QCOMPARE(signal.valueMetadata.step, 0.1);
    QCOMPARE(
        signal.valueMetadata.enumValues,
        QList<Data::SemanticEnumValue>({{0, "stopped", "Stopped"}}));
    QVERIFY(signal.hasSafeValue);
    QCOMPARE(signal.safeValue, QVariant(0));
    QVERIFY(signal.manualControl.allowed);
    QVERIFY(signal.manualControl.requiresExclusiveControl);
    QVERIFY(signal.manualControl.holdToRun);
    QCOMPARE(signal.manualControl.commandTimeoutMs, quint32(250));
    QCOMPARE(signal.manualControl.timeoutAction, Data::ManualControlTimeoutAction::ControlledStop);

    Data::DeviceAdapterManifest changedRevision = qualified;
    changedRevision.match.minimumRevision = 0x00020004;
    QVERIFY(changedRevision != qualified);
    Data::DeviceAdapterManifest changedEsi = qualified;
    changedEsi.match.exactEsiSha256[0] = char(changedEsi.match.exactEsiSha256.at(0) ^ char(0xff));
    QVERIFY(changedEsi != qualified);

    Data::DeviceAdapterManifest candidate
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::Candidate, "2.2.0-rc1");
    QVERIFY(candidate != qualified);
    QVERIFY(!candidate.signatureVerified);
    QVERIFY(!candidate.realHardwareAllowed);
    QCOMPARE(candidate.qualification, Data::DeviceAdapterQualification::Candidate);
    Data::DeviceAdapterManifest mock
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::MockOnly, "mock-1");
    QCOMPARE(mock.qualification, Data::DeviceAdapterQualification::MockOnly);
    QVERIFY(!mock.realHardwareAllowed);
    QVERIFY(Data::DeviceAdapterQualification::Unqualified != candidate.qualification);
    QVERIFY(candidate.qualification != qualified.qualification);
    QVERIFY(qualified.qualification != mock.qualification);

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device.summary.identity
        = {qualified.match.vendorId, qualified.match.productCode, qualified.match.minimumRevision};
    request.device.sourceSha256 = qualified.match.exactEsiSha256;
    request.expectedAdapterId = qualified.id;
    request.expectedAdapterVersion = qualified.version;
    request.expectedAdapterContentSha256 = qualified.contentSha256;
    request.allowCandidate = true;
    request.allowMock = false;
    request.requireRealHardwareQualification = false;
    QVERIFY(request.hasExpectedAdapterSelection());
    QVERIFY(request.hasValidExpectedAdapterSelection());
    Data::DeviceAdapterResolutionRequest invalidExpectedAdapter = request;
    invalidExpectedAdapter.expectedAdapterContentSha256.chop(1);
    QVERIFY(invalidExpectedAdapter.hasExpectedAdapterSelection());
    QVERIFY(!invalidExpectedAdapter.hasValidExpectedAdapterSelection());
    Data::DeviceAdapterResolutionRequest automaticAdapter = request;
    automaticAdapter.expectedAdapterId = {};
    automaticAdapter.expectedAdapterVersion.clear();
    automaticAdapter.expectedAdapterContentSha256.clear();
    QVERIFY(!automaticAdapter.hasExpectedAdapterSelection());
    QVERIFY(automaticAdapter.hasValidExpectedAdapterSelection());
    const Data::NodeId pdoId = Data::NodeId::create();
    const Data::NodeId entryId = Data::NodeId::create();
    const Data::NodeId syncManagerId = Data::NodeId::create();
    request.processImage.outputs.bitSize = 32;
    request.processImage.outputs.byteSize = 4;
    request.processImage.outputs.entries = {
        {pdoId,
         entryId,
         syncManagerId,
         binding.pdoIndex,
         binding.objectIndex,
         binding.objectSubIndex,
         "Target velocity",
         binding.pdoDirection,
         2,
         96,
         binding.bitWidth,
         12,
         0,
         binding.physicalType}};
    QCOMPARE(Data::DeviceAdapterResolutionRequest(request), request);

    Data::BoundSemanticSignal boundSignal;
    boundSignal.definition = signal;
    boundSignal.binding = binding;
    boundSignal.processImageEntryId = entryId;
    boundSignal.processImageBitOffset = 96;
    boundSignal.processImageBitLength = 32;

    Data::ResolvedDeviceModel model;
    model.slaveId = request.slaveId;
    model.identity = request.device.summary.identity;
    model.esiSha256 = request.device.sourceSha256;
    model.adapterId = qualified.id;
    model.adapterVersion = qualified.version;
    model.adapterContentSha256 = qualified.contentSha256;
    model.qualification = qualified.qualification;
    model.capabilities = qualified.capabilities;
    model.boundSignals = {boundSignal};
    model.warnings = {"Test-only resolved model"};
    model.complete = true;
    QCOMPARE(Data::ResolvedDeviceModel(model), model);
    QCOMPARE(model.boundSignals.constFirst().processImageEntryId, entryId);
    QCOMPARE(model.boundSignals.constFirst().processImageBitOffset, qint64(96));
    QCOMPARE(model.identity.revisionNumber, qualified.match.minimumRevision);
    QCOMPARE(model.esiSha256, qualified.match.exactEsiSha256);
    QCOMPARE(model.adapterContentSha256, qualified.contentSha256);

    const Data::DeviceAdapterResolutionResult result{true, model, {}};
    QCOMPARE(Data::DeviceAdapterResolutionResult(result), result);
    QVERIFY(result.resolved);
    QVERIFY(result.error.isEmpty());
}

void EtherCATCoreTests::testDeviceAdapterProviderContract()
{
    const Data::DeviceAdapterManifest qualified = testDeviceAdapterManifest();
    const Data::DeviceAdapterManifest candidate
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::Candidate, "2.2.0-rc1");
    TestDeviceAdapterProvider provider({qualified, candidate});

    QCOMPARE(int(ProviderKind::DeviceAdapter), 6);
    QCOMPARE(provider.kind(), ProviderKind::DeviceAdapter);
    QCOMPARE(provider.adapterManifests(), QList<Data::DeviceAdapterManifest>({qualified, candidate}));
    const std::optional<Data::DeviceAdapterManifest> exact
        = provider.adapterManifest(qualified.id, qualified.version);
    QVERIFY(exact);
    QCOMPARE(*exact, qualified);
    const std::optional<Data::DeviceAdapterManifest> candidateVersion
        = provider.adapterManifest(candidate.id, candidate.version);
    QVERIFY(candidateVersion);
    QCOMPARE(*candidateVersion, candidate);
    QVERIFY(!provider.adapterManifest(qualified.id, "missing-version"));
    QVERIFY(!provider.adapterManifest({"org.example.test.adapter/missing"}, qualified.version));

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device.summary.identity
        = {qualified.match.vendorId, qualified.match.productCode, qualified.match.minimumRevision};
    request.device.sourceSha256 = qualified.match.exactEsiSha256;
    request.expectedAdapterId = qualified.id;
    request.expectedAdapterVersion = qualified.version;
    request.expectedAdapterContentSha256 = qualified.contentSha256;
    request.allowCandidate = false;
    request.allowMock = false;
    request.requireRealHardwareQualification = true;
    provider.resolutionResult.resolved = true;
    provider.resolutionResult.model.slaveId = request.slaveId;
    provider.resolutionResult.model.identity = request.device.summary.identity;
    provider.resolutionResult.model.esiSha256 = request.device.sourceSha256;
    provider.resolutionResult.model.adapterId = qualified.id;
    provider.resolutionResult.model.adapterVersion = qualified.version;
    provider.resolutionResult.model.adapterContentSha256 = qualified.contentSha256;
    provider.resolutionResult.model.qualification = qualified.qualification;
    provider.resolutionResult.model.complete = true;
    const Data::DeviceAdapterResolutionResult resolution = provider.resolveDevice(request);
    QVERIFY(resolution.resolved);
    QCOMPARE(resolution, provider.resolutionResult);
    QCOMPARE(provider.lastResolutionRequest, request);
    QCOMPARE(provider.resolutionCount, 1);

    QSignalSpy manifestsSpy(&provider, &DeviceAdapterProvider::adapterManifestsChanged);
    provider.replaceManifests({qualified});
    QCOMPARE(manifestsSpy.count(), 1);
    QCOMPARE(provider.adapterManifests(), QList<Data::DeviceAdapterManifest>({qualified}));
    QVERIFY(!provider.adapterManifest(candidate.id, candidate.version));

    QSignalSpy availabilitySpy(&provider, &Provider::availabilityChanged);
    QVERIFY(!provider.isAvailable());
    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());
    QCOMPARE(availabilitySpy.count(), 1);

    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);
    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    ExtensionSystem::PluginManager::addObject(&provider);
    QScopeGuard removeProvider(
        [&provider] { ExtensionSystem::PluginManager::removeObject(&provider); });
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::DeviceAdapter), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    ExtensionSystem::PluginManager::removeObject(&provider);
    removeProvider.dismiss();
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(registry->providers(ProviderKind::DeviceAdapter).isEmpty());
    QCOMPARE(removedSpy.count(), 1);
}

void EtherCATCoreTests::testPropertyPageProviderContract()
{
    const PropertyPageContext
        context{Data::NodeId::create(), Data::NodeId::create(), WorkbenchNodeKind::Device, "Drive"};
    const PropertyPageContext copy = context;
    QCOMPARE(copy, context);

    TestPropertyPageProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::PropertyPage);
    const QList<PropertyPageDescriptor> pages = provider.pages(context);
    QCOMPARE(
        pages, QList<PropertyPageDescriptor>({{Utils::Id("EtherCAT.Test.General"), "General", 10}}));

    std::unique_ptr<QWidget> page(provider.createPage(pages.first().id, nullptr));
    QVERIFY(page);
    provider.updatePage(pages.first().id, page.get(), context);
    QCOMPARE(page->objectName(), context.nodeId.toString());

    PropertyPageContext projectContext = context;
    projectContext.nodeKind = WorkbenchNodeKind::Project;
    QVERIFY(provider.pages(projectContext).isEmpty());
}

void EtherCATCoreTests::testWorkbenchDerivedNodeKinds()
{
    QCOMPARE(int(WorkbenchNodeKind::None), 0);
    QCOMPARE(int(WorkbenchNodeKind::Project), 1);
    QCOMPARE(int(WorkbenchNodeKind::Target), 2);
    QCOMPARE(int(WorkbenchNodeKind::Master), 3);
    QCOMPARE(int(WorkbenchNodeKind::DeviceRepository), 4);
    QCOMPARE(int(WorkbenchNodeKind::Device), 5);
    QCOMPARE(int(WorkbenchNodeKind::ConfiguredSlave), 6);
    QCOMPARE(int(WorkbenchNodeKind::Diagnostics), 7);
    QCOMPARE(int(WorkbenchNodeKind::Placeholder), 8);

    QCOMPARE(int(WorkbenchNodeKind::ProcessInputs), 9);
    QCOMPARE(int(WorkbenchNodeKind::ProcessOutputs), 10);
    QCOMPARE(int(WorkbenchNodeKind::RxPdoGroup), 11);
    QCOMPARE(int(WorkbenchNodeKind::TxPdoGroup), 12);
    QCOMPARE(int(WorkbenchNodeKind::Pdo), 13);
    QCOMPARE(int(WorkbenchNodeKind::PdoEntry), 14);
    QCOMPARE(int(WorkbenchNodeKind::Modules), 15);
    QCOMPARE(int(WorkbenchNodeKind::Module), 16);
    QCOMPARE(int(WorkbenchNodeKind::Channel), 17);

    const PropertyPageContext context{
        Data::NodeId::create(), Data::NodeId::create(), WorkbenchNodeKind::PdoEntry, "Controlword"};
    QCOMPARE(PropertyPageContext(context), context);
}

void EtherCATCoreTests::testControllerConnectionProviderContract()
{
    const Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    QCOMPARE(Data::ControllerConnectionScope(scope), scope);
    TestControllerConnectionProvider provider;
    QCOMPARE(int(ProviderKind::ControllerConnection), 5);
    QCOMPARE(provider.kind(), ProviderKind::ControllerConnection);
    const QList<Data::ControllerConnectionProfile> profiles = provider.connectionProfiles(scope);
    QCOMPARE(profiles.size(), 1);
    const Data::ControllerConnectionProfile profile = profiles.constFirst();
    QVERIFY(!profile.id.isNull());
    QVERIFY(profile.configured);
    QVERIFY(profile.supported);
    QVERIFY(profile.defaultProfile);
    QCOMPARE(profile.endpointSummary, QString("192.168.3.101:15200"));
    QCOMPARE(Data::ControllerConnectionProfile(profile), profile);
    const Data::ControllerConnectionRequest request{scope, profile.id};
    QCOMPARE(Data::ControllerConnectionRequest(request), request);
    Data::ControllerConnectionProfileConfiguration configuration;
    configuration.profileId = profile.id;
    configuration.endpoint = profile.endpointSummary;
    configuration.placeholder = QStringLiteral("192.168.3.101:15200");
    configuration.editable = true;
    QCOMPARE(Data::ControllerConnectionProfileConfiguration(configuration), configuration);
    QVERIFY(!provider.connectionProfileConfiguration(scope, profile.id));
    const Utils::Result<> unsupportedEndpointEdit
        = provider.setConnectionProfileEndpoint(scope, profile.id, QStringLiteral("192.0.2.1"));
    QVERIFY(!unsupportedEndpointEdit);
    QCOMPARE(
        unsupportedEndpointEdit.error(),
        Tr::tr("This controller provider does not support editing connection profiles."));
    QVERIFY(!provider.supportsPackageDeployment());
    Data::ControllerPackageDeploymentRequest deploymentRequest;
    deploymentRequest.operationId = QStringLiteral("core-contract");
    deploymentRequest.artifact = QByteArray("signed-controller-package");
    deploymentRequest.configurationId = 813;
    QCOMPARE(Data::ControllerPackageDeploymentRequest(deploymentRequest), deploymentRequest);
    const Utils::Result<> unsupportedDeployment = provider.deployPackage(deploymentRequest);
    QVERIFY(!unsupportedDeployment);
    QCOMPARE(
        unsupportedDeployment.error(),
        Tr::tr("This controller provider does not support package deployment."));
    const Utils::Result<> unsupportedCancel = provider.cancelPackageDeployment(
        deploymentRequest.operationId);
    QVERIFY(!unsupportedCancel);
    QCOMPARE(
        unsupportedCancel.error(),
        Tr::tr("This controller provider does not support canceling package deployment."));
    QVERIFY(!provider.supportsRuntimeResources());
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    const Utils::Result<> unsupportedRuntimeRefresh = provider.refreshRuntimeResources();
    QVERIFY(!unsupportedRuntimeRefresh);
    QCOMPARE(
        unsupportedRuntimeRefresh.error(),
        Tr::tr("This controller provider does not support runtime resources."));
    QSignalSpy targetedFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    const Utils::Result<> unsupportedTargetedRead = provider.requestRuntimeResourceSnapshot({});
    QVERIFY(!unsupportedTargetedRead);
    QCOMPARE(
        unsupportedTargetedRead.error(),
        Tr::tr("This controller provider does not support targeted runtime resource snapshots."));
    QCOMPARE(targetedFinishedSpy.count(), 0);

    QVERIFY(!provider.supportsRuntimeSemanticMappingAttestation());
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QSignalSpy semanticAttestationChangedSpy(
        &provider, &ControllerConnectionProvider::runtimeSemanticMappingAttestationChanged);
    QSignalSpy semanticAttestationFinishedSpy(
        &provider,
        &ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    const Utils::Result<> unsupportedSemanticAttestation
        = provider.requestRuntimeSemanticMappingAttestation({});
    QVERIFY(!unsupportedSemanticAttestation);
    QCOMPARE(
        unsupportedSemanticAttestation.error(),
        Tr::tr("This controller provider does not support semantic mapping attestation."));
    QCOMPARE(semanticAttestationChangedSpy.count(), 0);
    QCOMPARE(semanticAttestationFinishedSpy.count(), 0);

    RuntimeOutputFixture runtimeOutput;
    QVERIFY(!provider.supportsRuntimeOutputTransactions());
    QSignalSpy outputPolicyFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    QSignalSpy outputStateChangedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputTransactionStateChanged);
    QSignalSpy outputStateFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    QSignalSpy outputTransactionFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputTransactionFinished);
    const Utils::Result<> unsupportedOutputPolicy = provider.requestRuntimeOutputGroupPolicy(
        runtimeOutput.policyRequest);
    QVERIFY(!unsupportedOutputPolicy);
    QCOMPARE(
        unsupportedOutputPolicy.error(),
        Tr::tr("This controller provider does not support runtime output group policies."));
    const Utils::Result<> unsupportedOutputState = provider.requestRuntimeOutputTransactionState(
        runtimeOutput.stateRequest);
    QVERIFY(!unsupportedOutputState);
    QCOMPARE(
        unsupportedOutputState.error(),
        Tr::tr("This controller provider does not support runtime output transaction state."));
    const Utils::Result<> unsupportedOutputTransaction = provider.applyRuntimeOutputTransaction(
        runtimeOutput.transactionRequest);
    QVERIFY(!unsupportedOutputTransaction);
    QCOMPARE(
        unsupportedOutputTransaction.error(),
        Tr::tr("This controller provider does not support runtime output transactions."));
    QCOMPARE(outputPolicyFinishedSpy.count(), 0);
    QCOMPARE(outputStateChangedSpy.count(), 0);
    QCOMPARE(outputStateFinishedSpy.count(), 0);
    QCOMPARE(outputTransactionFinishedSpy.count(), 0);

    Data::ControllerPackageDeploymentProgress deploymentProgress;
    deploymentProgress.operationId = deploymentRequest.operationId;
    deploymentProgress.artifactSha256 = QByteArray(32, '\x5a');
    deploymentProgress.state = Data::ControllerPackageDeploymentState::Uploading;
    deploymentProgress.totalBytes = deploymentRequest.artifact.size();
    deploymentProgress.transferredBytes = 7;
    deploymentProgress.candidate = Data::ControllerPackageSelector{
        Data::ControllerSlot::B, 12, deploymentRequest.configurationId};
    deploymentProgress.previousActive
        = Data::ControllerPackageSelector{Data::ControllerSlot::A, 11, 810};
    Data::ControllerPackageDeploymentAuditEvent auditEvent;
    auditEvent.sequence = 1;
    auditEvent.operation = Data::ControllerOperation::UploadPackage;
    auditEvent.requestId = 42;
    auditEvent.detail = QStringLiteral("BulkBegin queued");
    auditEvent.occurredAt = QDateTime::currentDateTimeUtc();
    deploymentProgress.audit.append(auditEvent);
    QCOMPARE(
        Data::ControllerPackageSelector(*deploymentProgress.candidate),
        *deploymentProgress.candidate);
    QVERIFY(deploymentProgress.candidate->isValid());
    QCOMPARE(
        Data::ControllerPackageDeploymentAuditEvent(deploymentProgress.audit.constFirst()),
        auditEvent);
    QCOMPARE(Data::ControllerPackageDeploymentProgress(deploymentProgress), deploymentProgress);

    TestControllerConnectionProvider alternateProvider(
        "EtherCAT.Connection.VendorIpc",
        "Vendor IPC controller",
        "ipc://controller-1",
        {"primary"},
        profile.id);
    const Data::ControllerConnectionProfile alternateProfile
        = alternateProvider.connectionProfiles(scope).constFirst();
    QCOMPARE(alternateProfile.id, profile.id);
    alternateProvider.setAvailable(true);
    QVERIFY_RESULT(alternateProvider.connectToController(request));
    QCOMPARE(alternateProvider.connectionSnapshot().channels.size(), 1);
    QCOMPARE(alternateProvider.connectionSnapshot().channels.constFirst().id, QString("primary"));
    QCOMPARE(alternateProvider.connectionSnapshot().endpointSummary, QString("ipc://controller-1"));
    QVERIFY_RESULT(alternateProvider.disconnectFromController());

    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.connectionSnapshot().readOnly);
    QVERIFY(!provider.refreshController());

    QSignalSpy profilesSpy(&provider, &ControllerConnectionProvider::connectionProfilesChanged);
    QSignalSpy snapshotSpy(&provider, &ControllerConnectionProvider::connectionSnapshotChanged);
    QSignalSpy catalogSpy(&provider, &ControllerConnectionProvider::runtimeResourceCatalogChanged);
    QSignalSpy
        runtimeSnapshotSpy(&provider, &ControllerConnectionProvider::runtimeResourceSnapshotChanged);
    QCOMPARE(catalogSpy.count(), 0);
    QCOMPARE(runtimeSnapshotSpy.count(), 0);
    QVERIFY(!provider.isAvailable());
    QVERIFY(!provider.connectToController(request));
    QCOMPARE(snapshotSpy.count(), 0);
    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());
    QVERIFY(!provider.connectToController({}));
    Data::ControllerConnectionRequest invalidRequest = request;
    invalidRequest.scope.projectId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest = request;
    invalidRequest.scope.masterId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest = request;
    invalidRequest.profileId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest.profileId = Data::NodeId::create();
    QVERIFY(invalidRequest.profileId != profile.id);
    QVERIFY(!provider.connectToController(invalidRequest));
    provider.setProfileConfigured(false);
    QCOMPARE(profilesSpy.count(), 1);
    const Data::ControllerConnectionProfile incompleteProfile
        = provider.connectionProfiles(scope).constFirst();
    QVERIFY(!incompleteProfile.configured);
    QVERIFY(!incompleteProfile.configurationIssue.isEmpty());
    QVERIFY(!provider.connectToController(request));
    provider.setProfileConfigured(true);
    QCOMPARE(profilesSpy.count(), 2);
    provider.setProfileSupported(false);
    QCOMPARE(profilesSpy.count(), 3);
    QVERIFY(!provider.connectionProfiles(scope).constFirst().supported);
    QVERIFY(!provider.connectToController(request));
    provider.setProfileSupported(true);
    QCOMPARE(profilesSpy.count(), 4);
    QVERIFY(provider.connectionProfiles({}).isEmpty());
    QCOMPARE(snapshotSpy.count(), 0);

    const Utils::Result<> connectResult = provider.connectToController(request);
    QVERIFY_RESULT(connectResult);
    const Data::ControllerConnectionSnapshot connecting = provider.connectionSnapshot();
    QCOMPARE(connecting.scope, request.scope);
    QCOMPARE(connecting.profileId, request.profileId);
    QCOMPARE(connecting.endpointSummary, profile.endpointSummary);
    QCOMPARE(connecting.state, Data::ControllerConnectionState::Connecting);
    QCOMPARE(connecting.sessionGeneration, quint64(1));
    QCOMPARE(connecting.channels.size(), 3);
    QCOMPARE(connecting.channels.at(0).id, QString("control"));
    QCOMPARE(connecting.channels.at(0).displayName, QString("CONTROL"));
    QCOMPARE(connecting.channels.at(0).state, Data::ControllerChannelState::Connecting);
    QCOMPARE(connecting.channels.at(0).maximumPayloadBytes, 4096);
    QCOMPARE(connecting.channels.at(1).id, QString("events"));
    QCOMPARE(connecting.channels.at(1).maximumPayloadBytes, 65536);
    QCOMPARE(connecting.channels.at(2).id, QString("bulk"));
    QCOMPARE(connecting.channels.at(2).maximumPayloadBytes, 65536);
    QVERIFY(connecting.readOnly);
    QCOMPARE(snapshotSpy.count(), 1);
    QVERIFY(!provider.connectToController(request));

    provider.completeHandshake();
    const Data::ControllerConnectionSnapshot connected = provider.connectionSnapshot();
    QCOMPARE(connected.state, Data::ControllerConnectionState::Connected);
    QCOMPARE(connected.protocolVersion, Data::ControllerProtocolVersion(1, 9));
    QVERIFY(connected.session);
    QCOMPARE(connected.session->sessionId, quint64(17));
    QCOMPARE(connected.session->bootId, quint64(42));
    QVERIFY(!connected.session->ownsControlLease);
    QVERIFY(connected.controllerState);
    QCOMPARE(connected.controllerState->serviceState, Data::ControllerServiceState::Shutdown);
    QVERIFY(connected.controllerState->ready);
    QVERIFY(connected.capability);
    QCOMPARE(connected.capability->maximumSlaves, 64);
    QCOMPARE(connected.capability->descriptorSha256.size(), 32);
    QVERIFY(connected.capability->controlLease);
    QVERIFY(connected.capability->resumablePush);
    QVERIFY(connected.capability->transactionalBulk);
    QVERIFY(!connected.capability->runtimeOutputTransactions);
    QVERIFY(connected.capability->firmwareUpdate);
    QVERIFY(connected.capability->coe);
    QVERIFY(connected.capability->distributedClocks);
    QVERIFY(connected.package);
    QCOMPARE(connected.package->activeSlot, Data::ControllerSlot::A);
    QCOMPARE(connected.package->activeConfigurationId, quint64(810));
    QCOMPARE(connected.package->controllerState, Data::ControllerPackageState::Empty);
    QVERIFY(connected.firmware);
    QCOMPARE(connected.firmware->state, Data::ControllerFirmwareState::Confirmed);
    QCOMPARE(connected.firmware->generation, quint64(2517));
    QCOMPARE(Data::ControllerConnectionSnapshot(connected), connected);
    QCOMPARE(snapshotSpy.count(), 2);

    provider.setAvailable(false);
    QVERIFY(!provider.refreshController());
    QCOMPARE(snapshotSpy.count(), 2);
    provider.setAvailable(true);
    const Utils::Result<> refreshResult = provider.refreshController();
    QVERIFY_RESULT(refreshResult);
    QVERIFY(provider.connectionSnapshot().updatedAt.isValid());
    QCOMPARE(snapshotSpy.count(), 3);

    provider.degradePush();
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Degraded);
    QCOMPARE(provider.connectionSnapshot().channels.at(1).state, Data::ControllerChannelState::Failed);
    QCOMPARE(snapshotSpy.count(), 4);
    const Utils::Result<> degradedRefreshResult = provider.refreshController();
    QVERIFY_RESULT(degradedRefreshResult);
    QCOMPARE(snapshotSpy.count(), 5);

    provider.failProtocol();
    const Data::ControllerConnectionSnapshot failed = provider.connectionSnapshot();
    QCOMPARE(failed.state, Data::ControllerConnectionState::Failed);
    QVERIFY(failed.lastError);
    QCOMPARE(failed.lastError->source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(failed.lastError->channelId, QString("events"));
    QCOMPARE(failed.lastError->operation, Data::ControllerOperation::SubscribeEvents);
    QVERIFY(!failed.lastError->code);
    QVERIFY(!failed.lastError->operationResult);
    QCOMPARE(failed.lastError->codeName, QString("UNEXPECTED_PUSH_FRAME"));
    QVERIFY(!failed.lastError->sourceDetail);
    QVERIFY(failed.lastError->requestId);
    QCOMPARE(*failed.lastError->requestId, quint64(73));
    QCOMPARE(failed.lastError->retryDisposition, Data::ControllerRetryDisposition::Reconnect);
    QCOMPARE(snapshotSpy.count(), 6);

    Data::ControllerOperationError controllerError;
    controllerError.source = Data::ControllerErrorSource::Controller;
    controllerError.channelId = "control";
    controllerError.operation = Data::ControllerOperation::QueryState;
    controllerError.code = -6;
    controllerError.operationResult = -2;
    controllerError.sourceDetail = 17;
    controllerError.codeName = "BAD_MESSAGE";
    controllerError.retryAfterMs = 250;
    const Data::ControllerOperationError controllerErrorCopy = controllerError;
    QCOMPARE(controllerErrorCopy, controllerError);
    QCOMPARE(*controllerErrorCopy.sourceDetail, quint64(17));
    QCOMPARE(*controllerErrorCopy.retryAfterMs, 250);

    provider.setAvailable(false);
    const Utils::Result<> failedDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(failedDisconnectResult);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    for (const Data::ControllerChannelStatus &channel : provider.connectionSnapshot().channels)
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
    QCOMPARE(provider.connectionSnapshot().scope, request.scope);
    QCOMPARE(provider.connectionSnapshot().profileId, request.profileId);
    QVERIFY(!provider.connectionSnapshot().session);
    QVERIFY(!provider.connectionSnapshot().lastHeartbeatAt.isValid());
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(2));
    QCOMPARE(snapshotSpy.count(), 7);
    QVERIFY(!provider.connectToController(request));
    QCOMPARE(snapshotSpy.count(), 7);

    provider.setAvailable(true);
    const Utils::Result<> reconnectResult = provider.connectToController(request);
    QVERIFY_RESULT(reconnectResult);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(3));
    QCOMPARE(snapshotSpy.count(), 8);
    const Utils::Result<> reconnectDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(reconnectDisconnectResult);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(4));
    QCOMPARE(snapshotSpy.count(), 9);
    const Utils::Result<> repeatedDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(repeatedDisconnectResult);
    QCOMPARE(snapshotSpy.count(), 9);
}

void EtherCATCoreTests::testScanProviderContract()
{
    TestScanProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::Scan);
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
    QVERIFY(!provider.lastScanResult());

    QSignalSpy stateSpy(&provider, &ScanProvider::scanStateChanged);
    QSignalSpy progressSpy(&provider, &ScanProvider::scanProgressChanged);
    QSignalSpy finishedSpy(&provider, &ScanProvider::scanFinished);
    const Data::ScanRequest
        request{Data::NodeId::create(), Data::NodeId::create(), Data::ScanOperation::Slaves, {}};
    QVERIFY_RESULT(provider.startScan(request));
    QCOMPARE(provider.scanState(), Data::ScanState::Preparing);
    QCOMPARE(provider.scanProgress().maximum, 4);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(progressSpy.count(), 1);

    QVERIFY(!provider.startScan(request));
    provider.complete();
    QCOMPARE(provider.scanState(), Data::ScanState::Completed);
    QVERIFY(provider.lastScanResult());
    QVERIFY(provider.lastScanResult()->snapshot.mock);
    QCOMPARE(provider.lastScanResult()->snapshot.slaves.first().serialNumber, quint32(17));
    QCOMPARE(provider.lastScanResult()->comparison.differences.size(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    provider.clearScanResult();
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
    QVERIFY_RESULT(provider.startScan(request));
    provider.cancelScan();
    QCOMPARE(provider.scanState(), Data::ScanState::Cancelled);
    QCOMPARE(finishedSpy.count(), 2);
    provider.clearScanResult();
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
}

void EtherCATCoreTests::testDiagnosticsProviderContract()
{
    TestDiagnosticsProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::Diagnostics);
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(provider.limits(), Data::DiagnosticsLimits(4, 5, 10, 100));
    QVERIFY(!provider.latestSnapshot());
    QVERIFY(provider.events().isEmpty());
    QVERIFY(provider.trendSamples().isEmpty());

    QSignalSpy stateSpy(&provider, &DiagnosticsProvider::streamStateChanged);
    QSignalSpy snapshotSpy(&provider, &DiagnosticsProvider::diagnosticsSnapshotChanged);
    QSignalSpy eventSpy(&provider, &DiagnosticsProvider::diagnosticEventsChanged);
    QSignalSpy trendSpy(&provider, &DiagnosticsProvider::diagnosticTrendChanged);
    QSignalSpy stoppedSpy(&provider, &DiagnosticsProvider::monitoringStopped);
    QVERIFY(!provider.startMonitoring({}));

    const Data::DiagnosticsRequest request{Data::NodeId::create(), Data::NodeId::create()};
    const Utils::Result<> startResult = provider.startMonitoring(request);
    QVERIFY_RESULT(startResult);
    QCOMPARE(provider.activeRequest(), request);
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Running);
    QCOMPARE(stateSpy.count(), 2);
    QVERIFY(!provider.startMonitoring(request));

    provider.publish();
    QVERIFY(provider.latestSnapshot());
    const Data::DiagnosticsSnapshot snapshot = *provider.latestSnapshot();
    const Data::DiagnosticsSnapshot snapshotCopy = snapshot;
    QCOMPARE(snapshotCopy, snapshot);
    QVERIFY(snapshot.mock);
    QCOMPARE(snapshot.masterState, Data::EtherCATState::Operational);
    QCOMPARE(snapshot.workingCounter.expected, quint32(4));
    QCOMPARE(snapshot.workingCounter.actual, quint32(3));
    QCOMPARE(snapshot.frameErrors.overflows, quint64(6));
    QCOMPARE(snapshot.distributedClock.offsetNs, qint64(17));
    QCOMPARE(snapshot.cycle.nominalCycleNs, qint64(125000));
    QCOMPARE(snapshot.slaves.size(), 1);
    QCOMPARE(provider.events().size(), 1);
    QCOMPARE(provider.events().first().repeatCount, quint32(2));
    QCOMPARE(provider.trendSamples().size(), 1);
    QCOMPARE(snapshotSpy.count(), 1);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(trendSpy.count(), 1);

    const Utils::Result<> modeResult = provider.requestRunMode(Data::DiagnosticsRunMode::Config);
    QVERIFY_RESULT(modeResult);
    QCOMPARE(provider.latestSnapshot()->runMode, Data::DiagnosticsRunMode::Config);
    QCOMPARE(snapshotSpy.count(), 2);

    const Data::NodeId alarmId = provider.events().first().id;
    const Utils::Result<> acknowledgeResult = provider.acknowledgeAlarm(alarmId);
    QVERIFY_RESULT(acknowledgeResult);
    QCOMPARE(provider.events().first().lifecycle, Data::AlarmLifecycle::Acknowledged);
    QVERIFY(!provider.acknowledgeAlarm(alarmId));
    provider.recoverAlarm();
    const Utils::Result<> clearResult = provider.clearRecoveredEvents();
    QVERIFY_RESULT(clearResult);
    QVERIFY(provider.events().isEmpty());
    QCOMPARE(eventSpy.count(), 4);

    provider.stopMonitoring();
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(provider.activeRequest(), Data::DiagnosticsRequest());
    QCOMPARE(stateSpy.count(), 4);
    QCOMPARE(stoppedSpy.count(), 1);
    provider.stopMonitoring();
    QCOMPARE(stoppedSpy.count(), 1);
    QVERIFY(!provider.requestRunMode(Data::DiagnosticsRunMode::Run));

    const Utils::Result<> restartResult = provider.startMonitoring(request);
    QVERIFY_RESULT(restartResult);
    provider.fail("Mock diagnostics source failed");
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Failed);
    QCOMPARE(provider.lastDiagnosticsError(), QString("Mock diagnostics source failed"));
    QCOMPARE(stoppedSpy.count(), 2);
    provider.stopMonitoring();
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(stoppedSpy.count(), 2);
}

void EtherCATCoreTests::testSelectionServicePublishesStableIds()
{
    SelectionService *service = ExtensionSystem::PluginManager::getObject<SelectionService>();
    QVERIFY(service);
    service->clear();

    QSignalSpy changedSpy(service, &SelectionService::currentNodeChanged);
    const Data::NodeId selectedId = Data::NodeId::create();
    service->setCurrentNodeId(selectedId);
    QCOMPARE(service->currentNodeId(), selectedId);
    QCOMPARE(changedSpy.count(), 1);

    service->setCurrentNodeId(selectedId);
    QCOMPARE(changedSpy.count(), 1);

    service->clear();
    QVERIFY(service->currentNodeId().isNull());
    QCOMPARE(changedSpy.count(), 2);
}

void EtherCATCoreTests::testStateServiceAggregatesContributions()
{
    StateService *service = ExtensionSystem::PluginManager::getObject<StateService>();
    QVERIFY(service);
    service->clearAll();

    QSignalSpy aggregateSpy(service, &StateService::aggregateSeverityChanged);
    QVERIFY(!service->setStatus({}));
    QVERIFY(service->setStatus({"EtherCAT.Project", StatusSeverity::Busy, "Opening", {}}));
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Busy);
    QVERIFY(service->setStatus({"EtherCAT.Scan", StatusSeverity::Error, "Failed", "Mock"}));
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Error);
    QCOMPARE(service->statuses().size(), 2);
    QCOMPARE(service->statuses().at(0).sourceId, Utils::Id("EtherCAT.Project"));
    QCOMPARE(aggregateSpy.count(), 2);

    service->clearStatus("EtherCAT.Scan");
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Busy);
    service->clearAll();
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Ready);
    QVERIFY(service->statuses().isEmpty());
}

void EtherCATCoreTests::testProviderRegistryTracksObjectPool()
{
    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);

    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    TestScanProvider provider;
    TestPropertyPageProvider reentrantProvider;
    TestControllerConnectionProvider connectionProvider;
    TestControllerConnectionProvider alternateConnectionProvider(
        "EtherCAT.Connection.VendorIpc", "Vendor IPC controller", "ipc://controller-1", {"primary"});

    ExtensionSystem::PluginManager::addObject(&provider);
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::Scan), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());

    ExtensionSystem::PluginManager::addObject(&reentrantProvider);
    QCOMPARE(registry->provider(reentrantProvider.id()), &reentrantProvider);
    QCOMPARE(addedSpy.count(), 2);

    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    QCOMPARE(registry->provider(connectionProvider.id()), &connectionProvider);
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&connectionProvider}));
    QCOMPARE(addedSpy.count(), 3);

    ExtensionSystem::PluginManager::addObject(&alternateConnectionProvider);
    QCOMPARE(registry->provider(alternateConnectionProvider.id()), &alternateConnectionProvider);
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&connectionProvider, &alternateConnectionProvider}));
    QCOMPARE(addedSpy.count(), 4);

    bool departingProviderUnlinked = false;
    bool nestedRemovalObserved = false;
    connect(
        registry,
        &ProviderRegistry::providerAboutToBeRemoved,
        &reentrantProvider,
        [&](Provider *departingProvider) {
            if (departingProvider != &reentrantProvider)
                return;
            departingProviderUnlinked = !registry->provider(reentrantProvider.id());
            nestedRemovalObserved = true;
            ExtensionSystem::PluginManager::removeObject(&provider);
        },
        Qt::DirectConnection);
    ExtensionSystem::PluginManager::removeObject(&reentrantProvider);
    QVERIFY(departingProviderUnlinked);
    QVERIFY(nestedRemovalObserved);
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(!registry->provider(reentrantProvider.id()));
    QVERIFY(registry->providers(ProviderKind::Scan).isEmpty());
    QVERIFY(!registry->providers(ProviderKind::PropertyPage).contains(&reentrantProvider));
    QCOMPARE(removedSpy.count(), 2);

    ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    QVERIFY(!registry->provider(connectionProvider.id()));
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&alternateConnectionProvider}));
    QCOMPARE(removedSpy.count(), 3);

    ExtensionSystem::PluginManager::removeObject(&alternateConnectionProvider);
    QVERIFY(!registry->provider(alternateConnectionProvider.id()));
    QVERIFY(registry->providers(ProviderKind::ControllerConnection).isEmpty());
    QCOMPARE(removedSpy.count(), 4);
}

void EtherCATCoreTests::testSettingsPageIsRegistered()
{
    const QList<::Core::IOptionsPage *> pages = ::Core::IOptionsPage::allOptionsPages();
    const auto found = std::find_if(pages.cbegin(), pages.cend(), [](const auto *page) {
        return page->id() == Constants::SETTINGS_GENERAL;
    });
    QVERIFY(found != pages.cend());
    QCOMPARE((*found)->category(), Utils::Id(Constants::SETTINGS_CATEGORY));
    QCOMPARE((*found)->displayCategory(), QString("EtherCAT"));
    QVERIFY((*found)->aspects().has_value());

    std::unique_ptr<::Core::IOptionsPageWidget> widget((*found)->createWidget());
    QVERIFY(widget);
    QVERIFY(!settings().showAdvancedProperties());
    QCOMPARE(settings().maximumRecentEvents(), 1000);
}

} // namespace EtherCAT::Core::Internal
