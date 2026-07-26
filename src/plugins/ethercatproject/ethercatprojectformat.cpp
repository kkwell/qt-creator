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
#include <cmath>
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
    if (!std::isfinite(rawValue) || rawValue < 0 || rawValue > maximum
        || std::trunc(rawValue) != rawValue) {
        return Utils::ResultError(Tr::tr("%1 has an out-of-range '%2' value.").arg(objectName, key));
    }
    return quint32(rawValue);
}

static Utils::Result<qint64> parseSigned(
    const QJsonObject &object,
    const QString &key,
    const QString &objectName,
    qint64 minimum,
    qint64 maximum)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    const double rawValue = jsonValue.toDouble();
    constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
    if (!std::isfinite(rawValue) || rawValue < double(minimum) || rawValue > double(maximum)
        || rawValue < -double(maximumExactJsonInteger) || rawValue > double(maximumExactJsonInteger)
        || std::trunc(rawValue) != rawValue) {
        return Utils::ResultError(Tr::tr("%1 has an out-of-range '%2' value.").arg(objectName, key));
    }
    return qint64(rawValue);
}

static Utils::Result<bool> parseBool(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    return value.toBool();
}

static Utils::Result<QString> parseString(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' value.").arg(objectName, key));
    }
    return value.toString();
}

static Utils::Result<QJsonArray> parseArray(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' array.").arg(objectName, key));
    }
    return value.toArray();
}

static Utils::Result<QJsonObject> parseObject(
    const QJsonObject &object, const QString &key, const QString &objectName)
{
    const QJsonValue value = object.value(key);
    if (!value.isObject()) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid or missing '%2' object.").arg(objectName, key));
    }
    return value.toObject();
}

static Utils::Result<Data::NodeId> parseUniqueId(
    const QJsonObject &object,
    const QString &key,
    const QString &objectName,
    QSet<Data::NodeId> *uniqueIds)
{
    const auto id = parseRequiredId(object, key, objectName);
    if (!id)
        return Utils::ResultError(id.error());
    if (uniqueIds->contains(*id)) {
        return Utils::ResultError(
            Tr::tr("%1 reuses a stable ID that is already present in the project.").arg(objectName));
    }
    uniqueIds->insert(*id);
    return *id;
}

static QString dataTypeName(Data::EtherCATDataType dataType)
{
    switch (dataType) {
    case Data::EtherCATDataType::Unknown:
        return "unknown";
    case Data::EtherCATDataType::Boolean:
        return "boolean";
    case Data::EtherCATDataType::Integer8:
        return "integer8";
    case Data::EtherCATDataType::UnsignedInteger8:
        return "unsigned-integer8";
    case Data::EtherCATDataType::Integer16:
        return "integer16";
    case Data::EtherCATDataType::UnsignedInteger16:
        return "unsigned-integer16";
    case Data::EtherCATDataType::Integer32:
        return "integer32";
    case Data::EtherCATDataType::UnsignedInteger32:
        return "unsigned-integer32";
    case Data::EtherCATDataType::Integer64:
        return "integer64";
    case Data::EtherCATDataType::UnsignedInteger64:
        return "unsigned-integer64";
    case Data::EtherCATDataType::Real32:
        return "real32";
    case Data::EtherCATDataType::Real64:
        return "real64";
    case Data::EtherCATDataType::VisibleString:
        return "visible-string";
    case Data::EtherCATDataType::OctetString:
        return "octet-string";
    }
    return "unknown";
}

