// Copyright (C) 2026 Kvell

#include "detailsview.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <ethercatcore/providerregistry.h>
#include <ethercatcore/selectionservice.h>

#include <utils/stylehelper.h>

#include <QApplication>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace EtherCAT::Workbench::Internal {

static constexpr int maxSynchronousPageOperations = 8;

struct PageCandidate
{
    QPointer<Core::PropertyPageProvider> provider;
    Core::PropertyPageDescriptor descriptor;
};

static QString diagnosticsEmptyState(const OptionalProviderPresentation &provider)
{
    const QString displayName = optionalProviderDisplayName(
        provider, Core::ProviderKind::Diagnostics);
    switch (provider.state) {
    case OptionalProviderState::Absent:
        return Tr::tr(
            "No Diagnostics Provider is registered. V1 diagnostics are local Mock only; no "
            "controller connection or hardware state is represented.");
    case OptionalProviderState::Unavailable:
        return Tr::tr(
                   "%1 is registered but unavailable. No controller or hardware state is "
                   "available.")
            .arg(displayName);
    case OptionalProviderState::Available:
        return Tr::tr(
                   "%1 is available but does not provide a Diagnostics page for this selection. "
                   "No controller or hardware state is inferred.")
            .arg(displayName);
    }
    return {};
}

DetailsView::DetailsView(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_title(new QLabel(this))
    , m_emptyState(new QLabel(this))
    , m_tabs(new QTabWidget(this))
{
    setObjectName("EtherCATWorkbenchDetails");
    setAccessibleName(Tr::tr("EtherCAT Workbench details"));
    setAccessibleDescription(
        Tr::tr(
            "Shows engineering properties and available controller communication for the "
            "EtherCAT node selected in the device tree."));
    m_title->setObjectName("EtherCATWorkbenchDetailsTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setFont(Utils::StyleHelper::uiFont(Utils::StyleHelper::UiElementH4));
    m_title->setAccessibleDescription(Tr::tr("Current EtherCAT Workbench selection"));

    m_emptyState->setObjectName("EtherCATWorkbenchEmptyState");
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_emptyState->setFocusPolicy(Qt::TabFocus);
    m_emptyState->setAccessibleName(Tr::tr("EtherCAT Workbench guidance"));
    m_tabs->setObjectName("EtherCATWorkbenchPropertyTabs");
    m_tabs->setDocumentMode(true);
    m_tabs->setAccessibleName(Tr::tr("EtherCAT property pages"));
    m_tabs->setAccessibleDescription(
        Tr::tr("Switch between engineering property pages for the selected EtherCAT node."));
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *focusWidget) {
        if (m_internalFocusChange
            || (!m_rebuildTransactionActive && !m_rebuildPending
                && !m_pageOperationDrainPosted)
            || !focusWidget || m_clearingPages || m_mutatingTabs) {
            return;
        }
        if (focusWidget != m_tabs && !m_tabs->isAncestorOf(focusWidget)) {
            cancelTransactionFocusRestore();
            return;
        }
        if (m_processingPageOperations)
            return;
        QWidget *page = m_tabs->currentWidget();
        const QString pageKey
            = page ? page->property("EtherCAT.PageKey").toString() : QString();
        if (!pageKey.isEmpty()) {
            m_pendingPreferredPageKey = pageKey;
            m_transactionPreferredPageKey = pageKey;
        }
        PageFocusState focusState = currentPageFocusState();
        if (!focusState.active) {
            cancelTransactionFocusRestore();
            return;
        }
        focusState.contextNodeId = m_context.nodeId;
        focusState.rebuildGeneration = m_rebuildGeneration;
        m_pendingPageFocusState = focusState;
        m_transactionPageFocusState = focusState;
        m_transactionFocusCancelled = false;
    });
    const auto handleTabChoice = [this](int index) {
        if (index < 0 || m_processingPageOperations || m_clearingPages || m_mutatingTabs
            || (!m_rebuildPending && !m_rebuildTransactionActive
                && !m_pageOperationDrainPosted)) {
            return;
        }
        QWidget *page = m_tabs->widget(index);
        if (!page)
            return;
        const QString pageKey = page->property("EtherCAT.PageKey").toString();
        m_pendingPreferredPageKey = pageKey;
        m_transactionPreferredPageKey = pageKey;
        cancelTransactionFocusRestore();
    };
    connect(m_tabs->tabBar(), &QTabBar::currentChanged, this, handleTabChoice);
    connect(m_tabs->tabBar(), &QTabBar::tabBarClicked, this, handleTabChoice);

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
                    m_departingPropertyPageProviderIds.remove(provider->id());
                    watchPropertyPageProvider(provider);
                    rebuildPages();
                }
            });
        connect(
            controller->providerRegistry(),
            &Core::ProviderRegistry::providerAboutToBeRemoved,
            this,
            &DetailsView::handleProviderRemoving);
        for (Core::Provider *provider :
             controller->providerRegistry()->providers(Core::ProviderKind::PropertyPage)) {
            watchPropertyPageProvider(provider);
        }
    }
    connect(
        controller,
        &WorkbenchController::diagnosticsProviderChanged,
        this,
        [this](bool availabilityChanged) {
            if (availabilityChanged) {
                rebuildPages();
                return;
            }
            refreshPageContents();
            updateNoPageState();
        });
    connect(controller->treeModel(), &QAbstractItemModel::modelReset, this, [this] {
        if (m_context.nodeKind == Core::WorkbenchNodeKind::None)
            rebuildPages();
    });
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
            [this](const Data::ProjectSnapshot &project) {
                if (project.id == m_context.projectId)
                    refreshPageContents();
            });
    }

    const Data::NodeId initialSelection = controller->selectionService()
                                                ? controller->selectionService()->currentNodeId()
                                                : Data::NodeId();
    setCurrentNode(initialSelection);
}

