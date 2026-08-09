// Copyright (C) 2026 Kvell

#pragma once

#include "workbenchtreemodel.h"

#include <ethercatcore/runtimepackageactivationservice.h>
#include <ethercatcore/scanproviderselectionservice.h>
#include <ethercatcore/topologyservice.h>

#include <utils/id.h>
#include <utils/result.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <optional>

namespace EtherCAT::Core {
class ControllerConnectionProvider;
class Provider;
class ProviderRegistry;
class ScanProvider;
class ScanProviderSelectionService;
struct ProviderStartupDiagnostic;
class SelectionService;
} // namespace EtherCAT::Core

namespace EtherCAT::Workbench::Internal {

class EtherCATWorkbenchTests;
class RuntimePackageCompilerPreparationBridge;

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

enum class ControllerQuickControlAction {
    Run,
    Debug,
    Stop,
};

enum class ControllerOutputLevel {
    Information,
    Warning,
    Error,
};

struct ProviderStartupOutput
{
    QString message;
    ControllerOutputLevel level = ControllerOutputLevel::Information;
    bool reveal = false;

    friend bool operator==(const ProviderStartupOutput &, const ProviderStartupOutput &) = default;
};

QList<ProviderStartupOutput> providerStartupOutput(Core::Provider *provider);
std::optional<ProviderStartupOutput> providerStartupOutput(
    const Core::ProviderStartupDiagnostic &diagnostic);

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
    OptionalProviderPresentation scanProviderPresentation(
        const Data::ControllerConnectionScope &scope) const;
    QList<Core::ScanProvider *> scanProviders() const;
    std::optional<Core::ScanProviderSelection> scanProviderSelection(
        const Data::ControllerConnectionScope &scope) const;
    Utils::Result<> selectScanProvider(
        const Data::ControllerConnectionScope &scope, Utils::Id providerId);
    OptionalProviderPresentation diagnosticsProviderPresentation() const;
    DiagnosticsStatusPresentation diagnosticsStatusPresentation() const;
    std::optional<Data::ScanResult> automationScanResult(
        const Data::ControllerConnectionScope &scope) const;
    std::optional<Data::DiagnosticsSnapshot> automationDiagnosticsSnapshot(
        const Data::ControllerConnectionScope &scope) const;
    QList<Core::ControllerConnectionProvider *> controllerConnectionProviders() const;
    static std::optional<Data::ControllerConnectionScope> uniqueMasterControllerConnectionScope(
        const Data::ProjectSnapshot &project);
    std::optional<Data::ControllerConnectionScope> quickControllerControlScope() const;
    QString quickControllerControlScopeUnavailableReason() const;
    std::optional<Data::ControllerConnectionScope> activeControllerConnectionScope() const;
    std::optional<Data::ControllerConnectionScope> selectedControllerConnectionScope() const;
    ControllerConnectionSelection prepareControllerConnection(
        const Data::ControllerConnectionScope &scope);
    ControllerConnectionSelection controllerConnectionSelection(
        const Data::ControllerConnectionScope &scope) const;
    Core::ControllerConnectionProvider *controllerConnectionProvider(
        const Data::ControllerConnectionScope &scope) const;
    QList<Data::ControllerConnectionProfile> controllerConnectionProfiles(
        const Data::ControllerConnectionScope &scope) const;
    std::optional<Data::ControllerConnectionProfileConfiguration>
    controllerConnectionProfileConfiguration(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &profileId) const;
    Data::ControllerConnectionSnapshot controllerConnectionSnapshot(
        const Data::ControllerConnectionScope &scope) const;
    bool controllerConnectionProjectIsOpen(const Data::ControllerConnectionScope &scope) const;
    Utils::Result<> selectControllerConnectionProvider(
        const Data::ControllerConnectionScope &scope, Utils::Id providerId);
    Utils::Result<> selectControllerConnectionProfile(
        const Data::ControllerConnectionScope &scope, const Data::NodeId &profileId);
    Utils::Result<> setControllerConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &profileId,
        const QString &endpoint);
    bool controllerConnectionSelectionLocked(const Data::ControllerConnectionScope &scope) const;
    bool canConnectController(const Data::ControllerConnectionScope &scope) const;
    bool canConnectSelectedController() const;
    bool canDisconnectSelectedController() const;
    bool canRefreshSelectedController() const;
    bool canExecuteControllerControl(
        const Data::ControllerConnectionScope &scope,
        Data::ControllerControlCommand command) const;
    QString controllerControlUnavailableReason(
        const Data::ControllerConnectionScope &scope,
        Data::ControllerControlCommand command) const;
    bool canExecuteSelectedControllerControl(Data::ControllerControlCommand command) const;
    std::optional<Data::ControllerControlCommand> quickControllerControlCommand(
        const Data::ControllerConnectionScope &scope,
        ControllerQuickControlAction action) const;
    QString quickControllerControlUnavailableReason(
        const Data::ControllerConnectionScope &scope,
        ControllerQuickControlAction action) const;
    bool controllerStartupInProgress(const Data::ControllerConnectionScope &scope) const;
    bool controllerStopInProgress(const Data::ControllerConnectionScope &scope) const;
    Utils::Result<> executeQuickControllerControl(
        const Data::ControllerConnectionScope &scope, ControllerQuickControlAction action);
    Utils::Result<> connectController(const Data::ControllerConnectionScope &scope);
    Utils::Result<> connectSelectedController();
    Utils::Result<> disconnectSelectedController();
    Utils::Result<> refreshSelectedController();
    QString currentBusApplyUnavailableReason() const;
    bool canApplyCurrentBusToProject() const;
    Utils::Result<> applyCurrentBusToProject();
    Utils::Result<> executeControllerControl(
        const Data::ControllerConnectionScope &scope,
        const Data::ControllerControlRequest &request);
    Utils::Result<> executeSelectedControllerControl(
        const Data::ControllerControlRequest &request);
    QString packageDeploymentUnavailableReason(
        const Data::ControllerConnectionScope &scope) const;
    bool canDeployControllerPackage(const Data::ControllerConnectionScope &scope) const;
    bool canCancelControllerPackageDeployment(
        const Data::ControllerConnectionScope &scope) const;
    Utils::Result<> deployControllerPackage(
        const Data::ControllerConnectionScope &scope,
        const Data::ControllerPackageDeploymentRequest &request);
    Utils::Result<> cancelControllerPackageDeployment(
        const Data::ControllerConnectionScope &scope, const QString &operationId);
    QString trustedRuntimePackageActivationUnavailableReason(
        const Data::ControllerConnectionScope &scope) const;
    QString trustedRuntimePackageActivationStatus(
        const Data::ControllerConnectionScope &scope) const;
    bool canStartTrustedRuntimePackageActivation(
        const Data::ControllerConnectionScope &scope) const;
    Utils::Result<> startTrustedRuntimePackageActivation(
        const Data::ControllerConnectionScope &scope);
    void writeControllerOutput(
        const QString &message,
        ControllerOutputLevel level = ControllerOutputLevel::Information);
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
    Utils::Result<> setMasterConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        const Data::MasterConfiguration &configuration);
    QString masterTimingModeUnavailableReason(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        Data::MasterTimingMode timingMode) const;
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
    void scanProviderChanged();
    void diagnosticsProviderChanged(bool availabilityChanged);
    void diagnosticsStatusChanged();
    void controllerConnectionChanged();
    void trustedRuntimePackageActivationChanged();
    void controllerOutputRequested(
        const QString &message, EtherCAT::Workbench::Internal::ControllerOutputLevel level);

