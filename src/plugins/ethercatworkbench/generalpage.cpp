// Copyright (C) 2026 Kvell

#include "generalpage.h"

#include "esidevicegeneralpage.h"
#include "esirepositorypage.h"
#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMargins>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace EtherCAT::Workbench::Internal {

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static QString controllerAlStateName(quint32 alState)
{
    if (alState & 0x08)
        return "OP";
    if (alState & 0x04)
        return "SAFEOP";
    if (alState & 0x02)
        return "PREOP";
    if (alState & 0x01)
        return "INIT";
    return hexValue(alState, 4);
}

static int structuralNodeOrdinal(
    const Data::ProjectSnapshot &project, Data::ProjectNodeKind kind, const Data::NodeId &nodeId)
{
    int ordinal = 0;
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind != kind)
            continue;
        ++ordinal;
        if (node.id == nodeId)
            return ordinal;
    }
    return -1;
}

static QString firstNodeName(
    const Data::ProjectSnapshot &project, Data::ProjectNodeKind kind, const QString &fallback)
{
    const auto node = std::find_if(
        project.nodes.cbegin(), project.nodes.cend(), [kind](const auto &entry) {
            return entry.kind == kind;
        });
    return node == project.nodes.cend() ? fallback : node->name;
}

static std::optional<QString> projectNodeName(
    const Data::ProjectSnapshot &project,
    const Data::NodeId &nodeId,
    Data::ProjectNodeKind kind)
{
    const auto node = std::find_if(
        project.nodes.cbegin(), project.nodes.cend(), [&nodeId, kind](const auto &entry) {
            return entry.id == nodeId && entry.kind == kind;
        });
    return node == project.nodes.cend() ? std::nullopt : std::optional<QString>(node->name);
}

static std::optional<QString> projectSlaveName(
    const Data::ProjectSnapshot &project, const Data::NodeId &nodeId)
{
    const auto slave = std::find_if(
        project.slaves.cbegin(), project.slaves.cend(), [&nodeId](const auto &entry) {
            return entry.id == nodeId;
        });
    return slave == project.slaves.cend() ? std::nullopt
                                           : std::optional<QString>(slave->name);
}

static QString projectValidityText(const Data::ProjectSnapshot &project)
{
    if (project.valid)
        return Tr::tr("Valid");
    return project.error.isEmpty() ? Tr::tr("Invalid")
                                   : Tr::tr("Invalid: %1").arg(project.error);
}

static QString masterTimingModeName(Data::MasterTimingMode mode)
{
    switch (mode) {
    case Data::MasterTimingMode::Unassigned:
        return Tr::tr("Not assigned");
    case Data::MasterTimingMode::FreeRun:
        return Tr::tr("FreeRun");
    case Data::MasterTimingMode::DistributedClocks:
        return Tr::tr("Distributed Clocks");
    }
    return Tr::tr("Unknown");
}

