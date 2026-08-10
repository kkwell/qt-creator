// Copyright (C) 2026 Embed Labs

#include "compilerruntimebootstrapprofile.h"

#include "compilerprovisioningprofile.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <optional>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_DARWIN
#include <sys/acl.h>
#endif
#endif

namespace EtherCAT::ProjectCompiler {

namespace {

constexpr qsizetype maximumExpectationBytes = 64 * 1024;
constexpr auto expectationFormat = "ethercat-ide-compiler-runtime-expectations-v1";

std::optional<Data::RuntimePackageCompilerSha256> sha256FromJson(const QJsonValue &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!value.isString() || !pattern.match(value.toString()).hasMatch())
        return std::nullopt;
    Data::RuntimePackageCompilerSha256 result(QByteArray::fromHex(value.toString().toLatin1()));
    return result.isValid() ? std::optional(result) : std::nullopt;
}

Data::RuntimePackageCompilerSha256 sha256(QByteArrayView bytes)
{
    return Data::RuntimePackageCompilerSha256(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
}

bool isSemanticVersion(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return pattern.match(value).hasMatch();
}

std::optional<Utils::FilePath> absoluteLocalPath(const QJsonValue &value)
{
    if (!value.isString())
        return std::nullopt;
    const Utils::FilePath path = Utils::FilePath::fromString(value.toString());
    if (!path.isAbsolutePath() || !path.scheme().isEmpty()
        || QDir::cleanPath(path.path()) != path.path()) {
        return std::nullopt;
    }
    return path;
}

bool isWithin(const Utils::FilePath &candidate, const Utils::FilePath &root)
{
    const QString canonicalCandidate = QFileInfo(candidate.path()).canonicalFilePath();
    const QString canonicalRoot = QFileInfo(root.path()).canonicalFilePath();
    return !canonicalCandidate.isEmpty() && !canonicalRoot.isEmpty()
           && (canonicalCandidate == canonicalRoot
               || canonicalCandidate.startsWith(canonicalRoot + QLatin1Char('/')));
}

bool isExactCanonicalPath(const Utils::FilePath &path)
{
    const QString canonical = QFileInfo(path.path()).canonicalFilePath();
    return !canonical.isEmpty() && QDir::cleanPath(canonical) == path.path();
}

bool rootsAreDisjoint(const QList<Utils::FilePath> &roots)
{
    for (qsizetype left = 0; left < roots.size(); ++left) {
        for (qsizetype right = left + 1; right < roots.size(); ++right) {
            if (isWithin(roots.at(left), roots.at(right))
                || isWithin(roots.at(right), roots.at(left))) {
                return false;
            }
        }
    }
    return true;
}

bool isNonzeroKey(const QByteArray &key)
{
    return key.size() == 32
           && std::any_of(key.cbegin(), key.cend(), [](char byte) { return byte != 0; });
}

#ifdef Q_OS_UNIX
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
            QStringLiteral("Cannot verify compiler runtime trust-input ACL: %1").arg(path));
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
                QStringLiteral("Cannot inspect compiler runtime trust-input ACL: %1").arg(path));
        }
        acl_tag_t tag = ACL_UNDEFINED_TAG;
        if (::acl_get_tag_type(entry, &tag) != 0 || tag != ACL_EXTENDED_DENY) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime trust input has a permissive ACL: %1").arg(path));
        }
        entryId = ACL_NEXT_ENTRY;
    }
}
#endif

Utils::Result<> inspectTrustPath(const QString &path, bool directory)
{
    struct stat metadata = {};
    if (::lstat(QFile::encodeName(path).constData(), &metadata) != 0
        || (directory ? !S_ISDIR(metadata.st_mode) : !S_ISREG(metadata.st_mode))
        || (metadata.st_uid != ::geteuid() && metadata.st_uid != 0)
        || (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0
        || (!directory && metadata.st_nlink != 1)) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime trust input has an unsafe path: %1").arg(path));
    }
#ifdef Q_OS_DARWIN
    if (const Utils::Result<> acl = inspectExtendedAcl(path); !acl)
        return acl;
#endif
    return Utils::ResultOk;
}

