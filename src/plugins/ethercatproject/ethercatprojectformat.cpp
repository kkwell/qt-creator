// Copyright (C) 2026 Kvell

#include "ethercatprojectformat.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojecttr.h"

#include <ethercatcore/manualcontrolcontract.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace EtherCAT::Project::Internal {

static constexpr char FORMAT_NAME[] = "ethercat-project";

static Utils::Result<Data::NodeId> parseRequiredId(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const Data::NodeId id = Data::NodeId::fromString(object.value(key).toString());
    if (id.isNull()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    return id;
}

static Utils::Result<QString> parseRequiredName(const QJsonObject &object, const QString &objectName)
{
    const QString name = object.value("name").toString().trimmed();
    if (name.isEmpty())
        return Utils::ResultError(Tr::tr("%1 has an empty or missing name.").arg(objectName));
    return name;
}

static Utils::Result<quint32> parseUnsigned(
    const QJsonObject &object,
    const QString &key,
    const QString &objectName,
    quint32 maximum = std::numeric_limits<quint32>::max())
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    const double rawValue = jsonValue.toDouble(-1);
    if (!std::isfinite(rawValue) || rawValue < 0 || rawValue > maximum
        || std::trunc(rawValue) != rawValue) {
        return Utils::ResultError(Tr::tr("%1 has an out-of-range '%2' value.").arg(objectName, key));
    }
    return quint32(rawValue);
}

static Utils::Result<qint64> parseSigned(
    const QJsonObject &object,
    const QString &key,
    const QString &objectName,
    qint64 minimum,
    qint64 maximum)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    const double rawValue = jsonValue.toDouble();
    constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
    if (!std::isfinite(rawValue) || rawValue < double(minimum) || rawValue > double(maximum)
        || rawValue < -double(maximumExactJsonInteger) || rawValue > double(maximumExactJsonInteger)
        || std::trunc(rawValue) != rawValue) {
        return Utils::ResultError(Tr::tr("%1 has an out-of-range '%2' value.").arg(objectName, key));
    }
    return qint64(rawValue);
}

static Utils::Result<bool> parseBool(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    return value.toBool();
}

static Utils::Result<QString> parseString(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    return value.toString();
}

static Utils::Result<QJsonArray> parseArray(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' array.").arg(objectName, key));
    }
    return value.toArray();
}

static Utils::Result<QJsonObject> parseObject(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isObject()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' object.").arg(objectName, key));
    }
    return value.toObject();
}

static Utils::Result<QByteArray> parseSha256(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const auto value = parseString(object, key, objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (value->size() != 64) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid '%2' SHA-256 digest.").arg(objectName, key));
    }
    for (const QChar character : *value) {
        const bool digit = character >= u'0' && character <= u'9';
        const bool lowerHex = character >= u'a' && character <= u'f';
        if (!digit && !lowerHex) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid '%2' SHA-256 digest.").arg(objectName, key));
        }
    }
    return QByteArray::fromHex(value->toLatin1());
}

static bool hasOnlyKeys(const QJsonObject &object, const QSet<QString> &allowedKeys)
{
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!allowedKeys.contains(iterator.key()))
            return false;
    }
    return true;
}

static Utils::Result<> rejectUnknownKeys(
    const QJsonObject &object, const QSet<QString> &allowedKeys, const QString &objectName)
{
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!allowedKeys.contains(iterator.key())) {
            return Utils::ResultError(
                Tr::tr("%1 has an unsupported '%2' field.").arg(objectName, iterator.key()));
        }
    }
    return Utils::ResultOk;
}

static Utils::Result<> rejectDuplicateJsonKeys(const QByteArray &contents)
{
    struct Scope
    {
        bool object = false;
        bool expectsKey = false;
        QSet<QString> keys;
    };
    QList<Scope> scopes;

    for (qsizetype offset = 0; offset < contents.size();) {
        const char character = contents.at(offset);
        if (character == '{') {
            scopes.append(Scope{true, true, {}});
            ++offset;
            continue;
        }
        if (character == '[') {
            scopes.append(Scope{false, false, {}});
            ++offset;
            continue;
        }
        if (character == '}' || character == ']') {
            if (!scopes.isEmpty())
                scopes.removeLast();
            ++offset;
            continue;
        }
        if (character == ',') {
            if (!scopes.isEmpty() && scopes.last().object)
                scopes.last().expectsKey = true;
            ++offset;
            continue;
        }
        if (character != '"') {
            ++offset;
            continue;
        }

        const qsizetype start = offset++;
        while (offset < contents.size()) {
            const char stringCharacter = contents.at(offset++);
            if (stringCharacter == '\\') {
                ++offset;
                continue;
            }
            if (stringCharacter == '"')
                break;
        }
        if (scopes.isEmpty() || !scopes.last().object || !scopes.last().expectsKey)
            continue;

        const QByteArray encoded = contents.mid(start, offset - start);
        QJsonParseError parseError;
        const QJsonDocument decoded = QJsonDocument::fromJson(
            QByteArrayLiteral("[") + encoded + QByteArrayLiteral("]"), &parseError);
        if (parseError.error != QJsonParseError::NoError || !decoded.isArray()
            || decoded.array().size() != 1 || !decoded.array().first().isString()) {
            return Utils::ResultError(Tr::tr("The JSON document has an invalid object key."));
        }
        const QString key = decoded.array().first().toString();
        if (scopes.last().keys.contains(key)) {
            return Utils::ResultError(
                Tr::tr("The JSON document contains the duplicate key '%1'.").arg(key));
        }
        scopes.last().keys.insert(key);
        scopes.last().expectsKey = false;
    }
    return Utils::ResultOk;
}

static bool isCanonicalIdentifier(const QString &value)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > 256)
        return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

static Utils::Result<qint64> parseCanonicalSignedString(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const auto text = parseString(object, key, objectName);
    if (!text)
        return Utils::ResultError(text.error());
    bool ok = false;
    const qint64 value = text->toLongLong(&ok, 10);
    if (!ok || *text != QString::number(value)) {
        return Utils::ResultError(
            Tr::tr("%1 has a non-canonical '%2' decimal integer.").arg(objectName, key));
    }
    return value;
}

static Utils::Result<quint64> parseCanonicalUnsignedString(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const auto text = parseString(object, key, objectName);
    if (!text)
        return Utils::ResultError(text.error());
    bool ok = false;
    const quint64 value = text->toULongLong(&ok, 10);
    if (!ok || *text != QString::number(value)) {
        return Utils::ResultError(
            Tr::tr("%1 has a non-canonical '%2' decimal integer.").arg(objectName, key));
    }
    return value;
}

static Utils::Result<Data::ExactRational> parseExactRational(
    const QJsonValue &value, const QString &objectName)
{
    if (!value.isObject())
        return Utils::ResultError(Tr::tr("%1 must be an exact-rational object.").arg(objectName));
    const QJsonObject object = value.toObject();
    static const QSet<QString> keys{"numerator", "denominator"};
    if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
        return Utils::ResultError(Tr::tr("%1 has an invalid exact-rational shape.").arg(objectName));
    const auto numerator = parseCanonicalSignedString(object, "numerator", objectName);
    const auto denominator = parseCanonicalSignedString(object, "denominator", objectName);
    if (!numerator || !denominator) {
        return Utils::ResultError(!numerator ? numerator.error() : denominator.error());
    }
    const Data::ExactRational rational{*numerator, *denominator};
    if (!EtherCAT::Core::validateExactRational(rational).accepted()) {
        return Utils::ResultError(
            Tr::tr("%1 is not a canonical exact rational.").arg(objectName));
    }
    return rational;
}

static Utils::Result<Data::EngineeringValue> parseEngineeringValue(
    const QJsonValue &value, const QString &objectName)
{
    if (!value.isObject())
        return Utils::ResultError(Tr::tr("%1 must be an engineering-value object.").arg(objectName));
    const QJsonObject object = value.toObject();
    const auto kind = parseString(object, "kind", objectName);
    if (!kind)
        return Utils::ResultError(kind.error());

    Data::EngineeringValue result;
    if (*kind == "boolean") {
        static const QSet<QString> keys{"kind", "boolean"};
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
            return Utils::ResultError(Tr::tr("%1 has an invalid Boolean value.").arg(objectName));
        const auto parsed = parseBool(object, "boolean", objectName);
        if (!parsed)
            return Utils::ResultError(parsed.error());
        result = Data::EngineeringValue::fromBoolean(*parsed);
    } else if (*kind == "signed-integer") {
        static const QSet<QString> keys{"kind", "signedInteger"};
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid signed-integer value.").arg(objectName));
        }
        const auto parsed = parseCanonicalSignedString(object, "signedInteger", objectName);
        if (!parsed)
            return Utils::ResultError(parsed.error());
        result = Data::EngineeringValue::fromSignedInteger(*parsed);
    } else if (*kind == "unsigned-integer") {
        static const QSet<QString> keys{"kind", "unsignedInteger"};
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid unsigned-integer value.").arg(objectName));
        }
        const auto parsed = parseCanonicalUnsignedString(object, "unsignedInteger", objectName);
        if (!parsed)
            return Utils::ResultError(parsed.error());
        result = Data::EngineeringValue::fromUnsignedInteger(*parsed);
    } else if (*kind == "exact-rational") {
        static const QSet<QString> keys{"kind", "exactRational"};
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid exact-rational value.").arg(objectName));
        }
        const auto parsed = parseExactRational(
            object.value("exactRational"), objectName + Tr::tr(" exact rational"));
        if (!parsed)
            return Utils::ResultError(parsed.error());
        result = Data::EngineeringValue::fromExactRational(*parsed);
    } else if (*kind == "enumeration") {
        static const QSet<QString> keys{"kind", "enumerationName"};
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid enumeration value.").arg(objectName));
        }
        const auto parsed = parseString(object, "enumerationName", objectName);
        if (!parsed)
            return Utils::ResultError(parsed.error());
        if (!isCanonicalIdentifier(*parsed)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid enumeration identifier.").arg(objectName));
        }
        result = Data::EngineeringValue::fromEnumeration(*parsed);
    } else {
        return Utils::ResultError(
            Tr::tr("%1 has an unsupported engineering-value kind.").arg(objectName));
    }

    if (!EtherCAT::Core::validateEngineeringValue(result).accepted())
        return Utils::ResultError(Tr::tr("%1 is not canonical.").arg(objectName));
    return result;
}

static Utils::Result<std::optional<Data::ExactRational>> parseOptionalRational(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (value.isNull())
        return std::optional<Data::ExactRational>();
    const auto parsed = parseExactRational(value, objectName + " " + key);
    if (!parsed)
        return Utils::ResultError(parsed.error());
    return std::optional<Data::ExactRational>(*parsed);
}

static Utils::Result<std::optional<Data::EngineeringValue>> parseOptionalEngineeringValue(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (value.isNull())
        return std::optional<Data::EngineeringValue>();
    const auto parsed = parseEngineeringValue(value, objectName + " " + key);
    if (!parsed)
        return Utils::ResultError(parsed.error());
    return std::optional<Data::EngineeringValue>(*parsed);
}

