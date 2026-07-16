// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/projectsnapshot.h>

#include <utils/result.h>

#include <QByteArray>

namespace EtherCAT::Project::Internal {

struct LoadedProject
{
    Data::ProjectSnapshot snapshot;
    bool migrationRequired = false;
};

Data::ProjectSnapshot createProjectSnapshot(const QString &name, const QString &createdBy);
Utils::Result<LoadedProject> parseProject(const QByteArray &contents, const QString &fallbackName);
QByteArray serializeProject(const Data::ProjectSnapshot &snapshot);

} // namespace EtherCAT::Project::Internal
