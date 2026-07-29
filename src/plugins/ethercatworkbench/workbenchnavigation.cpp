// Copyright (C) 2026 Kvell

#include "workbenchnavigation.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/find/itemviewfind.h>
#include <coreplugin/icontext.h>

#include <ethercatcore/selectionservice.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/qtcsettings.h>
#include <utils/stylehelper.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <optional>

namespace EtherCAT::Workbench::Internal {

static const Utils::Key navigationHeaderStateKey(
    "EtherCAT/Workbench/NavigationHeaderState");

class WorkbenchItemViewFind final : public ::Core::ItemViewFind
{
public:
    using ItemViewFind::ItemViewFind;

    Utils::FindFlags supportedFindFlags() const final
    {
        return ItemViewFind::supportedFindFlags() & ~Utils::FindWholeWords;
    }
};

class WorkbenchNavigationFilterModel final : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
        const Core::WorkbenchNodeKind kind
            = sourceIndex.data(WorkbenchTreeModel::NodeKindRole)
                  .value<Core::WorkbenchNodeKind>();
        switch (kind) {
        case Core::WorkbenchNodeKind::Project:
        case Core::WorkbenchNodeKind::Master:
        case Core::WorkbenchNodeKind::ConfiguredSlave:
            break;
        case Core::WorkbenchNodeKind::Module:
            if (sourceParent.data(WorkbenchTreeModel::NodeKindRole)
                    .value<Core::WorkbenchNodeKind>()
                != Core::WorkbenchNodeKind::Master) {
                return false;
            }
            break;
        case Core::WorkbenchNodeKind::Placeholder:
            if (sourceParent.isValid())
                return false;
            break;
        default:
            return false;
        }
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }
};

static std::optional<Data::NodeId> currentLocateProjectId(
    WorkbenchController *controller, const WorkbenchTreeModel *model)
{
    if (!controller || !model)
        return std::nullopt;
    Core::SelectionService *selectionService = controller->selectionService();
    if (!selectionService)
        return std::nullopt;
    const Data::NodeId currentNodeId = selectionService->currentNodeId();
    const Core::PropertyPageContext context = model->contextForNodeId(currentNodeId);
    if (!currentNodeId.isNull() && context.nodeId.isNull())
        return std::nullopt;
    return context.projectId;
}

static QString projectTimingModeName(Data::MasterTimingMode mode)
{
    switch (mode) {
    case Data::MasterTimingMode::Unassigned:
        return Tr::tr("Not configured");
    case Data::MasterTimingMode::FreeRun:
        return Tr::tr("FreeRun");
    case Data::MasterTimingMode::DistributedClocks:
        return Tr::tr("Distributed Clocks");
    }
    return Tr::tr("Unknown");
}

