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
    void testFaultResetLifecycle();
    void testPackageDeploymentLifecycle();
    void testPackageDeploymentGuardsAndIdempotency();
    void testPackageDeploymentCancellationAndAbort();
    void testHardwareControlLifecycle();
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
};

} // namespace EtherCAT::ProductApi::Internal
