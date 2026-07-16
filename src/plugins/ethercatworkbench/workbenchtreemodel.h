// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QAbstractItemModel>

#include <memory>

namespace EtherCAT::Workbench::Internal {

class WorkbenchTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
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
    QHash<int, QByteArray> roleNames() const final;

    void setProjects(const QList<Data::ProjectSnapshot> &projects);
    void syncDevices(const QList<Data::DeviceSummary> &devices);
    void setOptionalProviders(bool scanAvailable, bool diagnosticsAvailable);
    void clear();

    QModelIndex indexForNodeId(const Data::NodeId &nodeId, int column = 0) const;
    QModelIndex firstUnsupportedDevice() const;
    Core::PropertyPageContext contextForIndex(const QModelIndex &index) const;
    Core::PropertyPageContext contextForNodeId(const Data::NodeId &nodeId) const;

private:
    QModelIndex indexForNode(const Node *node, int column = 0) const;
    Node *nodeForIndex(const QModelIndex &index) const;
    Node *findNode(const Data::NodeId &nodeId) const;
    void updateOptionalProviderStatus();
    void rebuild();

    std::unique_ptr<Node> m_root;
    Node *m_repositoryNode = nullptr;
    QList<Data::ProjectSnapshot> m_projects;
    QList<Data::DeviceSummary> m_devices;
    bool m_scanAvailable = false;
    bool m_diagnosticsAvailable = false;
};

} // namespace EtherCAT::Workbench::Internal
