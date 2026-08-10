// Copyright (C) 2026 Embed Labs

#include "ethercatprojectcompilertests.h"

#include "compileroperationstore.h"
#include "compilerpythonruntimeprofile.h"
#include "compilerinputprovisioningprofile.h"
#include "compilerprovisioningprofile.h"
#include "compilerruntimebundleprofile.h"
#include "durableruntimepackagecompilerpreparationcoordinator.h"
#include "ethercatprojectcompilerconstants.h"
#include "provisionedruntimepackagecompilerprovider.h"
#include "provisionedruntimepackagecompilerprojectrequestbuilder.h"
#include "runtimepackagecompilercompilerecoverycodec.h"
#include "runtimepackagecompilerpreparationjournal.h"

#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/offlineconfiguration.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/runtimepackageactivation.h>
#include <ethercatdata/runtimepackagecompiler.h>

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/providers.h>
#include <ethercatcore/runtimepackagecompilercodec.h>
#include <ethercatcore/runtimepackagecompilerprojectrequestbuilder.h>
#include <ethercatcore/runtimepackagecompilerprovider.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/filepath.h>

#include <monocypher-ed25519.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <optional>
#include <thread>
#include <tuple>

#ifdef Q_OS_UNIX
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_DARWIN
#include <sys/acl.h>
#endif
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

void appendPythonJsonString(QByteArray &output, const QString &value)
{
    static constexpr char hex[] = "0123456789abcdef";
    output.append('"');
    for (QChar character : value) {
        const ushort code = character.unicode();
        switch (code) {
        case '"':
            output.append("\\\"");
            break;
        case '\\':
            output.append("\\\\");
            break;
        case '\b':
            output.append("\\b");
            break;
        case '\t':
            output.append("\\t");
            break;
        case '\n':
            output.append("\\n");
            break;
        case '\f':
            output.append("\\f");
            break;
        case '\r':
            output.append("\\r");
            break;
        default:
            if (code >= 0x20 && code <= 0x7e) {
                output.append(char(code));
            } else {
                output.append("\\u");
                output.append(hex[(code >> 12) & 0xf]);
                output.append(hex[(code >> 8) & 0xf]);
                output.append(hex[(code >> 4) & 0xf]);
                output.append(hex[code & 0xf]);
            }
            break;
        }
    }
    output.append('"');
}

bool appendPythonCanonicalJson(QByteArray &output, const QJsonValue &value)
{
    if (value.isNull()) {
        output.append("null");
        return true;
    }
    if (value.isBool()) {
        output.append(value.toBool() ? "true" : "false");
        return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number != std::floor(number))
            return false;
        output.append(QByteArray::number(qint64(number)));
        return true;
    }
    if (value.isString()) {
        appendPythonJsonString(output, value.toString());
        return true;
    }
    if (value.isArray()) {
        output.append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index)
                output.append(',');
            if (!appendPythonCanonicalJson(output, array.at(index)))
                return false;
        }
        output.append(']');
        return true;
    }
    if (value.isObject()) {
        output.append('{');
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        for (qsizetype index = 0; index < keys.size(); ++index) {
            if (index)
                output.append(',');
            appendPythonJsonString(output, keys.at(index));
            output.append(':');
            if (!appendPythonCanonicalJson(output, object.value(keys.at(index))))
                return false;
        }
        output.append('}');
        return true;
    }
    return false;
}

QByteArray pythonCanonicalJson(const QJsonValue &value)
{
    QByteArray result;
    if (!appendPythonCanonicalJson(result, value))
        return {};
    result.append('\n');
    return result;
}

struct CompilerFixture
{
    Data::NodeId projectId = Data::NodeId::fromString("11111111-1111-4111-8111-111111111111");
    Data::NodeId masterId = Data::NodeId::fromString("22222222-2222-4222-8222-222222222222");
    Data::NodeId slaveId = Data::NodeId::fromString("33333333-3333-4333-8333-333333333333");
    Data::NodeId deviceDescriptionId
        = Data::NodeId::fromString("66666666-6666-4666-8666-666666666666");
    QByteArray publicKey;
    QByteArray signature;
    QByteArray serializedProject = QByteArray("{\"format\":\"embed-labs-ethercat-project\"}\n");
    Data::RuntimePackageCompilerCompileRequest request;

    CompilerFixture()
    {
        QByteArray signingSeed(32, '\x31');
        QByteArray secretKey(64, '\0');
        publicKey.resize(32);
        crypto_ed25519_key_pair(
            reinterpret_cast<uint8_t *>(secretKey.data()),
            reinterpret_cast<uint8_t *>(publicKey.data()),
            reinterpret_cast<uint8_t *>(signingSeed.data()));

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
        signature.resize(64);
        crypto_ed25519_sign(
            reinterpret_cast<uint8_t *>(signature.data()),
            reinterpret_cast<const uint8_t *>(secretKey.constData()),
            reinterpret_cast<const uint8_t *>(target.constData()),
            size_t(target.size()));
        secretKey.fill('\0');
        signingSeed.fill('\0');

        Data::ProjectSnapshot project;
        project.id = projectId;
        project.name = QStringLiteral("Compiler fixture");
        project.formatVersion = 8;
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
        slave.deviceDescriptionId = deviceDescriptionId;
        slave.stationAddress = 0x1001;
        slave.alias = 0x002a;
        slave.esiSha256 = sha256(esiBytes).value();
        slave.adapterSelection.adapterId = {"org.embedlabs.adapter.solidot.xb6-fixture"};
        slave.adapterSelection.adapterVersion = QStringLiteral("0.3.0");
        slave.adapterSelection.adapterContentSha256 = sha256("upper-adapter").value();
        slave.adapterSelection.processDataProfileId = QStringLiteral("xb6.do16");
        slave.adapterSelection.moduleAssignments = {{1, 0x00000624, 0, 0}};
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
            Data::runtimePackageActivationBindingToken(project.masterBindingArtifact),
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
        topology.slaves = {{slaveId,
                            0,
                            0x1001,
                            0x002a,
                            slave.identity,
                            0,
                            {{1, 0x00000624, 0, 0}}}};
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
                  "\"link_speed_mbps\":100,\"slaves\":[{\"alias\":42,"
                  "\"identity\":{\"product_code\":182,\"revision\":1,"
                  "\"vendor_id\":8930371},\"modules\":[{\"module_ident\":1572,"
                  "\"slot\":1}],\"position\":0,"
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
        projection.alias = 0x002a;
        projection.identity = slave.identity;
        projection.esiSha256 = sha256(esiBytes);
        projection.targetProfileId = signedTarget.profileId;
        projection.adapterId = deviceSource.adapterId;
        projection.adapterVersion = deviceSource.adapterVersion;
        projection.adapterSha256 = adapterFileSha;
        projection.pdoProfileId = QStringLiteral("do16");
        projection.moduleAssignments = {{1, 0x00000624, 0, 0}};
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

bool awaitJobState(
    Core::RuntimePackageCompilerJob *job,
    Core::RuntimePackageCompilerJobState expected,
    int timeoutMs = 2000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (job->state() != expected && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return job->state() == expected;
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

Data::RuntimePackageActivationProjectCapture projectCapture(const CompilerFixture &fixture)
{
    const Data::RuntimePackageCompilerProjectSnapshotEvidence &evidence
        = fixture.request.projectSnapshotEvidence;
    return {
        evidence.snapshot(),
        fixture.serializedProject,
        evidence.documentRevisionNumber(),
        evidence.documentRevision(),
        evidence.originalBinding(),
    };
}

class BuilderProjectService final : public Core::ProjectService
{
public:
    explicit BuilderProjectService(const CompilerFixture &fixture)
        : Core::ProjectService(
              Utils::Id("EtherCAT.ProjectCompiler.Test.Project"), QStringLiteral("Test project"))
        , m_capture(projectCapture(fixture))
    {
        setAvailable(true);
    }

    QList<Data::ProjectSnapshot> projects() const final { return {m_capture.snapshot()}; }
    std::optional<Data::ProjectSnapshot> project(const Data::NodeId &id) const final
    {
        return id == m_capture.snapshot().id ? std::optional(m_capture.snapshot()) : std::nullopt;
    }
    Data::NodeId activeProjectId() const final { return m_capture.snapshot().id; }
    bool managesProject(const QObject *) const final { return false; }
    Utils::Result<> activateProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> renameProject(const Data::NodeId &, const QString &) final { return unsupported(); }
    Utils::Result<> saveProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> undoProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> redoProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> setMasterConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::MasterConfiguration &) final
    {
        return unsupported();
    }
    Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &, const Data::NodeId &, const QList<Data::OfflineSlaveConfiguration> &) final
    {
        return unsupported();
    }
    Utils::Result<> setProcessDataConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::ProcessDataConfiguration &) final
    {
        return unsupported();
    }
    Utils::Result<> setStartupConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::StartupConfiguration &) final
    {
        return unsupported();
    }
    Utils::Result<> setDcConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::DcConfiguration &) final
    {
        return unsupported();
    }
    bool canUndoProject(const Data::NodeId &) const final { return false; }
    bool canRedoProject(const Data::NodeId &) const final { return false; }
    Utils::Result<> renameStructuralNode(
        const Data::NodeId &, const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }
    Utils::Result<> setDeviceAdapterSelection(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &) final
    {
        return unsupported();
    }
    Utils::Result<> setDeviceParameterConfiguration(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &,
        const Data::DeviceParameterConfiguration &) final
    {
        return unsupported();
    }
    Utils::Result<> setManualControlEnvelope(
        const Data::NodeId &, const Data::NodeId &, const Data::ManualControlEnvelope &) final
    {
        return unsupported();
    }
    Utils::Result<> setMasterBindingArtifact(
        const Data::NodeId &, const Data::SemanticBindingArtifactReference &) final
    {
        return unsupported();
    }
    Utils::Result<Data::RuntimePackageActivationProjectCapture>
    captureRuntimePackageActivationProject(const Data::NodeId &id) const final
    {
        return id == m_capture.snapshot().id
                   ? Utils::Result<Data::RuntimePackageActivationProjectCapture>{m_capture}
                   : Utils::Result<Data::RuntimePackageActivationProjectCapture>{
                         Utils::ResultError(QStringLiteral("Unexpected project."))};
    }
    Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult>
    compareAndSetMasterBindingArtifact(
        const Data::NodeId &,
        const Data::RuntimePackageActivationDocumentRevisionToken &,
        const Data::RuntimePackageActivationOriginalBindingToken &,
        const Data::SemanticBindingArtifactReference &) final
    {
        return Utils::ResultError(QStringLiteral("Mutation is not supported."));
    }

    void setCapture(Data::RuntimePackageActivationProjectCapture capture)
    {
        m_capture = std::move(capture);
    }

private:
    static Utils::Result<> unsupported()
    {
        return Utils::ResultError(QStringLiteral("Mutation is not supported."));
    }
    Data::RuntimePackageActivationProjectCapture m_capture;
};

class BuilderDeviceRepository final : public Core::DeviceRepositoryProvider
{
public:
    explicit BuilderDeviceRepository(const CompilerFixture &fixture)
        : Core::DeviceRepositoryProvider(
              Utils::Id("EtherCAT.ProjectCompiler.Test.ESI"), QStringLiteral("Test ESI"))
        , m_id(fixture.deviceDescriptionId)
        , m_xml(fixture.request.deviceSourceEvidence.constFirst().originalEsi.exactBytes)
    {
        const Data::OfflineSlaveConfiguration &slave
            = fixture.request.projectSnapshotEvidence.snapshot().slaves.constFirst();
        m_description.summary.id = m_id;
        m_description.summary.identity = slave.identity;
        m_description.sourceSha256 = slave.esiSha256;
        setAvailable(true);
    }

    QList<Data::DeviceSummary> devices(const Data::DeviceFilter &) const final
    {
        return {m_description.summary};
    }
    std::optional<Data::DeviceDescription> device(const Data::NodeId &id) const final
    {
        return id == m_id ? std::optional(m_description) : std::nullopt;
    }
    QByteArray originalXml(const Data::NodeId &id) const final { return id == m_id ? m_xml : QByteArray{}; }
    Core::DeviceImportJob *importFiles(const Utils::FilePaths &) final { return nullptr; }
    Core::DeviceImportJob *rebuildIndex() final { return nullptr; }
    bool isIndexing() const final { return false; }

private:
    Data::NodeId m_id;
    QByteArray m_xml;
    Data::DeviceDescription m_description;
};

class BuilderAdapterProvider final : public Core::DeviceAdapterProvider
{
public:
    explicit BuilderAdapterProvider(const CompilerFixture &fixture)
        : Core::DeviceAdapterProvider(
              Utils::Id("EtherCAT.ProjectCompiler.Test.Adapter"), QStringLiteral("Test adapter"))
    {
        const Data::OfflineSlaveConfiguration &slave
            = fixture.request.projectSnapshotEvidence.snapshot().slaves.constFirst();
        const Data::RuntimePackageCompilerDeviceSourceEvidence &source
            = fixture.request.deviceSourceEvidence.constFirst();
        m_manifest.contractVersion = Data::DeviceAdapterContractVersion::V3;
        m_manifest.id = slave.adapterSelection.adapterId;
        m_manifest.version = slave.adapterSelection.adapterVersion;
        m_manifest.match = {slave.identity.vendorId,
                            slave.identity.productCode,
                            slave.identity.revisionNumber,
                            slave.identity.revisionNumber,
                            slave.esiSha256};
        Data::ProcessDataProfile profile;
        profile.id = slave.adapterSelection.processDataProfileId;
        profile.signedPdoProfileId = source.projectSignedPdoProfileId;
        profile.signedDcProfileId = source.projectSignedDcProfileId;
        for (const Data::PdoConfiguration &pdo : slave.processData.pdos) {
            if (!pdo.selected)
                continue;
            (pdo.direction == Data::PdoDirection::Rx ? profile.rxPdoIndices
                                                     : profile.txPdoIndices)
                .append(pdo.index);
        }
        m_manifest.processDataProfiles = {profile};
        m_manifest.controllerAdapterTarget = source.projectControllerAdapterTarget;
        m_manifest.contentSha256 = slave.adapterSelection.adapterContentSha256;
        m_manifest.signatureVerified = true;
        m_manifest.realHardwareAllowed = true;
        setAvailable(true);
    }

    QList<Data::DeviceAdapterManifest> adapterManifests() const final { return {m_manifest}; }
    std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &id, const QString &version) const final
    {
        return id == m_manifest.id && version == m_manifest.version ? std::optional(m_manifest)
                                                                    : std::nullopt;
    }
    Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &) const final
    {
        return {};
    }

private:
    Data::DeviceAdapterManifest m_manifest;
};

class BuilderConnectionProvider final : public Core::ControllerConnectionProvider
{
public:
    explicit BuilderConnectionProvider(const CompilerFixture &fixture)
        : Core::ControllerConnectionProvider(
              Utils::Id("EtherCAT.ProjectCompiler.Test.Connection"),
              QStringLiteral("Test connection"))
    {
        const Data::RuntimePackageCompilerFreshTopologyEvidence &topology
            = fixture.request.topologyEvidence;
        m_snapshot.scope = topology.scope;
        m_snapshot.state = Data::ControllerConnectionState::Connected;
        m_snapshot.sessionGeneration = topology.sessionGeneration;
        m_snapshot.mock = false;
        m_snapshot.session = Data::ControllerSessionSummary{
            topology.sessionId, topology.captureBootId, 0, 30000, false};
        Data::ControllerCapabilitySummary capability;
        capability.descriptorSha256
            = fixture.request.targetProfile.capabilityDescriptorSha256.value();
        capability.topologyEvidence = true;
        m_snapshot.capability = capability;
        Data::ControllerTopologySnapshot observed;
        observed.firstStationAddress = topology.slaves.constFirst().stationAddress;
        observed.respondingCount = topology.slaves.size();
        observed.discoveredAt = QDateTime::fromMSecsSinceEpoch(topology.capturedAtNs / 1'000'000);
        observed.scope = topology.scope;
        observed.sessionGeneration = topology.sessionGeneration;
        observed.sessionId = topology.sessionId;
        observed.bootId = topology.captureBootId;
        observed.requestId = 41;
        observed.responseSequence = 42;
        observed.topologyCaptureSequence = quint32(topology.captureSequence);
        observed.topologyCompletedTimeNs = 43;
        observed.receivedAt = observed.discoveredAt;
        for (const Data::RuntimePackageCompilerTopologySlaveEvidence &slave : topology.slaves) {
            Data::ControllerTopologySlave wire;
            wire.position = quint32(slave.position);
            wire.stationAddress = slave.stationAddress;
            wire.vendorId = slave.identity.vendorId;
            wire.productCode = slave.identity.productCode;
            wire.revision = slave.identity.revisionNumber;
            wire.serial = slave.serialNumber;
            wire.alias = slave.alias;
            wire.aliasValidity = Data::ControllerTopologyEvidenceValidity::Valid;
            wire.aliasProvenance = Data::ControllerTopologyEvidenceProvenance::Observed;
            wire.aliasSource = Data::ControllerTopologyEvidenceSource::EscStationAlias;
            wire.moduleProvenance = Data::ControllerTopologyEvidenceProvenance::DeviceReported;
            if (slave.moduleAssignments.isEmpty()) {
                wire.moduleValidity = Data::ControllerTopologyEvidenceValidity::Unavailable;
                wire.moduleSource = Data::ControllerTopologyEvidenceSource::SiiMailbox;
            } else {
                wire.moduleValidity = Data::ControllerTopologyEvidenceValidity::Valid;
                wire.moduleSource = Data::ControllerTopologyEvidenceSource::CoeDetectedModules;
                for (const Data::DeviceModuleAssignment &module : slave.moduleAssignments) {
                    wire.modules.append(
                        {quint16(module.slot),
                         module.moduleIdent,
                         Data::ControllerTopologyEvidenceValidity::Valid,
                         Data::ControllerTopologyEvidenceProvenance::DeviceReported,
                         Data::ControllerTopologyEvidenceSource::CoeDetectedModules});
                }
            }
            observed.slaves.append(wire);
        }
        m_snapshot.topology = observed;
        setAvailable(true);
    }

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }
    Data::ControllerConnectionSnapshot connectionSnapshot() const final { return m_snapshot; }
    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &) final
    {
        return Utils::ResultError(QStringLiteral("Not supported."));
    }
    Utils::Result<> disconnectFromController() final
    {
        return Utils::ResultError(QStringLiteral("Not supported."));
    }
    Utils::Result<> refreshController() final
    {
        return Utils::ResultError(QStringLiteral("Not supported."));
    }

    Data::ControllerConnectionSnapshot &mutableSnapshot() { return m_snapshot; }

private:
    Data::ControllerConnectionSnapshot m_snapshot;
};

class ScopedProviderKindRegistration
{
public:
    ScopedProviderKindRegistration(Core::ProviderRegistry *registry, Core::Provider *provider)
        : m_registry(registry)
        , m_provider(provider)
    {
        m_displaced = registry->providers(provider->kind());
        for (Core::Provider *existing : std::as_const(m_displaced))
            ExtensionSystem::PluginManager::removeObject(existing);
        ExtensionSystem::PluginManager::addObject(provider);
    }
    ~ScopedProviderKindRegistration()
    {
        ExtensionSystem::PluginManager::removeObject(m_provider);
        for (Core::Provider *existing : std::as_const(m_displaced))
            ExtensionSystem::PluginManager::addObject(existing);
    }

private:
    Core::ProviderRegistry *m_registry = nullptr;
    Core::Provider *m_provider = nullptr;
    QList<Core::Provider *> m_displaced;
};

struct BuilderInputEnvironment
{
    QTemporaryDir temporary;
    QString profilePath;

    explicit BuilderInputEnvironment(const CompilerFixture &fixture)
    {
        const QString root = temporary.path();
        const auto writeArtifact = [&](const Data::RuntimePackageCompilerSourceArtifact &source) {
            return writeFile(temporary.filePath(source.relativePath), source.exactBytes);
        };
        const auto &sources = fixture.request.sourceArtifacts;
        writeArtifact(sources.targetProfile);
        writeArtifact(sources.targetProfileSignature);
        writeArtifact(sources.productionPublicKey);
        writeArtifact(sources.adapterBundle);
        writeArtifact(sources.policyTemplate);
        writeArtifact(sources.controllerFeatures);
        writeArtifact(sources.runtimeSource);
        const auto &deviceSource = fixture.request.deviceSourceEvidence.constFirst();
        writeArtifact(deviceSource.adapterSourceFile);

        const auto descriptor = [](const Data::RuntimePackageCompilerSourceArtifact &source) {
            return QJsonObject{{QStringLiteral("path"), source.relativePath},
                               {QStringLiteral("sha256"),
                                QString::fromLatin1(source.sha256.value().toHex())}};
        };
        const auto stringObject = [](const QMap<QString, QString> &values) {
            QJsonObject object;
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                object.insert(it.key(), it.value());
            return object;
        };
        const auto &projection = fixture.request.projectProjection.devices.constFirst();
        const QJsonObject manual{
            {QStringLiteral("enabled"), projection.manualEnvelope.enabled},
            {QStringLiteral("failure_action"), QStringLiteral("hold_safe")},
            {QStringLiteral("max_hold_cycles"), projection.manualEnvelope.maximumHoldCycles},
            {QStringLiteral("max_ttl_cycles"), projection.manualEnvelope.maximumTtlCycles},
            {QStringLiteral("refresh_cycles"), projection.manualEnvelope.refreshCycles},
            {QStringLiteral("release_action"), QStringLiteral("hold_safe")},
            {QStringLiteral("timeout_action"), QStringLiteral("hold_safe")},
        };
        QJsonArray expectedModules;
        for (const Data::DeviceModuleAssignment &module : projection.moduleAssignments) {
            expectedModules.append(
                QJsonObject{{QStringLiteral("module_ident"), double(module.moduleIdent)},
                            {QStringLiteral("slot"), module.slot}});
        }
        const QJsonObject device{
            {QStringLiteral("adapter_source"), descriptor(deviceSource.adapterSourceFile)},
            {QStringLiteral("component_binding_ids"),
             stringObject(projection.componentBindingIds)},
            {QStringLiteral("expected_alias"), projection.alias},
            {QStringLiteral("expected_modules"), expectedModules},
            {QStringLiteral("manual_envelope"), manual},
            {QStringLiteral("project_device_id"), projection.projectDeviceId},
            {QStringLiteral("project_slave_node_id"), fixture.slaveId.toString()},
            {QStringLiteral("semantic_action_binding_ids"),
             stringObject(projection.semanticActionBindingIds)},
            {QStringLiteral("semantic_binding_ids"),
             stringObject(projection.semanticBindingIds)},
            {QStringLiteral("slave_node_id"), projection.slaveNodeId},
            {QStringLiteral("symbol_mode"), QStringLiteral("report_only")},
            {QStringLiteral("symbols"), stringObject(projection.symbols)},
        };
        const QJsonObject profile{
            {QStringLiteral("adapter_bundle"), descriptor(sources.adapterBundle)},
            {QStringLiteral("contract_id"), fixture.request.contractIdentity.contractId},
            {QStringLiteral("contract_version"),
             int(fixture.request.contractIdentity.contractVersion)},
            {QStringLiteral("controller_features"), descriptor(sources.controllerFeatures)},
            {QStringLiteral("devices"), QJsonArray{device}},
            {QStringLiteral("format"),
             QStringLiteral("ethercat-ide-compiler-input-provisioning-v1")},
            {QStringLiteral("format_version"), 1},
            {QStringLiteral("master_node_id"), fixture.request.projectProjection.masterNodeId},
            {QStringLiteral("policy_template"), descriptor(sources.policyTemplate)},
            {QStringLiteral("production_public_key"), descriptor(sources.productionPublicKey)},
            {QStringLiteral("project_id"), fixture.request.projectProjection.projectId},
            {QStringLiteral("runtime_source"), descriptor(sources.runtimeSource)},
            {QStringLiteral("schema_bundle_sha256"),
             QString::fromLatin1(
                 fixture.request.contractIdentity.schemaBundleSha256.value().toHex())},
            {QStringLiteral("target_profile"), descriptor(sources.targetProfile)},
            {QStringLiteral("target_profile_signature"),
             descriptor(sources.targetProfileSignature)},
            {QStringLiteral("topology_ttl_ns"), 3'600'000'000'000.0},
            {QStringLiteral("ui_metadata"), QJsonObject{}},
        };
        profilePath = temporary.filePath(QStringLiteral("compile-inputs.json"));
        writeFile(profilePath, QJsonDocument(profile).toJson(QJsonDocument::Compact));
    }
};

