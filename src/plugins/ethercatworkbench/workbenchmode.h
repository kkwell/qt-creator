// Copyright (C) 2026 Kvell

#pragma once

#include <coreplugin/imode.h>

#include <QPointer>

namespace EtherCAT::Workbench::Internal {

class DetailsView;
class WorkbenchController;

class WorkbenchMode final : public ::Core::IMode
{
    Q_OBJECT

public:
    explicit WorkbenchMode(WorkbenchController *controller, QObject *parent = nullptr);
};

} // namespace EtherCAT::Workbench::Internal
