// Copyright (C) 2026 Embed Labs

#include "verifiedecpkgstore_p.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

#include <optional>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr int storeLockTimeoutMs = 5000;
constexpr auto storeDirectoryName = "sha256";
constexpr auto storeLockFileName = ".verified-ecpkg.lock";
constexpr auto packageSuffix = ".ecpkg";
constexpr qsizetype packageSuffixCharacters = 6;

Utils::ResultError storeError(const QString &detail)
{
    return Utils::ResultError(QString::fromLatin1("Verified ECPKG store error: %1").arg(detail));
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

QString digestFileName(QByteArrayView digest)
{
    return QString::fromLatin1(QByteArray(digest.data(), digest.size()).toHex()) + packageSuffix;
}

bool hasCanonicalPackageFileName(const QString &fileName)
{
    if (fileName.size() != 64 + packageSuffixCharacters
        || !fileName.endsWith(QString::fromLatin1(packageSuffix))) {
        return false;
    }
    const QString digest = fileName.first(64);
    for (QChar character : digest) {
        const ushort value = character.unicode();
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

Utils::Result<QString> prepareStoreRoot(const QString &storeRoot, bool create)
{
    if (storeRoot.isEmpty() || !QDir::isAbsolutePath(storeRoot))
        return storeError(QString::fromLatin1("the store root must be an absolute path"));

    const QString cleanRoot = QDir::cleanPath(storeRoot);
    QFileInfo rootInfo(cleanRoot);
    if (rootInfo.exists() && (rootInfo.isSymLink() || !rootInfo.isDir())) {
        return storeError(QString::fromLatin1("the store root is not a regular directory"));
    }
    if (!rootInfo.exists()) {
        if (!create)
            return storeError(QString::fromLatin1("the store root does not exist"));
        if (!QDir().mkpath(cleanRoot))
            return storeError(QString::fromLatin1("the store root could not be created"));
        rootInfo.refresh();
    }
    if (!rootInfo.exists() || rootInfo.isSymLink() || !rootInfo.isDir())
        return storeError(QString::fromLatin1("the store root is unavailable"));
    return rootInfo.absoluteFilePath();
}

Utils::Result<QString> prepareDigestDirectory(const QString &rootPath, bool create)
{
    const QString directoryPath = QDir(rootPath).filePath(QString::fromLatin1(storeDirectoryName));
    QFileInfo directoryInfo(directoryPath);
    if (directoryInfo.exists() && (directoryInfo.isSymLink() || !directoryInfo.isDir())) {
        return storeError(QString::fromLatin1("the sha256 entry path is not a regular directory"));
    }
    if (!directoryInfo.exists()) {
        if (!create)
            return storeError(QString::fromLatin1("the sha256 entry directory does not exist"));
        if (!QDir().mkpath(directoryPath)) {
            return storeError(
                QString::fromLatin1("the sha256 entry directory could not be created"));
        }
        directoryInfo.refresh();
    }
    if (!directoryInfo.exists() || directoryInfo.isSymLink() || !directoryInfo.isDir()) {
        return storeError(QString::fromLatin1("the sha256 entry directory is unavailable"));
    }
    return directoryInfo.absoluteFilePath();
}

Utils::Result<> acquireStoreLock(QLockFile *lock)
{
    lock->setStaleLockTime(0);
    if (!lock->tryLock(storeLockTimeoutMs))
        return storeError(QString::fromLatin1("the store lock could not be acquired"));
    return Utils::ResultOk;
}

Utils::Result<QByteArray> readPackageFile(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || fileInfo.isSymLink() || !fileInfo.isFile()) {
        return storeError(
            QString::fromLatin1("a package entry is missing or is not a regular file"));
    }
    if (fileInfo.size() <= 0 || fileInfo.size() > defaultMaximumEcpkgContainerBytes) {
        return storeError(QString::fromLatin1("a package entry has an invalid size"));
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return storeError(QString::fromLatin1("a package entry could not be opened"));
    const QByteArray bytes = file.read(defaultMaximumEcpkgContainerBytes + 1);
    if (bytes.size() != fileInfo.size() || !file.atEnd()) {
        return storeError(
            QString::fromLatin1("a package entry changed or exceeded its size limit while read"));
    }
    return bytes;
}

Utils::Result<VerifiedEcpkgPackage> verifyProductionEcpkgWithoutProject(
    QByteArrayView packageBytes, const QList<EcpkgTrustedPublicKey> &trustedPublicKeys)
{
    Utils::Result<EcpkgContainer> container = parseCanonicalEcpkgContainer(packageBytes);
    if (!container)
        return storeError(container.error());

    Utils::Result<VerifiedSignedEcpkgManifest> manifest
        = verifySignedEcpkgManifest(*container, trustedPublicKeys);
    if (!manifest)
        return storeError(manifest.error());
    if (manifest->trust != EcpkgTrustClass::Production) {
        return storeError(
            QString::fromLatin1("engineering packages are not accepted by this store"));
    }
    if (manifest->packageSha256 != container->packageSha256
        || manifest->packageSha256
               != QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256)) {
        return storeError(QString::fromLatin1("the package identity is inconsistent"));
    }

    Utils::Result<EcfgConfiguration> configuration = parseStrictEcfgConfiguration(
        container->configurationEcfg);
    if (!configuration)
        return storeError(configuration.error());
    if (manifest->configurationId != configuration->configurationId
        || manifest->configuration.sha256 != configuration->configurationSha256
        || manifest->configuration.bytes != quint32(container->configurationEcfg.size())
        || manifest->capability.sha256 != configuration->capabilitySha256) {
        return storeError(
            QString::fromLatin1("the signed manifest and ECFG identity are inconsistent"));
    }

    if (manifest->semanticBinding) {
        const SignedEcpkgSemanticBindingSummary &summary = *manifest->semanticBinding;
        if (summary.bindingCount != quint32(configuration->resources.size())
            || summary.catalogRevision != configuration->catalogRevision
            || summary.topologyIdentity != configuration->topologyIdentity
            || summary.resourceRecordsSha256 != configuration->resourceRecordsSha256
            || summary.resourceSectionSha256 != configuration->resourceTableSectionSha256) {
            return storeError(
                QString::fromLatin1(
                    "the signed semantic summary and ECFG resource table are inconsistent"));
        }
    }

    VerifiedEcpkgPackage result;
    result.packageBytes = QByteArray(packageBytes.data(), packageBytes.size());
    result.container = std::move(*container);
    result.manifest = std::move(*manifest);
    result.configuration = std::move(*configuration);
    return result;
}

Utils::Result<VerifiedEcpkgPackage> verifyStoredPackage(
    const QString &filePath,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource)
{
    Utils::Result<QByteArray> bytes = readPackageFile(filePath);
    if (!bytes)
        return storeError(bytes.error());
    Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkg(*bytes, trustedPublicKeys, compiledProjectSource);
    if (!package)
        return storeError(package.error());
    if (QFileInfo(filePath).fileName() != digestFileName(package->manifest.packageSha256)) {
        return storeError(
            QString::fromLatin1("a package entry name does not match its content digest"));
    }
    package->storedFilePath = QFileInfo(filePath).absoluteFilePath();
    return package;
}

Utils::Result<> writePackageAtomically(const QString &filePath, QByteArrayView packageBytes)
{
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return storeError(QString::fromLatin1("a package entry could not be created"));
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return storeError(QString::fromLatin1("secure package entry permissions could not be set"));
    }
    if (file.write(packageBytes.data(), packageBytes.size()) != packageBytes.size()) {
        file.cancelWriting();
        return storeError(QString::fromLatin1("a package entry could not be written completely"));
    }
    if (!file.commit())
        return storeError(QString::fromLatin1("a package entry could not be committed"));
    return Utils::ResultOk;
}

} // namespace

