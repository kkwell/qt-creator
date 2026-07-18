// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QTabWidget;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class DetailsView final : public QWidget
{
    Q_OBJECT

public:
    explicit DetailsView(WorkbenchController *controller, QWidget *parent = nullptr);
    ~DetailsView() final;

    Core::PropertyPageContext currentContext() const;
    QTabWidget *tabWidget() const;

private:
    struct PageEntry
    {
        QPointer<Core::PropertyPageProvider> provider;
        Utils::Id pageId;
        QPointer<QWidget> widget;
    };

    struct PageFocusState
    {
        Data::NodeId contextNodeId;
        QString pageKey;
        QString objectName;
        quint64 rebuildGeneration = 0;
        bool active = false;
    };

    void setCurrentNode(const Data::NodeId &nodeId);
    void rebuildPages();
    void requestPageRebuild(const QString &preferredPageKey, PageFocusState focusState);
    void processPendingPageOperations();
    void schedulePendingPageOperations();
    void rebuildPagesWithPreferredKey(
        const QString &preferredPageKey, PageFocusState focusState);
    PageFocusState currentPageFocusState() const;
    void cancelTransactionFocusRestore();
    void setFocusInternally(QWidget *widget);
    void restorePageFocus(const PageFocusState &focusState);
    void updateEmptyState();
    void updateNoPageState();
    void refreshPageContents();
    void refreshPageContentsNow();
    void discardPage(const QPointer<QWidget> &page);
    void clearPages();
    void removePagesForProvider(Core::Provider *provider);
    void deleteDeferredProviderPages();
    void handleProviderRemoving(Core::Provider *provider);
    void watchPropertyPageProvider(Core::Provider *provider);

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_title = nullptr;
    QLabel *m_emptyState = nullptr;
    QTabWidget *m_tabs = nullptr;
    QList<PageEntry> m_pages;
    QList<PageEntry> m_pagesPendingDeletion;
    QList<QPointer<QWidget>> m_deferredProviderPageDeletes;
    QPointer<QWidget> m_activePageCallbackWidget;
    QSet<Utils::Id> m_departingPropertyPageProviderIds;
    QString m_pendingPreferredPageKey;
    PageFocusState m_pendingPageFocusState;
    QString m_transactionPreferredPageKey;
    PageFocusState m_transactionPageFocusState;
    bool m_clearingPages = false;
    bool m_destroying = false;
    bool m_internalFocusChange = false;
    bool m_pageOperationDrainDeferred = false;
    bool m_pageOperationDrainPosted = false;
    bool m_processingPageOperations = false;
    bool m_rebuildPending = false;
    bool m_rebuildTransactionActive = false;
    bool m_transactionFocusCancelled = false;
    bool m_mutatingTabs = false;
    bool m_refreshInProgress = false;
    bool m_refreshPending = false;
    quint64 m_rebuildGeneration = 0;
};

} // namespace EtherCAT::Workbench::Internal
