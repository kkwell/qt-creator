// Copyright (C) 2026 Kvell

#include "providers.h"

#include "ethercatcoretr.h"

namespace EtherCAT::Core {

bool isMockUiEnabled()
{
    if (qEnvironmentVariableIsSet("QTC_ETHER_CAT_ENABLE_MOCK_UI")) {
        return qEnvironmentVariable("QTC_ETHER_CAT_ENABLE_MOCK_UI") == "1";
    }
#ifdef WITH_TESTS
    return true;
#else
    return false;
#endif
}

Provider::Provider(ProviderKind kind, Utils::Id id, const QString &displayName, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_id(id)
    , m_displayName(displayName)
{}

ProviderKind Provider::kind() const
{
    return m_kind;
}

Utils::Id Provider::id() const
{
    return m_id;
}

QString Provider::displayName() const
{
    return m_displayName;
}

bool Provider::isAvailable() const
{
    return m_available;
}

void Provider::setDisplayName(const QString &displayName)
{
    if (m_displayName == displayName)
        return;

    m_displayName = displayName;
    emit displayNameChanged(m_displayName);
}

void Provider::setAvailable(bool available)
{
    if (m_available == available)
        return;

    m_available = available;
    emit availabilityChanged(m_available);
}

ProjectService::ProjectService(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::Project, id, displayName, parent)
{}

DeviceRepositoryProvider::DeviceRepositoryProvider(
    Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::DeviceRepository, id, displayName, parent)
{}

DeviceAdapterProvider::DeviceAdapterProvider(
    Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::DeviceAdapter, id, displayName, parent)
{}

DeviceImportJob::DeviceImportJob(QObject *parent)
    : QObject(parent)
{}

DeviceImportState DeviceImportJob::state() const
{
    return m_state;
}

int DeviceImportJob::progressValue() const
{
    return m_progressValue;
}

int DeviceImportJob::progressMaximum() const
{
    return m_progressMaximum;
}

Data::DeviceImportResult DeviceImportJob::result() const
{
    return m_result;
}

void DeviceImportJob::setState(DeviceImportState state)
{
    if (m_state == state || m_state == DeviceImportState::Finished)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void DeviceImportJob::setProgress(int value, int maximum)
{
    if (m_state == DeviceImportState::Finished)
        return;
    const int boundedMaximum = qMax(0, maximum);
    const int boundedValue = qBound(0, value, boundedMaximum);
    if (m_progressValue == boundedValue && m_progressMaximum == boundedMaximum)
        return;
    m_progressValue = boundedValue;
    m_progressMaximum = boundedMaximum;
    emit progressChanged(m_progressValue, m_progressMaximum);
}

void DeviceImportJob::finish(const Data::DeviceImportResult &result)
{
    if (m_state == DeviceImportState::Finished)
        return;
    m_result = result;
    m_state = DeviceImportState::Finished;
    emit stateChanged(m_state);
    emit finished(m_result);
}

PropertyPageProvider::PropertyPageProvider(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::PropertyPage, id, displayName, parent)
{}

ControllerConnectionProvider::ControllerConnectionProvider(
    Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::ControllerConnection, id, displayName, parent)
{}

std::optional<Data::ControllerConnectionProfileConfiguration>
ControllerConnectionProvider::connectionProfileConfiguration(
    const Data::ControllerConnectionScope &, const Data::NodeId &) const
{
    return std::nullopt;
}

Utils::Result<> ControllerConnectionProvider::setConnectionProfileEndpoint(
    const Data::ControllerConnectionScope &, const Data::NodeId &, const QString &)
{
    return Utils::ResultError(
        Tr::tr("This controller provider does not support editing connection profiles."));
}

bool ControllerConnectionProvider::supportsControlCommand(
    Data::ControllerControlCommand) const
{
    return false;
}

Utils::Result<> ControllerConnectionProvider::executeControlCommand(
    const Data::ControllerControlRequest &)
{
    return Utils::ResultError(
        Tr::tr("This controller provider does not support control commands."));
}

bool ControllerConnectionProvider::supportsPackageDeployment() const
{
    return false;
}

Utils::Result<> ControllerConnectionProvider::deployPackage(
    const Data::ControllerPackageDeploymentRequest &)
{
    return Utils::ResultError(
        Tr::tr("This controller provider does not support package deployment."));
}

Utils::Result<> ControllerConnectionProvider::cancelPackageDeployment(const QString &)
{
    return Utils::ResultError(
        Tr::tr("This controller provider does not support canceling package deployment."));
}

bool ControllerConnectionProvider::supportsRuntimeResources() const
{
    return false;
}

std::optional<Data::RuntimeResourceCatalog>
ControllerConnectionProvider::runtimeResourceCatalog() const
{
    return std::nullopt;
}

std::optional<Data::RuntimeResourceSnapshot>
ControllerConnectionProvider::runtimeResourceSnapshot() const
{
    return std::nullopt;
}

Utils::Result<> ControllerConnectionProvider::refreshRuntimeResources()
{
    return Utils::ResultError(
        Tr::tr("This controller provider does not support runtime resources."));
}

Utils::Result<> ControllerConnectionProvider::requestRuntimeResourceSnapshot(
    const Data::RuntimeResourceSnapshotRequest &)
{
    return Utils::ResultError(
        Tr::tr("This controller provider does not support targeted runtime resource snapshots."));
}

ScanProvider::ScanProvider(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::Scan, id, displayName, parent)
{}

DiagnosticsProvider::DiagnosticsProvider(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::Diagnostics, id, displayName, parent)
{}

} // namespace EtherCAT::Core