static Utils::Result<Data::EngineeringConstraint> parseEngineeringConstraint(
    const QJsonValue &value, const QString &objectName)
{
    if (!value.isObject())
        return Utils::ResultError(Tr::tr("%1 must be a constraint object.").arg(objectName));
    const QJsonObject object = value.toObject();
    static const QSet<QString> keys{
        "minimum",
        "maximum",
        "step",
        "stepOrigin",
        "enumeration",
    };
    if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
        return Utils::ResultError(Tr::tr("%1 has an invalid constraint shape.").arg(objectName));

    const auto minimum = parseOptionalRational(object, "minimum", objectName);
    const auto maximum = parseOptionalRational(object, "maximum", objectName);
    const auto step = parseOptionalRational(object, "step", objectName);
    const auto stepOrigin = parseOptionalRational(object, "stepOrigin", objectName);
    const auto enumeration = parseArray(object, "enumeration", objectName);
    if (!minimum || !maximum || !step || !stepOrigin || !enumeration) {
        const QString error = !minimum      ? minimum.error()
                              : !maximum   ? maximum.error()
                              : !step      ? step.error()
                              : !stepOrigin ? stepOrigin.error()
                                           : enumeration.error();
        return Utils::ResultError(error);
    }

    QList<Data::EngineeringEnumerationValue> entries;
    entries.reserve(enumeration->size());
    QString previousId;
    static const QSet<QString> entryKeys{"id", "displayName", "value"};
    for (qsizetype index = 0; index < enumeration->size(); ++index) {
        if (!enumeration->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("%1 enumeration entry %2 must be an object.").arg(objectName).arg(index));
        }
        const QJsonObject entryObject = enumeration->at(index).toObject();
        const QString entryName = Tr::tr("%1 enumeration entry %2").arg(objectName).arg(index);
        if (entryObject.size() != entryKeys.size()
            || !hasOnlyKeys(entryObject, entryKeys)) {
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(entryName));
        }
        const auto id = parseString(entryObject, "id", entryName);
        const auto displayName = parseString(entryObject, "displayName", entryName);
        const auto exact = parseExactRational(entryObject.value("value"), entryName + " value");
        if (!id || !displayName || !exact) {
            const QString error = !id ? id.error() : !displayName ? displayName.error() : exact.error();
            return Utils::ResultError(error);
        }
        if (!isCanonicalIdentifier(*id)
            || (!previousId.isEmpty() && *id <= previousId)) {
            return Utils::ResultError(
                Tr::tr("%1 enumeration entries are not in canonical ID order.").arg(objectName));
        }
        previousId = *id;
        entries.append({*id, *displayName, *exact});
    }

    const Data::EngineeringConstraint constraint{
        *minimum,
        *maximum,
        *step,
        *stepOrigin,
        entries,
    };
    if (!EtherCAT::Core::validateEngineeringConstraint(constraint).accepted())
        return Utils::ResultError(Tr::tr("%1 is not a valid exact constraint.").arg(objectName));
    return constraint;
}

static Utils::Result<Data::ManualCommandTiming> parseManualCommandTiming(
    const QJsonValue &value, const QString &objectName)
{
    if (!value.isObject())
        return Utils::ResultError(Tr::tr("%1 must be a timing object.").arg(objectName));
    const QJsonObject object = value.toObject();
    static const QSet<QString> keys{
        "commandTtlMs",
        "refreshTimeoutMs",
        "maxContinuousHoldMs",
    };
    if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
        return Utils::ResultError(Tr::tr("%1 has an invalid timing shape.").arg(objectName));
    const auto commandTtl = parseUnsigned(object, "commandTtlMs", objectName);
    const auto refresh = parseUnsigned(object, "refreshTimeoutMs", objectName);
    const auto maximumHold = parseUnsigned(object, "maxContinuousHoldMs", objectName);
    if (!commandTtl || !refresh || !maximumHold) {
        const QString error = !commandTtl ? commandTtl.error()
                              : !refresh  ? refresh.error()
                                          : maximumHold.error();
        return Utils::ResultError(error);
    }
    return Data::ManualCommandTiming{*commandTtl, *refresh, *maximumHold};
}

Utils::Result<> validateManualControlEnvelopeStructure(
    const Data::ManualControlEnvelope &envelope)
{
    QString previousSignalId;
    bool enabledOperation = false;
    for (const Data::ManualSignalEnvelope &signal : envelope.signalEnvelopes) {
        if (!isCanonicalIdentifier(signal.signalId.value)
            || (!previousSignalId.isEmpty() && signal.signalId.value <= previousSignalId)) {
            return Utils::ResultError(
                Tr::tr("Manual signal envelopes must use unique canonical ID order."));
        }
        previousSignalId = signal.signalId.value;
        enabledOperation = enabledOperation || signal.enabled;
        if (signal.enabled
            && (signal.timing.commandTtlMs == 0
                || (signal.holdToRun
                    && (signal.timing.refreshTimeoutMs == 0
                        || signal.timing.maxContinuousHoldMs == 0
                        || signal.timing.refreshTimeoutMs
                               > signal.timing.maxContinuousHoldMs))
                || (!signal.holdToRun
                    && (signal.timing.refreshTimeoutMs != 0
                        || signal.timing.maxContinuousHoldMs != 0)))) {
            return Utils::ResultError(Tr::tr("Manual signal timing is invalid."));
        }
        if (!EtherCAT::Core::validateEngineeringConstraint(signal.allowedRange).accepted()) {
            return Utils::ResultError(Tr::tr("Manual signal range is invalid."));
        }
        if (signal.enabled && !signal.safeValue) {
            return Utils::ResultError(
                Tr::tr("Enabled manual signals require an exact safe value."));
        }
        if (signal.safeValue
            && !EtherCAT::Core::validateEngineeringValueAgainstConstraint(
                    *signal.safeValue, signal.allowedRange)
                    .accepted()) {
            return Utils::ResultError(Tr::tr("Manual signal safe value is invalid."));
        }
        QString previousPeerId;
        for (const Data::ManualSignalSafeValue &peer : signal.consistencyGroupSafeValues) {
            if (!isCanonicalIdentifier(peer.signalId.value)
                || peer.signalId == signal.signalId
                || (!previousPeerId.isEmpty() && peer.signalId.value <= previousPeerId)
                || !EtherCAT::Core::validateEngineeringValue(peer.value).accepted()) {
                return Utils::ResultError(
                    Tr::tr("Manual consistency-group safe values are not canonical."));
            }
            previousPeerId = peer.signalId.value;
        }
    }

    QString previousActionId;
    for (const Data::ManualActionEnvelope &action : envelope.actionEnvelopes) {
        if (!isCanonicalIdentifier(action.actionId.value)
            || (!previousActionId.isEmpty() && action.actionId.value <= previousActionId)) {
            return Utils::ResultError(
                Tr::tr("Manual action envelopes must use unique canonical ID order."));
        }
        previousActionId = action.actionId.value;
        enabledOperation = enabledOperation || action.enabled;
        if (action.enabled
            && (action.timing.commandTtlMs == 0
                || (action.holdToRun
                    && (action.timing.refreshTimeoutMs == 0
                        || action.timing.maxContinuousHoldMs == 0
                        || action.timing.refreshTimeoutMs
                               > action.timing.maxContinuousHoldMs))
                || (!action.holdToRun
                    && (action.timing.refreshTimeoutMs != 0
                        || action.timing.maxContinuousHoldMs != 0)))) {
            return Utils::ResultError(Tr::tr("Manual action timing is invalid."));
        }
        if (!isCanonicalIdentifier(action.releaseActionId.value)
            || !isCanonicalIdentifier(action.timeoutActionId.value)
            || !isCanonicalIdentifier(action.failureActionId.value)
            || action.releaseActionId == action.actionId
            || action.timeoutActionId == action.actionId
            || action.failureActionId == action.actionId) {
            return Utils::ResultError(Tr::tr("Manual action fallbacks are incomplete."));
        }

        QString previousParameterId;
        for (const Data::ManualActionParameterEnvelope &parameter : action.parameters) {
            if (!isCanonicalIdentifier(parameter.parameterId)
                || (!previousParameterId.isEmpty()
                    && parameter.parameterId <= previousParameterId)
                || !EtherCAT::Core::validateEngineeringConstraint(parameter.allowedRange)
                        .accepted()
                || (parameter.defaultValue
                    && !EtherCAT::Core::validateEngineeringValueAgainstConstraint(
                            *parameter.defaultValue, parameter.allowedRange)
                            .accepted())) {
                return Utils::ResultError(
                    Tr::tr("Manual action parameter envelopes are not canonical."));
            }
            previousParameterId = parameter.parameterId;
        }
    }

    if (envelope.enabled && !enabledOperation) {
        return Utils::ResultError(
            Tr::tr("Enabled manual control requires at least one enabled operation."));
    }
    return Utils::ResultOk;
}

