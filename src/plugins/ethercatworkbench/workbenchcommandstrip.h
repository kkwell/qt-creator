// Copyright (C) 2026 Kvell

#pragma once

#include <QPointer>
#include <QToolBar>

QT_BEGIN_NAMESPACE
class QAction;
class QEvent;
class QMenu;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchCommandStrip final : public QToolBar
{
public:
    explicit WorkbenchCommandStrip(
        QMenu *sourceMenu, QAction *excludedAction, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) final;

private:
    void synchronizeActions();

    QPointer<QMenu> m_sourceMenu;
    QPointer<QAction> m_excludedAction;
};

} // namespace EtherCAT::Workbench::Internal
