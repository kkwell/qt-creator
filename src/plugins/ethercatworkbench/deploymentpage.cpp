// Copyright (C) 2026 Kvell

#include "deploymentpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <utils/fileutils.h>
#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QProgressBar>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

static constexpr qint64 maximumPackageBytes = 16 * 1024 * 1024;

static bool packageDeploymentIsActive(Data::ControllerPackageDeploymentState state)
{
    using State = Data::ControllerPackageDeploymentState;
    return state == State::Uploading || state == State::Committing || state == State::Validating
           || state == State::Activating || state == State::RollingBack
           || state == State::Canceling;
}

static bool packageDeploymentIsTerminal(Data::ControllerPackageDeploymentState state)
{
    using State = Data::ControllerPackageDeploymentState;
    return state == State::Succeeded || state == State::Canceled || state == State::Failed;
}

static QString packageDeploymentStateText(Data::ControllerPackageDeploymentState state)
{
    using State = Data::ControllerPackageDeploymentState;
    switch (state) {
    case State::Idle:
        return Tr::tr("Idle");
    case State::Uploading:
        return Tr::tr("Uploading");
    case State::Committing:
        return Tr::tr("Committing");
    case State::Validating:
        return Tr::tr("Validating");
    case State::Activating:
        return Tr::tr("Activating");
    case State::RollingBack:
        return Tr::tr("Rolling back");
    case State::Canceling:
        return Tr::tr("Canceling");
    case State::Succeeded:
        return Tr::tr("Succeeded");
    case State::Canceled:
        return Tr::tr("Canceled");
    case State::Failed:
        return Tr::tr("Failed");
    case State::OutcomeUnknown:
        return Tr::tr("Outcome unknown");
    }
    return Tr::tr("Unknown");
}

static QString controllerOperationText(Data::ControllerOperation operation)
{
    using Operation = Data::ControllerOperation;
    switch (operation) {
    case Operation::UploadPackage:
        return Tr::tr("Upload package");
    case Operation::AbortPackageUpload:
        return Tr::tr("Abort package upload");
    case Operation::ValidatePackage:
        return Tr::tr("Validate package");
    case Operation::ActivatePackage:
        return Tr::tr("Activate package");
    case Operation::RollbackPackage:
        return Tr::tr("Rollback package");
    case Operation::QueryPackageState:
        return Tr::tr("Query package state");
    default:
        return Tr::tr("Controller operation");
    }
}

static QString slotText(Data::ControllerSlot slot)
{
    switch (slot) {
    case Data::ControllerSlot::None:
        return Tr::tr("None");
    case Data::ControllerSlot::A:
        return Tr::tr("Slot A");
    case Data::ControllerSlot::B:
        return Tr::tr("Slot B");
    }
    return Tr::tr("Unknown");
}

static QString selectorText(Data::ControllerSlot slot, quint64 generation, quint64 configurationId)
{
    if (slot == Data::ControllerSlot::None || !generation || !configurationId)
        return Tr::tr("Not available");
    return Tr::tr("%1 / generation %2 / configuration %3")
        .arg(slotText(slot))
        .arg(generation)
        .arg(configurationId);
}

static QString optionalNumber(const std::optional<qint32> &value)
{
    return value ? QString::number(*value) : Tr::tr("Not available");
}

static void addStatusRow(QTreeWidget *tree, const QString &property, const QString &value)
{
    auto item = new QTreeWidgetItem({property, value});
    item->setToolTip(0, property);
    item->setToolTip(1, value);
    tree->addTopLevelItem(item);
}

