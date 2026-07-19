// Copyright (C) 2026 Kvell

#include "processdatapage.h"

#include "esiconfigurationfactory.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/minisplitter.h>

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace EtherCAT::Workbench::Internal {

enum TableRole {
    StableIdRole = Qt::UserRole + 1,
    DataTypeRole,
};

static constexpr char baseDescriptionProperty[]
    = "EtherCAT.ProcessData.BaseAccessibleDescription";

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
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

static QString syncManagerDirectionName(Data::SyncManagerDirection direction)
{
    switch (direction) {
    case Data::SyncManagerDirection::MasterToSlave:
        return Tr::tr("Outputs");
    case Data::SyncManagerDirection::SlaveToMaster:
        return Tr::tr("Inputs");
    case Data::SyncManagerDirection::Unknown:
        return Tr::tr("Unknown");
    }
    return Tr::tr("Unknown");
}

static QString pdoDirectionName(Data::PdoDirection direction)
{
    return direction == Data::PdoDirection::Rx ? Tr::tr("Outputs (RxPDO)")
                                               : Tr::tr("Inputs (TxPDO)");
}

static QString byteBitSize(qint64 bitSize)
{
    return QString("%1.%2").arg(bitSize / 8).arg(bitSize % 8);
}

static qint64 pdoBitSize(const Data::PdoConfiguration &pdo)
{
    qint64 result = 0;
    for (const Data::PdoEntryConfiguration &entry : pdo.entries)
        result += qMax(0, entry.bitLength);
    return result;
}

static QString pdoFlags(const Data::PdoConfiguration &pdo)
{
    QStringList flags;
    if (pdo.fixed)
        flags.append("F");
    if (pdo.mandatory)
        flags.append("M");
    if (!pdo.mappingSupported)
        flags.append(Tr::tr("Unsupported"));
    return flags.join(' ');
}

static QString tableCellDescription(
    const QAbstractItemModel *model, const QModelIndex &index, const QString &help = {})
{
    const QString header = model->headerData(index.column(), Qt::Horizontal).toString();
    const QString text = model->data(index, Qt::AccessibleTextRole).toString();
    int nameColumn = -1;
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() == Tr::tr("Name")) {
            nameColumn = column;
            break;
        }
    }
    const QString rowName
        = nameColumn >= 0
              ? model->data(model->index(index.row(), nameColumn), Qt::AccessibleTextRole).toString()
              : QString();

    QString description;
    if (!rowName.isEmpty() && index.column() != nameColumn)
        description = text.isEmpty() ? Tr::tr("%1, %2").arg(rowName, header)
                                     : Tr::tr("%1, %2: %3").arg(rowName, header, text);
    else
        description = text.isEmpty() ? header : Tr::tr("%1: %2").arg(header, text);
    if (!help.isEmpty())
        description += '\n' + help;
    return description;
}

static bool isEmpty(const Data::ProcessDataConfiguration &configuration)
{
    return configuration.syncManagers.isEmpty() && configuration.pdos.isEmpty();
}

static std::optional<Data::PdoDirection> directionForNodeKind(Core::WorkbenchNodeKind kind)
{
    using Kind = Core::WorkbenchNodeKind;
    switch (kind) {
    case Kind::ProcessInputs:
    case Kind::TxPdoGroup:
        return Data::PdoDirection::Tx;
    case Kind::ProcessOutputs:
    case Kind::RxPdoGroup:
        return Data::PdoDirection::Rx;
    default:
        return std::nullopt;
    }
}

static bool parseUnsignedValue(const QVariant &value, quint64 maximum, quint64 *result)
{
    QString text = value.toString().trimmed();
    int base = 10;
    if (text.startsWith("0x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    }
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, base);
    if (!ok || parsed > maximum)
        return false;
    *result = parsed;
    return true;
}

class SyncManagerTableModel final : public QAbstractTableModel
{
public:
    enum Column { Index, Type, Name, Size, Limit, Pdos, Enabled, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : m_configuration.syncManagers.size();
    }

    int columnCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid() || index.row() >= m_configuration.syncManagers.size())
            return {};
        const Data::SyncManagerConfiguration &syncManager = m_configuration.syncManagers.at(
            index.row());
        if (role == StableIdRole)
            return QVariant::fromValue(syncManager.id);
        if (role == Qt::AccessibleTextRole)
            return data(index, Qt::DisplayRole).toString();
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            return tableCellDescription(
                this,
                index,
                Tr::tr(
                    "Selecting this Sync Manager filters PDO Assignment, PDO List, and "
                    "PDO Content below."));
        }
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case Index:
            return QString("SM%1").arg(syncManager.index);
        case Type:
            return syncManagerDirectionName(syncManager.direction);
        case Name:
            return syncManager.name;
        case Size: {
            qint64 bits = 0;
            for (const Data::PdoConfiguration &pdo : m_configuration.pdos) {
                if (pdo.syncManager == syncManager.index && pdo.selected)
                    bits += pdoBitSize(pdo);
            }
            return byteBitSize(bits);
        }
        case Limit:
            return syncManager.sizeLimitBytes > 0 ? Tr::tr("%1 B").arg(syncManager.sizeLimitBytes)
                                                  : Tr::tr("None");
        case Pdos: {
            int selected = 0;
            int available = 0;
            for (const Data::PdoConfiguration &pdo : m_configuration.pdos) {
                if (pdo.syncManager != syncManager.index)
                    continue;
                ++available;
                if (pdo.selected)
                    ++selected;
            }
            return QString("%1 / %2").arg(selected).arg(available);
        }
        case Enabled:
            return syncManager.enabled ? Tr::tr("Yes") : Tr::tr("No");
        default:
            return {};
        }
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const QStringList headers
            = {Tr::tr("SM"),
               Tr::tr("Type"),
               Tr::tr("Name"),
               Tr::tr("Size (byte.bit)"),
               Tr::tr("Limit"),
               Tr::tr("PDOs"),
               Tr::tr("Enabled")};
        return headers.value(section);
    }

    void setConfiguration(const Data::ProcessDataConfiguration &configuration)
    {
        beginResetModel();
        m_configuration = configuration;
        endResetModel();
    }

