// Copyright (C) 2026 Kvell

#include "ethercatprojecttests.h"

#include "ethercatproject.h"
#include "ethercatprojectconstants.h"
#include "ethercatprojectdocument.h"
#include "ethercatprojectformat.h"
#include "ethercatprojecttr.h"
#include "projectserviceimpl.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/iwizardfactory.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <ethercatdata/offlineconfiguration.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/taskhub.h>

#include <utils/filepath.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace EtherCAT::Project::Internal {

static Utils::FilePath temporaryFilePath(const QTemporaryDir &directory, const QString &fileName)
{
    return Utils::FilePath::fromString(directory.path()).canonicalPath().pathAppended(fileName);
}

static void writeProject(const Utils::FilePath &filePath, const Data::ProjectSnapshot &snapshot)
{
    const Utils::Result<qint64> writeResult = filePath.writeFileContents(serializeProject(snapshot));
    QVERIFY_RESULT(writeResult);
}

static Data::NodeId masterId(const Data::ProjectSnapshot &snapshot)
{
    for (const Data::ProjectNodeSnapshot &node : snapshot.nodes) {
        if (node.kind == Data::ProjectNodeKind::Master)
            return node.id;
    }
    return {};
}

static Data::NodeId targetId(const Data::ProjectSnapshot &snapshot)
{
    for (const Data::ProjectNodeSnapshot &node : snapshot.nodes) {
        if (node.kind == Data::ProjectNodeKind::Target)
            return node.id;
    }
    return {};
}

static QString nodeName(const Data::ProjectSnapshot &snapshot, const Data::NodeId &nodeId)
{
    for (const Data::ProjectNodeSnapshot &node : snapshot.nodes) {
        if (node.id == nodeId)
            return node.name;
    }
    return {};
}

static QList<Data::OfflineSlaveConfiguration> offlineSlaves(const Data::NodeId &master)
{
    return {
        {Data::NodeId::create(),
         master,
         1,
         {2, 0x2000, 2},
         102,
         0,
         "Mock Drive B",
         Data::NodeId::create(),
         {},
         {},
         {}},
        {Data::NodeId::create(), master, 0, {2, 0x1000, 1}, 101, 7, "Mock I/O A", {}, {}, {}, {}}};
}

static Data::ProcessDataConfiguration processDataConfiguration()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::MasterToSlave, true, 8},
        {Data::NodeId::create(), 3, "Inputs", Data::SyncManagerDirection::SlaveToMaster, true, 4},
    };

    Data::PdoConfiguration outputs;
    outputs.id = Data::NodeId::create();
    outputs.index = 0x1600;
    outputs.name = "Drive outputs";
    outputs.direction = Data::PdoDirection::Rx;
    outputs.syncManager = 2;
    outputs.selected = true;
    outputs.mandatory = true;
    outputs.defaultSelected = true;
    outputs.entries = {
        {Data::NodeId::create(),
         0x6040,
         0,
         "Controlword",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT"},
        {Data::NodeId::create(),
         0x607a,
         0,
         "Target position",
         32,
         Data::EtherCATDataType::Integer32,
         "DINT"},
    };

    Data::PdoConfiguration inputs;
    inputs.id = Data::NodeId::create();
    inputs.index = 0x1a00;
    inputs.name = "Drive inputs";
    inputs.direction = Data::PdoDirection::Tx;
    inputs.syncManager = 3;
    inputs.selected = true;
    inputs.fixed = true;
    inputs.entries = {
        {Data::NodeId::create(),
         0x6041,
         0,
         "Statusword",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT"},
    };
    configuration.pdos = {outputs, inputs};
    return configuration;
}

static Data::StartupConfiguration startupConfiguration()
{
    return {
        {{Data::NodeId::create(),
          true,
          0,
          "PS",
          0x6060,
          0,
          Data::EtherCATDataType::Integer8,
          "SINT",
          QByteArray::fromHex("08"),
          "Cyclic synchronous position mode"}}};
}

static Data::DcConfiguration dcConfiguration()
{
    return {true, "DC-Synchronous", 0x0300, {true, 125000, -1000}, {}, true};
}

void EtherCATProjectTests::testMetadataAndService()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        Constants::PLUGIN_ID);
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATProject"));
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
    QVERIFY(hasRequiredDependency("projectexplorer"));
    QVERIFY(hasRequiredDependency("ethercatcore"));

    auto *service = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    QVERIFY(service);
    QCOMPARE(service->id(), Utils::Id(Constants::PROJECT_SERVICE_ID));
    QVERIFY(service->isAvailable());
    QVERIFY(service->projects().isEmpty());
    QVERIFY(service->activeProjectId().isNull());

    const QList<::Core::IWizardFactory *> factories = ::Core::IWizardFactory::allWizardFactories();
    const auto wizard = std::find_if(factories.cbegin(), factories.cend(), [](const auto *factory) {
        return factory->id() == Utils::Id(Constants::WIZARD_ID);
    });
    QVERIFY(wizard != factories.cend());
    QCOMPARE((*wizard)->kind(), ::Core::IWizardFactory::ProjectWizard);
    QCOMPARE((*wizard)->supportedProjectTypes(), QSet<Utils::Id>({Constants::PROJECT_ID}));
}