DetailsView::~DetailsView()
{
    m_destroying = true;
    m_rebuildPending = false;
    m_refreshPending = false;
    m_pageOperationDrainDeferred = false;
    ++m_rebuildGeneration;
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
    if (m_destroying)
        return;
    m_context = m_controller ? m_controller->treeModel()->contextForNodeId(nodeId)
                             : Core::PropertyPageContext();
    rebuildPages();
}

void DetailsView::rebuildPages()
{
    if (m_destroying)
        return;
    if (m_rebuildTransactionActive) {
        requestPageRebuild(m_transactionPreferredPageKey, m_transactionPageFocusState);
        return;
    }

    PageFocusState focusState = currentPageFocusState();
    const QString previousKey
        = m_tabs->currentWidget() ? m_tabs->currentWidget()->property("EtherCAT.PageKey").toString()
                                  : QString();
    requestPageRebuild(previousKey, focusState);
}

void DetailsView::requestPageRebuild(
    const QString &preferredPageKey, PageFocusState focusState)
{
    if (m_destroying)
        return;
    if (m_transactionFocusCancelled)
        focusState.active = false;
    focusState.contextNodeId = m_context.nodeId;
    if (m_rebuildPending && m_pendingPreferredPageKey == preferredPageKey
        && m_pendingPageFocusState.contextNodeId == focusState.contextNodeId
        && m_pendingPageFocusState.pageKey == focusState.pageKey
        && m_pendingPageFocusState.objectName == focusState.objectName
        && m_pendingPageFocusState.active == focusState.active) {
        return;
    }
    ++m_rebuildGeneration;
    focusState.rebuildGeneration = m_rebuildGeneration;
    m_pendingPreferredPageKey = preferredPageKey;
    m_pendingPageFocusState = focusState;
    m_rebuildPending = true;
    processPendingPageOperations();
}

