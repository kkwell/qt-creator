// Copyright (C) 2026 Kvell

#include "startuppage.h"

#include "esiconfigurationfactory.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#include <QAbstractItemView>
#include <QAbstractTableModel>
#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace EtherCAT::Workbench::Internal {

enum TableRole {
    StableIdRole = Qt::UserRole + 1,
    DataTypeRole,
};

static constexpr char baseDescriptionProperty[] = "EtherCAT.Startup.BaseAccessibleDescription";

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

static QList<Data::EtherCATDataType> dataTypes()
{
    return {
        Data::EtherCATDataType::Unknown,
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
}

static bool isEmpty(const Data::StartupConfiguration &configuration)
{
    return configuration.parameters.isEmpty();
}

static bool isFixed(const Data::StartupParameterConfiguration &parameter)
{
    const QString transition = parameter.transition.trimmed();
    return transition.startsWith('<') && transition.endsWith('>');
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

static std::optional<QByteArray> parseRawValue(
    const QVariant &value, QString *rejectionReason = nullptr)
{
    if (rejectionReason)
        rejectionReason->clear();
    QString compact;
    const QString source = value.toString().trimmed();
    compact.reserve(source.size());
    for (const QChar character : source) {
        if (character.isSpace() || character == ':' || character == '_')
            continue;
        compact.append(character);
    }
    if (compact.startsWith("0x", Qt::CaseInsensitive))
        compact.remove(0, 2);
    if (compact.isEmpty()) {
        if (rejectionReason) {
            *rejectionReason
                = Tr::tr("Startup request Data must contain at least one hexadecimal byte.");
        }
        return std::nullopt;
    }
    if (compact.size() % 2 != 0) {
        if (rejectionReason) {
            *rejectionReason = Tr::tr(
                "Startup request Data must contain an even number of hexadecimal digits.");
        }
        return std::nullopt;
    }
    static const QString hexadecimal = "0123456789abcdefABCDEF";
    if (std::any_of(compact.cbegin(), compact.cend(), [](QChar character) {
            return !hexadecimal.contains(character);
        })) {
        if (rejectionReason) {
            *rejectionReason
                = Tr::tr("Startup request Data contains a non-hexadecimal character.");
        }
        return std::nullopt;
    }
    return QByteArray::fromHex(compact.toLatin1());
}

static QString rawValueText(const QByteArray &value)
{
    return QString::fromLatin1(value.toHex(' ').toUpper());
}

static QString startupObjectAddress(const Data::StartupParameterConfiguration &parameter)
{
    return QString("%1:%2")
        .arg(
            hexValue(parameter.index, 4),
            QString("%1").arg(parameter.subIndex, 2, 16, QLatin1Char('0')).toUpper());
}

class StartupTableModel final : public QAbstractTableModel
{
public:
    enum Column {
        Enabled,
        Order,
        Transition,
        Protocol,
        Index,
        Subindex,
        Type,
        Data,
        Comment,
        ColumnCount
    };

    explicit StartupTableModel(StartupPage *page)
        : QAbstractTableModel(page)
        , m_page(page)
    {}

    int rowCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : m_parameters.size();
    }

    int columnCount(const QModelIndex &parent = {}) const final
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid() || index.row() >= m_parameters.size())
            return {};
        const Data::StartupParameterConfiguration &parameter = m_parameters.at(index.row());
        if (role == StableIdRole)
            return QVariant::fromValue(parameter.id);
        if (role == DataTypeRole)
            return int(parameter.dataType);
        if (role == Qt::CheckStateRole && index.column() == Enabled)
            return parameter.enabled ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::AccessibleTextRole) {
            if (index.column() == Enabled)
                return parameter.enabled ? Tr::tr("Enabled") : Tr::tr("Disabled");
            return data(index, Qt::DisplayRole).toString();
        }
        if (role == Qt::AccessibleDescriptionRole || role == Qt::ToolTipRole) {
            const QString header = headerData(index.column(), Qt::Horizontal).toString();
            const QString text = data(index, Qt::AccessibleTextRole).toString();
            QString description = text.isEmpty()
                                      ? Tr::tr("Request %1, %2")
                                            .arg(startupObjectAddress(parameter), header)
                                      : Tr::tr("Request %1, %2: %3")
                                            .arg(startupObjectAddress(parameter), header, text);
            if (isFixed(parameter)) {
                description += '\n'
                               + Tr::tr(
                                   "The angle-bracketed transition identifies a fixed ESI "
                                   "request. It is read-only and cannot be enabled or disabled, "
                                   "edited, deleted, or moved.");
            } else if (!m_editable) {
                description += '\n'
                               + Tr::tr(
                                   "This Startup request is read-only in the current context.");
            } else if (flags(index) & Qt::ItemIsUserCheckable) {
                description += '\n'
                               + Tr::tr(
                                   "Enable or disable this request in the offline Startup "
                                   "sequence.");
            } else if (flags(index) & Qt::ItemIsEditable) {
                description += '\n'
                               + Tr::tr(
                                   "Edit this value directly or with Edit; Project Undo and Redo "
                                   "remain available.");
            } else {
                description += '\n'
                               + Tr::tr(
                                   "This value is read-only; other non-fixed request fields "
                                   "remain editable.");
            }
            return description;
        }
        if (role != Qt::DisplayRole && role != Qt::EditRole)
            return {};
        switch (index.column()) {
        case Enabled:
            return {};
        case Order:
            return parameter.order;
        case Transition:
            return parameter.transition;
        case Protocol:
            return Tr::tr("CoE");
        case Index:
            return role == Qt::EditRole ? QVariant(parameter.index)
                                        : QVariant(hexValue(parameter.index, 4));
        case Subindex:
            return role == Qt::EditRole ? QVariant(parameter.subIndex)
                                        : QVariant(hexValue(parameter.subIndex, 2));
        case Type:
            return dataTypeName(parameter.dataType, parameter.rawDataType);
        case Data:
            return rawValueText(parameter.rawValue);
        case Comment:
            return parameter.comment;
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
            = {Tr::tr("Enabled"),
               Tr::tr("Order"),
               Tr::tr("Transition"),
               Tr::tr("Protocol"),
               Tr::tr("Index"),
               Tr::tr("Subindex"),
               Tr::tr("Type"),
               Tr::tr("Data"),
               Tr::tr("Comment")};
        return headers.value(section);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const final
    {
        if (!index.isValid() || index.row() >= m_parameters.size())
            return Qt::NoItemFlags;
        const Data::StartupParameterConfiguration &parameter = m_parameters.at(index.row());
        Qt::ItemFlags result = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        if (!m_editable || isFixed(parameter))
            return result;
        if (index.column() == Enabled)
            result |= Qt::ItemIsUserCheckable;
        if (index.column() == Order || index.column() == Transition || index.column() == Index
            || index.column() == Subindex || index.column() == Type || index.column() == Data
            || index.column() == Comment) {
            result |= Qt::ItemIsEditable;
        }
        return result;
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) final
    {
        if (!index.isValid() || index.row() >= m_parameters.size() || !m_editable)
            return false;
        const Data::StartupParameterConfiguration current = m_parameters.at(index.row());
        if (isFixed(current))
            return false;
        const bool checkStateEdit = role == Qt::CheckStateRole && index.column() == Enabled;
        const bool dataTypeEdit = role == DataTypeRole && index.column() == Type;
        if (role != Qt::EditRole && !checkStateEdit && !dataTypeEdit) {
            return false;
        }
        m_lastEditRejection.clear();

        Data::StartupConfiguration candidate = m_configuration;
        const auto found = std::find_if(
            candidate.parameters.begin(),
            candidate.parameters.end(),
            [&current](const auto &entry) { return entry.id == current.id; });
        if (found == candidate.parameters.end())
            return false;

        const auto rejectUserInput = [this](const QString &reason) {
            m_lastEditRejection = Tr::tr("Change not applied. %1").arg(reason);
            return false;
        };
        quint64 unsignedValue = 0;
        switch (index.column()) {
        case Enabled:
            if (role != Qt::CheckStateRole)
                return false;
            found->enabled = value.toInt() == Qt::Checked;
            break;
        case Order:
            if (!parseUnsignedValue(value, std::numeric_limits<int>::max(), &unsignedValue)) {
                return rejectUserInput(
                    Tr::tr("Startup request Order must be a decimal or 0x-prefixed hexadecimal "
                           "integer from 0 to 2147483647."));
            }
            found->order = int(unsignedValue);
            break;
        case Transition:
            found->transition = value.toString().trimmed();
            if (found->transition.startsWith('<') || found->transition.endsWith('>')) {
                return rejectUserInput(Tr::tr(
                    "Angle brackets are reserved for fixed requests imported from ESI."));
            }
            break;
        case Protocol:
            return false;
        case Index:
            if (!parseUnsignedValue(value, std::numeric_limits<quint16>::max(), &unsignedValue)) {
                return rejectUserInput(
                    Tr::tr("Startup request Index must be a decimal or 0x-prefixed hexadecimal "
                           "integer from 0 to 65535."));
            }
            found->index = quint16(unsignedValue);
            break;
        case Subindex:
            if (!parseUnsignedValue(value, std::numeric_limits<quint8>::max(), &unsignedValue)) {
                return rejectUserInput(
                    Tr::tr("Startup request Subindex must be a decimal or 0x-prefixed hexadecimal "
                           "integer from 0 to 255."));
            }
            found->subIndex = quint8(unsignedValue);
            break;
        case Type: {
            bool ok = false;
            const int typeValue = value.toInt(&ok);
            if (!ok || typeValue < int(Data::EtherCATDataType::Unknown)
                || typeValue > int(Data::EtherCATDataType::OctetString)) {
                return rejectUserInput(Tr::tr("Startup request Type is unavailable."));
            }
            found->dataType = Data::EtherCATDataType(typeValue);
            found->rawDataType = found->dataType == Data::EtherCATDataType::Unknown
                                     ? QString()
                                     : dataTypeName(found->dataType);
            break;
        }
        case Data: {
            if (value.toString().trimmed().isEmpty()) {
                found->rawValue.clear();
                break;
            }
            QString rejectionReason;
            const std::optional<QByteArray> rawValue = parseRawValue(value, &rejectionReason);
            if (!rawValue)
                return rejectUserInput(rejectionReason);
            found->rawValue = *rawValue;
            break;
        }
        case Comment:
            found->comment = value.toString().trimmed();
            break;
        default:
            return false;
        }
        QString rejection;
        const bool accepted
            = m_page
              && m_page->submitConfiguration(
                  candidate, current.id, index.column(), &rejection);
        if (!accepted)
            m_lastEditRejection = rejection;
        return accepted;
    }

    void clearEditRejection()
    {
        m_lastEditRejection.clear();
    }

    QString takeEditRejection()
    {
        return std::exchange(m_lastEditRejection, {});
    }

    bool setConfiguration(
        const Data::StartupConfiguration &configuration,
        bool editable,
        const Data::NodeId &preservedParameterId = {},
        int preservedColumn = -1,
        bool notifyPreservedColumn = false,
        bool *preservedMetadataChanged = nullptr)
    {
        if (preservedMetadataChanged)
            *preservedMetadataChanged = false;
        QList<Data::StartupParameterConfiguration> parameters = configuration.parameters;
        std::stable_sort(
            parameters.begin(), parameters.end(), [](const auto &left, const auto &right) {
                return left.order < right.order;
            });
        if (!preservedParameterId.isNull() && preservedColumn >= 0 && m_editable == editable
            && hasValidUniqueIds(m_parameters) && hasValidUniqueIds(parameters)) {
            const bool metadataChanged = synchronizeConfiguration(
                configuration,
                parameters,
                editable,
                preservedParameterId,
                preservedColumn,
                notifyPreservedColumn);
            if (preservedMetadataChanged)
                *preservedMetadataChanged = metadataChanged;
            return false;
        }

        beginResetModel();
        m_configuration = configuration;
        m_parameters = parameters;
        m_editable = editable;
        endResetModel();
        return true;
    }

    const Data::StartupParameterConfiguration *parameterAt(int row) const
    {
        return row >= 0 && row < m_parameters.size() ? &m_parameters.at(row) : nullptr;
    }

    QList<Data::NodeId> orderedIds() const
    {
        QList<Data::NodeId> result;
        result.reserve(m_parameters.size());
        for (const Data::StartupParameterConfiguration &parameter : m_parameters)
            result.append(parameter.id);
        return result;
    }

    void notifyMetadataChanged(const Data::NodeId &parameterId, int column)
    {
        for (int row = 0; row < m_parameters.size(); ++row) {
            if (m_parameters.at(row).id != parameterId)
                continue;
            const QModelIndex changedIndex = index(row, column);
            emit dataChanged(
                changedIndex,
                changedIndex,
                {Qt::AccessibleTextRole,
                 Qt::AccessibleDescriptionRole,
                 Qt::ToolTipRole});
            return;
        }
    }

private:
    static bool hasValidUniqueIds(const QList<Data::StartupParameterConfiguration> &parameters)
    {
        for (qsizetype left = 0; left < parameters.size(); ++left) {
            if (parameters.at(left).id.isNull())
                return false;
            for (qsizetype right = left + 1; right < parameters.size(); ++right) {
                if (parameters.at(left).id == parameters.at(right).id)
                    return false;
            }
        }
        return true;
    }

    bool synchronizeConfiguration(
        const Data::StartupConfiguration &configuration,
        const QList<Data::StartupParameterConfiguration> &parameters,
        bool editable,
        const Data::NodeId &preservedParameterId,
        int preservedColumn,
        bool notifyPreservedColumn)
    {
        bool preservedMetadataChanged = false;
        const auto containsId = [](const auto &entries, const Data::NodeId &id) {
            return std::any_of(entries.cbegin(), entries.cend(), [&id](const auto &entry) {
                return entry.id == id;
            });
        };
        for (int row = int(m_parameters.size()) - 1; row >= 0; --row) {
            if (containsId(parameters, m_parameters.at(row).id))
                continue;
            beginRemoveRows({}, row, row);
            m_parameters.removeAt(row);
            endRemoveRows();
        }

        for (int targetRow = 0; targetRow < parameters.size(); ++targetRow) {
            const Data::NodeId targetId = parameters.at(targetRow).id;
            int currentRow = -1;
            for (int row = 0; row < m_parameters.size(); ++row) {
                if (m_parameters.at(row).id == targetId) {
                    currentRow = row;
                    break;
                }
            }
            if (currentRow < 0) {
                beginInsertRows({}, targetRow, targetRow);
                m_parameters.insert(targetRow, parameters.at(targetRow));
                endInsertRows();
            } else if (currentRow != targetRow) {
                const int destination = currentRow < targetRow ? targetRow + 1 : targetRow;
                beginMoveRows({}, currentRow, currentRow, {}, destination);
                m_parameters.move(currentRow, targetRow);
                endMoveRows();
            }
        }

        m_configuration = configuration;
        m_editable = editable;
        for (int row = 0; row < parameters.size(); ++row) {
            if (m_parameters.at(row) == parameters.at(row))
                continue;
            m_parameters[row] = parameters.at(row);
            const bool preservedRow = parameters.at(row).id == preservedParameterId;
            preservedMetadataChanged |= preservedRow && !notifyPreservedColumn;
            if (!preservedRow || notifyPreservedColumn) {
                emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
            } else if (preservedColumn > 0) {
                emit dataChanged(
                    index(row, 0),
                    index(row, preservedColumn - 1));
            }
            if (preservedRow && !notifyPreservedColumn && preservedColumn + 1 < ColumnCount) {
                emit dataChanged(
                    index(row, preservedColumn + 1), index(row, ColumnCount - 1));
            }
        }
        return preservedMetadataChanged;
    }

    StartupPage *m_page = nullptr;
    Data::StartupConfiguration m_configuration;
    QList<Data::StartupParameterConfiguration> m_parameters;
    bool m_editable = false;
    QString m_lastEditRejection;
};

