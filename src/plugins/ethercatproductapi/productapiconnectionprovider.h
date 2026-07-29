// Copyright (C) 2026 Kvell

#pragma once

#include "productapisession.h"

#include <ethercatcore/providers.h>

namespace EtherCAT::ProductApi::Internal {

class ProductApiConnectionProvider final : public Core::ControllerConnectionProvider
{
    Q_OBJECT

public:
    explicit ProductApiConnectionProvider(QObject *parent = nullptr);
    ProductApiConnectionProvider(
        const ProductApiSession::EndpointSet &endpoints,
        const ProductApiSession::Options &options,
        QObject *parent = nullptr);
    ~ProductApiConnectionProvider() override;

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &scope) const override;
    std::optional<Data::ControllerConnectionProfileConfiguration>
    connectionProfileConfiguration(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &profileId) const override;
    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &scope,
        const Data::NodeId &profileId,
        const QString &endpoint) override;
    Data::ControllerConnectionSnapshot connectionSnapshot() const override;

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &request) override;
    Utils::Result<> disconnectFromController() override;
    Utils::Result<> refreshController() override;
    bool supportsRuntimeResources() const override;
    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const override;
    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const override;
    Utils::Result<> refreshRuntimeResources() override;
    bool supportsControlCommand(Data::ControllerControlCommand command) const override;
    Utils::Result<> executeControlCommand(
        const Data::ControllerControlRequest &request) override;
    bool supportsPackageDeployment() const override;
    Utils::Result<> deployPackage(const Data::ControllerPackageDeploymentRequest &request) override;
    Utils::Result<> cancelPackageDeployment(const QString &operationId) override;

    void shutdown();

#ifdef WITH_TESTS
    ProductApiSession *sessionForTests() const;
#endif

private:
    Data::NodeId defaultProfileId() const;

    ProductApiSession *m_session = nullptr;
    bool m_persistEndpoint = false;
};

} // namespace EtherCAT::ProductApi::Internal
