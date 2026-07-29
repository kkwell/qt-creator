// Copyright (C) 2026 Kvell

#include "ethercatdevicestests.h"

#include "devicerepository.h"
#include "esiparser.h"
#include "ethercatdevicesconstants.h"

#include <coreplugin/icore.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <utils/filepath.h>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace EtherCAT::Devices::Internal {

static QByteArray esiDocument(const QByteArray &deviceElements, const QByteArray &rootAttribute = {})
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<EtherCATInfo "
           + rootAttribute
           + ">"
             "<Vendor><Id>#x00000002</Id><Name LcId=\"1033\">Vendor</Name></Vendor>"
             "<Descriptions>"
             "<Groups><Group><Type>IO</Type><Name LcId=\"1033\">I/O</Name></Group></Groups>"
             "<Devices>"
           + deviceElements + "</Devices></Descriptions></EtherCATInfo>";
}

static QByteArray simpleDevice(quint32 revision, int ordinal = 0)
{
    return QByteArray("<Device><Type ProductCode=\"#x00001234\" RevisionNo=\"#x")
           + QByteArray::number(revision, 16).rightJustified(8, '0')
           + "\">EL" + QByteArray::number(ordinal)
           + "</Type><Name LcId=\"1033\">Digital Terminal " + QByteArray::number(ordinal)
           + "</Name><GroupType>IO</GroupType></Device>";
}

static QByteArray operationalDevice()
{
    return R"(<Device>
<Type ProductCode="#x00005678" RevisionNo="#x00000011">AX5000</Type>
<Name LcId="1033">Servo Drive</Name><GroupType>IO</GroupType>
<Sm StartAddress="#x1000" DefaultSize="128" ControlByte="#x26" Enable="1">Outputs</Sm>
<Sm StartAddress="#x1100" DefaultSize="128" ControlByte="#x22" Enable="1">Inputs</Sm>
<RxPdo Sm="0" Fixed="1" Mandatory="1"><Index>#x1600</Index><Name>Outputs</Name>
 <Entry><Index>#x6040</Index><SubIndex>0</SubIndex><BitLen>16</BitLen><Name>Controlword</Name><DataType>UINT</DataType></Entry>
</RxPdo>
<TxPdo Sm="1"><Index>#x1a00</Index><Name>Inputs</Name>
 <Entry><Index>#x6041</Index><SubIndex>0</SubIndex><BitLen>16</BitLen><Name>Statusword</Name><DataType>UINT</DataType></Entry>
 <Entry><Index>#x6064</Index><SubIndex>0</SubIndex><BitLen>32</BitLen><Name>Position</Name><DataType>DINT</DataType></Entry>
</TxPdo>
<Mailbox><CoE SdoInfo="true" PdoAssign="1" PdoConfig="true" CompleteAccess="1">
 <InitCmds><InitCmd><Transition>PS</Transition><Index>#x6060</Index><SubIndex>0</SubIndex><Data>08</Data><Comment>Mode</Comment></InitCmd></InitCmds>
</CoE></Mailbox>
<Dc><OpMode><Name>DC Sync0</Name><AssignActivate>#x0300</AssignActivate>
 <CycleTimeSync0>125000</CycleTimeSync0><ShiftTimeSync0>1000</ShiftTimeSync0>
</OpMode></Dc>
<Profile><Dictionary><DataTypes>
 <DataType><Name>DT1C32</Name><SubItem><SubIdx>4</SubIdx>
  <Name>Sync modes supported</Name><Type>UINT</Type></SubItem></DataType>
 <DataType><Name>DT1C33</Name><SubItem><SubIdx>4</SubIdx>
  <Name>Sync modes supported</Name><Type>UINT</Type></SubItem></DataType>
</DataTypes><Objects>
 <Object><Index>#x1c32</Index><Type>DT1C32</Type><Info>
  <SubItem><Name>Sync modes supported</Name>
   <Info><DefaultData>0004</DefaultData></Info></SubItem>
 </Info></Object>
 <Object><Index>#x1c33</Index><Type>DT1C33</Type><Info>
  <SubItem><Name>Sync modes supported</Name>
   <Info><DefaultValue>4</DefaultValue></Info></SubItem>
 </Info></Object>
</Objects></Dictionary></Profile>
</Device>)";
}

