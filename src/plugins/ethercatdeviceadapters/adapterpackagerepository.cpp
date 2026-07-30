// Copyright (C) 2026 Embed Labs

#include "adapterpackagerepository.h"

#include <ethercatcore/manualcontrolcontract.h>

#include <utils/id.h>

#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace EtherCAT::DeviceAdapters::Internal {

using namespace Data;

namespace {

constexpr char schemaVersionV1[] = "embed-labs.device-adapter/v1";
constexpr char schemaVersionV2[] = "embed-labs.device-adapter/v2";
constexpr char schemaVersionV3[] = "embed-labs.device-adapter/v3";
constexpr char canonicalJsonDomainV2[] = "embed-labs.device-adapter/v2";
constexpr char canonicalJsonDomainV3[] = "embed-labs.device-adapter/v3";

enum class PackageSchema { V1, V2, V3 };

struct Package
{
    DeviceAdapterManifest manifest;
    Utils::FilePath sourcePath;
};

static bool fail(QString *error, const QString &message)
{
    *error = message;
    return false;
}

static bool canonicalIdentifier(const QString &value)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > 256)
        return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

static bool stableV3Identifier(const QString &value)
{
    static const QRegularExpression pattern(
        QString::fromLatin1("^[A-Za-z0-9][A-Za-z0-9._:/{}-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

static bool simpleV3Identifier(const QString &value)
{
    static const QRegularExpression pattern(
        QString::fromLatin1("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

static bool exactSchema(PackageSchema schema)
{
    return schema == PackageSchema::V2 || schema == PackageSchema::V3;
}

static DeviceAdapterContractVersion contractVersion(PackageSchema schema)
{
    switch (schema) {
    case PackageSchema::V1:
        return DeviceAdapterContractVersion::V1;
    case PackageSchema::V2:
        return DeviceAdapterContractVersion::V2;
    case PackageSchema::V3:
        return DeviceAdapterContractVersion::V3;
    }
    return DeviceAdapterContractVersion::Unknown;
}

static bool parseCanonicalSignedDecimal(
    const QJsonValue &value, const QString &context, qint64 *result, QString *error)
{
    if (!value.isString())
        return fail(error, QString("%1 must be a canonical signed decimal string").arg(context));
    const QString text = value.toString();
    qsizetype digitIndex = 0;
    if (text.startsWith('-')) {
        if (text.size() < 2 || text.at(1) == '0')
            return fail(error, QString("%1 is not a canonical signed decimal").arg(context));
        digitIndex = 1;
    } else if (text.size() > 1 && text.startsWith('0')) {
        return fail(error, QString("%1 is not a canonical signed decimal").arg(context));
    }
    if (text.isEmpty() || std::any_of(text.cbegin() + digitIndex, text.cend(), [](QChar character) {
            return character < '0' || character > '9';
        })) {
        return fail(error, QString("%1 is not a canonical signed decimal").arg(context));
    }
    bool ok = false;
    const qint64 parsed = text.toLongLong(&ok, 10);
    if (!ok)
        return fail(error, QString("%1 exceeds the signed 64-bit range").arg(context));
    *result = parsed;
    return true;
}

static bool parseCanonicalUnsignedDecimal(
    const QJsonValue &value, const QString &context, quint64 *result, QString *error)
{
    if (!value.isString())
        return fail(error, QString("%1 must be a canonical unsigned decimal string").arg(context));
    const QString text = value.toString();
    if (text.isEmpty() || (text.size() > 1 && text.startsWith('0'))
        || std::any_of(text.cbegin(), text.cend(), [](QChar character) {
               return character < '0' || character > '9';
           })) {
        return fail(error, QString("%1 is not a canonical unsigned decimal").arg(context));
    }
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, 10);
    if (!ok)
        return fail(error, QString("%1 exceeds the unsigned 64-bit range").arg(context));
    *result = parsed;
    return true;
}

static void appendCanonicalJsonString(const QString &value, QByteArray *result)
{
    constexpr char hex[] = "0123456789abcdef";
    result->append('"');
    const QList<uint> codePoints = value.toUcs4();
    for (uint codePoint : codePoints) {
        switch (codePoint) {
        case '"':
            result->append("\\\"");
            continue;
        case '\\':
            result->append("\\\\");
            continue;
        case '\b':
            result->append("\\b");
            continue;
        case '\f':
            result->append("\\f");
            continue;
        case '\n':
            result->append("\\n");
            continue;
        case '\r':
            result->append("\\r");
            continue;
        case '\t':
            result->append("\\t");
            continue;
        default:
            break;
        }
        if (codePoint < 0x20) {
            result->append("\\u00");
            result->append(hex[(codePoint >> 4) & 0xf]);
            result->append(hex[codePoint & 0xf]);
            continue;
        }
        const char32_t utf32 = char32_t(codePoint);
        result->append(QString::fromUcs4(&utf32, 1).toUtf8());
    }
    result->append('"');
}

static bool appendCanonicalJson(
    const QJsonValue &value, const QString &context, QByteArray *result, QString *error)
{
    if (value.isNull() || value.isUndefined()) {
        result->append("null");
        return true;
    }
    if (value.isBool()) {
        result->append(value.toBool() ? "true" : "false");
        return true;
    }
    if (value.isString()) {
        appendCanonicalJsonString(value.toString(), result);
        return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        constexpr double largestExactlyRepresentableInteger = 9007199254740991.0;
        if (!std::isfinite(number) || std::trunc(number) != number
            || std::abs(number) > largestExactlyRepresentableInteger) {
            return fail(
                error,
                QString(
                    "%1 contains a non-canonical JSON number; v2 numbers must be exact "
                    "integers")
                    .arg(context));
        }
        result->append(QByteArray::number(qint64(number)));
        return true;
    }
    if (value.isArray()) {
        result->append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index != 0)
                result->append(',');
            if (!appendCanonicalJson(
                    array.at(index), QString("%1[%2]").arg(context).arg(index), result, error)) {
                return false;
            }
        }
        result->append(']');
        return true;
    }
    if (value.isObject()) {
        result->append('{');
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
            return left.toUtf8() < right.toUtf8();
        });
        for (qsizetype index = 0; index < keys.size(); ++index) {
            if (index != 0)
                result->append(',');
            const QString &key = keys.at(index);
            appendCanonicalJsonString(key, result);
            result->append(':');
            if (!appendCanonicalJson(
                    object.value(key), QString("%1.%2").arg(context, key), result, error)) {
                return false;
            }
        }
        result->append('}');
        return true;
    }
    return fail(error, QString("%1 contains an unsupported JSON value").arg(context));
}

static QByteArray canonicalContentSha256V2(
    const QJsonObject &object, const QString &context, QString *error)
{
    QByteArray canonical;
    if (!appendCanonicalJson(object, context, &canonical, error))
        return {};
    QByteArray digestInput(canonicalJsonDomainV2);
    digestInput.append('\0');
    digestInput.append(canonical);
    return QCryptographicHash::hash(digestInput, QCryptographicHash::Sha256);
}

static QByteArray canonicalContentSha256V3(
    const QJsonObject &object, const QString &context, QString *error)
{
    QByteArray canonical;
    if (!appendCanonicalJson(object, context, &canonical, error))
        return {};
    QByteArray digestInput(canonicalJsonDomainV3);
    digestInput.append('\0');
    digestInput.append(canonical);
    return QCryptographicHash::hash(digestInput, QCryptographicHash::Sha256);
}

static bool checkKeys(
    const QJsonObject &object,
    const QStringList &requiredKeys,
    const QString &context,
    QString *error)
{
    for (const QString &key : object.keys()) {
        if (!requiredKeys.contains(key))
            return fail(error, QString("%1: unknown field \"%2\"").arg(context, key));
    }
    for (const QString &key : requiredKeys) {
        if (!object.contains(key))
            return fail(error, QString("%1: missing field \"%2\"").arg(context, key));
    }
    return true;
}

static bool parseString(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QString *result,
    QString *error,
    bool allowEmpty = false)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
        return fail(error, QString("%1.%2 must be a string").arg(context, key));
    *result = value.toString();
    if (!allowEmpty && result->isEmpty())
        return fail(error, QString("%1.%2 must not be empty").arg(context, key));
    return true;
}

static bool parseNullableString(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QString *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        result->clear();
        return true;
    }
    return parseString(object, key, context, result, error, true);
}

static bool parseBool(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    bool *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool())
        return fail(error, QString("%1.%2 must be a Boolean").arg(context, key));
    *result = value.toBool();
    return true;
}

static bool parseIntegerValue(
    const QJsonValue &value, quint64 maximum, const QString &context, quint64 *result, QString *error)
{
    quint64 parsed = 0;
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < 0 || std::trunc(number) != number
            || number > double(maximum)) {
            return fail(error, QString("%1 must be an unsigned integer").arg(context));
        }
        parsed = quint64(number);
    } else {
        return fail(error, QString("%1 must be an unsigned integer").arg(context));
    }
    *result = parsed;
    return true;
}

static bool parseUnsigned(
    const QJsonObject &object,
    const QString &key,
    quint64 maximum,
    const QString &context,
    quint64 *result,
    QString *error)
{
    return parseIntegerValue(
        object.value(key), maximum, QString("%1.%2").arg(context, key), result, error);
}

static bool parseSignedInteger(
    const QJsonValue &value, const QString &context, qint64 *result, QString *error)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        constexpr double largestExactlyRepresentableInteger = 9007199254740991.0;
        if (!std::isfinite(number) || std::trunc(number) != number
            || std::abs(number) > largestExactlyRepresentableInteger) {
            return fail(error, QString("%1 must be an integer").arg(context));
        }
        *result = qint64(number);
        return true;
    }
    return fail(error, QString("%1 must be an integer").arg(context));
}

static bool parseDoubleOrNull(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    bool *present,
    double *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        *present = false;
        *result = 0.0;
        return true;
    }
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return fail(error, QString("%1.%2 must be a finite number or null").arg(context, key));
    *present = true;
    *result = value.toDouble();
    return true;
}

static bool parseScalar(
    const QJsonValue &value, const QString &context, bool allowNull, QVariant *result, QString *error)
{
    if (value.isNull()) {
        if (!allowNull)
            return fail(error, QString("%1 must not be null").arg(context));
        *result = {};
        return true;
    }
    if (!value.isBool() && !value.isDouble() && !value.isString())
        return fail(error, QString("%1 must be a JSON scalar").arg(context));
    *result = value.toVariant();
    return true;
}

static bool parseHash(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QByteArray *result,
    QString *error,
    bool allowEmpty = false)
{
    QString text;
    if (!parseString(object, key, context, &text, error, allowEmpty))
        return false;
    if (text.isEmpty()) {
        result->clear();
        return true;
    }
    const QByteArray encoded = text.toLatin1();
    const QByteArray decoded = QByteArray::fromHex(encoded);
    if (encoded.size() != 64 || decoded.size() != 32 || decoded.toHex() != encoded) {
        return fail(
            error,
            QString("%1.%2 must be an exact 64-character SHA-256 hex string").arg(context, key));
    }
    *result = decoded;
    return true;
}

static bool parseStringList(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QStringList *result,
    QString *error,
    bool requireCanonicalOrder = false)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray())
        return fail(error, QString("%1.%2 must be an array").arg(context, key));
    result->clear();
    const QJsonArray array = value.toArray();
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isString() || array.at(index).toString().isEmpty()) {
            return fail(
                error, QString("%1.%2[%3] must be a non-empty string").arg(context, key).arg(index));
        }
        result->append(array.at(index).toString());
    }
    QSet<QString> unique(result->cbegin(), result->cend());
    if (unique.size() != result->size())
        return fail(error, QString("%1.%2 contains duplicate values").arg(context, key));
    if (requireCanonicalOrder
        && !std::is_sorted(result->cbegin(), result->cend(), std::less<QString>())) {
        return fail(
            error, QString("%1.%2 must be sorted in strict canonical order").arg(context, key));
    }
    if (requireCanonicalOrder
        && std::any_of(result->cbegin(), result->cend(), [](const QString &item) {
               return !canonicalIdentifier(item);
           })) {
        return fail(error, QString("%1.%2 contains a non-canonical identifier").arg(context, key));
    }
    return true;
}

static bool parseCapabilityList(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QList<DeviceCapabilityId> *result,
    QString *error,
    bool requireCanonicalOrder = false)
{
    QStringList values;
    if (!parseStringList(object, key, context, &values, error, requireCanonicalOrder))
        return false;
    result->clear();
    for (const QString &value : std::as_const(values))
        result->append({value});
    return true;
}

static std::optional<DeviceAdapterQualification> qualificationFromString(const QString &value)
{
    if (value == "unqualified")
        return DeviceAdapterQualification::Unqualified;
    if (value == "candidate")
        return DeviceAdapterQualification::Candidate;
    if (value == "qualified")
        return DeviceAdapterQualification::Qualified;
    if (value == "mock-only")
        return DeviceAdapterQualification::MockOnly;
    if (value == "revoked")
        return DeviceAdapterQualification::Revoked;
    return std::nullopt;
}

static std::optional<SemanticSignalDirection> signalDirectionFromString(const QString &value)
{
    if (value == "input")
        return SemanticSignalDirection::Input;
    if (value == "output")
        return SemanticSignalDirection::Output;
    if (value == "bidirectional")
        return SemanticSignalDirection::Bidirectional;
    return std::nullopt;
}

static std::optional<SemanticSignalAccess> signalAccessFromString(const QString &value)
{
    if (value == "read-only")
        return SemanticSignalAccess::ReadOnly;
    if (value == "write-only")
        return SemanticSignalAccess::WriteOnly;
    if (value == "read-write")
        return SemanticSignalAccess::ReadWrite;
    return std::nullopt;
}

static std::optional<SemanticSignalExposure> signalExposureFromString(const QString &value)
{
    if (value == "public")
        return SemanticSignalExposure::Public;
    if (value == "action-only")
        return SemanticSignalExposure::ActionOnly;
    if (value == "internal")
        return SemanticSignalExposure::Internal;
    return std::nullopt;
}

static std::optional<EtherCATDataType> dataTypeFromString(const QString &value)
{
    if (value == "boolean")
        return EtherCATDataType::Boolean;
    if (value == "integer8")
        return EtherCATDataType::Integer8;
    if (value == "unsigned-integer8")
        return EtherCATDataType::UnsignedInteger8;
    if (value == "integer16")
        return EtherCATDataType::Integer16;
    if (value == "unsigned-integer16")
        return EtherCATDataType::UnsignedInteger16;
    if (value == "integer32")
        return EtherCATDataType::Integer32;
    if (value == "unsigned-integer32")
        return EtherCATDataType::UnsignedInteger32;
    if (value == "integer64")
        return EtherCATDataType::Integer64;
    if (value == "unsigned-integer64")
        return EtherCATDataType::UnsignedInteger64;
    if (value == "real32")
        return EtherCATDataType::Real32;
    if (value == "real64")
        return EtherCATDataType::Real64;
    if (value == "visible-string")
        return EtherCATDataType::VisibleString;
    if (value == "octet-string")
        return EtherCATDataType::OctetString;
    return std::nullopt;
}

static std::optional<EngineeringRounding> roundingFromString(const QString &value)
{
    if (value == "reject-inexact")
        return EngineeringRounding::RejectInexact;
    if (value == "toward-zero")
        return EngineeringRounding::TowardZero;
    if (value == "toward-negative-infinity")
        return EngineeringRounding::TowardNegativeInfinity;
    if (value == "toward-positive-infinity")
        return EngineeringRounding::TowardPositiveInfinity;
    if (value == "nearest-ties-to-even")
        return EngineeringRounding::NearestTiesToEven;
    return std::nullopt;
}

static bool parseExactRational(
    const QJsonValue &value, const QString &context, ExactRational *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an exact rational object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"numerator", "denominator"}, context, error))
        return false;
    qint64 numerator = 0;
    quint64 denominator = 0;
    if (!parseCanonicalSignedDecimal(
            object.value("numerator"), context + ".numerator", &numerator, error)
        || !parseCanonicalUnsignedDecimal(
            object.value("denominator"), context + ".denominator", &denominator, error)) {
        return false;
    }
    if (denominator == 0 || denominator > quint64(std::numeric_limits<qint64>::max())) {
        return fail(
            error,
            QString("%1.denominator must be in the signed positive 64-bit range").arg(context));
    }
    *result = {numerator, qint64(denominator)};
    const Core::EngineeringContractValidation validation = Core::validateExactRational(*result);
    if (!validation.accepted())
        return fail(error, QString("%1: %2").arg(context, validation.detail));
    return true;
}