Utils::Result<> inspectTrustInput(const Utils::FilePath &file)
{
    const QString path = QDir::cleanPath(file.path());
    if (QFileInfo(path).canonicalFilePath() != path)
        return Utils::ResultError(QStringLiteral("Compiler runtime trust input uses a symlink."));
    if (const Utils::Result<> leaf = inspectTrustPath(path, false); !leaf)
        return leaf;
    QString current = QDir::cleanPath(QFileInfo(path).dir().absolutePath());
    while (true) {
        if (const Utils::Result<> ancestor = inspectTrustPath(current, true); !ancestor)
            return ancestor;
        const QString parent = QDir::cleanPath(QFileInfo(current).dir().absolutePath());
        if (parent == current)
            return Utils::ResultOk;
        current = parent;
    }
}
#else
Utils::Result<> inspectTrustInput(const Utils::FilePath &file)
{
    const QFileInfo info(file.path());
    if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != file.path()) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime trust input has an unsafe path."));
    }
    return Utils::ResultOk;
}
#endif

bool runtimeProfilesEqual(
    const CompilerRuntimeBundleProfile &left, const CompilerRuntimeBundleProfile &right)
{
    return left.bundleRoot() == right.bundleRoot()
           && left.compilerExecutable() == right.compilerExecutable()
           && left.provisionExecutable() == right.provisionExecutable()
           && left.verifyExecutable() == right.verifyExecutable()
           && left.selfTestExecutable() == right.selfTestExecutable()
           && left.runtimeRequirements() == right.runtimeRequirements()
           && left.compilerImportRoot() == right.compilerImportRoot()
           && left.expectation() == right.expectation() && left.identity() == right.identity();
}

bool pythonProfilesEqual(
    const CompilerPythonRuntimeProfile &left, const CompilerPythonRuntimeProfile &right)
{
    return left.companionRoot() == right.companionRoot()
           && left.runtimeRoot() == right.runtimeRoot()
           && left.pythonExecutable() == right.pythonExecutable()
           && left.runtimeIdentityFile() == right.runtimeIdentityFile()
           && left.expectation() == right.expectation() && left.identity() == right.identity();
}

} // namespace