static Utils::FilePath temporaryPath(
    const QTemporaryDir &directory, const QString &relativePath)
{
    return Utils::FilePath::fromString(directory.path()).canonicalPath().pathAppended(relativePath);
}

static Data::DeviceImportResult waitForJob(Core::DeviceImportJob *job)
{
    QSignalSpy finished(job, &Core::DeviceImportJob::finished);
    if (job->state() != Core::DeviceImportState::Finished)
        finished.wait(15000);
    return job->result();
}

void EtherCATDevicesTests::testMetadataAndProvider()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        Constants::PLUGIN_ID);
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATDevices"));
    QVERIFY(!spec->hasError());

    const QList<ExtensionSystem::PluginDependency> dependencies = spec->dependencies();
    const auto hasRequiredDependency = [&dependencies](const QString &id) {
        return std::any_of(
            dependencies.cbegin(),
            dependencies.cend(),
            [&id](const ExtensionSystem::PluginDependency &dependency) {
                return dependency.id == id
                       && dependency.type == ExtensionSystem::PluginDependency::Required;
            });
    };
    QVERIFY(hasRequiredDependency("core"));
    QVERIFY(hasRequiredDependency("ethercatcore"));

    auto *repository = ExtensionSystem::PluginManager::getObject<Core::DeviceRepositoryProvider>();
    QVERIFY(repository);
    QCOMPARE(repository->id(), Utils::Id(Constants::REPOSITORY_PROVIDER_ID));
    QVERIFY(repository->isAvailable());
    const auto containsIdentity = [repository](const Data::DeviceIdentity &identity) {
        const QList<Data::DeviceSummary> devices = repository->devices();
        return std::any_of(
            devices.cbegin(),
            devices.cend(),
            [&identity](const Data::DeviceSummary &device) {
                return device.identity == identity;
            });
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        containsIdentity({0x00884443, 0x000000b6, 0x00000001}), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        containsIdentity({0x00100000, 0x000c0112, 0x00010000}), 15000);
}

void EtherCATDevicesTests::testParserRejectsInvalidInput()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(!parseEsiFile("<EtherCATInfo>", "broken.xml", now));
    QVERIFY(!parseEsiFile("<EtherCATInfo/><Other/>", "two-roots.xml", now));
    QVERIFY(!parseEsiFile("<Other/>", "wrong-root.xml", now));
    QVERIFY(!parseEsiFile(
        "<EtherCATInfo><Descriptions><Devices/></Descriptions></EtherCATInfo>",
        "missing-vendor.xml",
        now));

    const QByteArray missingRevision
        = R"(<Device><Type ProductCode="#x1">Incomplete</Type><Name>Bad</Name></Device>)";
    const Utils::Result<QList<Data::DeviceDescription>> result = parseEsiFile(
        esiDocument(missingRevision), "missing-revision.xml", now);
    QVERIFY(!result);
    QVERIFY(result.error().contains("RevisionNo"));
}

