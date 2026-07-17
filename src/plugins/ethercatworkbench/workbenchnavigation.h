// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/nodeid.h>

#include <coreplugin/inavigationwidgetfactory.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QSortFilterProxyModel;
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

private:
    void selectSourceIndex(const QModelIndex &sourceIndex);
    void selectNode(const Data::NodeId &nodeId);
    void showContextMenu(const QPoint &position);
    void locateUnsupportedDevice();

    QPointer<WorkbenchController> m_controller;
    WorkbenchTreeModel *m_sourceModel = nullptr;
    QSortFilterProxyModel *m_proxyModel = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QTreeView *m_treeView = nullptr;
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