static Utils::Result<Data::ManualControlEnvelope> parseManualControlEnvelope(
    const QJsonObject &slaveObject, const QString &objectName)
{
    const auto object = parseObject(slaveObject, "manualControlEnvelope", objectName);
    if (!object)
        return Utils::ResultError(object.error());
    static const QSet<QString> envelopeKeys{"enabled", "signalEnvelopes", "actionEnvelopes"};
    if (object->size() != envelopeKeys.size() || !hasOnlyKeys(*object, envelopeKeys)) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid manual-control envelope shape.").arg(objectName));
    }
    const auto enabled = parseBool(*object, "enabled", objectName);
    const auto signalArray = parseArray(*object, "signalEnvelopes", objectName);
    const auto actions = parseArray(*object, "actionEnvelopes", objectName);
    if (!enabled || !signalArray || !actions) {
        const QString error
            = !enabled ? enabled.error() : !signalArray ? signalArray.error() : actions.error();
        return Utils::ResultError(error);
    }

    QList<Data::ManualSignalEnvelope> signalEnvelopes;
    signalEnvelopes.reserve(signalArray->size());
    static const QSet<QString> signalKeys{
        "signalId",
        "enabled",
        "holdToRun",
        "timing",
        "allowedRange",
        "safeValue",
        "consistencyGroupSafeValues",
    };
    static const QSet<QString> peerKeys{"signalId", "value"};
    for (qsizetype index = 0; index < signalArray->size(); ++index) {
        if (!signalArray->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Manual signal envelope %1 must be an object.").arg(index));
        }
        const QJsonObject signalObject = signalArray->at(index).toObject();
        const QString signalName = Tr::tr("Manual signal envelope %1").arg(index);
        if (signalObject.size() != signalKeys.size()
            || !hasOnlyKeys(signalObject, signalKeys)) {
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(signalName));
        }
        const auto signalId = parseString(signalObject, "signalId", signalName);
        const auto signalEnabled = parseBool(signalObject, "enabled", signalName);
        const auto holdToRun = parseBool(signalObject, "holdToRun", signalName);
        const auto timing = parseManualCommandTiming(signalObject.value("timing"), signalName);
        const auto allowedRange
            = parseEngineeringConstraint(signalObject.value("allowedRange"), signalName);
        const auto safeValue
            = parseOptionalEngineeringValue(signalObject, "safeValue", signalName);
        const auto peers = parseArray(signalObject, "consistencyGroupSafeValues", signalName);
        if (!signalId || !signalEnabled || !holdToRun || !timing || !allowedRange || !safeValue
            || !peers) {
            const QString error = !signalId      ? signalId.error()
                                  : !signalEnabled ? signalEnabled.error()
                                  : !holdToRun   ? holdToRun.error()
                                  : !timing      ? timing.error()
                                  : !allowedRange ? allowedRange.error()
                                  : !safeValue   ? safeValue.error()
                                                 : peers.error();
            return Utils::ResultError(error);
        }
        QList<Data::ManualSignalSafeValue> groupValues;
        groupValues.reserve(peers->size());
        for (qsizetype peerIndex = 0; peerIndex < peers->size(); ++peerIndex) {
            if (!peers->at(peerIndex).isObject()) {
                return Utils::ResultError(
                    Tr::tr("%1 consistency peer %2 must be an object.")
                        .arg(signalName)
                        .arg(peerIndex));
            }
            const QJsonObject peerObject = peers->at(peerIndex).toObject();
            const QString peerName
                = Tr::tr("%1 consistency peer %2").arg(signalName).arg(peerIndex);
            if (peerObject.size() != peerKeys.size() || !hasOnlyKeys(peerObject, peerKeys))
                return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(peerName));
            const auto peerId = parseString(peerObject, "signalId", peerName);
            const auto peerValue = parseEngineeringValue(peerObject.value("value"), peerName);
            if (!peerId || !peerValue)
                return Utils::ResultError(!peerId ? peerId.error() : peerValue.error());
            groupValues.append({Data::SemanticSignalId{*peerId}, *peerValue});
        }
        signalEnvelopes.append(
            {Data::SemanticSignalId{*signalId},
             *signalEnabled,
             *holdToRun,
             *timing,
             *allowedRange,
             *safeValue,
             groupValues});
    }

    QList<Data::ManualActionEnvelope> actionEnvelopes;
    actionEnvelopes.reserve(actions->size());
    static const QSet<QString> actionKeys{
        "actionId",
        "enabled",
        "holdToRun",
        "timing",
        "parameters",
        "releaseActionId",
        "timeoutActionId",
        "failureActionId",
    };
    static const QSet<QString> parameterKeys{"parameterId", "allowedRange", "defaultValue"};
    for (qsizetype index = 0; index < actions->size(); ++index) {
        if (!actions->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Manual action envelope %1 must be an object.").arg(index));
        }
        const QJsonObject actionObject = actions->at(index).toObject();
        const QString actionName = Tr::tr("Manual action envelope %1").arg(index);
        if (actionObject.size() != actionKeys.size()
            || !hasOnlyKeys(actionObject, actionKeys)) {
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(actionName));
        }
        const auto actionId = parseString(actionObject, "actionId", actionName);
        const auto actionEnabled = parseBool(actionObject, "enabled", actionName);
        const auto holdToRun = parseBool(actionObject, "holdToRun", actionName);
        const auto timing = parseManualCommandTiming(actionObject.value("timing"), actionName);
        const auto parameters = parseArray(actionObject, "parameters", actionName);
        const auto releaseActionId = parseString(actionObject, "releaseActionId", actionName);
        const auto timeoutActionId = parseString(actionObject, "timeoutActionId", actionName);
        const auto failureActionId = parseString(actionObject, "failureActionId", actionName);
        if (!actionId || !actionEnabled || !holdToRun || !timing || !parameters
            || !releaseActionId || !timeoutActionId || !failureActionId) {
            const QString error = !actionId        ? actionId.error()
                                  : !actionEnabled ? actionEnabled.error()
                                  : !holdToRun     ? holdToRun.error()
                                  : !timing        ? timing.error()
                                  : !parameters    ? parameters.error()
                                  : !releaseActionId ? releaseActionId.error()
                                  : !timeoutActionId ? timeoutActionId.error()
                                                     : failureActionId.error();
            return Utils::ResultError(error);
        }
        QList<Data::ManualActionParameterEnvelope> parameterEnvelopes;
        parameterEnvelopes.reserve(parameters->size());
        for (qsizetype parameterIndex = 0; parameterIndex < parameters->size();
             ++parameterIndex) {
            if (!parameters->at(parameterIndex).isObject()) {
                return Utils::ResultError(
                    Tr::tr("%1 parameter %2 must be an object.")
                        .arg(actionName)
                        .arg(parameterIndex));
            }
            const QJsonObject parameterObject = parameters->at(parameterIndex).toObject();
            const QString parameterName
                = Tr::tr("%1 parameter %2").arg(actionName).arg(parameterIndex);
            if (parameterObject.size() != parameterKeys.size()
                || !hasOnlyKeys(parameterObject, parameterKeys)) {
                return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(parameterName));
            }
            const auto parameterId = parseString(parameterObject, "parameterId", parameterName);
            const auto allowedRange
                = parseEngineeringConstraint(parameterObject.value("allowedRange"), parameterName);
            const auto defaultValue
                = parseOptionalEngineeringValue(parameterObject, "defaultValue", parameterName);
            if (!parameterId || !allowedRange || !defaultValue) {
                const QString error = !parameterId   ? parameterId.error()
                                      : !allowedRange ? allowedRange.error()
                                                      : defaultValue.error();
                return Utils::ResultError(error);
            }
            parameterEnvelopes.append({*parameterId, *allowedRange, *defaultValue});
        }
        actionEnvelopes.append(
            {Data::SemanticActionId{*actionId},
             *actionEnabled,
             *holdToRun,
             *timing,
             parameterEnvelopes,
             Data::SemanticActionId{*releaseActionId},
             Data::SemanticActionId{*timeoutActionId},
             Data::SemanticActionId{*failureActionId}});
    }

    Data::ManualControlEnvelope envelope{*enabled, signalEnvelopes, actionEnvelopes};
    if (const Utils::Result<> validation = validateManualControlEnvelopeStructure(envelope);
        !validation) {
        return Utils::ResultError(validation.error());
    }
    return envelope;
}

static Utils::Result<Data::DeviceAdapterProjectSelection> parseAdapterSelection(
    const QJsonObject &slaveObject, const QString &objectName, const QByteArray &esiSha256)
{
    const QJsonValue selectionValue = slaveObject.value("adapterSelection");
    if (selectionValue.isUndefined())
        return Data::DeviceAdapterProjectSelection();
    if (!selectionValue.isObject()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid 'adapterSelection' object.").arg(objectName));
    }
    if (esiSha256.size() != 32) {
        return Utils::ResultError(
            Tr::tr("%1 must bind its adapter selection to an ESI SHA-256 digest.")
                .arg(objectName));
    }

    const QJsonObject selectionObject = selectionValue.toObject();
    static const QSet<QString> selectionKeys{
        "adapterId",
        "adapterVersion",
        "adapterContentSha256",
        "processDataProfileId",
        "moduleAssignments",
    };
    if (selectionObject.size() != selectionKeys.size()
        || !hasOnlyKeys(selectionObject, selectionKeys)) {
        return Utils::ResultError(
            Tr::tr("%1 has an incomplete adapter selection.").arg(objectName));
    }

    const auto adapterId = parseString(selectionObject, "adapterId", objectName);
    const auto adapterVersion = parseString(selectionObject, "adapterVersion", objectName);
    const auto adapterSha256
        = parseSha256(selectionObject, "adapterContentSha256", objectName);
    const auto profileId = parseString(selectionObject, "processDataProfileId", objectName);
    const auto modules = parseArray(selectionObject, "moduleAssignments", objectName);
    if (!adapterId || !adapterVersion || !adapterSha256 || !profileId || !modules) {
        const QString error = !adapterId        ? adapterId.error()
                              : !adapterVersion ? adapterVersion.error()
                              : !adapterSha256  ? adapterSha256.error()
                              : !profileId      ? profileId.error()
                                                : modules.error();
        return Utils::ResultError(error);
    }
    if (adapterId->isEmpty() || *adapterId != adapterId->trimmed()
        || adapterVersion->isEmpty() || *adapterVersion != adapterVersion->trimmed()
        || profileId->isEmpty() || *profileId != profileId->trimmed()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid adapter identifier, version, or profile.").arg(objectName));
    }

    QList<Data::DeviceModuleAssignment> assignments;
    assignments.reserve(modules->size());
    QSet<int> moduleSlots;
    static const QSet<QString> moduleKeys{
        "slot",
        "moduleIdent",
        "objectIndexOffset",
        "pdoIndexOffset",
    };
    for (qsizetype index = 0; index < modules->size(); ++index) {
        if (!modules->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Module assignment %1 for %2 must be a JSON object.")
                    .arg(index)
                    .arg(objectName));
        }
        const QJsonObject moduleObject = modules->at(index).toObject();
        const QString moduleName = Tr::tr("Module assignment %1 for %2")
                                       .arg(index)
                                       .arg(objectName);
        if (moduleObject.size() != moduleKeys.size()
            || !hasOnlyKeys(moduleObject, moduleKeys)) {
            return Utils::ResultError(
                Tr::tr("%1 is incomplete.").arg(moduleName));
        }
        const auto slot = parseUnsigned(
            moduleObject, "slot", moduleName, std::numeric_limits<int>::max());
        const auto moduleIdent = parseUnsigned(moduleObject, "moduleIdent", moduleName);
        const auto objectOffset = parseUnsigned(
            moduleObject,
            "objectIndexOffset",
            moduleName,
            std::numeric_limits<quint16>::max());
        const auto pdoOffset = parseUnsigned(
            moduleObject,
            "pdoIndexOffset",
            moduleName,
            std::numeric_limits<quint16>::max());
        if (!slot || !moduleIdent || !objectOffset || !pdoOffset) {
            const QString error = !slot          ? slot.error()
                                  : !moduleIdent ? moduleIdent.error()
                                  : !objectOffset ? objectOffset.error()
                                                  : pdoOffset.error();
            return Utils::ResultError(error);
        }
        if (*moduleIdent == 0) {
            return Utils::ResultError(
                Tr::tr("%1 requires a non-zero ModuleIdent.").arg(moduleName));
        }
        if (moduleSlots.contains(int(*slot))) {
            return Utils::ResultError(
                Tr::tr("%1 reuses an adapter module slot.").arg(moduleName));
        }
        moduleSlots.insert(int(*slot));
        assignments.append(
            {int(*slot), *moduleIdent, quint16(*objectOffset), quint16(*pdoOffset)});
    }
    std::sort(assignments.begin(), assignments.end(), [](const auto &left, const auto &right) {
        return left.slot < right.slot;
    });

    return Data::DeviceAdapterProjectSelection{
        Data::DeviceAdapterId{*adapterId},
        *adapterVersion,
        *adapterSha256,
        *profileId,
        assignments,
    };
}

