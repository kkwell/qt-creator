// Copyright (C) 2026 Embed Labs

#include "ethercatprojectcompilertests.h"

#include "compileroperationstore.h"
#include "compilerprovisioningprofile.h"
#include "ethercatprojectcompilerconstants.h"
#include "provisionedruntimepackagecompilerprovider.h"

#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/offlineconfiguration.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/runtimepackageactivation.h>
#include <ethercatdata/runtimepackagecompiler.h>

#include <ethercatcore/runtimepackagecompilercodec.h>
#include <ethercatcore/runtimepackagecompilerprovider.h>

#include <utils/filepath.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <optional>
#include <thread>
#include <tuple>

#ifdef Q_OS_UNIX
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace EtherCAT::ProjectCompiler::Internal {

namespace {

using namespace std::chrono_literals;

Data::RuntimePackageCompilerSha256 sha256(QByteArrayView bytes)
{
    return Data::RuntimePackageCompilerSha256{
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
}

Data::RuntimePackageCompilerCanonicalJson canonical(QByteArray bytes)
{
    return Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(std::move(bytes));
}

Data::RuntimePackageCompilerSourceArtifact artifact(
    Data::RuntimePackageCompilerSourceArtifactKind kind,
    const QString &path,
    const QByteArray &bytes)
{
    return {kind, path, bytes, sha256(bytes)};
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(bytes) == bytes.size() && file.flush();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct CompilerFixture
{
    Data::NodeId projectId = Data::NodeId::fromString("11111111-1111-4111-8111-111111111111");
    Data::NodeId masterId = Data::NodeId::fromString("22222222-2222-4222-8222-222222222222");
    Data::NodeId slaveId = Data::NodeId::fromString("33333333-3333-4333-8333-333333333333");
    QByteArray publicKey = QByteArray(32, '\x31');
    QByteArray signature = QByteArray(64, '\x32');
    QByteArray serializedProject = QByteArray("{\"format\":\"embed-labs-ethercat-project\"}\n");
    Data::RuntimePackageCompilerCompileRequest request;

    CompilerFixture()
    {
        const QByteArray esiBytes("<EtherCATInfo/>");
        const QByteArray adapterBytes("format: ethercat-device-adapter-v1\n");
        const auto adapterFileSha = sha256(adapterBytes);
        const QByteArray adapterBundle
            = QByteArray(
                  "{\"bundle_id\":\"embedlabs.fixture.adapters\",\"entries\":[{"
                  "\"adapter_id\":\"solidot.xb6.fixture\",\"adapter_version\":"
                  "\"1.0.0\",\"bytes\":")
              + QByteArray::number(adapterBytes.size()) + QByteArray(",\"canonical_sha256\":\"")
              + adapterFileSha.value().toHex() + QByteArray("\",\"file_sha256\":\"")
              + adapterFileSha.value().toHex()
              + QByteArray(
                  "\",\"relative_path\":\"adapter_bundle/xb6.ecdev.yaml\"}],"
                  "\"format\":\"ethercat-lower-adapter-bundle-v1\","
                  "\"format_version\":1}\n");
        const QByteArray controllerFeatures("{\"format\":\"controller-features-v1\"}\n");
        const auto adapterBundleSha = sha256(adapterBundle);
        const auto capabilitySha = sha256("capability");
        const auto controllerFeaturesSha = sha256(controllerFeatures);
        const auto signingKeySha = sha256(publicKey);
        const QByteArray target = QByteArray("{\"adapter_bundle_sha256\":\"")
                                  + adapterBundleSha.value().toHex()
                                  + QByteArray(
                                      "\",\"capability\":{\"fixture\":true},"
                                      "\"capability_descriptor_sha256\":\"")
                                  + capabilitySha.value().toHex()
                                  + QByteArray("\",\"controller_features_sha256\":\"")
                                  + controllerFeaturesSha.value().toHex()
                                  + QByteArray(
                                      "\",\"cpu1_abi_version\":2,\"cycle_periods_ns\":[125000],"
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
                                  + signingKeySha.value().toHex() + QByteArray("\"}\n");

        Data::ProjectSnapshot project;
        project.id = projectId;
        project.name = QStringLiteral("Compiler fixture");
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
        slave.adapterSelection.adapterId = {"org.embedlabs.adapter.solidot.xb6-fixture"};
        slave.adapterSelection.adapterVersion = QStringLiteral("0.3.0");
        slave.adapterSelection.adapterContentSha256 = sha256("upper-adapter").value();
        slave.adapterSelection.processDataProfileId = QStringLiteral("xb6.do16");
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
        project.slaves = {slave};

        const Data::RuntimePackageActivationProjectCapture capture{
            project,
            serializedProject,
            7,
            Data::RuntimePackageActivationDocumentRevisionToken{"revision-7"},
            Data::RuntimePackageActivationOriginalBindingToken{"binding-empty"},
        };

        Data::RuntimePackageCompilerFreshTopologyEvidence topology;
        topology.scope = {projectId, masterId};
        topology.sessionGeneration = 7;
        topology.sessionId = 9;
        topology.evidenceId = QStringLiteral("discover:fixture:57");
        topology.captureBootId = 0x4f536f408aafbc51;
        topology.captureSequence = 57;
        topology.capturedAtNs = 1'785'542'400'000'000'000ULL;
        topology.expiresAtNs = topology.capturedAtNs + 3'600'000'000'000ULL;
        topology.cyclePeriodNs = 125000;
        topology.linkSpeedMbps = 100;
        topology.slaves = {{slaveId, 0, 0x1001, 0, slave.identity, 0, {}}};
        const QByteArray topologyBytes
            = QByteArray(
                  "{\"capture_boot_id\":\"0x4f536f408aafbc51\","
                  "\"capture_sequence\":57,\"captured_at_ns\":")
              + QByteArray::number(topology.capturedAtNs)
              + QByteArray(",\"evidence_id\":\"discover:fixture:57\",\"expires_at_ns\":")
              + QByteArray::number(topology.expiresAtNs)
              + QByteArray(
                  ",\"format\":\"ethercat-discover-topology-evidence-v1\","
                  "\"format_version\":1,\"matched_scan\":{"
                  "\"cycle_period_ns\":125000,\"format\":\"ethercat-esi-match-v1\","
                  "\"link_speed_mbps\":100,\"slaves\":[{\"alias\":0,"
                  "\"identity\":{\"product_code\":182,\"revision\":1,"
                  "\"vendor_id\":8930371},\"modules\":[],\"position\":0,"
                  "\"serial\":0,\"station_address\":4097}]}}\n");
        topology.canonicalEvidence = canonical(topologyBytes);

        Data::RuntimePackageCompilerSourceArtifacts sources;
        sources.topologyEvidence = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::TopologyEvidence,
            QStringLiteral("topology-evidence-v1.json"),
            topologyBytes);
        sources.targetProfile = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::TargetProfile,
            QStringLiteral("target-capability-profile-v1.json"),
            target);
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
            adapterBundle);
        sources.policyTemplate = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::PolicyTemplate,
            QStringLiteral("policy-template.json"),
            QByteArray("{\"format\":\"policy-template-v1\"}\n"));
        sources.controllerFeatures = artifact(
            Data::RuntimePackageCompilerSourceArtifactKind::ControllerFeatures,
            QStringLiteral("controller-features-v1.json"),
            controllerFeatures);
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
        deviceSource.projectAdapterContractVersion = Data::DeviceAdapterContractVersion::V3;
        deviceSource.projectAdapterId = slave.adapterSelection.adapterId;
        deviceSource.projectAdapterVersion = slave.adapterSelection.adapterVersion;
        deviceSource.projectAdapterContentSha256 = Data::RuntimePackageCompilerSha256{
            slave.adapterSelection.adapterContentSha256};
        deviceSource.projectControllerAdapterTarget = {
            QStringLiteral("solidot.xb6.fixture"),
            QStringLiteral("1.0.0"),
            adapterFileSha.value(),
            sha256(esiBytes).value(),
        };
        deviceSource.projectPdoProfileId = slave.adapterSelection.processDataProfileId;
        deviceSource.projectSignedPdoProfileId = QStringLiteral("do16");
        deviceSource.adapterId = QStringLiteral("solidot.xb6.fixture");
        deviceSource.adapterVersion = QStringLiteral("1.0.0");
        deviceSource.adapterCanonicalSha256 = adapterFileSha;
        deviceSource.pdoProfileId = QStringLiteral("do16");
        deviceSource.explicitNoDc = true;

        Data::RuntimePackageCompilerSignedTargetProfileEvidence signedTarget;
        signedTarget.profileId = QStringLiteral("embedlabs.zynq.fixture");
        signedTarget.policyRevision = 1;
        signedTarget.canonicalProfile = canonical(target);
        signedTarget.capabilityDescriptorSha256 = capabilitySha;
        signedTarget.controllerFeaturesSha256 = controllerFeaturesSha;
        signedTarget.adapterBundleSha256 = adapterBundleSha;
        signedTarget.cpu1AbiVersion = 2;
        signedTarget.fpgaAbiVersion = 1;
        signedTarget.signature = signature;
        signedTarget.signingKeyIdSha256 = signingKeySha;
        signedTarget.productionSigned = true;

        Data::RuntimePackageCompilerDeviceProjection projection;
        projection.projectSlaveNodeId = slaveId;
        projection.slaveNodeId = QStringLiteral("ide:slave:xb6-fixture");
        projection.projectDeviceId = QStringLiteral("embedlabs:project:device:xb6-fixture");
        projection.position = 0;
        projection.stationAddress = 0x1001;
        projection.identity = slave.identity;
        projection.esiSha256 = sha256(esiBytes);
        projection.targetProfileId = signedTarget.profileId;
        projection.adapterId = deviceSource.adapterId;
        projection.adapterVersion = deviceSource.adapterVersion;
        projection.adapterSha256 = adapterFileSha;
        projection.pdoProfileId = QStringLiteral("do16");
        projection.pdoMappings = {
            {QStringLiteral("do16"),
             Data::RuntimePackageCompilerPdoDirection::Output,
             0x1600,
             2,
             true,
             {{QStringLiteral("do0"), 0x7000, 1, 1, QStringLiteral("BOOL")}}}};
        projection.componentBindingIds
            .insert(QStringLiteral("0"), QStringLiteral("embedlabs:component:xb6"));
        projection.semanticBindingIds
            .insert(QStringLiteral("io.digital_output"), QStringLiteral("embedlabs:binding:xb6:do0"));
        projection.symbolMode = Data::RuntimePackageCompilerSymbolMode::ReportOnly;
        projection.manualEnvelope = {
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
        projectProjection.devices = {projection};
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
        request.projectSnapshotEvidence = Data::RuntimePackageCompilerProjectSnapshotEvidence{
            capture};
        request.projectProjection = projectProjection;
        request.topologyEvidence = topology;
        request.sourceArtifacts = sources;
        request.deviceSourceEvidence = {deviceSource};
        request.targetProfile = signedTarget;
    }
};

struct TestEnvironment
{
    QTemporaryDir temporary;
    CompilerFixture fixture;
    QString root;
    QString executable;
    QString key;
    QString provisioning;

    TestEnvironment()
    {
        root = temporary.filePath(QStringLiteral("compiler-root"));
        executable = temporary.filePath(QStringLiteral("fakecompiler"));
        key = temporary.filePath(QStringLiteral("production.pub"));
        provisioning = temporary.filePath(QStringLiteral("provisioning.json"));
        QDir().mkpath(root);
        QFile::copy(QString::fromUtf8(ETHERCAT_PROJECT_COMPILER_FAKE_EXECUTABLE), executable);
        QFile::setPermissions(
            executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        writeFile(key, fixture.publicKey);
        rewriteProfile();
    }

    void rewriteProfile(const QString &executablePath = {})
    {
        const QString path = executablePath.isEmpty() ? executable : executablePath;
        const QByteArray executableBytes = readFile(path);
        const QByteArray profile
            = QStringLiteral(
                  "{\"contract_id\":\"ethercat-ide-project-compiler-contract-v1\","
                  "\"contract_version\":1,\"executable_path\":\"%1\","
                  "\"executable_sha256\":\"%2\","
                  "\"format\":\"ethercat-ide-compiler-provisioning-v1\","
                  "\"format_version\":1,\"production_public_key_path\":\"%3\","
                  "\"production_public_key_sha256\":\"%4\","
                  "\"schema_bundle_sha256\":\"%5\"}\n")
                  .arg(
                      path,
                      QString::fromLatin1(sha256(executableBytes).value().toHex()),
                      key,
                      QString::fromLatin1(sha256(fixture.publicKey).value().toHex()),
                      QString::fromLatin1(sha256("api042-schema-bundle").value().toHex()))
                  .toUtf8();
        writeFile(provisioning, profile);
    }

    std::unique_ptr<ProvisionedRuntimePackageCompilerProvider> provider(
        RuntimePackageCompilerProcessLimits limits = {}) const
    {
        return std::make_unique<ProvisionedRuntimePackageCompilerProvider>(
            Utils::FilePath::fromString(provisioning), Utils::FilePath::fromString(root), limits);
    }
};

bool awaitTerminal(Core::RuntimePackageCompilerJob *job, int timeoutMs = 10000)
{
    if (job->state() == Core::RuntimePackageCompilerJobState::Finished)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(job, &Core::RuntimePackageCompilerJob::finished, &loop, &QEventLoop::quit);
    QObject::connect(
        job, &Core::RuntimePackageCompilerJob::completionFailed, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();
    return timeout.isActive() && job->state() == Core::RuntimePackageCompilerJobState::Finished;
}

const Data::RuntimePackageCompilerCompileResult *compileResult(
    const Core::RuntimePackageCompilerJob &job)
{
    return job.result()
               ? std::get_if<Data::RuntimePackageCompilerCompileResult>(&job.result()->value())
               : nullptr;
}

const Data::RuntimePackageCompilerFinalizeResult *finalizeResult(
    const Core::RuntimePackageCompilerJob &job)
{
    return job.result()
               ? std::get_if<Data::RuntimePackageCompilerFinalizeResult>(&job.result()->value())
               : nullptr;
}

const Data::RuntimePackageCompilerVerifyResult *verifyResult(
    const Core::RuntimePackageCompilerJob &job)
{
    return job.result()
               ? std::get_if<Data::RuntimePackageCompilerVerifyResult>(&job.result()->value())
               : nullptr;
}

Data::RuntimePackageCompilerCanonicalJson signResponse(
    const CompilerFixture &fixture, const Data::RuntimePackageCompilerCompileResult &result)
{
    const QString manifest = QString::fromLatin1(result.manifestSha256->value().toHex());
    const QString requestSha = QString::fromLatin1(result.signRequest->sha256().value().toHex());
    const QString key = QString::fromLatin1(
        fixture.request.targetProfile.signingKeyIdSha256.value().toHex());
    const QString signature = QString::fromLatin1(QByteArray(64, '\x44').toHex());
    const QByteArray projection
        = QStringLiteral(
              "{\"format\":\"ethercat-ecpkg-sign-response-v1\",\"format_version\":1,"
              "\"manifest_sha256\":\"%1\",\"operation_id\":\"%2\","
              "\"policy_revision\":1,\"request_sha256\":\"%3\","
              "\"signature_hex\":\"%4\",\"signing_key_id\":\"%5\"}\n")
              .arg(manifest, fixture.request.operationId.value(), requestSha, signature, key)
              .toLatin1();
    return canonical(QStringLiteral(
                         "{\"format\":\"ethercat-ecpkg-sign-response-v1\",\"format_version\":1,"
                         "\"manifest_sha256\":\"%1\",\"operation_id\":\"%2\","
                         "\"policy_revision\":1,\"receipt_sha256\":\"%3\","
                         "\"request_sha256\":\"%4\",\"signature_hex\":\"%5\","
                         "\"signing_key_id\":\"%6\"}\n")
                         .arg(
                             manifest,
                             fixture.request.operationId.value(),
                             QString::fromLatin1(sha256(projection).value().toHex()),
                             requestSha,
                             signature,
                             key)
                         .toLatin1());
}

Data::RuntimePackageCompilerFinalizeRequest finalizeRequestFor(
    const CompilerFixture &fixture, const Data::RuntimePackageCompilerCompileResult &result)
{
    return {
        fixture.request.operationId,
        fixture.request.configurationId,
        fixture.request.contractIdentity,
        result.envelope.requestSha256,
        result.signRequest->sha256(),
        *result.manifestSha256,
        fixture.request.targetProfile.signingKeyIdSha256,
        fixture.request.targetProfile.policyRevision,
        signResponse(fixture, result),
    };
}

Core::RuntimePackageCompilerActivationProofAssemblyRequest activationProofAssemblyRequest(
    const Data::RuntimePackageCompilerActivationProof &proof)
{
    return {
        proof.compilerProviderId,
        proof.contractIdentity,
        proof.compileRequest,
        proof.compileResult,
        proof.finalizeRequest,
        proof.finalizeResult,
        proof.verifyRequest,
        proof.verifyResult,
    };
}

std::optional<Data::RuntimePackageCompilerActivationProof> successfulActivationProof(
    TestEnvironment &environment,
    ProvisionedRuntimePackageCompilerProvider &provider,
    Data::RuntimePackageCompilerOperationId verifyOperationId
    = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000099")})
{
    const Utils::Result<Core::RuntimePackageCompilerJob *> compileJob = provider.compile(
        environment.fixture.request);
    if (!compileJob || !awaitTerminal(*compileJob))
        return std::nullopt;
    const Data::RuntimePackageCompilerCompileResult *compiled = compileResult(**compileJob);
    if (!compiled || !compiled->isSuccess())
        return std::nullopt;
    const Data::RuntimePackageCompilerCompileResult compileResultCopy = *compiled;

    const Data::RuntimePackageCompilerFinalizeRequest finalizeRequest
        = finalizeRequestFor(environment.fixture, compileResultCopy);
    const Utils::Result<Core::RuntimePackageCompilerJob *> finalizeJob = provider.finalize(
        finalizeRequest);
    if (!finalizeJob || !awaitTerminal(*finalizeJob))
        return std::nullopt;
    const Data::RuntimePackageCompilerFinalizeResult *finalized = finalizeResult(**finalizeJob);
    if (!finalized || !finalized->isSuccess())
        return std::nullopt;
    const Data::RuntimePackageCompilerFinalizeResult finalizeResultCopy = *finalized;

    const Data::RuntimePackageCompilerVerifyRequest verifyRequest{
        std::move(verifyOperationId),
        environment.fixture.request.contractIdentity,
        finalizeResultCopy.packageBytes,
        *finalizeResultCopy.packageSha256,
    };
    const Utils::Result<Core::RuntimePackageCompilerJob *> verifyJob = provider.verify(
        verifyRequest);
    if (!verifyJob || !awaitTerminal(*verifyJob))
        return std::nullopt;
    const Data::RuntimePackageCompilerVerifyResult *verified = verifyResult(**verifyJob);
    if (!verified || !verified->isSuccess())
        return std::nullopt;

    const Core::RuntimePackageCompilerActivationProofAssemblyRequest assemblyRequest{
        QString::fromUtf8(Constants::PROJECT_COMPILER_PROVIDER_ID.name()),
        environment.fixture.request.contractIdentity,
        environment.fixture.request,
        compileResultCopy,
        finalizeRequest,
        finalizeResultCopy,
        verifyRequest,
        *verified,
    };
    const Utils::Result<Data::RuntimePackageCompilerActivationProof> proof
        = provider.assembleActivationProof(assemblyRequest);
    return proof ? std::optional<Data::RuntimePackageCompilerActivationProof>{*proof}
                 : std::nullopt;
}

QByteArray storeWriteFingerprint(const QString &root)
{
    QStringList entries;
    QDirIterator iterator(
        root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        entries.append(QDir(root).relativeFilePath(iterator.next()));
    entries.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &relative : std::as_const(entries)) {
        const QString absolute = QDir(root).filePath(relative);
        const QFileInfo info(absolute);
        hash.addData(relative.toUtf8());
        hash.addData(info.isDir() ? QByteArrayView("D") : QByteArrayView("F"));
        hash.addData(QByteArray::number(quint64(info.permissions())));
#ifdef Q_OS_UNIX
        struct stat metadata = {};
        if (::lstat(QFile::encodeName(absolute).constData(), &metadata) == 0) {
            hash.addData(QByteArray::number(quint64(metadata.st_dev)));
            hash.addData(QByteArray::number(quint64(metadata.st_ino)));
            hash.addData(QByteArray::number(quint64(metadata.st_size)));
#ifdef Q_OS_DARWIN
            hash.addData(QByteArray::number(metadata.st_mtimespec.tv_sec));
            hash.addData(QByteArray::number(metadata.st_mtimespec.tv_nsec));
#else
            hash.addData(QByteArray::number(metadata.st_mtim.tv_sec));
            hash.addData(QByteArray::number(metadata.st_mtim.tv_nsec));
#endif
        }
#else
        hash.addData(QByteArray::number(info.size()));
        hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
#endif
        if (info.isFile())
            hash.addData(readFile(absolute));
    }
    return hash.result();
}

void replaceProofProjectCapture(
    Data::RuntimePackageCompilerActivationProof *proof,
    Data::ProjectSnapshot snapshot,
    QByteArray serializedProject,
    Data::RuntimePackageActivationDocumentRevisionToken documentRevision,
    Data::RuntimePackageActivationOriginalBindingToken originalBinding)
{
    const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
        = proof->compileRequest.projectSnapshotEvidence;
    const Data::RuntimePackageActivationProjectCapture capture{
        std::move(snapshot),
        std::move(serializedProject),
        current.documentRevisionNumber(),
        std::move(documentRevision),
        std::move(originalBinding),
    };
    proof->compileRequest.projectSnapshotEvidence
        = Data::RuntimePackageCompilerProjectSnapshotEvidence{capture};
}

void replaceProofProjectSnapshot(
    Data::RuntimePackageCompilerActivationProof *proof, Data::ProjectSnapshot snapshot)
{
    const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
        = proof->compileRequest.projectSnapshotEvidence;
    replaceProofProjectCapture(
        proof,
        std::move(snapshot),
        QByteArray("mutated-project-snapshot"),
        current.documentRevision(),
        current.originalBinding());
}

} // namespace

void EtherCATProjectCompilerTests::testPluginMetadataAndDefaultAvailability()
{
    QCOMPARE(
        Constants::PROJECT_COMPILER_PROVIDER_ID,
        Utils::Id("EtherCAT.ProjectCompiler.ProvisionedCli"));
    QTemporaryDir temporary;
    ProvisionedRuntimePackageCompilerProvider provider(
        Utils::FilePath::fromString(temporary.filePath(QStringLiteral("missing.json"))),
        Utils::FilePath::fromString(temporary.filePath(QStringLiteral("root"))));
    QVERIFY(!provider.isAvailable());
    QVERIFY(!provider.provisioningError().isEmpty());
}

void EtherCATProjectCompilerTests::testProvisioningRejectsUnsafeExecutables()
{
    TestEnvironment environment;
    const auto loaded = CompilerProvisioningProfile::load(
        Utils::FilePath::fromString(environment.provisioning));
    QVERIFY(bool(loaded));

#ifdef Q_OS_UNIX
    const QString link = environment.temporary.filePath(QStringLiteral("compiler-link"));
    QVERIFY(QFile::link(environment.executable, link));
    environment.rewriteProfile(link);
    QVERIFY(
        !CompilerProvisioningProfile::load(Utils::FilePath::fromString(environment.provisioning)));
#endif

    environment.rewriteProfile();
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    QFile changed(environment.executable);
    QVERIFY(changed.open(QIODevice::Append));
    QCOMPARE(changed.write("drift", 5), 5);
    changed.close();
    QVERIFY(!provider->compile(environment.fixture.request));
    QVERIFY(!QFileInfo::exists(environment.root + QStringLiteral("/compiler-ledger.json.calls")));
}

void EtherCATProjectCompilerTests::testScheduledJobUsesPinnedProvisioning()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());

#ifdef Q_OS_UNIX
    const QString provisionedRoot = provider->compilerRoot().path() + "/provisioned/";
    const QString pinnedExecutable = provisionedRoot
                                     + QString::fromLatin1(
                                         sha256(readFile(environment.executable)).value().toHex())
                                     + QStringLiteral("/compiler");
    const QString pinnedKey = provisionedRoot
                              + QString::fromLatin1(
                                  sha256(environment.fixture.publicKey).value().toHex())
                              + QStringLiteral("/production-public-key.bin");
    struct stat executableInfo = {};
    struct stat keyInfo = {};
    QCOMPARE(::lstat(QFile::encodeName(pinnedExecutable).constData(), &executableInfo), 0);
    QCOMPARE(::lstat(QFile::encodeName(pinnedKey).constData(), &keyInfo), 0);
    QVERIFY(S_ISREG(executableInfo.st_mode));
    QVERIFY(S_ISREG(keyInfo.st_mode));
    QCOMPARE(executableInfo.st_uid, ::geteuid());
    QCOMPARE(keyInfo.st_uid, ::geteuid());
    QCOMPARE(executableInfo.st_nlink, nlink_t(1));
    QCOMPARE(keyInfo.st_nlink, nlink_t(1));
    QCOMPARE(executableInfo.st_mode & 0777, mode_t(0500));
    QCOMPARE(keyInfo.st_mode & 0777, mode_t(0400));
#endif

    auto accepted = environment.fixture.request;
    accepted.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000007")};
    accepted.configurationId = 4207;
    const auto scheduled = provider->compile(accepted);
    QVERIFY(bool(scheduled));

    QFile sourceExecutable(environment.executable);
    QVERIFY(sourceExecutable.open(QIODevice::Append));
    QCOMPARE(sourceExecutable.write("source-drift", 12), 12);
    sourceExecutable.close();

    QVERIFY(awaitTerminal(*scheduled));
    const auto *result = compileResult(**scheduled);
    QVERIFY(result && result->isSuccess());

    auto rejected = environment.fixture.request;
    rejected.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000008")};
    rejected.configurationId = 4208;
    QVERIFY(!provider->compile(rejected));
}

void EtherCATProjectCompilerTests::testStoreAnchorsAncestorsAndPreservesEvidence()
{
#ifndef Q_OS_UNIX
    QSKIP("This test exercises the Unix dirfd implementation.");
#else
    TestEnvironment environment;
    CompilerOperationStore store{Utils::FilePath::fromString(environment.root)};
    QVERIFY(store.initialize());
    Utils::Result<CompilerOperationLease> acquired = store.acquireLease();
    QVERIFY(bool(acquired));
    CompilerOperationLease lease = std::move(*acquired);
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> encoded
        = Core::encodeRuntimePackageCompilerCompileRequest(environment.fixture.request);
    QVERIFY(bool(encoded));
    const Utils::Result<CompilerOperationPaths> reserved
        = store.reserveCompile(lease, environment.fixture.request, *encoded);
    QVERIFY(bool(reserved));

    struct stat before = {};
    const QByteArray requestPath = QFile::encodeName(reserved->compileRequest.path());
    QCOMPARE(::stat(requestPath.constData(), &before), 0);
    const Utils::Result<CompilerOperationPaths> replayed
        = store.reserveCompile(lease, environment.fixture.request, *encoded);
    QVERIFY(bool(replayed));
    struct stat after = {};
    QCOMPARE(::stat(requestPath.constData(), &after), 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);

    const QString packages = reserved->outputDir.path() + QStringLiteral("/packages");
    const QString anchored = reserved->outputDir.path() + QStringLiteral("/packages-anchored");
    const QString external = environment.temporary.filePath(QStringLiteral("external-packages"));
    const QString leafName = QStringLiteral("probe.bin");
    const QString trustedLeaf = packages + '/' + leafName;
    QVERIFY(writeFile(trustedLeaf, QByteArray("trusted")));
    QVERIFY(writeFile(external + '/' + leafName, QByteArray("external")));

    bool renamed = false;
    bool linked = false;
    CompilerOperationStore::setBeforeLeafOpenHookForTest([&] {
        renamed = QDir().rename(packages, anchored);
        if (renamed) {
            linked = ::symlink(
                         QFile::encodeName(external).constData(),
                         QFile::encodeName(packages).constData())
                     == 0;
        }
    });
    const Utils::Result<QByteArray> bytes
        = store.readProviderFile(lease, Utils::FilePath::fromString(trustedLeaf), 1024);
    CompilerOperationStore::setBeforeLeafOpenHookForTest({});
    QVERIFY(renamed);
    QVERIFY(linked);
    QVERIFY(bool(bytes));
    QCOMPARE(*bytes, QByteArray("trusted"));

    QVERIFY(QFile::remove(packages));
    QVERIFY(QDir().rename(anchored, packages));
#endif
}

void EtherCATProjectCompilerTests::testCompileProcessAndImmutableEvidence()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    const auto scheduled = provider->compile(environment.fixture.request);
    QVERIFY(bool(scheduled));
    QVERIFY(awaitTerminal(*scheduled));
    const auto *result = compileResult(**scheduled);
    QVERIFY(result);
    QVERIFY(result->isSuccess());
    QVERIFY(result->signRequest);
    QVERIFY(QFileInfo::exists(result->outputDirectory + QStringLiteral("/project.json")));
    QVERIFY(
        QFileInfo::exists(
            result->outputDirectory + QStringLiteral("/packages/compile_report.json")));
    QVERIFY(
        QFileInfo::exists(
            result->outputDirectory + QStringLiteral("/effective-project-companion-v1.json")));
    QCOMPARE(
        sha256(readFile(result->outputDirectory + QStringLiteral("/sign-request.json"))),
        result->signRequest->sha256());
    const QString evidenceRoot
        = provider->operationRoot(environment.fixture.request.operationId).toFSPathString()
          + QStringLiteral("/evidence/");
    const QList<Data::RuntimePackageCompilerSha256> artifactDigests{
        *result->compiledProjectSha256,
        *result->compileReportSha256,
        *result->effectiveProjectCompanionSha256,
        *result->manifestSha256,
        result->signRequest->sha256(),
    };
    for (const Data::RuntimePackageCompilerSha256 &digest : artifactDigests) {
        QVERIFY(
            QFileInfo::exists(
                evidenceRoot + QString::fromLatin1(digest.value().toHex())
                + QStringLiteral(".bin")));
    }
}

void EtherCATProjectCompilerTests::testFailureAndStrictOutputHandling()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    auto failure = environment.fixture.request;
    failure.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000002")};
    failure.configurationId = 4202;
    const auto failureScheduled = provider->compile(failure);
    QVERIFY(bool(failureScheduled));
    QVERIFY(awaitTerminal(*failureScheduled));
    const auto *domainResult = compileResult(**failureScheduled);
    QVERIFY(domainResult);
    QVERIFY(!domainResult->isSuccess());
    QCOMPARE(domainResult->envelope.status, Data::RuntimePackageCompilerResultStatus::DomainFailed);

    auto extra = environment.fixture.request;
    extra.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000003")};
    extra.configurationId = 4203;
    const auto extraScheduled = provider->compile(extra);
    QVERIFY(bool(extraScheduled));
    QVERIFY(awaitTerminal(*extraScheduled));
    QVERIFY(!(*extraScheduled)->result());
    QVERIFY(
        (*extraScheduled)->completionError()
        != Core::RuntimePackageCompilerJobCompletionError::None);
}

