// Copyright (C) 2026 Embed Labs

#include "signedecpkgmanifest_p.h"

#include "canonicaljson_p.h"
#include "ed25519verifier.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QString>

#include <array>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr qsizetype maximumManifestBytes = 1024 * 1024;
constexpr qsizetype maximumInnerPayloadBytes = 2 * 1024 * 1024;
constexpr qsizetype maximumCompileReportBytes = 8 * 1024 * 1024;
constexpr qsizetype maximumSourceBytes = 16 * 1024 * 1024;
constexpr qsizetype publicKeyBytes = 32;
constexpr qsizetype signatureBytes = 64;
constexpr quint16 ecpkgManifestFormatVersion2 = 2;
constexpr std::string_view actionDefinitionsPayloadName
    = "semantic-action-definitions-v1.json";
constexpr std::string_view actionDefinitionsPayloadMediaType
    = "application/vnd.kvell.ethercat.semantic-action-definitions-v1+json";

constexpr std::array<std::string_view, 4> payloadNames{
    "capability.bin",
    "configuration.ecfg",
    "runtime.erun",
    "compile_report.json",
};

constexpr std::array<std::string_view, 4> payloadMediaTypes{
    "application/vnd.kvell.ethercat.capability-v1",
    "application/vnd.kvell.ethercat.configuration-v1",
    "application/vnd.kvell.ethercat.runtime-v1",
    "application/json",
};

constexpr std::array<std::string_view, 5> sourceNames{
    "matched_scan",
    "policy",
    "adapter_project",
    "controller_features",
    "compiled_project",
};

struct ParsedManifest
{
    quint16 formatVersion = 0;
    bool production = false;
    quint64 configurationId = 0;
    QByteArray signingKeyId;
    std::array<EcpkgDigestRecord, payloadNames.size()> payloads;
    std::optional<EcpkgDigestRecord> actionDefinitions;
    std::array<EcpkgDigestRecord, sourceNames.size()> sources;
    std::optional<SignedEcpkgSemanticBindingSummary> semanticBinding;
};

Utils::ResultError invalidManifest(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Invalid signed ECPKG manifest: %1").arg(detail));
}

QString fieldPath(const QString &path, std::string_view field)
{
    return path + QLatin1Char('.') + QString::fromLatin1(field.data(), qsizetype(field.size()));
}

bool containsField(const StrictJson &object, std::string_view field)
{
    return object.find(field) != object.end();
}

bool hasExactFields(
    const StrictJson &object,
    const QString &path,
    std::initializer_list<std::string_view> required,
    std::initializer_list<std::string_view> optional,
    QString *error)
{
    if (!object.is_object()) {
        *error = QString::fromLatin1("%1 must be an object").arg(path);
        return false;
    }

    std::set<std::string_view> allowed;
    for (std::string_view field : required) {
        allowed.insert(field);
        if (!containsField(object, field)) {
            *error = QString::fromLatin1("%1 is required").arg(fieldPath(path, field));
            return false;
        }
    }
    for (std::string_view field : optional)
        allowed.insert(field);

    for (auto iterator = object.cbegin(); iterator != object.cend(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            *error = QString::fromLatin1("%1 contains an unknown field")
                         .arg(fieldPath(path, iterator.key()));
            return false;
        }
    }
    return true;
}

bool hasExactFields(
    const StrictJson &object,
    const QString &path,
    std::initializer_list<std::string_view> required,
    QString *error)
{
    return hasExactFields(object, path, required, {}, error);
}

bool readUnsigned(const StrictJson &value, quint64 maximum, quint64 *result)
{
    if (value.is_boolean() || !value.is_number_integer())
        return false;

    if (value.is_number_unsigned()) {
        const auto number = value.get<StrictJson::number_unsigned_t>();
        if (number > maximum)
            return false;
        *result = quint64(number);
        return true;
    }

    const auto number = value.get<StrictJson::number_integer_t>();
    if (number < 0 || quint64(number) > maximum)
        return false;
    *result = quint64(number);
    return true;
}

bool validateUnsigned(
    const StrictJson &value, quint64 minimum, quint64 maximum, const QString &path, QString *error)
{
    quint64 number = 0;
    if (!readUnsigned(value, maximum, &number) || number < minimum) {
        *error = QString::fromLatin1("%1 must be an integer in [%2, %3]")
                     .arg(path)
                     .arg(minimum)
                     .arg(maximum);
        return false;
    }
    return true;
}

bool readBoolean(const StrictJson &value, bool *result)
{
    if (!value.is_boolean())
        return false;
    *result = value.get<bool>();
    return true;
}

QString jsonString(const StrictJson &value)
{
    const std::string &string = value.get_ref<const std::string &>();
    return QString::fromUtf8(string.data(), qsizetype(string.size()));
}

qsizetype unicodeLength(const QString &value)
{
    return value.toUcs4().size();
}

bool validateStringLength(
    const StrictJson &value,
    qsizetype minimum,
    qsizetype maximum,
    const QString &path,
    QString *error)
{
    if (!value.is_string()) {
        *error = QString::fromLatin1("%1 must be a string").arg(path);
        return false;
    }
    const qsizetype length = unicodeLength(jsonString(value));
    if (length < minimum || length > maximum) {
        *error = QString::fromLatin1("%1 must contain between %2 and %3 Unicode characters")
                     .arg(path)
                     .arg(minimum)
                     .arg(maximum);
        return false;
    }
    return true;
}

bool validateConstantString(
    const StrictJson &value, std::string_view expected, const QString &path, QString *error)
{
    if (!value.is_string() || value.get_ref<const std::string &>() != expected) {
        *error = QString::fromLatin1("%1 must equal %2")
                     .arg(path, QString::fromLatin1(expected.data(), qsizetype(expected.size())));
        return false;
    }
    return true;
}