void EtherCATProjectTests::testProjectNeedsNoTargetConfiguration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "targetless.ecatproject");
    writeProject(projectFile, createProjectSnapshot("Targetless", "Test"));

    EtherCATProject project(projectFile);
    QVERIFY(project.snapshot().valid);
    QVERIFY(!project.needsConfiguration());
    QVERIFY(project.targets().isEmpty());
    QVERIFY(!project.activeTarget());
}

void EtherCATProjectTests::testFormatRoundTripAndCorruption()
{
    const Data::ProjectSnapshot source = createProjectSnapshot("Packing Line", "Test 20.0.1");
    const Utils::Result<LoadedProject> loaded = parseProject(serializeProject(source), "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->snapshot, source);
    QVERIFY(!loaded->migrationRequired);
    QCOMPARE(loaded->sourceFormatVersion, Constants::CURRENT_FORMAT_VERSION);

    const Utils::Result<LoadedProject> malformed = parseProject("{broken", "Fallback");
    QVERIFY(!malformed);
    QVERIFY(!malformed.error().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath corruptFile = temporaryFilePath(directory, "corrupt.ecatproject");
    QVERIFY_RESULT(corruptFile.writeFileContents("{broken"));
    EtherCATProjectDocument corruptDocument;
    QVERIFY(!corruptDocument.load(corruptFile));
    QVERIFY(!corruptDocument.snapshot().valid);
    QVERIFY(!corruptDocument.snapshot().error.isEmpty());
    QVERIFY(!corruptDocument.save());

    QJsonObject unsupported;
    unsupported.insert("format", "ethercat-project");
    unsupported.insert("formatVersion", 99);
    const Utils::Result<LoadedProject> future
        = parseProject(QJsonDocument(unsupported).toJson(), "Fallback");
    QVERIFY(!future);
    QVERIFY(!future.error().isEmpty());
}

void EtherCATProjectTests::testDocumentUndoRedoAndAtomicFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "undo.ecatproject");
    const Data::ProjectSnapshot source = createProjectSnapshot("Original", "Test");
    const QByteArray originalContents = serializeProject(source);
    QVERIFY_RESULT(projectFile.writeFileContents(originalContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(!document.isModified());
    QVERIFY(!document.isSaveAsAllowed());
    QVERIFY(!document.shouldAutoSave());
    QCOMPARE(document.snapshot().name, QString("Original"));

    const Data::ProjectSnapshot differentProject = createProjectSnapshot("Different", "Test");
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(differentProject)));
    QVERIFY(!document.reload(::Core::IDocument::FlagReload, ::Core::IDocument::TypeContents));
    QCOMPARE(document.snapshot().id, source.id);
    QVERIFY_RESULT(projectFile.writeFileContents(originalContents));

    QVERIFY_RESULT(document.renameProject("Renamed"));
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().name, QString("Renamed"));
    QVERIFY(document.undoStack()->canUndo());

    document.undoStack()->undo();
    QCOMPARE(document.snapshot().name, QString("Original"));
    QVERIFY(!document.isModified());
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().name, QString("Renamed"));
    QVERIFY(document.isModified());

    const Utils::Result<> failedSave = document.save(Utils::FilePath::fromString(directory.path()));
    QVERIFY(!failedSave);
    QVERIFY(document.isModified());

    QFile directoryFile(directory.path());
    const QFileDevice::Permissions originalPermissions = QFileInfo(directory.path()).permissions();
    QVERIFY(directoryFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const Utils::Result<> failedAtomicSave = document.save();
    QVERIFY(directoryFile.setPermissions(originalPermissions));
    QVERIFY(!failedAtomicSave);
    QVERIFY(document.isModified());

    const Utils::Result<QByteArray> unchanged = projectFile.fileContents();
    QVERIFY_RESULT(unchanged);
    QCOMPARE(*unchanged, originalContents);

    QVERIFY_RESULT(document.save());
    QVERIFY(!document.isModified());
    const Utils::Result<LoadedProject> saved = parseProject(*projectFile.fileContents(), "Fallback");
    QVERIFY_RESULT(saved);
    QCOMPARE(saved->snapshot.name, QString("Renamed"));
}

