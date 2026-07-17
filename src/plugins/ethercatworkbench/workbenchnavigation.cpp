// Copyright (C) 2026 Kvell

#include "workbenchnavigation.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>

#include <ethercatcore/selectionservice.h>

#include <utils/stylehelper.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
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
    m_filterEdit->setAccessibleName(Tr::tr("Filter EtherCAT nodes"));
    m_filterEdit->setAccessibleDescription(
        Tr::tr("Filter the offline EtherCAT tree by node, status, or identity."));
    m_filterEdit->setPlaceholderText(Tr::tr("Filter nodes, status, or identity"));
    m_filterEdit->setClearButtonEnabled(true);

    m_proxyModel->setSourceModel(m_sourceModel);
    m_proxyModel->setFilterRole(WorkbenchTreeModel::SearchTextRole);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setRecursiveFilteringEnabled(true);
    m_proxyModel->setAutoAcceptChildRows(true);

    m_treeView->setObjectName("EtherCATWorkbenchTree");
    m_treeView->setAccessibleName(Tr::tr("EtherCAT device tree"));
    m_treeView->setAccessibleDescription(
        Tr::tr("Browse offline projects, masters, slaves, process data, and ESI devices. Drag a "
               "supported ESI device to the active offline Master to append it."));
    m_treeView->setModel(m_proxyModel);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setTextElideMode(Qt::ElideNone);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setDragEnabled(true);
    m_treeView->viewport()->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeView->setDefaultDropAction(Qt::CopyAction);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setHeaderHidden(false);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    setFocusProxy(m_treeView);

    m_emptyState = new QWidget(this);
    m_emptyState->setObjectName("EtherCATWorkbenchFilterEmptyState");
    m_emptyState->setAccessibleName(Tr::tr("No matching EtherCAT nodes"));
    m_emptyState->setAccessibleDescription(
        Tr::tr("No offline EtherCAT tree nodes match the current filter."));

    auto emptyMessage = new QLabel(
        Tr::tr("No EtherCAT nodes match the current filter."), m_emptyState);
    emptyMessage->setObjectName("EtherCATWorkbenchFilterEmptyMessage");
    emptyMessage->setAccessibleName(Tr::tr("No matching EtherCAT nodes"));
    emptyMessage->setAlignment(Qt::AlignCenter);
    emptyMessage->setWordWrap(true);

    m_clearFilter = new QPushButton(Tr::tr("Clear Filter"), m_emptyState);
    m_clearFilter->setObjectName("EtherCATWorkbenchClearFilter");
    m_clearFilter->setAccessibleDescription(
        Tr::tr("Clear the navigation filter and return to the offline EtherCAT tree."));

    auto emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    emptyLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyMessage);
    emptyLayout->addWidget(m_clearFilter, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();

    m_resultsStack = new QStackedWidget(this);
    m_resultsStack->setObjectName("EtherCATWorkbenchNavigationResults");
    m_resultsStack->addWidget(m_treeView);
    m_resultsStack->addWidget(m_emptyState);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVXxs);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_resultsStack);

    connect(m_filterEdit, &QLineEdit::textChanged, m_proxyModel, [this](const QString &text) {
        m_proxyModel->setFilterFixedString(text);
        if (!text.isEmpty())
            m_treeView->expandAll();
        updateFilterState();
    });
    connect(m_proxyModel, &QAbstractItemModel::rowsInserted, this, [this] {
        updateFilterState();
    });
    connect(m_proxyModel, &QAbstractItemModel::rowsRemoved, this, [this] {
        updateFilterState();
    });
    connect(m_proxyModel, &QAbstractItemModel::modelReset, this, [this] {
        updateFilterState();
    });
    connect(m_proxyModel, &QAbstractItemModel::layoutChanged, this, [this] {
        updateFilterState();
    });
    connect(m_proxyModel, &QAbstractItemModel::dataChanged, this, [this] {
        updateFilterState();
    });
    connect(m_clearFilter, &QPushButton::clicked, this, [this] {
        const Data::NodeId currentNodeId
            = m_controller && m_controller->selectionService()
                  ? m_controller->selectionService()->currentNodeId()
                  : Data::NodeId{};
        m_filterEdit->clear();
        m_treeView->setFocus(Qt::ShortcutFocusReason);
        selectNode(currentNodeId);
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
        controller,
        &WorkbenchController::locateFirstTopologyDifferenceRequested,
        this,
        &WorkbenchNavigationWidget::locateFirstTopologyDifference);
    connect(
        controller,
        &WorkbenchController::locateFirstIssueRequested,
        this,
        &WorkbenchNavigationWidget::locateFirstIssue);
    connect(
        controller,
        &WorkbenchController::openDiagnosticsRequested,
        this,
        &WorkbenchNavigationWidget::openDiagnostics);
    connect(
        controller,
        &WorkbenchController::locateUnsupportedDeviceRequested,
        this,
        &WorkbenchNavigationWidget::locateUnsupportedDevice);
    connect(
        controller,
        &WorkbenchController::copyCurrentNodeIdRequested,
        this,
        &WorkbenchNavigationWidget::copyCurrentNodeId);
    connect(
        m_treeView,
        &QTreeView::customContextMenuRequested,
        this,
        &WorkbenchNavigationWidget::showContextMenu);
    m_treeView->expandToDepth(2);
    updateFilterState();
}