private:
    Data::ProcessDataConfiguration m_configuration;
};

class PdoAssignmentTableModel final : public QAbstractTableModel
{
public:
    enum Column { Assigned, Index, Size, Name, Flags, ColumnCount };

    explicit PdoAssignmentTableModel(ProcessDataPage *page)
        : QAbstractTableModel(page)
        , m_page(page)
    {}

    int rowCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : m_pdos.size();
    }

    int columnCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid() || index.row() >= m_pdos.size())
            return {};
        const Data::PdoConfiguration &pdo = m_pdos.at(index.row());
        if (role == StableIdRole)
            return QVariant::fromValue(pdo.id);
        if (role == Qt::CheckStateRole && index.column() == Assigned)
            return pdo.selected ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::AccessibleTextRole) {
            if (index.column() == Assigned)
                return pdo.selected ? Tr::tr("Assigned") : Tr::tr("Not assigned");
            return data(index, Qt::DisplayRole).toString();
        }
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            QString help;
            if (pdo.mandatory) {
                help = Tr::tr("Mandatory PDOs cannot be removed from the assignment.");
            } else if (!pdo.mappingSupported) {
                help = Tr::tr(
                    "This ESI mapping is preserved for inspection but cannot be selected.");
            } else if (!m_editable) {
                help = Tr::tr(
                    "This PDO assignment is read-only in the current Process Data selection.");
            } else {
                help = Tr::tr("Select whether this PDO participates in cyclic process data.");
            }
            return tableCellDescription(this, index, help);
        }
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case Assigned:
            return {};
        case Index:
            return hexValue(pdo.index, 4);
        case Size:
            return byteBitSize(pdoBitSize(pdo));
        case Name:
            return pdo.name;
        case Flags:
            return pdoFlags(pdo);
        default:
            return {};
        }
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const QStringList headers
            = {Tr::tr("Assigned"),
               Tr::tr("Index"),
               Tr::tr("Size (byte.bit)"),
               Tr::tr("Name"),
               Tr::tr("Flags")};
        return headers.value(section);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const final
    {
        if (!index.isValid() || index.row() >= m_pdos.size())
            return Qt::NoItemFlags;
        const Data::PdoConfiguration &pdo = m_pdos.at(index.row());
        Qt::ItemFlags result = Qt::ItemIsSelectable;
        if (pdo.mappingSupported)
            result |= Qt::ItemIsEnabled;
        if (index.column() == Assigned && m_editable && pdo.mappingSupported && !pdo.mandatory)
            result |= Qt::ItemIsUserCheckable;
        return result;
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) final
    {
        if (!index.isValid() || index.row() >= m_pdos.size() || index.column() != Assigned
            || role != Qt::CheckStateRole || !m_editable) {
            return false;
        }
        const Data::PdoConfiguration &current = m_pdos.at(index.row());
        if (current.mandatory || !current.mappingSupported)
            return false;
        const bool selected = value.toInt() == Qt::Checked;
        if (current.selected == selected)
            return true;

        Data::ProcessDataConfiguration candidate = m_configuration;
        const auto pdo = std::find_if(
            candidate.pdos.begin(), candidate.pdos.end(), [&current](const auto &entry) {
                return entry.id == current.id;
            });
        if (pdo == candidate.pdos.end())
            return false;
        pdo->selected = selected;
        return m_page && m_page->submitConfiguration(candidate);
    }

    void setConfiguration(
        const Data::ProcessDataConfiguration &configuration, int syncManager, bool editable)
    {
        beginResetModel();
        m_configuration = configuration;
        m_pdos.clear();
        for (const Data::PdoConfiguration &pdo : configuration.pdos) {
            if (pdo.syncManager == syncManager)
                m_pdos.append(pdo);
        }
        m_editable = editable;
        endResetModel();
    }

private:
    ProcessDataPage *m_page = nullptr;
    Data::ProcessDataConfiguration m_configuration;
    QList<Data::PdoConfiguration> m_pdos;
    bool m_editable = false;
};

class PdoListTableModel final : public QAbstractTableModel
{
public:
    enum Column { Index, Size, Name, Flags, Default, SyncManager, Predefined, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : m_pdos.size();
    }

    int columnCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid() || index.row() >= m_pdos.size())
            return {};
        const Data::PdoConfiguration &pdo = m_pdos.at(index.row());
        if (role == StableIdRole)
            return QVariant::fromValue(pdo.id);
        if (role == Qt::AccessibleTextRole)
            return data(index, Qt::DisplayRole).toString();
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            const QString help = pdo.selected ? Tr::tr("Assigned to cyclic process data")
                                              : Tr::tr("Available but not assigned");
            return tableCellDescription(this, index, help);
        }
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case Index:
            return hexValue(pdo.index, 4);
        case Size:
            return byteBitSize(pdoBitSize(pdo));
        case Name:
            return pdo.name;
        case Flags:
            return pdoFlags(pdo);
        case Default:
            return pdo.defaultSelected ? Tr::tr("Yes") : Tr::tr("No");
        case SyncManager:
            return pdo.selected ? QString("SM%1").arg(pdo.syncManager) : QString();
        case Predefined:
            return pdo.predefinedGroup;
        default:
            return {};
        }
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const QStringList headers
            = {Tr::tr("Index"),
               Tr::tr("Size (byte.bit)"),
               Tr::tr("Name"),
               Tr::tr("Flags"),
               Tr::tr("Default"),
               Tr::tr("SM"),
               Tr::tr("Predefined Group")};
        return headers.value(section);
    }

    void setConfiguration(const Data::ProcessDataConfiguration &configuration, int syncManager)
    {
        beginResetModel();
        m_pdos.clear();
        for (const Data::PdoConfiguration &pdo : configuration.pdos) {
            if (pdo.syncManager == syncManager)
                m_pdos.append(pdo);
        }
        endResetModel();
    }

private:
    QList<Data::PdoConfiguration> m_pdos;
};

