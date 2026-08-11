// Copyright (C) 2026 Embed Labs

#include "compilerinputprovisioningprofile.h"

#include "compilerprovisioningprofile.h"

#include <monocypher-ed25519.h>

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <limits>

namespace EtherCAT::ProjectCompiler {

namespace {

constexpr qsizetype maximumProfileBytes = 1024 * 1024;
constexpr qsizetype maximumArtifactBytes = 256 * 1024 * 1024;

bool hasExactKeys(const QJsonObject &object, const QSet<QString> &keys)
{
    if (object.size() != keys.size())
        return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!keys.contains(it.key()))
            return false;
    }
    return true;
}

std::optional<Data::RuntimePackageCompilerSha256> sha256FromHex(const QJsonValue &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!value.isString() || !pattern.match(value.toString()).hasMatch())
        return std::nullopt;
    Data::RuntimePackageCompilerSha256 sha{QByteArray::fromHex(value.toString().toLatin1())};
    return sha.isValid() ? std::optional(sha) : std::nullopt;
}

bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || path != path.trimmed() || !QDir::isRelativePath(path)
        || path.contains(QLatin1Char('\\'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    return std::all_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return !part.isEmpty() && part != QLatin1String(".") && part != QLatin1String("..");
    });
}

Utils::Result<Data::RuntimePackageCompilerSourceArtifact> loadArtifact(
    const Utils::FilePath &profileFile,
    const QJsonValue &value,
    Data::RuntimePackageCompilerSourceArtifactKind kind,
    qsizetype maximumBytes = maximumArtifactBytes)
{
    const QJsonObject object = value.toObject();
    if (!hasExactKeys(object, {QStringLiteral("path"), QStringLiteral("sha256")}))
        return Utils::ResultError(QStringLiteral("Compiler input artifact descriptor is invalid."));
    const QString relativePath = object.value(QStringLiteral("path")).toString();
    const auto expectedSha = sha256FromHex(object.value(QStringLiteral("sha256")));
    if (!isSafeRelativePath(relativePath) || !expectedSha)
        return Utils::ResultError(QStringLiteral("Compiler input artifact identity is invalid."));
    const Utils::FilePath path = profileFile.parentDir().pathAppended(relativePath);
    const Utils::Result<QByteArray> bytes = readProvisionedRegularLeaf(path, maximumBytes);
    if (!bytes)
        return Utils::ResultError(bytes.error());
    const Data::RuntimePackageCompilerSha256 actual{
        QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256)};
    if (actual != *expectedSha)
        return Utils::ResultError(QStringLiteral("Compiler input artifact digest does not match."));
    return Data::RuntimePackageCompilerSourceArtifact{kind, relativePath, *bytes, actual};
}

std::optional<quint64> unsignedInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble(-1);
    if (number < 0 || number > double(std::numeric_limits<qint64>::max())
        || number != quint64(number)) {
        return std::nullopt;
    }
    return quint64(number);
}

QMap<QString, QString> stringMap(const QJsonValue &value, bool *ok)
{
    QMap<QString, QString> result;
    if (!value.isObject()) {
        *ok = false;
        return result;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString() || it.key().isEmpty() || it.value().toString().isEmpty()) {
            *ok = false;
            return {};
        }
        result.insert(it.key(), it.value().toString());
    }
    return result;
}

std::optional<Data::RuntimePackageCompilerManualRecoveryAction> recoveryAction(
    const QString &value)
{
    using Action = Data::RuntimePackageCompilerManualRecoveryAction;
    if (value == QLatin1String("hold_safe"))
        return Action::HoldSafe;
    if (value == QLatin1String("return_task"))
        return Action::ReturnToTask;
    if (value == QLatin1String("stop"))
        return Action::Stop;
    return std::nullopt;
}

