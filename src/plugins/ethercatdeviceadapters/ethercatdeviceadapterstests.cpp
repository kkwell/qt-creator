// Copyright (C) 2026 Kvell

#include "ethercatdeviceadapterstests.h"

#include "adapterpackagerepository.h"

#include <coreplugin/icore.h>

#include <ethercatcore/manualcontrolcontract.h>
#include <ethercatcore/providerregistry.h>

#include <ethercatdata/runtimepackagecompiler.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/filepath.h>

#include <monocypher-ed25519.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace EtherCAT::DeviceAdapters::Internal {

static constexpr Data::DeviceIdentity xb6Identity{0x00884443, 0x000000b6, 0x00000001};
static constexpr Data::DeviceIdentity sv630nIdentity{0x00100000, 0x000c0112, 0x00010000};

static const QByteArray xb6EsiSha256 = QByteArray::fromHex(
    "5b0bfbfffdfde1fd293589deb4a1c59f974aa79dcd9206ac0a695ab00f395bf7");
static const QByteArray sv630nEsiSha256 = QByteArray::fromHex(
    "e6f39fd4e0f8801c83ec3ac796e138fe3ee1566cb94bb28285b93538fdb9e4a1");

static bool writePackage(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(object).toJson()) >= 0;
}

static bool writeBytes(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

static QByteArray compactJsonValue(const QJsonValue &value)
{
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    QJsonArray wrapper;
    wrapper.append(value);
    QByteArray result = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    result.remove(0, 1);
    result.chop(1);
    return result;
}

static QByteArray jsonWithReverseRootKeys(const QJsonObject &object)
{
    QStringList keys = object.keys();
    std::reverse(keys.begin(), keys.end());
    QByteArray result("{");
    for (qsizetype index = 0; index < keys.size(); ++index) {
        if (index != 0)
            result.append(',');
        result.append(compactJsonValue(keys.at(index)));
        result.append(':');
        result.append(compactJsonValue(object.value(keys.at(index))));
    }
    result.append('}');
    return result;
}

struct AuthorizationTestKey
{
    QByteArray secretKey;
    QByteArray publicKey;
    QByteArray keyId;
};

static AuthorizationTestKey authorizationTestKey(char seedByte)
{
    QByteArray seed(32, seedByte);
    AuthorizationTestKey result;
    result.secretKey.resize(64);
    result.publicKey.resize(32);
    crypto_ed25519_key_pair(
        reinterpret_cast<uint8_t *>(result.secretKey.data()),
        reinterpret_cast<uint8_t *>(result.publicKey.data()),
        reinterpret_cast<uint8_t *>(seed.data()));
    result.keyId = QCryptographicHash::hash(result.publicKey, QCryptographicHash::Sha256);
    return result;
}

static QByteArray canonicalAuthorizationJson(const QJsonObject &object)
{
    QByteArray result = QJsonDocument(object).toJson(QJsonDocument::Compact);
    result.append('\n');
    return result;
}

static QByteArray authorizationSignature(
    const QByteArray &canonical, const QByteArray &secretKey, const QByteArray &domain)
{
    QByteArray message = domain;
    message.append('\0');
    message.append(canonical);
    QByteArray signature(64, '\0');
    crypto_ed25519_sign(
        reinterpret_cast<uint8_t *>(signature.data()),
        reinterpret_cast<const uint8_t *>(secretKey.constData()),
        reinterpret_cast<const uint8_t *>(message.constData()),
        size_t(message.size()));
    return signature;
}

static bool writeSignedAuthorizationDocument(
    const QString &jsonPath,
    const QJsonObject &object,
    const AuthorizationTestKey &key,
    const QByteArray &domain,
    const QByteArray &signingDomain = {})
{
    const QByteArray canonical = canonicalAuthorizationJson(object);
    const Data::RuntimePackageCompilerCanonicalJson checked
        = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(canonical);
    QString signaturePath = jsonPath;
    signaturePath.chop(5);
    signaturePath.append(".sig");
    return checked.isValid() && writeBytes(jsonPath, canonical)
           && writeBytes(
               signaturePath,
               authorizationSignature(
                   canonical, key.secretKey, signingDomain.isEmpty() ? domain : signingDomain));
}

static QString adapterQualificationString(Data::DeviceAdapterQualification qualification)
{
    switch (qualification) {
    case Data::DeviceAdapterQualification::Unqualified:
        return "unqualified";
    case Data::DeviceAdapterQualification::Candidate:
        return "candidate";
    case Data::DeviceAdapterQualification::Qualified:
        return "qualified";
    case Data::DeviceAdapterQualification::MockOnly:
        return "mock-only";
    case Data::DeviceAdapterQualification::Revoked:
        return "revoked";
    }
    return {};
}

static QJsonArray authorizationStringArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values)
        result.append(value);
    return result;
}

static QJsonObject authorizationBinding(const Data::DeviceAdapterManifest &manifest)
{
    QJsonObject match;
    match.insert("vendorId", qint64(manifest.match.vendorId));
    match.insert("productCode", qint64(manifest.match.productCode));
    match.insert("minimumRevision", qint64(manifest.match.minimumRevision));
    match.insert("maximumRevision", qint64(manifest.match.maximumRevision));
    match.insert("exactEsiSha256", QString::fromLatin1(manifest.match.exactEsiSha256.toHex()));

    QJsonObject target;
    target.insert("adapterId", manifest.controllerAdapterTarget.adapterId);
    target.insert("adapterVersion", manifest.controllerAdapterTarget.adapterVersion);
    target.insert(
        "adapterSha256",
        QString::fromLatin1(manifest.controllerAdapterTarget.adapterSha256.toHex()));
    target
        .insert("esiSha256", QString::fromLatin1(manifest.controllerAdapterTarget.esiSha256.toHex()));

    QJsonArray profiles;
    for (const Data::ProcessDataProfile &profile : manifest.processDataProfiles) {
        QJsonObject item;
        item.insert("id", profile.id);
        item.insert("signedPdoProfileId", profile.signedPdoProfileId);
        item.insert(
            "signedDcProfileId",
            profile.signedDcProfileId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                : QJsonValue(profile.signedDcProfileId));
        QJsonArray rx;
        for (quint16 value : profile.rxPdoIndices)
            rx.append(value);
        QJsonArray tx;
        for (quint16 value : profile.txPdoIndices)
            tx.append(value);
        QStringList required;
        for (const Data::SemanticSignalId &id : profile.requiredSignals)
            required.append(id.value);
        item.insert("rxPdoIndices", rx);
        item.insert("txPdoIndices", tx);
        item.insert("requiredSignals", authorizationStringArray(required));
        profiles.append(item);
    }

    QJsonArray actions;
    for (const Data::DeviceControlAction &action : manifest.controlActions) {
        QJsonObject item;
        item.insert("id", action.id.value);
        item.insert("enabled", action.enabled);
        item.insert(
            "qualification",
            action.signedQualification == Data::DeviceControlActionQualification::Qualified
                ? QString("qualified")
                : QString("unqualified"));
        item.insert(
            "disabledReason",
            action.disabledReason.isEmpty() ? QJsonValue(QJsonValue::Null)
                                            : QJsonValue(action.disabledReason));
        item.insert("requiresDc", action.requiresDc);
        item.insert(
            "expectedSignedDefinitionSha256",
            QString::fromLatin1(action.expectedSignedDefinitionSha256.toHex()));
        item.insert("signedPdoProfileIds", authorizationStringArray(action.signedPdoProfileIds));
        actions.append(item);
    }

    QJsonObject result;
    result.insert("id", manifest.id.value);
    result.insert("version", manifest.version);
    result.insert("contentSha256", QString::fromLatin1(manifest.contentSha256.toHex()));
    result.insert("qualification", adapterQualificationString(manifest.qualification));
    result.insert("match", match);
    result.insert("controllerAdapterTarget", target);
    result.insert("processDataProfiles", profiles);
    result.insert("actions", actions);
    result.insert("evidenceSha256", QString::fromLatin1(manifest.evidenceSha256.toHex()));
    if (manifest.contractVersion == Data::DeviceAdapterContractVersion::V4) {
        QJsonArray definitions;
        for (const Data::DeviceParameterDefinition &definition : manifest.parameterDefinitions) {
            definitions.append(QJsonObject{
                {"id", definition.id},
                {"definitionSha256", QString::fromLatin1(definition.definitionSha256.toHex())},
            });
        }
        result.insert("schemaVersion", "embed-labs.device-adapter/v4");
        result.insert("parameterDefinitions", definitions);
    }
    return result;
}

static QString packageLoadError(const QJsonObject &object)
{
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()
        || !writePackage(temporaryDirectory.path() + "/package.adapter.json", object)) {
        return QStringLiteral("test package could not be written");
    }
    AdapterPackageRepository repository(Utils::FilePath::fromString(temporaryDirectory.path()));
    return repository.loadErrors().join('\n');
}

static QByteArray packageDigest(const QByteArray &contents, QString *error)
{
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()
        || !writeBytes(temporaryDirectory.path() + "/package.adapter.json", contents)) {
        *error = QStringLiteral("test package could not be written");
        return {};
    }
    AdapterPackageRepository repository(Utils::FilePath::fromString(temporaryDirectory.path()));
    if (!repository.isAvailable() || repository.loadedPackageCount() != 1) {
        *error = repository.loadErrors().join('\n');
        return {};
    }
    error->clear();
    return repository.adapterManifests().constFirst().contentSha256;
}

static QJsonObject v3Fixture()
{
    const QString path = QFINDTESTDATA("testdata/device-adapter-v3-contract.fixture.json");
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

static QJsonObject exactRational(qint64 numerator, qint64 denominator = 1)
{
    return {
        {"numerator", QString::number(numerator)},
        {"denominator", QString::number(denominator)},
    };
}

static QJsonObject integerConstraint(qint64 minimum, quint64 maximum)
{
    return {
        {"minimum", exactRational(minimum)},
        {"maximum",
         QJsonObject{
             {"numerator", QString::number(maximum)},
             {"denominator", "1"},
         }},
        {"step", exactRational(1)},
        {"stepOrigin", exactRational(0)},
        {"enumeration", QJsonArray{}},
    };
}

static QJsonObject parameterObject(
    quint16 index,
    quint8 subIndex,
    const QString &physicalType,
    int bitWidth,
    const QJsonObject &constraint)
{
    return {
        {"index", index},
        {"subIndex", subIndex},
        {"physicalType", physicalType},
        {"bitWidth", bitWidth},
        {"byteOrder", "little-endian"},
        {"engineeringTransform",
         QJsonObject{
             {"scale", exactRational(1)},
             {"offset", exactRational(0)},
             {"rounding", "reject-inexact"},
             {"constraint", constraint},
         }},
    };
}

static QJsonObject deviceParameterDefinition(
    const QString &id,
    const QString &dataType,
    const QString &unit,
    const QJsonObject &constraint,
    const std::optional<QJsonObject> &object = std::nullopt,
    const QString &projectOnlyReason = {})
{
    QJsonObject configured = object.value_or(QJsonObject{});
    configured.insert("kind", object ? "coe-startup-sdo" : "project-only");
    if (object)
        configured.insert("transition", "PS");
    QJsonObject observed = object.value_or(QJsonObject{});
    observed.insert("kind", object ? "coe-sdo-upload" : "unavailable");
    if (!object)
        observed.insert("reason", projectOnlyReason);
    return {
        {"id", id},
        {"displayName", id},
        {"description", "Offline-only Adapter v4 contract fixture."},
        {"dataType", dataType},
        {"unit", unit},
        {"engineeringConstraint", constraint},
        {"required", true},
        {"engineeringDefaultValue", QJsonValue(QJsonValue::Null)},
        {"configuredProjection", configured},
        {"observedSource", observed},
    };
}

static QJsonArray sv630nV4ParameterDefinitions()
{
    const QJsonObject uint32Positive = integerConstraint(1, std::numeric_limits<quint32>::max());
    const QJsonObject uint32NonNegative = integerConstraint(0, std::numeric_limits<quint32>::max());
    const QJsonObject rpmLimit = integerConstraint(0, 6000);
    const QJsonObject stopThreshold = integerConstraint(0, std::numeric_limits<qint32>::max());
    return {
        deviceParameterDefinition(
            "cia402.gear_ratio.motor_revolutions",
            "unsigned-integer",
            "revolution",
            uint32Positive,
            parameterObject(0x6091, 1, "unsigned-integer32", 32, uint32Positive)),
        deviceParameterDefinition(
            "cia402.gear_ratio.shaft_revolutions",
            "unsigned-integer",
            "revolution",
            uint32Positive,
            parameterObject(0x6091, 2, "unsigned-integer32", 32, uint32Positive)),
        deviceParameterDefinition(
            "cia402.max_profile_velocity.reference_units_per_second",
            "unsigned-integer",
            "reference_unit_per_second",
            uint32NonNegative,
            parameterObject(0x607f, 0, "unsigned-integer32", 32, uint32NonNegative)),
        deviceParameterDefinition(
            "inovance.sv630n.speed_limit.positive_rpm",
            "unsigned-integer",
            "rpm",
            rpmLimit,
            parameterObject(0x2006, 9, "unsigned-integer16", 16, rpmLimit)),
        deviceParameterDefinition(
            "inovance.sv630n.speed_limit.reverse_rpm",
            "unsigned-integer",
            "rpm",
            rpmLimit,
            parameterObject(0x2006, 10, "unsigned-integer16", 16, rpmLimit)),
        deviceParameterDefinition(
            "motion.csv.stop_velocity_threshold.reference_units_per_second",
            "signed-integer",
            "reference_unit_per_second",
            stopThreshold,
            std::nullopt,
            "software_policy_not_device_object"),
        deviceParameterDefinition(
            "motor.encoder_resolution_counts_per_revolution",
            "unsigned-integer",
            "count_per_revolution",
            uint32Positive,
            std::nullopt,
            "no_direct_readable_object"),
    };
}

static QJsonObject sv630nV4Fixture()
{
    const Utils::FilePath path = ::Core::ICore::resourcePath(
        "ethercat/adapters/v3/inovance-sv630n-rev00010000.adapter.json");
    const Utils::Result<QByteArray> contents = path.fileContents();
    if (!contents)
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(*contents);
    if (!document.isObject())
        return {};
    QJsonObject result = document.object();
    result.insert("schemaVersion", "embed-labs.device-adapter/v4");
    result.insert("version", "0.4.0-test");
    result.insert("description", "Offline-only SV630N Adapter v4 contract fixture.");
    result.insert("parameterDefinitions", sv630nV4ParameterDefinitions());
    return result;
}

static QJsonObject installedSv630nV4Manifest()
{
    const Utils::FilePath path = ::Core::ICore::resourcePath(
        "ethercat/adapters/v4/inovance-sv630n-rev00010000.adapter.json");
    const Utils::Result<QByteArray> contents = path.fileContents();
    if (!contents)
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(*contents);
    return document.isObject() ? document.object() : QJsonObject{};
}

static Utils::FilePath adaptersRoot()
{
    return ::Core::ICore::resourcePath("ethercat/adapters");
}

static const Data::DeviceAdapterManifest *manifestForIdentity(
    const QList<Data::DeviceAdapterManifest> &manifests,
    const Data::DeviceIdentity &identity,
    const QString &version = {})
{
    const auto found = std::find_if(
        manifests.cbegin(),
        manifests.cend(),
        [&identity, &version](const Data::DeviceAdapterManifest &item) {
            return item.match.vendorId == identity.vendorId
                   && item.match.productCode == identity.productCode
                   && item.match.minimumRevision == identity.revisionNumber
                   && item.match.maximumRevision == identity.revisionNumber
                   && (version.isEmpty() || item.version == version);
        });
    return found == manifests.cend() ? nullptr : &*found;
}

static Data::DeviceDescription device(
    const Data::DeviceIdentity &identity, const QByteArray &esiSha256)
{
    Data::DeviceDescription result;
    result.summary.id = Data::NodeId::create();
    result.summary.identity = identity;
    result.sourceSha256 = esiSha256;
    return result;
}

static Data::ProcessImageEntry processEntry(
    Data::PdoDirection direction,
    quint16 pdoIndex,
    quint16 objectIndex,
    quint8 objectSubIndex,
    qint64 bitOffset,
    int bitLength,
    Data::EtherCATDataType dataType)
{
    Data::ProcessImageEntry result;
    result.pdoId = Data::NodeId::create();
    result.entryId = Data::NodeId::create();
    result.syncManagerId = Data::NodeId::create();
    result.pdoIndex = pdoIndex;
    result.index = objectIndex;
    result.subIndex = objectSubIndex;
    result.direction = direction;
    result.syncManager = direction == Data::PdoDirection::Rx ? 2 : 3;
    result.bitOffset = bitOffset;
    result.bitLength = bitLength;
    result.byteOffset = bitOffset / 8;
    result.bitOffsetInByte = int(bitOffset % 8);
    result.dataType = dataType;
    return result;
}

static void appendProcessEntry(
    Data::ProcessImagePreview *preview, const Data::ProcessImageEntry &entry)
{
    Data::ProcessImageDirection *direction = entry.direction == Data::PdoDirection::Rx
                                                 ? &preview->outputs
                                                 : &preview->inputs;
    direction->entries.append(entry);
    direction->bitSize = qMax(direction->bitSize, entry.bitOffset + entry.bitLength);
    direction->byteSize = (direction->bitSize + 7) / 8;
}

static Data::ProcessImagePreview sv630nVelocityProcessImage()
{
    Data::ProcessImagePreview result;

    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx,
            0x1702,
            0x6040,
            0,
            0,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx, 0x1702, 0x607a, 0, 16, 32, Data::EtherCATDataType::Integer32));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx, 0x1702, 0x60ff, 0, 48, 32, Data::EtherCATDataType::Integer32));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx, 0x1702, 0x6071, 0, 80, 16, Data::EtherCATDataType::Integer16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx, 0x1702, 0x6060, 0, 96, 8, Data::EtherCATDataType::Integer8));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx,
            0x1702,
            0x60b8,
            0,
            104,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Rx,
            0x1702,
            0x607f,
            0,
            120,
            32,
            Data::EtherCATDataType::UnsignedInteger32));

    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx,
            0x1b04,
            0x603f,
            0,
            0,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx,
            0x1b04,
            0x6041,
            0,
            16,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x6064, 0, 32, 32, Data::EtherCATDataType::Integer32));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x6077, 0, 64, 16, Data::EtherCATDataType::Integer16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x6061, 0, 80, 8, Data::EtherCATDataType::Integer8));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x60f4, 0, 88, 32, Data::EtherCATDataType::Integer32));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx,
            0x1b04,
            0x60b9,
            0,
            120,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x60ba, 0, 136, 32, Data::EtherCATDataType::Integer32));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x60bc, 0, 168, 32, Data::EtherCATDataType::Integer32));
    appendProcessEntry(
        &result,
        processEntry(
            Data::PdoDirection::Tx, 0x1b04, 0x606c, 0, 200, 32, Data::EtherCATDataType::Integer32));
    return result;
}

static Data::DeviceAdapterResolutionRequest sv630nApi038Request(
    const Data::DeviceAdapterManifest &manifest)
{
    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device = device(sv630nIdentity, sv630nEsiSha256);
    request.allowCandidate = true;
    if (manifest.processDataProfiles.isEmpty())
        return request;

    const Data::ProcessDataProfile &profile = manifest.processDataProfiles.constFirst();
    request.processDataProfileId = profile.id;
    qint64 outputBitOffset = 0;
    qint64 inputBitOffset = 0;
    for (const Data::SemanticSignalDefinition &signal : manifest.semanticSignals) {
        for (const Data::DeviceSignalBinding &binding : signal.bindings) {
            if (binding.kind != Data::DeviceSignalBindingKind::ProcessDataObject)
                continue;
            const bool selected = binding.pdoDirection == Data::PdoDirection::Rx
                                      ? profile.rxPdoIndices.contains(binding.pdoIndex)
                                      : profile.txPdoIndices.contains(binding.pdoIndex);
            if (!selected)
                continue;
            qint64 &bitOffset = binding.pdoDirection == Data::PdoDirection::Rx ? outputBitOffset
                                                                               : inputBitOffset;
            appendProcessEntry(
                &request.processImage,
                processEntry(
                    binding.pdoDirection,
                    binding.pdoIndex,
                    binding.objectIndex,
                    binding.objectSubIndex,
                    bitOffset,
                    binding.bitWidth,
                    binding.physicalType));
            bitOffset += binding.bitWidth;
        }
    }
    return request;
}

static Data::DeviceAdapterResolutionRequest sv630nRequest(const Data::DeviceAdapterManifest &manifest)
{
    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device = device(sv630nIdentity, sv630nEsiSha256);
    request.processImage = sv630nVelocityProcessImage();
    const auto profile = std::find_if(
        manifest.processDataProfiles.cbegin(),
        manifest.processDataProfiles.cend(),
        [](const Data::ProcessDataProfile &item) {
            return item.rxPdoIndices == QList<quint16>{0x1702}
                   && item.txPdoIndices == QList<quint16>{0x1b04};
        });
    if (profile != manifest.processDataProfiles.cend())
        request.processDataProfileId = profile->id;
    request.allowCandidate = true;
    return request;
}

static const Data::SemanticSignalDefinition *signalForObject(
    const Data::DeviceAdapterManifest &manifest,
    Data::PdoDirection direction,
    quint16 pdoIndex,
    quint16 objectIndex)
{
    for (const Data::SemanticSignalDefinition &signal : manifest.semanticSignals) {
        const bool found = std::any_of(
            signal.bindings.cbegin(),
            signal.bindings.cend(),
            [direction, pdoIndex, objectIndex](const Data::DeviceSignalBinding &binding) {
                return binding.kind == Data::DeviceSignalBindingKind::ProcessDataObject
                       && binding.pdoDirection == direction && binding.pdoIndex == pdoIndex
                       && binding.objectIndex == objectIndex;
            });
        if (found)
            return &signal;
    }
    return nullptr;
}