void EtherCATProjectTests::testStructuralNodeRenameAndPersistence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "node-names.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Node Names", "Test");
    const Data::NodeId target = targetId(source);
    const Data::NodeId master = masterId(source);
    const QList<Data::OfflineSlaveConfiguration> slaves = offlineSlaves(master);
    source.slaves = slaves;
    for (const Data::OfflineSlaveConfiguration &slave : slaves)
        source.nodes.append({slave.id, slave.masterId, Data::ProjectNodeKind::Slave, slave.name});
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(!target.isNull());
    QVERIFY(!master.isNull());
    const QString originalTargetName = nodeName(document.snapshot(), target);
    const QString originalMasterName = nodeName(document.snapshot(), master);

    QVERIFY_RESULT(document.renameStructuralNode(master, "  EtherCAT Device 1  "));
    QCOMPARE(nodeName(document.snapshot(), master), QString("EtherCAT Device 1"));
    QCOMPARE(document.undoStack()->count(), 1);
    QVERIFY_RESULT(document.renameStructuralNode(master, "EtherCAT Device 1"));
    QCOMPARE(document.undoStack()->count(), 1);

    QVERIFY(!document.renameStructuralNode(master, "   "));
    QVERIFY(!document.renameStructuralNode(source.id, "Project through node API"));
    QVERIFY(!document.renameStructuralNode(slaves.first().id, "Slave through node API"));
    QVERIFY(!document.renameStructuralNode(Data::NodeId::create(), "Unknown node"));
    QCOMPARE(document.undoStack()->count(), 1);

    QVERIFY_RESULT(document.renameStructuralNode(target, "  Industrial PC  "));
    QCOMPARE(nodeName(document.snapshot(), target), QString("Industrial PC"));
    QCOMPARE(document.undoStack()->count(), 2);
    QVERIFY(document.isModified());

    document.undoStack()->undo();
    QCOMPARE(nodeName(document.snapshot(), target), originalTargetName);
    document.undoStack()->undo();
    QCOMPARE(nodeName(document.snapshot(), master), originalMasterName);
    QVERIFY(!document.isModified());

    document.undoStack()->redo();
    document.undoStack()->redo();
    QCOMPARE(nodeName(document.snapshot(), master), QString("EtherCAT Device 1"));
    QCOMPARE(nodeName(document.snapshot(), target), QString("Industrial PC"));
    QVERIFY_RESULT(document.save());
    QVERIFY(!document.isModified());

    const Utils::Result<QByteArray> savedContents = projectFile.fileContents();
    QVERIFY_RESULT(savedContents);
    const Utils::Result<LoadedProject> loaded = parseProject(*savedContents, "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(nodeName(loaded->snapshot, master), QString("EtherCAT Device 1"));
    QCOMPARE(nodeName(loaded->snapshot, target), QString("Industrial PC"));
}

void EtherCATProjectTests::testOfflineSlavePersistenceAndUndo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "slaves.ecatproject");
    const Data::ProjectSnapshot source = createProjectSnapshot("Scan Target", "Test");
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    const Data::NodeId master = masterId(document.snapshot());
    QVERIFY(!master.isNull());
    const QList<Data::OfflineSlaveConfiguration> slaves = offlineSlaves(master);
    QVERIFY_RESULT(document.replaceOfflineSlaves(master, slaves));
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().slaves.size(), 2);
    QCOMPARE(document.snapshot().slaves.at(0).position, 0);
    QCOMPARE(document.snapshot().nodes.size(), 5);

    QList<Data::OfflineSlaveConfiguration> duplicatePositions = slaves;
    duplicatePositions[0].position = 0;
    QVERIFY(!document.replaceOfflineSlaves(master, duplicatePositions));
    QCOMPARE(document.snapshot().slaves.size(), 2);

    document.undoStack()->undo();
    QVERIFY(document.snapshot().slaves.isEmpty());
    QCOMPARE(document.snapshot().nodes.size(), 3);
    QVERIFY(!document.isModified());
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().slaves.size(), 2);
    QVERIFY(document.isModified());

    QVERIFY_RESULT(document.save());
    QVERIFY(!document.isModified());
    const Utils::Result<QByteArray> savedContents = projectFile.fileContents();
    QVERIFY_RESULT(savedContents);
    const Utils::Result<LoadedProject> loaded = parseProject(*savedContents, "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->snapshot.slaves, document.snapshot().slaves);
    QCOMPARE(loaded->snapshot.nodes, document.snapshot().nodes);

    QJsonDocument malformed = QJsonDocument::fromJson(*savedContents);
    QJsonObject malformedRoot = malformed.object();
    QJsonObject malformedMaster = malformedRoot.value("master").toObject();
    QJsonArray malformedSlaves = malformedMaster.value("slaves").toArray();
    QJsonObject duplicate = malformedSlaves.at(1).toObject();
    duplicate.insert("position", 0);
    malformedSlaves[1] = duplicate;
    malformedMaster.insert("slaves", malformedSlaves);
    malformedRoot.insert("master", malformedMaster);
    QVERIFY(!parseProject(QJsonDocument(malformedRoot).toJson(), "Fallback"));
}

void EtherCATProjectTests::testMasterConfigurationPersistenceAndUndo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "master-configuration.ecatproject");
    const Data::ProjectSnapshot source = createProjectSnapshot("Timed Line", "Test");
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    const Data::NodeId master = masterId(document.snapshot());
    QVERIFY(!master.isNull());
    QCOMPARE(document.snapshot().masterConfiguration, Data::MasterConfiguration());

    const int commandCount = document.undoStack()->count();
    QVERIFY(!document.setMasterConfiguration(
        master, {Data::MasterTimingMode::Unassigned, 125000}));
    QVERIFY(!document.setMasterConfiguration(master, {Data::MasterTimingMode::FreeRun, 0}));
    QVERIFY(!document.setMasterConfiguration(
        Data::NodeId::create(), {Data::MasterTimingMode::DistributedClocks, 125000}));
    QVERIFY(!document.setMasterConfiguration(
        master, {static_cast<Data::MasterTimingMode>(99), 125000}));
    QCOMPARE(document.undoStack()->count(), commandCount);

    const Data::MasterConfiguration configuration{
        Data::MasterTimingMode::DistributedClocks, 125000};
    QVERIFY_RESULT(document.setMasterConfiguration(master, configuration));
    QCOMPARE(document.snapshot().masterConfiguration, configuration);
    QVERIFY(document.isModified());

    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterConfiguration, Data::MasterConfiguration());
    QVERIFY(!document.isModified());
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().masterConfiguration, configuration);

    QVERIFY_RESULT(document.save());
    const Utils::Result<QByteArray> savedContents = projectFile.fileContents();
    QVERIFY_RESULT(savedContents);
    const QJsonObject masterObject
        = QJsonDocument::fromJson(*savedContents).object().value("master").toObject();
    const QJsonObject savedConfiguration = masterObject.value("configuration").toObject();
    QCOMPARE(savedConfiguration.value("timingMode").toString(), QString("distributed-clocks"));
    QCOMPARE(savedConfiguration.value("cyclePeriodNs").toInt(), 125000);

    const Utils::Result<LoadedProject> loaded = parseProject(*savedContents, "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->snapshot.masterConfiguration, configuration);
    QVERIFY(!loaded->migrationRequired);
}