std::optional<Data::RuntimePackageCompilerManualEnvelope> manualEnvelope(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    static const QSet<QString> keys{
        QStringLiteral("enabled"),
        QStringLiteral("failure_action"),
        QStringLiteral("max_hold_cycles"),
        QStringLiteral("max_ttl_cycles"),
        QStringLiteral("refresh_cycles"),
        QStringLiteral("release_action"),
        QStringLiteral("timeout_action"),
    };
    if (!hasExactKeys(object, keys) || !object.value(QStringLiteral("enabled")).isBool())
        return std::nullopt;
    const auto maximumHold = unsignedInteger(object.value(QStringLiteral("max_hold_cycles")));
    const auto maximumTtl = unsignedInteger(object.value(QStringLiteral("max_ttl_cycles")));
    const auto refresh = unsignedInteger(object.value(QStringLiteral("refresh_cycles")));
    const auto failure = recoveryAction(object.value(QStringLiteral("failure_action")).toString());
    const auto release = recoveryAction(object.value(QStringLiteral("release_action")).toString());
    const auto timeout = recoveryAction(object.value(QStringLiteral("timeout_action")).toString());
    if (!maximumHold || !maximumTtl || !refresh || *maximumHold > 65535 || *maximumTtl > 65535
        || *refresh > 65535 || !failure || !release || !timeout) {
        return std::nullopt;
    }
    Data::RuntimePackageCompilerManualEnvelope result{
        object.value(QStringLiteral("enabled")).toBool(),
        quint16(*maximumTtl),
        quint16(*refresh),
        quint16(*maximumHold),
        *timeout,
        *release,
        *failure,
    };
    return result.isValid() ? std::optional(result) : std::nullopt;
}

std::optional<Data::RuntimePackageCompilerSymbolMode> symbolMode(const QString &value)
{
    using Mode = Data::RuntimePackageCompilerSymbolMode;
    if (value == QLatin1String("report_only"))
        return Mode::ReportOnly;
    if (value == QLatin1String("requested"))
        return Mode::Requested;
    if (value == QLatin1String("all"))
        return Mode::All;
    return std::nullopt;
}

Utils::Result<CompilerInputProvisionedDevice> loadDevice(
    const Utils::FilePath &profileFile, const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    static const QSet<QString> keys{
        QStringLiteral("adapter_source"),
        QStringLiteral("component_binding_ids"),
        QStringLiteral("expected_alias"),
        QStringLiteral("expected_modules"),
        QStringLiteral("manual_envelope"),
        QStringLiteral("project_device_id"),
        QStringLiteral("project_slave_node_id"),
        QStringLiteral("semantic_action_binding_ids"),
        QStringLiteral("semantic_binding_ids"),
        QStringLiteral("slave_node_id"),
        QStringLiteral("symbol_mode"),
        QStringLiteral("symbols"),
    };
    if (!hasExactKeys(object, keys))
        return Utils::ResultError(QStringLiteral("Provisioned compiler device shape is invalid."));

    const auto alias = unsignedInteger(object.value(QStringLiteral("expected_alias")));
    const QJsonArray modules = object.value(QStringLiteral("expected_modules")).toArray();
    if (!alias || *alias > std::numeric_limits<quint16>::max()
        || !object.value(QStringLiteral("expected_modules")).isArray()
        || modules.size() > 128) {
        return Utils::ResultError(QStringLiteral("Provisioned topology evidence is invalid."));
    }

    bool mapsOk = true;
    CompilerInputProvisionedDevice result;
    result.projectSlaveNodeId = Data::NodeId::fromString(
        object.value(QStringLiteral("project_slave_node_id")).toString());
    result.slaveNodeId = object.value(QStringLiteral("slave_node_id")).toString();
    result.projectDeviceId = object.value(QStringLiteral("project_device_id")).toString();
    result.expectedAlias = quint16(*alias);
    int previousSlot = 0;
    for (const QJsonValue &moduleValue : modules) {
        const QJsonObject module = moduleValue.toObject();
        if (!hasExactKeys(
                module,
                {QStringLiteral("module_ident"), QStringLiteral("slot")})) {
            return Utils::ResultError(
                QStringLiteral("Provisioned module evidence shape is invalid."));
        }
        const auto slot = unsignedInteger(module.value(QStringLiteral("slot")));
        const auto moduleIdent = unsignedInteger(module.value(QStringLiteral("module_ident")));
        if (!slot || *slot < 1 || *slot > std::numeric_limits<quint16>::max()
            || int(*slot) <= previousSlot || !moduleIdent
            || *moduleIdent > std::numeric_limits<quint32>::max()) {
            return Utils::ResultError(
                QStringLiteral("Provisioned module evidence identity is invalid."));
        }
        result.expectedModuleAssignments.append(
            {int(*slot), quint32(*moduleIdent), 0, 0});
        previousSlot = int(*slot);
    }
    result.componentBindingIds = stringMap(
        object.value(QStringLiteral("component_binding_ids")), &mapsOk);
    result.semanticBindingIds = stringMap(
        object.value(QStringLiteral("semantic_binding_ids")), &mapsOk);
    result.semanticActionBindingIds = stringMap(
        object.value(QStringLiteral("semantic_action_binding_ids")), &mapsOk);
    result.symbols = stringMap(object.value(QStringLiteral("symbols")), &mapsOk);
    const auto mode = symbolMode(object.value(QStringLiteral("symbol_mode")).toString());
    const auto envelope = manualEnvelope(object.value(QStringLiteral("manual_envelope")));
    const auto source = loadArtifact(
        profileFile,
        object.value(QStringLiteral("adapter_source")),
        Data::RuntimePackageCompilerSourceArtifactKind::AdapterSourceFile);
    if (!mapsOk || !mode || !envelope || !source)
        return Utils::ResultError(QStringLiteral("Provisioned compiler device data is invalid."));
    result.symbolMode = *mode;
    result.manualEnvelope = *envelope;
    result.adapterSourceFile = *source;
    if (!result.isValid())
        return Utils::ResultError(QStringLiteral("Provisioned compiler device contract is invalid."));
    return result;
}

} // namespace