static const Data::BoundSemanticSignal *boundSignalForObject(
    const Data::ResolvedDeviceModel &model,
    Data::PdoDirection direction,
    quint16 pdoIndex,
    quint16 objectIndex)
{
    const auto found = std::find_if(
        model.boundSignals.cbegin(),
        model.boundSignals.cend(),
        [direction, pdoIndex, objectIndex](const Data::BoundSemanticSignal &signal) {
            return signal.binding.pdoDirection == direction && signal.binding.pdoIndex == pdoIndex
                   && signal.binding.objectIndex == objectIndex;
        });
    return found == model.boundSignals.cend() ? nullptr : &*found;
}

void EtherCATDeviceAdaptersTests::testBundledResourcesAndManifests()
{
    const Utils::FilePath packageRoot = adaptersRoot();
    AdapterPackageRepository repository(packageRoot);
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    QCOMPARE(repository.packageRoot(), packageRoot);
    QCOMPARE(repository.loadedPackageCount(), 7);
    QVERIFY(repository.loadErrors().isEmpty());

    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    QCOMPARE(manifests.size(), 7);
    const Data::DeviceAdapterManifest *xb6 = manifestForIdentity(manifests, xb6Identity, "0.3.1");
    const Data::DeviceAdapterManifest *sv630n
        = manifestForIdentity(manifests, sv630nIdentity, "0.4.0");
    QVERIFY(xb6);
    QVERIFY(sv630n);
    QCOMPARE(xb6->contractVersion, Data::DeviceAdapterContractVersion::V3);
    QCOMPARE(sv630n->contractVersion, Data::DeviceAdapterContractVersion::V4);
    QCOMPARE(xb6->version, QString("0.3.1"));
    QCOMPARE(sv630n->version, QString("0.4.0"));
    QCOMPARE(xb6->match.exactEsiSha256, xb6EsiSha256);
    QCOMPARE(sv630n->match.exactEsiSha256, sv630nEsiSha256);

    const struct ExpectedEsi
    {
        QString fileName;
        QByteArray sha256;
        QByteArray identityText;
    } expectedEsi[] = {
        {"EcatTerminal-XB6_V3.22_ENUM.xml", xb6EsiSha256, "#x000B6"},
        {"INOVANCE_SV630N_1Axis_V16.xml", sv630nEsiSha256, "#x000C0112"},
    };
    for (const ExpectedEsi &expected : expectedEsi) {
        const Utils::FilePath path
            = ::Core::ICore::resourcePath("ethercat/esi").pathAppended(expected.fileName);
        const Utils::Result<QByteArray> contents = path.fileContents();
        QVERIFY_RESULT(contents);
        QCOMPARE(QCryptographicHash::hash(*contents, QCryptographicHash::Sha256), expected.sha256);
        QVERIFY(contents->contains(expected.identityText));
    }
}

void EtherCATDeviceAdaptersTests::testInvalidPackagesAreRejected()
{
    const Utils::FilePath bundledPath = ::Core::ICore::resourcePath(
        "ethercat/adapters/v1/inovance-sv630n-rev00010000.adapter.json");
    const Utils::Result<QByteArray> bundledContents = bundledPath.fileContents();
    QVERIFY_RESULT(bundledContents);
    const QJsonDocument bundledDocument = QJsonDocument::fromJson(*bundledContents);
    QVERIFY(bundledDocument.isObject());

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString root = temporaryDirectory.path();
    const QJsonObject valid = bundledDocument.object();
    QVERIFY(writePackage(root + "/a-valid.adapter.json", valid));
    QVERIFY(writePackage(root + "/b-duplicate.adapter.json", valid));

    QJsonObject unknownField = valid;
    unknownField.insert("unexpectedField", true);
    QVERIFY(writePackage(root + "/c-unknown-field.adapter.json", unknownField));

    QJsonObject wrongSchema = valid;
    wrongSchema.insert("schemaVersion", "embed-labs.device-adapter/v5");
    QVERIFY(writePackage(root + "/d-wrong-schema.adapter.json", wrongSchema));

    QJsonObject upperCaseHash = valid;
    QJsonObject upperCaseMatch = upperCaseHash.value("match").toObject();
    upperCaseMatch
        .insert("exactEsiSha256", upperCaseMatch.value("exactEsiSha256").toString().toUpper());
    upperCaseHash.insert("match", upperCaseMatch);
    QVERIFY(writePackage(root + "/e-upper-case-hash.adapter.json", upperCaseHash));

    QJsonObject zeroTtlAction = valid;
    zeroTtlAction.insert("qualification", "qualified");
    zeroTtlAction.insert("signatureVerified", false);
    zeroTtlAction.insert("realHardwareAllowed", false);
    QJsonArray actions = zeroTtlAction.value("controlActions").toArray();
    QVERIFY(!actions.isEmpty());
    QJsonObject enabledAction = actions.at(0).toObject();
    enabledAction.insert("enabled", true);
    enabledAction.insert("commandTtlMs", 0);
    actions.replace(0, enabledAction);
    zeroTtlAction.insert("controlActions", actions);
    QVERIFY(writePackage(root + "/f-zero-ttl-action.adapter.json", zeroTtlAction));

    QJsonObject forgedTrust = valid;
    forgedTrust.insert("qualification", "qualified");
    forgedTrust.insert("signatureVerified", true);
    forgedTrust.insert("realHardwareAllowed", true);
    QVERIFY(writePackage(root + "/g-forged-trust.adapter.json", forgedTrust));

    QJsonObject mismatchedSource = valid;
    QJsonObject source = mismatchedSource.value("source").toObject();
    source.insert("sha256", QString(64, QLatin1Char('0')));
    mismatchedSource.insert("source", source);
    QVERIFY(writePackage(root + "/h-mismatched-source.adapter.json", mismatchedSource));

    AdapterPackageRepository repository(Utils::FilePath::fromString(root));
    QVERIFY(!repository.isAvailable());
    QCOMPARE(repository.loadedPackageCount(), 1);
    const QString errors = repository.loadErrors().join('\n');
    QVERIFY(errors.contains("duplicate adapter id and version"));
    QVERIFY(errors.contains("unknown field \"unexpectedField\""));
    QVERIFY(errors.contains("schemaVersion"));
    QVERIFY(errors.contains("SHA-256"));
    QVERIFY(errors.contains("non-zero command TTL"));
    QVERIFY(errors.contains("embedded trust assertions"));
    QVERIFY(errors.contains("source SHA-256 does not match"));
}

void EtherCATDeviceAdaptersTests::testBundledV2ExactContracts()
{
    AdapterPackageRepository repository(adaptersRoot());
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *xb6 = manifestForIdentity(manifests, xb6Identity, "0.2.0");
    const Data::DeviceAdapterManifest *sv630n
        = manifestForIdentity(manifests, sv630nIdentity, "0.2.0");
    QVERIFY(xb6);
    QVERIFY(sv630n);
    QCOMPARE(xb6->contractVersion, Data::DeviceAdapterContractVersion::V2);
    QCOMPARE(sv630n->contractVersion, Data::DeviceAdapterContractVersion::V2);
    QCOMPARE(xb6->version, QString("0.2.0"));
    QCOMPARE(sv630n->version, QString("0.2.0"));
    QCOMPARE(
        xb6->contentSha256,
        QByteArray::fromHex("ec6ea39eaa5f7a12832f9cfeab4c3b29f66ffa004abe83f070772123d33c44d4"));
    QCOMPARE(
        sv630n->contentSha256,
        QByteArray::fromHex("7fc445d4372799cce8f0cdf68fa71cad9ca2a8ea47e0a0b0dffd5aff02a3fb64"));

    for (const Data::DeviceAdapterManifest *manifest : {xb6, sv630n}) {
        QCOMPARE(manifest->qualification, Data::DeviceAdapterQualification::Candidate);
        QVERIFY(!manifest->signatureVerified);
        QVERIFY(!manifest->realHardwareAllowed);
        QVERIFY(
            std::all_of(
                manifest->processDataProfiles.cbegin(),
                manifest->processDataProfiles.cend(),
                [](const Data::ProcessDataProfile &profile) {
                    return profile.signedDcProfileId.isEmpty();
                }));
        QVERIFY(
            std::all_of(
                manifest->controlActions.cbegin(),
                manifest->controlActions.cend(),
                [](const Data::DeviceControlAction &action) { return !action.enabled; }));
    }

    for (const Data::SemanticSignalDefinition &signal : xb6->semanticSignals) {
        QCOMPARE(signal.exposure, Data::SemanticSignalExposure::Internal);
        QVERIFY(signal.engineeringTransform);
        QVERIFY(!signal.hasSafeValue);
        QVERIFY(!signal.manualControl.allowed);
    }
    for (const Data::DeviceModuleProfile &module : xb6->moduleProfiles) {
        for (const Data::SemanticSignalDefinition &signal : module.slotRelativeSignals) {
            QCOMPARE(signal.exposure, Data::SemanticSignalExposure::Public);
            QVERIFY(signal.engineeringTransform);
            QCOMPARE(signal.engineeringTransform->scale, (Data::ExactRational{1, 1}));
            QCOMPARE(signal.engineeringTransform->offset, (Data::ExactRational{0, 1}));
            QVERIFY(signal.engineeringTransform->rounding);
            QCOMPARE(*signal.engineeringTransform->rounding, Data::EngineeringRounding::RejectInexact);
            QVERIFY(signal.engineeringTransform->constraint.minimum);
            QVERIFY(signal.engineeringTransform->constraint.maximum);
            QCOMPARE(*signal.engineeringTransform->constraint.minimum, (Data::ExactRational{0, 1}));
            QCOMPARE(*signal.engineeringTransform->constraint.maximum, (Data::ExactRational{1, 1}));
            QVERIFY(!signal.hasSafeValue);
            QVERIFY(!signal.manualControl.allowed);
        }
    }

    for (const Data::SemanticSignalDefinition &signal : sv630n->semanticSignals) {
        QVERIFY(signal.engineeringTransform);
        QCOMPARE(
            signal.exposure,
            signal.direction == Data::SemanticSignalDirection::Input
                ? Data::SemanticSignalExposure::Public
                : Data::SemanticSignalExposure::ActionOnly);
        QVERIFY(!signal.engineeringTransform->unit.contains("rpm", Qt::CaseInsensitive));
        QVERIFY(!signal.hasSafeValue);
        QVERIFY(!signal.manualControl.allowed);
    }
    const Data::SemanticSignalDefinition *targetVelocity
        = signalForObject(*sv630n, Data::PdoDirection::Rx, 0x1702, 0x60ff);
    QVERIFY(targetVelocity);
    QCOMPARE(targetVelocity->engineeringTransform->unit, QString("raw-drive-unit"));

    const auto stop = std::find_if(
        sv630n->controlActions.cbegin(),
        sv630n->controlActions.cend(),
        [](const Data::DeviceControlAction &action) {
            return action.id.value.endsWith(".action.stop-csv");
        });
    QVERIFY(stop != sv630n->controlActions.cend());
    QVERIFY(stop->allowedReleaseActionIds.isEmpty());
    QVERIFY(stop->allowedTimeoutActionIds.isEmpty());
    QVERIFY(stop->allowedFailureActionIds.isEmpty());
    QCOMPARE(stop->parameters.size(), 1);
    QVERIFY(stop->parameters.constFirst().engineeringConstraint);
    QVERIFY(!stop->parameters.constFirst().engineeringDefaultValue);

    for (const Data::DeviceControlAction &action : sv630n->controlActions) {
        for (const Data::DeviceControlStep &step : action.steps) {
            if (step.value.source == Data::DeviceControlValueSource::Literal)
                QVERIFY(step.value.engineeringLiteralValue);
            if (step.mask.source == Data::DeviceControlValueSource::Literal)
                QVERIFY(step.mask.engineeringLiteralValue);
            QVERIFY(!step.value.literalValue.isValid());
            QVERIFY(!step.mask.literalValue.isValid());
        }
    }

    const Data::DeviceControlAction &prepare = sv630n->controlActions.constFirst();
    Data::ManualActionEnvelope actionEnvelope;
    actionEnvelope.actionId = prepare.id;
    actionEnvelope.holdToRun = prepare.holdToRun;
    Data::ManualControlEnvelope envelope;
    envelope.actionEnvelopes.append(actionEnvelope);
    const Core::ManualControlContractValidation validation = Core::validateManualControlEnvelope(
        envelope, sv630n->semanticSignals, sv630n->controlActions);
    QCOMPARE(validation.error, Core::ManualControlContractError::MissingFallback);
}

void EtherCATDeviceAdaptersTests::testBundledV3Api038Contracts()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v3"));
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    QCOMPARE(repository.loadedPackageCount(), 2);

    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *xb6 = manifestForIdentity(manifests, xb6Identity, "0.3.1");
    const Data::DeviceAdapterManifest *sv630n
        = manifestForIdentity(manifests, sv630nIdentity, "0.3.0");
    QVERIFY(xb6);
    QVERIFY(sv630n);
    QCOMPARE(xb6->contractVersion, Data::DeviceAdapterContractVersion::V3);
    QCOMPARE(sv630n->contractVersion, Data::DeviceAdapterContractVersion::V3);
    QCOMPARE(
        xb6->contentSha256,
        QByteArray::fromHex("1b95083165cf124054069bd9897837d09bc414e8f375bbd77a0f9ddc1bcfa84c"));
    QCOMPARE(
        sv630n->contentSha256,
        QByteArray::fromHex("acf63ee7c837cf39e87fa510aeb8aadfac89b393d04b6cf28926037499a825b9"));

    QCOMPARE(xb6->controllerAdapterTarget.adapterId, QString("solidot.xb6_ec0002_rev1_do16"));
    QCOMPARE(xb6->controllerAdapterTarget.adapterVersion, QString("1.3.0"));
    QCOMPARE(
        xb6->controllerAdapterTarget.adapterSha256,
        QByteArray::fromHex("1b33847b728585760471384cc6dec4273c7c1fb739e001a0b848229b935e3c42"));
    QCOMPARE(xb6->controllerAdapterTarget.esiSha256, xb6EsiSha256);
    QCOMPARE(xb6->semanticSignals.size(), 18);
    QCOMPARE(xb6->processDataProfiles.size(), 1);
    QCOMPARE(xb6->processDataProfiles.constFirst().signedPdoProfileId, QString("do16"));
    QVERIFY(xb6->processDataProfiles.constFirst().signedDcProfileId.isEmpty());
    QCOMPARE(xb6->controlActions.size(), 2);
    QVERIFY(!xb6->signatureVerified);
    QVERIFY(!xb6->realHardwareAllowed);

    QStringList xb6OutputIds;
    for (const Data::SemanticSignalDefinition &signal : xb6->semanticSignals) {
        if (signal.id.value.contains(".digital-output.channel.")) {
            xb6OutputIds.append(signal.id.value);
            QCOMPARE(signal.direction, Data::SemanticSignalDirection::Output);
            QCOMPARE(signal.access, Data::SemanticSignalAccess::ReadWrite);
            QVERIFY(signal.engineeringSafeValue);
            QCOMPARE(signal.engineeringSafeValue->kind, Data::EngineeringValueKind::Boolean);
            QVERIFY(!signal.engineeringSafeValue->boolean);
        }
    }
    QCOMPARE(xb6OutputIds.size(), 16);
    QVERIFY(xb6OutputIds.contains("org.embedlabs.solidot.xb6.slot.1.digital-output.channel.0"));
    QVERIFY(xb6OutputIds.contains("org.embedlabs.solidot.xb6.slot.1.digital-output.channel.15"));
    QVERIFY(!xb6OutputIds.contains("org.embedlabs.solidot.xb6.slot.1.digital-output.channel.16"));

    const QHash<QString, QByteArray> xb6ActionDigests{
        {"org.embedlabs.solidot.xb6.action.clear-digital-outputs",
         QByteArray::fromHex("4e6e1ac0ca79032980f5aab89d8eb7f14905123145b87987c58bdc1a908b4b21")},
        {"org.embedlabs.solidot.xb6.action.set-digital-outputs",
         QByteArray::fromHex("d7eae9ade36e0d28fde08b36511966072a63a99a5ebb089f50fdfb3d799c72bd")},
    };
    for (const Data::DeviceControlAction &action : xb6->controlActions) {
        QVERIFY(action.enabled);
        QCOMPARE(action.signedQualification, Data::DeviceControlActionQualification::Qualified);
        QCOMPARE(action.expectedSignedDefinitionSha256, xb6ActionDigests.value(action.id.value));
        QCOMPARE(action.signedPdoProfileIds, QStringList{"do16"});
        QCOMPARE(action.consistencyGroups.size(), 1);
        QCOMPARE(action.consistencyGroups.constFirst().id, QString("manual_do16"));
        QCOMPARE(action.consistencyGroups.constFirst().members.size(), 16);
        QCOMPARE(
            action.consistencyGroups.constFirst().recovery,
            Data::DeviceControlGroupRecovery::HoldSafe);
        QCOMPARE(action.consistencyGroups.constFirst().maximumTtlCycles, quint32(1000));
        QCOMPARE(
            action.failureDisposition, Data::DeviceControlFailureDisposition::HoldOperationalFault);
    }

    QCOMPARE(
        sv630n->controllerAdapterTarget.adapterId, QString("inovance.sv630n_1axis_rev00010000_csp"));
    QCOMPARE(sv630n->controllerAdapterTarget.adapterVersion, QString("1.5.0"));
    QCOMPARE(
        sv630n->controllerAdapterTarget.adapterSha256,
        QByteArray::fromHex("f5b8d1d579b9804927d9749b8ca61e18f4701ab5be75626aeda7459b9a9db99e"));
    QCOMPARE(sv630n->controllerAdapterTarget.esiSha256, sv630nEsiSha256);
    QCOMPARE(sv630n->semanticSignals.size(), 19);
    QCOMPARE(sv630n->processDataProfiles.size(), 1);
    QCOMPARE(sv630n->processDataProfiles.constFirst().signedPdoProfileId, QString("csp_1704_1b04"));
    QCOMPARE(sv630n->processDataProfiles.constFirst().signedDcProfileId, QString("sync0_125us"));
    QCOMPARE(sv630n->processDataProfiles.constFirst().rxPdoIndices, QList<quint16>{0x1704});
    QCOMPARE(sv630n->processDataProfiles.constFirst().txPdoIndices, QList<quint16>{0x1b04});
    QCOMPARE(sv630n->controlActions.size(), 3);
    QVERIFY(!sv630n->signatureVerified);
    QVERIFY(!sv630n->realHardwareAllowed);

    const QHash<QString, QByteArray> svActionDigests{
        {"org.embedlabs.inovance.sv630n.action.prepare-csv",
         QByteArray::fromHex("4a8bc220a783e600c25b9842802678712d8f4e3a0eb0a1948f295145105551bd")},
        {"org.embedlabs.inovance.sv630n.action.set-csv-velocity",
         QByteArray::fromHex("1355c4d05de03d96f8064f60706e7eb7f0fb381caa715b5dc3f607164114a90a")},
        {"org.embedlabs.inovance.sv630n.action.stop-csv",
         QByteArray::fromHex("df27cc25c14dfb4b6aa435d4ab5d853e051f2e754e3c16093925f5ccf69393d3")},
    };
    for (const Data::DeviceControlAction &action : sv630n->controlActions) {
        QVERIFY(!action.enabled);
        QCOMPARE(action.signedQualification, Data::DeviceControlActionQualification::Unqualified);
        QCOMPARE(action.disabledReason, QString("reference_unit_to_rpm_conversion_not_bound"));
        QVERIFY(action.requiresDc);
        QCOMPARE(action.expectedSignedDefinitionSha256, svActionDigests.value(action.id.value));
        QCOMPARE(action.signedPdoProfileIds, QStringList{"csp_1704_1b04"});
        QCOMPARE(action.consistencyGroups.size(), 1);
        QCOMPARE(action.consistencyGroups.constFirst().id, QString("manual_velocity"));
        QCOMPARE(action.consistencyGroups.constFirst().members.size(), 3);
        QCOMPARE(
            action.consistencyGroups.constFirst().recovery,
            Data::DeviceControlGroupRecovery::HoldSafe);
        QCOMPARE(action.consistencyGroups.constFirst().maximumTtlCycles, quint32(1000));
    }
}

void EtherCATDeviceAdaptersTests::testInstalledProductionAuthorizations()
{
    AdapterPackageRepository repository(
        adaptersRoot(),
        ::Core::ICore::resourcePath("ethercat/adapter-authorizations"),
        ::Core::ICore::resourcePath("ethercat/adapter-authorization-trust"));

    QVERIFY(repository.isAvailable());
    QVERIFY2(repository.loadErrors().isEmpty(), qPrintable(repository.loadErrors().join('\n')));
    QCOMPARE(repository.loadedPackageCount(), 7);
    QVERIFY2(
        repository.authorizationDiagnostics().isEmpty(),
        qPrintable(repository.authorizationDiagnostics().join('\n')));
    QCOMPARE(repository.authorizationStatus().state, AdapterAuthorizationState::Authorized);
    QCOMPARE(repository.authorizationStatus().authorizedAdapterCount, 3);
    QCOMPARE(repository.authorizationStatus().validationFailureCount, 0);
    QVERIFY(repository.startupDiagnostics().isEmpty());

    const auto xb6 = repository.adapterManifest(
        {"org.embedlabs.adapter.solidot.xb6-ec0002.rev1"}, "0.3.1");
    const auto sv630nV3 = repository.adapterManifest(
        {"org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"}, "0.3.0");
    const auto sv630nV4 = repository.adapterManifest(
        {"org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"}, "0.4.0");
    QVERIFY(xb6);
    QVERIFY(sv630nV3);
    QVERIFY(sv630nV4);
    QVERIFY(xb6->signatureVerified);
    QVERIFY(xb6->realHardwareAllowed);
    QVERIFY(sv630nV3->signatureVerified);
    QVERIFY(sv630nV3->realHardwareAllowed);
    QVERIFY(sv630nV4->signatureVerified);
    QVERIFY(sv630nV4->realHardwareAllowed);
    auto *source
        = qobject_cast<Core::DeviceAdapterAuthorizationProvenanceSource *>(&repository);
    QVERIFY(source);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot snapshot
        = source->authorizationProvenanceSnapshot();
    QVERIFY(snapshot.isValid());
    QCOMPARE(
        snapshot.state, Data::DeviceAdapterAuthorizationProvenanceState::Authorized);
    QCOMPARE(snapshot.records.size(), 3);
    QVERIFY(source->validateCurrent(snapshot));
    QVERIFY(source->validateCurrent(snapshot, *xb6));
    QVERIFY(source->validateCurrent(snapshot, *sv630nV3));
    QVERIFY(source->validateCurrent(snapshot, *sv630nV4));
}

