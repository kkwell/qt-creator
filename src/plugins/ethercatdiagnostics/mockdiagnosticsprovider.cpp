// Copyright (C) 2026 Kvell

#include "mockdiagnosticsprovider.h"

#include "ethercatdiagnosticsconstants.h"
#include "ethercatdiagnosticstr.h"

#include <ethercatcore/ethercatcoresettings.h>

#include <extensionsystem/pluginmanager.h>

#include <QThread>

#include <algorithm>
#include <utility>

namespace EtherCAT::Diagnostics::Internal {

static Data::DiagnosticsLimits productionLimits()
{
    const int capacity = Core::settings().maximumRecentEvents();
    return {capacity, capacity, 10, 100};
}

static Data::DiagnosticsLimits boundedLimits(Data::DiagnosticsLimits limits)
{
    limits.eventCapacity = qMax(1, limits.eventCapacity);
    limits.trendCapacity = qMax(1, limits.trendCapacity);
    limits.samplePeriodMs = qMax(1, limits.samplePeriodMs);
    limits.publishPeriodMs = qMax(limits.samplePeriodMs, limits.publishPeriodMs);
    return limits;
}

static Data::EtherCATState stateForMode(Data::DiagnosticsRunMode mode)
{
    switch (mode) {
    case Data::DiagnosticsRunMode::Offline:
        return Data::EtherCATState::Init;
    case Data::DiagnosticsRunMode::Config:
        return Data::EtherCATState::PreOperational;
    case Data::DiagnosticsRunMode::FreeRun:
        return Data::EtherCATState::SafeOperational;
    case Data::DiagnosticsRunMode::Run:
        return Data::EtherCATState::Operational;
    }
    return Data::EtherCATState::Unknown;
}

static Data::WorkingCounterState workingCounterState(quint32 expected, quint32 actual)
{
    if (expected == actual)
        return Data::WorkingCounterState::Valid;
    if (actual == 0)
        return Data::WorkingCounterState::Zero;
    if (actual < expected)
        return Data::WorkingCounterState::Incomplete;
    return Data::WorkingCounterState::Error;
}

MockDiagnosticsProvider::MockDiagnosticsProvider(QObject *parent)
    : MockDiagnosticsProvider(productionLimits(), parent)
{}

MockDiagnosticsProvider::MockDiagnosticsProvider(
    const Data::DiagnosticsLimits &limits, QObject *parent)
    : Core::DiagnosticsProvider(
          Constants::PROVIDER_ID, Tr::tr("Local Mock EtherCAT diagnostics"), parent)
    , m_limits(boundedLimits(limits))
{
    initialize();
}

MockDiagnosticsProvider::~MockDiagnosticsProvider()
{
    shutdown();
}

void MockDiagnosticsProvider::initialize()
{
    m_projectService = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    if (m_projectService) {
        connect(
            m_projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            [this](const Data::NodeId &projectId) {
                if (m_request.projectId == projectId)
                    stopMonitoring();
            });
        connect(
            m_projectService,
            &Core::ProjectService::projectChanged,
            this,
            [this](const Data::ProjectSnapshot &project) {
                if (m_request.projectId == project.id)
                    stopMonitoring();
            });
    }
    setAvailable(bool(m_projectService));
}

Data::DiagnosticsStreamState MockDiagnosticsProvider::streamState() const
{
    return m_streamState;
}

Data::DiagnosticsRequest MockDiagnosticsProvider::activeRequest() const
{
    return m_request;
}

std::optional<Data::DiagnosticsSnapshot> MockDiagnosticsProvider::latestSnapshot() const
{
    return m_snapshot;
}

QList<Data::DiagnosticEvent> MockDiagnosticsProvider::events() const
{
    return m_events;
}

QList<Data::DiagnosticTrendSample> MockDiagnosticsProvider::trendSamples() const
{
    return m_trend;
}

Data::DiagnosticsLimits MockDiagnosticsProvider::limits() const
{
    return m_limits;
}

QString MockDiagnosticsProvider::lastDiagnosticsError() const
{
    return m_error;
}

Utils::Result<> MockDiagnosticsProvider::startMonitoring(
    const Data::DiagnosticsRequest &request)
{
    if (m_shuttingDown || !isAvailable()) {
        return Utils::ResultError(
            Tr::tr("The local Mock diagnostics provider is unavailable."));
    }
    if (m_streamState != Data::DiagnosticsStreamState::Stopped) {
        return Utils::ResultError(
            Tr::tr("A local Mock diagnostics session is already active."));
    }
    if (request.projectId.isNull() || request.masterId.isNull()) {
        return Utils::ResultError(
            Tr::tr("Select an EtherCAT project or master before monitoring."));
    }
    const std::optional<Data::ProjectSnapshot> project
        = m_projectService ? m_projectService->project(request.projectId) : std::nullopt;
    if (!project)
        return Utils::ResultError(Tr::tr("The selected EtherCAT project is not open."));
    const bool masterExists = std::any_of(
        project->nodes.cbegin(), project->nodes.cend(), [&request](const auto &node) {
            return node.id == request.masterId
                   && node.kind == Data::ProjectNodeKind::Master;
        });
    if (!masterExists)
        return Utils::ResultError(Tr::tr("The selected EtherCAT master no longer exists."));

    resetSession();
    m_request = request;
    setStreamState(Data::DiagnosticsStreamState::Starting);
    publishInitialSnapshot(*project);
    startSampler(expectedWorkingCounter(*project));
    return Utils::ResultOk;
}

void MockDiagnosticsProvider::stopMonitoring()
{
    if (m_streamState == Data::DiagnosticsStreamState::Stopped)
        return;
    const bool alreadyFinished = m_streamState == Data::DiagnosticsStreamState::Failed;
    setStreamState(Data::DiagnosticsStreamState::Stopping);
    stopSampler();
    m_request = {};
    if (m_snapshot) {
        m_snapshot->runMode = Data::DiagnosticsRunMode::Offline;
        m_snapshot->masterState = Data::EtherCATState::Init;
        m_snapshot->capturedAt = QDateTime::currentDateTimeUtc();
        emit diagnosticsSnapshotChanged();
    }
    setStreamState(Data::DiagnosticsStreamState::Stopped);
    if (!alreadyFinished)
        emit monitoringStopped();
}

Utils::Result<> MockDiagnosticsProvider::requestRunMode(Data::DiagnosticsRunMode mode)
{
    if (m_streamState != Data::DiagnosticsStreamState::Running || !m_snapshot) {
        return Utils::ResultError(
            Tr::tr("Start local Mock diagnostics before changing its run mode."));
    }
    if (mode == Data::DiagnosticsRunMode::Offline) {
        return Utils::ResultError(
            Tr::tr("Use Stop Mock Monitoring to return to Offline mode."));
    }
    if (m_runMode == mode)
        return Utils::ResultOk;
    m_runMode = mode;
    m_snapshot->runMode = mode;
    m_snapshot->masterState = stateForMode(mode);
    for (Data::SlaveDiagnostics &slave : m_snapshot->slaves)
        slave.state = stateForMode(mode);
    m_snapshot->capturedAt = QDateTime::currentDateTimeUtc();
    Data::DiagnosticEvent event;
    event.id = Data::NodeId::create();
    event.sequence = m_generation + quint64(m_events.size()) + 1;
    event.occurredAt = m_snapshot->capturedAt;
    event.nodeId = m_request.masterId;
    event.kind = Data::DiagnosticEventKind::StateChange;
    event.severity = Data::DiagnosticSeverity::Information;
    event.lifecycle = Data::AlarmLifecycle::NotApplicable;
    event.code = QString("MOCK-MODE-%1").arg(int(mode));
    event.summary = Tr::tr("MOCK run mode changed");
    event.detail = Tr::tr(
        "Local simulation entered mode %1; no controller command was sent.")
                       .arg(int(mode));
    appendEvent(event);
    emit diagnosticsSnapshotChanged();
    return Utils::ResultOk;
}

Utils::Result<> MockDiagnosticsProvider::acknowledgeAlarm(const Data::NodeId &eventId)
{
    const auto event = std::find_if(
        m_events.begin(), m_events.end(), [&eventId](const Data::DiagnosticEvent &candidate) {
            return candidate.id == eventId
                   && candidate.lifecycle == Data::AlarmLifecycle::Active;
        });
    if (event == m_events.end())
        return Utils::ResultError(Tr::tr("The selected active Mock alarm was not found."));
    event->lifecycle = Data::AlarmLifecycle::Acknowledged;
    event->acknowledgedAt = QDateTime::currentDateTimeUtc();
    refreshAlarmCounts();
    emit diagnosticEventsChanged();
    emit diagnosticsSnapshotChanged();
    return Utils::ResultOk;
}

Utils::Result<> MockDiagnosticsProvider::clearRecoveredEvents()
{
    const qsizetype previousSize = m_events.size();
    m_events.removeIf([](const Data::DiagnosticEvent &event) {
        return event.lifecycle == Data::AlarmLifecycle::Recovered;
    });
    if (m_events.size() == previousSize)
        return Utils::ResultOk;
    refreshAlarmCounts();
    emit diagnosticEventsChanged();
    emit diagnosticsSnapshotChanged();
    return Utils::ResultOk;
}

MockDiagnosticsScenario MockDiagnosticsProvider::scenario() const
{
    return m_scenario;
}

void MockDiagnosticsProvider::setScenario(MockDiagnosticsScenario scenario)
{
    if (m_scenario == scenario)
        return;
    m_scenario = scenario;
    if (m_sampler && m_samplerThread && m_samplerThread->isRunning()) {
        const QPointer<MockDiagnosticsSampler> sampler = m_sampler;
        QMetaObject::invokeMethod(
            m_sampler,
            [sampler, scenario] {
                if (sampler)
                    sampler->setScenario(scenario);
            },
            Qt::QueuedConnection);
    }
    emit scenarioChanged(m_scenario);
}

void MockDiagnosticsProvider::addConsumer()
{
    ++m_consumerCount;
    emit consumerCountChanged(m_consumerCount);
}

void MockDiagnosticsProvider::removeConsumer()
{
    if (m_consumerCount <= 0)
        return;
    --m_consumerCount;
    emit consumerCountChanged(m_consumerCount);
    if (m_consumerCount == 0
        && m_streamState != Data::DiagnosticsStreamState::Stopped
        && m_streamState != Data::DiagnosticsStreamState::Failed) {
        stopMonitoring();
    }
}

int MockDiagnosticsProvider::consumerCount() const
{
    return m_consumerCount;
}

bool MockDiagnosticsProvider::samplerRunning() const
{
    return m_samplerThread && m_samplerThread->isRunning();
}

void MockDiagnosticsProvider::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    stopSampler();
    m_request = {};
    m_streamState = Data::DiagnosticsStreamState::Stopped;
    setAvailable(false);
}

