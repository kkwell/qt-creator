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
         {},
         {},
         {},
         {},
         0x1002},
        {Data::NodeId::create(),
         master,
         0,
         {2, 0x1000, 1},
         101,
         7,
         "Mock I/O A",
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         0x1001}};
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

static Data::DeviceAdapterProjectSelection adapterSelection()
{
    return {
        Data::DeviceAdapterId{"com.embedlabs.test.adapter"},
        "1.2.3",
        QByteArray(32, '\x5a'),
        "dc-default",
        {{3, 0x03000001, 0x0300, 0x0030}, {1, 0x01000001, 0x0100, 0x0010}},
    };
}

static Data::SemanticBindingArtifactReference bindingArtifact()
{
    return {
        "binding/com.embedlabs.test/1",
        QByteArray(32, '\x6b'),
        QByteArray(32, '\x7c'),
        {},
    };
}

static Data::SemanticBindingArtifactReference bindingArtifact(
    const QList<Data::OfflineSlaveConfiguration> &slaves)
{
    Data::SemanticBindingArtifactReference reference = bindingArtifact();
    for (const Data::OfflineSlaveConfiguration &slave : slaves) {
        reference.projectDeviceBindings.append(
            {slave.id, QString("embedlabs:test:project-device:%1").arg(slave.position)});
    }
    std::sort(
        reference.projectDeviceBindings.begin(),
        reference.projectDeviceBindings.end(),
        [](const auto &left, const auto &right) {
            return left.slaveId.toString() < right.slaveId.toString();
        });
    return reference;
}

static Data::EngineeringConstraint booleanConstraint()
{
    return {
        Data::ExactRational{0, 1},
        Data::ExactRational{1, 1},
        Data::ExactRational{1, 1},
        Data::ExactRational{0, 1},
        {},
    };
}

