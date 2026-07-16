// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Project::Internal {

class EtherCATProjectTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataAndService();
    void testFormatRoundTripAndCorruption();
    void testDocumentUndoRedoAndAtomicFailure();
    void testOfflineSlavePersistenceAndUndo();
    void testMigrationCreatesRecoveryBackup();
    void testProjectExplorerMultiProjectLifecycle();
};

} // namespace EtherCAT::Project::Internal
