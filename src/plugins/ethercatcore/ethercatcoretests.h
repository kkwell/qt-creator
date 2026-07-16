// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Core::Internal {

class EtherCATCoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataAndServices();
    void testNodeIdRoundTrip();
    void testProjectSnapshotValueSemantics();
    void testProcessDataConfigurationPreview();
    void testProcessDataConfigurationValidation();
    void testStartupAndDcConfigurationValidation();
    void testDeviceDescriptionAndImportJobContract();
    void testPropertyPageProviderContract();
    void testScanProviderContract();
    void testDiagnosticsProviderContract();
    void testSelectionServicePublishesStableIds();
    void testStateServiceAggregatesContributions();
    void testProviderRegistryTracksObjectPool();
    void testSettingsPageIsRegistered();
};

} // namespace EtherCAT::Core::Internal