void DetailsView::processPendingPageOperations()
{
    if (m_destroying || m_pageOperationDrainDeferred || m_pageOperationDrainPosted
        || m_processingPageOperations || m_clearingPages || m_mutatingTabs) {
        return;
    }

    m_processingPageOperations = true;
    int processedOperations = 0;
    while (!m_destroying) {
        if (m_pageOperationDrainDeferred)
            break;
        if (processedOperations >= maxSynchronousPageOperations) {
            schedulePendingPageOperations();
            break;
        }
        if (m_rebuildPending) {
            m_rebuildPending = false;
            if (!m_rebuildTransactionActive) {
                m_rebuildTransactionActive = true;
                m_transactionFocusCancelled = false;
            }
            m_transactionPreferredPageKey = std::exchange(m_pendingPreferredPageKey, {});
            m_transactionPageFocusState = std::exchange(m_pendingPageFocusState, {});
            if (m_transactionFocusCancelled)
                m_transactionPageFocusState.active = false;
            rebuildPagesWithPreferredKey(
                m_transactionPreferredPageKey, m_transactionPageFocusState);
            ++processedOperations;
            continue;
        }
        m_rebuildTransactionActive = false;
        m_transactionFocusCancelled = false;
        m_transactionPreferredPageKey.clear();
        m_transactionPageFocusState = {};
        if (m_refreshPending) {
            m_refreshPending = false;
            m_refreshInProgress = true;
            refreshPageContentsNow();
            m_refreshInProgress = false;
            ++processedOperations;
            continue;
        }
        break;
    }
    if (!m_rebuildPending) {
        m_rebuildTransactionActive = false;
        m_transactionFocusCancelled = false;
        m_transactionPreferredPageKey.clear();
        m_transactionPageFocusState = {};
    }
    m_refreshInProgress = false;
    m_processingPageOperations = false;
}

void DetailsView::schedulePendingPageOperations()
{
    if (m_destroying || m_pageOperationDrainPosted)
        return;
    m_pageOperationDrainPosted = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_pageOperationDrainPosted = false;
            m_pageOperationDrainDeferred = false;
            processPendingPageOperations();
        },
        Qt::QueuedConnection);
}