static bool parseOptionalExactRational(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    std::optional<ExactRational> *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        result->reset();
        return true;
    }
    ExactRational parsed;
    if (!parseExactRational(value, QString("%1.%2").arg(context, key), &parsed, error))
        return false;
    *result = parsed;
    return true;
}

static bool parseEngineeringConstraint(
    const QJsonValue &value, const QString &context, EngineeringConstraint *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an engineering constraint object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(
            object, {"minimum", "maximum", "step", "stepOrigin", "enumeration"}, context, error)) {
        return false;
    }
    if (!parseOptionalExactRational(object, "minimum", context, &result->minimum, error)
        || !parseOptionalExactRational(object, "maximum", context, &result->maximum, error)
        || !parseOptionalExactRational(object, "step", context, &result->step, error)
        || !parseOptionalExactRational(object, "stepOrigin", context, &result->stepOrigin, error)) {
        return false;
    }

    const QJsonValue enumeration = object.value("enumeration");
    if (!enumeration.isArray())
        return fail(error, QString("%1.enumeration must be an array").arg(context));
    result->enumeration.clear();
    QString previousId;
    for (qsizetype index = 0; index < enumeration.toArray().size(); ++index) {
        const QString itemContext = QString("%1.enumeration[%2]").arg(context).arg(index);
        const QJsonValue itemValue = enumeration.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject item = itemValue.toObject();
        if (!checkKeys(item, {"id", "displayName", "value"}, itemContext, error))
            return false;
        EngineeringEnumerationValue entry;
        if (!parseString(item, "id", itemContext, &entry.id, error)
            || !parseString(item, "displayName", itemContext, &entry.displayName, error)
            || !parseExactRational(item.value("value"), itemContext + ".value", &entry.value, error)) {
            return false;
        }
        if (!canonicalIdentifier(entry.id) || (!previousId.isEmpty() && entry.id <= previousId)) {
            return fail(
                error,
                QString("%1.enumeration must use canonical IDs in strict sorted order").arg(context));
        }
        previousId = entry.id;
        result->enumeration.append(entry);
    }
    const Core::EngineeringContractValidation validation = Core::validateEngineeringConstraint(
        *result);
    if (!validation.accepted())
        return fail(error, QString("%1: %2").arg(context, validation.detail));
    return true;
}

static bool parseEngineeringTransform(
    const QJsonValue &value, const QString &context, EngineeringTransform *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an engineering transform object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"scale", "offset", "unit", "rounding", "constraint"}, context, error))
        return false;
    QString rounding;
    if (!parseExactRational(object.value("scale"), context + ".scale", &result->scale, error)
        || !parseExactRational(object.value("offset"), context + ".offset", &result->offset, error)
        || !parseString(object, "unit", context, &result->unit, error, true)
        || !parseString(object, "rounding", context, &rounding, error)
        || !parseEngineeringConstraint(
            object.value("constraint"), context + ".constraint", &result->constraint, error)) {
        return false;
    }
    const std::optional<EngineeringRounding> parsedRounding = roundingFromString(rounding);
    if (!parsedRounding)
        return fail(error, QString("%1.rounding is not supported").arg(context));
    result->rounding = parsedRounding;
    const Core::EngineeringContractValidation validation = Core::validateEngineeringTransform(
        *result);
    if (!validation.accepted())
        return fail(error, QString("%1: %2").arg(context, validation.detail));
    return true;
}

static bool parseEngineeringValue(
    const QJsonValue &value, const QString &context, EngineeringValue *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an exact engineering value object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"kind", "value"}, context, error))
        return false;
    QString kind;
    if (!parseString(object, "kind", context, &kind, error))
        return false;
    const QJsonValue encodedValue = object.value("value");
    if (kind == "boolean") {
        if (!encodedValue.isBool())
            return fail(error, QString("%1.value must be a Boolean").arg(context));
        *result = EngineeringValue::fromBoolean(encodedValue.toBool());
    } else if (kind == "signed-integer") {
        qint64 parsed = 0;
        if (!parseCanonicalSignedDecimal(encodedValue, context + ".value", &parsed, error))
            return false;
        *result = EngineeringValue::fromSignedInteger(parsed);
    } else if (kind == "unsigned-integer") {
        quint64 parsed = 0;
        if (!parseCanonicalUnsignedDecimal(encodedValue, context + ".value", &parsed, error))
            return false;
        *result = EngineeringValue::fromUnsignedInteger(parsed);
    } else if (kind == "exact-rational") {
        ExactRational parsed;
        if (!parseExactRational(encodedValue, context + ".value", &parsed, error))
            return false;
        *result = EngineeringValue::fromExactRational(parsed);
    } else if (kind == "enumeration") {
        if (!encodedValue.isString() || !canonicalIdentifier(encodedValue.toString()))
            return fail(error, QString("%1.value must be a canonical enumeration ID").arg(context));
        *result = EngineeringValue::fromEnumeration(encodedValue.toString());
    } else {
        return fail(error, QString("%1.kind is not supported").arg(context));
    }
    const Core::EngineeringContractValidation validation = Core::validateEngineeringValue(*result);
    if (!validation.accepted())
        return fail(error, QString("%1: %2").arg(context, validation.detail));
    return true;
}

static bool parseOptionalEngineeringValue(
    const QJsonValue &value,
    const QString &context,
    std::optional<EngineeringValue> *result,
    QString *error)
{
    if (value.isNull()) {
        result->reset();
        return true;
    }
    EngineeringValue parsed;
    if (!parseEngineeringValue(value, context, &parsed, error))
        return false;
    *result = parsed;
    return true;
}

static double rationalAsDisplayDouble(const ExactRational &value)
{
    return double(value.numerator) / double(value.denominator);
}

static void deriveLegacyValueMetadata(
    const EngineeringTransform &transform, SemanticValueMetadata *result)
{
    result->unit = transform.unit;
    result->scale = rationalAsDisplayDouble(transform.scale);
    result->offset = rationalAsDisplayDouble(transform.offset);
    result->hasMinimum = transform.constraint.minimum.has_value();
    result->minimum = result->hasMinimum ? rationalAsDisplayDouble(*transform.constraint.minimum)
                                         : 0.0;
    result->hasMaximum = transform.constraint.maximum.has_value();
    result->maximum = result->hasMaximum ? rationalAsDisplayDouble(*transform.constraint.maximum)
                                         : 0.0;
    result->hasStep = transform.constraint.step.has_value();
    result->step = result->hasStep ? rationalAsDisplayDouble(*transform.constraint.step) : 0.0;
    result->enumValues.clear();
    for (const EngineeringEnumerationValue &entry : transform.constraint.enumeration) {
        if (entry.value.denominator != 1)
            continue;
        result->enumValues.append({entry.value.numerator, entry.id, entry.displayName});
    }
}

static std::optional<ManualControlTimeoutAction> timeoutActionFromString(const QString &value)
{
    if (value == "reject")
        return ManualControlTimeoutAction::RejectFurtherWrites;
    if (value == "hold")
        return ManualControlTimeoutAction::HoldLastValue;
    if (value == "restore-safe")
        return ManualControlTimeoutAction::RestoreSafeValue;
    if (value == "controlled-stop")
        return ManualControlTimeoutAction::ControlledStop;
    return std::nullopt;
}

static bool parseValueMetadata(
    const QJsonValue &value, const QString &context, SemanticValueMetadata *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(
            object,
            {"unit", "scale", "offset", "minimum", "maximum", "step", "enumValues"},
            context,
            error)) {
        return false;
    }
    if (!parseString(object, "unit", context, &result->unit, error, true))
        return false;
    const QJsonValue scale = object.value("scale");
    const QJsonValue offset = object.value("offset");
    if (!scale.isDouble() || !std::isfinite(scale.toDouble()) || scale.toDouble() == 0.0)
        return fail(error, QString("%1.scale must be a finite non-zero number").arg(context));
    if (!offset.isDouble() || !std::isfinite(offset.toDouble()))
        return fail(error, QString("%1.offset must be a finite number").arg(context));
    result->scale = scale.toDouble();
    result->offset = offset.toDouble();
    if (!parseDoubleOrNull(object, "minimum", context, &result->hasMinimum, &result->minimum, error)
        || !parseDoubleOrNull(object, "maximum", context, &result->hasMaximum, &result->maximum, error)
        || !parseDoubleOrNull(object, "step", context, &result->hasStep, &result->step, error)) {
        return false;
    }
    if (result->hasMinimum && result->hasMaximum && result->minimum > result->maximum)
        return fail(error, QString("%1 minimum exceeds maximum").arg(context));
    if (result->hasStep && result->step <= 0.0)
        return fail(error, QString("%1.step must be positive").arg(context));

    const QJsonValue enumValues = object.value("enumValues");
    if (!enumValues.isArray())
        return fail(error, QString("%1.enumValues must be an array").arg(context));
    result->enumValues.clear();
    QSet<qint64> seenValues;
    QSet<QString> seenNames;
    for (qsizetype index = 0; index < enumValues.toArray().size(); ++index) {
        const QString itemContext = QString("%1.enumValues[%2]").arg(context).arg(index);
        const QJsonValue itemValue = enumValues.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject item = itemValue.toObject();
        if (!checkKeys(item, {"value", "name", "displayName"}, itemContext, error))
            return false;
        SemanticEnumValue enumValue;
        if (!parseSignedInteger(item.value("value"), itemContext + ".value", &enumValue.value, error)
            || !parseString(item, "name", itemContext, &enumValue.name, error)
            || !parseString(item, "displayName", itemContext, &enumValue.displayName, error)) {
            return false;
        }
        if (seenValues.contains(enumValue.value) || seenNames.contains(enumValue.name))
            return fail(error, QString("%1 contains a duplicate enum value or name").arg(context));
        seenValues.insert(enumValue.value);
        seenNames.insert(enumValue.name);
        result->enumValues.append(enumValue);
    }
    return true;
}

static bool parseManualControl(
    const QJsonValue &value, const QString &context, ManualControlPolicy *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(
            object,
            {"policyId",
             "allowed",
             "requiresExclusiveControl",
             "holdToRun",
             "commandTimeoutMs",
             "timeoutAction"},
            context,
            error)) {
        return false;
    }
    if (!parseString(object, "policyId", context, &result->policyId, error, true)
        || !parseBool(object, "allowed", context, &result->allowed, error)
        || !parseBool(
            object, "requiresExclusiveControl", context, &result->requiresExclusiveControl, error)
        || !parseBool(object, "holdToRun", context, &result->holdToRun, error)) {
        return false;
    }
    quint64 commandTimeoutMs = 0;
    if (!parseUnsigned(
            object,
            "commandTimeoutMs",
            std::numeric_limits<quint32>::max(),
            context,
            &commandTimeoutMs,
            error)) {
        return false;
    }
    result->commandTimeoutMs = quint32(commandTimeoutMs);
    QString timeoutAction;
    if (!parseString(object, "timeoutAction", context, &timeoutAction, error))
        return false;
    const auto parsedTimeoutAction = timeoutActionFromString(timeoutAction);
    if (!parsedTimeoutAction)
        return fail(error, QString("%1.timeoutAction is not supported").arg(context));
    result->timeoutAction = *parsedTimeoutAction;
    if (result->allowed && result->policyId.isEmpty())
        return fail(
            error, QString("%1.policyId is required for writable manual control").arg(context));
    return true;
}

static bool parseBinding(
    const QJsonValue &value, const QString &context, DeviceSignalBinding *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(
            object,
            {"kind",
             "pdoDirection",
             "pdoIndex",
             "objectIndex",
             "subIndex",
             "physicalType",
             "bitWidth",
             "byteOrder",
             "slotRelative"},
            context,
            error)) {
        return false;
    }
    QString kind;
    QString pdoDirection;
    QString physicalType;
    QString byteOrder;
    if (!parseString(object, "kind", context, &kind, error)
        || !parseString(object, "pdoDirection", context, &pdoDirection, error)
        || !parseString(object, "physicalType", context, &physicalType, error)
        || !parseString(object, "byteOrder", context, &byteOrder, error)
        || !parseBool(object, "slotRelative", context, &result->slotRelative, error)) {
        return false;
    }
    if (kind == "process-data-object")
        result->kind = DeviceSignalBindingKind::ProcessDataObject;
    else if (kind == "object-dictionary")
        result->kind = DeviceSignalBindingKind::ObjectDictionary;
    else
        return fail(error, QString("%1.kind is not supported").arg(context));
    if (pdoDirection == "rx")
        result->pdoDirection = PdoDirection::Rx;
    else if (pdoDirection == "tx")
        result->pdoDirection = PdoDirection::Tx;
    else
        return fail(error, QString("%1.pdoDirection is not supported").arg(context));
    const auto parsedType = dataTypeFromString(physicalType);
    if (!parsedType)
        return fail(error, QString("%1.physicalType is not supported").arg(context));
    result->physicalType = *parsedType;
    if (byteOrder == "little-endian")
        result->byteOrder = DeviceByteOrder::LittleEndian;
    else if (byteOrder == "big-endian")
        result->byteOrder = DeviceByteOrder::BigEndian;
    else
        return fail(error, QString("%1.byteOrder is not supported").arg(context));

    quint64 pdoIndex = 0;
    quint64 objectIndex = 0;
    quint64 subIndex = 0;
    quint64 bitWidth = 0;
    if (!parseUnsigned(
            object, "pdoIndex", std::numeric_limits<quint16>::max(), context, &pdoIndex, error)
        || !parseUnsigned(
            object, "objectIndex", std::numeric_limits<quint16>::max(), context, &objectIndex, error)
        || !parseUnsigned(
            object, "subIndex", std::numeric_limits<quint8>::max(), context, &subIndex, error)
        || !parseUnsigned(
            object, "bitWidth", std::numeric_limits<int>::max(), context, &bitWidth, error)) {
        return false;
    }
    if (objectIndex == 0 || bitWidth == 0)
        return fail(error, QString("%1 objectIndex and bitWidth must be non-zero").arg(context));
    if (result->kind == DeviceSignalBindingKind::ProcessDataObject && pdoIndex == 0)
        return fail(error, QString("%1.pdoIndex must be non-zero for process data").arg(context));
    result->pdoIndex = quint16(pdoIndex);
    result->objectIndex = quint16(objectIndex);
    result->objectSubIndex = quint8(subIndex);
    result->bitWidth = int(bitWidth);
    return true;
}

static auto bindingCanonicalKey(const DeviceSignalBinding &binding)
{
    return std::tuple{
        int(binding.kind),
        int(binding.pdoDirection),
        binding.pdoIndex,
        binding.objectIndex,
        binding.objectSubIndex,
        int(binding.physicalType),
        binding.bitWidth,
        int(binding.byteOrder),
        binding.slotRelative,
    };
}