class PdoContentTableModel final : public QAbstractTableModel
{
public:
    enum Column { Index, Subindex, Bits, BitOffset, Name, Type, Direction, ColumnCount };

    explicit PdoContentTableModel(ProcessDataPage *page)
        : QAbstractTableModel(page)
        , m_page(page)
    {}

    int rowCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() || !m_pdo ? 0 : m_pdo->entries.size();
    }

    int columnCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid() || !m_pdo || index.row() >= m_pdo->entries.size())
            return {};
        const Data::PdoEntryConfiguration &entry = m_pdo->entries.at(index.row());
        if (role == StableIdRole)
            return QVariant::fromValue(entry.id);
        if (role == DataTypeRole)
            return int(entry.dataType);
        if (role == Qt::AccessibleTextRole)
            return data(index, Qt::DisplayRole).toString();
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            QString help;
            if (index.column() == BitOffset) {
                const std::optional<Data::ProcessImageEntry> imageEntry = processImageEntry(
                    entry.id);
                if (!imageEntry) {
                    help = Tr::tr("Automatic offset. The entry is not in the active process image.");
                } else if (entry.requestedBitOffset < 0) {
                    help = Tr::tr("Automatic offset: %1 byte(s), bit %2.")
                               .arg(imageEntry->byteOffset)
                               .arg(imageEntry->bitOffsetInByte);
                } else {
                    help = Tr::tr("Requested offset: %1 bit(s).").arg(entry.requestedBitOffset);
                }
            }
            return tableCellDescription(this, index, help);
        }
        if (role != Qt::DisplayRole && role != Qt::EditRole)
            return {};
        switch (index.column()) {
        case Index:
            return role == Qt::EditRole ? QVariant(entry.index)
                                        : QVariant(hexValue(entry.index, 4));
        case Subindex:
            return role == Qt::EditRole ? QVariant(entry.subIndex)
                                        : QVariant(hexValue(entry.subIndex, 2));
        case Bits:
            return entry.bitLength;
        case BitOffset:
            if (role == Qt::EditRole)
                return entry.requestedBitOffset;
            if (entry.requestedBitOffset >= 0)
                return entry.requestedBitOffset;
            if (const std::optional<Data::ProcessImageEntry> imageEntry = processImageEntry(
                    entry.id)) {
                return Tr::tr("Auto (%1)").arg(imageEntry->bitOffset);
            }
            return Tr::tr("Auto");
        case Name:
            return entry.name;
        case Type:
            return dataTypeName(entry.dataType, entry.rawDataType);
        case Direction:
            return pdoDirectionName(m_pdo->direction);
        default:
            return {};
        }
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const QStringList headers
            = {Tr::tr("Index"),
               Tr::tr("Subindex"),
               Tr::tr("Bits"),
               Tr::tr("Bit Offset"),
               Tr::tr("Name"),
               Tr::tr("Type"),
               Tr::tr("Direction")};
        return headers.value(section);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const final
    {
        if (!index.isValid() || !m_pdo || index.row() >= m_pdo->entries.size())
            return Qt::NoItemFlags;
        Qt::ItemFlags result = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        const Data::PdoEntryConfiguration &entry = m_pdo->entries.at(index.row());
        const bool editableColumn = index.column() == Index || index.column() == Subindex
                                    || index.column() == Bits || index.column() == BitOffset
                                    || index.column() == Name || index.column() == Type;
        if (m_editable && editableColumn && !m_pdo->fixed && m_pdo->mappingSupported
            && entry.mappingSupported) {
            result |= Qt::ItemIsEditable;
        }
        return result;
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) final
    {
        if (!index.isValid() || !m_pdo || index.row() >= m_pdo->entries.size() || !m_editable)
            return false;
        if (role != Qt::EditRole && !(role == DataTypeRole && index.column() == Type))
            return false;

        Data::ProcessDataConfiguration candidate = m_configuration;
        const auto pdo
            = std::find_if(candidate.pdos.begin(), candidate.pdos.end(), [this](const auto &entry) {
                  return entry.id == m_pdo->id;
              });
        if (pdo == candidate.pdos.end() || pdo->fixed || !pdo->mappingSupported)
            return false;
        Data::PdoEntryConfiguration entry = pdo->entries.at(index.row());
        if (!entry.mappingSupported)
            return false;

        quint64 unsignedValue = 0;
        switch (index.column()) {
        case Index:
            if (!parseUnsignedValue(value, std::numeric_limits<quint16>::max(), &unsignedValue)
                || unsignedValue == 0) {
                return false;
            }
            entry.index = quint16(unsignedValue);
            break;
        case Subindex:
            if (!parseUnsignedValue(value, std::numeric_limits<quint8>::max(), &unsignedValue))
                return false;
            entry.subIndex = quint8(unsignedValue);
            break;
        case Bits:
            if (!parseUnsignedValue(value, std::numeric_limits<int>::max(), &unsignedValue)
                || unsignedValue == 0) {
                return false;
            }
            entry.bitLength = int(unsignedValue);
            break;
        case BitOffset: {
            const QString text = value.toString().trimmed();
            if (text.compare(Tr::tr("Auto"), Qt::CaseInsensitive) == 0
                || text.compare("auto", Qt::CaseInsensitive) == 0) {
                entry.requestedBitOffset = -1;
                break;
            }
            bool ok = false;
            const qint64 offset = text.toLongLong(&ok);
            if (!ok || offset < -1)
                return false;
            entry.requestedBitOffset = offset;
            break;
        }
        case Name:
            if (value.toString().trimmed().isEmpty())
                return false;
            entry.name = value.toString().trimmed();
            break;
        case Type: {
            bool ok = false;
            const int typeValue = value.toInt(&ok);
            if (!ok || typeValue < int(Data::EtherCATDataType::Unknown)
                || typeValue > int(Data::EtherCATDataType::OctetString)) {
                return false;
            }
            entry.dataType = Data::EtherCATDataType(typeValue);
            if (entry.dataType == Data::EtherCATDataType::Unknown)
                entry.rawDataType.clear();
            else
                entry.rawDataType = dataTypeName(entry.dataType);
            if (const int bitLength = fixedDataTypeBitLength(entry.dataType); bitLength > 0)
                entry.bitLength = bitLength;
            break;
        }
        default:
            return false;
        }
        pdo->entries.replace(index.row(), entry);
        return m_page && m_page->submitConfiguration(candidate);
    }

    void setConfiguration(
        const Data::ProcessDataConfiguration &configuration,
        const Data::NodeId &pdoId,
        bool editable)
    {
        beginResetModel();
        m_configuration = configuration;
        m_pdo.reset();
        const auto pdo = std::find_if(
            configuration.pdos.cbegin(), configuration.pdos.cend(), [&pdoId](const auto &entry) {
                return entry.id == pdoId;
            });
        if (pdo != configuration.pdos.cend())
            m_pdo = *pdo;
        m_validation = Data::validateProcessDataConfiguration(configuration);
        m_editable = editable;
        endResetModel();
    }

