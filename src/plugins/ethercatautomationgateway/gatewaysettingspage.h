// Copyright (C) 2026 Kvell

#pragma once

#include <memory>

namespace Core {
class IOptionsPage;
}

namespace EtherCAT::AutomationGateway::Internal {

class GatewayRuntimeController;

std::unique_ptr<::Core::IOptionsPage> createGatewaySettingsPage(
    GatewayRuntimeController *runtime);

} // namespace EtherCAT::AutomationGateway::Internal
