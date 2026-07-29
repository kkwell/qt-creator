// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace EtherCAT::Core {
class SemanticRuntimeService;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class SemanticControlPage final : public QWidget
{
    Q_OBJECT

public:
    SemanticControlPage(
        WorkbenchController *controller,
        Core::SemanticRuntimeService *runtimeService,
        QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    void refresh();

    QPointer<WorkbenchController> m_controller;
    QPointer<Core::SemanticRuntimeService> m_runtimeService;
    Core::PropertyPageContext m_context;
    QLabel *m_status;
    QTreeWidget *m_signals;
    QGroupBox *m_manualControl;
    QLineEdit *m_requestedValue;
    QPushButton *m_apply;
};

} // namespace EtherCAT::Workbench::Internal
