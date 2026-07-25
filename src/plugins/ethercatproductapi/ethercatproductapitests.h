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
    void testConnectionProfileEndpointConfiguration_data();
    void testConnectionProfileEndpointConfiguration();
    void testConnectionProfileEndpointReconfigurationGuards();
    void testConnectionProfileEndpointPersistence();
    void testThreeChannelInitialSnapshot();
    void testLeaseOwnershipRequiresAcquireOrResume();
    void testProtocolMinorDowngrade();
    void testControlLifecycle();
    void testHardwareControlLifecycle();
    void testShutdownReleaseSafety_data();
    void testShutdownReleaseSafety();
    void testDisconnectWaitsForRelease();
    void testHeartbeatTimeoutDoesNotPreemptDisconnectRelease();
    void testDisconnectRejectedReleasePreservesSession();
    void testDisconnectReleaseWriteFailure();
    void testDisconnectReleaseTimeoutPreservesEvidence();
    void testRejectedControlRefreshAndReleaseGates();
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
