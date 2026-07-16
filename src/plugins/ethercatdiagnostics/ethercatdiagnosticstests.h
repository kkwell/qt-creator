// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Diagnostics::Internal {

class EtherCATDiagnosticsTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataProvidersActionsAndPages();
    void testMockStreamModesAndFields();
    void testBoundedAggregationAndAlarmLifecycle();
    void testFailureConsumerAndProjectCleanup();
    void testLivePageUpdatesAndShutdown();
};

} // namespace EtherCAT::Diagnostics::Internal
