// Copyright (C) 2026 Embed Labs

#pragma once

#include <QObject>

namespace EtherCAT::ProjectCompiler::Internal {

class EtherCATProjectCompilerTests final : public QObject
{
    Q_OBJECT

private slots:
    void testPluginMetadataAndDefaultAvailability();
    void testPythonRuntimeProfileVerifiesSignedInstalledTree();
    void testRuntimeBundleProfileVerifiesInstalledTree();
    void testProvisioningRejectsUnsafeExecutables();
    void testCompilerInputProvisioningVerifiesTargetSignature();
    void testCompileRecoveryRoundTrip();
    void testCompileRecoveryVersionOneCompatibility();
    void testCompileRecoveryRejectsMutations();
    void testProjectRequestBuilderProvisioningAndDeterminism();
    void testDeviceParametersFailClosedBeforeCompilation();
    void testProjectRequestBuilderFailsClosedOnUnprovenTopology();
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
    void testPreparationCoordinatorRestoresFromRecoverySidecar();
    void testPreparationCoordinatorRecoversBeforeProviderReservation();
    void testPreparationCoordinatorRejectsInvalidRecovery();
    void testPreparationCoordinatorRejectsCorruptProviderQuery();
    void testPreparationCoordinatorRejectsInvalidCompilerLedger();
    void testPreparationCoordinatorRejectsMalformedUnknownProvider();
    void testPreparationCoordinatorRejectsConcurrentInvalidation();
    void testPreparationCoordinatorRestartAndTerminalTombstones();
    void testPreparationJournalCasRequiresExactPredecessor();
    void testPreparationJournalPortableRecoveryRoundTrip();
    void testPreparationJournalPersistsImmutableRecovery();
    void testPreparationJournalBindsRecoveryFingerprint();
    void testPreparationJournalRejectsUnsafeStorage();
};

} // namespace EtherCAT::ProjectCompiler::Internal