void EtherCATProjectCompilerTests::testCancellationQueriesBeforeCompletion()
{
    TestEnvironment environment;
    RuntimePackageCompilerProcessLimits limits;
    limits.commandTimeout = 10s;
    limits.queryTimeout = 2s;
    limits.cancellationGrace = 50ms;
    auto provider = environment.provider(limits);
    auto request = environment.fixture.request;
    request.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000004")};
    request.configurationId = 4204;
    const auto scheduled = provider->compile(request);
    QVERIFY(bool(scheduled));
    QTRY_COMPARE_WITH_TIMEOUT(
        (*scheduled)->state(), Core::RuntimePackageCompilerJobState::Running, 2000);
    (*scheduled)->cancel();
    QVERIFY(awaitTerminal(*scheduled));
    QVERIFY(!(*scheduled)->result());
    QCOMPARE(
        (*scheduled)->completionError(),
        Core::RuntimePackageCompilerJobCompletionError::CanceledAfterReconciliation);
    const QString ledger = environment.root + QStringLiteral("/compiler-ledger.json");
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(ledger + QStringLiteral(".query-marker")), 3000);
    QVERIFY(readFile(ledger + QStringLiteral(".calls")).contains("query "));

    auto immediate = environment.fixture.request;
    immediate.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000010004")};
    immediate.configurationId = 4214;
    const auto immediateScheduled = provider->compile(immediate);
    QVERIFY(bool(immediateScheduled));
    (*immediateScheduled)->cancel();
    QVERIFY(awaitTerminal(*immediateScheduled));
    QVERIFY(!(*immediateScheduled)->result());
    QCOMPARE(
        (*immediateScheduled)->completionError(),
        Core::RuntimePackageCompilerJobCompletionError::CanceledAfterReconciliation);
    const QByteArray immediateQuery = QByteArray("query ")
                                      + immediate.operationId.value().toLatin1() + '\n';
    QCOMPARE(readFile(ledger + QStringLiteral(".calls")).count(immediateQuery), 1);
}

