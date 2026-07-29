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
    void testEcpkgContainerCanonicalMinimal();
    void testEcpkgContainerRejectsMetadataMutations();
    void testEcpkgContainerRejectsLayoutsAndLimits();
    void testEcpkgContainerTransferredPackages();
    void testEd25519Rfc8032();
    void testEd25519RejectsInvalidInputs();
    void testEd25519TransferredManifests();
    void testSignedEcpkgTransferredPackages();
    void testPublishesOneProductionService();
    void testProjectAndProviderLifecycle();
    void testStrictProviderCardinality();
    void testScopeSessionAndEpochInvalidation();
    void testCandidateAdaptersNeverWrite();
    void testMissingCatalogAndProofFailClosed();
};

} // namespace EtherCAT::SemanticRuntime::Internal
