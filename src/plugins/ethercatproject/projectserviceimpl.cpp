// Copyright (C) 2026 Kvell

#include "projectserviceimpl.h"

#include "ethercatproject.h"
#include "ethercatprojectconstants.h"
#include "ethercatprojectdocument.h"
#include "ethercatprojecttr.h"

#include <projectexplorer/projectmanager.h>
#include <projectexplorer/task.h>
#include <projectexplorer/taskhub.h>

#include <utils/qtcassert.h>

#include <QCoreApplication>
#include <QThread>

#include <utility>

namespace EtherCAT::Project::Internal {

ProjectServiceImpl::ProjectServiceImpl(QObject *parent)
    : Core::ProjectService(Constants::PROJECT_SERVICE_ID, Tr::tr("EtherCAT projects"), parent)
{
    setAvailable(true);
}

QList<Data::ProjectSnapshot> ProjectServiceImpl::projects() const
{
    QTC_ASSERT(isGuiThread(), return {});
    QList<Data::ProjectSnapshot> result;
    result.reserve(m_projects.size());
    for (const QPointer<EtherCATProject> &project : m_projects) {
        if (project)
            result.append(project->snapshot());
    }
    return result;
}

std::optional<Data::ProjectSnapshot> ProjectServiceImpl::project(const Data::NodeId &projectId) const
{
    QTC_ASSERT(isGuiThread(), return std::nullopt);
    if (const EtherCATProject *project = findProject(projectId))
        return project->snapshot();
    return std::nullopt;
}

Data::NodeId ProjectServiceImpl::activeProjectId() const
{
    QTC_ASSERT(isGuiThread(), return {});
    return m_activeProjectId;
}

bool ProjectServiceImpl::managesProject(const QObject *project) const
{
    QTC_ASSERT(isGuiThread(), return false);
    const auto *etherCATProject = qobject_cast<const EtherCATProject *>(project);
    if (!etherCATProject)
        return false;
    for (const QPointer<EtherCATProject> &managedProject : m_projects) {
        if (managedProject.data() == etherCATProject)
            return true;
    }
    return false;
}

Utils::Result<> ProjectServiceImpl::activateProject(const Data::NodeId &projectId)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    if (projectId.isNull()) {
        ProjectExplorer::ProjectManager::setStartupProject(nullptr);
        return Utils::ResultOk;
    }
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    ProjectExplorer::ProjectManager::setStartupProject(project);
    return Utils::ResultOk;
}

Utils::Result<> ProjectServiceImpl::renameProject(const Data::NodeId &projectId, const QString &name)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->renameProject(name);
}

Utils::Result<> ProjectServiceImpl::saveProject(const Data::NodeId &projectId)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->save();
}

Utils::Result<> ProjectServiceImpl::undoProject(const Data::NodeId &projectId)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    QUndoStack *stack = project->document()->undoStack();
    if (!stack->canUndo())
        return Utils::ResultError(Tr::tr("The EtherCAT project has no change to undo."));
    stack->undo();
    return Utils::ResultOk;
}

Utils::Result<> ProjectServiceImpl::redoProject(const Data::NodeId &projectId)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    QUndoStack *stack = project->document()->undoStack();
    if (!stack->canRedo())
        return Utils::ResultError(Tr::tr("The EtherCAT project has no change to redo."));
    stack->redo();
    return Utils::ResultOk;
}

Utils::Result<> ProjectServiceImpl::setMasterConfiguration(
    const Data::NodeId &projectId,
    const Data::NodeId &masterId,
    const Data::MasterConfiguration &configuration)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->setMasterConfiguration(masterId, configuration);
}

Utils::Result<> ProjectServiceImpl::replaceOfflineSlaves(
    const Data::NodeId &projectId,
    const Data::NodeId &masterId,
    const QList<Data::OfflineSlaveConfiguration> &slaves)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->replaceOfflineSlaves(masterId, slaves);
}

