// Copyright (C) 2026 Embed Labs

#include "compilerruntimebundleprofile.h"

#include <monocypher-ed25519.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_DARWIN
#include <sys/acl.h>
#endif
#endif

namespace EtherCAT::ProjectCompiler {

namespace {

constexpr auto bundleFormat = "embedlabs-external-compiler-runtime-bundle-v1";
constexpr auto bundleId = "org.embedlabs.ethercat.project-compiler";
constexpr auto signatureDomain = "embedlabs-ethercat-compiler-runtime-bundle-v1";
constexpr auto compilerContractName = "ethercat-ide-project-compiler";
constexpr auto compilerImplementation = "ethercat-ide-project-compiler-1.0.0";
constexpr auto compilerPath = "bin/embedlabs-ecpkg-compiler";
constexpr auto provisionPath = "bin/embedlabs-ecpkg-compiler-provision";
constexpr auto verifyPath = "bin/embedlabs-ecpkg-compiler-verify";
constexpr auto selfTestPath = "bin/embedlabs-ecpkg-compiler-self-test";
constexpr auto requirementsPath
    = "runtime/igh_osless/contracts/compiler-runtime-requirements-v1.txt";
constexpr qsizetype maximumManifestBytes = 1024 * 1024;
constexpr qsizetype maximumSingleFileBytes = 16 * 1024 * 1024;
constexpr quint64 maximumTotalBytes = 32 * 1024 * 1024;
constexpr qsizetype maximumFiles = 256;
constexpr qsizetype maximumPathBytes = 240;

struct FileRecord
{
    QString path;
    quint64 bytes = 0;
    Data::RuntimePackageCompilerSha256 sha256;
    quint32 mode = 0;
};

struct ParsedManifest
{
    CompilerRuntimeBundleIdentity identity;
    QString compiler;
    QString provision;
    QString verify;
    QString selfTest;
    QString requirements;
    QList<FileRecord> files;
};

#ifdef Q_OS_UNIX
struct TreeEntry
{
    bool directory = false;
    quint64 device = 0;
    quint64 inode = 0;
    quint64 size = 0;
    quint64 mode = 0;
    quint64 links = 0;
    quint64 owner = 0;
    qint64 modifiedSeconds = 0;
    qint64 modifiedNanoseconds = 0;
    qint64 changedSeconds = 0;
    qint64 changedNanoseconds = 0;

    friend bool operator==(const TreeEntry &, const TreeEntry &) = default;
};
#endif

bool hasExactKeys(const QJsonObject &object, const QSet<QString> &keys)
{
    if (object.size() != keys.size())
        return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!keys.contains(it.key()))
            return false;
    }
    return true;
}

std::optional<quint64> unsignedInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble(-1);
    if (number < 0 || number > double(std::numeric_limits<qint64>::max())
        || number != quint64(number)) {
        return std::nullopt;
    }
    return quint64(number);
}

std::optional<Data::RuntimePackageCompilerSha256> sha256FromJson(const QJsonValue &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!value.isString() || !pattern.match(value.toString()).hasMatch())
        return std::nullopt;
    Data::RuntimePackageCompilerSha256 result(QByteArray::fromHex(value.toString().toLatin1()));
    return result.isValid() ? std::optional(result) : std::nullopt;
}

bool isSemanticVersion(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return pattern.match(value).hasMatch();
}

bool isSafeRelativePath(const QString &path)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]*$"));
    if (!pattern.match(path).hasMatch() || path.toUtf8().size() > maximumPathBytes
        || path.contains(QLatin1Char('\\')) || !QDir::isRelativePath(path)) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    return std::all_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return !part.isEmpty() && part != QLatin1String(".") && part != QLatin1String("..");
    });
}

QByteArray canonicalJson(const QJsonDocument &document)
{
    QByteArray result = document.toJson(QJsonDocument::Compact);
    result.append('\n');
    return result;
}

