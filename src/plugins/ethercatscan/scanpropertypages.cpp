// Copyright (C) 2026 Kvell

#include "scanpropertypages.h"

#include "ethercatscanconstants.h"
#include "ethercatscantr.h"
#include "mockscanprovider.h"
#include "scanworkflow.h"

#include <utils/stylehelper.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace EtherCAT::Scan::Internal {

static QString stateName(Data::ScanState state)
{
    switch (state) {
    case Data::ScanState::Idle:
        return Tr::tr("Idle");
    case Data::ScanState::Preparing:
        return Tr::tr("Preparing");
    case Data::ScanState::ScanningMaster:
        return Tr::tr("Scanning Master");
    case Data::ScanState::ScanningSlaves:
        return Tr::tr("Scanning Slaves");
    case Data::ScanState::BuildingSnapshot:
        return Tr::tr("Building Snapshot");
    case Data::ScanState::Comparing:
        return Tr::tr("Comparing");
    case Data::ScanState::Completed:
        return Tr::tr("Completed");
    case Data::ScanState::Cancelled:
        return Tr::tr("Cancelled");
    case Data::ScanState::Failed:
        return Tr::tr("Failed");
    }
    return Tr::tr("Unknown");
}

static QString scenarioName(MockScanScenario scenario)
{
    switch (scenario) {
    case MockScanScenario::Normal:
        return Tr::tr("Normal Mock topology");
    case MockScanScenario::Slow:
        return Tr::tr("Slow Mock scan");
    case MockScanScenario::PartialFailure:
        return Tr::tr("Partial Mock failure");
    case MockScanScenario::DuplicateDevice:
        return Tr::tr("Duplicate Mock device");
    case MockScanScenario::RevisionMismatch:
        return Tr::tr("Mock revision mismatch");
    }
    return {};
}

static QString differenceName(Data::TopologyDifferenceKind kind)
{
    switch (kind) {
    case Data::TopologyDifferenceKind::Added:
        return Tr::tr("Added");
    case Data::TopologyDifferenceKind::Missing:
        return Tr::tr("Missing");
    case Data::TopologyDifferenceKind::PositionChanged:
        return Tr::tr("Position");
    case Data::TopologyDifferenceKind::VendorMismatch:
        return Tr::tr("Vendor");
    case Data::TopologyDifferenceKind::ProductMismatch:
        return Tr::tr("Product");
    case Data::TopologyDifferenceKind::RevisionMismatch:
        return Tr::tr("Revision");
    case Data::TopologyDifferenceKind::SerialMismatch:
        return Tr::tr("Serial Number");
    case Data::TopologyDifferenceKind::AliasMismatch:
        return Tr::tr("Alias");
    case Data::TopologyDifferenceKind::DuplicateDevice:
        return Tr::tr("Duplicate");
    case Data::TopologyDifferenceKind::PdoConfiguration:
        return Tr::tr("PDO");
    case Data::TopologyDifferenceKind::DcConfiguration:
        return Tr::tr("DC");
    }
    return {};
}

static QString severityName(Data::DifferenceSeverity severity)
{
    switch (severity) {
    case Data::DifferenceSeverity::Information:
        return Tr::tr("Information");
    case Data::DifferenceSeverity::Warning:
        return Tr::tr("Warning");
    case Data::DifferenceSeverity::Blocking:
        return Tr::tr("Blocking");
    }
    return {};
}

class ScanPageWidget final : public QWidget
{
public:
    ScanPageWidget(MockScanProvider *provider, ScanWorkflow *workflow, QWidget *parent)
        : QWidget(parent)
        , m_provider(provider)
        , m_workflow(workflow)
        , m_banner(new QLabel(this))
        , m_context(new QLabel(this))
        , m_state(new QLabel(this))
        , m_progress(new QProgressBar(this))
        , m_scenario(new QComboBox(this))
        , m_differences(new QTreeWidget(this))
    {
        setProperty("EtherCAT.Scan.Page", true);
        setObjectName("EtherCATMockScanPage");
        m_banner->setObjectName("EtherCATMockScanBanner");
        m_banner->setText(
            Tr::tr("MOCK SCAN - local simulation only; no network or controller is accessed."));
        m_banner->setWordWrap(true);
        m_banner->setFont(Utils::StyleHelper::uiFont(Utils::StyleHelper::UiElementH5));
        m_context->setObjectName("EtherCATMockScanContext");
        m_context->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_state->setObjectName("EtherCATMockScanState");
        m_state->setWordWrap(true);
        m_progress->setObjectName("EtherCATMockScanProgress");
        m_progress->setTextVisible(true);
        m_scenario->setObjectName("EtherCATMockScanScenario");
        for (int value = int(MockScanScenario::Normal);
             value <= int(MockScanScenario::RevisionMismatch);
             ++value) {
            const MockScanScenario scenario = MockScanScenario(value);
            m_scenario->addItem(scenarioName(scenario), value);
        }
        m_scenario->setCurrentIndex(m_scenario->findData(int(provider->scenario())));

        m_differences->setObjectName("EtherCATMockScanDifferences");
        m_differences->setColumnCount(5);
        m_differences->setHeaderLabels({Tr::tr("Severity"),
                                        Tr::tr("Change"),
                                        Tr::tr("Offline"),
                                        Tr::tr("Scanned"),
                                        Tr::tr("Detail")});
        m_differences->setAlternatingRowColors(true);
        m_differences->setRootIsDecorated(false);
        m_differences->setUniformRowHeights(true);
        m_differences->header()->setStretchLastSection(true);

        auto actionsLayout = new QHBoxLayout;
        actionsLayout->setContentsMargins(QMargins());
        actionsLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHXs);
        const QList<Utils::Id> actionIds = {
            Constants::SCAN_SLAVES_ACTION_ID,
            Constants::COMPARE_ACTION_ID,
            Constants::ACCEPT_ACTION_ID,
            Constants::KEEP_ACTION_ID,
            Constants::CANCEL_ACTION_ID,
        };
        for (Utils::Id actionId : actionIds) {
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
        layout->addWidget(m_progress);
        layout->addWidget(m_differences, 1);

        connect(
            m_scenario,
            &QComboBox::currentIndexChanged,
            this,
            [this](int index) {
                m_provider->setScenario(
                    MockScanScenario(m_scenario->itemData(index).toInt()));
            });
        connect(provider, &Core::ScanProvider::scanProgressChanged, this, [this] { refresh(); });
        connect(provider, &Core::ScanProvider::scanResultChanged, this, [this] { refresh(); });
        connect(provider, &MockScanProvider::scenarioChanged, this, [this] { refresh(); });
        connect(workflow, &ScanWorkflow::actionStateChanged, this, [this] { refresh(); });
        refresh();
    }

