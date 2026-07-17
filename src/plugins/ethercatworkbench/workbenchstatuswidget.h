// Copyright (C) 2026 Kvell

#pragma once

#include <QPointer>
#include <QToolButton>

QT_BEGIN_NAMESPACE
class QMenu;
QT_END_NAMESPACE

namespace EtherCAT::Core {
class StateService;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchStatusWidget final : public QToolButton
{
public:
    explicit WorkbenchStatusWidget(Core::StateService *stateService, QWidget *parent = nullptr);

private:
    void updateStatus();
    void updateMinimumWidth();

    QPointer<Core::StateService> m_stateService;
    QMenu *m_menu = nullptr;
};

} // namespace EtherCAT::Workbench::Internal