Utils::Result<> ProjectServiceImpl::setProcessDataConfiguration(
    const Data::NodeId &projectId,
    const Data::NodeId &slaveId,
    const Data::ProcessDataConfiguration &configuration)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->setProcessDataConfiguration(slaveId, configuration);
}

Utils::Result<> ProjectServiceImpl::setStartupConfiguration(
    const Data::NodeId &projectId,
    const Data::NodeId &slaveId,
    const Data::StartupConfiguration &configuration)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->setStartupConfiguration(slaveId, configuration);
}

Utils::Result<> ProjectServiceImpl::setDcConfiguration(
    const Data::NodeId &projectId,
    const Data::NodeId &slaveId,
    const Data::DcConfiguration &configuration)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->setDcConfiguration(slaveId, configuration);
}

Utils::Result<> ProjectServiceImpl::setDeviceAdapterSelection(
    const Data::NodeId &projectId,
    const Data::NodeId &slaveId,
    const QByteArray &esiSha256,
    const Data::DeviceAdapterProjectSelection &selection)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->setDeviceAdapterSelection(slaveId, esiSha256, selection);
}

Utils::Result<> ProjectServiceImpl::setMasterBindingArtifact(
    const Data::NodeId &projectId,
    const Data::SemanticBindingArtifactReference &reference)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->setMasterBindingArtifact(reference);
}

bool ProjectServiceImpl::canUndoProject(const Data::NodeId &projectId) const
{
    QTC_ASSERT(isGuiThread(), return false);
    const EtherCATProject *project = findProject(projectId);
    return project && project->document()->undoStack()->canUndo();
}

bool ProjectServiceImpl::canRedoProject(const Data::NodeId &projectId) const
{
    QTC_ASSERT(isGuiThread(), return false);
    const EtherCATProject *project = findProject(projectId);
    return project && project->document()->undoStack()->canRedo();
}

