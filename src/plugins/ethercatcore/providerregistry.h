// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"
#include "providers.h"

#include <QPointer>
#include <QSet>

namespace EtherCAT::Core {

namespace Internal {
class EtherCATCoreTests;
}

class ETHERCATCORE_EXPORT ProviderRegistry final : public QObject
{
    Q_OBJECT

public:
    explicit ProviderRegistry(QObject *parent = nullptr);

    QList<Provider *> providers() const;
    QList<Provider *> providers(ProviderKind kind) const;
    Provider *provider(Utils::Id id) const;
    QList<ProviderStartupDiagnostic> registrationDiagnostics() const;

signals:
    void providerAdded(EtherCAT::Core::Provider *provider);
    // The Provider is already unlinked from this registry but remains alive in the object pool.
    void providerAboutToBeRemoved(EtherCAT::Core::Provider *provider);
    void registrationDiagnosticAdded(const EtherCAT::Core::ProviderStartupDiagnostic &diagnostic);

private:
    friend class Internal::EtherCATCoreTests;

    static constexpr qsizetype MaximumRegistrationDiagnostics = 64;

    void handleObjectAdded(QObject *object);
    void handleObjectAboutToBeRemoved(QObject *object);
    void discardDestroyedProviders();
    void recordDuplicateProvider(Provider *registered, Provider *rejected);

    QList<QPointer<Provider>> m_providers;
    QList<ProviderStartupDiagnostic> m_registrationDiagnostics;
    QSet<Utils::Id> m_reportedDuplicateIds;
    bool m_registrationDiagnosticOverflowReported = false;
};

} // namespace EtherCAT::Core