void MockDiagnosticsProvider::startSampler(quint32 expectedWorkingCounter)
{
    auto thread = new QThread;
    auto sampler = new MockDiagnosticsSampler;
    sampler->moveToThread(thread);
    m_samplerThread = thread;
    m_sampler = sampler;

    const Data::DiagnosticsLimits limitsCopy = m_limits;
    const MockDiagnosticsScenario scenarioCopy = m_scenario;
    connect(thread, &QThread::finished, sampler, &QObject::deleteLater);
    connect(thread, &QThread::started, sampler, [sampler,
                                                limitsCopy,
                                                scenarioCopy,
                                                expectedWorkingCounter] {
        sampler->start(limitsCopy, scenarioCopy, expectedWorkingCounter);
    });
    connect(
        sampler,
        &MockDiagnosticsSampler::started,
        this,
        &MockDiagnosticsProvider::handleSamplerStarted,
        Qt::QueuedConnection);
    connect(
        sampler,
        &MockDiagnosticsSampler::sampleBatchReady,
        this,
        &MockDiagnosticsProvider::handleSampleBatch,
        Qt::QueuedConnection);
    connect(
        sampler,
        &MockDiagnosticsSampler::failed,
        this,
        &MockDiagnosticsProvider::handleSamplerFailure,
        Qt::QueuedConnection);
    thread->start();
}

