// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Workbench::Internal {

class EtherCATWorkbenchTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataModeActionsAndProvider();
    void testProjectOpenShowsMasterDetails();
    void testQuickControllerScopeResolution();
    void testModeCommandStripMirrorsRegisteredActions();
    void testNavigationCommandsUseActionManager();
    void testOfflineTopologyEditingWorkflow();
    void testOfflineSlaveRemovalConfirmationInvalidationLifecycle();
    void testConfiguredSlaveTreePhysicalOrder();
    void testDeviceAdapterSelectionBuildsModuleChannelTree();
    void testDeviceAdapterTreeFailsClosed();
    void testDeviceAdapterProviderRemovalInvalidatesTree();
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
    void testGeneralRenameRejectionFeedback();
    void testGeneralPropertyTreeAccessibility();
    void testTwinCatMasterEtherCATWorkflow();
    void testMasterTopologyDialogContextLifecycle();
    void testMasterTopologyDialogBounds();
    void testMasterTopologyCellAccessibility();
    void testEtherCATSyncManagerCellAccessibility();
    void testEtherCATRepositoryEmptyState();
    void testEditableConfiguredSlaveEtherCATWorkflow();
    void testStatusBarTracksStateService();
    void testStatusBarIgnoresControllerConnection();
    void testStatusBarTracksPreferredDiagnosticsMode();
    void testTreeModelLargeIncrementalUpdate();
    void testConfiguredSlaveStateIcon();
    void testBundledEsiOnlineTopologyPresentation();
    void testNavigationHeaderResizePersistence();
    void testProviderStateTreeAndNavigation();
    void testProjectScopedLocateNavigation();
    void testNavigationSelectionAndFiltering();
    void testNavigationSelectionClearsBeforeModelReset();
    void testNavigationVisibleIdentityFiltering();
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
    void testNavigationNativeFindIntegration();
    void testNavigationKeyboardFocus();
    void testNavigationKeyboardContextMenuTargetsCurrentNode();
    void testBuiltInDevicePages();
    void testConfiguredSlaveTreeAndPages();
    void testTwinCatProcessDataTree();
    void testProcessDataTableAccessibility();
    void testProcessDataRepositoryEmptyState();
    void testEditableProcessDataWorkflow();
    void testProcessDataEditRejectionFeedback();
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
    void testStartupEditRejectionFeedback();
    void testEditableStartupWorkflow();
    void testDcRepositoryModePreview();
    void testDcRepositoryModeEmptyState();
    void testDcEditRejectionFeedback();
    void testEditableDcWorkflow();
    void testDcDraftsSurviveNonConflictingRefresh();
    void testDynamicPropertyProviderRemoval();
    void testControllerCommunicationSelectionAndScope();
    void testControllerCommunicationPagePresentation();
    void testControllerCommunicationControlWorkflow();
    void testControllerPackageDeploymentWorkflow();
    void testControllerFreeRunCapabilityWarnings();
    void testControllerQuickStopToShutdown();
    void testControllerQuickStartupAndLivePresentation();
    void testControllerCommunicationAutoAcquire();
    void testControllerCommunicationDoesNotAutoDiscover();
    void testControllerCurrentBusApplyWorkflow();
    void testControllerCommunicationAutoAcquireAcrossProjects();
    void testControllerProjectRemovalPreservesAutonomousRuntime();
    void testControllerCommunicationLifecycleAndProviderRemoval();
    void testProductionMockUiBoundary();
    void testOptionalProviderAvailabilityPresentation();
    void testDynamicOptionalProviders();
};

} // namespace EtherCAT::Workbench::Internal
