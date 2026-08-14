// Copyright (C) 2026 Embed Labs

#pragma once

#include "readonlysemanticbindingfactory_p.h"
#include "runtimepackageevidence_p.h"

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/deviceadapterauthorizationprovenance.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/semanticruntime.h>

#include <ethercatcore/providers.h>

#include <utils/result.h>

#include <QPointer>
#include <QStringView>

#include <functional>

namespace EtherCAT::SemanticRuntime::Internal {

struct SemanticActionRuntimeGates
{
    bool outputTransactionsSupported = false;
    bool ownsExclusiveControl = false;
    Data::ControllerServiceState serviceState = Data::ControllerServiceState::Unknown;
    bool dcRuntimeActive = false;
};

using AvailableDeviceAdapterProviderList = std::function<QList<Core::DeviceAdapterProvider *>()>;

// Every production admission is scoped to an owning QObject and a monotonic
// signal generation. This makes even a same-valued, synchronous provider
// signal observable while external provider/source getters are running.
struct AvailableDeviceAdapterProviders
{
    AvailableDeviceAdapterProviderList providers;
    QPointer<QObject> owner;
    std::function<quint64()> signalGeneration;

    explicit operator bool() const { return providers && owner && signalGeneration; }
};

struct SemanticActionAdapterAuthorizationCatalogMember
{
    QPointer<Core::DeviceAdapterProvider> provider;
    QList<Data::DeviceAdapterManifest> manifests;
    bool authorizationSource = false;
    std::optional<Data::DeviceAdapterAuthorizationProvenanceSnapshot> snapshot;

    friend bool operator==(
        const SemanticActionAdapterAuthorizationCatalogMember &,
        const SemanticActionAdapterAuthorizationCatalogMember &)
        = default;
};

// Private, process-local admission token. It deliberately retains the provider
// QObject identity as well as the complete manifest and authorization snapshot:
// an approval made under one authorization generation must not survive an
// allow-to-allow authorization rotation.
struct SemanticActionAdapterAuthorizationAdmission
{
    QPointer<QObject> catalogOwner;
    quint64 catalogSignalGeneration = 0;
    QList<SemanticActionAdapterAuthorizationCatalogMember> catalog;
    QPointer<Core::DeviceAdapterProvider> provider;
    Data::DeviceAdapterManifest manifest;
    Data::DeviceAdapterAuthorizationProvenanceSnapshot snapshot;

    friend bool operator==(
        const SemanticActionAdapterAuthorizationAdmission &,
        const SemanticActionAdapterAuthorizationAdmission &)
        = default;
};

struct SemanticActionAdapterResolution
{
    Data::DeviceAdapterManifest manifest;
    std::optional<SemanticActionAdapterAuthorizationAdmission> authorization;
};

// Resolves one exact project adapter. Production V3 performs a fresh
// before/validate/after authorization admission and returns the token that an
// execution must bind and re-check before every provider request. V4 remains
// unsupported until parameter projection is bound by signed compiler evidence.
Utils::Result<SemanticActionAdapterResolution> resolveSemanticActionAdapter(
    const Data::OfflineSlaveConfiguration &slave,
    const AvailableDeviceAdapterProviders &availableProviders);

// Proves that an upper-layer process profile names the exact PDO and DC
// profiles signed for one controller topology instance. An action that
// explicitly requires DC additionally requires a nonempty signed DC profile.
bool processDataProfileMatchesSignedTopology(
    const Data::ProcessDataProfile &profile,
    const SemanticBindingTopologyInstance &topology,
    bool actionRequiresDc);

// Projects the signed public action surface without exposing the private step,
// assignment, or consistency-group plan. The supplied bindings must be the
// complete candidate set produced for the same verified package and live
// controller context. Any mismatch rejects the complete projection.
Utils::Result<QList<Data::SemanticActionRuntimeState>> buildSemanticActionRuntimeStates(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const ReadOnlySemanticBindingCandidates &candidates,
    const QList<Data::DeviceAdapterManifest> &adapterManifests,
    const SemanticActionRuntimeGates &gates);

Utils::Result<QList<Data::SemanticActionRuntimeState>> buildSemanticActionRuntimeStates(
    QStringView controllerId,
    const Data::ProjectSnapshot &project,
    const VerifiedRuntimePackageEvidence &evidence,
    const ReadOnlySemanticBindingCandidates &candidates,
    const AvailableDeviceAdapterProviders &availableProviders,
    const SemanticActionRuntimeGates &gates);

} // namespace EtherCAT::SemanticRuntime::Internal
