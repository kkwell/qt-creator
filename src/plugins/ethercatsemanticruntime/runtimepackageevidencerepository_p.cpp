// Copyright (C) 2026 Embed Labs

#include "runtimepackageevidencerepository_p.h"

#include "canonicaljson_p.h"
#include "productiontruststore_p.h"
#include "verifiedecpkgstore_p.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr int repositoryLockTimeoutMs = 5000;
constexpr auto digestDirectoryName = "sha256";
constexpr auto projectSourceSuffix = ".json";
constexpr auto repositoryLockFileName = ".runtime-package-evidence.lock";
constexpr auto packageStoreLockFileName = ".verified-ecpkg.lock";
constexpr qsizetype maximumProjectSourceEntries = 4096;
constexpr qsizetype sha256HexCharacters = 64;

enum class RepositoryRootKind {
    PackageStore,
    ProjectSource,
};

struct FileSnapshot
{
    QString fileName;
    qint64 size = -1;
    QDateTime lastModified;

    friend bool operator==(const FileSnapshot &, const FileSnapshot &) = default;
};

Utils::ResultError repositoryError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Runtime package evidence repository error: %1").arg(detail));
}

bool isLinkLike(const QFileInfo &fileInfo)
{
    return fileInfo.isSymLink() || fileInfo.isJunction();
}

bool digestIsValid(QByteArrayView digest)
{
    if (digest.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256))
        return false;
    for (char byte : digest) {
        if (byte != 0)
            return true;
    }
    return false;
}

bool trustStoresEqual(
    const QList<EcpkgTrustedPublicKey> &left,
    const QList<EcpkgTrustedPublicKey> &right)
{
    if (left.size() != right.size())
        return false;
    for (qsizetype index = 0; index < left.size(); ++index) {
        if (left.at(index).rawPublicKey != right.at(index).rawPublicKey
            || left.at(index).trust != right.at(index).trust) {
            return false;
        }
    }
    return true;
}

