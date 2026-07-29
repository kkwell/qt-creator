// Copyright (C) 2026 Embed Labs

#pragma once

#include "ecpkgcontainer.h"

#include <utils/result.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

constexpr qsizetype maximumEcpkgTrustedPublicKeys = 32;
constexpr qsizetype defaultMaximumCompiledProjectBytes = 16 * 1024 * 1024;

enum class EcpkgTrustClass {
    Production,
    Engineering,
};

struct EcpkgTrustedPublicKey
{
    QByteArray rawPublicKey;
    EcpkgTrustClass trust = EcpkgTrustClass::Engineering;
};

struct EcpkgDigestRecord
{
    quint32 bytes = 0;
    QByteArray sha256;

    friend bool operator==(const EcpkgDigestRecord &, const EcpkgDigestRecord &) = default;
};

struct SignedEcpkgSemanticBindingSummary
{
    quint16 formatVersion = 0;
    quint32 bindingCount = 0;
    quint64 catalogRevision = 0;
    quint64 topologyIdentity = 0;
    QByteArray mappingSha256;
    QByteArray resourceRecordsSha256;
    QByteArray resourceSectionSha256;
    QByteArray topologySha256;
    quint16 actionDefinitionsFormatVersion = 0;
    quint32 actionDefinitionCount = 0;
    QByteArray actionDefinitionsSha256;
};

struct VerifiedSignedEcpkgManifest
{
    quint16 formatVersion = 0;
    EcpkgTrustClass trust = EcpkgTrustClass::Engineering;
    quint64 configurationId = 0;
    QByteArray packageSha256;
    QByteArray manifestSha256;
    QByteArray signingKeyIdSha256;
    EcpkgDigestRecord capability;
    EcpkgDigestRecord configuration;
    EcpkgDigestRecord runtime;
    EcpkgDigestRecord compileReport;
    std::optional<EcpkgDigestRecord> actionDefinitions;
    EcpkgDigestRecord compiledProjectSource;
    // This is only the signed manifest summary. The semantic artifact and ECFG resource
    // section still require independent verification before constructing a mapping proof.
    std::optional<SignedEcpkgSemanticBindingSummary> semanticBinding;
};

// The container must come from parseCanonicalEcpkgContainer(). Every successful result has
// a verified raw Ed25519 signature and an exact trust-domain match.
Utils::Result<VerifiedSignedEcpkgManifest> verifySignedEcpkgManifest(
    const EcpkgContainer &container, const QList<EcpkgTrustedPublicKey> &trustedPublicKeys);

// Source bytes are external to ECPKG. Call this separately when the exact compiled project
// is available; a signed source record alone does not prove possession of those bytes.
Utils::Result<> verifyEcpkgCompiledProjectSource(
    const VerifiedSignedEcpkgManifest &manifest,
    QByteArrayView compiledProject,
    qsizetype maximumCompiledProjectBytes = defaultMaximumCompiledProjectBytes);

} // namespace EtherCAT::SemanticRuntime::Internal
