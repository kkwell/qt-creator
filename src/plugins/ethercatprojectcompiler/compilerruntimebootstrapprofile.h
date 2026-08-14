// Copyright (C) 2026 Embed Labs

#pragma once

#include "compilerpythonruntimeprofile.h"
#include "compilerruntimebundleprofile.h"

#include <utils/filepath.h>
#include <utils/result.h>

namespace EtherCAT::ProjectCompiler {

// Read-only product bootstrap for one externally provisioned compiler bundle
// and one externally provisioned relocatable Python runtime. The expectation
// file and both raw release keys are administrator-owned trust inputs outside
// the received runtime trees. Loading never installs or executes either tree.
class CompilerRuntimeBootstrapProfile
{
public:
    static Utils::Result<CompilerRuntimeBootstrapProfile> load(
        const Utils::FilePath &expectationFile);

    Utils::Result<> validateCurrent() const;

    Utils::FilePath expectationFile() const;
    const Data::RuntimePackageCompilerSha256 &provisioningProfileSha256() const;
    const CompilerRuntimeBundleProfile &compilerRuntime() const;
    const CompilerPythonRuntimeProfile &pythonRuntime() const;

private:
    static Utils::Result<CompilerRuntimeBootstrapProfile> loadImpl(
        const Utils::FilePath &expectationFile);

    Utils::FilePath m_expectationFile;
    Utils::FilePath m_compilerReleaseKeyFile;
    Utils::FilePath m_pythonReleaseKeyFile;
    QByteArray m_exactExpectationBytes;
    QByteArray m_exactCompilerReleaseKey;
    QByteArray m_exactPythonReleaseKey;
    Data::RuntimePackageCompilerSha256 m_provisioningProfileSha256;
    CompilerRuntimeBundleProfile m_compilerRuntime;
    CompilerPythonRuntimeProfile m_pythonRuntime;
};

} // namespace EtherCAT::ProjectCompiler
