// Copyright (C) 2026 Embed Labs

#include "semanticbindingartifact_p.h"

#include "canonicaljson_p.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr quint32 semanticBindingFormatVersion = 1;
constexpr quint32 maximumBindings = 8192;
constexpr quint32 maximumTopologyInstances = 256;
constexpr qsizetype sha256Bytes = 32;

struct ReportSemanticSymbol
{
    QString semanticSignalId;
    quint64 resourceId = 0;
    quint64 componentInstanceId = 0;
    quint64 parentInstanceId = 0;
    quint32 instanceOrdinal = 0;
    quint32 consistencyGroupId = 0;
    EcfgResourcePrimitive primitive = EcfgResourcePrimitive::Bool;
    quint16 bitWidth = 0;
    EcfgResourceDirection direction = EcfgResourceDirection::Input;
    EcfgResourceAccess access = EcfgResourceAccess::Read;
    EcfgResourceSource source = EcfgResourceSource::PdoInput;
    quint32 processImageBitOffset = 0;
    QString adapterId;
    QString adapterVersion;
    QString canonicalSymbol;
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint32 slot = 0;
};

Utils::ResultError invalidArtifact(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Invalid semantic binding artifact: %1").arg(detail));
}

QString fieldPath(const QString &path, std::string_view field)
{
    return path + QLatin1Char('.') + QString::fromLatin1(field.data(), qsizetype(field.size()));
}

QString arrayPath(const QString &path, std::size_t index)
{
    return path + QString::fromLatin1("[%1]").arg(qulonglong(index));
}

bool containsField(const StrictJson &object, std::string_view field)
{
    return object.find(field) != object.end();
}

bool hasExactFields(
    const StrictJson &object,
    const QString &path,
    std::initializer_list<std::string_view> required,
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
    for (auto iterator = object.cbegin(); iterator != object.cend(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            *error = QString::fromLatin1("%1 contains an unknown field")
                         .arg(fieldPath(path, iterator.key()));
            return false;
        }
    }
    return true;
}

bool hasFields(
    const StrictJson &object,
    const QString &path,
    std::initializer_list<std::string_view> required,
    QString *error)
{
    if (!object.is_object()) {
        *error = QString::fromLatin1("%1 must be an object").arg(path);
        return false;
    }
    for (std::string_view field : required) {
        if (!containsField(object, field)) {
            *error = QString::fromLatin1("%1 is required").arg(fieldPath(path, field));
            return false;
        }
    }
    return true;
}