void EtherCATProjectCompilerTests::testShutdownReapsCompilerBeforeUnlock()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    auto interrupted = environment.fixture.request;
    interrupted.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000020004")};
    interrupted.configurationId = 4224;
    const auto scheduled = provider->compile(interrupted);
    QVERIFY(bool(scheduled));
    QTRY_COMPARE_WITH_TIMEOUT(
        (*scheduled)->state(), Core::RuntimePackageCompilerJobState::Running, 2000);

    provider->shutdown();
    QVERIFY(!provider->isAvailable());
    QCOMPARE((*scheduled)->state(), Core::RuntimePackageCompilerJobState::Finished);
    QCOMPARE(
        (*scheduled)->completionError(),
        Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure);
    const QString interruptedOutput = provider->operationRoot(interrupted.operationId).path()
                                      + QStringLiteral("/output");
    QVERIFY(!QFileInfo::exists(interruptedOutput + QStringLiteral("/project.json")));

    auto replacement = environment.provider();
    QVERIFY(replacement->isAvailable());
    auto resumed = environment.fixture.request;
    resumed.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000020005")};
    resumed.configurationId = 4225;
    const auto resumedJob = replacement->compile(resumed);
    QVERIFY(bool(resumedJob));
    QVERIFY(awaitTerminal(*resumedJob));
    const auto *result = compileResult(**resumedJob);
    QVERIFY(result && result->isSuccess());
    QVERIFY(!QFileInfo::exists(interruptedOutput + QStringLiteral("/project.json")));
}

