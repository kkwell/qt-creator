// Copyright (C) 2026 Kvell

#include "productapiconnectionprovider.h"

#include "ethercatproductapiconstants.h"
#include "ethercatproductapitr.h"

namespace EtherCAT::ProductApi::Internal {

ProductApiConnectionProvider::ProductApiConnectionProvider(QObject *parent)
    : ProductApiConnectionProvider(
          ProductApiSession::EndpointSet::productionDefaults(), ProductApiSession::Options(), parent)
{}

ProductApiConnectionProvider::ProductApiConnectionProvider(
    const ProductApiSession::EndpointSet &endpoints,
    const ProductApiSession::Options &options,
    QObject *parent)
    : Core::ControllerConnectionProvider(
          Constants::CONTROLLER_PROVIDER_ID, Tr::tr("Embed Labs Product API"), parent)
    , m_session(new ProductApiSession(endpoints, options, this))
{
    connect(m_session, &ProductApiSession::snapshotChanged, this, [this] {
        emit connectionSnapshotChanged();
    });
    setAvailable(endpoints.isValid() && options.isValid());
}

ProductApiConnectionProvider::~ProductApiConnectionProvider()
{
    shutdown();
}

QList<Data::ControllerConnectionProfile> ProductApiConnectionProvider::connectionProfiles(
    const Data::ControllerConnectionScope &scope) const
{
    if (scope.projectId.isNull() || scope.masterId.isNull())
        return {};

    Data::ControllerConnectionProfile profile;
    profile.id = defaultProfileId();
    profile.displayName = Tr::tr("Embed Labs Product API v1.9");
    profile.endpointSummary = m_session->snapshot().endpointSummary;
    profile.configured = !profile.endpointSummary.isEmpty();
    profile.supported = isAvailable();
    profile.defaultProfile = true;
    if (!profile.configured)
        profile.configurationIssue = Tr::tr("The controller endpoint is not configured.");
    else if (!profile.supported)
        profile.configurationIssue = Tr::tr("The controller connection profile is unavailable.");
    return {profile};
}

Data::ControllerConnectionSnapshot ProductApiConnectionProvider::connectionSnapshot() const
{
    return m_session->snapshot();
}

Utils::Result<> ProductApiConnectionProvider::connectToController(
    const Data::ControllerConnectionRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    if (request.scope.projectId.isNull() || request.scope.masterId.isNull()) {
        return Utils::ResultError(
            Tr::tr("Select an EtherCAT project and master before connecting."));
    }
    if (request.profileId != defaultProfileId())
        return Utils::ResultError(Tr::tr("The selected controller profile is not available."));
    return m_session->connectToController(request);
}

Utils::Result<> ProductApiConnectionProvider::disconnectFromController()
{
    return m_session->disconnectFromController();
}

Utils::Result<> ProductApiConnectionProvider::refreshController()
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->refreshController();
}

void ProductApiConnectionProvider::shutdown()
{
    if (!m_session)
        return;
    m_session->shutdown();
    if (isAvailable()) {
        setAvailable(false);
        emit connectionProfilesChanged();
    }
}

#ifdef WITH_TESTS
ProductApiSession *ProductApiConnectionProvider::sessionForTests() const
{
    return m_session;
}
#endif

Data::NodeId ProductApiConnectionProvider::defaultProfileId() const
{
    return Data::NodeId::fromString(QString::fromLatin1(Constants::DEFAULT_PROFILE_ID));
}

} // namespace EtherCAT::ProductApi::Internal