Utils::Result<VerifiedEcpkgPackage> verifyProductionEcpkg(
    QByteArrayView packageBytes,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource)
{
    Utils::Result<VerifiedEcpkgPackage> package
        = verifyProductionEcpkgWithoutProject(packageBytes, trustedPublicKeys);
    if (!package)
        return storeError(package.error());
    const Utils::Result<> projectResult
        = verifyEcpkgCompiledProjectSource(package->manifest, compiledProjectSource);
    if (!projectResult)
        return storeError(projectResult.error());
    return package;
}

Utils::Result<VerifiedEcpkgPackage> importVerifiedEcpkg(
    const QString &storeRoot,
    QByteArrayView packageBytes,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource)
{
    Utils::Result<VerifiedEcpkgPackage> verified
        = verifyProductionEcpkg(packageBytes, trustedPublicKeys, compiledProjectSource);
    if (!verified)
        return storeError(verified.error());

    Utils::Result<QString> rootPath = prepareStoreRoot(storeRoot, true);
    if (!rootPath)
        return storeError(rootPath.error());
    QLockFile lock(QDir(*rootPath).filePath(QString::fromLatin1(storeLockFileName)));
    const Utils::Result<> lockResult = acquireStoreLock(&lock);
    if (!lockResult)
        return storeError(lockResult.error());

    Utils::Result<QString> digestDirectory = prepareDigestDirectory(*rootPath, true);
    if (!digestDirectory)
        return storeError(digestDirectory.error());
    const QString filePath
        = QDir(*digestDirectory).filePath(digestFileName(verified->manifest.packageSha256));
    if (QFileInfo::exists(filePath)) {
        Utils::Result<VerifiedEcpkgPackage> existing
            = verifyStoredPackage(filePath, trustedPublicKeys, compiledProjectSource);
        if (!existing)
            return storeError(existing.error());
        if (existing->packageBytes != verified->packageBytes) {
            return storeError(
                QString::fromLatin1("an existing package identity has different bytes"));
        }
        return existing;
    }

    const Utils::Result<> writeResult = writePackageAtomically(filePath, verified->packageBytes);
    if (!writeResult)
        return storeError(writeResult.error());
    Utils::Result<VerifiedEcpkgPackage> stored
        = verifyStoredPackage(filePath, trustedPublicKeys, compiledProjectSource);
    if (!stored)
        return storeError(stored.error());
    if (stored->packageBytes != verified->packageBytes) {
        return storeError(QString::fromLatin1("the package bytes changed during atomic storage"));
    }
    return stored;
}

