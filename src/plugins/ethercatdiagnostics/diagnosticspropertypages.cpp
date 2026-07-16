// Copyright (C) 2026 Kvell

#include "diagnosticspropertypages.h"

#include "diagnosticsworkflow.h"
#include "ethercatdiagnosticsconstants.h"
#include "ethercatdiagnosticstr.h"
#include "mockdiagnosticsprovider.h"

#include <utils/stylehelper.h>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace EtherCAT::Diagnostics::Internal {

enum class DiagnosticsPageKind {
    Master,
    Slaves,
    SlaveOnline,
    WorkingCounter,
    DistributedClock,
    Ports,
    Counters,
    Events,
    Performance,
};

static QString streamStateName(Data::DiagnosticsStreamState state)
{
    switch (state) {
    case Data::DiagnosticsStreamState::Stopped:
        return Tr::tr("Stopped");
    case Data::DiagnosticsStreamState::Starting:
        return Tr::tr("Starting");
    case Data::DiagnosticsStreamState::Running:
        return Tr::tr("Running");
    case Data::DiagnosticsStreamState::Stopping:
        return Tr::tr("Stopping");
    case Data::DiagnosticsStreamState::Failed:
        return Tr::tr("Failed");
    }
    return Tr::tr("Unknown");
}

static QString runModeName(Data::DiagnosticsRunMode mode)
{
    switch (mode) {
    case Data::DiagnosticsRunMode::Offline:
        return Tr::tr("Offline");
    case Data::DiagnosticsRunMode::Config:
        return Tr::tr("Mock Config");
    case Data::DiagnosticsRunMode::FreeRun:
        return Tr::tr("Mock FreeRun");
    case Data::DiagnosticsRunMode::Run:
        return Tr::tr("Mock Run / OP");
    }
    return Tr::tr("Unknown");
}

static QString etherCATStateName(Data::EtherCATState state)
{
    switch (state) {
    case Data::EtherCATState::Unknown:
        return Tr::tr("Unknown");
    case Data::EtherCATState::Init:
        return Tr::tr("INIT");
    case Data::EtherCATState::PreOperational:
        return Tr::tr("PREOP");
    case Data::EtherCATState::SafeOperational:
        return Tr::tr("SAFEOP");
    case Data::EtherCATState::Operational:
        return Tr::tr("OP");
    case Data::EtherCATState::Bootstrap:
        return Tr::tr("BOOTSTRAP");
    }
    return Tr::tr("Unknown");
}

static QString workingCounterStateName(Data::WorkingCounterState state)
{
    switch (state) {
    case Data::WorkingCounterState::Unknown:
        return Tr::tr("Unknown");
    case Data::WorkingCounterState::Valid:
        return Tr::tr("Valid");
    case Data::WorkingCounterState::Incomplete:
        return Tr::tr("Incomplete");
    case Data::WorkingCounterState::Zero:
        return Tr::tr("Zero");
    case Data::WorkingCounterState::Error:
        return Tr::tr("Error");
    }
    return Tr::tr("Unknown");
}

static QString linkStateName(Data::LinkState state)
{
    switch (state) {
    case Data::LinkState::Unknown:
        return Tr::tr("Unknown");
    case Data::LinkState::Down:
        return Tr::tr("Down");
    case Data::LinkState::Up:
        return Tr::tr("Up");
    }
    return Tr::tr("Unknown");
}

static QString dcStateName(Data::DcSyncState state)
{
    switch (state) {
    case Data::DcSyncState::NotConfigured:
        return Tr::tr("Not configured");
    case Data::DcSyncState::Synchronizing:
        return Tr::tr("Synchronizing");
    case Data::DcSyncState::Synchronized:
        return Tr::tr("Synchronized");
    case Data::DcSyncState::Lost:
        return Tr::tr("Lost");
    }
    return Tr::tr("Unknown");
}

