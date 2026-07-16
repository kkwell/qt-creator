// Copyright (C) 2026 Kvell

#include "devicerepository.h"

#include "esiparser.h"
#include "ethercatdevicesconstants.h"
#include "ethercatdevicestr.h"

#include <utils/async.h>
#include <utils/fileutils.h>
#include <utils/qtcassert.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

namespace EtherCAT::Devices::Internal {

enum class JobKind { Import, Rebuild };

struct SourceRecord
{
    Utils::FilePath filePath;
    QString sourcePath;
    QDateTime importedAt;
    QByteArray expectedSha256;
};

struct ParsedSource
{
    QByteArray sha256;
    QString sourcePath;
    QDateTime importedAt;
    QList<Data::DeviceDescription> devices;
    QByteArray originalXml;
};

struct JobOutput
{
    Data::DeviceImportResult result;
    QList<ParsedSource> sources;
};

struct JobInput
{
    JobKind kind = JobKind::Import;
    Utils::FilePath sourcesRoot;
    QList<SourceRecord> sources;
    QSet<QByteArray> loadedHashes;
};

class DeviceRepositoryJob final : public Core::DeviceImportJob
{
public:
    DeviceRepositoryJob(DeviceRepository *repository, const JobInput &input)
        : Core::DeviceImportJob(repository)
        , m_repository(repository)
        , m_input(input)
        , m_output(std::make_shared<JobOutput>())
        , m_canceled(std::make_shared<std::atomic_bool>(false))
    {
        connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this] {
            if (m_repository)
                m_repository->handleJobFinished(this);
        });
    }

    JobKind kind() const { return m_input.kind; }
    JobOutput &output() { return *m_output; }
    void setInput(const JobInput &input) { m_input = input; }
    void setLoadedHashes(const QSet<QByteArray> &loadedHashes)
    {
        m_input.loadedHashes = loadedHashes;
    }
    void setImportTime(const QDateTime &importedAt)
    {
        for (SourceRecord &source : m_input.sources)
            source.importedAt = importedAt;
    }

    void start()
    {
        if (m_canceled->load()) {
            m_output->result.requestedFiles = m_input.sources.size();
            m_output->result.canceled = true;
            if (m_repository)
                m_repository->handleJobFinished(this);
            return;
        }

        setState(Core::DeviceImportState::Running);
        setProgress(0, m_input.sources.size());
        const JobInput input = m_input;
        const std::shared_ptr<JobOutput> output = m_output;
        const std::shared_ptr<std::atomic_bool> canceled = m_canceled;
        const QPointer<DeviceRepositoryJob> guard(this);
        m_watcher.setFuture(Utils::asyncRun([input, output, canceled, guard] {
            run(input, output.get(), canceled, guard);
        }));
    }

    void cancel() final
    {
        if (state() == Core::DeviceImportState::Finished)
            return;
        m_canceled->store(true);
        setState(Core::DeviceImportState::Canceling);
        m_watcher.future().cancel();
    }

    void waitForFinished()
    {
        if (m_watcher.isRunning())
            m_watcher.waitForFinished();
    }

    void complete()
    {
        setProgress(m_output->result.requestedFiles, m_output->result.requestedFiles);
        finish(m_output->result);
    }

