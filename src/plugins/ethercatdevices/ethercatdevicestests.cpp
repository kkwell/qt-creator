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

static QByteArray modularEsiDocument(
    const QByteArray &moduleElements,
    const QByteArray &slotsAttributes = "DownloadModuleIdentList=\"true\" "
                                        "SlotIndexIncrement=\"16\" SlotPdoIncrement=\"1\"",
    const QByteArray &moduleSuffix = {})
{
    const QByteArray effectiveSlotsAttributes
        = slotsAttributes.isEmpty()
              ? QByteArray("DownloadModuleIdentList=\"true\" "
                           "SlotIndexIncrement=\"16\" SlotPdoIncrement=\"1\"")
              : slotsAttributes;
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<EtherCATInfo>"
           "<Vendor><Id>#x00000002</Id><Name LcId=\"1033\">Vendor</Name></Vendor>"
           "<Descriptions>"
           "<Groups><Group><Type>IO</Type><Name LcId=\"1033\">I/O</Name></Group></Groups>"
           "<Devices><Device>"
           "<Type ProductCode=\"#x00001234\" RevisionNo=\"#x00000001\">EL-MDP</Type>"
           "<Name LcId=\"1033\">Modular terminal</Name><GroupType>IO</GroupType>"
           "<Slots "
           + effectiveSlotsAttributes
           + ">"
             "<Slot MinInstances=\"0\" MaxInstances=\"4\"><Name>Terminals</Name>"
             "<ModuleClass><Class>digital-output</Class>"
             "<Name LcId=\"1033\">Digital output terminals</Name></ModuleClass></Slot>"
             "<ModulePdoGroup Alignment=\"1\" RxPdo=\"#x1600\" TxPdo=\"#x1a00\"/>"
             "</Slots></Device></Devices>"
             "<Modules>"
           + moduleElements + moduleSuffix + "</Modules></Descriptions></EtherCATInfo>";
}

static QByteArray digitalOutputModule(const QByteArray &extraElements = {})
{
    return QByteArray(R"(<Module>
<Type ModuleIdent="#x00000010" ModuleClass="digital-output" ModulePdoGroup="0">EL-DO2</Type>
<Name LcId="1033">Two-channel digital output</Name>
<RxPdo Fixed="1" Sm="2"><Index DependOnSlot="1">#x1600</Index><Name>Outputs</Name>
 <Entry><Index DependOnSlot="true">#x7000</Index><SubIndex>1</SubIndex>
  <BitLen>1</BitLen><Name>Channel 1</Name><DataType>BOOL</DataType></Entry>
 <Entry><Index>#x0000</Index><BitLen>7</BitLen></Entry>
</RxPdo>)")
           + extraElements + "</Module>";
}

static QByteArray moduleParameterProfile(const QByteArray &parameterInfoExtra = {})
{
    return QByteArray(R"(<Profile><Dictionary><DataTypes>
<DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType>
<DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
<DataType><Name>MODE</Name><BaseType>UDINT</BaseType><BitSize>32</BitSize>
 <EnumInfo><Text>Off</Text><Enum>0</Enum></EnumInfo>
 <EnumInfo><Text>On</Text><Enum>1</Enum></EnumInfo>
</DataType>
<DataType><Name>CFG</Name><BitSize>48</BitSize>
 <SubItem><SubIdx>0</SubIdx><Name>SubIndex 000</Name><Type>USINT</Type>
  <BitSize>8</BitSize><BitOffs>0</BitOffs>
  <Flags><Access>ro</Access><Setting>0</Setting></Flags></SubItem>
 <SubItem><SubIdx>1</SubIdx><Name>Mode</Name><Type>MODE</Type>
  <BitSize>32</BitSize><BitOffs>16</BitOffs>
  <Flags><Access>rw</Access><Setting>1</Setting></Flags></SubItem>
</DataType></DataTypes><Objects><Object>
<Index DependOnSlot="true">#x2000</Index><Name>Configuration</Name>
<Type>CFG</Type><BitSize>48</BitSize><Info>
 <SubItem><Name>SubIndex 000</Name><Info><DefaultData>01</DefaultData></Info></SubItem>
 <SubItem><Name>Mode</Name><Info><MinData>00000000</MinData>
  <MaxData>01000000</MaxData><DefaultData>01000000</DefaultData>)")
           + parameterInfoExtra
           + R"(</Info></SubItem>
</Info><Flags><Access>rw</Access><Category>o</Category></Flags>
</Object></Objects></Dictionary></Profile>)";
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

    QVERIFY(!device.moduleCatalog.available);
    QVERIFY(device.moduleCatalog.modules.isEmpty());
}

