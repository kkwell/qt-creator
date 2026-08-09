// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::AutomationGateway::Internal {

class EtherCATAutomationGatewayTests final : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultOffAndClosedToolCatalog();
    void testPluginDiscoveryAndSettingsPageContract();
    void testRuntimeValidationRollbackAndRestart();
    void testUnavailableRuntimeStaysDisabled();
    void testWorkbenchPublishesSingleAutomationService();
    void testSharedSnapshotUpdatesWithoutGatewayCache();
    void testOperationJournalAcrossTransports();
    void testSelectedTopologyEvidenceOrderingAndRedaction();
    void testSelectedTopologyEvidenceFailureClosure();
    void testSelectedTopologyJournalAndLegacyIsolation();
    void testMutationsAreRejectedWithoutProviderCalls();
    void testVendorDetailsAreNotProjected();
    void testSemanticRuntimeFailsClosedAndRedactsBindings();
    void testSemanticRuntimeOperationIntentAndJournal();
    void testMcpToolSchemaRoundTrip();
    void testListenerLifecycleAndAtomicRollback();
    void testMcpRestIntegrationAndOriginBoundary();
    void testExternalProcessProbe();
    void testArtifactValidationAndBuildSystemSync();
};

} // namespace EtherCAT::AutomationGateway::Internal