static bool parseSignal(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    SemanticSignalDefinition *result,
    QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    const QStringList keys
        = schema == PackageSchema::V1
              ? QStringList{
                    "id",
                    "displayName",
                    "description",
                    "capabilities",
                    "direction",
                    "access",
                    "required",
                    "bindings",
                    "value",
                    "safeValue",
                    "manualControl",
                }
              : schema == PackageSchema::V2
                    ? QStringList{
                          "id",
                          "displayName",
                          "description",
                          "capabilities",
                          "direction",
                          "access",
                          "exposure",
                          "required",
                          "bindings",
                          "engineeringTransform",
                      }
                    : QStringList{
                          "id",
                          "displayName",
                          "description",
                          "capabilities",
                          "direction",
                          "access",
                          "exposure",
                          "required",
                          "bindings",
                          "engineeringTransform",
                          "engineeringSafeValue",
                      };
    if (!checkKeys(object, keys, context, error)) {
        return false;
    }
    if (!parseString(object, "id", context, &result->id.value, error)
        || !parseString(object, "displayName", context, &result->displayName, error)
        || !parseString(object, "description", context, &result->description, error, true)
        || !parseCapabilityList(
            object, "capabilities", context, &result->capabilities, error, exactSchema(schema))
        || !parseBool(object, "required", context, &result->requiredForComplete, error)) {
        return false;
    }
    if (exactSchema(schema) && !canonicalIdentifier(result->id.value))
        return fail(error, QString("%1.id must be a canonical identifier").arg(context));
    if (schema == PackageSchema::V3 && !stableV3Identifier(result->id.value))
        return fail(error, QString("%1.id must be a stable v3 identifier").arg(context));
    QString direction;
    QString access;
    if (!parseString(object, "direction", context, &direction, error)
        || !parseString(object, "access", context, &access, error)) {
        return false;
    }
    const auto parsedDirection = signalDirectionFromString(direction);
    const auto parsedAccess = signalAccessFromString(access);
    if (!parsedDirection)
        return fail(error, QString("%1.direction is not supported").arg(context));
    if (!parsedAccess)
        return fail(error, QString("%1.access is not supported").arg(context));
    result->direction = *parsedDirection;
    result->access = *parsedAccess;
    if (exactSchema(schema)) {
        QString exposure;
        if (!parseString(object, "exposure", context, &exposure, error))
            return false;
        const std::optional<SemanticSignalExposure> parsedExposure = signalExposureFromString(
            exposure);
        if (!parsedExposure)
            return fail(error, QString("%1.exposure is not supported").arg(context));
        result->exposure = *parsedExposure;
    }

    const QJsonValue bindings = object.value("bindings");
    if (!bindings.isArray() || bindings.toArray().isEmpty())
        return fail(error, QString("%1.bindings must be a non-empty array").arg(context));
    result->bindings.clear();
    for (qsizetype index = 0; index < bindings.toArray().size(); ++index) {
        DeviceSignalBinding binding;
        if (!parseBinding(
                bindings.toArray().at(index),
                QString("%1.bindings[%2]").arg(context).arg(index),
                &binding,
                error)) {
            return false;
        }
        if (exactSchema(schema) && !result->bindings.isEmpty()
            && bindingCanonicalKey(binding) <= bindingCanonicalKey(result->bindings.constLast())) {
            return fail(
                error,
                QString("%1.bindings must be unique and in strict canonical order").arg(context));
        }
        result->bindings.append(binding);
    }
    if (schema == PackageSchema::V1) {
        if (!parseValueMetadata(
                object.value("value"), context + ".value", &result->valueMetadata, error)
            || !parseManualControl(
                object.value("manualControl"),
                context + ".manualControl",
                &result->manualControl,
                error)
            || !parseScalar(
                object.value("safeValue"), context + ".safeValue", true, &result->safeValue, error)) {
            return false;
        }
        result->hasSafeValue = !object.value("safeValue").isNull();
    } else {
        EngineeringTransform transform;
        if (!parseEngineeringTransform(
                object.value("engineeringTransform"),
                context + ".engineeringTransform",
                &transform,
                error)) {
            return false;
        }
        result->engineeringTransform = transform;
        deriveLegacyValueMetadata(transform, &result->valueMetadata);
        if (schema == PackageSchema::V3
            && !parseOptionalEngineeringValue(
                object.value("engineeringSafeValue"),
                context + ".engineeringSafeValue",
                &result->engineeringSafeValue,
                error)) {
            return false;
        }
        result->hasSafeValue = false;
        result->safeValue = {};
        result->manualControl = {};
    }
    if (result->direction == SemanticSignalDirection::Input
        && result->access != SemanticSignalAccess::ReadOnly) {
        return fail(error, QString("%1 input signal must be read-only").arg(context));
    }
    if (result->access == SemanticSignalAccess::ReadOnly && result->manualControl.allowed)
        return fail(error, QString("%1 read-only signal cannot allow manual writes").arg(context));
    if (exactSchema(schema) && result->exposure == SemanticSignalExposure::ActionOnly
        && (result->direction == SemanticSignalDirection::Input
            || result->access == SemanticSignalAccess::ReadOnly)) {
        return fail(error, QString("%1 action-only signal must be writable").arg(context));
    }
    return true;
}

static bool parseSignals(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    QList<SemanticSignalDefinition> *result,
    QString *error)
{
    if (!value.isArray())
        return fail(error, QString("%1 must be an array").arg(context));
    result->clear();
    QSet<QString> ids;
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        SemanticSignalDefinition signal;
        if (!parseSignal(
                value.toArray().at(index),
                QString("%1[%2]").arg(context).arg(index),
                schema,
                &signal,
                error)) {
            return false;
        }
        if (ids.contains(signal.id.value))
            return fail(
                error,
                QString("%1 contains duplicate signal id \"%2\"").arg(context, signal.id.value));
        ids.insert(signal.id.value);
        if (exactSchema(schema) && !result->isEmpty()
            && signal.id.value <= result->constLast().id.value) {
            return fail(error, QString("%1 must be in strict semantic ID order").arg(context));
        }
        result->append(signal);
    }
    return true;
}

static bool parseIndexList(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QList<quint16> *result,
    QString *error,
    bool requireCanonicalOrder = false)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray())
        return fail(error, QString("%1.%2 must be an array").arg(context, key));
    result->clear();
    QSet<quint16> seen;
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        quint64 parsed = 0;
        if (!parseIntegerValue(
                value.toArray().at(index),
                std::numeric_limits<quint16>::max(),
                QString("%1.%2[%3]").arg(context, key).arg(index),
                &parsed,
                error)) {
            return false;
        }
        if (parsed == 0 || seen.contains(quint16(parsed)))
            return fail(error, QString("%1.%2 contains zero or a duplicate index").arg(context, key));
        if (requireCanonicalOrder && !result->isEmpty() && parsed <= result->constLast()) {
            return fail(error, QString("%1.%2 must be in strict numeric order").arg(context, key));
        }
        seen.insert(quint16(parsed));
        result->append(quint16(parsed));
    }
    return true;
}

static bool parseProfiles(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    QList<ProcessDataProfile> *result,
    QString *error)
{
    if (!value.isArray())
        return fail(error, QString("%1 must be an array").arg(context));
    result->clear();
    QSet<QString> ids;
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        const QString itemContext = QString("%1[%2]").arg(context).arg(index);
        const QJsonValue itemValue = value.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject object = itemValue.toObject();
        const QStringList keys = schema == PackageSchema::V3
                                     ? QStringList{
                                           "id",
                                           "signedPdoProfileId",
                                           "rxPdos",
                                           "txPdos",
                                           "requiredSignals",
                                       }
                                     : QStringList{"id", "rxPdos", "txPdos", "requiredSignals"};
        if (!checkKeys(object, keys, itemContext, error)) {
            return false;
        }
        ProcessDataProfile profile;
        QStringList requiredSignals;
        if (!parseString(object, "id", itemContext, &profile.id, error)
            || (schema == PackageSchema::V3
                && !parseString(
                    object, "signedPdoProfileId", itemContext, &profile.signedPdoProfileId, error))
            || !parseIndexList(
                object, "rxPdos", itemContext, &profile.rxPdoIndices, error, exactSchema(schema))
            || !parseIndexList(
                object, "txPdos", itemContext, &profile.txPdoIndices, error, exactSchema(schema))
            || !parseStringList(
                object,
                "requiredSignals",
                itemContext,
                &requiredSignals,
                error,
                exactSchema(schema))) {
            return false;
        }
        if (exactSchema(schema) && !canonicalIdentifier(profile.id))
            return fail(error, QString("%1.id must be canonical").arg(itemContext));
        if (schema == PackageSchema::V3 && !simpleV3Identifier(profile.signedPdoProfileId)) {
            return fail(error, QString("%1.signedPdoProfileId must be canonical").arg(itemContext));
        }
        if (schema == PackageSchema::V3
            && (!stableV3Identifier(profile.id) || !simpleV3Identifier(profile.signedPdoProfileId)
                || std::any_of(
                    requiredSignals.cbegin(), requiredSignals.cend(), [](const QString &signal) {
                        return !stableV3Identifier(signal);
                    }))) {
            return fail(
                error, QString("%1 profile IDs must be stable v3 identifiers").arg(itemContext));
        }
        if (profile.rxPdoIndices.isEmpty() && profile.txPdoIndices.isEmpty())
            return fail(error, QString("%1 must select at least one PDO").arg(itemContext));
        if (ids.contains(profile.id))
            return fail(
                error, QString("%1 contains duplicate profile id \"%2\"").arg(context, profile.id));
        ids.insert(profile.id);
        if (exactSchema(schema) && !result->isEmpty() && profile.id <= result->constLast().id) {
            return fail(error, QString("%1 must be in strict profile ID order").arg(context));
        }
        for (const QString &signal : std::as_const(requiredSignals))
            profile.requiredSignals.append({signal});
        result->append(profile);
    }
    return true;
}

static bool parseModules(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    QList<DeviceModuleProfile> *result,
    QString *error)
{
    if (!value.isArray())
        return fail(error, QString("%1 must be an array").arg(context));
    result->clear();
    QSet<QString> ids;
    QSet<quint32> moduleIdents;
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        const QString itemContext = QString("%1[%2]").arg(context).arg(index);
        const QJsonValue itemValue = value.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject object = itemValue.toObject();
        if (!checkKeys(
                object,
                {"id", "moduleIdent", "typeName", "moduleClass", "capabilities", "signals"},
                itemContext,
                error)) {
            return false;
        }
        DeviceModuleProfile profile;
        quint64 moduleIdent = 0;
        if (!parseString(object, "id", itemContext, &profile.id, error)
            || !parseUnsigned(
                object,
                "moduleIdent",
                std::numeric_limits<quint32>::max(),
                itemContext,
                &moduleIdent,
                error)
            || !parseString(object, "typeName", itemContext, &profile.typeName, error)
            || !parseString(object, "moduleClass", itemContext, &profile.moduleClass, error)
            || !parseCapabilityList(
                object, "capabilities", itemContext, &profile.capabilities, error, exactSchema(schema))
            || !parseSignals(
                object.value("signals"),
                itemContext + ".signals",
                schema,
                &profile.slotRelativeSignals,
                error)) {
            return false;
        }
        if (exactSchema(schema) && !canonicalIdentifier(profile.id))
            return fail(error, QString("%1.id must be canonical").arg(itemContext));
        if (schema == PackageSchema::V3 && !stableV3Identifier(profile.id))
            return fail(error, QString("%1.id must be a stable v3 identifier").arg(itemContext));
        if (moduleIdent == 0)
            return fail(error, QString("%1.moduleIdent must be non-zero").arg(itemContext));
        profile.moduleIdent = quint32(moduleIdent);
        if (ids.contains(profile.id) || moduleIdents.contains(profile.moduleIdent)) {
            return fail(
                error, QString("%1 contains a duplicate module id or ModuleIdent").arg(context));
        }
        ids.insert(profile.id);
        moduleIdents.insert(profile.moduleIdent);
        if (exactSchema(schema) && !result->isEmpty()) {
            const DeviceModuleProfile &previous = result->constLast();
            if (std::tie(profile.moduleIdent, profile.id)
                <= std::tie(previous.moduleIdent, previous.id)) {
                return fail(
                    error, QString("%1 must be ordered by ModuleIdent and module ID").arg(context));
            }
        }
        for (const SemanticSignalDefinition &signal : std::as_const(profile.slotRelativeSignals)) {
            if (!signal.id.value.contains("{slot}")) {
                return fail(
                    error,
                    QString("%1 signal \"%2\" must contain {slot}")
                        .arg(itemContext, signal.id.value));
            }
            if (!std::all_of(
                    signal.bindings.cbegin(),
                    signal.bindings.cend(),
                    [](const DeviceSignalBinding &binding) { return binding.slotRelative; })) {
                return fail(
                    error,
                    QString("%1 signal \"%2\" must use only slot-relative bindings")
                        .arg(itemContext, signal.id.value));
            }
        }
        result->append(profile);
    }
    return true;
}

static bool parseControlValue(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    DeviceControlValue *result,
    QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    const QStringList keys = schema == PackageSchema::V1
                                 ? QStringList{"source", "literal", "parameterId"}
                                 : QStringList{"source", "engineeringLiteralValue", "parameterId"};
    if (!checkKeys(object, keys, context, error))
        return false;
    QString source;
    if (!parseString(object, "source", context, &source, error)
        || !parseNullableString(object, "parameterId", context, &result->parameterId, error)) {
        return false;
    }
    if (schema == PackageSchema::V1) {
        if (!parseScalar(
                object.value("literal"), context + ".literal", true, &result->literalValue, error)) {
            return false;
        }
    } else {
        if (!parseOptionalEngineeringValue(
                object.value("engineeringLiteralValue"),
                context + ".engineeringLiteralValue",
                &result->engineeringLiteralValue,
                error)) {
            return false;
        }
        result->literalValue = {};
        if (!result->parameterId.isEmpty() && !canonicalIdentifier(result->parameterId)) {
            return fail(error, QString("%1.parameterId must be canonical").arg(context));
        }
    }
    if (source == "invalid")
        result->source = DeviceControlValueSource::Invalid;
    else if (source == "literal")
        result->source = DeviceControlValueSource::Literal;
    else if (source == "parameter")
        result->source = DeviceControlValueSource::Parameter;
    else
        return fail(error, QString("%1.source is not supported").arg(context));
    const bool hasLiteral = schema == PackageSchema::V1
                                ? !object.value("literal").isNull()
                                : result->engineeringLiteralValue.has_value();
    if (result->source == DeviceControlValueSource::Literal && !hasLiteral)
        return fail(error, QString("%1 requires an exact literal value").arg(context));
    if (exactSchema(schema) && result->source != DeviceControlValueSource::Literal && hasLiteral) {
        return fail(error, QString("%1 literal is only valid for literal source").arg(context));
    }
    if (result->source == DeviceControlValueSource::Parameter && result->parameterId.isEmpty())
        return fail(error, QString("%1.parameterId must be set for parameter source").arg(context));
    if (result->source != DeviceControlValueSource::Parameter && !result->parameterId.isEmpty())
        return fail(error, QString("%1.parameterId is only valid for parameter source").arg(context));
    return true;
}

static bool parseActionParameter(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    DeviceControlActionParameter *result,
    QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    const QStringList keys = schema == PackageSchema::V1
                                 ? QStringList{
                                       "id",
                                       "displayName",
                                       "description",
                                       "dataType",
                                       "value",
                                       "required",
                                       "defaultValue",
                                   }
                                 : QStringList{
                                       "id",
                                       "displayName",
                                       "description",
                                       "dataType",
                                       "unit",
                                       "engineeringConstraint",
                                       "required",
                                       "engineeringDefaultValue",
                                   };
    if (!checkKeys(object, keys, context, error)) {
        return false;
    }
    QString dataType;
    if (!parseString(object, "id", context, &result->id, error)
        || !parseString(object, "displayName", context, &result->displayName, error)
        || !parseString(object, "description", context, &result->description, error, true)
        || !parseString(object, "dataType", context, &dataType, error)
        || !parseBool(object, "required", context, &result->required, error)) {
        return false;
    }
    if (schema == PackageSchema::V1) {
        if (!parseValueMetadata(
                object.value("value"), context + ".value", &result->valueMetadata, error)
            || !parseScalar(
                object.value("defaultValue"),
                context + ".defaultValue",
                true,
                &result->defaultValue,
                error)) {
            return false;
        }
        result->hasDefaultValue = !object.value("defaultValue").isNull();
    } else {
        if (!canonicalIdentifier(result->id))
            return fail(error, QString("%1.id must be canonical").arg(context));
        if (!parseString(object, "unit", context, &result->valueMetadata.unit, error, true)) {
            return false;
        }
        EngineeringConstraint constraint;
        if (!parseEngineeringConstraint(
                object.value("engineeringConstraint"),
                context + ".engineeringConstraint",
                &constraint,
                error)
            || !parseOptionalEngineeringValue(
                object.value("engineeringDefaultValue"),
                context + ".engineeringDefaultValue",
                &result->engineeringDefaultValue,
                error)) {
            return false;
        }
        result->engineeringConstraint = constraint;
        result->hasDefaultValue = result->engineeringDefaultValue.has_value();
        result->defaultValue = {};
        result->valueMetadata.scale = 1.0;
        result->valueMetadata.offset = 0.0;
        result->valueMetadata.hasMinimum = constraint.minimum.has_value();
        result->valueMetadata.minimum = constraint.minimum
                                            ? rationalAsDisplayDouble(*constraint.minimum)
                                            : 0.0;
        result->valueMetadata.hasMaximum = constraint.maximum.has_value();
        result->valueMetadata.maximum = constraint.maximum
                                            ? rationalAsDisplayDouble(*constraint.maximum)
                                            : 0.0;
        result->valueMetadata.hasStep = constraint.step.has_value();
        result->valueMetadata.step = constraint.step ? rationalAsDisplayDouble(*constraint.step)
                                                     : 0.0;
        if (result->engineeringDefaultValue) {
            const Core::EngineeringContractValidation validation
                = Core::validateEngineeringValueAgainstConstraint(
                    *result->engineeringDefaultValue, constraint);
            if (!validation.accepted())
                return fail(error, QString("%1: %2").arg(context, validation.detail));
        }
    }
    const auto parsedType = dataTypeFromString(dataType);
    if (!parsedType)
        return fail(error, QString("%1.dataType is not supported").arg(context));
    result->dataType = *parsedType;
    return true;
}

