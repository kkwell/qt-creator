// Copyright (C) 2026 Kvell

#include "workbenchtreemodel.h"

#include "ethercatworkbenchtr.h"

#include <coreplugin/coreicons.h>

#include <utils/utilsicons.h>

#include <QMimeData>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <vector>

namespace EtherCAT::Workbench::Internal {

static constexpr char deviceMimeType[] = "application/x-embed-labs-ethercat-esi-device";

enum class StateMarker { None, Healthy, Information, Warning, Error };

struct WorkbenchTreeModel::Node
{
    Data::NodeId id;
    Data::NodeId projectId;
    Core::WorkbenchNodeKind kind = Core::WorkbenchNodeKind::None;
    QString name;
    QString baseStatus;
    QString baseCompactStatus;
    QString status;
    QString compactStatus;
    QStringList presentationStatus;
    QStringList presentationCompactStatus;
    QStringList presentationDetails;
    Data::DeviceSummary device;
    Data::NodeId ownerSlaveId;
    Data::NodeId sourceId;
    StateMarker marker = StateMarker::None;
    bool topologyDifference = false;
    bool issue = false;
    int differenceOrder = std::numeric_limits<int>::max();
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
};

static Data::NodeId derivedNodeId(const QString &key)
{
    static const QUuid namespaceId("{5122a7ee-e988-5f83-9374-4b627d1c6a68}");
    return Data::NodeId::fromString(
        QUuid::createUuidV5(namespaceId, key.toUtf8()).toString());
}

static Core::WorkbenchNodeKind workbenchKind(Data::ProjectNodeKind kind)
{
    switch (kind) {
    case Data::ProjectNodeKind::Project:
        return Core::WorkbenchNodeKind::Project;
    case Data::ProjectNodeKind::Target:
        return Core::WorkbenchNodeKind::Target;
    case Data::ProjectNodeKind::Master:
        return Core::WorkbenchNodeKind::Master;
    case Data::ProjectNodeKind::Slave:
        return Core::WorkbenchNodeKind::ConfiguredSlave;
    }
    return Core::WorkbenchNodeKind::None;
}

static QString projectStatus(Core::WorkbenchNodeKind kind)
{
    switch (kind) {
    case Core::WorkbenchNodeKind::Project:
        return Tr::tr("Offline");
    case Core::WorkbenchNodeKind::Target:
        return Tr::tr("Offline target");
    case Core::WorkbenchNodeKind::Master:
        return Tr::tr("Not configured");
    case Core::WorkbenchNodeKind::ConfiguredSlave:
        return Tr::tr("Offline configured");
    default:
        return {};
    }
}

QString optionalProviderDisplayName(
    const OptionalProviderPresentation &provider, Core::ProviderKind kind)
{
    QString displayName = provider.displayName;
    if (displayName.isEmpty()) {
        displayName = kind == Core::ProviderKind::Scan
                          ? Tr::tr("Unnamed Scan Provider")
                          : Tr::tr("Unnamed Diagnostics Provider");
    }
    return displayName;
}

static QString optionalProviderStatus(
    const OptionalProviderPresentation &provider, Core::ProviderKind kind)
{
    if (provider.state == OptionalProviderState::Absent) {
        if (!Core::isMockUiEnabled()) {
            return kind == Core::ProviderKind::Scan
                       ? Tr::tr("Connect to the controller and scan the EtherCAT bus")
                       : Tr::tr("No Diagnostics Provider registered");
        }
        return kind == Core::ProviderKind::Scan
                   ? Tr::tr("No Scan Provider registered | Local Mock only")
                   : Tr::tr("No Diagnostics Provider registered | Local Mock only");
    }
    const QString displayName = optionalProviderDisplayName(provider, kind);
    return provider.state == OptionalProviderState::Available
               ? Tr::tr("%1 available").arg(displayName)
               : Tr::tr("%1 unavailable").arg(displayName);
}

static QString optionalProviderCompactStatus(
    const OptionalProviderPresentation &provider, Core::ProviderKind kind)
{
    if (provider.state == OptionalProviderState::Absent) {
        if (!Core::isMockUiEnabled()) {
            return kind == Core::ProviderKind::Scan ? Tr::tr("Scan controller bus")
                                                    : Tr::tr("No provider");
        }
        return Tr::tr("No provider · Mock only");
    }
    return provider.state == OptionalProviderState::Available ? Tr::tr("Available")
                                                              : Tr::tr("Unavailable");
}

static bool shouldShowDiagnosticsNode(const OptionalProviderPresentation &provider)
{
    return Core::isMockUiEnabled() || provider.state != OptionalProviderState::Absent;
}

static std::unique_ptr<WorkbenchTreeModel::Node> makeNode(
    WorkbenchTreeModel::Node *parent,
    const Data::NodeId &id,
    const Data::NodeId &projectId,
    Core::WorkbenchNodeKind kind,
    const QString &name,
    const QString &status,
    const QString &compactStatus = {})
{
    auto node = std::make_unique<WorkbenchTreeModel::Node>();
    node->parent = parent;
    node->id = id;
    node->projectId = projectId;
    node->kind = kind;
    node->name = name;
    node->baseStatus = status;
    node->baseCompactStatus = compactStatus.isEmpty() ? status : compactStatus;
    node->status = status;
    node->compactStatus = node->baseCompactStatus;
    return node;
}

static int markerPriority(StateMarker marker)
{
    switch (marker) {
    case StateMarker::None:
        return 0;
    case StateMarker::Healthy:
        return 1;
    case StateMarker::Information:
        return 2;
    case StateMarker::Warning:
        return 3;
    case StateMarker::Error:
        return 4;
    }
    return 0;
}

static StateMarker markerForSeverity(Data::DifferenceSeverity severity)
{
    switch (severity) {
    case Data::DifferenceSeverity::Information:
        return StateMarker::Information;
    case Data::DifferenceSeverity::Warning:
        return StateMarker::Warning;
    case Data::DifferenceSeverity::Blocking:
        return StateMarker::Error;
    }
    return StateMarker::None;
}

static void raiseMarker(WorkbenchTreeModel::Node *node, StateMarker marker)
{
    if (node && markerPriority(marker) > markerPriority(node->marker))
        node->marker = marker;
}

static void appendPresentationStatus(
    WorkbenchTreeModel::Node *node, const QString &status, const QString &compactStatus = {})
{
    if (node && !status.isEmpty() && !node->presentationStatus.contains(status)) {
        node->presentationStatus.append(status);
        node->presentationCompactStatus.append(
            compactStatus.isEmpty() ? status : compactStatus);
    }
}

static void appendPresentationDetail(WorkbenchTreeModel::Node *node, const QString &detail)
{
    if (node && !detail.isEmpty() && !node->presentationDetails.contains(detail))
        node->presentationDetails.append(detail);
}

static void applyInvalidProjectPresentation(
    WorkbenchTreeModel::Node *node, const Data::ProjectSnapshot &project)
{
    if (!node || project.valid)
        return;
    appendPresentationDetail(
        node,
        project.error.isEmpty()
            ? Tr::tr("The project file could not be loaded.")
            : Tr::tr("Project load error: %1").arg(project.error));
    raiseMarker(node, StateMarker::Error);
    node->issue = true;
}

static QString differenceName(Data::TopologyDifferenceKind kind)
{
    switch (kind) {
    case Data::TopologyDifferenceKind::Added:
        return Tr::tr("Added");
    case Data::TopologyDifferenceKind::Missing:
        return Tr::tr("Missing");
    case Data::TopologyDifferenceKind::PositionChanged:
        return Tr::tr("Position");
    case Data::TopologyDifferenceKind::VendorMismatch:
        return Tr::tr("Vendor");
    case Data::TopologyDifferenceKind::ProductMismatch:
        return Tr::tr("Product");
    case Data::TopologyDifferenceKind::RevisionMismatch:
        return Tr::tr("Revision");
    case Data::TopologyDifferenceKind::SerialMismatch:
        return Tr::tr("Serial Number");
    case Data::TopologyDifferenceKind::AliasMismatch:
        return Tr::tr("Alias");
    case Data::TopologyDifferenceKind::DuplicateDevice:
        return Tr::tr("Duplicate");
    case Data::TopologyDifferenceKind::PdoConfiguration:
        return Tr::tr("PDO");
    case Data::TopologyDifferenceKind::DcConfiguration:
        return Tr::tr("DC");
    }
    return {};
}

static QString etherCATStateName(Data::EtherCATState state)
{
    switch (state) {
    case Data::EtherCATState::Unknown:
        return Tr::tr("Unknown");
    case Data::EtherCATState::Init:
        return "INIT";
    case Data::EtherCATState::PreOperational:
        return "PREOP";
    case Data::EtherCATState::SafeOperational:
        return "SAFEOP";
    case Data::EtherCATState::Operational:
        return "OP";
    case Data::EtherCATState::Bootstrap:
        return "BOOT";
    }
    return Tr::tr("Unknown");
}

static QString runModeName(Data::DiagnosticsRunMode mode)
{
    switch (mode) {
    case Data::DiagnosticsRunMode::Offline:
        return Tr::tr("Offline");
    case Data::DiagnosticsRunMode::Config:
        return Tr::tr("Config");
    case Data::DiagnosticsRunMode::FreeRun:
        return Tr::tr("FreeRun");
    case Data::DiagnosticsRunMode::Run:
        return Tr::tr("Run");
    }
    return {};
}

static QString streamStateName(Data::DiagnosticsStreamState state)
{
    switch (state) {
    case Data::DiagnosticsStreamState::Stopped:
        return Tr::tr("Stopped");
    case Data::DiagnosticsStreamState::Starting:
        return Tr::tr("Starting");
    case Data::DiagnosticsStreamState::Running:
        return Tr::tr("Running");
    case Data::DiagnosticsStreamState::Stopping:
        return Tr::tr("Stopping");
    case Data::DiagnosticsStreamState::Failed:
        return Tr::tr("Failed");
    }
    return {};
}

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static QString dataTypeName(Data::EtherCATDataType type)
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
        return Tr::tr("Unknown");
    }
    return Tr::tr("Unknown");
}