void DetailsView::rebuildPagesWithPreferredKey(
    const QString &preferredPageKey, PageFocusState focusState)
{
    const quint64 rebuildGeneration = m_rebuildGeneration;
    const Core::PropertyPageContext context = m_context;
    clearPages();
    if (m_rebuildGeneration != rebuildGeneration)
        return;
    if (focusState.active) {
        QWidget *focusWidget = QApplication::focusWidget();
        if (focusWidget && focusWidget != m_tabs && !m_tabs->isAncestorOf(focusWidget)) {
            focusState.active = false;
            cancelTransactionFocusRestore();
        } else {
            setFocusInternally(m_tabs);
        }
    }

    if (context.nodeKind == Core::WorkbenchNodeKind::None) {
        m_title->setText(Tr::tr("EtherCAT Workbench"));
        m_title->setAccessibleName(m_title->text());
        updateEmptyState();
        m_emptyState->show();
        if (focusState.active)
            setFocusInternally(m_emptyState);
        m_tabs->hide();
        return;
    }
    m_title->setText(context.displayName);
    m_title->setAccessibleName(m_title->text());

    QList<PageCandidate> candidates;
    if (m_controller && m_controller->providerRegistry()) {
        QList<QPointer<Core::Provider>> providerObjects;
        for (Core::Provider *provider :
             m_controller->providerRegistry()->providers(Core::ProviderKind::PropertyPage)) {
            providerObjects.append(provider);
        }
        for (const QPointer<Core::Provider> &providerObject : std::as_const(providerObjects)) {
            if (m_rebuildGeneration != rebuildGeneration)
                return;
            QPointer<Core::PropertyPageProvider> provider
                = qobject_cast<Core::PropertyPageProvider *>(providerObject.data());
            if (!provider || m_departingPropertyPageProviderIds.contains(provider->id())
                || !provider->isAvailable()) {
                continue;
            }
            const Utils::Id providerId = provider->id();
            const QList<Core::PropertyPageDescriptor> descriptors = provider->pages(context);
            if (m_rebuildGeneration != rebuildGeneration || !provider
                || m_departingPropertyPageProviderIds.contains(providerId)) {
                return;
            }
            QSet<Utils::Id> pageIds;
            for (const Core::PropertyPageDescriptor &descriptor : descriptors) {
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
        if (!left.provider)
            return false;
        if (!right.provider)
            return true;
        const int providerOrder = left.provider->id()
                                      .toString()
                                      .compare(right.provider->id().toString(), Qt::CaseInsensitive);
        if (providerOrder != 0)
            return providerOrder < 0;
        return left.descriptor.id.toString() < right.descriptor.id.toString();
    });

    int restoredIndex = -1;
    for (const PageCandidate &candidate : std::as_const(candidates)) {
        if (m_rebuildGeneration != rebuildGeneration)
            return;
        QPointer<Core::PropertyPageProvider> provider = candidate.provider;
        if (!provider || m_departingPropertyPageProviderIds.contains(provider->id())
            || !provider->isAvailable()) {
            continue;
        }
        const Utils::Id providerId = provider->id();
        QWidget *page = provider->createPage(candidate.descriptor.id, m_tabs);
        QPointer<QWidget> guardedPage(page);
        if (m_rebuildGeneration != rebuildGeneration || !provider
            || m_departingPropertyPageProviderIds.contains(providerId)) {
            delete guardedPage.data();
            return;
        }
        if (!page)
            continue;
        const QString pageKey = providerId.toString() + '/' + candidate.descriptor.id.toString();
        page->setProperty("EtherCAT.PageKey", pageKey);
        if (m_rebuildGeneration != rebuildGeneration || !provider || !guardedPage
            || m_departingPropertyPageProviderIds.contains(providerId)) {
            delete guardedPage.data();
            return;
        }
        m_pages.append({provider, candidate.descriptor.id, page});
        {
            const QScopedValueRollback activePageCallback(
                m_activePageCallbackWidget, guardedPage);
            provider->updatePage(candidate.descriptor.id, page, context);
        }
        deleteDeferredProviderPages();
        if (m_rebuildGeneration != rebuildGeneration || !provider || !guardedPage
            || m_departingPropertyPageProviderIds.contains(providerId)) {
            discardPage(guardedPage);
            return;
        }
        int index = -1;
        {
            const QSignalBlocker blocker(m_tabs);
            const QScopedValueRollback activePageCallback(
                m_activePageCallbackWidget, guardedPage);
            index = m_tabs->addTab(page, candidate.descriptor.displayName);
        }
        deleteDeferredProviderPages();
        if (m_rebuildGeneration != rebuildGeneration || !provider || !guardedPage
            || m_departingPropertyPageProviderIds.contains(providerId)) {
            discardPage(guardedPage);
            return;
        }
        if (pageKey == preferredPageKey)
            restoredIndex = index;
    }

    if (restoredIndex >= 0) {
        const QSignalBlocker blocker(m_tabs);
        m_tabs->setCurrentIndex(restoredIndex);
    }
    if (m_rebuildGeneration != rebuildGeneration)
        return;
    const bool havePages = !m_pages.isEmpty();
    if (havePages) {
        m_tabs->show();
        m_emptyState->hide();
    } else {
        m_emptyState->show();
        if (focusState.active)
            setFocusInternally(m_emptyState);
        m_tabs->hide();
    }
    if (m_rebuildGeneration != rebuildGeneration)
        return;
    updateNoPageState();
    if (havePages)
        restorePageFocus(focusState);
}

DetailsView::PageFocusState DetailsView::currentPageFocusState() const
{
    QWidget *page = m_tabs->currentWidget();
    QWidget *focusWidget = QApplication::focusWidget();
    if (!page || !focusWidget
        || (focusWidget != page && !page->isAncestorOf(focusWidget))) {
        return {};
    }

    PageFocusState result;
    result.pageKey = page->property("EtherCAT.PageKey").toString();
    result.active = !result.pageKey.isEmpty();
    for (QWidget *widget = focusWidget; widget; widget = widget->parentWidget()) {
        if (!widget->objectName().isEmpty()) {
            result.objectName = widget->objectName();
            break;
        }
        if (widget == page)
            break;
    }
    return result;
}

void DetailsView::cancelTransactionFocusRestore()
{
    m_transactionFocusCancelled = true;
    m_transactionPageFocusState.active = false;
    if (m_rebuildPending)
        m_pendingPageFocusState.active = false;
}

void DetailsView::setFocusInternally(QWidget *widget)
{
    if (!widget)
        return;
    const QScopedValueRollback internalFocusChange(m_internalFocusChange, true);
    widget->setFocus(Qt::OtherFocusReason);
}

void DetailsView::restorePageFocus(const PageFocusState &focusState)
{
    if (!focusState.active || focusState.contextNodeId != m_context.nodeId
        || focusState.rebuildGeneration != m_rebuildGeneration) {
        return;
    }

    QWidget *currentFocusWidget = QApplication::focusWidget();
    if (currentFocusWidget && currentFocusWidget != m_tabs
        && !m_tabs->isAncestorOf(currentFocusWidget)) {
        cancelTransactionFocusRestore();
        return;
    }

    QWidget *page = m_tabs->currentWidget();
    if (!page)
        return;

    if (page->property("EtherCAT.PageKey").toString() != focusState.pageKey) {
        setFocusInternally(m_tabs);
        return;
    }

    QList<QWidget *> candidates;
    const auto appendCandidate = [&candidates](QWidget *widget) {
        if (widget && widget->isVisible() && widget->isEnabled()
            && (widget->focusPolicy() & Qt::TabFocus)) {
            candidates.append(widget);
        }
    };
    if (!focusState.objectName.isEmpty()) {
        if (page->objectName() == focusState.objectName)
            appendCandidate(page);
        for (QWidget *widget :
             page->findChildren<QWidget *>(focusState.objectName, Qt::FindChildrenRecursively)) {
            appendCandidate(widget);
        }
    }

    QWidget *focusTarget = candidates.size() == 1 ? candidates.constFirst() : nullptr;
    if (!focusTarget) {
        QSet<QWidget *> visited;
        for (QWidget *widget = page->nextInFocusChain(); widget && widget != page;
             widget = widget->nextInFocusChain()) {
            if (visited.contains(widget))
                break;
            visited.insert(widget);
            if (page->isAncestorOf(widget) && widget->isVisible() && widget->isEnabled()
                && (widget->focusPolicy() & Qt::TabFocus)) {
                focusTarget = widget;
                break;
            }
        }
        if (!focusTarget && page->isVisible() && page->isEnabled()
            && (page->focusPolicy() & Qt::TabFocus)) {
            focusTarget = page;
        }
    }
    if (!focusTarget)
        focusTarget = m_tabs;
    setFocusInternally(focusTarget);
}

void DetailsView::updateEmptyState()
{
    const bool haveProjects = m_controller && m_controller->projectService()
                              && !m_controller->projectService()->projects().isEmpty();
    m_emptyState->setText(
        haveProjects
            ? Tr::tr("Select an EtherCAT node in the tree to inspect its offline details.")
            : Tr::tr("No EtherCAT project is open. Create or open an EtherCAT project "
                     "(.ecatproject), or select Device Repository to inspect local ESI "
                     "descriptions."));
    m_emptyState->setAccessibleDescription(m_emptyState->text());
}

void DetailsView::updateNoPageState()
{
    if (!m_pages.isEmpty() || m_context.nodeKind == Core::WorkbenchNodeKind::None)
        return;
    m_emptyState->setText(m_context.nodeKind == Core::WorkbenchNodeKind::Diagnostics
                              ? diagnosticsEmptyState(
                                    m_controller
                                        ? m_controller->diagnosticsProviderPresentation()
                                        : OptionalProviderPresentation())
                              : Tr::tr("No property page provider supports this node."));
    m_emptyState->setAccessibleDescription(m_emptyState->text());
}

void DetailsView::refreshPageContents()
{
    if (m_destroying || m_refreshInProgress)
        return;
    m_refreshPending = true;
    processPendingPageOperations();
}

void DetailsView::refreshPageContentsNow()
{
    if (m_controller && !m_context.nodeId.isNull()) {
        const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
            m_context.nodeId);
        if (current.nodeKind != Core::WorkbenchNodeKind::None) {
            m_context = current;
            m_title->setText(m_context.displayName);
            m_title->setAccessibleName(m_title->text());
        }
    }
    const quint64 rebuildGeneration = m_rebuildGeneration;
    const Core::PropertyPageContext context = m_context;
    const QList<PageEntry> pages = m_pages;
    for (const PageEntry &entry : pages) {
        if (m_rebuildGeneration != rebuildGeneration)
            return;
        QPointer<Core::PropertyPageProvider> provider = entry.provider;
        QPointer<QWidget> widget = entry.widget;
        if (provider && widget && !m_departingPropertyPageProviderIds.contains(provider->id())) {
            {
                const QScopedValueRollback activePageCallback(
                    m_activePageCallbackWidget, widget);
                provider->updatePage(entry.pageId, widget, context);
            }
            deleteDeferredProviderPages();
        }
        if (m_rebuildGeneration != rebuildGeneration)
            return;
    }
}