Data::RuntimePackageCompilerSha256 sha256(QByteArrayView bytes)
{
    return Data::RuntimePackageCompilerSha256(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
}

Utils::Result<ParsedManifest> parseManifest(
    const QByteArray &exactBytes, const CompilerRuntimeBundleExpectation &expectation)
{
    if (exactBytes.startsWith("\xef\xbb\xbf")
        || std::any_of(exactBytes.cbegin(), exactBytes.cend(), [](char value) {
               return quint8(value) > 0x7f;
           })) {
        return Utils::ResultError(QStringLiteral("Compiler runtime manifest is not ASCII JSON."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(exactBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Utils::ResultError(QStringLiteral("Compiler runtime manifest JSON is malformed."));
    }
    if (canonicalJson(document) != exactBytes) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime manifest is not exact canonical JSON."));
    }

    const QJsonObject root = document.object();
    static const QSet<QString> rootKeys{
        QStringLiteral("artifact_root_contract"),
        QStringLiteral("bundle_id"),
        QStringLiteral("bundle_version"),
        QStringLiteral("compiler_contract"),
        QStringLiteral("entrypoints"),
        QStringLiteral("files"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("limits"),
        QStringLiteral("runtime"),
        QStringLiteral("signature"),
    };
    if (!hasExactKeys(root, rootKeys)
        || root.value(QStringLiteral("format")).toString() != QLatin1String(bundleFormat)
        || root.value(QStringLiteral("format_version")).toInt() != 1
        || root.value(QStringLiteral("bundle_id")).toString() != QLatin1String(bundleId)
        || root.value(QStringLiteral("bundle_version")).toString()
               != expectation.bundleVersion) {
        return Utils::ResultError(QStringLiteral("Compiler runtime manifest identity is invalid."));
    }

    const QJsonObject contract = root.value(QStringLiteral("compiler_contract")).toObject();
    static const QSet<QString> contractKeys{
        QStringLiteral("ecpkg_versions"),
        QStringLiteral("implementation"),
        QStringLiteral("name"),
        QStringLiteral("version"),
    };
    const QJsonArray ecpkgVersions = contract.value(QStringLiteral("ecpkg_versions")).toArray();
    if (!hasExactKeys(contract, contractKeys)
        || contract.value(QStringLiteral("name")).toString()
               != QLatin1String(compilerContractName)
        || contract.value(QStringLiteral("version")).toInt() != 1
        || contract.value(QStringLiteral("implementation")).toString()
               != QLatin1String(compilerImplementation)
        || ecpkgVersions.size() != 1 || ecpkgVersions.at(0).toInt() != 2) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime compiler contract is unsupported."));
    }

    const QJsonObject runtime = root.value(QStringLiteral("runtime")).toObject();
    static const QSet<QString> runtimeKeys{
        QStringLiteral("implementation"),
        QStringLiteral("native_windows_supported"),
        QStringLiteral("platforms"),
        QStringLiteral("python_maximum_exclusive"),
        QStringLiteral("python_minimum"),
        QStringLiteral("requirements_path"),
        QStringLiteral("requirements_sha256"),
    };
    const QJsonArray platforms = runtime.value(QStringLiteral("platforms")).toArray();
    const auto requirementsSha = sha256FromJson(
        runtime.value(QStringLiteral("requirements_sha256")));
    if (!hasExactKeys(runtime, runtimeKeys)
        || runtime.value(QStringLiteral("implementation")).toString() != QLatin1String("CPython")
        || platforms.size() != 2 || platforms.at(0).toString() != QLatin1String("darwin")
        || platforms.at(1).toString() != QLatin1String("linux")
        || runtime.value(QStringLiteral("python_minimum")).toString() != QLatin1String("3.11.0")
        || runtime.value(QStringLiteral("python_maximum_exclusive")).toString()
               != QLatin1String("3.13.0")
        || runtime.value(QStringLiteral("requirements_path")).toString()
               != QLatin1String(requirementsPath)
        || !requirementsSha
        || runtime.value(QStringLiteral("native_windows_supported")).toBool(true)) {
        return Utils::ResultError(QStringLiteral("Compiler runtime host contract is invalid."));
    }

    const QJsonObject entrypoints = root.value(QStringLiteral("entrypoints")).toObject();
    static const QSet<QString> entrypointKeys{
        QStringLiteral("compiler"),
        QStringLiteral("provision"),
        QStringLiteral("self_test"),
        QStringLiteral("verify"),
    };
    if (!hasExactKeys(entrypoints, entrypointKeys)
        || entrypoints.value(QStringLiteral("compiler")).toString() != QLatin1String(compilerPath)
        || entrypoints.value(QStringLiteral("provision")).toString()
               != QLatin1String(provisionPath)
        || entrypoints.value(QStringLiteral("verify")).toString() != QLatin1String(verifyPath)
        || entrypoints.value(QStringLiteral("self_test")).toString()
               != QLatin1String(selfTestPath)) {
        return Utils::ResultError(QStringLiteral("Compiler runtime entrypoints are invalid."));
    }

    const QJsonObject artifactRoot
        = root.value(QStringLiteral("artifact_root_contract")).toObject();
    static const QSet<QString> artifactRootKeys{
        QStringLiteral("explicit_argument"),
        QStringLiteral("request_paths"),
        QStringLiteral("symlinks_allowed"),
        QStringLiteral("working_directory_independent"),
    };
    if (!hasExactKeys(artifactRoot, artifactRootKeys)
        || artifactRoot.value(QStringLiteral("explicit_argument")).toString()
               != QLatin1String("--artifact-root")
        || artifactRoot.value(QStringLiteral("request_paths")).toString()
               != QLatin1String("relative_to_artifact_root")
        || !artifactRoot.value(QStringLiteral("working_directory_independent")).toBool()
        || artifactRoot.value(QStringLiteral("symlinks_allowed")).toBool(true)) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime artifact-root contract is invalid."));
    }

    const QJsonObject limits = root.value(QStringLiteral("limits")).toObject();
    static const QSet<QString> limitKeys{
        QStringLiteral("max_files"),
        QStringLiteral("max_path_bytes"),
        QStringLiteral("max_single_file_bytes"),
        QStringLiteral("max_total_uncompressed_bytes"),
    };
    if (!hasExactKeys(limits, limitKeys)
        || limits.value(QStringLiteral("max_files")).toInt() != maximumFiles
        || limits.value(QStringLiteral("max_path_bytes")).toInt() != maximumPathBytes
        || limits.value(QStringLiteral("max_single_file_bytes")).toInt()
               != maximumSingleFileBytes
        || limits.value(QStringLiteral("max_total_uncompressed_bytes")).toInt()
               != maximumTotalBytes) {
        return Utils::ResultError(QStringLiteral("Compiler runtime limits are invalid."));
    }

    const QJsonObject signature = root.value(QStringLiteral("signature")).toObject();
    static const QSet<QString> signatureKeys{
        QStringLiteral("algorithm"),
        QStringLiteral("domain"),
        QStringLiteral("key_id"),
        QStringLiteral("signature_file"),
    };
    const auto manifestKeyId = sha256FromJson(signature.value(QStringLiteral("key_id")));
    const auto expectedKeyId = sha256(expectation.signingPublicKey);
    if (!hasExactKeys(signature, signatureKeys)
        || signature.value(QStringLiteral("algorithm")).toString() != QLatin1String("ed25519")
        || signature.value(QStringLiteral("domain")).toString() != QLatin1String(signatureDomain)
        || signature.value(QStringLiteral("signature_file")).toString()
               != QLatin1String("manifest.sig")
        || !manifestKeyId || *manifestKeyId != expectedKeyId) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime signing identity is invalid."));
    }

    const QJsonValue filesValue = root.value(QStringLiteral("files"));
    const QJsonArray files = filesValue.toArray();
    if (!filesValue.isArray() || files.isEmpty() || files.size() > maximumFiles) {
        return Utils::ResultError(QStringLiteral("Compiler runtime file list is invalid."));
    }
    QList<FileRecord> records;
    records.reserve(files.size());
    QSet<QString> paths;
    QSet<QString> caseFoldedPaths;
    quint64 totalBytes = 0;
    QString previousPath;
    for (const QJsonValue &value : files) {
        const QJsonObject object = value.toObject();
        static const QSet<QString> recordKeys{
            QStringLiteral("bytes"),
            QStringLiteral("mode"),
            QStringLiteral("path"),
            QStringLiteral("sha256"),
        };
        const QString path = object.value(QStringLiteral("path")).toString();
        const auto bytes = unsignedInteger(object.value(QStringLiteral("bytes")));
        const auto mode = unsignedInteger(object.value(QStringLiteral("mode")));
        const auto digest = sha256FromJson(object.value(QStringLiteral("sha256")));
        const QString folded = path.toCaseFolded();
        if (!hasExactKeys(object, recordKeys) || !isSafeRelativePath(path) || !bytes
            || *bytes > maximumSingleFileBytes || !mode || (*mode != 0644 && *mode != 0755)
            || !digest || path == QLatin1String("manifest.json")
            || path == QLatin1String("manifest.sig") || paths.contains(path)
            || caseFoldedPaths.contains(folded)
            || (!previousPath.isEmpty() && previousPath >= path)
            || totalBytes > maximumTotalBytes - *bytes) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime file record is invalid: %1").arg(path));
        }
        paths.insert(path);
        caseFoldedPaths.insert(folded);
        previousPath = path;
        totalBytes += *bytes;
        records.append({path, *bytes, *digest, quint32(*mode)});
    }
    if (totalBytes > maximumTotalBytes) {
        return Utils::ResultError(QStringLiteral("Compiler runtime payload is too large."));
    }

    const QMap<QString, quint32> requiredModes{
        {QString::fromLatin1(compilerPath), 0755},
        {QString::fromLatin1(provisionPath), 0755},
        {QString::fromLatin1(verifyPath), 0755},
        {QString::fromLatin1(selfTestPath), 0755},
        {QString::fromLatin1(requirementsPath), 0644},
        {QStringLiteral("trust/%1.pub").arg(QString::fromLatin1(expectedKeyId.value().toHex())),
         0644},
    };
    for (auto it = requiredModes.cbegin(); it != requiredModes.cend(); ++it) {
        const auto found = std::find_if(records.cbegin(), records.cend(), [&](const FileRecord &item) {
            return item.path == it.key() && item.mode == it.value();
        });
        if (found == records.cend()) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime required file is absent: %1").arg(it.key()));
        }
        if (it.key() == QLatin1String(requirementsPath) && found->sha256 != *requirementsSha) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime requirements identity is inconsistent."));
        }
    }

    ParsedManifest result;
    result.identity.bundleId = QString::fromLatin1(bundleId);
    result.identity.bundleVersion = expectation.bundleVersion;
    result.identity.compilerContractName = QString::fromLatin1(compilerContractName);
    result.identity.compilerContractVersion = 1;
    result.identity.compilerImplementation = QString::fromLatin1(compilerImplementation);
    result.identity.ecpkgVersions = {2};
    result.identity.manifestSha256 = sha256(exactBytes);
    result.identity.signingKeyId = expectedKeyId;
    result.identity.requirementsSha256 = *requirementsSha;
    result.identity.fileCount = records.size();
    result.identity.totalPayloadBytes = totalBytes;
    result.compiler = QString::fromLatin1(compilerPath);
    result.provision = QString::fromLatin1(provisionPath);
    result.verify = QString::fromLatin1(verifyPath);
    result.selfTest = QString::fromLatin1(selfTestPath);
    result.requirements = QString::fromLatin1(requirementsPath);
    result.files = std::move(records);
    return result;
}