void EtherCATProjectCompilerTests::testFinalizeQueryVerifyAndRestart()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    const auto compileScheduled = provider->compile(environment.fixture.request);
    QVERIFY(bool(compileScheduled));
    QVERIFY(awaitTerminal(*compileScheduled));
    const auto *compiled = compileResult(**compileScheduled);
    QVERIFY(compiled && compiled->isSuccess());

    const auto finalizeRequest = finalizeRequestFor(environment.fixture, *compiled);
    QVERIFY(finalizeRequest.isValid());
    const auto finalizeScheduled = provider->finalize(finalizeRequest);
    QVERIFY(bool(finalizeScheduled));
    QVERIFY(awaitTerminal(*finalizeScheduled));
    const auto *finalized = finalizeResult(**finalizeScheduled);
    QVERIFY2(
        finalized && finalized->isSuccess(),
        finalized ? finalized->envelope.canonicalResult.exactBytes().constData()
                  : "No trusted finalize result was decoded.");

    const Data::RuntimePackageCompilerQueryRequest queryRequest{
        environment.fixture.request.operationId,
        environment.fixture.request.contractIdentity,
        compiled->envelope.requestSha256,
    };
    const auto queried = provider->query(queryRequest);
    QVERIFY(bool(queried));
    QVERIFY(awaitTerminal(*queried));
    QVERIFY((*queried)->result());

    const Data::RuntimePackageCompilerVerifyRequest verifyRequest{
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000099")},
        environment.fixture.request.contractIdentity,
        finalized->packageBytes,
        *finalized->packageSha256,
    };
    const auto verified = provider->verify(verifyRequest);
    QVERIFY(bool(verified));
    QVERIFY(awaitTerminal(*verified));
    QVERIFY((*verified)->result());
    const auto *verification = std::get_if<Data::RuntimePackageCompilerVerifyResult>(
        &(*verified)->result()->value());
    QVERIFY(verification && verification->trusted);

    provider->shutdown();
    provider.reset();
    auto restarted = environment.provider();
    QVERIFY(restarted->isAvailable());
    const auto replayedQuery = restarted->query(queryRequest);
    QVERIFY(bool(replayedQuery));
    QVERIFY(awaitTerminal(*replayedQuery));
    QVERIFY((*replayedQuery)->result());
    const auto *query = std::get_if<Data::RuntimePackageCompilerQueryResult>(
        &(*replayedQuery)->result()->value());
    QVERIFY(query && query->hasCompilerRecord());
}