void DetailsView::discardPage(const QPointer<QWidget> &page)
{
    const QScopedValueRollback tabMutation(m_mutatingTabs, true);
    {
        const QSignalBlocker blocker(m_tabs);
        for (qsizetype index = m_pages.size() - 1; index >= 0; --index) {
            const QPointer<QWidget> candidate = m_pages.at(index).widget;
            if (!candidate || (page && candidate == page))
                m_pages.removeAt(index);
        }
        if (page) {
            const int tabIndex = m_tabs->indexOf(page);
            if (tabIndex >= 0)
                m_tabs->removeTab(tabIndex);
        }
    }
    delete page.data();
}

void DetailsView::clearPages()
{
    if (m_clearingPages)
        return;
    m_clearingPages = true;
    m_pagesPendingDeletion.append(m_pages);
    m_pages.clear();
    {
        const QSignalBlocker blocker(m_tabs);
        while (m_tabs->count() > 0) {
            QPointer<QWidget> page = m_tabs->widget(0);
            {
                const QScopedValueRollback activePageCallback(
                    m_activePageCallbackWidget, page);
                m_tabs->removeTab(0);
            }
            deleteDeferredProviderPages();
            const auto knownPage = std::find_if(
                m_pagesPendingDeletion.cbegin(),
                m_pagesPendingDeletion.cend(),
                [page](const PageEntry &entry) { return entry.widget == page; });
            if (page && knownPage == m_pagesPendingDeletion.cend())
                m_pagesPendingDeletion.append({{}, {}, page});
        }
    }
    while (!m_pagesPendingDeletion.isEmpty()) {
        const QPointer<QWidget> page = m_pagesPendingDeletion.takeFirst().widget;
        if (page && page == m_activePageCallbackWidget)
            m_deferredProviderPageDeletes.append(page);
        else
            delete page.data();
    }
    m_clearingPages = false;
}

