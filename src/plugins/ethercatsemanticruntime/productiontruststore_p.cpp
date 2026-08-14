// Copyright (C) 2026 Embed Labs

#include "productiontruststore_p.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr qsizetype ed25519PublicKeyBytes = 32;
constexpr qsizetype sha256HexCharacters = 64;
constexpr qsizetype publicKeySuffixCharacters = 4;
constexpr auto publicKeySuffix = ".pub";

struct TrustEntrySnapshot
{
    QString fileName;
    qint64 size = -1;
    QDateTime lastModified;

    friend bool operator==(const TrustEntrySnapshot &, const TrustEntrySnapshot &) = default;
};

Utils::ResultError trustStoreError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Production ECPKG trust store error: %1").arg(detail));
}

bool isLinkLike(const QFileInfo &fileInfo)
{
    return fileInfo.isSymLink() || fileInfo.isJunction();
}

bool hasCanonicalPublicKeyFileName(const QString &fileName)
{
    if (fileName.size() != sha256HexCharacters + publicKeySuffixCharacters
        || !fileName.endsWith(QString::fromLatin1(publicKeySuffix))) {
        return false;
    }

    for (QChar character : fileName.first(sha256HexCharacters)) {
        const ushort value = character.unicode();
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

bool pathContainsSymbolicLink(const QString &absolutePath)
{
    QString currentPath = QDir::cleanPath(absolutePath);
    while (!currentPath.isEmpty()) {
        const QFileInfo currentInfo(currentPath);
        if (isLinkLike(currentInfo))
            return true;

        const QString parentPath = currentInfo.dir().absolutePath();
        if (parentPath == currentPath)
            break;
        currentPath = parentPath;
    }
    return false;
}

Utils::Result<QFileInfoList> trustDirectoryEntries(const QString &directoryPath)
{
    const QFileInfo directoryInfo(directoryPath);
    if (!directoryInfo.exists() || isLinkLike(directoryInfo) || !directoryInfo.isDir()
        || pathContainsSymbolicLink(directoryPath)) {
        return trustStoreError(
            QString::fromLatin1("the trust path is not a regular, symlink-free directory"));
    }

    const QFileInfoList entries = QDir(directoryPath)
                                      .entryInfoList(
                                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                                              | QDir::System,
                                          QDir::Name);
    if (entries.isEmpty())
        return trustStoreError(QString::fromLatin1("the trust directory is empty"));
    if (entries.size() > maximumEcpkgTrustedPublicKeys) {
        return trustStoreError(
            QString::fromLatin1("the trust directory exceeds %1 entries")
                .arg(maximumEcpkgTrustedPublicKeys));
    }

    for (const QFileInfo &entry : entries) {
        if (entry.isHidden() || isLinkLike(entry) || !entry.isFile()
            || !hasCanonicalPublicKeyFileName(entry.fileName())) {
            return trustStoreError(
                QString::fromLatin1("the trust directory contains a noncanonical entry"));
        }
        if (entry.size() != ed25519PublicKeyBytes) {
            return trustStoreError(
                QString::fromLatin1("a trusted Ed25519 public key is not exactly 32 bytes"));
        }
    }
    return entries;
}

QList<TrustEntrySnapshot> snapshotEntries(const QFileInfoList &entries)
{
    QList<TrustEntrySnapshot> result;
    result.reserve(entries.size());
    for (const QFileInfo &entry : entries) {
        result.append({entry.fileName(), entry.size(), entry.lastModified()});
    }
    return result;
}

Utils::Result<QByteArray> readPublicKey(const QFileInfo &initialInfo)
{
    QFile keyFile(initialInfo.absoluteFilePath());
    if (!keyFile.open(QIODevice::ReadOnly))
        return trustStoreError(QString::fromLatin1("a trusted public key could not be opened"));
    if (keyFile.isSequential() || keyFile.size() != ed25519PublicKeyBytes) {
        return trustStoreError(
            QString::fromLatin1("a trusted public key changed size while being opened"));
    }

    const QByteArray firstRead = keyFile.read(ed25519PublicKeyBytes + 1);
    if (firstRead.size() != ed25519PublicKeyBytes || !keyFile.atEnd()
        || keyFile.error() != QFileDevice::NoError) {
        return trustStoreError(
            QString::fromLatin1("a trusted public key changed or could not be read completely"));
    }
    if (!keyFile.seek(0)) {
        return trustStoreError(
            QString::fromLatin1("a trusted public key could not be checked for changes"));
    }

    const QByteArray secondRead = keyFile.read(ed25519PublicKeyBytes + 1);
    if (secondRead != firstRead || !keyFile.atEnd()
        || keyFile.error() != QFileDevice::NoError
        || keyFile.size() != ed25519PublicKeyBytes) {
        return trustStoreError(
            QString::fromLatin1("a trusted public key changed while it was being read"));
    }

    const QFileInfo finalInfo(initialInfo.absoluteFilePath());
    if (!finalInfo.exists() || finalInfo.isHidden() || isLinkLike(finalInfo)
        || !finalInfo.isFile() || finalInfo.size() != initialInfo.size()
        || finalInfo.lastModified() != initialInfo.lastModified()
        || finalInfo.fileName() != initialInfo.fileName()) {
        return trustStoreError(
            QString::fromLatin1("a trusted public-key entry changed while it was being read"));
    }

    return secondRead;
}

} // namespace

Utils::Result<QList<EcpkgTrustedPublicKey>> loadProductionEcpkgTrustStore(
    const QString &absoluteTrustDirectory)
{
    if (absoluteTrustDirectory.isEmpty() || !QDir::isAbsolutePath(absoluteTrustDirectory)) {
        return trustStoreError(
            QString::fromLatin1("the trust directory must be an explicit absolute path"));
    }

    const QString directoryPath = QDir::cleanPath(absoluteTrustDirectory);
    Utils::Result<QFileInfoList> initialEntries = trustDirectoryEntries(directoryPath);
    if (!initialEntries)
        return trustStoreError(initialEntries.error());
    const QList<TrustEntrySnapshot> initialSnapshot = snapshotEntries(*initialEntries);

    QList<EcpkgTrustedPublicKey> trustedKeys;
    trustedKeys.reserve(initialEntries->size());
    QSet<QByteArray> keyIds;
    for (const QFileInfo &entry : std::as_const(*initialEntries)) {
        Utils::Result<QByteArray> rawPublicKey = readPublicKey(entry);
        if (!rawPublicKey)
            return trustStoreError(rawPublicKey.error());

        const QByteArray keyId
            = QCryptographicHash::hash(*rawPublicKey, QCryptographicHash::Sha256);
        const QString expectedFileName
            = QString::fromLatin1(keyId.toHex()) + QString::fromLatin1(publicKeySuffix);
        if (entry.fileName() != expectedFileName) {
            return trustStoreError(
                QString::fromLatin1(
                    "a trusted public-key file name does not match its SHA-256 key ID"));
        }
        if (keyIds.contains(keyId)) {
            return trustStoreError(
                QString::fromLatin1("the trust directory contains a duplicate key ID"));
        }
        keyIds.insert(keyId);
        trustedKeys.append({std::move(*rawPublicKey), EcpkgTrustClass::Production});
    }

    Utils::Result<QFileInfoList> finalEntries = trustDirectoryEntries(directoryPath);
    if (!finalEntries || snapshotEntries(*finalEntries) != initialSnapshot) {
        return trustStoreError(
            QString::fromLatin1("the trust directory changed while it was being read"));
    }
    return trustedKeys;
}

} // namespace EtherCAT::SemanticRuntime::Internal