#ifdef Q_OS_UNIX

TreeEntry treeEntry(const struct stat &metadata, bool directory)
{
    TreeEntry result;
    result.directory = directory;
    result.device = quint64(metadata.st_dev);
    result.inode = quint64(metadata.st_ino);
    result.size = quint64(metadata.st_size);
    result.mode = quint64(metadata.st_mode);
    result.links = quint64(metadata.st_nlink);
    result.owner = quint64(metadata.st_uid);
#ifdef Q_OS_DARWIN
    result.modifiedSeconds = metadata.st_mtimespec.tv_sec;
    result.modifiedNanoseconds = metadata.st_mtimespec.tv_nsec;
    result.changedSeconds = metadata.st_ctimespec.tv_sec;
    result.changedNanoseconds = metadata.st_ctimespec.tv_nsec;
#else
    result.modifiedSeconds = metadata.st_mtim.tv_sec;
    result.modifiedNanoseconds = metadata.st_mtim.tv_nsec;
    result.changedSeconds = metadata.st_ctim.tv_sec;
    result.changedNanoseconds = metadata.st_ctim.tv_nsec;
#endif
    return result;
}

#ifdef Q_OS_DARWIN
Utils::Result<> inspectExtendedAcl(const QString &path)
{
    errno = 0;
    acl_t accessControlList
        = ::acl_get_link_np(QFile::encodeName(path).constData(), ACL_TYPE_EXTENDED);
    if (!accessControlList) {
        if (errno == ENOENT)
            return Utils::ResultOk;
        return Utils::ResultError(
            QStringLiteral("Cannot verify compiler runtime path ACL: %1").arg(path));
    }
    struct AccessControlListCloser
    {
        acl_t accessControlList = nullptr;
        ~AccessControlListCloser() { ::acl_free(accessControlList); }
    } closer{accessControlList};

    acl_entry_t entry = nullptr;
    int entryId = ACL_FIRST_ENTRY;
    while (true) {
        errno = 0;
        if (::acl_get_entry(accessControlList, entryId, &entry) != 0) {
            if (errno == EINVAL)
                return Utils::ResultOk;
            return Utils::ResultError(
                QStringLiteral("Cannot inspect compiler runtime path ACL: %1").arg(path));
        }
        acl_tag_t tag = ACL_UNDEFINED_TAG;
        if (::acl_get_tag_type(entry, &tag) != 0 || tag != ACL_EXTENDED_DENY) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime path has a permissive ACL: %1").arg(path));
        }
        entryId = ACL_NEXT_ENTRY;
    }
}
#endif