private:
    std::optional<Data::ProcessImageEntry> processImageEntry(const Data::NodeId &entryId) const
    {
        const auto findIn = [&entryId](const Data::ProcessImageDirection &direction)
            -> std::optional<Data::ProcessImageEntry> {
            const auto found = std::find_if(
                direction.entries.cbegin(),
                direction.entries.cend(),
                [&entryId](const auto &entry) { return entry.entryId == entryId; });
            return found == direction.entries.cend()
                       ? std::nullopt
                       : std::optional<Data::ProcessImageEntry>(*found);
        };
        if (const auto output = findIn(m_validation.processImage.outputs))
            return output;
        return findIn(m_validation.processImage.inputs);
    }

    ProcessDataPage *m_page = nullptr;
    Data::ProcessDataConfiguration m_configuration;
    std::optional<Data::PdoConfiguration> m_pdo;
    Data::ConfigurationValidation m_validation;
    bool m_editable = false;
};

class ProcessImageTableModel final : public QAbstractTableModel
{
public:
    enum Column { Direction, Offset, Bits, Index, Subindex, Name, Type, Pdo, Sm, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : m_entries.size();
    }

    int columnCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid() || index.row() >= m_entries.size())
            return {};
        const Data::ProcessImageEntry &entry = m_entries.at(index.row());
        if (role == StableIdRole)
            return QVariant::fromValue(entry.entryId);
        if (role == Qt::AccessibleTextRole)
            return data(index, Qt::DisplayRole).toString();
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            return tableCellDescription(
                this,
                index,
                Tr::tr("Absolute process-image bit range [%1, %2).")
                    .arg(entry.bitOffset)
                    .arg(entry.bitOffset + entry.bitLength));
        }
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case Direction:
            return pdoDirectionName(entry.direction);
        case Offset:
            return QString("%1.%2").arg(entry.byteOffset).arg(entry.bitOffsetInByte);
        case Bits:
            return entry.bitLength;
        case Index:
            return hexValue(entry.index, 4);
        case Subindex:
            return hexValue(entry.subIndex, 2);
        case Name:
            return entry.name;
        case Type:
            return dataTypeName(entry.dataType);
        case Pdo:
            return hexValue(entry.pdoIndex, 4);
        case Sm:
            return QString("SM%1").arg(entry.syncManager);
        default:
            return {};
        }
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const QStringList headers
            = {Tr::tr("Direction"),
               Tr::tr("Offset (byte.bit)"),
               Tr::tr("Bits"),
               Tr::tr("Index"),
               Tr::tr("Subindex"),
               Tr::tr("Name"),
               Tr::tr("Type"),
               Tr::tr("PDO"),
               Tr::tr("SM")};
        return headers.value(section);
    }

    void setValidation(const Data::ConfigurationValidation &validation)
    {
        beginResetModel();
        m_entries = validation.processImage.outputs.entries;
        m_entries.append(validation.processImage.inputs.entries);
        endResetModel();
    }

private:
    QList<Data::ProcessImageEntry> m_entries;
};

class DataTypeDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const final
    {
        auto editor = new QComboBox(parent);
        const QList<Data::EtherCATDataType> types
            = {Data::EtherCATDataType::Unknown,
               Data::EtherCATDataType::Boolean,
               Data::EtherCATDataType::Integer8,
               Data::EtherCATDataType::UnsignedInteger8,
               Data::EtherCATDataType::Integer16,
               Data::EtherCATDataType::UnsignedInteger16,
               Data::EtherCATDataType::Integer32,
               Data::EtherCATDataType::UnsignedInteger32,
               Data::EtherCATDataType::Integer64,
               Data::EtherCATDataType::UnsignedInteger64,
               Data::EtherCATDataType::Real32,
               Data::EtherCATDataType::Real64,
               Data::EtherCATDataType::VisibleString,
               Data::EtherCATDataType::OctetString};
        for (Data::EtherCATDataType type : types)
            editor->addItem(dataTypeName(type), int(type));
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const final
    {
        auto comboBox = qobject_cast<QComboBox *>(editor);
        if (!comboBox)
            return;
        comboBox->setCurrentIndex(comboBox->findData(index.data(DataTypeRole)));
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const final
    {
        auto comboBox = qobject_cast<QComboBox *>(editor);
        if (comboBox)
            model->setData(index, comboBox->currentData(), DataTypeRole);
    }

    void updateEditorGeometry(
        QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const final
    {
        editor->setGeometry(option.rect);
    }
};

static void configureTable(
    QTableView *view,
    int stretchColumn,
    const QString &accessibleName,
    const QString &accessibleDescription)
{
    view->setAccessibleName(accessibleName);
    view->setAccessibleDescription(accessibleDescription);
    view->setProperty(baseDescriptionProperty, accessibleDescription);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    view->setWordWrap(false);
    view->verticalHeader()->hide();
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (stretchColumn >= 0)
        view->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
}

static QGroupBox *tableGroup(const QString &title, QTableView *view)
{
    auto group = new QGroupBox(title);
    auto layout = new QVBoxLayout(group);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(view);
    return group;
}

static int rowForId(const QAbstractItemModel *model, const Data::NodeId &id)
{
    if (!model || id.isNull())
        return -1;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data(StableIdRole).value<Data::NodeId>() == id)
            return row;
    }
    return -1;
}

