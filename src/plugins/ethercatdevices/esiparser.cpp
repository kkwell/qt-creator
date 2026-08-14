// Copyright (C) 2026 Kvell

#include "esiparser.h"

#include "ethercatdevicestr.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>

#include <algorithm>
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

static bool hasAttribute(const XmlElement &element, const QString &name)
{
    for (auto it = element.attributes.cbegin(); it != element.attributes.cend(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
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

static std::optional<bool> parsedBoolean(const QString &text)
{
    if (text == "1" || text.compare("true", Qt::CaseInsensitive) == 0
        || text.compare("yes", Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (text == "0" || text.compare("false", Qt::CaseInsensitive) == 0
        || text.compare("no", Qt::CaseInsensitive) == 0) {
        return false;
    }
    return std::nullopt;
}

static bool isAllowedName(const QString &candidate, const QStringList &allowedNames)
{
    return std::any_of(
        allowedNames.cbegin(),
        allowedNames.cend(),
        [&candidate](const QString &allowed) {
            return candidate.compare(allowed, Qt::CaseInsensitive) == 0;
        });
}

static void appendUnsupportedFeature(QStringList *features, const QString &feature)
{
    if (!features->contains(feature))
        features->append(feature);
}

static void reportUnknownAttributes(
    const XmlElement &element,
    const QStringList &allowedNames,
    const QString &context,
    QStringList *unsupportedFeatures)
{
    for (auto it = element.attributes.cbegin(); it != element.attributes.cend(); ++it) {
        if (!isAllowedName(it.key(), allowedNames)) {
            appendUnsupportedFeature(
                unsupportedFeatures,
                Tr::tr("%1 attribute '%2' is not interpreted.").arg(context, it.key()));
        }
    }
}

static void reportUnknownChildren(
    const XmlElement &element,
    const QStringList &allowedNames,
    const QString &context,
    QStringList *unsupportedFeatures)
{
    for (const XmlElement &candidate : element.children) {
        if (!isAllowedName(candidate.name, allowedNames)) {
            appendUnsupportedFeature(
                unsupportedFeatures,
                Tr::tr("%1 element '%2' is not interpreted.").arg(context, candidate.name));
        }
    }
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
    if (const XmlElement *indexElement = child(element, "Index")) {
        if (const auto index = parsedUnsigned(indexElement->text, 0xffff))
            entry.index = quint16(*index);
        entry.indexDependsOnSlot = parseBoolean(attribute(*indexElement, "DependOnSlot"));
    }
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
    if (const XmlElement *indexElement = child(element, "Index")) {
        if (const auto index = parsedUnsigned(indexElement->text, 0xffff))
            pdo.index = quint16(*index);
        pdo.indexDependsOnSlot = parseBoolean(attribute(*indexElement, "DependOnSlot"));
    }
    pdo.name = localizedChildText(element, "Name");
    if (const auto syncManager = parsedUnsigned(attribute(element, "Sm"), std::numeric_limits<int>::max()))
        pdo.syncManager = int(*syncManager);
    pdo.fixed = parseBoolean(attribute(element, "Fixed"));
    pdo.mandatory = parseBoolean(attribute(element, "Mandatory"));
    for (const XmlElement *entry : children(element, "Entry"))
        pdo.entries.append(parsePdoEntry(*entry, warnings));
    return pdo;
}

struct ParsedParameterSubItem
{
    quint8 subIndex = 0;
    QString name;
    QString typeName;
    int bitLength = 0;
    int bitOffset = 0;
    Data::ParameterAccess access = Data::ParameterAccess::ReadOnly;
    bool setting = false;
};

struct ParsedParameterDataType
{
    QString name;
    QString baseType;
    int bitLength = 0;
    QList<Data::ModuleParameterEnumValueDescription> enumValues;
    QList<ParsedParameterSubItem> subItems;
};

static int fixedDataTypeBitLength(Data::EtherCATDataType type)
{
    switch (type) {
    case Data::EtherCATDataType::Boolean:
        return 1;
    case Data::EtherCATDataType::Integer8:
    case Data::EtherCATDataType::UnsignedInteger8:
        return 8;
    case Data::EtherCATDataType::Integer16:
    case Data::EtherCATDataType::UnsignedInteger16:
        return 16;
    case Data::EtherCATDataType::Integer32:
    case Data::EtherCATDataType::UnsignedInteger32:
    case Data::EtherCATDataType::Real32:
        return 32;
    case Data::EtherCATDataType::Integer64:
    case Data::EtherCATDataType::UnsignedInteger64:
    case Data::EtherCATDataType::Real64:
        return 64;
    case Data::EtherCATDataType::Unknown:
    case Data::EtherCATDataType::VisibleString:
    case Data::EtherCATDataType::OctetString:
        return 0;
    }
    return 0;
}

static bool isSignedIntegerDataType(Data::EtherCATDataType type)
{
    return type == Data::EtherCATDataType::Integer8
           || type == Data::EtherCATDataType::Integer16
           || type == Data::EtherCATDataType::Integer32
           || type == Data::EtherCATDataType::Integer64;
}

static bool isUnsignedIntegerDataType(Data::EtherCATDataType type)
{
    return type == Data::EtherCATDataType::Boolean
           || type == Data::EtherCATDataType::UnsignedInteger8
           || type == Data::EtherCATDataType::UnsignedInteger16
           || type == Data::EtherCATDataType::UnsignedInteger32
           || type == Data::EtherCATDataType::UnsignedInteger64;
}

static std::optional<Data::ParameterAccess> parsedParameterAccess(const QString &text)
{
    if (text.compare("ro", Qt::CaseInsensitive) == 0)
        return Data::ParameterAccess::ReadOnly;
    if (text.compare("wo", Qt::CaseInsensitive) == 0)
        return Data::ParameterAccess::WriteOnly;
    if (text.compare("rw", Qt::CaseInsensitive) == 0)
        return Data::ParameterAccess::ReadWrite;
    return std::nullopt;
}

static bool validIntegerLiteral(
    const QString &input, Data::EtherCATDataType type, int bitLength)
{
    QString text = input.trimmed();
    if (text.isEmpty() || bitLength <= 0 || bitLength > 64)
        return false;

    if (text.startsWith("#x", Qt::CaseInsensitive)
        || text.startsWith("0x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
        bool ok = false;
        const quint64 value = text.toULongLong(&ok, 16);
        const quint64 maximum
            = bitLength == 64 ? std::numeric_limits<quint64>::max()
                              : (quint64(1) << bitLength) - 1;
        return ok && value <= maximum;
    }
    if (isSignedIntegerDataType(type)) {
        bool ok = false;
        const qint64 value = text.toLongLong(&ok, 10);
        if (!ok)
            return false;
        if (bitLength == 64)
            return true;
        const qint64 minimum = -(qint64(1) << (bitLength - 1));
        const qint64 maximum = (qint64(1) << (bitLength - 1)) - 1;
        return value >= minimum && value <= maximum;
    }
    if (isUnsignedIntegerDataType(type)) {
        bool ok = false;
        const quint64 value = text.toULongLong(&ok, 10);
        const quint64 maximum
            = bitLength == 64 ? std::numeric_limits<quint64>::max()
                              : (quint64(1) << bitLength) - 1;
        return ok && value <= maximum;
    }
    return false;
}

static Utils::Result<QByteArray> parseParameterData(
    const XmlElement &element, int bitLength, const QString &context)
{
    QString text = element.text.trimmed();
    if (text.startsWith("#x", Qt::CaseInsensitive)
        || text.startsWith("0x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
    }
    text.remove(' ');
    text.remove('\t');
    text.remove('\r');
    text.remove('\n');
    if (text.isEmpty() || text.size() % 2 != 0) {
        return Utils::ResultError(
            Tr::tr("%1 must contain non-empty, byte-aligned hexadecimal data.").arg(context));
    }
    for (const QChar character : std::as_const(text)) {
        if (!character.isDigit()
            && !(character.toLower() >= QLatin1Char('a')
                 && character.toLower() <= QLatin1Char('f'))) {
            return Utils::ResultError(
                Tr::tr("%1 contains invalid hexadecimal data.").arg(context));
        }
    }
    const QByteArray value = QByteArray::fromHex(text.toLatin1());
    if (value.size() > (bitLength + 7) / 8) {
        return Utils::ResultError(
            Tr::tr("%1 exceeds its declared %2-bit data type.").arg(context).arg(bitLength));
    }
    return value;
}

static Utils::Result<Data::EtherCATDataType> resolvedParameterDataType(
    const QString &typeName,
    const QHash<QString, ParsedParameterDataType> &dataTypes,
    QSet<QString> *resolving)
{
    const auto definition = dataTypes.constFind(typeName);
    if (definition == dataTypes.cend()) {
        return Utils::ResultError(
            Tr::tr("Parameter data type '%1' is not declared.").arg(typeName));
    }
    if (resolving->contains(typeName)) {
        return Utils::ResultError(
            Tr::tr("Parameter data type '%1' has a cyclic BaseType reference.").arg(typeName));
    }
    if (!definition->subItems.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("Record data type '%1' cannot be used as a scalar parameter type.")
                .arg(typeName));
    }

    resolving->insert(typeName);
    Data::EtherCATDataType resolved = Data::EtherCATDataType::Unknown;
    if (definition->baseType.isEmpty()) {
        resolved = dataType(definition->name);
    } else {
        Utils::Result<Data::EtherCATDataType> base = resolvedParameterDataType(
            definition->baseType, dataTypes, resolving);
        if (!base)
            return Utils::ResultError(base.error());
        resolved = *base;
    }
    resolving->remove(typeName);

    if (resolved == Data::EtherCATDataType::Unknown
        || fixedDataTypeBitLength(resolved) != definition->bitLength) {
        return Utils::ResultError(
            Tr::tr("Parameter data type '%1' has an unsupported or inconsistent BitSize.")
                .arg(typeName));
    }
    return resolved;
}

static Utils::Result<QHash<QString, ParsedParameterDataType>> parseParameterDataTypes(
    const XmlElement &dataTypesElement,
    const QString &moduleContext,
    QStringList *unsupportedFeatures)
{
    QHash<QString, ParsedParameterDataType> result;
    reportUnknownAttributes(
        dataTypesElement,
        {},
        Tr::tr("%1 Profile DataTypes").arg(moduleContext),
        unsupportedFeatures);
    reportUnknownChildren(
        dataTypesElement,
        {"DataType"},
        Tr::tr("%1 Profile DataTypes").arg(moduleContext),
        unsupportedFeatures);

    for (const XmlElement *dataTypeElement : children(dataTypesElement, "DataType")) {
        if (children(*dataTypeElement, "Name").size() != 1
            || children(*dataTypeElement, "BitSize").size() != 1
            || children(*dataTypeElement, "BaseType").size() > 1) {
            return Utils::ResultError(
                Tr::tr("%1 Profile DataType must contain one Name and BitSize.")
                    .arg(moduleContext));
        }
        ParsedParameterDataType parsed;
        parsed.name = childText(*dataTypeElement, "Name");
        parsed.baseType = childText(*dataTypeElement, "BaseType");
        const std::optional<quint64> bitLength = parsedUnsigned(
            childText(*dataTypeElement, "BitSize"), std::numeric_limits<int>::max());
        if (parsed.name.isEmpty() || !bitLength || *bitLength == 0) {
            return Utils::ResultError(
                Tr::tr("%1 Profile DataType has an invalid Name or BitSize.")
                    .arg(moduleContext));
        }
        if (result.contains(parsed.name)) {
            return Utils::ResultError(
                Tr::tr("%1 Profile contains duplicate DataType '%2'.")
                    .arg(moduleContext, parsed.name));
        }
        parsed.bitLength = int(*bitLength);
        const QString typeContext
            = Tr::tr("%1 Profile DataType '%2'").arg(moduleContext, parsed.name);
        reportUnknownAttributes(
            *dataTypeElement, {}, typeContext, unsupportedFeatures);
        reportUnknownChildren(
            *dataTypeElement,
            {"Name", "BaseType", "BitSize", "EnumInfo", "SubItem"},
            typeContext,
            unsupportedFeatures);
        for (const QString &elementName :
             {QString("Name"), QString("BaseType"), QString("BitSize")}) {
            if (const XmlElement *valueElement = child(*dataTypeElement, elementName)) {
                reportUnknownAttributes(
                    *valueElement,
                    {},
                    Tr::tr("%1 %2").arg(typeContext, elementName),
                    unsupportedFeatures);
            }
        }

        QSet<QString> enumValues;
        for (const XmlElement *enumElement : children(*dataTypeElement, "EnumInfo")) {
            if (children(*enumElement, "Text").size() != 1
                || children(*enumElement, "Enum").size() != 1) {
                return Utils::ResultError(
                    Tr::tr("%1 EnumInfo must contain one Text and Enum.").arg(typeContext));
            }
            Data::ModuleParameterEnumValueDescription enumValue;
            enumValue.name = localizedChildText(*enumElement, "Text");
            enumValue.value = childText(*enumElement, "Enum");
            if (enumValue.name.isEmpty() || enumValue.value.isEmpty()
                || enumValues.contains(enumValue.value)) {
                return Utils::ResultError(
                    Tr::tr("%1 contains an invalid or duplicate EnumInfo value.")
                        .arg(typeContext));
            }
            enumValues.insert(enumValue.value);
            parsed.enumValues.append(enumValue);
            reportUnknownAttributes(
                *enumElement, {}, Tr::tr("%1 EnumInfo").arg(typeContext), unsupportedFeatures);
            reportUnknownChildren(
                *enumElement,
                {"Text", "Enum"},
                Tr::tr("%1 EnumInfo").arg(typeContext),
                unsupportedFeatures);
            for (const XmlElement *textElement : children(*enumElement, "Text")) {
                reportUnknownAttributes(
                    *textElement,
                    {"LcId"},
                    Tr::tr("%1 EnumInfo Text").arg(typeContext),
                    unsupportedFeatures);
            }
            if (const XmlElement *valueElement = child(*enumElement, "Enum")) {
                reportUnknownAttributes(
                    *valueElement,
                    {},
                    Tr::tr("%1 EnumInfo Enum").arg(typeContext),
                    unsupportedFeatures);
            }
        }

        QSet<quint8> subIndexes;
        QSet<QString> subItemNames;
        for (const XmlElement *subItemElement : children(*dataTypeElement, "SubItem")) {
            const QStringList requiredElements = {
                "SubIdx", "Name", "Type", "BitSize", "BitOffs", "Flags"};
            if (std::any_of(
                    requiredElements.cbegin(),
                    requiredElements.cend(),
                    [subItemElement](const QString &name) {
                        return children(*subItemElement, name).size() != 1;
                    })) {
                return Utils::ResultError(
                    Tr::tr("%1 SubItem is missing a required field.").arg(typeContext));
            }
            const XmlElement *flagsElement = child(*subItemElement, "Flags");
            if (children(*flagsElement, "Access").size() != 1
                || children(*flagsElement, "Setting").size() != 1) {
                return Utils::ResultError(
                    Tr::tr("%1 SubItem Flags must contain Access and Setting.")
                        .arg(typeContext));
            }

            ParsedParameterSubItem subItem;
            const std::optional<quint64> subIndex
                = parsedUnsigned(childText(*subItemElement, "SubIdx"), 0xff);
            const std::optional<quint64> subItemBitLength = parsedUnsigned(
                childText(*subItemElement, "BitSize"), std::numeric_limits<int>::max());
            const std::optional<quint64> bitOffset = parsedUnsigned(
                childText(*subItemElement, "BitOffs"), std::numeric_limits<int>::max());
            const std::optional<Data::ParameterAccess> access = parsedParameterAccess(
                childText(*flagsElement, "Access"));
            const std::optional<bool> setting = parsedBoolean(
                childText(*flagsElement, "Setting"));
            subItem.name = localizedChildText(*subItemElement, "Name");
            subItem.typeName = childText(*subItemElement, "Type");
            if (!subIndex || !subItemBitLength || *subItemBitLength == 0 || !bitOffset || !access
                || !setting || subItem.name.isEmpty() || subItem.typeName.isEmpty()) {
                return Utils::ResultError(
                    Tr::tr("%1 SubItem has an invalid index, type, layout, access, or setting.")
                        .arg(typeContext));
            }
            subItem.subIndex = quint8(*subIndex);
            subItem.bitLength = int(*subItemBitLength);
            subItem.bitOffset = int(*bitOffset);
            subItem.access = *access;
            subItem.setting = *setting;
            if (subIndexes.contains(subItem.subIndex) || subItemNames.contains(subItem.name)) {
                return Utils::ResultError(
                    Tr::tr("%1 contains a duplicate SubItem index or name.").arg(typeContext));
            }
            subIndexes.insert(subItem.subIndex);
            subItemNames.insert(subItem.name);
            parsed.subItems.append(subItem);

            reportUnknownAttributes(
                *subItemElement,
                {},
                Tr::tr("%1 SubItem '%2'").arg(typeContext, subItem.name),
                unsupportedFeatures);
            reportUnknownChildren(
                *subItemElement,
                requiredElements,
                Tr::tr("%1 SubItem '%2'").arg(typeContext, subItem.name),
                unsupportedFeatures);
            reportUnknownAttributes(
                *flagsElement,
                {},
                Tr::tr("%1 SubItem '%2' Flags").arg(typeContext, subItem.name),
                unsupportedFeatures);
            reportUnknownChildren(
                *flagsElement,
                {"Access", "Setting"},
                Tr::tr("%1 SubItem '%2' Flags").arg(typeContext, subItem.name),
                unsupportedFeatures);
            for (const QString &elementName :
                 {QString("SubIdx"),
                  QString("Name"),
                  QString("Type"),
                  QString("BitSize"),
                  QString("BitOffs")}) {
                if (const XmlElement *valueElement = child(*subItemElement, elementName)) {
                    reportUnknownAttributes(
                        *valueElement,
                        elementName == "Name" ? QStringList{"LcId"} : QStringList{},
                        Tr::tr("%1 SubItem '%2' %3")
                            .arg(typeContext, subItem.name, elementName),
                        unsupportedFeatures);
                }
            }
            for (const QString &elementName : {QString("Access"), QString("Setting")}) {
                if (const XmlElement *valueElement = child(*flagsElement, elementName)) {
                    reportUnknownAttributes(
                        *valueElement,
                        {},
                        Tr::tr("%1 SubItem '%2' %3")
                            .arg(typeContext, subItem.name, elementName),
                        unsupportedFeatures);
                }
            }
        }

        const bool isPrimitive
            = parsed.baseType.isEmpty() && parsed.enumValues.isEmpty() && parsed.subItems.isEmpty();
        const bool isEnum = !parsed.baseType.isEmpty() && !parsed.enumValues.isEmpty()
                            && parsed.subItems.isEmpty();
        const bool isRecord = parsed.baseType.isEmpty() && parsed.enumValues.isEmpty()
                              && !parsed.subItems.isEmpty();
        if (!isPrimitive && !isEnum && !isRecord) {
            return Utils::ResultError(
                Tr::tr("%1 has an unsupported DataType shape.").arg(typeContext));
        }
        result.insert(parsed.name, parsed);
    }
    if (result.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("%1 Profile DataTypes is empty.").arg(moduleContext));
    }

    for (auto it = result.cbegin(); it != result.cend(); ++it) {
        const ParsedParameterDataType &definition = it.value();
        if (definition.subItems.isEmpty()) {
            QSet<QString> resolving;
            Utils::Result<Data::EtherCATDataType> resolved
                = resolvedParameterDataType(definition.name, result, &resolving);
            if (!resolved)
                return Utils::ResultError(resolved.error());
            if (!definition.enumValues.isEmpty()) {
                if (!isSignedIntegerDataType(*resolved) && !isUnsignedIntegerDataType(*resolved)) {
                    return Utils::ResultError(
                        Tr::tr("Enum DataType '%1' does not resolve to an integer type.")
                            .arg(definition.name));
                }
                for (const Data::ModuleParameterEnumValueDescription &enumValue :
                     definition.enumValues) {
                    if (!validIntegerLiteral(
                            enumValue.value, *resolved, definition.bitLength)) {
                        return Utils::ResultError(
                            Tr::tr("Enum DataType '%1' contains out-of-range value '%2'.")
                                .arg(definition.name, enumValue.value));
                    }
                }
            }
            continue;
        }

        QList<QPair<int, int>> occupiedRanges;
        for (const ParsedParameterSubItem &subItem : definition.subItems) {
            QSet<QString> resolving;
            Utils::Result<Data::EtherCATDataType> resolved = resolvedParameterDataType(
                subItem.typeName, result, &resolving);
            if (!resolved)
                return Utils::ResultError(resolved.error());
            const auto referencedType = result.constFind(subItem.typeName);
            if (referencedType == result.cend()
                || referencedType->bitLength != subItem.bitLength
                || subItem.bitOffset > definition.bitLength - subItem.bitLength) {
                return Utils::ResultError(
                    Tr::tr("Record DataType '%1' has an invalid SubItem layout or type width.")
                        .arg(definition.name));
            }
            const int rangeEnd = subItem.bitOffset + subItem.bitLength;
            if (std::any_of(
                    occupiedRanges.cbegin(),
                    occupiedRanges.cend(),
                    [&subItem, rangeEnd](const QPair<int, int> &range) {
                        return subItem.bitOffset < range.second && range.first < rangeEnd;
                    })) {
                return Utils::ResultError(
                    Tr::tr("Record DataType '%1' contains overlapping SubItems.")
                        .arg(definition.name));
            }
            occupiedRanges.append({subItem.bitOffset, rangeEnd});
        }
    }
    return result;
}

static Utils::Result<QList<Data::ModuleParameterObjectDescription>> parseModuleParameterProfile(
    const XmlElement &profileElement,
    const QString &moduleContext,
    QStringList *unsupportedFeatures)
{
    if (children(profileElement, "Dictionary").size() != 1) {
        return Utils::ResultError(
            Tr::tr("%1 Profile must contain exactly one Dictionary.").arg(moduleContext));
    }
    const XmlElement &dictionary = *children(profileElement, "Dictionary").constFirst();
    if (children(dictionary, "DataTypes").size() != 1
        || children(dictionary, "Objects").size() != 1) {
        return Utils::ResultError(
            Tr::tr("%1 Profile Dictionary must contain DataTypes and Objects.")
                .arg(moduleContext));
    }
    const XmlElement &dataTypesElement = *children(dictionary, "DataTypes").constFirst();
    const XmlElement &objectsElement = *children(dictionary, "Objects").constFirst();

    reportUnknownAttributes(
        profileElement, {}, Tr::tr("%1 Profile").arg(moduleContext), unsupportedFeatures);
    reportUnknownChildren(
        profileElement,
        {"Dictionary"},
        Tr::tr("%1 Profile").arg(moduleContext),
        unsupportedFeatures);
    reportUnknownAttributes(
        dictionary,
        {},
        Tr::tr("%1 Profile Dictionary").arg(moduleContext),
        unsupportedFeatures);
    reportUnknownChildren(
        dictionary,
        {"DataTypes", "Objects"},
        Tr::tr("%1 Profile Dictionary").arg(moduleContext),
        unsupportedFeatures);
    reportUnknownAttributes(
        objectsElement,
        {},
        Tr::tr("%1 Profile Objects").arg(moduleContext),
        unsupportedFeatures);
    reportUnknownChildren(
        objectsElement,
        {"Object"},
        Tr::tr("%1 Profile Objects").arg(moduleContext),
        unsupportedFeatures);

    Utils::Result<QHash<QString, ParsedParameterDataType>> dataTypes
        = parseParameterDataTypes(dataTypesElement, moduleContext, unsupportedFeatures);
    if (!dataTypes)
        return Utils::ResultError(dataTypes.error());

    QList<Data::ModuleParameterObjectDescription> result;
    QSet<quint16> objectIndexes;
    for (const XmlElement *objectElement : children(objectsElement, "Object")) {
        const QStringList requiredElements = {
            "Index", "Name", "Type", "BitSize", "Info", "Flags"};
        if (std::any_of(
                requiredElements.cbegin(),
                requiredElements.cend(),
                [objectElement](const QString &name) {
                    return children(*objectElement, name).size() != 1;
                })) {
            return Utils::ResultError(
                Tr::tr("%1 Profile Object is missing a required field.").arg(moduleContext));
        }
        const XmlElement *indexElement = child(*objectElement, "Index");
        const XmlElement *flagsElement = child(*objectElement, "Flags");
        const XmlElement *infoElement = child(*objectElement, "Info");
        if (children(*flagsElement, "Access").size() != 1
            || children(*flagsElement, "Category").size() != 1) {
            return Utils::ResultError(
                Tr::tr("%1 Profile Object Flags must contain Access and Category.")
                    .arg(moduleContext));
        }

        Data::ModuleParameterObjectDescription object;
        const std::optional<quint64> index = parsedUnsigned(indexElement->text, 0xffff);
        const std::optional<quint64> bitLength = parsedUnsigned(
            childText(*objectElement, "BitSize"), std::numeric_limits<int>::max());
        const std::optional<Data::ParameterAccess> access = parsedParameterAccess(
            childText(*flagsElement, "Access"));
        object.name = localizedChildText(*objectElement, "Name");
        object.rawDataType = childText(*objectElement, "Type");
        object.category = childText(*flagsElement, "Category");
        if (!index || *index == 0 || !bitLength || *bitLength == 0 || !access
            || object.name.isEmpty() || object.rawDataType.isEmpty()
            || object.category.isEmpty()) {
            return Utils::ResultError(
                Tr::tr("%1 Profile Object has an invalid identity, type, layout, or access.")
                    .arg(moduleContext));
        }
        object.index = quint16(*index);
        object.bitLength = int(*bitLength);
        object.access = *access;
        if (objectIndexes.contains(object.index)) {
            return Utils::ResultError(
                Tr::tr("%1 Profile contains duplicate Object Index 0x%2.")
                    .arg(moduleContext)
                    .arg(object.index, 4, 16, QLatin1Char('0')));
        }
        objectIndexes.insert(object.index);

        if (hasAttribute(*indexElement, "DependOnSlot")) {
            const std::optional<bool> dependsOnSlot = parsedBoolean(
                attribute(*indexElement, "DependOnSlot"));
            if (!dependsOnSlot) {
                return Utils::ResultError(
                    Tr::tr("%1 Profile Object 0x%2 has invalid DependOnSlot.")
                        .arg(moduleContext)
                        .arg(object.index, 4, 16, QLatin1Char('0')));
            }
            object.indexDependsOnSlot = *dependsOnSlot;
        }

        const auto recordType = dataTypes->constFind(object.rawDataType);
        if (recordType == dataTypes->cend() || recordType->subItems.isEmpty()
            || recordType->bitLength != object.bitLength) {
            return Utils::ResultError(
                Tr::tr("%1 Profile Object 0x%2 references an invalid record DataType '%3'.")
                    .arg(moduleContext)
                    .arg(object.index, 4, 16, QLatin1Char('0'))
                    .arg(object.rawDataType));
        }
        const QList<const XmlElement *> infoSubItems = children(*infoElement, "SubItem");
        if (infoSubItems.size() != recordType->subItems.size()) {
            return Utils::ResultError(
                Tr::tr("%1 Profile Object 0x%2 Info does not cover every record SubItem.")
                    .arg(moduleContext)
                    .arg(object.index, 4, 16, QLatin1Char('0')));
        }

        const QString objectContext
            = Tr::tr("%1 Profile Object 0x%2").arg(moduleContext).arg(
                object.index, 4, 16, QLatin1Char('0'));
        reportUnknownAttributes(
            *objectElement, {}, objectContext, unsupportedFeatures);
        reportUnknownChildren(
            *objectElement, requiredElements, objectContext, unsupportedFeatures);
        reportUnknownAttributes(
            *indexElement,
            {"DependOnSlot"},
            Tr::tr("%1 Index").arg(objectContext),
            unsupportedFeatures);
        reportUnknownAttributes(
            *flagsElement,
            {},
            Tr::tr("%1 Flags").arg(objectContext),
            unsupportedFeatures);
        reportUnknownChildren(
            *flagsElement,
            {"Access", "Category"},
            Tr::tr("%1 Flags").arg(objectContext),
            unsupportedFeatures);
        reportUnknownAttributes(
            *infoElement, {}, Tr::tr("%1 Info").arg(objectContext), unsupportedFeatures);
        reportUnknownChildren(
            *infoElement,
            {"SubItem"},
            Tr::tr("%1 Info").arg(objectContext),
            unsupportedFeatures);
        for (const QString &elementName : {QString("Name"), QString("Type"), QString("BitSize")}) {
            if (const XmlElement *valueElement = child(*objectElement, elementName)) {
                reportUnknownAttributes(
                    *valueElement,
                    elementName == "Name" ? QStringList{"LcId"} : QStringList{},
                    Tr::tr("%1 %2").arg(objectContext, elementName),
                    unsupportedFeatures);
            }
        }
        for (const QString &elementName : {QString("Access"), QString("Category")}) {
            if (const XmlElement *valueElement = child(*flagsElement, elementName)) {
                reportUnknownAttributes(
                    *valueElement,
                    {},
                    Tr::tr("%1 %2").arg(objectContext, elementName),
                    unsupportedFeatures);
            }
        }

        for (int subItemIndex = 0; subItemIndex < recordType->subItems.size(); ++subItemIndex) {
            const ParsedParameterSubItem &source = recordType->subItems.at(subItemIndex);
            const XmlElement &infoSubItem = *infoSubItems.at(subItemIndex);
            if (children(infoSubItem, "Name").size() != 1
                || children(infoSubItem, "Info").size() != 1
                || localizedChildText(infoSubItem, "Name") != source.name) {
                return Utils::ResultError(
                    Tr::tr("%1 Info SubItem does not match record SubItem '%2'.")
                        .arg(objectContext, source.name));
            }
            const XmlElement &valueInfo = *children(infoSubItem, "Info").constFirst();
            Data::ModuleParameterDescription parameter;
            parameter.subIndex = source.subIndex;
            parameter.name = source.name;
            parameter.rawDataType = source.typeName;
            parameter.bitLength = source.bitLength;
            parameter.bitOffset = source.bitOffset;
            parameter.access = source.access;
            parameter.setting = source.setting;

            QSet<QString> resolving;
            Utils::Result<Data::EtherCATDataType> resolved = resolvedParameterDataType(
                source.typeName, *dataTypes, &resolving);
            if (!resolved)
                return Utils::ResultError(resolved.error());
            parameter.dataType = *resolved;
            const auto sourceType = dataTypes->constFind(source.typeName);
            if (sourceType == dataTypes->cend())
                return Utils::ResultError(Tr::tr("Internal parameter type lookup failed."));
            parameter.enumValues = sourceType->enumValues;

            for (const QString &valueName :
                 {QString("DefaultData"), QString("MinData"), QString("MaxData")}) {
                const QList<const XmlElement *> values = children(valueInfo, valueName);
                if (values.size() > 1) {
                    return Utils::ResultError(
                        Tr::tr("%1 parameter '%2' contains duplicate %3.")
                            .arg(objectContext, parameter.name, valueName));
                }
                if (values.isEmpty())
                    continue;
                Utils::Result<QByteArray> value = parseParameterData(
                    *values.constFirst(),
                    parameter.bitLength,
                    Tr::tr("%1 parameter '%2' %3")
                        .arg(objectContext, parameter.name, valueName));
                if (!value)
                    return Utils::ResultError(value.error());
                const int declaredBytes = (parameter.bitLength + 7) / 8;
                if (value->size() != declaredBytes) {
                    appendUnsupportedFeature(
                        unsupportedFeatures,
                        Tr::tr("%1 parameter '%2' %3 has %4 raw bytes for a declared %5-bit "
                               "type; automatic value encoding is disabled.")
                            .arg(objectContext, parameter.name, valueName)
                            .arg(value->size())
                            .arg(parameter.bitLength));
                }
                if (valueName == "DefaultData") {
                    parameter.hasDefaultData = true;
                    parameter.defaultData = *value;
                } else if (valueName == "MinData") {
                    parameter.hasMinimumData = true;
                    parameter.minimumData = *value;
                } else {
                    parameter.hasMaximumData = true;
                    parameter.maximumData = *value;
                }
                reportUnknownAttributes(
                    *values.constFirst(),
                    {},
                    Tr::tr("%1 parameter '%2' %3")
                        .arg(objectContext, parameter.name, valueName),
                    unsupportedFeatures);
            }
            if (parameter.hasMinimumData != parameter.hasMaximumData) {
                return Utils::ResultError(
                    Tr::tr("%1 parameter '%2' must declare MinData and MaxData together.")
                        .arg(objectContext, parameter.name));
            }

            const QString parameterContext
                = Tr::tr("%1 parameter '%2'").arg(objectContext, parameter.name);
            reportUnknownAttributes(
                infoSubItem, {}, parameterContext, unsupportedFeatures);
            reportUnknownChildren(
                infoSubItem, {"Name", "Info"}, parameterContext, unsupportedFeatures);
            if (const XmlElement *nameElement = child(infoSubItem, "Name")) {
                reportUnknownAttributes(
                    *nameElement,
                    {"LcId"},
                    Tr::tr("%1 Name").arg(parameterContext),
                    unsupportedFeatures);
            }
            reportUnknownAttributes(
                valueInfo, {}, Tr::tr("%1 Info").arg(parameterContext), unsupportedFeatures);
            reportUnknownChildren(
                valueInfo,
                {"DefaultData", "MinData", "MaxData"},
                Tr::tr("%1 Info").arg(parameterContext),
                unsupportedFeatures);
            object.parameters.append(parameter);
        }
        result.append(object);
    }
    if (result.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("%1 Profile Objects is empty.").arg(moduleContext));
    }
    return result;
}

struct ParsedModule
{
    Data::ModuleDescription description;
    QStringList unsupportedFeatures;
};

struct ParsedModuleDefinitions
{
    QList<ParsedModule> modules;
    QStringList unsupportedFeatures;
};

struct ParsedModuleCatalog
{
    Data::ModuleCatalogDescription description;
    QStringList unsupportedFeatures;
};

static Utils::Result<bool> strictBooleanAttribute(
    const XmlElement &element,
    const QString &attributeName,
    bool defaultValue,
    const QString &context)
{
    if (!hasAttribute(element, attributeName))
        return defaultValue;
    const std::optional<bool> value = parsedBoolean(attribute(element, attributeName));
    if (!value) {
        return Utils::ResultError(
            Tr::tr("%1 has an invalid %2 boolean value.").arg(context, attributeName));
    }
    return *value;
}

static Utils::Result<Data::PdoEntryDescription> parseModulePdoEntry(
    const XmlElement &element,
    const QString &moduleContext,
    QStringList *warnings,
    QStringList *unsupportedFeatures)
{
    const QList<const XmlElement *> indexElements = children(element, "Index");
    const QList<const XmlElement *> bitLengthElements = children(element, "BitLen");
    if (indexElements.size() != 1 || bitLengthElements.size() != 1) {
        return Utils::ResultError(
            Tr::tr("%1 contains a PDO Entry without exactly one Index and BitLen.")
                .arg(moduleContext));
    }
    const XmlElement *indexElement = indexElements.constFirst();
    const std::optional<quint64> index
        = parsedUnsigned(indexElement->text, 0xffff);
    if (!index) {
        return Utils::ResultError(
            Tr::tr("%1 contains a PDO Entry with an invalid Index.").arg(moduleContext));
    }
    const std::optional<quint64> bitLength = parsedUnsigned(
        childText(element, "BitLen"), std::numeric_limits<int>::max());
    if (!bitLength || *bitLength == 0) {
        return Utils::ResultError(
            Tr::tr("%1 contains a PDO Entry with an invalid BitLen.").arg(moduleContext));
    }
    const QList<const XmlElement *> subIndexElements = children(element, "SubIndex");
    if ((*index != 0 && subIndexElements.size() != 1) || subIndexElements.size() > 1
        || (!subIndexElements.isEmpty()
            && !parsedUnsigned(subIndexElements.constFirst()->text, 0xff))) {
        return Utils::ResultError(
            Tr::tr("%1 contains a non-padding PDO Entry with an invalid SubIndex.")
                .arg(moduleContext));
    }

    Data::PdoEntryDescription entry = parsePdoEntry(element, warnings);
    if (hasAttribute(*indexElement, "DependOnSlot")) {
        const Utils::Result<bool> dependsOnSlot = strictBooleanAttribute(
            *indexElement, "DependOnSlot", false, Tr::tr("%1 PDO Entry Index").arg(moduleContext));
        if (!dependsOnSlot)
            return Utils::ResultError(dependsOnSlot.error());
        entry.indexDependsOnSlot = *dependsOnSlot;
    }

    reportUnknownAttributes(
        element, {}, Tr::tr("%1 PDO Entry").arg(moduleContext), unsupportedFeatures);
    reportUnknownChildren(
        element,
        {"Index", "SubIndex", "BitLen", "Name", "DataType"},
        Tr::tr("%1 PDO Entry").arg(moduleContext),
        unsupportedFeatures);
    reportUnknownAttributes(
        *indexElement,
        {"DependOnSlot"},
        Tr::tr("%1 PDO Entry Index").arg(moduleContext),
        unsupportedFeatures);
    if (const XmlElement *nameElement = child(element, "Name")) {
        reportUnknownAttributes(
            *nameElement,
            {"LcId"},
            Tr::tr("%1 PDO Entry Name").arg(moduleContext),
            unsupportedFeatures);
    }
    for (const QString &elementName : {QString("SubIndex"), QString("BitLen"), QString("DataType")}) {
        if (const XmlElement *valueElement = child(element, elementName)) {
            reportUnknownAttributes(
                *valueElement,
                {},
                Tr::tr("%1 PDO Entry %2").arg(moduleContext, elementName),
                unsupportedFeatures);
        }
    }
    return entry;
}

static Utils::Result<Data::PdoDescription> parseModulePdo(
    const XmlElement &element,
    Data::PdoDirection direction,
    const QString &moduleContext,
    QStringList *warnings,
    QStringList *unsupportedFeatures)
{
    const QList<const XmlElement *> indexElements = children(element, "Index");
    if (indexElements.size() != 1) {
        return Utils::ResultError(
            Tr::tr("%1 contains a PDO without exactly one Index.").arg(moduleContext));
    }
    const XmlElement *indexElement = indexElements.constFirst();
    const std::optional<quint64> index
        = parsedUnsigned(indexElement->text, 0xffff);
    if (!index || *index == 0) {
        return Utils::ResultError(
            Tr::tr("%1 contains a PDO with an invalid Index.").arg(moduleContext));
    }
    const std::optional<quint64> syncManager = parsedUnsigned(
        attribute(element, "Sm"), std::numeric_limits<int>::max());
    if (!syncManager) {
        return Utils::ResultError(
            Tr::tr("%1 contains PDO 0x%2 with an invalid Sm value.")
                .arg(moduleContext)
                .arg(*index, 4, 16, QLatin1Char('0')));
    }

    Data::PdoDescription pdo = parsePdo(element, direction, warnings);
    if (hasAttribute(*indexElement, "DependOnSlot")) {
        const Utils::Result<bool> dependsOnSlot = strictBooleanAttribute(
            *indexElement, "DependOnSlot", false, Tr::tr("%1 PDO Index").arg(moduleContext));
        if (!dependsOnSlot)
            return Utils::ResultError(dependsOnSlot.error());
        pdo.indexDependsOnSlot = *dependsOnSlot;
    }
    for (const QString &booleanAttribute : {QString("Fixed"), QString("Mandatory")}) {
        if (!hasAttribute(element, booleanAttribute))
            continue;
        const Utils::Result<bool> parsed = strictBooleanAttribute(
            element,
            booleanAttribute,
            false,
            Tr::tr("%1 PDO 0x%2").arg(moduleContext).arg(*index, 4, 16, QLatin1Char('0')));
        if (!parsed)
            return Utils::ResultError(parsed.error());
        if (booleanAttribute == "Fixed")
            pdo.fixed = *parsed;
        else
            pdo.mandatory = *parsed;
    }

    pdo.entries.clear();
    for (const XmlElement *entryElement : children(element, "Entry")) {
        Utils::Result<Data::PdoEntryDescription> entry = parseModulePdoEntry(
            *entryElement, moduleContext, warnings, unsupportedFeatures);
        if (!entry)
            return Utils::ResultError(entry.error());
        pdo.entries.append(*entry);
    }

    reportUnknownAttributes(
        element,
        {"Sm", "Fixed", "Mandatory"},
        Tr::tr("%1 PDO 0x%2").arg(moduleContext).arg(*index, 4, 16, QLatin1Char('0')),
        unsupportedFeatures);
    reportUnknownChildren(
        element,
        {"Index", "Name", "Entry"},
        Tr::tr("%1 PDO 0x%2").arg(moduleContext).arg(*index, 4, 16, QLatin1Char('0')),
        unsupportedFeatures);
    reportUnknownAttributes(
        *indexElement,
        {"DependOnSlot"},
        Tr::tr("%1 PDO Index").arg(moduleContext),
        unsupportedFeatures);
    if (const XmlElement *nameElement = child(element, "Name")) {
        reportUnknownAttributes(
            *nameElement,
            {"LcId"},
            Tr::tr("%1 PDO Name").arg(moduleContext),
            unsupportedFeatures);
    }
    return pdo;
}

static Utils::Result<ParsedModuleDefinitions> parseModuleDefinitions(
    const XmlElement *modulesElement)
{
    ParsedModuleDefinitions result;
    if (!modulesElement)
        return result;

    reportUnknownAttributes(
        *modulesElement, {}, Tr::tr("ESI Modules"), &result.unsupportedFeatures);
    reportUnknownChildren(
        *modulesElement, {"Module"}, Tr::tr("ESI Modules"), &result.unsupportedFeatures);

    QSet<quint32> moduleIdentifiers;
    for (const XmlElement *moduleElement : children(*modulesElement, "Module")) {
        const QList<const XmlElement *> typeElements = children(*moduleElement, "Type");
        if (typeElements.size() != 1) {
            return Utils::ResultError(
                Tr::tr("ESI Module must contain exactly one Type element."));
        }
        const XmlElement &typeElement = *typeElements.constFirst();
        const std::optional<quint64> moduleIdent
            = parsedUnsigned(attribute(typeElement, "ModuleIdent"), 0xffffffff);
        if (!moduleIdent || *moduleIdent == 0) {
            return Utils::ResultError(
                Tr::tr("ESI Module Type has an invalid ModuleIdent value."));
        }
        if (moduleIdentifiers.contains(quint32(*moduleIdent))) {
            return Utils::ResultError(
                Tr::tr("ESI Modules contains duplicate ModuleIdent 0x%1.")
                    .arg(*moduleIdent, 8, 16, QLatin1Char('0')));
        }
        moduleIdentifiers.insert(quint32(*moduleIdent));

        ParsedModule parsed;
        parsed.description.moduleIdent = quint32(*moduleIdent);
        parsed.description.typeName = typeElement.text.trimmed();
        parsed.description.name = localizedChildText(*moduleElement, "Name");
        if (parsed.description.name.isEmpty())
            parsed.description.name = parsed.description.typeName;
        parsed.description.moduleClass = attribute(typeElement, "ModuleClass");
        const std::optional<quint64> pdoGroup = parsedUnsigned(
            attribute(typeElement, "ModulePdoGroup"), std::numeric_limits<int>::max());
        const QString context = Tr::tr("Module 0x%1")
                                    .arg(*moduleIdent, 8, 16, QLatin1Char('0'));
        if (parsed.description.typeName.isEmpty() || parsed.description.moduleClass.isEmpty()) {
            return Utils::ResultError(
                Tr::tr("%1 is missing Type text or ModuleClass.").arg(context));
        }
        if (!pdoGroup) {
            return Utils::ResultError(
                Tr::tr("%1 has an invalid ModulePdoGroup reference.").arg(context));
        }
        parsed.description.modulePdoGroupIndex = int(*pdoGroup);
        const QList<const XmlElement *> profileElements = children(*moduleElement, "Profile");
        if (profileElements.size() > 1) {
            return Utils::ResultError(
                Tr::tr("%1 must not contain more than one Profile.").arg(context));
        }

        reportUnknownAttributes(
            *moduleElement, {}, context, &parsed.unsupportedFeatures);
        reportUnknownAttributes(
            typeElement,
            {"ModuleIdent", "ModuleClass", "ModulePdoGroup"},
            Tr::tr("%1 Type").arg(context),
            &parsed.unsupportedFeatures);
        for (const XmlElement *nameElement : children(*moduleElement, "Name")) {
            reportUnknownAttributes(
                *nameElement,
                {"LcId"},
                Tr::tr("%1 Name").arg(context),
                &parsed.unsupportedFeatures);
        }
        for (const XmlElement &candidate : moduleElement->children) {
            if (isAllowedName(candidate.name, {"Type", "Name", "RxPdo", "TxPdo", "Profile"}))
                continue;
            appendUnsupportedFeature(
                &parsed.unsupportedFeatures,
                Tr::tr("%1 element '%2' is not interpreted.").arg(context, candidate.name));
        }

        QStringList warnings;
        for (const XmlElement *pdoElement : children(*moduleElement, "RxPdo")) {
            Utils::Result<Data::PdoDescription> pdo = parseModulePdo(
                *pdoElement,
                Data::PdoDirection::Rx,
                context,
                &warnings,
                &parsed.unsupportedFeatures);
            if (!pdo)
                return Utils::ResultError(pdo.error());
            parsed.description.rxPdos.append(*pdo);
        }
        for (const XmlElement *pdoElement : children(*moduleElement, "TxPdo")) {
            Utils::Result<Data::PdoDescription> pdo = parseModulePdo(
                *pdoElement,
                Data::PdoDirection::Tx,
                context,
                &warnings,
                &parsed.unsupportedFeatures);
            if (!pdo)
                return Utils::ResultError(pdo.error());
            parsed.description.txPdos.append(*pdo);
        }
        if (!profileElements.isEmpty()) {
            QStringList parameterWarnings;
            Utils::Result<QList<Data::ModuleParameterObjectDescription>> parameterObjects
                = parseModuleParameterProfile(
                    *profileElements.constFirst(), context, &parameterWarnings);
            if (!parameterObjects)
                return Utils::ResultError(parameterObjects.error());
            parsed.description.parameterObjects = *parameterObjects;
            parsed.description.parameterWarnings = parameterWarnings;
            parsed.description.parameterConfigurationSupported = parameterWarnings.isEmpty();
        }
        for (const QString &warning : std::as_const(warnings))
            appendUnsupportedFeature(&parsed.unsupportedFeatures, warning);
        result.modules.append(parsed);
    }
    return result;
}

static Utils::Result<ParsedModuleCatalog> parseModuleCatalog(
    const XmlElement &deviceElement,
    const ParsedModuleDefinitions &moduleDefinitions,
    QSet<quint32> *usedModuleIdentifiers)
{
    ParsedModuleCatalog result;
    const QList<const XmlElement *> slotsElements = children(deviceElement, "Slots");
    if (slotsElements.isEmpty())
        return result;
    if (slotsElements.size() != 1) {
        return Utils::ResultError(
            Tr::tr("ESI Device must not contain more than one Slots element."));
    }
    const XmlElement &slotsElement = *slotsElements.constFirst();
    result.description.available = true;

    const Utils::Result<bool> downloadModuleIdentList = strictBooleanAttribute(
        slotsElement,
        "DownloadModuleIdentList",
        false,
        Tr::tr("ESI Device Slots"));
    if (!downloadModuleIdentList)
        return Utils::ResultError(downloadModuleIdentList.error());
    result.description.downloadModuleIdentList = *downloadModuleIdentList;

    const std::optional<quint64> slotIndexIncrement = parsedUnsigned(
        attribute(slotsElement, "SlotIndexIncrement"), std::numeric_limits<int>::max());
    if (!slotIndexIncrement || *slotIndexIncrement == 0) {
        return Utils::ResultError(
            Tr::tr("ESI Device Slots has an invalid SlotIndexIncrement."));
    }
    result.description.slotIndexIncrement = int(*slotIndexIncrement);

    const std::optional<quint64> slotPdoIncrement = parsedUnsigned(
        attribute(slotsElement, "SlotPdoIncrement"), std::numeric_limits<int>::max());
    if (!slotPdoIncrement || *slotPdoIncrement == 0) {
        return Utils::ResultError(
            Tr::tr("ESI Device Slots has an invalid SlotPdoIncrement."));
    }
    result.description.slotPdoIncrement = int(*slotPdoIncrement);

    reportUnknownAttributes(
        slotsElement,
        {"DownloadModuleIdentList", "SlotIndexIncrement", "SlotPdoIncrement"},
        Tr::tr("ESI Device Slots"),
        &result.unsupportedFeatures);
    reportUnknownChildren(
        slotsElement,
        {"Slot", "ModulePdoGroup"},
        Tr::tr("ESI Device Slots"),
        &result.unsupportedFeatures);

    QSet<QString> allowedModuleClasses;
    for (const XmlElement *slotElement : children(slotsElement, "Slot")) {
        const std::optional<quint64> minimumInstances = parsedUnsigned(
            attribute(*slotElement, "MinInstances"), std::numeric_limits<int>::max());
        const std::optional<quint64> maximumInstances = parsedUnsigned(
            attribute(*slotElement, "MaxInstances"), std::numeric_limits<int>::max());
        if (!minimumInstances || !maximumInstances || *maximumInstances == 0
            || *minimumInstances > *maximumInstances) {
            return Utils::ResultError(
                Tr::tr("ESI Device Slot has invalid MinInstances or MaxInstances."));
        }

        Data::ModuleSlotConstraintDescription slot;
        slot.name = localizedChildText(*slotElement, "Name");
        slot.minimumInstances = int(*minimumInstances);
        slot.maximumInstances = int(*maximumInstances);
        reportUnknownAttributes(
            *slotElement,
            {"MinInstances", "MaxInstances"},
            Tr::tr("ESI Device Slot"),
            &result.unsupportedFeatures);
        reportUnknownChildren(
            *slotElement,
            {"Name", "ModuleClass"},
            Tr::tr("ESI Device Slot"),
            &result.unsupportedFeatures);

        QSet<QString> slotClasses;
        for (const XmlElement *classElement : children(*slotElement, "ModuleClass")) {
            if (children(*classElement, "Class").size() != 1) {
                return Utils::ResultError(
                    Tr::tr("ESI Device Slot ModuleClass must contain exactly one Class element."));
            }
            Data::ModuleClassDescription moduleClass;
            moduleClass.identifier = childText(*classElement, "Class");
            moduleClass.name = localizedChildText(*classElement, "Name");
            if (moduleClass.identifier.isEmpty()) {
                return Utils::ResultError(
                    Tr::tr("ESI Device Slot contains a ModuleClass without Class text."));
            }
            if (moduleClass.name.isEmpty())
                moduleClass.name = moduleClass.identifier;
            if (slotClasses.contains(moduleClass.identifier)) {
                return Utils::ResultError(
                    Tr::tr("ESI Device Slot contains duplicate ModuleClass '%1'.")
                        .arg(moduleClass.identifier));
            }
            slotClasses.insert(moduleClass.identifier);
            allowedModuleClasses.insert(moduleClass.identifier);
            slot.allowedModuleClasses.append(moduleClass);
            reportUnknownAttributes(
                *classElement,
                {},
                Tr::tr("ESI Device Slot ModuleClass '%1'").arg(moduleClass.identifier),
                &result.unsupportedFeatures);
            reportUnknownChildren(
                *classElement,
                {"Class", "Name"},
                Tr::tr("ESI Device Slot ModuleClass '%1'").arg(moduleClass.identifier),
                &result.unsupportedFeatures);
            if (const XmlElement *nameElement = child(*classElement, "Name")) {
                reportUnknownAttributes(
                    *nameElement,
                    {"LcId"},
                    Tr::tr("ESI Device Slot ModuleClass '%1' Name")
                        .arg(moduleClass.identifier),
                    &result.unsupportedFeatures);
            }
        }
        if (slot.allowedModuleClasses.isEmpty()) {
            return Utils::ResultError(
                Tr::tr("ESI Device Slot does not declare any ModuleClass."));
        }
        result.description.slotConstraints.append(slot);
    }
    if (result.description.slotConstraints.isEmpty())
        return Utils::ResultError(Tr::tr("ESI Device Slots does not contain a Slot."));

    int groupIndex = 0;
    for (const XmlElement *groupElement : children(slotsElement, "ModulePdoGroup")) {
        const std::optional<quint64> alignment = parsedUnsigned(
            attribute(*groupElement, "Alignment"), std::numeric_limits<int>::max());
        if (!alignment || *alignment == 0) {
            return Utils::ResultError(
                Tr::tr("ESI Device ModulePdoGroup has an invalid Alignment."));
        }
        Data::ModulePdoGroupDescription group;
        group.index = groupIndex++;
        group.alignment = int(*alignment);
        if (hasAttribute(*groupElement, "RxPdo")) {
            const std::optional<quint64> rxPdo
                = parsedUnsigned(attribute(*groupElement, "RxPdo"), 0xffff);
            if (!rxPdo) {
                return Utils::ResultError(
                    Tr::tr("ESI Device ModulePdoGroup has an invalid RxPdo."));
            }
            group.hasRxPdo = true;
            group.rxPdoIndex = quint16(*rxPdo);
        }
        if (hasAttribute(*groupElement, "TxPdo")) {
            const std::optional<quint64> txPdo
                = parsedUnsigned(attribute(*groupElement, "TxPdo"), 0xffff);
            if (!txPdo) {
                return Utils::ResultError(
                    Tr::tr("ESI Device ModulePdoGroup has an invalid TxPdo."));
            }
            group.hasTxPdo = true;
            group.txPdoIndex = quint16(*txPdo);
        }
        if (!group.hasRxPdo && !group.hasTxPdo) {
            return Utils::ResultError(
                Tr::tr("ESI Device ModulePdoGroup must declare RxPdo or TxPdo."));
        }
        reportUnknownAttributes(
            *groupElement,
            {"Alignment", "RxPdo", "TxPdo"},
            Tr::tr("ESI Device ModulePdoGroup %1").arg(group.index),
            &result.unsupportedFeatures);
        reportUnknownChildren(
            *groupElement,
            {},
            Tr::tr("ESI Device ModulePdoGroup %1").arg(group.index),
            &result.unsupportedFeatures);
        result.description.pdoGroups.append(group);
    }
    if (result.description.pdoGroups.isEmpty()) {
        return Utils::ResultError(
            Tr::tr("ESI Device Slots does not contain a ModulePdoGroup."));
    }

    for (const ParsedModule &module : moduleDefinitions.modules) {
        if (!allowedModuleClasses.contains(module.description.moduleClass))
            continue;
        if (module.description.modulePdoGroupIndex < 0
            || module.description.modulePdoGroupIndex >= result.description.pdoGroups.size()) {
            return Utils::ResultError(
                Tr::tr("Module 0x%1 references unavailable ModulePdoGroup %2.")
                    .arg(module.description.moduleIdent, 8, 16, QLatin1Char('0'))
                    .arg(module.description.modulePdoGroupIndex));
        }
        const Data::ModulePdoGroupDescription &group
            = result.description.pdoGroups.at(module.description.modulePdoGroupIndex);
        for (const Data::PdoDescription &pdo : module.description.rxPdos) {
            if (!group.hasRxPdo || pdo.index != group.rxPdoIndex) {
                return Utils::ResultError(
                    Tr::tr("Module 0x%1 RxPDO 0x%2 does not match ModulePdoGroup %3.")
                        .arg(module.description.moduleIdent, 8, 16, QLatin1Char('0'))
                        .arg(pdo.index, 4, 16, QLatin1Char('0'))
                        .arg(group.index));
            }
        }
        for (const Data::PdoDescription &pdo : module.description.txPdos) {
            if (!group.hasTxPdo || pdo.index != group.txPdoIndex) {
                return Utils::ResultError(
                    Tr::tr("Module 0x%1 TxPDO 0x%2 does not match ModulePdoGroup %3.")
                        .arg(module.description.moduleIdent, 8, 16, QLatin1Char('0'))
                        .arg(pdo.index, 4, 16, QLatin1Char('0'))
                        .arg(group.index));
            }
        }
        result.description.modules.append(module.description);
        usedModuleIdentifiers->insert(module.description.moduleIdent);
        for (const QString &feature : module.unsupportedFeatures)
            appendUnsupportedFeature(&result.unsupportedFeatures, feature);
    }
    if (result.description.modules.isEmpty()) {
        QStringList definedModuleClasses;
        for (const ParsedModule &module : moduleDefinitions.modules) {
            if (!definedModuleClasses.contains(module.description.moduleClass))
                definedModuleClasses.append(module.description.moduleClass);
        }
        return Utils::ResultError(
            Tr::tr("ESI Device Slots has no matching Module definitions; available ModuleClass "
                   "values are: %1.")
                .arg(definedModuleClasses.join(", ")));
    }
    for (const QString &feature : moduleDefinitions.unsupportedFeatures)
        appendUnsupportedFeature(&result.unsupportedFeatures, feature);
    return result;
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
    const ParsedModuleDefinitions &moduleDefinitions,
    QSet<quint32> *usedModuleIdentifiers,
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

    Utils::Result<ParsedModuleCatalog> moduleCatalog
        = parseModuleCatalog(element, moduleDefinitions, usedModuleIdentifiers);
    if (!moduleCatalog)
        return Utils::ResultError(moduleCatalog.error());
    device.moduleCatalog = moduleCatalog->description;
    for (const QString &feature : moduleCatalog->unsupportedFeatures) {
        appendUnsupportedFeature(&device.unsupportedFeatures, feature);
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

    const QList<const XmlElement *> modulesCollections = children(*descriptions, "Modules");
    if (modulesCollections.size() > 1) {
        return Utils::ResultError(
            Tr::tr("ESI Descriptions must not contain more than one Modules collection."));
    }
    const Utils::Result<ParsedModuleDefinitions> moduleDefinitions = parseModuleDefinitions(
        modulesCollections.isEmpty() ? nullptr : modulesCollections.constFirst());
    if (!moduleDefinitions)
        return Utils::ResultError(Tr::tr("%1: %2").arg(sourcePath, moduleDefinitions.error()));

    const QByteArray sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
    QList<Data::DeviceDescription> result;
    QSet<quint32> usedModuleIdentifiers;
    for (const XmlElement *device : children(*devices, "Device")) {
        Utils::Result<Data::DeviceDescription> parsed = parseDevice(
            *device,
            quint32(*vendorId),
            groupNames,
            *moduleDefinitions,
            &usedModuleIdentifiers,
            sourcePath,
            sha256,
            importedAt);
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
    for (const ParsedModule &module : moduleDefinitions->modules) {
        if (!usedModuleIdentifiers.contains(module.description.moduleIdent)) {
            return Utils::ResultError(
                Tr::tr(
                    "%1: Module 0x%2 uses dangling ModuleClass '%3' that is not allowed by any "
                    "Device Slot.")
                    .arg(sourcePath)
                    .arg(module.description.moduleIdent, 8, 16, QLatin1Char('0'))
                    .arg(module.description.moduleClass));
        }
    }
    return result;
}

} // namespace EtherCAT::Devices::Internal