static Data::ManualControlEnvelope manualControlEnvelope()
{
    Data::ManualSignalEnvelope output0;
    output0.signalId = {"io.output.0"};
    output0.enabled = true;
    output0.timing.commandTtlMs = 250;
    output0.allowedRange = booleanConstraint();
    output0.safeValue = Data::EngineeringValue::fromBoolean(false);
    output0.consistencyGroupSafeValues = {
        {Data::SemanticSignalId{"io.output.1"}, Data::EngineeringValue::fromBoolean(false)},
    };

    Data::ManualSignalEnvelope output1;
    output1.signalId = {"io.output.1"};
    output1.allowedRange = booleanConstraint();
    output1.safeValue = Data::EngineeringValue::fromBoolean(false);

    Data::ManualActionParameterEnvelope speed;
    speed.parameterId = "speed";
    speed.allowedRange = {
        Data::ExactRational{-1000, 1},
        Data::ExactRational{1000, 1},
        Data::ExactRational{1, 2},
        Data::ExactRational{0, 1},
        {},
    };
    speed.defaultValue
        = Data::EngineeringValue::fromExactRational(Data::ExactRational{1, 2});

    Data::ManualActionEnvelope jog;
    jog.actionId = {"motion.jog"};
    jog.holdToRun = true;
    jog.parameters = {speed};
    jog.releaseActionId = {"motion.stop.release"};
    jog.timeoutActionId = {"motion.stop.timeout"};
    jog.failureActionId = {"motion.stop.failure"};

    return {true, {output0, output1}, {jog}};
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

void EtherCATProjectTests::testStrictProjectJsonShape()
{
    Data::ProjectSnapshot source = createProjectSnapshot("Strict Shape", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.processData = processDataConfiguration();
    slave.startup = startupConfiguration();
    slave.dc = dcConfiguration();
    slave.esiSha256 = QByteArray(32, '\x49');
    slave.adapterSelection = adapterSelection();
    std::sort(
        slave.adapterSelection.moduleAssignments.begin(),
        slave.adapterSelection.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    slave.manualControlEnvelope = manualControlEnvelope();
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    source.masterConfiguration = {Data::MasterTimingMode::DistributedClocks, 125000};
    source.masterBindingArtifact = bindingArtifact(source.slaves);

    const QJsonObject validRoot = QJsonDocument::fromJson(serializeProject(source)).object();
    QVERIFY_RESULT(parseProject(QJsonDocument(validRoot).toJson(), "Fallback"));

    const auto projectWithStationAddress = [&validRoot](const QJsonValue &value, bool remove) {
        QJsonObject mutated = validRoot;
        QJsonObject masterObject = mutated.value("master").toObject();
        QJsonArray slaves = masterObject.value("slaves").toArray();
        QJsonObject slaveObject = slaves.first().toObject();
        if (remove)
            slaveObject.remove("stationAddress");
        else
            slaveObject.insert("stationAddress", value);
        slaves[0] = slaveObject;
        masterObject.insert("slaves", slaves);
        mutated.insert("master", masterObject);
        return mutated;
    };
    for (const QJsonObject &invalidStation :
         {projectWithStationAddress({}, true),
          projectWithStationAddress(-1, false),
          projectWithStationAddress(65536, false),
          projectWithStationAddress(1.5, false),
          projectWithStationAddress("0x1001", false)}) {
        QVERIFY(!parseProject(QJsonDocument(invalidStation).toJson(), "Fallback"));
    }

    const auto injectUnknown = [](auto &&self,
                                  const QJsonValue &value,
                                  const QStringList &path) -> QJsonValue {
        if (path.isEmpty()) {
            QJsonObject object = value.toObject();
            object.insert("ignoredProductionField", true);
            return object;
        }

        const QString head = path.first();
        const QStringList tail = path.sliced(1);
        if (head.startsWith('#')) {
            QJsonArray array = value.toArray();
            bool ok = false;
            const qsizetype index = head.sliced(1).toLongLong(&ok);
            if (ok && index >= 0 && index < array.size())
                array[index] = self(self, array.at(index), tail);
            return array;
        }

        QJsonObject object = value.toObject();
        if (object.contains(head))
            object.insert(head, self(self, object.value(head), tail));
        return object;
    };

    const QList<QStringList> objectPaths{
        {},
        {"project"},
        {"target"},
        {"master"},
        {"master", "configuration"},
        {"master", "semanticBindingArtifact"},
        {"master", "semanticBindingArtifact", "projectDeviceBindings", "#0"},
        {"master", "slaves", "#0"},
        {"master", "slaves", "#0", "configuration"},
        {"master", "slaves", "#0", "configuration", "processData"},
        {"master", "slaves", "#0", "configuration", "processData", "syncManagers", "#0"},
        {"master", "slaves", "#0", "configuration", "processData", "pdos", "#0"},
        {"master", "slaves", "#0", "configuration", "processData", "pdos", "#0", "entries", "#0"},
        {"master", "slaves", "#0", "configuration", "startup"},
        {"master", "slaves", "#0", "configuration", "startup", "parameters", "#0"},
        {"master", "slaves", "#0", "configuration", "dc"},
        {"master", "slaves", "#0", "configuration", "dc", "sync0"},
        {"master", "slaves", "#0", "configuration", "dc", "sync1"},
        {"master", "slaves", "#0", "adapterSelection"},
        {"master", "slaves", "#0", "adapterSelection", "moduleAssignments", "#0"},
        {"master", "slaves", "#0", "manualControlEnvelope"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0", "timing"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0",
         "allowedRange"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0",
         "allowedRange", "minimum"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0",
         "safeValue"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0",
         "consistencyGroupSafeValues", "#0"},
        {"master", "slaves", "#0", "manualControlEnvelope", "signalEnvelopes", "#0",
         "consistencyGroupSafeValues", "#0", "value"},
        {"master", "slaves", "#0", "manualControlEnvelope", "actionEnvelopes", "#0"},
        {"master", "slaves", "#0", "manualControlEnvelope", "actionEnvelopes", "#0", "timing"},
        {"master", "slaves", "#0", "manualControlEnvelope", "actionEnvelopes", "#0",
         "parameters", "#0"},
        {"master", "slaves", "#0", "manualControlEnvelope", "actionEnvelopes", "#0",
         "parameters", "#0", "allowedRange"},
        {"master", "slaves", "#0", "manualControlEnvelope", "actionEnvelopes", "#0",
         "parameters", "#0", "defaultValue"},
        {"master", "slaves", "#0", "manualControlEnvelope", "actionEnvelopes", "#0",
         "parameters", "#0", "defaultValue", "exactRational"},
    };
    for (const QStringList &path : objectPaths) {
        const QJsonValue mutated = injectUnknown(injectUnknown, validRoot, path);
        const Utils::Result<LoadedProject> loaded
            = parseProject(QJsonDocument(mutated.toObject()).toJson(), "Fallback");
        QVERIFY2(!loaded, qPrintable(path.join('/')));
    }

    QByteArray duplicateRoot = serializeProject(source);
    const qsizetype rootStart = duplicateRoot.indexOf('{');
    QVERIFY(rootStart >= 0);
    duplicateRoot.insert(rootStart + 1, "\"f\\u006frmat\":\"ethercat-project\",");
    const Utils::Result<LoadedProject> duplicateRootResult
        = parseProject(duplicateRoot, "Fallback");
    QVERIFY(!duplicateRootResult);
    QVERIFY(duplicateRootResult.error().contains("duplicate", Qt::CaseInsensitive));

    QByteArray duplicateNested = serializeProject(source);
    const QByteArray projectMarker = QByteArrayLiteral("\"project\": {");
    const qsizetype projectStart = duplicateNested.indexOf(projectMarker);
    QVERIFY(projectStart >= 0);
    duplicateNested.insert(
        projectStart + projectMarker.size(), "\"na\\u006de\":\"shadowed\",");
    const Utils::Result<LoadedProject> duplicateNestedResult
        = parseProject(duplicateNested, "Fallback");
    QVERIFY(!duplicateNestedResult);
    QVERIFY(duplicateNestedResult.error().contains("duplicate", Qt::CaseInsensitive));

    QJsonObject legacyRoot{
        {"format", "ethercat-project"},
        {"version", 0},
        {"id", Data::NodeId::create().toString()},
        {"name", "Legacy"},
        {"createdBy", "Legacy Tool"},
        {"ignoredProductionField", true},
    };
    QVERIFY(!parseProject(QJsonDocument(legacyRoot).toJson(), "Fallback"));
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
    QCOMPARE(document.snapshot().slaves.at(0).stationAddress, quint16(0x1001));
    QCOMPARE(document.snapshot().slaves.at(1).stationAddress, quint16(0x1002));
    QCOMPARE(document.snapshot().nodes.size(), 5);

    QList<Data::OfflineSlaveConfiguration> duplicatePositions = slaves;
    duplicatePositions[0].position = 0;
    QVERIFY(!document.replaceOfflineSlaves(master, duplicatePositions));
    QCOMPARE(document.snapshot().slaves.size(), 2);

    QList<Data::OfflineSlaveConfiguration> duplicateStationAddresses = slaves;
    duplicateStationAddresses[0].stationAddress = duplicateStationAddresses[1].stationAddress;
    QVERIFY(!document.replaceOfflineSlaves(master, duplicateStationAddresses));
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

    malformed = QJsonDocument::fromJson(*savedContents);
    malformedRoot = malformed.object();
    malformedMaster = malformedRoot.value("master").toObject();
    malformedSlaves = malformedMaster.value("slaves").toArray();
    duplicate = malformedSlaves.at(1).toObject();
    duplicate.insert(
        "stationAddress",
        malformedSlaves.at(0).toObject().value("stationAddress"));
    malformedSlaves[1] = duplicate;
    malformedMaster.insert("slaves", malformedSlaves);
    malformedRoot.insert("master", malformedMaster);
    QVERIFY(!parseProject(QJsonDocument(malformedRoot).toJson(), "Fallback"));

    malformed = QJsonDocument::fromJson(*savedContents);
    malformedRoot = malformed.object();
    malformedMaster = malformedRoot.value("master").toObject();
    malformedSlaves = malformedMaster.value("slaves").toArray();
    for (qsizetype index = 0; index < malformedSlaves.size(); ++index) {
        QJsonObject unbound = malformedSlaves.at(index).toObject();
        unbound.insert("stationAddress", 0);
        malformedSlaves[index] = unbound;
    }
    malformedMaster.insert("slaves", malformedSlaves);
    malformedRoot.insert("master", malformedMaster);
    const Utils::Result<LoadedProject> unbound
        = parseProject(QJsonDocument(malformedRoot).toJson(), "Fallback");
    QVERIFY_RESULT(unbound);
    for (const Data::OfflineSlaveConfiguration &slave : unbound->snapshot.slaves)
        QCOMPARE(slave.stationAddress, quint16(0));
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
    QCOMPARE(root.value("formatVersion").toInt(), Constants::CURRENT_FORMAT_VERSION);
    const QJsonArray savedSlaves = root.value("master").toObject().value("slaves").toArray();
    QVERIFY(savedSlaves.first().toObject().value("configuration").isObject());

    const Utils::Result<LoadedProject> loaded = parseProject(*savedContents, "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->snapshot.slaves, document.snapshot().slaves);
    QVERIFY(!loaded->migrationRequired);
}

void EtherCATProjectTests::testAdapterSelectionPersistenceAndUndo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "adapter-selection.ecatproject");
    const Data::ProjectSnapshot source = createProjectSnapshot("Adapter Line", "Test");
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    const Data::NodeId master = masterId(document.snapshot());
    QList<Data::OfflineSlaveConfiguration> slaves = {offlineSlaves(master).first()};
    QVERIFY_RESULT(document.replaceOfflineSlaves(master, slaves));
    const Data::NodeId slaveId = document.snapshot().slaves.first().id;
    const QByteArray esiSha256(32, '\x49');

    const int beforeAdapterCommandCount = document.undoStack()->count();
    QVERIFY(!document.setManualControlEnvelope(slaveId, manualControlEnvelope()));
    QCOMPARE(document.undoStack()->count(), beforeAdapterCommandCount);
    QCOMPARE(
        document.snapshot().slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());

    Data::DeviceAdapterProjectSelection selection = adapterSelection();
    QVERIFY_RESULT(document.setDeviceAdapterSelection(slaveId, esiSha256, selection));
    selection.moduleAssignments = {
        {1, 0x01000001, 0x0100, 0x0010},
        {3, 0x03000001, 0x0300, 0x0030},
    };
    QCOMPARE(document.snapshot().slaves.first().adapterSelection, selection);

    const Data::SemanticBindingArtifactReference reference
        = bindingArtifact(document.snapshot().slaves);
    QVERIFY_RESULT(document.setMasterBindingArtifact(reference));
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    const int commandCount = document.undoStack()->count();
    Data::DeviceAdapterProjectSelection partialSelection = selection;
    partialSelection.adapterVersion.clear();
    QVERIFY(!document.setDeviceAdapterSelection(slaveId, esiSha256, partialSelection));
    Data::DeviceAdapterProjectSelection duplicateSlotSelection = selection;
    duplicateSlotSelection.moduleAssignments[1].slot = 1;
    QVERIFY(!document.setDeviceAdapterSelection(slaveId, esiSha256, duplicateSlotSelection));
    Data::DeviceAdapterProjectSelection zeroModuleSelection = selection;
    zeroModuleSelection.moduleAssignments[0].moduleIdent = 0;
    QVERIFY(!document.setDeviceAdapterSelection(slaveId, esiSha256, zeroModuleSelection));
    QVERIFY(!document.setDeviceAdapterSelection(slaveId, QByteArray(31, '\x49'), selection));
    QVERIFY(!document.setDeviceAdapterSelection(Data::NodeId::create(), esiSha256, selection));
    Data::SemanticBindingArtifactReference partialReference = reference;
    partialReference.projectConfigurationSha256.clear();
    QVERIFY(!document.setMasterBindingArtifact(partialReference));
    Data::SemanticBindingArtifactReference unknownSlaveReference = reference;
    unknownSlaveReference.projectDeviceBindings.first().slaveId = Data::NodeId::create();
    QVERIFY(!document.setMasterBindingArtifact(unknownSlaveReference));
    Data::SemanticBindingArtifactReference untrimmedDeviceReference = reference;
    untrimmedDeviceReference.projectDeviceBindings.first().projectDeviceId.append(u' ');
    QVERIFY(!document.setMasterBindingArtifact(untrimmedDeviceReference));
    QCOMPARE(document.undoStack()->count(), commandCount);
    QCOMPARE(document.snapshot().slaves.first().adapterSelection, selection);
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    document.undoStack()->undo();
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(
        document.snapshot().slaves.first().adapterSelection,
        Data::DeviceAdapterProjectSelection());
    document.undoStack()->redo();
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().slaves.first().adapterSelection, selection);
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    QVERIFY_RESULT(document.save());
    EtherCATProjectDocument reloaded;
    QVERIFY_RESULT(reloaded.load(projectFile));
    QCOMPARE(reloaded.snapshot().slaves.first().esiSha256, esiSha256);
    QCOMPARE(reloaded.snapshot().slaves.first().adapterSelection, selection);
    QCOMPARE(reloaded.snapshot().masterBindingArtifact, reference);
    QVERIFY_RESULT(reloaded.setDeviceAdapterSelection(
        slaveId, esiSha256, Data::DeviceAdapterProjectSelection()));
    QCOMPARE(reloaded.snapshot().slaves.first().esiSha256, esiSha256);
    QCOMPARE(
        reloaded.snapshot().slaves.first().adapterSelection,
        Data::DeviceAdapterProjectSelection());
    reloaded.undoStack()->undo();
    QCOMPARE(reloaded.snapshot().slaves.first().adapterSelection, selection);

    const Utils::Result<QByteArray> savedContents = projectFile.fileContents();
    QVERIFY_RESULT(savedContents);
    const QByteArray lowerHex = QByteArray(32, '\x49').toHex();
    QVERIFY(savedContents->contains(lowerHex));
    QVERIFY(!savedContents->contains("runtimeResourceId"));
    QVERIFY(!savedContents->contains("processImageBitOffset"));
    QVERIFY(!savedContents->contains("runtimeGeneration"));
}

void EtherCATProjectTests::testManualControlEnvelopePersistenceAndUndo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "manual-control-envelope.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Manual Control", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.esiSha256 = QByteArray(32, '\x49');
    slave.adapterSelection = adapterSelection();
    std::sort(
        slave.adapterSelection.moduleAssignments.begin(),
        slave.adapterSelection.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    source.masterBindingArtifact = bindingArtifact();
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    const Data::ManualControlEnvelope envelope = manualControlEnvelope();
    QVERIFY_RESULT(document.setManualControlEnvelope(slave.id, envelope));
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    QCOMPARE(document.undoStack()->undoText(), Tr::tr("Configure EtherCAT manual control"));

    document.undoStack()->undo();
    QCOMPARE(
        document.snapshot().slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());

    QVERIFY_RESULT(document.setMasterBindingArtifact(bindingArtifact()));
    QVERIFY_RESULT(document.renameProject("Manual Control Display"));
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());
    document.undoStack()->undo();

    QVERIFY_RESULT(document.renameStructuralNode(master, "Manual Master"));
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());
    document.undoStack()->undo();

    QVERIFY_RESULT(document.setMasterConfiguration(
        master, {Data::MasterTimingMode::DistributedClocks, 125000}));
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    const Utils::Result<> processDataResult
        = document.setProcessDataConfiguration(slave.id, processDataConfiguration());
    QVERIFY_RESULT(processDataResult);
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    QCOMPARE(document.undoStack()->undoText(), Tr::tr("Configure EtherCAT Process Data"));
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.artifactId,
        bindingArtifact().artifactId);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.artifactSha256,
        bindingArtifact().artifactSha256);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.projectConfigurationSha256,
        bindingArtifact().projectConfigurationSha256);

    const Utils::Result<> startupResult
        = document.setStartupConfiguration(slave.id, startupConfiguration());
    QVERIFY_RESULT(startupResult);
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    const Utils::Result<> dcResult = document.setDcConfiguration(slave.id, dcConfiguration());
    QVERIFY_RESULT(dcResult);
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    Data::ManualControlEnvelope invalidOrder = envelope;
    std::reverse(
        invalidOrder.signalEnvelopes.begin(), invalidOrder.signalEnvelopes.end());
    const int commandCount = document.undoStack()->count();
    const int commandIndex = document.undoStack()->index();
    QVERIFY(!document.setManualControlEnvelope(slave.id, invalidOrder));
    QCOMPARE(document.undoStack()->count(), commandCount);
    QCOMPARE(document.undoStack()->index(), commandIndex);
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    Data::DeviceAdapterProjectSelection reorderedSelection = slave.adapterSelection;
    std::reverse(
        reorderedSelection.moduleAssignments.begin(),
        reorderedSelection.moduleAssignments.end());
    const int beforeReorderedCount = document.undoStack()->count();
    const int beforeReorderedIndex = document.undoStack()->index();
    const Utils::Result<> reorderedResult = document.setDeviceAdapterSelection(
        slave.id, slave.esiSha256, reorderedSelection);
    QVERIFY_RESULT(reorderedResult);
    QCOMPARE(document.undoStack()->count(), beforeReorderedCount);
    QCOMPARE(document.undoStack()->index(), beforeReorderedIndex);
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    Data::DeviceAdapterProjectSelection changedSelection = slave.adapterSelection;
    changedSelection.processDataProfileId = "dc-alternate";
    QVERIFY_RESULT(document.setDeviceAdapterSelection(
        slave.id, slave.esiSha256, changedSelection));
    QCOMPARE(
        document.snapshot().slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    QVERIFY_RESULT(document.setDeviceAdapterSelection(
        slave.id, QByteArray(32, '\x50'), slave.adapterSelection));
    QCOMPARE(
        document.snapshot().slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    Data::DeviceAdapterProjectSelection changedModules = slave.adapterSelection;
    changedModules.moduleAssignments[0].moduleIdent += 1;
    QVERIFY_RESULT(document.setDeviceAdapterSelection(
        slave.id, slave.esiSha256, changedModules));
    QCOMPARE(
        document.snapshot().slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(document.snapshot().masterBindingArtifact, bindingArtifact());

    QVERIFY_RESULT(document.save());
    EtherCATProjectDocument reloaded;
    QVERIFY_RESULT(reloaded.load(projectFile));
    QCOMPARE(reloaded.snapshot().slaves.first().manualControlEnvelope, envelope);
    QCOMPARE(reloaded.snapshot().masterBindingArtifact, bindingArtifact());

    const Utils::Result<QByteArray> savedContents = projectFile.fileContents();
    QVERIFY_RESULT(savedContents);
    const QJsonObject root = QJsonDocument::fromJson(*savedContents).object();
    QCOMPARE(root.value("formatVersion").toInt(), Constants::CURRENT_FORMAT_VERSION);
    const QJsonObject savedSlave
        = root.value("master").toObject().value("slaves").toArray().first().toObject();
    QVERIFY(savedSlave.value("manualControlEnvelope").isObject());
    const QJsonObject savedEnvelope = savedSlave.value("manualControlEnvelope").toObject();
    const QJsonObject firstRange = savedEnvelope.value("signalEnvelopes")
                                       .toArray()
                                       .first()
                                       .toObject()
                                       .value("allowedRange")
                                       .toObject();
    QCOMPARE(
        firstRange.value("minimum").toObject().value("numerator").toString(),
        QString("0"));
    QVERIFY(firstRange.value("minimum").toObject().value("numerator").isString());
    QCOMPARE(
        firstRange.value("minimum").toObject().value("denominator").toString(),
        QString("1"));
}

void EtherCATProjectTests::testBindingArtifactInvalidationAndUndo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "binding-invalidation.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Binding Guard", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.esiSha256 = QByteArray(32, '\x49');
    slave.adapterSelection = adapterSelection();
    std::sort(
        slave.adapterSelection.moduleAssignments.begin(),
        slave.adapterSelection.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    slave.manualControlEnvelope = manualControlEnvelope();
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    const Data::SemanticBindingArtifactReference reference = bindingArtifact(source.slaves);
    source.masterBindingArtifact = reference;
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    QVERIFY_RESULT(document.renameProject("Display Name Only"));
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    const Utils::Result<> renameMasterResult
        = document.renameStructuralNode(master, "Display Master Only");
    QVERIFY_RESULT(renameMasterResult);
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    Data::OfflineSlaveConfiguration renamedSlave = slave;
    renamedSlave.name = "Display Slave Only";
    QVERIFY_RESULT(document.replaceOfflineSlaves(master, {renamedSlave}));
    QCOMPARE(document.snapshot().slaves.first().manualControlEnvelope, manualControlEnvelope());
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first(), slave);
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    Data::OfflineSlaveConfiguration changedStation = slave;
    ++changedStation.stationAddress;
    QVERIFY_RESULT(document.replaceOfflineSlaves(master, {changedStation}));
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().slaves.first(), slave);
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    QVERIFY_RESULT(document.setMasterConfiguration(
        master, {Data::MasterTimingMode::DistributedClocks, 125000}));
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    QCOMPARE(document.undoStack()->undoText(), Tr::tr("Configure EtherCAT master cycle"));
    document.undoStack()->undo();
    QCOMPARE(
        document.snapshot().masterBindingArtifact.artifactId,
        reference.artifactId);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.artifactSha256,
        reference.artifactSha256);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.projectConfigurationSha256,
        reference.projectConfigurationSha256);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.projectDeviceBindings,
        reference.projectDeviceBindings);
    QCOMPARE(document.snapshot().masterConfiguration, Data::MasterConfiguration());

    const Data::ProcessDataConfiguration changedProcessData = processDataConfiguration();
    const Utils::Result<> processDataResult
        = document.setProcessDataConfiguration(slave.id, changedProcessData);
    QVERIFY_RESULT(processDataResult);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    QCOMPARE(document.undoStack()->undoText(), Tr::tr("Configure EtherCAT Process Data"));
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);
    QCOMPARE(
        document.snapshot().slaves.first().processData,
        Data::ProcessDataConfiguration());

    const Data::StartupConfiguration changedStartup = startupConfiguration();
    const Utils::Result<> startupResult
        = document.setStartupConfiguration(slave.id, changedStartup);
    QVERIFY_RESULT(startupResult);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    const Utils::Result<> dcResult = document.setDcConfiguration(slave.id, dcConfiguration());
    QVERIFY_RESULT(dcResult);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    Data::DeviceAdapterProjectSelection changedSelection = slave.adapterSelection;
    changedSelection.adapterVersion = "1.2.4";
    const Utils::Result<> adapterResult
        = document.setDeviceAdapterSelection(slave.id, slave.esiSha256, changedSelection);
    QVERIFY_RESULT(adapterResult);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);
    QCOMPARE(document.snapshot().slaves.first().adapterSelection, slave.adapterSelection);

    Data::OfflineSlaveConfiguration changedSlave = slave;
    changedSlave.alias = 42;
    const Utils::Result<> replaceResult = document.replaceOfflineSlaves(master, {changedSlave});
    QVERIFY_RESULT(replaceResult);
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->undo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);
    QCOMPARE(document.snapshot().slaves.first(), slave);
    QVERIFY(!document.isModified());
}

