// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/runtimepackageactivationservice.h>

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/result.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <functional>
#include <optional>

namespace EtherCAT::Core {
class Provider;
class ProviderRegistry;
class RuntimePackageCompilerPreparationCoordinator;
struct RuntimePackageCompilerPreparationRecord;
struct RuntimePackageActivationPreparationRequest;
} // namespace EtherCAT::Core

namespace EtherCAT::Workbench::Internal {

class EtherCATWorkbenchTests;
class WorkbenchController;

// Admits only the durable, provider-verified preparation published by the
// compiler coordinator. This bridge never starts activation or accesses a
// controller; the user remains the only initiator of deployment.
class RuntimePackageCompilerPreparationBridge final : public QObject
{
public:
    RuntimePackageCompilerPreparationBridge(
        WorkbenchController *controller,
        Core::RuntimePackageCompilerPreparationCoordinator *coordinator,
        QObject *parent = nullptr);
    ~RuntimePackageCompilerPreparationBridge() final;

private:
    friend class EtherCATWorkbenchTests;

    using CurrentProjectCaptureProvider = std::function<
        Utils::Result<Data::RuntimePackageActivationProjectCapture>(const Data::NodeId &projectId)>;

    RuntimePackageCompilerPreparationBridge(
        WorkbenchController *controller,
        Core::RuntimePackageCompilerPreparationCoordinator *coordinator,
        CurrentProjectCaptureProvider currentProjectCaptureProvider,
        QObject *parent = nullptr);
    struct PreparationOperationIdentity
    {
        Data::RuntimePackageCompilerOperationId compileOperationId;
        Data::RuntimePackageCompilerOperationId verifyOperationId;
        Data::RuntimePackageActivationOperationId activationOperationId;

        bool isValid() const;

        friend bool operator==(
            const PreparationOperationIdentity &, const PreparationOperationIdentity &) = default;
    };

    struct PendingReadyPreparation
    {
        Core::RuntimePackageActivationPreparationRequest preparation;
        PreparationOperationIdentity operationIdentity;
        quint64 projectGeneration = 0;

        friend bool operator==(const PendingReadyPreparation &, const PendingReadyPreparation &)
            = default;
    };

    void handleRecordChanged(const Core::RuntimePackageCompilerPreparationRecord &record);
    void handlePreparationReady(const Core::RuntimePackageActivationPreparationRequest &preparation);
    Utils::Result<Data::RuntimePackageActivationProjectCapture> currentProjectCapture(
        const Data::NodeId &projectId) const;
    Utils::Result<> validateAgainstCurrentProject(
        const Core::RuntimePackageActivationPreparationRequest &preparation) const;
    void handleProjectChanged(const Data::ProjectSnapshot &project);
    void handleProjectAboutToBeRemoved(const Data::NodeId &projectId);
    void invalidateProject(const Data::NodeId &projectId);
    void invalidatePendingPreparation();
    void invalidateTrustedPreparation();
    void clearTrustedPreparation();
    void clearPreparation();
    void trackCompilerProvider(Core::Provider *provider);
    void handleCompilerProviderSetChanged();
    void setCoordinatorAvailable(bool available);
    static PreparationOperationIdentity operationIdentity(
        const Core::RuntimePackageCompilerPreparationRecord &record);
    static PreparationOperationIdentity operationIdentity(
        const Core::RuntimePackageActivationPreparationRequest &preparation);
    static QString operationIdentityKey(const PreparationOperationIdentity &identity);

    QPointer<WorkbenchController> m_controller;
    QPointer<Core::RuntimePackageCompilerPreparationCoordinator> m_coordinator;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    CurrentProjectCaptureProvider m_currentProjectCaptureProvider;
    std::optional<PendingReadyPreparation> m_pendingReadyPreparation;
    std::optional<Core::RuntimePackageActivationPreparationRequest> m_trustedPreparation;
    std::optional<PreparationOperationIdentity> m_trustedOperationIdentity;
    std::optional<Data::ControllerConnectionScope> m_trustedScope;
    QHash<Data::NodeId, quint64> m_projectGenerations;
    QSet<QString> m_invalidatedOperationIdentities;
    QSet<Core::Provider *> m_trackedCompilerProviders;
    QList<QMetaObject::Connection> m_connections;
    quint64 m_admissionSequence = 0;
    quint64 m_compilerProviderGeneration = 0;
    bool m_coordinatorAvailable = false;
};

} // namespace EtherCAT::Workbench::Internal
