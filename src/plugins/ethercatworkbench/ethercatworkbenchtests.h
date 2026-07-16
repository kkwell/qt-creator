// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Workbench::Internal {

class EtherCATWorkbenchTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataModeActionsAndProvider();
    void testTreeModelLargeIncrementalUpdate();
    void testNavigationSelectionAndFiltering();
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
