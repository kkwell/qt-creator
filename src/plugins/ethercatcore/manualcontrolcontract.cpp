// Copyright (C) 2026 Kvell

#include "manualcontrolcontract.h"

#include <QChar>
#include <QSet>

#include <algorithm>
#include <limits>

namespace EtherCAT::Core {
namespace {

using Data::EngineeringValue;
using Data::EngineeringValueKind;
using Data::ExactRational;

EngineeringContractValidation engineeringRejection(
    EngineeringContractError error, const QString &detail)
{
    return {error, detail};
}

ManualControlContractValidation manualRejection(
    ManualControlContractError error, const QString &detail)
{
    return {error, detail};
}

quint64 magnitude(qint64 value)
{
    if (value >= 0)
        return quint64(value);
    return quint64(-(value + 1)) + 1;
}

quint64 greatestCommonDivisor(quint64 left, quint64 right)
{
    while (right != 0) {
        const quint64 remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

bool checkedAdd(qint64 left, qint64 right, qint64 *result)
{
    if ((right > 0 && left > std::numeric_limits<qint64>::max() - right)
        || (right < 0 && left < std::numeric_limits<qint64>::min() - right)) {
        return false;
    }
    *result = left + right;
    return true;
}

bool checkedMultiply(qint64 left, qint64 right, qint64 *result)
{
    if (left == 0 || right == 0) {
        *result = 0;
        return true;
    }
    if ((left == -1 && right == std::numeric_limits<qint64>::min())
        || (right == -1 && left == std::numeric_limits<qint64>::min())) {
        return false;
    }
    if (left > 0) {
        if ((right > 0 && left > std::numeric_limits<qint64>::max() / right)
            || (right < 0 && right < std::numeric_limits<qint64>::min() / left)) {
            return false;
        }
    } else if (
        (right > 0 && left < std::numeric_limits<qint64>::min() / right)
        || (right < 0 && left < std::numeric_limits<qint64>::max() / right)) {
        return false;
    }
    *result = left * right;
    return true;
}

ExactRationalResult rationalResult(
    qint64 numerator, qint64 denominator, EngineeringContractError overflowError)
{
    if (denominator <= 0) {
        return {
            engineeringRejection(
                overflowError, QStringLiteral("Exact rational denominator must be positive.")),
            std::nullopt,
        };
    }
    if (numerator == 0)
        return {{}, ExactRational{0, 1}};

    const quint64 divisor = greatestCommonDivisor(magnitude(numerator), quint64(denominator));
    return {{}, ExactRational{numerator / qint64(divisor), denominator / qint64(divisor)}};
}

ExactRationalResult addRationals(const ExactRational &left, const ExactRational &right)
{
    const qint64 divisor = qint64(
        greatestCommonDivisor(quint64(left.denominator), quint64(right.denominator)));
    const qint64 leftMultiplier = right.denominator / divisor;
    const qint64 rightMultiplier = left.denominator / divisor;

    qint64 leftNumerator = 0;
    qint64 rightNumerator = 0;
    qint64 numerator = 0;
    qint64 denominator = 0;
    if (!checkedMultiply(left.numerator, leftMultiplier, &leftNumerator)
        || !checkedMultiply(right.numerator, rightMultiplier, &rightNumerator)
        || !checkedAdd(leftNumerator, rightNumerator, &numerator)
        || !checkedMultiply(left.denominator, leftMultiplier, &denominator)) {
        return {
            engineeringRejection(
                EngineeringContractError::ArithmeticOverflow,
                QStringLiteral("Exact rational addition overflowed.")),
            std::nullopt,
        };
    }
    return rationalResult(numerator, denominator, EngineeringContractError::ArithmeticOverflow);
}

ExactRationalResult negateRational(const ExactRational &value)
{
    if (value.numerator == std::numeric_limits<qint64>::min()) {
        return {
            engineeringRejection(
                EngineeringContractError::ArithmeticOverflow,
                QStringLiteral("Exact rational negation overflowed.")),
            std::nullopt,
        };
    }
    return {{}, ExactRational{-value.numerator, value.denominator}};
}

ExactRationalResult subtractRationals(const ExactRational &left, const ExactRational &right)
{
    const ExactRationalResult negated = negateRational(right);
    if (!negated.validation.accepted())
        return negated;
    return addRationals(left, *negated.value);
}

ExactRationalResult multiplyRationals(const ExactRational &left, const ExactRational &right)
{
    const quint64 leftDivisor
        = greatestCommonDivisor(magnitude(left.numerator), quint64(right.denominator));
    const quint64 rightDivisor
        = greatestCommonDivisor(magnitude(right.numerator), quint64(left.denominator));

    const qint64 leftNumerator = left.numerator / qint64(leftDivisor);
    const qint64 rightNumerator = right.numerator / qint64(rightDivisor);
    const qint64 leftDenominator = left.denominator / qint64(rightDivisor);
    const qint64 rightDenominator = right.denominator / qint64(leftDivisor);

    qint64 numerator = 0;
    qint64 denominator = 0;
    if (!checkedMultiply(leftNumerator, rightNumerator, &numerator)
        || !checkedMultiply(leftDenominator, rightDenominator, &denominator)) {
        return {
            engineeringRejection(
                EngineeringContractError::ArithmeticOverflow,
                QStringLiteral("Exact rational multiplication overflowed.")),
            std::nullopt,
        };
    }
    return rationalResult(numerator, denominator, EngineeringContractError::ArithmeticOverflow);
}

ExactRationalResult divideRationals(const ExactRational &left, const ExactRational &right)
{
    if (right.numerator == 0) {
        return {
            engineeringRejection(
                EngineeringContractError::ScaleIsZero,
                QStringLiteral("Exact rational division by zero is not allowed.")),
            std::nullopt,
        };
    }
    if (right.numerator == std::numeric_limits<qint64>::min()) {
        return {
            engineeringRejection(
                EngineeringContractError::ArithmeticOverflow,
                QStringLiteral("Exact rational reciprocal overflowed.")),
            std::nullopt,
        };
    }

    ExactRational reciprocal;
    reciprocal.numerator = right.numerator < 0 ? -right.denominator : right.denominator;
    reciprocal.denominator = qint64(magnitude(right.numerator));
    return multiplyRationals(left, reciprocal);
}

ExactRationalResult valueAsRational(
    const EngineeringValue &value, const Data::EngineeringConstraint *constraint = nullptr)
{
    switch (value.kind) {
    case EngineeringValueKind::Boolean:
        return {{}, ExactRational{value.boolean ? 1 : 0, 1}};
    case EngineeringValueKind::SignedInteger:
        return {{}, ExactRational{value.signedInteger, 1}};
    case EngineeringValueKind::UnsignedInteger:
        if (value.unsignedInteger > quint64(std::numeric_limits<qint64>::max())) {
            return {
                engineeringRejection(
                    EngineeringContractError::ArithmeticOverflow,
                    QStringLiteral("Unsigned value exceeds the exact rational domain.")),
                std::nullopt,
            };
        }
        return {{}, ExactRational{qint64(value.unsignedInteger), 1}};
    case EngineeringValueKind::ExactRational:
        return {validateExactRational(value.rational), value.rational};
    case EngineeringValueKind::Enumeration:
        if (!constraint) {
            return {
                engineeringRejection(
                    EngineeringContractError::InvalidValue,
                    QStringLiteral("Enumeration value requires an engineering constraint.")),
                std::nullopt,
            };
        }
        for (const Data::EngineeringEnumerationValue &entry : constraint->enumeration) {
            if (entry.id == value.enumerationName)
                return {{}, entry.value};
        }
        return {
            engineeringRejection(
                EngineeringContractError::ConstraintEnumerationViolation,
                QStringLiteral("Enumeration value is not declared by the constraint.")),
            std::nullopt,
        };
    case EngineeringValueKind::Invalid:
        break;
    }
    return {
        engineeringRejection(
            EngineeringContractError::InvalidValue,
            QStringLiteral("Engineering value kind is invalid.")),
        std::nullopt,
    };
}

EngineeringContractValidation compareRationals(
    const ExactRational &left, const ExactRational &right, int *comparison)
{
    qint64 leftProduct = 0;
    qint64 rightProduct = 0;
    if (!checkedMultiply(left.numerator, right.denominator, &leftProduct)
        || !checkedMultiply(right.numerator, left.denominator, &rightProduct)) {
        return engineeringRejection(
            EngineeringContractError::ArithmeticOverflow,
            QStringLiteral("Exact rational comparison overflowed."));
    }
    *comparison = leftProduct < rightProduct ? -1 : (leftProduct > rightProduct ? 1 : 0);
    return {};
}

EngineeringContractValidation validateRawValue(const EngineeringValue &raw, quint32 bitWidth)
{
    if (bitWidth == 0 || bitWidth > 64) {
        return engineeringRejection(
            EngineeringContractError::InvalidRawFormat,
            QStringLiteral("Raw integer bit width must be in the range 1 to 64."));
    }

    if (raw.kind == EngineeringValueKind::Boolean) {
        if (bitWidth != 1) {
            return engineeringRejection(
                EngineeringContractError::InvalidRawFormat,
                QStringLiteral("Raw Boolean values require bit width 1."));
        }
        return {};
    }
    if (raw.kind == EngineeringValueKind::SignedInteger) {
        if (bitWidth == 64)
            return {};
        const qint64 magnitudeLimit = qint64(1) << (bitWidth - 1);
        if (raw.signedInteger < -magnitudeLimit || raw.signedInteger >= magnitudeLimit) {
            return engineeringRejection(
                EngineeringContractError::RawValueOutOfRange,
                QStringLiteral("Signed raw value does not fit the declared bit width."));
        }
        return {};
    }
    if (raw.kind == EngineeringValueKind::UnsignedInteger) {
        if (bitWidth == 64)
            return {};
        const quint64 maximum = (quint64(1) << bitWidth) - 1;
        if (raw.unsignedInteger > maximum) {
            return engineeringRejection(
                EngineeringContractError::RawValueOutOfRange,
                QStringLiteral("Unsigned raw value does not fit the declared bit width."));
        }
        return {};
    }
    return engineeringRejection(
        EngineeringContractError::InvalidRawFormat,
        QStringLiteral("Raw value must be Boolean, signed integer, or unsigned integer."));
}

EngineeringContractValidation roundedInteger(
    const ExactRational &value, Data::EngineeringRounding rounding, qint64 *result)
{
    qint64 quotient = value.numerator / value.denominator;
    const qint64 remainder = value.numerator % value.denominator;
    if (remainder == 0) {
        *result = quotient;
        return {};
    }

    switch (rounding) {
    case Data::EngineeringRounding::RejectInexact:
        return engineeringRejection(
            EngineeringContractError::InexactConversion,
            QStringLiteral("Engineering value is not exactly representable as a raw integer."));
    case Data::EngineeringRounding::TowardZero:
        break;
    case Data::EngineeringRounding::TowardNegativeInfinity:
        if (value.numerator < 0 && !checkedAdd(quotient, -1, &quotient)) {
            return engineeringRejection(
                EngineeringContractError::ArithmeticOverflow,
                QStringLiteral("Rounded raw value overflowed."));
        }
        break;
    case Data::EngineeringRounding::TowardPositiveInfinity:
        if (value.numerator > 0 && !checkedAdd(quotient, 1, &quotient)) {
            return engineeringRejection(
                EngineeringContractError::ArithmeticOverflow,
                QStringLiteral("Rounded raw value overflowed."));
        }
        break;
    case Data::EngineeringRounding::NearestTiesToEven: {
        const quint64 remainderMagnitude = magnitude(remainder);
        const quint64 denominator = quint64(value.denominator);
        const bool greaterThanHalf = remainderMagnitude > denominator - remainderMagnitude;
        const bool exactlyHalf = remainderMagnitude == denominator - remainderMagnitude;
        const bool odd = (magnitude(quotient) & 1U) != 0;
        if (greaterThanHalf || (exactlyHalf && odd)) {
            const qint64 increment = value.numerator < 0 ? -1 : 1;
            if (!checkedAdd(quotient, increment, &quotient)) {
                return engineeringRejection(
                    EngineeringContractError::ArithmeticOverflow,
                    QStringLiteral("Rounded raw value overflowed."));
            }
        }
        break;
    }
    }

    *result = quotient;
    return {};
}

bool canonicalIdentifier(const QString &value)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > 256)
        return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

template<typename Item, typename Value>
const Item *findUnique(
    const QList<Item> &items, const Value &value, const auto &extractor, bool *ambiguous)
{
    const Item *match = nullptr;
    for (const Item &item : items) {
        if (extractor(item) != value)
            continue;
        if (match) {
            *ambiguous = true;
            return nullptr;
        }
        match = &item;
    }
    return match;
}

ManualControlContractValidation validateTiming(
    const Data::ManualCommandTiming &timing, bool holdToRun)
{
    if (timing.commandTtlMs == 0) {
        return manualRejection(
            ManualControlContractError::InvalidTiming,
            QStringLiteral("Enabled manual commands require a nonzero command TTL."));
    }
    if (holdToRun) {
        if (timing.refreshTimeoutMs == 0 || timing.maxContinuousHoldMs == 0
            || timing.refreshTimeoutMs > timing.maxContinuousHoldMs) {
            return manualRejection(
                ManualControlContractError::InvalidTiming,
                QStringLiteral("Held commands require nonzero refresh and continuous-hold limits."));
        }
    } else if (timing.refreshTimeoutMs != 0 || timing.maxContinuousHoldMs != 0) {
        return manualRejection(
            ManualControlContractError::InvalidTiming,
            QStringLiteral("Non-held commands cannot declare hold timing."));
    }
    return {};
}

bool sortedUniqueActionIds(const QList<Data::SemanticActionId> &ids)
{
    QString previous;
    for (const Data::SemanticActionId &id : ids) {
        if (!canonicalIdentifier(id.value) || (!previous.isEmpty() && id.value <= previous))
            return false;
        previous = id.value;
    }
    return true;
}

EngineeringContractValidation constraintIsSubset(
    const Data::EngineeringConstraint &candidate, const Data::EngineeringConstraint &allowed)
{
    const EngineeringContractValidation candidateValidation = validateEngineeringConstraint(
        candidate);
    if (!candidateValidation.accepted())
        return candidateValidation;
    const EngineeringContractValidation allowedValidation = validateEngineeringConstraint(allowed);
    if (!allowedValidation.accepted())
        return allowedValidation;

    if (allowed.minimum) {
        if (!candidate.minimum) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Project range omits the adapter minimum."));
        }
        int comparison = 0;
        const EngineeringContractValidation compared
            = compareRationals(*candidate.minimum, *allowed.minimum, &comparison);
        if (!compared.accepted() || comparison < 0)
            return compared.accepted()
                       ? engineeringRejection(
                             EngineeringContractError::InvalidConstraint,
                             QStringLiteral("Project minimum is outside the adapter range."))
                       : compared;
    }
    if (allowed.maximum) {
        if (!candidate.maximum) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Project range omits the adapter maximum."));
        }
        int comparison = 0;
        const EngineeringContractValidation compared
            = compareRationals(*candidate.maximum, *allowed.maximum, &comparison);
        if (!compared.accepted() || comparison > 0)
            return compared.accepted()
                       ? engineeringRejection(
                             EngineeringContractError::InvalidConstraint,
                             QStringLiteral("Project maximum is outside the adapter range."))
                       : compared;
    }
    if (allowed.step) {
        if (!candidate.step || !candidate.stepOrigin) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Project range omits the adapter step contract."));
        }
        const ExactRationalResult stepRatio = divideRationals(*candidate.step, *allowed.step);
        if (!stepRatio.validation.accepted() || stepRatio.value->denominator != 1
            || stepRatio.value->numerator <= 0) {
            return stepRatio.validation.accepted()
                       ? engineeringRejection(
                             EngineeringContractError::InvalidConstraint,
                             QStringLiteral("Project step is not an adapter-step multiple."))
                       : stepRatio.validation;
        }
        const ExactRationalResult originDelta
            = subtractRationals(*candidate.stepOrigin, *allowed.stepOrigin);
        if (!originDelta.validation.accepted())
            return originDelta.validation;
        const ExactRationalResult originSteps = divideRationals(*originDelta.value, *allowed.step);
        if (!originSteps.validation.accepted() || originSteps.value->denominator != 1) {
            return originSteps.validation.accepted()
                       ? engineeringRejection(
                             EngineeringContractError::InvalidConstraint,
                             QStringLiteral("Project step origin is not adapter-aligned."))
                       : originSteps.validation;
        }
    }
    if (!allowed.enumeration.isEmpty()) {
        if (candidate.enumeration.isEmpty()) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Project range omits the adapter enumeration."));
        }
        for (const Data::EngineeringEnumerationValue &entry : candidate.enumeration) {
            const auto allowedEntry = std::find_if(
                allowed.enumeration.cbegin(),
                allowed.enumeration.cend(),
                [&entry](const Data::EngineeringEnumerationValue &candidateEntry) {
                    return candidateEntry.id == entry.id && candidateEntry.value == entry.value;
                });
            if (allowedEntry == allowed.enumeration.cend()) {
                return engineeringRejection(
                    EngineeringContractError::InvalidConstraint,
                    QStringLiteral("Project enumeration is not an adapter subset."));
            }
        }
    } else if (!candidate.enumeration.isEmpty()) {
        return engineeringRejection(
            EngineeringContractError::InvalidConstraint,
            QStringLiteral("Project range cannot introduce adapter-unsigned enumeration names."));
    }
    return {};
}

bool actionHasExactValues(
    const Data::DeviceControlAction &action,
    const QList<Data::SemanticSignalDefinition> &signalDefinitions)
{
    for (const Data::DeviceControlStep &step : action.steps) {
        if (step.kind == Data::DeviceControlStepKind::Delay)
            continue;
        bool ambiguous = false;
        const Data::SemanticSignalDefinition *signal = findUnique(
            signalDefinitions,
            step.signalId,
            [](const Data::SemanticSignalDefinition &candidate) { return candidate.id; },
            &ambiguous);
        if (!signal || ambiguous || !signal->engineeringTransform)
            return false;

        const auto valueIsExact = [&action, signal](const Data::DeviceControlValue &value) {
            switch (value.source) {
            case Data::DeviceControlValueSource::Literal:
                return value.engineeringLiteralValue
                       && validateEngineeringValue(*value.engineeringLiteralValue).accepted();
            case Data::DeviceControlValueSource::Parameter:
                for (const Data::DeviceControlActionParameter &parameter : action.parameters) {
                    if (parameter.id != value.parameterId)
                        continue;
                    return parameter.engineeringConstraint
                           && constraintIsSubset(
                                  *parameter.engineeringConstraint,
                                  signal->engineeringTransform->constraint)
                                  .accepted();
                }
                return false;
            case Data::DeviceControlValueSource::Invalid:
                return false;
            }
            return false;
        };
        if (!valueIsExact(step.value))
            return false;
        if (step.value.source == Data::DeviceControlValueSource::Literal
            && !validateEngineeringValueAgainstConstraint(
                    *step.value.engineeringLiteralValue, signal->engineeringTransform->constraint)
                    .accepted()) {
            return false;
        }
        if (step.kind == Data::DeviceControlStepKind::WaitMaskedEquals && !valueIsExact(step.mask)) {
            return false;
        }
    }
    return true;
}