bool hasCanonicalDigestFileName(const QString &fileName, QStringView suffix)
{
    if (fileName.size() != sha256HexCharacters + suffix.size()
        || !fileName.endsWith(suffix)) {
        return false;
    }
    for (QChar character : fileName.first(sha256HexCharacters)) {
        const ushort value = character.unicode();
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

QString digestFileName(QByteArrayView digest, QStringView suffix)
{
    return QString::fromLatin1(QByteArray(digest.data(), digest.size()).toHex()) + suffix;
}

bool pathContainsLinkLikeEntry(const QString &absolutePath)
{
    QString path = absolutePath;
    while (!path.isEmpty()) {
        const QFileInfo info(path);
        if (info.exists() && isLinkLike(info))
            return true;

        const QString parent = info.dir().absolutePath();
        if (parent == path)
            break;
        path = parent;
    }
    return false;
}

Utils::Result<> validateRootConfiguration(const QString &root, bool mayBeMissing)
{
    if (root.isEmpty() || !QDir::isAbsolutePath(root))
        return repositoryError(QString::fromLatin1("all roots must be absolute paths"));
    if (root != QDir::cleanPath(root)) {
        return repositoryError(
            QString::fromLatin1("all roots must use clean canonical path spelling"));
    }
    if (pathContainsLinkLikeEntry(root)) {
        return repositoryError(
            QString::fromLatin1("a configured root has a symbolic-link or junction component"));
    }

    const QFileInfo info(root);
    if (!info.exists()) {
        if (mayBeMissing)
            return Utils::ResultOk;
        return repositoryError(QString::fromLatin1("a configured root does not exist"));
    }
    if (isLinkLike(info) || !info.isDir()) {
        return repositoryError(
            QString::fromLatin1("a configured root is not a regular directory"));
    }
    const QString canonicalPath = info.canonicalFilePath();
    if (canonicalPath.isEmpty() || canonicalPath != root) {
        return repositoryError(
            QString::fromLatin1("a configured root does not have canonical path spelling"));
    }
    return Utils::ResultOk;
}

Utils::Result<> prepareRoot(const QString &root, bool create)
{
    const Utils::Result<> configuration = validateRootConfiguration(root, create);
    if (!configuration)
        return repositoryError(configuration.error());
    if (!QFileInfo::exists(root)) {
        if (!create || !QDir().mkpath(root))
            return repositoryError(QString::fromLatin1("a configured root could not be created"));
    }
    return validateRootConfiguration(root, false);
}

Utils::Result<QString> prepareDigestDirectory(const QString &root, bool create)
{
    const QString path = QDir(root).filePath(QString::fromLatin1(digestDirectoryName));
    const QFileInfo initialInfo(path);
    if (initialInfo.exists() && (isLinkLike(initialInfo) || !initialInfo.isDir())) {
        return repositoryError(
            QString::fromLatin1("a sha256 entry path is not a regular directory"));
    }
    if (!initialInfo.exists()) {
        if (!create || !QDir().mkpath(path)) {
            return repositoryError(
                QString::fromLatin1("a sha256 entry directory could not be created"));
        }
    }

    const QFileInfo finalInfo(path);
    if (!finalInfo.exists() || isLinkLike(finalInfo) || !finalInfo.isDir()
        || pathContainsLinkLikeEntry(path) || finalInfo.canonicalFilePath() != path) {
        return repositoryError(
            QString::fromLatin1(
                "the sha256 entry directory '%1' is not the canonical path '%2'")
                .arg(path, finalInfo.canonicalFilePath()));
    }
    return path;
}

Utils::Result<> validateDigestEntries(
    const QString &digestDirectory, RepositoryRootKind kind)
{
    const QFileInfoList entries = QDir(digestDirectory)
                                      .entryInfoList(
                                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                                              | QDir::System,
                                          QDir::Name);
    const qsizetype maximumEntries = kind == RepositoryRootKind::PackageStore
                                         ? maximumVerifiedEcpkgStoreEntries
                                         : maximumProjectSourceEntries;
    if (entries.size() > maximumEntries) {
        return repositoryError(
            QString::fromLatin1("a sha256 directory exceeds its entry limit"));
    }

    const QString suffix = kind == RepositoryRootKind::PackageStore
                               ? QString::fromLatin1(".ecpkg")
                               : QString::fromLatin1(projectSourceSuffix);
    for (const QFileInfo &entry : entries) {
        if (entry.isHidden() || isLinkLike(entry) || !entry.isFile()
            || !hasCanonicalDigestFileName(entry.fileName(), suffix)) {
            return repositoryError(
                QString::fromLatin1("a sha256 directory contains a noncanonical entry"));
        }
        const qint64 maximumBytes = kind == RepositoryRootKind::PackageStore
                                        ? defaultMaximumEcpkgContainerBytes
                                        : defaultMaximumCompiledProjectBytes;
        if (entry.size() <= 0 || entry.size() > maximumBytes) {
            return repositoryError(
                QString::fromLatin1("a content-addressed entry has an invalid size"));
        }
    }
    return Utils::ResultOk;
}

Utils::Result<> validateRootLayout(
    const QString &root, RepositoryRootKind kind, bool requireDigestDirectory)
{
    const Utils::Result<> rootResult = validateRootConfiguration(root, false);
    if (!rootResult)
        return repositoryError(rootResult.error());

    bool sawDigestDirectory = false;
    const QFileInfoList entries = QDir(root).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const QFileInfo &entry : entries) {
        const bool digestDirectory
            = entry.fileName() == QString::fromLatin1(digestDirectoryName);
        const QString allowedLockName
            = kind == RepositoryRootKind::PackageStore
                  ? QString::fromLatin1(packageStoreLockFileName)
                  : QString::fromLatin1(repositoryLockFileName);
        const bool lockFile = entry.fileName() == allowedLockName;

        if (isLinkLike(entry)
            || (digestDirectory && (!entry.isDir() || entry.isHidden()))
            || (lockFile && !entry.isFile())
            || (!digestDirectory && !lockFile)) {
            return repositoryError(
                QString::fromLatin1("a repository root contains a noncanonical entry"));
        }
        sawDigestDirectory |= digestDirectory;
    }

    if (!sawDigestDirectory) {
        if (requireDigestDirectory) {
            return repositoryError(
                QString::fromLatin1("a repository sha256 directory is missing"));
        }
        return Utils::ResultOk;
    }
    return validateDigestEntries(
        QDir(root).filePath(QString::fromLatin1(digestDirectoryName)), kind);
}

Utils::Result<> acquireRepositoryLock(QLockFile *lock)
{
    lock->setStaleLockTime(0);
    if (!lock->tryLock(repositoryLockTimeoutMs)) {
        return repositoryError(
            QString::fromLatin1("the evidence repository lock could not be acquired"));
    }
    return Utils::ResultOk;
}

Utils::Result<> validateProjectSourceBytes(QByteArrayView bytes)
{
    if (bytes.isEmpty() || bytes.size() > defaultMaximumCompiledProjectBytes) {
        return repositoryError(
            QString::fromLatin1("the compiled-project source has an invalid size"));
    }
    const Utils::Result<StrictJson> parsed
        = parseStrictJson(bytes, defaultMaximumCompiledProjectBytes);
    if (!parsed || !parsed->is_object()) {
        return repositoryError(
            parsed ? QString::fromLatin1("the compiled-project JSON root is not an object")
                   : parsed.error());
    }
    return Utils::ResultOk;
}

Utils::Result<QByteArray> readProjectSourceFile(
    const QString &projectSourceRoot, QByteArrayView expectedSha256)
{
    if (!digestIsValid(expectedSha256))
        return repositoryError(QString::fromLatin1("the compiled-project digest is invalid"));

    const QString directoryPath
        = QDir(projectSourceRoot).filePath(QString::fromLatin1(digestDirectoryName));
    const QString filePath = QDir(directoryPath)
                                 .filePath(
                                     digestFileName(
                                         expectedSha256,
                                         QString::fromLatin1(projectSourceSuffix)));
    const QFileInfo initialInfo(filePath);
    if (!initialInfo.exists() || initialInfo.isHidden() || isLinkLike(initialInfo)
        || !initialInfo.isFile()
        || !hasCanonicalDigestFileName(
            initialInfo.fileName(), QString::fromLatin1(projectSourceSuffix))
        || initialInfo.size() <= 0
        || initialInfo.size() > defaultMaximumCompiledProjectBytes) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar is missing or invalid"));
    }
    const FileSnapshot initialSnapshot{
        initialInfo.fileName(), initialInfo.size(), initialInfo.lastModified()};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.isSequential()
        || file.size() != initialInfo.size()) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar could not be opened safely"));
    }
    const QByteArray firstRead = file.read(defaultMaximumCompiledProjectBytes + 1);
    if (firstRead.size() != initialInfo.size() || !file.atEnd()
        || file.error() != QFileDevice::NoError || !file.seek(0)) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar changed while being read"));
    }
    const QByteArray secondRead = file.read(defaultMaximumCompiledProjectBytes + 1);
    if (secondRead != firstRead || !file.atEnd() || file.error() != QFileDevice::NoError
        || file.size() != initialInfo.size()) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar changed while being read"));
    }

    const QFileInfo finalInfo(filePath);
    const FileSnapshot finalSnapshot{
        finalInfo.fileName(), finalInfo.size(), finalInfo.lastModified()};
    if (!finalInfo.exists() || finalInfo.isHidden() || isLinkLike(finalInfo)
        || !finalInfo.isFile() || finalSnapshot != initialSnapshot
        || finalInfo.canonicalFilePath() != filePath) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar identity changed while read"));
    }
    if (QCryptographicHash::hash(secondRead, QCryptographicHash::Sha256) != expectedSha256) {
        return repositoryError(
            QString::fromLatin1(
                "the compiled-project sidecar name does not match its content digest"));
    }
    const Utils::Result<> sourceResult = validateProjectSourceBytes(secondRead);
    if (!sourceResult)
        return repositoryError(sourceResult.error());
    return secondRead;
}