static Utils::Result<Data::EtherCATDataType> parseDataType(
    const QJsonObject &object, const QString &objectName)
{
    const auto value = parseString(object, "dataType", objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (*value == "unknown")
        return Data::EtherCATDataType::Unknown;
    if (*value == "boolean")
        return Data::EtherCATDataType::Boolean;
    if (*value == "integer8")
        return Data::EtherCATDataType::Integer8;
    if (*value == "unsigned-integer8")
        return Data::EtherCATDataType::UnsignedInteger8;
    if (*value == "integer16")
        return Data::EtherCATDataType::Integer16;
    if (*value == "unsigned-integer16")
        return Data::EtherCATDataType::UnsignedInteger16;
    if (*value == "integer32")
        return Data::EtherCATDataType::Integer32;
    if (*value == "unsigned-integer32")
        return Data::EtherCATDataType::UnsignedInteger32;
    if (*value == "integer64")
        return Data::EtherCATDataType::Integer64;
    if (*value == "unsigned-integer64")
        return Data::EtherCATDataType::UnsignedInteger64;
    if (*value == "real32")
        return Data::EtherCATDataType::Real32;
    if (*value == "real64")
        return Data::EtherCATDataType::Real64;
    if (*value == "visible-string")
        return Data::EtherCATDataType::VisibleString;
    if (*value == "octet-string")
        return Data::EtherCATDataType::OctetString;
    return Utils::ResultError(
        Tr::tr("%1 has an unsupported data type '%2'.").arg(objectName, *value));
}

static QString syncManagerDirectionName(Data::SyncManagerDirection direction)
{
    switch (direction) {
    case Data::SyncManagerDirection::Unknown:
        return "unknown";
    case Data::SyncManagerDirection::MasterToSlave:
        return "master-to-slave";
    case Data::SyncManagerDirection::SlaveToMaster:
        return "slave-to-master";
    }
    return "unknown";
}

static Utils::Result<Data::SyncManagerDirection> parseSyncManagerDirection(
    const QJsonObject &object, const QString &objectName)
{
    const auto value = parseString(object, "direction", objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (*value == "unknown")
        return Data::SyncManagerDirection::Unknown;
    if (*value == "master-to-slave")
        return Data::SyncManagerDirection::MasterToSlave;
    if (*value == "slave-to-master")
        return Data::SyncManagerDirection::SlaveToMaster;
    return Utils::ResultError(
        Tr::tr("%1 has an unsupported Sync Manager direction '%2'.").arg(objectName, *value));
}

static QString pdoDirectionName(Data::PdoDirection direction)
{
    return direction == Data::PdoDirection::Rx ? "rx" : "tx";
}

static Utils::Result<Data::PdoDirection> parsePdoDirection(
    const QJsonObject &object, const QString &objectName)
{
    const auto value = parseString(object, "direction", objectName);
    if (!value)
        return Utils::ResultError(value.error());
    if (*value == "rx")
        return Data::PdoDirection::Rx;
    if (*value == "tx")
        return Data::PdoDirection::Tx;
    return Utils::ResultError(
        Tr::tr("%1 has an unsupported PDO direction '%2'.").arg(objectName, *value));
}

static QString masterTimingModeName(Data::MasterTimingMode mode)
{
    switch (mode) {
    case Data::MasterTimingMode::Unassigned:
        return "unassigned";
    case Data::MasterTimingMode::FreeRun:
        return "free-run";
    case Data::MasterTimingMode::DistributedClocks:
        return "distributed-clocks";
    }
    return "unassigned";
}

static Utils::Result<Data::MasterConfiguration> parseMasterConfiguration(
    const QJsonObject &masterObject)
{
    const auto object = parseObject(masterObject, "configuration", Tr::tr("Master"));
    if (!object)
        return Utils::ResultError(object.error());
    const auto mode = parseString(*object, "timingMode", Tr::tr("Master configuration"));
    const auto cycle = parseUnsigned(*object, "cyclePeriodNs", Tr::tr("Master configuration"));
    if (!mode || !cycle)
        return Utils::ResultError(!mode ? mode.error() : cycle.error());

    Data::MasterTimingMode timingMode = Data::MasterTimingMode::Unassigned;
    if (*mode == "free-run") {
        timingMode = Data::MasterTimingMode::FreeRun;
    } else if (*mode == "distributed-clocks") {
        timingMode = Data::MasterTimingMode::DistributedClocks;
    } else if (*mode != "unassigned") {
        return Utils::ResultError(
            Tr::tr("Master configuration has an unsupported timing mode '%1'.").arg(*mode));
    }

    if (timingMode == Data::MasterTimingMode::Unassigned && *cycle != 0) {
        return Utils::ResultError(
            Tr::tr("An unassigned EtherCAT timing mode cannot have a cycle period."));
    }
    if (timingMode != Data::MasterTimingMode::Unassigned && *cycle == 0) {
        return Utils::ResultError(
            Tr::tr("FreeRun and Distributed Clocks require a non-zero cycle period."));
    }
    return Data::MasterConfiguration{timingMode, *cycle};
}

static Utils::Result<QList<Data::SyncManagerConfiguration>> parseSyncManagers(
    const QJsonObject &processDataObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto array = parseArray(processDataObject, "syncManagers", slaveName);
    if (!array)
        return Utils::ResultError(array.error());

    QList<Data::SyncManagerConfiguration> syncManagers;
    syncManagers.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Sync Manager %1 for '%2' must be a JSON object.").arg(index).arg(slaveName));
        }
        const QJsonObject object = array->at(index).toObject();
        const QString objectName = Tr::tr("Sync Manager %1 for '%2'").arg(index).arg(slaveName);
        const auto id = parseUniqueId(object, "id", objectName, uniqueIds);
        const auto managerIndex
            = parseUnsigned(object, "index", objectName, std::numeric_limits<int>::max());
        const auto name = parseString(object, "name", objectName);
        const auto direction = parseSyncManagerDirection(object, objectName);
        const auto enabled = parseBool(object, "enabled", objectName);
        const auto sizeLimit
            = parseUnsigned(object, "sizeLimitBytes", objectName, std::numeric_limits<int>::max());
        if (!id || !managerIndex || !name || !direction || !enabled || !sizeLimit) {
            const QString error = !id             ? id.error()
                                  : !managerIndex ? managerIndex.error()
                                  : !name         ? name.error()
                                  : !direction    ? direction.error()
                                  : !enabled      ? enabled.error()
                                                  : sizeLimit.error();
            return Utils::ResultError(error);
        }
        syncManagers.append({*id, int(*managerIndex), *name, *direction, *enabled, int(*sizeLimit)});
    }
    return syncManagers;
}

