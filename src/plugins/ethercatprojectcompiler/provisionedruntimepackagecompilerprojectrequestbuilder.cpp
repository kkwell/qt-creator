// Copyright (C) 2026 Embed Labs

#include "provisionedruntimepackagecompilerprojectrequestbuilder.h"

#include "ethercatprojectcompilertr.h"

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/runtimepackagecompilercodec.h>
#include <ethercatcore/runtimepackagecompilerpreparationcoordinator.h>

#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <limits>

namespace EtherCAT::ProjectCompiler {

namespace {

template<typename ProviderType>
Utils::Result<ProviderType *> uniqueAvailableProvider(
    Core::ProviderRegistry *registry, Core::ProviderKind kind, const QString &label)
{
    QList<ProviderType *> matches;
    for (Core::Provider *provider : registry->providers(kind)) {
        if (!provider || !provider->isAvailable())
            continue;
        if (provider->thread() != QThread::currentThread())
            return Utils::ResultError(Tr::tr("%1 provider belongs to another thread.").arg(label));
        auto *typed = qobject_cast<ProviderType *>(provider);
        if (!typed)
            return Utils::ResultError(Tr::tr("%1 provider has the wrong type.").arg(label));
        matches.append(typed);
    }
    if (matches.size() != 1) {
        return Utils::ResultError(
            matches.isEmpty() ? Tr::tr("%1 provider is unavailable.").arg(label)
                              : Tr::tr("%1 provider selection is ambiguous.").arg(label));
    }
    return matches.constFirst();
}

bool stableId(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    return pattern.match(value).hasMatch();
}

QString dataTypeName(Data::EtherCATDataType type, const QString &rawType)
{
    using Type = Data::EtherCATDataType;
    switch (type) {
    case Type::Boolean:
        return QStringLiteral("BOOL");
    case Type::Integer8:
        return QStringLiteral("SINT");
    case Type::UnsignedInteger8:
        return QStringLiteral("USINT");
    case Type::Integer16:
        return QStringLiteral("INT");
    case Type::UnsignedInteger16:
        return QStringLiteral("UINT");
    case Type::Integer32:
        return QStringLiteral("DINT");
    case Type::UnsignedInteger32:
        return QStringLiteral("UDINT");
    case Type::Integer64:
        return QStringLiteral("LINT");
    case Type::UnsignedInteger64:
        return QStringLiteral("ULINT");
    case Type::Real32:
        return QStringLiteral("REAL");
    case Type::Real64:
        return QStringLiteral("LREAL");
    case Type::VisibleString:
        return QStringLiteral("STRING");
    case Type::OctetString:
        return QStringLiteral("OCTET_STRING");
    case Type::Unknown:
        return rawType;
    }
    return {};
}

QByteArray quotedJson(const QString &value)
{
    QByteArray result = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return result.mid(1, result.size() - 2);
}

Data::RuntimePackageCompilerCanonicalJson canonicalTopologyEvidence(
    const Data::RuntimePackageCompilerFreshTopologyEvidence &topology)
{
    QByteArray bytes{"{\"capture_boot_id\":\"0x"};
    bytes += QByteArray::number(topology.captureBootId, 16).rightJustified(16, '0');
    bytes += "\",\"capture_sequence\":" + QByteArray::number(topology.captureSequence);
    bytes += ",\"captured_at_ns\":" + QByteArray::number(topology.capturedAtNs);
    bytes += ",\"evidence_id\":" + quotedJson(topology.evidenceId);
    bytes += ",\"expires_at_ns\":" + QByteArray::number(topology.expiresAtNs);
    bytes += ",\"format\":\"ethercat-discover-topology-evidence-v1\"";
    bytes += ",\"format_version\":1,\"matched_scan\":{";
    bytes += "\"cycle_period_ns\":" + QByteArray::number(topology.cyclePeriodNs);
    bytes += ",\"format\":\"ethercat-esi-match-v1\",\"link_speed_mbps\":";
    bytes += QByteArray::number(topology.linkSpeedMbps) + ",\"slaves\":[";
    for (qsizetype index = 0; index < topology.slaves.size(); ++index) {
        const Data::RuntimePackageCompilerTopologySlaveEvidence &slave = topology.slaves.at(index);
        if (index)
            bytes += ',';
        bytes += "{\"alias\":" + QByteArray::number(slave.alias);
        bytes += ",\"identity\":{\"product_code\":";
        bytes += QByteArray::number(slave.identity.productCode);
        bytes += ",\"revision\":" + QByteArray::number(slave.identity.revisionNumber);
        bytes += ",\"vendor_id\":" + QByteArray::number(slave.identity.vendorId) + '}';
        bytes += ",\"modules\":[],\"position\":" + QByteArray::number(slave.position);
        bytes += ",\"serial\":" + QByteArray::number(slave.serialNumber);
        bytes += ",\"station_address\":" + QByteArray::number(slave.stationAddress) + '}';
    }
    bytes += "]}}\n";
    return Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(std::move(bytes));
}

const CompilerInputProvisionedDevice *provisionedDevice(
    const CompilerInputProvisioningProfile &profile, const Data::NodeId &slaveId)
{
    const QList<CompilerInputProvisionedDevice> &devices = profile.devices();
    const auto found = std::find_if(devices.cbegin(), devices.cend(), [&](const auto &device) {
        return device.projectSlaveNodeId == slaveId;
    });
    return found == devices.cend() ? nullptr : &*found;
}

const Data::ProcessDataProfile *selectedProcessDataProfile(
    const Data::DeviceAdapterManifest &manifest, const QString &id)
{
    const auto found = std::find_if(
        manifest.processDataProfiles.cbegin(),
        manifest.processDataProfiles.cend(),
        [&](const Data::ProcessDataProfile &profile) { return profile.id == id; });
    return found == manifest.processDataProfiles.cend() ? nullptr : &*found;
}

Utils::Result<> validateSelectedPdos(
    const Data::OfflineSlaveConfiguration &slave, const Data::ProcessDataProfile &profile)
{
    QList<quint16> rxPdos;
    QList<quint16> txPdos;
    QSet<quint16> seenRx;
    QSet<quint16> seenTx;
    for (const Data::PdoConfiguration &pdo : slave.processData.pdos) {
        if (!pdo.selected)
            continue;
        QList<quint16> *indices
            = pdo.direction == Data::PdoDirection::Rx ? &rxPdos : &txPdos;
        QSet<quint16> *seen = pdo.direction == Data::PdoDirection::Rx ? &seenRx : &seenTx;
        if (seen->contains(pdo.index))
            return Utils::ResultError(Tr::tr("The selected PDO profile contains duplicates."));
        seen->insert(pdo.index);
        indices->append(pdo.index);
    }
    if (QSet<quint16>(profile.rxPdoIndices.cbegin(), profile.rxPdoIndices.cend()).size()
            != profile.rxPdoIndices.size()
        || QSet<quint16>(profile.txPdoIndices.cbegin(), profile.txPdoIndices.cend()).size()
               != profile.txPdoIndices.size()
        || rxPdos != profile.rxPdoIndices || txPdos != profile.txPdoIndices) {
        return Utils::ResultError(
            Tr::tr("Project PDO selection differs from the signed PDO profile."));
    }
    return Utils::ResultOk;
}

Utils::Result<Data::RuntimePackageCompilerPdoMapping> pdoProjection(
    const Data::PdoConfiguration &pdo)
{
    if (!pdo.selected || !stableId(pdo.name) || pdo.syncManager < 0 || pdo.syncManager > 15
        || pdo.entries.isEmpty()) {
        return Utils::ResultError(Tr::tr("A selected PDO lacks explicit compiler identifiers."));
    }
    Data::RuntimePackageCompilerPdoMapping result;
    result.id = pdo.name;
    result.direction = pdo.direction == Data::PdoDirection::Tx
                           ? Data::RuntimePackageCompilerPdoDirection::Input
                           : Data::RuntimePackageCompilerPdoDirection::Output;
    result.pdoIndex = pdo.index;
    result.syncManager = quint8(pdo.syncManager);
    result.fixed = pdo.fixed;
    for (const Data::PdoEntryConfiguration &entry : pdo.entries) {
        const QString type = dataTypeName(entry.dataType, entry.rawDataType);
        if (!stableId(entry.name) || entry.bitLength <= 0 || entry.bitLength > 65535
            || type.isEmpty()) {
            return Utils::ResultError(
                Tr::tr("A selected PDO entry lacks explicit compiler identifiers."));
        }
        result.entries.append({entry.name,
                               entry.index,
                               entry.subIndex,
                               quint16(entry.bitLength),
                               type});
    }
    return result.isValid() ? Utils::Result<Data::RuntimePackageCompilerPdoMapping>{result}
                            : Utils::Result<Data::RuntimePackageCompilerPdoMapping>{
                                  Utils::ResultError(Tr::tr("PDO projection is invalid."))};
}

Utils::Result<Data::RuntimePackageCompilerDeviceProjection> deviceProjection(
    const Data::OfflineSlaveConfiguration &slave,
    const Data::DeviceAdapterManifest &manifest,
    const Data::ProcessDataProfile &processProfile,
    const CompilerInputProvisionedDevice &provisioned,
    const Data::RuntimePackageCompilerSignedTargetProfileEvidence &target)
{
    if (!slave.startup.parameters.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("Startup SDO compiler metadata is not provisioned; compilation is denied."));
    }
    if (slave.alias != 0 || !slave.adapterSelection.moduleAssignments.isEmpty()
        || provisioned.expectedAlias != 0 || !provisioned.expectedModuleAssignments.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("Product API topology cannot prove alias or module assignments."));
    }