private:
    static void reportProgress(const QPointer<DeviceRepositoryJob> &guard, int value, int maximum)
    {
        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard,
            [guard, value, maximum] {
                if (guard)
                    guard->setProgress(value, maximum);
            },
            Qt::QueuedConnection);
    }

    static void appendFailure(JobOutput *output, const SourceRecord &source, const QString &message)
    {
        ++output->result.failedFiles;
        output->result.errors.append({source.sourcePath, message});
    }

    static void run(
        const JobInput &input,
        JobOutput *output,
        const std::shared_ptr<std::atomic_bool> &canceled,
        const QPointer<DeviceRepositoryJob> &guard)
    {
        output->result.requestedFiles = input.sources.size();
        QSet<QByteArray> seenImportHashes;
        for (int index = 0; index < input.sources.size(); ++index) {
            if (canceled->load()) {
                output->result.canceled = true;
                break;
            }

            const SourceRecord &source = input.sources.at(index);
            const Utils::Result<QByteArray> contents = source.filePath.fileContents();
            if (!contents) {
                appendFailure(output, source, contents.error());
                reportProgress(guard, index + 1, input.sources.size());
                continue;
            }

            const QByteArray sha256 = QCryptographicHash::hash(
                *contents, QCryptographicHash::Sha256);
            if (!source.expectedSha256.isEmpty() && source.expectedSha256 != sha256) {
                appendFailure(
                    output,
                    source,
                    Tr::tr("Stored ESI SHA-256 mismatch for %1.")
                        .arg(source.filePath.fileName()));
                reportProgress(guard, index + 1, input.sources.size());
                continue;
            }
            const bool loadedDuplicate
                = input.kind == JobKind::Import
                  && (input.loadedHashes.contains(sha256) || seenImportHashes.contains(sha256));
            if (loadedDuplicate) {
                ++output->result.duplicateFiles;
                reportProgress(guard, index + 1, input.sources.size());
                continue;
            }
            if (input.kind == JobKind::Import)
                seenImportHashes.insert(sha256);

            const Utils::Result<QList<Data::DeviceDescription>> parsed = parseEsiFile(
                *contents, source.sourcePath, source.importedAt);
            if (!parsed) {
                appendFailure(output, source, parsed.error());
                reportProgress(guard, index + 1, input.sources.size());
                continue;
            }

            output->sources.append(
                {sha256,
                 source.sourcePath,
                 source.importedAt,
                 *parsed,
                 input.kind == JobKind::Import ? *contents : QByteArray()});
            reportProgress(guard, index + 1, input.sources.size());
        }

        if (canceled->load()) {
            output->result.canceled = true;
            output->sources.clear();
            return;
        }
        if (input.kind != JobKind::Import)
            return;

        QList<ParsedSource> persistedSources;
        QList<Utils::FilePath> createdFiles;
        for (const ParsedSource &source : std::as_const(output->sources)) {
            if (canceled->load())
                break;
            const Utils::FilePath storedPath = input.sourcesRoot.pathAppended(
                QString::fromLatin1(source.sha256.toHex()) + ".xml");
            if (storedPath.exists()) {
                ++output->result.duplicateFiles;
                persistedSources.append(source);
                continue;
            }

            Utils::FileSaver saver(storedPath);
            if (!saver.write(source.originalXml)) {
                ++output->result.failedFiles;
                output->result.errors.append({source.sourcePath, saver.errorString()});
                continue;
            }
            const Utils::Result<> saved = saver.finalize();
            if (!saved) {
                ++output->result.failedFiles;
                output->result.errors.append({source.sourcePath, saved.error()});
                continue;
            }
            createdFiles.append(storedPath);
            persistedSources.append(source);
        }

        if (canceled->load()) {
            for (const Utils::FilePath &createdFile : std::as_const(createdFiles)) {
                const Utils::Result<> removed = createdFile.removeFile();
                if (!removed) {
                    ++output->result.failedFiles;
                    output->result.errors.append(
                        {createdFile.toUserOutput(),
                         Tr::tr("Cancellation cleanup failed: %1").arg(removed.error())});
                }
            }
            output->result.canceled = true;
            output->sources.clear();
            return;
        }
        output->sources = std::move(persistedSources);
    }

    QPointer<DeviceRepository> m_repository;
    JobInput m_input;
    std::shared_ptr<JobOutput> m_output;
    std::shared_ptr<std::atomic_bool> m_canceled;
    QFutureWatcher<void> m_watcher;
};

static bool summaryLessThan(const Data::DeviceSummary &left, const Data::DeviceSummary &right)
{
    if (left.identity.vendorId != right.identity.vendorId)
        return left.identity.vendorId < right.identity.vendorId;
    if (left.identity.productCode != right.identity.productCode)
        return left.identity.productCode < right.identity.productCode;
    if (left.identity.revisionNumber != right.identity.revisionNumber)
        return left.identity.revisionNumber < right.identity.revisionNumber;
    return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
}

