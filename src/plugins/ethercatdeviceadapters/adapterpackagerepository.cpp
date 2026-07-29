// Copyright (C) 2026 Embed Labs

#include "adapterpackagerepository.h"

#include <utils/id.h>

#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace EtherCAT::DeviceAdapters::Internal {

using namespace Data;

namespace {

constexpr char schemaVersion[] = "embed-labs.device-adapter/v1";

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
    QString *error)
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
    return true;
}

static bool parseCapabilityList(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QList<DeviceCapabilityId> *result,
    QString *error)
{
    QStringList values;
    if (!parseStringList(object, key, context, &values, error))
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

static bool parseSignal(
    const QJsonValue &value,
    const QString &context,
    SemanticSignalDefinition *result,
    QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(
            object,
            {"id",
             "displayName",
             "description",
             "capabilities",
             "direction",
             "access",
             "required",
             "bindings",
             "value",
             "safeValue",
             "manualControl"},
            context,
            error)) {
        return false;
    }
    if (!parseString(object, "id", context, &result->id.value, error)
        || !parseString(object, "displayName", context, &result->displayName, error)
        || !parseString(object, "description", context, &result->description, error, true)
        || !parseCapabilityList(object, "capabilities", context, &result->capabilities, error)
        || !parseBool(object, "required", context, &result->requiredForComplete, error)) {
        return false;
    }
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
        result->bindings.append(binding);
    }
    if (!parseValueMetadata(object.value("value"), context + ".value", &result->valueMetadata, error)
        || !parseManualControl(
            object.value("manualControl"),
            context + ".manualControl",
            &result->manualControl,
            error)) {
        return false;
    }
    if (!parseScalar(
            object.value("safeValue"), context + ".safeValue", true, &result->safeValue, error)) {
        return false;
    }
    result->hasSafeValue = !object.value("safeValue").isNull();
    if (result->direction == SemanticSignalDirection::Input
        && result->access != SemanticSignalAccess::ReadOnly) {
        return fail(error, QString("%1 input signal must be read-only").arg(context));
    }
    if (result->access == SemanticSignalAccess::ReadOnly && result->manualControl.allowed)
        return fail(error, QString("%1 read-only signal cannot allow manual writes").arg(context));
    return true;
}

static bool parseSignals(
    const QJsonValue &value,
    const QString &context,
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
                &signal,
                error)) {
            return false;
        }
        if (ids.contains(signal.id.value))
            return fail(
                error,
                QString("%1 contains duplicate signal id \"%2\"").arg(context, signal.id.value));
        ids.insert(signal.id.value);
        result->append(signal);
    }
    return true;
}

static bool parseIndexList(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QList<quint16> *result,
    QString *error)
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
        seen.insert(quint16(parsed));
        result->append(quint16(parsed));
    }
    return true;
}

static bool parseProfiles(
    const QJsonValue &value,
    const QString &context,
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
        if (!checkKeys(object, {"id", "rxPdos", "txPdos", "requiredSignals"}, itemContext, error)) {
            return false;
        }
        ProcessDataProfile profile;
        QStringList requiredSignals;
        if (!parseString(object, "id", itemContext, &profile.id, error)
            || !parseIndexList(object, "rxPdos", itemContext, &profile.rxPdoIndices, error)
            || !parseIndexList(object, "txPdos", itemContext, &profile.txPdoIndices, error)
            || !parseStringList(object, "requiredSignals", itemContext, &requiredSignals, error)) {
            return false;
        }
        if (profile.rxPdoIndices.isEmpty() && profile.txPdoIndices.isEmpty())
            return fail(error, QString("%1 must select at least one PDO").arg(itemContext));
        if (ids.contains(profile.id))
            return fail(
                error, QString("%1 contains duplicate profile id \"%2\"").arg(context, profile.id));
        ids.insert(profile.id);
        for (const QString &signal : std::as_const(requiredSignals))
            profile.requiredSignals.append({signal});
        result->append(profile);
    }
    return true;
}

static bool parseModules(
    const QJsonValue &value,
    const QString &context,
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
            || !parseCapabilityList(object, "capabilities", itemContext, &profile.capabilities, error)
            || !parseSignals(
                object.value("signals"),
                itemContext + ".signals",
                &profile.slotRelativeSignals,
                error)) {
            return false;
        }
        if (moduleIdent == 0)
            return fail(error, QString("%1.moduleIdent must be non-zero").arg(itemContext));
        profile.moduleIdent = quint32(moduleIdent);
        if (ids.contains(profile.id) || moduleIdents.contains(profile.moduleIdent)) {
            return fail(
                error, QString("%1 contains a duplicate module id or ModuleIdent").arg(context));
        }
        ids.insert(profile.id);
        moduleIdents.insert(profile.moduleIdent);
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
    const QJsonValue &value, const QString &context, DeviceControlValue *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"source", "literal", "parameterId"}, context, error))
        return false;
    QString source;
    if (!parseString(object, "source", context, &source, error)
        || !parseNullableString(object, "parameterId", context, &result->parameterId, error)
        || !parseScalar(
            object.value("literal"), context + ".literal", true, &result->literalValue, error)) {
        return false;
    }
    if (source == "invalid")
        result->source = DeviceControlValueSource::Invalid;
    else if (source == "literal")
        result->source = DeviceControlValueSource::Literal;
    else if (source == "parameter")
        result->source = DeviceControlValueSource::Parameter;
    else
        return fail(error, QString("%1.source is not supported").arg(context));
    if (result->source == DeviceControlValueSource::Literal && object.value("literal").isNull())
        return fail(error, QString("%1.literal must be set for literal source").arg(context));
    if (result->source == DeviceControlValueSource::Parameter && result->parameterId.isEmpty())
        return fail(error, QString("%1.parameterId must be set for parameter source").arg(context));
    if (result->source != DeviceControlValueSource::Parameter && !result->parameterId.isEmpty())
        return fail(error, QString("%1.parameterId is only valid for parameter source").arg(context));
    return true;
}