static QString lifecycleName(Data::AlarmLifecycle lifecycle)
{
    switch (lifecycle) {
    case Data::AlarmLifecycle::NotApplicable:
        return Tr::tr("Event");
    case Data::AlarmLifecycle::Active:
        return Tr::tr("Active");
    case Data::AlarmLifecycle::Acknowledged:
        return Tr::tr("Acknowledged");
    case Data::AlarmLifecycle::Recovered:
        return Tr::tr("Recovered");
    }
    return Tr::tr("Unknown");
}

static QString severityName(Data::DiagnosticSeverity severity)
{
    switch (severity) {
    case Data::DiagnosticSeverity::Information:
        return Tr::tr("Information");
    case Data::DiagnosticSeverity::Warning:
        return Tr::tr("Warning");
    case Data::DiagnosticSeverity::Error:
        return Tr::tr("Error");
    case Data::DiagnosticSeverity::Critical:
        return Tr::tr("Critical");
    }
    return Tr::tr("Unknown");
}

static QString scenarioName(MockDiagnosticsScenario scenario)
{
    switch (scenario) {
    case MockDiagnosticsScenario::Normal:
        return Tr::tr("Normal Mock data");
    case MockDiagnosticsScenario::WorkingCounterMismatch:
        return Tr::tr("Mock WKC mismatch");
    case MockDiagnosticsScenario::LinkInterruption:
        return Tr::tr("Mock link interruption");
    case MockDiagnosticsScenario::DcDrift:
        return Tr::tr("Mock DC drift");
    case MockDiagnosticsScenario::DeadlinePressure:
        return Tr::tr("Mock deadline pressure");
    case MockDiagnosticsScenario::AlarmBurst:
        return Tr::tr("Mock alarm burst");
    case MockDiagnosticsScenario::SourceFailure:
        return Tr::tr("Mock source failure");
    }
    return {};
}

static QString counterText(quint64 value)
{
    return QString::number(value);
}

class TrendWidget final : public QWidget
{
public:
    explicit TrendWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("EtherCATMockDiagnosticsTrend");
    }

    void setSamples(const QList<Data::DiagnosticTrendSample> &samples)
    {
        if (m_samples == samples)
            return;
        m_samples = samples;
        update();
    }

    QSize sizeHint() const final
    {
        return {fontMetrics().horizontalAdvance(QString(48, QLatin1Char('0'))),
                fontMetrics().height() * 10};
    }

private:
    void paintEvent(QPaintEvent *) final
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        painter.setRenderHint(QPainter::Antialiasing);
        const int horizontalPadding = Utils::StyleHelper::SpacingTokens::PaddingHM;
        const int verticalPadding = Utils::StyleHelper::SpacingTokens::PaddingVM;
        const int titleHeight = fontMetrics().height();
        const QRect plot = rect().adjusted(
            horizontalPadding,
            verticalPadding + titleHeight,
            -horizontalPadding,
            -verticalPadding);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
            rect().adjusted(horizontalPadding, verticalPadding, 0, 0),
            Qt::AlignLeft | Qt::AlignTop,
            Tr::tr("MOCK cycle jitter trend (ns)"));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(plot);
        if (m_samples.isEmpty() || plot.width() <= 0 || plot.height() <= 0)
            return;

        qint64 minimum = m_samples.first().jitterNs;
        qint64 maximum = minimum;
        for (const Data::DiagnosticTrendSample &sample : m_samples) {
            minimum = qMin(minimum, sample.jitterNs);
            maximum = qMax(maximum, sample.jitterNs);
        }
        if (minimum == maximum) {
            --minimum;
            ++maximum;
        }
        QPolygonF points;
        points.reserve(m_samples.size());
        for (qsizetype index = 0; index < m_samples.size(); ++index) {
            const qreal x = m_samples.size() == 1
                                ? plot.center().x()
                                : plot.left()
                                      + qreal(index) * plot.width()
                                            / qreal(m_samples.size() - 1);
            const qreal normalized = qreal(m_samples.at(index).jitterNs - minimum)
                                     / qreal(maximum - minimum);
            const qreal y = plot.bottom() - normalized * plot.height();
            points.append({x, y});
        }
        painter.setPen(palette().color(QPalette::Highlight));
        painter.drawPolyline(points);
    }

    QList<Data::DiagnosticTrendSample> m_samples;
};