static Utils::Result<Data::SemanticBindingArtifactReference> parseBindingArtifact(
    const QJsonObject &masterObject,
    const QList<Data::OfflineSlaveConfiguration> &slaves,
    const Data::NodeId &masterId,
    bool requireProjectDeviceBindings)
{
    const QJsonValue referenceValue = masterObject.value("semanticBindingArtifact");
    if (referenceValue.isUndefined())
        return Data::SemanticBindingArtifactReference();
    if (!referenceValue.isObject()) {
        return Utils::ResultError(
            Tr::tr("Master has an invalid 'semanticBindingArtifact' object."));
    }

    const QJsonObject referenceObject = referenceValue.toObject();
    static const QSet<QString> referenceKeys{
        "artifactId",
        "artifactSha256",
        "projectConfigurationSha256",
        "projectDeviceBindings",
    };
    const qsizetype requiredKeyCount = requireProjectDeviceBindings ? referenceKeys.size()
                                                                   : referenceKeys.size() - 1;
    if (referenceObject.size() != requiredKeyCount
        || !hasOnlyKeys(referenceObject, referenceKeys)
        || (requireProjectDeviceBindings
            && !referenceObject.contains("projectDeviceBindings"))) {
        return Utils::ResultError(Tr::tr("Master has an incomplete binding artifact reference."));
    }

    const auto artifactId = parseString(referenceObject, "artifactId", Tr::tr("Master"));
    const auto artifactSha256
        = parseSha256(referenceObject, "artifactSha256", Tr::tr("Master"));
    const auto projectSha256
        = parseSha256(referenceObject, "projectConfigurationSha256", Tr::tr("Master"));
    if (!artifactId || !artifactSha256 || !projectSha256) {
        const QString error = !artifactId       ? artifactId.error()
                              : !artifactSha256 ? artifactSha256.error()
                                                : projectSha256.error();
        return Utils::ResultError(error);
    }
    if (artifactId->isEmpty() || *artifactId != artifactId->trimmed()) {
        return Utils::ResultError(Tr::tr("Master has an invalid binding artifact ID."));
    }

    QList<Data::SemanticProjectDeviceBinding> projectDeviceBindings;
    if (requireProjectDeviceBindings) {
        const auto bindings
            = parseArray(referenceObject, "projectDeviceBindings", Tr::tr("Master"));
        if (!bindings)
            return Utils::ResultError(bindings.error());

        QSet<Data::NodeId> slaveIds;
        QSet<QString> projectDeviceIds;
        QString previousSlaveId;
        static const QSet<QString> bindingKeys{"slaveId", "projectDeviceId"};
        for (qsizetype index = 0; index < bindings->size(); ++index) {
            if (!bindings->at(index).isObject()) {
                return Utils::ResultError(
                    Tr::tr("Project device binding %1 must be an object.").arg(index));
            }
            const QJsonObject bindingObject = bindings->at(index).toObject();
            if (bindingObject.size() != bindingKeys.size()
                || !hasOnlyKeys(bindingObject, bindingKeys)) {
                return Utils::ResultError(
                    Tr::tr("Project device binding %1 is incomplete.").arg(index));
            }
            const QString bindingName = Tr::tr("Project device binding %1").arg(index);
            const auto slaveId = parseRequiredId(bindingObject, "slaveId", bindingName);
            const auto projectDeviceId
                = parseString(bindingObject, "projectDeviceId", bindingName);
            if (!slaveId || !projectDeviceId) {
                return Utils::ResultError(
                    !slaveId ? slaveId.error() : projectDeviceId.error());
            }
            if (projectDeviceId->isEmpty() || *projectDeviceId != projectDeviceId->trimmed()) {
                return Utils::ResultError(
                    Tr::tr("%1 has an invalid project device ID.").arg(bindingName));
            }
            const QString canonicalSlaveId = slaveId->toString();
            if (!previousSlaveId.isEmpty() && previousSlaveId >= canonicalSlaveId) {
                return Utils::ResultError(
                    Tr::tr("Project device bindings must use unique canonical slave ID order."));
            }
            const auto slave = std::find_if(
                slaves.cbegin(), slaves.cend(), [&slaveId, &masterId](const auto &entry) {
                    return entry.id == *slaveId && entry.masterId == masterId;
                });
            if (slave == slaves.cend()) {
                return Utils::ResultError(
                    Tr::tr("A project device binding refers to a slave outside this master."));
            }
            if (slaveIds.contains(*slaveId) || projectDeviceIds.contains(*projectDeviceId)) {
                return Utils::ResultError(
                    Tr::tr("Project device bindings must use unique slave and project device IDs."));
            }
            slaveIds.insert(*slaveId);
            projectDeviceIds.insert(*projectDeviceId);
            previousSlaveId = canonicalSlaveId;
            projectDeviceBindings.append({*slaveId, *projectDeviceId});
        }
    }
    return Data::SemanticBindingArtifactReference{
        *artifactId,
        *artifactSha256,
        *projectSha256,
        projectDeviceBindings,
    };
}

static Utils::Result<Data::NodeId> parseUniqueId(
    const QJsonObject &object,
    const QString &key,
    const QString &objectName,
    QSet<Data::NodeId> *uniqueIds)
{
    const auto id = parseRequiredId(object, key, objectName);
    if (!id)
        return Utils::ResultError(id.error());
    if (uniqueIds->contains(*id)) {
        return Utils::ResultError(
            Tr::tr("%1 reuses a stable ID that is already present in the project.").arg(objectName));
    }
    uniqueIds->insert(*id);
    return *id;
}

static QString dataTypeName(Data::EtherCATDataType dataType)
{
    switch (dataType) {
    case Data::EtherCATDataType::Unknown:
        return "unknown";
    case Data::EtherCATDataType::Boolean:
        return "boolean";
    case Data::EtherCATDataType::Integer8:
        return "integer8";
    case Data::EtherCATDataType::UnsignedInteger8:
        return "unsigned-integer8";
    case Data::EtherCATDataType::Integer16:
        return "integer16";
    case Data::EtherCATDataType::UnsignedInteger16:
        return "unsigned-integer16";
    case Data::EtherCATDataType::Integer32:
        return "integer32";
    case Data::EtherCATDataType::UnsignedInteger32:
        return "unsigned-integer32";
    case Data::EtherCATDataType::Integer64:
        return "integer64";
    case Data::EtherCATDataType::UnsignedInteger64:
        return "unsigned-integer64";
    case Data::EtherCATDataType::Real32:
        return "real32";
    case Data::EtherCATDataType::Real64:
        return "real64";
    case Data::EtherCATDataType::VisibleString:
        return "visible-string";
    case Data::EtherCATDataType::OctetString:
        return "octet-string";
    }
    return "unknown";
}

static Utils::Result<Data::EtherCATDataType> parseDataType(
    const QJsonObject &object, const QString &objectName)
{
    const auto value = parseString(object, "dataType", objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (*value == "unknown")
        return Data::EtherCATDataType::Unknown;
    if (*value == "boolean")
        return Data::EtherCATDataType::Boolean;
    if (*value == "integer8")
        return Data::EtherCATDataType::Integer8;
    if (*value == "unsigned-integer8")
        return Data::EtherCATDataType::UnsignedInteger8;
    if (*value == "integer16")
        return Data::EtherCATDataType::Integer16;
    if (*value == "unsigned-integer16")
        return Data::EtherCATDataType::UnsignedInteger16;
    if (*value == "integer32")
        return Data::EtherCATDataType::Integer32;
    if (*value == "unsigned-integer32")
        return Data::EtherCATDataType::UnsignedInteger32;
    if (*value == "integer64")
        return Data::EtherCATDataType::Integer64;
    if (*value == "unsigned-integer64")
        return Data::EtherCATDataType::UnsignedInteger64;
    if (*value == "real32")
        return Data::EtherCATDataType::Real32;
    if (*value == "real64")
        return Data::EtherCATDataType::Real64;
    if (*value == "visible-string")
        return Data::EtherCATDataType::VisibleString;
    if (*value == "octet-string")
        return Data::EtherCATDataType::OctetString;
    return Utils::ResultError(
        Tr::tr("%1 has an unsupported data type '%2'.").arg(objectName, *value));
}

static QString syncManagerDirectionName(Data::SyncManagerDirection direction)
{
    switch (direction) {
    case Data::SyncManagerDirection::Unknown:
        return "unknown";
    case Data::SyncManagerDirection::MasterToSlave:
        return "master-to-slave";
    case Data::SyncManagerDirection::SlaveToMaster:
        return "slave-to-master";
    }
    return "unknown";
}

static Utils::Result<Data::SyncManagerDirection> parseSyncManagerDirection(
    const QJsonObject &object, const QString &objectName)
{
    const auto value = parseString(object, "direction", objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (*value == "unknown")
        return Data::SyncManagerDirection::Unknown;
    if (*value == "master-to-slave")
        return Data::SyncManagerDirection::MasterToSlave;
    if (*value == "slave-to-master")
        return Data::SyncManagerDirection::SlaveToMaster;
    return Utils::ResultError(
        Tr::tr("%1 has an unsupported Sync Manager direction '%2'.").arg(objectName, *value));
}

static QString pdoDirectionName(Data::PdoDirection direction)
{
    return direction == Data::PdoDirection::Rx ? "rx" : "tx";
}

static Utils::Result<Data::PdoDirection> parsePdoDirection(
    const QJsonObject &object, const QString &objectName)
{
    const auto value = parseString(object, "direction", objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (*value == "rx")
        return Data::PdoDirection::Rx;
    if (*value == "tx")
        return Data::PdoDirection::Tx;
    return Utils::ResultError(
        Tr::tr("%1 has an unsupported PDO direction '%2'.").arg(objectName, *value));
}

static QString masterTimingModeName(Data::MasterTimingMode mode)
{
    switch (mode) {
    case Data::MasterTimingMode::Unassigned:
        return "unassigned";
    case Data::MasterTimingMode::FreeRun:
        return "free-run";
    case Data::MasterTimingMode::DistributedClocks:
        return "distributed-clocks";
    }
    return "unassigned";
}

static Utils::Result<Data::MasterConfiguration> parseMasterConfiguration(
    const QJsonObject &masterObject)
{
    const auto object = parseObject(masterObject, "configuration", Tr::tr("Master"));
    if (!object)
        return Utils::ResultError(object.error());
    static const QSet<QString> keys{"timingMode", "cyclePeriodNs"};
    if (object->size() != keys.size() || !hasOnlyKeys(*object, keys)) {
        return Utils::ResultError(
            Tr::tr("Master configuration has an invalid shape."));
    }
    const auto mode = parseString(*object, "timingMode", Tr::tr("Master configuration"));
    const auto cycle = parseUnsigned(*object, "cyclePeriodNs", Tr::tr("Master configuration"));
    if (!mode || !cycle)
        return Utils::ResultError(!mode ? mode.error() : cycle.error());

    Data::MasterTimingMode timingMode = Data::MasterTimingMode::Unassigned;
    if (*mode == "free-run") {
        timingMode = Data::MasterTimingMode::FreeRun;
    } else if (*mode == "distributed-clocks") {
        timingMode = Data::MasterTimingMode::DistributedClocks;
    } else if (*mode != "unassigned") {
        return Utils::ResultError(
            Tr::tr("Master configuration has an unsupported timing mode '%1'.").arg(*mode));
    }

    if (timingMode == Data::MasterTimingMode::Unassigned && *cycle != 0) {
        return Utils::ResultError(
            Tr::tr("An unassigned EtherCAT timing mode cannot have a cycle period."));
    }
    if (timingMode != Data::MasterTimingMode::Unassigned && *cycle == 0) {
        return Utils::ResultError(
            Tr::tr("FreeRun and Distributed Clocks require a non-zero cycle period."));
    }
    return Data::MasterConfiguration{timingMode, *cycle};
}

static Utils::Result<QList<Data::SyncManagerConfiguration>> parseSyncManagers(
    const QJsonObject &processDataObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto array = parseArray(processDataObject, "syncManagers", slaveName);
    if (!array)
        return Utils::ResultError(array.error());

    QList<Data::SyncManagerConfiguration> syncManagers;
    syncManagers.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Sync Manager %1 for '%2' must be a JSON object.").arg(index).arg(slaveName));
        }
        const QJsonObject object = array->at(index).toObject();
        const QString objectName = Tr::tr("Sync Manager %1 for '%2'").arg(index).arg(slaveName);
        static const QSet<QString> keys{
            "id",
            "index",
            "name",
            "direction",
            "enabled",
            "sizeLimitBytes",
        };
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(objectName));
        const auto id = parseUniqueId(object, "id", objectName, uniqueIds);
        const auto managerIndex
            = parseUnsigned(object, "index", objectName, std::numeric_limits<int>::max());
        const auto name = parseString(object, "name", objectName);
        const auto direction = parseSyncManagerDirection(object, objectName);
        const auto enabled = parseBool(object, "enabled", objectName);
        const auto sizeLimit
            = parseUnsigned(object, "sizeLimitBytes", objectName, std::numeric_limits<int>::max());
        if (!id || !managerIndex || !name || !direction || !enabled || !sizeLimit) {
            const QString error = !id             ? id.error()
                                  : !managerIndex ? managerIndex.error()
                                  : !name         ? name.error()
                                  : !direction    ? direction.error()
                                  : !enabled      ? enabled.error()
                                                  : sizeLimit.error();
            return Utils::ResultError(error);
        }
        syncManagers.append({*id, int(*managerIndex), *name, *direction, *enabled, int(*sizeLimit)});
    }
    return syncManagers;
}

