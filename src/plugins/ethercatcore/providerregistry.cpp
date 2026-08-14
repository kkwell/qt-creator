// Copyright (C) 2026 Kvell

#include "providerregistry.h"

#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>

#include <QThread>

namespace EtherCAT::Core {

ProviderRegistry::ProviderRegistry(QObject *parent)
    : QObject(parent)
{
    auto *manager = ExtensionSystem::PluginManager::instance();
    QTC_ASSERT(manager, return);

    connect(
        manager,
        &ExtensionSystem::PluginManager::objectAdded,
        this,
        &ProviderRegistry::handleObjectAdded);
    connect(
        manager,
        &ExtensionSystem::PluginManager::aboutToRemoveObject,
        this,
        &ProviderRegistry::handleObjectAboutToBeRemoved);

    const QObjectList existingObjects = ExtensionSystem::PluginManager::allObjects();
    for (QObject *object : existingObjects)
        handleObjectAdded(object);
}

QList<Provider *> ProviderRegistry::providers() const
{
    QList<Provider *> result;
    result.reserve(m_providers.size());
    for (const QPointer<Provider> &provider : m_providers) {
        if (provider)
            result.append(provider.data());
    }
    return result;
}

QList<Provider *> ProviderRegistry::providers(ProviderKind kind) const
{
    QList<Provider *> result;
    for (Provider *candidate : providers()) {
        if (candidate->kind() == kind)
            result.append(candidate);
    }
    return result;
}

Provider *ProviderRegistry::provider(Utils::Id id) const
{
    for (Provider *candidate : providers()) {
        if (candidate->id() == id)
            return candidate;
    }
    return nullptr;
}

QList<ProviderStartupDiagnostic> ProviderRegistry::registrationDiagnostics() const
{
    return m_registrationDiagnostics;
}

void ProviderRegistry::handleObjectAdded(QObject *object)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    auto *provider = qobject_cast<Provider *>(object);
    if (!provider)
        return;

    if (Provider *registered = this->provider(provider->id())) {
        if (registered == provider)
            return;
        recordDuplicateProvider(registered, provider);
        return;
    }

    m_providers.append(provider);
    connect(provider, &QObject::destroyed, this, &ProviderRegistry::discardDestroyedProviders);
    emit providerAdded(provider);
}

void ProviderRegistry::recordDuplicateProvider(Provider *registered, Provider *rejected)
{
    QTC_ASSERT(registered && rejected, return);

    const Utils::Id rejectedId = rejected->id();
    if (m_reportedDuplicateIds.contains(rejectedId))
        return;

    ProviderStartupDiagnostic diagnostic;
    if (m_reportedDuplicateIds.size() >= MaximumRegistrationDiagnostics - 1) {
        if (m_registrationDiagnosticOverflowReported)
            return;
        m_registrationDiagnosticOverflowReported = true;
        diagnostic.code = Utils::Id("EtherCAT.ProviderRegistry.DiagnosticCapacityExceeded");
        diagnostic.message = tr("Additional duplicate provider IDs were rejected; only the first "
                                "%1 registration conflicts are retained.")
                                 .arg(MaximumRegistrationDiagnostics - 1);
    } else {
        m_reportedDuplicateIds.insert(rejectedId);
        diagnostic.code = Utils::Id("EtherCAT.ProviderRegistry.DuplicateId");
        diagnostic.message = tr("Provider ID \"%1\" is already registered by \"%2\"; \"%3\" "
                                "was rejected.")
                                 .arg(
                                     rejectedId.toString(),
                                     registered->displayName(),
                                     rejected->displayName());
    }
    diagnostic.severity = ProviderDiagnosticSeverity::Error;
    m_registrationDiagnostics.append(diagnostic);
    emit registrationDiagnosticAdded(diagnostic);
}

void ProviderRegistry::handleObjectAboutToBeRemoved(QObject *object)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    auto *provider = qobject_cast<Provider *>(object);
    if (!provider)
        return;

    for (qsizetype index = 0; index < m_providers.size(); ++index) {
        if (m_providers.at(index) != provider)
            continue;

        // Slots may remove other providers, so do not retain an index across the signal.
        m_providers.removeAt(index);
        emit providerAboutToBeRemoved(provider);
        return;
    }
}

void ProviderRegistry::discardDestroyedProviders()
{
    for (qsizetype index = m_providers.size() - 1; index >= 0; --index) {
        if (m_providers.at(index).isNull())
            m_providers.removeAt(index);
    }
}

} // namespace EtherCAT::Core
