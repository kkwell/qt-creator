// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/filepath.h>
#include <utils/result.h>

namespace EtherCAT::ProjectCompiler {

// Provisioning is an administrator-controlled, same-user trust boundary. The
// provider pins exact source bytes before accepting work, but it does not
// defend against a process with the IDE user's privileges mutating the private
// compiler store or influencing a provisioned executable's dynamic runtime.
class CompilerProvisioningProfile
{
public:
    static Utils::Result<CompilerProvisioningProfile> load(const Utils::FilePath &profileFile);

    Utils::Result<> validateCurrent() const;
    Utils::Result<> validatePinnedFiles() const;
    Utils::Result<> usePinnedFiles(
        const Utils::FilePath &executable, const Utils::FilePath &productionPublicKey);

    Utils::FilePath profileFile() const;
    Utils::FilePath executable() const;
    Utils::FilePath productionPublicKey() const;
    Data::RuntimePackageCompilerContractIdentity contractIdentity() const;
    Data::RuntimePackageCompilerSha256 profileSha256() const;
    Data::RuntimePackageCompilerSha256 executableSha256() const;
    Data::RuntimePackageCompilerSha256 productionPublicKeySha256() const;
    const QByteArray &exactExecutableBytes() const;
    const QByteArray &exactProductionPublicKeyBytes() const;

private:
    Utils::FilePath m_profileFile;
    Utils::FilePath m_executable;
    Utils::FilePath m_productionPublicKey;
    Data::RuntimePackageCompilerContractIdentity m_contractIdentity;
    Data::RuntimePackageCompilerSha256 m_profileSha256;
    Data::RuntimePackageCompilerSha256 m_executableSha256;
    Data::RuntimePackageCompilerSha256 m_productionPublicKeySha256;
    QByteArray m_exactProfileBytes;
    QByteArray m_exactExecutableBytes;
    QByteArray m_exactProductionPublicKeyBytes;
};

} // namespace EtherCAT::ProjectCompiler
