// Copyright (C) 2026 Embed Labs

#pragma once

#include "runtimepackageevidence_p.h"

#include <ethercatdata/runtimeoutputtransaction.h>
#include <ethercatdata/semanticruntime.h>

#include <utils/result.h>

#include <QList>

#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

class SemanticActionPlanBuilder;

enum class SemanticActionPlanStepKind {
    WriteGroup,
    WaitMasked,
    WaitAbsoluteLimit,
};

class SemanticActionPlanGroup final
{
public:
    const Data::RuntimeConsistencyGroupId &consistencyGroupId() const;
    Data::RuntimeOutputRecoveryPolicy recoveryPolicy() const;
    quint32 maximumTtlCycles() const;
    const QByteArray &completeGroupRecordDigest() const;
    quint32 completeResourceCount() const;
    const QList<Data::RuntimeResourceId> &completeResourceIds() const;

private:
    Data::RuntimeConsistencyGroupId m_consistencyGroupId;
    Data::RuntimeOutputRecoveryPolicy m_recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
    quint32 m_maximumTtlCycles = 0;
    QByteArray m_completeGroupRecordDigest;
    quint32 m_completeResourceCount = 0;
    QList<Data::RuntimeResourceId> m_completeResourceIds;

    friend class SemanticActionPlanBuilder;
};

class SemanticActionPlanStep final
{
public:
    quint32 index() const;
    SemanticActionPlanStepKind kind() const;

    // WriteGroup-only fields. completeGroupWrites contains the complete signed
    // consistency group in provider canonical RuntimeResourceId order.
    const Data::RuntimeConsistencyGroupId &consistencyGroupId() const;
    const Data::RuntimeOutputOperationId &outputOperationId() const;
    const QList<Data::RuntimeOutputValueWrite> &completeGroupWrites() const;

    // Wait-only fields. Mask/value are used by WaitMasked; absoluteLimit is
    // used by WaitAbsoluteLimit.
    const std::optional<Data::SemanticRuntimeBinding> &waitBinding() const;
    quint64 mask() const;
    quint64 expectedValue() const;
    quint64 absoluteLimit() const;
    quint32 timeoutCycles() const;

private:
    quint32 m_index = 0;
    SemanticActionPlanStepKind m_kind = SemanticActionPlanStepKind::WriteGroup;
    Data::RuntimeConsistencyGroupId m_consistencyGroupId;
    Data::RuntimeOutputOperationId m_outputOperationId;
    QList<Data::RuntimeOutputValueWrite> m_completeGroupWrites;
    std::optional<Data::SemanticRuntimeBinding> m_waitBinding;
    quint64 m_mask = 0;
    quint64 m_expectedValue = 0;
    quint64 m_absoluteLimit = 0;
    quint32 m_timeoutCycles = 0;

    friend class SemanticActionPlanBuilder;
};

// Factory-only value type. It owns copies of every signed group, step, binding,
// and resolved value needed for execution; no pointer into package evidence or
// the public runtime context survives construction.
class SemanticActionPlan final
{
public:
    const Data::SemanticOperationRequest &request() const;
    const QByteArray &canonicalRequestDigest() const;
    const QString &actionBindingId() const;
    const QString &actionDefinitionId() const;
    const QByteArray &actionDefinitionDigest() const;
    const QByteArray &actionDefinitionsDigest() const;
    const Data::ControllerConnectionScope &scope() const;
    quint64 sessionGeneration() const;
    const Data::RuntimeResourceCatalogEpoch &epoch() const;
    const QByteArray &mappingDigest() const;
    const QByteArray &contextHash() const;
    quint32 cyclePeriodNs() const;
    quint32 ttlCycles() const;
    const QList<SemanticActionPlanGroup> &groups() const;
    const QList<SemanticActionPlanStep> &steps() const;

private:
    Data::SemanticOperationRequest m_request;
    QByteArray m_canonicalRequestDigest;
    QString m_actionBindingId;
    QString m_actionDefinitionId;
    QByteArray m_actionDefinitionDigest;
    QByteArray m_actionDefinitionsDigest;
    Data::ControllerConnectionScope m_scope;
    quint64 m_sessionGeneration = 0;
    Data::RuntimeResourceCatalogEpoch m_epoch;
    QByteArray m_mappingDigest;
    QByteArray m_contextHash;
    quint32 m_cyclePeriodNs = 0;
    quint32 m_ttlCycles = 0;
    QList<SemanticActionPlanGroup> m_groups;
    QList<SemanticActionPlanStep> m_steps;

    friend class SemanticActionPlanBuilder;
};

// Builds an execution-only plan from one verified production package and the
// already projected public request/context. It revalidates both inputs, resolves
// signed constants/parameters to generic typed values, and performs no Provider
// calls. Unqualified or disabled actions always fail closed.
Utils::Result<SemanticActionPlan> buildSemanticActionPlan(
    const VerifiedRuntimePackageEvidence &evidence,
    const Data::SemanticOperationRequest &request,
    const Data::SemanticRuntimeContext &context);

} // namespace EtherCAT::SemanticRuntime::Internal
