// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/axisparameterevidence.h>
#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/devicedescription.h>
#include <ethercatdata/diagnosticssnapshot.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/runtimepackageactivation.h>
#include <ethercatdata/runtimeoutputtransaction.h>
#include <ethercatdata/runtimeresource.h>
#include <ethercatdata/scansnapshot.h>
#include <ethercatdata/semanticmappingattestation.h>

#include <utils/filepath.h>
#include <utils/id.h>
#include <utils/result.h>

#include <QObject>

#include <optional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace EtherCAT::Core {

enum class ProviderKind {
    Project,
    DeviceRepository,
    PropertyPage,
    Scan,
    Diagnostics,
    ControllerConnection,
    DeviceAdapter,
    RuntimePackageCompiler = 7,
    RuntimePackageCompilerProjectRequestBuilder = 8,
};
enum class DeviceImportState { Pending, Running, Canceling, Finished };
enum class ProviderDiagnosticSeverity { Information, Warning, Error };

struct ETHERCATCORE_EXPORT ProviderStartupDiagnostic
{
    Utils::Id code;
    QString message;
    ProviderDiagnosticSeverity severity = ProviderDiagnosticSeverity::Information;

    bool isValid() const { return code.isValid() && !message.trimmed().isEmpty(); }

    friend bool operator==(const ProviderStartupDiagnostic &, const ProviderStartupDiagnostic &)
        = default;
};

enum class WorkbenchNodeKind {
    None = 0,
    Project = 1,
    Target = 2,
    Master = 3,
    DeviceRepository = 4,
    Device = 5,
    ConfiguredSlave = 6,
    Diagnostics = 7,
    Placeholder = 8,
    ProcessInputs = 9,
    ProcessOutputs = 10,
    RxPdoGroup = 11,
    TxPdoGroup = 12,
    Pdo = 13,
    PdoEntry = 14,
    Modules = 15,
    Module = 16,
    Channel = 17,
};

ETHERCATCORE_EXPORT bool isMockUiEnabled();

struct ETHERCATCORE_EXPORT PropertyPageContext
{
    Data::NodeId projectId;
    Data::NodeId nodeId;
    WorkbenchNodeKind nodeKind = WorkbenchNodeKind::None;
    QString displayName;

    friend bool operator==(const PropertyPageContext &, const PropertyPageContext &) = default;
};

struct ETHERCATCORE_EXPORT PropertyPageDescriptor
{
    Utils::Id id;
    QString displayName;
    int priority = 0;

    friend bool operator==(const PropertyPageDescriptor &, const PropertyPageDescriptor &) = default;
};

class DeviceImportJob;

class ETHERCATCORE_EXPORT Provider : public QObject
{
    Q_OBJECT

public:
    Provider(ProviderKind kind, Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    ProviderKind kind() const;
    Utils::Id id() const;
    QString displayName() const;
    bool isAvailable() const;

    void setDisplayName(const QString &displayName);
    void setAvailable(bool available);

signals:
    void displayNameChanged(const QString &displayName);
    void availabilityChanged(bool available);

private:
    const ProviderKind m_kind;
    const Utils::Id m_id;
    QString m_displayName;
    bool m_available = false;
};

class ETHERCATCORE_EXPORT ProjectService : public Provider
{
    Q_OBJECT

public:
    ProjectService(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<Data::ProjectSnapshot> projects() const = 0;
    virtual std::optional<Data::ProjectSnapshot> project(const Data::NodeId &projectId) const = 0;
    virtual Data::NodeId activeProjectId() const = 0;
    virtual bool managesProject(const QObject *project) const = 0;

    virtual Utils::Result<> activateProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> renameProject(const Data::NodeId &projectId, const QString &name) = 0;
    virtual Utils::Result<> saveProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> undoProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> redoProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> setMasterConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        const Data::MasterConfiguration &configuration) = 0;
    virtual Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &projectId,
        const Data::NodeId &masterId,
        const QList<Data::OfflineSlaveConfiguration> &slaves) = 0;
    virtual Utils::Result<> setProcessDataConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::ProcessDataConfiguration &configuration) = 0;
    virtual Utils::Result<> setStartupConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::StartupConfiguration &configuration) = 0;
    virtual Utils::Result<> setDcConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::DcConfiguration &configuration) = 0;
    virtual bool canUndoProject(const Data::NodeId &projectId) const = 0;
    virtual bool canRedoProject(const Data::NodeId &projectId) const = 0;
    virtual Utils::Result<> renameStructuralNode(
        const Data::NodeId &projectId,
        const Data::NodeId &nodeId,
        const QString &name) = 0;
    virtual Utils::Result<> setDeviceAdapterSelection(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const QByteArray &esiSha256,
        const Data::DeviceAdapterProjectSelection &selection) = 0;
    virtual Utils::Result<> setDeviceParameterConfiguration(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const QByteArray &expectedEsiSha256,
        const Data::DeviceAdapterProjectSelection &expectedAdapterSelection,
        const Data::DeviceParameterConfiguration &configuration) = 0;
    virtual Utils::Result<> setManualControlEnvelope(
        const Data::NodeId &projectId,
        const Data::NodeId &slaveId,
        const Data::ManualControlEnvelope &envelope) = 0;
    virtual Utils::Result<> setMasterBindingArtifact(
        const Data::NodeId &projectId,
        const Data::SemanticBindingArtifactReference &reference) = 0;
    virtual Utils::Result<Data::RuntimePackageActivationProjectCapture>
    captureRuntimePackageActivationProject(const Data::NodeId &projectId) const = 0;
    virtual Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult>
    compareAndSetMasterBindingArtifact(
        const Data::NodeId &projectId,
        const Data::RuntimePackageActivationDocumentRevisionToken &expectedDocumentRevision,
        const Data::RuntimePackageActivationOriginalBindingToken &expectedBinding,
        const Data::SemanticBindingArtifactReference &targetReference) = 0;

