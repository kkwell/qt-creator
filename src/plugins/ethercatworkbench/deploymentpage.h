// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <utils/filepath.h>

#include <QByteArray>
#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QToolButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class DeploymentPage final : public QWidget
{
    Q_OBJECT

public:
    explicit DeploymentPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    void browseForArtifact();
    bool loadArtifact(bool reportError);
    void clearArtifact();
    void prepareNewOperation();
    void deploy();
    void cancelDeployment();
    void refresh();
    void updateStatus(
        const Data::ControllerConnectionSnapshot &snapshot,
        Core::ControllerConnectionProvider *provider);
    void updateAudit(const Data::ControllerPackageDeploymentProgress &progress);
    QString inputUnavailableReason() const;
    std::optional<quint64> configurationId() const;

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    Utils::FilePath m_loadedArtifactPath;
    QByteArray m_artifact;
    QByteArray m_artifactSha256;
    QLabel *m_guidance = nullptr;
    QLineEdit *m_artifactPath = nullptr;
    QToolButton *m_browse = nullptr;
    QLabel *m_artifactSummary = nullptr;
    QLineEdit *m_configurationId = nullptr;
    QLineEdit *m_operationId = nullptr;
    QToolButton *m_newOperation = nullptr;
    QCheckBox *m_activate = nullptr;
    QCheckBox *m_rollback = nullptr;
    QToolButton *m_deploy = nullptr;
    QToolButton *m_cancel = nullptr;
    QLabel *m_deploymentSummary = nullptr;
    QProgressBar *m_progress = nullptr;
    QTreeWidget *m_status = nullptr;
    QTreeWidget *m_audit = nullptr;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
