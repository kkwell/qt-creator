// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/runtimeresource.h>
#include <ethercatdata/semanticruntime.h>

#include <QByteArray>
#include <QList>
#include <QObject>

#include <optional>

namespace EtherCAT::Core {

enum class SemanticRuntimeValidationError {
    None,
    InvalidOperationId,
    InvalidTarget,
    InvalidRequest,
    InvalidEpoch,
    EpochMismatch,
    BindingUnverified,
    MappingDigestMissing,
    MappingDigestMismatch,
    InvalidBinding,
    ScopeMismatch,
    SessionMismatch,
    ResourceNotFound,
    DescriptorMismatch,
    SnapshotIncomplete,
    SampleNotFound,
    SampleAmbiguous,
    SampleQualityNotGood,
    SampleValueMismatch,
    ApprovalNotRequired,
    ApprovalActorInvalid,
    ApprovalChallengeMismatch,
    CancelActorInvalid,
    CancelBindingMismatch,
    CancelRevisionMismatch,
};

struct ETHERCATCORE_EXPORT SemanticRuntimeValidation
{
    SemanticRuntimeValidationError error = SemanticRuntimeValidationError::None;
    QString detail;

    bool accepted() const { return error == SemanticRuntimeValidationError::None; }

    friend bool operator==(const SemanticRuntimeValidation &, const SemanticRuntimeValidation &)
        = default;
};

struct ETHERCATCORE_EXPORT SemanticRuntimeReadValidation
{
    SemanticRuntimeValidation validation;
    std::optional<Data::RuntimeResourceSample> sample;

    friend bool operator==(
        const SemanticRuntimeReadValidation &, const SemanticRuntimeReadValidation &) = default;
};

ETHERCATCORE_EXPORT bool isValidSemanticRuntimeTarget(const Data::SemanticRuntimeTarget &target);
ETHERCATCORE_EXPORT bool isCompleteRuntimeResourceCatalogEpoch(
    const Data::RuntimeResourceCatalogEpoch &epoch);
ETHERCATCORE_EXPORT bool isCanonicalSha256Digest(const Data::SemanticRuntimeDigest &digest);
ETHERCATCORE_EXPORT bool isCanonicalSemanticOperationId(
    const Data::SemanticOperationId &operationId);
ETHERCATCORE_EXPORT bool isCanonicalSemanticOperationCancelId(
    const Data::SemanticOperationCancelId &cancelId);
ETHERCATCORE_EXPORT bool isAllowedSemanticRuntimeValue(const QVariant &value);
ETHERCATCORE_EXPORT bool isSemanticRuntimeValueCompatible(
    const QVariant &value, const Data::SemanticRuntimeBinding &binding);
ETHERCATCORE_EXPORT QByteArray canonicalSemanticOperationRequest(
    const Data::SemanticOperationRequest &request);
ETHERCATCORE_EXPORT bool semanticOperationRequestsCanonicallyEqual(
    const Data::SemanticOperationRequest &left,
    const Data::SemanticOperationRequest &right);
ETHERCATCORE_EXPORT QByteArray
canonicalSemanticOperationCancelRequest(const Data::SemanticOperationCancelRequest &request);
ETHERCATCORE_EXPORT SemanticRuntimeValidation validateSemanticRuntimeEpoch(
    const Data::RuntimeResourceCatalogEpoch &expected,
    const Data::RuntimeResourceCatalogEpoch &actual);
ETHERCATCORE_EXPORT SemanticRuntimeValidation validateSemanticRuntimeBinding(
    const Data::SemanticRuntimeBinding &binding);
ETHERCATCORE_EXPORT SemanticRuntimeValidation validateSemanticOperationRequest(
    const Data::SemanticOperationRequest &request,
    const Data::SemanticRuntimeContext &context);
ETHERCATCORE_EXPORT SemanticRuntimeValidation validateSemanticOperationApproval(
    const Data::SemanticOperationApprovalRequest &approval,
    const Data::SemanticRuntimeActor &actor,
    const Data::SemanticOperationRecord &operation,
    const Data::SemanticRuntimeContext &context);
ETHERCATCORE_EXPORT SemanticRuntimeValidation validateSemanticOperationCancelRequest(
    const Data::SemanticOperationCancelRequest &request,
    const Data::SemanticRuntimeActor &actor,
    const Data::SemanticOperationRecord &operation);
ETHERCATCORE_EXPORT SemanticRuntimeValidation validateSemanticOperationCancellationEvidence(
    const Data::SemanticOperationCancelRequest &request,
    const Data::SemanticRuntimeActor &actor,
    const Data::SemanticOperationRecord &operation);
ETHERCATCORE_EXPORT SemanticRuntimeReadValidation validateSemanticRuntimeRead(
    const Data::SemanticRuntimeBinding &binding,
    const Data::RuntimeResourceCatalog &catalog,
    const Data::RuntimeResourceSnapshot &snapshot);

class ETHERCATCORE_EXPORT SemanticRuntimeService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Implementations expose value snapshots only. They remain the sole bridge from semantic
    // targets to verified runtime bindings and provider operations.
    virtual QList<Data::SemanticRuntimeContext> contexts() const = 0;
    virtual std::optional<Data::SemanticRuntimeContext> context(
        const QString &controllerId) const;

    // Requests an exact set of public semantic signals. Implementations may
    // return Accepted and later emit liveRefreshCompleted(), or return a
    // terminal result directly. Deferred is a terminal no-work-scheduled
    // response for transient busy/backpressure conditions. Stable result codes
    // are not translated; detail is presentation text. The base service never
    // resolves a binding or reaches a controller.
    virtual Data::SemanticLiveRefreshResult requestLiveRefresh(
        const Data::SemanticLiveRefreshRequest &request);

    // The base implementation is deliberately fail-closed and never attempts execution.
    virtual Data::SemanticOperationRecord submit(
        const Data::SemanticOperationRequest &request,
        const Data::SemanticRuntimeActor &actor);
    virtual Data::SemanticOperationRecord approve(
        const Data::SemanticOperationApprovalRequest &approval,
        const Data::SemanticRuntimeActor &actor);
    virtual std::optional<Data::SemanticOperationRecord> operation(
        const Data::SemanticOperationId &operationId) const;
    virtual QList<Data::SemanticRuntimeAuditEvent> audit(
        const QString &controllerId, quint64 afterSequence = 0) const;
    virtual Data::SemanticOperationCancelResult cancel(
        const Data::SemanticOperationCancelRequest &request,
        const Data::SemanticRuntimeActor &actor);

signals:
    void contextsChanged();
    void liveRefreshCompleted(const EtherCAT::Data::SemanticLiveRefreshResult &result);
    void operationChanged(const EtherCAT::Data::SemanticOperationId &operationId);
    void auditChanged(const QString &controllerId);
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::SemanticRuntimeValidationError)
Q_DECLARE_METATYPE(EtherCAT::Core::SemanticRuntimeValidation)
Q_DECLARE_METATYPE(EtherCAT::Core::SemanticRuntimeReadValidation)
