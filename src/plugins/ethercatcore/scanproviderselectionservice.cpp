// Copyright (C) 2026 Kvell

#include "scanproviderselectionservice.h"

#include "ethercatcoretr.h"
#include "providerregistry.h"
#include "providers.h"

#include <utils/qtcassert.h>

#include <QThread>

#include <algorithm>
#include <utility>

namespace EtherCAT::Core {

namespace {

bool scopeIsValid(const Data::ControllerConnectionScope &scope)
{
    return !scope.projectId.isNull() && !scope.masterId.isNull();
}

bool scanIsActive(Data::ScanState state)
{
    switch (state) {
    case Data::ScanState::Preparing:
    case Data::ScanState::ScanningMaster:
    case Data::ScanState::ScanningSlaves:
    case Data::ScanState::BuildingSnapshot:
    case Data::ScanState::Comparing:
        return true;
    case Data::ScanState::Idle:
    case Data::ScanState::Completed:
    case Data::ScanState::Cancelled:
    case Data::ScanState::Failed:
        return false;
    }
    return false;
}

} // namespace

bool ScanProviderSelection::isValid() const
{
    return scopeIsValid(scope) && providerId.isValid();
}

ScanProviderSelectionService::ScanProviderSelectionService(
    ProviderRegistry *providerRegistry, QObject *parent)
    : QObject(parent)
    , m_providerRegistry(providerRegistry)
{
    QTC_ASSERT(m_providerRegistry, return);

    connect(
        m_providerRegistry,
        &ProviderRegistry::providerAdded,
        this,
        &ScanProviderSelectionService::attachProvider);
    connect(
        m_providerRegistry,
        &ProviderRegistry::providerAboutToBeRemoved,
        this,
        &ScanProviderSelectionService::handleProviderAboutToBeRemoved);

    for (Provider *provider : m_providerRegistry->providers())
        attachProvider(provider);
}

std::optional<ScanProviderSelection> ScanProviderSelectionService::selection(
    const Data::ControllerConnectionScope &scope) const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return std::nullopt);

    const auto found = std::find_if(
        m_selections.cbegin(),
        m_selections.cend(),
        [&scope](const ScanProviderSelection &candidate) { return candidate.scope == scope; });
    if (found == m_selections.cend())
        return std::nullopt;
    return *found;
}

QList<ScanProviderSelection> ScanProviderSelectionService::selections() const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return {});

    QList<ScanProviderSelection> result = m_selections;
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        if (left.scope.projectId != right.scope.projectId) {
            return left.scope.projectId.toString() < right.scope.projectId.toString();
        }
        return left.scope.masterId.toString() < right.scope.masterId.toString();
    });
    return result;
}

bool ScanProviderSelectionService::selectionIsAvailable(
    const Data::ControllerConnectionScope &scope) const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return false);

    const std::optional<ScanProviderSelection> selected = selection(scope);
    if (!selected)
        return false;
    ScanProvider *provider = registeredScanProvider(selected->providerId);
    return provider && provider->isAvailable();
}

Utils::Result<> ScanProviderSelectionService::select(
    const Data::ControllerConnectionScope &scope, Utils::Id providerId)
{
    QTC_ASSERT(
        QThread::currentThread() == thread(),
        return Utils::ResultError(Tr::tr("The scan provider selection is on another thread.")));

    if (!scopeIsValid(scope))
        return Utils::ResultError(Tr::tr("The scan provider selection scope is invalid."));
    if (!providerId.isValid())
        return Utils::ResultError(Tr::tr("The scan provider ID is invalid."));
    if (!scopeExists(scope)) {
        return Utils::ResultError(
            Tr::tr("The scan provider selection does not identify an open EtherCAT Master."));
    }

    const auto existing = std::find_if(
        m_selections.begin(),
        m_selections.end(),
        [&scope](const ScanProviderSelection &candidate) { return candidate.scope == scope; });
    ScanProvider *provider = registeredScanProvider(providerId);
    if (!provider) {
        return Utils::ResultError(
            Tr::tr("The requested Scan Provider is not currently registered."));
    }
    if (existing != m_selections.end() && existing->providerId == providerId)
        return Utils::ResultOk;

    if (existing != m_selections.end()) {
        if (ScanProvider *current = registeredScanProvider(existing->providerId);
            current && scanIsActive(current->scanState())) {
            return Utils::ResultError(
                Tr::tr("Cancel the active scan before changing its Scan Provider."));
        }
    }
    if (scanIsActive(provider->scanState())) {
        return Utils::ResultError(
            Tr::tr("The requested Scan Provider is already running a scan."));
    }

    if (existing != m_selections.end())
        existing->providerId = providerId;
    else
        m_selections.append({scope, providerId});

    emit selectionChanged(scope);
    emit selectionValidityChanged({scope, providerId}, provider->isAvailable());
    return Utils::ResultOk;
}

