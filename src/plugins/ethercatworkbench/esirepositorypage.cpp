// Copyright (C) 2026 Kvell

#include "esirepositorypage.h"

#include "ethercatworkbenchtr.h"

#include <coreplugin/documentmanager.h>

#include <utils/stylehelper.h>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace EtherCAT::Workbench::Internal {

static Utils::FilePaths localXmlFiles(const QMimeData *mimeData)
{
    Utils::FilePaths files;
    if (!mimeData || !mimeData->hasUrls())
        return files;
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile())
            continue;
        const Utils::FilePath file = Utils::FilePath::fromString(url.toLocalFile());
        if (file.suffix().compare("xml", Qt::CaseInsensitive) == 0)
            files.append(file);
    }
    return files;
}

static QString importResultText(const Data::DeviceImportResult &result)
{
    QStringList lines = {
        Tr::tr("Requested files: %1").arg(result.requestedFiles),
        Tr::tr("Imported devices: %1").arg(result.importedDevices),
        Tr::tr("Updated devices: %1").arg(result.updatedDevices),
        Tr::tr("Duplicate files: %1").arg(result.duplicateFiles),
        Tr::tr("Failed files: %1").arg(result.failedFiles),
        Tr::tr("Canceled: %1").arg(result.canceled ? Tr::tr("Yes") : Tr::tr("No")),
    };
    if (!result.errors.isEmpty()) {
        lines.append(QString());
        lines.append(Tr::tr("Errors:"));
        constexpr int maximumDisplayedErrors = 100;
        const qsizetype displayedErrors
            = std::min<qsizetype>(result.errors.size(), maximumDisplayedErrors);
        for (qsizetype index = 0; index < displayedErrors; ++index) {
            const Data::DeviceImportError &error = result.errors.at(index);
            lines.append(Tr::tr("%1: %2").arg(error.sourcePath, error.message));
        }
        if (result.errors.size() > displayedErrors) {
            lines.append(
                Tr::tr("%n additional error(s) not displayed", nullptr,
                       int(result.errors.size() - displayedErrors)));
        }
    }
    return lines.join('\n');
}

