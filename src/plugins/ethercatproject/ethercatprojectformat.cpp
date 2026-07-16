// Copyright (C) 2026 Kvell

#include "ethercatprojectformat.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojecttr.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

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

static Utils::Result<quint32> parseUnsigned(
    const QJsonObject &object,
    const QString &key,
    const QString &objectName,
    quint32 maximum = std::numeric_limits<quint32>::max())
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    const double rawValue = jsonValue.toDouble(-1);
    const quint64 value = rawValue >= 0 ? quint64(rawValue) : quint64(maximum) + 1;
    if (rawValue < 0 || rawValue > maximum || rawValue != double(value)) {
        return Utils::ResultError(
            Tr::tr("%1 has an out-of-range '%2' value.").arg(objectName, key));
    }
    return quint32(value);
}

static Utils::Result<QList<Data::OfflineSlaveConfiguration>> parseOfflineSlaves(
    const QJsonObject &masterObject,
    const Data::NodeId &masterId,
    QSet<Data::NodeId> *uniqueIds)
{
    const QJsonValue slavesValue = masterObject.value("slaves");
    if (slavesValue.isUndefined())
        return QList<Data::OfflineSlaveConfiguration>();
    if (!slavesValue.isArray())
        return Utils::ResultError(Tr::tr("Master 'slaves' must be a JSON array."));

    QList<Data::OfflineSlaveConfiguration> slaves;
    QSet<int> positions;
    const QJsonArray array = slavesValue.toArray();
    slaves.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Offline slave %1 must be a JSON object.").arg(index));
        }
        const QJsonObject object = array.at(index).toObject();
        const QString objectName = Tr::tr("Offline slave %1").arg(index);
        const auto id = parseRequiredId(object, "id", objectName);
        if (!id)
            return Utils::ResultError(id.error());
        if (uniqueIds->contains(*id))
            return Utils::ResultError(Tr::tr("EtherCAT project node IDs must be unique."));
        const auto name = parseRequiredName(object, objectName);
        if (!name)
            return Utils::ResultError(name.error());
        const auto position = parseUnsigned(
            object, "position", objectName, std::numeric_limits<int>::max());
        if (!position)
            return Utils::ResultError(position.error());
        if (positions.contains(int(*position)))
            return Utils::ResultError(Tr::tr("Offline slave positions must be unique."));
        const auto vendorId = parseUnsigned(object, "vendorId", objectName);
        const auto productCode = parseUnsigned(object, "productCode", objectName);
        const auto revisionNumber = parseUnsigned(object, "revisionNumber", objectName);
        const auto serialNumber = parseUnsigned(object, "serialNumber", objectName);
        const auto alias = parseUnsigned(
            object, "alias", objectName, std::numeric_limits<quint16>::max());
        if (!vendorId || !productCode || !revisionNumber || !serialNumber || !alias) {
            const QString error = !vendorId          ? vendorId.error()
                                  : !productCode     ? productCode.error()
                                  : !revisionNumber ? revisionNumber.error()
                                  : !serialNumber   ? serialNumber.error()
                                                    : alias.error();
            return Utils::ResultError(error);
        }
        if (*vendorId == 0 || *productCode == 0) {
            return Utils::ResultError(
                Tr::tr("%1 requires non-zero Vendor ID and Product Code values.")
                    .arg(objectName));
        }

        Data::NodeId descriptionId;
        const QString descriptionIdText = object.value("deviceDescriptionId").toString();
        if (!descriptionIdText.isEmpty()) {
            descriptionId = Data::NodeId::fromString(descriptionIdText);
            if (descriptionId.isNull()) {
                return Utils::ResultError(
                    Tr::tr("%1 has an invalid device description ID.").arg(objectName));
            }
        }

        uniqueIds->insert(*id);
        positions.insert(int(*position));
        slaves.append({*id,
                       masterId,
                       int(*position),
                       {*vendorId, *productCode, *revisionNumber},
                       *serialNumber,
                       quint16(*alias),
                       *name,
                       descriptionId});
    }
    std::sort(slaves.begin(), slaves.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return slaves;
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

    const auto slaves = parseOfflineSlaves(masterObject, *masterId, &uniqueIds);
    if (!slaves)
        return Utils::ResultError(slaves.error());

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
        *slaves,
    };
    for (const Data::OfflineSlaveConfiguration &slave : *slaves) {
        snapshot.nodes.append(
            {slave.id, slave.masterId, Data::ProjectNodeKind::Slave, slave.name});
    }
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

    QJsonArray slaves;
    QList<Data::OfflineSlaveConfiguration> sortedSlaves = snapshot.slaves;
    std::sort(sortedSlaves.begin(), sortedSlaves.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(sortedSlaves)) {
        QJsonObject object;
        object.insert("id", slave.id.toString());
        object.insert("name", slave.name);
        object.insert("position", slave.position);
        object.insert("vendorId", double(slave.identity.vendorId));
        object.insert("productCode", double(slave.identity.productCode));
        object.insert("revisionNumber", double(slave.identity.revisionNumber));
        object.insert("serialNumber", double(slave.serialNumber));
        object.insert("alias", slave.alias);
        if (!slave.deviceDescriptionId.isNull())
            object.insert("deviceDescriptionId", slave.deviceDescriptionId.toString());
        slaves.append(object);
    }
    masterObject.insert("slaves", slaves);

    QJsonObject root;
    root.insert("format", QLatin1StringView(FORMAT_NAME));
    root.insert("formatVersion", Constants::CURRENT_FORMAT_VERSION);
    root.insert("project", projectObject);
    root.insert("target", targetObject);
    root.insert("master", masterObject);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace EtherCAT::Project::Internal