void MockDiagnosticsProvider::stopSampler()
{
    if (!m_samplerThread)
        return;
    if (m_sampler && m_samplerThread->isRunning()) {
        const QPointer<MockDiagnosticsSampler> sampler = m_sampler;
        QMetaObject::invokeMethod(
            m_sampler,
            [sampler] {
                if (sampler)
                    sampler->stop();
            },
            Qt::BlockingQueuedConnection);
    }
    m_samplerThread->quit();
    m_samplerThread->wait();
    delete m_samplerThread;
    m_samplerThread = nullptr;
    m_sampler = nullptr;
}

void MockDiagnosticsProvider::handleSamplerStarted()
{
    if (m_streamState == Data::DiagnosticsStreamState::Starting)
        setStreamState(Data::DiagnosticsStreamState::Running);
}

void MockDiagnosticsProvider::handleSampleBatch(const MockSampleBatch &batch)
{
    if (m_streamState != Data::DiagnosticsStreamState::Running || batch.samples.isEmpty())
        return;
    const std::optional<Data::ProjectSnapshot> project = activeProject();
    if (!project) {
        handleSamplerFailure(
            Tr::tr("The monitored EtherCAT project was closed during Mock diagnostics."));
        return;
    }
    publishBatch(batch, *project);
}

void MockDiagnosticsProvider::handleSamplerFailure(const QString &error)
{
    if (m_streamState == Data::DiagnosticsStreamState::Stopped
        || m_streamState == Data::DiagnosticsStreamState::Failed) {
        return;
    }
    m_error = error;
    stopSampler();
    setStreamState(Data::DiagnosticsStreamState::Failed);
    emit monitoringStopped();
}

