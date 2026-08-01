// Copyright (C) 2026 Embed Labs

#pragma once

#include <QObject>

namespace EtherCAT::ProjectCompiler::Internal {

class EtherCATProjectCompilerTests final : public QObject
{
    Q_OBJECT

private slots:
    void testPluginMetadataAndDefaultAvailability();
    void testProvisioningRejectsUnsafeExecutables();
    void testScheduledJobUsesPinnedProvisioning();
    void testStoreAnchorsAncestorsAndPreservesEvidence();
    void testCompileProcessAndImmutableEvidence();
    void testFailureAndStrictOutputHandling();
    void testCancellationQueriesBeforeCompletion();
    void testShutdownReapsCompilerBeforeUnlock();
    void testFinalizeQueryVerifyAndRestart();
    void testOperationAndConfigurationConflicts();
    void testActivationProofProvenanceAndRestart();
    void testActivationProofRejectsMutationsWithoutProcess();
    void testActivationProofRejectsUnsafeEvidence();
    void testPreparationCoordinatorSuccessAndCancellation();
    void testPreparationCoordinatorRejectsConcurrentInvalidation();
    void testPreparationCoordinatorRestartAndTerminalTombstones();
    void testPreparationJournalCasRequiresExactPredecessor();
    void testPreparationJournalRejectsUnsafeStorage();
};

} // namespace EtherCAT::ProjectCompiler::Internal