GeneralPage::GeneralPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_nameFeedback(new Utils::InfoLabel(this))
    , m_repositoryPage(
          new EsiRepositoryPage(controller ? controller->deviceRepository() : nullptr, this))
    , m_esiDevicePage(new EsiDeviceGeneralPage(this))
    , m_projectContent(new QWidget(this))
    , m_projectForm(new QWidget(m_projectContent))
    , m_projectName(new QLineEdit(m_projectForm))
    , m_projectId(new QLineEdit(m_projectForm))
    , m_projectType(new QLineEdit(m_projectForm))
    , m_projectSummaryForm(
          new QGroupBox(Tr::tr("Project main information"), m_projectContent))
    , m_projectFormatVersion(new QLineEdit(m_projectSummaryForm))
    , m_projectCreatedBy(new QLineEdit(m_projectSummaryForm))
    , m_projectValidity(new QLineEdit(m_projectSummaryForm))
    , m_projectMigration(new QLineEdit(m_projectSummaryForm))
    , m_projectModified(new QLineEdit(m_projectSummaryForm))
    , m_projectTarget(new QLineEdit(m_projectSummaryForm))
    , m_projectMaster(new QLineEdit(m_projectSummaryForm))
    , m_projectTimingMode(new QLineEdit(m_projectSummaryForm))
    , m_projectCycle(new QLineEdit(m_projectSummaryForm))
    , m_projectSlaveCount(new QLineEdit(m_projectSummaryForm))
    , m_identityForm(new QWidget(this))
    , m_name(new QLineEdit(m_identityForm))
    , m_id(new QLineEdit(m_identityForm))
    , m_objectId(new QLineEdit(m_identityForm))
    , m_type(new QLineEdit(m_identityForm))
    , m_targetContent(new QWidget(this))
    , m_targetHeader(new QWidget(m_targetContent))
    , m_targetIcon(new QLabel(m_targetHeader))
    , m_targetName(new QLineEdit(m_targetHeader))
    , m_targetIdentity(new QLabel(m_targetHeader))
    , m_chooseTarget(new QPushButton(Tr::tr("Choose Target..."), m_targetHeader))
    , m_targetVersionForm(new QGroupBox(Tr::tr("Version"), m_targetContent))
    , m_targetEngineering(new QLabel(m_targetVersionForm))
    , m_targetRuntime(new QLabel(m_targetVersionForm))
    , m_targetLocalRuntime(new QLabel(m_targetVersionForm))
    , m_targetProjectVersion(new QLabel(m_targetVersionForm))
    , m_targetPinVersion(new QCheckBox(Tr::tr("Pin Version"), m_targetVersionForm))
    , m_masterContent(new QWidget(this))
    , m_masterForm(new QWidget(m_masterContent))
    , m_masterName(new QLineEdit(m_masterForm))
    , m_masterId(new QLineEdit(m_masterForm))
    , m_masterObjectId(new QLineEdit(m_masterForm))
    , m_masterType(new QLineEdit(m_masterForm))
    , m_masterComment(new QPlainTextEdit(m_masterForm))
    , m_masterDisabled(new QCheckBox(Tr::tr("Disabled"), m_masterForm))
    , m_masterCreateSymbols(new QCheckBox(Tr::tr("Create symbols"), m_masterForm))
    , m_masterSummaryForm(new QGroupBox(Tr::tr("Cycle configuration"), m_masterContent))
    , m_masterTimingMode(new QComboBox(m_masterSummaryForm))
    , m_masterCycle(new QLineEdit(m_masterSummaryForm))
    , m_masterSlaveCount(new QLineEdit(m_masterSummaryForm))
    , m_masterStatus(new QLineEdit(m_masterSummaryForm))
    , m_masterScanProvider(new QComboBox(m_masterSummaryForm))
    , m_masterApply(new QPushButton(Tr::tr("Apply"), m_masterSummaryForm))
    , m_tree(new QTreeWidget(this))
{
    m_summary->setObjectName("EtherCATWorkbenchPageSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_nameFeedback->setObjectName("EtherCATGeneralNameFeedback");
    m_nameFeedback->setElideMode(Qt::ElideNone);
    m_nameFeedback->setWordWrap(true);
    m_nameFeedback->setAccessibleName(Tr::tr("General name edit feedback"));
    m_nameFeedback->hide();

    m_projectContent->setObjectName("EtherCATProjectGeneralContent");
    m_projectForm->setObjectName("EtherCATProjectGeneralForm");
    m_projectName->setObjectName("EtherCATProjectGeneralName");
    m_projectId->setObjectName("EtherCATProjectGeneralId");
    m_projectType->setObjectName("EtherCATProjectGeneralType");
    m_projectSummaryForm->setObjectName("EtherCATProjectConfigurationSummary");
    m_projectFormatVersion->setObjectName("EtherCATProjectGeneralFormatVersion");
    m_projectCreatedBy->setObjectName("EtherCATProjectGeneralCreatedBy");
    m_projectValidity->setObjectName("EtherCATProjectGeneralValidity");
    m_projectMigration->setObjectName("EtherCATProjectGeneralMigration");
    m_projectModified->setObjectName("EtherCATProjectGeneralModified");
    m_projectTarget->setObjectName("EtherCATProjectGeneralTarget");
    m_projectMaster->setObjectName("EtherCATProjectGeneralMaster");
    m_projectTimingMode->setObjectName("EtherCATProjectGeneralTimingMode");
    m_projectCycle->setObjectName("EtherCATProjectGeneralCyclePeriod");
    m_projectSlaveCount->setObjectName("EtherCATProjectGeneralSlaveCount");
    m_projectName->setAccessibleName(Tr::tr("EtherCAT project name"));
    m_projectId->setAccessibleName(Tr::tr("EtherCAT project ID"));
    m_projectType->setAccessibleName(Tr::tr("EtherCAT project type"));
    m_projectFormatVersion->setAccessibleName(Tr::tr("EtherCAT project format version"));
    m_projectCreatedBy->setAccessibleName(Tr::tr("EtherCAT project creation tool"));
    m_projectValidity->setAccessibleName(Tr::tr("EtherCAT project validity"));
    m_projectMigration->setAccessibleName(Tr::tr("EtherCAT project migration state"));
    m_projectModified->setAccessibleName(Tr::tr("EtherCAT project modified state"));
    m_projectTarget->setAccessibleName(Tr::tr("Offline target name"));
    m_projectMaster->setAccessibleName(Tr::tr("EtherCAT master name"));
    m_projectTimingMode->setAccessibleName(Tr::tr("Project EtherCAT timing mode"));
    m_projectCycle->setAccessibleName(Tr::tr("Project EtherCAT cycle period"));
    m_projectSlaveCount->setAccessibleName(Tr::tr("Configured EtherCAT slave count"));
    m_projectId->setToolTip(
        Tr::tr("Stable offline project identifier; this is not an ADS or runtime ID."));
    m_projectType->setToolTip(
        Tr::tr("Local engineering project type; no PLC runtime is represented."));
    for (QLineEdit *field :
         {m_projectId,
          m_projectType,
          m_projectFormatVersion,
          m_projectCreatedBy,
          m_projectValidity,
          m_projectMigration,
          m_projectModified,
          m_projectTarget,
          m_projectMaster,
          m_projectTimingMode,
          m_projectCycle,
          m_projectSlaveCount}) {
        field->setReadOnly(true);
    }

    auto projectForm = new QFormLayout(m_projectForm);
    projectForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    projectForm->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    projectForm->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    projectForm->addRow(Tr::tr("Project name:"), m_projectName);
    projectForm->addRow(Tr::tr("Project ID:"), m_projectId);
    projectForm->addRow(Tr::tr("Project type:"), m_projectType);

    auto projectSummary = new QFormLayout(m_projectSummaryForm);
    projectSummary->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    projectSummary->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    projectSummary->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    projectSummary->addRow(Tr::tr("Validity:"), m_projectValidity);
    projectSummary->addRow(Tr::tr("Offline target:"), m_projectTarget);
    projectSummary->addRow(Tr::tr("EtherCAT master:"), m_projectMaster);
    projectSummary->addRow(Tr::tr("Timing mode:"), m_projectTimingMode);
    projectSummary->addRow(Tr::tr("Cycle period (ns):"), m_projectCycle);
    projectSummary->addRow(Tr::tr("Configured slaves:"), m_projectSlaveCount);
    projectSummary->addRow(Tr::tr("Modified:"), m_projectModified);
    projectSummary->addRow(Tr::tr("Format version:"), m_projectFormatVersion);
    projectSummary->addRow(Tr::tr("Created by:"), m_projectCreatedBy);
    projectSummary->addRow(Tr::tr("Migration:"), m_projectMigration);

    auto projectContentLayout = new QVBoxLayout(m_projectContent);
    projectContentLayout->setContentsMargins(QMargins());
    projectContentLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    projectContentLayout->addWidget(m_projectForm);
    projectContentLayout->addWidget(m_projectSummaryForm);
    projectContentLayout->addStretch(1);

    m_identityForm->setObjectName("EtherCATGeneralIdentityForm");
    m_name->setObjectName("EtherCATGeneralName");
    m_id->setObjectName("EtherCATGeneralId");
    m_objectId->setObjectName("EtherCATGeneralObjectId");
    m_type->setObjectName("EtherCATGeneralType");
    m_name->setAccessibleName(Tr::tr("EtherCAT device name"));
    m_id->setAccessibleName(Tr::tr("EtherCAT device ID"));
    m_objectId->setAccessibleName(Tr::tr("EtherCAT object ID"));
    m_type->setAccessibleName(Tr::tr("EtherCAT device type"));
    m_id->setToolTip(Tr::tr("One-based identifier derived from the offline physical order."));
    m_objectId->setToolTip(Tr::tr("Stable project node identifier."));
    m_id->setReadOnly(true);
    m_objectId->setReadOnly(true);
    m_type->setReadOnly(true);

    auto form = new QFormLayout(m_identityForm);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    form->addRow(Tr::tr("Name:"), m_name);
    form->addRow(Tr::tr("Id:"), m_id);
    form->addRow(Tr::tr("Object Id:"), m_objectId);
    form->addRow(Tr::tr("Type:"), m_type);

    m_targetContent->setObjectName("EtherCATTargetGeneralContent");
    m_targetHeader->setObjectName("EtherCATTargetGeneralHeader");
    m_targetIcon->setObjectName("EtherCATTargetGeneralIcon");
    m_targetName->setObjectName("EtherCATTargetGeneralName");
    m_targetIdentity->setObjectName("EtherCATTargetGeneralIdentity");
    m_chooseTarget->setObjectName("EtherCATTargetGeneralChooseTarget");
    m_targetVersionForm->setObjectName("EtherCATTargetGeneralVersion");
    m_targetEngineering->setObjectName("EtherCATTargetGeneralEngineering");
    m_targetRuntime->setObjectName("EtherCATTargetGeneralTargetRuntime");
    m_targetLocalRuntime->setObjectName("EtherCATTargetGeneralLocalRuntime");
    m_targetProjectVersion->setObjectName("EtherCATTargetGeneralProjectVersion");
    m_targetPinVersion->setObjectName("EtherCATTargetGeneralPinVersion");
    m_targetContent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_targetHeader->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_targetVersionForm->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_targetName->setAccessibleName(Tr::tr("Offline target name"));
    m_targetIdentity->setAccessibleName(Tr::tr("Offline target identity"));
    m_targetIcon->setAccessibleName(Tr::tr("Offline target"));
    m_chooseTarget->setAccessibleName(Tr::tr("Choose target system"));
    m_targetEngineering->setAccessibleName(Tr::tr("Engineering version"));
    m_targetRuntime->setAccessibleName(Tr::tr("Target runtime version"));
    m_targetLocalRuntime->setAccessibleName(Tr::tr("Local runtime version"));
    m_targetProjectVersion->setAccessibleName(Tr::tr("Project version"));
    m_targetPinVersion->setAccessibleName(Tr::tr("Pin engineering version"));
    m_targetIdentity->setWordWrap(true);
    m_targetIdentity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    for (QLabel *value :
         {m_targetEngineering, m_targetRuntime, m_targetLocalRuntime, m_targetProjectVersion}) {
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
    }
    const int targetIconExtent
        = style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, m_targetIcon);
    m_targetIcon->setPixmap(
        style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(targetIconExtent, targetIconExtent));
    m_targetIcon->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_targetIcon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    const QString targetUnavailableTip = Tr::tr(
        "Target discovery and runtime selection are not available in the phase 1 local Mock.");
    m_chooseTarget->setToolTip(targetUnavailableTip);
    m_chooseTarget->setAccessibleDescription(targetUnavailableTip);
    m_chooseTarget->setEnabled(false);
    const QString versionUnavailableTip = Tr::tr(
        "The phase 1 offline project does not persist or activate a target runtime version.");
    m_targetPinVersion->setToolTip(versionUnavailableTip);
    m_targetPinVersion->setAccessibleDescription(versionUnavailableTip);
    m_targetPinVersion->setEnabled(false);

    auto targetHeaderLayout = new QGridLayout(m_targetHeader);
    targetHeaderLayout->setContentsMargins(QMargins());
    targetHeaderLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    targetHeaderLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    targetHeaderLayout->setColumnStretch(1, 1);
    targetHeaderLayout->addWidget(m_targetIcon, 0, 0, 2, 1);
    targetHeaderLayout->addWidget(m_targetName, 0, 1);
    targetHeaderLayout->addWidget(m_chooseTarget, 0, 2, Qt::AlignTop);
    targetHeaderLayout->addWidget(m_targetIdentity, 1, 1, 1, 2);

    auto targetVersionLayout = new QGridLayout(m_targetVersionForm);
    targetVersionLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    targetVersionLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    targetVersionLayout->setColumnStretch(1, 1);
    targetVersionLayout->setColumnStretch(3, 1);
    targetVersionLayout->addWidget(new QLabel(Tr::tr("Engineering"), m_targetVersionForm), 0, 0);
    targetVersionLayout->addWidget(m_targetEngineering, 0, 1, 1, 3);
    targetVersionLayout->addWidget(new QLabel(Tr::tr("Target"), m_targetVersionForm), 1, 0);
    targetVersionLayout->addWidget(m_targetRuntime, 1, 1);
    targetVersionLayout->addWidget(new QLabel(Tr::tr("Local"), m_targetVersionForm), 1, 2);
    targetVersionLayout->addWidget(m_targetLocalRuntime, 1, 3);
    targetVersionLayout->addWidget(new QLabel(Tr::tr("Project"), m_targetVersionForm), 2, 0);
    targetVersionLayout->addWidget(m_targetProjectVersion, 2, 1);
    targetVersionLayout->addWidget(m_targetPinVersion, 2, 2, 1, 2);

    auto targetContentLayout = new QVBoxLayout(m_targetContent);
    targetContentLayout->setContentsMargins(QMargins());
    targetContentLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    targetContentLayout->addWidget(m_targetHeader);
    targetContentLayout->addWidget(m_targetVersionForm);
    targetContentLayout->addStretch(1);

    m_masterContent->setObjectName("EtherCATMasterGeneralContent");
    m_masterForm->setObjectName("EtherCATMasterGeneralForm");
    m_masterName->setObjectName("EtherCATMasterGeneralName");
    m_masterId->setObjectName("EtherCATMasterGeneralId");
    m_masterObjectId->setObjectName("EtherCATMasterGeneralObjectId");
    m_masterType->setObjectName("EtherCATMasterGeneralType");
    m_masterComment->setObjectName("EtherCATMasterGeneralComment");
    m_masterDisabled->setObjectName("EtherCATMasterGeneralDisabled");
    m_masterCreateSymbols->setObjectName("EtherCATMasterGeneralCreateSymbols");
    m_masterForm->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_masterName->setAccessibleName(Tr::tr("EtherCAT master name"));
    m_masterId->setAccessibleName(Tr::tr("EtherCAT master ID"));
    m_masterObjectId->setAccessibleName(Tr::tr("EtherCAT master object ID"));
    m_masterType->setAccessibleName(Tr::tr("EtherCAT master type"));
    m_masterComment->setAccessibleName(Tr::tr("EtherCAT master comment"));
    m_masterDisabled->setAccessibleName(Tr::tr("Disable EtherCAT master"));
    m_masterCreateSymbols->setAccessibleName(Tr::tr("Create EtherCAT master symbols"));
    m_masterId->setToolTip(Tr::tr("One-based identifier of this master in the project."));
    m_masterObjectId->setToolTip(Tr::tr("Stable project node identifier."));
    const QString unavailableTip = Tr::tr(
        "The current offline project contract does not store this TwinCAT-style setting.");
    m_masterComment->setToolTip(unavailableTip);
    m_masterComment->setAccessibleDescription(unavailableTip);
    m_masterComment->setPlaceholderText(Tr::tr("Not available in phase 1"));
    m_masterDisabled->setToolTip(unavailableTip);
    m_masterDisabled->setAccessibleDescription(unavailableTip);
    m_masterCreateSymbols->setToolTip(unavailableTip);
    m_masterCreateSymbols->setAccessibleDescription(unavailableTip);
    m_masterId->setReadOnly(true);
    m_masterObjectId->setReadOnly(true);
    m_masterType->setReadOnly(true);
    m_masterComment->setReadOnly(true);
    constexpr int commentVisibleLines = 5;
    const int commentHeight = m_masterComment->fontMetrics().lineSpacing() * commentVisibleLines
                              + 2
                                    * m_masterComment->style()->pixelMetric(
                                        QStyle::PM_DefaultFrameWidth);
    m_masterComment->setFixedHeight(commentHeight);
    m_masterComment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_masterDisabled->setEnabled(false);
    m_masterCreateSymbols->setEnabled(false);

    auto masterGrid = new QGridLayout(m_masterForm);
    masterGrid->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    masterGrid->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    masterGrid->setColumnStretch(1, 1);
    auto nameLabel = new QLabel(Tr::tr("Name:"), m_masterForm);
    nameLabel->setBuddy(m_masterName);
    auto idLabel = new QLabel(Tr::tr("Id:"), m_masterForm);
    idLabel->setBuddy(m_masterId);
    auto objectIdLabel = new QLabel(Tr::tr("Object Id:"), m_masterForm);
    objectIdLabel->setBuddy(m_masterObjectId);
    auto typeLabel = new QLabel(Tr::tr("Type:"), m_masterForm);
    typeLabel->setBuddy(m_masterType);
    auto commentLabel = new QLabel(Tr::tr("Comment:"), m_masterForm);
    commentLabel->setBuddy(m_masterComment);
    commentLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    masterGrid->addWidget(nameLabel, 0, 0);
    masterGrid->addWidget(m_masterName, 0, 1);
    masterGrid->addWidget(idLabel, 0, 2);
    masterGrid->addWidget(m_masterId, 0, 3);
    masterGrid->addWidget(objectIdLabel, 1, 0);
    masterGrid->addWidget(m_masterObjectId, 1, 1, 1, 3);
    masterGrid->addWidget(typeLabel, 2, 0);
    masterGrid->addWidget(m_masterType, 2, 1, 1, 3);
    masterGrid->addWidget(commentLabel, 3, 0);
    masterGrid->addWidget(m_masterComment, 3, 1, 1, 3);
    masterGrid->addWidget(m_masterDisabled, 4, 1);
    masterGrid->addWidget(m_masterCreateSymbols, 4, 3, Qt::AlignRight);

    m_masterSummaryForm->setObjectName("EtherCATMasterConfigurationSummary");
    m_masterSummaryForm->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_masterTimingMode->setObjectName("EtherCATMasterGeneralTimingMode");
    m_masterCycle->setObjectName("EtherCATMasterGeneralCycle");
    m_masterSlaveCount->setObjectName("EtherCATMasterGeneralSlaveCount");
    m_masterStatus->setObjectName("EtherCATMasterGeneralStatus");
    m_masterScanProvider->setObjectName("EtherCATMasterGeneralScanProvider");
    m_masterApply->setObjectName("EtherCATMasterGeneralApply");
    m_masterTimingMode->setAccessibleName(Tr::tr("EtherCAT master timing mode"));
    m_masterCycle->setAccessibleName(Tr::tr("EtherCAT master cycle period"));
    m_masterSlaveCount->setAccessibleName(Tr::tr("Configured EtherCAT slave count"));
    m_masterStatus->setAccessibleName(Tr::tr("EtherCAT master status summary"));
    m_masterScanProvider->setAccessibleName(Tr::tr("Mock topology Scan Provider"));
    m_masterApply->setAccessibleName(Tr::tr("Apply EtherCAT master cycle configuration"));
    m_masterTimingMode->addItem(
        Tr::tr("Not assigned"), int(Data::MasterTimingMode::Unassigned));
    m_masterTimingMode->addItem(Tr::tr("FreeRun"), int(Data::MasterTimingMode::FreeRun));
    m_masterTimingMode->addItem(
        Tr::tr("Distributed Clocks"), int(Data::MasterTimingMode::DistributedClocks));
    m_masterCycle->setToolTip(
        Tr::tr("Master cycle period in nanoseconds; it must match the selected bus configuration."));
    m_masterCycle->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[0-9]{0,10}"), m_masterCycle));
    m_masterStatus->setToolTip(
        Tr::tr("Summary from the shared Workbench scan and diagnostics presentation."));
    const QString scanProviderTip = Tr::tr(
        "Select the exact Scan Provider whose completed Mock topology may be shown for this "
        "Master. This selection does not start, cancel, or clear a scan and does not contact a "
        "controller.");
    m_masterScanProvider->setToolTip(scanProviderTip);
    m_masterScanProvider->setAccessibleDescription(scanProviderTip);
    m_masterSlaveCount->setReadOnly(true);
    m_masterStatus->setReadOnly(true);
    auto masterSummary = new QFormLayout(m_masterSummaryForm);
    masterSummary->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    masterSummary->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    masterSummary->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    masterSummary->addRow(Tr::tr("Timing mode:"), m_masterTimingMode);
    masterSummary->addRow(Tr::tr("Cycle period (ns):"), m_masterCycle);
    masterSummary->addRow(m_masterApply);
    masterSummary->addRow(Tr::tr("Configured slaves:"), m_masterSlaveCount);
    masterSummary->addRow(Tr::tr("Status:"), m_masterStatus);
    masterSummary->addRow(Tr::tr("Mock topology:"), m_masterScanProvider);

    auto masterContentLayout = new QVBoxLayout(m_masterContent);
    masterContentLayout->setContentsMargins(QMargins());
    masterContentLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    masterContentLayout->addWidget(m_masterForm);
    masterContentLayout->addWidget(m_masterSummaryForm);
    masterContentLayout->addStretch(1);

    m_tree->setObjectName("EtherCATWorkbenchPageTree");
    m_tree->setAccessibleName(Tr::tr("EtherCAT General properties"));
    m_tree->setAccessibleDescription(Tr::tr(
        "Read-only engineering and live identity values for the selected EtherCAT node. "
        "Viewing these values does not perform a controller operation."));
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_summary);
    layout->addWidget(m_nameFeedback);
    layout->addWidget(m_repositoryPage, 1);
    layout->addWidget(m_esiDevicePage, 1);
    layout->addWidget(m_projectContent, 1);
    layout->addWidget(m_targetContent, 1);
    layout->addWidget(m_masterContent, 1);
    layout->addWidget(m_identityForm);
    layout->addWidget(m_tree, 1);

    connect(m_projectName, &QLineEdit::editingFinished, this, &GeneralPage::commitProjectName);
    connect(m_name, &QLineEdit::editingFinished, this, &GeneralPage::commitName);
    connect(m_targetName, &QLineEdit::editingFinished, this, &GeneralPage::commitTargetName);
    connect(m_masterName, &QLineEdit::editingFinished, this, &GeneralPage::commitMasterName);
    connect(m_masterApply, &QPushButton::clicked, this, &GeneralPage::commitMasterConfiguration);
    connect(
        m_masterScanProvider,
        &QComboBox::activated,
        this,
        &GeneralPage::commitMasterScanProvider);
    for (QLineEdit *name : {m_projectName, m_name, m_targetName, m_masterName}) {
        connect(name, &QLineEdit::textEdited, this, [this] { clearNameFeedback(); });
    }
    if (m_controller) {
        connect(
            m_controller->treeModel(),
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
                refreshMasterSummary();
            });
        connect(
            m_controller->treeModel(),
            &QAbstractItemModel::modelReset,
            this,
            &GeneralPage::refreshMasterSummary);
        connect(
            m_controller,
            &WorkbenchController::scanProviderChanged,
            this,
            &GeneralPage::refreshMasterSummary);
    }
    reset({});
}