DeviceRepository::DeviceRepository(const Utils::FilePath &repositoryRoot, QObject *parent)
    : Core::DeviceRepositoryProvider(
          Utils::Id(Constants::REPOSITORY_PROVIDER_ID), Tr::tr("ESI Device Repository"), parent)
    , m_repositoryRoot(repositoryRoot)
    , m_sourcesRoot(repositoryRoot.pathAppended("sources"))
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);
    const Utils::Result<> rootResult = m_repositoryRoot.ensureWritableDir();
    const Utils::Result<> sourcesResult = m_sourcesRoot.ensureWritableDir();
    setAvailable(rootResult && sourcesResult);
    if (isAvailable())
        loadMetadata();
}

DeviceRepository::~DeviceRepository()
{
    shutdown();
}

QList<Data::DeviceSummary> DeviceRepository::devices(const Data::DeviceFilter &filter) const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return {});
    QList<Data::DeviceSummary> result;
    const QString searchText = filter.text.trimmed();
    for (const Data::DeviceDescription &description : m_devices) {
        const Data::DeviceSummary &summary = description.summary;
        if (filter.filterByVendor && summary.identity.vendorId != filter.vendorId)
            continue;
        if (!filter.group.isEmpty()
            && summary.group.compare(filter.group, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (filter.supportedOnly && !summary.supported)
            continue;
        if (!searchText.isEmpty()) {
            const QString identity = QString("%1 %2 %3")
                                         .arg(summary.identity.vendorId, 8, 16, QLatin1Char('0'))
                                         .arg(summary.identity.productCode, 8, 16, QLatin1Char('0'))
                                         .arg(summary.identity.revisionNumber, 8, 16, QLatin1Char('0'));
            const QString haystack = summary.name + ' ' + summary.typeName + ' ' + summary.group
                                     + ' ' + identity;
            if (!haystack.contains(searchText, Qt::CaseInsensitive))
                continue;
        }
        result.append(summary);
    }
    std::sort(result.begin(), result.end(), summaryLessThan);
    return result;
}

std::optional<Data::DeviceDescription> DeviceRepository::device(
    const Data::NodeId &deviceId) const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return std::nullopt);
    const auto it = m_devices.constFind(deviceId);
    if (it == m_devices.cend())
        return std::nullopt;
    return it.value();
}

QByteArray DeviceRepository::originalXml(const Data::NodeId &deviceId) const
{
    QTC_ASSERT(QThread::currentThread() == thread(), return {});
    const QByteArray hash = m_deviceSourceHashes.value(deviceId);
    if (hash.isEmpty())
        return {};
    const Utils::Result<QByteArray> contents = m_sourcesRoot
                                                   .pathAppended(QString::fromLatin1(hash.toHex())
                                                                 + ".xml")
                                                   .fileContents();
    return contents ? *contents : QByteArray();
}

Core::DeviceImportJob *DeviceRepository::importFiles(const Utils::FilePaths &filePaths)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return nullptr);
    if (m_shuttingDown)
        return nullptr;

    JobInput input;
    input.kind = JobKind::Import;
    input.sourcesRoot = m_sourcesRoot;
    input.loadedHashes = m_loadedSourceHashes;
    for (const Utils::FilePath &filePath : filePaths)
        input.sources.append({filePath, filePath.toUserOutput(), {}, {}});

    auto *job = new DeviceRepositoryJob(this, input);
    m_jobs.insert(job);
    m_jobQueue.append(job);
    connect(job, &Core::DeviceImportJob::finished, this, [this, job] { removeJob(job); });
    QMetaObject::invokeMethod(this, [this] { startNextJob(); }, Qt::QueuedConnection);
    return job;
}

Core::DeviceImportJob *DeviceRepository::rebuildIndex()
{
    QTC_ASSERT(QThread::currentThread() == thread(), return nullptr);
    if (m_shuttingDown)
        return nullptr;
    if (m_indexJob)
        return m_indexJob;

    JobInput input;
    input.kind = JobKind::Rebuild;
    input.sourcesRoot = m_sourcesRoot;

    auto *job = new DeviceRepositoryJob(this, input);
    m_indexJob = job;
    m_jobs.insert(job);
    m_jobQueue.append(job);
    emit indexingChanged(true);
    connect(job, &Core::DeviceImportJob::finished, this, [this, job] { removeJob(job); });
    QMetaObject::invokeMethod(this, [this] { startNextJob(); }, Qt::QueuedConnection);
    return job;
}