Utils::Result<CompilerRuntimeBootstrapProfile> CompilerRuntimeBootstrapProfile::loadImpl(
    const Utils::FilePath &expectationFile)
{
    if (const Utils::Result<> safeExpectation = inspectTrustInput(expectationFile);
        !safeExpectation) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation profile is unavailable: %1")
                .arg(safeExpectation.error()));
    }
    const Utils::Result<QByteArray> expectationBytes
        = readProvisionedRegularLeaf(expectationFile, maximumExpectationBytes, false);
    if (!expectationBytes) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation profile is unavailable: %1")
                .arg(expectationBytes.error()));
    }
    if (!Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(*expectationBytes).isValid()) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation profile is not exact canonical ASCII JSON."));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*expectationBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation profile JSON is malformed."));
    }
    const QJsonObject object = document.object();
    static const QSet<QString> expectedKeys{
        QStringLiteral("compiler_bundle_root"),
        QStringLiteral("compiler_bundle_version"),
        QStringLiteral("compiler_manifest_sha256"),
        QStringLiteral("compiler_provisioning_profile_sha256"),
        QStringLiteral("compiler_release_public_key_path"),
        QStringLiteral("compiler_release_public_key_sha256"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("python_companion_bundle_sha256"),
        QStringLiteral("python_companion_manifest_sha256"),
        QStringLiteral("python_companion_root"),
        QStringLiteral("python_companion_version"),
        QStringLiteral("python_executable_sha256"),
        QStringLiteral("python_installed_tree_sha256"),
        QStringLiteral("python_portable_identity_sha256"),
        QStringLiteral("python_release_public_key_path"),
        QStringLiteral("python_release_public_key_sha256"),
        QStringLiteral("python_runtime_root"),
    };
    const QStringList actualKeys = object.keys();
    if (QSet<QString>(actualKeys.cbegin(), actualKeys.cend()) != expectedKeys
        || object.value(QStringLiteral("format")).toString()
               != QLatin1String(expectationFormat)
        || !object.value(QStringLiteral("format_version")).isDouble()
        || object.value(QStringLiteral("format_version")).toDouble() != 1) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation profile has an invalid shape."));
    }

    const QString compilerVersion = object.value(QStringLiteral("compiler_bundle_version")).toString();
    const QString pythonVersion = object.value(QStringLiteral("python_companion_version")).toString();
    const auto compilerRoot = absoluteLocalPath(object.value(QStringLiteral("compiler_bundle_root")));
    const auto companionRoot = absoluteLocalPath(object.value(QStringLiteral("python_companion_root")));
    const auto pythonRoot = absoluteLocalPath(object.value(QStringLiteral("python_runtime_root")));
    const auto compilerKeyFile = absoluteLocalPath(
        object.value(QStringLiteral("compiler_release_public_key_path")));
    const auto pythonKeyFile = absoluteLocalPath(
        object.value(QStringLiteral("python_release_public_key_path")));
    const auto compilerManifestSha = sha256FromJson(
        object.value(QStringLiteral("compiler_manifest_sha256")));
    const auto provisioningProfileSha = sha256FromJson(
        object.value(QStringLiteral("compiler_provisioning_profile_sha256")));
    const auto compilerKeySha = sha256FromJson(
        object.value(QStringLiteral("compiler_release_public_key_sha256")));
    const auto companionBundleSha = sha256FromJson(
        object.value(QStringLiteral("python_companion_bundle_sha256")));
    const auto companionManifestSha = sha256FromJson(
        object.value(QStringLiteral("python_companion_manifest_sha256")));
    const auto pythonExecutableSha = sha256FromJson(
        object.value(QStringLiteral("python_executable_sha256")));
    const auto installedTreeSha = sha256FromJson(
        object.value(QStringLiteral("python_installed_tree_sha256")));
    const auto portableIdentitySha = sha256FromJson(
        object.value(QStringLiteral("python_portable_identity_sha256")));
    const auto pythonKeySha = sha256FromJson(
        object.value(QStringLiteral("python_release_public_key_sha256")));
    if (!isSemanticVersion(compilerVersion) || !isSemanticVersion(pythonVersion) || !compilerRoot
        || !companionRoot || !pythonRoot || !compilerKeyFile || !pythonKeyFile
        || !compilerManifestSha || !provisioningProfileSha || !compilerKeySha || !companionBundleSha
        || !companionManifestSha || !pythonExecutableSha || !installedTreeSha
        || !portableIdentitySha || !pythonKeySha) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation profile identity is incomplete."));
    }

    const QList<Utils::FilePath> allPaths{
        expectationFile,
        *compilerRoot,
        *companionRoot,
        *pythonRoot,
        *compilerKeyFile,
        *pythonKeyFile,
    };
    if (!std::all_of(allPaths.cbegin(), allPaths.cend(), isExactCanonicalPath)) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation paths must exist without symlink ancestors."));
    }

    const QList<Utils::FilePath> runtimeRoots{*compilerRoot, *companionRoot, *pythonRoot};
    if (!rootsAreDisjoint(runtimeRoots)) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime expectation roots must be disjoint."));
    }
    for (const Utils::FilePath &root : runtimeRoots) {
        if (isWithin(expectationFile, root) || isWithin(*compilerKeyFile, root)
            || isWithin(*pythonKeyFile, root)) {
            return Utils::ResultError(
                QStringLiteral("Compiler runtime trust inputs must be outside signed trees."));
        }
    }
    if (*compilerKeyFile == *pythonKeyFile) {
        return Utils::ResultError(
            QStringLiteral("Compiler and Python runtime release keys must be independent."));
    }

    if (const Utils::Result<> safeCompilerKey = inspectTrustInput(*compilerKeyFile);
        !safeCompilerKey) {
        return Utils::ResultError(safeCompilerKey.error());
    }
    if (const Utils::Result<> safePythonKey = inspectTrustInput(*pythonKeyFile); !safePythonKey)
        return Utils::ResultError(safePythonKey.error());

    const Utils::Result<QByteArray> compilerKey
        = readProvisionedRegularLeaf(*compilerKeyFile, 32, false);
    const Utils::Result<QByteArray> pythonKey
        = readProvisionedRegularLeaf(*pythonKeyFile, 32, false);
    if (!compilerKey || !pythonKey) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime release key is unavailable: %1")
                .arg(!compilerKey ? compilerKey.error() : pythonKey.error()));
    }
    if (!isNonzeroKey(*compilerKey) || !isNonzeroKey(*pythonKey)
        || sha256(*compilerKey) != *compilerKeySha || sha256(*pythonKey) != *pythonKeySha
        || *compilerKey == *pythonKey) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime release key identity is invalid."));
    }

    const Utils::Result<CompilerRuntimeBundleProfile> compiler = CompilerRuntimeBundleProfile::load(
        *compilerRoot, {compilerVersion, *compilerKey, *compilerManifestSha});
    if (!compiler)
        return Utils::ResultError(compiler.error());
    const Utils::Result<CompilerPythonRuntimeProfile> python = CompilerPythonRuntimeProfile::load(
        *companionRoot,
        *pythonRoot,
        {pythonVersion,
         *pythonKey,
         *companionBundleSha,
         *companionManifestSha,
         *pythonExecutableSha,
         *installedTreeSha,
         *portableIdentitySha});
    if (!python)
        return Utils::ResultError(python.error());

    const Utils::Result<QByteArray> stableExpectations
        = readProvisionedRegularLeaf(expectationFile, maximumExpectationBytes, false);
    const Utils::Result<QByteArray> stableCompilerKey
        = readProvisionedRegularLeaf(*compilerKeyFile, 32, false);
    const Utils::Result<QByteArray> stablePythonKey
        = readProvisionedRegularLeaf(*pythonKeyFile, 32, false);
    const Utils::Result<> stableExpectationPath = inspectTrustInput(expectationFile);
    const Utils::Result<> stableCompilerKeyPath = inspectTrustInput(*compilerKeyFile);
    const Utils::Result<> stablePythonKeyPath = inspectTrustInput(*pythonKeyFile);
    if (!stableExpectations || !stableCompilerKey || !stablePythonKey || !stableExpectationPath
        || !stableCompilerKeyPath || !stablePythonKeyPath
        || *stableExpectations != *expectationBytes || *stableCompilerKey != *compilerKey
        || *stablePythonKey != *pythonKey) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime trust inputs changed during validation."));
    }

    CompilerRuntimeBootstrapProfile result;
    result.m_expectationFile = expectationFile;
    result.m_compilerReleaseKeyFile = *compilerKeyFile;
    result.m_pythonReleaseKeyFile = *pythonKeyFile;
    result.m_exactExpectationBytes = *expectationBytes;
    result.m_exactCompilerReleaseKey = *compilerKey;
    result.m_exactPythonReleaseKey = *pythonKey;
    result.m_provisioningProfileSha256 = *provisioningProfileSha;
    result.m_compilerRuntime = *compiler;
    result.m_pythonRuntime = *python;
    return result;
}