static Utils::Result<QList<Data::PdoEntryConfiguration>> parsePdoEntries(
    const QJsonObject &pdoObject, const QString &pdoName, QSet<Data::NodeId> *uniqueIds)
{
    const auto array = parseArray(pdoObject, "entries", pdoName);
    if (!array)
        return Utils::ResultError(array.error());

    QList<Data::PdoEntryConfiguration> entries;
    entries.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("PDO entry %1 in %2 must be a JSON object.").arg(index).arg(pdoName));
        }
        const QJsonObject object = array->at(index).toObject();
        const QString objectName = Tr::tr("PDO entry %1 in %2").arg(index).arg(pdoName);
        static const QSet<QString> keys{
            "id",
            "index",
            "subIndex",
            "name",
            "bitLength",
            "dataType",
            "rawDataType",
            "requestedBitOffset",
            "mappingSupported",
            "padding",
        };
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(objectName));
        const auto id = parseUniqueId(object, "id", objectName, uniqueIds);
        const auto objectIndex
            = parseUnsigned(object, "index", objectName, std::numeric_limits<quint16>::max());
        const auto subIndex
            = parseUnsigned(object, "subIndex", objectName, std::numeric_limits<quint8>::max());
        const auto name = parseString(object, "name", objectName);
        const auto bitLength
            = parseUnsigned(object, "bitLength", objectName, std::numeric_limits<int>::max());
        const auto dataType = parseDataType(object, objectName);
        const auto rawDataType = parseString(object, "rawDataType", objectName);
        constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
        const auto requestedBitOffset
            = parseSigned(object, "requestedBitOffset", objectName, -1, maximumExactJsonInteger);
        const auto mappingSupported = parseBool(object, "mappingSupported", objectName);
        const auto padding = parseBool(object, "padding", objectName);
        if (!id || !objectIndex || !subIndex || !name || !bitLength || !dataType || !rawDataType
            || !requestedBitOffset || !mappingSupported || !padding) {
            const QString error = !id                   ? id.error()
                                  : !objectIndex        ? objectIndex.error()
                                  : !subIndex           ? subIndex.error()
                                  : !name               ? name.error()
                                  : !bitLength          ? bitLength.error()
                                  : !dataType           ? dataType.error()
                                  : !rawDataType        ? rawDataType.error()
                                  : !requestedBitOffset ? requestedBitOffset.error()
                                  : !mappingSupported   ? mappingSupported.error()
                                                        : padding.error();
            return Utils::ResultError(error);
        }
        entries.append(
            {*id,
             quint16(*objectIndex),
             quint8(*subIndex),
             *name,
             int(*bitLength),
             *dataType,
             *rawDataType,
             *requestedBitOffset,
             *mappingSupported,
             *padding});
    }
    return entries;
}

static Utils::Result<QList<Data::PdoConfiguration>> parsePdos(
    const QJsonObject &processDataObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto array = parseArray(processDataObject, "pdos", slaveName);
    if (!array)
        return Utils::ResultError(array.error());

    QList<Data::PdoConfiguration> pdos;
    pdos.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("PDO %1 for '%2' must be a JSON object.").arg(index).arg(slaveName));
        }
        const QJsonObject object = array->at(index).toObject();
        const QString objectName = Tr::tr("PDO %1 for '%2'").arg(index).arg(slaveName);
        static const QSet<QString> keys{
            "id",
            "index",
            "name",
            "direction",
            "syncManager",
            "selected",
            "fixed",
            "mandatory",
            "defaultSelected",
            "mappingSupported",
            "predefinedGroup",
            "entries",
        };
        if (object.size() != keys.size() || !hasOnlyKeys(object, keys))
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(objectName));
        const auto id = parseUniqueId(object, "id", objectName, uniqueIds);
        const auto pdoIndex
            = parseUnsigned(object, "index", objectName, std::numeric_limits<quint16>::max());
        const auto name = parseString(object, "name", objectName);
        const auto direction = parsePdoDirection(object, objectName);
        const auto syncManager
            = parseSigned(object, "syncManager", objectName, -1, std::numeric_limits<int>::max());
        const auto selected = parseBool(object, "selected", objectName);
        const auto fixed = parseBool(object, "fixed", objectName);
        const auto mandatory = parseBool(object, "mandatory", objectName);
        const auto defaultSelected = parseBool(object, "defaultSelected", objectName);
        const auto mappingSupported = parseBool(object, "mappingSupported", objectName);
        const auto predefinedGroup = parseString(object, "predefinedGroup", objectName);
        const auto entries = parsePdoEntries(object, objectName, uniqueIds);
        if (!id || !pdoIndex || !name || !direction || !syncManager || !selected || !fixed
            || !mandatory || !defaultSelected || !mappingSupported || !predefinedGroup
            || !entries) {
            const QString error = !id                 ? id.error()
                                  : !pdoIndex         ? pdoIndex.error()
                                  : !name             ? name.error()
                                  : !direction        ? direction.error()
                                  : !syncManager      ? syncManager.error()
                                  : !selected         ? selected.error()
                                  : !fixed            ? fixed.error()
                                  : !mandatory        ? mandatory.error()
                                  : !defaultSelected  ? defaultSelected.error()
                                  : !mappingSupported ? mappingSupported.error()
                                  : !predefinedGroup  ? predefinedGroup.error()
                                                      : entries.error();
            return Utils::ResultError(error);
        }
        pdos.append(
            {*id,
             quint16(*pdoIndex),
             *name,
             *direction,
             int(*syncManager),
             *selected,
             *fixed,
             *mandatory,
             *defaultSelected,
             *mappingSupported,
             *predefinedGroup,
             *entries});
    }
    return pdos;
}

static Utils::Result<Data::ProcessDataConfiguration> parseProcessDataConfiguration(
    const QJsonObject &configurationObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto object = parseObject(configurationObject, "processData", slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    static const QSet<QString> keys{"syncManagers", "pdos"};
    if (object->size() != keys.size() || !hasOnlyKeys(*object, keys)) {
        return Utils::ResultError(
            Tr::tr("Process Data for '%1' has an invalid shape.").arg(slaveName));
    }
    const auto syncManagers = parseSyncManagers(*object, slaveName, uniqueIds);
    if (!syncManagers)
        return Utils::ResultError(syncManagers.error());
    const auto pdos = parsePdos(*object, slaveName, uniqueIds);
    if (!pdos)
        return Utils::ResultError(pdos.error());

    const Data::ProcessDataConfiguration processData{*syncManagers, *pdos};
    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        processData);
    const auto error = std::find_if(
        validation.issues.cbegin(),
        validation.issues.cend(),
        [](const Data::ConfigurationIssue &issue) {
            return issue.severity == Data::ConfigurationIssueSeverity::Error;
        });
    if (error != validation.issues.cend()) {
        return Utils::ResultError(
            Tr::tr("Process Data for '%1' is invalid: %2").arg(slaveName, error->message));
    }
    return processData;
}

static bool isHexString(const QString &value)
{
    return value.size() % 2 == 0 && std::all_of(value.cbegin(), value.cend(), [](QChar character) {
               const ushort codePoint = character.unicode();
               return (codePoint >= '0' && codePoint <= '9')
                      || (codePoint >= 'a' && codePoint <= 'f')
                      || (codePoint >= 'A' && codePoint <= 'F');
           });
}

static Utils::Result<Data::StartupConfiguration> parseStartupConfiguration(
    const QJsonObject &configurationObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto object = parseObject(configurationObject, "startup", slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    static const QSet<QString> keys{"parameters"};
    if (object->size() != keys.size() || !hasOnlyKeys(*object, keys))
        return Utils::ResultError(Tr::tr("Startup for '%1' has an invalid shape.").arg(slaveName));
    const auto array = parseArray(*object, "parameters", slaveName);
    if (!array)
        return Utils::ResultError(array.error());

    Data::StartupConfiguration startup;
    startup.parameters.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Startup parameter %1 for '%2' must be a JSON object.")
                    .arg(index)
                    .arg(slaveName));
        }
        const QJsonObject parameterObject = array->at(index).toObject();
        const QString objectName = Tr::tr("Startup parameter %1 for '%2'").arg(index).arg(slaveName);
        static const QSet<QString> parameterKeys{
            "id",
            "enabled",
            "order",
            "transition",
            "index",
            "subIndex",
            "dataType",
            "rawDataType",
            "rawValueHex",
            "comment",
        };
        if (parameterObject.size() != parameterKeys.size()
            || !hasOnlyKeys(parameterObject, parameterKeys)) {
            return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(objectName));
        }
        const auto id = parseUniqueId(parameterObject, "id", objectName, uniqueIds);
        const auto enabled = parseBool(parameterObject, "enabled", objectName);
        const auto order
            = parseSigned(parameterObject, "order", objectName, -1, std::numeric_limits<int>::max());
        const auto transition = parseString(parameterObject, "transition", objectName);
        const auto objectIndex = parseUnsigned(
            parameterObject, "index", objectName, std::numeric_limits<quint16>::max());
        const auto subIndex = parseUnsigned(
            parameterObject, "subIndex", objectName, std::numeric_limits<quint8>::max());
        const auto dataType = parseDataType(parameterObject, objectName);
        const auto rawDataType = parseString(parameterObject, "rawDataType", objectName);
        const auto rawValue = parseString(parameterObject, "rawValueHex", objectName);
        const auto comment = parseString(parameterObject, "comment", objectName);
        if (!id || !enabled || !order || !transition || !objectIndex || !subIndex || !dataType
            || !rawDataType || !rawValue || !comment) {
            const QString error = !id            ? id.error()
                                  : !enabled     ? enabled.error()
                                  : !order       ? order.error()
                                  : !transition  ? transition.error()
                                  : !objectIndex ? objectIndex.error()
                                  : !subIndex    ? subIndex.error()
                                  : !dataType    ? dataType.error()
                                  : !rawDataType ? rawDataType.error()
                                  : !rawValue    ? rawValue.error()
                                                 : comment.error();
            return Utils::ResultError(error);
        }
        if (!isHexString(*rawValue)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid hexadecimal raw value.").arg(objectName));
        }
        startup.parameters.append(
            {*id,
             *enabled,
             int(*order),
             *transition,
             quint16(*objectIndex),
             quint8(*subIndex),
             *dataType,
             *rawDataType,
             QByteArray::fromHex(rawValue->toLatin1()),
             *comment});
    }

    const QList<Data::ConfigurationIssue> issues = Data::validateStartupConfiguration(startup);
    const auto error
        = std::find_if(issues.cbegin(), issues.cend(), [](const Data::ConfigurationIssue &issue) {
              return issue.severity == Data::ConfigurationIssueSeverity::Error;
          });
    if (error != issues.cend()) {
        return Utils::ResultError(
            Tr::tr("Startup for '%1' is invalid: %2").arg(slaveName, error->message));
    }
    return startup;
}

