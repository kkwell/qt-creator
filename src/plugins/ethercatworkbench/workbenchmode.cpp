// Copyright (C) 2026 Kvell

#include "workbenchmode.h"

#include "detailsview.h"
#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/outputpane.h>

#include <utils/utilsicons.h>

namespace EtherCAT::Workbench::Internal {

class WorkbenchModeWidget final : public ::Core::MiniSplitter
{
public:
    explicit WorkbenchModeWidget(WorkbenchController *controller)
    {
        setObjectName("EtherCATWorkbenchModeWidget");

        auto centralSplitter = new ::Core::MiniSplitter(Qt::Vertical);
        centralSplitter->setObjectName("EtherCATWorkbenchCentralSplitter");
        centralSplitter->addWidget(new DetailsView(controller, centralSplitter));
        auto outputPane = new ::Core::OutputPanePlaceHolder(Constants::MODE_ID, centralSplitter);
        outputPane->setObjectName("EtherCATWorkbenchOutputPane");
        centralSplitter->addWidget(outputPane);
        centralSplitter->setStretchFactor(0, 1);
        centralSplitter->setStretchFactor(1, 0);

        addWidget(new ::Core::NavigationWidgetPlaceHolder(
            Constants::MODE_ID, ::Core::Side::Left, this));
        addWidget(centralSplitter);
        setStretchFactor(0, 0);
        setStretchFactor(1, 1);
        ::Core::IContext::attach(this, ::Core::Context(Constants::CONTEXT_ID));
    }
};

WorkbenchMode::WorkbenchMode(WorkbenchController *controller, QObject *parent)
    : ::Core::IMode(parent)
{
    setObjectName("EtherCATWorkbenchMode");
    setId(Constants::MODE_ID);
    setContext(::Core::Context(Constants::CONTEXT_ID, ::Core::Constants::C_NAVIGATION_PANE));
    setDisplayName(Tr::tr("EtherCAT"));
    setIcon(Utils::Icons::SETTINGS.icon());
    setPriority(Constants::MODE_PRIORITY);
    setWidgetCreator([controller] { return new WorkbenchModeWidget(controller); });

    connect(
        ::Core::ModeManager::instance(),
        &::Core::ModeManager::currentModeChanged,
        this,
        [this](Utils::Id modeId) {
            if (modeId == id()) {
                ::Core::NavigationWidget::activateSubWidget(
                    Constants::NAVIGATION_ID, ::Core::Side::Left);
            }
        });
}

} // namespace EtherCAT::Workbench::Internal