    Data::RuntimePackageCompilerDeviceProjection result;
    result.projectSlaveNodeId = slave.id;
    result.slaveNodeId = provisioned.slaveNodeId;
    result.projectDeviceId = provisioned.projectDeviceId;
    result.position = slave.position;
    result.stationAddress = slave.stationAddress;
    result.alias = 0;
    result.identity = slave.identity;
    result.serialNumber = slave.serialNumber;
    result.esiSha256 = Data::RuntimePackageCompilerSha256{slave.esiSha256};
    result.targetProfileId = target.profileId;
    result.adapterId = manifest.controllerAdapterTarget.adapterId;
    result.adapterVersion = manifest.controllerAdapterTarget.adapterVersion;
    result.adapterSha256 = Data::RuntimePackageCompilerSha256{
        manifest.controllerAdapterTarget.adapterSha256};
    result.pdoProfileId = processProfile.signedPdoProfileId;
    for (const Data::PdoConfiguration &pdo : slave.processData.pdos) {
        if (!pdo.selected)
            continue;
        const auto projected = pdoProjection(pdo);
        if (!projected)
            return Utils::ResultError(projected.error());
        result.pdoMappings.append(*projected);
    }
    if (slave.dc.enabled) {
        if (processProfile.signedDcProfileId.isEmpty())
            return Utils::ResultError(Tr::tr("The selected DC profile is not signed."));
        result.signedDcProfileId = processProfile.signedDcProfileId;
        result.dc = {
            true,
            processProfile.signedDcProfileId,
            slave.dc.modeName,
            slave.dc.assignActivate,
            quint64(slave.dc.sync0.cycleTimeNs),
            slave.dc.sync0.shiftTimeNs,
            quint64(slave.dc.sync1.cycleTimeNs),
            slave.dc.sync1.shiftTimeNs,
            slave.dc.potentialReferenceClock,
        };
    }
    result.componentBindingIds = provisioned.componentBindingIds;
    result.semanticBindingIds = provisioned.semanticBindingIds;
    result.semanticActionBindingIds = provisioned.semanticActionBindingIds;
    result.symbolMode = provisioned.symbolMode;
    result.symbols = provisioned.symbols;
    result.manualEnvelope = provisioned.manualEnvelope;
    return result.isValid() ? Utils::Result<Data::RuntimePackageCompilerDeviceProjection>{result}
                            : Utils::Result<Data::RuntimePackageCompilerDeviceProjection>{
                                  Utils::ResultError(Tr::tr("Device projection is incomplete."))};
}

} // namespace

