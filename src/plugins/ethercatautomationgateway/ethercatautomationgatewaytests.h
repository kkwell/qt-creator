// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::AutomationGateway::Internal {

class EtherCATAutomationGatewayTests final : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultOffAndClosedToolCatalog();
    void testWorkbenchPublishesSingleAutomationService();
    void testSharedSnapshotUpdatesWithoutGatewayCache();
    void testOperationJournalAcrossTransports();
    void testMutationsAreRejectedWithoutProviderCalls();
    void testVendorDetailsAreNotProjected();
    void testListenerLifecycleAndAtomicRollback();
    void testMcpRestIntegrationAndOriginBoundary();
    void testArtifactValidationAndBuildSystemSync();
};

} // namespace EtherCAT::AutomationGateway::Internal
