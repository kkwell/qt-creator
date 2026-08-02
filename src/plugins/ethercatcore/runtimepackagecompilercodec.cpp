// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilercodec.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace EtherCAT::Core {

namespace {

using namespace Data;

struct JsonValue
{
    enum class Kind { Null, Boolean, Signed, Unsigned, String, Array, Object, Raw };

    Kind kind = Kind::Null;
    bool boolean = false;
    qint64 signedInteger = 0;
    quint64 unsignedInteger = 0;
    QString string;
    std::vector<JsonValue> array;
    std::vector<std::pair<QString, JsonValue>> object;
    QByteArray raw;
};

JsonValue nullValue()
{
    return {};
}

JsonValue booleanValue(bool value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Boolean;
    result.boolean = value;
    return result;
}

JsonValue signedValue(qint64 value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Signed;
    result.signedInteger = value;
    return result;
}

JsonValue unsignedValue(quint64 value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Unsigned;
    result.unsignedInteger = value;
    return result;
}

JsonValue stringValue(QString value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::String;
    result.string = std::move(value);
    return result;
}

JsonValue arrayValue(std::vector<JsonValue> value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Array;
    result.array = std::move(value);
    return result;
}

JsonValue objectValue(std::vector<std::pair<QString, JsonValue>> value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Object;
    result.object = std::move(value);
    return result;
}

JsonValue rawValue(QByteArray value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Raw;
    result.raw = std::move(value);
    return result;
}

bool codePointLess(const QString &left, const QString &right)
{
    const QList<uint> leftCodePoints = left.toUcs4();
    const QList<uint> rightCodePoints = right.toUcs4();
    return std::lexicographical_compare(
        leftCodePoints.cbegin(),
        leftCodePoints.cend(),
        rightCodePoints.cbegin(),
        rightCodePoints.cend());
}

void appendHexCodeUnit(QByteArray *output, quint16 value)
{
    static constexpr char digits[] = "0123456789abcdef";
    output->append("\\u");
    output->append(digits[(value >> 12) & 0xf]);
    output->append(digits[(value >> 8) & 0xf]);
    output->append(digits[(value >> 4) & 0xf]);
    output->append(digits[value & 0xf]);
}

void appendString(QByteArray *output, const QString &value)
{
    output->append('"');
    const QList<uint> codePoints = value.toUcs4();
    for (uint codePoint : codePoints) {
        switch (codePoint) {
        case '"':
            output->append("\\\"");
            continue;
        case '\\':
            output->append("\\\\");
            continue;
        case '\b':
            output->append("\\b");
            continue;
        case '\f':
            output->append("\\f");
            continue;
        case '\n':
            output->append("\\n");
            continue;
        case '\r':
            output->append("\\r");
            continue;
        case '\t':
            output->append("\\t");
            continue;
        default:
            break;
        }
        if (codePoint >= 0x20 && codePoint <= 0x7e) {
            output->append(char(codePoint));
        } else if (codePoint <= 0xffff) {
            appendHexCodeUnit(output, quint16(codePoint));
        } else {
            const uint scalar = codePoint - 0x10000;
            appendHexCodeUnit(output, quint16(0xd800 + (scalar >> 10)));
            appendHexCodeUnit(output, quint16(0xdc00 + (scalar & 0x3ff)));
        }
    }
    output->append('"');
}

bool appendJsonValue(QByteArray *output, const JsonValue &value)
{
    switch (value.kind) {
    case JsonValue::Kind::Null:
        output->append("null");
        return true;
    case JsonValue::Kind::Boolean:
        output->append(value.boolean ? "true" : "false");
        return true;
    case JsonValue::Kind::Signed:
        output->append(QByteArray::number(value.signedInteger));
        return true;
    case JsonValue::Kind::Unsigned:
        output->append(QByteArray::number(value.unsignedInteger));
        return true;
    case JsonValue::Kind::String:
        appendString(output, value.string);
        return true;
    case JsonValue::Kind::Array:
        output->append('[');
        for (size_t index = 0; index < value.array.size(); ++index) {
            if (index != 0)
                output->append(',');
            if (!appendJsonValue(output, value.array.at(index)))
                return false;
        }
        output->append(']');
        return true;
    case JsonValue::Kind::Object: {
        std::vector<std::pair<QString, JsonValue>> members = value.object;
        std::sort(members.begin(), members.end(), [](const auto &left, const auto &right) {
            return codePointLess(left.first, right.first);
        });
        output->append('{');
        QString previous;
        bool first = true;
        for (const auto &[key, member] : members) {
            if (!first && key == previous)
                return false;
            if (!first)
                output->append(',');
            appendString(output, key);
            output->append(':');
            if (!appendJsonValue(output, member))
                return false;
            previous = key;
            first = false;
        }
        output->append('}');
        return true;
    }
    case JsonValue::Kind::Raw:
        if (value.raw.isEmpty())
            return false;
        output->append(value.raw);
        return true;
    }
    return false;
}

RuntimePackageCompilerCanonicalJson canonicalObject(const JsonValue &root)
{
    QByteArray exactBytes;
    if (root.kind != JsonValue::Kind::Object || !appendJsonValue(&exactBytes, root))
        return {};
    exactBytes.append('\n');
    return RuntimePackageCompilerCanonicalJson::fromExactBytes(std::move(exactBytes));
}

JsonValue shaValue(const RuntimePackageCompilerSha256 &sha256)
{
    return stringValue(QString::fromLatin1(sha256.value().toHex()));
}

JsonValue artifactValue(const RuntimePackageCompilerSourceArtifact &artifact)
{
    return objectValue({
        {QStringLiteral("bytes"), unsignedValue(quint64(artifact.exactBytes.size()))},
        {QStringLiteral("path"), stringValue(artifact.relativePath)},
        {QStringLiteral("sha256"), shaValue(artifact.sha256)},
    });
}

QString pdoDirectionName(RuntimePackageCompilerPdoDirection direction)
{
    switch (direction) {
    case RuntimePackageCompilerPdoDirection::Input:
        return QStringLiteral("input");
    case RuntimePackageCompilerPdoDirection::Output:
        return QStringLiteral("output");
    case RuntimePackageCompilerPdoDirection::Unknown:
        return {};
    }
    return {};
}

QString startupStageName(RuntimePackageCompilerStartupStage stage)
{
    switch (stage) {
    case RuntimePackageCompilerStartupStage::PreOperational:
        return QStringLiteral("preop");
    case RuntimePackageCompilerStartupStage::SafeOperational:
        return QStringLiteral("safeop");
    case RuntimePackageCompilerStartupStage::Operational:
        return QStringLiteral("op");
    case RuntimePackageCompilerStartupStage::Unknown:
        return {};
    }
    return {};
}

QString startupFailureActionName(RuntimePackageCompilerStartupFailureAction action)
{
    switch (action) {
    case RuntimePackageCompilerStartupFailureAction::Abort:
        return QStringLiteral("abort");
    case RuntimePackageCompilerStartupFailureAction::Warn:
        return QStringLiteral("warn");
    case RuntimePackageCompilerStartupFailureAction::Continue:
        return QStringLiteral("continue");
    case RuntimePackageCompilerStartupFailureAction::Unknown:
        return {};
    }
    return {};
}

QString manualRecoveryActionName(RuntimePackageCompilerManualRecoveryAction action)
{
    switch (action) {
    case RuntimePackageCompilerManualRecoveryAction::HoldSafe:
        return QStringLiteral("hold_safe");
    case RuntimePackageCompilerManualRecoveryAction::ReturnToTask:
        return QStringLiteral("return_to_task");
    case RuntimePackageCompilerManualRecoveryAction::Stop:
        return QStringLiteral("stop");
    case RuntimePackageCompilerManualRecoveryAction::Unknown:
        return {};
    }
    return {};
}

QString symbolModeName(RuntimePackageCompilerSymbolMode mode)
{
    switch (mode) {
    case RuntimePackageCompilerSymbolMode::ReportOnly:
        return QStringLiteral("report_only");
    case RuntimePackageCompilerSymbolMode::Requested:
        return QStringLiteral("requested");
    case RuntimePackageCompilerSymbolMode::All:
        return QStringLiteral("all");
    case RuntimePackageCompilerSymbolMode::Unknown:
        return {};
    }
    return {};
}

JsonValue stringMapValue(const QMap<QString, QString> &values)
{
    std::vector<std::pair<QString, JsonValue>> members;
    members.reserve(size_t(values.size()));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        members.emplace_back(it.key(), stringValue(it.value()));
    return objectValue(std::move(members));
}

JsonValue pdoEntryValue(const RuntimePackageCompilerPdoEntry &entry)
{
    return objectValue({
        {QStringLiteral("bit_length"), unsignedValue(entry.bitLength)},
        {QStringLiteral("data_type"), stringValue(entry.dataType)},
        {QStringLiteral("field_id"), stringValue(entry.fieldId)},
        {QStringLiteral("index"), unsignedValue(entry.index)},
        {QStringLiteral("subindex"), unsignedValue(entry.subIndex)},
    });
}

JsonValue pdoMappingValue(const RuntimePackageCompilerPdoMapping &mapping)
{
    std::vector<JsonValue> entries;
    entries.reserve(size_t(mapping.entries.size()));
    for (const RuntimePackageCompilerPdoEntry &entry : mapping.entries)
        entries.push_back(pdoEntryValue(entry));
    return objectValue({
        {QStringLiteral("direction"), stringValue(pdoDirectionName(mapping.direction))},
        {QStringLiteral("entries"), arrayValue(std::move(entries))},
        {QStringLiteral("fixed"), booleanValue(mapping.fixed)},
        {QStringLiteral("id"), stringValue(mapping.id)},
        {QStringLiteral("pdo_index"), unsignedValue(mapping.pdoIndex)},
        {QStringLiteral("sm"), unsignedValue(mapping.syncManager)},
    });
}

JsonValue startupSdoValue(const RuntimePackageCompilerStartupSdo &sdo)
{
    const JsonValue value = std::holds_alternative<qint64>(sdo.value)
                                ? signedValue(std::get<qint64>(sdo.value))
                                : unsignedValue(std::get<quint64>(sdo.value));
    return objectValue({
        {QStringLiteral("complete_access"), booleanValue(sdo.completeAccess)},
        {QStringLiteral("enabled"), booleanValue(sdo.enabled)},
        {QStringLiteral("failure_action"), stringValue(startupFailureActionName(sdo.failureAction))},
        {QStringLiteral("id"), stringValue(sdo.id)},
        {QStringLiteral("index"), unsignedValue(sdo.index)},
        {QStringLiteral("persistent"), booleanValue(sdo.persistent)},
        {QStringLiteral("requires_power_cycle"), booleanValue(sdo.requiresPowerCycle)},
        {QStringLiteral("retry_count"), unsignedValue(sdo.retryCount)},
        {QStringLiteral("sequence"), unsignedValue(sdo.sequence)},
        {QStringLiteral("stage"), stringValue(startupStageName(sdo.stage))},
        {QStringLiteral("subindex"), unsignedValue(sdo.subIndex)},
        {QStringLiteral("timeout_ns"), unsignedValue(sdo.timeoutNs)},
        {QStringLiteral("value"), value},
        {QStringLiteral("value_bytes"), unsignedValue(sdo.valueBytes)},
    });
}

JsonValue dcValue(const RuntimePackageCompilerDcProjection &dc)
{
    return objectValue({
        {QStringLiteral("assign_activate"), unsignedValue(dc.assignActivate)},
        {QStringLiteral("enabled"), booleanValue(dc.enabled)},
        {QStringLiteral("mode"), dc.mode ? stringValue(*dc.mode) : nullValue()},
        {QStringLiteral("reference_clock"), booleanValue(dc.referenceClock)},
        {QStringLiteral("signed_dc_profile_id"),
         dc.signedDcProfileId ? stringValue(*dc.signedDcProfileId) : nullValue()},
        {QStringLiteral("sync0_cycle_ns"), unsignedValue(dc.sync0CycleNs)},
        {QStringLiteral("sync0_shift_ns"), signedValue(dc.sync0ShiftNs)},
        {QStringLiteral("sync1_cycle_ns"), unsignedValue(dc.sync1CycleNs)},
        {QStringLiteral("sync1_shift_ns"), signedValue(dc.sync1ShiftNs)},
    });
}

JsonValue manualEnvelopeValue(const RuntimePackageCompilerManualEnvelope &envelope)
{
    return objectValue({
        {QStringLiteral("enabled"), booleanValue(envelope.enabled)},
        {QStringLiteral("failure_action"),
         stringValue(manualRecoveryActionName(envelope.failureAction))},
        {QStringLiteral("max_hold_cycles"), unsignedValue(envelope.maximumHoldCycles)},
        {QStringLiteral("max_ttl_cycles"), unsignedValue(envelope.maximumTtlCycles)},
        {QStringLiteral("refresh_cycles"), unsignedValue(envelope.refreshCycles)},
        {QStringLiteral("release_action"),
         stringValue(manualRecoveryActionName(envelope.releaseAction))},
        {QStringLiteral("timeout_action"),
         stringValue(manualRecoveryActionName(envelope.timeoutAction))},
    });
}

JsonValue deviceValue(const RuntimePackageCompilerDeviceProjection &device)
{
    std::vector<JsonValue> mappings;
    mappings.reserve(size_t(device.pdoMappings.size()));
    for (const RuntimePackageCompilerPdoMapping &mapping : device.pdoMappings)
        mappings.push_back(pdoMappingValue(mapping));

    std::vector<JsonValue> startupSdos;
    startupSdos.reserve(size_t(device.startupSdos.size()));
    for (const RuntimePackageCompilerStartupSdo &sdo : device.startupSdos)
        startupSdos.push_back(startupSdoValue(sdo));

    std::vector<JsonValue> modules;
    modules.reserve(size_t(device.moduleAssignments.size()));
    for (const DeviceModuleAssignment &module : device.moduleAssignments) {
        modules.push_back(objectValue({
            {QStringLiteral("module_ident"), unsignedValue(module.moduleIdent)},
            {QStringLiteral("slot"), unsignedValue(quint64(module.slot))},
        }));
    }

    return objectValue({
        {QStringLiteral("alias"), unsignedValue(device.alias)},
        {QStringLiteral("component_binding_ids"), stringMapValue(device.componentBindingIds)},
        {QStringLiteral("controller_adapter_target"),
         objectValue({
             {QStringLiteral("adapter_id"), stringValue(device.adapterId)},
             {QStringLiteral("adapter_sha256"), shaValue(device.adapterSha256)},
             {QStringLiteral("adapter_version"), stringValue(device.adapterVersion)},
             {QStringLiteral("pdo_profile_id"), stringValue(device.pdoProfileId)},
             {QStringLiteral("signed_dc_profile_id"),
              device.signedDcProfileId ? stringValue(*device.signedDcProfileId) : nullValue()},
             {QStringLiteral("target_profile_id"), stringValue(device.targetProfileId)},
         })},
        {QStringLiteral("dc"), dcValue(device.dc)},
        {QStringLiteral("esi_sha256"), shaValue(device.esiSha256)},
        {QStringLiteral("identity"),
         objectValue({
             {QStringLiteral("product_code"), unsignedValue(device.identity.productCode)},
             {QStringLiteral("revision"), unsignedValue(device.identity.revisionNumber)},
             {QStringLiteral("serial"), unsignedValue(device.serialNumber)},
             {QStringLiteral("vendor_id"), unsignedValue(device.identity.vendorId)},
         })},
        {QStringLiteral("manual_envelope"), manualEnvelopeValue(device.manualEnvelope)},
        {QStringLiteral("module_assignments"), arrayValue(std::move(modules))},
        {QStringLiteral("pdo"),
         objectValue({
             {QStringLiteral("mappings"), arrayValue(std::move(mappings))},
             {QStringLiteral("profile_id"), stringValue(device.pdoProfileId)},
         })},
        {QStringLiteral("position"), unsignedValue(quint64(device.position))},
        {QStringLiteral("project_device_id"), stringValue(device.projectDeviceId)},
        {QStringLiteral("semantic_action_binding_ids"),
         stringMapValue(device.semanticActionBindingIds)},
        {QStringLiteral("semantic_binding_ids"), stringMapValue(device.semanticBindingIds)},
        {QStringLiteral("slave_node_id"), stringValue(device.slaveNodeId)},
        {QStringLiteral("startup_sdos"), arrayValue(std::move(startupSdos))},
        {QStringLiteral("station_address"), unsignedValue(device.stationAddress)},
        {QStringLiteral("symbol_mode"), stringValue(symbolModeName(device.symbolMode))},
        {QStringLiteral("symbols"), stringMapValue(device.symbols)},
    });
}

JsonValue projectValue(const RuntimePackageCompilerProjectProjection &project)
{
    std::vector<JsonValue> devices;
    devices.reserve(size_t(project.devices.size()));
    for (const RuntimePackageCompilerDeviceProjection &device : project.devices)
        devices.push_back(deviceValue(device));

    QByteArray uiMetadata = project.uiMetadata.exactBytes();
    uiMetadata.chop(1);
    return objectValue({
        {QStringLiteral("devices"), arrayValue(std::move(devices))},
        {QStringLiteral("document_revision"), unsignedValue(project.documentRevision)},
        {QStringLiteral("format"), stringValue(QStringLiteral("ethercat-project-snapshot-v1"))},
        {QStringLiteral("master"),
         objectValue({
             {QStringLiteral("cycle_period_ns"), unsignedValue(project.cyclePeriodNs)},
             {QStringLiteral("link_speed_mbps"), unsignedValue(project.linkSpeedMbps)},
             {QStringLiteral("timing_mode"),
              stringValue(
                  project.timingMode == MasterTimingMode::DistributedClocks
                      ? QStringLiteral("dc")
                      : QStringLiteral("free_run"))},
         })},
        {QStringLiteral("master_node_id"), stringValue(project.masterNodeId)},
        {QStringLiteral("project_id"), stringValue(project.projectId)},
        {QStringLiteral("ui_metadata"), rawValue(std::move(uiMetadata))},
    });
}

RuntimePackageCompilerCanonicalJson encodeCompileRequestUnchecked(
    const RuntimePackageCompilerCompileRequest &request)
{
    const RuntimePackageCompilerSourceArtifacts &artifacts = request.sourceArtifacts;
    return canonicalObject(objectValue({
        {QStringLiteral("artifacts"),
         objectValue({
             {QStringLiteral("adapter_bundle"), artifactValue(artifacts.adapterBundle)},
             {QStringLiteral("controller_features"), artifactValue(artifacts.controllerFeatures)},
             {QStringLiteral("policy_template"), artifactValue(artifacts.policyTemplate)},
             {QStringLiteral("production_public_key"), artifactValue(artifacts.productionPublicKey)},
             {QStringLiteral("runtime_source"), artifactValue(artifacts.runtimeSource)},
             {QStringLiteral("target_profile"), artifactValue(artifacts.targetProfile)},
             {QStringLiteral("target_profile_signature"),
              artifactValue(artifacts.targetProfileSignature)},
             {QStringLiteral("topology_evidence"), artifactValue(artifacts.topologyEvidence)},
         })},
        {QStringLiteral("build_timestamp_ns"), unsignedValue(request.buildTimestampNs)},
        {QStringLiteral("compile_time_ns"), unsignedValue(request.compileTimeNs)},
        {QStringLiteral("configuration_id"), unsignedValue(request.configurationId)},
        {QStringLiteral("format"),
         stringValue(QStringLiteral("ethercat-ide-project-compiler-request-v1"))},
        {QStringLiteral("format_version"), unsignedValue(1)},
        {QStringLiteral("intent_id"), stringValue(request.intentId)},
        {QStringLiteral("manifest_format_version"), unsignedValue(request.manifestFormatVersion)},
        {QStringLiteral("operation_id"), stringValue(request.operationId.value())},
        {QStringLiteral("project_snapshot"), projectValue(request.projectProjection)},
    }));
}

JsonValue contractIdentityValue(const RuntimePackageCompilerContractIdentity &identity)
{
    return objectValue({
        {QStringLiteral("contract_id"), stringValue(identity.contractId)},
        {QStringLiteral("contract_version"), unsignedValue(identity.contractVersion)},
        {QStringLiteral("schema_bundle_sha256"), shaValue(identity.schemaBundleSha256)},
    });
}

RuntimePackageCompilerCanonicalJson encodeFinalizeRequestUnchecked(
    const RuntimePackageCompilerFinalizeRequest &request)
{
    QByteArray detachedSigningResponse = request.detachedSigningResponse.exactBytes();
    detachedSigningResponse.chop(1);
    return canonicalObject(objectValue({
        {QStringLiteral("compile_request_sha256"), shaValue(request.compileRequestSha256)},
        {QStringLiteral("configuration_id"), unsignedValue(request.configurationId)},
        {QStringLiteral("contract"), contractIdentityValue(request.contractIdentity)},
        {QStringLiteral("detached_signing_response"), rawValue(std::move(detachedSigningResponse))},
        {QStringLiteral("format"),
         stringValue(QStringLiteral("ethercat-ide-project-compiler-finalize-request-v1"))},
        {QStringLiteral("format_version"), unsignedValue(1)},
        {QStringLiteral("manifest_sha256"), shaValue(request.manifestSha256)},
        {QStringLiteral("operation_id"), stringValue(request.operationId.value())},
        {QStringLiteral("sign_request_sha256"), shaValue(request.signRequestSha256)},
        {QStringLiteral("signing_key_id_sha256"), shaValue(request.signingKeyIdSha256)},
        {QStringLiteral("signing_policy_revision"), unsignedValue(request.signingPolicyRevision)},
    }));
}

RuntimePackageCompilerCanonicalJson encodeVerifyRequestUnchecked(
    const RuntimePackageCompilerVerifyRequest &request)
{
    return canonicalObject(objectValue({
        {QStringLiteral("contract"), contractIdentityValue(request.contractIdentity)},
        {QStringLiteral("format"),
         stringValue(QStringLiteral("ethercat-ide-project-compiler-verify-request-v1"))},
        {QStringLiteral("format_version"), unsignedValue(1)},
        {QStringLiteral("operation_id"), stringValue(request.operationId.value())},
        {QStringLiteral("package_bytes"), unsignedValue(quint64(request.packageBytes.size()))},
        {QStringLiteral("package_sha256"), shaValue(request.packageSha256)},
    }));
}

bool skipString(QByteArrayView bytes, qsizetype *offset)
{
    if (*offset >= bytes.size() || bytes[*offset] != '"')
        return false;
    ++*offset;
    while (*offset < bytes.size()) {
        const char character = bytes[*offset];
        ++*offset;
        if (character == '"')
            return true;
        if (character == '\\') {
            if (*offset >= bytes.size())
                return false;
            const char escaped = bytes[*offset];
            ++*offset;
            if (escaped == 'u') {
                if (*offset + 4 > bytes.size())
                    return false;
                *offset += 4;
            }
        }
    }
    return false;
}

bool skipValue(QByteArrayView bytes, qsizetype *offset, int depth = 0)
{
    if (depth > 256 || *offset >= bytes.size())
        return false;
    const char first = bytes[*offset];
    if (first == '"')
        return skipString(bytes, offset);
    if (first == '{') {
        ++*offset;
        if (*offset < bytes.size() && bytes[*offset] == '}') {
            ++*offset;
            return true;
        }
        while (*offset < bytes.size()) {
            if (!skipString(bytes, offset) || *offset >= bytes.size() || bytes[*offset] != ':') {
                return false;
            }
            ++*offset;
            if (!skipValue(bytes, offset, depth + 1))
                return false;
            if (*offset < bytes.size() && bytes[*offset] == '}') {
                ++*offset;
                return true;
            }
            if (*offset >= bytes.size() || bytes[*offset] != ',')
                return false;
            ++*offset;
        }
        return false;
    }
    if (first == '[') {
        ++*offset;
        if (*offset < bytes.size() && bytes[*offset] == ']') {
            ++*offset;
            return true;
        }
        while (*offset < bytes.size()) {
            if (!skipValue(bytes, offset, depth + 1))
                return false;
            if (*offset < bytes.size() && bytes[*offset] == ']') {
                ++*offset;
                return true;
            }
            if (*offset >= bytes.size() || bytes[*offset] != ',')
                return false;
            ++*offset;
        }
        return false;
    }
    while (*offset < bytes.size()) {
        const char character = bytes[*offset];
        if (character == ',' || character == '}' || character == ']')
            break;
        ++*offset;
    }
    return true;
}

std::optional<QByteArray> rootValue(const QByteArray &canonical, QByteArrayView wantedKey)
{
    if (canonical.size() < 3 || canonical.front() != '{' || !canonical.endsWith('\n'))
        return std::nullopt;
    const QByteArrayView bytes(canonical.constData(), canonical.size() - 1);
    qsizetype offset = 1;
    if (bytes[offset] == '}')
        return std::nullopt;
    while (offset < bytes.size()) {
        const qsizetype keyStart = offset;
        if (!skipString(bytes, &offset) || offset >= bytes.size() || bytes[offset] != ':')
            return std::nullopt;
        const QByteArrayView encodedKey = bytes.sliced(keyStart, offset - keyStart);
        ++offset;
        const qsizetype valueStart = offset;
        if (!skipValue(bytes, &offset))
            return std::nullopt;
        QByteArray wantedEncoded;
        wantedEncoded.append('"');
        wantedEncoded.append(wantedKey.data(), wantedKey.size());
        wantedEncoded.append('"');
        if (encodedKey == QByteArrayView(wantedEncoded))
            return QByteArray(bytes.sliced(valueStart, offset - valueStart));
        if (offset < bytes.size() && bytes[offset] == '}')
            return std::nullopt;
        if (offset >= bytes.size() || bytes[offset] != ',')
            return std::nullopt;
        ++offset;
    }
    return std::nullopt;
}

std::optional<quint64> rootUnsignedInteger(
    const RuntimePackageCompilerCanonicalJson &canonical, QByteArrayView key)
{
    const std::optional<QByteArray> raw = rootValue(canonical.exactBytes(), key);
    if (!raw || raw->isEmpty() || !std::all_of(raw->cbegin(), raw->cend(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        return std::nullopt;
    }
    bool ok = false;
    const quint64 value = raw->toULongLong(&ok);
    return ok ? std::optional<quint64>{value} : std::nullopt;
}

std::vector<QByteArray> arrayItems(const QByteArray &array)
{
    std::vector<QByteArray> result;
    if (array.size() < 2 || array.front() != '[' || array.back() != ']')
        return {};
    qsizetype offset = 1;
    const QByteArrayView bytes(array);
    if (bytes[offset] == ']')
        return result;
    while (offset < bytes.size()) {
        const qsizetype start = offset;
        if (!skipValue(bytes, &offset))
            return {};
        result.emplace_back(bytes.sliced(start, offset - start));
        if (offset < bytes.size() && bytes[offset] == ']')
            return result;
        if (offset >= bytes.size() || bytes[offset] != ',')
            return {};
        ++offset;
    }
    return {};
}

std::optional<RuntimePackageCompilerSha256> shaFromJson(const QJsonObject &object, const QString &key)
{
    const QByteArray hex = object.value(key).toString().toLatin1();
    if (hex.size() != 64 || !std::all_of(hex.cbegin(), hex.cend(), [](char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        })) {
        return std::nullopt;
    }
    RuntimePackageCompilerSha256 result{QByteArray::fromHex(hex)};
    return result.isValid() ? std::optional<RuntimePackageCompilerSha256>{result} : std::nullopt;
}

RuntimePackageCompilerDiagnosticCategory categoryForDiagnostic(
    const QString &stage, const QString &code)
{
    if (code == QStringLiteral("ECOMP-OPERATION-UNKNOWN"))
        return RuntimePackageCompilerDiagnosticCategory::Idempotency;
    if (stage == QStringLiteral("input"))
        return RuntimePackageCompilerDiagnosticCategory::Input;
    if (stage == QStringLiteral("topology"))
        return RuntimePackageCompilerDiagnosticCategory::Topology;
    if (stage == QStringLiteral("esi"))
        return RuntimePackageCompilerDiagnosticCategory::Esi;
    if (stage == QStringLiteral("adapter"))
        return RuntimePackageCompilerDiagnosticCategory::Adapter;
    if (stage == QStringLiteral("pdo"))
        return RuntimePackageCompilerDiagnosticCategory::Pdo;
    if (stage == QStringLiteral("sdo"))
        return RuntimePackageCompilerDiagnosticCategory::Sdo;
    if (stage == QStringLiteral("dc"))
        return RuntimePackageCompilerDiagnosticCategory::Dc;
    if (stage == QStringLiteral("capability"))
        return RuntimePackageCompilerDiagnosticCategory::Capability;
    if (stage == QStringLiteral("timing"))
        return RuntimePackageCompilerDiagnosticCategory::Timing;
    if (stage == QStringLiteral("configuration"))
        return RuntimePackageCompilerDiagnosticCategory::Configuration;
    if (stage == QStringLiteral("signing"))
        return RuntimePackageCompilerDiagnosticCategory::Signing;
    if (stage == QStringLiteral("security"))
        return RuntimePackageCompilerDiagnosticCategory::Security;
    if (stage == QStringLiteral("cancelled"))
        return RuntimePackageCompilerDiagnosticCategory::Canceled;
    if (stage == QStringLiteral("internal"))
        return RuntimePackageCompilerDiagnosticCategory::Internal;
    return RuntimePackageCompilerDiagnosticCategory::Unknown;
}

std::optional<RuntimePackageCompilerDiagnosticSeverity> severityFromName(const QString &severity)
{
    if (severity == QStringLiteral("info"))
        return RuntimePackageCompilerDiagnosticSeverity::Information;
    if (severity == QStringLiteral("warning"))
        return RuntimePackageCompilerDiagnosticSeverity::Warning;
    if (severity == QStringLiteral("error"))
        return RuntimePackageCompilerDiagnosticSeverity::Error;
    return std::nullopt;
}

Utils::Result<QList<RuntimePackageCompilerDiagnostic>> decodeDiagnostics(
    const RuntimePackageCompilerCanonicalJson &canonical)
{
    const std::optional<QByteArray> rawDiagnostics
        = rootValue(canonical.exactBytes(), QByteArrayView("diagnostics"));
    if (!rawDiagnostics)
        return Utils::ResultError(QStringLiteral("Compiler failure has no diagnostics array."));
    const std::vector<QByteArray> items = arrayItems(*rawDiagnostics);
    if (items.empty())
        return Utils::ResultError(QStringLiteral("Compiler failure diagnostics are empty."));

    QList<RuntimePackageCompilerDiagnostic> result;
    result.reserve(qsizetype(items.size()));
    for (QByteArray item : items) {
        item.append('\n');
        const RuntimePackageCompilerCanonicalJson exact
            = RuntimePackageCompilerCanonicalJson::fromExactBytes(std::move(item));
        if (!exact.isValid()) {
            return Utils::ResultError(QStringLiteral("Compiler diagnostic is not canonical JSON."));
        }
        const QJsonObject object = QJsonDocument::fromJson(exact.exactBytes()).object();
        const QString stage = object.value(QStringLiteral("stage")).toString();
        const auto severity = severityFromName(object.value(QStringLiteral("severity")).toString());
        if (!severity) {
            return Utils::ResultError(QStringLiteral("Compiler diagnostic severity is invalid."));
        }
        RuntimePackageCompilerDiagnostic diagnostic{
            categoryForDiagnostic(stage, object.value(QStringLiteral("code")).toString()),
            *severity,
            stage,
            object.value(QStringLiteral("code")).toString(),
            object.value(QStringLiteral("path")).toString(),
            object.value(QStringLiteral("message")).toString(),
            object.value(QStringLiteral("retryable")).toBool(),
            exact,
        };
        if (!diagnostic.isValid()) {
            return Utils::ResultError(
                QStringLiteral("Compiler diagnostic violates the frozen contract."));
        }
        result.append(std::move(diagnostic));
    }
    return result;
}

struct DecodedProcessDocument
{
    RuntimePackageCompilerResultStatus status = RuntimePackageCompilerResultStatus::Unknown;
    RuntimePackageCompilerCanonicalJson canonical;
    QList<RuntimePackageCompilerDiagnostic> diagnostics;
};

Utils::Result<DecodedProcessDocument> decodeProcessDocument(
    const RuntimePackageCompilerProcessOutput &output)
{
    if (!output.exitedNormally)
        return Utils::ResultError(QStringLiteral("Compiler process did not exit normally."));
    const bool success = output.exitCode == 0;
    const bool domainFailure = output.exitCode == 1;
    if (!success && !domainFailure) {
        return Utils::ResultError(
            QStringLiteral("Compiler process returned an unsupported exit code."));
    }
    if ((success && !output.standardError.isEmpty())
        || (domainFailure && !output.standardOutput.isEmpty())) {
        return Utils::ResultError(
            QStringLiteral("Compiler process wrote to the forbidden output stream."));
    }
    const QByteArray exactBytes = success ? output.standardOutput : output.standardError;
    const RuntimePackageCompilerCanonicalJson canonical
        = RuntimePackageCompilerCanonicalJson::fromExactBytes(exactBytes);
    if (!canonical.isValid()) {
        return Utils::ResultError(
            QStringLiteral("Compiler process output is not one canonical JSON object."));
    }
    if (success)
        return DecodedProcessDocument{RuntimePackageCompilerResultStatus::Succeeded, canonical, {}};

    const QJsonObject object = QJsonDocument::fromJson(canonical.exactBytes()).object();
    if (object.size() != 2
        || object.value(QStringLiteral("status")).toString() != QStringLiteral("fail")) {
        return Utils::ResultError(
            QStringLiteral("Compiler failure output violates the frozen envelope."));
    }
    const Utils::Result<QList<RuntimePackageCompilerDiagnostic>> diagnostics = decodeDiagnostics(
        canonical);
    if (!diagnostics)
        return Utils::ResultError(diagnostics.error());
    const bool canceled
        = std::any_of(diagnostics->cbegin(), diagnostics->cend(), [](const auto &diagnostic) {
              return diagnostic.stage == QStringLiteral("cancelled");
          });
    return DecodedProcessDocument{
        canceled ? RuntimePackageCompilerResultStatus::Canceled
                 : RuntimePackageCompilerResultStatus::DomainFailed,
        canonical,
        *diagnostics,
    };
}

RuntimePackageCompilerResultEnvelope envelope(
    RuntimePackageCompilerCommand command,
    const DecodedProcessDocument &document,
    const RuntimePackageCompilerOperationId &operationId,
    quint64 configurationId,
    const RuntimePackageCompilerSha256 &requestSha256,
    const QString &successStatus)
{
    QString backendStatus = successStatus;
    if (document.status == RuntimePackageCompilerResultStatus::DomainFailed)
        backendStatus = QStringLiteral("fail");
    else if (document.status == RuntimePackageCompilerResultStatus::Canceled)
        backendStatus = QStringLiteral("cancelled");
    return {
        command,
        document.status,
        backendStatus,
        operationId,
        configurationId,
        requestSha256,
        document.canonical,
        document.diagnostics,
    };
}

template<typename Result>
Utils::Result<Result> checkedResult(Result result, const QString &description)
{
    if (!result.isValid())
        return Utils::ResultError(description);
    return result;
}

} // namespace

Utils::Result<RuntimePackageCompilerCanonicalJson> encodeRuntimePackageCompilerCompileRequest(
    const RuntimePackageCompilerCompileRequest &request)
{
    if (!request.isValid()) {
        return Utils::ResultError(
            QStringLiteral("The typed API-042 compile request is incomplete or inconsistent."));
    }
    RuntimePackageCompilerCanonicalJson result = encodeCompileRequestUnchecked(request);
    if (!result.isValid()) {
        return Utils::ResultError(
            QStringLiteral("The typed API-042 compile request could not be encoded canonically."));
    }
    return result;
}

Utils::Result<RuntimePackageCompilerCanonicalJson> encodeRuntimePackageCompilerFinalizeRequest(
    const RuntimePackageCompilerFinalizeRequest &request)
{
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("The finalize request is incomplete."));
    RuntimePackageCompilerCanonicalJson result = encodeFinalizeRequestUnchecked(request);
    if (!result.isValid()) {
        return Utils::ResultError(
            QStringLiteral("The finalize request could not be encoded canonically."));
    }
    return result;
}

Utils::Result<RuntimePackageCompilerCanonicalJson> encodeRuntimePackageCompilerVerifyRequest(
    const RuntimePackageCompilerVerifyRequest &request)
{
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("The verify request is incomplete."));
    RuntimePackageCompilerCanonicalJson result = encodeVerifyRequestUnchecked(request);
    if (!result.isValid()) {
        return Utils::ResultError(
            QStringLiteral("The verify request could not be encoded canonically."));
    }
    return result;
}