Utils::Result<> writeProjectSourceFile(
    const QString &projectSourceRoot,
    QByteArrayView projectSha256,
    QByteArrayView compiledProjectSource)
{
    Utils::Result<QString> directoryPath = prepareDigestDirectory(projectSourceRoot, true);
    if (!directoryPath)
        return repositoryError(directoryPath.error());
    const QString filePath = QDir(*directoryPath)
                                 .filePath(
                                     digestFileName(
                                         projectSha256,
                                         QString::fromLatin1(projectSourceSuffix)));
    if (QFileInfo::exists(filePath)) {
        const Utils::Result<QByteArray> existing
            = readProjectSourceFile(projectSourceRoot, projectSha256);
        if (!existing)
            return repositoryError(existing.error());
        if (*existing != compiledProjectSource) {
            return repositoryError(
                QString::fromLatin1(
                    "an existing compiled-project identity has different bytes"));
        }
        return Utils::ResultOk;
    }

    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar could not be created"));
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return repositoryError(
            QString::fromLatin1("secure compiled-project permissions could not be set"));
    }
    if (file.write(compiledProjectSource.data(), compiledProjectSource.size())
        != compiledProjectSource.size()) {
        file.cancelWriting();
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar was not written completely"));
    }
    if (!file.commit()) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar could not be committed"));
    }

    const Utils::Result<QByteArray> stored
        = readProjectSourceFile(projectSourceRoot, projectSha256);
    if (!stored)
        return repositoryError(stored.error());
    if (*stored != compiledProjectSource) {
        return repositoryError(
            QString::fromLatin1("the compiled-project bytes changed during atomic storage"));
    }
    return Utils::ResultOk;
}

