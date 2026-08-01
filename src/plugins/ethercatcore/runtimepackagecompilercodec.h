// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

namespace EtherCAT::Core {

// Exact output from one independently provisioned API-042 compiler process.
// The codec accepts no merged streams: stdout/stderr and process termination
// remain separate so extra output can never be mistaken for a domain result.
struct ETHERCATCORE_EXPORT RuntimePackageCompilerProcessOutput
{
    bool exitedNormally = false;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerCanonicalJson>
encodeRuntimePackageCompilerCompileRequest(const Data::RuntimePackageCompilerCompileRequest &request);

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerCanonicalJson>
encodeRuntimePackageCompilerFinalizeRequest(
    const Data::RuntimePackageCompilerFinalizeRequest &request);

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerCanonicalJson>
encodeRuntimePackageCompilerVerifyRequest(const Data::RuntimePackageCompilerVerifyRequest &request);

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerCompileResult>
decodeRuntimePackageCompilerCompileResult(
    const Data::RuntimePackageCompilerCompileRequest &request,
    const RuntimePackageCompilerProcessOutput &output,
    const Data::RuntimePackageCompilerCanonicalJson &signRequest);

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerFinalizeResult>
decodeRuntimePackageCompilerFinalizeResult(
    const Data::RuntimePackageCompilerFinalizeRequest &request,
    const RuntimePackageCompilerProcessOutput &output,
    const QByteArray &packageBytes);

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerQueryResult>
decodeRuntimePackageCompilerQueryResult(
    const Data::RuntimePackageCompilerQueryRequest &request,
    const RuntimePackageCompilerProcessOutput &output);

ETHERCATCORE_EXPORT Utils::Result<Data::RuntimePackageCompilerVerifyResult>
decodeRuntimePackageCompilerVerifyResult(
    const Data::RuntimePackageCompilerVerifyRequest &request,
    const RuntimePackageCompilerProcessOutput &output);

} // namespace EtherCAT::Core
