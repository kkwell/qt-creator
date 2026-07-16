// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <utils/filepath.h>

#include <QHash>
#include <QSet>

namespace EtherCAT::Devices::Internal {

class DeviceRepositoryJob;

class DeviceRepository final : public Core::DeviceRepositoryProvider
{
    Q_OBJECT

public:
    explicit DeviceRepository(const Utils::FilePath &repositoryRoot, QObject *parent = nullptr);
    ~DeviceRepository() final;

    QList<Data::DeviceSummary> devices(const Data::DeviceFilter &filter = {}) const final;
    std::optional<Data::DeviceDescription> device(const Data::NodeId &deviceId) const final;
    QByteArray originalXml(const Data::NodeId &deviceId) const final;
    Core::DeviceImportJob *importFiles(const Utils::FilePaths &filePaths) final;
    Core::DeviceImportJob *rebuildIndex() final;
    bool isIndexing() const final;

    Utils::FilePath repositoryRoot() const;
    int activeJobCount() const;
    void shutdown();

private:
    friend class DeviceRepositoryJob;

    struct SourceMetadata
    {
        QString sourcePath;
        QDateTime importedAt;
    };

    void loadMetadata();
    Utils::Result<> saveMetadata() const;
    void startNextJob();
    void prepareRebuildJob(DeviceRepositoryJob *job) const;
    void handleJobFinished(DeviceRepositoryJob *job);
    void removeJob(DeviceRepositoryJob *job);

    Utils::FilePath m_repositoryRoot;
    Utils::FilePath m_sourcesRoot;
    QHash<Data::NodeId, Data::DeviceDescription> m_devices;
    QHash<Data::NodeId, QByteArray> m_deviceSourceHashes;
    QHash<QByteArray, SourceMetadata> m_sourceMetadata;
    QSet<QByteArray> m_loadedSourceHashes;
    QSet<DeviceRepositoryJob *> m_jobs;
    QList<DeviceRepositoryJob *> m_jobQueue;
    DeviceRepositoryJob *m_activeJob = nullptr;
    DeviceRepositoryJob *m_indexJob = nullptr;
    bool m_shuttingDown = false;
};

} // namespace EtherCAT::Devices::Internal
