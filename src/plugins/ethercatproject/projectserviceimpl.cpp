// Copyright (C) 2026 Kvell

#include "projectserviceimpl.h"

#include "ethercatproject.h"
#include "ethercatprojectconstants.h"
#include "ethercatprojectdocument.h"
#include "ethercatprojecttr.h"

#include <projectexplorer/projectmanager.h>

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

void ProjectServiceImpl::registerProject(ProjectExplorer::Project *project)
{
    QTC_ASSERT(isGuiThread(), return);
    auto *etherCATProject = qobject_cast<EtherCATProject *>(project);
    if (!etherCATProject || m_projects.contains(etherCATProject))
        return;
    if (findProject(etherCATProject->snapshot().id)) {
        etherCATProject->addTask(ProjectExplorer::Project::createTask(
            ProjectExplorer::Task::TaskType::Error,
            Tr::tr("Another open EtherCAT project has the same project ID.")));
        return;
    }

    m_projects.append(etherCATProject);
    connect(
        etherCATProject,
        &EtherCATProject::snapshotChanged,
        this,
        [this, etherCATProject](const Data::ProjectSnapshot &snapshot) {
            handleSnapshotChanged(etherCATProject, snapshot);
        });
    emit projectAdded(etherCATProject->snapshot());
}

void ProjectServiceImpl::unregisterProject(ProjectExplorer::Project *project)
{
    QTC_ASSERT(isGuiThread(), return);
    auto *etherCATProject = qobject_cast<EtherCATProject *>(project);
    if (!etherCATProject || !m_projects.contains(etherCATProject))
        return;

    const Data::NodeId projectId = etherCATProject->snapshot().id;
    emit projectAboutToBeRemoved(projectId);
    if (m_activeProjectId == projectId) {
        m_activeProjectId = {};
        emit activeProjectChanged(projectId, {});
    }
    disconnect(etherCATProject, nullptr, this, nullptr);
    m_projects.removeAll(etherCATProject);
}

void ProjectServiceImpl::setStartupProject(ProjectExplorer::Project *project)
{
    QTC_ASSERT(isGuiThread(), return);
    const auto *etherCATProject = qobject_cast<EtherCATProject *>(project);
    const Data::NodeId newProjectId = etherCATProject ? etherCATProject->snapshot().id
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
    for (const QPointer<EtherCATProject> &project : std::as_const(m_projects)) {
        if (project)
            disconnect(project, nullptr, this, nullptr);
    }
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
