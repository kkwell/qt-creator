// Copyright (C) 2026 Embed Labs

#pragma once

#include "ecfgconfiguration_p.h"
#include "ecpkgcontainer.h"
#include "signedecpkgmanifest_p.h"

#include <utils/result.h>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace EtherCAT::SemanticRuntime::Internal {

constexpr qsizetype defaultMaximumSemanticCompileReportBytes = 8 * 1024 * 1024;
constexpr qsizetype defaultMaximumSemanticArtifactBytes = 8 * 1024 * 1024;

struct SemanticBindingTopologyInstance
{
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint32 vendorId = 0;
    quint32 productCode = 0;
    std::optional<quint32> revision;
    std::optional<quint32> serial;
    QString adapterId;
    QString adapterVersion;
    QByteArray adapterSha256;
    QByteArray esiSha256;
    QString pdoProfile;
    std::optional<QString> dcProfile;
};

struct VerifiedSemanticBinding
{
    QString semanticSignalId;
    quint64 resourceId = 0;
    quint64 componentInstanceId = 0;
    quint64 parentInstanceId = 0;
    quint32 instanceOrdinal = 0;
    quint32 consistencyGroupId = 0;
    EcfgResourcePrimitive primitive = EcfgResourcePrimitive::Bool;
    quint16 bitWidth = 0;
    EcfgResourceDirection direction = EcfgResourceDirection::Input;
    EcfgResourceAccess access = EcfgResourceAccess::Read;
    EcfgResourceSource source = EcfgResourceSource::PdoInput;
    quint32 processImageBitOffset = 0;
    quint16 processImageBitLength = 0;
    quint8 valueBytes = 0;
    quint8 safeValueBytes = 0;
    quint16 qualityMask = 0;
    QByteArray safeValueLittleEndian;

    QString adapterId;
    QString adapterVersion;
    QByteArray adapterSha256;
    QByteArray esiSha256;
    QString canonicalSymbol;
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint32 slot = 0;
};

struct VerifiedSemanticBindingArtifact
{
    EcpkgTrustClass trust = EcpkgTrustClass::Engineering;
    QByteArray packageSha256;
    QByteArray manifestSha256;
    QByteArray signingKeyIdSha256;
    QByteArray canonicalArtifact;
    QByteArray artifactSha256;

    quint64 configurationId = 0;
    quint64 buildTimestamp = 0;
    QByteArray capabilitySha256;
    QByteArray configurationSha256;
    QByteArray runtimeSha256;

    quint64 catalogRevision = 0;
    quint64 topologyIdentity = 0;
    QByteArray resourceRecordsSha256;
    QByteArray resourceSectionSha256;
    QByteArray topologySha256;

    QList<SemanticBindingTopologyInstance> topologyInstances;
    QList<VerifiedSemanticBinding> bindings;

    const VerifiedSemanticBinding *findBySemanticSignalId(QStringView semanticSignalId) const;
    const VerifiedSemanticBinding *findByResourceId(quint64 resourceId) const;
};

// All inputs must originate from one read of an ECPKG: the canonical container, its
// successfully verified signed manifest, and its strictly parsed configuration.ecfg.
// The returned bindings are explicit signed mappings. Provenance fields are retained
// for audit only and are never used to infer a ResourceId.
Utils::Result<VerifiedSemanticBindingArtifact> verifySemanticBindingArtifact(
    const EcpkgContainer &container,
    const VerifiedSignedEcpkgManifest &manifest,
    const EcfgConfiguration &configuration,
    qsizetype maximumCompileReportBytes = defaultMaximumSemanticCompileReportBytes,
    qsizetype maximumArtifactBytes = defaultMaximumSemanticArtifactBytes);

} // namespace EtherCAT::SemanticRuntime::Internal