static bool parseV3AssignmentValue(
    const QJsonValue &value, const QString &context, DeviceControlValue *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"source", "engineeringConstant", "parameterId"}, context, error))
        return false;

    QString source;
    if (!parseString(object, "source", context, &source, error)
        || !parseNullableString(object, "parameterId", context, &result->parameterId, error)
        || !parseOptionalEngineeringValue(
            object.value("engineeringConstant"),
            context + ".engineeringConstant",
            &result->engineeringLiteralValue,
            error)) {
        return false;
    }
    if (!result->parameterId.isEmpty() && !simpleV3Identifier(result->parameterId))
        return fail(error, QString("%1.parameterId must be canonical").arg(context));

    if (source == "constant")
        result->source = DeviceControlValueSource::Literal;
    else if (source == "parameter")
        result->source = DeviceControlValueSource::Parameter;
    else
        return fail(error, QString("%1.source must be constant or parameter").arg(context));

    if (result->source == DeviceControlValueSource::Literal) {
        if (!result->engineeringLiteralValue || !result->parameterId.isEmpty()) {
            return fail(
                error, QString("%1 constant source requires only engineeringConstant").arg(context));
        }
    } else if (result->engineeringLiteralValue || result->parameterId.isEmpty()) {
        return fail(error, QString("%1 parameter source requires only parameterId").arg(context));
    }
    return true;
}

static std::optional<DeviceControlGroupRecovery> groupRecoveryFromString(const QString &value)
{
    if (value == "return-task")
        return DeviceControlGroupRecovery::ReturnTask;
    if (value == "hold-safe")
        return DeviceControlGroupRecovery::HoldSafe;
    return std::nullopt;
}

static std::optional<DeviceControlFailureDisposition> failureDispositionFromString(
    const QString &value)
{
    if (value == "return-task")
        return DeviceControlFailureDisposition::ReturnTask;
    if (value == "hold-safe")
        return DeviceControlFailureDisposition::HoldSafe;
    if (value == "hold-operational-fault")
        return DeviceControlFailureDisposition::HoldOperationalFault;
    return std::nullopt;
}

static bool parseV3ConsistencyGroups(
    const QJsonValue &value,
    const QString &context,
    QList<DeviceControlConsistencyGroup> *result,
    QString *error)
{
    if (!value.isArray())
        return fail(error, QString("%1 must be an array").arg(context));
    result->clear();
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        const QString itemContext = QString("%1[%2]").arg(context).arg(index);
        const QJsonValue itemValue = value.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject object = itemValue.toObject();
        if (!checkKeys(object, {"id", "members", "recovery", "maxTtlCycles"}, itemContext, error)) {
            return false;
        }

        DeviceControlConsistencyGroup group;
        QStringList members;
        QString recovery;
        quint64 maximumTtlCycles = 0;
        if (!parseString(object, "id", itemContext, &group.id, error)
            || !parseStringList(object, "members", itemContext, &members, error, true)
            || !parseString(object, "recovery", itemContext, &recovery, error)
            || !parseUnsigned(
                object,
                "maxTtlCycles",
                std::numeric_limits<quint16>::max(),
                itemContext,
                &maximumTtlCycles,
                error)) {
            return false;
        }
        if (!simpleV3Identifier(group.id))
            return fail(error, QString("%1.id must be canonical").arg(itemContext));
        if (std::any_of(members.cbegin(), members.cend(), [](const QString &member) {
                return !stableV3Identifier(member);
            })) {
            return fail(error, QString("%1.members contains an unstable identifier").arg(itemContext));
        }
        if (members.isEmpty() || members.size() > 64) {
            return fail(error, QString("%1.members must contain one to 64 signals").arg(itemContext));
        }
        if (!result->isEmpty() && group.id <= result->constLast().id) {
            return fail(error, QString("%1 must be in strict group ID order").arg(context));
        }
        const auto parsedRecovery = groupRecoveryFromString(recovery);
        if (!parsedRecovery)
            return fail(error, QString("%1.recovery is not supported").arg(itemContext));
        if (maximumTtlCycles == 0) {
            return fail(
                error, QString("%1.maxTtlCycles must be in the range 1 to 65535").arg(itemContext));
        }
        group.recovery = *parsedRecovery;
        group.maximumTtlCycles = quint32(maximumTtlCycles);
        for (const QString &member : std::as_const(members))
            group.members.append({member});
        result->append(group);
    }
    return true;
}

static bool parseV3ActionStep(
    const QJsonValue &value, const QString &context, DeviceControlStep *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    QString kind;
    if (!parseString(object, "kind", context, &kind, error))
        return false;

    if (kind == "write-group") {
        if (!checkKeys(object, {"kind", "consistencyGroup", "assignments"}, context, error)
            || !parseString(object, "consistencyGroup", context, &result->consistencyGroupId, error)) {
            return false;
        }
        if (!simpleV3Identifier(result->consistencyGroupId)) {
            return fail(error, QString("%1.consistencyGroup must be canonical").arg(context));
        }
        const QJsonValue assignments = object.value("assignments");
        if (!assignments.isArray() || assignments.toArray().isEmpty()
            || assignments.toArray().size() > 64) {
            return fail(error, QString("%1.assignments must contain one to 64 values").arg(context));
        }
        QString previousSignal;
        for (qsizetype index = 0; index < assignments.toArray().size(); ++index) {
            const QString assignmentContext = QString("%1.assignments[%2]").arg(context).arg(index);
            const QJsonValue assignmentValue = assignments.toArray().at(index);
            if (!assignmentValue.isObject())
                return fail(error, QString("%1 must be an object").arg(assignmentContext));
            const QJsonObject assignmentObject = assignmentValue.toObject();
            DeviceControlGroupAssignment assignment;
            if (!checkKeys(assignmentObject, {"signalId", "value"}, assignmentContext, error)
                || !parseString(
                    assignmentObject, "signalId", assignmentContext, &assignment.signalId.value, error)
                || !parseV3AssignmentValue(
                    assignmentObject.value("value"),
                    assignmentContext + ".value",
                    &assignment.value,
                    error)) {
                return false;
            }
            if (!stableV3Identifier(assignment.signalId.value)
                || (!previousSignal.isEmpty() && assignment.signalId.value <= previousSignal)) {
                return fail(
                    error,
                    QString("%1.assignments must use canonical signals in strict order")
                        .arg(context));
            }
            previousSignal = assignment.signalId.value;
            result->assignments.append(assignment);
        }
        result->kind = DeviceControlStepKind::WriteGroup;
        return true;
    }

    if (kind == "wait-masked-equals") {
        if (!checkKeys(object, {"kind", "signalId", "value", "mask", "timeoutCycles"}, context, error)
            || !parseString(object, "signalId", context, &result->signalId.value, error)
            || !parseEngineeringValue(
                object.value("value"),
                context + ".value",
                &result->value.engineeringLiteralValue.emplace(),
                error)
            || !parseEngineeringValue(
                object.value("mask"),
                context + ".mask",
                &result->mask.engineeringLiteralValue.emplace(),
                error)) {
            return false;
        }
        result->value.source = DeviceControlValueSource::Literal;
        result->mask.source = DeviceControlValueSource::Literal;
        result->kind = DeviceControlStepKind::WaitMaskedEquals;
    } else if (kind == "wait-absolute-at-most") {
        if (!checkKeys(object, {"kind", "signalId", "limit", "timeoutCycles"}, context, error)
            || !parseString(object, "signalId", context, &result->signalId.value, error)
            || !parseEngineeringValue(
                object.value("limit"),
                context + ".limit",
                &result->value.engineeringLiteralValue.emplace(),
                error)) {
            return false;
        }
        result->value.source = DeviceControlValueSource::Literal;
        result->kind = DeviceControlStepKind::WaitAbsoluteAtMost;
    } else if (kind == "wait-cycles") {
        if (!checkKeys(object, {"kind", "cycles"}, context, error))
            return false;
        result->kind = DeviceControlStepKind::WaitCycles;
    } else {
        return fail(
            error,
            QString("%1.kind is unsupported; v3 forbids legacy write-signal steps").arg(context));
    }

    if (!result->signalId.value.isEmpty() && !stableV3Identifier(result->signalId.value))
        return fail(error, QString("%1.signalId must be canonical").arg(context));
    quint64 cycles = 0;
    const QString cycleKey = result->kind == DeviceControlStepKind::WaitCycles
                                 ? QString("cycles")
                                 : QString("timeoutCycles");
    if (!parseUnsigned(object, cycleKey, std::numeric_limits<quint32>::max(), context, &cycles, error)
        || cycles == 0) {
        return fail(
            error, QString("%1.%2 must be a finite non-zero cycle count").arg(context, cycleKey));
    }
    result->timeoutCycles = quint32(cycles);
    return true;
}

static bool parseV3Actions(
    const QJsonValue &value,
    const QString &context,
    QList<DeviceControlAction> *result,
    QString *error)
{
    if (!value.isArray())
        return fail(error, QString("%1 must be an array").arg(context));
    result->clear();
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        const QString itemContext = QString("%1[%2]").arg(context).arg(index);
        const QJsonValue itemValue = value.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject object = itemValue.toObject();
        if (!checkKeys(
                object,
                {"id",
                 "displayName",
                 "description",
                 "enabled",
                 "qualification",
                 "disabledReason",
                 "requiresDc",
                 "expectedSignedDefinitionSha256",
                 "signedPdoProfileIds",
                 "failureDisposition",
                 "requiredSignals",
                 "optionalSignals",
                 "parameters",
                 "consistencyGroups",
                 "steps"},
                itemContext,
                error)) {
            return false;
        }

        DeviceControlAction action;
        QString qualification;
        QString failureDisposition;
        QStringList requiredSignals;
        QStringList optionalSignals;
        if (!parseString(object, "id", itemContext, &action.id.value, error)
            || !parseString(object, "displayName", itemContext, &action.displayName, error)
            || !parseString(object, "description", itemContext, &action.description, error, true)
            || !parseBool(object, "enabled", itemContext, &action.enabled, error)
            || !parseString(object, "qualification", itemContext, &qualification, error)
            || !parseNullableString(
                object, "disabledReason", itemContext, &action.disabledReason, error)
            || !parseBool(object, "requiresDc", itemContext, &action.requiresDc, error)
            || !parseHash(
                object,
                "expectedSignedDefinitionSha256",
                itemContext,
                &action.expectedSignedDefinitionSha256,
                error)
            || !parseStringList(
                object, "signedPdoProfileIds", itemContext, &action.signedPdoProfileIds, error, true)
            || !parseString(object, "failureDisposition", itemContext, &failureDisposition, error)
            || !parseStringList(object, "requiredSignals", itemContext, &requiredSignals, error, true)
            || !parseStringList(object, "optionalSignals", itemContext, &optionalSignals, error, true)
            || !parseV3ConsistencyGroups(
                object.value("consistencyGroups"),
                itemContext + ".consistencyGroups",
                &action.consistencyGroups,
                error)) {
            return false;
        }
        if (!stableV3Identifier(action.id.value)
            || (!result->isEmpty() && action.id.value <= result->constLast().id.value)) {
            return fail(
                error, QString("%1 must use action IDs in strict canonical order").arg(context));
        }
        if (qualification == "qualified")
            action.signedQualification = DeviceControlActionQualification::Qualified;
        else if (qualification == "unqualified")
            action.signedQualification = DeviceControlActionQualification::Unqualified;
        else
            return fail(error, QString("%1.qualification is not supported").arg(itemContext));
        if (action.enabled) {
            if (action.signedQualification != DeviceControlActionQualification::Qualified
                || !action.disabledReason.isEmpty()) {
                return fail(
                    error,
                    QString("%1 enabled action must be qualified without a disabled reason")
                        .arg(itemContext));
            }
        } else if (action.disabledReason.isEmpty() || !stableV3Identifier(action.disabledReason)) {
            return fail(
                error,
                QString("%1 disabled action requires a canonical disabled reason").arg(itemContext));
        }
        const auto parsedDisposition = failureDispositionFromString(failureDisposition);
        if (!parsedDisposition) {
            return fail(error, QString("%1.failureDisposition is not supported").arg(itemContext));
        }
        action.failureDisposition = *parsedDisposition;
        if (action.signedPdoProfileIds.isEmpty()) {
            return fail(error, QString("%1.signedPdoProfileIds must not be empty").arg(itemContext));
        }
        if (std::any_of(
                action.signedPdoProfileIds.cbegin(),
                action.signedPdoProfileIds.cend(),
                [](const QString &profileId) { return !simpleV3Identifier(profileId); })) {
            return fail(
                error,
                QString("%1.signedPdoProfileIds contains an unstable identifier").arg(itemContext));
        }
        QSet<QString> signalSet;
        for (const QString &signal : std::as_const(requiredSignals)) {
            if (!stableV3Identifier(signal)) {
                return fail(
                    error,
                    QString("%1.requiredSignals contains an unstable identifier").arg(itemContext));
            }
            action.requiredSignals.append({signal});
            signalSet.insert(signal);
        }
        for (const QString &signal : std::as_const(optionalSignals)) {
            if (!stableV3Identifier(signal)) {
                return fail(
                    error,
                    QString("%1.optionalSignals contains an unstable identifier").arg(itemContext));
            }
            if (signalSet.contains(signal)) {
                return fail(
                    error,
                    QString("%1 required and optional signals must be disjoint").arg(itemContext));
            }
            action.optionalSignals.append({signal});
        }

        const QJsonValue parameters = object.value("parameters");
        if (!parameters.isArray())
            return fail(error, QString("%1.parameters must be an array").arg(itemContext));
        QSet<QString> parameterIds;
        for (qsizetype parameterIndex = 0; parameterIndex < parameters.toArray().size();
             ++parameterIndex) {
            DeviceControlActionParameter parameter;
            if (!parseActionParameter(
                    parameters.toArray().at(parameterIndex),
                    QString("%1.parameters[%2]").arg(itemContext).arg(parameterIndex),
                    PackageSchema::V3,
                    &parameter,
                    error)) {
                return false;
            }
            if (parameterIds.contains(parameter.id) || !simpleV3Identifier(parameter.id)
                || (!action.parameters.isEmpty()
                    && parameter.id <= action.parameters.constLast().id)) {
                return fail(
                    error,
                    QString("%1.parameters must use IDs in strict canonical order").arg(itemContext));
            }
            parameterIds.insert(parameter.id);
            action.parameters.append(parameter);
        }

        const QJsonValue steps = object.value("steps");
        if (!steps.isArray() || steps.toArray().isEmpty() || steps.toArray().size() > 64) {
            return fail(error, QString("%1.steps must contain one to 64 steps").arg(itemContext));
        }
        for (qsizetype stepIndex = 0; stepIndex < steps.toArray().size(); ++stepIndex) {
            DeviceControlStep step;
            if (!parseV3ActionStep(
                    steps.toArray().at(stepIndex),
                    QString("%1.steps[%2]").arg(itemContext).arg(stepIndex),
                    &step,
                    error)) {
                return false;
            }
            if (step.kind == DeviceControlStepKind::WriteGroup) {
                for (const DeviceControlGroupAssignment &assignment : step.assignments) {
                    if (assignment.value.source == DeviceControlValueSource::Parameter
                        && !parameterIds.contains(assignment.value.parameterId)) {
                        return fail(
                            error,
                            QString("%1 references an unknown action parameter").arg(itemContext));
                    }
                }
            }
            action.steps.append(step);
        }
        action.requiresExclusiveControl = true;
        action.holdToRun = false;
        result->append(action);
    }
    return true;
}

