// Copyright (C) 2026 Kvell

#include "coeonlinepage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <utils/fancylineedit.h>
#include <utils/infolabel.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>

#include <QAbstractItemModel>
#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace EtherCAT::Workbench::Internal {

enum CoeObjectRole {
    AddressRole = Qt::UserRole + 1,
    RawValueRole,
    DataTypeRole,
    RawDataTypeRole,
    WritableRole,
    ProcessDataRole,
};

enum class DictionaryRange {
    All,
    Communication,
    VendorSpecific,
    ProfileSpecific,
};

struct CoeObjectDefinition
{
    quint16 index = 0;
    quint8 subIndex = 0;
    QString name;
    bool writable = false;
    bool processData = false;
    Data::EtherCATDataType dataType = Data::EtherCATDataType::Unknown;
    QString rawDataType;
    QByteArray offlineValue;
    QString unit;
};

struct CoeObjectItem
{
    quint16 index = 0;
    quint8 subIndex = 0;
    QString name;
    bool writable = false;
    bool processData = false;
    bool synthetic = false;
    Data::EtherCATDataType dataType = Data::EtherCATDataType::Unknown;
    QString rawDataType;
    QByteArray offlineValue;
    QByteArray mockValue;
    bool mockValueEdited = false;
    QString unit;
    CoeObjectItem *parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<CoeObjectItem>> children;
};

static quint32 address(quint16 index, quint8 subIndex)
{
    return (quint32(index) << 8) | subIndex;
}

static QString indexText(quint16 index, quint8 subIndex)
{
    return QString("%1:%2")
        .arg(index, 4, 16, QLatin1Char('0'))
        .arg(subIndex, 2, 16, QLatin1Char('0'))
        .toUpper();
}

static QString dataTypeName(Data::EtherCATDataType type, const QString &rawType = {})
{
    switch (type) {
    case Data::EtherCATDataType::Boolean:
        return "BOOL";
    case Data::EtherCATDataType::Integer8:
        return "INT8";
    case Data::EtherCATDataType::UnsignedInteger8:
        return "UINT8";
    case Data::EtherCATDataType::Integer16:
        return "INT16";
    case Data::EtherCATDataType::UnsignedInteger16:
        return "UINT16";
    case Data::EtherCATDataType::Integer32:
        return "INT32";
    case Data::EtherCATDataType::UnsignedInteger32:
        return "UINT32";
    case Data::EtherCATDataType::Integer64:
        return "INT64";
    case Data::EtherCATDataType::UnsignedInteger64:
        return "UINT64";
    case Data::EtherCATDataType::Real32:
        return "REAL32";
    case Data::EtherCATDataType::Real64:
        return "REAL64";
    case Data::EtherCATDataType::VisibleString:
        return "STRING";
    case Data::EtherCATDataType::OctetString:
        return "OCTET_STRING";
    case Data::EtherCATDataType::Unknown:
        return rawType.isEmpty() ? Tr::tr("Unknown") : rawType;
    }
    return Tr::tr("Unknown");
}

static QString valueText(const CoeObjectItem &item, const QByteArray &value)
{
    if (!item.children.empty())
        return Tr::tr(">%1<").arg(qulonglong(item.children.size()));
    if (item.dataType == Data::EtherCATDataType::VisibleString)
        return QString::fromUtf8(value);
    const auto unsignedText = [](quint64 number, int width) {
        const QString hexadecimal
            = QString::number(number, 16).toUpper().rightJustified(width, QLatin1Char('0'));
        return QString("0x%1 (%2)").arg(hexadecimal).arg(number);
    };
    const auto signedText = [](quint64 bits, qint64 number, int width) {
        const QString hexadecimal
            = QString::number(bits, 16).toUpper().rightJustified(width, QLatin1Char('0'));
        return QString("0x%1 (%2)").arg(hexadecimal).arg(number);
    };
    switch (item.dataType) {
    case Data::EtherCATDataType::Boolean:
        if (value.size() == 1)
            return quint8(value.at(0)) == 0 ? Tr::tr("FALSE") : Tr::tr("TRUE");
        break;
    case Data::EtherCATDataType::UnsignedInteger8:
        if (value.size() == 1)
            return unsignedText(quint8(value.at(0)), 2);
        break;
    case Data::EtherCATDataType::Integer8:
        if (value.size() == 1)
            return signedText(quint8(value.at(0)), qint8(value.at(0)), 2);
        break;
    case Data::EtherCATDataType::UnsignedInteger16:
        if (value.size() == 2)
            return unsignedText(qFromLittleEndian<quint16>(value.constData()), 4);
        break;
    case Data::EtherCATDataType::Integer16:
        if (value.size() == 2) {
            const quint16 bits = qFromLittleEndian<quint16>(value.constData());
            return signedText(bits, qint16(bits), 4);
        }
        break;
    case Data::EtherCATDataType::UnsignedInteger32:
        if (value.size() == 4)
            return unsignedText(qFromLittleEndian<quint32>(value.constData()), 8);
        break;
    case Data::EtherCATDataType::Integer32:
        if (value.size() == 4) {
            const quint32 bits = qFromLittleEndian<quint32>(value.constData());
            return signedText(bits, qint32(bits), 8);
        }
        break;
    case Data::EtherCATDataType::UnsignedInteger64:
        if (value.size() == 8)
            return unsignedText(qFromLittleEndian<quint64>(value.constData()), 16);
        break;
    case Data::EtherCATDataType::Integer64:
        if (value.size() == 8) {
            const quint64 bits = qFromLittleEndian<quint64>(value.constData());
            return signedText(bits, qint64(bits), 16);
        }
        break;
    case Data::EtherCATDataType::Unknown:
    case Data::EtherCATDataType::Real32:
    case Data::EtherCATDataType::Real64:
    case Data::EtherCATDataType::VisibleString:
    case Data::EtherCATDataType::OctetString:
        break;
    }
    return value.isEmpty() ? QString() : "0x" + QString::fromLatin1(value.toHex().toUpper());
}

enum class RawValueParseError {
    None,
    Empty,
    IncompleteByte,
    InvalidHex,
};

struct RawValueParseResult
{
    QByteArray value;
    RawValueParseError error = RawValueParseError::None;
    int hexadecimalDigitCount = 0;
};

static RawValueParseResult parseRawValue(const QString &source)
{
    QString compact;
    compact.reserve(source.size());
    for (const QChar character : source.trimmed()) {
        if (character.isSpace() || character == ':' || character == '_')
            continue;
        compact.append(character);
    }
    if (compact.startsWith("0x", Qt::CaseInsensitive))
        compact.remove(0, 2);
    RawValueParseResult result;
    result.hexadecimalDigitCount = compact.size();
    if (compact.isEmpty()) {
        result.error = RawValueParseError::Empty;
        return result;
    }
    static const QString hexadecimal = "0123456789abcdefABCDEF";
    if (std::any_of(compact.cbegin(), compact.cend(), [](QChar character) {
            return !hexadecimal.contains(character);
        })) {
        result.error = RawValueParseError::InvalidHex;
        return result;
    }
    if (compact.size() % 2 != 0) {
        result.error = RawValueParseError::IncompleteByte;
        return result;
    }
    result.value = QByteArray::fromHex(compact.toLatin1());
    return result;
}

static QString digitCountText(int count)
{
    return count == 1 ? Tr::tr("1 digit") : Tr::tr("%1 digits").arg(count);
}

static QString byteCountText(int count)
{
    return count == 1 ? Tr::tr("1 byte") : Tr::tr("%1 bytes").arg(count);
}

static QByteArray unsigned32Value(quint32 value)
{
    QByteArray bytes(sizeof(value), Qt::Uninitialized);
    qToLittleEndian(value, bytes.data());
    return bytes;
}

static bool fixedStartupTransition(const QString &transition)
{
    const QString trimmed = transition.trimmed();
    return trimmed.startsWith('<') && trimmed.endsWith('>');
}