static qint64 pdoBitSize(const Data::PdoConfiguration &pdo)
{
    qint64 result = 0;
    for (const Data::PdoEntryConfiguration &entry : pdo.entries)
        result += qMax(0, entry.bitLength);
    return result;
}

static QString pdoName(const Data::PdoConfiguration &pdo)
{
    return pdo.name.isEmpty() ? Tr::tr("PDO %1").arg(hexValue(pdo.index, 4)) : pdo.name;
}

static QString entryName(const Data::PdoEntryConfiguration &entry)
{
    if (!entry.name.isEmpty())
        return entry.name;
    if (entry.padding)
        return Tr::tr("Padding");
    return Tr::tr("Entry %1:%2")
        .arg(hexValue(entry.index, 4))
        .arg(entry.subIndex, 2, 16, QLatin1Char('0'));
}

static QString sourceKey(const Data::NodeId &sourceId, const QString &fallback)
{
    return sourceId.isNull() ? fallback : sourceId.toString();
}

static std::unique_ptr<WorkbenchTreeModel::Node> makeSlaveChild(
    WorkbenchTreeModel::Node *parent,
    const Data::OfflineSlaveConfiguration &slave,
    Core::WorkbenchNodeKind kind,
    const QString &key,
    const QString &name,
    const QString &status,
    const Data::NodeId &sourceId = {},
    const QString &compactStatus = {})
{
    auto node = makeNode(
        parent,
        derivedNodeId(slave.id.toString() + ':' + key),
        parent->projectId,
        kind,
        name,
        status,
        compactStatus);
    node->ownerSlaveId = slave.id;
    node->sourceId = sourceId;
    return node;
}

static void appendEmptyState(
    WorkbenchTreeModel::Node *parent,
    const Data::OfflineSlaveConfiguration &slave,
    const QString &key,
    const QString &name,
    const QString &status,
    const QString &compactStatus = {})
{
    parent->children.push_back(makeSlaveChild(
        parent,
        slave,
        Core::WorkbenchNodeKind::Placeholder,
        key,
        name,
        status,
        {},
        compactStatus));
}

static void appendProcessImageBranch(
    WorkbenchTreeModel::Node *slaveNode,
    const Data::OfflineSlaveConfiguration &slave,
    Core::WorkbenchNodeKind kind,
    const QString &key,
    const QString &name,
    const Data::ProcessImageDirection &image,
    const QString &emptyName,
    const QString &emptyStatus)
{
    const QString status = image.entries.isEmpty()
                               ? Tr::tr("No mapped variables")
                               : Tr::tr("%n variable(s), %1 byte(s)", nullptr, image.entries.size())
                                     .arg(image.byteSize);
    auto branch = makeSlaveChild(slaveNode, slave, kind, key, name, status);
    WorkbenchTreeModel::Node *branchPointer = branch.get();
    slaveNode->children.push_back(std::move(branch));

    QList<Data::ProcessImageEntry> entries = image.entries;
    std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
        if (left.bitOffset != right.bitOffset)
            return left.bitOffset < right.bitOffset;
        return left.entryId.toString() < right.entryId.toString();
    });
    for (const Data::ProcessImageEntry &entry : std::as_const(entries)) {
        const QString pdoKey = sourceKey(
            entry.pdoId, QString("%1:%2").arg(entry.syncManager).arg(entry.pdoIndex));
        const QString entryKey = sourceKey(
            entry.entryId,
            QString("%1:%2:%3")
                .arg(entry.pdoIndex)
                .arg(entry.index)
                .arg(entry.subIndex));
        const QString statusText
            = Tr::tr("@%1.%2, %3, %4 bit(s)")
                  .arg(entry.byteOffset)
                  .arg(entry.bitOffsetInByte)
                  .arg(dataTypeName(entry.dataType))
                  .arg(entry.bitLength);
        branchPointer->children.push_back(makeSlaveChild(
            branchPointer,
            slave,
            Core::WorkbenchNodeKind::PdoEntry,
            key + ":pdo:" + pdoKey + ":entry:" + entryKey,
            entry.name.isEmpty()
                ? Tr::tr("Entry %1:%2")
                      .arg(hexValue(entry.index, 4))
                      .arg(entry.subIndex, 2, 16, QLatin1Char('0'))
                : entry.name,
            statusText,
            entry.entryId,
            Tr::tr("@%1.%2 · %3")
                .arg(entry.byteOffset)
                .arg(entry.bitOffsetInByte)
                .arg(dataTypeName(entry.dataType))));
    }
    if (branchPointer->children.empty())
        appendEmptyState(branchPointer, slave, key + ":empty", emptyName, emptyStatus);
}

static void appendPdoBranch(
    WorkbenchTreeModel::Node *slaveNode,
    const Data::OfflineSlaveConfiguration &slave,
    Data::PdoDirection direction,
    Core::WorkbenchNodeKind kind,
    const QString &key,
    const QString &name)
{
    QList<Data::PdoConfiguration> pdos;
    for (const Data::PdoConfiguration &pdo : slave.processData.pdos) {
        if (pdo.selected && pdo.direction == direction)
            pdos.append(pdo);
    }
    std::sort(pdos.begin(), pdos.end(), [](const auto &left, const auto &right) {
        if (left.syncManager != right.syncManager)
            return left.syncManager < right.syncManager;
        if (left.index != right.index)
            return left.index < right.index;
        return left.id.toString() < right.id.toString();
    });

    const QString status = pdos.isEmpty()
                               ? Tr::tr("No assigned PDOs")
                               : Tr::tr("%n assigned PDO(s)", nullptr, pdos.size());
    auto branch = makeSlaveChild(slaveNode, slave, kind, key, name, status);
    WorkbenchTreeModel::Node *branchPointer = branch.get();
    slaveNode->children.push_back(std::move(branch));

    for (const Data::PdoConfiguration &pdo : std::as_const(pdos)) {
        const QString pdoKey = sourceKey(
            pdo.id, QString("%1:%2").arg(pdo.syncManager).arg(pdo.index));
        auto pdoNode = makeSlaveChild(
            branchPointer,
            slave,
            Core::WorkbenchNodeKind::Pdo,
            key + ":pdo:" + pdoKey,
            pdoName(pdo),
            Tr::tr("%1, SM%2, %3 bit(s)")
                .arg(hexValue(pdo.index, 4))
                .arg(pdo.syncManager)
                .arg(pdoBitSize(pdo)),
            pdo.id,
            Tr::tr("%1 · SM%2").arg(hexValue(pdo.index, 4)).arg(pdo.syncManager));
        WorkbenchTreeModel::Node *pdoPointer = pdoNode.get();
        branchPointer->children.push_back(std::move(pdoNode));
        for (int row = 0; row < pdo.entries.size(); ++row) {
            const Data::PdoEntryConfiguration &entry = pdo.entries.at(row);
            const QString entryKey = sourceKey(
                entry.id,
                QString("%1:%2:%3").arg(entry.index).arg(entry.subIndex).arg(row));
            pdoPointer->children.push_back(makeSlaveChild(
                pdoPointer,
                slave,
                Core::WorkbenchNodeKind::PdoEntry,
                key + ":pdo:" + pdoKey + ":entry:" + entryKey,
                entryName(entry),
                Tr::tr("%1:%2, %3, %4 bit(s)")
                    .arg(hexValue(entry.index, 4))
                    .arg(entry.subIndex, 2, 16, QLatin1Char('0'))
                    .arg(dataTypeName(entry.dataType))
                    .arg(entry.bitLength),
                entry.id,
                Tr::tr("%1:%2")
                    .arg(hexValue(entry.index, 4))
                    .arg(entry.subIndex, 2, 16, QLatin1Char('0'))));
        }
        if (pdoPointer->children.empty()) {
            appendEmptyState(
                pdoPointer,
                slave,
                key + ":pdo:" + pdoKey + ":empty",
                Tr::tr("No PDO entries"),
                Tr::tr("The assigned PDO has no mapped entries"),
                Tr::tr("No mapped entries"));
        }
    }
    if (branchPointer->children.empty()) {
        appendEmptyState(
            branchPointer,
            slave,
            key + ":empty",
            direction == Data::PdoDirection::Rx ? Tr::tr("No assigned RxPDOs")
                                                : Tr::tr("No assigned TxPDOs"),
            Tr::tr("Select mappings on the Process Data page"),
            Tr::tr("Configure mappings"));
    }
}