void EtherCATDeviceAdaptersTests::testInstalledApi075ProductionV4()
{
    AdapterPackageRepository repository(
        adaptersRoot(),
        ::Core::ICore::resourcePath("ethercat/adapter-authorizations"),
        ::Core::ICore::resourcePath("ethercat/adapter-authorization-trust"));
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    QVERIFY2(
        repository.authorizationDiagnostics().isEmpty(),
        qPrintable(repository.authorizationDiagnostics().join('\n')));

    const auto manifest = repository.adapterManifest(
        {"org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"}, "0.4.0");
    QVERIFY(manifest);
    QCOMPARE(manifest->contractVersion, Data::DeviceAdapterContractVersion::V4);
    QCOMPARE(
        manifest->contentSha256,
        QByteArray::fromHex("4524bb0a9b796f580c6012a95c1c77257f5afa31c0851cb65595295bf1f2c3bc"));
    QVERIFY(manifest->signatureVerified);
    QVERIFY(manifest->realHardwareAllowed);

    static constexpr std::array<std::pair<const char *, const char *>, 7> expectedDefinitions{{
        {"cia402.gear_ratio.motor_revolutions",
         "0c7e0c701e2c62f137d23388fa7681b96812d3c006b4a203041339fb8824662c"},
        {"cia402.gear_ratio.shaft_revolutions",
         "a6dab16a98c4b0faa169a0e7901d7506fb44a11ddc57f484a92535208ea7f689"},
        {"cia402.max_profile_velocity.reference_units_per_second",
         "b4928ade1adb41cc2f6476067d211a46caf509e23e14a0fdc5cc2bf55dd5500e"},
        {"inovance.sv630n.speed_limit.positive_rpm",
         "7be8f2b7321d277fb9601201586034e6304edcab39bda07ce3fbf2c55035853b"},
        {"inovance.sv630n.speed_limit.reverse_rpm",
         "f5619085e895fa9dee7d532eafd3c07f4fe816a939ce1e964f6457dfa5549ba8"},
        {"motion.csv.stop_velocity_threshold.reference_units_per_second",
         "e17e32f201b0821fe5a2b90a8e6f70a4ba391969bfbcabc8fcb599e9e37acf8c"},
        {"motor.encoder_resolution_counts_per_revolution",
         "d9d8d45a3be389585857354a36395f546d3e93c3f1ce6a00b7e43c6600ce3201"},
    }};
    QCOMPARE(manifest->parameterDefinitions.size(), qsizetype(expectedDefinitions.size()));
    for (qsizetype index = 0; index < manifest->parameterDefinitions.size(); ++index) {
        const Data::DeviceParameterDefinition &definition = manifest->parameterDefinitions.at(index);
        const auto &expected = expectedDefinitions.at(size_t(index));
        QCOMPARE(definition.id, QString::fromLatin1(expected.first));
        QCOMPARE(definition.definitionSha256, QByteArray::fromHex(expected.second));
        QVERIFY(definition.required);
        QVERIFY(!definition.engineeringDefaultValue);
    }

    const auto objectProjectionMatches = [](const Data::DeviceParameterDefinition &definition,
                                            quint16 index,
                                            quint8 subIndex,
                                            Data::EtherCATDataType physicalType) {
        return definition.configuredProjection.kind
                   == Data::DeviceParameterProjectionKind::CoeStartupSdo
               && definition.configuredProjection.transition == "PS"
               && definition.configuredProjection.object
               && definition.configuredProjection.object->index == index
               && definition.configuredProjection.object->subIndex == subIndex
               && definition.configuredProjection.object->physicalType == physicalType
               && definition.observedSource.kind
                      == Data::DeviceParameterObservedSourceKind::CoeSdoUpload
               && definition.observedSource.object
               && *definition.observedSource.object == *definition.configuredProjection.object;
    };
    QVERIFY(objectProjectionMatches(
        manifest->parameterDefinitions.at(0), 0x6091, 1, Data::EtherCATDataType::UnsignedInteger32));
    QVERIFY(objectProjectionMatches(
        manifest->parameterDefinitions.at(1), 0x6091, 2, Data::EtherCATDataType::UnsignedInteger32));
    QVERIFY(objectProjectionMatches(
        manifest->parameterDefinitions.at(2), 0x607f, 0, Data::EtherCATDataType::UnsignedInteger32));
    QVERIFY(objectProjectionMatches(
        manifest->parameterDefinitions.at(3), 0x2006, 9, Data::EtherCATDataType::UnsignedInteger16));
    QVERIFY(objectProjectionMatches(
        manifest->parameterDefinitions.at(4), 0x2006, 10, Data::EtherCATDataType::UnsignedInteger16));
    QCOMPARE(
        manifest->parameterDefinitions.at(5).configuredProjection.kind,
        Data::DeviceParameterProjectionKind::ProjectOnly);
    QCOMPARE(
        manifest->parameterDefinitions.at(5).observedSource.kind,
        Data::DeviceParameterObservedSourceKind::Unavailable);
    QCOMPARE(
        manifest->parameterDefinitions.at(5).observedSource.reason,
        QString("software_policy_not_device_object"));
    QCOMPARE(
        manifest->parameterDefinitions.at(6).configuredProjection.kind,
        Data::DeviceParameterProjectionKind::ProjectOnly);
    QCOMPARE(
        manifest->parameterDefinitions.at(6).observedSource.reason,
        QString("no_direct_readable_object"));

    static constexpr std::array<const char *, 3> expectedActionIds{
        "org.embedlabs.inovance.sv630n.action.prepare-csv",
        "org.embedlabs.inovance.sv630n.action.set-csv-velocity",
        "org.embedlabs.inovance.sv630n.action.stop-csv",
    };
    QCOMPARE(manifest->controlActions.size(), qsizetype(expectedActionIds.size()));
    for (qsizetype index = 0; index < manifest->controlActions.size(); ++index) {
        const Data::DeviceControlAction &action = manifest->controlActions.at(index);
        QCOMPARE(action.id.value, QString::fromLatin1(expectedActionIds.at(size_t(index))));
        QVERIFY(!action.enabled);
        QCOMPARE(action.signedQualification, Data::DeviceControlActionQualification::Unqualified);
        QCOMPARE(action.disabledReason, QString("reference_unit_to_rpm_conversion_not_bound"));
    }

    const auto binding = Data::canonicalDeviceAdapterAuthorizationBinding(
        *manifest, Data::DeviceAdapterAuthorizationVersion::V2);
    QVERIFY(binding);
    QVERIFY(binding->isValid());
    const Utils::Result<QByteArray> authorizationContents
        = ::Core::ICore::resourcePath(
              "ethercat/adapter-authorizations/"
              "embedlabs.adapter-auth.inovance-sv630n.0.4.0.api075.authorization.json")
              .fileContents();
    QVERIFY_RESULT(authorizationContents);
    const QJsonDocument authorizationDocument = QJsonDocument::fromJson(*authorizationContents);
    const QJsonDocument bindingDocument = QJsonDocument::fromJson(binding->exactBytes);
    QVERIFY(authorizationDocument.isObject());
    QVERIFY(bindingDocument.isObject());
    QCOMPARE(bindingDocument.object(), authorizationDocument.object().value("adapter").toObject());

    auto *source = qobject_cast<Core::DeviceAdapterAuthorizationProvenanceSource *>(&repository);
    QVERIFY(source);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot snapshot
        = source->authorizationProvenanceSnapshot();
    QVERIFY(snapshot.isValid());
    QCOMPARE(snapshot.records.size(), 3);
    const auto provenance = std::find_if(
        snapshot.records.cbegin(),
        snapshot.records.cend(),
        [](const Data::DeviceAdapterAuthorizationProvenance &record) {
            return record.authorizationVersion == Data::DeviceAdapterAuthorizationVersion::V2;
        });
    QVERIFY(provenance != snapshot.records.cend());
    QCOMPARE(provenance->adapterContractVersion, Data::DeviceAdapterContractVersion::V4);
    QCOMPARE(provenance->adapterId, manifest->id);
    QCOMPARE(provenance->adapterVersion, QString("0.4.0"));
    QCOMPARE(provenance->adapterContentSha256, manifest->contentSha256);
    QCOMPARE(provenance->adapterBindingSha256, binding->sha256);
    QCOMPARE(
        provenance->authorizationId, QString("embedlabs.adapter-auth.inovance-sv630n.0.4.0.api075"));
    QCOMPARE(provenance->policyId, QString("embedlabs.adapter-authorization.production.api054"));
    QCOMPARE(provenance->policyRevision, quint32(1));
    QCOMPARE(
        provenance->signerKeyId,
        QByteArray::fromHex("017eec68600999eea588857f765c10e740327091ccf0fd9bde86236af3ac5783"));
    QVERIFY(source->validateCurrent(snapshot));
    QVERIFY(source->validateCurrent(snapshot, *manifest));
}