DeploymentPage::DeploymentPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_guidance(new QLabel(this))
    , m_artifactPath(new QLineEdit(this))
    , m_browse(new QToolButton(this))
    , m_artifactSummary(new QLabel(this))
    , m_configurationId(new QLineEdit(this))
    , m_operationId(new QLineEdit(this))
    , m_newOperation(new QToolButton(this))
    , m_activate(new QCheckBox(this))
    , m_rollback(new QCheckBox(this))
    , m_deploy(new QToolButton(this))
    , m_cancel(new QToolButton(this))
    , m_trustedActivationStatus(new QLabel(this))
    , m_trustedActivate(new QToolButton(this))
    , m_deploymentSummary(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_status(new QTreeWidget(this))
    , m_audit(new QTreeWidget(this))
{
    setObjectName("EtherCATWorkbenchDeploymentPage");
    setAccessibleName(Tr::tr("Controller package staging"));
    setAccessibleDescription(
        Tr::tr(
            "Stages and validates one already-built ECPKG through the selected controller "
            "adapter's transactional package service."));

    m_guidance->setObjectName("EtherCATDeploymentGuidance");
    m_guidance->setWordWrap(true);
    m_guidance->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_guidance->setText(
        Tr::tr(
            "Select an immutable, already-built ECPKG. This page only uploads and validates it. "
            "Activation and node control binding use the separate trusted project workflow "
            "below."));

    m_artifactPath->setObjectName("EtherCATDeploymentArtifactPath");
    m_artifactPath->setAccessibleName(Tr::tr("ECPKG artifact path"));
    m_artifactPath->setClearButtonEnabled(true);
    m_artifactPath->setPlaceholderText(Tr::tr("Select an .ecpkg file"));
    m_browse->setObjectName("EtherCATDeploymentBrowse");
    m_browse->setText(Tr::tr("Browse..."));
    m_browse->setAccessibleName(Tr::tr("Browse for ECPKG"));
    m_browse->setIcon(Utils::Icons::OPENFILE.icon());
    m_browse->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_artifactSummary->setObjectName("EtherCATDeploymentArtifactSummary");
    m_artifactSummary->setWordWrap(true);
    m_artifactSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto artifactWidget = new QWidget(this);
    auto artifactLayout = new QHBoxLayout(artifactWidget);
    artifactLayout->setContentsMargins(QMargins());
    artifactLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHS);
    artifactLayout->addWidget(m_artifactPath, 1);
    artifactLayout->addWidget(m_browse);

    m_configurationId->setObjectName("EtherCATDeploymentConfigurationId");
    m_configurationId->setAccessibleName(Tr::tr("Package configuration ID"));
    m_configurationId->setPlaceholderText(Tr::tr("For example, 813"));
    m_configurationId->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[0-9]{0,20}"), m_configurationId));

    m_operationId->setObjectName("EtherCATDeploymentOperationId");
    m_operationId->setAccessibleName(Tr::tr("Package deployment OperationId"));
    m_operationId->setReadOnly(true);
    m_newOperation->setObjectName("EtherCATDeploymentNewOperation");
    m_newOperation->setText(Tr::tr("New Operation ID"));
    m_newOperation->setAccessibleName(Tr::tr("Generate a new package deployment OperationId"));
    m_newOperation->setToolTip(
        Tr::tr(
            "Generate a new client audit and idempotency identifier. Do not retry an unknown "
            "outcome with a new identifier."));
    m_newOperation->setIcon(Utils::Icons::RELOAD.icon());
    m_newOperation->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto operationWidget = new QWidget(this);
    auto operationLayout = new QHBoxLayout(operationWidget);
    operationLayout->setContentsMargins(QMargins());
    operationLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHS);
    operationLayout->addWidget(m_operationId, 1);
    operationLayout->addWidget(m_newOperation);

    m_activate->setObjectName("EtherCATDeploymentActivate");
    m_activate->setChecked(false);
    m_activate->setEnabled(false);
    m_activate->hide();
    m_rollback->setObjectName("EtherCATDeploymentRollback");
    m_rollback->setChecked(false);
    m_rollback->setEnabled(false);
    m_rollback->hide();

    auto form = new QFormLayout;
    form->setContentsMargins(QMargins());
    form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    form->addRow(Tr::tr("ECPKG artifact:"), artifactWidget);
    form->addRow(QString(), m_artifactSummary);
    form->addRow(Tr::tr("Configuration ID:"), m_configurationId);
    form->addRow(Tr::tr("Operation ID:"), operationWidget);

    m_deploy->setObjectName("EtherCATDeploymentStart");
    m_deploy->setText(Tr::tr("Stage Package"));
    m_deploy->setAccessibleName(Tr::tr("Stage and validate controller package"));
    m_deploy->setIcon(Utils::Icons::EXPORTFILE_TOOLBAR.icon());
    m_deploy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_cancel->setObjectName("EtherCATDeploymentCancel");
    m_cancel->setText(Tr::tr("Cancel Upload"));
    m_cancel->setAccessibleName(Tr::tr("Cancel controller package upload"));
    m_cancel->setIcon(Utils::Icons::STOP_SMALL.icon());
    m_cancel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto commandLayout = new QHBoxLayout;
    commandLayout->setContentsMargins(QMargins());
    commandLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    commandLayout->addWidget(m_deploy);
    commandLayout->addWidget(m_cancel);
    commandLayout->addStretch();

    m_trustedActivationStatus->setObjectName(
        "EtherCATTrustedActivationStatus");
    m_trustedActivationStatus->setWordWrap(true);
    m_trustedActivationStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    m_trustedActivate->setObjectName("EtherCATTrustedActivationStart");
    m_trustedActivate->setText(Tr::tr("Activate Verified Project"));
    m_trustedActivate->setAccessibleName(
        Tr::tr("Activate the verified project package"));
    m_trustedActivate->setIcon(Utils::Icons::RUN_SMALL_TOOLBAR.icon());
    m_trustedActivate->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto trustedActivationGroup
        = new QGroupBox(Tr::tr("Trusted project activation"), this);
    auto trustedActivationLayout = new QVBoxLayout(trustedActivationGroup);
    trustedActivationLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS,
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS);
    trustedActivationLayout->setSpacing(
        Utils::StyleHelper::SpacingTokens::GapVS);
    trustedActivationLayout->addWidget(m_trustedActivationStatus);
    auto trustedActivationCommands = new QHBoxLayout;
    trustedActivationCommands->setContentsMargins(QMargins());
    trustedActivationCommands->setSpacing(
        Utils::StyleHelper::SpacingTokens::GapHM);
    trustedActivationCommands->addWidget(m_trustedActivate);
    trustedActivationCommands->addStretch();
    trustedActivationLayout->addLayout(trustedActivationCommands);

    m_deploymentSummary->setObjectName("EtherCATDeploymentSummary");
    m_deploymentSummary->setWordWrap(true);
    m_deploymentSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_progress->setObjectName("EtherCATDeploymentProgress");
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(Tr::tr("No deployment"));

    m_status->setObjectName("EtherCATDeploymentStatus");
    m_status->setAccessibleName(Tr::tr("Controller package status"));
    m_status->setColumnCount(2);
    m_status->setHeaderLabels({Tr::tr("Property"), Tr::tr("Value")});
    m_status->setRootIsDecorated(false);
    m_status->setAlternatingRowColors(true);
    m_status->setUniformRowHeights(true);
    m_status->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_status->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    m_audit->setObjectName("EtherCATDeploymentAudit");
    m_audit->setAccessibleName(Tr::tr("Package deployment audit events"));
    m_audit->setColumnCount(6);
    m_audit->setHeaderLabels(
        {Tr::tr("Sequence"),
         Tr::tr("Time"),
         Tr::tr("Operation"),
         Tr::tr("Request"),
         Tr::tr("Status / Result"),
         Tr::tr("Detail")});
    m_audit->setRootIsDecorated(false);
    m_audit->setAlternatingRowColors(true);
    m_audit->setUniformRowHeights(true);
    m_audit->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_audit->header()->setStretchLastSection(true);

    auto stateGroup = new QGroupBox(Tr::tr("Deployment state"), this);
    auto stateLayout = new QVBoxLayout(stateGroup);
    stateLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS,
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS);
    stateLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    stateLayout->addWidget(m_deploymentSummary);
    stateLayout->addWidget(m_progress);
    stateLayout->addWidget(m_status, 1);

    auto auditGroup = new QGroupBox(Tr::tr("Deployment audit"), this);
    auto auditLayout = new QVBoxLayout(auditGroup);
    auditLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS,
        Utils::StyleHelper::SpacingTokens::PaddingHS,
        Utils::StyleHelper::SpacingTokens::PaddingVS);
    auditLayout->addWidget(m_audit);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_guidance);
    layout->addLayout(form);
    layout->addLayout(commandLayout);
    layout->addWidget(trustedActivationGroup);
    layout->addWidget(stateGroup, 1);
    layout->addWidget(auditGroup, 1);

    connect(m_browse, &QToolButton::clicked, this, &DeploymentPage::browseForArtifact);
    connect(m_artifactPath, &QLineEdit::textEdited, this, [this] {
        clearArtifact();
        prepareNewOperation();
        refresh();
    });
    connect(m_configurationId, &QLineEdit::textEdited, this, [this] {
        prepareNewOperation();
        refresh();
    });
    connect(m_newOperation, &QToolButton::clicked, this, [this] {
        prepareNewOperation();
        refresh();
    });
    connect(m_deploy, &QToolButton::clicked, this, &DeploymentPage::deploy);
    connect(
        m_trustedActivate,
        &QToolButton::clicked,
        this,
        &DeploymentPage::activateTrustedPackage);
    connect(m_cancel, &QToolButton::clicked, this, &DeploymentPage::cancelDeployment);
    if (m_controller) {
        connect(
            m_controller,
            &WorkbenchController::controllerConnectionChanged,
            this,
            &DeploymentPage::refresh);
        connect(
            m_controller,
            &WorkbenchController::trustedRuntimePackageActivationChanged,
            this,
            &DeploymentPage::refresh);
    }

    prepareNewOperation();
    refresh();
}

