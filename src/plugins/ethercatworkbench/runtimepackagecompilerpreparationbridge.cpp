// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilerpreparationbridge.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/providers.h>
#include <ethercatcore/runtimepackagecompilerpreparationcoordinator.h>
#include <ethercatcore/runtimepackagecompilerprovider.h>

#include <algorithm>
#include <utility>

namespace EtherCAT::Workbench::Internal {

bool RuntimePackageCompilerPreparationBridge::PreparationOperationIdentity::isValid() const
{
    return compileOperationId.isValid() && verifyOperationId.isValid()
           && activationOperationId.isValid()
           && compileOperationId.value() != verifyOperationId.value()
           && compileOperationId.value() != activationOperationId.value()
           && verifyOperationId.value() != activationOperationId.value();
}

RuntimePackageCompilerPreparationBridge::RuntimePackageCompilerPreparationBridge(
    WorkbenchController *controller,
    Core::RuntimePackageCompilerPreparationCoordinator *coordinator,
    QObject *parent)
    : RuntimePackageCompilerPreparationBridge(controller, coordinator, {}, parent)
{}

RuntimePackageCompilerPreparationBridge::RuntimePackageCompilerPreparationBridge(
    WorkbenchController *controller,
    Core::RuntimePackageCompilerPreparationCoordinator *coordinator,
    CurrentProjectCaptureProvider currentProjectCaptureProvider,
    QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_coordinator(coordinator)
    , m_providerRegistry(controller ? controller->providerRegistry() : nullptr)
    , m_currentProjectCaptureProvider(std::move(currentProjectCaptureProvider))
{
    if (!m_controller || !m_coordinator || !m_providerRegistry)
        return;

    m_connections.append(connect(
        m_coordinator,
        &Core::RuntimePackageCompilerPreparationCoordinator::recordChanged,
        this,
        &RuntimePackageCompilerPreparationBridge::handleRecordChanged));
    m_connections.append(connect(
        m_coordinator,
        &Core::RuntimePackageCompilerPreparationCoordinator::preparationReady,
        this,
        &RuntimePackageCompilerPreparationBridge::handlePreparationReady));
    if (Core::ProjectService *projectService = m_controller->projectService()) {
        m_connections.append(connect(
            projectService,
            &Core::ProjectService::projectChanged,
            this,
            &RuntimePackageCompilerPreparationBridge::handleProjectChanged));
        m_connections.append(connect(
            projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            &RuntimePackageCompilerPreparationBridge::handleProjectAboutToBeRemoved));
    }
    m_connections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAdded,
        this,
        [this](Core::Provider *provider) {
            trackCompilerProvider(provider);
            handleCompilerProviderSetChanged();
        }));
    m_connections.append(connect(
        m_providerRegistry,
        &Core::ProviderRegistry::providerAboutToBeRemoved,
        this,
        [this](Core::Provider *provider) {
            if (!provider || provider->kind() != Core::ProviderKind::RuntimePackageCompiler)
                return;
            m_trackedCompilerProviders.remove(provider);
            handleCompilerProviderSetChanged();
        }));
    for (Core::Provider *provider :
         m_providerRegistry->providers(Core::ProviderKind::RuntimePackageCompiler)) {
        trackCompilerProvider(provider);
    }
    m_connections.append(connect(m_coordinator, &QObject::destroyed, this, [this] {
        m_coordinator = nullptr;
        clearPreparation();
        setCoordinatorAvailable(false);
    }));
    setCoordinatorAvailable(true);
}

RuntimePackageCompilerPreparationBridge::~RuntimePackageCompilerPreparationBridge()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    clearPreparation();
    setCoordinatorAvailable(false);
}

