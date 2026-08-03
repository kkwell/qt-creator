// Copyright (C) 2026 Embed Labs

#pragma once

#include "compilerinputprovisioningprofile.h"

#include <ethercatcore/runtimepackagecompilerprojectrequestbuilder.h>

#include <utils/filepath.h>

#include <functional>
#include <memory>

namespace EtherCAT::Core {
class ProviderRegistry;
}

namespace EtherCAT::ProjectCompiler {

class ProvisionedRuntimePackageCompilerProjectRequestBuilder final
    : public Core::RuntimePackageCompilerProjectRequestBuilder
{
    Q_OBJECT

public:
    using CurrentTimeNs = std::function<quint64()>;

    ProvisionedRuntimePackageCompilerProjectRequestBuilder(
        Core::ProviderRegistry *providerRegistry,
        const Utils::FilePath &inputProvisioningFile,
        QObject *parent = nullptr,
        CurrentTimeNs currentTimeNs = {});
    ~ProvisionedRuntimePackageCompilerProjectRequestBuilder() final;

    QString provisioningError() const;
    QString unavailableReason() const final;

    Utils::Result<Core::RuntimePackageCompilerPreparationStartRequest> build(
        const Core::RuntimePackageCompilerProjectRequestSeed &seed) final;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace EtherCAT::ProjectCompiler