class ProvisionedRuntimePackageCompilerProjectRequestBuilder::Private
{
public:
    Private(
        ProvisionedRuntimePackageCompilerProjectRequestBuilder *q,
        Core::ProviderRegistry *providerRegistry,
        const Utils::FilePath &inputProvisioningFile,
        CurrentTimeNs currentTimeNs)
        : q(q)
        , registry(providerRegistry)
        , currentTimeNs(
              currentTimeNs ? std::move(currentTimeNs)
                            : [] {
                                  const qint64 milliseconds
                                      = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
                                  return milliseconds > 0
                                                 && quint64(milliseconds)
                                                        <= std::numeric_limits<quint64>::max()
                                                               / 1'000'000
                                             ? quint64(milliseconds) * 1'000'000
                                             : 0;
                              })
    {
        if (!registry) {
            error = Tr::tr("Provider registry is unavailable.");
            return;
        }
        const auto loaded = CompilerInputProvisioningProfile::load(inputProvisioningFile);
        if (!loaded) {
            error = loaded.error();
            return;
        }
        profile = *loaded;
        q->setAvailable(true);
    }

    Utils::Result<Core::RuntimePackageCompilerPreparationStartRequest> build(
        const Core::RuntimePackageCompilerProjectRequestSeed &seed)
    {
        if (!seed.isValid())
            return Utils::ResultError(Tr::tr("Compiler request seed is invalid."));
        if (!registry || QThread::currentThread() != q->thread()
            || QThread::currentThread() != registry->thread()) {
            return Utils::ResultError(Tr::tr("Compiler request builder thread is invalid."));
        }
        if (!profile)
            return Utils::ResultError(error);
        if (const Utils::Result<> current = profile->validateCurrent(); !current) {
            error = current.error();
            q->setAvailable(false);
            return Utils::ResultError(error);
        }

        const auto projectService = projectOwner(seed.scope.projectId);
        const auto connectionProvider = connectionOwner(seed.scope);
        const auto deviceRepository = uniqueAvailableProvider<Core::DeviceRepositoryProvider>(
            registry, Core::ProviderKind::DeviceRepository, Tr::tr("ESI repository"));
        const auto adapterProvider = uniqueAvailableProvider<Core::DeviceAdapterProvider>(
            registry, Core::ProviderKind::DeviceAdapter, Tr::tr("device adapter"));
        if (!projectService)
            return Utils::ResultError(projectService.error());
        if (!connectionProvider)
            return Utils::ResultError(connectionProvider.error());
        if (!deviceRepository)
            return Utils::ResultError(deviceRepository.error());
        if (!adapterProvider)
            return Utils::ResultError(adapterProvider.error());

        const Data::ControllerConnectionSnapshot before = (*connectionProvider)->connectionSnapshot();
        if (const Utils::Result<> valid = validateConnectionSnapshot(before, seed); !valid)
            return Utils::ResultError(valid.error());
        const auto capture = (*projectService)->captureRuntimePackageActivationProject(
            seed.scope.projectId);
        if (!capture)
            return Utils::ResultError(capture.error());
        const Data::ControllerConnectionSnapshot after = (*connectionProvider)->connectionSnapshot();
        if (before != after) {
            return Utils::ResultError(
                Tr::tr("Controller topology changed while project inputs were captured."));
        }

        Data::RuntimePackageCompilerCompileRequest request;
        request.operationId = seed.compileOperationId;
        request.intentId = seed.intentId;
        request.configurationId = seed.configurationId;
        request.buildTimestampNs = seed.buildTimestampNs;
        request.compileTimeNs = seed.compileTimeNs;
        request.contractIdentity = profile->contractIdentity();
        request.projectSnapshotEvidence = Data::RuntimePackageCompilerProjectSnapshotEvidence{
            *capture};
        request.targetProfile = profile->targetProfile();
        request.sourceArtifacts = profile->sourceArtifacts();
        const auto topology = buildTopology(seed, *before.topology, capture->snapshot());
        if (!topology)
            return Utils::ResultError(topology.error());
        request.topologyEvidence = *topology;
        request.sourceArtifacts.topologyEvidence = {
            Data::RuntimePackageCompilerSourceArtifactKind::TopologyEvidence,
            QStringLiteral("topology-evidence-v1.json"),
            topology->canonicalEvidence.exactBytes(),
            topology->canonicalEvidence.sha256(),
        };

        const auto projection = buildProjectProjection(
            seed,
            *capture,
            **deviceRepository,
            **adapterProvider,
            &request.deviceSourceEvidence);
        if (!projection)
            return Utils::ResultError(projection.error());
        request.projectProjection = *projection;
        if (!request.isValid())
            return Utils::ResultError(Tr::tr("Captured compiler request failed strict validation."));
        if (const auto encoded = Core::encodeRuntimePackageCompilerCompileRequest(request); !encoded)
            return Utils::ResultError(encoded.error());

        Core::RuntimePackageCompilerPreparationStartRequest result{
            request,
            seed.verifyOperationId,
            seed.activationOperationId,
            seed.rollbackOnActivationFailure,
        };
        return result.isValid()
                   ? Utils::Result<Core::RuntimePackageCompilerPreparationStartRequest>{result}
                   : Utils::Result<Core::RuntimePackageCompilerPreparationStartRequest>{
                         Utils::ResultError(Tr::tr("Compiler preparation request is invalid."))};
    }

