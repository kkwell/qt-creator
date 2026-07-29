// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Scan::Internal {

class EtherCATScanTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataProvidersActionsAndPage();
    void testTopologyComparison();
    void testBlockingIdentityAndDuplicateDifferences();
    void testScanPreservesManualConfigurationByIdentity();
    void testMockProviderStateCancellationAndFailure();
    void testProjectCloseClearsOwnedScanLifecycle_data();
    void testProjectCloseClearsOwnedScanLifecycle();
    void testWorkflowAcceptUndoAndRedo();
};

} // namespace EtherCAT::Scan::Internal