Utils::Result<VerifiedEcpkgPackage> loadVerifiedEcpkgByPackageSha256(
    const QString &storeRoot,
    QByteArrayView packageSha256,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource)
{
    if (!digestIsValid(packageSha256))
        return storeError(QString::fromLatin1("the package digest is invalid"));
    Utils::Result<QString> rootPath = prepareStoreRoot(storeRoot, false);
    if (!rootPath)
        return storeError(rootPath.error());
    QLockFile lock(QDir(*rootPath).filePath(QString::fromLatin1(storeLockFileName)));
    const Utils::Result<> lockResult = acquireStoreLock(&lock);
    if (!lockResult)
        return storeError(lockResult.error());
    Utils::Result<QString> digestDirectory = prepareDigestDirectory(*rootPath, false);
    if (!digestDirectory)
        return storeError(digestDirectory.error());

    const QString filePath = QDir(*digestDirectory).filePath(digestFileName(packageSha256));
    Utils::Result<VerifiedEcpkgPackage> package
        = verifyStoredPackage(filePath, trustedPublicKeys, compiledProjectSource);
    if (!package)
        return storeError(package.error());
    if (package->manifest.packageSha256 != packageSha256)
        return storeError(QString::fromLatin1("the requested package digest does not match"));
    return package;
}

Utils::Result<VerifiedEcpkgPackage> findVerifiedEcpkgBySemanticMapping(
    const QString &storeRoot,
    QByteArrayView semanticMappingSha256,
    const QList<EcpkgTrustedPublicKey> &trustedPublicKeys,
    QByteArrayView compiledProjectSource)
{
    if (!digestIsValid(semanticMappingSha256))
        return storeError(QString::fromLatin1("the semantic-mapping digest is invalid"));
    if (compiledProjectSource.size() > defaultMaximumCompiledProjectBytes) {
        return storeError(QString::fromLatin1("the compiled-project source exceeds its size limit"));
    }
    Utils::Result<QString> rootPath = prepareStoreRoot(storeRoot, false);
    if (!rootPath)
        return storeError(rootPath.error());
    QLockFile lock(QDir(*rootPath).filePath(QString::fromLatin1(storeLockFileName)));
    const Utils::Result<> lockResult = acquireStoreLock(&lock);
    if (!lockResult)
        return storeError(lockResult.error());
    Utils::Result<QString> digestDirectory = prepareDigestDirectory(*rootPath, false);
    if (!digestDirectory)
        return storeError(digestDirectory.error());

    const QFileInfoList entries = QDir(*digestDirectory)
                                      .entryInfoList(
                                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                                              | QDir::System,
                                          QDir::Name);
    if (entries.size() > maximumVerifiedEcpkgStoreEntries) {
        return storeError(QString::fromLatin1("the package entry count exceeds the store limit"));
    }

    const QByteArray projectSha256
        = QCryptographicHash::hash(compiledProjectSource, QCryptographicHash::Sha256);
    std::optional<VerifiedEcpkgPackage> match;
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink() || !entry.isFile() || !hasCanonicalPackageFileName(entry.fileName())) {
            return storeError(
                QString::fromLatin1("the sha256 directory contains a noncanonical entry"));
        }
        Utils::Result<QByteArray> bytes = readPackageFile(entry.absoluteFilePath());
        if (!bytes)
            return storeError(bytes.error());
        Utils::Result<VerifiedEcpkgPackage> package
            = verifyProductionEcpkgWithoutProject(*bytes, trustedPublicKeys);
        if (!package)
            return storeError(package.error());
        if (entry.fileName() != digestFileName(package->manifest.packageSha256)) {
            return storeError(
                QString::fromLatin1("a package entry name does not match its content digest"));
        }

        if (!package->manifest.semanticBinding
            || package->manifest.semanticBinding->mappingSha256 != semanticMappingSha256
            || package->manifest.compiledProjectSource.sha256 != projectSha256
            || package->manifest.compiledProjectSource.bytes
                   != quint64(compiledProjectSource.size())) {
            continue;
        }
        const Utils::Result<> sourceResult
            = verifyEcpkgCompiledProjectSource(package->manifest, compiledProjectSource);
        if (!sourceResult)
            return storeError(sourceResult.error());
        package->storedFilePath = entry.absoluteFilePath();
        if (match) {
            return storeError(
                QString::fromLatin1(
                    "multiple package identities match the semantic mapping and project source"));
        }
        match = std::move(*package);
    }
    if (!match) {
        return storeError(
            QString::fromLatin1(
                "no package matches the semantic mapping and compiled-project source"));
    }
    return std::move(*match);
}

} // namespace EtherCAT::SemanticRuntime::Internal
