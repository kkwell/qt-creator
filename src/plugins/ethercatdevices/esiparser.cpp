// Copyright (C) 2026 Kvell

#include "esiparser.h"

#include "ethercatdevicestr.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>

#include <limits>
#include <optional>
#include <utility>

namespace EtherCAT::Devices::Internal {

struct XmlElement
{
    QString name;
    QString namespaceUri;
    QHash<QString, QString> attributes;
    QString text;
    QList<XmlElement> children;
};

static XmlElement readElement(QXmlStreamReader &reader)
{
    XmlElement element;
    element.name = reader.name().toString();
    element.namespaceUri = reader.namespaceUri().toString();
    for (const QXmlStreamAttribute &attribute : reader.attributes())
        element.attributes.insert(attribute.name().toString(), attribute.value().toString());

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            element.children.append(readElement(reader));
        } else if (token == QXmlStreamReader::Characters
                   || token == QXmlStreamReader::EntityReference) {
            element.text += reader.text();
        } else if (token == QXmlStreamReader::EndElement) {
            break;
        }
    }
    element.text = element.text.trimmed();
    return element;
}

static const XmlElement *child(const XmlElement &element, const QString &name)
{
    for (const XmlElement &candidate : element.children) {
        if (candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            return &candidate;
    }
    return nullptr;
}

static QList<const XmlElement *> children(const XmlElement &element, const QString &name)
{
    QList<const XmlElement *> result;
    for (const XmlElement &candidate : element.children) {
        if (candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            result.append(&candidate);
    }
    return result;
}

static void collectDescendants(
    const XmlElement &element, const QString &name, QList<const XmlElement *> *result)
{
    for (const XmlElement &candidate : element.children) {
        if (candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            result->append(&candidate);
        collectDescendants(candidate, name, result);
    }
}

static QList<const XmlElement *> descendants(const XmlElement &element, const QString &name)
{
    QList<const XmlElement *> result;
    collectDescendants(element, name, &result);
    return result;
}

static QString attribute(const XmlElement &element, const QString &name)
{
    for (auto it = element.attributes.cbegin(); it != element.attributes.cend(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return it.value().trimmed();
    }
    return {};
}

static QString childText(const XmlElement &element, const QString &name)
{
    if (const XmlElement *value = child(element, name))
        return value->text.trimmed();
    return {};
}

static QString localizedChildText(const XmlElement &element, const QString &name)
{
    const QList<const XmlElement *> values = children(element, name);
    for (const QString &preferredLanguage : {QString("1033"), QString("1031")}) {
        for (const XmlElement *value : values) {
            if (attribute(*value, "LcId") == preferredLanguage && !value->text.trimmed().isEmpty())
                return value->text.trimmed();
        }
    }
    for (const XmlElement *value : values) {
        if (!value->text.trimmed().isEmpty())
            return value->text.trimmed();
    }
    return {};
}

static bool parseUnsigned(const QString &input, quint64 maximum, quint64 *value)
{
    QString text = input.trimmed();
    int base = 10;
    if (text.startsWith("#x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    } else if (text.startsWith("0x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    }
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, base);
    if (!ok || parsed > maximum)
        return false;
    *value = parsed;
    return true;
}

static std::optional<quint64> parsedUnsigned(const QString &text, quint64 maximum)
{
    quint64 value = 0;
    if (!parseUnsigned(text, maximum, &value))
        return std::nullopt;
    return value;
}

static bool parseBoolean(const QString &text, bool defaultValue = false)
{
    if (text.isEmpty())
        return defaultValue;
    return text.compare("true", Qt::CaseInsensitive) == 0 || text == "1"
           || text.compare("yes", Qt::CaseInsensitive) == 0;
}

static QByteArray parseHexData(QString text, bool *ok)
{
    text = text.trimmed();
    text.remove("#x", Qt::CaseInsensitive);
    text.remove("0x", Qt::CaseInsensitive);
    text.remove(' ');
    text.remove('\t');
    text.remove('\r');
    text.remove('\n');
    if (text.isEmpty()) {
        *ok = true;
        return {};
    }
    for (const QChar character : std::as_const(text)) {
        if (!character.isDigit()
            && !(character.toLower() >= QLatin1Char('a')
                 && character.toLower() <= QLatin1Char('f'))) {
            *ok = false;
            return {};
        }
    }
    if (text.size() % 2 != 0)
        text.prepend('0');
    *ok = true;
    return QByteArray::fromHex(text.toLatin1());
}

static Data::EtherCATDataType dataType(const QString &rawType)
{
    QString type = rawType.trimmed().toUpper();
    type.remove(' ');
    type.remove('_');
    static const QHash<QString, Data::EtherCATDataType> types = {
        {"BOOL", Data::EtherCATDataType::Boolean},
        {"BOOLEAN", Data::EtherCATDataType::Boolean},
        {"BIT", Data::EtherCATDataType::Boolean},
        {"SINT", Data::EtherCATDataType::Integer8},
        {"INT8", Data::EtherCATDataType::Integer8},
        {"INTEGER8", Data::EtherCATDataType::Integer8},
        {"USINT", Data::EtherCATDataType::UnsignedInteger8},
        {"UINT8", Data::EtherCATDataType::UnsignedInteger8},
        {"UNSIGNED8", Data::EtherCATDataType::UnsignedInteger8},
        {"BYTE", Data::EtherCATDataType::UnsignedInteger8},
        {"BITARR8", Data::EtherCATDataType::UnsignedInteger8},
        {"INT", Data::EtherCATDataType::Integer16},
        {"INT16", Data::EtherCATDataType::Integer16},
        {"INTEGER16", Data::EtherCATDataType::Integer16},
        {"UINT", Data::EtherCATDataType::UnsignedInteger16},
        {"UINT16", Data::EtherCATDataType::UnsignedInteger16},
        {"UNSIGNED16", Data::EtherCATDataType::UnsignedInteger16},
        {"WORD", Data::EtherCATDataType::UnsignedInteger16},
        {"BITARR16", Data::EtherCATDataType::UnsignedInteger16},
        {"DINT", Data::EtherCATDataType::Integer32},
        {"INT32", Data::EtherCATDataType::Integer32},
        {"INTEGER32", Data::EtherCATDataType::Integer32},
        {"UDINT", Data::EtherCATDataType::UnsignedInteger32},
        {"UINT32", Data::EtherCATDataType::UnsignedInteger32},
        {"UNSIGNED32", Data::EtherCATDataType::UnsignedInteger32},
        {"DWORD", Data::EtherCATDataType::UnsignedInteger32},
        {"BITARR32", Data::EtherCATDataType::UnsignedInteger32},
        {"LINT", Data::EtherCATDataType::Integer64},
        {"INT64", Data::EtherCATDataType::Integer64},
        {"INTEGER64", Data::EtherCATDataType::Integer64},
        {"ULINT", Data::EtherCATDataType::UnsignedInteger64},
        {"UINT64", Data::EtherCATDataType::UnsignedInteger64},
        {"UNSIGNED64", Data::EtherCATDataType::UnsignedInteger64},
        {"LWORD", Data::EtherCATDataType::UnsignedInteger64},
        {"REAL", Data::EtherCATDataType::Real32},
        {"REAL32", Data::EtherCATDataType::Real32},
        {"LREAL", Data::EtherCATDataType::Real64},
        {"REAL64", Data::EtherCATDataType::Real64},
        {"STRING", Data::EtherCATDataType::VisibleString},
        {"VISIBLESTRING", Data::EtherCATDataType::VisibleString},
        {"OCTETSTRING", Data::EtherCATDataType::OctetString},
    };
    if (type.startsWith("STRING(") || type.startsWith("VISIBLESTRING("))
        return Data::EtherCATDataType::VisibleString;
    if (type.startsWith("OCTETSTRING("))
        return Data::EtherCATDataType::OctetString;
    return types.value(type, Data::EtherCATDataType::Unknown);
}

static Data::NodeId stableDeviceId(const Data::DeviceIdentity &identity)
{
    static const QUuid namespaceId("{3495c5f7-84e9-5cc5-bf13-b919185cf2d1}");
    const QByteArray key = QByteArray::number(identity.vendorId) + ':'
                           + QByteArray::number(identity.productCode) + ':'
                           + QByteArray::number(identity.revisionNumber);
    return Data::NodeId::fromString(QUuid::createUuidV5(namespaceId, key).toString());
}

static Data::PdoEntryDescription parsePdoEntry(
    const XmlElement &element, QStringList *warnings)
{
    Data::PdoEntryDescription entry;
    if (const auto index = parsedUnsigned(childText(element, "Index"), 0xffff))
        entry.index = quint16(*index);
    if (const auto subIndex = parsedUnsigned(childText(element, "SubIndex"), 0xff))
        entry.subIndex = quint8(*subIndex);
    if (const auto bitLength = parsedUnsigned(childText(element, "BitLen"), std::numeric_limits<int>::max()))
        entry.bitLength = int(*bitLength);
    entry.name = localizedChildText(element, "Name");
    entry.rawDataType = childText(element, "DataType");
    entry.dataType = dataType(entry.rawDataType);
    if (!entry.rawDataType.isEmpty() && entry.dataType == Data::EtherCATDataType::Unknown) {
        warnings->append(Tr::tr("PDO entry %1 uses unsupported data type %2.")
                             .arg(entry.name.isEmpty() ? QString::number(entry.index, 16) : entry.name,
                                  entry.rawDataType));
    }
    return entry;
}

static Data::PdoDescription parsePdo(
    const XmlElement &element, Data::PdoDirection direction, QStringList *warnings)
{
    Data::PdoDescription pdo;
    pdo.direction = direction;
    if (const auto index = parsedUnsigned(childText(element, "Index"), 0xffff))
        pdo.index = quint16(*index);
    pdo.name = localizedChildText(element, "Name");
    if (const auto syncManager = parsedUnsigned(attribute(element, "Sm"), std::numeric_limits<int>::max()))
        pdo.syncManager = int(*syncManager);
    pdo.fixed = parseBoolean(attribute(element, "Fixed"));
    pdo.mandatory = parseBoolean(attribute(element, "Mandatory"));
    for (const XmlElement *entry : children(element, "Entry"))
        pdo.entries.append(parsePdoEntry(*entry, warnings));
    return pdo;
}

static Data::SyncManagerDescription parseSyncManager(const XmlElement &element, int index)
{
    Data::SyncManagerDescription syncManager;
    syncManager.index = index;
    syncManager.name = element.text.trimmed();
    if (const auto value = parsedUnsigned(attribute(element, "StartAddress"), 0xffff))
        syncManager.startAddress = quint16(*value);
    if (const auto value = parsedUnsigned(attribute(element, "DefaultSize"), 0xffff))
        syncManager.defaultSize = quint16(*value);
    if (const auto value = parsedUnsigned(attribute(element, "ControlByte"), 0xff))
        syncManager.controlByte = quint8(*value);
    syncManager.enabled = parseBoolean(attribute(element, "Enable"));

    if (syncManager.name.contains("out", Qt::CaseInsensitive)
        || syncManager.name.contains("output", Qt::CaseInsensitive)) {
        syncManager.direction = Data::SyncManagerDirection::MasterToSlave;
    } else if (syncManager.name.contains("in", Qt::CaseInsensitive)
               || syncManager.name.contains("input", Qt::CaseInsensitive)) {
        syncManager.direction = Data::SyncManagerDirection::SlaveToMaster;
    }
    return syncManager;
}

static Data::StartupParameterDescription parseStartupParameter(
    const XmlElement &element, QStringList *warnings)
{
    Data::StartupParameterDescription parameter;
    parameter.transition = childText(element, "Transition");
    if (const auto index = parsedUnsigned(childText(element, "Index"), 0xffff))
        parameter.index = quint16(*index);
    if (const auto subIndex = parsedUnsigned(childText(element, "SubIndex"), 0xff))
        parameter.subIndex = quint8(*subIndex);
    bool validData = false;
    parameter.data = parseHexData(childText(element, "Data"), &validData);
    if (!validData)
        warnings->append(Tr::tr("Startup parameter %1:%2 contains invalid hexadecimal data.")
                             .arg(parameter.index, 4, 16, QLatin1Char('0'))
                             .arg(parameter.subIndex, 2, 16, QLatin1Char('0')));
    parameter.comment = localizedChildText(element, "Comment");
    if (parameter.comment.isEmpty())
        parameter.comment = childText(element, "Comment");
    return parameter;
}

static qint64 parseSignedTiming(const XmlElement &element, const QString &name)
{
    const XmlElement *timing = child(element, name);
    if (!timing)
        return 0;
    bool ok = false;
    const qint64 value = timing->text.trimmed().toLongLong(&ok, 10);
    return ok ? value : 0;
}

static Data::DcModeDescription parseDcMode(const XmlElement &element, QStringList *warnings)
{
    Data::DcModeDescription mode;
    mode.name = localizedChildText(element, "Name");
    if (const auto value = parsedUnsigned(childText(element, "AssignActivate"), 0xffff))
        mode.assignActivate = quint16(*value);
    mode.cycleTimeSync0Ns = parseSignedTiming(element, "CycleTimeSync0");
    mode.shiftTimeSync0Ns = parseSignedTiming(element, "ShiftTimeSync0");
    mode.cycleTimeSync1Ns = parseSignedTiming(element, "CycleTimeSync1");
    mode.shiftTimeSync1Ns = parseSignedTiming(element, "ShiftTimeSync1");
    const QStringList timingNames = {
        "CycleTimeSync0", "ShiftTimeSync0", "CycleTimeSync1", "ShiftTimeSync1"};
    for (const QString &timingName : timingNames) {
        const XmlElement *timing = child(element, timingName);
        if (timing && !timing->attributes.isEmpty()) {
            warnings->append(Tr::tr("DC mode %1 has formula attributes on %2; the literal value "
                                    "is available but the formula is preserved only in the source XML.")
                                 .arg(mode.name, timingName));
        }
    }
    return mode;
}

static std::optional<quint16> synchronizationTypesSupported(
    const XmlElement &device, quint16 objectIndex, bool *objectDeclared, QStringList *warnings)
{
    const auto defaultValue = [](const XmlElement &subItem) -> std::optional<quint16> {
        const XmlElement *info = child(subItem, "Info");
        QString value = info ? childText(*info, "DefaultValue") : QString();
        if (value.isEmpty())
            value = childText(subItem, "DefaultValue");
        if (const std::optional<quint64> parsed = parsedUnsigned(value, 0xffff))
            return quint16(*parsed);

        QString data = info ? childText(*info, "DefaultData") : QString();
        if (data.isEmpty())
            data = childText(subItem, "DefaultData");
        bool validData = false;
        const QByteArray bytes = parseHexData(data, &validData);
        if (!validData || bytes.isEmpty() || bytes.size() > 2)
            return std::nullopt;
        const quint16 low = quint8(bytes.at(0));
        const quint16 high = bytes.size() == 2 ? quint16(quint8(bytes.at(1))) << 8 : 0;
        return low | high;
    };
    const auto isSynchronizationTypesItem = [](const XmlElement &subItem) {
        QString subIndexText = childText(subItem, "SubIdx");
        if (subIndexText.isEmpty())
            subIndexText = childText(subItem, "SubIndex");
        if (const std::optional<quint64> subIndex = parsedUnsigned(subIndexText, 0xff))
            return *subIndex == 4;
        return false;
    };

    *objectDeclared = false;
    for (const XmlElement *object : descendants(device, "Object")) {
        const std::optional<quint64> index = parsedUnsigned(childText(*object, "Index"), 0xffff);
        if (!index || *index != objectIndex)
            continue;

        *objectDeclared = true;
        QString synchronizationTypesName;
        const QString objectType = childText(*object, "Type");
        for (const XmlElement *type : descendants(device, "DataType")) {
            if (childText(*type, "Name").compare(objectType, Qt::CaseInsensitive) != 0)
                continue;
            for (const XmlElement *subItem : children(*type, "SubItem")) {
                if (isSynchronizationTypesItem(*subItem)) {
                    synchronizationTypesName = localizedChildText(*subItem, "Name");
                    break;
                }
            }
            break;
        }
        const XmlElement *info = child(*object, "Info");
        if (info) {
            for (const XmlElement *subItem : children(*info, "SubItem")) {
                const QString name = localizedChildText(*subItem, "Name");
                const bool matchesDataType
                    = !synchronizationTypesName.isEmpty()
                      && name.compare(synchronizationTypesName, Qt::CaseInsensitive) == 0;
                const bool matchesKnownName
                    = name.contains("Synchronization Types supported", Qt::CaseInsensitive)
                      || name.contains("Sync modes supported", Qt::CaseInsensitive);
                if (!isSynchronizationTypesItem(*subItem) && !matchesDataType && !matchesKnownName)
                    continue;
                if (const std::optional<quint16> value = defaultValue(*subItem))
                    return value;
                break;
            }
        }

        warnings->append(
            Tr::tr(
                "Object 0x%1 does not declare a valid synchronization-types-supported value "
                "at subindex 4.")
                .arg(objectIndex, 4, 16, QLatin1Char('0')));
        return std::nullopt;
    }
    return std::nullopt;
}

static Utils::Result<Data::DeviceDescription> parseDevice(
    const XmlElement &element,
    quint32 vendorId,
    const QHash<QString, QString> &groupNames,
    const QString &sourcePath,
    const QByteArray &sha256,
    const QDateTime &importedAt)
{
    const XmlElement *type = child(element, "Type");
    if (!type)
        return Utils::ResultError(Tr::tr("ESI Device is missing its Type element."));

    const auto productCode = parsedUnsigned(attribute(*type, "ProductCode"), 0xffffffff);
    const auto revisionNumber = parsedUnsigned(attribute(*type, "RevisionNo"), 0xffffffff);
    if (!productCode || !revisionNumber) {
        return Utils::ResultError(
            Tr::tr("ESI Device Type is missing a valid ProductCode or RevisionNo identity field."));
    }

    Data::DeviceDescription device;
    device.summary.identity = {vendorId, quint32(*productCode), quint32(*revisionNumber)};
    device.summary.typeName = type->text.trimmed();
    device.summary.name = localizedChildText(element, "Name");
    if (device.summary.name.isEmpty())
        device.summary.name = device.summary.typeName;
    if (device.summary.name.isEmpty())
        return Utils::ResultError(Tr::tr("ESI Device is missing both Name and Type text."));
    device.summary.id = stableDeviceId(device.summary.identity);

    const QString groupType = childText(element, "GroupType");
    device.summary.group = groupNames.value(groupType, groupType);
    device.sourcePath = sourcePath;
    device.sourceSha256 = sha256;
    device.importedAt = importedAt;

    int syncManagerIndex = 0;
    for (const XmlElement *syncManager : children(element, "Sm"))
        device.syncManagers.append(parseSyncManager(*syncManager, syncManagerIndex++));
    for (const XmlElement *pdo : children(element, "RxPdo"))
        device.rxPdos.append(parsePdo(*pdo, Data::PdoDirection::Rx, &device.warnings));
    for (const XmlElement *pdo : children(element, "TxPdo"))
        device.txPdos.append(parsePdo(*pdo, Data::PdoDirection::Tx, &device.warnings));

    if (const XmlElement *mailbox = child(element, "Mailbox")) {
        if (const XmlElement *coe = child(*mailbox, "CoE")) {
            device.coe.supported = true;
            device.coe.sdoInfo = parseBoolean(attribute(*coe, "SdoInfo"));
            device.coe.pdoAssign = parseBoolean(attribute(*coe, "PdoAssign"));
            device.coe.pdoConfiguration = parseBoolean(attribute(*coe, "PdoConfig"));
            device.coe.completeAccess = parseBoolean(attribute(*coe, "CompleteAccess"));
            for (const XmlElement *command : descendants(*coe, "InitCmd"))
                device.startupParameters.append(parseStartupParameter(*command, &device.warnings));
        }
    }

    if (const XmlElement *dc = child(element, "Dc")) {
        for (const XmlElement *mode : children(*dc, "OpMode"))
            device.dcModes.append(parseDcMode(*mode, &device.warnings));
    }
    if (const std::optional<quint16> outputTypes = synchronizationTypesSupported(
            element, 0x1c32, &device.synchronizationTypes.outputTypesDeclared, &device.warnings)) {
        device.synchronizationTypes.outputSupportedTypes = *outputTypes;
    }
    if (const std::optional<quint16> inputTypes = synchronizationTypesSupported(
            element, 0x1c33, &device.synchronizationTypes.inputTypesDeclared, &device.warnings)) {
        device.synchronizationTypes.inputSupportedTypes = *inputTypes;
    }
    if (device.synchronizationTypes.outputTypesDeclared
        && device.synchronizationTypes.inputTypesDeclared
        && device.synchronizationTypes.outputSupportedTypes
               != device.synchronizationTypes.inputSupportedTypes) {
        device.warnings.append(
            Tr::tr(
                "Objects 0x1C32 and 0x1C33 declare different synchronization types supported "
                "(0x%1 and 0x%2); the original ESI values were preserved.")
                .arg(device.synchronizationTypes.outputSupportedTypes, 4, 16, QLatin1Char('0'))
                .arg(device.synchronizationTypes.inputSupportedTypes, 4, 16, QLatin1Char('0')));
    }

    QSet<QString> unsupported;
    const QStringList unsupportedElementNames = {"Modules", "Module", "Slots", "Slot"};
    for (const QString &elementName : unsupportedElementNames) {
        if (!descendants(element, elementName).isEmpty())
            unsupported.insert(elementName);
    }
    QStringList unsupportedNames = unsupported.values();
    unsupportedNames.sort(Qt::CaseInsensitive);
    for (const QString &name : std::as_const(unsupportedNames)) {
        device.unsupportedFeatures.append(
            Tr::tr("%1 structure is preserved in the original XML but is not expanded.").arg(name));
    }
    device.summary.supported = device.unsupportedFeatures.isEmpty();
    return device;
}

Utils::Result<QList<Data::DeviceDescription>> parseEsiFile(
    const QByteArray &contents, const QString &sourcePath, const QDateTime &importedAt)
{
    QXmlStreamReader reader(contents);
    if (!reader.readNextStartElement())
        return Utils::ResultError(Tr::tr("The ESI file does not contain a root element."));
    XmlElement root = readElement(reader);
    while (!reader.atEnd())
        reader.readNext();
    if (reader.hasError()) {
        return Utils::ResultError(Tr::tr("Malformed ESI XML at line %1, column %2: %3")
                                      .arg(reader.lineNumber())
                                      .arg(reader.columnNumber())
                                      .arg(reader.errorString()));
    }
    if (root.name.compare("EtherCATInfo", Qt::CaseInsensitive) != 0) {
        return Utils::ResultError(
            Tr::tr("Unexpected ESI root element '%1'; expected EtherCATInfo.").arg(root.name));
    }

    const XmlElement *vendor = child(root, "Vendor");
    const auto vendorId = vendor
                              ? parsedUnsigned(childText(*vendor, "Id"), 0xffffffff)
                              : std::optional<quint64>();
    if (!vendorId)
        return Utils::ResultError(Tr::tr("ESI Vendor is missing a valid Id identity field."));

    const XmlElement *descriptions = child(root, "Descriptions");
    const XmlElement *devices = descriptions ? child(*descriptions, "Devices") : nullptr;
    if (!devices)
        return Utils::ResultError(Tr::tr("ESI Descriptions is missing its Devices collection."));

    QHash<QString, QString> groupNames;
    if (const XmlElement *groups = child(*descriptions, "Groups")) {
        for (const XmlElement *group : children(*groups, "Group")) {
            const QString type = childText(*group, "Type");
            const QString name = localizedChildText(*group, "Name");
            if (!type.isEmpty())
                groupNames.insert(type, name.isEmpty() ? type : name);
        }
    }

    const QByteArray sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
    QList<Data::DeviceDescription> result;
    for (const XmlElement *device : children(*devices, "Device")) {
        Utils::Result<Data::DeviceDescription> parsed = parseDevice(
            *device, quint32(*vendorId), groupNames, sourcePath, sha256, importedAt);
        if (!parsed)
            return Utils::ResultError(Tr::tr("%1: %2").arg(sourcePath, parsed.error()));
        if (!root.namespaceUri.isEmpty()
            && !root.namespaceUri.contains("ethercat", Qt::CaseInsensitive)) {
            parsed->warnings.prepend(
                Tr::tr("Unrecognized ESI namespace '%1'; elements were matched by local name.")
                    .arg(root.namespaceUri));
        }
        result.append(*parsed);
    }
    if (result.isEmpty())
        return Utils::ResultError(Tr::tr("ESI Devices collection is empty."));
    return result;
}

} // namespace EtherCAT::Devices::Internal