static Utils::Result<QList<Data::PdoEntryConfiguration>> parsePdoEntries(
    const QJsonObject &pdoObject, const QString &pdoName, QSet<Data::NodeId> *uniqueIds)
{
    const auto array = parseArray(pdoObject, "entries", pdoName);
    if (!array)
        return Utils::ResultError(array.error());

    QList<Data::PdoEntryConfiguration> entries;
    entries.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("PDO entry %1 in %2 must be a JSON object.").arg(index).arg(pdoName));
        }
        const QJsonObject object = array->at(index).toObject();
        const QString objectName = Tr::tr("PDO entry %1 in %2").arg(index).arg(pdoName);
        const auto id = parseUniqueId(object, "id", objectName, uniqueIds);
        const auto objectIndex
            = parseUnsigned(object, "index", objectName, std::numeric_limits<quint16>::max());
        const auto subIndex
            = parseUnsigned(object, "subIndex", objectName, std::numeric_limits<quint8>::max());
        const auto name = parseString(object, "name", objectName);
        const auto bitLength
            = parseUnsigned(object, "bitLength", objectName, std::numeric_limits<int>::max());
        const auto dataType = parseDataType(object, objectName);
        const auto rawDataType = parseString(object, "rawDataType", objectName);
        constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
        const auto requestedBitOffset
            = parseSigned(object, "requestedBitOffset", objectName, -1, maximumExactJsonInteger);
        const auto mappingSupported = parseBool(object, "mappingSupported", objectName);
        const auto padding = parseBool(object, "padding", objectName);
        if (!id || !objectIndex || !subIndex || !name || !bitLength || !dataType || !rawDataType
            || !requestedBitOffset || !mappingSupported || !padding) {
            const QString error = !id                   ? id.error()
                                  : !objectIndex        ? objectIndex.error()
                                  : !subIndex           ? subIndex.error()
                                  : !name               ? name.error()
                                  : !bitLength          ? bitLength.error()
                                  : !dataType           ? dataType.error()
                                  : !rawDataType        ? rawDataType.error()
                                  : !requestedBitOffset ? requestedBitOffset.error()
                                  : !mappingSupported   ? mappingSupported.error()
                                                        : padding.error();
            return Utils::ResultError(error);
        }
        entries.append(
            {*id,
             quint16(*objectIndex),
             quint8(*subIndex),
             *name,
             int(*bitLength),
             *dataType,
             *rawDataType,
             *requestedBitOffset,
             *mappingSupported,
             *padding});
    }
    return entries;
}

