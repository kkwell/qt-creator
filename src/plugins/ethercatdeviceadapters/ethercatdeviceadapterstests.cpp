// Copyright (C) 2026 Kvell

#include "ethercatdeviceadapterstests.h"

#include "adapterpackagerepository.h"

#include <coreplugin/icore.h>

#include <ethercatcore/manualcontrolcontract.h>
#include <ethercatcore/providerregistry.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/filepath.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

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

static Utils::FilePath adaptersRoot()
{
    return ::Core::ICore::resourcePath("ethercat/adapters");
}

static const Data::DeviceAdapterManifest *manifestForIdentity(
    const QList<Data::DeviceAdapterManifest> &manifests, const Data::DeviceIdentity &identity)
{
    const auto found = std::find_if(
        manifests.cbegin(), manifests.cend(), [&identity](const Data::DeviceAdapterManifest &item) {
            return item.match.vendorId == identity.vendorId
                   && item.match.productCode == identity.productCode
                   && item.match.minimumRevision == identity.revisionNumber
                   && item.match.maximumRevision == identity.revisionNumber;
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
    QCOMPARE(repository.loadedPackageCount(), 4);
    QVERIFY(repository.loadErrors().isEmpty());

    const QList<Data::DeviceAdapterManifest> manifests = repository.adapterManifests();
    QCOMPARE(manifests.size(), 4);
    const Data::DeviceAdapterManifest *xb6 = manifestForIdentity(manifests, xb6Identity);
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(xb6);
    QVERIFY(sv630n);
    QCOMPARE(xb6->version, QString("0.2.0"));
    QCOMPARE(sv630n->version, QString("0.2.0"));
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
    wrongSchema.insert("schemaVersion", "embed-labs.device-adapter/v3");
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
    const Data::DeviceAdapterManifest *xb6 = manifestForIdentity(manifests, xb6Identity);
    const Data::DeviceAdapterManifest *sv630n = manifestForIdentity(manifests, sv630nIdentity);
    QVERIFY(xb6);
    QVERIFY(sv630n);
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
        QVERIFY(std::all_of(
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
    QCOMPARE(
        sv630n->contentSha256,
        QCryptographicHash::hash(*bundledContents, QCryptographicHash::Sha256));
    QVERIFY(std::all_of(
        sv630n->semanticSignals.cbegin(),
        sv630n->semanticSignals.cend(),
        [](const Data::SemanticSignalDefinition &signal) { return !signal.engineeringTransform; }));

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
        QVERIFY(std::all_of(
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
        QVERIFY(std::all_of(
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
        QVERIFY(std::all_of(
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
    QVERIFY(std::all_of(
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
    QVERIFY(std::is_sorted(
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
