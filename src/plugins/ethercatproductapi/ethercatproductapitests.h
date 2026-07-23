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
    void testReadOnlyRequestWhitelist();
    void testThreeChannelInitialSnapshot();
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