class DiagnosticsPageWidget final : public QWidget
{
public:
    DiagnosticsPageWidget(
        DiagnosticsPageKind kind,
        MockDiagnosticsProvider *provider,
        DiagnosticsWorkflow *workflow,
        QWidget *parent)
        : QWidget(parent)
        , m_kind(kind)
        , m_provider(provider)
        , m_workflow(workflow)
        , m_banner(new QLabel(this))
        , m_context(new QLabel(this))
        , m_state(new QLabel(this))
        , m_scenario(new QComboBox(this))
        , m_table(new QTreeWidget(this))
    {
        setObjectName("EtherCATMockDiagnosticsPage");
        setProperty("EtherCAT.Diagnostics.Page", true);
        m_banner->setObjectName("EtherCATMockDiagnosticsBanner");
        m_banner->setText(
            Tr::tr("MOCK DIAGNOSTICS - local synthetic data only; "
                   "no EtherCAT hardware or controller is connected."));
        m_banner->setWordWrap(true);
        m_banner->setFont(Utils::StyleHelper::uiFont(Utils::StyleHelper::UiElementH5));
        m_context->setObjectName("EtherCATMockDiagnosticsContext");
        m_context->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_state->setObjectName("EtherCATMockDiagnosticsState");
        m_state->setWordWrap(true);
        m_table->setObjectName("EtherCATMockDiagnosticsTable");
        m_table->setAlternatingRowColors(true);
        m_table->setRootIsDecorated(false);
        m_table->setUniformRowHeights(true);
        m_table->header()->setStretchLastSection(true);
        for (int value = int(MockDiagnosticsScenario::Normal);
             value <= int(MockDiagnosticsScenario::SourceFailure);
             ++value) {
            const MockDiagnosticsScenario scenario = MockDiagnosticsScenario(value);
            m_scenario->addItem(scenarioName(scenario), value);
        }
        m_scenario->setObjectName("EtherCATMockDiagnosticsScenario");
        m_scenario->setCurrentIndex(m_scenario->findData(int(provider->scenario())));

        auto actionsLayout = new QHBoxLayout;
        actionsLayout->setContentsMargins(QMargins());
        actionsLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHXs);
        for (Utils::Id actionId : {Utils::Id(Constants::START_ACTION_ID),
                                   Utils::Id(Constants::STOP_ACTION_ID),
                                   Utils::Id(Constants::CONFIG_ACTION_ID),
                                   Utils::Id(Constants::FREE_RUN_ACTION_ID),
                                   Utils::Id(Constants::RUN_ACTION_ID),
                                   Utils::Id(Constants::ACKNOWLEDGE_ACTION_ID),
                                   Utils::Id(Constants::CLEAR_RECOVERED_ACTION_ID)}) {
            if (QAction *action = workflow->action(actionId)) {
                auto button = new QToolButton(this);
                button->setDefaultAction(action);
                actionsLayout->addWidget(button);
            }
        }
        actionsLayout->addStretch(1);