Utils::Result<> validateReference(
    const Data::SemanticBindingArtifactReference &reference)
{
    if (reference.artifactId.isEmpty()
        || reference.artifactId != reference.artifactId.trimmed()
        || !digestIsValid(reference.artifactSha256)
        || !digestIsValid(reference.projectConfigurationSha256)) {
        return repositoryError(
            QString::fromLatin1("the semantic binding artifact reference is invalid"));
    }
    return Utils::ResultOk;
}

} // namespace

RuntimePackageEvidenceRepository::RuntimePackageEvidenceRepository(
    QString verifiedPackageStoreRoot,
    QString productionTrustDirectory,
    QString compiledProjectSourceRoot)
    : m_verifiedPackageStoreRoot(std::move(verifiedPackageStoreRoot))
    , m_productionTrustDirectory(std::move(productionTrustDirectory))
    , m_compiledProjectSourceRoot(std::move(compiledProjectSourceRoot))
{}

const QString &RuntimePackageEvidenceRepository::verifiedPackageStoreRoot() const
{
    return m_verifiedPackageStoreRoot;
}

const QString &RuntimePackageEvidenceRepository::productionTrustDirectory() const
{
    return m_productionTrustDirectory;
}

const QString &RuntimePackageEvidenceRepository::compiledProjectSourceRoot() const
{
    return m_compiledProjectSourceRoot;
}

