// Copyright (C) 2026 Embed Labs

#include "semanticactiondefinitions_p.h"

#include "canonicaljson_p.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <variant>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr std::string_view actionDefinitionsFormat = "ethercat-semantic-action-definitions-v1";
constexpr std::string_view actionDefinitionsCanonicalization
    = "kvell-json-ascii-sorted-compact-lf-v1";
constexpr quint16 actionDefinitionsFormatVersion = 1;
constexpr qsizetype digestBytes = 32;

using IntegerValue = std::variant<qint64, quint64>;

Utils::ResultError definitionsError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Semantic action definition evidence error: %1").arg(detail));
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
    QString *error)
{
    if (!object.is_object()) {
        *error = QString::fromLatin1("%1 must be an object").arg(path);
        return false;
    }

    std::set<std::string_view> expected;
    for (std::string_view field : required) {
        expected.insert(field);
        if (!containsField(object, field)) {
            *error = QString::fromLatin1("%1 is required").arg(fieldPath(path, field));
            return false;
        }
    }
    for (auto iterator = object.cbegin(); iterator != object.cend(); ++iterator) {
        if (!expected.contains(iterator.key())) {
            *error = QString::fromLatin1("%1 contains an unknown field")
                         .arg(fieldPath(path, iterator.key()));
            return false;
        }
    }
    return true;
}

