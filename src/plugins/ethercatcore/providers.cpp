// Copyright (C) 2026 Kvell

#include "providers.h"

namespace EtherCAT::Core {

Provider::Provider(ProviderKind kind, Utils::Id id, const QString &displayName, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_id(id)
    , m_displayName(displayName)
{}

ProviderKind Provider::kind() const
{
    return m_kind;
}

Utils::Id Provider::id() const
{
    return m_id;
}

QString Provider::displayName() const
{
    return m_displayName;
}

bool Provider::isAvailable() const
{
    return m_available;
}

void Provider::setDisplayName(const QString &displayName)
{
    if (m_displayName == displayName)
        return;

    m_displayName = displayName;
    emit displayNameChanged(m_displayName);
}

void Provider::setAvailable(bool available)
{
    if (m_available == available)
        return;

    m_available = available;
    emit availabilityChanged(m_available);
}

ProjectService::ProjectService(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::Project, id, displayName, parent)
{}

DeviceRepositoryProvider::DeviceRepositoryProvider(
    Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::DeviceRepository, id, displayName, parent)
{}

PropertyPageProvider::PropertyPageProvider(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::PropertyPage, id, displayName, parent)
{}

ScanProvider::ScanProvider(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::Scan, id, displayName, parent)
{}

DiagnosticsProvider::DiagnosticsProvider(Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(ProviderKind::Diagnostics, id, displayName, parent)
{}

} // namespace EtherCAT::Core