void GeneralPage::setContext(const Core::PropertyPageContext &context)
{
    clearNameFeedback();
    const std::optional<Data::OfflineSlaveConfiguration> offlineSlave
        = m_controller ? m_controller->treeModel()->offlineSlave(context.nodeId) : std::nullopt;
    const std::optional<Data::ControllerTopologySlave> controllerSlave
        = m_controller
              ? m_controller->treeModel()->controllerTopologySlave(context.nodeId)
              : std::nullopt;
    const std::optional<Data::DeviceDescription> device =
        [this, &context, &offlineSlave, &controllerSlave]()
        -> std::optional<Data::DeviceDescription> {
        if (!m_controller || !m_controller->deviceRepository())
            return std::nullopt;
        if (context.nodeKind == Core::WorkbenchNodeKind::Device)
            return m_controller->deviceRepository()->device(context.nodeId);
        if (controllerSlave) {
            const Data::NodeId sourceId = m_controller->treeModel()->sourceNodeId(context.nodeId);
            if (!sourceId.isNull())
                return m_controller->deviceRepository()->device(sourceId);
        }
        if (offlineSlave && !offlineSlave->deviceDescriptionId.isNull()) {
            return m_controller->deviceRepository()->device(offlineSlave->deviceDescriptionId);
        }
        return std::nullopt;
    }();
    const std::optional<Data::ProjectSnapshot> project
        = !context.projectId.isNull() && m_controller && m_controller->projectService()
              ? m_controller->projectService()->project(context.projectId)
              : std::nullopt;

    const bool configuredSlave = context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
                                 && offlineSlave.has_value();
    const std::optional<QString> authoritativeName = [&]() -> std::optional<QString> {
        if (!project)
            return std::nullopt;
        if (context.nodeKind == Core::WorkbenchNodeKind::Project) {
            return project->id == context.projectId && project->id == context.nodeId
                       ? std::optional<QString>(project->name)
                       : std::nullopt;
        }
        if (configuredSlave)
            return projectSlaveName(*project, context.nodeId);
        if (context.nodeKind == Core::WorkbenchNodeKind::Target) {
            return projectNodeName(
                *project, context.nodeId, Data::ProjectNodeKind::Target);
        }
        if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
            return projectNodeName(
                *project, context.nodeId, Data::ProjectNodeKind::Master);
        }
        return std::nullopt;
    }();
    const QString displayedName
        = authoritativeName.value_or(configuredSlave ? offlineSlave->name : context.displayName);
    const bool editableName = authoritativeName.has_value() && project && project->valid;
    QLineEdit *previousName = nameEditor(m_context.nodeKind);
    const bool preserveName
        = !m_forceAuthoritativeNameReload && editableName && previousName
          && !previousName->isReadOnly()
          && (previousName->isModified() || previousName->hasFocus())
          && m_context.projectId == context.projectId && m_context.nodeId == context.nodeId
          && m_context.nodeKind == context.nodeKind
          && m_nameBaselineProjectId == context.projectId
          && m_nameBaselineNodeId == context.nodeId && m_nameBaselineKind == context.nodeKind
          && m_nameBaseline == *authoritativeName;

    m_context = context;
    m_updating = true;

    reset(
        controllerSlave
            ? Tr::tr("Live EtherCAT identity and scan state for %1").arg(context.displayName)
            : Tr::tr("Offline properties for %1").arg(context.displayName),
        preserveName ? previousName : nullptr);
    if (context.nodeKind == Core::WorkbenchNodeKind::DeviceRepository) {
        m_summary->hide();
        m_repositoryPage->refresh();
        m_repositoryPage->show();
        m_tree->hide();
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Device) {
        m_summary->hide();
        m_esiDevicePage->setDevice(device, context.displayName);
        m_esiDevicePage->show();
        m_tree->hide();
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Project) {
        m_summary->hide();
        const QString unavailable = Tr::tr("Unavailable");
        const bool validProject = project && project->valid;
        if (!preserveName)
            m_projectName->setText(displayedName);
        m_projectName->setReadOnly(!editableName);
        m_projectId->setText(validProject ? project->id.toString() : unavailable);
        m_projectType->setText(Tr::tr("Offline EtherCAT Engineering Project"));
        if (validProject) {
            m_projectFormatVersion->setText(QString::number(project->formatVersion));
            m_projectCreatedBy->setText(
                project->createdBy.isEmpty() ? Tr::tr("Not recorded") : project->createdBy);
            m_projectMigration->setText(
                project->migrated ? Tr::tr("Migrated from an older format")
                                  : Tr::tr("Current format"));
            m_projectModified->setText(project->modified ? Tr::tr("Yes") : Tr::tr("No"));
            m_projectTarget->setText(firstNodeName(
                *project, Data::ProjectNodeKind::Target, Tr::tr("Not configured")));
            m_projectMaster->setText(firstNodeName(
                *project, Data::ProjectNodeKind::Master, Tr::tr("Not configured")));
            m_projectTimingMode->setText(
                masterTimingModeName(project->masterConfiguration.timingMode));
            m_projectCycle->setText(
                project->masterConfiguration.cyclePeriodNs
                    ? QString::number(project->masterConfiguration.cyclePeriodNs)
                    : Tr::tr("Not configured"));
            m_projectSlaveCount->setText(QString::number(project->slaves.size()));
        } else {
            m_projectFormatVersion->setText(unavailable);
            m_projectCreatedBy->setText(unavailable);
            m_projectMigration->setText(unavailable);
            m_projectModified->setText(unavailable);
            m_projectTarget->setText(unavailable);
            m_projectMaster->setText(unavailable);
            m_projectTimingMode->setText(unavailable);
            m_projectCycle->setText(unavailable);
            m_projectSlaveCount->setText(unavailable);
        }
        m_projectValidity->setText(project ? projectValidityText(*project) : unavailable);
        m_projectContent->show();
        m_projectForm->show();
        m_projectSummaryForm->show();
        m_tree->hide();
    } else if (configuredSlave) {
        const QString typeName = !device ? Tr::tr("Unknown ESI device")
                                 : !device->summary.typeName.isEmpty() ? device->summary.typeName
                                                                       : device->summary.name;
        if (!preserveName)
            m_name->setText(displayedName);
        m_name->setReadOnly(!editableName);
        m_id->setText(QString::number(offlineSlave->position + 1));
        m_objectId->setText(offlineSlave->id.toString());
        m_type->setText(typeName);
        m_identityForm->show();
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Target) {
        m_summary->hide();
        if (!preserveName)
            m_targetName->setText(displayedName);
        m_targetName->setReadOnly(!editableName);
        m_targetIdentity->setText(
            Tr::tr("Offline / Mock target\nObject Id: %1").arg(context.nodeId.toString()));
        m_targetEngineering->setText(::Core::ICore::versionString());
        m_targetRuntime->setText(Tr::tr("Not assigned (offline)"));
        m_targetLocalRuntime->setText(Tr::tr("Not available (phase 1)"));
        if (project) {
            const QString projectVersion = Tr::tr("Format %1").arg(project->formatVersion);
            m_targetProjectVersion->setText(
                project->createdBy.isEmpty()
                    ? projectVersion
                    : Tr::tr("%1 · %2").arg(projectVersion, project->createdBy));
        } else {
            m_targetProjectVersion->setText(Tr::tr("Unavailable"));
        }
        m_targetContent->show();
        m_tree->hide();
    } else if (context.nodeKind == Core::WorkbenchNodeKind::Master) {
        m_summary->setText(
            Tr::tr("Offline EtherCAT master properties for %1").arg(context.displayName));
        if (!preserveName)
            m_masterName->setText(displayedName);
        const int id
            = project
                  ? structuralNodeOrdinal(*project, Data::ProjectNodeKind::Master, context.nodeId)
                  : -1;
        m_masterId->setText(id > 0 ? QString::number(id) : Tr::tr("Unavailable"));
        m_masterObjectId->setText(context.nodeId.toString());
        m_masterType->setText(Tr::tr("EtherCAT Master"));
        m_masterName->setReadOnly(!editableName);
        m_masterContent->show();
        m_masterForm->show();
        m_masterSummaryForm->show();
        m_tree->hide();
        refreshMasterSummary();
    } else {
        addRow({Tr::tr("Name"), context.displayName});
        addRow({Tr::tr("Node ID"), context.nodeId.toString()});
    }

    if (controllerSlave) {
        addRow({Tr::tr("Position"), QString::number(controllerSlave->position)});
        addRow(
            {Tr::tr("Station address"), hexValue(controllerSlave->stationAddress, 4)});
        addRow(
            {Tr::tr("AL state"), controllerAlStateName(controllerSlave->alState)});
        addRow({Tr::tr("Flags"), hexValue(controllerSlave->flags, 8)});
        addRow({Tr::tr("Vendor ID"), hexValue(controllerSlave->vendorId, 8)});
        addRow({Tr::tr("Product Code"), hexValue(controllerSlave->productCode, 8)});
        addRow({Tr::tr("Revision"), hexValue(controllerSlave->revision, 8)});
        addRow({Tr::tr("Serial Number"), hexValue(controllerSlave->serial, 8)});
        addRow(
            {Tr::tr("ESI match"),
             device ? device->summary.name : Tr::tr("No matching ESI device")});
    } else if (offlineSlave) {
        if (!configuredSlave)
            addRow({Tr::tr("Owner slave"), offlineSlave->name});
        addRow({Tr::tr("Position"), QString::number(offlineSlave->position)});
        addRow(
            {Tr::tr("Station address"),
             offlineSlave->stationAddress ? hexValue(offlineSlave->stationAddress, 4)
                                          : Tr::tr("Unbound — scan required")});
        addRow({Tr::tr("Vendor ID"), hexValue(offlineSlave->identity.vendorId, 8)});
        addRow({Tr::tr("Product Code"), hexValue(offlineSlave->identity.productCode, 8)});
        addRow({Tr::tr("Revision"), hexValue(offlineSlave->identity.revisionNumber, 8)});
        addRow({Tr::tr("Serial Number"), QString::number(offlineSlave->serialNumber)});
        addRow({Tr::tr("Alias"), QString::number(offlineSlave->alias)});
        addRow(
            {Tr::tr("ESI match"), device ? device->summary.name : Tr::tr("No matching ESI device")});
    } else if (device && context.nodeKind != Core::WorkbenchNodeKind::Device) {
        addRow({Tr::tr("Vendor ID"), hexValue(device->summary.identity.vendorId, 8)});
        addRow({Tr::tr("Product Code"), hexValue(device->summary.identity.productCode, 8)});
        addRow({Tr::tr("Revision"), hexValue(device->summary.identity.revisionNumber, 8)});
    }
    if (device && context.nodeKind != Core::WorkbenchNodeKind::Device) {
        if (!configuredSlave)
            addRow({Tr::tr("Type"), device->summary.typeName});
        addRow({Tr::tr("Group"), device->summary.group});
        addRow(
            {Tr::tr("Support"),
             device->summary.supported ? Tr::tr("Supported") : Tr::tr("Limited")});
        addRow({Tr::tr("Source"), device->sourcePath});
        addRow({Tr::tr("Imported"), device->importedAt.toLocalTime().toString(Qt::ISODate)});
    } else if (
        project && context.nodeKind != Core::WorkbenchNodeKind::Project
        && context.nodeKind != Core::WorkbenchNodeKind::Master) {
        addRow({Tr::tr("Format version"), QString::number(project->formatVersion)});
        addRow({Tr::tr("Created by"), project->createdBy});
        addRow({Tr::tr("Modified"), project->modified ? Tr::tr("Yes") : Tr::tr("No")});
        if (context.nodeKind == Core::WorkbenchNodeKind::Target)
            addRow({Tr::tr("Target type"), Tr::tr("Offline / Mock")});
    }
    if (editableName) {
        m_nameBaselineProjectId = context.projectId;
        m_nameBaselineNodeId = context.nodeId;
        m_nameBaselineKind = context.nodeKind;
        m_nameBaseline = *authoritativeName;
    } else {
        m_nameBaselineProjectId = {};
        m_nameBaselineNodeId = {};
        m_nameBaselineKind = Core::WorkbenchNodeKind::None;
        m_nameBaseline.clear();
    }
    m_updating = false;
}