QString jsonString(const StrictJson &value)
{
    const std::string &text = value.get_ref<const std::string &>();
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
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

bool readInteger(const StrictJson &value, IntegerValue *result)
{
    if (value.is_boolean() || !value.is_number_integer())
        return false;
    if (value.is_number_unsigned()) {
        *result = quint64(value.get<StrictJson::number_unsigned_t>());
        return true;
    }
    *result = qint64(value.get<StrictJson::number_integer_t>());
    return true;
}

bool integerLess(const IntegerValue &left, const IntegerValue &right)
{
    if (std::holds_alternative<qint64>(left) && std::get<qint64>(left) < 0)
        return !(
            std::holds_alternative<qint64>(right) && std::get<qint64>(right) < 0
            && std::get<qint64>(right) <= std::get<qint64>(left));
    if (std::holds_alternative<qint64>(right) && std::get<qint64>(right) < 0)
        return false;
    const quint64 leftValue = std::holds_alternative<quint64>(left)
                                  ? std::get<quint64>(left)
                                  : quint64(std::get<qint64>(left));
    const quint64 rightValue = std::holds_alternative<quint64>(right)
                                   ? std::get<quint64>(right)
                                   : quint64(std::get<qint64>(right));
    return leftValue < rightValue;
}

bool integerEquals(const IntegerValue &left, const IntegerValue &right)
{
    return !integerLess(left, right) && !integerLess(right, left);
}

bool integerEquals(const IntegerValue &left, qint64 right)
{
    return integerEquals(left, IntegerValue(right));
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

bool validateStableId(const StrictJson &value, const QString &path, QString *error)
{
    static const QRegularExpression pattern(
        QString::fromLatin1("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    if (!value.is_string() || !pattern.match(jsonString(value)).hasMatch()) {
        *error = QString::fromLatin1("%1 is not a valid stable identifier").arg(path);
        return false;
    }
    return true;
}

bool validateSimpleId(const StrictJson &value, const QString &path, QString *error)
{
    static const QRegularExpression pattern(QString::fromLatin1("^[a-z][a-z0-9_]{0,63}$"));
    if (!value.is_string() || !pattern.match(jsonString(value)).hasMatch()) {
        *error = QString::fromLatin1("%1 is not a valid simple identifier").arg(path);
        return false;
    }
    return true;
}

bool readSha256(const StrictJson &value, const QString &path, QByteArray *result, QString *error)
{
    static const QRegularExpression pattern(QString::fromLatin1("^[0-9a-f]{64}$"));
    if (!value.is_string() || !pattern.match(jsonString(value)).hasMatch()) {
        *error = QString::fromLatin1("%1 is not a lowercase SHA-256 digest").arg(path);
        return false;
    }
    const std::string &hex = value.get_ref<const std::string &>();
    *result = QByteArray::fromHex(QByteArray(hex.data(), qsizetype(hex.size())));
    return result->size() == digestBytes;
}

bool validatePrimitive(const StrictJson &value, const QString &path, QString *error)
{
    if (!value.is_string()) {
        *error = QString::fromLatin1("%1 must be a primitive name").arg(path);
        return false;
    }
    static const std::set<std::string_view> primitives{
        "bool",
        "u8",
        "s8",
        "u16",
        "s16",
        "u32",
        "s32",
        "u64",
        "s64",
    };
    if (!primitives.contains(value.get_ref<const std::string &>())) {
        *error = QString::fromLatin1("%1 contains an unsupported primitive").arg(path);
        return false;
    }
    return true;
}

QString primitiveName(EcfgResourcePrimitive primitive)
{
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return QString::fromLatin1("bool");
    case EcfgResourcePrimitive::U8:
        return QString::fromLatin1("u8");
    case EcfgResourcePrimitive::S8:
        return QString::fromLatin1("s8");
    case EcfgResourcePrimitive::U16:
        return QString::fromLatin1("u16");
    case EcfgResourcePrimitive::S16:
        return QString::fromLatin1("s16");
    case EcfgResourcePrimitive::U32:
        return QString::fromLatin1("u32");
    case EcfgResourcePrimitive::S32:
        return QString::fromLatin1("s32");
    case EcfgResourcePrimitive::U64:
        return QString::fromLatin1("u64");
    case EcfgResourcePrimitive::S64:
        return QString::fromLatin1("s64");
    case EcfgResourcePrimitive::Q32_32:
        return QString::fromLatin1("q32_32");
    case EcfgResourcePrimitive::RawBits:
        return QString::fromLatin1("raw_bits");
    }
    return {};
}

bool validateSortedIdArray(
    const StrictJson &value,
    const QString &path,
    qsizetype minimum,
    qsizetype maximum,
    bool simple,
    QStringList *result,
    QString *error)
{
    if (!value.is_array() || value.size() < std::size_t(minimum)
        || value.size() > std::size_t(maximum)) {
        *error = QString::fromLatin1("%1 has an invalid item count").arg(path);
        return false;
    }

    QString previous;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const QString itemPath = path + QString::fromLatin1("[%1]").arg(index);
        if ((simple && !validateSimpleId(value[index], itemPath, error))
            || (!simple && !validateStableId(value[index], itemPath, error))) {
            return false;
        }
        const QString current = jsonString(value[index]);
        if (index && !(previous < current)) {
            *error = QString::fromLatin1("%1 must be strictly sorted and unique").arg(path);
            return false;
        }
        result->append(current);
        previous = current;
    }
    return true;
}

bool validateValueSource(
    const StrictJson &value, const QString &path, const QSet<QString> &parameterIds, QString *error)
{
    if (!value.is_object() || !containsField(value, "kind") || !value.at("kind").is_string()) {
        *error = QString::fromLatin1("%1 must be a value source").arg(path);
        return false;
    }
    const std::string &kind = value.at("kind").get_ref<const std::string &>();
    if (kind == "constant") {
        IntegerValue ignored;
        if (!hasExactFields(value, path, {"kind", "value"}, error)
            || !readInteger(value.at("value"), &ignored)) {
            if (error->isEmpty())
                *error = QString::fromLatin1("%1.value must be an integer").arg(path);
            return false;
        }
        return true;
    }
    if (kind == "parameter") {
        if (!hasExactFields(value, path, {"kind", "parameter_id"}, error)
            || !validateSimpleId(value.at("parameter_id"), path + ".parameter_id", error)) {
            return false;
        }
        if (!parameterIds.contains(jsonString(value.at("parameter_id")))) {
            *error = QString::fromLatin1("%1 references an unknown parameter").arg(path);
            return false;
        }
        return true;
    }
    *error = QString::fromLatin1("%1.kind is unsupported").arg(path);
    return false;
}

bool validateDefinition(
    const StrictJson &record, const QString &path, QByteArray *definitionDigest, QString *error)
{
    if (!hasExactFields(
            record,
            path,
            {"action_definition_id",
             "definition_format_version",
             "canonicalization",
             "action_definition_sha256",
             "definition"},
            error)
        || !validateStableId(record.at("action_definition_id"), path + ".action_definition_id", error)
        || !validateUnsigned(
            record.at("definition_format_version"),
            actionDefinitionsFormatVersion,
            actionDefinitionsFormatVersion,
            path + ".definition_format_version",
            error)
        || !validateConstantString(
            record.at("canonicalization"),
            actionDefinitionsCanonicalization,
            path + ".canonicalization",
            error)
        || !readSha256(
            record.at("action_definition_sha256"),
            path + ".action_definition_sha256",
            definitionDigest,
            error)) {
        return false;
    }

    const StrictJson &definition = record.at("definition");
    const QString definitionPath = path + ".definition";
    if (!hasExactFields(
            definition,
            definitionPath,
            {"definition_id",
             "enabled",
             "qualification",
             "disabled_reason",
             "pdo_profiles",
             "dc_required",
             "required_signal_definition_ids",
             "optional_signal_definition_ids",
             "parameters",
             "steps"},
            error)
        || !validateStableId(definition.at("definition_id"), definitionPath + ".definition_id", error)
        || jsonString(definition.at("definition_id"))
               != jsonString(record.at("action_definition_id"))
        || !definition.at("enabled").is_boolean() || !definition.at("qualification").is_string()
        || !definition.at("dc_required").is_boolean()) {
        if (error->isEmpty())
            *error = QString::fromLatin1("%1 has an invalid identity or state").arg(definitionPath);
        return false;
    }

    const bool enabled = definition.at("enabled").get<bool>();
    const std::string &qualification = definition.at("qualification").get_ref<const std::string &>();
    const StrictJson &reason = definition.at("disabled_reason");
    if ((enabled && (qualification != "qualified" || !reason.is_null()))
        || (!enabled
            && (qualification != "unqualified" || !reason.is_string()
                || jsonString(reason).isEmpty() || jsonString(reason).size() > 256))) {
        *error = QString::fromLatin1("%1 has an inconsistent qualification").arg(definitionPath);
        return false;
    }

    QStringList pdoProfiles;
    QStringList required;
    QStringList optional;
    if (!validateSortedIdArray(
            definition.at("pdo_profiles"),
            definitionPath + ".pdo_profiles",
            1,
            256,
            true,
            &pdoProfiles,
            error)
        || !validateSortedIdArray(
            definition.at("required_signal_definition_ids"),
            definitionPath + ".required_signal_definition_ids",
            1,
            256,
            false,
            &required,
            error)
        || !validateSortedIdArray(
            definition.at("optional_signal_definition_ids"),
            definitionPath + ".optional_signal_definition_ids",
            0,
            256,
            false,
            &optional,
            error)) {
        return false;
    }
    QSet<QString> requiredSet(required.cbegin(), required.cend());
    const QSet<QString> optionalSet(optional.cbegin(), optional.cend());
    QSet<QString> overlap = requiredSet;
    overlap.intersect(optionalSet);
    if (!overlap.isEmpty()) {
        *error = QString::fromLatin1("%1 required and optional signals overlap").arg(definitionPath);
        return false;
    }

    const StrictJson &parameters = definition.at("parameters");
    if (!parameters.is_array() || parameters.size() > 64) {
        *error = QString::fromLatin1("%1.parameters has an invalid item count").arg(definitionPath);
        return false;
    }
    QSet<QString> parameterIds;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const StrictJson &parameter = parameters[index];
        const QString parameterPath = definitionPath
                                      + QString::fromLatin1(".parameters[%1]").arg(index);
        if (!hasExactFields(
                parameter,
                parameterPath,
                {"parameter_id", "primitive", "unit", "minimum", "maximum"},
                error)
            || !validateSimpleId(parameter.at("parameter_id"), parameterPath + ".parameter_id", error)
            || !validatePrimitive(parameter.at("primitive"), parameterPath + ".primitive", error)) {
            return false;
        }
        const StrictJson &unit = parameter.at("unit");
        if (!unit.is_null() && (!unit.is_string() || jsonString(unit).size() > 64)) {
            *error = QString::fromLatin1("%1.unit is invalid").arg(parameterPath);
            return false;
        }
        IntegerValue minimum;
        IntegerValue maximum;
        if (!readInteger(parameter.at("minimum"), &minimum)
            || !readInteger(parameter.at("maximum"), &maximum) || integerLess(maximum, minimum)) {
            *error = QString::fromLatin1("%1 has an invalid integer range").arg(parameterPath);
            return false;
        }
        const QString parameterId = jsonString(parameter.at("parameter_id"));
        if (parameterIds.contains(parameterId)) {
            *error = QString::fromLatin1("%1 duplicates a parameter ID").arg(parameterPath);
            return false;
        }
        parameterIds.insert(parameterId);
    }

    const StrictJson &steps = definition.at("steps");
    if (!steps.is_array() || steps.empty() || steps.size() > 64) {
        *error = QString::fromLatin1("%1.steps has an invalid item count").arg(definitionPath);
        return false;
    }
    QSet<QString> referencedSignals;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const StrictJson &step = steps[index];
        const QString stepPath = definitionPath + QString::fromLatin1(".steps[%1]").arg(index);
        if (!step.is_object() || !containsField(step, "kind") || !step.at("kind").is_string()) {
            *error = QString::fromLatin1("%1 must be an action step").arg(stepPath);
            return false;
        }
        const std::string &kind = step.at("kind").get_ref<const std::string &>();
        if (kind == "write_group") {
            if (!hasExactFields(step, stepPath, {"kind", "consistency_group", "assignments"}, error)
                || !validateSimpleId(
                    step.at("consistency_group"), stepPath + ".consistency_group", error)) {
                return false;
            }
            const StrictJson &assignments = step.at("assignments");
            if (!assignments.is_array() || assignments.empty() || assignments.size() > 64) {
                *error
                    = QString::fromLatin1("%1.assignments has an invalid item count").arg(stepPath);
                return false;
            }
            QString previous;
            for (std::size_t assignmentIndex = 0; assignmentIndex < assignments.size();
                 ++assignmentIndex) {
                const StrictJson &assignment = assignments[assignmentIndex];
                const QString assignmentPath
                    = stepPath + QString::fromLatin1(".assignments[%1]").arg(assignmentIndex);
                if (!hasExactFields(
                        assignment,
                        assignmentPath,
                        {"semantic_signal_definition_id", "value_source"},
                        error)
                    || !validateStableId(
                        assignment.at("semantic_signal_definition_id"),
                        assignmentPath + ".semantic_signal_definition_id",
                        error)
                    || !validateValueSource(
                        assignment.at("value_source"),
                        assignmentPath + ".value_source",
                        parameterIds,
                        error)) {
                    return false;
                }
                const QString signalId = jsonString(assignment.at("semantic_signal_definition_id"));
                if (assignmentIndex && !(previous < signalId)) {
                    *error = QString::fromLatin1(
                                 "%1.assignments must be strictly sorted and unique")
                                 .arg(stepPath);
                    return false;
                }
                referencedSignals.insert(signalId);
                previous = signalId;
            }
        } else if (kind == "wait_masked") {
            if (!hasExactFields(
                    step,
                    stepPath,
                    {"kind", "semantic_signal_definition_id", "mask", "value", "timeout_cycles"},
                    error)
                || !validateStableId(
                    step.at("semantic_signal_definition_id"),
                    stepPath + ".semantic_signal_definition_id",
                    error)
                || !validateUnsigned(
                    step.at("mask"), 1, std::numeric_limits<quint64>::max(), stepPath + ".mask", error)
                || !validateUnsigned(
                    step.at("value"),
                    0,
                    std::numeric_limits<quint64>::max(),
                    stepPath + ".value",
                    error)
                || !validateUnsigned(
                    step.at("timeout_cycles"),
                    1,
                    std::numeric_limits<quint32>::max(),
                    stepPath + ".timeout_cycles",
                    error)) {
                return false;
            }
            quint64 mask = 0;
            quint64 value = 0;
            readUnsigned(step.at("mask"), std::numeric_limits<quint64>::max(), &mask);
            readUnsigned(step.at("value"), std::numeric_limits<quint64>::max(), &value);
            if (value & ~mask) {
                *error = QString::fromLatin1("%1.value contains bits outside mask").arg(stepPath);
                return false;
            }
            referencedSignals.insert(jsonString(step.at("semantic_signal_definition_id")));
        } else if (kind == "wait_absolute_limit") {
            if (!hasExactFields(
                    step,
                    stepPath,
                    {"kind", "semantic_signal_definition_id", "limit", "timeout_cycles"},
                    error)
                || !validateStableId(
                    step.at("semantic_signal_definition_id"),
                    stepPath + ".semantic_signal_definition_id",
                    error)
                || !validateUnsigned(
                    step.at("limit"),
                    0,
                    quint64(std::numeric_limits<qint64>::max()),
                    stepPath + ".limit",
                    error)
                || !validateUnsigned(
                    step.at("timeout_cycles"),
                    1,
                    std::numeric_limits<quint32>::max(),
                    stepPath + ".timeout_cycles",
                    error)) {
                return false;
            }
            referencedSignals.insert(jsonString(step.at("semantic_signal_definition_id")));
        } else {
            *error = QString::fromLatin1("%1.kind is unsupported").arg(stepPath);
            return false;
        }
    }

    if (referencedSignals != requiredSet) {
        *error = QString::fromLatin1(
                     "%1 required signals do not equal the complete step reference set")
                     .arg(definitionPath);
        return false;
    }

    const Utils::Result<QByteArray> canonicalDefinition
        = serializeCanonicalJson(definition, defaultMaximumSemanticActionDefinitionsBytes);
    if (!canonicalDefinition) {
        *error = canonicalDefinition.error();
        return false;
    }
    const QByteArray actualDigest
        = QCryptographicHash::hash(*canonicalDefinition, QCryptographicHash::Sha256);
    if (actualDigest != *definitionDigest) {
        *error = QString::fromLatin1("%1 action definition digest is stale").arg(path);
        return false;
    }
    return true;
}