void EtherCATProjectCompilerTests::testOperationAndConfigurationConflicts()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    const auto first = provider->compile(environment.fixture.request);
    QVERIFY(bool(first));
    QVERIFY(awaitTerminal(*first));

    auto operationConflict = environment.fixture.request;
    operationConflict.intentId += QStringLiteral(":changed");
    QVERIFY(!provider->compile(operationConflict));

    auto configurationConflict = environment.fixture.request;
    configurationConflict.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000005")};
    QVERIFY(!provider->compile(configurationConflict));

    auto traversal = environment.fixture.request;
    traversal.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000006")};
    traversal.configurationId = 4206;
    traversal.sourceArtifacts.runtimeSource.relativePath = QStringLiteral("../escape.st");
    QVERIFY(!provider->compile(traversal));
    QVERIFY(!QFileInfo::exists(environment.temporary.filePath(QStringLiteral("escape.st"))));
}

void EtherCATProjectCompilerTests::testActivationProofProvenanceAndRestart()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    const auto proof = successfulActivationProof(environment, *provider);
    QVERIFY(proof);
    QVERIFY(proof->isValid());
    QVERIFY(
        proof->compileRequest.projectSnapshotEvidence.serializedProjectSha256()
        == sha256(environment.fixture.serializedProject));
    const Core::RuntimePackageCompilerActivationProofAssemblyRequest assemblyRequest
        = activationProofAssemblyRequest(*proof);
    QVERIFY(assemblyRequest.isValid());

    const QString calls = environment.root + QStringLiteral("/compiler-ledger.json.calls");
    const QByteArray callsBefore = readFile(calls);
    const QByteArray storeBefore = storeWriteFingerprint(environment.root);
    const Utils::Result<Data::RuntimePackageCompilerActivationProof> assembled
        = provider->assembleActivationProof(assemblyRequest);
    QVERIFY(assembled);
    QCOMPARE(*assembled, *proof);
    QVERIFY(provider->validateActivationProof(*proof));
    QCOMPARE(readFile(calls), callsBefore);
    QCOMPARE(storeWriteFingerprint(environment.root), storeBefore);

    const bool availabilityBeforeCrossThreadCalls = provider->isAvailable();
    bool crossThreadValidationSucceeded = true;
    bool crossThreadAssemblySucceeded = true;
    std::thread worker([&] {
        crossThreadValidationSucceeded = bool(provider->validateActivationProof(*proof));
        crossThreadAssemblySucceeded = bool(provider->assembleActivationProof(assemblyRequest));
    });
    worker.join();
    QVERIFY(!crossThreadValidationSucceeded);
    QVERIFY(!crossThreadAssemblySucceeded);
    QCOMPARE(provider->isAvailable(), availabilityBeforeCrossThreadCalls);
    QCOMPARE(readFile(calls), callsBefore);
    QCOMPARE(storeWriteFingerprint(environment.root), storeBefore);