static void appendConfiguredSlaveChildren(
    WorkbenchTreeModel::Node *slaveNode, const Data::OfflineSlaveConfiguration &slave)
{
    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        slave.processData);
    appendProcessImageBranch(
        slaveNode,
        slave,
        Core::WorkbenchNodeKind::ProcessInputs,
        "inputs",
        Tr::tr("Inputs"),
        validation.processImage.inputs,
        Tr::tr("No input variables"),
        Tr::tr("No selected TxPDO entries"));
    appendProcessImageBranch(
        slaveNode,
        slave,
        Core::WorkbenchNodeKind::ProcessOutputs,
        "outputs",
        Tr::tr("Outputs"),
        validation.processImage.outputs,
        Tr::tr("No output variables"),
        Tr::tr("No selected RxPDO entries"));
    appendPdoBranch(
        slaveNode,
        slave,
        Data::PdoDirection::Rx,
        Core::WorkbenchNodeKind::RxPdoGroup,
        "rxpdo",
        Tr::tr("RxPDO"));
    appendPdoBranch(
        slaveNode,
        slave,
        Data::PdoDirection::Tx,
        Core::WorkbenchNodeKind::TxPdoGroup,
        "txpdo",
        Tr::tr("TxPDO"));

    auto modules = makeSlaveChild(
        slaveNode,
        slave,
        Core::WorkbenchNodeKind::Modules,
        "modules",
        Tr::tr("Modules / Channels"),
        Tr::tr("No configured modules"),
        {},
        Tr::tr("No modular data"));
    WorkbenchTreeModel::Node *modulesPointer = modules.get();
    slaveNode->children.push_back(std::move(modules));
    appendEmptyState(
        modulesPointer,
        slave,
        "modules:empty",
        Tr::tr("No module or channel data"),
        Tr::tr("No modular profile is stored in this project"),
        Tr::tr("No modular data"));
}

WorkbenchTreeModel::WorkbenchTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    rebuild();
}

WorkbenchTreeModel::~WorkbenchTreeModel() = default;

QModelIndex WorkbenchTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= columnCount(parent))
        return {};
    Node *parentNode = nodeForIndex(parent);
    if (!parentNode || row >= int(parentNode->children.size()))
        return {};
    return createIndex(row, column, parentNode->children.at(row).get());
}

QModelIndex WorkbenchTreeModel::parent(const QModelIndex &child) const
{
    Node *node = nodeForIndex(child);
    if (!node || !node->parent || node->parent == m_root.get())
        return {};
    return indexForNode(node->parent);
}

int WorkbenchTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;
    Node *node = nodeForIndex(parent);
    return node ? int(node->children.size()) : 0;
}

int WorkbenchTreeModel::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant WorkbenchTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Node *node = nodeForIndex(index);
    if (!node)
        return {};

    const QString status = visibleStatus(node);
    if (role == Qt::DisplayRole)
        return index.column() == 0 ? node->name : visibleCompactStatus(node);
    if (role == Qt::AccessibleTextRole)
        return index.column() == 0 ? node->name : status;
    if (role == NodeIdRole)
        return QVariant::fromValue(node->id);
    if (role == NodeKindRole)
        return QVariant::fromValue(node->kind);
    if (role == ProjectIdRole)
        return QVariant::fromValue(node->projectId);
    if (role == StatusRole)
        return status;
    if (role == SearchTextRole) {
        if (index.column() != 0)
            return {};
        QString text = node->name + ' ' + status;
        const QString compactStatus = visibleCompactStatus(node);
        if (compactStatus != status)
            text += ' ' + compactStatus;
        if (!node->presentationDetails.isEmpty())
            text += ' ' + node->presentationDetails.join(' ');
        if (node->kind == Core::WorkbenchNodeKind::Device) {
            text += QString(" %1 %2 %3 %4 %5 0x%1 0x%2 0x%3")
                        .arg(node->device.identity.vendorId, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.productCode, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.revisionNumber, 8, 16, QLatin1Char('0'))
                        .arg(node->device.group)
                        .arg(node->device.typeName);
        }
        return text;
    }
    if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) {
        QString text = node->name;
        if (!status.isEmpty())
            text += "\n" + Tr::tr("Status: %1").arg(status);
        if (!node->presentationDetails.isEmpty()) {
            text += "\n" + Tr::tr("Details:");
            text += "\n" + node->presentationDetails.join("\n");
        }
        if (node->kind == Core::WorkbenchNodeKind::Device) {
            if (!node->device.typeName.isEmpty())
                text += Tr::tr("\nType: %1").arg(node->device.typeName);
            if (!node->device.group.isEmpty())
                text += Tr::tr("\nGroup: %1").arg(node->device.group);
            text += Tr::tr("\nVendor: 0x%1\nProduct: 0x%2\nRevision: 0x%3")
                        .arg(node->device.identity.vendorId, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.productCode, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.revisionNumber, 8, 16, QLatin1Char('0'));
            if (node->device.supported) {
                text += Tr::tr(
                    "\nDrag this ESI device to the active offline EtherCAT Master to append it.");
            }
        } else if (
            node->kind == Core::WorkbenchNodeKind::Master
            && node->id == m_dropTargetMasterId) {
            text += Tr::tr(
                "\nDrop a supported ESI device here to append it to this offline Master.");
        }
        return text;
    }
    if (role == Qt::DecorationRole && index.column() == 0) {
        switch (node->marker) {
        case StateMarker::Healthy:
            return Utils::Icons::OK.icon();
        case StateMarker::Information:
            return Utils::Icons::INFO.icon();
        case StateMarker::Warning:
            return Utils::Icons::WARNING.icon();
        case StateMarker::Error:
            return Utils::Icons::CRITICAL.icon();
        case StateMarker::None:
            break;
        }
        switch (node->kind) {
        case Core::WorkbenchNodeKind::Project:
            return Utils::Icons::PROJECT.icon();
        case Core::WorkbenchNodeKind::Target:
        case Core::WorkbenchNodeKind::Master:
        case Core::WorkbenchNodeKind::DeviceRepository:
            return Utils::Icons::SETTINGS.icon();
        case Core::WorkbenchNodeKind::Device:
        case Core::WorkbenchNodeKind::ConfiguredSlave:
            return node->device.supported ? ::Core::Icons::DESKTOP_DEVICE_SMALL.icon()
                                          : Utils::Icons::BROKEN.icon();
        case Core::WorkbenchNodeKind::Diagnostics:
            return Utils::Icons::INFO.icon();
        case Core::WorkbenchNodeKind::ProcessInputs:
        case Core::WorkbenchNodeKind::ProcessOutputs:
            return Utils::Icons::SNAPSHOT.icon();
        case Core::WorkbenchNodeKind::RxPdoGroup:
        case Core::WorkbenchNodeKind::TxPdoGroup:
        case Core::WorkbenchNodeKind::Pdo:
            return Utils::Icons::SETTINGS.icon();
        case Core::WorkbenchNodeKind::PdoEntry:
        case Core::WorkbenchNodeKind::Channel:
            return Utils::Icons::LINK.icon();
        case Core::WorkbenchNodeKind::Modules:
            return Utils::Icons::DIR.icon();
        case Core::WorkbenchNodeKind::Module:
            return ::Core::Icons::DESKTOP_DEVICE_SMALL.icon();
        case Core::WorkbenchNodeKind::Placeholder:
            return Utils::Icons::NOTLOADED.icon();
        default:
            return {};
        }
    }
    return {};
}