static Utils::Result<QList<Data::PdoConfiguration>> parsePdos(
    const QJsonObject &processDataObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto array = parseArray(processDataObject, "pdos", slaveName);
    if (!array)
        return Utils::ResultError(array.error());

    QList<Data::PdoConfiguration> pdos;
    pdos.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("PDO %1 for '%2' must be a JSON object.").arg(index).arg(slaveName));
        }
        const QJsonObject object = array->at(index).toObject();
        const QString objectName = Tr::tr("PDO %1 for '%2'").arg(index).arg(slaveName);
        const auto id = parseUniqueId(object, "id", objectName, uniqueIds);
        const auto pdoIndex
            = parseUnsigned(object, "index", objectName, std::numeric_limits<quint16>::max());
        const auto name = parseString(object, "name", objectName);
        const auto direction = parsePdoDirection(object, objectName);
        const auto syncManager
            = parseSigned(object, "syncManager", objectName, -1, std::numeric_limits<int>::max());
        const auto selected = parseBool(object, "selected", objectName);
        const auto fixed = parseBool(object, "fixed", objectName);
        const auto mandatory = parseBool(object, "mandatory", objectName);
        const auto defaultSelected = parseBool(object, "defaultSelected", objectName);
        const auto mappingSupported = parseBool(object, "mappingSupported", objectName);
        const auto predefinedGroup = parseString(object, "predefinedGroup", objectName);
        const auto entries = parsePdoEntries(object, objectName, uniqueIds);
        if (!id || !pdoIndex || !name || !direction || !syncManager || !selected || !fixed
            || !mandatory || !defaultSelected || !mappingSupported || !predefinedGroup
            || !entries) {
            const QString error = !id                 ? id.error()
                                  : !pdoIndex         ? pdoIndex.error()
                                  : !name             ? name.error()
                                  : !direction        ? direction.error()
                                  : !syncManager      ? syncManager.error()
                                  : !selected         ? selected.error()
                                  : !fixed            ? fixed.error()
                                  : !mandatory        ? mandatory.error()
                                  : !defaultSelected  ? defaultSelected.error()
                                  : !mappingSupported ? mappingSupported.error()
                                  : !predefinedGroup  ? predefinedGroup.error()
                                                      : entries.error();
            return Utils::ResultError(error);
        }
        pdos.append(
            {*id,
             quint16(*pdoIndex),
             *name,
             *direction,
             int(*syncManager),
             *selected,
             *fixed,
             *mandatory,
             *defaultSelected,
             *mappingSupported,
             *predefinedGroup,
             *entries});
    }
    return pdos;
}

static Utils::Result<Data::ProcessDataConfiguration> parseProcessDataConfiguration(
    const QJsonObject &configurationObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto object = parseObject(configurationObject, "processData", slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    const auto syncManagers = parseSyncManagers(*object, slaveName, uniqueIds);
    if (!syncManagers)
        return Utils::ResultError(syncManagers.error());
    const auto pdos = parsePdos(*object, slaveName, uniqueIds);
    if (!pdos)
        return Utils::ResultError(pdos.error());

    const Data::ProcessDataConfiguration processData{*syncManagers, *pdos};
    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        processData);
    const auto error = std::find_if(
        validation.issues.cbegin(),
        validation.issues.cend(),
        [](const Data::ConfigurationIssue &issue) {
            return issue.severity == Data::ConfigurationIssueSeverity::Error;
        });
    if (error != validation.issues.cend()) {
        return Utils::ResultError(
            Tr::tr("Process Data for '%1' is invalid: %2").arg(slaveName, error->message));
    }
    return processData;
}

static bool isHexString(const QString &value)
{
    return value.size() % 2 == 0 && std::all_of(value.cbegin(), value.cend(), [](QChar character) {
               const ushort codePoint = character.unicode();
               return (codePoint >= '0' && codePoint <= '9')
                      || (codePoint >= 'a' && codePoint <= 'f')
                      || (codePoint >= 'A' && codePoint <= 'F');
           });
}