bool isSimpleId(std::string_view value)
{
    if (value.empty() || value.size() > 96 || value.front() < 'a' || value.front() > 'z')
        return false;
    for (char character : value) {
        const bool valid = (character >= 'a' && character <= 'z')
                           || (character >= '0' && character <= '9') || character == '_'
                           || character == '-';
        if (!valid)
            return false;
    }
    return true;
}

bool validateSimpleId(const StrictJson &value, const QString &path, QString *error)
{
    if (!value.is_string() || !isSimpleId(value.get_ref<const std::string &>())) {
        *error = QString::fromLatin1("%1 is not a valid simple identifier").arg(path);
        return false;
    }
    return true;
}

bool isQualifiedId(std::string_view value)
{
    if (value.empty() || value.size() > 192)
        return false;

    std::size_t segmentStart = 0;
    std::size_t segmentCount = 0;
    while (segmentStart <= value.size()) {
        const std::size_t separator = value.find('.', segmentStart);
        const std::size_t segmentEnd
            = separator == std::string_view::npos ? value.size() : separator;
        const std::string_view segment = value.substr(segmentStart, segmentEnd - segmentStart);
        if (segment.empty() || segment.front() < 'a' || segment.front() > 'z')
            return false;
        for (char character : segment) {
            const bool valid = (character >= 'a' && character <= 'z')
                               || (character >= '0' && character <= '9') || character == '_'
                               || character == '-';
            if (!valid)
                return false;
        }
        ++segmentCount;
        if (separator == std::string_view::npos)
            break;
        segmentStart = separator + 1;
    }
    return segmentCount >= 2;
}

bool validateQualifiedId(const StrictJson &value, const QString &path, QString *error)
{
    if (!value.is_string() || !isQualifiedId(value.get_ref<const std::string &>())) {
        *error = QString::fromLatin1("%1 is not a valid qualified identifier").arg(path);
        return false;
    }
    return true;
}

bool validatePattern(
    const StrictJson &value,
    const QRegularExpression &pattern,
    const QString &path,
    QString *error)
{
    if (!value.is_string() || !pattern.match(jsonString(value)).hasMatch()) {
        *error = QString::fromLatin1("%1 has an invalid format").arg(path);
        return false;
    }
    return true;
}