class CoeObjectModel final : public QAbstractItemModel
{
public:
    enum Column { Index, Name, Flags, Value, Unit, ColumnCount };

    enum class MockValueEditRejection {
        None,
        NotEditable,
        Empty,
        IncompleteByte,
        InvalidHex,
        WidthMismatch,
    };

    struct MockValueEditValidation
    {
        QByteArray value;
        MockValueEditRejection rejection = MockValueEditRejection::None;
        int hexadecimalDigitCount = 0;
        int expectedSize = 0;
        int actualSize = 0;
    };

    struct MockValueOverride
    {
        QByteArray value;
        QByteArray offlineValue;
        Data::EtherCATDataType dataType = Data::EtherCATDataType::Unknown;
        QString rawDataType;
        bool writable = false;
        bool processData = false;
    };

    using MockValueOverrides = QMap<quint32, MockValueOverride>;

    explicit CoeObjectModel(QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {}

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const final
    {
        if (row < 0 || column < 0 || column >= ColumnCount)
            return {};
        const auto *parentItem = item(parent);
        const auto &children = parentItem ? parentItem->children : m_roots;
        if (row >= int(children.size()))
            return {};
        return createIndex(row, column, children.at(row).get());
    }

    QModelIndex parent(const QModelIndex &child) const final
    {
        const CoeObjectItem *childItem = item(child);
        if (!childItem || !childItem->parent)
            return {};
        return createIndex(childItem->parent->row, 0, childItem->parent);
    }

    int rowCount(const QModelIndex &parent = {}) const final
    {
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const CoeObjectItem *parentItem = item(parent);
        return parentItem ? int(parentItem->children.size()) : int(m_roots.size());
    }

    int columnCount(const QModelIndex & = {}) const final { return ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const final
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case Index:
            return Tr::tr("Index");
        case Name:
            return Tr::tr("Name");
        case Flags:
            return Tr::tr("Flags");
        case Value:
            return Tr::tr("Value");
        case Unit:
            return Tr::tr("Unit");
        default:
            return {};
        }
    }

    QVariant data(const QModelIndex &modelIndex, int role = Qt::DisplayRole) const final
    {
        const CoeObjectItem *object = item(modelIndex);
        if (!object)
            return {};
        const QByteArray value = m_showOffline ? object->offlineValue : object->mockValue;
        const auto displayText = [&]() -> QString {
            switch (modelIndex.column()) {
            case Index:
                return indexText(object->index, object->subIndex);
            case Name:
                return object->name;
            case Flags:
                return QString("%1%2").arg(
                    object->writable && !object->synthetic ? "RW" : "RO",
                    object->processData ? " P" : "");
            case Value:
                return valueText(*object, value);
            case Unit:
                return object->unit;
            default:
                return {};
            }
        };
        if (role == AddressRole)
            return address(object->index, object->subIndex);
        if (role == RawValueRole)
            return value;
        if (role == DataTypeRole)
            return QVariant::fromValue(object->dataType);
        if (role == RawDataTypeRole)
            return object->rawDataType;
        if (role == WritableRole)
            return object->writable && !object->synthetic;
        if (role == ProcessDataRole)
            return object->processData;
        if (role == Qt::EditRole && modelIndex.column() == Value)
            return QString::fromLatin1(value.toHex().toUpper());
        if (role == Qt::DisplayRole)
            return displayText();
        if (role == Qt::AccessibleTextRole)
            return displayText();
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            const QString displayed = displayText();
            const QString heading
                = headerData(modelIndex.column(), Qt::Horizontal, Qt::DisplayRole).toString();
            const QString operation
                = flags(modelIndex) & Qt::ItemIsEditable
                      ? Tr::tr("Edit this local Mock value temporarily; this does not change an "
                               "offline Project.")
                      : Tr::tr("This cell is read-only in the current view.");
            return Tr::tr(
                       "Object %1, %2 column: %3. Data type: %4. Source: %5. Access flags are "
                       "a local engineering prototype. No controller connection or SDO transfer "
                       "occurs. %6")
                .arg(
                    indexText(object->index, object->subIndex),
                    heading,
                    displayed.isEmpty() ? Tr::tr("empty") : displayed,
                    dataTypeName(object->dataType, object->rawDataType),
                    m_showOffline ? Tr::tr("offline") : Tr::tr("Mock"),
                    operation);
        }
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex &modelIndex) const final
    {
        const CoeObjectItem *object = item(modelIndex);
        if (!object)
            return Qt::NoItemFlags;
        Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (m_mockValueEditingEnabled && !m_showOffline && modelIndex.column() == Value
            && object->writable && !object->synthetic) {
            result |= Qt::ItemIsEditable;
        }
        return result;
    }

    bool setData(const QModelIndex &modelIndex, const QVariant &value, int role) final
    {
        if (role != Qt::EditRole)
            return false;
        const MockValueEditValidation validation
            = validateMockValueEdit(modelIndex, value.toString());
        if (validation.rejection != MockValueEditRejection::None)
            return false;
        CoeObjectItem *object = item(modelIndex);
        QTC_ASSERT(object, return false);
        object->mockValue = validation.value;
        object->mockValueEdited = true;
        emit dataChanged(
            modelIndex,
            modelIndex,
            {Qt::DisplayRole,
             Qt::EditRole,
             Qt::AccessibleTextRole,
             Qt::AccessibleDescriptionRole,
             Qt::ToolTipRole,
             RawValueRole});
        return true;
    }

    MockValueEditValidation validateMockValueEdit(
        const QModelIndex &modelIndex, const QString &text) const
    {
        MockValueEditValidation result;
        const CoeObjectItem *object = item(modelIndex);
        if (modelIndex.column() != Value || !object || !m_mockValueEditingEnabled || m_showOffline
            || !object->writable || object->synthetic) {
            result.rejection = MockValueEditRejection::NotEditable;
            return result;
        }
        const RawValueParseResult parsed = parseRawValue(text);
        result.value = parsed.value;
        result.hexadecimalDigitCount = parsed.hexadecimalDigitCount;
        switch (parsed.error) {
        case RawValueParseError::Empty:
            result.rejection = MockValueEditRejection::Empty;
            return result;
        case RawValueParseError::IncompleteByte:
            result.rejection = MockValueEditRejection::IncompleteByte;
            return result;
        case RawValueParseError::InvalidHex:
            result.rejection = MockValueEditRejection::InvalidHex;
            return result;
        case RawValueParseError::None:
            break;
        }
        result.expectedSize = object->mockValue.isEmpty() ? object->offlineValue.size()
                                                          : object->mockValue.size();
        result.actualSize = parsed.value.size();
        if (result.expectedSize > 0 && result.actualSize != result.expectedSize)
            result.rejection = MockValueEditRejection::WidthMismatch;
        return result;
    }

    MockValueOverrides mockValueOverrides() const
    {
        MockValueOverrides result;
        const auto collect = [&result](auto &&self, const CoeObjectItem *item) -> void {
            if (item->mockValueEdited && item->writable && !item->synthetic) {
                result.insert(
                    address(item->index, item->subIndex),
                    {item->mockValue,
                     item->offlineValue,
                     item->dataType,
                     item->rawDataType,
                     item->writable,
                     item->processData});
            }
            for (const auto &child : item->children)
                self(self, child.get());
        };
        for (const auto &root : m_roots)
            collect(collect, root.get());
        return result;
    }

    bool canSynchronizeDefinitions(
        const QList<CoeObjectDefinition> &definitions,
        bool mockValueEditingEnabled,
        int generation,
        const MockValueOverrides &preservedOverrides,
        quint32 preservedAddress) const
    {
        std::vector<std::unique_ptr<CoeObjectItem>> targetItems = itemsForDefinitions(definitions);
        refreshItems(
            targetItems, generation, preservedOverrides, mockValueEditingEnabled);
        return canSynchronizeItem(
            targetItems, mockValueEditingEnabled, preservedAddress);
    }

    bool setDefinitions(
        const QList<CoeObjectDefinition> &definitions,
        bool mockValueEditingEnabled,
        int generation = 0,
        const MockValueOverrides &preservedOverrides = {},
        const std::optional<quint32> &preservedAddress = {})
    {
        std::vector<std::unique_ptr<CoeObjectItem>> targetItems = itemsForDefinitions(definitions);
        refreshItems(
            targetItems, generation, preservedOverrides, mockValueEditingEnabled);
        if (preservedAddress
            && canSynchronizeItem(targetItems, mockValueEditingEnabled, *preservedAddress)) {
            m_mockValueEditingEnabled = mockValueEditingEnabled;
            synchronizeItems(m_roots, targetItems, nullptr, {}, *preservedAddress);
            return false;
        }

        beginResetModel();
        m_mockValueEditingEnabled = mockValueEditingEnabled;
        m_roots = std::move(targetItems);
        endResetModel();
        return true;
    }

    void setShowOffline(bool showOffline)
    {
        if (m_showOffline == showOffline)
            return;
        beginResetModel();
        m_showOffline = showOffline;
        endResetModel();
    }

    bool showOffline() const { return m_showOffline; }

    void refreshMockValues(
        int generation, const MockValueOverrides &preservedOverrides = {})
    {
        beginResetModel();
        refreshItems(
            m_roots, generation, preservedOverrides, m_mockValueEditingEnabled);
        endResetModel();
    }