void EtherCATDeviceAdaptersTests::testSignedAdapterAuthorizationProjection()
{
    constexpr char policyDomain[] = "embed-labs.ethercat-device-adapter-authorization-policy/v1";
    constexpr char authorizationDomain[] = "embed-labs.ethercat-device-adapter-authorization/v1";

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString authorizationRoot = temporaryDirectory.path() + "/authorizations";
    const QString trustRoot = temporaryDirectory.path() + "/trust";
    QVERIFY(QDir().mkpath(authorizationRoot + "/policies"));
    QVERIFY(QDir().mkpath(authorizationRoot + "/authorizations"));
    QVERIFY(QDir().mkpath(trustRoot));

    const Utils::FilePath packageRoot = adaptersRoot().pathAppended("v3");
    const Utils::FilePath authorizationRootPath = Utils::FilePath::fromString(authorizationRoot);
    const Utils::FilePath trustRootPath = Utils::FilePath::fromString(trustRoot);
    AdapterPackageRepository unsignedRepository(packageRoot, authorizationRootPath, trustRootPath);
    QVERIFY(unsignedRepository.isAvailable());
    QCOMPARE(unsignedRepository.authorizationDiagnostics(), QStringList{});
    QCOMPARE(unsignedRepository.authorizationStatus().state, AdapterAuthorizationState::NotInstalled);
    QCOMPARE(unsignedRepository.authorizationStatus().authorizedAdapterCount, 0);
    QCOMPARE(unsignedRepository.authorizationStatus().validationFailureCount, 0);
    QVERIFY(adapterAuthorizationStartupMessage(unsignedRepository.authorizationStatus())
                .contains("not installed"));
    const QList<Core::ProviderStartupDiagnostic> unsignedStartupDiagnostics
        = unsignedRepository.startupDiagnostics();
    QCOMPARE(unsignedStartupDiagnostics.size(), 1);
    QCOMPARE(
        unsignedStartupDiagnostics.constFirst().code,
        Utils::Id("EtherCAT.AdapterAuthorization.NotInstalled"));
    QCOMPARE(
        unsignedStartupDiagnostics.constFirst().severity, Core::ProviderDiagnosticSeverity::Warning);
    QCOMPARE(unsignedRepository.loadedPackageCount(), 2);
    const auto unsignedXb6
        = unsignedRepository
              .adapterManifest({"org.embedlabs.adapter.solidot.xb6-ec0002.rev1"}, "0.3.1");
    const auto unsignedSv = unsignedRepository.adapterManifest(
        {"org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"}, "0.3.0");
    QVERIFY(unsignedXb6);
    QVERIFY(unsignedSv);
    QVERIFY(!unsignedXb6->signatureVerified);
    QVERIFY(!unsignedXb6->realHardwareAllowed);
    QVERIFY(!unsignedSv->signatureVerified);
    QVERIFY(!unsignedSv->realHardwareAllowed);
    const QByteArray immutableContentSha256 = unsignedXb6->contentSha256;

    const AuthorizationTestKey rootKey = authorizationTestKey('\x11');
    const AuthorizationTestKey signerKey = authorizationTestKey('\x22');
    const QString rootKeyPath = trustRoot + '/' + QString::fromLatin1(rootKey.keyId.toHex())
                                + ".pub";
    QVERIFY(writeBytes(rootKeyPath, rootKey.publicKey));

    const QString policyPath = authorizationRoot + "/policies/production.policy.json";
    const QString authorizationPath = authorizationRoot + "/authorizations/xb6.authorization.json";
    const QString duplicateAuthorizationPath = authorizationRoot
                                               + "/authorizations/xb6-copy.authorization.json";

    const auto policy = [&](quint32 revision,
                            const QStringList &revokedSignerIds = {},
                            const QStringList &revokedAuthorizationIds = {}) {
        QJsonObject signer;
        signer.insert("keyId", QString::fromLatin1(signerKey.keyId.toHex()));
        signer.insert("publicKey", QString::fromLatin1(signerKey.publicKey.toHex()));
        signer.insert("adapterIds", QJsonArray{unsignedXb6->id.value});
        signer.insert("decisions", QJsonArray{"allow", "deny"});
        QJsonObject result;
        result.insert("format", "embed-labs.ethercat-device-adapter-authorization-policy-v1");
        result.insert("formatVersion", 1);
        result.insert("canonicalization", "kvell-json-ascii-sorted-compact-lf-v1");
        result.insert("policyId", "embed-labs.production-adapter-policy");
        result.insert("policyRevision", qint64(revision));
        result.insert("rootKeyId", QString::fromLatin1(rootKey.keyId.toHex()));
        result.insert("signers", QJsonArray{signer});
        result.insert("revokedSignerKeyIds", authorizationStringArray(revokedSignerIds));
        result.insert("revokedAuthorizationIds", authorizationStringArray(revokedAuthorizationIds));
        return result;
    };
    const auto authorization = [&](quint32 revision, const QString &decision = "allow") {
        QJsonObject result;
        result.insert("format", "embed-labs.ethercat-device-adapter-authorization-v1");
        result.insert("formatVersion", 1);
        result.insert("canonicalization", "kvell-json-ascii-sorted-compact-lf-v1");
        result.insert("authorizationId", "embed-labs.production.xb6.0-3-1");
        result.insert("policyId", "embed-labs.production-adapter-policy");
        result.insert("policyRevision", qint64(revision));
        result.insert("signerKeyId", QString::fromLatin1(signerKey.keyId.toHex()));
        result.insert("decision", decision);
        result.insert("adapter", authorizationBinding(*unsignedXb6));
        return result;
    };
    const auto removeDuplicate = [&] {
        QFile::remove(duplicateAuthorizationPath);
        QString signature = duplicateAuthorizationPath;
        signature.chop(5);
        signature.append(".sig");
        QFile::remove(signature);
    };
    const auto install = [&](const QJsonObject &policyObject,
                             const QJsonObject &authorizationObject,
                             const QByteArray &authorizationSigningDomain = QByteArray{}) {
        removeDuplicate();
        QVERIFY(writeSignedAuthorizationDocument(policyPath, policyObject, rootKey, policyDomain));
        QVERIFY(writeSignedAuthorizationDocument(
            authorizationPath,
            authorizationObject,
            signerKey,
            authorizationDomain,
            authorizationSigningDomain));
    };
    struct Evaluation
    {
        bool available = false;
        bool implementsProvenanceSource = false;
        bool validatesCurrent = false;
        Data::DeviceAdapterManifest xb6;
        Data::DeviceAdapterManifest sv;
        Data::DeviceAdapterAuthorizationProvenanceSnapshot provenance;
        QStringList diagnostics;
        AdapterAuthorizationStatus authorizationStatus;
        QList<Core::ProviderStartupDiagnostic> startupDiagnostics;
    };
    const auto evaluate = [&] {
        AdapterPackageRepository repository(packageRoot, authorizationRootPath, trustRootPath);
        Evaluation result;
        result.available = repository.isAvailable();
        result.diagnostics = repository.authorizationDiagnostics();
        result.authorizationStatus = repository.authorizationStatus();
        result.startupDiagnostics = repository.startupDiagnostics();
        result.xb6 = *repository.adapterManifest(unsignedXb6->id, unsignedXb6->version);
        result.sv = *repository.adapterManifest(unsignedSv->id, unsignedSv->version);
        auto *source
            = qobject_cast<Core::DeviceAdapterAuthorizationProvenanceSource *>(&repository);
        result.implementsProvenanceSource = source;
        if (source) {
            result.provenance = source->authorizationProvenanceSnapshot();
            result.validatesCurrent = source->validateCurrent(result.provenance);
        }
        return result;
    };

    install(policy(1), authorization(1));
    Evaluation result = evaluate();
    QVERIFY(result.available);
    QVERIFY2(result.diagnostics.isEmpty(), qPrintable(result.diagnostics.join('\n')));
    QVERIFY(result.xb6.signatureVerified);
    QVERIFY(result.xb6.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::Authorized);
    QCOMPARE(result.authorizationStatus.authorizedAdapterCount, 1);
    QCOMPARE(result.authorizationStatus.validationFailureCount, 0);
    QVERIFY(result.startupDiagnostics.isEmpty());
    QCOMPARE(result.xb6.contentSha256, immutableContentSha256);
    QVERIFY(!result.sv.signatureVerified);
    QVERIFY(!result.sv.realHardwareAllowed);
    QVERIFY(result.implementsProvenanceSource);
    QVERIFY(result.validatesCurrent);
    QVERIFY(result.provenance.isValid());
    QCOMPARE(
        result.provenance.state,
        Data::DeviceAdapterAuthorizationProvenanceState::Authorized);
    QCOMPARE(result.provenance.records.size(), 1);
    const QByteArray authorizedSetSha256 = result.provenance.authorizationSetSha256;
    const Data::DeviceAdapterAuthorizationProvenance v1Provenance
        = result.provenance.records.constFirst();
    QVERIFY(v1Provenance.isValid());
    QCOMPARE(
        v1Provenance.authorizationVersion, Data::DeviceAdapterAuthorizationVersion::V1);
    QCOMPARE(v1Provenance.adapterContractVersion, Data::DeviceAdapterContractVersion::V3);
    QCOMPARE(v1Provenance.adapterId, result.xb6.id);
    QCOMPARE(v1Provenance.adapterVersion, result.xb6.version);
    QCOMPARE(v1Provenance.adapterContentSha256, result.xb6.contentSha256);
    const auto v1Binding = Data::canonicalDeviceAdapterAuthorizationBinding(
        result.xb6, Data::DeviceAdapterAuthorizationVersion::V1);
    QVERIFY(v1Binding);
    QVERIFY(v1Binding->isValid());
    QCOMPARE(
        v1Binding->exactBytes,
        QJsonDocument(authorizationBinding(result.xb6)).toJson(QJsonDocument::Compact));
    QCOMPARE(v1Provenance.adapterBindingSha256, v1Binding->sha256);
    QVERIFY(!Data::canonicalDeviceAdapterAuthorizationBinding(
        result.xb6, Data::DeviceAdapterAuthorizationVersion::V2));
    Data::DeviceAdapterAuthorizationProvenanceSnapshot forged = result.provenance;
    forged.records[0].authorizationId.append(".forged");
    QVERIFY(forged.isValid());
    AdapterPackageRepository forgedCheckRepository(
        packageRoot, authorizationRootPath, trustRootPath);
    QVERIFY(!forgedCheckRepository.validateCurrent(forged));

    install(policy(1), authorization(1));
    QString authorizationSignaturePath = authorizationPath;
    authorizationSignaturePath.chop(5);
    authorizationSignaturePath.append(".sig");
    QVERIFY(writeBytes(authorizationSignaturePath, QByteArray(64, '\0')));
    result = evaluate();
    QVERIFY(result.available);
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("authorization signature is invalid"));
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidSignature);
    QCOMPARE(result.authorizationStatus.authorizedAdapterCount, 0);
    QCOMPARE(result.authorizationStatus.validationFailureCount, 1);
    QVERIFY(result.provenance.isValid());
    QCOMPARE(
        result.provenance.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(!result.provenance.authorizationSetSha256.isEmpty());
    const QString invalidSignatureMessage = adapterAuthorizationStartupMessage(
        result.authorizationStatus);
    QVERIFY(invalidSignatureMessage.contains("signature check failed"));
    QVERIFY(invalidSignatureMessage.contains("1 issues"));
    QVERIFY(!invalidSignatureMessage.contains(temporaryDirectory.path()));
    QVERIFY(!invalidSignatureMessage.contains("xb6.authorization.json"));
    QCOMPARE(result.startupDiagnostics.size(), 1);
    QCOMPARE(
        result.startupDiagnostics.constFirst().code,
        Utils::Id("EtherCAT.AdapterAuthorization.ValidationFailed"));
    QCOMPARE(result.startupDiagnostics.constFirst().severity, Core::ProviderDiagnosticSeverity::Error);
    QCOMPARE(result.startupDiagnostics.constFirst().message, invalidSignatureMessage);

    install(policy(1), authorization(1));
    const QString invalidExtraAuthorizationPath
        = authorizationRoot + "/authorizations/invalid-extra.authorization.json";
    QVERIFY(writeBytes(invalidExtraAuthorizationPath, QByteArray("not-json\n")));
    result = evaluate();
    QVERIFY(!result.xb6.signatureVerified);
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(!result.sv.signatureVerified);
    QVERIFY(!result.sv.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(result.authorizationStatus.authorizedAdapterCount, 0);
    QFile::remove(invalidExtraAuthorizationPath);

    install(policy(1), authorization(1));
    const QString orphanAuthorizationSignaturePath
        = authorizationRoot + "/authorizations/orphan.authorization.sig";
    QVERIFY(writeBytes(orphanAuthorizationSignaturePath, QByteArray(64, '\0')));
    result = evaluate();
    QVERIFY2(result.diagnostics.isEmpty(), qPrintable(result.diagnostics.join('\n')));
    QVERIFY(!result.xb6.signatureVerified);
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(!result.sv.signatureVerified);
    QVERIFY(!result.sv.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(
        result.authorizationStatus.firstFailure,
        AdapterAuthorizationFailure::IncompleteBundle);
    QCOMPARE(result.authorizationStatus.authorizedAdapterCount, 0);
    QVERIFY(adapterAuthorizationStartupMessage(result.authorizationStatus).contains("incomplete"));
    QCOMPARE(
        result.provenance.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QVERIFY(result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(QFile::remove(orphanAuthorizationSignaturePath));

    install(policy(1), authorization(1));
    QVERIFY(QFile::rename(authorizationSignaturePath, orphanAuthorizationSignaturePath));
    result = evaluate();
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(
        result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidDocument);
    QCOMPARE(
        result.provenance.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QVERIFY(result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(QFile::remove(orphanAuthorizationSignaturePath));

    install(policy(1), authorization(1), policyDomain);
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("authorization signature is invalid"));

    QJsonObject unknown = authorization(1);
    unknown.insert("unknown", true);
    install(policy(1), unknown);
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("invalid field set"));

    install(policy(1), authorization(1));
    const QByteArray nonCanonical = QJsonDocument(authorization(1)).toJson(QJsonDocument::Indented);
    QVERIFY(writeBytes(authorizationPath, nonCanonical));
    QVERIFY(writeBytes(
        authorizationSignaturePath,
        authorizationSignature(nonCanonical, signerKey.secretKey, authorizationDomain)));
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("not kvell-json"));

    install(policy(1), authorization(1));
    QFile trailingAuthorization(authorizationPath);
    QVERIFY(trailingAuthorization.open(QIODevice::Append));
    QCOMPARE(trailingAuthorization.write(" "), qint64(1));
    trailingAuthorization.close();
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("not kvell-json"));

    QJsonObject mismatch = authorization(1);
    QJsonObject mismatchAdapter = mismatch.value("adapter").toObject();
    QJsonObject mismatchMatch = mismatchAdapter.value("match").toObject();
    mismatchMatch.insert("vendorId", qint64(1));
    mismatchAdapter.insert("match", mismatchMatch);
    mismatch.insert("adapter", mismatchAdapter);
    install(policy(1), mismatch);
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(!result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.authorizationSetSha256 != authorizedSetSha256);

    install(policy(1, {}, {"embed-labs.production.xb6.0-3-1"}), authorization(1));
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("revoked"));

    install(policy(1), authorization(1));
    QVERIFY(QFile::copy(authorizationPath, duplicateAuthorizationPath));
    QString duplicateSignaturePath = duplicateAuthorizationPath;
    duplicateSignaturePath.chop(5);
    duplicateSignaturePath.append(".sig");
    QVERIFY(QFile::copy(authorizationSignaturePath, duplicateSignaturePath));
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("duplicate or conflicting"));

    install(policy(1), authorization(1, "deny"));
    result = evaluate();
    QVERIFY(result.available);
    QVERIFY2(result.diagnostics.isEmpty(), qPrintable(result.diagnostics.join('\n')));
    QVERIFY(!result.xb6.signatureVerified);
    QVERIFY(!result.xb6.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::Denied);
    QVERIFY(result.provenance.isValid());
    QCOMPARE(
        result.provenance.state, Data::DeviceAdapterAuthorizationProvenanceState::Denied);
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(!result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.authorizationSetSha256 != authorizedSetSha256);
    QVERIFY(adapterAuthorizationStartupMessage(result.authorizationStatus)
                .contains("installed adapters are not authorized"));
    QCOMPARE(result.startupDiagnostics.size(), 1);
    QCOMPARE(
        result.startupDiagnostics.constFirst().code,
        Utils::Id("EtherCAT.AdapterAuthorization.Denied"));
    QCOMPARE(
        result.startupDiagnostics.constFirst().severity, Core::ProviderDiagnosticSeverity::Warning);

    QJsonObject badKey = authorization(1);
    badKey.insert("signerKeyId", QString::fromLatin1(rootKey.keyId.toHex()));
    install(policy(1), badKey);
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("outside policy"));

    install(policy(1), authorization(1));
    QString policySignaturePath = policyPath;
    policySignaturePath.chop(5);
    policySignaturePath.append(".sig");
    QVERIFY(writeBytes(policySignaturePath, QByteArray(64, '\0')));
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("root policy signature is invalid"));

    install(policy(1), authorization(1));
    const QString linkedNeighborTarget
        = temporaryDirectory.path() + "/linked-neighbor.authorization.json";
    const QString linkedNeighborPath
        = authorizationRoot + "/authorizations/zz-linked-neighbor.authorization.json";
    QVERIFY(QFile::copy(authorizationPath, linkedNeighborTarget));
    QVERIFY(QFile::link(linkedNeighborTarget, linkedNeighborPath));
    QVERIFY(QFileInfo(linkedNeighborPath).isSymLink());
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("symbolic-link authorization-root entries"));
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QVERIFY(result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(QFile::remove(linkedNeighborPath));
    QVERIFY(QFile::remove(linkedNeighborTarget));

    install(policy(1), authorization(1));
    const QString linkedDirectoryTarget = temporaryDirectory.path() + "/linked-material-tree";
    const QString linkedDirectoryPath = authorizationRoot + "/ordinary-neighbor";
    QVERIFY(QDir().mkpath(linkedDirectoryTarget));
    QVERIFY(QFile::copy(
        authorizationPath, linkedDirectoryTarget + "/hidden.authorization.json"));
    QVERIFY(QFile::link(linkedDirectoryTarget, linkedDirectoryPath));
    QVERIFY(QFileInfo(linkedDirectoryPath).isSymLink());
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("symbolic-link authorization-root entries"));
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QVERIFY(result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(result.validatesCurrent);
    QVERIFY(QFile::remove(linkedDirectoryPath));
    QVERIFY(QDir(linkedDirectoryTarget).removeRecursively());

    install(policy(1), authorization(1));
    const QString hiddenLinkedDirectoryTarget
        = temporaryDirectory.path() + "/hidden-linked-material-tree";
    const QString hiddenLinkedDirectoryPath = authorizationRoot + "/.hidden-neighbor";
    QVERIFY(QDir().mkpath(hiddenLinkedDirectoryTarget));
    QVERIFY(QFile::copy(
        authorizationPath, hiddenLinkedDirectoryTarget + "/hidden.authorization.json"));
    QVERIFY(QFile::link(hiddenLinkedDirectoryTarget, hiddenLinkedDirectoryPath));
    QVERIFY(QFileInfo(hiddenLinkedDirectoryPath).isSymLink());
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("symbolic-link authorization-root entries"));
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QVERIFY(result.provenance.authorizationSetSha256.isEmpty());
    QVERIFY(result.provenance.records.isEmpty());
    QVERIFY(result.validatesCurrent);
    QVERIFY(QFile::remove(hiddenLinkedDirectoryPath));
    QVERIFY(QDir(hiddenLinkedDirectoryTarget).removeRecursively());

    install(policy(1), authorization(1));
    const QString linkedTarget = temporaryDirectory.path() + "/linked.authorization.json";
    QVERIFY(QFile::rename(authorizationPath, linkedTarget));
    QVERIFY(QFile::link(linkedTarget, authorizationPath));
    QVERIFY(QFileInfo(authorizationPath).isSymLink());
    result = evaluate();
    QVERIFY(!result.xb6.signatureVerified);
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(!result.sv.signatureVerified);
    QVERIFY(!result.sv.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("symbolic-link authorization-root entries"));
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(result.authorizationStatus.authorizedAdapterCount, 0);
    QVERIFY(QFile::remove(authorizationPath));

    install(policy(1), authorization(1));
    const QString linkedSignatureTarget = temporaryDirectory.path() + "/linked.sig";
    QVERIFY(QFile::rename(authorizationSignaturePath, linkedSignatureTarget));
    QVERIFY(QFile::link(linkedSignatureTarget, authorizationSignaturePath));
    QVERIFY(QFileInfo(authorizationSignaturePath).isSymLink());
    result = evaluate();
    QVERIFY(!result.xb6.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("non-symlink"));
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QVERIFY(QFile::remove(authorizationSignaturePath));

    install(policy(1), authorization(1));
    AdapterPackageRepository hiddenMaterialRepository(
        packageRoot, authorizationRootPath, trustRootPath);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot beforeHiddenMaterial
        = hiddenMaterialRepository.authorizationProvenanceSnapshot();
    QVERIFY(beforeHiddenMaterial.isValid());
    QCOMPARE(
        beforeHiddenMaterial.state,
        Data::DeviceAdapterAuthorizationProvenanceState::Authorized);
    const QString hiddenMaterialRoot = authorizationRoot + "/.hidden-material";
    QVERIFY(QDir().mkpath(hiddenMaterialRoot));
    QVERIFY(QFile::copy(
        authorizationPath, hiddenMaterialRoot + "/copy.authorization.json"));
    QVERIFY(QFile::copy(
        authorizationSignaturePath, hiddenMaterialRoot + "/copy.authorization.sig"));
    QVERIFY(!hiddenMaterialRepository.validateCurrent(beforeHiddenMaterial));
    AdapterPackageRepository hiddenMaterialObservedRepository(
        packageRoot, authorizationRootPath, trustRootPath);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot hiddenMaterialSnapshot
        = hiddenMaterialObservedRepository.authorizationProvenanceSnapshot();
    QVERIFY(hiddenMaterialSnapshot.isValid());
    QCOMPARE(
        hiddenMaterialSnapshot.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QCOMPARE(hiddenMaterialSnapshot.authorizationSetSha256.size(), 32);
    QVERIFY(
        hiddenMaterialSnapshot.authorizationSetSha256
        != beforeHiddenMaterial.authorizationSetSha256);
    QVERIFY(hiddenMaterialSnapshot.records.isEmpty());
    QVERIFY(hiddenMaterialObservedRepository.validateCurrent(hiddenMaterialSnapshot));
    QVERIFY(QDir(hiddenMaterialRoot).removeRecursively());
    QVERIFY(hiddenMaterialRepository.validateCurrent(beforeHiddenMaterial));

    install(policy(1), authorization(1));
    AdapterPackageRepository
        equivocationRepository(packageRoot, authorizationRootPath, trustRootPath);
    QVERIFY(equivocationRepository.adapterManifest(unsignedXb6->id, unsignedXb6->version)
                ->realHardwareAllowed);
    auto *equivocationSource
        = qobject_cast<Core::DeviceAdapterAuthorizationProvenanceSource *>(
            &equivocationRepository);
    QVERIFY(equivocationSource);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot beforeEquivocation
        = equivocationSource->authorizationProvenanceSnapshot();
    QVERIFY(equivocationSource->validateCurrent(beforeEquivocation));
    QVERIFY(writeBytes(orphanAuthorizationSignaturePath, QByteArray(64, '\0')));
    QVERIFY(!equivocationSource->validateCurrent(beforeEquivocation));
    QVERIFY(QFile::remove(orphanAuthorizationSignaturePath));
    QVERIFY(equivocationSource->validateCurrent(beforeEquivocation));
    install(policy(1, {QString::fromLatin1(signerKey.keyId.toHex())}), authorization(1));
    QVERIFY(!equivocationSource->validateCurrent(beforeEquivocation));
    equivocationRepository.reload();
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot afterEquivocation
        = equivocationSource->authorizationProvenanceSnapshot();
    QVERIFY(afterEquivocation.generation > beforeEquivocation.generation);
    QCOMPARE(
        afterEquivocation.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QVERIFY(afterEquivocation.records.isEmpty());
    QVERIFY(!equivocationSource->validateCurrent(beforeEquivocation));
    QVERIFY(!equivocationRepository.adapterManifest(unsignedXb6->id, unsignedXb6->version)
                 ->realHardwareAllowed);
    QVERIFY(
        equivocationRepository.authorizationDiagnostics().join('\n').contains("changed identity"));

    install(policy(2), authorization(2));
    AdapterPackageRepository rollbackRepository(packageRoot, authorizationRootPath, trustRootPath);
    QVERIFY(rollbackRepository.adapterManifest(unsignedXb6->id, unsignedXb6->version)
                ->realHardwareAllowed);
    install(policy(1), authorization(1));
    rollbackRepository.reload();
    QVERIFY(!rollbackRepository.adapterManifest(unsignedXb6->id, unsignedXb6->version)
                 ->realHardwareAllowed);
    QVERIFY(rollbackRepository.authorizationDiagnostics().join('\n').contains("rollback"));
}

void EtherCATDeviceAdaptersTests::testAuthorizationStartupMessage()
{
    AdapterAuthorizationStatus partialFailure;
    partialFailure.state = AdapterAuthorizationState::ValidationFailed;
    partialFailure.firstFailure = AdapterAuthorizationFailure::BindingMismatch;
    partialFailure.authorizedAdapterCount = 1;
    partialFailure.validationFailureCount = 3;

    const QString message = adapterAuthorizationStartupMessage(partialFailure);
    QVERIFY(message.contains("does not match the installed adapter"));
    QVERIFY(message.contains("3 issues"));
    QVERIFY(message.startsWith("Manual control unavailable"));
    QVERIFY(!message.contains('/'));
    QVERIFY(!message.contains('\\'));
}

void EtherCATDeviceAdaptersTests::testV2StrictParserAndCanonicalDigest()
{
    const Utils::FilePath bundledPath = ::Core::ICore::resourcePath(
        "ethercat/adapters/v2/inovance-sv630n-rev00010000.adapter.json");
    const Utils::Result<QByteArray> bundledContents = bundledPath.fileContents();
    QVERIFY_RESULT(bundledContents);
    const QJsonDocument bundledDocument = QJsonDocument::fromJson(*bundledContents);
    QVERIFY(bundledDocument.isObject());
    const QJsonObject valid = bundledDocument.object();
    QVERIFY2(packageLoadError(valid).isEmpty(), qPrintable(packageLoadError(valid)));

    QJsonObject nestedUnknown = valid;
    QJsonArray signalArray = nestedUnknown.value("signals").toArray();
    QJsonObject signal = signalArray.at(0).toObject();
    QJsonObject transform = signal.value("engineeringTransform").toObject();
    transform.insert("unexpected", true);
    signal.insert("engineeringTransform", transform);
    signalArray.replace(0, signal);
    nestedUnknown.insert("signals", signalArray);
    QVERIFY(packageLoadError(nestedUnknown).contains("unknown field \"unexpected\""));

    QJsonObject nonCanonicalRational = valid;
    signalArray = nonCanonicalRational.value("signals").toArray();
    signal = signalArray.at(0).toObject();
    transform = signal.value("engineeringTransform").toObject();
    QJsonObject scale = transform.value("scale").toObject();
    scale.insert("numerator", "01");
    transform.insert("scale", scale);
    signal.insert("engineeringTransform", transform);
    signalArray.replace(0, signal);
    nonCanonicalRational.insert("signals", signalArray);
    QVERIFY(packageLoadError(nonCanonicalRational).contains("canonical signed decimal"));

    QJsonObject unreducedRational = valid;
    signalArray = unreducedRational.value("signals").toArray();
    signal = signalArray.at(0).toObject();
    transform = signal.value("engineeringTransform").toObject();
    scale = transform.value("scale").toObject();
    scale.insert("numerator", "2");
    scale.insert("denominator", "2");
    transform.insert("scale", scale);
    signal.insert("engineeringTransform", transform);
    signalArray.replace(0, signal);
    unreducedRational.insert("signals", signalArray);
    QVERIFY(packageLoadError(unreducedRational).contains("canonical reduced form"));

    QJsonObject implicitRounding = valid;
    signalArray = implicitRounding.value("signals").toArray();
    signal = signalArray.at(0).toObject();
    transform = signal.value("engineeringTransform").toObject();
    transform.insert("rounding", "implicit");
    signal.insert("engineeringTransform", transform);
    signalArray.replace(0, signal);
    implicitRounding.insert("signals", signalArray);
    QVERIFY(packageLoadError(implicitRounding).contains("rounding is not supported"));

    QJsonObject invalidExposure = valid;
    signalArray = invalidExposure.value("signals").toArray();
    signal = signalArray.at(0).toObject();
    signal.insert("exposure", "writable");
    signalArray.replace(0, signal);
    invalidExposure.insert("signals", signalArray);
    QVERIFY(packageLoadError(invalidExposure).contains("exposure is not supported"));

    QJsonObject unsortedSignals = valid;
    signalArray = unsortedSignals.value("signals").toArray();
    const QJsonValue firstSignal = signalArray.at(0);
    signalArray.replace(0, signalArray.at(1));
    signalArray.replace(1, firstSignal);
    unsortedSignals.insert("signals", signalArray);
    QVERIFY(packageLoadError(unsortedSignals).contains("strict semantic ID order"));

    QJsonObject invalidLiteral = valid;
    QJsonArray actions = invalidLiteral.value("controlActions").toArray();
    QJsonObject action = actions.at(0).toObject();
    QJsonArray steps = action.value("steps").toArray();
    QJsonObject step = steps.at(0).toObject();
    QJsonObject controlValue = step.value("value").toObject();
    QJsonObject exactValue = controlValue.value("engineeringLiteralValue").toObject();
    exactValue.insert("value", "09");
    controlValue.insert("engineeringLiteralValue", exactValue);
    step.insert("value", controlValue);
    steps.replace(0, step);
    action.insert("steps", steps);
    actions.replace(0, action);
    invalidLiteral.insert("controlActions", actions);
    QVERIFY(packageLoadError(invalidLiteral).contains("canonical signed decimal"));

    QJsonObject unsortedFallback = valid;
    actions = unsortedFallback.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert("allowedReleaseActionIds", QJsonArray{"z", "a"});
    actions.replace(0, action);
    unsortedFallback.insert("controlActions", actions);
    QVERIFY(packageLoadError(unsortedFallback).contains("strict canonical order"));

    QJsonObject unknownFallback = valid;
    actions = unknownFallback.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert(
        "allowedReleaseActionIds", QJsonArray{"org.embedlabs.inovance.sv630n.action.unknown"});
    actions.replace(0, action);
    unknownFallback.insert("controlActions", actions);
    QVERIFY(packageLoadError(unknownFallback).contains("unknown fallback"));

    QJsonObject selfFallback = valid;
    actions = selfFallback.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert("allowedReleaseActionIds", QJsonArray{action.value("id").toString()});
    actions.replace(0, action);
    selfFallback.insert("controlActions", actions);
    QVERIFY(packageLoadError(selfFallback).contains("self-referential"));

    QJsonObject invalidParameterDefault = valid;
    actions = invalidParameterDefault.value("controlActions").toArray();
    action = actions.at(1).toObject();
    QJsonArray parameters = action.value("parameters").toArray();
    QJsonObject parameter = parameters.at(0).toObject();
    parameter
        .insert("engineeringDefaultValue", QJsonObject{{"kind", "signed-integer"}, {"value", "-1"}});
    parameters.replace(0, parameter);
    action.insert("parameters", parameters);
    actions.replace(1, action);
    invalidParameterDefault.insert("controlActions", actions);
    QVERIFY(packageLoadError(invalidParameterDefault).contains("below the declared minimum"));

    const QByteArray pretty = QJsonDocument(valid).toJson(QJsonDocument::Indented);
    const QByteArray compact = QJsonDocument(valid).toJson(QJsonDocument::Compact);
    const QByteArray reversed = jsonWithReverseRootKeys(valid);
    QString error;
    const QByteArray prettyDigest = packageDigest(pretty, &error);
    QVERIFY2(!prettyDigest.isEmpty(), qPrintable(error));
    const QByteArray compactDigest = packageDigest(compact, &error);
    QVERIFY2(!compactDigest.isEmpty(), qPrintable(error));
    const QByteArray reversedDigest = packageDigest(reversed, &error);
    QVERIFY2(!reversedDigest.isEmpty(), qPrintable(error));
    QCOMPARE(compactDigest, prettyDigest);
    QCOMPARE(reversedDigest, prettyDigest);

    QJsonObject semanticMutation = valid;
    QString changedDescription = semanticMutation.value("description").toString();
    changedDescription.append(" changed");
    semanticMutation.insert("description", changedDescription);
    const QByteArray changedDigest
        = packageDigest(QJsonDocument(semanticMutation).toJson(QJsonDocument::Compact), &error);
    QVERIFY2(!changedDigest.isEmpty(), qPrintable(error));
    QVERIFY(changedDigest != prettyDigest);
}

void EtherCATDeviceAdaptersTests::testV3SignedActionContract()
{
    const QJsonObject valid = v3Fixture();
    QVERIFY(!valid.isEmpty());
    QVERIFY2(packageLoadError(valid).isEmpty(), qPrintable(packageLoadError(valid)));

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QVERIFY(writePackage(temporaryDirectory.path() + "/v3.adapter.json", valid));
    AdapterPackageRepository repository(Utils::FilePath::fromString(temporaryDirectory.path()));
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    QCOMPARE(repository.loadedPackageCount(), 1);
    const Data::DeviceAdapterManifest manifest = repository.adapterManifests().constFirst();
    QCOMPARE(manifest.contractVersion, Data::DeviceAdapterContractVersion::V3);
    QVERIFY(Data::isValidDeviceAdapterContractVersion(manifest.contractVersion));
    QVERIFY(!Data::isValidDeviceAdapterContractVersion(Data::DeviceAdapterContractVersion::Unknown));
    QCOMPARE(manifest.id.value, QString("org.embedlabs.adapter.test.atomic-output"));
    QCOMPARE(manifest.controllerAdapterTarget.adapterId, QString("test.atomic_output"));
    QCOMPARE(manifest.controllerAdapterTarget.adapterVersion, QString("1.0.0"));
    QCOMPARE(
        manifest.controllerAdapterTarget.adapterSha256,
        QByteArray::fromHex("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));
    QCOMPARE(manifest.controllerAdapterTarget.esiSha256, manifest.match.exactEsiSha256);
    QCOMPARE(manifest.processDataProfiles.size(), 1);
    QCOMPARE(manifest.processDataProfiles.constFirst().signedPdoProfileId, QString("test-profile-v1"));
    QVERIFY(manifest.processDataProfiles.constFirst().signedDcProfileId.isEmpty());
    QCOMPARE(manifest.controlActions.size(), 1);

    const Data::DeviceControlAction &action = manifest.controlActions.constFirst();
    QVERIFY(action.enabled);
    QCOMPARE(action.signedQualification, Data::DeviceControlActionQualification::Qualified);
    QVERIFY(action.disabledReason.isEmpty());
    QCOMPARE(action.expectedSignedDefinitionSha256.size(), qsizetype(32));
    QCOMPARE(action.signedPdoProfileIds, QStringList{"test-profile-v1"});
    QCOMPARE(action.failureDisposition, Data::DeviceControlFailureDisposition::HoldOperationalFault);
    QCOMPARE(action.consistencyGroups.size(), 1);
    const Data::DeviceControlConsistencyGroup &group = action.consistencyGroups.constFirst();
    QCOMPARE(group.id, QString("manual_outputs"));
    QCOMPARE(group.members.size(), 2);
    QCOMPARE(group.recovery, Data::DeviceControlGroupRecovery::HoldSafe);
    QCOMPARE(group.maximumTtlCycles, quint32(1000));
    QCOMPARE(action.steps.size(), 3);
    QCOMPARE(action.steps.at(0).kind, Data::DeviceControlStepKind::WaitMaskedEquals);
    QCOMPARE(action.steps.at(0).timeoutCycles, quint32(1000));
    QCOMPARE(action.steps.at(1).kind, Data::DeviceControlStepKind::WriteGroup);
    QCOMPARE(action.steps.at(1).assignments.size(), 2);
    QCOMPARE(
        action.steps.at(1).assignments.at(0).value.source,
        Data::DeviceControlValueSource::Parameter);
    QCOMPARE(
        action.steps.at(1).assignments.at(1).value.source, Data::DeviceControlValueSource::Literal);
    QCOMPARE(action.steps.at(2).kind, Data::DeviceControlStepKind::WaitCycles);
    QCOMPARE(action.steps.at(2).timeoutCycles, quint32(2));
    QVERIFY(action.parameters.constFirst().engineeringDefaultValue);
    QVERIFY(manifest.semanticSignals.at(0).engineeringSafeValue);
    QVERIFY(manifest.semanticSignals.at(1).engineeringSafeValue);
    QVERIFY(!manifest.semanticSignals.at(2).engineeringSafeValue);

    QJsonObject dcBound = valid;
    QJsonArray dcProfiles = dcBound.value("processDataProfiles").toArray();
    QJsonObject dcProfile = dcProfiles.at(0).toObject();
    dcProfile.insert("signedDcProfileId", "sync0_125us");
    dcProfiles.replace(0, dcProfile);
    dcBound.insert("processDataProfiles", dcProfiles);
    QJsonArray dcActions = dcBound.value("controlActions").toArray();
    QJsonObject dcAction = dcActions.at(0).toObject();
    dcAction.insert("requiresDc", true);
    dcActions.replace(0, dcAction);
    dcBound.insert("controlActions", dcActions);
    QVERIFY2(packageLoadError(dcBound).isEmpty(), qPrintable(packageLoadError(dcBound)));

    QString error;
    const QByteArray pretty
        = packageDigest(QJsonDocument(valid).toJson(QJsonDocument::Indented), &error);
    QVERIFY2(!pretty.isEmpty(), qPrintable(error));
    const QByteArray compact
        = packageDigest(QJsonDocument(valid).toJson(QJsonDocument::Compact), &error);
    QVERIFY2(!compact.isEmpty(), qPrintable(error));
    const QByteArray reversed = packageDigest(jsonWithReverseRootKeys(valid), &error);
    QVERIFY2(!reversed.isEmpty(), qPrintable(error));
    QCOMPARE(compact, pretty);
    QCOMPARE(reversed, pretty);

    QJsonObject semanticMutation = valid;
    QJsonArray actions = semanticMutation.value("controlActions").toArray();
    QJsonObject mutatedAction = actions.at(0).toObject();
    mutatedAction.insert("failureDisposition", "hold-safe");
    actions.replace(0, mutatedAction);
    semanticMutation.insert("controlActions", actions);
    const QByteArray changed
        = packageDigest(QJsonDocument(semanticMutation).toJson(QJsonDocument::Compact), &error);
    QVERIFY2(!changed.isEmpty(), qPrintable(error));
    QVERIFY(changed != pretty);

    const QString schemaPath = QFINDTESTDATA("testdata/device-adapter-v3.schema.json");
    QVERIFY(!schemaPath.isEmpty());
    QFile schemaFile(schemaPath);
    QVERIFY(schemaFile.open(QIODevice::ReadOnly));
    const QJsonDocument schemaDocument = QJsonDocument::fromJson(schemaFile.readAll());
    QVERIFY(schemaDocument.isObject());
    QCOMPARE(
        schemaDocument.object().value("$id").toString(),
        QString("https://embed-labs.dev/schemas/device-adapter-v3.schema.json"));
    const QJsonObject definitions = schemaDocument.object().value("$defs").toObject();
    QCOMPARE(
        definitions.value("simpleId").toObject().value("pattern").toString(),
        QString("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
    QCOMPARE(
        definitions.value("nonzeroSha256")
            .toObject()
            .value("allOf")
            .toArray()
            .at(0)
            .toObject()
            .value("$ref")
            .toString(),
        QString("#/$defs/sha256"));
    const QJsonObject profileSchema = definitions.value("profile").toObject();
    QVERIFY(profileSchema.value("required")
                .toArray()
                .contains(QJsonValue(QStringLiteral("signedDcProfileId"))));
    QCOMPARE(
        profileSchema.value("properties")
            .toObject()
            .value("signedDcProfileId")
            .toObject()
            .value("oneOf")
            .toArray()
            .size(),
        2);
}

void EtherCATDeviceAdaptersTests::testV3RejectsUnsafeContracts()
{
    const QJsonObject valid = v3Fixture();
    QVERIFY(!valid.isEmpty());

    QJsonObject unknown = valid;
    unknown.insert("unexpected", true);
    QVERIFY(packageLoadError(unknown).contains("unknown field \"unexpected\""));

    QJsonObject missingTarget = valid;
    missingTarget.remove("controllerAdapterTarget");
    QVERIFY(packageLoadError(missingTarget).contains("missing field \"controllerAdapterTarget\""));

    QJsonObject unknownTargetField = valid;
    QJsonObject target = unknownTargetField.value("controllerAdapterTarget").toObject();
    target.insert("unexpected", true);
    unknownTargetField.insert("controllerAdapterTarget", target);
    QVERIFY(packageLoadError(unknownTargetField).contains("unknown field \"unexpected\""));

    QJsonObject zeroTargetDigest = valid;
    target = zeroTargetDigest.value("controllerAdapterTarget").toObject();
    target.insert("adapterSha256", QString(64, '0'));
    zeroTargetDigest.insert("controllerAdapterTarget", target);
    QVERIFY(packageLoadError(zeroTargetDigest).contains("adapterSha256 must be non-zero"));

    QJsonObject mismatchedTargetEsi = valid;
    target = mismatchedTargetEsi.value("controllerAdapterTarget").toObject();
    target.insert("esiSha256", QString(64, 'e'));
    mismatchedTargetEsi.insert("controllerAdapterTarget", target);
    QVERIFY(packageLoadError(mismatchedTargetEsi).contains("does not match the exact ESI identity"));

    QJsonObject missing = valid;
    QJsonArray actions = missing.value("controlActions").toArray();
    QJsonObject action = actions.at(0).toObject();
    action.remove("failureDisposition");
    actions.replace(0, action);
    missing.insert("controlActions", actions);
    QVERIFY(packageLoadError(missing).contains("missing field \"failureDisposition\""));

    QJsonObject invalidDigest = valid;
    actions = invalidDigest.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert(
        "expectedSignedDefinitionSha256",
        action.value("expectedSignedDefinitionSha256").toString().toUpper());
    actions.replace(0, action);
    invalidDigest.insert("controlActions", actions);
    QVERIFY(packageLoadError(invalidDigest).contains("exact 64-character SHA-256"));

    QJsonObject zeroDigest = valid;
    actions = zeroDigest.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert("expectedSignedDefinitionSha256", QString(64, '0'));
    actions.replace(0, action);
    zeroDigest.insert("controlActions", actions);
    QVERIFY(packageLoadError(zeroDigest).contains("invalid signed definition digest"));

    QJsonObject duplicateMember = valid;
    actions = duplicateMember.value("controlActions").toArray();
    action = actions.at(0).toObject();
    QJsonArray groups = action.value("consistencyGroups").toArray();
    QJsonObject group = groups.at(0).toObject();
    QJsonArray members = group.value("members").toArray();
    members.append(members.at(1));
    group.insert("members", members);
    groups.replace(0, group);
    action.insert("consistencyGroups", groups);
    actions.replace(0, action);
    duplicateMember.insert("controlActions", actions);
    QVERIFY(packageLoadError(duplicateMember).contains("duplicate values"));

    QJsonObject partialGroup = valid;
    actions = partialGroup.value("controlActions").toArray();
    action = actions.at(0).toObject();
    QJsonArray steps = action.value("steps").toArray();
    QJsonObject writeGroup = steps.at(1).toObject();
    QJsonArray assignments = writeGroup.value("assignments").toArray();
    assignments.removeLast();
    writeGroup.insert("assignments", assignments);
    steps.replace(1, writeGroup);
    action.insert("steps", steps);
    actions.replace(0, action);
    partialGroup.insert("controlActions", actions);
    QVERIFY(packageLoadError(partialGroup).contains("partial WriteGroup"));

    QJsonObject missingSafeValue = valid;
    QJsonArray signalArray = missingSafeValue.value("signals").toArray();
    QJsonObject output = signalArray.at(0).toObject();
    output.insert("engineeringSafeValue", QJsonValue::Null);
    signalArray.replace(0, output);
    missingSafeValue.insert("signals", signalArray);
    QVERIFY(packageLoadError(missingSafeValue).contains("unsafe or duplicate member"));

    QJsonObject nonOutputPdo = valid;
    signalArray = nonOutputPdo.value("signals").toArray();
    output = signalArray.at(0).toObject();
    QJsonArray bindings = output.value("bindings").toArray();
    QJsonObject binding = bindings.at(0).toObject();
    binding.insert("pdoDirection", "tx");
    bindings.replace(0, binding);
    output.insert("bindings", bindings);
    signalArray.replace(0, output);
    nonOutputPdo.insert("signals", signalArray);
    QVERIFY(packageLoadError(nonOutputPdo).contains("unsafe or duplicate member"));

    QJsonObject writeOnlyOutput = valid;
    signalArray = writeOnlyOutput.value("signals").toArray();
    output = signalArray.at(0).toObject();
    output.insert("access", "write-only");
    signalArray.replace(0, output);
    writeOnlyOutput.insert("signals", signalArray);
    QVERIFY(packageLoadError(writeOnlyOutput).contains("unsafe or duplicate member"));

    QJsonObject bidirectionalOutput = valid;
    signalArray = bidirectionalOutput.value("signals").toArray();
    output = signalArray.at(0).toObject();
    output.insert("direction", "bidirectional");
    signalArray.replace(0, output);
    bidirectionalOutput.insert("signals", signalArray);
    QVERIFY(packageLoadError(bidirectionalOutput).contains("unsafe or duplicate member"));

    QJsonObject unsafeConstant = valid;
    actions = unsafeConstant.value("controlActions").toArray();
    action = actions.at(0).toObject();
    steps = action.value("steps").toArray();
    writeGroup = steps.at(1).toObject();
    assignments = writeGroup.value("assignments").toArray();
    QJsonObject assignment = assignments.at(1).toObject();
    QJsonObject assignmentValue = assignment.value("value").toObject();
    assignmentValue
        .insert("engineeringConstant", QJsonObject{{"kind", "unsigned-integer"}, {"value", "2"}});
    assignment.insert("value", assignmentValue);
    assignments.replace(1, assignment);
    writeGroup.insert("assignments", assignments);
    steps.replace(1, writeGroup);
    action.insert("steps", steps);
    actions.replace(0, action);
    unsafeConstant.insert("controlActions", actions);
    const QString unsafeConstantError = packageLoadError(unsafeConstant);
    QVERIFY2(unsafeConstantError.contains("declared maximum"), qPrintable(unsafeConstantError));

    QJsonObject mixedTimeout = valid;
    actions = mixedTimeout.value("controlActions").toArray();
    action = actions.at(0).toObject();
    steps = action.value("steps").toArray();
    QJsonObject wait = steps.at(0).toObject();
    wait.insert("timeoutMs", 1000);
    steps.replace(0, wait);
    action.insert("steps", steps);
    actions.replace(0, action);
    mixedTimeout.insert("controlActions", actions);
    QVERIFY(packageLoadError(mixedTimeout).contains("unknown field \"timeoutMs\""));

    QJsonObject legacyWrite = valid;
    actions = legacyWrite.value("controlActions").toArray();
    action = actions.at(0).toObject();
    steps = action.value("steps").toArray();
    writeGroup = steps.at(1).toObject();
    writeGroup.insert("kind", "write-signal");
    steps.replace(1, writeGroup);
    action.insert("steps", steps);
    actions.replace(0, action);
    legacyWrite.insert("controlActions", actions);
    QVERIFY(packageLoadError(legacyWrite).contains("forbids legacy write-signal"));

    QJsonObject unknownGroup = valid;
    actions = unknownGroup.value("controlActions").toArray();
    action = actions.at(0).toObject();
    steps = action.value("steps").toArray();
    writeGroup = steps.at(1).toObject();
    writeGroup.insert("consistencyGroup", "unknown_group");
    steps.replace(1, writeGroup);
    action.insert("steps", steps);
    actions.replace(0, action);
    unknownGroup.insert("controlActions", actions);
    QVERIFY(packageLoadError(unknownGroup).contains("invalid or partial WriteGroup"));

    QJsonObject duplicateSignedProfile = valid;
    QJsonArray profiles = duplicateSignedProfile.value("processDataProfiles").toArray();
    QJsonObject secondProfile = profiles.at(0).toObject();
    secondProfile.insert("id", "org.embedlabs.test.profile.second");
    profiles.append(secondProfile);
    duplicateSignedProfile.insert("processDataProfiles", profiles);
    QVERIFY(packageLoadError(duplicateSignedProfile).contains("signed PDO profile IDs"));

    QJsonObject incompleteSignedProfile = valid;
    profiles = incompleteSignedProfile.value("processDataProfiles").toArray();
    QJsonObject profile = profiles.at(0).toObject();
    QJsonArray profileSignals = profile.value("requiredSignals").toArray();
    profileSignals.removeLast();
    profile.insert("requiredSignals", profileSignals);
    profiles.replace(0, profile);
    incompleteSignedProfile.insert("processDataProfiles", profiles);
    QVERIFY(packageLoadError(incompleteSignedProfile)
                .contains("required signals are not completely covered"));

    QJsonObject missingSignedDcProfile = valid;
    profiles = missingSignedDcProfile.value("processDataProfiles").toArray();
    profile = profiles.at(0).toObject();
    profile.remove("signedDcProfileId");
    profiles.replace(0, profile);
    missingSignedDcProfile.insert("processDataProfiles", profiles);
    QVERIFY(
        packageLoadError(missingSignedDcProfile).contains("missing field \"signedDcProfileId\""));

    QJsonObject emptySignedDcProfile = valid;
    profiles = emptySignedDcProfile.value("processDataProfiles").toArray();
    profile = profiles.at(0).toObject();
    profile.insert("signedDcProfileId", "");
    profiles.replace(0, profile);
    emptySignedDcProfile.insert("processDataProfiles", profiles);
    QVERIFY(packageLoadError(emptySignedDcProfile)
                .contains("signedDcProfileId must be null or canonical"));

    QJsonObject malformedSignedDcProfile = valid;
    profiles = malformedSignedDcProfile.value("processDataProfiles").toArray();
    profile = profiles.at(0).toObject();
    profile.insert("signedDcProfileId", "invalid:dc");
    profiles.replace(0, profile);
    malformedSignedDcProfile.insert("processDataProfiles", profiles);
    QVERIFY(packageLoadError(malformedSignedDcProfile)
                .contains("signedDcProfileId must be null or canonical"));

    QJsonObject nonDcActionOnDcProfile = valid;
    profiles = nonDcActionOnDcProfile.value("processDataProfiles").toArray();
    profile = profiles.at(0).toObject();
    profile.insert("signedDcProfileId", "sync0_125us");
    profiles.replace(0, profile);
    nonDcActionOnDcProfile.insert("processDataProfiles", profiles);
    QVERIFY2(
        packageLoadError(nonDcActionOnDcProfile).isEmpty(),
        qPrintable(packageLoadError(nonDcActionOnDcProfile)));

    QJsonObject missingRequiredDcProfile = valid;
    actions = missingRequiredDcProfile.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert("requiresDc", true);
    actions.replace(0, action);
    missingRequiredDcProfile.insert("controlActions", actions);
    QVERIFY(packageLoadError(missingRequiredDcProfile).contains("requires a signed DC profile"));

    QJsonObject invalidTtl = valid;
    actions = invalidTtl.value("controlActions").toArray();
    action = actions.at(0).toObject();
    groups = action.value("consistencyGroups").toArray();
    group = groups.at(0).toObject();
    group.insert("maxTtlCycles", 0);
    groups.replace(0, group);
    action.insert("consistencyGroups", groups);
    actions.replace(0, action);
    invalidTtl.insert("controlActions", actions);
    QVERIFY(packageLoadError(invalidTtl).contains("range 1 to 65535"));

    QJsonObject overlappingSignals = valid;
    actions = overlappingSignals.value("controlActions").toArray();
    action = actions.at(0).toObject();
    action.insert("optionalSignals", QJsonArray{"org.embedlabs.test.status"});
    actions.replace(0, action);
    overlappingSignals.insert("controlActions", actions);
    QVERIFY(packageLoadError(overlappingSignals).contains("must be disjoint"));

    QJsonObject nonSimpleProfileId = valid;
    profiles = nonSimpleProfileId.value("processDataProfiles").toArray();
    profile = profiles.at(0).toObject();
    profile.insert("signedPdoProfileId", "invalid:profile");
    profiles.replace(0, profile);
    nonSimpleProfileId.insert("processDataProfiles", profiles);
    QVERIFY(packageLoadError(nonSimpleProfileId).contains("signedPdoProfileId must be canonical"));

    QJsonObject nonSimpleGroupId = valid;
    actions = nonSimpleGroupId.value("controlActions").toArray();
    action = actions.at(0).toObject();
    groups = action.value("consistencyGroups").toArray();
    group = groups.at(0).toObject();
    group.insert("id", "invalid:group");
    groups.replace(0, group);
    action.insert("consistencyGroups", groups);
    actions.replace(0, action);
    nonSimpleGroupId.insert("controlActions", actions);
    QVERIFY(packageLoadError(nonSimpleGroupId).contains(".id must be canonical"));

    QJsonObject nonSimpleParameterId = valid;
    actions = nonSimpleParameterId.value("controlActions").toArray();
    action = actions.at(0).toObject();
    QJsonArray parameters = action.value("parameters").toArray();
    QJsonObject parameter = parameters.at(0).toObject();
    parameter.insert("id", "invalid:parameter");
    parameters.replace(0, parameter);
    action.insert("parameters", parameters);
    actions.replace(0, action);
    nonSimpleParameterId.insert("controlActions", actions);
    QVERIFY(packageLoadError(nonSimpleParameterId).contains("parameters must use IDs"));

    QJsonObject duplicateGlobalSignal = valid;
    signalArray = duplicateGlobalSignal.value("signals").toArray();
    QJsonObject duplicateSignal = signalArray.at(signalArray.size() - 1).toObject();
    duplicateSignal.insert("id", "zz.embedlabs.test.{slot}.status");
    bindings = duplicateSignal.value("bindings").toArray();
    binding = bindings.at(0).toObject();
    binding.insert("slotRelative", true);
    bindings.replace(0, binding);
    duplicateSignal.insert("bindings", bindings);
    signalArray.append(duplicateSignal);
    duplicateGlobalSignal.insert("signals", signalArray);
    duplicateGlobalSignal.insert(
        "moduleProfiles",
        QJsonArray{QJsonObject{
            {"id", "zz.embedlabs.test.module"},
            {"moduleIdent", 1},
            {"typeName", "Duplicate test module"},
            {"moduleClass", "test"},
            {"capabilities", QJsonArray{}},
            {"signals", QJsonArray{duplicateSignal}},
        }});
    QVERIFY(
        packageLoadError(duplicateGlobalSignal).contains("duplicate v3 semantic signal definition"));

    QJsonObject inexactParameterStep = valid;
    signalArray = inexactParameterStep.value("signals").toArray();
    output = signalArray.at(0).toObject();
    QJsonObject transform = output.value("engineeringTransform").toObject();
    QJsonObject scale = transform.value("scale").toObject();
    scale.insert("numerator", "2");
    transform.insert("scale", scale);
    QJsonObject signalConstraint = transform.value("constraint").toObject();
    QJsonObject signalMaximum = signalConstraint.value("maximum").toObject();
    signalMaximum.insert("numerator", "2");
    signalConstraint.insert("maximum", signalMaximum);
    transform.insert("constraint", signalConstraint);
    output.insert("engineeringTransform", transform);
    signalArray.replace(0, output);
    inexactParameterStep.insert("signals", signalArray);
    actions = inexactParameterStep.value("controlActions").toArray();
    action = actions.at(0).toObject();
    parameters = action.value("parameters").toArray();
    parameter = parameters.at(0).toObject();
    QJsonObject parameterConstraint = parameter.value("engineeringConstraint").toObject();
    QJsonObject parameterMaximum = parameterConstraint.value("maximum").toObject();
    parameterMaximum.insert("numerator", "2");
    parameterConstraint.insert("maximum", parameterMaximum);
    parameter.insert("engineeringConstraint", parameterConstraint);
    parameters.replace(0, parameter);
    action.insert("parameters", parameters);
    actions.replace(0, action);
    inexactParameterStep.insert("controlActions", actions);
    QVERIFY(packageLoadError(inexactParameterStep).contains("step is not exactly encodable"));
}

void EtherCATDeviceAdaptersTests::testV4ParameterDefinitionContract()
{
    const QJsonObject valid = installedSv630nV4Manifest();
    QVERIFY(!valid.isEmpty());
    QVERIFY2(packageLoadError(valid).isEmpty(), qPrintable(packageLoadError(valid)));

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QVERIFY(writePackage(temporaryDirectory.path() + "/sv630n.adapter.json", valid));
    AdapterPackageRepository repository(Utils::FilePath::fromString(temporaryDirectory.path()));
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    QCOMPARE(repository.loadedPackageCount(), 1);
    const Data::DeviceAdapterManifest manifest = repository.adapterManifests().constFirst();
    QCOMPARE(manifest.contractVersion, Data::DeviceAdapterContractVersion::V4);
    QCOMPARE(manifest.parameterDefinitions.size(), 7);
    QVERIFY(!manifest.signatureVerified);
    QVERIFY(!manifest.realHardwareAllowed);
    for (const Data::DeviceParameterDefinition &definition : manifest.parameterDefinitions) {
        QCOMPARE(definition.definitionSha256.size(), 32);
        QVERIFY(std::any_of(
            definition.definitionSha256.cbegin(),
            definition.definitionSha256.cend(),
            [](char byte) { return byte != 0; }));
        QVERIFY(definition.required);
        QVERIFY(!definition.engineeringDefaultValue);
    }
    QCOMPARE(manifest.parameterDefinitions.at(0).id, QString("cia402.gear_ratio.motor_revolutions"));
    QCOMPARE(manifest.parameterDefinitions.at(0).configuredProjection.object->index, quint16(0x6091));
    QCOMPARE(manifest.parameterDefinitions.at(0).configuredProjection.object->subIndex, quint8(1));
    QCOMPARE(manifest.parameterDefinitions.at(1).configuredProjection.object->subIndex, quint8(2));
    QCOMPARE(manifest.parameterDefinitions.at(2).configuredProjection.object->index, quint16(0x607f));
    QCOMPARE(manifest.parameterDefinitions.at(3).configuredProjection.object->index, quint16(0x2006));
    QCOMPARE(manifest.parameterDefinitions.at(3).configuredProjection.object->subIndex, quint8(9));
    QCOMPARE(manifest.parameterDefinitions.at(4).configuredProjection.object->subIndex, quint8(10));
    QCOMPARE(
        manifest.parameterDefinitions.at(5).configuredProjection.kind,
        Data::DeviceParameterProjectionKind::ProjectOnly);
    QCOMPARE(
        manifest.parameterDefinitions.at(5).observedSource.kind,
        Data::DeviceParameterObservedSourceKind::Unavailable);
    QCOMPARE(
        manifest.parameterDefinitions.at(6).id,
        QString("motor.encoder_resolution_counts_per_revolution"));

    QString digestError;
    const QByteArray compactBytes = QJsonDocument(valid).toJson(QJsonDocument::Compact);
    const QByteArray compactDigest = packageDigest(compactBytes, &digestError);
    QVERIFY2(!compactDigest.isEmpty(), qPrintable(digestError));
    const QByteArray reorderedDigest = packageDigest(jsonWithReverseRootKeys(valid), &digestError);
    QVERIFY2(!reorderedDigest.isEmpty(), qPrintable(digestError));
    QCOMPARE(reorderedDigest, compactDigest);
    QVERIFY(compactDigest != QCryptographicHash::hash(compactBytes, QCryptographicHash::Sha256));

    QJsonObject missingDefinitions = valid;
    missingDefinitions.remove("parameterDefinitions");
    QVERIFY(packageLoadError(missingDefinitions).contains("missing field \"parameterDefinitions\""));

    QCOMPARE(Data::maximumDeviceParameterDefinitionsPerAdapter, qsizetype(64));
    QJsonObject emptyDefinitions = valid;
    emptyDefinitions.insert("parameterDefinitions", QJsonArray{});
    QVERIFY(packageLoadError(emptyDefinitions).contains("between 1 and 64 definitions"));

    QJsonObject v3WithDefinitions = valid;
    v3WithDefinitions.insert("schemaVersion", "embed-labs.device-adapter/v3");
    QVERIFY(packageLoadError(v3WithDefinitions).contains("unknown field \"parameterDefinitions\""));

    QJsonObject unknownDefinitionField = valid;
    QJsonArray definitions = unknownDefinitionField.value("parameterDefinitions").toArray();
    QJsonObject first = definitions.at(0).toObject();
    first.insert("unexpected", true);
    definitions.replace(0, first);
    unknownDefinitionField.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(unknownDefinitionField).contains("unknown field \"unexpected\""));

    QJsonObject unsorted = valid;
    definitions = unsorted.value("parameterDefinitions").toArray();
    const QJsonValue firstValue = definitions.at(0);
    definitions.replace(0, definitions.at(1));
    definitions.replace(1, firstValue);
    unsorted.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(unsorted).contains("strict parameter ID order"));

    QJsonObject requiredFalse = valid;
    definitions = requiredFalse.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    first.insert("required", false);
    definitions.replace(0, first);
    requiredFalse.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(requiredFalse).contains("required must equal true"));

    QJsonObject nonNullDefault = valid;
    definitions = nonNullDefault.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    first.insert("engineeringDefaultValue", QJsonObject{{"kind", "signed-integer"}, {"value", "1"}});
    definitions.replace(0, first);
    nonNullDefault.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(nonNullDefault).contains("engineeringDefaultValue must equal null"));

    QJsonObject badUnit = valid;
    definitions = badUnit.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    first.insert("unit", "raw-drive-unit");
    definitions.replace(0, first);
    badUnit.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(badUnit).contains("unit is not supported"));

    QJsonObject badDataType = valid;
    definitions = badDataType.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    first.insert("dataType", "exact-rational");
    definitions.replace(0, first);
    badDataType.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(badDataType).contains("dataType is not supported"));

    QJsonObject legacyValueKind = valid;
    definitions = legacyValueKind.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    first.remove("dataType");
    first.insert("valueKind", "unsigned-integer");
    definitions.replace(0, first);
    legacyValueKind.insert("parameterDefinitions", definitions);
    QVERIFY(!packageLoadError(legacyValueKind).isEmpty());

    QJsonObject mixedValueKind = valid;
    definitions = mixedValueKind.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    first.insert("valueKind", first.value("dataType"));
    definitions.replace(0, first);
    mixedValueKind.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(mixedValueKind).contains("unknown field \"valueKind\""));

    QJsonObject legacyNestedProjection = valid;
    definitions = legacyNestedProjection.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    QJsonObject projection = first.value("configuredProjection").toObject();
    QJsonObject legacyObject = projection;
    legacyObject.remove("kind");
    legacyObject.remove("transition");
    legacyObject.remove("bitWidth");
    legacyObject.insert("protocol", "coe");
    QJsonObject legacyTransform = legacyObject.value("engineeringTransform").toObject();
    legacyTransform.insert("unit", first.value("unit"));
    legacyObject.insert("engineeringTransform", legacyTransform);
    first.insert(
        "configuredProjection",
        QJsonObject{{"kind", "coe-startup-sdo"}, {"transition", "PS"}, {"object", legacyObject}});
    first.insert("observedSource", QJsonObject{{"kind", "coe-sdo-upload"}, {"object", legacyObject}});
    definitions.replace(0, first);
    legacyNestedProjection.insert("parameterDefinitions", definitions);
    QVERIFY(!packageLoadError(legacyNestedProjection).isEmpty());

    QJsonObject mixedNestedProjection = valid;
    definitions = mixedNestedProjection.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("object", legacyObject);
    first.insert("configuredProjection", projection);
    definitions.replace(0, first);
    mixedNestedProjection.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(mixedNestedProjection).contains("unknown field \"object\""));

    QJsonObject wrongBitWidth = valid;
    definitions = wrongBitWidth.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("bitWidth", 16);
    first.insert("configuredProjection", projection);
    definitions.replace(0, first);
    wrongBitWidth.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(wrongBitWidth).contains("bitWidth does not match physicalType"));

    QJsonObject badPhysicalType = valid;
    definitions = badPhysicalType.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("physicalType", "integer32");
    first.insert("configuredProjection", projection);
    definitions.replace(0, first);
    badPhysicalType.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(badPhysicalType).contains("physicalType must be"));

    QJsonObject badByteOrder = valid;
    definitions = badByteOrder.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("byteOrder", "big-endian");
    first.insert("configuredProjection", projection);
    definitions.replace(0, first);
    badByteOrder.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(badByteOrder).contains("byteOrder must equal little-endian"));

    QJsonObject badTransition = valid;
    definitions = badTransition.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("transition", "OP");
    first.insert("configuredProjection", projection);
    definitions.replace(0, first);
    badTransition.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(badTransition).contains("transition must equal PS"));

    QJsonObject projectOnlyReason = valid;
    definitions = projectOnlyReason.value("parameterDefinitions").toArray();
    first = definitions.at(5).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("reason", "software_policy_not_device_object");
    first.insert("configuredProjection", projection);
    definitions.replace(5, first);
    projectOnlyReason.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(projectOnlyReason).contains("unknown field \"reason\""));

    QJsonObject unknownUnavailableReason = valid;
    definitions = unknownUnavailableReason.value("parameterDefinitions").toArray();
    first = definitions.at(5).toObject();
    QJsonObject observed = first.value("observedSource").toObject();
    observed.insert("reason", "unknown_reason");
    first.insert("observedSource", observed);
    definitions.replace(5, first);
    unknownUnavailableReason.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(unknownUnavailableReason).contains("reason is not supported"));

    QJsonObject zeroIndex = valid;
    definitions = zeroIndex.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    projection = first.value("configuredProjection").toObject();
    projection.insert("index", 0);
    first.insert("configuredProjection", projection);
    observed = first.value("observedSource").toObject();
    observed.insert("index", 0);
    first.insert("observedSource", observed);
    definitions.replace(0, first);
    zeroIndex.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(zeroIndex).contains("index must be non-zero"));

    QJsonObject mismatchedReadback = valid;
    definitions = mismatchedReadback.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    observed = first.value("observedSource").toObject();
    observed.insert("subIndex", 2);
    first.insert("observedSource", observed);
    definitions.replace(0, first);
    mismatchedReadback.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(mismatchedReadback).contains("must be identical"));

    QJsonObject crossDefinitionCollision = valid;
    definitions = crossDefinitionCollision.value("parameterDefinitions").toArray();
    first = definitions.at(0).toObject();
    QJsonObject second = definitions.at(1).toObject();
    second.insert("configuredProjection", first.value("configuredProjection"));
    second.insert("observedSource", first.value("observedSource"));
    definitions.replace(1, second);
    crossDefinitionCollision.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(crossDefinitionCollision).contains("multiple IDs"));

    QJsonObject tooMany = valid;
    definitions = tooMany.value("parameterDefinitions").toArray();
    while (definitions.size() <= Data::maximumDeviceParameterDefinitionsPerAdapter)
        definitions.append(definitions.at(0));
    QCOMPARE(definitions.size(), 65);
    tooMany.insert("parameterDefinitions", definitions);
    QVERIFY(packageLoadError(tooMany).contains("between 1 and 64 definitions"));
}

