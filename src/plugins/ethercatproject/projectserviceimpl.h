// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QList>
#include <QPointer>

namespace ProjectExplorer {
class Project;
}

namespace EtherCAT::Project::Internal {

class EtherCATProject;

class ProjectServiceImpl final : public Core::ProjectService
{
    Q_OBJECT

public:
    explicit ProjectServiceImpl(QObject *parent = nullptr);

    QList<Data::ProjectSnapshot> projects() const final;
    std::optional<Data::ProjectSnapshot> project(const Data::NodeId &projectId) const final;
    Data::NodeId activeProjectId() const final;

    Utils::Result<> activateProject(const Data::NodeId &projectId) final;
    Utils::Result<> renameProject(const Data::NodeId &projectId, const QString &name) final;
    Utils::Result<> saveProject(const Data::NodeId &projectId) final;
    Utils::Result<> undoProject(const Data::NodeId &projectId) final;
    Utils::Result<> redoProject(const Data::NodeId &projectId) final;
    Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        const QList<Data::OfflineSlaveConfiguration> &slaves) final;
    Utils::Result<> setProcessDataConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::ProcessDataConfiguration &configuration) final;
    Utils::Result<> setStartupConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::StartupConfiguration &configuration) final;
    Utils::Result<> setDcConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::DcConfiguration &configuration) final;
    bool canUndoProject(const Data::NodeId &projectId) const final;
    bool canRedoProject(const Data::NodeId &projectId) const final;

    void registerProject(ProjectExplorer::Project *project);
    void unregisterProject(ProjectExplorer::Project *project);
    void setStartupProject(ProjectExplorer::Project *project);
    void clear();

private:
    EtherCATProject *findProject(const Data::NodeId &projectId) const;
    void handleSnapshotChanged(EtherCATProject *project, const Data::ProjectSnapshot &snapshot);
    bool isGuiThread() const;

    QList<QPointer<EtherCATProject>> m_projects;
    Data::NodeId m_activeProjectId;
};

} // namespace EtherCAT::Project::Internal
