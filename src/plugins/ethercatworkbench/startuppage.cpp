// Copyright (C) 2026 Kvell

#include "startuppage.h"

#include "esiconfigurationfactory.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#include <QAbstractItemView>
#include <QAbstractTableModel>
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
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <optional>

namespace EtherCAT::Workbench::Internal {

enum TableRole {
    StableIdRole = Qt::UserRole + 1,
    DataTypeRole,
};

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

static std::optional<QByteArray> parseRawValue(const QVariant &value)
{
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
    if (compact.isEmpty() || compact.size() % 2 != 0)
        return std::nullopt;
    static const QString hexadecimal = "0123456789abcdefABCDEF";
    if (std::any_of(compact.cbegin(), compact.cend(), [](QChar character) {
            return !hexadecimal.contains(character);
        })) {
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
        if (role != Qt::EditRole && role != Qt::CheckStateRole
            && !(role == DataTypeRole && index.column() == Type)) {
            return false;
        }

        Data::StartupConfiguration candidate = m_configuration;
        const auto found = std::find_if(
            candidate.parameters.begin(),
            candidate.parameters.end(),
            [&current](const auto &entry) { return entry.id == current.id; });
        if (found == candidate.parameters.end())
            return false;

        quint64 unsignedValue = 0;
        switch (index.column()) {
        case Enabled:
            if (role != Qt::CheckStateRole)
                return false;
            found->enabled = value.toInt() == Qt::Checked;
            break;
        case Order:
            if (!parseUnsignedValue(value, std::numeric_limits<int>::max(), &unsignedValue))
                return false;
            found->order = int(unsignedValue);
            break;
        case Transition:
            found->transition = value.toString().trimmed();
            if (found->transition.startsWith('<') || found->transition.endsWith('>')) {
                if (m_page) {
                    m_page->showValidation(
                        {{Data::ConfigurationIssueCode::MissingStartupTransition,
                          Data::ConfigurationIssueSeverity::Error,
                          current.id,
                          "transition",
                          Tr::tr(
                              "Angle brackets are reserved for fixed requests imported from "
                              "ESI.")}},
                        Tr::tr("Change not applied."));
                }
                return false;
            }
            break;
        case Protocol:
            return false;
        case Index:
            if (!parseUnsignedValue(value, std::numeric_limits<quint16>::max(), &unsignedValue))
                return false;
            found->index = quint16(unsignedValue);
            break;
        case Subindex:
            if (!parseUnsignedValue(value, std::numeric_limits<quint8>::max(), &unsignedValue))
                return false;
            found->subIndex = quint8(unsignedValue);
            break;
        case Type: {
            bool ok = false;
            const int typeValue = value.toInt(&ok);
            if (!ok || typeValue < int(Data::EtherCATDataType::Unknown)
                || typeValue > int(Data::EtherCATDataType::OctetString)) {
                return false;
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
            const std::optional<QByteArray> rawValue = parseRawValue(value);
            if (!rawValue) {
                if (m_page) {
                    m_page->showValidation(
                        {{Data::ConfigurationIssueCode::InvalidStartupValueSize,
                          Data::ConfigurationIssueSeverity::Error,
                          current.id,
                          "rawValue",
                          Tr::tr("Startup raw value must contain an even number of hex digits.")}},
                        Tr::tr("Change not applied."));
                }
                return false;
            }
            found->rawValue = *rawValue;
            break;
        }
        case Comment:
            found->comment = value.toString().trimmed();
            break;
        default:
            return false;
        }
        return m_page && m_page->submitConfiguration(candidate);
    }

    void setConfiguration(const Data::StartupConfiguration &configuration, bool editable)
    {
        beginResetModel();
        m_configuration = configuration;
        m_parameters = configuration.parameters;
        std::stable_sort(
            m_parameters.begin(), m_parameters.end(), [](const auto &left, const auto &right) {
                return left.order < right.order;
            });
        m_editable = editable;
        endResetModel();
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

private:
    StartupPage *m_page = nullptr;
    Data::StartupConfiguration m_configuration;
    QList<Data::StartupParameterConfiguration> m_parameters;
    bool m_editable = false;
};

class DataTypeDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const final
    {
        auto editor = new QComboBox(parent);
        for (Data::EtherCATDataType type : dataTypes())
            editor->addItem(dataTypeName(type), int(type));
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const final
    {
        if (auto comboBox = qobject_cast<QComboBox *>(editor))
            comboBox->setCurrentIndex(comboBox->findData(index.data(DataTypeRole)));
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const final
    {
        if (auto comboBox = qobject_cast<QComboBox *>(editor))
            model->setData(index, comboBox->currentData(), DataTypeRole);
    }

    void updateEditorGeometry(
        QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const final
    {
        editor->setGeometry(option.rect);
    }
};

class TransitionDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const final
    {
        auto editor = new QComboBox(parent);
        editor->setEditable(true);
        editor->addItems({"PS", "SO", "IP", "OS", "SP", "PI"});
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const final
    {
        if (auto comboBox = qobject_cast<QComboBox *>(editor))
            comboBox->setCurrentText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const final
    {
        if (auto comboBox = qobject_cast<QComboBox *>(editor))
            model->setData(index, comboBox->currentText().trimmed());
    }

    void updateEditorGeometry(
        QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const final
    {
        editor->setGeometry(option.rect);
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
        m_validation->setType(Utils::InfoLabel::Error);
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
    , m_table(new QTableView(this))
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
    m_validation->setElideMode(Qt::ElideNone);
    m_validation->setWordWrap(true);
    m_restoreDefaults->setObjectName("EtherCATStartupRestoreDefaults");
    m_table->setObjectName("EtherCATStartupTable");
    m_table->setAccessibleName(Tr::tr("Startup requests"));
    m_table->setAccessibleDescription(
        Tr::tr(
            "Ordered offline CoE Startup requests with transition, object address, data, and "
            "comment details."));
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
    m_table->setItemDelegateForColumn(StartupTableModel::Type, new DataTypeDelegate(m_table));
    m_table->setItemDelegateForColumn(StartupTableModel::Transition, new TransitionDelegate(m_table));

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

void StartupPage::setContext(const Core::PropertyPageContext &context)
{
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
    if (isEmpty(m_configuration) && !isEmpty(m_esiDefaults)) {
        m_configuration = m_esiDefaults;
        m_showingEsiDefaults = true;
    }

    if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        m_summary->setText(
            Tr::tr(
                "ESI Startup requests. Their transition, object address, raw data, and order "
                "can be inspected here. Add the device to an offline project before editing."));
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
    rebuildModel();
}

bool StartupPage::submitConfiguration(const Data::StartupConfiguration &configuration)
{
    const QList<Data::ConfigurationIssue> issues = Data::validateStartupConfiguration(configuration);
    const bool hasErrors = std::any_of(issues.cbegin(), issues.cend(), [](const auto &issue) {
        return issue.severity == Data::ConfigurationIssueSeverity::Error;
    });
    if (hasErrors) {
        showValidation(issues, Tr::tr("Change not applied."));
        return false;
    }
    if (!m_editable || !m_controller || !m_controller->projectService()) {
        showValidation(issues, Tr::tr("Change not applied: this ESI catalogue page is read-only."));
        return false;
    }
    const Utils::Result<> result
        = m_controller->projectService()
              ->setStartupConfiguration(m_context.projectId, m_context.nodeId, configuration);
    if (!result) {
        showValidation(issues, Tr::tr("Change not applied: %1").arg(result.error()));
        return false;
    }

    m_configuration = configuration;
    m_showingEsiDefaults = false;
    m_restoreDefaults->setText(Tr::tr("Restore ESI Defaults"));
    rebuildModel();
    return true;
}

void StartupPage::rebuildModel()
{
    m_rebuilding = true;
    m_model->setConfiguration(m_configuration, m_editable);
    int row = rowForId(m_model, m_selectedParameterId);
    if (row < 0 && m_model->rowCount() > 0)
        row = 0;
    m_selectedParameterId = row >= 0
                                ? m_model->index(row, 0).data(StableIdRole).value<Data::NodeId>()
                                : Data::NodeId();
    m_table->setCurrentIndex(m_model->index(row, 0));
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
    StartupParameterDialog dialog(parameter, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    parameter = dialog.parameter();
    Data::StartupConfiguration candidate = m_configuration;
    candidate.parameters.append(parameter);
    m_selectedParameterId = parameter.id;
    submitConfiguration(candidate);
}

void StartupPage::editParameter()
{
    const int row = rowForId(m_model, m_selectedParameterId);
    const Data::StartupParameterConfiguration *selected = m_model->parameterAt(row);
    if (!m_editable || !selected || isFixed(*selected))
        return;
    StartupParameterDialog dialog(*selected, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Data::StartupParameterConfiguration parameter = dialog.parameter();
    Data::StartupConfiguration candidate = m_configuration;
    const auto found = std::find_if(
        candidate.parameters.begin(), candidate.parameters.end(), [this](const auto &entry) {
            return entry.id == m_selectedParameterId;
        });
    if (found == candidate.parameters.end())
        return;
    *found = parameter;
    submitConfiguration(candidate);
}

void StartupPage::deleteParameter()
{
    const int row = rowForId(m_model, m_selectedParameterId);
    const Data::StartupParameterConfiguration *selected = m_model->parameterAt(row);
    if (!m_editable || !selected || isFixed(*selected))
        return;
    if (QMessageBox::question(
            this,
            Tr::tr("Delete Startup Request"),
            Tr::tr("Delete the selected offline Startup request?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    const QList<Data::NodeId> order = m_model->orderedIds();
    Data::StartupConfiguration candidate = m_configuration;
    candidate.parameters.removeIf(
        [this](const auto &entry) { return entry.id == m_selectedParameterId; });
    int nextOrder = 0;
    for (const Data::NodeId &id : order) {
        const auto found = std::find_if(
            candidate.parameters.begin(), candidate.parameters.end(), [&id](const auto &entry) {
                return entry.id == id;
            });
        if (found != candidate.parameters.end())
            found->order = nextOrder++;
    }
    m_selectedParameterId
        = row + 1 < m_model->rowCount()
              ? m_model->index(row + 1, 0).data(StableIdRole).value<Data::NodeId>()
              : (row > 0 ? m_model->index(row - 1, 0).data(StableIdRole).value<Data::NodeId>()
                         : Data::NodeId());
    submitConfiguration(candidate);
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
    if (errorCount > 0) {
        m_validation->setType(Utils::InfoLabel::Error);
        text += Tr::tr("%n Startup configuration error(s).", nullptr, errorCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
    } else if (warningCount > 0) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr("%n Startup configuration warning(s).", nullptr, warningCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
    } else {
        m_validation->setType(Utils::InfoLabel::Ok);
        text
            += Tr::tr("Startup configuration is valid. %n request(s).", nullptr, m_model->rowCount());
    }
    m_validation->setText(text);
    m_validation->setToolTip(details.join('\n'));
}

} // namespace EtherCAT::Workbench::Internal