bool isLowerSha256(std::string_view value)
{
    if (value.size() != 64)
        return false;
    for (char character : value) {
        if (!((character >= '0' && character <= '9')
              || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool readSha256(
    const StrictJson &value, const QString &path, QByteArray *result, QString *error)
{
    if (!value.is_string() || !isLowerSha256(value.get_ref<const std::string &>())) {
        *error = QString::fromLatin1("%1 must be a lowercase SHA-256 hexadecimal string").arg(path);
        return false;
    }
    const std::string &hex = value.get_ref<const std::string &>();
    *result = QByteArray::fromHex(QByteArray(hex.data(), qsizetype(hex.size())));
    return true;
}

bool validateEnumString(
    const StrictJson &value,
    std::initializer_list<std::string_view> choices,
    const QString &path,
    QString *error)
{
    if (value.is_string()) {
        const std::string &actual = value.get_ref<const std::string &>();
        for (std::string_view choice : choices) {
            if (actual == choice)
                return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unsupported value").arg(path);
    return false;
}

bool validateUniqueStringArray(
    const StrictJson &value,
    const QString &path,
    bool (*validateItem)(const StrictJson &, const QString &, QString *),
    QString *error)
{
    if (!value.is_array()) {
        *error = QString::fromLatin1("%1 must be an array").arg(path);
        return false;
    }

    std::set<std::string> values;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const StrictJson &item = value[index];
        const QString itemPath = path + QString::fromLatin1("[%1]").arg(index);
        if (!validateItem(item, itemPath, error))
            return false;
        if (!values.insert(item.get_ref<const std::string &>()).second) {
            *error = QString::fromLatin1("%1 contains a duplicate value").arg(itemPath);
            return false;
        }
    }
    return true;
}

bool validateMailboxProtocol(
    const StrictJson &value, const QString &path, QString *error)
{
    return validateEnumString(value, {"coe", "soe", "foe", "eoe", "voe"}, path, error);
}

bool validateTrustLevel(const StrictJson &value, const QString &path, QString *error)
{
    return validateEnumString(
        value,
        {"discovered",
         "schema_valid",
         "simulated",
         "hardware_tested",
         "production_qualified"},
        path,
        error);
}

int trustLevelRank(const StrictJson &value)
{
    const std::string &trust = value.get_ref<const std::string &>();
    if (trust == "discovered")
        return 0;
    if (trust == "schema_valid")
        return 1;
    if (trust == "simulated")
        return 2;
    if (trust == "hardware_tested")
        return 3;
    return 4;
}

bool validateCompiler(const StrictJson &value, QString *error)
{
    const QString path = QString::fromLatin1("$.compiler");
    if (!hasExactFields(value, path, {"ecpkg", "adapter_project", "st"}, {"etir"}, error))
        return false;

    const QRegularExpression ecpkgPattern(
        QString::fromLatin1("^osl-ecpkg-[0-9]+\\.[0-9]+\\.[0-9]+$"));
    const QRegularExpression etirPattern(
        QString::fromLatin1("^osl-etir-[0-9]+\\.[0-9]+\\.[0-9]+$"));
    if (!validatePattern(value.at("ecpkg"), ecpkgPattern, path + ".ecpkg", error)
        || !validateStringLength(
            value.at("adapter_project"), 1, 64, path + ".adapter_project", error)) {
        return false;
    }

    const StrictJson &st = value.at("st");
    if (!st.is_null() && !validateStringLength(st, 1, 64, path + ".st", error))
        return false;
    if (containsField(value, "etir")
        && !validatePattern(value.at("etir"), etirPattern, path + ".etir", error)) {
        return false;
    }
    return true;
}

bool validateConfiguration(const StrictJson &value, ParsedManifest *result, QString *error)
{
    const QString path = QString::fromLatin1("$.configuration");
    if (!hasExactFields(
            value,
            path,
            {"configuration_id",
             "build_timestamp",
             "configured_cycle_ns",
             "minimum_cycle_ns",
             "remaining_margin_ns",
             "wire_time_ns",
             "frame_count",
             "patch_count",
             "expected_wkc",
             "slave_count",
             "pdo_output_bytes",
             "pdo_input_bytes",
             "cpu1_abi_version",
             "fpga_abi_version"},
            {"requested_timing_mode", "effective_timing_mode", "dc_record_count"},
            error)) {
        return false;
    }

    struct IntegerField
    {
        const char *name;
        quint64 minimum;
        quint64 maximum;
    };
    constexpr std::array<IntegerField, 14> integerFields{{
        {"configuration_id", 0, std::numeric_limits<quint64>::max()},
        {"build_timestamp", 0, std::numeric_limits<quint64>::max()},
        {"configured_cycle_ns", 1, std::numeric_limits<quint32>::max()},
        {"minimum_cycle_ns", 1, std::numeric_limits<quint32>::max()},
        {"remaining_margin_ns", 0, std::numeric_limits<quint32>::max()},
        {"wire_time_ns", 0, std::numeric_limits<quint32>::max()},
        {"frame_count", 0, std::numeric_limits<quint16>::max()},
        {"patch_count", 0, std::numeric_limits<quint32>::max()},
        {"expected_wkc", 0, std::numeric_limits<quint32>::max()},
        {"slave_count", 0, std::numeric_limits<quint16>::max()},
        {"pdo_output_bytes", 0, std::numeric_limits<quint32>::max()},
        {"pdo_input_bytes", 0, std::numeric_limits<quint32>::max()},
        {"cpu1_abi_version", 1, std::numeric_limits<quint32>::max()},
        {"fpga_abi_version", 1, std::numeric_limits<quint32>::max()},
    }};
    for (const IntegerField &field : integerFields) {
        if (!validateUnsigned(
                value.at(field.name),
                field.minimum,
                field.maximum,
                fieldPath(path, field.name),
                error)) {
            return false;
        }
    }

    if (containsField(value, "requested_timing_mode")
        && !validateEnumString(
            value.at("requested_timing_mode"),
            {"auto", "free_run", "dc"},
            path + ".requested_timing_mode",
            error)) {
        return false;
    }
    if (containsField(value, "effective_timing_mode")
        && !validateEnumString(
            value.at("effective_timing_mode"),
            {"free_run", "dc"},
            path + ".effective_timing_mode",
            error)) {
        return false;
    }
    if (containsField(value, "dc_record_count")
        && !validateUnsigned(
            value.at("dc_record_count"),
            0,
            std::numeric_limits<quint16>::max(),
            path + ".dc_record_count",
            error)) {
        return false;
    }

    readUnsigned(
        value.at("configuration_id"),
        std::numeric_limits<quint64>::max(),
        &result->configurationId);
    return true;
}

bool validateRequirements(const StrictJson &value, QString *error)
{
    const QString path = QString::fromLatin1("$.requirements");
    if (!hasExactFields(
            value, path, {"mailbox_protocols", "profiles", "controller_features"}, error)) {
        return false;
    }
    return validateUniqueStringArray(
               value.at("mailbox_protocols"),
               path + ".mailbox_protocols",
               validateMailboxProtocol,
               error)
           && validateUniqueStringArray(
               value.at("profiles"), path + ".profiles", validateQualifiedId, error)
           && validateUniqueStringArray(
               value.at("controller_features"),
               path + ".controller_features",
               validateSimpleId,
               error);
}

bool validateTopologyInstance(
    const StrictJson &value, const QString &path, bool production, QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"position",
             "station_address",
             "vendor_id",
             "product_code",
             "revision",
             "serial",
             "firmware",
             "adapter_id",
             "adapter_version",
             "adapter_sha256",
             "trust_level",
             "evidence_ids",
             "esi_sha256",
             "pdo_profile",
             "dc_profile",
             "symbol_mode"},
            error)) {
        return false;
    }

    if (!validateUnsigned(
            value.at("position"),
            0,
            std::numeric_limits<quint16>::max(),
            path + ".position",
            error)
        || !validateUnsigned(
            value.at("station_address"),
            1,
            std::numeric_limits<quint16>::max(),
            path + ".station_address",
            error)
        || !validateUnsigned(
            value.at("vendor_id"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".vendor_id",
            error)
        || !validateUnsigned(
            value.at("product_code"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".product_code",
            error)) {
        return false;
    }

    for (std::string_view field : {"revision", "serial"}) {
        const StrictJson &item = value.at(field);
        if (!item.is_null()
            && !validateUnsigned(
                item,
                0,
                std::numeric_limits<quint32>::max(),
                fieldPath(path, field),
                error)) {
            return false;
        }
    }

    const StrictJson &firmware = value.at("firmware");
    if (!firmware.is_null() && !firmware.is_string()) {
        if (!validateUnsigned(
                firmware,
                0,
                std::numeric_limits<quint32>::max(),
                path + ".firmware",
                error)) {
            return false;
        }
    } else if (firmware.is_string()
               && !validateStringLength(firmware, 1, 128, path + ".firmware", error)) {
        return false;
    }

    static const QRegularExpression adapterVersionPattern(
        QString::fromLatin1("^[0-9]+\\.[0-9]+\\.[0-9]+(?:-[a-z0-9.-]+)?$"));
    QByteArray ignoredDigest;
    if (!validateQualifiedId(value.at("adapter_id"), path + ".adapter_id", error)
        || !validatePattern(
            value.at("adapter_version"),
            adapterVersionPattern,
            path + ".adapter_version",
            error)
        || !readSha256(
            value.at("adapter_sha256"), path + ".adapter_sha256", &ignoredDigest, error)
        || !validateTrustLevel(value.at("trust_level"), path + ".trust_level", error)
        || !validateUniqueStringArray(
            value.at("evidence_ids"), path + ".evidence_ids", validateQualifiedId, error)
        || !readSha256(value.at("esi_sha256"), path + ".esi_sha256", &ignoredDigest, error)
        || !validateSimpleId(value.at("pdo_profile"), path + ".pdo_profile", error)) {
        return false;
    }

    const StrictJson &dcProfile = value.at("dc_profile");
    if (!dcProfile.is_null()
        && !validateSimpleId(dcProfile, path + ".dc_profile", error)) {
        return false;
    }
    if (!validateEnumString(
            value.at("symbol_mode"),
            {"report_only", "requested", "all"},
            path + ".symbol_mode",
            error)) {
        return false;
    }

    if (production) {
        if (trustLevelRank(value.at("trust_level")) < 3) {
            *error = QString::fromLatin1(
                         "%1.trust_level is below hardware_tested for a production package")
                         .arg(path);
            return false;
        }
        if (firmware.is_null()) {
            *error = QString::fromLatin1(
                         "%1.firmware is required for a production package")
                         .arg(path);
            return false;
        }
        if (value.at("evidence_ids").empty()) {
            *error = QString::fromLatin1(
                         "%1.evidence_ids must be non-empty for a production package")
                         .arg(path);
            return false;
        }
    }
    return true;
}

bool validateTopology(const StrictJson &value, bool production, QString *error)
{
    const QString path = QString::fromLatin1("$.topology");
    if (!value.is_array() || value.empty() || value.size() > 256) {
        *error = QString::fromLatin1("%1 must contain between 1 and 256 instances").arg(path);
        return false;
    }

    std::set<quint64> positions;
    std::set<quint64> stations;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const QString itemPath = path + QString::fromLatin1("[%1]").arg(index);
        if (!validateTopologyInstance(value[index], itemPath, production, error))
            return false;

        quint64 position = 0;
        quint64 station = 0;
        readUnsigned(
            value[index].at("position"), std::numeric_limits<quint16>::max(), &position);
        readUnsigned(
            value[index].at("station_address"),
            std::numeric_limits<quint16>::max(),
            &station);
        if (!positions.insert(position).second || !stations.insert(station).second) {
            *error = QString::fromLatin1(
                "%1 duplicates a topology position or station address")
                         .arg(itemPath);
            return false;
        }
    }
    return true;
}

bool validateQualification(const StrictJson &value, QString *error)
{
    const QString path = QString::fromLatin1("$.qualification");
    return hasExactFields(value, path, {"minimum_trust_level", "evidence_ids"}, error)
           && validateTrustLevel(
               value.at("minimum_trust_level"), path + ".minimum_trust_level", error)
           && validateUniqueStringArray(
               value.at("evidence_ids"), path + ".evidence_ids", validateQualifiedId, error);
}

bool readDigestRecord(
    const StrictJson &value, const QString &path, EcpkgDigestRecord *result, QString *error)
{
    if (!hasExactFields(value, path, {"bytes", "sha256"}, error))
        return false;

    quint64 bytes = 0;
    if (!validateUnsigned(
            value.at("bytes"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".bytes",
            error)
        || !readSha256(value.at("sha256"), path + ".sha256", &result->sha256, error)) {
        return false;
    }
    readUnsigned(value.at("bytes"), std::numeric_limits<quint32>::max(), &bytes);
    result->bytes = quint32(bytes);
    return true;
}

bool validateSources(const StrictJson &value, ParsedManifest *result, QString *error)
{
    const QString path = QString::fromLatin1("$.sources");
    if (!hasExactFields(
            value,
            path,
            {"matched_scan",
             "policy",
             "adapter_project",
             "controller_features",
             "compiled_project"},
            error)) {
        return false;
    }

    for (std::size_t index = 0; index < sourceNames.size(); ++index) {
        const std::string_view name = sourceNames[index];
        if (!readDigestRecord(
                value.at(name), fieldPath(path, name), &result->sources[index], error)) {
            return false;
        }
        if (result->sources[index].bytes > quint32(maximumSourceBytes)) {
            *error = QString::fromLatin1("%1.bytes exceeds the 16 MiB source limit")
                         .arg(fieldPath(path, name));
            return false;
        }
    }
    return true;
}

bool validatePayloads(const StrictJson &value, ParsedManifest *result, QString *error)
{
    const QString path = QString::fromLatin1("$.payloads");
    const std::size_t expectedCount
        = result->formatVersion == ecpkgManifestFormatVersion2
              ? payloadNames.size() + 1
              : payloadNames.size();
    if (!value.is_array() || value.size() != expectedCount) {
        *error = QString::fromLatin1("%1 must contain exactly %2 payload records")
                     .arg(path)
                     .arg(qulonglong(expectedCount));
        return false;
    }

    for (std::size_t index = 0; index < payloadNames.size(); ++index) {
        const StrictJson &record = value[index];
        const QString itemPath = path + QString::fromLatin1("[%1]").arg(index);
        if (!hasExactFields(record, itemPath, {"name", "bytes", "sha256", "media_type"}, error)
            || !validateConstantString(
                record.at("name"), payloadNames[index], itemPath + ".name", error)
            || !validateUnsigned(
                record.at("bytes"),
                0,
                std::numeric_limits<quint32>::max(),
                itemPath + ".bytes",
                error)
            || !readSha256(
                record.at("sha256"),
                itemPath + ".sha256",
                &result->payloads[index].sha256,
                error)
            || !validateConstantString(
                record.at("media_type"),
                payloadMediaTypes[index],
                itemPath + ".media_type",
                error)) {
            return false;
        }
        quint64 bytes = 0;
        readUnsigned(record.at("bytes"), std::numeric_limits<quint32>::max(), &bytes);
        result->payloads[index].bytes = quint32(bytes);
    }

    if (result->formatVersion == ecpkgManifestFormatVersion2) {
        const StrictJson &record = value[payloadNames.size()];
        const QString itemPath
            = path + QString::fromLatin1("[%1]").arg(qulonglong(payloadNames.size()));
        EcpkgDigestRecord actionDefinitions;
        if (!hasExactFields(
                record, itemPath, {"name", "bytes", "sha256", "media_type"}, error)
            || !validateConstantString(
                record.at("name"),
                actionDefinitionsPayloadName,
                itemPath + ".name",
                error)
            || !validateUnsigned(
                record.at("bytes"),
                1,
                std::numeric_limits<quint32>::max(),
                itemPath + ".bytes",
                error)
            || !readSha256(
                record.at("sha256"),
                itemPath + ".sha256",
                &actionDefinitions.sha256,
                error)
            || !validateConstantString(
                record.at("media_type"),
                actionDefinitionsPayloadMediaType,
                itemPath + ".media_type",
                error)) {
            return false;
        }
        quint64 bytes = 0;
        readUnsigned(record.at("bytes"), std::numeric_limits<quint32>::max(), &bytes);
        actionDefinitions.bytes = quint32(bytes);
        result->actionDefinitions = std::move(actionDefinitions);
    }
    return true;
}

bool validateSemanticBinding(
    const StrictJson &value,
    quint16 manifestFormatVersion,
    SignedEcpkgSemanticBindingSummary *result,
    QString *error)
{
    const QString path = QString::fromLatin1("$.semantic_binding");
    const bool actionDefinitionsRequired
        = manifestFormatVersion == ecpkgManifestFormatVersion2;
    const bool fieldsValid
        = actionDefinitionsRequired
              ? hasExactFields(
                  value,
                  path,
                  {"format_version",
                   "binding_count",
                   "catalog_revision",
                   "topology_identity",
                   "artifact_sha256",
                   "resource_records_sha256",
                   "resource_section_sha256",
                   "topology_sha256",
                   "action_definitions_format_version",
                   "action_definition_count",
                   "action_definitions_sha256"},
                  error)
              : hasExactFields(
                  value,
                  path,
                  {"format_version",
                   "binding_count",
                   "catalog_revision",
                   "topology_identity",
                   "artifact_sha256",
                   "resource_records_sha256",
                   "resource_section_sha256",
                   "topology_sha256"},
                  error);
    if (!fieldsValid
        || !validateUnsigned(value.at("format_version"), 1, 2, path + ".format_version", error)
        || !validateUnsigned(
            value.at("binding_count"), 1, 8192, path + ".binding_count", error)
        || !validateUnsigned(
            value.at("catalog_revision"),
            0,
            std::numeric_limits<quint64>::max(),
            path + ".catalog_revision",
            error)
        || !validateUnsigned(
            value.at("topology_identity"),
            0,
            std::numeric_limits<quint64>::max(),
            path + ".topology_identity",
            error)
        || !readSha256(
            value.at("artifact_sha256"),
            path + ".artifact_sha256",
            &result->mappingSha256,
            error)
        || !readSha256(
            value.at("resource_records_sha256"),
            path + ".resource_records_sha256",
            &result->resourceRecordsSha256,
            error)
        || !readSha256(
            value.at("resource_section_sha256"),
            path + ".resource_section_sha256",
            &result->resourceSectionSha256,
            error)
        || !readSha256(
            value.at("topology_sha256"),
            path + ".topology_sha256",
            &result->topologySha256,
            error)) {
        return false;
    }

    quint64 formatVersion = 0;
    quint64 bindingCount = 0;
    readUnsigned(value.at("format_version"), 2, &formatVersion);
    readUnsigned(value.at("binding_count"), 8192, &bindingCount);
    readUnsigned(
        value.at("catalog_revision"),
        std::numeric_limits<quint64>::max(),
        &result->catalogRevision);
    readUnsigned(
        value.at("topology_identity"),
        std::numeric_limits<quint64>::max(),
        &result->topologyIdentity);
    result->formatVersion = quint16(formatVersion);
    result->bindingCount = quint32(bindingCount);

    if (actionDefinitionsRequired) {
        quint64 actionDefinitionsFormatVersion = 0;
        quint64 actionDefinitionCount = 0;
        if (result->formatVersion != 2
            || !validateUnsigned(
                value.at("action_definitions_format_version"),
                1,
                1,
                path + ".action_definitions_format_version",
                error)
            || !validateUnsigned(
                value.at("action_definition_count"),
                1,
                4096,
                path + ".action_definition_count",
                error)
            || !readSha256(
                value.at("action_definitions_sha256"),
                path + ".action_definitions_sha256",
                &result->actionDefinitionsSha256,
                error)) {
            if (error->isEmpty()) {
                *error = QString::fromLatin1(
                             "%1 must describe semantic binding format 2 action definitions")
                             .arg(path);
            }
            return false;
        }
        readUnsigned(
            value.at("action_definitions_format_version"),
            1,
            &actionDefinitionsFormatVersion);
        readUnsigned(value.at("action_definition_count"), 4096, &actionDefinitionCount);
        result->actionDefinitionsFormatVersion
            = quint16(actionDefinitionsFormatVersion);
        result->actionDefinitionCount = quint32(actionDefinitionCount);
    }
    return true;
}

bool validateSignature(const StrictJson &value, ParsedManifest *result, QString *error)
{
    const QString path = QString::fromLatin1("$.signature");
    if (!hasExactFields(value, path, {"algorithm", "key_id", "signature_file"}, error)
        || !validateConstantString(value.at("algorithm"), "ed25519", path + ".algorithm", error)
        || !readSha256(value.at("key_id"), path + ".key_id", &result->signingKeyId, error)
        || !validateConstantString(
            value.at("signature_file"), "manifest.sig", path + ".signature_file", error)) {
        return false;
    }
    return true;
}

bool validateManifestSchema(
    const StrictJson &manifest, ParsedManifest *result, QString *error)
{
    quint64 formatVersion = 0;
    if (!hasExactFields(
            manifest,
            QString::fromLatin1("$"),
            {"format",
             "format_version",
             "compiler",
             "production",
             "configuration",
             "requirements",
             "topology",
             "qualification",
             "sources",
             "payloads",
             "signature"},
            {"semantic_binding"},
            error)
        || !validateConstantString(
            manifest.at("format"), "ethercat-ecpkg", QString::fromLatin1("$.format"), error)
        || !validateUnsigned(
            manifest.at("format_version"), 1, 2, QString::fromLatin1("$.format_version"), error)
        || !readBoolean(manifest.at("production"), &result->production)) {
        if (error->isEmpty())
            *error = QString::fromLatin1("$.production must be boolean");
        return false;
    }
    readUnsigned(manifest.at("format_version"), 2, &formatVersion);
    result->formatVersion = quint16(formatVersion);
    if (result->formatVersion == ecpkgManifestFormatVersion2 && !result->production) {
        *error = QString::fromLatin1(
            "$.production must be true for ECPKG manifest format 2");
        return false;
    }

    if (!validateCompiler(manifest.at("compiler"), error)
        || !validateConfiguration(manifest.at("configuration"), result, error)
        || !validateRequirements(manifest.at("requirements"), error)
        || !validateTopology(manifest.at("topology"), result->production, error)
        || !validateQualification(manifest.at("qualification"), error)
        || !validateSources(manifest.at("sources"), result, error)
        || !validatePayloads(manifest.at("payloads"), result, error)
        || !validateSignature(manifest.at("signature"), result, error)) {
        return false;
    }

    if (containsField(manifest, "semantic_binding")) {
        SignedEcpkgSemanticBindingSummary semanticBinding;
        if (!validateSemanticBinding(
                manifest.at("semantic_binding"),
                result->formatVersion,
                &semanticBinding,
                error)) {
            return false;
        }
        result->semanticBinding = std::move(semanticBinding);
    }
    if (result->formatVersion == ecpkgManifestFormatVersion2) {
        if (!result->semanticBinding || !result->actionDefinitions
            || result->semanticBinding->formatVersion != 2
            || result->semanticBinding->actionDefinitionsFormatVersion != 1
            || !result->semanticBinding->actionDefinitionCount
            || result->semanticBinding->actionDefinitionsSha256
                   != result->actionDefinitions->sha256) {
            *error = QString::fromLatin1(
                "ECPKG manifest format 2 has inconsistent semantic action definitions");
            return false;
        }
    }
    return true;
}

EcpkgDigestRecord digestRecord(QByteArrayView bytes)
{
    EcpkgDigestRecord result;
    result.bytes = quint32(bytes.size());
    result.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return result;
}

bool validatePayloadRecord(
    const EcpkgDigestRecord &declared,
    QByteArrayView actual,
    std::string_view name,
    QString *error)
{
    const EcpkgDigestRecord digest = digestRecord(actual);
    if (declared != digest) {
        *error = QString::fromLatin1("$.payloads record for %1 does not match its exact bytes")
                     .arg(QString::fromLatin1(name.data(), qsizetype(name.size())));
        return false;
    }
    return true;
}

bool validateSourceBindings(
    const StrictJson &report, const ParsedManifest &manifest, QString *error)
{
    if (!report.is_object()) {
        *error = QString::fromLatin1("$.compile_report must be an object");
        return false;
    }

    const auto productionIterator = report.find("production");
    bool reportProduction = false;
    if (productionIterator == report.end() || !readBoolean(*productionIterator, &reportProduction)
        || reportProduction != manifest.production) {
        *error = QString::fromLatin1(
            "$.compile_report.production does not match the signed manifest");
        return false;
    }

    const auto sourcesIterator = report.find("source_inputs");
    const QString sourcesPath = QString::fromLatin1("$.compile_report.source_inputs");
    if (sourcesIterator == report.end()
        || !hasExactFields(
            *sourcesIterator,
            sourcesPath,
            {"matched_scan",
             "policy",
             "adapter_project",
             "controller_features",
             "compiled_project"},
            error)) {
        if (sourcesIterator == report.end())
            *error = sourcesPath + QString::fromLatin1(" is required");
        return false;
    }

    for (std::size_t index = 0; index < sourceNames.size(); ++index) {
        EcpkgDigestRecord reportRecord;
        const std::string_view name = sourceNames[index];
        if (!readDigestRecord(
                sourcesIterator->at(name),
                fieldPath(sourcesPath, name),
                &reportRecord,
                error)
            || reportRecord != manifest.sources[index]) {
            if (error->isEmpty()) {
                *error = QString::fromLatin1(
                             "%1 differs from the signed manifest source record")
                             .arg(fieldPath(sourcesPath, name));
            }
            return false;
        }
    }

    const bool reportHasSemanticArtifact
        = containsField(report, "semantic_binding_manifest");
    const bool reportHasSemanticDigest
        = containsField(report, "semantic_binding_manifest_sha256");
    if (manifest.semanticBinding) {
        QByteArray reportDigest;
        if (!reportHasSemanticArtifact || !report.at("semantic_binding_manifest").is_object()
            || !reportHasSemanticDigest
            || !readSha256(
                report.at("semantic_binding_manifest_sha256"),
                QString::fromLatin1("$.compile_report.semantic_binding_manifest_sha256"),
                &reportDigest,
                error)
            || reportDigest != manifest.semanticBinding->mappingSha256) {
            if (error->isEmpty()) {
                *error = QString::fromLatin1(
                    "$.compile_report semantic binding declaration differs from the signed "
                    "manifest summary");
            }
            return false;
        }
    } else if (reportHasSemanticArtifact || reportHasSemanticDigest) {
        *error = QString::fromLatin1(
            "$.compile_report declares semantic bindings absent from the signed manifest");
        return false;
    }

    const bool reportHasActionDefinitions
        = containsField(report, "semantic_action_definitions_manifest");
    const bool reportHasActionDefinitionsDigest
        = containsField(report, "semantic_action_definitions_sha256");
    if (manifest.formatVersion == ecpkgManifestFormatVersion2) {
        QByteArray reportDigest;
        if (!manifest.semanticBinding || !manifest.actionDefinitions
            || !reportHasActionDefinitions
            || !report.at("semantic_action_definitions_manifest").is_object()
            || !reportHasActionDefinitionsDigest
            || !readSha256(
                report.at("semantic_action_definitions_sha256"),
                QString::fromLatin1(
                    "$.compile_report.semantic_action_definitions_sha256"),
                &reportDigest,
                error)
            || reportDigest != manifest.actionDefinitions->sha256
            || reportDigest != manifest.semanticBinding->actionDefinitionsSha256) {
            if (error->isEmpty()) {
                *error = QString::fromLatin1(
                    "$.compile_report semantic action definitions differ from the signed "
                    "manifest summary");
            }
            return false;
        }
    } else if (reportHasActionDefinitions || reportHasActionDefinitionsDigest) {
        *error = QString::fromLatin1(
            "$.compile_report declares semantic action definitions in ECPKG format 1");
        return false;
    }
    return true;
}

bool readMatchingReportValue(
    const StrictJson &object,
    std::string_view name,
    quint64 maximum,
    quint64 expected,
    const QString &path,
    QString *error)
{
    const auto iterator = object.find(name);
    quint64 actual = 0;
    if (iterator == object.end() || !readUnsigned(*iterator, maximum, &actual)
        || actual != expected) {
        *error = QString::fromLatin1("%1 does not match the signed package")
                     .arg(fieldPath(path, name));
        return false;
    }
    return true;
}

bool readMatchingReportDigest(
    const StrictJson &object,
    std::string_view name,
    const QByteArray &expected,
    const QString &path,
    QString *error)
{
    const auto iterator = object.find(name);
    QByteArray actual;
    if (iterator == object.end()
        || !readSha256(*iterator, fieldPath(path, name), &actual, error)
        || actual != expected) {
        if (error->isEmpty()) {
            *error = QString::fromLatin1("%1 does not match the signed package")
                         .arg(fieldPath(path, name));
        }
        return false;
    }
    return true;
}

bool validateCompileReportPackageBindings(
    const StrictJson &report, const ParsedManifest &manifest, QString *error)
{
    const auto packageIterator = report.find("package");
    const QString path = QString::fromLatin1("$.compile_report.package");
    if (packageIterator == report.end() || !packageIterator->is_object()) {
        *error = path + QString::fromLatin1(" must be an object");
        return false;
    }

    const StrictJson &package = *packageIterator;
    return readMatchingReportDigest(
               package, "capability_sha256", manifest.payloads[0].sha256, path, error)
           && readMatchingReportDigest(
               package, "configuration_sha256", manifest.payloads[1].sha256, path, error)
           && readMatchingReportDigest(
               package, "runtime_sha256", manifest.payloads[2].sha256, path, error)
           && readMatchingReportValue(
               package,
               "configuration_bytes",
               std::numeric_limits<quint32>::max(),
               manifest.payloads[1].bytes,
               path,
               error)
           && readMatchingReportValue(
               package,
               "runtime_bytes",
               std::numeric_limits<quint32>::max(),
               manifest.payloads[2].bytes,
               path,
               error)
           && readMatchingReportValue(
               package,
               "configuration_id",
               std::numeric_limits<quint64>::max(),
               manifest.configurationId,
               path,
               error);
}

bool validTrustClass(EcpkgTrustClass trust)
{
    return trust == EcpkgTrustClass::Production || trust == EcpkgTrustClass::Engineering;
}

Utils::Result<const EcpkgTrustedPublicKey *> findTrustedPublicKey(
    QByteArrayView signingKeyId,
    EcpkgTrustClass requiredTrust,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys)
{
    if (trustedPublicKeys.isEmpty())
        return invalidManifest(QString::fromLatin1("the trusted public-key set is empty"));
    if (trustedPublicKeys.size() > maximumEcpkgTrustedPublicKeys) {
        return invalidManifest(
            QString::fromLatin1("the trusted public-key set exceeds %1 entries")
                .arg(maximumEcpkgTrustedPublicKeys));
    }

    std::set<QByteArray> keyIds;
    const EcpkgTrustedPublicKey *matching = nullptr;
    bool foundWithWrongTrust = false;
    for (const EcpkgTrustedPublicKey &trustedKey : trustedPublicKeys) {
        if (trustedKey.rawPublicKey.size() != publicKeyBytes) {
            return invalidManifest(
                QString::fromLatin1("every trusted Ed25519 public key must be exactly 32 bytes"));
        }
        if (!validTrustClass(trustedKey.trust)) {
            return invalidManifest(
                QString::fromLatin1("a trusted public key has an invalid trust class"));
        }

        const QByteArray keyId
            = QCryptographicHash::hash(trustedKey.rawPublicKey, QCryptographicHash::Sha256);
        if (!keyIds.insert(keyId).second) {
            return invalidManifest(
                QString::fromLatin1("the trusted public-key set contains a duplicate key"));
        }
        if (keyId == signingKeyId) {
            if (trustedKey.trust == requiredTrust)
                matching = &trustedKey;
            else
                foundWithWrongTrust = true;
        }
    }

    if (!matching) {
        if (foundWithWrongTrust) {
            return invalidManifest(
                QString::fromLatin1(
                    "the signing key is not trusted for the package's production class"));
        }
        return invalidManifest(
            QString::fromLatin1("the manifest signing key is not in the trusted key set"));
    }
    return matching;
}

bool validateContainerFields(const EcpkgContainer &container, QString *error)
{
    if (container.packageSha256.size() != 32) {
        *error = QString::fromLatin1("the container package SHA-256 is not 32 bytes");
        return false;
    }
    if (container.manifestJson.isEmpty() || container.manifestJson.size() > maximumManifestBytes) {
        *error = QString::fromLatin1("manifest.json has an invalid size");
        return false;
    }
    if (container.capabilityBin.isEmpty()
        || container.capabilityBin.size() > maximumInnerPayloadBytes
        || container.configurationEcfg.isEmpty()
        || container.configurationEcfg.size() > maximumInnerPayloadBytes
        || container.runtimeErun.isEmpty()
        || container.runtimeErun.size() > maximumInnerPayloadBytes) {
        *error = QString::fromLatin1("an inner ECPKG payload has an invalid size");
        return false;
    }
    if (container.compileReportJson.isEmpty()
        || container.compileReportJson.size() > maximumCompileReportBytes) {
        *error = QString::fromLatin1("compile_report.json has an invalid size");
        return false;
    }
    if (container.semanticActionDefinitionsJson.size() > maximumCompileReportBytes) {
        *error = QString::fromLatin1(
            "semantic-action-definitions-v1.json exceeds its size limit");
        return false;
    }
    if (container.manifestSignature.size() != signatureBytes) {
        *error = QString::fromLatin1("manifest.sig is not exactly 64 bytes");
        return false;
    }
    return true;
}

} // namespace

Utils::Result<VerifiedSignedEcpkgManifest> verifySignedEcpkgManifest(
    const EcpkgContainer &container, const QList<EcpkgTrustedPublicKey> &trustedPublicKeys)
{
    QString validationError;
    if (!validateContainerFields(container, &validationError))
        return invalidManifest(validationError);

    Utils::Result<StrictJson> manifest
        = parseCanonicalJson(container.manifestJson, maximumManifestBytes);
    if (!manifest)
        return invalidManifest(manifest.error());

    ParsedManifest parsed;
    if (!validateManifestSchema(*manifest, &parsed, &validationError))
        return invalidManifest(validationError);

    const bool containerHasActionDefinitions
        = !container.semanticActionDefinitionsJson.isEmpty();
    if (containerHasActionDefinitions
        != (parsed.formatVersion == ecpkgManifestFormatVersion2)) {
        return invalidManifest(
            QString::fromLatin1(
                "the canonical container shape does not match the manifest format version"));
    }

    const std::array<QByteArrayView, 4> payloads{
        container.capabilityBin,
        container.configurationEcfg,
        container.runtimeErun,
        container.compileReportJson,
    };
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        if (!validatePayloadRecord(
                parsed.payloads[index], payloads[index], payloadNames[index], &validationError)) {
            return invalidManifest(validationError);
        }
    }
    if (parsed.actionDefinitions
        && !validatePayloadRecord(
            *parsed.actionDefinitions,
            container.semanticActionDefinitionsJson,
            actionDefinitionsPayloadName,
            &validationError)) {
        return invalidManifest(validationError);
    }

    Utils::Result<StrictJson> report
        = parseStrictJson(container.compileReportJson, maximumCompileReportBytes);
    if (!report)
        return invalidManifest(report.error());
    if (!validateSourceBindings(*report, parsed, &validationError)
        || !validateCompileReportPackageBindings(*report, parsed, &validationError)) {
        return invalidManifest(validationError);
    }

    const EcpkgTrustClass trust
        = parsed.production ? EcpkgTrustClass::Production : EcpkgTrustClass::Engineering;
    Utils::Result<const EcpkgTrustedPublicKey *> trustedKey
        = findTrustedPublicKey(parsed.signingKeyId, trust, trustedPublicKeys);
    if (!trustedKey)
        return Utils::ResultError(trustedKey.error());

    if (!verifyEd25519DetachedSignature(
            (*trustedKey)->rawPublicKey,
            container.manifestSignature,
            container.manifestJson)) {
        return invalidManifest(
            QString::fromLatin1("the Ed25519 signature over exact manifest.json bytes is invalid"));
    }

    VerifiedSignedEcpkgManifest result;
    result.formatVersion = parsed.formatVersion;
    result.trust = trust;
    result.configurationId = parsed.configurationId;
    result.packageSha256 = container.packageSha256;
    result.manifestSha256
        = QCryptographicHash::hash(container.manifestJson, QCryptographicHash::Sha256);
    result.signingKeyIdSha256 = parsed.signingKeyId;
    result.capability = parsed.payloads[0];
    result.configuration = parsed.payloads[1];
    result.runtime = parsed.payloads[2];
    result.compileReport = parsed.payloads[3];
    result.actionDefinitions = std::move(parsed.actionDefinitions);
    result.compiledProjectSource = parsed.sources[4];
    result.semanticBinding = std::move(parsed.semanticBinding);
    return result;
}

Utils::Result<> verifyEcpkgCompiledProjectSource(
    const VerifiedSignedEcpkgManifest &manifest,
    QByteArrayView compiledProject,
    qsizetype maximumCompiledProjectBytes)
{
    if (maximumCompiledProjectBytes <= 0
        || maximumCompiledProjectBytes > defaultMaximumCompiledProjectBytes) {
        return invalidManifest(
            QString::fromLatin1("the compiled-project size limit is invalid"));
    }
    if (compiledProject.size() > defaultMaximumCompiledProjectBytes
        || compiledProject.size() > maximumCompiledProjectBytes
        || quint64(compiledProject.size()) > std::numeric_limits<quint32>::max()) {
        return invalidManifest(
            QString::fromLatin1("the compiled-project source exceeds its configured size limit"));
    }

    const EcpkgDigestRecord actual = digestRecord(compiledProject);
    if (actual != manifest.compiledProjectSource) {
        return invalidManifest(
            QString::fromLatin1(
                "the compiled-project source bytes do not match the signed source record"));
    }
    return Utils::ResultOk;
}

} // namespace EtherCAT::SemanticRuntime::Internal
