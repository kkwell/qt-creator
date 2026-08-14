// Copyright (C) 2026 Kvell

#include "topologyservice.h"

#include "providerregistry.h"
#include "providers.h"

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QThread>

namespace EtherCAT::Core {

namespace {

bool scopeIsValid(const Data::ControllerConnectionScope &scope)
{
    return !scope.projectId.isNull() && !scope.masterId.isNull();
}

TopologyEvidenceSource sourceForProvider(const Provider *provider)
{
    if (qobject_cast<const ControllerConnectionProvider *>(provider))
        return TopologyEvidenceSource::RealController;
    if (qobject_cast<const ScanProvider *>(provider))
        return TopologyEvidenceSource::MockScan;
    return TopologyEvidenceSource::None;
}

bool connectionCanOwnFreshTopology(Data::ControllerConnectionState state)
{
    return state == Data::ControllerConnectionState::Connected
           || state == Data::ControllerConnectionState::Degraded;
}

TopologyLookupResult controllerTopology(
    ControllerConnectionProvider *provider, const TopologySelection &selection)
{
    const Data::ControllerConnectionSnapshot connection = provider->connectionSnapshot();
    if (connection.mock)
        return {TopologyLookupStatus::EvidenceInvalid, std::nullopt};
    if (!connection.topology)
        return {TopologyLookupStatus::EvidenceUnavailable, std::nullopt};

    const Data::ControllerTopologySnapshot &evidence = *connection.topology;
    if (connection.scope != selection.scope || evidence.scope != selection.scope)
        return {TopologyLookupStatus::ScopeMismatch, std::nullopt};

    TopologySnapshot snapshot;
    snapshot.selection = selection;
    snapshot.controllerEvidence = evidence;

    if (!evidence.hasCompleteProvenance()) {
        snapshot.freshness = TopologyEvidenceFreshness::Incomplete;
    } else if (!provider->isAvailable() || !connectionCanOwnFreshTopology(connection.state)
               || !connection.session || connection.sessionGeneration != evidence.sessionGeneration
               || connection.session->sessionId != evidence.sessionId
               || connection.session->bootId != evidence.bootId) {
        snapshot.freshness = TopologyEvidenceFreshness::Stale;
    } else {
        snapshot.freshness = TopologyEvidenceFreshness::Fresh;
    }

    if (!snapshot.isValid())
        return {TopologyLookupStatus::EvidenceInvalid, std::nullopt};
    return {TopologyLookupStatus::Success, snapshot};
}

TopologyLookupResult mockTopology(ScanProvider *provider, const TopologySelection &selection)
{
    const std::optional<Data::ScanResult> result = provider->lastScanResult();
    if (!result)
        return {TopologyLookupStatus::EvidenceUnavailable, std::nullopt};

    const Data::ScanSnapshot &evidence = result->snapshot;
    if (evidence.projectId != selection.scope.projectId
        || evidence.masterId != selection.scope.masterId
        || result->comparison.projectId != selection.scope.projectId
        || result->comparison.masterId != selection.scope.masterId) {
        return {TopologyLookupStatus::ScopeMismatch, std::nullopt};
    }

    TopologySnapshot snapshot;
    snapshot.selection = selection;
    snapshot.mockEvidence = *result;

    if (!evidence.mock)
        return {TopologyLookupStatus::EvidenceInvalid, std::nullopt};
    if (!evidence.complete || !evidence.capturedAt.isValid() || evidence.id.isNull()) {
        snapshot.freshness = TopologyEvidenceFreshness::Incomplete;
    } else if (!provider->isAvailable() || provider->scanState() != Data::ScanState::Completed) {
        snapshot.freshness = TopologyEvidenceFreshness::Stale;
    } else {
        snapshot.freshness = TopologyEvidenceFreshness::Fresh;
    }

    if (!snapshot.isValid())
        return {TopologyLookupStatus::EvidenceInvalid, std::nullopt};
    return {TopologyLookupStatus::Success, snapshot};
}

} // namespace

bool RealTopologyGeneration::isValid() const
{
    return sessionGeneration && sessionId && bootId && requestId && responseSequence
           && (cpu1RequestSequence || topologyCaptureSequence);
}

bool MockTopologyGeneration::isValid() const
{
    return !snapshotId.isNull();
}

TopologyEvidenceSource TopologyGeneration::source() const
{
    if (std::holds_alternative<RealTopologyGeneration>(value))
        return TopologyEvidenceSource::RealController;
    if (std::holds_alternative<MockTopologyGeneration>(value))
        return TopologyEvidenceSource::MockScan;
    return TopologyEvidenceSource::None;
}

bool TopologyGeneration::isValid() const
{
    if (const auto *real = std::get_if<RealTopologyGeneration>(&value))
        return real->isValid();
    if (const auto *mock = std::get_if<MockTopologyGeneration>(&value))
        return mock->isValid();
    return false;
}

bool TopologySelection::isValid() const
{
    return source != TopologyEvidenceSource::None && providerId.isValid() && scopeIsValid(scope);
}

TopologyGeneration TopologySnapshot::generation() const
{
    if (selection.source == TopologyEvidenceSource::RealController && controllerEvidence) {
        const Data::ControllerTopologySnapshot &evidence = *controllerEvidence;
        return {{RealTopologyGeneration{
            evidence.sessionGeneration,
            evidence.sessionId,
            evidence.bootId,
            evidence.requestId,
            evidence.responseSequence,
            evidence.cpu1RequestSequence,
            evidence.topologyCaptureSequence,
        }}};
    }
    if (selection.source == TopologyEvidenceSource::MockScan && mockEvidence)
        return {{MockTopologyGeneration{mockEvidence->snapshot.id}}};
    return {};
}

bool TopologySnapshot::isValid() const
{
    if (!selection.isValid() || controllerEvidence.has_value() == mockEvidence.has_value())
        return false;
    if (freshness != TopologyEvidenceFreshness::Incomplete && !generation().isValid())
        return false;

    if (selection.source == TopologyEvidenceSource::RealController) {
        return controllerEvidence && controllerEvidence->scope == selection.scope;
    }
    if (selection.source == TopologyEvidenceSource::MockScan) {
        return mockEvidence && mockEvidence->snapshot.mock
               && mockEvidence->snapshot.projectId == selection.scope.projectId
               && mockEvidence->snapshot.masterId == selection.scope.masterId
               && mockEvidence->comparison.projectId == selection.scope.projectId
               && mockEvidence->comparison.masterId == selection.scope.masterId;
    }
    return false;
}

bool TopologyLookupResult::isSuccess() const
{
    return status == TopologyLookupStatus::Success && snapshot && snapshot->isValid();
}

bool TopologyLookupResult::hasFreshProviderEvidence() const
{
    return isSuccess() && snapshot->freshness == TopologyEvidenceFreshness::Fresh
           && snapshot->generation().isValid();
}

TopologyService::TopologyService(ProviderRegistry *providerRegistry, QObject *parent)
    : QObject(parent)
    , m_providerRegistry(providerRegistry)
{
    QTC_ASSERT(m_providerRegistry, return);

    connect(
        m_providerRegistry,
        &ProviderRegistry::providerAdded,
        this,
        &TopologyService::attachProvider);
    connect(
        m_providerRegistry,
        &ProviderRegistry::providerAboutToBeRemoved,
        this,
        &TopologyService::handleProviderAboutToBeRemoved);

    for (Provider *provider : m_providerRegistry->providers())
        attachProvider(provider);
}

QList<TopologySelection> TopologyService::availableSelections() const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return {});
    QTC_ASSERT(m_providerRegistry, return {});

    QList<TopologySelection> result;
    for (Provider *provider : m_providerRegistry->providers()) {
        if (auto *connectionProvider = qobject_cast<ControllerConnectionProvider *>(provider)) {
            const Data::ControllerConnectionSnapshot connection
                = connectionProvider->connectionSnapshot();
            if (connection.topology && scopeIsValid(connection.topology->scope)) {
                const TopologySelection selection{
                    TopologyEvidenceSource::RealController,
                    provider->id(),
                    connection.topology->scope,
                };
                if (topology(selection).isSuccess())
                    result.append(selection);
            }
            continue;
        }

        if (auto *scanProvider = qobject_cast<ScanProvider *>(provider)) {
            const std::optional<Data::ScanResult> scan = scanProvider->lastScanResult();
            if (scan) {
                const Data::ControllerConnectionScope scope{
                    scan->snapshot.projectId,
                    scan->snapshot.masterId,
                };
                const TopologySelection selection{
                    TopologyEvidenceSource::MockScan,
                    provider->id(),
                    scope,
                };
                if (selection.isValid() && topology(selection).isSuccess())
                    result.append(selection);
            }
        }
    }

    Utils::sort(result, [](const TopologySelection &left, const TopologySelection &right) {
        if (left.source != right.source)
            return left.source < right.source;
        if (left.providerId != right.providerId)
            return left.providerId.toString() < right.providerId.toString();
        if (left.scope.projectId != right.scope.projectId)
            return left.scope.projectId.toString() < right.scope.projectId.toString();
        return left.scope.masterId.toString() < right.scope.masterId.toString();
    });
    return result;
}