    Utils::Result<Core::ProjectService *> projectOwner(const Data::NodeId &projectId) const
    {
        QList<Core::ProjectService *> matches;
        for (Core::Provider *provider : registry->providers(Core::ProviderKind::Project)) {
            if (!provider || !provider->isAvailable())
                continue;
            if (provider->thread() != QThread::currentThread()) {
                return Utils::ResultError(
                    Tr::tr("Project owner provider belongs to another thread."));
            }
            auto *project = qobject_cast<Core::ProjectService *>(provider);
            if (!project)
                return Utils::ResultError(Tr::tr("Project owner provider has the wrong type."));
            if (project->project(projectId))
                matches.append(project);
        }
        if (matches.size() != 1) {
            return Utils::ResultError(
                matches.isEmpty() ? Tr::tr("Project owner is unavailable.")
                                  : Tr::tr("Project owner selection is ambiguous."));
        }
        return matches.constFirst();
    }

    Utils::Result<Core::ControllerConnectionProvider *> connectionOwner(
        const Data::ControllerConnectionScope &scope) const
    {
        QList<Core::ControllerConnectionProvider *> matches;
        for (Core::Provider *provider : registry->providers(
                 Core::ProviderKind::ControllerConnection)) {
            if (!provider || !provider->isAvailable())
                continue;
            if (provider->thread() != QThread::currentThread()) {
                return Utils::ResultError(
                    Tr::tr("Controller connection provider belongs to another thread."));
            }
            auto *connection = qobject_cast<Core::ControllerConnectionProvider *>(provider);
            if (!connection) {
                return Utils::ResultError(
                    Tr::tr("Controller connection provider has the wrong type."));
            }
            if (connection->connectionSnapshot().scope == scope) {
                matches.append(connection);
            }
        }
        if (matches.size() != 1) {
            return Utils::ResultError(
                matches.isEmpty() ? Tr::tr("Controller connection is unavailable.")
                                  : Tr::tr("Controller connection selection is ambiguous."));
        }
        return matches.constFirst();
    }

