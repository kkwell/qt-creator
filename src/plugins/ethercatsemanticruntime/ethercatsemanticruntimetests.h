// Copyright (C) 2026 Embed Labs

#pragma once

#include <QObject>

namespace EtherCAT::SemanticRuntime::Internal {

class EtherCATSemanticRuntimeTests final : public QObject
{
    Q_OBJECT

private slots:
    void testCanonicalJsonRoundTrip();
    void testCanonicalJsonRejectsAmbiguity();
    void testCanonicalJsonTransferredManifests();
    void testEcfgTransferredConfigurations();
    void testEcfgRejectsDeepMutations();
    void testEcpkgContainerCanonicalMinimal();
    void testEcpkgContainerRejectsMetadataMutations();
    void testEcpkgContainerRejectsLayoutsAndLimits();
    void testEcpkgContainerTransferredPackages();
    void testEd25519Rfc8032();
    void testEd25519RejectsInvalidInputs();
    void testEd25519TransferredManifests();
    void testSignedEcpkgTransferredPackages();
    void testProductionTrustStore();
    void testProductionTrustStoreRejectsUnsafeInputs();
    void testSemanticBindingTransferredPackages();
    void testSemanticBindingRejectsMismatches();
    void testSemanticBindingV2TransferredPackage();
    void testSemanticBindingV2RejectsDeepMutations();
    void testSemanticBindingV2ParserGuards();
    void testSemanticActionDefinitionsProductionPackage();
    void testSemanticActionDefinitionsRejectsMismatches();
    void testVerifiedEcpkgStore();
    void testVerifiedEcpkgStoreRejectsTampering();
    void testRuntimePackageEvidenceTransferredPackages();
    void testRuntimePackageEvidenceRejectsMismatches();
    void testRuntimePackageEvidenceRepository();
    void testRuntimePackageEvidenceRepositoryRejectsUnsafeInputs();
    void testHardwareApi038SemanticPreflight();
    void testHardwareApi038SemanticAdmissionFailsClosed();
    void testHardwareApi051SemanticPreflight();
    void testHardwareApi051SemanticAcceptance();
    void testTrustedRuntimePackageActivation();
    void testTrustedRuntimePackageActivationFailsClosed();
    void testInstalledProductionTrustAnchor();
    void testReadOnlySemanticBindingFactory();
    void testReadOnlySemanticBindingFactoryRejectsMismatches();
    void testSemanticActionRuntimeFactory();
    void testSemanticActionRuntimeFactoryFailsClosed();
    void testSemanticOperationJournal();
    void testSemanticActionPlan();
    void testSemanticActionPlanFailsClosed();
    void testExecutorRejectsUnauthorizedManualActionBeforeApply();
    void testExecutorExecutesApi038Xb6Action();
    void testExecutorFailsClosedBeforeApply_data();
    void testExecutorFailsClosedBeforeApply();
    void testExecutorReconcilesUnknownOutput_data();
    void testExecutorReconcilesUnknownOutput();
    void testExecutorIgnoresMismatchedCorrelations();
    void testExecutorRejectsSessionChangeBeforeWrite();
    void testExecutorSerializesPerController();
    void testExecutorRejectsExpiredSameValueProof();
    void testExecutorBlocksOldUnknownAcrossScopes();
    void testExecutorFreezesUnknownWhenInfrastructureDisappears_data();
    void testExecutorFreezesUnknownWhenInfrastructureDisappears();
    void testExecutorPublishesVerifiedReadOnlyContext();
    void testRuntimeBootstrapIsSerializedAndIdempotent();
    void testRuntimeBootstrapRetriesRejectedRefreshOnce();
    void testRuntimeBootstrapRequiresEvidenceAndUniqueProvider();
    void testPublishesOneProductionService();
    void testProjectAndProviderLifecycle();
    void testStrictProviderCardinality();
    void testScopeSessionAndEpochInvalidation();
    void testCandidateAdaptersNeverWrite();
    void testMissingCatalogAndProofFailClosed();
};

} // namespace EtherCAT::SemanticRuntime::Internal