Utils::Result<VerifiedRuntimePackageEvidence> RuntimePackageEvidenceRepository::load(
    const Data::SemanticBindingArtifactReference &reference) const
{
    const Data::SemanticBindingArtifactReference stableReference = reference;
    const Utils::Result<> referenceResult = validateReference(stableReference);
    if (!referenceResult)
        return repositoryError(referenceResult.error());
    const Utils::Result<> packageRoot = prepareRoot(m_verifiedPackageStoreRoot, false);
    if (!packageRoot)
        return repositoryError(packageRoot.error());
    const Utils::Result<> projectRoot = prepareRoot(m_compiledProjectSourceRoot, false);
    if (!projectRoot)
        return repositoryError(projectRoot.error());
    const Utils::Result<> trustRoot = prepareRoot(m_productionTrustDirectory, false);
    if (!trustRoot)
        return repositoryError(trustRoot.error());

    const Utils::Result<> packageLayout = validateRootLayout(
        m_verifiedPackageStoreRoot, RepositoryRootKind::PackageStore, true);
    if (!packageLayout)
        return repositoryError(packageLayout.error());
    const Utils::Result<> projectLayout = validateRootLayout(
        m_compiledProjectSourceRoot, RepositoryRootKind::ProjectSource, true);
    if (!projectLayout)
        return repositoryError(projectLayout.error());

    QLockFile lock(
        QDir(m_compiledProjectSourceRoot)
            .filePath(QString::fromLatin1(repositoryLockFileName)));
    const Utils::Result<> lockResult = acquireRepositoryLock(&lock);
    if (!lockResult)
        return repositoryError(lockResult.error());
    return loadLocked(
        stableReference.artifactSha256,
        stableReference.projectConfigurationSha256,
        &stableReference);
}

