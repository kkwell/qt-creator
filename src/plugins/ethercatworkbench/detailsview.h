// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
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

    void setCurrentNode(const Data::NodeId &nodeId);
    void rebuildPages();
    void updateEmptyState();
    void updateNoPageState();
    void refreshPageContents();
    void clearPages();
    void handleProviderRemoving(Core::Provider *provider);
    void watchPropertyPageProvider(Core::Provider *provider);

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_title = nullptr;
    QLabel *m_emptyState = nullptr;
    QTabWidget *m_tabs = nullptr;
    QList<PageEntry> m_pages;
};

} // namespace EtherCAT::Workbench::Internal