void EtherCATDeviceAdaptersTests::testSignedAdapterAuthorizationV2ParameterClosure()
{
    constexpr char policyDomain[] = "embed-labs.ethercat-device-adapter-authorization-policy/v1";
    constexpr char authorizationDomainV1[] = "embed-labs.ethercat-device-adapter-authorization/v1";
    constexpr char authorizationDomainV2[] = "embed-labs.ethercat-device-adapter-authorization/v2";

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString packageRoot = temporaryDirectory.path() + "/packages";
    const QString authorizationRoot = temporaryDirectory.path() + "/authorizations";
    const QString trustRoot = temporaryDirectory.path() + "/trust";
    QVERIFY(QDir().mkpath(packageRoot));
    QVERIFY(QDir().mkpath(authorizationRoot + "/policies"));
    QVERIFY(QDir().mkpath(authorizationRoot + "/authorizations"));
    QVERIFY(QDir().mkpath(trustRoot));
    QVERIFY(writePackage(packageRoot + "/sv630n-v4.adapter.json", sv630nV4Fixture()));
    QVERIFY(writePackage(packageRoot + "/v3.adapter.json", v3Fixture()));

    const Utils::FilePath packageRootPath = Utils::FilePath::fromString(packageRoot);
    const Utils::FilePath authorizationRootPath = Utils::FilePath::fromString(authorizationRoot);
    const Utils::FilePath trustRootPath = Utils::FilePath::fromString(trustRoot);
    AdapterPackageRepository
        unsignedRepository(packageRootPath, authorizationRootPath, trustRootPath);
    QVERIFY2(unsignedRepository.isAvailable(), qPrintable(unsignedRepository.loadErrors().join('\n')));
    QCOMPARE(unsignedRepository.loadedPackageCount(), 2);
    const auto unsignedV4 = unsignedRepository.adapterManifest(
        {"org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"}, "0.4.0-test");
    const auto unsignedV3
        = unsignedRepository
              .adapterManifest({"org.embedlabs.adapter.test.atomic-output"}, "3.0.0-test");
    QVERIFY(unsignedV4);
    QVERIFY(unsignedV3);
    QCOMPARE(unsignedV4->contractVersion, Data::DeviceAdapterContractVersion::V4);
    QCOMPARE(unsignedV3->contractVersion, Data::DeviceAdapterContractVersion::V3);

    const AuthorizationTestKey rootKey = authorizationTestKey('\x31');
    const AuthorizationTestKey signerKey = authorizationTestKey('\x32');
    QVERIFY(writeBytes(
        trustRoot + '/' + QString::fromLatin1(rootKey.keyId.toHex()) + ".pub", rootKey.publicKey));
    QStringList adapterIds{unsignedV4->id.value, unsignedV3->id.value};
    adapterIds.sort();
    QJsonObject signer{
        {"keyId", QString::fromLatin1(signerKey.keyId.toHex())},
        {"publicKey", QString::fromLatin1(signerKey.publicKey.toHex())},
        {"adapterIds", authorizationStringArray(adapterIds)},
        {"decisions", QJsonArray{"allow"}},
    };
    const QJsonObject policy{
        {"format", "embed-labs.ethercat-device-adapter-authorization-policy-v1"},
        {"formatVersion", 1},
        {"canonicalization", "kvell-json-ascii-sorted-compact-lf-v1"},
        {"policyId", "embed-labs.test-v4-policy"},
        {"policyRevision", 1},
        {"rootKeyId", QString::fromLatin1(rootKey.keyId.toHex())},
        {"signers", QJsonArray{signer}},
        {"revokedSignerKeyIds", QJsonArray{}},
        {"revokedAuthorizationIds", QJsonArray{}},
    };
    const QString policyPath = authorizationRoot + "/policies/test.policy.json";
    const QString authorizationPath = authorizationRoot + "/authorizations/test.authorization.json";
    QVERIFY(writeSignedAuthorizationDocument(policyPath, policy, rootKey, policyDomain));

    const auto authorization = [&](const QString &format,
                                   int formatVersion,
                                   const QJsonObject &binding,
                                   const QString &id) {
        return QJsonObject{
            {"format", format},
            {"formatVersion", formatVersion},
            {"canonicalization", "kvell-json-ascii-sorted-compact-lf-v1"},
            {"authorizationId", id},
            {"policyId", "embed-labs.test-v4-policy"},
            {"policyRevision", 1},
            {"signerKeyId", QString::fromLatin1(signerKey.keyId.toHex())},
            {"decision", "allow"},
            {"adapter", binding},
        };
    };
    const auto install = [&](const QJsonObject &document, const QByteArray &domain) {
        QVERIFY(writeSignedAuthorizationDocument(authorizationPath, document, signerKey, domain));
    };
    struct Evaluation
    {
        bool available = false;
        bool validatesCurrent = false;
        Data::DeviceAdapterManifest v4;
        Data::DeviceAdapterManifest v3;
        Data::DeviceAdapterAuthorizationProvenanceSnapshot provenance;
        QStringList loadErrors;
        QStringList diagnostics;
        AdapterAuthorizationStatus authorizationStatus;
        QList<Core::ProviderStartupDiagnostic> startupDiagnostics;
    };
    const auto evaluate = [&] {
        AdapterPackageRepository
            resultRepository(packageRootPath, authorizationRootPath, trustRootPath);
        Evaluation result;
        result.available = resultRepository.isAvailable();
        result.v4 = *resultRepository.adapterManifest(unsignedV4->id, unsignedV4->version);
        result.v3 = *resultRepository.adapterManifest(unsignedV3->id, unsignedV3->version);
        result.loadErrors = resultRepository.loadErrors();
        result.diagnostics = resultRepository.authorizationDiagnostics();
        result.authorizationStatus = resultRepository.authorizationStatus();
        result.startupDiagnostics = resultRepository.startupDiagnostics();
        auto *source
            = qobject_cast<Core::DeviceAdapterAuthorizationProvenanceSource *>(
                &resultRepository);
        if (source) {
            result.provenance = source->authorizationProvenanceSnapshot();
            result.validatesCurrent = source->validateCurrent(result.provenance);
        }
        return result;
    };

    const QJsonObject v4Binding = authorizationBinding(*unsignedV4);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            v4Binding,
            "embed-labs.test.v4.allow"),
        authorizationDomainV2);
    Evaluation result = evaluate();
    QVERIFY2(result.diagnostics.isEmpty(), qPrintable(result.diagnostics.join('\n')));
    QVERIFY(result.v4.signatureVerified);
    QVERIFY(result.v4.realHardwareAllowed);
    QVERIFY(!result.v3.signatureVerified);
    QVERIFY(result.validatesCurrent);
    QVERIFY(result.provenance.isValid());
    QCOMPARE(
        result.provenance.state,
        Data::DeviceAdapterAuthorizationProvenanceState::Authorized);
    QCOMPARE(result.provenance.records.size(), 1);
    const Data::DeviceAdapterAuthorizationProvenance v2Provenance
        = result.provenance.records.constFirst();
    QVERIFY(v2Provenance.isValid());
    QCOMPARE(
        v2Provenance.authorizationVersion, Data::DeviceAdapterAuthorizationVersion::V2);
    QCOMPARE(v2Provenance.adapterContractVersion, Data::DeviceAdapterContractVersion::V4);
    const auto canonicalV2Binding = Data::canonicalDeviceAdapterAuthorizationBinding(
        result.v4, Data::DeviceAdapterAuthorizationVersion::V2);
    QVERIFY(canonicalV2Binding);
    QVERIFY(canonicalV2Binding->isValid());
    QCOMPARE(
        canonicalV2Binding->exactBytes,
        QJsonDocument(v4Binding).toJson(QJsonDocument::Compact));
    QCOMPARE(v2Provenance.adapterBindingSha256, canonicalV2Binding->sha256);
    QVERIFY(!Data::canonicalDeviceAdapterAuthorizationBinding(
        result.v4, Data::DeviceAdapterAuthorizationVersion::V1));

    Data::DeviceAdapterManifest canonicalEdgeManifest = result.v4;
    QString canonicalEdgeText;
    canonicalEdgeText.append(QChar(0x007f));
    canonicalEdgeText.append(QChar(0x4e2d));
    canonicalEdgeText.append(QChar(0xd83d));
    canonicalEdgeText.append(QChar(0xde00));
    canonicalEdgeText.append(QChar(0xd800));
    canonicalEdgeText.append(QLatin1Char('x'));
    canonicalEdgeText.append(QChar(0xdc00));
    canonicalEdgeManifest.controlActions[0].disabledReason = canonicalEdgeText;
    const auto canonicalEdgeBinding = Data::canonicalDeviceAdapterAuthorizationBinding(
        canonicalEdgeManifest, Data::DeviceAdapterAuthorizationVersion::V2);
    QVERIFY(canonicalEdgeBinding);
    QVERIFY(canonicalEdgeBinding->isValid());
    QVERIFY(canonicalEdgeBinding->exactBytes.contains(
        QByteArrayLiteral("\\u007f\\u4e2d\\ud83d\\ude00\\ud800x\\udc00")));

    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            v4Binding,
            "embed-labs.test.v4.wrong-domain"),
        authorizationDomainV1);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("signature is invalid"));

    QJsonObject v1Binding = v4Binding;
    v1Binding.remove("schemaVersion");
    v1Binding.remove("parameterDefinitions");
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v1",
            1,
            v1Binding,
            "embed-labs.test.v1-to-v4"),
        authorizationDomainV1);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));

    QJsonObject v3Binding = authorizationBinding(*unsignedV3);
    v3Binding.insert("schemaVersion", "embed-labs.device-adapter/v4");
    v3Binding.insert(
        "parameterDefinitions",
        QJsonArray{QJsonObject{
            {"id", "test.v3.synthetic-parameter"},
            {"definitionSha256", QString(64, QLatin1Char('1'))},
        }});
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            v3Binding,
            "embed-labs.test.v2-to-v3"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v3.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));

    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            1,
            v4Binding,
            "embed-labs.test.v2-format-v1"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidDocument);
    QVERIFY(result.diagnostics.join('\n').contains("formatVersion must equal 2"));

    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v1",
            2,
            v1Binding,
            "embed-labs.test.v1-format-v2"),
        authorizationDomainV1);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidDocument);
    QVERIFY(result.diagnostics.join('\n').contains("formatVersion must equal 1"));

    QJsonObject staleContentBinding = v4Binding;
    staleContentBinding.insert("contentSha256", QString(64, QLatin1Char('1')));
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            staleContentBinding,
            "embed-labs.test.v4.stale-content"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not match"));

    QJsonObject staleVersionBinding = v4Binding;
    staleVersionBinding.insert("version", "0.4.0-stale");
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            staleVersionBinding,
            "embed-labs.test.v4.stale-version"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not match"));

    QJsonObject missingClosureBinding = v4Binding;
    QJsonArray closure = missingClosureBinding.value("parameterDefinitions").toArray();
    closure.removeLast();
    missingClosureBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            missingClosureBinding,
            "embed-labs.test.v4.missing-closure"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));

    QJsonObject staleDefinitionBinding = v4Binding;
    closure = staleDefinitionBinding.value("parameterDefinitions").toArray();
    QJsonObject closureItem = closure.at(0).toObject();
    closureItem.insert("definitionSha256", QString(64, '1'));
    closure.replace(0, closureItem);
    staleDefinitionBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            staleDefinitionBinding,
            "embed-labs.test.v4.stale-definition"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));

    QJsonObject duplicateClosureBinding = v4Binding;
    closure = duplicateClosureBinding.value("parameterDefinitions").toArray();
    closure.insert(1, closure.at(0));
    duplicateClosureBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            duplicateClosureBinding,
            "embed-labs.test.v4.duplicate-definition"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("strict closure"));

    QJsonObject reversedClosureBinding = v4Binding;
    closure = reversedClosureBinding.value("parameterDefinitions").toArray();
    const QJsonValue closureFirst = closure.at(0);
    closure.replace(0, closure.at(1));
    closure.replace(1, closureFirst);
    reversedClosureBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            reversedClosureBinding,
            "embed-labs.test.v4.reversed-definitions"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("strict closure"));

    QJsonObject uppercaseDigestBinding = v4Binding;
    closure = uppercaseDigestBinding.value("parameterDefinitions").toArray();
    closureItem = closure.at(0).toObject();
    closureItem.insert("definitionSha256", QString(64, 'A'));
    closure.replace(0, closureItem);
    uppercaseDigestBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            uppercaseDigestBinding,
            "embed-labs.test.v4.uppercase-definition-digest"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidDocument);
    QVERIFY(result.diagnostics.join('\n').contains("must be lowercase hexadecimal"));

    QJsonObject unknownClosureFieldBinding = v4Binding;
    closure = unknownClosureFieldBinding.value("parameterDefinitions").toArray();
    closureItem = closure.at(0).toObject();
    closureItem.insert("unexpected", true);
    closure.replace(0, closureItem);
    unknownClosureFieldBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            unknownClosureFieldBinding,
            "embed-labs.test.v4.unknown-definition-field"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidDocument);
    QVERIFY(result.diagnostics.join('\n').contains("invalid field set"));

    QJsonObject renamedClosureBinding = v4Binding;
    closure = renamedClosureBinding.value("parameterDefinitions").toArray();
    closureItem = closure.at(0).toObject();
    closureItem.insert("id", "aaa.test.renamed-parameter");
    closure.replace(0, closureItem);
    renamedClosureBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            renamedClosureBinding,
            "embed-labs.test.v4.renamed-definition"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));

    QJsonObject addedClosureBinding = v4Binding;
    closure = addedClosureBinding.value("parameterDefinitions").toArray();
    closure.append(QJsonObject{
        {"id", "zz.test.added-parameter"},
        {"definitionSha256", QString(64, '1')},
    });
    addedClosureBinding.insert("parameterDefinitions", closure);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            addedClosureBinding,
            "embed-labs.test.v4.added-definition"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(result.diagnostics.join('\n').contains("does not exactly match"));

    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            v4Binding,
            "embed-labs.test.v4.allow-with-invalid-neighbor"),
        authorizationDomainV2);
    const QString invalidNeighborPath = authorizationRoot
                                        + "/authorizations/zz-invalid.authorization.json";
    QVERIFY(writeSignedAuthorizationDocument(
        invalidNeighborPath,
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            unknownClosureFieldBinding,
            "embed-labs.test.v4.invalid-neighbor"),
        signerKey,
        authorizationDomainV2));
    result = evaluate();
    QVERIFY(!result.v4.signatureVerified);
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(!result.v3.signatureVerified);
    QVERIFY(!result.v3.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(result.authorizationStatus.authorizedAdapterCount, 0);
    QCOMPARE(result.authorizationStatus.firstFailure, AdapterAuthorizationFailure::InvalidDocument);
    QVERIFY(result.diagnostics.join('\n').contains("invalid field set"));

    QString invalidNeighborSignaturePath = invalidNeighborPath;
    invalidNeighborSignaturePath.chop(5);
    invalidNeighborSignaturePath.append(".sig");
    QVERIFY(QFile::remove(invalidNeighborPath));
    QVERIFY(QFile::remove(invalidNeighborSignaturePath));

    QJsonObject injectedProvenancePackage = sv630nV4Fixture();
    injectedProvenancePackage.insert(
        "authorizationProvenance",
        QJsonObject{{"authorizationVersion", 2}, {"authorizationId", "forged"}});
    const QString malformedNeighborPath = packageRoot + "/zz-malformed.adapter.json";
    QVERIFY(writePackage(malformedNeighborPath, injectedProvenancePackage));
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            v4Binding,
            "embed-labs.test.v4.allow-with-malformed-adapter-neighbor"),
        authorizationDomainV2);
    result = evaluate();
    QVERIFY(!result.available);
    QVERIFY(result.loadErrors.join('\n').contains("authorizationProvenance"));
    QVERIFY(!result.v4.signatureVerified);
    QVERIFY(!result.v4.realHardwareAllowed);
    QVERIFY(!result.v3.signatureVerified);
    QVERIFY(!result.v3.realHardwareAllowed);
    QCOMPARE(result.authorizationStatus.state, AdapterAuthorizationState::ValidationFailed);
    QCOMPARE(
        result.authorizationStatus.firstFailure,
        AdapterAuthorizationFailure::InvalidAdapterPackage);
    QVERIFY(result.authorizationStatus.validationFailureCount > 0);
    QCOMPARE(
        result.provenance.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QVERIFY(result.provenance.records.isEmpty());
    QCOMPARE(result.startupDiagnostics.size(), 1);
    QVERIFY(result.startupDiagnostics.constFirst().message.contains("invalid or unreadable"));
    QVERIFY(QFile::remove(malformedNeighborPath));

    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            v4Binding,
            "embed-labs.test.v4.validate-current-package-tree"),
        authorizationDomainV2);
    AdapterPackageRepository stableRepository(
        packageRootPath, authorizationRootPath, trustRootPath);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot stableSnapshot
        = stableRepository.authorizationProvenanceSnapshot();
    const QList<Data::DeviceAdapterManifest> stableManifests
        = stableRepository.adapterManifests();
    const AdapterAuthorizationStatus stableStatus = stableRepository.authorizationStatus();
    QVERIFY(stableSnapshot.isValid());
    QVERIFY(stableRepository.validateCurrent(stableSnapshot));
    const auto stableV4
        = stableRepository.adapterManifest(unsignedV4->id, unsignedV4->version);
    QVERIFY(stableV4);
    QVERIFY(stableRepository.validateCurrent(stableSnapshot, *stableV4));
    const auto stableBinding = Data::canonicalDeviceAdapterAuthorizationBinding(
        *stableV4, Data::DeviceAdapterAuthorizationVersion::V2);
    QVERIFY(stableBinding);

    Data::DeviceAdapterManifest changedIndex = *stableV4;
    QVERIFY(changedIndex.parameterDefinitions.constFirst().configuredProjection.object);
    QVERIFY(changedIndex.parameterDefinitions.constFirst().observedSource.object);
    changedIndex.parameterDefinitions[0].configuredProjection.object->index = 0x6092;
    changedIndex.parameterDefinitions[0].observedSource.object->index = 0x6092;
    const auto changedIndexBinding = Data::canonicalDeviceAdapterAuthorizationBinding(
        changedIndex, Data::DeviceAdapterAuthorizationVersion::V2);
    QVERIFY(changedIndexBinding);
    QCOMPARE(changedIndexBinding->sha256, stableBinding->sha256);
    QVERIFY(!stableRepository.validateCurrent(stableSnapshot, changedIndex));

    Data::DeviceAdapterManifest changedScale = *stableV4;
    QVERIFY(changedScale.parameterDefinitions.at(3).configuredProjection.object);
    QVERIFY(changedScale.parameterDefinitions.at(3).observedSource.object);
    changedScale.parameterDefinitions[3]
        .configuredProjection.object->engineeringTransform.scale = {2, 1};
    changedScale.parameterDefinitions[3]
        .observedSource.object->engineeringTransform.scale = {2, 1};
    const auto changedScaleBinding = Data::canonicalDeviceAdapterAuthorizationBinding(
        changedScale, Data::DeviceAdapterAuthorizationVersion::V2);
    QVERIFY(changedScaleBinding);
    QCOMPARE(changedScaleBinding->sha256, stableBinding->sha256);
    QVERIFY(!stableRepository.validateCurrent(stableSnapshot, changedScale));
    QCOMPARE(stableRepository.authorizationProvenanceSnapshot(), stableSnapshot);
    QCOMPARE(stableRepository.adapterManifests(), stableManifests);
    QCOMPARE(stableRepository.authorizationStatus().state, stableStatus.state);

    const QString emptyPackageRoot = temporaryDirectory.path() + "/empty-packages";
    QVERIFY(QDir().mkpath(emptyPackageRoot));
    AdapterPackageRepository emptyRepository(
        Utils::FilePath::fromString(emptyPackageRoot), authorizationRootPath, trustRootPath);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot emptySnapshot
        = emptyRepository.authorizationProvenanceSnapshot();
    QVERIFY(emptySnapshot.isValid());
    QCOMPARE(
        emptySnapshot.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QCOMPARE(emptySnapshot.authorizationSetSha256.size(), 32);
    QVERIFY(emptySnapshot.records.isEmpty());
    QVERIFY(emptyRepository.validateCurrent(emptySnapshot));
    QVERIFY(!emptyRepository.validateCurrent(emptySnapshot, *stableV4));

    const QString invalidPackageRoot = temporaryDirectory.path() + "/invalid-packages";
    QVERIFY(QDir().mkpath(invalidPackageRoot));
    QVERIFY(writeBytes(
        invalidPackageRoot + "/broken.adapter.json", QByteArrayLiteral("not-json\n")));
    AdapterPackageRepository invalidRepository(
        Utils::FilePath::fromString(invalidPackageRoot), authorizationRootPath, trustRootPath);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot invalidSnapshot
        = invalidRepository.authorizationProvenanceSnapshot();
    QVERIFY(invalidSnapshot.isValid());
    QCOMPARE(
        invalidSnapshot.state,
        Data::DeviceAdapterAuthorizationProvenanceState::ValidationFailed);
    QCOMPARE(invalidSnapshot.authorizationSetSha256.size(), 32);
    QVERIFY(invalidSnapshot.records.isEmpty());
    QVERIFY(invalidRepository.validateCurrent(invalidSnapshot));

    const QString v4PackagePath = packageRoot + "/sv630n-v4.adapter.json";
    const Utils::Result<QByteArray> originalV4Package
        = Utils::FilePath::fromString(v4PackagePath).fileContents();
    QVERIFY_RESULT(originalV4Package);
    QVERIFY(writeBytes(v4PackagePath, QByteArrayLiteral("not-json\n")));
    QVERIFY(!stableRepository.validateCurrent(stableSnapshot));
    QCOMPARE(stableRepository.authorizationProvenanceSnapshot(), stableSnapshot);
    QCOMPARE(stableRepository.adapterManifests(), stableManifests);
    QCOMPARE(stableRepository.authorizationStatus().state, stableStatus.state);
    QCOMPARE(
        stableRepository.authorizationStatus().authorizedAdapterCount,
        stableStatus.authorizedAdapterCount);
    QVERIFY(writeBytes(v4PackagePath, *originalV4Package));
    QVERIFY(stableRepository.validateCurrent(stableSnapshot));
    QVERIFY(QFile::remove(v4PackagePath));
    QVERIFY(!stableRepository.validateCurrent(stableSnapshot));
    QCOMPARE(stableRepository.authorizationProvenanceSnapshot().generation, stableSnapshot.generation);
    QCOMPARE(stableRepository.adapterManifests(), stableManifests);
    QVERIFY(writeBytes(v4PackagePath, *originalV4Package));
    QVERIFY(stableRepository.validateCurrent(stableSnapshot));

    QString unusualVersion(300, QLatin1Char('v'));
    unusualVersion.prepend(QChar(0x0001));
    unusualVersion.prepend(QLatin1Char(' '));
    QJsonObject unusualVersionFixture = sv630nV4Fixture();
    unusualVersionFixture.insert("version", unusualVersion);
    QVERIFY(writePackage(packageRoot + "/sv630n-v4.adapter.json", unusualVersionFixture));
    AdapterPackageRepository unusualUnsignedRepository(
        packageRootPath, authorizationRootPath, trustRootPath);
    const auto unusualUnsigned = unusualUnsignedRepository.adapterManifest(
        unsignedV4->id, unusualVersion);
    QVERIFY(unusualUnsigned);
    const QJsonObject unusualBinding = authorizationBinding(*unusualUnsigned);
    install(
        authorization(
            "embed-labs.ethercat-device-adapter-authorization-v2",
            2,
            unusualBinding,
            "embed-labs.test.v4.unusual-version"),
        authorizationDomainV2);
    AdapterPackageRepository unusualAuthorizedRepository(
        packageRootPath, authorizationRootPath, trustRootPath);
    const auto unusualAuthorized = unusualAuthorizedRepository.adapterManifest(
        unsignedV4->id, unusualVersion);
    QVERIFY(unusualAuthorized);
    QVERIFY(unusualAuthorized->signatureVerified);
    QVERIFY(unusualAuthorized->realHardwareAllowed);
    const Data::DeviceAdapterAuthorizationProvenanceSnapshot unusualSnapshot
        = unusualAuthorizedRepository.authorizationProvenanceSnapshot();
    QVERIFY(unusualSnapshot.isValid());
    QCOMPARE(
        unusualSnapshot.state,
        Data::DeviceAdapterAuthorizationProvenanceState::Authorized);
    QCOMPARE(unusualSnapshot.records.size(), 1);
    QCOMPARE(unusualSnapshot.records.constFirst().adapterVersion, unusualVersion);
    QVERIFY(unusualAuthorizedRepository.validateCurrent(unusualSnapshot));
}