std::optional<Data::ProjectSnapshot> MockDiagnosticsProvider::activeProject() const
{
    return m_projectService && !m_request.projectId.isNull()
               ? m_projectService->project(m_request.projectId)
               : std::nullopt;
}

quint32 MockDiagnosticsProvider::expectedWorkingCounter(
    const Data::ProjectSnapshot &project) const
{
    return quint32(configuredSlaves(project).size() * 2);
}

QList<Data::OfflineSlaveConfiguration> MockDiagnosticsProvider::configuredSlaves(
    const Data::ProjectSnapshot &project) const
{
    QList<Data::OfflineSlaveConfiguration> slaves;
    for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
        if (slave.masterId == m_request.masterId)
            slaves.append(slave);
    }
    std::sort(slaves.begin(), slaves.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return slaves;
}

void MockDiagnosticsProvider::resetSession()
{
    stopSampler();
    m_snapshot.reset();
    m_events.clear();
    m_trend.clear();
    m_frameErrors = {};
    m_error.clear();
    m_generation = 0;
    m_sourceSampleCount = 0;
    m_coalescedSampleCount = 0;
    m_droppedEventCount = 0;
    m_droppedTrendSampleCount = 0;
    m_wkcMismatchCount = 0;
    m_linkInterruptionCount = 0;
    m_dcLostSyncCount = 0;
    m_cycleSampleCount = 0;
    m_missedDeadlineCount = 0;
    m_minimumCycleNs = 0;
    m_maximumCycleNs = 0;
    m_maximumAbsoluteDcOffsetNs = 0;
    m_lastLinkUp = true;
    m_lastDcSynchronized = true;
    m_runMode = Data::DiagnosticsRunMode::Config;
}

void MockDiagnosticsProvider::publishInitialSnapshot(
    const Data::ProjectSnapshot &project)
{
    Data::DiagnosticsSnapshot snapshot;
    snapshot.projectId = m_request.projectId;
    snapshot.masterId = m_request.masterId;
    snapshot.generation = ++m_generation;
    snapshot.capturedAt = QDateTime::currentDateTimeUtc();
    snapshot.mock = true;
    snapshot.runMode = Data::DiagnosticsRunMode::Config;
    snapshot.masterState = Data::EtherCATState::Init;
    snapshot.workingCounter.expected = expectedWorkingCounter(project);
    snapshot.workingCounter.actual = 0;
    snapshot.workingCounter.state = Data::WorkingCounterState::Unknown;
    snapshot.masterPorts = {{0, Data::LinkState::Unknown, false, 0, 0, 0},
                            {1, Data::LinkState::Unknown, false, 0, 0, 0}};
    snapshot.distributedClock.state = Data::DcSyncState::Synchronizing;
    snapshot.cycle.nominalCycleNs = 125000;
    m_snapshot = snapshot;
    emit diagnosticsSnapshotChanged();
}