bool replaceCompilerInputArtifact(
    BuilderInputEnvironment *environment,
    const Data::RuntimePackageCompilerSourceArtifact &artifact,
    const QByteArray &replacement)
{
    QJsonDocument document = QJsonDocument::fromJson(readFile(environment->profilePath));
    if (!document.isObject()
        || !writeFile(environment->temporary.filePath(artifact.relativePath), replacement)) {
        return false;
    }
    QJsonObject profile = document.object();
    bool found = false;
    for (auto it = profile.begin(); it != profile.end(); ++it) {
        if (!it->isObject())
            continue;
        QJsonObject descriptor = it->toObject();
        if (descriptor.value(QStringLiteral("path")).toString() != artifact.relativePath)
            continue;
        descriptor.insert(
            QStringLiteral("sha256"),
            QString::fromLatin1(sha256(replacement).value().toHex()));
        it.value() = descriptor;
        found = true;
        break;
    }
    return found
           && writeFile(
               environment->profilePath, QJsonDocument(profile).toJson(QJsonDocument::Compact));
}

Core::RuntimePackageCompilerPreparationStartRequest preparationRequest(
    const CompilerFixture &fixture, quint64 ordinal)
{
    Data::RuntimePackageCompilerCompileRequest compileRequest = fixture.request;
    if (ordinal != 1) {
        compileRequest.operationId = Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8000-%1").arg(ordinal, 12, 10, QLatin1Char('0'))};
        compileRequest.configurationId = 4200 + ordinal;
        compileRequest.intentId = QStringLiteral("embedlabs:compile-intent:test-%1").arg(ordinal);
    }
    return {
        compileRequest,
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8001-%1").arg(ordinal, 12, 10, QLatin1Char('0'))},
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/compiler-preparation-%1").arg(ordinal)},
        true,
    };
}

bool awaitPreparationPhase(
    Core::RuntimePackageCompilerPreparationCoordinator &coordinator,
    const Data::RuntimePackageCompilerOperationId &operationId,
    Core::RuntimePackageCompilerPreparationPhase phase,
    int timeoutMs = 15000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeoutMs) {
        const auto current = coordinator.record(operationId);
        if (current && *current && (*current)->phase == phase)
            return true;
        QTest::qWait(10);
    }
    return false;
}

class ScopedCompilerProviderRegistration
{
public:
    ScopedCompilerProviderRegistration(
        Core::ProviderRegistry *registry, Core::RuntimePackageCompilerProvider *provider)
        : m_registry(registry)
        , m_provider(provider)
    {
        if (!m_registry || !m_provider)
            return;
        m_displaced = m_registry->providers(Core::ProviderKind::RuntimePackageCompiler);
        for (Core::Provider *candidate : std::as_const(m_displaced))
            ExtensionSystem::PluginManager::removeObject(candidate);
        ExtensionSystem::PluginManager::addObject(m_provider);
        m_registered = m_registry->provider(m_provider->id()) == m_provider;
    }

    ~ScopedCompilerProviderRegistration()
    {
        if (m_registered)
            ExtensionSystem::PluginManager::removeObject(m_provider);
        for (Core::Provider *candidate : std::as_const(m_displaced))
            ExtensionSystem::PluginManager::addObject(candidate);
    }

    bool isValid() const
    {
        return m_registered
               && m_registry->providers(Core::ProviderKind::RuntimePackageCompiler)
                      == QList<Core::Provider *>({m_provider});
    }

    void unregisterProvider()
    {
        if (!m_registered)
            return;
        ExtensionSystem::PluginManager::removeObject(m_provider);
        m_registered = false;
    }

private:
    Core::ProviderRegistry *m_registry = nullptr;
    Core::RuntimePackageCompilerProvider *m_provider = nullptr;
    QList<Core::Provider *> m_displaced;
    bool m_registered = false;
};

RuntimePackageCompilerPreparationJournalEntry journalEntry(
    const Core::RuntimePackageCompilerPreparationStartRequest &request,
    Core::RuntimePackageCompilerPreparationPhase phase,
    quint64 revision,
    QString detail = {})
{
    const Utils::Result<Data::RuntimePackageCompilerSha256> fingerprint
        = Core::runtimePackageCompilerPreparationStartRequestFingerprint(request);
    if (!fingerprint)
        return {};
    RuntimePackageCompilerPreparationJournalEntry entry;
    entry.compileOperationId = request.compileRequest.operationId;
    entry.startRequestFingerprint = *fingerprint;
    entry.verifyOperationId = request.verifyOperationId;
    entry.activationOperationId = request.activationOperationId;
    entry.compilerProviderId = QString::fromUtf8(Constants::PROJECT_COMPILER_PROVIDER_ID.name());
    entry.contractIdentity = request.compileRequest.contractIdentity;
    entry.configurationId = request.compileRequest.configurationId;
    entry.buildTimestampNs = request.compileRequest.buildTimestampNs;
    entry.revision = revision;
    entry.phase = phase;
    entry.detail = std::move(detail);
    entry.rollbackOnActivationFailure = request.rollbackOnActivationFailure;
    return entry;
}

const RuntimePackageCompilerPreparationJournalEntry *journalEntryFor(
    const RuntimePackageCompilerPreparationJournalState &state,
    const Data::RuntimePackageCompilerOperationId &operationId)
{
    const auto found
        = std::find_if(state.entries.cbegin(), state.entries.cend(), [&](const auto &entry) {
              return entry.compileOperationId == operationId;
          });
    return found == state.entries.cend() ? nullptr : &*found;
}

Utils::Result<RuntimePackageCompilerPreparationJournalState> reservePreparationForRestart(
    RuntimePackageCompilerPreparationJournal &journal,
    const RuntimePackageCompilerPreparationJournalState &state,
    const Core::RuntimePackageCompilerPreparationStartRequest &request,
    QByteArrayView serializedProject)
{
    const RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
        request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    if (!journal.supportsDurableRecoveryStorage())
        return journal.commit(reserved, state.sequence, std::nullopt);

    const Utils::Result<QByteArray> recovery = encodeRuntimePackageCompilerCompileRecovery(
        {request.compileRequest, QByteArray(serializedProject)});
    if (!recovery)
        return Utils::ResultError(recovery.error());
    return journal.reserveWithRecovery(reserved, state.sequence, *recovery);
}

class ImmediateRuntimePackageCompilerJob final : public Core::RuntimePackageCompilerJob
{
public:
    ImmediateRuntimePackageCompilerJob(
        Data::RuntimePackageCompilerJobResult result, QObject *parent)
        : Core::RuntimePackageCompilerJob(result.command(), parent)
    {
        finish(result);
    }

private:
    void requestCancellation() final {}
};

class MalformedUnknownCompilerProvider final : public Core::RuntimePackageCompilerProvider
{
public:
    MalformedUnknownCompilerProvider()
        : Core::RuntimePackageCompilerProvider(
              Constants::PROJECT_COMPILER_PROVIDER_ID,
              QStringLiteral("Malformed unknown compiler"))
    {
        setAvailable(true);
    }

    Utils::Result<Core::RuntimePackageCompilerJob *> compile(
        const Data::RuntimePackageCompilerCompileRequest &) final
    {
        ++compileCallCount;
        return Utils::ResultError(QStringLiteral("Compile must not be scheduled."));
    }

    Utils::Result<Core::RuntimePackageCompilerJob *> finalize(
        const Data::RuntimePackageCompilerFinalizeRequest &) final
    {
        return Utils::ResultError(QStringLiteral("Finalize is unavailable."));
    }

    Utils::Result<Core::RuntimePackageCompilerJob *> query(
        const Data::RuntimePackageCompilerQueryRequest &request) final
    {
        ++queryCallCount;
        const QByteArray failure = QByteArrayLiteral(
            "{\"diagnostics\":[{\"code\":\"ECOMP-OPERATION-UNKNOWN\","
            "\"format\":\"ethercat-ide-compiler-diagnostic-v1\","
            "\"message\":\"Operation state is retryable\",\"path\":\"$.wrong\","
            "\"retryable\":true,\"severity\":\"error\",\"stage\":\"internal\"}],"
            "\"status\":\"fail\"}\n");
        const Utils::Result<Data::RuntimePackageCompilerQueryResult> decoded
            = Core::decodeRuntimePackageCompilerQueryResult(
                request, {true, 1, {}, failure});
        if (!decoded)
            return Utils::ResultError(decoded.error());
        return new ImmediateRuntimePackageCompilerJob(
            Data::RuntimePackageCompilerJobResult{*decoded}, this);
    }

    Utils::Result<Core::RuntimePackageCompilerJob *> verify(
        const Data::RuntimePackageCompilerVerifyRequest &) final
    {
        return Utils::ResultError(QStringLiteral("Verify is unavailable."));
    }

    int queryCallCount = 0;
    int compileCallCount = 0;
};

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
    QCOMPARE(
        provider.unavailableReason(),
        QStringLiteral("No trusted compiler provisioning profile is installed."));

    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    ProvisionedRuntimePackageCompilerProjectRequestBuilder builder(
        registry,
        Utils::FilePath::fromString(temporary.filePath(QStringLiteral("missing-inputs.json"))));
    QVERIFY(!builder.isAvailable());
    QCOMPARE(
        builder.unavailableReason(),
        QStringLiteral("No trusted compiler input profile is installed."));
}

