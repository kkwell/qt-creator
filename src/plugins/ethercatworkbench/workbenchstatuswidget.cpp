// Copyright (C) 2026 Kvell

#include "workbenchstatuswidget.h"

#include "ethercatworkbenchconstants.h"
#include "ethercatworkbenchtr.h"

#include <coreplugin/modemanager.h>

#include <ethercatcore/stateservice.h>

#include <utils/utilsicons.h>

#include <QAction>
#include <QMenu>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>

#include <algorithm>

namespace EtherCAT::Workbench::Internal {

static QIcon statusIcon(Core::StatusSeverity severity)
{
    switch (severity) {
    case Core::StatusSeverity::Ready:
        return Utils::Icons::OK.icon();
    case Core::StatusSeverity::Busy:
        return Utils::Icons::RUN_SMALL_TOOLBAR.icon();
    case Core::StatusSeverity::Warning:
        return Utils::Icons::WARNING_TOOLBAR.icon();
    case Core::StatusSeverity::Error:
        return Utils::Icons::CRITICAL_TOOLBAR.icon();
    }
    return Utils::Icons::NOTLOADED.icon();
}

static QString statusText(Core::StatusSeverity severity)
{
    switch (severity) {
    case Core::StatusSeverity::Ready:
        return Tr::tr("MOCK Ready");
    case Core::StatusSeverity::Busy:
        return Tr::tr("MOCK Busy");
    case Core::StatusSeverity::Warning:
        return Tr::tr("MOCK Warning");
    case Core::StatusSeverity::Error:
        return Tr::tr("MOCK Fault");
    }
    return Tr::tr("Unknown");
}

static const Core::StatusEntry *primaryStatus(const QList<Core::StatusEntry> &statuses)
{
    if (statuses.isEmpty())
        return nullptr;
    return &*std::max_element(
        statuses.cbegin(), statuses.cend(), [](const auto &left, const auto &right) {
            return int(left.severity) < int(right.severity);
        });
}

WorkbenchStatusWidget::WorkbenchStatusWidget(Core::StateService *stateService, QWidget *parent)
    : QToolButton(parent)
    , m_stateService(stateService)
    , m_menu(new QMenu(this))
{
    setObjectName("EtherCATWorkbenchStatus");
    setAutoRaise(true);
    setPopupMode(QToolButton::InstantPopup);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const int iconExtent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    setIconSize(QSize(iconExtent, iconExtent));
    setAccessibleName(Tr::tr("EtherCAT workbench status"));
    setMenu(m_menu);

    connect(
        ::Core::ModeManager::instance(),
        &::Core::ModeManager::currentModeChanged,
        this,
        [this](Utils::Id modeId) { setVisible(modeId == Constants::MODE_ID); });
    setVisible(::Core::ModeManager::currentModeId() == Constants::MODE_ID);

    if (m_stateService) {
        connect(m_stateService, &Core::StateService::statusChanged, this, [this] {
            updateStatus();
        });
    }
    updateStatus();
}

void WorkbenchStatusWidget::updateStatus()
{
    const QList<Core::StatusEntry> statuses = m_stateService ? m_stateService->statuses()
                                                             : QList<Core::StatusEntry>();
    const Core::StatusEntry *primary = primaryStatus(statuses);

    m_menu->clear();
    if (!primary) {
        const QString summary = Tr::tr("Offline");
        const QString details = Tr::tr(
            "No local Mock scan or diagnostics workflow is active. No controller is connected.");
        setText(summary);
        setIcon(Utils::Icons::NOTLOADED.icon());
        setToolTip(details);
        setAccessibleDescription(details);
        QAction *offline = m_menu->addAction(Utils::Icons::NOTLOADED.icon(), summary);
        offline->setToolTip(details);
        offline->setEnabled(false);
        updateMinimumWidth();
        return;
    }

    setText(statusText(primary->severity));
    setIcon(statusIcon(primary->severity));

    QStringList details;
    for (const Core::StatusEntry &status : statuses) {
        const QString line = status.details.isEmpty()
                                 ? status.summary
                                 : Tr::tr("%1 — %2").arg(status.summary, status.details);
        details.append(line);
        QAction *entry = m_menu->addAction(statusIcon(status.severity), status.summary);
        entry->setToolTip(status.details);
        entry->setEnabled(false);
    }
    const QString tooltip = details.join('\n');
    setToolTip(tooltip);
    setAccessibleDescription(tooltip);
    updateMinimumWidth();
}

void WorkbenchStatusWidget::updateMinimumWidth()
{
    setMinimumWidth(sizeHint().width());
    updateGeometry();
}

} // namespace EtherCAT::Workbench::Internal
