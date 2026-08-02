// Copyright (C) 2026 Embed Labs

#include "ethercatsemanticruntimetests.h"

#include "canonicaljson_p.h"
#include "ecfgconfiguration_p.h"
#include "ecpkgcontainer.h"
#include "ed25519verifier.h"
#include "productiontruststore_p.h"
#include "readonlysemanticbindingfactory_p.h"
#include "runtimepackageevidence_p.h"
#include "runtimepackageevidencerepository_p.h"
#include "runtimepackageactivationservice_p.h"
#include "semanticactiondefinitions_p.h"
#include "semanticactionplan_p.h"
#include "semanticactionruntimefactory_p.h"
#include "semanticbindingartifact_p.h"
#include "semanticoperationjournal_p.h"
#include "semanticruntimeexecutor.h"
#include "signedecpkgmanifest_p.h"
#include "verifiedecpkgstore_p.h"

#include <coreplugin/icore.h>

#include <ethercatcore/ethercatcoretests.h>
#include <ethercatcore/providerregistry.h>
#include <ethercatcore/providers.h>

#include <extensionsystem/pluginmanager.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <functional>
#include <future>
#include <limits>
#include <utility>
#include <variant>

namespace EtherCAT::SemanticRuntime::Internal {

class TestProjectService final : public Core::ProjectService
{
public:
    TestProjectService()
        : ProjectService("EtherCAT.SemanticRuntime.Tests.Project", "Semantic Runtime test projects")
    {
        setAvailable(true);
    }

    QList<Data::ProjectSnapshot> projects() const final { return m_projects; }

    std::optional<Data::ProjectSnapshot> project(const Data::NodeId &projectId) const final
    {
        const auto found = std::find_if(
            m_projects.cbegin(),
            m_projects.cend(),
            [&projectId](const Data::ProjectSnapshot &candidate) {
                return candidate.id == projectId;
            });
        if (found == m_projects.cend())
            return std::nullopt;
        return *found;
    }

    Data::NodeId activeProjectId() const final { return m_activeProjectId; }
    bool managesProject(const QObject *) const final { return false; }

    Utils::Result<> activateProject(const Data::NodeId &projectId) final
    {
        if (!project(projectId))
            return Utils::ResultError("Unknown test project");
        const Data::NodeId previous = m_activeProjectId;
        m_activeProjectId = projectId;
        emit activeProjectChanged(previous, m_activeProjectId);
        return Utils::ResultOk;
    }

    Utils::Result<> renameProject(const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Utils::Result<> saveProject(const Data::NodeId &projectId) final
    {
        if (!activationCapture
            || activationCapture->snapshot().id != projectId) {
            return unsupported();
        }
        ++activationSaveCalls;
        if (activationSaveError)
            return Utils::ResultError(*activationSaveError);
        const Data::RuntimePackageActivationDocumentRevisionToken
            persistedDocumentRevision{QByteArray(32, '\x6e')};
        Data::ProjectSnapshot savedSnapshot = activationCapture->snapshot();
        savedSnapshot.modified = false;
        activationCapture = Data::RuntimePackageActivationProjectCapture{
            savedSnapshot,
            activationCapture->serializedProject(),
            activationCapture->documentRevisionNumber() + 1,
            persistedDocumentRevision,
            activationCapture->originalBinding(),
        };
        return Utils::ResultOk;
    }
    Utils::Result<> undoProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> redoProject(const Data::NodeId &) final { return unsupported(); }

    Utils::Result<> setMasterConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::MasterConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &,
        const Data::NodeId &,
        const QList<Data::OfflineSlaveConfiguration> &) final
    {
        return unsupported();
    }

    Utils::Result<> setProcessDataConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::ProcessDataConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> setStartupConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::StartupConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> setDcConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::DcConfiguration &) final
    {
        return unsupported();
    }

    bool canUndoProject(const Data::NodeId &) const final { return false; }
    bool canRedoProject(const Data::NodeId &) const final { return false; }

    Utils::Result<> renameStructuralNode(
        const Data::NodeId &, const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Utils::Result<> setDeviceAdapterSelection(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &) final
    {
        return unsupported();
    }

    Utils::Result<> setManualControlEnvelope(
        const Data::NodeId &, const Data::NodeId &, const Data::ManualControlEnvelope &) final
    {
        return unsupported();
    }

    Utils::Result<> setMasterBindingArtifact(
        const Data::NodeId &, const Data::SemanticBindingArtifactReference &) final
    {
        return unsupported();
    }

    Utils::Result<Data::RuntimePackageActivationProjectCapture>
    captureRuntimePackageActivationProject(const Data::NodeId &projectId) const final
    {
        ++activationCaptureCalls;
        if (!activationCapture)
            return Utils::ResultError("Test project capture is not implemented");
        if (activationCapture->snapshot().id != projectId)
            return Utils::ResultError("Unknown test activation project");
        return *activationCapture;
    }

    Utils::Result<Data::RuntimePackageActivationProjectCompareAndSetResult>
    compareAndSetMasterBindingArtifact(
        const Data::NodeId &projectId,
        const Data::RuntimePackageActivationDocumentRevisionToken &documentRevision,
        const Data::RuntimePackageActivationOriginalBindingToken &originalBinding,
        const Data::SemanticBindingArtifactReference &target) final
    {
        ++activationCompareAndSetCalls;
        lastActivationTarget = target;
        if (activationCompareAndSetError)
            return Utils::ResultError(*activationCompareAndSetError);
        if (!activationCapture || activationCapture->snapshot().id != projectId
            || activationCapture->documentRevision() != documentRevision
            || activationCapture->originalBinding() != originalBinding
            || activationCompareAndSetStale) {
            return Data::RuntimePackageActivationProjectCompareAndSetResult{
                Data::RuntimePackageActivationProjectCompareAndSetDisposition::Stale};
        }

        const auto resultingBinding
            = Data::runtimePackageActivationBindingToken(target);
        const Data::RuntimePackageActivationDocumentRevisionToken
            resultingDocumentRevision{QByteArray(32, '\x7e')};
        const Data::RuntimePackageActivationProjectCommit commit{
            documentRevision,
            originalBinding,
            resultingDocumentRevision,
            resultingBinding,
            Data::RuntimePackageActivationProjectCommitDisposition::
                CompareAndSetCommitted,
            QDateTime::currentDateTimeUtc(),
        };
        if (!commit.isValid())
            return Utils::ResultError("The test project commit is invalid");
        Data::ProjectSnapshot resultingSnapshot = activationCapture->snapshot();
        resultingSnapshot.masterBindingArtifact = target;
        resultingSnapshot.modified = true;
        activationCapture = Data::RuntimePackageActivationProjectCapture{
            resultingSnapshot,
            activationCapture->serializedProject()
                + QByteArray("\nactivation-binding-saved"),
            activationCapture->documentRevisionNumber() + 1,
            resultingDocumentRevision,
            resultingBinding,
        };
        for (Data::ProjectSnapshot &candidate : m_projects) {
            if (candidate.id == projectId) {
                candidate = resultingSnapshot;
                emit projectChanged(candidate);
                break;
            }
        }
        return Data::RuntimePackageActivationProjectCompareAndSetResult{
            Data::RuntimePackageActivationProjectCompareAndSetDisposition::
                CompareAndSetCommitted,
            commit};
    }

    void addProject(const Data::ProjectSnapshot &project)
    {
        m_projects.append(project);
        emit projectAdded(project);
    }

    void changeProject(const Data::ProjectSnapshot &project)
    {
        for (Data::ProjectSnapshot &candidate : m_projects) {
            if (candidate.id != project.id)
                continue;
            candidate = project;
            emit projectChanged(project);
            return;
        }
    }

    void removeProject(const Data::NodeId &projectId)
    {
        for (qsizetype index = 0; index < m_projects.size(); ++index) {
            if (m_projects.at(index).id != projectId)
                continue;
            emit projectAboutToBeRemoved(projectId);
            m_projects.removeAt(index);
            return;
        }
    }

    std::optional<Data::RuntimePackageActivationProjectCapture> activationCapture;
    std::optional<QString> activationCompareAndSetError;
    std::optional<QString> activationSaveError;
    bool activationCompareAndSetStale = false;
    mutable int activationCaptureCalls = 0;
    int activationCompareAndSetCalls = 0;
    int activationSaveCalls = 0;
    Data::SemanticBindingArtifactReference lastActivationTarget;

private:
    static Utils::Result<> unsupported()
    {
        return Utils::ResultError("Test project mutation is not implemented");
    }

    QList<Data::ProjectSnapshot> m_projects;
    Data::NodeId m_activeProjectId;
};

class CountingControllerProvider final : public Core::ControllerConnectionProvider
{
public:
    explicit CountingControllerProvider(Utils::Id id)
        : ControllerConnectionProvider(id, QStringLiteral("Semantic Runtime counting controller"))
    {}

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }

    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &, const Data::NodeId &, const QString &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final
    {
        ++snapshotReads;
        return snapshot;
    }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> disconnectFromController() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> refreshController() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsControlCommand(Data::ControllerControlCommand) const final { return true; }

    Utils::Result<> executeControlCommand(const Data::ControllerControlRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsPackageDeployment() const final { return true; }

    Utils::Result<> deployPackage(const Data::ControllerPackageDeploymentRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> cancelPackageDeployment(const QString &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsRuntimeResources() const final { return runtimeResourcesSupported; }

    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const final
    {
        ++catalogReads;
        return catalog;
    }

    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const final
    {
        ++resourceSnapshotReads;
        return resourceSnapshot;
    }

    Utils::Result<> refreshRuntimeResources() final
    {
        ++runtimeResourceRefreshCalls;
        return runtimeResourceRefreshError
                   ? Utils::ResultError(*runtimeResourceRefreshError)
                   : Utils::ResultOk;
    }

    Utils::Result<> requestRuntimeResourceSnapshot(const Data::RuntimeResourceSnapshotRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsRuntimeSemanticMappingAttestation() const final
    {
        return semanticMappingAttestationSupported;
    }

    std::optional<Data::RuntimeSemanticMappingAttestation>
    runtimeSemanticMappingAttestation() const final
    {
        ++semanticMappingAttestationReads;
        return semanticMappingAttestation;
    }

    Utils::Result<> requestRuntimeSemanticMappingAttestation(
        const Data::RuntimeSemanticMappingAttestationRequest &request) final
    {
        semanticMappingAttestationRequests.append(request);
        return semanticMappingAttestationRequestError
                   ? Utils::ResultError(*semanticMappingAttestationRequestError)
                   : Utils::ResultOk;
    }

    void publishSnapshot(const Data::ControllerConnectionSnapshot &value)
    {
        snapshot = value;
        emit connectionSnapshotChanged();
    }

    void publishCatalog(const std::optional<Data::RuntimeResourceCatalog> &value)
    {
        catalog = value;
        emit runtimeResourceCatalogChanged();
    }

    void publishResourceSnapshot(const std::optional<Data::RuntimeResourceSnapshot> &value)
    {
        resourceSnapshot = value;
        emit runtimeResourceSnapshotChanged();
    }

    void publishResourceSnapshotRequestFinished(
        const Data::RuntimeResourceSnapshotResult &result)
    {
        emit runtimeResourceSnapshotRequestFinished(result);
    }

    void publishSemanticMappingAttestation(
        const std::optional<Data::RuntimeSemanticMappingAttestation> &value)
    {
        semanticMappingAttestation = value;
        emit runtimeSemanticMappingAttestationChanged();
    }

    void setRuntimeResourcesSupported(bool supported)
    {
        runtimeResourcesSupported = supported;
        emit runtimeResourceCatalogChanged();
    }

    mutable int snapshotReads = 0;
    mutable int catalogReads = 0;
    mutable int resourceSnapshotReads = 0;
    mutable int semanticMappingAttestationReads = 0;
    int runtimeResourceRefreshCalls = 0;
    int mutationCalls = 0;
    QList<Data::RuntimeSemanticMappingAttestationRequest>
        semanticMappingAttestationRequests;
    std::optional<QString> runtimeResourceRefreshError;
    std::optional<QString> semanticMappingAttestationRequestError;
    bool runtimeResourcesSupported = true;
    bool semanticMappingAttestationSupported = true;
    Data::ControllerConnectionSnapshot snapshot;
    std::optional<Data::RuntimeResourceCatalog> catalog;
    std::optional<Data::RuntimeResourceSnapshot> resourceSnapshot;
    std::optional<Data::RuntimeSemanticMappingAttestation> semanticMappingAttestation;

private:
    static Utils::Result<> rejectedMutation()
    {
        return Utils::ResultError("Counting controller must not be mutated");
    }
};

class ActivationControllerProvider final : public Core::ControllerConnectionProvider
{
public:
    enum class DeploymentMode {
        Succeed,
        FailTerminal,
        RejectImmediately,
        OutcomeUnknown,
        Stall,
        ProgressThenSucceed,
    };

    enum class ControlMode {
        Complete,
        StallAcquire,
        StallRelease,
    };

    explicit ActivationControllerProvider(Utils::Id id)
        : ControllerConnectionProvider(
              id, QStringLiteral("Trusted activation test controller"))
    {}

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }

    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &,
        const Data::NodeId &,
        const QString &) final
    {
        return unsupported();
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final
    {
        return snapshot;
    }

    Utils::Result<> connectToController(
        const Data::ControllerConnectionRequest &) final
    {
        return unsupported();
    }

    Utils::Result<> disconnectFromController() final { return unsupported(); }
    Utils::Result<> refreshController() final { return unsupported(); }

    bool supportsControlCommand(Data::ControllerControlCommand command) const final
    {
        return command == Data::ControllerControlCommand::AcquireControl
               || command == Data::ControllerControlCommand::ReleaseControl;
    }

    Utils::Result<> executeControlCommand(
        const Data::ControllerControlRequest &request) final
    {
        controlRequests.append(request);
        if (!snapshot.session)
            return Utils::ResultError("The activation test session is missing");
        if (request.command == Data::ControllerControlCommand::AcquireControl) {
            if (controlMode == ControlMode::StallAcquire)
                return Utils::ResultOk;
            QTimer::singleShot(0, this, [this] {
                snapshot.session->controlLeaseOwnerSessionId
                    = snapshot.session->sessionId;
                snapshot.session->ownsControlLease = true;
                snapshot.controlProgress = successfulControlProgress(
                    Data::ControllerControlCommand::AcquireControl,
                    QStringLiteral("lease-acquired"));
                emit connectionSnapshotChanged();
            });
            return Utils::ResultOk;
        }
        if (request.command == Data::ControllerControlCommand::ReleaseControl) {
            if (controlMode == ControlMode::StallRelease)
                return Utils::ResultOk;
            QTimer::singleShot(0, this, [this] {
                snapshot.session->controlLeaseOwnerSessionId = 0;
                snapshot.session->ownsControlLease = false;
                snapshot.controlProgress = successfulControlProgress(
                    Data::ControllerControlCommand::ReleaseControl,
                    QStringLiteral("lease-released"));
                emit connectionSnapshotChanged();
            });
            return Utils::ResultOk;
        }
        return unsupported();
    }

    bool supportsPackageDeployment() const final { return true; }

    Utils::Result<> deployPackage(
        const Data::ControllerPackageDeploymentRequest &request) final
    {
        deploymentRequests.append(request);
        if (deploymentMode == DeploymentMode::RejectImmediately)
            return Utils::ResultError("The provider rejected package deployment");
        if (deploymentMode == DeploymentMode::Stall)
            return Utils::ResultOk;

        int completionDelayMs = 0;
        if (deploymentMode == DeploymentMode::ProgressThenSucceed) {
            completionDelayMs = 30;
            QTimer::singleShot(10, this, [this, request] {
                const QDateTime startedAt
                    = QDateTime::currentDateTimeUtc();
                Data::ControllerPackageDeploymentProgress progress;
                progress.operationId = request.operationId;
                progress.artifactSha256 = QCryptographicHash::hash(
                    request.artifact, QCryptographicHash::Sha256);
                progress.state
                    = Data::ControllerPackageDeploymentState::Uploading;
                progress.totalBytes = request.artifact.size();
                progress.transferredBytes = request.artifact.size() / 2;
                progress.previousActive = previousActive;
                progress.startedAt = startedAt;
                progress.audit.append(deploymentAuditEvent(
                    1,
                    Data::ControllerOperation::UploadPackage,
                    1,
                    {},
                    startedAt));
                snapshot.packageDeploymentProgress = progress;
                emit connectionSnapshotChanged();
            });
        }

        QTimer::singleShot(completionDelayMs, this, [this, request] {
            const QDateTime startedAt = QDateTime::currentDateTimeUtc();
            Data::ControllerPackageDeploymentProgress progress;
            progress.operationId = request.operationId;
            progress.artifactSha256 = QCryptographicHash::hash(
                request.artifact, QCryptographicHash::Sha256);
            progress.totalBytes = request.artifact.size();
            progress.previousActive = previousActive;
            progress.startedAt = startedAt;
            progress.audit.append(deploymentAuditEvent(
                1,
                Data::ControllerOperation::UploadPackage,
                {},
                {},
                startedAt));
            progress.audit.append(deploymentAuditEvent(
                2,
                Data::ControllerOperation::UploadPackage,
                1,
                {},
                startedAt));

            if (deploymentMode == DeploymentMode::OutcomeUnknown) {
                progress.state
                    = Data::ControllerPackageDeploymentState::OutcomeUnknown;
                progress.detail = QStringLiteral("deployment-outcome-unknown");
                progress.completedAt = startedAt;
                snapshot.packageDeploymentProgress = progress;
                emit connectionSnapshotChanged();
                return;
            }
            if (deploymentMode == DeploymentMode::FailTerminal) {
                progress.audit.append(deploymentAuditEvent(
                    3,
                    Data::ControllerOperation::UploadPackage,
                    1,
                    -21,
                    startedAt));
                progress.state = Data::ControllerPackageDeploymentState::Failed;
                progress.status = -21;
                progress.operationResult = 0;
                progress.detail = QStringLiteral("deployment-rejected");
                progress.completedAt = startedAt;
                snapshot.packageDeploymentProgress = progress;
                emit connectionSnapshotChanged();
                return;
            }

            quint64 sequence = 2;
            progress.audit.append(deploymentAuditEvent(
                ++sequence,
                Data::ControllerOperation::UploadPackage,
                1,
                0,
                startedAt));
            progress.audit.append(deploymentAuditEvent(
                ++sequence,
                Data::ControllerOperation::UploadPackage,
                2,
                {},
                startedAt));
            progress.audit.append(deploymentAuditEvent(
                ++sequence,
                Data::ControllerOperation::UploadPackage,
                2,
                0,
                startedAt));
            progress.audit.append(deploymentAuditEvent(
                ++sequence,
                Data::ControllerOperation::UploadPackage,
                3,
                {},
                startedAt));
            progress.audit.append(deploymentAuditEvent(
                ++sequence,
                Data::ControllerOperation::UploadPackage,
                3,
                0,
                startedAt));
            appendSixResponseExchange(
                progress,
                sequence,
                Data::ControllerOperation::ValidatePackage,
                4,
                startedAt);
            appendSixResponseExchange(
                progress,
                sequence,
                Data::ControllerOperation::ActivatePackage,
                5,
                startedAt);

            progress.state = Data::ControllerPackageDeploymentState::Succeeded;
            progress.transferredBytes = request.artifact.size();
            progress.candidate = candidate;
            progress.status = 0;
            progress.operationResult = 0;
            progress.detail = QStringLiteral("deployment-succeeded");
            progress.completedAt = startedAt;
            snapshot.packageDeploymentProgress = progress;
            if (snapshot.package) {
                snapshot.package->stagedSlot = candidate.slot;
                snapshot.package->stagedGeneration = candidate.generation;
                snapshot.package->stagedConfigurationId
                    = candidate.configurationId;
                snapshot.package->activeSlot = candidate.slot;
                snapshot.package->activeGeneration = candidate.generation;
                snapshot.package->activeConfigurationId
                    = candidate.configurationId;
                snapshot.package->controllerState
                    = Data::ControllerPackageState::Active;
            }
            if (snapshot.controllerState) {
                snapshot.controllerState->serviceState
                    = Data::ControllerServiceState::OperationalSafe;
            }
            emit runtimeResourceCatalogChanged();
            emit runtimeSemanticMappingAttestationChanged();
            emit connectionSnapshotChanged();
        });
        return Utils::ResultOk;
    }

    Utils::Result<> cancelPackageDeployment(const QString &) final
    {
        return unsupported();
    }

    bool supportsRuntimeResources() const final { return true; }

    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const final
    {
        return catalog;
    }

    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const final
    {
        return std::nullopt;
    }

    Utils::Result<> refreshRuntimeResources() final { return Utils::ResultOk; }

    Utils::Result<> requestRuntimeResourceSnapshot(
        const Data::RuntimeResourceSnapshotRequest &) final
    {
        return unsupported();
    }

    bool supportsRuntimeSemanticMappingAttestation() const final { return true; }

    std::optional<Data::RuntimeSemanticMappingAttestation>
    runtimeSemanticMappingAttestation() const final
    {
        return attestation;
    }

    Utils::Result<> requestRuntimeSemanticMappingAttestation(
        const Data::RuntimeSemanticMappingAttestationRequest &request) final
    {
        QTimer::singleShot(0, this, [this, request] {
            emit runtimeSemanticMappingAttestationRequestFinished(
                Data::RuntimeSemanticMappingAttestationResult{
                    request, attestation, {}});
        });
        return Utils::ResultOk;
    }

    static Data::ControllerControlProgress successfulControlProgress(
        Data::ControllerControlCommand command, const QString &detail)
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        return {
            command,
            Data::ControllerControlState::Succeeded,
            4,
            true,
            0,
            0,
            0,
            0,
            detail,
            now,
            now,
        };
    }

    static Data::ControllerPackageDeploymentAuditEvent deploymentAuditEvent(
        quint64 sequence,
        Data::ControllerOperation operation,
        std::optional<quint64> requestId,
        std::optional<qint32> status,
        const QDateTime &occurredAt)
    {
        return {
            sequence,
            operation,
            requestId,
            status,
            status ? std::optional<qint32>(0) : std::nullopt,
            QStringLiteral("activation-test-deployment"),
            occurredAt,
        };
    }

    static void appendSixResponseExchange(
        Data::ControllerPackageDeploymentProgress &progress,
        quint64 &sequence,
        Data::ControllerOperation operation,
        quint64 requestId,
        const QDateTime &startedAt)
    {
        progress.audit.append(deploymentAuditEvent(
            ++sequence,
            operation,
            requestId,
            {},
            startedAt));
        for (int index = 0; index < 6; ++index) {
            progress.audit.append(deploymentAuditEvent(
                ++sequence,
                operation,
                requestId,
                0,
                startedAt));
        }
    }

    static Utils::Result<> unsupported()
    {
        return Utils::ResultError(
            "The activation test provider does not support this operation");
    }

    DeploymentMode deploymentMode = DeploymentMode::Succeed;
    ControlMode controlMode = ControlMode::Complete;
    Data::ControllerPackageSelector previousActive;
    Data::ControllerPackageSelector candidate;
    Data::ControllerConnectionSnapshot snapshot;
    std::optional<Data::RuntimeResourceCatalog> catalog;
    std::optional<Data::RuntimeSemanticMappingAttestation> attestation;
    QList<Data::ControllerControlRequest> controlRequests;
    QList<Data::ControllerPackageDeploymentRequest> deploymentRequests;
};

class DeterministicOutputControllerProvider final : public Core::ControllerConnectionProvider
{
public:
    explicit DeterministicOutputControllerProvider(Utils::Id id)
        : ControllerConnectionProvider(
              id, QStringLiteral("Semantic Runtime deterministic output controller"))
    {}

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }

    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &, const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final { return snapshot; }
    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &) final
    {
        return unsupported();
    }
    Utils::Result<> disconnectFromController() final { return unsupported(); }
    Utils::Result<> refreshController() final { return unsupported(); }

    bool supportsRuntimeResources() const final { return true; }
    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const final
    {
        return catalog;
    }
    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const final
    {
        return resourceSnapshot;
    }
    Utils::Result<> refreshRuntimeResources() final { return unsupported(); }
    Utils::Result<> requestRuntimeResourceSnapshot(
        const Data::RuntimeResourceSnapshotRequest &request) final
    {
        if (!request.isValid())
            return Utils::ResultError("The deterministic snapshot request is invalid");
        const Utils::Result<> started = beginRequest(false);
        if (!started)
            return started;
        pendingSnapshotRequest = request;
        snapshotRequests.append(request);
        requestTrace.append(
            request.correlationId.startsWith(QStringLiteral("semantic-live-"))
                ? QStringLiteral("snapshot-live")
                : (request.correlationId.endsWith(QStringLiteral("-after"))
                       ? QStringLiteral("snapshot-after")
                       : QStringLiteral("snapshot-before")));
        if (snapshotRequestStarted)
            snapshotRequestStarted(request);
        if (failSnapshotStartAfterCallback)
            return Utils::ResultError(snapshotStartError);
        return Utils::ResultOk;
    }

    bool supportsRuntimeSemanticMappingAttestation() const final { return true; }
    std::optional<Data::RuntimeSemanticMappingAttestation>
    runtimeSemanticMappingAttestation() const final
    {
        return semanticMappingAttestation;
    }
    Utils::Result<> requestRuntimeSemanticMappingAttestation(
        const Data::RuntimeSemanticMappingAttestationRequest &) final
    {
        return unsupported();
    }

    bool supportsRuntimeOutputTransactions() const final { return true; }
    Utils::Result<> requestRuntimeOutputGroupPolicy(
        const Data::RuntimeOutputGroupPolicyRequest &request) final
    {
        if (!request.isValid())
            return Utils::ResultError("The deterministic policy request is invalid");
        const Utils::Result<> started = beginRequest(false);
        if (!started)
            return started;
        pendingPolicyRequest = request;
        policyRequests.append(request);
        requestTrace.append(QStringLiteral("policy"));
        return Utils::ResultOk;
    }

    Utils::Result<> requestRuntimeOutputTransactionState(
        const Data::RuntimeOutputTransactionStateRequest &request) final
    {
        if (!request.isValid())
            return Utils::ResultError("The deterministic state request is invalid");
        const Utils::Result<> started = beginRequest(false);
        if (!started)
            return started;
        pendingStateRequest = request;
        stateRequests.append(request);
        requestTrace.append(
            request.correlationId.endsWith(QStringLiteral("-post-state"))
                ? QStringLiteral("state-post")
                : QStringLiteral("state-pre"));
        return Utils::ResultOk;
    }

    Utils::Result<> applyRuntimeOutputTransaction(
        const Data::RuntimeOutputTransactionRequest &request) final
    {
        if (!request.isValid())
            return Utils::ResultError("The deterministic output request is invalid");
        const Utils::Result<> started = beginRequest(true);
        if (!started)
            return started;
        pendingApplyRequest = request;
        applyRequests.append(request);
        requestTrace.append(QStringLiteral("apply"));
        return Utils::ResultOk;
    }

    void publishConnectionSnapshot(const Data::ControllerConnectionSnapshot &value)
    {
        snapshot = value;
        emit connectionSnapshotChanged();
    }

    void publishRuntimeContext(
        const Data::RuntimeResourceCatalog &newCatalog,
        const Data::RuntimeResourceSnapshot &newSnapshot,
        const Data::RuntimeSemanticMappingAttestation &newAttestation,
        const Data::ControllerConnectionSnapshot &connection)
    {
        catalog = newCatalog;
        resourceSnapshot = newSnapshot;
        semanticMappingAttestation = newAttestation;
        snapshot = connection;
        emit connectionSnapshotChanged();
        emit runtimeResourceCatalogChanged();
        emit runtimeSemanticMappingAttestationChanged();
        emit runtimeResourceSnapshotChanged();
    }

    void sendSnapshotResult(
        const Data::RuntimeResourceSnapshotResult &result, bool completePendingRequest = true)
    {
        if (completePendingRequest)
            completeRequest(pendingSnapshotRequest);
        emit runtimeResourceSnapshotRequestFinished(result);
    }

    void sendPolicyResult(
        const Data::RuntimeOutputGroupPolicyResult &result, bool completePendingRequest = true)
    {
        if (completePendingRequest)
            completeRequest(pendingPolicyRequest);
        emit runtimeOutputGroupPolicyRequestFinished(result);
    }

    void sendStateResult(
        const Data::RuntimeOutputTransactionStateResult &result,
        bool completePendingRequest = true)
    {
        if (completePendingRequest)
            completeRequest(pendingStateRequest);
        emit runtimeOutputTransactionStateRequestFinished(result);
    }

    void sendApplyResult(
        const Data::RuntimeOutputTransactionResult &result, bool completePendingRequest = true)
    {
        if (completePendingRequest)
            completeRequest(pendingApplyRequest);
        emit runtimeOutputTransactionFinished(result);
    }

    int pendingRequestCount() const
    {
        return int(pendingSnapshotRequest.has_value()) + int(pendingPolicyRequest.has_value())
               + int(pendingStateRequest.has_value()) + int(pendingApplyRequest.has_value());
    }

    Data::ControllerConnectionSnapshot snapshot;
    std::optional<Data::RuntimeResourceCatalog> catalog;
    std::optional<Data::RuntimeResourceSnapshot> resourceSnapshot;
    std::optional<Data::RuntimeSemanticMappingAttestation> semanticMappingAttestation;
    std::optional<Data::RuntimeResourceSnapshotRequest> pendingSnapshotRequest;
    std::optional<Data::RuntimeOutputGroupPolicyRequest> pendingPolicyRequest;
    std::optional<Data::RuntimeOutputTransactionStateRequest> pendingStateRequest;
    std::optional<Data::RuntimeOutputTransactionRequest> pendingApplyRequest;
    QList<Data::RuntimeResourceSnapshotRequest> snapshotRequests;
    QList<Data::RuntimeOutputGroupPolicyRequest> policyRequests;
    QList<Data::RuntimeOutputTransactionStateRequest> stateRequests;
    QList<Data::RuntimeOutputTransactionRequest> applyRequests;
    QStringList requestTrace;
    int maximumConcurrentRequests = 0;
    bool allowReadsWhileApplyPending = false;
    std::function<void(const Data::RuntimeResourceSnapshotRequest &)> snapshotRequestStarted;
    bool failSnapshotStartAfterCallback = false;
    QString snapshotStartError = QStringLiteral(
        "The deterministic snapshot start failed after callback");

private:
    Utils::Result<> beginRequest(bool mutation)
    {
        const int currentRequests = pendingRequestCount();
        const bool readBesideUnresolvedApply
            = allowReadsWhileApplyPending && !mutation && pendingApplyRequest
              && currentRequests == 1;
        if (currentRequests && !readBesideUnresolvedApply)
            return Utils::ResultError("The deterministic provider already has a request");
        maximumConcurrentRequests = std::max(maximumConcurrentRequests, currentRequests + 1);
        return Utils::ResultOk;
    }

    template<typename Request>
    void completeRequest(std::optional<Request> &request)
    {
        request.reset();
    }

    static Utils::Result<> unsupported()
    {
        return Utils::ResultError("The deterministic provider does not support this operation");
    }
};

class RegisteredObject
{
public:
    explicit RegisteredObject(QObject *object)
        : m_object(object)
    {
        ExtensionSystem::PluginManager::addObject(m_object);
    }

    ~RegisteredObject() { remove(); }

    void remove()
    {
        if (!m_object)
            return;
        ExtensionSystem::PluginManager::removeObject(m_object);
        m_object = nullptr;
    }

private:
    QObject *m_object = nullptr;
};

static Utils::Id nextActivationAdapterProviderId()
{
    static quint64 sequence = 0;
    return Utils::Id::fromString(
        QStringLiteral("EtherCAT.SemanticRuntime.Tests.ActivationAdapter.%1")
            .arg(++sequence));
}

class ActivationAdapterProvider final : public Core::DeviceAdapterProvider
{
public:
    explicit ActivationAdapterProvider(
        QList<Data::DeviceAdapterManifest> manifests)
        : DeviceAdapterProvider(
              nextActivationAdapterProviderId(),
              "Trusted activation test adapters")
        , manifests(std::move(manifests))
    {
        setAvailable(true);
    }

    QList<Data::DeviceAdapterManifest> adapterManifests() const final
    {
        return manifests;
    }

    std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &adapterId,
        const QString &version) const final
    {
        const auto found = std::find_if(
            manifests.cbegin(),
            manifests.cend(),
            [&adapterId, &version](const Data::DeviceAdapterManifest &manifest) {
                return manifest.id == adapterId
                       && manifest.version == version;
            });
        if (found == manifests.cend())
            return std::nullopt;
        return *found;
    }

    Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &) const final
    {
        return {
            false,
            {},
            QStringLiteral(
                "The activation test adapter provider exposes manifests only."),
        };
    }

    void replaceManifests(QList<Data::DeviceAdapterManifest> replacement)
    {
        manifests = std::move(replacement);
        emit adapterManifestsChanged();
    }

    QList<Data::DeviceAdapterManifest> manifests;
};

static Data::ControllerConnectionScope projectScope(const Data::ProjectSnapshot &project)
{
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind == Data::ProjectNodeKind::Master)
            return {project.id, node.id};
    }
    return {};
}

static Data::SemanticBindingArtifactReference bindingArtifact()
{
    return {
        QStringLiteral("binding/embed-labs/runtime/1"),
        QByteArray(32, '\x6b'),
        QByteArray(32, '\x7c'),
        {},
    };
}

static Data::ProjectSnapshot testProject(bool includeBinding = true, bool includeAdapters = false)
{
    Data::ProjectSnapshot project;
    project.id = Data::NodeId::create();
    project.name = QStringLiteral("Semantic Runtime Test");
    project.formatVersion = 4;
    project.createdBy = QStringLiteral("EtherCATSemanticRuntimeTests");
    project.valid = true;

    const Data::NodeId masterId = Data::NodeId::create();
    project.nodes = {
        {project.id, {}, Data::ProjectNodeKind::Project, project.name},
        {masterId, project.id, Data::ProjectNodeKind::Master, QStringLiteral("Master")},
    };
    if (includeBinding)
        project.masterBindingArtifact = bindingArtifact();

    if (includeAdapters) {
        Data::OfflineSlaveConfiguration xb6;
        xb6.id = Data::NodeId::create();
        xb6.masterId = masterId;
        xb6.position = 0;
        xb6.name = QStringLiteral("XB6-EC0002");
        xb6.esiSha256 = QByteArray(32, '\x11');
        xb6.adapterSelection = {
            Data::DeviceAdapterId{QStringLiteral("org.embedlabs.adapter.solidot.xb6-ec0002.rev1")},
            QStringLiteral("0.1.0"),
            QByteArray(32, '\x21'),
            QStringLiteral("do16-default"),
            {},
        };

        Data::OfflineSlaveConfiguration sv630n;
        sv630n.id = Data::NodeId::create();
        sv630n.masterId = masterId;
        sv630n.position = 1;
        sv630n.name = QStringLiteral("SV630N_1Axis_03716");
        sv630n.esiSha256 = QByteArray(32, '\x12');
        sv630n.adapterSelection = {
            Data::DeviceAdapterId{
                QStringLiteral("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000")},
            QStringLiteral("1.4.0"),
            QByteArray(32, '\x22'),
            QStringLiteral("sync0-125us"),
            {},
        };
        project.slaves = {xb6, sv630n};
    }
    return project;
}

static Data::ControllerConnectionSnapshot connectedSnapshot(
    const Data::ControllerConnectionScope &scope, quint64 sessionGeneration)
{
    Data::ControllerConnectionSnapshot snapshot;
    snapshot.scope = scope;
    snapshot.state = Data::ControllerConnectionState::Connected;
    snapshot.sessionGeneration = sessionGeneration;
    snapshot.readOnly = false;
    return snapshot;
}

static Data::RuntimeResourceCatalogEpoch catalogEpoch(quint64 runtimeGeneration = 5)
{
    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = 0x1122334455667788;
    epoch.activePackageSlot = Data::ControllerSlot::A;
    epoch.activePackageGeneration = 3;
    epoch.configurationId = 813;
    epoch.topologyGeneration = 4;
    epoch.runtimeGeneration = runtimeGeneration;
    epoch.catalogRevision = 6;
    epoch.topologyIdentity = QByteArray(32, '\x33');
    return epoch;
}

static Data::RuntimeResourceCatalog resourceCatalog(
    const Data::ControllerConnectionScope &scope,
    quint64 sessionGeneration,
    const Data::RuntimeResourceCatalogEpoch &epoch = catalogEpoch())
{
    Data::RuntimeResourceCatalog catalog;
    catalog.scope = scope;
    catalog.sessionGeneration = sessionGeneration;
    catalog.epoch = epoch;

    Data::RuntimeResourceDescriptor descriptor;
    descriptor.id.value = QByteArray::fromHex("1000000000000001");
    descriptor.componentInstanceId.value = QByteArray::fromHex("2000000000000001");
    descriptor.consistencyGroupId.value = QByteArray::fromHex("00000001");
    descriptor.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    descriptor.bitWidth = 16;
    descriptor.direction = Data::RuntimeResourceDirection::Input;
    descriptor.access = Data::RuntimeResourceAccess::ReadOnly;
    catalog.resources = {descriptor};
    return catalog;
}

static QString detailFor(SemanticRuntimeContextIssue issue)
{
    return semanticRuntimeContextIssueDetail(issue);
}

static QByteArray fromHex(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

static QByteArray readTestData(const QString &relativePath)
{
    QFile file(
        QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR) + QLatin1Char('/')
        + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void EtherCATSemanticRuntimeTests::testCanonicalJsonRoundTrip()
{
    const QByteArray canonical = "{\"array\":[true,false,null],\"control\":\"\\u0001\","
                                 "\"large\":18446744073709551615,\"negative\":-9223372036854775808,"
                                 "\"precise\":9007199254740993,\"unicode\":\"\\ud83d\\ude00\"}\n";

    const Utils::Result<StrictJson> parsed = parseCanonicalJson(canonical, canonical.size());
    QVERIFY_RESULT(parsed);
    QVERIFY(parsed->at("large").is_number_unsigned());
    QCOMPARE(parsed->at("large").get<quint64>(), std::numeric_limits<quint64>::max());
    QVERIFY(parsed->at("negative").is_number_integer());
    QCOMPARE(parsed->at("negative").get<qint64>(), std::numeric_limits<qint64>::min());
    QCOMPARE(parsed->at("precise").get<quint64>(), quint64(9007199254740993ULL));

    const Utils::Result<QByteArray> serialized = serializeCanonicalJson(*parsed, canonical.size());
    QVERIFY_RESULT(serialized);
    QCOMPARE(*serialized, canonical);

    const QByteArray pretty = "{\n  \"value\": 1\n}\n";
    const Utils::Result<StrictJson> strict = parseStrictJson(pretty, pretty.size());
    QVERIFY_RESULT(strict);
    QVERIFY(!parseCanonicalJson(pretty, pretty.size()));
}

void EtherCATSemanticRuntimeTests::testCanonicalJsonRejectsAmbiguity()
{
    const std::array<QByteArray, 17> invalidJson{
        QByteArray(),
        QByteArray("{\"a\":1,\"a\":2}"),
        QByteArray("{\"a\":1,\"\\u0061\":2}"),
        QByteArray("{\"outer\":{\"a\":1,\"a\":2}}"),
        QByteArray("{\"float\":1.0}"),
        QByteArray("{\"exponent\":1e2}"),
        QByteArray("{\"overflow\":18446744073709551616}"),
        QByteArray("{\"underflow\":-9223372036854775809}"),
        QByteArray("{\"unterminated\":"),
        QByteArray("{\"invalid\":\"\xc0\xaf\"}"),
        QByteArray("\xef\xbb\xbf{\"a\":1}\n"),
        QByteArray("{\"a\":1}\r\n"),
        QByteArray("{\"a\":1}"),
        QByteArray("{\"a\":1}\n\n"),
        QByteArray("{ \"a\":1}\n"),
        QByteArray("{\"b\":1,\"a\":2}\n"),
        QByteArray("{\"unicode\":\"\xf0\x9f\x98\x80\"}\n"),
    };

    for (const QByteArray &input : invalidJson)
        QVERIFY(!parseCanonicalJson(input, qMax<qsizetype>(input.size(), 1)));

    QByteArray tooDeep;
    for (int depth = 0; depth < 65; ++depth)
        tooDeep.append('[');
    tooDeep.append('0');
    for (int depth = 0; depth < 65; ++depth)
        tooDeep.append(']');
    QVERIFY(!parseStrictJson(tooDeep, tooDeep.size()));

    StrictJson floating = StrictJson::object();
    floating["value"] = 1.5;
    QVERIFY(!serializeCanonicalJson(floating, 128));

    const StrictJson valid = StrictJson::object({{"value", 1}});
    QVERIFY(!serializeCanonicalJson(valid, 11));
    const Utils::Result<QByteArray> exactLimit = serializeCanonicalJson(valid, 12);
    QVERIFY_RESULT(exactLimit);
    QCOMPARE(*exactLimit, QByteArray("{\"value\":1}\n"));
}

void EtherCATSemanticRuntimeTests::testCanonicalJsonTransferredManifests()
{
    const std::array<QString, 2> manifests{
        "testdata/api035-manifest.json",
        "testdata/api036-manifest.json",
    };
    for (const QString &path : manifests) {
        const QByteArray bytes = readTestData(path);
        QVERIFY2(!bytes.isEmpty(), qPrintable(path));
        const Utils::Result<StrictJson> parsed = parseCanonicalJson(bytes, 1024 * 1024);
        QVERIFY_RESULT(parsed);
        const Utils::Result<QByteArray> serialized = serializeCanonicalJson(*parsed, 1024 * 1024);
        QVERIFY_RESULT(serialized);
        QCOMPARE(*serialized, bytes);
    }
}

namespace {

constexpr std::array<const char *, 6> canonicalTestEntryNames{
    "manifest.json",
    "capability.bin",
    "configuration.ecfg",
    "runtime.erun",
    "compile_report.json",
    "manifest.sig",
};

struct CanonicalTestEcpkg
{
    QByteArray wire;
    std::array<qsizetype, 6> localOffsets;
    std::array<qsizetype, 6> dataOffsets;
    std::array<qsizetype, 6> centralOffsets;
    qsizetype centralOffset = 0;
    qsizetype eocdOffset = 0;
};

static void appendLe16(QByteArray &bytes, quint16 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
}

static void appendLe32(QByteArray &bytes, quint32 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
    bytes.append(char((value >> 16) & 0xff));
    bytes.append(char((value >> 24) & 0xff));
}

static void putLe16(QByteArray &bytes, qsizetype offset, quint16 value)
{
    bytes[offset] = char(value & 0xff);
    bytes[offset + 1] = char((value >> 8) & 0xff);
}

static void putLe32(QByteArray &bytes, qsizetype offset, quint32 value)
{
    bytes[offset] = char(value & 0xff);
    bytes[offset + 1] = char((value >> 8) & 0xff);
    bytes[offset + 2] = char((value >> 16) & 0xff);
    bytes[offset + 3] = char((value >> 24) & 0xff);
}

static quint32 testZipCrc32(QByteArrayView bytes)
{
    quint32 crc = std::numeric_limits<quint32>::max();
    for (char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 lowBitMask = quint32(0) - (crc & 1);
            crc = (crc >> 1) ^ (0xedb88320 & lowBitMask);
        }
    }
    return ~crc;
}

static std::array<QByteArray, 6> canonicalTestPayloads()
{
    return {
        QByteArray("{\"format\":\"test\"}\n"),
        QByteArray::fromHex("01020304"),
        QByteArray::fromHex("454346470100"),
        QByteArray::fromHex("4552554e0100"),
        QByteArray("{\"result\":\"ok\"}\n"),
        QByteArray(64, char(0xa5)),
    };
}

static std::array<QByteArray, 6> canonicalTestNames()
{
    std::array<QByteArray, 6> names;
    for (std::size_t index = 0; index < names.size(); ++index)
        names[index] = canonicalTestEntryNames[index];
    return names;
}

static CanonicalTestEcpkg buildCanonicalTestEcpkg(
    const std::array<QByteArray, 6> &payloads = canonicalTestPayloads(),
    const std::array<QByteArray, 6> &names = canonicalTestNames())
{
    CanonicalTestEcpkg package;
    std::array<quint32, 6> crcs;

    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const QByteArray &name = names[index];
        const QByteArray &payload = payloads[index];
        package.localOffsets[index] = package.wire.size();
        crcs[index] = testZipCrc32(payload);

        appendLe32(package.wire, 0x04034b50);
        appendLe16(package.wire, 20);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 33);
        appendLe32(package.wire, crcs[index]);
        appendLe32(package.wire, quint32(payload.size()));
        appendLe32(package.wire, quint32(payload.size()));
        appendLe16(package.wire, quint16(name.size()));
        appendLe16(package.wire, 0);
        package.wire.append(name);
        package.dataOffsets[index] = package.wire.size();
        package.wire.append(payload);
    }

    package.centralOffset = package.wire.size();
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const QByteArray &name = names[index];
        const QByteArray &payload = payloads[index];
        package.centralOffsets[index] = package.wire.size();

        appendLe32(package.wire, 0x02014b50);
        appendLe16(package.wire, 0x0314);
        appendLe16(package.wire, 20);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 33);
        appendLe32(package.wire, crcs[index]);
        appendLe32(package.wire, quint32(payload.size()));
        appendLe32(package.wire, quint32(payload.size()));
        appendLe16(package.wire, quint16(name.size()));
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe32(package.wire, 0x81a40000);
        appendLe32(package.wire, quint32(package.localOffsets[index]));
        package.wire.append(name);
    }

    package.eocdOffset = package.wire.size();
    appendLe32(package.wire, 0x06054b50);
    appendLe16(package.wire, 0);
    appendLe16(package.wire, 0);
    appendLe16(package.wire, 6);
    appendLe16(package.wire, 6);
    appendLe32(package.wire, quint32(package.eocdOffset - package.centralOffset));
    appendLe32(package.wire, quint32(package.centralOffset));
    appendLe16(package.wire, 0);
    return package;
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

static bool writeFile(const QString &path, QByteArrayView bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(bytes.data(), bytes.size()) == bytes.size()
           && file.flush();
}

static QString systemTemporaryDirectoryTemplate(QStringView stem)
{
    QString temporaryRoot = QDir::tempPath();
    const QString canonicalRoot = QFileInfo(temporaryRoot).canonicalFilePath();
    if (!canonicalRoot.isEmpty())
        temporaryRoot = canonicalRoot;
    return QDir(temporaryRoot).filePath(stem.toString() + QStringLiteral("-XXXXXX"));
}

static QByteArray factoryOpaqueId(quint64 value)
{
    QByteArray bytes(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

static QByteArray factoryOpaqueId(quint32 value)
{
    QByteArray bytes(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

static Data::RuntimeResourcePrimitiveType factoryPrimitive(EcfgResourcePrimitive primitive)
{
    using RuntimePrimitive = Data::RuntimeResourcePrimitiveType;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return RuntimePrimitive::Boolean;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        return RuntimePrimitive::UnsignedInteger;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        return RuntimePrimitive::SignedInteger;
    case EcfgResourcePrimitive::Q32_32:
        return RuntimePrimitive::FloatingPoint;
    case EcfgResourcePrimitive::RawBits:
        return RuntimePrimitive::ByteArray;
    }
    return RuntimePrimitive::Opaque;
}

static QByteArray factoryValueTypeIdentity(EcfgResourcePrimitive primitive, quint16 bitWidth)
{
    return QByteArray("ethercat.runtime.value/primitive-") + QByteArray::number(quint8(primitive))
           + "/bits-" + QByteArray::number(bitWidth);
}

static Data::RuntimeResourceTypedValue factorySafeValue(const VerifiedSemanticBinding &binding)
{
    Data::RuntimeResourceTypedValue value;
    value.primitiveType = factoryPrimitive(binding.primitive);
    value.typeIdentity = factoryValueTypeIdentity(binding.primitive, binding.bitWidth);

    const quint64 unsignedValue = std::holds_alternative<quint64>(*binding.safeValue)
                                      ? std::get<quint64>(*binding.safeValue)
                                      : quint64(std::get<qint64>(*binding.safeValue));
    const qint64 signedValue = std::holds_alternative<qint64>(*binding.safeValue)
                                   ? std::get<qint64>(*binding.safeValue)
                                   : qint64(std::get<quint64>(*binding.safeValue));
    QByteArray bigEndian = binding.safeValueLittleEndian;
    std::reverse(bigEndian.begin(), bigEndian.end());

    switch (binding.primitive) {
    case EcfgResourcePrimitive::Bool:
        value.value = bool(unsignedValue);
        break;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
    case EcfgResourcePrimitive::U64:
        value.value = QVariant::fromValue<qulonglong>(unsignedValue);
        break;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
    case EcfgResourcePrimitive::S64:
        value.value = QVariant::fromValue<qlonglong>(signedValue);
        break;
    case EcfgResourcePrimitive::Q32_32:
        value.value = double(signedValue) / 4294967296.0;
        value.opaqueRepresentation = bigEndian;
        break;
    case EcfgResourcePrimitive::RawBits:
        value.value = bigEndian;
        break;
    }
    return value;
}

static Data::ProjectSnapshot factoryProject(const VerifiedRuntimePackageEvidence &evidence)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    Data::ProjectSnapshot project;
    project.id = Data::NodeId::create();
    project.name = QStringLiteral("API-037 semantic binding factory");
    project.formatVersion = 7;
    project.createdBy = QStringLiteral("EtherCATSemanticRuntimeTests");
    project.valid = true;

    const Data::NodeId masterId = Data::NodeId::create();
    project.nodes = {
        {project.id, {}, Data::ProjectNodeKind::Project, project.name},
        {masterId, project.id, Data::ProjectNodeKind::Master, QStringLiteral("Master")},
    };
    project.masterBindingArtifact.artifactId = QStringLiteral("test/api037/semantic-binding-v2");
    project.masterBindingArtifact.artifactSha256 = evidence.semanticMappingProof().mappingSha256;
    project.masterBindingArtifact.projectConfigurationSha256 = evidence.projectConfigurationSha256();

    for (const VerifiedSemanticDevice &device : artifact.devices) {
        Data::OfflineSlaveConfiguration slave;
        slave.id = Data::NodeId::create();
        slave.masterId = masterId;
        slave.position = device.position;
        slave.name = device.projectDeviceId;
        slave.esiSha256 = device.esiSha256;
        const auto topology = std::find_if(
            artifact.topologyInstances.cbegin(),
            artifact.topologyInstances.cend(),
            [&device](const SemanticBindingTopologyInstance &candidate) {
                return candidate.position == device.position
                       && candidate.stationAddress == device.stationAddress;
            });
        if (topology != artifact.topologyInstances.cend()) {
            slave.stationAddress = topology->stationAddress;
            slave.identity.vendorId = topology->vendorId;
            slave.identity.productCode = topology->productCode;
            slave.identity.revisionNumber = topology->revision.value_or(0);
            slave.serialNumber = topology->serial.value_or(0);
        }
        slave.adapterSelection.adapterId = {device.adapterId};
        slave.adapterSelection.adapterVersion = device.adapterVersion;
        slave.adapterSelection.adapterContentSha256 = device.adapterSha256;
        project.slaves.append(slave);
        project.nodes.append({slave.id, masterId, Data::ProjectNodeKind::Slave, slave.name});
        project.masterBindingArtifact.projectDeviceBindings.append(
            {slave.id, device.projectDeviceId});
    }
    return project;
}

static std::optional<Data::EtherCATDataType> factoryDataType(EcfgResourcePrimitive primitive)
{
    using Type = Data::EtherCATDataType;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        return Type::Boolean;
    case EcfgResourcePrimitive::U8:
        return Type::UnsignedInteger8;
    case EcfgResourcePrimitive::S8:
        return Type::Integer8;
    case EcfgResourcePrimitive::U16:
        return Type::UnsignedInteger16;
    case EcfgResourcePrimitive::S16:
        return Type::Integer16;
    case EcfgResourcePrimitive::U32:
        return Type::UnsignedInteger32;
    case EcfgResourcePrimitive::S32:
        return Type::Integer32;
    case EcfgResourcePrimitive::U64:
        return Type::UnsignedInteger64;
    case EcfgResourcePrimitive::S64:
        return Type::Integer64;
    case EcfgResourcePrimitive::Q32_32:
        return Type::Real64;
    case EcfgResourcePrimitive::RawBits:
        return Type::OctetString;
    }
    return {};
}

static std::optional<Data::EngineeringConstraint> factoryConstraint(
    EcfgResourcePrimitive primitive, quint16 bitWidth)
{
    Data::EngineeringConstraint result;
    switch (primitive) {
    case EcfgResourcePrimitive::Bool:
        result.minimum = Data::ExactRational{0, 1};
        result.maximum = Data::ExactRational{1, 1};
        break;
    case EcfgResourcePrimitive::U8:
    case EcfgResourcePrimitive::U16:
    case EcfgResourcePrimitive::U32:
        result.minimum = Data::ExactRational{0, 1};
        result.maximum = Data::ExactRational{
            bitWidth == 32 ? qint64(std::numeric_limits<quint32>::max())
                           : qint64((quint64(1) << bitWidth) - 1),
            1,
        };
        break;
    case EcfgResourcePrimitive::S8:
    case EcfgResourcePrimitive::S16:
    case EcfgResourcePrimitive::S32:
        result.minimum = Data::ExactRational{-(qint64(1) << (bitWidth - 1)), 1};
        result.maximum = Data::ExactRational{(qint64(1) << (bitWidth - 1)) - 1, 1};
        break;
    case EcfgResourcePrimitive::U64:
    case EcfgResourcePrimitive::S64:
    case EcfgResourcePrimitive::Q32_32:
    case EcfgResourcePrimitive::RawBits:
        return {};
    }
    return result;
}

static std::optional<Data::EngineeringValue> factoryEngineeringValue(
    EcfgResourcePrimitive primitive, const std::variant<qint64, quint64> &raw)
{
    if (primitive == EcfgResourcePrimitive::Bool) {
        const quint64 value = std::holds_alternative<quint64>(raw)
                                  ? std::get<quint64>(raw)
                                  : quint64(std::get<qint64>(raw));
        if (value > 1)
            return {};
        return Data::EngineeringValue::fromBoolean(value != 0);
    }
    if (primitive == EcfgResourcePrimitive::U8 || primitive == EcfgResourcePrimitive::U16
        || primitive == EcfgResourcePrimitive::U32
        || primitive == EcfgResourcePrimitive::U64) {
        if (!std::holds_alternative<quint64>(raw))
            return {};
        return Data::EngineeringValue::fromUnsignedInteger(std::get<quint64>(raw));
    }
    if (!std::holds_alternative<qint64>(raw))
        return {};
    return Data::EngineeringValue::fromSignedInteger(std::get<qint64>(raw));
}

struct Api038UpperAdapterIdentity
{
    QString id;
    QString version;
    QByteArray contentSha256;
    QString processDataProfileId;
    QList<quint16> rxPdos;
    QList<quint16> txPdos;
    QString localGroupId;
};

// These are the exact bundled V3 identities covered by the DeviceAdapters
// parser tests. This fixture projects their API-038 action contract from the
// independently verified signed package instead of granting a test-only
// runtime authorization.
static std::optional<Api038UpperAdapterIdentity> api038UpperAdapterIdentity(
    QStringView lowerAdapterId)
{
    if (lowerAdapterId == u"solidot.xb6_ec0002_rev1_do16") {
        return Api038UpperAdapterIdentity{
            QStringLiteral("org.embedlabs.adapter.solidot.xb6-ec0002.rev1"),
            QStringLiteral("0.3.1"),
            QByteArray::fromHex(
                "1b95083165cf124054069bd9897837d09bc414e8f375bbd77a0f9ddc1bcfa84c"),
            QStringLiteral("org.embedlabs.solidot.xb6.api038-do16"),
            {0x1600, 0x16ff},
            {0x1aff},
            QStringLiteral("manual_do16"),
        };
    }
    if (lowerAdapterId == u"inovance.sv630n_1axis_rev00010000_csp") {
        return Api038UpperAdapterIdentity{
            QStringLiteral("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"),
            QStringLiteral("0.3.0"),
            QByteArray::fromHex(
                "acf63ee7c837cf39e87fa510aeb8aadfac89b393d04b6cf28926037499a825b9"),
            QStringLiteral("org.embedlabs.inovance.sv630n.api038-csp-1704-1b04"),
            {0x1704},
            {0x1b04},
            QStringLiteral("manual_velocity"),
        };
    }
    return {};
}

static Utils::Result<QList<Data::DeviceAdapterManifest>> publishedApi038V3ActionAdapters()
{
    auto *providerRegistry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    if (!providerRegistry)
        return Utils::ResultError("The controller provider registry is unavailable");

    QList<Data::DeviceAdapterManifest> result;
    QSet<QString> identities;
    for (Core::Provider *provider :
         providerRegistry->providers(Core::ProviderKind::DeviceAdapter)) {
        auto *adapterProvider = qobject_cast<Core::DeviceAdapterProvider *>(provider);
        if (!adapterProvider || !adapterProvider->isAvailable())
            continue;
        for (const Data::DeviceAdapterManifest &manifest :
             adapterProvider->adapterManifests()) {
            const std::optional<Api038UpperAdapterIdentity> identity
                = api038UpperAdapterIdentity(manifest.controllerAdapterTarget.adapterId);
            if (!identity || manifest.contractVersion != Data::DeviceAdapterContractVersion::V3
                || manifest.id.value != identity->id || manifest.version != identity->version
                || manifest.contentSha256 != identity->contentSha256) {
                continue;
            }
            const QString key = manifest.id.value + QLatin1Char('@') + manifest.version;
            if (identities.contains(key))
                return Utils::ResultError("A published API-038 V3 adapter is ambiguous");
            identities.insert(key);
            result.append(manifest);
        }
    }
    if (result.size() != 2)
        return Utils::ResultError("The two published API-038 V3 adapters are unavailable");
    return result;
}

constexpr auto api038HardwareEnableVariable = "QTC_ETHER_CAT_API038_HARDWARE";
constexpr auto api038HardwareConfirmationVariable = "QTC_ETHER_CAT_API038_CONFIRM";
constexpr auto api038HardwareConfirmation
    = "API038_CFG3701_D0B8EDD70B5ECEC53252AC4B09DC9665_XB6_DO16";
constexpr auto api038PackageSha256
    = "d0b8edd70b5ecec53252ac4b09dc9665ac82b91178d18e2d94222b9a538165d0";

enum class Api038HardwareAuthorization {
    Disabled,
    Invalid,
    Authorized,
};

static Api038HardwareAuthorization api038HardwareAuthorization(
    QByteArrayView enabled, QByteArrayView confirmation)
{
    if (enabled != QByteArrayView("1"))
        return Api038HardwareAuthorization::Disabled;
    if (confirmation != QByteArrayView(api038HardwareConfirmation))
        return Api038HardwareAuthorization::Invalid;
    return Api038HardwareAuthorization::Authorized;
}

static Utils::Result<> validateApi038LocalEvidence(
    const VerifiedRuntimePackageEvidence &evidence)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    const QByteArray expectedPackageSha256 = QByteArray::fromHex(api038PackageSha256);
    if (!evidence.isValid() || artifact.trust != EcpkgTrustClass::Production
        || artifact.packageSha256 != expectedPackageSha256) {
        return Utils::ResultError(
            "API-038 evidence is not the exact production-trusted package");
    }
    if (artifact.configurationId != 3701 || artifact.formatVersion != 2
        || evidence.semanticMappingProof().formatVersion != 2
        || artifact.bindings.size() != 56 || artifact.devices.size() != 3
        || artifact.actions.size() != 8 || evidence.cyclePeriodNs() != 125000) {
        return Utils::ResultError(
            "API-038 signed configuration, semantic mapping, or timing differs");
    }
    if (!evidence.actionDefinitions()
        || evidence.actionDefinitions()->definitionCount != 5
        || evidence.actionDefinitions()->actionCount != 8
        || !evidence.permitsWritableActions()) {
        return Utils::ResultError(
            "API-038 signed semantic action definitions are incomplete");
    }

    const QStringList xb6Actions{
        QStringLiteral("embedlabs:project:action:xb6:set-outputs"),
        QStringLiteral("embedlabs:project:action:xb6:clear-outputs"),
    };
    const QStringList svActions{
        QStringLiteral("embedlabs:project:action:axis0:prepare-csv"),
        QStringLiteral("embedlabs:project:action:axis0:set-csv-velocity"),
        QStringLiteral("embedlabs:project:action:axis0:stop-csv"),
        QStringLiteral("embedlabs:project:action:axis1:prepare-csv"),
        QStringLiteral("embedlabs:project:action:axis1:set-csv-velocity"),
        QStringLiteral("embedlabs:project:action:axis1:stop-csv"),
    };
    QSet<QString> expectedActions;
    for (const QString &actionId : xb6Actions)
        expectedActions.insert(actionId);
    for (const QString &actionId : svActions)
        expectedActions.insert(actionId);
    QSet<QString> actualActions;
    for (const VerifiedSemanticAction &action : artifact.actions)
        actualActions.insert(action.actionBindingId);
    if (actualActions != expectedActions) {
        return Utils::ResultError(
            "API-038 signed semantic action instance set differs");
    }

    for (const QString &actionId : xb6Actions) {
        const VerifiedSemanticAction *action = artifact.findAction(actionId);
        if (!action || !action->enabled
            || action->qualification
                   != VerifiedSemanticActionQualification::Qualified
            || action->disabledReason || action->dcRequired
            || !evidence.invocableAction(actionId, false)) {
            return Utils::ResultError(
                "API-038 XB6 set/clear action is not signed and qualified");
        }
    }
    for (const QString &actionId : svActions) {
        const VerifiedSemanticAction *action = artifact.findAction(actionId);
        if (!action || action->enabled
            || action->qualification
                   != VerifiedSemanticActionQualification::Unqualified
            || action->disabledReason
                   != QStringLiteral(
                       "reference_unit_to_rpm_conversion_not_bound")
            || evidence.invocableAction(actionId, false)
            || evidence.invocableAction(actionId, true)) {
            return Utils::ResultError(
                "API-038 SV630N motion qualification boundary differs");
        }
    }
    return Utils::ResultOk;
}

static Utils::Result<VerifiedRuntimePackageEvidence> importApi038LocalEvidence(
    QByteArrayView packageBytes,
    QByteArrayView projectBytes,
    const QString &packageStoreRoot,
    const QString &productionTrustDirectory,
    const QString &projectStoreRoot)
{
    const QByteArray packageSha256
        = QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256);
    if (packageSha256 != QByteArray::fromHex(api038PackageSha256)) {
        return Utils::ResultError(
            "API-038 package SHA-256 differs from the fixed cfg3701 fixture");
    }

    const RuntimePackageEvidenceRepository repository{
        packageStoreRoot,
        productionTrustDirectory,
        projectStoreRoot,
    };
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = repository.import(packageBytes, projectBytes);
    if (!evidence) {
        return Utils::ResultError(
            QStringLiteral("API-038 repository verification failed: %1")
                .arg(evidence.error()));
    }
    const Utils::Result<> validation = validateApi038LocalEvidence(*evidence);
    if (!validation)
        return Utils::ResultError(validation.error());
    return evidence;
}

static QStringList api038HardwareAdmissionBlockers(
    const VerifiedRuntimePackageEvidence &evidence,
    const QList<Data::DeviceAdapterManifest> &publishedAdapters)
{
    QStringList blockers;
    QSet<QString> checkedAdapterIds;
    for (const SemanticBindingTopologyInstance &topology :
         evidence.semanticBindingArtifact().topologyInstances) {
        if (checkedAdapterIds.contains(topology.adapterId))
            continue;
        checkedAdapterIds.insert(topology.adapterId);

        QList<const Data::DeviceAdapterManifest *> matches;
        for (const Data::DeviceAdapterManifest &manifest : publishedAdapters) {
            if (manifest.controllerAdapterTarget.adapterId == topology.adapterId)
                matches.append(&manifest);
        }
        if (matches.size() != 1) {
            blockers.append(
                QStringLiteral("published adapter %1 is missing or ambiguous")
                    .arg(topology.adapterId));
            continue;
        }

        const Data::DeviceAdapterManifest &manifest = *matches.constFirst();
        const Data::DeviceAdapterControllerTarget &target
            = manifest.controllerAdapterTarget;
        if (!manifest.signatureVerified || !manifest.realHardwareAllowed
            || manifest.qualification
                   != Data::DeviceAdapterQualification::Qualified) {
            blockers.append(
                QStringLiteral("published adapter %1 is not hardware-qualified")
                    .arg(topology.adapterId));
        }
        if (target.adapterVersion != topology.adapterVersion
            || target.adapterSha256 != topology.adapterSha256
            || target.esiSha256 != topology.esiSha256) {
            blockers.append(
                QStringLiteral(
                    "published adapter %1 target %2 does not match signed cfg3701 target %3")
                    .arg(
                        topology.adapterId,
                        target.adapterVersion,
                        topology.adapterVersion));
            continue;
        }
        const qsizetype profileMatches = std::count_if(
            manifest.processDataProfiles.cbegin(),
            manifest.processDataProfiles.cend(),
            [&topology](const Data::ProcessDataProfile &profile) {
                return profile.signedPdoProfileId == topology.pdoProfile
                       && profile.signedDcProfileId
                              == topology.dcProfile.value_or(QString());
            });
        if (profileMatches != 1) {
            blockers.append(
                QStringLiteral("published adapter %1 lacks the exact signed PDO/DC profile")
                    .arg(topology.adapterId));
        }
    }

    // The fixed API-038 package predates API-042. No environment value or
    // package metadata can substitute for a compiler-provider-owned activation
    // proof validated against the current project capture.
    blockers.append(
        QStringLiteral(
            "API-042 compiler activation proof and current project capture are absent"));
    return blockers;
}

constexpr auto api051HardwareEnableVariable = "QTC_ETHER_CAT_API051_HARDWARE";
constexpr auto api051HardwareConfirmationVariable = "QTC_ETHER_CAT_API051_CONFIRM";
constexpr auto api051PackagePathVariable = "QTC_ETHER_CAT_API051_PACKAGE";
constexpr auto api051ProjectPathVariable = "QTC_ETHER_CAT_API051_PROJECT";
constexpr auto api051HostVariable = "QTC_ETHER_CAT_PRODUCT_API_HOST";
constexpr auto api051HardwareConfirmation
    = "API051_CFG3702_1E7F2E41328F8B7EE6E8647A7F1EC468_XB6_DO16_DC";
constexpr auto api051PackageSha256
    = "1e7f2e41328f8b7ee6e8647a7f1ec468ac38633186a5a72c589f815c7db8b35b";
constexpr auto api051ProjectSha256
    = "4667dca2bb7e46e3db478ec6baf20711545e3b4bb829ba5318393a3c74be1c1b";
constexpr auto api051MappingSha256
    = "38c9ee73cc46ee98af09dfc254389bb0aff475726afae41adb96d2edfb863c5a";
constexpr auto api051SigningKeyId
    = "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6";
constexpr quint64 api051CatalogRevision = 0x1a0b4e3588236c68ULL;
constexpr quint64 api051TopologyIdentity = 0x697eaf5137ad0da5ULL;
constexpr quint64 api051ConfigurationId = 3702;
constexpr quint64 api051PackageGeneration = 18;
constexpr quint32 api051CyclePeriodNs = 125000;
constexpr quint32 api051ExpectedWorkingCounter = 11;
constexpr quint32 api051OutputTtlCycles = 1000;

enum class Api051HardwareAuthorization {
    Disabled,
    Invalid,
    Authorized,
};

static Api051HardwareAuthorization api051HardwareAuthorization(
    QByteArrayView enabled, QByteArrayView confirmation)
{
    if (enabled != QByteArrayView("1"))
        return Api051HardwareAuthorization::Disabled;
    if (confirmation != QByteArrayView(api051HardwareConfirmation))
        return Api051HardwareAuthorization::Invalid;
    return Api051HardwareAuthorization::Authorized;
}

static QString api051DefaultHandoffPath(QStringView relativePath)
{
    const QDir sourceDirectory(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    return QDir(sourceDirectory.absoluteFilePath(QStringLiteral("../../..")))
        .absoluteFilePath(
            QStringLiteral("build/vendor_api_fixed_handoff/api051/") + relativePath.toString());
}

static QString api051InputPath(const char *variable, QStringView defaultRelativePath)
{
    const QString configured = qEnvironmentVariable(variable).trimmed();
    return configured.isEmpty() ? api051DefaultHandoffPath(defaultRelativePath)
                                : QFileInfo(configured).absoluteFilePath();
}

static Utils::Result<> validateApi051Manifest(QByteArrayView packageBytes)
{
    const Utils::Result<EcpkgContainer> container = parseCanonicalEcpkgContainer(packageBytes);
    if (!container)
        return Utils::ResultError(container.error());
    const Utils::Result<StrictJson> manifest
        = parseCanonicalJson(container->manifestJson, container->manifestJson.size());
    if (!manifest || !manifest->is_object())
        return Utils::ResultError("API-051 signed manifest is not canonical");

    const auto configuration = manifest->find("configuration");
    if (configuration == manifest->end() || !configuration->is_object())
        return Utils::ResultError("API-051 signed configuration summary is missing");
    const auto exactUnsigned = [&configuration](const char *field, quint64 expected) {
        const auto value = configuration->find(field);
        return value != configuration->end() && value->is_number_unsigned()
               && value->get<quint64>() == expected;
    };
    const auto exactString = [&configuration](const char *field, const char *expected) {
        const auto value = configuration->find(field);
        return value != configuration->end() && value->is_string()
               && value->get<std::string>() == expected;
    };
    if (!exactUnsigned("configuration_id", api051ConfigurationId)
        || !exactUnsigned("configured_cycle_ns", api051CyclePeriodNs)
        || !exactUnsigned("expected_wkc", api051ExpectedWorkingCounter)
        || !exactUnsigned("frame_count", 1) || !exactUnsigned("dc_record_count", 2)
        || !exactString("requested_timing_mode", "dc")
        || !exactString("effective_timing_mode", "dc")) {
        return Utils::ResultError(
            "API-051 signed timing mode, cycle, frame plan, or expected WKC differs");
    }
    return Utils::ResultOk;
}

static const VerifiedSemanticAction *api051Action(
    const VerifiedRuntimePackageEvidence &evidence, QStringView actionBindingId)
{
    return evidence.semanticBindingArtifact().findAction(actionBindingId);
}

static Utils::Result<> validateApi051LocalEvidence(
    const VerifiedRuntimePackageEvidence &evidence,
    QByteArrayView packageBytes,
    QByteArrayView projectBytes)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    const Data::RuntimeSemanticMappingProof &proof = evidence.semanticMappingProof();
    if (!evidence.isValid() || artifact.trust != EcpkgTrustClass::Production
        || artifact.packageSha256 != QByteArray::fromHex(api051PackageSha256)
        || QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256)
               != QByteArray::fromHex(api051PackageSha256)
        || QCryptographicHash::hash(projectBytes, QCryptographicHash::Sha256)
               != QByteArray::fromHex(api051ProjectSha256)) {
        return Utils::ResultError(
            "API-051 evidence is not the exact production-trusted cfg3702 package and project");
    }
    if (artifact.configurationId != api051ConfigurationId || artifact.formatVersion != 2
        || artifact.artifactSha256 != QByteArray::fromHex(api051MappingSha256)
        || artifact.catalogRevision != api051CatalogRevision
        || artifact.topologyIdentity != api051TopologyIdentity || artifact.bindings.size() != 56
        || artifact.devices.size() != 3 || artifact.actions.size() != 8
        || evidence.cyclePeriodNs() != api051CyclePeriodNs || proof.formatVersion != 2
        || proof.bindingCount != 56
        || proof.mappingSha256 != QByteArray::fromHex(api051MappingSha256)
        || proof.signingKeyIdSha256 != QByteArray::fromHex(api051SigningKeyId)
        || proof.trust != Data::RuntimeSemanticMappingTrust::Production || !proof.packageSigned
        || !proof.signatureVerified || !proof.semanticBindingVerified) {
        return Utils::ResultError(
            "API-051 signed semantic identity, trust, catalog, topology, or timing differs");
    }
    if (!evidence.actionDefinitions() || evidence.actionDefinitions()->definitionCount != 5
        || evidence.actionDefinitions()->actionCount != 8 || !evidence.permitsWritableActions()) {
        return Utils::ResultError("API-051 signed semantic action definitions are incomplete");
    }

    struct TopologyExpectation
    {
        quint32 position;
        quint16 stationAddress;
        quint32 vendorId;
        quint32 productCode;
        quint32 revision;
        QString adapterId;
        QString adapterVersion;
    };
    const std::array<TopologyExpectation, 3> expectedTopology{
        TopologyExpectation{
            0,
            0x1001,
            0x00884443,
            0x000000b6,
            0x00000001,
            QStringLiteral("solidot.xb6_ec0002_rev1_do16"),
            QStringLiteral("1.3.0")},
        TopologyExpectation{
            1,
            0x1002,
            0x00100000,
            0x000c0112,
            0x00010000,
            QStringLiteral("inovance.sv630n_1axis_rev00010000_csp"),
            QStringLiteral("1.5.0")},
        TopologyExpectation{
            2,
            0x1003,
            0x00100000,
            0x000c0112,
            0x00010000,
            QStringLiteral("inovance.sv630n_1axis_rev00010000_csp"),
            QStringLiteral("1.5.0")},
    };
    if (artifact.topologyInstances.size() != qsizetype(expectedTopology.size()))
        return Utils::ResultError("API-051 signed topology does not contain three devices");
    for (const TopologyExpectation &expected : expectedTopology) {
        const auto topology = std::find_if(
            artifact.topologyInstances.cbegin(),
            artifact.topologyInstances.cend(),
            [&expected](const SemanticBindingTopologyInstance &candidate) {
                return candidate.position == expected.position;
            });
        if (topology == artifact.topologyInstances.cend()
            || topology->stationAddress != expected.stationAddress
            || topology->vendorId != expected.vendorId
            || topology->productCode != expected.productCode
            || topology->revision != expected.revision || topology->adapterId != expected.adapterId
            || topology->adapterVersion != expected.adapterVersion
            || (expected.position == 0
                    ? (topology->pdoProfile != QStringLiteral("do16")
                       || topology->dcProfile.has_value())
                    : (topology->pdoProfile != QStringLiteral("csp_1704_1b04")
                       || topology->dcProfile
                              != std::optional<QString>(QStringLiteral("sync0_125us"))))) {
            return Utils::ResultError(
                "API-051 signed slave identity, PDO profile, or DC profile differs");
        }
    }

    const QStringList xb6Actions{
        QStringLiteral("embedlabs:project:action:xb6:set-outputs"),
        QStringLiteral("embedlabs:project:action:xb6:clear-outputs"),
    };
    const QStringList svActions{
        QStringLiteral("embedlabs:project:action:axis0:prepare-csv"),
        QStringLiteral("embedlabs:project:action:axis0:set-csv-velocity"),
        QStringLiteral("embedlabs:project:action:axis0:stop-csv"),
        QStringLiteral("embedlabs:project:action:axis1:prepare-csv"),
        QStringLiteral("embedlabs:project:action:axis1:set-csv-velocity"),
        QStringLiteral("embedlabs:project:action:axis1:stop-csv"),
    };
    quint32 xb6GroupId = 0;
    for (const QString &actionId : xb6Actions) {
        const VerifiedSemanticAction *action = api051Action(evidence, actionId);
        if (!action || !action->enabled
            || action->qualification != VerifiedSemanticActionQualification::Qualified
            || action->disabledReason || action->requiredBindings.size() != 16
            || action->consistencyGroups.size() != 1
            || action->consistencyGroups.constFirst().recoveryPolicy
                   != EcfgOutputRecoveryPolicy::HoldSafe
            || action->consistencyGroups.constFirst().maximumTtlCycles != api051OutputTtlCycles
            || evidence.invocableAction(actionId, false) != action) {
            return Utils::ResultError(
                "API-051 XB6 set/clear action is not signed, complete, and qualified");
        }
        const quint32 groupId = action->consistencyGroups.constFirst().consistencyGroupId;
        if (!groupId || (xb6GroupId && xb6GroupId != groupId)) {
            return Utils::ResultError("API-051 XB6 action group identity differs");
        }
        xb6GroupId = groupId;
        QSet<QString> bindingIds;
        for (const VerifiedSemanticActionBindingReference &binding : action->requiredBindings) {
            if (binding.direction != EcfgResourceDirection::Output
                || binding.access != EcfgResourceAccess::ReadWrite
                || binding.primitive != EcfgResourcePrimitive::Bool || binding.bitWidth != 1
                || binding.consistencyGroupId != xb6GroupId
                || bindingIds.contains(binding.semanticBindingId)) {
                return Utils::ResultError(
                    "API-051 XB6 action does not cover one exact 16-bit Boolean output group");
            }
            bindingIds.insert(binding.semanticBindingId);
        }
    }
    const auto policy = std::find_if(
        evidence.outputPolicies().cbegin(),
        evidence.outputPolicies().cend(),
        [xb6GroupId](const EcfgOutputGroupPolicy &candidate) {
            return candidate.consistencyGroupId == xb6GroupId;
        });
    if (policy == evidence.outputPolicies().cend() || policy->resourceCount != 16
        || policy->resourceIds.size() != 16
        || policy->recoveryPolicy != EcfgOutputRecoveryPolicy::HoldSafe
        || policy->maximumTtlCycles != api051OutputTtlCycles
        || policy->groupResourceRecordsSha256.size() != 32) {
        return Utils::ResultError("API-051 XB6 signed output policy differs");
    }
    for (const QString &actionId : svActions) {
        const VerifiedSemanticAction *action = api051Action(evidence, actionId);
        if (!action || action->enabled
            || action->qualification != VerifiedSemanticActionQualification::Unqualified
            || action->disabledReason != QStringLiteral("reference_unit_to_rpm_conversion_not_bound")
            || evidence.invocableAction(actionId, false)
            || evidence.invocableAction(actionId, true)) {
            return Utils::ResultError("API-051 SV630N motion qualification boundary differs");
        }
    }

    return validateApi051Manifest(packageBytes);
}

static Utils::Result<VerifiedRuntimePackageEvidence> importApi051LocalEvidence(
    QByteArrayView packageBytes,
    QByteArrayView projectBytes,
    const QString &packageStoreRoot,
    const QString &productionTrustDirectory,
    const QString &projectStoreRoot)
{
    if (QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256)
            != QByteArray::fromHex(api051PackageSha256)
        || QCryptographicHash::hash(projectBytes, QCryptographicHash::Sha256)
               != QByteArray::fromHex(api051ProjectSha256)) {
        return Utils::ResultError(
            "API-051 package or project SHA-256 differs from the fixed cfg3702 evidence");
    }
    const RuntimePackageEvidenceRepository repository{
        packageStoreRoot,
        productionTrustDirectory,
        projectStoreRoot,
    };
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = repository.import(packageBytes, projectBytes);
    if (!evidence) {
        return Utils::ResultError(
            QStringLiteral("API-051 repository verification failed: %1").arg(evidence.error()));
    }
    const Utils::Result<> validation
        = validateApi051LocalEvidence(*evidence, packageBytes, projectBytes);
    if (!validation)
        return Utils::ResultError(validation.error());
    return evidence;
}

static QStringList api051AdapterAdmissionBlockers(
    const VerifiedRuntimePackageEvidence &evidence,
    const QList<Data::DeviceAdapterManifest> &publishedAdapters)
{
    QStringList blockers;
    QSet<QString> checkedAdapterIds;
    for (const SemanticBindingTopologyInstance &topology :
         evidence.semanticBindingArtifact().topologyInstances) {
        if (checkedAdapterIds.contains(topology.adapterId))
            continue;
        checkedAdapterIds.insert(topology.adapterId);

        QList<const Data::DeviceAdapterManifest *> matches;
        for (const Data::DeviceAdapterManifest &manifest : publishedAdapters) {
            if (manifest.controllerAdapterTarget.adapterId == topology.adapterId)
                matches.append(&manifest);
        }
        if (matches.size() != 1) {
            blockers.append(QStringLiteral("published adapter %1 is missing or ambiguous")
                                .arg(topology.adapterId));
            continue;
        }
        const Data::DeviceAdapterManifest &manifest = *matches.constFirst();
        const Data::DeviceAdapterControllerTarget &target = manifest.controllerAdapterTarget;
        if (!manifest.signatureVerified || !manifest.realHardwareAllowed) {
            blockers.append(
                QStringLiteral(
                    "published adapter %1 lacks independently verified real-hardware permission")
                    .arg(topology.adapterId));
        }
        if (manifest.contractVersion != Data::DeviceAdapterContractVersion::V3
            || manifest.qualification != Data::DeviceAdapterQualification::Qualified
            || target.adapterVersion != topology.adapterVersion
            || target.adapterSha256 != topology.adapterSha256
            || target.esiSha256 != topology.esiSha256) {
            blockers.append(QStringLiteral("published adapter %1 differs from signed cfg3702")
                                .arg(topology.adapterId));
            continue;
        }
        const qsizetype profileMatches = std::count_if(
            manifest.processDataProfiles.cbegin(),
            manifest.processDataProfiles.cend(),
            [&topology](const Data::ProcessDataProfile &profile) {
                return profile.signedPdoProfileId == topology.pdoProfile
                       && profile.signedDcProfileId == topology.dcProfile.value_or(QString());
            });
        if (profileMatches != 1) {
            blockers.append(
                QStringLiteral("published adapter %1 lacks the exact signed PDO/DC profile")
                    .arg(topology.adapterId));
        }
    }
    return blockers;
}

static Utils::Result<QList<Data::DeviceAdapterManifest>> api038HistoricalTestAdapters(
    const VerifiedRuntimePackageEvidence &evidence)
{
    const Utils::Result<QList<Data::DeviceAdapterManifest>> published
        = publishedApi038V3ActionAdapters();
    if (!published)
        return Utils::ResultError(published.error());

    QList<Data::DeviceAdapterManifest> result = *published;
    const auto &topologies = evidence.semanticBindingArtifact().topologyInstances;
    for (Data::DeviceAdapterManifest &manifest : result) {
        const auto topology = std::find_if(
            topologies.cbegin(),
            topologies.cend(),
            [&manifest](const SemanticBindingTopologyInstance &candidate) {
                return candidate.adapterId
                       == manifest.controllerAdapterTarget.adapterId;
            });
        if (topology == topologies.cend()
            || !std::all_of(
                topology,
                topologies.cend(),
                [&manifest, &topology](
                    const SemanticBindingTopologyInstance &candidate) {
                    return candidate.adapterId
                               != manifest.controllerAdapterTarget.adapterId
                           || (candidate.adapterVersion == topology->adapterVersion
                               && candidate.adapterSha256 == topology->adapterSha256
                               && candidate.esiSha256 == topology->esiSha256
                               && candidate.pdoProfile == topology->pdoProfile
                               && candidate.dcProfile == topology->dcProfile);
                })) {
            return Utils::ResultError(
                "The activation fixture controller target is ambiguous");
        }
        const auto profile = std::find_if(
            manifest.processDataProfiles.cbegin(),
            manifest.processDataProfiles.cend(),
            [&topology](const Data::ProcessDataProfile &candidate) {
                return candidate.signedPdoProfileId == topology->pdoProfile
                       && candidate.signedDcProfileId
                              == topology->dcProfile.value_or(QString());
            });
        if (profile == manifest.processDataProfiles.cend()) {
            return Utils::ResultError(
                "The activation fixture signed PDO/DC profile is unavailable");
        }

        manifest.id.value.prepend(QStringLiteral("org.embedlabs.tests.api038."));
        manifest.version = QStringLiteral("1.0.0");
        manifest.controllerAdapterTarget = {
            topology->adapterId,
            topology->adapterVersion,
            topology->adapterSha256,
            topology->esiSha256,
        };
        QByteArray contentIdentity = manifest.id.value.toUtf8();
        contentIdentity.append(topology->adapterSha256);
        manifest.contentSha256 = QCryptographicHash::hash(
            contentIdentity, QCryptographicHash::Sha256);
        // This is a test-only projection of the already verified API-038
        // production evidence. Production providers must independently set
        // both authorization flags after verifying their adapter package.
        manifest.signatureVerified = true;
        manifest.realHardwareAllowed = true;
    }
    return result;
}

// Historical cfg3701 tests derive their upper-layer contract from the real
// provider parser, then bind only the test copy to that package's exact lower
// adapter identity. The evidence-derived mirror remains a diagnostic fallback.
static QList<Data::DeviceAdapterManifest> factoryApi038MutationAdapters(
    const VerifiedRuntimePackageEvidence &evidence)
{
    const Utils::Result<QList<Data::DeviceAdapterManifest>> historical
        = api038HistoricalTestAdapters(evidence);
    if (historical)
        return *historical;

    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    QHash<QString, Data::DeviceAdapterManifest> manifests;

    for (const SemanticBindingTopologyInstance &topology : artifact.topologyInstances) {
        const std::optional<Api038UpperAdapterIdentity> upper = api038UpperAdapterIdentity(
            topology.adapterId);
        if (!upper)
            return {};
        if (manifests.contains(topology.adapterId))
            continue;

        Data::DeviceAdapterManifest manifest;
        manifest.contractVersion = Data::DeviceAdapterContractVersion::V3;
        manifest.id = {upper->id};
        manifest.version = upper->version;
        manifest.displayName = upper->id;
        manifest.qualification = Data::DeviceAdapterQualification::Qualified;
        manifest.match = {
            topology.vendorId,
            topology.productCode,
            topology.revision.value_or(0),
            topology.revision.value_or(0),
            topology.esiSha256,
        };
        manifest.provenance.sourceSha256 = topology.esiSha256;
        manifest.controllerAdapterTarget = {
            topology.adapterId,
            topology.adapterVersion,
            topology.adapterSha256,
            topology.esiSha256,
        };
        manifest.contentSha256 = upper->contentSha256;
        manifest.signatureVerified = true;
        manifest.realHardwareAllowed = true;

        QHash<QString, Data::SemanticSignalDefinition> signalDefinitions;
        for (const VerifiedSemanticBinding &binding : artifact.bindings) {
            if (binding.adapterId != topology.adapterId
                || binding.adapterVersion != topology.adapterVersion
                || binding.adapterSha256 != topology.adapterSha256
                || binding.esiSha256 != topology.esiSha256
                || signalDefinitions.contains(binding.semanticSignalDefinitionId)) {
                continue;
            }
            const std::optional<Data::EtherCATDataType> dataType = factoryDataType(
                binding.primitive);
            const std::optional<Data::EngineeringConstraint> constraint = factoryConstraint(
                binding.primitive, binding.bitWidth);
            if (!dataType || !constraint || binding.scaleNumerator != 1
                || binding.scaleDenominator != 1 || binding.scaleOffset != 0) {
                return {};
            }

            Data::SemanticSignalDefinition signal;
            signal.id = {binding.semanticSignalDefinitionId};
            signal.displayName = binding.semanticSignalDefinitionId;
            signal.direction = binding.direction == EcfgResourceDirection::Output
                                   ? Data::SemanticSignalDirection::Output
                                   : Data::SemanticSignalDirection::Input;
            signal.access = binding.access == EcfgResourceAccess::ReadWrite
                                ? Data::SemanticSignalAccess::ReadWrite
                                : Data::SemanticSignalAccess::ReadOnly;
            signal.exposure = Data::SemanticSignalExposure::ActionOnly;
            Data::DeviceSignalBinding deviceBinding;
            deviceBinding.kind = Data::DeviceSignalBindingKind::ProcessDataObject;
            deviceBinding.pdoDirection = binding.direction == EcfgResourceDirection::Output
                                             ? Data::PdoDirection::Rx
                                             : Data::PdoDirection::Tx;
            deviceBinding.pdoIndex = deviceBinding.pdoDirection == Data::PdoDirection::Rx
                                         ? upper->rxPdos.constFirst()
                                         : upper->txPdos.constFirst();
            deviceBinding.objectIndex = 1;
            deviceBinding.physicalType = *dataType;
            deviceBinding.bitWidth = binding.bitWidth;
            signal.bindings.append(deviceBinding);

            Data::EngineeringTransform transform;
            transform.unit = binding.unit.value_or(QString());
            transform.constraint = *constraint;
            transform.rounding = Data::EngineeringRounding::RejectInexact;
            signal.engineeringTransform = transform;
            if (binding.safeValueDeclared && binding.safeValue) {
                signal.engineeringSafeValue = factoryEngineeringValue(
                    binding.primitive, *binding.safeValue);
                if (!signal.engineeringSafeValue)
                    return {};
            }
            signalDefinitions.insert(binding.semanticSignalDefinitionId, signal);
        }
        QStringList signalIds = signalDefinitions.keys();
        std::sort(signalIds.begin(), signalIds.end());
        for (const QString &signalId : std::as_const(signalIds))
            manifest.semanticSignals.append(signalDefinitions.value(signalId));

        Data::ProcessDataProfile profile;
        profile.id = upper->processDataProfileId;
        profile.signedPdoProfileId = topology.pdoProfile;
        profile.signedDcProfileId = topology.dcProfile.value_or(QString());
        profile.rxPdoIndices = upper->rxPdos;
        profile.txPdoIndices = upper->txPdos;
        for (const QString &signalId : std::as_const(signalIds))
            profile.requiredSignals.append({signalId});
        manifest.processDataProfiles.append(profile);
        manifests.insert(topology.adapterId, manifest);
    }

    QHash<QString, QSet<QString>> actionDefinitionsByAdapter;
    for (const VerifiedSemanticAction &action : artifact.actions) {
        auto manifest = manifests.find(action.adapterId);
        const VerifiedSemanticDevice *device = artifact.findDevice(action.projectDeviceId);
        const auto topology = device
                                  ? std::find_if(
                                        artifact.topologyInstances.cbegin(),
                                        artifact.topologyInstances.cend(),
                                        [device](const SemanticBindingTopologyInstance &candidate) {
                                            return candidate.position == device->position
                                                   && candidate.stationAddress
                                                          == device->stationAddress;
                                        })
                                  : artifact.topologyInstances.cend();
        const std::optional<Api038UpperAdapterIdentity> upper = api038UpperAdapterIdentity(
            action.adapterId);
        if (manifest == manifests.end() || !device
            || topology == artifact.topologyInstances.cend() || !upper) {
            return {};
        }
        if (actionDefinitionsByAdapter[action.adapterId].contains(action.actionDefinitionId))
            continue;

        Data::DeviceControlAction definition;
        definition.id = {action.actionDefinitionId};
        definition.displayName = action.actionDefinitionId;
        definition.enabled = action.enabled;
        definition.signedQualification
            = action.qualification == VerifiedSemanticActionQualification::Qualified
                  ? Data::DeviceControlActionQualification::Qualified
                  : Data::DeviceControlActionQualification::Unqualified;
        definition.disabledReason = action.disabledReason.value_or(QString());
        definition.requiresExclusiveControl = true;
        definition.requiresDc = action.dcRequired;
        definition.holdToRun = false;
        definition.expectedSignedDefinitionSha256 = action.actionDefinitionSha256;
        definition.signedPdoProfileIds = {topology->pdoProfile};
        definition.failureDisposition
            = Data::DeviceControlFailureDisposition::HoldOperationalFault;
        for (const VerifiedSemanticActionBindingReference &reference : action.requiredBindings)
            definition.requiredSignals.append({reference.semanticSignalDefinitionId});
        for (const VerifiedSemanticActionBindingReference &reference : action.optionalBindings)
            definition.optionalSignals.append({reference.semanticSignalDefinitionId});
        std::sort(
            definition.requiredSignals.begin(),
            definition.requiredSignals.end(),
            [](const Data::SemanticSignalId &left, const Data::SemanticSignalId &right) {
                return left.value < right.value;
            });
        std::sort(
            definition.optionalSignals.begin(),
            definition.optionalSignals.end(),
            [](const Data::SemanticSignalId &left, const Data::SemanticSignalId &right) {
                return left.value < right.value;
            });

        for (const VerifiedSemanticActionParameter &parameter : action.parameters) {
            const std::optional<Data::EtherCATDataType> dataType = factoryDataType(
                parameter.primitive);
            if (!dataType)
                return {};
            Data::DeviceControlActionParameter publicParameter;
            publicParameter.id = parameter.parameterId;
            publicParameter.displayName = parameter.parameterId;
            publicParameter.dataType = *dataType;
            publicParameter.valueMetadata.unit = parameter.unit.value_or(QString());
            publicParameter.required = true;
            Data::EngineeringConstraint constraint;
            constraint.minimum = Data::ExactRational{parameter.minimum, 1};
            constraint.maximum = Data::ExactRational{parameter.maximum, 1};
            constraint.step = Data::ExactRational{1, 1};
            constraint.stepOrigin = Data::ExactRational{0, 1};
            publicParameter.engineeringConstraint = constraint;
            const std::variant<qint64, quint64> zero
                = parameter.primitive == EcfgResourcePrimitive::Bool
                      || parameter.primitive == EcfgResourcePrimitive::U8
                      || parameter.primitive == EcfgResourcePrimitive::U16
                      || parameter.primitive == EcfgResourcePrimitive::U32
                      || parameter.primitive == EcfgResourcePrimitive::U64
                  ? std::variant<qint64, quint64>{quint64(0)}
                  : std::variant<qint64, quint64>{qint64(0)};
            publicParameter.engineeringDefaultValue = factoryEngineeringValue(
                parameter.primitive, zero);
            publicParameter.hasDefaultValue
                = publicParameter.engineeringDefaultValue.has_value();
            definition.parameters.append(publicParameter);
        }
        std::sort(
            definition.parameters.begin(),
            definition.parameters.end(),
            [](const Data::DeviceControlActionParameter &left,
               const Data::DeviceControlActionParameter &right) {
                return left.id < right.id;
            });

        QHash<quint32, QString> localGroupByNumericGroup;
        for (const VerifiedSemanticActionGroup &signedGroup : action.consistencyGroups) {
            Data::DeviceControlConsistencyGroup group;
            group.id = upper->localGroupId;
            group.recovery = signedGroup.recoveryPolicy == EcfgOutputRecoveryPolicy::HoldSafe
                                 ? Data::DeviceControlGroupRecovery::HoldSafe
                                 : Data::DeviceControlGroupRecovery::ReturnTask;
            group.maximumTtlCycles = signedGroup.maximumTtlCycles;
            for (const VerifiedSemanticActionBindingReference &reference :
                 action.requiredBindings) {
                if (reference.consistencyGroupId == signedGroup.consistencyGroupId
                    && reference.direction == EcfgResourceDirection::Output
                    && reference.access == EcfgResourceAccess::ReadWrite) {
                    group.members.append({reference.semanticSignalDefinitionId});
                }
            }
            std::sort(
                group.members.begin(),
                group.members.end(),
                [](const Data::SemanticSignalId &left, const Data::SemanticSignalId &right) {
                    return left.value < right.value;
                });
            if (group.members.isEmpty())
                return {};
            localGroupByNumericGroup.insert(signedGroup.consistencyGroupId, group.id);
            definition.consistencyGroups.append(group);
        }

        for (const VerifiedSemanticActionStep &signedStep : action.steps) {
            Data::DeviceControlStep step;
            step.timeoutCycles = signedStep.timeoutCycles;
            if (signedStep.kind == VerifiedSemanticActionStepKind::WriteGroup) {
                step.kind = Data::DeviceControlStepKind::WriteGroup;
                step.consistencyGroupId = localGroupByNumericGroup.value(
                    signedStep.consistencyGroupId);
                for (const VerifiedSemanticActionAssignment &signedAssignment :
                     signedStep.assignments) {
                    const auto reference = std::find_if(
                        action.requiredBindings.cbegin(),
                        action.requiredBindings.cend(),
                        [&signedAssignment](
                            const VerifiedSemanticActionBindingReference &candidate) {
                            return candidate.semanticBindingId
                                   == signedAssignment.semanticBindingId;
                        });
                    if (reference == action.requiredBindings.cend())
                        return {};
                    Data::DeviceControlGroupAssignment assignment;
                    assignment.signalId = {reference->semanticSignalDefinitionId};
                    if (signedAssignment.parameterId) {
                        assignment.value.source = Data::DeviceControlValueSource::Parameter;
                        assignment.value.parameterId = *signedAssignment.parameterId;
                    } else if (signedAssignment.constantValue) {
                        assignment.value.source = Data::DeviceControlValueSource::Literal;
                        assignment.value.engineeringLiteralValue = factoryEngineeringValue(
                            reference->primitive, *signedAssignment.constantValue);
                        if (!assignment.value.engineeringLiteralValue)
                            return {};
                    } else {
                        return {};
                    }
                    step.assignments.append(assignment);
                }
                std::sort(
                    step.assignments.begin(),
                    step.assignments.end(),
                    [](const Data::DeviceControlGroupAssignment &left,
                       const Data::DeviceControlGroupAssignment &right) {
                        return left.signalId.value < right.signalId.value;
                    });
            } else {
                const auto reference = std::find_if(
                    action.requiredBindings.cbegin(),
                    action.requiredBindings.cend(),
                    [&signedStep](const VerifiedSemanticActionBindingReference &candidate) {
                        return candidate.semanticBindingId == signedStep.semanticBindingId;
                    });
                if (reference == action.requiredBindings.cend())
                    return {};
                step.signalId = {reference->semanticSignalDefinitionId};
                step.value.source = Data::DeviceControlValueSource::Literal;
                if (signedStep.kind == VerifiedSemanticActionStepKind::WaitMasked) {
                    step.kind = Data::DeviceControlStepKind::WaitMaskedEquals;
                    step.value.engineeringLiteralValue
                        = Data::EngineeringValue::fromUnsignedInteger(signedStep.value);
                    step.mask.source = Data::DeviceControlValueSource::Literal;
                    step.mask.engineeringLiteralValue
                        = Data::EngineeringValue::fromUnsignedInteger(signedStep.mask);
                } else {
                    step.kind = Data::DeviceControlStepKind::WaitAbsoluteAtMost;
                    step.value.engineeringLiteralValue
                        = Data::EngineeringValue::fromSignedInteger(
                            qint64(signedStep.absoluteLimit));
                }
            }
            definition.steps.append(step);
        }
        manifest->controlActions.append(definition);
        actionDefinitionsByAdapter[action.adapterId].insert(action.actionDefinitionId);
    }

    QList<Data::DeviceAdapterManifest> result = manifests.values();
    for (Data::DeviceAdapterManifest &manifest : result) {
        std::sort(
            manifest.controlActions.begin(),
            manifest.controlActions.end(),
            [](const Data::DeviceControlAction &left, const Data::DeviceControlAction &right) {
                return left.id.value < right.id.value;
            });
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const Data::DeviceAdapterManifest &left, const Data::DeviceAdapterManifest &right) {
            return left.id.value < right.id.value;
        });
    return result;
}

static void configureApi038V3ManualProject(
    Data::ProjectSnapshot &project,
    const QList<Data::DeviceAdapterManifest> &adapterManifests)
{
    for (Data::OfflineSlaveConfiguration &slave : project.slaves) {
        const auto manifest = std::find_if(
            adapterManifests.cbegin(),
            adapterManifests.cend(),
            [&slave](const Data::DeviceAdapterManifest &candidate) {
                return candidate.controllerAdapterTarget.adapterId
                           == slave.adapterSelection.adapterId.value
                       && candidate.controllerAdapterTarget.adapterVersion
                              == slave.adapterSelection.adapterVersion
                       && candidate.controllerAdapterTarget.adapterSha256
                              == slave.adapterSelection.adapterContentSha256
                       && candidate.controllerAdapterTarget.esiSha256 == slave.esiSha256;
            });
        if (manifest == adapterManifests.cend())
            continue;
        slave.adapterSelection.adapterId = manifest->id;
        slave.adapterSelection.adapterVersion = manifest->version;
        slave.adapterSelection.adapterContentSha256 = manifest->contentSha256;
        slave.adapterSelection.processDataProfileId
            = manifest->processDataProfiles.constFirst().id;

        Data::ManualControlEnvelope envelope;
        for (const Data::DeviceControlAction &definition : manifest->controlActions) {
            Data::ManualActionEnvelope action;
            action.actionId = definition.id;
            action.enabled = definition.enabled;
            action.holdToRun = false;
            action.timing.commandTtlMs = 125;
            for (const Data::DeviceControlActionParameter &parameter : definition.parameters) {
                if (!parameter.engineeringConstraint)
                    continue;
                Data::ManualActionParameterEnvelope manualParameter;
                manualParameter.parameterId = parameter.id;
                manualParameter.allowedRange = *parameter.engineeringConstraint;
                action.parameters.append(std::move(manualParameter));
            }
            std::sort(
                action.parameters.begin(),
                action.parameters.end(),
                [](const Data::ManualActionParameterEnvelope &left,
                   const Data::ManualActionParameterEnvelope &right) {
                    return left.parameterId < right.parameterId;
                });
            envelope.actionEnvelopes.append(std::move(action));
        }
        envelope.enabled = std::any_of(
            envelope.actionEnvelopes.cbegin(),
            envelope.actionEnvelopes.cend(),
            [](const Data::ManualActionEnvelope &action) { return action.enabled; });
        std::sort(
            envelope.actionEnvelopes.begin(),
            envelope.actionEnvelopes.end(),
            [](const Data::ManualActionEnvelope &left,
               const Data::ManualActionEnvelope &right) {
                return left.actionId.value < right.actionId.value;
            });
        slave.manualControlEnvelope = envelope;
    }
}

static Data::OfflineSlaveConfiguration *factoryManualSlave(Data::ProjectSnapshot &project)
{
    const auto found = std::find_if(
        project.slaves.begin(),
        project.slaves.end(),
        [](const Data::OfflineSlaveConfiguration &slave) {
            return slave.manualControlEnvelope.enabled
                   && !slave.manualControlEnvelope.actionEnvelopes.isEmpty();
        });
    return found == project.slaves.end() ? nullptr : &*found;
}

static QList<Data::DeviceAdapterManifest> factoryCurrentIdeV2Adapters()
{
    Data::DeviceAdapterManifest xb6;
    xb6.id = {QStringLiteral("org.embedlabs.adapter.solidot.xb6-ec0002.rev1")};
    xb6.version = QStringLiteral("0.2.0");
    xb6.contentSha256 = QByteArray::fromHex(
        "ec6ea39eaa5f7a12832f9cfeab4c3b29f66ffa004abe83f070772123d33c44d4");
    xb6.qualification = Data::DeviceAdapterQualification::Candidate;

    Data::DeviceAdapterManifest sv630n;
    sv630n.id = {
        QStringLiteral("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"),
    };
    sv630n.version = QStringLiteral("0.2.0");
    sv630n.contentSha256 = QByteArray::fromHex(
        "7fc445d4372799cce8f0cdf68fa71cad9ca2a8ea47e0a0b0dffd5aff02a3fb64");
    sv630n.qualification = Data::DeviceAdapterQualification::Candidate;
    return {xb6, sv630n};
}

static Data::RuntimeResourceCatalog factoryCatalog(
    const Data::ProjectSnapshot &project, const VerifiedRuntimePackageEvidence &evidence)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    Data::RuntimeResourceCatalog catalog;
    catalog.scope = projectScope(project);
    catalog.sessionGeneration = 37;
    catalog.epoch.controllerBootId = 0x1122334455667788ULL;
    catalog.epoch.activePackageSlot = Data::ControllerSlot::B;
    catalog.epoch.activePackageGeneration = 12;
    catalog.epoch.configurationId = artifact.configurationId;
    catalog.epoch.topologyGeneration = 13;
    catalog.epoch.runtimeGeneration = 14;
    catalog.epoch.catalogRevision = artifact.catalogRevision;
    catalog.epoch.topologyIdentity = factoryOpaqueId(artifact.topologyIdentity);
    catalog.receivedAt = QDateTime::currentDateTimeUtc();
    catalog.resources.reserve(artifact.bindings.size());

    for (const VerifiedSemanticBinding &binding : artifact.bindings) {
        Data::RuntimeResourceDescriptor descriptor;
        descriptor.id.value = factoryOpaqueId(binding.resourceId);
        descriptor.componentInstanceId.value = factoryOpaqueId(binding.componentInstanceId);
        if (binding.parentInstanceId)
            descriptor.parentInstanceId.value = factoryOpaqueId(binding.parentInstanceId);
        descriptor.instanceOrdinal = binding.instanceOrdinal;
        descriptor.consistencyGroupId.value = factoryOpaqueId(binding.consistencyGroupId);
        descriptor.primitiveType = factoryPrimitive(binding.primitive);
        descriptor.valueTypeIdentity = factoryValueTypeIdentity(binding.primitive, binding.bitWidth);
        descriptor.bitWidth = binding.bitWidth;
        descriptor.direction = binding.direction == EcfgResourceDirection::Input
                                   ? Data::RuntimeResourceDirection::Input
                                   : Data::RuntimeResourceDirection::Output;
        descriptor.access = binding.access == EcfgResourceAccess::Read
                                ? Data::RuntimeResourceAccess::ReadOnly
                                : Data::RuntimeResourceAccess::ReadWrite;
        descriptor.processImageBitOffset = binding.processImageBitOffset;
        descriptor.processImageBitLength = binding.processImageBitLength;
        descriptor.qualityMask = binding.qualityMask;
        if (binding.unit)
            descriptor.unit = *binding.unit;
        if (binding.safeValueDeclared)
            descriptor.safeValue = factorySafeValue(binding);
        catalog.resources.append(std::move(descriptor));
    }
    return catalog;
}

static Data::RuntimeSemanticMappingAttestation factoryAttestation(
    const Data::RuntimeResourceCatalog &catalog, const VerifiedRuntimePackageEvidence &evidence)
{
    Data::RuntimeSemanticMappingAttestation attestation;
    attestation.scope = catalog.scope;
    attestation.sessionGeneration = catalog.sessionGeneration;
    attestation.epoch = catalog.epoch;
    attestation.proof = evidence.semanticMappingProof();
    attestation.receivedAt = QDateTime::currentDateTimeUtc();
    return attestation;
}

class RuntimeBootstrapFixture final
{
public:
    RuntimeBootstrapFixture()
        : temporary(systemTemporaryDirectoryTemplate(u"embed-labs-runtime-bootstrap"))
    {}

    Utils::Result<> initialize()
    {
        if (!temporary.isValid())
            return Utils::ResultError("The runtime bootstrap temporary directory is invalid");

        const QByteArray packageBytes = readTestData(
            "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
        const QByteArray projectBytes = readTestData("testdata/api038/project.json");
        const QString keyFileName = QStringLiteral(
            "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
        const QByteArray publicKey = readTestData(
            QStringLiteral("testdata/api038/") + keyFileName);
        const QString trustRoot = QDir(temporary.path()).filePath("trust");
        if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.size() != 32
            || !QDir().mkpath(trustRoot)
            || !writeFile(QDir(trustRoot).filePath(keyFileName), publicKey)) {
            return Utils::ResultError("The runtime bootstrap fixture is incomplete");
        }

        repository = std::make_shared<RuntimePackageEvidenceRepository>(
            QDir(temporary.path()).filePath("packages"),
            trustRoot,
            QDir(temporary.path()).filePath("projects"));
        const Utils::Result<VerifiedRuntimePackageEvidence> imported
            = repository->import(packageBytes, projectBytes);
        if (!imported)
            return Utils::ResultError(imported.error());
        evidence = std::make_shared<const VerifiedRuntimePackageEvidence>(*imported);
        project = factoryProject(*evidence);
        return Utils::ResultOk;
    }

    QTemporaryDir temporary;
    std::shared_ptr<RuntimePackageEvidenceRepository> repository;
    std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence;
    Data::ProjectSnapshot project;
};

static Utils::Id nextActivationFixtureProviderId()
{
    static quint64 sequence = 0;
    return Utils::Id::fromString(
        QStringLiteral("EtherCAT.SemanticRuntime.Tests.Activation.%1")
            .arg(++sequence));
}

class ActivationFixture final
{
public:
    ActivationFixture()
        : temporary(systemTemporaryDirectoryTemplate(u"embed-labs-runtime-activation"))
        , ownedProvider(std::make_unique<ActivationControllerProvider>(
              nextActivationFixtureProviderId()))
        , provider(*ownedProvider)
    {}

    Utils::Result<> initialize(
        ActivationControllerProvider::DeploymentMode deploymentMode
            = ActivationControllerProvider::DeploymentMode::Succeed,
        QString operationId = QStringLiteral("operation/runtime-activation/success"),
        bool prepareForStart = true)
    {
        if (!temporary.isValid())
            return Utils::ResultError("The activation temporary directory is invalid");

        packageBytes = readTestData(
            "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
        projectBytes = readTestData("testdata/api038/project.json");
        const QString keyFileName = QStringLiteral(
            "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
        const QByteArray publicKey = readTestData(
            QStringLiteral("testdata/api038/") + keyFileName);
        if (packageBytes.isEmpty() || projectBytes.isEmpty()
            || publicKey.size() != 32) {
            return Utils::ResultError("The API-038 activation fixture is incomplete");
        }

        const QString trustRoot = QDir(temporary.path()).filePath("trust");
        if (!QDir().mkpath(trustRoot)
            || !writeFile(QDir(trustRoot).filePath(keyFileName), publicKey)) {
            return Utils::ResultError("The activation trust store could not be prepared");
        }
        repository = std::make_shared<RuntimePackageEvidenceRepository>(
            QDir(temporary.path()).filePath("packages"),
            trustRoot,
            QDir(temporary.path()).filePath("projects"));
        const Utils::Result<VerifiedRuntimePackageEvidence> imported
            = repository->import(packageBytes, projectBytes);
        if (!imported)
            return Utils::ResultError(imported.error());
        evidence = std::make_shared<const VerifiedRuntimePackageEvidence>(*imported);

        project = factoryProject(*evidence);
        project.masterBindingArtifact = {};
        const VerifiedSemanticBindingArtifact &artifact
            = evidence->semanticBindingArtifact();
        for (Data::OfflineSlaveConfiguration &slave : project.slaves) {
            const auto topology = std::find_if(
                artifact.topologyInstances.cbegin(),
                artifact.topologyInstances.cend(),
                [&slave](const SemanticBindingTopologyInstance &candidate) {
                    return candidate.position == quint32(slave.position)
                           && candidate.stationAddress == slave.stationAddress;
                });
            if (topology == artifact.topologyInstances.cend())
                return Utils::ResultError("The signed activation topology is incomplete");
            slave.adapterSelection.processDataProfileId = topology->pdoProfile;
        }
        const Utils::Result<QList<Data::DeviceAdapterManifest>> activationAdapters
            = api038HistoricalTestAdapters(*evidence);
        if (!activationAdapters)
            return Utils::ResultError(activationAdapters.error());
        adapterManifests = *activationAdapters;
        configureApi038V3ManualProject(project, adapterManifests);
        project.masterConfiguration.timingMode
            = Data::MasterTimingMode::DistributedClocks;
        project.masterConfiguration.cyclePeriodNs
            = evidence->cyclePeriodNs();

        const Data::ControllerConnectionScope scope = projectScope(project);
        targetReference.artifactId
            = QStringLiteral("test/api038/runtime-activation-binding");
        targetReference.artifactSha256
            = evidence->semanticMappingProof().mappingSha256;
        targetReference.projectConfigurationSha256
            = evidence->projectConfigurationSha256();
        for (const VerifiedSemanticDevice &device : artifact.devices) {
            const auto slave = std::find_if(
                project.slaves.cbegin(),
                project.slaves.cend(),
                [&device](const Data::OfflineSlaveConfiguration &candidate) {
                    return candidate.position == int(device.position)
                           && candidate.stationAddress == device.stationAddress;
                });
            if (slave == project.slaves.cend())
                return Utils::ResultError("The activation project device is missing");
            targetReference.projectDeviceBindings.append(
                {slave->id, device.projectDeviceId});
        }

        documentRevision
            = Data::RuntimePackageActivationDocumentRevisionToken{
                QByteArray(32, '\x61')};
        originalBinding
            = Data::runtimePackageActivationBindingToken(
                project.masterBindingArtifact);
        targetBinding
            = Data::runtimePackageActivationBindingToken(targetReference);
        const auto digestHex = [](QByteArrayView bytes) {
            return QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                    .toHex());
        };
        QJsonArray companionDevices;
        for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
            const auto binding = std::find_if(
                targetReference.projectDeviceBindings.cbegin(),
                targetReference.projectDeviceBindings.cend(),
                [&slave](const Data::SemanticProjectDeviceBinding &candidate) {
                    return candidate.slaveId == slave.id;
                });
            if (binding == targetReference.projectDeviceBindings.cend())
                return Utils::ResultError("The activation companion device is missing");
            const auto signedDevice = std::find_if(
                artifact.devices.cbegin(),
                artifact.devices.cend(),
                [&slave](const VerifiedSemanticDevice &candidate) {
                    return candidate.position == slave.position
                           && candidate.stationAddress
                                  == slave.stationAddress;
                });
            if (signedDevice == artifact.devices.cend())
                return Utils::ResultError("The signed activation device is missing");
            const QString deviceDomain
                = QStringLiteral("activation-companion-device-%1")
                      .arg(slave.position);
            const auto deviceDigestHex
                = [&digestHex, &deviceDomain](QByteArrayView suffix) {
                      QByteArray input = deviceDomain.toUtf8();
                      input.append(suffix);
                      return digestHex(input);
                  };
            companionDevices.append(QJsonObject{
                {QStringLiteral("adapter_id"),
                 signedDevice->adapterId},
                {QStringLiteral("adapter_sha256"),
                 QString::fromLatin1(
                     signedDevice->adapterSha256.toHex())},
                {QStringLiteral("adapter_version"),
                 signedDevice->adapterVersion},
                {QStringLiteral("binding_identity_sha256"),
                 deviceDigestHex("-binding")},
                {QStringLiteral("dc_sha256"),
                 deviceDigestHex("-dc")},
                {QStringLiteral("esi_sha256"),
                 QString::fromLatin1(slave.esiSha256.toHex())},
                {QStringLiteral("identity_sha256"),
                 deviceDigestHex("-identity")},
                {QStringLiteral("manual_envelope_sha256"),
                 deviceDigestHex("-manual")},
                {QStringLiteral("module_assignments_sha256"),
                 deviceDigestHex("-modules")},
                {QStringLiteral("pdo_selection_sha256"),
                 deviceDigestHex("-pdo")},
                {QStringLiteral("position"), slave.position},
                {QStringLiteral("project_device_id"),
                 binding->projectDeviceId},
                {QStringLiteral("slave_node_id"), slave.id.toString()},
                {QStringLiteral("startup_sdo_sha256"),
                 deviceDigestHex("-sdo")},
                {QStringLiteral("station_address"),
                 int(slave.stationAddress)},
            });
        }
        effectiveProjectCompanion
            = QJsonDocument(QJsonObject{
                                {QStringLiteral("adapter_bundle_sha256"),
                                 digestHex("adapter-bundle")},
                                {QStringLiteral("compiled_project_sha256"),
                                 digestHex(projectBytes)},
                                {QStringLiteral("configuration_id"),
                                 qint64(artifact.configurationId)},
                                {QStringLiteral("devices"),
                                 companionDevices},
                                {QStringLiteral("document_revision"), 1},
                                {QStringLiteral("format"),
                                 QStringLiteral(
                                     "ethercat-effective-project-companion-v1")},
                                {QStringLiteral("format_version"), 1},
                                {QStringLiteral("intent_sha256"),
                                 digestHex("activation-intent")},
                                {QStringLiteral("master"),
                                 QJsonObject{
                                     {QStringLiteral("cycle_period_ns"),
                                      qint64(evidence->cyclePeriodNs())},
                                     {QStringLiteral("link_speed_mbps"), 100},
                                     {QStringLiteral("timing_mode"),
                                      QStringLiteral("dc")},
                                 }},
                                {QStringLiteral("master_node_id"),
                                 scope.masterId.toString()},
                                {QStringLiteral("project_id"),
                                 scope.projectId.toString()},
                                {QStringLiteral("target_profile_sha256"),
                                 digestHex("target-profile")},
                                {QStringLiteral("topology_evidence_sha256"),
                                 digestHex("topology-evidence")},
                            })
                  .toJson(QJsonDocument::Compact)
              + '\n';
        identity = Data::RuntimePackageActivationIdentity{
            Data::RuntimePackageActivationOperationId{std::move(operationId)},
            scope,
            documentRevision,
            originalBinding,
            targetBinding,
            targetReference.artifactId,
            artifact.configurationId,
            artifact.catalogRevision,
            factoryOpaqueId(artifact.topologyIdentity),
            Data::RuntimePackageActivationSha256{
                evidence->semanticMappingProof().packageSha256},
            Data::RuntimePackageActivationSha256{
                evidence->projectConfigurationSha256()},
            Data::RuntimePackageActivationSha256{QCryptographicHash::hash(
                effectiveProjectCompanion, QCryptographicHash::Sha256)},
            evidence->semanticMappingProof(),
        };
        request = Data::RuntimePackageActivationRequest{
            identity,
            packageBytes,
            projectBytes,
            effectiveProjectCompanion,
            true,
        };
        if (!request.isValid())
            return Utils::ResultError("The activation request is invalid");

        projects.addProject(project);
        projects.activationCapture
            = Data::RuntimePackageActivationProjectCapture{
                project,
                projectBytes,
                1,
                documentRevision,
                originalBinding,
            };
        if (!projects.activationCapture->isValid())
            return Utils::ResultError("The activation project capture is invalid");

        catalog = factoryCatalog(project, *evidence);
        attestation = factoryAttestation(catalog, *evidence);
        provider.deploymentMode = deploymentMode;
        provider.catalog = catalog;
        provider.attestation = attestation;
        provider.previousActive
            = {Data::ControllerSlot::A, 11, 813};
        provider.candidate = {
            catalog.epoch.activePackageSlot,
            catalog.epoch.activePackageGeneration,
            catalog.epoch.configurationId,
        };
        provider.snapshot = connectedSnapshot(scope, catalog.sessionGeneration);
        provider.snapshot.protocolVersion = {1, 14};
        provider.snapshot.readOnly = false;
        provider.snapshot.mock = false;
        provider.snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        provider.snapshot.updatedAt = provider.snapshot.connectedAt;
        provider.snapshot.session = Data::ControllerSessionSummary{
            0x0102030405060708ULL,
            catalog.epoch.controllerBootId,
            0,
            30000,
            false,
        };
        Data::ControllerStateSummary controllerState;
        controllerState.serviceState = Data::ControllerServiceState::Shutdown;
        controllerState.severity = Data::ControllerSeverity::Information;
        controllerState.ready = true;
        controllerState.controllerBootId = catalog.epoch.controllerBootId;
        provider.snapshot.controllerState = controllerState;
        Data::ControllerPackageSummary package;
        package.activeSlot = provider.previousActive.slot;
        package.activeGeneration = provider.previousActive.generation;
        package.activeConfigurationId
            = provider.previousActive.configurationId;
        package.controllerState = Data::ControllerPackageState::Empty;
        package.controllerBootId = catalog.epoch.controllerBootId;
        provider.snapshot.package = package;
        Data::ControllerTopologySnapshot topology;
        topology.firstStationAddress = artifact.topologyInstances.isEmpty()
                                           ? 0
                                           : artifact.topologyInstances.constFirst()
                                                 .stationAddress;
        topology.respondingCount = quint32(artifact.topologyInstances.size());
        topology.result = 0;
        for (const SemanticBindingTopologyInstance &instance :
             artifact.topologyInstances) {
            topology.slaves.append({
                instance.position,
                instance.stationAddress,
                8,
                0,
                instance.vendorId,
                instance.productCode,
                instance.revision.value_or(0),
                instance.serial.value_or(0),
            });
        }
        topology.discoveredAt = QDateTime::currentDateTimeUtc();
        topology.scope = scope;
        topology.sessionGeneration = catalog.sessionGeneration;
        topology.sessionId = provider.snapshot.session->sessionId;
        topology.bootId = provider.snapshot.session->bootId;
        topology.requestId = 1;
        topology.responseSequence = 2;
        topology.cpu1RequestSequence = 7;
        topology.cpu1CompletedTimeNs = 123456789;
        topology.receivedAt = topology.discoveredAt;
        provider.snapshot.topology = topology;
        provider.setAvailable(true);

        QList<Data::DeviceAdapterManifest> activationManifests
            = adapterManifests;
        for (Data::DeviceAdapterManifest &manifest : activationManifests) {
            manifest.signatureVerified = true;
            manifest.realHardwareAllowed = true;
        }
        ownedAdapterProvider
            = std::make_unique<ActivationAdapterProvider>(
                std::move(activationManifests));
        adapterRegistration
            = std::make_unique<RegisteredObject>(
                ownedAdapterProvider.get());
        registration = std::make_unique<RegisteredObject>(&provider);
        registry = std::make_unique<Core::ProviderRegistry>();
        journalRoot = QDir(temporary.path()).filePath("activation-journal");
        service = std::make_unique<TrustedRuntimePackageActivationService>(
            &projects,
            registry.get(),
            repository,
            journalRoot,
            nullptr,
            15000,
            45000,
            std::function<bool()>{},
            exactProjectProofVerifier());
        if (prepareForStart)
            return prepareCurrentRequest();
        return Utils::ResultOk;
    }

    Utils::Result<> prepareCurrentRequest()
    {
        if (!service)
            return Utils::ResultError("The activation service is unavailable");
        const Core::RuntimePackageActivationPreparationResult prepared
            = service->prepare({
                identity.operationId(),
                projectScope(project),
                packageBytes,
                projectBytes,
                effectiveProjectCompanion,
                compilerVerification(),
                request.rollbackOnActivationFailure(),
                compilerActivationProof(),
            });
        if (!prepared.isValid() || !prepared.request) {
            return Utils::ResultError(
                prepared.detail.isEmpty()
                    ? QStringLiteral(
                          "The activation request could not be prepared.")
                    : prepared.detail);
        }
        request = *prepared.request;
        identity = request.identity();
        targetBinding = identity.targetBindingToken();
        return Utils::ResultOk;
    }

    void setAdapterAuthorization(
        bool signatureVerified, bool realHardwareAllowed)
    {
        for (Data::DeviceAdapterManifest &manifest :
             ownedAdapterProvider->manifests) {
            manifest.signatureVerified = signatureVerified;
            manifest.realHardwareAllowed = realHardwareAllowed;
        }
    }

    Data::RuntimePackageActivationRequest requestWithPackage(
        QByteArray changedPackage) const
    {
        Data::RuntimeSemanticMappingProof proof
            = identity.expectedMappingProof();
        proof.packageSha256 = QCryptographicHash::hash(
            changedPackage, QCryptographicHash::Sha256);
        const Data::RuntimePackageActivationIdentity changedIdentity{
            identity.operationId(),
            identity.scope(),
            identity.documentRevisionToken(),
            identity.originalBindingToken(),
            identity.targetBindingToken(),
            identity.bindingArtifactId(),
            identity.configurationId(),
            identity.expectedCatalogRevision(),
            identity.expectedTopologyIdentity(),
            Data::RuntimePackageActivationSha256{proof.packageSha256},
            identity.compiledProjectSha256(),
            identity.effectiveProjectCompanionSha256(),
            proof,
        };
        return {
            changedIdentity,
            std::move(changedPackage),
            projectBytes,
            effectiveProjectCompanion,
            true,
        };
    }

    Data::RuntimePackageCompilerVerifyResult compilerVerification() const
    {
        return compilerVerificationForCompanion(
            effectiveProjectCompanion);
    }

    Data::RuntimePackageCompilerActivationProof compilerActivationProof() const
    {
        return EtherCAT::Core::Internal::
            syntheticRuntimePackageCompilerActivationProof();
    }

    Data::RuntimePackageCompilerVerifyResult
    compilerVerificationForCompanion(QByteArrayView companion) const
    {
        const auto digest = [](QByteArrayView bytes) {
            return Data::RuntimePackageCompilerSha256{
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
        };
        const Data::RuntimePackageCompilerSha256 packageSha256{
            evidence->semanticMappingProof().packageSha256};
        const Data::RuntimePackageCompilerSha256 companionSha256
            = digest(companion);
        const Data::RuntimePackageCompilerSha256 intentSha256
            = digest("activation-intent");
        const Data::RuntimePackageCompilerSha256 targetProfileSha256
            = digest("target-profile");
        const Data::RuntimePackageCompilerSha256 adapterBundleSha256
            = digest("adapter-bundle");
        const Data::RuntimePackageCompilerSha256 topologyEvidenceSha256
            = digest("topology-evidence");
        const QByteArray exactResult
            = QStringLiteral(
                  "{\"adapter_bundle_sha256\":\"%1\",\"configuration_id\":%2,"
                  "\"effective_project_companion_sha256\":\"%3\","
                  "\"format\":\"ethercat-ide-project-compiler-verification-v1\","
                  "\"intent_sha256\":\"%4\",\"manifest_format_version\":2,"
                  "\"package_sha256\":\"%5\",\"status\":\"pass\","
                  "\"target_profile_sha256\":\"%6\",\"topology_evidence_sha256\":\"%7\"}\n")
                  .arg(
                      QString::fromLatin1(adapterBundleSha256.value().toHex()),
                      QString::number(
                          evidence->semanticBindingArtifact().configurationId),
                      QString::fromLatin1(companionSha256.value().toHex()),
                      QString::fromLatin1(intentSha256.value().toHex()),
                      QString::fromLatin1(packageSha256.value().toHex()),
                      QString::fromLatin1(targetProfileSha256.value().toHex()),
                      QString::fromLatin1(topologyEvidenceSha256.value().toHex()))
                  .toLatin1();
        const Data::RuntimePackageCompilerResultEnvelope envelope{
            Data::RuntimePackageCompilerCommand::Verify,
            Data::RuntimePackageCompilerResultStatus::Succeeded,
            QStringLiteral("pass"),
            Data::RuntimePackageCompilerOperationId{
                QStringLiteral("03803804-2001-4000-8000-000000000001")},
            evidence->semanticBindingArtifact().configurationId,
            digest("verify-request"),
            Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(
                exactResult),
            {},
        };
        return {
            envelope,
            packageSha256,
            2,
            intentSha256,
            companionSha256,
            targetProfileSha256,
            adapterBundleSha256,
            topologyEvidenceSha256,
            true,
        };
    }

    TrustedRuntimePackageActivationService::
        ExactCompileTimeProjectProofVerifier
    exactProjectProofVerifier()
    {
        return [this](
                   const Core::RuntimePackageActivationPreparationRequest &candidate,
                   const Data::RuntimePackageActivationProjectCapture &capture,
                   const VerifiedRuntimePackageEvidence &verified)
                   -> Utils::Result<> {
            if (!projects.activationCapture
                || capture != *projects.activationCapture
                || capture.snapshot() != project
                || capture.documentRevisionNumber() != 1
                || capture.documentRevision() != documentRevision
                || capture.originalBinding() != originalBinding
                || candidate.operationId != identity.operationId()
                || candidate.scope != projectScope(project)
                || candidate.packageBytes != packageBytes
                || candidate.compiledProjectSource != projectBytes
                || candidate.effectiveProjectCompanion
                       != effectiveProjectCompanion
                || candidate.compilerVerification
                       != compilerVerification()
                || !candidate.compilerActivationProof
                || !candidate.compilerActivationProof->isValid()
                || verified.semanticMappingProof()
                       != evidence->semanticMappingProof()
                || verified.projectConfigurationSha256()
                       != evidence->projectConfigurationSha256()) {
                return Utils::ResultError(
                    "The deterministic compile-time project proof does not match");
            }
            return Utils::ResultOk;
        };
    }

    int pendingGuardCount() const
    {
        return QDir(journalRoot).entryList(
            {QStringLiteral("*.pending.json")},
            QDir::Files | QDir::NoSymLinks).size();
    }

    Utils::Result<> resetServiceWithProviderDeadline(int deadlineMs)
    {
        service = std::make_unique<TrustedRuntimePackageActivationService>(
            &projects,
            registry.get(),
            repository,
            journalRoot,
            nullptr,
            deadlineMs,
            deadlineMs,
            std::function<bool()>{},
            exactProjectProofVerifier());
        return prepareCurrentRequest();
    }

    QTemporaryDir temporary;
    TestProjectService projects;
    std::unique_ptr<ActivationControllerProvider> ownedProvider;
    ActivationControllerProvider &provider;
    QByteArray packageBytes;
    QByteArray projectBytes;
    QByteArray effectiveProjectCompanion;
    Data::ProjectSnapshot project;
    Data::SemanticBindingArtifactReference targetReference;
    Data::RuntimePackageActivationDocumentRevisionToken documentRevision;
    Data::RuntimePackageActivationOriginalBindingToken originalBinding;
    Data::RuntimePackageActivationOriginalBindingToken targetBinding;
    Data::RuntimePackageActivationIdentity identity;
    Data::RuntimePackageActivationRequest request;
    Data::RuntimeResourceCatalog catalog;
    Data::RuntimeSemanticMappingAttestation attestation;
    QString journalRoot;
    std::shared_ptr<RuntimePackageEvidenceRepository> repository;
    std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence;
    QList<Data::DeviceAdapterManifest> adapterManifests;
    std::unique_ptr<ActivationAdapterProvider> ownedAdapterProvider;
    std::unique_ptr<Core::ProviderRegistry> registry;
    std::unique_ptr<RegisteredObject> adapterRegistration;
    std::unique_ptr<RegisteredObject> registration;
    std::unique_ptr<TrustedRuntimePackageActivationService> service;
};

static Data::RuntimeResourceSnapshot factorySnapshot(
    const Data::RuntimeResourceCatalog &catalog)
{
    Data::RuntimeResourceSnapshot snapshot;
    snapshot.scope = catalog.scope;
    snapshot.sessionGeneration = catalog.sessionGeneration;
    snapshot.epoch = catalog.epoch;
    snapshot.snapshotSequence = 1;
    snapshot.captureCycle = 100;
    snapshot.controllerTimestampNs = 1000;
    snapshot.receivedAt = QDateTime::currentDateTimeUtc();
    snapshot.complete = true;
    snapshot.samples.reserve(catalog.resources.size());

    for (const Data::RuntimeResourceDescriptor &descriptor : catalog.resources) {
        Data::RuntimeResourceSample sample;
        sample.resourceId = descriptor.id;
        sample.consistencyGroupId = descriptor.consistencyGroupId;
        sample.value.primitiveType = descriptor.primitiveType;
        sample.value.typeIdentity = descriptor.valueTypeIdentity;
        switch (descriptor.primitiveType) {
        case Data::RuntimeResourcePrimitiveType::Boolean:
            sample.value.value = false;
            break;
        case Data::RuntimeResourcePrimitiveType::SignedInteger:
            sample.value.value = QVariant::fromValue<qlonglong>(0);
            break;
        case Data::RuntimeResourcePrimitiveType::UnsignedInteger:
            sample.value.value = QVariant::fromValue<qulonglong>(0);
            break;
        case Data::RuntimeResourcePrimitiveType::FloatingPoint:
            sample.value.value = 0.0;
            break;
        case Data::RuntimeResourcePrimitiveType::ByteArray:
            sample.value.value = QByteArray((descriptor.bitWidth + 7) / 8, '\0');
            break;
        case Data::RuntimeResourcePrimitiveType::Text:
        case Data::RuntimeResourcePrimitiveType::Opaque:
            break;
        }
        sample.quality.state = Data::RuntimeResourceQualityState::Good;
        sample.valueSequence = 1;
        sample.controllerTimestampNs = snapshot.controllerTimestampNs;
        snapshot.samples.append(std::move(sample));
    }
    return snapshot;
}

static Utils::Result<VerifiedRuntimePackageEvidence> api038ActionEvidence()
{
    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    const QByteArray publicKey = readTestData(
        "testdata/api038/"
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.size() != 32)
        return Utils::ResultError("API-038 action test data is incomplete");

    const Utils::Result<VerifiedEcpkgPackage> package = verifyProductionEcpkg(
        packageBytes, {{publicKey, EcpkgTrustClass::Production}}, projectBytes);
    if (!package)
        return Utils::ResultError(package.error());
    return verifyRuntimePackageEvidence(*package);
}

static Utils::Result<Data::SemanticRuntimeContext> api038ActionContext(
    const VerifiedRuntimePackageEvidence &evidence)
{
    if (!evidence.actionDefinitions())
        return Utils::ResultError("API-038 action definitions are unavailable");

    Data::ProjectSnapshot project = factoryProject(evidence);
    const Utils::Result<QList<Data::DeviceAdapterManifest>> historicalAdapters
        = api038HistoricalTestAdapters(evidence);
    if (!historicalAdapters)
        return Utils::ResultError(historicalAdapters.error());
    const QList<Data::DeviceAdapterManifest> adapterManifests = *historicalAdapters;
    configureApi038V3ManualProject(project, adapterManifests);
    const Data::RuntimeResourceCatalog catalog = factoryCatalog(project, evidence);
    const Data::RuntimeSemanticMappingAttestation attestation
        = factoryAttestation(catalog, evidence);
    const Utils::Result<ReadOnlySemanticBindingCandidates> candidates
        = buildReadOnlySemanticBindingCandidates(
            u"embed-labs.product-api", project, evidence, catalog, attestation);
    if (!candidates)
        return Utils::ResultError(candidates.error());

    const Utils::Result<QList<Data::SemanticActionRuntimeState>> states
        = buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            project,
            evidence,
            *candidates,
            adapterManifests,
            {
                true,
                true,
                Data::ControllerServiceState::OperationalSafe,
                true,
            });
    if (!states)
        return Utils::ResultError(states.error());

    Data::SemanticRuntimeContext context;
    context.controllerId = QStringLiteral("embed-labs.product-api");
    context.scope = catalog.scope;
    context.sessionGeneration = catalog.sessionGeneration;
    context.epoch = catalog.epoch;
    context.mappingDigest = candidates->mappingDigest;
    context.controllerMappingDigest = candidates->controllerMappingDigest;
    context.actionDefinitionsDigest = {
        QStringLiteral("sha256"),
        evidence.actionDefinitions()->definitionsSha256,
    };
    context.cyclePeriodNs = evidence.cyclePeriodNs();
    context.bindingVerification = candidates->verification;
    context.signalStates = candidates->signalStates;
    context.actionStates = *states;
    context.complete = true;
    context.contextHash = semanticRuntimeContextHash(context);
    if (context.contextHash.size()
        != QCryptographicHash::hashLength(QCryptographicHash::Sha256)) {
        return Utils::ResultError("API-038 action context hash is invalid");
    }
    return context;
}

static Data::SemanticOperationRequest api038ActionRequest(
    const Data::SemanticRuntimeContext &context,
    QStringView actionBindingId,
    QStringView operationId)
{
    const auto action = std::find_if(
        context.actionStates.cbegin(),
        context.actionStates.cend(),
        [actionBindingId](const Data::SemanticActionRuntimeState &candidate) {
            return candidate.actionBindingId == actionBindingId;
        });

    Data::SemanticOperationRequest request;
    request.operationId.value = operationId.toString();
    request.kind = Data::SemanticOperationKind::InvokeAction;
    if (action != context.actionStates.cend()) {
        request.target = action->target;
        request.expectedActionDefinitionDigest = action->actionDefinitionDigest;
    }
    request.expectedEpoch = context.epoch;
    request.expectedMappingDigest = context.mappingDigest;
    request.expectedControllerMappingDigest = context.controllerMappingDigest;
    request.expectedContextHash = context.contextHash;
    request.ttlCycles = 1000;
    request.reason = QStringLiteral("API-038 focused action test");
    return request;
}

static void setApi038DigitalOutputParameters(Data::SemanticOperationRequest &request)
{
    for (int channel = 0; channel < 16; ++channel) {
        request.parameters.insert(
            QStringLiteral("do%1").arg(channel),
            channel % 3 == 0);
    }
}

static void setApi051DigitalOutputParameters(Data::SemanticOperationRequest &request, bool output0)
{
    request.parameters.clear();
    for (int channel = 0; channel < 16; ++channel) {
        request.parameters.insert(QStringLiteral("do%1").arg(channel), channel == 0 && output0);
    }
    request.ttlCycles = api051OutputTtlCycles;
    request.reason = QStringLiteral("API-051 cfg3702 headless semantic acceptance");
}

template<typename Predicate>
static bool waitForApi051Condition(Predicate &&predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    return predicate();
}

static bool api051Connected(const Data::ControllerConnectionSnapshot &snapshot)
{
    QSet<QString> channelIds;
    for (const Data::ControllerChannelStatus &channel : snapshot.channels) {
        if (channel.state != Data::ControllerChannelState::Connected)
            return false;
        channelIds.insert(channel.id);
    }
    return snapshot.state == Data::ControllerConnectionState::Connected
           && snapshot.channels.size() == 3
           && channelIds
                  == QSet<QString>{
                      QStringLiteral("control"),
                      QStringLiteral("push"),
                      QStringLiteral("bulk"),
                  };
}

static bool api051HasAllV114Capabilities(
    const std::optional<Data::ControllerCapabilitySummary> &capability)
{
    return capability && capability->controlLease && capability->resumablePush
           && capability->transactionalBulk && capability->capabilityQuery
           && capability->sdoFailureDiagnostics && capability->exactAlarmReplay
           && capability->linkDiagnostics && capability->timeCorrelation
           && capability->processInputSample && capability->structuredHandshakeError
           && capability->firmwareUpdate && capability->explicitTimingModeStart
           && capability->faultReset && capability->runtimeResources
           && capability->semanticMappingAttestation && capability->runtimeOutputTransactions;
}

static QString api051ExecutionCorrelationId(
    const Data::SemanticOperationId &operationId, QStringView phase, quint32 index = 0)
{
    QByteArray material = operationId.value.toUtf8();
    material.append('\0');
    material.append(phase.toUtf8());
    material.append('\0');
    const quint32 bigEndianIndex = qToBigEndian(index);
    material.append(
        reinterpret_cast<const char *>(&bigEndianIndex), qsizetype(sizeof(bigEndianIndex)));
    return QStringLiteral("semantic-")
           + QString::fromLatin1(
               QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex())
           + QLatin1Char('-') + phase.toString();
}

template<typename Result>
static const Result *api051SingleCorrelatedResult(
    const QList<Result> &results, QStringView correlationId)
{
    const auto matches = [correlationId](const Result &result) {
        return result.request.correlationId == correlationId;
    };
    if (std::count_if(results.cbegin(), results.cend(), matches) != 1)
        return nullptr;
    return &*std::find_if(results.cbegin(), results.cend(), matches);
}

static const Data::RuntimeOutputTransactionResult *api051SingleOutputResult(
    const QList<Data::RuntimeOutputTransactionResult> &results,
    const Data::RuntimeOutputOperationId &operationId)
{
    const auto matches = [&operationId](const Data::RuntimeOutputTransactionResult &result) {
        return result.request.operationId == operationId;
    };
    if (std::count_if(results.cbegin(), results.cend(), matches) != 1)
        return nullptr;
    return &*std::find_if(results.cbegin(), results.cend(), matches);
}

static QString executeApi051Command(
    Core::ControllerConnectionProvider &provider,
    const Data::ControllerControlRequest &request,
    int timeoutMs)
{
    const Utils::Result<> dispatched = provider.executeControlCommand(request);
    if (!dispatched)
        return dispatched.error();
    const bool terminal = waitForApi051Condition(
        [&provider, command = request.command] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            return (snapshot.controlProgress.command == command
                    && snapshot.controlProgress.state != Data::ControllerControlState::Pending
                    && snapshot.controlProgress.state != Data::ControllerControlState::Idle)
                   || snapshot.state == Data::ControllerConnectionState::Disconnected
                   || snapshot.state == Data::ControllerConnectionState::Failed;
        },
        timeoutMs);
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    if (!terminal || snapshot.controlProgress.command != request.command)
        return QStringLiteral("The controller command did not reach a terminal result");
    if (snapshot.controlProgress.state != Data::ControllerControlState::Succeeded
        || !snapshot.controlProgress.final || snapshot.controlProgress.status != 0
        || snapshot.controlProgress.operationResult != 0) {
        const QString providerDetail
            = snapshot.lastError
                  ? QStringLiteral(" [%1: %2]")
                        .arg(snapshot.lastError->codeName, snapshot.lastError->detail)
                  : QString();
        return QStringLiteral("stage %1 status %2 result %3: %4%5")
            .arg(snapshot.controlProgress.stage)
            .arg(snapshot.controlProgress.status.value_or(0))
            .arg(snapshot.controlProgress.operationResult.value_or(0))
            .arg(snapshot.controlProgress.detail, providerDetail);
    }
    return {};
}

static std::optional<Data::SemanticRuntimeContext> api051Context(
    const SemanticRuntimeExecutor &executor, const Data::ControllerConnectionScope &scope)
{
    QList<Data::SemanticRuntimeContext> matches;
    for (const Data::SemanticRuntimeContext &context : executor.contexts()) {
        if (context.scope == scope)
            matches.append(context);
    }
    if (matches.size() != 1)
        return {};
    return matches.constFirst();
}

static QString api051LiveDcGate(
    const Core::ControllerConnectionProvider &provider,
    const SemanticRuntimeExecutor &executor,
    const Data::ControllerConnectionScope &scope,
    quint64 sessionGeneration,
    quint64 sessionId,
    quint64 bootId,
    const Data::RuntimeResourceCatalogEpoch &epoch)
{
    const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
    const std::optional<Data::RuntimeResourceCatalog> catalog = provider.runtimeResourceCatalog();
    const std::optional<Data::SemanticRuntimeContext> context = api051Context(executor, scope);
    if (!api051Connected(snapshot) || snapshot.scope != scope
        || snapshot.sessionGeneration != sessionGeneration || !snapshot.session
        || snapshot.session->sessionId != sessionId || snapshot.session->bootId != bootId
        || !snapshot.session->ownsControlLease
        || snapshot.session->controlLeaseOwnerSessionId != sessionId || !snapshot.controllerState
        || !snapshot.package || !catalog || !context || !context->complete) {
        return QStringLiteral(
            "The live Product API session, lease, package, catalog, or semantic context changed");
    }

    const Data::ControllerStateSummary &state = *snapshot.controllerState;
    const Data::ControllerPackageSummary &package = *snapshot.package;
    if (state.serviceState != Data::ControllerServiceState::Running || !state.ready
        || !state.busOperational || !state.applicationActive || state.safeOutput
        || !state.distributedClocksLocked || state.controllerBootId != bootId
        || state.ethercatAlStateBits != 0x08
        || state.expectedWorkingCounter != api051ExpectedWorkingCounter
        || state.actualWorkingCounter != api051ExpectedWorkingCounter || state.currentFaults
        || state.latchedFaults || !state.cycleCount
        || package.activeSlot != Data::ControllerSlot::A
        || package.activeGeneration != api051PackageGeneration
        || package.activeConfigurationId != api051ConfigurationId
        || package.controllerState != Data::ControllerPackageState::Active
        || package.controllerBootId != bootId) {
        return QStringLiteral(
            "The live controller is not fault-free cfg3702 RUNNING/DC/WKC11/11");
    }

    const QByteArray expectedMapping = QByteArray::fromHex(api051MappingSha256);
    if (catalog->scope != scope || catalog->sessionGeneration != sessionGeneration
        || catalog->epoch != epoch || catalog->resources.size() != 56 || context->scope != scope
        || context->sessionGeneration != sessionGeneration || context->epoch != epoch
        || context->mappingDigest.value != expectedMapping
        || context->controllerMappingDigest.value != expectedMapping
        || context->cyclePeriodNs != api051CyclePeriodNs) {
        return QStringLiteral("The live cfg3702 semantic epoch or mapping changed");
    }
    return {};
}

static bool api051OperationTerminal(Data::SemanticOperationState state)
{
    using State = Data::SemanticOperationState;
    return state == State::Rejected || state == State::Succeeded || state == State::Failed
           || state == State::TimedOut || state == State::OutcomeUnknown || state == State::Canceled
           || state == State::Expired;
}

static bool api051SemanticSnapshotMatches(
    const std::optional<Data::SemanticOperationSnapshot> &snapshot, bool output0, QString *error)
{
    if (!snapshot || !snapshot->complete || snapshot->observations.size() != 16) {
        *error = QStringLiteral("The semantic output readback is not one complete 16-bit snapshot");
        return false;
    }
    QSet<int> channels;
    for (const Data::SemanticOperationSignalObservation &observation : snapshot->observations) {
        const QString id = observation.target.signalId.value;
        const qsizetype marker = id.lastIndexOf(QStringLiteral(".channel."));
        bool validChannel = false;
        const int channel = marker >= 0 ? id.sliced(marker + qsizetype(9)).toInt(&validChannel)
                                        : -1;
        if (!validChannel || channel < 0 || channel >= 16 || channels.contains(channel)
            || observation.value.primitiveType != Data::RuntimeResourcePrimitiveType::Boolean
            || observation.value.value.metaType().id() != QMetaType::Bool
            || observation.value.value.toBool() != (channel == 0 && output0)
            || observation.quality.state != Data::RuntimeResourceQualityState::Good) {
            *error = QStringLiteral("The semantic output readback value or quality differs");
            return false;
        }
        channels.insert(channel);
    }
    if (channels.size() != 16) {
        *error = QStringLiteral("The semantic output readback does not cover channels 0 through 15");
        return false;
    }
    return true;
}

static QList<Data::RuntimeResourceId> api051Xb6OutputResourceIds(
    const Data::SemanticRuntimeContext &context)
{
    const auto action = std::find_if(
        context.actionStates.cbegin(),
        context.actionStates.cend(),
        [](const Data::SemanticActionRuntimeState &candidate) {
            return candidate.actionBindingId
                   == QStringLiteral("embedlabs:project:action:xb6:set-outputs");
        });
    if (action == context.actionStates.cend() || action->bindings.size() != 16)
        return {};
    QList<Data::RuntimeResourceId> result;
    result.reserve(action->bindings.size());
    for (const Data::SemanticRuntimeBinding &binding : action->bindings)
        result.append(binding.resourceId);
    std::sort(
        result.begin(),
        result.end(),
        [](const Data::RuntimeResourceId &left, const Data::RuntimeResourceId &right) {
            return left.value < right.value;
        });
    if (std::adjacent_find(result.cbegin(), result.cend()) != result.cend())
        return {};
    return result;
}

static bool api051RuntimeSnapshotAllFalse(
    const Data::RuntimeResourceSnapshotResult &result, QString *error)
{
    if (!result.isValid() || !result.snapshot || result.snapshot->samples.size() != 16) {
        *error = QStringLiteral("The controller safe-hold readback is incomplete");
        return false;
    }
    for (const Data::RuntimeResourceSample &sample : result.snapshot->samples) {
        if (sample.value.primitiveType != Data::RuntimeResourcePrimitiveType::Boolean
            || sample.value.value.metaType().id() != QMetaType::Bool || sample.value.value.toBool()
            || sample.quality.state != Data::RuntimeResourceQualityState::Good) {
            *error = QStringLiteral(
                "The controller safe-hold readback is not 16 valid false values");
            return false;
        }
    }
    return true;
}

static Data::ControllerConnectionSnapshot executionConnectionSnapshot(
    const Data::ControllerConnectionScope &scope,
    quint64 sessionGeneration,
    const Data::RuntimeResourceCatalogEpoch &epoch)
{
    Data::ControllerConnectionSnapshot snapshot = connectedSnapshot(scope, sessionGeneration);
    snapshot.protocolVersion = {1, 14};

    Data::ControllerSessionSummary session;
    session.sessionId = 0x0102030405060708ULL;
    session.bootId = epoch.controllerBootId;
    session.controlLeaseOwnerSessionId = session.sessionId;
    session.defaultControlLeaseDurationMs = 30000;
    session.ownsControlLease = true;
    snapshot.session = session;

    Data::ControllerStateSummary state;
    state.serviceState = Data::ControllerServiceState::OperationalSafe;
    state.severity = Data::ControllerSeverity::Information;
    state.ready = true;
    state.busOperational = true;
    state.safeOutput = true;
    state.distributedClocksLocked = true;
    state.controllerBootId = epoch.controllerBootId;
    state.expectedWorkingCounter = 11;
    state.actualWorkingCounter = 11;
    snapshot.controllerState = state;

    Data::ControllerCapabilitySummary capability;
    capability.controlLease = true;
    capability.runtimeResources = true;
    capability.semanticMappingAttestation = true;
    capability.runtimeOutputTransactions = true;
    capability.distributedClocks = true;
    snapshot.capability = capability;
    return snapshot;
}

static Data::RuntimeResourceSnapshot targetedSnapshot(
    const Data::RuntimeResourceSnapshot &source,
    const Data::RuntimeResourceSnapshotRequest &request,
    const QList<Data::RuntimeOutputValueWrite> &writes,
    quint64 sequence,
    quint64 captureCycle,
    quint64 controllerTimestampNs)
{
    Data::RuntimeResourceSnapshot snapshot;
    snapshot.scope = request.scope;
    snapshot.sessionGeneration = request.sessionGeneration;
    snapshot.epoch = request.expectedEpoch;
    snapshot.snapshotSequence = sequence;
    snapshot.captureCycle = captureCycle;
    snapshot.controllerTimestampNs = controllerTimestampNs;
    snapshot.receivedAt = QDateTime::currentDateTimeUtc();
    snapshot.complete = true;
    for (const Data::RuntimeResourceId &resourceId : request.resourceIds) {
        const auto found = std::find_if(
            source.samples.cbegin(),
            source.samples.cend(),
            [&resourceId](const Data::RuntimeResourceSample &candidate) {
                return candidate.resourceId == resourceId;
            });
        if (found == source.samples.cend())
            continue;
        Data::RuntimeResourceSample sample = *found;
        const auto write = std::find_if(
            writes.cbegin(),
            writes.cend(),
            [&resourceId](const Data::RuntimeOutputValueWrite &candidate) {
                return candidate.resourceId == resourceId;
            });
        if (write != writes.cend())
            sample.value = write->value;
        sample.valueSequence = sequence;
        sample.controllerTimestampNs = controllerTimestampNs;
        snapshot.samples.append(std::move(sample));
    }
    return snapshot;
}

static Data::RuntimeOutputGroupPolicy outputPolicy(
    const Data::RuntimeOutputGroupPolicyRequest &request,
    const SemanticActionPlanGroup &group,
    quint64 outputGeneration)
{
    Data::RuntimeOutputGroupPolicy policy;
    policy.scope = request.scope;
    policy.sessionGeneration = request.sessionGeneration;
    policy.epoch = request.expectedEpoch;
    policy.consistencyGroupId = request.consistencyGroupId;
    policy.mappingDigest = request.expectedMappingDigest;
    policy.completeGroupRecordDigest = group.completeGroupRecordDigest();
    policy.recoveryPolicy = group.recoveryPolicy();
    policy.maximumTtlCycles = group.maximumTtlCycles();
    policy.completeResourceCount = group.completeResourceCount();
    policy.currentOutputGeneration = outputGeneration;
    policy.manualWriteAllowed = true;
    policy.receivedAt = QDateTime::currentDateTimeUtc();
    return policy;
}

static Data::RuntimeOutputTransactionState idleOutputState(
    const Data::RuntimeOutputTransactionStateRequest &request, quint64 outputGeneration)
{
    Data::RuntimeOutputTransactionState state;
    state.scope = request.scope;
    state.sessionGeneration = request.sessionGeneration;
    state.epoch = request.expectedEpoch;
    state.mappingDigest = request.expectedMappingDigest;
    state.state = Data::RuntimeOutputState::Idle;
    state.outputGeneration = outputGeneration;
    state.controllerTimestampNs = 2000;
    state.receivedAt = QDateTime::currentDateTimeUtc();
    return state;
}

static Data::RuntimeOutputTransactionState completedOutputState(
    const Data::RuntimeOutputTransactionRequest &request,
    Data::RuntimeOutputTransactionOutcome outcome,
    quint64 appliedCycle = 120)
{
    Data::RuntimeOutputTransactionState state;
    state.scope = request.scope;
    state.sessionGeneration = request.sessionGeneration;
    state.epoch = request.expectedEpoch;
    state.mappingDigest = request.expectedMappingDigest;
    state.operationId = request.operationId;
    state.appliedCycle = appliedCycle;
    state.expiryCycle = appliedCycle + request.ttlCycles;
    state.consistencyGroupId = request.consistencyGroupId;
    state.ttlCycles = request.ttlCycles;
    state.recoveryPolicy = request.expectedRecoveryPolicy;
    state.valueCount = quint16(request.completeGroupWrites.size());
    state.controllerTimestampNs = 3000;
    state.receivedAt = QDateTime::currentDateTimeUtc();
    if (outcome == Data::RuntimeOutputTransactionOutcome::Applied) {
        state.state = Data::RuntimeOutputState::OverrideActive;
        state.resultFlags = Data::RuntimeOutputTransactionResultFlag::OverrideActive;
        state.outputGeneration = request.expectedOutputGeneration + 1;
    } else if (request.expectedRecoveryPolicy == Data::RuntimeOutputRecoveryPolicy::HoldSafe) {
        state.state = Data::RuntimeOutputState::SafeHold;
        state.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
        state.outputGeneration = request.expectedOutputGeneration + 2;
        state.providerDetail = state.expiryCycle;
    } else {
        state.state = Data::RuntimeOutputState::Idle;
        state.resultFlags = Data::RuntimeOutputTransactionResultFlag::ReturnedTask;
        state.outputGeneration = request.expectedOutputGeneration + 2;
        state.providerDetail = state.expiryCycle;
    }
    return state;
}

static Data::ControllerOperationError uncertainOutputError()
{
    Data::ControllerOperationError error;
    error.source = Data::ControllerErrorSource::Network;
    error.channelId = QStringLiteral("Control");
    error.operation = Data::ControllerOperation::ApplyRuntimeOutputTransaction;
    error.codeName = QStringLiteral("OUTCOME_UNKNOWN");
    error.retryDisposition = Data::ControllerRetryDisposition::Retryable;
    error.summary = QStringLiteral("The terminal output response was not observed.");
    error.detail = QStringLiteral("Reconcile the retained operation identifier.");
    return error;
}

static Data::SemanticRuntimeActor api038Submitter()
{
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("automation/api038-executor-test");
    actor.displayName = QStringLiteral("API-038 executor test");
    actor.kind = Data::SemanticRuntimeActorKind::Automation;
    actor.origin = QStringLiteral("QtTest");
    actor.authenticationDigest = QByteArray(32, '\x51');
    return actor;
}

static Data::SemanticRuntimeActor api038Approver()
{
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("user/api038-executor-test");
    actor.displayName = QStringLiteral("API-038 human approver");
    actor.kind = Data::SemanticRuntimeActorKind::User;
    actor.origin = QStringLiteral("QtTest");
    actor.authenticationDigest = QByteArray(32, '\x52');
    return actor;
}

static Data::SemanticOperationRecord submitAndApprove(
    SemanticRuntimeExecutor &executor,
    const Data::SemanticRuntimeContext &context,
    const Data::SemanticOperationRequest &request)
{
    const Data::SemanticOperationRecord submitted = executor.submit(request, api038Submitter());
    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = submitted.approvalChallenge;
    approval.expectedRequestDigest = submitted.canonicalRequestDigest;
    approval.expectedContextHash = context.contextHash;
    approval.detail = QStringLiteral("Approved by the deterministic QtTest user.");
    return executor.approve(approval, api038Approver());
}

static Utils::Id nextSemanticExecutorFixtureProviderId(QStringView kind)
{
    static quint64 sequence = 0;
    return Utils::Id::fromString(
        QStringLiteral("EtherCAT.SemanticRuntime.Tests.%1.%2")
            .arg(kind)
            .arg(++sequence));
}

class SemanticExecutorFixture final
{
public:
    SemanticExecutorFixture()
        : temporary(systemTemporaryDirectoryTemplate(u"embed-labs-semantic-output-executor"))
        , provider(nextSemanticExecutorFixtureProviderId(u"DeterministicOutput"))
    {}

    ~SemanticExecutorFixture()
    {
        unregisterProviders();
    }

    void unregisterProviders()
    {
        executor.reset();
        ownedRegistry.reset();
        adapterRegistration.reset();
        ownedAdapterProvider.reset();
        registration.reset();
    }

    bool providersAreUnregistered() const
    {
        const QObjectList objects = ExtensionSystem::PluginManager::allObjects();
        return !objects.contains(
            const_cast<DeterministicOutputControllerProvider *>(&provider));
    }

    Utils::Result<> initialize(
        bool privateProviderRegistry = false,
        bool configureManualPolicy = true,
        int liveRefreshTimeoutMs = 5000,
        int liveSampleFreshnessMs = 5000,
        std::function<QDateTime()> utcNow = {})
    {
        if (!temporary.isValid())
            return Utils::ResultError("The semantic executor temporary directory is invalid");

        const QByteArray packageBytes = readTestData(
            "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
        const QByteArray projectBytes = readTestData("testdata/api038/project.json");
        const QString keyFileName = QStringLiteral(
            "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
        const QByteArray publicKey = readTestData(
            QStringLiteral("testdata/api038/") + keyFileName);
        if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.size() != 32)
            return Utils::ResultError("The API-038 executor fixture is incomplete");

        const QString trustRoot = QDir(temporary.path()).filePath("trust");
        if (!QDir().mkpath(trustRoot)
            || !writeFile(QDir(trustRoot).filePath(keyFileName), publicKey)) {
            return Utils::ResultError("The API-038 executor trust store could not be prepared");
        }
        repository = std::make_shared<RuntimePackageEvidenceRepository>(
            QDir(temporary.path()).filePath("packages"),
            trustRoot,
            QDir(temporary.path()).filePath("projects"));
        const Utils::Result<VerifiedRuntimePackageEvidence> imported
            = repository->import(packageBytes, projectBytes);
        if (!imported)
            return Utils::ResultError(imported.error());
        evidence = std::make_shared<const VerifiedRuntimePackageEvidence>(*imported);

        project = factoryProject(*evidence);
        const Utils::Result<QList<Data::DeviceAdapterManifest>> historicalAdapters
            = api038HistoricalTestAdapters(*evidence);
        if (!historicalAdapters)
            return Utils::ResultError(historicalAdapters.error());
        adapterManifests = *historicalAdapters;
        if (configureManualPolicy)
            configureApi038V3ManualProject(project, adapterManifests);
        catalog = factoryCatalog(project, *evidence);
        attestation = factoryAttestation(catalog, *evidence);
        snapshot = factorySnapshot(catalog);
        provider.snapshot = executionConnectionSnapshot(
            catalog.scope, catalog.sessionGeneration, catalog.epoch);
        provider.catalog = catalog;
        provider.resourceSnapshot = snapshot;
        provider.semanticMappingAttestation = attestation;
        provider.setAvailable(true);

        projects.addProject(project);
        ownedAdapterProvider
            = std::make_unique<ActivationAdapterProvider>(adapterManifests);
        adapterRegistration
            = std::make_unique<RegisteredObject>(ownedAdapterProvider.get());
        registration = std::make_unique<RegisteredObject>(&provider);
        Core::ProviderRegistry *registry = nullptr;
        if (privateProviderRegistry) {
            ownedRegistry = std::make_unique<Core::ProviderRegistry>();
            registry = ownedRegistry.get();
        } else {
            registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
        }
        if (!registry)
            return Utils::ResultError("The controller provider registry is unavailable");
        executor = std::make_unique<SemanticRuntimeExecutor>(
            &projects,
            registry,
            nullptr,
            repository,
            liveRefreshTimeoutMs,
            liveSampleFreshnessMs,
            std::move(utcNow));
        if (executor->contexts().size() != 1 || !executor->contexts().constFirst().complete)
            return Utils::ResultError("The API-038 semantic execution context is incomplete");
        const Data::SemanticRuntimeContext &context = executor->contexts().constFirst();
        if (configureManualPolicy) {
            const auto readyAction = std::find_if(
                context.actionStates.cbegin(),
                context.actionStates.cend(),
                [](const Data::SemanticActionRuntimeState &action) {
                    return action.actionBindingId
                               == QStringLiteral("embedlabs:project:action:xb6:set-outputs")
                           && action.availability == Data::SemanticActionAvailability::Ready;
                });
            if (readyAction == context.actionStates.cend())
                return Utils::ResultError("The API-038 XB6 action is not executable");
        }
        return Utils::ResultOk;
    }

    QTemporaryDir temporary;
    TestProjectService projects;
    DeterministicOutputControllerProvider provider;
    std::shared_ptr<const VerifiedRuntimePackageEvidence> evidence;
    QList<Data::DeviceAdapterManifest> adapterManifests;
    Data::ProjectSnapshot project;
    Data::RuntimeResourceCatalog catalog;
    Data::RuntimeSemanticMappingAttestation attestation;
    Data::RuntimeResourceSnapshot snapshot;
    std::unique_ptr<ActivationAdapterProvider> ownedAdapterProvider;
    std::unique_ptr<RegisteredObject> adapterRegistration;
    std::unique_ptr<RegisteredObject> registration;
    std::unique_ptr<Core::ProviderRegistry> ownedRegistry;
    std::shared_ptr<RuntimePackageEvidenceRepository> repository;
    std::unique_ptr<SemanticRuntimeExecutor> executor;
};

static bool advanceExecutorToApply(
    SemanticExecutorFixture &fixture,
    const SemanticActionPlan &plan,
    quint64 outputGeneration = 41,
    quint64 sequence = 2,
    quint64 captureCycle = 110)
{
    if (!fixture.provider.pendingSnapshotRequest)
        return false;
    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    const Data::RuntimeResourceSnapshotResult beforeResult{
        beforeRequest,
        targetedSnapshot(
            fixture.snapshot,
            beforeRequest,
            {},
            sequence,
            captureCycle,
            captureCycle * 10),
        {},
    };
    if (!beforeResult.isValid())
        return false;
    fixture.provider.sendSnapshotResult(beforeResult);
    if (!fixture.provider.pendingPolicyRequest)
        return false;
    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    const Data::RuntimeOutputGroupPolicyResult policyResult{
        policyRequest,
        outputPolicy(policyRequest, plan.groups().constFirst(), outputGeneration),
        {},
    };
    if (!policyResult.isValid())
        return false;
    fixture.provider.sendPolicyResult(policyResult);
    if (!fixture.provider.pendingStateRequest)
        return false;
    const Data::RuntimeOutputTransactionStateRequest stateRequest
        = *fixture.provider.pendingStateRequest;
    const Data::RuntimeOutputTransactionStateResult stateResult{
        stateRequest, idleOutputState(stateRequest, outputGeneration), {}};
    if (!stateResult.isValid())
        return false;
    fixture.provider.sendStateResult(stateResult);
    return fixture.provider.pendingApplyRequest.has_value();
}

static quint32 testReadLe32(QByteArrayView bytes, qsizetype offset)
{
    return quint32(quint8(bytes[offset])) | (quint32(quint8(bytes[offset + 1])) << 8)
           | (quint32(quint8(bytes[offset + 2])) << 16)
           | (quint32(quint8(bytes[offset + 3])) << 24);
}

static void putLe64(QByteArray &bytes, qsizetype offset, quint64 value)
{
    putLe32(bytes, offset, quint32(value));
    putLe32(bytes, offset + 4, quint32(value >> 32));
}

static quint32 testCrc32c(QByteArrayView bytes)
{
    quint32 crc = std::numeric_limits<quint32>::max();
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        const quint8 value = index >= 116 && index < 120 ? 0 : quint8(bytes[index]);
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 lowBitMask = quint32(0) - (crc & 1U);
            crc = (crc >> 1) ^ (0x82f63b78U & lowBitMask);
        }
    }
    return ~crc;
}

static void refreshEcfgEnvelope(QByteArray &configuration)
{
    const quint32 payloadOffset = testReadLe32(configuration, 108);
    const quint32 payloadBytes = testReadLe32(configuration, 112);
    const QByteArray payloadSha256 = QCryptographicHash::hash(
        QByteArrayView(configuration).sliced(payloadOffset, payloadBytes),
        QCryptographicHash::Sha256);
    configuration.replace(68, payloadSha256.size(), payloadSha256);
    putLe32(configuration, 116, testCrc32c(configuration));
}

static void refreshResourceTableSection(
    QByteArray &configuration, const EcfgSectionDescriptor &section)
{
    const qsizetype recordsOffset = section.offset + 80;
    const qsizetype recordsBytes = section.length - 80;
    const QByteArray recordsSha256 = QCryptographicHash::hash(
        QByteArrayView(configuration).sliced(recordsOffset, recordsBytes),
        QCryptographicHash::Sha256);
    configuration.replace(section.offset + 40, recordsSha256.size(), recordsSha256);
    quint64 catalogRevision = 0;
    for (int index = 0; index < 8; ++index)
        catalogRevision |= quint64(quint8(recordsSha256[index])) << (index * 8);
    putLe64(configuration, section.offset + 16, catalogRevision ? catalogRevision : 1);
}

static void refreshOutputPolicySection(
    QByteArray &configuration, const EcfgSectionDescriptor &section)
{
    const qsizetype recordsOffset = section.offset + 64;
    const qsizetype recordsBytes = section.length - 64;
    const QByteArray recordsSha256 = QCryptographicHash::hash(
        QByteArrayView(configuration).sliced(recordsOffset, recordsBytes),
        QCryptographicHash::Sha256);
    configuration.replace(section.offset + 24, recordsSha256.size(), recordsSha256);
}

} // namespace

void EtherCATSemanticRuntimeTests::testEcfgTransferredConfigurations()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");

    struct Fixture
    {
        QString directory;
        QString packageName;
        quint16 formatMinor = 0;
        QByteArray configurationSha256;
        quint64 catalogRevision = 0;
        qsizetype policyCount = 0;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            1,
            fromHex("3df29b74313a43678d751fb5646c7cac1f851f518c95bab64aee2c5edda9ac71"),
            0x0dea3816a0a0d7afULL,
            0,
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            2,
            fromHex("0e7f51d93400224bc1dfc362359525815222e78c44e83bc6794922c26702c99d"),
            0xc84fe35be276b2a8ULL,
            1,
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QByteArray packageBytes
            = readFile(directory.absoluteFilePath(fixture.packageName));
        if (packageBytes.isEmpty())
            continue;
        foundFixture = true;

        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        const Utils::Result<VerifiedSignedEcpkgManifest> manifest
            = verifySignedEcpkgManifest(
                *container, {{publicKey, EcpkgTrustClass::Production}});
        QVERIFY_RESULT(manifest);
        const Utils::Result<EcfgConfiguration> configuration
            = parseStrictEcfgConfiguration(container->configurationEcfg);
        QVERIFY_RESULT(configuration);

        QCOMPARE(configuration->formatMajor, quint16(1));
        QCOMPARE(configuration->formatMinor, fixture.formatMinor);
        QCOMPARE(configuration->configurationId, quint64(3501));
        QCOMPARE(configuration->configurationSha256, fixture.configurationSha256);
        QCOMPARE(configuration->configurationSha256, manifest->configuration.sha256);
        QCOMPARE(quint32(container->configurationEcfg.size()), manifest->configuration.bytes);
        QCOMPARE(configuration->processInputBits, quint32(480));
        QCOMPARE(configuration->processOutputBits, quint32(400));
        QCOMPARE(configuration->catalogRevision, fixture.catalogRevision);
        QCOMPARE(configuration->topologyIdentity, quint64(0x2ee7c79bc774840cULL));
        QCOMPARE(configuration->resources.size(), qsizetype(56));
        QCOMPARE(configuration->outputPolicies.size(), fixture.policyCount);
        QVERIFY(manifest->semanticBinding.has_value());
        QCOMPARE(
            configuration->resourceRecordsSha256,
            manifest->semanticBinding->resourceRecordsSha256);
        QCOMPARE(
            configuration->resourceTableSectionSha256,
            manifest->semanticBinding->resourceSectionSha256);
        QCOMPARE(
            configuration->catalogRevision, manifest->semanticBinding->catalogRevision);
        QCOMPARE(
            configuration->topologyIdentity, manifest->semanticBinding->topologyIdentity);

        if (fixture.policyCount == 1) {
            const EcfgOutputGroupPolicy &policy = configuration->outputPolicies.constFirst();
            QCOMPARE(policy.consistencyGroupId, quint32(13825));
            QCOMPARE(policy.flags, quint32(1));
            QCOMPARE(policy.recoveryPolicy, EcfgOutputRecoveryPolicy::ReturnTask);
            QCOMPARE(policy.maximumTtlCycles, quint32(1000));
            QCOMPARE(policy.resourceCount, quint32(2));
            QCOMPARE(policy.resourceIds.size(), qsizetype(2));
        }
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECFG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testEcfgRejectsDeepMutations()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api036"));
    const QByteArray original
        = readFile(fixtureDirectory.absoluteFilePath("configuration.ecfg"));
    if (original.isEmpty())
        QSKIP("Transferred API-036 ECFG fixture is not present");

    const Utils::Result<EcfgConfiguration> parsed = parseStrictEcfgConfiguration(original);
    QVERIFY_RESULT(parsed);
    QCOMPARE(parsed->sections.size(), qsizetype(8));

    QByteArray truncated = original;
    truncated.chop(1);
    QVERIFY(!parseStrictEcfgConfiguration(truncated));
    QVERIFY(!parseStrictEcfgConfiguration(original, original.size() - 1));

    QByteArray badCrc = original;
    badCrc[130] ^= 1;
    QVERIFY(!parseStrictEcfgConfiguration(badCrc));

    QByteArray reservedHeader = original;
    reservedHeader[120] = 1;
    refreshEcfgEnvelope(reservedHeader);
    QVERIFY(!parseStrictEcfgConfiguration(reservedHeader));

    QByteArray unknownSection = original;
    putLe16(unknownSection, 128 + 7 * 16, 9);
    refreshEcfgEnvelope(unknownSection);
    QVERIFY(!parseStrictEcfgConfiguration(unknownSection));

    const EcfgSectionDescriptor resourceSection = parsed->sections.at(6);
    QByteArray duplicateResource = original;
    const qsizetype firstResource = resourceSection.offset + 80;
    const qsizetype secondResource = firstResource + 64;
    duplicateResource.replace(
        secondResource, 8, QByteArrayView(duplicateResource).sliced(firstResource, 8));
    refreshResourceTableSection(duplicateResource, resourceSection);
    refreshEcfgEnvelope(duplicateResource);
    QVERIFY(!parseStrictEcfgConfiguration(duplicateResource));

    const EcfgSectionDescriptor policySection = parsed->sections.at(7);
    QByteArray reservedPolicy = original;
    reservedPolicy[policySection.offset + 64 + 20] = 1;
    refreshOutputPolicySection(reservedPolicy, policySection);
    refreshEcfgEnvelope(reservedPolicy);
    QVERIFY(!parseStrictEcfgConfiguration(reservedPolicy));

    QByteArray wrongGroupDigest = original;
    wrongGroupDigest[policySection.offset + 64 + 32] ^= 1;
    refreshOutputPolicySection(wrongGroupDigest, policySection);
    refreshEcfgEnvelope(wrongGroupDigest);
    QVERIFY(!parseStrictEcfgConfiguration(wrongGroupDigest));
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerCanonicalMinimal()
{
    const std::array<QByteArray, 6> payloads = canonicalTestPayloads();
    const CanonicalTestEcpkg package = buildCanonicalTestEcpkg(payloads);

    const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(package.wire);
    QVERIFY_RESULT(parsed);
    QCOMPARE(
        parsed->packageSha256, QCryptographicHash::hash(package.wire, QCryptographicHash::Sha256));
    QCOMPARE(parsed->manifestJson, payloads[0]);
    QCOMPARE(parsed->capabilityBin, payloads[1]);
    QCOMPARE(parsed->configurationEcfg, payloads[2]);
    QCOMPARE(parsed->runtimeErun, payloads[3]);
    QCOMPARE(parsed->compileReportJson, payloads[4]);
    QCOMPARE(parsed->manifestSignature, payloads[5]);

    const Utils::Result<EcpkgContainer> explicitLimit
        = parseCanonicalEcpkgContainer(package.wire, package.wire.size());
    QVERIFY_RESULT(explicitLimit);
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerRejectsMetadataMutations()
{
    const CanonicalTestEcpkg package = buildCanonicalTestEcpkg();

    const auto rejectMutation = [&package](const char *description, const auto &mutate) {
        QByteArray altered = package.wire;
        mutate(altered);
        const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(altered);
        QVERIFY2(!parsed, description);
    };

    const qsizetype local = package.localOffsets[0];
    rejectMutation("local magic", [local](QByteArray &bytes) { putLe32(bytes, local, 0x04034b51); });
    rejectMutation("local version needed", [local](QByteArray &bytes) {
        putLe16(bytes, local + 4, 21);
    });
    rejectMutation("local flags/data descriptor", [local](QByteArray &bytes) {
        putLe16(bytes, local + 6, 0x0008);
    });
    rejectMutation("local compression method", [local](QByteArray &bytes) {
        putLe16(bytes, local + 8, 8);
    });
    rejectMutation("local DOS time", [local](QByteArray &bytes) { putLe16(bytes, local + 10, 1); });
    rejectMutation("local DOS date", [local](QByteArray &bytes) { putLe16(bytes, local + 12, 34); });
    rejectMutation("local CRC", [local](QByteArray &bytes) { putLe32(bytes, local + 14, 1); });
    rejectMutation("local compressed size", [local](QByteArray &bytes) {
        putLe32(bytes, local + 18, 1);
    });
    rejectMutation("local uncompressed size", [local](QByteArray &bytes) {
        putLe32(bytes, local + 22, 1);
    });
    rejectMutation("local name length", [local](QByteArray &bytes) {
        putLe16(bytes, local + 26, 1);
    });
    rejectMutation("local extra length", [local](QByteArray &bytes) {
        putLe16(bytes, local + 28, 1);
    });
    rejectMutation("local name", [local](QByteArray &bytes) { bytes[local + 30] ^= 1; });

    const qsizetype central = package.centralOffsets[0];
    rejectMutation("central magic", [central](QByteArray &bytes) {
        putLe32(bytes, central, 0x02014b51);
    });
    rejectMutation("central version made by", [central](QByteArray &bytes) {
        putLe16(bytes, central + 4, 0x0014);
    });
    rejectMutation("central version needed", [central](QByteArray &bytes) {
        putLe16(bytes, central + 6, 21);
    });
    rejectMutation("central flags", [central](QByteArray &bytes) {
        putLe16(bytes, central + 8, 1);
    });
    rejectMutation("central compression method", [central](QByteArray &bytes) {
        putLe16(bytes, central + 10, 8);
    });
    rejectMutation("central DOS time", [central](QByteArray &bytes) {
        putLe16(bytes, central + 12, 1);
    });
    rejectMutation("central DOS date", [central](QByteArray &bytes) {
        putLe16(bytes, central + 14, 34);
    });
    rejectMutation("central CRC", [central](QByteArray &bytes) { putLe32(bytes, central + 16, 1); });
    rejectMutation("central compressed size", [central](QByteArray &bytes) {
        putLe32(bytes, central + 20, 1);
    });
    rejectMutation("central uncompressed size", [central](QByteArray &bytes) {
        putLe32(bytes, central + 24, 1);
    });
    rejectMutation("central name length", [central](QByteArray &bytes) {
        putLe16(bytes, central + 28, 1);
    });
    rejectMutation("central extra length", [central](QByteArray &bytes) {
        putLe16(bytes, central + 30, 1);
    });
    rejectMutation("central comment length", [central](QByteArray &bytes) {
        putLe16(bytes, central + 32, 1);
    });
    rejectMutation("central disk", [central](QByteArray &bytes) {
        putLe16(bytes, central + 34, 1);
    });
    rejectMutation("central internal attributes", [central](QByteArray &bytes) {
        putLe16(bytes, central + 36, 1);
    });
    rejectMutation("central external attributes", [central](QByteArray &bytes) {
        putLe32(bytes, central + 38, 0x81ed0000);
    });
    rejectMutation("central local offset", [central](QByteArray &bytes) {
        putLe32(bytes, central + 42, 1);
    });
    rejectMutation("central name", [central](QByteArray &bytes) { bytes[central + 46] ^= 1; });

    const qsizetype eocd = package.eocdOffset;
    rejectMutation("EOCD magic", [eocd](QByteArray &bytes) { putLe32(bytes, eocd, 0x06054b51); });
    rejectMutation("EOCD disk", [eocd](QByteArray &bytes) { putLe16(bytes, eocd + 4, 1); });
    rejectMutation("EOCD central disk", [eocd](QByteArray &bytes) { putLe16(bytes, eocd + 6, 1); });
    rejectMutation("EOCD disk entry count", [eocd](QByteArray &bytes) {
        putLe16(bytes, eocd + 8, 5);
    });
    rejectMutation("EOCD total entry count", [eocd](QByteArray &bytes) {
        putLe16(bytes, eocd + 10, 5);
    });
    rejectMutation("EOCD central size", [eocd](QByteArray &bytes) { putLe32(bytes, eocd + 12, 1); });
    rejectMutation("EOCD central offset", [eocd](QByteArray &bytes) {
        putLe32(bytes, eocd + 16, 1);
    });
    rejectMutation("EOCD archive comment", [eocd](QByteArray &bytes) {
        putLe16(bytes, eocd + 20, 1);
    });

    rejectMutation("payload CRC", [&package](QByteArray &bytes) {
        bytes[package.dataOffsets[0]] ^= 1;
    });
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerRejectsLayoutsAndLimits()
{
    const CanonicalTestEcpkg package = buildCanonicalTestEcpkg();
    const auto reject = [](const QByteArray &wire, const char *description) {
        const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(wire);
        QVERIFY2(!parsed, description);
    };

    std::array<QByteArray, 6> reorderedNames = canonicalTestNames();
    std::swap(reorderedNames[0], reorderedNames[1]);
    reject(buildCanonicalTestEcpkg(canonicalTestPayloads(), reorderedNames).wire, "entry order");

    std::array<QByteArray, 6> duplicateNames = canonicalTestNames();
    duplicateNames[1] = duplicateNames[0];
    reject(buildCanonicalTestEcpkg(canonicalTestPayloads(), duplicateNames).wire, "duplicate entry");

    std::array<QByteArray, 6> unsafeNames = canonicalTestNames();
    unsafeNames[0] = "../manifest.json";
    reject(buildCanonicalTestEcpkg(canonicalTestPayloads(), unsafeNames).wire, "zip slip name");

    QByteArray prefix = package.wire;
    prefix.prepend('\0');
    for (std::size_t index = 0; index < package.centralOffsets.size(); ++index) {
        putLe32(
            prefix,
            package.centralOffsets[index] + 1 + 42,
            quint32(package.localOffsets[index] + 1));
    }
    putLe32(prefix, package.eocdOffset + 1 + 16, quint32(package.centralOffset + 1));
    reject(prefix, "leading bytes");

    QByteArray gap = package.wire;
    gap.insert(package.centralOffset, '\0');
    putLe32(gap, package.eocdOffset + 1 + 16, quint32(package.centralOffset + 1));
    reject(gap, "gap before central directory");

    QByteArray overlap = package.wire;
    putLe32(overlap, package.centralOffsets[1] + 42, quint32(package.localOffsets[0]));
    reject(overlap, "overlapping local entries");

    QByteArray trailing = package.wire;
    trailing.append('\0');
    reject(trailing, "trailing bytes");

    QByteArray zip64Size = package.wire;
    putLe32(zip64Size, package.centralOffsets[0] + 20, 0xffffffff);
    putLe32(zip64Size, package.centralOffsets[0] + 24, 0xffffffff);
    reject(zip64Size, "ZIP64 size marker");

    QByteArray zip64Offset = package.wire;
    putLe32(zip64Offset, package.centralOffsets[0] + 42, 0xffffffff);
    reject(zip64Offset, "ZIP64 offset marker");

    QByteArray zip64Count = package.wire;
    putLe16(zip64Count, package.eocdOffset + 8, 0xffff);
    putLe16(zip64Count, package.eocdOffset + 10, 0xffff);
    reject(zip64Count, "ZIP64 count marker");

    const std::array<qsizetype, 9> truncationPoints{
        0,
        1,
        21,
        package.localOffsets[0] + 29,
        package.dataOffsets[0],
        package.centralOffset,
        package.centralOffsets[0] + 45,
        package.eocdOffset,
        package.wire.size() - 1,
    };
    for (qsizetype bytes : truncationPoints)
        reject(package.wire.first(bytes), "truncated package");

    QVERIFY(!parseCanonicalEcpkgContainer(package.wire, 0));
    QVERIFY(!parseCanonicalEcpkgContainer(package.wire, -1));
    QVERIFY(!parseCanonicalEcpkgContainer(package.wire, package.wire.size() - 1));
    const QByteArray tooLarge(defaultMaximumEcpkgContainerBytes + 1, '\0');
    QVERIFY(!parseCanonicalEcpkgContainer(tooLarge));
    QVERIFY(!parseCanonicalEcpkgContainer(tooLarge, tooLarge.size()));

    const std::array<qsizetype, 5> entryLimits{
        1024 * 1024,
        2 * 1024 * 1024,
        2 * 1024 * 1024,
        2 * 1024 * 1024,
        8 * 1024 * 1024,
    };
    for (std::size_t index = 0; index < entryLimits.size(); ++index) {
        std::array<QByteArray, 6> payloads = canonicalTestPayloads();
        payloads[index] = QByteArray(entryLimits[index] + 1, char(index + 1));
        reject(buildCanonicalTestEcpkg(payloads).wire, "entry size limit");
    }

    std::array<QByteArray, 6> maximumPayloads = canonicalTestPayloads();
    for (std::size_t index = 0; index < entryLimits.size(); ++index)
        maximumPayloads[index] = QByteArray(entryLimits[index], char(index + 1));
    const Utils::Result<EcpkgContainer> maximumEntries = parseCanonicalEcpkgContainer(
        buildCanonicalTestEcpkg(maximumPayloads).wire);
    QVERIFY_RESULT(maximumEntries);

    std::array<QByteArray, 6> shortSignature = canonicalTestPayloads();
    shortSignature[5].chop(1);
    reject(buildCanonicalTestEcpkg(shortSignature).wire, "short signature");

    std::array<QByteArray, 6> longSignature = canonicalTestPayloads();
    longSignature[5].append('\0');
    reject(buildCanonicalTestEcpkg(longSignature).wire, "long signature");

    for (std::size_t index = 0; index < canonicalTestEntryNames.size(); ++index) {
        std::array<QByteArray, 6> payloads = canonicalTestPayloads();
        payloads[index].clear();
        reject(buildCanonicalTestEcpkg(payloads).wire, "empty entry");
    }
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QString fixtureRoot = repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff");

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray packageSha256;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            QByteArray::fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee"),
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            QByteArray::fromHex("40222de1f5156556117ade48922ea2e2ed246ba0a51a3caa871131803987980b"),
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(QDir(fixtureRoot).absoluteFilePath(fixture.directory));
        const QString packagePath = directory.absoluteFilePath(fixture.packageName);
        if (!QFileInfo::exists(packagePath))
            continue;
        foundFixture = true;

        const QByteArray package = readFile(packagePath);
        QVERIFY2(!package.isEmpty(), qPrintable(packagePath));
        const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(package);
        QVERIFY_RESULT(parsed);
        QCOMPARE(parsed->packageSha256, fixture.packageSha256);
        QCOMPARE(parsed->manifestJson, readFile(directory.absoluteFilePath("manifest.json")));
        QCOMPARE(
            parsed->configurationEcfg, readFile(directory.absoluteFilePath("configuration.ecfg")));
        QCOMPARE(
            parsed->compileReportJson, readFile(directory.absoluteFilePath("compile_report.json")));
        QCOMPARE(parsed->manifestSignature, readFile(directory.absoluteFilePath("manifest.sig")));
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testEd25519Rfc8032()
{
    const QByteArray publicKey = fromHex(
        "d75a980182b10ab7d54bfed3c964073a"
        "0ee172f3daa62325af021a68f707511a");
    const QByteArray signature = fromHex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46b"
        "d25bf5f0595bbe24655141438e7a100b");

    QVERIFY(verifyEd25519DetachedSignature(publicKey, signature, QByteArrayView()));

    const QByteArray oneBytePublicKey = fromHex(
        "3d4017c3e843895a92b70aa74d1b7ebc"
        "9c982ccf2ec4968cc0cd55f12af4660c");
    const QByteArray oneByteSignature = fromHex(
        "92a009a9f0d4cab8720e820b5f642540"
        "a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c"
        "387b2eaeb4302aeeb00d291612bb0c00");
    const QByteArray oneByteMessage = fromHex("72");

    QVERIFY(verifyEd25519DetachedSignature(oneBytePublicKey, oneByteSignature, oneByteMessage));
}

void EtherCATSemanticRuntimeTests::testEd25519RejectsInvalidInputs()
{
    const QByteArray publicKey = fromHex(
        "d75a980182b10ab7d54bfed3c964073a"
        "0ee172f3daa62325af021a68f707511a");
    const QByteArray signature = fromHex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46b"
        "d25bf5f0595bbe24655141438e7a100b");

    QByteArray wrongPublicKey = publicKey;
    wrongPublicKey[0] ^= 1;
    QVERIFY(!verifyEd25519DetachedSignature(wrongPublicKey, signature, QByteArrayView()));

    QByteArray wrongSignature = signature;
    wrongSignature[0] ^= 1;
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, wrongSignature, QByteArrayView()));
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, signature, QByteArray("x")));

    QVERIFY(!verifyEd25519DetachedSignature(publicKey.first(31), signature, QByteArrayView()));
    QByteArray oversizedPublicKey = publicKey;
    oversizedPublicKey.append('\0');
    QVERIFY(!verifyEd25519DetachedSignature(oversizedPublicKey, signature, QByteArrayView()));
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, signature.first(63), QByteArrayView()));
    QByteArray oversizedSignature = signature;
    oversizedSignature.append('\0');
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, oversizedSignature, QByteArrayView()));

    const QByteArray nonCanonicalSignature = fromHex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "4c8c7872aa064e049dbb3013fbf29380"
        "d25bf5f0595bbe24655141438e7a101b");
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, nonCanonicalSignature, QByteArrayView()));
}

void EtherCATSemanticRuntimeTests::testEd25519TransferredManifests()
{
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QByteArray api035Signature = fromHex(
        "847ec27182b17f0db3b77d8aba662df2"
        "032e06075fe57e1738490707de63e62e2"
        "faafd22d696f25e1d078e59033d12844"
        "27e0a0bf63e6e7be659060ea48bf509");
    const QByteArray api036Signature = fromHex(
        "f3e6c1506b144b0ac47e20c0a6d92ad"
        "bf2eb60a9245d7c851f7abd5d92d72808"
        "afd7b953fe43680ae0f574b47b14053a"
        "5020b4e37b1b1d14fcc325f5d021db0b");
    const QByteArray api035Manifest = readTestData("testdata/api035-manifest.json");
    const QByteArray api036Manifest = readTestData("testdata/api036-manifest.json");

    QCOMPARE(
        QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex(),
        QByteArray("eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6"));
    QCOMPARE(
        QCryptographicHash::hash(api035Signature, QCryptographicHash::Sha256).toHex(),
        QByteArray("921c5ab1c0dfd1744c99dd84c4717ecb863d4c673059dbd042d9119054e3b708"));
    QCOMPARE(
        QCryptographicHash::hash(api036Signature, QCryptographicHash::Sha256).toHex(),
        QByteArray("dd25d7f2ebdecb7f97b541510f2e0ca29b368715e624408f4affa8698d09fee9"));
    QCOMPARE(api035Manifest.size(), 4327);
    QCOMPARE(
        QCryptographicHash::hash(api035Manifest, QCryptographicHash::Sha256).toHex(),
        QByteArray("f82dcf1bd1188b1eb4648396f946b8db5828ebf15d0c22fcc50fa2d14a6de4c0"));
    QVERIFY(verifyEd25519DetachedSignature(publicKey, api035Signature, api035Manifest));

    QCOMPARE(api036Manifest.size(), 4328);
    QCOMPARE(
        QCryptographicHash::hash(api036Manifest, QCryptographicHash::Sha256).toHex(),
        QByteArray("9eb3fcc112dfa5e52d286eb5e257a75f9e81d4fb0ace64ea4efdba9c2c680dcf"));
    QVERIFY(verifyEd25519DetachedSignature(publicKey, api036Signature, api036Manifest));

    QByteArray alteredManifest = api036Manifest;
    alteredManifest[0] ^= 1;
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, api036Signature, alteredManifest));
}

void EtherCATSemanticRuntimeTests::testSignedEcpkgTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> productionTrust{
        {publicKey, EcpkgTrustClass::Production},
    };

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray packageSha256;
        QByteArray manifestSha256;
        QByteArray mappingSha256;
        QByteArray projectSha256;
        quint64 catalogRevision = 0;
        quint64 topologyIdentity = 0;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee"),
            fromHex("f82dcf1bd1188b1eb4648396f946b8db5828ebf15d0c22fcc50fa2d14a6de4c0"),
            fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
            fromHex("3f649afb59281629bea3729d6ac9a23086d612537b73327ad04d3131e69fb8ea"),
            0x0dea3816a0a0d7afULL,
            0x2ee7c79bc774840cULL,
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            fromHex("40222de1f5156556117ade48922ea2e2ed246ba0a51a3caa871131803987980b"),
            fromHex("9eb3fcc112dfa5e52d286eb5e257a75f9e81d4fb0ace64ea4efdba9c2c680dcf"),
            fromHex("d4143cbbae9ac312181b12210d107db46957fb3b84a2d7d8082e929e96c26f2e"),
            fromHex("71aca9908f319acafb31fbf46b60c2e7c5e66e34dbeaf2804ab125d363384ac0"),
            0xc84fe35be276b2a8ULL,
            0x2ee7c79bc774840cULL,
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QString packagePath = directory.absoluteFilePath(fixture.packageName);
        if (!QFileInfo::exists(packagePath))
            continue;
        foundFixture = true;

        const QByteArray packageBytes = readFile(packagePath);
        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        const Utils::Result<VerifiedSignedEcpkgManifest> verified
            = verifySignedEcpkgManifest(*container, productionTrust);
        QVERIFY_RESULT(verified);
        QCOMPARE(verified->trust, EcpkgTrustClass::Production);
        QCOMPARE(verified->configurationId, quint64(3501));
        QCOMPARE(verified->packageSha256, fixture.packageSha256);
        QCOMPARE(verified->manifestSha256, fixture.manifestSha256);
        QCOMPARE(
            verified->signingKeyIdSha256,
            fromHex("eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6"));
        QCOMPARE(verified->compiledProjectSource.sha256, fixture.projectSha256);
        QVERIFY(verified->semanticBinding.has_value());
        QCOMPARE(verified->semanticBinding->formatVersion, quint16(1));
        QCOMPARE(verified->semanticBinding->bindingCount, quint32(56));
        QCOMPARE(verified->semanticBinding->catalogRevision, fixture.catalogRevision);
        QCOMPARE(verified->semanticBinding->topologyIdentity, fixture.topologyIdentity);
        QCOMPARE(verified->semanticBinding->mappingSha256, fixture.mappingSha256);

        const QByteArray projectBytes
            = readFile(directory.absoluteFilePath("project.json"));
        QVERIFY_RESULT(verifyEcpkgCompiledProjectSource(*verified, projectBytes));
        QByteArray alteredProject = projectBytes;
        alteredProject[0] ^= 1;
        QVERIFY(!verifyEcpkgCompiledProjectSource(*verified, alteredProject));

        const QList<EcpkgTrustedPublicKey> wrongTrust{
            {publicKey, EcpkgTrustClass::Engineering},
        };
        QVERIFY(!verifySignedEcpkgManifest(*container, wrongTrust));

        QByteArray wrongKey = publicKey;
        wrongKey[0] ^= 1;
        QVERIFY(!verifySignedEcpkgManifest(
            *container, {{wrongKey, EcpkgTrustClass::Production}}));

        EcpkgContainer alteredSignature = *container;
        alteredSignature.manifestSignature[0] ^= 1;
        QVERIFY(!verifySignedEcpkgManifest(alteredSignature, productionTrust));

        EcpkgContainer alteredPayload = *container;
        alteredPayload.compileReportJson[0] ^= 1;
        QVERIFY(!verifySignedEcpkgManifest(alteredPayload, productionTrust));

        EcpkgContainer unknownField = *container;
        unknownField.manifestJson.replace(
            QByteArray("{\"compiler\":"),
            QByteArray("{\"additional\":0,\"compiler\":"));
        QVERIFY(!verifySignedEcpkgManifest(unknownField, productionTrust));
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testProductionTrustStore()
{
    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-production-trust"));
    QVERIFY(temporary.isValid());
    const QString trustDirectory = QDir(temporary.path()).filePath("trust");
    QVERIFY(QDir().mkpath(trustDirectory));

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QByteArray keyId
        = QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256);
    QCOMPARE(
        keyId.toHex(),
        QByteArray("eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6"));

    QFile keyFile(
        QDir(trustDirectory).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"));
    QVERIFY(keyFile.open(QIODevice::WriteOnly));
    QCOMPARE(keyFile.write(publicKey), qint64(publicKey.size()));
    keyFile.close();

    const Utils::Result<QList<EcpkgTrustedPublicKey>> trust
        = loadProductionEcpkgTrustStore(trustDirectory);
    QVERIFY_RESULT(trust);
    QCOMPARE(trust->size(), qsizetype(1));
    QCOMPARE(trust->constFirst().rawPublicKey, publicKey);
    QCOMPARE(trust->constFirst().trust, EcpkgTrustClass::Production);

    const QDir fixtureDirectory(
        QDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR))
            .absoluteFilePath("../../../build/vendor_api_036_handoff/api036"));
    const QByteArray packageBytes = readFile(
        fixtureDirectory.absoluteFilePath(
            "three-slave-output-transaction-cfg3501.ecpkg"));
    if (!packageBytes.isEmpty()) {
        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        QVERIFY_RESULT(verifySignedEcpkgManifest(*container, *trust));
    }
}

void EtherCATSemanticRuntimeTests::testProductionTrustStoreRejectsUnsafeInputs()
{
    QVERIFY(!loadProductionEcpkgTrustStore(QString::fromLatin1("relative/trust")));

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QByteArray keyId
        = QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256);
    const auto writeFile = [](const QString &path, QByteArrayView bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly)
               && file.write(bytes.data(), bytes.size()) == bytes.size();
    };

    QTemporaryDir empty(systemTemporaryDirectoryTemplate(u"embed-labs-empty-trust"));
    QVERIFY(empty.isValid());
    QVERIFY(!loadProductionEcpkgTrustStore(empty.path()));

    QTemporaryDir wrongName(
        systemTemporaryDirectoryTemplate(u"embed-labs-wrong-trust"));
    QVERIFY(wrongName.isValid());
    QVERIFY(writeFile(
        QDir(wrongName.path()).filePath(QString(64, '0') + ".pub"), publicKey));
    QVERIFY(!loadProductionEcpkgTrustStore(wrongName.path()));

    QTemporaryDir wrongSize(
        systemTemporaryDirectoryTemplate(u"embed-labs-short-trust"));
    QVERIFY(wrongSize.isValid());
    QVERIFY(writeFile(
        QDir(wrongSize.path()).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"),
        publicKey.first(31)));
    QVERIFY(!loadProductionEcpkgTrustStore(wrongSize.path()));

    QTemporaryDir unexpected(
        systemTemporaryDirectoryTemplate(u"embed-labs-extra-trust"));
    QVERIFY(unexpected.isValid());
    QVERIFY(writeFile(
        QDir(unexpected.path()).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"),
        publicKey));
    QVERIFY(writeFile(QDir(unexpected.path()).filePath(".hidden"), QByteArrayView("x", 1)));
    QVERIFY(!loadProductionEcpkgTrustStore(unexpected.path()));

    QTemporaryDir tooMany(
        systemTemporaryDirectoryTemplate(u"embed-labs-many-trust"));
    QVERIFY(tooMany.isValid());
    for (qsizetype index = 0; index <= maximumEcpkgTrustedPublicKeys; ++index) {
        QByteArray key(32, '\0');
        key[0] = char(index);
        key[31] = char(index + 1);
        const QByteArray digest
            = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
        QVERIFY(writeFile(
            QDir(tooMany.path()).filePath(QString::fromLatin1(digest.toHex()) + ".pub"),
            key));
    }
    QVERIFY(!loadProductionEcpkgTrustStore(tooMany.path()));

    QTemporaryDir linked(
        systemTemporaryDirectoryTemplate(u"embed-labs-linked-trust"));
    QVERIFY(linked.isValid());
    const QString realDirectory = QDir(linked.path()).filePath("real");
    const QString linkedDirectory = QDir(linked.path()).filePath("linked");
    QVERIFY(QDir().mkpath(realDirectory));
    QVERIFY(writeFile(
        QDir(realDirectory).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"),
        publicKey));
    if (QFile::link(realDirectory, linkedDirectory))
        QVERIFY(!loadProductionEcpkgTrustStore(linkedDirectory));
}

void EtherCATSemanticRuntimeTests::testSemanticBindingTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray mappingSha256;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            fromHex("d4143cbbae9ac312181b12210d107db46957fb3b84a2d7d8082e929e96c26f2e"),
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QByteArray packageBytes
            = readFile(directory.absoluteFilePath(fixture.packageName));
        if (packageBytes.isEmpty())
            continue;
        foundFixture = true;

        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        const Utils::Result<VerifiedSignedEcpkgManifest> manifest
            = verifySignedEcpkgManifest(*container, trust);
        QVERIFY_RESULT(manifest);
        const Utils::Result<EcfgConfiguration> configuration
            = parseStrictEcfgConfiguration(container->configurationEcfg);
        QVERIFY_RESULT(configuration);
        const Utils::Result<VerifiedSemanticBindingArtifact> artifact
            = verifySemanticBindingArtifact(*container, *manifest, *configuration);
        QVERIFY_RESULT(artifact);

        QCOMPARE(artifact->trust, EcpkgTrustClass::Production);
        QCOMPARE(artifact->artifactSha256, fixture.mappingSha256);
        QCOMPARE(
            artifact->canonicalArtifact,
            readFile(directory.absoluteFilePath("semantic-binding-v1.json")));
        QCOMPARE(artifact->bindings.size(), qsizetype(56));
        QCOMPARE(artifact->topologyInstances.size(), qsizetype(3));
        QCOMPARE(artifact->configurationId, quint64(3501));
        QCOMPARE(artifact->catalogRevision, configuration->catalogRevision);
        QCOMPARE(artifact->topologyIdentity, configuration->topologyIdentity);

        const VerifiedSemanticBinding *axis0Controlword
            = artifact->findBySemanticSignalId(
                u"embedlabs:fixture:axis0:command:controlword");
        QVERIFY(axis0Controlword);
        QCOMPARE(axis0Controlword->resourceId, quint64(0x0997885c279b0862ULL));
        QCOMPARE(axis0Controlword->componentInstanceId, quint64(0xcbe141c7c6c9c773ULL));
        QCOMPARE(artifact->findByResourceId(axis0Controlword->resourceId), axis0Controlword);
        QVERIFY(!artifact->findBySemanticSignalId(u"embedlabs:fixture:missing"));
        QVERIFY(!artifact->findByResourceId(0));
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testSemanticBindingRejectsMismatches()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api035"));
    const QByteArray packageBytes = readFile(
        fixtureDirectory.absoluteFilePath(
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg"));
    if (packageBytes.isEmpty())
        QSKIP("Transferred API-035 ECPKG fixture is not present");

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const Utils::Result<EcpkgContainer> container
        = parseCanonicalEcpkgContainer(packageBytes);
    QVERIFY_RESULT(container);
    const Utils::Result<VerifiedSignedEcpkgManifest> manifest
        = verifySignedEcpkgManifest(
            *container, {{publicKey, EcpkgTrustClass::Production}});
    QVERIFY_RESULT(manifest);
    const Utils::Result<EcfgConfiguration> configuration
        = parseStrictEcfgConfiguration(container->configurationEcfg);
    QVERIFY_RESULT(configuration);
    QVERIFY_RESULT(verifySemanticBindingArtifact(*container, *manifest, *configuration));

    EcpkgContainer wrongPackage = *container;
    wrongPackage.packageSha256[0] ^= 1;
    QVERIFY(!verifySemanticBindingArtifact(wrongPackage, *manifest, *configuration));

    VerifiedSignedEcpkgManifest wrongMapping = *manifest;
    wrongMapping.semanticBinding->mappingSha256[0] ^= 1;
    QVERIFY(!verifySemanticBindingArtifact(*container, wrongMapping, *configuration));

    EcfgConfiguration missingResource = *configuration;
    missingResource.resources.removeLast();
    QVERIFY(!verifySemanticBindingArtifact(*container, *manifest, missingResource));

    EcfgConfiguration wrongResourceType = *configuration;
    wrongResourceType.resources[0].bitWidth += 1;
    QVERIFY(!verifySemanticBindingArtifact(*container, *manifest, wrongResourceType));

    EcpkgContainer changedReport = *container;
    changedReport.compileReportJson[0] ^= 1;
    QVERIFY(!verifySemanticBindingArtifact(changedReport, *manifest, *configuration));

    const Utils::Result<StrictJson> parsedReport
        = parseStrictJson(container->compileReportJson, container->compileReportJson.size());
    QVERIFY_RESULT(parsedReport);

    const auto rebuildReportEvidence =
        [&container, &manifest](StrictJson report) {
            VerifiedSignedEcpkgManifest rebuiltManifest = *manifest;
            const Utils::Result<QByteArray> canonicalArtifact = serializeCanonicalJson(
                report.at("semantic_binding_manifest"),
                defaultMaximumSemanticArtifactBytes);
            if (!canonicalArtifact)
                return std::optional<std::pair<EcpkgContainer, VerifiedSignedEcpkgManifest>>();
            const QByteArray mappingSha256 = QCryptographicHash::hash(
                *canonicalArtifact, QCryptographicHash::Sha256);
            report["semantic_binding_manifest_sha256"]
                = mappingSha256.toHex().toStdString();
            std::string reportText = report.dump(2);
            reportText.push_back('\n');

            EcpkgContainer rebuiltContainer = *container;
            rebuiltContainer.compileReportJson = QByteArray(
                reportText.data(), qsizetype(reportText.size()));
            rebuiltManifest.compileReport.bytes
                = quint32(rebuiltContainer.compileReportJson.size());
            rebuiltManifest.compileReport.sha256 = QCryptographicHash::hash(
                rebuiltContainer.compileReportJson, QCryptographicHash::Sha256);
            rebuiltManifest.semanticBinding->mappingSha256 = mappingSha256;
            return std::optional(std::pair(
                std::move(rebuiltContainer), std::move(rebuiltManifest)));
        };

    StrictJson wrongSymbol = *parsedReport;
    wrongSymbol["semantic_symbols"][0]["data_type"] = "s16";
    const auto wrongSymbolEvidence = rebuildReportEvidence(std::move(wrongSymbol));
    QVERIFY(wrongSymbolEvidence);
    QVERIFY(!verifySemanticBindingArtifact(
        wrongSymbolEvidence->first, wrongSymbolEvidence->second, *configuration));

    StrictJson duplicateBinding = *parsedReport;
    duplicateBinding["semantic_binding_manifest"]["bindings"][1]["semantic_signal_id"]
        = duplicateBinding["semantic_binding_manifest"]["bindings"][0]["semantic_signal_id"];
    const auto duplicateEvidence = rebuildReportEvidence(std::move(duplicateBinding));
    QVERIFY(duplicateEvidence);
    QVERIFY(!verifySemanticBindingArtifact(
        duplicateEvidence->first, duplicateEvidence->second, *configuration));

    StrictJson unknownField = *parsedReport;
    unknownField["semantic_binding_manifest"]["unexpected"] = 0;
    const auto unknownEvidence = rebuildReportEvidence(std::move(unknownField));
    QVERIFY(unknownEvidence);
    QVERIFY(!verifySemanticBindingArtifact(
        unknownEvidence->first, unknownEvidence->second, *configuration));
}

void EtherCATSemanticRuntimeTests::testSemanticBindingV2TransferredPackage()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath(
            "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath(
            "artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray canonicalArtifact = readFile(
        fixtureRoot.absoluteFilePath("artifacts/semantic-binding-v2.json"));
    const QByteArray projectBytes = readFile(
        fixtureRoot.absoluteFilePath("package_inputs/project.json"));
    const QByteArray publicKey = readFile(
        fixtureRoot.absoluteFilePath(
            "trust/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"));
    if (packageBytes.isEmpty() || canonicalArtifact.isEmpty() || projectBytes.isEmpty()
        || publicKey.isEmpty()) {
        QSKIP("Transferred API-037 ECPKG fixture is not present");
    }

    QCOMPARE(packageBytes.size(), qsizetype(347840));
    QCOMPARE(
        QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256),
        fromHex("1f0772f09dcb2e3b9e4f73e435b085b02e7e989e2a7cb39f7aa24bab95ef8814"));
    QCOMPARE(
        QCryptographicHash::hash(canonicalArtifact, QCryptographicHash::Sha256),
        fromHex("a24a040d96a876fd4ac2753219aaa071d9fbbfb0a7db80d3adcb83872ec1f6e2"));

    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkg(packageBytes, trust, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidence);
    QVERIFY(evidence->isValid());
    QVERIFY(!evidence->permitsWritableActions());
    QCOMPARE(package->configuration.cyclePeriodNs, quint32(125000));
    QCOMPARE(package->configuration.dcRecords.size(), qsizetype(2));

    const VerifiedSemanticBindingArtifact &artifact
        = evidence->semanticBindingArtifact();
    const Data::RuntimeSemanticMappingProof &proof
        = evidence->semanticMappingProof();
    QCOMPARE(artifact.formatVersion, quint16(2));
    QCOMPARE(artifact.configurationId, quint64(3701));
    QCOMPARE(artifact.catalogRevision, quint64(0x9ffaf9e73061d964ULL));
    QCOMPARE(artifact.topologyIdentity, quint64(0x5b37fe1904c0300dULL));
    QCOMPARE(artifact.canonicalArtifact, canonicalArtifact);
    QCOMPARE(artifact.bindings.size(), qsizetype(56));
    QCOMPARE(artifact.devices.size(), qsizetype(3));
    QCOMPARE(artifact.actions.size(), qsizetype(8));
    QVERIFY(artifact.permitsWritableActions());
    QVERIFY(proof.isValid());
    QCOMPARE(proof.formatVersion, quint16(2));
    QCOMPARE(proof.mappingSha256, artifact.artifactSha256);

    const VerifiedSemanticDevice *xb6
        = artifact.findDevice(u"embedlabs:project:device:xb6");
    QVERIFY(xb6);
    QCOMPARE(xb6->components.size(), qsizetype(2));
    QCOMPARE(xb6->adapterId, QStringLiteral("solidot.xb6_ec0002_rev1_do16"));
    QVERIFY(!artifact.findDevice(u"embedlabs:project:device:missing"));

    const VerifiedSemanticBinding *do0
        = artifact.findBySemanticSignalId(u"embedlabs:fixture:xb6:output:do0");
    QVERIFY(do0);
    QCOMPARE(do0->semanticSignalDefinitionId,
             QStringLiteral("org.embedlabs.solidot.xb6.slot.1.digital-output.channel.0"));
    QCOMPARE(do0->projectDeviceId, xb6->projectDeviceId);
    QCOMPARE(do0->componentBindingId,
             QStringLiteral("embedlabs:project:component:xb6:do16"));
    QCOMPARE(do0->resourceId, quint64(0xf768f3df0713bbfeULL));
    QCOMPARE(do0->consistencyGroupId, quint32(0xf0286f69U));
    QCOMPARE(do0->primitive, EcfgResourcePrimitive::Bool);
    QCOMPARE(do0->direction, EcfgResourceDirection::Output);
    QCOMPARE(do0->access, EcfgResourceAccess::ReadWrite);
    QVERIFY(do0->safeValueDeclared);
    QVERIFY(do0->safeValue.has_value());
    QVERIFY(std::holds_alternative<quint64>(*do0->safeValue));
    QCOMPARE(std::get<quint64>(*do0->safeValue), quint64(0));

    const VerifiedSemanticAction *setOutputs
        = artifact.findAction(u"embedlabs:project:action:xb6:set-outputs");
    const VerifiedSemanticAction *clearOutputs
        = artifact.findAction(u"embedlabs:project:action:xb6:clear-outputs");
    QVERIFY(setOutputs);
    QVERIFY(clearOutputs);
    QVERIFY(setOutputs->enabled);
    QCOMPARE(
        setOutputs->qualification, VerifiedSemanticActionQualification::Qualified);
    QVERIFY(!setOutputs->disabledReason);
    QVERIFY(!setOutputs->dcRequired);
    QCOMPARE(setOutputs->requiredBindings.size(), qsizetype(16));
    QCOMPARE(setOutputs->parameters.size(), qsizetype(16));
    QCOMPARE(setOutputs->consistencyGroups.size(), qsizetype(1));
    QCOMPARE(
        setOutputs->consistencyGroups.constFirst().consistencyGroupId,
        quint32(0xf0286f69U));
    QCOMPARE(
        setOutputs->consistencyGroups.constFirst().recoveryPolicy,
        EcfgOutputRecoveryPolicy::HoldSafe);
    QCOMPARE(setOutputs->consistencyGroups.constFirst().maximumTtlCycles, quint32(1000));
    QCOMPARE(setOutputs->steps.size(), qsizetype(1));
    QCOMPARE(
        setOutputs->steps.constFirst().kind,
        VerifiedSemanticActionStepKind::WriteGroup);
    QCOMPARE(setOutputs->steps.constFirst().assignments.size(), qsizetype(16));
    QVERIFY(clearOutputs->enabled);
    QCOMPARE(clearOutputs->parameters.size(), qsizetype(0));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:xb6:set-outputs", false));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:xb6:clear-outputs", false));

    const VerifiedSemanticAction *axis0Velocity
        = artifact.findAction(u"embedlabs:project:action:axis0:set-csv-velocity");
    const VerifiedSemanticAction *axis1Stop
        = artifact.findAction(u"embedlabs:project:action:axis1:stop-csv");
    QVERIFY(axis0Velocity);
    QVERIFY(axis1Stop);
    QVERIFY(!axis0Velocity->enabled);
    QVERIFY(!axis1Stop->enabled);
    QCOMPARE(
        axis0Velocity->qualification,
        VerifiedSemanticActionQualification::Unqualified);
    QVERIFY(axis0Velocity->disabledReason.has_value());
    QCOMPARE(
        *axis0Velocity->disabledReason,
        QStringLiteral("reference_unit_to_rpm_conversion_not_bound"));
    QVERIFY(axis0Velocity->dcRequired);
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:axis0:set-csv-velocity", false));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:axis0:set-csv-velocity", true));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:axis1:stop-csv", true));
    QVERIFY(!evidence->invocableAction(u"embedlabs:project:action:missing", true));
    QVERIFY(!artifact.findAction(u"embedlabs:project:action:missing"));
}

void EtherCATSemanticRuntimeTests::testSemanticBindingV2RejectsDeepMutations()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath(
            "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath(
            "artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray publicKey = readFile(
        fixtureRoot.absoluteFilePath(
            "trust/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"));
    if (packageBytes.isEmpty() || publicKey.isEmpty())
        QSKIP("Transferred API-037 ECPKG fixture is not present");

    const Utils::Result<EcpkgContainer> container
        = parseCanonicalEcpkgContainer(packageBytes);
    QVERIFY_RESULT(container);
    const Utils::Result<VerifiedSignedEcpkgManifest> manifest
        = verifySignedEcpkgManifest(
            *container, {{publicKey, EcpkgTrustClass::Production}});
    QVERIFY_RESULT(manifest);
    const Utils::Result<EcfgConfiguration> configuration
        = parseStrictEcfgConfiguration(container->configurationEcfg);
    QVERIFY_RESULT(configuration);
    QVERIFY_RESULT(verifySemanticBindingArtifact(*container, *manifest, *configuration));
    const Utils::Result<StrictJson> parsedReport
        = parseStrictJson(container->compileReportJson, container->compileReportJson.size());
    QVERIFY_RESULT(parsedReport);

    const auto rebuildReportEvidence =
        [&container, &manifest](StrictJson report) {
            VerifiedSignedEcpkgManifest rebuiltManifest = *manifest;
            const Utils::Result<QByteArray> canonicalArtifact = serializeCanonicalJson(
                report.at("semantic_binding_manifest"),
                defaultMaximumSemanticArtifactBytes);
            if (!canonicalArtifact)
                return std::optional<std::pair<EcpkgContainer, VerifiedSignedEcpkgManifest>>();
            const QByteArray mappingSha256 = QCryptographicHash::hash(
                *canonicalArtifact, QCryptographicHash::Sha256);
            report["semantic_binding_manifest_sha256"]
                = mappingSha256.toHex().toStdString();
            std::string reportText = report.dump(2);
            reportText.push_back('\n');

            EcpkgContainer rebuiltContainer = *container;
            rebuiltContainer.compileReportJson = QByteArray(
                reportText.data(), qsizetype(reportText.size()));
            rebuiltManifest.compileReport.bytes
                = quint32(rebuiltContainer.compileReportJson.size());
            rebuiltManifest.compileReport.sha256 = QCryptographicHash::hash(
                rebuiltContainer.compileReportJson, QCryptographicHash::Sha256);
            rebuiltManifest.semanticBinding->mappingSha256 = mappingSha256;
            return std::optional(std::pair(
                std::move(rebuiltContainer), std::move(rebuiltManifest)));
        };

    const auto rejects =
        [&rebuildReportEvidence, &configuration](StrictJson report) {
            const auto evidence = rebuildReportEvidence(std::move(report));
            return evidence
                   && !verifySemanticBindingArtifact(
                       evidence->first, evidence->second, *configuration);
        };

    StrictJson wrongProjectDevice = *parsedReport;
    wrongProjectDevice["semantic_binding_manifest"]["bindings"][0]["project_device_id"]
        = "embedlabs:project:device:axis1";
    QVERIFY(rejects(std::move(wrongProjectDevice)));

    StrictJson swappedDeviceInstances = *parsedReport;
    std::swap(
        swappedDeviceInstances["project_device_bindings"][0]["position"],
        swappedDeviceInstances["project_device_bindings"][1]["position"]);
    std::swap(
        swappedDeviceInstances["project_device_bindings"][0]["station_address"],
        swappedDeviceInstances["project_device_bindings"][1]["station_address"]);
    QVERIFY(rejects(std::move(swappedDeviceInstances)));

    StrictJson collidedDefinition = *parsedReport;
    collidedDefinition["semantic_binding_manifest"]["bindings"][1]
                      ["semantic_signal_definition_id"]
        = "org.embedlabs.inovance.sv630n.negative-torque-limit";
    collidedDefinition["semantic_symbols"][24]["semantic_definition_id"]
        = "org.embedlabs.inovance.sv630n.negative-torque-limit";
    QVERIFY(rejects(std::move(collidedDefinition)));

    StrictJson wrongSafeValue = *parsedReport;
    wrongSafeValue["semantic_binding_manifest"]["bindings"][38]["safe_value"] = 1;
    wrongSafeValue["semantic_symbols"][38]["safe_value"] = 1;
    QVERIFY(rejects(std::move(wrongSafeValue)));

    StrictJson enabledUnqualified = *parsedReport;
    enabledUnqualified["semantic_binding_manifest"]["actions"][0]["enabled"] = true;
    enabledUnqualified["semantic_action_plans"][0]["enabled"] = true;
    QVERIFY(rejects(std::move(enabledUnqualified)));

    StrictJson partialWriteGroup = *parsedReport;
    partialWriteGroup["semantic_binding_manifest"]["actions"][6]["steps"][0]["assignments"]
        .erase(
            partialWriteGroup["semantic_binding_manifest"]["actions"][6]["steps"][0]
                ["assignments"]
                    .begin());
    partialWriteGroup["semantic_action_plans"][6]["steps"][0]["assignments"].erase(
        partialWriteGroup["semantic_action_plans"][6]["steps"][0]["assignments"].begin());
    QVERIFY(rejects(std::move(partialWriteGroup)));

    StrictJson crossDeviceAction = *parsedReport;
    crossDeviceAction["semantic_binding_manifest"]["actions"][6]["required_bindings"][0]
        ["semantic_binding_id"] = "embedlabs:fixture:axis0:command:controlword";
    crossDeviceAction["semantic_action_plans"][6]["required_bindings"][0]
        ["semantic_binding_id"] = "embedlabs:fixture:axis0:command:controlword";
    QVERIFY(rejects(std::move(crossDeviceAction)));

    StrictJson staleFormat = *parsedReport;
    staleFormat["semantic_binding_manifest"]["format_version"] = 1;
    QVERIFY(rejects(std::move(staleFormat)));

    EcfgConfiguration missingDcRuntime = *configuration;
    missingDcRuntime.dcRecords.clear();
    QVERIFY(!verifySemanticBindingArtifact(*container, *manifest, missingDcRuntime));
}

void EtherCATSemanticRuntimeTests::testSemanticBindingV2ParserGuards()
{
    QList<VerifiedSemanticComponent> components{
        {
            QStringLiteral("component.a"),
            1,
            std::nullopt,
            0,
            0,
        },
        {
            QStringLiteral("component.b"),
            2,
            QStringLiteral("component.a"),
            1,
            0,
        },
        {
            QStringLiteral("component.c"),
            3,
            QStringLiteral("component.b"),
            2,
            0,
        },
    };
    QVERIFY(isAcyclicSemanticComponentParentGraph(components));

    // Mutate an otherwise valid three-level graph into a cycle that does not
    // rely on the parser's direct self-parent rejection.
    components[0].parentComponentBindingId = QStringLiteral("component.c");
    components[0].parentInstanceId = 3;
    QVERIFY(!isAcyclicSemanticComponentParentGraph(components));

    components[0].parentComponentBindingId.reset();
    components[0].parentInstanceId = 0;
    components[2].parentComponentBindingId = QStringLiteral("component.missing");
    QVERIFY(!isAcyclicSemanticComponentParentGraph(components));

    QVERIFY(isValidSemanticMaskedWaitCondition(0x0f, 0x05, 8));
    QVERIFY(isValidSemanticMaskedWaitCondition(
        quint64(1) << 63, quint64(1) << 63, 64));
    QVERIFY(!isValidSemanticMaskedWaitCondition(0, 0, 8));
    QVERIFY(!isValidSemanticMaskedWaitCondition(0x0f, 0x10, 8));
    QVERIFY(!isValidSemanticMaskedWaitCondition(0x10, 0, 4));
}

void EtherCATSemanticRuntimeTests::testSemanticActionDefinitionsProductionPackage()
{
    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    const QByteArray companionBytes = readTestData(
        "testdata/api038/semantic-action-definitions-v1.json");
    const QByteArray artifactBytes = readTestData(
        "testdata/api038/semantic-binding-v2.json");
    const QByteArray publicKey = readTestData(
        "testdata/api038/"
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    QVERIFY(!packageBytes.isEmpty());
    QVERIFY(!projectBytes.isEmpty());
    QVERIFY(!companionBytes.isEmpty());
    QVERIFY(!artifactBytes.isEmpty());
    QCOMPARE(publicKey.size(), qsizetype(32));
    QCOMPARE(packageBytes.size(), qsizetype(391667));
    QCOMPARE(
        QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256),
        fromHex("d0b8edd70b5ecec53252ac4b09dc9665ac82b91178d18e2d94222b9a538165d0"));
    QCOMPARE(
        QCryptographicHash::hash(companionBytes, QCryptographicHash::Sha256),
        fromHex("f056912152f5c97e8306c5391118938fce93aee6768c7d35af2350533c72c210"));
    QCOMPARE(
        QCryptographicHash::hash(artifactBytes, QCryptographicHash::Sha256),
        fromHex("a24a040d96a876fd4ac2753219aaa071d9fbbfb0a7db80d3adcb83872ec1f6e2"));

    const Utils::Result<EcpkgContainer> container
        = parseCanonicalEcpkgContainer(packageBytes);
    QVERIFY_RESULT(container);
    QCOMPARE(container->semanticActionDefinitionsJson, companionBytes);

    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const Utils::Result<VerifiedSignedEcpkgManifest> manifest
        = verifySignedEcpkgManifest(*container, trust);
    QVERIFY_RESULT(manifest);
    QCOMPARE(manifest->formatVersion, quint16(2));
    QCOMPARE(
        manifest->manifestSha256,
        fromHex("930da674dd20f2d6d14dc37a39eff3d163687d330cfd898b8f36769d334729f7"));
    QVERIFY(manifest->actionDefinitions.has_value());
    QVERIFY(manifest->semanticBinding.has_value());
    QCOMPARE(manifest->semanticBinding->actionDefinitionsFormatVersion, quint16(1));
    QCOMPARE(manifest->semanticBinding->actionDefinitionCount, quint32(5));
    QCOMPARE(
        manifest->semanticBinding->actionDefinitionsSha256,
        fromHex("f056912152f5c97e8306c5391118938fce93aee6768c7d35af2350533c72c210"));

    const Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkg(packageBytes, trust, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidence);
    QVERIFY(evidence->isValid());
    QVERIFY(evidence->actionDefinitions().has_value());
    QVERIFY(evidence->actionDefinitions()->isValid());
    QCOMPARE(evidence->actionDefinitions()->definitionCount, quint32(5));
    QCOMPARE(evidence->actionDefinitions()->actionCount, quint32(8));
    QCOMPARE(evidence->cyclePeriodNs(), quint32(125000));
    QVERIFY(evidence->permitsWritableActions());

    QVERIFY(evidence->invocableAction(
        u"embedlabs:project:action:xb6:set-outputs", false));
    QVERIFY(evidence->invocableAction(
        u"embedlabs:project:action:xb6:clear-outputs", false));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:axis0:prepare-csv", true));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:axis0:set-csv-velocity", true));
    QVERIFY(!evidence->invocableAction(
        u"embedlabs:project:action:axis1:stop-csv", true));
}

void EtherCATSemanticRuntimeTests::testSemanticActionDefinitionsRejectsMismatches()
{
    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    const QByteArray publicKey = readTestData(
        "testdata/api038/"
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkg(packageBytes, trust, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidence);

    const Utils::Result<StrictJson> originalCompanion = parseCanonicalJson(
        package->container.semanticActionDefinitionsJson,
        defaultMaximumSemanticActionDefinitionsBytes);
    const Utils::Result<StrictJson> originalReport = parseStrictJson(
        package->container.compileReportJson,
        defaultMaximumSemanticCompileReportBytes);
    QVERIFY_RESULT(originalCompanion);
    QVERIFY_RESULT(originalReport);

    const auto rebuild = [&package, &evidence, &originalReport](
                             StrictJson companion,
                             bool updateReport = true,
                             const VerifiedSemanticBindingArtifact *artifact
                             = nullptr) -> Utils::Result<VerifiedSemanticActionDefinitions> {
        const Utils::Result<QByteArray> canonical
            = serializeCanonicalJson(companion, defaultMaximumSemanticActionDefinitionsBytes);
        if (!canonical)
            return Utils::ResultError(canonical.error());

        EcpkgContainer container = package->container;
        container.semanticActionDefinitionsJson = *canonical;
        VerifiedSignedEcpkgManifest manifest = package->manifest;
        const QByteArray digest = QCryptographicHash::hash(*canonical, QCryptographicHash::Sha256);
        manifest.actionDefinitions->bytes = quint32(canonical->size());
        manifest.actionDefinitions->sha256 = digest;
        manifest.semanticBinding->actionDefinitionsSha256 = digest;
        const StrictJson &count = companion.at("definition_count");
        const quint64 definitionCount = count.is_number_unsigned() ? count.get<quint64>()
                                                                   : quint64(count.get<qint64>());
        manifest.semanticBinding->actionDefinitionCount = quint32(definitionCount);

        if (updateReport) {
            StrictJson report = *originalReport;
            report["semantic_action_definitions_manifest"] = companion;
            report["semantic_action_definitions_sha256"] = digest.toHex().toStdString();
            std::string reportText = report.dump(2);
            reportText.push_back('\n');
            container.compileReportJson
                = QByteArray(reportText.data(), qsizetype(reportText.size()));
        }
        return verifySemanticActionDefinitions(
            container, manifest, artifact ? *artifact : evidence->semanticBindingArtifact());
    };
    const auto resignDefinition = [](StrictJson &companion,
                                     std::size_t definitionIndex,
                                     VerifiedSemanticBindingArtifact *artifact) {
        StrictJson &record = companion["definitions"][definitionIndex];
        const Utils::Result<QByteArray> canonical = serializeCanonicalJson(
            record["definition"], defaultMaximumSemanticActionDefinitionsBytes);
        if (!canonical)
            return canonical;
        const QByteArray digest = QCryptographicHash::hash(*canonical, QCryptographicHash::Sha256);
        record["action_definition_sha256"] = digest.toHex().toStdString();
        const QString definitionId = QString::fromStdString(
            record["action_definition_id"].get<std::string>());
        for (VerifiedSemanticAction &action : artifact->actions) {
            if (action.actionDefinitionId == definitionId)
                action.actionDefinitionSha256 = digest;
        }
        return canonical;
    };

    StrictJson staleConstant = *originalCompanion;
    staleConstant["definitions"][4]["definition"]["steps"][0]["assignments"][0]["value_source"]
                 ["value"]
        = 1;
    QVERIFY(!rebuild(std::move(staleConstant)));

    StrictJson resignedConstant = *originalCompanion;
    StrictJson &changedConstantDefinition = resignedConstant["definitions"][4]["definition"];
    changedConstantDefinition["steps"][0]["assignments"][0]["value_source"]["value"] = 1;
    const Utils::Result<QByteArray> changedConstantCanonical = serializeCanonicalJson(
        changedConstantDefinition, defaultMaximumSemanticActionDefinitionsBytes);
    QVERIFY_RESULT(changedConstantCanonical);
    resignedConstant["definitions"][4]["action_definition_sha256"]
        = QCryptographicHash::hash(*changedConstantCanonical, QCryptographicHash::Sha256)
              .toHex()
              .toStdString();
    QVERIFY(!rebuild(std::move(resignedConstant)));

    VerifiedSemanticBindingArtifact wrongPdoProfile = evidence->semanticBindingArtifact();
    wrongPdoProfile.topologyInstances[0].pdoProfile = QStringLiteral("different_profile");
    QVERIFY(!rebuild(*originalCompanion, true, &wrongPdoProfile));

    StrictJson changedLocalGroup = *originalCompanion;
    VerifiedSemanticBindingArtifact changedLocalGroupArtifact = evidence->semanticBindingArtifact();
    changedLocalGroup["definitions"][0]["definition"]["steps"][2]["consistency_group"]
        = "different_group";
    QVERIFY_RESULT(resignDefinition(changedLocalGroup, 0, &changedLocalGroupArtifact));
    QVERIFY(!rebuild(std::move(changedLocalGroup), true, &changedLocalGroupArtifact));

    StrictJson changedCrossActionGroup = *originalCompanion;
    VerifiedSemanticBindingArtifact changedCrossActionGroupArtifact
        = evidence->semanticBindingArtifact();
    changedCrossActionGroup["definitions"][3]["definition"]["steps"][0]["consistency_group"]
        = "different_group";
    QVERIFY_RESULT(resignDefinition(changedCrossActionGroup, 3, &changedCrossActionGroupArtifact));
    QVERIFY(!rebuild(std::move(changedCrossActionGroup), true, &changedCrossActionGroupArtifact));

    StrictJson changedStepKind = *originalCompanion;
    VerifiedSemanticBindingArtifact changedStepKindArtifact = evidence->semanticBindingArtifact();
    StrictJson &waitStep = changedStepKind["definitions"][0]["definition"]["steps"][1];
    waitStep.erase("mask");
    waitStep.erase("value");
    waitStep["kind"] = "wait_absolute_limit";
    waitStep["limit"] = 10;
    QVERIFY_RESULT(resignDefinition(changedStepKind, 0, &changedStepKindArtifact));
    QVERIFY(!rebuild(std::move(changedStepKind), true, &changedStepKindArtifact));

    StrictJson duplicateDefinition = *originalCompanion;
    duplicateDefinition["definitions"][1]["action_definition_id"]
        = duplicateDefinition["definitions"][0]["action_definition_id"];
    duplicateDefinition["definitions"][1]["definition"]["definition_id"]
        = duplicateDefinition["definitions"][0]["definition"]["definition_id"];
    QVERIFY(!rebuild(std::move(duplicateDefinition)));

    StrictJson missingDefinition = *originalCompanion;
    missingDefinition["definitions"].erase(missingDefinition["definitions"].begin());
    missingDefinition["definition_count"] = missingDefinition["definitions"].size();
    QVERIFY(!rebuild(std::move(missingDefinition)));

    StrictJson differentArtifact = *originalCompanion;
    differentArtifact["semantic_binding_artifact_sha256"] = std::string(64, '0');
    QVERIFY(!rebuild(std::move(differentArtifact)));

    EcpkgContainer mismatchedReportContainer = package->container;
    StrictJson mismatchedReport = *originalReport;
    mismatchedReport["semantic_action_definitions_manifest"]["action_count"] = 7;
    std::string mismatchedReportText = mismatchedReport.dump(2);
    mismatchedReportText.push_back('\n');
    mismatchedReportContainer.compileReportJson
        = QByteArray(mismatchedReportText.data(), qsizetype(mismatchedReportText.size()));
    QVERIFY(!verifySemanticActionDefinitions(
        mismatchedReportContainer, package->manifest, evidence->semanticBindingArtifact()));

    EcpkgContainer noncanonicalContainer = package->container;
    QByteArray noncanonical = noncanonicalContainer.semanticActionDefinitionsJson;
    noncanonical.insert(1, ' ');
    noncanonicalContainer.semanticActionDefinitionsJson = noncanonical;
    VerifiedSignedEcpkgManifest noncanonicalManifest = package->manifest;
    const QByteArray noncanonicalDigest
        = QCryptographicHash::hash(noncanonical, QCryptographicHash::Sha256);
    noncanonicalManifest.actionDefinitions->bytes = quint32(noncanonical.size());
    noncanonicalManifest.actionDefinitions->sha256 = noncanonicalDigest;
    noncanonicalManifest.semanticBinding->actionDefinitionsSha256 = noncanonicalDigest;
    QVERIFY(!verifySemanticActionDefinitions(
        noncanonicalContainer,
        noncanonicalManifest,
        evidence->semanticBindingArtifact()));
}

void EtherCATSemanticRuntimeTests::testVerifiedEcpkgStore()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QDir api035(fixtureRoot.absoluteFilePath("api035"));
    const QDir api036(fixtureRoot.absoluteFilePath("api036"));
    const QByteArray package035 = readFile(
        api035.absoluteFilePath(
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg"));
    const QByteArray project035 = readFile(api035.absoluteFilePath("project.json"));
    const QByteArray package036 = readFile(
        api036.absoluteFilePath("three-slave-output-transaction-cfg3501.ecpkg"));
    const QByteArray project036 = readFile(api036.absoluteFilePath("project.json"));
    if (package035.isEmpty() || project035.isEmpty() || package036.isEmpty()
        || project036.isEmpty()) {
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
    }

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const QByteArray package035Sha256
        = fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee");
    const QByteArray mapping035
        = fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f");

    const Utils::Result<VerifiedEcpkgPackage> verified035
        = verifyProductionEcpkg(package035, trust, project035);
    QVERIFY_RESULT(verified035);
    QCOMPARE(verified035->manifest.packageSha256, package035Sha256);
    const Utils::Result<VerifiedEcpkgPackage> verified036
        = verifyProductionEcpkg(package036, trust, project036);
    QVERIFY_RESULT(verified036);
    QVERIFY(verified036->manifest.packageSha256 != verified035->manifest.packageSha256);

    QByteArray wrongKey = publicKey;
    wrongKey[0] ^= 1;
    QVERIFY(!verifyProductionEcpkg(
        package035, {{wrongKey, EcpkgTrustClass::Production}}, project035));
    QVERIFY(!verifyProductionEcpkg(
        package035, {{publicKey, EcpkgTrustClass::Engineering}}, project035));
    QByteArray wrongProject = project035;
    wrongProject[0] ^= 1;
    QVERIFY(!verifyProductionEcpkg(package035, trust, wrongProject));

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-verified-ecpkg-store"));
    QVERIFY(temporary.isValid());
    const QString storeRoot = QDir(temporary.path()).absoluteFilePath("packages");
    const Utils::Result<VerifiedEcpkgPackage> imported
        = importVerifiedEcpkg(storeRoot, package035, trust, project035);
    QVERIFY_RESULT(imported);
    QVERIFY(QFileInfo::exists(imported->storedFilePath));
    QVERIFY(imported->storedFilePath.endsWith(
        QString::fromLatin1(package035Sha256.toHex()) + ".ecpkg"));

    const Utils::Result<VerifiedEcpkgPackage> repeated
        = importVerifiedEcpkg(storeRoot, package035, trust, project035);
    QVERIFY_RESULT(repeated);
    QCOMPARE(repeated->storedFilePath, imported->storedFilePath);
    QCOMPARE(repeated->packageBytes, package035);

    const Utils::Result<VerifiedEcpkgPackage> loaded
        = loadVerifiedEcpkgByPackageSha256(
            storeRoot, package035Sha256, trust, project035);
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->storedFilePath, imported->storedFilePath);
    const Utils::Result<VerifiedEcpkgPackage> found
        = findVerifiedEcpkgBySemanticMapping(
            storeRoot, mapping035, trust, project035);
    QVERIFY_RESULT(found);
    QCOMPARE(found->manifest.packageSha256, package035Sha256);

    QTemporaryDir concurrentTemporary;
    QVERIFY(concurrentTemporary.isValid());
    const QString concurrentRoot
        = QDir(concurrentTemporary.path()).absoluteFilePath("packages");
    const auto importConcurrent = [&] {
        const Utils::Result<VerifiedEcpkgPackage> result
            = importVerifiedEcpkg(concurrentRoot, package035, trust, project035);
        return result ? result->storedFilePath : QString();
    };
    std::future<QString> first
        = std::async(std::launch::async, importConcurrent);
    std::future<QString> second
        = std::async(std::launch::async, importConcurrent);
    const QString firstPath = first.get();
    const QString secondPath = second.get();
    QVERIFY(!firstPath.isEmpty());
    QCOMPARE(secondPath, firstPath);
    QCOMPARE(
        QDir(QDir(concurrentRoot).filePath("sha256"))
            .entryList({"*.ecpkg"}, QDir::Files)
            .size(),
        qsizetype(1));
}

void EtherCATSemanticRuntimeTests::testVerifiedEcpkgStoreRejectsTampering()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api035"));
    const QByteArray package = readFile(
        fixtureDirectory.absoluteFilePath(
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg"));
    const QByteArray project = readFile(fixtureDirectory.absoluteFilePath("project.json"));
    if (package.isEmpty() || project.isEmpty())
        QSKIP("Transferred API-035 ECPKG fixture is not present");

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const QByteArray packageSha256
        = fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee");

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-verified-ecpkg-unsafe"));
    QVERIFY(temporary.isValid());
    const QString storeRoot = QDir(temporary.path()).absoluteFilePath("packages");
    const Utils::Result<VerifiedEcpkgPackage> imported
        = importVerifiedEcpkg(storeRoot, package, trust, project);
    QVERIFY_RESULT(imported);

    QFile stored(imported->storedFilePath);
    QVERIFY(stored.open(QIODevice::ReadWrite));
    const QByteArray firstByte = stored.read(1);
    QCOMPARE(firstByte.size(), qsizetype(1));
    QVERIFY(stored.seek(0));
    QCOMPARE(stored.write(QByteArray(1, char(firstByte.at(0) ^ 1))), qint64(1));
    stored.close();
    QVERIFY(!loadVerifiedEcpkgByPackageSha256(
        storeRoot, packageSha256, trust, project));

    QTemporaryDir malformedTemporary;
    QVERIFY(malformedTemporary.isValid());
    const QString malformedRoot
        = QDir(malformedTemporary.path()).absoluteFilePath("packages");
    const Utils::Result<VerifiedEcpkgPackage> valid
        = importVerifiedEcpkg(malformedRoot, package, trust, project);
    QVERIFY_RESULT(valid);
    QFile unexpected(
        QDir(QDir(malformedRoot).filePath("sha256")).filePath("unexpected"));
    QVERIFY(unexpected.open(QIODevice::WriteOnly));
    QCOMPARE(unexpected.write("x"), qint64(1));
    unexpected.close();
    QVERIFY(!findVerifiedEcpkgBySemanticMapping(
        malformedRoot,
        fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
        trust,
        project));
}

void EtherCATSemanticRuntimeTests::testRuntimePackageEvidenceTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray packageSha256;
        QByteArray manifestSha256;
        QByteArray mappingSha256;
        quint64 catalogRevision = 0;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee"),
            fromHex("f82dcf1bd1188b1eb4648396f946b8db5828ebf15d0c22fcc50fa2d14a6de4c0"),
            fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
            0x0dea3816a0a0d7afULL,
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            fromHex("40222de1f5156556117ade48922ea2e2ed246ba0a51a3caa871131803987980b"),
            fromHex("9eb3fcc112dfa5e52d286eb5e257a75f9e81d4fb0ace64ea4efdba9c2c680dcf"),
            fromHex("d4143cbbae9ac312181b12210d107db46957fb3b84a2d7d8082e929e96c26f2e"),
            0xc84fe35be276b2a8ULL,
        },
    };

    QList<QByteArray> mappingDigests;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QByteArray packageBytes
            = readFile(directory.absoluteFilePath(fixture.packageName));
        const QByteArray projectBytes
            = readFile(directory.absoluteFilePath("project.json"));
        if (packageBytes.isEmpty() || projectBytes.isEmpty())
            QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");

        const Utils::Result<VerifiedEcpkgPackage> package
            = verifyProductionEcpkg(packageBytes, trust, projectBytes);
        QVERIFY_RESULT(package);
        const Utils::Result<VerifiedRuntimePackageEvidence> evidence
            = verifyRuntimePackageEvidence(*package);
        QVERIFY_RESULT(evidence);
        QVERIFY(evidence->isValid());
        QVERIFY(!evidence->permitsWritableActions());
        QCOMPARE(
            evidence->projectConfigurationSha256(),
            QCryptographicHash::hash(projectBytes, QCryptographicHash::Sha256));

        const VerifiedSemanticBindingArtifact &artifact
            = evidence->semanticBindingArtifact();
        const Data::RuntimeSemanticMappingProof &proof
            = evidence->semanticMappingProof();
        QCOMPARE(artifact.trust, EcpkgTrustClass::Production);
        QCOMPARE(artifact.configurationId, quint64(3501));
        QCOMPARE(artifact.catalogRevision, fixture.catalogRevision);
        QCOMPARE(artifact.topologyIdentity, quint64(0x2ee7c79bc774840cULL));
        QCOMPARE(artifact.bindings.size(), qsizetype(56));

        QVERIFY(proof.isValid());
        QCOMPARE(proof.formatVersion, quint16(1));
        QCOMPARE(proof.bindingCount, quint32(56));
        QVERIFY(proof.packageSigned);
        QVERIFY(proof.signatureVerified);
        QVERIFY(proof.semanticBindingVerified);
        QCOMPARE(proof.trust, Data::RuntimeSemanticMappingTrust::Production);
        QCOMPARE(proof.packageSha256, fixture.packageSha256);
        QCOMPARE(proof.manifestSha256, fixture.manifestSha256);
        QCOMPARE(proof.mappingSha256, fixture.mappingSha256);
        QCOMPARE(proof.packageSha256, artifact.packageSha256);
        QCOMPARE(proof.manifestSha256, artifact.manifestSha256);
        QCOMPARE(proof.mappingSha256, artifact.artifactSha256);
        QCOMPARE(proof.resourceRecordsSha256, artifact.resourceRecordsSha256);
        QCOMPARE(proof.resourceSectionSha256, artifact.resourceSectionSha256);
        QCOMPARE(proof.topologySha256, artifact.topologySha256);
        QCOMPARE(proof.signingKeyIdSha256, artifact.signingKeyIdSha256);
        QCOMPARE(proof.mappingSha256, package->manifest.semanticBinding->mappingSha256);
        mappingDigests.append(proof.mappingSha256);
    }

    QCOMPARE(mappingDigests.size(), qsizetype(2));
    QVERIFY(mappingDigests.at(0) != mappingDigests.at(1));
}

void EtherCATSemanticRuntimeTests::testRuntimePackageEvidenceRejectsMismatches()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api036"));
    const QByteArray packageBytes = readFile(
        fixtureDirectory.absoluteFilePath(
            "three-slave-output-transaction-cfg3501.ecpkg"));
    const QByteArray projectBytes
        = readFile(fixtureDirectory.absoluteFilePath("project.json"));
    if (packageBytes.isEmpty() || projectBytes.isEmpty())
        QSKIP("Transferred API-036 ECPKG fixture is not present");

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkg(packageBytes, trust, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidenceResult
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidenceResult);
    const VerifiedRuntimePackageEvidence evidence = *evidenceResult;

    const Data::ControllerConnectionScope scope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    constexpr quint64 sessionGeneration = 17;
    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = 0x1122334455667788ULL;
    epoch.activePackageSlot = Data::ControllerSlot::B;
    epoch.activePackageGeneration = 12;
    epoch.configurationId = evidence.semanticBindingArtifact().configurationId;
    epoch.topologyGeneration = 23;
    epoch.runtimeGeneration = 24;
    epoch.catalogRevision = evidence.semanticBindingArtifact().catalogRevision;
    epoch.topologyIdentity = QByteArray(qsizetype(sizeof(quint64)), '\0');
    qToBigEndian(
        evidence.semanticBindingArtifact().topologyIdentity,
        reinterpret_cast<uchar *>(epoch.topologyIdentity.data()));

    Data::SemanticBindingArtifactReference artifactReference;
    artifactReference.artifactId = QStringLiteral("test/runtime/api036");
    artifactReference.artifactSha256 = evidence.semanticMappingProof().mappingSha256;
    artifactReference.projectConfigurationSha256 = evidence.projectConfigurationSha256();

    Data::RuntimeSemanticMappingAttestation attestation;
    attestation.scope = scope;
    attestation.sessionGeneration = sessionGeneration;
    attestation.epoch = epoch;
    attestation.proof = evidence.semanticMappingProof();
    attestation.receivedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(attestation.isValid());
    QVERIFY_RESULT(verifyRuntimeSemanticMappingAttestation(
        attestation, scope, sessionGeneration, epoch, artifactReference, evidence));

    const auto rejects =
        [&artifactReference, &evidence](
            const Data::RuntimeSemanticMappingAttestation &candidate,
            const Data::ControllerConnectionScope &expectedScope,
            quint64 expectedSession,
            const Data::RuntimeResourceCatalogEpoch &expectedEpoch) {
            return !verifyRuntimeSemanticMappingAttestation(
                candidate,
                expectedScope,
                expectedSession,
                expectedEpoch,
                artifactReference,
                evidence);
        };

    Data::RuntimeSemanticMappingAttestation wrong = attestation;
    wrong.scope.projectId = Data::NodeId::create();
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    wrong = attestation;
    ++wrong.sessionGeneration;
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    wrong = attestation;
    ++wrong.epoch.controllerBootId;
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    wrong = attestation;
    wrong.epoch.activePackageSlot = Data::ControllerSlot::A;
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    wrong = attestation;
    ++wrong.epoch.activePackageGeneration;
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    wrong = attestation;
    ++wrong.epoch.topologyGeneration;
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    wrong = attestation;
    ++wrong.epoch.runtimeGeneration;
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    Data::RuntimeResourceCatalogEpoch wrongPackageEpoch = epoch;
    ++wrongPackageEpoch.configurationId;
    wrong = attestation;
    wrong.epoch = wrongPackageEpoch;
    QVERIFY(rejects(
        wrong, scope, sessionGeneration, wrongPackageEpoch));

    wrongPackageEpoch = epoch;
    ++wrongPackageEpoch.catalogRevision;
    wrong = attestation;
    wrong.epoch = wrongPackageEpoch;
    QVERIFY(rejects(
        wrong, scope, sessionGeneration, wrongPackageEpoch));

    wrongPackageEpoch = epoch;
    wrongPackageEpoch.topologyIdentity[0] ^= 1;
    wrong = attestation;
    wrong.epoch = wrongPackageEpoch;
    QVERIFY(rejects(
        wrong, scope, sessionGeneration, wrongPackageEpoch));

    using ProofDigest = QByteArray Data::RuntimeSemanticMappingProof::*;
    const std::array<ProofDigest, 7> proofDigests{
        &Data::RuntimeSemanticMappingProof::packageSha256,
        &Data::RuntimeSemanticMappingProof::manifestSha256,
        &Data::RuntimeSemanticMappingProof::mappingSha256,
        &Data::RuntimeSemanticMappingProof::resourceRecordsSha256,
        &Data::RuntimeSemanticMappingProof::resourceSectionSha256,
        &Data::RuntimeSemanticMappingProof::topologySha256,
        &Data::RuntimeSemanticMappingProof::signingKeyIdSha256,
    };
    for (ProofDigest digest : proofDigests) {
        wrong = attestation;
        (wrong.proof.*digest)[0] ^= 1;
        QVERIFY(wrong.isValid());
        QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));
    }

    wrong = attestation;
    wrong.proof.trust = Data::RuntimeSemanticMappingTrust::Engineering;
    QVERIFY(wrong.isValid());
    QVERIFY(rejects(wrong, scope, sessionGeneration, epoch));

    Data::SemanticBindingArtifactReference wrongArtifactReference = artifactReference;
    wrongArtifactReference.artifactSha256[0] ^= 1;
    QVERIFY(!verifyRuntimeSemanticMappingAttestation(
        attestation,
        scope,
        sessionGeneration,
        epoch,
        wrongArtifactReference,
        evidence));

    wrongArtifactReference = artifactReference;
    wrongArtifactReference.projectConfigurationSha256[0] ^= 1;
    QVERIFY(!verifyRuntimeSemanticMappingAttestation(
        attestation,
        scope,
        sessionGeneration,
        epoch,
        wrongArtifactReference,
        evidence));

    wrongArtifactReference = artifactReference;
    wrongArtifactReference.artifactId.clear();
    QVERIFY(!verifyRuntimeSemanticMappingAttestation(
        attestation,
        scope,
        sessionGeneration,
        epoch,
        wrongArtifactReference,
        evidence));

    VerifiedEcpkgPackage engineeringPackage = *package;
    engineeringPackage.manifest.trust = EcpkgTrustClass::Engineering;
    QVERIFY(!verifyRuntimePackageEvidence(engineeringPackage));

    VerifiedEcpkgPackage changedBytes = *package;
    changedBytes.packageBytes[0] ^= 1;
    QVERIFY(!verifyRuntimePackageEvidence(changedBytes));
}

void EtherCATSemanticRuntimeTests::testRuntimePackageEvidenceRepository()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath(
            "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath(
            "artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray projectBytes
        = readFile(fixtureRoot.absoluteFilePath("package_inputs/project.json"));
    const QByteArray publicKey = readFile(
        fixtureRoot.absoluteFilePath(
            "trust/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"));
    if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.isEmpty())
        QSKIP("Transferred API-037 repository fixture is not present");

    const auto writeBytes = [](const QString &path, QByteArrayView bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(bytes.data(), bytes.size()) != bytes.size()) {
            return false;
        }
        file.close();
        return file.error() == QFileDevice::NoError;
    };

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-runtime-evidence-repository"));
    QVERIFY(temporary.isValid());
    const QString packageStoreRoot = QDir(temporary.path()).filePath("packages");
    const QString projectSourceRoot = QDir(temporary.path()).filePath("projects");
    const QString trustDirectory = QDir(temporary.path()).filePath("trust");
    QVERIFY(QDir().mkpath(trustDirectory));
    const QByteArray keyId
        = QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256);
    QVERIFY(writeBytes(
        QDir(trustDirectory).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"),
        publicKey));

    const RuntimePackageEvidenceRepository repository{
        packageStoreRoot,
        trustDirectory,
        projectSourceRoot,
    };
    QCOMPARE(repository.verifiedPackageStoreRoot(), packageStoreRoot);
    QCOMPARE(repository.productionTrustDirectory(), trustDirectory);
    QCOMPARE(repository.compiledProjectSourceRoot(), projectSourceRoot);

    const Utils::Result<VerifiedRuntimePackageEvidence> imported
        = repository.import(packageBytes, projectBytes);
    QVERIFY_RESULT(imported);
    QVERIFY(imported->isValid());
    QVERIFY(!imported->permitsWritableActions());
    QVERIFY(imported->semanticBindingArtifact().permitsWritableActions());
    QCOMPARE(imported->semanticMappingProof().formatVersion, quint16(2));

    const QByteArray packageSha256
        = QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256);
    const QByteArray projectSha256
        = QCryptographicHash::hash(projectBytes, QCryptographicHash::Sha256);
    QCOMPARE(imported->projectConfigurationSha256(), projectSha256);
    const QString storedPackagePath
        = QDir(QDir(packageStoreRoot).filePath("sha256"))
              .filePath(QString::fromLatin1(packageSha256.toHex()) + ".ecpkg");
    const QString storedProjectPath
        = QDir(QDir(projectSourceRoot).filePath("sha256"))
              .filePath(QString::fromLatin1(projectSha256.toHex()) + ".json");
    QCOMPARE(readFile(storedPackagePath), packageBytes);
    QCOMPARE(readFile(storedProjectPath), projectBytes);

    Data::SemanticBindingArtifactReference reference;
    // artifactId remains opaque metadata and must never participate in path
    // construction.
    reference.artifactId = QStringLiteral("../../opaque-not-a-path");
    reference.artifactSha256
        = imported->semanticBindingArtifact().artifactSha256;
    reference.projectConfigurationSha256 = projectSha256;
    const Utils::Result<VerifiedRuntimePackageEvidence> loaded
        = repository.load(reference);
    QVERIFY_RESULT(loaded);
    QCOMPARE(
        loaded->semanticBindingArtifact().packageSha256,
        imported->semanticBindingArtifact().packageSha256);
    QCOMPARE(
        loaded->semanticBindingArtifact().artifactSha256,
        imported->semanticBindingArtifact().artifactSha256);

    const Utils::Result<VerifiedRuntimePackageEvidence> repeated
        = repository.import(packageBytes, projectBytes);
    QVERIFY_RESULT(repeated);
    QCOMPARE(
        repeated->semanticBindingArtifact().packageSha256,
        imported->semanticBindingArtifact().packageSha256);

    const auto concurrentImport = [&repository, &packageBytes, &projectBytes] {
        const Utils::Result<VerifiedRuntimePackageEvidence> result
            = repository.import(packageBytes, projectBytes);
        return result ? result->semanticBindingArtifact().packageSha256 : QByteArray();
    };
    std::future<QByteArray> first = std::async(std::launch::async, concurrentImport);
    std::future<QByteArray> second = std::async(std::launch::async, concurrentImport);
    QCOMPARE(first.get(), packageSha256);
    QCOMPARE(second.get(), packageSha256);
    QCOMPARE(
        QDir(QDir(packageStoreRoot).filePath("sha256"))
            .entryList({"*.ecpkg"}, QDir::Files)
            .size(),
        qsizetype(1));
    QCOMPARE(
        QDir(QDir(projectSourceRoot).filePath("sha256"))
            .entryList({"*.json"}, QDir::Files)
            .size(),
        qsizetype(1));

    Data::SemanticBindingArtifactReference wrongReference = reference;
    wrongReference.artifactSha256[0] ^= 1;
    QVERIFY(!repository.load(wrongReference));
    wrongReference = reference;
    wrongReference.projectConfigurationSha256[0] ^= 1;
    QVERIFY(!repository.load(wrongReference));
    wrongReference = reference;
    wrongReference.artifactId.clear();
    QVERIFY(!repository.load(wrongReference));
}

void EtherCATSemanticRuntimeTests::testRuntimePackageEvidenceRepositoryRejectsUnsafeInputs()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath(
            "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath(
            "artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray projectBytes
        = readFile(fixtureRoot.absoluteFilePath("package_inputs/project.json"));
    const QByteArray publicKey = readFile(
        fixtureRoot.absoluteFilePath(
            "trust/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"));
    if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.isEmpty())
        QSKIP("Transferred API-037 repository fixture is not present");

    const auto writeBytes = [](const QString &path, QByteArrayView bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(bytes.data(), bytes.size()) != bytes.size()) {
            return false;
        }
        file.close();
        return file.error() == QFileDevice::NoError;
    };
    const QByteArray keyId
        = QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256);
    const QByteArray packageSha256
        = QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256);
    const QByteArray projectSha256
        = QCryptographicHash::hash(projectBytes, QCryptographicHash::Sha256);

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-runtime-evidence-unsafe"));
    QVERIFY(temporary.isValid());
    const QString packageStoreRoot = QDir(temporary.path()).filePath("packages");
    const QString projectSourceRoot = QDir(temporary.path()).filePath("projects");
    const QString trustDirectory = QDir(temporary.path()).filePath("trust");
    QVERIFY(QDir().mkpath(trustDirectory));
    QVERIFY(writeBytes(
        QDir(trustDirectory).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"),
        publicKey));

    const RuntimePackageEvidenceRepository repository{
        packageStoreRoot,
        trustDirectory,
        projectSourceRoot,
    };
    const Utils::Result<VerifiedRuntimePackageEvidence> imported
        = repository.import(packageBytes, projectBytes);
    QVERIFY_RESULT(imported);

    Data::SemanticBindingArtifactReference reference;
    reference.artifactId = QStringLiteral("test/api037/runtime-evidence");
    reference.artifactSha256
        = imported->semanticBindingArtifact().artifactSha256;
    reference.projectConfigurationSha256 = projectSha256;
    QVERIFY_RESULT(repository.load(reference));

    const QString storedPackagePath
        = QDir(QDir(packageStoreRoot).filePath("sha256"))
              .filePath(QString::fromLatin1(packageSha256.toHex()) + ".ecpkg");
    const QString storedProjectPath
        = QDir(QDir(projectSourceRoot).filePath("sha256"))
              .filePath(QString::fromLatin1(projectSha256.toHex()) + ".json");

    QByteArray tamperedPackage = packageBytes;
    tamperedPackage[0] ^= 1;
    QVERIFY(writeBytes(storedPackagePath, tamperedPackage));
    QVERIFY(!repository.load(reference));
    QVERIFY(writeBytes(storedPackagePath, packageBytes));
    QVERIFY_RESULT(repository.load(reference));

    QByteArray tamperedProject = projectBytes;
    tamperedProject[0] ^= 1;
    QVERIFY(writeBytes(storedProjectPath, tamperedProject));
    QVERIFY(!repository.load(reference));
    QVERIFY(writeBytes(storedProjectPath, projectBytes));
    QVERIFY_RESULT(repository.load(reference));

    QFile hidden(QDir(QDir(projectSourceRoot).filePath("sha256")).filePath(".hidden"));
    QVERIFY(hidden.open(QIODevice::WriteOnly));
    QCOMPARE(hidden.write("x"), qint64(1));
    hidden.close();
    QVERIFY(!repository.load(reference));
    QVERIFY(QFile::remove(hidden.fileName()));
    QVERIFY_RESULT(repository.load(reference));

    const QString originalProjectPath = storedProjectPath + ".original";
    QVERIFY(QFile::rename(storedProjectPath, originalProjectPath));
    if (QFile::link(originalProjectPath, storedProjectPath)) {
        QVERIFY(!repository.load(reference));
        QVERIFY(QFile::remove(storedProjectPath));
        QVERIFY(QFile::rename(originalProjectPath, storedProjectPath));
    } else {
        QVERIFY(QFile::rename(originalProjectPath, storedProjectPath));
    }
    QVERIFY_RESULT(repository.load(reference));

    const QString linkedPackageRoot = QDir(temporary.path()).filePath("linked-packages");
    if (QFile::link(packageStoreRoot, linkedPackageRoot)) {
        const RuntimePackageEvidenceRepository linkedRepository{
            linkedPackageRoot,
            trustDirectory,
            projectSourceRoot,
        };
        QVERIFY(!linkedRepository.load(reference));
    }

    const QString wrongTrustDirectory = QDir(temporary.path()).filePath("wrong-trust");
    QVERIFY(QDir().mkpath(wrongTrustDirectory));
    const QByteArray wrongKey(32, '\x5a');
    const QByteArray wrongKeyId
        = QCryptographicHash::hash(wrongKey, QCryptographicHash::Sha256);
    QVERIFY(writeBytes(
        QDir(wrongTrustDirectory)
            .filePath(QString::fromLatin1(wrongKeyId.toHex()) + ".pub"),
        wrongKey));
    const RuntimePackageEvidenceRepository wrongTrustRepository{
        packageStoreRoot,
        wrongTrustDirectory,
        projectSourceRoot,
    };
    QVERIFY(!wrongTrustRepository.load(reference));

    const QString trustedKeyPath
        = QDir(trustDirectory).filePath(QString::fromLatin1(keyId.toHex()) + ".pub");
    QVERIFY(writeBytes(trustedKeyPath, wrongKey));
    QVERIFY(!repository.load(reference));
    QVERIFY(writeBytes(trustedKeyPath, publicKey));
    QVERIFY_RESULT(repository.load(reference));

    const RuntimePackageEvidenceRepository noncanonicalRepository{
        packageStoreRoot + QStringLiteral("/../packages"),
        trustDirectory,
        projectSourceRoot,
    };
    QVERIFY(!noncanonicalRepository.load(reference));

    // Force the two-root transaction to fail only after the project sidecar
    // has been committed. The orphan remains digest-addressed and cannot
    // produce evidence while the package entry is invalid.
    const QString halfPackageRoot = QDir(temporary.path()).filePath("half-packages");
    const QString halfProjectRoot = QDir(temporary.path()).filePath("half-projects");
    const QString halfPackageDirectory = QDir(halfPackageRoot).filePath("sha256");
    QVERIFY(QDir().mkpath(halfPackageDirectory));
    const QString halfPackagePath
        = QDir(halfPackageDirectory)
              .filePath(QString::fromLatin1(packageSha256.toHex()) + ".ecpkg");
    QVERIFY(writeBytes(halfPackagePath, QByteArrayView("not-an-ecpkg", 12)));
    const RuntimePackageEvidenceRepository halfRepository{
        halfPackageRoot,
        trustDirectory,
        halfProjectRoot,
    };
    QVERIFY(!halfRepository.import(packageBytes, projectBytes));
    const QString orphanProjectPath
        = QDir(QDir(halfProjectRoot).filePath("sha256"))
              .filePath(QString::fromLatin1(projectSha256.toHex()) + ".json");
    QCOMPARE(readFile(orphanProjectPath), projectBytes);
    QCOMPARE(readFile(halfPackagePath), QByteArray("not-an-ecpkg"));
    QVERIFY(!halfRepository.load(reference));

    QVERIFY(QFile::remove(halfPackagePath));
    const Utils::Result<VerifiedRuntimePackageEvidence> recovered
        = halfRepository.import(packageBytes, projectBytes);
    QVERIFY_RESULT(recovered);
    QCOMPARE(
        recovered->semanticBindingArtifact().packageSha256,
        imported->semanticBindingArtifact().packageSha256);
    QVERIFY_RESULT(halfRepository.load(reference));
}

void EtherCATSemanticRuntimeTests::testHardwareApi038SemanticPreflight()
{
    QCOMPARE(
        api038HardwareAuthorization({}, {}),
        Api038HardwareAuthorization::Disabled);
    QCOMPARE(
        api038HardwareAuthorization("0", api038HardwareConfirmation),
        Api038HardwareAuthorization::Disabled);
    QCOMPARE(
        api038HardwareAuthorization("1", "wrong-confirmation"),
        Api038HardwareAuthorization::Invalid);
    QCOMPARE(
        api038HardwareAuthorization("1", api038HardwareConfirmation),
        Api038HardwareAuthorization::Authorized);

    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    const QByteArray publicKey = readTestData(
        "testdata/api038/"
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    QVERIFY(!packageBytes.isEmpty());
    QVERIFY(!projectBytes.isEmpty());
    QCOMPARE(publicKey.size(), qsizetype(32));

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-api038-hardware-preflight"));
    QVERIFY(temporary.isValid());
    const QString trustRoot = QDir(temporary.path()).filePath("trust");
    QVERIFY(QDir().mkpath(trustRoot));
    const QByteArray keyId
        = QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256);
    QVERIFY(writeFile(
        QDir(trustRoot).filePath(QString::fromLatin1(keyId.toHex()) + ".pub"),
        publicKey));

    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = importApi038LocalEvidence(
            packageBytes,
            projectBytes,
            QDir(temporary.path()).filePath("packages"),
            trustRoot,
            QDir(temporary.path()).filePath("projects"));
    QVERIFY_RESULT(evidence);

    QByteArray wrongPackage = packageBytes;
    wrongPackage[0] ^= 1;
    const Utils::Result<VerifiedRuntimePackageEvidence> wrongPackageResult
        = importApi038LocalEvidence(
            wrongPackage,
            projectBytes,
            QDir(temporary.path()).filePath("wrong-package-store"),
            trustRoot,
            QDir(temporary.path()).filePath("wrong-package-projects"));
    QVERIFY(!wrongPackageResult);
    QVERIFY(wrongPackageResult.error().contains(QStringLiteral("SHA-256 differs")));

    const QString wrongTrustRoot = QDir(temporary.path()).filePath("wrong-trust");
    QVERIFY(QDir().mkpath(wrongTrustRoot));
    const QByteArray wrongPublicKey(32, '\x5a');
    const QByteArray wrongKeyId
        = QCryptographicHash::hash(wrongPublicKey, QCryptographicHash::Sha256);
    QVERIFY(writeFile(
        QDir(wrongTrustRoot)
            .filePath(QString::fromLatin1(wrongKeyId.toHex()) + ".pub"),
        wrongPublicKey));
    const Utils::Result<VerifiedRuntimePackageEvidence> wrongTrustResult
        = importApi038LocalEvidence(
            packageBytes,
            projectBytes,
            QDir(temporary.path()).filePath("wrong-trust-packages"),
            wrongTrustRoot,
            QDir(temporary.path()).filePath("wrong-trust-projects"));
    QVERIFY(!wrongTrustResult);
    QVERIFY(wrongTrustResult.error().contains(
        QStringLiteral("repository verification failed")));

    const Utils::Result<QList<Data::DeviceAdapterManifest>> publishedAdapters
        = publishedApi038V3ActionAdapters();
    QVERIFY_RESULT(publishedAdapters);
    const QStringList blockers
        = api038HardwareAdmissionBlockers(*evidence, *publishedAdapters);
    QVERIFY(std::any_of(
        blockers.cbegin(),
        blockers.cend(),
        [](const QString &blocker) {
            return blocker.contains(
                QStringLiteral("target 1.3.0 does not match signed cfg3701 target 1.2.0"));
        }));
    QCOMPARE(
        std::count_if(
            blockers.cbegin(),
            blockers.cend(),
            [](const QString &blocker) {
                return blocker.contains(QStringLiteral("is not hardware-qualified"));
            }),
        2);
    QVERIFY(blockers.contains(
        QStringLiteral(
            "API-042 compiler activation proof and current project capture are absent")));
}

void EtherCATSemanticRuntimeTests::testHardwareApi038SemanticAdmissionFailsClosed()
{
    const Api038HardwareAuthorization authorization = api038HardwareAuthorization(
        qgetenv(api038HardwareEnableVariable),
        qgetenv(api038HardwareConfirmationVariable));
    if (authorization == Api038HardwareAuthorization::Disabled) {
        QSKIP(
            "API-038 semantic hardware acceptance is disabled; no Product API session "
            "or mutation was attempted");
    }
    if (authorization == Api038HardwareAuthorization::Invalid) {
        QFAIL(
            "QTC_ETHER_CAT_API038_CONFIRM does not authorize the exact fixed cfg3701 "
            "semantic acceptance fixture");
    }

    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    if (packageBytes.isEmpty() || projectBytes.isEmpty())
        QFAIL("The fixed API-038 ECPKG or compiled project source is missing");

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-api038-hardware-acceptance"));
    if (!temporary.isValid())
        QFAIL("The API-038 local evidence repository could not be created");
    const Utils::FilePath trustDirectory
        = ::Core::ICore::resourcePath("ethercat/production-trust");
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = importApi038LocalEvidence(
            packageBytes,
            projectBytes,
            QDir(temporary.path()).filePath("packages"),
            trustDirectory.toFSPathString(),
            QDir(temporary.path()).filePath("projects"));
    if (!evidence) {
        const QByteArray detail
            = QStringLiteral("API-038 local evidence preflight failed: %1")
                  .arg(evidence.error())
                  .toUtf8();
        QFAIL(detail.constData());
    }

    const Utils::Result<QList<Data::DeviceAdapterManifest>> publishedAdapters
        = publishedApi038V3ActionAdapters();
    if (!publishedAdapters) {
        const QByteArray detail
            = QStringLiteral(
                  "API-038 stopped before controller access: %1")
                  .arg(publishedAdapters.error())
                  .toUtf8();
        QSKIP(detail.constData());
    }
    const QStringList blockers
        = api038HardwareAdmissionBlockers(*evidence, *publishedAdapters);
    if (!blockers.isEmpty()) {
        const QByteArray detail
            = QStringLiteral(
                  "API-038 stopped before any Product API session, lease, or mutation: %1")
                  .arg(blockers.join(QStringLiteral("; ")))
                  .toUtf8();
        QSKIP(detail.constData());
    }

    QFAIL(
        "API-038 admission unexpectedly passed without a product activation preparation; "
        "controller access remains deliberately unimplemented");
}

void EtherCATSemanticRuntimeTests::testHardwareApi051SemanticPreflight()
{
    QCOMPARE(api051HardwareAuthorization({}, {}), Api051HardwareAuthorization::Disabled);
    QCOMPARE(
        api051HardwareAuthorization("0", api051HardwareConfirmation),
        Api051HardwareAuthorization::Disabled);
    QCOMPARE(
        api051HardwareAuthorization("1", "wrong-confirmation"),
        Api051HardwareAuthorization::Invalid);
    QCOMPARE(
        api051HardwareAuthorization("1", api051HardwareConfirmation),
        Api051HardwareAuthorization::Authorized);

    const QString packagePath = api051InputPath(
        api051PackagePathVariable, u"package/three-slave-manual-control-fixed-pdo-cfg3702.ecpkg");
    const QString projectPath = api051InputPath(api051ProjectPathVariable, u"evidence/project.json");
    const QByteArray packageBytes = readFile(packagePath);
    const QByteArray projectBytes = readFile(projectPath);
    if (packageBytes.isEmpty() || projectBytes.isEmpty()) {
        QSKIP(
            "The ignored API-051 handoff fixture is not present; authorization remained disabled "
            "and no Product API object was accessed");
    }

    const Utils::FilePath trustDirectory = ::Core::ICore::resourcePath("ethercat/production-trust");
    if (!QFileInfo::exists(trustDirectory.toFSPathString())) {
        QSKIP(
            "The installed production trust directory is unavailable; no Product API object was "
            "accessed");
    }

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-api051-hardware-preflight"));
    QVERIFY(temporary.isValid());
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence = importApi051LocalEvidence(
        packageBytes,
        projectBytes,
        QDir(temporary.path()).filePath("packages"),
        trustDirectory.toFSPathString(),
        QDir(temporary.path()).filePath("projects"));
    QVERIFY_RESULT(evidence);

    const Utils::Result<QList<Data::DeviceAdapterManifest>> publishedAdapters
        = publishedApi038V3ActionAdapters();
    QVERIFY_RESULT(publishedAdapters);
    const QStringList adapterBlockers
        = api051AdapterAdmissionBlockers(*evidence, *publishedAdapters);
    QCOMPARE(adapterBlockers.size(), 2);
    QVERIFY(
        std::all_of(adapterBlockers.cbegin(), adapterBlockers.cend(), [](const QString &blocker) {
            return blocker.contains(
                QStringLiteral("lacks independently verified real-hardware permission"));
        }));

    Data::ProjectSnapshot project = factoryProject(*evidence);
    configureApi038V3ManualProject(project, *publishedAdapters);
    project.masterConfiguration.timingMode = Data::MasterTimingMode::DistributedClocks;
    project.masterConfiguration.cyclePeriodNs = api051CyclePeriodNs;
    QCOMPARE(project.slaves.size(), qsizetype(3));
    const Data::OfflineSlaveConfiguration *xb6 = factoryManualSlave(project);
    QVERIFY(xb6);
    QCOMPARE(xb6->position, 0);
    QVERIFY(xb6->manualControlEnvelope.enabled);
    QCOMPARE(xb6->manualControlEnvelope.actionEnvelopes.size(), qsizetype(2));
    QCOMPARE(
        std::count_if(
            project.slaves.cbegin(),
            project.slaves.cend(),
            [](const Data::OfflineSlaveConfiguration &slave) {
                return std::count_if(
                           slave.manualControlEnvelope.actionEnvelopes.cbegin(),
                           slave.manualControlEnvelope.actionEnvelopes.cend(),
                           [](const Data::ManualActionEnvelope &action) { return !action.enabled; })
                       == 3;
            }),
        2);

    QByteArray changedPackage = packageBytes;
    changedPackage[0] ^= 1;
    const Utils::Result<VerifiedRuntimePackageEvidence> changedPackageResult
        = importApi051LocalEvidence(
            changedPackage,
            projectBytes,
            QDir(temporary.path()).filePath("changed-packages"),
            trustDirectory.toFSPathString(),
            QDir(temporary.path()).filePath("changed-projects"));
    QVERIFY(!changedPackageResult);
    QVERIFY(changedPackageResult.error().contains(QStringLiteral("SHA-256 differs")));

    QByteArray changedProject = projectBytes;
    changedProject[0] ^= 1;
    const Utils::Result<VerifiedRuntimePackageEvidence> changedProjectResult
        = importApi051LocalEvidence(
            packageBytes,
            changedProject,
            QDir(temporary.path()).filePath("project-mismatch-packages"),
            trustDirectory.toFSPathString(),
            QDir(temporary.path()).filePath("project-mismatch-projects"));
    QVERIFY(!changedProjectResult);
    QVERIFY(changedProjectResult.error().contains(QStringLiteral("SHA-256 differs")));
}

void EtherCATSemanticRuntimeTests::testHardwareApi051SemanticAcceptance()
{
    constexpr int connectTimeoutMs = 20000;
    constexpr int operationTimeoutMs = 55000;
    constexpr int runtimeTimeoutMs = 45000;

    const Api051HardwareAuthorization authorization = api051HardwareAuthorization(
        qgetenv(api051HardwareEnableVariable), qgetenv(api051HardwareConfirmationVariable));
    if (authorization == Api051HardwareAuthorization::Disabled) {
        QSKIP(
            "API-051 semantic hardware acceptance is disabled; no Product API provider, session, "
            "or mutation was accessed");
    }
    if (authorization == Api051HardwareAuthorization::Invalid) {
        QFAIL(
            "QTC_ETHER_CAT_API051_CONFIRM does not authorize the exact cfg3702 XB6/DC "
            "acceptance flow");
    }

    // Package and project paths are untrusted byte sources only. All identities
    // below are fixed in the test and independently rederived from the signed
    // package before the Product API provider is obtained.
    const QString packagePath = api051InputPath(
        api051PackagePathVariable, u"package/three-slave-manual-control-fixed-pdo-cfg3702.ecpkg");
    const QString projectPath = api051InputPath(api051ProjectPathVariable, u"evidence/project.json");
    const QByteArray packageBytes = readFile(packagePath);
    const QByteArray projectBytes = readFile(projectPath);
    if (packageBytes.isEmpty() || projectBytes.isEmpty())
        QFAIL("The exact API-051 package or compiled project source is unavailable");

    const Utils::FilePath trustDirectory = ::Core::ICore::resourcePath("ethercat/production-trust");
    if (!QFileInfo::exists(trustDirectory.toFSPathString()))
        QFAIL("The installed production trust directory is unavailable");
    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-api051-hardware-acceptance"));
    if (!temporary.isValid())
        QFAIL("The API-051 verified evidence repository could not be created");

    auto repository = std::make_shared<RuntimePackageEvidenceRepository>(
        QDir(temporary.path()).filePath("packages"),
        trustDirectory.toFSPathString(),
        QDir(temporary.path()).filePath("projects"));
    const Utils::Result<VerifiedRuntimePackageEvidence> imported
        = repository->import(packageBytes, projectBytes);
    if (!imported) {
        const QByteArray detail = QStringLiteral("API-051 repository verification failed: %1")
                                      .arg(imported.error())
                                      .toUtf8();
        QFAIL(detail.constData());
    }
    const Utils::Result<> evidenceValidation
        = validateApi051LocalEvidence(*imported, packageBytes, projectBytes);
    if (!evidenceValidation) {
        const QByteArray detail = evidenceValidation.error().toUtf8();
        QFAIL(detail.constData());
    }
    const auto evidence = std::make_shared<const VerifiedRuntimePackageEvidence>(*imported);

    const Utils::Result<QList<Data::DeviceAdapterManifest>> publishedAdapters
        = publishedApi038V3ActionAdapters();
    if (!publishedAdapters) {
        const QByteArray detail = QStringLiteral("API-051 upper adapters are unavailable: %1")
                                      .arg(publishedAdapters.error())
                                      .toUtf8();
        QFAIL(detail.constData());
    }
    const QStringList adapterBlockers
        = api051AdapterAdmissionBlockers(*evidence, *publishedAdapters);
    if (!adapterBlockers.isEmpty()) {
        const QByteArray detail = QStringLiteral("API-051 adapter preflight failed: %1")
                                      .arg(adapterBlockers.join(QStringLiteral("; ")))
                                      .toUtf8();
        QFAIL(detail.constData());
    }

    Data::ProjectSnapshot project = factoryProject(*evidence);
    configureApi038V3ManualProject(project, *publishedAdapters);
    project.name = QStringLiteral("API-051 cfg3702 headless acceptance");
    project.masterConfiguration.timingMode = Data::MasterTimingMode::DistributedClocks;
    project.masterConfiguration.cyclePeriodNs = api051CyclePeriodNs;
    const Data::ControllerConnectionScope scope = projectScope(project);
    if (scope.projectId.isNull() || scope.masterId.isNull() || !factoryManualSlave(project)) {
        QFAIL("API-051 could not construct the exact upper-layer project adaptation");
    }

    TestProjectService projects;
    projects.addProject(project);
    if (!projects.activateProject(project.id))
        QFAIL("The API-051 headless project could not be activated");

    auto *providerRegistry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    if (!providerRegistry)
        QFAIL("The public controller provider registry is unavailable");
    QString host = qEnvironmentVariable(api051HostVariable).trimmed();
    if (host.isEmpty())
        host = QStringLiteral("192.168.3.101");

    struct ProviderSelection
    {
        Core::ControllerConnectionProvider *provider = nullptr;
        Data::ControllerConnectionProfile profile;
        Data::ControllerConnectionProfileConfiguration configuration;
    };
    QList<ProviderSelection> matchingProviders;
    for (Core::Provider *candidate :
         providerRegistry->providers(Core::ProviderKind::ControllerConnection)) {
        auto *connectionProvider
            = qobject_cast<Core::ControllerConnectionProvider *>(candidate);
        if (!connectionProvider || !connectionProvider->isAvailable()
            || connectionProvider->connectionSnapshot().state
                   != Data::ControllerConnectionState::Disconnected) {
            continue;
        }
        const QList<Data::ControllerConnectionProfile> candidateProfiles
            = connectionProvider->connectionProfiles(scope);
        if (candidateProfiles.size() != 1 || !candidateProfiles.constFirst().defaultProfile
            || !candidateProfiles.constFirst().configured
            || !candidateProfiles.constFirst().supported
            || !candidateProfiles.constFirst().configurationIssue.isEmpty()) {
            continue;
        }
        const std::optional<Data::ControllerConnectionProfileConfiguration> configuration
            = connectionProvider->connectionProfileConfiguration(
                scope, candidateProfiles.constFirst().id);
        if (!configuration || !configuration->editable
            || !configuration->endpointDescription.contains(QStringLiteral("15200"))
            || !configuration->endpointDescription.contains(QStringLiteral("15201"))
            || !configuration->endpointDescription.contains(QStringLiteral("15202"))) {
            continue;
        }
        matchingProviders.append(
            {connectionProvider, candidateProfiles.constFirst(), *configuration});
    }
    if (matchingProviders.size() != 1)
        QFAIL("The public three-channel Product API profile is missing or ambiguous");
    Core::ControllerConnectionProvider *providerPointer
        = matchingProviders.constFirst().provider;
    Core::ControllerConnectionProvider &provider = *providerPointer;
    const Data::ControllerConnectionProfile profile = matchingProviders.constFirst().profile;
    const Data::ControllerConnectionProfileConfiguration previousProfile
        = matchingProviders.constFirst().configuration;
    auto executor
        = std::make_unique<SemanticRuntimeExecutor>(&projects, providerRegistry, nullptr, repository);

    QList<Data::RuntimeOutputGroupPolicyResult> policyResults;
    QList<Data::RuntimeOutputTransactionStateResult> outputStateResults;
    QList<Data::RuntimeOutputTransactionResult> outputResults;
    QList<Data::RuntimeResourceSnapshotResult> targetedSnapshotResults;
    QList<Data::SemanticOperationId> attemptedOutputOperations;
    const QMetaObject::Connection policyConnection = QObject::connect(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished,
        &provider,
        [&policyResults](const Data::RuntimeOutputGroupPolicyResult &result) {
            policyResults.append(result);
        });
    const QMetaObject::Connection outputStateConnection = QObject::connect(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished,
        &provider,
        [&outputStateResults](const Data::RuntimeOutputTransactionStateResult &result) {
            outputStateResults.append(result);
        });
    const QMetaObject::Connection outputConnection = QObject::connect(
        &provider,
        &Core::ControllerConnectionProvider::runtimeOutputTransactionFinished,
        &provider,
        [&outputResults](const Data::RuntimeOutputTransactionResult &result) {
            outputResults.append(result);
        });
    const QMetaObject::Connection snapshotConnection = QObject::connect(
        &provider,
        &Core::ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished,
        &provider,
        [&targetedSnapshotResults](const Data::RuntimeResourceSnapshotResult &result) {
            targetedSnapshotResults.append(result);
        });
    const QScopeGuard signalCleanup([&] {
        QObject::disconnect(policyConnection);
        QObject::disconnect(outputStateConnection);
        QObject::disconnect(outputConnection);
        QObject::disconnect(snapshotConnection);
    });

    QString failure;
    QStringList cleanupFailures;
    bool connectionStarted = false;
    bool endpointConfigured = false;
    quint64 bootId = 0;
    quint64 sessionId = 0;
    quint64 sessionGeneration = 0;
    const auto fail = [&failure](const QString &detail) {
        if (failure.isEmpty())
            failure = detail;
    };
    const auto command = [&provider, &fail, &failure](
                             Data::ControllerControlCommand command,
                             QStringView label,
                             quint32 leaseDurationMs = 0) {
        if (!failure.isEmpty())
            return false;
        Data::ControllerControlRequest request;
        request.command = command;
        request.leaseDurationMs = leaseDurationMs;
        const QString error = executeApi051Command(provider, request, operationTimeoutMs);
        if (!error.isEmpty()) {
            fail(QStringLiteral("%1 failed: %2").arg(label, error));
            return false;
        }
        return true;
    };

    const Utils::Result<> configured
        = provider.setConnectionProfileEndpoint(scope, profile.id, host);
    if (!configured) {
        fail(QStringLiteral("The API-051 controller IP is invalid: %1").arg(configured.error()));
    } else {
        endpointConfigured = true;
    }

    if (failure.isEmpty()) {
        const Data::ControllerConnectionRequest request{scope, profile.id};
        const Utils::Result<> connected = provider.connectToController(request);
        if (!connected) {
            fail(QStringLiteral("The Product API connection could not start: %1")
                     .arg(connected.error()));
        } else {
            connectionStarted = true;
            if (!waitForApi051Condition(
                    [&provider] {
                        const Data::ControllerConnectionState state
                            = provider.connectionSnapshot().state;
                        return state == Data::ControllerConnectionState::Connected
                               || state == Data::ControllerConnectionState::Failed
                               || state == Data::ControllerConnectionState::Disconnected;
                    },
                    connectTimeoutMs)) {
                fail(QStringLiteral("The three Product API channels did not connect in time"));
            }
        }
    }

    Data::ControllerConnectionSnapshot initialSnapshot = provider.connectionSnapshot();
    if (failure.isEmpty()) {
        if (!api051Connected(initialSnapshot) || initialSnapshot.protocolVersion.major != 1
            || initialSnapshot.protocolVersion.minor != 14
            || !api051HasAllV114Capabilities(initialSnapshot.capability) || !initialSnapshot.session
            || !initialSnapshot.controllerState || !initialSnapshot.package
            || initialSnapshot.readOnly || initialSnapshot.mock) {
            fail(QStringLiteral(
                "The Product API v1.14 three-channel capability set is unavailable"));
        } else {
            bootId = initialSnapshot.session->bootId;
            sessionId = initialSnapshot.session->sessionId;
            sessionGeneration = initialSnapshot.sessionGeneration;
        }
    }

    if (failure.isEmpty()) {
        const Data::ControllerStateSummary beforeState = *initialSnapshot.controllerState;
        const Utils::Result<> refreshed = provider.refreshController();
        if (!refreshed) {
            fail(QStringLiteral("The explicit controller heartbeat refresh was rejected: %1")
                     .arg(refreshed.error()));
        } else if (!waitForApi051Condition(
                       [&provider,
                        bootId,
                        sessionId,
                        sessionGeneration,
                        beforeHeartbeat = beforeState.controllerHeartbeat] {
                           const Data::ControllerConnectionSnapshot snapshot
                               = provider.connectionSnapshot();
                           return api051Connected(snapshot) && snapshot.session
                                  && snapshot.controllerState
                                  && snapshot.sessionGeneration == sessionGeneration
                                  && snapshot.session->sessionId == sessionId
                                  && snapshot.session->bootId == bootId
                                  && snapshot.controllerState->controllerHeartbeat
                                         > beforeHeartbeat;
                       },
                       15000)) {
            fail(QStringLiteral(
                "The CPU1 heartbeat did not advance in the same session and BootId"));
        }
    }

    if (failure.isEmpty()) {
        initialSnapshot = provider.connectionSnapshot();
        const Data::ControllerStateSummary &state = *initialSnapshot.controllerState;
        const Data::ControllerPackageSummary &package = *initialSnapshot.package;
        if (state.serviceState != Data::ControllerServiceState::Shutdown || !state.ready
            || state.currentFaults || state.latchedFaults || state.applicationActive
            || state.busOperational || state.safeOutput || state.controllerBootId != bootId
            || initialSnapshot.session->ownsControlLease
            || initialSnapshot.session->controlLeaseOwnerSessionId
            || package.activeSlot != Data::ControllerSlot::A
            || package.activeGeneration != api051PackageGeneration
            || package.activeConfigurationId != api051ConfigurationId
            || package.stagedSlot != Data::ControllerSlot::A
            || package.stagedGeneration != api051PackageGeneration
            || package.stagedConfigurationId != api051ConfigurationId) {
            fail(QStringLiteral(
                "Initial state is not fault-free SHUTDOWN with lease zero and persistent "
                "A/18/3702"));
        }
    }

    command(Data::ControllerControlCommand::AcquireControl, u"AcquireControl(30000ms)", 30000);
    if (failure.isEmpty()) {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        if (!snapshot.session || !snapshot.session->ownsControlLease
            || snapshot.session->sessionId != sessionId || snapshot.session->bootId != bootId) {
            fail(QStringLiteral("The exact joined session did not acquire the exclusive lease"));
        }
    }

    command(Data::ControllerControlCommand::EnterConfigurationMode, u"EnterConfigurationMode");
    if (failure.isEmpty()) {
        Data::ControllerControlRequest discover;
        discover.command = Data::ControllerControlCommand::DiscoverTopology;
        discover.firstStationAddress = 0x1001;
        discover.topologyCapacity = 64;
        const QString error = executeApi051Command(provider, discover, operationTimeoutMs);
        if (!error.isEmpty())
            fail(QStringLiteral("DiscoverTopology failed: %1").arg(error));
    }
    if (failure.isEmpty()) {
        const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        const auto matchesSlave = [&snapshot](quint32 position,
                                              quint16 stationAddress,
                                              quint32 vendorId,
                                              quint32 productCode,
                                              quint32 revision,
                                              quint32 serial) {
            return std::count_if(
                       snapshot.topology->slaves.cbegin(),
                       snapshot.topology->slaves.cend(),
                       [=](const Data::ControllerTopologySlave &slave) {
                           return slave.position == position
                                  && slave.stationAddress == stationAddress
                                  && slave.vendorId == vendorId
                                  && slave.productCode == productCode
                                  && slave.revision == revision && slave.serial == serial;
                       })
                   == 1;
        };
        if (!snapshot.topology || !snapshot.topology->hasCompleteProvenance()
            || snapshot.topology->scope != scope
            || snapshot.topology->sessionGeneration != sessionGeneration
            || snapshot.topology->sessionId != sessionId || snapshot.topology->bootId != bootId
            || snapshot.topology->firstStationAddress != 0x1001
            || snapshot.topology->respondingCount != 3 || snapshot.topology->result != 0
            || snapshot.topology->slaves.size() != 3
            || !matchesSlave(
                0,
                0x1001,
                0x00884443,
                0x000000b6,
                0x00000001,
                0)
            || !matchesSlave(
                1,
                0x1002,
                0x00100000,
                0x000c0112,
                0x00010000,
                0)
            || !matchesSlave(
                2,
                0x1003,
                0x00100000,
                0x000c0112,
                0x00010000,
                0)) {
            fail(QStringLiteral(
                "The IDE Provider scan did not prove the exact XB6 plus two SV630N topology"));
        }
    }

    command(Data::ControllerControlCommand::RestoreActivePackage, u"RestoreActivePackage");
    if (failure.isEmpty()
        && !waitForApi051Condition(
            [&provider, bootId, sessionId, sessionGeneration] {
                const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
                if (!api051Connected(snapshot) || !snapshot.session || !snapshot.controllerState
                    || !snapshot.package || snapshot.sessionGeneration != sessionGeneration
                    || snapshot.session->sessionId != sessionId
                    || snapshot.session->bootId != bootId || !snapshot.session->ownsControlLease) {
                    return false;
                }
                const Data::ControllerStateSummary &state = *snapshot.controllerState;
                const Data::ControllerPackageSummary &package = *snapshot.package;
                return state.serviceState == Data::ControllerServiceState::OperationalSafe
                       && state.ready && state.busOperational && !state.applicationActive
                       && state.safeOutput && !state.currentFaults && !state.latchedFaults
                       && state.controllerBootId == bootId && (state.ethercatAlStateBits & 0x08)
                       && state.expectedWorkingCounter == api051ExpectedWorkingCounter
                       && state.actualWorkingCounter == api051ExpectedWorkingCounter
                       && package.activeSlot == Data::ControllerSlot::A
                       && package.activeGeneration == api051PackageGeneration
                       && package.activeConfigurationId == api051ConfigurationId
                       && package.controllerState == Data::ControllerPackageState::Active
                       && package.controllerBootId == bootId;
            },
            runtimeTimeoutMs)) {
        fail(QStringLiteral("RestoreActivePackage did not reach the strict cfg3702 OP_SAFE gate"));
    }

    std::optional<Data::SemanticRuntimeContext> context;
    Data::RuntimeResourceCatalogEpoch runtimeEpoch;
    if (failure.isEmpty()) {
        if (!waitForApi051Condition(
                [&executor, &scope, &context] {
                    context = api051Context(*executor, scope);
                    return context && context->complete;
                },
                runtimeTimeoutMs)) {
            const QString detail = context ? context->detail : QStringLiteral("no unique context");
            fail(QStringLiteral("SemanticRuntime did not become ready: %1").arg(detail));
        }
    }

    if (failure.isEmpty()) {
        const std::optional<Data::RuntimeResourceCatalog> catalog
            = provider.runtimeResourceCatalog();
        const std::optional<Data::RuntimeSemanticMappingAttestation> attestation
            = provider.runtimeSemanticMappingAttestation();
        if (!catalog || !attestation || catalog->resources.size() != 56 || catalog->scope != scope
            || catalog->sessionGeneration != sessionGeneration
            || catalog->epoch.controllerBootId != bootId
            || catalog->epoch.activePackageSlot != Data::ControllerSlot::A
            || catalog->epoch.activePackageGeneration != api051PackageGeneration
            || catalog->epoch.configurationId != api051ConfigurationId
            || catalog->epoch.catalogRevision != api051CatalogRevision
            || catalog->epoch.topologyIdentity != factoryOpaqueId(api051TopologyIdentity)
            || context->epoch != catalog->epoch
            || context->mappingDigest.value != QByteArray::fromHex(api051MappingSha256)
            || context->controllerMappingDigest.value != QByteArray::fromHex(api051MappingSha256)
            || context->cyclePeriodNs != api051CyclePeriodNs) {
            fail(QStringLiteral("Runtime catalog, epoch, mapping, or context differs from cfg3702"));
        } else {
            runtimeEpoch = catalog->epoch;
            const Utils::Result<> attestationResult = verifyRuntimeSemanticMappingAttestation(
                *attestation,
                scope,
                sessionGeneration,
                catalog->epoch,
                project.masterBindingArtifact,
                *evidence);
            if (!attestationResult) {
                fail(QStringLiteral("Controller semantic attestation failed: %1")
                         .arg(attestationResult.error()));
            }
        }
    }

    command(Data::ControllerControlCommand::StartDistributedClocks, u"StartDC");
    quint64 runningCycle = 0;
    if (failure.isEmpty()
        && !waitForApi051Condition(
            [&provider, bootId, &runningCycle] {
                const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
                if (!snapshot.session || !snapshot.session->ownsControlLease
                    || !snapshot.controllerState || !snapshot.package)
                    return false;
                const Data::ControllerStateSummary &state = *snapshot.controllerState;
                if (state.serviceState != Data::ControllerServiceState::Running || !state.ready
                    || !state.busOperational || !state.applicationActive || state.safeOutput
                    || !state.distributedClocksLocked || state.controllerBootId != bootId
                    || state.expectedWorkingCounter != api051ExpectedWorkingCounter
                    || state.actualWorkingCounter != api051ExpectedWorkingCounter
                    || state.currentFaults || state.latchedFaults || !state.cycleCount) {
                    return false;
                }
                runningCycle = state.cycleCount;
                return true;
            },
            runtimeTimeoutMs)) {
        fail(QStringLiteral("StartDC did not reach fault-free RUNNING, lock, and WKC 11/11"));
    }
    if (failure.isEmpty()
        && !waitForApi051Condition(
            [&provider, runningCycle] {
                const auto state = provider.connectionSnapshot().controllerState;
                return state && state->cycleCount > runningCycle;
            },
            5000)) {
        fail(QStringLiteral("The DC cycle counter did not advance"));
    }

    if (failure.isEmpty()) {
        context = api051Context(*executor, scope);
        if (!context || !context->complete) {
            fail(QStringLiteral("The semantic context became unavailable after StartDC"));
        } else {
            const QStringList svActions{
                QStringLiteral("embedlabs:project:action:axis0:prepare-csv"),
                QStringLiteral("embedlabs:project:action:axis0:set-csv-velocity"),
                QStringLiteral("embedlabs:project:action:axis0:stop-csv"),
                QStringLiteral("embedlabs:project:action:axis1:prepare-csv"),
                QStringLiteral("embedlabs:project:action:axis1:set-csv-velocity"),
                QStringLiteral("embedlabs:project:action:axis1:stop-csv"),
            };
            const qsizetype mutationBaseline = outputResults.size();
            for (const QString &actionId : svActions) {
                Data::SemanticOperationRequest request = api038ActionRequest(
                    *context,
                    actionId,
                    QStringLiteral("hardware/api051/sv-denied/%1/%2")
                        .arg(actionId, QUuid::createUuid().toString(QUuid::WithoutBraces)));
                request.reason = QStringLiteral("API-051 SV qualification rejection proof");
                const Data::SemanticOperationRecord rejected
                    = executor->submit(request, api038Submitter());
                if (rejected.state != Data::SemanticOperationState::Rejected
                    || rejected.executionAttempted) {
                    fail(QStringLiteral("Unqualified SV action %1 was not rejected locally")
                             .arg(actionId));
                    break;
                }
            }
            QCoreApplication::processEvents();
            if (failure.isEmpty() && outputResults.size() != mutationBaseline)
                fail(QStringLiteral("An unqualified SV action emitted an output mutation"));
        }
    }

    Data::SemanticOperationRecord setOperation;
    Data::RuntimeOutputTransactionState setOutputState;
    Data::RuntimeOutputOperationId setOutputOperationId;
    QString setPolicyCorrelation;
    if (failure.isEmpty()) {
        const QString gate = api051LiveDcGate(
            provider,
            *executor,
            scope,
            sessionGeneration,
            sessionId,
            bootId,
            runtimeEpoch);
        if (!gate.isEmpty())
            fail(QStringLiteral("Before XB6 set: %1").arg(gate));
    }
    if (failure.isEmpty()) {
        Data::SemanticOperationRequest request = api038ActionRequest(
            *context,
            u"embedlabs:project:action:xb6:set-outputs",
            QStringLiteral("hardware/api051/xb6-set/%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        setApi051DigitalOutputParameters(request, true);
        const Utils::Result<SemanticActionPlan> plan
            = buildSemanticActionPlan(*evidence, request, *context);
        if (!plan || plan->steps().size() != 1 || plan->groups().size() != 1
            || plan->steps().constFirst().completeGroupWrites().size() != 16) {
            fail(QStringLiteral("The signed XB6 set action did not produce one complete group"));
        } else {
            setOutputOperationId = plan->steps().constFirst().outputOperationId();
            setPolicyCorrelation = api051ExecutionCorrelationId(request.operationId, u"policy");
            attemptedOutputOperations.append(request.operationId);
            const Data::SemanticOperationRecord approved
                = submitAndApprove(*executor, *context, request);
            if (approved.state != Data::SemanticOperationState::Approved) {
                fail(QStringLiteral("The signed XB6 set action was not approved"));
            } else if (!waitForApi051Condition(
                           [&executor, operationId = request.operationId] {
                               const auto operation = executor->operation(operationId);
                               return operation && api051OperationTerminal(operation->state);
                           },
                           runtimeTimeoutMs)) {
                fail(QStringLiteral("The signed XB6 set action did not finish"));
            } else {
                setOperation = *executor->operation(request.operationId);
            }
        }
        if (failure.isEmpty()
            && (setOperation.state != Data::SemanticOperationState::Succeeded
                || !setOperation.executionAttempted || !setOperation.appliedCycle
                || !setOutputOperationId.isValid() || setPolicyCorrelation.isEmpty())) {
            fail(QStringLiteral("The XB6 set action did not produce one verified transaction"));
        }
        if (failure.isEmpty()) {
            const Data::RuntimeOutputGroupPolicyResult *policy
                = api051SingleCorrelatedResult(policyResults, setPolicyCorrelation);
            const Data::RuntimeOutputTransactionResult *result
                = api051SingleOutputResult(outputResults, setOutputOperationId);
            if (!policy || !result || !policy->isValid() || !policy->policy
                || policy->request.scope != scope
                || policy->request.sessionGeneration != sessionGeneration
                || policy->request.expectedEpoch != runtimeEpoch
                || policy->request.consistencyGroupId
                       != plan->groups().constFirst().consistencyGroupId()
                || policy->policy->completeGroupRecordDigest
                       != plan->groups().constFirst().completeGroupRecordDigest()
                || policy->policy->recoveryPolicy != Data::RuntimeOutputRecoveryPolicy::HoldSafe
                || policy->policy->maximumTtlCycles != api051OutputTtlCycles
                || policy->policy->completeResourceCount != 16 || !result->isValid()
                || result->request.scope != scope
                || result->request.sessionGeneration != sessionGeneration
                || result->request.expectedEpoch != runtimeEpoch
                || result->outcome != Data::RuntimeOutputTransactionOutcome::Applied
                || !result->finalResponseObserved || !result->state
                || result->request.completeGroupWrites.size() != 16
                || result->request.expectedCompleteResourceCount != 16
                || result->request.expectedCompleteGroupRecordDigest
                       != plan->groups().constFirst().completeGroupRecordDigest()
                || result->request.consistencyGroupId
                       != plan->groups().constFirst().consistencyGroupId()
                || result->request.ttlCycles != api051OutputTtlCycles
                || result->request.expectedRecoveryPolicy
                       != Data::RuntimeOutputRecoveryPolicy::HoldSafe
                || result->state->appliedCycle != setOperation.appliedCycle
                || result->state->outputGeneration
                       != result->request.expectedOutputGeneration + 1) {
                fail(QStringLiteral(
                    "The XB6 set policy, applied cycle, or output generation differs"));
            } else {
                setOutputState = *result->state;
            }
        }
        if (failure.isEmpty()) {
            QString readbackError;
            if (!api051SemanticSnapshotMatches(setOperation.afterSnapshot, true, &readbackError)) {
                fail(readbackError);
            }
        }
        if (failure.isEmpty()) {
            const QString gate = api051LiveDcGate(
                provider,
                *executor,
                scope,
                sessionGeneration,
                sessionId,
                bootId,
                runtimeEpoch);
            if (!gate.isEmpty())
                fail(QStringLiteral("After XB6 set: %1").arg(gate));
        }
    }

    Data::RuntimeOutputTransactionState safeHoldState;
    if (failure.isEmpty()) {
        if (!waitForApi051Condition(
                [&provider, expiryCycle = setOutputState.expiryCycle] {
                    const auto state = provider.connectionSnapshot().controllerState;
                    return state && state->cycleCount > expiryCycle;
                },
                5000)) {
            fail(QStringLiteral("The controller did not advance beyond the XB6 TTL expiry cycle"));
        } else {
            const QString gate = api051LiveDcGate(
                provider,
                *executor,
                scope,
                sessionGeneration,
                sessionId,
                bootId,
                runtimeEpoch);
            if (!gate.isEmpty())
                fail(QStringLiteral("Before XB6 TTL evidence: %1").arg(gate));
        }
        if (failure.isEmpty()) {
            Data::RuntimeOutputTransactionStateRequest request;
            request.correlationId = QStringLiteral("hardware/api051/ttl-state/%1")
                                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            request.scope = scope;
            request.sessionGeneration = context->sessionGeneration;
            request.expectedEpoch = context->epoch;
            request.expectedMappingDigest = context->mappingDigest.value;
            const Utils::Result<> accepted = provider.requestRuntimeOutputTransactionState(request);
            if (!accepted) {
                fail(QStringLiteral("The TTL output-state query was rejected: %1")
                         .arg(accepted.error()));
            } else if (!waitForApi051Condition(
                           [&outputStateResults, correlationId = request.correlationId] {
                               return api051SingleCorrelatedResult(
                                          outputStateResults, correlationId)
                                      != nullptr;
                           },
                           runtimeTimeoutMs)) {
                fail(QStringLiteral("The TTL output-state query did not finish"));
            } else {
                const Data::RuntimeOutputTransactionStateResult *result
                    = api051SingleCorrelatedResult(outputStateResults, request.correlationId);
                if (!result || !result->isValid() || result->request != request || !result->state
                    || result->state->state != Data::RuntimeOutputState::SafeHold
                    || !result->state->resultFlags.testFlag(
                        Data::RuntimeOutputTransactionResultFlag::SafeHold)
                    || result->state->operationId != setOutputState.operationId
                    || result->state->appliedCycle != setOutputState.appliedCycle
                    || result->state->expiryCycle != setOutputState.expiryCycle
                    || result->state->outputGeneration != setOutputState.outputGeneration + 1
                    || result->state->providerDetail != setOutputState.expiryCycle
                    || result->state->recoveryPolicy
                           != Data::RuntimeOutputRecoveryPolicy::HoldSafe) {
                    fail(QStringLiteral("The XB6 TTL did not transition atomically to HOLD_SAFE"));
                } else {
                    safeHoldState = *result->state;
                }
            }
        }
    }

    if (failure.isEmpty()) {
        const QList<Data::RuntimeResourceId> outputIds = api051Xb6OutputResourceIds(*context);
        Data::RuntimeResourceSnapshotRequest request;
        request.correlationId = QStringLiteral("hardware/api051/safe-readback/%1")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        request.scope = scope;
        request.sessionGeneration = context->sessionGeneration;
        request.expectedEpoch = context->epoch;
        request.resourceIds = outputIds;
        if (!request.isValid()) {
            fail(QStringLiteral("The signed XB6 output readback request is incomplete"));
        } else {
            const Utils::Result<> accepted = provider.requestRuntimeResourceSnapshot(request);
            if (!accepted) {
                fail(QStringLiteral("The safe-hold output readback was rejected: %1")
                         .arg(accepted.error()));
            } else if (!waitForApi051Condition(
                           [&targetedSnapshotResults, correlationId = request.correlationId] {
                               return api051SingleCorrelatedResult(
                                          targetedSnapshotResults, correlationId)
                                      != nullptr;
                           },
                           runtimeTimeoutMs)) {
                fail(QStringLiteral("The safe-hold output readback did not finish"));
            } else {
                QString readbackError;
                const Data::RuntimeResourceSnapshotResult *result
                    = api051SingleCorrelatedResult(
                        targetedSnapshotResults, request.correlationId);
                if (!result || result->request != request
                    || !api051RuntimeSnapshotAllFalse(*result, &readbackError)) {
                    fail(
                        readbackError.isEmpty()
                            ? QStringLiteral(
                                  "The safe-hold readback result was missing or miscorrelated")
                            : readbackError);
                }
            }
        }
    }

    if (failure.isEmpty()) {
        const QString gate = api051LiveDcGate(
            provider,
            *executor,
            scope,
            sessionGeneration,
            sessionId,
            bootId,
            runtimeEpoch);
        if (!gate.isEmpty())
            fail(QStringLiteral("After XB6 TTL evidence: %1").arg(gate));
    }

    if (failure.isEmpty()) {
        context = api051Context(*executor, scope);
        if (!context || !context->complete) {
            fail(QStringLiteral("The semantic context became unavailable before XB6 clear"));
        } else {
            const QString gate = api051LiveDcGate(
                provider,
                *executor,
                scope,
                sessionGeneration,
                sessionId,
                bootId,
                runtimeEpoch);
            if (!gate.isEmpty())
                fail(QStringLiteral("Before XB6 clear: %1").arg(gate));
        }
    }
    if (failure.isEmpty()) {
        Data::SemanticOperationRequest request = api038ActionRequest(
            *context,
            u"embedlabs:project:action:xb6:clear-outputs",
            QStringLiteral("hardware/api051/xb6-clear/%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        request.ttlCycles = api051OutputTtlCycles;
        request.reason = QStringLiteral("API-051 explicit XB6 clear after HOLD_SAFE");
        const Utils::Result<SemanticActionPlan> plan
            = buildSemanticActionPlan(*evidence, request, *context);
        if (!plan || plan->steps().size() != 1 || plan->groups().size() != 1
            || plan->steps().constFirst().completeGroupWrites().size() != 16
            || plan->groups().constFirst().completeResourceCount() != 16
            || plan->groups().constFirst().recoveryPolicy()
                   != Data::RuntimeOutputRecoveryPolicy::HoldSafe) {
            fail(QStringLiteral("The signed XB6 clear action did not produce one complete group"));
        } else {
            const Data::RuntimeOutputOperationId clearOutputOperationId
                = plan->steps().constFirst().outputOperationId();
            const QString clearPolicyCorrelation
                = api051ExecutionCorrelationId(request.operationId, u"policy");
            const SemanticActionPlanGroup &clearGroup = plan->groups().constFirst();
            attemptedOutputOperations.append(request.operationId);
            const Data::SemanticOperationRecord approved
                = submitAndApprove(*executor, *context, request);
            if (approved.state != Data::SemanticOperationState::Approved) {
                fail(QStringLiteral("The signed XB6 clear action was not approved"));
            } else if (!waitForApi051Condition(
                           [&executor, operationId = request.operationId] {
                               const auto operation = executor->operation(operationId);
                               return operation && api051OperationTerminal(operation->state);
                           },
                           runtimeTimeoutMs)) {
                fail(QStringLiteral("The signed XB6 clear action did not finish"));
            } else {
                const Data::SemanticOperationRecord completed = *executor->operation(
                    request.operationId);
                QString readbackError;
                const Data::RuntimeOutputGroupPolicyResult *policy
                    = api051SingleCorrelatedResult(policyResults, clearPolicyCorrelation);
                const Data::RuntimeOutputTransactionResult *result
                    = api051SingleOutputResult(outputResults, clearOutputOperationId);
                if (completed.state != Data::SemanticOperationState::Succeeded
                    || !completed.executionAttempted || !completed.appliedCycle
                    || !policy || !result
                    || !api051SemanticSnapshotMatches(
                        completed.afterSnapshot, false, &readbackError)) {
                    fail(
                        readbackError.isEmpty()
                            ? QStringLiteral("The XB6 clear action did not complete exactly once")
                            : readbackError);
                } else {
                    if (!policy->isValid() || !policy->policy
                        || policy->request.scope != scope
                        || policy->request.sessionGeneration != sessionGeneration
                        || policy->request.expectedEpoch != runtimeEpoch
                        || policy->request.consistencyGroupId
                               != clearGroup.consistencyGroupId()
                        || policy->policy->completeGroupRecordDigest
                               != clearGroup.completeGroupRecordDigest()
                        || policy->policy->completeResourceCount != 16
                        || policy->policy->recoveryPolicy
                               != Data::RuntimeOutputRecoveryPolicy::HoldSafe
                        || policy->policy->maximumTtlCycles != api051OutputTtlCycles
                        || policy->policy->currentOutputGeneration
                               != safeHoldState.outputGeneration
                        || !result->isValid()
                        || result->request.scope != scope
                        || result->request.sessionGeneration != sessionGeneration
                        || result->request.expectedEpoch != runtimeEpoch
                        || result->outcome != Data::RuntimeOutputTransactionOutcome::Applied
                        || !result->finalResponseObserved || !result->state
                        || result->request.expectedCompleteResourceCount != 16
                        || result->request.completeGroupWrites.size() != 16
                        || result->request.expectedCompleteGroupRecordDigest
                               != clearGroup.completeGroupRecordDigest()
                        || result->request.expectedRecoveryPolicy
                               != Data::RuntimeOutputRecoveryPolicy::HoldSafe
                        || result->request.expectedMaximumTtlCycles != api051OutputTtlCycles
                        || result->request.ttlCycles != api051OutputTtlCycles
                        || result->request.consistencyGroupId
                               != safeHoldState.consistencyGroupId
                        || result->request.expectedOutputGeneration
                               != safeHoldState.outputGeneration
                        || result->state->appliedCycle != completed.appliedCycle
                        || result->state->valueCount != 16
                        || result->state->consistencyGroupId
                               != result->request.consistencyGroupId
                        || result->state->recoveryPolicy
                               != Data::RuntimeOutputRecoveryPolicy::HoldSafe
                        || std::any_of(
                            result->request.completeGroupWrites.cbegin(),
                            result->request.completeGroupWrites.cend(),
                            [](const Data::RuntimeOutputValueWrite &write) {
                                return write.value.value.toBool();
                            })) {
                        fail(QStringLiteral(
                            "The XB6 clear transaction did not write the full false group"));
                    }
                }
            }
        }
    }

    if (failure.isEmpty()) {
        const QString gate = api051LiveDcGate(
            provider,
            *executor,
            scope,
            sessionGeneration,
            sessionId,
            bootId,
            runtimeEpoch);
        if (!gate.isEmpty())
            fail(QStringLiteral("After XB6 clear: %1").arg(gate));
    }

    // Cleanup is fail-closed. A lost lease, unknown/transitional state, unresolved
    // autonomous RUNNING state, or failed stop prevents Release and Disconnect.
    // It never resets a fault, uploads a package, or invokes an SV action.
    bool safeToDisconnect = false;
    if (connectionStarted) {
        const auto stableControllerState = [&provider] {
            const Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
            if (!snapshot.controllerState)
                return false;
            const Data::ControllerServiceState state = snapshot.controllerState->serviceState;
            return state == Data::ControllerServiceState::Running
                   || state == Data::ControllerServiceState::Paused
                   || state == Data::ControllerServiceState::OperationalSafe
                   || state == Data::ControllerServiceState::Shutdown
                   || state == Data::ControllerServiceState::Fault;
        };
        waitForApi051Condition(stableControllerState, operationTimeoutMs);

        const bool semanticOperationsSettled = waitForApi051Condition(
            [&executor, &attemptedOutputOperations] {
                return std::all_of(
                    attemptedOutputOperations.cbegin(),
                    attemptedOutputOperations.cend(),
                    [&executor](const Data::SemanticOperationId &operationId) {
                        const auto operation = executor->operation(operationId);
                        return operation && api051OperationTerminal(operation->state);
                    });
            },
            2000);

        Data::ControllerConnectionSnapshot snapshot = provider.connectionSnapshot();
        const auto sameOwnedSession = [&] {
            return api051Connected(snapshot) && snapshot.session
                   && snapshot.sessionGeneration == sessionGeneration
                   && snapshot.session->sessionId == sessionId
                   && snapshot.session->bootId == bootId
                   && snapshot.session->ownsControlLease
                   && snapshot.session->controlLeaseOwnerSessionId == sessionId;
        };
        if (snapshot.controllerState
            && snapshot.controllerState->serviceState
                   != Data::ControllerServiceState::Shutdown) {
            if (!sameOwnedSession()) {
                cleanupFailures.append(
                    QStringLiteral(
                        "Unresolved autonomous controller state: the exact cleanup lease/session "
                        "is unavailable"));
            } else if (snapshot.controllerState->serviceState
                       == Data::ControllerServiceState::Running
                       || snapshot.controllerState->serviceState
                              == Data::ControllerServiceState::Paused) {
                Data::ControllerControlRequest stop;
                stop.command = Data::ControllerControlCommand::ControlledStop;
                const QString error = executeApi051Command(provider, stop, operationTimeoutMs);
                if (!error.isEmpty()) {
                    cleanupFailures.append(QStringLiteral("ControlledStop: %1").arg(error));
                }
            } else if (snapshot.controllerState->serviceState
                       != Data::ControllerServiceState::OperationalSafe) {
                cleanupFailures.append(
                    QStringLiteral(
                        "Unresolved autonomous controller state: cleanup did not settle to a "
                        "stoppable state"));
            }
        } else if (!snapshot.controllerState && sessionGeneration) {
            cleanupFailures.append(
                QStringLiteral(
                    "Unresolved autonomous controller state: no final ControllerState snapshot"));
        }

        snapshot = provider.connectionSnapshot();
        if (snapshot.controllerState
            && snapshot.controllerState->serviceState
                   == Data::ControllerServiceState::OperationalSafe) {
            if (!sameOwnedSession()) {
                cleanupFailures.append(
                    QStringLiteral("OP_SAFE cleanup lost the exact Product API lease/session"));
            } else {
                Data::ControllerControlRequest configuration;
                configuration.command = Data::ControllerControlCommand::EnterConfigurationMode;
                const QString error
                    = executeApi051Command(provider, configuration, operationTimeoutMs);
                if (!error.isEmpty()) {
                    cleanupFailures.append(
                        QStringLiteral("EnterConfigurationMode: %1").arg(error));
                }
            }
        }

        snapshot = provider.connectionSnapshot();
        const bool strictShutdown
            = api051Connected(snapshot) && snapshot.session && snapshot.controllerState
              && snapshot.sessionGeneration == sessionGeneration
              && snapshot.session->sessionId == sessionId && snapshot.session->bootId == bootId
              && snapshot.controllerState->serviceState
                     == Data::ControllerServiceState::Shutdown
              && snapshot.controllerState->ready && !snapshot.controllerState->currentFaults
              && !snapshot.controllerState->latchedFaults
              && !snapshot.controllerState->applicationActive
              && !snapshot.controllerState->busOperational && !snapshot.controllerState->safeOutput
              && snapshot.controllerState->controllerBootId == bootId;
        if (!strictShutdown && sessionGeneration) {
            cleanupFailures.append(
                QStringLiteral(
                    "Unresolved autonomous controller state: final SHUTDOWN/fault0 was not "
                    "proven; Release and Disconnect were withheld%1")
                    .arg(
                        semanticOperationsSettled
                            ? QString()
                            : QStringLiteral(" with an unresolved output operation")));
        } else if (strictShutdown) {
            if (snapshot.session->ownsControlLease) {
                Data::ControllerControlRequest release;
                release.command = Data::ControllerControlCommand::ReleaseControl;
                const QString error = executeApi051Command(provider, release, operationTimeoutMs);
                if (!error.isEmpty())
                    cleanupFailures.append(QStringLiteral("ReleaseControl: %1").arg(error));
            }
            snapshot = provider.connectionSnapshot();
            safeToDisconnect
                = api051Connected(snapshot) && snapshot.session && snapshot.controllerState
                  && snapshot.sessionGeneration == sessionGeneration
                  && snapshot.session->sessionId == sessionId && snapshot.session->bootId == bootId
                  && !snapshot.session->ownsControlLease
                  && !snapshot.session->controlLeaseOwnerSessionId
                  && snapshot.controllerState->serviceState
                         == Data::ControllerServiceState::Shutdown
                  && !snapshot.controllerState->currentFaults
                  && !snapshot.controllerState->latchedFaults
                  && !snapshot.controllerState->applicationActive
                  && !snapshot.controllerState->busOperational;
            if (!safeToDisconnect) {
                cleanupFailures.append(
                    QStringLiteral("Final SHUTDOWN/fault0/lease0 readback was not proven"));
            }
        } else {
            // A connection that never established a session cannot own an
            // autonomous runtime and may be closed to restore the endpoint.
            safeToDisconnect = !sessionGeneration;
        }

        if (safeToDisconnect) {
            const Utils::Result<> disconnected = provider.disconnectFromController();
            if (!disconnected) {
                cleanupFailures.append(QStringLiteral("Disconnect: %1").arg(disconnected.error()));
            } else if (!waitForApi051Condition(
                           [&provider] {
                               return provider.connectionSnapshot().state
                                      == Data::ControllerConnectionState::Disconnected;
                           },
                           connectTimeoutMs)) {
                cleanupFailures.append(QStringLiteral("Disconnect did not finish"));
            }
        }
    }

    const Data::ControllerConnectionState finalProviderState
        = provider.connectionSnapshot().state;
    if (safeToDisconnect
        && finalProviderState != Data::ControllerConnectionState::Disconnected) {
        cleanupFailures.append(QStringLiteral("Product API provider is not disconnected"));
    }
    if (endpointConfigured && previousProfile.endpoint != host) {
        if (finalProviderState != Data::ControllerConnectionState::Disconnected
            && finalProviderState != Data::ControllerConnectionState::Failed) {
            cleanupFailures.append(
                QStringLiteral("Controller endpoint was not restored because cleanup is unresolved"));
        } else {
            const Utils::Result<> restored
                = provider.setConnectionProfileEndpoint(scope, profile.id, previousProfile.endpoint);
            const auto restoredConfiguration
                = provider.connectionProfileConfiguration(scope, profile.id);
            if (!restored || !restoredConfiguration
                || restoredConfiguration->endpoint != previousProfile.endpoint) {
                cleanupFailures.append(
                    QStringLiteral("Restore controller endpoint: %1")
                        .arg(restored ? QStringLiteral("readback mismatch") : restored.error()));
            }
        }
    }
    if (!cleanupFailures.isEmpty()) {
        const QString cleanup = cleanupFailures.join(QStringLiteral("; "));
        if (failure.isEmpty())
            failure = QStringLiteral("API-051 cleanup failed: %1").arg(cleanup);
        else
            failure += QStringLiteral("; cleanup: %1").arg(cleanup);
    }
    if (!failure.isEmpty()) {
        const QByteArray detail = failure.toUtf8();
        QFAIL(detail.constData());
    }

    qInfo().noquote() << "[API-051 semantic hardware] PASS cfg3702 DC/WKC11 XB6 set/readback/"
                         "HOLD_SAFE/clear; SV mutations=0; final SHUTDOWN/fault0/lease0";
}

void EtherCATSemanticRuntimeTests::testTrustedRuntimePackageActivation()
{
    {
        ActivationFixture preparedFixture;
        const Utils::Result<> preparedInitialized
            = preparedFixture.initialize(
                ActivationControllerProvider::DeploymentMode::Succeed,
                QStringLiteral("operation/runtime-activation/prepared"));
        QVERIFY_RESULT(preparedInitialized);
        QVERIFY(std::all_of(
            preparedFixture.ownedAdapterProvider->manifests.cbegin(),
            preparedFixture.ownedAdapterProvider->manifests.cend(),
            [](const Data::DeviceAdapterManifest &manifest) {
                return manifest.signatureVerified
                       && manifest.realHardwareAllowed;
            }));
        const Data::RuntimePackageCompilerVerifyResult verification
            = preparedFixture.compilerVerification();
        QVERIFY(verification.isSuccess());
        const QByteArray currentQtProjectBytes
            = QByteArray("current-qt-ecatproject-bytes\n");
        preparedFixture.projects.activationCapture
            = Data::RuntimePackageActivationProjectCapture{
                preparedFixture.project,
                currentQtProjectBytes,
                1,
                preparedFixture.documentRevision,
                preparedFixture.originalBinding,
            };
        QVERIFY(preparedFixture.projects.activationCapture->isValid());
        QVERIFY(
            preparedFixture.projects.activationCapture->serializedProject()
            != preparedFixture.projectBytes);
        const auto prepare = [&preparedFixture, &verification] {
            return preparedFixture.service->prepare({
                Data::RuntimePackageActivationOperationId{
                    QStringLiteral("operation/runtime-activation/prepared")},
                projectScope(preparedFixture.project),
                preparedFixture.packageBytes,
                preparedFixture.projectBytes,
                preparedFixture.effectiveProjectCompanion,
                verification,
                true,
                preparedFixture.compilerActivationProof(),
            });
        };
        preparedFixture.setAdapterAuthorization(false, true);
        const Core::RuntimePackageActivationPreparationResult unsignedAdapter
            = prepare();
        QVERIFY(unsignedAdapter.isValid());
        QVERIFY(!unsignedAdapter.succeeded());
        QVERIFY(preparedFixture.provider.controlRequests.isEmpty());
        QVERIFY(preparedFixture.provider.deploymentRequests.isEmpty());
        preparedFixture.setAdapterAuthorization(true, false);
        const Core::RuntimePackageActivationPreparationResult hardwareDenied
            = prepare();
        QVERIFY(hardwareDenied.isValid());
        QVERIFY(!hardwareDenied.succeeded());
        QVERIFY(preparedFixture.provider.controlRequests.isEmpty());
        QVERIFY(preparedFixture.provider.deploymentRequests.isEmpty());
        preparedFixture.setAdapterAuthorization(true, true);
        const Core::RuntimePackageActivationPreparationResult prepared
            = prepare();
        QVERIFY(prepared.isValid());
        QVERIFY(prepared.succeeded());
        QVERIFY(prepared.request);
        QCOMPARE(
            prepared.request->identity().bindingArtifactId(),
            QStringLiteral("sha256:%1")
                .arg(QString::fromLatin1(
                    preparedFixture.evidence->semanticMappingProof()
                        .mappingSha256.toHex())));
        QCOMPARE(
            prepared.request->compiledProjectSource(),
            preparedFixture.projectBytes);
        QCOMPARE(
            prepared.request->identity().originalBindingToken(),
            preparedFixture.originalBinding);
        QVERIFY(preparedFixture.service->start(*prepared.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            preparedFixture.service
                    ->record(prepared.request->identity().operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    preparedFixture.service
                        ->record(prepared.request->identity().operationId())
                        ->outcome()),
            5000);
        QCOMPARE(
            preparedFixture.service
                ->record(prepared.request->identity().operationId())
                ->outcome(),
            Data::RuntimePackageActivationOutcome::
                SucceededWithActivatedPackage);
        QCOMPARE(
            preparedFixture.provider.deploymentRequests.size(),
            qsizetype(1));
        QCOMPARE(
            preparedFixture.provider.deploymentRequests.constFirst()
                .compiledProjectSource,
            preparedFixture.projectBytes);
        QVERIFY(
            preparedFixture.provider.deploymentRequests.constFirst()
                .compiledProjectSource
            != currentQtProjectBytes);
    }

    ActivationFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);

    const Core::RuntimePackageActivationCommandResult accepted
        = fixture.service->start(fixture.request);
    QVERIFY(accepted.isValid());
    QCOMPARE(
        accepted.disposition,
        Core::RuntimePackageActivationCommandDisposition::Accepted);

    const Core::RuntimePackageActivationCommandResult earlyReplay
        = fixture.service->start(fixture.request);
    QVERIFY(earlyReplay.isValid());
    QCOMPARE(
        earlyReplay.disposition,
        Core::RuntimePackageActivationCommandDisposition::IdempotentReplay);

    QTRY_VERIFY_WITH_TIMEOUT(
        fixture.service->record(fixture.identity.operationId())
            && Data::runtimePackageActivationOutcomeIsTerminal(
                fixture.service->record(fixture.identity.operationId())->outcome()),
        5000);
    const Data::RuntimePackageActivationRecord record
        = *fixture.service->record(fixture.identity.operationId());
    QVERIFY(record.isValid());
    QCOMPARE(
        record.outcome(),
        Data::RuntimePackageActivationOutcome::SucceededWithActivatedPackage);
    QVERIFY(record.projectCommit());
    QCOMPARE(record.projectCommit()->resultingBinding(), fixture.targetBinding);
    QCOMPARE(fixture.projects.activationCaptureCalls, 6);
    QCOMPARE(fixture.projects.activationCompareAndSetCalls, 1);
    QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
    QCOMPARE(
        fixture.provider.controlRequests.at(0).command,
        Data::ControllerControlCommand::AcquireControl);
    QCOMPARE(
        fixture.provider.controlRequests.at(1).command,
        Data::ControllerControlCommand::ReleaseControl);
    QCOMPARE(fixture.pendingGuardCount(), 0);

    const Core::RuntimePackageActivationCommandResult terminalReplay
        = fixture.service->start(fixture.request);
    QVERIFY(terminalReplay.isValid());
    QCOMPARE(
        terminalReplay.disposition,
        Core::RuntimePackageActivationCommandDisposition::IdempotentReplay);
    QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
    QCOMPARE(fixture.projects.activationCompareAndSetCalls, 1);

    ActivationFixture existing;
    const Utils::Result<> existingInitialized = existing.initialize(
        ActivationControllerProvider::DeploymentMode::Succeed,
        QStringLiteral("operation/runtime-activation/existing"));
    QVERIFY_RESULT(existingInitialized);
    existing.provider.snapshot.package->activeSlot
        = existing.provider.candidate.slot;
    existing.provider.snapshot.package->activeGeneration
        = existing.provider.candidate.generation;
    existing.provider.snapshot.package->activeConfigurationId
        = existing.provider.candidate.configurationId;
    existing.provider.snapshot.package->controllerState
        = Data::ControllerPackageState::Active;
    existing.provider.snapshot.controllerState->serviceState
        = Data::ControllerServiceState::OperationalSafe;
    QVERIFY(existing.service->start(existing.request).accepted());
    QTRY_VERIFY_WITH_TIMEOUT(
        existing.service->record(existing.identity.operationId())
            && Data::runtimePackageActivationOutcomeIsTerminal(
                existing.service->record(existing.identity.operationId())
                    ->outcome()),
        5000);
    const auto existingRecord
        = existing.service->record(existing.identity.operationId());
    QVERIFY(existingRecord);
    QVERIFY(existingRecord->isValid());
    QCOMPARE(
        existingRecord->outcome(),
        Data::RuntimePackageActivationOutcome::
            SucceededWithExistingPackage);
    QVERIFY(existing.provider.controlRequests.isEmpty());
    QVERIFY(existing.provider.deploymentRequests.isEmpty());
    QCOMPARE(existing.projects.activationCompareAndSetCalls, 1);
    QCOMPARE(existing.pendingGuardCount(), 0);

    QList<Data::RuntimePackageActivationAuditEvent> journalFailureAudit;
    for (const Data::RuntimePackageActivationAuditEvent &event :
         existingRecord->audit()) {
        journalFailureAudit.append(event);
        if (event.code() == QStringLiteral("controller-before"))
            break;
    }
    QVERIFY(!journalFailureAudit.isEmpty());
    QCOMPARE(
        journalFailureAudit.constLast().code(),
        QStringLiteral("controller-before"));
    const QString journalFailureDetail
        = QStringLiteral("The activation journal could not be committed.");
    const QDateTime journalFailureAt
        = journalFailureAudit.constLast().occurredAt().addMSecs(1);
    journalFailureAudit.append({
        quint64(journalFailureAudit.size() + 1),
        Data::RuntimePackageActivationAuditKind::PhaseTransition,
        Data::RuntimePackageActivationPhase::AwaitingReconciliation,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        Data::RuntimePackageActivationProviderAction::None,
        Data::RuntimePackageActivationProviderReconciliation::None,
        {},
        0,
        0,
        0,
        {},
        {},
        {},
        {},
        QStringLiteral("activation-journal-update-failed"),
        journalFailureDetail,
        journalFailureAt,
    });
    const Data::RuntimePackageActivationRecord journalFailureRecord{
        existingRecord->identity(),
        existingRecord->requestFingerprint(),
        existingRecord->rollbackOnActivationFailure(),
        quint64(journalFailureAudit.size()),
        Data::RuntimePackageActivationPhase::AwaitingReconciliation,
        Data::RuntimePackageActivationOutcome::OutcomeUnknown,
        journalFailureAudit,
        journalFailureDetail,
        existingRecord->startedAt(),
        journalFailureAt,
        {},
        existingRecord->beforeController(),
    };
    QVERIFY(journalFailureRecord.isValid());
    QVERIFY(journalFailureRecord.needsReconciliation());

    ActivationFixture recoveredExisting;
    const Utils::Result<> recoveredExistingInitialized
        = recoveredExisting.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral(
                "operation/runtime-activation/existing-crash"));
    QVERIFY_RESULT(recoveredExistingInitialized);
    recoveredExisting.provider.snapshot.package->activeSlot
        = recoveredExisting.provider.candidate.slot;
    recoveredExisting.provider.snapshot.package->activeGeneration
        = recoveredExisting.provider.candidate.generation;
    recoveredExisting.provider.snapshot.package->activeConfigurationId
        = recoveredExisting.provider.candidate.configurationId;
    recoveredExisting.provider.snapshot.package->controllerState
        = Data::ControllerPackageState::Active;
    recoveredExisting.provider.snapshot.controllerState->serviceState
        = Data::ControllerServiceState::OperationalSafe;
    QObject::connect(
        &recoveredExisting.projects,
        &Core::ProjectService::projectChanged,
        &recoveredExisting.provider,
        [&recoveredExisting] {
            QTimer::singleShot(
                0,
                &recoveredExisting.provider,
                [&recoveredExisting] {
                    recoveredExisting.service.reset();
                });
        });
    QVERIFY(
        recoveredExisting.service->start(recoveredExisting.request).accepted());
    QTRY_VERIFY_WITH_TIMEOUT(!recoveredExisting.service, 5000);
    QCOMPARE(recoveredExisting.pendingGuardCount(), 1);

    auto restartedExisting
        = std::make_unique<TrustedRuntimePackageActivationService>(
            &recoveredExisting.projects,
            recoveredExisting.registry.get(),
            recoveredExisting.repository,
            recoveredExisting.journalRoot);
    QVERIFY(restartedExisting->hasUnresolvedRecoveryBarrier());
    const Core::RuntimePackageActivationCommandResult recoveredResult
        = restartedExisting->reconcile(
            recoveredExisting.identity.operationId());
    QVERIFY(recoveredResult.isValid());
    QCOMPARE(
        recoveredResult.disposition,
        Core::RuntimePackageActivationCommandDisposition::Accepted);
    QTRY_VERIFY_WITH_TIMEOUT(
        restartedExisting->record(
            recoveredExisting.identity.operationId())
            && Data::runtimePackageActivationOutcomeIsTerminal(
                restartedExisting
                    ->record(recoveredExisting.identity.operationId())
                    ->outcome()),
        5000);
    QCOMPARE(
        restartedExisting
            ->record(recoveredExisting.identity.operationId())
            ->outcome(),
        Data::RuntimePackageActivationOutcome::
            SucceededWithExistingPackage);
    QVERIFY(recoveredExisting.provider.controlRequests.isEmpty());
    QVERIFY(recoveredExisting.provider.deploymentRequests.isEmpty());
    QCOMPARE(recoveredExisting.pendingGuardCount(), 0);
}

void EtherCATSemanticRuntimeTests::
    testTrustedRuntimePackageActivationFailsClosed()
{
    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/no-project-proof"),
            false);
        QVERIFY_RESULT(initialized);
        fixture.service
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot);
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                fixture.projectBytes,
                fixture.effectiveProjectCompanion,
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(prepared.detail.contains(
            QStringLiteral(
                "Compile-time project proof verification is unavailable")));
        QCOMPARE(fixture.projects.activationCaptureCalls, 0);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/project-proof-rejected"),
            false);
        QVERIFY_RESULT(initialized);
        fixture.service
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot,
                nullptr,
                15000,
                45000,
                std::function<bool()>{},
                [](
                    const Core::RuntimePackageActivationPreparationRequest &,
                    const Data::RuntimePackageActivationProjectCapture &,
                    const VerifiedRuntimePackageEvidence &)
                    -> Utils::Result<> {
                    return Utils::ResultError(
                        "The signed project projection receipt differs");
                });
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                fixture.projectBytes,
                fixture.effectiveProjectCompanion,
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(prepared.detail.contains(
            QStringLiteral(
                "Compile-time project proof verification failed")));
        QVERIFY(prepared.detail.contains(
            QStringLiteral("signed project projection receipt differs")));
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/journal-update"));
        QVERIFY_RESULT(initialized);
        int journalCommits = 0;
        fixture.service
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot,
                nullptr,
                15000,
                45000,
                [&journalCommits] { return ++journalCommits == 3; },
                fixture.exactProjectProofVerifier());
        QVERIFY_RESULT(fixture.prepareCurrentRequest());
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            5000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->audit().constLast().code(),
            QStringLiteral("activation-journal-update-failed"));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(1));
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 1);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/project-journal"));
        QVERIFY_RESULT(initialized);
        fixture.service
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot,
                nullptr,
                15000,
                45000,
                [&fixture] {
                    return fixture.projects.activationSaveCalls > 0;
                },
                fixture.exactProjectProofVerifier());
        QVERIFY_RESULT(fixture.prepareCurrentRequest());
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            5000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->audit().constLast().code(),
            QStringLiteral("activation-journal-update-failed"));
        QCOMPARE(fixture.projects.activationCompareAndSetCalls, 1);
        QCOMPARE(fixture.projects.activationSaveCalls, 1);
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(1));
        QCOMPARE(
            fixture.provider.controlRequests.constFirst().command,
            Data::ControllerControlCommand::AcquireControl);
        QCOMPARE(fixture.pendingGuardCount(), 1);

        fixture.service.reset();
        auto restarted
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot);
        QVERIFY(restarted->hasUnresolvedRecoveryBarrier());
        const auto reconciliation
            = restarted->reconcile(fixture.identity.operationId());
        QVERIFY(reconciliation.isValid());
        QCOMPARE(
            reconciliation.disposition,
            Core::RuntimePackageActivationCommandDisposition::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.provider.snapshot.session
                && !fixture.provider.snapshot.session->ownsControlLease,
            5000);
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
        QCOMPARE(
            fixture.provider.controlRequests.constLast().command,
            Data::ControllerControlCommand::ReleaseControl);
        const auto duplicate
            = restarted->reconcile(fixture.identity.operationId());
        QVERIFY(duplicate.isValid());
        QCOMPARE(
            duplicate.disposition,
            Core::RuntimePackageActivationCommandDisposition::
                ReconciliationRequired);
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/existing-project-journal"));
        QVERIFY_RESULT(initialized);
        fixture.provider.snapshot.package->activeSlot
            = fixture.provider.candidate.slot;
        fixture.provider.snapshot.package->activeGeneration
            = fixture.provider.candidate.generation;
        fixture.provider.snapshot.package->activeConfigurationId
            = fixture.provider.candidate.configurationId;
        fixture.provider.snapshot.package->controllerState
            = Data::ControllerPackageState::Active;
        fixture.provider.snapshot.controllerState->serviceState
            = Data::ControllerServiceState::OperationalSafe;
        fixture.service
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot,
                nullptr,
                15000,
                45000,
                [&fixture] {
                    return fixture.projects.activationSaveCalls > 0;
                },
                fixture.exactProjectProofVerifier());
        QVERIFY_RESULT(fixture.prepareCurrentRequest());
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            5000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(fixture.projects.activationCompareAndSetCalls, 1);
        QCOMPARE(fixture.projects.activationSaveCalls, 1);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 1);

        fixture.service.reset();
        auto restarted
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot);
        QVERIFY(restarted->hasUnresolvedRecoveryBarrier());
        const auto reconciliation
            = restarted->reconcile(fixture.identity.operationId());
        QVERIFY(reconciliation.isValid());
        QCOMPARE(
            reconciliation.disposition,
            Core::RuntimePackageActivationCommandDisposition::
                ReconciliationRequired);
        const auto recovered
            = restarted->record(fixture.identity.operationId());
        QVERIFY(recovered);
        QVERIFY(recovered->isValid());
        QVERIFY(recovered->needsReconciliation());
        QCOMPARE(
            recovered->outcome(),
            Data::RuntimePackageActivationOutcome::OutcomeUnknown);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 1);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/preparation-missing-lower"));
        QVERIFY_RESULT(initialized);
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                {},
                fixture.effectiveProjectCompanion,
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/preparation-wrong-lower"));
        QVERIFY_RESULT(initialized);
        QByteArray wrongLowerProject = fixture.projectBytes;
        wrongLowerProject.append(' ');
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                wrongLowerProject,
                fixture.effectiveProjectCompanion,
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral(
                "operation/runtime-activation/preparation-companion-lower"));
        QVERIFY_RESULT(initialized);
        QJsonObject companionObject
            = QJsonDocument::fromJson(
                  fixture.effectiveProjectCompanion)
                  .object();
        companionObject.insert(
            QStringLiteral("compiled_project_sha256"),
            QString::fromLatin1(
                QCryptographicHash::hash(
                    QByteArrayView("different-lower-project"),
                    QCryptographicHash::Sha256)
                    .toHex()));
        const QByteArray changedCompanion
            = QJsonDocument(companionObject).toJson(
                  QJsonDocument::Compact)
              + '\n';
        const Data::RuntimePackageCompilerVerifyResult verification
            = fixture.compilerVerificationForCompanion(
                changedCompanion);
        QVERIFY(verification.isSuccess());
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                fixture.projectBytes,
                changedCompanion,
                verification,
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral(
                "operation/runtime-activation/preparation-document-revision"));
        QVERIFY_RESULT(initialized);
        fixture.projects.activationCapture
            = Data::RuntimePackageActivationProjectCapture{
                fixture.project,
                QByteArray("newer-current-qt-project\n"),
                2,
                fixture.documentRevision,
                fixture.originalBinding,
            };
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                fixture.projectBytes,
                fixture.effectiveProjectCompanion,
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/preparation-companion"));
        QVERIFY_RESULT(initialized);
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                fixture.projectBytes,
                QByteArray("different-effective-project-companion\n"),
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/preparation-upper-target"));
        QVERIFY_RESULT(initialized);
        Data::ProjectSnapshot changedProject = fixture.project;
        changedProject.slaves.first().adapterSelection.adapterContentSha256[0]
            ^= 1;
        fixture.projects.activationCapture
            = Data::RuntimePackageActivationProjectCapture{
                changedProject,
                fixture.projectBytes,
                1,
                fixture.documentRevision,
                fixture.originalBinding,
            };
        const Core::RuntimePackageActivationPreparationResult prepared
            = fixture.service->prepare({
                fixture.identity.operationId(),
                projectScope(fixture.project),
                fixture.packageBytes,
                fixture.projectBytes,
                fixture.effectiveProjectCompanion,
                fixture.compilerVerification(),
                true,
                fixture.compilerActivationProof(),
            });
        QVERIFY(prepared.isValid());
        QVERIFY(!prepared.succeeded());
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/unprepared"),
            false);
        QVERIFY_RESULT(initialized);
        QVERIFY(fixture.request.isValid());
        const auto rejected = fixture.service->start(fixture.request);
        QVERIFY(rejected.isValid());
        QCOMPARE(
            rejected.disposition,
            Core::RuntimePackageActivationCommandDisposition::InvalidRequest);
        QVERIFY(!fixture.service->record(fixture.identity.operationId()));
        QCOMPARE(fixture.projects.activationCaptureCalls, 0);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/signature"));
        QVERIFY_RESULT(initialized);
        QByteArray tampered = fixture.packageBytes;
        tampered[tampered.size() / 2] ^= 1;
        const Data::RuntimePackageActivationRequest request
            = fixture.requestWithPackage(tampered);
        QVERIFY(request.isValid());
        const auto rejected = fixture.service->start(request);
        QVERIFY(rejected.isValid());
        QCOMPARE(
            rejected.disposition,
            Core::RuntimePackageActivationCommandDisposition::InvalidRequest);
        QVERIFY(!fixture.service->record(request.identity().operationId()));
        QCOMPARE(fixture.projects.activationCaptureCalls, 1);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/project-stale"));
        QVERIFY_RESULT(initialized);
        fixture.projects.activationCapture
            = Data::RuntimePackageActivationProjectCapture{
                fixture.project,
                fixture.projectBytes + QByteArray(" "),
                1,
                Data::RuntimePackageActivationDocumentRevisionToken{
                    QByteArray("different-document-revision")},
                fixture.originalBinding,
            };
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        QCOMPARE(
            fixture.service->record(fixture.identity.operationId())->outcome(),
            Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/topology-stale"));
        QVERIFY_RESULT(initialized);
        ++fixture.provider.snapshot.topology->slaves[0].stationAddress;
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        QCOMPARE(
            fixture.service->record(fixture.identity.operationId())->outcome(),
            Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/topology-no-cpu1-sequence"));
        QVERIFY_RESULT(initialized);
        fixture.provider.snapshot.topology->cpu1RequestSequence = 0;
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        QCOMPARE(
            fixture.service->record(fixture.identity.operationId())->outcome(),
            Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/topology-no-cpu1-time"));
        QVERIFY_RESULT(initialized);
        fixture.provider.snapshot.topology->cpu1CompletedTimeNs = 0;
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        QCOMPARE(
            fixture.service->record(fixture.identity.operationId())->outcome(),
            Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange);
        QVERIFY(fixture.provider.controlRequests.isEmpty());
        QVERIFY(fixture.provider.deploymentRequests.isEmpty());
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::FailTerminal,
            QStringLiteral("operation/runtime-activation/deploy-failed"));
        QVERIFY_RESULT(initialized);
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        const auto record = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->outcome(),
            Data::RuntimePackageActivationOutcome::FailedWithoutControllerChange);
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
        QCOMPARE(
            fixture.provider.controlRequests.constLast().command,
            Data::ControllerControlCommand::ReleaseControl);
        QCOMPARE(fixture.projects.activationCompareAndSetCalls, 0);
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/cas-stale"));
        QVERIFY_RESULT(initialized);
        fixture.projects.activationCompareAndSetStale = true;
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        const auto record = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->outcome(),
            Data::RuntimePackageActivationOutcome::
                FailedControllerChangedWithoutBinding);
        QCOMPARE(fixture.projects.activationCompareAndSetCalls, 1);
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Succeed,
            QStringLiteral("operation/runtime-activation/acquire-timeout"));
        QVERIFY_RESULT(initialized);
        fixture.provider.controlMode
            = ActivationControllerProvider::ControlMode::StallAcquire;
        QVERIFY_RESULT(fixture.resetServiceWithProviderDeadline(25));
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            1000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(record->audit().constLast().code(), QString("acquire-timeout"));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(1));
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(0));
        QCOMPARE(fixture.pendingGuardCount(), 1);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::
                ProgressThenSucceed,
            QStringLiteral("operation/runtime-activation/deployment-progress"));
        QVERIFY_RESULT(initialized);
        QVERIFY_RESULT(fixture.resetServiceWithProviderDeadline(25));
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    fixture.service->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->outcome(),
            Data::RuntimePackageActivationOutcome::
                SucceededWithActivatedPackage);
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::Stall,
            QStringLiteral("operation/runtime-activation/deployment-timeout"));
        QVERIFY_RESULT(initialized);
        QVERIFY_RESULT(fixture.resetServiceWithProviderDeadline(25));
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            1000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->audit().constLast().code(),
            QString("deployment-timeout"));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(1));
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
        QCOMPARE(fixture.pendingGuardCount(), 1);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::FailTerminal,
            QStringLiteral("operation/runtime-activation/release-timeout"));
        QVERIFY_RESULT(initialized);
        fixture.provider.controlMode
            = ActivationControllerProvider::ControlMode::StallRelease;
        QVERIFY_RESULT(fixture.resetServiceWithProviderDeadline(25));
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            1000);
        const auto record
            = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(record->audit().constLast().code(), QString("release-timeout"));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
        QCOMPARE(
            fixture.provider.controlRequests.constLast().command,
            Data::ControllerControlCommand::ReleaseControl);
        QCOMPARE(fixture.pendingGuardCount(), 1);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::OutcomeUnknown,
            QStringLiteral("operation/runtime-activation/unknown"));
        QVERIFY_RESULT(initialized);
        const auto accepted = fixture.service->start(fixture.request);
        QVERIFY(accepted.accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            5000);
        const auto record = fixture.service->record(fixture.identity.operationId());
        QVERIFY(record);
        QVERIFY(record->isValid());
        QCOMPARE(
            record->outcome(),
            Data::RuntimePackageActivationOutcome::OutcomeUnknown);
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(1));
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
        QCOMPARE(fixture.pendingGuardCount(), 1);

        fixture.service.reset();
        auto restarted
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot);
        QVERIFY(restarted->hasUnresolvedRecoveryBarrier());
        const auto blocked = restarted->start(fixture.request);
        QVERIFY(blocked.isValid());
        QCOMPARE(
            blocked.disposition,
            Core::RuntimePackageActivationCommandDisposition::IdempotentReplay);
        QVERIFY(blocked.record);
        QVERIFY(blocked.record->needsReconciliation());
        QCOMPARE(fixture.provider.deploymentRequests.size(), qsizetype(1));
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(1));

        const auto reconciliation
            = restarted->reconcile(fixture.identity.operationId());
        QVERIFY(reconciliation.isValid());
        QCOMPARE(
            reconciliation.disposition,
            Core::RuntimePackageActivationCommandDisposition::Accepted);
        const auto duplicateReconciliation
            = restarted->reconcile(fixture.identity.operationId());
        QVERIFY(duplicateReconciliation.isValid());
        QCOMPARE(
            duplicateReconciliation.disposition,
            Core::RuntimePackageActivationCommandDisposition::
                ReconciliationRequired);
        QTRY_VERIFY_WITH_TIMEOUT(
            restarted->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    restarted->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        const auto reconciled
            = restarted->record(fixture.identity.operationId());
        QVERIFY(reconciled);
        QVERIFY(reconciled->isValid());
        QCOMPARE(
            reconciled->outcome(),
            Data::RuntimePackageActivationOutcome::
                FailedWithoutControllerChange);
        QCOMPARE(fixture.provider.controlRequests.size(), qsizetype(2));
        QCOMPARE(
            fixture.provider.controlRequests.constLast().command,
            Data::ControllerControlCommand::ReleaseControl);
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }

    {
        ActivationFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(
            ActivationControllerProvider::DeploymentMode::OutcomeUnknown,
            QStringLiteral("operation/runtime-activation/recovery-no-target"));
        QVERIFY_RESULT(initialized);
        QVERIFY(fixture.service->start(fixture.request).accepted());
        QTRY_VERIFY_WITH_TIMEOUT(
            fixture.service->record(fixture.identity.operationId())
                && fixture.service->record(fixture.identity.operationId())
                           ->needsReconciliation(),
            5000);
        fixture.service.reset();
        fixture.projects.activationCapture.reset();
        fixture.provider.snapshot.package->activeSlot
            = fixture.provider.candidate.slot;
        fixture.provider.snapshot.package->activeGeneration
            = fixture.provider.candidate.generation;
        fixture.provider.snapshot.package->activeConfigurationId
            = fixture.provider.candidate.configurationId;
        fixture.provider.snapshot.package->controllerState
            = Data::ControllerPackageState::Active;
        fixture.provider.snapshot.controllerState->serviceState
            = Data::ControllerServiceState::OperationalSafe;

        auto restarted
            = std::make_unique<TrustedRuntimePackageActivationService>(
                &fixture.projects,
                fixture.registry.get(),
                fixture.repository,
                fixture.journalRoot);
        const auto reconciliation
            = restarted->reconcile(fixture.identity.operationId());
        QVERIFY(reconciliation.isValid());
        QCOMPARE(
            reconciliation.disposition,
            Core::RuntimePackageActivationCommandDisposition::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(
            restarted->record(fixture.identity.operationId())
                && Data::runtimePackageActivationOutcomeIsTerminal(
                    restarted->record(fixture.identity.operationId())
                        ->outcome()),
            5000);
        const auto terminal
            = restarted->record(fixture.identity.operationId());
        QVERIFY(terminal);
        QVERIFY(terminal->isValid());
        QCOMPARE(
            terminal->outcome(),
            Data::RuntimePackageActivationOutcome::
                FailedControllerChangedWithoutBinding);
        QCOMPARE(fixture.pendingGuardCount(), 0);
    }
}

void EtherCATSemanticRuntimeTests::testInstalledProductionTrustAnchor()
{
    const QString keyId = QStringLiteral(
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6");
    const Utils::FilePath trustDirectory
        = ::Core::ICore::resourcePath("ethercat/production-trust");
    const Utils::Result<QByteArray> publicKey
        = (trustDirectory / (keyId + ".pub")).fileContents();
    QVERIFY_RESULT(publicKey);
    QCOMPARE(publicKey->size(), qsizetype(32));
    QCOMPARE(
        QString::fromLatin1(
            QCryptographicHash::hash(*publicKey, QCryptographicHash::Sha256).toHex()),
        keyId);
    const Utils::Result<QList<EcpkgTrustedPublicKey>> trust
        = loadProductionEcpkgTrustStore(trustDirectory.toFSPathString());
    QVERIFY_RESULT(trust);
    QCOMPARE(trust->size(), qsizetype(1));
    QCOMPARE(trust->constFirst().rawPublicKey, *publicKey);
    QCOMPARE(trust->constFirst().trust, EcpkgTrustClass::Production);
}

void EtherCATSemanticRuntimeTests::testReadOnlySemanticBindingFactory()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(repositoryRoot.absoluteFilePath(
        "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath("artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray projectBytes = readFile(
        fixtureRoot.absoluteFilePath("package_inputs/project.json"));
    const QByteArray publicKey = readFile(fixtureRoot.absoluteFilePath(
        "trust/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"));
    if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.isEmpty())
        QSKIP("Transferred API-037 binding factory fixture is not present");

    const Utils::Result<VerifiedEcpkgPackage> package = verifyProductionEcpkg(
        packageBytes, {{publicKey, EcpkgTrustClass::Production}}, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidenceResult
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidenceResult);
    const VerifiedRuntimePackageEvidence evidence = *evidenceResult;

    Data::ProjectSnapshot project = factoryProject(evidence);
    const Data::RuntimeResourceCatalog catalog = factoryCatalog(project, evidence);
    const Data::RuntimeSemanticMappingAttestation attestation
        = factoryAttestation(catalog, evidence);
    QVERIFY(attestation.isValid());

    const Utils::Result<ReadOnlySemanticBindingCandidates> candidates
        = buildReadOnlySemanticBindingCandidates(
            u"embed-labs.product-api", project, evidence, catalog, attestation);
    QVERIFY_RESULT(candidates);
    QCOMPARE(candidates->verification.state, Data::SemanticBindingVerificationState::Verified);
    QVERIFY(candidates->verification.verifiedAt.isValid());
    QCOMPARE(candidates->mappingDigest.value, evidence.semanticMappingProof().mappingSha256);
    QCOMPARE(candidates->controllerMappingDigest, candidates->mappingDigest);
    QCOMPARE(candidates->bindings.size(), qsizetype(56));
    QCOMPARE(candidates->signalStates.size(), qsizetype(56));

    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    QSet<QString> targets;
    qsizetype writableSignals = 0;
    for (qsizetype index = 0; index < artifact.bindings.size(); ++index) {
        const VerifiedSemanticBinding &signedBinding = artifact.bindings.at(index);
        const Data::SemanticRuntimeBinding &binding = candidates->bindings.at(index);
        const Data::SemanticSignalRuntimeState &state = candidates->signalStates.at(index);

        QCOMPARE(binding.target, state.target);
        QCOMPARE(binding.target.controllerId, QStringLiteral("embed-labs.product-api"));
        QCOMPARE(binding.target.scope, catalog.scope);
        QCOMPARE(binding.target.kind, Data::SemanticRuntimeTargetKind::Signal);
        QCOMPARE(binding.target.signalId.value, signedBinding.semanticSignalDefinitionId);
        QVERIFY(!binding.target.deviceId.isNull());
        const QString targetKey = binding.target.deviceId.toString() + QLatin1Char('/')
                                  + binding.target.signalId.value;
        QVERIFY(!targets.contains(targetKey));
        targets.insert(targetKey);

        QCOMPARE(binding.semanticBindingId, signedBinding.semanticBindingId);
        QCOMPARE(binding.componentBindingId, signedBinding.componentBindingId);
        QCOMPARE(binding.resourceId.value, factoryOpaqueId(signedBinding.resourceId));
        QCOMPARE(
            binding.componentInstanceId.value, factoryOpaqueId(signedBinding.componentInstanceId));
        QCOMPARE(binding.consistencyGroupId.value, factoryOpaqueId(signedBinding.consistencyGroupId));
        QCOMPARE(binding.sessionGeneration, catalog.sessionGeneration);
        QCOMPARE(binding.epoch, catalog.epoch);
        QCOMPARE(binding.verification.state, Data::SemanticBindingVerificationState::Verified);

        QCOMPARE(state.availability, Data::SemanticSignalAvailability::Unverified);
        QVERIFY(state.binding.has_value());
        QCOMPARE(*state.binding, binding);
        QVERIFY(!state.value.has_value());
        QVERIFY(!state.snapshotComplete);
        QCOMPARE(state.definition.id.value, signedBinding.semanticSignalDefinitionId);
        QVERIFY(!state.definition.manualControl.allowed);
        QVERIFY(state.definition.manualControl.requiresExclusiveControl);
        QCOMPARE(
            state.definition.manualControl.timeoutAction,
            Data::ManualControlTimeoutAction::RejectFurtherWrites);

        if (signedBinding.access == EcfgResourceAccess::ReadWrite) {
            ++writableSignals;
            QCOMPARE(state.definition.access, Data::SemanticSignalAccess::ReadWrite);
            QCOMPARE(binding.access, Data::RuntimeResourceAccess::ReadWrite);
            QVERIFY(state.detail.contains(QStringLiteral("disabled")));
        }
    }
    QCOMPARE(targets.size(), qsizetype(56));
    QCOMPARE(writableSignals, qsizetype(22));
}

void EtherCATSemanticRuntimeTests::testReadOnlySemanticBindingFactoryRejectsMismatches()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(repositoryRoot.absoluteFilePath(
        "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath("artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray projectBytes = readFile(
        fixtureRoot.absoluteFilePath("package_inputs/project.json"));
    const QByteArray publicKey = readFile(fixtureRoot.absoluteFilePath(
        "trust/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"));
    if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.isEmpty())
        QSKIP("Transferred API-037 binding factory fixture is not present");

    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkg(packageBytes, trust, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidenceResult
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidenceResult);
    const VerifiedRuntimePackageEvidence evidence = *evidenceResult;

    const Data::ProjectSnapshot emptyProject = factoryProject(evidence);
    Data::ProjectSnapshot project = emptyProject;
    const QList<Data::DeviceAdapterManifest> adapterManifests
        = factoryApi038MutationAdapters(evidence);
    configureApi038V3ManualProject(project, adapterManifests);
    const Data::RuntimeResourceCatalog catalog = factoryCatalog(project, evidence);
    const Data::RuntimeSemanticMappingAttestation attestation
        = factoryAttestation(catalog, evidence);
    QVERIFY_RESULT(buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", project, evidence, catalog, attestation));

    Data::ProjectSnapshot unboundStation = project;
    unboundStation.slaves.first().stationAddress = 0;
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", unboundStation, evidence, catalog, attestation));

    Data::ProjectSnapshot wrongStation = project;
    ++wrongStation.slaves.first().stationAddress;
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", wrongStation, evidence, catalog, attestation));

    Data::ProjectSnapshot missingMapping = project;
    missingMapping.masterBindingArtifact.projectDeviceBindings.removeLast();
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", missingMapping, evidence, catalog, attestation));

    Data::ProjectSnapshot duplicateMapping = project;
    duplicateMapping.masterBindingArtifact.projectDeviceBindings[1].projectDeviceId
        = duplicateMapping.masterBindingArtifact.projectDeviceBindings[0].projectDeviceId;
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", duplicateMapping, evidence, catalog, attestation));

    Data::ProjectSnapshot swappedIdenticalDevices = project;
    auto axis0Mapping = std::find_if(
        swappedIdenticalDevices.masterBindingArtifact.projectDeviceBindings.begin(),
        swappedIdenticalDevices.masterBindingArtifact.projectDeviceBindings.end(),
        [](const Data::SemanticProjectDeviceBinding &mapping) {
            return mapping.projectDeviceId == QStringLiteral("embedlabs:project:device:axis0");
        });
    auto axis1Mapping = std::find_if(
        swappedIdenticalDevices.masterBindingArtifact.projectDeviceBindings.begin(),
        swappedIdenticalDevices.masterBindingArtifact.projectDeviceBindings.end(),
        [](const Data::SemanticProjectDeviceBinding &mapping) {
            return mapping.projectDeviceId == QStringLiteral("embedlabs:project:device:axis1");
        });
    QVERIFY(
        axis0Mapping != swappedIdenticalDevices.masterBindingArtifact.projectDeviceBindings.end());
    QVERIFY(
        axis1Mapping != swappedIdenticalDevices.masterBindingArtifact.projectDeviceBindings.end());
    std::swap(axis0Mapping->slaveId, axis1Mapping->slaveId);
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", swappedIdenticalDevices, evidence, catalog, attestation));

    Data::ProjectSnapshot localAdapterSelection = project;
    for (Data::OfflineSlaveConfiguration &slave : localAdapterSelection.slaves) {
        slave.adapterSelection.adapterId = {
            QStringLiteral("org.embedlabs.ide.local-adapter"),
        };
        slave.adapterSelection.adapterVersion = QStringLiteral("local");
        slave.adapterSelection.adapterContentSha256 = QByteArray(32, '\x5a');
    }
    QVERIFY_RESULT(buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", localAdapterSelection, evidence, catalog, attestation));

    Data::RuntimeResourceCatalog changedDescriptor = catalog;
    ++changedDescriptor.resources[0].bitWidth;
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", project, evidence, changedDescriptor, attestation));

    Data::RuntimeResourceCatalog duplicateDescriptor = catalog;
    duplicateDescriptor.resources[1].id = duplicateDescriptor.resources[0].id;
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", project, evidence, duplicateDescriptor, attestation));

    Data::RuntimeSemanticMappingAttestation changedProof = attestation;
    changedProof.proof.mappingSha256[0] ^= 1;
    QVERIFY(changedProof.isValid());
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", project, evidence, catalog, changedProof));

    Data::RuntimeSemanticMappingAttestation changedAttestationEpoch = attestation;
    ++changedAttestationEpoch.epoch.runtimeGeneration;
    QVERIFY(changedAttestationEpoch.isValid());
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", project, evidence, catalog, changedAttestationEpoch));

    Data::RuntimeResourceCatalog changedPackageEpoch = catalog;
    ++changedPackageEpoch.epoch.configurationId;
    Data::RuntimeSemanticMappingAttestation matchingChangedPackageEpoch
        = factoryAttestation(changedPackageEpoch, evidence);
    QVERIFY(matchingChangedPackageEpoch.isValid());
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api",
        project,
        evidence,
        changedPackageEpoch,
        matchingChangedPackageEpoch));

    const QDir v1Fixture(repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api036"));
    const QByteArray v1PackageBytes = readFile(
        v1Fixture.absoluteFilePath("three-slave-output-transaction-cfg3501.ecpkg"));
    const QByteArray v1ProjectBytes = readFile(v1Fixture.absoluteFilePath("project.json"));
    if (v1PackageBytes.isEmpty() || v1ProjectBytes.isEmpty())
        QSKIP("Transferred API-036 format-1 rejection fixture is not present");
    const Utils::Result<VerifiedEcpkgPackage> v1Package
        = verifyProductionEcpkg(v1PackageBytes, trust, v1ProjectBytes);
    QVERIFY_RESULT(v1Package);
    const Utils::Result<VerifiedRuntimePackageEvidence> v1Evidence = verifyRuntimePackageEvidence(
        *v1Package);
    QVERIFY_RESULT(v1Evidence);
    QCOMPARE(v1Evidence->semanticMappingProof().formatVersion, quint16(1));
    QVERIFY(!buildReadOnlySemanticBindingCandidates(
        u"embed-labs.product-api", project, *v1Evidence, catalog, attestation));
}

void EtherCATSemanticRuntimeTests::testSemanticActionRuntimeFactory()
{
    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    const QByteArray publicKey = readTestData(
        "testdata/api038/"
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    const Utils::Result<VerifiedEcpkgPackage> package = verifyProductionEcpkg(
        packageBytes, {{publicKey, EcpkgTrustClass::Production}}, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidenceResult
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidenceResult);
    const VerifiedRuntimePackageEvidence evidence = *evidenceResult;

    Data::ProjectSnapshot project = factoryProject(evidence);
    const Data::RuntimeResourceCatalog catalog = factoryCatalog(project, evidence);
    const Data::RuntimeSemanticMappingAttestation attestation
        = factoryAttestation(catalog, evidence);
    const Utils::Result<ReadOnlySemanticBindingCandidates> candidates
        = buildReadOnlySemanticBindingCandidates(
            u"embed-labs.product-api", project, evidence, catalog, attestation);
    QVERIFY_RESULT(candidates);

    const SemanticActionRuntimeGates readyGates{
        true,
        true,
        Data::ControllerServiceState::OperationalSafe,
        true,
    };
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> currentIdeStates
        = buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            project,
            evidence,
            *candidates,
            factoryCurrentIdeV2Adapters(),
            readyGates);
    QVERIFY_RESULT(currentIdeStates);
    QCOMPARE(
        std::count_if(
            currentIdeStates->cbegin(),
            currentIdeStates->cend(),
            [](const Data::SemanticActionRuntimeState &state) {
                return state.availability == Data::SemanticActionAvailability::Ready;
            }),
        qsizetype(0));

    const Utils::Result<QList<Data::DeviceAdapterManifest>> publishedV3Manifests
        = publishedApi038V3ActionAdapters();
    QVERIFY_RESULT(publishedV3Manifests);
    const auto publishedXb6Manifest = std::find_if(
        publishedV3Manifests->cbegin(),
        publishedV3Manifests->cend(),
        [](const Data::DeviceAdapterManifest &candidate) {
            return candidate.controllerAdapterTarget.adapterId
                   == QStringLiteral("solidot.xb6_ec0002_rev1_do16");
        });
    const auto signedXb6Topology = std::find_if(
        evidence.semanticBindingArtifact().topologyInstances.cbegin(),
        evidence.semanticBindingArtifact().topologyInstances.cend(),
        [](const SemanticBindingTopologyInstance &candidate) {
            return candidate.adapterId
                   == QStringLiteral("solidot.xb6_ec0002_rev1_do16");
        });
    QVERIFY(publishedXb6Manifest != publishedV3Manifests->cend());
    QVERIFY(signedXb6Topology
            != evidence.semanticBindingArtifact().topologyInstances.cend());
    QCOMPARE(
        publishedXb6Manifest->controllerAdapterTarget.adapterVersion,
        QStringLiteral("1.3.0"));
    QCOMPARE(signedXb6Topology->adapterVersion, QStringLiteral("1.2.0"));
    QVERIFY(publishedXb6Manifest->controllerAdapterTarget.adapterSha256
            != signedXb6Topology->adapterSha256);
    const auto publishedProfile =
        [&publishedV3Manifests](QStringView controllerAdapterId)
        -> const Data::ProcessDataProfile * {
        const auto manifest = std::find_if(
            publishedV3Manifests->cbegin(),
            publishedV3Manifests->cend(),
            [controllerAdapterId](const Data::DeviceAdapterManifest &candidate) {
                return candidate.controllerAdapterTarget.adapterId == controllerAdapterId;
            });
        if (manifest == publishedV3Manifests->cend()
            || manifest->processDataProfiles.size() != 1) {
            return nullptr;
        }
        return &manifest->processDataProfiles.constFirst();
    };
    const Data::ProcessDataProfile *publishedXb6Profile = publishedProfile(
        u"solidot.xb6_ec0002_rev1_do16");
    const Data::ProcessDataProfile *publishedSvProfile = publishedProfile(
        u"inovance.sv630n_1axis_rev00010000_csp");
    QVERIFY(publishedXb6Profile);
    QVERIFY(publishedSvProfile);
    QVERIFY(publishedXb6Profile->signedDcProfileId.isEmpty());
    QCOMPARE(publishedSvProfile->signedDcProfileId, QStringLiteral("sync0_125us"));
    for (const SemanticBindingTopologyInstance &topology :
         evidence.semanticBindingArtifact().topologyInstances) {
        const Data::ProcessDataProfile *profile = publishedProfile(topology.adapterId);
        QVERIFY(profile);
        QCOMPARE(profile->signedPdoProfileId, topology.pdoProfile);
        QCOMPARE(profile->signedDcProfileId, topology.dcProfile.value_or(QString()));
        QVERIFY(processDataProfileMatchesSignedTopology(*profile, topology, false));
        QCOMPARE(
            processDataProfileMatchesSignedTopology(*profile, topology, true),
            topology.dcProfile.has_value());

        Data::ProcessDataProfile wrongDcBinding = *profile;
        if (topology.dcProfile)
            wrongDcBinding.signedDcProfileId.clear();
        else
            wrongDcBinding.signedDcProfileId = QStringLiteral("sync0_125us");
        QVERIFY(!processDataProfileMatchesSignedTopology(wrongDcBinding, topology, false));

        Data::ProcessDataProfile wrongPdoBinding = *profile;
        wrongPdoBinding.signedPdoProfileId.append(QStringLiteral("_wrong"));
        QVERIFY(!processDataProfileMatchesSignedTopology(wrongPdoBinding, topology, false));
    }
    Data::ProjectSnapshot publishedV3Project = project;
    configureApi038V3ManualProject(publishedV3Project, *publishedV3Manifests);
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> publishedV3States
        = buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            publishedV3Project,
            evidence,
            *candidates,
            *publishedV3Manifests,
            readyGates);
    QVERIFY_RESULT(publishedV3States);
    qsizetype publishedAdapterRejected = 0;
    qsizetype publishedSignedRejected = 0;
    for (const Data::SemanticActionRuntimeState &state : *publishedV3States) {
        QCOMPARE(state.availability, Data::SemanticActionAvailability::Rejected);
        if (state.actionBindingId.startsWith(
                QStringLiteral("embedlabs:project:action:xb6:"))) {
            QCOMPARE(state.detail, QStringLiteral("manual_adapter_not_authorized"));
            ++publishedAdapterRejected;
        } else {
            QCOMPARE(
                state.detail,
                QStringLiteral("reference_unit_to_rpm_conversion_not_bound"));
            ++publishedSignedRejected;
        }
    }
    QCOMPARE(publishedAdapterRejected, qsizetype(2));
    QCOMPARE(publishedSignedRejected, qsizetype(6));

    const QList<Data::DeviceAdapterManifest> adapterManifests
        = factoryApi038MutationAdapters(evidence);
    QCOMPARE(adapterManifests.size(), qsizetype(2));
    QVERIFY(std::all_of(
        adapterManifests.cbegin(),
        adapterManifests.cend(),
        [](const Data::DeviceAdapterManifest &manifest) {
            return manifest.signatureVerified && manifest.realHardwareAllowed;
        }));
    configureApi038V3ManualProject(project, adapterManifests);
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> states
        = buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            project,
            evidence,
            *candidates,
            adapterManifests,
            readyGates);
    QVERIFY_RESULT(states);
    QCOMPARE(states->size(), qsizetype(8));

    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    qsizetype readyCount = 0;
    qsizetype rejectedCount = 0;
    QSet<QString> actionBindingIds;
    for (const Data::SemanticActionRuntimeState &state : *states) {
        const VerifiedSemanticAction *action = artifact.findAction(state.actionBindingId);
        QVERIFY(action);
        QVERIFY(!actionBindingIds.contains(state.actionBindingId));
        actionBindingIds.insert(state.actionBindingId);
        const bool xb6Action = state.actionBindingId.startsWith(
            QStringLiteral("embedlabs:project:action:xb6:"));

        QCOMPARE(state.target.controllerId, QStringLiteral("embed-labs.product-api"));
        QCOMPARE(state.target.scope, catalog.scope);
        QCOMPARE(state.target.kind, Data::SemanticRuntimeTargetKind::Action);
        QVERIFY(state.target.signalId.value.isEmpty());
        QCOMPARE(state.target.actionId.value, action->actionBindingId);
        QCOMPARE(state.actionBindingId, action->actionBindingId);
        QCOMPARE(state.actionDefinitionId, action->actionDefinitionId);
        QCOMPARE(state.definition.id, state.target.actionId);
        QVERIFY(!state.definition.displayName.isEmpty());
        QCOMPARE(state.definition.enabled, xb6Action);
        QCOMPARE(state.definition.requiresDc, action->dcRequired);
        QVERIFY(state.definition.requiresExclusiveControl);
        QVERIFY(state.definition.steps.isEmpty());
        QCOMPARE(
            state.actionDefinitionDigest,
            (Data::SemanticRuntimeDigest{
                QStringLiteral("sha256"),
                action->actionDefinitionSha256,
            }));
        QCOMPARE(
            state.qualification,
            action->qualification == VerifiedSemanticActionQualification::Qualified
                ? Data::SemanticActionQualification::Qualified
                : Data::SemanticActionQualification::Unqualified);
        QCOMPARE(state.requiresDc, action->dcRequired);
        QVERIFY(state.requiresApproval);
        QVERIFY(state.requiresExclusiveControl);
        QVERIFY(!state.holdToRun);
        QCOMPARE(
            state.maximumTtlMs,
            xb6Action ? quint32(125) : quint32(0));
        QCOMPARE(state.maximumTtlCycles, quint32(1000));
        QCOMPARE(
            state.bindings.size(),
            action->requiredBindings.size() + action->optionalBindings.size());
        QCOMPARE(state.parameters.size(), action->parameters.size());
        QCOMPARE(state.definition.parameters.size(), action->parameters.size());
        QCOMPARE(state.definition.requiredSignals.size(), action->requiredBindings.size());

        QSet<QString> publicBindingIds;
        for (const Data::SemanticRuntimeBinding &binding : state.bindings) {
            QVERIFY(!binding.semanticBindingId.isEmpty());
            QVERIFY(!publicBindingIds.contains(binding.semanticBindingId));
            publicBindingIds.insert(binding.semanticBindingId);
            QCOMPARE(binding.target.deviceId, state.target.deviceId);
            QCOMPARE(binding.target.kind, Data::SemanticRuntimeTargetKind::Signal);
        }

        if (xb6Action) {
            ++readyCount;
            QCOMPARE(state.availability, Data::SemanticActionAvailability::Ready);
            QCOMPARE(state.qualification, Data::SemanticActionQualification::Qualified);
            QVERIFY(state.disabledReason.isEmpty());
            QVERIFY(state.detail.isEmpty());
            QVERIFY(!state.requiresDc);
        } else {
            ++rejectedCount;
            QCOMPARE(state.availability, Data::SemanticActionAvailability::Rejected);
            QCOMPARE(state.qualification, Data::SemanticActionQualification::Unqualified);
            QCOMPARE(
                state.disabledReason, QStringLiteral("reference_unit_to_rpm_conversion_not_bound"));
            QCOMPARE(state.detail, state.disabledReason);
            QVERIFY(state.requiresDc);
        }
    }
    QCOMPARE(actionBindingIds.size(), qsizetype(8));
    QCOMPARE(readyCount, qsizetype(2));
    QCOMPARE(rejectedCount, qsizetype(6));

    const auto setOutputs = std::find_if(
        states->cbegin(), states->cend(), [](const Data::SemanticActionRuntimeState &state) {
            return state.actionBindingId
                   == QStringLiteral("embedlabs:project:action:xb6:set-outputs");
        });
    QVERIFY(setOutputs != states->cend());
    QCOMPARE(setOutputs->parameters.size(), qsizetype(16));
    for (const Data::SemanticActionParameterRuntimeDefinition &parameter : setOutputs->parameters) {
        QCOMPARE(parameter.primitiveType, Data::RuntimeResourcePrimitiveType::Boolean);
        QCOMPARE(parameter.minimum.metaType().id(), int(QMetaType::Bool));
        QCOMPARE(parameter.maximum.metaType().id(), int(QMetaType::Bool));
        QVERIFY(!parameter.minimum.toBool());
        QVERIFY(parameter.maximum.toBool());
    }

    const auto axisVelocity = std::find_if(
        states->cbegin(), states->cend(), [](const Data::SemanticActionRuntimeState &state) {
            return state.actionBindingId
                   == QStringLiteral("embedlabs:project:action:axis0:set-csv-velocity");
        });
    QVERIFY(axisVelocity != states->cend());
    QCOMPARE(axisVelocity->parameters.size(), qsizetype(1));
    QCOMPARE(
        axisVelocity->parameters.constFirst().primitiveType,
        Data::RuntimeResourcePrimitiveType::SignedInteger);
    QCOMPARE(axisVelocity->parameters.constFirst().unit, QStringLiteral("reference_unit_per_second"));
    QCOMPARE(axisVelocity->parameters.constFirst().minimum.toLongLong(), qlonglong(-1000));
    QCOMPARE(axisVelocity->parameters.constFirst().maximum.toLongLong(), qlonglong(1000));

    Data::SemanticRuntimeContext context;
    context.controllerId = QStringLiteral("embed-labs.product-api");
    context.scope = catalog.scope;
    context.sessionGeneration = catalog.sessionGeneration;
    context.epoch = catalog.epoch;
    context.mappingDigest = candidates->mappingDigest;
    context.controllerMappingDigest = candidates->controllerMappingDigest;
    context.actionDefinitionsDigest = {
        QStringLiteral("sha256"),
        evidence.actionDefinitions()->definitionsSha256,
    };
    context.cyclePeriodNs = evidence.cyclePeriodNs();
    context.bindingVerification = candidates->verification;
    context.signalStates = candidates->signalStates;
    context.actionStates = *states;
    context.complete = true;
    const QByteArray contextHash = semanticRuntimeContextHash(context);
    QCOMPARE(contextHash.size(), qsizetype(32));

    Data::SemanticRuntimeContext displayChange = context;
    displayChange.actionStates.first().definition.displayName = QStringLiteral(
        "Localized display name");
    displayChange.actionStates.first().availability = Data::SemanticActionAvailability::Unavailable;
    displayChange.signalStates.first().captureCycle = 42;
    displayChange.signalStates.first().controllerTimestampNs = 43;
    displayChange.signalStates.first().observedAt
        = QDateTime::fromString(QStringLiteral("2026-08-01T12:00:00Z"), Qt::ISODate);
    QCOMPARE(semanticRuntimeContextHash(displayChange), contextHash);

    Data::SemanticRuntimeContext reordered = context;
    std::reverse(reordered.signalStates.begin(), reordered.signalStates.end());
    std::reverse(reordered.actionStates.begin(), reordered.actionStates.end());
    for (Data::SemanticActionRuntimeState &action : reordered.actionStates) {
        std::reverse(action.parameters.begin(), action.parameters.end());
        std::reverse(action.bindings.begin(), action.bindings.end());
    }
    QCOMPARE(semanticRuntimeContextHash(reordered), contextHash);

    Data::SemanticRuntimeContext changedDefinition = context;
    changedDefinition.actionStates.first().actionDefinitionId.append(QStringLiteral(".changed"));
    QVERIFY(semanticRuntimeContextHash(changedDefinition) != contextHash);
    Data::SemanticRuntimeContext changedDefinitionDigest = context;
    changedDefinitionDigest.actionStates.first().actionDefinitionDigest.value[0] ^= 1;
    QVERIFY(semanticRuntimeContextHash(changedDefinitionDigest) != contextHash);
    Data::SemanticRuntimeContext changedCompanion = context;
    changedCompanion.actionDefinitionsDigest.value[0] ^= 1;
    QVERIFY(semanticRuntimeContextHash(changedCompanion) != contextHash);
    Data::SemanticRuntimeContext changedCycle = context;
    ++changedCycle.cyclePeriodNs;
    QVERIFY(semanticRuntimeContextHash(changedCycle) != contextHash);
    Data::SemanticRuntimeContext mockContext = context;
    mockContext.mock = true;
    QVERIFY(semanticRuntimeContextHash(mockContext) != contextHash);
    Data::SemanticRuntimeContext changedManualPolicy = context;
    changedManualPolicy.signalStates.first().definition.manualControl.policyId = QStringLiteral(
        "changed-policy");
    QVERIFY(semanticRuntimeContextHash(changedManualPolicy) != contextHash);
    Data::SemanticRuntimeContext changedActionTtl = context;
    ++changedActionTtl.actionStates.first().maximumTtlMs;
    QVERIFY(semanticRuntimeContextHash(changedActionTtl) != contextHash);
    Data::SemanticRuntimeContext changedAdapterPolicy = context;
    ++changedAdapterPolicy.actionStates.first().definition.commandTtlMs;
    QVERIFY(semanticRuntimeContextHash(changedAdapterPolicy) != contextHash);
    Data::SemanticRuntimeContext changedFallbackPolicy = context;
    changedFallbackPolicy.actionStates.first().definition.allowedFailureActionIds.append(
        {QStringLiteral("urn:test:unsigned-fallback")});
    QVERIFY(semanticRuntimeContextHash(changedFallbackPolicy) != contextHash);
    Data::SemanticRuntimeContext duplicateAction = context;
    duplicateAction.actionStates.append(duplicateAction.actionStates.constFirst());
    QVERIFY(semanticRuntimeContextHash(duplicateAction).isEmpty());
}

void EtherCATSemanticRuntimeTests::testSemanticActionRuntimeFactoryFailsClosed()
{
    const QByteArray packageBytes = readTestData(
        "testdata/api038/three-slave-manual-control-cfg3701.ecpkg");
    const QByteArray projectBytes = readTestData("testdata/api038/project.json");
    const QByteArray publicKey = readTestData(
        "testdata/api038/"
        "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    const Utils::Result<VerifiedEcpkgPackage> package = verifyProductionEcpkg(
        packageBytes, {{publicKey, EcpkgTrustClass::Production}}, projectBytes);
    QVERIFY_RESULT(package);
    const Utils::Result<VerifiedRuntimePackageEvidence> evidenceResult
        = verifyRuntimePackageEvidence(*package);
    QVERIFY_RESULT(evidenceResult);
    const VerifiedRuntimePackageEvidence evidence = *evidenceResult;

    const Data::ProjectSnapshot emptyProject = factoryProject(evidence);
    Data::ProjectSnapshot project = emptyProject;
    const QList<Data::DeviceAdapterManifest> adapterManifests
        = factoryApi038MutationAdapters(evidence);
    configureApi038V3ManualProject(project, adapterManifests);
    const Data::RuntimeResourceCatalog catalog = factoryCatalog(project, evidence);
    const Data::RuntimeSemanticMappingAttestation attestation
        = factoryAttestation(catalog, evidence);
    const Utils::Result<ReadOnlySemanticBindingCandidates> candidates
        = buildReadOnlySemanticBindingCandidates(
            u"embed-labs.product-api", project, evidence, catalog, attestation);
    QVERIFY_RESULT(candidates);

    SemanticActionRuntimeGates gates{
        false,
        true,
        Data::ControllerServiceState::OperationalSafe,
        true,
    };
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> unsupported
        = buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            project,
            evidence,
            *candidates,
            adapterManifests,
            gates);
    QVERIFY_RESULT(unsupported);
    QCOMPARE(unsupported->size(), qsizetype(8));
    for (const Data::SemanticActionRuntimeState &state : *unsupported) {
        if (state.actionBindingId.startsWith(QStringLiteral("embedlabs:project:action:xb6:"))) {
            QCOMPARE(
                state.detail, QStringLiteral("runtime_output_transactions_unavailable"));
            QCOMPARE(state.availability, Data::SemanticActionAvailability::Unavailable);
        } else {
            QCOMPARE(state.availability, Data::SemanticActionAvailability::Rejected);
            QCOMPARE(state.detail, QStringLiteral("reference_unit_to_rpm_conversion_not_bound"));
        }
    }

    gates.outputTransactionsSupported = true;
    gates.ownsExclusiveControl = false;
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> noControl
        = buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            project,
            evidence,
            *candidates,
            adapterManifests,
            gates);
    QVERIFY_RESULT(noControl);
    for (const Data::SemanticActionRuntimeState &state : *noControl) {
        if (state.actionBindingId.startsWith(QStringLiteral("embedlabs:project:action:xb6:"))) {
            QCOMPARE(state.availability, Data::SemanticActionAvailability::Unavailable);
            QCOMPARE(state.detail, QStringLiteral("exclusive_control_not_owned"));
        }
    }

    ReadOnlySemanticBindingCandidates incomplete = *candidates;
    incomplete.bindings.removeLast();
    QVERIFY(!buildSemanticActionRuntimeStates(
        u"embed-labs.product-api", project, evidence, incomplete, adapterManifests, gates));

    ReadOnlySemanticBindingCandidates changedResource = *candidates;
    changedResource.bindings[0].resourceId.value[0] ^= 1;
    QVERIFY(!buildSemanticActionRuntimeStates(
        u"embed-labs.product-api",
        project,
        evidence,
        changedResource,
        adapterManifests,
        gates));

    Data::ProjectSnapshot missingDevice = project;
    missingDevice.masterBindingArtifact.projectDeviceBindings.removeLast();
    QVERIFY(!buildSemanticActionRuntimeStates(
        u"embed-labs.product-api",
        missingDevice,
        evidence,
        *candidates,
        adapterManifests,
        gates));

    Data::ProjectSnapshot unboundStation = project;
    unboundStation.slaves.first().stationAddress = 0;
    QVERIFY(!buildSemanticActionRuntimeStates(
        u"embed-labs.product-api",
        unboundStation,
        evidence,
        *candidates,
        adapterManifests,
        gates));

    Data::ProjectSnapshot wrongStation = project;
    ++wrongStation.slaves.first().stationAddress;
    QVERIFY(!buildSemanticActionRuntimeStates(
        u"embed-labs.product-api",
        wrongStation,
        evidence,
        *candidates,
        adapterManifests,
        gates));

    gates.ownsExclusiveControl = true;
    const auto statesFor =
        [&evidence, &candidates, &gates](
            const Data::ProjectSnapshot &candidateProject,
            const QList<Data::DeviceAdapterManifest> &manifests)
        -> Utils::Result<QList<Data::SemanticActionRuntimeState>> {
        return buildSemanticActionRuntimeStates(
            u"embed-labs.product-api",
            candidateProject,
            evidence,
            *candidates,
            manifests,
            gates);
    };
    const auto actionFor =
        [](const QList<Data::SemanticActionRuntimeState> &states, QStringView bindingId)
        -> const Data::SemanticActionRuntimeState * {
        const auto found = std::find_if(
            states.cbegin(),
            states.cend(),
            [bindingId](const Data::SemanticActionRuntimeState &state) {
                return state.actionBindingId == bindingId;
            });
        return found == states.cend() ? nullptr : &*found;
    };
    const auto xb6Manifest =
        [](QList<Data::DeviceAdapterManifest> &manifests)
        -> Data::DeviceAdapterManifest * {
        const auto found = std::find_if(
            manifests.begin(),
            manifests.end(),
            [](const Data::DeviceAdapterManifest &manifest) {
                return manifest.controllerAdapterTarget.adapterId
                       == QStringLiteral("solidot.xb6_ec0002_rev1_do16");
            });
        return found == manifests.end() ? nullptr : &*found;
    };
    const auto xb6Action =
        [](Data::DeviceAdapterManifest &manifest, QStringView suffix)
        -> Data::DeviceControlAction * {
        const auto found = std::find_if(
            manifest.controlActions.begin(),
            manifest.controlActions.end(),
            [suffix](const Data::DeviceControlAction &action) {
                return action.id.value.endsWith(suffix);
            });
        return found == manifest.controlActions.end() ? nullptr : &*found;
    };
    const auto expectRejectedDetail =
        [&statesFor, &actionFor](
            const Data::ProjectSnapshot &candidateProject,
            const QList<Data::DeviceAdapterManifest> &manifests,
            QStringView actionBindingId,
            QStringView detail) {
        const Utils::Result<QList<Data::SemanticActionRuntimeState>> states
            = statesFor(candidateProject, manifests);
        QVERIFY_RESULT(states);
        const Data::SemanticActionRuntimeState *action = actionFor(
            *states, actionBindingId);
        QVERIFY(action);
        QCOMPARE(action->availability, Data::SemanticActionAvailability::Rejected);
        QCOMPARE(action->detail, detail.toString());
    };

    const Utils::Result<QList<Data::SemanticActionRuntimeState>> ready
        = statesFor(project, adapterManifests);
    QVERIFY_RESULT(ready);
    const Data::SemanticActionRuntimeState *setOutputs = actionFor(
        *ready, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Ready);
    QVERIFY(setOutputs->detail.isEmpty());
    QCOMPARE(setOutputs->maximumTtlCycles, quint32(1000));
    QCOMPARE(setOutputs->maximumTtlMs, quint32(125));
    QVERIFY(setOutputs->definition.enabled);
    QVERIFY(setOutputs->definition.steps.isEmpty());

    Data::ProjectSnapshot noManual = project;
    QVERIFY(factoryManualSlave(noManual));
    factoryManualSlave(noManual)->manualControlEnvelope = {};
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> emptyEnvelope
        = statesFor(noManual, adapterManifests);
    QVERIFY_RESULT(emptyEnvelope);
    setOutputs = actionFor(
        *emptyEnvelope, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(setOutputs->detail, QStringLiteral("manual_control_disabled"));

    Data::ProjectSnapshot disabledEnvelope = project;
    QVERIFY(factoryManualSlave(disabledEnvelope));
    factoryManualSlave(disabledEnvelope)->manualControlEnvelope.enabled = false;
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> disabled
        = statesFor(disabledEnvelope, adapterManifests);
    QVERIFY_RESULT(disabled);
    setOutputs = actionFor(*disabled, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);

    Data::ProjectSnapshot wrongDefinitionId = project;
    QVERIFY(factoryManualSlave(wrongDefinitionId));
    Data::ManualControlEnvelope &wrongEnvelope
        = factoryManualSlave(wrongDefinitionId)->manualControlEnvelope;
    const auto wrongAction = std::find_if(
        wrongEnvelope.actionEnvelopes.begin(),
        wrongEnvelope.actionEnvelopes.end(),
        [](const Data::ManualActionEnvelope &action) {
            return action.actionId.value.endsWith(QStringLiteral("set-digital-outputs"));
        });
    QVERIFY(wrongAction != wrongEnvelope.actionEnvelopes.end());
    wrongAction->actionId = {
        QStringLiteral("embedlabs:project:action:xb6:set-outputs"),
    };
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> wrongBinding
        = statesFor(wrongDefinitionId, adapterManifests);
    QVERIFY_RESULT(wrongBinding);
    setOutputs = actionFor(*wrongBinding, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(setOutputs->detail, QStringLiteral("manual_action_not_authorized"));

    QList<Data::DeviceAdapterManifest> mismatchedAdapter = adapterManifests;
    QVERIFY(xb6Manifest(mismatchedAdapter));
    xb6Manifest(mismatchedAdapter)->contentSha256[0] ^= 1;
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> adapterMismatch
        = statesFor(project, mismatchedAdapter);
    QVERIFY_RESULT(adapterMismatch);
    setOutputs = actionFor(*adapterMismatch, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(setOutputs->detail, QStringLiteral("manual_adapter_not_authorized"));

    QList<Data::DeviceAdapterManifest> unsignedAdapter = adapterManifests;
    QVERIFY(xb6Manifest(unsignedAdapter));
    xb6Manifest(unsignedAdapter)->signatureVerified = false;
    expectRejectedDetail(
        project,
        unsignedAdapter,
        u"embedlabs:project:action:xb6:set-outputs",
        u"manual_adapter_not_authorized");

    QList<Data::DeviceAdapterManifest> hardwareDeniedAdapter = adapterManifests;
    QVERIFY(xb6Manifest(hardwareDeniedAdapter));
    xb6Manifest(hardwareDeniedAdapter)->realHardwareAllowed = false;
    expectRejectedDetail(
        project,
        hardwareDeniedAdapter,
        u"embedlabs:project:action:xb6:set-outputs",
        u"manual_adapter_not_authorized");

    Data::ProjectSnapshot expandedParameter = project;
    QVERIFY(factoryManualSlave(expandedParameter));
    Data::ManualControlEnvelope &expandedEnvelope
        = factoryManualSlave(expandedParameter)->manualControlEnvelope;
    const auto expandedAction = std::find_if(
        expandedEnvelope.actionEnvelopes.begin(),
        expandedEnvelope.actionEnvelopes.end(),
        [](const Data::ManualActionEnvelope &action) {
            return action.actionId.value.endsWith(QStringLiteral("set-digital-outputs"));
        });
    QVERIFY(expandedAction != expandedEnvelope.actionEnvelopes.end());
    QVERIFY(!expandedAction->parameters.isEmpty());
    expandedAction->parameters.first().allowedRange.maximum = Data::ExactRational{2, 1};
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> expanded
        = statesFor(expandedParameter, adapterManifests);
    QVERIFY_RESULT(expanded);
    setOutputs = actionFor(*expanded, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);

    Data::ProjectSnapshot narrowedParameter = project;
    QVERIFY(factoryManualSlave(narrowedParameter));
    Data::ManualControlEnvelope &narrowedEnvelope
        = factoryManualSlave(narrowedParameter)->manualControlEnvelope;
    const auto narrowedAction = std::find_if(
        narrowedEnvelope.actionEnvelopes.begin(),
        narrowedEnvelope.actionEnvelopes.end(),
        [](const Data::ManualActionEnvelope &action) {
            return action.actionId.value.endsWith(QStringLiteral("set-digital-outputs"));
        });
    QVERIFY(narrowedAction != narrowedEnvelope.actionEnvelopes.end());
    const auto narrowedDo0 = std::find_if(
        narrowedAction->parameters.begin(),
        narrowedAction->parameters.end(),
        [](const Data::ManualActionParameterEnvelope &parameter) {
            return parameter.parameterId == QStringLiteral("do0");
        });
    QVERIFY(narrowedDo0 != narrowedAction->parameters.end());
    narrowedDo0->allowedRange.maximum = Data::ExactRational{0, 1};
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> narrowed
        = statesFor(narrowedParameter, adapterManifests);
    QVERIFY_RESULT(narrowed);
    setOutputs = actionFor(*narrowed, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Ready);
    QVERIFY(setOutputs->detail.isEmpty());
    const auto runtimeDo0 = std::find_if(
        setOutputs->parameters.cbegin(),
        setOutputs->parameters.cend(),
        [](const Data::SemanticActionParameterRuntimeDefinition &parameter) {
            return parameter.id == QStringLiteral("do0");
        });
    QVERIFY(runtimeDo0 != setOutputs->parameters.cend());
    QVERIFY(!runtimeDo0->minimum.toBool());
    QVERIFY(!runtimeDo0->maximum.toBool());

    Data::ProjectSnapshot smallerProjectTtl = project;
    QVERIFY(factoryManualSlave(smallerProjectTtl));
    Data::ManualControlEnvelope &smallerTtlEnvelope
        = factoryManualSlave(smallerProjectTtl)->manualControlEnvelope;
    const auto smallerTtlAction = std::find_if(
        smallerTtlEnvelope.actionEnvelopes.begin(),
        smallerTtlEnvelope.actionEnvelopes.end(),
        [](const Data::ManualActionEnvelope &action) {
            return action.actionId.value.endsWith(QStringLiteral("set-digital-outputs"));
        });
    QVERIFY(smallerTtlAction != smallerTtlEnvelope.actionEnvelopes.end());
    smallerTtlAction->timing.commandTtlMs = 50;
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> smallerTtl
        = statesFor(smallerProjectTtl, adapterManifests);
    QVERIFY_RESULT(smallerTtl);
    setOutputs = actionFor(*smallerTtl, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Ready);
    QVERIFY(setOutputs->detail.isEmpty());
    QCOMPARE(setOutputs->maximumTtlMs, quint32(50));
    QCOMPARE(setOutputs->maximumTtlCycles, quint32(400));
    QCOMPARE(setOutputs->definition.commandTtlMs, quint32(50));

    const auto rejectSetOutputs =
        [&expectRejectedDetail, &project](
            const QList<Data::DeviceAdapterManifest> &manifests, QStringView detail) {
        expectRejectedDetail(
            project,
            manifests,
            u"embedlabs:project:action:xb6:set-outputs",
            detail);
    };

    QList<Data::DeviceAdapterManifest> legacyContract = adapterManifests;
    QVERIFY(xb6Manifest(legacyContract));
    xb6Manifest(legacyContract)->contractVersion = Data::DeviceAdapterContractVersion::V2;
    rejectSetOutputs(legacyContract, u"manual_adapter_contract_version_unsupported");

    QList<Data::DeviceAdapterManifest> wrongControllerTarget = adapterManifests;
    QVERIFY(xb6Manifest(wrongControllerTarget));
    xb6Manifest(wrongControllerTarget)->controllerAdapterTarget.adapterId.append(
        QStringLiteral(".wrong"));
    rejectSetOutputs(wrongControllerTarget, u"manual_controller_target_mismatch");

    QList<Data::DeviceAdapterManifest> wrongEsi = adapterManifests;
    QVERIFY(xb6Manifest(wrongEsi));
    xb6Manifest(wrongEsi)->match.exactEsiSha256[0] ^= 1;
    rejectSetOutputs(wrongEsi, u"manual_adapter_identity_mismatch");

    QList<Data::DeviceAdapterManifest> wrongDefinitionDigest = adapterManifests;
    QVERIFY(xb6Manifest(wrongDefinitionDigest));
    QVERIFY(xb6Action(*xb6Manifest(wrongDefinitionDigest), u"set-digital-outputs"));
    xb6Action(
        *xb6Manifest(wrongDefinitionDigest),
        u"set-digital-outputs")->expectedSignedDefinitionSha256[0]
        ^= 1;
    rejectSetOutputs(wrongDefinitionDigest, u"manual_action_definition_mismatch");

    QList<Data::DeviceAdapterManifest> wrongProfile = adapterManifests;
    QVERIFY(xb6Manifest(wrongProfile));
    xb6Manifest(wrongProfile)->processDataProfiles.first().signedPdoProfileId
        = QStringLiteral("wrong_profile");
    rejectSetOutputs(wrongProfile, u"manual_pdo_profile_mismatch");

    QList<Data::DeviceAdapterManifest> wrongDcProfile = adapterManifests;
    QVERIFY(xb6Manifest(wrongDcProfile));
    xb6Manifest(wrongDcProfile)->processDataProfiles.first().signedDcProfileId
        = QStringLiteral("sync0_125us");
    rejectSetOutputs(wrongDcProfile, u"manual_dc_profile_mismatch");

    QList<Data::DeviceAdapterManifest> incompleteProfile = adapterManifests;
    QVERIFY(xb6Manifest(incompleteProfile));
    xb6Manifest(incompleteProfile)->processDataProfiles.first().requiredSignals.removeLast();
    rejectSetOutputs(incompleteProfile, u"manual_pdo_profile_mismatch");

    QList<Data::DeviceAdapterManifest> missingRequiredSignal = adapterManifests;
    QVERIFY(xb6Manifest(missingRequiredSignal));
    QVERIFY(xb6Action(*xb6Manifest(missingRequiredSignal), u"set-digital-outputs"));
    xb6Action(
        *xb6Manifest(missingRequiredSignal),
        u"set-digital-outputs")->requiredSignals.removeLast();
    rejectSetOutputs(missingRequiredSignal, u"manual_action_definition_mismatch");

    QList<Data::DeviceAdapterManifest> swappedAssignments = adapterManifests;
    QVERIFY(xb6Manifest(swappedAssignments));
    Data::DeviceControlAction *swappedAction = xb6Action(
        *xb6Manifest(swappedAssignments), u"set-digital-outputs");
    QVERIFY(swappedAction);
    QVERIFY(swappedAction->steps.first().assignments.size() >= 2);
    std::swap(
        swappedAction->steps.first().assignments[0].value.parameterId,
        swappedAction->steps.first().assignments[1].value.parameterId);
    rejectSetOutputs(swappedAssignments, u"manual_step_contract_mismatch");

    QList<Data::DeviceAdapterManifest> mixedValueRepresentation = adapterManifests;
    QVERIFY(xb6Manifest(mixedValueRepresentation));
    Data::DeviceControlAction *mixedValueAction = xb6Action(
        *xb6Manifest(mixedValueRepresentation), u"set-digital-outputs");
    QVERIFY(mixedValueAction);
    mixedValueAction->steps.first().assignments.first().value.literalValue = true;
    rejectSetOutputs(mixedValueRepresentation, u"manual_step_contract_mismatch");

    QList<Data::DeviceAdapterManifest> changedConstant = adapterManifests;
    QVERIFY(xb6Manifest(changedConstant));
    Data::DeviceControlAction *clearAction = xb6Action(
        *xb6Manifest(changedConstant), u"clear-digital-outputs");
    QVERIFY(clearAction);
    QVERIFY(clearAction->steps.first().assignments.first().value.engineeringLiteralValue);
    clearAction->steps.first().assignments.first().value.engineeringLiteralValue
        = Data::EngineeringValue::fromBoolean(true);
    expectRejectedDetail(
        project,
        changedConstant,
        u"embedlabs:project:action:xb6:clear-outputs",
        u"manual_step_contract_mismatch");

    QList<Data::DeviceAdapterManifest> partialGroup = adapterManifests;
    QVERIFY(xb6Manifest(partialGroup));
    QVERIFY(xb6Action(*xb6Manifest(partialGroup), u"set-digital-outputs"));
    xb6Action(
        *xb6Manifest(partialGroup),
        u"set-digital-outputs")->consistencyGroups.first().members.removeLast();
    rejectSetOutputs(partialGroup, u"manual_group_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongRecovery = adapterManifests;
    QVERIFY(xb6Manifest(wrongRecovery));
    QVERIFY(xb6Action(*xb6Manifest(wrongRecovery), u"set-digital-outputs"));
    xb6Action(
        *xb6Manifest(wrongRecovery),
        u"set-digital-outputs")->consistencyGroups.first().recovery
        = Data::DeviceControlGroupRecovery::ReturnTask;
    rejectSetOutputs(wrongRecovery, u"manual_group_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongGroupTtl = adapterManifests;
    QVERIFY(xb6Manifest(wrongGroupTtl));
    QVERIFY(xb6Action(*xb6Manifest(wrongGroupTtl), u"set-digital-outputs"));
    --xb6Action(
        *xb6Manifest(wrongGroupTtl),
        u"set-digital-outputs")->consistencyGroups.first().maximumTtlCycles;
    rejectSetOutputs(wrongGroupTtl, u"manual_group_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongSafeValue = adapterManifests;
    QVERIFY(xb6Manifest(wrongSafeValue));
    const auto safeSignal = std::find_if(
        xb6Manifest(wrongSafeValue)->semanticSignals.begin(),
        xb6Manifest(wrongSafeValue)->semanticSignals.end(),
        [](const Data::SemanticSignalDefinition &signal) {
            return signal.id.value.endsWith(QStringLiteral("channel.0"));
        });
    QVERIFY(safeSignal != xb6Manifest(wrongSafeValue)->semanticSignals.end());
    safeSignal->engineeringSafeValue = Data::EngineeringValue::fromBoolean(true);
    rejectSetOutputs(wrongSafeValue, u"manual_group_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongTransform = adapterManifests;
    QVERIFY(xb6Manifest(wrongTransform));
    const auto transformedSignal = std::find_if(
        xb6Manifest(wrongTransform)->semanticSignals.begin(),
        xb6Manifest(wrongTransform)->semanticSignals.end(),
        [](const Data::SemanticSignalDefinition &signal) {
            return signal.id.value.endsWith(QStringLiteral("channel.0"));
        });
    QVERIFY(transformedSignal != xb6Manifest(wrongTransform)->semanticSignals.end());
    QVERIFY(transformedSignal->engineeringTransform);
    transformedSignal->engineeringTransform->unit = QStringLiteral("wrong_unit");
    rejectSetOutputs(wrongTransform, u"manual_signal_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongParameter = adapterManifests;
    QVERIFY(xb6Manifest(wrongParameter));
    QVERIFY(xb6Action(*xb6Manifest(wrongParameter), u"set-digital-outputs"));
    xb6Action(
        *xb6Manifest(wrongParameter),
        u"set-digital-outputs")->parameters.first().valueMetadata.unit
        = QStringLiteral("wrong_unit");
    rejectSetOutputs(wrongParameter, u"manual_parameter_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongStepKind = adapterManifests;
    QVERIFY(xb6Manifest(wrongStepKind));
    QVERIFY(xb6Action(*xb6Manifest(wrongStepKind), u"set-digital-outputs"));
    xb6Action(
        *xb6Manifest(wrongStepKind),
        u"set-digital-outputs")->steps.first().kind
        = Data::DeviceControlStepKind::WaitCycles;
    rejectSetOutputs(wrongStepKind, u"manual_step_contract_mismatch");

    QList<Data::DeviceAdapterManifest> wrongDc = adapterManifests;
    QVERIFY(xb6Manifest(wrongDc));
    QVERIFY(xb6Action(*xb6Manifest(wrongDc), u"set-digital-outputs"));
    xb6Action(*xb6Manifest(wrongDc), u"set-digital-outputs")->requiresDc = true;
    rejectSetOutputs(wrongDc, u"manual_action_definition_mismatch");

    QList<Data::DeviceAdapterManifest> elevatedSv = adapterManifests;
    const auto svManifest = std::find_if(
        elevatedSv.begin(),
        elevatedSv.end(),
        [](const Data::DeviceAdapterManifest &manifest) {
            return manifest.controllerAdapterTarget.adapterId
                   == QStringLiteral("inovance.sv630n_1axis_rev00010000_csp");
        });
    QVERIFY(svManifest != elevatedSv.end());
    for (Data::DeviceControlAction &definition : svManifest->controlActions) {
        definition.enabled = true;
        definition.signedQualification = Data::DeviceControlActionQualification::Qualified;
        definition.disabledReason.clear();
    }
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> elevatedSvStates
        = statesFor(project, elevatedSv);
    QVERIFY_RESULT(elevatedSvStates);
    const Data::SemanticActionRuntimeState *svVelocity = actionFor(
        *elevatedSvStates, u"embedlabs:project:action:axis0:set-csv-velocity");
    QVERIFY(svVelocity);
    QCOMPARE(svVelocity->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(
        svVelocity->detail,
        QStringLiteral("reference_unit_to_rpm_conversion_not_bound"));

    Data::ProjectSnapshot heldAction = project;
    QVERIFY(factoryManualSlave(heldAction));
    Data::ManualControlEnvelope &heldEnvelope
        = factoryManualSlave(heldAction)->manualControlEnvelope;
    const auto held = std::find_if(
        heldEnvelope.actionEnvelopes.begin(),
        heldEnvelope.actionEnvelopes.end(),
        [](const Data::ManualActionEnvelope &action) {
            return action.actionId.value.endsWith(QStringLiteral("set-digital-outputs"));
        });
    QVERIFY(held != heldEnvelope.actionEnvelopes.end());
    held->holdToRun = true;
    held->timing.refreshTimeoutMs = 10;
    held->timing.maxContinuousHoldMs = 20;
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> heldStates
        = statesFor(heldAction, adapterManifests);
    QVERIFY_RESULT(heldStates);
    setOutputs = actionFor(*heldStates, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(setOutputs->detail, QStringLiteral("manual_action_hold_unsupported"));

    Data::ProjectSnapshot fallbackAction = project;
    QVERIFY(factoryManualSlave(fallbackAction));
    Data::ManualControlEnvelope &fallbackEnvelope
        = factoryManualSlave(fallbackAction)->manualControlEnvelope;
    const auto fallback = std::find_if(
        fallbackEnvelope.actionEnvelopes.begin(),
        fallbackEnvelope.actionEnvelopes.end(),
        [](const Data::ManualActionEnvelope &action) {
            return action.actionId.value.endsWith(QStringLiteral("set-digital-outputs"));
        });
    QVERIFY(fallback != fallbackEnvelope.actionEnvelopes.end());
    fallback->failureActionId = {QStringLiteral("urn:test:unsigned-fallback")};
    const Utils::Result<QList<Data::SemanticActionRuntimeState>> fallbackStates
        = statesFor(fallbackAction, adapterManifests);
    QVERIFY_RESULT(fallbackStates);
    setOutputs = actionFor(*fallbackStates, u"embedlabs:project:action:xb6:set-outputs");
    QVERIFY(setOutputs);
    QCOMPARE(setOutputs->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(setOutputs->detail, QStringLiteral("manual_action_fallback_unsupported"));
}

void EtherCATSemanticRuntimeTests::testSemanticOperationJournal()
{
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence = api038ActionEvidence();
    QVERIFY_RESULT(evidence);
    const Utils::Result<Data::SemanticRuntimeContext> contextResult = api038ActionContext(*evidence);
    QVERIFY_RESULT(contextResult);
    const Data::SemanticRuntimeContext context = *contextResult;

    Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/journal/set-outputs");
    setApi038DigitalOutputParameters(request);

    Data::SemanticRuntimeActor submitter;
    submitter.id = QStringLiteral("automation/api038-test");
    submitter.displayName = QStringLiteral("API-038 test submitter");
    submitter.kind = Data::SemanticRuntimeActorKind::Automation;
    submitter.origin = QStringLiteral("QtTest");
    submitter.authenticationDigest = QByteArray(32, '\x24');

    SemanticOperationJournal journal;
    const QDateTime submittedAt = QDateTime::fromString(
        QStringLiteral("2026-07-30T01:02:03.000Z"), Qt::ISODateWithMs);
    const SemanticOperationJournalResult submitted
        = journal.submit(request, submitter, context, submittedAt);
    QCOMPARE(submitted.disposition, SemanticOperationJournalDisposition::Created);
    QVERIFY(submitted.record);
    QCOMPARE(submitted.record->state, Data::SemanticOperationState::ApprovalRequired);
    QCOMPARE(submitted.record->canonicalRequestDigest.size(), qsizetype(32));
    QCOMPARE(submitted.record->approvalChallenge.size(), qsizetype(32));

    const SemanticOperationJournalResult replay
        = journal.submit(request, submitter, context, submittedAt.addSecs(1));
    QCOMPARE(replay.disposition, SemanticOperationJournalDisposition::Replayed);
    QCOMPARE(replay.record, submitted.record);

    Data::SemanticOperationRequest conflictingRequest = request;
    conflictingRequest.reason.append(QStringLiteral(" changed"));
    const SemanticOperationJournalResult conflict
        = journal.submit(conflictingRequest, submitter, context, submittedAt.addSecs(2));
    QCOMPARE(conflict.disposition, SemanticOperationJournalDisposition::Conflict);
    QCOMPARE(
        conflict.validation.error,
        Core::SemanticRuntimeValidationError::InvalidRequest);
    QVERIFY(conflict.record);
    QCOMPARE(conflict.record->request, request);

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = submitted.record->approvalChallenge;
    approval.expectedRequestDigest = submitted.record->canonicalRequestDigest;
    approval.expectedContextHash = context.contextHash;
    approval.detail = QStringLiteral("Confirmed API-038 test action");

    Data::SemanticRuntimeActor automationApprover = submitter;
    automationApprover.authenticationDigest = QByteArray(32, '\x31');
    const SemanticOperationJournalResult automationRejected
        = journal.approve(approval, automationApprover, context, submittedAt.addSecs(3));
    QCOMPARE(automationRejected.disposition, SemanticOperationJournalDisposition::Rejected);
    QCOMPARE(
        automationRejected.validation.error,
        Core::SemanticRuntimeValidationError::ApprovalActorInvalid);

    Data::SemanticRuntimeActor user;
    user.id = QStringLiteral("user/api038-test");
    user.displayName = QStringLiteral("Authenticated API-038 user");
    user.kind = Data::SemanticRuntimeActorKind::User;
    user.origin = QStringLiteral("QtTest");
    user.authenticationDigest = QByteArray(32, '\x42');

    Data::SemanticRuntimeContext changedContext = context;
    changedContext.contextHash[0] ^= '\x01';
    const SemanticOperationJournalResult contextRejected
        = journal.approve(approval, user, changedContext, submittedAt.addSecs(4));
    QCOMPARE(contextRejected.disposition, SemanticOperationJournalDisposition::Rejected);
    QCOMPARE(
        contextRejected.validation.error,
        Core::SemanticRuntimeValidationError::ApprovalChallengeMismatch);

    const SemanticOperationJournalResult approved
        = journal.approve(approval, user, context, submittedAt.addSecs(5));
    QCOMPARE(approved.disposition, SemanticOperationJournalDisposition::Updated);
    QVERIFY(approved.record);
    QCOMPARE(approved.record->state, Data::SemanticOperationState::Approved);
    QCOMPARE(approved.record->approvals.size(), qsizetype(1));
    QCOMPARE(approved.record->approvals.constFirst().actor, user);

    const SemanticOperationJournalResult approvalReplay
        = journal.approve(approval, user, context, submittedAt.addSecs(6));
    QCOMPARE(approvalReplay.disposition, SemanticOperationJournalDisposition::Replayed);

    const SemanticOperationJournalResult staleApprovalReplay
        = journal.approve(approval, user, changedContext, submittedAt.addMSecs(6500));
    QCOMPARE(staleApprovalReplay.disposition, SemanticOperationJournalDisposition::Rejected);
    QCOMPARE(
        staleApprovalReplay.validation.error,
        Core::SemanticRuntimeValidationError::ApprovalChallengeMismatch);

    SemanticOperationJournalStateUpdate executing;
    executing.expectedState = Data::SemanticOperationState::Approved;
    executing.state = Data::SemanticOperationState::Executing;
    executing.actor = submitter;
    executing.resultCode = QStringLiteral("executing");
    const SemanticOperationJournalResult executingResult
        = journal.transition(request.operationId, executing, submittedAt.addSecs(7));
    QCOMPARE(executingResult.disposition, SemanticOperationJournalDisposition::Updated);

    SemanticOperationJournalStateUpdate unknown;
    unknown.expectedState = Data::SemanticOperationState::Executing;
    unknown.state = Data::SemanticOperationState::OutcomeUnknown;
    unknown.actor = submitter;
    unknown.resultCode = QStringLiteral("outcome-unknown");
    const SemanticOperationJournalResult unknownResult
        = journal.transition(request.operationId, unknown, submittedAt.addSecs(8));
    QCOMPARE(unknownResult.disposition, SemanticOperationJournalDisposition::Updated);

    SemanticOperationJournalStateUpdate expired;
    expired.expectedState = Data::SemanticOperationState::OutcomeUnknown;
    expired.state = Data::SemanticOperationState::Expired;
    expired.actor = submitter;
    expired.resultCode = QStringLiteral("ttl-recovered");
    expired.detail = QStringLiteral("The controller applied the signed TTL recovery policy.");
    const SemanticOperationJournalResult expiredResult
        = journal.transition(request.operationId, expired, submittedAt.addSecs(9));
    QCOMPARE(expiredResult.disposition, SemanticOperationJournalDisposition::Updated);
    QVERIFY(expiredResult.record);
    QCOMPARE(expiredResult.record->state, Data::SemanticOperationState::Expired);

    const std::optional<Data::SemanticOperationRecord> stored = journal.operation(
        request.operationId);
    QVERIFY(stored);
    QCOMPARE(stored->state, Data::SemanticOperationState::Expired);

    const QList<Data::SemanticRuntimeAuditEvent> audit = journal.audit(context.controllerId);
    QCOMPARE(audit.size(), qsizetype(9));
    for (qsizetype index = 0; index < audit.size(); ++index) {
        QCOMPARE(audit.at(index).sequence, quint64(index + 1));
        QCOMPARE(audit.at(index).controllerId, context.controllerId);
        QCOMPARE(audit.at(index).operationId, request.operationId);
    }
    QCOMPARE(audit.constFirst().kind, Data::SemanticAuditEventKind::Submitted);
    QCOMPARE(audit.constLast().kind, Data::SemanticAuditEventKind::OutcomeReconciled);
    QCOMPARE(journal.audit(context.controllerId, 6), audit.sliced(6));

    const auto prepareForExecution =
        [&context, &submitter, &user, submittedAt](
            SemanticOperationJournal &executionJournal,
            const Data::SemanticOperationRequest &executionRequest) {
            const SemanticOperationJournalResult executionSubmitted = executionJournal.submit(
                executionRequest, submitter, context, submittedAt);
            if (!executionSubmitted.record)
                return executionSubmitted;

            Data::SemanticOperationApprovalRequest executionApproval;
            executionApproval.operationId = executionRequest.operationId;
            executionApproval.decision = Data::SemanticApprovalDecision::Approved;
            executionApproval.challenge = executionSubmitted.record->approvalChallenge;
            executionApproval.expectedRequestDigest
                = executionSubmitted.record->canonicalRequestDigest;
            executionApproval.expectedContextHash = context.contextHash;
            return executionJournal.approve(
                executionApproval, user, context, submittedAt.addSecs(1));
        };
    const auto beginExecuting =
        [&submitter, submittedAt](
            SemanticOperationJournal &executionJournal,
            const Data::SemanticOperationId &operationId) {
            SemanticOperationJournalStateUpdate begin;
            begin.expectedState = Data::SemanticOperationState::Approved;
            begin.state = Data::SemanticOperationState::Executing;
            begin.actor = submitter;
            begin.resultCode = QStringLiteral("executing");
            return executionJournal.transition(operationId, begin, submittedAt.addSecs(2));
        };

    Data::SemanticOperationRequest incompleteRequest = request;
    incompleteRequest.operationId.value.append(QStringLiteral("/incomplete"));
    SemanticOperationJournal incompleteJournal;
    QCOMPARE(
        prepareForExecution(incompleteJournal, incompleteRequest).disposition,
        SemanticOperationJournalDisposition::Updated);
    QCOMPARE(
        beginExecuting(incompleteJournal, incompleteRequest.operationId).disposition,
        SemanticOperationJournalDisposition::Updated);
    SemanticOperationJournalStepUpdate firstOfTwo;
    firstOfTwo.actor = submitter;
    firstOfTwo.stepIndex = 1;
    firstOfTwo.totalSteps = 2;
    QCOMPARE(
        incompleteJournal.recordStep(
            incompleteRequest.operationId, firstOfTwo, submittedAt.addSecs(3))
            .disposition,
        SemanticOperationJournalDisposition::Updated);
    SemanticOperationJournalStateUpdate prematureSuccess;
    prematureSuccess.expectedState = Data::SemanticOperationState::Executing;
    prematureSuccess.state = Data::SemanticOperationState::Succeeded;
    prematureSuccess.actor = submitter;
    prematureSuccess.resultCode = QStringLiteral("succeeded");
    QCOMPARE(
        incompleteJournal.transition(
            incompleteRequest.operationId, prematureSuccess, submittedAt.addSecs(4))
            .disposition,
        SemanticOperationJournalDisposition::Rejected);

    Data::SemanticOperationRequest failedStepRequest = request;
    failedStepRequest.operationId.value.append(QStringLiteral("/failed-step"));
    SemanticOperationJournal failedStepJournal;
    QCOMPARE(
        prepareForExecution(failedStepJournal, failedStepRequest).disposition,
        SemanticOperationJournalDisposition::Updated);
    QCOMPARE(
        beginExecuting(failedStepJournal, failedStepRequest.operationId).disposition,
        SemanticOperationJournalDisposition::Updated);
    SemanticOperationJournalStepUpdate failedFirstStep = firstOfTwo;
    failedFirstStep.failed = true;
    QCOMPARE(
        failedStepJournal.recordStep(
            failedStepRequest.operationId, failedFirstStep, submittedAt.addSecs(3))
            .disposition,
        SemanticOperationJournalDisposition::Updated);
    SemanticOperationJournalStepUpdate stepAfterFailure = firstOfTwo;
    stepAfterFailure.stepIndex = 2;
    QCOMPARE(
        failedStepJournal.recordStep(
            failedStepRequest.operationId, stepAfterFailure, submittedAt.addSecs(4))
            .disposition,
        SemanticOperationJournalDisposition::Rejected);
    QCOMPARE(
        failedStepJournal.transition(
            failedStepRequest.operationId, prematureSuccess, submittedAt.addSecs(5))
            .disposition,
        SemanticOperationJournalDisposition::Rejected);
}

void EtherCATSemanticRuntimeTests::testSemanticActionPlan()
{
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence = api038ActionEvidence();
    QVERIFY_RESULT(evidence);
    const Utils::Result<Data::SemanticRuntimeContext> contextResult = api038ActionContext(*evidence);
    QVERIFY_RESULT(contextResult);
    const Data::SemanticRuntimeContext context = *contextResult;

    Data::SemanticOperationRequest setRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/plan/set-outputs");
    setApi038DigitalOutputParameters(setRequest);
    const Utils::Result<SemanticActionPlan> setPlan = buildSemanticActionPlan(
        *evidence, setRequest, context);
    QVERIFY_RESULT(setPlan);
    QCOMPARE(setPlan->actionBindingId(), QStringLiteral(
        "embedlabs:project:action:xb6:set-outputs"));
    QCOMPARE(setPlan->cyclePeriodNs(), quint32(125000));
    QCOMPARE(setPlan->ttlCycles(), quint32(1000));
    QCOMPARE(setPlan->groups().size(), qsizetype(1));
    QCOMPARE(setPlan->steps().size(), qsizetype(1));

    const VerifiedSemanticAction *signedSetAction
        = evidence->semanticBindingArtifact().findAction(setPlan->actionBindingId());
    QVERIFY(signedSetAction);
    QCOMPARE(signedSetAction->consistencyGroups.size(), qsizetype(1));
    QCOMPARE(signedSetAction->steps.size(), qsizetype(1));

    const quint32 signedGroupId = signedSetAction->consistencyGroups.constFirst()
                                      .consistencyGroupId;
    const auto policy = std::find_if(
        evidence->outputPolicies().cbegin(),
        evidence->outputPolicies().cend(),
        [signedGroupId](const EcfgOutputGroupPolicy &candidate) {
            return candidate.consistencyGroupId == signedGroupId;
        });
    QVERIFY(policy != evidence->outputPolicies().cend());

    const SemanticActionPlanGroup &group = setPlan->groups().constFirst();
    QCOMPARE(group.consistencyGroupId().value, factoryOpaqueId(signedGroupId));
    QCOMPARE(group.recoveryPolicy(), Data::RuntimeOutputRecoveryPolicy::HoldSafe);
    QCOMPARE(group.maximumTtlCycles(), policy->maximumTtlCycles);
    QCOMPARE(group.completeGroupRecordDigest(), policy->groupResourceRecordsSha256);
    QCOMPARE(group.completeResourceCount(), policy->resourceCount);
    QCOMPARE(group.completeResourceIds().size(), qsizetype(policy->resourceCount));

    QList<Data::RuntimeResourceId> expectedResourceIds;
    for (quint64 resourceId : policy->resourceIds)
        expectedResourceIds.append(Data::RuntimeResourceId{factoryOpaqueId(resourceId)});
    QCOMPARE(group.completeResourceIds(), expectedResourceIds);

    const SemanticActionPlanStep &setStep = setPlan->steps().constFirst();
    QCOMPARE(setStep.index(), quint32(0));
    QCOMPARE(setStep.kind(), SemanticActionPlanStepKind::WriteGroup);
    QCOMPARE(setStep.consistencyGroupId(), group.consistencyGroupId());
    QVERIFY(setStep.outputOperationId().isValid());
    QCOMPARE(setStep.outputOperationId().value.size(), qsizetype(16));
    QCOMPARE(setStep.completeGroupWrites().size(), qsizetype(16));

    QHash<QByteArray, bool> expectedSetValues;
    for (const VerifiedSemanticActionAssignment &assignment :
         signedSetAction->steps.constFirst().assignments) {
        QVERIFY(assignment.parameterId);
        const VerifiedSemanticBinding *binding
            = evidence->semanticBindingArtifact().findBySemanticSignalId(
                assignment.semanticBindingId);
        QVERIFY(binding);
        const auto parameter = setRequest.parameters.constFind(*assignment.parameterId);
        QVERIFY(parameter != setRequest.parameters.cend());
        expectedSetValues.insert(factoryOpaqueId(binding->resourceId), parameter->toBool());
    }
    QCOMPARE(expectedSetValues.size(), 16);

    QByteArray previousResourceId;
    for (const Data::RuntimeOutputValueWrite &write : setStep.completeGroupWrites()) {
        QVERIFY(write.isValid());
        QVERIFY(previousResourceId.isEmpty() || previousResourceId < write.resourceId.value);
        previousResourceId = write.resourceId.value;
        QCOMPARE(write.bitWidth, quint16(1));
        QCOMPARE(write.value.primitiveType, Data::RuntimeResourcePrimitiveType::Boolean);
        QCOMPARE(write.value.value.metaType().id(), int(QMetaType::Bool));
        QVERIFY(expectedSetValues.contains(write.resourceId.value));
        QCOMPARE(write.value.value.toBool(), expectedSetValues.value(write.resourceId.value));
    }

    const Utils::Result<SemanticActionPlan> repeatedSetPlan = buildSemanticActionPlan(
        *evidence, setRequest, context);
    QVERIFY_RESULT(repeatedSetPlan);
    QCOMPARE(
        repeatedSetPlan->steps().constFirst().outputOperationId(),
        setStep.outputOperationId());

    Data::SemanticOperationRequest anotherOperation = setRequest;
    anotherOperation.operationId.value.append(QStringLiteral("/another"));
    const Utils::Result<SemanticActionPlan> anotherPlan = buildSemanticActionPlan(
        *evidence, anotherOperation, context);
    QVERIFY_RESULT(anotherPlan);
    QVERIFY(
        anotherPlan->steps().constFirst().outputOperationId()
        != setStep.outputOperationId());

    const Data::SemanticOperationRequest clearRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:clear-outputs",
        u"operation/api038/plan/clear-outputs");
    const Utils::Result<SemanticActionPlan> clearPlan = buildSemanticActionPlan(
        *evidence, clearRequest, context);
    QVERIFY_RESULT(clearPlan);
    QCOMPARE(clearPlan->groups().size(), qsizetype(1));
    QCOMPARE(clearPlan->steps().size(), qsizetype(1));
    QCOMPARE(
        clearPlan->groups().constFirst().completeGroupRecordDigest(),
        group.completeGroupRecordDigest());
    QCOMPARE(
        clearPlan->groups().constFirst().completeResourceIds(),
        group.completeResourceIds());
    const SemanticActionPlanStep &clearStep = clearPlan->steps().constFirst();
    QCOMPARE(clearStep.completeGroupWrites().size(), qsizetype(16));
    QVERIFY(clearStep.outputOperationId() != setStep.outputOperationId());
    for (const Data::RuntimeOutputValueWrite &write : clearStep.completeGroupWrites()) {
        QCOMPARE(write.bitWidth, quint16(1));
        QCOMPARE(write.value.primitiveType, Data::RuntimeResourcePrimitiveType::Boolean);
        QVERIFY(!write.value.value.toBool());
    }
}

void EtherCATSemanticRuntimeTests::testSemanticActionPlanFailsClosed()
{
    const Utils::Result<VerifiedRuntimePackageEvidence> evidence = api038ActionEvidence();
    QVERIFY_RESULT(evidence);
    const Utils::Result<Data::SemanticRuntimeContext> contextResult = api038ActionContext(*evidence);
    QVERIFY_RESULT(contextResult);
    const Data::SemanticRuntimeContext context = *contextResult;

    Data::SemanticOperationRequest setRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/plan/rejections");
    setApi038DigitalOutputParameters(setRequest);
    QVERIFY_RESULT(buildSemanticActionPlan(*evidence, setRequest, context));

    Data::SemanticOperationRequest excessiveTtl = setRequest;
    excessiveTtl.ttlCycles = 1001;
    QVERIFY(!buildSemanticActionPlan(*evidence, excessiveTtl, context));

    Data::SemanticOperationRequest zeroTtl = setRequest;
    zeroTtl.ttlCycles = 0;
    QVERIFY(!buildSemanticActionPlan(*evidence, zeroTtl, context));

    Data::SemanticOperationRequest missingParameter = setRequest;
    missingParameter.parameters.remove(QStringLiteral("do15"));
    QVERIFY(!buildSemanticActionPlan(*evidence, missingParameter, context));

    Data::SemanticOperationRequest wrongParameterType = setRequest;
    wrongParameterType.parameters[QStringLiteral("do0")]
        = QVariant::fromValue<qulonglong>(1);
    QVERIFY(!buildSemanticActionPlan(*evidence, wrongParameterType, context));

    Data::SemanticRuntimeContext narrowedParameterContext = context;
    const auto narrowedAction = std::find_if(
        narrowedParameterContext.actionStates.begin(),
        narrowedParameterContext.actionStates.end(),
        [](const Data::SemanticActionRuntimeState &candidate) {
            return candidate.actionBindingId
                   == QStringLiteral("embedlabs:project:action:xb6:set-outputs");
        });
    QVERIFY(narrowedAction != narrowedParameterContext.actionStates.end());
    const auto narrowedDo0 = std::find_if(
        narrowedAction->parameters.begin(),
        narrowedAction->parameters.end(),
        [](const Data::SemanticActionParameterRuntimeDefinition &parameter) {
            return parameter.id == QStringLiteral("do0");
        });
    QVERIFY(narrowedDo0 != narrowedAction->parameters.end());
    narrowedDo0->maximum = false;
    narrowedParameterContext.contextHash = semanticRuntimeContextHash(narrowedParameterContext);
    Data::SemanticOperationRequest outsideProjectRange = setRequest;
    outsideProjectRange.expectedContextHash = narrowedParameterContext.contextHash;
    QVERIFY(!buildSemanticActionPlan(
        *evidence, outsideProjectRange, narrowedParameterContext));
    Data::SemanticOperationRequest insideProjectRange = outsideProjectRange;
    insideProjectRange.parameters[QStringLiteral("do0")] = false;
    QVERIFY_RESULT(buildSemanticActionPlan(
        *evidence, insideProjectRange, narrowedParameterContext));

    Data::SemanticRuntimeContext changedCycle = context;
    ++changedCycle.cyclePeriodNs;
    changedCycle.contextHash = semanticRuntimeContextHash(changedCycle);
    Data::SemanticOperationRequest changedCycleRequest = setRequest;
    changedCycleRequest.expectedContextHash = changedCycle.contextHash;
    QVERIFY(Core::validateSemanticOperationRequest(changedCycleRequest, changedCycle).accepted());
    QVERIFY(!buildSemanticActionPlan(*evidence, changedCycleRequest, changedCycle));

    Data::SemanticRuntimeContext changedActionProjection = context;
    const auto changedAction = std::find_if(
        changedActionProjection.actionStates.begin(),
        changedActionProjection.actionStates.end(),
        [](const Data::SemanticActionRuntimeState &candidate) {
            return candidate.actionBindingId
                   == QStringLiteral("embedlabs:project:action:xb6:set-outputs");
        });
    QVERIFY(changedAction != changedActionProjection.actionStates.end());
    --changedAction->maximumTtlCycles;
    changedActionProjection.contextHash = semanticRuntimeContextHash(changedActionProjection);
    Data::SemanticOperationRequest changedActionRequest = setRequest;
    changedActionRequest.ttlCycles = changedAction->maximumTtlCycles;
    changedActionRequest.expectedContextHash = changedActionProjection.contextHash;
    QVERIFY(Core::validateSemanticOperationRequest(
        changedActionRequest, changedActionProjection).accepted());
    QVERIFY_RESULT(buildSemanticActionPlan(
        *evidence, changedActionRequest, changedActionProjection));

    Data::SemanticRuntimeContext expandedTtlProjection = context;
    const auto expandedTtlAction = std::find_if(
        expandedTtlProjection.actionStates.begin(),
        expandedTtlProjection.actionStates.end(),
        [](const Data::SemanticActionRuntimeState &candidate) {
            return candidate.actionBindingId
                   == QStringLiteral("embedlabs:project:action:xb6:set-outputs");
        });
    QVERIFY(expandedTtlAction != expandedTtlProjection.actionStates.end());
    ++expandedTtlAction->maximumTtlCycles;
    expandedTtlProjection.contextHash = semanticRuntimeContextHash(expandedTtlProjection);
    Data::SemanticOperationRequest expandedTtlRequest = setRequest;
    expandedTtlRequest.expectedContextHash = expandedTtlProjection.contextHash;
    QVERIFY(Core::validateSemanticOperationRequest(
        expandedTtlRequest, expandedTtlProjection).accepted());
    QVERIFY(!buildSemanticActionPlan(
        *evidence, expandedTtlRequest, expandedTtlProjection));

    Data::SemanticOperationRequest svRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:axis0:set-csv-velocity",
        u"operation/api038/plan/unqualified-sv630n");
    svRequest.parameters.insert(
        QStringLiteral("target_velocity"), QVariant::fromValue<qlonglong>(100));
    QVERIFY(!buildSemanticActionPlan(*evidence, svRequest, context));
}

void EtherCATSemanticRuntimeTests::testExecutorRejectsUnauthorizedManualActionBeforeApply()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize(true);
    QVERIFY_RESULT(initialized);
    const Data::ProjectSnapshot authorizedProject = fixture.project;

    const auto xb6Manifest = [](QList<Data::DeviceAdapterManifest> &manifests) {
        return std::find_if(
            manifests.begin(),
            manifests.end(),
            [](const Data::DeviceAdapterManifest &manifest) {
                return manifest.controllerAdapterTarget.adapterId
                       == QStringLiteral("solidot.xb6_ec0002_rev1_do16");
            });
    };
    const auto xb6Action = [](const Data::SemanticRuntimeContext &context) {
        return std::find_if(
            context.actionStates.cbegin(),
            context.actionStates.cend(),
            [](const Data::SemanticActionRuntimeState &action) {
                return action.actionBindingId
                       == QStringLiteral("embedlabs:project:action:xb6:set-outputs");
            });
    };

    QList<Data::DeviceAdapterManifest> unsignedManifests = fixture.adapterManifests;
    auto unsignedXb6 = xb6Manifest(unsignedManifests);
    QVERIFY(unsignedXb6 != unsignedManifests.end());
    unsignedXb6->signatureVerified = false;
    fixture.ownedAdapterProvider->replaceManifests(unsignedManifests);
    QTRY_VERIFY(!fixture.executor->contexts().isEmpty());
    const Data::SemanticRuntimeContext unsignedContext
        = fixture.executor->contexts().constFirst();
    const auto unsignedAction = xb6Action(unsignedContext);
    QVERIFY(unsignedAction != unsignedContext.actionStates.cend());
    QCOMPARE(unsignedAction->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(unsignedAction->detail, QStringLiteral("manual_adapter_not_authorized"));
    Data::SemanticOperationRequest unsignedRequest = api038ActionRequest(
        unsignedContext,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/unsigned-adapter");
    setApi038DigitalOutputParameters(unsignedRequest);
    QVERIFY(!buildSemanticActionPlan(*fixture.evidence, unsignedRequest, unsignedContext));
    const Data::SemanticOperationRecord unsignedRejected
        = fixture.executor->submit(unsignedRequest, api038Submitter());
    QCOMPARE(unsignedRejected.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!unsignedRejected.executionAttempted);
    QVERIFY(fixture.provider.applyRequests.isEmpty());

    QList<Data::DeviceAdapterManifest> hardwareDeniedManifests = fixture.adapterManifests;
    auto hardwareDeniedXb6 = xb6Manifest(hardwareDeniedManifests);
    QVERIFY(hardwareDeniedXb6 != hardwareDeniedManifests.end());
    hardwareDeniedXb6->realHardwareAllowed = false;
    fixture.ownedAdapterProvider->replaceManifests(hardwareDeniedManifests);
    QTRY_VERIFY(!fixture.executor->contexts().isEmpty());
    const Data::SemanticRuntimeContext hardwareDeniedContext
        = fixture.executor->contexts().constFirst();
    const auto hardwareDeniedAction = xb6Action(hardwareDeniedContext);
    QVERIFY(hardwareDeniedAction != hardwareDeniedContext.actionStates.cend());
    QCOMPARE(hardwareDeniedAction->availability, Data::SemanticActionAvailability::Rejected);
    QCOMPARE(hardwareDeniedAction->detail, QStringLiteral("manual_adapter_not_authorized"));
    Data::SemanticOperationRequest hardwareDeniedRequest = api038ActionRequest(
        hardwareDeniedContext,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/hardware-denied-adapter");
    setApi038DigitalOutputParameters(hardwareDeniedRequest);
    QVERIFY(!buildSemanticActionPlan(
        *fixture.evidence, hardwareDeniedRequest, hardwareDeniedContext));
    const Data::SemanticOperationRecord hardwareDeniedRejected
        = fixture.executor->submit(hardwareDeniedRequest, api038Submitter());
    QCOMPARE(hardwareDeniedRejected.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!hardwareDeniedRejected.executionAttempted);
    QVERIFY(fixture.provider.snapshotRequests.isEmpty());
    QVERIFY(fixture.provider.policyRequests.isEmpty());
    QVERIFY(fixture.provider.stateRequests.isEmpty());
    QVERIFY(fixture.provider.applyRequests.isEmpty());
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);

    fixture.ownedAdapterProvider->replaceManifests(fixture.adapterManifests);
    QTRY_VERIFY(([&fixture, &xb6Action] {
        const QList<Data::SemanticRuntimeContext> contexts = fixture.executor->contexts();
        if (contexts.size() != 1)
            return false;
        const auto action = xb6Action(contexts.constFirst());
        return action != contexts.constFirst().actionStates.cend()
               && action->availability == Data::SemanticActionAvailability::Ready;
    }()));

    Data::ProjectSnapshot unboundProject = authorizedProject;
    unboundProject.slaves.first().stationAddress = 0;
    fixture.projects.changeProject(unboundProject);
    const Data::SemanticRuntimeContext unboundContext
        = fixture.executor->contexts().constFirst();
    QVERIFY(!unboundContext.complete);
    QVERIFY(unboundContext.actionStates.isEmpty());
    fixture.project = authorizedProject;
    fixture.projects.changeProject(fixture.project);

    QVERIFY(factoryManualSlave(fixture.project));
    factoryManualSlave(fixture.project)->manualControlEnvelope.enabled = false;
    fixture.projects.changeProject(fixture.project);
    const Data::SemanticRuntimeContext unauthorizedContext
        = fixture.executor->contexts().constFirst();
    QVERIFY(unauthorizedContext.complete);
    QCOMPARE(
        std::count_if(
            unauthorizedContext.actionStates.cbegin(),
            unauthorizedContext.actionStates.cend(),
            [](const Data::SemanticActionRuntimeState &action) {
                return action.availability == Data::SemanticActionAvailability::Ready;
            }),
        qsizetype(0));

    Data::SemanticOperationRequest request = api038ActionRequest(
        unauthorizedContext,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/manual-policy-rejected");
    setApi038DigitalOutputParameters(request);
    const Data::SemanticOperationRecord rejected
        = fixture.executor->submit(request, api038Submitter());
    QCOMPARE(rejected.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!rejected.executionAttempted);
    QVERIFY(fixture.provider.snapshotRequests.isEmpty());
    QVERIFY(fixture.provider.policyRequests.isEmpty());
    QVERIFY(fixture.provider.stateRequests.isEmpty());
    QVERIFY(fixture.provider.applyRequests.isEmpty());
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);

    fixture.project = authorizedProject;
    fixture.projects.changeProject(fixture.project);
    const Data::SemanticRuntimeContext authorizedContext
        = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest pending = api038ActionRequest(
        authorizedContext,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/manual-policy-changed");
    setApi038DigitalOutputParameters(pending);
    const Data::SemanticOperationRecord submitted
        = fixture.executor->submit(pending, api038Submitter());
    QCOMPARE(submitted.state, Data::SemanticOperationState::ApprovalRequired);
    QVERIFY(!submitted.executionAttempted);

    QVERIFY(factoryManualSlave(fixture.project));
    factoryManualSlave(fixture.project)->manualControlEnvelope.enabled = false;
    fixture.projects.changeProject(fixture.project);
    const Data::SemanticRuntimeContext disabledContext
        = fixture.executor->contexts().constFirst();
    QVERIFY(disabledContext.complete);
    QVERIFY(disabledContext.contextHash != authorizedContext.contextHash);

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = pending.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = submitted.approvalChallenge;
    approval.expectedRequestDigest = submitted.canonicalRequestDigest;
    approval.expectedContextHash = authorizedContext.contextHash;
    approval.detail = QStringLiteral("This stale approval must be rejected.");
    const Data::SemanticOperationRecord stale
        = fixture.executor->approve(approval, api038Approver());
    QCOMPARE(stale.state, Data::SemanticOperationState::ApprovalRequired);
    QVERIFY(!stale.executionAttempted);
    QVERIFY(fixture.provider.snapshotRequests.isEmpty());
    QVERIFY(fixture.provider.policyRequests.isEmpty());
    QVERIFY(fixture.provider.stateRequests.isEmpty());
    QVERIFY(fixture.provider.applyRequests.isEmpty());
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);

    QSignalSpy contextsSpy(
        fixture.executor.get(),
        &Core::SemanticRuntimeService::contextsChanged);
    QVERIFY(contextsSpy.isValid());
    QVERIFY(fixture.ownedRegistry);
    fixture.ownedRegistry.reset();
    const int afterRegistryRemoval = contextsSpy.count();
    QCoreApplication::processEvents();
    QCOMPARE(contextsSpy.count(), afterRegistryRemoval);
    fixture.unregisterProviders();
    QVERIFY(fixture.providersAreUnregistered());
}

void EtherCATSemanticRuntimeTests::testExecutorRefreshesVerifiedSignals()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext before = fixture.executor->contexts().constFirst();
    QVERIFY(before.signalStates.size() >= 3);

    const Data::SemanticSignalRuntimeState &first = before.signalStates.at(0);
    const Data::SemanticSignalRuntimeState &second = before.signalStates.at(1);
    const Data::SemanticSignalRuntimeState &unselected = before.signalStates.at(2);
    QVERIFY(first.binding);
    QVERIFY(second.binding);

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = before.controllerId;
    request.scope = before.scope;
    request.targets = {
        {second.target.deviceId, second.target.signalId},
        {first.target.deviceId, first.target.signalId},
    };
    request.expectedContextHash = before.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh/verified-signals");
    QVERIFY(request.isValid());

    QSignalSpy contextsSpy(fixture.executor.get(), &Core::SemanticRuntimeService::contextsChanged);
    QSignalSpy
        completedSpy(fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
    QVERIFY(contextsSpy.isValid());
    QVERIFY(completedSpy.isValid());

    const Data::SemanticLiveRefreshResult accepted = fixture.executor->requestLiveRefresh(request);
    QVERIFY(accepted.isValid());
    QCOMPARE(accepted.outcome, Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest providerRequest
        = *fixture.provider.pendingSnapshotRequest;
    QCOMPARE(providerRequest.resourceIds.size(), qsizetype(2));
    QVERIFY(providerRequest.resourceIds.at(0).value < providerRequest.resourceIds.at(1).value);
    QList<Data::RuntimeResourceId> expectedIds{first.binding->resourceId, second.binding->resourceId};
    std::sort(
        expectedIds.begin(),
        expectedIds.end(),
        [](const Data::RuntimeResourceId &left, const Data::RuntimeResourceId &right) {
            return left.value < right.value;
        });
    QCOMPARE(providerRequest.resourceIds, expectedIds);

    const quint64 captureCycle = fixture.snapshot.captureCycle + 100;
    const Data::RuntimeResourceSnapshot liveSnapshot = targetedSnapshot(
        fixture.snapshot,
        providerRequest,
        {},
        fixture.snapshot.snapshotSequence + 100,
        captureCycle,
        fixture.snapshot.controllerTimestampNs + 1000);
    const Data::RuntimeResourceSnapshotResult result{providerRequest, liveSnapshot, {}};
    QVERIFY(result.isValid());
    fixture.provider.sendSnapshotResult(result);

    QTRY_COMPARE(completedSpy.count(), 1);
    const Data::SemanticLiveRefreshResult completed
        = qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.constFirst().constFirst());
    QVERIFY(completed.isValid());
    QCOMPARE(completed.outcome, Data::SemanticLiveRefreshOutcome::Refreshed);
    QCOMPARE(completed.captureCycle, captureCycle);
    QTRY_VERIFY(contextsSpy.count() >= 1);

    const Data::SemanticRuntimeContext after = fixture.executor->contexts().constFirst();
    QCOMPARE(after.contextHash, before.contextHash);
    const auto stateFor = [&after](const Data::SemanticRuntimeTarget &target) {
        return std::find_if(
            after.signalStates.cbegin(),
            after.signalStates.cend(),
            [&target](const Data::SemanticSignalRuntimeState &state) {
                return state.target == target;
            });
    };
    const auto firstAfter = stateFor(first.target);
    const auto secondAfter = stateFor(second.target);
    const auto unselectedAfter = stateFor(unselected.target);
    QVERIFY(firstAfter != after.signalStates.cend());
    QVERIFY(secondAfter != after.signalStates.cend());
    QVERIFY(unselectedAfter != after.signalStates.cend());
    QCOMPARE(firstAfter->captureCycle, captureCycle);
    QCOMPARE(secondAfter->captureCycle, captureCycle);
    QCOMPARE(unselectedAfter->captureCycle, unselected.captureCycle);
    QCOMPARE(unselectedAfter->value, unselected.value);
    QCOMPARE(fixture.provider.maximumConcurrentRequests, 1);
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorSerializesLiveRefreshWithActions()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    QVERIFY(!context.signalStates.isEmpty());
    const Data::SemanticSignalRuntimeState &signal = context.signalStates.constFirst();

    Data::SemanticLiveRefreshRequest refresh;
    refresh.controllerId = context.controllerId;
    refresh.scope = context.scope;
    refresh.targets = {{signal.target.deviceId, signal.target.signalId}};
    refresh.expectedContextHash = context.contextHash;
    refresh.correlationId = QStringLiteral("semantic-refresh/serialize/live-first");
    QCOMPARE(
        fixture.executor->requestLiveRefresh(refresh).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest liveRequest
        = *fixture.provider.pendingSnapshotRequest;

    Data::SemanticOperationRequest actionRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/live-serialization");
    setApi038DigitalOutputParameters(actionRequest);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, actionRequest, context);
    QVERIFY_RESULT(plan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, actionRequest).state,
        Data::SemanticOperationState::Approved);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(1));
    QVERIFY(fixture.provider.pendingSnapshotRequest);
    QCOMPARE(*fixture.provider.pendingSnapshotRequest, liveRequest);

    const Data::RuntimeResourceSnapshotResult liveResult{
        liveRequest,
        targetedSnapshot(
            fixture.snapshot,
            liveRequest,
            {},
            fixture.snapshot.snapshotSequence + 10,
            fixture.snapshot.captureCycle + 10,
            fixture.snapshot.controllerTimestampNs + 100),
        {},
    };
    QVERIFY(liveResult.isValid());
    fixture.provider.sendSnapshotResult(liveResult);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QTRY_COMPARE(fixture.provider.snapshotRequests.size(), qsizetype(2));
    QVERIFY(*fixture.provider.pendingSnapshotRequest != liveRequest);

    QVERIFY(advanceExecutorToApply(fixture, *plan));
    QVERIFY(fixture.provider.pendingApplyRequest);
    Data::SemanticLiveRefreshRequest blockedRefresh = refresh;
    blockedRefresh.correlationId = QStringLiteral("semantic-refresh/serialize/action-active");
    const Data::SemanticLiveRefreshResult deferred = fixture.executor->requestLiveRefresh(
        blockedRefresh);
    QVERIFY(deferred.isValid());
    QCOMPARE(deferred.outcome, Data::SemanticLiveRefreshOutcome::Deferred);
    QCOMPARE(deferred.code, QStringLiteral("semantic-live-refresh-busy"));
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(2));

    const Data::RuntimeOutputTransactionRequest applyRequest = *fixture.provider.pendingApplyRequest;
    const Data::RuntimeOutputTransactionState appliedState
        = completedOutputState(applyRequest, Data::RuntimeOutputTransactionOutcome::Applied, 220);
    fixture.provider.sendApplyResult({
        applyRequest,
        Data::RuntimeOutputTransactionOutcome::Applied,
        true,
        appliedState,
        {},
    });
    QVERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest afterRequest
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult({
        afterRequest,
        targetedSnapshot(
            fixture.snapshot,
            afterRequest,
            applyRequest.completeGroupWrites,
            30,
            appliedState.appliedCycle + 1,
            3300),
        {},
    });
    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest postStateRequest
        = *fixture.provider.pendingStateRequest;
    Data::RuntimeOutputTransactionState confirmedState = appliedState;
    confirmedState.controllerTimestampNs = 3301;
    fixture.provider.sendStateResult({postStateRequest, confirmedState, {}});

    QTRY_VERIFY(fixture.executor->operation(actionRequest.operationId).has_value());
    QCOMPARE(
        fixture.executor->operation(actionRequest.operationId)->state,
        Data::SemanticOperationState::Succeeded);
    QCOMPARE(
        fixture.provider.requestTrace,
        QStringList({
            QStringLiteral("snapshot-live"),
            QStringLiteral("snapshot-before"),
            QStringLiteral("policy"),
            QStringLiteral("state-pre"),
            QStringLiteral("apply"),
            QStringLiteral("snapshot-after"),
            QStringLiteral("state-post"),
        }));
    QCOMPARE(fixture.provider.maximumConcurrentRequests, 1);
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorDefersSynchronousLiveResultUntilAccepted()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    const Data::SemanticSignalRuntimeState signal = context.signalStates.constFirst();

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = context.controllerId;
    request.scope = context.scope;
    request.targets = {{signal.target.deviceId, signal.target.signalId}};
    request.expectedContextHash = context.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh/synchronous-result");

    fixture.provider.snapshotRequestStarted =
        [&fixture](const Data::RuntimeResourceSnapshotRequest &providerRequest) {
            Data::RuntimeResourceSnapshot snapshot = targetedSnapshot(
                fixture.snapshot,
                providerRequest,
                {},
                fixture.snapshot.snapshotSequence + 20,
                fixture.snapshot.captureCycle + 20,
                fixture.snapshot.controllerTimestampNs + 200);
            fixture.provider.sendSnapshotResult({providerRequest, snapshot, {}});
        };
    fixture.provider.failSnapshotStartAfterCallback = true;
    QSignalSpy
        completedSpy(fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
    QVERIFY(completedSpy.isValid());

    const Data::SemanticLiveRefreshResult accepted = fixture.executor->requestLiveRefresh(request);
    QCOMPARE(accepted.outcome, Data::SemanticLiveRefreshOutcome::Accepted);
    QCOMPARE(completedSpy.count(), 0);
    QTRY_COMPARE(completedSpy.count(), 1);
    const Data::SemanticLiveRefreshResult completed
        = qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.constFirst().constFirst());
    QCOMPARE(completed.outcome, Data::SemanticLiveRefreshOutcome::Refreshed);
    QCOMPARE(completed.correlationId, request.correlationId);
    QTest::qWait(20);
    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorTimesOutLiveRefreshAndUnblocksAction()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize(false, true, 20);
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    const Data::SemanticSignalRuntimeState signal = context.signalStates.constFirst();

    Data::SemanticLiveRefreshRequest refresh;
    refresh.controllerId = context.controllerId;
    refresh.scope = context.scope;
    refresh.targets = {{signal.target.deviceId, signal.target.signalId}};
    refresh.expectedContextHash = context.contextHash;
    refresh.correlationId = QStringLiteral("semantic-refresh/timeout");
    QSignalSpy
        completedSpy(fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
    QVERIFY(completedSpy.isValid());
    QCOMPARE(
        fixture.executor->requestLiveRefresh(refresh).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);

    Data::SemanticOperationRequest actionRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/live-timeout");
    setApi038DigitalOutputParameters(actionRequest);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, actionRequest).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    const Data::RuntimeResourceSnapshotRequest timedOutRequest
        = *fixture.provider.pendingSnapshotRequest;
    QTRY_COMPARE(completedSpy.count(), 1);
    const Data::SemanticLiveRefreshResult completion
        = qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.constFirst().constFirst());
    QCOMPARE(completion.outcome, Data::SemanticLiveRefreshOutcome::Failed);
    QCOMPARE(completion.code, QStringLiteral("semantic-live-refresh-timeout"));
    QTRY_VERIFY(fixture.executor->operation(actionRequest.operationId).has_value());
    QTRY_COMPARE(
        fixture.executor->operation(actionRequest.operationId)->state,
        Data::SemanticOperationState::Failed);

    fixture.provider.sendSnapshotResult({
        timedOutRequest,
        targetedSnapshot(
            fixture.snapshot,
            timedOutRequest,
            {},
            fixture.snapshot.snapshotSequence + 30,
            fixture.snapshot.captureCycle + 30,
            fixture.snapshot.controllerTimestampNs + 300),
        {},
    });
    QTest::qWait(20);
    QCOMPARE(completedSpy.count(), 1);
}

void EtherCATSemanticRuntimeTests::testExecutorFailsLiveRefreshWhenProviderStartFails()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    const Data::SemanticSignalRuntimeState signal = context.signalStates.constFirst();

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = context.controllerId;
    request.scope = context.scope;
    request.targets = {{signal.target.deviceId, signal.target.signalId}};
    request.expectedContextHash = context.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh/provider-start-failure");
    fixture.provider.failSnapshotStartAfterCallback = true;
    fixture.provider.snapshotStartError = QStringLiteral(
        "resource_id=0011223344556677 pdo=0x7010 /Users/private/controller.log");
    QSignalSpy
        completedSpy(fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
    QVERIFY(completedSpy.isValid());

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QCOMPARE(completedSpy.count(), 0);
    QTRY_COMPARE(completedSpy.count(), 1);
    const Data::SemanticLiveRefreshResult completion
        = qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.constFirst().constFirst());
    QCOMPARE(completion.outcome, Data::SemanticLiveRefreshOutcome::Failed);
    QCOMPARE(completion.code, QStringLiteral("semantic-live-refresh-provider-failed"));
    QCOMPARE(
        completion.detail,
        QStringLiteral("The controller provider could not start the live snapshot."));
    QVERIFY(!completion.detail.contains(QStringLiteral("resource_id")));
    QVERIFY(!completion.detail.contains(QStringLiteral("pdo"), Qt::CaseInsensitive));
    QVERIFY(!completion.detail.contains(QStringLiteral("/Users/")));
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(1));
}

void EtherCATSemanticRuntimeTests::testExecutorRejectsMismatchedAndLateLiveResults()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    QVERIFY(context.signalStates.size() >= 2);
    const Data::SemanticSignalRuntimeState first = context.signalStates.at(0);
    const Data::SemanticSignalRuntimeState second = context.signalStates.at(1);
    QVERIFY(first.binding);
    QVERIFY(second.binding);

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = context.controllerId;
    request.scope = context.scope;
    request.targets = {{first.target.deviceId, first.target.signalId}};
    request.expectedContextHash = context.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh/reused-public-correlation");
    QSignalSpy
        completedSpy(fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
    QVERIFY(completedSpy.isValid());

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    const Data::RuntimeResourceSnapshotRequest firstAttempt
        = *fixture.provider.pendingSnapshotRequest;
    Data::RuntimeResourceSnapshotRequest mismatchedAttempt = firstAttempt;
    mismatchedAttempt.resourceIds = {second.binding->resourceId};
    QVERIFY(mismatchedAttempt.isValid());
    const Data::RuntimeResourceSnapshot mismatchedSnapshot = targetedSnapshot(
        fixture.snapshot,
        mismatchedAttempt,
        {},
        fixture.snapshot.snapshotSequence + 40,
        fixture.snapshot.captureCycle + 40,
        fixture.snapshot.controllerTimestampNs + 400);
    const Data::RuntimeResourceSnapshotResult
        mismatchedResult{mismatchedAttempt, mismatchedSnapshot, {}};
    QVERIFY(mismatchedResult.isValid());
    fixture.provider.sendSnapshotResult(mismatchedResult);
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(
        qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.constFirst().constFirst())
            .outcome,
        Data::SemanticLiveRefreshOutcome::Failed);

    const Data::RuntimeResourceSnapshotResult lateFirstResult{
        firstAttempt,
        targetedSnapshot(
            fixture.snapshot,
            firstAttempt,
            {},
            fixture.snapshot.snapshotSequence + 41,
            fixture.snapshot.captureCycle + 41,
            fixture.snapshot.controllerTimestampNs + 410),
        {},
    };
    QVERIFY(lateFirstResult.isValid());
    fixture.provider.sendSnapshotResult(lateFirstResult, false);
    QCoreApplication::processEvents();
    QCOMPARE(completedSpy.count(), 1);

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    const Data::RuntimeResourceSnapshotRequest secondAttempt
        = *fixture.provider.pendingSnapshotRequest;
    QVERIFY(secondAttempt.correlationId != firstAttempt.correlationId);

    fixture.provider.sendSnapshotResult(lateFirstResult, false);
    QCoreApplication::processEvents();
    QCOMPARE(completedSpy.count(), 1);
    QVERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    QCOMPARE(*fixture.provider.pendingSnapshotRequest, secondAttempt);

    fixture.provider.sendSnapshotResult({
        secondAttempt,
        targetedSnapshot(
            fixture.snapshot,
            secondAttempt,
            {},
            fixture.snapshot.snapshotSequence + 42,
            fixture.snapshot.captureCycle + 42,
            fixture.snapshot.controllerTimestampNs + 420),
        {},
    });
    QTRY_COMPARE(completedSpy.count(), 2);
    QCOMPARE(
        qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.at(1).constFirst()).outcome,
        Data::SemanticLiveRefreshOutcome::Refreshed);
    fixture.provider.sendSnapshotResult(
        {
            secondAttempt,
            targetedSnapshot(
                fixture.snapshot,
                secondAttempt,
                {},
                fixture.snapshot.snapshotSequence + 42,
                fixture.snapshot.captureCycle + 42,
                fixture.snapshot.controllerTimestampNs + 420),
            {},
        },
        false);
    QCoreApplication::processEvents();
    QCOMPARE(completedSpy.count(), 2);

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    const Data::RuntimeResourceSnapshotRequest regressionAttempt
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult({
        regressionAttempt,
        targetedSnapshot(
            fixture.snapshot,
            regressionAttempt,
            {},
            fixture.snapshot.snapshotSequence + 60,
            fixture.snapshot.captureCycle + 41,
            fixture.snapshot.controllerTimestampNs + 410),
        {},
    });
    QTRY_COMPARE(completedSpy.count(), 3);
    QCOMPARE(
        qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.at(2).constFirst()).outcome,
        Data::SemanticLiveRefreshOutcome::Failed);

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    const Data::RuntimeResourceSnapshotRequest equalCycleAttempt
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult({
        equalCycleAttempt,
        targetedSnapshot(
            fixture.snapshot,
            equalCycleAttempt,
            {},
            fixture.snapshot.snapshotSequence + 61,
            fixture.snapshot.captureCycle + 42,
            fixture.snapshot.controllerTimestampNs + 420),
        {},
    });
    QTRY_COMPARE(completedSpy.count(), 4);
    QCOMPARE(
        qvariant_cast<Data::SemanticLiveRefreshResult>(completedSpy.at(3).constFirst()).outcome,
        Data::SemanticLiveRefreshOutcome::Failed);
}

void EtherCATSemanticRuntimeTests::testExecutorReleasesLiveRefreshOnRemoval()
{
    const auto beginRefreshAndAction = [](SemanticExecutorFixture &fixture, QStringView suffix) {
        const QString suffixString = suffix.toString();
        const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
        const Data::SemanticSignalRuntimeState signal = context.signalStates.constFirst();
        Data::SemanticLiveRefreshRequest refresh;
        refresh.controllerId = context.controllerId;
        refresh.scope = context.scope;
        refresh.targets = {{signal.target.deviceId, signal.target.signalId}};
        refresh.expectedContextHash = context.contextHash;
        refresh.correlationId = QStringLiteral("semantic-refresh/removal/") + suffixString;
        if (fixture.executor->requestLiveRefresh(refresh).outcome
            != Data::SemanticLiveRefreshOutcome::Accepted) {
            return Data::SemanticOperationId{};
        }
        const QString operationId = QStringLiteral("operation/api038/executor/removal/")
                                    + suffixString;
        Data::SemanticOperationRequest actionRequest
            = api038ActionRequest(context, u"embedlabs:project:action:xb6:set-outputs", operationId);
        setApi038DigitalOutputParameters(actionRequest);
        const Data::SemanticOperationRecord approved
            = submitAndApprove(*fixture.executor, context, actionRequest);
        return approved.state == Data::SemanticOperationState::Approved
                   ? actionRequest.operationId
                   : Data::SemanticOperationId{};
    };

    {
        SemanticExecutorFixture fixture;
        const Utils::Result<> initialized = fixture.initialize();
        QVERIFY_RESULT(initialized);
        QSignalSpy completedSpy(
            fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
        const Data::SemanticOperationId operationId = beginRefreshAndAction(fixture, u"provider");
        QVERIFY(!operationId.value.isEmpty());
        QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
        fixture.registration->remove();
        QTRY_COMPARE(completedSpy.count(), 1);
        QTRY_VERIFY(fixture.executor->operation(operationId).has_value());
        QTRY_COMPARE(
            fixture.executor->operation(operationId)->state, Data::SemanticOperationState::Failed);
    }

    {
        SemanticExecutorFixture fixture;
        const Utils::Result<> initialized = fixture.initialize();
        QVERIFY_RESULT(initialized);
        QSignalSpy completedSpy(
            fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
        const Data::SemanticOperationId operationId = beginRefreshAndAction(fixture, u"project");
        QVERIFY(!operationId.value.isEmpty());
        QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
        fixture.projects.removeProject(fixture.project.id);
        QTRY_COMPARE(completedSpy.count(), 1);
        QTRY_VERIFY(fixture.executor->operation(operationId).has_value());
        QTRY_COMPARE(
            fixture.executor->operation(operationId)->state, Data::SemanticOperationState::Failed);
    }

    {
        SemanticExecutorFixture fixture;
        const Utils::Result<> initialized = fixture.initialize(true);
        QVERIFY_RESULT(initialized);
        QSignalSpy completedSpy(
            fixture.executor.get(), &Core::SemanticRuntimeService::liveRefreshCompleted);
        const Data::SemanticOperationId operationId = beginRefreshAndAction(fixture, u"registry");
        QVERIFY(!operationId.value.isEmpty());
        QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
        fixture.ownedRegistry.reset();
        QTRY_COMPARE(completedSpy.count(), 1);
        QTRY_VERIFY(fixture.executor->operation(operationId).has_value());
        QTRY_COMPARE(
            fixture.executor->operation(operationId)->state, Data::SemanticOperationState::Failed);
    }
}

void EtherCATSemanticRuntimeTests::testExecutorRejectsRecursiveRefreshDuringProjectRemoval()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    const Data::SemanticSignalRuntimeState signal = context.signalStates.constFirst();

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = context.controllerId;
    request.scope = context.scope;
    request.targets = {{signal.target.deviceId, signal.target.signalId}};
    request.expectedContextHash = context.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh/project-removal/initial");
    std::optional<Data::SemanticLiveRefreshResult> recursiveResult;
    int completionCount = 0;
    QObject::connect(
        fixture.executor.get(),
        &Core::SemanticRuntimeService::liveRefreshCompleted,
        fixture.executor.get(),
        [&fixture, &request, &recursiveResult, &completionCount] {
            ++completionCount;
            Data::SemanticLiveRefreshRequest recursiveRequest = request;
            recursiveRequest.correlationId = QStringLiteral(
                "semantic-refresh/project-removal/recursive");
            recursiveResult = fixture.executor->requestLiveRefresh(recursiveRequest);
        },
        Qt::DirectConnection);

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    fixture.projects.removeProject(fixture.project.id);
    QCOMPARE(completionCount, 1);
    QVERIFY(recursiveResult);
    QCOMPARE(recursiveResult->outcome, Data::SemanticLiveRefreshOutcome::Rejected);
    QCOMPARE(recursiveResult->code, QStringLiteral("semantic-live-refresh-context-changed"));
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(0));
    QCoreApplication::processEvents();
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(0));
    QCOMPARE(completionCount, 1);
}

void EtherCATSemanticRuntimeTests::testExecutorSurvivesCompletionDeletingExecutor()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize(false, true, 20);
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    const Data::SemanticSignalRuntimeState signal = context.signalStates.constFirst();

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = context.controllerId;
    request.scope = context.scope;
    request.targets = {{signal.target.deviceId, signal.target.signalId}};
    request.expectedContextHash = context.contextHash;
    request.correlationId = QStringLiteral("semantic-refresh/delete-executor");
    fixture.provider.snapshotRequestStarted =
        [&fixture](const Data::RuntimeResourceSnapshotRequest &providerRequest) {
            fixture.provider.sendSnapshotResult({
                providerRequest,
                targetedSnapshot(
                    fixture.snapshot,
                    providerRequest,
                    {},
                    fixture.snapshot.snapshotSequence + 70,
                    fixture.snapshot.captureCycle + 70,
                    fixture.snapshot.controllerTimestampNs + 700),
                {},
            });
        };
    fixture.provider.failSnapshotStartAfterCallback = true;
    bool completionReceived = false;
    QObject::connect(
        fixture.executor.get(),
        &Core::SemanticRuntimeService::liveRefreshCompleted,
        fixture.executor.get(),
        [&fixture, &completionReceived] {
            completionReceived = true;
            fixture.executor.reset();
        },
        Qt::DirectConnection);

    QCOMPARE(
        fixture.executor->requestLiveRefresh(request).outcome,
        Data::SemanticLiveRefreshOutcome::Accepted);
    QTRY_VERIFY(completionReceived);
    QVERIFY(!fixture.executor);
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
    QTest::qWait(40);
    QVERIFY(!fixture.executor);
}

void EtherCATSemanticRuntimeTests::testExecutorExpiresAndSeparatesLiveCaptureCohorts()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize(false, true, 5000, 5000, [&now] {
        return now;
    });
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext initial = fixture.executor->contexts().constFirst();
    QVERIFY(initial.signalStates.size() >= 2);
    const Data::SemanticSignalRuntimeState first = initial.signalStates.at(0);
    const Data::SemanticSignalRuntimeState second = initial.signalStates.at(1);

    const auto refreshOne = [&fixture, &initial, &now](
                                const Data::SemanticSignalRuntimeState &signal,
                                QStringView correlation,
                                quint64 captureCycle,
                                quint64 timestampNs) {
        Data::SemanticLiveRefreshRequest request;
        request.controllerId = initial.controllerId;
        request.scope = initial.scope;
        request.targets = {{signal.target.deviceId, signal.target.signalId}};
        request.expectedContextHash = initial.contextHash;
        request.correlationId = correlation.toString();
        if (fixture.executor->requestLiveRefresh(request).outcome
            != Data::SemanticLiveRefreshOutcome::Accepted) {
            return false;
        }
        QCoreApplication::processEvents();
        if (!fixture.provider.pendingSnapshotRequest)
            return false;
        const Data::RuntimeResourceSnapshotRequest providerRequest
            = *fixture.provider.pendingSnapshotRequest;
        Data::RuntimeResourceSnapshot snapshot = targetedSnapshot(
            fixture.snapshot, providerRequest, {}, captureCycle, captureCycle, timestampNs);
        snapshot.receivedAt = now;
        fixture.provider.sendSnapshotResult({providerRequest, snapshot, {}});
        return true;
    };

    QVERIFY(refreshOne(first, u"semantic-refresh/cohort/first", 200, 2000));
    QVERIFY(refreshOne(second, u"semantic-refresh/cohort/second", 201, 2010));
    const Data::SemanticRuntimeContext mixed = fixture.executor->contexts().constFirst();
    const auto stateFor = [](const Data::SemanticRuntimeContext &context,
                             const Data::SemanticRuntimeTarget &target) {
        return std::find_if(
            context.signalStates.cbegin(),
            context.signalStates.cend(),
            [&target](const Data::SemanticSignalRuntimeState &state) {
                return state.target == target;
            });
    };
    const auto mixedFirst = stateFor(mixed, first.target);
    const auto mixedSecond = stateFor(mixed, second.target);
    QVERIFY(mixedFirst != mixed.signalStates.cend());
    QVERIFY(mixedSecond != mixed.signalStates.cend());
    QCOMPARE(mixedFirst->availability, Data::SemanticSignalAvailability::Ready);
    QCOMPARE(mixedSecond->availability, Data::SemanticSignalAvailability::Ready);
    QCOMPARE(mixedFirst->captureCycle, quint64(200));
    QCOMPARE(mixedSecond->captureCycle, quint64(201));
    QVERIFY(mixed.detail.startsWith(QStringLiteral("runtime-resource-snapshot-incoherent:")));
    QVERIFY(!mixed.detail.startsWith(QStringLiteral("semantic-runtime-ready:")));

    now = now.addMSecs(5001);
    fixture.provider.publishConnectionSnapshot(fixture.provider.snapshot);
    const Data::SemanticRuntimeContext stale = fixture.executor->contexts().constFirst();
    const auto staleFirst = stateFor(stale, first.target);
    const auto staleSecond = stateFor(stale, second.target);
    QVERIFY(staleFirst != stale.signalStates.cend());
    QVERIFY(staleSecond != stale.signalStates.cend());
    QCOMPARE(staleFirst->availability, Data::SemanticSignalAvailability::Stale);
    QCOMPARE(staleSecond->availability, Data::SemanticSignalAvailability::Stale);
    QCOMPARE(staleFirst->quality.state, Data::RuntimeResourceQualityState::Stale);
    QCOMPARE(staleSecond->quality.state, Data::RuntimeResourceQualityState::Stale);
    QVERIFY(!staleFirst->snapshotComplete);
    QVERIFY(!staleSecond->snapshotComplete);
    QCOMPARE(staleFirst->observedAt, now.addMSecs(-5001));
    QCOMPARE(staleSecond->observedAt, now.addMSecs(-5001));
}

void EtherCATSemanticRuntimeTests::testExecutorExecutesApi038Xb6Action()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/happy");
    setApi038DigitalOutputParameters(request);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, request, context);
    QVERIFY_RESULT(plan);

    const Data::SemanticOperationRecord approved
        = submitAndApprove(*fixture.executor, context, request);
    QCOMPARE(approved.state, Data::SemanticOperationState::Approved);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest.has_value());
    QCOMPARE(fixture.provider.pendingRequestCount(), 1);

    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    const Data::RuntimeResourceSnapshot beforeSnapshot = targetedSnapshot(
        fixture.snapshot, beforeRequest, {}, 2, 110, 1100);
    const Data::RuntimeResourceSnapshotResult beforeResult{
        beforeRequest, beforeSnapshot, {}};
    QVERIFY(beforeResult.isValid());
    fixture.provider.sendSnapshotResult(beforeResult);
    QVERIFY(fixture.provider.pendingPolicyRequest);

    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    const Data::RuntimeOutputGroupPolicy policy
        = outputPolicy(policyRequest, plan->groups().constFirst(), 41);
    const Data::RuntimeOutputGroupPolicyResult policyResult{policyRequest, policy, {}};
    QVERIFY(policyResult.isValid());
    fixture.provider.sendPolicyResult(policyResult);
    QVERIFY(fixture.provider.pendingStateRequest);

    const Data::RuntimeOutputTransactionStateRequest stateRequest
        = *fixture.provider.pendingStateRequest;
    const Data::RuntimeOutputTransactionState state = idleOutputState(stateRequest, 41);
    const Data::RuntimeOutputTransactionStateResult stateResult{stateRequest, state, {}};
    QVERIFY(stateResult.isValid());
    fixture.provider.sendStateResult(stateResult);
    QVERIFY(fixture.provider.pendingApplyRequest);

    const Data::RuntimeOutputTransactionRequest applyRequest
        = *fixture.provider.pendingApplyRequest;
    QCOMPARE(applyRequest.completeGroupWrites.size(), qsizetype(16));
    QCOMPARE(applyRequest.completeGroupWrites, plan->steps().constFirst().completeGroupWrites());
    QByteArray previousResourceId;
    for (const Data::RuntimeOutputValueWrite &write : applyRequest.completeGroupWrites) {
        QVERIFY(previousResourceId.isEmpty() || previousResourceId < write.resourceId.value);
        previousResourceId = write.resourceId.value;
    }
    const Data::RuntimeOutputTransactionState appliedState = completedOutputState(
        applyRequest, Data::RuntimeOutputTransactionOutcome::Applied);
    const Data::RuntimeOutputTransactionResult appliedResult{
        applyRequest,
        Data::RuntimeOutputTransactionOutcome::Applied,
        true,
        appliedState,
        {},
    };
    QVERIFY(appliedResult.isValid());
    fixture.provider.sendApplyResult(appliedResult);
    QVERIFY(fixture.provider.pendingSnapshotRequest);

    const Data::RuntimeResourceSnapshotRequest afterRequest
        = *fixture.provider.pendingSnapshotRequest;
    const Data::RuntimeResourceSnapshot afterSnapshot = targetedSnapshot(
        fixture.snapshot,
        afterRequest,
        applyRequest.completeGroupWrites,
        3,
        appliedState.appliedCycle + 1,
        3100);
    const Data::RuntimeResourceSnapshotResult afterResult{
        afterRequest, afterSnapshot, {}};
    QVERIFY(afterResult.isValid());
    fixture.provider.sendSnapshotResult(afterResult);
    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest postStateRequest
        = *fixture.provider.pendingStateRequest;
    Data::RuntimeOutputTransactionState confirmedState = appliedState;
    confirmedState.controllerTimestampNs = afterSnapshot.controllerTimestampNs + 1;
    const Data::RuntimeOutputTransactionStateResult postStateResult{
        postStateRequest, confirmedState, {}};
    QVERIFY(postStateResult.isValid());
    fixture.provider.sendStateResult(postStateResult);

    QTRY_VERIFY(fixture.executor->operation(request.operationId).has_value());
    const std::optional<Data::SemanticOperationRecord> completed
        = fixture.executor->operation(request.operationId);
    QVERIFY(completed);
    QCOMPARE(completed->state, Data::SemanticOperationState::Succeeded);
    QCOMPARE(completed->appliedCycle, appliedState.appliedCycle);
    QVERIFY(completed->beforeSnapshot);
    QVERIFY(completed->afterSnapshot);
    QCOMPARE(
        fixture.provider.requestTrace,
        QStringList({
            QStringLiteral("snapshot-before"),
            QStringLiteral("policy"),
            QStringLiteral("state-pre"),
            QStringLiteral("apply"),
            QStringLiteral("snapshot-after"),
            QStringLiteral("state-post"),
        }));
    QCOMPARE(fixture.provider.maximumConcurrentRequests, 1);
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorFailsClosedBeforeApply_data()
{
    QTest::addColumn<int>("mismatch");
    QTest::newRow("group-digest") << 0;
    QTest::newRow("resource-count") << 1;
    QTest::newRow("recovery-policy") << 2;
    QTest::newRow("maximum-ttl") << 3;
    QTest::newRow("output-generation") << 4;
}

void EtherCATSemanticRuntimeTests::testExecutorFailsClosedBeforeApply()
{
    QFETCH(int, mismatch);
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        QStringLiteral("operation/api038/executor/mismatch/%1").arg(mismatch));
    setApi038DigitalOutputParameters(request);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, request, context);
    QVERIFY_RESULT(plan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, request).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult(
        {
            beforeRequest,
            targetedSnapshot(fixture.snapshot, beforeRequest, {}, 2, 110, 1100),
            {},
        });
    QVERIFY(fixture.provider.pendingPolicyRequest);

    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    Data::RuntimeOutputGroupPolicy policy
        = outputPolicy(policyRequest, plan->groups().constFirst(), 41);
    if (mismatch == 0) {
        policy.completeGroupRecordDigest[0] ^= '\x01';
    } else if (mismatch == 1) {
        --policy.completeResourceCount;
    } else if (mismatch == 2) {
        policy.recoveryPolicy = policy.recoveryPolicy
                                        == Data::RuntimeOutputRecoveryPolicy::HoldSafe
                                    ? Data::RuntimeOutputRecoveryPolicy::ReturnTask
                                    : Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    } else if (mismatch == 3) {
        --policy.maximumTtlCycles;
    }
    const Data::RuntimeOutputGroupPolicyResult policyResult{policyRequest, policy, {}};
    QVERIFY(policyResult.isValid());
    fixture.provider.sendPolicyResult(policyResult);

    if (mismatch == 4) {
        QVERIFY(fixture.provider.pendingStateRequest);
        const Data::RuntimeOutputTransactionStateRequest stateRequest
            = *fixture.provider.pendingStateRequest;
        const Data::RuntimeOutputTransactionState state = idleOutputState(stateRequest, 42);
        const Data::RuntimeOutputTransactionStateResult stateResult{stateRequest, state, {}};
        QVERIFY(stateResult.isValid());
        fixture.provider.sendStateResult(stateResult);
    }

    QTRY_VERIFY(fixture.executor->operation(request.operationId).has_value());
    const std::optional<Data::SemanticOperationRecord> failed
        = fixture.executor->operation(request.operationId);
    QVERIFY(failed);
    QCOMPARE(failed->state, Data::SemanticOperationState::Failed);
    QVERIFY(fixture.provider.applyRequests.isEmpty());
    QVERIFY(!fixture.provider.requestTrace.contains(QStringLiteral("apply")));
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorReconcilesUnknownOutput_data()
{
    QTest::addColumn<bool>("recovered");
    QTest::newRow("later-applied") << false;
    QTest::newRow("later-applied-then-recovered") << true;
}

void EtherCATSemanticRuntimeTests::testExecutorReconcilesUnknownOutput()
{
    QFETCH(bool, recovered);
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        recovered ? u"operation/api038/executor/unknown/recovered"
                  : u"operation/api038/executor/unknown/applied");
    setApi038DigitalOutputParameters(request);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, request, context);
    QVERIFY_RESULT(plan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, request).state,
        Data::SemanticOperationState::Approved);
    const Data::SemanticOperationRequest queuedRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:clear-outputs",
        recovered ? u"operation/api038/executor/unknown/queued-after-recovered"
                  : u"operation/api038/executor/unknown/queued-after-applied");
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, queuedRequest).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult(
        {
            beforeRequest,
            targetedSnapshot(fixture.snapshot, beforeRequest, {}, 2, 110, 1100),
            {},
        });
    QVERIFY(fixture.provider.pendingPolicyRequest);
    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    fixture.provider.sendPolicyResult(
        {
            policyRequest,
            outputPolicy(policyRequest, plan->groups().constFirst(), 41),
            {},
        });
    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest stateRequest
        = *fixture.provider.pendingStateRequest;
    fixture.provider.sendStateResult(
        {stateRequest, idleOutputState(stateRequest, 41), {}});
    QVERIFY(fixture.provider.pendingApplyRequest);
    const Data::RuntimeOutputTransactionRequest applyRequest
        = *fixture.provider.pendingApplyRequest;

    const Data::RuntimeOutputTransactionResult unknownResult{
        applyRequest,
        Data::RuntimeOutputTransactionOutcome::OutcomeUnknown,
        false,
        {},
        uncertainOutputError(),
    };
    QVERIFY(unknownResult.isValid());
    fixture.provider.sendApplyResult(unknownResult, false);
    const std::optional<Data::SemanticOperationRecord> unknown
        = fixture.executor->operation(request.operationId);
    QVERIFY(unknown);
    QCOMPARE(unknown->state, Data::SemanticOperationState::OutcomeUnknown);
    QCOMPARE(fixture.provider.applyRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.pendingRequestCount(), 1);
    const std::optional<Data::SemanticOperationRecord> stillQueued
        = fixture.executor->operation(queuedRequest.operationId);
    QVERIFY(stillQueued);
    QCOMPARE(stillQueued->state, Data::SemanticOperationState::Approved);
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(1));

    const Data::RuntimeOutputTransactionOutcome outcome
        = recovered ? Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered
                    : Data::RuntimeOutputTransactionOutcome::Applied;
    const Data::RuntimeOutputTransactionState reconciledState
        = completedOutputState(applyRequest, outcome);
    const Data::RuntimeOutputTransactionResult reconciledResult{
        applyRequest,
        outcome,
        false,
        reconciledState,
        {},
    };
    QVERIFY(reconciledResult.isValid());
    fixture.provider.sendApplyResult(reconciledResult);

    if (!recovered) {
        QVERIFY(fixture.provider.pendingSnapshotRequest);
        const Data::RuntimeResourceSnapshotRequest afterRequest
            = *fixture.provider.pendingSnapshotRequest;
        const Data::RuntimeResourceSnapshot afterSnapshot = targetedSnapshot(
            fixture.snapshot,
            afterRequest,
            applyRequest.completeGroupWrites,
            3,
            reconciledState.appliedCycle + 1,
            3100);
        fixture.provider.sendSnapshotResult(
            {afterRequest, afterSnapshot, {}});
        QVERIFY(fixture.provider.pendingStateRequest);
        const Data::RuntimeOutputTransactionStateRequest postStateRequest
            = *fixture.provider.pendingStateRequest;
        Data::RuntimeOutputTransactionState confirmedState = reconciledState;
        confirmedState.controllerTimestampNs = afterSnapshot.controllerTimestampNs + 1;
        fixture.provider.sendStateResult(
            {postStateRequest, confirmedState, {}});
    }

    const std::optional<Data::SemanticOperationRecord> terminal
        = fixture.executor->operation(request.operationId);
    QVERIFY(terminal);
    QCOMPARE(
        terminal->state,
        recovered ? Data::SemanticOperationState::Expired
                  : Data::SemanticOperationState::Succeeded);
    QCOMPARE(terminal->appliedCycle, reconciledState.appliedCycle);
    QCOMPARE(fixture.provider.applyRequests.size(), qsizetype(1));
    QCOMPARE(
        fixture.provider.requestTrace.count(QStringLiteral("apply")),
        1);
    QCOMPARE(
        fixture.provider.requestTrace.contains(QStringLiteral("snapshot-after")),
        !recovered);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QCOMPARE(
        fixture.provider.snapshotRequests.size(),
        recovered ? qsizetype(2) : qsizetype(3));
    const std::optional<Data::SemanticOperationRecord> dequeued
        = fixture.executor->operation(queuedRequest.operationId);
    QVERIFY(dequeued);
    QCOMPARE(dequeued->state, Data::SemanticOperationState::Executing);
    const QList<Data::SemanticRuntimeAuditEvent> audit
        = fixture.executor->audit(context.controllerId);
    QVERIFY(std::any_of(
        audit.cbegin(),
        audit.cend(),
        [](const Data::SemanticRuntimeAuditEvent &event) {
            return event.kind == Data::SemanticAuditEventKind::OutcomeReconciled;
        }));
}

void EtherCATSemanticRuntimeTests::testExecutorIgnoresMismatchedCorrelations()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/wrong-correlation");
    setApi038DigitalOutputParameters(request);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, request, context);
    QVERIFY_RESULT(plan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, request).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    Data::RuntimeResourceSnapshotRequest wrongSnapshotRequest = beforeRequest;
    wrongSnapshotRequest.correlationId.append(QStringLiteral("-wrong"));
    const Data::RuntimeResourceSnapshotResult wrongSnapshotResult{
        wrongSnapshotRequest,
        targetedSnapshot(fixture.snapshot, wrongSnapshotRequest, {}, 2, 110, 1100),
        {},
    };
    QVERIFY(wrongSnapshotResult.isValid());
    fixture.provider.sendSnapshotResult(wrongSnapshotResult, false);
    QVERIFY(fixture.provider.pendingSnapshotRequest);
    QCOMPARE(*fixture.provider.pendingSnapshotRequest, beforeRequest);
    QVERIFY(!fixture.provider.pendingPolicyRequest);
    fixture.provider.sendSnapshotResult(
        {
            beforeRequest,
            targetedSnapshot(fixture.snapshot, beforeRequest, {}, 2, 110, 1100),
            {},
        });

    QVERIFY(fixture.provider.pendingPolicyRequest);
    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    Data::RuntimeOutputGroupPolicyRequest wrongPolicyRequest = policyRequest;
    wrongPolicyRequest.correlationId.append(QStringLiteral("-wrong"));
    const Data::RuntimeOutputGroupPolicyResult wrongPolicyResult{
        wrongPolicyRequest,
        outputPolicy(wrongPolicyRequest, plan->groups().constFirst(), 41),
        {},
    };
    QVERIFY(wrongPolicyResult.isValid());
    fixture.provider.sendPolicyResult(wrongPolicyResult, false);
    QVERIFY(fixture.provider.pendingPolicyRequest);
    QCOMPARE(*fixture.provider.pendingPolicyRequest, policyRequest);
    QVERIFY(!fixture.provider.pendingStateRequest);
    fixture.provider.sendPolicyResult(
        {
            policyRequest,
            outputPolicy(policyRequest, plan->groups().constFirst(), 41),
            {},
        });

    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest stateRequest
        = *fixture.provider.pendingStateRequest;
    Data::RuntimeOutputTransactionStateRequest wrongStateRequest = stateRequest;
    wrongStateRequest.correlationId.append(QStringLiteral("-wrong"));
    const Data::RuntimeOutputTransactionStateResult wrongStateResult{
        wrongStateRequest, idleOutputState(wrongStateRequest, 41), {}};
    QVERIFY(wrongStateResult.isValid());
    fixture.provider.sendStateResult(wrongStateResult, false);
    QVERIFY(fixture.provider.pendingStateRequest);
    QCOMPARE(*fixture.provider.pendingStateRequest, stateRequest);
    QVERIFY(!fixture.provider.pendingApplyRequest);
    fixture.provider.sendStateResult(
        {stateRequest, idleOutputState(stateRequest, 41), {}});

    QVERIFY(fixture.provider.pendingApplyRequest);
    const Data::RuntimeOutputTransactionRequest applyRequest
        = *fixture.provider.pendingApplyRequest;
    Data::RuntimeOutputTransactionRequest wrongApplyRequest = applyRequest;
    wrongApplyRequest.operationId.value[0] ^= '\x01';
    const Data::RuntimeOutputTransactionState wrongAppliedState = completedOutputState(
        wrongApplyRequest, Data::RuntimeOutputTransactionOutcome::Applied);
    const Data::RuntimeOutputTransactionResult wrongApplyResult{
        wrongApplyRequest,
        Data::RuntimeOutputTransactionOutcome::Applied,
        true,
        wrongAppliedState,
        {},
    };
    QVERIFY(wrongApplyResult.isValid());
    fixture.provider.sendApplyResult(wrongApplyResult, false);
    QVERIFY(fixture.provider.pendingApplyRequest);
    QCOMPARE(*fixture.provider.pendingApplyRequest, applyRequest);
    QVERIFY(!fixture.provider.pendingSnapshotRequest);

    const Data::RuntimeOutputTransactionState appliedState = completedOutputState(
        applyRequest, Data::RuntimeOutputTransactionOutcome::Applied);
    fixture.provider.sendApplyResult(
        {
            applyRequest,
            Data::RuntimeOutputTransactionOutcome::Applied,
            true,
            appliedState,
            {},
        });
    QVERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest afterRequest
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult(
        {
            afterRequest,
            targetedSnapshot(
                fixture.snapshot,
                afterRequest,
                applyRequest.completeGroupWrites,
                3,
                appliedState.appliedCycle + 1,
                3100),
            {},
        });
    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest postStateRequest
        = *fixture.provider.pendingStateRequest;
    Data::RuntimeOutputTransactionStateRequest wrongPostStateRequest = postStateRequest;
    wrongPostStateRequest.correlationId.append(QStringLiteral("-wrong"));
    Data::RuntimeOutputTransactionState confirmedState = appliedState;
    confirmedState.controllerTimestampNs = 3200;
    const Data::RuntimeOutputTransactionStateResult wrongPostStateResult{
        wrongPostStateRequest, confirmedState, {}};
    QVERIFY(wrongPostStateResult.isValid());
    fixture.provider.sendStateResult(wrongPostStateResult, false);
    QVERIFY(fixture.provider.pendingStateRequest);
    QCOMPARE(*fixture.provider.pendingStateRequest, postStateRequest);
    fixture.provider.sendStateResult(
        {postStateRequest, confirmedState, {}});

    const std::optional<Data::SemanticOperationRecord> completed
        = fixture.executor->operation(request.operationId);
    QVERIFY(completed);
    QCOMPARE(completed->state, Data::SemanticOperationState::Succeeded);
    QCOMPARE(fixture.provider.applyRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(2));
    QCOMPARE(fixture.provider.maximumConcurrentRequests, 1);
}

void EtherCATSemanticRuntimeTests::testExecutorRejectsSessionChangeBeforeWrite()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/session-change");
    setApi038DigitalOutputParameters(request);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, request, context);
    QVERIFY_RESULT(plan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, request).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult(
        {
            beforeRequest,
            targetedSnapshot(fixture.snapshot, beforeRequest, {}, 2, 110, 1100),
            {},
        });
    QVERIFY(fixture.provider.pendingPolicyRequest);
    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    fixture.provider.sendPolicyResult(
        {
            policyRequest,
            outputPolicy(policyRequest, plan->groups().constFirst(), 41),
            {},
        });
    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest stateRequest
        = *fixture.provider.pendingStateRequest;

    Data::ControllerConnectionSnapshot changedSession = fixture.provider.snapshot;
    ++changedSession.sessionGeneration;
    QVERIFY(changedSession.session);
    ++changedSession.session->sessionId;
    changedSession.session->controlLeaseOwnerSessionId = changedSession.session->sessionId;
    fixture.provider.publishConnectionSnapshot(changedSession);
    fixture.provider.sendStateResult(
        {stateRequest, idleOutputState(stateRequest, 41), {}});

    const std::optional<Data::SemanticOperationRecord> failed
        = fixture.executor->operation(request.operationId);
    QVERIFY(failed);
    QCOMPARE(failed->state, Data::SemanticOperationState::Failed);
    QVERIFY(fixture.provider.applyRequests.isEmpty());
    QVERIFY(!fixture.provider.requestTrace.contains(QStringLiteral("apply")));
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorSerializesPerController()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();

    Data::SemanticOperationRequest firstRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/fifo/first");
    setApi038DigitalOutputParameters(firstRequest);
    const Utils::Result<SemanticActionPlan> firstPlan
        = buildSemanticActionPlan(*fixture.evidence, firstRequest, context);
    QVERIFY_RESULT(firstPlan);
    Data::SemanticOperationRequest secondRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:clear-outputs",
        u"operation/api038/executor/fifo/second");
    const Utils::Result<SemanticActionPlan> secondPlan
        = buildSemanticActionPlan(*fixture.evidence, secondRequest, context);
    QVERIFY_RESULT(secondPlan);

    QCOMPARE(
        submitAndApprove(*fixture.executor, context, firstRequest).state,
        Data::SemanticOperationState::Approved);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, secondRequest).state,
        Data::SemanticOperationState::Approved);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(1));
    const std::optional<Data::SemanticOperationRecord> queuedSecond
        = fixture.executor->operation(secondRequest.operationId);
    QVERIFY(queuedSecond);
    QCOMPARE(queuedSecond->state, Data::SemanticOperationState::Approved);

    const auto completeActive =
        [&fixture](
            const SemanticActionPlan &plan,
            quint64 outputGeneration,
            quint64 appliedCycle,
            quint64 sequenceBase) {
            if (!fixture.provider.pendingSnapshotRequest)
                return false;
            const Data::RuntimeResourceSnapshotRequest beforeRequest
                = *fixture.provider.pendingSnapshotRequest;
            const Data::RuntimeResourceSnapshotResult beforeResult{
                beforeRequest,
                targetedSnapshot(
                    fixture.snapshot,
                    beforeRequest,
                    {},
                    sequenceBase,
                    appliedCycle - 2,
                    appliedCycle * 10),
                {},
            };
            if (!beforeResult.isValid())
                return false;
            fixture.provider.sendSnapshotResult(beforeResult);
            if (!fixture.provider.pendingPolicyRequest)
                return false;
            const Data::RuntimeOutputGroupPolicyRequest policyRequest
                = *fixture.provider.pendingPolicyRequest;
            const Data::RuntimeOutputGroupPolicyResult policyResult{
                policyRequest,
                outputPolicy(
                    policyRequest, plan.groups().constFirst(), outputGeneration),
                {},
            };
            if (!policyResult.isValid())
                return false;
            fixture.provider.sendPolicyResult(policyResult);
            if (!fixture.provider.pendingStateRequest)
                return false;
            const Data::RuntimeOutputTransactionStateRequest stateRequest
                = *fixture.provider.pendingStateRequest;
            const Data::RuntimeOutputTransactionStateResult stateResult{
                stateRequest, idleOutputState(stateRequest, outputGeneration), {}};
            if (!stateResult.isValid())
                return false;
            fixture.provider.sendStateResult(stateResult);
            if (!fixture.provider.pendingApplyRequest)
                return false;
            const Data::RuntimeOutputTransactionRequest applyRequest
                = *fixture.provider.pendingApplyRequest;
            const Data::RuntimeOutputTransactionState appliedState = completedOutputState(
                applyRequest,
                Data::RuntimeOutputTransactionOutcome::Applied,
                appliedCycle);
            const Data::RuntimeOutputTransactionResult appliedResult{
                applyRequest,
                Data::RuntimeOutputTransactionOutcome::Applied,
                true,
                appliedState,
                {},
            };
            if (!appliedResult.isValid())
                return false;
            fixture.provider.sendApplyResult(appliedResult);
            if (!fixture.provider.pendingSnapshotRequest)
                return false;
            const Data::RuntimeResourceSnapshotRequest afterRequest
                = *fixture.provider.pendingSnapshotRequest;
            const Data::RuntimeResourceSnapshotResult afterResult{
                afterRequest,
                targetedSnapshot(
                    fixture.snapshot,
                    afterRequest,
                    applyRequest.completeGroupWrites,
                    sequenceBase + 1,
                    appliedCycle + 1,
                    appliedCycle * 10 + 1),
                {},
            };
            if (!afterResult.isValid())
                return false;
            fixture.provider.sendSnapshotResult(afterResult);
            if (!fixture.provider.pendingStateRequest)
                return false;
            const Data::RuntimeOutputTransactionStateRequest postStateRequest
                = *fixture.provider.pendingStateRequest;
            Data::RuntimeOutputTransactionState confirmedState = appliedState;
            confirmedState.controllerTimestampNs = appliedCycle * 10 + 2;
            const Data::RuntimeOutputTransactionStateResult postStateResult{
                postStateRequest, confirmedState, {}};
            if (!postStateResult.isValid())
                return false;
            fixture.provider.sendStateResult(postStateResult);
            return true;
        };

    QVERIFY(completeActive(*firstPlan, 41, 120, 2));
    const std::optional<Data::SemanticOperationRecord> firstCompleted
        = fixture.executor->operation(firstRequest.operationId);
    QVERIFY(firstCompleted);
    QCOMPARE(firstCompleted->state, Data::SemanticOperationState::Succeeded);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QCOMPARE(fixture.provider.snapshotRequests.size(), qsizetype(3));
    const std::optional<Data::SemanticOperationRecord> secondExecuting
        = fixture.executor->operation(secondRequest.operationId);
    QVERIFY(secondExecuting);
    QCOMPARE(secondExecuting->state, Data::SemanticOperationState::Executing);

    QVERIFY(completeActive(*secondPlan, 43, 140, 4));
    const std::optional<Data::SemanticOperationRecord> secondCompleted
        = fixture.executor->operation(secondRequest.operationId);
    QVERIFY(secondCompleted);
    QCOMPARE(secondCompleted->state, Data::SemanticOperationState::Succeeded);
    QCOMPARE(
        fixture.provider.requestTrace,
        QStringList({
            QStringLiteral("snapshot-before"),
            QStringLiteral("policy"),
            QStringLiteral("state-pre"),
            QStringLiteral("apply"),
            QStringLiteral("snapshot-after"),
            QStringLiteral("state-post"),
            QStringLiteral("snapshot-before"),
            QStringLiteral("policy"),
            QStringLiteral("state-pre"),
            QStringLiteral("apply"),
            QStringLiteral("snapshot-after"),
            QStringLiteral("state-post"),
        }));
    QCOMPARE(fixture.provider.maximumConcurrentRequests, 1);
    QCOMPARE(fixture.provider.pendingRequestCount(), 0);
}

void EtherCATSemanticRuntimeTests::testExecutorRejectsExpiredSameValueProof()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    const Data::SemanticOperationRequest request = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:clear-outputs",
        u"operation/api038/executor/expired-same-value");
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, request, context);
    QVERIFY_RESULT(plan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, request).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QVERIFY(advanceExecutorToApply(fixture, *plan));
    const Data::RuntimeOutputTransactionRequest applyRequest
        = *fixture.provider.pendingApplyRequest;
    QVERIFY(std::all_of(
        applyRequest.completeGroupWrites.cbegin(),
        applyRequest.completeGroupWrites.cend(),
        [](const Data::RuntimeOutputValueWrite &write) {
            return write.value.value.metaType().id() == QMetaType::Bool
                   && !write.value.value.toBool();
        }));
    const Data::RuntimeOutputTransactionState appliedState = completedOutputState(
        applyRequest, Data::RuntimeOutputTransactionOutcome::Applied);
    fixture.provider.sendApplyResult(
        {
            applyRequest,
            Data::RuntimeOutputTransactionOutcome::Applied,
            true,
            appliedState,
            {},
        });
    QVERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest afterRequest
        = *fixture.provider.pendingSnapshotRequest;
    const Data::RuntimeResourceSnapshot afterSnapshot = targetedSnapshot(
        fixture.snapshot,
        afterRequest,
        applyRequest.completeGroupWrites,
        3,
        appliedState.appliedCycle + 1,
        3100);
    fixture.provider.sendSnapshotResult({afterRequest, afterSnapshot, {}});
    QVERIFY(fixture.provider.pendingStateRequest);

    const Data::RuntimeOutputTransactionStateRequest postStateRequest
        = *fixture.provider.pendingStateRequest;
    Data::RuntimeOutputTransactionState recoveredState = completedOutputState(
        applyRequest, Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered);
    recoveredState.controllerTimestampNs = afterSnapshot.controllerTimestampNs + 1;
    const Data::RuntimeOutputTransactionStateResult recoveredResult{
        postStateRequest, recoveredState, {}};
    QVERIFY(recoveredResult.isValid());
    fixture.provider.sendStateResult(recoveredResult);

    const std::optional<Data::SemanticOperationRecord> terminal
        = fixture.executor->operation(request.operationId);
    QVERIFY(terminal);
    QCOMPARE(terminal->state, Data::SemanticOperationState::Expired);
    QVERIFY(!terminal->afterSnapshot);
    QVERIFY(terminal->state != Data::SemanticOperationState::Succeeded);
}

void EtherCATSemanticRuntimeTests::testExecutorBlocksOldUnknownAcrossScopes()
{
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize();
    QVERIFY_RESULT(initialized);
    fixture.provider.allowReadsWhileApplyPending = true;
    const Data::SemanticRuntimeContext firstContext
        = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest firstRequest = api038ActionRequest(
        firstContext,
        u"embedlabs:project:action:xb6:set-outputs",
        u"operation/api038/executor/cross-scope/unknown");
    setApi038DigitalOutputParameters(firstRequest);
    const Utils::Result<SemanticActionPlan> firstPlan
        = buildSemanticActionPlan(*fixture.evidence, firstRequest, firstContext);
    QVERIFY_RESULT(firstPlan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, firstContext, firstRequest).state,
        Data::SemanticOperationState::Approved);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QVERIFY(advanceExecutorToApply(fixture, *firstPlan));
    const Data::RuntimeOutputTransactionRequest unresolvedApply
        = *fixture.provider.pendingApplyRequest;
    const Data::RuntimeOutputTransactionResult unknownResult{
        unresolvedApply,
        Data::RuntimeOutputTransactionOutcome::OutcomeUnknown,
        false,
        {},
        uncertainOutputError(),
    };
    QVERIFY(unknownResult.isValid());
    fixture.provider.sendApplyResult(unknownResult, false);
    const std::optional<Data::SemanticOperationRecord> unknown
        = fixture.executor->operation(firstRequest.operationId);
    QVERIFY(unknown);
    QCOMPARE(unknown->state, Data::SemanticOperationState::OutcomeUnknown);

    Data::ProjectSnapshot secondProject = factoryProject(*fixture.evidence);
    configureApi038V3ManualProject(secondProject, fixture.adapterManifests);
    const Data::RuntimeResourceCatalog secondCatalog
        = factoryCatalog(secondProject, *fixture.evidence);
    const Data::RuntimeSemanticMappingAttestation secondAttestation
        = factoryAttestation(secondCatalog, *fixture.evidence);
    fixture.snapshot = factorySnapshot(secondCatalog);
    fixture.projects.addProject(secondProject);
    fixture.provider.publishRuntimeContext(
        secondCatalog,
        fixture.snapshot,
        secondAttestation,
        executionConnectionSnapshot(
            secondCatalog.scope, secondCatalog.sessionGeneration, secondCatalog.epoch));

    const QList<Data::SemanticRuntimeContext> contexts = fixture.executor->contexts();
    const auto secondContext = std::find_if(
        contexts.cbegin(),
        contexts.cend(),
        [&secondCatalog](const Data::SemanticRuntimeContext &candidate) {
            return candidate.scope == secondCatalog.scope && candidate.complete;
        });
    QVERIFY(secondContext != contexts.cend());
    const Data::SemanticOperationRequest secondRequest = api038ActionRequest(
        *secondContext,
        u"embedlabs:project:action:xb6:clear-outputs",
        u"operation/api038/executor/cross-scope/blocked");
    const Utils::Result<SemanticActionPlan> secondPlan
        = buildSemanticActionPlan(*fixture.evidence, secondRequest, *secondContext);
    QVERIFY_RESULT(secondPlan);
    QCOMPARE(
        submitAndApprove(*fixture.executor, *secondContext, secondRequest).state,
        Data::SemanticOperationState::Approved);

    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    const Data::RuntimeResourceSnapshotRequest beforeRequest
        = *fixture.provider.pendingSnapshotRequest;
    fixture.provider.sendSnapshotResult(
        {
            beforeRequest,
            targetedSnapshot(fixture.snapshot, beforeRequest, {}, 2, 110, 1100),
            {},
        });
    QVERIFY(fixture.provider.pendingPolicyRequest);
    const Data::RuntimeOutputGroupPolicyRequest policyRequest
        = *fixture.provider.pendingPolicyRequest;
    fixture.provider.sendPolicyResult(
        {
            policyRequest,
            outputPolicy(policyRequest, secondPlan->groups().constFirst(), 41),
            {},
        });
    QVERIFY(fixture.provider.pendingStateRequest);
    const Data::RuntimeOutputTransactionStateRequest stateRequest
        = *fixture.provider.pendingStateRequest;

    fixture.provider.sendApplyResult(unknownResult, false);
    QCOMPARE(
        fixture.executor->operation(firstRequest.operationId)->state,
        Data::SemanticOperationState::OutcomeUnknown);
    QCOMPARE(
        fixture.executor->operation(secondRequest.operationId)->state,
        Data::SemanticOperationState::Executing);
    fixture.provider.sendStateResult(
        {stateRequest, idleOutputState(stateRequest, 41), {}});

    const std::optional<Data::SemanticOperationRecord> blocked
        = fixture.executor->operation(secondRequest.operationId);
    QVERIFY(blocked);
    QCOMPARE(blocked->state, Data::SemanticOperationState::Failed);
    QCOMPARE(fixture.provider.applyRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.applyRequests.constFirst(), unresolvedApply);
    QCOMPARE(
        fixture.executor->operation(firstRequest.operationId)->state,
        Data::SemanticOperationState::OutcomeUnknown);
    QVERIFY(fixture.provider.pendingApplyRequest);
    QCOMPARE(*fixture.provider.pendingApplyRequest, unresolvedApply);
}

void EtherCATSemanticRuntimeTests::
    testExecutorFreezesUnknownWhenInfrastructureDisappears_data()
{
    QTest::addColumn<bool>("destroyRegistry");
    QTest::newRow("provider-removed") << false;
    QTest::newRow("registry-destroyed") << true;
}

void EtherCATSemanticRuntimeTests::
    testExecutorFreezesUnknownWhenInfrastructureDisappears()
{
    QFETCH(bool, destroyRegistry);
    SemanticExecutorFixture fixture;
    const Utils::Result<> initialized = fixture.initialize(destroyRegistry);
    QVERIFY_RESULT(initialized);
    const Data::SemanticRuntimeContext context = fixture.executor->contexts().constFirst();
    Data::SemanticOperationRequest firstRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:set-outputs",
        destroyRegistry ? u"operation/api038/executor/registry-destroyed"
                        : u"operation/api038/executor/provider-removed");
    setApi038DigitalOutputParameters(firstRequest);
    const Utils::Result<SemanticActionPlan> plan
        = buildSemanticActionPlan(*fixture.evidence, firstRequest, context);
    QVERIFY_RESULT(plan);
    const Data::SemanticOperationRequest queuedRequest = api038ActionRequest(
        context,
        u"embedlabs:project:action:xb6:clear-outputs",
        destroyRegistry ? u"operation/api038/executor/registry-queued"
                        : u"operation/api038/executor/provider-queued");
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, firstRequest).state,
        Data::SemanticOperationState::Approved);
    QCOMPARE(
        submitAndApprove(*fixture.executor, context, queuedRequest).state,
        Data::SemanticOperationState::Approved);
    QTRY_VERIFY(fixture.provider.pendingSnapshotRequest);
    QVERIFY(advanceExecutorToApply(fixture, *plan));
    QCOMPARE(fixture.provider.applyRequests.size(), qsizetype(1));

    if (destroyRegistry) {
        QVERIFY(fixture.ownedRegistry);
        fixture.ownedRegistry.reset();
    } else {
        QVERIFY(fixture.registration);
        fixture.registration->remove();
    }
    QCoreApplication::processEvents();

    const std::optional<Data::SemanticOperationRecord> unknown
        = fixture.executor->operation(firstRequest.operationId);
    QVERIFY(unknown);
    QCOMPARE(unknown->state, Data::SemanticOperationState::OutcomeUnknown);
    const std::optional<Data::SemanticOperationRecord> queued
        = fixture.executor->operation(queuedRequest.operationId);
    QVERIFY(queued);
    QCOMPARE(queued->state, Data::SemanticOperationState::Approved);
    QCOMPARE(fixture.provider.applyRequests.size(), qsizetype(1));
    QCOMPARE(fixture.provider.pendingRequestCount(), 1);
}

void EtherCATSemanticRuntimeTests::testExecutorPublishesVerifiedReadOnlyContext()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(repositoryRoot.absoluteFilePath(
        "build/vendor_api_037_handoff/api037_handoff_569d39310549"));
    const QByteArray packageBytes = readFile(
        fixtureRoot.absoluteFilePath("artifacts/three-slave-manual-control-cfg3701.ecpkg"));
    const QByteArray projectBytes = readFile(
        fixtureRoot.absoluteFilePath("package_inputs/project.json"));
    const QString keyFileName
        = QStringLiteral(
              "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub");
    const QByteArray publicKey = readFile(
        fixtureRoot.absoluteFilePath(QStringLiteral("trust/") + keyFileName));
    if (packageBytes.isEmpty() || projectBytes.isEmpty() || publicKey.isEmpty())
        QSKIP("Transferred API-037 executor fixture is not present");

    QTemporaryDir temporary(
        systemTemporaryDirectoryTemplate(u"embed-labs-semantic-executor"));
    QVERIFY(temporary.isValid());
    const QString packageRoot = QDir(temporary.path()).filePath("packages");
    const QString trustRoot = QDir(temporary.path()).filePath("trust");
    const QString projectRoot = QDir(temporary.path()).filePath("projects");
    QVERIFY(QDir().mkpath(trustRoot));
    QVERIFY(writeFile(QDir(trustRoot).filePath(keyFileName), publicKey));

    auto evidenceRepository = std::make_shared<RuntimePackageEvidenceRepository>(
        packageRoot, trustRoot, projectRoot);
    const Utils::Result<VerifiedRuntimePackageEvidence> imported
        = evidenceRepository->import(packageBytes, projectBytes);
    QVERIFY_RESULT(imported);

    const Data::ProjectSnapshot project = factoryProject(*imported);
    const Data::ControllerConnectionScope scope = projectScope(project);
    const Data::RuntimeResourceCatalog catalog = factoryCatalog(project, *imported);
    const Data::RuntimeSemanticMappingAttestation attestation
        = factoryAttestation(catalog, *imported);
    Data::RuntimeResourceSnapshot snapshot = factorySnapshot(catalog);

    TestProjectService projects;
    projects.addProject(project);
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.VerifiedReadOnly");
    provider.publishSnapshot(connectedSnapshot(scope, catalog.sessionGeneration));
    provider.publishCatalog(catalog);
    provider.publishSemanticMappingAttestation(attestation);
    provider.publishResourceSnapshot(snapshot);
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    SemanticRuntimeExecutor executor(
        &projects, registry, nullptr, evidenceRepository);
    QCOMPARE(executor.contexts().size(), 1);
    const Data::SemanticRuntimeContext initial = executor.contexts().constFirst();
    QVERIFY(initial.complete);
    QCOMPARE(
        initial.bindingVerification.state,
        Data::SemanticBindingVerificationState::Verified);
    QCOMPARE(initial.mappingDigest, initial.controllerMappingDigest);
    QCOMPARE(initial.signalStates.size(), qsizetype(56));
    QVERIFY(initial.actionStates.isEmpty());
    QVERIFY(std::all_of(
        initial.signalStates.cbegin(),
        initial.signalStates.cend(),
        [](const Data::SemanticSignalRuntimeState &state) {
            return state.availability == Data::SemanticSignalAvailability::Ready
                   && state.snapshotComplete && state.captureCycle == 100
                   && state.controllerTimestampNs == 1000 && state.value.has_value();
        }));

    snapshot.snapshotSequence = 2;
    snapshot.captureCycle = 101;
    snapshot.controllerTimestampNs = 1010;
    for (Data::RuntimeResourceSample &sample : snapshot.samples)
        sample.controllerTimestampNs = snapshot.controllerTimestampNs;
    provider.publishResourceSnapshot(snapshot);

    const Data::SemanticRuntimeContext refreshed = executor.contexts().constFirst();
    QVERIFY(refreshed.complete);
    QCOMPARE(refreshed.contextHash, initial.contextHash);
    QVERIFY(std::all_of(
        refreshed.signalStates.cbegin(),
        refreshed.signalStates.cend(),
        [](const Data::SemanticSignalRuntimeState &state) {
            return state.availability == Data::SemanticSignalAvailability::Ready
                   && state.captureCycle == 101 && state.controllerTimestampNs == 1010;
        }));
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testRuntimeBootstrapIsSerializedAndIdempotent()
{
    RuntimeBootstrapFixture fixture;
    QVERIFY_RESULT(fixture.initialize());
    QVERIFY(fixture.evidence);

    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    TestProjectService projects;
    projects.addProject(fixture.project);
    const Data::ControllerConnectionScope scope = projectScope(fixture.project);
    const Data::RuntimeResourceCatalog firstCatalog
        = factoryCatalog(fixture.project, *fixture.evidence);

    CountingControllerProvider provider(
        "EtherCAT.SemanticRuntime.Tests.RuntimeBootstrap.Idempotent");
    Data::ControllerConnectionSnapshot disconnected;
    disconnected.scope = scope;
    disconnected.state = Data::ControllerConnectionState::Disconnected;
    provider.publishSnapshot(disconnected);
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    SemanticRuntimeExecutor executor(
        &projects, registry, nullptr, fixture.repository);
    QCoreApplication::processEvents();
    QCOMPARE(provider.runtimeResourceRefreshCalls, 0);
    QVERIFY(provider.semanticMappingAttestationRequests.isEmpty());

    const Data::ControllerConnectionSnapshot connected
        = connectedSnapshot(scope, firstCatalog.sessionGeneration);
    provider.publishSnapshot(connected);
    QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 1);
    QVERIFY(provider.semanticMappingAttestationRequests.isEmpty());

    provider.publishCatalog(firstCatalog);
    QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(1));
    const Data::RuntimeSemanticMappingAttestationRequest firstRequest
        = provider.semanticMappingAttestationRequests.constFirst();
    QVERIFY(firstRequest.isValid());
    QCOMPARE(firstRequest.scope, scope);
    QCOMPARE(firstRequest.sessionGeneration, firstCatalog.sessionGeneration);
    QCOMPARE(firstRequest.expectedEpoch, firstCatalog.epoch);
    QCOMPARE(firstRequest.expectedProof, fixture.evidence->semanticMappingProof());

    provider.publishSnapshot(connected);
    provider.publishCatalog(firstCatalog);
    provider.publishSemanticMappingAttestation(std::nullopt);
    QTest::qWait(10);
    QCOMPARE(provider.runtimeResourceRefreshCalls, 1);
    QCOMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(1));

    Data::RuntimeResourceCatalog secondCatalog = firstCatalog;
    ++secondCatalog.epoch.runtimeGeneration;
    ++secondCatalog.epoch.catalogRevision;
    secondCatalog.receivedAt = QDateTime::currentDateTimeUtc();
    provider.publishCatalog(secondCatalog);
    QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(2));
    QCOMPARE(
        provider.semanticMappingAttestationRequests.constLast().expectedEpoch,
        secondCatalog.epoch);
    QCOMPARE(
        provider.semanticMappingAttestationRequests.constLast().expectedProof,
        fixture.evidence->semanticMappingProof());
    QCOMPARE(provider.runtimeResourceRefreshCalls, 1);

    Data::ControllerConnectionSnapshot packageChanged = connected;
    packageChanged.package = Data::ControllerPackageSummary{};
    packageChanged.package->activeSlot = secondCatalog.epoch.activePackageSlot;
    packageChanged.package->activeGeneration
        = secondCatalog.epoch.activePackageGeneration;
    packageChanged.package->activeConfigurationId
        = secondCatalog.epoch.configurationId;
    packageChanged.package->controllerBootId = secondCatalog.epoch.controllerBootId;
    provider.publishSnapshot(packageChanged);
    QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 2);

    provider.semanticMappingAttestationRequestError = QStringLiteral("refresh still busy");
    provider.publishCatalog(secondCatalog);
    QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(3));
    provider.publishCatalog(secondCatalog);
    QTest::qWait(10);
    QCOMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(3));

    provider.publishResourceSnapshot(factorySnapshot(secondCatalog));
    QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(4));
    provider.publishResourceSnapshot(factorySnapshot(secondCatalog));
    QTest::qWait(10);
    QCOMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(4));

    provider.semanticMappingAttestationRequestError.reset();
    Data::ControllerConnectionSnapshot starting = packageChanged;
    starting.controlProgress.command = Data::ControllerControlCommand::StartDistributedClocks;
    starting.controlProgress.state = Data::ControllerControlState::Pending;
    provider.publishSnapshot(starting);
    QTest::qWait(10);
    QCOMPARE(provider.runtimeResourceRefreshCalls, 2);

    Data::ControllerConnectionSnapshot running = packageChanged;
    running.controllerState = Data::ControllerStateSummary{};
    running.controllerState->controllerBootId = secondCatalog.epoch.controllerBootId;
    running.controllerState->applicationActive = true;
    running.controllerState->busOperational = true;
    provider.publishSnapshot(running);
    QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 3);
    provider.publishCatalog(secondCatalog);
    QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(5));

    Data::ControllerConnectionSnapshot stopped = running;
    stopped.controllerState->applicationActive = false;
    stopped.controllerState->busOperational = false;
    provider.publishSnapshot(stopped);
    QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 4);
    provider.publishCatalog(secondCatalog);
    QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(6));
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testRuntimeBootstrapRetriesRejectedRefreshOnce()
{
    RuntimeBootstrapFixture fixture;
    QVERIFY_RESULT(fixture.initialize());
    QVERIFY(fixture.evidence);
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);
    const Data::ControllerConnectionScope scope = projectScope(fixture.project);
    const Data::RuntimeResourceCatalog catalog
        = factoryCatalog(fixture.project, *fixture.evidence);
    const Data::ControllerConnectionSnapshot connected
        = connectedSnapshot(scope, catalog.sessionGeneration);
    Data::RuntimeResourceSnapshotRequest completedRequest;
    completedRequest.correlationId = QStringLiteral("runtime-bootstrap/targeted-finished");
    completedRequest.scope = scope;
    completedRequest.sessionGeneration = catalog.sessionGeneration;
    completedRequest.expectedEpoch = catalog.epoch;
    completedRequest.resourceIds = {catalog.resources.constFirst().id};
    const Data::RuntimeResourceSnapshotResult completedResult{
        completedRequest,
        targetedSnapshot(factorySnapshot(catalog), completedRequest, {}, 2, 100, 1000),
        {},
    };
    QVERIFY(completedResult.isValid());

    {
        TestProjectService projects;
        projects.addProject(fixture.project);
        CountingControllerProvider provider(
            "EtherCAT.SemanticRuntime.Tests.RuntimeBootstrap.RetrySuccess");
        provider.runtimeResourceRefreshError = QStringLiteral("provider temporarily busy");
        provider.publishSnapshot(connected);
        provider.setAvailable(true);
        RegisteredObject registration(&provider);
        SemanticRuntimeExecutor executor(
            &projects, registry, nullptr, fixture.repository);

        QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 1);
        QTest::qWait(10);
        QCOMPARE(provider.runtimeResourceRefreshCalls, 1);

        provider.runtimeResourceRefreshError.reset();
        provider.publishResourceSnapshotRequestFinished(completedResult);
        QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 2);
        provider.publishCatalog(catalog);
        QTRY_COMPARE(provider.semanticMappingAttestationRequests.size(), qsizetype(1));
        provider.publishResourceSnapshot(factorySnapshot(catalog));
        QTest::qWait(10);
        QCOMPARE(provider.runtimeResourceRefreshCalls, 2);
        QCOMPARE(provider.mutationCalls, 0);
    }

    {
        TestProjectService projects;
        projects.addProject(fixture.project);
        CountingControllerProvider provider(
            "EtherCAT.SemanticRuntime.Tests.RuntimeBootstrap.RetryRejected");
        provider.runtimeResourceRefreshError = QStringLiteral("provider remains busy");
        provider.publishSnapshot(connected);
        provider.setAvailable(true);
        RegisteredObject registration(&provider);
        SemanticRuntimeExecutor executor(
            &projects, registry, nullptr, fixture.repository);

        QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 1);
        provider.publishResourceSnapshotRequestFinished(completedResult);
        QTRY_COMPARE(provider.runtimeResourceRefreshCalls, 2);
        for (int index = 0; index < 5; ++index)
            provider.publishResourceSnapshotRequestFinished(completedResult);
        QTest::qWait(10);
        QCOMPARE(provider.runtimeResourceRefreshCalls, 2);
        QVERIFY(provider.semanticMappingAttestationRequests.isEmpty());
        QCOMPARE(provider.mutationCalls, 0);
    }
}

void EtherCATSemanticRuntimeTests::testRuntimeBootstrapRequiresEvidenceAndUniqueProvider()
{
    RuntimeBootstrapFixture fixture;
    QVERIFY_RESULT(fixture.initialize());
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    {
        TestProjectService projects;
        Data::ProjectSnapshot project = fixture.project;
        project.masterBindingArtifact = {};
        projects.addProject(project);
        CountingControllerProvider provider(
            "EtherCAT.SemanticRuntime.Tests.RuntimeBootstrap.NoArtifact");
        provider.publishSnapshot(connectedSnapshot(projectScope(project), 31));
        provider.setAvailable(true);
        RegisteredObject registration(&provider);
        SemanticRuntimeExecutor executor(
            &projects, registry, nullptr, fixture.repository);
        QTest::qWait(10);
        QCOMPARE(provider.runtimeResourceRefreshCalls, 0);
        QVERIFY(provider.semanticMappingAttestationRequests.isEmpty());
    }

    {
        TestProjectService projects;
        projects.addProject(fixture.project);
        const Data::ControllerConnectionScope scope = projectScope(fixture.project);
        CountingControllerProvider first(
            "EtherCAT.SemanticRuntime.Tests.RuntimeBootstrap.Ambiguous.First");
        CountingControllerProvider second(
            "EtherCAT.SemanticRuntime.Tests.RuntimeBootstrap.Ambiguous.Second");
        first.publishSnapshot(connectedSnapshot(scope, 41));
        second.publishSnapshot(connectedSnapshot(scope, 42));
        first.setAvailable(true);
        second.setAvailable(true);
        RegisteredObject firstRegistration(&first);
        RegisteredObject secondRegistration(&second);
        SemanticRuntimeExecutor executor(
            &projects, registry, nullptr, fixture.repository);
        QTest::qWait(10);
        QCOMPARE(first.runtimeResourceRefreshCalls, 0);
        QCOMPARE(second.runtimeResourceRefreshCalls, 0);
        QVERIFY(first.semanticMappingAttestationRequests.isEmpty());
        QVERIFY(second.semanticMappingAttestationRequests.isEmpty());
    }
}

void EtherCATSemanticRuntimeTests::testPublishesOneProductionService()
{
    QList<Core::SemanticRuntimeService *> services;
    for (QObject *object : ExtensionSystem::PluginManager::allObjects()) {
        if (auto *service = qobject_cast<Core::SemanticRuntimeService *>(object))
            services.append(service);
    }

    QCOMPARE(services.size(), 1);
    QCOMPARE(
        ExtensionSystem::PluginManager::getObject<Core::SemanticRuntimeService>(),
        services.constFirst());
    QVERIFY(qobject_cast<SemanticRuntimeExecutor *>(services.constFirst()));
}

void EtherCATSemanticRuntimeTests::testProjectAndProviderLifecycle()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    SemanticRuntimeExecutor executor(&projects, registry);
    QSignalSpy contextsSpy(&executor, &Core::SemanticRuntimeService::contextsChanged);
    QVERIFY(contextsSpy.isValid());
    QVERIFY(executor.contexts().isEmpty());

    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    QCOMPARE(executor.contexts().size(), 1);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    projects.setAvailable(false);
    QVERIFY(executor.contexts().isEmpty());
    projects.setAvailable(true);
    QCOMPARE(executor.contexts().size(), 1);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Lifecycle");
    provider.publishSnapshot(connectedSnapshot(scope, 7));
    provider.publishCatalog(resourceCatalog(scope, 7));
    RegisteredObject registration(&provider);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    provider.setAvailable(true);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    registration.remove();
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    projects.removeProject(project.id);
    QVERIFY(executor.contexts().isEmpty());
    QVERIFY(contextsSpy.count() >= 6);
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testStrictProviderCardinality()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider first("EtherCAT.SemanticRuntime.Tests.Cardinality.First");
    first.publishSnapshot(connectedSnapshot(scope, 9));
    first.publishCatalog(resourceCatalog(scope, 9));
    first.setAvailable(true);
    RegisteredObject firstRegistration(&first);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    CountingControllerProvider second("EtherCAT.SemanticRuntime.Tests.Cardinality.Second");
    second.publishSnapshot(connectedSnapshot(scope, 10));
    second.publishCatalog(resourceCatalog(scope, 10));
    second.setAvailable(true);
    RegisteredObject secondRegistration(&second);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderAmbiguous));

    secondRegistration.remove();
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    RegisteredObject secondRegistrationAgain(&second);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderAmbiguous));

    second.setAvailable(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    first.setAvailable(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));
    QCOMPARE(first.mutationCalls, 0);
    QCOMPARE(second.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testScopeSessionAndEpochInvalidation()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Epoch");
    provider.publishSnapshot(connectedSnapshot(scope, 11));
    const Data::RuntimeResourceCatalogEpoch firstEpoch = catalogEpoch(5);
    provider.publishCatalog(resourceCatalog(scope, 11, firstEpoch));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    QCOMPARE(executor.contexts().constFirst().sessionGeneration, quint64(11));
    QCOMPARE(executor.contexts().constFirst().epoch, firstEpoch);

    const Data::ControllerConnectionScope otherScope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    provider.publishSnapshot(connectedSnapshot(otherScope, 12));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    provider.publishSnapshot(connectedSnapshot(scope, 12));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogStale));
    QCOMPARE(executor.contexts().constFirst().epoch, Data::RuntimeResourceCatalogEpoch());

    const Data::RuntimeResourceCatalogEpoch secondEpoch = catalogEpoch(6);
    provider.publishCatalog(resourceCatalog(scope, 12, secondEpoch));
    QCOMPARE(executor.contexts().constFirst().sessionGeneration, quint64(12));
    QCOMPARE(executor.contexts().constFirst().epoch, secondEpoch);

    Data::RuntimeResourceCatalogEpoch incompleteEpoch = secondEpoch;
    incompleteEpoch.catalogRevision = 0;
    provider.publishCatalog(resourceCatalog(scope, 12, incompleteEpoch));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceEpochIncomplete));
    QCOMPARE(executor.contexts().constFirst().epoch, Data::RuntimeResourceCatalogEpoch());
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testCandidateAdaptersNeverWrite()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject(true, true);
    const Data::ControllerConnectionScope scope = projectScope(project);
    QCOMPARE(project.slaves.size(), 2);
    QCOMPARE(
        project.slaves.at(0).adapterSelection.adapterId.value,
        QString("org.embedlabs.adapter.solidot.xb6-ec0002.rev1"));
    QCOMPARE(
        project.slaves.at(1).adapterSelection.adapterId.value,
        QString("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"));
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Candidates");
    provider.publishSnapshot(connectedSnapshot(scope, 14));
    provider.publishCatalog(resourceCatalog(scope, 14));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    const Data::SemanticRuntimeContext context = executor.contexts().constFirst();
    QVERIFY(!context.complete);
    QCOMPARE(context.detail, detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));
    QVERIFY(context.signalStates.isEmpty());
    QVERIFY(context.actionStates.isEmpty());

    Data::SemanticOperationRequest request;
    request.operationId.value = QStringLiteral("operation/candidate-adapters/1");
    request.target.controllerId = context.controllerId;
    request.target.scope = scope;
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("test-user");

    const Data::SemanticOperationRecord submitted = executor.submit(request, actor);
    QCOMPARE(submitted.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!submitted.executionAttempted);

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = request.operationId;
    const Data::SemanticOperationRecord approved = executor.approve(approval, actor);
    QCOMPARE(approved.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!approved.executionAttempted);
    QVERIFY(!executor.operation(request.operationId));
    QVERIFY(executor.audit(context.controllerId).isEmpty());
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testMissingCatalogAndProofFailClosed()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.FailClosed");
    provider.publishSnapshot(connectedSnapshot(scope, 16));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogUnavailable));

    Data::RuntimeResourceCatalog emptyCatalog = resourceCatalog(scope, 16);
    emptyCatalog.resources.clear();
    provider.publishCatalog(emptyCatalog);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogEmpty));

    provider.publishCatalog(resourceCatalog(scope, 16));
    const Data::SemanticRuntimeContext proofless = executor.contexts().constFirst();
    QVERIFY(!proofless.complete);
    QCOMPARE(proofless.bindingVerification.state, Data::SemanticBindingVerificationState::Unverified);
    QCOMPARE(
        proofless.detail, detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    project.masterBindingArtifact = {};
    projects.changeProject(project);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::BindingArtifactMissing));

    project.masterBindingArtifact = bindingArtifact();
    project.masterBindingArtifact.artifactSha256.chop(1);
    projects.changeProject(project);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::BindingArtifactInvalid));

    project.masterBindingArtifact = bindingArtifact();
    projects.changeProject(project);
    provider.setRuntimeResourcesSupported(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourcesUnsupported));
    QCOMPARE(provider.mutationCalls, 0);
}

} // namespace EtherCAT::SemanticRuntime::Internal
