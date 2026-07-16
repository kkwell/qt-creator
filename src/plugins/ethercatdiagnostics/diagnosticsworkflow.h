// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QHash>
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace EtherCAT::Core {
class SelectionService;
class StateService;
}

namespace EtherCAT::Diagnostics::Internal {

class MockDiagnosticsProvider;

class DiagnosticsWorkflow final : public QObject
{
    Q_OBJECT

public:
    explicit DiagnosticsWorkflow(
        MockDiagnosticsProvider *provider, QObject *parent = nullptr);

    void setupActions();
    void setPageContext(const Core::PropertyPageContext &context);
    void setSelectedAlarmId(const Data::NodeId &eventId);
    Utils::Result<> startMonitoring();
    void stopMonitoring();
    Utils::Result<> requestMode(Data::DiagnosticsRunMode mode);
    Utils::Result<> acknowledgeSelectedAlarm();
    Utils::Result<> clearRecoveredEvents();
    QAction *action(Utils::Id id) const;
    void shutdown();

signals:
    void actionStateChanged();

private:
    Utils::Result<Data::DiagnosticsRequest> requestForContext() const;
    QAction *registeredAction(Utils::Id id) const;
    void updateActions();
    void updateStatus();
    void reportError(const QString &error) const;

    QPointer<MockDiagnosticsProvider> m_provider;
    QPointer<Core::SelectionService> m_selectionService;
    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::StateService> m_stateService;
    Core::PropertyPageContext m_pageContext;
    Data::NodeId m_selectedAlarmId;
    QHash<Utils::Id, QAction *> m_registeredActions;
    bool m_shuttingDown = false;
};

} // namespace EtherCAT::Diagnostics::Internal