        auto scenarioLayout = new QHBoxLayout;
        scenarioLayout->setContentsMargins(QMargins());
        scenarioLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHXs);
        scenarioLayout->addWidget(new QLabel(Tr::tr("Mock scenario:"), this));
        scenarioLayout->addWidget(m_scenario);
        scenarioLayout->addStretch(1);

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM,
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM);
        layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
        layout->addWidget(m_banner);
        layout->addWidget(m_context);
        layout->addLayout(scenarioLayout);
        layout->addLayout(actionsLayout);
        layout->addWidget(m_state);
        if (m_kind == DiagnosticsPageKind::Performance) {
            m_trend = new TrendWidget(this);
            layout->addWidget(m_trend, 1);
        }
        layout->addWidget(m_table, 1);

        provider->addConsumer();
        connect(
            m_scenario,
            &QComboBox::currentIndexChanged,
            this,
            [this](int index) {
                if (m_provider) {
                    m_provider->setScenario(
                        MockDiagnosticsScenario(m_scenario->itemData(index).toInt()));
                }
            });
        connect(
            provider,
            &Core::DiagnosticsProvider::streamStateChanged,
            this,
            [this] { refresh(); });
        connect(
            provider,
            &Core::DiagnosticsProvider::diagnosticsSnapshotChanged,
            this,
            [this] { refresh(); });
        connect(
            provider,
            &Core::DiagnosticsProvider::diagnosticEventsChanged,
            this,
            [this] { refresh(); });
        connect(
            provider,
            &Core::DiagnosticsProvider::diagnosticTrendChanged,
            this,
            [this] { refresh(); });
        connect(
            provider,
            &MockDiagnosticsProvider::scenarioChanged,
            this,
            [this] { refresh(); });
        connect(
            m_table,
            &QTreeWidget::currentItemChanged,
            this,
            [this](QTreeWidgetItem *item) {
                if (m_workflow && m_kind == DiagnosticsPageKind::Events) {
                    m_workflow->setSelectedAlarmId(
                        item ? item->data(0, Qt::UserRole).value<Data::NodeId>()
                             : Data::NodeId());
                }
            });
        refresh();
    }

    ~DiagnosticsPageWidget() final
    {
        if (m_provider)
            m_provider->removeConsumer();
    }

    void setContext(const Core::PropertyPageContext &context)
    {
        m_pageContext = context;
        m_context->setText(
            Tr::tr("Selected offline node: %1 (%2) - MOCK data")
                .arg(context.displayName, context.nodeId.toString()));
        refresh();
    }

    void refresh()
    {
        m_table->clear();
        if (!m_provider) {
            m_state->setText(Tr::tr("MOCK diagnostics provider was removed."));
            return;
        }
        const QSignalBlocker blocker(m_scenario);
        const int scenarioIndex = m_scenario->findData(int(m_provider->scenario()));
        if (scenarioIndex >= 0)
            m_scenario->setCurrentIndex(scenarioIndex);
        m_state->setText(
            Tr::tr("MOCK stream: %1\nSource period: %2 ms; UI publish period: %3 ms\n%4")
                .arg(streamStateName(m_provider->streamState()))
                .arg(m_provider->limits().samplePeriodMs)
                .arg(m_provider->limits().publishPeriodMs)
                .arg(m_provider->lastDiagnosticsError()));
        const std::optional<Data::DiagnosticsSnapshot> snapshot
            = m_provider->latestSnapshot();
        if (!snapshot) {
            setHeaders({Tr::tr("Status"), Tr::tr("Detail")});
            addRow({Tr::tr("No Mock snapshot"),
                    Tr::tr("Start Mock Diagnostics to generate local synthetic data.")});
            if (m_trend)
                m_trend->setSamples({});
            return;
        }
        switch (m_kind) {
        case DiagnosticsPageKind::Master:
            refreshMaster(*snapshot);
            break;
        case DiagnosticsPageKind::Slaves:
            refreshSlaves(*snapshot);
            break;
        case DiagnosticsPageKind::SlaveOnline:
            refreshSelectedSlave(*snapshot);
            break;
        case DiagnosticsPageKind::WorkingCounter:
            refreshWorkingCounter(*snapshot);
            break;
        case DiagnosticsPageKind::DistributedClock:
            refreshDistributedClock(*snapshot);
            break;
        case DiagnosticsPageKind::Ports:
            refreshPorts(*snapshot);
            break;
        case DiagnosticsPageKind::Counters:
            refreshCounters(*snapshot);
            break;
        case DiagnosticsPageKind::Events:
            refreshEvents();
            break;
        case DiagnosticsPageKind::Performance:
            refreshPerformance(*snapshot);
            break;
        }
    }

