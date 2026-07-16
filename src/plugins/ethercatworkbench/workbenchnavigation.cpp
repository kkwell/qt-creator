// Copyright (C) 2026 Kvell

#include "workbenchnavigation.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <ethercatcore/selectionservice.h>

#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

WorkbenchNavigationWidget::WorkbenchNavigationWidget(
    WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_sourceModel(controller->treeModel())
    , m_proxyModel(new QSortFilterProxyModel(this))
    , m_filterEdit(new QLineEdit(this))
    , m_treeView(new QTreeView(this))
{
    setObjectName("EtherCATWorkbenchNavigation");
    m_filterEdit->setObjectName("EtherCATWorkbenchFilter");
    m_filterEdit->setPlaceholderText(Tr::tr("Filter nodes, status, or identity"));
    m_filterEdit->setClearButtonEnabled(true);

    m_proxyModel->setSourceModel(m_sourceModel);
    m_proxyModel->setFilterRole(WorkbenchTreeModel::SearchTextRole);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setRecursiveFilteringEnabled(true);
    m_proxyModel->setAutoAcceptChildRows(true);

    m_treeView->setObjectName("EtherCATWorkbenchTree");
    m_treeView->setModel(m_proxyModel);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVXxs);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_treeView);

    connect(m_filterEdit, &QLineEdit::textChanged, m_proxyModel, [this](const QString &text) {
        m_proxyModel->setFilterFixedString(text);
        if (!text.isEmpty())
            m_treeView->expandAll();
    });
    connect(
        m_treeView->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        [this](const QModelIndex &current) {
            if (!m_controller || !m_controller->selectionService())
                return;
            const QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
            const Core::PropertyPageContext context = m_sourceModel->contextForIndex(sourceIndex);
            if (context.nodeKind == Core::WorkbenchNodeKind::Placeholder)
                return;
            m_controller->selectionService()->setCurrentNodeId(context.nodeId);
        });
    connect(
        controller->selectionService(),
        &Core::SelectionService::currentNodeChanged,
        this,
        [this](const Data::NodeId &current) { selectNode(current); });
    connect(
        m_sourceModel,
        &QAbstractItemModel::modelReset,
        this,
        [this] {
            if (m_controller && m_controller->selectionService())
                selectNode(m_controller->selectionService()->currentNodeId());
            m_treeView->expandToDepth(2);
        });
    connect(
        controller,
        &WorkbenchController::expandAllRequested,
        m_treeView,
        &QTreeView::expandAll);
    connect(
        controller,
        &WorkbenchController::collapseAllRequested,
        m_treeView,
        &QTreeView::collapseAll);
    connect(
        m_treeView,
        &QTreeView::customContextMenuRequested,
        this,
        &WorkbenchNavigationWidget::showContextMenu);
    m_treeView->expandToDepth(2);
}

QTreeView *WorkbenchNavigationWidget::treeView() const
{
    return m_treeView;
}

QLineEdit *WorkbenchNavigationWidget::filterEdit() const
{
    return m_filterEdit;
}

void WorkbenchNavigationWidget::selectNode(const Data::NodeId &nodeId)
{
    if (nodeId.isNull()) {
        m_treeView->clearSelection();
        m_treeView->setCurrentIndex({});
        return;
    }
    const QModelIndex sourceIndex = m_sourceModel->indexForNodeId(nodeId);
    QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid() && sourceIndex.isValid() && !m_filterEdit->text().isEmpty()) {
        m_filterEdit->clear();
        proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    }
    if (!proxyIndex.isValid())
        return;
    QModelIndex parent = proxyIndex.parent();
    while (parent.isValid()) {
        m_treeView->expand(parent);
        parent = parent.parent();
    }
    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex);
}

void WorkbenchNavigationWidget::showContextMenu(const QPoint &position)
{
    const QModelIndex proxyIndex = m_treeView->indexAt(position);
    if (proxyIndex.isValid())
        m_treeView->setCurrentIndex(proxyIndex);
    const Core::PropertyPageContext context = m_sourceModel->contextForIndex(
        m_proxyModel->mapToSource(m_treeView->currentIndex()));

    QMenu menu(this);
    QAction *expand = menu.addAction(Tr::tr("Expand All"));
    expand->setIcon(Utils::Icons::EXPAND_ALL_TOOLBAR.icon());
    connect(expand, &QAction::triggered, m_treeView, &QTreeView::expandAll);
    QAction *collapse = menu.addAction(Tr::tr("Collapse All"));
    collapse->setIcon(Utils::Icons::COLLAPSE_TOOLBAR.icon());
    connect(collapse, &QAction::triggered, m_treeView, &QTreeView::collapseAll);
    QAction *unsupported = menu.addAction(Tr::tr("Locate Unsupported Device"));
    unsupported->setEnabled(m_sourceModel->firstUnsupportedDevice().isValid());
    connect(
        unsupported,
        &QAction::triggered,
        this,
        &WorkbenchNavigationWidget::locateUnsupportedDevice);
    if (!context.nodeId.isNull()) {
        menu.addSeparator();
        QAction *copyId = menu.addAction(Tr::tr("Copy Node ID"));
        connect(copyId, &QAction::triggered, this, [context] {
            QApplication::clipboard()->setText(context.nodeId.toString());
        });
    }
    menu.exec(m_treeView->viewport()->mapToGlobal(position));
}

void WorkbenchNavigationWidget::locateUnsupportedDevice()
{
    m_filterEdit->clear();
    const QModelIndex sourceIndex = m_sourceModel->firstUnsupportedDevice();
    if (!sourceIndex.isValid())
        return;
    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex);
}

WorkbenchNavigationFactory::WorkbenchNavigationFactory(WorkbenchController *controller)
    : m_controller(controller)
{
    setId(Constants::NAVIGATION_ID);
    setDisplayName(Tr::tr("EtherCAT Devices"));
    setPriority(950);
}

::Core::NavigationView WorkbenchNavigationFactory::createWidget()
{
    auto widget = new WorkbenchNavigationWidget(m_controller);
    m_lastCreatedWidget = widget;

    auto expandButton = new QToolButton;
    expandButton->setIcon(Utils::Icons::EXPAND_ALL_TOOLBAR.icon());
    expandButton->setToolTip(Tr::tr("Expand All"));
    connect(expandButton, &QToolButton::clicked, widget->treeView(), &QTreeView::expandAll);

    auto collapseButton = new QToolButton;
    collapseButton->setIcon(Utils::Icons::COLLAPSE_TOOLBAR.icon());
    collapseButton->setToolTip(Tr::tr("Collapse All"));
    connect(collapseButton, &QToolButton::clicked, widget->treeView(), &QTreeView::collapseAll);
    return {widget, {expandButton, collapseButton}};
}

WorkbenchNavigationWidget *WorkbenchNavigationFactory::lastCreatedWidget() const
{
    return m_lastCreatedWidget;
}

} // namespace EtherCAT::Workbench::Internal