static bool sameInlineEditorAuthority(
    const Data::StartupParameterConfiguration &left,
    const Data::StartupParameterConfiguration &right,
    int column)
{
    if (left.id != right.id)
        return false;
    switch (column) {
    case StartupTableModel::Order:
        return left.order == right.order;
    case StartupTableModel::Transition:
        return left.transition == right.transition;
    case StartupTableModel::Index:
        return left.index == right.index;
    case StartupTableModel::Subindex:
        return left.subIndex == right.subIndex;
    case StartupTableModel::Type:
        return left.dataType == right.dataType && left.rawDataType == right.rawDataType;
    case StartupTableModel::Data:
        return left.rawValue == right.rawValue;
    case StartupTableModel::Comment:
        return left.comment == right.comment;
    default:
        return false;
    }
}

using EditorOpenedHandler = std::function<void(QWidget *, const QModelIndex &)>;
using EditRejectedHandler = std::function<void(const QModelIndex &, const QString &, bool)>;

class StartupItemDelegate : public QStyledItemDelegate
{
public:
    StartupItemDelegate(
        EditorOpenedHandler editorOpened, EditRejectedHandler editRejected, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_editorOpened(std::move(editorOpened))
        , m_editRejected(std::move(editRejected))
    {}

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        notifyEditorOpened(editor, index);
        return editor;
    }