private:
    static std::vector<std::unique_ptr<CoeObjectItem>> itemsForDefinitions(
        const QList<CoeObjectDefinition> &definitions)
    {
        std::vector<std::unique_ptr<CoeObjectItem>> roots;
        QMap<quint16, QList<CoeObjectDefinition>> groups;
        for (const CoeObjectDefinition &definition : definitions)
            groups[definition.index].append(definition);
        for (auto group = groups.cbegin(); group != groups.cend(); ++group) {
            QList<CoeObjectDefinition> values = group.value();
            std::sort(values.begin(), values.end(), [](const auto &left, const auto &right) {
                return left.subIndex < right.subIndex;
            });
            const auto zero = std::find_if(values.cbegin(), values.cend(), [](const auto &value) {
                return value.subIndex == 0;
            });
            const QString groupIndex
                = QString::number(group.key(), 16).toUpper().rightJustified(4, QLatin1Char('0'));
            const CoeObjectDefinition rootDefinition
                = zero != values.cend() ? *zero
                                        : CoeObjectDefinition{
                                              group.key(),
                                              0,
                                              Tr::tr("Object 0x%1").arg(groupIndex),
                                              false,
                                              false,
                                              Data::EtherCATDataType::Unknown,
                                              {},
                                              {},
                                              {}};
            auto root = createItem(rootDefinition, nullptr, int(roots.size()));
            root->synthetic = zero == values.cend();
            for (const CoeObjectDefinition &definition : std::as_const(values)) {
                if (definition.subIndex == 0)
                    continue;
                root->children.push_back(
                    createItem(definition, root.get(), int(root->children.size())));
            }
            roots.push_back(std::move(root));
        }
        return roots;
    }

    static void refreshItems(
        std::vector<std::unique_ptr<CoeObjectItem>> &roots,
        int generation,
        const MockValueOverrides &preservedOverrides,
        bool mockValueEditingEnabled)
    {
        const auto refresh = [&preservedOverrides, generation, mockValueEditingEnabled](
                                 auto &&self, CoeObjectItem *item) -> void {
            item->mockValueEdited = false;
            if (!item->synthetic) {
                item->mockValue = item->offlineValue;
                if (!item->mockValue.isEmpty()
                    && item->dataType != Data::EtherCATDataType::VisibleString
                    && (item->writable || item->processData)) {
                    item->mockValue[0]
                        = char((quint8(item->mockValue.at(0)) + generation) & 0xff);
                }
            }
            const auto preserved = preservedOverrides.constFind(
                address(item->index, item->subIndex));
            if (preserved != preservedOverrides.cend() && mockValueEditingEnabled
                && item->writable && !item->synthetic
                && preserved->offlineValue == item->offlineValue
                && preserved->dataType == item->dataType
                && preserved->rawDataType == item->rawDataType
                && preserved->writable == item->writable
                && preserved->processData == item->processData
                && (item->offlineValue.isEmpty()
                    || preserved->value.size() == item->mockValue.size())) {
                item->mockValue = preserved->value;
                item->mockValueEdited = true;
            }
            for (const auto &child : item->children)
                self(self, child.get());
        };
        for (const auto &root : roots)
            refresh(refresh, root.get());
    }

    static const CoeObjectItem *findItem(
        const std::vector<std::unique_ptr<CoeObjectItem>> &items, quint32 objectAddress)
    {
        for (const auto &item : items) {
            if (address(item->index, item->subIndex) == objectAddress)
                return item.get();
            if (const CoeObjectItem *child = findItem(item->children, objectAddress))
                return child;
        }
        return nullptr;
    }

    bool canSynchronizeItem(
        const std::vector<std::unique_ptr<CoeObjectItem>> &targetItems,
        bool mockValueEditingEnabled,
        quint32 preservedAddress) const
    {
        const CoeObjectItem *current = findItem(m_roots, preservedAddress);
        const CoeObjectItem *target = findItem(targetItems, preservedAddress);
        return m_mockValueEditingEnabled && mockValueEditingEnabled && !m_showOffline && current
               && target && !current->synthetic && !target->synthetic && current->writable
               && target->writable && current->index == target->index
               && current->subIndex == target->subIndex
               && current->processData == target->processData
               && current->dataType == target->dataType
               && current->rawDataType == target->rawDataType
               && current->offlineValue == target->offlineValue
               && current->mockValue == target->mockValue
               && current->mockValueEdited == target->mockValueEdited
               && current->children.size() == target->children.size();
    }

    static bool sameItemData(const CoeObjectItem &left, const CoeObjectItem &right)
    {
        return left.index == right.index && left.subIndex == right.subIndex
               && left.name == right.name && left.writable == right.writable
               && left.processData == right.processData && left.synthetic == right.synthetic
               && left.dataType == right.dataType && left.rawDataType == right.rawDataType
               && left.offlineValue == right.offlineValue && left.mockValue == right.mockValue
               && left.mockValueEdited == right.mockValueEdited && left.unit == right.unit;
    }

    static void copyItemData(CoeObjectItem *target, const CoeObjectItem &source)
    {
        target->index = source.index;
        target->subIndex = source.subIndex;
        target->name = source.name;
        target->writable = source.writable;
        target->processData = source.processData;
        target->synthetic = source.synthetic;
        target->dataType = source.dataType;
        target->rawDataType = source.rawDataType;
        target->offlineValue = source.offlineValue;
        target->mockValue = source.mockValue;
        target->mockValueEdited = source.mockValueEdited;
        target->unit = source.unit;
    }

    static void updateRows(
        std::vector<std::unique_ptr<CoeObjectItem>> &items, CoeObjectItem *parent)
    {
        for (int row = 0; row < int(items.size()); ++row) {
            items.at(row)->parent = parent;
            items.at(row)->row = row;
        }
    }

    void synchronizeItems(
        std::vector<std::unique_ptr<CoeObjectItem>> &currentItems,
        std::vector<std::unique_ptr<CoeObjectItem>> &targetItems,
        CoeObjectItem *parentItem,
        const QModelIndex &parentIndex,
        quint32 preservedAddress)
    {
        const auto containsAddress = [](const auto &items, quint32 objectAddress) {
            return std::any_of(items.cbegin(), items.cend(), [objectAddress](const auto &item) {
                return address(item->index, item->subIndex) == objectAddress;
            });
        };
        for (int row = int(currentItems.size()) - 1; row >= 0; --row) {
            const quint32 currentAddress
                = address(currentItems.at(row)->index, currentItems.at(row)->subIndex);
            if (containsAddress(targetItems, currentAddress))
                continue;
            beginRemoveRows(parentIndex, row, row);
            currentItems.erase(currentItems.begin() + row);
            updateRows(currentItems, parentItem);
            endRemoveRows();
        }

        for (int targetRow = 0; targetRow < int(targetItems.size()); ++targetRow) {
            const quint32 targetAddress
                = address(targetItems.at(targetRow)->index, targetItems.at(targetRow)->subIndex);
            int currentRow = -1;
            for (int row = 0; row < int(currentItems.size()); ++row) {
                if (address(currentItems.at(row)->index, currentItems.at(row)->subIndex)
                    == targetAddress) {
                    currentRow = row;
                    break;
                }
            }
            if (currentRow < 0) {
                beginInsertRows(parentIndex, targetRow, targetRow);
                currentItems.insert(
                    currentItems.begin() + targetRow, std::move(targetItems.at(targetRow)));
                updateRows(currentItems, parentItem);
                endInsertRows();
                continue;
            }
            if (currentRow != targetRow) {
                const int destination = currentRow < targetRow ? targetRow + 1 : targetRow;
                if (!beginMoveRows(
                        parentIndex, currentRow, currentRow, parentIndex, destination)) {
                    QTC_CHECK(false);
                    continue;
                }
                std::unique_ptr<CoeObjectItem> moved = std::move(currentItems.at(currentRow));
                currentItems.erase(currentItems.begin() + currentRow);
                currentItems.insert(currentItems.begin() + targetRow, std::move(moved));
                updateRows(currentItems, parentItem);
                endMoveRows();
            }

            CoeObjectItem *current = currentItems.at(targetRow).get();
            CoeObjectItem *target = targetItems.at(targetRow).get();
            const bool itemDataChanged = !sameItemData(*current, *target);
            const qsizetype previousChildCount = qsizetype(current->children.size());
            copyItemData(current, *target);
            const QModelIndex currentIndex = index(targetRow, 0, parentIndex);
            synchronizeItems(
                current->children,
                target->children,
                current,
                currentIndex,
                preservedAddress);
            if (!itemDataChanged && previousChildCount == qsizetype(current->children.size()))
                continue;
            if (targetAddress == preservedAddress) {
                emit dataChanged(
                    index(targetRow, 0, parentIndex),
                    index(targetRow, Value - 1, parentIndex));
                emit dataChanged(
                    index(targetRow, Value + 1, parentIndex),
                    index(targetRow, ColumnCount - 1, parentIndex));
            } else {
                emit dataChanged(
                    index(targetRow, 0, parentIndex),
                    index(targetRow, ColumnCount - 1, parentIndex));
            }
        }
    }

    static std::unique_ptr<CoeObjectItem> createItem(
        const CoeObjectDefinition &definition, CoeObjectItem *parent, int row)
    {
        auto item = std::make_unique<CoeObjectItem>();
        item->index = definition.index;
        item->subIndex = definition.subIndex;
        item->name = definition.name;
        item->writable = definition.writable;
        item->processData = definition.processData;
        item->dataType = definition.dataType;
        item->rawDataType = definition.rawDataType;
        item->offlineValue = definition.offlineValue;
        item->mockValue = definition.offlineValue;
        item->unit = definition.unit;
        item->parent = parent;
        item->row = row;
        return item;
    }

    static CoeObjectItem *item(const QModelIndex &modelIndex)
    {
        return modelIndex.isValid() ? static_cast<CoeObjectItem *>(modelIndex.internalPointer())
                                    : nullptr;
    }

    std::vector<std::unique_ptr<CoeObjectItem>> m_roots;
    bool m_mockValueEditingEnabled = false;
    bool m_showOffline = false;
};