signals:
    void projectAdded(const EtherCAT::Data::ProjectSnapshot &project);
    void projectAboutToBeRemoved(const EtherCAT::Data::NodeId &projectId);
    void projectChanged(const EtherCAT::Data::ProjectSnapshot &project);
    void activeProjectChanged(
        const EtherCAT::Data::NodeId &oldProjectId, const EtherCAT::Data::NodeId &newProjectId);
};

class ETHERCATCORE_EXPORT DeviceRepositoryProvider : public Provider
{
    Q_OBJECT

public:
    DeviceRepositoryProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<Data::DeviceSummary> devices(const Data::DeviceFilter &filter = {}) const = 0;
    virtual std::optional<Data::DeviceDescription> device(const Data::NodeId &deviceId) const = 0;
    virtual QByteArray originalXml(const Data::NodeId &deviceId) const = 0;
    virtual DeviceImportJob *importFiles(const Utils::FilePaths &filePaths) = 0;
    virtual DeviceImportJob *rebuildIndex() = 0;
    virtual bool isIndexing() const = 0;

signals:
    void devicesReset();
    void devicesChanged(const QList<EtherCAT::Data::NodeId> &deviceIds);
    void indexingChanged(bool indexing);
};

class ETHERCATCORE_EXPORT DeviceAdapterProvider : public Provider
{
    Q_OBJECT

public:
    DeviceAdapterProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<Data::DeviceAdapterManifest> adapterManifests() const = 0;
    virtual std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &adapterId, const QString &version) const = 0;
    virtual Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &request) const = 0;
    virtual QList<ProviderStartupDiagnostic> startupDiagnostics() const;

signals:
    void adapterManifestsChanged();
};

class ETHERCATCORE_EXPORT DeviceImportJob : public QObject
{
    Q_OBJECT

public:
    explicit DeviceImportJob(QObject *parent = nullptr);

    DeviceImportState state() const;
    int progressValue() const;
    int progressMaximum() const;
    Data::DeviceImportResult result() const;

    virtual void cancel() = 0;

signals:
    void stateChanged(EtherCAT::Core::DeviceImportState state);
    void progressChanged(int value, int maximum);
    void finished(const EtherCAT::Data::DeviceImportResult &result);

protected:
    void setState(DeviceImportState state);
    void setProgress(int value, int maximum);
    void finish(const Data::DeviceImportResult &result);

private:
    DeviceImportState m_state = DeviceImportState::Pending;
    int m_progressValue = 0;
    int m_progressMaximum = 0;
    Data::DeviceImportResult m_result;
};

class ETHERCATCORE_EXPORT PropertyPageProvider : public Provider
{
    Q_OBJECT

public:
    PropertyPageProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<PropertyPageDescriptor> pages(const PropertyPageContext &context) const = 0;
    virtual QWidget *createPage(Utils::Id pageId, QWidget *parent) = 0;
    virtual void updatePage(Utils::Id pageId, QWidget *page, const PropertyPageContext &context) = 0;
};

