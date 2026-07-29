// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/engineeringvalue.h>
#include <ethercatdata/manualcontrol.h>

#include <QString>

#include <optional>

namespace EtherCAT::Core {

enum class EngineeringContractError {
    None,
    InvalidRational,
    InvalidValue,
    InvalidConstraint,
    InvalidTransform,
    ScaleIsZero,
    ArithmeticOverflow,
    InvalidRawFormat,
    RawValueOutOfRange,
    InexactConversion,
    ConstraintRangeViolation,
    ConstraintStepViolation,
    ConstraintEnumerationViolation,
};

struct ETHERCATCORE_EXPORT EngineeringContractValidation
{
    EngineeringContractError error = EngineeringContractError::None;
    QString detail;

    bool accepted() const { return error == EngineeringContractError::None; }

    friend bool operator==(
        const EngineeringContractValidation &, const EngineeringContractValidation &) = default;
};

struct ETHERCATCORE_EXPORT ExactRationalResult
{
    EngineeringContractValidation validation;
    std::optional<Data::ExactRational> value;

    friend bool operator==(const ExactRationalResult &, const ExactRationalResult &) = default;
};

struct ETHERCATCORE_EXPORT EngineeringConversionResult
{
    EngineeringContractValidation validation;
    std::optional<Data::EngineeringValue> value;

    friend bool operator==(const EngineeringConversionResult &, const EngineeringConversionResult &)
        = default;
};

ETHERCATCORE_EXPORT ExactRationalResult
normalizedExactRational(qint64 numerator, qint64 denominator);
ETHERCATCORE_EXPORT EngineeringContractValidation
validateExactRational(const Data::ExactRational &value);
ETHERCATCORE_EXPORT EngineeringContractValidation
validateEngineeringValue(const Data::EngineeringValue &value);
ETHERCATCORE_EXPORT EngineeringContractValidation
validateEngineeringConstraint(const Data::EngineeringConstraint &constraint);
ETHERCATCORE_EXPORT EngineeringContractValidation
validateEngineeringTransform(const Data::EngineeringTransform &transform);
ETHERCATCORE_EXPORT EngineeringContractValidation validateEngineeringValueAgainstConstraint(
    const Data::EngineeringValue &value, const Data::EngineeringConstraint &constraint);

// The conversion formula is always engineering = raw * scale + offset. rawKind must be Boolean,
// SignedInteger, or UnsignedInteger. The inverse uses transform.rounding and never clamps or wraps.
ETHERCATCORE_EXPORT EngineeringConversionResult convertRawToEngineering(
    const Data::EngineeringValue &raw,
    quint32 bitWidth,
    const Data::EngineeringTransform &transform);
ETHERCATCORE_EXPORT EngineeringConversionResult convertEngineeringToRaw(
    const Data::EngineeringValue &engineering,
    Data::EngineeringValueKind rawKind,
    quint32 bitWidth,
    const Data::EngineeringTransform &transform);

enum class ManualControlContractError {
    None,
    InvalidEnvelope,
    DuplicateIdentifier,
    NonCanonicalIdentifierOrder,
    DefinitionNotFound,
    SignalNotPublic,
    SignalNotWritable,
    ExactTransformMissing,
    InvalidTiming,
    InvalidSafeValue,
    InvalidParameter,
    InexactActionDefinition,
    ConstraintNotSubset,
    MissingFallback,
    SelfReferentialFallback,
    FallbackNotAllowed,
    FallbackNotTerminal,
};

struct ETHERCATCORE_EXPORT ManualControlContractValidation
{
    ManualControlContractError error = ManualControlContractError::None;
    QString detail;

    bool accepted() const { return error == ManualControlContractError::None; }

    friend bool operator==(
        const ManualControlContractValidation &, const ManualControlContractValidation &) = default;
};

// This validates the project-owned envelope against one exact adapter definition. It never
// authorizes execution: callers must additionally require envelope.enabled and the later signed
// runtime binding/approval gates.
ETHERCATCORE_EXPORT ManualControlContractValidation validateManualControlEnvelope(
    const Data::ManualControlEnvelope &envelope,
    const QList<Data::SemanticSignalDefinition> &signalDefinitions,
    const QList<Data::DeviceControlAction> &actionDefinitions);

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::EngineeringContractError)
Q_DECLARE_METATYPE(EtherCAT::Core::EngineeringContractValidation)
Q_DECLARE_METATYPE(EtherCAT::Core::ExactRationalResult)
Q_DECLARE_METATYPE(EtherCAT::Core::EngineeringConversionResult)
Q_DECLARE_METATYPE(EtherCAT::Core::ManualControlContractError)
Q_DECLARE_METATYPE(EtherCAT::Core::ManualControlContractValidation)