Utils::Result<> ScanProviderSelectionService::clear(
    const Data::ControllerConnectionScope &scope)
{
    QTC_ASSERT(
        QThread::currentThread() == thread(),
        return Utils::ResultError(Tr::tr("The scan provider selection is on another thread.")));

    if (!scopeIsValid(scope))
        return Utils::ResultError(Tr::tr("The scan provider selection scope is invalid."));

    const auto existing = std::find_if(
        m_selections.begin(),
        m_selections.end(),
        [&scope](const ScanProviderSelection &candidate) { return candidate.scope == scope; });
    if (existing == m_selections.end())
        return Utils::ResultOk;

    if (ScanProvider *current = registeredScanProvider(existing->providerId);
        current && scanIsActive(current->scanState())) {
        return Utils::ResultError(
            Tr::tr("Cancel the active scan before clearing its Scan Provider."));
    }

    m_selections.erase(existing);
    emit selectionChanged(scope);
    return Utils::ResultOk;
}

ScanProvider *ScanProviderSelectionService::registeredScanProvider(Utils::Id providerId) const
{
    if (!m_providerRegistry || !providerId.isValid())
        return nullptr;
    return qobject_cast<ScanProvider *>(m_providerRegistry->provider(providerId));
}

bool ScanProviderSelectionService::scopeExists(
    const Data::ControllerConnectionScope &scope) const
{
    if (!m_providerRegistry || !scopeIsValid(scope))
        return false;

    for (Provider *provider : m_providerRegistry->providers(ProviderKind::Project)) {
        auto *projectService = qobject_cast<ProjectService *>(provider);
        if (!projectService)
            continue;
        const std::optional<Data::ProjectSnapshot> project
            = projectService->project(scope.projectId);
        if (!project || project->id != scope.projectId)
            continue;
        if (std::any_of(
                project->nodes.cbegin(),
                project->nodes.cend(),
                [&scope](const Data::ProjectNodeSnapshot &node) {
                    return node.id == scope.masterId
                           && node.kind == Data::ProjectNodeKind::Master;
                })) {
            return true;
        }
    }
    return false;
}

void ScanProviderSelectionService::attachProvider(Provider *provider)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    if (auto *scanProvider = qobject_cast<ScanProvider *>(provider)) {
        connect(
            scanProvider,
            &Provider::availabilityChanged,
            this,
            [this, scanProvider](bool available) {
                notifyProviderValidity(scanProvider->id(), available);
            });
        notifyProviderValidity(scanProvider->id(), scanProvider->isAvailable());
        return;
    }

    if (auto *projectService = qobject_cast<ProjectService *>(provider)) {
        connect(
            projectService,
            &ProjectService::projectAboutToBeRemoved,
            this,
            &ScanProviderSelectionService::clearProjectSelections);
        connect(
            projectService,
            &ProjectService::projectChanged,
            this,
            &ScanProviderSelectionService::handleProjectChanged);
    }
}

void ScanProviderSelectionService::handleProviderAboutToBeRemoved(Provider *provider)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    if (!provider)
        return;

    disconnect(provider, nullptr, this, nullptr);
    if (auto *scanProvider = qobject_cast<ScanProvider *>(provider)) {
        notifyProviderValidity(scanProvider->id(), false);
        return;
    }

    if (auto *projectService = qobject_cast<ProjectService *>(provider)) {
        const QList<Data::ProjectSnapshot> projects = projectService->projects();
        for (const Data::ProjectSnapshot &project : projects)
            clearProjectSelections(project.id);
    }
}

void ScanProviderSelectionService::handleProjectChanged(
    const Data::ProjectSnapshot &project)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    if (project.id.isNull())
        return;

    QList<Data::ControllerConnectionScope> removedScopes;
    for (qsizetype index = m_selections.size() - 1; index >= 0; --index) {
        const Data::ControllerConnectionScope scope = m_selections.at(index).scope;
        if (scope.projectId != project.id || scopeExists(scope))
            continue;
        removedScopes.prepend(scope);
        m_selections.removeAt(index);
    }
    for (const Data::ControllerConnectionScope &scope : std::as_const(removedScopes))
        emit selectionChanged(scope);
}

void ScanProviderSelectionService::notifyProviderValidity(
    Utils::Id providerId, bool available)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    for (const ScanProviderSelection &selected : std::as_const(m_selections)) {
        if (selected.providerId == providerId)
            emit selectionValidityChanged(selected, available);
    }
}

void ScanProviderSelectionService::clearProjectSelections(const Data::NodeId &projectId)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    if (projectId.isNull())
        return;

    QList<Data::ControllerConnectionScope> removedScopes;
    for (qsizetype index = m_selections.size() - 1; index >= 0; --index) {
        if (m_selections.at(index).scope.projectId != projectId)
            continue;
        removedScopes.prepend(m_selections.at(index).scope);
        m_selections.removeAt(index);
    }
    for (const Data::ControllerConnectionScope &scope : std::as_const(removedScopes))
        emit selectionChanged(scope);
}

} // namespace EtherCAT::Core
