// Copyright (C) 2026 Embed Labs

#pragma once

#include "signedecpkgmanifest_p.h"

#include <utils/result.h>

#include <QList>
#include <QString>

namespace EtherCAT::SemanticRuntime::Internal {

// Loads an explicit production trust directory. Every accepted entry is a raw
// 32-byte Ed25519 public key named by its lowercase SHA-256 key ID:
//   <64-lowercase-hex-characters>.pub
//
// The directory must already exist at an absolute path. Symlinks, unexpected
// entries, empty stores, and changes observed while loading fail closed.
// Selecting and protecting that directory remains the caller/installer's
// responsibility; this loader never falls back to an engineering trust source.
Utils::Result<QList<EcpkgTrustedPublicKey>> loadProductionEcpkgTrustStore(
    const QString &absoluteTrustDirectory);

} // namespace EtherCAT::SemanticRuntime::Internal