bool compareOptionalString(const StrictJson &value, const std::optional<QString> &expected)
{
    return expected ? value.is_string() && jsonString(value) == *expected : value.is_null();
}

bool compareParameter(const StrictJson &definition, const VerifiedSemanticActionParameter &action)
{
    IntegerValue minimum;
    IntegerValue maximum;
    return jsonString(definition.at("parameter_id")) == action.parameterId
           && jsonString(definition.at("primitive")) == primitiveName(action.primitive)
           && compareOptionalString(definition.at("unit"), action.unit)
           && readInteger(definition.at("minimum"), &minimum)
           && readInteger(definition.at("maximum"), &maximum)
           && integerEquals(minimum, action.minimum) && integerEquals(maximum, action.maximum);
}

bool compareValueSource(const StrictJson &source, const VerifiedSemanticActionAssignment &assignment)
{
    const std::string &kind = source.at("kind").get_ref<const std::string &>();
    if (kind == "constant") {
        IntegerValue value;
        return assignment.constantValue && !assignment.parameterId
               && readInteger(source.at("value"), &value)
               && integerEquals(value, *assignment.constantValue);
    }
    return kind == "parameter" && !assignment.constantValue && assignment.parameterId
           && jsonString(source.at("parameter_id")) == *assignment.parameterId;
}