static Utils::Result<Data::StartupConfiguration> parseStartupConfiguration(
    const QJsonObject &configurationObject, const QString &slaveName, QSet<Data::NodeId> *uniqueIds)
{
    const auto object = parseObject(configurationObject, "startup", slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    const auto array = parseArray(*object, "parameters", slaveName);
    if (!array)
        return Utils::ResultError(array.error());

    Data::StartupConfiguration startup;
    startup.parameters.reserve(array->size());
    for (qsizetype index = 0; index < array->size(); ++index) {
        if (!array->at(index).isObject()) {
            return Utils::ResultError(
                Tr::tr("Startup parameter %1 for '%2' must be a JSON object.")
                    .arg(index)
                    .arg(slaveName));
        }
        const QJsonObject parameterObject = array->at(index).toObject();
        const QString objectName = Tr::tr("Startup parameter %1 for '%2'").arg(index).arg(slaveName);
        const auto id = parseUniqueId(parameterObject, "id", objectName, uniqueIds);
        const auto enabled = parseBool(parameterObject, "enabled", objectName);
        const auto order
            = parseSigned(parameterObject, "order", objectName, -1, std::numeric_limits<int>::max());
        const auto transition = parseString(parameterObject, "transition", objectName);
        const auto objectIndex = parseUnsigned(
            parameterObject, "index", objectName, std::numeric_limits<quint16>::max());
        const auto subIndex = parseUnsigned(
            parameterObject, "subIndex", objectName, std::numeric_limits<quint8>::max());
        const auto dataType = parseDataType(parameterObject, objectName);
        const auto rawDataType = parseString(parameterObject, "rawDataType", objectName);
        const auto rawValue = parseString(parameterObject, "rawValueHex", objectName);
        const auto comment = parseString(parameterObject, "comment", objectName);
        if (!id || !enabled || !order || !transition || !objectIndex || !subIndex || !dataType
            || !rawDataType || !rawValue || !comment) {
            const QString error = !id            ? id.error()
                                  : !enabled     ? enabled.error()
                                  : !order       ? order.error()
                                  : !transition  ? transition.error()
                                  : !objectIndex ? objectIndex.error()
                                  : !subIndex    ? subIndex.error()
                                  : !dataType    ? dataType.error()
                                  : !rawDataType ? rawDataType.error()
                                  : !rawValue    ? rawValue.error()
                                                 : comment.error();
            return Utils::ResultError(error);
        }
        if (!isHexString(*rawValue)) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid hexadecimal raw value.").arg(objectName));
        }
        startup.parameters.append(
            {*id,
             *enabled,
             int(*order),
             *transition,
             quint16(*objectIndex),
             quint8(*subIndex),
             *dataType,
             *rawDataType,
             QByteArray::fromHex(rawValue->toLatin1()),
             *comment});
    }

    const QList<Data::ConfigurationIssue> issues = Data::validateStartupConfiguration(startup);
    const auto error
        = std::find_if(issues.cbegin(), issues.cend(), [](const Data::ConfigurationIssue &issue) {
              return issue.severity == Data::ConfigurationIssueSeverity::Error;
          });
    if (error != issues.cend()) {
        return Utils::ResultError(
            Tr::tr("Startup for '%1' is invalid: %2").arg(slaveName, error->message));
    }
    return startup;
}