QLineEdit *GeneralPage::nameEditor(Core::WorkbenchNodeKind kind) const
{
    switch (kind) {
    case Core::WorkbenchNodeKind::Project:
        return m_projectName;
    case Core::WorkbenchNodeKind::Target:
        return m_targetName;
    case Core::WorkbenchNodeKind::Master:
        return m_masterName;
    case Core::WorkbenchNodeKind::ConfiguredSlave:
        return m_name;
    default:
        return nullptr;
    }
}

void GeneralPage::reset(const QString &summary, QLineEdit *preservedName)
{
    const bool preserveProjectName = preservedName == m_projectName;
    const bool preserveConfiguredSlaveName = preservedName == m_name;
    const bool preserveTargetName = preservedName == m_targetName;
    const bool preserveMasterName = preservedName == m_masterName;
    m_summary->setText(summary);
    m_summary->show();
    m_repositoryPage->hide();
    m_esiDevicePage->setDevice(std::nullopt, {});
    m_esiDevicePage->hide();
    if (!preserveProjectName) {
        m_projectContent->hide();
        m_projectForm->hide();
        m_projectSummaryForm->hide();
        m_projectName->clear();
        m_projectName->setReadOnly(true);
    }
    m_projectId->clear();
    m_projectType->clear();
    m_projectFormatVersion->clear();
    m_projectCreatedBy->clear();
    m_projectValidity->clear();
    m_projectMigration->clear();
    m_projectModified->clear();
    m_projectTarget->clear();
    m_projectMaster->clear();
    m_projectTimingMode->clear();
    m_projectCycle->clear();
    m_projectSlaveCount->clear();
    if (!preserveConfiguredSlaveName)
        m_identityForm->hide();
    if (!preserveTargetName)
        m_targetContent->hide();
    if (!preserveMasterName) {
        m_masterContent->hide();
        m_masterForm->hide();
        m_masterSummaryForm->hide();
    }
    m_tree->show();
    if (!preserveConfiguredSlaveName) {
        m_name->clear();
        m_name->setReadOnly(true);
    }
    m_id->clear();
    m_objectId->clear();
    m_type->clear();
    if (!preserveTargetName) {
        m_targetName->clear();
        m_targetName->setReadOnly(true);
    }
    m_targetIdentity->clear();
    m_targetEngineering->clear();
    m_targetRuntime->clear();
    m_targetLocalRuntime->clear();
    m_targetProjectVersion->clear();
    m_targetPinVersion->setChecked(false);
    if (!preserveMasterName) {
        m_masterName->clear();
        m_masterName->setReadOnly(true);
    }
    m_masterId->clear();
    m_masterObjectId->clear();
    m_masterType->clear();
    m_masterComment->clear();
    m_masterDisabled->setChecked(false);
    m_masterCreateSymbols->setChecked(false);
    m_masterTimingMode->setCurrentIndex(0);
    m_masterTimingMode->setEnabled(false);
    m_masterCycle->clear();
    m_masterCycle->setEnabled(false);
    m_masterApply->setEnabled(false);
    m_masterSlaveCount->clear();
    m_masterStatus->clear();
    m_masterScanProvider->clear();
    m_masterScanProvider->setEnabled(false);
    m_tree->clear();
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({Tr::tr("Property"), Tr::tr("Value")});
}

