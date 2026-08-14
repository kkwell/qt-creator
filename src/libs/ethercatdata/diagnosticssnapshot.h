// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

namespace EtherCAT::Data {

enum class EtherCATState {
    Unknown,
    Init,
    PreOperational,
    SafeOperational,
    Operational,
    Bootstrap,
};

enum class DiagnosticsRunMode { Offline, Config, FreeRun, Run };
enum class DiagnosticsStreamState { Stopped, Starting, Running, Stopping, Failed };
enum class WorkingCounterState { Unknown, Valid, Incomplete, Zero, Error };
enum class LinkState { Unknown, Down, Up };
enum class DcSyncState { NotConfigured, Synchronizing, Synchronized, Lost };
enum class DiagnosticSeverity { Information, Warning, Error, Critical };
enum class DiagnosticEventKind {
    StateChange,
    WorkingCounter,
    Link,
    FrameError,
    DistributedClock,
    Cycle,
    Alarm,
};
enum class AlarmLifecycle { NotApplicable, Active, Acknowledged, Recovered };

struct ETHERCATDATA_EXPORT DiagnosticsRequest
{
    NodeId projectId;
    NodeId masterId;

    friend bool operator==(const DiagnosticsRequest &, const DiagnosticsRequest &) = default;
};

struct ETHERCATDATA_EXPORT DiagnosticsLimits
{
    int eventCapacity = 1000;
    int trendCapacity = 1000;
    int samplePeriodMs = 10;
    int publishPeriodMs = 100;

    friend bool operator==(const DiagnosticsLimits &, const DiagnosticsLimits &) = default;
};

struct ETHERCATDATA_EXPORT WorkingCounterDiagnostics
{
    quint32 expected = 0;
    quint32 actual = 0;
    WorkingCounterState state = WorkingCounterState::Unknown;
    quint64 mismatchCount = 0;

    friend bool operator==(
        const WorkingCounterDiagnostics &, const WorkingCounterDiagnostics &) = default;
};

struct ETHERCATDATA_EXPORT FrameErrorCounters
{
    quint64 lostFrames = 0;
    quint64 crcErrors = 0;
    quint64 timeouts = 0;
    quint64 drops = 0;
    quint64 lateFrames = 0;
    quint64 overflows = 0;

    friend bool operator==(const FrameErrorCounters &, const FrameErrorCounters &) = default;
};

struct ETHERCATDATA_EXPORT PortDiagnostics
{
    int port = -1;
    LinkState linkState = LinkState::Unknown;
    bool communicationEstablished = false;
    quint64 interruptionCount = 0;
    quint64 invalidFrameCount = 0;
    quint64 receiveErrorCount = 0;

    friend bool operator==(const PortDiagnostics &, const PortDiagnostics &) = default;
};

struct ETHERCATDATA_EXPORT DcDiagnostics
{
    DcSyncState state = DcSyncState::NotConfigured;
    qint64 offsetNs = 0;
    qint64 maximumAbsoluteOffsetNs = 0;
    qint64 deviationNs = 0;
    quint64 lostSyncCount = 0;

    friend bool operator==(const DcDiagnostics &, const DcDiagnostics &) = default;
};

struct ETHERCATDATA_EXPORT CycleDiagnostics
{
    qint64 nominalCycleNs = 0;
    qint64 lastCycleNs = 0;
    qint64 minimumCycleNs = 0;
    qint64 maximumCycleNs = 0;
    qint64 jitterNs = 0;
    qint64 deadlineMarginNs = 0;
    quint64 sampleCount = 0;
    quint64 missedDeadlineCount = 0;