bool CompilerInputProvisionedDevice::isValid() const
{
    Data::RuntimePackageCompilerDeviceProjection projection;
    projection.projectSlaveNodeId = projectSlaveNodeId;
    projection.slaveNodeId = slaveNodeId;
    projection.projectDeviceId = projectDeviceId;
    projection.position = 0;
    projection.stationAddress = 1;
    projection.alias = expectedAlias;
    projection.identity = {1, 1, 0};
    projection.esiSha256 = Data::RuntimePackageCompilerSha256{QByteArray(32, '\1')};
    projection.targetProfileId = QStringLiteral("provisioning.validation");
    projection.adapterId = QStringLiteral("provisioning.validation");
    projection.adapterVersion = QStringLiteral("1");
    projection.adapterSha256 = Data::RuntimePackageCompilerSha256{QByteArray(32, '\1')};
    projection.pdoProfileId = QStringLiteral("provisioning.validation");
    projection.pdoMappings = {{QStringLiteral("validation"),
                               Data::RuntimePackageCompilerPdoDirection::Input,
                               1,
                               0,
                               false,
                               {{QStringLiteral("validation"), 1, 0, 1, QStringLiteral("BOOL")}}}};
    projection.moduleAssignments = expectedModuleAssignments;
    projection.componentBindingIds = componentBindingIds;
    projection.semanticBindingIds = semanticBindingIds;
    projection.semanticActionBindingIds = semanticActionBindingIds;
    projection.symbolMode = symbolMode;
    projection.symbols = symbols;
    projection.manualEnvelope = manualEnvelope;
    return adapterSourceFile.isValid() && projection.isValid();
}