void GeneralPage::addRow(const QStringList &values)
{
    auto item = new QTreeWidgetItem(values);
    const QString property = values.value(0);
    const QString value = values.value(1);
    const QString describedValue = value.isEmpty() ? Tr::tr("Empty") : value;
    for (int column = 0; column < m_tree->columnCount(); ++column) {
        const QString display = item->text(column);
        const QString header = m_tree->headerItem()->text(column);
        const QString description
            = Tr::tr(
                  "%1 column. Property: %2. Complete value: %3. Read-only Workbench data. "
                  "Viewing this value does not perform a controller operation.")
                  .arg(header, property, describedValue);
        item->setData(column, Qt::AccessibleTextRole, display);
        item->setData(column, Qt::AccessibleDescriptionRole, description);
        item->setData(column, Qt::ToolTipRole, description);
    }
    m_tree->addTopLevelItem(item);
}

void GeneralPage::showNameFeedback(const QString &message)
{
    m_nameFeedback->setType(Utils::InfoLabel::Error);
    m_nameFeedback->setText(message);
    m_nameFeedback->setAccessibleDescription(message);
    m_nameFeedback->setAdditionalToolTip(message);
    m_nameFeedback->setToolTip(message);
    m_nameFeedback->show();
#if QT_CONFIG(accessibility)
    QAccessibleAnnouncementEvent announcement(m_nameFeedback, message);
    announcement.setPoliteness(QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&announcement);
#endif
}