static Utils::Result<Data::DcSignalConfiguration> parseDcSignal(
    const QJsonObject &dcObject, const QString &key, const QString &slaveName)
{
    const auto object = parseObject(dcObject, key, slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    const QString objectName = key.toUpper() + Tr::tr(" for '%1'").arg(slaveName);
    static const QSet<QString> keys{"enabled", "cycleTimeNs", "shiftTimeNs"};
    if (object->size() != keys.size() || !hasOnlyKeys(*object, keys))
        return Utils::ResultError(Tr::tr("%1 has an invalid shape.").arg(objectName));
    const auto enabled = parseBool(*object, "enabled", objectName);
    constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
    const auto cycle = parseSigned(
        *object, "cycleTimeNs", objectName, -maximumExactJsonInteger, maximumExactJsonInteger);
    const auto shift = parseSigned(
        *object, "shiftTimeNs", objectName, -maximumExactJsonInteger, maximumExactJsonInteger);
    if (!enabled || !cycle || !shift) {
        const QString error = !enabled ? enabled.error() : !cycle ? cycle.error() : shift.error();
        return Utils::ResultError(error);
    }
    return Data::DcSignalConfiguration{*enabled, *cycle, *shift};
}

static Utils::Result<Data::DcConfiguration> parseDcConfiguration(
    const QJsonObject &configurationObject, const QString &slaveName)
{
    const auto object = parseObject(configurationObject, "dc", slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    static const QSet<QString> keys{
        "enabled",
        "modeName",
        "assignActivate",
        "sync0",
        "sync1",
        "potentialReferenceClock",
    };
    if (object->size() != keys.size() || !hasOnlyKeys(*object, keys))
        return Utils::ResultError(Tr::tr("DC for '%1' has an invalid shape.").arg(slaveName));
    const auto enabled = parseBool(*object, "enabled", slaveName);
    const auto modeName = parseString(*object, "modeName", slaveName);
    const auto assignActivate
        = parseUnsigned(*object, "assignActivate", slaveName, std::numeric_limits<quint16>::max());
    const auto sync0 = parseDcSignal(*object, "sync0", slaveName);
    const auto sync1 = parseDcSignal(*object, "sync1", slaveName);
    const auto potentialReferenceClock = parseBool(*object, "potentialReferenceClock", slaveName);
    if (!enabled || !modeName || !assignActivate || !sync0 || !sync1 || !potentialReferenceClock) {
        const QString error = !enabled          ? enabled.error()
                              : !modeName       ? modeName.error()
                              : !assignActivate ? assignActivate.error()
                              : !sync0          ? sync0.error()
                              : !sync1          ? sync1.error()
                                                : potentialReferenceClock.error();
        return Utils::ResultError(error);
    }
    const Data::DcConfiguration
        dc{*enabled, *modeName, quint16(*assignActivate), *sync0, *sync1, *potentialReferenceClock};
    const QList<Data::ConfigurationIssue> issues = Data::validateDcConfiguration(dc);
    const auto error
        = std::find_if(issues.cbegin(), issues.cend(), [](const Data::ConfigurationIssue &issue) {
              return issue.severity == Data::ConfigurationIssueSeverity::Error;
          });
    if (error != issues.cend()) {
        return Utils::ResultError(
            Tr::tr("DC for '%1' is invalid: %2").arg(slaveName, error->message));
    }
    return dc;
}

static Utils::Result<QList<Data::OfflineSlaveConfiguration>> parseOfflineSlaves(
    const QJsonObject &masterObject,
    const Data::NodeId &masterId,
    QSet<Data::NodeId> *uniqueIds,
    int version)
{
    const bool parseConfiguration = version >= 2;
    const bool parseAdapterData = version >= 4;
    const bool parseManualControl = version >= 5;
    const QJsonValue slavesValue = masterObject.value("slaves");
    if (slavesValue.isUndefined() && !parseConfiguration)
        return QList<Data::OfflineSlaveConfiguration>();
    if (!slavesValue.isArray())
        return Utils::ResultError(Tr::tr("Master 'slaves' must be a JSON array."));

    QList<Data::OfflineSlaveConfiguration> slaves;
    QSet<int> positions;
    const QJsonArray array = slavesValue.toArray();
    slaves.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            return Utils::ResultError(Tr::tr("Offline slave %1 must be a JSON object.").arg(index));
        }
        const QJsonObject object = array.at(index).toObject();
        const QString objectName = Tr::tr("Offline slave %1").arg(index);
        QSet<QString> slaveKeys{
            "id",
            "name",
            "position",
            "vendorId",
            "productCode",
            "revisionNumber",
            "serialNumber",
            "alias",
            "deviceDescriptionId",
        };
        if (parseConfiguration)
            slaveKeys.insert("configuration");
        if (parseAdapterData) {
            slaveKeys.insert("esiSha256");
            slaveKeys.insert("adapterSelection");
        }
        if (parseManualControl)
            slaveKeys.insert("manualControlEnvelope");
        if (const Utils::Result<> shape = rejectUnknownKeys(object, slaveKeys, objectName);
            !shape) {
            return Utils::ResultError(shape.error());
        }
        const auto id = parseRequiredId(object, "id", objectName);
        if (!id)
            return Utils::ResultError(id.error());
        if (uniqueIds->contains(*id))
            return Utils::ResultError(Tr::tr("EtherCAT project node IDs must be unique."));
        const auto name = parseRequiredName(object, objectName);
        if (!name)
            return Utils::ResultError(name.error());
        const auto position
            = parseUnsigned(object, "position", objectName, std::numeric_limits<int>::max());
        if (!position)
            return Utils::ResultError(position.error());
        if (positions.contains(int(*position)))
            return Utils::ResultError(Tr::tr("Offline slave positions must be unique."));
        const auto vendorId = parseUnsigned(object, "vendorId", objectName);
        const auto productCode = parseUnsigned(object, "productCode", objectName);
        const auto revisionNumber = parseUnsigned(object, "revisionNumber", objectName);
        const auto serialNumber = parseUnsigned(object, "serialNumber", objectName);
        const auto alias
            = parseUnsigned(object, "alias", objectName, std::numeric_limits<quint16>::max());
        if (!vendorId || !productCode || !revisionNumber || !serialNumber || !alias) {
            const QString error = !vendorId         ? vendorId.error()
                                  : !productCode    ? productCode.error()
                                  : !revisionNumber ? revisionNumber.error()
                                  : !serialNumber   ? serialNumber.error()
                                                    : alias.error();
            return Utils::ResultError(error);
        }
        if (*vendorId == 0 || *productCode == 0) {
            return Utils::ResultError(
                Tr::tr("%1 requires non-zero Vendor ID and Product Code values.").arg(objectName));
        }

        Data::NodeId descriptionId;
        const QJsonValue descriptionIdValue = object.value("deviceDescriptionId");
        if (!descriptionIdValue.isUndefined() && !descriptionIdValue.isString()) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid device description ID.").arg(objectName));
        }
        const QString descriptionIdText = descriptionIdValue.toString();
        if (!descriptionIdText.isEmpty()) {
            descriptionId = Data::NodeId::fromString(descriptionIdText);
            if (descriptionId.isNull()) {
                return Utils::ResultError(
                    Tr::tr("%1 has an invalid device description ID.").arg(objectName));
            }
        }
        uniqueIds->insert(*id);

        Data::ProcessDataConfiguration processData;
        Data::StartupConfiguration startup;
        Data::DcConfiguration dc;
        if (parseConfiguration) {
            const auto configuration = parseObject(object, "configuration", objectName);
            if (!configuration)
                return Utils::ResultError(configuration.error());
            static const QSet<QString> configurationKeys{"processData", "startup", "dc"};
            if (configuration->size() != configurationKeys.size()
                || !hasOnlyKeys(*configuration, configurationKeys)) {
                return Utils::ResultError(
                    Tr::tr("%1 has an invalid configuration shape.").arg(objectName));
            }
            const auto parsedProcessData
                = parseProcessDataConfiguration(*configuration, *name, uniqueIds);
            if (!parsedProcessData)
                return Utils::ResultError(parsedProcessData.error());
            const auto parsedStartup = parseStartupConfiguration(*configuration, *name, uniqueIds);
            if (!parsedStartup)
                return Utils::ResultError(parsedStartup.error());
            const auto parsedDc = parseDcConfiguration(*configuration, *name);
            if (!parsedDc)
                return Utils::ResultError(parsedDc.error());
            processData = *parsedProcessData;
            startup = *parsedStartup;
            dc = *parsedDc;
        }

        QByteArray esiSha256;
        Data::DeviceAdapterProjectSelection adapterSelection;
        if (parseAdapterData) {
            const QJsonValue esiValue = object.value("esiSha256");
            if (!esiValue.isUndefined()) {
                const auto parsedEsiSha256 = parseSha256(object, "esiSha256", objectName);
                if (!parsedEsiSha256)
                    return Utils::ResultError(parsedEsiSha256.error());
                esiSha256 = *parsedEsiSha256;
            }
            const auto parsedSelection = parseAdapterSelection(object, objectName, esiSha256);
            if (!parsedSelection)
                return Utils::ResultError(parsedSelection.error());
            adapterSelection = *parsedSelection;
        }

        Data::ManualControlEnvelope manualControlEnvelope;
        if (parseManualControl) {
            const auto parsedEnvelope = parseManualControlEnvelope(object, objectName);
            if (!parsedEnvelope)
                return Utils::ResultError(parsedEnvelope.error());
            manualControlEnvelope = *parsedEnvelope;
            const bool hasManualControl = manualControlEnvelope.enabled
                                          || !manualControlEnvelope.signalEnvelopes.isEmpty()
                                          || !manualControlEnvelope.actionEnvelopes.isEmpty();
            const bool hasAdapterSelection = !adapterSelection.adapterId.value.isEmpty()
                                             || !adapterSelection.adapterVersion.isEmpty()
                                             || !adapterSelection.adapterContentSha256.isEmpty()
                                             || !adapterSelection.processDataProfileId.isEmpty()
                                             || !adapterSelection.moduleAssignments.isEmpty();
            if (hasManualControl && !hasAdapterSelection) {
                return Utils::ResultError(
                    Tr::tr("%1 manual control requires an exact adapter selection.")
                        .arg(objectName));
            }
        }

        positions.insert(int(*position));
        slaves.append(
            {*id,
             masterId,
             int(*position),
             {*vendorId, *productCode, *revisionNumber},
             *serialNumber,
             quint16(*alias),
             *name,
             descriptionId,
             processData,
             startup,
             dc,
             esiSha256,
             adapterSelection,
             manualControlEnvelope});
    }
    std::sort(slaves.begin(), slaves.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return slaves;
}

Data::ProjectSnapshot createProjectSnapshot(const QString &name, const QString &createdBy)
{
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    const QString projectName = name.trimmed().isEmpty() ? Tr::tr("EtherCAT Project")
                                                         : name.trimmed();

    return {
        projectId,
        projectName,
        Constants::CURRENT_FORMAT_VERSION,
        createdBy,
        {{projectId, {}, Data::ProjectNodeKind::Project, projectName},
         {targetId, projectId, Data::ProjectNodeKind::Target, Tr::tr("Target Controller")},
         {masterId, targetId, Data::ProjectNodeKind::Master, Tr::tr("EtherCAT Master")}},
        false,
        true,
        false,
        {},
        {},
        {},
        {},
    };
}

static Utils::Result<LoadedProject> parseVersionZero(
    const QJsonObject &root, const QString &fallbackName)
{
    Data::ProjectSnapshot snapshot = createProjectSnapshot(
        root.value("name").toString(fallbackName), root.value("createdBy").toString());

    if (root.contains("id")) {
        const Data::NodeId projectId = Data::NodeId::fromString(root.value("id").toString());
        if (projectId.isNull())
            return Utils::ResultError(Tr::tr("Version 0 project has an invalid project ID."));
        snapshot.id = projectId;
        snapshot.nodes[0].id = projectId;
        snapshot.nodes[1].parentId = projectId;
    }

    snapshot.migrated = true;
    return LoadedProject{snapshot, true, 0};
}