    friend bool operator==(const CycleDiagnostics &, const CycleDiagnostics &) = default;
};

struct ETHERCATDATA_EXPORT SlaveDiagnostics
{
    NodeId nodeId;
    int position = -1;
    QString name;
    EtherCATState state = EtherCATState::Unknown;
    bool hasError = false;
    quint16 alStatusCode = 0;
    QString alStatusText;
    WorkingCounterState workingCounterState = WorkingCounterState::Unknown;
    QList<PortDiagnostics> ports;
    FrameErrorCounters frameErrors;
    DcDiagnostics distributedClock;
    QDateTime lastSeenAt;

    friend bool operator==(const SlaveDiagnostics &, const SlaveDiagnostics &) = default;
};

struct ETHERCATDATA_EXPORT DiagnosticEvent
{
    NodeId id;
    quint64 sequence = 0;
    QDateTime occurredAt;
    NodeId nodeId;
    DiagnosticEventKind kind = DiagnosticEventKind::StateChange;
    DiagnosticSeverity severity = DiagnosticSeverity::Information;
    AlarmLifecycle lifecycle = AlarmLifecycle::NotApplicable;
    QString code;
    QString summary;
    QString detail;
    quint32 repeatCount = 1;
    QDateTime acknowledgedAt;
    QDateTime recoveredAt;

    friend bool operator==(const DiagnosticEvent &, const DiagnosticEvent &) = default;
};

struct ETHERCATDATA_EXPORT DiagnosticTrendSample
{
    quint64 sequence = 0;
    QDateTime capturedAt;
    quint32 actualWorkingCounter = 0;
    qint64 cycleTimeNs = 0;
    qint64 jitterNs = 0;
    qint64 deadlineMarginNs = 0;
    qint64 dcOffsetNs = 0;

    friend bool operator==(
        const DiagnosticTrendSample &, const DiagnosticTrendSample &) = default;
};

struct ETHERCATDATA_EXPORT DiagnosticsSnapshot
{
    NodeId projectId;
    NodeId masterId;
    quint64 generation = 0;
    QDateTime capturedAt;
    bool mock = false;
    DiagnosticsRunMode runMode = DiagnosticsRunMode::Offline;
    EtherCATState masterState = EtherCATState::Unknown;
    bool masterHasError = false;
    quint16 masterAlStatusCode = 0;
    QString masterAlStatusText;
    WorkingCounterDiagnostics workingCounter;
    QList<PortDiagnostics> masterPorts;
    FrameErrorCounters frameErrors;
    DcDiagnostics distributedClock;
    CycleDiagnostics cycle;
    QList<SlaveDiagnostics> slaves;
    int activeAlarmCount = 0;
    int unacknowledgedAlarmCount = 0;
    quint64 sourceSampleCount = 0;
    quint64 coalescedSampleCount = 0;
    quint64 droppedEventCount = 0;
    quint64 droppedTrendSampleCount = 0;

    friend bool operator==(const DiagnosticsSnapshot &, const DiagnosticsSnapshot &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::EtherCATState)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticsRunMode)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticsStreamState)
Q_DECLARE_METATYPE(EtherCAT::Data::WorkingCounterState)
Q_DECLARE_METATYPE(EtherCAT::Data::LinkState)
Q_DECLARE_METATYPE(EtherCAT::Data::DcSyncState)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticEventKind)
Q_DECLARE_METATYPE(EtherCAT::Data::AlarmLifecycle)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticsRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticsLimits)
Q_DECLARE_METATYPE(EtherCAT::Data::WorkingCounterDiagnostics)
Q_DECLARE_METATYPE(EtherCAT::Data::FrameErrorCounters)
Q_DECLARE_METATYPE(EtherCAT::Data::PortDiagnostics)
Q_DECLARE_METATYPE(EtherCAT::Data::DcDiagnostics)
Q_DECLARE_METATYPE(EtherCAT::Data::CycleDiagnostics)
Q_DECLARE_METATYPE(EtherCAT::Data::SlaveDiagnostics)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticEvent)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticTrendSample)
Q_DECLARE_METATYPE(EtherCAT::Data::DiagnosticsSnapshot)