class CoeFilterModel final : public QSortFilterProxyModel
{
public:
    explicit CoeFilterModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setFilterKeyColumn(-1);
        setRecursiveFilteringEnabled(true);
        setAutoAcceptChildRows(true);
    }

    void setDictionaryRange(DictionaryRange range)
    {
        if (m_range == range)
            return;
        m_range = range;
        invalidateRowsFilter();
    }

    void setHideStandard(bool hide)
    {
        if (m_hideStandard == hide)
            return;
        m_hideStandard = hide;
        invalidateRowsFilter();
    }

    void setHidePdo(bool hide)
    {
        if (m_hidePdo == hide)
            return;
        m_hidePdo = hide;
        invalidateRowsFilter();
    }

    bool hideStandard() const { return m_hideStandard; }
    bool hidePdo() const { return m_hidePdo; }
    DictionaryRange dictionaryRange() const { return m_range; }
    bool hasAdvancedFilters() const
    {
        return m_range != DictionaryRange::All || m_hideStandard || m_hidePdo;
    }

    void resetAdvancedFilters()
    {
        setDictionaryRange(DictionaryRange::All);
        setHideStandard(false);
        setHidePdo(false);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        const QModelIndex source = sourceModel()->index(sourceRow, 0, sourceParent);
        const quint16 index = quint16(source.data(AddressRole).toUInt() >> 8);
        if (m_hideStandard && index < 0x2000)
            return false;
        if (m_hidePdo && source.data(ProcessDataRole).toBool())
            return false;
        switch (m_range) {
        case DictionaryRange::Communication:
            if (index < 0x1000 || index > 0x1fff)
                return false;
            break;
        case DictionaryRange::VendorSpecific:
            if (index < 0x2000 || index > 0x5fff)
                return false;
            break;
        case DictionaryRange::ProfileSpecific:
            if (index < 0x6000 || index > 0x9fff)
                return false;
            break;
        case DictionaryRange::All:
            break;
        }
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

private:
    DictionaryRange m_range = DictionaryRange::All;
    bool m_hideStandard = false;
    bool m_hidePdo = false;
};

using CoeEditorOpenedHandler = std::function<void(QWidget *, const QModelIndex &)>;
using CoeEditorSubmittedHandler
    = std::function<void(const QModelIndex &, const QString &, bool accepted)>;

class CoeItemDelegate final : public QStyledItemDelegate
{
public:
    CoeItemDelegate(
        CoeEditorOpenedHandler editorOpened,
        CoeEditorSubmittedHandler editorSubmitted,
        QObject *parent)
        : QStyledItemDelegate(parent)
        , m_editorOpened(std::move(editorOpened))
        , m_editorSubmitted(std::move(editorSubmitted))
    {}

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const final
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor && m_editorOpened)
            m_editorOpened(editor, index);
        return editor;
    }

    void setModelData(
        QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const final
    {
        auto lineEdit = qobject_cast<QLineEdit *>(editor);
        if (!lineEdit) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }
        const QString text = lineEdit->text();
        const bool accepted = model && model->setData(index, text, Qt::EditRole);
        if (m_editorSubmitted)
            m_editorSubmitted(index, text, accepted);
    }

private:
    CoeEditorOpenedHandler m_editorOpened;
    CoeEditorSubmittedHandler m_editorSubmitted;
};

class CoeTreeView final : public QTreeView
{
public:
    using QTreeView::QTreeView;

    void discardInlineEditor(QWidget *editor)
    {
        closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
    }
};

static QModelIndex indexForAddress(
    const QAbstractItemModel *model, quint32 objectAddress, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data(AddressRole).toUInt() == objectAddress)
            return index;
        const QModelIndex child = indexForAddress(model, objectAddress, index);
        if (child.isValid())
            return child;
    }
    return {};
}

static void addOrReplaceDefinition(
    QList<CoeObjectDefinition> *definitions,
    const CoeObjectDefinition &definition,
    bool replaceExisting = true)
{
    const auto existing
        = std::find_if(definitions->begin(), definitions->end(), [&definition](const auto &entry) {
              return entry.index == definition.index && entry.subIndex == definition.subIndex;
          });
    if (existing == definitions->end())
        definitions->append(definition);
    else if (replaceExisting)
        *existing = definition;
}