void EtherCATProjectTests::testOfflineConfigurationPersistenceAndUndo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "configuration.ecatproject");
    const Data::ProjectSnapshot source = createProjectSnapshot("Configured Line", "Test");
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    const Data::NodeId master = masterId(document.snapshot());
    QList<Data::OfflineSlaveConfiguration> slaves = offlineSlaves(master);
    slaves = {slaves.at(1)};
    QVERIFY_RESULT(document.replaceOfflineSlaves(master, slaves));
    const Data::NodeId slaveId = document.snapshot().slaves.first().id;

    const Data::ProcessDataConfiguration processData = processDataConfiguration();
    const Data::StartupConfiguration startup = startupConfiguration();
    const Data::DcConfiguration dc = dcConfiguration();
    QVERIFY_RESULT(document.setProcessDataConfiguration(slaveId, processData));
    QVERIFY_RESULT(document.setStartupConfiguration(slaveId, startup));
    QVERIFY_RESULT(document.setDcConfiguration(slaveId, dc));
    QCOMPARE(document.snapshot().slaves.first().processData, processData);
    QCOMPARE(document.snapshot().slaves.first().startup, startup);
    QCOMPARE(document.snapshot().slaves.first().dc, dc);

    Data::ProcessDataConfiguration invalidProcessData = processData;
    invalidProcessData.pdos.first().syncManager = 99;
    const int commandCount = document.undoStack()->count();
    QVERIFY(!document.setProcessDataConfiguration(slaveId, invalidProcessData));
    invalidProcessData = processData;
    invalidProcessData.pdos.first().entries.first().requestedBitOffset = (qint64(1) << 53) + 1;
    QVERIFY(!document.setProcessDataConfiguration(slaveId, invalidProcessData));
    QCOMPARE(document.undoStack()->count(), commandCount);

    Data::StartupConfiguration invalidStartup = startup;
    invalidStartup.parameters.first().order = -1;
    QVERIFY(!document.setStartupConfiguration(slaveId, invalidStartup));
    Data::DcConfiguration invalidDc = dc;
    invalidDc.sync0.shiftTimeNs = invalidDc.sync0.cycleTimeNs + 1;
    QVERIFY(!document.setDcConfiguration(slaveId, invalidDc));
    QCOMPARE(document.undoStack()->count(), commandCount);

    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().dc, Data::DcConfiguration());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().startup, Data::StartupConfiguration());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().processData, Data::ProcessDataConfiguration());
    document.undoStack()->redo();
    document.undoStack()->redo();
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().slaves.first().dc, dc);

    QVERIFY_RESULT(document.save());
    const Utils::Result<QByteArray> savedContents = projectFile.fileContents();
    QVERIFY_RESULT(savedContents);
    const QJsonObject root = QJsonDocument::fromJson(*savedContents).object();
    QCOMPARE(root.value("formatVersion").toInt(), 3);
    const QJsonArray savedSlaves = root.value("master").toObject().value("slaves").toArray();
    QVERIFY(savedSlaves.first().toObject().value("configuration").isObject());

    const Utils::Result<LoadedProject> loaded = parseProject(*savedContents, "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->snapshot.slaves, document.snapshot().slaves);
    QVERIFY(!loaded->migrationRequired);
}

void EtherCATProjectTests::testVersionOneConfigurationMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "version-one.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Version One", "Test");
    const Data::NodeId master = masterId(source);
    source.slaves = offlineSlaves(master);
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(source.slaves)) {
        source.nodes.append({slave.id, slave.masterId, Data::ProjectNodeKind::Slave, slave.name});
    }

    QJsonObject root = QJsonDocument::fromJson(serializeProject(source)).object();
    root.insert("formatVersion", 1);
    QJsonObject masterObject = root.value("master").toObject();
    masterObject.remove("configuration");
    QJsonArray slaves = masterObject.value("slaves").toArray();
    for (qsizetype index = 0; index < slaves.size(); ++index) {
        QJsonObject slave = slaves.at(index).toObject();
        slave.remove("configuration");
        slaves[index] = slave;
    }
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);
    const QByteArray versionOneContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionOneContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(document.snapshot().slaves.size(), 2);
    QCOMPARE(document.snapshot().slaves.first().processData, Data::ProcessDataConfiguration());

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v1.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionOneContents);
}