QVariant WorkbenchTreeModel::headerData(
    int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return section == 0 ? Tr::tr("EtherCAT Node") : Tr::tr("Status");
}

Qt::ItemFlags WorkbenchTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    const Node *node = nodeForIndex(index);
    if (!node)
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled;
    if (node->kind != Core::WorkbenchNodeKind::Placeholder)
        result |= Qt::ItemIsSelectable;
    if (index.column() == 0 && node->kind == Core::WorkbenchNodeKind::Device
        && node->device.supported) {
        result |= Qt::ItemIsDragEnabled;
    }
    if (node->kind == Core::WorkbenchNodeKind::Master && node->id == m_dropTargetMasterId)
        result |= Qt::ItemIsDropEnabled;
    return result;
}

Qt::DropActions WorkbenchTreeModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

Qt::DropActions WorkbenchTreeModel::supportedDropActions() const
{
    return Qt::CopyAction;
}

QStringList WorkbenchTreeModel::mimeTypes() const
{
    return {deviceMimeType};
}

QMimeData *WorkbenchTreeModel::mimeData(const QModelIndexList &indexes) const
{
    auto data = new QMimeData;
    Data::NodeId deviceId;
    for (const QModelIndex &index : indexes) {
        if (!index.isValid() || index.column() != 0)
            continue;
        const Node *node = nodeForIndex(index);
        if (!node || node->kind != Core::WorkbenchNodeKind::Device || !node->device.supported)
            continue;
        if (!deviceId.isNull() && deviceId != node->id)
            return data;
        deviceId = node->id;
    }
    if (!deviceId.isNull())
        data->setData(deviceMimeType, deviceId.toString().toUtf8());
    return data;
}

bool WorkbenchTreeModel::canDropMimeData(
    const QMimeData *data,
    Qt::DropAction action,
    int row,
    int column,
    const QModelIndex &parent) const
{
    if (!m_deviceDropHandler || action != Qt::CopyAction || row != -1
        || (column != -1 && column != 0)) {
        return false;
    }
    const Node *target = nodeForIndex(parent);
    if (!target || target == m_root.get() || target->kind != Core::WorkbenchNodeKind::Master
        || target->id != m_dropTargetMasterId) {
        return false;
    }
    const Node *device = findNode(deviceIdFromMimeData(data));
    return device && device->kind == Core::WorkbenchNodeKind::Device && device->device.supported;
}

bool WorkbenchTreeModel::dropMimeData(
    const QMimeData *data,
    Qt::DropAction action,
    int row,
    int column,
    const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction)
        return true;
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    const Node *target = nodeForIndex(parent);
    const Data::NodeId deviceId = deviceIdFromMimeData(data);
    const Data::NodeId masterId = target->id;
    return m_deviceDropHandler(deviceId, masterId);
}

QHash<int, QByteArray> WorkbenchTreeModel::roleNames() const
{
    QHash<int, QByteArray> result = QAbstractItemModel::roleNames();
    result.insert(NodeIdRole, "nodeId");
    result.insert(NodeKindRole, "nodeKind");
    result.insert(ProjectIdRole, "projectId");
    result.insert(StatusRole, "status");
    result.insert(SearchTextRole, "searchText");
    return result;
}

void WorkbenchTreeModel::setProjects(const QList<Data::ProjectSnapshot> &projects)
{
    if (m_projects == projects)
        return;
    m_projects = projects;
    rebuild();
}

void WorkbenchTreeModel::setActiveProjectId(const Data::NodeId &projectId)
{
    if (m_activeProjectId == projectId)
        return;

    const auto projectIndex = [this](const Data::NodeId &id) {
        const QModelIndex index = indexForNodeId(id);
        if (!index.isValid() || index.parent().isValid())
            return QModelIndex();
        const Node *node = nodeForIndex(index);
        return node && node->kind == Core::WorkbenchNodeKind::Project ? index : QModelIndex();
    };
    const QModelIndex previous = projectIndex(m_activeProjectId);
    m_activeProjectId = projectId;
    const QModelIndex current = projectIndex(m_activeProjectId);
    if (!previous.isValid() && !current.isValid())
        return;

    const int firstRow = previous.isValid() && current.isValid()
                             ? std::min(previous.row(), current.row())
                             : (previous.isValid() ? previous.row() : current.row());
    const int lastRow = previous.isValid() && current.isValid()
                            ? std::max(previous.row(), current.row())
                            : firstRow;
    emit dataChanged(
        index(firstRow, 0),
        index(lastRow, columnCount() - 1),
        {Qt::DisplayRole,
         Qt::ToolTipRole,
         Qt::AccessibleTextRole,
         Qt::AccessibleDescriptionRole,
         StatusRole,
         SearchTextRole});
}

void WorkbenchTreeModel::setDropTargetMasterId(const Data::NodeId &masterId)
{
    if (m_dropTargetMasterId == masterId)
        return;
    const QModelIndex previous = indexForNodeId(m_dropTargetMasterId);
    m_dropTargetMasterId = masterId;
    const QModelIndex current = indexForNodeId(m_dropTargetMasterId);
    if (previous.isValid())
        emit dataChanged(previous, previous.siblingAtColumn(columnCount(previous) - 1));
    if (current.isValid())
        emit dataChanged(current, current.siblingAtColumn(columnCount(current) - 1));
}

void WorkbenchTreeModel::setDeviceDropHandler(DeviceDropHandler handler)
{
    m_deviceDropHandler = std::move(handler);
}

void WorkbenchTreeModel::syncDevices(const QList<Data::DeviceSummary> &devices)
{
    QList<Data::DeviceSummary> desired = devices;
    std::sort(desired.begin(), desired.end(), [](const auto &left, const auto &right) {
        const int nameOrder = left.name.compare(right.name, Qt::CaseInsensitive);
        if (nameOrder != 0)
            return nameOrder < 0;
        return left.id.toString() < right.id.toString();
    });
    m_devices = desired;
    if (!m_repositoryNode) {
        rebuild();
        return;
    }

    const QModelIndex repositoryIndex = indexForNode(m_repositoryNode);
    auto &current = m_repositoryNode->children;
    if (desired.isEmpty()) {
        if (current.size() == 1
            && current.front()->kind == Core::WorkbenchNodeKind::Placeholder) {
            return;
        }
        if (!current.empty()) {
            beginRemoveRows(repositoryIndex, 0, int(current.size()) - 1);
            current.clear();
            endRemoveRows();
        }
        beginInsertRows(repositoryIndex, 0, 0);
        current.push_back(makeNode(
            m_repositoryNode,
            derivedNodeId("repository:empty"),
            {},
            Core::WorkbenchNodeKind::Placeholder,
            Tr::tr("No ESI devices imported"),
            Tr::tr("Select Device Repository, then choose Import ESI Files..."),
            Tr::tr("Import ESI files")));
        endInsertRows();
        return;
    }

    if (current.size() == 1 && current.front()->kind == Core::WorkbenchNodeKind::Placeholder) {
        beginRemoveRows(repositoryIndex, 0, 0);
        current.clear();
        endRemoveRows();
    }
    if (current.empty()) {
        beginInsertRows(repositoryIndex, 0, desired.size() - 1);
        for (const Data::DeviceSummary &summary : std::as_const(desired)) {
            auto node = makeNode(
                m_repositoryNode,
                summary.id,
                {},
                Core::WorkbenchNodeKind::Device,
                summary.name,
                summary.supported ? Tr::tr("Available") : Tr::tr("Unsupported"));
            node->device = summary;
            current.push_back(std::move(node));
        }
        endInsertRows();
        return;
    }

    const QSet<Data::NodeId> desiredIds = [&desired] {
        QSet<Data::NodeId> ids;
        for (const Data::DeviceSummary &device : desired)
            ids.insert(device.id);
        return ids;
    }();
    for (int row = int(current.size()) - 1; row >= 0; --row) {
        if (desiredIds.contains(current.at(row)->id))
            continue;
        beginRemoveRows(repositoryIndex, row, row);
        current.erase(current.begin() + row);
        endRemoveRows();
    }

    for (int desiredRow = 0; desiredRow < desired.size(); ++desiredRow) {
        const Data::DeviceSummary &summary = desired.at(desiredRow);
        int currentRow = -1;
        for (int row = desiredRow; row < int(current.size()); ++row) {
            if (current.at(row)->id == summary.id) {
                currentRow = row;
                break;
            }
        }
        if (currentRow < 0) {
            beginInsertRows(repositoryIndex, desiredRow, desiredRow);
            auto node = makeNode(
                m_repositoryNode,
                summary.id,
                {},
                Core::WorkbenchNodeKind::Device,
                summary.name,
                summary.supported ? Tr::tr("Available") : Tr::tr("Unsupported"));
            node->device = summary;
            current.insert(current.begin() + desiredRow, std::move(node));
            endInsertRows();
        } else if (currentRow != desiredRow) {
            beginMoveRows(repositoryIndex, currentRow, currentRow, repositoryIndex, desiredRow);
            std::unique_ptr<Node> moved = std::move(current.at(currentRow));
            current.erase(current.begin() + currentRow);
            current.insert(current.begin() + desiredRow, std::move(moved));
            endMoveRows();
        }

        Node *node = current.at(desiredRow).get();
        const QString status = summary.supported ? Tr::tr("Available") : Tr::tr("Unsupported");
        if (node->name != summary.name || node->status != status || node->device != summary) {
            node->name = summary.name;
            node->baseStatus = status;
            node->baseCompactStatus = status;
            node->status = status;
            node->compactStatus = status;
            node->device = summary;
            emit dataChanged(
                index(desiredRow, 0, repositoryIndex),
                index(desiredRow, 1, repositoryIndex));
        }
    }
}

