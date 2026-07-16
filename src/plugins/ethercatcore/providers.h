// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <utils/id.h>

#include <QObject>

namespace EtherCAT::Core {

enum class ProviderKind { Project, DeviceRepository, PropertyPage, Scan, Diagnostics };

class ETHERCATCORE_EXPORT Provider : public QObject
{
    Q_OBJECT

public:
    Provider(ProviderKind kind, Utils::Id id, const QString &displayName, QObject *parent = nullptr);

    ProviderKind kind() const;
    Utils::Id id() const;
    QString displayName() const;
    bool isAvailable() const;

    void setDisplayName(const QString &displayName);
    void setAvailable(bool available);

signals:
    void displayNameChanged(const QString &displayName);
    void availabilityChanged(bool available);

private:
    const ProviderKind m_kind;
    const Utils::Id m_id;
    QString m_displayName;
    bool m_available = false;
};

class ETHERCATCORE_EXPORT ProjectService : public Provider
{
    Q_OBJECT

public:
    ProjectService(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

class ETHERCATCORE_EXPORT DeviceRepositoryProvider : public Provider
{
    Q_OBJECT

public:
    DeviceRepositoryProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

class ETHERCATCORE_EXPORT PropertyPageProvider : public Provider
{
    Q_OBJECT

public:
    PropertyPageProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

class ETHERCATCORE_EXPORT ScanProvider : public Provider
{
    Q_OBJECT

public:
    ScanProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

class ETHERCATCORE_EXPORT DiagnosticsProvider : public Provider
{
    Q_OBJECT

public:
    DiagnosticsProvider(Utils::Id id, const QString &displayName, QObject *parent = nullptr);
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::ProviderKind)
