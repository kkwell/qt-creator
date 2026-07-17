// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QAbstractItemModel>

#include <functional>
#include <memory>

namespace EtherCAT::Workbench::Internal {

class WorkbenchTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    using DeviceDropHandler
        = std::function<bool(const Data::NodeId &deviceId, const Data::NodeId &masterId)>;

    enum Role {
        NodeIdRole = Qt::UserRole + 1,
        NodeKindRole,
        ProjectIdRole,
        StatusRole,
        SearchTextRole,
    };

    struct Node;

    explicit WorkbenchTreeModel(QObject *parent = nullptr);
    ~WorkbenchTreeModel() final;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const final;
    QModelIndex parent(const QModelIndex &child) const final;
    int rowCount(const QModelIndex &parent = {}) const final;
    int columnCount(const QModelIndex &parent = {}) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final;
    Qt::ItemFlags flags(const QModelIndex &index) const final;
    Qt::DropActions supportedDragActions() const final;
    Qt::DropActions supportedDropActions() const final;
    QStringList mimeTypes() const final;
    QMimeData *mimeData(const QModelIndexList &indexes) const final;
    bool canDropMimeData(
        const QMimeData *data,
        Qt::DropAction action,
        int row,
        int column,
        const QModelIndex &parent) const final;
    bool dropMimeData(
        const QMimeData *data,
        Qt::DropAction action,
        int row,
        int column,
        const QModelIndex &parent) final;
    QHash<int, QByteArray> roleNames() const final;

    void setProjects(const QList<Data::ProjectSnapshot> &projects);
    void setDropTargetMasterId(const Data::NodeId &masterId);
    void setDeviceDropHandler(DeviceDropHandler handler);
    void syncDevices(const QList<Data::DeviceSummary> &devices);
    void setOptionalProviders(bool scanAvailable, bool diagnosticsAvailable);
    void setScanPresentation(const std::optional<Data::ScanResult> &result);
    void setDiagnosticsPresentation(
        Data::DiagnosticsStreamState state,
        const Data::DiagnosticsRequest &request,
        const std::optional<Data::DiagnosticsSnapshot> &snapshot);
    void clear();

    QModelIndex indexForNodeId(const Data::NodeId &nodeId, int column = 0) const;
    QModelIndex firstUnsupportedDevice() const;
    QModelIndex firstTopologyDifference() const;
    QModelIndex firstIssue() const;
    QModelIndex diagnosticsForProject(const Data::NodeId &projectId) const;
    Core::PropertyPageContext contextForIndex(const QModelIndex &index) const;
    Core::PropertyPageContext contextForNodeId(const Data::NodeId &nodeId) const;
    Data::NodeId sourceNodeId(const QModelIndex &index) const;
    Data::NodeId sourceNodeId(const Data::NodeId &nodeId) const;
    std::optional<Data::OfflineSlaveConfiguration> offlineSlave(
        const Data::NodeId &nodeId) const;
    QList<Data::OfflineSlaveConfiguration> offlineSlavesForMaster(
        const Data::NodeId &masterId) const;

private:
    QModelIndex indexForNode(const Node *node, int column = 0) const;
    Node *nodeForIndex(const QModelIndex &index) const;
    Node *findNode(const Data::NodeId &nodeId) const;
    Data::NodeId deviceIdFromMimeData(const QMimeData *data) const;
    void updateOptionalProviderStatus();
    void updateProviderPresentation();
    void rebuild();

    std::unique_ptr<Node> m_root;
    Node *m_repositoryNode = nullptr;
    QList<Data::ProjectSnapshot> m_projects;
    QList<Data::DeviceSummary> m_devices;
    Data::NodeId m_dropTargetMasterId;
    DeviceDropHandler m_deviceDropHandler;
    bool m_scanAvailable = false;
    bool m_diagnosticsAvailable = false;
    std::optional<Data::ScanResult> m_scanResult;
    Data::DiagnosticsStreamState m_diagnosticsState = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_diagnosticsRequest;
    std::optional<Data::DiagnosticsSnapshot> m_diagnosticsSnapshot;
};

} // namespace EtherCAT::Workbench::Internal