void GeneralPage::clearNameFeedback()
{
    m_nameFeedback->clear();
    m_nameFeedback->setAccessibleDescription({});
    m_nameFeedback->setAdditionalToolTip({});
    m_nameFeedback->setToolTip({});
    m_nameFeedback->hide();
}

void GeneralPage::commitProjectName()
{
    if (m_updating || !m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Project)
        return;
    const QScopedValueRollback forceReload(m_forceAuthoritativeNameReload, true);
    m_projectName->setModified(false);
    const Utils::Result<> result
        = m_controller->renameProject(m_context.projectId, m_projectName->text());
    const QString error = result
                              ? QString()
                              : Tr::tr("Cannot rename the EtherCAT project: %1")
                                    .arg(result.error());
    if (!error.isEmpty())
        ::Core::MessageManager::writeFlashing(error);
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
    if (!error.isEmpty())
        showNameFeedback(error);
}

void GeneralPage::commitName()
{
    if (m_updating || !m_controller
        || m_context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave) {
        return;
    }
    const QScopedValueRollback forceReload(m_forceAuthoritativeNameReload, true);
    m_name->setModified(false);
    const Utils::Result<> result
        = m_controller->renameOfflineSlave(m_context.projectId, m_context.nodeId, m_name->text());
    const QString error = result
                              ? QString()
                              : Tr::tr("Cannot rename the offline slave: %1")
                                    .arg(result.error());
    if (!error.isEmpty())
        ::Core::MessageManager::writeFlashing(error);
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
    if (!error.isEmpty())
        showNameFeedback(error);
}