void MockDiagnosticsProvider::publishBatch(
    const MockSampleBatch &batch, const Data::ProjectSnapshot &project)
{
    const MockRawSample &latest = batch.samples.constLast();
    m_sourceSampleCount += quint64(batch.samples.size());
    m_coalescedSampleCount += quint64(qMax(0, batch.samples.size() - 1));
    for (const MockRawSample &sample : batch.samples) {
        m_frameErrors.lostFrames += sample.lostFrameDelta;
        m_frameErrors.crcErrors += sample.crcErrorDelta;
        m_frameErrors.timeouts += sample.timeoutDelta;
        m_frameErrors.drops += sample.dropDelta;
        m_frameErrors.lateFrames += sample.lateDelta;
        m_frameErrors.overflows += sample.overflowDelta;
        if (sample.actualWorkingCounter != sample.expectedWorkingCounter)
            ++m_wkcMismatchCount;
        if (m_lastLinkUp && !sample.linkUp)
            ++m_linkInterruptionCount;
        m_lastLinkUp = sample.linkUp;
        const bool dcSynchronized = qAbs(sample.dcOffsetNs) <= 5000;
        if (m_lastDcSynchronized && !dcSynchronized)
            ++m_dcLostSyncCount;
        m_lastDcSynchronized = dcSynchronized;
        if (sample.deadlineMarginNs < 0)
            ++m_missedDeadlineCount;
        ++m_cycleSampleCount;
        if (m_minimumCycleNs == 0 || sample.cycleTimeNs < m_minimumCycleNs)
            m_minimumCycleNs = sample.cycleTimeNs;
        m_maximumCycleNs = qMax(m_maximumCycleNs, sample.cycleTimeNs);
        m_maximumAbsoluteDcOffsetNs = qMax(
            m_maximumAbsoluteDcOffsetNs, qAbs(sample.dcOffsetNs));
    }

    const QDateTime capturedAt = QDateTime::fromMSecsSinceEpoch(
        latest.capturedAtMs, Qt::UTC);
    const QList<Data::OfflineSlaveConfiguration> offlineSlaves = configuredSlaves(project);
    const Data::NodeId faultNode = offlineSlaves.isEmpty() ? m_request.masterId
                                                           : offlineSlaves.first().id;
    const bool wkcFault
        = latest.actualWorkingCounter != latest.expectedWorkingCounter;
    const bool linkFault = !latest.linkUp;
    const bool dcFault = qAbs(latest.dcOffsetNs) > 5000;
    const bool deadlineFault = latest.deadlineMarginNs < 0;
    const bool frameFault = latest.crcErrorDelta > 0 || latest.lostFrameDelta > 0;
    updateCondition(
        "MOCK-WKC",
        wkcFault,
        Data::DiagnosticEventKind::WorkingCounter,
        Data::DiagnosticSeverity::Error,
        Tr::tr("MOCK working counter mismatch"),
        Tr::tr("Expected %1, actual %2; local simulation only.")
            .arg(latest.expectedWorkingCounter)
            .arg(latest.actualWorkingCounter),
        faultNode,
        capturedAt);
    updateCondition(
        "MOCK-LINK",
        linkFault,
        Data::DiagnosticEventKind::Link,
        Data::DiagnosticSeverity::Error,
        Tr::tr("MOCK link interruption"),
        Tr::tr("Simulated port 0 link down; no physical link was sampled."),
        faultNode,
        capturedAt);
    updateCondition(
        "MOCK-DC",
        dcFault,
        Data::DiagnosticEventKind::DistributedClock,
        Data::DiagnosticSeverity::Warning,
        Tr::tr("MOCK DC synchronization lost"),
        Tr::tr("Simulated DC offset is %1 ns.").arg(latest.dcOffsetNs),
        faultNode,
        capturedAt);
    updateCondition(
        "MOCK-DEADLINE",
        deadlineFault,
        Data::DiagnosticEventKind::Cycle,
        Data::DiagnosticSeverity::Critical,
        Tr::tr("MOCK cycle deadline missed"),
        Tr::tr("Simulated deadline margin is %1 ns.").arg(latest.deadlineMarginNs),
        m_request.masterId,
        capturedAt);
    updateCondition(
        "MOCK-FRAME",
        frameFault,
        Data::DiagnosticEventKind::FrameError,
        Data::DiagnosticSeverity::Warning,
        Tr::tr("MOCK frame errors detected"),
        Tr::tr("Simulated frame counters changed; no EtherCAT frame was received."),
        faultNode,
        capturedAt);
    if (latest.alarmBurst)
        appendBurstAlarm(latest, faultNode);

    appendTrend({latest.sequence,
                 capturedAt,
                 latest.actualWorkingCounter,
                 latest.cycleTimeNs,
                 latest.jitterNs,
                 latest.deadlineMarginNs,
                 latest.dcOffsetNs});

    Data::DiagnosticsSnapshot snapshot;
    snapshot.projectId = m_request.projectId;
    snapshot.masterId = m_request.masterId;
    snapshot.generation = ++m_generation;
    snapshot.capturedAt = capturedAt;
    snapshot.mock = true;
    snapshot.runMode = m_runMode;
    snapshot.masterState = stateForMode(m_runMode);
    snapshot.masterHasError = wkcFault || linkFault || deadlineFault;
    snapshot.masterAlStatusCode = snapshot.masterHasError ? quint16(0xffff) : 0;
    snapshot.masterAlStatusText = snapshot.masterHasError
                                      ? Tr::tr("MOCK simulated error; not a hardware AL Status")
                                      : Tr::tr("MOCK no simulated AL error");
    snapshot.workingCounter = {latest.expectedWorkingCounter,
                               latest.actualWorkingCounter,
                               workingCounterState(
                                   latest.expectedWorkingCounter,
                                   latest.actualWorkingCounter),
                               m_wkcMismatchCount};
    snapshot.masterPorts = {{0,
                             latest.linkUp ? Data::LinkState::Up : Data::LinkState::Down,
                             latest.linkUp,
                             m_linkInterruptionCount,
                             m_frameErrors.crcErrors,
                             m_frameErrors.lostFrames},
                            {1, Data::LinkState::Up, true, 0, 0, 0}};
    snapshot.frameErrors = m_frameErrors;
    snapshot.distributedClock = {
        dcFault ? Data::DcSyncState::Lost : Data::DcSyncState::Synchronized,
        latest.dcOffsetNs,
        m_maximumAbsoluteDcOffsetNs,
        latest.dcDeviationNs,
        m_dcLostSyncCount};
    snapshot.cycle = {125000,
                      latest.cycleTimeNs,
                      m_minimumCycleNs,
                      m_maximumCycleNs,
                      latest.jitterNs,
                      latest.deadlineMarginNs,
                      m_cycleSampleCount,
                      m_missedDeadlineCount};
    for (const Data::OfflineSlaveConfiguration &offline : offlineSlaves) {
        const bool slaveFault = offline.id == faultNode
                                && (wkcFault || linkFault || dcFault || frameFault);
        Data::SlaveDiagnostics slave;
        slave.nodeId = offline.id;
        slave.position = offline.position;
        slave.name = offline.name;
        slave.state = stateForMode(m_runMode);
        slave.hasError = slaveFault;
        slave.alStatusCode = slaveFault ? quint16(0xffff) : 0;
        slave.alStatusText = slaveFault
                                 ? Tr::tr("MOCK simulated slave error")
                                 : Tr::tr("MOCK no simulated AL error");
        slave.workingCounterState = slaveFault
                                        ? snapshot.workingCounter.state
                                        : Data::WorkingCounterState::Valid;
        slave.ports = {{0,
                        offline.id == faultNode
                            ? snapshot.masterPorts.first().linkState
                            : Data::LinkState::Up,
                        offline.id != faultNode || latest.linkUp,
                        offline.id == faultNode ? m_linkInterruptionCount : 0,
                        offline.id == faultNode ? m_frameErrors.crcErrors : 0,
                        offline.id == faultNode ? m_frameErrors.lostFrames : 0}};
        slave.frameErrors = offline.id == faultNode ? m_frameErrors
                                                     : Data::FrameErrorCounters();
        slave.distributedClock = offline.id == faultNode
                                     ? snapshot.distributedClock
                                     : Data::DcDiagnostics{
                                           Data::DcSyncState::Synchronized, 0, 0, 0, 0};
        slave.lastSeenAt = capturedAt;
        snapshot.slaves.append(slave);
    }
    snapshot.sourceSampleCount = m_sourceSampleCount;
    snapshot.coalescedSampleCount = m_coalescedSampleCount;
    snapshot.droppedEventCount = m_droppedEventCount;
    snapshot.droppedTrendSampleCount = m_droppedTrendSampleCount;
    m_snapshot = snapshot;
    refreshAlarmCounts();
    emit diagnosticsSnapshotChanged();
    emit diagnosticTrendChanged();
}