void EtherCATDeviceAdaptersTests::testV1RemainsFailClosed()
{
    const Utils::FilePath bundledPath = ::Core::ICore::resourcePath(
        "ethercat/adapters/v1/inovance-sv630n-rev00010000.adapter.json");
    const Utils::Result<QByteArray> bundledContents = bundledPath.fileContents();
    QVERIFY_RESULT(bundledContents);
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(sv630n);
    QCOMPARE(sv630n->contractVersion, Data::DeviceAdapterContractVersion::V1);
    QVERIFY(
        std::all_of(
            sv630n->processDataProfiles.cbegin(),
            sv630n->processDataProfiles.cend(),
            [](const Data::ProcessDataProfile &profile) {
                return profile.signedDcProfileId.isEmpty();
            }));
    QCOMPARE(
        sv630n->contentSha256,
        QCryptographicHash::hash(*bundledContents, QCryptographicHash::Sha256));
    QVERIFY(
        std::all_of(
            sv630n->semanticSignals.cbegin(),
            sv630n->semanticSignals.cend(),
            [](const Data::SemanticSignalDefinition &signal) {
                return !signal.engineeringTransform;
            }));

    const QJsonDocument document = QJsonDocument::fromJson(*bundledContents);
    QVERIFY(document.isObject());
    QString error;
    const QByteArray compact = QJsonDocument(document.object()).toJson(QJsonDocument::Compact);
    const QByteArray compactDigest = packageDigest(compact, &error);
    QVERIFY2(!compactDigest.isEmpty(), qPrintable(error));
    QCOMPARE(compactDigest, QCryptographicHash::hash(compact, QCryptographicHash::Sha256));
    QVERIFY(compactDigest != sv630n->contentSha256);

    const Data::SemanticSignalDefinition *controlword
        = signalForObject(*sv630n, Data::PdoDirection::Rx, 0x1702, 0x6040);
    QVERIFY(controlword);
    Data::ManualSignalEnvelope signalEnvelope;
    signalEnvelope.signalId = controlword->id;
    Data::ManualControlEnvelope manualSignal;
    manualSignal.signalEnvelopes.append(signalEnvelope);
    QCOMPARE(
        Core::validateManualControlEnvelope(
            manualSignal, sv630n->semanticSignals, sv630n->controlActions)
            .error,
        Core::ManualControlContractError::ExactTransformMissing);

    Data::ManualActionEnvelope actionEnvelope;
    actionEnvelope.actionId = sv630n->controlActions.constFirst().id;
    actionEnvelope.holdToRun = sv630n->controlActions.constFirst().holdToRun;
    Data::ManualControlEnvelope manualAction;
    manualAction.actionEnvelopes.append(actionEnvelope);
    QCOMPARE(
        Core::validateManualControlEnvelope(
            manualAction, sv630n->semanticSignals, sv630n->controlActions)
            .error,
        Core::ManualControlContractError::InexactActionDefinition);
}