void EtherCATProjectTests::testVersionTwoMasterConfigurationMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "version-two.ecatproject");
    const Data::ProjectSnapshot source = createProjectSnapshot("Version Two", "Test");
    QJsonObject root = QJsonDocument::fromJson(serializeProject(source)).object();
    root.insert("formatVersion", 2);
    QJsonObject masterObject = root.value("master").toObject();
    masterObject.remove("configuration");
    root.insert("master", masterObject);
    const QByteArray versionTwoContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionTwoContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(document.snapshot().masterConfiguration, Data::MasterConfiguration());

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v2.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionTwoContents);
}

void EtherCATProjectTests::testOfflineConfigurationCorruption()
{
    Data::ProjectSnapshot source = createProjectSnapshot("Corruption", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.processData = processDataConfiguration();
    slave.startup = startupConfiguration();
    slave.dc = dcConfiguration();
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});

    const QJsonObject validRoot = QJsonDocument::fromJson(serializeProject(source)).object();

    QJsonObject missingMasterConfigurationRoot = validRoot;
    QJsonObject missingMasterConfiguration
        = missingMasterConfigurationRoot.value("master").toObject();
    missingMasterConfiguration.remove("configuration");
    missingMasterConfigurationRoot.insert("master", missingMasterConfiguration);
    QVERIFY(!parseProject(
        QJsonDocument(missingMasterConfigurationRoot).toJson(), "Fallback"));

    QJsonObject invalidMasterModeRoot = validRoot;
    QJsonObject invalidMasterMode = invalidMasterModeRoot.value("master").toObject();
    QJsonObject invalidMasterModeConfiguration
        = invalidMasterMode.value("configuration").toObject();
    invalidMasterModeConfiguration.insert("timingMode", "external-clock");
    invalidMasterMode.insert("configuration", invalidMasterModeConfiguration);
    invalidMasterModeRoot.insert("master", invalidMasterMode);
    QVERIFY(!parseProject(QJsonDocument(invalidMasterModeRoot).toJson(), "Fallback"));

    QJsonObject inconsistentMasterCycleRoot = validRoot;
    QJsonObject inconsistentMasterCycle
        = inconsistentMasterCycleRoot.value("master").toObject();
    QJsonObject inconsistentMasterConfiguration
        = inconsistentMasterCycle.value("configuration").toObject();
    inconsistentMasterConfiguration.insert("cyclePeriodNs", 125000);
    inconsistentMasterCycle.insert("configuration", inconsistentMasterConfiguration);
    inconsistentMasterCycleRoot.insert("master", inconsistentMasterCycle);
    QVERIFY(!parseProject(
        QJsonDocument(inconsistentMasterCycleRoot).toJson(), "Fallback"));

    QJsonObject missingConfigurationRoot = validRoot;
    QJsonObject missingConfigurationMaster = missingConfigurationRoot.value("master").toObject();
    QJsonArray missingConfigurationSlaves = missingConfigurationMaster.value("slaves").toArray();
    QJsonObject missingConfigurationSlave = missingConfigurationSlaves.first().toObject();
    missingConfigurationSlave.remove("configuration");
    missingConfigurationSlaves[0] = missingConfigurationSlave;
    missingConfigurationMaster.insert("slaves", missingConfigurationSlaves);
    missingConfigurationRoot.insert("master", missingConfigurationMaster);
    QVERIFY(!parseProject(QJsonDocument(missingConfigurationRoot).toJson(), "Fallback"));

    QJsonObject hugeNumberRoot = validRoot;
    QJsonObject hugeNumberMaster = hugeNumberRoot.value("master").toObject();
    QJsonArray hugeNumberSlaves = hugeNumberMaster.value("slaves").toArray();
    QJsonObject hugeNumberSlave = hugeNumberSlaves.first().toObject();
    hugeNumberSlave.insert("vendorId", 1e300);
    hugeNumberSlaves[0] = hugeNumberSlave;
    hugeNumberMaster.insert("slaves", hugeNumberSlaves);
    hugeNumberRoot.insert("master", hugeNumberMaster);
    const Utils::Result<LoadedProject> hugeNumber
        = parseProject(QJsonDocument(hugeNumberRoot).toJson(), "Fallback");
    QVERIFY(!hugeNumber);
    QVERIFY(!hugeNumber.error().isEmpty());

    QJsonObject duplicateIdRoot = validRoot;
    QJsonObject duplicateIdMaster = duplicateIdRoot.value("master").toObject();
    QJsonArray duplicateIdSlaves = duplicateIdMaster.value("slaves").toArray();
    QJsonObject duplicateIdSlave = duplicateIdSlaves.first().toObject();
    QJsonObject duplicateIdConfiguration = duplicateIdSlave.value("configuration").toObject();
    QJsonObject duplicateIdProcessData = duplicateIdConfiguration.value("processData").toObject();
    QJsonArray duplicateIdSyncManagers = duplicateIdProcessData.value("syncManagers").toArray();
    QJsonArray duplicateIdPdos = duplicateIdProcessData.value("pdos").toArray();
    QJsonObject duplicateIdPdo = duplicateIdPdos.first().toObject();
    duplicateIdPdo.insert("id", duplicateIdSyncManagers.first().toObject().value("id"));
    duplicateIdPdos[0] = duplicateIdPdo;
    duplicateIdProcessData.insert("pdos", duplicateIdPdos);
    duplicateIdConfiguration.insert("processData", duplicateIdProcessData);
    duplicateIdSlave.insert("configuration", duplicateIdConfiguration);
    duplicateIdSlaves[0] = duplicateIdSlave;
    duplicateIdMaster.insert("slaves", duplicateIdSlaves);
    duplicateIdRoot.insert("master", duplicateIdMaster);
    const Utils::Result<LoadedProject> duplicateId
        = parseProject(QJsonDocument(duplicateIdRoot).toJson(), "Fallback");
    QVERIFY(!duplicateId);
    QVERIFY(!duplicateId.error().isEmpty());

    QJsonObject invalidHexRoot = validRoot;
    QJsonObject invalidHexMaster = invalidHexRoot.value("master").toObject();
    QJsonArray invalidHexSlaves = invalidHexMaster.value("slaves").toArray();
    QJsonObject invalidHexSlave = invalidHexSlaves.first().toObject();
    QJsonObject invalidHexConfiguration = invalidHexSlave.value("configuration").toObject();
    QJsonObject invalidHexStartup = invalidHexConfiguration.value("startup").toObject();
    QJsonArray invalidHexParameters = invalidHexStartup.value("parameters").toArray();
    QJsonObject invalidHexParameter = invalidHexParameters.first().toObject();
    invalidHexParameter.insert("rawValueHex", "0g");
    invalidHexParameters[0] = invalidHexParameter;
    invalidHexStartup.insert("parameters", invalidHexParameters);
    invalidHexConfiguration.insert("startup", invalidHexStartup);
    invalidHexSlave.insert("configuration", invalidHexConfiguration);
    invalidHexSlaves[0] = invalidHexSlave;
    invalidHexMaster.insert("slaves", invalidHexSlaves);
    invalidHexRoot.insert("master", invalidHexMaster);
    const Utils::Result<LoadedProject> invalidHex
        = parseProject(QJsonDocument(invalidHexRoot).toJson(), "Fallback");
    QVERIFY(!invalidHex);
    QVERIFY(!invalidHex.error().isEmpty());

    QJsonObject root = validRoot;
    QJsonObject masterObject = root.value("master").toObject();
    QJsonArray slaves = masterObject.value("slaves").toArray();
    QJsonObject slaveObject = slaves.first().toObject();
    QJsonObject configuration = slaveObject.value("configuration").toObject();
    QJsonObject processData = configuration.value("processData").toObject();
    QJsonArray pdos = processData.value("pdos").toArray();
    QJsonObject pdo = pdos.first().toObject();
    pdo.insert("syncManager", 99);
    pdos[0] = pdo;
    processData.insert("pdos", pdos);
    configuration.insert("processData", processData);
    slaveObject.insert("configuration", configuration);
    slaves[0] = slaveObject;
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);

    const Utils::Result<LoadedProject> loaded
        = parseProject(QJsonDocument(root).toJson(), "Fallback");
    QVERIFY(!loaded);
    QVERIFY(!loaded.error().isEmpty());
}

