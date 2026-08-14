// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <projectexplorer/task.h>

#include <QHash>
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
    bool managesProject(const QObject *project) const final;

    Utils::Result<> activateProject(const Data::NodeId &projectId) final;
    Utils::Result<> renameProject(const Data::NodeId &projectId, const QString &name) final;
    Utils::Result<> saveProject(const Data::NodeId &projectId) final;
    Utils::Result<> undoProject(const Data::NodeId &projectId) final;
    Utils::Result<> redoProject(const Data::NodeId &projectId) final;
    Utils::Result<> setMasterConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        const Data::MasterConfiguration &configuration) final;
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
    Utils::Result<> setDeviceAdapterSelection(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const QByteArray &esiSha256,
        const Data::DeviceAdapterProjectSelection &selection) final;
    Utils::Result<> setDeviceParameterConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const QByteArray &expectedEsiSha256,
        const Data::DeviceAdapterProjectSelection &expectedAdapterSelection,
        const Data::DeviceParameterConfiguration &configuration) final;
    Utils::Result<> setManualControlEnvelope(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::ManualControlEnvelope &envelope) final;
    Utils::Result<> setMasterBindingArtifact(
        const Data::NodeId &projectId,
        const Data::SemanticBindingArtifactReference &reference) final;
    Utils::Result<Data::RuntimePackageActivationProjectCapture>
    captureRuntimePackageActivationProject(const Data::NodeId &projectId) const final;
    Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult>
    compareAndSetMasterBindingArtifact(
        const Data::NodeId &projectId,
        const Data::RuntimePackageActivationDocumentRevisionToken &expectedDocumentRevision,
        const Data::RuntimePackageActivationOriginalBindingToken &expectedBinding,
        const Data::SemanticBindingArtifactReference &targetReference) final;
    bool canUndoProject(const Data::NodeId &projectId) const final;
    bool canRedoProject(const Data::NodeId &projectId) const final;
    Utils::Result<> renameStructuralNode(
        const Data::NodeId &projectId,
        const Data::NodeId &nodeId,
        const QString &name) final;

    void registerProject(ProjectExplorer::Project *project);
    void unregisterProject(ProjectExplorer::Project *project);
    void setStartupProject(ProjectExplorer::Project *project);
    void clear();

private:
    EtherCATProject *findProject(const Data::NodeId &projectId) const;
    void manageProject(EtherCATProject *project);
    void reevaluateProjectId(const Data::NodeId &projectId);
    void markDuplicateProject(EtherCATProject *project);
    void clearDuplicateProjectTask(EtherCATProject *project);
    void handleSnapshotChanged(EtherCATProject *project, const Data::ProjectSnapshot &snapshot);
    bool isGuiThread() const;

    QList<QPointer<EtherCATProject>> m_openProjects;
    QList<QPointer<EtherCATProject>> m_projects;
    QHash<EtherCATProject *, ProjectExplorer::Task> m_duplicateProjectTasks;
    Data::NodeId m_activeProjectId;
};

} // namespace EtherCAT::Project::Internal
