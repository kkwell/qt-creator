// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace Utils {
class InfoLabel;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;
class EsiDeviceGeneralPage;
class EsiRepositoryPage;

class GeneralPage final : public QWidget
{
    Q_OBJECT

public:
    explicit GeneralPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    QLineEdit *nameEditor(Core::WorkbenchNodeKind kind) const;
    void reset(const QString &summary, QLineEdit *preservedName = nullptr);
    void addRow(const QStringList &values);
    void showNameFeedback(const QString &message);
    void clearNameFeedback();
    void commitProjectName();
    void commitName();
    void commitTargetName();
    void commitMasterName();
    void commitMasterConfiguration();
    void refreshMasterSummary();

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_summary;
    Utils::InfoLabel *m_nameFeedback;
    EsiRepositoryPage *m_repositoryPage;
    EsiDeviceGeneralPage *m_esiDevicePage;
    QWidget *m_projectContent;
    QWidget *m_projectForm;
    QLineEdit *m_projectName;
    QLineEdit *m_projectId;
    QLineEdit *m_projectType;
    QGroupBox *m_projectSummaryForm;
    QLineEdit *m_projectFormatVersion;
    QLineEdit *m_projectCreatedBy;
    QLineEdit *m_projectValidity;
    QLineEdit *m_projectMigration;
    QLineEdit *m_projectModified;
    QLineEdit *m_projectTarget;
    QLineEdit *m_projectMaster;
    QLineEdit *m_projectSlaveCount;
    QWidget *m_identityForm;
    QLineEdit *m_name;
    QLineEdit *m_id;
    QLineEdit *m_objectId;
    QLineEdit *m_type;
    QWidget *m_targetContent;
    QWidget *m_targetHeader;
    QLabel *m_targetIcon;
    QLineEdit *m_targetName;
    QLabel *m_targetIdentity;
    QPushButton *m_chooseTarget;
    QGroupBox *m_targetVersionForm;
    QLabel *m_targetEngineering;
    QLabel *m_targetRuntime;
    QLabel *m_targetLocalRuntime;
    QLabel *m_targetProjectVersion;
    QCheckBox *m_targetPinVersion;
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
    QComboBox *m_masterTimingMode;
    QLineEdit *m_masterCycle;
    QLineEdit *m_masterSlaveCount;
    QLineEdit *m_masterStatus;
    QPushButton *m_masterApply;
    QTreeWidget *m_tree;
    Data::NodeId m_nameBaselineProjectId;
    Data::NodeId m_nameBaselineNodeId;
    Core::WorkbenchNodeKind m_nameBaselineKind = Core::WorkbenchNodeKind::None;
    QString m_nameBaseline;
    bool m_forceAuthoritativeNameReload = false;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