Utils::Result<LoadedProject> parseProject(const QByteArray &contents, const QString &fallbackName)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return Utils::ResultError(
            Tr::tr("Invalid JSON at offset %1: %2")
                .arg(parseError.offset)
                .arg(parseError.errorString()));
    }
    if (const Utils::Result<> duplicateKeys = rejectDuplicateJsonKeys(contents); !duplicateKeys) {
        return Utils::ResultError(duplicateKeys.error());
    }
    if (!document.isObject())
        return Utils::ResultError(Tr::tr("The EtherCAT project root must be a JSON object."));

    const QJsonObject root = document.object();
    if (root.value("format").toString() != QLatin1StringView(FORMAT_NAME))
        return Utils::ResultError(Tr::tr("The file is not an EtherCAT project."));

    const int version = root.value("formatVersion").toInt(root.value("version").toInt(-1));
    if (version == 0) {
        static const QSet<QString> versionZeroKeys{
            "format",
            "version",
            "formatVersion",
            "id",
            "name",
            "createdBy",
        };
        if (root.contains("version") && root.contains("formatVersion")) {
            return Utils::ResultError(
                Tr::tr("Version 0 must use exactly one format-version field."));
        }
        if (const Utils::Result<> shape
            = rejectUnknownKeys(root, versionZeroKeys, Tr::tr("Version 0 project"));
            !shape) {
            return Utils::ResultError(shape.error());
        }
        return parseVersionZero(root, fallbackName);
    }
    if (version != 1 && version != 2 && version != 3 && version != 4 && version != 5
        && version != Constants::CURRENT_FORMAT_VERSION) {
        return Utils::ResultError(
            Tr::tr("Unsupported EtherCAT project format version %1.").arg(version));
    }

    static const QSet<QString> rootKeys{
        "format",
        "formatVersion",
        "project",
        "target",
        "master",
    };
    if (root.size() != rootKeys.size() || !hasOnlyKeys(root, rootKeys))
        return Utils::ResultError(Tr::tr("The EtherCAT project root has an invalid shape."));

    const auto projectValue = parseObject(root, "project", Tr::tr("EtherCAT project root"));
    const auto targetValue = parseObject(root, "target", Tr::tr("EtherCAT project root"));
    const auto masterValue = parseObject(root, "master", Tr::tr("EtherCAT project root"));
    if (!projectValue || !targetValue || !masterValue) {
        const QString error = !projectValue ? projectValue.error()
                              : !targetValue ? targetValue.error()
                                             : masterValue.error();
        return Utils::ResultError(error);
    }
    const QJsonObject projectObject = *projectValue;
    const QJsonObject targetObject = *targetValue;
    const QJsonObject masterObject = *masterValue;
    static const QSet<QString> projectKeys{"id", "name", "createdBy"};
    static const QSet<QString> targetKeys{"id", "name"};
    if (projectObject.size() != projectKeys.size() || !hasOnlyKeys(projectObject, projectKeys))
        return Utils::ResultError(Tr::tr("Project has an invalid shape."));
    if (targetObject.size() != targetKeys.size() || !hasOnlyKeys(targetObject, targetKeys))
        return Utils::ResultError(Tr::tr("Target has an invalid shape."));

    QSet<QString> masterKeys{"id", "name", "slaves"};
    if (version >= 3)
        masterKeys.insert("configuration");
    if (version >= 4)
        masterKeys.insert("semanticBindingArtifact");
    if (const Utils::Result<> shape
        = rejectUnknownKeys(masterObject, masterKeys, Tr::tr("Master"));
        !shape) {
        return Utils::ResultError(shape.error());
    }

    const auto projectId = parseRequiredId(projectObject, "id", Tr::tr("Project"));
    if (!projectId)
        return Utils::ResultError(projectId.error());
    const auto targetId = parseRequiredId(targetObject, "id", Tr::tr("Target"));
    if (!targetId)
        return Utils::ResultError(targetId.error());
    const auto masterId = parseRequiredId(masterObject, "id", Tr::tr("Master"));
    if (!masterId)
        return Utils::ResultError(masterId.error());

    QSet<Data::NodeId> uniqueIds{*projectId, *targetId, *masterId};
    if (uniqueIds.size() != 3)
        return Utils::ResultError(Tr::tr("Project, target, and master IDs must be unique."));

    const auto projectName = parseRequiredName(projectObject, Tr::tr("Project"));
    if (!projectName)
        return Utils::ResultError(projectName.error());
    const auto targetName = parseRequiredName(targetObject, Tr::tr("Target"));
    if (!targetName)
        return Utils::ResultError(targetName.error());
    const auto masterName = parseRequiredName(masterObject, Tr::tr("Master"));
    if (!masterName)
        return Utils::ResultError(masterName.error());

    Data::MasterConfiguration masterConfiguration;
    if (version >= 3) {
        const auto parsedMasterConfiguration = parseMasterConfiguration(masterObject);
        if (!parsedMasterConfiguration)
            return Utils::ResultError(parsedMasterConfiguration.error());
        masterConfiguration = *parsedMasterConfiguration;
    }

    const auto slaves = parseOfflineSlaves(masterObject, *masterId, &uniqueIds, version);
    if (!slaves)
        return Utils::ResultError(slaves.error());

    Data::SemanticBindingArtifactReference bindingArtifact;
    if (version >= 4) {
        const auto parsedBindingArtifact = parseBindingArtifact(
            masterObject, *slaves, *masterId, version >= 6);
        if (!parsedBindingArtifact)
            return Utils::ResultError(parsedBindingArtifact.error());
        if (version >= 5)
            bindingArtifact = *parsedBindingArtifact;
    }

    const bool migrationRequired = version != Constants::CURRENT_FORMAT_VERSION;
    Data::ProjectSnapshot snapshot{
        *projectId,
        *projectName,
        Constants::CURRENT_FORMAT_VERSION,
        projectObject.value("createdBy").toString(),
        {{*projectId, {}, Data::ProjectNodeKind::Project, *projectName},
         {*targetId, *projectId, Data::ProjectNodeKind::Target, *targetName},
         {*masterId, *targetId, Data::ProjectNodeKind::Master, *masterName}},
        false,
        true,
        migrationRequired,
        {},
        *slaves,
        masterConfiguration,
        bindingArtifact,
    };
    for (const Data::OfflineSlaveConfiguration &slave : *slaves) {
        snapshot.nodes.append({slave.id, slave.masterId, Data::ProjectNodeKind::Slave, slave.name});
    }
    return LoadedProject{snapshot, migrationRequired, version};
}

static QJsonObject serializeProcessData(const Data::ProcessDataConfiguration &processData)
{
    QJsonArray syncManagers;
    for (const Data::SyncManagerConfiguration &syncManager : processData.syncManagers) {
        QJsonObject object;
        object.insert("id", syncManager.id.toString());
        object.insert("index", syncManager.index);
        object.insert("name", syncManager.name);
        object.insert("direction", syncManagerDirectionName(syncManager.direction));
        object.insert("enabled", syncManager.enabled);
        object.insert("sizeLimitBytes", syncManager.sizeLimitBytes);
        syncManagers.append(object);
    }

    QJsonArray pdos;
    for (const Data::PdoConfiguration &pdo : processData.pdos) {
        QJsonArray entries;
        for (const Data::PdoEntryConfiguration &entry : pdo.entries) {
            QJsonObject object;
            object.insert("id", entry.id.toString());
            object.insert("index", entry.index);
            object.insert("subIndex", entry.subIndex);
            object.insert("name", entry.name);
            object.insert("bitLength", entry.bitLength);
            object.insert("dataType", dataTypeName(entry.dataType));
            object.insert("rawDataType", entry.rawDataType);
            object.insert("requestedBitOffset", double(entry.requestedBitOffset));
            object.insert("mappingSupported", entry.mappingSupported);
            object.insert("padding", entry.padding);
            entries.append(object);
        }

        QJsonObject object;
        object.insert("id", pdo.id.toString());
        object.insert("index", pdo.index);
        object.insert("name", pdo.name);
        object.insert("direction", pdoDirectionName(pdo.direction));
        object.insert("syncManager", pdo.syncManager);
        object.insert("selected", pdo.selected);
        object.insert("fixed", pdo.fixed);
        object.insert("mandatory", pdo.mandatory);
        object.insert("defaultSelected", pdo.defaultSelected);
        object.insert("mappingSupported", pdo.mappingSupported);
        object.insert("predefinedGroup", pdo.predefinedGroup);
        object.insert("entries", entries);
        pdos.append(object);
    }

    QJsonObject object;
    object.insert("syncManagers", syncManagers);
    object.insert("pdos", pdos);
    return object;
}

static QJsonObject serializeStartup(const Data::StartupConfiguration &startup)
{
    QJsonArray parameters;
    for (const Data::StartupParameterConfiguration &parameter : startup.parameters) {
        QJsonObject object;
        object.insert("id", parameter.id.toString());
        object.insert("enabled", parameter.enabled);
        object.insert("order", parameter.order);
        object.insert("transition", parameter.transition);
        object.insert("index", parameter.index);
        object.insert("subIndex", parameter.subIndex);
        object.insert("dataType", dataTypeName(parameter.dataType));
        object.insert("rawDataType", parameter.rawDataType);
        object.insert("rawValueHex", QString::fromLatin1(parameter.rawValue.toHex()));
        object.insert("comment", parameter.comment);
        parameters.append(object);
    }
    QJsonObject object;
    object.insert("parameters", parameters);
    return object;
}

static QJsonObject serializeDcSignal(const Data::DcSignalConfiguration &signal)
{
    QJsonObject object;
    object.insert("enabled", signal.enabled);
    object.insert("cycleTimeNs", double(signal.cycleTimeNs));
    object.insert("shiftTimeNs", double(signal.shiftTimeNs));
    return object;
}

static QJsonObject serializeDc(const Data::DcConfiguration &dc)
{
    QJsonObject object;
    object.insert("enabled", dc.enabled);
    object.insert("modeName", dc.modeName);
    object.insert("assignActivate", dc.assignActivate);
    object.insert("sync0", serializeDcSignal(dc.sync0));
    object.insert("sync1", serializeDcSignal(dc.sync1));
    object.insert("potentialReferenceClock", dc.potentialReferenceClock);
    return object;
}

static bool adapterSelectionIsEmpty(const Data::DeviceAdapterProjectSelection &selection)
{
    return selection.adapterId.value.isEmpty() && selection.adapterVersion.isEmpty()
           && selection.adapterContentSha256.isEmpty()
           && selection.processDataProfileId.isEmpty() && selection.moduleAssignments.isEmpty();
}

static QJsonObject serializeAdapterSelection(
    const Data::DeviceAdapterProjectSelection &selection)
{
    QJsonArray modules;
    QList<Data::DeviceModuleAssignment> sortedModules = selection.moduleAssignments;
    std::sort(sortedModules.begin(), sortedModules.end(), [](const auto &left, const auto &right) {
        return left.slot < right.slot;
    });
    for (const Data::DeviceModuleAssignment &assignment : std::as_const(sortedModules)) {
        QJsonObject module;
        module.insert("slot", assignment.slot);
        module.insert("moduleIdent", double(assignment.moduleIdent));
        module.insert("objectIndexOffset", assignment.objectIndexOffset);
        module.insert("pdoIndexOffset", assignment.pdoIndexOffset);
        modules.append(module);
    }

    QJsonObject object;
    object.insert("adapterId", selection.adapterId.value);
    object.insert("adapterVersion", selection.adapterVersion);
    object.insert(
        "adapterContentSha256", QString::fromLatin1(selection.adapterContentSha256.toHex()));
    object.insert("processDataProfileId", selection.processDataProfileId);
    object.insert("moduleAssignments", modules);
    return object;
}