    Utils::Result<> validateConnectionSnapshot(
        const Data::ControllerConnectionSnapshot &snapshot,
        const Core::RuntimePackageCompilerProjectRequestSeed &seed) const
    {
        if (snapshot.scope != seed.scope || snapshot.state != Data::ControllerConnectionState::Connected
            || snapshot.mock || snapshot.sessionGeneration == 0 || !snapshot.session
            || !snapshot.capability || !snapshot.topology || !snapshot.topology->hasCompleteProvenance()
            || snapshot.topology->scope != seed.scope
            || snapshot.topology->sessionGeneration != snapshot.sessionGeneration
            || snapshot.topology->sessionId != snapshot.session->sessionId
            || snapshot.topology->bootId != snapshot.session->bootId) {
            return Utils::ResultError(
                Tr::tr("A current real Product API topology capture is required."));
        }
        if (snapshot.capability->descriptorSha256
            != profile->targetProfile().capabilityDescriptorSha256.value()) {
            return Utils::ResultError(
                Tr::tr("Controller capability does not match the signed target profile."));
        }
        return Utils::ResultOk;
    }

    Utils::Result<Data::RuntimePackageCompilerFreshTopologyEvidence> buildTopology(
        const Core::RuntimePackageCompilerProjectRequestSeed &seed,
        const Data::ControllerTopologySnapshot &observed,
        const Data::ProjectSnapshot &project) const
    {
        const qint64 capturedMs = observed.receivedAt.toMSecsSinceEpoch();
        if (capturedMs <= 0 || quint64(capturedMs) > std::numeric_limits<quint64>::max() / 1'000'000
            || quint64(capturedMs) * 1'000'000
                   > std::numeric_limits<quint64>::max() - profile->topologyTtlNs()) {
            return Utils::ResultError(Tr::tr("Topology capture time is invalid."));
        }
        Data::RuntimePackageCompilerFreshTopologyEvidence result;
        result.scope = seed.scope;
        result.sessionGeneration = observed.sessionGeneration;
        result.sessionId = observed.sessionId;
        result.captureBootId = observed.bootId;
        result.captureSequence = observed.cpu1RequestSequence;
        result.evidenceId = QStringLiteral("discover:boot-%1:sequence-%2")
                                .arg(observed.bootId, 16, 16, QLatin1Char('0'))
                                .arg(observed.cpu1RequestSequence);
        result.capturedAtNs = quint64(capturedMs) * 1'000'000;
        result.expiresAtNs = result.capturedAtNs + profile->topologyTtlNs();
        result.cyclePeriodNs = project.masterConfiguration.cyclePeriodNs;
        result.linkSpeedMbps = 100;

        QList<Data::OfflineSlaveConfiguration> slaves;
        for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
            if (slave.masterId == seed.scope.masterId)
                slaves.append(slave);
        }
        if (slaves.size() != observed.slaves.size() || slaves.size() != profile->devices().size())
            return Utils::ResultError(Tr::tr("Project and controller topology sizes differ."));
        int previousPosition = -1;
        for (qsizetype index = 0; index < slaves.size(); ++index) {
            const Data::OfflineSlaveConfiguration &slave = slaves.at(index);
            const Data::ControllerTopologySlave &wire = observed.slaves.at(index);
            const CompilerInputProvisionedDevice *provisioned = provisionedDevice(*profile, slave.id);
            if (!provisioned || slave.position <= previousPosition || slave.position != int(wire.position)
                || slave.stationAddress != wire.stationAddress || slave.identity.vendorId != wire.vendorId
                || slave.identity.productCode != wire.productCode
                || slave.identity.revisionNumber != wire.revision
                || slave.serialNumber != wire.serial || slave.alias != 0
                || !slave.adapterSelection.moduleAssignments.isEmpty()
                || provisioned->expectedAlias != 0
                || !provisioned->expectedModuleAssignments.isEmpty()) {
                return Utils::ResultError(
                    Tr::tr("Project, provisioned input, and Product API topology differ."));
            }
            previousPosition = slave.position;
            result.slaves.append({slave.id,
                                  slave.position,
                                  slave.stationAddress,
                                  0,
                                  slave.identity,
                                  slave.serialNumber,
                                  {}});
        }
        result.canonicalEvidence = canonicalTopologyEvidence(result);
        const quint64 actualBuildTimeNs = currentTimeNs();
        if (actualBuildTimeNs < result.capturedAtNs || actualBuildTimeNs > result.expiresAtNs) {
            return Utils::ResultError(
                Tr::tr("Product API topology evidence is stale at build time."));
        }
        if (!result.isFreshAt(seed.compileTimeNs))
            return Utils::ResultError(Tr::tr("Product API topology evidence is stale."));
        return result;
    }

    Utils::Result<Data::RuntimePackageCompilerProjectProjection> buildProjectProjection(
        const Core::RuntimePackageCompilerProjectRequestSeed &seed,
        const Data::RuntimePackageActivationProjectCapture &capture,
        Core::DeviceRepositoryProvider &deviceRepository,
        Core::DeviceAdapterProvider &adapterProvider,
        QList<Data::RuntimePackageCompilerDeviceSourceEvidence> *deviceSources) const
    {
        const Data::ProjectSnapshot &project = capture.snapshot();
        Data::RuntimePackageCompilerProjectProjection result;
        result.projectNodeId = project.id;
        result.masterProjectNodeId = seed.scope.masterId;
        result.projectId = profile->projectId();
        result.masterNodeId = profile->masterNodeId();
        result.documentRevision = capture.documentRevisionNumber();
        result.timingMode = project.masterConfiguration.timingMode;
        result.cyclePeriodNs = project.masterConfiguration.cyclePeriodNs;
        result.linkSpeedMbps = 100;
        result.uiMetadata = profile->uiMetadata();

        for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
            if (slave.masterId != seed.scope.masterId)
                continue;
            const CompilerInputProvisionedDevice *provisioned = provisionedDevice(*profile, slave.id);
            if (!provisioned)
                return Utils::ResultError(Tr::tr("Slave compiler input is not provisioned."));
            if (!project.masterBindingArtifact.projectDeviceBindings.isEmpty()) {
                const auto binding = std::find_if(
                    project.masterBindingArtifact.projectDeviceBindings.cbegin(),
                    project.masterBindingArtifact.projectDeviceBindings.cend(),
                    [&](const Data::SemanticProjectDeviceBinding &candidate) {
                        return candidate.slaveId == slave.id;
                    });
                if (binding == project.masterBindingArtifact.projectDeviceBindings.cend()
                    || binding->projectDeviceId != provisioned->projectDeviceId) {
                    return Utils::ResultError(
                        Tr::tr("Existing project device binding differs from provisioning."));
                }
            }

            const auto description = deviceRepository.device(slave.deviceDescriptionId);
            const QByteArray esiBytes = deviceRepository.originalXml(slave.deviceDescriptionId);
            if (!description || esiBytes.isEmpty() || description->summary.identity != slave.identity
                || description->sourceSha256 != slave.esiSha256
                || QCryptographicHash::hash(esiBytes, QCryptographicHash::Sha256)
                       != slave.esiSha256) {
                return Utils::ResultError(Tr::tr("Original ESI evidence is unavailable or stale."));
            }
            const auto manifest = adapterProvider.adapterManifest(
                slave.adapterSelection.adapterId, slave.adapterSelection.adapterVersion);
            if (!manifest || manifest->contractVersion != Data::DeviceAdapterContractVersion::V3
                || !manifest->signatureVerified || !manifest->realHardwareAllowed
                || manifest->contentSha256 != slave.adapterSelection.adapterContentSha256
                || manifest->match.exactEsiSha256 != slave.esiSha256
                || manifest->controllerAdapterTarget.esiSha256 != slave.esiSha256
                || manifest->controllerAdapterTarget.adapterSha256
                       != provisioned->adapterSourceFile.sha256.value()) {
                return Utils::ResultError(Tr::tr("Upper device adapter evidence is incomplete."));
            }
            const Data::ProcessDataProfile *processProfile = selectedProcessDataProfile(
                *manifest, slave.adapterSelection.processDataProfileId);
            if (!processProfile || processProfile->signedPdoProfileId.isEmpty())
                return Utils::ResultError(Tr::tr("Selected PDO profile is not signed."));
            if (const Utils::Result<> pdoValidation = validateSelectedPdos(slave, *processProfile);
                !pdoValidation) {
                return Utils::ResultError(pdoValidation.error());
            }

            const auto projected = deviceProjection(
                slave, *manifest, *processProfile, *provisioned, profile->targetProfile());
            if (!projected)
                return Utils::ResultError(projected.error());
            result.devices.append(*projected);

            Data::RuntimePackageCompilerDeviceSourceEvidence source;
            source.projectSlaveNodeId = slave.id;
            source.originalEsi = {
                Data::RuntimePackageCompilerSourceArtifactKind::OriginalEsi,
                QStringLiteral("esi/%1.xml").arg(QString::fromLatin1(slave.esiSha256.toHex())),
                esiBytes,
                Data::RuntimePackageCompilerSha256{slave.esiSha256},
            };
            source.adapterSourceFile = provisioned->adapterSourceFile;
            source.projectAdapterContractVersion = manifest->contractVersion;
            source.projectAdapterId = manifest->id;
            source.projectAdapterVersion = manifest->version;
            source.projectAdapterContentSha256 = Data::RuntimePackageCompilerSha256{
                manifest->contentSha256};
            source.projectControllerAdapterTarget = manifest->controllerAdapterTarget;
            source.projectPdoProfileId = processProfile->id;
            source.projectSignedPdoProfileId = processProfile->signedPdoProfileId;
            source.projectSignedDcProfileId = processProfile->signedDcProfileId;
            source.adapterId = manifest->controllerAdapterTarget.adapterId;
            source.adapterVersion = manifest->controllerAdapterTarget.adapterVersion;
            source.adapterCanonicalSha256 = Data::RuntimePackageCompilerSha256{
                manifest->controllerAdapterTarget.adapterSha256};
            source.pdoProfileId = processProfile->signedPdoProfileId;
            if (slave.dc.enabled)
                source.signedDcProfileId = processProfile->signedDcProfileId;
            else
                source.explicitNoDc = true;
            if (!source.isValid())
                return Utils::ResultError(Tr::tr("Lower adapter bridge is incomplete."));
            deviceSources->append(source);
        }
        return result.isValid()
                   ? Utils::Result<Data::RuntimePackageCompilerProjectProjection>{result}
                   : Utils::Result<Data::RuntimePackageCompilerProjectProjection>{
                         Utils::ResultError(
                             Tr::tr("Project compiler projection is invalid."))};
    }

    ProvisionedRuntimePackageCompilerProjectRequestBuilder *q = nullptr;
    QPointer<Core::ProviderRegistry> registry;
    CurrentTimeNs currentTimeNs;
    std::optional<CompilerInputProvisioningProfile> profile;
    QString error;
};

