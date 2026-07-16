// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QTimer>

namespace EtherCAT::Core {
class DeviceRepositoryProvider;
class ProjectService;
}

namespace EtherCAT::Scan::Internal {

enum class MockScanScenario {
    Normal,
    Slow,
    PartialFailure,
    DuplicateDevice,
    RevisionMismatch,
};

class MockScanProvider final : public Core::ScanProvider
{
    Q_OBJECT

public:
    explicit MockScanProvider(QObject *parent = nullptr);
    ~MockScanProvider() final;

    Data::ScanState scanState() const final;
    Data::ScanProgress scanProgress() const final;
    std::optional<Data::ScanResult> lastScanResult() const final;
    QString lastScanError() const final;

    Utils::Result<> startScan(const Data::ScanRequest &request) final;
    void cancelScan() final;
    void clearScanResult() final;

    MockScanScenario scenario() const;
    void setScenario(MockScanScenario scenario);
    void setStepIntervalForTests(int milliseconds);
    Utils::Result<> compareWithCurrentProject();
    void shutdown();

signals:
    void scenarioChanged(EtherCAT::Scan::Internal::MockScanScenario scenario);

private:
    bool isActive() const;
    int stepInterval() const;
    void scheduleAdvance();
    void advance();
    void enterState(Data::ScanState state, int value, const QString &detail);
    void discoverNextSlave();
    void buildSnapshot();
    void completeComparison();
    void fail(const QString &error);
    QList<Data::ScannedSlave> scenarioSlaves() const;
    Data::NodeId stableScannedId(
        int position, const Data::DeviceIdentity &identity, quint32 serialNumber) const;

    QPointer<Core::ProjectService> m_projectService;
    QPointer<Core::DeviceRepositoryProvider> m_deviceRepository;
    QTimer m_timer;
    Data::ScanRequest m_request;
    Data::ScanProgress m_progress;
    std::optional<Data::ScanResult> m_result;
    QList<Data::ScannedSlave> m_expectedSlaves;
    QList<Data::ScannedSlave> m_discoveredSlaves;
    QString m_error;
    MockScanScenario m_scenario = MockScanScenario::Normal;
    int m_testInterval = -1;
    bool m_shuttingDown = false;
};

} // namespace EtherCAT::Scan::Internal

Q_DECLARE_METATYPE(EtherCAT::Scan::Internal::MockScanScenario)