TopologyLookupResult TopologyService::topology(const TopologySelection &selection) const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return {});
    QTC_ASSERT(m_providerRegistry, return {});

    if (!selection.isValid())
        return {TopologyLookupStatus::InvalidSelection, std::nullopt};

    Provider *provider = m_providerRegistry->provider(selection.providerId);
    if (!provider)
        return {TopologyLookupStatus::ProviderNotFound, std::nullopt};

    if (selection.source == TopologyEvidenceSource::RealController) {
        auto *connectionProvider = qobject_cast<ControllerConnectionProvider *>(provider);
        if (!connectionProvider)
            return {TopologyLookupStatus::ProviderKindMismatch, std::nullopt};
        return controllerTopology(connectionProvider, selection);
    }

    if (selection.source == TopologyEvidenceSource::MockScan) {
        auto *scanProvider = qobject_cast<ScanProvider *>(provider);
        if (!scanProvider)
            return {TopologyLookupStatus::ProviderKindMismatch, std::nullopt};
        return mockTopology(scanProvider, selection);
    }

    return {TopologyLookupStatus::InvalidSelection, std::nullopt};
}

void TopologyService::attachProvider(Provider *provider)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);
    if (!provider || sourceForProvider(provider) == TopologyEvidenceSource::None)
        return;

    connect(
        provider,
        &Provider::availabilityChanged,
        this,
        [this, provider] { notifyProviderChanged(provider); });

    if (auto *connectionProvider = qobject_cast<ControllerConnectionProvider *>(provider)) {
        connect(
            connectionProvider,
            &ControllerConnectionProvider::connectionSnapshotChanged,
            this,
            [this, provider] { notifyProviderChanged(provider); });
    } else if (auto *scanProvider = qobject_cast<ScanProvider *>(provider)) {
        connect(
            scanProvider,
            &ScanProvider::scanResultChanged,
            this,
            [this, provider] { notifyProviderChanged(provider); });
        connect(
            scanProvider,
            &ScanProvider::scanStateChanged,
            this,
            [this, provider] { notifyProviderChanged(provider); });
    }

    notifyProviderChanged(provider);
}

void TopologyService::handleProviderAboutToBeRemoved(Provider *provider)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);
    if (!provider)
        return;

    const TopologyEvidenceSource source = sourceForProvider(provider);
    if (source == TopologyEvidenceSource::None)
        return;

    disconnect(provider, nullptr, this, nullptr);
    emit topologyChanged(source, provider->id());
}

void TopologyService::notifyProviderChanged(Provider *provider)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);
    if (!provider)
        return;
    const TopologyEvidenceSource source = sourceForProvider(provider);
    if (source != TopologyEvidenceSource::None)
        emit topologyChanged(source, provider->id());
}

} // namespace EtherCAT::Core