bool terminalFallbackAction(
    const Data::DeviceControlAction &action,
    const QList<Data::SemanticSignalDefinition> &signalDefinitions)
{
    if (!action.enabled || action.holdToRun || action.steps.isEmpty()
        || !std::any_of(
            action.steps.cbegin(),
            action.steps.cend(),
            [](const Data::DeviceControlStep &step) {
                return step.kind == Data::DeviceControlStepKind::WriteSignal;
            })
        || !action.allowedReleaseActionIds.isEmpty() || !action.allowedTimeoutActionIds.isEmpty()
        || !action.allowedFailureActionIds.isEmpty()) {
        return false;
    }
    return actionHasExactValues(action, signalDefinitions)
           && std::all_of(
               action.parameters.cbegin(),
               action.parameters.cend(),
               [](const Data::DeviceControlActionParameter &parameter) {
                   if (!parameter.required)
                       return true;
                   return parameter.engineeringConstraint && parameter.engineeringDefaultValue
                          && validateEngineeringValueAgainstConstraint(
                                 *parameter.engineeringDefaultValue,
                                 *parameter.engineeringConstraint)
                                 .accepted();
               });
}

} // namespace

ExactRationalResult normalizedExactRational(qint64 numerator, qint64 denominator)
{
    return rationalResult(numerator, denominator, EngineeringContractError::InvalidRational);
}