private:
    void setHeaders(const QStringList &headers)
    {
        m_table->setColumnCount(headers.size());
        m_table->setHeaderLabels(headers);
    }

    void addRow(const QStringList &columns, const Data::NodeId &eventId = {})
    {
        auto item = new QTreeWidgetItem(columns);
        if (!eventId.isNull())
            item->setData(0, Qt::UserRole, QVariant::fromValue(eventId));
        m_table->addTopLevelItem(item);
    }

    void refreshMaster(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("MOCK master item"), Tr::tr("Value"), Tr::tr("Detail")});
        addRow({Tr::tr("Run mode"), runModeName(snapshot.runMode), Tr::tr("Local only")});
        addRow({Tr::tr("ESM"), etherCATStateName(snapshot.masterState),
                snapshot.masterHasError ? Tr::tr("MOCK error") : Tr::tr("MOCK healthy")});
        addRow({Tr::tr("AL Status"),
                QString("0x%1").arg(snapshot.masterAlStatusCode, 4, 16, QLatin1Char('0')),
                snapshot.masterAlStatusText});
        addRow({Tr::tr("Working Counter"),
                Tr::tr("%1 / %2")
                    .arg(snapshot.workingCounter.actual)
                    .arg(snapshot.workingCounter.expected),
                workingCounterStateName(snapshot.workingCounter.state)});
        addRow({Tr::tr("Configured slaves"), QString::number(snapshot.slaves.size()), {}});
        addRow({Tr::tr("Active alarms"), QString::number(snapshot.activeAlarmCount),
                Tr::tr("%1 unacknowledged").arg(snapshot.unacknowledgedAlarmCount)});
        addRow({Tr::tr("Source samples"), counterText(snapshot.sourceSampleCount),
                Tr::tr("%1 coalesced before UI publication")
                    .arg(snapshot.coalescedSampleCount)});
        addRow({Tr::tr("Captured"), snapshot.capturedAt.toString(Qt::ISODateWithMs),
                Tr::tr("MOCK timestamp")});
    }

    void refreshSlaves(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("Position"),
                    Tr::tr("Slave"),
                    Tr::tr("ESM"),
                    Tr::tr("WcState"),
                    Tr::tr("AL Status"),
                    Tr::tr("Link"),
                    Tr::tr("DC")});
        for (const Data::SlaveDiagnostics &slave : snapshot.slaves) {
            addRow({QString::number(slave.position),
                    slave.name,
                    etherCATStateName(slave.state)
                        + (slave.hasError ? Tr::tr(" + MOCK error") : QString()),
                    workingCounterStateName(slave.workingCounterState),
                    QString("0x%1 %2")
                        .arg(slave.alStatusCode, 4, 16, QLatin1Char('0'))
                        .arg(slave.alStatusText),
                    slave.ports.isEmpty()
                        ? Tr::tr("Unknown")
                        : linkStateName(slave.ports.first().linkState),
                    dcStateName(slave.distributedClock.state)});
        }
        if (snapshot.slaves.isEmpty())
            addRow({{}, Tr::tr("No configured Mock slaves")});
    }

    void refreshSelectedSlave(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("MOCK online item"), Tr::tr("Value"), Tr::tr("Detail")});
        const auto slave = std::find_if(
            snapshot.slaves.cbegin(),
            snapshot.slaves.cend(),
            [this](const Data::SlaveDiagnostics &candidate) {
                return candidate.nodeId == m_pageContext.nodeId;
            });
        if (slave == snapshot.slaves.cend()) {
            addRow({Tr::tr("Selected slave"), Tr::tr("Not in Mock snapshot"),
                    Tr::tr("Check the offline configuration.")});
            return;
        }
        addRow({Tr::tr("Position"), QString::number(slave->position), slave->name});
        addRow({Tr::tr("ESM"), etherCATStateName(slave->state),
                slave->hasError ? Tr::tr("MOCK error") : Tr::tr("MOCK healthy")});
        addRow({Tr::tr("AL Status"),
                QString("0x%1").arg(slave->alStatusCode, 4, 16, QLatin1Char('0')),
                slave->alStatusText});
        addRow({Tr::tr("WcState"), workingCounterStateName(slave->workingCounterState), {}});
        addRow({Tr::tr("DC offset"), QString::number(slave->distributedClock.offsetNs),
                Tr::tr("ns")});
        addRow({Tr::tr("Lost Frames"), counterText(slave->frameErrors.lostFrames),
                Tr::tr("MOCK counter")});
        for (const Data::PortDiagnostics &port : slave->ports) {
            addRow({Tr::tr("Port %1").arg(port.port), linkStateName(port.linkState),
                    Tr::tr("%1 interruption(s)").arg(port.interruptionCount)});
        }
    }

    void refreshWorkingCounter(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("Scope"),
                    Tr::tr("Expected"),
                    Tr::tr("Actual"),
                    Tr::tr("WcState"),
                    Tr::tr("Mismatch samples")});
        addRow({Tr::tr("Master"),
                QString::number(snapshot.workingCounter.expected),
                QString::number(snapshot.workingCounter.actual),
                workingCounterStateName(snapshot.workingCounter.state),
                counterText(snapshot.workingCounter.mismatchCount)});
        for (const Data::SlaveDiagnostics &slave : snapshot.slaves) {
            addRow({slave.name, {}, {}, workingCounterStateName(slave.workingCounterState), {}});
        }
    }

    void refreshDistributedClock(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("Scope"),
                    Tr::tr("State"),
                    Tr::tr("Offset (ns)"),
                    Tr::tr("Deviation (ns)"),
                    Tr::tr("Max abs offset (ns)"),
                    Tr::tr("Lost sync")});
        const auto addDc = [this](const QString &scope, const Data::DcDiagnostics &dc) {
            addRow({scope,
                    dcStateName(dc.state),
                    QString::number(dc.offsetNs),
                    QString::number(dc.deviationNs),
                    QString::number(dc.maximumAbsoluteOffsetNs),
                    counterText(dc.lostSyncCount)});
        };
        addDc(Tr::tr("Master"), snapshot.distributedClock);
        for (const Data::SlaveDiagnostics &slave : snapshot.slaves)
            addDc(slave.name, slave.distributedClock);
    }

    void refreshPorts(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("Scope"),
                    Tr::tr("Port"),
                    Tr::tr("Link"),
                    Tr::tr("Communication"),
                    Tr::tr("Interruptions"),
                    Tr::tr("Invalid frames"),
                    Tr::tr("Receive errors")});
        const auto addPorts = [this](
                                  const QString &scope,
                                  const QList<Data::PortDiagnostics> &ports) {
            for (const Data::PortDiagnostics &port : ports) {
                addRow({scope,
                        QString::number(port.port),
                        linkStateName(port.linkState),
                        port.communicationEstablished ? Tr::tr("Established")
                                                      : Tr::tr("Not established"),
                        counterText(port.interruptionCount),
                        counterText(port.invalidFrameCount),
                        counterText(port.receiveErrorCount)});
            }
        };
        addPorts(Tr::tr("Master"), snapshot.masterPorts);
        for (const Data::SlaveDiagnostics &slave : snapshot.slaves)
            addPorts(slave.name, slave.ports);
    }

    void refreshCounters(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("Scope"),
                    Tr::tr("Lost Frames"),
                    Tr::tr("CRC"),
                    Tr::tr("Timeout"),
                    Tr::tr("Drop"),
                    Tr::tr("Late"),
                    Tr::tr("Overflow")});
        const auto addCounters = [this](
                                     const QString &scope,
                                     const Data::FrameErrorCounters &counters) {
            addRow({scope,
                    counterText(counters.lostFrames),
                    counterText(counters.crcErrors),
                    counterText(counters.timeouts),
                    counterText(counters.drops),
                    counterText(counters.lateFrames),
                    counterText(counters.overflows)});
        };
        addCounters(Tr::tr("Master"), snapshot.frameErrors);
        for (const Data::SlaveDiagnostics &slave : snapshot.slaves)
            addCounters(slave.name, slave.frameErrors);
    }

    void refreshEvents()
    {
        setHeaders({Tr::tr("Time"),
                    Tr::tr("Severity"),
                    Tr::tr("Lifecycle"),
                    Tr::tr("Code"),
                    Tr::tr("Node"),
                    Tr::tr("Summary"),
                    Tr::tr("Repeat")});
        for (const Data::DiagnosticEvent &event : m_provider->events()) {
            addRow({event.occurredAt.toString(Qt::ISODateWithMs),
                    severityName(event.severity),
                    lifecycleName(event.lifecycle),
                    event.code,
                    event.nodeId.toString(),
                    event.summary + ": " + event.detail,
                    QString::number(event.repeatCount)},
                   event.id);
        }
        if (m_provider->events().isEmpty())
            addRow({{}, Tr::tr("No Mock events")});
    }

    void refreshPerformance(const Data::DiagnosticsSnapshot &snapshot)
    {
        setHeaders({Tr::tr("Metric"), Tr::tr("Value"), Tr::tr("Unit / detail")});
        addRow({Tr::tr("Nominal cycle"), QString::number(snapshot.cycle.nominalCycleNs),
                Tr::tr("ns")});
        addRow({Tr::tr("Last cycle"), QString::number(snapshot.cycle.lastCycleNs),
                Tr::tr("ns")});
        addRow({Tr::tr("Minimum cycle"), QString::number(snapshot.cycle.minimumCycleNs),
                Tr::tr("ns")});
        addRow({Tr::tr("Maximum cycle"), QString::number(snapshot.cycle.maximumCycleNs),
                Tr::tr("ns")});
        addRow({Tr::tr("Jitter"), QString::number(snapshot.cycle.jitterNs), Tr::tr("ns")});
        addRow({Tr::tr("Deadline margin"), QString::number(snapshot.cycle.deadlineMarginNs),
                Tr::tr("ns")});
        addRow({Tr::tr("Sample count"), counterText(snapshot.cycle.sampleCount),
                Tr::tr("source samples")});
        addRow({Tr::tr("Missed deadlines"), counterText(snapshot.cycle.missedDeadlineCount),
                Tr::tr("MOCK counter")});
        addRow({Tr::tr("Dropped trend samples"),
                counterText(snapshot.droppedTrendSampleCount),
                Tr::tr("oldest-first bounded eviction")});
        if (m_trend)
            m_trend->setSamples(m_provider->trendSamples());
    }

    DiagnosticsPageKind m_kind;
    QPointer<MockDiagnosticsProvider> m_provider;
    QPointer<DiagnosticsWorkflow> m_workflow;
    Core::PropertyPageContext m_pageContext;
    QLabel *m_banner;
    QLabel *m_context;
    QLabel *m_state;
    QComboBox *m_scenario;
    QTreeWidget *m_table;
    TrendWidget *m_trend = nullptr;
};

