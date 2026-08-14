// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackageactivation.h>

#include <utils/result.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

// Durable, self-contained state for one activation operation. The duplicated
// controller/project evidence is intentional: the decoder cross-checks it
// against the immutable record before making the state available to recovery
// code. Recovery must reconcile the recorded provider action against an
// authoritative controller snapshot; it must never replay the action blindly.
struct RuntimePackageActivationJournalState
{
    Data::RuntimePackageActivationRequest request;
    Data::RuntimePackageActivationPhase phase = Data::RuntimePackageActivationPhase::Idle;
    Data::RuntimePackageActivationProviderAction outstandingProviderAction
        = Data::RuntimePackageActivationProviderAction::None;
    std::optional<Data::RuntimePackageActivationControllerEvidence> beforeController;
    std::optional<Data::RuntimePackageActivationControllerEvidence> afterController;
    QList<Data::RuntimePackageActivationControllerEvidence> controllerEvidenceHistory;
    std::optional<Data::RuntimePackageActivationProjectCommit> projectCommit;
    // Set only after the project document has been durably saved. This token
    // intentionally differs from ProjectCommit::resultingDocumentRevision and
    // lets recovery/release reject document drift after the save boundary.
    std::optional<Data::RuntimePackageActivationDocumentRevisionToken> persistedDocumentRevision;
    Data::RuntimePackageActivationRecord record;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageActivationJournalState &,
        const RuntimePackageActivationJournalState &) = default;
};

// The codec emits canonical, sorted, compact ASCII JSON with one terminal line
// feed. Integers wider than 32 bits use fixed-width hexadecimal strings and
// byte arrays use canonical padded Base64, avoiding JSON number precision loss.
Utils::Result<QByteArray> serializeRuntimePackageActivationJournal(
    const RuntimePackageActivationJournalState &state);

// Parsing is fail-closed: duplicate, missing, unknown, noncanonical, oversized,
// internally inconsistent, or semantically invalid data is rejected.
Utils::Result<RuntimePackageActivationJournalState> parseRuntimePackageActivationJournal(
    QByteArrayView bytes);

} // namespace EtherCAT::SemanticRuntime::Internal