void EtherCATProjectTests::testProjectDeviceBindingPersistenceAndValidation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "project-device-bindings.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Device Identities", "Test");
    const Data::NodeId master = masterId(source);
    source.slaves = offlineSlaves(master);
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(source.slaves))
        source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    QVERIFY_RESULT(projectFile.writeFileContents(serializeProject(source)));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    const Data::SemanticBindingArtifactReference reference = bindingArtifact(source.slaves);
    QCOMPARE(reference.projectDeviceBindings.size(), 2);
    QVERIFY_RESULT(document.setMasterBindingArtifact(reference));
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    document.undoStack()->undo();
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    document.undoStack()->redo();
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    const int commandCount = document.undoStack()->count();
    Data::SemanticBindingArtifactReference unsorted = reference;
    std::reverse(
        unsorted.projectDeviceBindings.begin(), unsorted.projectDeviceBindings.end());
    QVERIFY(!document.setMasterBindingArtifact(unsorted));

    Data::SemanticBindingArtifactReference duplicateSlave = reference;
    duplicateSlave.projectDeviceBindings[1].slaveId
        = duplicateSlave.projectDeviceBindings[0].slaveId;
    QVERIFY(!document.setMasterBindingArtifact(duplicateSlave));

    Data::SemanticBindingArtifactReference duplicateProjectDevice = reference;
    duplicateProjectDevice.projectDeviceBindings[1].projectDeviceId
        = duplicateProjectDevice.projectDeviceBindings[0].projectDeviceId;
    QVERIFY(!document.setMasterBindingArtifact(duplicateProjectDevice));

    Data::SemanticBindingArtifactReference nullSlave = reference;
    nullSlave.projectDeviceBindings[0].slaveId = {};
    QVERIFY(!document.setMasterBindingArtifact(nullSlave));

    Data::SemanticBindingArtifactReference unknownSlave = reference;
    unknownSlave.projectDeviceBindings[0].slaveId = Data::NodeId::create();
    std::sort(
        unknownSlave.projectDeviceBindings.begin(),
        unknownSlave.projectDeviceBindings.end(),
        [](const auto &left, const auto &right) {
            return left.slaveId.toString() < right.slaveId.toString();
        });
    QVERIFY(!document.setMasterBindingArtifact(unknownSlave));

    Data::SemanticBindingArtifactReference emptyProjectDevice = reference;
    emptyProjectDevice.projectDeviceBindings[0].projectDeviceId.clear();
    QVERIFY(!document.setMasterBindingArtifact(emptyProjectDevice));

    Data::SemanticBindingArtifactReference untrimmedProjectDevice = reference;
    untrimmedProjectDevice.projectDeviceBindings[0].projectDeviceId.prepend(u' ');
    QVERIFY(!document.setMasterBindingArtifact(untrimmedProjectDevice));

    Data::SemanticBindingArtifactReference mappingWithoutArtifact;
    mappingWithoutArtifact.projectDeviceBindings = reference.projectDeviceBindings;
    QVERIFY(!document.setMasterBindingArtifact(mappingWithoutArtifact));
    QCOMPARE(document.undoStack()->count(), commandCount);
    QCOMPARE(document.snapshot().masterBindingArtifact, reference);

    QVERIFY_RESULT(document.save());
    const Utils::Result<QByteArray> contents = projectFile.fileContents();
    QVERIFY_RESULT(contents);
    const QJsonObject root = QJsonDocument::fromJson(*contents).object();
    const QJsonArray bindings = root.value("master")
                                    .toObject()
                                    .value("semanticBindingArtifact")
                                    .toObject()
                                    .value("projectDeviceBindings")
                                    .toArray();
    QCOMPARE(bindings.size(), 2);
    const QString firstSlaveId = bindings[0].toObject().value("slaveId").toString();
    const QString secondSlaveId = bindings[1].toObject().value("slaveId").toString();
    QVERIFY(firstSlaveId < secondSlaveId);

    EtherCATProjectDocument reloaded;
    QVERIFY_RESULT(reloaded.load(projectFile));
    QCOMPARE(reloaded.snapshot().masterBindingArtifact, reference);

    const Data::SemanticBindingArtifactReference legacyReference = bindingArtifact();
    QVERIFY_RESULT(reloaded.setMasterBindingArtifact(legacyReference));
    QCOMPARE(reloaded.snapshot().masterBindingArtifact, legacyReference);
    reloaded.undoStack()->undo();
    QCOMPARE(reloaded.snapshot().masterBindingArtifact, reference);
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
        slave.remove("manualControlEnvelope");
        slave.remove("stationAddress");
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
    QVERIFY(document.snapshot().slaves.first().esiSha256.isEmpty());
    QCOMPARE(
        document.snapshot().slaves.first().adapterSelection,
        Data::DeviceAdapterProjectSelection());
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());

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
    QJsonArray slaves = masterObject.value("slaves").toArray();
    for (qsizetype index = 0; index < slaves.size(); ++index) {
        QJsonObject slave = slaves.at(index).toObject();
        slave.remove("manualControlEnvelope");
        slave.remove("stationAddress");
        slaves[index] = slave;
    }
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);
    const QByteArray versionTwoContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionTwoContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(document.snapshot().masterConfiguration, Data::MasterConfiguration());
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v2.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionTwoContents);
}