Utils::Result<RuntimePackageCompilerCompileResult> decodeRuntimePackageCompilerCompileResult(
    const RuntimePackageCompilerCompileRequest &request,
    const RuntimePackageCompilerProcessOutput &output,
    const RuntimePackageCompilerCanonicalJson &signRequest)
{
    const Utils::Result<RuntimePackageCompilerCanonicalJson> encoded
        = encodeRuntimePackageCompilerCompileRequest(request);
    if (!encoded)
        return Utils::ResultError(encoded.error());
    const Utils::Result<DecodedProcessDocument> document = decodeProcessDocument(output);
    if (!document)
        return Utils::ResultError(document.error());
    const RuntimePackageCompilerResultEnvelope resultEnvelope = envelope(
        RuntimePackageCompilerCommand::Compile,
        *document,
        request.operationId,
        request.configurationId,
        encoded->sha256(),
        QStringLiteral("awaiting_signature"));
    if (document->status != RuntimePackageCompilerResultStatus::Succeeded) {
        return checkedResult(
            RuntimePackageCompilerCompileResult{resultEnvelope, {}, {}, {}, {}, {}, {}, {}, {}, {}},
            QStringLiteral("Compiler failure result is invalid."));
    }

    const QJsonObject object = QJsonDocument::fromJson(document->canonical.exactBytes()).object();
    const auto intent = shaFromJson(object, QStringLiteral("intent_sha256"));
    const auto compiledProject = shaFromJson(object, QStringLiteral("compiled_project_sha256"));
    const auto compileReport = shaFromJson(object, QStringLiteral("compile_report_sha256"));
    const auto companion = shaFromJson(object, QStringLiteral("effective_project_companion_sha256"));
    const auto manifest = shaFromJson(object, QStringLiteral("manifest_sha256"));
    const auto target = shaFromJson(object, QStringLiteral("target_profile_sha256"));
    const auto adapter = shaFromJson(object, QStringLiteral("adapter_bundle_sha256"));
    const auto expectedSignRequest = shaFromJson(object, QStringLiteral("sign_request_sha256"));
    const QJsonObject signRequestObject = QJsonDocument::fromJson(signRequest.exactBytes()).object();
    const auto signingKeyId = shaFromJson(signRequestObject, QStringLiteral("signing_key_id"));
    const auto signingPolicyRevision
        = rootUnsignedInteger(signRequest, QByteArrayView("policy_revision"));
    if (!intent || !compiledProject || !compileReport || !companion || !manifest || !target
        || !adapter || !expectedSignRequest || !signRequest.isValid()
        || *expectedSignRequest != signRequest.sha256()
        || *target != request.sourceArtifacts.targetProfile.sha256
        || *adapter != request.sourceArtifacts.adapterBundle.sha256 || !signingKeyId
        || *signingKeyId != request.targetProfile.signingKeyIdSha256 || !signingPolicyRevision
        || *signingPolicyRevision != request.targetProfile.policyRevision) {
        return Utils::ResultError(
            QStringLiteral("Compiler prepared result omits or mismatches signed evidence."));
    }
    return checkedResult(
        RuntimePackageCompilerCompileResult{
            resultEnvelope,
            *intent,
            *compiledProject,
            *compileReport,
            *companion,
            signRequest,
            *manifest,
            *target,
            *adapter,
            object.value(QStringLiteral("output_dir")).toString(),
        },
        QStringLiteral("Compiler prepared result violates the frozen schema."));
}

