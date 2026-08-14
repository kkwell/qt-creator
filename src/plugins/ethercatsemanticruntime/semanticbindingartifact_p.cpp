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

bool isAcyclicSemanticComponentParentGraph(
    const QList<VerifiedSemanticComponent> &components)
{
    QHash<QString, qsizetype> componentIndexes;
    componentIndexes.reserve(components.size());
    for (qsizetype index = 0; index < components.size(); ++index) {
        const VerifiedSemanticComponent &component = components.at(index);
        if (component.componentBindingId.isEmpty()
            || componentIndexes.contains(component.componentBindingId)) {
            return false;
        }
        componentIndexes.insert(component.componentBindingId, index);
    }

    for (qsizetype startIndex = 0; startIndex < components.size(); ++startIndex) {
        QSet<qsizetype> path;
        qsizetype currentIndex = startIndex;
        while (currentIndex >= 0) {
            if (path.contains(currentIndex))
                return false;
            path.insert(currentIndex);

            const VerifiedSemanticComponent &component = components.at(currentIndex);
            if (!component.parentComponentBindingId)
                break;
            const qsizetype parentIndex
                = componentIndexes.value(*component.parentComponentBindingId, -1);
            if (parentIndex < 0
                || components.at(parentIndex).componentInstanceId
                       != component.parentInstanceId) {
                return false;
            }
            currentIndex = parentIndex;
        }
    }
    return true;
}

bool isValidSemanticMaskedWaitCondition(quint64 mask, quint64 value, quint16 bitWidth)
{
    if (!mask || !bitWidth || bitWidth > 64 || (value & ~mask))
        return false;
    return bitWidth == 64 || ((mask | value) >> bitWidth) == 0;
}

