// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/runtimepackagecompilerprovider.h>

#include <utils/filepath.h>

#include <chrono>
#include <memory>

namespace EtherCAT::ProjectCompiler {

struct RuntimePackageCompilerProcessLimits
{
    qsizetype maximumStandardOutputBytes = 1024 * 1024;
    qsizetype maximumStandardErrorBytes = 1024 * 1024;
    qsizetype maximumArtifactBytes = 16 * 1024 * 1024;
    std::chrono::milliseconds commandTimeout = std::chrono::minutes(2);
    std::chrono::milliseconds queryTimeout = std::chrono::seconds(15);
    std::chrono::milliseconds cancellationGrace = std::chrono::milliseconds(500);

    bool isValid() const;
};

class ProvisionedRuntimePackageCompilerProvider final : public Core::RuntimePackageCompilerProvider
{
    Q_OBJECT

public:
    ProvisionedRuntimePackageCompilerProvider(
        const Utils::FilePath &provisioningFile,
        const Utils::FilePath &compilerRoot,
        RuntimePackageCompilerProcessLimits limits = {},
        QObject *parent = nullptr);
    ~ProvisionedRuntimePackageCompilerProvider() final;

    QString provisioningError() const;
    Utils::FilePath compilerRoot() const;
    Utils::FilePath operationRoot(const Data::RuntimePackageCompilerOperationId &operationId) const;

    Utils::Result<Core::RuntimePackageCompilerJob *> compile(
        const Data::RuntimePackageCompilerCompileRequest &request) final;
    Utils::Result<Core::RuntimePackageCompilerJob *> finalize(
        const Data::RuntimePackageCompilerFinalizeRequest &request) final;
    Utils::Result<Core::RuntimePackageCompilerJob *> query(
        const Data::RuntimePackageCompilerQueryRequest &request) final;
    Utils::Result<Core::RuntimePackageCompilerJob *> verify(
        const Data::RuntimePackageCompilerVerifyRequest &request) final;

    void shutdown();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace EtherCAT::ProjectCompiler
