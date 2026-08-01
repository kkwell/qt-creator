// Copyright (C) 2026 Kvell

#include "builtinpropertypages.h"

#include "coeonlinepage.h"
#include "communicationpage.h"
#include "dcpage.h"
#include "deploymentpage.h"
#include "ethercatpage.h"
#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "esirepositorypage.h"
#include "generalpage.h"
#include "processdatapage.h"
#include "semanticcontrolpage.h"
#include "startuppage.h"
#include "workbenchcontroller.h"

#include <ethercatcore/semanticruntimeservice.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/stylehelper.h>

#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

class BuiltinPageWidget final : public QWidget
{
public:
    explicit BuiltinPageWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , summary(new QLabel(this))
        , tree(new QTreeWidget(this))
    {
        setProperty("EtherCAT.Workbench.BuiltinPage", true);
        summary->setObjectName("EtherCATWorkbenchPageSummary");
        summary->setWordWrap(true);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        summary->setAccessibleName(Tr::tr("EtherCAT page summary"));
        tree->setObjectName("EtherCATWorkbenchPageTree");
        tree->setAlternatingRowColors(true);
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(true);
        tree->header()->setStretchLastSection(true);

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM,
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM);
        layout->addWidget(summary);
        layout->addWidget(tree, 1);
    }

    void reset(const QString &text, const QStringList &headers)
    {
        summary->setText(text);
        summary->setAccessibleDescription(text);
        tree->clear();
        tree->setColumnCount(qMax(1, headers.size()));
        tree->setHeaderLabels(headers);
        tree->setVisible(!headers.isEmpty());
    }

    void addRow(const QStringList &values)
    {
        tree->addTopLevelItem(new QTreeWidgetItem(values));
    }

    QLabel *summary;
    QTreeWidget *tree;
};

static BuiltinPageWidget *pageWidget(QWidget *page)
{
    return page && page->property("EtherCAT.Workbench.BuiltinPage").toBool()
               ? static_cast<BuiltinPageWidget *>(page)
               : nullptr;
}

static bool shouldExposeDiagnosticsFallback(WorkbenchController *controller)
{
    return Core::isMockUiEnabled()
           || (controller
               && controller->diagnosticsProviderPresentation().state
                      != OptionalProviderState::Absent);
}

BuiltinPropertyPageProvider::BuiltinPropertyPageProvider(
    WorkbenchController *controller,
    QObject *parent,
    Core::SemanticRuntimeService *runtimeService)
    : Core::PropertyPageProvider(
          Constants::BUILTIN_PAGE_PROVIDER_ID, Tr::tr("Built-in EtherCAT pages"), parent)
    , m_controller(controller)
    , m_runtimeService(
          runtimeService
              ? runtimeService
              : ExtensionSystem::PluginManager::getObject<Core::SemanticRuntimeService>())
{
    setAvailable(true);
}

