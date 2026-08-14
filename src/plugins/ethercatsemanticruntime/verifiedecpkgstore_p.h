// Copyright (C) 2026 Embed Labs

#pragma once

#include "ecfgconfiguration_p.h"
#include "signedecpkgmanifest_p.h"

#include <utils/result.h>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

namespace EtherCAT::SemanticRuntime::Internal {

constexpr qsizetype maximumVerifiedEcpkgStoreEntries = 4096;

// A package in this type has a verified production manifest, exact payload digests,
// a strict ECFG configuration, and an exact compiled-project source match. The
// semanticBinding member remains a signed summary only; this type deliberately does
// not claim that compile_report.json's complete semantic artifact has been verified.
struct VerifiedEcpkgPackage
{
    QByteArray packageBytes;
    EcpkgContainer container;
    VerifiedSignedEcpkgManifest manifest;
    EcfgConfiguration configuration;
    QString storedFilePath;
};

Utils::Result<VerifiedEcpkgPackage> verifyProductionEcpkg(
    QByteArrayView packageBytes,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource);

// Imports only after complete in-memory verification. The caller supplies the store
// root and trust roots explicitly. The package is stored as:
//   <storeRoot>/sha256/<lowercase-package-sha256>.ecpkg
Utils::Result<VerifiedEcpkgPackage> importVerifiedEcpkg(
    const QString &storeRoot,
    QByteArrayView packageBytes,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource);

Utils::Result<VerifiedEcpkgPackage> loadVerifiedEcpkgByPackageSha256(
    const QString &storeRoot,
    QByteArrayView packageSha256,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource);

// Searches the verified content store for exactly one package with both the signed
// semantic-mapping digest and the exact compiled-project source. Multiple matching
// package identities are rejected as ambiguous.
Utils::Result<VerifiedEcpkgPackage> findVerifiedEcpkgBySemanticMapping(
    const QString &storeRoot,
    QByteArrayView semanticMappingSha256,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource);

} // namespace EtherCAT::SemanticRuntime::Internal