void WorkbenchTreeModel::setProviderPresentations(
    const OptionalProviderPresentation &scanProvider,
    const OptionalProviderPresentation &diagnosticsProvider,
    const std::optional<Data::ScanResult> &scanResult,
    Data::DiagnosticsStreamState diagnosticsState,
    const Data::DiagnosticsRequest &diagnosticsRequest,
    const std::optional<Data::DiagnosticsSnapshot> &diagnosticsSnapshot)
{
    const bool optionalProvidersChanged = m_scanProvider != scanProvider
                                          || m_diagnosticsProvider != diagnosticsProvider;
    const bool diagnosticsNodeVisibilityChanged
        = shouldShowDiagnosticsNode(m_diagnosticsProvider)
          != shouldShowDiagnosticsNode(diagnosticsProvider);
    if (!optionalProvidersChanged && m_scanResult == scanResult
        && m_diagnosticsState == diagnosticsState
        && m_diagnosticsRequest == diagnosticsRequest
        && m_diagnosticsSnapshot == diagnosticsSnapshot) {
        return;
    }

    m_scanProvider = scanProvider;
    m_diagnosticsProvider = diagnosticsProvider;
    m_scanResult = scanResult;
    m_diagnosticsState = diagnosticsState;
    m_diagnosticsRequest = diagnosticsRequest;
    m_diagnosticsSnapshot = diagnosticsSnapshot;
    if (diagnosticsNodeVisibilityChanged) {
        rebuild();
        return;
    }
    if (optionalProvidersChanged)
        updateOptionalProviderStatus();
    updateProviderPresentation();
}

void WorkbenchTreeModel::clear()
{
    m_projects.clear();
    m_devices.clear();
    m_activeProjectId = {};
    m_dropTargetMasterId = {};
    rebuild();
}

QModelIndex WorkbenchTreeModel::indexForNodeId(const Data::NodeId &nodeId, int column) const
{
    if (nodeId.isNull())
        return {};
    return indexForNode(findNode(nodeId), column);
}

QString WorkbenchTreeModel::visibleStatus(const Node *node) const
{
    if (!node)
        return {};
    if (node->kind == Core::WorkbenchNodeKind::Project && node->projectId == m_activeProjectId)
        return Tr::tr("Active project | %1").arg(node->status);
    return node->status;
}

QString WorkbenchTreeModel::visibleCompactStatus(const Node *node) const
{
    if (!node)
        return {};
    if (node->kind == Core::WorkbenchNodeKind::Project && node->projectId == m_activeProjectId)
        return Tr::tr("Active · %1").arg(node->compactStatus);
    return node->compactStatus;
}

QModelIndex WorkbenchTreeModel::firstUnsupportedDevice() const
{
    if (!m_repositoryNode)
        return {};
    for (const std::unique_ptr<Node> &node : m_repositoryNode->children) {
        if (node->kind == Core::WorkbenchNodeKind::Device && !node->device.supported)
            return indexForNode(node.get());
    }
    return {};
}

QModelIndex WorkbenchTreeModel::firstTopologyDifference(const Data::NodeId &projectId) const
{
    Node *best = nullptr;
    Node *fallback = nullptr;
    int bestOrder = std::numeric_limits<int>::max();
    const auto visit = [&best, &fallback, &bestOrder, &projectId](
                           const auto &self, Node *parent) -> void {
        for (const std::unique_ptr<Node> &child : parent->children) {
            if (!projectId.isNull() && child->projectId != projectId)
                continue;
            self(self, child.get());
            if (!child->topologyDifference)
                continue;
            if (!fallback)
                fallback = child.get();
            if (child->differenceOrder < bestOrder) {
                best = child.get();
                bestOrder = child->differenceOrder;
            }
        }
    };
    visit(visit, m_root.get());
    return indexForNode(best ? best : fallback);
}

QModelIndex WorkbenchTreeModel::firstIssue(const Data::NodeId &projectId) const
{
    const auto findForMarker = [&projectId](
                                   const auto &self, Node *parent, StateMarker marker) -> Node * {
        for (const std::unique_ptr<Node> &child : parent->children) {
            if (!projectId.isNull() && child->projectId != projectId)
                continue;
            if (Node *descendant = self(self, child.get(), marker))
                return descendant;
            if (child->issue && child->marker == marker)
                return child.get();
        }
        return nullptr;
    };
    if (Node *error = findForMarker(findForMarker, m_root.get(), StateMarker::Error))
        return indexForNode(error);
    return indexForNode(findForMarker(findForMarker, m_root.get(), StateMarker::Warning));
}

QModelIndex WorkbenchTreeModel::diagnosticsForProject(const Data::NodeId &projectId) const
{
    const auto findDiagnostics = [&projectId](const auto &self, Node *parent) -> Node * {
        for (const std::unique_ptr<Node> &child : parent->children) {
            if (child->kind == Core::WorkbenchNodeKind::Diagnostics
                && (projectId.isNull() || child->projectId == projectId)) {
                return child.get();
            }
            if (Node *descendant = self(self, child.get()))
                return descendant;
        }
        return nullptr;
    };
    return indexForNode(findDiagnostics(findDiagnostics, m_root.get()));
}

Core::PropertyPageContext WorkbenchTreeModel::contextForIndex(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    if (!node || node == m_root.get())
        return {};
    return {node->projectId, node->id, node->kind, node->name};
}

Core::PropertyPageContext WorkbenchTreeModel::contextForNodeId(
    const Data::NodeId &nodeId) const
{
    return contextForIndex(indexForNodeId(nodeId));
}

Data::NodeId WorkbenchTreeModel::sourceNodeId(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    return node && node != m_root.get() ? node->sourceId : Data::NodeId();
}

Data::NodeId WorkbenchTreeModel::sourceNodeId(const Data::NodeId &nodeId) const
{
    const Node *node = findNode(nodeId);
    return node ? node->sourceId : Data::NodeId();
}

std::optional<Data::OfflineSlaveConfiguration> WorkbenchTreeModel::offlineSlave(
    const Data::NodeId &nodeId) const
{
    const Node *node = findNode(nodeId);
    if (!node || node->ownerSlaveId.isNull())
        return std::nullopt;
    for (const Data::ProjectSnapshot &project : m_projects) {
        const auto slave = std::find_if(
            project.slaves.cbegin(), project.slaves.cend(), [node](const auto &candidate) {
                return candidate.id == node->ownerSlaveId;
            });
        if (slave != project.slaves.cend())
            return *slave;
    }
    return std::nullopt;
}