void DeploymentPage::setContext(const Core::PropertyPageContext &context)
{
    const bool scopeChanged = m_context.projectId != context.projectId
                              || m_context.nodeId != context.nodeId
                              || m_context.nodeKind != context.nodeKind;
    m_context = context;
    if (scopeChanged)
        prepareNewOperation();
    refresh();
}

void DeploymentPage::browseForArtifact()
{
    const Utils::FilePath current = Utils::FilePath::fromUserInput(m_artifactPath->text().trimmed());
    const Utils::FilePath selected = Utils::FileUtils::getOpenFilePath(
        Tr::tr("Select Controller Package"),
        current.isEmpty() ? Utils::FilePath() : current.parentDir(),
        Tr::tr("EtherCAT controller packages (*.ecpkg);;All files (*)"));
    if (selected.isEmpty())
        return;
    m_artifactPath->setText(selected.toUserOutput());
    clearArtifact();
    prepareNewOperation();
    loadArtifact(true);
    refresh();
}

bool DeploymentPage::loadArtifact(bool reportError)
{
    const Utils::FilePath path = Utils::FilePath::fromUserInput(m_artifactPath->text().trimmed());
    QString error;
    if (path.isEmpty()) {
        error = Tr::tr("Select an ECPKG artifact.");
    } else if (path.suffix().compare("ecpkg", Qt::CaseInsensitive) != 0) {
        error = Tr::tr("The selected artifact must use the .ecpkg extension.");
    } else if (!path.isFile()) {
        error = Tr::tr("The selected ECPKG artifact is not a readable file.");
    } else {
        const Utils::Result<QByteArray> contents = path.fileContents();
        if (!contents) {
            error = contents.error();
        } else if (contents->isEmpty()) {
            error = Tr::tr("The selected ECPKG artifact is empty.");
        } else if (contents->size() > maximumPackageBytes) {
            error = Tr::tr("The selected ECPKG exceeds the protocol maximum of 16 MiB.");
        } else {
            m_loadedArtifactPath = path;
            m_artifact = *contents;
            m_artifactSha256 = QCryptographicHash::hash(m_artifact, QCryptographicHash::Sha256);
            m_artifactSummary->setText(
                Tr::tr("%1 bytes · SHA-256 %2")
                    .arg(QLocale().toString(m_artifact.size()))
                    .arg(QString::fromLatin1(m_artifactSha256.toHex())));
            return true;
        }
    }

    clearArtifact();
    m_artifactSummary->setText(error);
    if (reportError && m_controller) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot load the controller package: %1").arg(error),
            ControllerOutputLevel::Error);
    }
    return false;
}