static bool parseActionStep(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    DeviceControlStep *result,
    QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"kind", "signalId", "value", "mask", "timeoutMs"}, context, error))
        return false;
    QString kind;
    if (!parseString(object, "kind", context, &kind, error)
        || !parseString(object, "signalId", context, &result->signalId.value, error, true)
        || !parseControlValue(object.value("value"), context + ".value", schema, &result->value, error)
        || !parseControlValue(object.value("mask"), context + ".mask", schema, &result->mask, error)) {
        return false;
    }
    if (schema == PackageSchema::V2 && !result->signalId.value.isEmpty()
        && !canonicalIdentifier(result->signalId.value)) {
        return fail(error, QString("%1.signalId must be canonical").arg(context));
    }
    if (kind == "write-signal")
        result->kind = DeviceControlStepKind::WriteSignal;
    else if (kind == "wait-masked-equals")
        result->kind = DeviceControlStepKind::WaitMaskedEquals;
    else if (kind == "wait-absolute-at-most")
        result->kind = DeviceControlStepKind::WaitAbsoluteAtMost;
    else if (kind == "delay")
        result->kind = DeviceControlStepKind::Delay;
    else
        return fail(error, QString("%1.kind is not supported").arg(context));
    quint64 timeoutMs = 0;
    if (!parseUnsigned(
            object, "timeoutMs", std::numeric_limits<quint32>::max(), context, &timeoutMs, error)) {
        return false;
    }
    result->timeoutMs = quint32(timeoutMs);
    if (result->kind == DeviceControlStepKind::Delay) {
        if (!result->signalId.value.isEmpty() || result->timeoutMs == 0)
            return fail(error, QString("%1 delay requires only a non-zero timeout").arg(context));
    } else if (result->signalId.value.isEmpty()) {
        return fail(error, QString("%1.signalId must not be empty").arg(context));
    }
    if (result->timeoutMs == 0)
        return fail(error, QString("%1 step must have a finite timeout").arg(context));
    if (schema == PackageSchema::V2) {
        const bool valuePresent = result->value.source != DeviceControlValueSource::Invalid;
        const bool maskPresent = result->mask.source != DeviceControlValueSource::Invalid;
        if (result->kind == DeviceControlStepKind::Delay && (valuePresent || maskPresent)) {
            return fail(error, QString("%1 delay cannot carry values").arg(context));
        }
        if (result->kind == DeviceControlStepKind::WriteSignal && (!valuePresent || maskPresent)) {
            return fail(error, QString("%1 write requires one exact value and no mask").arg(context));
        }
        if (result->kind == DeviceControlStepKind::WaitMaskedEquals
            && (!valuePresent || !maskPresent)) {
            return fail(error, QString("%1 masked wait requires exact value and mask").arg(context));
        }
        if (result->kind == DeviceControlStepKind::WaitAbsoluteAtMost
            && (!valuePresent || maskPresent)) {
            return fail(
                error,
                QString("%1 absolute wait requires one exact value and no mask").arg(context));
        }
    }
    return true;
}

static bool parseActions(
    const QJsonValue &value,
    const QString &context,
    PackageSchema schema,
    QList<DeviceControlAction> *result,
    QString *error)
{
    if (schema == PackageSchema::V3)
        return parseV3Actions(value, context, result, error);
    if (!value.isArray())
        return fail(error, QString("%1 must be an array").arg(context));
    result->clear();
    QSet<QString> actionIds;
    for (qsizetype index = 0; index < value.toArray().size(); ++index) {
        const QString itemContext = QString("%1[%2]").arg(context).arg(index);
        const QJsonValue itemValue = value.toArray().at(index);
        if (!itemValue.isObject())
            return fail(error, QString("%1 must be an object").arg(itemContext));
        const QJsonObject object = itemValue.toObject();
        const QStringList keys = schema == PackageSchema::V1
                                     ? QStringList{
                                           "id",
                                           "displayName",
                                           "description",
                                           "enabled",
                                           "requiresExclusiveControl",
                                           "requiresDc",
                                           "holdToRun",
                                           "commandTtlMs",
                                           "runtimeConditions",
                                           "failureAction",
                                           "timeoutAction",
                                           "requiredSignals",
                                           "parameters",
                                           "steps",
                                       }
                                     : QStringList{
                                           "id",
                                           "displayName",
                                           "description",
                                           "enabled",
                                           "requiresExclusiveControl",
                                           "requiresDc",
                                           "holdToRun",
                                           "commandTtlMs",
                                           "runtimeConditions",
                                           "allowedReleaseActionIds",
                                           "allowedTimeoutActionIds",
                                           "allowedFailureActionIds",
                                           "requiredSignals",
                                           "parameters",
                                           "steps",
                                       };
        if (!checkKeys(object, keys, itemContext, error)) {
            return false;
        }
        DeviceControlAction action;
        QStringList requiredSignals;
        if (!parseString(object, "id", itemContext, &action.id.value, error)
            || !parseString(object, "displayName", itemContext, &action.displayName, error)
            || !parseString(object, "description", itemContext, &action.description, error, true)
            || !parseBool(object, "enabled", itemContext, &action.enabled, error)
            || !parseBool(
                object,
                "requiresExclusiveControl",
                itemContext,
                &action.requiresExclusiveControl,
                error)
            || !parseBool(object, "requiresDc", itemContext, &action.requiresDc, error)
            || !parseBool(object, "holdToRun", itemContext, &action.holdToRun, error)
            || !parseStringList(
                object,
                "runtimeConditions",
                itemContext,
                &action.runtimeConditions,
                error,
                schema == PackageSchema::V2)
            || !parseStringList(
                object,
                "requiredSignals",
                itemContext,
                &requiredSignals,
                error,
                schema == PackageSchema::V2)) {
            return false;
        }
        if (schema == PackageSchema::V2 && !canonicalIdentifier(action.id.value))
            return fail(error, QString("%1.id must be canonical").arg(itemContext));
        quint64 commandTtlMs = 0;
        if (!parseUnsigned(
                object,
                "commandTtlMs",
                std::numeric_limits<quint32>::max(),
                itemContext,
                &commandTtlMs,
                error)) {
            return false;
        }
        action.commandTtlMs = quint32(commandTtlMs);
        if (schema == PackageSchema::V1) {
            QString failureAction;
            QString timeoutAction;
            if (!parseString(object, "failureAction", itemContext, &failureAction, error)
                || !parseString(object, "timeoutAction", itemContext, &timeoutAction, error)) {
                return false;
            }
            const auto parsedFailureAction = timeoutActionFromString(failureAction);
            const auto parsedTimeoutAction = timeoutActionFromString(timeoutAction);
            if (!parsedFailureAction || !parsedTimeoutAction)
                return fail(error, QString("%1 has an unsupported failure action").arg(itemContext));
            action.failureAction = *parsedFailureAction;
            action.timeoutAction = *parsedTimeoutAction;
        } else {
            const struct
            {
                const char *key;
                QList<SemanticActionId> *target;
            } fallbackLists[] = {
                {"allowedReleaseActionIds", &action.allowedReleaseActionIds},
                {"allowedTimeoutActionIds", &action.allowedTimeoutActionIds},
                {"allowedFailureActionIds", &action.allowedFailureActionIds},
            };
            for (const auto &fallbackList : fallbackLists) {
                QStringList values;
                if (!parseStringList(
                        object,
                        QString::fromLatin1(fallbackList.key),
                        itemContext,
                        &values,
                        error,
                        true)) {
                    return false;
                }
                for (const QString &fallbackId : std::as_const(values))
                    fallbackList.target->append({fallbackId});
            }
        }
        for (const QString &signal : std::as_const(requiredSignals))
            action.requiredSignals.append({signal});

        const QJsonValue parameters = object.value("parameters");
        if (!parameters.isArray())
            return fail(error, QString("%1.parameters must be an array").arg(itemContext));
        QSet<QString> parameterIds;
        for (qsizetype parameterIndex = 0; parameterIndex < parameters.toArray().size();
             ++parameterIndex) {
            DeviceControlActionParameter parameter;
            if (!parseActionParameter(
                    parameters.toArray().at(parameterIndex),
                    QString("%1.parameters[%2]").arg(itemContext).arg(parameterIndex),
                    schema,
                    &parameter,
                    error)) {
                return false;
            }
            if (parameterIds.contains(parameter.id))
                return fail(error, QString("%1 has duplicate parameter ids").arg(itemContext));
            if (schema == PackageSchema::V2 && !action.parameters.isEmpty()
                && parameter.id <= action.parameters.constLast().id) {
                return fail(
                    error,
                    QString("%1.parameters must be in strict parameter ID order").arg(itemContext));
            }
            parameterIds.insert(parameter.id);
            action.parameters.append(parameter);
        }
        const QJsonValue steps = object.value("steps");
        if (!steps.isArray())
            return fail(error, QString("%1.steps must be an array").arg(itemContext));
        for (qsizetype stepIndex = 0; stepIndex < steps.toArray().size(); ++stepIndex) {
            DeviceControlStep step;
            if (!parseActionStep(
                    steps.toArray().at(stepIndex),
                    QString("%1.steps[%2]").arg(itemContext).arg(stepIndex),
                    schema,
                    &step,
                    error)) {
                return false;
            }
            if ((step.value.source == DeviceControlValueSource::Parameter
                 && !parameterIds.contains(step.value.parameterId))
                || (step.mask.source == DeviceControlValueSource::Parameter
                    && !parameterIds.contains(step.mask.parameterId))) {
                return fail(
                    error, QString("%1 references an unknown action parameter").arg(itemContext));
            }
            action.steps.append(step);
        }
        if (actionIds.contains(action.id.value))
            return fail(error, QString("%1 contains duplicate action ids").arg(context));
        actionIds.insert(action.id.value);
        if (schema == PackageSchema::V2 && !result->isEmpty()
            && action.id.value <= result->constLast().id.value) {
            return fail(error, QString("%1 must be in strict action ID order").arg(context));
        }
        result->append(action);
    }
    return true;
}

static std::optional<EngineeringValueKind> rawKind(EtherCATDataType dataType)
{
    switch (dataType) {
    case EtherCATDataType::Boolean:
        return EngineeringValueKind::Boolean;
    case EtherCATDataType::Integer8:
    case EtherCATDataType::Integer16:
    case EtherCATDataType::Integer32:
    case EtherCATDataType::Integer64:
        return EngineeringValueKind::SignedInteger;
    case EtherCATDataType::UnsignedInteger8:
    case EtherCATDataType::UnsignedInteger16:
    case EtherCATDataType::UnsignedInteger32:
    case EtherCATDataType::UnsignedInteger64:
        return EngineeringValueKind::UnsignedInteger;
    case EtherCATDataType::Unknown:
    case EtherCATDataType::Real32:
    case EtherCATDataType::Real64:
    case EtherCATDataType::VisibleString:
    case EtherCATDataType::OctetString:
        return std::nullopt;
    }
    return std::nullopt;
}

static const SemanticSignalDefinition *signalForId(
    const DeviceAdapterManifest &manifest, const SemanticSignalId &id)
{
    const auto topLevel = std::find_if(
        manifest.semanticSignals.cbegin(),
        manifest.semanticSignals.cend(),
        [&id](const SemanticSignalDefinition &signal) { return signal.id == id; });
    if (topLevel != manifest.semanticSignals.cend())
        return &*topLevel;
    const SemanticSignalDefinition *match = nullptr;
    for (const DeviceModuleProfile &module : manifest.moduleProfiles) {
        const auto found = std::find_if(
            module.slotRelativeSignals.cbegin(),
            module.slotRelativeSignals.cend(),
            [&id](const SemanticSignalDefinition &signal) { return signal.id == id; });
        if (found == module.slotRelativeSignals.cend())
            continue;
        if (match)
            return nullptr;
        match = &*found;
    }
    return match;
}

static bool valueConvertsForAllBindings(
    const SemanticSignalDefinition &signal,
    const EngineeringValue &value,
    const QString &context,
    QString *error)
{
    if (!signal.engineeringTransform
        || signal.engineeringTransform->rounding != EngineeringRounding::RejectInexact) {
        return fail(error, QString("%1 lacks an exact engineering transform").arg(context));
    }
    const Core::EngineeringContractValidation constrained
        = Core::validateEngineeringValueAgainstConstraint(
            value, signal.engineeringTransform->constraint);
    if (!constrained.accepted())
        return fail(error, QString("%1: %2").arg(context, constrained.detail));
    for (const DeviceSignalBinding &binding : signal.bindings) {
        const std::optional<EngineeringValueKind> kind = rawKind(binding.physicalType);
        if (!kind || binding.bitWidth < 1 || binding.bitWidth > 64) {
            return fail(error, QString("%1 uses an unsupported writable raw format").arg(context));
        }
        const Core::EngineeringConversionResult converted = Core::convertEngineeringToRaw(
            value, *kind, quint32(binding.bitWidth), *signal.engineeringTransform);
        if (!converted.validation.accepted()) {
            return fail(
                error,
                QString("%1 is not exactly representable: %2")
                    .arg(context, converted.validation.detail));
        }
    }
    return true;
}

static bool parameterDomainConvertsForAllBindings(
    const DeviceControlActionParameter &parameter,
    const SemanticSignalDefinition &signal,
    const QString &context,
    QString *error)
{
    if (!parameter.engineeringConstraint || !signal.engineeringTransform) {
        return fail(error, QString("%1 lacks an exact parameter or signal contract").arg(context));
    }
    const EngineeringConstraint &constraint = *parameter.engineeringConstraint;
    if (!constraint.minimum || !constraint.maximum || !constraint.step || !constraint.stepOrigin) {
        return fail(
            error, QString("%1 requires finite boundaries, a step, and a step origin").arg(context));
    }

    const auto validateBoundary = [&](const ExactRational &boundary, const QString &name) {
        const EngineeringValue value = EngineeringValue::fromExactRational(boundary);
        const Core::EngineeringContractValidation ownConstraint
            = Core::validateEngineeringValueAgainstConstraint(value, constraint);
        if (!ownConstraint.accepted()) {
            return fail(error, QString("%1 %2: %3").arg(context, name, ownConstraint.detail));
        }
        return valueConvertsForAllBindings(signal, value, QString("%1 %2").arg(context, name), error);
    };
    if (!validateBoundary(*constraint.minimum, "minimum")
        || !validateBoundary(*constraint.maximum, "maximum")) {
        return false;
    }

    const __int128 rawStepNumerator = __int128(constraint.step->numerator)
                                      * signal.engineeringTransform->scale.denominator;
    const __int128 rawStepDenominator = __int128(constraint.step->denominator)
                                        * signal.engineeringTransform->scale.numerator;
    if (rawStepDenominator == 0 || rawStepNumerator % rawStepDenominator != 0) {
        return fail(
            error, QString("%1 step is not exactly encodable by the target signal").arg(context));
    }

    if (parameter.engineeringDefaultValue
        && !valueConvertsForAllBindings(
            signal, *parameter.engineeringDefaultValue, QString("%1 default").arg(context), error)) {
        return false;
    }
    for (const EngineeringEnumerationValue &entry : constraint.enumeration) {
        if (!valueConvertsForAllBindings(
                signal,
                EngineeringValue::fromEnumeration(entry.id),
                QString("%1 enumeration \"%2\"").arg(context, entry.id),
                error)) {
            return false;
        }
    }
    return true;
}

static bool constraintSafelyWithin(
    const EngineeringConstraint &candidate,
    const EngineeringConstraint &allowed,
    const QString &context,
    QString *error)
{
    const Core::EngineeringContractValidation validation = Core::validateEngineeringConstraint(
        candidate);
    if (!validation.accepted())
        return fail(error, QString("%1: %2").arg(context, validation.detail));
    if (!candidate.minimum || !candidate.maximum) {
        return fail(error, QString("%1 must declare finite minimum and maximum values").arg(context));
    }
    for (const ExactRational *boundary : {&*candidate.minimum, &*candidate.maximum}) {
        const Core::EngineeringContractValidation boundaryValidation
            = Core::validateEngineeringValueAgainstConstraint(
                EngineeringValue::fromExactRational(*boundary), allowed);
        if (!boundaryValidation.accepted()) {
            return fail(
                error,
                QString("%1 exceeds the target signal constraint: %2")
                    .arg(context, boundaryValidation.detail));
        }
    }
    if (allowed.step
        && (candidate.step != allowed.step || candidate.stepOrigin != allowed.stepOrigin)) {
        return fail(
            error, QString("%1 must retain the exact target signal step contract").arg(context));
    }
    if (!allowed.enumeration.isEmpty()) {
        if (candidate.enumeration.isEmpty())
            return fail(error, QString("%1 omits the target signal enumeration").arg(context));
        for (const EngineeringEnumerationValue &entry : candidate.enumeration) {
            const auto found = std::find_if(
                allowed.enumeration.cbegin(),
                allowed.enumeration.cend(),
                [&entry](const EngineeringEnumerationValue &allowedEntry) {
                    return allowedEntry.id == entry.id && allowedEntry.value == entry.value;
                });
            if (found == allowed.enumeration.cend()) {
                return fail(
                    error, QString("%1 introduces an unsigned enumeration value").arg(context));
            }
        }
    } else if (!candidate.enumeration.isEmpty()) {
        return fail(error, QString("%1 introduces an unsupported enumeration").arg(context));
    }
    return true;
}