#ifdef Q_OS_UNIX
    QFile writerLock(environment.root + QStringLiteral("/.compiler.lock"));
    QVERIFY(writerLock.open(QIODevice::ReadWrite));
    QCOMPARE(::flock(writerLock.handle(), LOCK_EX | LOCK_NB), 0);
    QVERIFY(!provider->validateActivationProof(*proof));
    QCOMPARE(readFile(calls), callsBefore);
    QCOMPARE(storeWriteFingerprint(environment.root), storeBefore);
    QCOMPARE(::flock(writerLock.handle(), LOCK_UN), 0);
    writerLock.close();
    QVERIFY(provider->validateActivationProof(*proof));
#endif

    provider->shutdown();
    provider.reset();
    provider = environment.provider();
    QVERIFY(provider->isAvailable());
    const QByteArray restartedCalls = readFile(calls);
    const QByteArray restartedStore = storeWriteFingerprint(environment.root);
    const Utils::Result<Data::RuntimePackageCompilerActivationProof> restartedAssembly
        = provider->assembleActivationProof(assemblyRequest);
    QVERIFY(restartedAssembly);
    QCOMPARE(*restartedAssembly, *proof);
    QVERIFY(provider->validateActivationProof(*proof));
    QCOMPARE(readFile(calls), restartedCalls);
    QCOMPARE(storeWriteFingerprint(environment.root), restartedStore);

    provider->shutdown();
    provider.reset();
    environment.fixture.publicKey = QByteArray(32, '\x51');
    QVERIFY(writeFile(environment.key, environment.fixture.publicKey));
    environment.rewriteProfile();
    provider = environment.provider();
    QVERIFY(provider->isAvailable());
    const QByteArray changedKeyCalls = readFile(calls);
    QVERIFY(!provider->validateActivationProof(*proof));
    QCOMPARE(readFile(calls), changedKeyCalls);
}