Utils::Result<TreeEntry> inspectPath(const QString &path, bool expectDirectory)
{
    struct stat metadata = {};
    if (::lstat(QFile::encodeName(path).constData(), &metadata) != 0
        || (expectDirectory ? !S_ISDIR(metadata.st_mode) : !S_ISREG(metadata.st_mode))
        || (metadata.st_uid != ::geteuid() && metadata.st_uid != 0)
        || (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime tree contains an unsafe path: %1").arg(path));
    }
#ifdef Q_OS_DARWIN
    const Utils::Result<> acl = inspectExtendedAcl(path);
    if (!acl)
        return Utils::ResultError(acl.error());
#endif
    if (expectDirectory) {
        constexpr mode_t unsafeBits = S_ISUID | S_ISGID | S_ISVTX;
        constexpr mode_t requiredBits = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((metadata.st_mode & unsafeBits) != 0
            || (metadata.st_mode & requiredBits) != requiredBits) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime directory permissions are unsafe: %1").arg(path));
        }
    } else if (metadata.st_nlink != 1) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime file has multiple hard links: %1").arg(path));
    }
    return treeEntry(metadata, expectDirectory);
}

Utils::Result<> inspectAncestorChain(const QString &root)
{
    QString current = root;
    while (true) {
        const Utils::Result<TreeEntry> inspected = inspectPath(current, true);
        if (!inspected)
            return Utils::ResultError(inspected.error());
        const QString parent = QDir::cleanPath(QFileInfo(current).dir().absolutePath());
        if (parent == current)
            break;
        current = parent;
    }
    return Utils::ResultOk;
}

