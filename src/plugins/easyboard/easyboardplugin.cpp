// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#include "easyboardtr.h"
#include "easyboardwidget.h"

#include <remotelinux/customcommanddeploystep.h>
#include <remotelinux/killappstep.h>
#include <remotelinux/linuxdevice.h>
#include <remotelinux/remotelinux_constants.h>
#include <remotelinux/remotelinuxcustomrunconfiguration.h>
#include <remotelinux/remotelinuxdebugsupport.h>
#include <remotelinux/remotelinuxdeploysupport.h>
#include <remotelinux/remotelinuxrunconfiguration.h>
#include <remotelinux/tarpackagecreationstep.h>
#include <remotelinux/tarpackagedeploystep.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/coreicons.h>
#include <coreplugin/icore.h>
#include <coreplugin/imode.h>
#include <coreplugin/ieasyboardpage.h>
#include <coreplugin/modemanager.h>

#include <utils/fsengine/fsengine.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/icon.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/styledbar.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/treemodel.h>

#include <QButtonGroup>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>


using namespace Core;
using namespace ExtensionSystem;
using namespace Utils;
using namespace Utils::StyleHelper::SpacingTokens;

namespace EasyBoard::Internal {

const char currentPageSettingsKeyC[] = "EmbedLabs";

class EasyBoardMode : public IMode
{
    Q_OBJECT

public:
    EasyBoardMode();
    ~EasyBoardMode();

    void initPlugins();

private:
    void addPage(IEasyBoardPage *page);

    QStackedWidget *m_pageStack;
    QList<IEasyBoardPage *> m_pluginList;
    QList<QAbstractButton *> m_pageButtons;
    QButtonGroup *m_buttonGroup;
    Id m_activePage;
    Id m_defaultPage;
};

class EasyBoardPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EasyBoard.json")

public:
    EasyBoardPlugin()
    {

    }

    ~EasyBoardPlugin() final
    {
        delete m_easyboardmode;
    }

    Result<> initialize(const QStringList &) final
    {
        // QPalette palette = creatorTheme()->palette();
        // palette.setColor(QPalette::Window, themeColor(Theme::Welcome_BackgroundColor));
        // pDevice = new DeviceList(palette);
        // pDev = new DevicePage;
        //addAutoReleasedObject(pDev);
        m_easyboardmode = new EasyBoardMode;
        //addAutoReleasedObject(m_sgasMode);
        return ResultOk;
    }

    void extensionsInitialized() final
    {
        m_easyboardmode->initPlugins();
        // ModeManager::activateMode(m_welcomeMode->id());
    }

    EasyBoardMode *m_easyboardmode = nullptr;
};

EasyBoardMode::EasyBoardMode()
{
    setObjectName("EasyBoardMode");
    setId(Constants::MODE_EASYBOARD);
    setContext(Context(Constants::C_EASYBOARD_MODE));
    setDisplayName(Tr::tr("Devices"));
    const Icon CLASSIC(":/easyboard/images/mode_device.png");
    const Icon FLAT({{":/easyboard/images/mode_device_mask.png",
                      Theme::IconsBaseColor}});
    setIcon(Icon::sideBarIcon(CLASSIC, FLAT));

    setPriority(Constants::P_MODE_EASYBOARD);


    using namespace Layouting;
    auto widget = Column {
        new StyledBar,
        createEasyBoardWidget(),
        noMargin, spacing(0),
    }.emerge();

    setWidget(widget);
}

EasyBoardMode::~EasyBoardMode()
{
    QtcSettings *settings = ICore::settings();
    settings->setValueWithDefault(currentPageSettingsKeyC,
                                  m_activePage.toSetting(),
                                  m_defaultPage.toSetting());
    delete widget();
}

void EasyBoardMode::initPlugins()
{
    QtcSettings *settings = ICore::settings();
    m_activePage = Id::fromSetting(settings->value(currentPageSettingsKeyC));


}

void EasyBoardMode::addPage(IEasyBoardPage *page)
{

}


} // EasyBoard::Internal

#include "easyboardplugin.moc"