EngineeringContractValidation validateExactRational(const ExactRational &value)
{
    const ExactRationalResult normalized
        = normalizedExactRational(value.numerator, value.denominator);
    if (!normalized.validation.accepted())
        return normalized.validation;
    if (*normalized.value != value) {
        return engineeringRejection(
            EngineeringContractError::InvalidRational,
            QStringLiteral("Exact rational is not in canonical reduced form."));
    }
    return {};
}

EngineeringContractValidation validateEngineeringValue(const EngineeringValue &value)
{
    const bool neutralRational = value.rational == ExactRational{};
    switch (value.kind) {
    case EngineeringValueKind::Boolean:
        if (value.signedInteger == 0 && value.unsignedInteger == 0 && neutralRational
            && value.enumerationName.isEmpty()) {
            return {};
        }
        break;
    case EngineeringValueKind::SignedInteger:
        if (!value.boolean && value.unsignedInteger == 0 && neutralRational
            && value.enumerationName.isEmpty()) {
            return {};
        }
        break;
    case EngineeringValueKind::UnsignedInteger:
        if (!value.boolean && value.signedInteger == 0 && neutralRational
            && value.enumerationName.isEmpty()) {
            return {};
        }
        break;
    case EngineeringValueKind::ExactRational:
        if (!value.boolean && value.signedInteger == 0 && value.unsignedInteger == 0
            && value.enumerationName.isEmpty()) {
            return validateExactRational(value.rational);
        }
        break;
    case EngineeringValueKind::Enumeration:
        if (!value.boolean && value.signedInteger == 0 && value.unsignedInteger == 0
            && neutralRational && canonicalIdentifier(value.enumerationName)) {
            return {};
        }
        break;
    case EngineeringValueKind::Invalid:
        break;
    }
    return engineeringRejection(
        EngineeringContractError::InvalidValue,
        QStringLiteral("Engineering value is not canonical."));
}