class ETHERCATCORE_EXPORT ControllerConnectionProvider : public Provider
{
    Q_OBJECT

public:
    ControllerConnectionProvider(
        Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &scope) const = 0;
    virtual std::optional<Data::ControllerConnectionProfileConfiguration>
    connectionProfileConfiguration(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &profileId) const;
    virtual Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &profileId,
        const QString &endpoint);
    virtual Data::ControllerConnectionSnapshot connectionSnapshot() const = 0;

    virtual Utils::Result<> connectToController(
        const Data::ControllerConnectionRequest &request) = 0;
    virtual Utils::Result<> disconnectFromController() = 0;
    virtual Utils::Result<> refreshController() = 0;

    virtual bool supportsAxisParameterEvidence() const;
    virtual std::optional<Data::AxisParameterEvidenceBatch> axisParameterEvidenceBatch() const;

    virtual bool supportsControlCommand(
        Data::ControllerControlCommand command) const;
    virtual Utils::Result<> executeControlCommand(
        const Data::ControllerControlRequest &request);
    virtual bool supportsPackageDeployment() const;
    virtual Utils::Result<> deployPackage(const Data::ControllerPackageDeploymentRequest &request);
    virtual Utils::Result<> cancelPackageDeployment(const QString &operationId);
    virtual bool supportsRuntimeResources() const;
    virtual std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const;
    virtual std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const;
    virtual Utils::Result<> refreshRuntimeResources();
    virtual Utils::Result<> requestRuntimeResourceSnapshot(
        const Data::RuntimeResourceSnapshotRequest &request);
    virtual bool supportsRuntimeSemanticMappingAttestation() const;
    virtual std::optional<Data::RuntimeSemanticMappingAttestation>
    runtimeSemanticMappingAttestation() const;
    virtual Utils::Result<> requestRuntimeSemanticMappingAttestation(
        const Data::RuntimeSemanticMappingAttestationRequest &request);
    virtual bool supportsRuntimeOutputTransactions() const;
    virtual Utils::Result<> requestRuntimeOutputGroupPolicy(
        const Data::RuntimeOutputGroupPolicyRequest &request);
    virtual Utils::Result<> requestRuntimeOutputTransactionState(
        const Data::RuntimeOutputTransactionStateRequest &request);
    virtual Utils::Result<> applyRuntimeOutputTransaction(
        const Data::RuntimeOutputTransactionRequest &request);

signals:
    void connectionProfilesChanged();
    void connectionSnapshotChanged();
    void axisParameterEvidenceBatchChanged();
    void runtimeResourceCatalogChanged();
    void runtimeResourceSnapshotChanged();
    void runtimeResourceSnapshotRequestFinished(
        const EtherCAT::Data::RuntimeResourceSnapshotResult &result);
    void runtimeSemanticMappingAttestationChanged();
    void runtimeSemanticMappingAttestationRequestFinished(
        const EtherCAT::Data::RuntimeSemanticMappingAttestationResult &result);
    void runtimeOutputGroupPolicyRequestFinished(
        const EtherCAT::Data::RuntimeOutputGroupPolicyResult &result);
    void runtimeOutputTransactionStateChanged(
        const EtherCAT::Data::RuntimeOutputTransactionState &state);
    void runtimeOutputTransactionStateInvalidated();
    void runtimeOutputTransactionStateRequestFinished(
        const EtherCAT::Data::RuntimeOutputTransactionStateResult &result);
    void runtimeOutputTransactionFinished(
        const EtherCAT::Data::RuntimeOutputTransactionResult &result);
};

class ETHERCATCORE_EXPORT ScanProvider : public Provider
{
    Q_OBJECT

public:
    ScanProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual Data::ScanState scanState() const = 0;
    virtual Data::ScanProgress scanProgress() const = 0;
    virtual std::optional<Data::ScanResult> lastScanResult() const = 0;
    virtual QString lastScanError() const = 0;

    virtual Utils::Result<> startScan(const Data::ScanRequest &request) = 0;
    virtual void cancelScan() = 0;
    virtual void clearScanResult() = 0;

signals:
    void scanStateChanged(EtherCAT::Data::ScanState state);
    void scanProgressChanged(const EtherCAT::Data::ScanProgress &progress);
    void scanResultChanged();
    void scanFinished(EtherCAT::Data::ScanState terminalState);
};

class ETHERCATCORE_EXPORT DiagnosticsProvider : public Provider
{
    Q_OBJECT

public:
    DiagnosticsProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual Data::DiagnosticsStreamState streamState() const = 0;
    virtual Data::DiagnosticsRequest activeRequest() const = 0;
    virtual std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const = 0;
    virtual QList<Data::DiagnosticEvent> events() const = 0;
    virtual QList<Data::DiagnosticTrendSample> trendSamples() const = 0;
    virtual Data::DiagnosticsLimits limits() const = 0;
    virtual QString lastDiagnosticsError() const = 0;

    virtual Utils::Result<> startMonitoring(const Data::DiagnosticsRequest &request) = 0;
    virtual void stopMonitoring() = 0;
    virtual Utils::Result<> requestRunMode(Data::DiagnosticsRunMode mode) = 0;
    virtual Utils::Result<> acknowledgeAlarm(const Data::NodeId &eventId) = 0;
    virtual Utils::Result<> clearRecoveredEvents() = 0;

signals:
    void streamStateChanged(EtherCAT::Data::DiagnosticsStreamState state);
    void diagnosticsSnapshotChanged();
    void diagnosticEventsChanged();
    void diagnosticTrendChanged();
    void monitoringStopped();
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::ProviderKind)
Q_DECLARE_METATYPE(EtherCAT::Core::DeviceImportState)
Q_DECLARE_METATYPE(EtherCAT::Core::ProviderDiagnosticSeverity)
Q_DECLARE_METATYPE(EtherCAT::Core::ProviderStartupDiagnostic)
Q_DECLARE_METATYPE(EtherCAT::Core::WorkbenchNodeKind)
Q_DECLARE_METATYPE(EtherCAT::Core::PropertyPageContext)
Q_DECLARE_METATYPE(EtherCAT::Core::PropertyPageDescriptor)
