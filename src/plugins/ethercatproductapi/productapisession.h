// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/controllerconnection.h>

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
    bool supportsControlCommand(Data::ControllerControlCommand command) const;
    Utils::Result<> executeControlCommand(const Data::ControllerControlRequest &request);
    void shutdown();

#ifdef WITH_TESTS
    EndpointSet endpointsForTests() const;
    bool isIdleForTests() const;
    int activeSocketCountForTests() const;
    int pendingRequestCountForTests() const;
    void failNextWriteForTests();
#endif

signals:
    void snapshotChanged();

private:
    std::unique_ptr<ProductApiSessionPrivate> d;
};

} // namespace EtherCAT::ProductApi::Internal