void EtherCATProjectCompilerTests::testActivationProofRejectsMutationsWithoutProcess()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    const auto proof = successfulActivationProof(environment, *provider);
    QVERIFY(proof);
    const QString calls = environment.root + QStringLiteral("/compiler-ledger.json.calls");

    const auto rejectedWithoutProcess =
        [&](const Data::RuntimePackageCompilerActivationProof &value) {
            const QByteArray before = readFile(calls);
            const bool rejected = !provider->validateActivationProof(value);
            return rejected && readFile(calls) == before;
        };
    const auto assemblyRejectedWithoutProcess =
        [&](const Core::RuntimePackageCompilerActivationProofAssemblyRequest &request) {
            const QByteArray before = readFile(calls);
            const bool rejected = !provider->assembleActivationProof(request);
            return rejected && readFile(calls) == before;
        };

    Core::RuntimePackageCompilerActivationProofAssemblyRequest changedAssembly
        = activationProofAssemblyRequest(*proof);
    changedAssembly.compileRequest.intentId.append(QStringLiteral(".mutated"));
    QVERIFY(changedAssembly.isValid());
    QVERIFY(assemblyRejectedWithoutProcess(changedAssembly));

    changedAssembly = activationProofAssemblyRequest(*proof);
    changedAssembly.finalizeResult.packageBytes.append("different-package");
    QVERIFY(!changedAssembly.isValid());
    QVERIFY(assemblyRejectedWithoutProcess(changedAssembly));

    Data::RuntimePackageCompilerActivationProof changed = *proof;
    changed.compilerProviderId = QStringLiteral("EtherCAT.ProjectCompiler.Relabeled");
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    changed.finalizeRequestSha256 = sha256("different-finalize-request");
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    changed.verifyRequestSha256 = sha256("different-verify-request");
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    changed.compileRequest.intentId.append(QStringLiteral(".mutated"));
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    ++changed.compileRequest.buildTimestampNs;
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    ++changed.compileRequest.projectProjection.devices[0].manualEnvelope.maximumTtlCycles;
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    Data::ProjectSnapshot pdoProject = changed.compileRequest.projectSnapshotEvidence.snapshot();
    ++pdoProject.slaves[0].processData.pdos[0].entries[0].index;
    ++changed.compileRequest.projectProjection.devices[0].pdoMappings[0].entries[0].index;
    replaceProofProjectSnapshot(&changed, std::move(pdoProject));
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    Data::ProjectSnapshot sdoProject = changed.compileRequest.projectSnapshotEvidence.snapshot();
    Data::StartupParameterConfiguration startup;
    startup.id = Data::NodeId::create();
    startup.order = 1;
    startup.transition = QStringLiteral("preop");
    startup.index = 0x2000;
    startup.subIndex = 1;
    startup.dataType = Data::EtherCATDataType::UnsignedInteger16;
    startup.rawValue = QByteArray::fromHex("0001");
    sdoProject.slaves[0].startup.parameters.append(startup);
    changed.compileRequest.projectProjection.devices[0].startupSdos.append({
        1,
        QStringLiteral("startup.fixture"),
        true,
        Data::RuntimePackageCompilerStartupStage::PreOperational,
        0x2000,
        1,
        quint64(1),
        2,
        false,
        1'000'000,
        0,
        Data::RuntimePackageCompilerStartupFailureAction::Abort,
        false,
        false,
    });
    replaceProofProjectSnapshot(&changed, std::move(sdoProject));
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    changed.compileRequest.projectProjection.devices[0].adapterId.append(QStringLiteral(".mutated"));
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    {
        const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
            = changed.compileRequest.projectSnapshotEvidence;
        replaceProofProjectCapture(
            &changed,
            current.snapshot(),
            environment.fixture.serializedProject + QByteArray("changed"),
            current.documentRevision(),
            current.originalBinding());
    }
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    {
        const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
            = changed.compileRequest.projectSnapshotEvidence;
        replaceProofProjectCapture(
            &changed,
            current.snapshot(),
            environment.fixture.serializedProject,
            Data::RuntimePackageActivationDocumentRevisionToken{"revision-spliced"},
            current.originalBinding());
    }
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    {
        const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
            = changed.compileRequest.projectSnapshotEvidence;
        replaceProofProjectCapture(
            &changed,
            current.snapshot(),
            environment.fixture.serializedProject,
            current.documentRevision(),
            Data::RuntimePackageActivationOriginalBindingToken{"binding-spliced"});
    }
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    {
        const Data::NodeId oldProjectId = changed.compileRequest.topologyEvidence.scope.projectId;
        const Data::NodeId newProjectId = Data::NodeId::create();
        Data::ProjectSnapshot snapshot = changed.compileRequest.projectSnapshotEvidence.snapshot();
        snapshot.id = newProjectId;
        for (Data::ProjectNodeSnapshot &node : snapshot.nodes) {
            if (node.id == oldProjectId)
                node.id = newProjectId;
            if (node.parentId == oldProjectId)
                node.parentId = newProjectId;
        }
        changed.compileRequest.topologyEvidence.scope.projectId = newProjectId;
        changed.compileRequest.projectProjection.projectNodeId = newProjectId;
        const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
            = changed.compileRequest.projectSnapshotEvidence;
        replaceProofProjectCapture(
            &changed,
            std::move(snapshot),
            environment.fixture.serializedProject,
            current.documentRevision(),
            current.originalBinding());
    }
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changed = *proof;
    {
        const Data::NodeId oldMasterId = changed.compileRequest.topologyEvidence.scope.masterId;
        const Data::NodeId newMasterId = Data::NodeId::create();
        Data::ProjectSnapshot snapshot = changed.compileRequest.projectSnapshotEvidence.snapshot();
        for (Data::ProjectNodeSnapshot &node : snapshot.nodes) {
            if (node.id == oldMasterId)
                node.id = newMasterId;
            if (node.parentId == oldMasterId)
                node.parentId = newMasterId;
        }
        for (Data::OfflineSlaveConfiguration &slave : snapshot.slaves) {
            if (slave.masterId == oldMasterId)
                slave.masterId = newMasterId;
        }
        changed.compileRequest.topologyEvidence.scope.masterId = newMasterId;
        changed.compileRequest.projectProjection.masterProjectNodeId = newMasterId;
        const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
            = changed.compileRequest.projectSnapshotEvidence;
        replaceProofProjectCapture(
            &changed,
            std::move(snapshot),
            environment.fixture.serializedProject,
            current.documentRevision(),
            current.originalBinding());
    }
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    const Data::RuntimePackageCompilerCompileRequest originalRequest = environment.fixture.request;
    environment.fixture.request.operationId = Data::RuntimePackageCompilerOperationId{
        QStringLiteral("04204204-2001-4000-8000-000000000012")};
    environment.fixture.request.configurationId = 4202;
    environment.fixture.request.intentId.append(QStringLiteral(".second-operation"));
    {
        const Data::RuntimePackageCompilerProjectSnapshotEvidence &current
            = environment.fixture.request.projectSnapshotEvidence;
        const Data::RuntimePackageActivationProjectCapture secondCapture{
            current.snapshot(),
            environment.fixture.serializedProject + QByteArray("second-operation"),
            current.documentRevisionNumber(),
            Data::RuntimePackageActivationDocumentRevisionToken{"revision-second-operation"},
            Data::RuntimePackageActivationOriginalBindingToken{"binding-second-operation"},
        };
        environment.fixture.request.projectSnapshotEvidence
            = Data::RuntimePackageCompilerProjectSnapshotEvidence{secondCapture};
    }
    const auto secondOperationProof = successfulActivationProof(
        environment,
        *provider,
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-000000000098")});
    environment.fixture.request = originalRequest;
    QVERIFY(secondOperationProof);
    QCOMPARE(
        secondOperationProof->compileRequest.projectProjection,
        proof->compileRequest.projectProjection);
    changed = *proof;
    changed.compileRequest.projectSnapshotEvidence
        = secondOperationProof->compileRequest.projectSnapshotEvidence;
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    changedAssembly = activationProofAssemblyRequest(*proof);
    changedAssembly.verifyRequest = secondOperationProof->verifyRequest;
    changedAssembly.verifyResult = secondOperationProof->verifyResult;
    QVERIFY(changedAssembly.isValid());
    QVERIFY(assemblyRejectedWithoutProcess(changedAssembly));

    changed = *proof;
    changed.verifyRequest.operationId = changed.compileRequest.operationId;
    changed.verifyResult.envelope.operationId = changed.compileRequest.operationId;
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> sameOperationVerify
        = Core::encodeRuntimePackageCompilerVerifyRequest(changed.verifyRequest);
    QVERIFY(sameOperationVerify);
    changed.verifyRequestSha256 = sameOperationVerify->sha256();
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));

    TestEnvironment foreignEnvironment;
    auto foreignProvider = foreignEnvironment.provider();
    const auto foreignProof = successfulActivationProof(foreignEnvironment, *foreignProvider);
    QVERIFY(foreignProof);
    QVERIFY(rejectedWithoutProcess(*foreignProof));

    changed = *proof;
    changed.compileResult = foreignProof->compileResult;
    QVERIFY(changed.isValid());
    QVERIFY(rejectedWithoutProcess(changed));
}

