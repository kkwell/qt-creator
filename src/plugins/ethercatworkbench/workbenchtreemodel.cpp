// Copyright (C) 2026 Kvell

#include "workbenchtreemodel.h"

#include "ethercatworkbenchtr.h"

#include <coreplugin/coreicons.h>

#include <utils/utilsicons.h>

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <vector>

namespace EtherCAT::Workbench::Internal {

struct WorkbenchTreeModel::Node
{
    Data::NodeId id;
    Data::NodeId projectId;
    Core::WorkbenchNodeKind kind = Core::WorkbenchNodeKind::None;
    QString name;
    QString status;
    Data::DeviceSummary device;
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
    default:
        return {};
    }
}

static std::unique_ptr<WorkbenchTreeModel::Node> makeNode(
    WorkbenchTreeModel::Node *parent,
    const Data::NodeId &id,
    const Data::NodeId &projectId,
    Core::WorkbenchNodeKind kind,
    const QString &name,
    const QString &status)
{
    auto node = std::make_unique<WorkbenchTreeModel::Node>();
    node->parent = parent;
    node->id = id;
    node->projectId = projectId;
    node->kind = kind;
    node->name = name;
    node->status = status;
    return node;
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

    if (role == Qt::DisplayRole)
        return index.column() == 0 ? node->name : node->status;
    if (role == NodeIdRole)
        return QVariant::fromValue(node->id);
    if (role == NodeKindRole)
        return QVariant::fromValue(node->kind);
    if (role == ProjectIdRole)
        return QVariant::fromValue(node->projectId);
    if (role == StatusRole)
        return node->status;
    if (role == SearchTextRole) {
        QString text = node->name + ' ' + node->status;
        if (node->kind == Core::WorkbenchNodeKind::Device) {
            text += QString(" %1 %2 %3 %4")
                        .arg(node->device.identity.vendorId, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.productCode, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.revisionNumber, 8, 16, QLatin1Char('0'))
                        .arg(node->device.group);
        }
        return text;
    }
    if (role == Qt::ToolTipRole) {
        QString text = node->name;
        if (!node->status.isEmpty())
            text += "\n" + node->status;
        if (node->kind == Core::WorkbenchNodeKind::Device) {
            text += Tr::tr("\nVendor: 0x%1\nProduct: 0x%2\nRevision: 0x%3")
                        .arg(node->device.identity.vendorId, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.productCode, 8, 16, QLatin1Char('0'))
                        .arg(node->device.identity.revisionNumber, 8, 16, QLatin1Char('0'));
        }
        return text;
    }
    if (role == Qt::DecorationRole && index.column() == 0) {
        switch (node->kind) {
        case Core::WorkbenchNodeKind::Project:
            return Utils::Icons::PROJECT.icon();
        case Core::WorkbenchNodeKind::Target:
        case Core::WorkbenchNodeKind::Master:
        case Core::WorkbenchNodeKind::DeviceRepository:
            return Utils::Icons::SETTINGS.icon();
        case Core::WorkbenchNodeKind::Device:
            return node->device.supported ? ::Core::Icons::DESKTOP_DEVICE_SMALL.icon()
                                          : Utils::Icons::BROKEN.icon();
        case Core::WorkbenchNodeKind::Diagnostics:
            return Utils::Icons::INFO.icon();
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
    return result;
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
            Tr::tr("Use the import command in a later UI stage")));
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
            node->status = status;
            node->device = summary;
            emit dataChanged(
                index(desiredRow, 0, repositoryIndex),
                index(desiredRow, 1, repositoryIndex));
        }
    }
}

void WorkbenchTreeModel::setOptionalProviders(bool scanAvailable, bool diagnosticsAvailable)
{
    if (m_scanAvailable == scanAvailable && m_diagnosticsAvailable == diagnosticsAvailable)
        return;
    m_scanAvailable = scanAvailable;
    m_diagnosticsAvailable = diagnosticsAvailable;
    updateOptionalProviderStatus();
}

void WorkbenchTreeModel::clear()
{
    m_projects.clear();
    m_devices.clear();
    rebuild();
}

QModelIndex WorkbenchTreeModel::indexForNodeId(const Data::NodeId &nodeId, int column) const
{
    if (nodeId.isNull())
        return {};
    return indexForNode(findNode(nodeId), column);
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

void WorkbenchTreeModel::updateOptionalProviderStatus()
{
    const auto updateRecursive = [this](const auto &self, Node *parent) -> void {
        for (const std::unique_ptr<Node> &child : parent->children) {
            QString status = child->status;
            if (child->kind == Core::WorkbenchNodeKind::Diagnostics) {
                status = m_diagnosticsAvailable ? Tr::tr("Provider available")
                                                : Tr::tr("Plugin not installed");
            } else if (child->kind == Core::WorkbenchNodeKind::Placeholder
                       && child->parent
                       && child->parent->kind == Core::WorkbenchNodeKind::Master) {
                status = m_scanAvailable ? Tr::tr("Ready to scan")
                                         : Tr::tr("Scan plugin not installed");
            }
            if (status != child->status) {
                child->status = status;
                emit dataChanged(
                    indexForNode(child.get(), 0),
                    indexForNode(child.get(), 1),
                    {Qt::DisplayRole, StatusRole, SearchTextRole, Qt::ToolTipRole});
            }
            self(self, child.get());
        }
    };
    updateRecursive(updateRecursive, m_root.get());
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
            Tr::tr("Create or open an .ecatproject file")));
    }

    for (const Data::ProjectSnapshot &project : std::as_const(projects)) {
        QHash<Data::NodeId, const Data::ProjectNodeSnapshot *> snapshots;
        for (const Data::ProjectNodeSnapshot &node : project.nodes)
            snapshots.insert(node.id, &node);

        const auto appendChildren = [this, &project, &snapshots](
                                        const auto &self,
                                        Node *parent,
                                        const Data::NodeId &parentId) -> void {
            QList<const Data::ProjectNodeSnapshot *> children;
            for (const Data::ProjectNodeSnapshot *candidate : snapshots) {
                if (candidate->parentId == parentId)
                    children.append(candidate);
            }
            std::sort(children.begin(), children.end(), [](const auto *left, const auto *right) {
                return left->name.compare(right->name, Qt::CaseInsensitive) < 0;
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
                parent->children.push_back(std::move(node));
                self(self, nodePointer, snapshot->id);
                if (kind == Core::WorkbenchNodeKind::Master) {
                    nodePointer->children.push_back(makeNode(
                        nodePointer,
                        derivedNodeId(snapshot->id.toString() + ":diagnostics"),
                        project.id,
                        Core::WorkbenchNodeKind::Diagnostics,
                        Tr::tr("Diagnostics"),
                        m_diagnosticsAvailable ? Tr::tr("Provider available")
                                               : Tr::tr("Plugin not installed")));
                    nodePointer->children.push_back(makeNode(
                        nodePointer,
                        derivedNodeId(snapshot->id.toString() + ":slaves-empty"),
                        project.id,
                        Core::WorkbenchNodeKind::Placeholder,
                        Tr::tr("No configured slaves"),
                        m_scanAvailable ? Tr::tr("Ready to scan")
                                        : Tr::tr("Scan plugin not installed")));
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
}

} // namespace EtherCAT::Workbench::Internal