static std::optional<DiagnosticsPageKind> pageKind(Utils::Id pageId)
{
    if (pageId == Utils::Id(Constants::MASTER_PAGE_ID))
        return DiagnosticsPageKind::Master;
    if (pageId == Utils::Id(Constants::SLAVES_PAGE_ID))
        return DiagnosticsPageKind::Slaves;
    if (pageId == Utils::Id(Constants::SLAVE_ONLINE_PAGE_ID))
        return DiagnosticsPageKind::SlaveOnline;
    if (pageId == Utils::Id(Constants::WKC_PAGE_ID))
        return DiagnosticsPageKind::WorkingCounter;
    if (pageId == Utils::Id(Constants::DC_PAGE_ID))
        return DiagnosticsPageKind::DistributedClock;
    if (pageId == Utils::Id(Constants::PORTS_PAGE_ID))
        return DiagnosticsPageKind::Ports;
    if (pageId == Utils::Id(Constants::COUNTERS_PAGE_ID))
        return DiagnosticsPageKind::Counters;
    if (pageId == Utils::Id(Constants::EVENTS_PAGE_ID))
        return DiagnosticsPageKind::Events;
    if (pageId == Utils::Id(Constants::PERFORMANCE_PAGE_ID))
        return DiagnosticsPageKind::Performance;
    return std::nullopt;
}

