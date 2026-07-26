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
    void testSemanticAuxiliaryRecords();
    void testSupportedRequestPolicy();
    void testPackageDeploymentRequestPolicy();
    void testConnectionProfileEndpointConfiguration_data();
    void testConnectionProfileEndpointConfiguration();
    void testConnectionProfileEndpointReconfigurationGuards();
    void testConnectionProfileEndpointPersistence();
    void testThreeChannelInitialSnapshot();
    void testLeaseOwnershipRequiresAcquireOrResume();
    void testProtocolMinorDowngrade();
    void testControlLifecycle();
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
};

} // namespace EtherCAT::ProductApi::Internal