QList<Core::PropertyPageDescriptor> BuiltinPropertyPageProvider::pages(
    const Core::PropertyPageContext &context) const
{
    using Kind = Core::WorkbenchNodeKind;
    switch (context.nodeKind) {
    case Kind::Project:
    case Kind::Target:
    case Kind::DeviceRepository:
        return {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
    case Kind::Master:
    {
        QList<Core::PropertyPageDescriptor> result
            = {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100},
               {Utils::Id(Constants::COMMUNICATION_PAGE_ID), Tr::tr("Communication"), 150},
               {Utils::Id(Constants::ETHERCAT_PAGE_ID), Tr::tr("EtherCAT"), 200},
               {Utils::Id(Constants::DEPLOYMENT_PAGE_ID), Tr::tr("Deployment"), 550},
               {Utils::Id(Constants::ESI_REPOSITORY_PAGE_ID),
                Tr::tr("Device Descriptions"),
                600}};
        if ((!m_controller || !m_controller->diagnosticsAvailable())
            && shouldExposeDiagnosticsFallback(m_controller)) {
            result.append(
                {Utils::Id(Constants::ONLINE_PAGE_ID), Tr::tr("Online"), 800});
            result.append(
                {Utils::Id(Constants::DIAGNOSTICS_PAGE_ID), Tr::tr("Diagnostics"), 900});
        }
        return result;
    }
    case Kind::Device:
    {
        QList<Core::PropertyPageDescriptor> result
            = {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100},
               {Utils::Id(Constants::ETHERCAT_PAGE_ID), Tr::tr("EtherCAT"), 200},
               {Utils::Id(Constants::PROCESS_DATA_PAGE_ID), Tr::tr("Process Data"), 300},
               {Utils::Id(Constants::COE_ONLINE_PAGE_ID), Tr::tr("CoE Online"), 350},
               {Utils::Id(Constants::STARTUP_PAGE_ID), Tr::tr("Startup"), 400},
               {Utils::Id(Constants::DC_PAGE_ID), Tr::tr("DC"), 500}};
        if ((!m_controller || !m_controller->diagnosticsAvailable())
            && shouldExposeDiagnosticsFallback(m_controller)) {
            result.append({Utils::Id(Constants::ONLINE_PAGE_ID), Tr::tr("Online"), 800});
        }
        return result;
    }
    case Kind::ConfiguredSlave:
    {
        QList<Core::PropertyPageDescriptor> result
            = {{Utils::Id(Constants::SEMANTIC_CONTROL_PAGE_ID), Tr::tr("Control"), 50},
               {Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100},
               {Utils::Id(Constants::ETHERCAT_PAGE_ID), Tr::tr("EtherCAT"), 200},
               {Utils::Id(Constants::PROCESS_DATA_PAGE_ID), Tr::tr("Process Data"), 300},
               {Utils::Id(Constants::COE_ONLINE_PAGE_ID), Tr::tr("CoE Online"), 350},
               {Utils::Id(Constants::STARTUP_PAGE_ID), Tr::tr("Startup"), 400},
               {Utils::Id(Constants::DC_PAGE_ID), Tr::tr("DC"), 500}};
        if ((!m_controller || !m_controller->diagnosticsAvailable())
            && shouldExposeDiagnosticsFallback(m_controller)) {
            result.append({Utils::Id(Constants::ONLINE_PAGE_ID), Tr::tr("Online"), 800});
        }
        return result;
    }
    case Kind::ProcessInputs:
    case Kind::ProcessOutputs:
    case Kind::RxPdoGroup:
    case Kind::TxPdoGroup:
    case Kind::Pdo:
    case Kind::PdoEntry:
        return {{Utils::Id(Constants::PROCESS_DATA_PAGE_ID), Tr::tr("Process Data"), 300}};
    case Kind::Module:
        if (m_controller && m_controller->treeModel()
            && (m_controller->treeModel()->controllerTopologySlave(context.nodeId)
                || m_controller->treeModel()->semanticControlSelection(context.nodeId))) {
            return {{Utils::Id(Constants::SEMANTIC_CONTROL_PAGE_ID), Tr::tr("Control"), 50},
                    {Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
        }
        return {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
    case Kind::Modules:
        if (m_controller && m_controller->treeModel()
            && m_controller->treeModel()->semanticControlSelection(context.nodeId)) {
            return {{Utils::Id(Constants::SEMANTIC_CONTROL_PAGE_ID), Tr::tr("Control"), 50},
                    {Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
        }
        return {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
    case Kind::Channel:
        if (m_controller && m_controller->treeModel()
            && m_controller->treeModel()->semanticControlSelection(context.nodeId)) {
            return {{Utils::Id(Constants::SEMANTIC_CONTROL_PAGE_ID), Tr::tr("Control"), 50},
                    {Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
        }
        return {{Utils::Id(Constants::GENERAL_PAGE_ID), Tr::tr("General"), 100}};
    case Kind::Diagnostics:
        if (m_controller && m_controller->diagnosticsAvailable())
            return {};
        if (!shouldExposeDiagnosticsFallback(m_controller))
            return {};
        return {{Utils::Id(Constants::DIAGNOSTICS_PAGE_ID), Tr::tr("Diagnostics"), 900}};
    default:
        return {};
    }
}

QWidget *BuiltinPropertyPageProvider::createPage(Utils::Id pageId, QWidget *parent)
{
    if ((pageId == Utils::Id(Constants::ONLINE_PAGE_ID)
         || pageId == Utils::Id(Constants::DIAGNOSTICS_PAGE_ID))
        && !shouldExposeDiagnosticsFallback(m_controller)) {
        return nullptr;
    }
    const QList<Utils::Id> knownPages = {
        Constants::GENERAL_PAGE_ID,
        Constants::COMMUNICATION_PAGE_ID,
        Constants::DEPLOYMENT_PAGE_ID,
        Constants::ETHERCAT_PAGE_ID,
        Constants::PROCESS_DATA_PAGE_ID,
        Constants::COE_ONLINE_PAGE_ID,
        Constants::STARTUP_PAGE_ID,
        Constants::DC_PAGE_ID,
        Constants::ESI_REPOSITORY_PAGE_ID,
        Constants::ONLINE_PAGE_ID,
        Constants::DIAGNOSTICS_PAGE_ID,
        Constants::SEMANTIC_CONTROL_PAGE_ID,
    };
    if (!knownPages.contains(pageId))
        return nullptr;
    if (pageId == Utils::Id(Constants::GENERAL_PAGE_ID)) {
        auto page = new GeneralPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::COMMUNICATION_PAGE_ID)) {
        auto page = new CommunicationPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::DEPLOYMENT_PAGE_ID)) {
        auto page = new DeploymentPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::ETHERCAT_PAGE_ID)) {
        auto page = new EtherCATPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::PROCESS_DATA_PAGE_ID)) {
        auto page = new ProcessDataPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::COE_ONLINE_PAGE_ID)) {
        auto page = new CoeOnlinePage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::STARTUP_PAGE_ID)) {
        auto page = new StartupPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::DC_PAGE_ID)) {
        auto page = new DcPage(m_controller, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::ESI_REPOSITORY_PAGE_ID)) {
        auto page = new EsiRepositoryPage(
            m_controller ? m_controller->deviceRepository() : nullptr, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    if (pageId == Utils::Id(Constants::SEMANTIC_CONTROL_PAGE_ID)) {
        auto page = new SemanticControlPage(m_controller, m_runtimeService, parent);
        page->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
        return page;
    }
    auto widget = new BuiltinPageWidget(parent);
    widget->setObjectName("EtherCATWorkbenchPropertyPage_" + pageId.toString());
    return widget;
}

void BuiltinPropertyPageProvider::updatePage(
    Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context)
{
    if (pageId == Utils::Id(Constants::GENERAL_PAGE_ID)) {
        if (auto generalPage = qobject_cast<GeneralPage *>(page))
            generalPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::COMMUNICATION_PAGE_ID)) {
        if (auto communicationPage = qobject_cast<CommunicationPage *>(page))
            communicationPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::DEPLOYMENT_PAGE_ID)) {
        if (auto deploymentPage = qobject_cast<DeploymentPage *>(page))
            deploymentPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::ETHERCAT_PAGE_ID)) {
        if (auto ethercatPage = qobject_cast<EtherCATPage *>(page))
            ethercatPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::PROCESS_DATA_PAGE_ID)) {
        if (auto processDataPage = qobject_cast<ProcessDataPage *>(page))
            processDataPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::COE_ONLINE_PAGE_ID)) {
        if (auto coeOnlinePage = qobject_cast<CoeOnlinePage *>(page))
            coeOnlinePage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::STARTUP_PAGE_ID)) {
        if (auto startupPage = qobject_cast<StartupPage *>(page))
            startupPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::DC_PAGE_ID)) {
        if (auto dcPage = qobject_cast<DcPage *>(page))
            dcPage->setContext(context);
        return;
    }
    if (pageId == Utils::Id(Constants::ESI_REPOSITORY_PAGE_ID)) {
        if (page)
            static_cast<EsiRepositoryPage *>(page)->refresh();
        return;
    }
    if (pageId == Utils::Id(Constants::SEMANTIC_CONTROL_PAGE_ID)) {
        if (auto semanticControlPage = qobject_cast<SemanticControlPage *>(page))
            semanticControlPage->setContext(context);
        return;
    }
    BuiltinPageWidget *widget = pageWidget(page);
    if (!widget || !m_controller)
        return;

    if (pageId == Utils::Id(Constants::ONLINE_PAGE_ID)) {
        const OptionalProviderPresentation provider
            = m_controller->diagnosticsProviderPresentation();
        QString text;
        if (provider.state == OptionalProviderState::Absent) {
            text = Tr::tr(
                "Online data is unavailable. No Diagnostics Provider is registered. V1 "
                "provides only local Mock diagnostics and contains no controller protocol.");
        } else if (provider.state == OptionalProviderState::Unavailable) {
            text = Tr::tr(
                       "Online data is unavailable. %1 is registered but unavailable. This V1 "
                       "stage contains no controller protocol.")
                       .arg(optionalProviderDisplayName(
                           provider, Core::ProviderKind::Diagnostics));
        } else {
            text = Tr::tr(
                       "%1 is available, but no Online page is active for this selection. No "
                       "controller protocol is inferred.")
                       .arg(optionalProviderDisplayName(
                           provider, Core::ProviderKind::Diagnostics));
        }
        widget->reset(text, {});
        return;
    }

    if (pageId == Utils::Id(Constants::DIAGNOSTICS_PAGE_ID)) {
        const OptionalProviderPresentation provider
            = m_controller->diagnosticsProviderPresentation();
        QString text;
        if (provider.state == OptionalProviderState::Absent) {
            text = Tr::tr(
                "No Diagnostics Provider is registered. V1 provides only local Mock "
                "diagnostics; Workbench produces no WKC, DC, link, alarm, or live controller "
                "state.");
        } else if (provider.state == OptionalProviderState::Unavailable) {
            text = Tr::tr(
                       "%1 is registered but unavailable. Workbench produces no WKC, DC, link, "
                       "alarm, or live controller state.")
                       .arg(optionalProviderDisplayName(
                           provider, Core::ProviderKind::Diagnostics));
        } else {
            text = Tr::tr(
                       "%1 is available, but no Diagnostics page is active for this selection. "
                       "No controller or hardware state is inferred.")
                       .arg(optionalProviderDisplayName(
                           provider, Core::ProviderKind::Diagnostics));
        }
        widget->reset(text, {});
    }
}

} // namespace EtherCAT::Workbench::Internal
