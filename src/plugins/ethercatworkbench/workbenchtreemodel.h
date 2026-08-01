// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QAbstractItemModel>
#include <QPointer>

#include <functional>
#include <memory>

namespace EtherCAT::Workbench::Internal {

enum class OptionalProviderState { Absent, Unavailable, Available };

struct OptionalProviderPresentation
{
    OptionalProviderState state = OptionalProviderState::Absent;
    QString displayName;

    bool isAvailable() const { return state == OptionalProviderState::Available; }

    friend bool operator==(
        const OptionalProviderPresentation &, const OptionalProviderPresentation &)
        = default;
};

QString optionalProviderDisplayName(
    const OptionalProviderPresentation &provider, Core::ProviderKind kind);

struct SemanticControlSelection
{
    Data::ControllerConnectionScope scope;
    Data::NodeId deviceId;
    QList<Data::SemanticSignalId> signalIds;
    bool wholeDevice = true;

    friend bool operator==(const SemanticControlSelection &, const SemanticControlSelection &)
        = default;
};

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
    void setActiveProjectId(const Data::NodeId &projectId);
    void setDropTargetMasterId(const Data::NodeId &masterId);
    void setDeviceDropHandler(DeviceDropHandler handler);
    void setDeviceAdapterProviders(const QList<Core::DeviceAdapterProvider *> &providers);
    void invalidateDeviceAdapterProviders();
    void syncDevices(const QList<Data::DeviceSummary> &devices);
    void setProviderPresentations(
        const OptionalProviderPresentation &scanProvider,
        const OptionalProviderPresentation &diagnosticsProvider,
        const std::optional<Data::ScanResult> &scanResult,
        Data::DiagnosticsStreamState diagnosticsState,
        const Data::DiagnosticsRequest &diagnosticsRequest,
        const std::optional<Data::DiagnosticsSnapshot> &diagnosticsSnapshot);
    void setControllerConnections(
        const QList<Data::ControllerConnectionSnapshot> &controllerConnections);
    void clear();

    QModelIndex indexForNodeId(const Data::NodeId &nodeId, int column = 0) const;
    QModelIndex firstUnsupportedDevice() const;
    QModelIndex firstTopologyDifference(const Data::NodeId &projectId = {}) const;
    QModelIndex firstIssue(const Data::NodeId &projectId = {}) const;
    QModelIndex diagnosticsForProject(const Data::NodeId &projectId) const;
    Core::PropertyPageContext contextForIndex(const QModelIndex &index) const;
    Core::PropertyPageContext contextForNodeId(const Data::NodeId &nodeId) const;
    std::optional<SemanticControlSelection> semanticControlSelection(
        const Data::NodeId &nodeId) const;
    Data::NodeId sourceNodeId(const QModelIndex &index) const;
    Data::NodeId sourceNodeId(const Data::NodeId &nodeId) const;
    std::optional<Data::OfflineSlaveConfiguration> offlineSlave(
        const Data::NodeId &nodeId) const;
    std::optional<Data::ControllerTopologySlave> controllerTopologySlave(
        const Data::NodeId &nodeId) const;
    QList<Data::OfflineSlaveConfiguration> offlineSlavesForMaster(
        const Data::NodeId &masterId) const;

private:
    QModelIndex indexForNode(const Node *node, int column = 0) const;
    Node *nodeForIndex(const QModelIndex &index) const;
    Node *findNode(const Data::NodeId &nodeId) const;
    QString visibleStatus(const Node *node) const;
    QString visibleCompactStatus(const Node *node) const;
    Data::NodeId deviceIdFromMimeData(const QMimeData *data) const;
    void updateOptionalProviderStatus();
    void updateProviderPresentation();
    void rebuild();

    std::unique_ptr<Node> m_root;
    Node *m_repositoryNode = nullptr;
    QList<Data::ProjectSnapshot> m_projects;
    QList<Data::DeviceSummary> m_devices;
    Data::NodeId m_activeProjectId;
    Data::NodeId m_dropTargetMasterId;
    DeviceDropHandler m_deviceDropHandler;
    QList<QPointer<Core::DeviceAdapterProvider>> m_deviceAdapterProviders;
    OptionalProviderPresentation m_scanProvider;
    OptionalProviderPresentation m_diagnosticsProvider;
    std::optional<Data::ScanResult> m_scanResult;
    Data::DiagnosticsStreamState m_diagnosticsState = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_diagnosticsRequest;
    std::optional<Data::DiagnosticsSnapshot> m_diagnosticsSnapshot;
    QList<Data::ControllerConnectionSnapshot> m_controllerConnections;
    QString m_controllerTopologyFingerprint;
};

} // namespace EtherCAT::Workbench::Internal