static QString projectCyclePeriodText(quint32 cyclePeriodNs)
{
    if (!cyclePeriodNs)
        return Tr::tr("Not configured");
    if (cyclePeriodNs % 1'000'000 == 0)
        return Tr::tr("%1 ms").arg(cyclePeriodNs / 1'000'000);
    if (cyclePeriodNs % 1'000 == 0)
        return Tr::tr("%1 µs").arg(cyclePeriodNs / 1'000);
    return Tr::tr("%1 ns").arg(cyclePeriodNs);
}

WorkbenchNavigationWidget::WorkbenchNavigationWidget(
    WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_sourceModel(controller->treeModel())
    , m_proxyModel(new WorkbenchNavigationFilterModel(this))
    , m_filterEdit(new QLineEdit(this))
    , m_treeView(new QTreeView(this))
{
    setObjectName("EtherCATWorkbenchNavigation");
    ::Core::IContext::attach(this, ::Core::Context(Constants::CONTEXT_ID));
    m_filterEdit->setObjectName("EtherCATWorkbenchFilter");
    m_filterEdit->setAccessibleName(Tr::tr("Filter EtherCAT nodes"));
    m_filterEdit->setAccessibleDescription(
        Tr::tr("Filter the EtherCAT device tree by device, status, or identity."));
    m_filterEdit->setPlaceholderText(Tr::tr("Filter nodes, status, or identity"));
    m_filterEdit->setClearButtonEnabled(true);

    connect(
        m_sourceModel,
        &QAbstractItemModel::modelAboutToBeReset,
        this,
        &WorkbenchNavigationWidget::handleModelAboutToBeReset);
    m_proxyModel->setSourceModel(m_sourceModel);
    m_proxyModel->setFilterRole(WorkbenchTreeModel::SearchTextRole);
    m_proxyModel->setFilterKeyColumn(0);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setRecursiveFilteringEnabled(true);
    m_proxyModel->setAutoAcceptChildRows(false);
    const auto beginProxyChange = [this] { ++m_proxyModelChangeDepth; };
    const auto endProxyChange = [this] {
        if (m_proxyModelChangeDepth > 0)
            --m_proxyModelChangeDepth;
    };
    connect(
        m_proxyModel,
        &QAbstractItemModel::modelAboutToBeReset,
        this,
        beginProxyChange);
    connect(m_proxyModel, &QAbstractItemModel::modelReset, this, endProxyChange);
    connect(
        m_proxyModel,
        &QAbstractItemModel::layoutAboutToBeChanged,
        this,
        beginProxyChange);
    connect(m_proxyModel, &QAbstractItemModel::layoutChanged, this, endProxyChange);
    connect(
        m_proxyModel,
        &QAbstractItemModel::rowsAboutToBeInserted,
        this,
        beginProxyChange);
    connect(m_proxyModel, &QAbstractItemModel::rowsInserted, this, endProxyChange);
    connect(
        m_proxyModel,
        &QAbstractItemModel::rowsAboutToBeRemoved,
        this,
        beginProxyChange);
    connect(m_proxyModel, &QAbstractItemModel::rowsRemoved, this, endProxyChange);
    connect(
        m_proxyModel,
        &QAbstractItemModel::rowsAboutToBeMoved,
        this,
        beginProxyChange);
    connect(m_proxyModel, &QAbstractItemModel::rowsMoved, this, endProxyChange);

    m_treeView->setObjectName("EtherCATWorkbenchTree");
    m_treeView->setAccessibleName(Tr::tr("EtherCAT device tree"));
    m_treeView->setAccessibleDescription(
        Tr::tr(
            "Browse projects, EtherCAT Masters, and the configured or detected bus devices. "
            "Select a node to inspect and configure it in the right panel."));
    m_treeView->setModel(m_proxyModel);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setTextElideMode(Qt::ElideRight);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setDragEnabled(true);
    m_treeView->viewport()->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeView->setDefaultDropAction(Qt::CopyAction);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->installEventFilter(this);
    m_treeView->viewport()->installEventFilter(this);
    m_treeView->setHeaderHidden(false);
    QHeaderView *header = m_treeView->header();
    header->setStretchLastSection(true);
    header->setSectionsMovable(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    const QByteArray headerState
        = Utils::userSettings().value(navigationHeaderStateKey).toByteArray();
    if (!headerState.isEmpty())
        header->restoreState(headerState);
    connect(header, &QHeaderView::sectionResized, this, [header] {
        Utils::userSettings().setValue(navigationHeaderStateKey, header->saveState());
    });
    setFocusProxy(m_treeView);

    auto findSupport = new WorkbenchItemViewFind(
        m_treeView, WorkbenchTreeModel::SearchTextRole);
    const auto resetIncrementalSearch = [findSupport] {
        findSupport->resetIncrementalSearch();
    };
    connect(
        m_proxyModel,
        &QAbstractItemModel::modelAboutToBeReset,
        findSupport,
        resetIncrementalSearch);
    connect(
        m_proxyModel,
        &QAbstractItemModel::rowsAboutToBeInserted,
        findSupport,
        resetIncrementalSearch);
    connect(
        m_proxyModel,
        &QAbstractItemModel::rowsAboutToBeRemoved,
        findSupport,
        resetIncrementalSearch);
    connect(
        m_proxyModel,
        &QAbstractItemModel::rowsAboutToBeMoved,
        findSupport,
        resetIncrementalSearch);
    connect(
        m_sourceModel,
        &QAbstractItemModel::rowsAboutToBeMoved,
        findSupport,
        resetIncrementalSearch);
    connect(
        m_proxyModel,
        &QAbstractItemModel::layoutAboutToBeChanged,
        findSupport,
        resetIncrementalSearch);
    m_treeResults = ::Core::ItemViewFind::createSearchableWrapper(findSupport);
    m_treeResults->setObjectName("EtherCATWorkbenchSearchableTree");

    m_emptyState = new QWidget(this);
    m_emptyState->setObjectName("EtherCATWorkbenchFilterEmptyState");
    m_emptyState->setAccessibleName(Tr::tr("No matching EtherCAT nodes"));
    m_emptyState->setAccessibleDescription(
        Tr::tr("No EtherCAT device tree nodes match the current filter."));

    auto emptyMessage = new QLabel(
        Tr::tr("No EtherCAT nodes match the current filter."), m_emptyState);
    emptyMessage->setObjectName("EtherCATWorkbenchFilterEmptyMessage");
    emptyMessage->setAccessibleName(Tr::tr("No matching EtherCAT nodes"));
    emptyMessage->setAlignment(Qt::AlignCenter);
    emptyMessage->setWordWrap(true);

    m_clearFilter = new QPushButton(Tr::tr("Clear Filter"), m_emptyState);
    m_clearFilter->setObjectName("EtherCATWorkbenchClearFilter");
    m_clearFilter->setAccessibleDescription(
        Tr::tr("Clear the navigation filter and return to the EtherCAT device tree."));

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
    m_resultsStack->addWidget(m_treeResults);
    m_resultsStack->addWidget(m_emptyState);

    m_projectSummary = new QGroupBox(Tr::tr("Project"), this);
    m_projectSummary->setObjectName("EtherCATWorkbenchProjectSummary");
    m_projectSummary->setAccessibleName(Tr::tr("Current project information"));
    m_projectSummary->setAccessibleDescription(
        Tr::tr("Current project timing mode, cycle period, and configured device count."));
    m_projectSummaryName = new QLabel(m_projectSummary);
    m_projectSummaryName->setObjectName("EtherCATWorkbenchProjectSummaryName");
    m_projectSummaryMode = new QLabel(m_projectSummary);
    m_projectSummaryMode->setObjectName("EtherCATWorkbenchProjectSummaryMode");
    m_projectSummaryCycle = new QLabel(m_projectSummary);
    m_projectSummaryCycle->setObjectName("EtherCATWorkbenchProjectSummaryCycle");
    m_projectSummaryDevices = new QLabel(m_projectSummary);
    m_projectSummaryDevices->setObjectName("EtherCATWorkbenchProjectSummaryDevices");
    const auto configureSummaryValue = [](QLabel *label, const QString &accessibleName) {
        label->setAccessibleName(accessibleName);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    };
    configureSummaryValue(m_projectSummaryName, Tr::tr("Current project name"));
    configureSummaryValue(m_projectSummaryMode, Tr::tr("Current project timing mode"));
    configureSummaryValue(m_projectSummaryCycle, Tr::tr("Current project cycle period"));
    configureSummaryValue(m_projectSummaryDevices, Tr::tr("Configured device count"));

    auto projectSummaryLayout = new QGridLayout(m_projectSummary);
    projectSummaryLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS,
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS);
    projectSummaryLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHS);
    projectSummaryLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVXs);
    projectSummaryLayout->addWidget(new QLabel(Tr::tr("Name:"), m_projectSummary), 0, 0);
    projectSummaryLayout->addWidget(m_projectSummaryName, 0, 1);
    projectSummaryLayout->addWidget(new QLabel(Tr::tr("Mode:"), m_projectSummary), 1, 0);
    projectSummaryLayout->addWidget(m_projectSummaryMode, 1, 1);
    projectSummaryLayout->addWidget(new QLabel(Tr::tr("Cycle:"), m_projectSummary), 2, 0);
    projectSummaryLayout->addWidget(m_projectSummaryCycle, 2, 1);
    projectSummaryLayout->addWidget(new QLabel(Tr::tr("Devices:"), m_projectSummary), 3, 0);
    projectSummaryLayout->addWidget(m_projectSummaryDevices, 3, 1);
    projectSummaryLayout->setColumnStretch(1, 1);
    m_projectSummary->hide();

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVXxs);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_resultsStack, 1);
    layout->addWidget(m_projectSummary);

    connect(
        m_filterEdit,
        &QLineEdit::textChanged,
        this,
        &WorkbenchNavigationWidget::handleFilterTextChanged);
    const auto refreshFilteredResults = [this] {
        expandFilteredResults();
        updateFilterState();
    };
    connect(m_proxyModel, &QAbstractItemModel::rowsInserted, this, refreshFilteredResults);
    connect(m_proxyModel, &QAbstractItemModel::rowsRemoved, this, refreshFilteredResults);
    connect(m_proxyModel, &QAbstractItemModel::modelReset, this, refreshFilteredResults);
    connect(m_proxyModel, &QAbstractItemModel::layoutChanged, this, refreshFilteredResults);
    connect(m_proxyModel, &QAbstractItemModel::dataChanged, this, refreshFilteredResults);
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
            if (m_sourceModelResetting || m_proxyModelChangeDepth > 0 || !current.isValid())
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
        m_treeView,
        &QTreeView::expanded,
        this,
        [this](const QModelIndex &index) { updateExpansionState(index, true); });
    connect(
        m_treeView,
        &QTreeView::collapsed,
        this,
        [this](const QModelIndex &index) { updateExpansionState(index, false); });
    connect(
        m_sourceModel,
        &QAbstractItemModel::modelReset,
        this,
        &WorkbenchNavigationWidget::handleModelReset);
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
    if (Core::ProjectService *projectService = controller->projectService()) {
        const auto refreshProjectSummary = [this] { updateProjectSummary(); };
        connect(
            projectService,
            &Core::ProjectService::projectAdded,
            this,
            refreshProjectSummary);
        connect(
            projectService,
            &Core::ProjectService::projectChanged,
            this,
            refreshProjectSummary);
        connect(
            projectService,
            &Core::ProjectService::activeProjectChanged,
            this,
            refreshProjectSummary);
        connect(
            projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            [this, projectService](const Data::NodeId &projectId) {
                if (projectId == projectService->activeProjectId())
                    m_projectSummary->hide();
            });
    }
    connect(
        m_treeView,
        &QTreeView::customContextMenuRequested,
        this,
        [this](const QPoint &position) { showContextMenu(position, true); });
    m_knownNodeIds = sourceNodeIds();
    m_treeView->expandToDepth(2);
    updateFilterState();
    updateProjectSummary();
}

