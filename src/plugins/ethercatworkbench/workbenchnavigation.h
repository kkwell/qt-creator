// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/nodeid.h>

#include <coreplugin/inavigationwidgetfactory.h>

#include <QPointer>
#include <QSet>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QEvent;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QTreeView;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;
class WorkbenchTreeModel;

class WorkbenchNavigationWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit WorkbenchNavigationWidget(WorkbenchController *controller, QWidget *parent = nullptr);

    QTreeView *treeView() const;
    QLineEdit *filterEdit() const;
    void locateFirstTopologyDifference();
    void locateFirstIssue();
    void openDiagnostics();

protected:
    bool eventFilter(QObject *watched, QEvent *event) final;

private:
    QSet<Data::NodeId> sourceNodeIds(int maximumDepth = -1) const;
    void handleFilterTextChanged(const QString &text);
    void handleModelAboutToBeReset();
    void handleModelReset();
    void restoreExpansionState();
    void expandFilteredResults();
    void updateExpansionState(const QModelIndex &proxyIndex, bool expanded);
    void selectSourceIndex(const QModelIndex &sourceIndex);
    void selectNode(const Data::NodeId &nodeId);
    void updateFilterState();
    void showContextMenu(const QPoint &position, bool mouseTriggered);
    void locateUnsupportedDevice();
    void copyCurrentNodeId();

    QPointer<WorkbenchController> m_controller;
    WorkbenchTreeModel *m_sourceModel = nullptr;
    QSortFilterProxyModel *m_proxyModel = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QTreeView *m_treeView = nullptr;
    QWidget *m_treeResults = nullptr;
    QStackedWidget *m_resultsStack = nullptr;
    QWidget *m_emptyState = nullptr;
    QPushButton *m_clearFilter = nullptr;
    QSet<Data::NodeId> m_expandedNodeIds;
    QSet<Data::NodeId> m_knownNodeIds;
    bool m_filterActive = false;
    bool m_ignoreExpansionChanges = false;
    bool m_sourceModelResetting = false;
};

class WorkbenchNavigationFactory final : public ::Core::INavigationWidgetFactory
{
public:
    explicit WorkbenchNavigationFactory(WorkbenchController *controller);

    ::Core::NavigationView createWidget() final;

    WorkbenchNavigationWidget *lastCreatedWidget() const;

private:
    QPointer<WorkbenchController> m_controller;
    QPointer<WorkbenchNavigationWidget> m_lastCreatedWidget;
};

} // namespace EtherCAT::Workbench::Internal
