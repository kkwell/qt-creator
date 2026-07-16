// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/devicedescription.h>
#include <ethercatdata/projectsnapshot.h>

#include <utils/filepath.h>
#include <utils/id.h>
#include <utils/result.h>

#include <QObject>

#include <optional>

namespace EtherCAT::Core {

enum class ProviderKind { Project, DeviceRepository, PropertyPage, Scan, Diagnostics };
enum class DeviceImportState { Pending, Running, Canceling, Finished };

class DeviceImportJob;

class ETHERCATCORE_EXPORT Provider : public QObject
{
    Q_OBJECT

public:
    Provider(ProviderKind kind, Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    ProviderKind kind() const;
    Utils::Id id() const;
    QString displayName() const;
    bool isAvailable() const;

    void setDisplayName(const QString &displayName);
    void setAvailable(bool available);

signals:
    void displayNameChanged(const QString &displayName);
    void availabilityChanged(bool available);

private:
    const ProviderKind m_kind;
    const Utils::Id m_id;
    QString m_displayName;
    bool m_available = false;
};

class ETHERCATCORE_EXPORT ProjectService : public Provider
{
    Q_OBJECT

public:
    ProjectService(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<Data::ProjectSnapshot> projects() const = 0;
    virtual std::optional<Data::ProjectSnapshot> project(const Data::NodeId &projectId) const = 0;
    virtual Data::NodeId activeProjectId() const = 0;

    virtual Utils::Result<> activateProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> renameProject(const Data::NodeId &projectId, const QString &name) = 0;
    virtual Utils::Result<> saveProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> undoProject(const Data::NodeId &projectId) = 0;
    virtual Utils::Result<> redoProject(const Data::NodeId &projectId) = 0;
    virtual bool canUndoProject(const Data::NodeId &projectId) const = 0;
    virtual bool canRedoProject(const Data::NodeId &projectId) const = 0;

signals:
    void projectAdded(const EtherCAT::Data::ProjectSnapshot &project);
    void projectAboutToBeRemoved(const EtherCAT::Data::NodeId &projectId);
    void projectChanged(const EtherCAT::Data::ProjectSnapshot &project);
    void activeProjectChanged(
        const EtherCAT::Data::NodeId &oldProjectId, const EtherCAT::Data::NodeId &newProjectId);
};

class ETHERCATCORE_EXPORT DeviceRepositoryProvider : public Provider
{
    Q_OBJECT

public:
    DeviceRepositoryProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    virtual QList<Data::DeviceSummary> devices(const Data::DeviceFilter &filter = {}) const = 0;
    virtual std::optional<Data::DeviceDescription> device(const Data::NodeId &deviceId) const = 0;
    virtual QByteArray originalXml(const Data::NodeId &deviceId) const = 0;
    virtual DeviceImportJob *importFiles(const Utils::FilePaths &filePaths) = 0;
    virtual DeviceImportJob *rebuildIndex() = 0;
    virtual bool isIndexing() const = 0;

signals:
    void devicesReset();
    void devicesChanged(const QList<EtherCAT::Data::NodeId> &deviceIds);
    void indexingChanged(bool indexing);
};

class ETHERCATCORE_EXPORT DeviceImportJob : public QObject
{
    Q_OBJECT

public:
    explicit DeviceImportJob(QObject *parent = nullptr);

    DeviceImportState state() const;
    int progressValue() const;
    int progressMaximum() const;
    Data::DeviceImportResult result() const;

    virtual void cancel() = 0;

signals:
    void stateChanged(EtherCAT::Core::DeviceImportState state);
    void progressChanged(int value, int maximum);
    void finished(const EtherCAT::Data::DeviceImportResult &result);

protected:
    void setState(DeviceImportState state);
    void setProgress(int value, int maximum);
    void finish(const Data::DeviceImportResult &result);

private:
    DeviceImportState m_state = DeviceImportState::Pending;
    int m_progressValue = 0;
    int m_progressMaximum = 0;
    Data::DeviceImportResult m_result;
};

class ETHERCATCORE_EXPORT PropertyPageProvider : public Provider
{
    Q_OBJECT

public:
    PropertyPageProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

class ETHERCATCORE_EXPORT ScanProvider : public Provider
{
    Q_OBJECT

public:
    ScanProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

class ETHERCATCORE_EXPORT DiagnosticsProvider : public Provider
{
    Q_OBJECT

public:
    DiagnosticsProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::ProviderKind)
Q_DECLARE_METATYPE(EtherCAT::Core::DeviceImportState)