static QList<CoeObjectDefinition> objectDefinitions(
    const std::optional<Data::OfflineSlaveConfiguration> &slave,
    const std::optional<Data::DeviceDescription> &device)
{
    QList<CoeObjectDefinition> definitions;
    const Data::DeviceIdentity identity = slave    ? slave->identity
                                          : device ? device->summary.identity
                                                   : Data::DeviceIdentity();
    const QString deviceName = slave    ? slave->name
                               : device ? device->summary.name
                                        : Tr::tr("Unknown");
    const quint32 serialNumber = slave ? slave->serialNumber : 0;
    definitions.append(
        {0x1008,
         0,
         Tr::tr("Device name"),
         false,
         false,
         Data::EtherCATDataType::VisibleString,
         "STRING",
         deviceName.toUtf8(),
         {}});
    definitions.append(
        {0x1018,
         0,
         Tr::tr("Identity"),
         false,
         false,
         Data::EtherCATDataType::UnsignedInteger8,
         "UINT8",
         QByteArray(1, char(4)),
         {}});
    definitions.append(
        {0x1018,
         1,
         Tr::tr("Vendor ID"),
         false,
         false,
         Data::EtherCATDataType::UnsignedInteger32,
         "UINT32",
         unsigned32Value(identity.vendorId),
         {}});
    definitions.append(
        {0x1018,
         2,
         Tr::tr("Product code"),
         false,
         false,
         Data::EtherCATDataType::UnsignedInteger32,
         "UINT32",
         unsigned32Value(identity.productCode),
         {}});
    definitions.append(
        {0x1018,
         3,
         Tr::tr("Revision"),
         false,
         false,
         Data::EtherCATDataType::UnsignedInteger32,
         "UINT32",
         unsigned32Value(identity.revisionNumber),
         {}});
    definitions.append(
        {0x1018,
         4,
         Tr::tr("Serial number"),
         false,
         false,
         Data::EtherCATDataType::UnsignedInteger32,
         "UINT32",
         unsigned32Value(serialNumber),
         {}});

    if (device) {
        for (const Data::StartupParameterDescription &parameter : device->startupParameters) {
            addOrReplaceDefinition(
                &definitions,
                {parameter.index,
                 parameter.subIndex,
                 parameter.comment.isEmpty() ? Tr::tr("Startup object") : parameter.comment,
                 !fixedStartupTransition(parameter.transition),
                 false,
                 Data::EtherCATDataType::Unknown,
                 {},
                 parameter.data,
                 {}},
                false);
        }
    }
    if (slave) {
        for (const Data::StartupParameterConfiguration &parameter : slave->startup.parameters) {
            addOrReplaceDefinition(
                &definitions,
                {parameter.index,
                 parameter.subIndex,
                 parameter.comment.isEmpty() ? Tr::tr("Startup object") : parameter.comment,
                 !fixedStartupTransition(parameter.transition),
                 false,
                 parameter.dataType,
                 parameter.rawDataType,
                 parameter.rawValue,
                 {}});
        }
    }

    const auto addPdoEntries = [&definitions](const auto &pdos) {
        for (const auto &pdo : pdos) {
            for (const auto &entry : pdo.entries) {
                if (entry.index == 0 || entry.bitLength <= 0)
                    continue;
                const auto existing = std::find_if(
                    definitions.begin(), definitions.end(), [&entry](const auto &definition) {
                        return definition.index == entry.index
                               && definition.subIndex == entry.subIndex;
                    });
                if (existing != definitions.end()) {
                    existing->processData = true;
                    continue;
                }
                addOrReplaceDefinition(
                    &definitions,
                    {entry.index,
                     entry.subIndex,
                     entry.name,
                     false,
                     true,
                     entry.dataType,
                     entry.rawDataType,
                     QByteArray(qMax(1, (entry.bitLength + 7) / 8), char(0)),
                     {}},
                    false);
            }
        }
    };
    if (slave && !slave->processData.pdos.isEmpty()) {
        addPdoEntries(slave->processData.pdos);
    } else if (device) {
        addPdoEntries(device->rxPdos);
        addPdoEntries(device->txPdos);
    }
    std::sort(definitions.begin(), definitions.end(), [](const auto &left, const auto &right) {
        return address(left.index, left.subIndex) < address(right.index, right.subIndex);
    });
    return definitions;
}