static Utils::Result<Data::DcSignalConfiguration> parseDcSignal(
    const QJsonObject &dcObject, const QString &key, const QString &slaveName)
{
    const auto object = parseObject(dcObject, key, slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    const QString objectName = key.toUpper() + Tr::tr(" for '%1'").arg(slaveName);
    const auto enabled = parseBool(*object, "enabled", objectName);
    constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
    const auto cycle = parseSigned(
        *object, "cycleTimeNs", objectName, -maximumExactJsonInteger, maximumExactJsonInteger);
    const auto shift = parseSigned(
        *object, "shiftTimeNs", objectName, -maximumExactJsonInteger, maximumExactJsonInteger);
    if (!enabled || !cycle || !shift) {
        const QString error = !enabled ? enabled.error() : !cycle ? cycle.error() : shift.error();
        return Utils::ResultError(error);
    }
    return Data::DcSignalConfiguration{*enabled, *cycle, *shift};
}

static Utils::Result<Data::DcConfiguration> parseDcConfiguration(
    const QJsonObject &configurationObject, const QString &slaveName)
{
    const auto object = parseObject(configurationObject, "dc", slaveName);
    if (!object)
        return Utils::ResultError(object.error());
    const auto enabled = parseBool(*object, "enabled", slaveName);
    const auto modeName = parseString(*object, "modeName", slaveName);
    const auto assignActivate
        = parseUnsigned(*object, "assignActivate", slaveName, std::numeric_limits<quint16>::max());
    const auto sync0 = parseDcSignal(*object, "sync0", slaveName);
    const auto sync1 = parseDcSignal(*object, "sync1", slaveName);
    const auto potentialReferenceClock = parseBool(*object, "potentialReferenceClock", slaveName);
    if (!enabled || !modeName || !assignActivate || !sync0 || !sync1 || !potentialReferenceClock) {
        const QString error = !enabled          ? enabled.error()
                              : !modeName       ? modeName.error()
                              : !assignActivate ? assignActivate.error()
                              : !sync0          ? sync0.error()
                              : !sync1          ? sync1.error()
                                                : potentialReferenceClock.error();
        return Utils::ResultError(error);
    }
    const Data::DcConfiguration
        dc{*enabled, *modeName, quint16(*assignActivate), *sync0, *sync1, *potentialReferenceClock};
    const QList<Data::ConfigurationIssue> issues = Data::validateDcConfiguration(dc);
    const auto error
        = std::find_if(issues.cbegin(), issues.cend(), [](const Data::ConfigurationIssue &issue) {
              return issue.severity == Data::ConfigurationIssueSeverity::Error;
          });
    if (error != issues.cend()) {
        return Utils::ResultError(
            Tr::tr("DC for '%1' is invalid: %2").arg(slaveName, error->message));
    }
    return dc;
}

static Utils::Result<QList<Data::OfflineSlaveConfiguration>> parseOfflineSlaves(
    const QJsonObject &masterObject,
    const Data::NodeId &masterId,
    QSet<Data::NodeId> *uniqueIds,
    bool parseConfiguration)
{
    const QJsonValue slavesValue = masterObject.value("slaves");
    if (slavesValue.isUndefined() && !parseConfiguration)
        return QList<Data::OfflineSlaveConfiguration>();
    if (!slavesValue.isArray())
        return Utils::ResultError(Tr::tr("Master 'slaves' must be a JSON array."));

    QList<Data::OfflineSlaveConfiguration> slaves;
    QSet<int> positions;
    const QJsonArray array = slavesValue.toArray();
    slaves.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            return Utils::ResultError(Tr::tr("Offline slave %1 must be a JSON object.").arg(index));
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
        const auto position
            = parseUnsigned(object, "position", objectName, std::numeric_limits<int>::max());
        if (!position)
            return Utils::ResultError(position.error());
        if (positions.contains(int(*position)))
            return Utils::ResultError(Tr::tr("Offline slave positions must be unique."));
        const auto vendorId = parseUnsigned(object, "vendorId", objectName);
        const auto productCode = parseUnsigned(object, "productCode", objectName);
        const auto revisionNumber = parseUnsigned(object, "revisionNumber", objectName);
        const auto serialNumber = parseUnsigned(object, "serialNumber", objectName);
        const auto alias
            = parseUnsigned(object, "alias", objectName, std::numeric_limits<quint16>::max());
        if (!vendorId || !productCode || !revisionNumber || !serialNumber || !alias) {
            const QString error = !vendorId         ? vendorId.error()
                                  : !productCode    ? productCode.error()
                                  : !revisionNumber ? revisionNumber.error()
                                  : !serialNumber   ? serialNumber.error()
                                                    : alias.error();
            return Utils::ResultError(error);
        }
        if (*vendorId == 0 || *productCode == 0) {
            return Utils::ResultError(
                Tr::tr("%1 requires non-zero Vendor ID and Product Code values.").arg(objectName));
        }

        Data::NodeId descriptionId;
        const QJsonValue descriptionIdValue = object.value("deviceDescriptionId");
        if (!descriptionIdValue.isUndefined() && !descriptionIdValue.isString()) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid device description ID.").arg(objectName));
        }
        const QString descriptionIdText = descriptionIdValue.toString();
        if (!descriptionIdText.isEmpty()) {
            descriptionId = Data::NodeId::fromString(descriptionIdText);
            if (descriptionId.isNull()) {
                return Utils::ResultError(
                    Tr::tr("%1 has an invalid device description ID.").arg(objectName));
            }
        }
        uniqueIds->insert(*id);

        Data::ProcessDataConfiguration processData;
        Data::StartupConfiguration startup;
        Data::DcConfiguration dc;
        if (parseConfiguration) {
            const auto configuration = parseObject(object, "configuration", objectName);
            if (!configuration)
                return Utils::ResultError(configuration.error());
            const auto parsedProcessData
                = parseProcessDataConfiguration(*configuration, *name, uniqueIds);
            if (!parsedProcessData)
                return Utils::ResultError(parsedProcessData.error());
            const auto parsedStartup = parseStartupConfiguration(*configuration, *name, uniqueIds);
            if (!parsedStartup)
                return Utils::ResultError(parsedStartup.error());
            const auto parsedDc = parseDcConfiguration(*configuration, *name);
            if (!parsedDc)
                return Utils::ResultError(parsedDc.error());
            processData = *parsedProcessData;
            startup = *parsedStartup;
            dc = *parsedDc;
        }

        positions.insert(int(*position));
        slaves.append(
            {*id,
             masterId,
             int(*position),
             {*vendorId, *productCode, *revisionNumber},
             *serialNumber,
             quint16(*alias),
             *name,
             descriptionId,
             processData,
             startup,
             dc});
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
    return LoadedProject{snapshot, true, 0};
}

