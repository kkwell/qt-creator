// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0


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

#include <utils/fsengine/fsengine.h>

using namespace Utils;

namespace EasyBoard::Internal {

class EasyBoardPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EasyBoard.json")

public:
    EasyBoardPlugin()
    {
        // FSEngine::registerDeviceScheme(u"ssh");
    }

    ~EasyBoardPlugin() final
    {
        // FSEngine::unregisterDeviceScheme(u"ssh");
    }

    void initialize() final
    {

    }
};

} // EasyBoard::Internal

#include "easyboardplugin.moc"
