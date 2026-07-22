// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QDialog;
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
    void reset(const QString &summary, const QStringList &headers, bool preserveAlias = false);
    void addSyncManagerRow(const QStringList &values);
    void addSyncManagers(const Data::DeviceDescription &device);
    void commitAlias();
    void showMasterTopology();
    QString previousPortText(const Data::OfflineSlaveConfiguration &slave) const;

    QPointer<WorkbenchController> m_controller;
    QPointer<QDialog> m_masterTopologyDialog;
    Core::PropertyPageContext m_context;
    QLabel *m_summary;
    QWidget *m_masterForm;
    QLineEdit *m_masterNetId;
    QPushButton *m_masterAdvancedSettings;
    QPushButton *m_masterExportConfiguration;
    QPushButton *m_masterSyncUnitAssignment;
    QPushButton *m_masterTopology;
    QLabel *m_masterFrameState;
    QWidget *m_slaveForm;
    QLineEdit *m_type;
    QLineEdit *m_productRevision;
    QLineEdit *m_autoIncAddress;
    QLineEdit *m_ethercatAddress;
    QSpinBox *m_alias;
    QLineEdit *m_aliasEditor;
    QLineEdit *m_identificationValue;
    QLineEdit *m_previousPort;
    QPushButton *m_advancedSettings;
    QTreeWidget *m_tree;
    Data::NodeId m_aliasBaselineProjectId;
    Data::NodeId m_aliasBaselineNodeId;
    Core::WorkbenchNodeKind m_aliasBaselineKind = Core::WorkbenchNodeKind::None;
    quint16 m_aliasBaseline = 0;
    bool m_forceAuthoritativeAliasReload = false;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
