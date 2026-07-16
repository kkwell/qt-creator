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

void ProviderRegistry::handleObjectAdded(QObject *object)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    auto *provider = qobject_cast<Provider *>(object);
    if (!provider || this->provider(provider->id()))
        return;

    m_providers.append(provider);
    connect(provider, &QObject::destroyed, this, &ProviderRegistry::discardDestroyedProviders);
    emit providerAdded(provider);
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

        emit providerAboutToBeRemoved(provider);
        m_providers.removeAt(index);
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
