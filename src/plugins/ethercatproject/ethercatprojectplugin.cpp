// Copyright (C) 2026 Kvell

#include "ethercatproject.h"
#include "ethercatprojectconstants.h"
#ifdef WITH_TESTS
#include "ethercatprojecttests.h"
#endif
#include "ethercatprojectwizard.h"
#include "projectserviceimpl.h"

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/projectmanager.h>

#include <memory>
#include <utility>

namespace EtherCAT::Project::Internal {

class EtherCATProjectPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATProject.json")

public:
    ~EtherCATProjectPlugin() final;

    void initialize() final;
    void extensionsInitialized() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void unregisterService();

    std::unique_ptr<ProjectServiceImpl> m_projectService;
    QList<QMetaObject::Connection> m_connections;
    bool m_serviceRegistered = false;
};

EtherCATProjectPlugin::~EtherCATProjectPlugin()
{
    unregisterService();
}

void EtherCATProjectPlugin::initialize()
{
    qRegisterMetaType<Data::ProjectNodeKind>();
    qRegisterMetaType<Data::ProjectNodeSnapshot>();
    qRegisterMetaType<Data::ProjectSnapshot>();

    m_projectService = std::make_unique<ProjectServiceImpl>();
    ExtensionSystem::PluginManager::addObject(m_projectService.get());
    m_serviceRegistered = true;

    ProjectExplorer::ProjectManager::registerProjectType<EtherCATProject>(Constants::MIME_TYPE);
    setupEtherCATProjectWizard();

    ProjectExplorer::ProjectManager *manager = ProjectExplorer::ProjectManager::instance();
    m_connections.append(connect(
        manager,
        &ProjectExplorer::ProjectManager::projectAdded,
        m_projectService.get(),
        &ProjectServiceImpl::registerProject));
    m_connections.append(connect(
        manager,
        &ProjectExplorer::ProjectManager::aboutToRemoveProject,
        m_projectService.get(),
        &ProjectServiceImpl::unregisterProject));
    m_connections.append(connect(
        manager,
        &ProjectExplorer::ProjectManager::startupProjectChanged,
        m_projectService.get(),
        &ProjectServiceImpl::setStartupProject));

    for (ProjectExplorer::Project *project : ProjectExplorer::ProjectManager::projects())
        m_projectService->registerProject(project);
    m_projectService->setStartupProject(ProjectExplorer::ProjectManager::startupProject());

#ifdef WITH_TESTS
    addTest<EtherCATProjectTests>();
#endif
}

void EtherCATProjectPlugin::extensionsInitialized() {}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATProjectPlugin::aboutToShutdown()
{
    unregisterService();
    return SynchronousShutdown;
}

void EtherCATProjectPlugin::unregisterService()
{
    if (!m_serviceRegistered)
        return;

    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();

    m_projectService->clear();
    ExtensionSystem::PluginManager::removeObject(m_projectService.get());
    m_projectService.reset();
    m_serviceRegistered = false;
}

} // namespace EtherCAT::Project::Internal

#include "ethercatprojectplugin.moc"
