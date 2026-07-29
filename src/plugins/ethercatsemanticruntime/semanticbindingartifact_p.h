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
#include <variant>

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
    // For format 1 this is the signed semantic_signal_id. For format 2 it is the
    // project-instance semantic_binding_id and remains the canonical lookup key.
    QString semanticSignalId;
    QString semanticSignalDefinitionId;
    QString semanticBindingId;
    QString projectDeviceId;
    QString componentBindingId;
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
    std::optional<QString> unit;
    qint64 scaleNumerator = 1;
    qint64 scaleDenominator = 1;
    qint64 scaleOffset = 0;
    bool safeValueDeclared = false;
    std::optional<std::variant<qint64, quint64>> safeValue;

    QString adapterId;
    QString adapterVersion;
    QByteArray adapterSha256;
    QByteArray esiSha256;
    QString canonicalSymbol;
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint32 slot = 0;
};

struct VerifiedSemanticComponent
{
    QString componentBindingId;
    quint64 componentInstanceId = 0;
    std::optional<QString> parentComponentBindingId;
    quint64 parentInstanceId = 0;
    quint32 slot = 0;
};

struct VerifiedSemanticDevice
{
    QString projectDeviceId;
    quint16 position = 0;
    quint16 stationAddress = 0;
    QString adapterId;
    QString adapterVersion;
    QByteArray adapterSha256;
    QByteArray esiSha256;
    QList<VerifiedSemanticComponent> components;
};

// Parser invariants exposed from this private header so they can be exercised
// with hermetic mutation tests.
bool isAcyclicSemanticComponentParentGraph(
    const QList<VerifiedSemanticComponent> &components);
bool isValidSemanticMaskedWaitCondition(quint64 mask, quint64 value, quint16 bitWidth);

enum class VerifiedSemanticActionQualification {
    Qualified,
    Unqualified,
};

struct VerifiedSemanticActionBindingReference
{
    QString semanticSignalDefinitionId;
    QString semanticBindingId;
    quint64 resourceId = 0;
    QString componentBindingId;
    quint32 consistencyGroupId = 0;
    EcfgResourcePrimitive primitive = EcfgResourcePrimitive::Bool;
    quint16 bitWidth = 0;
    EcfgResourceDirection direction = EcfgResourceDirection::Input;
    EcfgResourceAccess access = EcfgResourceAccess::Read;
    std::optional<QString> unit;
    qint64 scaleNumerator = 1;
    qint64 scaleDenominator = 1;
    qint64 scaleOffset = 0;
};

struct VerifiedSemanticActionParameter
{
    QString parameterId;
    EcfgResourcePrimitive primitive = EcfgResourcePrimitive::Bool;
    std::optional<QString> unit;
    qint64 minimum = 0;
    qint64 maximum = 0;
};

struct VerifiedSemanticActionGroup
{
    quint32 consistencyGroupId = 0;
    EcfgOutputRecoveryPolicy recoveryPolicy = EcfgOutputRecoveryPolicy::ReturnTask;
    quint32 maximumTtlCycles = 0;
};

struct VerifiedSemanticActionAssignment
{
    QString semanticBindingId;
    std::optional<std::variant<qint64, quint64>> constantValue;
    std::optional<QString> parameterId;
};

enum class VerifiedSemanticActionStepKind {
    WriteGroup,
    WaitMasked,
    WaitAbsoluteLimit,
};

struct VerifiedSemanticActionStep
{
    VerifiedSemanticActionStepKind kind = VerifiedSemanticActionStepKind::WriteGroup;
    quint32 consistencyGroupId = 0;
    QList<VerifiedSemanticActionAssignment> assignments;
    QString semanticBindingId;
    quint64 mask = 0;
    quint64 value = 0;
    quint64 absoluteLimit = 0;
    quint32 timeoutCycles = 0;
};

struct VerifiedSemanticAction
{
    QString actionDefinitionId;
    QByteArray actionDefinitionSha256;
    QString actionBindingId;
    QString projectDeviceId;
    QStringList componentBindingIds;
    QString adapterActionKey;
    bool enabled = false;
    VerifiedSemanticActionQualification qualification
        = VerifiedSemanticActionQualification::Unqualified;
    std::optional<QString> disabledReason;
    bool dcRequired = false;
    QString adapterId;
    QString adapterVersion;
    QByteArray adapterSha256;
    QByteArray esiSha256;
    QList<VerifiedSemanticActionBindingReference> requiredBindings;
    QList<VerifiedSemanticActionBindingReference> optionalBindings;
    QList<VerifiedSemanticActionParameter> parameters;
    QList<VerifiedSemanticActionGroup> consistencyGroups;
    QList<VerifiedSemanticActionStep> steps;
};

struct VerifiedSemanticBindingArtifact
{
    quint16 formatVersion = 0;
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
    QList<VerifiedSemanticDevice> devices;
    QList<VerifiedSemanticBinding> bindings;
    QList<VerifiedSemanticAction> actions;

    const VerifiedSemanticBinding *findBySemanticSignalId(QStringView semanticSignalId) const;
    const VerifiedSemanticBinding *findByResourceId(quint64 resourceId) const;
    const VerifiedSemanticDevice *findDevice(QStringView projectDeviceId) const;
    const VerifiedSemanticAction *findAction(QStringView actionBindingId) const;
    bool permitsWritableActions() const;
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