void EtherCATProjectTests::testVersionThreeAdapterMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile = temporaryFilePath(directory, "version-three.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Version Three", "Test");
    source.masterConfiguration = {Data::MasterTimingMode::DistributedClocks, 125000};
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.processData = processDataConfiguration();
    slave.startup = startupConfiguration();
    slave.dc = dcConfiguration();
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});

    QJsonObject root = QJsonDocument::fromJson(serializeProject(source)).object();
    root.insert("formatVersion", 3);
    QJsonObject masterObject = root.value("master").toObject();
    QJsonArray slaves = masterObject.value("slaves").toArray();
    for (qsizetype index = 0; index < slaves.size(); ++index) {
        QJsonObject slaveObject = slaves.at(index).toObject();
        slaveObject.remove("manualControlEnvelope");
        slaveObject.remove("stationAddress");
        slaves[index] = slaveObject;
    }
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);
    const QByteArray versionThreeContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionThreeContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().masterConfiguration, source.masterConfiguration);
    QCOMPARE(document.snapshot().slaves.first().processData, slave.processData);
    QCOMPARE(document.snapshot().slaves.first().startup, slave.startup);
    QCOMPARE(document.snapshot().slaves.first().dc, slave.dc);
    QVERIFY(document.snapshot().slaves.first().esiSha256.isEmpty());
    QCOMPARE(
        document.snapshot().slaves.first().adapterSelection,
        Data::DeviceAdapterProjectSelection());
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v3.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionThreeContents);
}

