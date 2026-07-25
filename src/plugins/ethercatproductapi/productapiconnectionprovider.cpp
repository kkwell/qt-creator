// Copyright (C) 2026 Kvell

#include "productapiconnectionprovider.h"

#include "ethercatproductapiconstants.h"
#include "ethercatproductapitr.h"

#include <utils/qtcsettings.h>

#include <QHostAddress>

namespace EtherCAT::ProductApi::Internal {

static bool isAsciiDecimal(QStringView value)
{
    if (value.isEmpty())
        return false;
    for (const QChar character : value) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    }
    return true;
}

static Utils::Result<ProductApiSession::EndpointSet> endpointsFromBaseEndpoint(
    const QString &input)
{
    const QString endpoint = input.trimmed();
    if (endpoint.isEmpty())
        return Utils::ResultError(Tr::tr("Enter a controller IP address."));
    if (endpoint.count(QLatin1Char(':')) > 1) {
        return Utils::ResultError(
            Tr::tr("Use an IPv4 address with an optional base port."));
    }

    const qsizetype separator = endpoint.indexOf(QLatin1Char(':'));
    const QString host
        = (separator < 0 ? endpoint : endpoint.first(separator)).trimmed();
    const QStringList octets = host.split(QLatin1Char('.'));
    if (octets.size() != 4) {
        return Utils::ResultError(Tr::tr("Enter a valid IPv4 controller address."));
    }
    QStringList canonicalOctets;
    canonicalOctets.reserve(octets.size());
    for (const QString &octet : octets) {
        bool octetOk = false;
        const int value = octet.toInt(&octetOk);
        if (!isAsciiDecimal(octet) || !octetOk || value > 255) {
            return Utils::ResultError(Tr::tr("Enter a valid IPv4 controller address."));
        }
        canonicalOctets.append(QString::number(value));
    }
    QHostAddress address;
    if (!address.setAddress(canonicalOctets.join(QLatin1Char('.')))
        || address.protocol() != QAbstractSocket::IPv4Protocol) {
        return Utils::ResultError(Tr::tr("Enter a valid IPv4 controller address."));
    }

    int basePort = 15200;
    if (separator >= 0) {
        const QString port = endpoint.sliced(separator + 1).trimmed();
        bool portOk = false;
        basePort = port.toInt(&portOk);
        if (!isAsciiDecimal(port) || !portOk || basePort < 1 || basePort > 65533) {
            return Utils::ResultError(
                Tr::tr("The base port must be between 1 and 65533."));
        }
    }

    const QString canonicalHost = address.toString();
    return ProductApiSession::EndpointSet{
        canonicalHost,
        quint16(basePort),
        quint16(basePort + 1),
        quint16(basePort + 2),
        QStringLiteral("%1:%2").arg(canonicalHost).arg(basePort),
    };
}

static ProductApiSession::EndpointSet configuredProductionEndpoints()
{
    const ProductApiSession::EndpointSet defaults
        = ProductApiSession::EndpointSet::productionDefaults();
    const QString configured = Utils::userSettings()
                                   .value(Constants::BASE_ENDPOINT_SETTINGS_KEY,
                                          defaults.endpointSummary)
                                   .toString();
    const Utils::Result<ProductApiSession::EndpointSet> parsed
        = endpointsFromBaseEndpoint(configured);
    return parsed ? *parsed : defaults;
}

ProductApiConnectionProvider::ProductApiConnectionProvider(QObject *parent)
    : ProductApiConnectionProvider(
          configuredProductionEndpoints(), ProductApiSession::Options(), parent)
{
    m_persistEndpoint = true;
}

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
    profile.displayName = Tr::tr("Embed Labs Product API v1.10");
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

std::optional<Data::ControllerConnectionProfileConfiguration>
ProductApiConnectionProvider::connectionProfileConfiguration(
    const Data::ControllerConnectionScope &scope, const Data::NodeId &profileId) const
{
    if (scope.projectId.isNull() || scope.masterId.isNull()
        || profileId != defaultProfileId()) {
        return std::nullopt;
    }

    const Data::ControllerConnectionSnapshot snapshot = m_session->snapshot();
    Data::ControllerConnectionProfileConfiguration configuration;
    configuration.profileId = profileId;
    configuration.endpoint = snapshot.endpointSummary;
    configuration.placeholder
        = ProductApiSession::EndpointSet::productionDefaults().endpointSummary;
    configuration.editable
        = snapshot.state == Data::ControllerConnectionState::Disconnected
          || snapshot.state == Data::ControllerConnectionState::Failed;
    return configuration;
}

Utils::Result<> ProductApiConnectionProvider::setConnectionProfileEndpoint(
    const Data::ControllerConnectionScope &scope,
    const Data::NodeId &profileId,
    const QString &endpoint)
{
    if (scope.projectId.isNull() || scope.masterId.isNull())
        return Utils::ResultError(Tr::tr("Select an EtherCAT project and master first."));
    if (profileId != defaultProfileId())
        return Utils::ResultError(Tr::tr("The selected controller profile is not available."));

    const Utils::Result<ProductApiSession::EndpointSet> endpoints
        = endpointsFromBaseEndpoint(endpoint);
    if (!endpoints)
        return Utils::ResultError(endpoints.error());
    const Utils::Result<> configured = m_session->setEndpoints(*endpoints);
    if (!configured)
        return configured;

    if (m_persistEndpoint) {
        Utils::userSettings().setValue(
            Constants::BASE_ENDPOINT_SETTINGS_KEY, endpoints->endpointSummary);
    }
    emit connectionProfilesChanged();
    return {};
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

bool ProductApiConnectionProvider::supportsControlCommand(
    Data::ControllerControlCommand command) const
{
    return isAvailable() && m_session->supportsControlCommand(command);
}

Utils::Result<> ProductApiConnectionProvider::executeControlCommand(
    const Data::ControllerControlRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->executeControlCommand(request);
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
