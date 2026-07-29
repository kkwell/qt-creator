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

static Utils::Result<ProductApiSession::EndpointSet> endpointsFromControllerAddress(
    const QString &input)
{
    const QString host = input.trimmed();
    if (host.isEmpty())
        return Utils::ResultError(Tr::tr("Enter a controller IP address."));
    if (host.contains(QLatin1Char(':'))) {
        return Utils::ResultError(
            Tr::tr("Enter only the IPv4 controller address. Ports are fixed automatically."));
    }

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

    const QString canonicalHost = address.toString();
    return ProductApiSession::EndpointSet{
        canonicalHost,
        15200,
        15201,
        15202,
        QStringLiteral("%1:15200").arg(canonicalHost),
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
    // Migrate the legacy editable base-port setting to the fixed product ports.
    const QString configuredHost = configured.section(QLatin1Char(':'), 0, 0);
    const Utils::Result<ProductApiSession::EndpointSet> parsed
        = endpointsFromControllerAddress(configuredHost);
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
    connect(
        m_session,
        &ProductApiSession::runtimeResourceCatalogChanged,
        this,
        &ProductApiConnectionProvider::runtimeResourceCatalogChanged);
    connect(
        m_session,
        &ProductApiSession::runtimeResourceSnapshotChanged,
        this,
        &ProductApiConnectionProvider::runtimeResourceSnapshotChanged);
    connect(
        m_session,
        &ProductApiSession::runtimeResourceSnapshotRequestFinished,
        this,
        &ProductApiConnectionProvider::runtimeResourceSnapshotRequestFinished);
    connect(
        m_session,
        &ProductApiSession::runtimeSemanticMappingAttestationChanged,
        this,
        &ProductApiConnectionProvider::runtimeSemanticMappingAttestationChanged);
    connect(
        m_session,
        &ProductApiSession::runtimeSemanticMappingAttestationRequestFinished,
        this,
        &ProductApiConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    connect(
        m_session,
        &ProductApiSession::runtimeOutputGroupPolicyRequestFinished,
        this,
        &ProductApiConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    connect(
        m_session,
        &ProductApiSession::runtimeOutputTransactionStateChanged,
        this,
        &ProductApiConnectionProvider::runtimeOutputTransactionStateChanged);
    connect(
        m_session,
        &ProductApiSession::runtimeOutputTransactionStateInvalidated,
        this,
        &ProductApiConnectionProvider::runtimeOutputTransactionStateInvalidated);
    connect(
        m_session,
        &ProductApiSession::runtimeOutputTransactionStateRequestFinished,
        this,
        &ProductApiConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    connect(
        m_session,
        &ProductApiSession::runtimeOutputTransactionFinished,
        this,
        &ProductApiConnectionProvider::runtimeOutputTransactionFinished);
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
    profile.displayName = Tr::tr("Embed Labs Product API");
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
    configuration.endpoint = snapshot.endpointSummary.section(QLatin1Char(':'), 0, 0);
    configuration.placeholder = ProductApiSession::EndpointSet::productionDefaults().host;
    configuration.editable
        = snapshot.state == Data::ControllerConnectionState::Disconnected
          || snapshot.state == Data::ControllerConnectionState::Failed;
    configuration.endpointLabel = Tr::tr("Controller IP:");
    configuration.endpointAccessibleName = Tr::tr("Controller IP address");
    configuration.endpointDescription = Tr::tr(
        "Enter only the IPv4 controller address. Control, Push, and Bulk always use ports "
        "15200, 15201, and 15202.");
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
        = endpointsFromControllerAddress(endpoint);
    if (!endpoints)
        return Utils::ResultError(endpoints.error());
    const Utils::Result<> configured = m_session->setEndpoints(*endpoints);
    if (!configured)
        return configured;

    if (m_persistEndpoint) {
        Utils::userSettings().setValue(
            Constants::BASE_ENDPOINT_SETTINGS_KEY, endpoints->host);
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

bool ProductApiConnectionProvider::supportsRuntimeResources() const
{
    return isAvailable() && m_session->supportsRuntimeResources();
}

std::optional<Data::RuntimeResourceCatalog> ProductApiConnectionProvider::runtimeResourceCatalog()
    const
{
    return m_session->runtimeResourceCatalog();
}

std::optional<Data::RuntimeResourceSnapshot> ProductApiConnectionProvider::runtimeResourceSnapshot()
    const
{
    return m_session->runtimeResourceSnapshot();
}

Utils::Result<> ProductApiConnectionProvider::refreshRuntimeResources()
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->refreshRuntimeResources();
}

Utils::Result<> ProductApiConnectionProvider::requestRuntimeResourceSnapshot(
    const Data::RuntimeResourceSnapshotRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->requestRuntimeResourceSnapshot(request);
}

bool ProductApiConnectionProvider::supportsRuntimeSemanticMappingAttestation() const
{
    return isAvailable() && m_session->supportsRuntimeSemanticMappingAttestation();
}

std::optional<Data::RuntimeSemanticMappingAttestation>
ProductApiConnectionProvider::runtimeSemanticMappingAttestation() const
{
    return m_session->runtimeSemanticMappingAttestation();
}

Utils::Result<> ProductApiConnectionProvider::requestRuntimeSemanticMappingAttestation(
    const Data::RuntimeSemanticMappingAttestationRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->requestRuntimeSemanticMappingAttestation(request);
}

bool ProductApiConnectionProvider::supportsRuntimeOutputTransactions() const
{
    return isAvailable() && m_session->supportsRuntimeOutputTransactions();
}

Utils::Result<> ProductApiConnectionProvider::requestRuntimeOutputGroupPolicy(
    const Data::RuntimeOutputGroupPolicyRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->requestRuntimeOutputGroupPolicy(request);
}

Utils::Result<> ProductApiConnectionProvider::requestRuntimeOutputTransactionState(
    const Data::RuntimeOutputTransactionStateRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->requestRuntimeOutputTransactionState(request);
}

Utils::Result<> ProductApiConnectionProvider::applyRuntimeOutputTransaction(
    const Data::RuntimeOutputTransactionRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->applyRuntimeOutputTransaction(request);
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

bool ProductApiConnectionProvider::supportsPackageDeployment() const
{
    return isAvailable() && m_session->supportsPackageDeployment();
}

Utils::Result<> ProductApiConnectionProvider::deployPackage(
    const Data::ControllerPackageDeploymentRequest &request)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->deployPackage(request);
}

Utils::Result<> ProductApiConnectionProvider::cancelPackageDeployment(const QString &operationId)
{
    if (!isAvailable())
        return Utils::ResultError(Tr::tr("The Embed Labs controller provider is unavailable."));
    return m_session->cancelPackageDeployment(operationId);
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