QList<Data::OfflineSlaveConfiguration> WorkbenchTreeModel::offlineSlavesForMaster(
    const Data::NodeId &masterId) const
{
    QList<Data::OfflineSlaveConfiguration> result;
    for (const Data::ProjectSnapshot &project : m_projects) {
        for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
            if (slave.masterId == masterId)
                result.append(slave);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return result;
}

QModelIndex WorkbenchTreeModel::indexForNode(const Node *node, int column) const
{
    if (!node || !node->parent || node == m_root.get())
        return {};
    const auto &siblings = node->parent->children;
    for (int row = 0; row < int(siblings.size()); ++row) {
        if (siblings.at(row).get() == node)
            return createIndex(row, column, const_cast<Node *>(node));
    }
    return {};
}

WorkbenchTreeModel::Node *WorkbenchTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root.get();
    return static_cast<Node *>(index.internalPointer());
}

WorkbenchTreeModel::Node *WorkbenchTreeModel::findNode(const Data::NodeId &nodeId) const
{
    if (nodeId.isNull())
        return nullptr;
    const auto findRecursive = [&nodeId](const auto &self, Node *parent) -> Node * {
        for (const std::unique_ptr<Node> &child : parent->children) {
            if (child->id == nodeId)
                return child.get();
            if (Node *found = self(self, child.get()))
                return found;
        }
        return nullptr;
    };
    return findRecursive(findRecursive, m_root.get());
}

Data::NodeId WorkbenchTreeModel::deviceIdFromMimeData(const QMimeData *data) const
{
    if (!data || !data->hasFormat(deviceMimeType))
        return {};
    const QByteArray encoded = data->data(deviceMimeType);
    if (encoded.isEmpty() || encoded.size() > 128)
        return {};
    return Data::NodeId::fromString(QString::fromUtf8(encoded));
}

void WorkbenchTreeModel::updateOptionalProviderStatus()
{
    const auto updateRecursive = [this](const auto &self, Node *parent) -> void {
        for (const std::unique_ptr<Node> &child : parent->children) {
            QString status = child->baseStatus;
            if (child->kind == Core::WorkbenchNodeKind::Diagnostics) {
                status = optionalProviderStatus(
                    m_diagnosticsProvider, Core::ProviderKind::Diagnostics);
            } else if (
                child->kind == Core::WorkbenchNodeKind::Placeholder && child->parent
                && child->parent->kind == Core::WorkbenchNodeKind::Master) {
                status = optionalProviderStatus(m_scanProvider, Core::ProviderKind::Scan);
            }
            child->baseStatus = status;
            if (child->kind == Core::WorkbenchNodeKind::Diagnostics) {
                child->baseCompactStatus = optionalProviderCompactStatus(
                    m_diagnosticsProvider, Core::ProviderKind::Diagnostics);
            } else if (
                child->kind == Core::WorkbenchNodeKind::Placeholder && child->parent
                && child->parent->kind == Core::WorkbenchNodeKind::Master) {
                child->baseCompactStatus = optionalProviderCompactStatus(
                    m_scanProvider, Core::ProviderKind::Scan);
            }
            self(self, child.get());
        }
    };
    updateRecursive(updateRecursive, m_root.get());
}

