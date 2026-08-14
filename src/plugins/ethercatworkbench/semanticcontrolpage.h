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
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QTimer;
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

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct RuntimePresentation
    {
        Core::PropertyPageContext pageContext;
        Data::ControllerConnectionScope scope;
        Data::NodeId deviceId;
        QList<Data::SemanticSignalId> signalIds;
        bool wholeDevice = true;
        Data::SemanticRuntimeContext runtimeContext;
    };

    void refresh();
    bool refreshLiveSignalsOnly();
    void presentLiveSignals(
        const QList<Data::SemanticSignalRuntimeState> &states, bool requiresWholeDeviceControl);
    void updateLiveRefreshScheduler();
    void stopLiveRefreshScheduler();
    void requestLiveRefresh();
    void handleLiveRefreshResult(const Data::SemanticLiveRefreshResult &result);
    std::optional<Data::SemanticLiveRefreshResult> normalizeLiveRefreshResult(
        const Data::SemanticLiveRefreshResult &result, bool completionSignal) const;
    void scheduleLiveRefresh(int delayMs);
    bool liveRefreshBlockedByOperation() const;
    void pauseLiveRefresh(Data::SemanticLiveRefreshOutcome outcome);
    void clearLiveRefreshPauseIndication();
    void refreshActionEditor();
    void refreshOperation();
    void requestSelectedAction();
    void submitConfirmedAction(
        const Data::SemanticOperationRequest &request, const QString &actionName);
    void requestStop();
    void presentOperation(const Data::SemanticOperationRecord &record);
    bool recordMatchesActiveOperation(const Data::SemanticOperationRecord &record) const;
    void updateApplyEnabled();
    void updateStopEnabled();

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
    QPushButton *m_stop;
    QTimer *m_liveRefreshTimer;
    QList<Data::SemanticActionRuntimeState> m_actionStates;
    QHash<QString, QPointer<QWidget>> m_parameterEditors;
    std::optional<RuntimePresentation> m_runtimePresentation;
    Data::SemanticActionId m_selectedActionId;
    Data::SemanticOperationId m_activeOperationId;
    std::optional<Data::SemanticOperationState> m_activeOperationState;
    std::optional<Data::SemanticOperationRequest> m_activeOperationRequest;
    std::optional<Data::SemanticOperationRecord> m_activeOperationRecord;
    std::optional<Data::SemanticOperationCancelRequest> m_activeCancelRequest;
    QList<Data::SemanticLiveRefreshSignal> m_liveRefreshTargets;
    QString m_liveRefreshControllerId;
    Data::ControllerConnectionScope m_liveRefreshScope;
    quint64 m_liveRefreshSessionGeneration = 0;
    QByteArray m_liveRefreshContextHash;
    QString m_liveRefreshCorrelationId;
    qsizetype m_liveRefreshBatchOffset = 0;
    quint64 m_liveRefreshRequestSequence = 0;
    int m_liveRefreshDeferredCount = 0;
    int m_liveRefreshFailureCount = 0;
    bool m_liveRefreshPaused = false;
    bool m_discardLiveRefreshCompletion = false;
    QString m_liveRefreshPauseMessage;
    QString m_activeActionName;
    QByteArray m_localAuthenticationDigest;
    Data::SemanticOperationId m_lastReportedOperationId;
    QString m_lastReportedOperationMessage;
    Data::SemanticOperationState m_lastReportedOperationState
        = Data::SemanticOperationState::Rejected;
    bool m_hasReportedOperation = false;
    bool m_confirmationOpen = false;
    bool m_activeOperationCancelable = false;
};

} // namespace EtherCAT::Workbench::Internal