Utils::Result<RuntimePackageCompilerFinalizeResult> decodeRuntimePackageCompilerFinalizeResult(
    const RuntimePackageCompilerFinalizeRequest &request,
    const RuntimePackageCompilerProcessOutput &output,
    const QByteArray &packageBytes)
{
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("Finalize request is invalid."));
    const Utils::Result<DecodedProcessDocument> document = decodeProcessDocument(output);
    if (!document)
        return Utils::ResultError(document.error());
    const RuntimePackageCompilerResultEnvelope resultEnvelope = envelope(
        RuntimePackageCompilerCommand::Finalize,
        *document,
        request.operationId,
        request.configurationId,
        request.compileRequestSha256,
        QStringLiteral("complete"));
    if (document->status != RuntimePackageCompilerResultStatus::Succeeded) {
        return checkedResult(
            RuntimePackageCompilerFinalizeResult{resultEnvelope, {}, {}, {}, {}, {}},
            QStringLiteral("Finalize failure result is invalid."));
    }

    const QJsonObject object = QJsonDocument::fromJson(document->canonical.exactBytes()).object();
    const auto package = shaFromJson(object, QStringLiteral("package_sha256"));
    const auto manifest = shaFromJson(object, QStringLiteral("manifest_sha256"));
    const auto receipt = shaFromJson(object, QStringLiteral("signing_receipt_sha256"));
    const QJsonObject signingResponse
        = QJsonDocument::fromJson(request.detachedSigningResponse.exactBytes()).object();
    const auto expectedReceipt = shaFromJson(signingResponse, QStringLiteral("receipt_sha256"));
    if (!package || !manifest || !receipt || !expectedReceipt || *manifest != request.manifestSha256
        || *receipt != *expectedReceipt) {
        return Utils::ResultError(QStringLiteral("Finalize result omits signed evidence."));
    }
    return checkedResult(
        RuntimePackageCompilerFinalizeResult{
            resultEnvelope,
            object.value(QStringLiteral("package_path")).toString(),
            packageBytes,
            *package,
            *manifest,
            *receipt,
        },
        QStringLiteral("Finalize result violates the frozen schema."));
}

