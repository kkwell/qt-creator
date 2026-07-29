// Copyright (C) 2026 Kvell

#pragma once

#include <QObject>

namespace EtherCAT::Devices::Internal {

class EtherCATDevicesTests final : public QObject
{
    Q_OBJECT

private slots:
    void testMetadataAndProvider();
    void testParserRejectsInvalidInput();
    void testParserKeepsMultipleRevisions();
    void testParserReadsOperationalData();
    void testBundledVendorEsiFiles();
    void testRepositoryImportFilterAndRebuild();
    void testLargeLibraryAndCancellation();
};

} // namespace EtherCAT::Devices::Internal
