// Copyright (C) 2026 Kvell

#include "workbenchcommandstrip.h"

#include "ethercatworkbenchtr.h"

#include <utils/qtcassert.h>

#include <QAction>
#include <QEvent>
#include <QMenu>

namespace EtherCAT::Workbench::Internal {

WorkbenchCommandStrip::WorkbenchCommandStrip(
    QMenu *sourceMenu, QAction *excludedAction, QWidget *parent)
    : QToolBar(parent)
    , m_sourceMenu(sourceMenu)
    , m_excludedAction(excludedAction)
{
    setObjectName("EtherCATWorkbenchCommandStrip");
    setWindowTitle(Tr::tr("EtherCAT Engineering Commands"));
    setAccessibleName(Tr::tr("EtherCAT engineering commands"));
    setAccessibleDescription(
        Tr::tr("Commands registered by the available EtherCAT plugins."));
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);

    QTC_ASSERT(m_sourceMenu, return);
    m_sourceMenu->installEventFilter(this);
    connect(m_sourceMenu, &QObject::destroyed, this, [this] { clear(); });
    synchronizeActions();
}

bool WorkbenchCommandStrip::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_sourceMenu
        && (event->type() == QEvent::ActionAdded || event->type() == QEvent::ActionRemoved)) {
        synchronizeActions();
    }
    return QToolBar::eventFilter(watched, event);
}

void WorkbenchCommandStrip::synchronizeActions()
{
    clear();
    if (!m_sourceMenu)
        return;

    for (QAction *action : m_sourceMenu->actions()) {
        if (action == m_excludedAction)
            continue;
        addAction(action);
    }
}

} // namespace EtherCAT::Workbench::Internal
