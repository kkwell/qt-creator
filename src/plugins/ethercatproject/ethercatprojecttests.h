// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Project::Internal {

class EtherCATProjectTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataAndService();
    void testProjectNeedsNoTargetConfiguration();
    void testFormatRoundTripAndCorruption();
    void testDocumentUndoRedoAndAtomicFailure();
    void testStructuralNodeRenameAndPersistence();
    void testOfflineSlavePersistenceAndUndo();
    void testMasterConfigurationPersistenceAndUndo();
    void testOfflineConfigurationPersistenceAndUndo();
    void testAdapterSelectionPersistenceAndUndo();
    void testManualControlEnvelopePersistenceAndUndo();
    void testBindingArtifactInvalidationAndUndo();
    void testProjectDeviceBindingPersistenceAndValidation();
    void testVersionOneConfigurationMigration();
    void testVersionTwoMasterConfigurationMigration();
    void testVersionThreeAdapterMigration();
    void testVersionFourManualControlMigration();
    void testVersionFiveBindingArtifactMigration();
    void testOfflineConfigurationCorruption();
    void testAdapterSelectionCorruption();
    void testManualControlEnvelopeCorruption();
    void testMigrationCreatesRecoveryBackup();
    void testProjectExplorerMultiProjectLifecycle();
    void testDuplicateProjectIdCannotOwnStartupContext();
    void testDuplicateProjectIdOwnerRecovery();
};

} // namespace EtherCAT::Project::Internal