QTreeView *WorkbenchNavigationWidget::treeView() const
{
    return m_treeView;
}

QLineEdit *WorkbenchNavigationWidget::filterEdit() const
{
    return m_filterEdit;
}

bool WorkbenchNavigationWidget::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_treeView || watched == m_treeView->viewport())
        && event->type() == QEvent::ContextMenu) {
        auto contextMenuEvent = static_cast<QContextMenuEvent *>(event);
        const QPoint viewportPosition
            = watched == m_treeView
                  ? m_treeView->viewport()->mapFrom(m_treeView, contextMenuEvent->pos())
                  : contextMenuEvent->pos();
        showContextMenu(
            viewportPosition, contextMenuEvent->reason() == QContextMenuEvent::Mouse);
        contextMenuEvent->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QSet<Data::NodeId> WorkbenchNavigationWidget::sourceNodeIds(int maximumDepth) const
{
    QSet<Data::NodeId> result;
    const auto collect = [this, maximumDepth, &result](
                             const auto &self, const QModelIndex &parent, int depth) -> void {
        for (int row = 0; row < m_sourceModel->rowCount(parent); ++row) {
            const QModelIndex index = m_sourceModel->index(row, 0, parent);
            const Data::NodeId nodeId
                = index.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
            if (!nodeId.isNull() && (maximumDepth < 0 || depth <= maximumDepth))
                result.insert(nodeId);
            if (maximumDepth < 0 || depth < maximumDepth)
                self(self, index, depth + 1);
        }
    };
    collect(collect, {}, 0);
    return result;
}

void WorkbenchNavigationWidget::handleFilterTextChanged(const QString &text)
{
    const bool wasFiltering = m_filterActive;
    m_filterActive = !text.isEmpty();
    const QScopedValueRollback ignoreChanges(m_ignoreExpansionChanges, true);
    m_proxyModel->setFilterFixedString(text);
    if (m_filterActive)
        m_treeView->expandAll();
    else if (wasFiltering) {
        restoreExpansionState();
        if (m_controller && m_controller->selectionService())
            selectNode(m_controller->selectionService()->currentNodeId());
    }
    updateFilterState();
}

void WorkbenchNavigationWidget::handleModelAboutToBeReset()
{
    m_knownNodeIds = sourceNodeIds();
    m_sourceModelResetting = true;
    m_treeView->clearSelection();
    m_treeView->setCurrentIndex({});
}

void WorkbenchNavigationWidget::handleModelReset()
{
    const QSet<Data::NodeId> currentNodeIds = sourceNodeIds();
    m_expandedNodeIds.intersect(currentNodeIds);
    const QSet<Data::NodeId> addedNodeIds = currentNodeIds - m_knownNodeIds;
    const QSet<Data::NodeId> defaultExpandedNodeIds = sourceNodeIds(2);
    for (const Data::NodeId &nodeId : addedNodeIds) {
        if (defaultExpandedNodeIds.contains(nodeId))
            m_expandedNodeIds.insert(nodeId);
    }
    m_knownNodeIds = currentNodeIds;

    {
        const QScopedValueRollback ignoreChanges(m_ignoreExpansionChanges, true);
        if (m_filterActive)
            m_treeView->expandAll();
        else
            restoreExpansionState();
    }
    m_sourceModelResetting = false;

    if (m_controller && m_controller->selectionService())
        selectNode(m_controller->selectionService()->currentNodeId(), false);
}

void WorkbenchNavigationWidget::restoreExpansionState()
{
    m_treeView->collapseAll();
    const auto restore = [this](const auto &self, const QModelIndex &parent) -> void {
        for (int row = 0; row < m_sourceModel->rowCount(parent); ++row) {
            const QModelIndex sourceIndex = m_sourceModel->index(row, 0, parent);
            const Data::NodeId nodeId
                = sourceIndex.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
            const QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
            if (proxyIndex.isValid() && m_expandedNodeIds.contains(nodeId))
                m_treeView->expand(proxyIndex);
            self(self, sourceIndex);
        }
    };
    restore(restore, {});
}

void WorkbenchNavigationWidget::expandFilteredResults()
{
    if (!m_filterActive)
        return;
    const QScopedValueRollback ignoreChanges(m_ignoreExpansionChanges, true);
    m_treeView->expandAll();
}

void WorkbenchNavigationWidget::updateExpansionState(
    const QModelIndex &proxyIndex, bool expanded)
{
    if (m_filterActive || m_ignoreExpansionChanges || m_sourceModelResetting)
        return;
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const Data::NodeId nodeId
        = sourceIndex.data(WorkbenchTreeModel::NodeIdRole).value<Data::NodeId>();
    if (nodeId.isNull())
        return;
    if (expanded)
        m_expandedNodeIds.insert(nodeId);
    else
        m_expandedNodeIds.remove(nodeId);
}

void WorkbenchNavigationWidget::updateFilterState()
{
    const bool noMatches
        = !m_filterEdit->text().isEmpty() && m_proxyModel->rowCount() == 0;
    m_resultsStack->setCurrentWidget(noMatches ? m_emptyState : m_treeResults);
    setFocusProxy(noMatches ? static_cast<QWidget *>(m_clearFilter)
                            : static_cast<QWidget *>(m_treeView));
}

void WorkbenchNavigationWidget::updateProjectSummary()
{
    Core::ProjectService *projectService
        = m_controller ? m_controller->projectService() : nullptr;
    const Data::NodeId projectId
        = projectService ? projectService->activeProjectId() : Data::NodeId();
    const std::optional<Data::ProjectSnapshot> project
        = projectService && !projectId.isNull() ? projectService->project(projectId)
                                               : std::nullopt;
    if (!project) {
        m_projectSummary->hide();
        return;
    }

    const QString mode = project->valid
                             ? projectTimingModeName(project->masterConfiguration.timingMode)
                             : Tr::tr("Unavailable");
    const QString cycle = project->valid
                              ? projectCyclePeriodText(
                                    project->masterConfiguration.cyclePeriodNs)
                              : Tr::tr("Unavailable");
    m_projectSummaryName->setText(project->name);
    m_projectSummaryName->setToolTip(project->name);
    m_projectSummaryMode->setText(mode);
    m_projectSummaryMode->setToolTip(mode);
    m_projectSummaryCycle->setText(cycle);
    m_projectSummaryCycle->setToolTip(
        project->valid && project->masterConfiguration.cyclePeriodNs
            ? Tr::tr("%1 ns").arg(project->masterConfiguration.cyclePeriodNs)
            : cycle);
    m_projectSummaryDevices->setText(QString::number(project->slaves.size()));
    m_projectSummaryDevices->setToolTip(
        Tr::tr("%n configured device(s)", nullptr, project->slaves.size()));
    m_projectSummary->show();
}

void WorkbenchNavigationWidget::selectNode(const Data::NodeId &nodeId, bool selectRow)
{
    if (nodeId.isNull()) {
        const QSignalBlocker blocker(m_treeView->selectionModel());
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
    if (!proxyIndex.isValid()) {
        const QSignalBlocker blocker(m_treeView->selectionModel());
        m_treeView->clearSelection();
        m_treeView->setCurrentIndex({});
        return;
    }
    QModelIndex parent = proxyIndex.parent();
    while (parent.isValid()) {
        m_treeView->expand(parent);
        parent = parent.parent();
    }
    if (selectRow) {
        m_treeView->setCurrentIndex(proxyIndex);
    } else {
        m_treeView->selectionModel()->setCurrentIndex(
            proxyIndex, QItemSelectionModel::NoUpdate);
    }
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
    const std::optional<Data::NodeId> projectId
        = currentLocateProjectId(m_controller.data(), m_sourceModel);
    if (projectId)
        selectSourceIndex(m_sourceModel->firstTopologyDifference(*projectId));
}

void WorkbenchNavigationWidget::locateFirstIssue()
{
    const std::optional<Data::NodeId> projectId
        = currentLocateProjectId(m_controller.data(), m_sourceModel);
    if (projectId)
        selectSourceIndex(m_sourceModel->firstIssue(*projectId));
}

void WorkbenchNavigationWidget::openDiagnostics()
{
    Core::SelectionService *selectionService
        = m_controller ? m_controller->selectionService() : nullptr;
    if (!selectionService)
        return;
    const Data::NodeId currentNodeId = selectionService->currentNodeId();
    const Core::PropertyPageContext current = m_sourceModel->contextForNodeId(currentNodeId);
    if (!currentNodeId.isNull() && current.nodeId.isNull())
        return;
    selectSourceIndex(m_sourceModel->diagnosticsForProject(current.projectId));
}

void WorkbenchNavigationWidget::showContextMenu(const QPoint &position, bool mouseTriggered)
{
    QModelIndex contextIndex = m_treeView->currentIndex();
    if (mouseTriggered) {
        const QModelIndex proxyIndex = m_treeView->indexAt(position);
        if (proxyIndex.isValid()) {
            contextIndex = proxyIndex;
            m_treeView->setCurrentIndex(proxyIndex);
            if (m_controller && m_controller->selectionService()) {
                const Core::PropertyPageContext clicked = m_sourceModel->contextForIndex(
                    m_proxyModel->mapToSource(proxyIndex));
                if (clicked.nodeKind != Core::WorkbenchNodeKind::Placeholder)
                    m_controller->selectionService()->setCurrentNodeId(clicked.nodeId);
            }
        } else if (m_treeView->viewport()->rect().contains(position))
            contextIndex = {};
    }
    const Core::PropertyPageContext context = m_sourceModel->contextForIndex(
        m_proxyModel->mapToSource(contextIndex));
    Core::SelectionService *selectionService
        = m_controller ? m_controller->selectionService() : nullptr;
    const Data::NodeId currentNodeId
        = selectionService ? selectionService->currentNodeId() : Data::NodeId();
    const bool contextMatchesSelection
        = selectionService
          && (currentNodeId.isNull() ? context.nodeId.isNull() : context.nodeId == currentNodeId);
    const auto setCommandEnabled = [](const Utils::Id &id, bool enabled) {
        if (::Core::Command *command = ::Core::ActionManager::command(id))
            command->action()->setEnabled(enabled);
    };
    setCommandEnabled(
        Constants::LOCATE_DIFFERENCE_ACTION_ID,
        contextMatchesSelection
            && m_sourceModel->firstTopologyDifference(context.projectId).isValid());
    setCommandEnabled(
        Constants::LOCATE_ISSUE_ACTION_ID,
        contextMatchesSelection && m_sourceModel->firstIssue(context.projectId).isValid());
    setCommandEnabled(
        Constants::OPEN_DIAGNOSTICS_ACTION_ID,
        contextMatchesSelection
            && m_sourceModel->diagnosticsForProject(context.projectId).isValid());
    setCommandEnabled(
        Constants::LOCATE_UNSUPPORTED_DEVICE_ACTION_ID,
        m_sourceModel->firstUnsupportedDevice().isValid());
    const bool canCopyNodeId = contextMatchesSelection && m_controller
                               && m_controller->canCopyNodeId(context.nodeId);
    setCommandEnabled(Constants::COPY_NODE_ID_ACTION_ID, canCopyNodeId);
    const bool canActivateSelectedProject
        = contextMatchesSelection && m_controller
          && m_controller->canActivateSelectedProject();
    setCommandEnabled(Constants::SET_ACTIVE_PROJECT_ACTION_ID, canActivateSelectedProject);
    setCommandEnabled(
        Constants::INSERT_DEVICE_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canInsertDeviceOnSelectedMaster());
    setCommandEnabled(
        Constants::ADD_DEVICE_TO_MASTER_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canAddSelectedDeviceToMaster());
    setCommandEnabled(
        Constants::REMOVE_OFFLINE_SLAVE_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canRemoveSelectedOfflineSlave());
    setCommandEnabled(
        Constants::MOVE_OFFLINE_SLAVE_UP_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canMoveSelectedOfflineSlaveUp());
    setCommandEnabled(
        Constants::MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canMoveSelectedOfflineSlaveDown());
    setCommandEnabled(
        Constants::CONNECT_CONTROLLER_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canConnectSelectedController());
    setCommandEnabled(
        Constants::SCAN_CONTROLLER_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canExecuteSelectedControllerControl(
                Data::ControllerControlCommand::DiscoverTopology));
    setCommandEnabled(
        Constants::APPLY_CURRENT_BUS_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canApplyCurrentBusToProject());
    setCommandEnabled(
        Constants::RESET_FAULT_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canExecuteSelectedControllerControl(
                Data::ControllerControlCommand::ResetFault));
    setCommandEnabled(
        Constants::REFRESH_CONTROLLER_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canRefreshSelectedController());
    setCommandEnabled(
        Constants::DISCONNECT_CONTROLLER_ACTION_ID,
        contextMatchesSelection && m_controller
            && m_controller->canDisconnectSelectedController());

    QMenu menu(this);
    if (selectionService) {
        connect(
            selectionService,
            &Core::SelectionService::currentNodeChanged,
            &menu,
            [&menu, currentNodeId](const Data::NodeId &current) {
                if (current != currentNodeId)
                    menu.close();
            });
    }
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
    if (context.nodeKind == Core::WorkbenchNodeKind::Project && canActivateSelectedProject) {
        menu.addSeparator();
        addCommand(Constants::SET_ACTIVE_PROJECT_ACTION_ID);
    }
    if (contextMatchesSelection && !context.projectId.isNull()) {
        menu.addSeparator();
        addCommand(Constants::CONNECT_CONTROLLER_ACTION_ID);
        addCommand(Constants::SCAN_CONTROLLER_ACTION_ID);
        addCommand(Constants::APPLY_CURRENT_BUS_ACTION_ID);
        addCommand(ProjectExplorer::Constants::RUN);
        addCommand(Constants::DEBUG_ACTION_ID);
        addCommand(Constants::CONTROLLED_STOP_ACTION_ID);
        addCommand(Constants::RESET_FAULT_ACTION_ID);
        addCommand(Constants::REFRESH_CONTROLLER_ACTION_ID);
        addCommand(Constants::DISCONNECT_CONTROLLER_ACTION_ID);
    }
    if (canCopyNodeId) {
        menu.addSeparator();
        addCommand(Constants::COPY_NODE_ID_ACTION_ID);
    }
    QPoint menuPosition = position;
    if (!mouseTriggered) {
        const QRect currentRect = m_treeView->visualRect(m_treeView->currentIndex());
        const QRect visibleCurrentRect
            = currentRect.intersected(m_treeView->viewport()->rect());
        menuPosition = visibleCurrentRect.isEmpty()
                           ? m_treeView->viewport()->rect().center()
                           : visibleCurrentRect.center();
    }
    menu.exec(m_treeView->viewport()->mapToGlobal(menuPosition));

    Core::SelectionService *restoredSelectionService
        = m_controller ? m_controller->selectionService() : nullptr;
    if (!restoredSelectionService) {
        setCommandEnabled(Constants::LOCATE_DIFFERENCE_ACTION_ID, false);
        setCommandEnabled(Constants::LOCATE_ISSUE_ACTION_ID, false);
        setCommandEnabled(Constants::OPEN_DIAGNOSTICS_ACTION_ID, false);
        return;
    }
    const Data::NodeId restoredNodeId = restoredSelectionService->currentNodeId();
    const Core::PropertyPageContext restoredContext
        = m_sourceModel->contextForNodeId(restoredNodeId);
    const bool restoredSelectionIsKnown
        = restoredNodeId.isNull() || !restoredContext.nodeId.isNull();
    selectNode(restoredNodeId);
    setCommandEnabled(
        Constants::LOCATE_DIFFERENCE_ACTION_ID,
        restoredSelectionIsKnown
            && m_sourceModel->firstTopologyDifference(restoredContext.projectId).isValid());
    setCommandEnabled(
        Constants::LOCATE_ISSUE_ACTION_ID,
        restoredSelectionIsKnown
            && m_sourceModel->firstIssue(restoredContext.projectId).isValid());
    setCommandEnabled(
        Constants::OPEN_DIAGNOSTICS_ACTION_ID,
        restoredSelectionIsKnown
            && m_sourceModel->diagnosticsForProject(restoredContext.projectId).isValid());
    setCommandEnabled(
        Constants::COPY_NODE_ID_ACTION_ID,
        m_controller && m_controller->canCopyNodeId(restoredNodeId));
    setCommandEnabled(
        Constants::SET_ACTIVE_PROJECT_ACTION_ID,
        m_controller && m_controller->canActivateSelectedProject());
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
    setCommandEnabled(
        Constants::APPLY_CURRENT_BUS_ACTION_ID,
        m_controller && m_controller->canApplyCurrentBusToProject());
}

void WorkbenchNavigationWidget::locateUnsupportedDevice()
{
    selectSourceIndex(m_sourceModel->firstUnsupportedDevice());
}

void WorkbenchNavigationWidget::copyCurrentNodeId()
{
    const Core::PropertyPageContext context = m_sourceModel->contextForIndex(
        m_proxyModel->mapToSource(m_treeView->currentIndex()));
    if (m_controller && m_controller->canCopyNodeId(context.nodeId))
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
