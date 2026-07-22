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
    void testOfflineSlaveRemovalConfirmationInvalidationLifecycle();
    void testConfiguredSlaveTreePhysicalOrder();
    void testTwinCatInsertDeviceWorkflow();
    void testEsiDeviceSelectionRevisionTogglePreservesSelection();
    void testEsiDeviceSelectionCellAccessibility();
    void testInsertDeviceDialogTargetLifecycle_data();
    void testInsertDeviceDialogTargetLifecycle();
    void testInsertDeviceDialogAsynchronousLifecycle();
    void testEsiDeviceDragDropWorkflow();
    void testEditableProjectGeneralWorkflow();
    void testEsiRepositoryEmptyGuidance();
    void testEsiRepositoryGeneralWorkflow();
    void testEsiDeviceGeneralWorkflow();
    void testEditableTargetGeneralWorkflow();
    void testEditableMasterGeneralWorkflow();
    void testEditableConfiguredSlaveGeneralWorkflow();
    void testGeneralPropertyTreeAccessibility();
    void testTwinCatMasterEtherCATWorkflow();
    void testMasterTopologyDialogContextLifecycle();
    void testMasterTopologyDialogBounds();
    void testMasterTopologyCellAccessibility();
    void testEtherCATSyncManagerCellAccessibility();
    void testEtherCATRepositoryEmptyState();
    void testEditableConfiguredSlaveEtherCATWorkflow();
    void testStatusBarTracksStateService();
    void testStatusBarTracksPreferredDiagnosticsMode();
    void testTreeModelLargeIncrementalUpdate();
    void testConfiguredSlaveStateIcon();
    void testProviderStateTreeAndNavigation();
    void testProjectScopedLocateNavigation();
    void testNavigationSelectionAndFiltering();
    void testNavigationExpansionStateLifecycle();
    void testNavigationActiveProjectLifecycle();
    void testInvalidProjectPresentationAndLifecycle();
    void testDetailsEmptyStateLifecycle();
    void testDetailsKeyboardFocusContinuity();
    void testProjectScopedDetailsRefreshPreservesGeneralDraft();
    void testEsiRepositoryRefreshPreservesGeneralDraft();
    void testEthercatAliasDraftSurvivesNonConflictingRefresh();
    void testNavigationSetActiveProjectCommand();
    void testNavigationFilterEmptyState();
    void testNavigationKeyboardFocus();
    void testNavigationKeyboardContextMenuTargetsCurrentNode();
    void testBuiltInDevicePages();
    void testConfiguredSlaveTreeAndPages();
    void testTwinCatProcessDataTree();
    void testProcessDataTableAccessibility();
    void testProcessDataRepositoryEmptyState();
    void testEditableProcessDataWorkflow();
    void testProcessDataInlineDraftSurvivesNonConflictingRefresh();
    void testCoeOnlineMockWorkflow();
    void testCoeMockEditRejectionFeedback();
    void testCoeSameContextViewStateContinuity();
    void testCoeMockValueSurvivesNonConflictingRefresh();
    void testCoeInlineDraftSurvivesNonConflictingRefresh();
    void testCoeDictionaryCellAccessibility();
    void testCoeRepositoryReadOnlyWorkflow();
    void testCoeAdvancedDialogRepositoryRefresh();
    void testCoeAddConfirmationRepositoryRefresh();
    void testStartupTableAccessibility();
    void testStartupRepositoryEmptyState();
    void testStartupDialogProjectRefresh();
    void testStartupInlineDraftSurvivesNonConflictingRefresh();
    void testEditableStartupWorkflow();
    void testDcRepositoryModePreview();
    void testDcRepositoryModeEmptyState();
    void testEditableDcWorkflow();
    void testDcDraftsSurviveNonConflictingRefresh();
    void testDynamicPropertyProviderRemoval();
    void testOptionalProviderAvailabilityPresentation();
    void testDynamicOptionalProviders();
};

} // namespace EtherCAT::Workbench::Internal