void EtherCATProjectTests::testMigrationCreatesRecoveryBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "legacy.ecatproject");
    const Data::NodeId legacyId = Data::NodeId::create();

    QJsonObject legacy;
    legacy.insert("format", "ethercat-project");
    legacy.insert("version", 0);
    legacy.insert("id", legacyId.toString());
    legacy.insert("name", "Legacy Line");
    legacy.insert("createdBy", "Legacy Tool");
    const QByteArray legacyContents = QJsonDocument(legacy).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(legacyContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().valid);
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().id, legacyId);

    QVERIFY_RESULT(document.save());
    QVERIFY(!document.isModified());
    QVERIFY(!document.migrationBackupPath().isEmpty());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v0.bak"));
    QVERIFY(document.migrationBackupPath().exists());
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, legacyContents);

    const Utils::Result<QByteArray> currentContents = projectFile.fileContents();
    QVERIFY_RESULT(currentContents);
    const Utils::Result<LoadedProject> current = parseProject(*currentContents, "Fallback");
    QVERIFY_RESULT(current);
    QCOMPARE(current->snapshot.formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(current->snapshot.id, legacyId);
    QVERIFY(!current->migrationRequired);
}

void EtherCATProjectTests::testProjectExplorerMultiProjectLifecycle()
{
    auto *service = ExtensionSystem::PluginManager::getObject<ProjectServiceImpl>();
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath firstFile = temporaryFilePath(directory, "first.ecatproject");
    const Utils::FilePath secondFile = temporaryFilePath(directory, "second.ecatproject");
    writeProject(firstFile, createProjectSnapshot("First", "Test"));
    writeProject(secondFile, createProjectSnapshot("Second", "Test"));

    QSignalSpy addedSpy(service, &Core::ProjectService::projectAdded);
    QSignalSpy removedSpy(service, &Core::ProjectService::projectAboutToBeRemoved);
    QSignalSpy activeSpy(service, &Core::ProjectService::activeProjectChanged);

    const ProjectExplorer::OpenProjectResult firstResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(firstFile, false);
    QVERIFY2(firstResult, qPrintable(firstResult.errorMessage()));
    auto *firstProject = qobject_cast<EtherCATProject *>(firstResult.project());
    QVERIFY(firstProject);

    const ProjectExplorer::OpenProjectResult secondResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(secondFile, false);
    QVERIFY2(secondResult, qPrintable(secondResult.errorMessage()));
    auto *secondProject = qobject_cast<EtherCATProject *>(secondResult.project());
    QVERIFY(secondProject);

    QCOMPARE(service->projects().size(), 2);
    QCOMPARE(addedSpy.count(), 2);

    ProjectExplorer::ProjectManager::setStartupProject(firstProject);
    QCOMPARE(service->activeProjectId(), firstProject->snapshot().id);

    ProjectExplorer::ProjectManager::setStartupProject(secondProject);
    QCOMPARE(service->activeProjectId(), secondProject->snapshot().id);
    QVERIFY(activeSpy.count() >= 1);

    QVERIFY_RESULT(service->renameProject(secondProject->snapshot().id, "Second Renamed"));
    QCOMPARE(service->project(secondProject->snapshot().id)->name, QString("Second Renamed"));
    QVERIFY(service->canUndoProject(secondProject->snapshot().id));
    QVERIFY(::Core::DocumentManager::modifiedDocuments().contains(secondProject->document()));
    QVERIFY_RESULT(service->undoProject(secondProject->snapshot().id));
    QCOMPARE(service->project(secondProject->snapshot().id)->name, QString("Second"));
    QVERIFY(!::Core::DocumentManager::modifiedDocuments().contains(secondProject->document()));

    const Data::NodeId secondMaster = masterId(secondProject->snapshot());
    QVERIFY_RESULT(service->renameStructuralNode(
        secondProject->snapshot().id, secondMaster, "Motion Master"));
    QCOMPARE(
        nodeName(*service->project(secondProject->snapshot().id), secondMaster),
        QString("Motion Master"));
    QVERIFY_RESULT(service->undoProject(secondProject->snapshot().id));
    QCOMPARE(
        nodeName(*service->project(secondProject->snapshot().id), secondMaster),
        Tr::tr("EtherCAT Master"));

    QVERIFY_RESULT(service->replaceOfflineSlaves(
        secondProject->snapshot().id, secondMaster, offlineSlaves(secondMaster)));
    QCOMPARE(service->project(secondProject->snapshot().id)->slaves.size(), 2);
    const Data::NodeId configuredSlaveId
        = service->project(secondProject->snapshot().id)->slaves.first().id;
    QVERIFY_RESULT(service->setProcessDataConfiguration(
        secondProject->snapshot().id, configuredSlaveId, processDataConfiguration()));
    QCOMPARE(
        service->project(secondProject->snapshot().id)->slaves.first().processData.pdos.size(), 2);
    QVERIFY_RESULT(service->undoProject(secondProject->snapshot().id));
    QVERIFY(
        service->project(secondProject->snapshot().id)->slaves.first().processData.pdos.isEmpty());
    QVERIFY(service->canUndoProject(secondProject->snapshot().id));
    QVERIFY_RESULT(service->undoProject(secondProject->snapshot().id));
    QVERIFY(service->project(secondProject->snapshot().id)->slaves.isEmpty());

    // File watch registration is delivered back from Qt Creator's watcher thread.
    // Drain it before deleting the just-opened projects in this accelerated test.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    ProjectExplorer::ProjectManager::removeProject(secondProject);
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(service->projects().size(), 1);
    QCOMPARE(service->activeProjectId(), firstProject->snapshot().id);

    const Data::NodeId firstProjectId = firstProject->snapshot().id;
    QVERIFY_RESULT(service->renameProject(firstProjectId, "First Closed"));
    QVERIFY(firstProject->document()->isModified());
    ProjectExplorer::ProjectManager::removeProject(firstProject);
    QCOMPARE(removedSpy.count(), 2);
    QVERIFY(service->projects().isEmpty());
    QVERIFY(service->activeProjectId().isNull());

    const Utils::Result<QByteArray> closedContents = firstFile.fileContents();
    QVERIFY_RESULT(closedContents);
    const Utils::Result<LoadedProject> closedProject = parseProject(*closedContents, "Fallback");
    QVERIFY_RESULT(closedProject);
    QCOMPARE(closedProject->snapshot.name, QString("First Closed"));
}

