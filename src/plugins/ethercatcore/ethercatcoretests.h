// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Core::Internal {

class EtherCATCoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataAndServices();
    void testAutomationServiceValueLookup();
    void testMockUiVisibility();
    void testNodeIdRoundTrip();
    void testProjectSnapshotValueSemantics();
    void testRuntimeResourceValueSemantics();
    void testProcessDataConfigurationPreview();
    void testProcessDataConfigurationValidation();
    void testStartupAndDcConfigurationValidation();
    void testDeviceDescriptionAndImportJobContract();
    void testDeviceAdapterValueSemantics();
    void testDeviceAdapterProviderContract();
    void testPropertyPageProviderContract();
    void testWorkbenchDerivedNodeKinds();
    void testControllerConnectionProviderContract();
    void testScanProviderContract();
    void testDiagnosticsProviderContract();
    void testSelectionServicePublishesStableIds();
    void testStateServiceAggregatesContributions();
    void testProviderRegistryTracksObjectPool();
    void testSettingsPageIsRegistered();
};

} // namespace EtherCAT::Core::Internal