private:
    friend class EtherCATWorkbenchTests;
    friend class RuntimePackageCompilerPreparationBridge;

    Utils::Result<> setTrustedRuntimePackageActivationPreparation(
        const Core::RuntimePackageActivationPreparationRequest &request);
    void clearTrustedRuntimePackageActivationPreparation();
    void setRuntimePackageCompilerPreparationCoordinatorAvailable(bool available);

#ifdef WITH_TESTS
    static std::optional<Data::DeviceDescription> matchingDeviceDescriptionForCurrentBusTest(
        Core::DeviceRepositoryProvider *repository,
        const QList<Data::DeviceSummary> &devices,
        const Data::ControllerTopologySlave &slave,
        const Data::OfflineSlaveConfiguration *existing,
        int *ambiguousMatches,
        int *unsupportedMatches);
#endif

    // Product API state-changing commands can legitimately remain pending for 45 seconds,
    // including the authoritative state refresh. Keep the whole project-close cleanup bounded
    // while leaving enough time for the final release and disconnect.
    static constexpr int controllerCleanupPollIntervalMs = 100;
    static constexpr int controllerCleanupMaximumWaitMs = 60000;
    static constexpr int controllerCleanupMaximumPolls = controllerCleanupMaximumWaitMs
                                                         / controllerCleanupPollIntervalMs;
    static constexpr int controllerStartupPollIntervalMs = 100;
    static constexpr int controllerStartupMaximumPhaseWaitMs = 60000;
    static constexpr int controllerStartupMaximumPhasePolls = controllerStartupMaximumPhaseWaitMs
                                                              / controllerStartupPollIntervalMs;
    static constexpr int controllerStopPollIntervalMs = 100;
    static constexpr int controllerStopMaximumPhaseWaitMs = 60000;
    static constexpr int controllerStopMaximumPhasePolls = controllerStopMaximumPhaseWaitMs
                                                           / controllerStopPollIntervalMs;

    struct ControllerAutoAcquireState
    {
        quint64 providerEpoch = 0;
        Data::ControllerConnectionScope scope;
        Data::NodeId profileId;
        quint64 sessionGeneration = 0;
        bool acquireQueued = false;
        bool acquireAttempted = false;
    };

    enum class ControllerStartupPhase {
        WaitingForRestore,
        WaitingForRunning,
    };

    struct ControllerStartupState
    {
        quint64 providerEpoch = 0;
        Data::ControllerConnectionScope scope;
        Data::NodeId profileId;
        quint64 sessionGeneration = 0;
        quint64 sessionId = 0;
        quint64 bootId = 0;
        Data::ControllerControlCommand startCommand = Data::ControllerControlCommand::None;
        ControllerStartupPhase phase = ControllerStartupPhase::WaitingForRestore;
        int remainingPolls = 0;
        int refreshCooldown = 0;
        bool scheduled = false;
    };

    enum class ControllerStopPhase {
        WaitingForControlledStop,
        WaitingForConfiguration,
    };

    struct ControllerStopState
    {
        quint64 providerEpoch = 0;
        Data::ControllerConnectionScope scope;
        Data::NodeId profileId;
        quint64 sessionGeneration = 0;
        quint64 sessionId = 0;
        quint64 bootId = 0;
        ControllerStopPhase phase = ControllerStopPhase::WaitingForControlledStop;
        int remainingPolls = 0;
        int refreshCooldown = 0;
        bool scheduled = false;
    };

    enum class ControllerCleanupPhase {
        Evaluate,
        WaitingForPending,
        WaitingForStableState,
        WaitingForRelease,
        WaitingForDisconnect,
        Failed,
    };

    struct ControllerCleanupState
    {
        quint64 providerEpoch = 0;
        Data::ControllerConnectionScope scope;
        Data::NodeId profileId;
        quint64 sessionGeneration = 0;
        quint64 sessionId = 0;
        quint64 bootId = 0;
        ControllerCleanupPhase phase = ControllerCleanupPhase::Evaluate;
        int remainingPolls = 0;
        int refreshCooldown = 0;
        int disconnectAttempts = 0;
        int generationChanges = 0;
        bool disconnectAccepted = false;
        bool scheduled = false;
        QString failure;
    };

    void refreshProjects();
    void refreshDevices();
    void refreshDeviceAdapterProviders();
    void refreshDeviceAdapterProvidersExcluding(Core::Provider *provider);
    void watchOptionalProvider(Core::Provider *provider);
    void watchDeviceAdapterProvider(Core::Provider *provider);
    void watchControllerConnectionProvider(Core::Provider *provider);
    void restoreScanProviderSelections();
    void refreshOptionalProviders(Core::Provider *excluding = nullptr);
    void handleOptionalAvailabilityChanged();
    void handleProjectAboutToBeRemoved(const Data::NodeId &projectId);
    void handleControllerConnectionChanged();
    void refreshControllerConnectionPresentation();
    void writeControllerTopologyCapabilityOutput(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionSnapshot &snapshot);
    void scheduleControllerAutoAcquire();
    void executeControllerAutoAcquire(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &expectedScope,
        const Data::NodeId &expectedProfileId,
        quint64 expectedGeneration,
        quint64 expectedProviderEpoch);
    QString controllerStartupUnavailableReason(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionSnapshot &snapshot,
        Data::ControllerControlCommand startCommand) const;
    Utils::Result<> beginControllerStartup(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionSnapshot &snapshot,
        Data::ControllerControlCommand startCommand);
    void scheduleControllerStartup(Core::ControllerConnectionProvider *provider, int delayMs = 0);
    void advanceControllerStartup(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &expectedScope,
        const Data::NodeId &expectedProfileId,
        quint64 expectedGeneration,
        quint64 expectedProviderEpoch);
    void finishControllerStartup(Core::ControllerConnectionProvider *provider);
    void failControllerStartup(Core::ControllerConnectionProvider *provider, const QString &reason);
    QString controllerStopUnavailableReason(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionSnapshot &snapshot) const;
    Utils::Result<> beginControllerStop(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionSnapshot &snapshot);
    void scheduleControllerStop(Core::ControllerConnectionProvider *provider, int delayMs = 0);
    void advanceControllerStop(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &expectedScope,
        const Data::NodeId &expectedProfileId,
        quint64 expectedGeneration,
        quint64 expectedProviderEpoch);
    void finishControllerStop(Core::ControllerConnectionProvider *provider);
    void failControllerStop(Core::ControllerConnectionProvider *provider, const QString &reason);
    void beginControllerCleanup(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionSnapshot &snapshot);
    void scheduleControllerCleanup(Core::ControllerConnectionProvider *provider, int delayMs = 0);
    void advanceControllerCleanup(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &expectedScope,
        const Data::NodeId &expectedProfileId,
        quint64 expectedGeneration,
        quint64 expectedProviderEpoch);
    void finishControllerCleanup(Core::ControllerConnectionProvider *provider);
    void failControllerCleanup(Core::ControllerConnectionProvider *provider, const QString &reason);
    bool controllerCleanupBindingIsRetained(const Data::ControllerConnectionScope &scope) const;
    static std::optional<ControllerConnectionSelection> automaticControllerConnectionSelection(
        const Data::ControllerConnectionScope &scope,
        const QList<Core::ControllerConnectionProvider *> &providers);
    bool controllerConnectionScopeIsValid(const Data::ControllerConnectionScope &scope) const;
    Core::TopologyLookupResult selectedMockTopology(
        const Data::ControllerConnectionScope &scope) const;
    Core::TopologyLookupResult selectedRealTopology(
        const Data::ControllerConnectionScope &scope) const;
    Data::ControllerConnectionSnapshot projectedControllerConnectionSnapshot(
        Core::ControllerConnectionProvider *provider,
        const Data::ControllerConnectionScope &scope) const;
    QString controllerControlCommonUnavailableReason(
        const Data::ControllerConnectionScope &scope,
        Data::ControllerControlCommand command,
        Data::ControllerConnectionSnapshot *snapshot) const;
    ControllerConnectionSelection *mutableControllerConnectionSelection(
        const Data::ControllerConnectionScope &scope);

    WorkbenchTreeModel m_treeModel;
    QPointer<Core::SelectionService> m_selectionService;
    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::DeviceRepositoryProvider> m_deviceRepository;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    QPointer<Core::RuntimePackageActivationService> m_runtimePackageActivationService;
    QPointer<Core::ScanProviderSelectionService> m_scanProviderSelectionService;
    QSet<Utils::Id> m_removingScanProviderIds;
    QPointer<Core::TopologyService> m_topologyService;
    std::optional<Core::RuntimePackageActivationPreparationRequest>
        m_runtimePackageActivationPreparation;
    bool m_runtimePackageCompilerPreparationCoordinatorAvailable = false;
    QHash<QString, quint64> m_reportedRuntimePackageActivationRevisions;
    QList<QMetaObject::Connection> m_connections;
    bool m_shuttingDown = false;
    bool m_suppressScanProviderPreferenceRestore = false;
    OptionalProviderPresentation m_diagnosticsProvider;
    DiagnosticsStatusPresentation m_diagnosticsStatus;
    QList<ControllerConnectionSelection> m_controllerConnectionSelections;
    QHash<Core::ControllerConnectionProvider *, quint64> m_controllerConnectionProviderEpochs;
    QHash<Core::ControllerConnectionProvider *, ControllerAutoAcquireState>
        m_controllerAutoAcquireStates;
    QHash<Core::ControllerConnectionProvider *, ControllerStartupState> m_controllerStartupStates;
    QHash<Core::ControllerConnectionProvider *, ControllerStopState> m_controllerStopStates;
    QHash<Core::ControllerConnectionProvider *, ControllerCleanupState> m_controllerCleanupStates;
    QHash<Core::ControllerConnectionProvider *, QString> m_controllerOutputFingerprints;
    QHash<Core::ControllerConnectionProvider *, QString> m_topologyCapabilityFingerprints;
    quint64 m_nextControllerConnectionProviderEpoch = 0;
    bool m_suppressControllerConnectionChanges = false;
};

} // namespace EtherCAT::Workbench::Internal

Q_DECLARE_METATYPE(EtherCAT::Workbench::Internal::ControllerOutputLevel)
