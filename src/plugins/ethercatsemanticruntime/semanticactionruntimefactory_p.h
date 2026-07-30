// Copyright (C) 2026 Embed Labs

#pragma once

#include "readonlysemanticbindingfactory_p.h"
#include "runtimepackageevidence_p.h"

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/semanticruntime.h>

#include <utils/result.h>

#include <QStringView>

namespace EtherCAT::SemanticRuntime::Internal {

struct SemanticActionRuntimeGates
{
    bool outputTransactionsSupported = false;
    bool ownsExclusiveControl = false;
    Data::ControllerServiceState serviceState = Data::ControllerServiceState::Unknown;
    bool dcRuntimeActive = false;
};

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

} // namespace EtherCAT::SemanticRuntime::Internal