void EtherCATProjectTests::testVersionFourManualControlMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "version-four.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Version Four", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.esiSha256 = QByteArray(32, '\x49');
    slave.adapterSelection = adapterSelection();
    std::sort(
        slave.adapterSelection.moduleAssignments.begin(),
        slave.adapterSelection.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    slave.manualControlEnvelope = manualControlEnvelope();
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    source.masterBindingArtifact = bindingArtifact();

    QJsonObject root = QJsonDocument::fromJson(serializeProject(source)).object();
    root.insert("formatVersion", 4);
    QJsonObject masterObject = root.value("master").toObject();
    QJsonObject referenceObject
        = masterObject.value("semanticBindingArtifact").toObject();
    referenceObject.remove("projectDeviceBindings");
    masterObject.insert("semanticBindingArtifact", referenceObject);
    QJsonArray slaves = masterObject.value("slaves").toArray();
    QJsonObject versionFourSlave = slaves.first().toObject();
    versionFourSlave.remove("manualControlEnvelope");
    versionFourSlave.remove("stationAddress");
    slaves[0] = versionFourSlave;
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);
    const QByteArray versionFourContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionFourContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(document.snapshot().slaves.first().esiSha256, slave.esiSha256);
    QCOMPARE(
        document.snapshot().slaves.first().adapterSelection,
        slave.adapterSelection);
    QCOMPARE(
        document.snapshot().slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());
    QCOMPARE(
        document.snapshot().masterBindingArtifact,
        Data::SemanticBindingArtifactReference());

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v4.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionFourContents);
    const Utils::Result<LoadedProject> current
        = parseProject(*projectFile.fileContents(), "Fallback");
    QVERIFY_RESULT(current);
    QCOMPARE(current->snapshot.formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(
        current->snapshot.slaves.first().manualControlEnvelope,
        Data::ManualControlEnvelope());
    QCOMPARE(
        current->snapshot.masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
}

void EtherCATProjectTests::testVersionFiveBindingArtifactMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "version-five.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Version Five", "Test");
    const Data::NodeId master = masterId(source);
    source.slaves = {offlineSlaves(master).first()};
    source.nodes.append(
        {source.slaves.first().id, master, Data::ProjectNodeKind::Slave, source.slaves.first().name});
    source.masterBindingArtifact = bindingArtifact(source.slaves);

    QJsonObject root = QJsonDocument::fromJson(serializeProject(source)).object();
    root.insert("formatVersion", 5);
    QJsonObject masterObject = root.value("master").toObject();
    QJsonObject referenceObject
        = masterObject.value("semanticBindingArtifact").toObject();
    referenceObject.remove("projectDeviceBindings");
    masterObject.insert("semanticBindingArtifact", referenceObject);
    QJsonArray slaves = masterObject.value("slaves").toArray();
    for (qsizetype index = 0; index < slaves.size(); ++index) {
        QJsonObject slaveObject = slaves.at(index).toObject();
        slaveObject.remove("stationAddress");
        slaves[index] = slaveObject;
    }
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);
    const QByteArray versionFiveContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionFiveContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.artifactId,
        source.masterBindingArtifact.artifactId);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.artifactSha256,
        source.masterBindingArtifact.artifactSha256);
    QCOMPARE(
        document.snapshot().masterBindingArtifact.projectConfigurationSha256,
        source.masterBindingArtifact.projectConfigurationSha256);
    QVERIFY(document.snapshot().masterBindingArtifact.projectDeviceBindings.isEmpty());

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v5.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionFiveContents);
    const QJsonObject savedRoot
        = QJsonDocument::fromJson(*projectFile.fileContents()).object();
    const QJsonObject savedReference = savedRoot.value("master")
                                           .toObject()
                                           .value("semanticBindingArtifact")
                                           .toObject();
    QVERIFY(savedReference.value("projectDeviceBindings").isArray());
    QVERIFY(savedReference.value("projectDeviceBindings").toArray().isEmpty());
}

void EtherCATProjectTests::testVersionSixStationAddressMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "version-six.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Version Six", "Test");
    const Data::NodeId master = masterId(source);
    source.slaves = offlineSlaves(master);
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(source.slaves))
        source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    source.masterBindingArtifact = bindingArtifact(source.slaves);

    QJsonObject root = QJsonDocument::fromJson(serializeProject(source)).object();
    root.insert("formatVersion", 6);
    QJsonObject masterObject = root.value("master").toObject();
    QJsonArray slaves = masterObject.value("slaves").toArray();
    for (qsizetype index = 0; index < slaves.size(); ++index) {
        QJsonObject slaveObject = slaves.at(index).toObject();
        slaveObject.remove("stationAddress");
        slaves[index] = slaveObject;
    }
    masterObject.insert("slaves", slaves);
    root.insert("master", masterObject);
    const QByteArray versionSixContents = QJsonDocument(root).toJson();
    QVERIFY_RESULT(projectFile.writeFileContents(versionSixContents));

    EtherCATProjectDocument document;
    QVERIFY_RESULT(document.load(projectFile));
    QVERIFY(document.snapshot().migrated);
    QVERIFY(document.isModified());
    QCOMPARE(document.snapshot().formatVersion, Constants::CURRENT_FORMAT_VERSION);
    QCOMPARE(document.snapshot().slaves.size(), 2);
    for (const Data::OfflineSlaveConfiguration &slave : document.snapshot().slaves)
        QCOMPARE(slave.stationAddress, quint16(0));

    QVERIFY_RESULT(document.save());
    QVERIFY(document.migrationBackupPath().toUrlishString().contains(".v6.bak"));
    const Utils::Result<QByteArray> backup = document.migrationBackupPath().fileContents();
    QVERIFY_RESULT(backup);
    QCOMPARE(*backup, versionSixContents);

    const QJsonArray savedSlaves = QJsonDocument::fromJson(*projectFile.fileContents())
                                       .object()
                                       .value("master")
                                       .toObject()
                                       .value("slaves")
                                       .toArray();
    QCOMPARE(savedSlaves.size(), 2);
    for (const QJsonValue &slave : savedSlaves)
        QCOMPARE(slave.toObject().value("stationAddress").toInt(-1), 0);
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

