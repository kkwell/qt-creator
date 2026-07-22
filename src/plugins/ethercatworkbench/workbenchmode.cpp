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
#include <QPointer>
#include <QVBoxLayout>

#include <algorithm>

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
            if (m_insertDeviceDialog) {
                m_insertDeviceDialog->raise();
                m_insertDeviceDialog->activateWindow();
                return;
            }
            const Data::NodeId masterId = controller->selectedOfflineMasterId();
            Core::DeviceRepositoryProvider *repository = controller->deviceRepository();
            Core::ProjectService *projectService = controller->projectService();
            const Core::PropertyPageContext targetContext
                = controller->treeModel()->contextForNodeId(masterId);
            const Data::NodeId projectId = targetContext.projectId;
            if (masterId.isNull() || projectId.isNull() || !repository || !projectService)
                return;
            auto *dialog = new EsiDeviceSelectionDialog(repository->devices(), this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            m_insertDeviceDialog = dialog;

            const QPointer<EsiDeviceSelectionDialog> guardedDialog = dialog;
            const auto rejectIfVisible = [guardedDialog] {
                if (guardedDialog && guardedDialog->isVisible())
                    guardedDialog->reject();
            };
            connect(
                projectService,
                &Core::ProjectService::projectAboutToBeRemoved,
                dialog,
                [projectId, rejectIfVisible](const Data::NodeId &removedProjectId) {
                    if (removedProjectId == projectId)
                        rejectIfVisible();
                });
            connect(
                projectService,
                &Core::ProjectService::activeProjectChanged,
                dialog,
                [projectId, rejectIfVisible](const Data::NodeId &, const Data::NodeId &currentId) {
                    if (currentId != projectId)
                        rejectIfVisible();
                });
            connect(
                projectService,
                &Core::ProjectService::projectChanged,
                dialog,
                [projectId, masterId, rejectIfVisible](const Data::ProjectSnapshot &project) {
                    if (project.id != projectId)
                        return;
                    const bool masterStillAvailable
                        = project.valid
                          && std::any_of(
                              project.nodes.cbegin(),
                              project.nodes.cend(),
                              [masterId](const auto &node) {
                                  return node.id == masterId
                                         && node.kind == Data::ProjectNodeKind::Master;
                              });
                    if (!masterStillAvailable)
                        rejectIfVisible();
            });
            const QPointer<WorkbenchController> guardedController = controller;
            connect(
                dialog,
                &QDialog::finished,
                this,
                [this, dialog, guardedController, masterId](int resultCode) {
                    const Data::NodeId selectedDeviceId = resultCode == QDialog::Accepted
                                                              ? dialog->selectedDeviceId()
                                                              : Data::NodeId();
                    if (m_insertDeviceDialog == dialog)
                        m_insertDeviceDialog.clear();
                    if (resultCode != QDialog::Accepted || !guardedController)
                        return;
                    const Utils::Result<> result
                        = guardedController->addDeviceToMaster(selectedDeviceId, masterId);
                    if (!result) {
                        ::Core::MessageManager::writeFlashing(
                            Tr::tr("Cannot add the ESI device: %1").arg(result.error()));
                    }
                });
            dialog->open();
        });
    }

private:
    QPointer<EsiDeviceSelectionDialog> m_insertDeviceDialog;
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