static bool parseActionParameter(
    const QJsonValue &value,
    const QString &context,
    DeviceControlActionParameter *result,
    QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(
            object,
            {"id", "displayName", "description", "dataType", "value", "required", "defaultValue"},
            context,
            error)) {
        return false;
    }
    QString dataType;
    if (!parseString(object, "id", context, &result->id, error)
        || !parseString(object, "displayName", context, &result->displayName, error)
        || !parseString(object, "description", context, &result->description, error, true)
        || !parseString(object, "dataType", context, &dataType, error)
        || !parseValueMetadata(object.value("value"), context + ".value", &result->valueMetadata, error)
        || !parseBool(object, "required", context, &result->required, error)
        || !parseScalar(
            object.value("defaultValue"),
            context + ".defaultValue",
            true,
            &result->defaultValue,
            error)) {
        return false;
    }
    const auto parsedType = dataTypeFromString(dataType);
    if (!parsedType)
        return fail(error, QString("%1.dataType is not supported").arg(context));
    result->dataType = *parsedType;
    result->hasDefaultValue = !object.value("defaultValue").isNull();
    return true;
}

static bool parseActionStep(
    const QJsonValue &value, const QString &context, DeviceControlStep *result, QString *error)
{
    if (!value.isObject())
        return fail(error, QString("%1 must be an object").arg(context));
    const QJsonObject object = value.toObject();
    if (!checkKeys(object, {"kind", "signalId", "value", "mask", "timeoutMs"}, context, error))
        return false;
    QString kind;
    if (!parseString(object, "kind", context, &kind, error)
        || !parseString(object, "signalId", context, &result->signalId.value, error, true)
        || !parseControlValue(object.value("value"), context + ".value", &result->value, error)
        || !parseControlValue(object.value("mask"), context + ".mask", &result->mask, error)) {
        return false;
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
    return true;
}

static bool parseActions(
    const QJsonValue &value,
    const QString &context,
    QList<DeviceControlAction> *result,
    QString *error)
{
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
        if (!checkKeys(
                object,
                {"id",
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
                 "steps"},
                itemContext,
                error)) {
            return false;
        }
        DeviceControlAction action;
        QString failureAction;
        QString timeoutAction;
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
                object, "runtimeConditions", itemContext, &action.runtimeConditions, error)
            || !parseString(object, "failureAction", itemContext, &failureAction, error)
            || !parseString(object, "timeoutAction", itemContext, &timeoutAction, error)
            || !parseStringList(object, "requiredSignals", itemContext, &requiredSignals, error)) {
            return false;
        }
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
        const auto parsedFailureAction = timeoutActionFromString(failureAction);
        const auto parsedTimeoutAction = timeoutActionFromString(timeoutAction);
        if (!parsedFailureAction || !parsedTimeoutAction)
            return fail(error, QString("%1 has an unsupported failure action").arg(itemContext));
        action.failureAction = *parsedFailureAction;
        action.timeoutAction = *parsedTimeoutAction;
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
                    &parameter,
                    error)) {
                return false;
            }
            if (parameterIds.contains(parameter.id))
                return fail(error, QString("%1 has duplicate parameter ids").arg(itemContext));
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
        result->append(action);
    }
    return true;
}

static bool validateManifest(DeviceAdapterManifest *manifest, QString *error)
{
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
        if (action.enabled && action.commandTtlMs == 0) {
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
    if (!checkKeys(
            object,
            {"schemaVersion",
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
             "controlActions"},
            context,
            error)) {
        return std::nullopt;
    }

    Package package;
    DeviceAdapterManifest &manifest = package.manifest;
    QString parsedSchema;
    if (!parseString(object, "schemaVersion", context, &parsedSchema, error)
        || !parseString(object, "id", context, &manifest.id.value, error)
        || !parseString(object, "version", context, &manifest.version, error)
        || !parseString(object, "displayName", context, &manifest.displayName, error)
        || !parseString(object, "description", context, &manifest.description, error, true)) {
        return std::nullopt;
    }
    if (parsedSchema != schemaVersion) {
        fail(
            error,
            QString("%1.schemaVersion must equal \"%2\"")
                .arg(context, QString::fromLatin1(schemaVersion)));
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

    if (!parseCapabilityList(object, "capabilities", context, &manifest.capabilities, error)) {
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
            object.value("signals"), context + ".signals", &manifest.semanticSignals, error)
        || !parseProfiles(
            object.value("processDataProfiles"),
            context + ".processDataProfiles",
            &manifest.processDataProfiles,
            error)
        || !parseModules(
            object.value("moduleProfiles"),
            context + ".moduleProfiles",
            &manifest.moduleProfiles,
            error)
        || !parseActions(
            object.value("controlActions"),
            context + ".controlActions",
            &manifest.controlActions,
            error)) {
        return std::nullopt;
    }
    manifest.contentSha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
    package.sourcePath = sourcePath;
    if (!validateManifest(&manifest, error))
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