EsiRepositoryPage::EsiRepositoryPage(
    Core::DeviceRepositoryProvider *repository, QWidget *parent, FilePicker filePicker)
    : QWidget(parent)
    , m_repository(repository)
    , m_filePicker(std::move(filePicker))
    , m_title(new QLabel(Tr::tr("ESI Device Repository"), this))
    , m_description(new QLabel(this))
    , m_summary(new QGroupBox(Tr::tr("Repository summary"), this))
    , m_status(new QLineEdit(m_summary))
    , m_deviceCount(new QLineEdit(m_summary))
    , m_supportedCount(new QLineEdit(m_summary))
    , m_limitedCount(new QLineEdit(m_summary))
    , m_vendorCount(new QLineEdit(m_summary))
    , m_sourceCount(new QLineEdit(m_summary))
    , m_importFiles(new QPushButton(Tr::tr("Import ESI Files..."), this))
    , m_reload(new QPushButton(Tr::tr("Reload Device Descriptions"), this))
    , m_cancel(new QPushButton(Tr::tr("Cancel"), this))
    , m_progress(new QProgressBar(this))
    , m_operation(new QLabel(this))
    , m_result(new QPlainTextEdit(this))
{
    setObjectName("EtherCATEsiRepositoryContent");
    setAccessibleName(Tr::tr("ESI device repository management"));
    setAcceptDrops(true);

    m_title->setObjectName("EtherCATEsiRepositoryTitle");
    m_title->setFont(Utils::StyleHelper::uiFont(Utils::StyleHelper::UiElementH5));
    m_description->setObjectName("EtherCATEsiRepositoryDescription");
    m_description->setText(Tr::tr(
        "Manage the local offline ESI catalogue used to configure EtherCAT devices. "
        "Import one or more XML files, or reload descriptions already stored in the "
        "repository. No network or controller is accessed."));
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_summary->setObjectName("EtherCATEsiRepositorySummary");
    m_status->setObjectName("EtherCATEsiRepositoryStatus");
    m_deviceCount->setObjectName("EtherCATEsiRepositoryDevices");
    m_supportedCount->setObjectName("EtherCATEsiRepositorySupported");
    m_limitedCount->setObjectName("EtherCATEsiRepositoryLimited");
    m_vendorCount->setObjectName("EtherCATEsiRepositoryVendors");
    m_sourceCount->setObjectName("EtherCATEsiRepositorySources");
    m_status->setAccessibleName(Tr::tr("ESI repository status"));
    m_deviceCount->setAccessibleName(Tr::tr("Indexed ESI device count"));
    m_supportedCount->setAccessibleName(Tr::tr("Supported ESI device count"));
    m_limitedCount->setAccessibleName(Tr::tr("Limited ESI device count"));
    m_vendorCount->setAccessibleName(Tr::tr("Indexed ESI vendor count"));
    m_sourceCount->setAccessibleName(Tr::tr("Referenced ESI source path count"));
    for (QLineEdit *field :
         {m_status,
          m_deviceCount,
          m_supportedCount,
          m_limitedCount,
          m_vendorCount,
          m_sourceCount}) {
        field->setReadOnly(true);
        field->setAcceptDrops(false);
    }

    auto summaryLayout = new QFormLayout(m_summary);
    summaryLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    summaryLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    summaryLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
    summaryLayout->addRow(Tr::tr("Status:"), m_status);
    summaryLayout->addRow(Tr::tr("Indexed devices:"), m_deviceCount);
    summaryLayout->addRow(Tr::tr("Supported:"), m_supportedCount);
    summaryLayout->addRow(Tr::tr("Limited:"), m_limitedCount);
    summaryLayout->addRow(Tr::tr("Vendors:"), m_vendorCount);
    summaryLayout->addRow(Tr::tr("Referenced source paths:"), m_sourceCount);

    m_importFiles->setObjectName("EtherCATEsiRepositoryImport");
    m_reload->setObjectName("EtherCATEsiRepositoryReload");
    m_cancel->setObjectName("EtherCATEsiRepositoryCancel");
    m_importFiles->setAccessibleName(Tr::tr("Import ESI XML files"));
    m_reload->setAccessibleName(Tr::tr("Reload ESI device descriptions"));
    m_cancel->setAccessibleName(Tr::tr("Cancel ESI repository operation"));
    m_importFiles->setToolTip(
        Tr::tr("Import one or more local EtherCAT Slave Information XML files."));
    m_reload->setToolTip(
        Tr::tr("Reparse every ESI XML file already stored in the local repository."));
    m_cancel->setToolTip(Tr::tr("Cancel the current import or reload operation."));

    m_progress->setObjectName("EtherCATEsiRepositoryProgress");
    m_progress->setAccessibleName(Tr::tr("ESI repository operation progress"));
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_operation->setObjectName("EtherCATEsiRepositoryOperation");
    m_operation->setAccessibleName(Tr::tr("ESI repository operation state"));
    m_operation->setText(Tr::tr("No repository operation has been run in this session."));
    m_operation->setWordWrap(true);
    m_operation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_result->setObjectName("EtherCATEsiRepositoryResult");
    m_result->setAccessibleName(Tr::tr("ESI repository operation result"));
    m_result->setReadOnly(true);
    m_result->setAcceptDrops(false);
    m_result->setPlaceholderText(
        Tr::tr("Import and reload results, including rejected files, appear here."));

    auto actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(QMargins());
    actionLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHXs);
    actionLayout->addWidget(m_importFiles);
    actionLayout->addWidget(m_reload);
    actionLayout->addWidget(m_cancel);
    actionLayout->addStretch(1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_title);
    layout->addWidget(m_description);
    layout->addWidget(m_summary);
    layout->addLayout(actionLayout);
    layout->addWidget(m_operation);
    layout->addWidget(m_progress);
    layout->addWidget(m_result, 1);

    if (!m_filePicker) {
        m_filePicker = [] {
            return ::Core::DocumentManager::getOpenFileNames(
                Tr::tr("EtherCAT Slave Information (*.xml);;XML files (*.xml)"));
        };
    }

    connect(m_importFiles, &QPushButton::clicked, this, &EsiRepositoryPage::chooseFiles);
    connect(m_reload, &QPushButton::clicked, this, &EsiRepositoryPage::reloadDescriptions);
    connect(m_cancel, &QPushButton::clicked, this, &EsiRepositoryPage::cancelOperation);
    if (m_repository) {
        connect(m_repository, &Core::DeviceRepositoryProvider::devicesReset, this, [this] {
            refresh();
        });
        connect(
            m_repository,
            &Core::DeviceRepositoryProvider::devicesChanged,
            this,
            [this] { refresh(); });
        connect(
            m_repository,
            &Core::DeviceRepositoryProvider::indexingChanged,
            this,
            [this] { refresh(); });
    }
    refresh();
}

void EsiRepositoryPage::refresh()
{
    const bool available = m_repository && m_repository->isAvailable();
    const bool indexing = available && m_repository->isIndexing();
    m_status->setText(!available ? Tr::tr("Unavailable")
                                 : indexing ? Tr::tr("Reloading device descriptions")
                                            : Tr::tr("Ready"));

    const QList<Data::DeviceSummary> devices = available ? m_repository->devices()
                                                          : QList<Data::DeviceSummary>();
    QSet<quint32> vendors;
    QSet<QString> sources;
    int supported = 0;
    for (const Data::DeviceSummary &device : devices) {
        vendors.insert(device.identity.vendorId);
        supported += device.supported ? 1 : 0;
        const std::optional<Data::DeviceDescription> description = m_repository->device(device.id);
        if (description && !description->sourcePath.isEmpty())
            sources.insert(description->sourcePath);
    }
    m_deviceCount->setText(QString::number(devices.size()));
    m_supportedCount->setText(QString::number(supported));
    m_limitedCount->setText(QString::number(devices.size() - supported));
    m_vendorCount->setText(QString::number(vendors.size()));
    m_sourceCount->setText(QString::number(sources.size()));

    const bool active = operationActive();
    m_importFiles->setEnabled(available && !indexing && !active);
    m_reload->setEnabled(available && !indexing && !active);
    m_cancel->setEnabled(active && m_job->state() != Core::DeviceImportState::Canceling);
}

void EsiRepositoryPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_repository && m_repository->isAvailable() && !m_repository->isIndexing()
        && !operationActive() && !localXmlFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void EsiRepositoryPage::dropEvent(QDropEvent *event)
{
    const Utils::FilePaths files = localXmlFiles(event->mimeData());
    if (!m_repository || !m_repository->isAvailable() || m_repository->isIndexing()
        || files.isEmpty() || operationActive()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    importFiles(files);
}

void EsiRepositoryPage::chooseFiles()
{
    if (!m_filePicker)
        return;
    importFiles(m_filePicker());
}

void EsiRepositoryPage::importFiles(const Utils::FilePaths &filePaths)
{
    if (filePaths.isEmpty())
        return;
    if (!m_repository || !m_repository->isAvailable()) {
        showStartFailure(Tr::tr("The local ESI repository is unavailable."));
        return;
    }
    if (operationActive()) {
        showStartFailure(Tr::tr("Another ESI repository operation is already running."));
        return;
    }
    startJob(m_repository->importFiles(filePaths), Tr::tr("Import"));
}

void EsiRepositoryPage::reloadDescriptions()
{
    if (!m_repository || !m_repository->isAvailable()) {
        showStartFailure(Tr::tr("The local ESI repository is unavailable."));
        return;
    }
    if (operationActive()) {
        showStartFailure(Tr::tr("Another ESI repository operation is already running."));
        return;
    }
    startJob(m_repository->rebuildIndex(), Tr::tr("Reload"));
}

void EsiRepositoryPage::startJob(Core::DeviceImportJob *job, const QString &operationName)
{
    if (!job) {
        showStartFailure(Tr::tr("The ESI repository did not start the requested operation."));
        return;
    }

    m_job = job;
    m_operationName = operationName;
    m_operationActive = true;
    m_operation->setText(Tr::tr("%1 in progress...").arg(operationName));
    m_result->clear();
    updateProgress(job->progressValue(), job->progressMaximum());
    connect(job, &Core::DeviceImportJob::progressChanged, this, &EsiRepositoryPage::updateProgress);
    connect(job, &Core::DeviceImportJob::stateChanged, this, [this](Core::DeviceImportState state) {
        if (state == Core::DeviceImportState::Canceling)
            m_operation->setText(Tr::tr("Canceling %1...").arg(m_operationName.toLower()));
        refresh();
    });
    connect(job, &Core::DeviceImportJob::finished, this, &EsiRepositoryPage::finishOperation);
    connect(job, &QObject::destroyed, this, [this] {
        if (!m_operationActive)
            return;
        m_operationActive = false;
        m_operation->setText(Tr::tr("Repository operation stopped"));
        m_result->setPlainText(
            Tr::tr("The repository operation ended before a result was available."));
        refresh();
    });
    refresh();
}

void EsiRepositoryPage::cancelOperation()
{
    if (!operationActive())
        return;
    m_operation->setText(Tr::tr("Canceling %1...").arg(m_operationName.toLower()));
    m_cancel->setEnabled(false);
    m_job->cancel();
    refresh();
}

void EsiRepositoryPage::updateProgress(int value, int maximum)
{
    m_progress->setRange(0, std::max(1, maximum));
    m_progress->setValue(std::clamp(value, 0, std::max(1, maximum)));
}

void EsiRepositoryPage::finishOperation(const Data::DeviceImportResult &result)
{
    const QString operationName = m_operationName;
    m_operationActive = false;
    m_job.clear();
    m_operation->setText(
        result.canceled ? Tr::tr("%1 canceled").arg(operationName)
                        : Tr::tr("%1 complete").arg(operationName));
    m_result->setPlainText(importResultText(result));
    updateProgress(result.requestedFiles, result.requestedFiles);
    refresh();
}

void EsiRepositoryPage::showStartFailure(const QString &message)
{
    m_operation->setText(Tr::tr("Repository operation could not start"));
    m_result->setPlainText(message);
    refresh();
}

bool EsiRepositoryPage::operationActive() const
{
    return m_operationActive && m_job
           && m_job->state() != Core::DeviceImportState::Finished;
}

} // namespace EtherCAT::Workbench::Internal