namespace {

constexpr quint32 semanticBindingFormatVersion1 = 1;
constexpr quint32 semanticBindingFormatVersion2 = 2;
constexpr quint32 maximumBindings = 8192;
constexpr quint32 maximumTopologyInstances = 256;
constexpr quint32 maximumDevices = 256;
constexpr quint32 maximumComponentsPerDevice = 256;
constexpr quint32 maximumActions = 4096;
constexpr quint32 maximumActionBindings = 256;
constexpr quint32 maximumActionParameters = 64;
constexpr quint32 maximumActionGroups = 64;
constexpr quint32 maximumActionSteps = 256;
constexpr quint32 maximumActionAssignments = 64;
constexpr qsizetype sha256Bytes = 32;

struct ReportSemanticSymbol
{
    QString semanticSignalId;
    QString semanticSignalDefinitionId;
    QString semanticBindingId;
    QString projectDeviceId;
    QString componentBindingId;
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
    std::optional<QString> unit;
    qint64 scaleNumerator = 1;
    qint64 scaleDenominator = 1;
    qint64 scaleOffset = 0;
    bool safeValueDeclared = false;
    std::optional<std::variant<qint64, quint64>> safeValue;
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

bool readOptionalString(
    const StrictJson &value,
    qsizetype maximum,
    const QString &path,
    std::optional<QString> *result,
    QString *error)
{
    if (value.is_null()) {
        result->reset();
        return true;
    }
    QString text;
    if (!readString(value, 0, maximum, path, &text, error))
        return false;
    *result = std::move(text);
    return true;
}

bool readSignedInteger(
    const StrictJson &value, const QString &path, qint64 *result, QString *error)
{
    if (value.is_boolean() || !value.is_number_integer()) {
        *error = QString::fromLatin1("%1 must be an integer").arg(path);
        return false;
    }
    if (value.is_number_unsigned()) {
        const quint64 number = value.get<StrictJson::number_unsigned_t>();
        if (number > quint64(std::numeric_limits<qint64>::max())) {
            *error = QString::fromLatin1("%1 is outside the supported signed 64-bit range")
                         .arg(path);
            return false;
        }
        *result = qint64(number);
        return true;
    }
    *result = qint64(value.get<StrictJson::number_integer_t>());
    return true;
}

bool readIntegerValue(
    const StrictJson &value,
    const QString &path,
    std::variant<qint64, quint64> *result,
    QString *error)
{
    if (value.is_boolean() || !value.is_number_integer()) {
        *error = QString::fromLatin1("%1 must be an integer").arg(path);
        return false;
    }
    if (value.is_number_unsigned()) {
        *result = quint64(value.get<StrictJson::number_unsigned_t>());
        return true;
    }
    const qint64 number = qint64(value.get<StrictJson::number_integer_t>());
    if (number < 0)
        *result = number;
    else
        *result = quint64(number);
    return true;
}

bool readScale(
    const StrictJson &value,
    const QString &path,
    qint64 *numerator,
    qint64 *denominator,
    qint64 *offset,
    QString *error)
{
    if (!hasExactFields(value, path, {"numerator", "denominator", "offset"}, error)
        || !readSignedInteger(value.at("numerator"), path + ".numerator", numerator, error)
        || !readSignedInteger(value.at("denominator"), path + ".denominator", denominator, error)
        || !*denominator
        || !readSignedInteger(value.at("offset"), path + ".offset", offset, error)) {
        if (error->isEmpty())
            *error = path + QString::fromLatin1(".denominator must be nonzero");
        return false;
    }
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

bool primitiveIsSigned(EcfgResourcePrimitive primitive)
{
    return primitive == EcfgResourcePrimitive::S8 || primitive == EcfgResourcePrimitive::S16
           || primitive == EcfgResourcePrimitive::S32 || primitive == EcfgResourcePrimitive::S64
           || primitive == EcfgResourcePrimitive::Q32_32;
}

bool integerFits(
    const std::variant<qint64, quint64> &value,
    EcfgResourcePrimitive primitive,
    quint16 bitWidth)
{
    if (!bitWidth || bitWidth > 128)
        return false;
    if (primitive == EcfgResourcePrimitive::Bool) {
        return bitWidth == 1 && std::holds_alternative<quint64>(value)
               && std::get<quint64>(value) <= 1;
    }

    if (primitiveIsSigned(primitive)) {
        if (bitWidth > 64)
            return false;
        if (std::holds_alternative<quint64>(value)) {
            const quint64 number = std::get<quint64>(value);
            return bitWidth == 64 ? number <= quint64(std::numeric_limits<qint64>::max())
                                  : number < (quint64(1) << (bitWidth - 1));
        }
        if (bitWidth == 64)
            return true;
        const qint64 number = std::get<qint64>(value);
        const qint64 limit = qint64(1) << (bitWidth - 1);
        return number >= -limit && number < limit;
    }

    if (std::holds_alternative<qint64>(value))
        return false;
    const quint64 number = std::get<quint64>(value);
    return bitWidth >= 64 || number < (quint64(1) << bitWidth);
}

QByteArray integerLittleEndian(
    const std::variant<qint64, quint64> &value, quint16 bitWidth)
{
    const qsizetype bytes = (bitWidth + 7) / 8;
    QByteArray encoded(bytes, '\0');
    const bool negative
        = std::holds_alternative<qint64>(value) && std::get<qint64>(value) < 0;
    quint64 low = std::holds_alternative<qint64>(value)
                      ? quint64(std::get<qint64>(value))
                      : std::get<quint64>(value);
    for (qsizetype index = 0; index < bytes; ++index) {
        if (index < 8)
            encoded[index] = char(low >> (index * 8));
        else
            encoded[index] = negative ? char(0xff) : char(0);
    }
    if (bitWidth % 8) {
        const quint8 mask = quint8((quint16(1) << (bitWidth % 8)) - 1);
        encoded[bytes - 1] = char(quint8(encoded.at(bytes - 1)) & mask);
    }
    return encoded;
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
    quint16 formatVersion,
    VerifiedSemanticBinding *result,
    QString *error)
{
    const bool v2 = formatVersion == semanticBindingFormatVersion2;
    const bool fieldsValid
        = v2 ? hasExactFields(
                   value,
                   path,
                   {"semantic_signal_definition_id",
                    "semantic_binding_id",
                    "project_device_id",
                    "component_binding_id",
                    "resource_id",
                    "component_instance_id",
                    "consistency_group_id",
                    "primitive",
                    "bit_width",
                    "direction",
                    "access",
                    "unit",
                    "scale",
                    "safe_value_declared",
                    "safe_value",
                    "adapter_id",
                    "adapter_version",
                    "adapter_sha256",
                    "esi_sha256",
                    "canonical_symbol",
                    "position",
                    "station_address",
                    "slot"},
                   error)
             : hasExactFields(
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
                   error);
    if (!fieldsValid)
        return false;

    quint64 resourceId = 0;
    quint64 componentInstanceId = 0;
    quint64 consistencyGroupId = 0;
    quint64 bitWidth = 0;
    quint64 position = 0;
    quint64 stationAddress = 0;
    quint64 slot = 0;
    const std::string_view identityField = v2 ? "semantic_binding_id" : "semantic_signal_id";
    if (!readSemanticSignalId(
            value.at(identityField),
            fieldPath(path, identityField),
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

    if (v2) {
        bool safeValueDeclared = false;
        if (!readSemanticSignalId(
                value.at("semantic_signal_definition_id"),
                path + ".semantic_signal_definition_id",
                &result->semanticSignalDefinitionId,
                error)
            || !readSemanticSignalId(
                value.at("semantic_binding_id"),
                path + ".semantic_binding_id",
                &result->semanticBindingId,
                error)
            || result->semanticBindingId != result->semanticSignalId
            || !readSemanticSignalId(
                value.at("project_device_id"),
                path + ".project_device_id",
                &result->projectDeviceId,
                error)
            || !readSemanticSignalId(
                value.at("component_binding_id"),
                path + ".component_binding_id",
                &result->componentBindingId,
                error)
            || !readOptionalString(value.at("unit"), 64, path + ".unit", &result->unit, error)
            || !readScale(
                value.at("scale"),
                path + ".scale",
                &result->scaleNumerator,
                &result->scaleDenominator,
                &result->scaleOffset,
                error)
            || !readBoolean(
                value.at("safe_value_declared"),
                path + ".safe_value_declared",
                &safeValueDeclared,
                error)) {
            if (error->isEmpty())
                *error = path + QString::fromLatin1(" contains inconsistent v2 identities");
            return false;
        }
        result->safeValueDeclared = safeValueDeclared;
        if (value.at("safe_value").is_null()) {
            result->safeValue.reset();
        } else {
            std::variant<qint64, quint64> safeValue;
            if (!readIntegerValue(value.at("safe_value"), path + ".safe_value", &safeValue, error))
                return false;
            result->safeValue = std::move(safeValue);
        }
        if (result->safeValueDeclared != result->safeValue.has_value()
            || (result->safeValue
                && !integerFits(*result->safeValue, result->primitive, quint16(bitWidth)))) {
            *error = path
                     + QString::fromLatin1(
                         " has an inconsistent or out-of-range safe-value declaration");
            return false;
        }
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
    quint16 formatVersion,
    ReportSemanticSymbol *result,
    QString *error)
{
    const bool v2 = formatVersion == semanticBindingFormatVersion2;
    const std::string_view identityField = v2 ? "semantic_binding_id" : "semantic_signal_id";
    if (!hasFields(
            value,
            path,
            {identityField,
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
            value.at(identityField),
            fieldPath(path, identityField),
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

    if (v2) {
        bool safeValueDeclared = false;
        if (!hasFields(
                value,
                path,
                {"semantic_definition_id",
                 "semantic_binding_id",
                 "project_device_id",
                 "component_binding_id",
                 "unit",
                 "scale",
                 "safe_value_declared",
                 "safe_value"},
                error)
            || !readSemanticSignalId(
                value.at("semantic_definition_id"),
                path + ".semantic_definition_id",
                &result->semanticSignalDefinitionId,
                error)
            || !readSemanticSignalId(
                value.at("semantic_binding_id"),
                path + ".semantic_binding_id",
                &result->semanticBindingId,
                error)
            || result->semanticBindingId != result->semanticSignalId
            || !readSemanticSignalId(
                value.at("project_device_id"),
                path + ".project_device_id",
                &result->projectDeviceId,
                error)
            || !readSemanticSignalId(
                value.at("component_binding_id"),
                path + ".component_binding_id",
                &result->componentBindingId,
                error)
            || !readOptionalString(value.at("unit"), 64, path + ".unit", &result->unit, error)
            || !readScale(
                value.at("scale"),
                path + ".scale",
                &result->scaleNumerator,
                &result->scaleDenominator,
                &result->scaleOffset,
                error)
            || !readBoolean(
                value.at("safe_value_declared"),
                path + ".safe_value_declared",
                &safeValueDeclared,
                error)) {
            if (error->isEmpty())
                *error = path + QString::fromLatin1(" contains inconsistent v2 identities");
            return false;
        }
        result->safeValueDeclared = safeValueDeclared;
        if (value.at("safe_value").is_null()) {
            result->safeValue.reset();
        } else {
            std::variant<qint64, quint64> safeValue;
            if (!readIntegerValue(value.at("safe_value"), path + ".safe_value", &safeValue, error))
                return false;
            result->safeValue = std::move(safeValue);
        }
        if (result->safeValueDeclared != result->safeValue.has_value()
            || (result->safeValue
                && !integerFits(*result->safeValue, result->primitive, quint16(bitWidth)))) {
            *error = path
                     + QString::fromLatin1(
                         " has an inconsistent or out-of-range safe-value declaration");
            return false;
        }
    }

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
    quint16 *declaredFormatVersion,
    quint32 *declaredBindingCount,
    quint32 *declaredDeviceCount,
    quint32 *declaredActionCount,
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
    if (!hasFields(
            request,
            root + ".semantic_binding_request",
            {"format_version", "binding_count"},
            error)
        || !readUnsigned(
            request.at("format_version"),
            semanticBindingFormatVersion1,
            semanticBindingFormatVersion2,
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
    const bool format2 = requestedFormat == semanticBindingFormatVersion2;
    if ((format2
         && !hasExactFields(
             request,
             root + ".semantic_binding_request",
             {"format_version", "binding_count", "device_count", "action_count"},
             error))
        || (!format2
            && !hasExactFields(
                request,
                root + ".semantic_binding_request",
                {"format_version", "binding_count"},
                error))) {
        return false;
    }
    quint64 requestedDeviceCount = 0;
    quint64 requestedActionCount = 0;
    if (format2
        && (!readUnsigned(
                request.at("device_count"),
                1,
                maximumDevices,
                root + ".semantic_binding_request.device_count",
                &requestedDeviceCount,
                error)
            || !readUnsigned(
                request.at("action_count"),
                1,
                maximumActions,
                root + ".semantic_binding_request.action_count",
                &requestedActionCount,
                error))) {
        return false;
    }
    if (format2
        && (!hasFields(
                report,
                root,
                {"project_device_bindings", "semantic_action_plans"},
                error)
            || !report.at("project_device_bindings").is_array()
            || report.at("project_device_bindings").size() != requestedDeviceCount
            || !report.at("semantic_action_plans").is_array()
            || report.at("semantic_action_plans").size() != requestedActionCount)) {
        if (error->isEmpty()) {
            *error = root
                     + QString::fromLatin1(
                         " v2 device/action evidence differs from the declared request");
        }
        return false;
    }
    *declaredFormatVersion = quint16(requestedFormat);
    *declaredBindingCount = quint32(requestedCount);
    *declaredDeviceCount = quint32(requestedDeviceCount);
    *declaredActionCount = quint32(requestedActionCount);

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
    quint16 semanticFormatVersion,
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
        || summary.formatVersion != semanticFormatVersion
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
    quint16 formatVersion,
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

    if (formatVersion == semanticBindingFormatVersion2) {
        if (binding->semanticSignalDefinitionId != symbol.semanticSignalDefinitionId
            || binding->semanticBindingId != symbol.semanticBindingId
            || binding->projectDeviceId != symbol.projectDeviceId
            || binding->componentBindingId != symbol.componentBindingId
            || binding->unit != symbol.unit
            || binding->scaleNumerator != symbol.scaleNumerator
            || binding->scaleDenominator != symbol.scaleDenominator
            || binding->scaleOffset != symbol.scaleOffset
            || binding->safeValueDeclared != symbol.safeValueDeclared
            || binding->safeValue != symbol.safeValue
            || binding->safeValueDeclared != (resource.safeValueBytes != 0)
            || (binding->safeValue
                && integerLittleEndian(*binding->safeValue, binding->bitWidth)
                       != resource.safeValueLittleEndian)) {
            *error = QString::fromLatin1(
                         "%1 differs from the signed v2 project binding or safe-value evidence")
                         .arg(path);
            return false;
        }
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
    quint16 formatVersion,
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
        if (!parseReportSemanticSymbol(
                reportSymbols[index], path, formatVersion, &symbol, error)) {
            return false;
        }
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
    QHash<QString, qsizetype> semanticDefinitionIndexes;
    QByteArray previousSemanticId;
    for (std::size_t index = 0; index < artifactBindings.size(); ++index) {
        VerifiedSemanticBinding binding;
        const QString path = arrayPath(artifactPath, index);
        if (!parseArtifactBinding(
                artifactBindings[index], path, formatVersion, &binding, error)) {
            return false;
        }
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
                &binding,
                *symbolIterator,
                *resource,
                *instance,
                formatVersion,
                path,
                error)) {
            return false;
        }
        if (formatVersion == semanticBindingFormatVersion2) {
            const auto definitionIterator
                = semanticDefinitionIndexes.constFind(binding.semanticSignalDefinitionId);
            if (definitionIterator != semanticDefinitionIndexes.cend()) {
                const VerifiedSemanticBinding &definition
                    = bindings.at(*definitionIterator);
                if (definition.primitive != binding.primitive
                    || definition.bitWidth != binding.bitWidth
                    || definition.direction != binding.direction
                    || definition.access != binding.access || definition.unit != binding.unit
                    || definition.scaleNumerator != binding.scaleNumerator
                    || definition.scaleDenominator != binding.scaleDenominator
                    || definition.scaleOffset != binding.scaleOffset
                    || definition.safeValueDeclared != binding.safeValueDeclared
                    || definition.safeValue != binding.safeValue) {
                    *error = QString::fromLatin1(
                                 "%1 reuses a semantic signal definition with changed "
                                 "type, direction, access, unit, scale, or safe value")
                                 .arg(path);
                    return false;
                }
            } else {
                semanticDefinitionIndexes.insert(
                    binding.semanticSignalDefinitionId, bindings.size());
            }
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

std::string fixedHexString(quint64 value, qsizetype digits)
{
    return QString::fromLatin1("0x%1").arg(value, digits, 16, QLatin1Char('0')).toStdString();
}

bool normalizedReportDevices(
    const StrictJson &reportDevices, StrictJson *expected, QString *error)
{
    const QString path = QString::fromLatin1("$.compile_report.project_device_bindings");
    if (!reportDevices.is_array() || reportDevices.empty()
        || reportDevices.size() > maximumDevices) {
        *error = path + QString::fromLatin1(" must contain a bounded nonempty array");
        return false;
    }

    *expected = StrictJson::array();
    for (std::size_t index = 0; index < reportDevices.size(); ++index) {
        const StrictJson &source = reportDevices[index];
        const QString itemPath = arrayPath(path, index);
        if (!hasFields(
                source,
                itemPath,
                {"project_device_id",
                 "adapter_id",
                 "adapter_version",
                 "adapter_sha256",
                 "esi_sha256",
                 "position",
                 "station_address",
                 "components"},
                error)
            || !source.at("components").is_array() || source.at("components").empty()
            || source.at("components").size() > maximumComponentsPerDevice) {
            if (error->isEmpty())
                *error = itemPath + QString::fromLatin1(".components is invalid");
            return false;
        }
        StrictJson target = StrictJson::object();
        for (std::string_view field :
             {"project_device_id",
              "adapter_id",
              "adapter_version",
              "adapter_sha256",
              "esi_sha256"}) {
            target[field] = source.at(field);
        }
        target["components"] = StrictJson::array();
        for (std::size_t componentIndex = 0;
             componentIndex < source.at("components").size();
             ++componentIndex) {
            const StrictJson &component = source.at("components")[componentIndex];
            const QString componentPath = arrayPath(itemPath + ".components", componentIndex);
            if (!hasExactFields(
                    component,
                    componentPath,
                    {"component_binding_id",
                     "component_instance_id",
                     "parent_component_binding_id",
                     "parent_instance_id",
                     "slot"},
                    error)) {
                return false;
            }
            quint64 componentInstanceId = 0;
            quint64 parentInstanceId = 0;
            if (!readUnsigned(
                    component.at("component_instance_id"),
                    1,
                    std::numeric_limits<quint64>::max(),
                    componentPath + ".component_instance_id",
                    &componentInstanceId,
                    error)
                || !readUnsigned(
                    component.at("parent_instance_id"),
                    0,
                    std::numeric_limits<quint64>::max(),
                    componentPath + ".parent_instance_id",
                    &parentInstanceId,
                    error)) {
                return false;
            }
            StrictJson normalized = component;
            normalized["component_instance_id"] = fixedHexString(componentInstanceId, 16);
            normalized["parent_instance_id"] = fixedHexString(parentInstanceId, 16);
            target["components"].push_back(std::move(normalized));
        }
        std::sort(
            target["components"].begin(),
            target["components"].end(),
            [](const StrictJson &left, const StrictJson &right) {
                return left.at("component_binding_id").get_ref<const std::string &>()
                       < right.at("component_binding_id").get_ref<const std::string &>();
            });
        expected->push_back(std::move(target));
    }
    std::sort(expected->begin(), expected->end(), [](const StrictJson &left, const StrictJson &right) {
        return left.at("project_device_id").get_ref<const std::string &>()
               < right.at("project_device_id").get_ref<const std::string &>();
    });
    return true;
}

bool normalizedReportActions(
    const StrictJson &reportActions, StrictJson *expected, QString *error)
{
    const QString path = QString::fromLatin1("$.compile_report.semantic_action_plans");
    if (!reportActions.is_array() || reportActions.empty()
        || reportActions.size() > maximumActions) {
        *error = path + QString::fromLatin1(" must contain a bounded nonempty array");
        return false;
    }
    *expected = reportActions;
    for (std::size_t index = 0; index < expected->size(); ++index) {
        StrictJson &action = expected->at(index);
        const QString actionPath = arrayPath(path, index);
        if (!hasFields(
                action,
                actionPath,
                {"action_binding_id",
                 "consistency_groups",
                 "required_bindings",
                 "optional_bindings",
                 "steps"},
                error)
            || !action.at("consistency_groups").is_array()
            || !action.at("required_bindings").is_array()
            || !action.at("optional_bindings").is_array() || !action.at("steps").is_array()) {
            if (error->isEmpty())
                *error = actionPath + QString::fromLatin1(" contains invalid action arrays");
            return false;
        }
        for (std::size_t groupIndex = 0;
             groupIndex < action.at("consistency_groups").size();
             ++groupIndex) {
            StrictJson &group = action["consistency_groups"][groupIndex];
            quint64 groupId = 0;
            if (!containsField(group, "consistency_group_id")
                || !readUnsigned(
                    group.at("consistency_group_id"),
                    1,
                    std::numeric_limits<quint32>::max(),
                    arrayPath(actionPath + ".consistency_groups", groupIndex)
                        + ".consistency_group_id",
                    &groupId,
                    error)) {
                return false;
            }
            group["consistency_group_id"] = fixedHexString(groupId, 8);
        }
        for (std::string_view collectionName : {"required_bindings", "optional_bindings"}) {
            StrictJson &collection = action[collectionName];
            for (std::size_t bindingIndex = 0; bindingIndex < collection.size(); ++bindingIndex) {
                StrictJson &binding = collection[bindingIndex];
                const QString bindingPath = arrayPath(
                    fieldPath(actionPath, collectionName), bindingIndex);
                quint64 resourceId = 0;
                quint64 groupId = 0;
                if (!containsField(binding, "resource_id")
                    || !containsField(binding, "consistency_group_id")
                    || !readUnsigned(
                        binding.at("resource_id"),
                        1,
                        std::numeric_limits<quint64>::max(),
                        bindingPath + ".resource_id",
                        &resourceId,
                        error)
                    || !readUnsigned(
                        binding.at("consistency_group_id"),
                        1,
                        std::numeric_limits<quint32>::max(),
                        bindingPath + ".consistency_group_id",
                        &groupId,
                        error)) {
                    return false;
                }
                binding["resource_id"] = fixedHexString(resourceId, 16);
                binding["consistency_group_id"] = fixedHexString(groupId, 8);
            }
        }
        for (std::size_t stepIndex = 0; stepIndex < action.at("steps").size(); ++stepIndex) {
            StrictJson &step = action["steps"][stepIndex];
            const QString stepPath = arrayPath(actionPath + ".steps", stepIndex);
            if (!containsField(step, "kind") || !step.at("kind").is_string())
                return false;
            if (step.at("kind").get_ref<const std::string &>() != "write_group")
                continue;
            quint64 groupId = 0;
            if (!containsField(step, "consistency_group_id")
                || !readUnsigned(
                    step.at("consistency_group_id"),
                    1,
                    std::numeric_limits<quint32>::max(),
                    stepPath + ".consistency_group_id",
                    &groupId,
                    error)) {
                return false;
            }
            step["consistency_group_id"] = fixedHexString(groupId, 8);
        }
    }
    std::sort(expected->begin(), expected->end(), [](const StrictJson &left, const StrictJson &right) {
        return left.at("action_binding_id").get_ref<const std::string &>()
               < right.at("action_binding_id").get_ref<const std::string &>();
    });
    return true;
}

const VerifiedSemanticComponent *findComponent(
    const VerifiedSemanticDevice &device, QStringView componentBindingId)
{
    const auto iterator = std::lower_bound(
        device.components.cbegin(),
        device.components.cend(),
        componentBindingId,
        [](const VerifiedSemanticComponent &component, QStringView wanted) {
            return QStringView(component.componentBindingId).compare(wanted) < 0;
        });
    return iterator != device.components.cend()
                   && QStringView(iterator->componentBindingId) == componentBindingId
               ? &*iterator
               : nullptr;
}

bool validateDevices(
    const StrictJson &artifactDevices,
    const StrictJson &reportDevices,
    quint32 declaredDeviceCount,
    VerifiedSemanticBindingArtifact *result,
    QString *error)
{
    const QString path = QString::fromLatin1("$.semantic_binding_manifest.devices");
    StrictJson expected;
    if (!normalizedReportDevices(reportDevices, &expected, error))
        return false;
    if (!artifactDevices.is_array() || artifactDevices != expected
        || artifactDevices.size() != declaredDeviceCount) {
        *error = path
                 + QString::fromLatin1(
                     " differs from the signed compile-report project-device bindings");
        return false;
    }

    QList<VerifiedSemanticDevice> devices;
    devices.reserve(qsizetype(artifactDevices.size()));
    QSet<QString> deviceIds;
    QSet<QString> componentIds;
    QSet<quint64> componentInstanceIds;
    QByteArray previousDeviceId;
    for (std::size_t index = 0; index < artifactDevices.size(); ++index) {
        const StrictJson &value = artifactDevices[index];
        const QString itemPath = arrayPath(path, index);
        if (!hasExactFields(
                value,
                itemPath,
                {"project_device_id",
                 "adapter_id",
                 "adapter_version",
                 "adapter_sha256",
                 "esi_sha256",
                 "components"},
                error)
            || !value.at("components").is_array() || value.at("components").empty()
            || value.at("components").size() > maximumComponentsPerDevice) {
            if (error->isEmpty())
                *error = itemPath + QString::fromLatin1(".components is invalid");
            return false;
        }

        VerifiedSemanticDevice device;
        if (!readSemanticSignalId(
                value.at("project_device_id"),
                itemPath + ".project_device_id",
                &device.projectDeviceId,
                error)
            || !readSemanticSignalId(
                value.at("adapter_id"), itemPath + ".adapter_id", &device.adapterId, error)
            || !readString(
                value.at("adapter_version"),
                1,
                64,
                itemPath + ".adapter_version",
                &device.adapterVersion,
                error)
            || !readSha256(
                value.at("adapter_sha256"),
                itemPath + ".adapter_sha256",
                &device.adapterSha256,
                error)
            || !readSha256(
                value.at("esi_sha256"), itemPath + ".esi_sha256", &device.esiSha256, error)) {
            return false;
        }
        const QByteArray canonicalDeviceId = device.projectDeviceId.toLatin1();
        if ((!previousDeviceId.isEmpty() && canonicalDeviceId <= previousDeviceId)
            || deviceIds.contains(device.projectDeviceId)) {
            *error = itemPath
                     + QString::fromLatin1(
                         " is duplicate or not in canonical project-device order");
            return false;
        }
        previousDeviceId = canonicalDeviceId;
        deviceIds.insert(device.projectDeviceId);

        QByteArray previousComponentId;
        for (std::size_t componentIndex = 0;
             componentIndex < value.at("components").size();
             ++componentIndex) {
            const StrictJson &componentValue = value.at("components")[componentIndex];
            const QString componentPath = arrayPath(itemPath + ".components", componentIndex);
            if (!hasExactFields(
                    componentValue,
                    componentPath,
                    {"component_binding_id",
                     "component_instance_id",
                     "parent_component_binding_id",
                     "parent_instance_id",
                     "slot"},
                    error)) {
                return false;
            }
            VerifiedSemanticComponent component;
            quint64 slot = 0;
            if (!readSemanticSignalId(
                    componentValue.at("component_binding_id"),
                    componentPath + ".component_binding_id",
                    &component.componentBindingId,
                    error)
                || !readFixedHex(
                    componentValue.at("component_instance_id"),
                    16,
                    componentPath + ".component_instance_id",
                    &component.componentInstanceId,
                    error)
                || !readFixedHex(
                    componentValue.at("parent_instance_id"),
                    16,
                    componentPath + ".parent_instance_id",
                    &component.parentInstanceId,
                    error)
                || !readUnsigned(
                    componentValue.at("slot"),
                    0,
                    std::numeric_limits<quint32>::max(),
                    componentPath + ".slot",
                    &slot,
                    error)) {
                return false;
            }
            if (componentValue.at("parent_component_binding_id").is_null()) {
                component.parentComponentBindingId.reset();
            } else {
                QString parentId;
                if (!readSemanticSignalId(
                        componentValue.at("parent_component_binding_id"),
                        componentPath + ".parent_component_binding_id",
                        &parentId,
                        error)) {
                    return false;
                }
                component.parentComponentBindingId = std::move(parentId);
            }
            component.slot = quint32(slot);
            const QByteArray canonicalComponentId = component.componentBindingId.toLatin1();
            if (!component.componentInstanceId
                || (!previousComponentId.isEmpty()
                    && canonicalComponentId <= previousComponentId)
                || componentIds.contains(component.componentBindingId)
                || componentInstanceIds.contains(component.componentInstanceId)
                || component.parentComponentBindingId.has_value()
                       != bool(component.parentInstanceId)
                || (component.parentComponentBindingId
                    && *component.parentComponentBindingId == component.componentBindingId)) {
                *error = componentPath
                         + QString::fromLatin1(
                             " has duplicate, unordered, or inconsistent component identity");
                return false;
            }
            previousComponentId = canonicalComponentId;
            componentIds.insert(component.componentBindingId);
            componentInstanceIds.insert(component.componentInstanceId);
            device.components.append(std::move(component));
        }
        if (!isAcyclicSemanticComponentParentGraph(device.components)) {
            *error = itemPath
                     + QString::fromLatin1(
                         ".components contains an unknown, mismatched, or cyclic parent graph");
            return false;
        }
        devices.append(std::move(device));
    }

    for (std::size_t index = 0; index < reportDevices.size(); ++index) {
        const StrictJson &reportDevice = reportDevices[index];
        QString projectDeviceId;
        quint64 position = 0;
        quint64 stationAddress = 0;
        const QString reportPath
            = arrayPath(QString::fromLatin1("$.compile_report.project_device_bindings"), index);
        if (!readSemanticSignalId(
                reportDevice.at("project_device_id"),
                reportPath + ".project_device_id",
                &projectDeviceId,
                error)
            || !readUnsigned(
                reportDevice.at("position"),
                0,
                std::numeric_limits<quint16>::max(),
                reportPath + ".position",
                &position,
                error)
            || !readUnsigned(
                reportDevice.at("station_address"),
                1,
                std::numeric_limits<quint16>::max(),
                reportPath + ".station_address",
                &stationAddress,
                error)) {
            return false;
        }
        const auto deviceIterator = std::lower_bound(
            devices.begin(),
            devices.end(),
            QStringView(projectDeviceId),
            [](const VerifiedSemanticDevice &device, QStringView wanted) {
                return QStringView(device.projectDeviceId).compare(wanted) < 0;
            });
        const SemanticBindingTopologyInstance *topology
            = findTopologyInstance(result->topologyInstances, quint16(position));
        if (deviceIterator == devices.end()
            || deviceIterator->projectDeviceId != projectDeviceId || !topology
            || topology->stationAddress != stationAddress
            || topology->adapterId != deviceIterator->adapterId
            || topology->adapterVersion != deviceIterator->adapterVersion
            || topology->adapterSha256 != deviceIterator->adapterSha256
            || topology->esiSha256 != deviceIterator->esiSha256) {
            *error = path
                     + QString::fromLatin1(
                         " differs from the exact signed topology provenance");
            return false;
        }
        deviceIterator->position = quint16(position);
        deviceIterator->stationAddress = quint16(stationAddress);
    }

    result->devices = std::move(devices);
    for (const VerifiedSemanticBinding &binding : std::as_const(result->bindings)) {
        const auto deviceIterator = std::lower_bound(
            result->devices.cbegin(),
            result->devices.cend(),
            QStringView(binding.projectDeviceId),
            [](const VerifiedSemanticDevice &device, QStringView wanted) {
                return QStringView(device.projectDeviceId).compare(wanted) < 0;
            });
        if (deviceIterator == result->devices.cend()
            || deviceIterator->projectDeviceId != binding.projectDeviceId) {
            *error = path + QString::fromLatin1(" does not own every semantic binding");
            return false;
        }
        const VerifiedSemanticComponent *component
            = findComponent(*deviceIterator, binding.componentBindingId);
        if (!component || component->componentInstanceId != binding.componentInstanceId
            || component->slot != binding.slot
            || deviceIterator->position != binding.position
            || deviceIterator->stationAddress != binding.stationAddress
            || deviceIterator->adapterId != binding.adapterId
            || deviceIterator->adapterVersion != binding.adapterVersion
            || deviceIterator->adapterSha256 != binding.adapterSha256
            || deviceIterator->esiSha256 != binding.esiSha256) {
            *error = path
                     + QString::fromLatin1(
                         " has a component or device identity inconsistent with a binding");
            return false;
        }
    }
    return true;
}

const VerifiedSemanticBinding *findBinding(
    const VerifiedSemanticBindingArtifact &artifact, QStringView semanticBindingId)
{
    const auto iterator = std::lower_bound(
        artifact.bindings.cbegin(),
        artifact.bindings.cend(),
        semanticBindingId,
        [](const VerifiedSemanticBinding &binding, QStringView wanted) {
            return QStringView(binding.semanticSignalId).compare(wanted) < 0;
        });
    return iterator != artifact.bindings.cend()
                   && QStringView(iterator->semanticSignalId) == semanticBindingId
               ? &*iterator
               : nullptr;
}

bool parseActionBindingReference(
    const StrictJson &value,
    const QString &path,
    const VerifiedSemanticBindingArtifact &artifact,
    VerifiedSemanticActionBindingReference *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"semantic_signal_definition_id",
             "semantic_binding_id",
             "resource_id",
             "component_binding_id",
             "consistency_group_id",
             "primitive",
             "bit_width",
             "direction",
             "access",
             "unit",
             "scale"},
            error)) {
        return false;
    }
    quint64 bitWidth = 0;
    quint64 groupId = 0;
    if (!readSemanticSignalId(
            value.at("semantic_signal_definition_id"),
            path + ".semantic_signal_definition_id",
            &result->semanticSignalDefinitionId,
            error)
        || !readSemanticSignalId(
            value.at("semantic_binding_id"),
            path + ".semantic_binding_id",
            &result->semanticBindingId,
            error)
        || !readFixedHex(
            value.at("resource_id"), 16, path + ".resource_id", &result->resourceId, error)
        || !readSemanticSignalId(
            value.at("component_binding_id"),
            path + ".component_binding_id",
            &result->componentBindingId,
            error)
        || !readFixedHex(
            value.at("consistency_group_id"),
            8,
            path + ".consistency_group_id",
            &groupId,
            error)
        || !readPrimitive(value.at("primitive"), path + ".primitive", &result->primitive, error)
        || !readUnsigned(
            value.at("bit_width"), 1, 128, path + ".bit_width", &bitWidth, error)
        || !readDirection(value.at("direction"), path + ".direction", &result->direction, error)
        || !readAccess(value.at("access"), path + ".access", &result->access, error)
        || !readOptionalString(value.at("unit"), 64, path + ".unit", &result->unit, error)
        || !readScale(
            value.at("scale"),
            path + ".scale",
            &result->scaleNumerator,
            &result->scaleDenominator,
            &result->scaleOffset,
            error)) {
        return false;
    }
    result->consistencyGroupId = quint32(groupId);
    result->bitWidth = quint16(bitWidth);
    const VerifiedSemanticBinding *binding = findBinding(artifact, result->semanticBindingId);
    if (!result->resourceId || !result->consistencyGroupId || !binding
        || binding->semanticSignalDefinitionId != result->semanticSignalDefinitionId
        || binding->resourceId != result->resourceId
        || binding->componentBindingId != result->componentBindingId
        || binding->consistencyGroupId != result->consistencyGroupId
        || binding->primitive != result->primitive || binding->bitWidth != result->bitWidth
        || binding->direction != result->direction || binding->access != result->access
        || binding->unit != result->unit || binding->scaleNumerator != result->scaleNumerator
        || binding->scaleDenominator != result->scaleDenominator
        || binding->scaleOffset != result->scaleOffset) {
        *error = path
                 + QString::fromLatin1(
                     " differs from the exact signed project binding descriptor");
        return false;
    }
    return true;
}

bool validateActionBindingCollection(
    const StrictJson &collection,
    const QString &path,
    bool required,
    const VerifiedSemanticBindingArtifact &artifact,
    QList<VerifiedSemanticActionBindingReference> *result,
    QString *error)
{
    if (!collection.is_array() || (required && collection.empty())
        || collection.size() > maximumActionBindings) {
        *error = path + QString::fromLatin1(" has an invalid binding count");
        return false;
    }
    QByteArray previousId;
    QSet<QString> ids;
    for (std::size_t index = 0; index < collection.size(); ++index) {
        VerifiedSemanticActionBindingReference binding;
        const QString itemPath = arrayPath(path, index);
        if (!parseActionBindingReference(collection[index], itemPath, artifact, &binding, error))
            return false;
        const QByteArray canonicalId = binding.semanticBindingId.toLatin1();
        if ((!previousId.isEmpty() && canonicalId <= previousId)
            || ids.contains(binding.semanticBindingId)) {
            *error = itemPath
                     + QString::fromLatin1(
                         " is duplicate or not in canonical SemanticBindingId order");
            return false;
        }
        previousId = canonicalId;
        ids.insert(binding.semanticBindingId);
        result->append(std::move(binding));
    }
    return true;
}

const EcfgOutputGroupPolicy *findOutputPolicy(
    const EcfgConfiguration &configuration, quint32 groupId)
{
    const auto iterator = std::lower_bound(
        configuration.outputPolicies.cbegin(),
        configuration.outputPolicies.cend(),
        groupId,
        [](const EcfgOutputGroupPolicy &policy, quint32 wanted) {
            return policy.consistencyGroupId < wanted;
        });
    return iterator != configuration.outputPolicies.cend()
                   && iterator->consistencyGroupId == groupId
               ? &*iterator
               : nullptr;
}

bool validateActionParameters(
    const StrictJson &parameters,
    const QString &path,
    QList<VerifiedSemanticActionParameter> *result,
    QString *error)
{
    if (!parameters.is_array() || parameters.size() > maximumActionParameters) {
        *error = path + QString::fromLatin1(" has an invalid parameter count");
        return false;
    }
    QSet<QString> parameterIds;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const StrictJson &value = parameters[index];
        const QString itemPath = arrayPath(path, index);
        if (!hasExactFields(
                value,
                itemPath,
                {"parameter_id", "primitive", "unit", "minimum", "maximum"},
                error)) {
            return false;
        }
        VerifiedSemanticActionParameter parameter;
        if (!readSemanticSignalId(
                value.at("parameter_id"),
                itemPath + ".parameter_id",
                &parameter.parameterId,
                error)
            || !readPrimitive(
                value.at("primitive"), itemPath + ".primitive", &parameter.primitive, error)
            || !readOptionalString(
                value.at("unit"), 64, itemPath + ".unit", &parameter.unit, error)
            || !readSignedInteger(
                value.at("minimum"), itemPath + ".minimum", &parameter.minimum, error)
            || !readSignedInteger(
                value.at("maximum"), itemPath + ".maximum", &parameter.maximum, error)
            || parameter.minimum > parameter.maximum
            || parameterIds.contains(parameter.parameterId)) {
            if (error->isEmpty())
                *error = itemPath + QString::fromLatin1(" is duplicate or has invalid bounds");
            return false;
        }
        parameterIds.insert(parameter.parameterId);
        result->append(std::move(parameter));
    }
    return true;
}

bool validateActionGroups(
    const StrictJson &groups,
    const QString &path,
    const EcfgConfiguration &configuration,
    QList<VerifiedSemanticActionGroup> *result,
    QString *error)
{
    if (!groups.is_array() || groups.empty() || groups.size() > maximumActionGroups) {
        *error = path + QString::fromLatin1(" has an invalid consistency-group count");
        return false;
    }
    QSet<quint32> ids;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const StrictJson &value = groups[index];
        const QString itemPath = arrayPath(path, index);
        if (!hasExactFields(
                value,
                itemPath,
                {"consistency_group_id", "recovery_policy", "max_ttl_cycles"},
                error)) {
            return false;
        }
        VerifiedSemanticActionGroup group;
        quint64 groupId = 0;
        quint64 maximumTtl = 0;
        if (!readFixedHex(
                value.at("consistency_group_id"),
                8,
                itemPath + ".consistency_group_id",
                &groupId,
                error)
            || !readUnsigned(
                value.at("max_ttl_cycles"),
                1,
                std::numeric_limits<quint32>::max(),
                itemPath + ".max_ttl_cycles",
                &maximumTtl,
                error)
            || !value.at("recovery_policy").is_string()) {
            return false;
        }
        const std::string &recovery = value.at("recovery_policy").get_ref<const std::string &>();
        if (recovery == "hold_safe")
            group.recoveryPolicy = EcfgOutputRecoveryPolicy::HoldSafe;
        else if (recovery == "task_output")
            group.recoveryPolicy = EcfgOutputRecoveryPolicy::ReturnTask;
        else {
            *error = itemPath + QString::fromLatin1(".recovery_policy is unsupported");
            return false;
        }
        group.consistencyGroupId = quint32(groupId);
        group.maximumTtlCycles = quint32(maximumTtl);
        const EcfgOutputGroupPolicy *policy
            = findOutputPolicy(configuration, group.consistencyGroupId);
        if (!group.consistencyGroupId || ids.contains(group.consistencyGroupId) || !policy
            || policy->recoveryPolicy != group.recoveryPolicy
            || policy->maximumTtlCycles != group.maximumTtlCycles) {
            *error = itemPath
                     + QString::fromLatin1(
                         " differs from the signed ECFG output-group policy");
            return false;
        }
        ids.insert(group.consistencyGroupId);
        result->append(std::move(group));
    }
    return true;
}

const VerifiedSemanticActionParameter *findParameter(
    const VerifiedSemanticAction &action, QStringView parameterId)
{
    const auto iterator = std::find_if(
        action.parameters.cbegin(),
        action.parameters.cend(),
        [parameterId](const VerifiedSemanticActionParameter &parameter) {
            return QStringView(parameter.parameterId) == parameterId;
        });
    return iterator == action.parameters.cend() ? nullptr : &*iterator;
}

bool parseActionAssignment(
    const StrictJson &value,
    const QString &path,
    const VerifiedSemanticBindingArtifact &artifact,
    const VerifiedSemanticAction &action,
    VerifiedSemanticActionAssignment *result,
    QString *error)
{
    if (!hasExactFields(value, path, {"semantic_binding_id", "value_source"}, error)
        || !readSemanticSignalId(
            value.at("semantic_binding_id"),
            path + ".semantic_binding_id",
            &result->semanticBindingId,
            error)) {
        return false;
    }
    const VerifiedSemanticBinding *binding = findBinding(artifact, result->semanticBindingId);
    if (!binding || binding->direction != EcfgResourceDirection::Output
        || binding->access != EcfgResourceAccess::ReadWrite || !binding->safeValueDeclared) {
        *error = path + QString::fromLatin1(" does not target a writable safe output");
        return false;
    }
    const StrictJson &source = value.at("value_source");
    if (!hasFields(source, path + ".value_source", {"kind"}, error)
        || !source.at("kind").is_string()) {
        return false;
    }
    const std::string &kind = source.at("kind").get_ref<const std::string &>();
    if (kind == "constant") {
        if (!hasExactFields(source, path + ".value_source", {"kind", "value"}, error))
            return false;
        std::variant<qint64, quint64> constant;
        if (!readIntegerValue(
                source.at("value"), path + ".value_source.value", &constant, error)
            || !integerFits(constant, binding->primitive, binding->bitWidth)) {
            if (error->isEmpty())
                *error = path + QString::fromLatin1(".value_source.value is out of range");
            return false;
        }
        result->constantValue = std::move(constant);
    } else if (kind == "parameter") {
        if (!hasExactFields(source, path + ".value_source", {"kind", "parameter_id"}, error)) {
            return false;
        }
        QString parameterId;
        if (!readSemanticSignalId(
                source.at("parameter_id"),
                path + ".value_source.parameter_id",
                &parameterId,
                error)) {
            return false;
        }
        const VerifiedSemanticActionParameter *parameter = findParameter(action, parameterId);
        const std::variant<qint64, quint64> minimumValue
            = parameter && parameter->minimum < 0
                  ? std::variant<qint64, quint64>{parameter->minimum}
                  : std::variant<qint64, quint64>{
                      quint64(parameter ? parameter->minimum : 0)};
        const std::variant<qint64, quint64> maximumValue
            = parameter && parameter->maximum < 0
                  ? std::variant<qint64, quint64>{parameter->maximum}
                  : std::variant<qint64, quint64>{
                      quint64(parameter ? parameter->maximum : 0)};
        if (!parameter || parameter->primitive != binding->primitive
            || parameter->unit != binding->unit
            || !integerFits(minimumValue, binding->primitive, binding->bitWidth)
            || !integerFits(maximumValue, binding->primitive, binding->bitWidth)) {
            *error = path
                     + QString::fromLatin1(
                         " references a missing or type-incompatible parameter");
            return false;
        }
        result->parameterId = std::move(parameterId);
    } else {
        *error = path + QString::fromLatin1(".value_source.kind is unsupported");
        return false;
    }
    return true;
}

bool validateActionSteps(
    const StrictJson &steps,
    const QString &path,
    const EcfgConfiguration &configuration,
    const VerifiedSemanticBindingArtifact &artifact,
    VerifiedSemanticAction *action,
    QString *error)
{
    if (!steps.is_array() || steps.empty() || steps.size() > maximumActionSteps) {
        *error = path + QString::fromLatin1(" has an invalid step count");
        return false;
    }
    QSet<quint32> declaredGroups;
    for (const VerifiedSemanticActionGroup &group : std::as_const(action->consistencyGroups))
        declaredGroups.insert(group.consistencyGroupId);
    QSet<quint32> usedGroups;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const StrictJson &value = steps[index];
        const QString itemPath = arrayPath(path, index);
        if (!hasFields(value, itemPath, {"kind"}, error) || !value.at("kind").is_string())
            return false;
        const std::string &kind = value.at("kind").get_ref<const std::string &>();
        VerifiedSemanticActionStep step;
        if (kind == "write_group") {
            if (!hasExactFields(
                    value,
                    itemPath,
                    {"kind", "consistency_group_id", "assignments"},
                    error)
                || !value.at("assignments").is_array() || value.at("assignments").empty()
                || value.at("assignments").size() > maximumActionAssignments) {
                if (error->isEmpty())
                    *error = itemPath + QString::fromLatin1(".assignments is invalid");
                return false;
            }
            quint64 groupId = 0;
            if (!readFixedHex(
                    value.at("consistency_group_id"),
                    8,
                    itemPath + ".consistency_group_id",
                    &groupId,
                    error)) {
                return false;
            }
            step.kind = VerifiedSemanticActionStepKind::WriteGroup;
            step.consistencyGroupId = quint32(groupId);
            const EcfgOutputGroupPolicy *policy
                = findOutputPolicy(configuration, step.consistencyGroupId);
            if (!declaredGroups.contains(step.consistencyGroupId) || !policy) {
                *error = itemPath
                         + QString::fromLatin1(
                             " references an undeclared output consistency group");
                return false;
            }
            usedGroups.insert(step.consistencyGroupId);
            QSet<QString> assignmentIds;
            QSet<quint64> assignedResources;
            QByteArray previousId;
            for (std::size_t assignmentIndex = 0;
                 assignmentIndex < value.at("assignments").size();
                 ++assignmentIndex) {
                VerifiedSemanticActionAssignment assignment;
                const QString assignmentPath
                    = arrayPath(itemPath + ".assignments", assignmentIndex);
                if (!parseActionAssignment(
                        value.at("assignments")[assignmentIndex],
                        assignmentPath,
                        artifact,
                        *action,
                        &assignment,
                        error)) {
                    return false;
                }
                const QByteArray canonicalId = assignment.semanticBindingId.toLatin1();
                const VerifiedSemanticBinding *binding
                    = findBinding(artifact, assignment.semanticBindingId);
                const bool isRequired = std::any_of(
                    action->requiredBindings.cbegin(),
                    action->requiredBindings.cend(),
                    [&assignment](const VerifiedSemanticActionBindingReference &reference) {
                        return reference.semanticBindingId == assignment.semanticBindingId;
                    });
                if ((!previousId.isEmpty() && canonicalId <= previousId)
                    || assignmentIds.contains(assignment.semanticBindingId) || !binding
                    || !isRequired
                    || binding->consistencyGroupId != step.consistencyGroupId) {
                    *error = assignmentPath
                             + QString::fromLatin1(
                                 " is duplicate, unordered, or outside the complete group");
                    return false;
                }
                previousId = canonicalId;
                assignmentIds.insert(assignment.semanticBindingId);
                assignedResources.insert(binding->resourceId);
                step.assignments.append(std::move(assignment));
            }
            QSet<quint64> policyResources(
                policy->resourceIds.cbegin(), policy->resourceIds.cend());
            if (assignedResources != policyResources
                || assignedResources.size() != policy->resourceCount) {
                *error = itemPath
                         + QString::fromLatin1(
                             " does not assign the complete signed output group");
                return false;
            }
        } else if (kind == "wait_masked") {
            if (!hasExactFields(
                    value,
                    itemPath,
                    {"kind", "semantic_binding_id", "mask", "value", "timeout_cycles"},
                    error)) {
                return false;
            }
            quint64 timeoutCycles = 0;
            if (!readSemanticSignalId(
                    value.at("semantic_binding_id"),
                    itemPath + ".semantic_binding_id",
                    &step.semanticBindingId,
                    error)
                || !readUnsigned(
                    value.at("mask"),
                    0,
                    std::numeric_limits<quint64>::max(),
                    itemPath + ".mask",
                    &step.mask,
                    error)
                || !readUnsigned(
                    value.at("value"),
                    0,
                    std::numeric_limits<quint64>::max(),
                    itemPath + ".value",
                    &step.value,
                    error)
                || !readUnsigned(
                    value.at("timeout_cycles"),
                    1,
                    std::numeric_limits<quint32>::max(),
                    itemPath + ".timeout_cycles",
                    &timeoutCycles,
                    error)) {
                return false;
            }
            step.kind = VerifiedSemanticActionStepKind::WaitMasked;
            step.timeoutCycles = quint32(timeoutCycles);
            const VerifiedSemanticBinding *binding
                = findBinding(artifact, step.semanticBindingId);
            const bool actionKnowsBinding = std::any_of(
                    action->requiredBindings.cbegin(),
                    action->requiredBindings.cend(),
                    [&step](const VerifiedSemanticActionBindingReference &reference) {
                        return reference.semanticBindingId == step.semanticBindingId;
                    })
                || std::any_of(
                    action->optionalBindings.cbegin(),
                    action->optionalBindings.cend(),
                    [&step](const VerifiedSemanticActionBindingReference &reference) {
                        return reference.semanticBindingId == step.semanticBindingId;
                    });
            if (!binding || !actionKnowsBinding
                || binding->direction != EcfgResourceDirection::Input
                || binding->access != EcfgResourceAccess::Read || binding->bitWidth > 64
                || !isValidSemanticMaskedWaitCondition(
                    step.mask, step.value, binding->bitWidth)) {
                *error = itemPath
                         + QString::fromLatin1(
                             " has an invalid or unbound masked wait condition");
                return false;
            }
        } else if (kind == "wait_absolute_limit") {
            if (!hasExactFields(
                    value,
                    itemPath,
                    {"kind", "semantic_binding_id", "limit", "timeout_cycles"},
                    error)) {
                return false;
            }
            quint64 timeoutCycles = 0;
            if (!readSemanticSignalId(
                    value.at("semantic_binding_id"),
                    itemPath + ".semantic_binding_id",
                    &step.semanticBindingId,
                    error)
                || !readUnsigned(
                    value.at("limit"),
                    0,
                    std::numeric_limits<quint64>::max(),
                    itemPath + ".limit",
                    &step.absoluteLimit,
                    error)
                || !readUnsigned(
                    value.at("timeout_cycles"),
                    1,
                    std::numeric_limits<quint32>::max(),
                    itemPath + ".timeout_cycles",
                    &timeoutCycles,
                    error)) {
                return false;
            }
            step.kind = VerifiedSemanticActionStepKind::WaitAbsoluteLimit;
            step.timeoutCycles = quint32(timeoutCycles);
            const VerifiedSemanticBinding *binding
                = findBinding(artifact, step.semanticBindingId);
            const bool actionKnowsBinding = std::any_of(
                    action->requiredBindings.cbegin(),
                    action->requiredBindings.cend(),
                    [&step](const VerifiedSemanticActionBindingReference &reference) {
                        return reference.semanticBindingId == step.semanticBindingId;
                    })
                || std::any_of(
                    action->optionalBindings.cbegin(),
                    action->optionalBindings.cend(),
                    [&step](const VerifiedSemanticActionBindingReference &reference) {
                        return reference.semanticBindingId == step.semanticBindingId;
                    });
            if (!binding || !actionKnowsBinding
                || binding->direction != EcfgResourceDirection::Input
                || binding->access != EcfgResourceAccess::Read
                || !primitiveIsSigned(binding->primitive) || binding->bitWidth > 64) {
                *error = itemPath
                         + QString::fromLatin1(
                             " has an invalid or unbound absolute-limit wait condition");
                return false;
            }
        } else {
            *error = itemPath + QString::fromLatin1(".kind is unsupported");
            return false;
        }
        action->steps.append(std::move(step));
    }
    if (usedGroups != declaredGroups) {
        *error = path
                 + QString::fromLatin1(
                     " does not use every declared consistency group in a write step");
        return false;
    }
    return true;
}

bool validateActions(
    const StrictJson &artifactActions,
    const StrictJson &reportActions,
    quint32 declaredActionCount,
    const EcfgConfiguration &configuration,
    VerifiedSemanticBindingArtifact *result,
    QString *error)
{
    const QString path = QString::fromLatin1("$.semantic_binding_manifest.actions");
    StrictJson expected;
    if (!normalizedReportActions(reportActions, &expected, error))
        return false;
    if (!artifactActions.is_array() || artifactActions != expected
        || artifactActions.size() != declaredActionCount) {
        *error = path
                 + QString::fromLatin1(
                     " differs from the signed compile-report semantic action plans");
        return false;
    }

    QSet<QString> actionBindingIds;
    QHash<QString, QByteArray> definitionDigests;
    QByteArray previousActionBindingId;
    QList<VerifiedSemanticAction> actions;
    actions.reserve(qsizetype(artifactActions.size()));
    for (std::size_t index = 0; index < artifactActions.size(); ++index) {
        const StrictJson &value = artifactActions[index];
        const QString itemPath = arrayPath(path, index);
        if (!hasExactFields(
                value,
                itemPath,
                {"action_definition_id",
                 "action_definition_sha256",
                 "action_binding_id",
                 "project_device_id",
                 "component_binding_ids",
                 "adapter_action_key",
                 "enabled",
                 "qualification",
                 "disabled_reason",
                 "dc_required",
                 "adapter_id",
                 "adapter_version",
                 "adapter_sha256",
                 "esi_sha256",
                 "required_bindings",
                 "optional_bindings",
                 "parameters",
                 "consistency_groups",
                 "steps"},
                error)) {
            return false;
        }
        VerifiedSemanticAction action;
        bool enabled = false;
        bool dcRequired = false;
        if (!readSemanticSignalId(
                value.at("action_definition_id"),
                itemPath + ".action_definition_id",
                &action.actionDefinitionId,
                error)
            || !readSha256(
                value.at("action_definition_sha256"),
                itemPath + ".action_definition_sha256",
                &action.actionDefinitionSha256,
                error)
            || !readSemanticSignalId(
                value.at("action_binding_id"),
                itemPath + ".action_binding_id",
                &action.actionBindingId,
                error)
            || !readSemanticSignalId(
                value.at("project_device_id"),
                itemPath + ".project_device_id",
                &action.projectDeviceId,
                error)
            || !readSemanticSignalId(
                value.at("adapter_action_key"),
                itemPath + ".adapter_action_key",
                &action.adapterActionKey,
                error)
            || !readBoolean(value.at("enabled"), itemPath + ".enabled", &enabled, error)
            || !readBoolean(
                value.at("dc_required"), itemPath + ".dc_required", &dcRequired, error)
            || !readSemanticSignalId(
                value.at("adapter_id"), itemPath + ".adapter_id", &action.adapterId, error)
            || !readString(
                value.at("adapter_version"),
                1,
                64,
                itemPath + ".adapter_version",
                &action.adapterVersion,
                error)
            || !readSha256(
                value.at("adapter_sha256"),
                itemPath + ".adapter_sha256",
                &action.adapterSha256,
                error)
            || !readSha256(
                value.at("esi_sha256"), itemPath + ".esi_sha256", &action.esiSha256, error)) {
            return false;
        }
        action.enabled = enabled;
        action.dcRequired = dcRequired;
        if (!value.at("qualification").is_string()) {
            *error = itemPath + QString::fromLatin1(".qualification is invalid");
            return false;
        }
        const std::string &qualification
            = value.at("qualification").get_ref<const std::string &>();
        if (qualification == "qualified")
            action.qualification = VerifiedSemanticActionQualification::Qualified;
        else if (qualification == "unqualified")
            action.qualification = VerifiedSemanticActionQualification::Unqualified;
        else {
            *error = itemPath + QString::fromLatin1(".qualification is unsupported");
            return false;
        }
        if (value.at("disabled_reason").is_null()) {
            action.disabledReason.reset();
        } else {
            QString reason;
            if (!readString(
                    value.at("disabled_reason"),
                    1,
                    256,
                    itemPath + ".disabled_reason",
                    &reason,
                    error)) {
                return false;
            }
            action.disabledReason = std::move(reason);
        }
        if (action.enabled
                != (action.qualification == VerifiedSemanticActionQualification::Qualified)
            || action.disabledReason.has_value() == action.enabled) {
            *error = itemPath
                     + QString::fromLatin1(
                         " has an inconsistent enabled, qualification, or disabled reason state");
            return false;
        }

        if (!value.at("component_binding_ids").is_array()
            || value.at("component_binding_ids").empty()
            || value.at("component_binding_ids").size() > maximumComponentsPerDevice) {
            *error = itemPath + QString::fromLatin1(".component_binding_ids is invalid");
            return false;
        }
        QSet<QString> componentIds;
        QByteArray previousComponentId;
        for (std::size_t componentIndex = 0;
             componentIndex < value.at("component_binding_ids").size();
             ++componentIndex) {
            QString componentId;
            const QString componentPath
                = arrayPath(itemPath + ".component_binding_ids", componentIndex);
            if (!readSemanticSignalId(
                    value.at("component_binding_ids")[componentIndex],
                    componentPath,
                    &componentId,
                    error)) {
                return false;
            }
            const QByteArray canonicalId = componentId.toLatin1();
            if ((!previousComponentId.isEmpty() && canonicalId <= previousComponentId)
                || componentIds.contains(componentId)) {
                *error = componentPath
                         + QString::fromLatin1(
                             " is duplicate or not in canonical component order");
                return false;
            }
            previousComponentId = canonicalId;
            componentIds.insert(componentId);
            action.componentBindingIds.append(std::move(componentId));
        }

        const VerifiedSemanticDevice *device = nullptr;
        const auto deviceIterator = std::lower_bound(
            result->devices.cbegin(),
            result->devices.cend(),
            QStringView(action.projectDeviceId),
            [](const VerifiedSemanticDevice &candidate, QStringView wanted) {
                return QStringView(candidate.projectDeviceId).compare(wanted) < 0;
            });
        if (deviceIterator != result->devices.cend()
            && deviceIterator->projectDeviceId == action.projectDeviceId) {
            device = &*deviceIterator;
        }
        if (!device || device->adapterId != action.adapterId
            || device->adapterVersion != action.adapterVersion
            || device->adapterSha256 != action.adapterSha256
            || device->esiSha256 != action.esiSha256
            || std::any_of(
                action.componentBindingIds.cbegin(),
                action.componentBindingIds.cend(),
                [device](const QString &componentId) {
                    return !findComponent(*device, componentId);
                })) {
            *error = itemPath
                     + QString::fromLatin1(
                         " differs from its signed project-device/component binding");
            return false;
        }

        if (!validateActionBindingCollection(
                value.at("required_bindings"),
                itemPath + ".required_bindings",
                true,
                *result,
                &action.requiredBindings,
                error)
            || !validateActionBindingCollection(
                value.at("optional_bindings"),
                itemPath + ".optional_bindings",
                false,
                *result,
                &action.optionalBindings,
                error)
            || !validateActionParameters(
                value.at("parameters"), itemPath + ".parameters", &action.parameters, error)
            || !validateActionGroups(
                value.at("consistency_groups"),
                itemPath + ".consistency_groups",
                configuration,
                &action.consistencyGroups,
                error)) {
            return false;
        }

        QSet<QString> referencedBindingIds;
        for (const VerifiedSemanticActionBindingReference &reference :
             std::as_const(action.requiredBindings)) {
            referencedBindingIds.insert(reference.semanticBindingId);
            const VerifiedSemanticBinding *binding
                = findBinding(*result, reference.semanticBindingId);
            if (!binding || binding->projectDeviceId != action.projectDeviceId
                || !componentIds.contains(binding->componentBindingId)) {
                *error = itemPath
                         + QString::fromLatin1(
                             ".required_bindings crosses a project-device boundary");
                return false;
            }
        }
        for (const VerifiedSemanticActionBindingReference &reference :
             std::as_const(action.optionalBindings)) {
            const VerifiedSemanticBinding *binding
                = findBinding(*result, reference.semanticBindingId);
            if (referencedBindingIds.contains(reference.semanticBindingId) || !binding
                || binding->projectDeviceId != action.projectDeviceId
                || !componentIds.contains(binding->componentBindingId)) {
                *error = itemPath
                         + QString::fromLatin1(
                             ".optional_bindings is duplicate or crosses a device boundary");
                return false;
            }
            referencedBindingIds.insert(reference.semanticBindingId);
        }

        if (!validateActionSteps(
                value.at("steps"),
                itemPath + ".steps",
                configuration,
                *result,
                &action,
                error)) {
            return false;
        }
        if (action.dcRequired) {
            if (!configuration.cyclePeriodNs || configuration.dcRecords.isEmpty()) {
                *error = itemPath
                         + QString::fromLatin1(
                             " requires DC but the signed ECFG has no DC runtime");
                return false;
            }
            const bool allWritableBindingsUseDc = std::all_of(
                action.requiredBindings.cbegin(),
                action.requiredBindings.cend(),
                [result, &configuration](
                    const VerifiedSemanticActionBindingReference &reference) {
                    if (reference.direction != EcfgResourceDirection::Output)
                        return true;
                    const VerifiedSemanticBinding *binding
                        = findBinding(*result, reference.semanticBindingId);
                    const SemanticBindingTopologyInstance *topology
                        = binding
                              ? findTopologyInstance(
                                  result->topologyInstances, binding->position)
                              : nullptr;
                    const auto dcRecord = binding
                                              ? std::find_if(
                                                  configuration.dcRecords.cbegin(),
                                                  configuration.dcRecords.cend(),
                                                  [binding](
                                                      const EcfgDcRecord &record) {
                                                      return record.stationAddress
                                                             == binding->stationAddress;
                                                  })
                                              : configuration.dcRecords.cend();
                    return topology && topology->dcProfile.has_value()
                           && dcRecord != configuration.dcRecords.cend()
                           && dcRecord->assignActivate
                           && dcRecord->sync0CycleNs == configuration.cyclePeriodNs;
                });
            if (!allWritableBindingsUseDc) {
                *error = itemPath
                         + QString::fromLatin1(
                             " requires DC but references a non-DC output component");
                return false;
            }
        }

        const QByteArray canonicalActionId = action.actionBindingId.toLatin1();
        const auto previousDefinition = definitionDigests.constFind(action.actionDefinitionId);
        if ((!previousActionBindingId.isEmpty()
             && canonicalActionId <= previousActionBindingId)
            || actionBindingIds.contains(action.actionBindingId)
            || (previousDefinition != definitionDigests.cend()
                && *previousDefinition != action.actionDefinitionSha256)) {
            *error = itemPath
                     + QString::fromLatin1(
                         " is duplicate, unordered, or reuses a changed action definition");
            return false;
        }
        previousActionBindingId = canonicalActionId;
        actionBindingIds.insert(action.actionBindingId);
        definitionDigests.insert(action.actionDefinitionId, action.actionDefinitionSha256);
        actions.append(std::move(action));
    }
    result->actions = std::move(actions);
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

const VerifiedSemanticDevice *VerifiedSemanticBindingArtifact::findDevice(
    QStringView projectDeviceId) const
{
    const auto iterator = std::lower_bound(
        devices.cbegin(),
        devices.cend(),
        projectDeviceId,
        [](const VerifiedSemanticDevice &device, QStringView wanted) {
            return QStringView(device.projectDeviceId).compare(wanted) < 0;
        });
    return iterator != devices.cend() && QStringView(iterator->projectDeviceId) == projectDeviceId
               ? &*iterator
               : nullptr;
}

const VerifiedSemanticAction *VerifiedSemanticBindingArtifact::findAction(
    QStringView actionBindingId) const
{
    const auto iterator = std::lower_bound(
        actions.cbegin(),
        actions.cend(),
        actionBindingId,
        [](const VerifiedSemanticAction &action, QStringView wanted) {
            return QStringView(action.actionBindingId).compare(wanted) < 0;
        });
    return iterator != actions.cend() && QStringView(iterator->actionBindingId) == actionBindingId
               ? &*iterator
               : nullptr;
}

bool VerifiedSemanticBindingArtifact::permitsWritableActions() const
{
    return formatVersion == semanticBindingFormatVersion2
           && trust == EcpkgTrustClass::Production && !devices.isEmpty() && !actions.isEmpty();
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
        quint16 declaredFormatVersion = 0;
        quint32 declaredBindingCount = 0;
        quint32 declaredDeviceCount = 0;
        quint32 declaredActionCount = 0;
        if (!validateReportEnvelope(
                *report,
                container,
                manifest,
                configuration,
                &declaredFormatVersion,
                &declaredBindingCount,
                &declaredDeviceCount,
                &declaredActionCount,
                &error)) {
            return invalidArtifact(error);
        }

        const StrictJson &artifact = report->at("semantic_binding_manifest");
        const QString artifactPath = QString::fromLatin1("$.semantic_binding_manifest");
        const bool format2 = declaredFormatVersion == semanticBindingFormatVersion2;
        const bool fieldsValid
            = format2
                  ? hasExactFields(
                      artifact,
                      artifactPath,
                      {"format",
                       "format_version",
                       "binding_count",
                       "device_count",
                       "action_count",
                       "package",
                       "resource_catalog",
                       "topology",
                       "devices",
                       "bindings",
                       "actions"},
                      &error)
                  : hasExactFields(
                      artifact,
                      artifactPath,
                      {"format",
                       "format_version",
                       "binding_count",
                       "package",
                       "resource_catalog",
                       "topology",
                       "bindings"},
                      &error);
        if (!fieldsValid
            || !readConstantString(
                artifact.at("format"),
                format2 ? "ethercat-semantic-binding-v2" : "ethercat-semantic-binding-v1",
                artifactPath + ".format",
                &error)) {
            return invalidArtifact(error);
        }

        quint64 formatVersion = 0;
        quint64 bindingCount = 0;
        if (!readUnsigned(
                artifact.at("format_version"),
                declaredFormatVersion,
                declaredFormatVersion,
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
        if (format2) {
            quint64 deviceCount = 0;
            quint64 actionCount = 0;
            if (!readUnsigned(
                    artifact.at("device_count"),
                    1,
                    maximumDevices,
                    artifactPath + ".device_count",
                    &deviceCount,
                    &error)
                || !readUnsigned(
                    artifact.at("action_count"),
                    1,
                    maximumActions,
                    artifactPath + ".action_count",
                    &actionCount,
                    &error)
                || deviceCount != declaredDeviceCount || actionCount != declaredActionCount) {
                if (error.isEmpty()) {
                    error = artifactPath
                            + QString::fromLatin1(
                                " device/action counts differ from compile evidence");
                }
                return invalidArtifact(error);
            }
        }

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
        result.formatVersion = quint16(formatVersion);
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
                artifact.at("resource_catalog"),
                declaredFormatVersion,
                manifest,
                configuration,
                &result,
                &error)
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
                declaredFormatVersion,
                declaredBindingCount,
                configuration,
                &result,
                &error)) {
            return invalidArtifact(error);
        }
        if (format2
            && (!validateDevices(
                    artifact.at("devices"),
                    report->at("project_device_bindings"),
                    declaredDeviceCount,
                    &result,
                    &error)
                || !validateActions(
                    artifact.at("actions"),
                    report->at("semantic_action_plans"),
                    declaredActionCount,
                    configuration,
                    &result,
                    &error))) {
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
