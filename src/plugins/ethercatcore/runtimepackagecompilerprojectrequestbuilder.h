// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"
#include "providers.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

namespace EtherCAT::Core {

class ProviderRegistry;
struct RuntimePackageCompilerPreparationStartRequest;

// Caller-owned identities and timestamps for one IDE-local package preparation.
// The builder owns every project, topology, ESI, adapter and provisioned lower
// input. Supplying this seed never authorizes deployment or controller access.
struct ETHERCATCORE_EXPORT RuntimePackageCompilerProjectRequestSeed
{
    Data::ControllerConnectionScope scope;
    Data::RuntimePackageCompilerOperationId compileOperationId;
    Data::RuntimePackageCompilerOperationId verifyOperationId;
    Data::RuntimePackageActivationOperationId activationOperationId;
    QString intentId;
    quint64 configurationId = 0;
    quint64 buildTimestampNs = 0;
    quint64 compileTimeNs = 0;
    bool rollbackOnActivationFailure = true;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerProjectRequestSeed &,
        const RuntimePackageCompilerProjectRequestSeed &)
        = default;
};

// Product entry contract for ProjectSnapshot -> API-042 request assembly. A
// concrete provider must fail closed if any captured or provisioned input is
// absent, stale, ambiguous, or cannot be represented losslessly. A successful
// result is still only a preparation request: it is not deployed or activated.
class ETHERCATCORE_EXPORT RuntimePackageCompilerProjectRequestBuilder : public Provider
{
    Q_OBJECT

public:
    RuntimePackageCompilerProjectRequestBuilder(
        Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    // An unavailable builder may expose a concise, user-facing reason. This
    // must not contain untrusted backend output or sensitive local paths.
    virtual QString unavailableReason() const;

    virtual Utils::Result<RuntimePackageCompilerPreparationStartRequest> build(
        const RuntimePackageCompilerProjectRequestSeed &seed)
        = 0;
};

// Selects exactly one available builder on the registry thread. This keeps UI
// and automation callers from choosing different project compilation truth.
ETHERCATCORE_EXPORT Utils::Result<RuntimePackageCompilerProjectRequestBuilder *>
uniqueRuntimePackageCompilerProjectRequestBuilder(ProviderRegistry *providerRegistry);

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::RuntimePackageCompilerProjectRequestSeed)