DiagnosticsPropertyPageProvider::DiagnosticsPropertyPageProvider(
    MockDiagnosticsProvider *diagnosticsProvider,
    DiagnosticsWorkflow *workflow,
    QObject *parent)
    : Core::PropertyPageProvider(
          Constants::PAGE_PROVIDER_ID, Tr::tr("Mock diagnostic pages"), parent)
    , m_diagnosticsProvider(diagnosticsProvider)
    , m_workflow(workflow)
{
    setAvailable(true);
}

QList<Core::PropertyPageDescriptor> DiagnosticsPropertyPageProvider::pages(
    const Core::PropertyPageContext &context) const
{
    if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
        return {{Utils::Id(Constants::MASTER_PAGE_ID), Tr::tr("Mock Online"), 650}};
    }
    if (context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave) {
        return {{Utils::Id(Constants::SLAVE_ONLINE_PAGE_ID), Tr::tr("Mock Online"), 650}};
    }
    if (context.nodeKind != Core::WorkbenchNodeKind::Diagnostics)
        return {};
    return {{Utils::Id(Constants::MASTER_PAGE_ID), Tr::tr("Master Overview"), 700},
            {Utils::Id(Constants::SLAVES_PAGE_ID), Tr::tr("Slave States"), 710},
            {Utils::Id(Constants::WKC_PAGE_ID), Tr::tr("WKC / WcState"), 720},
            {Utils::Id(Constants::DC_PAGE_ID), Tr::tr("DC Sync"), 730},
            {Utils::Id(Constants::PORTS_PAGE_ID), Tr::tr("Link / Ports"), 740},
            {Utils::Id(Constants::COUNTERS_PAGE_ID), Tr::tr("Frame / Errors"), 750},
            {Utils::Id(Constants::EVENTS_PAGE_ID), Tr::tr("Events / Alarms"), 760},
            {Utils::Id(Constants::PERFORMANCE_PAGE_ID), Tr::tr("Cycle / Performance"), 770}};
}

QWidget *DiagnosticsPropertyPageProvider::createPage(Utils::Id pageId, QWidget *parent)
{
    const std::optional<DiagnosticsPageKind> kind = pageKind(pageId);
    if (!kind || !m_diagnosticsProvider || !m_workflow)
        return nullptr;
    return new DiagnosticsPageWidget(
        *kind, m_diagnosticsProvider, m_workflow, parent);
}

void DiagnosticsPropertyPageProvider::updatePage(
    Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context)
{
    if (!pageKind(pageId) || !page
        || !page->property("EtherCAT.Diagnostics.Page").toBool()) {
        return;
    }
    auto diagnosticsPage = static_cast<DiagnosticsPageWidget *>(page);
    if (m_workflow)
        m_workflow->setPageContext(context);
    diagnosticsPage->setContext(context);
}

} // namespace EtherCAT::Diagnostics::Internal
