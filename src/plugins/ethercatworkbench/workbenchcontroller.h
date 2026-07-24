// Copyright (C) 2026 Kvell

#pragma once

#include "workbenchtreemodel.h"

#include <utils/id.h>
#include <utils/result.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

namespace EtherCAT::Core {
class ControllerConnectionProvider;
class ProviderRegistry;
class SelectionService;
} // namespace EtherCAT::Core

namespace EtherCAT::Workbench::Internal {

class EtherCATWorkbenchTests;

struct OfflineSlaveRemovalCandidate
{
    Data::NodeId projectId;
    Data::NodeId masterId;
    Data::NodeId slaveId;
    QString name;
    int position = -1;
    Data::OfflineSlaveConfiguration expectedSlave;
};

struct OfflineMasterTarget
{
    Data::NodeId projectId;
    Data::NodeId masterId;
    QString projectName;
    QString masterName;
};

struct DiagnosticsStatusPresentation
{
    OptionalProviderPresentation provider;
    Data::DiagnosticsStreamState streamState = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest request;
    std::optional<Data::DiagnosticsRunMode> runMode;
    std::optional<Data::EtherCATState> masterState;
    bool mock = false;
    bool masterHasError = false;
    int activeAlarmCount = 0;

    friend bool operator==(
        const DiagnosticsStatusPresentation &, const DiagnosticsStatusPresentation &) = default;
};

struct ControllerConnectionSelection
{
    Data::ControllerConnectionScope scope;
    Utils::Id providerId;
    Data::NodeId profileId;
    bool providerExplicitlySelected = false;
    bool profileExplicitlySelected = false;

    friend bool operator==(
        const ControllerConnectionSelection &, const ControllerConnectionSelection &) = default;
};

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
    DiagnosticsStatusPresentation diagnosticsStatusPresentation() const;
    QList<Core::ControllerConnectionProvider *> controllerConnectionProviders() const;
    std::optional<Data::ControllerConnectionScope> selectedControllerConnectionScope() const;
    ControllerConnectionSelection prepareControllerConnection(
        const Data::ControllerConnectionScope &scope);
    ControllerConnectionSelection controllerConnectionSelection(
        const Data::ControllerConnectionScope &scope) const;
    Core::ControllerConnectionProvider *controllerConnectionProvider(
        const Data::ControllerConnectionScope &scope) const;
    QList<Data::ControllerConnectionProfile> controllerConnectionProfiles(
        const Data::ControllerConnectionScope &scope) const;
    Data::ControllerConnectionSnapshot controllerConnectionSnapshot(
        const Data::ControllerConnectionScope &scope) const;
    bool controllerConnectionProjectIsOpen(const Data::ControllerConnectionScope &scope) const;
    Utils::Result<> selectControllerConnectionProvider(
        const Data::ControllerConnectionScope &scope, Utils::Id providerId);
    Utils::Result<> selectControllerConnectionProfile(
        const Data::ControllerConnectionScope &scope, const Data::NodeId &profileId);
    bool controllerConnectionSelectionLocked(const Data::ControllerConnectionScope &scope) const;
    bool canConnectSelectedController() const;
    bool canDisconnectSelectedController() const;
    bool canRefreshSelectedController() const;
    Utils::Result<> connectSelectedController();
    Utils::Result<> disconnectSelectedController();
    Utils::Result<> refreshSelectedController();
    bool scanAvailable() const;
    bool diagnosticsAvailable() const;
    bool canInsertDeviceOnSelectedMaster() const;
    bool canAddSelectedDeviceToMaster() const;
    bool canRemoveSelectedOfflineSlave() const;
    bool canMoveSelectedOfflineSlaveUp() const;
    bool canMoveSelectedOfflineSlaveDown() const;
    bool canActivateSelectedProject() const;
    bool canCopyNodeId(const Data::NodeId &nodeId) const;

    void refresh();
    void shutdown();
    std::optional<OfflineMasterTarget> activeOfflineMasterTarget() const;
    Data::NodeId selectedOfflineMasterId() const;
    Utils::Result<> addDeviceToMaster(const Data::NodeId &deviceId, const Data::NodeId &masterId);
    Utils::Result<> addSelectedDeviceToMaster(const OfflineMasterTarget &target);
    std::optional<OfflineSlaveRemovalCandidate> selectedOfflineSlaveRemovalCandidate() const;
    Utils::Result<> removeOfflineSlave(const OfflineSlaveRemovalCandidate &candidate);
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
    void diagnosticsStatusChanged();
    void controllerConnectionChanged();

private:
    friend class EtherCATWorkbenchTests;

    void refreshProjects();
    void refreshDevices();
    void watchOptionalProvider(Core::Provider *provider);
    void watchControllerConnectionProvider(Core::Provider *provider);
    void refreshOptionalProviders(Core::Provider *excluding = nullptr);
    void handleOptionalAvailabilityChanged();
    void handleProjectAboutToBeRemoved(const Data::NodeId &projectId);
    void handleControllerConnectionChanged();
    void requestControllerDisconnect(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &expectedScope,
        quint64 expectedGeneration);
    void requestControllerDisconnectAttempt(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &expectedScope,
        quint64 expectedGeneration,
        quint64 expectedProviderEpoch,
        int retryStep);
    ControllerConnectionSelection *mutableControllerConnectionSelection(
        const Data::ControllerConnectionScope &scope);

    WorkbenchTreeModel m_treeModel;
    QPointer<Core::SelectionService> m_selectionService;
    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::DeviceRepositoryProvider> m_deviceRepository;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    QList<QMetaObject::Connection> m_connections;
    bool m_shuttingDown = false;
    OptionalProviderPresentation m_scanProvider;
    OptionalProviderPresentation m_diagnosticsProvider;
    DiagnosticsStatusPresentation m_diagnosticsStatus;
    QList<ControllerConnectionSelection> m_controllerConnectionSelections;
    QHash<Core::ControllerConnectionProvider *, quint64> m_controllerConnectionProviderEpochs;
    quint64 m_nextControllerConnectionProviderEpoch = 0;
    bool m_suppressControllerConnectionChanges = false;
};

} // namespace EtherCAT::Workbench::Internal
