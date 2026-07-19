// Copyright (C) 2026 Kvell

#include "esideviceselectiondialog.h"

#include "ethercatworkbenchtr.h"

#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

enum DeviceColumn {
    DeviceColumnName,
    DeviceColumnType,
    DeviceColumnVendor,
    DeviceColumnProduct,
    DeviceColumnRevision,
    DeviceColumnGroup,
    DeviceColumnSupport,
    DeviceColumnCount,
};

enum DeviceRole {
    DeviceIdRole = Qt::UserRole + 1,
    DeviceSupportedRole,
    DeviceLatestRevisionRole,
};

class EsiDeviceFilterModel final : public QSortFilterProxyModel
{
public:
    explicit EsiDeviceFilterModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {}

    void setShowPreviousRevisions(bool show)
    {
        if (m_showPreviousRevisions == show)
            return;
        m_showPreviousRevisions = show;
        invalidateFilter();
    }

private:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        const QModelIndex source = sourceModel()->index(sourceRow, DeviceColumnName, sourceParent);
        if (!m_showPreviousRevisions && !source.data(DeviceLatestRevisionRole).toBool())
            return false;
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

    bool m_showPreviousRevisions = false;
};

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static quint64 productKey(const Data::DeviceIdentity &identity)
{
    return (quint64(identity.vendorId) << 32) | identity.productCode;
}

static QStringList deviceColumnLabels()
{
    return {Tr::tr("Device"),
            Tr::tr("Type"),
            Tr::tr("Vendor ID"),
            Tr::tr("Product Code"),
            Tr::tr("Revision"),
            Tr::tr("Group"),
            Tr::tr("Support")};
}

static QString deviceCellDescription(
    const QString &columnLabel, const QString &value, bool supported)
{
    const QString operation
        = supported
              ? Tr::tr("Supported ESI description; this device can be appended to the selected "
                       "offline EtherCAT Master. No controller or network is accessed.")
              : Tr::tr("Limited ESI description; this device cannot be appended to the selected "
                       "offline EtherCAT Master until its unsupported structures are resolved. "
                       "No controller or network is accessed.");
    return Tr::tr("%1: %2. %3").arg(columnLabel, value, operation);
}

static QList<QStandardItem *> deviceRow(
    const Data::DeviceSummary &device, bool latestRevision)
{
    const QString support = device.supported ? Tr::tr("Supported") : Tr::tr("Limited");
    const QString name = device.name.isEmpty() ? device.typeName : device.name;
    QList<QStandardItem *> row = {
        new QStandardItem(name),
        new QStandardItem(device.typeName),
        new QStandardItem(hexValue(device.identity.vendorId, 8)),
        new QStandardItem(hexValue(device.identity.productCode, 8)),
        new QStandardItem(hexValue(device.identity.revisionNumber, 8)),
        new QStandardItem(device.group),
        new QStandardItem(support),
    };
    const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const QStringList columnLabels = deviceColumnLabels();
    for (int column = 0; column < row.size(); ++column) {
        QStandardItem *item = row.at(column);
        const QString description
            = deviceCellDescription(columnLabels.at(column), item->text(), device.supported);
        item->setFlags(flags);
        item->setAccessibleText(item->text());
        item->setAccessibleDescription(description);
        item->setToolTip(description);
    }
    row.first()->setIcon(
        device.supported ? Utils::Icons::OK.icon() : Utils::Icons::WARNING.icon());
    row.first()->setData(device.id.toString(), DeviceIdRole);
    row.first()->setData(device.supported, DeviceSupportedRole);
    row.first()->setData(latestRevision, DeviceLatestRevisionRole);
    return row;
}

