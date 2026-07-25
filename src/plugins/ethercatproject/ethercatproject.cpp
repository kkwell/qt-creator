// Copyright (C) 2026 Kvell

#include "ethercatproject.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojectdocument.h"
#include "ethercatprojecttr.h"

#include <coreplugin/documentmanager.h>

#include <projectexplorer/projectnodes.h>

#include <QDebug>

namespace EtherCAT::Project::Internal {

EtherCATProject::EtherCATProject(const Utils::FilePath &filePath)
    : ProjectExplorer::Project(Constants::MIME_TYPE, filePath)
    , m_document(std::make_unique<EtherCATProjectDocument>())
{
    setType(Constants::PROJECT_ID);
    setSupportsBuilding(false);
    setIsEditModePreferred(false);

    const Utils::Result<> loadResult = m_document->load(filePath);
    // Project's built-in document already watches the project file. Register the
    // editable domain document for Save All without adding a duplicate watcher.
    ::Core::DocumentManager::addDocument(m_document.get(), false);
    connect(
        m_document.get(),
        &EtherCATProjectDocument::snapshotChanged,
        this,
        [this](const Data::ProjectSnapshot &snapshot) {
            updateProjectTree();
            emit snapshotChanged(snapshot);
        });
    connect(this, &ProjectExplorer::Project::projectFileIsDirty, this, [this] {
        if (!m_document->isModified())
            (void)
                m_document->reload(::Core::IDocument::FlagReload, ::Core::IDocument::TypeContents);
    });
    connect(this, &ProjectExplorer::Project::aboutToSaveSettings, this, [this] {
        if (!m_document->isModified())
            return;
        const Utils::Result<> result = m_document->save();
        if (!result) {
            qWarning().noquote() << Tr::tr("Could not save EtherCAT project during close: %1")
                                        .arg(result.error());
        }
    });

    updateProjectTree();
    if (!loadResult) {
        addTask(createTask(
            ProjectExplorer::Task::TaskType::Error,
            Tr::tr("Cannot load EtherCAT project: %1").arg(loadResult.error())));
    }
}

EtherCATProject::~EtherCATProject()
{
    disconnect(m_document.get(), nullptr, this, nullptr);
    ::Core::DocumentManager::removeDocument(m_document.get());
}

const Data::ProjectSnapshot &EtherCATProject::snapshot() const
{
    return m_document->snapshot();
}

EtherCATProjectDocument *EtherCATProject::document() const
{
    return m_document.get();
}

Utils::Result<> EtherCATProject::renameProject(const QString &name)
{
    return m_document->renameProject(name);
}

bool EtherCATProject::needsConfiguration() const
{
    return false;
}

void EtherCATProject::updateProjectTree()
{
    const Data::ProjectSnapshot &project = snapshot();
    const QString displayName = project.name.isEmpty() ? projectFilePath().completeBaseName()
                                                       : project.name;
    setDisplayName(displayName);

    auto root = std::make_unique<ProjectExplorer::ProjectNode>(projectFilePath());
    root->setDisplayName(displayName);
    root->addNode(std::make_unique<ProjectExplorer::FileNode>(
        projectFilePath(), ProjectExplorer::FileType::Project));
    setRootProjectNode(std::move(root));
}

} // namespace EtherCAT::Project::Internal
