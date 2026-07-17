// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Workbench::Internal {

class EtherCATWorkbenchTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataModeActionsAndProvider();
    void testModeCommandStripMirrorsRegisteredActions();
    void testNavigationCommandsUseActionManager();
    void testOfflineTopologyEditingWorkflow();
    void testTwinCatInsertDeviceWorkflow();
    void testEsiDeviceDragDropWorkflow();
    void testEditableProjectGeneralWorkflow();
    void testEsiRepositoryEmptyGuidance();
    void testEsiRepositoryGeneralWorkflow();
    void testEsiDeviceGeneralWorkflow();
    void testEditableTargetGeneralWorkflow();
    void testEditableMasterGeneralWorkflow();
    void testEditableConfiguredSlaveGeneralWorkflow();
    void testTwinCatMasterEtherCATWorkflow();
    void testEditableConfiguredSlaveEtherCATWorkflow();
    void testStatusBarTracksStateService();
    void testTreeModelLargeIncrementalUpdate();
    void testConfiguredSlaveStateIcon();
    void testProviderStateTreeAndNavigation();
    void testNavigationSelectionAndFiltering();
    void testNavigationFilterEmptyState();
    void testNavigationKeyboardFocus();
    void testBuiltInDevicePages();
    void testConfiguredSlaveTreeAndPages();
    void testTwinCatProcessDataTree();
    void testEditableProcessDataWorkflow();
    void testCoeOnlineMockWorkflow();
    void testEditableStartupWorkflow();
    void testEditableDcWorkflow();
    void testDynamicPropertyProviderRemoval();
    void testDynamicOptionalProviders();
};

} // namespace EtherCAT::Workbench::Internal
