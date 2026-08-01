// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::ProductApi::Internal {

class EtherCATProductApiTests final : public QObject
{
    Q_OBJECT

private slots:
    void testCrcAndGoldenFrame();
    void testFrameStreamFragmentationAndCoalescing();
    void testFrameStreamRejectsMalformedInput_data();
    void testFrameStreamRejectsMalformedInput();
    void testSemanticControllerState();
    void testSemanticPerformanceSnapshot();
    void testSemanticAuxiliaryRecords();
    void testRuntimeResourceGoldenFrames();
    void testRuntimeResourceCodecRejectsMalformed_data();
    void testRuntimeResourceCodecRejectsMalformed();
    void testSemanticBindingAttestationGoldenFrames();
    void testSemanticBindingAttestationFormatV2();
    void testSemanticBindingAttestationRejectsMalformed_data();
    void testSemanticBindingAttestationRejectsMalformed();
    void testOutputTransactionGoldenFrames();
    void testOutputTransactionCodecRejectsMalformed_data();
    void testOutputTransactionCodecRejectsMalformed();
    void testSupportedRequestPolicy();
    void testPackageDeploymentRequestPolicy();
    void testConnectionProfileEndpointConfiguration_data();
    void testConnectionProfileEndpointConfiguration();
    void testConnectionProfileEndpointReconfigurationGuards();
    void testConnectionProfileEndpointPersistence();
    void testThreeChannelInitialSnapshot();
    void testLiveStatePolling();
    void testLeaseOwnershipRequiresAcquireOrResume();
    void testProtocolMinorDowngrade();
    void testControlLifecycle();
    void testTopologyProvenanceLifecycle();
    void testFaultResetLifecycle();
    void testPackageDeploymentLifecycle();
    void testPackageDeploymentMaximumAudit();
    void testPackageDeploymentGuardsAndIdempotency();
    void testPackageDeploymentCancellationAndAbort();
    void testApi038HardwareFixturePreflight();
    void testHardwareControlLifecycle();
    void testHardwareApi038ProviderAcceptance();
    void testShutdownReleasePreservesAutonomousRuntime_data();
    void testShutdownReleasePreservesAutonomousRuntime();
    void testDisconnectPreservesAutonomousRuntime();
    void testLeaseExpiryPreservesAutonomousRuntime();
    void testDisconnectWaitsForRelease();
    void testHeartbeatTimeoutDoesNotPreemptDisconnectRelease();
    void testLongControlKeepsHeartbeatResponseWindow_data();
    void testLongControlKeepsHeartbeatResponseWindow();
    void testDisconnectRejectedReleasePreservesSession();
    void testDisconnectReleaseWriteFailure();
    void testDisconnectReleaseTimeoutPreservesEvidence();
    void testRejectedControlRefreshStillAllowsRelease();
    void testControllerErrorAttribution_data();
    void testControllerErrorAttribution();
    void testMalformedControllerStatus_data();
    void testMalformedControllerStatus();
    void testSessionCapacityRetry();
    void testMalformedSessionCapacity();
    void testChannelConnectionFailureDiagnostics_data();
    void testChannelConnectionFailureDiagnostics();
    void testSessionTimeoutAndShutdown();
    void testInFlightShutdown();
    void testSessionReconnectAndGeneration();
    void testInvalidAlarmCheckpointRefresh_data();
    void testInvalidAlarmCheckpointRefresh();
    void testRuntimeResourceRefreshCapabilityGuards_data();
    void testRuntimeResourceRefreshCapabilityGuards();
    void testRuntimeResourceLoopbackLifecycle();
    void testRuntimeResourceFailureAndInvalidation();
    void testRuntimeResourceProtocolFailures_data();
    void testRuntimeResourceProtocolFailures();
    void testRuntimeResourceSignalDisconnectReentrancy();
    void testRuntimeResourceReconnectSignalDisconnectReentrancy();
    void testRuntimeResourceBounds();
    void testRuntimeResourceControlInvalidation();
    void testRuntimeResourceIncompleteSnapshot();
    void testTargetedRuntimeResourceSnapshotLifecycle();
    void testTargetedRuntimeResourceSnapshotLocalGuards();
    void testTargetedRuntimeResourceSnapshotFailures();
    void testTargetedRuntimeResourceProtocolFailures_data();
    void testTargetedRuntimeResourceProtocolFailures();
    void testTargetedRuntimeResourceDisconnectAndControlInvalidation();
    void testTargetedRuntimeResourceIgnoredRequestBound();
    void testSemanticAttestationCapabilityGuards_data();
    void testSemanticAttestationCapabilityGuards();
    void testSemanticAttestationLoopbackLifecycle();
    void testSemanticAttestationFailures();
    void testSemanticAttestationProtocolFailures_data();
    void testSemanticAttestationProtocolFailures();
    void testSemanticAttestationInvalidationAndStaleResponse();
    void testOutputTransactionCapabilityGuards_data();
    void testOutputTransactionCapabilityGuards();
    void testOutputTransactionLoopbackLifecycle();
    void testOutputTransactionFailuresAndGuards();
    void testOutputTransactionCompleteGroupGuards();
    void testOutputTransactionTimeoutReconciliation();
    void testOutputTransactionControlledStopPreemptsRead();
    void testOutputTransactionReconciliationBoundaries();
    void testOutputTransactionUnknownSurvivesReconnect();
    void testOutputTransactionGenerationRegression();
    void testOutputTransactionReconnectInvalidation();
};

} // namespace EtherCAT::ProductApi::Internal