void MockDiagnosticsProvider::appendTrend(const Data::DiagnosticTrendSample &sample)
{
    while (m_trend.size() >= m_limits.trendCapacity) {
        m_trend.removeFirst();
        ++m_droppedTrendSampleCount;
    }
    m_trend.append(sample);
}

void MockDiagnosticsProvider::updateCondition(
    const QString &code,
    bool active,
    Data::DiagnosticEventKind kind,
    Data::DiagnosticSeverity severity,
    const QString &summary,
    const QString &detail,
    const Data::NodeId &nodeId,
    const QDateTime &occurredAt)
{
    const auto event = std::find_if(
        m_events.rbegin(),
        m_events.rend(),
        [&code, &nodeId](const Data::DiagnosticEvent &candidate) {
            return candidate.code == code && candidate.nodeId == nodeId
                   && candidate.lifecycle != Data::AlarmLifecycle::Recovered;
        });
    if (active) {
        if (event != m_events.rend()) {
            ++event->repeatCount;
            event->detail = detail;
            emit diagnosticEventsChanged();
            return;
        }
        Data::DiagnosticEvent newEvent;
        newEvent.id = Data::NodeId::create();
        newEvent.sequence = m_generation + quint64(m_events.size()) + 1;
        newEvent.occurredAt = occurredAt;
        newEvent.nodeId = nodeId;
        newEvent.kind = kind;
        newEvent.severity = severity;
        newEvent.lifecycle = Data::AlarmLifecycle::Active;
        newEvent.code = code;
        newEvent.summary = summary;
        newEvent.detail = detail;
        appendEvent(newEvent);
        return;
    }
    if (event == m_events.rend())
        return;
    event->lifecycle = Data::AlarmLifecycle::Recovered;
    event->recoveredAt = occurredAt;
    emit diagnosticEventsChanged();
}