Utils::Result<LoadedProject> parseProject(const QByteArray &contents, const QString &fallbackName)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return Utils::ResultError(
            Tr::tr("Invalid JSON at offset %1: %2")
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
    if (version != 1 && version != 2 && version != Constants::CURRENT_FORMAT_VERSION) {
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

    Data::MasterConfiguration masterConfiguration;
    if (version == Constants::CURRENT_FORMAT_VERSION) {
        const auto parsedMasterConfiguration = parseMasterConfiguration(masterObject);
        if (!parsedMasterConfiguration)
            return Utils::ResultError(parsedMasterConfiguration.error());
        masterConfiguration = *parsedMasterConfiguration;
    }

    const auto slaves = parseOfflineSlaves(masterObject, *masterId, &uniqueIds, version >= 2);
    if (!slaves)
        return Utils::ResultError(slaves.error());

    const bool migrationRequired = version != Constants::CURRENT_FORMAT_VERSION;
    Data::ProjectSnapshot snapshot{
        *projectId,
        *projectName,
        Constants::CURRENT_FORMAT_VERSION,
        projectObject.value("createdBy").toString(),
        {{*projectId, {}, Data::ProjectNodeKind::Project, *projectName},
         {*targetId, *projectId, Data::ProjectNodeKind::Target, *targetName},
         {*masterId, *targetId, Data::ProjectNodeKind::Master, *masterName}},
        false,
        true,
        migrationRequired,
        {},
        *slaves,
        masterConfiguration,
    };
    for (const Data::OfflineSlaveConfiguration &slave : *slaves) {
        snapshot.nodes.append({slave.id, slave.masterId, Data::ProjectNodeKind::Slave, slave.name});
    }
    return LoadedProject{snapshot, migrationRequired, version};
}

static QJsonObject serializeProcessData(const Data::ProcessDataConfiguration &processData)
{
    QJsonArray syncManagers;
    for (const Data::SyncManagerConfiguration &syncManager : processData.syncManagers) {
        QJsonObject object;
        object.insert("id", syncManager.id.toString());
        object.insert("index", syncManager.index);
        object.insert("name", syncManager.name);
        object.insert("direction", syncManagerDirectionName(syncManager.direction));
        object.insert("enabled", syncManager.enabled);
        object.insert("sizeLimitBytes", syncManager.sizeLimitBytes);
        syncManagers.append(object);
    }

    QJsonArray pdos;
    for (const Data::PdoConfiguration &pdo : processData.pdos) {
        QJsonArray entries;
        for (const Data::PdoEntryConfiguration &entry : pdo.entries) {
            QJsonObject object;
            object.insert("id", entry.id.toString());
            object.insert("index", entry.index);
            object.insert("subIndex", entry.subIndex);
            object.insert("name", entry.name);
            object.insert("bitLength", entry.bitLength);
            object.insert("dataType", dataTypeName(entry.dataType));
            object.insert("rawDataType", entry.rawDataType);
            object.insert("requestedBitOffset", double(entry.requestedBitOffset));
            object.insert("mappingSupported", entry.mappingSupported);
            object.insert("padding", entry.padding);
            entries.append(object);
        }

        QJsonObject object;
        object.insert("id", pdo.id.toString());
        object.insert("index", pdo.index);
        object.insert("name", pdo.name);
        object.insert("direction", pdoDirectionName(pdo.direction));
        object.insert("syncManager", pdo.syncManager);
        object.insert("selected", pdo.selected);
        object.insert("fixed", pdo.fixed);
        object.insert("mandatory", pdo.mandatory);
        object.insert("defaultSelected", pdo.defaultSelected);
        object.insert("mappingSupported", pdo.mappingSupported);
        object.insert("predefinedGroup", pdo.predefinedGroup);
        object.insert("entries", entries);
        pdos.append(object);
    }

    QJsonObject object;
    object.insert("syncManagers", syncManagers);
    object.insert("pdos", pdos);
    return object;
}

static QJsonObject serializeStartup(const Data::StartupConfiguration &startup)
{
    QJsonArray parameters;
    for (const Data::StartupParameterConfiguration &parameter : startup.parameters) {
        QJsonObject object;
        object.insert("id", parameter.id.toString());
        object.insert("enabled", parameter.enabled);
        object.insert("order", parameter.order);
        object.insert("transition", parameter.transition);
        object.insert("index", parameter.index);
        object.insert("subIndex", parameter.subIndex);
        object.insert("dataType", dataTypeName(parameter.dataType));
        object.insert("rawDataType", parameter.rawDataType);
        object.insert("rawValueHex", QString::fromLatin1(parameter.rawValue.toHex()));
        object.insert("comment", parameter.comment);
        parameters.append(object);
    }
    QJsonObject object;
    object.insert("parameters", parameters);
    return object;
}

static QJsonObject serializeDcSignal(const Data::DcSignalConfiguration &signal)
{
    QJsonObject object;
    object.insert("enabled", signal.enabled);
    object.insert("cycleTimeNs", double(signal.cycleTimeNs));
    object.insert("shiftTimeNs", double(signal.shiftTimeNs));
    return object;
}

static QJsonObject serializeDc(const Data::DcConfiguration &dc)
{
    QJsonObject object;
    object.insert("enabled", dc.enabled);
    object.insert("modeName", dc.modeName);
    object.insert("assignActivate", dc.assignActivate);
    object.insert("sync0", serializeDcSignal(dc.sync0));
    object.insert("sync1", serializeDcSignal(dc.sync1));
    object.insert("potentialReferenceClock", dc.potentialReferenceClock);
    return object;
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
        QJsonObject configuration;
        configuration.insert("processData", serializeProcessData(slave.processData));
        configuration.insert("startup", serializeStartup(slave.startup));
        configuration.insert("dc", serializeDc(slave.dc));
        object.insert("configuration", configuration);
        slaves.append(object);
    }
    QJsonObject masterConfiguration;
    masterConfiguration.insert(
        "timingMode", masterTimingModeName(snapshot.masterConfiguration.timingMode));
    masterConfiguration.insert("cyclePeriodNs", double(snapshot.masterConfiguration.cyclePeriodNs));
    masterObject.insert("configuration", masterConfiguration);
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