void EtherCATDeviceAdaptersTests::testV1AndV2ExactSelection()
{
    AdapterPackageRepository repository(adaptersRoot());
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    const Data::DeviceAdapterId adapterId{
        "org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000",
    };
    const std::optional<Data::DeviceAdapterManifest> v1
        = repository.adapterManifest(adapterId, "0.1.0");
    const std::optional<Data::DeviceAdapterManifest> v2
        = repository.adapterManifest(adapterId, "0.2.0");
    QVERIFY(v1);
    QVERIFY(v2);
    QVERIFY(v1->contentSha256 != v2->contentSha256);

    Data::DeviceAdapterResolutionRequest automatic = sv630nRequest(*v2);
    const Data::DeviceAdapterResolutionResult automaticResult = repository.resolveDevice(automatic);
    QVERIFY2(automaticResult.resolved, qPrintable(automaticResult.error));
    QCOMPARE(automaticResult.model.adapterVersion, QString("0.2.0"));

    Data::DeviceAdapterResolutionRequest exactV1 = automatic;
    exactV1.expectedAdapterId = v1->id;
    exactV1.expectedAdapterVersion = v1->version;
    exactV1.expectedAdapterContentSha256 = v1->contentSha256;
    const Data::DeviceAdapterResolutionResult v1Result = repository.resolveDevice(exactV1);
    QVERIFY2(v1Result.resolved, qPrintable(v1Result.error));
    QCOMPARE(v1Result.model.adapterVersion, QString("0.1.0"));

    Data::DeviceAdapterResolutionRequest exactV2 = automatic;
    exactV2.expectedAdapterId = v2->id;
    exactV2.expectedAdapterVersion = v2->version;
    exactV2.expectedAdapterContentSha256 = v2->contentSha256;
    const Data::DeviceAdapterResolutionResult v2Result = repository.resolveDevice(exactV2);
    QVERIFY2(v2Result.resolved, qPrintable(v2Result.error));
    QCOMPARE(v2Result.model.adapterVersion, QString("0.2.0"));
}

void EtherCATDeviceAdaptersTests::testV3AndV4ExactSelection()
{
    AdapterPackageRepository repository(adaptersRoot());
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    const Data::DeviceAdapterId adapterId{
        "org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000",
    };
    const auto v3 = repository.adapterManifest(adapterId, "0.3.0");
    const auto v4 = repository.adapterManifest(adapterId, "0.4.0");
    QVERIFY(v3);
    QVERIFY(v4);
    QCOMPARE(v3->contractVersion, Data::DeviceAdapterContractVersion::V3);
    QCOMPARE(v4->contractVersion, Data::DeviceAdapterContractVersion::V4);
    QVERIFY(v3->contentSha256 != v4->contentSha256);

    Data::DeviceAdapterResolutionRequest automatic = sv630nApi038Request(*v4);
    QVERIFY(!automatic.processDataProfileId.isEmpty());
    const Data::DeviceAdapterResolutionResult automaticResult = repository.resolveDevice(automatic);
    QVERIFY(!automaticResult.resolved);
    QVERIFY(automaticResult.error.contains("ambiguous at the highest priority"));

    Data::DeviceAdapterResolutionRequest exactV3 = automatic;
    exactV3.expectedAdapterId = v3->id;
    exactV3.expectedAdapterVersion = v3->version;
    exactV3.expectedAdapterContentSha256 = v3->contentSha256;
    const Data::DeviceAdapterResolutionResult v3Result = repository.resolveDevice(exactV3);
    QVERIFY2(v3Result.resolved, qPrintable(v3Result.error));
    QCOMPARE(v3Result.model.adapterVersion, QString("0.3.0"));
    QCOMPARE(v3Result.model.adapterContentSha256, v3->contentSha256);

    Data::DeviceAdapterResolutionRequest exactV4 = automatic;
    exactV4.expectedAdapterId = v4->id;
    exactV4.expectedAdapterVersion = v4->version;
    exactV4.expectedAdapterContentSha256 = v4->contentSha256;
    const Data::DeviceAdapterResolutionResult v4Result = repository.resolveDevice(exactV4);
    QVERIFY2(v4Result.resolved, qPrintable(v4Result.error));
    QCOMPARE(v4Result.model.adapterVersion, QString("0.4.0"));
    QCOMPARE(v4Result.model.adapterContentSha256, v4->contentSha256);
}

void EtherCATDeviceAdaptersTests::testExactIdentityAndEsiMatching()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(sv630n);
    Data::DeviceAdapterResolutionRequest request = sv630nRequest(*sv630n);
    QVERIFY(!request.processDataProfileId.isEmpty());
    QVERIFY(repository.resolveDevice(request).resolved);

    Data::DeviceAdapterResolutionRequest mismatch = request;
    ++mismatch.device.summary.identity.vendorId;
    QVERIFY(!repository.resolveDevice(mismatch).resolved);
    mismatch = request;
    ++mismatch.device.summary.identity.productCode;
    QVERIFY(!repository.resolveDevice(mismatch).resolved);
    mismatch = request;
    ++mismatch.device.summary.identity.revisionNumber;
    QVERIFY(!repository.resolveDevice(mismatch).resolved);
    mismatch = request;
    mismatch.device.sourceSha256[0] = char(mismatch.device.sourceSha256.at(0) ^ char(0xff));
    QVERIFY(!repository.resolveDevice(mismatch).resolved);
}