void EtherCATDevicesTests::testParserKeepsMultipleRevisions()
{
    const Utils::Result<QList<Data::DeviceDescription>> parsed = parseEsiFile(
        esiDocument(simpleDevice(1, 1) + simpleDevice(2, 2)),
        "multiple-revisions.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(parsed);
    QCOMPARE(parsed->size(), 2);
    QCOMPARE(parsed->at(0).summary.identity.vendorId, quint32(2));
    QCOMPARE(parsed->at(0).summary.identity.productCode, quint32(0x1234));
    QCOMPARE(parsed->at(0).summary.identity.revisionNumber, quint32(1));
    QCOMPARE(parsed->at(1).summary.identity.revisionNumber, quint32(2));
    QVERIFY(parsed->at(0).summary.id != parsed->at(1).summary.id);
    QCOMPARE(parsed->at(0).summary.group, QString("I/O"));

    const Utils::Result<QList<Data::DeviceDescription>> repeated = parseEsiFile(
        esiDocument(simpleDevice(1, 1)), "repeat.xml", QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(repeated);
    QCOMPARE(repeated->first().summary.id, parsed->first().summary.id);
}

void EtherCATDevicesTests::testParserReadsOperationalData()
{
    const QByteArray xml = esiDocument(
        operationalDevice(), "xmlns=\"http://www.ethercat.org/2011/11/EtherCATInfo\"");
    const Utils::Result<QList<Data::DeviceDescription>> parsed = parseEsiFile(
        xml, "servo.xml", QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(parsed);
    QCOMPARE(parsed->size(), 1);
    const Data::DeviceDescription &device = parsed->first();
    QCOMPARE(device.syncManagers.size(), 2);
    QCOMPARE(device.syncManagers.at(0).direction, Data::SyncManagerDirection::MasterToSlave);
    QCOMPARE(device.syncManagers.at(1).direction, Data::SyncManagerDirection::SlaveToMaster);
    QCOMPARE(device.rxPdos.size(), 1);
    QCOMPARE(device.txPdos.size(), 1);
    QCOMPARE(device.rxPdos.first().entries.first().dataType, Data::EtherCATDataType::UnsignedInteger16);
    QCOMPARE(device.txPdos.first().entries.at(1).dataType, Data::EtherCATDataType::Integer32);
    QVERIFY(device.coe.supported);
    QVERIFY(device.coe.sdoInfo);
    QVERIFY(device.coe.pdoAssign);
    QVERIFY(device.coe.pdoConfiguration);
    QVERIFY(device.coe.completeAccess);
    QCOMPARE(device.startupParameters.size(), 1);
    QCOMPARE(device.startupParameters.first().data, QByteArray::fromHex("08"));
    QCOMPARE(device.dcModes.size(), 1);
    QCOMPARE(device.dcModes.first().assignActivate, quint16(0x0300));
    QCOMPARE(device.dcModes.first().cycleTimeSync0Ns, qint64(125000));
    QVERIFY(device.synchronizationTypes.outputTypesDeclared);
    QCOMPARE(device.synchronizationTypes.outputSupportedTypes, quint16(0x0400));
    QVERIFY(device.synchronizationTypes.inputTypesDeclared);
    QCOMPARE(device.synchronizationTypes.inputSupportedTypes, quint16(0x0004));
    QVERIFY(
        std::any_of(device.warnings.cbegin(), device.warnings.cend(), [](const QString &warning) {
            return warning.contains("0x0400") && warning.contains("0x0004");
        }));
    QVERIFY(device.summary.supported);
    QCOMPARE(device.sourceSha256.size(), 32);

    const Utils::Result<QList<Data::DeviceDescription>> modular = parseEsiFile(
        esiDocument(simpleDevice(3, 3).replace("</Device>", "<Modules/></Device>")),
        "modular.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(modular);
    QVERIFY(!modular->first().summary.supported);
    QVERIFY(!modular->first().unsupportedFeatures.isEmpty());
}

void EtherCATDevicesTests::testBundledVendorEsiFiles()
{
    struct ExpectedDevice
    {
        QString fileName;
        QByteArray sha256;
        Data::DeviceIdentity identity;
        QString typeName;
        QString name;
        QString group;
        bool hasDistributedClocks = false;
    };
    const QList<ExpectedDevice> expectedDevices = {
        {"EcatTerminal-XB6_V3.22_ENUM.xml",
         QByteArray::fromHex(
             "5b0bfbfffdfde1fd293589deb4a1c59f974aa79dcd9206ac0a695ab00f395bf7"),
         {0x00884443, 0x000000b6, 0x00000001},
         "XB6-EC0002",
         "XB6-EC0002(Modules/Slots and MDP)",
         "XB6 Series Fieldbus",
         false},
        {"INOVANCE_SV630N_1Axis_V16.xml",
         QByteArray::fromHex(
             "e6f39fd4e0f8801c83ec3ac796e138fe3ee1566cb94bb28285b93538fdb9e4a1"),
         {0x00100000, 0x000c0112, 0x00010000},
         "InoSV630N",
         "SV630N_1Axis_03716",
         "Servo Drives",
         true},
    };

    const Utils::FilePath library = ::Core::ICore::resourcePath("ethercat/esi");
    for (const ExpectedDevice &expected : expectedDevices) {
        const Utils::FilePath filePath = library.pathAppended(expected.fileName);
        QVERIFY2(filePath.isFile(), qPrintable(filePath.toUserOutput()));
        const Utils::Result<QByteArray> contents = filePath.fileContents();
        QVERIFY_RESULT(contents);
        const Utils::Result<QList<Data::DeviceDescription>> parsed = parseEsiFile(
            *contents, filePath.toUserOutput(), QDateTime::currentDateTimeUtc());
        QVERIFY_RESULT(parsed);
        const auto match = std::find_if(
            parsed->cbegin(),
            parsed->cend(),
            [&expected](const Data::DeviceDescription &device) {
                return device.summary.identity == expected.identity;
            });
        QVERIFY(match != parsed->cend());
        QCOMPARE(match->sourceSha256, expected.sha256);
        QCOMPARE(match->summary.typeName, expected.typeName);
        QCOMPARE(match->summary.name, expected.name);
        QCOMPARE(match->summary.group, expected.group);
        QCOMPARE(!match->dcModes.isEmpty(), expected.hasDistributedClocks);
        if (expected.hasDistributedClocks) {
            QCOMPARE(match->dcModes.constFirst().name, QString("DC"));
            QCOMPARE(match->dcModes.constFirst().assignActivate, quint16(0x0300));
        }
    }
}

void EtherCATDevicesTests::testRepositoryImportFilterAndRebuild()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath repositoryRoot = temporaryPath(directory, "repository");
    const Utils::FilePath sourcePath = temporaryPath(directory, "library.xml");
    const QByteArray xml = esiDocument(simpleDevice(1, 1) + simpleDevice(2, 2));
    QVERIFY_RESULT(sourcePath.writeFileContents(xml));

    Data::NodeId firstId;
    {
        DeviceRepository repository(repositoryRoot);
        QVERIFY(repository.isAvailable());
        QSignalSpy changed(&repository, &Core::DeviceRepositoryProvider::devicesChanged);
        Core::DeviceImportJob *job = repository.importFiles({sourcePath, sourcePath});
        QVERIFY(job);
        QCOMPARE(job->state(), Core::DeviceImportState::Pending);
        const Data::DeviceImportResult result = waitForJob(job);
        QCOMPARE(result.requestedFiles, 2);
        QCOMPARE(result.duplicateFiles, 1);
        QCOMPARE(result.failedFiles, 0);
        QCOMPARE(result.importedDevices, 2);
        QVERIFY(!result.canceled);
        QCOMPARE(changed.size(), 1);
        QTRY_COMPARE(repository.activeJobCount(), 0);

        const QList<Data::DeviceSummary> allDevices = repository.devices();
        QCOMPARE(allDevices.size(), 2);
        firstId = allDevices.first().id;
        QVERIFY(!firstId.isNull());
        QCOMPARE(repository.originalXml(firstId), xml);

        Data::DeviceFilter filter;
        filter.text = "Digital Terminal 2";
        QCOMPARE(repository.devices(filter).size(), 1);
        filter = {};
        filter.filterByVendor = true;
        filter.vendorId = 7;
        QVERIFY(repository.devices(filter).isEmpty());
        filter.vendorId = 2;
        QCOMPARE(repository.devices(filter).size(), 2);

        Core::DeviceImportJob *duplicateJob = repository.importFiles({sourcePath});
        const Data::DeviceImportResult duplicate = waitForJob(duplicateJob);
        QCOMPARE(duplicate.duplicateFiles, 1);
        QCOMPARE(duplicate.importedDevices, 0);
        QCOMPARE(duplicate.updatedDevices, 0);
        QCOMPARE(repository.devices().size(), 2);
    }

    DeviceRepository rebuilt(repositoryRoot);
    QSignalSpy reset(&rebuilt, &Core::DeviceRepositoryProvider::devicesReset);
    Core::DeviceImportJob *rebuildJob = rebuilt.rebuildIndex();
    QVERIFY(rebuildJob);
    QVERIFY(rebuilt.isIndexing());
    const Data::DeviceImportResult rebuild = waitForJob(rebuildJob);
    QCOMPARE(rebuild.failedFiles, 0);
    QCOMPARE(rebuild.importedDevices, 2);
    QCOMPARE(reset.size(), 1);
    QVERIFY(!rebuilt.isIndexing());
    QCOMPARE(rebuilt.devices().first().id, firstId);
    QCOMPARE(rebuilt.originalXml(firstId), xml);

    DeviceRepository queued(temporaryPath(directory, "queued-repository"));
    Core::DeviceImportJob *queuedImport = queued.importFiles({sourcePath});
    Core::DeviceImportJob *queuedRebuild = queued.rebuildIndex();
    QVERIFY(queued.isIndexing());
    QCOMPARE(waitForJob(queuedImport).importedDevices, 2);
    QCOMPARE(waitForJob(queuedRebuild).importedDevices, 2);
    QCOMPARE(queued.devices().size(), 2);
    QVERIFY(!queued.isIndexing());
    QTRY_COMPARE(queued.activeJobCount(), 0);

    const Utils::FilePath latestRoot = temporaryPath(directory, "latest-repository");
    const Utils::FilePath oldPath = temporaryPath(directory, "old.xml");
    const Utils::FilePath newPath = temporaryPath(directory, "new.xml");
    const QByteArray oldXml = esiDocument(simpleDevice(1, 1));
    QByteArray newDevice = simpleDevice(1, 1);
    newDevice.replace("Digital Terminal 1", "Updated Terminal");
    const QByteArray newXml = esiDocument(newDevice);
    QVERIFY_RESULT(oldPath.writeFileContents(oldXml));
    QVERIFY_RESULT(newPath.writeFileContents(newXml));
    {
        DeviceRepository latest(latestRoot);
        QCOMPARE(waitForJob(latest.importFiles({oldPath})).importedDevices, 1);
        const Data::NodeId deviceId = latest.devices().first().id;
        QCOMPARE(waitForJob(latest.importFiles({newPath})).updatedDevices, 1);
        QCOMPARE(latest.device(deviceId)->summary.name, QString("Updated Terminal"));
        QCOMPARE(latest.originalXml(deviceId), newXml);
    }
    DeviceRepository latestRebuilt(latestRoot);
    QCOMPARE(waitForJob(latestRebuilt.rebuildIndex()).failedFiles, 0);
    QCOMPARE(latestRebuilt.devices().first().name, QString("Updated Terminal"));

    const Utils::FilePaths storedSources = repositoryRoot.pathAppended("sources").dirEntries(
        QDir::Files | QDir::NoDotAndDotDot);
    QCOMPARE(storedSources.size(), 1);
    QVERIFY_RESULT(storedSources.first().writeFileContents("<tampered/>"));
    DeviceRepository corrupted(repositoryRoot);
    const Data::DeviceImportResult corruptedResult = waitForJob(corrupted.rebuildIndex());
    QCOMPARE(corruptedResult.failedFiles, 1);
    QVERIFY(corrupted.devices().isEmpty());
}

void EtherCATDevicesTests::testLargeLibraryAndCancellation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray devices;
    for (int index = 0; index < 300; ++index) {
        devices += QByteArray("<Device><Type ProductCode=\"#x")
                   + QByteArray::number(0x2000 + index, 16)
                   + "\" RevisionNo=\"#x1\">T" + QByteArray::number(index)
                   + "</Type><Name>Device " + QByteArray::number(index)
                   + "</Name><GroupType>IO</GroupType></Device>";
    }
    const Utils::FilePath sourcePath = temporaryPath(directory, "large.xml");
    QVERIFY_RESULT(sourcePath.writeFileContents(esiDocument(devices)));

    DeviceRepository canceledRepository(temporaryPath(directory, "canceled"));
    Core::DeviceImportJob *canceledJob = canceledRepository.importFiles({sourcePath});
    QPointer<Core::DeviceImportJob> canceledGuard(canceledJob);
    QSignalSpy canceledFinished(canceledJob, &Core::DeviceImportJob::finished);
    canceledJob->cancel();
    QCOMPARE(canceledJob->state(), Core::DeviceImportState::Canceling);
    QVERIFY(canceledFinished.wait(15000));
    QVERIFY(canceledJob->result().canceled);
    QVERIFY(canceledRepository.devices().isEmpty());
    QTRY_COMPARE(canceledRepository.activeJobCount(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canceledGuard.isNull());

    DeviceRepository repository(temporaryPath(directory, "large-repository"));
    Core::DeviceImportJob *job = repository.importFiles({sourcePath});
    const Data::DeviceImportResult result = waitForJob(job);
    QCOMPARE(result.failedFiles, 0);
    QCOMPARE(result.importedDevices, 300);
    QCOMPARE(repository.devices().size(), 300);
}

} // namespace EtherCAT::Devices::Internal
