// Copyright (C) 2026 Kvell

#include "ethercatprojecttests.h"

#include "ethercatproject.h"
#include "ethercatprojectconstants.h"
#include "ethercatprojectdocument.h"
#include "ethercatprojectformat.h"
#include "projectserviceimpl.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/iwizardfactory.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <utils/filepath.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
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

void EtherCATProjectTests::testFormatRoundTripAndCorruption()
{
    const Data::ProjectSnapshot source = createProjectSnapshot("Packing Line", "Test 20.0.1");
    const Utils::Result<LoadedProject> loaded = parseProject(serializeProject(source), "Fallback");
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->snapshot, source);
    QVERIFY(!loaded->migrationRequired);

    const Utils::Result<LoadedProject> malformed = parseProject("{broken", "Fallback");
    QVERIFY(!malformed);
    QVERIFY(malformed.error().contains("Invalid JSON"));

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
    QVERIFY(future.error().contains("version 99"));
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

} // namespace EtherCAT::Project::Internal