Utils::Result<QByteArray> readRegularFile(
    const QString &path, qsizetype maximumBytes, quint32 expectedMode)
{
    const Utils::Result<TreeEntry> beforeResult = inspectPath(path, false);
    if (!beforeResult)
        return Utils::ResultError(beforeResult.error());
    const TreeEntry before = *beforeResult;
    if ((before.mode & 07777) != expectedMode || before.size > quint64(maximumBytes)) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime file mode or size is invalid: %1").arg(path));
    }
    int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(QFile::encodeName(path).constData(), flags);
    if (descriptor < 0) {
        return Utils::ResultError(
            QStringLiteral("Cannot securely open compiler runtime file: %1").arg(path));
    }
    struct DescriptorCloser
    {
        int descriptor = -1;
        ~DescriptorCloser() { ::close(descriptor); }
    } closer{descriptor};

    struct stat openedMetadata = {};
    if (::fstat(descriptor, &openedMetadata) != 0
        || treeEntry(openedMetadata, false) != before) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime file changed while opening: %1").arg(path));
    }
    QByteArray bytes(qsizetype(before.size), Qt::Uninitialized);
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count
            = ::read(descriptor, bytes.data() + offset, size_t(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            return Utils::ResultError(
                QStringLiteral("Cannot completely read compiler runtime file: %1").arg(path));
        }
        offset += qsizetype(count);
    }
    char trailing = 0;
    ssize_t trailingCount = -1;
    do {
        trailingCount = ::read(descriptor, &trailing, 1);
    } while (trailingCount < 0 && errno == EINTR);
    struct stat finalMetadata = {};
    if (trailingCount != 0 || ::fstat(descriptor, &finalMetadata) != 0
        || treeEntry(finalMetadata, false) != before) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime file changed while reading: %1").arg(path));
    }
    return bytes;
}