    void setModelData(
        QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto startupModel = static_cast<StartupTableModel *>(model);
        startupModel->clearEditRejection();
        QStyledItemDelegate::setModelData(editor, model, index);
        notifyEditRejected(startupModel, index, true);
    }

protected:
    bool editorEvent(
        QEvent *event,
        QAbstractItemModel *model,
        const QStyleOptionViewItem &option,
        const QModelIndex &index) override
    {
        auto startupModel = static_cast<StartupTableModel *>(model);
        startupModel->clearEditRejection();
        const bool handled = QStyledItemDelegate::editorEvent(event, model, option, index);
        notifyEditRejected(startupModel, index, false);
        return handled;
    }

    void notifyEditorOpened(QWidget *editor, const QModelIndex &index) const
    {
        if (editor && m_editorOpened)
            m_editorOpened(editor, index);
    }

    void notifyEditRejected(
        StartupTableModel *model, const QModelIndex &index, bool inlineEditorSubmission) const
    {
        const QString rejection = model->takeEditRejection();
        if (!rejection.isEmpty() && m_editRejected)
            m_editRejected(index, rejection, inlineEditorSubmission);
    }

private:
    EditorOpenedHandler m_editorOpened;
    EditRejectedHandler m_editRejected;
};

class DataTypeDelegate final : public StartupItemDelegate
{
public:
    using StartupItemDelegate::StartupItemDelegate;

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const final
    {
        auto editor = new QComboBox(parent);
        for (Data::EtherCATDataType type : dataTypes())
            editor->addItem(dataTypeName(type), int(type));
        notifyEditorOpened(editor, index);
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const final
    {
        if (auto comboBox = qobject_cast<QComboBox *>(editor))
            comboBox->setCurrentIndex(comboBox->findData(index.data(DataTypeRole)));
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const final
    {
        auto startupModel = static_cast<StartupTableModel *>(model);
        startupModel->clearEditRejection();
        if (auto comboBox = qobject_cast<QComboBox *>(editor)) {
            model->setData(index, comboBox->currentData(), DataTypeRole);
            notifyEditRejected(startupModel, index, true);
        }
    }

    void updateEditorGeometry(
        QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const final
    {
        editor->setGeometry(option.rect);
    }
};

class TransitionDelegate final : public StartupItemDelegate
{
public:
    using StartupItemDelegate::StartupItemDelegate;

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const final
    {
        auto editor = new QComboBox(parent);
        editor->setEditable(true);
        editor->addItems({"PS", "SO", "IP", "OS", "SP", "PI"});
        notifyEditorOpened(editor, index);
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const final
    {
        if (auto comboBox = qobject_cast<QComboBox *>(editor))
            comboBox->setCurrentText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const final
    {
        auto startupModel = static_cast<StartupTableModel *>(model);
        startupModel->clearEditRejection();
        if (auto comboBox = qobject_cast<QComboBox *>(editor)) {
            model->setData(index, comboBox->currentText().trimmed());
            notifyEditRejected(startupModel, index, true);
        }
    }

    void updateEditorGeometry(
        QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const final
    {
        editor->setGeometry(option.rect);
    }
};

class StartupTableView final : public QTableView
{
public:
    using QTableView::QTableView;

    void discardInlineEditor(QWidget *editor)
    {
        closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
    }
};

class StartupParameterDialog final : public QDialog
{
public:
    StartupParameterDialog(
        const Data::StartupParameterConfiguration &parameter, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_parameter(parameter)
        , m_enabled(new QCheckBox(Tr::tr("Send this request during startup"), this))
        , m_transition(new QComboBox(this))
        , m_index(new QLineEdit(this))
        , m_subindex(new QLineEdit(this))
        , m_type(new QComboBox(this))
        , m_data(new QLineEdit(this))
        , m_comment(new QLineEdit(this))
        , m_validation(new Utils::InfoLabel(this))
    {
        setWindowTitle(Tr::tr("Startup Request"));
        setModal(true);
        setObjectName("EtherCATStartupParameterDialog");

        m_enabled->setObjectName("EtherCATStartupDialogEnabled");
        m_transition->setObjectName("EtherCATStartupDialogTransition");
        m_index->setObjectName("EtherCATStartupDialogIndex");
        m_subindex->setObjectName("EtherCATStartupDialogSubindex");
        m_type->setObjectName("EtherCATStartupDialogType");
        m_data->setObjectName("EtherCATStartupDialogData");
        m_comment->setObjectName("EtherCATStartupDialogComment");
        m_validation->setObjectName("EtherCATStartupDialogValidation");

        m_transition->setEditable(true);
        m_transition->addItems({"PS", "SO", "IP", "OS", "SP", "PI"});
        for (Data::EtherCATDataType type : dataTypes())
            m_type->addItem(dataTypeName(type), int(type));

        m_enabled->setChecked(parameter.enabled);
        m_transition->setCurrentText(parameter.transition.isEmpty() ? "PS" : parameter.transition);
        m_index->setText(hexValue(parameter.index, 4));
        m_subindex->setText(hexValue(parameter.subIndex, 2));
        m_type->setCurrentIndex(m_type->findData(int(parameter.dataType)));
        m_data->setText(rawValueText(parameter.rawValue));
        m_comment->setText(parameter.comment);
        m_validation->setType(Utils::InfoLabelType::Error);
        m_validation->setElideMode(Qt::ElideNone);
        m_validation->setWordWrap(true);
        m_validation->hide();

        auto form = new QFormLayout;
        form->setContentsMargins(QMargins());
        form->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->addRow(Tr::tr("Transition:"), m_transition);
        form->addRow(Tr::tr("Protocol:"), new QLabel(Tr::tr("CoE"), this));
        form->addRow(Tr::tr("Index:"), m_index);
        form->addRow(Tr::tr("Subindex:"), m_subindex);
        form->addRow(Tr::tr("Data type:"), m_type);
        form->addRow(Tr::tr("Data (hex):"), m_data);
        form->addRow(Tr::tr("Comment:"), m_comment);

        auto buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM,
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM);
        layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
        layout->addWidget(m_enabled);
        layout->addLayout(form);
        layout->addWidget(m_validation);
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &StartupParameterDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &StartupParameterDialog::reject);
    }

    Data::StartupParameterConfiguration parameter() const { return m_parameter; }

    void accept() final
    {
        quint64 index = 0;
        quint64 subindex = 0;
        const std::optional<QByteArray> rawValue = parseRawValue(m_data->text());
        if (!parseUnsignedValue(m_index->text(), std::numeric_limits<quint16>::max(), &index)
            || !parseUnsignedValue(m_subindex->text(), std::numeric_limits<quint8>::max(), &subindex)
            || !rawValue) {
            m_validation->setText(
                Tr::tr("Enter a valid object index, subindex, and an even number of hex digits."));
            m_validation->show();
            return;
        }

        m_parameter.enabled = m_enabled->isChecked();
        m_parameter.transition = m_transition->currentText().trimmed();
        if (m_parameter.transition.startsWith('<') || m_parameter.transition.endsWith('>')) {
            m_validation->setText(
                Tr::tr("Angle brackets are reserved for fixed requests imported from ESI."));
            m_validation->show();
            return;
        }
        m_parameter.index = quint16(index);
        m_parameter.subIndex = quint8(subindex);
        m_parameter.dataType = Data::EtherCATDataType(m_type->currentData().toInt());
        m_parameter.rawDataType = m_parameter.dataType == Data::EtherCATDataType::Unknown
                                      ? QString()
                                      : dataTypeName(m_parameter.dataType);
        m_parameter.rawValue = *rawValue;
        m_parameter.comment = m_comment->text().trimmed();

        const QList<Data::ConfigurationIssue> issues = Data::validateStartupConfiguration(
            {{m_parameter}});
        const auto error = std::find_if(issues.cbegin(), issues.cend(), [](const auto &issue) {
            return issue.severity == Data::ConfigurationIssueSeverity::Error;
        });
        if (error != issues.cend()) {
            m_validation->setText(error->message);
            m_validation->show();
            return;
        }
        QDialog::accept();
    }

private:
    Data::StartupParameterConfiguration m_parameter;
    QCheckBox *m_enabled = nullptr;
    QComboBox *m_transition = nullptr;
    QLineEdit *m_index = nullptr;
    QLineEdit *m_subindex = nullptr;
    QComboBox *m_type = nullptr;
    QLineEdit *m_data = nullptr;
    QLineEdit *m_comment = nullptr;
    Utils::InfoLabel *m_validation = nullptr;
};

static int rowForId(const StartupTableModel *model, const Data::NodeId &id)
{
    if (!model || id.isNull())
        return -1;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data(StableIdRole).value<Data::NodeId>() == id)
            return row;
    }
    return -1;
}

StartupPage::StartupPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_validation(new Utils::InfoLabel(this))
    , m_restoreDefaults(new QPushButton(Tr::tr("Store ESI Defaults"), this))
    , m_table(new StartupTableView(this))
    , m_moveUp(new QPushButton(Tr::tr("Move Up"), this))
    , m_moveDown(new QPushButton(Tr::tr("Move Down"), this))
    , m_new(new QPushButton(Tr::tr("New..."), this))
    , m_delete(new QPushButton(Tr::tr("Delete"), this))
    , m_edit(new QPushButton(Tr::tr("Edit..."), this))
    , m_model(new StartupTableModel(this))
{
    setObjectName("EtherCATWorkbenchStartupPage");
    m_summary->setObjectName("EtherCATStartupSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_validation->setObjectName("EtherCATStartupValidation");
    m_validation->setAccessibleName(Tr::tr("Startup edit feedback"));
    m_validation->setElideMode(Qt::ElideNone);
    m_validation->setWordWrap(true);
    m_restoreDefaults->setObjectName("EtherCATStartupRestoreDefaults");
    m_table->setObjectName("EtherCATStartupTable");
    m_table->setAccessibleName(Tr::tr("Startup requests"));
    m_table->setAccessibleDescription(
        Tr::tr(
            "Ordered offline CoE Startup requests with transition, object address, data, and "
            "comment details."));
    m_table->setProperty(baseDescriptionProperty, m_table->accessibleDescription());
    m_moveUp->setObjectName("EtherCATStartupMoveUp");
    m_moveDown->setObjectName("EtherCATStartupMoveDown");
    m_new->setObjectName("EtherCATStartupNew");
    m_delete->setObjectName("EtherCATStartupDelete");
    m_edit->setObjectName("EtherCATStartupEdit");

    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()
        ->setSectionResizeMode(StartupTableModel::Comment, QHeaderView::Stretch);
    const EditorOpenedHandler editorOpened = [this](QWidget *editor, const QModelIndex &index) {
        if (const Data::StartupParameterConfiguration *parameter
            = m_model->parameterAt(index.row())) {
            trackInlineEditor(editor, *parameter, index.column());
        }
    };
    const EditRejectedHandler editRejected = [this](
                                                   const QModelIndex &index,
                                                   const QString &message,
                                                   bool inlineEditorSubmission) {
        if (const Data::StartupParameterConfiguration *parameter
            = m_model->parameterAt(index.row())) {
            showEditRejection(parameter->id, index.column(), message, inlineEditorSubmission);
        }
    };
    m_table->setItemDelegate(new StartupItemDelegate(editorOpened, editRejected, m_table));
    m_table->setItemDelegateForColumn(
        StartupTableModel::Type, new DataTypeDelegate(editorOpened, editRejected, m_table));
    m_table->setItemDelegateForColumn(
        StartupTableModel::Transition,
        new TransitionDelegate(editorOpened, editRejected, m_table));

    auto headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(QMargins());
    headerLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    headerLayout->addWidget(m_summary, 1);
    headerLayout->addWidget(m_restoreDefaults);

    auto buttonLayout = new QVBoxLayout;
    buttonLayout->setContentsMargins(QMargins());
    buttonLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    buttonLayout->addWidget(m_moveUp);
    buttonLayout->addWidget(m_moveDown);
    buttonLayout->addSpacing(Utils::StyleHelper::SpacingTokens::GapVL);
    buttonLayout->addWidget(m_new);
    buttonLayout->addWidget(m_delete);
    buttonLayout->addWidget(m_edit);
    buttonLayout->addStretch();

    auto tableLayout = new QHBoxLayout;
    tableLayout->setContentsMargins(QMargins());
    tableLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    tableLayout->addWidget(m_table, 1);
    tableLayout->addLayout(buttonLayout);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addLayout(headerLayout);
    layout->addWidget(m_validation);
    layout->addLayout(tableLayout, 1);

    connect(
        m_table->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        [this](const QModelIndex &current) {
            if (!m_rebuilding)
                m_selectedParameterId = current.data(StableIdRole).value<Data::NodeId>();
            updateButtonState();
        });
    connect(
        m_table->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        [this](const QModelIndex &current, const QModelIndex &previous) {
            if (!m_rebuilding && current != previous)
                clearInlineEditRejection();
        });
    connect(m_restoreDefaults, &QPushButton::clicked, this, [this] {
        if (!isEmpty(m_esiDefaults))
            submitConfiguration(m_esiDefaults);
    });
    connect(m_moveUp, &QPushButton::clicked, this, [this] { moveParameter(-1); });
    connect(m_moveDown, &QPushButton::clicked, this, [this] { moveParameter(1); });
    connect(m_new, &QPushButton::clicked, this, &StartupPage::addParameter);
    connect(m_delete, &QPushButton::clicked, this, &StartupPage::deleteParameter);
    connect(m_edit, &QPushButton::clicked, this, &StartupPage::editParameter);
}

StartupPage::~StartupPage()
{
    if (m_inlineEditor) {
        m_inlineEditorMetadataDirty = false;
        QPointer<QWidget> editor = m_inlineEditor;
        static_cast<StartupTableView *>(m_table)->discardInlineEditor(editor);
        delete editor.data();
    }
}

void StartupPage::setContext(const Core::PropertyPageContext &context)
{
    const bool stableContext = m_context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
                               && context.nodeKind == m_context.nodeKind
                               && context.projectId == m_context.projectId
                               && context.nodeId == m_context.nodeId;
    ++m_contextGeneration;
    m_context = context;
    const std::optional<Data::ProjectSnapshot> project
        = m_controller && m_controller->projectService()
              ? m_controller->projectService()->project(context.projectId)
              : std::nullopt;
    m_editable = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && project
                 && project->valid;
    m_configuration = {};
    m_esiDefaults = {};
    m_showingEsiDefaults = false;
    m_repositoryDeviceAvailable = false;
    m_repositoryDeviceSupported = false;
    m_repositoryStartupAvailable = false;
    m_repositoryStartupHasErrors = false;

    std::optional<Data::OfflineSlaveConfiguration> slave;
    std::optional<Data::DeviceDescription> device;
    if (m_controller) {
        if (context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave) {
            slave = m_controller->treeModel()->offlineSlave(context.nodeId);
            if (slave)
                m_configuration = slave->startup;
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
        m_esiDefaults = startupDefaultsFromDevice(*device, context.nodeId);
    m_repositoryDeviceAvailable = context.nodeKind == Core::WorkbenchNodeKind::Device
                                  && device.has_value();
    m_repositoryDeviceSupported = m_repositoryDeviceAvailable && device->summary.supported;
    m_repositoryStartupAvailable = m_repositoryDeviceAvailable
                                   && !m_esiDefaults.parameters.isEmpty();
    if (isEmpty(m_configuration) && !isEmpty(m_esiDefaults)) {
        m_configuration = m_esiDefaults;
        m_showingEsiDefaults = true;
    }
    if (m_repositoryStartupAvailable) {
        const QList<Data::ConfigurationIssue> issues = Data::validateStartupConfiguration(
            m_configuration);
        m_repositoryStartupHasErrors
            = std::any_of(issues.cbegin(), issues.cend(), [](const auto &issue) {
                  return issue.severity == Data::ConfigurationIssueSeverity::Error;
              });
    }

    if (context.nodeKind == Core::WorkbenchNodeKind::Device && !m_repositoryDeviceAvailable) {
        m_summary->setText(
            Tr::tr("The ESI device description is no longer available. Return to Device Repository "
                   "and select an available device before opening Startup."));
    } else if (
        context.nodeKind == Core::WorkbenchNodeKind::Device && !m_repositoryStartupAvailable
        && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr("No ESI Startup request is available. This repository device also contains "
                   "unsupported ESI structures and cannot be added to an offline Project. Review "
                   "its support details in Device Repository; Workbench will not fabricate Startup "
                   "requests, and no SDO, controller, network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device && !m_repositoryStartupAvailable) {
        m_summary->setText(
            Tr::tr("No ESI Startup request is available for this repository device. The device can "
                   "still be added to an offline Project, where Startup requests can be created "
                   "manually, but Workbench will not fabricate requests. Review its source in "
                   "Device Repository or import a matching ESI description; no SDO, controller, "
                   "network, or physical hardware is accessed."));
    } else if (
        context.nodeKind == Core::WorkbenchNodeKind::Device && m_repositoryStartupHasErrors
        && !m_repositoryDeviceSupported) {
        m_summary->setText(Tr::tr(
            "ESI Startup requests are available for read-only preview, but validation errors "
            "and unsupported ESI structures mean this repository device cannot be added to "
            "an offline Project. Review the error and support details, then import a corrected "
            "matching ESI description through Device Repository; no SDO, controller, network, "
            "or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device && m_repositoryStartupHasErrors) {
        m_summary->setText(Tr::tr(
            "ESI Startup requests are available for read-only preview, but validation errors "
            "mean this repository device cannot be added to an offline Project. Review the "
            "error details below and import a corrected matching ESI description through "
            "Device Repository; no SDO, controller, network, or physical hardware is "
            "accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr("ESI Startup requests are available for read-only preview, but this repository "
                   "device contains unsupported ESI structures and cannot be added to an offline "
                   "Project. Review its support details in Device Repository; no SDO, controller, "
                   "network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        m_summary->setText(
            Tr::tr("ESI Startup requests. Their transition, object address, raw data, and order "
                   "can be inspected here. Add the device to an offline Project before editing; no "
                   "Startup request is sent from this read-only preview."));
    } else if (m_showingEsiDefaults) {
        m_summary->setText(
            Tr::tr(
                "ESI Startup defaults are shown but are not stored in the project. Store the "
                "defaults or create a request to enter the Undo/Redo history."));
    } else if (!isEmpty(m_configuration) && device) {
        m_summary->setText(
            Tr::tr(
                "Offline Startup request list with ESI reference. Enabled requests are applied "
                "in the displayed order for each EtherCAT state transition."));
    } else if (!isEmpty(m_configuration)) {
        m_summary->setText(
            Tr::tr(
                "Offline Startup request list. The matching ESI description is unavailable, "
                "but the persisted commands remain editable."));
    } else {
        m_summary->setText(
            Tr::tr("No Startup requests are configured. Use New to add an offline CoE request."));
    }

    m_restoreDefaults->setVisible(
        context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && device.has_value());
    m_restoreDefaults->setEnabled(m_editable && !isEmpty(m_esiDefaults));
    m_restoreDefaults->setText(
        m_showingEsiDefaults ? Tr::tr("Store ESI Defaults") : Tr::tr("Restore ESI Defaults"));
    updateTablePresentation();
    rebuildModel(
        canPreserveInlineEditor(stableContext), m_inlineEditorCommitInProgress);
}

std::optional<Data::StartupConfiguration> StartupPage::currentConfiguration(
    quint64 contextGeneration, const Data::NodeId &projectId, const Data::NodeId &slaveId) const
{
    if (contextGeneration != m_contextGeneration
        || m_context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave
        || m_context.projectId != projectId || m_context.nodeId != slaveId || !m_controller
        || !m_controller->projectService()) {
        return std::nullopt;
    }
    const std::optional<Data::ProjectSnapshot> project = m_controller->projectService()->project(
        projectId);
    if (!project || !project->valid)
        return std::nullopt;
    const auto slave = std::find_if(
        project->slaves.cbegin(), project->slaves.cend(), [&slaveId](const auto &candidate) {
            return candidate.id == slaveId;
        });
    if (slave == project->slaves.cend())
        return std::nullopt;
    return m_showingEsiDefaults ? m_configuration : slave->startup;
}

bool StartupPage::submitConfiguration(
    const Data::StartupConfiguration &configuration,
    const Data::NodeId &inlineParameterId,
    int inlineColumn,
    QString *inlineRejection)
{
    if (inlineRejection)
        inlineRejection->clear();
    const QList<Data::ConfigurationIssue> issues = Data::validateStartupConfiguration(configuration);
    const bool hasErrors = std::any_of(issues.cbegin(), issues.cend(), [](const auto &issue) {
        return issue.severity == Data::ConfigurationIssueSeverity::Error;
    });
    if (hasErrors) {
        if (inlineRejection) {
            const auto firstError = std::find_if(
                issues.cbegin(), issues.cend(), [](const Data::ConfigurationIssue &issue) {
                    return issue.severity == Data::ConfigurationIssueSeverity::Error;
                });
            *inlineRejection
                = firstError == issues.cend()
                      ? Tr::tr("Change not applied.")
                      : Tr::tr("Change not applied. %1").arg(firstError->message);
        } else {
            showValidation(issues, Tr::tr("Change not applied."));
        }
        return false;
    }
    if (!m_editable || !m_controller || !m_controller->projectService()) {
        const QString message
            = Tr::tr("Change not applied: this ESI catalogue page is read-only.");
        if (inlineRejection)
            *inlineRejection = message;
        else
            showValidation(issues, message);
        return false;
    }
    const bool inlineEditorCommit
        = m_inlineEditor && m_inlineEditorAuthority
          && m_inlineEditorAuthority->id == inlineParameterId
          && m_inlineEditorColumn == inlineColumn;
    std::optional<Data::StartupParameterConfiguration> submittedAuthority;
    if (inlineEditorCommit) {
        const auto submitted = std::find_if(
            configuration.parameters.cbegin(),
            configuration.parameters.cend(),
            [&inlineParameterId](const auto &parameter) {
                return parameter.id == inlineParameterId;
            });
        if (submitted != configuration.parameters.cend())
            submittedAuthority = *submitted;
    }
    const QScopedValueRollback commitGuard(m_inlineEditorCommitInProgress, inlineEditorCommit);
    const QScopedValueRollback submittedGuard(
        m_inlineEditorSubmittedAuthority, submittedAuthority);
    const Utils::Result<> result
        = m_controller->projectService()
              ->setStartupConfiguration(m_context.projectId, m_context.nodeId, configuration);
    if (!result) {
        const QString message = Tr::tr("Change not applied: %1").arg(result.error());
        if (inlineRejection)
            *inlineRejection = message;
        else
            showValidation(issues, message);
        return false;
    }

    m_configuration = configuration;
    m_showingEsiDefaults = false;
    m_restoreDefaults->setText(Tr::tr("Restore ESI Defaults"));
    rebuildModel(
        inlineEditorCommit && canPreserveInlineEditor(true), inlineEditorCommit);
    return true;
}

void StartupPage::trackInlineEditor(
    QWidget *editor, const Data::StartupParameterConfiguration &parameter, int column)
{
    m_inlineEditor = editor;
    m_inlineEditorAuthority = parameter;
    m_inlineEditorColumn = column;
    m_inlineEditorShowingEsiDefaults = m_showingEsiDefaults;
    m_inlineEditorMetadataDirty = false;
    const quint64 generation = ++m_inlineEditorGeneration;
    connect(editor, &QObject::destroyed, this, [this, generation, parameterId = parameter.id, column] {
        if (generation != m_inlineEditorGeneration)
            return;
        if (m_inlineEditorMetadataDirty)
            m_model->notifyMetadataChanged(parameterId, column);
        m_inlineEditor = nullptr;
        m_inlineEditorAuthority.reset();
        m_inlineEditorColumn = -1;
        m_inlineEditorMetadataDirty = false;
    });
}

void StartupPage::showEditRejection(
    const Data::NodeId &parameterId,
    int column,
    const QString &message,
    bool inlineEditorSubmission)
{
    if (inlineEditorSubmission
        && (!m_inlineEditor || !m_inlineEditorAuthority
            || m_inlineEditorAuthority->id != parameterId || m_inlineEditorColumn != column)) {
        return;
    }
    if (!inlineEditorSubmission && (column != StartupTableModel::Enabled || !m_editable)) {
        return;
    }

    m_inlineEditRejectionActive = true;
    m_validation->setType(Utils::InfoLabelType::Error);
    m_validation->setText(message);
    m_validation->setAccessibleDescription(message);
    m_validation->setAdditionalToolTip(message);
    m_validation->setToolTip(message);
#if QT_CONFIG(accessibility)
    QAccessibleAnnouncementEvent announcement(m_validation, message);
    announcement.setPoliteness(QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&announcement);
#endif
}

void StartupPage::clearInlineEditRejection()
{
    if (m_inlineEditRejectionActive)
        showValidation(Data::validateStartupConfiguration(m_configuration));
}

bool StartupPage::canPreserveInlineEditor(bool stableContext) const
{
    if (!stableContext || !m_editable || !m_inlineEditor || !m_inlineEditorAuthority
        || (!m_inlineEditorCommitInProgress
            && m_inlineEditorShowingEsiDefaults != m_showingEsiDefaults)) {
        return false;
    }
    const auto parameter = std::find_if(
        m_configuration.parameters.cbegin(),
        m_configuration.parameters.cend(),
        [this](const auto &candidate) { return candidate.id == m_inlineEditorAuthority->id; });
    if (parameter == m_configuration.parameters.cend() || isFixed(*parameter))
        return false;
    const Data::StartupParameterConfiguration &authority
        = m_inlineEditorCommitInProgress && m_inlineEditorSubmittedAuthority
              ? *m_inlineEditorSubmittedAuthority
              : *m_inlineEditorAuthority;
    return sameInlineEditorAuthority(authority, *parameter, m_inlineEditorColumn);
}

void StartupPage::rebuildModel(bool preserveInlineEditor, bool notifyPreservedColumn)
{
    if (!preserveInlineEditor && m_inlineEditor) {
        m_inlineEditorMetadataDirty = false;
        static_cast<StartupTableView *>(m_table)->discardInlineEditor(m_inlineEditor);
    }
    m_rebuilding = true;
    bool preservedMetadataChanged = false;
    const bool modelReset = m_model->setConfiguration(
        m_configuration,
        m_editable,
        preserveInlineEditor ? m_inlineEditorAuthority->id : Data::NodeId(),
        preserveInlineEditor ? m_inlineEditorColumn : -1,
        notifyPreservedColumn,
        &preservedMetadataChanged);
    if (preserveInlineEditor)
        m_inlineEditorMetadataDirty |= preservedMetadataChanged;
    if (modelReset) {
        int row = rowForId(m_model, m_selectedParameterId);
        if (row < 0 && m_model->rowCount() > 0)
            row = 0;
        m_selectedParameterId = row >= 0
                                    ? m_model->index(row, 0)
                                          .data(StableIdRole)
                                          .value<Data::NodeId>()
                                    : Data::NodeId();
        m_table->setCurrentIndex(m_model->index(row, 0));
    }
    m_rebuilding = false;
    updateButtonState();
    showValidation(Data::validateStartupConfiguration(m_configuration));
}

void StartupPage::updateButtonState()
{
    const int row = rowForId(m_model, m_selectedParameterId);
    const Data::StartupParameterConfiguration *parameter = m_model->parameterAt(row);
    const bool canChange = m_editable && parameter && !isFixed(*parameter);
    const Data::StartupParameterConfiguration *previous = m_model->parameterAt(row - 1);
    const Data::StartupParameterConfiguration *next = m_model->parameterAt(row + 1);
    m_new->setEnabled(m_editable);
    m_edit->setEnabled(canChange);
    m_delete->setEnabled(canChange);
    m_moveUp->setEnabled(canChange && previous && !isFixed(*previous));
    m_moveDown->setEnabled(canChange && next && !isFixed(*next));
}

void StartupPage::updateTablePresentation()
{
    const bool repositoryDeviceMissing = m_context.nodeKind == Core::WorkbenchNodeKind::Device
                                         && !m_repositoryDeviceAvailable;
    const bool repositoryStartupEmpty = m_context.nodeKind == Core::WorkbenchNodeKind::Device
                                        && m_repositoryDeviceAvailable
                                        && !m_repositoryStartupAvailable;
    const bool repositoryStartupEmptyUnsupported = repositoryStartupEmpty
                                                   && !m_repositoryDeviceSupported;
    const bool repositoryStartupPreview = m_context.nodeKind == Core::WorkbenchNodeKind::Device
                                          && m_repositoryDeviceAvailable
                                          && m_repositoryStartupAvailable;
    const bool repositoryStartupPreviewUnsupported = repositoryStartupPreview
                                                     && !m_repositoryDeviceSupported;
    const bool repositoryStartupPreviewInvalid = repositoryStartupPreview
                                                 && m_repositoryStartupHasErrors;

    QString contextDescription;
    if (repositoryDeviceMissing) {
        contextDescription = Tr::tr(
            "The ESI device description is unavailable, so no Startup requests can be shown. "
            "Return to Device Repository and select an available device. This read-only page "
            "does not send an SDO request or access a controller, network, or physical hardware.");
    } else if (repositoryStartupEmptyUnsupported) {
        contextDescription = Tr::tr(
            "No ESI Startup request is available. This repository device contains unsupported "
            "ESI structures and cannot be added to an offline Project. Review its support details "
            "in Device Repository. This read-only page will not fabricate requests and does not "
            "send an SDO request or access a controller, network, or physical hardware.");
    } else if (repositoryStartupEmpty) {
        contextDescription = Tr::tr(
            "No ESI Startup request is available for this repository device. This read-only page "
            "will not fabricate requests. Add the device to an offline Project to create Startup "
            "requests manually, or review its source in Device Repository; no SDO request is sent "
            "and no controller, network, or physical hardware is accessed.");
    } else if (repositoryStartupPreviewInvalid && repositoryStartupPreviewUnsupported) {
        contextDescription = Tr::tr(
            "This is a read-only preview of imported ESI Startup requests. The configuration has "
            "validation errors and the repository device contains unsupported ESI structures, so "
            "it cannot be added to an offline Project. Review the error and support details, then "
            "import a corrected matching ESI description through Device Repository. The preview "
            "does not modify a Project, send an SDO request, or access a controller, network, or "
            "physical hardware.");
    } else if (repositoryStartupPreviewInvalid) {
        contextDescription = Tr::tr(
            "This is a read-only preview of imported ESI Startup requests. The configuration has "
            "validation errors and cannot be added to an offline Project. Review the error details "
            "and import a corrected matching ESI description through Device Repository. The "
            "preview does not modify a Project, send an SDO request, or access a controller, "
            "network, or physical hardware.");
    } else if (repositoryStartupPreviewUnsupported) {
        contextDescription = Tr::tr(
            "This is a read-only preview of imported ESI Startup requests. The repository device "
            "contains unsupported ESI structures and cannot be added to an offline Project. Review "
            "its support details in Device Repository. The preview does not modify a Project, send "
            "an SDO request, or access a controller, network, or physical hardware.");
    } else if (repositoryStartupPreview) {
        contextDescription = Tr::tr(
            "This is a read-only offline preview of imported ESI Startup requests. Add the device "
            "to an offline Project before editing. Selecting rows changes only this page "
            "presentation; it does not modify a Project, send an SDO request, or access a "
            "controller, network, or physical hardware.");
    }

    QString description = m_table->property(baseDescriptionProperty).toString();
    if (!contextDescription.isEmpty())
        description += ' ' + contextDescription;
    m_table->setAccessibleDescription(description);
    m_table->setToolTip(contextDescription.isEmpty() ? QString() : description);
}

void StartupPage::addParameter()
{
    if (!m_editable)
        return;
    int nextOrder = 0;
    for (const Data::StartupParameterConfiguration &parameter : m_configuration.parameters)
        nextOrder = qMax(nextOrder, parameter.order + 1);
    Data::StartupParameterConfiguration parameter{
        Data::NodeId::create(),
        true,
        nextOrder,
        "PS",
        0x2000,
        0,
        Data::EtherCATDataType::Unknown,
        {},
        QByteArray::fromHex("00"),
        {}};
    const quint64 contextGeneration = m_contextGeneration;
    const Data::NodeId projectId = m_context.projectId;
    const Data::NodeId slaveId = m_context.nodeId;
    const Data::NodeId parameterId = parameter.id;
    auto dialog = new StartupParameterDialog(parameter, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(
        dialog,
        &QDialog::finished,
        this,
        [this, dialog, contextGeneration, projectId, slaveId, parameterId](int result) {
            if (result != QDialog::Accepted)
                return;
            std::optional<Data::StartupConfiguration> candidate
                = currentConfiguration(contextGeneration, projectId, slaveId);
            if (!candidate)
                return;
            Data::StartupParameterConfiguration accepted = dialog->parameter();
            accepted.id = parameterId;
            accepted.order = 0;
            for (const Data::StartupParameterConfiguration &existing : candidate->parameters)
                accepted.order = qMax(accepted.order, existing.order + 1);
            candidate->parameters.append(accepted);
            const Data::NodeId previousSelection = m_selectedParameterId;
            m_selectedParameterId = accepted.id;
            if (!submitConfiguration(*candidate))
                m_selectedParameterId = previousSelection;
        });
    dialog->open();
}

void StartupPage::editParameter()
{
    const int row = rowForId(m_model, m_selectedParameterId);
    const Data::StartupParameterConfiguration *selected = m_model->parameterAt(row);
    if (!m_editable || !selected || isFixed(*selected))
        return;
    const quint64 contextGeneration = m_contextGeneration;
    const Data::NodeId projectId = m_context.projectId;
    const Data::NodeId slaveId = m_context.nodeId;
    const Data::NodeId parameterId = selected->id;
    auto dialog = new StartupParameterDialog(*selected, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(
        dialog,
        &QDialog::finished,
        this,
        [this, dialog, contextGeneration, projectId, slaveId, parameterId](int result) {
            if (result != QDialog::Accepted)
                return;
            std::optional<Data::StartupConfiguration> candidate
                = currentConfiguration(contextGeneration, projectId, slaveId);
            if (!candidate)
                return;
            const auto found = std::find_if(
                candidate->parameters.begin(),
                candidate->parameters.end(),
                [&parameterId](const auto &entry) { return entry.id == parameterId; });
            if (found == candidate->parameters.end() || isFixed(*found))
                return;
            Data::StartupParameterConfiguration accepted = dialog->parameter();
            accepted.id = parameterId;
            accepted.order = found->order;
            *found = accepted;
            const Data::NodeId previousSelection = m_selectedParameterId;
            m_selectedParameterId = parameterId;
            if (!submitConfiguration(*candidate))
                m_selectedParameterId = previousSelection;
        });
    dialog->open();
}

void StartupPage::deleteParameter()
{
    const int row = rowForId(m_model, m_selectedParameterId);
    const Data::StartupParameterConfiguration *selected = m_model->parameterAt(row);
    if (!m_editable || !selected || isFixed(*selected))
        return;
    const quint64 contextGeneration = m_contextGeneration;
    const Data::NodeId projectId = m_context.projectId;
    const Data::NodeId slaveId = m_context.nodeId;
    const Data::NodeId parameterId = selected->id;
    auto messageBox = new QMessageBox(
        QMessageBox::Question,
        Tr::tr("Delete Startup Request"),
        Tr::tr("Delete the selected offline Startup request?"),
        QMessageBox::Yes | QMessageBox::No,
        this);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setDefaultButton(QMessageBox::No);
    connect(
        messageBox,
        &QDialog::finished,
        this,
        [this, contextGeneration, projectId, slaveId, parameterId](int result) {
            if (result != QMessageBox::Yes)
                return;
            std::optional<Data::StartupConfiguration> candidate
                = currentConfiguration(contextGeneration, projectId, slaveId);
            if (!candidate)
                return;
            QList<Data::StartupParameterConfiguration> ordered = candidate->parameters;
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto &left, const auto &right) {
                return left.order < right.order;
            });
            const auto selected
                = std::find_if(ordered.cbegin(), ordered.cend(), [&parameterId](const auto &entry) {
                      return entry.id == parameterId;
                  });
            if (selected == ordered.cend() || isFixed(*selected))
                return;
            const qsizetype selectedPosition = std::distance(ordered.cbegin(), selected);
            const Data::NodeId nextSelection = selectedPosition + 1 < ordered.size()
                                                   ? ordered.at(selectedPosition + 1).id
                                                   : (selectedPosition > 0
                                                          ? ordered.at(selectedPosition - 1).id
                                                          : Data::NodeId());
            candidate->parameters.removeIf(
                [&parameterId](const auto &entry) { return entry.id == parameterId; });
            int nextOrder = 0;
            for (const Data::StartupParameterConfiguration &parameter : std::as_const(ordered)) {
                if (parameter.id == parameterId)
                    continue;
                const auto found = std::find_if(
                    candidate->parameters.begin(),
                    candidate->parameters.end(),
                    [&parameter](const auto &entry) { return entry.id == parameter.id; });
                if (found != candidate->parameters.end())
                    found->order = nextOrder++;
            }
            const Data::NodeId previousSelection = m_selectedParameterId;
            m_selectedParameterId = nextSelection;
            if (!submitConfiguration(*candidate))
                m_selectedParameterId = previousSelection;
        });
    messageBox->open();
}

void StartupPage::moveParameter(int distance)
{
    const int row = rowForId(m_model, m_selectedParameterId);
    const int targetRow = row + distance;
    const Data::StartupParameterConfiguration *selected = m_model->parameterAt(row);
    const Data::StartupParameterConfiguration *target = m_model->parameterAt(targetRow);
    if (!m_editable || !selected || isFixed(*selected) || targetRow < 0
        || targetRow >= m_model->rowCount() || !target || isFixed(*target)) {
        return;
    }
    QList<Data::NodeId> order = m_model->orderedIds();
    order.swapItemsAt(row, targetRow);
    Data::StartupConfiguration candidate = m_configuration;
    for (int position = 0; position < order.size(); ++position) {
        const Data::NodeId id = order.at(position);
        const auto found = std::find_if(
            candidate.parameters.begin(), candidate.parameters.end(), [&id](const auto &entry) {
                return entry.id == id;
            });
        if (found != candidate.parameters.end())
            found->order = position;
    }
    submitConfiguration(candidate);
}

void StartupPage::showValidation(const QList<Data::ConfigurationIssue> &issues, const QString &prefix)
{
    m_inlineEditRejectionActive = false;
    int errorCount = 0;
    int warningCount = 0;
    QStringList details;
    for (const Data::ConfigurationIssue &issue : issues) {
        details.append(issue.message);
        if (issue.severity == Data::ConfigurationIssueSeverity::Error)
            ++errorCount;
        else if (issue.severity == Data::ConfigurationIssueSeverity::Warning)
            ++warningCount;
    }

    QString text = prefix;
    if (!text.isEmpty() && !details.isEmpty())
        text += ' ';
    const bool repositoryDeviceMissing = m_context.nodeKind == Core::WorkbenchNodeKind::Device
                                         && !m_repositoryDeviceAvailable;
    const bool repositoryStartupEmpty = m_context.nodeKind == Core::WorkbenchNodeKind::Device
                                        && m_repositoryDeviceAvailable
                                        && !m_repositoryStartupAvailable;
    const bool repositoryStartupEmptyUnsupported = repositoryStartupEmpty
                                                   && !m_repositoryDeviceSupported;
    const bool repositoryStartupPreviewUnsupported = m_context.nodeKind
                                                         == Core::WorkbenchNodeKind::Device
                                                     && m_repositoryDeviceAvailable
                                                     && m_repositoryStartupAvailable
                                                     && !m_repositoryDeviceSupported;
    const bool repositoryStartupPreviewInvalid = m_context.nodeKind
                                                     == Core::WorkbenchNodeKind::Device
                                                 && m_repositoryDeviceAvailable
                                                 && m_repositoryStartupAvailable && errorCount > 0;
    QString repositoryPreviewNotice;
    if (repositoryStartupPreviewInvalid && repositoryStartupPreviewUnsupported) {
        repositoryPreviewNotice = Tr::tr(
            "This Startup configuration has validation errors and the Device contains unsupported "
            "ESI structures, so it cannot be added to an offline Project. Review the error and "
            "support details, then import a corrected matching ESI description through Device "
            "Repository.");
    } else if (repositoryStartupPreviewInvalid) {
        repositoryPreviewNotice = Tr::tr(
            "This Startup configuration cannot be added to an offline Project. Import a corrected "
            "matching ESI description through Device Repository.");
    } else if (repositoryStartupPreviewUnsupported) {
        repositoryPreviewNotice = Tr::tr(
            "This Device contains unsupported ESI structures and cannot be added to an offline "
            "Project. Review its support details in Device Repository.");
    }
    if (repositoryDeviceMissing) {
        m_validation->setType(Utils::InfoLabelType::Warning);
        text += Tr::tr(
            "The ESI device description is unavailable, so no Startup requests can be shown.");
    } else if (repositoryStartupEmptyUnsupported) {
        m_validation->setType(Utils::InfoLabelType::Warning);
        text += Tr::tr(
            "No ESI Startup request is available. This device contains unsupported ESI structures "
            "and cannot be added to an offline Project. Review its support details in Device "
            "Repository.");
    } else if (repositoryStartupEmpty) {
        m_validation->setType(Utils::InfoLabelType::Information);
        text += Tr::tr(
            "No ESI Startup request is available to preview; Workbench will not fabricate "
            "requests.");
    } else if (errorCount > 0) {
        m_validation->setType(Utils::InfoLabelType::Error);
        text += Tr::tr("%n Startup configuration error(s).", nullptr, errorCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
        if (!repositoryPreviewNotice.isEmpty())
            text += ' ' + repositoryPreviewNotice;
    } else if (warningCount > 0) {
        m_validation->setType(Utils::InfoLabelType::Warning);
        text += Tr::tr("%n Startup configuration warning(s).", nullptr, warningCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
        if (!repositoryPreviewNotice.isEmpty())
            text += ' ' + repositoryPreviewNotice;
    } else if (repositoryStartupPreviewUnsupported) {
        m_validation->setType(Utils::InfoLabelType::Warning);
        text += Tr::tr("These Startup requests are available only for read-only preview. ");
        text += repositoryPreviewNotice;
    } else {
        m_validation->setType(Utils::InfoLabelType::Ok);
        text
            += Tr::tr("Startup configuration is valid. %n request(s).", nullptr, m_model->rowCount());
    }
    m_validation->setText(text);
    m_validation->setAccessibleDescription(text);
    m_validation->setAdditionalToolTip(details.join('\n'));
    m_validation->setToolTip(details.join('\n'));
}

} // namespace EtherCAT::Workbench::Internal
