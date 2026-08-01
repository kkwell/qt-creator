// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providers.h>

#include <ethercatdata/semanticruntime.h>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
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
    void refreshActionEditor();
    void refreshOperation();
    void requestSelectedAction();
    void submitConfirmedAction(
        const Data::SemanticOperationRequest &request, const QString &actionName);
    void presentOperation(const Data::SemanticOperationRecord &record);
    void updateApplyEnabled();

    std::optional<Data::SemanticRuntimeContext> selectedRuntimeContext() const;
    std::optional<Data::SemanticActionRuntimeState> selectedAction() const;
    std::optional<QMap<QString, QVariant>> parameterValues() const;
    Data::SemanticRuntimeActor localUserActor() const;

    QPointer<WorkbenchController> m_controller;
    QPointer<Core::SemanticRuntimeService> m_runtimeService;
    Core::PropertyPageContext m_context;
    QLabel *m_selectionScope;
    QLabel *m_status;
    QTreeWidget *m_signals;
    QGroupBox *m_manualControl;
    QTreeWidget *m_actions;
    QLabel *m_actionDetail;
    QWidget *m_parameterHost;
    QFormLayout *m_parameterLayout;
    QSpinBox *m_ttlCycles;
    QLabel *m_operationStatus;
    QLineEdit *m_requestedValue;
    QPushButton *m_apply;
    QList<Data::SemanticActionRuntimeState> m_actionStates;
    QHash<QString, QPointer<QWidget>> m_parameterEditors;
    Data::SemanticActionId m_selectedActionId;
    Data::SemanticOperationId m_activeOperationId;
    std::optional<Data::SemanticOperationState> m_activeOperationState;
    QString m_activeActionName;
    QByteArray m_localAuthenticationDigest;
    Data::SemanticOperationId m_lastReportedOperationId;
    QString m_lastReportedOperationMessage;
    Data::SemanticOperationState m_lastReportedOperationState
        = Data::SemanticOperationState::Rejected;
    bool m_hasReportedOperation = false;
    bool m_confirmationOpen = false;
};

} // namespace EtherCAT::Workbench::Internal
