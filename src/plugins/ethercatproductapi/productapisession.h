// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/runtimeoutputtransaction.h>
#include <ethercatdata/runtimeresource.h>
#include <ethercatdata/semanticmappingattestation.h>

#include <utils/result.h>

#include <QObject>
#include <QString>

#include <memory>

namespace EtherCAT::ProductApi::Internal {

class ProductApiSessionPrivate;

class ProductApiSession final : public QObject
{
    Q_OBJECT

public:
    struct EndpointSet
    {
        QString host;
        quint16 controlPort = 0;
        quint16 pushPort = 0;
        quint16 bulkPort = 0;
        QString endpointSummary;

        static EndpointSet productionDefaults();
        bool isValid() const;

        friend bool operator==(const EndpointSet &, const EndpointSet &) = default;
    };

    struct Options
    {
        int connectTimeoutMs = 5000;
        int handshakeTimeoutMs = 5000;
        int requestTimeoutMs = 5000;
        int reconnectInitialDelayMs = 250;
        int reconnectMaximumDelayMs = 5000;
        int reconnectAttempts = 5;
        int liveStatePollIntervalMs = 500;
        int runtimeResourceRefreshTimeoutMs = 30000;
        quint32 maximumRuntimeResourceCount = 65536;

        bool isValid() const;

        friend bool operator==(const Options &, const Options &) = default;
    };

    explicit ProductApiSession(QObject *parent = nullptr);
    ProductApiSession(
        const EndpointSet &endpoints, const Options &options, QObject *parent = nullptr);
    ~ProductApiSession() override;

    Data::ControllerConnectionSnapshot snapshot() const;
    Utils::Result<> setEndpoints(const EndpointSet &endpoints);

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &request);
    Utils::Result<> disconnectFromController();
    Utils::Result<> refreshController();
    bool supportsRuntimeResources() const;
    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const;
    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const;
    Utils::Result<> refreshRuntimeResources();
    Utils::Result<> requestRuntimeResourceSnapshot(
        const Data::RuntimeResourceSnapshotRequest &request);
    bool supportsRuntimeSemanticMappingAttestation() const;
    std::optional<Data::RuntimeSemanticMappingAttestation>
    runtimeSemanticMappingAttestation() const;
    Utils::Result<> requestRuntimeSemanticMappingAttestation(
        const Data::RuntimeSemanticMappingAttestationRequest &request);
    bool supportsRuntimeOutputTransactions() const;
    Utils::Result<> requestRuntimeOutputGroupPolicy(
        const Data::RuntimeOutputGroupPolicyRequest &request);
    Utils::Result<> requestRuntimeOutputTransactionState(
        const Data::RuntimeOutputTransactionStateRequest &request);
    Utils::Result<> applyRuntimeOutputTransaction(
        const Data::RuntimeOutputTransactionRequest &request);
    bool supportsControlCommand(Data::ControllerControlCommand command) const;
    Utils::Result<> executeControlCommand(const Data::ControllerControlRequest &request);
    bool supportsPackageDeployment() const;
    Utils::Result<> deployPackage(const Data::ControllerPackageDeploymentRequest &request);
    Utils::Result<> cancelPackageDeployment(const QString &operationId);
    void shutdown();

#ifdef WITH_TESTS
    EndpointSet endpointsForTests() const;
    bool isIdleForTests() const;
    bool refreshInProgressForTests() const;
    int activeSocketCountForTests() const;
    int pendingRequestCountForTests() const;
    int ignoredRuntimeResourceRequestCountForTests() const;
    quint16 negotiatedMinorForTests() const;
    quint32 controlFeatureBitsForTests() const;
    quint32 pushFeatureBitsForTests() const;
    quint32 bulkFeatureBitsForTests() const;
    void failNextWriteForTests();
#endif

signals:
    void snapshotChanged();
    void runtimeResourceCatalogChanged();
    void runtimeResourceSnapshotChanged();
    void runtimeResourceSnapshotRequestFinished(
        const EtherCAT::Data::RuntimeResourceSnapshotResult &result);
    void runtimeSemanticMappingAttestationChanged();
    void runtimeSemanticMappingAttestationRequestFinished(
        const EtherCAT::Data::RuntimeSemanticMappingAttestationResult &result);
    void runtimeOutputGroupPolicyRequestFinished(
        const EtherCAT::Data::RuntimeOutputGroupPolicyResult &result);
    void runtimeOutputTransactionStateChanged(
        const EtherCAT::Data::RuntimeOutputTransactionState &state);
    void runtimeOutputTransactionStateInvalidated();
    void runtimeOutputTransactionStateRequestFinished(
        const EtherCAT::Data::RuntimeOutputTransactionStateResult &result);
    void runtimeOutputTransactionFinished(
        const EtherCAT::Data::RuntimeOutputTransactionResult &result);

private:
    std::unique_ptr<ProductApiSessionPrivate> d;
};

} // namespace EtherCAT::ProductApi::Internal