Utils::Result<CompilerRuntimeBootstrapProfile> CompilerRuntimeBootstrapProfile::load(
    const Utils::FilePath &expectationFile)
{
    return loadImpl(expectationFile);
}

Utils::Result<> CompilerRuntimeBootstrapProfile::validateCurrent() const
{
    const Utils::Result<CompilerRuntimeBootstrapProfile> current = load(m_expectationFile);
    if (!current)
        return Utils::ResultError(current.error());
    if (current->m_expectationFile != m_expectationFile
        || current->m_compilerReleaseKeyFile != m_compilerReleaseKeyFile
        || current->m_pythonReleaseKeyFile != m_pythonReleaseKeyFile
        || current->m_exactExpectationBytes != m_exactExpectationBytes
        || current->m_exactCompilerReleaseKey != m_exactCompilerReleaseKey
        || current->m_exactPythonReleaseKey != m_exactPythonReleaseKey
        || current->m_provisioningProfileSha256 != m_provisioningProfileSha256
        || !runtimeProfilesEqual(current->m_compilerRuntime, m_compilerRuntime)
        || !pythonProfilesEqual(current->m_pythonRuntime, m_pythonRuntime)) {
        return Utils::ResultError(
            QStringLiteral("Compiler runtime bootstrap identity changed after validation."));
    }
    return Utils::ResultOk;
}

Utils::FilePath CompilerRuntimeBootstrapProfile::expectationFile() const
{
    return m_expectationFile;
}

const Data::RuntimePackageCompilerSha256 &
CompilerRuntimeBootstrapProfile::provisioningProfileSha256() const
{
    return m_provisioningProfileSha256;
}

const CompilerRuntimeBundleProfile &CompilerRuntimeBootstrapProfile::compilerRuntime() const
{
    return m_compilerRuntime;
}

const CompilerPythonRuntimeProfile &CompilerRuntimeBootstrapProfile::pythonRuntime() const
{
    return m_pythonRuntime;
}

} // namespace EtherCAT::ProjectCompiler
