// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

#include <QByteArrayView>

namespace EtherCAT::ProjectCompiler {

// The typed compiler request deliberately retains only the digest of the
// project document bytes. Recovery therefore carries those exact bytes beside
// the request so ProjectSnapshotEvidence can be reconstructed without reading
// or reserializing the current project after a restart.
struct RuntimePackageCompilerCompileRecovery
{
    Data::RuntimePackageCompilerCompileRequest request;
    QByteArray serializedProject;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerCompileRecovery &, const RuntimePackageCompilerCompileRecovery &)
        = default;
};

// Canonical recovery-v1 JSON wraps a versioned, platform-independent binary
// projection of every typed request field plus serializedProject. The encoder
// accepts only an atomic ProjectService capture; it must not reconstruct either
// side from the current project. It contains public compiler inputs only and
// never carries a private signing key.
Utils::Result<QByteArray> encodeRuntimePackageCompilerCompileRecovery(
    const RuntimePackageCompilerCompileRecovery &recovery);

// expectedRecoverySha256 must cover every byte in canonicalRecovery and come
// from the independently persisted operation ledger. Digests inside the file
// are integrity metadata, not authentication anchors.
Utils::Result<RuntimePackageCompilerCompileRecovery> decodeRuntimePackageCompilerCompileRecovery(
    QByteArrayView canonicalRecovery,
    const Data::RuntimePackageCompilerSha256 &expectedRecoverySha256);

} // namespace EtherCAT::ProjectCompiler
