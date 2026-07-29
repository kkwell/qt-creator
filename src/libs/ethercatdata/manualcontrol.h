// Copyright (C) 2026 Kvell

#pragma once

#include "engineeringvalue.h"
#include "ethercatdata_global.h"
#include "semanticids.h"

#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Data {

// The three time limits have intentionally different meanings:
// - commandTtlMs: validity before a controller accepts one output transaction;
// - refreshTimeoutMs: maximum gap between refreshes of a held command;
// - maxContinuousHoldMs: absolute hold duration even while refreshes continue.
struct ETHERCATDATA_EXPORT ManualCommandTiming
{
    quint32 commandTtlMs = 0;
    quint32 refreshTimeoutMs = 0;
    quint32 maxContinuousHoldMs = 0;

    friend bool operator==(const ManualCommandTiming &, const ManualCommandTiming &) = default;
};

struct ETHERCATDATA_EXPORT ManualSignalSafeValue
{
    SemanticSignalId signalId;
    EngineeringValue value;

    friend bool operator==(const ManualSignalSafeValue &, const ManualSignalSafeValue &) = default;
};

struct ETHERCATDATA_EXPORT ManualSignalEnvelope
{
    SemanticSignalId signalId;
    bool enabled = false;
    bool holdToRun = false;
    ManualCommandTiming timing;
    EngineeringConstraint allowedRange;
    std::optional<EngineeringValue> safeValue;
    QList<ManualSignalSafeValue> consistencyGroupSafeValues;

    friend bool operator==(const ManualSignalEnvelope &, const ManualSignalEnvelope &) = default;
};

struct ETHERCATDATA_EXPORT ManualActionParameterEnvelope
{
    QString parameterId;
    EngineeringConstraint allowedRange;
    std::optional<EngineeringValue> defaultValue;

    friend bool operator==(
        const ManualActionParameterEnvelope &, const ManualActionParameterEnvelope &) = default;
};

struct ETHERCATDATA_EXPORT ManualActionEnvelope
{
    SemanticActionId actionId;
    bool enabled = false;
    bool holdToRun = false;
    ManualCommandTiming timing;
    QList<ManualActionParameterEnvelope> parameters;

    // Fallbacks address exact semantic actions. They are never inferred from names, positions,
    // units, or vendor data.
    SemanticActionId releaseActionId;
    SemanticActionId timeoutActionId;
    SemanticActionId failureActionId;

    friend bool operator==(const ManualActionEnvelope &, const ManualActionEnvelope &) = default;
};

struct ETHERCATDATA_EXPORT ManualControlEnvelope
{
    // Existing adapter manifests remain fail-closed: absence of an envelope, or enabled=false,
    // cannot authorize manual writes or action execution.
    bool enabled = false;
    QList<ManualSignalEnvelope> signalEnvelopes;
    QList<ManualActionEnvelope> actionEnvelopes;

    friend bool operator==(const ManualControlEnvelope &, const ManualControlEnvelope &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ManualCommandTiming)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualSignalSafeValue)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualSignalEnvelope)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualActionParameterEnvelope)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualActionEnvelope)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualControlEnvelope)