void RuntimePackageCompilerPreparationBridge::handleRecordChanged(
    const Core::RuntimePackageCompilerPreparationRecord &record)
{
    if (!m_controller)
        return;

    const quint64 recordSequence = ++m_admissionSequence;
    if (!record.isValid()) {
        clearPreparation();
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("Project package preparation update rejected."),
                ControllerOutputLevel::Error);
        }
        return;
    }

    const PreparationOperationIdentity identity = operationIdentity(record);
    if (!identity.isValid()) {
        clearPreparation();
        return;
    }
    const bool supersedesPending = m_pendingReadyPreparation
                                   && identity != m_pendingReadyPreparation->operationIdentity;
    const bool supersedesTrusted = m_trustedOperationIdentity
                                   && identity != *m_trustedOperationIdentity;
    if (supersedesPending)
        invalidatePendingPreparation();
    if (supersedesTrusted) {
        invalidateTrustedPreparation();
        clearTrustedPreparation();
    }

    using Phase = Core::RuntimePackageCompilerPreparationPhase;
    switch (record.phase) {
    case Phase::AwaitingDetachedSignature:
        invalidatePendingPreparation();
        if (m_trustedOperationIdentity && identity == *m_trustedOperationIdentity) {
            invalidateTrustedPreparation();
            clearTrustedPreparation();
        }
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("External package signature required [%1].")
                    .arg(record.compileOperationId.value()));
        }
        break;
    case Phase::ReconciliationRequired:
        clearPreparation();
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("Project package preparation needs review [%1]: %2")
                    .arg(record.compileOperationId.value(), record.detail.left(512)),
                ControllerOutputLevel::Error);
        }
        break;
    case Phase::Canceled:
        clearPreparation();
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("Project package preparation canceled [%1].")
                    .arg(record.compileOperationId.value()),
                ControllerOutputLevel::Warning);
        }
        break;
    case Phase::Failed:
        clearPreparation();
        if (m_controller) {
            m_controller->writeControllerOutput(
                record.detail.isEmpty()
                    ? Tr::tr("Project package preparation failed [%1].")
                          .arg(record.compileOperationId.value())
                    : Tr::tr("Project package preparation failed [%1]: %2")
                          .arg(record.compileOperationId.value(), record.detail.left(512)),
                ControllerOutputLevel::Error);
        }
        break;
    case Phase::Ready: {
        const QString identityKey = operationIdentityKey(identity);
        if (!record.preparation || m_invalidatedOperationIdentities.contains(identityKey)) {
            clearPreparation();
            if (m_controller) {
                m_controller->writeControllerOutput(
                    Tr::tr(
                        "Verified project package rejected: the preparation is no longer current."),
                    ControllerOutputLevel::Error);
            }
            break;
        }
        const Utils::Result<> current = validateAgainstCurrentProject(*record.preparation);
        if (recordSequence != m_admissionSequence || !m_controller)
            break;
        if (!current) {
            m_invalidatedOperationIdentities.insert(identityKey);
            clearPreparation();
            if (m_controller) {
                m_controller->writeControllerOutput(
                    Tr::tr("Verified project package rejected: %1").arg(current.error().left(512)),
                    ControllerOutputLevel::Error);
            }
            break;
        }
        const Data::NodeId projectId = record.preparation->scope.projectId;
        m_pendingReadyPreparation = PendingReadyPreparation{
            *record.preparation, identity, m_projectGenerations.value(projectId)};
        break;
    }
    case Phase::Idle:
    case Phase::Reserved:
    case Phase::Compiling:
    case Phase::Finalizing:
    case Phase::Verifying:
    case Phase::AssemblingProof:
    case Phase::CancelRequested:
        invalidatePendingPreparation();
        if (m_trustedOperationIdentity && identity == *m_trustedOperationIdentity) {
            invalidateTrustedPreparation();
            clearTrustedPreparation();
        }
        break;
    }
}

