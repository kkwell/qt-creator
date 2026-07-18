// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"
#include "providers.h"

#include <QPointer>

namespace EtherCAT::Core {

class ETHERCATCORE_EXPORT ProviderRegistry final : public QObject
{
    Q_OBJECT

public:
    explicit ProviderRegistry(QObject *parent = nullptr);

    QList<Provider *> providers() const;
    QList<Provider *> providers(ProviderKind kind) const;
    Provider *provider(Utils::Id id) const;

signals:
    void providerAdded(EtherCAT::Core::Provider *provider);
    // The Provider is already unlinked from this registry but remains alive in the object pool.
    void providerAboutToBeRemoved(EtherCAT::Core::Provider *provider);

private:
    void handleObjectAdded(QObject *object);
    void handleObjectAboutToBeRemoved(QObject *object);
    void discardDestroyedProviders();

    QList<QPointer<Provider>> m_providers;
};

} // namespace EtherCAT::Core
