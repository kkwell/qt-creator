// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/filepath.h>
#include <utils/result.h>

namespace EtherCAT::ProjectCompiler {

struct CompilerPythonRuntimeExpectation
{
    QString companionVersion;
    QByteArray signingPublicKey;
    Data::RuntimePackageCompilerSha256 companionBundleSha256;
    Data::RuntimePackageCompilerSha256 companionManifestSha256;
    Data::RuntimePackageCompilerSha256 pythonExecutableSha256;
    Data::RuntimePackageCompilerSha256 installedTreeSha256;
    Data::RuntimePackageCompilerSha256 portableIdentitySha256;

    bool isValid() const;

    friend bool operator==(
        const CompilerPythonRuntimeExpectation &,
        const CompilerPythonRuntimeExpectation &) = default;
};

struct CompilerPythonRuntimeIdentity
{
    QString companionId;
    QString companionVersion;
    QString pythonVersion;
    QString pythonCacheTag;
    QString pythonSoAbi;
    QString runtimeReleaseTag;
    Data::RuntimePackageCompilerSha256 companionBundleSha256;
    Data::RuntimePackageCompilerSha256 companionManifestSha256;
    Data::RuntimePackageCompilerSha256 companionKeyId;
    Data::RuntimePackageCompilerSha256 pythonExecutableSha256;
    Data::RuntimePackageCompilerSha256 baseRuntimeArchiveSha256;
    Data::RuntimePackageCompilerSha256 baseRuntimeTreeSha256;
    Data::RuntimePackageCompilerSha256 wheelLockSha256;
    Data::RuntimePackageCompilerSha256 wheelhouseContentSha256;
    Data::RuntimePackageCompilerSha256 wheelOverlayTreeSha256;
    Data::RuntimePackageCompilerSha256 packageSetSha256;
    Data::RuntimePackageCompilerSha256 sysPathSha256;
    Data::RuntimePackageCompilerSha256 installedTreeSha256;
    Data::RuntimePackageCompilerSha256 exactIdentitySha256;
    Data::RuntimePackageCompilerSha256 portableIdentitySha256;
    qsizetype installedTreeEntries = 0;
    quint64 installedFileBytes = 0;

    bool isValid() const;

    friend bool operator==(
        const CompilerPythonRuntimeIdentity &, const CompilerPythonRuntimeIdentity &) = default;
};

// Verified view of one API-070 signed companion and its already-installed
// relocatable Python runtime. Both roots are treated as immutable closed sets.
// Loading never executes a companion or runtime file. validateCurrent() must
// succeed immediately before and after every compiler process launch.
class CompilerPythonRuntimeProfile
{
public:
    static Utils::Result<CompilerPythonRuntimeProfile> load(
        const Utils::FilePath &installedCompanionRoot,
        const Utils::FilePath &installedRuntimeRoot,
        CompilerPythonRuntimeExpectation expectation);

    Utils::Result<> validateCurrent() const;

    Utils::FilePath companionRoot() const;
    Utils::FilePath runtimeRoot() const;
    Utils::FilePath pythonExecutable() const;
    Utils::FilePath runtimeIdentityFile() const;
    const CompilerPythonRuntimeExpectation &expectation() const;
    const CompilerPythonRuntimeIdentity &identity() const;

private:
    static Utils::Result<CompilerPythonRuntimeProfile> loadImpl(
        const Utils::FilePath &installedCompanionRoot,
        const Utils::FilePath &installedRuntimeRoot,
        CompilerPythonRuntimeExpectation expectation);

    Utils::FilePath m_companionRoot;
    Utils::FilePath m_runtimeRoot;
    Utils::FilePath m_pythonExecutable;
    Utils::FilePath m_runtimeIdentityFile;
    CompilerPythonRuntimeExpectation m_expectation;
    CompilerPythonRuntimeIdentity m_identity;
};

} // namespace EtherCAT::ProjectCompiler
