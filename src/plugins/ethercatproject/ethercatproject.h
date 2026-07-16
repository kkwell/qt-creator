// Copyright (C) 2026 Kvell

#pragma once

#include <projectexplorer/project.h>

#include <ethercatdata/projectsnapshot.h>

#include <utils/result.h>

#include <memory>

namespace EtherCAT::Project::Internal {

class EtherCATProjectDocument;

class EtherCATProject final : public ProjectExplorer::Project
{
    Q_OBJECT

public:
    explicit EtherCATProject(const Utils::FilePath &filePath);
    ~EtherCATProject() final;

    const Data::ProjectSnapshot &snapshot() const;
    EtherCATProjectDocument *document() const;
    Utils::Result<> renameProject(const QString &name);

signals:
    void snapshotChanged(const EtherCAT::Data::ProjectSnapshot &snapshot);

private:
    void updateProjectTree();

    std::unique_ptr<EtherCATProjectDocument> m_document;
};

} // namespace EtherCAT::Project::Internal