void GeneralPage::commitTargetName()
{
    if (m_updating || !m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Target)
        return;
    const QScopedValueRollback forceReload(m_forceAuthoritativeNameReload, true);
    m_targetName->setModified(false);
    const Utils::Result<> result
        = m_controller
              ->renameStructuralNode(m_context.projectId, m_context.nodeId, m_targetName->text());
    const QString error = result
                              ? QString()
                              : Tr::tr("Cannot rename the offline target: %1")
                                    .arg(result.error());
    if (!error.isEmpty())
        ::Core::MessageManager::writeFlashing(error);
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
    if (!error.isEmpty())
        showNameFeedback(error);
}

void GeneralPage::commitMasterName()
{
    if (m_updating || !m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;
    const QScopedValueRollback forceReload(m_forceAuthoritativeNameReload, true);
    m_masterName->setModified(false);
    const Utils::Result<> result
        = m_controller
              ->renameStructuralNode(m_context.projectId, m_context.nodeId, m_masterName->text());
    const QString error = result
                              ? QString()
                              : Tr::tr("Cannot rename the EtherCAT master: %1")
                                    .arg(result.error());
    if (!error.isEmpty())
        ::Core::MessageManager::writeFlashing(error);
    const Core::PropertyPageContext current = m_controller->treeModel()->contextForNodeId(
        m_context.nodeId);
    setContext(current.nodeKind == Core::WorkbenchNodeKind::None ? m_context : current);
    if (!error.isEmpty())
        showNameFeedback(error);
}

void GeneralPage::commitMasterConfiguration()
{
    if (m_updating || !m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;

    const auto timingMode
        = static_cast<Data::MasterTimingMode>(m_masterTimingMode->currentData().toInt());
    const QString cycleText = m_masterCycle->text().trimmed();
    bool validCycle = false;
    const qulonglong rawCycle = cycleText.isEmpty() ? 0 : cycleText.toULongLong(&validCycle);
    if (cycleText.isEmpty())
        validCycle = timingMode == Data::MasterTimingMode::Unassigned;
    if (!validCycle || rawCycle > std::numeric_limits<quint32>::max()) {
        m_controller->writeControllerOutput(
            Tr::tr("Enter a valid EtherCAT master cycle period from 1 through 4294967295 ns."),
            ControllerOutputLevel::Error);
        refreshMasterSummary();
        return;
    }

    if (const QString reason = m_controller->masterTimingModeUnavailableReason(
            m_context.projectId, m_context.nodeId, timingMode);
        !reason.isEmpty()) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot select FreeRun: %1").arg(reason), ControllerOutputLevel::Warning);
        refreshMasterSummary();
        return;
    }

    const Utils::Result<> result = m_controller->setMasterConfiguration(
        m_context.projectId,
        m_context.nodeId,
        {timingMode, quint32(rawCycle)});
    if (!result) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot update the EtherCAT master cycle configuration: %1")
                .arg(result.error()),
            ControllerOutputLevel::Error);
    } else {
        m_controller->writeControllerOutput(
            Tr::tr("EtherCAT master cycle configuration updated."));
    }
    refreshMasterSummary();
}