void EtherCATProjectCompilerTests::testPythonRuntimeProfileVerifiesSignedInstalledTree()
{
#if !defined(Q_OS_DARWIN) || !defined(Q_PROCESSOR_ARM_64)
    QSKIP("The relocatable Python runtime contract requires native macOS arm64.");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString base = QFileInfo(temporary.path()).canonicalFilePath();
    const QString container = QDir(base).filePath(QStringLiteral("python-profile-container"));
    const QString companionRoot = QDir(container).filePath(QStringLiteral("companion"));
    const QString runtimeRoot = QDir(container).filePath(QStringLiteral("runtime-测试"));
    QVERIFY(QDir().mkpath(companionRoot));
    QVERIFY(QDir().mkpath(runtimeRoot));
    QVERIFY(::chmod(QFile::encodeName(container).constData(), 0700) == 0);
    QVERIFY(::chmod(QFile::encodeName(companionRoot).constData(), 0700) == 0);
    QVERIFY(::chmod(QFile::encodeName(runtimeRoot).constData(), 0700) == 0);

    QByteArray seed(32, '\x70');
    QByteArray secretKey(64, '\0');
    QByteArray publicKey(32, '\0');
    crypto_ed25519_key_pair(
        reinterpret_cast<uint8_t *>(secretKey.data()),
        reinterpret_cast<uint8_t *>(publicKey.data()),
        reinterpret_cast<uint8_t *>(seed.data()));
    const QByteArray keyId = sha256(publicKey).value().toHex();

    QByteArray pythonBytes = QByteArray::fromHex("cffaedfe0c0000010000000002000000");
    pythonBytes.append(QByteArray(240, '\x5a'));
    const QMap<QString, QPair<QByteArray, int>> runtimeFiles{
        {QStringLiteral("python/bin/python3.11"), {pythonBytes, 0755}},
        {QStringLiteral("python/lib/python3.11/LICENSE.txt"),
         {QByteArray("CPython fixture license\n"), 0644}},
        {QStringLiteral("python/lib/python3.11/lib-dynload/_fixture.so"),
         {QByteArray("Mach-O extension fixture\n"), 0755}},
        {QStringLiteral("python/lib/python3.11/site-packages/embedlabs_api070/fixture.py"),
         {QByteArray("VALUE = 70\n"), 0644}},
    };
    for (auto it = runtimeFiles.cbegin(); it != runtimeFiles.cend(); ++it) {
        const QString path = QDir(runtimeRoot).filePath(it.key());
        QVERIFY(writeFile(path, it->first));
        QVERIFY(::chmod(QFile::encodeName(path).constData(), it->second) == 0);
    }
    const QString pythonSymlink = QDir(runtimeRoot).filePath(QStringLiteral("python/bin/python3"));
    QVERIFY(::symlink("python3.11", QFile::encodeName(pythonSymlink).constData()) == 0);

    QMap<QString, QJsonObject> baseRuntimeMembers;
    for (auto it = runtimeFiles.cbegin(); it != runtimeFiles.cend(); ++it) {
        const QString path = it.key().sliced(QStringLiteral("python/").size());
        baseRuntimeMembers.insert(
            path,
            QJsonObject{
                {QStringLiteral("archive_mode"), it->second},
                {QStringLiteral("bytes"), it->first.size()},
                {QStringLiteral("installed_mode"), it->second},
                {QStringLiteral("mtime"), 1704067200},
                {QStringLiteral("path"), path},
                {QStringLiteral("sha256"), QString::fromLatin1(sha256(it->first).value().toHex())},
                {QStringLiteral("type"), QStringLiteral("file")}});
    }
    baseRuntimeMembers.insert(
        QStringLiteral("bin/python3"),
        QJsonObject{
            {QStringLiteral("mtime"), 1704067200},
            {QStringLiteral("path"), QStringLiteral("bin/python3")},
            {QStringLiteral("target"), QStringLiteral("python3.11")},
            {QStringLiteral("type"), QStringLiteral("symlink")}});
    QJsonArray runtimeMemberArray;
    quint64 baseRuntimeBytes = 0;
    for (auto it = baseRuntimeMembers.cbegin(); it != baseRuntimeMembers.cend(); ++it) {
        runtimeMemberArray.append(it.value());
        baseRuntimeBytes += quint64(it.value().value(QStringLiteral("bytes")).toInt());
    }
    const QJsonObject runtimeTreeProjection{
        {QStringLiteral("format"),
         QStringLiteral("embedlabs-relocatable-python-tree-projection-v1")},
        {QStringLiteral("members"), runtimeMemberArray}};
    const auto runtimeTreeSha = sha256(pythonCanonicalJson(runtimeTreeProjection));
    const QByteArray archiveBytes("fixture-python-runtime-archive");
    const QString archivePath = QStringLiteral("runtime/python-runtime-fixture.tar.gz");
    const QJsonObject runtimeTree{
        {QStringLiteral("archive"),
         QJsonObject{
             {QStringLiteral("bytes"), archiveBytes.size()},
             {QStringLiteral("compression"), QStringLiteral("tar+gzip")},
             {QStringLiteral("path"), archivePath},
             {QStringLiteral("sha256"), QString::fromLatin1(sha256(archiveBytes).value().toHex())}}},
        {QStringLiteral("counts"),
         QJsonObject{
             {QStringLiteral("file_bytes"), double(baseRuntimeBytes)},
             {QStringLiteral("files"), runtimeFiles.size()},
             {QStringLiteral("macho_files"), 2},
             {QStringLiteral("members"), baseRuntimeMembers.size()},
             {QStringLiteral("symlinks"), 1}}},
        {QStringLiteral("format"), QStringLiteral("embedlabs-relocatable-python-tree-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("members"), runtimeMemberArray},
        {QStringLiteral("root_prefix"), QStringLiteral("python")},
        {QStringLiteral("target"),
         QJsonObject{
             {QStringLiteral("cache_tag"), QStringLiteral("cpython-311")},
             {QStringLiteral("implementation"), QStringLiteral("CPython")},
             {QStringLiteral("machine"), QStringLiteral("arm64")},
             {QStringLiteral("minimum_macos_version"), QStringLiteral("11.0")},
             {QStringLiteral("operating_system"), QStringLiteral("macos")},
             {QStringLiteral("python_version"), QStringLiteral("3.11.15")},
             {QStringLiteral("soabi"), QStringLiteral("cpython-311-darwin")}}},
        {QStringLiteral("tree_sha256"), QString::fromLatin1(runtimeTreeSha.value().toHex())},
    };
    const QByteArray runtimeTreeBytes = pythonCanonicalJson(runtimeTree);

    const QByteArray contractBytes("{\"format\":\"fixture-contract\"}\n");
    const QByteArray licenseBytes("Runtime fixture license\n");
    const QByteArray lockBytes("fixture==1.0 --hash=sha256:00\n");
    const QByteArray wheelManifestBytes("{\"format\":\"fixture-wheelhouse\"}\n");
    const QByteArray binaryEntrypoint = QByteArray::fromHex(
        "cffaedfe0c000001000000000200000000000000");
    const QString treePath = QStringLiteral("runtime/runtime-tree-manifest-v1.json");
    const QString contractPath = QStringLiteral(
        "contracts/compiler-runtime-relocatable-macos-arm64-cp311-v1.json");
    const QString licensePath = QStringLiteral("licenses/runtime-license.txt");
    const QString lockPath = QStringLiteral(
        "locks/compiler-runtime-relocatable-macos-arm64-cp311.lock");
    const QString wheelManifestPath = QStringLiteral("wheelhouse/wheelhouse-manifest-v2.json");
    const QString trustPath = QStringLiteral("trust/%1.pub").arg(QString::fromLatin1(keyId));
    const QMap<QString, QPair<QByteArray, int>> companionFiles{
        {QStringLiteral("bin/embedlabs-python-relocatable-runtime-install"),
         {binaryEntrypoint, 0755}},
        {QStringLiteral("bin/embedlabs-python-relocatable-runtime-self-test"),
         {binaryEntrypoint, 0755}},
        {QStringLiteral("bin/embedlabs-python-relocatable-runtime-verify"),
         {binaryEntrypoint, 0755}},
        {contractPath, {contractBytes, 0644}},
        {licensePath, {licenseBytes, 0644}},
        {lockPath, {lockBytes, 0644}},
        {archivePath, {archiveBytes, 0644}},
        {treePath, {runtimeTreeBytes, 0644}},
        {trustPath, {publicKey, 0644}},
        {wheelManifestPath, {wheelManifestBytes, 0644}},
    };
    QJsonArray companionFileRecords;
    for (auto it = companionFiles.cbegin(); it != companionFiles.cend(); ++it) {
        const QString path = QDir(companionRoot).filePath(it.key());
        QVERIFY(writeFile(path, it->first));
        QVERIFY(::chmod(QFile::encodeName(path).constData(), it->second) == 0);
        companionFileRecords.append(
            QJsonObject{
                {QStringLiteral("bytes"), it->first.size()},
                {QStringLiteral("mode"), it->second},
                {QStringLiteral("path"), it.key()},
                {QStringLiteral("sha256"), QString::fromLatin1(sha256(it->first).value().toHex())}});
    }
    const QStringList identityFieldNames{
        QStringLiteral("base_runtime_archive_sha256"),
        QStringLiteral("base_runtime_tree_sha256"),
        QStringLiteral("companion_bundle_sha256"),
        QStringLiteral("companion_id"),
        QStringLiteral("companion_key_id"),
        QStringLiteral("companion_manifest_format"),
        QStringLiteral("companion_manifest_format_version"),
        QStringLiteral("companion_manifest_sha256"),
        QStringLiteral("companion_version"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("identity_sha256"),
        QStringLiteral("installed_file_bytes"),
        QStringLiteral("installed_tree_entries"),
        QStringLiteral("installed_tree_sha256"),
        QStringLiteral("machine"),
        QStringLiteral("package_set_sha256"),
        QStringLiteral("platform"),
        QStringLiteral("python_cache_tag"),
        QStringLiteral("python_executable"),
        QStringLiteral("python_executable_sha256"),
        QStringLiteral("python_implementation"),
        QStringLiteral("python_soabi"),
        QStringLiteral("python_version"),
        QStringLiteral("runtime_release_tag"),
        QStringLiteral("runtime_root"),
        QStringLiteral("safe_path_policy"),
        QStringLiteral("signature_algorithm"),
        QStringLiteral("signature_domain"),
        QStringLiteral("sys_base_prefix"),
        QStringLiteral("sys_path_relative"),
        QStringLiteral("sys_path_sha256"),
        QStringLiteral("sys_prefix"),
        QStringLiteral("wheel_lock_sha256"),
        QStringLiteral("wheel_overlay_tree_sha256"),
        QStringLiteral("wheelhouse_content_sha256")};
    QJsonArray identityFields;
    for (const QString &name : identityFieldNames)
        identityFields.append(name);
    const QByteArray wheelContent("fixture-wheel-content");
    const QJsonObject companionManifest{
        {QStringLiteral("companion_id"),
         QStringLiteral("org.embedlabs.ethercat.project-compiler.python-runtime.relocatable")},
        {QStringLiteral("companion_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("contract"),
         QJsonObject{
             {QStringLiteral("bytes"), contractBytes.size()},
             {QStringLiteral("path"), contractPath},
             {QStringLiteral("sha256"),
              QString::fromLatin1(sha256(contractBytes).value().toHex())}}},
        {QStringLiteral("files"), companionFileRecords},
        {QStringLiteral("format"),
         QStringLiteral("embedlabs-relocatable-macos-python-runtime-companion-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("host_runtime"),
         QJsonObject{
             {QStringLiteral("archive"),
              QJsonObject{
                  {QStringLiteral("bytes"), archiveBytes.size()},
                  {QStringLiteral("path"), archivePath},
                  {QStringLiteral("sha256"),
                   QString::fromLatin1(sha256(archiveBytes).value().toHex())}}},
             {QStringLiteral("binary_architectures"), QJsonArray{QStringLiteral("arm64")}},
             {QStringLiteral("cpython_license_sha256"),
              QString::fromLatin1(sha256("cpython-license").value().toHex())},
             {QStringLiteral("distribution"),
              QStringLiteral("astral-sh/python-build-standalone install_only")},
             {QStringLiteral("python_executable"), QStringLiteral("python/bin/python3.11")},
             {QStringLiteral("release_commit"), QString(40, QLatin1Char('7'))},
             {QStringLiteral("release_tag"), QStringLiteral("20260718")},
             {QStringLiteral("repository"),
              QStringLiteral("https://github.com/astral-sh/python-build-standalone")},
             {QStringLiteral("runtime_members"), baseRuntimeMembers.size()},
             {QStringLiteral("tree_manifest"),
              QJsonObject{
                  {QStringLiteral("bytes"), runtimeTreeBytes.size()},
                  {QStringLiteral("path"), treePath},
                  {QStringLiteral("sha256"),
                   QString::fromLatin1(sha256(runtimeTreeBytes).value().toHex())}}},
             {QStringLiteral("tree_sha256"), QString::fromLatin1(runtimeTreeSha.value().toHex())},
             {QStringLiteral("upstream_license"),
              QJsonObject{
                  {QStringLiteral("bytes"), licenseBytes.size()},
                  {QStringLiteral("path"), licensePath},
                  {QStringLiteral("sha256"),
                   QString::fromLatin1(sha256(licenseBytes).value().toHex())}}}}},
        {QStringLiteral("identity_fields"), identityFields},
        {QStringLiteral("installation"),
         QJsonObject{
             {QStringLiteral("atomic_sibling_publish"), true},
             {QStringLiteral("compiler_import_root"),
              QStringLiteral("externally-verified-api068-runtime-root/runtime/igh_osless/tools")},
             {QStringLiteral("creates_system_links"), false},
             {QStringLiteral("destination_absolute_and_empty"), true},
             {QStringLiteral("entrypoint"),
              QStringLiteral("bin/embedlabs-python-relocatable-runtime-install")},
             {QStringLiteral("forbidden_prefixes"),
              QJsonArray{
                  QStringLiteral("/Library"),
                  QStringLiteral("/System"),
                  QStringLiteral("/usr"),
                  QStringLiteral("/bin"),
                  QStringLiteral("/sbin")}},
             {QStringLiteral("implicit_script_directory_allowed"), false},
             {QStringLiteral("modifies_shell_profile"), false},
             {QStringLiteral("network_allowed"), false},
             {QStringLiteral("pip_configuration_allowed"), false},
             {QStringLiteral("requires_root"), false},
             {QStringLiteral("safe_path_required"), true},
             {QStringLiteral("self_test"),
              QStringLiteral("bin/embedlabs-python-relocatable-runtime-self-test")},
             {QStringLiteral("user_site_allowed"), false},
             {QStringLiteral("verifier"),
              QStringLiteral("bin/embedlabs-python-relocatable-runtime-verify")},
             {QStringLiteral("writes_outside_destination_parent"), false}}},
        {QStringLiteral("limits"),
         QJsonObject{
             {QStringLiteral("max_bundle_file_bytes"), 33554432},
             {QStringLiteral("max_bundle_files"), 64},
             {QStringLiteral("max_bundle_uncompressed_bytes"), 50331648},
             {QStringLiteral("max_path_bytes"), 512},
             {QStringLiteral("max_runtime_file_bytes"), 33554432},
             {QStringLiteral("max_runtime_file_total_bytes"), 134217728},
             {QStringLiteral("max_runtime_members"), 4096}}},
        {QStringLiteral("signature"),
         QJsonObject{
             {QStringLiteral("algorithm"), QStringLiteral("ed25519")},
             {QStringLiteral("domain"),
              QStringLiteral("embedlabs-ethercat-macos-python-relocatable-runtime-companion-v1")},
             {QStringLiteral("key_id"), QString::fromLatin1(keyId)},
             {QStringLiteral("signature_file"), QStringLiteral("manifest.sig")}}},
        {QStringLiteral("target"),
         QJsonObject{
             {QStringLiteral("machine"), QStringLiteral("arm64")},
             {QStringLiteral("minimum_macos_version"), QStringLiteral("11.0")},
             {QStringLiteral("operating_system"), QStringLiteral("macos")},
             {QStringLiteral("python_implementation"), QStringLiteral("CPython")},
             {QStringLiteral("python_version"), QStringLiteral("3.11.15")}}},
        {QStringLiteral("wheelhouse"),
         QJsonObject{
             {QStringLiteral("content_sha256"),
              QString::fromLatin1(sha256(wheelContent).value().toHex())},
             {QStringLiteral("directory"), QStringLiteral("wheelhouse")},
             {QStringLiteral("distribution_count"), 7},
             {QStringLiteral("installation_mode"), QStringLiteral("verified-relative-site-overlay")},
             {QStringLiteral("lock"),
              QJsonObject{
                  {QStringLiteral("bytes"), lockBytes.size()},
                  {QStringLiteral("path"), lockPath},
                  {QStringLiteral("sha256"),
                   QString::fromLatin1(sha256(lockBytes).value().toHex())}}},
             {QStringLiteral("manifest"),
              QJsonObject{
                  {QStringLiteral("bytes"), wheelManifestBytes.size()},
                  {QStringLiteral("path"), wheelManifestPath},
                  {QStringLiteral("sha256"),
                   QString::fromLatin1(sha256(wheelManifestBytes).value().toHex())}}}}},
    };
    const QByteArray companionManifestBytes = pythonCanonicalJson(companionManifest);
    QByteArray signedBytes("embedlabs-ethercat-macos-python-relocatable-runtime-companion-v1");
    signedBytes.append('\0');
    signedBytes.append(companionManifestBytes);
    QByteArray signature(64, '\0');
    crypto_ed25519_sign(
        reinterpret_cast<uint8_t *>(signature.data()),
        reinterpret_cast<const uint8_t *>(secretKey.constData()),
        reinterpret_cast<const uint8_t *>(signedBytes.constData()),
        size_t(signedBytes.size()));
    const QString manifestPath = QDir(companionRoot).filePath(QStringLiteral("manifest.json"));
    const QString signaturePath = QDir(companionRoot).filePath(QStringLiteral("manifest.sig"));
    QVERIFY(writeFile(manifestPath, companionManifestBytes));
    QVERIFY(writeFile(signaturePath, signature));
    QVERIFY(::chmod(QFile::encodeName(manifestPath).constData(), 0644) == 0);
    QVERIFY(::chmod(QFile::encodeName(signaturePath).constData(), 0644) == 0);

    QMap<QString, QJsonObject> installedRecords;
    quint64 installedBytes = 0;
    for (auto it = runtimeFiles.cbegin(); it != runtimeFiles.cend(); ++it) {
        installedBytes += quint64(it->first.size());
        installedRecords.insert(
            it.key(),
            QJsonObject{
                {QStringLiteral("bytes"), it->first.size()},
                {QStringLiteral("mode"), it->second},
                {QStringLiteral("path"), it.key()},
                {QStringLiteral("sha256"), QString::fromLatin1(sha256(it->first).value().toHex())},
                {QStringLiteral("type"), QStringLiteral("file")}});
    }
    installedRecords.insert(
        QStringLiteral("python/bin/python3"),
        QJsonObject{
            {QStringLiteral("path"), QStringLiteral("python/bin/python3")},
            {QStringLiteral("target"), QStringLiteral("python3.11")},
            {QStringLiteral("type"), QStringLiteral("symlink")}});
    QJsonArray installedRecordArray;
    for (auto it = installedRecords.cbegin(); it != installedRecords.cend(); ++it)
        installedRecordArray.append(it.value());
    const QJsonObject installedTreeProjection{
        {QStringLiteral("entries"), installedRecordArray},
        {QStringLiteral("format"),
         QStringLiteral("embedlabs-installed-relocatable-runtime-tree-v1")}};
    const auto installedTreeSha = sha256(pythonCanonicalJson(installedTreeProjection));
    const QJsonArray sysPath{
        QStringLiteral("python/lib/python311.zip"),
        QStringLiteral("python/lib/python3.11"),
        QStringLiteral("python/lib/python3.11/lib-dynload"),
        QStringLiteral("python/lib/python3.11/site-packages"),
        QStringLiteral("python/lib/python3.11/site-packages/embedlabs_api070")};
    const auto companionBundleSha = sha256("signed-companion-archive-fixture");
    QJsonObject identity{
        {QStringLiteral("base_runtime_archive_sha256"),
         QString::fromLatin1(sha256(archiveBytes).value().toHex())},
        {QStringLiteral("base_runtime_tree_sha256"),
         QString::fromLatin1(runtimeTreeSha.value().toHex())},
        {QStringLiteral("companion_bundle_sha256"),
         QString::fromLatin1(companionBundleSha.value().toHex())},
        {QStringLiteral("companion_id"),
         QStringLiteral("org.embedlabs.ethercat.project-compiler.python-runtime.relocatable")},
        {QStringLiteral("companion_key_id"), QString::fromLatin1(keyId)},
        {QStringLiteral("companion_manifest_format"),
         QStringLiteral("embedlabs-relocatable-macos-python-runtime-companion-v1")},
        {QStringLiteral("companion_manifest_format_version"), 1},
        {QStringLiteral("companion_manifest_sha256"),
         QString::fromLatin1(sha256(companionManifestBytes).value().toHex())},
        {QStringLiteral("companion_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("format"),
         QStringLiteral("embedlabs-installed-relocatable-python-runtime-identity-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("installed_file_bytes"), double(installedBytes)},
        {QStringLiteral("installed_tree_entries"), installedRecords.size()},
        {QStringLiteral("installed_tree_sha256"),
         QString::fromLatin1(installedTreeSha.value().toHex())},
        {QStringLiteral("machine"), QStringLiteral("arm64")},
        {QStringLiteral("package_set_sha256"),
         QString::fromLatin1(sha256("package-set").value().toHex())},
        {QStringLiteral("platform"), QStringLiteral("macos")},
        {QStringLiteral("python_cache_tag"), QStringLiteral("cpython-311")},
        {QStringLiteral("python_executable"),
         QDir(runtimeRoot).filePath(QStringLiteral("python/bin/python3.11"))},
        {QStringLiteral("python_executable_sha256"),
         QString::fromLatin1(sha256(pythonBytes).value().toHex())},
        {QStringLiteral("python_implementation"), QStringLiteral("CPython")},
        {QStringLiteral("python_soabi"), QStringLiteral("cpython-311-darwin")},
        {QStringLiteral("python_version"), QStringLiteral("3.11.15")},
        {QStringLiteral("runtime_release_tag"), QStringLiteral("20260718")},
        {QStringLiteral("runtime_root"), runtimeRoot},
        {QStringLiteral("safe_path_policy"), QStringLiteral("required-explicit-signed-import-root")},
        {QStringLiteral("signature_algorithm"), QStringLiteral("ed25519")},
        {QStringLiteral("signature_domain"),
         QStringLiteral("embedlabs-ethercat-macos-python-relocatable-runtime-companion-v1")},
        {QStringLiteral("sys_base_prefix"), QDir(runtimeRoot).filePath(QStringLiteral("python"))},
        {QStringLiteral("sys_path_relative"), sysPath},
        {QStringLiteral("sys_path_sha256"),
         QString::fromLatin1(sha256(pythonCanonicalJson(sysPath)).value().toHex())},
        {QStringLiteral("sys_prefix"), QDir(runtimeRoot).filePath(QStringLiteral("python"))},
        {QStringLiteral("wheel_lock_sha256"),
         QString::fromLatin1(sha256(lockBytes).value().toHex())},
        {QStringLiteral("wheel_overlay_tree_sha256"),
         QString::fromLatin1(sha256("wheel-overlay").value().toHex())},
        {QStringLiteral("wheelhouse_content_sha256"),
         QString::fromLatin1(sha256(wheelContent).value().toHex())},
    };
    const auto writeIdentity = [&] {
        QJsonObject projection = identity;
        projection.remove(QStringLiteral("identity_sha256"));
        identity.insert(
            QStringLiteral("identity_sha256"),
            QString::fromLatin1(sha256(pythonCanonicalJson(projection)).value().toHex()));
        const QString path = QDir(runtimeRoot).filePath(QStringLiteral("runtime-identity-v1.json"));
        return writeFile(path, pythonCanonicalJson(identity))
               && ::chmod(QFile::encodeName(path).constData(), 0600) == 0;
    };
    QVERIFY(writeIdentity());
    QJsonObject portableIdentity = identity;
    portableIdentity.remove(QStringLiteral("runtime_root"));
    portableIdentity.remove(QStringLiteral("python_executable"));
    portableIdentity.remove(QStringLiteral("sys_prefix"));
    portableIdentity.remove(QStringLiteral("sys_base_prefix"));
    portableIdentity.remove(QStringLiteral("identity_sha256"));
    const CompilerPythonRuntimeExpectation expectation{
        QStringLiteral("1.0.0"),
        publicKey,
        companionBundleSha,
        sha256(companionManifestBytes),
        sha256(pythonBytes),
        installedTreeSha,
        sha256(pythonCanonicalJson(portableIdentity)),
    };

    const auto loaded = CompilerPythonRuntimeProfile::load(
        Utils::FilePath::fromString(companionRoot),
        Utils::FilePath::fromString(runtimeRoot),
        expectation);
    QVERIFY_RESULT(loaded);
    QVERIFY(loaded->identity().isValid());
    QCOMPARE(
        loaded->pythonExecutable().path(),
        QDir(runtimeRoot).filePath(QStringLiteral("python/bin/python3.11")));
    QVERIFY_RESULT(loaded->validateCurrent());

    const QString runtimeRootLink = QDir(container).filePath(QStringLiteral("runtime-link"));
    QVERIFY(::symlink(
                QFile::encodeName(runtimeRoot).constData(),
                QFile::encodeName(runtimeRootLink).constData())
            == 0);
    QVERIFY(!CompilerPythonRuntimeProfile::load(
        Utils::FilePath::fromString(companionRoot),
        Utils::FilePath::fromString(runtimeRootLink),
        expectation));
    QVERIFY(::unlink(QFile::encodeName(runtimeRootLink).constData()) == 0);

    const QStringList historicalIdentityFieldNames{
        QStringLiteral("base_runtime_archive_sha256"),
        QStringLiteral("base_runtime_tree_sha256"),
        QStringLiteral("companion_bundle_sha256"),
        QStringLiteral("companion_key_id"),
        QStringLiteral("companion_id"),
        QStringLiteral("companion_manifest_format"),
        QStringLiteral("companion_manifest_format_version"),
        QStringLiteral("companion_manifest_sha256"),
        QStringLiteral("companion_version"),
        QStringLiteral("identity_sha256"),
        QStringLiteral("installed_file_bytes"),
        QStringLiteral("installed_tree_entries"),
        QStringLiteral("installed_tree_sha256"),
        QStringLiteral("machine"),
        QStringLiteral("package_set_sha256"),
        QStringLiteral("platform"),
        QStringLiteral("python_cache_tag"),
        QStringLiteral("python_executable"),
        QStringLiteral("python_executable_sha256"),
        QStringLiteral("python_implementation"),
        QStringLiteral("python_soabi"),
        QStringLiteral("python_version"),
        QStringLiteral("runtime_release_tag"),
        QStringLiteral("runtime_root"),
        QStringLiteral("safe_path_policy"),
        QStringLiteral("signature_algorithm"),
        QStringLiteral("signature_domain"),
        QStringLiteral("sys_path_sha256"),
        QStringLiteral("wheel_lock_sha256"),
        QStringLiteral("wheelhouse_content_sha256"),
        QStringLiteral("wheel_overlay_tree_sha256")};
    QJsonArray historicalIdentityFields;
    for (const QString &name : historicalIdentityFieldNames)
        historicalIdentityFields.append(name);
    QCOMPARE(historicalIdentityFields.size(), 31);
    QJsonObject historicalManifest = companionManifest;
    historicalManifest.insert(QStringLiteral("identity_fields"), historicalIdentityFields);
    const QByteArray historicalManifestBytes = pythonCanonicalJson(historicalManifest);
    QByteArray historicalSignedBytes(
        "embedlabs-ethercat-macos-python-relocatable-runtime-companion-v1");
    historicalSignedBytes.append('\0');
    historicalSignedBytes.append(historicalManifestBytes);
    QByteArray historicalSignature(64, '\0');
    crypto_ed25519_sign(
        reinterpret_cast<uint8_t *>(historicalSignature.data()),
        reinterpret_cast<const uint8_t *>(secretKey.constData()),
        reinterpret_cast<const uint8_t *>(historicalSignedBytes.constData()),
        size_t(historicalSignedBytes.size()));
    QVERIFY(writeFile(manifestPath, historicalManifestBytes));
    QVERIFY(writeFile(signaturePath, historicalSignature));
    QVERIFY(::chmod(QFile::encodeName(manifestPath).constData(), 0644) == 0);
    QVERIFY(::chmod(QFile::encodeName(signaturePath).constData(), 0644) == 0);
    CompilerPythonRuntimeExpectation historicalExpectation = expectation;
    historicalExpectation.companionManifestSha256 = sha256(historicalManifestBytes);
    QVERIFY(!CompilerPythonRuntimeProfile::load(
        Utils::FilePath::fromString(companionRoot),
        Utils::FilePath::fromString(runtimeRoot),
        historicalExpectation));
    QVERIFY(writeFile(manifestPath, companionManifestBytes));
    QVERIFY(writeFile(signaturePath, signature));
    QVERIFY(::chmod(QFile::encodeName(manifestPath).constData(), 0644) == 0);
    QVERIFY(::chmod(QFile::encodeName(signaturePath).constData(), 0644) == 0);
    secretKey.fill('\0');
    seed.fill('\0');
    QVERIFY_RESULT(loaded->validateCurrent());

    CompilerPythonRuntimeExpectation wrongPortable = expectation;
    wrongPortable.portableIdentitySha256 = Data::RuntimePackageCompilerSha256(
        QByteArray(32, '\x42'));
    QVERIFY(!CompilerPythonRuntimeProfile::load(
        Utils::FilePath::fromString(companionRoot),
        Utils::FilePath::fromString(runtimeRoot),
        wrongPortable));

    const QString identityPath
        = QDir(runtimeRoot).filePath(QStringLiteral("runtime-identity-v1.json"));
    QByteArray nonCanonicalIdentity = QJsonDocument(identity).toJson(QJsonDocument::Compact);
    nonCanonicalIdentity.append('\n');
    QVERIFY(nonCanonicalIdentity != pythonCanonicalJson(identity));
    QVERIFY(writeFile(identityPath, nonCanonicalIdentity));
    QVERIFY(::chmod(QFile::encodeName(identityPath).constData(), 0600) == 0);
    QVERIFY(!CompilerPythonRuntimeProfile::load(
        Utils::FilePath::fromString(companionRoot),
        Utils::FilePath::fromString(runtimeRoot),
        expectation));
    QVERIFY(writeIdentity());

    QVERIFY(::unlink(QFile::encodeName(pythonSymlink).constData()) == 0);
    QVERIFY(::symlink("../LICENSE.txt", QFile::encodeName(pythonSymlink).constData()) == 0);
    QVERIFY(!loaded->validateCurrent());
    QVERIFY(::unlink(QFile::encodeName(pythonSymlink).constData()) == 0);
    QVERIFY(::symlink("python3.11", QFile::encodeName(pythonSymlink).constData()) == 0);

    const QString licenseFile
        = QDir(runtimeRoot).filePath(QStringLiteral("python/lib/python3.11/LICENSE.txt"));
    const QByteArray originalLicense = readFile(licenseFile);
    QVERIFY(writeFile(licenseFile, originalLicense + QByteArray("tamper")));
    QVERIFY(::chmod(QFile::encodeName(licenseFile).constData(), 0644) == 0);
    QVERIFY(!loaded->validateCurrent());
    QVERIFY(writeFile(licenseFile, originalLicense));
    QVERIFY(::chmod(QFile::encodeName(licenseFile).constData(), 0644) == 0);

    const QString emptyDirectory = QDir(runtimeRoot).filePath(QStringLiteral("unsigned-empty"));
    QVERIFY(QDir().mkpath(emptyDirectory));
    QVERIFY(!loaded->validateCurrent());
    QVERIFY(QDir().rmdir(emptyDirectory));

    const QString hardLink = QDir(runtimeRoot).filePath(QStringLiteral("hard-link"));
    QVERIFY(
        ::link(QFile::encodeName(licenseFile).constData(), QFile::encodeName(hardLink).constData())
        == 0);
    QVERIFY(!loaded->validateCurrent());
    QVERIFY(::unlink(QFile::encodeName(hardLink).constData()) == 0);

    const QString fifo = QDir(runtimeRoot).filePath(QStringLiteral("unexpected-fifo"));
    QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0);
    QVERIFY(!loaded->validateCurrent());
    QVERIFY(::unlink(QFile::encodeName(fifo).constData()) == 0);

    const QString contractFile = QDir(companionRoot).filePath(contractPath);
    QVERIFY(writeFile(contractFile, contractBytes + QByteArray("tamper")));
    QVERIFY(::chmod(QFile::encodeName(contractFile).constData(), 0644) == 0);
    QVERIFY(!loaded->validateCurrent());
    QVERIFY(writeFile(contractFile, contractBytes));
    QVERIFY(::chmod(QFile::encodeName(contractFile).constData(), 0644) == 0);
    QVERIFY_RESULT(loaded->validateCurrent());

    const QString compilerContainer = QDir(base).filePath(QStringLiteral("compiler-container"));
    const QString compilerRoot = QDir(compilerContainer).filePath(QStringLiteral("runtime"));
    QVERIFY(QDir().mkpath(compilerRoot));
    QVERIFY(::chmod(QFile::encodeName(compilerContainer).constData(), 0700) == 0);

    QByteArray compilerSeed(32, '\x68');
    QByteArray compilerSecretKey(64, '\0');
    QByteArray compilerPublicKey(32, '\0');
    crypto_ed25519_key_pair(
        reinterpret_cast<uint8_t *>(compilerSecretKey.data()),
        reinterpret_cast<uint8_t *>(compilerPublicKey.data()),
        reinterpret_cast<uint8_t *>(compilerSeed.data()));
    const QByteArray compilerKeyId = sha256(compilerPublicKey).value().toHex();
    const QString compilerTrustPath
        = QStringLiteral("trust/%1.pub").arg(QString::fromLatin1(compilerKeyId));
    const QString compilerRequirementsPath = QStringLiteral(
        "runtime/igh_osless/contracts/compiler-runtime-requirements-v1.txt");
    const QByteArray compilerRequirements("jsonschema==4.25.0\n");
    const QByteArray fakeCompiler = readFile(
        QString::fromUtf8(ETHERCAT_PROJECT_COMPILER_FAKE_EXECUTABLE));
    QVERIFY(!fakeCompiler.isEmpty());
    const QString environmentEvidencePath = QDir(base).filePath(
        QStringLiteral("compiler-environment.txt"));
    const QString compilerImportRoot
        = QDir(compilerRoot).filePath(QStringLiteral("runtime/igh_osless/tools"));
    const QByteArray compilerWrapper
        = QStringLiteral(
              "#!/bin/sh\n"
              "/usr/bin/env > \"%3\"\n"
              "[ \"${EMBEDLABS_COMPILER_PYTHON-}\" = \"%1\" ] || exit 90\n"
              "[ \"${PYTHONPATH-}\" = \"%2\" ] || exit 91\n"
              "[ \"${PYTHONSAFEPATH-}\" = \"1\" ] || exit 92\n"
              "[ \"${PATH-}\" = \"/usr/bin:/bin\" ] || exit 93\n"
              "[ \"${LANG-}\" = \"C\" ] || exit 94\n"
              "[ \"${LC_ALL-}\" = \"C\" ] || exit 95\n"
              "[ \"${TZ-}\" = \"UTC\" ] || exit 96\n"
              "[ \"${PYTHONNOUSERSITE-}\" = \"1\" ] || exit 97\n"
              "[ \"${PYTHONDONTWRITEBYTECODE-}\" = \"1\" ] || exit 98\n"
              "[ \"${PYTHONUTF8-}\" = \"1\" ] || exit 99\n"
              "[ \"${PYTHONIOENCODING-}\" = \"utf-8\" ] || exit 100\n"
              "[ \"${PYTHONHASHSEED-}\" = \"0\" ] || exit 101\n"
              "[ \"${PYTHONBREAKPOINT-}\" = \"0\" ] || exit 102\n"
              "[ -z \"${HOME+x}\" ] || exit 103\n"
              "[ -z \"${PYTHONHOME+x}\" ] || exit 104\n"
              "exec \"${0%/*}/../runtime/fakecompiler\" \"$@\" 2>/dev/null\n")
              .arg(
                  loaded->pythonExecutable().path(),
                  compilerImportRoot,
                  environmentEvidencePath)
              .toUtf8();
    const QMap<QString, QPair<QByteArray, int>> compilerPayload{
        {QStringLiteral("bin/embedlabs-ecpkg-compiler"), {compilerWrapper, 0755}},
        {QStringLiteral("bin/embedlabs-ecpkg-compiler-provision"),
         {QByteArray("#!/bin/sh\n"), 0755}},
        {QStringLiteral("bin/embedlabs-ecpkg-compiler-self-test"),
         {QByteArray("#!/bin/sh\n"), 0755}},
        {QStringLiteral("bin/embedlabs-ecpkg-compiler-verify"),
         {QByteArray("#!/bin/sh\n"), 0755}},
        {compilerRequirementsPath, {compilerRequirements, 0644}},
        {QStringLiteral("runtime/fakecompiler"), {fakeCompiler, 0755}},
        {QStringLiteral("runtime/igh_osless/tools/compiler_runtime_entry.py"),
         {QByteArray("# signed fixture compiler entry\n"), 0644}},
        {QStringLiteral("runtime/igh_osless/tools/ide_project_compiler.py"),
         {QByteArray("# signed fixture project compiler\n"), 0644}},
        {compilerTrustPath, {compilerPublicKey, 0644}},
    };
    QJsonArray compilerFileRecords;
    for (auto it = compilerPayload.cbegin(); it != compilerPayload.cend(); ++it) {
        const QString path = QDir(compilerRoot).filePath(it.key());
        QVERIFY(writeFile(path, it->first));
        QVERIFY(::chmod(QFile::encodeName(path).constData(), it->second) == 0);
        compilerFileRecords.append(
            QJsonObject{{QStringLiteral("bytes"), it->first.size()},
                        {QStringLiteral("mode"), it->second},
                        {QStringLiteral("path"), it.key()},
                        {QStringLiteral("sha256"),
                         QString::fromLatin1(sha256(it->first).value().toHex())}});
    }
    const QJsonObject compilerManifest{
        {QStringLiteral("artifact_root_contract"),
         QJsonObject{{QStringLiteral("explicit_argument"), QStringLiteral("--artifact-root")},
                     {QStringLiteral("request_paths"),
                      QStringLiteral("relative_to_artifact_root")},
                     {QStringLiteral("symlinks_allowed"), false},
                     {QStringLiteral("working_directory_independent"), true}}},
        {QStringLiteral("bundle_id"), QStringLiteral("org.embedlabs.ethercat.project-compiler")},
        {QStringLiteral("bundle_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("compiler_contract"),
         QJsonObject{{QStringLiteral("ecpkg_versions"), QJsonArray{2}},
                     {QStringLiteral("implementation"),
                      QStringLiteral("ethercat-ide-project-compiler-1.0.0")},
                     {QStringLiteral("name"),
                      QStringLiteral("ethercat-ide-project-compiler")},
                     {QStringLiteral("version"), 1}}},
        {QStringLiteral("entrypoints"),
         QJsonObject{{QStringLiteral("compiler"),
                      QStringLiteral("bin/embedlabs-ecpkg-compiler")},
                     {QStringLiteral("provision"),
                      QStringLiteral("bin/embedlabs-ecpkg-compiler-provision")},
                     {QStringLiteral("self_test"),
                      QStringLiteral("bin/embedlabs-ecpkg-compiler-self-test")},
                     {QStringLiteral("verify"),
                      QStringLiteral("bin/embedlabs-ecpkg-compiler-verify")}}},
        {QStringLiteral("files"), compilerFileRecords},
        {QStringLiteral("format"), QStringLiteral("embedlabs-external-compiler-runtime-bundle-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("limits"),
         QJsonObject{{QStringLiteral("max_files"), 256},
                     {QStringLiteral("max_path_bytes"), 240},
                     {QStringLiteral("max_single_file_bytes"), 16777216},
                     {QStringLiteral("max_total_uncompressed_bytes"), 33554432}}},
        {QStringLiteral("runtime"),
         QJsonObject{{QStringLiteral("implementation"), QStringLiteral("CPython")},
                     {QStringLiteral("native_windows_supported"), false},
                     {QStringLiteral("platforms"),
                      QJsonArray{QStringLiteral("darwin"), QStringLiteral("linux")}},
                     {QStringLiteral("python_maximum_exclusive"), QStringLiteral("3.13.0")},
                     {QStringLiteral("python_minimum"), QStringLiteral("3.11.0")},
                     {QStringLiteral("requirements_path"), compilerRequirementsPath},
                     {QStringLiteral("requirements_sha256"),
                      QString::fromLatin1(sha256(compilerRequirements).value().toHex())}}},
        {QStringLiteral("signature"),
         QJsonObject{{QStringLiteral("algorithm"), QStringLiteral("ed25519")},
                     {QStringLiteral("domain"),
                      QStringLiteral("embedlabs-ethercat-compiler-runtime-bundle-v1")},
                     {QStringLiteral("key_id"), QString::fromLatin1(compilerKeyId)},
                     {QStringLiteral("signature_file"), QStringLiteral("manifest.sig")}}},
    };
    QByteArray compilerManifestBytes = QJsonDocument(compilerManifest).toJson(
        QJsonDocument::Compact);
    compilerManifestBytes.append('\n');
    QByteArray compilerSignedBytes("embedlabs-ethercat-compiler-runtime-bundle-v1");
    compilerSignedBytes.append('\0');
    compilerSignedBytes.append(compilerManifestBytes);
    QByteArray compilerSignature(64, '\0');
    crypto_ed25519_sign(
        reinterpret_cast<uint8_t *>(compilerSignature.data()),
        reinterpret_cast<const uint8_t *>(compilerSecretKey.constData()),
        reinterpret_cast<const uint8_t *>(compilerSignedBytes.constData()),
        size_t(compilerSignedBytes.size()));
    compilerSecretKey.fill('\0');
    compilerSeed.fill('\0');
    const QString compilerManifestPath
        = QDir(compilerRoot).filePath(QStringLiteral("manifest.json"));
    const QString compilerSignaturePath
        = QDir(compilerRoot).filePath(QStringLiteral("manifest.sig"));
    QVERIFY(writeFile(compilerManifestPath, compilerManifestBytes));
    QVERIFY(writeFile(compilerSignaturePath, compilerSignature));
    QVERIFY(::chmod(QFile::encodeName(compilerManifestPath).constData(), 0644) == 0);
    QVERIFY(::chmod(QFile::encodeName(compilerSignaturePath).constData(), 0644) == 0);
    const CompilerRuntimeBundleExpectation compilerExpectation{
        QStringLiteral("1.0.0"), compilerPublicKey, sha256(compilerManifestBytes)};
    const auto compilerProfile = CompilerRuntimeBundleProfile::load(
        Utils::FilePath::fromString(compilerRoot), compilerExpectation);
    QVERIFY_RESULT(compilerProfile);
    QCOMPARE(compilerProfile->compilerImportRoot().path(), compilerImportRoot);

    TestEnvironment environment;
    environment.rewriteProfile(compilerProfile->compilerExecutable().path());
    RuntimePackageCompilerProcessLimits processLimits;
    processLimits.commandTimeout = 10s;
    processLimits.queryTimeout = 2s;
    processLimits.cancellationGrace = 50ms;
    const auto makeProvider = [&] {
        return std::make_unique<ProvisionedRuntimePackageCompilerProvider>(
            Utils::FilePath::fromString(environment.provisioning),
            Utils::FilePath::fromString(environment.root),
            *compilerProfile,
            *loaded,
            processLimits);
    };
    auto provider = makeProvider();
    QVERIFY(provider->isAvailable());
    const auto scheduled = provider->compile(environment.fixture.request);
    QVERIFY_RESULT(scheduled);
    QVERIFY(awaitTerminal(*scheduled));
    const Data::RuntimePackageCompilerCompileResult *compiled = compileResult(**scheduled);
    const QByteArray executionEvidence
        = readFile(environmentEvidencePath) + "\nCALLS\n"
          + readFile(environment.root + QStringLiteral("/compiler-ledger.json.calls"))
          + "\nCOMPLETION "
          + QByteArray::number(int((*scheduled)->completionError()));
    QVERIFY2(compiled && compiled->isSuccess(), executionEvidence.constData());

    QMap<QByteArray, QByteArray> observedEnvironment;
    for (const QByteArray &line : readFile(environmentEvidencePath).split('\n')) {
        if (line.isEmpty())
            continue;
        const qsizetype separator = line.indexOf('=');
        QVERIFY(separator > 0);
        observedEnvironment.insert(line.first(separator), line.sliced(separator + 1));
    }
    const QMap<QByteArray, QByteArray> fixedEnvironment{
        {QByteArray("EMBEDLABS_COMPILER_PYTHON"),
         loaded->pythonExecutable().toFSPathString().toUtf8()},
        {QByteArray("LANG"), QByteArray("C")},
        {QByteArray("LC_ALL"), QByteArray("C")},
        {QByteArray("PATH"), QByteArray("/usr/bin:/bin")},
        {QByteArray("PYTHONBREAKPOINT"), QByteArray("0")},
        {QByteArray("PYTHONDONTWRITEBYTECODE"), QByteArray("1")},
        {QByteArray("PYTHONHASHSEED"), QByteArray("0")},
        {QByteArray("PYTHONIOENCODING"), QByteArray("utf-8")},
        {QByteArray("PYTHONNOUSERSITE"), QByteArray("1")},
        {QByteArray("PYTHONPATH"), compilerProfile->compilerImportRoot().toFSPathString().toUtf8()},
        {QByteArray("PYTHONSAFEPATH"), QByteArray("1")},
        {QByteArray("PYTHONUTF8"), QByteArray("1")},
        {QByteArray("TZ"), QByteArray("UTC")},
    };
    for (auto it = fixedEnvironment.cbegin(); it != fixedEnvironment.cend(); ++it)
        QCOMPARE(observedEnvironment.value(it.key()), it.value());
    QSet<QByteArray> remainingEnvironment(observedEnvironment.keyBegin(), observedEnvironment.keyEnd());
    for (auto it = fixedEnvironment.cbegin(); it != fixedEnvironment.cend(); ++it)
        remainingEnvironment.remove(it.key());
    const QSet<QByteArray> permittedShellEnvironment{
        QByteArray("PWD"), QByteArray("SHLVL"), QByteArray("_")};
    for (const QByteArray &key : std::as_const(remainingEnvironment))
        QVERIFY2(permittedShellEnvironment.contains(key), key.constData());
    const QSet<QByteArray> forbiddenEnvironment{
        QByteArray("ALL_PROXY"),
        QByteArray("BASH_ENV"),
        QByteArray("CONDA_PREFIX"),
        QByteArray("DYLD_INSERT_LIBRARIES"),
        QByteArray("DYLD_LIBRARY_PATH"),
        QByteArray("ENV"),
        QByteArray("HOME"),
        QByteArray("HTTPS_PROXY"),
        QByteArray("HTTP_PROXY"),
        QByteArray("LD_LIBRARY_PATH"),
        QByteArray("LD_PRELOAD"),
        QByteArray("NO_PROXY"),
        QByteArray("PYTHONHOME"),
        QByteArray("SHELL"),
        QByteArray("SSL_CERT_DIR"),
        QByteArray("SSL_CERT_FILE"),
        QByteArray("TMPDIR"),
        QByteArray("USER"),
        QByteArray("VIRTUAL_ENV"),
    };
    for (const QByteArray &key : forbiddenEnvironment)
        QVERIFY(!observedEnvironment.contains(key));
    const QString pinnedCompiler = QDir(environment.root)
                                       .filePath(QStringLiteral("provisioned/%1/compiler")
                                                     .arg(QString::fromLatin1(
                                                         sha256(compilerWrapper).value().toHex())));
    QVERIFY(!QFileInfo::exists(pinnedCompiler));

    struct TamperTarget
    {
        QString path;
        QByteArray exactBytes;
        int mode = 0;
    };
    const TamperTarget compilerTarget{
        QDir(compilerRoot).filePath(compilerRequirementsPath), compilerRequirements, 0644};
    const TamperTarget pythonRuntimeTarget{licenseFile, originalLicense, 0644};
    const TamperTarget pythonCompanionTarget{contractFile, contractBytes, 0644};
    const auto rewriteTarget = [](const TamperTarget &target, bool tamper) {
        const QByteArray bytes
            = tamper ? target.exactBytes + QByteArray("tamper") : target.exactBytes;
        return writeFile(target.path, bytes)
               && ::chmod(QFile::encodeName(target.path).constData(), target.mode) == 0;
    };
    const auto requestFor = [&](QStringView operationId, quint64 configurationId) {
        auto request = environment.fixture.request;
        request.operationId = Data::RuntimePackageCompilerOperationId{operationId.toString()};
        request.configurationId = configurationId;
        return request;
    };
    const auto profilesAreCurrent = [&] {
        return bool(compilerProfile->validateCurrent()) && bool(loaded->validateCurrent());
    };

    const auto preLaunchTamper = [&](const TamperTarget &target,
                                     QStringView operationId,
                                     quint64 configurationId) {
        provider.reset();
        provider = makeProvider();
        if (!provider->isAvailable())
            return false;
        const auto request = requestFor(operationId, configurationId);
        const auto job = provider->compile(request);
        if (!job || !rewriteTarget(target, true))
            return false;
        QByteArray expectedCall("compile ");
        expectedCall.append(request.operationId.value().toLatin1());
        expectedCall.append('\n');
        const bool rejected
            = awaitTerminal(*job)
              && (*job)->completionError()
                     == Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure
              && !(*job)->result()
              && !readFile(environment.root + QStringLiteral("/compiler-ledger.json.calls"))
                      .contains(expectedCall);
        return rewriteTarget(target, false) && profilesAreCurrent() && rejected;
    };
    QVERIFY(preLaunchTamper(
        compilerTarget,
        u"04204204-2001-4000-8000-000000001001",
        4211));
    QVERIFY(preLaunchTamper(
        pythonRuntimeTarget,
        u"04204204-2001-4000-8000-000000001002",
        4212));

    const auto reconciliationTamper = [&](const TamperTarget &target,
                                          QStringView operationId,
                                          quint64 configurationId) {
        provider.reset();
        provider = makeProvider();
        if (!provider->isAvailable())
            return false;
        const auto request = requestFor(operationId, configurationId);
        const auto job = provider->compile(request);
        if (!job)
            return false;
        (*job)->cancel();
        if (!rewriteTarget(target, true))
            return false;
        QByteArray expectedCall("query ");
        expectedCall.append(request.operationId.value().toLatin1());
        expectedCall.append('\n');
        const bool rejected
            = awaitTerminal(*job)
              && (*job)->completionError()
                     == Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure
              && !(*job)->result()
              && !readFile(environment.root + QStringLiteral("/compiler-ledger.json.calls"))
                      .contains(expectedCall);
        return rewriteTarget(target, false) && profilesAreCurrent() && rejected;
    };
    QVERIFY(reconciliationTamper(
        compilerTarget,
        u"04204204-2001-4000-8000-000000002001",
        4221));
    QVERIFY(reconciliationTamper(
        pythonCompanionTarget,
        u"04204204-2001-4000-8000-000000002002",
        4222));

    const auto postRunTamper = [&](const TamperTarget &target,
                                   QStringView operationId,
                                   quint64 configurationId) {
        provider.reset();
        provider = makeProvider();
        if (!provider->isAvailable())
            return false;
        const auto request = requestFor(operationId, configurationId);
        const auto job = provider->compile(request);
        if (!job || !awaitJobState(*job, Core::RuntimePackageCompilerJobState::Running)
            || !rewriteTarget(target, true)) {
            return false;
        }
        const bool rejected
            = awaitTerminal(*job)
              && (*job)->completionError()
                     == Core::RuntimePackageCompilerJobCompletionError::BackendProcessFailure
              && !(*job)->result()
              && QFileInfo::exists(
                  provider->operationRoot(request.operationId).path()
                  + QStringLiteral("/output/sign-request.json"));
        return rewriteTarget(target, false) && profilesAreCurrent() && rejected;
    };
    QVERIFY(postRunTamper(
        compilerTarget,
        u"04204204-2001-4000-8000-000000000004",
        4231));
    QVERIFY(postRunTamper(
        pythonRuntimeTarget,
        u"04204204-2001-4000-8000-000000010004",
        4232));
#endif
}

void EtherCATProjectCompilerTests::testRuntimeBundleProfileVerifiesInstalledTree()
{
#ifndef Q_OS_UNIX
    QSKIP("The external compiler runtime contract requires a POSIX host.");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString container
        = QDir(QFileInfo(temporary.path()).canonicalFilePath()).filePath("runtime-container");
    const QString root = QDir(container).filePath("runtime");
    QVERIFY(QDir().mkpath(root));
    QVERIFY(::chmod(QFile::encodeName(container).constData(), 0700) == 0);

    QByteArray seed(32, '\x58');
    QByteArray secretKey(64, '\0');
    QByteArray publicKey(32, '\0');
    crypto_ed25519_key_pair(
        reinterpret_cast<uint8_t *>(secretKey.data()),
        reinterpret_cast<uint8_t *>(publicKey.data()),
        reinterpret_cast<uint8_t *>(seed.data()));
    const QByteArray keyId = sha256(publicKey).value().toHex();
    const QString trustPath = QStringLiteral("trust/%1.pub").arg(QString::fromLatin1(keyId));
    const QString requirementsPath = QStringLiteral(
        "runtime/igh_osless/contracts/compiler-runtime-requirements-v1.txt");
    const QByteArray requirements("jsonschema==4.25.0\n");
    const QMap<QString, QPair<QByteArray, int>> payload{
        {QStringLiteral("bin/embedlabs-ecpkg-compiler"), {QByteArray("#!/bin/sh\n"), 0755}},
        {QStringLiteral("bin/embedlabs-ecpkg-compiler-provision"),
         {QByteArray("#!/bin/sh\n"), 0755}},
        {QStringLiteral("bin/embedlabs-ecpkg-compiler-self-test"),
         {QByteArray("#!/bin/sh\n"), 0755}},
        {QStringLiteral("bin/embedlabs-ecpkg-compiler-verify"),
         {QByteArray("#!/bin/sh\n"), 0755}},
        {requirementsPath, {requirements, 0644}},
        {QStringLiteral("runtime/igh_osless/tools/compiler_runtime_entry.py"),
         {QByteArray("# fixture compiler entry\n"), 0644}},
        {QStringLiteral("runtime/igh_osless/tools/ide_project_compiler.py"),
         {QByteArray("# fixture project compiler\n"), 0644}},
        {trustPath, {publicKey, 0644}},
    };

    QJsonArray fileRecords;
    for (auto it = payload.cbegin(); it != payload.cend(); ++it) {
        QVERIFY(writeFile(QDir(root).filePath(it.key()), it->first));
        QVERIFY(::chmod(QFile::encodeName(QDir(root).filePath(it.key())).constData(), it->second)
                == 0);
        fileRecords.append(QJsonObject{
            {QStringLiteral("bytes"), it->first.size()},
            {QStringLiteral("mode"), it->second},
            {QStringLiteral("path"), it.key()},
            {QStringLiteral("sha256"), QString::fromLatin1(sha256(it->first).value().toHex())},
        });
    }

    const QJsonObject manifest{
        {QStringLiteral("artifact_root_contract"),
         QJsonObject{
             {QStringLiteral("explicit_argument"), QStringLiteral("--artifact-root")},
             {QStringLiteral("request_paths"), QStringLiteral("relative_to_artifact_root")},
             {QStringLiteral("symlinks_allowed"), false},
             {QStringLiteral("working_directory_independent"), true},
         }},
        {QStringLiteral("bundle_id"), QStringLiteral("org.embedlabs.ethercat.project-compiler")},
        {QStringLiteral("bundle_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("compiler_contract"),
         QJsonObject{
             {QStringLiteral("ecpkg_versions"), QJsonArray{2}},
             {QStringLiteral("implementation"),
              QStringLiteral("ethercat-ide-project-compiler-1.0.0")},
             {QStringLiteral("name"), QStringLiteral("ethercat-ide-project-compiler")},
             {QStringLiteral("version"), 1},
         }},
        {QStringLiteral("entrypoints"),
         QJsonObject{
             {QStringLiteral("compiler"), QStringLiteral("bin/embedlabs-ecpkg-compiler")},
             {QStringLiteral("provision"),
              QStringLiteral("bin/embedlabs-ecpkg-compiler-provision")},
             {QStringLiteral("self_test"),
              QStringLiteral("bin/embedlabs-ecpkg-compiler-self-test")},
             {QStringLiteral("verify"), QStringLiteral("bin/embedlabs-ecpkg-compiler-verify")},
         }},
        {QStringLiteral("files"), fileRecords},
        {QStringLiteral("format"), QStringLiteral("embedlabs-external-compiler-runtime-bundle-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("limits"),
         QJsonObject{
             {QStringLiteral("max_files"), 256},
             {QStringLiteral("max_path_bytes"), 240},
             {QStringLiteral("max_single_file_bytes"), 16777216},
             {QStringLiteral("max_total_uncompressed_bytes"), 33554432},
         }},
        {QStringLiteral("runtime"),
         QJsonObject{
             {QStringLiteral("implementation"), QStringLiteral("CPython")},
             {QStringLiteral("native_windows_supported"), false},
             {QStringLiteral("platforms"), QJsonArray{QStringLiteral("darwin"),
                                                       QStringLiteral("linux")}},
             {QStringLiteral("python_maximum_exclusive"), QStringLiteral("3.13.0")},
             {QStringLiteral("python_minimum"), QStringLiteral("3.11.0")},
             {QStringLiteral("requirements_path"), requirementsPath},
             {QStringLiteral("requirements_sha256"),
              QString::fromLatin1(sha256(requirements).value().toHex())},
         }},
        {QStringLiteral("signature"),
         QJsonObject{
             {QStringLiteral("algorithm"), QStringLiteral("ed25519")},
             {QStringLiteral("domain"),
              QStringLiteral("embedlabs-ethercat-compiler-runtime-bundle-v1")},
             {QStringLiteral("key_id"), QString::fromLatin1(keyId)},
             {QStringLiteral("signature_file"), QStringLiteral("manifest.sig")},
         }},
    };
    QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    manifestBytes.append('\n');
    QByteArray signedBytes("embedlabs-ethercat-compiler-runtime-bundle-v1");
    signedBytes.append('\0');
    signedBytes.append(manifestBytes);
    QByteArray signature(64, '\0');
    crypto_ed25519_sign(
        reinterpret_cast<uint8_t *>(signature.data()),
        reinterpret_cast<const uint8_t *>(secretKey.constData()),
        reinterpret_cast<const uint8_t *>(signedBytes.constData()),
        size_t(signedBytes.size()));
    secretKey.fill('\0');
    seed.fill('\0');
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("manifest.json")), manifestBytes));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("manifest.sig")), signature));
    QVERIFY(::chmod(QFile::encodeName(QDir(root).filePath("manifest.json")).constData(), 0644) == 0);
    QVERIFY(::chmod(QFile::encodeName(QDir(root).filePath("manifest.sig")).constData(), 0644) == 0);

    const CompilerRuntimeBundleExpectation expectation{
        QStringLiteral("1.0.0"), publicKey, sha256(manifestBytes)};
    const auto loaded = CompilerRuntimeBundleProfile::load(
        Utils::FilePath::fromString(root), expectation);
    QVERIFY_RESULT(loaded);
    QVERIFY(loaded->identity().isValid());
    QCOMPARE(
        loaded->compilerExecutable().path(),
        QDir(root).filePath(QStringLiteral("bin/embedlabs-ecpkg-compiler")));
    QCOMPARE(
        loaded->compilerImportRoot().path(),
        QDir(root).filePath(QStringLiteral("runtime/igh_osless/tools")));
    QVERIFY_RESULT(loaded->validateCurrent());

    QVERIFY(::chmod(QFile::encodeName(container).constData(), 0777) == 0);
    QVERIFY(!CompilerRuntimeBundleProfile::load(Utils::FilePath::fromString(root), expectation));
    QVERIFY(::chmod(QFile::encodeName(container).constData(), 0700) == 0);
    QVERIFY_RESULT(loaded->validateCurrent());

#ifdef Q_OS_DARWIN
    const auto setExtendedAcl = [&container](QByteArrayView text) {
        acl_t accessControlList = ::acl_from_text(QByteArray(text).constData());
        if (!accessControlList)
            return false;
        const bool stored = ::acl_set_file(
                                QFile::encodeName(container).constData(),
                                ACL_TYPE_EXTENDED,
                                accessControlList)
                            == 0;
        ::acl_free(accessControlList);
        return stored;
    };
    const QByteArray denyAcl(
        "!#acl 1\n"
        "group:ABCDEFAB-CDEF-ABCD-EFAB-CDEF0000000C:everyone:12:deny:write\n");
    QVERIFY(setExtendedAcl(denyAcl));
    QVERIFY_RESULT(loaded->validateCurrent());
    const QByteArray allowAcl(
        "!#acl 1\n"
        "group:ABCDEFAB-CDEF-ABCD-EFAB-CDEF0000000C:everyone:12:allow:write\n");
    QVERIFY(setExtendedAcl(allowAcl));
    QVERIFY(!CompilerRuntimeBundleProfile::load(Utils::FilePath::fromString(root), expectation));
    acl_t emptyAcl = ::acl_init(0);
    QVERIFY(emptyAcl);
    QVERIFY(::acl_set_file(
                QFile::encodeName(container).constData(), ACL_TYPE_EXTENDED, emptyAcl)
            == 0);
    ::acl_free(emptyAcl);
    QVERIFY_RESULT(loaded->validateCurrent());
#endif

    CompilerRuntimeBundleExpectation wrongKey = expectation;
    wrongKey.signingPublicKey[0] = char(quint8(wrongKey.signingPublicKey.at(0)) ^ 0x01);
    QVERIFY(!CompilerRuntimeBundleProfile::load(Utils::FilePath::fromString(root), wrongKey));
    CompilerRuntimeBundleExpectation wrongManifest = expectation;
    wrongManifest.manifestSha256 = Data::RuntimePackageCompilerSha256(QByteArray(32, '\x01'));
    QVERIFY(!CompilerRuntimeBundleProfile::load(Utils::FilePath::fromString(root), wrongManifest));

    QFile compiler(QDir(root).filePath(QStringLiteral("bin/embedlabs-ecpkg-compiler")));
    QVERIFY(compiler.open(QIODevice::Append));
    QCOMPARE(compiler.write("tamper", 6), 6);
    compiler.close();
    QVERIFY(!loaded->validateCurrent());
#endif
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

void EtherCATProjectCompilerTests::testCompilerInputProvisioningVerifiesTargetSignature()
{
    CompilerFixture fixture;
    const auto &sources = fixture.request.sourceArtifacts;

    BuilderInputEnvironment valid(fixture);
    const auto loaded = CompilerInputProvisioningProfile::load(
        Utils::FilePath::fromString(valid.profilePath));
    QVERIFY_RESULT(loaded);
    QVERIFY(loaded->targetProfile().productionSigned);

    BuilderInputEnvironment changedTarget(fixture);
    QByteArray targetBytes = sources.targetProfile.exactBytes;
    targetBytes[0] = targetBytes[0] == '{' ? '[' : '{';
    QVERIFY(replaceCompilerInputArtifact(&changedTarget, sources.targetProfile, targetBytes));
    const auto targetRejected = CompilerInputProvisioningProfile::load(
        Utils::FilePath::fromString(changedTarget.profilePath));
    QVERIFY(!targetRejected);
    QVERIFY(targetRejected.error().contains(QStringLiteral("signature"), Qt::CaseInsensitive));

    BuilderInputEnvironment changedSignature(fixture);
    QByteArray signatureBytes = sources.targetProfileSignature.exactBytes;
    signatureBytes[0] = char(quint8(signatureBytes.at(0)) ^ 0x01);
    QVERIFY(replaceCompilerInputArtifact(
        &changedSignature, sources.targetProfileSignature, signatureBytes));
    const auto signatureRejected = CompilerInputProvisioningProfile::load(
        Utils::FilePath::fromString(changedSignature.profilePath));
    QVERIFY(!signatureRejected);
    QVERIFY(signatureRejected.error().contains(QStringLiteral("signature"), Qt::CaseInsensitive));

    BuilderInputEnvironment changedPublicKey(fixture);
    QByteArray publicKeyBytes = sources.productionPublicKey.exactBytes;
    publicKeyBytes[0] = char(quint8(publicKeyBytes.at(0)) ^ 0x01);
    QVERIFY(replaceCompilerInputArtifact(
        &changedPublicKey, sources.productionPublicKey, publicKeyBytes));
    const auto publicKeyRejected = CompilerInputProvisioningProfile::load(
        Utils::FilePath::fromString(changedPublicKey.profilePath));
    QVERIFY(!publicKeyRejected);
    QVERIFY(publicKeyRejected.error().contains(QStringLiteral("signature"), Qt::CaseInsensitive));
}

void EtherCATProjectCompilerTests::testCompileRecoveryRoundTrip()
{
    const CompilerFixture fixture;
    const RuntimePackageCompilerCompileRecovery recovery{fixture.request, fixture.serializedProject};
    QVERIFY(recovery.isValid());

    const auto first = encodeRuntimePackageCompilerCompileRecovery(recovery);
    QVERIFY(first);
    const auto second = encodeRuntimePackageCompilerCompileRecovery(recovery);
    QVERIFY(second);
    QCOMPARE(*second, *first);
    QVERIFY(first->endsWith('\n'));
    const QJsonObject recoveryObject = QJsonDocument::fromJson(*first).object();
    const QByteArray payload = QByteArray::fromBase64(
        recoveryObject.value(QStringLiteral("payload_base64")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    QCOMPARE(payload.first(8), QByteArray("ELPCRQ01", 8));
    QCOMPARE(payload.sliced(8, 4), QByteArray::fromHex("00000002"));

    const auto decoded = decodeRuntimePackageCompilerCompileRecovery(*first, sha256(*first));
    QVERIFY2(decoded, qPrintable(decoded.error()));
    QCOMPARE(*decoded, recovery);
    QCOMPARE(decoded->request.projectSnapshotEvidence, recovery.request.projectSnapshotEvidence);
    QCOMPARE(decoded->serializedProject, fixture.serializedProject);

    const auto reencoded = encodeRuntimePackageCompilerCompileRecovery(*decoded);
    QVERIFY(reencoded);
    QCOMPARE(*reencoded, *first);

    const auto lowerCanonical = Core::encodeRuntimePackageCompilerCompileRequest(fixture.request);
    QVERIFY(lowerCanonical);
    QVERIFY(!decodeRuntimePackageCompilerCompileRecovery(
        lowerCanonical->exactBytes(), lowerCanonical->sha256()));
    QVERIFY(!decodeRuntimePackageCompilerCompileRecovery(*first, sha256("wrong anchor")));

    RuntimePackageCompilerCompileRecovery wrongProject = recovery;
    wrongProject.serializedProject.append(' ');
    QVERIFY(!wrongProject.isValid());
    QVERIFY(!encodeRuntimePackageCompilerCompileRecovery(wrongProject));
}

void EtherCATProjectCompilerTests::testCompileRecoveryVersionOneCompatibility()
{
    const CompilerFixture fixture;
    const RuntimePackageCompilerCompileRecovery recovery{fixture.request, fixture.serializedProject};
    const auto encoded = encodeRuntimePackageCompilerCompileRecovery(recovery);
    QVERIFY(encoded);

    QJsonObject root = QJsonDocument::fromJson(*encoded).object();
    QByteArray payload = QByteArray::fromBase64(
        root.value(QStringLiteral("payload_base64")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    QVERIFY(!payload.isEmpty());
    QCOMPARE(payload.sliced(8, 4), QByteArray::fromHex("00000002"));

    QByteArray slaveTail(9, '\0');
    slaveTail.append(QByteArray::fromHex("1001"));
    slaveTail.append(QByteArray(4, '\0'));
    slaveTail.append(QByteArray::fromHex("000000010001e848"));
    slaveTail.append(QByteArray(16, '\0'));
    QCOMPARE(fixture.serializedProject.size(), 0x29);
    slaveTail.append(QByteArray::fromHex("00000029"));
    slaveTail.append(fixture.serializedProject);
    const qsizetype tailOffset = payload.indexOf(slaveTail);
    QVERIFY(tailOffset >= 0);
    QCOMPARE(payload.lastIndexOf(slaveTail), tailOffset);

    const qsizetype deviceParameterCountOffset = tailOffset + 9 + 2;
    QCOMPARE(payload.sliced(deviceParameterCountOffset, 4), QByteArray(4, '\0'));
    payload.remove(deviceParameterCountOffset, 4);
    payload.replace(8, 4, QByteArray::fromHex("00000001"));
    root.insert(
        QStringLiteral("payload_base64"), QString::fromLatin1(payload.toBase64()));
    root.insert(
        QStringLiteral("payload_sha256"), QString::fromLatin1(sha256(payload).value().toHex()));
    QByteArray legacyRecovery = QJsonDocument(root).toJson(QJsonDocument::Compact);
    legacyRecovery.append('\n');

    const auto decoded
        = decodeRuntimePackageCompilerCompileRecovery(legacyRecovery, sha256(legacyRecovery));
    QVERIFY2(decoded, qPrintable(decoded.error()));
    QCOMPARE(*decoded, recovery);
    QVERIFY(decoded->request.projectSnapshotEvidence.snapshot()
                .slaves.constFirst()
                .deviceParameters.values.isEmpty());
}

void EtherCATProjectCompilerTests::testCompileRecoveryRejectsMutations()
{
    const CompilerFixture fixture;
    const auto encoded = encodeRuntimePackageCompilerCompileRecovery(
        {fixture.request, fixture.serializedProject});
    QVERIFY(encoded);

    auto canonicalObject = [](QJsonObject object) {
        QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        bytes.append('\n');
        return bytes;
    };
    const QJsonObject original = QJsonDocument::fromJson(*encoded).object();
    QVERIFY(!original.isEmpty());
    QVERIFY(!decodeRuntimePackageCompilerCompileRecovery(*encoded, sha256("wrong anchor")));

    QList<QByteArray> malformed;
    QByteArray missingNewline = *encoded;
    missingNewline.chop(1);
    malformed.append(missingNewline);
    malformed.append(*encoded + QByteArray("\n"));
    malformed.append(QJsonDocument(original).toJson(QJsonDocument::Indented));

    QJsonObject missing = original;
    missing.remove(QStringLiteral("payload_sha256"));
    malformed.append(canonicalObject(missing));
    QJsonObject extra = original;
    extra.insert(QStringLiteral("unexpected"), true);
    malformed.append(canonicalObject(extra));
    QJsonObject wrongFormat = original;
    wrongFormat.insert(QStringLiteral("format"), QStringLiteral("wrong"));
    malformed.append(canonicalObject(wrongFormat));
    QJsonObject wrongVersion = original;
    wrongVersion.insert(QStringLiteral("format_version"), 2);
    malformed.append(canonicalObject(wrongVersion));
    QJsonObject invalidBase64 = original;
    invalidBase64.insert(QStringLiteral("payload_base64"), QStringLiteral("***"));
    malformed.append(canonicalObject(invalidBase64));
    QJsonObject upperDigest = original;
    upperDigest.insert(
        QStringLiteral("payload_sha256"),
        original.value(QStringLiteral("payload_sha256")).toString().toUpper());
    malformed.append(canonicalObject(upperDigest));
    QJsonObject wrongCompileDigest = original;
    wrongCompileDigest.insert(QStringLiteral("compile_request_sha256"), QString(64, '1'));
    malformed.append(canonicalObject(wrongCompileDigest));

    const QByteArray payload = QByteArray::fromBase64(
        original.value(QStringLiteral("payload_base64")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    QVERIFY(!payload.isEmpty());
    const QList<QByteArray> changedPayloads{
        payload.left(payload.size() - 1),
        payload + QByteArray("\0", 1),
    };
    for (const QByteArray &changedPayload : changedPayloads) {
        QJsonObject changed = original;
        changed
            .insert(QStringLiteral("payload_base64"), QString::fromLatin1(changedPayload.toBase64()));
        changed.insert(
            QStringLiteral("payload_sha256"),
            QString::fromLatin1(sha256(changedPayload).value().toHex()));
        malformed.append(canonicalObject(changed));
    }

    QByteArray changedOperation = payload;
    QVERIFY(changedOperation.size() > 16);
    changedOperation[16] = changedOperation.at(16) == '0' ? '1' : '0';
    QJsonObject changed = original;
    changed
        .insert(QStringLiteral("payload_base64"), QString::fromLatin1(changedOperation.toBase64()));
    changed.insert(
        QStringLiteral("payload_sha256"),
        QString::fromLatin1(sha256(changedOperation).value().toHex()));
    malformed.append(canonicalObject(changed));

    const QByteArray duplicateKey = QByteArray("{\"format\":\"duplicate\",") + encoded->mid(1);
    malformed.append(duplicateKey);

    for (const QByteArray &candidate : std::as_const(malformed)) {
        QVERIFY2(
            !decodeRuntimePackageCompilerCompileRecovery(candidate, sha256(candidate)),
            candidate.constData());
    }

    const Data::RuntimePackageCompilerProjectSnapshotEvidence &originalEvidence
        = fixture.request.projectSnapshotEvidence;
    Data::ProjectSnapshot mismatchedSnapshot = originalEvidence.snapshot();
    mismatchedSnapshot.masterBindingArtifact.artifactId = QStringLiteral("changed-binding");
    Data::RuntimePackageActivationProjectCapture mismatchedCapture{
        mismatchedSnapshot,
        fixture.serializedProject,
        originalEvidence.documentRevisionNumber(),
        originalEvidence.documentRevision(),
        originalEvidence.originalBinding(),
    };
    RuntimePackageCompilerCompileRecovery
        mismatchedBinding{fixture.request, fixture.serializedProject};
    mismatchedBinding.request.projectSnapshotEvidence
        = Data::RuntimePackageCompilerProjectSnapshotEvidence{mismatchedCapture};
    QVERIFY(mismatchedBinding.request.isValid());
    QVERIFY(!mismatchedBinding.isValid());
    QVERIFY(!encodeRuntimePackageCompilerCompileRecovery(mismatchedBinding));

    Data::ProjectSnapshot invalidEnumSnapshot = originalEvidence.snapshot();
    invalidEnumSnapshot.nodes.append({
        Data::NodeId::fromString("77777777-7777-4777-8777-777777777777"),
        invalidEnumSnapshot.id,
        static_cast<Data::ProjectNodeKind>(99),
        QStringLiteral("Invalid enum"),
    });
    Data::RuntimePackageActivationProjectCapture invalidEnumCapture{
        invalidEnumSnapshot,
        fixture.serializedProject,
        originalEvidence.documentRevisionNumber(),
        originalEvidence.documentRevision(),
        Data::runtimePackageActivationBindingToken(invalidEnumSnapshot.masterBindingArtifact),
    };
    RuntimePackageCompilerCompileRecovery invalidEnum{fixture.request, fixture.serializedProject};
    invalidEnum.request.projectSnapshotEvidence
        = Data::RuntimePackageCompilerProjectSnapshotEvidence{invalidEnumCapture};
    QVERIFY(invalidEnum.request.isValid());
    QVERIFY(invalidEnum.isValid());
    QVERIFY(!encodeRuntimePackageCompilerCompileRecovery(invalidEnum));
}

void EtherCATProjectCompilerTests::testProjectRequestBuilderProvisioningAndDeterminism()
{
    CompilerFixture fixture;
    BuilderInputEnvironment inputs(fixture);
    const auto loaded = CompilerInputProvisioningProfile::load(
        Utils::FilePath::fromString(inputs.profilePath));
    QVERIFY_RESULT(loaded);
    QVERIFY(loaded->validateCurrent());
    QCOMPARE(loaded->devices().size(), 1);

    Core::ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    BuilderProjectService project(fixture);
    BuilderConnectionProvider connection(fixture);
    BuilderDeviceRepository repository(fixture);
    BuilderAdapterProvider adapters(fixture);
    ScopedProviderKindRegistration projectRegistration(registry, &project);
    ScopedProviderKindRegistration connectionRegistration(registry, &connection);
    ScopedProviderKindRegistration repositoryRegistration(registry, &repository);
    ScopedProviderKindRegistration adapterRegistration(registry, &adapters);

    ProvisionedRuntimePackageCompilerProjectRequestBuilder builder(
        registry,
        Utils::FilePath::fromString(inputs.profilePath),
        nullptr,
        [&fixture] { return fixture.request.compileTimeNs; });
    QVERIFY2(builder.isAvailable(), qPrintable(builder.provisioningError()));
    const Core::RuntimePackageCompilerProjectRequestSeed seed{
        fixture.request.topologyEvidence.scope,
        fixture.request.operationId,
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8001-000000000001")},
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/compiler-entry-activation-1")},
        fixture.request.intentId,
        fixture.request.configurationId,
        fixture.request.buildTimestampNs,
        fixture.request.compileTimeNs,
        true,
    };
    QVERIFY(seed.isValid());
    const auto first = builder.build(seed);
    QVERIFY2(first, qPrintable(first.error()));
    const auto second = builder.build(seed);
    QVERIFY2(second, qPrintable(second.error()));
    QCOMPARE(*first, *second);
    QVERIFY(first->isValid());
    QVERIFY(first->compileRequest.isValid());
    QCOMPARE(first->compileRequest.topologyEvidence.slaves.constFirst().alias, quint16(0x002a));
    QCOMPARE(
        first->compileRequest.topologyEvidence.slaves.constFirst().moduleAssignments,
        QList<Data::DeviceModuleAssignment>({{1, 0x00000624, 0, 0}}));
    QCOMPARE(
        first->compileRequest.projectProjection.devices.constFirst().projectDeviceId,
        fixture.request.projectProjection.devices.constFirst().projectDeviceId);
    const auto encodedFirst = Core::encodeRuntimePackageCompilerCompileRequest(
        first->compileRequest);
    const auto encodedSecond = Core::encodeRuntimePackageCompilerCompileRequest(
        second->compileRequest);
    QVERIFY_RESULT(encodedFirst);
    QVERIFY_RESULT(encodedSecond);
    QCOMPARE(encodedFirst->exactBytes(), encodedSecond->exactBytes());

    QFile runtime(inputs.temporary.filePath(fixture.request.sourceArtifacts.runtimeSource.relativePath));
    QVERIFY(runtime.open(QIODevice::Append));
    QCOMPARE(runtime.write("drift", 5), 5);
    runtime.close();
    QVERIFY(!builder.build(seed));
    QVERIFY(!builder.isAvailable());
}

void EtherCATProjectCompilerTests::testDeviceParametersFailClosedBeforeCompilation()
{
    CompilerFixture fixture;
    const Data::RuntimePackageActivationProjectCapture originalCapture = projectCapture(fixture);
    Data::ProjectSnapshot parameterizedProject = originalCapture.snapshot();
    parameterizedProject.slaves[0].deviceParameters.values = {
        {QStringLiteral("motor.encoder_resolution_counts_per_revolution"),
         Data::EngineeringValue::fromUnsignedInteger(8'388'608)},
    };
    const Data::RuntimePackageActivationProjectCapture parameterizedCapture{
        parameterizedProject,
        originalCapture.serializedProject(),
        originalCapture.documentRevisionNumber(),
        originalCapture.documentRevision(),
        originalCapture.originalBinding(),
    };
    Data::RuntimePackageCompilerCompileRequest directRequest = fixture.request;
    directRequest.projectSnapshotEvidence
        = Data::RuntimePackageCompilerProjectSnapshotEvidence{parameterizedCapture};
    QVERIFY(directRequest.projectSnapshotEvidence.isValid());
    QVERIFY(!directRequest.hasValidReservationInputs());
    QVERIFY(!directRequest.isValid());
    QVERIFY(!Core::encodeRuntimePackageCompilerCompileRequest(directRequest));
    QVERIFY(!encodeRuntimePackageCompilerCompileRecovery(
        {directRequest, originalCapture.serializedProject()}));

    BuilderInputEnvironment inputs(fixture);
    Core::ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    BuilderProjectService project(fixture);
    project.setCapture(parameterizedCapture);
    BuilderConnectionProvider connection(fixture);
    BuilderDeviceRepository repository(fixture);
    BuilderAdapterProvider adapters(fixture);
    ScopedProviderKindRegistration projectRegistration(registry, &project);
    ScopedProviderKindRegistration connectionRegistration(registry, &connection);
    ScopedProviderKindRegistration repositoryRegistration(registry, &repository);
    ScopedProviderKindRegistration adapterRegistration(registry, &adapters);

    ProvisionedRuntimePackageCompilerProjectRequestBuilder builder(
        registry,
        Utils::FilePath::fromString(inputs.profilePath),
        nullptr,
        [&fixture] { return fixture.request.compileTimeNs; });
    QVERIFY2(builder.isAvailable(), qPrintable(builder.provisioningError()));
    const Core::RuntimePackageCompilerProjectRequestSeed seed{
        fixture.request.topologyEvidence.scope,
        fixture.request.operationId,
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8001-000000000001")},
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/compiler-entry-activation-1")},
        fixture.request.intentId,
        fixture.request.configurationId,
        fixture.request.buildTimestampNs,
        fixture.request.compileTimeNs,
        true,
    };
    const auto rejected = builder.build(seed);
    QVERIFY(!rejected);
    QVERIFY(rejected.error().contains(QStringLiteral("device parameter"), Qt::CaseInsensitive));
}

void EtherCATProjectCompilerTests::testProjectRequestBuilderFailsClosedOnUnprovenTopology()
{
    CompilerFixture fixture;
    BuilderInputEnvironment inputs(fixture);
    Core::ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    BuilderProjectService project(fixture);
    BuilderConnectionProvider connection(fixture);
    BuilderDeviceRepository repository(fixture);
    BuilderAdapterProvider adapters(fixture);
    ScopedProviderKindRegistration projectRegistration(registry, &project);
    ScopedProviderKindRegistration connectionRegistration(registry, &connection);
    ScopedProviderKindRegistration repositoryRegistration(registry, &repository);
    ScopedProviderKindRegistration adapterRegistration(registry, &adapters);
    quint64 currentTimeNs = fixture.request.compileTimeNs;
    ProvisionedRuntimePackageCompilerProjectRequestBuilder builder(
        registry,
        Utils::FilePath::fromString(inputs.profilePath),
        nullptr,
        [&currentTimeNs] { return currentTimeNs; });
    QVERIFY(builder.isAvailable());
    const Data::ControllerConnectionSnapshot originalConnection = connection.connectionSnapshot();

    const Core::RuntimePackageCompilerProjectRequestSeed seed{
        fixture.request.topologyEvidence.scope,
        fixture.request.operationId,
        Data::RuntimePackageCompilerOperationId{
            QStringLiteral("04204204-2001-4000-8001-000000000001")},
        Data::RuntimePackageActivationOperationId{
            QStringLiteral("operation/compiler-entry-activation-1")},
        fixture.request.intentId,
        fixture.request.configurationId,
        fixture.request.buildTimestampNs,
        fixture.request.compileTimeNs,
        true,
    };

    Data::ProjectSnapshot aliasProject = projectCapture(fixture).snapshot();
    aliasProject.slaves[0].alias = 1;
    const auto originalCapture = projectCapture(fixture);
    project.setCapture({aliasProject,
                        originalCapture.serializedProject(),
                        originalCapture.documentRevisionNumber(),
                        originalCapture.documentRevision(),
                        originalCapture.originalBinding()});
    const auto aliasRejected = builder.build(seed);
    QVERIFY(!aliasRejected);
    QVERIFY(aliasRejected.error().contains(QStringLiteral("alias"), Qt::CaseInsensitive)
            || aliasRejected.error().contains(QStringLiteral("topology"), Qt::CaseInsensitive));

    project.setCapture(originalCapture);
    connection.mutableSnapshot().topology->slaves[0].serial = 7;
    const auto topologyRejected = builder.build(seed);
    QVERIFY(!topologyRejected);
    QVERIFY(topologyRejected.error().contains(QStringLiteral("topology"), Qt::CaseInsensitive));

    connection.mutableSnapshot() = originalConnection;
    connection.mutableSnapshot().mock = true;
    const auto mockRejected = builder.build(seed);
    QVERIFY(!mockRejected);
    QVERIFY(mockRejected.error().contains(QStringLiteral("real"), Qt::CaseInsensitive));

    connection.mutableSnapshot() = originalConnection;
    const auto setProject = [&](Data::ProjectSnapshot snapshot) {
        project.setCapture({snapshot,
                            originalCapture.serializedProject(),
                            originalCapture.documentRevisionNumber(),
                            originalCapture.documentRevision(),
                            originalCapture.originalBinding()});
    };
    Data::ProjectSnapshot missingPdoProject = originalCapture.snapshot();
    missingPdoProject.slaves[0].processData.pdos[0].selected = false;
    setProject(missingPdoProject);
    const auto missingPdoRejected = builder.build(seed);
    QVERIFY(!missingPdoRejected);
    QVERIFY(missingPdoRejected.error().contains(QStringLiteral("PDO"), Qt::CaseInsensitive));

    Data::ProjectSnapshot extraPdoProject = originalCapture.snapshot();
    Data::PdoConfiguration extraPdo = extraPdoProject.slaves[0].processData.pdos.constFirst();
    extraPdo.id = Data::NodeId::create();
    ++extraPdo.index;
    extraPdo.name += QStringLiteral(".extra");
    extraPdoProject.slaves[0].processData.pdos.append(extraPdo);
    setProject(extraPdoProject);
    const auto extraPdoRejected = builder.build(seed);
    QVERIFY(!extraPdoRejected);
    QVERIFY(extraPdoRejected.error().contains(QStringLiteral("PDO"), Qt::CaseInsensitive));

    Data::ProjectSnapshot duplicatePdoProject = originalCapture.snapshot();
    Data::PdoConfiguration duplicatePdo
        = duplicatePdoProject.slaves[0].processData.pdos.constFirst();
    duplicatePdo.id = Data::NodeId::create();
    duplicatePdo.name += QStringLiteral(".duplicate");
    duplicatePdoProject.slaves[0].processData.pdos.append(duplicatePdo);
    setProject(duplicatePdoProject);
    const auto duplicatePdoRejected = builder.build(seed);
    QVERIFY(!duplicatePdoRejected);
    QVERIFY(duplicatePdoRejected.error().contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));

    Data::ProjectSnapshot wrongDirectionProject = originalCapture.snapshot();
    wrongDirectionProject.slaves[0].processData.pdos[0].direction = Data::PdoDirection::Tx;
    setProject(wrongDirectionProject);
    const auto wrongDirectionRejected = builder.build(seed);
    QVERIFY(!wrongDirectionRejected);
    QVERIFY(wrongDirectionRejected.error().contains(QStringLiteral("PDO"), Qt::CaseInsensitive));

    Data::ProjectSnapshot sdoProject = originalCapture.snapshot();
    Data::StartupParameterConfiguration startup;
    startup.id = Data::NodeId::create();
    startup.enabled = true;
    startup.order = 1;
    startup.transition = QStringLiteral("preop");
    startup.index = 0x2000;
    startup.subIndex = 1;
    startup.dataType = Data::EtherCATDataType::UnsignedInteger16;
    startup.rawValue = QByteArray::fromHex("0001");
    sdoProject.slaves[0].startup.parameters.append(startup);
    setProject(sdoProject);
    const auto sdoRejected = builder.build(seed);
    QVERIFY(!sdoRejected);
    QVERIFY(sdoRejected.error().contains(QStringLiteral("SDO"), Qt::CaseInsensitive));

    setProject(originalCapture.snapshot());
    currentTimeNs = fixture.request.topologyEvidence.expiresAtNs + 1;
    const auto replayedStaleTopologyRejected = builder.build(seed);
    QVERIFY(!replayedStaleTopologyRejected);
    QVERIFY(replayedStaleTopologyRejected.error().contains(
        QStringLiteral("build time"), Qt::CaseInsensitive));
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

void EtherCATProjectCompilerTests::testPreparationCoordinatorSuccessAndCancellation()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    ScopedCompilerProviderRegistration registration(registry, provider.get());
    QVERIFY(registration.isValid());

    QTemporaryDir journalTemporary;
    const Utils::FilePath journalRoot = Utils::FilePath::fromString(
        journalTemporary.filePath(QStringLiteral("preparations")));
    const RuntimePackageCompilerCurrentProjectCapture capture =
        [&environment](const Data::NodeId &projectId)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
        if (projectId != environment.fixture.projectId)
            return Utils::ResultError(QStringLiteral("Unexpected project capture request."));
        return projectCapture(environment.fixture);
    };
    DurableRuntimePackageCompilerPreparationCoordinator coordinator(registry, journalRoot, capture);
    QVERIFY2(
        coordinator.initializationError().isEmpty(), qPrintable(coordinator.initializationError()));
    QSignalSpy signingSpy(
        &coordinator, &Core::RuntimePackageCompilerPreparationCoordinator::detachedSigningRequested);
    QSignalSpy readySpy(
        &coordinator, &Core::RuntimePackageCompilerPreparationCoordinator::preparationReady);

    const Core::RuntimePackageCompilerPreparationStartRequest first
        = preparationRequest(environment.fixture, 1);
    QVERIFY(first.isValid());
    const auto started = coordinator.start(first);
    QVERIFY(started);
    QCOMPARE(*started, Core::RuntimePackageCompilerPreparationDisposition::Started);
    QVERIFY(awaitPreparationPhase(
        coordinator,
        first.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    QCOMPARE(signingSpy.count(), 1);
    auto firstRecord = coordinator.record(first.compileRequest.operationId);
    QVERIFY(firstRecord);
    QVERIFY(*firstRecord);
    QVERIFY((*firstRecord)->compileResult);
    const Data::RuntimePackageCompilerCanonicalJson detachedResponse
        = signResponse(environment.fixture, *(*firstRecord)->compileResult);
    const auto accepted
        = coordinator
              .submitDetachedSigningResponse(first.compileRequest.operationId, detachedResponse);
    QVERIFY(accepted);
    QCOMPARE(*accepted, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    QVERIFY(awaitPreparationPhase(
        coordinator,
        first.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::Ready,
        20000));
    QCOMPARE(readySpy.count(), 1);
    firstRecord = coordinator.record(first.compileRequest.operationId);
    QVERIFY(firstRecord);
    QVERIFY(*firstRecord);
    QVERIFY((*firstRecord)->preparation);
    QVERIFY((*firstRecord)->preparation->compilerActivationProof);
    QVERIFY(!(*firstRecord)->preparation->packageBytes.isEmpty());

    RuntimePackageCompilerPreparationJournal journal(journalRoot);
    const auto durableReady = journal.load();
    QVERIFY(durableReady);
    const RuntimePackageCompilerPreparationJournalEntry *readyEntry
        = journalEntryFor(*durableReady, first.compileRequest.operationId);
    QVERIFY(readyEntry);
    QCOMPARE(readyEntry->phase, Core::RuntimePackageCompilerPreparationPhase::Ready);
    QCOMPARE(readyEntry->detachedSigningResponse, std::optional(detachedResponse));
    QVERIFY(readyEntry->compileResultSha256);
    QVERIFY(readyEntry->finalizeResultSha256);
    QVERIFY(readyEntry->verifyResultSha256);

    const Core::RuntimePackageCompilerPreparationStartRequest second
        = preparationRequest(environment.fixture, 5);
    QVERIFY(second.isValid());
    QVERIFY(coordinator.start(second));
    QVERIFY(awaitPreparationPhase(
        coordinator,
        second.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    const auto canceled = coordinator.cancel(second.compileRequest.operationId);
    QVERIFY(canceled);
    QCOMPARE(*canceled, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    QVERIFY(awaitPreparationPhase(
        coordinator,
        second.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::Canceled));

    const Core::RuntimePackageCompilerPreparationStartRequest interrupted
        = preparationRequest(environment.fixture, 6);
    QVERIFY(interrupted.isValid());
    QVERIFY(coordinator.start(interrupted));
    QVERIFY(awaitPreparationPhase(
        coordinator,
        interrupted.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    coordinator.shutdown();
    QVERIFY(awaitPreparationPhase(
        coordinator,
        interrupted.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired));

    DurableRuntimePackageCompilerPreparationCoordinator restarted(registry, journalRoot, capture);
    QVERIFY2(restarted.initializationError().isEmpty(), qPrintable(restarted.initializationError()));
    QSignalSpy restartedSigningSpy(
        &restarted, &Core::RuntimePackageCompilerPreparationCoordinator::detachedSigningRequested);
    QSignalSpy restartedReadySpy(
        &restarted, &Core::RuntimePackageCompilerPreparationCoordinator::preparationReady);
    QCOMPARE(restartedSigningSpy.count(), 0);
    QCOMPARE(restartedReadySpy.count(), 0);
    auto interruptedRecord = restarted.record(interrupted.compileRequest.operationId);
    QVERIFY(interruptedRecord);
    QVERIFY(*interruptedRecord);
    QCOMPARE(
        (*interruptedRecord)->phase,
        Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    QCOMPARE(
        bool((*interruptedRecord)->startRequest), journal.supportsDurableRecoveryStorage());
    const Core::RuntimePackageCompilerPreparationStartRequest resumeRequest
        = (*interruptedRecord)->startRequest.value_or(interrupted);
    QCOMPARE(resumeRequest, interrupted);
    const auto resumed = restarted.resume(resumeRequest);
    QVERIFY(resumed);
    QCOMPARE(*resumed, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    QVERIFY(awaitPreparationPhase(
        restarted,
        interrupted.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    QCOMPARE(restartedSigningSpy.count(), 1);
    QVERIFY(restarted.cancel(interrupted.compileRequest.operationId));
    QVERIFY(awaitPreparationPhase(
        restarted,
        interrupted.compileRequest.operationId,
        Core::RuntimePackageCompilerPreparationPhase::Canceled));
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRestoresFromRecoverySidecar()
{
#ifndef Q_OS_UNIX
    QSKIP("Secure compiler recovery storage is Unix-only.");
#endif
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    ScopedCompilerProviderRegistration registration(registry, provider.get());
    QVERIFY(registration.isValid());

    QTemporaryDir journalTemporary;
    const Utils::FilePath journalRoot = Utils::FilePath::fromString(
        journalTemporary.filePath(QStringLiteral("preparations")));
    const Data::RuntimePackageActivationProjectCapture savedCapture
        = projectCapture(environment.fixture);
    int captureCount = 0;
    const RuntimePackageCompilerCurrentProjectCapture capture
        = [&savedCapture, &captureCount](const Data::NodeId &projectId)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
        ++captureCount;
        return projectId == savedCapture.snapshot().id
                   ? Utils::Result<Data::RuntimePackageActivationProjectCapture>{savedCapture}
                   : Utils::Result<Data::RuntimePackageActivationProjectCapture>{
                         Utils::ResultError(QStringLiteral("Unexpected project capture."))};
    };

    Core::RuntimePackageCompilerPreparationStartRequest initialRequest
        = preparationRequest(environment.fixture, 35);
    initialRequest.rollbackOnActivationFailure = false;
    QVERIFY(initialRequest.isValid());
    const Data::RuntimePackageCompilerOperationId operationId
        = initialRequest.compileRequest.operationId;
    const auto initialFingerprint
        = Core::runtimePackageCompilerPreparationStartRequestFingerprint(initialRequest);
    QVERIFY(initialFingerprint);
    Data::RuntimePackageCompilerSha256 firstCompileResultSha;
    Data::RuntimePackageCompilerSha256 firstSignRequestSha;
    QByteArray recoveryBytes;
    {
        DurableRuntimePackageCompilerPreparationCoordinator coordinator(
            registry, journalRoot, capture);
        QVERIFY(coordinator.initializationError().isEmpty());
        QVERIFY(coordinator.start(initialRequest));
        QVERIFY(awaitPreparationPhase(
            coordinator,
            operationId,
            Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
        const auto record = coordinator.record(operationId);
        QVERIFY(record && *record && (*record)->compileResult
                && (*record)->compileResult->signRequest);
        firstCompileResultSha = (*record)->compileResult->envelope.canonicalResult.sha256();
        firstSignRequestSha = (*record)->compileResult->signRequest->sha256();
        RuntimePackageCompilerPreparationJournal journal(journalRoot);
        recoveryBytes = readFile(journal.recoveryFile(operationId).path());
        QVERIFY(!recoveryBytes.isEmpty());
        coordinator.shutdown();
    }

    RuntimePackageCompilerPreparationJournal legacyJournal(journalRoot);
    QJsonObject version2
        = QJsonDocument::fromJson(readFile(legacyJournal.journalFile().path())).object();
    version2.insert(QStringLiteral("format_version"), 2);
    QJsonArray version2Records = version2.value(QStringLiteral("records")).toArray();
    for (qsizetype index = 0; index < version2Records.size(); ++index) {
        QJsonObject record = version2Records.at(index).toObject();
        record.remove(QStringLiteral("rollback_on_activation_failure"));
        version2Records[index] = record;
    }
    version2.insert(QStringLiteral("records"), version2Records);
    QByteArray version2Bytes = QJsonDocument(version2).toJson(QJsonDocument::Compact);
    version2Bytes.append('\n');
    QVERIFY(writeFile(legacyJournal.journalFile().path(), version2Bytes));
    QVERIFY(legacyJournal.load());

    environment.fixture.request = {};
    environment.fixture.serializedProject.clear();
    initialRequest = {};
    const QString callsPath = environment.root + QStringLiteral("/compiler-ledger.json.calls");
    const QByteArray compileCall = QByteArray("compile ") + operationId.value().toLatin1() + '\n';
    const QByteArray queryCall = QByteArray("query ") + operationId.value().toLatin1() + '\n';
    QCOMPARE(readFile(callsPath).count(compileCall), 1);
    QCOMPARE(readFile(callsPath).count(queryCall), 0);

    DurableRuntimePackageCompilerPreparationCoordinator restarted(registry, journalRoot, capture);
    QVERIFY2(restarted.initializationError().isEmpty(), qPrintable(restarted.initializationError()));
    QSignalSpy signingSpy(
        &restarted,
        &Core::RuntimePackageCompilerPreparationCoordinator::detachedSigningRequested);
    auto recoveredRecord = restarted.record(operationId);
    QVERIFY(recoveredRecord && *recoveredRecord);
    QCOMPARE(
        (*recoveredRecord)->phase,
        Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    QVERIFY((*recoveredRecord)->startRequest);
    const Core::RuntimePackageCompilerPreparationStartRequest recoveredRequest
        = *(*recoveredRecord)->startRequest;
    QVERIFY(!recoveredRequest.rollbackOnActivationFailure);
    const auto recoveredFingerprint
        = Core::runtimePackageCompilerPreparationStartRequestFingerprint(recoveredRequest);
    QVERIFY(recoveredFingerprint);
    QCOMPARE(*recoveredFingerprint, *initialFingerprint);
    const auto resumed = restarted.resume(recoveredRequest);
    QVERIFY(resumed);
    QCOMPARE(*resumed, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    QVERIFY(awaitPreparationPhase(
        restarted,
        operationId,
        Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature));
    QCOMPARE(signingSpy.count(), 1);
    recoveredRecord = restarted.record(operationId);
    QVERIFY(recoveredRecord && *recoveredRecord && (*recoveredRecord)->compileResult
            && (*recoveredRecord)->compileResult->signRequest);
    QCOMPARE(
        (*recoveredRecord)->compileResult->envelope.canonicalResult.sha256(),
        firstCompileResultSha);
    QCOMPARE((*recoveredRecord)->compileResult->signRequest->sha256(), firstSignRequestSha);
    QCOMPARE(readFile(callsPath).count(queryCall), 1);
    QCOMPARE(readFile(callsPath).count(compileCall), 2);
    RuntimePackageCompilerPreparationJournal journal(journalRoot);
    QCOMPARE(readFile(journal.recoveryFile(operationId).path()), recoveryBytes);
    const auto durable = journal.load();
    QVERIFY(durable);
    const RuntimePackageCompilerPreparationJournalEntry *durableEntry
        = journalEntryFor(*durable, operationId);
    QVERIFY(durableEntry && durableEntry->rollbackOnActivationFailure);
    QVERIFY(!*durableEntry->rollbackOnActivationFailure);
    QCOMPARE(durableEntry->startRequestFingerprint, *initialFingerprint);
    QVERIFY(captureCount >= 2);
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRecoversBeforeProviderReservation()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    ScopedCompilerProviderRegistration registration(registry, provider.get());
    QVERIFY(registration.isValid());

    QTemporaryDir temporary;
    const Utils::FilePath journalRoot = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(journalRoot);
    auto state = journal.initialize();
    QVERIFY(state);
    Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(environment.fixture, 36);
    const Core::RuntimePackageCompilerPreparationStartRequest exactOriginalRequest = request;
    const RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
        request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    // Model the non-Unix crash window after the coordinator reservation but
    // before the provider has reserved the operation in its own store. This is
    // deliberately exercised on every host so the fallback remains testable.
    state = journal.commit(reserved, state->sequence, std::nullopt);
    QVERIFY(state);
    const Data::RuntimePackageCompilerOperationId operationId
        = request.compileRequest.operationId;
    const Data::RuntimePackageActivationProjectCapture savedCapture
        = projectCapture(environment.fixture);
    request = {};
    environment.fixture.request = {};
    environment.fixture.serializedProject.clear();

    const QString callsPath = environment.root + QStringLiteral("/compiler-ledger.json.calls");
    const QByteArray compileCall = QByteArray("compile ") + operationId.value().toLatin1() + '\n';
    const QByteArray queryCall = QByteArray("query ") + operationId.value().toLatin1() + '\n';
    QVERIFY(readFile(callsPath).isEmpty());
    DurableRuntimePackageCompilerPreparationCoordinator coordinator(
        registry,
        journalRoot,
        [&savedCapture](const Data::NodeId &projectId)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
            return projectId == savedCapture.snapshot().id
                       ? Utils::Result<Data::RuntimePackageActivationProjectCapture>{savedCapture}
                       : Utils::Result<Data::RuntimePackageActivationProjectCapture>{
                             Utils::ResultError(QStringLiteral("Unexpected project capture."))};
        });
    QVERIFY(coordinator.initializationError().isEmpty());
    auto record = coordinator.record(operationId);
    QVERIFY(record && *record);
    QVERIFY(!(*record)->startRequest);
    QVERIFY(readFile(callsPath).isEmpty());
    const auto resumed = coordinator.resume(exactOriginalRequest);
    QVERIFY(resumed);
    QCOMPARE(*resumed, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    const bool reachedSigning = awaitPreparationPhase(
        coordinator,
        operationId,
        Core::RuntimePackageCompilerPreparationPhase::AwaitingDetachedSignature);
    const auto afterResume = coordinator.record(operationId);
    const QString resumeDiagnostic
        = afterResume && *afterResume
              ? QStringLiteral("phase=%1 detail=%2 calls=%3")
                    .arg(int((*afterResume)->phase))
                    .arg((*afterResume)->detail, QString::fromLatin1(readFile(callsPath)))
              : QStringLiteral("record unavailable");
    QVERIFY2(reachedSigning, qPrintable(resumeDiagnostic));
    // The secure store distinguishes a missing operation directory from every
    // unsafe/corrupt query error. The pinned backend must then confirm the
    // exact unknown OperationId before the coordinator may replay Compile.
    QCOMPARE(readFile(callsPath).count(queryCall), 1);
    QCOMPARE(readFile(callsPath).count(compileCall), 1);
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRejectsInvalidRecovery()
{
#ifndef Q_OS_UNIX
    QSKIP("Secure compiler recovery storage is Unix-only.");
#endif
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    const auto runCase = [registry](bool removeRecovery, bool changeCurrentProject) {
        TestEnvironment environment;
        auto provider = environment.provider();
        if (!provider->isAvailable())
            return false;
        ScopedCompilerProviderRegistration registration(registry, provider.get());
        if (!registration.isValid())
            return false;
        QTemporaryDir temporary;
        const Utils::FilePath journalRoot = Utils::FilePath::fromString(
            temporary.filePath(QStringLiteral("preparations")));
        RuntimePackageCompilerPreparationJournal journal(journalRoot);
        auto state = journal.initialize();
        if (!state)
            return false;
        const Core::RuntimePackageCompilerPreparationStartRequest request
            = preparationRequest(environment.fixture, removeRecovery ? 37 : 38);
        RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
            request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
        const auto recovery = encodeRuntimePackageCompilerCompileRecovery(
            {request.compileRequest, environment.fixture.serializedProject});
        if (!recovery)
            return false;
        state = journal.reserveWithRecovery(reserved, state->sequence, *recovery);
        if (!state)
            return false;
        if (removeRecovery
            && !QFile::remove(journal.recoveryFile(request.compileRequest.operationId).path())) {
            return false;
        }
        Data::RuntimePackageActivationProjectCapture current = projectCapture(environment.fixture);
        if (changeCurrentProject) {
            current = Data::RuntimePackageActivationProjectCapture{
                current.snapshot(),
                current.serializedProject() + QByteArray("changed"),
                current.documentRevisionNumber() + 1,
                Data::RuntimePackageActivationDocumentRevisionToken{"changed-revision"},
                current.originalBinding(),
            };
            if (!current.isValid())
                return false;
        }
        const QString callsPath
            = environment.root + QStringLiteral("/compiler-ledger.json.calls");
        const QByteArray callsBefore = readFile(callsPath);
        DurableRuntimePackageCompilerPreparationCoordinator coordinator(
            registry,
            journalRoot,
            [current](const Data::NodeId &projectId)
            -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
                return projectId == current.snapshot().id
                           ? Utils::Result<Data::RuntimePackageActivationProjectCapture>{current}
                           : Utils::Result<Data::RuntimePackageActivationProjectCapture>{
                                 Utils::ResultError(QStringLiteral("Unexpected project capture."))};
            });
        if (!coordinator.initializationError().isEmpty())
            return false;
        const auto record = coordinator.record(request.compileRequest.operationId);
        if (!record || !*record
            || (*record)->phase
                   != Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired) {
            return false;
        }
        if (removeRecovery && (*record)->startRequest)
            return false;
        if (!removeRecovery && !(*record)->startRequest)
            return false;
        const Core::RuntimePackageCompilerPreparationStartRequest resumeRequest
            = (*record)->startRequest ? *(*record)->startRequest : request;
        if (coordinator.resume(resumeRequest))
            return false;
        return readFile(callsPath) == callsBefore;
    };

    QVERIFY(runCase(true, false));
    QVERIFY(runCase(false, true));

    TestEnvironment canceledEnvironment;
    auto canceledProvider = canceledEnvironment.provider();
    QVERIFY(canceledProvider->isAvailable());
    ScopedCompilerProviderRegistration canceledRegistration(registry, canceledProvider.get());
    QVERIFY(canceledRegistration.isValid());
    QTemporaryDir canceledTemporary;
    const Utils::FilePath canceledRoot = Utils::FilePath::fromString(
        canceledTemporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal canceledJournal(canceledRoot);
    auto canceledState = canceledJournal.initialize();
    QVERIFY(canceledState);
    const Core::RuntimePackageCompilerPreparationStartRequest canceledRequest
        = preparationRequest(canceledEnvironment.fixture, 39);
    RuntimePackageCompilerPreparationJournalEntry canceledReserved = journalEntry(
        canceledRequest, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    const auto canceledRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {canceledRequest.compileRequest, canceledEnvironment.fixture.serializedProject});
    QVERIFY(canceledRecovery);
    canceledState = canceledJournal.reserveWithRecovery(
        canceledReserved, canceledState->sequence, *canceledRecovery);
    QVERIFY(canceledState);
    const RuntimePackageCompilerPreparationJournalEntry *persistedReserved
        = journalEntryFor(*canceledState, canceledRequest.compileRequest.operationId);
    QVERIFY(persistedReserved);
    RuntimePackageCompilerPreparationJournalEntry cancelRequested = *persistedReserved;
    cancelRequested.revision = 2;
    cancelRequested.phase = Core::RuntimePackageCompilerPreparationPhase::CancelRequested;
    canceledState = canceledJournal.commit(
        cancelRequested, canceledState->sequence, *persistedReserved);
    QVERIFY(canceledState);
    const Data::RuntimePackageActivationProjectCapture canceledCapture
        = projectCapture(canceledEnvironment.fixture);
    const QString canceledCalls
        = canceledEnvironment.root + QStringLiteral("/compiler-ledger.json.calls");
    const QByteArray canceledCallsBefore = readFile(canceledCalls);
    DurableRuntimePackageCompilerPreparationCoordinator canceledCoordinator(
        registry,
        canceledRoot,
        [canceledCapture](const Data::NodeId &)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
            return canceledCapture;
        });
    QVERIFY(canceledCoordinator.initializationError().isEmpty());
    const auto canceledRecord
        = canceledCoordinator.record(canceledRequest.compileRequest.operationId);
    QVERIFY(canceledRecord && *canceledRecord);
    QCOMPARE(
        (*canceledRecord)->phase,
        Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    QVERIFY(!(*canceledRecord)->startRequest);
    QVERIFY(!canceledCoordinator.resume(canceledRequest));
    QCOMPARE(readFile(canceledCalls), canceledCallsBefore);

}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRejectsCorruptProviderQuery()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    TestEnvironment environment;
    auto provider = environment.provider();
    QVERIFY(provider->isAvailable());
    ScopedCompilerProviderRegistration registration(registry, provider.get());
    QVERIFY(registration.isValid());

    QTemporaryDir temporary;
    const Utils::FilePath journalRoot = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(journalRoot);
    auto state = journal.initialize();
    QVERIFY(state);
    const Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(environment.fixture, 40);
    state = reservePreparationForRestart(
        journal, *state, request, environment.fixture.serializedProject);
    QVERIFY(state);

    const QString operationRoot = provider->operationRoot(request.compileRequest.operationId).path();
    QVERIFY(QDir().mkpath(operationRoot));
    QVERIFY(QFile::setPermissions(
        operationRoot,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QVERIFY(writeFile(
        operationRoot + QStringLiteral("/reservation.json"), QByteArray("corrupt\n")));
    const QString callsPath = environment.root + QStringLiteral("/compiler-ledger.json.calls");
    const QByteArray callsBefore = readFile(callsPath);
    const Data::RuntimePackageActivationProjectCapture capture
        = projectCapture(environment.fixture);
    DurableRuntimePackageCompilerPreparationCoordinator coordinator(
        registry,
        journalRoot,
        [capture](const Data::NodeId &)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
            return capture;
        });
    QVERIFY(coordinator.initializationError().isEmpty());
    auto record = coordinator.record(request.compileRequest.operationId);
    QVERIFY2(
        record && *record,
        qPrintable(
            record && *record ? QStringLiteral("Recovery unavailable: %1").arg((*record)->detail)
                              : QStringLiteral("Recovery record unavailable.")));
    QCOMPARE(bool((*record)->startRequest), journal.supportsDurableRecoveryStorage());
    const auto resumed = coordinator.resume((*record)->startRequest.value_or(request));
    QVERIFY(resumed);
    QCOMPARE(*resumed, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    record = coordinator.record(request.compileRequest.operationId);
    QVERIFY2(
        record,
        qPrintable(QStringLiteral("Record lookup failed: %1 / %2")
                       .arg(record.error(), coordinator.initializationError())));
    QVERIFY(*record);
    QCOMPARE(
        (*record)->phase,
        Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    QCOMPARE(readFile(callsPath), callsBefore);
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRejectsInvalidCompilerLedger()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    for (const bool removeLedger : {false, true}) {
        TestEnvironment environment;
        auto provider = environment.provider();
        QVERIFY(provider->isAvailable());
        ScopedCompilerProviderRegistration registration(registry, provider.get());
        QVERIFY(registration.isValid());

        const Utils::FilePath journalRoot = Utils::FilePath::fromString(
            environment.temporary.filePath(
                removeLedger ? QStringLiteral("missing-ledger-preparations")
                             : QStringLiteral("corrupt-ledger-preparations")));
        RuntimePackageCompilerPreparationJournal journal(journalRoot);
        auto state = journal.initialize();
        QVERIFY(state);
        const Core::RuntimePackageCompilerPreparationStartRequest request
            = preparationRequest(environment.fixture, removeLedger ? 45 : 46);
        state = reservePreparationForRestart(
            journal, *state, request, environment.fixture.serializedProject);
        QVERIFY(state);

        const QString ledgerPath = environment.root + QStringLiteral("/compiler-ledger.json");
        if (removeLedger)
            QVERIFY(QFile::remove(ledgerPath));
        else
            QVERIFY(writeFile(ledgerPath, QByteArray("corrupt\n")));
        const QString callsPath = ledgerPath + QStringLiteral(".calls");
        const QByteArray queryCall
            = QByteArray("query ") + request.compileRequest.operationId.value().toLatin1() + '\n';
        const QByteArray compileCall
            = QByteArray("compile ") + request.compileRequest.operationId.value().toLatin1() + '\n';
        const Data::RuntimePackageActivationProjectCapture capture
            = projectCapture(environment.fixture);
        DurableRuntimePackageCompilerPreparationCoordinator coordinator(
            registry,
            journalRoot,
            [capture](const Data::NodeId &)
            -> Utils::Result<Data::RuntimePackageActivationProjectCapture> { return capture; });
        QVERIFY(coordinator.initializationError().isEmpty());
        const auto recovered = coordinator.record(request.compileRequest.operationId);
        QVERIFY(recovered && *recovered);
        QCOMPARE(bool((*recovered)->startRequest), journal.supportsDurableRecoveryStorage());
        const auto resumed = coordinator.resume((*recovered)->startRequest.value_or(request));
        QVERIFY(resumed);
        QCOMPARE(*resumed, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
        QVERIFY(awaitPreparationPhase(
            coordinator,
            request.compileRequest.operationId,
            Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired));
        QCOMPARE(readFile(callsPath).count(queryCall), 1);
        QCOMPARE(readFile(callsPath).count(compileCall), 0);
    }
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRejectsMalformedUnknownProvider()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    CompilerFixture fixture;
    MalformedUnknownCompilerProvider provider;
    ScopedCompilerProviderRegistration registration(registry, &provider);
    QVERIFY(registration.isValid());

    QTemporaryDir temporary;
    const Utils::FilePath journalRoot = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(journalRoot);
    auto state = journal.initialize();
    QVERIFY(state);
    const Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(fixture, 47);
    state = reservePreparationForRestart(journal, *state, request, fixture.serializedProject);
    QVERIFY(state);

    const Data::RuntimePackageActivationProjectCapture capture = projectCapture(fixture);
    DurableRuntimePackageCompilerPreparationCoordinator coordinator(
        registry,
        journalRoot,
        [capture](const Data::NodeId &)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> { return capture; });
    QVERIFY(coordinator.initializationError().isEmpty());
    const auto recovered = coordinator.record(request.compileRequest.operationId);
    QVERIFY(recovered && *recovered);
    QCOMPARE(bool((*recovered)->startRequest), journal.supportsDurableRecoveryStorage());
    const auto resumed = coordinator.resume((*recovered)->startRequest.value_or(request));
    QVERIFY(resumed);
    QCOMPARE(*resumed, Core::RuntimePackageCompilerPreparationDisposition::Accepted);
    const auto afterResume = coordinator.record(request.compileRequest.operationId);
    QVERIFY(afterResume && *afterResume);
    QCOMPARE(
        (*afterResume)->phase,
        Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    QCOMPARE(provider.queryCallCount, 1);
    QCOMPARE(provider.compileCallCount, 0);
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRejectsConcurrentInvalidation()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    {
        TestEnvironment environment;
        auto provider = environment.provider();
        QVERIFY(provider->isAvailable());
        ScopedCompilerProviderRegistration registration(registry, provider.get());
        QVERIFY(registration.isValid());
        QTemporaryDir journalTemporary;
        const Utils::FilePath journalRoot = Utils::FilePath::fromString(
            journalTemporary.filePath(QStringLiteral("preparations")));
        DurableRuntimePackageCompilerPreparationCoordinator coordinator(
            registry,
            journalRoot,
            [&environment](const Data::NodeId &)
            -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
                return projectCapture(environment.fixture);
            });
        QVERIFY(coordinator.initializationError().isEmpty());
        bool profileInvalidated = false;
        bool profileWriteSucceeded = false;
        connect(
            &coordinator,
            &Core::RuntimePackageCompilerPreparationCoordinator::recordChanged,
            &coordinator,
            [&](const Core::RuntimePackageCompilerPreparationRecord &record) {
                if (profileInvalidated
                    || record.phase
                           != Core::RuntimePackageCompilerPreparationPhase::Compiling) {
                    return;
                }
                profileInvalidated = true;
                profileWriteSucceeded = writeFile(environment.provisioning, QByteArray("{}\n"));
            },
            Qt::DirectConnection);
        const Core::RuntimePackageCompilerPreparationStartRequest request
            = preparationRequest(environment.fixture, 21);
        QVERIFY(coordinator.start(request));
        QVERIFY(profileInvalidated);
        QVERIFY(profileWriteSucceeded);
        QVERIFY(awaitPreparationPhase(
            coordinator,
            request.compileRequest.operationId,
            Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired));
        const auto record = coordinator.record(request.compileRequest.operationId);
        QVERIFY(record);
        QVERIFY(*record);
        QCOMPARE(
            (*record)->phase,
            Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired);
    }

    {
        TestEnvironment environment;
        auto provider = environment.provider();
        QVERIFY(provider->isAvailable());
        ScopedCompilerProviderRegistration registration(registry, provider.get());
        QVERIFY(registration.isValid());
        QTemporaryDir journalTemporary;
        const Utils::FilePath journalRoot = Utils::FilePath::fromString(
            journalTemporary.filePath(QStringLiteral("preparations")));
        DurableRuntimePackageCompilerPreparationCoordinator coordinator(
            registry,
            journalRoot,
            [&environment](const Data::NodeId &)
            -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
                return projectCapture(environment.fixture);
            });
        QVERIFY(coordinator.initializationError().isEmpty());
        bool providerRemoved = false;
        connect(
            &coordinator,
            &Core::RuntimePackageCompilerPreparationCoordinator::recordChanged,
            &coordinator,
            [&](const Core::RuntimePackageCompilerPreparationRecord &record) {
                if (providerRemoved
                    || record.phase != Core::RuntimePackageCompilerPreparationPhase::Reserved) {
                    return;
                }
                providerRemoved = true;
                registration.unregisterProvider();
            },
            Qt::DirectConnection);
        const Core::RuntimePackageCompilerPreparationStartRequest request
            = preparationRequest(environment.fixture, 22);
        QVERIFY(coordinator.start(request));
        QVERIFY(providerRemoved);
        QVERIFY(awaitPreparationPhase(
            coordinator,
            request.compileRequest.operationId,
            Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired));
    }
}

void EtherCATProjectCompilerTests::testPreparationCoordinatorRestartAndTerminalTombstones()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    CompilerFixture fixture;
    QTemporaryDir journalTemporary;
    const Utils::FilePath journalRoot = Utils::FilePath::fromString(
        journalTemporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(journalRoot);
    auto state = journal.initialize();
    QVERIFY(state);

    const QList<Core::RuntimePackageCompilerPreparationStartRequest> requests{
        preparationRequest(fixture, 11),
        preparationRequest(fixture, 12),
        preparationRequest(fixture, 13),
    };
    QList<RuntimePackageCompilerPreparationJournalEntry> entries{
        journalEntry(requests.at(0), Core::RuntimePackageCompilerPreparationPhase::Ready, 1),
        journalEntry(
            requests.at(1),
            Core::RuntimePackageCompilerPreparationPhase::Canceled,
            1,
            QStringLiteral("Canceled before restart.")),
        journalEntry(
            requests.at(2),
            Core::RuntimePackageCompilerPreparationPhase::Failed,
            1,
            QStringLiteral("Failed before restart.")),
    };
    entries[0].compileResultSha256 = sha256("terminal-compile");
    entries[0].signRequestSha256 = sha256("terminal-sign-request");
    entries[0].detachedSigningResponse = canonical(QByteArray("{}\n"));
    entries[0].finalizeResultSha256 = sha256("terminal-finalize");
    entries[0].packageSha256 = sha256("terminal-package");
    entries[0].verifyResultSha256 = sha256("terminal-verify");
    for (const RuntimePackageCompilerPreparationJournalEntry &entry : std::as_const(entries)) {
        QVERIFY(entry.isValid());
        state = journal.commit(entry, state->sequence, std::nullopt);
        QVERIFY(state);
    }

    const RuntimePackageCompilerCurrentProjectCapture capture =
        [&fixture](const Data::NodeId &projectId)
        -> Utils::Result<Data::RuntimePackageActivationProjectCapture> {
        if (projectId != fixture.projectId)
            return Utils::ResultError(QStringLiteral("Unexpected project capture request."));
        return projectCapture(fixture);
    };
    DurableRuntimePackageCompilerPreparationCoordinator coordinator(registry, journalRoot, capture);
    QVERIFY2(
        coordinator.initializationError().isEmpty(), qPrintable(coordinator.initializationError()));
    QSignalSpy
        changedSpy(&coordinator, &Core::RuntimePackageCompilerPreparationCoordinator::recordChanged);
    QSignalSpy signingSpy(
        &coordinator, &Core::RuntimePackageCompilerPreparationCoordinator::detachedSigningRequested);
    QSignalSpy readySpy(
        &coordinator, &Core::RuntimePackageCompilerPreparationCoordinator::preparationReady);

    const QList<Core::RuntimePackageCompilerPreparationPhase> expectedPhases{
        Core::RuntimePackageCompilerPreparationPhase::Ready,
        Core::RuntimePackageCompilerPreparationPhase::Canceled,
        Core::RuntimePackageCompilerPreparationPhase::Failed,
    };
    for (qsizetype index = 0; index < requests.size(); ++index) {
        const auto restored = coordinator.record(requests.at(index).compileRequest.operationId);
        QVERIFY(restored);
        QVERIFY(*restored);
        QCOMPARE((*restored)->phase, expectedPhases.at(index));
        QVERIFY((*restored)->terminalSummary);
        QVERIFY(!(*restored)->startRequest);
        const auto replayedStart = coordinator.start(requests.at(index));
        QVERIFY(replayedStart);
        QCOMPARE(*replayedStart, Core::RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);
        const auto replayedResume = coordinator.resume(requests.at(index));
        QVERIFY(replayedResume);
        QCOMPARE(*replayedResume, Core::RuntimePackageCompilerPreparationDisposition::AlreadyTerminal);
    }
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(signingSpy.count(), 0);
    QCOMPARE(readySpy.count(), 0);
    const auto snapshot = coordinator.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->records.size(), 3);
    QCOMPARE(snapshot->sequence, quint64(3));
}

void EtherCATProjectCompilerTests::testPreparationJournalCasRequiresExactPredecessor()
{
    CompilerFixture fixture;
    QTemporaryDir temporary;
    const Utils::FilePath root = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(root);
    auto state = journal.initialize();
    QVERIFY(state);

    const Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(fixture, 31);
    RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
        request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    state = journal.commit(reserved, state->sequence, std::nullopt);
    QVERIFY(state);

    RuntimePackageCompilerPreparationJournalEntry compiling = reserved;
    compiling.revision = 2;
    compiling.phase = Core::RuntimePackageCompilerPreparationPhase::Compiling;
    QVERIFY(compiling.isValid());
    RuntimePackageCompilerPreparationJournalEntry forgedPredecessor = reserved;
    forgedPredecessor.phase = Core::RuntimePackageCompilerPreparationPhase::Compiling;
    QVERIFY(forgedPredecessor.isValid());
    QVERIFY(!journal.commit(compiling, state->sequence, forgedPredecessor));

    state = journal.commit(compiling, state->sequence, reserved);
    QVERIFY(state);
    const quint64 staleSequence = state->sequence;
    RuntimePackageCompilerPreparationJournalEntry reconciliation = compiling;
    reconciliation.revision = 3;
    reconciliation.phase
        = Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired;
    reconciliation.detail = QStringLiteral("Reconcile exact durable evidence.");
    QVERIFY(reconciliation.isValid());
    const auto committed = journal.commit(reconciliation, staleSequence, compiling);
    QVERIFY(committed);

    RuntimePackageCompilerPreparationJournalEntry competing = compiling;
    competing.revision = 3;
    competing.phase = Core::RuntimePackageCompilerPreparationPhase::Failed;
    competing.detail = QStringLiteral("Competing terminal update.");
    QVERIFY(competing.isValid());
    QVERIFY(!journal.commit(competing, staleSequence, compiling));

    QJsonObject legacyRoot
        = QJsonDocument::fromJson(readFile(journal.journalFile().path())).object();
    legacyRoot.insert(QStringLiteral("format_version"), 1);
    QJsonArray legacyRecords = legacyRoot.value(QStringLiteral("records")).toArray();
    for (qsizetype index = 0; index < legacyRecords.size(); ++index) {
        QJsonObject record = legacyRecords.at(index).toObject();
        record.remove(QStringLiteral("compile_recovery_bytes"));
        record.remove(QStringLiteral("compile_recovery_sha256"));
        record.remove(QStringLiteral("rollback_on_activation_failure"));
        legacyRecords[index] = record;
    }
    legacyRoot.insert(QStringLiteral("records"), legacyRecords);
    QByteArray legacyBytes = QJsonDocument(legacyRoot).toJson(QJsonDocument::Compact);
    legacyBytes.append('\n');
    QVERIFY(writeFile(journal.journalFile().path(), legacyBytes));
    const auto legacyLoaded = journal.load();
    QVERIFY(legacyLoaded);
    RuntimePackageCompilerPreparationJournalState expectedLegacy = *committed;
    expectedLegacy.entries.first().rollbackOnActivationFailure.reset();
    QCOMPARE(*legacyLoaded, expectedLegacy);

    const RuntimePackageCompilerPreparationJournalEntry *legacyEntry
        = journalEntryFor(*legacyLoaded, request.compileRequest.operationId);
    QVERIFY(legacyEntry);
    QVERIFY(!legacyEntry->rollbackOnActivationFailure);
    RuntimePackageCompilerPreparationJournalEntry migrated = *legacyEntry;
    migrated.revision = 4;
    migrated.phase = Core::RuntimePackageCompilerPreparationPhase::Failed;
    migrated.detail = QStringLiteral("Migrated terminal update.");
    const auto migratedState
        = journal.commit(migrated, legacyLoaded->sequence, *legacyEntry);
    QVERIFY(migratedState);
    const QJsonObject migratedRoot
        = QJsonDocument::fromJson(readFile(journal.journalFile().path())).object();
    QCOMPARE(migratedRoot.value(QStringLiteral("format_version")).toInt(), 3);
    const QJsonObject migratedRecord
        = migratedRoot.value(QStringLiteral("records")).toArray().first().toObject();
    QVERIFY(migratedRecord.contains(QStringLiteral("compile_recovery_bytes")));
    QVERIFY(migratedRecord.contains(QStringLiteral("compile_recovery_sha256")));
    QVERIFY(migratedRecord.contains(QStringLiteral("rollback_on_activation_failure")));
    QVERIFY(migratedRecord.value(QStringLiteral("rollback_on_activation_failure")).isNull());
}

void EtherCATProjectCompilerTests::testPreparationJournalPortableRecoveryRoundTrip()
{
    CompilerFixture fixture;
    QTemporaryDir temporary;
    const Utils::FilePath root = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(root);
    auto state = journal.initialize();
    QVERIFY(state);

    const Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(fixture, 50);
    const RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
        request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    const auto recovery = encodeRuntimePackageCompilerCompileRecovery(
        {request.compileRequest, fixture.serializedProject});
    QVERIFY(recovery);
#ifdef Q_OS_UNIX
    QVERIFY(journal.supportsDurableRecoveryStorage());
#else
    QVERIFY(!journal.supportsDurableRecoveryStorage());
    QVERIFY(!journal.reserveWithRecovery(reserved, state->sequence, *recovery));
    QVERIFY(!QFileInfo::exists(journal.recoveryFile(request.compileRequest.operationId).path()));
    return;
#endif
    const quint64 initialSequence = state->sequence;
    state = journal.reserveWithRecovery(reserved, initialSequence, *recovery);
    if (!state)
        QFAIL(qPrintable(state.error()));

    const RuntimePackageCompilerPreparationJournalEntry *persisted
        = journalEntryFor(*state, request.compileRequest.operationId);
    QVERIFY(persisted && persisted->compileRecovery);
    const auto loaded
        = journal.loadRecovery(request.compileRequest.operationId, state->sequence, *persisted);
    if (!loaded)
        QFAIL(qPrintable(loaded.error()));
    QCOMPARE(*loaded, *recovery);

    const auto replayed = journal.reserveWithRecovery(reserved, initialSequence, *recovery);
    QVERIFY(replayed);
    QCOMPARE(replayed->sequence, state->sequence);

    const QString recoveryPath = journal.recoveryFile(request.compileRequest.operationId).path();
    QByteArray corrupted = *recovery;
    corrupted[corrupted.size() / 2] ^= 0x01;
    QVERIFY(writeFile(recoveryPath, corrupted));
    QVERIFY(!journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *persisted));
    QVERIFY(writeFile(recoveryPath, *recovery));
    QVERIFY(journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *persisted));

    RuntimePackageCompilerPreparationJournal nonCanonical(
        Utils::FilePath::fromString(temporary.filePath(QStringLiteral("nested/../escape"))));
    QVERIFY(!nonCanonical.initialize());
}

void EtherCATProjectCompilerTests::testPreparationJournalPersistsImmutableRecovery()
{
#ifndef Q_OS_UNIX
    QSKIP("Secure compiler recovery storage is Unix-only.");
#else
    CompilerFixture fixture;
    QTemporaryDir temporary;
    const Utils::FilePath root = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(root);
    auto state = journal.initialize();
    QVERIFY(state);

    const Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(fixture, 32);
    RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
        request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    const auto recovery = encodeRuntimePackageCompilerCompileRecovery(
        {request.compileRequest, fixture.serializedProject});
    QVERIFY(recovery);
    const quint64 preReservationSequence = state->sequence;
    state = journal.reserveWithRecovery(reserved, state->sequence, *recovery);
    if (!state)
        QFAIL(qPrintable(state.error()));
    const RuntimePackageCompilerPreparationJournalEntry *persisted
        = journalEntryFor(*state, request.compileRequest.operationId);
    QVERIFY(persisted);
    QVERIFY(persisted->compileRecovery);
    QCOMPARE(persisted->compileRecovery->sha256, sha256(*recovery));
    QCOMPARE(persisted->compileRecovery->exactByteCount, quint64(recovery->size()));

    const auto loaded
        = journal.loadRecovery(request.compileRequest.operationId, state->sequence, *persisted);
    if (!loaded)
        QFAIL(qPrintable(loaded.error()));
    QCOMPARE(*loaded, *recovery);
    const auto decoded
        = decodeRuntimePackageCompilerCompileRecovery(*loaded, persisted->compileRecovery->sha256);
    QVERIFY(decoded);
    QCOMPARE(decoded->request, request.compileRequest);
    QCOMPARE(decoded->serializedProject, fixture.serializedProject);

    const QString recoveryFile = journal.recoveryFile(request.compileRequest.operationId).path();
    struct stat before = {};
    QVERIFY(::stat(QFile::encodeName(recoveryFile).constData(), &before) == 0);
    QCOMPARE(before.st_uid, ::geteuid());
    QCOMPARE(before.st_nlink, nlink_t(1));
    QCOMPARE(before.st_mode & 0777, mode_t(0600));
    struct stat directory = {};
    QVERIFY(::stat(
                QFile::encodeName(QFileInfo(recoveryFile).absolutePath()).constData(), &directory)
            == 0);
    QCOMPARE(directory.st_mode & 0777, mode_t(0700));

    const quint64 stableSequence = state->sequence;
    const auto replayed
        = journal.reserveWithRecovery(reserved, preReservationSequence, *recovery);
    if (!replayed)
        QFAIL(qPrintable(replayed.error()));
    QCOMPARE(replayed->sequence, stableSequence);
    struct stat afterReplay = {};
    QVERIFY(::stat(QFile::encodeName(recoveryFile).constData(), &afterReplay) == 0);
    QCOMPARE(afterReplay.st_dev, before.st_dev);
    QCOMPARE(afterReplay.st_ino, before.st_ino);
#ifdef Q_OS_DARWIN
    QCOMPARE(afterReplay.st_mtimespec.tv_sec, before.st_mtimespec.tv_sec);
    QCOMPARE(afterReplay.st_mtimespec.tv_nsec, before.st_mtimespec.tv_nsec);
#else
    QCOMPARE(afterReplay.st_mtim.tv_sec, before.st_mtim.tv_sec);
    QCOMPARE(afterReplay.st_mtim.tv_nsec, before.st_mtim.tv_nsec);
#endif

    Core::RuntimePackageCompilerPreparationStartRequest conflictingRequest = request;
    conflictingRequest.compileRequest.intentId.append(QStringLiteral("-conflict"));
    QVERIFY(conflictingRequest.isValid());
    RuntimePackageCompilerPreparationJournalEntry conflictingEntry = journalEntry(
        conflictingRequest, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    const auto conflictingRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {conflictingRequest.compileRequest, fixture.serializedProject});
    QVERIFY(conflictingRecovery);
    QVERIFY(!journal.reserveWithRecovery(
        conflictingEntry, stableSequence, *conflictingRecovery));
    QCOMPARE(readFile(recoveryFile), *recovery);

    RuntimePackageCompilerPreparationJournalEntry compiling = *persisted;
    compiling.revision = 2;
    compiling.phase = Core::RuntimePackageCompilerPreparationPhase::Compiling;
    state = journal.commit(compiling, state->sequence, *persisted);
    QVERIFY(state);
    const RuntimePackageCompilerPreparationJournalEntry *compiledEntry
        = journalEntryFor(*state, request.compileRequest.operationId);
    QVERIFY(compiledEntry);
    QVERIFY(compiledEntry->compileRecovery);
    QVERIFY(journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *compiledEntry));
    QVERIFY(!journal.loadRecovery(
        request.compileRequest.operationId, state->sequence - 1, *compiledEntry));

    RuntimePackageCompilerPreparationJournalEntry injectedEvidence = *compiledEntry;
    injectedEvidence.revision++;
    injectedEvidence.phase
        = Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired;
    injectedEvidence.compileResultSha256 = sha256("injected compile result");
    injectedEvidence.signRequestSha256 = sha256("injected sign request");
    injectedEvidence.detachedSigningResponse = canonical(QByteArray("{}\n"));
    injectedEvidence.finalizeResultSha256 = sha256("injected finalize result");
    injectedEvidence.packageSha256 = sha256("injected package");
    injectedEvidence.verifyResultSha256 = sha256("injected verify result");
    injectedEvidence.detail = QStringLiteral("Reject injected recovery evidence.");
    QVERIFY(injectedEvidence.isValid());

    QByteArray corruptedForInjection = *recovery;
    corruptedForInjection[corruptedForInjection.size() / 3] ^= 0x01;
    QVERIFY(writeFile(recoveryFile, corruptedForInjection));
    QVERIFY(!journal.commit(injectedEvidence, state->sequence, *compiledEntry));
    QVERIFY(writeFile(recoveryFile, *recovery));
    const QString missingRecoveryBackup = recoveryFile + QStringLiteral(".missing");
    QVERIFY(QFile::rename(recoveryFile, missingRecoveryBackup));
    QVERIFY(!journal.commit(injectedEvidence, state->sequence, *compiledEntry));
    QVERIFY(QFile::rename(missingRecoveryBackup, recoveryFile));
    const auto unchangedAfterInjection = journal.load();
    QVERIFY(unchangedAfterInjection);
    QCOMPARE(*unchangedAfterInjection, *state);

    QByteArray corrupted = *recovery;
    corrupted[corrupted.size() / 2] ^= 0x01;
    QVERIFY(writeFile(recoveryFile, corrupted));
    QVERIFY(!journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *compiledEntry));
    QVERIFY(writeFile(recoveryFile, *recovery));
    QVERIFY(journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *compiledEntry));

    const QString backup = recoveryFile + QStringLiteral(".original");
    QVERIFY(QFile::rename(recoveryFile, backup));
    QCOMPARE(
        ::link(QFile::encodeName(backup).constData(), QFile::encodeName(recoveryFile).constData()),
        0);
    QVERIFY(!journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *compiledEntry));
    QVERIFY(QFile::remove(recoveryFile));
    QVERIFY(QFile::rename(backup, recoveryFile));
    QVERIFY(journal.loadRecovery(
        request.compileRequest.operationId, state->sequence, *compiledEntry));

    const Core::RuntimePackageCompilerPreparationStartRequest adoptedRequest
        = preparationRequest(fixture, 33);
    RuntimePackageCompilerPreparationJournalEntry adoptedEntry = journalEntry(
        adoptedRequest, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);
    const auto adoptedRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {adoptedRequest.compileRequest, fixture.serializedProject});
    QVERIFY(adoptedRecovery);
    const QString adoptedFile
        = journal.recoveryFile(adoptedRequest.compileRequest.operationId).path();
    QVERIFY(writeFile(adoptedFile, *adoptedRecovery));
    QCOMPARE(::chmod(QFile::encodeName(adoptedFile).constData(), 0600), 0);
    struct stat beforeAdoption = {};
    QVERIFY(::stat(QFile::encodeName(adoptedFile).constData(), &beforeAdoption) == 0);
    const auto adoptedState
        = journal.reserveWithRecovery(adoptedEntry, state->sequence, *adoptedRecovery);
    QVERIFY(adoptedState);
    struct stat afterAdoption = {};
    QVERIFY(::stat(QFile::encodeName(adoptedFile).constData(), &afterAdoption) == 0);
    QCOMPARE(afterAdoption.st_dev, beforeAdoption.st_dev);
    QCOMPARE(afterAdoption.st_ino, beforeAdoption.st_ino);

    const Core::RuntimePackageCompilerPreparationStartRequest orphanRequest
        = preparationRequest(fixture, 34);
    const auto orphanRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {orphanRequest.compileRequest, fixture.serializedProject});
    QVERIFY(orphanRecovery);
    const QString orphanFile
        = journal.recoveryFile(orphanRequest.compileRequest.operationId).path();
    QVERIFY(writeFile(orphanFile, *orphanRecovery));
    QCOMPARE(::chmod(QFile::encodeName(orphanFile).constData(), 0600), 0);
    const RuntimePackageCompilerPreparationJournalEntry *firstAfterAdoption
        = journalEntryFor(*adoptedState, request.compileRequest.operationId);
    QVERIFY(firstAfterAdoption);
    QVERIFY(journal.loadRecovery(
        request.compileRequest.operationId, adoptedState->sequence, *firstAfterAdoption));
    QVERIFY(!QFileInfo::exists(orphanFile));
#endif
}

void EtherCATProjectCompilerTests::testPreparationJournalBindsRecoveryFingerprint()
{
#ifndef Q_OS_UNIX
    QSKIP("Secure compiler recovery storage is Unix-only.");
#else
    CompilerFixture fixture;
    QTemporaryDir temporary;
    const Utils::FilePath root = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(root);
    auto state = journal.initialize();
    QVERIFY(state);

    Core::RuntimePackageCompilerPreparationStartRequest request
        = preparationRequest(fixture, 40);
    request.rollbackOnActivationFailure = false;
    QVERIFY(request.isValid());
    const auto recovery = encodeRuntimePackageCompilerCompileRecovery(
        {request.compileRequest, fixture.serializedProject});
    QVERIFY(recovery);
    const RuntimePackageCompilerPreparationJournalEntry reserved = journalEntry(
        request, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1);

    RuntimePackageCompilerPreparationJournalEntry forged = reserved;
    forged.startRequestFingerprint = sha256("forged preparation fingerprint");
    QVERIFY(!journal.reserveWithRecovery(forged, state->sequence, *recovery));
    forged = reserved;
    forged.verifyOperationId = preparationRequest(fixture, 41).verifyOperationId;
    QVERIFY(!journal.reserveWithRecovery(forged, state->sequence, *recovery));
    forged = reserved;
    forged.activationOperationId = preparationRequest(fixture, 42).activationOperationId;
    QVERIFY(!journal.reserveWithRecovery(forged, state->sequence, *recovery));
    forged = reserved;
    forged.rollbackOnActivationFailure = true;
    QVERIFY(!journal.reserveWithRecovery(forged, state->sequence, *recovery));

    state = journal.reserveWithRecovery(reserved, state->sequence, *recovery);
    QVERIFY(state);
    const QByteArray canonicalV3 = readFile(journal.journalFile().path());
    QVERIFY(!canonicalV3.isEmpty());

    const auto mutateRecord = [&](const std::function<void(QJsonObject &)> &mutation) {
        QJsonObject document = QJsonDocument::fromJson(canonicalV3).object();
        QJsonArray records = document.value(QStringLiteral("records")).toArray();
        if (records.size() != 1)
            return false;
        QJsonObject record = records.first().toObject();
        mutation(record);
        records[0] = record;
        document.insert(QStringLiteral("records"), records);
        QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
        bytes.append('\n');
        return writeFile(journal.journalFile().path(), bytes);
    };
    const auto rejectsMutation = [&](const std::function<void(QJsonObject &)> &mutation) {
        if (!mutateRecord(mutation))
            return false;
        const auto mutatedState = journal.load();
        if (!mutatedState)
            return false;
        const RuntimePackageCompilerPreparationJournalEntry *mutatedEntry
            = journalEntryFor(*mutatedState, request.compileRequest.operationId);
        if (!mutatedEntry
            || journal.loadRecovery(
                request.compileRequest.operationId, mutatedState->sequence, *mutatedEntry)) {
            return false;
        }
        return writeFile(journal.journalFile().path(), canonicalV3);
    };

    QVERIFY(rejectsMutation([](QJsonObject &record) {
        record.insert(QStringLiteral("rollback_on_activation_failure"), true);
    }));
    QVERIFY(rejectsMutation([](QJsonObject &record) {
        record.insert(QStringLiteral("rollback_on_activation_failure"), QJsonValue::Null);
    }));
    const Core::RuntimePackageCompilerPreparationStartRequest otherVerify
        = preparationRequest(fixture, 43);
    QVERIFY(rejectsMutation([&](QJsonObject &record) {
        record.insert(
            QStringLiteral("verify_operation_id"), otherVerify.verifyOperationId.value());
    }));
    const Core::RuntimePackageCompilerPreparationStartRequest otherActivation
        = preparationRequest(fixture, 44);
    QVERIFY(rejectsMutation([&](QJsonObject &record) {
        record.insert(
            QStringLiteral("activation_operation_id"),
            otherActivation.activationOperationId.value());
    }));
    QVERIFY(rejectsMutation([](QJsonObject &record) {
        record.insert(
            QStringLiteral("start_request_fingerprint"),
            QString::fromLatin1(sha256("unbound fingerprint").value().toHex()));
    }));

    QJsonObject legacyV2 = QJsonDocument::fromJson(canonicalV3).object();
    legacyV2.insert(QStringLiteral("format_version"), 2);
    QJsonArray legacyRecords = legacyV2.value(QStringLiteral("records")).toArray();
    QJsonObject legacyRecord = legacyRecords.first().toObject();
    legacyRecord.remove(QStringLiteral("rollback_on_activation_failure"));
    legacyRecords[0] = legacyRecord;
    legacyV2.insert(QStringLiteral("records"), legacyRecords);
    QByteArray legacyBytes = QJsonDocument(legacyV2).toJson(QJsonDocument::Compact);
    legacyBytes.append('\n');
    QVERIFY(writeFile(journal.journalFile().path(), legacyBytes));
    const auto legacyState = journal.load();
    QVERIFY(legacyState);
    const RuntimePackageCompilerPreparationJournalEntry *legacyEntry
        = journalEntryFor(*legacyState, request.compileRequest.operationId);
    QVERIFY(legacyEntry && !legacyEntry->rollbackOnActivationFailure);

    RuntimePackageCompilerPreparationJournalEntry wrongMigration = *legacyEntry;
    wrongMigration.revision = 2;
    wrongMigration.phase = Core::RuntimePackageCompilerPreparationPhase::Compiling;
    wrongMigration.rollbackOnActivationFailure = true;
    QVERIFY(!journal.commit(wrongMigration, legacyState->sequence, *legacyEntry));

    QJsonObject unboundLegacy = legacyV2;
    QJsonArray unboundRecords = unboundLegacy.value(QStringLiteral("records")).toArray();
    QJsonObject unboundRecord = unboundRecords.first().toObject();
    unboundRecord.insert(
        QStringLiteral("start_request_fingerprint"),
        QString::fromLatin1(sha256("unbound legacy fingerprint").value().toHex()));
    unboundRecords[0] = unboundRecord;
    unboundLegacy.insert(QStringLiteral("records"), unboundRecords);
    QByteArray unboundBytes = QJsonDocument(unboundLegacy).toJson(QJsonDocument::Compact);
    unboundBytes.append('\n');
    QVERIFY(writeFile(journal.journalFile().path(), unboundBytes));
    const auto unboundState = journal.load();
    QVERIFY(unboundState);
    const RuntimePackageCompilerPreparationJournalEntry *unboundEntry
        = journalEntryFor(*unboundState, request.compileRequest.operationId);
    QVERIFY(unboundEntry);
    QVERIFY(!journal.loadRecovery(
        request.compileRequest.operationId, unboundState->sequence, *unboundEntry));
    QVERIFY(writeFile(journal.journalFile().path(), legacyBytes));

    RuntimePackageCompilerPreparationJournalEntry migrated = *legacyEntry;
    migrated.revision = 2;
    migrated.phase = Core::RuntimePackageCompilerPreparationPhase::Compiling;
    const auto migratedState
        = journal.commit(migrated, legacyState->sequence, *legacyEntry);
    QVERIFY(migratedState);
    const RuntimePackageCompilerPreparationJournalEntry *migratedEntry
        = journalEntryFor(*migratedState, request.compileRequest.operationId);
    QVERIFY(migratedEntry && migratedEntry->rollbackOnActivationFailure);
    QVERIFY(!*migratedEntry->rollbackOnActivationFailure);
    const QJsonObject migratedDocument
        = QJsonDocument::fromJson(readFile(journal.journalFile().path())).object();
    QCOMPARE(migratedDocument.value(QStringLiteral("format_version")).toInt(), 3);
    QCOMPARE(
        migratedDocument.value(QStringLiteral("records"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("rollback_on_activation_failure"))
            .toBool(true),
        false);
    QVERIFY(journal.load());

    QTemporaryDir multipleTemporary;
    const Utils::FilePath multipleRoot = Utils::FilePath::fromString(
        multipleTemporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal multipleJournal(multipleRoot);
    auto multipleState = multipleJournal.initialize();
    QVERIFY(multipleState);

    Core::RuntimePackageCompilerPreparationStartRequest firstRequest
        = preparationRequest(fixture, 45);
    firstRequest.rollbackOnActivationFailure = false;
    Core::RuntimePackageCompilerPreparationStartRequest secondRequest
        = preparationRequest(fixture, 46);
    secondRequest.rollbackOnActivationFailure = true;
    QVERIFY(firstRequest.isValid());
    QVERIFY(secondRequest.isValid());
    const auto firstRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {firstRequest.compileRequest, fixture.serializedProject});
    const auto secondRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {secondRequest.compileRequest, fixture.serializedProject});
    QVERIFY(firstRecovery);
    QVERIFY(secondRecovery);
    multipleState = multipleJournal.reserveWithRecovery(
        journalEntry(firstRequest, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1),
        multipleState->sequence,
        *firstRecovery);
    QVERIFY(multipleState);
    multipleState = multipleJournal.reserveWithRecovery(
        journalEntry(secondRequest, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1),
        multipleState->sequence,
        *secondRecovery);
    QVERIFY(multipleState);

    QJsonObject multipleLegacy
        = QJsonDocument::fromJson(readFile(multipleJournal.journalFile().path())).object();
    multipleLegacy.insert(QStringLiteral("format_version"), 2);
    QJsonArray multipleLegacyRecords = multipleLegacy.value(QStringLiteral("records")).toArray();
    QCOMPARE(multipleLegacyRecords.size(), 2);
    for (qsizetype index = 0; index < multipleLegacyRecords.size(); ++index) {
        QJsonObject record = multipleLegacyRecords.at(index).toObject();
        record.remove(QStringLiteral("rollback_on_activation_failure"));
        multipleLegacyRecords[index] = record;
    }
    multipleLegacy.insert(QStringLiteral("records"), multipleLegacyRecords);
    QByteArray multipleLegacyBytes = QJsonDocument(multipleLegacy).toJson(QJsonDocument::Compact);
    multipleLegacyBytes.append('\n');
    QVERIFY(writeFile(multipleJournal.journalFile().path(), multipleLegacyBytes));

    const auto multipleLegacyState = multipleJournal.load();
    QVERIFY(multipleLegacyState);
    const RuntimePackageCompilerPreparationJournalEntry *firstLegacyEntry = journalEntryFor(
        *multipleLegacyState, firstRequest.compileRequest.operationId);
    const RuntimePackageCompilerPreparationJournalEntry *secondLegacyEntry = journalEntryFor(
        *multipleLegacyState, secondRequest.compileRequest.operationId);
    QVERIFY(firstLegacyEntry && !firstLegacyEntry->rollbackOnActivationFailure);
    QVERIFY(secondLegacyEntry && !secondLegacyEntry->rollbackOnActivationFailure);
    RuntimePackageCompilerPreparationJournalEntry firstCompiling = *firstLegacyEntry;
    firstCompiling.revision++;
    firstCompiling.phase = Core::RuntimePackageCompilerPreparationPhase::Compiling;
    const auto multipleMigratedState = multipleJournal.commit(
        firstCompiling, multipleLegacyState->sequence, *firstLegacyEntry);
    QVERIFY(multipleMigratedState);

    const RuntimePackageCompilerPreparationJournalEntry *firstMigratedEntry = journalEntryFor(
        *multipleMigratedState, firstRequest.compileRequest.operationId);
    const RuntimePackageCompilerPreparationJournalEntry *secondMigratedEntry = journalEntryFor(
        *multipleMigratedState, secondRequest.compileRequest.operationId);
    QVERIFY(firstMigratedEntry && firstMigratedEntry->rollbackOnActivationFailure);
    QVERIFY(secondMigratedEntry && secondMigratedEntry->rollbackOnActivationFailure);
    QVERIFY(!*firstMigratedEntry->rollbackOnActivationFailure);
    QVERIFY(*secondMigratedEntry->rollbackOnActivationFailure);
    const QJsonObject multipleMigratedDocument
        = QJsonDocument::fromJson(readFile(multipleJournal.journalFile().path())).object();
    QCOMPARE(multipleMigratedDocument.value(QStringLiteral("format_version")).toInt(), 3);
    QVERIFY(multipleJournal.loadRecovery(
        firstRequest.compileRequest.operationId,
        multipleMigratedState->sequence,
        *firstMigratedEntry));
    QVERIFY(multipleJournal.loadRecovery(
        secondRequest.compileRequest.operationId,
        multipleMigratedState->sequence,
        *secondMigratedEntry));

    QJsonObject partiallyRecoverableLegacy = multipleMigratedDocument;
    partiallyRecoverableLegacy.insert(QStringLiteral("format_version"), 2);
    QJsonArray partiallyRecoverableRecords
        = partiallyRecoverableLegacy.value(QStringLiteral("records")).toArray();
    for (qsizetype index = 0; index < partiallyRecoverableRecords.size(); ++index) {
        QJsonObject record = partiallyRecoverableRecords.at(index).toObject();
        record.remove(QStringLiteral("rollback_on_activation_failure"));
        partiallyRecoverableRecords[index] = record;
    }
    partiallyRecoverableLegacy.insert(QStringLiteral("records"), partiallyRecoverableRecords);
    QByteArray partiallyRecoverableBytes
        = QJsonDocument(partiallyRecoverableLegacy).toJson(QJsonDocument::Compact);
    partiallyRecoverableBytes.append('\n');
    QVERIFY(writeFile(multipleJournal.journalFile().path(), partiallyRecoverableBytes));
    QVERIFY(QFile::remove(
        multipleJournal.recoveryFile(secondRequest.compileRequest.operationId).path()));
    const auto partiallyRecoverableState = multipleJournal.load();
    QVERIFY(partiallyRecoverableState);

    Core::RuntimePackageCompilerPreparationStartRequest newRequest
        = preparationRequest(fixture, 47);
    newRequest.rollbackOnActivationFailure = true;
    QVERIFY(newRequest.isValid());
    const auto newRecovery = encodeRuntimePackageCompilerCompileRecovery(
        {newRequest.compileRequest, fixture.serializedProject});
    QVERIFY(newRecovery);
    const auto stateWithNewReservation = multipleJournal.reserveWithRecovery(
        journalEntry(newRequest, Core::RuntimePackageCompilerPreparationPhase::Reserved, 1),
        partiallyRecoverableState->sequence,
        *newRecovery);
    QVERIFY(stateWithNewReservation);
    const RuntimePackageCompilerPreparationJournalEntry *recoverableLegacyEntry = journalEntryFor(
        *stateWithNewReservation, firstRequest.compileRequest.operationId);
    const RuntimePackageCompilerPreparationJournalEntry *isolatedLegacyEntry = journalEntryFor(
        *stateWithNewReservation, secondRequest.compileRequest.operationId);
    const RuntimePackageCompilerPreparationJournalEntry *newReservationEntry = journalEntryFor(
        *stateWithNewReservation, newRequest.compileRequest.operationId);
    QVERIFY(recoverableLegacyEntry && recoverableLegacyEntry->rollbackOnActivationFailure);
    QVERIFY(!*recoverableLegacyEntry->rollbackOnActivationFailure);
    QVERIFY(isolatedLegacyEntry && !isolatedLegacyEntry->rollbackOnActivationFailure);
    QVERIFY(newReservationEntry && newReservationEntry->rollbackOnActivationFailure);
    QVERIFY(*newReservationEntry->rollbackOnActivationFailure);
    const QJsonObject upgradedWithIsolatedRecord
        = QJsonDocument::fromJson(readFile(multipleJournal.journalFile().path())).object();
    QCOMPARE(upgradedWithIsolatedRecord.value(QStringLiteral("format_version")).toInt(), 3);
    QVERIFY(!multipleJournal.loadRecovery(
        secondRequest.compileRequest.operationId,
        stateWithNewReservation->sequence,
        *isolatedLegacyEntry));
    QVERIFY(multipleJournal.loadRecovery(
        newRequest.compileRequest.operationId,
        stateWithNewReservation->sequence,
        *newReservationEntry));
#endif
}

void EtherCATProjectCompilerTests::testPreparationJournalRejectsUnsafeStorage()
{
    QTemporaryDir temporary;
    const Utils::FilePath root = Utils::FilePath::fromString(
        temporary.filePath(QStringLiteral("preparations")));
    RuntimePackageCompilerPreparationJournal journal(root);
    const auto initialized = journal.initialize();
    QVERIFY(initialized);
    const QString journalFile = journal.journalFile().path();
    const QByteArray original = readFile(journalFile);
    QVERIFY(!original.isEmpty());

    QByteArray nonCanonical = original;
    nonCanonical.insert(nonCanonical.size() - 1, ' ');
    QVERIFY(writeFile(journalFile, nonCanonical));
    QVERIFY(!journal.load());
    QVERIFY(writeFile(journalFile, original));
    QVERIFY(journal.load());

    QJsonObject rootObject = QJsonDocument::fromJson(original).object();
    rootObject.insert(QStringLiteral("unexpected"), true);
    QByteArray unknownField = QJsonDocument(rootObject).toJson(QJsonDocument::Compact);
    unknownField.append('\n');
    QVERIFY(writeFile(journalFile, unknownField));
    QVERIFY(!journal.load());
    QVERIFY(writeFile(journalFile, original));
    QVERIFY(journal.load());

#ifdef Q_OS_UNIX
    const QString backup = journalFile + QStringLiteral(".original");
    QVERIFY(QFile::rename(journalFile, backup));
    QCOMPARE(
        ::symlink(QFile::encodeName(backup).constData(), QFile::encodeName(journalFile).constData()),
        0);
    QVERIFY(!journal.load());
    QVERIFY(QFile::remove(journalFile));
    QVERIFY(QFile::rename(backup, journalFile));

    QVERIFY(QFile::rename(journalFile, backup));
    QCOMPARE(
        ::link(QFile::encodeName(backup).constData(), QFile::encodeName(journalFile).constData()),
        0);
    QVERIFY(!journal.load());
    QVERIFY(QFile::remove(journalFile));
    QVERIFY(QFile::rename(backup, journalFile));
    QVERIFY(journal.load());
#endif
}

} // namespace EtherCAT::ProjectCompiler::Internal
