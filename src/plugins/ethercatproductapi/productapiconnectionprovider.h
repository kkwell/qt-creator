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
    Data::ControllerConnectionSnapshot connectionSnapshot() const override;

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &request) override;
    Utils::Result<> disconnectFromController() override;
    Utils::Result<> refreshController() override;

    void shutdown();

#ifdef WITH_TESTS
    ProductApiSession *sessionForTests() const;
#endif

private:
    Data::NodeId defaultProfileId() const;

    ProductApiSession *m_session = nullptr;
};

} // namespace EtherCAT::ProductApi::Internal