void EtherCATProjectCompilerTests::testActivationProofRejectsUnsafeEvidence()
{
    TestEnvironment environment;
    auto provider = environment.provider();
    const auto proof = successfulActivationProof(environment, *provider);
    QVERIFY(proof);
    const Utils::FilePath compileRoot = provider->operationRoot(proof->compileRequest.operationId);
    const Utils::FilePath verifyRoot = provider->operationRoot(proof->verifyRequest.operationId);
    const Utils::FilePath compilerRoot = compileRoot.parentDir().parentDir();
    const QString calls = environment.root + QStringLiteral("/compiler-ledger.json.calls");
    const Core::RuntimePackageCompilerActivationProofAssemblyRequest assemblyRequest
        = activationProofAssemblyRequest(*proof);

    const auto rejectsCurrentStore = [&] {
        const QByteArray before = readFile(calls);
        const bool rejected = !provider->validateActivationProof(*proof)
                              && !provider->assembleActivationProof(assemblyRequest);
        return rejected && readFile(calls) == before;
    };
    const auto corruptAndRestore = [&](const Utils::FilePath &file) {
        const QByteArray original = readFile(file.path());
        if (original.isEmpty() || !writeFile(file.path(), original + 'x'))
            return false;
        const bool rejected = rejectsCurrentStore();
        return writeFile(file.path(), original) && rejected;
    };

    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> compileRequest
        = Core::encodeRuntimePackageCompilerCompileRequest(proof->compileRequest);
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> finalizeRequest
        = Core::encodeRuntimePackageCompilerFinalizeRequest(proof->finalizeRequest);
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> verifyRequest
        = Core::encodeRuntimePackageCompilerVerifyRequest(proof->verifyRequest);
    QVERIFY(compileRequest);
    QVERIFY(finalizeRequest);
    QVERIFY(verifyRequest);

    const auto evidenceFile = [](const Utils::FilePath &root,
                                 const Data::RuntimePackageCompilerSha256 &digest,
                                 const QString &suffix) {
        return root / "evidence" / (QString::fromLatin1(digest.value().toHex()) + suffix);
    };
    const Utils::FilePath activationCapture = compileRoot / "activation-capture-v1.bin";
    const QByteArray activationCaptureBytes = readFile(activationCapture.path());
    QVERIFY(!activationCaptureBytes.isEmpty());
    const Utils::FilePath sealedActivationCapture
        = evidenceFile(compileRoot, sha256(activationCaptureBytes), QStringLiteral(".bin"));
    const QByteArray activationCaptureDomain = QByteArray(
        "embed-labs.runtime-package-compiler.activation-capture-evidence.v1");
    QByteArray activationCapturePrefix(4, '\0');
    activationCapturePrefix[3] = char(activationCaptureDomain.size());
    activationCapturePrefix += activationCaptureDomain;
    QVERIFY(activationCaptureBytes.startsWith(activationCapturePrefix));

    QList<Utils::FilePath> criticalFiles{
        compileRoot / "reservation.json",
        compilerRoot / "configurations" / QString::number(proof->compileRequest.configurationId)
            / "reservation.json",
        compileRoot / "compile-request.json",
        evidenceFile(compileRoot, compileRequest->sha256(), QStringLiteral(".json")),
        activationCapture,
        sealedActivationCapture,
        compileRoot / "finalize-request.json",
        evidenceFile(compileRoot, finalizeRequest->sha256(), QStringLiteral(".json")),
        compileRoot / "sign-request.json",
        evidenceFile(compileRoot, proof->compileResult.signRequest->sha256(), QStringLiteral(".json")),
        compileRoot / "sign-response.json",
        evidenceFile(
            compileRoot,
            proof->finalizeRequest.detachedSigningResponse.sha256(),
            QStringLiteral(".json")),
        evidenceFile(
            compileRoot,
            proof->compileResult.envelope.canonicalResult.sha256(),
            QStringLiteral(".json")),
        evidenceFile(
            compileRoot,
            proof->finalizeResult.envelope.canonicalResult.sha256(),
            QStringLiteral(".json")),
        compileRoot / "production-public-key.bin",
        compileRoot / "output" / "project.json",
        evidenceFile(compileRoot, *proof->compileResult.compiledProjectSha256, QStringLiteral(".bin")),
        compileRoot / "output" / "packages" / "compile_report.json",
        evidenceFile(compileRoot, *proof->compileResult.compileReportSha256, QStringLiteral(".bin")),
        compileRoot / "output" / "effective-project-companion-v1.json",
        evidenceFile(
            compileRoot,
            *proof->compileResult.effectiveProjectCompanionSha256,
            QStringLiteral(".bin")),
        compileRoot / "output" / "signing_stage" / "manifest.json",
        evidenceFile(compileRoot, *proof->compileResult.manifestSha256, QStringLiteral(".bin")),
        compileRoot / "output" / "sign-request.json",
        evidenceFile(compileRoot, proof->compileResult.signRequest->sha256(), QStringLiteral(".bin")),
        compileRoot / "package.ecpkg",
        evidenceFile(compileRoot, *proof->finalizeResult.packageSha256, QStringLiteral(".ecpkg")),
        verifyRoot / "reservation.json",
        verifyRoot / "verify-request.json",
        evidenceFile(verifyRoot, verifyRequest->sha256(), QStringLiteral(".json")),
        evidenceFile(
            verifyRoot,
            proof->verifyResult.envelope.canonicalResult.sha256(),
            QStringLiteral(".json")),
        verifyRoot / "package.ecpkg",
        evidenceFile(verifyRoot, *proof->finalizeResult.packageSha256, QStringLiteral(".ecpkg")),
    };
    const QList<Data::RuntimePackageCompilerSourceArtifact> fixedSourceArtifacts{
        proof->compileRequest.sourceArtifacts.topologyEvidence,
        proof->compileRequest.sourceArtifacts.targetProfile,
        proof->compileRequest.sourceArtifacts.targetProfileSignature,
        proof->compileRequest.sourceArtifacts.productionPublicKey,
        proof->compileRequest.sourceArtifacts.adapterBundle,
        proof->compileRequest.sourceArtifacts.policyTemplate,
        proof->compileRequest.sourceArtifacts.controllerFeatures,
        proof->compileRequest.sourceArtifacts.runtimeSource,
    };
    for (const Data::RuntimePackageCompilerSourceArtifact &artifact : fixedSourceArtifacts)
        criticalFiles.append(compileRoot / "artifacts" / artifact.relativePath);
    for (const Data::RuntimePackageCompilerDeviceSourceEvidence &device :
         proof->compileRequest.deviceSourceEvidence) {
        criticalFiles.append(compileRoot / "artifacts" / device.originalEsi.relativePath);
        criticalFiles.append(compileRoot / "artifacts" / device.adapterSourceFile.relativePath);
    }
    for (const Utils::FilePath &file : criticalFiles)
        QVERIFY2(corruptAndRestore(file), qPrintable(file.toUserOutput()));

    const Utils::FilePath removable = activationCapture;
    const QString removedBackup = removable.path() + QStringLiteral(".removed");
    QVERIFY(QFile::rename(removable.path(), removedBackup));
    provider->shutdown();
    provider.reset();
    provider = environment.provider();
    QVERIFY(provider->isAvailable());
    QVERIFY(rejectsCurrentStore());
    QVERIFY(QFile::rename(removedBackup, removable.path()));
    QVERIFY(provider->validateActivationProof(*proof));

    const QByteArray originalActivationCapture = readFile(activationCapture.path());
    QVERIFY(!originalActivationCapture.isEmpty());
    QVERIFY(writeFile(activationCapture.path(), QByteArray(1024 * 1024 + 1, 'x')));
    QVERIFY(rejectsCurrentStore());
    QVERIFY(writeFile(activationCapture.path(), originalActivationCapture));

#ifdef Q_OS_UNIX
    const Utils::FilePath target = activationCapture;
    const QString backup = target.path() + QStringLiteral(".original");
    QVERIFY(QFile::rename(target.path(), backup));
    QCOMPARE(
        ::link(QFile::encodeName(backup).constData(), QFile::encodeName(target.path()).constData()),
        0);
    QVERIFY(rejectsCurrentStore());
    QVERIFY(QFile::remove(target.path()));
    QVERIFY(QFile::rename(backup, target.path()));

    QVERIFY(QFile::rename(target.path(), backup));
    QCOMPARE(
        ::symlink(QFile::encodeName(backup).constData(), QFile::encodeName(target.path()).constData()),
        0);
    QVERIFY(rejectsCurrentStore());
    QVERIFY(QFile::remove(target.path()));
    QVERIFY(QFile::rename(backup, target.path()));
#endif
}

} // namespace EtherCAT::ProjectCompiler::Internal
