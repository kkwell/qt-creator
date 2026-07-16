// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <utils/aspects.h>

#include <memory>

namespace Core {
class IOptionsPage;
}

namespace EtherCAT::Core {

class ETHERCATCORE_EXPORT Settings final : public Utils::AspectContainer
{
public:
    Settings();

    Utils::BoolAspect showAdvancedProperties{this};
    Utils::IntegerAspect maximumRecentEvents{this};
};

ETHERCATCORE_EXPORT Settings &settings();
std::unique_ptr<::Core::IOptionsPage> createSettingsPage();

} // namespace EtherCAT::Core