void EtherCATProjectTests::testDuplicateProjectIdCannotOwnStartupContext()
{
    auto *service = ExtensionSystem::PluginManager::getObject<ProjectServiceImpl>();
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());
    QVERIFY(service->activeProjectId().isNull());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath firstFile = temporaryFilePath(directory, "first.ecatproject");
    const Utils::FilePath duplicateFile = temporaryFilePath(directory, "duplicate.ecatproject");

    const Data::ProjectSnapshot firstSnapshot = createProjectSnapshot("First", "Test");
    Data::ProjectSnapshot duplicateSnapshot = firstSnapshot;
    duplicateSnapshot.name = "Duplicate";
    writeProject(firstFile, firstSnapshot);
    writeProject(duplicateFile, duplicateSnapshot);

    const ProjectExplorer::OpenProjectResult firstResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(firstFile, false);
    QVERIFY2(firstResult, qPrintable(firstResult.errorMessage()));
    auto *firstProject = qobject_cast<EtherCATProject *>(firstResult.project());
    QVERIFY(firstProject);

    const ProjectExplorer::OpenProjectResult duplicateResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(duplicateFile, false);
    QVERIFY2(duplicateResult, qPrintable(duplicateResult.errorMessage()));
    auto *duplicateProject = qobject_cast<EtherCATProject *>(duplicateResult.project());
    QVERIFY(duplicateProject);

    QCOMPARE(service->projects().size(), 1);
    QCOMPARE(service->projects().constFirst().id, firstSnapshot.id);
    QVERIFY(service->managesProject(firstProject));
    QVERIFY(!service->managesProject(duplicateProject));

    ProjectExplorer::ProjectManager::setStartupProject(duplicateProject);
    QVERIFY(service->activeProjectId().isNull());

    ProjectExplorer::ProjectManager::setStartupProject(firstProject);
    QCOMPARE(service->activeProjectId(), firstSnapshot.id);

    ProjectExplorer::ProjectManager::removeProject(duplicateProject);
    QCOMPARE(service->projects().size(), 1);
    QCOMPARE(service->activeProjectId(), firstSnapshot.id);

    ProjectExplorer::ProjectManager::removeProject(firstProject);
    QVERIFY(service->projects().isEmpty());
    QVERIFY(service->activeProjectId().isNull());
}

