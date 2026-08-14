// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatdata_global.h"

#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Data {

// Exact engineering values deliberately avoid floating-point conversion. A rational is canonical
// only when denominator is positive, numerator and denominator are coprime, and zero is 0/1.
struct ETHERCATDATA_EXPORT ExactRational
{
    qint64 numerator = 0;
    qint64 denominator = 1;

    friend bool operator==(const ExactRational &, const ExactRational &) = default;
};

enum class EngineeringValueKind {
    Invalid,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    ExactRational,
    Enumeration,
};

struct ETHERCATDATA_EXPORT EngineeringValue
{
    EngineeringValueKind kind = EngineeringValueKind::Invalid;
    bool boolean = false;
    qint64 signedInteger = 0;
    quint64 unsignedInteger = 0;
    ExactRational rational;
    QString enumerationName;

    static EngineeringValue fromBoolean(bool value)
    {
        EngineeringValue result;
        result.kind = EngineeringValueKind::Boolean;
        result.boolean = value;
        return result;
    }

    static EngineeringValue fromSignedInteger(qint64 value)
    {
        EngineeringValue result;
        result.kind = EngineeringValueKind::SignedInteger;
        result.signedInteger = value;
        return result;
    }

    static EngineeringValue fromUnsignedInteger(quint64 value)
    {
        EngineeringValue result;
        result.kind = EngineeringValueKind::UnsignedInteger;
        result.unsignedInteger = value;
        return result;
    }

    static EngineeringValue fromExactRational(const ExactRational &value)
    {
        EngineeringValue result;
        result.kind = EngineeringValueKind::ExactRational;
        result.rational = value;
        return result;
    }

    static EngineeringValue fromEnumeration(const QString &name)
    {
        EngineeringValue result;
        result.kind = EngineeringValueKind::Enumeration;
        result.enumerationName = name;
        return result;
    }

    friend bool operator==(const EngineeringValue &, const EngineeringValue &) = default;
};

enum class EngineeringRounding {
    RejectInexact,
    TowardZero,
    TowardNegativeInfinity,
    TowardPositiveInfinity,
    NearestTiesToEven,
};

struct ETHERCATDATA_EXPORT EngineeringEnumerationValue
{
    QString id;
    QString displayName;
    ExactRational value;

    friend bool operator==(const EngineeringEnumerationValue &, const EngineeringEnumerationValue &)
        = default;
};

struct ETHERCATDATA_EXPORT EngineeringConstraint
{
    std::optional<ExactRational> minimum;
    std::optional<ExactRational> maximum;
    std::optional<ExactRational> step;
    std::optional<ExactRational> stepOrigin;
    QList<EngineeringEnumerationValue> enumeration;

    friend bool operator==(const EngineeringConstraint &, const EngineeringConstraint &) = default;
};

struct ETHERCATDATA_EXPORT EngineeringTransform
{
    // The only supported formula is engineering = raw * scale + offset.
    ExactRational scale{1, 1};
    ExactRational offset{0, 1};
    QString unit;
    EngineeringConstraint constraint;
    std::optional<EngineeringRounding> rounding;

    friend bool operator==(const EngineeringTransform &, const EngineeringTransform &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ExactRational)
Q_DECLARE_METATYPE(EtherCAT::Data::EngineeringValueKind)
Q_DECLARE_METATYPE(EtherCAT::Data::EngineeringValue)
Q_DECLARE_METATYPE(EtherCAT::Data::EngineeringRounding)
Q_DECLARE_METATYPE(EtherCAT::Data::EngineeringEnumerationValue)
Q_DECLARE_METATYPE(EtherCAT::Data::EngineeringConstraint)
Q_DECLARE_METATYPE(EtherCAT::Data::EngineeringTransform)