void DeploymentPage::clearArtifact()
{
    m_loadedArtifactPath = {};
    m_artifact.clear();
    m_artifactSha256.clear();
    m_artifactSummary->clear();
}

void DeploymentPage::prepareNewOperation()
{
    if (m_updating)
        return;
    m_operationId->setText(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void DeploymentPage::deploy()
{
    if (!m_controller)
        return;
    if (!loadArtifact(true)) {
        refresh();
        return;
    }
    const std::optional<quint64> configuredId = configurationId();
    if (!configuredId) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot stage the controller package: enter a nonzero configuration ID."),
            ControllerOutputLevel::Error);
        refresh();
        return;
    }

    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Data::ControllerConnectionSnapshot snapshot = m_controller->controllerConnectionSnapshot(
        scope);
    if (packageDeploymentIsTerminal(snapshot.packageDeploymentProgress.state)
        && snapshot.packageDeploymentProgress.operationId == m_operationId->text()) {
        prepareNewOperation();
    }

    Data::ControllerPackageDeploymentRequest request;
    request.operationId = m_operationId->text();
    request.artifact = m_artifact;
    request.configurationId = *configuredId;
    // This legacy page has no trusted compiler/activation coordinator. It must never turn an
    // arbitrary selected artifact into an active runtime or a semantic control binding.
    request.activate = false;
    request.rollbackOnActivationFailure = false;
    const Utils::Result<> result = m_controller->deployControllerPackage(scope, request);
    if (!result) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot stage the controller package: %1").arg(result.error()),
            ControllerOutputLevel::Error);
        refresh();
        return;
    }

    m_controller->writeControllerOutput(
        Tr::tr("Package staging queued [%1]: configuration %2, SHA-256 %3.")
            .arg(
                request.operationId,
                QString::number(request.configurationId),
                QString::fromLatin1(m_artifactSha256.toHex())));
    refresh();
}