bool compareActionProjection(
    const VerifiedSemanticAction &action,
    const StrictJson &definition,
    const VerifiedSemanticBindingArtifact &artifact,
    QString *error)
{
    const QString path = QString::fromLatin1("$.actions[%1]").arg(action.actionBindingId);
    const QString qualification = action.qualification
                                          == VerifiedSemanticActionQualification::Qualified
                                      ? QString::fromLatin1("qualified")
                                      : QString::fromLatin1("unqualified");
    if (jsonString(definition.at("definition_id")) != action.actionDefinitionId
        || definition.at("enabled").get<bool>() != action.enabled
        || jsonString(definition.at("qualification")) != qualification
        || !compareOptionalString(definition.at("disabled_reason"), action.disabledReason)
        || definition.at("dc_required").get<bool>() != action.dcRequired) {
        *error = QString::fromLatin1("%1 state differs from its signed definition").arg(path);
        return false;
    }

    const VerifiedSemanticDevice *device = artifact.findDevice(action.projectDeviceId);
    const SemanticBindingTopologyInstance *topologyInstance = nullptr;
    if (device) {
        for (const SemanticBindingTopologyInstance &candidate : artifact.topologyInstances) {
            if (candidate.position != device->position
                || candidate.stationAddress != device->stationAddress) {
                continue;
            }
            if (topologyInstance) {
                *error = QString::fromLatin1("%1 has an ambiguous topology projection").arg(path);
                return false;
            }
            topologyInstance = &candidate;
        }
    }
    QStringList pdoProfiles;
    for (const StrictJson &value : definition.at("pdo_profiles"))
        pdoProfiles.append(jsonString(value));
    if (!device || !topologyInstance || !pdoProfiles.contains(topologyInstance->pdoProfile)) {
        *error = QString::fromLatin1("%1 PDO profile differs from its signed definition").arg(path);
        return false;
    }

    const StrictJson &parameters = definition.at("parameters");
    if (parameters.size() != std::size_t(action.parameters.size())) {
        *error = QString::fromLatin1("%1 parameters differ from its signed definition").arg(path);
        return false;
    }
    for (qsizetype index = 0; index < action.parameters.size(); ++index) {
        if (!compareParameter(parameters[std::size_t(index)], action.parameters[index])) {
            *error = QString::fromLatin1("%1 parameter %2 differs from its signed definition")
                         .arg(path)
                         .arg(index);
            return false;
        }
    }

    const auto bindingDefinitions =
        [](const QList<VerifiedSemanticActionBindingReference> &bindings) {
            QStringList result;
            result.reserve(bindings.size());
            for (const VerifiedSemanticActionBindingReference &binding : bindings)
                result.append(binding.semanticSignalDefinitionId);
            std::sort(result.begin(), result.end());
            return result;
        };
    QStringList required;
    QStringList optional;
    for (const StrictJson &value : definition.at("required_signal_definition_ids"))
        required.append(jsonString(value));
    for (const StrictJson &value : definition.at("optional_signal_definition_ids"))
        optional.append(jsonString(value));
    if (required != bindingDefinitions(action.requiredBindings)
        || optional != bindingDefinitions(action.optionalBindings)) {
        *error = QString::fromLatin1("%1 binding sets differ from its signed definition").arg(path);
        return false;
    }

    QHash<QString, QString> definitionByBinding;
    for (const auto *bindings : {&action.requiredBindings, &action.optionalBindings}) {
        for (const VerifiedSemanticActionBindingReference &binding : *bindings) {
            if (definitionByBinding.contains(binding.semanticBindingId)) {
                *error = QString::fromLatin1("%1 contains duplicate binding IDs").arg(path);
                return false;
            }
            definitionByBinding.insert(binding.semanticBindingId, binding.semanticSignalDefinitionId);
        }
    }

    const StrictJson &steps = definition.at("steps");
    if (steps.size() != std::size_t(action.steps.size())) {
        *error = QString::fromLatin1("%1 step count differs from its signed definition").arg(path);
        return false;
    }
    QHash<QString, quint32> numericGroupByLocalGroup;
    QHash<quint32, QString> localGroupByNumericGroup;
    for (qsizetype index = 0; index < action.steps.size(); ++index) {
        const VerifiedSemanticActionStep &actionStep = action.steps[index];
        const StrictJson &definitionStep = steps[std::size_t(index)];
        const std::string &kind = definitionStep.at("kind").get_ref<const std::string &>();
        if (actionStep.kind == VerifiedSemanticActionStepKind::WriteGroup) {
            if (kind != "write_group") {
                *error = QString::fromLatin1("%1 step %2 kind differs").arg(path).arg(index);
                return false;
            }
            const QString localGroup = jsonString(definitionStep.at("consistency_group"));
            const auto numericGroup = numericGroupByLocalGroup.constFind(localGroup);
            const auto mappedLocalGroup = localGroupByNumericGroup.constFind(
                actionStep.consistencyGroupId);
            if ((numericGroup != numericGroupByLocalGroup.cend()
                 && *numericGroup != actionStep.consistencyGroupId)
                || (mappedLocalGroup != localGroupByNumericGroup.cend()
                    && *mappedLocalGroup != localGroup)) {
                *error = QString::fromLatin1("%1 step %2 group projection differs")
                             .arg(path)
                             .arg(index);
                return false;
            }
            numericGroupByLocalGroup.insert(localGroup, actionStep.consistencyGroupId);
            localGroupByNumericGroup.insert(actionStep.consistencyGroupId, localGroup);
            QMap<QString, const VerifiedSemanticActionAssignment *> actionAssignments;
            for (const VerifiedSemanticActionAssignment &assignment : actionStep.assignments) {
                const auto definitionIterator = definitionByBinding.constFind(
                    assignment.semanticBindingId);
                if (definitionIterator == definitionByBinding.cend()
                    || actionAssignments.contains(*definitionIterator)) {
                    *error = QString::fromLatin1("%1 step %2 binding projection is invalid")
                                 .arg(path)
                                 .arg(index);
                    return false;
                }
                actionAssignments.insert(*definitionIterator, &assignment);
            }
            const StrictJson &definitionAssignments = definitionStep.at("assignments");
            if (definitionAssignments.size() != std::size_t(actionAssignments.size())) {
                *error = QString::fromLatin1("%1 step %2 assignment count differs")
                             .arg(path)
                             .arg(index);
                return false;
            }
            qsizetype assignmentIndex = 0;
            for (auto iterator = actionAssignments.cbegin(); iterator != actionAssignments.cend();
                 ++iterator, ++assignmentIndex) {
                const StrictJson &expected = definitionAssignments[std::size_t(assignmentIndex)];
                if (jsonString(expected.at("semantic_signal_definition_id")) != iterator.key()
                    || !compareValueSource(expected.at("value_source"), *iterator.value())) {
                    *error
                        = QString::fromLatin1("%1 step %2 assignment differs").arg(path).arg(index);
                    return false;
                }
            }
            continue;
        }

        const std::string_view expectedKind = actionStep.kind
                                                      == VerifiedSemanticActionStepKind::WaitMasked
                                                  ? std::string_view("wait_masked")
                                                  : std::string_view("wait_absolute_limit");
        if (kind != expectedKind) {
            *error = QString::fromLatin1("%1 step %2 kind differs").arg(path).arg(index);
            return false;
        }
        const auto bindingIterator = definitionByBinding.constFind(actionStep.semanticBindingId);
        if (bindingIterator == definitionByBinding.cend()
            || jsonString(definitionStep.at("semantic_signal_definition_id")) != *bindingIterator) {
            *error = QString::fromLatin1("%1 step %2 signal differs").arg(path).arg(index);
            return false;
        }
        quint64 timeout = 0;
        readUnsigned(
            definitionStep.at("timeout_cycles"), std::numeric_limits<quint32>::max(), &timeout);
        if (timeout != actionStep.timeoutCycles) {
            *error = QString::fromLatin1("%1 step %2 timeout differs").arg(path).arg(index);
            return false;
        }
        if (actionStep.kind == VerifiedSemanticActionStepKind::WaitMasked) {
            quint64 mask = 0;
            quint64 value = 0;
            readUnsigned(definitionStep.at("mask"), std::numeric_limits<quint64>::max(), &mask);
            readUnsigned(definitionStep.at("value"), std::numeric_limits<quint64>::max(), &value);
            if (mask != actionStep.mask || value != actionStep.value) {
                *error = QString::fromLatin1("%1 step %2 masked wait differs").arg(path).arg(index);
                return false;
            }
        } else {
            quint64 limit = 0;
            readUnsigned(
                definitionStep.at("limit"), quint64(std::numeric_limits<qint64>::max()), &limit);
            if (limit != actionStep.absoluteLimit) {
                *error
                    = QString::fromLatin1("%1 step %2 absolute wait differs").arg(path).arg(index);
                return false;
            }
        }
    }
    return true;
}