void EtherCATDevicesTests::testParserReadsModuleCatalog()
{
    const Utils::Result<QList<Data::DeviceDescription>> parsed = parseEsiFile(
        modularEsiDocument(digitalOutputModule()),
        "modular.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(parsed);
    QCOMPARE(parsed->size(), 1);

    const Data::DeviceDescription &device = parsed->constFirst();
    QVERIFY(device.moduleCatalog.available);
    QVERIFY(device.moduleCatalog.downloadModuleIdentList);
    QCOMPARE(device.moduleCatalog.slotIndexIncrement, 16);
    QCOMPARE(device.moduleCatalog.slotPdoIncrement, 1);
    QCOMPARE(device.moduleCatalog.slotConstraints.size(), 1);
    const Data::ModuleSlotConstraintDescription &slot
        = device.moduleCatalog.slotConstraints.constFirst();
    QCOMPARE(slot.name, QString("Terminals"));
    QCOMPARE(slot.minimumInstances, 0);
    QCOMPARE(slot.maximumInstances, 4);
    QCOMPARE(slot.allowedModuleClasses.size(), 1);
    QCOMPARE(slot.allowedModuleClasses.constFirst().identifier, QString("digital-output"));

    QCOMPARE(device.moduleCatalog.pdoGroups.size(), 1);
    const Data::ModulePdoGroupDescription &group = device.moduleCatalog.pdoGroups.constFirst();
    QCOMPARE(group.index, 0);
    QCOMPARE(group.alignment, 1);
    QVERIFY(group.hasRxPdo);
    QCOMPARE(group.rxPdoIndex, quint16(0x1600));
    QVERIFY(group.hasTxPdo);
    QCOMPARE(group.txPdoIndex, quint16(0x1a00));

    QCOMPARE(device.moduleCatalog.modules.size(), 1);
    const Data::ModuleDescription &module = device.moduleCatalog.modules.constFirst();
    QCOMPARE(module.moduleIdent, quint32(0x10));
    QCOMPARE(module.typeName, QString("EL-DO2"));
    QCOMPARE(module.name, QString("Two-channel digital output"));
    QCOMPARE(module.moduleClass, QString("digital-output"));
    QCOMPARE(module.modulePdoGroupIndex, 0);
    QCOMPARE(module.rxPdos.size(), 1);
    QVERIFY(module.rxPdos.constFirst().indexDependsOnSlot);
    QCOMPARE(module.rxPdos.constFirst().entries.size(), 2);
    QVERIFY(module.rxPdos.constFirst().entries.constFirst().indexDependsOnSlot);
    QCOMPARE(module.rxPdos.constFirst().entries.constLast().index, quint16(0));
    QVERIFY(device.summary.supported);

    const Utils::Result<QList<Data::DeviceDescription>> profiled = parseEsiFile(
        modularEsiDocument(digitalOutputModule(moduleParameterProfile())),
        "module-parameters.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(profiled);
    const Data::ModuleDescription &profiledModule
        = profiled->constFirst().moduleCatalog.modules.constFirst();
    QCOMPARE(profiledModule.parameterObjects.size(), 1);
    const Data::ModuleParameterObjectDescription &parameterObject
        = profiledModule.parameterObjects.constFirst();
    QCOMPARE(parameterObject.index, quint16(0x2000));
    QVERIFY(parameterObject.indexDependsOnSlot);
    QCOMPARE(parameterObject.rawDataType, QString("CFG"));
    QCOMPARE(parameterObject.bitLength, 48);
    QCOMPARE(parameterObject.access, Data::ParameterAccess::ReadWrite);
    QCOMPARE(parameterObject.category, QString("o"));
    QCOMPARE(parameterObject.parameters.size(), 2);
    const Data::ModuleParameterDescription &modeParameter = parameterObject.parameters.at(1);
    QCOMPARE(modeParameter.subIndex, quint8(1));
    QCOMPARE(modeParameter.rawDataType, QString("MODE"));
    QCOMPARE(modeParameter.dataType, Data::EtherCATDataType::UnsignedInteger32);
    QCOMPARE(modeParameter.bitLength, 32);
    QCOMPARE(modeParameter.bitOffset, 16);
    QCOMPARE(modeParameter.access, Data::ParameterAccess::ReadWrite);
    QVERIFY(modeParameter.setting);
    QCOMPARE(modeParameter.defaultData, QByteArray::fromHex("01000000"));
    QCOMPARE(modeParameter.minimumData, QByteArray::fromHex("00000000"));
    QCOMPARE(modeParameter.maximumData, QByteArray::fromHex("01000000"));
    QCOMPARE(modeParameter.enumValues.size(), 2);
    QCOMPARE(modeParameter.enumValues.at(1).name, QString("On"));
    QCOMPARE(modeParameter.enumValues.at(1).value, QString("1"));
    QVERIFY(profiled->constFirst().summary.supported);

    QByteArray shortValueProfile = moduleParameterProfile();
    shortValueProfile.replace(
        "<DefaultData>01000000</DefaultData>",
        "<DefaultData>0100</DefaultData>");
    const Utils::Result<QList<Data::DeviceDescription>> shortValue = parseEsiFile(
        modularEsiDocument(digitalOutputModule(shortValueProfile)),
        "short-parameter-value.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(shortValue);
    const Data::ModuleDescription &shortValueModule
        = shortValue->constFirst().moduleCatalog.modules.constFirst();
    QVERIFY(!shortValueModule.parameterConfigurationSupported);
    QCOMPARE(
        shortValueModule.parameterObjects.constFirst().parameters.at(1).defaultData,
        QByteArray::fromHex("0100"));

    const Utils::Result<QList<Data::DeviceDescription>> unknownParameterStructure = parseEsiFile(
        modularEsiDocument(
            digitalOutputModule(moduleParameterProfile("<Scale>1</Scale>"))),
        "unsupported-parameter-structure.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(unknownParameterStructure);
    QVERIFY(unknownParameterStructure->constFirst().summary.supported);
    const Data::ModuleDescription &unsupportedParameterModule
        = unknownParameterStructure->constFirst().moduleCatalog.modules.constFirst();
    QVERIFY(!unsupportedParameterModule.parameterConfigurationSupported);
    QVERIFY(std::any_of(
        unsupportedParameterModule.parameterWarnings.cbegin(),
        unsupportedParameterModule.parameterWarnings.cend(),
        [](const QString &feature) { return feature.contains("Scale"); }));

    const Utils::Result<QList<Data::DeviceDescription>> unknownStructure = parseEsiFile(
        modularEsiDocument(digitalOutputModule("<VendorSpecificMapping/>")),
        "unsupported-module-structure.xml",
        QDateTime::currentDateTimeUtc());
    QVERIFY_RESULT(unknownStructure);
    QVERIFY(!unknownStructure->constFirst().summary.supported);
    QVERIFY(std::any_of(
        unknownStructure->constFirst().unsupportedFeatures.cbegin(),
        unknownStructure->constFirst().unsupportedFeatures.cend(),
        [](const QString &feature) { return feature.contains("VendorSpecificMapping"); }));
}

void EtherCATDevicesTests::testParserRejectsInvalidModuleCatalog()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray module = digitalOutputModule();

    const Utils::Result<QList<Data::DeviceDescription>> duplicateIdent = parseEsiFile(
        modularEsiDocument(module, {}, module), "duplicate-module-ident.xml", now);
    QVERIFY(!duplicateIdent);
    QVERIFY(duplicateIdent.error().contains("ModuleIdent"));

    QByteArray invalidIdent = module;
    invalidIdent.replace("#x00000010", "not-a-number");
    const Utils::Result<QList<Data::DeviceDescription>> badIdent = parseEsiFile(
        modularEsiDocument(invalidIdent), "invalid-module-ident.xml", now);
    QVERIFY(!badIdent);
    QVERIFY(badIdent.error().contains("ModuleIdent"));

    QByteArray danglingClass = module;
    danglingClass.replace("ModuleClass=\"digital-output\"", "ModuleClass=\"unknown-class\"");
    const Utils::Result<QList<Data::DeviceDescription>> badClass = parseEsiFile(
        modularEsiDocument(danglingClass), "dangling-module-class.xml", now);
    QVERIFY(!badClass);
    QVERIFY(badClass.error().contains("unknown-class"));

    QByteArray invalidPdo = module;
    invalidPdo.replace(
        "<Index DependOnSlot=\"1\">#x1600</Index>",
        "<Index DependOnSlot=\"1\">invalid</Index>");
    const Utils::Result<QList<Data::DeviceDescription>> badPdo = parseEsiFile(
        modularEsiDocument(invalidPdo), "invalid-module-pdo.xml", now);
    QVERIFY(!badPdo);
    QVERIFY(badPdo.error().contains("PDO"));

    QByteArray danglingGroup = module;
    danglingGroup.replace("ModulePdoGroup=\"0\"", "ModulePdoGroup=\"3\"");
    const Utils::Result<QList<Data::DeviceDescription>> badGroup = parseEsiFile(
        modularEsiDocument(danglingGroup), "dangling-pdo-group.xml", now);
    QVERIFY(!badGroup);
    QVERIFY(badGroup.error().contains("ModulePdoGroup 3"));

    const Utils::Result<QList<Data::DeviceDescription>> badSlotIncrement = parseEsiFile(
        modularEsiDocument(
            module,
            "DownloadModuleIdentList=\"true\" SlotIndexIncrement=\"bad\" "
            "SlotPdoIncrement=\"1\""),
        "invalid-slot-increment.xml",
        now);
    QVERIFY(!badSlotIncrement);
    QVERIFY(badSlotIncrement.error().contains("SlotIndexIncrement"));

    QByteArray duplicateTypeProfile = moduleParameterProfile();
    duplicateTypeProfile.replace(
        "</DataTypes>",
        "<DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>");
    const Utils::Result<QList<Data::DeviceDescription>> duplicateType = parseEsiFile(
        modularEsiDocument(digitalOutputModule(duplicateTypeProfile)),
        "duplicate-parameter-type.xml",
        now);
    QVERIFY(!duplicateType);
    QVERIFY(duplicateType.error().contains("duplicate DataType"));

    QByteArray danglingTypeProfile = moduleParameterProfile();
    danglingTypeProfile.replace("<Type>MODE</Type>", "<Type>MISSING</Type>");
    const Utils::Result<QList<Data::DeviceDescription>> danglingType = parseEsiFile(
        modularEsiDocument(digitalOutputModule(danglingTypeProfile)),
        "dangling-parameter-type.xml",
        now);
    QVERIFY(!danglingType);
    QVERIFY(danglingType.error().contains("MISSING"));

    QByteArray mismatchedInfoProfile = moduleParameterProfile();
    mismatchedInfoProfile.replace(
        "<SubItem><Name>Mode</Name><Info>",
        "<SubItem><Name>Wrong parameter</Name><Info>");
    const Utils::Result<QList<Data::DeviceDescription>> mismatchedInfo = parseEsiFile(
        modularEsiDocument(digitalOutputModule(mismatchedInfoProfile)),
        "mismatched-parameter-info.xml",
        now);
    QVERIFY(!mismatchedInfo);
    QVERIFY(mismatchedInfo.error().contains("does not match"));

    QByteArray invalidDataProfile = moduleParameterProfile();
    invalidDataProfile.replace(
        "<DefaultData>01000000</DefaultData>",
        "<DefaultData>not-hex</DefaultData>");
    const Utils::Result<QList<Data::DeviceDescription>> invalidData = parseEsiFile(
        modularEsiDocument(digitalOutputModule(invalidDataProfile)),
        "invalid-parameter-data.xml",
        now);
    QVERIFY(!invalidData);
    QVERIFY(invalidData.error().contains("hexadecimal"));

    QByteArray invalidAccessProfile = moduleParameterProfile();
    invalidAccessProfile.replace("<Access>rw</Access>", "<Access>invalid</Access>");
    const Utils::Result<QList<Data::DeviceDescription>> invalidAccess = parseEsiFile(
        modularEsiDocument(digitalOutputModule(invalidAccessProfile)),
        "invalid-parameter-access.xml",
        now);
    QVERIFY(!invalidAccess);
    QVERIFY(invalidAccess.error().contains("access"));

    QByteArray outOfRangeEnumProfile = moduleParameterProfile();
    outOfRangeEnumProfile.replace("<Enum>1</Enum>", "<Enum>4294967296</Enum>");
    const Utils::Result<QList<Data::DeviceDescription>> outOfRangeEnum = parseEsiFile(
        modularEsiDocument(digitalOutputModule(outOfRangeEnumProfile)),
        "out-of-range-parameter-enum.xml",
        now);
    QVERIFY(!outOfRangeEnum);
    QVERIFY(outOfRangeEnum.error().contains("out-of-range"));
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
            QVERIFY(!match->moduleCatalog.available);
            QVERIFY(match->moduleCatalog.modules.isEmpty());
            QVERIFY(match->summary.supported);
        } else {
            QVERIFY(match->moduleCatalog.available);
            QVERIFY(match->moduleCatalog.downloadModuleIdentList);
            QCOMPARE(match->moduleCatalog.slotIndexIncrement, 16);
            QCOMPARE(match->moduleCatalog.slotPdoIncrement, 1);
            QCOMPARE(match->moduleCatalog.slotConstraints.size(), 1);
            QCOMPARE(match->moduleCatalog.slotConstraints.constFirst().minimumInstances, 0);
            QCOMPARE(match->moduleCatalog.slotConstraints.constFirst().maximumInstances, 32);
            QCOMPARE(
                match->moduleCatalog.slotConstraints.constFirst().allowedModuleClasses.size(), 8);
            QCOMPARE(match->moduleCatalog.pdoGroups.size(), 2);
            QCOMPARE(match->moduleCatalog.pdoGroups.at(0).alignment, 2);
            QCOMPARE(match->moduleCatalog.pdoGroups.at(0).rxPdoIndex, quint16(0x16ff));
            QCOMPARE(match->moduleCatalog.pdoGroups.at(0).txPdoIndex, quint16(0x1aff));
            QCOMPARE(match->moduleCatalog.pdoGroups.at(1).alignment, 1);
            QCOMPARE(match->moduleCatalog.pdoGroups.at(1).rxPdoIndex, quint16(0x1600));
            QCOMPARE(match->moduleCatalog.pdoGroups.at(1).txPdoIndex, quint16(0x1a00));
            QCOMPARE(match->moduleCatalog.modules.size(), 42);
            const auto module = std::find_if(
                match->moduleCatalog.modules.cbegin(),
                match->moduleCatalog.modules.cend(),
                [](const Data::ModuleDescription &candidate) {
                    return candidate.moduleIdent == 0x00000629;
                });
            QVERIFY(module != match->moduleCatalog.modules.cend());
            QCOMPARE(module->typeName, QString("XB6-1600B"));
            QCOMPARE(module->moduleClass, QString("xb_dig_in"));
            QCOMPARE(module->modulePdoGroupIndex, 1);
            QCOMPARE(module->txPdos.size(), 1);
            QCOMPARE(module->txPdos.constFirst().index, quint16(0x1a00));
            QVERIFY(module->txPdos.constFirst().indexDependsOnSlot);
            QCOMPARE(module->txPdos.constFirst().entries.size(), 16);
            QCOMPARE(module->txPdos.constFirst().entries.constFirst().index, quint16(0x6000));
            QVERIFY(module->txPdos.constFirst().entries.constFirst().indexDependsOnSlot);
            const auto outputModule = std::find_if(
                match->moduleCatalog.modules.cbegin(),
                match->moduleCatalog.modules.cend(),
                [](const Data::ModuleDescription &candidate) {
                    return candidate.moduleIdent == 0x00000625;
                });
            QVERIFY(outputModule != match->moduleCatalog.modules.cend());
            QCOMPARE(outputModule->typeName, QString("XB6-0016B(W)"));
            QCOMPARE(outputModule->moduleClass, QString("xb_dig_out"));
            QCOMPARE(outputModule->modulePdoGroupIndex, 1);
            QCOMPARE(outputModule->rxPdos.size(), 1);
            QCOMPARE(outputModule->rxPdos.constFirst().index, quint16(0x1600));
            QCOMPARE(outputModule->rxPdos.constFirst().entries.size(), 16);
            QCOMPARE(outputModule->rxPdos.constFirst().entries.constFirst().index, quint16(0x7000));
            int parameterObjectCount = 0;
            int parameterCount = 0;
            int materializedEnumValueCount = 0;
            int parameterWarningCount = 0;
            int readOnlyParameterCount = 0;
            int readWriteParameterCount = 0;
            int unqualifiedParameterModuleCount = 0;
            for (const Data::ModuleDescription &candidate : match->moduleCatalog.modules) {
                parameterObjectCount += candidate.parameterObjects.size();
                parameterWarningCount += candidate.parameterWarnings.size();
                if (!candidate.parameterConfigurationSupported)
                    ++unqualifiedParameterModuleCount;
                for (const Data::ModuleParameterObjectDescription &object :
                     candidate.parameterObjects) {
                    parameterCount += object.parameters.size();
                    for (const Data::ModuleParameterDescription &parameter : object.parameters) {
                        materializedEnumValueCount += parameter.enumValues.size();
                        if (parameter.access == Data::ParameterAccess::ReadOnly)
                            ++readOnlyParameterCount;
                        else if (parameter.access == Data::ParameterAccess::ReadWrite)
                            ++readWriteParameterCount;
                    }
                }
            }
            QCOMPARE(parameterObjectCount, 30);
            QCOMPARE(parameterCount, 283);
            // The ESI declares 452 EnumInfo entries. Reused enum data types are copied to every
            // parameter that references them, producing 688 parameter-local enum choices.
            QCOMPARE(materializedEnumValueCount, 688);
            QCOMPARE(parameterWarningCount, 26);
            QCOMPARE(readOnlyParameterCount, 30);
            QCOMPARE(readWriteParameterCount, 253);
            QCOMPARE(unqualifiedParameterModuleCount, 1);

            const auto profiledModule = std::find_if(
                match->moduleCatalog.modules.cbegin(),
                match->moduleCatalog.modules.cend(),
                [](const Data::ModuleDescription &candidate) {
                    return candidate.moduleIdent == 0x00000627;
                });
            QVERIFY(profiledModule != match->moduleCatalog.modules.cend());
            QVERIFY(profiledModule->parameterConfigurationSupported);
            QCOMPARE(profiledModule->parameterObjects.size(), 1);
            const Data::ModuleParameterObjectDescription &object
                = profiledModule->parameterObjects.constFirst();
            QCOMPARE(object.index, quint16(0x2000));
            QVERIFY(object.indexDependsOnSlot);
            QCOMPARE(object.rawDataType, QString("DT2000"));
            QCOMPARE(object.bitLength, 48);
            QCOMPARE(object.parameters.size(), 2);
            const Data::ModuleParameterDescription &debounce = object.parameters.at(1);
            QCOMPARE(debounce.subIndex, quint8(1));
            QCOMPARE(debounce.name, QString("Channel Debounce Time"));
            QCOMPARE(debounce.rawDataType, QString("DT0800EN32"));
            QCOMPARE(debounce.dataType, Data::EtherCATDataType::UnsignedInteger32);
            QCOMPARE(debounce.defaultData, QByteArray::fromHex("03000000"));
            QCOMPARE(debounce.minimumData, QByteArray::fromHex("00000000"));
            QCOMPARE(debounce.maximumData, QByteArray::fromHex("14000000"));
            QCOMPARE(debounce.enumValues.size(), 23);
            QVERIFY(std::any_of(
                debounce.enumValues.cbegin(),
                debounce.enumValues.cend(),
                [](const Data::ModuleParameterEnumValueDescription &enumValue) {
                    return enumValue.value == "125" && enumValue.name == "0.25ms";
                }));

            const auto p20dModule = std::find_if(
                match->moduleCatalog.modules.cbegin(),
                match->moduleCatalog.modules.cend(),
                [](const Data::ModuleDescription &candidate) {
                    return candidate.moduleIdent == 0x0000620d;
                });
            QVERIFY(p20dModule != match->moduleCatalog.modules.cend());
            QVERIFY(!p20dModule->parameterConfigurationSupported);
            QCOMPARE(p20dModule->parameterWarnings.size(), 26);
            QVERIFY(std::any_of(
                p20dModule->parameterWarnings.cbegin(),
                p20dModule->parameterWarnings.cend(),
                [](const QString &warning) {
                    return warning.contains("32-bit") && warning.contains("raw bytes");
                }));
            QVERIFY(match->summary.supported);
            QVERIFY(match->unsupportedFeatures.isEmpty());
            QVERIFY(std::none_of(
                match->unsupportedFeatures.cbegin(),
                match->unsupportedFeatures.cend(),
                [](const QString &feature) {
                    return feature.contains("Modules structure")
                           || feature.contains("Slots structure");
                }));
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
        Utils::DirFilterFlag::Files | Utils::DirFilterFlag::NoDotAndDotDot);
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
