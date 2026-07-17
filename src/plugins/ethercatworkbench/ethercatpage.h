// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class EtherCATPage final : public QWidget
{
    Q_OBJECT

public:
    explicit EtherCATPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    void reset(const QString &summary, const QStringList &headers);
    void addRow(const QStringList &values);
    void addSyncManagers(const Data::DeviceDescription &device);
    void commitAlias();
    QString previousPortText(const Data::OfflineSlaveConfiguration &slave) const;

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_summary;
    QWidget *m_slaveForm;
    QLineEdit *m_type;
    QLineEdit *m_productRevision;
    QLineEdit *m_autoIncAddress;
    QLineEdit *m_ethercatAddress;
    QSpinBox *m_alias;
    QLineEdit *m_identificationValue;
    QLineEdit *m_previousPort;
    QPushButton *m_advancedSettings;
    QTreeWidget *m_tree;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
