// Copyright (C) 2026 Kvell

#pragma once

#include "controllerconnection.h"
#include "ethercatdata_global.h"
#include "runtimeresource.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <optional>

namespace EtherCAT::Data {

enum class RuntimeSemanticMappingTrust {
    Unknown,
    Production,
    Engineering,
};

inline bool isValidRuntimeSemanticMappingDigest(QByteArrayView digest)
{
    if (digest.size() != 32)
        return false;

    quint8 aggregate = 0;
    for (qsizetype index = 0; index < 32; ++index)
        aggregate |= quint8(digest.at(index));
    return aggregate != 0;
}

// Digest equality is deliberately independent of the first differing byte. Sizes are public
// structural data; valid proofs always contain exactly 32 bytes per digest.
inline bool runtimeSemanticMappingDigestsEqual(QByteArrayView left, QByteArrayView right)
{
    quint8 difference = 0;
    for (qsizetype index = 0; index < 32; ++index) {
        const quint8 leftByte = index < left.size() ? quint8(left.at(index)) : 0;
        const quint8 rightByte = index < right.size() ? quint8(right.at(index)) : 0;
        difference |= leftByte ^ rightByte;
    }
    return left.size() == 32 && right.size() == 32 && difference == 0;
}

struct ETHERCATDATA_EXPORT RuntimeSemanticMappingProof
{
    quint16 formatVersion = 0;
    quint32 bindingCount = 0;
    bool packageSigned = false;
    bool signatureVerified = false;
    bool semanticBindingVerified = false;
    RuntimeSemanticMappingTrust trust = RuntimeSemanticMappingTrust::Unknown;
    QByteArray packageSha256;
    QByteArray manifestSha256;
    QByteArray mappingSha256;
    QByteArray resourceRecordsSha256;
    QByteArray resourceSectionSha256;
    QByteArray topologySha256;
    QByteArray signingKeyIdSha256;

    bool isValid() const
    {
        return (formatVersion == 1 || formatVersion == 2) && bindingCount && packageSigned
               && signatureVerified
               && semanticBindingVerified
               && (trust == RuntimeSemanticMappingTrust::Production
                   || trust == RuntimeSemanticMappingTrust::Engineering)
               && isValidRuntimeSemanticMappingDigest(packageSha256)
               && isValidRuntimeSemanticMappingDigest(manifestSha256)
               && isValidRuntimeSemanticMappingDigest(mappingSha256)
               && isValidRuntimeSemanticMappingDigest(resourceRecordsSha256)
               && isValidRuntimeSemanticMappingDigest(resourceSectionSha256)
               && isValidRuntimeSemanticMappingDigest(topologySha256)
               && isValidRuntimeSemanticMappingDigest(signingKeyIdSha256);
    }

    friend bool operator==(
        const RuntimeSemanticMappingProof &left, const RuntimeSemanticMappingProof &right)
    {
        bool digestsEqual = true;
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.packageSha256, right.packageSha256);
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.manifestSha256, right.manifestSha256);
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.mappingSha256, right.mappingSha256);
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.resourceRecordsSha256, right.resourceRecordsSha256);
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.resourceSectionSha256, right.resourceSectionSha256);
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.topologySha256, right.topologySha256);
        digestsEqual &= runtimeSemanticMappingDigestsEqual(
            left.signingKeyIdSha256, right.signingKeyIdSha256);

        const bool metadataEqual
            = left.formatVersion == right.formatVersion
              && left.bindingCount == right.bindingCount
              && left.packageSigned == right.packageSigned
              && left.signatureVerified == right.signatureVerified
              && left.semanticBindingVerified == right.semanticBindingVerified
              && left.trust == right.trust;
        return metadataEqual && digestsEqual;
    }
};

inline bool isCompleteRuntimeSemanticMappingEpoch(const RuntimeResourceCatalogEpoch &epoch)
{
    return epoch.controllerBootId
           && (epoch.activePackageSlot == ControllerSlot::A
               || epoch.activePackageSlot == ControllerSlot::B)
           && epoch.activePackageGeneration && epoch.configurationId && epoch.topologyGeneration
           && epoch.runtimeGeneration && epoch.catalogRevision && !epoch.topologyIdentity.isEmpty();
}

inline bool isValidRuntimeSemanticMappingCorrelationId(const QString &correlationId)
{
    if (correlationId.isEmpty() || correlationId.size() > 128
        || correlationId != correlationId.trimmed()) {
        return false;
    }
    for (const QChar character : correlationId) {
        if (character.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

struct ETHERCATDATA_EXPORT RuntimeSemanticMappingAttestationRequest
{
    QString correlationId;
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch expectedEpoch;
    // This proof comes only from the IDE's independent verification of the exact signed package.
    // Providers compare it with the controller response; it is never serialized onto the wire.
    RuntimeSemanticMappingProof expectedProof;

    bool isValid() const
    {
        return isValidRuntimeSemanticMappingCorrelationId(correlationId)
               && !scope.projectId.isNull() && !scope.masterId.isNull() && sessionGeneration
               && isCompleteRuntimeSemanticMappingEpoch(expectedEpoch) && expectedProof.isValid();
    }

    friend bool operator==(
        const RuntimeSemanticMappingAttestationRequest &,
        const RuntimeSemanticMappingAttestationRequest &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeSemanticMappingAttestation
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    RuntimeResourceCatalogEpoch epoch;
    RuntimeSemanticMappingProof proof;
    QDateTime receivedAt;

    bool isValid() const
    {
        return !scope.projectId.isNull() && !scope.masterId.isNull() && sessionGeneration
               && isCompleteRuntimeSemanticMappingEpoch(epoch) && proof.isValid()
               && receivedAt.isValid();
    }

    friend bool operator==(
        const RuntimeSemanticMappingAttestation &,
        const RuntimeSemanticMappingAttestation &) = default;
};

struct ETHERCATDATA_EXPORT RuntimeSemanticMappingAttestationResult
{
    RuntimeSemanticMappingAttestationRequest request;
    std::optional<RuntimeSemanticMappingAttestation> attestation;
    std::optional<ControllerOperationError> error;

    bool isValid() const
    {
        if (!request.isValid() || attestation.has_value() == error.has_value())
            return false;
        if (error) {
            return error->operation
                   == ControllerOperation::QueryRuntimeSemanticMappingAttestation;
        }
        return attestation->isValid() && attestation->scope == request.scope
               && attestation->sessionGeneration == request.sessionGeneration
               && attestation->epoch == request.expectedEpoch
               && attestation->proof == request.expectedProof;
    }

    friend bool operator==(
        const RuntimeSemanticMappingAttestationResult &,
        const RuntimeSemanticMappingAttestationResult &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeSemanticMappingTrust)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeSemanticMappingProof)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeSemanticMappingAttestationRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeSemanticMappingAttestation)
Q_DECLARE_METATYPE(EtherCAT::Data::RuntimeSemanticMappingAttestationResult)