Utils::Result<CompilerInputProvisioningProfile> CompilerInputProvisioningProfile::load(
    const Utils::FilePath &profileFile)
{
    const Utils::Result<QByteArray> bytes = readProvisionedRegularLeaf(
        profileFile, maximumProfileBytes);
    if (!bytes)
        return Utils::ResultError(bytes.error());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return Utils::ResultError(QStringLiteral("Compiler input provisioning JSON is malformed."));
    const QJsonObject object = document.object();
    QSet<QString> keys{
        QStringLiteral("adapter_bundle"),
        QStringLiteral("contract_id"),
        QStringLiteral("contract_version"),
        QStringLiteral("controller_features"),
        QStringLiteral("devices"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("master_node_id"),
        QStringLiteral("policy_template"),
        QStringLiteral("production_public_key"),
        QStringLiteral("project_id"),
        QStringLiteral("runtime_source"),
        QStringLiteral("schema_bundle_sha256"),
        QStringLiteral("target_profile"),
        QStringLiteral("target_profile_signature"),
        QStringLiteral("topology_ttl_ns"),
        QStringLiteral("ui_metadata"),
    };
    const QString format = object.value(QStringLiteral("format")).toString();
    const int formatVersion = object.value(QStringLiteral("format_version")).toInt();
    const bool versionOne
        = format == QLatin1String("ethercat-ide-compiler-input-provisioning-v1")
          && formatVersion == 1;
    const bool versionTwo
        = format == QLatin1String("ethercat-ide-compiler-input-provisioning-v2")
          && formatVersion == 2;
    if (versionTwo)
        keys.insert(QStringLiteral("parameter_contract_bundle"));
    if ((!versionOne && !versionTwo) || !hasExactKeys(object, keys)
        || !object.value(QStringLiteral("ui_metadata")).isObject()
        || object.value(QStringLiteral("ui_metadata")).toObject().size() != 0) {
        return Utils::ResultError(QStringLiteral("Compiler input provisioning shape is invalid."));
    }

    const auto contractVersion = unsignedInteger(object.value(QStringLiteral("contract_version")));
    const auto schemaSha = sha256FromHex(object.value(QStringLiteral("schema_bundle_sha256")));
    const auto topologyTtl = unsignedInteger(object.value(QStringLiteral("topology_ttl_ns")));
    Data::RuntimePackageCompilerContractIdentity contract{
        object.value(QStringLiteral("contract_id")).toString(),
        contractVersion && *contractVersion <= std::numeric_limits<quint32>::max()
            ? quint32(*contractVersion)
            : 0,
        schemaSha.value_or(Data::RuntimePackageCompilerSha256{})};
    const bool supportedContract
        = (versionOne
           && contract.contractId
                  == QLatin1String("ethercat-ide-project-compiler-contract-v1")
           && contract.contractVersion == 1)
          || (versionTwo
              && contract.contractId == QLatin1String("ethercat-ide-project-compiler")
              && contract.contractVersion == 2);
    if (!contract.isValid() || !supportedContract || !topologyTtl || *topologyTtl < 1'000'000
        || *topologyTtl > 3'600'000'000'000ULL) {
        return Utils::ResultError(QStringLiteral("Compiler input provisioning identity is invalid."));
    }

    auto targetArtifact = loadArtifact(
        profileFile,
        object.value(QStringLiteral("target_profile")),
        Data::RuntimePackageCompilerSourceArtifactKind::TargetProfile,
        maximumProfileBytes);
    auto targetSignature = loadArtifact(
        profileFile,
        object.value(QStringLiteral("target_profile_signature")),
        Data::RuntimePackageCompilerSourceArtifactKind::TargetProfileSignature,
        64);
    auto publicKey = loadArtifact(
        profileFile,
        object.value(QStringLiteral("production_public_key")),
        Data::RuntimePackageCompilerSourceArtifactKind::ProductionPublicKey,
        32);
    auto adapterBundle = loadArtifact(
        profileFile,
        object.value(QStringLiteral("adapter_bundle")),
        Data::RuntimePackageCompilerSourceArtifactKind::AdapterBundle);
    auto policy = loadArtifact(
        profileFile,
        object.value(QStringLiteral("policy_template")),
        Data::RuntimePackageCompilerSourceArtifactKind::PolicyTemplate);
    auto features = loadArtifact(
        profileFile,
        object.value(QStringLiteral("controller_features")),
        Data::RuntimePackageCompilerSourceArtifactKind::ControllerFeatures);
    auto runtime = loadArtifact(
        profileFile,
        object.value(QStringLiteral("runtime_source")),
        Data::RuntimePackageCompilerSourceArtifactKind::RuntimeSource);
    Utils::Result<Data::RuntimePackageCompilerSourceArtifact> parameterContract{
        Utils::ResultError(QStringLiteral("Compiler v2 parameter contract is absent."))};
    if (versionTwo) {
        parameterContract = loadArtifact(
            profileFile,
            object.value(QStringLiteral("parameter_contract_bundle")),
            Data::RuntimePackageCompilerSourceArtifactKind::ParameterContractBundle);
    }
    if (!targetArtifact || !targetSignature || !publicKey || !adapterBundle || !policy || !features
        || !runtime || (versionTwo && !parameterContract)
        || targetSignature->exactBytes.size() != 64 || publicKey->exactBytes.size() != 32) {
        return Utils::ResultError(QStringLiteral("Compiler input artifacts are incomplete."));
    }
    if (crypto_ed25519_check(
            reinterpret_cast<const uint8_t *>(targetSignature->exactBytes.constData()),
            reinterpret_cast<const uint8_t *>(publicKey->exactBytes.constData()),
            reinterpret_cast<const uint8_t *>(targetArtifact->exactBytes.constData()),
            size_t(targetArtifact->exactBytes.size()))
        != 0) {
        return Utils::ResultError(
            QStringLiteral("Signed target profile signature verification failed."));
    }

    const QJsonDocument targetDocument = QJsonDocument::fromJson(targetArtifact->exactBytes);
    const QJsonObject targetObject = targetDocument.object();
    const auto policyRevision = unsignedInteger(targetObject.value(QStringLiteral("policy_revision")));
    const auto cpu1Abi = unsignedInteger(targetObject.value(QStringLiteral("cpu1_abi_version")));
    const auto fpgaAbi = unsignedInteger(targetObject.value(QStringLiteral("fpga_abi_version")));
    const auto capabilitySha = sha256FromHex(
        targetObject.value(QStringLiteral("capability_descriptor_sha256")));
    const auto targetFeaturesSha = sha256FromHex(
        targetObject.value(QStringLiteral("controller_features_sha256")));
    const auto targetBundleSha = sha256FromHex(
        targetObject.value(QStringLiteral("adapter_bundle_sha256")));
    const auto signingKeySha = sha256FromHex(targetObject.value(QStringLiteral("signing_key_id")));
    Data::RuntimePackageCompilerSignedTargetProfileEvidence target{
        targetObject.value(QStringLiteral("profile_id")).toString(),
        policyRevision.value_or(0),
        Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(targetArtifact->exactBytes),
        capabilitySha.value_or(Data::RuntimePackageCompilerSha256{}),
        targetFeaturesSha.value_or(Data::RuntimePackageCompilerSha256{}),
        targetBundleSha.value_or(Data::RuntimePackageCompilerSha256{}),
        cpu1Abi && *cpu1Abi <= std::numeric_limits<quint32>::max() ? quint32(*cpu1Abi) : 0,
        fpgaAbi && *fpgaAbi <= std::numeric_limits<quint32>::max() ? quint32(*fpgaAbi) : 0,
        targetSignature->exactBytes,
        signingKeySha.value_or(Data::RuntimePackageCompilerSha256{}),
        true,
    };
    if (!target.isValid() || target.controllerFeaturesSha256 != features->sha256
        || target.adapterBundleSha256 != adapterBundle->sha256
        || target.signingKeyIdSha256 != publicKey->sha256) {
        return Utils::ResultError(QStringLiteral("Signed target profile bindings are invalid."));
    }

    const QJsonArray deviceValues = object.value(QStringLiteral("devices")).toArray();
    if (!object.value(QStringLiteral("devices")).isArray() || deviceValues.isEmpty()
        || deviceValues.size() > 256) {
        return Utils::ResultError(QStringLiteral("Compiler input device set is invalid."));
    }
    QList<CompilerInputProvisionedDevice> devices;
    QSet<Data::NodeId> deviceIds;
    for (const QJsonValue &deviceValue : deviceValues) {
        const auto device = loadDevice(profileFile, deviceValue);
        if (!device || deviceIds.contains(device->projectSlaveNodeId))
            return Utils::ResultError(QStringLiteral("Compiler input device entry is invalid."));
        deviceIds.insert(device->projectSlaveNodeId);
        devices.append(*device);
    }

    CompilerInputProvisioningProfile result;
    result.m_profileFile = profileFile;
    result.m_exactProfileBytes = *bytes;
    result.m_contractIdentity = contract;
    result.m_projectId = object.value(QStringLiteral("project_id")).toString();
    result.m_masterNodeId = object.value(QStringLiteral("master_node_id")).toString();
    result.m_topologyTtlNs = *topologyTtl;
    result.m_uiMetadata = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes("{}\n");
    result.m_sourceArtifacts.targetProfile = *targetArtifact;
    result.m_sourceArtifacts.targetProfileSignature = *targetSignature;
    result.m_sourceArtifacts.productionPublicKey = *publicKey;
    result.m_sourceArtifacts.adapterBundle = *adapterBundle;
    result.m_sourceArtifacts.policyTemplate = *policy;
    result.m_sourceArtifacts.controllerFeatures = *features;
    result.m_sourceArtifacts.runtimeSource = *runtime;
    if (versionTwo)
        result.m_sourceArtifacts.parameterContractBundle = *parameterContract;
    result.m_targetProfile = target;
    result.m_devices = devices;
    if (result.m_projectId.isEmpty() || result.m_masterNodeId.isEmpty()
        || !result.m_uiMetadata.isValid()) {
        return Utils::ResultError(QStringLiteral("Compiler project identifiers are invalid."));
    }
    return result;
}

Utils::Result<> CompilerInputProvisioningProfile::validateCurrent() const
{
    const auto current = load(m_profileFile);
    if (!current)
        return Utils::ResultError(current.error());
    if (current->m_exactProfileBytes != m_exactProfileBytes
        || current->m_contractIdentity != m_contractIdentity
        || current->m_projectId != m_projectId || current->m_masterNodeId != m_masterNodeId
        || current->m_topologyTtlNs != m_topologyTtlNs
        || current->m_sourceArtifacts != m_sourceArtifacts
        || current->m_targetProfile != m_targetProfile || current->m_devices != m_devices) {
        return Utils::ResultError(QStringLiteral("Compiler inputs changed after provisioning."));
    }
    return Utils::ResultOk;
}

Utils::FilePath CompilerInputProvisioningProfile::profileFile() const { return m_profileFile; }
Data::RuntimePackageCompilerContractIdentity CompilerInputProvisioningProfile::contractIdentity() const
{
    return m_contractIdentity;
}
QString CompilerInputProvisioningProfile::projectId() const { return m_projectId; }
QString CompilerInputProvisioningProfile::masterNodeId() const { return m_masterNodeId; }
quint64 CompilerInputProvisioningProfile::topologyTtlNs() const { return m_topologyTtlNs; }
Data::RuntimePackageCompilerCanonicalJson CompilerInputProvisioningProfile::uiMetadata() const
{
    return m_uiMetadata;
}
Data::RuntimePackageCompilerSourceArtifacts CompilerInputProvisioningProfile::sourceArtifacts() const
{
    return m_sourceArtifacts;
}
Data::RuntimePackageCompilerSignedTargetProfileEvidence
CompilerInputProvisioningProfile::targetProfile() const
{
    return m_targetProfile;
}
const QList<CompilerInputProvisionedDevice> &CompilerInputProvisioningProfile::devices() const
{
    return m_devices;
}

} // namespace EtherCAT::ProjectCompiler