CoeOnlinePage::CoeOnlinePage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_banner(new Utils::InfoLabel(this))
    , m_feedback(new Utils::InfoLabel(this))
    , m_updateList(new QPushButton(Tr::tr("Update List"), this))
    , m_advanced(new QPushButton(Tr::tr("Advanced..."), this))
    , m_addToStartup(new QPushButton(Tr::tr("Add to Startup..."), this))
    , m_autoUpdate(new QCheckBox(Tr::tr("Auto Update"), this))
    , m_singleUpdate(new QCheckBox(Tr::tr("Single Update"), this))
    , m_showOffline(new QCheckBox(Tr::tr("Show Offline Data"), this))
    , m_dataSource(new QLabel(this))
    , m_moduleOd(new QLineEdit(this))
    , m_filter(new Utils::FancyLineEdit(this))
    , m_dictionaryStack(new QStackedWidget(this))
    , m_dictionary(new CoeTreeView(m_dictionaryStack))
    , m_filterEmptyState(new QWidget(m_dictionaryStack))
    , m_clearFilters(new QPushButton(Tr::tr("Clear Filters"), m_filterEmptyState))
    , m_model(new CoeObjectModel(this))
    , m_filterModel(new CoeFilterModel(this))
{
    setObjectName("EtherCATWorkbenchCoeOnlinePage");
    m_banner->setObjectName("EtherCATCoeMockBanner");
    m_banner->setType(Utils::InfoLabel::Warning);
    m_banner->setElideMode(Qt::ElideNone);
    m_banner->setWordWrap(true);
    m_feedback->setObjectName("EtherCATCoeFeedback");
    m_feedback->setElideMode(Qt::ElideNone);
    m_feedback->setWordWrap(true);
    m_feedback->setAccessibleName(Tr::tr("CoE operation feedback"));
    m_feedback->hide();
    m_updateList->setObjectName("EtherCATCoeUpdateList");
    m_advanced->setObjectName("EtherCATCoeAdvanced");
    m_addToStartup->setObjectName("EtherCATCoeAddToStartup");
    m_autoUpdate->setObjectName("EtherCATCoeAutoUpdate");
    m_autoUpdate->setEnabled(false);
    m_autoUpdate->setToolTip(
        Tr::tr("Automatic polling requires a future controller Provider and is unavailable."));
    m_singleUpdate->setObjectName("EtherCATCoeSingleUpdate");
    m_singleUpdate->setChecked(true);
    m_singleUpdate->setEnabled(false);
    m_singleUpdate->setToolTip(Tr::tr("Mock values update only when Update List is selected."));
    m_showOffline->setObjectName("EtherCATCoeShowOffline");
    m_dataSource->setObjectName("EtherCATCoeDataSource");
    m_dataSource->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_moduleOd->setObjectName("EtherCATCoeModuleOd");
    m_moduleOd->setText("0");
    m_moduleOd->setReadOnly(true);
    m_filter->setObjectName("EtherCATCoeFilter");
    m_filter->setFiltering(true);
    m_filter->setPlaceholderText(Tr::tr("Filter object dictionary"));
    m_filter->setAccessibleName(Tr::tr("Filter CoE object dictionary"));
    m_filter->setAccessibleDescription(
        Tr::tr("Filter local Mock and offline CoE objects by index, name, flags, value, or unit."));
    m_dictionaryStack->setObjectName("EtherCATCoeDictionaryResults");
    m_dictionary->setObjectName("EtherCATCoeObjectDictionary");
    m_dictionary->setAccessibleName(Tr::tr("CoE object dictionary"));
    m_dictionary->setAccessibleDescription(
        Tr::tr("Browse locally generated Mock and offline CoE object values."));
    m_dictionary->setAlternatingRowColors(true);
    m_dictionary->setUniformRowHeights(true);
    m_dictionary->setRootIsDecorated(true);
    m_dictionary->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dictionary->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dictionary->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    m_dictionary->setItemDelegate(new CoeItemDelegate(
        [this](QWidget *editor, const QModelIndex &index) {
            trackInlineEditor(editor, index);
        },
        [this](const QModelIndex &index, const QString &text, bool accepted) {
            handleInlineEditorSubmitted(index, text, accepted);
        },
        m_dictionary));
    m_filterModel->setSourceModel(m_model);
    const auto beginFilterResultChange = [this] { ++m_filterResultChangeDepth; };
    const auto finishFilterResultChange = [this] {
        refreshFilterResults();
        if (m_filterResultChangeDepth > 0)
            --m_filterResultChangeDepth;
    };
    connect(
        m_filterModel,
        &QAbstractItemModel::rowsAboutToBeInserted,
        this,
        beginFilterResultChange);
    connect(m_filterModel, &QAbstractItemModel::rowsInserted, this, finishFilterResultChange);
    connect(
        m_filterModel,
        &QAbstractItemModel::rowsAboutToBeRemoved,
        this,
        beginFilterResultChange);
    connect(m_filterModel, &QAbstractItemModel::rowsRemoved, this, finishFilterResultChange);
    connect(
        m_filterModel,
        &QAbstractItemModel::modelAboutToBeReset,
        this,
        beginFilterResultChange);
    connect(m_filterModel, &QAbstractItemModel::modelReset, this, finishFilterResultChange);
    connect(
        m_filterModel,
        &QAbstractItemModel::layoutAboutToBeChanged,
        this,
        beginFilterResultChange);
    connect(m_filterModel, &QAbstractItemModel::layoutChanged, this, finishFilterResultChange);
    connect(
        m_filterModel,
        &QAbstractItemModel::dataChanged,
        this,
        [this] { refreshFilterResults(); });
    m_dictionary->setModel(m_filterModel);
    m_dictionary->header()->setStretchLastSection(false);
    m_dictionary->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_dictionary->header()->setSectionResizeMode(CoeObjectModel::Name, QHeaderView::Stretch);

    m_filterEmptyState->setObjectName("EtherCATCoeFilterEmptyState");
    m_filterEmptyState->setAccessibleName(Tr::tr("No matching CoE objects"));
    m_filterEmptyState->setAccessibleDescription(
        Tr::tr("No local Mock or offline CoE objects match the current filters."));
    auto filterEmptyMessage = new QLabel(
        Tr::tr("No CoE objects match the current filters."), m_filterEmptyState);
    filterEmptyMessage->setObjectName("EtherCATCoeFilterEmptyMessage");
    filterEmptyMessage->setAccessibleName(Tr::tr("No matching CoE objects"));
    filterEmptyMessage->setAlignment(Qt::AlignCenter);
    filterEmptyMessage->setWordWrap(true);
    m_clearFilters->setObjectName("EtherCATCoeClearFilters");
    m_clearFilters->setAccessibleDescription(
        Tr::tr("Clear text and advanced CoE filters and return to the object dictionary."));
    auto filterEmptyLayout = new QVBoxLayout(m_filterEmptyState);
    filterEmptyLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    filterEmptyLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    filterEmptyLayout->addStretch();
    filterEmptyLayout->addWidget(filterEmptyMessage);
    filterEmptyLayout->addWidget(m_clearFilters, 0, Qt::AlignHCenter);
    filterEmptyLayout->addStretch();
    m_dictionaryStack->addWidget(m_dictionary);
    m_dictionaryStack->addWidget(m_filterEmptyState);

    auto controls = new QGridLayout;
    controls->setContentsMargins(QMargins());
    controls->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    controls->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    controls->addWidget(m_updateList, 0, 0);
    controls->addWidget(m_autoUpdate, 0, 1);
    controls->addWidget(m_singleUpdate, 0, 2);
    controls->addWidget(m_showOffline, 0, 3);
    controls->addWidget(m_advanced, 1, 0);
    controls->addWidget(m_filter, 1, 1, 1, 3);
    controls->addWidget(m_addToStartup, 2, 0);
    controls->addWidget(m_dataSource, 2, 1);
    controls->addWidget(new QLabel(Tr::tr("Module OD (AoE Port):"), this), 2, 2);
    controls->addWidget(m_moduleOd, 2, 3);
    controls->setColumnStretch(1, 1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_banner);
    layout->addLayout(controls);
    layout->addWidget(m_feedback);
    layout->addWidget(m_dictionaryStack, 1);

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        QScopedValueRollback resultChange(
            m_filterResultChangeDepth, m_filterResultChangeDepth + 1);
        m_filterModel->setFilterFixedString(text);
        refreshFilterResults();
    });
    connect(m_clearFilters, &QPushButton::clicked, this, &CoeOnlinePage::clearFilters);
    connect(m_updateList, &QPushButton::clicked, this, &CoeOnlinePage::updateList);
    connect(m_advanced, &QPushButton::clicked, this, &CoeOnlinePage::showAdvancedSettings);
    connect(m_addToStartup, &QPushButton::clicked, this, &CoeOnlinePage::addSelectedToStartup);
    connect(m_showOffline, &QCheckBox::toggled, this, [this](bool checked) {
        QScopedValueRollback resultChange(
            m_filterResultChangeDepth, m_filterResultChangeDepth + 1);
        clearInlineEditFeedback();
        discardInlineEditor();
        m_model->setShowOffline(checked);
        m_dataSource->setText(
            checked ? Tr::tr("Offline Data")
                    : Tr::tr("Mock Data - sample %1").arg(m_mockGeneration));
        m_dictionary->expandAll();
        refreshFilterResults();
    });
    connect(
        m_dictionary->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        [this](const QModelIndex &current, const QModelIndex &previous) {
            if (current.siblingAtColumn(0).data(AddressRole)
                != previous.siblingAtColumn(0).data(AddressRole)) {
                clearInlineEditFeedback();
            }
            if (current.isValid() && m_filterResultChangeDepth == 0)
                m_selectedObjectAddress = current.siblingAtColumn(0).data(AddressRole).toUInt();
            updateButtonState();
    });
}

CoeOnlinePage::~CoeOnlinePage()
{
    if (m_inlineEditor) {
        QPointer<QWidget> editor = m_inlineEditor;
        discardInlineEditor();
        delete editor.data();
    }
}

void CoeOnlinePage::trackInlineEditor(QWidget *editor, const QModelIndex &index)
{
    if (!editor || !index.isValid() || index.column() != CoeObjectModel::Value)
        return;
    m_inlineEditor = editor;
    m_inlineEditorAddress = index.data(AddressRole).toUInt();
    const quint64 generation = ++m_inlineEditorGeneration;
    connect(editor, &QObject::destroyed, this, [this, generation] {
        if (generation != m_inlineEditorGeneration)
            return;
        m_inlineEditor = nullptr;
        m_inlineEditorAddress.reset();
    });
}

void CoeOnlinePage::handleInlineEditorSubmitted(
    const QModelIndex &proxyIndex, const QString &text, bool accepted)
{
    if (accepted) {
        clearInlineEditFeedback();
        return;
    }
    const QVariant objectAddressData = proxyIndex.data(AddressRole);
    const quint32 objectAddress = objectAddressData.toUInt();
    const QModelIndex sourceIndex = m_filterModel->mapToSource(proxyIndex);
    const CoeObjectModel::MockValueEditValidation validation
        = m_model->validateMockValueEdit(sourceIndex, text);
    QString reason;
    switch (validation.rejection) {
    case CoeObjectModel::MockValueEditRejection::Empty:
        reason = Tr::tr("Enter at least one complete hexadecimal byte.");
        break;
    case CoeObjectModel::MockValueEditRejection::IncompleteByte:
        reason = Tr::tr("Hexadecimal bytes require an even number of digits; the supplied value "
                        "contains %1.")
                     .arg(digitCountText(validation.hexadecimalDigitCount));
        break;
    case CoeObjectModel::MockValueEditRejection::InvalidHex:
        reason = Tr::tr("Use only hexadecimal digits 0-9 or A-F. Spaces, colons, underscores, "
                        "and an optional 0x prefix are allowed.");
        break;
    case CoeObjectModel::MockValueEditRejection::WidthMismatch:
        reason = Tr::tr("This object expects %1; the supplied value contains %2.")
                     .arg(
                         byteCountText(validation.expectedSize),
                         byteCountText(validation.actualSize));
        break;
    case CoeObjectModel::MockValueEditRejection::NotEditable:
        reason = Tr::tr("This value is no longer editable in the current Mock view.");
        break;
    case CoeObjectModel::MockValueEditRejection::None:
        reason = Tr::tr("The value could not be accepted in the current Mock view.");
        break;
    }
    const QString object = objectAddressData.isValid()
                               ? indexText(quint16(objectAddress >> 8), quint8(objectAddress))
                               : Tr::tr("the selected object");
    showFeedback(
        Tr::tr("Local Mock value for %1 was not changed. %2").arg(object, reason), true, true);
}

