// Copyright (C) 2026 Kvell

#include "mockdiagnosticssampler.h"

#include "ethercatdiagnosticstr.h"

#include <QDateTime>
#include <QTimer>

#include <utility>

namespace EtherCAT::Diagnostics::Internal {

constexpr qint64 nominalCycleNs = 125000;

MockDiagnosticsSampler::MockDiagnosticsSampler(QObject *parent)
    : QObject(parent)
{}

void MockDiagnosticsSampler::start(
    const Data::DiagnosticsLimits &limits,
    MockDiagnosticsScenario scenario,
    quint32 expectedWorkingCounter)
{
    if (m_running)
        return;

    m_scenario = scenario;
    m_sequence = 0;
    m_publishCount = 0;
    m_expectedWorkingCounter = expectedWorkingCounter;
    m_pending.clear();
    const int samplePeriod = qMax(1, limits.samplePeriodMs);
    const int publishPeriod = qMax(samplePeriod, limits.publishPeriodMs);
    m_batchTarget = qMax(1, publishPeriod / samplePeriod);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(samplePeriod);
    connect(m_timer, &QTimer::timeout, this, &MockDiagnosticsSampler::sample);
    m_running = true;
    m_timer->start();
    emit started();
}

void MockDiagnosticsSampler::stop()
{
    const bool wasRunning = m_running;
    m_running = false;
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    m_pending.clear();
    if (wasRunning)
        emit stopped();
}

void MockDiagnosticsSampler::setScenario(MockDiagnosticsScenario scenario)
{
    m_scenario = scenario;
}

void MockDiagnosticsSampler::sample()
{
    if (!m_running)
        return;
    if (m_scenario == MockDiagnosticsScenario::SourceFailure && m_publishCount >= 3) {
        m_timer->stop();
        m_running = false;
        m_pending.clear();
        emit failed(Tr::tr("MOCK diagnostics source failure; no hardware was accessed."));
        return;
    }

    ++m_sequence;
    const qint64 wave = qint64(m_sequence % 9) - 4;
    MockRawSample sample;
    sample.sequence = m_sequence;
    sample.capturedAtMs = QDateTime::currentMSecsSinceEpoch();
    sample.expectedWorkingCounter = m_expectedWorkingCounter;
    sample.actualWorkingCounter = m_expectedWorkingCounter;
    sample.jitterNs = wave * 25;
    sample.cycleTimeNs = nominalCycleNs + sample.jitterNs;
    sample.deadlineMarginNs = 24000 - qAbs(sample.jitterNs);
    sample.dcOffsetNs = wave * 8;
    sample.dcDeviationNs = qAbs(wave * 3);

    switch (m_scenario) {
    case MockDiagnosticsScenario::Normal:
    case MockDiagnosticsScenario::SourceFailure:
        break;
    case MockDiagnosticsScenario::WorkingCounterMismatch:
        sample.actualWorkingCounter = m_expectedWorkingCounter > 0
                                          ? m_expectedWorkingCounter - 1
                                          : 0;
        sample.timeoutDelta = 1;
        break;
    case MockDiagnosticsScenario::LinkInterruption:
        sample.linkUp = false;
        sample.lostFrameDelta = 1;
        sample.crcErrorDelta = m_sequence % 3 == 0 ? 1 : 0;
        break;
    case MockDiagnosticsScenario::DcDrift:
        sample.dcOffsetNs = 6000 + wave * 50;
        sample.dcDeviationNs = 750;
        sample.lateDelta = 1;
        break;
    case MockDiagnosticsScenario::DeadlinePressure:
        sample.cycleTimeNs = nominalCycleNs + 32000 + wave * 100;
        sample.jitterNs = sample.cycleTimeNs - nominalCycleNs;
        sample.deadlineMarginNs = -4000 - qAbs(wave * 100);
        sample.dropDelta = 1;
        sample.overflowDelta = m_sequence % 4 == 0 ? 1 : 0;
        break;
    case MockDiagnosticsScenario::AlarmBurst:
        sample.alarmBurst = true;
        sample.crcErrorDelta = 1;
        break;
    }

    m_pending.append(sample);
    if (m_pending.size() < m_batchTarget)
        return;
    ++m_publishCount;
    emit sampleBatchReady({std::exchange(m_pending, {})});
}

} // namespace EtherCAT::Diagnostics::Internal