void GeneralPage::commitMasterScanProvider()
{
    if (m_updating || !m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;

    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Utils::Id providerId = Utils::Id::fromSetting(m_masterScanProvider->currentData());
    const Utils::Result<> result = m_controller->selectScanProvider(scope, providerId);
    if (!result) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot update the Mock topology provider: %1").arg(result.error()),
            ControllerOutputLevel::Error);
    } else {
        m_controller->writeControllerOutput(
            providerId.isValid() ? Tr::tr("Mock topology provider selected.")
                                 : Tr::tr("Mock topology provider selection cleared."));
    }
    refreshMasterSummary();
}

void GeneralPage::refreshMasterSummary()
{
    if (!m_controller || m_context.nodeKind != Core::WorkbenchNodeKind::Master)
        return;
    const std::optional<Data::ProjectSnapshot> project
        = m_controller->projectService()
              ? m_controller->projectService()->project(m_context.projectId)
              : std::nullopt;
    const bool editable = project && project->valid;
    const Data::MasterConfiguration configuration
        = editable ? project->masterConfiguration : Data::MasterConfiguration();
    const int modeIndex = m_masterTimingMode->findData(int(configuration.timingMode));
    m_masterTimingMode->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);
    m_masterCycle->setText(
        configuration.cyclePeriodNs ? QString::number(configuration.cyclePeriodNs) : QString());
    m_masterTimingMode->setEnabled(editable);
    m_masterCycle->setEnabled(editable);
    m_masterApply->setEnabled(editable);
    m_masterSlaveCount->setText(QString::number(
        m_controller->treeModel()->offlineSlavesForMaster(m_context.nodeId).size()));
    const QModelIndex masterIndex = m_controller->treeModel()->indexForNodeId(m_context.nodeId);
    const QString status = masterIndex.data(WorkbenchTreeModel::StatusRole).toString();
    m_masterStatus->setText(status.isEmpty() ? Tr::tr("Offline configuration") : status);

    const QSignalBlocker scanProviderBlocker(m_masterScanProvider);
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const std::optional<Core::ScanProviderSelection> selected
        = m_controller->scanProviderSelection(scope);
    m_masterScanProvider->clear();
    m_masterScanProvider->addItem(Tr::tr("No Mock topology provider"), QVariant());
    int selectedIndex = 0;
    for (Core::ScanProvider *provider : m_controller->scanProviders()) {
        QString displayName = provider->displayName().trimmed();
        if (displayName.isEmpty())
            displayName = Tr::tr("Unnamed Scan Provider");
        if (!provider->isAvailable())
            displayName = Tr::tr("%1 (unavailable)").arg(displayName);
        m_masterScanProvider->addItem(displayName, provider->id().toSetting());
        if (selected && selected->providerId == provider->id())
            selectedIndex = m_masterScanProvider->count() - 1;
    }
    if (selected && selectedIndex == 0) {
        m_masterScanProvider->addItem(
            Tr::tr("%1 (unavailable)").arg(selected->providerId.toString()),
            selected->providerId.toSetting());
        selectedIndex = m_masterScanProvider->count() - 1;
    }
    m_masterScanProvider->setCurrentIndex(selectedIndex);
    m_masterScanProvider->setEnabled(editable);
}

} // namespace EtherCAT::Workbench::Internal