Utils::Result<QMap<QString, TreeEntry>> inspectTree(
    const QString &root, const QSet<QString> &expectedFiles, const QSet<QString> &expectedDirs)
{
    QMap<QString, TreeEntry> entries;
    const Utils::Result<TreeEntry> rootEntry = inspectPath(root, true);
    if (!rootEntry)
        return Utils::ResultError(rootEntry.error());
    entries.insert(QString(), *rootEntry);

    QSet<QString> actualFiles;
    QSet<QString> actualDirs;
    QSet<QString> foldedPaths;
    const qsizetype maximumEntries = expectedFiles.size() + expectedDirs.size();
    QDirIterator iterator(
        root,
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    const QDir rootDirectory(root);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = QDir::fromNativeSeparators(rootDirectory.relativeFilePath(absolute));
        if (!isSafeRelativePath(relative) || foldedPaths.contains(relative.toCaseFolded())) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime tree contains a colliding path: %1").arg(relative));
        }
        foldedPaths.insert(relative.toCaseFolded());
        const QFileInfo information = iterator.fileInfo();
        if (information.isSymLink()) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime tree contains a symbolic link: %1").arg(relative));
        }
        const bool directory = information.isDir();
        if ((directory && !expectedDirs.contains(relative))
            || (!directory && !expectedFiles.contains(relative))
            || entries.size() > maximumEntries) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime installed tree is not the signed closed set."));
        }
        const Utils::Result<TreeEntry> metadata = inspectPath(absolute, directory);
        if (!metadata)
            return Utils::ResultError(metadata.error());
        entries.insert(relative, *metadata);
        (directory ? actualDirs : actualFiles).insert(relative);
    }
    if (actualFiles != expectedFiles || actualDirs != expectedDirs) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime installed tree is not the signed closed set."));
    }
    return entries;
}

#endif

QSet<QString> requiredDirectories(const QSet<QString> &files)
{
    QSet<QString> result;
    for (const QString &file : files) {
        QString path = file;
        while (path.contains(QLatin1Char('/'))) {
            path = path.left(path.lastIndexOf(QLatin1Char('/')));
            result.insert(path);
        }
    }
    return result;
}

} // namespace

