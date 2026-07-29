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
    void testExactIdentityAndEsiMatching();
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