ProvisionedRuntimePackageCompilerProjectRequestBuilder::
    ProvisionedRuntimePackageCompilerProjectRequestBuilder(
        Core::ProviderRegistry *providerRegistry,
        const Utils::FilePath &inputProvisioningFile,
        QObject *parent,
        CurrentTimeNs currentTimeNs)
    : Core::RuntimePackageCompilerProjectRequestBuilder(
          Utils::Id::fromString(
              QStringLiteral("EtherCAT.ProjectCompiler.ProjectRequestBuilder")),
          Tr::tr("Provisioned project compiler inputs"),
          parent)
    , d(std::make_unique<Private>(
          this, providerRegistry, inputProvisioningFile, std::move(currentTimeNs)))
{}

ProvisionedRuntimePackageCompilerProjectRequestBuilder::
    ~ProvisionedRuntimePackageCompilerProjectRequestBuilder() = default;

QString ProvisionedRuntimePackageCompilerProjectRequestBuilder::provisioningError() const
{
    return d->error;
}

Utils::Result<Core::RuntimePackageCompilerPreparationStartRequest>
ProvisionedRuntimePackageCompilerProjectRequestBuilder::build(
    const Core::RuntimePackageCompilerProjectRequestSeed &seed)
{
    return d->build(seed);
}

} // namespace EtherCAT::ProjectCompiler
