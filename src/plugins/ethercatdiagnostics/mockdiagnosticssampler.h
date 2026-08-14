// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/diagnosticssnapshot.h>

#include <QList>
#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace EtherCAT::Diagnostics::Internal {

enum class MockDiagnosticsScenario {
    Normal,
    WorkingCounterMismatch,
    LinkInterruption,
    DcDrift,
    DeadlinePressure,
    AlarmBurst,
    SourceFailure,
};

struct MockRawSample
{
    quint64 sequence = 0;
    qint64 capturedAtMs = 0;
    quint32 expectedWorkingCounter = 0;
    quint32 actualWorkingCounter = 0;
    qint64 cycleTimeNs = 0;
    qint64 jitterNs = 0;
    qint64 deadlineMarginNs = 0;
    qint64 dcOffsetNs = 0;
    qint64 dcDeviationNs = 0;
    bool linkUp = true;
    bool alarmBurst = false;
    quint64 lostFrameDelta = 0;
    quint64 crcErrorDelta = 0;
    quint64 timeoutDelta = 0;
    quint64 dropDelta = 0;
    quint64 lateDelta = 0;
    quint64 overflowDelta = 0;
};

struct MockSampleBatch
{
    QList<MockRawSample> samples;
};

class MockDiagnosticsSampler final : public QObject
{
    Q_OBJECT

public:
    explicit MockDiagnosticsSampler(QObject *parent = nullptr);

    void start(
        const Data::DiagnosticsLimits &limits,
        MockDiagnosticsScenario scenario,
        quint32 expectedWorkingCounter);
    void stop();
    void setScenario(MockDiagnosticsScenario scenario);

signals:
    void started();
    void sampleBatchReady(const EtherCAT::Diagnostics::Internal::MockSampleBatch &batch);
    void failed(const QString &error);
    void stopped();

private:
    void sample();

    QTimer *m_timer = nullptr;
    QList<MockRawSample> m_pending;
    MockDiagnosticsScenario m_scenario = MockDiagnosticsScenario::Normal;
    quint64 m_sequence = 0;
    quint64 m_publishCount = 0;
    quint32 m_expectedWorkingCounter = 0;
    int m_batchTarget = 1;
    bool m_running = false;
};

} // namespace EtherCAT::Diagnostics::Internal

Q_DECLARE_METATYPE(EtherCAT::Diagnostics::Internal::MockDiagnosticsScenario)
Q_DECLARE_METATYPE(EtherCAT::Diagnostics::Internal::MockRawSample)
Q_DECLARE_METATYPE(EtherCAT::Diagnostics::Internal::MockSampleBatch)