static QJsonObject serializeExactRational(const Data::ExactRational &rational)
{
    QJsonObject object;
    object.insert("numerator", QString::number(rational.numerator));
    object.insert("denominator", QString::number(rational.denominator));
    return object;
}

static QJsonObject serializeEngineeringValue(const Data::EngineeringValue &value)
{
    QJsonObject object;
    switch (value.kind) {
    case Data::EngineeringValueKind::Boolean:
        object.insert("kind", "boolean");
        object.insert("boolean", value.boolean);
        break;
    case Data::EngineeringValueKind::SignedInteger:
        object.insert("kind", "signed-integer");
        object.insert("signedInteger", QString::number(value.signedInteger));
        break;
    case Data::EngineeringValueKind::UnsignedInteger:
        object.insert("kind", "unsigned-integer");
        object.insert("unsignedInteger", QString::number(value.unsignedInteger));
        break;
    case Data::EngineeringValueKind::ExactRational:
        object.insert("kind", "exact-rational");
        object.insert("exactRational", serializeExactRational(value.rational));
        break;
    case Data::EngineeringValueKind::Enumeration:
        object.insert("kind", "enumeration");
        object.insert("enumerationName", value.enumerationName);
        break;
    case Data::EngineeringValueKind::Invalid:
        break;
    }
    return object;
}

static QJsonValue serializeOptionalEngineeringValue(
    const std::optional<Data::EngineeringValue> &value)
{
    return value ? QJsonValue(serializeEngineeringValue(*value))
                 : QJsonValue(QJsonValue::Null);
}

static QJsonValue serializeOptionalRational(const std::optional<Data::ExactRational> &value)
{
    return value ? QJsonValue(serializeExactRational(*value))
                 : QJsonValue(QJsonValue::Null);
}

static QJsonObject serializeEngineeringConstraint(
    const Data::EngineeringConstraint &constraint)
{
    QJsonArray enumeration;
    QList<Data::EngineeringEnumerationValue> sortedEntries = constraint.enumeration;
    std::sort(
        sortedEntries.begin(), sortedEntries.end(), [](const auto &left, const auto &right) {
            return left.id < right.id;
        });
    for (const Data::EngineeringEnumerationValue &entry : std::as_const(sortedEntries)) {
        QJsonObject object;
        object.insert("id", entry.id);
        object.insert("displayName", entry.displayName);
        object.insert("value", serializeExactRational(entry.value));
        enumeration.append(object);
    }

    QJsonObject object;
    object.insert("minimum", serializeOptionalRational(constraint.minimum));
    object.insert("maximum", serializeOptionalRational(constraint.maximum));
    object.insert("step", serializeOptionalRational(constraint.step));
    object.insert("stepOrigin", serializeOptionalRational(constraint.stepOrigin));
    object.insert("enumeration", enumeration);
    return object;
}

static QJsonObject serializeManualTiming(const Data::ManualCommandTiming &timing)
{
    QJsonObject object;
    object.insert("commandTtlMs", double(timing.commandTtlMs));
    object.insert("refreshTimeoutMs", double(timing.refreshTimeoutMs));
    object.insert("maxContinuousHoldMs", double(timing.maxContinuousHoldMs));
    return object;
}

static QJsonObject serializeManualControlEnvelope(
    const Data::ManualControlEnvelope &envelope)
{
    QJsonArray signalArray;
    QList<Data::ManualSignalEnvelope> sortedSignals = envelope.signalEnvelopes;
    std::sort(sortedSignals.begin(), sortedSignals.end(), [](const auto &left, const auto &right) {
        return left.signalId.value < right.signalId.value;
    });
    for (const Data::ManualSignalEnvelope &signal : std::as_const(sortedSignals)) {
        QJsonArray peers;
        QList<Data::ManualSignalSafeValue> sortedPeers = signal.consistencyGroupSafeValues;
        std::sort(sortedPeers.begin(), sortedPeers.end(), [](const auto &left, const auto &right) {
            return left.signalId.value < right.signalId.value;
        });
        for (const Data::ManualSignalSafeValue &peer : std::as_const(sortedPeers)) {
            QJsonObject peerObject;
            peerObject.insert("signalId", peer.signalId.value);
            peerObject.insert("value", serializeEngineeringValue(peer.value));
            peers.append(peerObject);
        }

        QJsonObject object;
        object.insert("signalId", signal.signalId.value);
        object.insert("enabled", signal.enabled);
        object.insert("holdToRun", signal.holdToRun);
        object.insert("timing", serializeManualTiming(signal.timing));
        object.insert("allowedRange", serializeEngineeringConstraint(signal.allowedRange));
        object.insert("safeValue", serializeOptionalEngineeringValue(signal.safeValue));
        object.insert("consistencyGroupSafeValues", peers);
        signalArray.append(object);
    }

    QJsonArray actions;
    QList<Data::ManualActionEnvelope> sortedActions = envelope.actionEnvelopes;
    std::sort(sortedActions.begin(), sortedActions.end(), [](const auto &left, const auto &right) {
        return left.actionId.value < right.actionId.value;
    });
    for (const Data::ManualActionEnvelope &action : std::as_const(sortedActions)) {
        QJsonArray parameters;
        QList<Data::ManualActionParameterEnvelope> sortedParameters = action.parameters;
        std::sort(
            sortedParameters.begin(), sortedParameters.end(), [](const auto &left, const auto &right) {
                return left.parameterId < right.parameterId;
            });
        for (const Data::ManualActionParameterEnvelope &parameter :
             std::as_const(sortedParameters)) {
            QJsonObject parameterObject;
            parameterObject.insert("parameterId", parameter.parameterId);
            parameterObject.insert(
                "allowedRange", serializeEngineeringConstraint(parameter.allowedRange));
            parameterObject.insert(
                "defaultValue", serializeOptionalEngineeringValue(parameter.defaultValue));
            parameters.append(parameterObject);
        }

        QJsonObject object;
        object.insert("actionId", action.actionId.value);
        object.insert("enabled", action.enabled);
        object.insert("holdToRun", action.holdToRun);
        object.insert("timing", serializeManualTiming(action.timing));
        object.insert("parameters", parameters);
        object.insert("releaseActionId", action.releaseActionId.value);
        object.insert("timeoutActionId", action.timeoutActionId.value);
        object.insert("failureActionId", action.failureActionId.value);
        actions.append(object);
    }

    QJsonObject object;
    object.insert("enabled", envelope.enabled);
    object.insert("signalEnvelopes", signalArray);
    object.insert("actionEnvelopes", actions);
    return object;
}

static bool bindingArtifactIsEmpty(
    const Data::SemanticBindingArtifactReference &reference)
{
    return reference.artifactId.isEmpty() && reference.artifactSha256.isEmpty()
           && reference.projectConfigurationSha256.isEmpty()
           && reference.projectDeviceBindings.isEmpty();
}

static QJsonObject serializeBindingArtifact(
    const Data::SemanticBindingArtifactReference &reference)
{
    QJsonObject object;
    object.insert("artifactId", reference.artifactId);
    object.insert("artifactSha256", QString::fromLatin1(reference.artifactSha256.toHex()));
    object.insert(
        "projectConfigurationSha256",
        QString::fromLatin1(reference.projectConfigurationSha256.toHex()));
    QList<Data::SemanticProjectDeviceBinding> bindings = reference.projectDeviceBindings;
    std::sort(bindings.begin(), bindings.end(), [](const auto &left, const auto &right) {
        return left.slaveId.toString() < right.slaveId.toString();
    });
    QJsonArray bindingArray;
    for (const Data::SemanticProjectDeviceBinding &binding : std::as_const(bindings)) {
        QJsonObject bindingObject;
        bindingObject.insert("slaveId", binding.slaveId.toString());
        bindingObject.insert("projectDeviceId", binding.projectDeviceId);
        bindingArray.append(bindingObject);
    }
    object.insert("projectDeviceBindings", bindingArray);
    return object;
}

QByteArray serializeProject(const Data::ProjectSnapshot &snapshot)
{
    QJsonObject projectObject;
    projectObject.insert("id", snapshot.id.toString());
    projectObject.insert("name", snapshot.name);
    projectObject.insert("createdBy", snapshot.createdBy);

    QJsonObject targetObject;
    QJsonObject masterObject;
    for (const Data::ProjectNodeSnapshot &node : snapshot.nodes) {
        if (node.kind == Data::ProjectNodeKind::Target) {
            targetObject.insert("id", node.id.toString());
            targetObject.insert("name", node.name);
        } else if (node.kind == Data::ProjectNodeKind::Master) {
            masterObject.insert("id", node.id.toString());
            masterObject.insert("name", node.name);
        }
    }

    QJsonArray slaves;
    QList<Data::OfflineSlaveConfiguration> sortedSlaves = snapshot.slaves;
    std::sort(sortedSlaves.begin(), sortedSlaves.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(sortedSlaves)) {
        QJsonObject object;
        object.insert("id", slave.id.toString());
        object.insert("name", slave.name);
        object.insert("position", slave.position);
        object.insert("vendorId", double(slave.identity.vendorId));
        object.insert("productCode", double(slave.identity.productCode));
        object.insert("revisionNumber", double(slave.identity.revisionNumber));
        object.insert("serialNumber", double(slave.serialNumber));
        object.insert("alias", slave.alias);
        if (!slave.deviceDescriptionId.isNull())
            object.insert("deviceDescriptionId", slave.deviceDescriptionId.toString());
        if (!slave.esiSha256.isEmpty())
            object.insert("esiSha256", QString::fromLatin1(slave.esiSha256.toHex()));
        if (!adapterSelectionIsEmpty(slave.adapterSelection))
            object.insert("adapterSelection", serializeAdapterSelection(slave.adapterSelection));
        object.insert(
            "manualControlEnvelope",
            serializeManualControlEnvelope(slave.manualControlEnvelope));
        QJsonObject configuration;
        configuration.insert("processData", serializeProcessData(slave.processData));
        configuration.insert("startup", serializeStartup(slave.startup));
        configuration.insert("dc", serializeDc(slave.dc));
        object.insert("configuration", configuration);
        slaves.append(object);
    }
    QJsonObject masterConfiguration;
    masterConfiguration.insert(
        "timingMode", masterTimingModeName(snapshot.masterConfiguration.timingMode));
    masterConfiguration.insert("cyclePeriodNs", double(snapshot.masterConfiguration.cyclePeriodNs));
    masterObject.insert("configuration", masterConfiguration);
    masterObject.insert("slaves", slaves);
    if (!bindingArtifactIsEmpty(snapshot.masterBindingArtifact)) {
        masterObject.insert(
            "semanticBindingArtifact",
            serializeBindingArtifact(snapshot.masterBindingArtifact));
    }

    QJsonObject root;
    root.insert("format", QLatin1StringView(FORMAT_NAME));
    root.insert("formatVersion", Constants::CURRENT_FORMAT_VERSION);
    root.insert("project", projectObject);
    root.insert("target", targetObject);
    root.insert("master", masterObject);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace EtherCAT::Project::Internal
