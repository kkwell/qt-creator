// Copyright (C) 2026 Kvell

#include "dcpage.h"

#include "esiconfigurationfactory.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace EtherCAT::Workbench::Internal {

static bool isEmpty(const Data::DcConfiguration &configuration)
{
    return configuration == Data::DcConfiguration{};
}

static bool modeMatchesConfiguration(
    const Data::DcModeDescription &mode, const Data::DcConfiguration &configuration)
{
    return mode.name == configuration.modeName
           && mode.assignActivate == configuration.assignActivate
           && mode.cycleTimeSync0Ns == configuration.sync0.cycleTimeNs
           && mode.shiftTimeSync0Ns == configuration.sync0.shiftTimeNs
           && mode.cycleTimeSync1Ns == configuration.sync1.cycleTimeNs
           && mode.shiftTimeSync1Ns == configuration.sync1.shiftTimeNs;
}

static QString hexValue(quint16 value)
{
    return QString("0x%1").arg(value, 4, 16, QLatin1Char('0'));
}

static bool parseAssignActivate(const QString &source, quint16 *result)
{
    QString text = source.trimmed();
    int base = 10;
    if (text.startsWith("0x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    } else if (text.startsWith("#x", Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    }
    bool ok = false;
    const quint64 value = text.toULongLong(&ok, base);
    if (!ok || value > std::numeric_limits<quint16>::max())
        return false;
    *result = quint16(value);
    return true;
}

static bool parseNanoseconds(const QString &source, qint64 *result)
{
    bool ok = false;
    const qint64 value = source.trimmed().toLongLong(&ok, 10);
    if (!ok)
        return false;
    *result = value;
    return true;
}

static QLabel *fieldLabel(const QString &text, QWidget *buddy, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setBuddy(buddy);
    return label;
}

static void configureForm(QFormLayout *layout)
{
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    layout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

DcPage::DcPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_validation(new Utils::InfoLabel(this))
    , m_units(new QLabel(this))
    , m_restoreDefaults(new QPushButton(Tr::tr("Store ESI Defaults"), this))
    , m_operationMode(new QComboBox(this))
    , m_enabled(new QCheckBox(Tr::tr("Enable Distributed Clocks"), this))
    , m_assignActivate(new QLineEdit(this))
    , m_sync0Enabled(new QCheckBox(Tr::tr("Enable SYNC 0"), this))
    , m_sync0Cycle(new QLineEdit(this))
    , m_sync0Shift(new QLineEdit(this))
    , m_sync1Enabled(new QCheckBox(Tr::tr("Enable SYNC 1"), this))
    , m_sync1Cycle(new QLineEdit(this))
    , m_sync1Shift(new QLineEdit(this))
    , m_potentialReferenceClock(new QCheckBox(Tr::tr("Use as potential Reference Clock"), this))
{
    setObjectName("EtherCATWorkbenchDcPage");
    m_summary->setObjectName("EtherCATDcSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_validation->setObjectName("EtherCATDcValidation");
    m_validation->setAccessibleName(Tr::tr("Distributed Clocks edit feedback"));
    m_validation->setElideMode(Qt::ElideNone);
    m_validation->setWordWrap(true);
    m_units->setObjectName("EtherCATDcUnits");
    m_units->setText(
        Tr::tr("All cycle and shift values are edited and stored in nanoseconds (ns)."));
    m_units->setWordWrap(true);
    m_units->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_restoreDefaults->setObjectName("EtherCATDcRestoreDefaults");
    m_operationMode->setObjectName("EtherCATDcOperationMode");
    m_operationMode->setEditable(true);
    m_operationMode->setInsertPolicy(QComboBox::NoInsert);
    m_enabled->setObjectName("EtherCATDcEnabled");
    m_assignActivate->setObjectName("EtherCATDcAssignActivate");
    m_sync0Enabled->setObjectName("EtherCATDcSync0Enabled");
    m_sync0Cycle->setObjectName("EtherCATDcSync0CycleNs");
    m_sync0Shift->setObjectName("EtherCATDcSync0ShiftNs");
    m_sync1Enabled->setObjectName("EtherCATDcSync1Enabled");
    m_sync1Cycle->setObjectName("EtherCATDcSync1CycleNs");
    m_sync1Shift->setObjectName("EtherCATDcSync1ShiftNs");
    m_potentialReferenceClock->setObjectName("EtherCATDcPotentialReferenceClock");

    m_operationMode->setAccessibleName(Tr::tr("Distributed Clocks operation mode"));
    m_assignActivate->setAccessibleName(Tr::tr("AssignActivate value"));
    m_sync0Cycle->setAccessibleName(Tr::tr("SYNC 0 cycle time in nanoseconds"));
    m_sync0Shift->setAccessibleName(Tr::tr("SYNC 0 shift time in nanoseconds"));
    m_sync1Cycle->setAccessibleName(Tr::tr("SYNC 1 cycle time in nanoseconds"));
    m_sync1Shift->setAccessibleName(Tr::tr("SYNC 1 shift time in nanoseconds"));

    auto cyclicGroup = new QGroupBox(Tr::tr("Cyclic Mode"), this);
    auto cyclicForm = new QFormLayout(cyclicGroup);
    configureForm(cyclicForm);
    cyclicForm->addRow(
        fieldLabel(Tr::tr("Operation Mode:"), m_operationMode, cyclicGroup), m_operationMode);
    cyclicForm->addRow(m_enabled);
    cyclicForm->addRow(
        fieldLabel(Tr::tr("AssignActivate:"), m_assignActivate, cyclicGroup), m_assignActivate);

    auto sync0Group = new QGroupBox(Tr::tr("SYNC 0"), this);
    auto sync0Form = new QFormLayout(sync0Group);
    configureForm(sync0Form);
    sync0Form->addRow(m_sync0Enabled);
    sync0Form->addRow(fieldLabel(Tr::tr("Cycle Time [ns]:"), m_sync0Cycle, sync0Group), m_sync0Cycle);
    sync0Form->addRow(fieldLabel(Tr::tr("Shift Time [ns]:"), m_sync0Shift, sync0Group), m_sync0Shift);

    auto sync1Group = new QGroupBox(Tr::tr("SYNC 1"), this);
    auto sync1Form = new QFormLayout(sync1Group);
    configureForm(sync1Form);
    sync1Form->addRow(m_sync1Enabled);
    sync1Form->addRow(fieldLabel(Tr::tr("Cycle Time [ns]:"), m_sync1Cycle, sync1Group), m_sync1Cycle);
    sync1Form->addRow(fieldLabel(Tr::tr("Shift Time [ns]:"), m_sync1Shift, sync1Group), m_sync1Shift);

    auto headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(QMargins());
    headerLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    headerLayout->addWidget(m_summary, 1);
    headerLayout->addWidget(m_restoreDefaults);

    auto signalLayout = new QHBoxLayout;
    signalLayout->setContentsMargins(QMargins());
    signalLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    signalLayout->addWidget(sync0Group, 1);
    signalLayout->addWidget(sync1Group, 1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addLayout(headerLayout);
    layout->addWidget(m_validation);
    layout->addWidget(m_units);
    layout->addWidget(cyclicGroup);
    layout->addLayout(signalLayout);
    layout->addWidget(m_potentialReferenceClock);
    layout->addStretch();

    connect(m_restoreDefaults, &QPushButton::clicked, this, [this] {
        if (!isEmpty(m_esiDefaults)) {
            const QScopedValueRollback forceReload(
                m_forceAuthoritativeDrafts,
                DraftFields(m_forceAuthoritativeDrafts | AllDrafts));
            submitConfiguration(m_esiDefaults);
        }
    });
    connect(m_operationMode, &QComboBox::activated, this, &DcPage::selectEsiMode);
    connect(m_operationMode->lineEdit(), &QLineEdit::editingFinished, this, &DcPage::commitModeName);
    connect(m_enabled, &QCheckBox::clicked, this, [this](bool checked) {
        Data::DcConfiguration candidate = m_configuration;
        candidate.enabled = checked;
        if (!checked) {
            candidate.sync0.enabled = false;
            candidate.sync1.enabled = false;
        }
        submitConfiguration(candidate);
    });
    connect(m_assignActivate, &QLineEdit::editingFinished, this, &DcPage::commitAssignActivate);
    connect(m_sync0Enabled, &QCheckBox::clicked, this, [this](bool checked) {
        Data::DcConfiguration candidate = m_configuration;
        candidate.sync0.enabled = checked;
        if (!checked)
            candidate.sync1.enabled = false;
        submitConfiguration(candidate);
    });
    connect(m_sync1Enabled, &QCheckBox::clicked, this, [this](bool checked) {
        Data::DcConfiguration candidate = m_configuration;
        candidate.sync1.enabled = checked;
        submitConfiguration(candidate);
    });
    connect(m_sync0Cycle, &QLineEdit::editingFinished, this, [this] {
        commitSignalValue(false, SignalField::CycleTime, m_sync0Cycle);
    });
    connect(m_sync0Shift, &QLineEdit::editingFinished, this, [this] {
        commitSignalValue(false, SignalField::ShiftTime, m_sync0Shift);
    });
    connect(m_sync1Cycle, &QLineEdit::editingFinished, this, [this] {
        commitSignalValue(true, SignalField::CycleTime, m_sync1Cycle);
    });
    connect(m_sync1Shift, &QLineEdit::editingFinished, this, [this] {
        commitSignalValue(true, SignalField::ShiftTime, m_sync1Shift);
    });
    connect(m_potentialReferenceClock, &QCheckBox::clicked, this, [this](bool checked) {
        Data::DcConfiguration candidate = m_configuration;
        candidate.potentialReferenceClock = checked;
        submitConfiguration(candidate);
    });
    for (QLineEdit *editor : {m_operationMode->lineEdit(),
                              m_assignActivate,
                              m_sync0Cycle,
                              m_sync0Shift,
                              m_sync1Cycle,
                              m_sync1Shift}) {
        connect(editor, &QLineEdit::textEdited, this, [this] { clearEditRejection(); });
    }
}

void DcPage::setContext(const Core::PropertyPageContext &context)
{
    const std::optional<Data::ProjectSnapshot> project
        = m_controller && m_controller->projectService()
              ? m_controller->projectService()->project(context.projectId)
              : std::nullopt;
    const bool editable = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && project
                          && project->valid;
    Data::DcConfiguration configuration;
    Data::DcConfiguration esiDefaults;
    QList<Data::DcModeDescription> esiModes;
    bool showingEsiDefaults = false;

    std::optional<Data::OfflineSlaveConfiguration> slave;
    std::optional<Data::DeviceDescription> device;
    if (m_controller) {
        if (context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave) {
            slave = m_controller->treeModel()->offlineSlave(context.nodeId);
            if (slave)
                configuration = slave->dc;
        }
        if (m_controller->deviceRepository()) {
            if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
                device = m_controller->deviceRepository()->device(context.nodeId);
            } else if (slave && !slave->deviceDescriptionId.isNull()) {
                device = m_controller->deviceRepository()->device(slave->deviceDescriptionId);
            }
        }
    }

    if (device) {
        esiModes = device->dcModes;
        if (!esiModes.isEmpty())
            esiDefaults = dcConfigurationFromMode(esiModes.first());
    }
    const bool repositoryDeviceAvailable
        = context.nodeKind == Core::WorkbenchNodeKind::Device && device.has_value();
    const bool repositoryDeviceSupported
        = repositoryDeviceAvailable && device->summary.supported;
    if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        configuration = esiDefaults;
    } else if (isEmpty(configuration) && !isEmpty(esiDefaults)) {
        configuration = esiDefaults;
        showingEsiDefaults = true;
    }

    const DraftFields preservedDrafts = draftsToPreserve(context, configuration, editable);
    m_context = context;
    m_editable = editable;
    m_configuration = configuration;
    m_esiDefaults = esiDefaults;
    m_esiModes = esiModes;
    m_showingEsiDefaults = showingEsiDefaults;
    m_repositoryDeviceAvailable = repositoryDeviceAvailable;
    m_repositoryDeviceSupported = repositoryDeviceSupported;
    updateAuthoritativeBaseline(context, configuration, editable);

    if (context.nodeKind == Core::WorkbenchNodeKind::Device && !m_repositoryDeviceAvailable) {
        m_summary->setText(
            Tr::tr(
                "The ESI device description is no longer available. Return to Device Repository "
                "and select an available device."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device && m_esiModes.isEmpty()
               && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr(
                "No ESI Distributed Clocks operation mode is available. This repository device "
                "also contains unsupported ESI structures and cannot be added to an offline "
                "Project. Review its support details in Device Repository; no controller, "
                "network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device && m_esiModes.isEmpty()) {
        m_summary->setText(
            Tr::tr(
                "No ESI Distributed Clocks operation mode is available for this repository "
                "device. Add the device to an offline Project to enter manual timing; no "
                "controller, network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device
               && !m_repositoryDeviceSupported) {
        m_summary->setText(
            Tr::tr(
                "ESI Distributed Clocks modes are available for read-only preview, but this "
                "repository device contains unsupported ESI structures and cannot be added to "
                "an offline Project. Review its support details in Device Repository; no "
                "controller, network, or physical hardware is accessed."));
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        m_summary->setText(
            Tr::tr(
                "ESI Distributed Clocks modes. Select an operation mode to preview its "
                "read-only timing. Add the device to an offline project before changing or "
                "storing its operation mode or timing."));
    } else if (m_showingEsiDefaults) {
        m_summary->setText(
            Tr::tr(
                "ESI DC defaults are shown but are not stored in the project. Store the "
                "defaults or edit a field to enter the Undo/Redo history."));
    } else if (!isEmpty(m_configuration) && device) {
        m_summary->setText(
            Tr::tr(
                "Offline Distributed Clocks configuration with ESI reference. Every accepted "
                "field change is validated and undoable."));
    } else if (!isEmpty(m_configuration)) {
        m_summary->setText(
            Tr::tr(
                "Offline Distributed Clocks configuration. No ESI reference is available, but "
                "the persisted mode and timing remain editable."));
    } else {
        m_summary->setText(
            Tr::tr(
                "No ESI Distributed Clocks mode or offline DC configuration is available. "
                "Enter a manual operation mode before enabling DC."));
    }

    m_restoreDefaults->setVisible(
        context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave && !m_esiModes.isEmpty());
    m_restoreDefaults->setEnabled(m_editable && !isEmpty(m_esiDefaults));
    m_restoreDefaults->setText(
        m_showingEsiDefaults ? Tr::tr("Store ESI Defaults") : Tr::tr("Restore ESI Defaults"));
    rebuildControls(preservedDrafts);
}

bool DcPage::submitConfiguration(const Data::DcConfiguration &configuration)
{
    const QList<Data::ConfigurationIssue> issues = Data::validateDcConfiguration(configuration);
    const auto completeRejectionMessage = [&issues](QString summary) {
        for (qsizetype index = 1; index < issues.size(); ++index)
            summary += '\n' + issues.at(index).message;
        return summary;
    };
    const bool hasErrors = std::any_of(issues.cbegin(), issues.cend(), [](const auto &issue) {
        return issue.severity == Data::ConfigurationIssueSeverity::Error;
    });
    if (hasErrors) {
        reloadCurrentContext();
        showValidation(issues, Tr::tr("Change not applied."));
        setEditRejection(m_validation->text(), completeRejectionMessage(m_validation->text()));
        return false;
    }
    if (!m_editable || !m_controller || !m_controller->projectService()) {
        reloadCurrentContext();
        showValidation(issues, Tr::tr("Change not applied: this ESI catalogue page is read-only."));
        setEditRejection(m_validation->text(), completeRejectionMessage(m_validation->text()));
        return false;
    }

    const Utils::Result<> result
        = m_controller->projectService()
              ->setDcConfiguration(m_context.projectId, m_context.nodeId, configuration);
    if (!result) {
        reloadCurrentContext();
        showValidation(issues, Tr::tr("Change not applied: %1").arg(result.error()));
        setEditRejection(m_validation->text(), completeRejectionMessage(m_validation->text()));
        return false;
    }

    reloadCurrentContext();
    return true;
}

void DcPage::reloadCurrentContext()
{
    Core::PropertyPageContext context = m_context;
    if (m_controller && m_controller->treeModel()) {
        const Core::PropertyPageContext current
            = m_controller->treeModel()->contextForNodeId(m_context.nodeId);
        if (current.nodeKind != Core::WorkbenchNodeKind::None)
            context = current;
    }
    setContext(context);
}

DcPage::DraftFields DcPage::draftsToPreserve(
    const Core::PropertyPageContext &context,
    const Data::DcConfiguration &configuration,
    bool editable) const
{
    if (!editable || !m_dcBaselineValid
        || context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave
        || m_context.projectId != context.projectId || m_context.nodeId != context.nodeId
        || m_context.nodeKind != context.nodeKind || m_dcBaselineProjectId != context.projectId
        || m_dcBaselineNodeId != context.nodeId || m_dcBaselineKind != context.nodeKind) {
        return NoDraft;
    }

    const auto activeDraft = [](const QLineEdit *editor) {
        return editor && !editor->isReadOnly() && (editor->isModified() || editor->hasFocus());
    };
    DraftFields drafts = NoDraft;
    if (!(m_forceAuthoritativeDrafts & ModeNameDraft)
        && (activeDraft(m_operationMode->lineEdit()) || m_operationMode->hasFocus())
        && m_dcBaseline.modeName == configuration.modeName) {
        drafts |= ModeNameDraft;
    }
    if (!(m_forceAuthoritativeDrafts & AssignActivateDraft) && activeDraft(m_assignActivate)
        && m_dcBaseline.assignActivate == configuration.assignActivate) {
        drafts |= AssignActivateDraft;
    }
    if (!(m_forceAuthoritativeDrafts & Sync0CycleDraft) && activeDraft(m_sync0Cycle)
        && m_dcBaseline.sync0.cycleTimeNs == configuration.sync0.cycleTimeNs) {
        drafts |= Sync0CycleDraft;
    }
    if (!(m_forceAuthoritativeDrafts & Sync0ShiftDraft) && activeDraft(m_sync0Shift)
        && m_dcBaseline.sync0.shiftTimeNs == configuration.sync0.shiftTimeNs) {
        drafts |= Sync0ShiftDraft;
    }
    if (!(m_forceAuthoritativeDrafts & Sync1CycleDraft) && activeDraft(m_sync1Cycle)
        && m_dcBaseline.sync1.cycleTimeNs == configuration.sync1.cycleTimeNs) {
        drafts |= Sync1CycleDraft;
    }
    if (!(m_forceAuthoritativeDrafts & Sync1ShiftDraft) && activeDraft(m_sync1Shift)
        && m_dcBaseline.sync1.shiftTimeNs == configuration.sync1.shiftTimeNs) {
        drafts |= Sync1ShiftDraft;
    }
    return drafts;
}

void DcPage::updateAuthoritativeBaseline(
    const Core::PropertyPageContext &context,
    const Data::DcConfiguration &configuration,
    bool editable)
{
    m_dcBaselineValid
        = editable && context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave;
    if (m_dcBaselineValid) {
        m_dcBaselineProjectId = context.projectId;
        m_dcBaselineNodeId = context.nodeId;
        m_dcBaselineKind = context.nodeKind;
        m_dcBaseline = configuration;
        return;
    }
    m_dcBaselineProjectId = {};
    m_dcBaselineNodeId = {};
    m_dcBaselineKind = Core::WorkbenchNodeKind::None;
    m_dcBaseline = {};
}

void DcPage::rebuildControls(DraftFields preservedDrafts)
{
    m_rebuilding = true;
    const QSignalBlocker operationModeBlocker(m_operationMode);
    const QSignalBlocker operationModeEditorBlocker(m_operationMode->lineEdit());
    const QSignalBlocker enabledBlocker(m_enabled);
    const QSignalBlocker assignActivateBlocker(m_assignActivate);
    const QSignalBlocker sync0EnabledBlocker(m_sync0Enabled);
    const QSignalBlocker sync0CycleBlocker(m_sync0Cycle);
    const QSignalBlocker sync0ShiftBlocker(m_sync0Shift);
    const QSignalBlocker sync1EnabledBlocker(m_sync1Enabled);
    const QSignalBlocker sync1CycleBlocker(m_sync1Cycle);
    const QSignalBlocker sync1ShiftBlocker(m_sync1Shift);
    const QSignalBlocker referenceClockBlocker(m_potentialReferenceClock);
    if (!(preservedDrafts & ModeNameDraft)) {
        m_visibleEsiModes = m_esiModes;
        m_operationMode->clear();
        for (int index = 0; index < m_visibleEsiModes.size(); ++index)
            m_operationMode->addItem(m_visibleEsiModes.at(index).name, index);

        QList<int> namedModeIndexes;
        for (int index = 0; index < m_visibleEsiModes.size(); ++index) {
            if (m_visibleEsiModes.at(index).name == m_configuration.modeName)
                namedModeIndexes.append(index);
        }
        int modeIndex = namedModeIndexes.size() == 1 ? namedModeIndexes.first() : -1;
        if (namedModeIndexes.size() > 1) {
            const auto matchingMode = std::find_if(
                namedModeIndexes.cbegin(), namedModeIndexes.cend(), [this](int index) {
                    return modeMatchesConfiguration(m_visibleEsiModes.at(index), m_configuration);
                });
            if (matchingMode != namedModeIndexes.cend())
                modeIndex = *matchingMode;
        }
        if (modeIndex < 0 && !m_configuration.modeName.isEmpty()) {
            m_operationMode->addItem(m_configuration.modeName, -1);
            modeIndex = m_operationMode->count() - 1;
        }
        m_operationMode->setCurrentIndex(modeIndex);
        if (modeIndex < 0)
            m_operationMode->setEditText({});
        m_operationMode->lineEdit()->setModified(false);
        m_operationModeItemsDeferred = false;
    } else {
        m_operationModeItemsDeferred = true;
    }

    m_enabled->setChecked(m_configuration.enabled);
    if (!(preservedDrafts & AssignActivateDraft)) {
        m_assignActivate->setText(hexValue(m_configuration.assignActivate));
        m_assignActivate->setModified(false);
    }
    m_sync0Enabled->setChecked(m_configuration.sync0.enabled);
    if (!(preservedDrafts & Sync0CycleDraft)) {
        m_sync0Cycle->setText(QString::number(m_configuration.sync0.cycleTimeNs));
        m_sync0Cycle->setModified(false);
    }
    if (!(preservedDrafts & Sync0ShiftDraft)) {
        m_sync0Shift->setText(QString::number(m_configuration.sync0.shiftTimeNs));
        m_sync0Shift->setModified(false);
    }
    m_sync1Enabled->setChecked(m_configuration.sync1.enabled);
    if (!(preservedDrafts & Sync1CycleDraft)) {
        m_sync1Cycle->setText(QString::number(m_configuration.sync1.cycleTimeNs));
        m_sync1Cycle->setModified(false);
    }
    if (!(preservedDrafts & Sync1ShiftDraft)) {
        m_sync1Shift->setText(QString::number(m_configuration.sync1.shiftTimeNs));
        m_sync1Shift->setModified(false);
    }
    m_potentialReferenceClock->setChecked(m_configuration.potentialReferenceClock);
    m_rebuilding = false;

    updateControlState();
    showValidation(Data::validateDcConfiguration(m_configuration));
}

void DcPage::updateControlState()
{
    const bool repositoryDeviceMissing
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && !m_repositoryDeviceAvailable;
    const bool repositoryModeEmpty
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device && m_repositoryDeviceAvailable
          && m_esiModes.isEmpty();
    const bool repositoryModeEmptyUnsupported
        = repositoryModeEmpty && !m_repositoryDeviceSupported;
    const bool repositoryModePreview
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device && m_repositoryDeviceAvailable
          && !m_esiModes.isEmpty();
    const bool repositoryModePreviewUnsupported
        = repositoryModePreview && !m_repositoryDeviceSupported;
    m_operationMode->setEnabled(m_editable || repositoryModePreview);
    m_operationMode->lineEdit()->setReadOnly(!m_editable);
    QString modeDescription
        = repositoryModePreview
              ? Tr::tr("Selects an imported ESI operation mode for read-only offline preview. "
                       "The preview does not modify a Project or access a controller, network, "
                       "or physical hardware.")
              : m_editable
                    ? Tr::tr("Selects and stores an ESI Distributed Clocks operation mode in "
                             "the offline Project as one undoable change. No controller, "
                             "network, or physical hardware is accessed.")
                    : Tr::tr("No Distributed Clocks operation mode is available for selection "
                             "in this context.");
    if (repositoryModePreviewUnsupported) {
        modeDescription = Tr::tr(
            "Selects an imported ESI operation mode for read-only offline preview. This "
            "repository device contains unsupported ESI structures and cannot be added to an "
            "offline Project. Review its support details in Device Repository. The preview "
            "does not access a controller, network, or physical hardware.");
    } else if (repositoryModeEmptyUnsupported) {
        modeDescription = Tr::tr(
            "No ESI Distributed Clocks operation mode is available. This repository device "
            "contains unsupported ESI structures and cannot be added to an offline Project. "
            "Review its support details in Device Repository. This read-only page does not "
            "access a controller, network, or physical hardware.");
    } else if (repositoryModeEmpty) {
        modeDescription = Tr::tr(
            "No ESI Distributed Clocks operation mode is available for this repository device. "
            "Add it to an offline Project to enter manual timing. This read-only page does not "
            "access a controller, network, or physical hardware.");
    } else if (repositoryDeviceMissing) {
        modeDescription = Tr::tr(
            "The ESI device description is unavailable, so no Distributed Clocks operation mode "
            "can be selected. Return to Device Repository and select an available device. This "
            "read-only page does not access a controller, network, or physical hardware.");
    }
    m_operationMode->setAccessibleDescription(modeDescription);
    m_operationMode->setToolTip(modeDescription);
    m_enabled->setEnabled(m_editable);
    m_assignActivate->setReadOnly(!m_editable);
    m_sync0Enabled->setEnabled(m_editable);
    m_sync0Cycle->setReadOnly(!m_editable);
    m_sync0Shift->setReadOnly(!m_editable);
    m_sync1Enabled->setEnabled(m_editable);
    m_sync1Cycle->setReadOnly(!m_editable);
    m_sync1Shift->setReadOnly(!m_editable);
    m_potentialReferenceClock->setEnabled(m_editable);
}

void DcPage::selectEsiMode(int index)
{
    if (m_rebuilding || index < 0 || index >= m_operationMode->count())
        return;
    int esiIndex = m_operationMode->itemData(index).toInt();
    if (m_operationModeItemsDeferred) {
        if (esiIndex < 0) {
            const QScopedValueRollback forceModeReload(
                m_forceAuthoritativeDrafts,
                DraftFields(m_forceAuthoritativeDrafts | ModeNameDraft));
            Data::DcConfiguration candidate = m_configuration;
            candidate.modeName = m_operationMode->currentText().trimmed();
            submitConfiguration(candidate);
            return;
        }
        if (esiIndex >= m_visibleEsiModes.size())
            return;
        const Data::DcModeDescription selectedMode = m_visibleEsiModes.at(esiIndex);
        const auto selected = std::find_if(
            m_esiModes.cbegin(), m_esiModes.cend(), [&selectedMode](const auto &mode) {
                return mode == selectedMode;
            });
        if (selected == m_esiModes.cend()) {
            const QScopedValueRollback forceModeReload(
                m_forceAuthoritativeDrafts,
                DraftFields(m_forceAuthoritativeDrafts | ModeNameDraft));
            reloadCurrentContext();
            showValidation(
                Data::validateDcConfiguration(m_configuration),
                Tr::tr("ESI operation modes changed; select a current mode."));
            return;
        }
        esiIndex = int(std::distance(m_esiModes.cbegin(), selected));
    }
    if (esiIndex < 0 || esiIndex >= m_esiModes.size())
        return;
    const QScopedValueRollback forceReload(
        m_forceAuthoritativeDrafts,
        DraftFields(m_forceAuthoritativeDrafts | AllDrafts));
    Data::DcConfiguration candidate = dcConfigurationFromMode(m_esiModes.at(esiIndex));
    if (m_context.nodeKind == Core::WorkbenchNodeKind::Device) {
        m_configuration = candidate;
        rebuildControls();
        return;
    }
    if (!m_editable)
        return;
    candidate.potentialReferenceClock = m_configuration.potentialReferenceClock;
    submitConfiguration(candidate);
}

void DcPage::commitModeName()
{
    if (m_rebuilding || !m_editable
        || (!m_operationMode->lineEdit()->isModified() && !m_operationModeItemsDeferred)) {
        return;
    }
    const QScopedValueRollback forceReload(
        m_forceAuthoritativeDrafts,
        DraftFields(m_forceAuthoritativeDrafts | ModeNameDraft));
    if (!m_operationMode->lineEdit()->isModified()) {
        reloadCurrentContext();
        return;
    }
    m_operationMode->lineEdit()->setModified(false);
    Data::DcConfiguration candidate = m_configuration;
    candidate.modeName = m_operationMode->currentText().trimmed();
    submitConfiguration(candidate);
}

void DcPage::commitAssignActivate()
{
    if (m_rebuilding || !m_editable || !m_assignActivate->isModified())
        return;
    const QScopedValueRollback forceReload(
        m_forceAuthoritativeDrafts,
        DraftFields(m_forceAuthoritativeDrafts | AssignActivateDraft));
    m_assignActivate->setModified(false);
    quint16 value = 0;
    if (!parseAssignActivate(m_assignActivate->text(), &value)) {
        rejectInput(Tr::tr("AssignActivate must be a decimal or hexadecimal 16-bit value."));
        return;
    }
    Data::DcConfiguration candidate = m_configuration;
    candidate.assignActivate = value;
    submitConfiguration(candidate);
}

void DcPage::commitSignalValue(bool sync1, SignalField field, QLineEdit *editor)
{
    if (m_rebuilding || !m_editable || !editor->isModified())
        return;
    const DraftField draftField = sync1 ? field == SignalField::CycleTime ? Sync1CycleDraft
                                                                            : Sync1ShiftDraft
                                        : field == SignalField::CycleTime ? Sync0CycleDraft
                                                                            : Sync0ShiftDraft;
    const QScopedValueRollback forceReload(
        m_forceAuthoritativeDrafts,
        DraftFields(m_forceAuthoritativeDrafts | draftField));
    editor->setModified(false);
    qint64 value = 0;
    if (!parseNanoseconds(editor->text(), &value)) {
        rejectInput(Tr::tr("Cycle and shift times must be whole nanoseconds."));
        return;
    }
    Data::DcConfiguration candidate = m_configuration;
    Data::DcSignalConfiguration &signal = sync1 ? candidate.sync1 : candidate.sync0;
    if (field == SignalField::CycleTime)
        signal.cycleTimeNs = value;
    else
        signal.shiftTimeNs = value;
    submitConfiguration(candidate);
}

void DcPage::showValidation(const QList<Data::ConfigurationIssue> &issues, const QString &prefix)
{
    m_editRejectionActive = false;
    int errorCount = 0;
    int warningCount = 0;
    QStringList details;
    for (const Data::ConfigurationIssue &issue : issues) {
        details.append(issue.message);
        if (issue.severity == Data::ConfigurationIssueSeverity::Error)
            ++errorCount;
        else if (issue.severity == Data::ConfigurationIssueSeverity::Warning)
            ++warningCount;
    }

    QString text = prefix;
    if (!text.isEmpty() && !details.isEmpty())
        text += ' ';
    const bool repositoryDeviceMissing
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device
          && !m_repositoryDeviceAvailable;
    const bool repositoryModeEmpty
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device && m_repositoryDeviceAvailable
          && m_esiModes.isEmpty();
    const bool repositoryModeEmptyUnsupported
        = repositoryModeEmpty && !m_repositoryDeviceSupported;
    const bool repositoryModePreviewUnsupported
        = m_context.nodeKind == Core::WorkbenchNodeKind::Device && m_repositoryDeviceAvailable
          && !m_repositoryDeviceSupported && !m_esiModes.isEmpty();
    const QString unsupportedPreviewNotice
        = repositoryModePreviewUnsupported
              ? Tr::tr("This Device contains unsupported ESI structures and cannot be added to "
                       "an offline Project. Review its support details in Device Repository.")
              : QString();
    if (repositoryDeviceMissing) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr(
            "The ESI device description is unavailable, so no Distributed Clocks data can be "
            "shown.");
    } else if (repositoryModeEmptyUnsupported) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr(
            "No ESI Distributed Clocks operation mode is available. This device contains "
            "unsupported ESI structures and cannot be added to an offline Project. Review its "
            "support details in Device Repository.");
    } else if (repositoryModeEmpty) {
        m_validation->setType(Utils::InfoLabel::Information);
        text += Tr::tr("No ESI Distributed Clocks operation mode is available to preview.");
    } else if (errorCount > 0) {
        m_validation->setType(Utils::InfoLabel::Error);
        text += Tr::tr("%n Distributed Clocks configuration error(s).", nullptr, errorCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
        if (repositoryModePreviewUnsupported)
            text += ' ' + unsupportedPreviewNotice;
    } else if (warningCount > 0) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr("%n Distributed Clocks configuration warning(s).", nullptr, warningCount);
        if (!details.isEmpty())
            text += ' ' + details.first();
        if (repositoryModePreviewUnsupported)
            text += ' ' + unsupportedPreviewNotice;
    } else if (repositoryModePreviewUnsupported) {
        m_validation->setType(Utils::InfoLabel::Warning);
        text += Tr::tr("This Distributed Clocks mode is available only for read-only preview. ");
        text += unsupportedPreviewNotice;
    } else {
        m_validation->setType(Utils::InfoLabel::Ok);
        text += m_configuration.enabled
                    ? Tr::tr("Distributed Clocks configuration is valid.")
                    : Tr::tr("Distributed Clocks are disabled; the offline configuration is valid.");
    }
    m_validation->setText(text);
    m_validation->setAccessibleDescription(text);
    m_validation->setAdditionalToolTip(details.join('\n'));
    m_validation->setToolTip(details.join('\n'));
}

void DcPage::rejectInput(const QString &message)
{
    reloadCurrentContext();
    setEditRejection(Tr::tr("Change not applied. %1").arg(message));
}

void DcPage::setEditRejection(const QString &message, const QString &completeMessage)
{
    const QString feedback = completeMessage.isEmpty() ? message : completeMessage;
    m_editRejectionActive = true;
    m_validation->setType(Utils::InfoLabel::Error);
    m_validation->setText(message);
    m_validation->setAccessibleDescription(feedback);
    m_validation->setAdditionalToolTip(feedback);
    m_validation->setToolTip(feedback);
#if QT_CONFIG(accessibility)
    QAccessibleAnnouncementEvent announcement(m_validation, feedback);
    announcement.setPoliteness(QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&announcement);
#endif
}

void DcPage::clearEditRejection()
{
    if (m_editRejectionActive)
        showValidation(Data::validateDcConfiguration(m_configuration));
}

} // namespace EtherCAT::Workbench::Internal
