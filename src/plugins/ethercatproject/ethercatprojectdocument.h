// Copyright (C) 2026 Kvell

#pragma once

#include <coreplugin/idocument.h>

#include <ethercatdata/projectsnapshot.h>

#include <QUndoStack>

namespace EtherCAT::Project::Internal {

class EtherCATProjectDocument final : public ::Core::IDocument
{
    Q_OBJECT

public:
    explicit EtherCATProjectDocument(QObject *parent = nullptr);

    Utils::Result<> load(const Utils::FilePath &filePath);
    const Data::ProjectSnapshot &snapshot() const;
    QUndoStack *undoStack();
    Utils::FilePath migrationBackupPath() const;

    Utils::Result<> renameProject(const QString &name);
    Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &masterId,
        const QList<Data::OfflineSlaveConfiguration> &slaves);

    QByteArray contents() const final;
    bool isModified() const final;
    bool isSaveAsAllowed() const final;
    bool shouldAutoSave() const final;
    ReloadBehavior reloadBehavior(ChangeTrigger trigger, ChangeType type) const final;
    Utils::Result<> reload(ReloadFlag flag, ChangeType type) final;

signals:
    void snapshotChanged(const EtherCAT::Data::ProjectSnapshot &snapshot);

protected:
    Utils::Result<> saveImpl(const Utils::FilePath &filePath, SaveOption option) final;

private:
    void applyProjectName(const QString &name);
    void applyOfflineSlaves(
        const Data::NodeId &masterId,
        const QList<Data::OfflineSlaveConfiguration> &slaves);
    void publishSnapshot();
    void setInvalidSnapshot(const QString &fallbackName, const QString &error);
    Utils::Result<> createMigrationBackup(const Utils::FilePath &sourcePath);

    Data::ProjectSnapshot m_snapshot;
    QUndoStack m_undoStack;
    bool m_migrationPending = false;
    Utils::FilePath m_migrationBackupPath;

    friend class RenameProjectCommand;
    friend class ReplaceOfflineSlavesCommand;
};

} // namespace EtherCAT::Project::Internal