    void setContextText(const QString &text) { m_context->setText(text); }

    void refresh()
    {
        if (!m_provider)
            return;
        const Data::ScanProgress progress = m_provider->scanProgress();
        m_state->setText(
            Tr::tr("MOCK state: %1\n%2\nDiscovered slaves: %3")
                .arg(stateName(progress.state), progress.detail)
                .arg(progress.discoveredSlaves));
        m_progress->setRange(0, qMax(1, progress.maximum));
        m_progress->setValue(progress.value);
        m_scenario->setEnabled(
            progress.state == Data::ScanState::Idle
            || progress.state == Data::ScanState::Completed
            || progress.state == Data::ScanState::Cancelled
            || progress.state == Data::ScanState::Failed);
        const int scenarioIndex = m_scenario->findData(int(m_provider->scenario()));
        if (scenarioIndex >= 0 && scenarioIndex != m_scenario->currentIndex())
            m_scenario->setCurrentIndex(scenarioIndex);

        m_differences->clear();
        const std::optional<Data::ScanResult> result = m_provider->lastScanResult();
        if (!result) {
            if (!m_provider->lastScanError().isEmpty()) {
                m_differences->addTopLevelItem(new QTreeWidgetItem(
                    {Tr::tr("Blocking"),
                     Tr::tr("Mock failure"),
                     {},
                     {},
                     m_provider->lastScanError()}));
            }
            return;
        }
        for (const Data::TopologyDifference &difference : result->comparison.differences) {
            m_differences->addTopLevelItem(new QTreeWidgetItem(
                {severityName(difference.severity),
                 differenceName(difference.kind),
                 difference.offlinePosition < 0 ? QString()
                                                : QString::number(difference.offlinePosition),
                 difference.scannedPosition < 0 ? QString()
                                                : QString::number(difference.scannedPosition),
                 difference.summary + ": " + difference.detail}));
        }
        if (result->snapshot.slaves.isEmpty() && !result->snapshot.interfaces.isEmpty()) {
            for (const Data::ScannedInterface &interface : result->snapshot.interfaces) {
                m_differences->addTopLevelItem(new QTreeWidgetItem(
                    {Tr::tr("Information"),
                     Tr::tr("Mock interface"),
                     {},
                     {},
                     interface.name + ": " + interface.description}));
            }
        }
    }

private:
    QPointer<MockScanProvider> m_provider;
    QPointer<ScanWorkflow> m_workflow;
    QLabel *m_banner;
    QLabel *m_context;
    QLabel *m_state;
    QProgressBar *m_progress;
    QComboBox *m_scenario;
    QTreeWidget *m_differences;
};

ScanPropertyPageProvider::ScanPropertyPageProvider(
    MockScanProvider *scanProvider, ScanWorkflow *workflow, QObject *parent)
    : Core::PropertyPageProvider(Constants::PAGE_PROVIDER_ID, Tr::tr("Mock scan pages"), parent)
    , m_scanProvider(scanProvider)
    , m_workflow(workflow)
{
    setAvailable(true);
}

QList<Core::PropertyPageDescriptor> ScanPropertyPageProvider::pages(
    const Core::PropertyPageContext &context) const
{
    if (context.nodeKind != Core::WorkbenchNodeKind::Master
        && context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave) {
        return {};
    }
    return {{Utils::Id(Constants::PAGE_ID), Tr::tr("Mock Scan"), 600}};
}

QWidget *ScanPropertyPageProvider::createPage(Utils::Id pageId, QWidget *parent)
{
    if (pageId != Utils::Id(Constants::PAGE_ID) || !m_scanProvider || !m_workflow)
        return nullptr;
    return new ScanPageWidget(m_scanProvider, m_workflow, parent);
}

void ScanPropertyPageProvider::updatePage(
    Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context)
{
    if (pageId != Utils::Id(Constants::PAGE_ID) || !page
        || !page->property("EtherCAT.Scan.Page").toBool()) {
        return;
    }
    auto scanPage = static_cast<ScanPageWidget *>(page);
    scanPage->setContextText(
        Tr::tr("Selected offline node: %1 (%2)")
            .arg(context.displayName, context.nodeId.toString()));
    scanPage->refresh();
}

} // namespace EtherCAT::Scan::Internal