void RuntimePackageCompilerPreparationBridge::handlePreparationReady(
    const Core::RuntimePackageActivationPreparationRequest &preparation)
{
    if (!m_controller)
        return;

    const quint64 admissionSequence = ++m_admissionSequence;
    const PreparationOperationIdentity identity = operationIdentity(preparation);
    if (!identity.isValid() || !m_pendingReadyPreparation
        || m_pendingReadyPreparation->preparation != preparation
        || m_pendingReadyPreparation->operationIdentity != identity
        || m_pendingReadyPreparation->projectGeneration
               != m_projectGenerations.value(preparation.scope.projectId)
        || m_invalidatedOperationIdentities.contains(operationIdentityKey(identity))) {
        invalidatePendingPreparation();
        invalidateTrustedPreparation();
        clearTrustedPreparation();
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("Verified project package rejected: no matching current preparation record."),
                ControllerOutputLevel::Error);
        }
        return;
    }

    const PendingReadyPreparation admissionCandidate = *m_pendingReadyPreparation;
    const quint64 compilerProviderGeneration = m_compilerProviderGeneration;
    const auto admissionIsCurrent = [&] {
        return admissionSequence == m_admissionSequence && m_controller && m_pendingReadyPreparation
               && *m_pendingReadyPreparation == admissionCandidate
               && m_compilerProviderGeneration == compilerProviderGeneration
               && admissionCandidate.projectGeneration
                      == m_projectGenerations.value(preparation.scope.projectId)
               && !m_invalidatedOperationIdentities.contains(operationIdentityKey(identity));
    };
    const auto discardAdmission = [&] {
        if (m_pendingReadyPreparation && *m_pendingReadyPreparation == admissionCandidate) {
            invalidatePendingPreparation();
        }
        WorkbenchController *controller = m_controller;
        if (controller && controller->m_runtimePackageActivationPreparation == preparation) {
            controller->clearTrustedRuntimePackageActivationPreparation();
        }
    };

    clearTrustedPreparation();
    if (!admissionIsCurrent()) {
        discardAdmission();
        return;
    }

    const Utils::Result<> current = validateAgainstCurrentProject(preparation);
    if (!admissionIsCurrent()) {
        discardAdmission();
        return;
    }
    if (!current) {
        m_invalidatedOperationIdentities.insert(operationIdentityKey(identity));
        discardAdmission();
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("Verified project package rejected: %1").arg(current.error().left(512)),
                ControllerOutputLevel::Error);
        }
        return;
    }

    WorkbenchController *controller = m_controller;
    const Utils::Result<> accepted = controller->setTrustedRuntimePackageActivationPreparation(
        preparation);
    if (!admissionIsCurrent()) {
        discardAdmission();
        return;
    }
    if (!accepted) {
        discardAdmission();
        m_controller->writeControllerOutput(
            Tr::tr("Verified project package rejected: %1").arg(accepted.error().left(512)),
            ControllerOutputLevel::Error);
        return;
    }

    const Utils::Result<> finalValidation = validateAgainstCurrentProject(preparation);
    if (!admissionIsCurrent() || !m_controller->m_runtimePackageActivationPreparation
        || *m_controller->m_runtimePackageActivationPreparation != preparation) {
        discardAdmission();
        return;
    }
    if (!finalValidation) {
        m_invalidatedOperationIdentities.insert(operationIdentityKey(identity));
        discardAdmission();
        if (m_controller) {
            m_controller->writeControllerOutput(
                Tr::tr("Verified project package rejected: %1")
                    .arg(finalValidation.error().left(512)),
                ControllerOutputLevel::Error);
        }
        return;
    }

    m_trustedPreparation = preparation;
    m_trustedOperationIdentity = identity;
    m_trustedScope = preparation.scope;
    m_pendingReadyPreparation.reset();
    m_controller->writeControllerOutput(
        Tr::tr("Project package verified and ready [%1].").arg(preparation.operationId.value()));
}

Utils::Result<Data::RuntimePackageActivationProjectCapture>
RuntimePackageCompilerPreparationBridge::currentProjectCapture(const Data::NodeId &projectId) const
{
    if (m_currentProjectCaptureProvider)
        return m_currentProjectCaptureProvider(projectId);
    if (!m_controller || !m_controller->projectService())
        return Utils::ResultError(Tr::tr("The project service is unavailable."));
    return m_controller->projectService()->captureRuntimePackageActivationProject(projectId);
}

Utils::Result<> RuntimePackageCompilerPreparationBridge::validateAgainstCurrentProject(
    const Core::RuntimePackageActivationPreparationRequest &preparation) const
{
    if (!m_providerRegistry)
        return Utils::ResultError(Tr::tr("The compiler provider registry is unavailable."));
    const Utils::Result<Data::RuntimePackageActivationProjectCapture> capture
        = currentProjectCapture(preparation.scope.projectId);
    if (!capture)
        return Utils::ResultError(capture.error());

    const Utils::Result<> validation = Core::validateRuntimePackageCompilerActivationProof(
        m_providerRegistry, preparation, *capture);
    if (!validation)
        return Utils::ResultError(validation.error());

    const Data::ProjectSnapshot &snapshot = capture->snapshot();
    const bool containsMaster = std::any_of(
        snapshot.nodes.cbegin(), snapshot.nodes.cend(), [&](const Data::ProjectNodeSnapshot &node) {
            return node.id == preparation.scope.masterId
                   && node.kind == Data::ProjectNodeKind::Master;
        });
    if (!containsMaster)
        return Utils::ResultError(Tr::tr("The compiled EtherCAT Master is no longer open."));
    return Utils::ResultOk;
}

void RuntimePackageCompilerPreparationBridge::handleProjectChanged(
    const Data::ProjectSnapshot &project)
{
    invalidateProject(project.id);
}

void RuntimePackageCompilerPreparationBridge::handleProjectAboutToBeRemoved(
    const Data::NodeId &projectId)
{
    invalidateProject(projectId);
}