static bool validateV3Manifest(const DeviceAdapterManifest &manifest, QString *error)
{
    if (!stableV3Identifier(manifest.id.value))
        return fail(error, "v3 adapter ID must be canonical");

    QHash<QString, const ProcessDataProfile *> signedProfiles;
    for (const ProcessDataProfile &profile : manifest.processDataProfiles) {
        if (!simpleV3Identifier(profile.signedPdoProfileId)
            || signedProfiles.contains(profile.signedPdoProfileId)) {
            return fail(error, "v3 signed PDO profile IDs must be canonical and unique");
        }
        signedProfiles.insert(profile.signedPdoProfileId, &profile);
    }

    QSet<QString> signalDefinitionIds;
    const auto addSignalDefinition = [&](const SemanticSignalDefinition &signal) {
        if (signalDefinitionIds.contains(signal.id.value)) {
            return fail(
                error,
                QString("duplicate v3 semantic signal definition \"%1\"").arg(signal.id.value));
        }
        signalDefinitionIds.insert(signal.id.value);
        return true;
    };
    for (const SemanticSignalDefinition &signal : manifest.semanticSignals) {
        if (!addSignalDefinition(signal))
            return false;
    }
    for (const DeviceModuleProfile &module : manifest.moduleProfiles) {
        for (const SemanticSignalDefinition &signal : module.slotRelativeSignals) {
            if (!addSignalDefinition(signal))
                return false;
        }
    }

    for (const SemanticSignalDefinition &signal : manifest.semanticSignals) {
        if (signal.engineeringSafeValue
            && !valueConvertsForAllBindings(
                signal,
                *signal.engineeringSafeValue,
                QString("signal \"%1\" safe value").arg(signal.id.value),
                error)) {
            return false;
        }
        if (signal.access == SemanticSignalAccess::ReadOnly && signal.engineeringSafeValue) {
            return fail(
                error,
                QString("read-only signal \"%1\" cannot declare a write safe value")
                    .arg(signal.id.value));
        }
    }
    for (const DeviceModuleProfile &module : manifest.moduleProfiles) {
        for (const SemanticSignalDefinition &signal : module.slotRelativeSignals) {
            if (signal.engineeringSafeValue
                && !valueConvertsForAllBindings(
                    signal,
                    *signal.engineeringSafeValue,
                    QString("signal \"%1\" safe value").arg(signal.id.value),
                    error)) {
                return false;
            }
            if (signal.access == SemanticSignalAccess::ReadOnly && signal.engineeringSafeValue) {
                return fail(
                    error,
                    QString("read-only signal \"%1\" cannot declare a write safe value")
                        .arg(signal.id.value));
            }
        }
    }

    for (const DeviceControlAction &action : manifest.controlActions) {
        if (action.expectedSignedDefinitionSha256.size() != 32
            || std::all_of(
                action.expectedSignedDefinitionSha256.cbegin(),
                action.expectedSignedDefinitionSha256.cend(),
                [](char byte) { return byte == 0; })) {
            return fail(
                error,
                QString("action \"%1\" has an invalid signed definition digest")
                    .arg(action.id.value));
        }
        for (const QString &profileId : action.signedPdoProfileIds) {
            if (!signedProfiles.contains(profileId)) {
                return fail(
                    error,
                    QString("action \"%1\" references unknown signed PDO profile \"%2\"")
                        .arg(action.id.value, profileId));
            }
        }

        QSet<QString> requiredSignals;
        for (const SemanticSignalId &signalId : action.requiredSignals) {
            if (!signalForId(manifest, signalId)) {
                return fail(
                    error,
                    QString("action \"%1\" references unknown signal \"%2\"")
                        .arg(action.id.value, signalId.value));
            }
            requiredSignals.insert(signalId.value);
        }
        for (const QString &profileId : action.signedPdoProfileIds) {
            QSet<QString> profileSignals;
            for (const SemanticSignalId &signalId : signedProfiles.value(profileId)->requiredSignals)
                profileSignals.insert(signalId.value);
            if (!std::all_of(
                    requiredSignals.cbegin(),
                    requiredSignals.cend(),
                    [&profileSignals](const QString &signalId) {
                        return profileSignals.contains(signalId);
                    })) {
                return fail(
                    error,
                    QString(
                        "action \"%1\" required signals are not completely covered by signed "
                        "PDO profile \"%2\"")
                        .arg(action.id.value, profileId));
            }
        }
        for (const SemanticSignalId &signalId : action.optionalSignals) {
            if (!signalForId(manifest, signalId)) {
                return fail(
                    error,
                    QString("action \"%1\" references unknown optional signal \"%2\"")
                        .arg(action.id.value, signalId.value));
            }
        }

        QHash<QString, const DeviceControlActionParameter *> parameters;
        for (const DeviceControlActionParameter &parameter : action.parameters)
            parameters.insert(parameter.id, &parameter);

        QHash<QString, const DeviceControlConsistencyGroup *> groups;
        QSet<QString> groupedSignals;
        for (const DeviceControlConsistencyGroup &group : action.consistencyGroups) {
            if (groups.contains(group.id) || group.maximumTtlCycles < 1
                || group.maximumTtlCycles > std::numeric_limits<quint16>::max()
                || group.recovery == DeviceControlGroupRecovery::Invalid) {
                return fail(
                    error,
                    QString("action \"%1\" has an invalid consistency group").arg(action.id.value));
            }
            groups.insert(group.id, &group);
            for (const SemanticSignalId &member : group.members) {
                const SemanticSignalDefinition *signal = signalForId(manifest, member);
                if (!signal || groupedSignals.contains(member.value)
                    || signal->direction != SemanticSignalDirection::Output
                    || signal->access != SemanticSignalAccess::ReadWrite
                    || !signal->engineeringSafeValue
                    || !std::all_of(
                        signal->bindings.cbegin(),
                        signal->bindings.cend(),
                        [](const DeviceSignalBinding &binding) {
                            return binding.kind == DeviceSignalBindingKind::ProcessDataObject
                                   && binding.pdoDirection == PdoDirection::Rx;
                        })) {
                    return fail(
                        error,
                        QString(
                            "action \"%1\" group \"%2\" has an unsafe or duplicate member "
                            "\"%3\"")
                            .arg(action.id.value, group.id, member.value));
                }
                groupedSignals.insert(member.value);
                if (!requiredSignals.contains(member.value)) {
                    return fail(
                        error,
                        QString("action \"%1\" group member \"%2\" is not required")
                            .arg(action.id.value, member.value));
                }
            }
        }

        QSet<QString> referencedSignals;
        QSet<QString> referencedGroups;
        QSet<QString> referencedParameters;
        for (const DeviceControlStep &step : action.steps) {
            if (step.kind == DeviceControlStepKind::WriteSignal) {
                return fail(
                    error,
                    QString("action \"%1\" uses forbidden legacy WriteSignal").arg(action.id.value));
            }
            if (step.kind == DeviceControlStepKind::WriteGroup) {
                const DeviceControlConsistencyGroup *group
                    = groups.value(step.consistencyGroupId, nullptr);
                if (!group || step.timeoutMs || step.timeoutCycles
                    || step.assignments.size() != group->members.size()) {
                    return fail(
                        error,
                        QString("action \"%1\" has an invalid or partial WriteGroup")
                            .arg(action.id.value));
                }
                referencedGroups.insert(group->id);
                for (qsizetype index = 0; index < step.assignments.size(); ++index) {
                    const DeviceControlGroupAssignment &assignment = step.assignments.at(index);
                    if (assignment.signalId != group->members.at(index)) {
                        return fail(
                            error,
                            QString(
                                "action \"%1\" WriteGroup does not cover its complete "
                                "canonical member set")
                                .arg(action.id.value));
                    }
                    const SemanticSignalDefinition *signal
                        = signalForId(manifest, assignment.signalId);
                    if (!signal)
                        return fail(error, "validated WriteGroup signal disappeared");
                    if (assignment.value.source == DeviceControlValueSource::Literal) {
                        if (!assignment.value.engineeringLiteralValue
                            || !valueConvertsForAllBindings(
                                *signal,
                                *assignment.value.engineeringLiteralValue,
                                QString("action \"%1\" assignment \"%2\"")
                                    .arg(action.id.value, assignment.signalId.value),
                                error)) {
                            return false;
                        }
                    } else if (assignment.value.source == DeviceControlValueSource::Parameter) {
                        const DeviceControlActionParameter *parameter
                            = parameters.value(assignment.value.parameterId, nullptr);
                        if (!parameter || !parameter->engineeringConstraint
                            || !signal->engineeringTransform
                            || !constraintSafelyWithin(
                                *parameter->engineeringConstraint,
                                signal->engineeringTransform->constraint,
                                QString("action \"%1\" parameter \"%2\"")
                                    .arg(action.id.value, assignment.value.parameterId),
                                error)) {
                            return false;
                        }
                        if (!std::all_of(
                                signal->bindings.cbegin(),
                                signal->bindings.cend(),
                                [parameter](const DeviceSignalBinding &binding) {
                                    return binding.physicalType == parameter->dataType;
                                })) {
                            return fail(
                                error,
                                QString(
                                    "action \"%1\" parameter \"%2\" has a mismatched raw "
                                    "type")
                                    .arg(action.id.value, assignment.value.parameterId));
                        }
                        if (!parameterDomainConvertsForAllBindings(
                                *parameter,
                                *signal,
                                QString("action \"%1\" parameter \"%2\"")
                                    .arg(action.id.value, assignment.value.parameterId),
                                error)) {
                            return false;
                        }
                        referencedParameters.insert(assignment.value.parameterId);
                    } else {
                        return fail(error, "v3 assignment has an invalid value source");
                    }
                    referencedSignals.insert(assignment.signalId.value);
                }
                continue;
            }
            if (step.kind == DeviceControlStepKind::WaitCycles) {
                if (!step.timeoutCycles || step.timeoutMs || !step.signalId.value.isEmpty()
                    || step.value.source != DeviceControlValueSource::Invalid
                    || step.mask.source != DeviceControlValueSource::Invalid) {
                    return fail(
                        error,
                        QString("action \"%1\" mixes cycle and legacy timeout fields")
                            .arg(action.id.value));
                }
                continue;
            }
            if (step.kind != DeviceControlStepKind::WaitMaskedEquals
                && step.kind != DeviceControlStepKind::WaitAbsoluteAtMost) {
                return fail(
                    error,
                    QString("action \"%1\" contains an unsupported v3 step").arg(action.id.value));
            }
            if (!step.timeoutCycles || step.timeoutMs) {
                return fail(
                    error,
                    QString("action \"%1\" wait must use only timeoutCycles").arg(action.id.value));
            }
            const SemanticSignalDefinition *signal = signalForId(manifest, step.signalId);
            if (!signal || !signal->engineeringTransform || !step.value.engineeringLiteralValue) {
                return fail(
                    error,
                    QString("action \"%1\" wait references an inexact signal").arg(action.id.value));
            }
            if (!valueConvertsForAllBindings(
                    *signal,
                    *step.value.engineeringLiteralValue,
                    QString("action \"%1\" wait value").arg(action.id.value),
                    error)) {
                return false;
            }
            if (step.kind == DeviceControlStepKind::WaitMaskedEquals) {
                if (!step.mask.engineeringLiteralValue
                    || step.value.engineeringLiteralValue->kind
                           != EngineeringValueKind::UnsignedInteger
                    || step.mask.engineeringLiteralValue->kind
                           != EngineeringValueKind::UnsignedInteger
                    || step.mask.engineeringLiteralValue->unsignedInteger == 0
                    || (step.value.engineeringLiteralValue->unsignedInteger
                        & ~step.mask.engineeringLiteralValue->unsignedInteger)) {
                    return fail(
                        error,
                        QString("action \"%1\" masked wait has an unsafe mask or value")
                            .arg(action.id.value));
                }
                const quint64 mask = step.mask.engineeringLiteralValue->unsignedInteger;
                for (const DeviceSignalBinding &binding : signal->bindings) {
                    if (binding.bitWidth < 1 || binding.bitWidth > 64
                        || (binding.bitWidth < 64 && (mask >> binding.bitWidth))) {
                        return fail(
                            error,
                            QString("action \"%1\" masked wait exceeds the signal width")
                                .arg(action.id.value));
                    }
                }
            } else if (step.mask.source != DeviceControlValueSource::Invalid) {
                return fail(
                    error,
                    QString("action \"%1\" absolute wait cannot carry a mask").arg(action.id.value));
            }
            referencedSignals.insert(step.signalId.value);
        }
        if (referencedGroups.size() != groups.size()) {
            return fail(
                error,
                QString("action \"%1\" declares an unused consistency group").arg(action.id.value));
        }
        if (referencedParameters.size() != parameters.size()) {
            return fail(
                error, QString("action \"%1\" declares an unused parameter").arg(action.id.value));
        }
        if (referencedSignals != requiredSignals) {
            return fail(
                error,
                QString(
                    "action \"%1\" required signals do not equal its complete step "
                    "reference set")
                    .arg(action.id.value));
        }
    }
    return true;
}

