// Copyright (C) 2026 Embed Labs

#include "runtimepackageevidence_p.h"

#include <QCryptographicHash>
#include <QtEndian>

#include <limits>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

Utils::ResultError evidenceError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Runtime package evidence error: %1").arg(detail));
}

bool validDigest(QByteArrayView digest)
{
    return Data::isValidRuntimeSemanticMappingDigest(digest);
}

bool sameDigest(QByteArrayView left, QByteArrayView right)
{
    return Data::runtimeSemanticMappingDigestsEqual(left, right);
}

QByteArray opaqueBigEndian(quint64 value)
{
    QByteArray bytes(qsizetype(sizeof(value)), '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

bool artifactIdentityIsValid(const VerifiedSemanticBindingArtifact &artifact)
{
    return (artifact.formatVersion == 1 || artifact.formatVersion == 2)
           && artifact.trust == EcpkgTrustClass::Production && artifact.configurationId
           && artifact.catalogRevision && artifact.topologyIdentity
           && !artifact.bindings.isEmpty()
           && artifact.bindings.size() <= std::numeric_limits<quint32>::max()
           && validDigest(artifact.packageSha256) && validDigest(artifact.manifestSha256)
           && validDigest(artifact.signingKeyIdSha256)
           && validDigest(artifact.artifactSha256)
           && validDigest(artifact.capabilitySha256)
           && validDigest(artifact.configurationSha256)
           && validDigest(artifact.runtimeSha256)
           && validDigest(artifact.resourceRecordsSha256)
           && validDigest(artifact.resourceSectionSha256)
           && validDigest(artifact.topologySha256) && !artifact.canonicalArtifact.isEmpty()
           && sameDigest(
               QCryptographicHash::hash(
                   artifact.canonicalArtifact, QCryptographicHash::Sha256),
               artifact.artifactSha256);
}

Utils::Result<> validateEvidence(const VerifiedRuntimePackageEvidence &evidence)
{
    const VerifiedSemanticBindingArtifact &artifact = evidence.semanticBindingArtifact();
    const Data::RuntimeSemanticMappingProof &proof = evidence.semanticMappingProof();
    if (!artifactIdentityIsValid(artifact))
        return evidenceError(QString::fromLatin1("the local semantic artifact is incomplete"));
    if (!validDigest(evidence.projectConfigurationSha256())) {
        return evidenceError(
            QString::fromLatin1("the compiled project identity is incomplete"));
    }
    if (!evidence.cyclePeriodNs()) {
        return evidenceError(
            QString::fromLatin1("the signed runtime cycle period is incomplete"));
    }
    if (!proof.isValid() || proof.trust != Data::RuntimeSemanticMappingTrust::Production) {
        return evidenceError(
            QString::fromLatin1("the local semantic mapping proof is not production trusted"));
    }
    if (proof.formatVersion != artifact.formatVersion
        || proof.bindingCount != quint32(artifact.bindings.size())
        || !sameDigest(proof.packageSha256, artifact.packageSha256)
        || !sameDigest(proof.manifestSha256, artifact.manifestSha256)
        || !sameDigest(proof.mappingSha256, artifact.artifactSha256)
        || !sameDigest(proof.resourceRecordsSha256, artifact.resourceRecordsSha256)
        || !sameDigest(proof.resourceSectionSha256, artifact.resourceSectionSha256)
        || !sameDigest(proof.topologySha256, artifact.topologySha256)
        || !sameDigest(proof.signingKeyIdSha256, artifact.signingKeyIdSha256)) {
        return evidenceError(
            QString::fromLatin1("the local proof differs from its verified semantic artifact"));
    }
    if (evidence.actionDefinitions()) {
        const VerifiedSemanticActionDefinitions &definitions
            = *evidence.actionDefinitions();
        if (!definitions.isValid()
            || definitions.semanticBindingArtifactSha256 != artifact.artifactSha256
            || definitions.definitionCount == 0
            || definitions.actionCount != quint32(artifact.actions.size())) {
            return evidenceError(
                QString::fromLatin1(
                    "the verified action definitions differ from the semantic artifact"));
        }
    }
    return Utils::ResultOk;
}

Utils::Result<> validatePackageAndArtifact(
    const VerifiedEcpkgPackage &package,
    const VerifiedSemanticBindingArtifact &artifact)
{
    if (package.manifest.trust != EcpkgTrustClass::Production
        || artifact.trust != EcpkgTrustClass::Production) {
        return evidenceError(
            QString::fromLatin1("only production-trusted packages are accepted"));
    }
    if (package.packageBytes.isEmpty()
        || package.packageBytes.size() > defaultMaximumEcpkgContainerBytes) {
        return evidenceError(QString::fromLatin1("the verified package bytes are unavailable"));
    }
    if (!package.manifest.compiledProjectSource.bytes
        || package.manifest.compiledProjectSource.bytes
               > quint32(defaultMaximumCompiledProjectBytes)
        || !validDigest(package.manifest.compiledProjectSource.sha256)) {
        return evidenceError(
            QString::fromLatin1("the signed compiled project identity is incomplete"));
    }
    const QByteArray packageSha256
        = QCryptographicHash::hash(package.packageBytes, QCryptographicHash::Sha256);
    if (!sameDigest(packageSha256, package.container.packageSha256)
        || !sameDigest(packageSha256, package.manifest.packageSha256)
        || !sameDigest(packageSha256, artifact.packageSha256)) {
        return evidenceError(
            QString::fromLatin1("the package bytes and verified package identities differ"));
    }

    if (!package.manifest.semanticBinding) {
        return evidenceError(
            QString::fromLatin1("the signed package has no semantic binding summary"));
    }
    const SignedEcpkgSemanticBindingSummary &summary = *package.manifest.semanticBinding;
    if (!summary.formatVersion || summary.formatVersion != artifact.formatVersion
        || !summary.bindingCount
        || summary.bindingCount != quint32(artifact.bindings.size())
        || summary.bindingCount != quint32(package.configuration.resources.size())
        || package.manifest.configurationId != package.configuration.configurationId
        || artifact.configurationId != package.configuration.configurationId
        || artifact.buildTimestamp != package.configuration.buildTimestamp
        || artifact.catalogRevision != package.configuration.catalogRevision
        || artifact.topologyIdentity != package.configuration.topologyIdentity
        || summary.catalogRevision != artifact.catalogRevision
        || summary.topologyIdentity != artifact.topologyIdentity
        || !sameDigest(artifact.manifestSha256, package.manifest.manifestSha256)
        || !sameDigest(artifact.signingKeyIdSha256, package.manifest.signingKeyIdSha256)
        || !sameDigest(artifact.capabilitySha256, package.manifest.capability.sha256)
        || !sameDigest(
            artifact.configurationSha256, package.manifest.configuration.sha256)
        || !sameDigest(artifact.runtimeSha256, package.manifest.runtime.sha256)
        || !sameDigest(
            artifact.configurationSha256, package.configuration.configurationSha256)
        || !sameDigest(
            artifact.capabilitySha256, package.configuration.capabilitySha256)
        || !sameDigest(artifact.artifactSha256, summary.mappingSha256)
        || !sameDigest(
            artifact.resourceRecordsSha256, summary.resourceRecordsSha256)
        || !sameDigest(
            artifact.resourceSectionSha256, summary.resourceSectionSha256)
        || !sameDigest(artifact.topologySha256, summary.topologySha256)
        || !sameDigest(
            artifact.resourceRecordsSha256,
            package.configuration.resourceRecordsSha256)
        || !sameDigest(
            artifact.resourceSectionSha256,
            package.configuration.resourceTableSectionSha256)) {
        return evidenceError(
            QString::fromLatin1(
                "the signed package, ECFG, and semantic artifact identities differ"));
    }
    return Utils::ResultOk;
}

} // namespace

bool VerifiedRuntimePackageEvidence::isValid() const
{
    return bool(validateEvidence(*this));
}

bool VerifiedRuntimePackageEvidence::permitsWritableActions() const
{
    return isValid() && m_semanticMappingProof.formatVersion == 2
           && m_semanticMappingProof.trust == Data::RuntimeSemanticMappingTrust::Production
           && m_actionDefinitions
           && m_semanticBindingArtifact.permitsWritableActions();
}

const VerifiedSemanticAction *VerifiedRuntimePackageEvidence::invocableAction(
    QStringView actionBindingId, bool dcRuntimeActive) const
{
    if (!permitsWritableActions())
        return nullptr;
    const VerifiedSemanticAction *action
        = m_semanticBindingArtifact.findAction(actionBindingId);
    if (!action || !action->enabled
        || action->qualification != VerifiedSemanticActionQualification::Qualified
        || action->disabledReason || (action->dcRequired && !dcRuntimeActive)) {
        return nullptr;
    }
    return action;
}

VerifiedRuntimePackageEvidence::VerifiedRuntimePackageEvidence(
    VerifiedSemanticBindingArtifact artifact,
    Data::RuntimeSemanticMappingProof proof,
    QByteArray projectConfigurationSha256,
    std::optional<VerifiedSemanticActionDefinitions> actionDefinitions,
    quint32 cyclePeriodNs)
    : m_semanticBindingArtifact(std::move(artifact))
    , m_semanticMappingProof(std::move(proof))
    , m_projectConfigurationSha256(std::move(projectConfigurationSha256))
    , m_actionDefinitions(std::move(actionDefinitions))
    , m_cyclePeriodNs(cyclePeriodNs)
{}

const VerifiedSemanticBindingArtifact &
VerifiedRuntimePackageEvidence::semanticBindingArtifact() const
{
    return m_semanticBindingArtifact;
}

const Data::RuntimeSemanticMappingProof &
VerifiedRuntimePackageEvidence::semanticMappingProof() const
{
    return m_semanticMappingProof;
}

const QByteArray &VerifiedRuntimePackageEvidence::projectConfigurationSha256() const
{
    return m_projectConfigurationSha256;
}

const std::optional<VerifiedSemanticActionDefinitions> &
VerifiedRuntimePackageEvidence::actionDefinitions() const
{
    return m_actionDefinitions;
}

quint32 VerifiedRuntimePackageEvidence::cyclePeriodNs() const
{
    return m_cyclePeriodNs;
}

Utils::Result<VerifiedRuntimePackageEvidence> verifyRuntimePackageEvidence(
    const VerifiedEcpkgPackage &package)
{
    if (package.manifest.trust != EcpkgTrustClass::Production) {
        return evidenceError(
            QString::fromLatin1("engineering packages cannot produce runtime evidence"));
    }

    Utils::Result<VerifiedSemanticBindingArtifact> artifact = verifySemanticBindingArtifact(
        package.container, package.manifest, package.configuration);
    if (!artifact)
        return evidenceError(artifact.error());

    const Utils::Result<> identityResult = validatePackageAndArtifact(package, *artifact);
    if (!identityResult)
        return evidenceError(identityResult.error());

    std::optional<VerifiedSemanticActionDefinitions> actionDefinitions;
    if (package.manifest.formatVersion == 2) {
        Utils::Result<VerifiedSemanticActionDefinitions> verifiedDefinitions
            = verifySemanticActionDefinitions(
                package.container, package.manifest, *artifact);
        if (!verifiedDefinitions)
            return evidenceError(verifiedDefinitions.error());
        actionDefinitions = std::move(*verifiedDefinitions);
    }

    const SignedEcpkgSemanticBindingSummary &summary = *package.manifest.semanticBinding;
    Data::RuntimeSemanticMappingProof proof;
    proof.formatVersion = summary.formatVersion;
    proof.bindingCount = summary.bindingCount;
    // These flags are evidence of the successful signed-manifest and complete
    // semantic-artifact verification represented by the two verified input types.
    proof.packageSigned = true;
    proof.signatureVerified = true;
    proof.semanticBindingVerified = true;
    proof.trust = Data::RuntimeSemanticMappingTrust::Production;
    proof.packageSha256 = artifact->packageSha256;
    proof.manifestSha256 = artifact->manifestSha256;
    proof.mappingSha256 = artifact->artifactSha256;
    proof.resourceRecordsSha256 = artifact->resourceRecordsSha256;
    proof.resourceSectionSha256 = artifact->resourceSectionSha256;
    proof.topologySha256 = artifact->topologySha256;
    proof.signingKeyIdSha256 = artifact->signingKeyIdSha256;

    VerifiedRuntimePackageEvidence result{
        std::move(*artifact),
        std::move(proof),
        package.manifest.compiledProjectSource.sha256,
        std::move(actionDefinitions),
        package.configuration.cyclePeriodNs,
    };
    const Utils::Result<> resultValidation = validateEvidence(result);
    if (!resultValidation)
        return evidenceError(resultValidation.error());
    return result;
}

Utils::Result<> verifyRuntimeSemanticMappingAttestation(
    const Data::RuntimeSemanticMappingAttestation &attestation,
    const Data::ControllerConnectionScope &expectedScope,
    quint64 expectedSessionGeneration,
    const Data::RuntimeResourceCatalogEpoch &expectedEpoch,
    const Data::SemanticBindingArtifactReference &expectedArtifactReference,
    const VerifiedRuntimePackageEvidence &localEvidence)
{
    const Utils::Result<> evidenceResult = validateEvidence(localEvidence);
    if (!evidenceResult)
        return evidenceError(evidenceResult.error());
    if (expectedScope.projectId.isNull() || expectedScope.masterId.isNull()
        || !expectedSessionGeneration
        || !Data::isCompleteRuntimeSemanticMappingEpoch(expectedEpoch)
        || expectedEpoch.topologyIdentity.size() != qsizetype(sizeof(quint64))
        || expectedArtifactReference.artifactId.isEmpty()
        || expectedArtifactReference.artifactId != expectedArtifactReference.artifactId.trimmed()
        || !validDigest(expectedArtifactReference.artifactSha256)
        || !validDigest(expectedArtifactReference.projectConfigurationSha256)) {
        return evidenceError(
            QString::fromLatin1("the expected controller context is incomplete"));
    }
    if (!attestation.isValid()) {
        return evidenceError(
            QString::fromLatin1("the controller attestation is incomplete"));
    }
    if (attestation.scope != expectedScope
        || attestation.sessionGeneration != expectedSessionGeneration) {
        return evidenceError(
            QString::fromLatin1("the controller scope or session generation changed"));
    }
    if (attestation.epoch != expectedEpoch) {
        return evidenceError(
            QString::fromLatin1("the complete controller runtime epoch changed"));
    }

    const VerifiedSemanticBindingArtifact &artifact
        = localEvidence.semanticBindingArtifact();
    if (!sameDigest(expectedArtifactReference.artifactSha256, artifact.artifactSha256)
        || !sameDigest(
            expectedArtifactReference.projectConfigurationSha256,
            localEvidence.projectConfigurationSha256())) {
        return evidenceError(
            QString::fromLatin1(
                "the current project binding reference differs from the verified package"));
    }
    if (expectedEpoch.configurationId != artifact.configurationId
        || expectedEpoch.catalogRevision != artifact.catalogRevision
        || expectedEpoch.topologyIdentity != opaqueBigEndian(artifact.topologyIdentity)) {
        return evidenceError(
            QString::fromLatin1(
                "the controller epoch does not identify the verified local package"));
    }
    if (!(attestation.proof == localEvidence.semanticMappingProof())) {
        return evidenceError(
            QString::fromLatin1(
                "the controller proof differs from the verified local package"));
    }
    return Utils::ResultOk;
}

} // namespace EtherCAT::SemanticRuntime::Internal