Utils::Result<VerifiedRuntimePackageEvidence> RuntimePackageEvidenceRepository::import(
    QByteArrayView packageBytes, QByteArrayView compiledProjectSource) const
{
    if (packageBytes.isEmpty() || packageBytes.size() > defaultMaximumEcpkgContainerBytes) {
        return repositoryError(QString::fromLatin1("the ECPKG bytes have an invalid size"));
    }
    const Utils::Result<> sourceResult = validateProjectSourceBytes(compiledProjectSource);
    if (!sourceResult)
        return repositoryError(sourceResult.error());
    const QByteArray stablePackageBytes(packageBytes.data(), packageBytes.size());
    const QByteArray stableProjectSource(
        compiledProjectSource.data(), compiledProjectSource.size());

    // Verification precedes every repository mutation.
    const Utils::Result<> packageRootConfiguration
        = validateRootConfiguration(m_verifiedPackageStoreRoot, true);
    if (!packageRootConfiguration)
        return repositoryError(packageRootConfiguration.error());
    const Utils::Result<> projectRootConfiguration
        = validateRootConfiguration(m_compiledProjectSourceRoot, true);
    if (!projectRootConfiguration)
        return repositoryError(projectRootConfiguration.error());
    const Utils::Result<> trustRoot = prepareRoot(m_productionTrustDirectory, false);
    if (!trustRoot)
        return repositoryError(trustRoot.error());
    Utils::Result<QList<EcpkgTrustedPublicKey>> trustedKeys
        = loadProductionEcpkgTrustStore(m_productionTrustDirectory);
    if (!trustedKeys)
        return repositoryError(trustedKeys.error());
    Utils::Result<VerifiedEcpkgPackage> verified
        = verifyProductionEcpkg(stablePackageBytes, *trustedKeys, stableProjectSource);
    if (!verified)
        return repositoryError(verified.error());
    Utils::Result<VerifiedRuntimePackageEvidence> initialEvidence
        = verifyRuntimePackageEvidence(*verified);
    if (!initialEvidence)
        return repositoryError(initialEvidence.error());

    const QByteArray projectSha256
        = QCryptographicHash::hash(stableProjectSource, QCryptographicHash::Sha256);
    if (projectSha256 != initialEvidence->projectConfigurationSha256()) {
        return repositoryError(
            QString::fromLatin1("the compiled-project source identity is inconsistent"));
    }
    const QByteArray artifactSha256
        = initialEvidence->semanticBindingArtifact().artifactSha256;
    const QByteArray packageSha256 = initialEvidence->semanticBindingArtifact().packageSha256;

    const Utils::Result<> packageRoot = prepareRoot(m_verifiedPackageStoreRoot, true);
    if (!packageRoot)
        return repositoryError(packageRoot.error());
    const Utils::Result<> projectRoot = prepareRoot(m_compiledProjectSourceRoot, true);
    if (!projectRoot)
        return repositoryError(projectRoot.error());

    QLockFile lock(
        QDir(m_compiledProjectSourceRoot)
            .filePath(QString::fromLatin1(repositoryLockFileName)));
    const Utils::Result<> lockResult = acquireRepositoryLock(&lock);
    if (!lockResult)
        return repositoryError(lockResult.error());

    const Utils::Result<> packageLayout = validateRootLayout(
        m_verifiedPackageStoreRoot, RepositoryRootKind::PackageStore, false);
    if (!packageLayout)
        return repositoryError(packageLayout.error());
    const Utils::Result<> projectLayout = validateRootLayout(
        m_compiledProjectSourceRoot, RepositoryRootKind::ProjectSource, false);
    if (!projectLayout)
        return repositoryError(projectLayout.error());

    // Re-read trust and reverify under the repository lock, so a changed trust
    // directory cannot be paired with the earlier result.
    trustedKeys = loadProductionEcpkgTrustStore(m_productionTrustDirectory);
    if (!trustedKeys)
        return repositoryError(trustedKeys.error());
    verified = verifyProductionEcpkg(stablePackageBytes, *trustedKeys, stableProjectSource);
    if (!verified)
        return repositoryError(verified.error());
    if (verified->manifest.packageSha256 != packageSha256) {
        return repositoryError(
            QString::fromLatin1("the verified package identity changed before import"));
    }

    const Utils::Result<> sidecarResult = writeProjectSourceFile(
        m_compiledProjectSourceRoot, projectSha256, stableProjectSource);
    if (!sidecarResult)
        return repositoryError(sidecarResult.error());

    Utils::Result<VerifiedEcpkgPackage> imported = importVerifiedEcpkg(
        m_verifiedPackageStoreRoot, stablePackageBytes, *trustedKeys, stableProjectSource);
    if (!imported)
        return repositoryError(imported.error());
    if (imported->manifest.packageSha256 != packageSha256
        || imported->packageBytes != stablePackageBytes) {
        return repositoryError(
            QString::fromLatin1("the stored package differs from the verified import"));
    }

    Utils::Result<VerifiedRuntimePackageEvidence> loaded
        = loadLocked(artifactSha256, projectSha256, nullptr);
    if (!loaded)
        return repositoryError(loaded.error());
    if (loaded->semanticBindingArtifact().packageSha256 != packageSha256
        || loaded->semanticBindingArtifact().artifactSha256 != artifactSha256
        || loaded->projectConfigurationSha256() != projectSha256) {
        return repositoryError(
            QString::fromLatin1("the reloaded evidence differs from the verified import"));
    }
    return loaded;
}