void WorkbenchTreeModel::updateProviderPresentation()
{
    if (!m_root)
        return;

    struct PreviousPresentation
    {
        Node *node = nullptr;
        QString status;
        QString compactStatus;
        QStringList details;
        StateMarker marker = StateMarker::None;
        bool topologyDifference = false;
        bool issue = false;
        int differenceOrder = std::numeric_limits<int>::max();
    };
    QList<PreviousPresentation> previous;
    const auto reset = [&previous](const auto &self, Node *parent) -> void {
        for (const std::unique_ptr<Node> &child : parent->children) {
            previous.append(
                {child.get(),
                 child->status,
                 child->compactStatus,
                 child->presentationDetails,
                 child->marker,
                 child->topologyDifference,
                 child->issue,
                 child->differenceOrder});
            child->status = child->baseStatus;
            child->compactStatus = child->baseCompactStatus;
            child->presentationStatus.clear();
            child->presentationCompactStatus.clear();
            child->presentationDetails.clear();
            child->marker = StateMarker::None;
            child->topologyDifference = false;
            child->issue = false;
            child->differenceOrder = std::numeric_limits<int>::max();
            self(self, child.get());
        }
    };
    reset(reset, m_root.get());

    for (const Data::ProjectSnapshot &project : std::as_const(m_projects)) {
        if (!project.valid)
            applyInvalidProjectPresentation(findNode(project.id), project);
    }

    if (m_diagnosticsSnapshot) {
        const Data::DiagnosticsSnapshot &snapshot = *m_diagnosticsSnapshot;
        Node *master = findNode(snapshot.masterId);
        if (master && master->kind == Core::WorkbenchNodeKind::Master
            && master->projectId == snapshot.projectId) {
            const QString providerName = optionalProviderDisplayName(
                m_diagnosticsProvider, Core::ProviderKind::Diagnostics);
            const QString source = snapshot.mock ? Tr::tr("MOCK") : Tr::tr("Online");
            QString status = Tr::tr("%1 | %2 %3 / %4")
                                 .arg(
                                     providerName,
                                     source,
                                     runModeName(snapshot.runMode),
                                     etherCATStateName(snapshot.masterState));
            if (m_diagnosticsState != Data::DiagnosticsStreamState::Running) {
                status = Tr::tr("%1 | %2 diagnostics %3: last %4 / %5")
                             .arg(
                                 providerName,
                                 source,
                                 streamStateName(m_diagnosticsState),
                                 runModeName(snapshot.runMode),
                                 etherCATStateName(snapshot.masterState));
            }
            if (snapshot.masterHasError)
                status += Tr::tr(" - Error");
            else if (snapshot.activeAlarmCount > 0)
                status += Tr::tr(" - %n active alarm(s)", nullptr, snapshot.activeAlarmCount);
            QString compactStatus
                = Tr::tr("%1 %2 / %3")
                      .arg(
                          source,
                          runModeName(snapshot.runMode),
                          etherCATStateName(snapshot.masterState));
            if (m_diagnosticsState != Data::DiagnosticsStreamState::Running) {
                compactStatus
                    = Tr::tr("%1 %2 · last %3 / %4")
                          .arg(
                              source,
                              streamStateName(m_diagnosticsState),
                              runModeName(snapshot.runMode),
                              etherCATStateName(snapshot.masterState));
            }
            if (snapshot.masterHasError) {
                compactStatus += Tr::tr(" · Error");
            } else if (snapshot.activeAlarmCount > 0) {
                compactStatus += Tr::tr(" · Alarms: %1").arg(snapshot.activeAlarmCount);
            }
            appendPresentationStatus(master, status, compactStatus);
            appendPresentationDetail(master, snapshot.masterAlStatusText);
            const StateMarker masterMarker = snapshot.masterHasError
                                                     || m_diagnosticsState
                                                            == Data::DiagnosticsStreamState::Failed
                                                 ? StateMarker::Error
                                                 : (m_diagnosticsState
                                                            == Data::DiagnosticsStreamState::Running
                                                        ? StateMarker::Healthy
                                                        : StateMarker::Information);
            raiseMarker(master, masterMarker);
            master->issue = masterMarker == StateMarker::Error || snapshot.activeAlarmCount > 0;

            Node *diagnostics = nodeForIndex(diagnosticsForProject(snapshot.projectId));
            if (diagnostics && diagnostics != m_root.get()) {
                QString diagnosticsStatus
                    = Tr::tr(
                          "%1 | %2 %3 - %n active alarm(s)",
                          nullptr,
                          snapshot.activeAlarmCount)
                          .arg(providerName, source, streamStateName(m_diagnosticsState));
                if (snapshot.masterHasError)
                    diagnosticsStatus += Tr::tr(" - Error");
                QString diagnosticsCompactStatus
                    = Tr::tr("%1 %2").arg(source, streamStateName(m_diagnosticsState));
                if (snapshot.activeAlarmCount > 0) {
                    diagnosticsCompactStatus
                        += Tr::tr(" · Alarms: %1").arg(snapshot.activeAlarmCount);
                }
                if (snapshot.masterHasError)
                    diagnosticsCompactStatus += Tr::tr(" · Error");
                appendPresentationStatus(
                    diagnostics, diagnosticsStatus, diagnosticsCompactStatus);
                const StateMarker diagnosticsMarker
                    = snapshot.masterHasError
                              || m_diagnosticsState == Data::DiagnosticsStreamState::Failed
                          ? StateMarker::Error
                          : (snapshot.activeAlarmCount > 0 ? StateMarker::Warning : masterMarker);
                raiseMarker(diagnostics, diagnosticsMarker);
                diagnostics->issue = diagnosticsMarker == StateMarker::Warning
                                     || diagnosticsMarker == StateMarker::Error;
            }

            QSet<Data::NodeId> reportedSlaves;
            for (const Data::SlaveDiagnostics &slave : snapshot.slaves) {
                Node *node = findNode(slave.nodeId);
                if (!node || node->kind != Core::WorkbenchNodeKind::ConfiguredSlave
                    || node->projectId != snapshot.projectId) {
                    continue;
                }
                reportedSlaves.insert(node->id);
                QString slaveStatus = Tr::tr("%1 %2").arg(source, etherCATStateName(slave.state));
                if (slave.hasError)
                    slaveStatus += Tr::tr(" - Error");
                QString slaveCompactStatus
                    = Tr::tr("%1 %2").arg(source, etherCATStateName(slave.state));
                if (slave.hasError)
                    slaveCompactStatus += Tr::tr(" · Error");
                appendPresentationStatus(node, slaveStatus, slaveCompactStatus);
                appendPresentationDetail(node, slave.alStatusText);
                raiseMarker(
                    node,
                    slave.hasError ? StateMarker::Error
                                   : (m_diagnosticsState == Data::DiagnosticsStreamState::Running
                                          ? StateMarker::Healthy
                                          : StateMarker::Information));
                node->issue = slave.hasError;
            }
            for (const std::unique_ptr<Node> &child : master->children) {
                if (child->kind != Core::WorkbenchNodeKind::ConfiguredSlave
                    || reportedSlaves.contains(child->id)) {
                    continue;
                }
                const QString absentStatus = Tr::tr("%1 not present").arg(source);
                appendPresentationStatus(child.get(), absentStatus, absentStatus);
                appendPresentationDetail(
                    child.get(),
                    Tr::tr("The configured slave is absent from the diagnostics snapshot."));
                raiseMarker(child.get(), StateMarker::Warning);
                child->issue = true;
            }
        }
    } else if (
        !m_diagnosticsRequest.masterId.isNull()
        && m_diagnosticsState != Data::DiagnosticsStreamState::Stopped) {
        Node *master = findNode(m_diagnosticsRequest.masterId);
        if (master && master->projectId == m_diagnosticsRequest.projectId) {
            const QString status
                = Tr::tr("%1 | Diagnostics %2")
                      .arg(
                          optionalProviderDisplayName(
                              m_diagnosticsProvider, Core::ProviderKind::Diagnostics),
                          streamStateName(m_diagnosticsState));
            const QString compactStatus
                = Tr::tr("Diagnostics %1").arg(streamStateName(m_diagnosticsState));
            appendPresentationStatus(master, status, compactStatus);
            raiseMarker(
                master,
                m_diagnosticsState == Data::DiagnosticsStreamState::Failed
                    ? StateMarker::Error
                    : StateMarker::Information);
            master->issue = m_diagnosticsState == Data::DiagnosticsStreamState::Failed;
            Node *diagnostics = nodeForIndex(diagnosticsForProject(m_diagnosticsRequest.projectId));
            if (diagnostics && diagnostics != m_root.get()) {
                appendPresentationStatus(diagnostics, status, compactStatus);
                raiseMarker(diagnostics, master->marker);
                diagnostics->issue = master->issue;
            }
        }
    }

    if (m_scanResult) {
        const Data::ScanResult &result = *m_scanResult;
        Node *master = findNode(result.snapshot.masterId);
        if (master && master->kind == Core::WorkbenchNodeKind::Master
            && master->projectId == result.snapshot.projectId) {
            const QString providerName = optionalProviderDisplayName(
                m_scanProvider, Core::ProviderKind::Scan);
            const QString source = result.snapshot.mock ? Tr::tr("MOCK") : Tr::tr("Online");
            if (result.snapshot.operation == Data::ScanOperation::Interfaces) {
                appendPresentationStatus(
                    master,
                    Tr::tr("%1 | %2 interface scan").arg(providerName, source),
                    Tr::tr("%1 interface scan").arg(source));
                raiseMarker(master, StateMarker::Information);
            } else {
                QList<const Data::TopologyDifference *> differences;
                for (const Data::TopologyDifference &difference : result.comparison.differences) {
                    const bool placeholder
                        = (difference.kind == Data::TopologyDifferenceKind::PdoConfiguration
                           || difference.kind == Data::TopologyDifferenceKind::DcConfiguration)
                          && difference.offlineSlaveId.isNull()
                          && difference.scannedSlaveId.isNull();
                    if (!placeholder)
                        differences.append(&difference);
                }

                QHash<Node *, QStringList> nodeDifferences;
                QSet<Data::NodeId> affectedOfflineSlaves;
                StateMarker aggregateMarker = StateMarker::None;
                for (int order = 0; order < differences.size(); ++order) {
                    const Data::TopologyDifference &difference = *differences.at(order);
                    Node *node = difference.offlineSlaveId.isNull()
                                     ? master
                                     : findNode(difference.offlineSlaveId);
                    if (!node || node->projectId != result.snapshot.projectId)
                        node = master;
                    if (!difference.offlineSlaveId.isNull())
                        affectedOfflineSlaves.insert(difference.offlineSlaveId);
                    const QString name = differenceName(difference.kind);
                    if (!nodeDifferences[node].contains(name))
                        nodeDifferences[node].append(name);
                    appendPresentationDetail(
                        node,
                        difference.detail.isEmpty()
                            ? difference.summary
                            : difference.summary + ": " + difference.detail);
                    node->topologyDifference = true;
                    node->differenceOrder = qMin(node->differenceOrder, order);
                    const StateMarker marker = markerForSeverity(difference.severity);
                    raiseMarker(node, marker);
                    if (markerPriority(marker) > markerPriority(aggregateMarker))
                        aggregateMarker = marker;
                    if (marker == StateMarker::Warning || marker == StateMarker::Error)
                        node->issue = true;
                }

                QString masterStatus;
                if (differences.isEmpty()) {
                    masterStatus = Tr::tr("%1 | %2 scan: topology matches")
                                       .arg(providerName, source);
                    aggregateMarker = StateMarker::Healthy;
                } else {
                    masterStatus = Tr::tr(
                                       "%1 | %2 scan: %n topology difference(s)",
                                       nullptr,
                                       differences.size())
                                       .arg(providerName, source);
                    if (nodeDifferences.contains(master)) {
                        masterStatus += " - " + nodeDifferences.value(master).join(", ");
                        nodeDifferences.remove(master);
                    }
                    master->topologyDifference = true;
                }
                const QString masterCompactStatus
                    = differences.isEmpty()
                          ? Tr::tr("%1 Match").arg(source)
                          : Tr::tr("%1 · Differences: %2")
                                .arg(source)
                                .arg(differences.size());
                appendPresentationStatus(master, masterStatus, masterCompactStatus);
                raiseMarker(master, aggregateMarker);
                master->issue = master->issue || aggregateMarker == StateMarker::Warning
                                || aggregateMarker == StateMarker::Error;

                for (auto iterator = nodeDifferences.cbegin(); iterator != nodeDifferences.cend();
                     ++iterator) {
                    appendPresentationStatus(
                        iterator.key(),
                        Tr::tr("%1 scan: %2").arg(source, iterator.value().join(", ")),
                        Tr::tr("%1 · %2").arg(source, iterator.value().join(", ")));
                }

                const bool fullMasterScan
                    = result.snapshot.operation == Data::ScanOperation::Slaves
                      || (result.snapshot.operation == Data::ScanOperation::SelectedBranch
                          && result.snapshot.branchNodeId == result.snapshot.masterId);
                for (const std::unique_ptr<Node> &child : master->children) {
                    if (child->kind != Core::WorkbenchNodeKind::ConfiguredSlave)
                        continue;
                    const bool inScope = fullMasterScan
                                         || (result.snapshot.operation
                                                 == Data::ScanOperation::SelectedBranch
                                             && result.snapshot.branchNodeId == child->id);
                    if (!inScope || affectedOfflineSlaves.contains(child->id))
                        continue;
                    appendPresentationStatus(
                        child.get(),
                        Tr::tr("%1 scan: Matched").arg(source),
                        Tr::tr("%1 Match").arg(source));
                    raiseMarker(child.get(), StateMarker::Healthy);
                }
            }
        }
    }

    for (const PreviousPresentation &entry : std::as_const(previous)) {
        Node *node = entry.node;
        node->status = node->presentationStatus.isEmpty() ? node->baseStatus
                                                          : node->presentationStatus.join(" | ");
        node->compactStatus
            = node->presentationCompactStatus.isEmpty()
                  ? node->baseCompactStatus
                  : node->presentationCompactStatus.join(" · ");
        if (entry.status == node->status && entry.compactStatus == node->compactStatus
            && entry.details == node->presentationDetails
            && entry.marker == node->marker && entry.topologyDifference == node->topologyDifference
            && entry.issue == node->issue && entry.differenceOrder == node->differenceOrder) {
            continue;
        }
        emit dataChanged(
            indexForNode(node, 0),
            indexForNode(node, 1),
            {Qt::DisplayRole,
             Qt::DecorationRole,
             Qt::ToolTipRole,
             Qt::AccessibleTextRole,
             Qt::AccessibleDescriptionRole,
             StatusRole,
             SearchTextRole});
    }
}