bool DeviceRepository::isIndexing() const
{
    return m_indexJob;
}

Utils::FilePath DeviceRepository::repositoryRoot() const
{
    return m_repositoryRoot;
}

int DeviceRepository::activeJobCount() const
{
    return m_jobs.size();
}

void DeviceRepository::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    const QList<DeviceRepositoryJob *> jobs = m_jobs.values();
    for (DeviceRepositoryJob *job : jobs)
        job->cancel();
    for (DeviceRepositoryJob *job : jobs)
        job->waitForFinished();
    m_jobs.clear();
    m_jobQueue.clear();
    m_activeJob = nullptr;
    m_indexJob = nullptr;
}

void DeviceRepository::loadMetadata()
{
    const Utils::FilePath metadataPath = m_repositoryRoot.pathAppended("repository.json");
    if (!metadataPath.exists())
        return;
    const Utils::Result<QByteArray> contents = metadataPath.fileContents();
    if (!contents)
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(*contents, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject root = document.object();
    if (root.value("format").toString() != "ethercat-esi-repository"
        || root.value("formatVersion").toInt() != 1) {
        return;
    }
    const QJsonArray sources = root.value("sources").toArray();
    for (const QJsonValue &value : sources) {
        const QJsonObject source = value.toObject();
        const QByteArray hash = QByteArray::fromHex(source.value("sha256").toString().toLatin1());
        if (hash.size() != 32)
            continue;
        SourceMetadata metadata;
        metadata.sourcePath = source.value("sourcePath").toString();
        metadata.importedAt = QDateTime::fromString(
            source.value("importedAt").toString(), Qt::ISODateWithMs);
        m_sourceMetadata.insert(hash, metadata);
    }
}

Utils::Result<> DeviceRepository::saveMetadata() const
{
    QList<QByteArray> hashes = m_sourceMetadata.keys();
    std::sort(hashes.begin(), hashes.end());
    QJsonArray sources;
    for (const QByteArray &hash : std::as_const(hashes)) {
        const SourceMetadata metadata = m_sourceMetadata.value(hash);
        QJsonObject source;
        source.insert("sha256", QString::fromLatin1(hash.toHex()));
        source.insert("sourcePath", metadata.sourcePath);
        source.insert("importedAt", metadata.importedAt.toUTC().toString(Qt::ISODateWithMs));
        sources.append(source);
    }
    QJsonObject root;
    root.insert("format", "ethercat-esi-repository");
    root.insert("formatVersion", 1);
    root.insert("sources", sources);
    Utils::FileSaver saver(m_repositoryRoot.pathAppended("repository.json"), QIODevice::Text);
    if (!saver.write(QJsonDocument(root).toJson()))
        return Utils::ResultError(saver.errorString());
    return saver.finalize();
}

void DeviceRepository::startNextJob()
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);
    if (m_shuttingDown || m_activeJob || m_jobQueue.isEmpty())
        return;

    m_activeJob = m_jobQueue.takeFirst();
    if (m_activeJob->kind() == JobKind::Rebuild)
        prepareRebuildJob(m_activeJob);
    else {
        QDateTime importedAt = QDateTime::currentDateTimeUtc();
        for (const SourceMetadata &metadata : m_sourceMetadata) {
            if (metadata.importedAt.isValid() && metadata.importedAt >= importedAt)
                importedAt = metadata.importedAt.addMSecs(1);
        }
        m_activeJob->setImportTime(importedAt);
        m_activeJob->setLoadedHashes(m_loadedSourceHashes);
    }
    m_activeJob->start();
}