void DeploymentPage::activateTrustedPackage()
{
    if (!m_controller)
        return;
    const Data::ControllerConnectionScope scope{
        m_context.projectId, m_context.nodeId};
    const Utils::Result<> result
        = m_controller->startTrustedRuntimePackageActivation(scope);
    if (!result) {
        m_controller->writeControllerOutput(
            Tr::tr("Trusted package activation unavailable: %1")
                .arg(result.error()),
            ControllerOutputLevel::Error);
    }
    refresh();
}

void DeploymentPage::cancelDeployment()
{
    if (!m_controller)
        return;
    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const Data::ControllerConnectionSnapshot snapshot = m_controller->controllerConnectionSnapshot(
        scope);
    const QString operationId = snapshot.packageDeploymentProgress.operationId;
    const Utils::Result<> result
        = m_controller->cancelControllerPackageDeployment(scope, operationId);
    if (!result) {
        m_controller->writeControllerOutput(
            Tr::tr("Cannot cancel the package deployment: %1").arg(result.error()),
            ControllerOutputLevel::Error);
        refresh();
        return;
    }
    m_controller->writeControllerOutput(
        Tr::tr("Package deployment cancellation requested [%1].").arg(operationId),
        ControllerOutputLevel::Warning);
    refresh();
}

void DeploymentPage::refresh()
{
    QScopedValueRollback updating(m_updating, true);
    const bool masterContext = m_context.nodeKind == Core::WorkbenchNodeKind::Master
                               && !m_context.projectId.isNull() && !m_context.nodeId.isNull();
    m_artifactPath->setEnabled(masterContext);
    m_browse->setEnabled(masterContext);
    m_configurationId->setEnabled(masterContext);
    m_activate->setEnabled(false);
    m_rollback->setEnabled(false);
    m_newOperation->setEnabled(masterContext);

    Data::ControllerConnectionSnapshot snapshot;
    Core::ControllerConnectionProvider *provider = nullptr;
    if (masterContext && m_controller) {
        const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
        provider = m_controller->controllerConnectionProvider(scope);
        snapshot = m_controller->controllerConnectionSnapshot(scope);
    }
    updateStatus(snapshot, provider);
    updateAudit(snapshot.packageDeploymentProgress);

    QString unavailable = inputUnavailableReason();
    if (unavailable.isEmpty() && m_controller) {
        const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
        unavailable = m_controller->packageDeploymentUnavailableReason(scope);
    }
    m_deploy->setEnabled(unavailable.isEmpty());
    m_deploy->setToolTip(
        unavailable.isEmpty()
            ? Tr::tr("Upload and validate the exact selected ECPKG without activating it.")
            : unavailable);

    const Data::ControllerConnectionScope scope{m_context.projectId, m_context.nodeId};
    const QString trustedUnavailable
        = masterContext && m_controller
              ? m_controller
                    ->trustedRuntimePackageActivationUnavailableReason(scope)
              : Tr::tr(
                    "Select an EtherCAT Master in an open project before trusted activation.");
    m_trustedActivationStatus->setText(
        masterContext && m_controller
            ? m_controller->trustedRuntimePackageActivationStatus(scope)
            : trustedUnavailable);
    m_trustedActivationStatus->setAccessibleDescription(
        m_trustedActivationStatus->text());
    m_trustedActivate->setEnabled(trustedUnavailable.isEmpty());
    m_trustedActivate->setToolTip(
        trustedUnavailable.isEmpty()
            ? Tr::tr(
                  "Prepare and start only the exact production-verified API-042 project package.")
            : trustedUnavailable);

    const bool canCancel = masterContext && m_controller
                           && m_controller->canCancelControllerPackageDeployment(scope);
    m_cancel->setEnabled(canCancel);
    m_cancel->setToolTip(
        canCancel ? Tr::tr("Request BulkAbort before package validation begins.")
                  : Tr::tr("Package deployment can only be canceled during upload or commit."));
}

