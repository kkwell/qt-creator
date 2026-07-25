// Copyright (C) 2026 Kvell

#include "automationservice.h"

#include <algorithm>

namespace EtherCAT::Core {

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
