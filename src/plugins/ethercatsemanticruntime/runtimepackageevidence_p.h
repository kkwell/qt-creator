// Copyright (C) 2026 Embed Labs

#pragma once

#include "semanticbindingartifact_p.h"
#include "verifiedecpkgstore_p.h"

#include <ethercatdata/deviceadapterselection.h>
#include <ethercatdata/semanticmappingattestation.h>

#include <utils/result.h>

namespace EtherCAT::SemanticRuntime::Internal {

// Immutable evidence derived from one verified production ECPKG. Construction is
// factory-only so callers cannot pair a valid signed digest with modified bindings.
class VerifiedRuntimePackageEvidence
{
public:
    const VerifiedSemanticBindingArtifact &semanticBindingArtifact() const;
    const Data::RuntimeSemanticMappingProof &semanticMappingProof() const;
    const QByteArray &projectConfigurationSha256() const;

    bool isValid() const;
    bool permitsWritableActions() const;
    const VerifiedSemanticAction *invocableAction(
        QStringView actionBindingId, bool dcRuntimeActive) const;

private:
    VerifiedRuntimePackageEvidence(
        VerifiedSemanticBindingArtifact artifact,
        Data::RuntimeSemanticMappingProof proof,
        QByteArray projectConfigurationSha256,
        bool actionDefinitionsVerified);

    VerifiedSemanticBindingArtifact m_semanticBindingArtifact;
    Data::RuntimeSemanticMappingProof m_semanticMappingProof;
    QByteArray m_projectConfigurationSha256;
    bool m_actionDefinitionsVerified = false;

    friend Utils::Result<VerifiedRuntimePackageEvidence> verifyRuntimePackageEvidence(
        const VerifiedEcpkgPackage &package);
};

// Revalidates the semantic artifact against the exact container, signed manifest,
// and ECFG configuration carried by package. Only production-trusted packages are
// accepted. The proof fields are derived from that cross-validated evidence.
Utils::Result<VerifiedRuntimePackageEvidence> verifyRuntimePackageEvidence(
    const VerifiedEcpkgPackage &package);

// Verifies that one controller attestation belongs to the caller's exact live
// scope/session/epoch and to the independently verified local production package.
// No field is inferred from a name, device position, station address, or PDO offset.
Utils::Result<> verifyRuntimeSemanticMappingAttestation(
    const Data::RuntimeSemanticMappingAttestation &attestation,
    const Data::ControllerConnectionScope &expectedScope,
    quint64 expectedSessionGeneration,
    const Data::RuntimeResourceCatalogEpoch &expectedEpoch,
    const Data::SemanticBindingArtifactReference &expectedArtifactReference,
    const VerifiedRuntimePackageEvidence &localEvidence);

} // namespace EtherCAT::SemanticRuntime::Internal
