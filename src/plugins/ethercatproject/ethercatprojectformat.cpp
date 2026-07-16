// Copyright (C) 2026 Kvell

#include "ethercatprojectformat.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojecttr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace EtherCAT::Project::Internal {

static constexpr char FORMAT_NAME[] = "ethercat-project";

static Utils::Result<Data::NodeId> parseRequiredId(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const Data::NodeId id = Data::NodeId::fromString(object.value(key).toString());
    if (id.isNull()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    return id;
}

static Utils::Result<QString> parseRequiredName(const QJsonObject &object, const QString &objectName)
{
    const QString name = object.value("name").toString().trimmed();
    if (name.isEmpty())
        return Utils::ResultError(Tr::tr("%1 has an empty or missing name.").arg(objectName));
    return name;
}

Data::ProjectSnapshot createProjectSnapshot(const QString &name, const QString &createdBy)
{
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId masterId = Data::NodeId::create();
    const QString projectName = name.trimmed().isEmpty() ? Tr::tr("EtherCAT Project")
                                                         : name.trimmed();

    return {
        projectId,
        projectName,
        Constants::CURRENT_FORMAT_VERSION,
        createdBy,
        {{projectId, {}, Data::ProjectNodeKind::Project, projectName},
         {targetId, projectId, Data::ProjectNodeKind::Target, Tr::tr("Target Controller")},
         {masterId, targetId, Data::ProjectNodeKind::Master, Tr::tr("EtherCAT Master")}},
        false,
        true,
        false,
        {},
    };
}

static Utils::Result<LoadedProject> parseVersionZero(
    const QJsonObject &root, const QString &fallbackName)
{
    Data::ProjectSnapshot snapshot = createProjectSnapshot(
        root.value("name").toString(fallbackName), root.value("createdBy").toString());

    if (root.contains("id")) {
        const Data::NodeId projectId = Data::NodeId::fromString(root.value("id").toString());
        if (projectId.isNull())
            return Utils::ResultError(Tr::tr("Version 0 project has an invalid project ID."));
        snapshot.id = projectId;
        snapshot.nodes[0].id = projectId;
        snapshot.nodes[1].parentId = projectId;
    }

    snapshot.migrated = true;
    return LoadedProject{snapshot, true};
}

Utils::Result<LoadedProject> parseProject(const QByteArray &contents, const QString &fallbackName)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return Utils::ResultError(Tr::tr("Invalid JSON at offset %1: %2")
                                      .arg(parseError.offset)
                                      .arg(parseError.errorString()));
    }
    if (!document.isObject())
        return Utils::ResultError(Tr::tr("The EtherCAT project root must be a JSON object."));

    const QJsonObject root = document.object();
    if (root.value("format").toString() != QLatin1StringView(FORMAT_NAME))
        return Utils::ResultError(Tr::tr("The file is not an EtherCAT project."));

    const int version = root.value("formatVersion").toInt(root.value("version").toInt(-1));
    if (version == 0)
        return parseVersionZero(root, fallbackName);
    if (version != Constants::CURRENT_FORMAT_VERSION) {
        return Utils::ResultError(
            Tr::tr("Unsupported EtherCAT project format version %1.").arg(version));
    }

    const QJsonObject projectObject = root.value("project").toObject();
    const QJsonObject targetObject = root.value("target").toObject();
    const QJsonObject masterObject = root.value("master").toObject();

    const auto projectId = parseRequiredId(projectObject, "id", Tr::tr("Project"));
    if (!projectId)
        return Utils::ResultError(projectId.error());
    const auto targetId = parseRequiredId(targetObject, "id", Tr::tr("Target"));
    if (!targetId)
        return Utils::ResultError(targetId.error());
    const auto masterId = parseRequiredId(masterObject, "id", Tr::tr("Master"));
    if (!masterId)
        return Utils::ResultError(masterId.error());

    QSet<Data::NodeId> uniqueIds{*projectId, *targetId, *masterId};
    if (uniqueIds.size() != 3)
        return Utils::ResultError(Tr::tr("Project, target, and master IDs must be unique."));

    const auto projectName = parseRequiredName(projectObject, Tr::tr("Project"));
    if (!projectName)
        return Utils::ResultError(projectName.error());
    const auto targetName = parseRequiredName(targetObject, Tr::tr("Target"));
    if (!targetName)
        return Utils::ResultError(targetName.error());
    const auto masterName = parseRequiredName(masterObject, Tr::tr("Master"));
    if (!masterName)
        return Utils::ResultError(masterName.error());

    Data::ProjectSnapshot snapshot{
        *projectId,
        *projectName,
        version,
        projectObject.value("createdBy").toString(),
        {{*projectId, {}, Data::ProjectNodeKind::Project, *projectName},
         {*targetId, *projectId, Data::ProjectNodeKind::Target, *targetName},
         {*masterId, *targetId, Data::ProjectNodeKind::Master, *masterName}},
        false,
        true,
        false,
        {},
    };
    return LoadedProject{snapshot, false};
}

QByteArray serializeProject(const Data::ProjectSnapshot &snapshot)
{
    QJsonObject projectObject;
    projectObject.insert("id", snapshot.id.toString());
    projectObject.insert("name", snapshot.name);
    projectObject.insert("createdBy", snapshot.createdBy);

    QJsonObject targetObject;
    QJsonObject masterObject;
    for (const Data::ProjectNodeSnapshot &node : snapshot.nodes) {
        if (node.kind == Data::ProjectNodeKind::Target) {
            targetObject.insert("id", node.id.toString());
            targetObject.insert("name", node.name);
        } else if (node.kind == Data::ProjectNodeKind::Master) {
            masterObject.insert("id", node.id.toString());
            masterObject.insert("name", node.name);
        }
    }

    QJsonObject root;
    root.insert("format", QLatin1StringView(FORMAT_NAME));
    root.insert("formatVersion", Constants::CURRENT_FORMAT_VERSION);
    root.insert("project", projectObject);
    root.insert("target", targetObject);
    root.insert("master", masterObject);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace EtherCAT::Project::Internal