void DeviceRepository::prepareRebuildJob(DeviceRepositoryJob *job) const
{
    JobInput input;
    input.kind = JobKind::Rebuild;
    input.sourcesRoot = m_sourcesRoot;
    Utils::FilePaths sourceFiles = m_sourcesRoot.dirEntries(QDir::Files | QDir::NoDotAndDotDot);
    sourceFiles.erase(
        std::remove_if(sourceFiles.begin(), sourceFiles.end(), [](const Utils::FilePath &filePath) {
            return filePath.suffix().compare("xml", Qt::CaseInsensitive) != 0;
        }),
        sourceFiles.end());
    std::sort(sourceFiles.begin(), sourceFiles.end(), [](const auto &left, const auto &right) {
        return left.toUrlishString() < right.toUrlishString();
    });
    for (const Utils::FilePath &filePath : std::as_const(sourceFiles)) {
        const QByteArray hash = QByteArray::fromHex(filePath.completeBaseName().toLatin1());
        const SourceMetadata metadata = m_sourceMetadata.value(hash);
        const QString sourcePath = metadata.sourcePath.isEmpty() ? filePath.toUserOutput()
                                                                 : metadata.sourcePath;
        const QDateTime importedAt = metadata.importedAt.isValid() ? metadata.importedAt
                                                                   : filePath.lastModified().toUTC();
        input.sources.append({filePath, sourcePath, importedAt, hash});
    }
    job->setInput(input);
}

void DeviceRepository::handleJobFinished(DeviceRepositoryJob *job)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);
    if (!m_jobs.contains(job) || job->state() == Core::DeviceImportState::Finished)
        return;

    JobOutput &output = job->output();
    if (!m_shuttingDown) {
        std::sort(
            output.sources.begin(),
            output.sources.end(),
            [](const ParsedSource &left, const ParsedSource &right) {
                if (left.importedAt != right.importedAt)
                    return left.importedAt < right.importedAt;
                return left.sha256 < right.sha256;
            });
        QHash<Data::NodeId, Data::DeviceDescription> nextDevices
            = job->kind() == JobKind::Rebuild
                  ? QHash<Data::NodeId, Data::DeviceDescription>()
                  : m_devices;
        QHash<Data::NodeId, QByteArray> nextSourceHashes
            = job->kind() == JobKind::Rebuild ? QHash<Data::NodeId, QByteArray>()
                                              : m_deviceSourceHashes;
        QSet<QByteArray> nextLoadedHashes
            = job->kind() == JobKind::Rebuild ? QSet<QByteArray>() : m_loadedSourceHashes;

        for (const ParsedSource &source : std::as_const(output.sources)) {
            m_sourceMetadata.insert(source.sha256, {source.sourcePath, source.importedAt});
            nextLoadedHashes.insert(source.sha256);
            for (const Data::DeviceDescription &description : source.devices) {
                if (nextDevices.contains(description.summary.id))
                    ++output.result.updatedDevices;
                else
                    ++output.result.importedDevices;
                nextDevices.insert(description.summary.id, description);
                nextSourceHashes.insert(description.summary.id, source.sha256);
                if (!output.result.affectedDeviceIds.contains(description.summary.id))
                    output.result.affectedDeviceIds.append(description.summary.id);
            }
        }

        m_devices = std::move(nextDevices);
        m_deviceSourceHashes = std::move(nextSourceHashes);
        m_loadedSourceHashes = std::move(nextLoadedHashes);
        const Utils::Result<> metadataResult = saveMetadata();
        if (!metadataResult) {
            output.result.errors.append(
                {m_repositoryRoot.toUserOutput(), metadataResult.error()});
        }

        if (job->kind() == JobKind::Rebuild)
            emit devicesReset();
        else if (!output.result.affectedDeviceIds.isEmpty())
            emit devicesChanged(output.result.affectedDeviceIds);
    }

    if (job == m_indexJob) {
        m_indexJob = nullptr;
        emit indexingChanged(false);
    }
    job->complete();
}

void DeviceRepository::removeJob(DeviceRepositoryJob *job)
{
    m_jobs.remove(job);
    m_jobQueue.removeAll(job);
    if (job == m_activeJob)
        m_activeJob = nullptr;
    if (job == m_indexJob) {
        m_indexJob = nullptr;
        emit indexingChanged(false);
    }
    job->deleteLater();
    QMetaObject::invokeMethod(this, [this] { startNextJob(); }, Qt::QueuedConnection);
}

} // namespace EtherCAT::Devices::Internal
