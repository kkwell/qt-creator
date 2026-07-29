// Copyright (C) 2026 Embed Labs

#pragma once

#include "runtimepackageevidence_p.h"

#include <ethercatdata/deviceadapterselection.h>

#include <utils/result.h>

#include <QByteArrayView>
#include <QString>

namespace EtherCAT::SemanticRuntime::Internal {

// Loads immutable runtime evidence from application-owned fixed directories.
// All three roots are explicit values; no path is derived from artifactId or
// from package content.
class RuntimePackageEvidenceRepository final
{
public:
    RuntimePackageEvidenceRepository(
        QString verifiedPackageStoreRoot,
        QString productionTrustDirectory,
        QString compiledProjectSourceRoot);

    const QString &verifiedPackageStoreRoot() const;
    const QString &productionTrustDirectory() const;
    const QString &compiledProjectSourceRoot() const;

    Utils::Result<VerifiedRuntimePackageEvidence> load(
        const Data::SemanticBindingArtifactReference &reference) const;

    // A successful import has been reloaded from both content-addressed stores
    // and reverified. A failed import may leave a verified, unreachable orphan,
    // but never returns evidence assembled from only one stored input.
    Utils::Result<VerifiedRuntimePackageEvidence> import(
        QByteArrayView packageBytes, QByteArrayView compiledProjectSource) const;

private:
    Utils::Result<VerifiedRuntimePackageEvidence> loadLocked(
        QByteArrayView artifactSha256,
        QByteArrayView projectConfigurationSha256,
        const Data::SemanticBindingArtifactReference *reference) const;

    QString m_verifiedPackageStoreRoot;
    QString m_productionTrustDirectory;
    QString m_compiledProjectSourceRoot;
};

} // namespace EtherCAT::SemanticRuntime::Internal
