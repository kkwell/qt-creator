// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
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
    void commitMasterName();
    void refreshMasterSummary();

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_summary;
    QWidget *m_identityForm;
    QLineEdit *m_name;
    QLineEdit *m_id;
    QLineEdit *m_objectId;
    QLineEdit *m_type;
    QWidget *m_masterContent;
    QWidget *m_masterForm;
    QLineEdit *m_masterName;
    QLineEdit *m_masterId;
    QLineEdit *m_masterObjectId;
    QLineEdit *m_masterType;
    QPlainTextEdit *m_masterComment;
    QCheckBox *m_masterDisabled;
    QCheckBox *m_masterCreateSymbols;
    QGroupBox *m_masterSummaryForm;
    QLineEdit *m_masterCycle;
    QLineEdit *m_masterSlaveCount;
    QLineEdit *m_masterStatus;
    QTreeWidget *m_tree;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