void CoeOnlinePage::showFeedback(
    const QString &message, bool error, bool inlineEditFeedback)
{
    m_inlineEditFeedbackActive = inlineEditFeedback;
    m_feedback->setType(error ? Utils::InfoLabel::Error : Utils::InfoLabel::Ok);
    m_feedback->setText(message);
    m_feedback->setAccessibleDescription(message);
    m_feedback->setAdditionalToolTip(message);
    m_feedback->setToolTip(message);
    m_feedback->show();
#if QT_CONFIG(accessibility)
    if (inlineEditFeedback) {
        QAccessibleAnnouncementEvent announcement(m_feedback, message);
        announcement.setPoliteness(QAccessible::AnnouncementPoliteness::Polite);
        QAccessible::updateAccessibility(&announcement);
    }
#endif
}

void CoeOnlinePage::clearFeedback()
{
    m_inlineEditFeedbackActive = false;
    m_feedback->clear();
    m_feedback->setAccessibleDescription({});
    m_feedback->setAdditionalToolTip({});
    m_feedback->setToolTip({});
    m_feedback->hide();
}

void CoeOnlinePage::clearInlineEditFeedback()
{
    if (m_inlineEditFeedbackActive)
        clearFeedback();
}

void CoeOnlinePage::discardInlineEditor()
{
    if (m_inlineEditor)
        static_cast<CoeTreeView *>(m_dictionary)->discardInlineEditor(m_inlineEditor);
}

void CoeOnlinePage::setContext(const Core::PropertyPageContext &context)
{
    const bool sameStableContext
        = context.nodeKind != Core::WorkbenchNodeKind::None
          && m_context.projectId == context.projectId && m_context.nodeId == context.nodeId
          && m_context.nodeKind == context.nodeKind;
    const std::optional<quint32> selectedObjectAddress
        = sameStableContext ? m_selectedObjectAddress : std::nullopt;
    ++m_contextGeneration;
    m_context = context;
    if (!sameStableContext) {
        m_mockGeneration = 0;
        m_selectedObjectAddress.reset();
        {
            QScopedValueRollback resultChange(
                m_filterResultChangeDepth, m_filterResultChangeDepth + 1);
            m_filterModel->resetAdvancedFilters();
            m_filter->clear();
            m_showOffline->setChecked(false);
        }
    }
    clearFeedback();
    {
        QScopedValueRollback resultChange(
            m_filterResultChangeDepth, m_filterResultChangeDepth + 1);
        rebuildObjects(sameStableContext);
    }
    if (selectedObjectAddress && indexForAddress(m_model, *selectedObjectAddress).isValid()) {
        m_selectedObjectAddress = selectedObjectAddress;
    } else {
        const QModelIndex current = m_dictionary->currentIndex().siblingAtColumn(0);
        if (current.isValid())
            m_selectedObjectAddress = current.data(AddressRole).toUInt();
        else
            m_selectedObjectAddress.reset();
    }
}

void CoeOnlinePage::rebuildObjects(bool preserveMockValues)
{
    const CoeObjectModel::MockValueOverrides mockValueOverrides
        = preserveMockValues ? m_model->mockValueOverrides()
                             : CoeObjectModel::MockValueOverrides();
    const std::optional<Data::OfflineSlaveConfiguration> slave
        = m_controller ? m_controller->treeModel()->offlineSlave(m_context.nodeId) : std::nullopt;
    std::optional<Data::DeviceDescription> device;
    if (m_controller && m_controller->deviceRepository()) {
        if (m_context.nodeKind == Core::WorkbenchNodeKind::Device) {
            device = m_controller->deviceRepository()->device(m_context.nodeId);
        } else if (slave && !slave->deviceDescriptionId.isNull()) {
            device = m_controller->deviceRepository()->device(slave->deviceDescriptionId);
        }
    }
    const bool mockValueEditingEnabled
        = m_context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && slave.has_value()
          && !m_context.projectId.isNull() && m_controller && m_controller->projectService()
          && m_controller->projectService()->project(m_context.projectId).has_value();
    const QList<CoeObjectDefinition> definitions = objectDefinitions(slave, device);
    const bool preserveInlineEditor
        = preserveMockValues && m_inlineEditor && m_inlineEditorAddress
          && m_model->canSynchronizeDefinitions(
              definitions,
              mockValueEditingEnabled,
              m_mockGeneration,
              mockValueOverrides,
              *m_inlineEditorAddress);
    const std::optional<quint32> inlineEditorAddress
        = preserveInlineEditor ? m_inlineEditorAddress : std::nullopt;
    if (!preserveInlineEditor)
        discardInlineEditor();
    m_model->setDefinitions(
        definitions,
        mockValueEditingEnabled,
        m_mockGeneration,
        mockValueOverrides,
        inlineEditorAddress);
    m_model->setShowOffline(m_showOffline->isChecked());
    m_dataSource->setText(
        m_showOffline->isChecked() ? Tr::tr("Offline Data")
                                   : Tr::tr("Mock Data - sample %1").arg(m_mockGeneration));
    const bool coeDeclared = device && device->coe.supported;
    m_banner->setText(
        coeDeclared
            ? Tr::tr(
                  "MOCK DATA: this CoE object dictionary is generated locally from the offline "
                  "project and ESI description. No controller connection or SDO transfer occurs.")
            : Tr::tr("MOCK DATA: no CoE object dictionary is declared by the matching ESI. Only "
                     "locally derived identity and configuration objects are shown; no controller "
                     "connection occurs."));
    m_dictionary->expandAll();
    ensureDictionarySelection();
    updateFilterState();
    updateButtonState();
}

void CoeOnlinePage::updateList()
{
    clearInlineEditFeedback();
    discardInlineEditor();
    if (!m_showOffline->isChecked()) {
        ++m_mockGeneration;
        m_model->refreshMockValues(m_mockGeneration);
        m_dataSource->setText(Tr::tr("Mock Data - sample %1").arg(m_mockGeneration));
    } else {
        rebuildObjects();
    }
    m_dictionary->expandAll();
    ensureDictionarySelection();
    updateFilterState();
    updateButtonState();
}

void CoeOnlinePage::updateButtonState()
{
    const QModelIndex proxyIndex = m_dictionary->currentIndex();
    const QModelIndex sourceIndex = proxyIndex.isValid()
                                        ? m_filterModel->mapToSource(proxyIndex.siblingAtColumn(0))
                                        : QModelIndex();
    const bool writable = sourceIndex.data(WritableRole).toBool();
    const bool configured = m_context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
                            && !m_context.projectId.isNull() && m_controller
                            && m_controller->projectService()
                            && m_controller->projectService()->project(m_context.projectId);
    m_addToStartup->setEnabled(
        configured && writable && !m_showOffline->isChecked()
        && !sourceIndex.data(RawValueRole).toByteArray().isEmpty());
}

void CoeOnlinePage::refreshFilterResults()
{
    ensureDictionarySelection();
    updateFilterState();
    updateButtonState();
}

void CoeOnlinePage::updateFilterState()
{
    const bool filtersActive
        = !m_filter->text().isEmpty() || m_filterModel->hasAdvancedFilters();
    const bool noMatches = filtersActive && m_filterModel->rowCount() == 0;
    m_dictionaryStack->setCurrentWidget(noMatches ? m_filterEmptyState : m_dictionary);
}