void WorkbenchTreeModel::rebuild()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    m_repositoryNode = nullptr;

    QList<Data::ProjectSnapshot> projects = m_projects;
    std::sort(projects.begin(), projects.end(), [](const auto &left, const auto &right) {
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    if (projects.isEmpty()) {
        m_root->children.push_back(makeNode(
            m_root.get(),
            derivedNodeId("projects:empty"),
            {},
            Core::WorkbenchNodeKind::Placeholder,
            Tr::tr("No EtherCAT project is open"),
            Tr::tr("Create or open an .ecatproject file"),
            Tr::tr("Open/create project")));
    }

    for (const Data::ProjectSnapshot &project : std::as_const(projects)) {
        if (!project.valid) {
            auto projectNode = makeNode(
                m_root.get(),
                project.id,
                project.id,
                Core::WorkbenchNodeKind::Project,
                project.name,
                Tr::tr("Invalid project | Offline data unavailable"),
                Tr::tr("Invalid · Offline"));
            applyInvalidProjectPresentation(projectNode.get(), project);
            projectNode->children.push_back(makeNode(
                projectNode.get(),
                derivedNodeId(project.id.toString() + ":configuration-unavailable"),
                project.id,
                Core::WorkbenchNodeKind::Placeholder,
                Tr::tr("Project configuration unavailable"),
                Tr::tr("Fix the project file and reopen it"),
                Tr::tr("Fix and reopen")));
            m_root->children.push_back(std::move(projectNode));
            continue;
        }

        QHash<Data::NodeId, const Data::ProjectNodeSnapshot *> snapshots;
        for (const Data::ProjectNodeSnapshot &node : project.nodes)
            snapshots.insert(node.id, &node);

        QHash<Data::NodeId, int> configuredSlavePositions;
        for (const Data::OfflineSlaveConfiguration &slave : project.slaves)
            configuredSlavePositions.insert(slave.id, slave.position);

        const auto appendChildren = [this, &project, &snapshots, &configuredSlavePositions](
                                        const auto &self,
                                        Node *parent,
                                        const Data::NodeId &parentId) -> void {
            QList<const Data::ProjectNodeSnapshot *> children;
            for (const Data::ProjectNodeSnapshot *candidate : snapshots) {
                if (candidate->parentId == parentId)
                    children.append(candidate);
            }
            const bool usePhysicalSlaveOrder
                = !children.isEmpty()
                  && std::all_of(
                      children.cbegin(),
                      children.cend(),
                      [&configuredSlavePositions](const auto *child) {
                          return child->kind == Data::ProjectNodeKind::Slave
                                 && configuredSlavePositions.contains(child->id);
                      });
            std::sort(
                children.begin(),
                children.end(),
                [&configuredSlavePositions, usePhysicalSlaveOrder](const auto *left,
                                                                   const auto *right) {
                    if (usePhysicalSlaveOrder) {
                        const int leftPosition = configuredSlavePositions.value(left->id);
                        const int rightPosition = configuredSlavePositions.value(right->id);
                        if (leftPosition != rightPosition)
                            return leftPosition < rightPosition;
                    }
                    const int nameOrder = left->name.compare(right->name, Qt::CaseInsensitive);
                    if (nameOrder != 0)
                        return nameOrder < 0;
                    return usePhysicalSlaveOrder && left->id.toString() < right->id.toString();
                });
            for (const Data::ProjectNodeSnapshot *snapshot : std::as_const(children)) {
                const Core::WorkbenchNodeKind kind = workbenchKind(snapshot->kind);
                auto node = makeNode(
                    parent,
                    snapshot->id,
                    project.id,
                    kind,
                    snapshot->name,
                    projectStatus(kind));
                Node *nodePointer = node.get();
                if (kind == Core::WorkbenchNodeKind::ConfiguredSlave) {
                    const auto offlineSlave = std::find_if(
                        project.slaves.cbegin(),
                        project.slaves.cend(),
                        [snapshot](const auto &slave) { return slave.id == snapshot->id; });
                    if (offlineSlave != project.slaves.cend())
                        node->ownerSlaveId = offlineSlave->id;
                }
                parent->children.push_back(std::move(node));
                self(self, nodePointer, snapshot->id);
                if (kind == Core::WorkbenchNodeKind::ConfiguredSlave) {
                    const auto offlineSlave = std::find_if(
                        project.slaves.cbegin(),
                        project.slaves.cend(),
                        [snapshot](const auto &slave) { return slave.id == snapshot->id; });
                    if (offlineSlave != project.slaves.cend())
                        appendConfiguredSlaveChildren(nodePointer, *offlineSlave);
                }
                if (kind == Core::WorkbenchNodeKind::Master) {
                    const int slaveCount = int(std::count_if(
                        nodePointer->children.cbegin(),
                        nodePointer->children.cend(),
                        [](const auto &child) {
                            return child->kind == Core::WorkbenchNodeKind::ConfiguredSlave;
                        }));
                    if (slaveCount > 0) {
                        nodePointer->baseStatus
                            = Tr::tr("%n configured slave(s)", nullptr, slaveCount);
                        nodePointer->baseCompactStatus = nodePointer->baseStatus;
                        nodePointer->status = nodePointer->baseStatus;
                        nodePointer->compactStatus = nodePointer->baseCompactStatus;
                    }
                    if (shouldShowDiagnosticsNode(m_diagnosticsProvider)) {
                        nodePointer->children.push_back(makeNode(
                            nodePointer,
                            derivedNodeId(snapshot->id.toString() + ":diagnostics"),
                            project.id,
                            Core::WorkbenchNodeKind::Diagnostics,
                            Tr::tr("Diagnostics"),
                            optionalProviderStatus(
                                m_diagnosticsProvider, Core::ProviderKind::Diagnostics),
                            optionalProviderCompactStatus(
                                m_diagnosticsProvider, Core::ProviderKind::Diagnostics)));
                    }
                    if (slaveCount == 0) {
                        nodePointer->children.push_back(makeNode(
                            nodePointer,
                            derivedNodeId(snapshot->id.toString() + ":slaves-empty"),
                            project.id,
                            Core::WorkbenchNodeKind::Placeholder,
                            Tr::tr("No configured slaves"),
                            optionalProviderStatus(
                                m_scanProvider, Core::ProviderKind::Scan),
                            optionalProviderCompactStatus(
                                m_scanProvider, Core::ProviderKind::Scan)));
                    }
                }
            }
        };
        appendChildren(appendChildren, m_root.get(), {});
    }

    auto repository = makeNode(
        m_root.get(),
        derivedNodeId("device-repository"),
        {},
        Core::WorkbenchNodeKind::DeviceRepository,
        Tr::tr("Device Repository"),
        Tr::tr("Offline ESI library"));
    m_repositoryNode = repository.get();
    m_root->children.push_back(std::move(repository));
    endResetModel();

    const QList<Data::DeviceSummary> devices = m_devices;
    m_devices.clear();
    syncDevices(devices);
    updateProviderPresentation();
}

} // namespace EtherCAT::Workbench::Internal