Utils::Result<CompilerRuntimeBundleProfile> CompilerRuntimeBundleProfile::loadImpl(
    const Utils::FilePath &installedRoot, CompilerRuntimeBundleExpectation expectation)
{
#ifndef Q_OS_UNIX
    Q_UNUSED(installedRoot)
    Q_UNUSED(expectation)
    return Utils::ResultError(
        QStringLiteral("External compiler runtimes require a POSIX host."));
#else
    if (!expectation.isValid()) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime external trust expectation is invalid."));
    }
    if (!installedRoot.isAbsolutePath() || !installedRoot.scheme().isEmpty()) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime root is not an absolute local path."));
    }
    const QString root = QDir::cleanPath(installedRoot.path());
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty() || QDir::cleanPath(canonicalRoot) != root) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime root has a symbolic-link ancestor."));
    }
    const Utils::Result<> ancestors = inspectAncestorChain(root);
    if (!ancestors)
        return Utils::ResultError(ancestors.error());

    const QString manifestPath = QDir(root).filePath(QStringLiteral("manifest.json"));
    const QString signaturePath = QDir(root).filePath(QStringLiteral("manifest.sig"));
    const Utils::Result<QByteArray> manifestBytes
        = readRegularFile(manifestPath, maximumManifestBytes, 0644);
    const Utils::Result<QByteArray> signatureBytes = readRegularFile(signaturePath, 64, 0644);
    if (!manifestBytes)
        return Utils::ResultError(manifestBytes.error());
    if (!signatureBytes)
        return Utils::ResultError(signatureBytes.error());
    if (signatureBytes->size() != 64 || sha256(*manifestBytes) != expectation.manifestSha256) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime manifest or signature identity does not match."));
    }

    QByteArray signedBytes(signatureDomain);
    signedBytes.append('\0');
    signedBytes.append(*manifestBytes);
    const int signatureStatus = crypto_ed25519_check(
        reinterpret_cast<const std::uint8_t *>(signatureBytes->constData()),
        reinterpret_cast<const std::uint8_t *>(expectation.signingPublicKey.constData()),
        reinterpret_cast<const std::uint8_t *>(signedBytes.constData()),
        size_t(signedBytes.size()));
    if (signatureStatus != 0) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime manifest signature is invalid."));
    }

    const Utils::Result<ParsedManifest> parsed = parseManifest(*manifestBytes, expectation);
    if (!parsed)
        return Utils::ResultError(parsed.error());
    if (parsed->identity.manifestSha256 != expectation.manifestSha256) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime manifest digest is inconsistent."));
    }

    QSet<QString> expectedFiles{QStringLiteral("manifest.json"), QStringLiteral("manifest.sig")};
    QMap<QString, FileRecord> records;
    for (const FileRecord &record : parsed->files) {
        expectedFiles.insert(record.path);
        records.insert(record.path, record);
    }
    const QSet<QString> expectedDirs = requiredDirectories(expectedFiles);
    const Utils::Result<QMap<QString, TreeEntry>> before
        = inspectTree(root, expectedFiles, expectedDirs);
    if (!before)
        return Utils::ResultError(before.error());

    const Utils::Result<QByteArray> stableManifest
        = readRegularFile(manifestPath, maximumManifestBytes, 0644);
    const Utils::Result<QByteArray> stableSignature
        = readRegularFile(signaturePath, 64, 0644);
    if (!stableManifest || !stableSignature || *stableManifest != *manifestBytes
        || *stableSignature != *signatureBytes) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime manifest changed before payload validation."));
    }

    for (auto it = records.cbegin(); it != records.cend(); ++it) {
        const QString absolute = QDir(root).filePath(it.key());
        const Utils::Result<QByteArray> bytes
            = readRegularFile(absolute, maximumSingleFileBytes, it->mode);
        if (!bytes)
            return Utils::ResultError(bytes.error());
        if (quint64(bytes->size()) != it->bytes || sha256(*bytes) != it->sha256) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime file does not match its signed record: %1")
                    .arg(it.key()));
        }
    }

    const QString trustPath = QStringLiteral("trust/%1.pub")
                                  .arg(QString::fromLatin1(
                                      parsed->identity.signingKeyId.value().toHex()));
    const Utils::Result<QByteArray> trustCopy
        = readRegularFile(QDir(root).filePath(trustPath), 32, 0644);
    if (!trustCopy || *trustCopy != expectation.signingPublicKey) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime bundled key copy is inconsistent."));
    }

    const Utils::Result<QMap<QString, TreeEntry>> after
        = inspectTree(root, expectedFiles, expectedDirs);
    if (!after)
        return Utils::ResultError(after.error());
    if (*before != *after) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime tree changed while it was validated."));
    }

    CompilerRuntimeBundleProfile result;
    result.m_bundleRoot = Utils::FilePath::fromString(root);
    result.m_compilerExecutable = result.m_bundleRoot.pathAppended(parsed->compiler);
    result.m_provisionExecutable = result.m_bundleRoot.pathAppended(parsed->provision);
    result.m_verifyExecutable = result.m_bundleRoot.pathAppended(parsed->verify);
    result.m_selfTestExecutable = result.m_bundleRoot.pathAppended(parsed->selfTest);
    result.m_runtimeRequirements = result.m_bundleRoot.pathAppended(parsed->requirements);
    result.m_expectation = std::move(expectation);
    result.m_identity = parsed->identity;
    return result;
