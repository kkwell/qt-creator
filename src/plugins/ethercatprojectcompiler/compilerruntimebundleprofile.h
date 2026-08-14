// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/filepath.h>
#include <utils/result.h>

#include <optional>

namespace EtherCAT::ProjectCompiler {

struct CompilerPythonRuntimeIdentity;

struct CompilerRuntimeBundleExpectation
{
    QString bundleVersion;
    QByteArray signingPublicKey;
    Data::RuntimePackageCompilerSha256 manifestSha256;

    bool isValid() const;

    friend bool operator==(
        const CompilerRuntimeBundleExpectation &, const CompilerRuntimeBundleExpectation &)
        = default;
};

struct CompilerRuntimeBundleIdentity
{
    struct CompanionIdentity
    {
        QString bundleVersion;
        Data::RuntimePackageCompilerSha256 archiveSha256;
        Data::RuntimePackageCompilerSha256 manifestSha256;
        Data::RuntimePackageCompilerSha256 keyId;
        Data::RuntimePackageCompilerSha256 portableIdentitySha256;
        Data::RuntimePackageCompilerSha256 pythonExecutableSha256;
        Data::RuntimePackageCompilerSha256 installedTreeSha256;

        bool isValid() const;

        friend bool operator==(const CompanionIdentity &, const CompanionIdentity &) = default;
    };

    QString bundleId;
    QString bundleVersion;
    QString compilerContractName;
    quint32 compilerContractVersion = 0;
    QString compilerImplementation;
    QList<quint32> ecpkgVersions;
    Data::RuntimePackageCompilerSha256 manifestSha256;
    Data::RuntimePackageCompilerSha256 signingKeyId;
    Data::RuntimePackageCompilerSha256 requirementsSha256;
    qsizetype fileCount = 0;
    quint64 totalPayloadBytes = 0;
    std::optional<CompanionIdentity> companion;

    bool isValid() const;

    friend bool operator==(
        const CompilerRuntimeBundleIdentity &, const CompilerRuntimeBundleIdentity &)
        = default;
};

Utils::Result<> validateCompilerRuntimeCompanionBinding(
    const CompilerRuntimeBundleIdentity &compiler,
    const CompilerPythonRuntimeIdentity &python);

// Verified view of one already-installed API-068 compiler runtime. The caller
// supplies the raw32 release key and exact manifest identity from outside the
// installed tree. No bundle entrypoint is executed while this profile loads.
// validateCurrent() must succeed immediately before every process launch.
class CompilerRuntimeBundleProfile
{
public:
    static Utils::Result<CompilerRuntimeBundleProfile> load(
        const Utils::FilePath &installedRoot,
        CompilerRuntimeBundleExpectation expectation);

    Utils::Result<> validateCurrent() const;

    Utils::FilePath bundleRoot() const;
    Utils::FilePath compilerExecutable() const;
    Utils::FilePath provisionExecutable() const;
    Utils::FilePath verifyExecutable() const;
    Utils::FilePath selfTestExecutable() const;
    Utils::FilePath runtimeRequirements() const;
    Utils::FilePath compilerImportRoot() const;
    const CompilerRuntimeBundleExpectation &expectation() const;
    const CompilerRuntimeBundleIdentity &identity() const;

private:
    static Utils::Result<CompilerRuntimeBundleProfile> loadImpl(
        const Utils::FilePath &installedRoot,
        CompilerRuntimeBundleExpectation expectation);

    Utils::FilePath m_bundleRoot;
    Utils::FilePath m_compilerExecutable;
    Utils::FilePath m_provisionExecutable;
    Utils::FilePath m_verifyExecutable;
    Utils::FilePath m_selfTestExecutable;
    Utils::FilePath m_runtimeRequirements;
    Utils::FilePath m_compilerImportRoot;
    CompilerRuntimeBundleExpectation m_expectation;
    CompilerRuntimeBundleIdentity m_identity;
};

} // namespace EtherCAT::ProjectCompiler
