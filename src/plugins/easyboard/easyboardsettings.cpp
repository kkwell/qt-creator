// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardsettings.h"
#include "easyboardtr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/layoutbuilder.h>

namespace EasyBoard::Internal {

EasyBoardSettings &settings()
{
    static EasyBoardSettings theEasyBoardSettings;
    return theEasyBoardSettings;
}

EasyBoardSettings::EasyBoardSettings()
{
    setAutoApply(false);
    setSettingsGroup("EasyBoard");

    externalRepoUrl.setDefaultValue("https://qc-extensions.qt.io");
    externalRepoUrl.setReadOnly(true);

    useExternalRepo.setSettingsKey("UseExternalRepo");
    useExternalRepo.setLabelText(Tr::tr("Use external repository"));
    useExternalRepo.setToolTip(Tr::tr("Repository: %1").arg(externalRepoUrl()));
    useExternalRepo.setDefaultValue(false);

    setLayouter([this] {
        using namespace Layouting;

        return Column {
            useExternalRepo,
            st
        };
    });

    readSettings();
}

class EasyBoardSettingsPage : public Core::IOptionsPage
{
public:
    EasyBoardSettingsPage()
    {
        setId("EasyBoard");
        setDisplayName(Tr::tr("Board"));
        setCategory(Core::Constants::SETTINGS_CATEGORY_CORE);
        setSettingsProvider([] { return &settings(); });
    }
};

const EasyBoardSettingsPage settingsPage;

} // EasyBoard::Internal
