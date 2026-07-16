// Copyright (C) 2026 Kvell

#include "ethercatprojectdocument.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojectformat.h"
#include "ethercatprojecttr.h"

#include <utils/fileutils.h>

#include <QSignalBlocker>
#include <QUndoCommand>

namespace EtherCAT::Project::Internal {

class RenameProjectCommand final : public QUndoCommand
{
public:
    RenameProjectCommand(
        EtherCATProjectDocument *document, const QString &oldName, const QString &newName)
        : m_document(document)
        , m_oldName(oldName)
        , m_newName(newName)
    {
        setText(Tr::tr("Rename EtherCAT project"));
    }

    void undo() final { m_document->applyProjectName(m_oldName); }
    void redo() final { m_document->applyProjectName(m_newName); }

private:
    EtherCATProjectDocument *m_document;
    QString m_oldName;
    QString m_newName;
};

EtherCATProjectDocument::EtherCATProjectDocument(QObject *parent)
    : ::Core::IDocument(parent)
{
    setId(Constants::DOCUMENT_ID);
    setMimeType(Constants::MIME_TYPE);
    setSuspendAllowed(false);

    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] { publishSnapshot(); });
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, [this] { publishSnapshot(); });
}

Utils::Result<> EtherCATProjectDocument::load(const Utils::FilePath &filePath)
{
    setFilePath(filePath);
    m_migrationBackupPath = {};

    const Utils::Result<QByteArray> contents = filePath.fileContents();
    if (!contents) {
        if (!m_snapshot.valid)
            setInvalidSnapshot(filePath.completeBaseName(), contents.error());
        return Utils::ResultError(contents.error());
    }

    const Utils::Result<LoadedProject> loaded = parseProject(*contents, filePath.completeBaseName());
    if (!loaded) {
        if (!m_snapshot.valid)
            setInvalidSnapshot(filePath.completeBaseName(), loaded.error());
        return Utils::ResultError(loaded.error());
    }
    if (m_snapshot.valid && !m_snapshot.id.isNull() && loaded->snapshot.id != m_snapshot.id) {
        return Utils::ResultError(
            Tr::tr("The project ID changed while the EtherCAT project was open."));
    }

    {
        const QSignalBlocker blocker(&m_undoStack);
        m_undoStack.clear();
        m_snapshot = loaded->snapshot;
    }
    m_migrationPending = loaded->migrationRequired;
    m_snapshot.modified = isModified();
    publishSnapshot();
    return Utils::ResultOk;
}

const Data::ProjectSnapshot &EtherCATProjectDocument::snapshot() const
{
    return m_snapshot;
}

QUndoStack *EtherCATProjectDocument::undoStack()
{
    return &m_undoStack;
}

Utils::FilePath EtherCATProjectDocument::migrationBackupPath() const
{
    return m_migrationBackupPath;
}

Utils::Result<> EtherCATProjectDocument::renameProject(const QString &name)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return Utils::ResultError(Tr::tr("Project name cannot be empty."));
    if (trimmedName == m_snapshot.name)
        return Utils::ResultOk;

    m_undoStack.push(new RenameProjectCommand(this, m_snapshot.name, trimmedName));
    return Utils::ResultOk;
}

QByteArray EtherCATProjectDocument::contents() const
{
    return serializeProject(m_snapshot);
}

bool EtherCATProjectDocument::isModified() const
{
    return m_migrationPending || !m_undoStack.isClean();
}

bool EtherCATProjectDocument::isSaveAsAllowed() const
{
    return false;
}

bool EtherCATProjectDocument::shouldAutoSave() const
{
    return false;
}

::Core::IDocument::ReloadBehavior EtherCATProjectDocument::reloadBehavior(
    ChangeTrigger trigger, ChangeType type) const
{
    Q_UNUSED(type)
    if (trigger == TriggerExternal && isModified())
        return BehaviorAsk;
    return BehaviorSilent;
}

Utils::Result<> EtherCATProjectDocument::reload(ReloadFlag flag, ChangeType type)
{
    Q_UNUSED(type)
    if (flag == FlagIgnore)
        return Utils::ResultOk;
    return load(filePath());
}

Utils::Result<> EtherCATProjectDocument::saveImpl(const Utils::FilePath &filePath, SaveOption option)
{
    Q_UNUSED(option)
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot save an invalid EtherCAT project."));

    const Utils::FilePath destination = filePath.isEmpty() ? this->filePath() : filePath;
    if (destination.isEmpty())
        return Utils::ResultError(Tr::tr("No EtherCAT project file path was specified."));
    if (destination != this->filePath()) {
        return Utils::ResultError(Tr::tr("Save As is not supported for an open EtherCAT project."));
    }

    if (m_migrationPending && destination == this->filePath()) {
        const Utils::Result<> backupResult = createMigrationBackup(this->filePath());
        if (!backupResult)
            return backupResult;
    }

    Utils::FileSaver saver(destination, QIODevice::Text);
    if (!saver.write(serializeProject(m_snapshot)))
        return Utils::ResultError(saver.errorString());
    const Utils::Result<> saveResult = saver.finalize();
    if (!saveResult)
        return saveResult;

    m_migrationPending = false;
    m_snapshot.migrated = false;
    m_undoStack.setClean();
    publishSnapshot();
    return Utils::ResultOk;
}

void EtherCATProjectDocument::applyProjectName(const QString &name)
{
    m_snapshot.name = name;
    for (Data::ProjectNodeSnapshot &node : m_snapshot.nodes) {
        if (node.kind == Data::ProjectNodeKind::Project) {
            node.name = name;
            break;
        }
    }
}

void EtherCATProjectDocument::publishSnapshot()
{
    m_snapshot.modified = isModified();
    emit changed();
    emit contentsChanged();
    emit snapshotChanged(m_snapshot);
}

void EtherCATProjectDocument::setInvalidSnapshot(const QString &fallbackName, const QString &error)
{
    {
        const QSignalBlocker blocker(&m_undoStack);
        m_undoStack.clear();
        m_snapshot = createProjectSnapshot(fallbackName, {});
    }
    m_snapshot.valid = false;
    m_snapshot.error = error;
    m_migrationPending = false;
    publishSnapshot();
}

Utils::Result<> EtherCATProjectDocument::createMigrationBackup(const Utils::FilePath &sourcePath)
{
    if (!m_migrationBackupPath.isEmpty())
        return Utils::ResultOk;

    Utils::FilePath candidate = sourcePath.stringAppended(".v0.bak");
    for (int index = 1; candidate.exists(); ++index)
        candidate = sourcePath.stringAppended(QString(".v0.bak.%1").arg(index));

    const Utils::Result<> copyResult = sourcePath.copyFile(candidate);
    if (!copyResult) {
        return Utils::ResultError(Tr::tr("Could not create migration backup '%1': %2")
                                      .arg(candidate.toUserOutput(), copyResult.error()));
    }
    m_migrationBackupPath = candidate;
    return Utils::ResultOk;
}

} // namespace EtherCAT::Project::Internal