static bool validateManifest(DeviceAdapterManifest *manifest, PackageSchema schema, QString *error)
{
    if (!isValidDeviceAdapterContractVersion(manifest->contractVersion)
        || manifest->contractVersion != contractVersion(schema)) {
        return fail(error, "adapter contract version does not match the parsed schema");
    }
    if (manifest->match.vendorId == 0 || manifest->match.productCode == 0
        || manifest->match.minimumRevision > manifest->match.maximumRevision) {
        return fail(error, "match identity or revision interval is invalid");
    }
    if (manifest->provenance.sourceSha256 != manifest->match.exactEsiSha256)
        return fail(error, "source SHA-256 does not match the exact ESI SHA-256");
    if (manifest->signatureVerified || manifest->realHardwareAllowed) {
        return fail(
            error,
            "embedded trust assertions are not accepted without independent signature "
            "verification");
    }
    if (manifest->qualification == DeviceAdapterQualification::Candidate) {
        const auto writableSignal = [](const SemanticSignalDefinition &signal) {
            return signal.manualControl.allowed;
        };
        if (std::any_of(
                manifest->semanticSignals.cbegin(),
                manifest->semanticSignals.cend(),
                writableSignal)) {
            return fail(error, "Candidate package exposes an enabled manual signal");
        }
        for (const DeviceModuleProfile &module : std::as_const(manifest->moduleProfiles)) {
            if (std::any_of(
                    module.slotRelativeSignals.cbegin(),
                    module.slotRelativeSignals.cend(),
                    writableSignal)) {
                return fail(error, "Candidate package exposes an enabled module manual signal");
            }
        }
        if (std::any_of(
                manifest->controlActions.cbegin(),
                manifest->controlActions.cend(),
                [](const DeviceControlAction &action) { return action.enabled; })) {
            return fail(error, "Candidate package exposes an enabled control action");
        }
    }

    QSet<QString> signalIds;
    for (const SemanticSignalDefinition &signal : std::as_const(manifest->semanticSignals))
        signalIds.insert(signal.id.value);
    for (const DeviceModuleProfile &module : std::as_const(manifest->moduleProfiles)) {
        for (const SemanticSignalDefinition &signal : module.slotRelativeSignals)
            signalIds.insert(signal.id.value);
    }
    for (const ProcessDataProfile &profile : std::as_const(manifest->processDataProfiles)) {
        for (const SemanticSignalId &requiredSignal : profile.requiredSignals) {
            if (!signalIds.contains(requiredSignal.value)) {
                return fail(
                    error,
                    QString("profile \"%1\" references unknown signal \"%2\"")
                        .arg(profile.id, requiredSignal.value));
            }
        }
    }
    for (const DeviceControlAction &action : std::as_const(manifest->controlActions)) {
        if (schema != PackageSchema::V3 && action.enabled && action.commandTtlMs == 0) {
            return fail(
                error,
                QString("enabled action \"%1\" must declare a non-zero command TTL")
                    .arg(action.id.value));
        }
        for (const SemanticSignalId &requiredSignal : action.requiredSignals) {
            if (!signalIds.contains(requiredSignal.value)) {
                return fail(
                    error,
                    QString("action \"%1\" references unknown signal \"%2\"")
                        .arg(action.id.value, requiredSignal.value));
            }
        }
        for (const DeviceControlStep &step : action.steps) {
            if (!step.signalId.value.isEmpty() && !signalIds.contains(step.signalId.value)) {
                return fail(
                    error,
                    QString("action \"%1\" step references unknown signal \"%2\"")
                        .arg(action.id.value, step.signalId.value));
            }
        }
    }
    if (schema == PackageSchema::V2) {
        if (!canonicalIdentifier(manifest->id.value))
            return fail(error, "v2 adapter ID must be canonical");

        const auto signalForId = [manifest](const SemanticSignalId &id) {
            const auto found = std::find_if(
                manifest->semanticSignals.cbegin(),
                manifest->semanticSignals.cend(),
                [&id](const SemanticSignalDefinition &signal) { return signal.id == id; });
            return found == manifest->semanticSignals.cend() ? nullptr : &*found;
        };
        QSet<QString> actionIds;
        for (const DeviceControlAction &action : std::as_const(manifest->controlActions))
            actionIds.insert(action.id.value);
        for (const DeviceControlAction &action : std::as_const(manifest->controlActions)) {
            const QList<QList<SemanticActionId>> fallbackLists{
                action.allowedReleaseActionIds,
                action.allowedTimeoutActionIds,
                action.allowedFailureActionIds,
            };
            for (const QList<SemanticActionId> &fallbackList : fallbackLists) {
                for (const SemanticActionId &fallback : fallbackList) {
                    if (fallback == action.id || !actionIds.contains(fallback.value)) {
                        return fail(
                            error,
                            QString(
                                "action \"%1\" has a self-referential or unknown fallback \"%2\"")
                                .arg(action.id.value, fallback.value));
                    }
                }
            }
            if (action.enabled
                && (action.allowedReleaseActionIds.isEmpty()
                    || action.allowedTimeoutActionIds.isEmpty()
                    || action.allowedFailureActionIds.isEmpty())) {
                return fail(
                    error,
                    QString("enabled v2 action \"%1\" requires explicit fallback allow-lists")
                        .arg(action.id.value));
            }
            for (const DeviceControlStep &step : action.steps) {
                if (step.kind == DeviceControlStepKind::Delay)
                    continue;
                const SemanticSignalDefinition *signal = signalForId(step.signalId);
                if (!signal || !signal->engineeringTransform) {
                    return fail(
                        error,
                        QString("action \"%1\" lacks an exact signal definition for \"%2\"")
                            .arg(action.id.value, step.signalId.value));
                }
                if (step.value.source == DeviceControlValueSource::Literal) {
                    const Core::EngineeringContractValidation validation
                        = Core::validateEngineeringValueAgainstConstraint(
                            *step.value.engineeringLiteralValue,
                            signal->engineeringTransform->constraint);
                    if (!validation.accepted()) {
                        return fail(
                            error,
                            QString("action \"%1\" value for \"%2\" is invalid: %3")
                                .arg(action.id.value, step.signalId.value, validation.detail));
                    }
                }
                if (step.mask.source == DeviceControlValueSource::Literal) {
                    const Core::EngineeringContractValidation validation
                        = Core::validateEngineeringValue(*step.mask.engineeringLiteralValue);
                    if (!validation.accepted()) {
                        return fail(
                            error,
                            QString("action \"%1\" mask for \"%2\" is invalid: %3")
                                .arg(action.id.value, step.signalId.value, validation.detail));
                    }
                }
            }
        }
    }
    if (schema == PackageSchema::V3 && !validateV3Manifest(*manifest, error))
        return false;
    return true;
}

static std::optional<Package> parsePackage(
    const Utils::FilePath &sourcePath, const QByteArray &contents, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(
            error,
            QString("%1: invalid JSON object: %2")
                .arg(sourcePath.toUserOutput(), parseError.errorString()));
        return std::nullopt;
    }
    const QString context = sourcePath.fileName();
    const QJsonObject object = document.object();
    QString parsedSchema;
    if (!parseString(object, "schemaVersion", context, &parsedSchema, error))
        return std::nullopt;
    PackageSchema schema;
    if (parsedSchema == schemaVersionV1)
        schema = PackageSchema::V1;
    else if (parsedSchema == schemaVersionV2)
        schema = PackageSchema::V2;
    else if (parsedSchema == schemaVersionV3)
        schema = PackageSchema::V3;
    else {
        fail(
            error,
            QString("%1.schemaVersion must equal \"%2\", \"%3\", or \"%4\"")
                .arg(
                    context,
                    QString::fromLatin1(schemaVersionV1),
                    QString::fromLatin1(schemaVersionV2),
                    QString::fromLatin1(schemaVersionV3)));
        return std::nullopt;
    }
    QStringList rootKeys{"schemaVersion",
                         "id",
                         "version",
                         "displayName",
                         "description",
                         "qualification",
                         "matchPriority",
                         "match",
                         "capabilities",
                         "source",
                         "evidenceSha256",
                         "signatureVerified",
                         "realHardwareAllowed",
                         "signals",
                         "processDataProfiles",
                         "moduleProfiles",
                         "controlActions"};
    if (schema == PackageSchema::V3)
        rootKeys.append("controllerAdapterTarget");
    if (!checkKeys(object, rootKeys, context, error)) {
        return std::nullopt;
    }

    Package package;
    DeviceAdapterManifest &manifest = package.manifest;
    manifest.contractVersion = contractVersion(schema);
    if (!parseString(object, "id", context, &manifest.id.value, error)
        || !parseString(object, "version", context, &manifest.version, error)
        || !parseString(object, "displayName", context, &manifest.displayName, error)
        || !parseString(object, "description", context, &manifest.description, error, true)) {
        return std::nullopt;
    }

    QString qualification;
    if (!parseString(object, "qualification", context, &qualification, error))
        return std::nullopt;
    const auto parsedQualification = qualificationFromString(qualification);
    if (!parsedQualification) {
        fail(error, QString("%1.qualification is not supported").arg(context));
        return std::nullopt;
    }
    manifest.qualification = *parsedQualification;
    quint64 matchPriority = 0;
    if (!parseUnsigned(
            object,
            "matchPriority",
            std::numeric_limits<int>::max(),
            context,
            &matchPriority,
            error)) {
        return std::nullopt;
    }
    manifest.matchPriority = int(matchPriority);

    const QJsonValue matchValue = object.value("match");
    if (!matchValue.isObject()) {
        fail(error, QString("%1.match must be an object").arg(context));
        return std::nullopt;
    }
    const QJsonObject match = matchValue.toObject();
    if (!checkKeys(
            match,
            {"vendorId", "productCode", "minimumRevision", "maximumRevision", "exactEsiSha256"},
            context + ".match",
            error)) {
        return std::nullopt;
    }
    quint64 vendorId = 0;
    quint64 productCode = 0;
    quint64 minimumRevision = 0;
    quint64 maximumRevision = 0;
    if (!parseUnsigned(
            match,
            "vendorId",
            std::numeric_limits<quint32>::max(),
            context + ".match",
            &vendorId,
            error)
        || !parseUnsigned(
            match,
            "productCode",
            std::numeric_limits<quint32>::max(),
            context + ".match",
            &productCode,
            error)
        || !parseUnsigned(
            match,
            "minimumRevision",
            std::numeric_limits<quint32>::max(),
            context + ".match",
            &minimumRevision,
            error)
        || !parseUnsigned(
            match,
            "maximumRevision",
            std::numeric_limits<quint32>::max(),
            context + ".match",
            &maximumRevision,
            error)
        || !parseHash(
            match, "exactEsiSha256", context + ".match", &manifest.match.exactEsiSha256, error)) {
        return std::nullopt;
    }
    manifest.match.vendorId = quint32(vendorId);
    manifest.match.productCode = quint32(productCode);
    manifest.match.minimumRevision = quint32(minimumRevision);
    manifest.match.maximumRevision = quint32(maximumRevision);

    if (schema == PackageSchema::V3) {
        const QString targetContext = context + ".controllerAdapterTarget";
        const QJsonValue targetValue = object.value("controllerAdapterTarget");
        if (!targetValue.isObject()) {
            fail(error, QString("%1 must be an object").arg(targetContext));
            return std::nullopt;
        }
        const QJsonObject target = targetValue.toObject();
        if (!checkKeys(
                target,
                {"adapterId", "adapterVersion", "adapterSha256", "esiSha256"},
                targetContext,
                error)
            || !parseString(
                target,
                "adapterId",
                targetContext,
                &manifest.controllerAdapterTarget.adapterId,
                error)
            || !parseString(
                target,
                "adapterVersion",
                targetContext,
                &manifest.controllerAdapterTarget.adapterVersion,
                error)
            || !parseHash(
                target,
                "adapterSha256",
                targetContext,
                &manifest.controllerAdapterTarget.adapterSha256,
                error)
            || !parseHash(
                target,
                "esiSha256",
                targetContext,
                &manifest.controllerAdapterTarget.esiSha256,
                error)) {
            return std::nullopt;
        }
        const DeviceAdapterControllerTarget &controllerTarget
            = manifest.controllerAdapterTarget;
        if (!simpleV3Identifier(controllerTarget.adapterId)
            || !canonicalIdentifier(controllerTarget.adapterVersion)) {
            fail(error, QString("%1 identity must be canonical").arg(targetContext));
            return std::nullopt;
        }
        if (std::all_of(
                controllerTarget.adapterSha256.cbegin(),
                controllerTarget.adapterSha256.cend(),
                [](char byte) { return byte == 0; })) {
            fail(error, QString("%1.adapterSha256 must be non-zero").arg(targetContext));
            return std::nullopt;
        }
        if (controllerTarget.esiSha256 != manifest.match.exactEsiSha256) {
            fail(
                error,
                QString("%1.esiSha256 does not match the exact ESI identity").arg(targetContext));
            return std::nullopt;
        }
    }

    if (!parseCapabilityList(
            object, "capabilities", context, &manifest.capabilities, error, exactSchema(schema))) {
        return std::nullopt;
    }
    const QJsonValue sourceValue = object.value("source");
    if (!sourceValue.isObject()) {
        fail(error, QString("%1.source must be an object").arg(context));
        return std::nullopt;
    }
    const QJsonObject source = sourceValue.toObject();
    if (!checkKeys(source, {"id", "version", "location", "sha256"}, context + ".source", error)
        || !parseString(source, "id", context + ".source", &manifest.provenance.sourceId, error)
        || !parseString(
            source, "version", context + ".source", &manifest.provenance.sourceVersion, error)
        || !parseString(
            source, "location", context + ".source", &manifest.provenance.sourceLocation, error)
        || !parseHash(source, "sha256", context + ".source", &manifest.provenance.sourceSha256, error)
        || !parseHash(object, "evidenceSha256", context, &manifest.evidenceSha256, error, true)
        || !parseBool(object, "signatureVerified", context, &manifest.signatureVerified, error)
        || !parseBool(object, "realHardwareAllowed", context, &manifest.realHardwareAllowed, error)
        || !parseSignals(
            object.value("signals"), context + ".signals", schema, &manifest.semanticSignals, error)
        || !parseProfiles(
            object.value("processDataProfiles"),
            context + ".processDataProfiles",
            schema,
            &manifest.processDataProfiles,
            error)
        || !parseModules(
            object.value("moduleProfiles"),
            context + ".moduleProfiles",
            schema,
            &manifest.moduleProfiles,
            error)
        || !parseActions(
            object.value("controlActions"),
            context + ".controlActions",
            schema,
            &manifest.controlActions,
            error)) {
        return std::nullopt;
    }
    if (schema == PackageSchema::V3
        && manifest.controllerAdapterTarget.esiSha256 != manifest.provenance.sourceSha256) {
        fail(
            error,
            QString("%1.controllerAdapterTarget.esiSha256 does not match source.sha256")
                .arg(context));
        return std::nullopt;
    }
    if (schema == PackageSchema::V1) {
        manifest.contentSha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
    } else if (schema == PackageSchema::V2) {
        manifest.contentSha256 = canonicalContentSha256V2(object, context, error);
        if (manifest.contentSha256.isEmpty())
            return std::nullopt;
    } else {
        manifest.contentSha256 = canonicalContentSha256V3(object, context, error);
        if (manifest.contentSha256.isEmpty())
            return std::nullopt;
    }
    package.sourcePath = sourcePath;
    if (!validateManifest(&manifest, schema, error))
        return std::nullopt;
    return package;
}

static bool packageLess(const Package &left, const Package &right)
{
    if (left.manifest.matchPriority != right.manifest.matchPriority)
        return left.manifest.matchPriority > right.manifest.matchPriority;
    if (left.manifest.id.value != right.manifest.id.value)
        return left.manifest.id.value < right.manifest.id.value;
    return left.manifest.version < right.manifest.version;
}

static bool identityMatches(const DeviceAdapterManifest &manifest, const DeviceDescription &device)
{
    const DeviceIdentity &identity = device.summary.identity;
    return manifest.match.vendorId == identity.vendorId
           && manifest.match.productCode == identity.productCode
           && identity.revisionNumber >= manifest.match.minimumRevision
           && identity.revisionNumber <= manifest.match.maximumRevision
           && manifest.match.exactEsiSha256 == device.sourceSha256;
}

static QSet<quint16> pdoIndices(const ProcessImageDirection &direction)
{
    QSet<quint16> result;
    for (const ProcessImageEntry &entry : direction.entries)
        result.insert(entry.pdoIndex);
    return result;
}

static bool profileIsPresent(
    const ProcessDataProfile &profile, const ProcessImagePreview &processImage)
{
    const QSet<quint16> rxPdos = pdoIndices(processImage.outputs);
    const QSet<quint16> txPdos = pdoIndices(processImage.inputs);
    return std::all_of(
               profile.rxPdoIndices.cbegin(),
               profile.rxPdoIndices.cend(),
               [&rxPdos](quint16 index) { return rxPdos.contains(index); })
           && std::all_of(
               profile.txPdoIndices.cbegin(),
               profile.txPdoIndices.cend(),
               [&txPdos](quint16 index) { return txPdos.contains(index); });
}

static const ProcessDataProfile *selectProfile(
    const DeviceAdapterManifest &manifest,
    const DeviceAdapterResolutionRequest &request,
    QString *error)
{
    if (manifest.processDataProfiles.isEmpty()) {
        if (!request.processDataProfileId.isEmpty())
            fail(error, "adapter does not declare process-data profiles");
        return nullptr;
    }
    if (!request.processDataProfileId.isEmpty()) {
        const auto found = std::find_if(
            manifest.processDataProfiles.cbegin(),
            manifest.processDataProfiles.cend(),
            [&request](const ProcessDataProfile &profile) {
                return profile.id == request.processDataProfileId;
            });
        if (found == manifest.processDataProfiles.cend()) {
            fail(
                error,
                QString("unknown process-data profile \"%1\"").arg(request.processDataProfileId));
            return nullptr;
        }
        if (!profileIsPresent(*found, request.processImage)) {
            fail(
                error,
                QString("process-data profile \"%1\" is not present in the process image")
                    .arg(found->id));
            return nullptr;
        }
        return &*found;
    }

    if (!manifest.moduleProfiles.isEmpty() && manifest.processDataProfiles.size() == 1)
        return &manifest.processDataProfiles.constFirst();

    QList<const ProcessDataProfile *> matches;
    for (const ProcessDataProfile &profile : manifest.processDataProfiles) {
        if (profileIsPresent(profile, request.processImage))
            matches.append(&profile);
    }
    if (matches.size() != 1) {
        fail(
            error,
            QString::fromLatin1(
                matches.isEmpty() ? "no process-data profile matches the process image"
                                  : "process-data profile selection is ambiguous"));
        return nullptr;
    }
    return matches.constFirst();
}