Utils::Result<RuntimePackageCompilerQueryResult> decodeRuntimePackageCompilerQueryResult(
    const RuntimePackageCompilerQueryRequest &request,
    const RuntimePackageCompilerProcessOutput &output)
{
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("Query request is invalid."));
    const Utils::Result<DecodedProcessDocument> document = decodeProcessDocument(output);
    if (!document)
        return Utils::ResultError(document.error());
    const RuntimePackageCompilerResultEnvelope resultEnvelope = envelope(
        RuntimePackageCompilerCommand::Query,
        *document,
        request.operationId,
        0,
        request.compileRequestSha256,
        QStringLiteral("state"));
    if (document->status != RuntimePackageCompilerResultStatus::Succeeded) {
        return checkedResult(
            RuntimePackageCompilerQueryResult{resultEnvelope, {}, {}},
            QStringLiteral("Query failure result is invalid."));
    }

    const std::optional<QByteArray> compiler
        = rootValue(document->canonical.exactBytes(), QByteArrayView("compiler"));
    const std::optional<QByteArray> signer
        = rootValue(document->canonical.exactBytes(), QByteArrayView("signer_response"));
    if (!compiler || !signer)
        return Utils::ResultError(QStringLiteral("Query result omits operation state."));
    const auto decodeRecord =
        [](QByteArray raw) -> Utils::Result<std::optional<RuntimePackageCompilerCanonicalJson>> {
        if (raw == QByteArrayView("null"))
            return std::optional<RuntimePackageCompilerCanonicalJson>{};
        raw.append('\n');
        RuntimePackageCompilerCanonicalJson canonical
            = RuntimePackageCompilerCanonicalJson::fromExactBytes(std::move(raw));
        if (!canonical.isValid())
            return Utils::ResultError(QStringLiteral("Query record is not canonical JSON."));
        return std::optional<RuntimePackageCompilerCanonicalJson>{std::move(canonical)};
    };
    const Utils::Result<std::optional<RuntimePackageCompilerCanonicalJson>> compilerRecord
        = decodeRecord(*compiler);
    const Utils::Result<std::optional<RuntimePackageCompilerCanonicalJson>> signerResponse
        = decodeRecord(*signer);
    if (!compilerRecord)
        return Utils::ResultError(compilerRecord.error());
    if (!signerResponse)
        return Utils::ResultError(signerResponse.error());
    return checkedResult(
        RuntimePackageCompilerQueryResult{
            resultEnvelope,
            *compilerRecord,
            *signerResponse,
        },
        QStringLiteral("Query result violates the frozen schema."));
}