void EtherCATProjectTests::testAdapterSelectionCorruption()
{
    Data::ProjectSnapshot source = createProjectSnapshot("Adapter Corruption", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.esiSha256 = QByteArray(32, '\x49');
    slave.adapterSelection = adapterSelection();
    std::sort(
        slave.adapterSelection.moduleAssignments.begin(),
        slave.adapterSelection.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    source.masterBindingArtifact = bindingArtifact(source.slaves);

    const QJsonObject validRoot = QJsonDocument::fromJson(serializeProject(source)).object();
    QVERIFY_RESULT(parseProject(QJsonDocument(validRoot).toJson(), "Fallback"));

    const auto rejectSlaveMutation = [&validRoot](const auto &mutate) {
        QJsonObject root = validRoot;
        QJsonObject masterObject = root.value("master").toObject();
        QJsonArray slaves = masterObject.value("slaves").toArray();
        QJsonObject slaveObject = slaves.first().toObject();
        mutate(slaveObject);
        slaves[0] = slaveObject;
        masterObject.insert("slaves", slaves);
        root.insert("master", masterObject);
        return parseProject(QJsonDocument(root).toJson(), "Fallback");
    };

    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        object.insert("esiSha256", QString(64, u'A'));
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        object.insert("esiSha256", QString(62, u'a'));
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        object.remove("esiSha256");
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        QJsonObject selection = object.value("adapterSelection").toObject();
        selection.remove("adapterVersion");
        object.insert("adapterSelection", selection);
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        QJsonObject selection = object.value("adapterSelection").toObject();
        selection.insert("runtimeResourceId", "0000000000000001");
        object.insert("adapterSelection", selection);
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        QJsonObject selection = object.value("adapterSelection").toObject();
        QJsonArray modules = selection.value("moduleAssignments").toArray();
        QJsonObject module = modules.first().toObject();
        module.insert("slot", -1);
        modules[0] = module;
        selection.insert("moduleAssignments", modules);
        object.insert("adapterSelection", selection);
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        QJsonObject selection = object.value("adapterSelection").toObject();
        QJsonArray modules = selection.value("moduleAssignments").toArray();
        QJsonObject module = modules.at(1).toObject();
        module.insert("slot", modules.first().toObject().value("slot"));
        modules[1] = module;
        selection.insert("moduleAssignments", modules);
        object.insert("adapterSelection", selection);
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        QJsonObject selection = object.value("adapterSelection").toObject();
        QJsonArray modules = selection.value("moduleAssignments").toArray();
        QJsonObject module = modules.first().toObject();
        module.insert("moduleIdent", 0);
        modules[0] = module;
        selection.insert("moduleAssignments", modules);
        object.insert("adapterSelection", selection);
    }));
    QVERIFY(!rejectSlaveMutation([](QJsonObject &object) {
        QJsonObject selection = object.value("adapterSelection").toObject();
        QJsonArray modules = selection.value("moduleAssignments").toArray();
        QJsonObject module = modules.first().toObject();
        module.insert("objectIndexOffset", 65536);
        modules[0] = module;
        selection.insert("moduleAssignments", modules);
        object.insert("adapterSelection", selection);
    }));

    const auto rejectArtifactMutation = [&validRoot](const auto &mutate) {
        QJsonObject root = validRoot;
        QJsonObject masterObject = root.value("master").toObject();
        QJsonObject reference = masterObject.value("semanticBindingArtifact").toObject();
        mutate(reference);
        masterObject.insert("semanticBindingArtifact", reference);
        root.insert("master", masterObject);
        return parseProject(QJsonDocument(root).toJson(), "Fallback");
    };
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        reference.remove("projectConfigurationSha256");
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        reference.insert("artifactSha256", QString(64, u'F'));
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        reference.insert("runtimeGeneration", 1);
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        reference.remove("projectDeviceBindings");
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        QJsonArray bindings = reference.value("projectDeviceBindings").toArray();
        QJsonObject binding = bindings.first().toObject();
        binding.insert("slaveId", Data::NodeId::create().toString());
        bindings[0] = binding;
        reference.insert("projectDeviceBindings", bindings);
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        QJsonArray bindings = reference.value("projectDeviceBindings").toArray();
        QJsonObject binding = bindings.first().toObject();
        binding.insert("projectDeviceId", " invalid");
        bindings[0] = binding;
        reference.insert("projectDeviceBindings", bindings);
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        QJsonArray bindings = reference.value("projectDeviceBindings").toArray();
        bindings.append(bindings.first());
        reference.insert("projectDeviceBindings", bindings);
    }));
    QVERIFY(!rejectArtifactMutation([](QJsonObject &reference) {
        QJsonArray bindings = reference.value("projectDeviceBindings").toArray();
        QJsonObject binding = bindings.first().toObject();
        binding.insert("position", 0);
        bindings[0] = binding;
        reference.insert("projectDeviceBindings", bindings);
    }));
}