ProcessDataPage::ProcessDataPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_validation(new Utils::InfoLabel(this))
    , m_restoreDefaults(new QPushButton(Tr::tr("Store ESI Defaults"), this))
    , m_syncManagers(new QTableView(this))
    , m_assignments(new QTableView(this))
    , m_pdoList(new QTableView(this))
    , m_pdoContent(new QTableView(this))
    , m_processImage(new QTableView(this))
    , m_syncManagerModel(new SyncManagerTableModel(this))
    , m_assignmentModel(new PdoAssignmentTableModel(this))
    , m_pdoListModel(new PdoListTableModel(this))
    , m_pdoContentModel(new PdoContentTableModel(this))
    , m_processImageModel(new ProcessImageTableModel(this))
{
    setObjectName("EtherCATWorkbenchProcessDataPage");
    m_summary->setObjectName("EtherCATProcessDataSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_validation->setObjectName("EtherCATProcessDataValidation");
    m_validation->setElideMode(Qt::ElideNone);
    m_validation->setWordWrap(true);
    m_restoreDefaults->setObjectName("EtherCATProcessDataRestoreDefaults");

    m_syncManagers->setObjectName("EtherCATProcessDataSyncManagers");
    m_assignments->setObjectName("EtherCATProcessDataAssignments");
    m_pdoList->setObjectName("EtherCATProcessDataPdoList");
    m_pdoContent->setObjectName("EtherCATProcessDataPdoContent");
    m_processImage->setObjectName("EtherCATProcessDataImage");
    m_syncManagers->setModel(m_syncManagerModel);
    m_assignments->setModel(m_assignmentModel);
    m_pdoList->setModel(m_pdoListModel);
    m_pdoContent->setModel(m_pdoContentModel);
    m_processImage->setModel(m_processImageModel);
    configureTable(
        m_syncManagers,
        SyncManagerTableModel::Name,
        Tr::tr("Process Data Sync Managers"),
        Tr::tr(
            "Sync Managers for the current Process Data selection. Selecting a row filters "
            "the PDO tables."));
    configureTable(
        m_assignments,
        PdoAssignmentTableModel::Name,
        Tr::tr("Process Data PDO Assignment"),
        Tr::tr(
            "PDOs available for the selected Sync Manager and whether each participates in "
            "cyclic process data."));
    configureTable(
        m_pdoList,
        PdoListTableModel::Name,
        Tr::tr("Process Data PDO List"),
        Tr::tr("PDO catalogue for the selected Sync Manager."));
    configureTable(
        m_pdoContent,
        PdoContentTableModel::Name,
        Tr::tr("Process Data PDO Content"),
        Tr::tr(
            "Entries in the selected PDO, including mapping addresses, sizes, names, and "
            "types."));
    configureTable(
        m_processImage,
        ProcessImageTableModel::Name,
        Tr::tr("Process Image Preview"),
        Tr::tr(
            "Validated absolute layout of active outputs and inputs in the offline process "
            "image."));
    m_pdoContent
        ->setItemDelegateForColumn(PdoContentTableModel::Type, new DataTypeDelegate(m_pdoContent));

    auto headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(QMargins());
    headerLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    headerLayout->addWidget(m_summary, 1);
    headerLayout->addWidget(m_restoreDefaults);

    auto left = new ::Core::MiniSplitter(Qt::Vertical, this);
    left->setObjectName("EtherCATProcessDataLeftSplitter");
    left->addWidget(tableGroup(Tr::tr("Sync Manager"), m_syncManagers));
    left->addWidget(tableGroup(Tr::tr("PDO Assignment"), m_assignments));
    left->setStretchFactor(0, 1);
    left->setStretchFactor(1, 1);

    auto right = new ::Core::MiniSplitter(Qt::Vertical, this);
    right->setObjectName("EtherCATProcessDataRightSplitter");
    right->addWidget(tableGroup(Tr::tr("PDO List"), m_pdoList));
    right->addWidget(tableGroup(Tr::tr("PDO Content"), m_pdoContent));
    right->setStretchFactor(0, 1);
    right->setStretchFactor(1, 1);

    auto configuration = new ::Core::MiniSplitter(Qt::Horizontal, this);
    configuration->setObjectName("EtherCATProcessDataConfigurationSplitter");
    configuration->addWidget(left);
    configuration->addWidget(right);
    configuration->setStretchFactor(0, 1);
    configuration->setStretchFactor(1, 2);

    auto mainSplitter = new ::Core::MiniSplitter(Qt::Vertical, this);
    mainSplitter->setObjectName("EtherCATProcessDataMainSplitter");
    mainSplitter->addWidget(configuration);
    mainSplitter->addWidget(tableGroup(Tr::tr("Process Image Preview"), m_processImage));
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addLayout(headerLayout);
    layout->addWidget(m_validation);
    layout->addWidget(mainSplitter, 1);

    connect(
        m_syncManagers->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        [this](const QModelIndex &current) {
            if (m_rebuilding)
                return;
            m_selectedSyncManagerId = current.data(StableIdRole).value<Data::NodeId>();
            m_selectedPdoId = {};
            rebuildPdoModels();
        });
    const auto pdoSelected = [this](const QModelIndex &current) {
        if (!m_rebuilding)
            selectPdo(current.data(StableIdRole).value<Data::NodeId>());
    };
    connect(
        m_assignments->selectionModel(), &QItemSelectionModel::currentRowChanged, this, pdoSelected);
    connect(m_pdoList->selectionModel(), &QItemSelectionModel::currentRowChanged, this, pdoSelected);
    connect(m_restoreDefaults, &QPushButton::clicked, this, [this] {
        if (!isEmpty(m_esiDefaults))
            submitConfiguration(m_esiDefaults);
    });
}

void ProcessDataPage::setContext(const Core::PropertyPageContext &context)
{
    m_context = context;
    const std::optional<Data::ProjectSnapshot> project
        = m_controller && m_controller->projectService()
              ? m_controller->projectService()->project(context.projectId)
              : std::nullopt;
    m_editable = false;
    m_esiDefaults = {};
    m_configuration = {};
    m_ownerSlaveId = {};
    m_selectedSyncManagerId = {};
    m_selectedPdoId = {};
    m_showingEsiDefaults = false;
    m_repositoryDeviceAvailable = false;
    m_repositoryDeviceSupported = false;
    m_repositoryProcessDataAvailable = false;
    m_repositoryProcessDataHasErrors = false;

    std::optional<Data::OfflineSlaveConfiguration> slave;
    std::optional<Data::DeviceDescription> device;
    if (m_controller) {
        slave = m_controller->treeModel()->offlineSlave(context.nodeId);
        if (slave) {
            m_ownerSlaveId = slave->id;
            m_configuration = slave->processData;
            m_editable = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && project
                         && project->valid;
        }
        if (m_controller->deviceRepository()) {
            if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
                device = m_controller->deviceRepository()->device(context.nodeId);
            } else if (slave && !slave->deviceDescriptionId.isNull()) {
                device = m_controller->deviceRepository()->device(slave->deviceDescriptionId);
            }
        }
    }
    if (device)
        m_esiDefaults = processDataDefaultsFromDevice(*device, slave ? slave->id : context.nodeId);
    m_repositoryDeviceAvailable
        = context.nodeKind == Core::WorkbenchNodeKind::Device && device.has_value();
    m_repositoryDeviceSupported
        = m_repositoryDeviceAvailable && device->summary.supported;
    m_repositoryProcessDataAvailable
        = m_repositoryDeviceAvailable && !m_esiDefaults.pdos.isEmpty();
    if (isEmpty(m_configuration) && !isEmpty(m_esiDefaults)) {
        m_configuration = m_esiDefaults;
        m_showingEsiDefaults = true;
    }
    m_repositoryProcessDataHasErrors
        = m_repositoryProcessDataAvailable
          && Data::validateProcessDataConfiguration(m_configuration).hasErrors();

    const Data::NodeId sourceId
        = m_controller ? m_controller->treeModel()->sourceNodeId(context.nodeId) : Data::NodeId();
    const std::optional<Data::PdoDirection> direction = directionForNodeKind(context.nodeKind);
    const auto selectedPdo = std::find_if(
        m_configuration.pdos.cbegin(),
        m_configuration.pdos.cend(),
        [&sourceId, direction, &context](const Data::PdoConfiguration &pdo) {
            if (direction)
                return pdo.selected && pdo.direction == *direction;
            if (context.nodeKind == Core::WorkbenchNodeKind::Pdo)
                return pdo.id == sourceId;
            if (context.nodeKind == Core::WorkbenchNodeKind::PdoEntry) {
                return std::any_of(
                    pdo.entries.cbegin(), pdo.entries.cend(), [&sourceId](const auto &entry) {
                        return entry.id == sourceId;
                    });
            }
            return false;
        });
    if (selectedPdo != m_configuration.pdos.cend()) {
        m_selectedPdoId = selectedPdo->id;
        const auto syncManager = std::find_if(
            m_configuration.syncManagers.cbegin(),
            m_configuration.syncManagers.cend(),
            [selectedPdo](const auto &entry) { return entry.index == selectedPdo->syncManager; });
        if (syncManager != m_configuration.syncManagers.cend())
            m_selectedSyncManagerId = syncManager->id;
    }

    if (context.nodeKind == Core::WorkbenchNodeKind::Device
        && !m_repositoryDeviceAvailable) {
        m_summary->setText(
            Tr::tr(
                "The ESI device description is no longer available. Return to Device Repository "
                "and select an available device before opening Process Data."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device
               && !m_repositoryProcessDataAvailable && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr(
                "No ESI Process Data mapping is available. This repository device also "
                "contains unsupported ESI structures and cannot be added to an offline "
                "Project. Review its support details in Device Repository; Workbench will not "
                "fabricate Sync Managers or PDOs, and no controller, network, or physical "
                "hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device
               && !m_repositoryProcessDataAvailable) {
        m_summary->setText(
            Tr::tr(
                "No ESI Process Data mapping is available for this repository device. The "
                "device can still be added to an offline Project, but Workbench will not "
                "fabricate Sync Managers or PDOs. Review its source in Device Repository or "
                "import a matching ESI description that contains Process Data; no controller, "
                "network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device
               && m_repositoryProcessDataHasErrors && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr(
                "ESI Process Data is available for read-only preview, but its validation "
                "errors and unsupported ESI structures mean this repository device cannot be "
                "added to an offline Project. Review the error and support details, then "
                "import a corrected matching ESI description through Device Repository; no "
                "controller, network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device
               && m_repositoryProcessDataHasErrors) {
        m_summary->setText(
            Tr::tr(
                "ESI Process Data is available for read-only preview, but validation errors "
                "mean this repository device cannot be added to an offline Project. Review the "
                "error details below and import a corrected matching ESI description "
                "through Device Repository; no controller, network, or physical hardware is "
                "accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device
               && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr(
                "ESI Process Data is available for read-only preview, but this repository "
                "device contains unsupported ESI structures and cannot be added to an offline "
                "Project. Review its support details in Device Repository; no controller, "
                "network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        m_summary->setText(
            Tr::tr(
                "ESI Process Data catalogue. Select a Sync Manager and PDO to inspect its "
                "mapping. Add the device to an offline project before editing."));
    } else if (context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave && slave) {
        m_summary->setText(
            Tr::tr(
                "Read-only tree selection. The owning slave and PDO are focused here; edit "
                "assignments on the configured-slave Process Data page."));
    } else if (m_showingEsiDefaults) {
        m_summary->setText(
            Tr::tr(
                "ESI defaults are shown but are not stored in the project. Store the defaults "
                "or edit an assignment to create an undoable offline configuration."));
    } else if (!isEmpty(m_configuration) && device) {
        m_summary->setText(
            Tr::tr(
                "Offline Process Data configuration with ESI reference. Changes are validated "
                "before they enter the project Undo/Redo history."));
    } else if (!isEmpty(m_configuration)) {
        m_summary->setText(
            Tr::tr(
                "Offline Process Data configuration. The matching ESI description is not "
                "available, but persisted stable-ID mappings remain editable."));
    } else {
        m_summary->setText(
            Tr::tr("No Process Data configuration or matching ESI description is available."));
    }

    m_restoreDefaults->setVisible(
        context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && device.has_value());
    m_restoreDefaults->setEnabled(m_editable && !isEmpty(m_esiDefaults));
    m_restoreDefaults->setText(
        m_showingEsiDefaults ? Tr::tr("Store ESI Defaults") : Tr::tr("Restore ESI Defaults"));
    updateTablePresentation();
    rebuildModels();
}

bool ProcessDataPage::submitConfiguration(const Data::ProcessDataConfiguration &configuration)
{
    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    if (validation.hasErrors()) {
        showValidation(validation, Tr::tr("Change not applied."));
        return false;
    }
    if (!m_editable || !m_controller || !m_controller->projectService()) {
        showValidation(
            validation, Tr::tr("Change not applied: this Process Data selection is read-only."));
        return false;
    }
    const Utils::Result<> result
        = m_controller->projectService()
              ->setProcessDataConfiguration(m_context.projectId, m_ownerSlaveId, configuration);
    if (!result) {
        showValidation(validation, Tr::tr("Change not applied: %1").arg(result.error()));
        return false;
    }

    m_configuration = configuration;
    m_showingEsiDefaults = false;
    m_restoreDefaults->setText(Tr::tr("Restore ESI Defaults"));
    rebuildModels();
    return true;
}

void ProcessDataPage::rebuildModels()
{
    m_rebuilding = true;
    m_syncManagerModel->setConfiguration(m_configuration);
    int syncManagerRow = rowForId(m_syncManagerModel, m_selectedSyncManagerId);
    if (syncManagerRow < 0 && m_syncManagerModel->rowCount() > 0)
        syncManagerRow = 0;
    m_selectedSyncManagerId
        = syncManagerRow >= 0
              ? m_syncManagerModel->index(syncManagerRow, 0).data(StableIdRole).value<Data::NodeId>()
              : Data::NodeId();
    m_syncManagers->setCurrentIndex(m_syncManagerModel->index(syncManagerRow, 0));
    m_rebuilding = false;
    rebuildPdoModels();

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        m_configuration);
    m_processImageModel->setValidation(validation);
    showValidation(validation);
}

void ProcessDataPage::rebuildPdoModels()
{
    int syncManager = -1;
    const auto selectedSyncManager = std::find_if(
        m_configuration.syncManagers.cbegin(),
        m_configuration.syncManagers.cend(),
        [this](const auto &entry) { return entry.id == m_selectedSyncManagerId; });
    if (selectedSyncManager != m_configuration.syncManagers.cend())
        syncManager = selectedSyncManager->index;

    m_rebuilding = true;
    m_assignmentModel->setConfiguration(m_configuration, syncManager, m_editable);
    m_pdoListModel->setConfiguration(m_configuration, syncManager);
    int pdoRow = rowForId(m_pdoListModel, m_selectedPdoId);
    if (pdoRow < 0 && m_pdoListModel->rowCount() > 0)
        pdoRow = 0;
    m_selectedPdoId = pdoRow >= 0
                          ? m_pdoListModel->index(pdoRow, 0).data(StableIdRole).value<Data::NodeId>()
                          : Data::NodeId();
    const int assignmentRow = rowForId(m_assignmentModel, m_selectedPdoId);
    m_assignments->setCurrentIndex(m_assignmentModel->index(assignmentRow, 0));
    m_pdoList->setCurrentIndex(m_pdoListModel->index(pdoRow, 0));
    m_pdoContentModel->setConfiguration(m_configuration, m_selectedPdoId, m_editable);
    m_rebuilding = false;
}

void ProcessDataPage::selectPdo(const Data::NodeId &pdoId)
{
    if (pdoId.isNull() || pdoId == m_selectedPdoId)
        return;
    m_selectedPdoId = pdoId;
    m_rebuilding = true;
    const int assignmentRow = rowForId(m_assignmentModel, pdoId);
    const int pdoRow = rowForId(m_pdoListModel, pdoId);
    if (assignmentRow >= 0)
        m_assignments->setCurrentIndex(m_assignmentModel->index(assignmentRow, 0));
    if (pdoRow >= 0)
        m_pdoList->setCurrentIndex(m_pdoListModel->index(pdoRow, 0));
    m_pdoContentModel->setConfiguration(m_configuration, pdoId, m_editable);
    m_rebuilding = false;
}

void ProcessDataPage::updateTablePresentation()
{
    const bool repositoryDeviceMissing
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && !m_repositoryDeviceAvailable;
    const bool repositoryProcessDataEmpty
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && m_repositoryDeviceAvailable && !m_repositoryProcessDataAvailable;
    const bool repositoryProcessDataEmptyUnsupported
        = repositoryProcessDataEmpty && !m_repositoryDeviceSupported;
    const bool repositoryProcessDataPreview
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && m_repositoryDeviceAvailable && m_repositoryProcessDataAvailable;
    const bool repositoryProcessDataPreviewUnsupported
        = repositoryProcessDataPreview && !m_repositoryDeviceSupported;
    const bool repositoryProcessDataPreviewInvalid
        = repositoryProcessDataPreview && m_repositoryProcessDataHasErrors;

    QString contextDescription;
    if (repositoryDeviceMissing) {
        contextDescription = Tr::tr(
            "The ESI device description is unavailable, so no Process Data can be shown. "
            "Return to Device Repository and select an available device. This read-only page "
            "does not access a controller, network, or physical hardware.");
    } else if (repositoryProcessDataEmptyUnsupported) {
        contextDescription = Tr::tr(
            "No ESI Process Data mapping is available. This repository device contains "
            "unsupported ESI structures and cannot be added to an offline Project. Review its "
            "support details in Device Repository. This read-only page will not fabricate Sync "
            "Managers or PDOs and does not access a controller, network, or physical hardware.");
    } else if (repositoryProcessDataEmpty) {
        contextDescription = Tr::tr(
            "No ESI Process Data mapping is available for this repository device. This "
            "read-only page will not fabricate Sync Managers or PDOs. Review its source in "
            "Device Repository or import a matching ESI description; no controller, network, "
            "or physical hardware is accessed.");
    } else if (repositoryProcessDataPreviewInvalid
               && repositoryProcessDataPreviewUnsupported) {
        contextDescription = Tr::tr(
            "This is a read-only preview of imported ESI Process Data. The mapping has "
            "validation errors and the repository device contains unsupported ESI structures, "
            "so it cannot be added to an offline Project. Review the error and support details, "
            "then import a corrected matching ESI description through Device Repository. The "
            "preview does not modify a Project or access a controller, network, or physical "
            "hardware.");
    } else if (repositoryProcessDataPreviewInvalid) {
        contextDescription = Tr::tr(
            "This is a read-only preview of imported ESI Process Data. The mapping has "
            "validation errors and cannot be added to an offline Project. Review the error "
            "details and import a corrected matching ESI description through Device Repository. "
            "The preview does not modify a Project or access a controller, network, or physical "
            "hardware.");
    } else if (repositoryProcessDataPreviewUnsupported) {
        contextDescription = Tr::tr(
            "This is a read-only preview of imported ESI Process Data. The repository device "
            "contains unsupported ESI structures and cannot be added to an offline Project. "
            "Review its support details in Device Repository. The preview does not modify a "
            "Project or access a controller, network, or physical hardware.");
    } else if (repositoryProcessDataPreview) {
        contextDescription = Tr::tr(
            "This is a read-only offline preview of imported ESI Process Data. Selecting rows "
            "changes only this page presentation; it does not modify a Project or access a "
            "controller, network, or physical hardware.");
    }

    const QList<QTableView *> tables = {
        m_syncManagers, m_assignments, m_pdoList, m_pdoContent, m_processImage};
    for (QTableView *table : tables) {
        QString description = table->property(baseDescriptionProperty).toString();
        if (!contextDescription.isEmpty())
            description += ' ' + contextDescription;
        table->setAccessibleDescription(description);
        table->setToolTip(contextDescription.isEmpty() ? QString() : description);
    }
}

void ProcessDataPage::showValidation(
    const Data::ConfigurationValidation &validation, const QString &prefix)
{
    int errorCount = 0;
    int warningCount = 0;
    QStringList details;
    for (const Data::ConfigurationIssue &issue : validation.issues) {
        details.append(issue.message);
        if (issue.severity == Data::ConfigurationIssueSeverity::Error)
            ++errorCount;
        else if (issue.severity == Data::ConfigurationIssueSeverity::Warning)
            ++warningCount;
    }

    QString text = prefix;
    if (!text.isEmpty() && !details.isEmpty())
        text += ' ';
    const bool repositoryDeviceMissing
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && !m_repositoryDeviceAvailable;
    const bool repositoryProcessDataEmpty
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && m_repositoryDeviceAvailable && !m_repositoryProcessDataAvailable;
    const bool repositoryProcessDataEmptyUnsupported
        = repositoryProcessDataEmpty && !m_repositoryDeviceSupported;
    const bool repositoryProcessDataPreviewUnsupported
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && m_repositoryDeviceAvailable && m_repositoryProcessDataAvailable
          && !m_repositoryDeviceSupported;
    const bool repositoryProcessDataPreviewInvalid
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && m_repositoryDeviceAvailable && m_repositoryProcessDataAvailable && errorCount > 0;
    QString repositoryPreviewNotice;
    if (repositoryProcessDataPreviewInvalid && repositoryProcessDataPreviewUnsupported) {
        repositoryPreviewNotice = Tr::tr(
            "This mapping has validation errors and the Device contains unsupported ESI "
            "structures, so it cannot be added to an offline Project. Review the error and "
            "support details, then import a corrected matching ESI description through Device "
            "Repository.");
    } else if (repositoryProcessDataPreviewInvalid) {
        repositoryPreviewNotice = Tr::tr(
            "This mapping cannot be added to an offline Project. Import a corrected matching ESI "
            "description through Device Repository.");
    } else if (repositoryProcessDataPreviewUnsupported) {
        repositoryPreviewNotice = Tr::tr(
            "This Device contains unsupported ESI structures and cannot be added to an offline "
            "Project. Review its support details in Device Repository.");
    }
    if (repositoryDeviceMissing) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr(
            "The ESI device description is unavailable, so no Process Data can be shown.");
    } else if (repositoryProcessDataEmptyUnsupported) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr(
            "No ESI Process Data mapping is available. This device contains unsupported ESI "
            "structures and cannot be added to an offline Project. Review its support details "
            "in Device Repository.");
    } else if (repositoryProcessDataEmpty) {
        m_validation->setType(Utils::InfoLabel::Information);
        text += Tr::tr(
            "No ESI Process Data mapping is available to preview; Workbench will not fabricate "
            "Sync Managers or PDOs.");
    } else if (errorCount > 0) {
        m_validation->setType(Utils::InfoLabel::Error);
        text += Tr::tr("%n configuration error(s).", nullptr, errorCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
        if (!repositoryPreviewNotice.isEmpty())
            text += ' ' + repositoryPreviewNotice;
    } else if (warningCount > 0) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr("%n configuration warning(s).", nullptr, warningCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
        if (!repositoryPreviewNotice.isEmpty())
            text += ' ' + repositoryPreviewNotice;
    } else if (repositoryProcessDataPreviewUnsupported) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr("This Process Data is available only for read-only preview. ");
        text += repositoryPreviewNotice;
    } else {
        m_validation->setType(Utils::InfoLabel::Ok);
        text += Tr::tr("Configuration is valid. Outputs: %1 byte(s); inputs: %2 byte(s).")
                    .arg(validation.processImage.outputs.byteSize)
                    .arg(validation.processImage.inputs.byteSize);
    }
    m_validation->setText(text);
    m_validation->setToolTip(details.join('\n'));
}

} // namespace EtherCAT::Workbench::Internal
