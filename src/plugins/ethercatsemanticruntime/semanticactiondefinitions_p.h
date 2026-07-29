// Copyright (C) 2026 Embed Labs

#pragma once

#include "semanticbindingartifact_p.h"

#include <utils/result.h>

#include <QByteArray>

namespace EtherCAT::SemanticRuntime::Internal {

constexpr qsizetype defaultMaximumSemanticActionDefinitionsBytes = 8 * 1024 * 1024;

// Immutable proof that every action definition used by one signed semantic
// binding artifact was independently reconstructed from the companion payload.
struct VerifiedSemanticActionDefinitions
{
    QByteArray canonicalDefinitions;
    QByteArray definitionsSha256;
    QByteArray semanticBindingArtifactSha256;
    quint32 definitionCount = 0;
    quint32 actionCount = 0;

    bool isValid() const;
};

Utils::Result<VerifiedSemanticActionDefinitions> verifySemanticActionDefinitions(
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const VerifiedSemanticBindingArtifact &artifact,
    qsizetype maximumDefinitionsBytes = defaultMaximumSemanticActionDefinitionsBytes);

} // namespace EtherCAT::SemanticRuntime::Internal