#endif
}

bool CompilerRuntimeBundleExpectation::isValid() const
{
    return isSemanticVersion(bundleVersion) && signingPublicKey.size() == 32
           && manifestSha256.isValid();
}

bool CompilerRuntimeBundleIdentity::isValid() const
{
    return bundleId == QLatin1String(::EtherCAT::ProjectCompiler::bundleId)
           && isSemanticVersion(bundleVersion)
           && compilerContractName
                  == QLatin1String(::EtherCAT::ProjectCompiler::compilerContractName)
           && compilerContractVersion == 1
           && compilerImplementation
                  == QLatin1String(::EtherCAT::ProjectCompiler::compilerImplementation)
           && ecpkgVersions == QList<quint32>{2} && manifestSha256.isValid()
           && signingKeyId.isValid() && requirementsSha256.isValid() && fileCount > 0
           && fileCount <= maximumFiles && totalPayloadBytes <= maximumTotalBytes;
}

Utils::Result<CompilerRuntimeBundleProfile> CompilerRuntimeBundleProfile::load(
    const Utils::FilePath &installedRoot, CompilerRuntimeBundleExpectation expectation)
{
    return loadImpl(installedRoot, std::move(expectation));
}

Utils::Result<> CompilerRuntimeBundleProfile::validateCurrent() const
{
    const Utils::Result<CompilerRuntimeBundleProfile> current = load(m_bundleRoot, m_expectation);
    if (!current)
        return Utils::ResultError(current.error());
    if (current->m_identity != m_identity || current->m_bundleRoot != m_bundleRoot
        || current->m_compilerExecutable != m_compilerExecutable
        || current->m_provisionExecutable != m_provisionExecutable
        || current->m_verifyExecutable != m_verifyExecutable
        || current->m_selfTestExecutable != m_selfTestExecutable
        || current->m_runtimeRequirements != m_runtimeRequirements) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime identity changed after validation."));
    }
    return Utils::ResultOk;
}

Utils::FilePath CompilerRuntimeBundleProfile::bundleRoot() const
{
    return m_bundleRoot;
}

Utils::FilePath CompilerRuntimeBundleProfile::compilerExecutable() const
{
    return m_compilerExecutable;
}

Utils::FilePath CompilerRuntimeBundleProfile::provisionExecutable() const
{
    return m_provisionExecutable;
}

Utils::FilePath CompilerRuntimeBundleProfile::verifyExecutable() const
{
    return m_verifyExecutable;
}

Utils::FilePath CompilerRuntimeBundleProfile::selfTestExecutable() const
{
    return m_selfTestExecutable;
}

Utils::FilePath CompilerRuntimeBundleProfile::runtimeRequirements() const
{
    return m_runtimeRequirements;
}

const CompilerRuntimeBundleExpectation &CompilerRuntimeBundleProfile::expectation() const
{
    return m_expectation;
}

const CompilerRuntimeBundleIdentity &CompilerRuntimeBundleProfile::identity() const
{
    return m_identity;
}

} // namespace EtherCAT::ProjectCompiler