void DetailsView::removePagesForProvider(Core::Provider *provider)
{
    const QScopedValueRollback tabMutation(m_mutatingTabs, true);
    QList<PageEntry> entriesToDelete;
    for (qsizetype index = m_pages.size() - 1; index >= 0; --index) {
        const PageEntry entry = m_pages.at(index);
        if (entry.provider != provider)
            continue;
        m_pages.removeAt(index);
        entriesToDelete.prepend(entry);
    }
    for (qsizetype index = m_pagesPendingDeletion.size() - 1; index >= 0; --index) {
        const PageEntry entry = m_pagesPendingDeletion.at(index);
        if (entry.provider != provider)
            continue;
        m_pagesPendingDeletion.removeAt(index);
        entriesToDelete.prepend(entry);
    }
    {
        const QSignalBlocker blocker(m_tabs);
        for (const PageEntry &entry : std::as_const(entriesToDelete)) {
            if (!entry.widget || entry.widget == m_activePageCallbackWidget)
                continue;
            const int tabIndex = m_tabs->indexOf(entry.widget);
            if (tabIndex >= 0)
                m_tabs->removeTab(tabIndex);
        }
    }
    for (const PageEntry &entry : std::as_const(entriesToDelete)) {
        const QPointer<QWidget> page = entry.widget;
        if (page && page == m_activePageCallbackWidget)
            m_deferredProviderPageDeletes.append(page);
        else
            delete page.data();
    }
}

void DetailsView::deleteDeferredProviderPages()
{
    if (m_activePageCallbackWidget)
        return;
    const QList<QPointer<QWidget>> pagesToDelete
        = std::exchange(m_deferredProviderPageDeletes, {});
    for (const QPointer<QWidget> &page : pagesToDelete)
        delete page.data();
}

void DetailsView::handleProviderRemoving(Core::Provider *provider)
{
    if (m_destroying || provider->kind() != Core::ProviderKind::PropertyPage)
        return;
    m_departingPropertyPageProviderIds.insert(provider->id());
    disconnect(provider, &Core::Provider::availabilityChanged, this, &DetailsView::rebuildPages);
    m_pageOperationDrainDeferred = true;
    rebuildPages();
    removePagesForProvider(provider);
    schedulePendingPageOperations();
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