bool validDigest(QByteArrayView digest)
{
    return digest.size() == digestBytes
           && std::any_of(digest.begin(), digest.end(), [](char byte) { return byte != 0; });
}

} // namespace

bool VerifiedSemanticActionDefinitions::isValid() const
{
    return !canonicalDefinitions.isEmpty() && validDigest(definitionsSha256)
           && validDigest(semanticBindingArtifactSha256) && definitionCount && actionCount
           && QCryptographicHash::hash(canonicalDefinitions, QCryptographicHash::Sha256)
                  == definitionsSha256;
}

Utils::Result<VerifiedSemanticActionDefinitions> verifySemanticActionDefinitions(
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const VerifiedSemanticBindingArtifact &artifact,
    qsizetype maximumDefinitionsBytes)
{
    if (maximumDefinitionsBytes <= 0
        || maximumDefinitionsBytes > defaultMaximumSemanticActionDefinitionsBytes) {
        return definitionsError(QString::fromLatin1("the payload size limit is invalid"));
    }
    if (manifest.formatVersion != 2 || manifest.trust != EcpkgTrustClass::Production
        || artifact.formatVersion != 2 || artifact.trust != EcpkgTrustClass::Production
        || !manifest.semanticBinding || !manifest.actionDefinitions
        || container.semanticActionDefinitionsJson.isEmpty()) {
        return definitionsError(
            QString::fromLatin1("the production ECPKG v2 evidence set is incomplete"));
    }
    if (container.semanticActionDefinitionsJson.size() > maximumDefinitionsBytes) {
        return definitionsError(
            QString::fromLatin1("the companion payload exceeds the configured size limit"));
    }

    const SignedEcpkgSemanticBindingSummary &summary = *manifest.semanticBinding;
    const QByteArray companionSha256 = QCryptographicHash::hash(
        container.semanticActionDefinitionsJson, QCryptographicHash::Sha256);
    if (summary.actionDefinitionsFormatVersion != actionDefinitionsFormatVersion
        || !summary.actionDefinitionCount || summary.actionDefinitionsSha256 != companionSha256
        || manifest.actionDefinitions->bytes
               != quint32(container.semanticActionDefinitionsJson.size())
        || manifest.actionDefinitions->sha256 != companionSha256) {
        return definitionsError(
            QString::fromLatin1("the signed companion identity is inconsistent"));
    }

    Utils::Result<StrictJson> companion
        = parseCanonicalJson(container.semanticActionDefinitionsJson, maximumDefinitionsBytes);
    if (!companion)
        return definitionsError(companion.error());

    QString validationError;
    if (!hasExactFields(
            *companion,
            QString::fromLatin1("$"),
            {"format",
             "format_version",
             "canonicalization",
             "semantic_binding_artifact_sha256",
             "action_count",
             "definition_count",
             "definitions"},
            &validationError)
        || !validateConstantString(
            companion->at("format"),
            actionDefinitionsFormat,
            QString::fromLatin1("$.format"),
            &validationError)
        || !validateUnsigned(
            companion->at("format_version"),
            actionDefinitionsFormatVersion,
            actionDefinitionsFormatVersion,
            QString::fromLatin1("$.format_version"),
            &validationError)
        || !validateConstantString(
            companion->at("canonicalization"),
            actionDefinitionsCanonicalization,
            QString::fromLatin1("$.canonicalization"),
            &validationError)) {
        return definitionsError(validationError);
    }

    QByteArray targetArtifactSha256;
    quint64 actionCount = 0;
    quint64 definitionCount = 0;
    if (!readSha256(
            companion->at("semantic_binding_artifact_sha256"),
            QString::fromLatin1("$.semantic_binding_artifact_sha256"),
            &targetArtifactSha256,
            &validationError)
        || !validateUnsigned(
            companion->at("action_count"),
            1,
            4096,
            QString::fromLatin1("$.action_count"),
            &validationError)
        || !validateUnsigned(
            companion->at("definition_count"),
            1,
            4096,
            QString::fromLatin1("$.definition_count"),
            &validationError)) {
        return definitionsError(validationError);
    }
    readUnsigned(companion->at("action_count"), 4096, &actionCount);
    readUnsigned(companion->at("definition_count"), 4096, &definitionCount);
    if (targetArtifactSha256 != artifact.artifactSha256
        || actionCount != quint64(artifact.actions.size())
        || definitionCount != summary.actionDefinitionCount) {
        return definitionsError(
            QString::fromLatin1("the companion targets a different action artifact"));
    }

    const StrictJson &definitions = companion->at("definitions");
    if (!definitions.is_array() || definitions.empty() || definitions.size() > 4096
        || definitions.size() != definitionCount) {
        return definitionsError(QString::fromLatin1("$.definitions has an invalid item count"));
    }

    QMap<QString, QByteArray> definitionDigests;
    QMap<QString, const StrictJson *> definitionRecords;
    QString previousDefinitionId;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const StrictJson &record = definitions[index];
        const QString path = QString::fromLatin1("$.definitions[%1]").arg(index);
        QByteArray digest;
        if (!validateDefinition(record, path, &digest, &validationError))
            return definitionsError(validationError);
        const QString definitionId = jsonString(record.at("action_definition_id"));
        if (index && !(previousDefinitionId < definitionId)) {
            return definitionsError(
                QString::fromLatin1("$.definitions must be strictly sorted and unique"));
        }
        definitionDigests.insert(definitionId, digest);
        definitionRecords.insert(definitionId, &record);
        previousDefinitionId = definitionId;
    }

    QMap<QString, QByteArray> actionDigests;
    for (const VerifiedSemanticAction &action : artifact.actions) {
        if (action.actionDefinitionId.isEmpty() || !validDigest(action.actionDefinitionSha256)) {
            return definitionsError(
                QString::fromLatin1("the signed action set contains an incomplete definition"));
        }
        const auto existing = actionDigests.constFind(action.actionDefinitionId);
        if (existing != actionDigests.cend() && *existing != action.actionDefinitionSha256) {
            return definitionsError(
                QString::fromLatin1("one action definition ID has multiple signed digests"));
        }
        actionDigests.insert(action.actionDefinitionId, action.actionDefinitionSha256);
    }
    if (actionDigests != definitionDigests) {
        return definitionsError(
            QString::fromLatin1("the artifact actions and companion definitions are not closed"));
    }
    for (const VerifiedSemanticAction &action : artifact.actions) {
        if (!compareActionProjection(
                action,
                definitionRecords.value(action.actionDefinitionId)->at("definition"),
                artifact,
                &validationError)) {
            return definitionsError(validationError);
        }
    }

    Utils::Result<StrictJson> compileReport
        = parseStrictJson(container.compileReportJson, defaultMaximumSemanticCompileReportBytes);
    if (!compileReport)
        return definitionsError(compileReport.error());
    QByteArray reportDigest;
    if (!compileReport->is_object()
        || !containsField(*compileReport, "semantic_action_definitions_manifest")
        || !containsField(*compileReport, "semantic_action_definitions_sha256")
        || compileReport->at("semantic_action_definitions_manifest") != *companion
        || !readSha256(
            compileReport->at("semantic_action_definitions_sha256"),
            QString::fromLatin1("$.compile_report.semantic_action_definitions_sha256"),
            &reportDigest,
            &validationError)
        || reportDigest != companionSha256) {
        if (validationError.isEmpty()) {
            validationError = QString::fromLatin1(
                "the compile report companion declaration differs from the signed payload");
        }
        return definitionsError(validationError);
    }

    VerifiedSemanticActionDefinitions result;
    result.canonicalDefinitions = container.semanticActionDefinitionsJson;
    result.definitionsSha256 = companionSha256;
    result.semanticBindingArtifactSha256 = targetArtifactSha256;
    result.definitionCount = quint32(definitionCount);
    result.actionCount = quint32(actionCount);
    if (!result.isValid())
        return definitionsError(QString::fromLatin1("the verified companion result is invalid"));
    return result;
}

} // namespace EtherCAT::SemanticRuntime::Internal