void EtherCATDeviceAdaptersTests::testExactPackageSelection()
{
    const Utils::FilePath bundledPath = ::Core::ICore::resourcePath(
        "ethercat/adapters/v1/inovance-sv630n-rev00010000.adapter.json");
    const Utils::Result<QByteArray> bundledContents = bundledPath.fileContents();
    QVERIFY_RESULT(bundledContents);
    const QJsonDocument bundledDocument = QJsonDocument::fromJson(*bundledContents);
    QVERIFY(bundledDocument.isObject());

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QJsonObject oldPackage = bundledDocument.object();
    oldPackage.insert("version", "0.1.0");
    oldPackage.insert("matchPriority", 100);
    QVERIFY(writePackage(temporaryDirectory.path() + "/old.adapter.json", oldPackage));
    QJsonObject newPackage = oldPackage;
    newPackage.insert("version", "0.2.0");
    newPackage.insert("matchPriority", 200);
    QVERIFY(writePackage(temporaryDirectory.path() + "/new.adapter.json", newPackage));

    AdapterPackageRepository repository(Utils::FilePath::fromString(temporaryDirectory.path()));
    QVERIFY2(repository.isAvailable(), qPrintable(repository.loadErrors().join('\n')));
    const Data::DeviceAdapterId adapterId{
        oldPackage.value("id").toString(),
    };
    const std::optional<Data::DeviceAdapterManifest> oldManifest
        = repository.adapterManifest(adapterId, "0.1.0");
    const std::optional<Data::DeviceAdapterManifest> newManifest
        = repository.adapterManifest(adapterId, "0.2.0");
    QVERIFY(oldManifest);
    QVERIFY(newManifest);
    QVERIFY(oldManifest->contentSha256 != newManifest->contentSha256);

    Data::DeviceAdapterResolutionRequest automatic = sv630nRequest(*oldManifest);
    const Data::DeviceAdapterResolutionResult automaticResult = repository.resolveDevice(automatic);
    QVERIFY2(automaticResult.resolved, qPrintable(automaticResult.error));
    QCOMPARE(automaticResult.model.adapterVersion, QString("0.2.0"));

    Data::DeviceAdapterResolutionRequest exact = automatic;
    exact.expectedAdapterId = oldManifest->id;
    exact.expectedAdapterVersion = oldManifest->version;
    exact.expectedAdapterContentSha256 = oldManifest->contentSha256;
    const Data::DeviceAdapterResolutionResult exactResult = repository.resolveDevice(exact);
    QVERIFY2(exactResult.resolved, qPrintable(exactResult.error));
    QCOMPARE(exactResult.model.adapterId, oldManifest->id);
    QCOMPARE(exactResult.model.adapterVersion, QString("0.1.0"));
    QCOMPARE(exactResult.model.adapterContentSha256, oldManifest->contentSha256);

    Data::DeviceAdapterResolutionRequest contentMismatch = exact;
    contentMismatch.expectedAdapterContentSha256[0] = char(
        contentMismatch.expectedAdapterContentSha256.at(0) ^ char(0xff));
    const Data::DeviceAdapterResolutionResult mismatchResult = repository.resolveDevice(
        contentMismatch);
    QVERIFY(!mismatchResult.resolved);
    QVERIFY(mismatchResult.error.contains("content SHA-256"));

    Data::DeviceAdapterResolutionRequest missingPackage = exact;
    missingPackage.expectedAdapterVersion = "0.0.1";
    const Data::DeviceAdapterResolutionResult missingResult = repository.resolveDevice(
        missingPackage);
    QVERIFY(!missingResult.resolved);
    QVERIFY(missingResult.error.contains("was not found"));

    Data::DeviceAdapterResolutionRequest partial = exact;
    partial.expectedAdapterContentSha256.clear();
    const Data::DeviceAdapterResolutionResult partialResult = repository.resolveDevice(partial);
    QVERIFY(!partialResult.resolved);
    QVERIFY(partialResult.error.contains("either empty or contain"));
}

void EtherCATDeviceAdaptersTests::testCandidateHardwareGate()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    for (const Data::DeviceAdapterManifest &manifest : manifests) {
        QCOMPARE(manifest.qualification, Data::DeviceAdapterQualification::Candidate);
        QVERIFY(!manifest.signatureVerified);
        QVERIFY(!manifest.realHardwareAllowed);
        int outputCount = 0;
        for (const Data::SemanticSignalDefinition &signal : manifest.semanticSignals) {
            if (signal.direction != Data::SemanticSignalDirection::Output)
                continue;
            ++outputCount;
            QVERIFY(!signal.manualControl.allowed);
            QVERIFY(!signal.hasSafeValue);
        }
        for (const Data::DeviceModuleProfile &profile : manifest.moduleProfiles) {
            for (const Data::SemanticSignalDefinition &signal : profile.slotRelativeSignals) {
                if (signal.direction != Data::SemanticSignalDirection::Output)
                    continue;
                ++outputCount;
                QVERIFY(!signal.manualControl.allowed);
                QVERIFY(!signal.hasSafeValue);
            }
        }
        QVERIFY(outputCount > 0);
        QVERIFY(
            std::all_of(
                manifest.controlActions.cbegin(),
                manifest.controlActions.cend(),
                [](const Data::DeviceControlAction &action) { return !action.enabled; }));
    }

    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(sv630n);
    Data::DeviceAdapterResolutionRequest request = sv630nRequest(*sv630n);
    request.allowCandidate = false;
    QVERIFY(!repository.resolveDevice(request).resolved);
    request.allowCandidate = true;
    request.requireRealHardwareQualification = true;
    QVERIFY(!repository.resolveDevice(request).resolved);
}

void EtherCATDeviceAdaptersTests::testSv630nProcessImageBinding()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(sv630n);
    Data::DeviceAdapterResolutionRequest request = sv630nRequest(*sv630n);

    // An excluded/default PDO containing the same object must not override the selected profile.
    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Rx,
            0x1701,
            0x6040,
            0,
            256,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    const Data::DeviceAdapterResolutionResult result = repository.resolveDevice(request);
    QVERIFY2(result.resolved, qPrintable(result.error));
    QVERIFY(result.model.complete);
    QCOMPARE(result.model.processDataProfileId, request.processDataProfileId);
    const Data::BoundSemanticSignal *controlword
        = boundSignalForObject(result.model, Data::PdoDirection::Rx, 0x1702, 0x6040);
    const Data::BoundSemanticSignal *targetVelocity
        = boundSignalForObject(result.model, Data::PdoDirection::Rx, 0x1702, 0x60ff);
    const Data::BoundSemanticSignal *actualVelocity
        = boundSignalForObject(result.model, Data::PdoDirection::Tx, 0x1b04, 0x606c);
    QVERIFY(controlword);
    QVERIFY(targetVelocity);
    QVERIFY(actualVelocity);
    QCOMPARE(controlword->processImageBitOffset, qint64(0));
    QCOMPARE(targetVelocity->processImageBitOffset, qint64(48));
    QCOMPARE(actualVelocity->processImageBitOffset, qint64(200));
    QVERIFY(!boundSignalForObject(result.model, Data::PdoDirection::Rx, 0x1701, 0x6040));
}

void EtherCATDeviceAdaptersTests::testSv630nRejectsInvalidProcessImages()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(sv630n);
    const Data::DeviceAdapterResolutionRequest valid = sv630nRequest(*sv630n);

    Data::DeviceAdapterResolutionRequest missing = valid;
    auto &missingEntries = missing.processImage.outputs.entries;
    missingEntries.erase(
        std::remove_if(
            missingEntries.begin(),
            missingEntries.end(),
            [](const Data::ProcessImageEntry &entry) { return entry.index == 0x60ff; }),
        missingEntries.end());
    QVERIFY(!repository.resolveDevice(missing).resolved);

    Data::DeviceAdapterResolutionRequest duplicate = valid;
    Data::ProcessImageEntry duplicateVelocity = *std::find_if(
        duplicate.processImage.outputs.entries.cbegin(),
        duplicate.processImage.outputs.entries.cend(),
        [](const Data::ProcessImageEntry &entry) { return entry.index == 0x60ff; });
    duplicateVelocity.entryId = Data::NodeId::create();
    duplicate.processImage.outputs.entries.append(duplicateVelocity);
    QVERIFY(!repository.resolveDevice(duplicate).resolved);

    Data::DeviceAdapterResolutionRequest wrongType = valid;
    auto actualVelocity = std::find_if(
        wrongType.processImage.inputs.entries.begin(),
        wrongType.processImage.inputs.entries.end(),
        [](const Data::ProcessImageEntry &entry) { return entry.index == 0x606c; });
    QVERIFY(actualVelocity != wrongType.processImage.inputs.entries.end());
    actualVelocity->dataType = Data::EtherCATDataType::UnsignedInteger32;
    QVERIFY(!repository.resolveDevice(wrongType).resolved);

    Data::DeviceAdapterResolutionRequest wrongWidth = valid;
    auto statusword = std::find_if(
        wrongWidth.processImage.inputs.entries.begin(),
        wrongWidth.processImage.inputs.entries.end(),
        [](const Data::ProcessImageEntry &entry) { return entry.index == 0x6041; });
    QVERIFY(statusword != wrongWidth.processImage.inputs.entries.end());
    statusword->bitLength = 32;
    QVERIFY(!repository.resolveDevice(wrongWidth).resolved);
}

void EtherCATDeviceAdaptersTests::testSv630nManualActionsRemainDisabled()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(sv630n);
    const Data::SemanticSignalDefinition *controlword
        = signalForObject(*sv630n, Data::PdoDirection::Rx, 0x1702, 0x6040);
    QVERIFY(controlword);
    QVERIFY(!controlword->manualControl.allowed);
    QVERIFY(!sv630n->controlActions.isEmpty());
    for (const Data::DeviceControlAction &action : sv630n->controlActions) {
        QVERIFY(!action.enabled);
        QVERIFY(action.requiresExclusiveControl);
        QVERIFY(action.requiresDc);
        QCOMPARE(action.commandTtlMs, quint32(0));
        QCOMPARE(action.failureAction, Data::ManualControlTimeoutAction::ControlledStop);
        QCOMPARE(action.timeoutAction, Data::ManualControlTimeoutAction::ControlledStop);
    }
}

static Data::DeviceAdapterResolutionRequest xb6Request(
    int firstSlot = 1, quint32 firstModuleIdent = 0x00000625)
{
    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device = device(xb6Identity, xb6EsiSha256);
    request.allowCandidate = true;
    request.processDataProfileId = "solidot.xb6.mdp-rx16ff-tx1aff";
    request.moduleAssignments = {
        {firstSlot, firstModuleIdent, quint16(firstSlot * 0x10), quint16(firstSlot)},
        {2, 0x00000629, 0x0020, 0x0002},
    };

    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Rx,
            0x16ff,
            0xf200,
            1,
            256,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Tx,
            0x1aff,
            0xf100,
            1,
            256,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    for (int channel = 1; channel <= 16; ++channel) {
        appendProcessEntry(
            &request.processImage,
            processEntry(
                Data::PdoDirection::Rx,
                quint16(0x1600 + firstSlot),
                quint16(0x7000 + firstSlot * 0x10),
                quint8(channel),
                channel - 1,
                1,
                Data::EtherCATDataType::Boolean));
        appendProcessEntry(
            &request.processImage,
            processEntry(
                Data::PdoDirection::Tx,
                0x1a02,
                0x6020,
                quint8(channel),
                channel - 1,
                1,
                Data::EtherCATDataType::Boolean));
    }
    return request;
}

static Data::DeviceAdapterResolutionRequest xb6SingleModuleRequest(quint32 moduleIdent, int slot = 1)
{
    const bool outputModule = moduleIdent == 0x00000624 || moduleIdent == 0x00000625;
    const Data::PdoDirection direction = outputModule ? Data::PdoDirection::Rx
                                                      : Data::PdoDirection::Tx;
    const quint16 pdoBase = outputModule ? 0x1600 : 0x1a00;
    const quint16 objectBase = outputModule ? 0x7000 : 0x6000;

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device = device(xb6Identity, xb6EsiSha256);
    request.allowCandidate = true;
    request.processDataProfileId = "solidot.xb6.mdp-rx16ff-tx1aff";
    request.moduleAssignments = {
        {slot, moduleIdent, quint16(slot * 0x10), quint16(slot)},
    };
    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Rx,
            0x16ff,
            0xf200,
            1,
            256,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Tx,
            0x1aff,
            0xf100,
            1,
            256,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    for (int channel = 1; channel <= 16; ++channel) {
        appendProcessEntry(
            &request.processImage,
            processEntry(
                direction,
                quint16(pdoBase + slot),
                quint16(objectBase + slot * 0x10),
                quint8(channel),
                channel - 1,
                1,
                Data::EtherCATDataType::Boolean));
    }
    return request;
}

static int writableBoundSignalCount(const Data::ResolvedDeviceModel &model)
{
    return int(std::count_if(
        model.boundSignals.cbegin(),
        model.boundSignals.cend(),
        [](const Data::BoundSemanticSignal &signal) {
            return signal.definition.access != Data::SemanticSignalAccess::ReadOnly;
        }));
}

void EtherCATDeviceAdaptersTests::testXb6RequiresDetectedModules()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device = device(xb6Identity, xb6EsiSha256);
    request.allowCandidate = true;
    request.processDataProfileId = "solidot.xb6.mdp-rx16ff-tx1aff";
    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Rx,
            0x16ff,
            0xf200,
            1,
            0,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    appendProcessEntry(
        &request.processImage,
        processEntry(
            Data::PdoDirection::Tx,
            0x1aff,
            0xf100,
            1,
            0,
            16,
            Data::EtherCATDataType::UnsignedInteger16));
    const Data::DeviceAdapterResolutionResult result = repository.resolveDevice(request);
    QVERIFY2(result.resolved, qPrintable(result.error));
    QVERIFY(!result.model.complete);
    QCOMPARE(result.model.processDataProfileId, request.processDataProfileId);
    QCOMPARE(result.model.moduleAssignments.size(), 0);
    QCOMPARE(writableBoundSignalCount(result.model), 0);
}

void EtherCATDeviceAdaptersTests::testXb6ExpandsDo16Modules()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));
    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    const Data::DeviceAdapterManifest *xb6 = manifestForIdentity(manifests, xb6Identity);
    QVERIFY(xb6);
    QCOMPARE(xb6->processDataProfiles.size(), 1);
    QCOMPARE(xb6->processDataProfiles.constFirst().id, QString("solidot.xb6.mdp-rx16ff-tx1aff"));
    QCOMPARE(xb6->processDataProfiles.constFirst().rxPdoIndices, QList<quint16>({0x16ff}));
    QCOMPARE(xb6->processDataProfiles.constFirst().txPdoIndices, QList<quint16>({0x1aff}));
    QCOMPARE(xb6->moduleProfiles.size(), 4);
    const QList<quint32> expectedModuleIdents{0x00000624, 0x00000625, 0x00000628, 0x00000629};
    for (quint32 moduleIdent : expectedModuleIdents) {
        const auto profile = std::find_if(
            xb6->moduleProfiles.cbegin(),
            xb6->moduleProfiles.cend(),
            [moduleIdent](const Data::DeviceModuleProfile &item) {
                return item.moduleIdent == moduleIdent;
            });
        QVERIFY(profile != xb6->moduleProfiles.cend());
        QCOMPARE(profile->slotRelativeSignals.size(), 16);
        QVERIFY(
            std::all_of(
                profile->slotRelativeSignals.cbegin(),
                profile->slotRelativeSignals.cend(),
                [](const Data::SemanticSignalDefinition &signal) {
                    return signal.bindings.size() == 1 && signal.bindings.constFirst().slotRelative;
                }));

        const Data::DeviceAdapterResolutionResult singleModule = repository.resolveDevice(
            xb6SingleModuleRequest(moduleIdent));
        QVERIFY2(singleModule.resolved, qPrintable(singleModule.error));
        QVERIFY(singleModule.model.complete);
        QCOMPARE(singleModule.model.boundSignals.size(), 18);
        QVERIFY(
            std::all_of(
                singleModule.model.boundSignals.cbegin(),
                singleModule.model.boundSignals.cend(),
                [moduleIdent](const Data::BoundSemanticSignal &signal) {
                    return signal.slot < 0 ? signal.moduleIdent == 0
                                           : signal.slot == 1 && signal.moduleIdent == moduleIdent;
                }));
    }

    const Data::DeviceAdapterResolutionResult result = repository.resolveDevice(xb6Request());
    QVERIFY2(result.resolved, qPrintable(result.error));
    QVERIFY(result.model.complete);
    QCOMPARE(result.model.processDataProfileId, QString("solidot.xb6.mdp-rx16ff-tx1aff"));
    QCOMPARE(result.model.moduleAssignments.size(), 2);
    QCOMPARE(result.model.boundSignals.size(), 34);
    QCOMPARE(writableBoundSignalCount(result.model), 17);
    QVERIFY(boundSignalForObject(result.model, Data::PdoDirection::Rx, 0x16ff, 0xf200));
    QVERIFY(boundSignalForObject(result.model, Data::PdoDirection::Tx, 0x1aff, 0xf100));

    QStringList semanticIds;
    for (const Data::BoundSemanticSignal &signal : result.model.boundSignals) {
        semanticIds.append(signal.definition.id.value);
        if (signal.slot < 0) {
            QCOMPARE(signal.moduleIdent, quint32(0));
            continue;
        }
        QVERIFY(signal.slot == 1 || signal.slot == 2);
        if (signal.slot == 1) {
            QCOMPARE(signal.moduleIdent, quint32(0x00000625));
            QCOMPARE(signal.binding.pdoDirection, Data::PdoDirection::Rx);
            QCOMPARE(signal.binding.pdoIndex, quint16(0x1601));
            QCOMPARE(signal.binding.objectIndex, quint16(0x7010));
        } else {
            QCOMPARE(signal.moduleIdent, quint32(0x00000629));
            QCOMPARE(signal.binding.pdoDirection, Data::PdoDirection::Tx);
            QCOMPARE(signal.binding.pdoIndex, quint16(0x1a02));
            QCOMPARE(signal.binding.objectIndex, quint16(0x6020));
        }
        QVERIFY(signal.binding.objectSubIndex >= 1);
        QVERIFY(signal.binding.objectSubIndex <= 16);
    }
    semanticIds.sort();
    QVERIFY(std::adjacent_find(semanticIds.cbegin(), semanticIds.cend()) == semanticIds.cend());
    QVERIFY(semanticIds.contains("org.embedlabs.solidot.xb6.slot.1.digital-output.channel.1"));
    QVERIFY(semanticIds.contains("org.embedlabs.solidot.xb6.slot.1.digital-output.channel.16"));
    QVERIFY(semanticIds.contains("org.embedlabs.solidot.xb6.slot.2.digital-input.channel.1"));
    QVERIFY(semanticIds.contains("org.embedlabs.solidot.xb6.slot.2.digital-input.channel.16"));

    // The structurally identical PNP/NPN profiles remain distinct exact ModuleIdent records.
    Data::DeviceAdapterResolutionRequest npn = xb6Request(1, 0x00000624);
    const Data::DeviceAdapterResolutionResult npnResult = repository.resolveDevice(npn);
    QVERIFY2(npnResult.resolved, qPrintable(npnResult.error));
    QVERIFY(npnResult.model.complete);
    QVERIFY(
        std::all_of(
            npnResult.model.boundSignals.cbegin(),
            npnResult.model.boundSignals.cend(),
            [](const Data::BoundSemanticSignal &signal) {
                return signal.slot != 1 || signal.moduleIdent == 0x00000624;
            }));
}

void EtherCATDeviceAdaptersTests::testXb6RejectsInvalidModuleLayouts()
{
    AdapterPackageRepository repository(::Core::ICore::resourcePath("ethercat/adapters/v1"));

    Data::DeviceAdapterResolutionRequest unknownModule = xb6Request();
    unknownModule.moduleAssignments[0].moduleIdent = 0x00000623; // A 32-channel profile.
    QVERIFY(!repository.resolveDevice(unknownModule).resolved);

    Data::DeviceAdapterResolutionRequest duplicateSlot = xb6Request();
    duplicateSlot.moduleAssignments[1].slot = duplicateSlot.moduleAssignments[0].slot;
    QVERIFY(!repository.resolveDevice(duplicateSlot).resolved);

    Data::DeviceAdapterResolutionRequest missingChannel = xb6Request();
    auto &outputEntries = missingChannel.processImage.outputs.entries;
    outputEntries.erase(
        std::remove_if(
            outputEntries.begin(),
            outputEntries.end(),
            [](const Data::ProcessImageEntry &entry) {
                return entry.pdoIndex == 0x1601 && entry.subIndex == 16;
            }),
        outputEntries.end());
    QVERIFY(!repository.resolveDevice(missingChannel).resolved);

    Data::DeviceAdapterResolutionRequest duplicateChannel = xb6Request();
    Data::ProcessImageEntry duplicate = duplicateChannel.processImage.outputs.entries.constFirst();
    duplicate.entryId = Data::NodeId::create();
    duplicateChannel.processImage.outputs.entries.append(duplicate);
    QVERIFY(!repository.resolveDevice(duplicateChannel).resolved);
}

void EtherCATDeviceAdaptersTests::testProviderRegistryOrdering()
{
    auto *repository = ExtensionSystem::PluginManager::getObject<AdapterPackageRepository>();
    QVERIFY(repository);
    QVERIFY(repository->isAvailable());
    Core::ProviderRegistry *registry
        = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    const QList<Core::Provider *> providers = registry->providers(Core::ProviderKind::DeviceAdapter);
    QVERIFY(providers.contains(repository));

    const QList<Data::DeviceAdapterManifest> manifests = repository->adapterManifests();
    QVERIFY(
        std::is_sorted(
            manifests.cbegin(),
            manifests.cend(),
            [](const Data::DeviceAdapterManifest &left, const Data::DeviceAdapterManifest &right) {
                if (left.matchPriority != right.matchPriority)
                    return left.matchPriority > right.matchPriority;
                if (left.id.value != right.id.value)
                    return left.id.value < right.id.value;
                return left.version < right.version;
            }));
    for (const Data::DeviceAdapterManifest &manifest : manifests) {
        const std::optional<Data::DeviceAdapterManifest> exact
            = repository->adapterManifest(manifest.id, manifest.version);
        QVERIFY(exact);
        QCOMPARE(*exact, manifest);
    }
}

} // namespace EtherCAT::DeviceAdapters::Internal