EsiDeviceSelectionDialog::EsiDeviceSelectionDialog(
    const QList<Data::DeviceSummary> &devices, QWidget *parent)
    : QDialog(parent)
    , m_model(new QStandardItemModel(0, DeviceColumnCount, this))
    , m_proxyModel(new EsiDeviceFilterModel(this))
    , m_filter(new QLineEdit(this))
    , m_extendedInformation(new QCheckBox(Tr::tr("Extended Information"), this))
    , m_showPrevious(new QCheckBox(Tr::tr("Show Previous Revisions"), this))
    , m_tree(new QTreeView(this))
    , m_status(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_addButton(m_buttons->button(QDialogButtonBox::Ok))
{
    setObjectName("EtherCATEsiDeviceSelectionDialog");
    setWindowTitle(Tr::tr("Add EtherCAT Device"));
    setAccessibleName(Tr::tr("Add EtherCAT device from the ESI catalogue"));

    auto description = new QLabel(
        Tr::tr("Select an imported ESI device to append to the selected offline EtherCAT "
               "Master. No controller or network is accessed."),
        this);
    description->setObjectName("EtherCATEsiDeviceSelectionDescription");
    description->setAccessibleName(Tr::tr("Device selection description"));
    description->setWordWrap(true);
    description->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_filter->setObjectName("EtherCATEsiDeviceSelectionFilter");
    m_filter->setAccessibleName(Tr::tr("Filter ESI devices"));
    m_filter->setPlaceholderText(Tr::tr("Search name, type, identity, group, or support"));
    m_filter->setClearButtonEnabled(true);

    m_extendedInformation->setObjectName("EtherCATEsiDeviceSelectionExtendedInformation");
    m_extendedInformation->setAccessibleDescription(
        Tr::tr("Show Vendor ID, Product Code, Revision, and Group columns."));
    m_showPrevious->setObjectName("EtherCATEsiDeviceSelectionShowPrevious");
    m_showPrevious->setAccessibleDescription(
        Tr::tr("Show every imported revision instead of only the highest revision per product."));

    m_model->setHorizontalHeaderLabels(deviceColumnLabels());
    QHash<quint64, quint32> highestRevision;
    for (const Data::DeviceSummary &device : devices) {
        const quint64 key = productKey(device.identity);
        highestRevision.insert(
            key, qMax(highestRevision.value(key), device.identity.revisionNumber));
    }
    for (const Data::DeviceSummary &device : devices) {
        m_model->appendRow(deviceRow(
            device,
            device.identity.revisionNumber == highestRevision.value(productKey(device.identity))));
    }

    auto proxy = static_cast<EsiDeviceFilterModel *>(m_proxyModel);
    proxy->setSourceModel(m_model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    proxy->setDynamicSortFilter(true);

    m_tree->setObjectName("EtherCATEsiDeviceSelectionTree");
    m_tree->setAccessibleName(Tr::tr("Available ESI devices"));
    m_tree->setAccessibleDescription(
        Tr::tr("Imported EtherCAT devices available for offline configuration."));
    m_tree->setModel(m_proxyModel);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(DeviceColumnName, Qt::AscendingOrder);
    m_tree->header()->setSectionResizeMode(DeviceColumnName, QHeaderView::Stretch);
    for (int column = DeviceColumnType; column < DeviceColumnCount; ++column)
        m_tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    m_status->setObjectName("EtherCATEsiDeviceSelectionStatus");
    m_status->setAccessibleName(Tr::tr("Selected ESI device status"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_buttons->setObjectName("EtherCATEsiDeviceSelectionButtons");
    m_addButton->setText(Tr::tr("Add"));
    m_addButton->setDefault(true);
    m_addButton->setEnabled(false);

    auto optionLayout = new QHBoxLayout;
    optionLayout->setContentsMargins(QMargins());
    optionLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    optionLayout->addWidget(m_extendedInformation);
    optionLayout->addWidget(m_showPrevious);
    optionLayout->addStretch(1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(description);
    layout->addWidget(m_filter);
    layout->addLayout(optionLayout);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_status);
    layout->addWidget(m_buttons);

    const auto setExtendedInformationVisible = [this](bool visible) {
        for (int column : {DeviceColumnVendor,
                           DeviceColumnProduct,
                           DeviceColumnRevision,
                           DeviceColumnGroup}) {
            m_tree->setColumnHidden(column, !visible);
        }
    };
    connect(m_extendedInformation, &QCheckBox::toggled, this, setExtendedInformationVisible);
    connect(m_showPrevious, &QCheckBox::toggled, this, [this, proxy](bool show) {
        proxy->setShowPreviousRevisions(show);
        selectFirstVisibleDevice();
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxyModel->setFilterFixedString(text);
        selectFirstVisibleDevice();
    });
    connect(
        m_tree->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        [this] { updateSelection(); });
    connect(m_tree, &QTreeView::doubleClicked, this, [this] { accept(); });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &EsiDeviceSelectionDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setExtendedInformationVisible(false);
    selectFirstVisibleDevice();
    m_filter->setFocus();
}

Data::NodeId EsiDeviceSelectionDialog::selectedDeviceId() const
{
    return m_selectedDeviceId;
}

void EsiDeviceSelectionDialog::selectFirstVisibleDevice()
{
    QModelIndex selected;
    for (int row = 0; row < m_proxyModel->rowCount(); ++row) {
        const QModelIndex candidate = m_proxyModel->index(row, DeviceColumnName);
        if (candidate.data(DeviceSupportedRole).toBool()) {
            selected = candidate;
            break;
        }
    }
    if (!selected.isValid() && m_proxyModel->rowCount() > 0)
        selected = m_proxyModel->index(0, DeviceColumnName);
    m_tree->setCurrentIndex(selected);
    updateSelection();
}

void EsiDeviceSelectionDialog::updateSelection()
{
    const QModelIndex selected = m_tree->currentIndex().siblingAtColumn(DeviceColumnName);
    m_selectedDeviceId = Data::NodeId::fromString(selected.data(DeviceIdRole).toString());
    m_selectedDeviceSupported = selected.isValid()
                                && selected.data(DeviceSupportedRole).toBool()
                                && !m_selectedDeviceId.isNull();
    m_addButton->setEnabled(m_selectedDeviceSupported);

    if (!selected.isValid()) {
        m_status->setText(
            m_model->rowCount() == 0
                ? Tr::tr("No ESI devices are available. Import device descriptions in the ESI "
                         "Device Repository first.")
                : Tr::tr("No ESI device matches the current filter."));
        return;
    }
    if (!m_selectedDeviceSupported) {
        m_status->setText(
            Tr::tr("Limited ESI description. This device cannot be added until its unsupported "
                   "structures are resolved."));
        return;
    }
    m_status->setText(
        Tr::tr("Ready to append %1 (%2, revision %3) to the selected offline EtherCAT Master.")
            .arg(selected.data().toString())
            .arg(selected.siblingAtColumn(DeviceColumnType).data().toString())
            .arg(selected.siblingAtColumn(DeviceColumnRevision).data().toString()));
}

void EsiDeviceSelectionDialog::accept()
{
    if (m_selectedDeviceSupported && !m_selectedDeviceId.isNull())
        QDialog::accept();
}

} // namespace EtherCAT::Workbench::Internal
