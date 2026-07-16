// Copyright (C) 2026 Kvell

#pragma once

#include "devicedescription.h"
#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QList>
#include <QMetaType>
#include <QString>

namespace EtherCAT::Data {

enum class ProjectNodeKind { Project, Target, Master, Slave };

struct ETHERCATDATA_EXPORT OfflineSlaveConfiguration
{
    NodeId id;
    NodeId masterId;
    int position = -1;
    DeviceIdentity identity;
    quint32 serialNumber = 0;
    quint16 alias = 0;
    QString name;
    NodeId deviceDescriptionId;

    friend bool operator==(const OfflineSlaveConfiguration &, const OfflineSlaveConfiguration &)
        = default;
};

struct ETHERCATDATA_EXPORT ProjectNodeSnapshot
{
    NodeId id;
    NodeId parentId;
    ProjectNodeKind kind = ProjectNodeKind::Project;
    QString name;

    friend bool operator==(const ProjectNodeSnapshot &, const ProjectNodeSnapshot &) = default;
};

struct ETHERCATDATA_EXPORT ProjectSnapshot
{
    NodeId id;
    QString name;
    int formatVersion = 0;
    QString createdBy;
    QList<ProjectNodeSnapshot> nodes;
    bool modified = false;
    bool valid = false;
    bool migrated = false;
    QString error;
    QList<OfflineSlaveConfiguration> slaves;

    friend bool operator==(const ProjectSnapshot &, const ProjectSnapshot &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ProjectNodeKind)
Q_DECLARE_METATYPE(EtherCAT::Data::OfflineSlaveConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::ProjectNodeSnapshot)
Q_DECLARE_METATYPE(EtherCAT::Data::ProjectSnapshot)
