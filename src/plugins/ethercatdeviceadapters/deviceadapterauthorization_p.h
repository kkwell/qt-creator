// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/deviceadapterauthorizationprovenance.h>

#include <utils/filepath.h>

#include <QHash>
#include <QStringList>

namespace EtherCAT::DeviceAdapters::Internal {

struct DeviceAdapterAuthorizationRoots
{
    Utils::FilePath authorizationRoot;
    Utils::FilePath trustRoot;
};

struct AcceptedDeviceAdapterPolicy
{
    quint32 revision = 0;
    QByteArray canonicalSha256;
    QByteArray rootKeyId;
};

struct DeviceAdapterAuthorizationEvaluation
{
    QByteArray authorizationSetSha256;
    QList<Data::DeviceAdapterAuthorizationProvenance> provenances;
    bool materialIdentityComplete = false;
};

// Authorization is deliberately projected after parsing the immutable adapter
// manifest. The signed documents can only raise the two runtime trust flags;
// they never replace adapter content or become part of its content digest.
void applyDeviceAdapterAuthorizations(
    const DeviceAdapterAuthorizationRoots &roots,
    QList<Data::DeviceAdapterManifest> *manifests,
    QHash<QString, AcceptedDeviceAdapterPolicy> *acceptedPolicies,
    QStringList *diagnostics,
    DeviceAdapterAuthorizationEvaluation *evaluation);

} // namespace EtherCAT::DeviceAdapters::Internal