EngineeringContractValidation validateEngineeringConstraint(
    const Data::EngineeringConstraint &constraint)
{
    const auto validateOptional = [](const std::optional<ExactRational> &value) {
        return value ? validateExactRational(*value) : EngineeringContractValidation{};
    };
    for (const std::optional<ExactRational> *value :
         {&constraint.minimum, &constraint.maximum, &constraint.step, &constraint.stepOrigin}) {
        const EngineeringContractValidation validation = validateOptional(*value);
        if (!validation.accepted())
            return validation;
    }

    if (constraint.minimum && constraint.maximum) {
        int comparison = 0;
        const EngineeringContractValidation compared
            = compareRationals(*constraint.minimum, *constraint.maximum, &comparison);
        if (!compared.accepted())
            return compared;
        if (comparison > 0) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Engineering minimum exceeds maximum."));
        }
    }
    if (constraint.step) {
        if (constraint.step->numerator <= 0 || !constraint.stepOrigin) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Engineering step requires a positive value and explicit origin."));
        }
    } else if (constraint.stepOrigin) {
        return engineeringRejection(
            EngineeringContractError::InvalidConstraint,
            QStringLiteral("Engineering step origin requires a declared step."));
    }

    QSet<QString> names;
    QList<ExactRational> values;
    QString previousName;
    for (const Data::EngineeringEnumerationValue &entry : constraint.enumeration) {
        if (!canonicalIdentifier(entry.id) || names.contains(entry.id)
            || (!previousName.isEmpty() && entry.id <= previousName)
            || !validateExactRational(entry.value).accepted()
            || std::find(values.cbegin(), values.cend(), entry.value) != values.cend()) {
            return engineeringRejection(
                EngineeringContractError::InvalidConstraint,
                QStringLiteral("Engineering enumeration entries must have unique exact values."));
        }
        names.insert(entry.id);
        previousName = entry.id;
        values.append(entry.value);
        const EngineeringContractValidation valueValidation
            = validateEngineeringValueAgainstConstraint(
                EngineeringValue::fromExactRational(entry.value),
                Data::EngineeringConstraint{
                    constraint.minimum,
                    constraint.maximum,
                    constraint.step,
                    constraint.stepOrigin,
                    {},
                });
        if (!valueValidation.accepted())
            return valueValidation;
    }
    return {};
}

EngineeringContractValidation validateEngineeringTransform(
    const Data::EngineeringTransform &transform)
{
    const EngineeringContractValidation scaleValidation = validateExactRational(transform.scale);
    if (!scaleValidation.accepted())
        return scaleValidation;
    if (transform.scale.numerator == 0) {
        return engineeringRejection(
            EngineeringContractError::ScaleIsZero,
            QStringLiteral("Engineering scale must be nonzero."));
    }
    const EngineeringContractValidation offsetValidation = validateExactRational(transform.offset);
    if (!offsetValidation.accepted())
        return offsetValidation;
    if (!transform.rounding) {
        return engineeringRejection(
            EngineeringContractError::InvalidTransform,
            QStringLiteral("Engineering-to-raw rounding must be explicitly declared."));
    }
    return validateEngineeringConstraint(transform.constraint);
}

EngineeringContractValidation validateEngineeringValueAgainstConstraint(
    const EngineeringValue &value, const Data::EngineeringConstraint &constraint)
{
    const EngineeringContractValidation constraintValidation = validateEngineeringConstraint(
        constraint);
    if (!constraintValidation.accepted())
        return constraintValidation;
    const EngineeringContractValidation valueValidation = validateEngineeringValue(value);
    if (!valueValidation.accepted())
        return valueValidation;

    const ExactRationalResult exact = valueAsRational(value, &constraint);
    if (!exact.validation.accepted())
        return exact.validation;

    if (constraint.minimum) {
        int comparison = 0;
        const EngineeringContractValidation compared
            = compareRationals(*exact.value, *constraint.minimum, &comparison);
        if (!compared.accepted())
            return compared;
        if (comparison < 0) {
            return engineeringRejection(
                EngineeringContractError::ConstraintRangeViolation,
                QStringLiteral("Engineering value is below the declared minimum."));
        }
    }
    if (constraint.maximum) {
        int comparison = 0;
        const EngineeringContractValidation compared
            = compareRationals(*exact.value, *constraint.maximum, &comparison);
        if (!compared.accepted())
            return compared;
        if (comparison > 0) {
            return engineeringRejection(
                EngineeringContractError::ConstraintRangeViolation,
                QStringLiteral("Engineering value exceeds the declared maximum."));
        }
    }
    if (constraint.step) {
        const ExactRationalResult delta = subtractRationals(*exact.value, *constraint.stepOrigin);
        if (!delta.validation.accepted())
            return delta.validation;
        const ExactRationalResult steps = divideRationals(*delta.value, *constraint.step);
        if (!steps.validation.accepted())
            return steps.validation;
        if (steps.value->denominator != 1) {
            return engineeringRejection(
                EngineeringContractError::ConstraintStepViolation,
                QStringLiteral("Engineering value is not aligned to the declared step origin."));
        }
    }
    if (!constraint.enumeration.isEmpty()) {
        const auto found = std::find_if(
            constraint.enumeration.cbegin(),
            constraint.enumeration.cend(),
            [&exact](const Data::EngineeringEnumerationValue &entry) {
                return entry.value == *exact.value;
            });
        if (found == constraint.enumeration.cend()) {
            return engineeringRejection(
                EngineeringContractError::ConstraintEnumerationViolation,
                QStringLiteral("Engineering value is not a declared enumeration member."));
        }
    }
    return {};
}

EngineeringConversionResult convertRawToEngineering(
    const EngineeringValue &raw, quint32 bitWidth, const Data::EngineeringTransform &transform)
{
    const EngineeringContractValidation transformValidation = validateEngineeringTransform(
        transform);
    if (!transformValidation.accepted())
        return {transformValidation, std::nullopt};
    const EngineeringContractValidation rawValidation = validateRawValue(raw, bitWidth);
    if (!rawValidation.accepted())
        return {rawValidation, std::nullopt};
    const EngineeringContractValidation valueValidation = validateEngineeringValue(raw);
    if (!valueValidation.accepted())
        return {valueValidation, std::nullopt};

    const ExactRationalResult rawExact = valueAsRational(raw);
    if (!rawExact.validation.accepted())
        return {rawExact.validation, std::nullopt};
    const ExactRationalResult scaled = multiplyRationals(*rawExact.value, transform.scale);
    if (!scaled.validation.accepted())
        return {scaled.validation, std::nullopt};
    const ExactRationalResult shifted = addRationals(*scaled.value, transform.offset);
    if (!shifted.validation.accepted())
        return {shifted.validation, std::nullopt};

    EngineeringValue result = EngineeringValue::fromExactRational(*shifted.value);
    if (raw.kind == EngineeringValueKind::Boolean && transform.scale == ExactRational{1, 1}
        && transform.offset == ExactRational{0, 1}) {
        result = EngineeringValue::fromBoolean(raw.boolean);
    } else if (!transform.constraint.enumeration.isEmpty()) {
        const auto member = std::find_if(
            transform.constraint.enumeration.cbegin(),
            transform.constraint.enumeration.cend(),
            [&shifted](const Data::EngineeringEnumerationValue &entry) {
                return entry.value == *shifted.value;
            });
        if (member != transform.constraint.enumeration.cend())
            result = EngineeringValue::fromEnumeration(member->id);
    }

    const EngineeringContractValidation constraintValidation
        = validateEngineeringValueAgainstConstraint(result, transform.constraint);
    if (!constraintValidation.accepted())
        return {constraintValidation, std::nullopt};
    return {{}, result};
}

EngineeringConversionResult convertEngineeringToRaw(
    const EngineeringValue &engineering,
    EngineeringValueKind rawKind,
    quint32 bitWidth,
    const Data::EngineeringTransform &transform)
{
    const EngineeringContractValidation transformValidation = validateEngineeringTransform(
        transform);
    if (!transformValidation.accepted())
        return {transformValidation, std::nullopt};
    const EngineeringContractValidation constraintValidation
        = validateEngineeringValueAgainstConstraint(engineering, transform.constraint);
    if (!constraintValidation.accepted())
        return {constraintValidation, std::nullopt};
    if (rawKind != EngineeringValueKind::Boolean && rawKind != EngineeringValueKind::SignedInteger
        && rawKind != EngineeringValueKind::UnsignedInteger) {
        return {
            engineeringRejection(
                EngineeringContractError::InvalidRawFormat,
                QStringLiteral("Requested raw value kind is not supported.")),
            std::nullopt,
        };
    }

    const ExactRationalResult exact = valueAsRational(engineering, &transform.constraint);
    if (!exact.validation.accepted())
        return {exact.validation, std::nullopt};
    const ExactRationalResult shifted = subtractRationals(*exact.value, transform.offset);
    if (!shifted.validation.accepted())
        return {shifted.validation, std::nullopt};
    const ExactRationalResult rawExact = divideRationals(*shifted.value, transform.scale);
    if (!rawExact.validation.accepted())
        return {rawExact.validation, std::nullopt};

    qint64 rounded = 0;
    const EngineeringContractValidation roundedValidation
        = roundedInteger(*rawExact.value, *transform.rounding, &rounded);
    if (!roundedValidation.accepted())
        return {roundedValidation, std::nullopt};

    EngineeringValue raw;
    if (rawKind == EngineeringValueKind::Boolean) {
        if (rounded != 0 && rounded != 1) {
            return {
                engineeringRejection(
                    EngineeringContractError::RawValueOutOfRange,
                    QStringLiteral("Raw Boolean value must be zero or one.")),
                std::nullopt,
            };
        }
        raw = EngineeringValue::fromBoolean(rounded != 0);
    } else if (rawKind == EngineeringValueKind::SignedInteger) {
        raw = EngineeringValue::fromSignedInteger(rounded);
    } else {
        if (rounded < 0) {
            return {
                engineeringRejection(
                    EngineeringContractError::RawValueOutOfRange,
                    QStringLiteral("Negative engineering result cannot become unsigned raw data.")),
                std::nullopt,
            };
        }
        raw = EngineeringValue::fromUnsignedInteger(quint64(rounded));
    }

    const EngineeringContractValidation rawValidation = validateRawValue(raw, bitWidth);
    if (!rawValidation.accepted())
        return {rawValidation, std::nullopt};
    return {{}, raw};
}