void MockDiagnosticsProvider::appendBurstAlarm(
    const MockRawSample &sample, const Data::NodeId &nodeId)
{
    Data::DiagnosticEvent event;
    event.id = Data::NodeId::create();
    event.sequence = sample.sequence;
    event.occurredAt = QDateTime::fromMSecsSinceEpoch(sample.capturedAtMs, Qt::UTC);
    event.nodeId = nodeId;
    event.kind = Data::DiagnosticEventKind::Alarm;
    event.severity = Data::DiagnosticSeverity::Warning;
    event.lifecycle = Data::AlarmLifecycle::Active;
    event.code = QString("MOCK-BURST-%1").arg(sample.sequence);
    event.summary = Tr::tr("MOCK generated alarm burst");
    event.detail = Tr::tr("Synthetic alarm for bounded-history verification.");
    appendEvent(event);
}

void MockDiagnosticsProvider::appendEvent(Data::DiagnosticEvent event)
{
    while (m_events.size() >= m_limits.eventCapacity) {
        m_events.removeFirst();
        ++m_droppedEventCount;
    }
    m_events.append(std::move(event));
    emit diagnosticEventsChanged();
}

void MockDiagnosticsProvider::refreshAlarmCounts()
{
    if (!m_snapshot)
        return;
    int active = 0;
    int unacknowledged = 0;
    for (const Data::DiagnosticEvent &event : std::as_const(m_events)) {
        if (event.lifecycle == Data::AlarmLifecycle::Active) {
            ++active;
            ++unacknowledged;
        } else if (event.lifecycle == Data::AlarmLifecycle::Acknowledged) {
            ++active;
        }
    }
    m_snapshot->activeAlarmCount = active;
    m_snapshot->unacknowledgedAlarmCount = unacknowledged;
    m_snapshot->droppedEventCount = m_droppedEventCount;
    m_snapshot->droppedTrendSampleCount = m_droppedTrendSampleCount;
}

void MockDiagnosticsProvider::setStreamState(Data::DiagnosticsStreamState state)
{
    if (m_streamState == state)
        return;
    m_streamState = state;
    emit streamStateChanged(m_streamState);
}

} // namespace EtherCAT::Diagnostics::Internal
