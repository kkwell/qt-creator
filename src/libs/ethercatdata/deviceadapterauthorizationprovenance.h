// Copyright (C) 2026 Embed Labs

#pragma once

#include "deviceadapter.h"
#include "ethercatdata_global.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Data {

enum class DeviceAdapterAuthorizationVersion : quint8 {
    None = 0,
    V1 = 1,
    V2 = 2,
};

enum class DeviceAdapterAuthorizationDecision : quint8 {
    None = 0,
    Allow = 1,
};

enum class DeviceAdapterAuthorizationProvenanceState : quint8 {
    NotInstalled = 0,
    Authorized = 1,
    Denied = 2,
    ValidationFailed = 3,
};

struct ETHERCATDATA_EXPORT CanonicalDeviceAdapterAuthorizationBinding
{
    DeviceAdapterAuthorizationVersion authorizationVersion
        = DeviceAdapterAuthorizationVersion::None;
    QByteArray exactBytes;
    QByteArray sha256;

    bool isValid() const;

    friend bool operator==(
        const CanonicalDeviceAdapterAuthorizationBinding &,
        const CanonicalDeviceAdapterAuthorizationBinding &) = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterAuthorizationProvenance
{
    DeviceAdapterContractVersion adapterContractVersion = DeviceAdapterContractVersion::Unknown;
    DeviceAdapterAuthorizationVersion authorizationVersion
        = DeviceAdapterAuthorizationVersion::None;
    DeviceAdapterAuthorizationDecision decision = DeviceAdapterAuthorizationDecision::None;
    DeviceAdapterId adapterId;
    QString adapterVersion;
    QByteArray adapterContentSha256;
    QByteArray adapterBindingSha256;
    QString authorizationId;
    QByteArray authorizationDocumentSha256;
    QByteArray authorizationSignatureSha256;
    QString policyId;
    quint32 policyRevision = 0;
    QByteArray policyDocumentSha256;
    QByteArray policySignatureSha256;
    QByteArray rootKeyId;
    QByteArray signerKeyId;

    bool isValid() const;

    friend bool operator==(
        const DeviceAdapterAuthorizationProvenance &,
        const DeviceAdapterAuthorizationProvenance &) = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterAuthorizationProvenanceSnapshot
{
    quint16 formatVersion = 0;
    quint64 generation = 0;
    DeviceAdapterAuthorizationProvenanceState state
        = DeviceAdapterAuthorizationProvenanceState::NotInstalled;
    QByteArray authorizationSetSha256;
    QList<DeviceAdapterAuthorizationProvenance> records;

    bool isValid() const;

    friend bool operator==(
        const DeviceAdapterAuthorizationProvenanceSnapshot &,
        const DeviceAdapterAuthorizationProvenanceSnapshot &) = default;
};

// Reproduces the signed authorization binding from the manifest's declared identity and digest
// fields. It does not reopen the Adapter package or independently recompute those digests.
ETHERCATDATA_EXPORT std::optional<CanonicalDeviceAdapterAuthorizationBinding>
canonicalDeviceAdapterAuthorizationBinding(
    const DeviceAdapterManifest &manifest,
    DeviceAdapterAuthorizationVersion authorizationVersion);

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterAuthorizationVersion)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterAuthorizationDecision)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterAuthorizationProvenanceState)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterAuthorizationProvenance)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterAuthorizationProvenanceSnapshot)
