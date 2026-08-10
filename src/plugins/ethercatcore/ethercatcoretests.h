// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <QObject>

namespace EtherCAT::Core::Internal {

// Shared by plugin tests that inject an exact-project verifier. Production
// code never consumes this synthetic compiler chain.
ETHERCATCORE_EXPORT Data::RuntimePackageCompilerActivationProof
syntheticRuntimePackageCompilerActivationProof();

class EtherCATCoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataAndServices();
    void testAutomationServiceValueLookup();
    void testMockUiVisibility();
    void testNodeIdRoundTrip();
    void testProjectSnapshotValueSemantics();
    void testAxisParameterEvidenceContract();
    void testRuntimeResourceValueSemantics();
    void testRuntimeResourceSnapshotRequestContract();
    void testRuntimeSemanticMappingAttestationContract();
    void testRuntimeOutputTransactionContract();
    void testRuntimePackageActivationContract();
    void testRuntimePackageCompilerValueSemantics();
    void testRuntimePackageCompilerCodec();
    void testRuntimePackageCompilerProviderContract();
    void testRuntimePackageCompilerProjectRequestBuilderContract();
    void testRuntimePackageCompilerPreparationCoordinatorContract();
    void testRuntimePackageCompilerActivationProofRouting();
    void testSemanticRuntimeValueSemantics();
    void testSemanticLiveRefreshContract();
    void testSemanticRuntimeEpochValidation();
    void testSemanticRuntimeReadValidation();
    void testSemanticRuntimeOperationContract();
    void testSemanticRuntimeServiceFailsClosed();
    void testExactEngineeringRationalContract();
    void testExactEngineeringConversionContract();
    void testExactEngineeringConstraintContract();
    void testDeviceParameterConfigurationContract();
    void testConfiguredDeviceParameterQualification();
    void testManualControlEnvelopeContract();
    void testProcessDataConfigurationPreview();
    void testProcessDataConfigurationValidation();
    void testStartupAndDcConfigurationValidation();
    void testDeviceDescriptionAndImportJobContract();
    void testDeviceAdapterValueSemantics();
    void testDeviceAdapterProviderContract();
    void testPropertyPageProviderContract();
    void testWorkbenchDerivedNodeKinds();
    void testControllerConnectionProviderContract();
    void testScanProviderContract();
    void testDiagnosticsProviderContract();
    void testSelectionServicePublishesStableIds();
    void testScanProviderSelectionServiceContract();
    void testStateServiceAggregatesContributions();
    void testTopologyServiceKeepsRealAndMockEvidenceSeparate();
    void testProviderRegistryTracksObjectPool();
    void testSettingsPageIsRegistered();
};

} // namespace EtherCAT::Core::Internal