Utils::Result<> ProjectServiceImpl::renameStructuralNode(
    const Data::NodeId &projectId, const Data::NodeId &nodeId, const QString &name)
{
    QTC_ASSERT(isGuiThread(), return Utils::ResultError(Tr::tr("Project service thread error.")));
    EtherCATProject *project = findProject(projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The requested EtherCAT project is not open."));
    return project->document()->renameStructuralNode(nodeId, name);
}

void ProjectServiceImpl::registerProject(ProjectExplorer::Project *project)
{
    QTC_ASSERT(isGuiThread(), return);
    auto *etherCATProject = qobject_cast<EtherCATProject *>(project);
    if (!etherCATProject || m_openProjects.contains(etherCATProject))
        return;
    m_openProjects.append(etherCATProject);
    reevaluateProjectId(etherCATProject->snapshot().id);
}

void ProjectServiceImpl::unregisterProject(ProjectExplorer::Project *project)
{
    QTC_ASSERT(isGuiThread(), return);
    auto *etherCATProject = qobject_cast<EtherCATProject *>(project);
    if (!etherCATProject || !m_openProjects.contains(etherCATProject))
        return;

    const Data::NodeId projectId = etherCATProject->snapshot().id;
    clearDuplicateProjectTask(etherCATProject);
    m_openProjects.removeAll(etherCATProject);
    if (m_projects.contains(etherCATProject)) {
        emit projectAboutToBeRemoved(projectId);
        if (m_activeProjectId == projectId) {
            m_activeProjectId = {};
            emit activeProjectChanged(projectId, {});
        }
        disconnect(etherCATProject, nullptr, this, nullptr);
        m_projects.removeAll(etherCATProject);
    }
    reevaluateProjectId(projectId);
    setStartupProject(ProjectExplorer::ProjectManager::startupProject());
}

void ProjectServiceImpl::setStartupProject(ProjectExplorer::Project *project)
{
    QTC_ASSERT(isGuiThread(), return);
    const auto *etherCATProject = qobject_cast<EtherCATProject *>(project);
    const Data::NodeId newProjectId
        = etherCATProject && managesProject(etherCATProject)
              ? etherCATProject->snapshot().id
              : Data::NodeId{};
    if (newProjectId == m_activeProjectId)
        return;
    const Data::NodeId oldProjectId = m_activeProjectId;
    m_activeProjectId = newProjectId;
    emit activeProjectChanged(oldProjectId, m_activeProjectId);
}

void ProjectServiceImpl::clear()
{
    QTC_ASSERT(isGuiThread(), return);
    for (ProjectExplorer::Task &task : m_duplicateProjectTasks)
        ProjectExplorer::TaskHub::clearAndRemoveTask(task);
    m_duplicateProjectTasks.clear();
    for (const QPointer<EtherCATProject> &project : std::as_const(m_projects)) {
        if (project)
            disconnect(project, nullptr, this, nullptr);
    }
    m_openProjects.clear();
    m_projects.clear();
    if (!m_activeProjectId.isNull()) {
        const Data::NodeId oldProjectId = m_activeProjectId;
        m_activeProjectId = {};
        emit activeProjectChanged(oldProjectId, {});
    }
}

EtherCATProject *ProjectServiceImpl::findProject(const Data::NodeId &projectId) const
{
    if (projectId.isNull())
        return nullptr;
    for (const QPointer<EtherCATProject> &project : m_projects) {
        if (project && project->snapshot().id == projectId)
            return project;
    }
    return nullptr;
}

void ProjectServiceImpl::manageProject(EtherCATProject *project)
{
    QTC_ASSERT(project, return);
    if (m_projects.contains(project))
        return;
    clearDuplicateProjectTask(project);
    m_projects.append(project);
    connect(
        project,
        &EtherCATProject::snapshotChanged,
        this,
        [this, project](const Data::ProjectSnapshot &snapshot) {
            handleSnapshotChanged(project, snapshot);
        });
    emit projectAdded(project->snapshot());
}

void ProjectServiceImpl::reevaluateProjectId(const Data::NodeId &projectId)
{
    QList<EtherCATProject *> candidates;
    for (const QPointer<EtherCATProject> &project : std::as_const(m_openProjects)) {
        if (project && project->snapshot().id == projectId)
            candidates.append(project);
    }

    if (EtherCATProject *owner = findProject(projectId)) {
        for (EtherCATProject *candidate : std::as_const(candidates)) {
            if (candidate != owner)
                markDuplicateProject(candidate);
        }
        return;
    }

    if (candidates.size() == 1) {
        manageProject(candidates.constFirst());
        return;
    }
    for (EtherCATProject *candidate : std::as_const(candidates))
        markDuplicateProject(candidate);
}

void ProjectServiceImpl::markDuplicateProject(EtherCATProject *project)
{
    QTC_ASSERT(project, return);
    if (m_duplicateProjectTasks.contains(project))
        return;
    const ProjectExplorer::Task task(
        ProjectExplorer::OtherTask(
            ProjectExplorer::Task::TaskType::Error,
            Tr::tr("Another open EtherCAT project has the same project ID.")));
    m_duplicateProjectTasks.insert(project, task);
    ProjectExplorer::TaskHub::addTask(task);
}

void ProjectServiceImpl::clearDuplicateProjectTask(EtherCATProject *project)
{
    auto task = m_duplicateProjectTasks.find(project);
    if (task == m_duplicateProjectTasks.end())
        return;
    ProjectExplorer::TaskHub::clearAndRemoveTask(task.value());
    m_duplicateProjectTasks.erase(task);
}

void ProjectServiceImpl::handleSnapshotChanged(
    EtherCATProject *project, const Data::ProjectSnapshot &snapshot)
{
    QTC_ASSERT(isGuiThread(), return);
    if (!m_projects.contains(project))
        return;
    emit projectChanged(snapshot);
}

bool ProjectServiceImpl::isGuiThread() const
{
    return QCoreApplication::instance()
           && QThread::currentThread() == QCoreApplication::instance()->thread();
}

} // namespace EtherCAT::Project::Internal
