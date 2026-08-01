// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/providers.h>

#include <utils/filepath.h>

#include <memory>
#include <QStringList>

namespace EtherCAT::DeviceAdapters::Internal {

enum class AdapterAuthorizationState {
    NotInstalled,
    Authorized,
    Denied,
    ValidationFailed,
};

enum class AdapterAuthorizationFailure {
    None,
    IncompleteBundle,
    InvalidSignature,
    InvalidTrustRoot,
    InvalidDocument,
    Revoked,
    PolicyConflict,
    BindingMismatch,
    InvalidSignerScope,
    InvalidFile,
    Unknown,
};

struct AdapterAuthorizationStatus
{
    AdapterAuthorizationState state = AdapterAuthorizationState::NotInstalled;
    AdapterAuthorizationFailure firstFailure = AdapterAuthorizationFailure::None;
    int authorizedAdapterCount = 0;
    int validationFailureCount = 0;
};

QString adapterAuthorizationStartupMessage(const AdapterAuthorizationStatus &status);

class AdapterPackageRepository final : public Core::DeviceAdapterProvider
{
    Q_OBJECT

public:
    explicit AdapterPackageRepository(const Utils::FilePath &packageRoot, QObject *parent = nullptr);
    AdapterPackageRepository(
        const Utils::FilePath &packageRoot,
        const Utils::FilePath &authorizationRoot,
        const Utils::FilePath &authorizationTrustRoot,
        QObject *parent = nullptr);
    ~AdapterPackageRepository() final;

    QList<Data::DeviceAdapterManifest> adapterManifests() const final;
    std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &adapterId, const QString &version) const final;
    Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &request) const final;
    QList<Core::ProviderStartupDiagnostic> startupDiagnostics() const final;

    Utils::FilePath packageRoot() const;
    QStringList loadErrors() const;
    QStringList authorizationDiagnostics() const;
    AdapterAuthorizationStatus authorizationStatus() const;
    int loadedPackageCount() const;
    void reload();

private:
    class Private;
    const std::unique_ptr<Private> d;
};

} // namespace EtherCAT::DeviceAdapters::Internal
