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
    void testOfflineConfigurationPersistenceAndUndo();
    void testVersionOneConfigurationMigration();
    void testOfflineConfigurationCorruption();
    void testMigrationCreatesRecoveryBackup();
    void testProjectExplorerMultiProjectLifecycle();
    void testDuplicateProjectIdCannotOwnStartupContext();
    void testDuplicateProjectIdOwnerRecovery();
};

} // namespace EtherCAT::Project::Internal