void EtherCATProjectTests::testDuplicateProjectIdOwnerRecovery()
{
    auto *service = ExtensionSystem::PluginManager::getObject<ProjectServiceImpl>();
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());
    QVERIFY(service->activeProjectId().isNull());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath ownerFile = temporaryFilePath(directory, "owner.ecatproject");
    const Utils::FilePath duplicateFile = temporaryFilePath(directory, "recoverable.ecatproject");

    const Data::ProjectSnapshot ownerSnapshot = createProjectSnapshot("Owner", "Test");
    Data::ProjectSnapshot duplicateSnapshot = ownerSnapshot;
    duplicateSnapshot.name = "Recoverable";
    writeProject(ownerFile, ownerSnapshot);
    writeProject(duplicateFile, duplicateSnapshot);

    QSignalSpy taskAddedSpy(
        &ProjectExplorer::taskHub(), &ProjectExplorer::TaskHub::taskAdded);
    QSignalSpy taskRemovedSpy(
        &ProjectExplorer::taskHub(), &ProjectExplorer::TaskHub::taskRemoved);

    const ProjectExplorer::OpenProjectResult ownerResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(ownerFile, false);
    QVERIFY2(ownerResult, qPrintable(ownerResult.errorMessage()));
    QPointer<EtherCATProject> ownerProject
        = qobject_cast<EtherCATProject *>(ownerResult.project());
    QVERIFY(ownerProject);

    const ProjectExplorer::OpenProjectResult duplicateResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(duplicateFile, false);
    QVERIFY2(duplicateResult, qPrintable(duplicateResult.errorMessage()));
    QPointer<EtherCATProject> duplicateProject
        = qobject_cast<EtherCATProject *>(duplicateResult.project());
    QVERIFY(duplicateProject);

    const QScopeGuard cleanup([&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (duplicateProject
            && ProjectExplorer::ProjectManager::hasProject(duplicateProject.data())) {
            ProjectExplorer::ProjectManager::removeProject(duplicateProject.data());
        }
        if (ownerProject && ProjectExplorer::ProjectManager::hasProject(ownerProject.data()))
            ProjectExplorer::ProjectManager::removeProject(ownerProject.data());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });

    QTRY_COMPARE(taskAddedSpy.count(), 1);
    const ProjectExplorer::Task conflictTask
        = qvariant_cast<ProjectExplorer::Task>(taskAddedSpy.constFirst().constFirst());
    QCOMPARE(
        conflictTask.description(),
        Tr::tr("Another open EtherCAT project has the same project ID."));
    QVERIFY(service->managesProject(ownerProject));
    QVERIFY(!service->managesProject(duplicateProject));

    ProjectExplorer::ProjectManager::setStartupProject(duplicateProject);
    QVERIFY(service->activeProjectId().isNull());

    ProjectExplorer::ProjectManager::removeProject(ownerProject);
    QTRY_VERIFY(ownerProject.isNull());
    QTRY_VERIFY(service->managesProject(duplicateProject));
    QCOMPARE(service->projects().size(), 1);
    QCOMPARE(service->projects().constFirst().id, ownerSnapshot.id);
    QTRY_COMPARE(service->activeProjectId(), ownerSnapshot.id);
    QTRY_COMPARE(taskRemovedSpy.count(), 1);
    QCOMPARE(
        qvariant_cast<ProjectExplorer::Task>(taskRemovedSpy.constFirst().constFirst()),
        conflictTask);

    ProjectExplorer::ProjectManager::removeProject(duplicateProject);
    QTRY_VERIFY(duplicateProject.isNull());
    QVERIFY(service->projects().isEmpty());
    QVERIFY(service->activeProjectId().isNull());
}

} // namespace EtherCAT::Project::Internal
