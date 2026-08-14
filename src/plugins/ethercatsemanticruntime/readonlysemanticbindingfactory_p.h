// Copyright (C) 2026 Embed Labs

#pragma once

#include "runtimepackageevidence_p.h"

#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/semanticruntime.h>

#include <utils/result.h>

#include <QStringView>

namespace EtherCAT::SemanticRuntime::Internal {

// Non-executable candidates derived only from explicit signed identities. Bindings retain the
// exact catalog direction and access for descriptor validation, but every signal state remains
// Unverified and its manual-control policy is disabled until a snapshot and the separate writable
// action evidence have been verified. No action candidates are produced here.
struct ReadOnlySemanticBindingCandidates
{
    Data::SemanticBindingVerification verification;
    Data::SemanticRuntimeDigest mappingDigest;
    Data::SemanticRuntimeDigest controllerMappingDigest;
    QList<Data::SemanticRuntimeBinding> bindings;
    QList<Data::SemanticSignalRuntimeState> signalStates;
};

// Builds one complete, fail-closed candidate set for a verified semantic-binding-v2 package.
// Project devices are resolved exclusively through ProjectSnapshot::masterBindingArtifact's
// projectDeviceBindings. Runtime resources are resolved exclusively through signed ResourceIds;
// display names, positions, station addresses, and Process Image offsets are never lookup keys.
Utils::Result<ReadOnlySemanticBindingCandidates> buildReadOnlySemanticBindingCandidates(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const Data::RuntimeResourceCatalog &catalog,
    const Data::RuntimeSemanticMappingAttestation &attestation);

} // namespace EtherCAT::SemanticRuntime::Internal
