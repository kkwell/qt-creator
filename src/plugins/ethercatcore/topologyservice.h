// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/scansnapshot.h>

#include <utils/id.h>

#include <QList>
#include <QObject>

#include <optional>
#include <variant>

namespace EtherCAT::Core {

class Provider;
class ProviderRegistry;

enum class TopologyEvidenceSource {
    None,
    RealController,
    MockScan,
};

enum class TopologyEvidenceFreshness {
    Fresh,
    Stale,
    Incomplete,
};

enum class TopologyLookupStatus {
    Success,
    InvalidSelection,
    ProviderNotFound,
    ProviderKindMismatch,
    EvidenceUnavailable,
    ScopeMismatch,
    EvidenceInvalid,
};

struct ETHERCATCORE_EXPORT RealTopologyGeneration
{
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 requestId = 0;
    quint64 responseSequence = 0;
    quint32 cpu1RequestSequence = 0;
    quint32 topologyCaptureSequence = 0;

    bool isValid() const;

    friend bool operator==(
        const RealTopologyGeneration &, const RealTopologyGeneration &) = default;
};

struct ETHERCATCORE_EXPORT MockTopologyGeneration
{
    Data::NodeId snapshotId;

    bool isValid() const;

    friend bool operator==(
        const MockTopologyGeneration &, const MockTopologyGeneration &) = default;
};

struct ETHERCATCORE_EXPORT TopologyGeneration
{
    using Value = std::variant<std::monostate, RealTopologyGeneration, MockTopologyGeneration>;

    Value value;

    TopologyEvidenceSource source() const;
    bool isValid() const;

    friend bool operator==(const TopologyGeneration &, const TopologyGeneration &) = default;
};

struct ETHERCATCORE_EXPORT TopologySelection
{
    TopologyEvidenceSource source = TopologyEvidenceSource::None;
    Utils::Id providerId;
    Data::ControllerConnectionScope scope;

    bool isValid() const;

    friend bool operator==(const TopologySelection &, const TopologySelection &) = default;
};

struct ETHERCATCORE_EXPORT TopologySnapshot
{
    TopologySelection selection;
    TopologyEvidenceFreshness freshness = TopologyEvidenceFreshness::Incomplete;
    std::optional<Data::ControllerTopologySnapshot> controllerEvidence;
    std::optional<Data::ScanResult> mockEvidence;

    // The generation is derived from the selected evidence on every call. It is
    // never a service-owned counter or a second copy of topology state.
    TopologyGeneration generation() const;
    bool isValid() const;

    friend bool operator==(const TopologySnapshot &, const TopologySnapshot &) = default;
};

struct ETHERCATCORE_EXPORT TopologyLookupResult
{
    TopologyLookupStatus status = TopologyLookupStatus::EvidenceUnavailable;
    std::optional<TopologySnapshot> snapshot;

    // Success means that the selected provider exposed structurally valid
    // evidence. Provider freshness does not bind a ProjectSnapshot revision or
    // authorize compilation or execution.
    bool isSuccess() const;
    bool hasFreshProviderEvidence() const;

    friend bool operator==(const TopologyLookupResult &, const TopologyLookupResult &) = default;
};

class ETHERCATCORE_EXPORT TopologyService final : public QObject
{
    Q_OBJECT

public:
    explicit TopologyService(ProviderRegistry *providerRegistry, QObject *parent = nullptr);

    QList<TopologySelection> availableSelections() const;
    TopologyLookupResult topology(const TopologySelection &selection) const;

signals:
    void topologyChanged(EtherCAT::Core::TopologyEvidenceSource source, Utils::Id providerId);

private:
    void attachProvider(Provider *provider);
    void handleProviderAboutToBeRemoved(Provider *provider);
    void notifyProviderChanged(Provider *provider);

    ProviderRegistry *m_providerRegistry = nullptr;
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::TopologyEvidenceSource)
Q_DECLARE_METATYPE(EtherCAT::Core::TopologyEvidenceFreshness)
Q_DECLARE_METATYPE(EtherCAT::Core::TopologyLookupStatus)
Q_DECLARE_METATYPE(EtherCAT::Core::RealTopologyGeneration)
Q_DECLARE_METATYPE(EtherCAT::Core::MockTopologyGeneration)
Q_DECLARE_METATYPE(EtherCAT::Core::TopologyGeneration)
Q_DECLARE_METATYPE(EtherCAT::Core::TopologySelection)
Q_DECLARE_METATYPE(EtherCAT::Core::TopologySnapshot)
Q_DECLARE_METATYPE(EtherCAT::Core::TopologyLookupResult)