static bool qualificationAllows(
    const DeviceAdapterManifest &manifest,
    const DeviceAdapterResolutionRequest &request,
    QString *error)
{
    switch (manifest.qualification) {
    case DeviceAdapterQualification::Candidate:
        if (!request.allowCandidate)
            return fail(error, "Candidate adapter use was not approved");
        if (request.requireRealHardwareQualification)
            return fail(error, "Candidate adapter cannot cross the real-hardware gate");
        return true;
    case DeviceAdapterQualification::Qualified:
        if (request.requireRealHardwareQualification
            && (!manifest.signatureVerified || !manifest.realHardwareAllowed)) {
            return fail(error, "Qualified adapter lacks signed real-hardware permission");
        }
        return true;
    case DeviceAdapterQualification::MockOnly:
        if (!request.allowMock || request.requireRealHardwareQualification)
            return fail(error, "Mock-only adapter use was not approved");
        return true;
    case DeviceAdapterQualification::Revoked:
        return fail(error, "adapter package has been revoked");
    case DeviceAdapterQualification::Unqualified:
        return fail(error, "adapter package is unqualified");
    }
    return fail(error, "adapter package qualification is invalid");
}

static QList<const ProcessImageEntry *> matchingEntries(
    const DeviceSignalBinding &binding,
    const ProcessImagePreview &processImage,
    const ProcessDataProfile *profile)
{
    if (binding.kind != DeviceSignalBindingKind::ProcessDataObject)
        return {};
    if (profile) {
        const QList<quint16> &allowedPdos = binding.pdoDirection == PdoDirection::Rx
                                                ? profile->rxPdoIndices
                                                : profile->txPdoIndices;
        if (!allowedPdos.contains(binding.pdoIndex))
            return {};
    }
    const ProcessImageDirection &direction = binding.pdoDirection == PdoDirection::Rx
                                                 ? processImage.outputs
                                                 : processImage.inputs;
    QList<const ProcessImageEntry *> matches;
    for (const ProcessImageEntry &entry : direction.entries) {
        if (entry.direction == binding.pdoDirection && entry.pdoIndex == binding.pdoIndex
            && entry.index == binding.objectIndex && entry.subIndex == binding.objectSubIndex
            && entry.dataType == binding.physicalType && entry.bitLength == binding.bitWidth) {
            matches.append(&entry);
        }
    }
    return matches;
}

static void appendCapabilities(
    QList<DeviceCapabilityId> *target, const QList<DeviceCapabilityId> &additional)
{
    QSet<QString> existing;
    for (const DeviceCapabilityId &capability : std::as_const(*target))
        existing.insert(capability.value);
    for (const DeviceCapabilityId &capability : additional) {
        if (existing.contains(capability.value))
            continue;
        existing.insert(capability.value);
        target->append(capability);
    }
}

static bool bindSignal(
    const SemanticSignalDefinition &signal,
    int slot,
    quint32 moduleIdent,
    const ProcessImagePreview &processImage,
    const ProcessDataProfile *profile,
    ResolvedDeviceModel *model,
    QString *error)
{
    struct Match
    {
        DeviceSignalBinding binding;
        const ProcessImageEntry *entry = nullptr;
    };
    QList<Match> matches;
    for (const DeviceSignalBinding &binding : signal.bindings) {
        const QList<const ProcessImageEntry *> entries
            = matchingEntries(binding, processImage, profile);
        for (const ProcessImageEntry *entry : entries)
            matches.append({binding, entry});
    }
    if (matches.size() > 1) {
        return fail(
            error,
            QString("semantic signal \"%1\" has ambiguous process-image bindings")
                .arg(signal.id.value));
    }
    if (matches.isEmpty()) {
        if (signal.requiredForComplete) {
            return fail(
                error, QString("required semantic signal \"%1\" is missing").arg(signal.id.value));
        }
        model->warnings.append(QString("Optional signal \"%1\" is not bound").arg(signal.id.value));
        return true;
    }
    const Match &match = matches.constFirst();
    BoundSemanticSignal bound;
    bound.definition = signal;
    bound.binding = match.binding;
    bound.processImageEntryId = match.entry->entryId;
    bound.processImageBitOffset = match.entry->bitOffset;
    bound.processImageBitLength = match.entry->bitLength;
    bound.slot = slot;
    bound.moduleIdent = moduleIdent;
    model->boundSignals.append(bound);
    return true;
}

static bool addOffset(quint16 base, quint16 offset, quint16 *result)
{
    const quint32 sum = quint32(base) + quint32(offset);
    if (sum > std::numeric_limits<quint16>::max())
        return false;
    *result = quint16(sum);
    return true;
}

static bool expandModules(
    const DeviceAdapterManifest &manifest,
    const DeviceAdapterResolutionRequest &request,
    QList<SemanticSignalDefinition> *signalDefinitions,
    QList<int> *signalSlots,
    QList<quint32> *moduleIdents,
    ResolvedDeviceModel *model,
    QString *error)
{
    if (manifest.moduleProfiles.isEmpty()) {
        if (!request.moduleAssignments.isEmpty())
            return fail(error, "non-modular adapter received module assignments");
        return true;
    }
    if (request.moduleAssignments.isEmpty()) {
        model->complete = false;
        model->warnings.append("Module assignments have not been detected");
        return true;
    }

    QSet<int> seenSlots;
    QSet<QString> expandedIds;
    for (const DeviceModuleAssignment &assignment : request.moduleAssignments) {
        if (assignment.slot < 0 || assignment.moduleIdent == 0)
            return fail(error, "module assignment has an invalid slot or ModuleIdent");
        if (seenSlots.contains(assignment.slot))
            return fail(
                error, QString("module slot %1 is assigned more than once").arg(assignment.slot));
        seenSlots.insert(assignment.slot);
        const auto profile = std::find_if(
            manifest.moduleProfiles.cbegin(),
            manifest.moduleProfiles.cend(),
            [&assignment](const DeviceModuleProfile &item) {
                return item.moduleIdent == assignment.moduleIdent;
            });
        if (profile == manifest.moduleProfiles.cend()) {
            return fail(
                error,
                QString("module slot %1 has unsupported ModuleIdent 0x%2")
                    .arg(assignment.slot)
                    .arg(assignment.moduleIdent, 8, 16, QLatin1Char('0')));
        }
        appendCapabilities(&model->capabilities, profile->capabilities);
        for (SemanticSignalDefinition signal : profile->slotRelativeSignals) {
            signal.id.value.replace("{slot}", QString::number(assignment.slot));
            if (signal.id.value.contains("{slot}") || expandedIds.contains(signal.id.value)) {
                return fail(
                    error,
                    QString("module slot %1 produces an invalid or duplicate signal id")
                        .arg(assignment.slot));
            }
            expandedIds.insert(signal.id.value);
            for (DeviceSignalBinding &binding : signal.bindings) {
                if (!binding.slotRelative)
                    return fail(error, "module profile contains an absolute binding");
                if (!addOffset(binding.pdoIndex, assignment.pdoIndexOffset, &binding.pdoIndex)
                    || !addOffset(
                        binding.objectIndex, assignment.objectIndexOffset, &binding.objectIndex)) {
                    return fail(
                        error,
                        QString("module slot %1 binding offset overflows").arg(assignment.slot));
                }
                binding.slotRelative = false;
            }
            signalDefinitions->append(signal);
            signalSlots->append(assignment.slot);
            moduleIdents->append(assignment.moduleIdent);
        }
        model->moduleAssignments.append(assignment);
    }
    return true;
}

static DeviceAdapterResolutionResult resolvePackage(
    const DeviceAdapterManifest &manifest, const DeviceAdapterResolutionRequest &request)
{
    DeviceAdapterResolutionResult result;
    if (!qualificationAllows(manifest, request, &result.error))
        return result;

    QString profileError;
    const ProcessDataProfile *profile = selectProfile(manifest, request, &profileError);
    if (!profileError.isEmpty()) {
        result.error = profileError;
        return result;
    }

    ResolvedDeviceModel model;
    model.slaveId = request.slaveId;
    model.identity = request.device.summary.identity;
    model.esiSha256 = request.device.sourceSha256;
    model.adapterId = manifest.id;
    model.adapterVersion = manifest.version;
    model.adapterContentSha256 = manifest.contentSha256;
    model.qualification = manifest.qualification;
    model.capabilities = manifest.capabilities;
    model.processDataProfileId = profile ? profile->id : QString();
    model.complete = true;
    if (manifest.qualification == DeviceAdapterQualification::Candidate)
        model.warnings.append("Candidate adapter: real hardware control remains disabled");

    QList<SemanticSignalDefinition> signalDefinitions = manifest.semanticSignals;
    QList<int> signalSlots(signalDefinitions.size(), -1);
    QList<quint32> moduleIdents(signalDefinitions.size(), 0);
    if (!expandModules(
            manifest,
            request,
            &signalDefinitions,
            &signalSlots,
            &moduleIdents,
            &model,
            &result.error)) {
        return result;
    }

    QSet<QString> profileRequiredSignals;
    if (profile) {
        for (const SemanticSignalId &signal : profile->requiredSignals)
            profileRequiredSignals.insert(signal.value);
    }
    for (qsizetype index = 0; index < signalDefinitions.size(); ++index) {
        SemanticSignalDefinition signal = signalDefinitions.at(index);
        if (profileRequiredSignals.contains(signal.id.value))
            signal.requiredForComplete = true;
        if (!bindSignal(
                signal,
                signalSlots.at(index),
                moduleIdents.at(index),
                request.processImage,
                signalSlots.at(index) < 0 ? profile : nullptr,
                &model,
                &result.error)) {
            return result;
        }
    }

    if (!manifest.moduleProfiles.isEmpty() && request.moduleAssignments.isEmpty()) {
        model.complete = false;
        model.boundSignals.erase(
            std::remove_if(
                model.boundSignals.begin(),
                model.boundSignals.end(),
                [](const BoundSemanticSignal &signal) {
                    return signal.definition.access != SemanticSignalAccess::ReadOnly;
                }),
            model.boundSignals.end());
    }
    result.resolved = true;
    result.model = model;
    return result;
}

} // namespace

class AdapterPackageRepository::Private
{
public:
    explicit Private(const Utils::FilePath &root)
        : packageRoot(root)
    {}

    Utils::FilePath packageRoot;
    QList<Package> packages;
    QStringList loadErrors;
};

AdapterPackageRepository::AdapterPackageRepository(
    const Utils::FilePath &packageRoot, QObject *parent)
    : Core::DeviceAdapterProvider(
          Utils::Id("EtherCAT.DeviceAdapters.Packages"), tr("Device adapter packages"), parent)
    , d(std::make_unique<Private>(packageRoot))
{
    reload();
}

AdapterPackageRepository::~AdapterPackageRepository() = default;

QList<DeviceAdapterManifest> AdapterPackageRepository::adapterManifests() const
{
    QList<DeviceAdapterManifest> result;
    result.reserve(d->packages.size());
    for (const Package &package : d->packages)
        result.append(package.manifest);
    return result;
}

std::optional<DeviceAdapterManifest> AdapterPackageRepository::adapterManifest(
    const DeviceAdapterId &adapterId, const QString &version) const
{
    const auto found = std::find_if(
        d->packages.cbegin(), d->packages.cend(), [&adapterId, &version](const Package &package) {
            return package.manifest.id == adapterId && package.manifest.version == version;
        });
    if (found == d->packages.cend())
        return std::nullopt;
    return found->manifest;
}

DeviceAdapterResolutionResult AdapterPackageRepository::resolveDevice(
    const DeviceAdapterResolutionRequest &request) const
{
    if (!isAvailable())
        return {false, {}, d->loadErrors.join('\n')};

    const bool hasExactSelection = request.hasExpectedAdapterSelection();
    if (!request.hasValidExpectedAdapterSelection()) {
        return {
            false,
            {},
            tr("The expected adapter selection must be either empty or contain a canonical "
               "adapter ID, version, and SHA-256 digest.")};
    }
    if (hasExactSelection) {
        const auto selected = std::find_if(
            d->packages.cbegin(), d->packages.cend(), [&request](const Package &package) {
                return package.manifest.id == request.expectedAdapterId
                       && package.manifest.version == request.expectedAdapterVersion;
            });
        if (selected == d->packages.cend()) {
            return {
                false,
                {},
                tr("The expected adapter package %1 %2 was not found.")
                    .arg(request.expectedAdapterId.value, request.expectedAdapterVersion)};
        }
        if (selected->manifest.contentSha256 != request.expectedAdapterContentSha256) {
            return {false, {}, tr("The expected adapter package content SHA-256 does not match.")};
        }
        if (!identityMatches(selected->manifest, request.device)) {
            return {
                false,
                {},
                tr("The expected adapter package does not match the exact identity, revision, "
                   "and ESI hash.")};
        }
        DeviceAdapterResolutionResult resolution = resolvePackage(selected->manifest, request);
        if (resolution.resolved
            && (resolution.model.adapterId != request.expectedAdapterId
                || resolution.model.adapterVersion != request.expectedAdapterVersion
                || resolution.model.adapterContentSha256 != request.expectedAdapterContentSha256)) {
            return {false, {}, tr("The resolved adapter package does not match the expectation.")};
        }
        return resolution;
    }

    struct Match
    {
        const Package *package = nullptr;
        DeviceAdapterResolutionResult resolution;
    };
    QList<Match> successes;
    QStringList failures;
    bool identityMatched = false;
    for (const Package &package : d->packages) {
        if (!identityMatches(package.manifest, request.device))
            continue;
        identityMatched = true;
        DeviceAdapterResolutionResult resolution = resolvePackage(package.manifest, request);
        if (resolution.resolved)
            successes.append({&package, resolution});
        else
            failures.append(
                QString("%1 %2: %3")
                    .arg(package.manifest.id.value, package.manifest.version, resolution.error));
    }
    if (successes.isEmpty()) {
        return {
            false,
            {},
            identityMatched ? failures.join('\n')
                            : tr("No adapter matches the exact identity, revision, and ESI hash.")};
    }
    const int highestPriority = successes.constFirst().package->manifest.matchPriority;
    const int topMatches = int(
        std::count_if(successes.cbegin(), successes.cend(), [highestPriority](const Match &match) {
            return match.package->manifest.matchPriority == highestPriority;
        }));
    if (topMatches != 1)
        return {false, {}, tr("Adapter package selection is ambiguous at the highest priority.")};
    return successes.constFirst().resolution;
}

Utils::FilePath AdapterPackageRepository::packageRoot() const
{
    return d->packageRoot;
}

QStringList AdapterPackageRepository::loadErrors() const
{
    return d->loadErrors;
}

int AdapterPackageRepository::loadedPackageCount() const
{
    return d->packages.size();
}

void AdapterPackageRepository::reload()
{
    const QList<DeviceAdapterManifest> oldManifests = adapterManifests();
    d->packages.clear();
    d->loadErrors.clear();

    if (!d->packageRoot.exists() || !d->packageRoot.isDir()) {
        d->loadErrors.append(
            tr("Adapter package directory does not exist: %1").arg(d->packageRoot.toUserOutput()));
    } else {
        QStringList paths;
        QDirIterator iterator(
            d->packageRoot.toFSPathString(),
            {"*.adapter.json"},
            QDir::Files,
            QDirIterator::Subdirectories);
        while (iterator.hasNext())
            paths.append(iterator.next());
        paths.sort();
        if (paths.isEmpty()) {
            d->loadErrors.append(
                tr("No adapter packages were found in %1").arg(d->packageRoot.toUserOutput()));
        }
        QSet<QString> packageKeys;
        for (const QString &path : std::as_const(paths)) {
            const Utils::FilePath sourcePath = Utils::FilePath::fromString(path);
            const Utils::Result<QByteArray> contents = sourcePath.fileContents();
            if (!contents) {
                d->loadErrors.append(tr("%1: %2").arg(sourcePath.toUserOutput(), contents.error()));
                continue;
            }
            QString error;
            const std::optional<Package> package = parsePackage(sourcePath, *contents, &error);
            if (!package) {
                d->loadErrors.append(
                    error.startsWith(sourcePath.fileName())
                        ? error
                        : QString("%1: %2").arg(sourcePath.toUserOutput(), error));
                continue;
            }
            const QString key = package->manifest.id.value + '\n' + package->manifest.version;
            if (packageKeys.contains(key)) {
                d->loadErrors.append(tr("%1: duplicate adapter id and version %2 %3")
                                         .arg(
                                             sourcePath.toUserOutput(),
                                             package->manifest.id.value,
                                             package->manifest.version));
                continue;
            }
            packageKeys.insert(key);
            d->packages.append(*package);
        }
        std::sort(d->packages.begin(), d->packages.end(), packageLess);
    }

    setAvailable(d->loadErrors.isEmpty() && !d->packages.isEmpty());
    if (oldManifests != adapterManifests())
        emit adapterManifestsChanged();
}

} // namespace EtherCAT::DeviceAdapters::Internal