void RuntimePackageCompilerPreparationBridge::invalidateProject(const Data::NodeId &projectId)
{
    ++m_admissionSequence;
    ++m_projectGenerations[projectId];
    if (m_pendingReadyPreparation
        && m_pendingReadyPreparation->preparation.scope.projectId == projectId) {
        invalidatePendingPreparation();
    }
    if (m_trustedScope && m_trustedScope->projectId == projectId) {
        invalidateTrustedPreparation();
        clearTrustedPreparation();
    }
}

void RuntimePackageCompilerPreparationBridge::invalidatePendingPreparation()
{
    if (!m_pendingReadyPreparation)
        return;
    m_invalidatedOperationIdentities.insert(
        operationIdentityKey(m_pendingReadyPreparation->operationIdentity));
    m_pendingReadyPreparation.reset();
}

void RuntimePackageCompilerPreparationBridge::invalidateTrustedPreparation()
{
    if (m_trustedOperationIdentity) {
        m_invalidatedOperationIdentities.insert(operationIdentityKey(*m_trustedOperationIdentity));
    }
}

void RuntimePackageCompilerPreparationBridge::clearTrustedPreparation()
{
    m_trustedPreparation.reset();
    m_trustedOperationIdentity.reset();
    m_trustedScope.reset();
    WorkbenchController *controller = m_controller;
    if (controller)
        controller->clearTrustedRuntimePackageActivationPreparation();
}

void RuntimePackageCompilerPreparationBridge::clearPreparation()
{
    ++m_admissionSequence;
    invalidatePendingPreparation();
    invalidateTrustedPreparation();
    clearTrustedPreparation();
}

void RuntimePackageCompilerPreparationBridge::trackCompilerProvider(Core::Provider *provider)
{
    if (!provider || provider->kind() != Core::ProviderKind::RuntimePackageCompiler
        || m_trackedCompilerProviders.contains(provider)) {
        return;
    }
    m_trackedCompilerProviders.insert(provider);
    m_connections.append(connect(provider, &Core::Provider::availabilityChanged, this, [this] {
        handleCompilerProviderSetChanged();
    }));
    m_connections.append(connect(provider, &QObject::destroyed, this, [this, provider] {
        m_trackedCompilerProviders.remove(provider);
        handleCompilerProviderSetChanged();
    }));
}

void RuntimePackageCompilerPreparationBridge::handleCompilerProviderSetChanged()
{
    ++m_compilerProviderGeneration;
    const std::optional<Core::RuntimePackageActivationPreparationRequest> preparation
        = m_pendingReadyPreparation ? std::optional(m_pendingReadyPreparation->preparation)
                                    : m_trustedPreparation;
    if (!preparation)
        return;

    const quint64 providerSequence = ++m_admissionSequence;
    const Utils::Result<> current = validateAgainstCurrentProject(*preparation);
    if (providerSequence != m_admissionSequence || !m_controller)
        return;
    if (current)
        return;

    clearPreparation();
    if (m_controller) {
        m_controller->writeControllerOutput(
            Tr::tr("Verified project package rejected: %1").arg(current.error().left(512)),
            ControllerOutputLevel::Error);
    }
}

void RuntimePackageCompilerPreparationBridge::setCoordinatorAvailable(bool available)
{
    if (m_coordinatorAvailable == available)
        return;
    m_coordinatorAvailable = available;
    WorkbenchController *controller = m_controller;
    if (controller)
        controller->setRuntimePackageCompilerPreparationCoordinatorAvailable(available);
}

RuntimePackageCompilerPreparationBridge::PreparationOperationIdentity
RuntimePackageCompilerPreparationBridge::operationIdentity(
    const Core::RuntimePackageCompilerPreparationRecord &record)
{
    return {record.compileOperationId, record.verifyOperationId, record.activationOperationId};
}

RuntimePackageCompilerPreparationBridge::PreparationOperationIdentity
RuntimePackageCompilerPreparationBridge::operationIdentity(
    const Core::RuntimePackageActivationPreparationRequest &preparation)
{
    if (!preparation.compilerActivationProof)
        return {};
    return {
        preparation.compilerActivationProof->compileRequest.operationId,
        preparation.compilerActivationProof->verifyRequest.operationId,
        preparation.operationId,
    };
}

QString RuntimePackageCompilerPreparationBridge::operationIdentityKey(
    const PreparationOperationIdentity &identity)
{
    return identity.compileOperationId.value() + QChar(u'\0') + identity.verifyOperationId.value()
           + QChar(u'\0') + identity.activationOperationId.value();
}

} // namespace EtherCAT::Workbench::Internal
