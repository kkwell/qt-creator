// Copyright (C) 2026 Kvell

#pragma once

#include "workbenchtreemodel.h"

#include <utils/result.h>

#include <QList>
#include <QObject>
#include <QPointer>

namespace EtherCAT::Core {
class ProviderRegistry;
class SelectionService;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchController final : public QObject
{
    Q_OBJECT

public:
    explicit WorkbenchController(QObject *parent = nullptr);
    ~WorkbenchController() final;

    WorkbenchTreeModel *treeModel();
    Core::SelectionService *selectionService() const;
    Core::ProjectService *projectService() const;
    Core::DeviceRepositoryProvider *deviceRepository() const;
    Core::ProviderRegistry *providerRegistry() const;
    OptionalProviderPresentation scanProviderPresentation() const;
    OptionalProviderPresentation diagnosticsProviderPresentation() const;
    bool scanAvailable() const;
    bool diagnosticsAvailable() const;
    bool canInsertDeviceOnSelectedMaster() const;
    bool canAddSelectedDeviceToMaster() const;
    bool canRemoveSelectedOfflineSlave() const;
    bool canMoveSelectedOfflineSlaveUp() const;
    bool canMoveSelectedOfflineSlaveDown() const;
    bool canActivateSelectedProject() const;

    void refresh();
    void shutdown();
    Data::NodeId selectedOfflineMasterId() const;
    Utils::Result<> addDeviceToMaster(
        const Data::NodeId &deviceId, const Data::NodeId &masterId);
    Utils::Result<> addSelectedDeviceToMaster();
    Utils::Result<> removeSelectedOfflineSlave();
    Utils::Result<> moveSelectedOfflineSlaveUp();
    Utils::Result<> moveSelectedOfflineSlaveDown();
    Utils::Result<> activateSelectedProject();
    Utils::Result<> renameProject(const Data::NodeId &projectId, const QString &name);
    Utils::Result<> renameOfflineSlave(
        const Data::NodeId &projectId, const Data::NodeId &slaveId, const QString &name);
    Utils::Result<> renameStructuralNode(
        const Data::NodeId &projectId, const Data::NodeId &nodeId, const QString &name);
    Utils::Result<> setOfflineSlaveAlias(
        const Data::NodeId &projectId, const Data::NodeId &slaveId, quint16 alias);

signals:
    void expandAllRequested();
    void collapseAllRequested();
    void locateFirstTopologyDifferenceRequested();
    void locateFirstIssueRequested();
    void openDiagnosticsRequested();
    void locateUnsupportedDeviceRequested();
    void copyCurrentNodeIdRequested();
    void insertDeviceRequested();
    void diagnosticsProviderChanged(bool availabilityChanged);

private:
    void refreshProjects();
    void refreshDevices();
    void watchOptionalProvider(Core::Provider *provider);
    void refreshOptionalProviders(Core::Provider *excluding = nullptr);
    void handleOptionalAvailabilityChanged();

    WorkbenchTreeModel m_treeModel;
    QPointer<Core::SelectionService> m_selectionService;
    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::DeviceRepositoryProvider> m_deviceRepository;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    QList<QMetaObject::Connection> m_connections;
    bool m_shuttingDown = false;
    OptionalProviderPresentation m_scanProvider;
    OptionalProviderPresentation m_diagnosticsProvider;
};

} // namespace EtherCAT::Workbench::Internal
