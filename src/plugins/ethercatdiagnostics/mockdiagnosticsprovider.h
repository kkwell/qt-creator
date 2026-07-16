// Copyright (C) 2026 Kvell

#pragma once

#include "mockdiagnosticssampler.h"

#include <ethercatcore/providers.h>

#include <QPointer>

QT_BEGIN_NAMESPACE
class QThread;
QT_END_NAMESPACE

namespace EtherCAT::Core {
class ProjectService;
}

namespace EtherCAT::Diagnostics::Internal {

class MockDiagnosticsProvider final : public Core::DiagnosticsProvider
{
    Q_OBJECT

public:
    explicit MockDiagnosticsProvider(QObject *parent = nullptr);
    MockDiagnosticsProvider(
        const Data::DiagnosticsLimits &limits, QObject *parent = nullptr);
    ~MockDiagnosticsProvider() final;

    Data::DiagnosticsStreamState streamState() const final;
    Data::DiagnosticsRequest activeRequest() const final;
    std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const final;
    QList<Data::DiagnosticEvent> events() const final;
    QList<Data::DiagnosticTrendSample> trendSamples() const final;
    Data::DiagnosticsLimits limits() const final;
    QString lastDiagnosticsError() const final;

    Utils::Result<> startMonitoring(const Data::DiagnosticsRequest &request) final;
    void stopMonitoring() final;
    Utils::Result<> requestRunMode(Data::DiagnosticsRunMode mode) final;
    Utils::Result<> acknowledgeAlarm(const Data::NodeId &eventId) final;
    Utils::Result<> clearRecoveredEvents() final;

    MockDiagnosticsScenario scenario() const;
    void setScenario(MockDiagnosticsScenario scenario);
    void addConsumer();
    void removeConsumer();
    int consumerCount() const;
    bool samplerRunning() const;
    void shutdown();

signals:
    void scenarioChanged(
        EtherCAT::Diagnostics::Internal::MockDiagnosticsScenario scenario);
    void consumerCountChanged(int count);

private:
    void initialize();
    void startSampler(quint32 expectedWorkingCounter);
    void stopSampler();
    void handleSamplerStarted();
    void handleSampleBatch(const MockSampleBatch &batch);
    void handleSamplerFailure(const QString &error);
    std::optional<Data::ProjectSnapshot> activeProject() const;
    quint32 expectedWorkingCounter(const Data::ProjectSnapshot &project) const;
    QList<Data::OfflineSlaveConfiguration> configuredSlaves(
        const Data::ProjectSnapshot &project) const;
    void resetSession();
    void publishInitialSnapshot(const Data::ProjectSnapshot &project);
    void publishBatch(
        const MockSampleBatch &batch, const Data::ProjectSnapshot &project);
    void appendTrend(const Data::DiagnosticTrendSample &sample);
    void updateCondition(
        const QString &code,
        bool active,
        Data::DiagnosticEventKind kind,
        Data::DiagnosticSeverity severity,
        const QString &summary,
        const QString &detail,
        const Data::NodeId &nodeId,
        const QDateTime &occurredAt);
    void appendBurstAlarm(const MockRawSample &sample, const Data::NodeId &nodeId);
    void appendEvent(Data::DiagnosticEvent event);
    void refreshAlarmCounts();
    void setStreamState(Data::DiagnosticsStreamState state);

    QPointer<Core::ProjectService> m_projectService;
    QPointer<MockDiagnosticsSampler> m_sampler;
    QThread *m_samplerThread = nullptr;
    Data::DiagnosticsLimits m_limits;
    Data::DiagnosticsStreamState m_streamState
        = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_request;
    std::optional<Data::DiagnosticsSnapshot> m_snapshot;
    QList<Data::DiagnosticEvent> m_events;
    QList<Data::DiagnosticTrendSample> m_trend;
    Data::FrameErrorCounters m_frameErrors;
    MockDiagnosticsScenario m_scenario = MockDiagnosticsScenario::Normal;
    Data::DiagnosticsRunMode m_runMode = Data::DiagnosticsRunMode::Config;
    QString m_error;
    quint64 m_generation = 0;
    quint64 m_sourceSampleCount = 0;
    quint64 m_coalescedSampleCount = 0;
    quint64 m_droppedEventCount = 0;
    quint64 m_droppedTrendSampleCount = 0;
    quint64 m_wkcMismatchCount = 0;
    quint64 m_linkInterruptionCount = 0;
    quint64 m_dcLostSyncCount = 0;
    quint64 m_cycleSampleCount = 0;
    quint64 m_missedDeadlineCount = 0;
    qint64 m_minimumCycleNs = 0;
    qint64 m_maximumCycleNs = 0;
    qint64 m_maximumAbsoluteDcOffsetNs = 0;
    bool m_lastLinkUp = true;
    bool m_lastDcSynchronized = true;
    int m_consumerCount = 0;
    bool m_shuttingDown = false;
};

} // namespace EtherCAT::Diagnostics::Internal
