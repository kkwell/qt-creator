// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/runtimepackageactivation.h>

#include <QObject>

#include <optional>

namespace EtherCAT::Core {

enum class RuntimePackageActivationCommandDisposition {
    Accepted,
    IdempotentReplay,
    Conflict,
    StaleRevision,
    TooLate,
    ReconciliationRequired,
    NotAllowed,
    NotFound,
    InvalidRequest,
};

struct ETHERCATCORE_EXPORT RuntimePackageActivationCommandResult
{
    RuntimePackageActivationCommandDisposition disposition
        = RuntimePackageActivationCommandDisposition::InvalidRequest;
    std::optional<Data::RuntimePackageActivationRecord> record;
    QString detail;

    bool isValid() const
    {
        const bool detailIsValid = !detail.isEmpty() && detail == detail.trimmed();
        switch (disposition) {
        case RuntimePackageActivationCommandDisposition::Accepted:
        case RuntimePackageActivationCommandDisposition::IdempotentReplay:
            return record && record->isValid() && detail.isEmpty();
        case RuntimePackageActivationCommandDisposition::Conflict:
        case RuntimePackageActivationCommandDisposition::StaleRevision:
        case RuntimePackageActivationCommandDisposition::TooLate:
        case RuntimePackageActivationCommandDisposition::ReconciliationRequired:
        case RuntimePackageActivationCommandDisposition::NotAllowed:
            return record && record->isValid() && detailIsValid;
        case RuntimePackageActivationCommandDisposition::NotFound:
        case RuntimePackageActivationCommandDisposition::InvalidRequest:
            return !record && detailIsValid;
        }
        return false;
    }

    bool accepted() const
    {
        return disposition == RuntimePackageActivationCommandDisposition::Accepted
               || disposition
                      == RuntimePackageActivationCommandDisposition::IdempotentReplay;
    }

    friend bool operator==(
        const RuntimePackageActivationCommandResult &,
        const RuntimePackageActivationCommandResult &) = default;
};

// Owns the asynchronous IDE-local transaction around one runtime package
// activation. The Core contract contains no controller protocol, companion
// schema, or project hashing implementation. Concrete services must validate
// all opaque evidence before any provider call.
//
// start() is OperationId-idempotent: an exact fingerprint replay returns the
// existing record with IdempotentReplay and performs no write, signal, or
// provider call; a different fingerprint returns Conflict.
//
// On startup, implementations must scan persisted nonterminal records before
// accepting start/cancel/resume work. Such records stay frozen until their
// outstanding provider request and control lease are authoritatively closed;
// an implementation must not blindly replay a mutation. reconcile() is the
// only recovery entry after AwaitingReconciliation was durably recorded. It is
// repeatable and preserves OutcomeUnknown while Reconciling.
//
// cancel() uses the record revision and original project CAS tokens. It is
// accepted only in runtimePackageActivationPhaseAllowsCancel() phases, before
// any provider request can be outstanding. Unknown and Reconciling records
// return ReconciliationRequired; cancellation from AcquiringControl or later
// returns TooLate. A repeated exact cancel is idempotent.
//
// Package activation proves an exact signed package/attestation and project
// binding only. Activation requires the captured pre-deploy controller state
// to already be Shutdown; there is no synthetic configuration phase here. The
// high-level DeployPackage action retains its complete Product API progress,
// including any internal rollback, as RuntimePackageActivationDeploymentEvidence.
// It is not the Product API ActivatePackage wire command. Start, StartFreeRun,
// and StartDistributedClocks remain outside this service.
class ETHERCATCORE_EXPORT RuntimePackageActivationService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual RuntimePackageActivationCommandResult start(
        const Data::RuntimePackageActivationRequest &request) = 0;
    virtual RuntimePackageActivationCommandResult reconcile(
        const Data::RuntimePackageActivationOperationId &operationId) = 0;
    virtual RuntimePackageActivationCommandResult cancel(
        const Data::RuntimePackageActivationCancelRequest &request) = 0;
    virtual std::optional<Data::RuntimePackageActivationRecord> record(
        const Data::RuntimePackageActivationOperationId &operationId) const = 0;
    virtual Data::RuntimePackageActivationSnapshot snapshot() const = 0;

signals:
    void recordChanged(const EtherCAT::Data::RuntimePackageActivationRecord &record);
    void snapshotChanged(const EtherCAT::Data::RuntimePackageActivationSnapshot &snapshot);
    void statusChanged(
        const EtherCAT::Data::RuntimePackageActivationOperationId &operationId,
        EtherCAT::Data::RuntimePackageActivationPhase phase,
        EtherCAT::Data::RuntimePackageActivationOutcome outcome);
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageActivationCommandDisposition)
Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageActivationCommandResult)