void DeploymentPage::updateStatus(
    const Data::ControllerConnectionSnapshot &snapshot, Core::ControllerConnectionProvider *provider)
{
    const Data::ControllerPackageDeploymentProgress &progress = snapshot.packageDeploymentProgress;
    const QString providerName = provider ? provider->displayName() : Tr::tr("Not available");
    m_status->clear();
    addStatusRow(m_status, Tr::tr("Adapter"), providerName);
    addStatusRow(
        m_status,
        Tr::tr("Active package"),
        snapshot.package ? selectorText(
                               snapshot.package->activeSlot,
                               snapshot.package->activeGeneration,
                               snapshot.package->activeConfigurationId)
                         : Tr::tr("Not available"));
    addStatusRow(
        m_status,
        Tr::tr("Staged package"),
        snapshot.package ? selectorText(
                               snapshot.package->stagedSlot,
                               snapshot.package->stagedGeneration,
                               snapshot.package->stagedConfigurationId)
                         : Tr::tr("Not available"));
    addStatusRow(
        m_status,
        Tr::tr("Operation ID"),
        progress.operationId.isEmpty() ? Tr::tr("Not available") : progress.operationId);
    addStatusRow(
        m_status,
        Tr::tr("Artifact SHA-256"),
        progress.artifactSha256.isEmpty() ? Tr::tr("Not available")
                                          : QString::fromLatin1(progress.artifactSha256.toHex()));
    addStatusRow(m_status, Tr::tr("Status"), optionalNumber(progress.status));
    addStatusRow(m_status, Tr::tr("Operation result"), optionalNumber(progress.operationResult));

    const QString stateText = packageDeploymentStateText(progress.state);
    m_deploymentSummary->setText(
        progress.detail.isEmpty() ? stateText : Tr::tr("%1 — %2").arg(stateText, progress.detail));
    m_deploymentSummary->setAccessibleDescription(m_deploymentSummary->text());

    if (progress.totalBytes > 0) {
        const int value
            = qBound(0, int(progress.transferredBytes * 1000 / progress.totalBytes), 1000);
        m_progress->setValue(value);
        m_progress->setFormat(
            Tr::tr("%1 / %2 bytes").arg(progress.transferredBytes).arg(progress.totalBytes));
    } else {
        m_progress->setValue(packageDeploymentIsActive(progress.state) ? 1 : 0);
        m_progress->setFormat(stateText);
    }
}

void DeploymentPage::updateAudit(const Data::ControllerPackageDeploymentProgress &progress)
{
    m_audit->clear();
    for (const Data::ControllerPackageDeploymentAuditEvent &event : progress.audit) {
        const QString occurredAt
            = event.occurredAt.isValid()
                  ? QLocale().toString(event.occurredAt.toLocalTime(), QLocale::ShortFormat)
                  : Tr::tr("Not available");
        const QString requestId = event.requestId ? QString::number(*event.requestId)
                                                  : Tr::tr("Not available");
        const QString status = Tr::tr("%1 / %2")
                                   .arg(optionalNumber(event.status))
                                   .arg(optionalNumber(event.operationResult));
        auto item = new QTreeWidgetItem(
            {QString::number(event.sequence),
             occurredAt,
             controllerOperationText(event.operation),
             requestId,
             status,
             event.detail});
        for (int column = 0; column < m_audit->columnCount(); ++column)
            item->setToolTip(column, item->text(column));
        m_audit->addTopLevelItem(item);
    }
}

QString DeploymentPage::inputUnavailableReason() const
{
    if (m_context.nodeKind != Core::WorkbenchNodeKind::Master || m_context.projectId.isNull()
        || m_context.nodeId.isNull()) {
        return Tr::tr("Select an EtherCAT Master before staging a package.");
    }
    if (m_artifactPath->text().trimmed().isEmpty())
        return Tr::tr("Select an ECPKG artifact.");
    if (!configurationId())
        return Tr::tr("Enter a nonzero package configuration ID.");
    if (m_operationId->text().trimmed().isEmpty())
        return Tr::tr("Generate a package deployment OperationId.");
    return {};
}

std::optional<quint64> DeploymentPage::configurationId() const
{
    bool ok = false;
    const quint64 value = m_configurationId->text().trimmed().toULongLong(&ok, 10);
    return ok && value ? std::optional<quint64>(value) : std::nullopt;
}

} // namespace EtherCAT::Workbench::Internal
