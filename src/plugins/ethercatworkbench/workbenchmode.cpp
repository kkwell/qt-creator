// Copyright (C) 2026 Kvell

#include "workbenchmode.h"

#include "detailsview.h"
#include "esideviceselectiondialog.h"
#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcommandstrip.h"
#include "workbenchcontroller.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/outputpane.h>

#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QMenu>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

class WorkbenchModeWidget final : public QWidget
{
public:
    explicit WorkbenchModeWidget(WorkbenchController *controller)
    {
        setObjectName("EtherCATWorkbenchModeWidget");

        auto mainSplitter = new ::Core::MiniSplitter;
        mainSplitter->setObjectName("EtherCATWorkbenchMainSplitter");
        auto centralSplitter = new ::Core::MiniSplitter(Qt::Vertical, mainSplitter);
        centralSplitter->setObjectName("EtherCATWorkbenchCentralSplitter");
        centralSplitter->addWidget(new DetailsView(controller, centralSplitter));
        auto outputPane = new ::Core::OutputPanePlaceHolder(Constants::MODE_ID, centralSplitter);
        outputPane->setObjectName("EtherCATWorkbenchOutputPane");
        centralSplitter->addWidget(outputPane);
        centralSplitter->setStretchFactor(0, 1);
        centralSplitter->setStretchFactor(1, 0);

        mainSplitter->addWidget(new ::Core::NavigationWidgetPlaceHolder(
            Constants::MODE_ID, ::Core::Side::Left, mainSplitter));
        mainSplitter->addWidget(centralSplitter);
        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);

        ::Core::ActionContainer *menuContainer
            = ::Core::ActionManager::actionContainer(Constants::MENU_ID);
        ::Core::Command *openCommand
            = ::Core::ActionManager::command(Constants::OPEN_ACTION_ID);
        auto commandStrip = new WorkbenchCommandStrip(
            menuContainer ? menuContainer->menu() : nullptr,
            openCommand ? openCommand->action() : nullptr,
            this);

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(QMargins());
        layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVXxs);
        layout->addWidget(commandStrip);
        layout->addWidget(mainSplitter, 1);
        ::Core::IContext::attach(this, ::Core::Context(Constants::CONTEXT_ID));

        connect(controller, &WorkbenchController::insertDeviceRequested, this, [this, controller] {
            const Data::NodeId masterId = controller->selectedOfflineMasterId();
            Core::DeviceRepositoryProvider *repository = controller->deviceRepository();
            if (masterId.isNull() || !repository)
                return;
            EsiDeviceSelectionDialog dialog(repository->devices(), this);
            if (dialog.exec() != QDialog::Accepted)
                return;
            const Utils::Result<> result
                = controller->addDeviceToMaster(dialog.selectedDeviceId(), masterId);
            if (!result) {
                ::Core::MessageManager::writeFlashing(
                    Tr::tr("Cannot add the ESI device: %1").arg(result.error()));
            }
        });
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