QTreeView *WorkbenchNavigationWidget::treeView() const
{
    return m_treeView;
}

QLineEdit *WorkbenchNavigationWidget::filterEdit() const
{
    return m_filterEdit;
}

void WorkbenchNavigationWidget::updateFilterState()
{
    const bool noMatches
        = !m_filterEdit->text().isEmpty() && m_proxyModel->rowCount() == 0;
    m_resultsStack->setCurrentWidget(noMatches ? m_emptyState : m_treeView);
    setFocusProxy(noMatches ? static_cast<QWidget *>(m_clearFilter)
                            : static_cast<QWidget *>(m_treeView));
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

void WorkbenchNavigationWidget::selectSourceIndex(const QModelIndex &sourceIndex)
{
    if (!sourceIndex.isValid())
        return;
    m_filterEdit->clear();
    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
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

void WorkbenchNavigationWidget::locateFirstTopologyDifference()
{
    selectSourceIndex(m_sourceModel->firstTopologyDifference());
}

void WorkbenchNavigationWidget::locateFirstIssue()
{
    selectSourceIndex(m_sourceModel->firstIssue());
}

void WorkbenchNavigationWidget::openDiagnostics()
{
    const Core::PropertyPageContext current = m_sourceModel->contextForIndex(
        m_proxyModel->mapToSource(m_treeView->currentIndex()));
    QModelIndex diagnostics = m_sourceModel->diagnosticsForProject(current.projectId);
    if (!diagnostics.isValid())
        diagnostics = m_sourceModel->diagnosticsForProject({});
    selectSourceIndex(diagnostics);
}

void WorkbenchNavigationWidget::showContextMenu(const QPoint &position)
{
    const QModelIndex proxyIndex = m_treeView->indexAt(position);
    if (proxyIndex.isValid())
        m_treeView->setCurrentIndex(proxyIndex);
    const Core::PropertyPageContext context = m_sourceModel->contextForIndex(
        m_proxyModel->mapToSource(m_treeView->currentIndex()));
    const auto setCommandEnabled = [](const Utils::Id &id, bool enabled) {
        if (::Core::Command *command = ::Core::ActionManager::command(id))
            command->action()->setEnabled(enabled);
    };
    setCommandEnabled(
        Constants::LOCATE_DIFFERENCE_ACTION_ID, m_sourceModel->firstTopologyDifference().isValid());
    setCommandEnabled(Constants::LOCATE_ISSUE_ACTION_ID, m_sourceModel->firstIssue().isValid());
    setCommandEnabled(
        Constants::OPEN_DIAGNOSTICS_ACTION_ID,
        m_sourceModel->diagnosticsForProject(context.projectId).isValid()
            || m_sourceModel->diagnosticsForProject({}).isValid());
    setCommandEnabled(
        Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID,
        m_sourceModel->firstUnsupportedDevice().isValid());
    setCommandEnabled(
        Constants::COPY_NODE_ID_ACTION_ID,
        !context.nodeId.isNull() && context.nodeKind != Core::WorkbenchNodeKind::Placeholder);
    setCommandEnabled(
        Constants::INSERT_DEVICE_ACTION_ID,
        m_controller && m_controller->canInsertDeviceOnSelectedMaster());
    setCommandEnabled(
        Constants::ADD_DEVICE_TO_MASTER_ACTION_ID,
        m_controller && m_controller->canAddSelectedDeviceToMaster());
    setCommandEnabled(
        Constants::REMOVE_OFFLINE_SLAVE_ACTION_ID,
        m_controller && m_controller->canRemoveSelectedOfflineSlave());
    setCommandEnabled(
        Constants::MOVE_OFFLINE_SLAVE_UP_ACTION_ID,
        m_controller && m_controller->canMoveSelectedOfflineSlaveUp());
    setCommandEnabled(
        Constants::MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID,
        m_controller && m_controller->canMoveSelectedOfflineSlaveDown());

    QMenu menu(this);
    const auto addCommand = [&menu](const Utils::Id &id) {
        if (::Core::Command *command = ::Core::ActionManager::command(id))
            menu.addAction(command->action());
    };
    addCommand(Constants::EXPAND_ACTION_ID);
    addCommand(Constants::COLLAPSE_ACTION_ID);
    menu.addSeparator();
    for (const Utils::Id id :
         {Utils::Id(Constants::LOCATE_DIFFERENCE_ACTION_ID),
          Utils::Id(Constants::LOCATE_ISSUE_ACTION_ID),
          Utils::Id(Constants::OPEN_DIAGNOSTICS_ACTION_ID),
          Utils::Id(Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID)}) {
        addCommand(id);
    }
    if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
        menu.addSeparator();
        addCommand(Constants::INSERT_DEVICE_ACTION_ID);
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        menu.addSeparator();
        addCommand(Constants::ADD_DEVICE_TO_MASTER_ACTION_ID);
    } else if (context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave) {
        menu.addSeparator();
        addCommand(Constants::REMOVE_OFFLINE_SLAVE_ACTION_ID);
        addCommand(Constants::MOVE_OFFLINE_SLAVE_UP_ACTION_ID);
        addCommand(Constants::MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID);
    }
    if (!context.nodeId.isNull() && context.nodeKind != Core::WorkbenchNodeKind::Placeholder) {
        menu.addSeparator();
        addCommand(Constants::COPY_NODE_ID_ACTION_ID);
    }
    menu.exec(m_treeView->viewport()->mapToGlobal(position));
}

void WorkbenchNavigationWidget::locateUnsupportedDevice()
{
    selectSourceIndex(m_sourceModel->firstUnsupportedDevice());
}

void WorkbenchNavigationWidget::copyCurrentNodeId()
{
    const Core::PropertyPageContext context = m_sourceModel->contextForIndex(
        m_proxyModel->mapToSource(m_treeView->currentIndex()));
    if (!context.nodeId.isNull())
        QApplication::clipboard()->setText(context.nodeId.toString());
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
    if (::Core::Command *command = ::Core::ActionManager::command(Constants::EXPAND_ACTION_ID))
        expandButton->setDefaultAction(command->action());

    auto collapseButton = new QToolButton;
    if (::Core::Command *command = ::Core::ActionManager::command(Constants::COLLAPSE_ACTION_ID))
        collapseButton->setDefaultAction(command->action());
    return {widget, {expandButton, collapseButton}};
}

WorkbenchNavigationWidget *WorkbenchNavigationFactory::lastCreatedWidget() const
{
    return m_lastCreatedWidget;
}

} // namespace EtherCAT::Workbench::Internal