void CoeOnlinePage::ensureDictionarySelection()
{
    if (m_filterModel->rowCount() == 0) {
        m_dictionary->setCurrentIndex({});
        return;
    }
    QModelIndex target;
    if (m_selectedObjectAddress)
        target = indexForAddress(m_filterModel, *m_selectedObjectAddress);
    if (target.isValid()) {
        const QModelIndex current = m_dictionary->currentIndex().siblingAtColumn(0);
        if (!current.isValid() || current.data(AddressRole).toUInt() != *m_selectedObjectAddress)
            m_dictionary->setCurrentIndex(target);
        return;
    }
    if (!m_dictionary->currentIndex().isValid())
        m_dictionary->setCurrentIndex(m_filterModel->index(0, 0));
}

void CoeOnlinePage::clearFilters()
{
    QScopedValueRollback resultChange(
        m_filterResultChangeDepth, m_filterResultChangeDepth + 1);
    m_filterModel->resetAdvancedFilters();
    m_filter->clear();
    refreshFilterResults();
    m_dictionary->setFocus(Qt::ShortcutFocusReason);
}

void CoeOnlinePage::showAdvancedSettings()
{
    const quint64 contextGeneration = m_contextGeneration;
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("EtherCATCoeAdvancedDialog");
    dialog->setWindowTitle(Tr::tr("Advanced CoE Dictionary Settings"));
    auto sourceGroup = new QGroupBox(Tr::tr("Dictionary source"), dialog);
    auto mock = new QRadioButton(Tr::tr("Mock object dictionary"), sourceGroup);
    auto offline = new QRadioButton(Tr::tr("Offline from device description"), sourceGroup);
    mock->setObjectName("EtherCATCoeAdvancedMockSource");
    offline->setObjectName("EtherCATCoeAdvancedOfflineSource");
    mock->setChecked(!m_showOffline->isChecked());
    offline->setChecked(m_showOffline->isChecked());
    auto sourceLayout = new QVBoxLayout(sourceGroup);
    sourceLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    sourceLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    sourceLayout->addWidget(mock);
    sourceLayout->addWidget(offline);

    auto range = new QComboBox(dialog);
    range->setObjectName("EtherCATCoeAdvancedRange");
    range->addItem(Tr::tr("All Objects"), int(DictionaryRange::All));
    range->addItem(
        Tr::tr("Communication Objects (0x1000-0x1FFF)"), int(DictionaryRange::Communication));
    range->addItem(
        Tr::tr("Vendor-specific Objects (0x2000-0x5FFF)"), int(DictionaryRange::VendorSpecific));
    range->addItem(
        Tr::tr("Profile-specific Objects (0x6000-0x9FFF)"), int(DictionaryRange::ProfileSpecific));
    range->setCurrentIndex(range->findData(int(m_filterModel->dictionaryRange())));
    auto hideStandard = new QCheckBox(Tr::tr("Hide Standard Objects"), dialog);
    auto hidePdo = new QCheckBox(Tr::tr("Hide PDO Objects"), dialog);
    hideStandard->setObjectName("EtherCATCoeAdvancedHideStandard");
    hidePdo->setObjectName("EtherCATCoeAdvancedHidePdo");
    hideStandard->setChecked(m_filterModel->hideStandard());
    hidePdo->setChecked(m_filterModel->hidePdo());

    auto options = new QFormLayout;
    options->setContentsMargins(QMargins());
    options->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    options->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    options->addRow(Tr::tr("Object range:"), range);
    options->addRow(QString(), hideStandard);
    options->addRow(QString(), hidePdo);
    auto buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, dialog);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(sourceGroup);
    layout->addLayout(options);
    layout->addWidget(buttons);
    connect(
        dialog,
        &QDialog::finished,
        this,
        [this, contextGeneration, range, hideStandard, hidePdo, offline](int result) {
            if (result != QDialog::Accepted || contextGeneration != m_contextGeneration)
                return;
            QScopedValueRollback resultChange(
                m_filterResultChangeDepth, m_filterResultChangeDepth + 1);
            m_filterModel->setDictionaryRange(DictionaryRange(range->currentData().toInt()));
            m_filterModel->setHideStandard(hideStandard->isChecked());
            m_filterModel->setHidePdo(hidePdo->isChecked());
            m_showOffline->setChecked(offline->isChecked());
            m_dictionary->expandAll();
            refreshFilterResults();
        });
    dialog->open();
}

void CoeOnlinePage::addSelectedToStartup()
{
    const QModelIndex proxyIndex = m_dictionary->currentIndex();
    const QModelIndex sourceIndex = proxyIndex.isValid()
                                        ? m_filterModel->mapToSource(proxyIndex.siblingAtColumn(0))
                                        : QModelIndex();
    if (!sourceIndex.isValid() || !sourceIndex.data(WritableRole).toBool()
        || m_showOffline->isChecked() || !m_controller || !m_controller->projectService()) {
        return;
    }
    const quint32 objectAddress = sourceIndex.data(AddressRole).toUInt();
    const QByteArray rawValue = sourceIndex.data(RawValueRole).toByteArray();
    if (rawValue.isEmpty())
        return;
    const quint64 contextGeneration = m_contextGeneration;
    const Data::NodeId projectId = m_context.projectId;
    const Data::NodeId slaveId = m_context.nodeId;
    const QString name = sourceIndex.siblingAtColumn(CoeObjectModel::Name).data().toString();
    const Data::EtherCATDataType dataType
        = sourceIndex.data(DataTypeRole).value<Data::EtherCATDataType>();
    const QString rawDataType = sourceIndex.data(RawDataTypeRole).toString();
    auto messageBox = new QMessageBox(
        QMessageBox::Question,
        Tr::tr("Add to Startup"),
        Tr::tr("Add the selected local Mock value for %1 (%2) as a new PS Startup request? "
               "Existing requests are not overwritten.")
            .arg(name, indexText(quint16(objectAddress >> 8), quint8(objectAddress))),
        QMessageBox::Yes | QMessageBox::No,
        this);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setDefaultButton(QMessageBox::No);
    connect(
        messageBox,
        &QDialog::finished,
        this,
        [this,
         contextGeneration,
         projectId,
         slaveId,
         objectAddress,
         rawValue,
         name,
         dataType,
         rawDataType](int result) {
            if (result != QMessageBox::Yes || contextGeneration != m_contextGeneration
                || !m_controller || !m_controller->projectService()) {
                return;
            }
            const std::optional<Data::ProjectSnapshot> project
                = m_controller->projectService()->project(projectId);
            if (!project)
                return;
            const auto slave = std::find_if(
                project->slaves.cbegin(),
                project->slaves.cend(),
                [&slaveId](const auto &candidate) { return candidate.id == slaveId; });
            if (slave == project->slaves.cend())
                return;
            Data::StartupConfiguration configuration = slave->startup;
            int nextOrder = 0;
            for (const Data::StartupParameterConfiguration &parameter : configuration.parameters)
                nextOrder = qMax(nextOrder, parameter.order + 1);
            configuration.parameters.append(
                {Data::NodeId::create(),
                 true,
                 nextOrder,
                 "PS",
                 quint16(objectAddress >> 8),
                 quint8(objectAddress),
                 dataType,
                 rawDataType,
                 rawValue,
                 Tr::tr("Copied from CoE Online Mock: %1").arg(name)});
            const Utils::Result<> updateResult
                = m_controller->projectService()->setStartupConfiguration(
                    projectId, slaveId, configuration);
            if (!updateResult) {
                showFeedback(
                    Tr::tr("Startup request was not added: %1").arg(updateResult.error()),
                    true,
                    false);
                return;
            }
            showFeedback(
                Tr::tr(
                    "Mock value added as Startup order %1. The project Undo command can remove it.")
                    .arg(nextOrder),
                false,
                false);
        });
    messageBox->open();
}

} // namespace EtherCAT::Workbench::Internal
