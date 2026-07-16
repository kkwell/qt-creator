// Copyright (C) 2026 Kvell

#include "detailsview.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <utils/stylehelper.h>

#include <QLabel>
#include <QSet>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace EtherCAT::Workbench::Internal {

struct PageCandidate
{
    Core::PropertyPageProvider *provider = nullptr;
    Core::PropertyPageDescriptor descriptor;
};

DetailsView::DetailsView(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_title(new QLabel(this))
    , m_emptyState(new QLabel(this))
    , m_tabs(new QTabWidget(this))
{
    setObjectName("EtherCATWorkbenchDetails");
    m_title->setObjectName("EtherCATWorkbenchDetailsTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setFont(Utils::StyleHelper::uiFont(Utils::StyleHelper::UiElementH4));

    m_emptyState->setObjectName("EtherCATWorkbenchEmptyState");
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_tabs->setObjectName("EtherCATWorkbenchPropertyTabs");
    m_tabs->setDocumentMode(true);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->addWidget(m_title);
    layout->addWidget(m_emptyState, 1);
    layout->addWidget(m_tabs, 1);

    if (controller->selectionService()) {
        connect(
            controller->selectionService(),
            &Core::SelectionService::currentNodeChanged,
            this,
            [this](const Data::NodeId &nodeId) { setCurrentNode(nodeId); });
    }
    if (controller->providerRegistry()) {
        connect(
            controller->providerRegistry(),
            &Core::ProviderRegistry::providerAdded,
            this,
            [this](Core::Provider *provider) {
                if (provider->kind() == Core::ProviderKind::PropertyPage) {
                    watchPropertyPageProvider(provider);
                    rebuildPages();
                }
            });
        connect(
            controller->providerRegistry(),
            &Core::ProviderRegistry::providerAboutToBeRemoved,
            this,
            &DetailsView::handleProviderRemoving);
        for (Core::Provider *provider : controller->providerRegistry()->providers(
                 Core::ProviderKind::PropertyPage)) {
            watchPropertyPageProvider(provider);
        }
    }
    connect(
        controller,
        &WorkbenchController::optionalProvidersChanged,
        this,
        &DetailsView::rebuildPages);
    if (controller->deviceRepository()) {
        connect(
            controller->deviceRepository(),
            &Core::DeviceRepositoryProvider::devicesReset,
            this,
            &DetailsView::refreshPageContents);
        connect(
            controller->deviceRepository(),
            &Core::DeviceRepositoryProvider::devicesChanged,
            this,
            [this] { refreshPageContents(); });
        connect(
            controller->deviceRepository(),
            &Core::DeviceRepositoryProvider::indexingChanged,
            this,
            [this] { refreshPageContents(); });
    }
    if (controller->projectService()) {
        connect(
            controller->projectService(),
            &Core::ProjectService::projectChanged,
            this,
            [this] { refreshPageContents(); });
    }

    const Data::NodeId initialSelection = controller->selectionService()
                                                ? controller->selectionService()->currentNodeId()
                                                : Data::NodeId();
    setCurrentNode(initialSelection);
}

DetailsView::~DetailsView()
{
    clearPages();
}

Core::PropertyPageContext DetailsView::currentContext() const
{
    return m_context;
}

QTabWidget *DetailsView::tabWidget() const
{
    return m_tabs;
}

void DetailsView::setCurrentNode(const Data::NodeId &nodeId)
{
    m_context = m_controller ? m_controller->treeModel()->contextForNodeId(nodeId)
                             : Core::PropertyPageContext();
    rebuildPages();
}

void DetailsView::rebuildPages()
{
    const QString previousKey
        = m_tabs->currentWidget()
              ? m_tabs->currentWidget()->property("EtherCAT.PageKey").toString()
              : QString();
    clearPages();

    if (m_context.nodeKind == Core::WorkbenchNodeKind::None) {
        m_title->setText(Tr::tr("EtherCAT Workbench"));
        m_emptyState->setText(Tr::tr("Select a project, master, or ESI device in the tree."));
        m_emptyState->show();
        m_tabs->hide();
        return;
    }
    m_title->setText(m_context.displayName);

    QList<PageCandidate> candidates;
    if (m_controller && m_controller->providerRegistry()) {
        for (Core::Provider *providerObject : m_controller->providerRegistry()->providers(
                 Core::ProviderKind::PropertyPage)) {
            auto provider = qobject_cast<Core::PropertyPageProvider *>(providerObject);
            if (!provider || !provider->isAvailable())
                continue;
            QSet<Utils::Id> pageIds;
            for (const Core::PropertyPageDescriptor &descriptor : provider->pages(m_context)) {
                if (!descriptor.id.isValid() || descriptor.displayName.isEmpty()
                    || pageIds.contains(descriptor.id)) {
                    continue;
                }
                pageIds.insert(descriptor.id);
                candidates.append({provider, descriptor});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        if (left.descriptor.priority != right.descriptor.priority)
            return left.descriptor.priority < right.descriptor.priority;
        const int providerOrder = left.provider->id().toString().compare(
            right.provider->id().toString(), Qt::CaseInsensitive);
        if (providerOrder != 0)
            return providerOrder < 0;
        return left.descriptor.id.toString() < right.descriptor.id.toString();
    });

    int restoredIndex = -1;
    for (const PageCandidate &candidate : std::as_const(candidates)) {
        QWidget *page = candidate.provider->createPage(candidate.descriptor.id, m_tabs);
        if (!page)
            continue;
        const QString pageKey = candidate.provider->id().toString() + '/'
                                + candidate.descriptor.id.toString();
        page->setProperty("EtherCAT.PageKey", pageKey);
        candidate.provider->updatePage(candidate.descriptor.id, page, m_context);
        const int index = m_tabs->addTab(page, candidate.descriptor.displayName);
        m_pages.append({candidate.provider, candidate.descriptor.id, page});
        if (pageKey == previousKey)
            restoredIndex = index;
    }

    if (restoredIndex >= 0)
        m_tabs->setCurrentIndex(restoredIndex);
    const bool havePages = !m_pages.isEmpty();
    m_tabs->setVisible(havePages);
    m_emptyState->setVisible(!havePages);
    if (!havePages) {
        m_emptyState->setText(
            m_context.nodeKind == Core::WorkbenchNodeKind::Diagnostics
                ? Tr::tr("Diagnostics plugin is not installed.")
                : Tr::tr("No property page provider supports this node."));
    }
}

void DetailsView::refreshPageContents()
{
    for (const PageEntry &entry : std::as_const(m_pages)) {
        if (entry.provider && entry.widget)
            entry.provider->updatePage(entry.pageId, entry.widget, m_context);
    }
}

void DetailsView::clearPages()
{
    while (m_tabs->count() > 0) {
        QWidget *page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete page;
    }
    m_pages.clear();
}

void DetailsView::handleProviderRemoving(Core::Provider *provider)
{
    if (provider->kind() != Core::ProviderKind::PropertyPage)
        return;
    const bool ownsPage = std::any_of(
        m_pages.cbegin(), m_pages.cend(), [provider](const PageEntry &entry) {
            return entry.provider == provider;
        });
    if (!ownsPage)
        return;
    clearPages();
    QMetaObject::invokeMethod(this, [this] { rebuildPages(); }, Qt::QueuedConnection);
}

void DetailsView::watchPropertyPageProvider(Core::Provider *provider)
{
    if (provider->kind() != Core::ProviderKind::PropertyPage)
        return;
    connect(
        provider,
        &Core::Provider::availabilityChanged,
        this,
        &DetailsView::rebuildPages,
        Qt::UniqueConnection);
}

} // namespace EtherCAT::Workbench::Internal
