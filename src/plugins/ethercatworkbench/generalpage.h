// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QTreeWidget;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class GeneralPage final : public QWidget
{
    Q_OBJECT

public:
    explicit GeneralPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    void reset(const QString &summary);
    void addRow(const QStringList &values);
    void commitName();

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_summary;
    QWidget *m_identityForm;
    QLineEdit *m_name;
    QLineEdit *m_id;
    QLineEdit *m_objectId;
    QLineEdit *m_type;
    QTreeWidget *m_tree;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
