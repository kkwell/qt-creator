// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/automationservice.h>

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class WorkbenchAutomationService final : public Core::AutomationService
{
    Q_OBJECT

public:
    explicit WorkbenchAutomationService(WorkbenchController *controller, QObject *parent = nullptr);

    QList<Core::AutomationContextSnapshot> contexts() const final;

private:
    WorkbenchController *const m_controller;
};

} // namespace EtherCAT::Workbench::Internal