bool readUnsigned(const StrictJson &value, quint64 maximum, quint64 *result)
{
    if (!result || value.is_boolean() || !value.is_number_integer())
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

bool readUnsigned(
    const StrictJson &value,
    quint64 minimum,
    quint64 maximum,
    const QString &path,
    quint64 *result,
    QString *error)
{
    quint64 number = 0;
    if (!readUnsigned(value, maximum, &number) || number < minimum) {
        *error = QString::fromLatin1("%1 must be an integer in [%2, %3]")
                     .arg(path)
                     .arg(minimum)
                     .arg(maximum);
        return false;
    }
    *result = number;
    return true;
}

bool readBoolean(const StrictJson &value, const QString &path, bool *result, QString *error)
{
    if (!value.is_boolean()) {
        *error = QString::fromLatin1("%1 must be boolean").arg(path);
        return false;
    }
    *result = value.get<bool>();
    return true;
}

QString jsonString(const StrictJson &value)
{
    const std::string &string = value.get_ref<const std::string &>();
    return QString::fromUtf8(string.data(), qsizetype(string.size()));
}

bool readString(
    const StrictJson &value,
    qsizetype minimum,
    qsizetype maximum,
    const QString &path,
    QString *result,
    QString *error)
{
    if (!value.is_string()) {
        *error = QString::fromLatin1("%1 must be a string").arg(path);
        return false;
    }
    const QString string = jsonString(value);
    const qsizetype length = string.toUcs4().size();
    if (length < minimum || length > maximum) {
        *error = QString::fromLatin1("%1 must contain between %2 and %3 Unicode characters")
                     .arg(path)
                     .arg(minimum)
                     .arg(maximum);
        return false;
    }
    *result = string;
    return true;
}

bool readConstantString(
    const StrictJson &value, std::string_view expected, const QString &path, QString *error)
{
    if (!value.is_string() || value.get_ref<const std::string &>() != expected) {
        *error = QString::fromLatin1("%1 must equal %2")
                     .arg(path, QString::fromLatin1(expected.data(), qsizetype(expected.size())));
        return false;
    }
    return true;
}

bool isLowerSha256(std::string_view value)
{
    if (value.size() != 64)
        return false;
    return std::all_of(value.cbegin(), value.cend(), [](char character) {
        return (character >= '0' && character <= '9')
               || (character >= 'a' && character <= 'f');
    });
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

bool readFixedHex(
    const StrictJson &value,
    qsizetype digits,
    const QString &path,
    quint64 *result,
    QString *error)
{
    if (!value.is_string()) {
        *error = QString::fromLatin1("%1 must be a hexadecimal string").arg(path);
        return false;
    }
    const std::string &text = value.get_ref<const std::string &>();
    if (text.size() != std::size_t(digits + 2) || text[0] != '0' || text[1] != 'x') {
        *error = QString::fromLatin1("%1 must contain exactly %2 lowercase hexadecimal digits")
                     .arg(path)
                     .arg(digits);
        return false;
    }
    quint64 number = 0;
    for (std::size_t index = 2; index < text.size(); ++index) {
        const char character = text[index];
        quint8 nibble = 0;
        if (character >= '0' && character <= '9')
            nibble = quint8(character - '0');
        else if (character >= 'a' && character <= 'f')
            nibble = quint8(character - 'a' + 10);
        else {
            *error = QString::fromLatin1("%1 must use lowercase hexadecimal digits").arg(path);
            return false;
        }
        number = (number << 4) | nibble;
    }
    *result = number;
    return true;
}

bool isSemanticSignalId(std::string_view value)
{
    if (value.empty() || value.size() > 192)
        return false;
    const auto validFirst = [](char character) {
        return (character >= 'A' && character <= 'Z')
               || (character >= 'a' && character <= 'z')
               || (character >= '0' && character <= '9');
    };
    if (!validFirst(value.front()))
        return false;
    return std::all_of(value.cbegin() + 1, value.cend(), [validFirst](char character) {
        return validFirst(character) || character == '.' || character == '_' || character == ':'
               || character == '/' || character == '-';
    });
}

bool readSemanticSignalId(
    const StrictJson &value, const QString &path, QString *result, QString *error)
{
    if (!value.is_string() || !isSemanticSignalId(value.get_ref<const std::string &>())) {
        *error = QString::fromLatin1("%1 is not a valid SemanticSignalId").arg(path);
        return false;
    }
    *result = jsonString(value);
    return true;
}

bool readPrimitive(
    const StrictJson &value,
    const QString &path,
    EcfgResourcePrimitive *result,
    QString *error)
{
    static constexpr std::array<std::pair<std::string_view, EcfgResourcePrimitive>, 11> choices{{
        {"bool", EcfgResourcePrimitive::Bool},
        {"u8", EcfgResourcePrimitive::U8},
        {"s8", EcfgResourcePrimitive::S8},
        {"u16", EcfgResourcePrimitive::U16},
        {"s16", EcfgResourcePrimitive::S16},
        {"u32", EcfgResourcePrimitive::U32},
        {"s32", EcfgResourcePrimitive::S32},
        {"u64", EcfgResourcePrimitive::U64},
        {"s64", EcfgResourcePrimitive::S64},
        {"q32_32", EcfgResourcePrimitive::Q32_32},
        {"bytes", EcfgResourcePrimitive::RawBits},
    }};
    if (value.is_string()) {
        const std::string &actual = value.get_ref<const std::string &>();
        for (const auto &[name, primitive] : choices) {
            if (actual == name) {
                *result = primitive;
                return true;
            }
        }
    }
    *error = QString::fromLatin1("%1 contains an unsupported primitive").arg(path);
    return false;
}

bool readDirection(
    const StrictJson &value,
    const QString &path,
    EcfgResourceDirection *result,
    QString *error)
{
    if (value.is_string()) {
        const std::string &actual = value.get_ref<const std::string &>();
        if (actual == "input") {
            *result = EcfgResourceDirection::Input;
            return true;
        }
        if (actual == "output") {
            *result = EcfgResourceDirection::Output;
            return true;
        }
        if (actual == "internal") {
            *error = QString::fromLatin1(
                         "%1 cannot bind an internal-only resource to the public ResourceTable")
                         .arg(path);
            return false;
        }
    }
    *error = QString::fromLatin1("%1 contains an unsupported direction").arg(path);
    return false;
}

bool readAccess(
    const StrictJson &value, const QString &path, EcfgResourceAccess *result, QString *error)
{
    if (value.is_string()) {
        const std::string &actual = value.get_ref<const std::string &>();
        if (actual == "read_only") {
            *result = EcfgResourceAccess::Read;
            return true;
        }
        if (actual == "read_write") {
            *result = EcfgResourceAccess::ReadWrite;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unsupported access mode").arg(path);
    return false;
}

bool readSource(
    const StrictJson &value, const QString &path, EcfgResourceSource *result, QString *error)
{
    if (value.is_string()) {
        const std::string &actual = value.get_ref<const std::string &>();
        if (actual == "pdo_input") {
            *result = EcfgResourceSource::PdoInput;
            return true;
        }
        if (actual == "pdo_output") {
            *result = EcfgResourceSource::PdoOutput;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unsupported resource source").arg(path);
    return false;
}

QByteArray sha256(QByteArrayView bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool validDigest(const QByteArray &digest)
{
    return digest.size() == sha256Bytes;
}

bool sameDigestRecord(const EcpkgDigestRecord &record, QByteArrayView bytes)
{
    return quint64(record.bytes) == quint64(bytes.size()) && record.sha256 == sha256(bytes);
}

bool parseTopologyInstance(
    const StrictJson &value,
    const QString &path,
    bool exactFields,
    SemanticBindingTopologyInstance *result,
    QString *error)
{
    const bool fieldsValid
        = exactFields
              ? hasExactFields(
                    value,
                    path,
                    {"position",
                     "station_address",
                     "vendor_id",
                     "product_code",
                     "revision",
                     "serial",
                     "adapter_id",
                     "adapter_version",
                     "adapter_sha256",
                     "esi_sha256",
                     "pdo_profile",
                     "dc_profile"},
                    error)
              : hasFields(
                    value,
                    path,
                    {"position",
                     "station_address",
                     "vendor_id",
                     "product_code",
                     "revision",
                     "serial",
                     "adapter_id",
                     "adapter_version",
                     "adapter_sha256",
                     "esi_sha256",
                     "pdo_profile",
                     "dc_profile"},
                    error);
    if (!fieldsValid)
        return false;

    quint64 position = 0;
    quint64 stationAddress = 0;
    quint64 vendorId = 0;
    quint64 productCode = 0;
    if (!readUnsigned(
            value.at("position"),
            0,
            std::numeric_limits<quint16>::max(),
            path + ".position",
            &position,
            error)
        || !readUnsigned(
            value.at("station_address"),
            1,
            std::numeric_limits<quint16>::max(),
            path + ".station_address",
            &stationAddress,
            error)
        || !readUnsigned(
            value.at("vendor_id"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".vendor_id",
            &vendorId,
            error)
        || !readUnsigned(
            value.at("product_code"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".product_code",
            &productCode,
            error)
        || !readString(
            value.at("adapter_id"),
            1,
            192,
            path + ".adapter_id",
            &result->adapterId,
            error)
        || !readString(
            value.at("adapter_version"),
            1,
            64,
            path + ".adapter_version",
            &result->adapterVersion,
            error)
        || !readSha256(
            value.at("adapter_sha256"), path + ".adapter_sha256", &result->adapterSha256, error)
        || !readSha256(value.at("esi_sha256"), path + ".esi_sha256", &result->esiSha256, error)
        || !readString(
            value.at("pdo_profile"), 1, 96, path + ".pdo_profile", &result->pdoProfile, error)) {
        return false;
    }

    const auto readNullableU32 = [&](std::string_view name, std::optional<quint32> *target) {
        const StrictJson &field = value.at(name);
        if (field.is_null()) {
            target->reset();
            return true;
        }
        quint64 number = 0;
        if (!readUnsigned(
                field,
                0,
                std::numeric_limits<quint32>::max(),
                fieldPath(path, name),
                &number,
                error)) {
            return false;
        }
        *target = quint32(number);
        return true;
    };
    if (!readNullableU32("revision", &result->revision)
        || !readNullableU32("serial", &result->serial)) {
        return false;
    }

    if (value.at("dc_profile").is_null()) {
        result->dcProfile.reset();
    } else {
        QString profile;
        if (!readString(
                value.at("dc_profile"), 1, 96, path + ".dc_profile", &profile, error)) {
            return false;
        }
        result->dcProfile = std::move(profile);
    }

    result->position = quint16(position);
    result->stationAddress = quint16(stationAddress);
    result->vendorId = quint32(vendorId);
    result->productCode = quint32(productCode);
    return true;
}

bool sameTopologyInstance(
    const SemanticBindingTopologyInstance &left,
    const SemanticBindingTopologyInstance &right)
{
    return left.position == right.position && left.stationAddress == right.stationAddress
           && left.vendorId == right.vendorId && left.productCode == right.productCode
           && left.revision == right.revision && left.serial == right.serial
           && left.adapterId == right.adapterId && left.adapterVersion == right.adapterVersion
           && left.adapterSha256 == right.adapterSha256 && left.esiSha256 == right.esiSha256
           && left.pdoProfile == right.pdoProfile && left.dcProfile == right.dcProfile;
}

bool parseArtifactBinding(
    const StrictJson &value,
    const QString &path,
    VerifiedSemanticBinding *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"semantic_signal_id",
             "resource_id",
             "component_instance_id",
             "consistency_group_id",
             "primitive",
             "bit_width",
             "direction",
             "access",
             "adapter_id",
             "adapter_version",
             "adapter_sha256",
             "esi_sha256",
             "canonical_symbol",
             "position",
             "station_address",
             "slot"},
            error)) {
        return false;
    }

    quint64 resourceId = 0;
    quint64 componentInstanceId = 0;
    quint64 consistencyGroupId = 0;
    quint64 bitWidth = 0;
    quint64 position = 0;
    quint64 stationAddress = 0;
    quint64 slot = 0;
    if (!readSemanticSignalId(
            value.at("semantic_signal_id"),
            path + ".semantic_signal_id",
            &result->semanticSignalId,
            error)
        || !readFixedHex(value.at("resource_id"), 16, path + ".resource_id", &resourceId, error)
        || !readFixedHex(
            value.at("component_instance_id"),
            16,
            path + ".component_instance_id",
            &componentInstanceId,
            error)
        || !readFixedHex(
            value.at("consistency_group_id"),
            8,
            path + ".consistency_group_id",
            &consistencyGroupId,
            error)
        || !readPrimitive(value.at("primitive"), path + ".primitive", &result->primitive, error)
        || !readUnsigned(
            value.at("bit_width"), 1, 128, path + ".bit_width", &bitWidth, error)
        || !readDirection(value.at("direction"), path + ".direction", &result->direction, error)
        || !readAccess(value.at("access"), path + ".access", &result->access, error)
        || !readString(
            value.at("adapter_id"),
            1,
            192,
            path + ".adapter_id",
            &result->adapterId,
            error)
        || !readString(
            value.at("adapter_version"),
            1,
            64,
            path + ".adapter_version",
            &result->adapterVersion,
            error)
        || !readSha256(
            value.at("adapter_sha256"), path + ".adapter_sha256", &result->adapterSha256, error)
        || !readSha256(value.at("esi_sha256"), path + ".esi_sha256", &result->esiSha256, error)
        || !readString(
            value.at("canonical_symbol"),
            1,
            192,
            path + ".canonical_symbol",
            &result->canonicalSymbol,
            error)
        || !readUnsigned(
            value.at("position"),
            0,
            std::numeric_limits<quint16>::max(),
            path + ".position",
            &position,
            error)
        || !readUnsigned(
            value.at("station_address"),
            1,
            std::numeric_limits<quint16>::max(),
            path + ".station_address",
            &stationAddress,
            error)
        || !readUnsigned(
            value.at("slot"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".slot",
            &slot,
            error)) {
        return false;
    }
    if (!resourceId || !componentInstanceId || !consistencyGroupId) {
        *error = QString::fromLatin1("%1 contains a zero runtime identity").arg(path);
        return false;
    }

    result->resourceId = resourceId;
    result->componentInstanceId = componentInstanceId;
    result->consistencyGroupId = quint32(consistencyGroupId);
    result->bitWidth = quint16(bitWidth);
    result->position = quint16(position);
    result->stationAddress = quint16(stationAddress);
    result->slot = quint32(slot);
    return true;
}

bool parseReportSemanticSymbol(
    const StrictJson &value,
    const QString &path,
    ReportSemanticSymbol *result,
    QString *error)
{
    if (!hasFields(
            value,
            path,
            {"semantic_signal_id",
             "resource_id",
             "component_instance_id",
             "parent_instance_id",
             "instance_ordinal",
             "consistency_group_id",
             "data_type",
             "bit_length",
             "direction",
             "resource_access",
             "source",
             "bit_offset",
             "binding_flags",
             "adapter_id",
             "adapter_version",
             "canonical_name",
             "position",
             "station_address",
             "slot"},
            error)) {
        return false;
    }

    quint64 resourceId = 0;
    quint64 componentInstanceId = 0;
    quint64 parentInstanceId = 0;
    quint64 instanceOrdinal = 0;
    quint64 consistencyGroupId = 0;
    quint64 bitWidth = 0;
    quint64 bitOffset = 0;
    quint64 bindingFlags = 0;
    quint64 position = 0;
    quint64 stationAddress = 0;
    quint64 slot = 0;
    if (!readSemanticSignalId(
            value.at("semantic_signal_id"),
            path + ".semantic_signal_id",
            &result->semanticSignalId,
            error)
        || !readUnsigned(
            value.at("resource_id"),
            1,
            std::numeric_limits<quint64>::max(),
            path + ".resource_id",
            &resourceId,
            error)
        || !readUnsigned(
            value.at("component_instance_id"),
            1,
            std::numeric_limits<quint64>::max(),
            path + ".component_instance_id",
            &componentInstanceId,
            error)
        || !readUnsigned(
            value.at("parent_instance_id"),
            0,
            std::numeric_limits<quint64>::max(),
            path + ".parent_instance_id",
            &parentInstanceId,
            error)
        || !readUnsigned(
            value.at("instance_ordinal"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".instance_ordinal",
            &instanceOrdinal,
            error)
        || !readUnsigned(
            value.at("consistency_group_id"),
            1,
            std::numeric_limits<quint32>::max(),
            path + ".consistency_group_id",
            &consistencyGroupId,
            error)
        || !readPrimitive(value.at("data_type"), path + ".data_type", &result->primitive, error)
        || !readUnsigned(
            value.at("bit_length"), 1, 128, path + ".bit_length", &bitWidth, error)
        || !readDirection(value.at("direction"), path + ".direction", &result->direction, error)
        || !readAccess(
            value.at("resource_access"), path + ".resource_access", &result->access, error)
        || !readSource(value.at("source"), path + ".source", &result->source, error)
        || !readUnsigned(
            value.at("bit_offset"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".bit_offset",
            &bitOffset,
            error)
        || !readUnsigned(
            value.at("binding_flags"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".binding_flags",
            &bindingFlags,
            error)
        || !readString(
            value.at("adapter_id"),
            1,
            192,
            path + ".adapter_id",
            &result->adapterId,
            error)
        || !readString(
            value.at("adapter_version"),
            1,
            64,
            path + ".adapter_version",
            &result->adapterVersion,
            error)
        || !readString(
            value.at("canonical_name"),
            1,
            192,
            path + ".canonical_name",
            &result->canonicalSymbol,
            error)
        || !readUnsigned(
            value.at("position"),
            0,
            std::numeric_limits<quint16>::max(),
            path + ".position",
            &position,
            error)
        || !readUnsigned(
            value.at("station_address"),
            1,
            std::numeric_limits<quint16>::max(),
            path + ".station_address",
            &stationAddress,
            error)
        || !readUnsigned(
            value.at("slot"),
            0,
            std::numeric_limits<quint32>::max(),
            path + ".slot",
            &slot,
            error)) {
        return false;
    }
    Q_UNUSED(bindingFlags)

    result->resourceId = resourceId;
    result->componentInstanceId = componentInstanceId;
    result->parentInstanceId = parentInstanceId;
    result->instanceOrdinal = quint32(instanceOrdinal);
    result->consistencyGroupId = quint32(consistencyGroupId);
    result->bitWidth = quint16(bitWidth);
    result->processImageBitOffset = quint32(bitOffset);
    result->position = quint16(position);
    result->stationAddress = quint16(stationAddress);
    result->slot = quint32(slot);
    return true;
}

const SemanticBindingTopologyInstance *findTopologyInstance(
    const QList<SemanticBindingTopologyInstance> &instances, quint16 position)
{
    const auto iterator = std::lower_bound(
        instances.cbegin(),
        instances.cend(),
        position,
        [](const SemanticBindingTopologyInstance &instance, quint16 wanted) {
            return instance.position < wanted;
        });
    return iterator != instances.cend() && iterator->position == position ? &*iterator : nullptr;
}

bool validateContainerAndManifest(
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const EcfgConfiguration &configuration,
    QString *error)
{
    if ((manifest.trust != EcpkgTrustClass::Production
         && manifest.trust != EcpkgTrustClass::Engineering)
        || !validDigest(container.packageSha256) || !validDigest(manifest.packageSha256)
        || !validDigest(manifest.manifestSha256) || !validDigest(manifest.signingKeyIdSha256)
        || container.packageSha256 != manifest.packageSha256
        || sha256(container.manifestJson) != manifest.manifestSha256
        || !sameDigestRecord(manifest.capability, container.capabilityBin)
        || !sameDigestRecord(manifest.configuration, container.configurationEcfg)
        || !sameDigestRecord(manifest.runtime, container.runtimeErun)
        || !sameDigestRecord(manifest.compileReport, container.compileReportJson)) {
        *error = QString::fromLatin1(
            "the container and verified signed-manifest result do not describe the same ECPKG");
        return false;
    }
    if (!manifest.semanticBinding) {
        *error = QString::fromLatin1("the signed manifest has no semantic_binding summary");
        return false;
    }
    if (!configuration.configurationId || configuration.configurationId != manifest.configurationId
        || configuration.configurationSha256 != manifest.configuration.sha256
        || configuration.configurationSha256 != sha256(container.configurationEcfg)
        || configuration.capabilitySha256 != manifest.capability.sha256
        || !configuration.catalogRevision || !configuration.topologyIdentity
        || configuration.resources.isEmpty()) {
        *error = QString::fromLatin1(
            "the parsed configuration and verified manifest do not describe the same ECPKG");
        return false;
    }
    return true;
}

bool validateReportEnvelope(
    const StrictJson &report,
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const EcfgConfiguration &configuration,
    quint32 *declaredBindingCount,
    QString *error)
{
    const QString root = QString::fromLatin1("$.compile_report");
    if (!hasFields(
            report,
            root,
            {"format",
             "production",
             "package",
             "esi_project",
             "runtime_resource_table",
             "semantic_binding_request",
             "semantic_binding_manifest",
             "semantic_binding_manifest_sha256",
             "semantic_symbols",
             "instances"},
            error)
        || !readConstantString(
            report.at("format"),
            "ethercat-adapter-project-report-v1",
            root + ".format",
            error)) {
        return false;
    }

    bool production = false;
    if (!readBoolean(report.at("production"), root + ".production", &production, error)
        || production != (manifest.trust == EcpkgTrustClass::Production)) {
        if (error->isEmpty())
            *error = root + QString::fromLatin1(".production differs from signed trust");
        return false;
    }

    const StrictJson &request = report.at("semantic_binding_request");
    quint64 requestedFormat = 0;
    quint64 requestedCount = 0;
    if (!hasExactFields(
            request,
            root + ".semantic_binding_request",
            {"format_version", "binding_count"},
            error)
        || !readUnsigned(
            request.at("format_version"),
            semanticBindingFormatVersion,
            semanticBindingFormatVersion,
            root + ".semantic_binding_request.format_version",
            &requestedFormat,
            error)
        || !readUnsigned(
            request.at("binding_count"),
            1,
            maximumBindings,
            root + ".semantic_binding_request.binding_count",
            &requestedCount,
            error)) {
        return false;
    }
    Q_UNUSED(requestedFormat)
    *declaredBindingCount = quint32(requestedCount);

    const StrictJson &resourceTable = report.at("runtime_resource_table");
    bool enabled = false;
    quint64 tableVersion = 0;
    quint64 tableCount = 0;
    if (!hasExactFields(
            resourceTable,
            root + ".runtime_resource_table",
            {"enabled", "format_version", "resource_count"},
            error)
        || !readBoolean(
            resourceTable.at("enabled"),
            root + ".runtime_resource_table.enabled",
            &enabled,
            error)
        || !enabled
        || !readUnsigned(
            resourceTable.at("format_version"),
            1,
            1,
            root + ".runtime_resource_table.format_version",
            &tableVersion,
            error)
        || !readUnsigned(
            resourceTable.at("resource_count"),
            1,
            maximumBindings,
            root + ".runtime_resource_table.resource_count",
            &tableCount,
            error)
        || tableCount != quint64(configuration.resources.size())) {
        if (error->isEmpty()) {
            *error = root
                     + QString::fromLatin1(
                         ".runtime_resource_table differs from configuration.ecfg");
        }
        return false;
    }
    Q_UNUSED(tableVersion)

    const StrictJson &package = report.at("package");
    if (!hasFields(
            package,
            root + ".package",
            {"capability_sha256",
             "configuration_sha256",
             "runtime_sha256",
             "configuration_bytes",
             "runtime_bytes",
             "configuration_id",
             "resource_count",
             "resource_table_version",
             "catalog_revision"},
            error)) {
        return false;
    }
    QByteArray capabilitySha;
    QByteArray configurationSha;
    QByteArray runtimeSha;
    quint64 configurationBytes = 0;
    quint64 runtimeBytes = 0;
    quint64 configurationId = 0;
    quint64 resourceCount = 0;
    quint64 resourceVersion = 0;
    quint64 catalogRevision = 0;
    if (!readSha256(
            package.at("capability_sha256"),
            root + ".package.capability_sha256",
            &capabilitySha,
            error)
        || !readSha256(
            package.at("configuration_sha256"),
            root + ".package.configuration_sha256",
            &configurationSha,
            error)
        || !readSha256(
            package.at("runtime_sha256"),
            root + ".package.runtime_sha256",
            &runtimeSha,
            error)
        || !readUnsigned(
            package.at("configuration_bytes"),
            1,
            std::numeric_limits<quint32>::max(),
            root + ".package.configuration_bytes",
            &configurationBytes,
            error)
        || !readUnsigned(
            package.at("runtime_bytes"),
            1,
            std::numeric_limits<quint32>::max(),
            root + ".package.runtime_bytes",
            &runtimeBytes,
            error)
        || !readUnsigned(
            package.at("configuration_id"),
            1,
            std::numeric_limits<quint64>::max(),
            root + ".package.configuration_id",
            &configurationId,
            error)
        || !readUnsigned(
            package.at("resource_count"),
            1,
            maximumBindings,
            root + ".package.resource_count",
            &resourceCount,
            error)
        || !readUnsigned(
            package.at("resource_table_version"),
            1,
            1,
            root + ".package.resource_table_version",
            &resourceVersion,
            error)
        || !readUnsigned(
            package.at("catalog_revision"),
            1,
            std::numeric_limits<quint64>::max(),
            root + ".package.catalog_revision",
            &catalogRevision,
            error)) {
        return false;
    }
    Q_UNUSED(resourceVersion)
    if (capabilitySha != manifest.capability.sha256
        || configurationSha != manifest.configuration.sha256
        || runtimeSha != manifest.runtime.sha256
        || configurationBytes != quint64(container.configurationEcfg.size())
        || runtimeBytes != quint64(container.runtimeErun.size())
        || configurationId != configuration.configurationId
        || resourceCount != quint64(configuration.resources.size())
        || catalogRevision != configuration.catalogRevision) {
        *error = root + QString::fromLatin1(".package differs from signed ECPKG content");
        return false;
    }

    const StrictJson &esiProject = report.at("esi_project");
    quint64 inputBytes = 0;
    quint64 outputBytes = 0;
    if (!hasFields(esiProject, root + ".esi_project", {"input_bytes", "output_bytes"}, error)
        || !readUnsigned(
            esiProject.at("input_bytes"),
            0,
            std::numeric_limits<quint32>::max(),
            root + ".esi_project.input_bytes",
            &inputBytes,
            error)
        || !readUnsigned(
            esiProject.at("output_bytes"),
            0,
            std::numeric_limits<quint32>::max(),
            root + ".esi_project.output_bytes",
            &outputBytes,
            error)
        || configuration.processInputBits % 8 || configuration.processOutputBits % 8
        || inputBytes != configuration.processInputBits / 8
        || outputBytes != configuration.processOutputBits / 8) {
        if (error->isEmpty()) {
            *error = root
                     + QString::fromLatin1(
                         ".esi_project process-image sizes differ from configuration.ecfg");
        }
        return false;
    }
    return true;
}

bool validateArtifactPackage(
    const StrictJson &value,
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const EcfgConfiguration &configuration,
    VerifiedSemanticBindingArtifact *result,
    QString *error)
{
    const QString path = QString::fromLatin1("$.semantic_binding_manifest.package");
    if (!hasExactFields(
            value,
            path,
            {"configuration_id",
             "build_timestamp",
             "capability_sha256",
             "configuration_sha256",
             "runtime_sha256"},
            error)) {
        return false;
    }
    quint64 configurationId = 0;
    quint64 buildTimestamp = 0;
    QByteArray capabilitySha;
    QByteArray configurationSha;
    QByteArray runtimeSha;
    if (!readUnsigned(
            value.at("configuration_id"),
            1,
            std::numeric_limits<quint64>::max(),
            path + ".configuration_id",
            &configurationId,
            error)
        || !readUnsigned(
            value.at("build_timestamp"),
            1,
            std::numeric_limits<quint64>::max(),
            path + ".build_timestamp",
            &buildTimestamp,
            error)
        || !readSha256(
            value.at("capability_sha256"), path + ".capability_sha256", &capabilitySha, error)
        || !readSha256(
            value.at("configuration_sha256"),
            path + ".configuration_sha256",
            &configurationSha,
            error)
        || !readSha256(value.at("runtime_sha256"), path + ".runtime_sha256", &runtimeSha, error)) {
        return false;
    }
    if (configurationId != configuration.configurationId
        || configurationId != manifest.configurationId
        || buildTimestamp != configuration.buildTimestamp
        || capabilitySha != manifest.capability.sha256
        || capabilitySha != sha256(container.capabilityBin)
        || configurationSha != configuration.configurationSha256
        || configurationSha != manifest.configuration.sha256
        || runtimeSha != manifest.runtime.sha256 || runtimeSha != sha256(container.runtimeErun)) {
        *error = path + QString::fromLatin1(" differs from the signed package identity");
        return false;
    }

    result->configurationId = configurationId;
    result->buildTimestamp = buildTimestamp;
    result->capabilitySha256 = std::move(capabilitySha);
    result->configurationSha256 = std::move(configurationSha);
    result->runtimeSha256 = std::move(runtimeSha);
    return true;
}

bool validateArtifactCatalog(
    const StrictJson &value,
    const VerifiedSignedEcpkgManifest &manifest,
    const EcfgConfiguration &configuration,
    VerifiedSemanticBindingArtifact *result,
    QString *error)
{
    const QString path = QString::fromLatin1("$.semantic_binding_manifest.resource_catalog");
    if (!hasExactFields(
            value,
            path,
            {"format_version",
             "catalog_revision",
             "topology_identity",
             "resource_count",
             "records_sha256",
             "section_sha256"},
            error)) {
        return false;
    }
    quint64 formatVersion = 0;
    quint64 catalogRevision = 0;
    quint64 topologyIdentity = 0;
    quint64 resourceCount = 0;
    QByteArray recordsSha;
    QByteArray sectionSha;
    if (!readUnsigned(
            value.at("format_version"),
            1,
            1,
            path + ".format_version",
            &formatVersion,
            error)
        || !readFixedHex(
            value.at("catalog_revision"),
            16,
            path + ".catalog_revision",
            &catalogRevision,
            error)
        || !readFixedHex(
            value.at("topology_identity"),
            16,
            path + ".topology_identity",
            &topologyIdentity,
            error)
        || !readUnsigned(
            value.at("resource_count"),
            1,
            maximumBindings,
            path + ".resource_count",
            &resourceCount,
            error)
        || !readSha256(value.at("records_sha256"), path + ".records_sha256", &recordsSha, error)
        || !readSha256(value.at("section_sha256"), path + ".section_sha256", &sectionSha, error)) {
        return false;
    }
    Q_UNUSED(formatVersion)
    const SignedEcpkgSemanticBindingSummary &summary = *manifest.semanticBinding;
    if (catalogRevision != configuration.catalogRevision
        || topologyIdentity != configuration.topologyIdentity
        || resourceCount != quint64(configuration.resources.size())
        || recordsSha != configuration.resourceRecordsSha256
        || sectionSha != configuration.resourceTableSectionSha256
        || summary.formatVersion != semanticBindingFormatVersion
        || summary.bindingCount != resourceCount || summary.catalogRevision != catalogRevision
        || summary.topologyIdentity != topologyIdentity
        || summary.resourceRecordsSha256 != recordsSha
        || summary.resourceSectionSha256 != sectionSha) {
        *error = path
                 + QString::fromLatin1(
                     " differs from configuration.ecfg or the signed manifest summary");
        return false;
    }

    result->catalogRevision = catalogRevision;
    result->topologyIdentity = topologyIdentity;
    result->resourceRecordsSha256 = std::move(recordsSha);
    result->resourceSectionSha256 = std::move(sectionSha);
    return true;
}

bool validateTopology(
    const StrictJson &artifactTopology,
    const StrictJson &reportInstances,
    const VerifiedSignedEcpkgManifest &manifest,
    qsizetype maximumArtifactBytes,
    VerifiedSemanticBindingArtifact *result,
    QString *error)
{
    const QString path = QString::fromLatin1("$.semantic_binding_manifest.topology");
    if (!hasExactFields(artifactTopology, path, {"sha256", "instances"}, error))
        return false;
    const StrictJson &instances = artifactTopology.at("instances");
    if (!instances.is_array() || instances.empty() || instances.size() > maximumTopologyInstances) {
        *error = path
                 + QString::fromLatin1(
                     ".instances must contain between 1 and %1 records")
                       .arg(maximumTopologyInstances);
        return false;
    }
    if (!reportInstances.is_array() || reportInstances.size() != instances.size()) {
        *error = QString::fromLatin1(
            "$.compile_report.instances differs from the semantic topology instance count");
        return false;
    }

    QList<SemanticBindingTopologyInstance> artifactInstances;
    artifactInstances.reserve(qsizetype(instances.size()));
    QSet<quint16> stations;
    quint16 previousPosition = 0;
    bool hasPreviousPosition = false;
    for (std::size_t index = 0; index < instances.size(); ++index) {
        SemanticBindingTopologyInstance instance;
        if (!parseTopologyInstance(
                instances[index], arrayPath(path + ".instances", index), true, &instance, error)) {
            return false;
        }
        if ((hasPreviousPosition && instance.position <= previousPosition)
            || stations.contains(instance.stationAddress)) {
            *error = QString::fromLatin1(
                         "%1 must use strictly increasing positions and unique station addresses")
                         .arg(arrayPath(path + ".instances", index));
            return false;
        }
        previousPosition = instance.position;
        hasPreviousPosition = true;
        stations.insert(instance.stationAddress);
        artifactInstances.append(std::move(instance));
    }

    QList<SemanticBindingTopologyInstance> compiledInstances;
    compiledInstances.reserve(qsizetype(reportInstances.size()));
    for (std::size_t index = 0; index < reportInstances.size(); ++index) {
        SemanticBindingTopologyInstance instance;
        if (!parseTopologyInstance(
                reportInstances[index],
                arrayPath(QString::fromLatin1("$.compile_report.instances"), index),
                false,
                &instance,
                error)) {
            return false;
        }
        compiledInstances.append(std::move(instance));
    }
    std::sort(
        compiledInstances.begin(),
        compiledInstances.end(),
        [](const SemanticBindingTopologyInstance &left,
           const SemanticBindingTopologyInstance &right) {
            return left.position < right.position;
        });
    for (qsizetype index = 0; index < compiledInstances.size(); ++index) {
        if ((index && compiledInstances.at(index - 1).position
                          == compiledInstances.at(index).position)
            || !sameTopologyInstance(artifactInstances.at(index), compiledInstances.at(index))) {
            *error = QString::fromLatin1(
                         "%1 differs from the signed compile-report topology evidence")
                         .arg(arrayPath(path + ".instances", std::size_t(index)));
            return false;
        }
    }

    StrictJson topologyDigestInput = StrictJson::object();
    topologyDigestInput["instances"] = instances;
    Utils::Result<QByteArray> canonical
        = serializeCanonicalJson(topologyDigestInput, maximumArtifactBytes);
    if (!canonical) {
        *error = QString::fromLatin1("cannot canonicalize topology evidence: %1")
                     .arg(canonical.error());
        return false;
    }
    QByteArray declaredTopologySha;
    if (!readSha256(
            artifactTopology.at("sha256"), path + ".sha256", &declaredTopologySha, error)) {
        return false;
    }
    const QByteArray actualTopologySha = sha256(*canonical);
    if (declaredTopologySha != actualTopologySha
        || declaredTopologySha != manifest.semanticBinding->topologySha256) {
        *error = path
                 + QString::fromLatin1(
                     ".sha256 differs from canonical topology evidence or signed summary");
        return false;
    }

    result->topologySha256 = std::move(declaredTopologySha);
    result->topologyInstances = std::move(artifactInstances);
    return true;
}

bool validateReportSymbolAgainstResource(
    const ReportSemanticSymbol &symbol,
    const EcfgRuntimeResource &resource,
    const QString &path,
    QString *error)
{
    if (symbol.resourceId != resource.resourceId
        || symbol.componentInstanceId != resource.componentInstanceId
        || symbol.parentInstanceId != resource.parentInstanceId
        || symbol.instanceOrdinal != resource.instanceOrdinal
        || symbol.consistencyGroupId != resource.consistencyGroupId
        || symbol.primitive != resource.primitive || symbol.bitWidth != resource.bitWidth
        || symbol.direction != resource.direction || symbol.access != resource.access
        || symbol.source != resource.source
        || symbol.processImageBitOffset != resource.processImageBitOffset) {
        *error = QString::fromLatin1(
                     "%1 differs from the exact signed ResourceTable descriptor")
                     .arg(path);
        return false;
    }
    return true;
}

bool validateBindingAgainstEvidence(
    VerifiedSemanticBinding *binding,
    const ReportSemanticSymbol &symbol,
    const EcfgRuntimeResource &resource,
    const SemanticBindingTopologyInstance &instance,
    const QString &path,
    QString *error)
{
    if (binding->semanticSignalId != symbol.semanticSignalId
        || binding->resourceId != symbol.resourceId
        || binding->componentInstanceId != symbol.componentInstanceId
        || binding->consistencyGroupId != symbol.consistencyGroupId
        || binding->primitive != symbol.primitive || binding->bitWidth != symbol.bitWidth
        || binding->direction != symbol.direction || binding->access != symbol.access
        || binding->adapterId != symbol.adapterId
        || binding->adapterVersion != symbol.adapterVersion
        || binding->canonicalSymbol != symbol.canonicalSymbol
        || binding->position != symbol.position || binding->stationAddress != symbol.stationAddress
        || binding->slot != symbol.slot || binding->position != instance.position
        || binding->stationAddress != instance.stationAddress
        || binding->adapterId != instance.adapterId
        || binding->adapterVersion != instance.adapterVersion
        || binding->adapterSha256 != instance.adapterSha256
        || binding->esiSha256 != instance.esiSha256) {
        *error = QString::fromLatin1(
                     "%1 differs from the explicit compile-report binding or provenance evidence")
                     .arg(path);
        return false;
    }

    binding->parentInstanceId = resource.parentInstanceId;
    binding->instanceOrdinal = resource.instanceOrdinal;
    binding->source = resource.source;
    binding->processImageBitOffset = resource.processImageBitOffset;
    binding->processImageBitLength = resource.processImageBitLength;
    binding->valueBytes = resource.valueBytes;
    binding->safeValueBytes = resource.safeValueBytes;
    binding->qualityMask = resource.qualityMask;
    binding->safeValueLittleEndian = resource.safeValueLittleEndian;
    return true;
}

bool validateBindings(
    const StrictJson &artifactBindings,
    const StrictJson &reportSymbols,
    quint32 declaredBindingCount,
    const EcfgConfiguration &configuration,
    VerifiedSemanticBindingArtifact *result,
    QString *error)
{
    const QString artifactPath = QString::fromLatin1("$.semantic_binding_manifest.bindings");
    if (!artifactBindings.is_array() || artifactBindings.empty()
        || artifactBindings.size() > maximumBindings
        || artifactBindings.size() != std::size_t(declaredBindingCount)
        || artifactBindings.size() != std::size_t(configuration.resources.size())) {
        *error = artifactPath
                 + QString::fromLatin1(
                     " must cover every ResourceTable record exactly once");
        return false;
    }
    if (!reportSymbols.is_array() || reportSymbols.size() != artifactBindings.size()) {
        *error = QString::fromLatin1(
            "$.compile_report.semantic_symbols must explicitly bind every ResourceTable record");
        return false;
    }

    QHash<quint64, const EcfgRuntimeResource *> resources;
    resources.reserve(configuration.resources.size());
    for (const EcfgRuntimeResource &resource : configuration.resources)
        resources.insert(resource.resourceId, &resource);

    QHash<quint64, ReportSemanticSymbol> symbols;
    symbols.reserve(qsizetype(reportSymbols.size()));
    QSet<QString> reportSemanticIds;
    for (std::size_t index = 0; index < reportSymbols.size(); ++index) {
        ReportSemanticSymbol symbol;
        const QString path
            = arrayPath(QString::fromLatin1("$.compile_report.semantic_symbols"), index);
        if (!parseReportSemanticSymbol(reportSymbols[index], path, &symbol, error))
            return false;
        const EcfgRuntimeResource *resource = resources.value(symbol.resourceId);
        if (!resource || symbols.contains(symbol.resourceId)
            || reportSemanticIds.contains(symbol.semanticSignalId)
            || !validateReportSymbolAgainstResource(symbol, *resource, path, error)) {
            if (error->isEmpty()) {
                *error = QString::fromLatin1(
                             "%1 is duplicate or references an unknown ResourceId")
                             .arg(path);
            }
            return false;
        }
        if (!findTopologyInstance(result->topologyInstances, symbol.position)) {
            *error = QString::fromLatin1(
                         "%1 has no exact compile-report topology instance")
                         .arg(path);
            return false;
        }
        reportSemanticIds.insert(symbol.semanticSignalId);
        symbols.insert(symbol.resourceId, std::move(symbol));
    }
    if (symbols.size() != resources.size()) {
        *error = QString::fromLatin1(
            "$.compile_report.semantic_symbols does not cover the ResourceTable");
        return false;
    }

    QList<VerifiedSemanticBinding> bindings;
    bindings.reserve(qsizetype(artifactBindings.size()));
    QSet<QString> semanticIds;
    QSet<quint64> resourceIds;
    QByteArray previousSemanticId;
    for (std::size_t index = 0; index < artifactBindings.size(); ++index) {
        VerifiedSemanticBinding binding;
        const QString path = arrayPath(artifactPath, index);
        if (!parseArtifactBinding(artifactBindings[index], path, &binding, error))
            return false;
        const QByteArray semanticId = binding.semanticSignalId.toLatin1();
        if ((!previousSemanticId.isEmpty() && semanticId <= previousSemanticId)
            || semanticIds.contains(binding.semanticSignalId)
            || resourceIds.contains(binding.resourceId)) {
            *error = QString::fromLatin1(
                         "%1 is duplicate or not in canonical SemanticSignalId order")
                         .arg(path);
            return false;
        }
        previousSemanticId = semanticId;
        semanticIds.insert(binding.semanticSignalId);
        resourceIds.insert(binding.resourceId);

        const EcfgRuntimeResource *resource = resources.value(binding.resourceId);
        const auto symbolIterator = symbols.constFind(binding.resourceId);
        const SemanticBindingTopologyInstance *instance
            = findTopologyInstance(result->topologyInstances, binding.position);
        if (!resource || symbolIterator == symbols.cend() || !instance) {
            *error = QString::fromLatin1(
                         "%1 references an unknown explicit runtime identity")
                         .arg(path);
            return false;
        }
        if (!validateBindingAgainstEvidence(
                &binding, *symbolIterator, *resource, *instance, path, error)) {
            return false;
        }
        bindings.append(std::move(binding));
    }
    if (resourceIds.size() != resources.size()) {
        *error = artifactPath + QString::fromLatin1(" does not cover the ResourceTable");
        return false;
    }
    result->bindings = std::move(bindings);
    return true;
}

} // namespace

const VerifiedSemanticBinding *VerifiedSemanticBindingArtifact::findBySemanticSignalId(
    QStringView semanticSignalId) const
{
    const auto iterator = std::lower_bound(
        bindings.cbegin(),
        bindings.cend(),
        semanticSignalId,
        [](const VerifiedSemanticBinding &binding, QStringView wanted) {
            return QStringView(binding.semanticSignalId).compare(wanted) < 0;
        });
    if (iterator == bindings.cend()
        || QStringView(iterator->semanticSignalId) != semanticSignalId) {
        return nullptr;
    }
    return &*iterator;
}

const VerifiedSemanticBinding *VerifiedSemanticBindingArtifact::findByResourceId(
    quint64 resourceId) const
{
    const auto iterator = std::find_if(
        bindings.cbegin(), bindings.cend(), [resourceId](const VerifiedSemanticBinding &binding) {
            return binding.resourceId == resourceId;
        });
    return iterator == bindings.cend() ? nullptr : &*iterator;
}

Utils::Result<VerifiedSemanticBindingArtifact> verifySemanticBindingArtifact(
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const EcfgConfiguration &configuration,
    qsizetype maximumCompileReportBytes,
    qsizetype maximumArtifactBytes)
{
    if (maximumCompileReportBytes <= 0
        || maximumCompileReportBytes > defaultMaximumSemanticCompileReportBytes
        || maximumArtifactBytes <= 0
        || maximumArtifactBytes > defaultMaximumSemanticArtifactBytes) {
        return invalidArtifact(QString::fromLatin1("a configured size limit is invalid"));
    }
    if (container.compileReportJson.size() > maximumCompileReportBytes) {
        return invalidArtifact(
            QString::fromLatin1("compile_report.json exceeds the configured size limit"));
    }

    QString error;
    if (!validateContainerAndManifest(container, manifest, configuration, &error))
        return invalidArtifact(error);

    Utils::Result<StrictJson> report
        = parseStrictJson(container.compileReportJson, maximumCompileReportBytes);
    if (!report)
        return invalidArtifact(report.error());

    try {
        quint32 declaredBindingCount = 0;
        if (!validateReportEnvelope(
                *report,
                container,
                manifest,
                configuration,
                &declaredBindingCount,
                &error)) {
            return invalidArtifact(error);
        }

        const StrictJson &artifact = report->at("semantic_binding_manifest");
        const QString artifactPath = QString::fromLatin1("$.semantic_binding_manifest");
        if (!hasExactFields(
                artifact,
                artifactPath,
                {"format",
                 "format_version",
                 "binding_count",
                 "package",
                 "resource_catalog",
                 "topology",
                 "bindings"},
                &error)
            || !readConstantString(
                artifact.at("format"),
                "ethercat-semantic-binding-v1",
                artifactPath + ".format",
                &error)) {
            return invalidArtifact(error);
        }

        quint64 formatVersion = 0;
        quint64 bindingCount = 0;
        if (!readUnsigned(
                artifact.at("format_version"),
                semanticBindingFormatVersion,
                semanticBindingFormatVersion,
                artifactPath + ".format_version",
                &formatVersion,
                &error)
            || !readUnsigned(
                artifact.at("binding_count"),
                1,
                maximumBindings,
                artifactPath + ".binding_count",
                &bindingCount,
                &error)
            || bindingCount != declaredBindingCount
            || bindingCount != manifest.semanticBinding->bindingCount) {
            if (error.isEmpty()) {
                error = artifactPath
                        + QString::fromLatin1(
                            ".binding_count differs from compile or signed summary evidence");
            }
            return invalidArtifact(error);
        }
        Q_UNUSED(formatVersion)

        Utils::Result<QByteArray> canonical
            = serializeCanonicalJson(artifact, maximumArtifactBytes);
        if (!canonical)
            return invalidArtifact(canonical.error());
        const QByteArray artifactSha = sha256(*canonical);

        QByteArray reportArtifactSha;
        if (!readSha256(
                report->at("semantic_binding_manifest_sha256"),
                QString::fromLatin1("$.compile_report.semantic_binding_manifest_sha256"),
                &reportArtifactSha,
                &error)
            || reportArtifactSha != artifactSha
            || reportArtifactSha != manifest.semanticBinding->mappingSha256) {
            if (error.isEmpty()) {
                error = QString::fromLatin1(
                    "the canonical artifact digest differs from compile and signed summary "
                    "evidence");
            }
            return invalidArtifact(error);
        }

        VerifiedSemanticBindingArtifact result;
        result.trust = manifest.trust;
        result.packageSha256 = manifest.packageSha256;
        result.manifestSha256 = manifest.manifestSha256;
        result.signingKeyIdSha256 = manifest.signingKeyIdSha256;
        result.canonicalArtifact = std::move(*canonical);
        result.artifactSha256 = std::move(artifactSha);

        if (!validateArtifactPackage(
                artifact.at("package"),
                container,
                manifest,
                configuration,
                &result,
                &error)
            || !validateArtifactCatalog(
                artifact.at("resource_catalog"), manifest, configuration, &result, &error)
            || !validateTopology(
                artifact.at("topology"),
                report->at("instances"),
                manifest,
                maximumArtifactBytes,
                &result,
                &error)
            || !validateBindings(
                artifact.at("bindings"),
                report->at("semantic_symbols"),
                declaredBindingCount,
                configuration,
                &result,
                &error)) {
            return invalidArtifact(error);
        }
        return result;
    } catch (const std::exception &exception) {
        return invalidArtifact(
            QString::fromLatin1("validation failed: %1").arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        return invalidArtifact(QString::fromLatin1("validation failed with an unknown error"));
    }
}

} // namespace EtherCAT::SemanticRuntime::Internal