Utils::Result<RuntimePackageCompilerVerifyResult> decodeRuntimePackageCompilerVerifyResult(
    const RuntimePackageCompilerVerifyRequest &request,
    const RuntimePackageCompilerProcessOutput &output)
{
    if (!request.isValid())
        return Utils::ResultError(QStringLiteral("Verify request is invalid."));
    const Utils::Result<DecodedProcessDocument> document = decodeProcessDocument(output);
    if (!document)
        return Utils::ResultError(document.error());
    if (document->status != RuntimePackageCompilerResultStatus::Succeeded) {
        const RuntimePackageCompilerResultEnvelope resultEnvelope = envelope(
            RuntimePackageCompilerCommand::Verify,
            *document,
            request.operationId,
            0,
            request.packageSha256,
            QStringLiteral("pass"));
        return checkedResult(
            RuntimePackageCompilerVerifyResult{
                resultEnvelope,
                request.packageSha256,
                0,
                {},
                {},
                {},
                {},
                {},
                false,
            },
            QStringLiteral("Verify failure result is invalid."));
    }

    const auto configurationId
        = rootUnsignedInteger(document->canonical, QByteArrayView("configuration_id"));
    const QJsonObject object = QJsonDocument::fromJson(document->canonical.exactBytes()).object();
    const auto package = shaFromJson(object, QStringLiteral("package_sha256"));
    const auto intent = shaFromJson(object, QStringLiteral("intent_sha256"));
    const auto companion = shaFromJson(object, QStringLiteral("effective_project_companion_sha256"));
    const auto target = shaFromJson(object, QStringLiteral("target_profile_sha256"));
    const auto adapter = shaFromJson(object, QStringLiteral("adapter_bundle_sha256"));
    const auto topology = shaFromJson(object, QStringLiteral("topology_evidence_sha256"));
    if (!configurationId || !package || !intent || !companion || !target || !adapter || !topology
        || *package != request.packageSha256) {
        return Utils::ResultError(QStringLiteral("Verify result omits signed evidence."));
    }
    const RuntimePackageCompilerResultEnvelope resultEnvelope = envelope(
        RuntimePackageCompilerCommand::Verify,
        *document,
        request.operationId,
        *configurationId,
        request.packageSha256,
        QStringLiteral("pass"));
    return checkedResult(
        RuntimePackageCompilerVerifyResult{
            resultEnvelope,
            *package,
            quint32(object.value(QStringLiteral("manifest_format_version")).toInt()),
            *intent,
            *companion,
            *target,
            *adapter,
            *topology,
            true,
        },
        QStringLiteral("Verify result violates the frozen schema."));
}

} // namespace EtherCAT::Core
