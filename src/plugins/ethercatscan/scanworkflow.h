// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace EtherCAT::Core {
class ProviderRegistry;
class ScanProviderSelectionService;
class SelectionService;
class StateService;
class TopologyService;
} // namespace EtherCAT::Core

namespace EtherCAT::Scan::Internal {

class MockScanProvider;

class ScanWorkflow final : public QObject
{
    Q_OBJECT

public:
    explicit ScanWorkflow(MockScanProvider *provider, QObject *parent = nullptr);

    void setupActions();
    Utils::Result<> scanInterfaces();
    Utils::Result<> scanSlaves();
    Utils::Result<> rescanSelectedBranch();
    Utils::Result<> compareWithProject();
    Utils::Result<> acceptScan();
    Utils::Result<> keepExistingConfiguration();
    void cancelScan();
    void shutdown();

    QAction *action(Utils::Id id) const;

signals:
    void actionStateChanged();

private:
    Utils::Result<Data::ScanRequest> requestForSelection(Data::ScanOperation operation) const;
    Utils::Result<> validateExactProviderSelection(
        const Data::ControllerConnectionScope &scope) const;
    Utils::Result<Data::ScanResult> freshMockResult(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &expectedSnapshotId = {}) const;
    Utils::Result<> start(Data::ScanOperation operation);
    void updateActions();
    void updateStatus(Data::ScanState state);
    void reportError(const QString &error) const;

    QPointer<MockScanProvider> m_provider;
    QPointer<Core::ProviderRegistry> m_providerRegistry;
    QPointer<Core::SelectionService> m_selectionService;
    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::ScanProviderSelectionService> m_scanProviderSelectionService;
    QPointer<Core::StateService> m_stateService;
    QPointer<Core::TopologyService> m_topologyService;
    QList<QAction *> m_actions;
    bool m_shuttingDown = false;
};

} // namespace EtherCAT::Scan::Internal
