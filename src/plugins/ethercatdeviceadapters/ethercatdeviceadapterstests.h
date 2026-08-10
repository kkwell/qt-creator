// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::DeviceAdapters::Internal {

class EtherCATDeviceAdaptersTests final : public QObject
{
    Q_OBJECT

private slots:
    void testBundledResourcesAndManifests();
    void testInvalidPackagesAreRejected();
    void testBundledV2ExactContracts();
    void testBundledV3Api038Contracts();
    void testInstalledProductionAuthorizations();
    void testSignedAdapterAuthorizationProjection();
    void testAuthorizationStartupMessage();
    void testV2StrictParserAndCanonicalDigest();
    void testV3SignedActionContract();
    void testV3RejectsUnsafeContracts();
    void testV4ParameterDefinitionContract();
    void testSignedAdapterAuthorizationV2ParameterClosure();
    void testV1RemainsFailClosed();
    void testV1AndV2ExactSelection();
    void testExactIdentityAndEsiMatching();
    void testExactPackageSelection();
    void testCandidateHardwareGate();
    void testSv630nProcessImageBinding();
    void testSv630nRejectsInvalidProcessImages();
    void testSv630nManualActionsRemainDisabled();
    void testXb6RequiresDetectedModules();
    void testXb6ExpandsDo16Modules();
    void testXb6RejectsInvalidModuleLayouts();
    void testProviderRegistryOrdering();
};

} // namespace EtherCAT::DeviceAdapters::Internal