ManualControlContractValidation validateManualControlEnvelope(
    const Data::ManualControlEnvelope &envelope,
    const QList<Data::SemanticSignalDefinition> &signalDefinitions,
    const QList<Data::DeviceControlAction> &actionDefinitions,
    ManualControlFallbackContract fallbackContract)
{
    QSet<QString> signalIds;
    QSet<QString> actionIds;
    QString previousSignalId;
    for (const Data::ManualSignalEnvelope &signal : envelope.signalEnvelopes) {
        if (!canonicalIdentifier(signal.signalId.value)
            || signalIds.contains(signal.signalId.value)) {
            return manualRejection(
                ManualControlContractError::DuplicateIdentifier,
                QStringLiteral("Manual signal IDs must be canonical and unique."));
        }
        if (!previousSignalId.isEmpty() && signal.signalId.value <= previousSignalId) {
            return manualRejection(
                ManualControlContractError::NonCanonicalIdentifierOrder,
                QStringLiteral("Manual signal envelopes must be sorted by full semantic ID."));
        }
        signalIds.insert(signal.signalId.value);
        previousSignalId = signal.signalId.value;

        bool ambiguous = false;
        const Data::SemanticSignalDefinition *definition = findUnique(
            signalDefinitions,
            signal.signalId,
            [](const Data::SemanticSignalDefinition &candidate) { return candidate.id; },
            &ambiguous);
        if (!definition || ambiguous) {
            return manualRejection(
                ManualControlContractError::DefinitionNotFound,
                QStringLiteral("Manual signal does not resolve to one exact adapter definition."));
        }
        if (definition->exposure != Data::SemanticSignalExposure::Public) {
            return manualRejection(
                ManualControlContractError::SignalNotPublic,
                QStringLiteral("Only public semantic signals can be controlled directly."));
        }
        if ((definition->direction != Data::SemanticSignalDirection::Output
             && definition->direction != Data::SemanticSignalDirection::Bidirectional)
            || (definition->access != Data::SemanticSignalAccess::WriteOnly
                && definition->access != Data::SemanticSignalAccess::ReadWrite)) {
            return manualRejection(
                ManualControlContractError::SignalNotWritable,
                QStringLiteral("Manual signal definition is not writable."));
        }
        if (!definition->engineeringTransform
            || !validateEngineeringTransform(*definition->engineeringTransform).accepted()) {
            return manualRejection(
                ManualControlContractError::ExactTransformMissing,
                QStringLiteral("Manual signal requires a valid exact engineering transform."));
        }
        const EngineeringContractValidation allowedRange
            = constraintIsSubset(signal.allowedRange, definition->engineeringTransform->constraint);
        if (!allowedRange.accepted()) {
            return manualRejection(
                ManualControlContractError::ConstraintNotSubset, allowedRange.detail);
        }

        if (signal.enabled) {
            const ManualControlContractValidation timing
                = validateTiming(signal.timing, signal.holdToRun);
            if (!timing.accepted())
                return timing;
            if (!signal.safeValue) {
                return manualRejection(
                    ManualControlContractError::InvalidSafeValue,
                    QStringLiteral("Enabled manual signal requires an exact safe value."));
            }
        }
        if (signal.safeValue) {
            const EngineeringContractValidation safeValidation
                = validateEngineeringValueAgainstConstraint(*signal.safeValue, signal.allowedRange);
            if (!safeValidation.accepted()) {
                return manualRejection(
                    ManualControlContractError::InvalidSafeValue, safeValidation.detail);
            }
        }

        QSet<QString> groupIds;
        QString previousGroupId;
        for (const Data::ManualSignalSafeValue &groupValue : signal.consistencyGroupSafeValues) {
            if (!canonicalIdentifier(groupValue.signalId.value)
                || groupValue.signalId == signal.signalId
                || groupIds.contains(groupValue.signalId.value)
                || (!previousGroupId.isEmpty() && groupValue.signalId.value <= previousGroupId)) {
                return manualRejection(
                    ManualControlContractError::InvalidSafeValue,
                    QStringLiteral("Consistency-group safe-value IDs must be unique peers."));
            }
            groupIds.insert(groupValue.signalId.value);
            previousGroupId = groupValue.signalId.value;
            bool groupAmbiguous = false;
            const Data::SemanticSignalDefinition *groupDefinition = findUnique(
                signalDefinitions,
                groupValue.signalId,
                [](const Data::SemanticSignalDefinition &candidate) { return candidate.id; },
                &groupAmbiguous);
            if (!groupDefinition || groupAmbiguous || !groupDefinition->engineeringTransform
                || (groupDefinition->direction != Data::SemanticSignalDirection::Output
                    && groupDefinition->direction != Data::SemanticSignalDirection::Bidirectional)
                || (groupDefinition->access != Data::SemanticSignalAccess::WriteOnly
                    && groupDefinition->access != Data::SemanticSignalAccess::ReadWrite)) {
                return manualRejection(
                    ManualControlContractError::InvalidSafeValue,
                    QStringLiteral(
                        "Consistency-group safe value lacks an exact signal definition."));
            }
            const EngineeringContractValidation groupSafeValidation
                = validateEngineeringValueAgainstConstraint(
                    groupValue.value, groupDefinition->engineeringTransform->constraint);
            if (!groupSafeValidation.accepted()) {
                return manualRejection(
                    ManualControlContractError::InvalidSafeValue, groupSafeValidation.detail);
            }
        }
    }

    QString previousActionId;
    for (const Data::ManualActionEnvelope &action : envelope.actionEnvelopes) {
        if (!canonicalIdentifier(action.actionId.value)
            || actionIds.contains(action.actionId.value)) {
            return manualRejection(
                ManualControlContractError::DuplicateIdentifier,
                QStringLiteral("Manual action IDs must be canonical and unique."));
        }
        if (!previousActionId.isEmpty() && action.actionId.value <= previousActionId) {
            return manualRejection(
                ManualControlContractError::NonCanonicalIdentifierOrder,
                QStringLiteral("Manual action envelopes must be sorted by full semantic ID."));
        }
        actionIds.insert(action.actionId.value);
        previousActionId = action.actionId.value;

        bool ambiguous = false;
        const Data::DeviceControlAction *definition = findUnique(
            actionDefinitions,
            action.actionId,
            [](const Data::DeviceControlAction &candidate) { return candidate.id; },
            &ambiguous);
        if (!definition || ambiguous) {
            return manualRejection(
                ManualControlContractError::DefinitionNotFound,
                QStringLiteral("Manual action does not resolve to one exact adapter definition."));
        }
        if (action.enabled && (!definition->enabled || definition->steps.isEmpty())) {
            return manualRejection(
                ManualControlContractError::InexactActionDefinition,
                QStringLiteral(
                    "Enabled manual action requires an enabled nonempty adapter definition."));
        }
        if (!actionHasExactValues(*definition, signalDefinitions)) {
            return manualRejection(
                ManualControlContractError::InexactActionDefinition,
                QStringLiteral(
                    "Manual action literals and parameter references require exact definitions."));
        }
        if (!sortedUniqueActionIds(definition->allowedReleaseActionIds)
            || !sortedUniqueActionIds(definition->allowedTimeoutActionIds)
            || !sortedUniqueActionIds(definition->allowedFailureActionIds)) {
            return manualRejection(
                ManualControlContractError::NonCanonicalIdentifierOrder,
                QStringLiteral("Adapter fallback allow-lists must be sorted and unique."));
        }
        if (action.holdToRun != definition->holdToRun) {
            return manualRejection(
                ManualControlContractError::InvalidTiming,
                QStringLiteral("Manual action hold behavior differs from its adapter definition."));
        }
        if (action.enabled) {
            const ManualControlContractValidation timing
                = validateTiming(action.timing, action.holdToRun);
            if (!timing.accepted())
                return timing;
        }

        QSet<QString> parameterIds;
        QString previousParameterId;
        for (const Data::ManualActionParameterEnvelope &parameter : action.parameters) {
            if (!canonicalIdentifier(parameter.parameterId)
                || parameterIds.contains(parameter.parameterId)
                || (!previousParameterId.isEmpty()
                    && parameter.parameterId <= previousParameterId)) {
                return manualRejection(
                    ManualControlContractError::InvalidParameter,
                    QStringLiteral("Manual action parameter IDs must be canonical and unique."));
            }
            parameterIds.insert(parameter.parameterId);
            previousParameterId = parameter.parameterId;
            const auto parameterDefinition = std::find_if(
                definition->parameters.cbegin(),
                definition->parameters.cend(),
                [&parameter](const Data::DeviceControlActionParameter &candidate) {
                    return candidate.id == parameter.parameterId;
                });
            if (parameterDefinition == definition->parameters.cend()
                || !parameterDefinition->engineeringConstraint
                || !constraintIsSubset(
                        parameter.allowedRange, *parameterDefinition->engineeringConstraint)
                        .accepted()
                || (parameter.defaultValue
                    && !validateEngineeringValueAgainstConstraint(
                            *parameter.defaultValue, parameter.allowedRange)
                            .accepted())) {
                return manualRejection(
                    ManualControlContractError::InvalidParameter,
                    QStringLiteral("Manual action parameter envelope is invalid."));
            }
        }
        for (const Data::DeviceControlActionParameter &parameter : definition->parameters) {
            if (parameter.required && !parameterIds.contains(parameter.id)) {
                return manualRejection(
                    ManualControlContractError::InvalidParameter,
                    QStringLiteral("Required action parameter lacks a manual envelope."));
            }
        }

        if (fallbackContract == ManualControlFallbackContract::SignedControllerRecovery) {
            if (action.holdToRun || !action.releaseActionId.value.isEmpty()
                || !action.timeoutActionId.value.isEmpty()
                || !action.failureActionId.value.isEmpty()
                || !definition->allowedReleaseActionIds.isEmpty()
                || !definition->allowedTimeoutActionIds.isEmpty()
                || !definition->allowedFailureActionIds.isEmpty()) {
                return manualRejection(
                    ManualControlContractError::FallbackNotAllowed,
                    QStringLiteral(
                        "Signed controller recovery cannot depend on semantic fallback actions."));
            }
            continue;
        }

        const struct
        {
            const Data::SemanticActionId &selected;
            const QList<Data::SemanticActionId> &allowed;
        } fallbacks[] = {
            {action.releaseActionId, definition->allowedReleaseActionIds},
            {action.timeoutActionId, definition->allowedTimeoutActionIds},
            {action.failureActionId, definition->allowedFailureActionIds},
        };
        for (const auto &fallback : fallbacks) {
            if (!canonicalIdentifier(fallback.selected.value)) {
                return manualRejection(
                    ManualControlContractError::MissingFallback,
                    QStringLiteral(
                        "Manual action requires exact release, timeout, and failure actions."));
            }
            if (fallback.selected == action.actionId) {
                return manualRejection(
                    ManualControlContractError::SelfReferentialFallback,
                    QStringLiteral("Manual action fallback cannot reference itself."));
            }
            if (!fallback.allowed.contains(fallback.selected)) {
                return manualRejection(
                    ManualControlContractError::FallbackNotAllowed,
                    QStringLiteral("Manual action fallback is not allowed by the adapter."));
            }
            bool fallbackAmbiguous = false;
            const Data::DeviceControlAction *fallbackDefinition = findUnique(
                actionDefinitions,
                fallback.selected,
                [](const Data::DeviceControlAction &candidate) { return candidate.id; },
                &fallbackAmbiguous);
            if (!fallbackDefinition || fallbackAmbiguous) {
                return manualRejection(
                    ManualControlContractError::DefinitionNotFound,
                    QStringLiteral("Manual action fallback definition is missing or ambiguous."));
            }
            if (!terminalFallbackAction(*fallbackDefinition, signalDefinitions)) {
                return manualRejection(
                    ManualControlContractError::FallbackNotTerminal,
                    QStringLiteral(
                        "Manual action fallback must be terminal with exact required defaults."));
            }
        }
    }

    if (envelope.enabled) {
        const bool hasEnabledSignal = std::any_of(
            envelope.signalEnvelopes.cbegin(),
            envelope.signalEnvelopes.cend(),
            [](const Data::ManualSignalEnvelope &signal) { return signal.enabled; });
        const bool hasEnabledAction = std::any_of(
            envelope.actionEnvelopes.cbegin(),
            envelope.actionEnvelopes.cend(),
            [](const Data::ManualActionEnvelope &action) { return action.enabled; });
        if (!hasEnabledSignal && !hasEnabledAction) {
            return manualRejection(
                ManualControlContractError::InvalidEnvelope,
                QStringLiteral("Enabled manual-control envelope has no enabled operation."));
        }
    }
    return {};
}

} // namespace EtherCAT::Core
