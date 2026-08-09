// Copyright (C) 2026 Kvell

#include "automationservice.h"

#include <algorithm>

namespace EtherCAT::Core {

bool AutomationTopologyView::isValid() const
{
    if (!selection.isValid())
        return false;
    switch (lookup.status) {
    case TopologyLookupStatus::Success:
        if (!lookup.snapshot || lookup.snapshot->selection != selection
            || !lookup.snapshot->isValid()) {
            return false;
        }
        switch (lookup.snapshot->freshness) {
        case TopologyEvidenceFreshness::Fresh:
            if (selection.source == TopologyEvidenceSource::RealController) {
                return lookup.snapshot->controllerEvidence
                       && lookup.snapshot->controllerEvidence->hasCompleteProvenance();
            }
            if (selection.source == TopologyEvidenceSource::MockScan) {
                return lookup.snapshot->mockEvidence
                       && lookup.snapshot->mockEvidence->snapshot.complete
                       && lookup.snapshot->mockEvidence->snapshot.capturedAt.isValid()
                       && !lookup.snapshot->mockEvidence->snapshot.id.isNull();
            }
            return false;
        case TopologyEvidenceFreshness::Stale:
        case TopologyEvidenceFreshness::Incomplete:
            return true;
        }
        return false;
    case TopologyLookupStatus::InvalidSelection:
    case TopologyLookupStatus::ProviderNotFound:
    case TopologyLookupStatus::ProviderKindMismatch:
    case TopologyLookupStatus::EvidenceUnavailable:
    case TopologyLookupStatus::ScopeMismatch:
    case TopologyLookupStatus::EvidenceInvalid:
        return !lookup.snapshot;
    }
    return false;
}

QString automationControllerId(const Data::ControllerConnectionScope &scope)
{
    if (scope.projectId.isNull() || scope.masterId.isNull())
        return {};
    return QString("ide:%1:%2").arg(scope.projectId.toString(), scope.masterId.toString());
}

std::optional<AutomationContextSnapshot> AutomationService::context(const QString &controllerId) const
{
    const QList<AutomationContextSnapshot> current = contexts();
    const auto found = std::find_if(
        current.cbegin(),
        current.cend(),
        [&controllerId](const AutomationContextSnapshot &candidate) {
            return candidate.controllerId == controllerId;
        });
    if (found == current.cend())
        return std::nullopt;
    return *found;
}

} // namespace EtherCAT::Core
