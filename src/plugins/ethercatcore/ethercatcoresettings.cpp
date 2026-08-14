// Copyright (C) 2026 Kvell

#include "ethercatcoresettings.h"

#include "ethercatcoreconstants.h"
#include "ethercatcoretr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/layoutbuilder.h>

namespace EtherCAT::Core {

Settings::Settings()
{
    setSettingsGroup("EtherCAT");
    setAutoApply(false);

    showAdvancedProperties.setSettingsKey("ShowAdvancedProperties");
    showAdvancedProperties.setDefaultValue(false);
    showAdvancedProperties.setLabelText(Tr::tr("Show advanced EtherCAT properties"));

    maximumRecentEvents.setSettingsKey("MaximumRecentEvents");
    maximumRecentEvents.setDefaultValue(1000);
    maximumRecentEvents.setRange(100, 100000);
    maximumRecentEvents.setLabelText(Tr::tr("Maximum recent diagnostic events:"));

    setLayouter([this] {
        using namespace Layouting;
        return Column{showAdvancedProperties, Form{maximumRecentEvents}, st};
    });

    readSettings();
}

Settings &settings()
{
    static Settings theSettings;
    return theSettings;
}

class SettingsPage final : public ::Core::IOptionsPage
{
public:
    SettingsPage()
    {
        setId(Constants::SETTINGS_GENERAL);
        setDisplayName(Tr::tr("General"));
        setCategory(Constants::SETTINGS_CATEGORY);
        setSettingsProvider([] { return &settings(); });
    }
};

std::unique_ptr<::Core::IOptionsPage> createSettingsPage()
{
    return std::make_unique<SettingsPage>();
}

} // namespace EtherCAT::Core