Utils::Result<VerifiedRuntimePackageEvidence>
RuntimePackageEvidenceRepository::loadLocked(
    QByteArrayView artifactSha256,
    QByteArrayView projectConfigurationSha256,
    const Data::SemanticBindingArtifactReference *reference) const
{
    if (!digestIsValid(artifactSha256) || !digestIsValid(projectConfigurationSha256)) {
        return repositoryError(
            QString::fromLatin1("the requested evidence identity is invalid"));
    }
    const Utils::Result<> packageLayout = validateRootLayout(
        m_verifiedPackageStoreRoot, RepositoryRootKind::PackageStore, true);
    if (!packageLayout)
        return repositoryError(packageLayout.error());
    const Utils::Result<> projectLayout = validateRootLayout(
        m_compiledProjectSourceRoot, RepositoryRootKind::ProjectSource, true);
    if (!projectLayout)
        return repositoryError(projectLayout.error());

    const Utils::Result<QByteArray> projectSource = readProjectSourceFile(
        m_compiledProjectSourceRoot, projectConfigurationSha256);
    if (!projectSource)
        return repositoryError(projectSource.error());
    Utils::Result<QList<EcpkgTrustedPublicKey>> trustedKeys
        = loadProductionEcpkgTrustStore(m_productionTrustDirectory);
    if (!trustedKeys)
        return repositoryError(trustedKeys.error());

    Utils::Result<VerifiedEcpkgPackage> found = findVerifiedEcpkgBySemanticMapping(
        m_verifiedPackageStoreRoot, artifactSha256, *trustedKeys, *projectSource);
    if (!found)
        return repositoryError(found.error());
    Utils::Result<VerifiedEcpkgPackage> reloaded = loadVerifiedEcpkgByPackageSha256(
        m_verifiedPackageStoreRoot,
        found->manifest.packageSha256,
        *trustedKeys,
        *projectSource);
    if (!reloaded)
        return repositoryError(reloaded.error());
    if (reloaded->packageBytes != found->packageBytes
        || reloaded->manifest.packageSha256 != found->manifest.packageSha256
        || reloaded->storedFilePath != found->storedFilePath) {
        return repositoryError(
            QString::fromLatin1("the package changed between verified store reads"));
    }

    Utils::Result<VerifiedRuntimePackageEvidence> evidence
        = verifyRuntimePackageEvidence(*reloaded);
    if (!evidence)
        return repositoryError(evidence.error());
    if (evidence->semanticBindingArtifact().artifactSha256 != artifactSha256
        || evidence->projectConfigurationSha256() != projectConfigurationSha256) {
        return repositoryError(
            QString::fromLatin1("the runtime evidence does not match the requested identity"));
    }
    if (reference
        && (reference->artifactSha256
                != evidence->semanticBindingArtifact().artifactSha256
            || reference->projectConfigurationSha256
                   != evidence->projectConfigurationSha256())) {
        return repositoryError(
            QString::fromLatin1("the runtime evidence does not match the project reference"));
    }

    const Utils::Result<QByteArray> finalProjectSource = readProjectSourceFile(
        m_compiledProjectSourceRoot, projectConfigurationSha256);
    if (!finalProjectSource || *finalProjectSource != *projectSource) {
        return repositoryError(
            QString::fromLatin1("the compiled-project sidecar changed during evidence loading"));
    }
    const Utils::Result<QList<EcpkgTrustedPublicKey>> finalTrustedKeys
        = loadProductionEcpkgTrustStore(m_productionTrustDirectory);
    if (!finalTrustedKeys || !trustStoresEqual(*trustedKeys, *finalTrustedKeys)) {
        return repositoryError(
            QString::fromLatin1("the production trust store changed during evidence loading"));
    }
    const Utils::Result<VerifiedEcpkgPackage> finalFound
        = findVerifiedEcpkgBySemanticMapping(
            m_verifiedPackageStoreRoot,
            artifactSha256,
            *finalTrustedKeys,
            *finalProjectSource);
    if (!finalFound || finalFound->manifest.packageSha256 != reloaded->manifest.packageSha256
        || finalFound->storedFilePath != reloaded->storedFilePath
        || finalFound->packageBytes != reloaded->packageBytes) {
        return repositoryError(
            QString::fromLatin1(
                "the unique package selected for this semantic mapping changed during loading"));
    }
    const Utils::Result<> finalPackageLayout = validateRootLayout(
        m_verifiedPackageStoreRoot, RepositoryRootKind::PackageStore, true);
    const Utils::Result<> finalProjectLayout = validateRootLayout(
        m_compiledProjectSourceRoot, RepositoryRootKind::ProjectSource, true);
    if (!finalPackageLayout || !finalProjectLayout) {
        return repositoryError(
            !finalPackageLayout ? finalPackageLayout.error() : finalProjectLayout.error());
    }
    return evidence;
}

} // namespace EtherCAT::SemanticRuntime::Internal