void EtherCATProjectTests::testManualControlEnvelopeCorruption()
{
    Data::ProjectSnapshot source = createProjectSnapshot("Manual Corruption", "Test");
    const Data::NodeId master = masterId(source);
    Data::OfflineSlaveConfiguration slave = offlineSlaves(master).first();
    slave.esiSha256 = QByteArray(32, '\x49');
    slave.adapterSelection = adapterSelection();
    std::sort(
        slave.adapterSelection.moduleAssignments.begin(),
        slave.adapterSelection.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    slave.manualControlEnvelope = manualControlEnvelope();
    source.slaves = {slave};
    source.nodes.append({slave.id, master, Data::ProjectNodeKind::Slave, slave.name});
    const QJsonObject validRoot = QJsonDocument::fromJson(serializeProject(source)).object();
    QVERIFY_RESULT(parseProject(QJsonDocument(validRoot).toJson(), "Fallback"));

    const auto rejectEnvelopeMutation = [&validRoot](const auto &mutate) {
        QJsonObject root = validRoot;
        QJsonObject masterObject = root.value("master").toObject();
        QJsonArray slaves = masterObject.value("slaves").toArray();
        QJsonObject slaveObject = slaves.first().toObject();
        QJsonObject envelope = slaveObject.value("manualControlEnvelope").toObject();
        mutate(envelope);
        slaveObject.insert("manualControlEnvelope", envelope);
        slaves[0] = slaveObject;
        masterObject.insert("slaves", slaves);
        root.insert("master", masterObject);
        return parseProject(QJsonDocument(root).toJson(), "Fallback");
    };

    QVERIFY(!rejectEnvelopeMutation([](QJsonObject &envelope) {
        envelope.insert("runtimeResourceId", "1");
    }));
    QVERIFY(!rejectEnvelopeMutation([](QJsonObject &envelope) {
        QJsonArray signalArray = envelope.value("signalEnvelopes").toArray();
        const QJsonValue first = signalArray.at(0);
        signalArray[0] = signalArray.at(1);
        signalArray[1] = first;
        envelope.insert("signalEnvelopes", signalArray);
    }));
    QVERIFY(!rejectEnvelopeMutation([](QJsonObject &envelope) {
        QJsonArray signalArray = envelope.value("signalEnvelopes").toArray();
        QJsonObject signal = signalArray.first().toObject();
        QJsonObject range = signal.value("allowedRange").toObject();
        QJsonObject minimum = range.value("minimum").toObject();
        minimum.insert("numerator", "00");
        range.insert("minimum", minimum);
        signal.insert("allowedRange", range);
        signalArray[0] = signal;
        envelope.insert("signalEnvelopes", signalArray);
    }));
    QVERIFY(!rejectEnvelopeMutation([](QJsonObject &envelope) {
        QJsonArray actions = envelope.value("actionEnvelopes").toArray();
        QJsonObject action = actions.first().toObject();
        QJsonArray parameters = action.value("parameters").toArray();
        QJsonObject parameter = parameters.first().toObject();
        QJsonObject range = parameter.value("allowedRange").toObject();
        QJsonObject step = range.value("step").toObject();
        step.insert("denominator", 2);
        range.insert("step", step);
        parameter.insert("allowedRange", range);
        parameters[0] = parameter;
        action.insert("parameters", parameters);
        actions[0] = action;
        envelope.insert("actionEnvelopes", actions);
    }));
    QVERIFY(!rejectEnvelopeMutation([](QJsonObject &envelope) {
        QJsonArray actions = envelope.value("actionEnvelopes").toArray();
        QJsonObject action = actions.first().toObject();
        action.remove("failureActionId");
        actions[0] = action;
        envelope.insert("actionEnvelopes", actions);
    }));

    QJsonObject missingRoot = validRoot;
    QJsonObject masterObject = missingRoot.value("master").toObject();
    QJsonArray slaves = masterObject.value("slaves").toArray();
    QJsonObject slaveObject = slaves.first().toObject();
    slaveObject.remove("manualControlEnvelope");
    slaves[0] = slaveObject;
    masterObject.insert("slaves", slaves);
    missingRoot.insert("master", masterObject);
    QVERIFY(!parseProject(QJsonDocument(missingRoot).toJson(), "Fallback"));

    QJsonObject missingAdapterRoot = validRoot;
    masterObject = missingAdapterRoot.value("master").toObject();
    slaves = masterObject.value("slaves").toArray();
    slaveObject = slaves.first().toObject();
    slaveObject.remove("adapterSelection");
    slaves[0] = slaveObject;
    masterObject.insert("slaves", slaves);
    missingAdapterRoot.insert("master", masterObject);
    QVERIFY(!parseProject(QJsonDocument(missingAdapterRoot).toJson(), "Fallback"));
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

    const QList<Data::OfflineSlaveConfiguration> serviceSlaves = offlineSlaves(secondMaster);
    const Utils::Result<> replaceSlavesResult = service->replaceOfflineSlaves(
        secondProject->snapshot().id, secondMaster, serviceSlaves);
    QVERIFY_RESULT(replaceSlavesResult);
    QCOMPARE(service->project(secondProject->snapshot().id)->slaves.size(), 2);
    const Data::NodeId configuredSlaveId
        = service->project(secondProject->snapshot().id)->slaves.first().id;
    const Data::ProcessDataConfiguration serviceProcessData = processDataConfiguration();
    const Utils::Result<> serviceProcessDataResult = service->setProcessDataConfiguration(
        secondProject->snapshot().id, configuredSlaveId, serviceProcessData);
    QVERIFY_RESULT(serviceProcessDataResult);
    QCOMPARE(
        service->project(secondProject->snapshot().id)->slaves.first().processData.pdos.size(), 2);
    const QByteArray esiSha256(32, '\x49');
    Data::DeviceAdapterProjectSelection selection = adapterSelection();
    QCOMPARE(
        service->project(secondProject->snapshot().id)->slaves.first().adapterSelection,
        Data::DeviceAdapterProjectSelection());
    const int adapterCommandCount = secondProject->document()->undoStack()->count();
    const int adapterCommandIndex = secondProject->document()->undoStack()->index();
    const Utils::Result<> serviceAdapterResult = service->setDeviceAdapterSelection(
        secondProject->snapshot().id, configuredSlaveId, esiSha256, selection);
    QVERIFY_RESULT(serviceAdapterResult);
    QCOMPARE(secondProject->document()->undoStack()->count(), adapterCommandCount + 1);
    QCOMPARE(secondProject->document()->undoStack()->index(), adapterCommandIndex + 1);
    QVERIFY(
        !service->project(secondProject->snapshot().id)
             ->slaves.first()
             .adapterSelection.adapterId.value.isEmpty());
    QCOMPARE(
        secondProject->document()->undoStack()->undoText(),
        Tr::tr("Select EtherCAT device adapter"));
    const Utils::Result<> serviceManualResult = service->setManualControlEnvelope(
        secondProject->snapshot().id, configuredSlaveId, manualControlEnvelope());
    QVERIFY_RESULT(serviceManualResult);
    QCOMPARE(
        secondProject->document()->undoStack()->undoText(),
        Tr::tr("Configure EtherCAT manual control"));
    QCOMPARE(
        service->project(secondProject->snapshot().id)
            ->slaves.first()
            .manualControlEnvelope,
        manualControlEnvelope());
    const Utils::Result<> serviceBindingResult
        = service->setMasterBindingArtifact(secondProject->snapshot().id, bindingArtifact());
    QVERIFY_RESULT(serviceBindingResult);
    QCOMPARE(
        secondProject->document()->undoStack()->undoText(),
        Tr::tr("Set EtherCAT semantic binding artifact"));
    QCOMPARE(
        service->project(secondProject->snapshot().id)->masterBindingArtifact,
        bindingArtifact());
    const Utils::Result<> undoBindingResult
        = service->undoProject(secondProject->snapshot().id);
    QVERIFY_RESULT(undoBindingResult);
    QCOMPARE(
        secondProject->document()->undoStack()->undoText(),
        Tr::tr("Configure EtherCAT manual control"));
    QCOMPARE(
        service->project(secondProject->snapshot().id)->masterBindingArtifact,
        Data::SemanticBindingArtifactReference());
    const Utils::Result<> undoManualResult
        = service->undoProject(secondProject->snapshot().id);
    QVERIFY_RESULT(undoManualResult);
    QCOMPARE(
        secondProject->document()->undoStack()->undoText(),
        Tr::tr("Select EtherCAT device adapter"));
    QCOMPARE(
        service->project(secondProject->snapshot().id)
            ->slaves.first()
            .manualControlEnvelope,
        Data::ManualControlEnvelope());
    const Utils::Result<> undoAdapterResult
        = service->undoProject(secondProject->snapshot().id);
    QVERIFY_RESULT(undoAdapterResult);
    QCOMPARE(
        secondProject->document()->undoStack()->undoText(),
        Tr::tr("Configure EtherCAT Process Data"));
    QCOMPARE(
        service->project(secondProject->snapshot().id)->slaves.first().adapterSelection,
        Data::DeviceAdapterProjectSelection());
    const Utils::Result<> undoProcessDataResult
        = service->undoProject(secondProject->snapshot().id);
    QVERIFY_RESULT(undoProcessDataResult);
    const std::optional<Data::ProjectSnapshot> afterProcessUndo
        = service->project(secondProject->snapshot().id);
    QVERIFY(afterProcessUndo);
    QCOMPARE(afterProcessUndo->slaves.size(), 2);
    QVERIFY(afterProcessUndo->slaves.first().processData.pdos.isEmpty());
    QVERIFY(service->canUndoProject(secondProject->snapshot().id));
    const Utils::Result<> undoSlavesResult
        = service->undoProject(secondProject->snapshot().id);
    QVERIFY_RESULT(undoSlavesResult);
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

void EtherCATProjectTests::testRuntimePackageActivationProjectCompareAndSet()
{
    auto *service = ExtensionSystem::PluginManager::getObject<ProjectServiceImpl>();
    QVERIFY(service);
    QVERIFY(service->projects().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const Utils::FilePath projectFile
        = temporaryFilePath(directory, "activation-cas.ecatproject");
    Data::ProjectSnapshot source = createProjectSnapshot("Activation CAS", "Test");
    const Data::NodeId sourceMasterId = masterId(source);
    source.slaves = offlineSlaves(sourceMasterId);
    writeProject(projectFile, source);

    const ProjectExplorer::OpenProjectResult openResult
        = ProjectExplorer::ProjectExplorerPlugin::openProject(projectFile, false);
    QVERIFY2(openResult, qPrintable(openResult.errorMessage()));
    QPointer<EtherCATProject> project = qobject_cast<EtherCATProject *>(openResult.project());
    QVERIFY(project);
    const QScopeGuard cleanup([&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (project && ProjectExplorer::ProjectManager::hasProject(project.data()))
            ProjectExplorer::ProjectManager::removeProject(project.data());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    });

    const Data::NodeId projectId = project->snapshot().id;
    const Data::NodeId slaveId = project->snapshot().slaves.constFirst().id;
    const Data::SemanticBindingArtifactReference targetReference
        = bindingArtifact(project->snapshot().slaves);

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> initialCapture
        = service->captureRuntimePackageActivationProject(projectId);
    QVERIFY_RESULT(initialCapture);
    QVERIFY(initialCapture->isValid());
    QCOMPARE(initialCapture->snapshot(), project->snapshot());
    QCOMPARE(initialCapture->serializedProject(), project->document()->contents());
    QCOMPARE(
        initialCapture->serializedProject(),
        serializeProject(initialCapture->snapshot()));

    QSignalSpy projectChangedSpy(service, &Core::ProjectService::projectChanged);
    const auto verifyStaleAfterMutation = [&](
                                              const auto &mutation,
                                              const QString &description) {
        const Utils::Result<Data::RuntimePackageActivationProjectCapture> capture
            = service->captureRuntimePackageActivationProject(projectId);
        QVERIFY2(capture, qPrintable(description + ": capture failed"));
        const Data::ProjectSnapshot beforeMutation = project->snapshot();
        const Utils::Result<> mutationResult = mutation();
        QVERIFY2(mutationResult, qPrintable(description + ": mutation failed"));
        QVERIFY(project->snapshot() != beforeMutation);

        const Data::ProjectSnapshot beforeCompareAndSet = project->snapshot();
        const int commandCount = project->document()->undoStack()->count();
        const int commandIndex = project->document()->undoStack()->index();
        projectChangedSpy.clear();
        const Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult> result
            = service->compareAndSetMasterBindingArtifact(
                projectId,
                capture->documentRevision(),
                capture->originalBinding(),
                targetReference);
        QVERIFY2(result, qPrintable(description + ": compare-and-set failed"));
        QVERIFY(result->isValid());
        QCOMPARE(
            result->disposition(),
            Data::RuntimePackageActivationProjectCompareAndSetDisposition::Stale);
        QVERIFY(!result->commit());
        QCOMPARE(project->snapshot(), beforeCompareAndSet);
        QCOMPARE(project->document()->undoStack()->count(), commandCount);
        QCOMPARE(project->document()->undoStack()->index(), commandIndex);
        QCOMPARE(projectChangedSpy.count(), 0);

        QVERIFY_RESULT(service->undoProject(projectId));
        QCOMPARE(project->snapshot(), beforeMutation);
        const Utils::Result<Data::RuntimePackageActivationProjectCapture> afterUndo
            = service->captureRuntimePackageActivationProject(projectId);
        QVERIFY_RESULT(afterUndo);
        QVERIFY(afterUndo->documentRevision() != capture->documentRevision());
    };

    verifyStaleAfterMutation(
        [&] {
            return service->setProcessDataConfiguration(
                projectId, slaveId, processDataConfiguration());
        },
        "PDO mutation");
    verifyStaleAfterMutation(
        [&] {
            return service->setStartupConfiguration(
                projectId, slaveId, startupConfiguration());
        },
        "Startup SDO mutation");
    verifyStaleAfterMutation(
        [&] {
            return service->setDcConfiguration(projectId, slaveId, dcConfiguration());
        },
        "DC mutation");
    verifyStaleAfterMutation(
        [&] {
            return service->setMasterBindingArtifact(projectId, targetReference);
        },
        "Binding mutation");

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> beforeCommit
        = service->captureRuntimePackageActivationProject(projectId);
    QVERIFY_RESULT(beforeCommit);
    const int commandIndexBeforeCommit = project->document()->undoStack()->index();
    projectChangedSpy.clear();
    const Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult> committed
        = service->compareAndSetMasterBindingArtifact(
            projectId,
            beforeCommit->documentRevision(),
            beforeCommit->originalBinding(),
            targetReference);
    QVERIFY_RESULT(committed);
    QVERIFY(committed->isValid());
    QCOMPARE(
        committed->disposition(),
        Data::RuntimePackageActivationProjectCompareAndSetDisposition::
            CompareAndSetCommitted);
    QVERIFY(committed->commit());
    QCOMPARE(
        committed->commit()->disposition(),
        Data::RuntimePackageActivationProjectCommitDisposition::
            CompareAndSetCommitted);
    QCOMPARE(
        committed->commit()->originalDocumentRevision(),
        beforeCommit->documentRevision());
    QCOMPARE(committed->commit()->originalBinding(), beforeCommit->originalBinding());
    QVERIFY(
        committed->commit()->resultingDocumentRevision()
        != beforeCommit->documentRevision());
    QVERIFY(committed->commit()->resultingBinding() != beforeCommit->originalBinding());
    QCOMPARE(project->document()->undoStack()->index(), commandIndexBeforeCommit + 1);
    QCOMPARE(project->document()->undoStack()->count(), commandIndexBeforeCommit + 1);
    QCOMPARE(projectChangedSpy.count(), 1);
    QCOMPARE(project->snapshot().masterBindingArtifact, targetReference);

    const Utils::Result<Data::RuntimePackageActivationProjectCapture> afterCommit
        = service->captureRuntimePackageActivationProject(projectId);
    QVERIFY_RESULT(afterCommit);
    QCOMPARE(
        afterCommit->documentRevision(),
        committed->commit()->resultingDocumentRevision());
    QCOMPARE(afterCommit->originalBinding(), committed->commit()->resultingBinding());

    const int exactCommandCount = project->document()->undoStack()->count();
    const int exactCommandIndex = project->document()->undoStack()->index();
    projectChangedSpy.clear();
    const Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult> exactReplay
        = service->compareAndSetMasterBindingArtifact(
            projectId,
            afterCommit->documentRevision(),
            afterCommit->originalBinding(),
            targetReference);
    QVERIFY_RESULT(exactReplay);
    QVERIFY(exactReplay->isValid());
    QCOMPARE(
        exactReplay->disposition(),
        Data::RuntimePackageActivationProjectCompareAndSetDisposition::AlreadyExact);
    QVERIFY(exactReplay->commit());
    QCOMPARE(
        exactReplay->commit()->disposition(),
        Data::RuntimePackageActivationProjectCommitDisposition::AlreadyExact);
    QCOMPARE(project->document()->undoStack()->count(), exactCommandCount);
    QCOMPARE(project->document()->undoStack()->index(), exactCommandIndex);
    QCOMPARE(projectChangedSpy.count(), 0);

    QVERIFY_RESULT(service->undoProject(projectId));
    const Utils::Result<Data::RuntimePackageActivationProjectCapture> afterCommitUndo
        = service->captureRuntimePackageActivationProject(projectId);
    QVERIFY_RESULT(afterCommitUndo);
    QCOMPARE(afterCommitUndo->snapshot(), beforeCommit->snapshot());
    QCOMPARE(afterCommitUndo->serializedProject(), beforeCommit->serializedProject());
    QCOMPARE(afterCommitUndo->originalBinding(), beforeCommit->originalBinding());
    QVERIFY(
        afterCommitUndo->documentRevision() != beforeCommit->documentRevision());
    QVERIFY(
        afterCommitUndo->documentRevision()
        != committed->commit()->resultingDocumentRevision());

    const Data::ProjectSnapshot beforeStaleReplay = project->snapshot();
    const int staleReplayCommandCount = project->document()->undoStack()->count();
    const int staleReplayCommandIndex = project->document()->undoStack()->index();
    projectChangedSpy.clear();
    const Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult> staleReplay
        = service->compareAndSetMasterBindingArtifact(
            projectId,
            beforeCommit->documentRevision(),
            beforeCommit->originalBinding(),
            targetReference);
    QVERIFY_RESULT(staleReplay);
    QCOMPARE(
        staleReplay->disposition(),
        Data::RuntimePackageActivationProjectCompareAndSetDisposition::Stale);
    QCOMPARE(project->snapshot(), beforeStaleReplay);
    QCOMPARE(project->document()->undoStack()->count(), staleReplayCommandCount);
    QCOMPARE(project->document()->undoStack()->index(), staleReplayCommandIndex);
    QCOMPARE(projectChangedSpy.count(), 0);
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
