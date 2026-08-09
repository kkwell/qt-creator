// Copyright (C) 2026 Embed Labs

#include "compilerpythonruntimeprofile.h"

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
#include <cmath>
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

constexpr auto companionFormat = "embedlabs-relocatable-macos-python-runtime-companion-v1";
constexpr auto companionId = "org.embedlabs.ethercat.project-compiler.python-runtime.relocatable";
constexpr auto signatureDomain = "embedlabs-ethercat-macos-python-relocatable-runtime-companion-v1";
constexpr auto identityFormat = "embedlabs-installed-relocatable-python-runtime-identity-v1";
constexpr auto runtimeTreeFormat = "embedlabs-relocatable-python-tree-v1";
constexpr auto pythonRelativePath = "python/bin/python3.11";
constexpr auto identityRelativePath = "runtime-identity-v1.json";
constexpr qsizetype maximumManifestBytes = 1024 * 1024;
constexpr qsizetype maximumIdentityBytes = 64 * 1024;
constexpr qsizetype maximumCompanionFileBytes = 32 * 1024 * 1024;
constexpr quint64 maximumCompanionBytes = 48 * 1024 * 1024;
constexpr qsizetype maximumCompanionFiles = 64;
constexpr qsizetype maximumRuntimeFileBytes = 32 * 1024 * 1024;
constexpr quint64 maximumRuntimeBytes = 256 * 1024 * 1024;
constexpr qsizetype maximumRuntimeEntries = 16384;
constexpr qsizetype maximumRuntimeFilesystemEntries = 32768;
constexpr qsizetype maximumPathBytes = 512;

struct FileRecord
{
    QString path;
    quint64 bytes = 0;
    Data::RuntimePackageCompilerSha256 sha256;
    quint32 mode = 0;
};

struct ArtifactRecord
{
    QString path;
    quint64 bytes = 0;
    Data::RuntimePackageCompilerSha256 sha256;
};

struct ParsedCompanion
{
    QMap<QString, FileRecord> files;
    QString runtimeTreeManifestPath;
    ArtifactRecord runtimeArchive;
    Data::RuntimePackageCompilerSha256 runtimeTreeSha256;
    QString runtimeReleaseTag;
    Data::RuntimePackageCompilerSha256 wheelLockSha256;
    Data::RuntimePackageCompilerSha256 wheelhouseContentSha256;
    Data::RuntimePackageCompilerSha256 signingKeyId;
};

struct ParsedRuntimeTree
{
    QMap<QString, QString> symlinks;
    Data::RuntimePackageCompilerSha256 treeSha256;
};

#ifdef Q_OS_UNIX
enum class PathKind {
    Directory,
    RegularFile,
    SymbolicLink,
};

struct TreeEntry
{
    PathKind kind = PathKind::RegularFile;
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

struct RuntimeTreeInspection
{
    QMap<QString, TreeEntry> snapshot;
    Data::RuntimePackageCompilerSha256 treeSha256;
    qsizetype entries = 0;
    quint64 fileBytes = 0;
};

struct HashedFile
{
    quint64 bytes = 0;
    Data::RuntimePackageCompilerSha256 sha256;
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

bool isSemanticVersion(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return pattern.match(value).hasMatch();
}

std::optional<quint64> unsignedInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble(-1);
    if (!std::isfinite(number) || number < 0 || number > double(std::numeric_limits<qint64>::max())
        || number != std::floor(number)) {
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

Data::RuntimePackageCompilerSha256 sha256(QByteArrayView bytes)
{
    return Data::RuntimePackageCompilerSha256(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
}

bool isSafeRelativePath(const QString &path, bool runtimePath)
{
    static const QRegularExpression companionPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+/-]*$"));
    static const QRegularExpression runtimePattern(
        QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._+()/ -]*$"));
    const QRegularExpression &pattern = runtimePath ? runtimePattern : companionPattern;
    if (!pattern.match(path).hasMatch() || path.toUtf8().size() > maximumPathBytes
        || path.contains(QLatin1Char('\\')) || !QDir::isRelativePath(path)) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    return std::all_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return !part.isEmpty() && part != QLatin1String(".") && part != QLatin1String("..");
    });
}

bool isConfinedSymlink(const QString &linkPath, const QString &target)
{
    if (target.isEmpty() || target.contains(QLatin1Char('\\')) || !QDir::isRelativePath(target)
        || target.toUtf8().size() > maximumPathBytes) {
        return false;
    }
    QStringList resolved
        = linkPath.section(QLatin1Char('/'), 0, -2).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList targetParts = target.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &part : targetParts) {
        if (part.isEmpty() || part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (resolved.isEmpty())
                return false;
            resolved.removeLast();
        } else {
            if (!isSafeRelativePath(part, true) || part.contains(QLatin1Char('/')))
                return false;
            resolved.append(part);
        }
    }
    return !resolved.isEmpty();
}

void appendEscapedAsciiString(QByteArray &output, const QString &value)
{
    static constexpr char hex[] = "0123456789abcdef";
    output.append('"');
    for (QChar character : value) {
        const ushort code = character.unicode();
        switch (code) {
        case '"':
            output.append("\\\"");
            break;
        case '\\':
            output.append("\\\\");
            break;
        case '\b':
            output.append("\\b");
            break;
        case '\t':
            output.append("\\t");
            break;
        case '\n':
            output.append("\\n");
            break;
        case '\f':
            output.append("\\f");
            break;
        case '\r':
            output.append("\\r");
            break;
        default:
            if (code >= 0x20 && code <= 0x7e) {
                output.append(char(code));
            } else {
                output.append("\\u");
                output.append(hex[(code >> 12) & 0xf]);
                output.append(hex[(code >> 8) & 0xf]);
                output.append(hex[(code >> 4) & 0xf]);
                output.append(hex[code & 0xf]);
            }
            break;
        }
    }
    output.append('"');
}

bool appendCanonicalAsciiJson(QByteArray &output, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
        output.append("null");
        return true;
    case QJsonValue::Bool:
        output.append(value.toBool() ? "true" : "false");
        return true;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number != std::floor(number)
            || number < double(std::numeric_limits<qint64>::min())
            || number > double(std::numeric_limits<qint64>::max())) {
            return false;
        }
        output.append(QByteArray::number(qint64(number)));
        return true;
    }
    case QJsonValue::String:
        appendEscapedAsciiString(output, value.toString());
        return true;
    case QJsonValue::Array: {
        output.append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index != 0)
                output.append(',');
            if (!appendCanonicalAsciiJson(output, array.at(index)))
                return false;
        }
        output.append(']');
        return true;
    }
    case QJsonValue::Object: {
        output.append('{');
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        for (qsizetype index = 0; index < keys.size(); ++index) {
            if (index != 0)
                output.append(',');
            appendEscapedAsciiString(output, keys.at(index));
            output.append(':');
            if (!appendCanonicalAsciiJson(output, object.value(keys.at(index))))
                return false;
        }
        output.append('}');
        return true;
    }
    case QJsonValue::Undefined:
        return false;
    }
    return false;
}

Utils::Result<QByteArray> canonicalAsciiJson(const QJsonValue &value)
{
    QByteArray result;
    if (!appendCanonicalAsciiJson(result, value))
        return Utils::ResultError(QStringLiteral("Runtime identity contains a non-integer number."));
    result.append('\n');
    return result;
}

Utils::Result<QJsonObject> parseCanonicalObject(const QByteArray &bytes, const QString &label)
{
    if (!Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(bytes).isValid()) {
        return Utils::ResultError(
            QStringLiteral("%1 is not exact Python canonical ASCII JSON.").arg(label));
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return Utils::ResultError(QStringLiteral("%1 JSON is malformed.").arg(label));
    return document.object();
}

std::optional<ArtifactRecord> artifactRecord(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    static const QSet<QString>
        keys{QStringLiteral("bytes"), QStringLiteral("path"), QStringLiteral("sha256")};
    const QString path = object.value(QStringLiteral("path")).toString();
    const auto bytes = unsignedInteger(object.value(QStringLiteral("bytes")));
    const auto digest = sha256FromJson(object.value(QStringLiteral("sha256")));
    if (!hasExactKeys(object, keys) || !isSafeRelativePath(path, false) || !bytes || *bytes == 0
        || *bytes > quint64(maximumCompanionFileBytes) || !digest) {
        return std::nullopt;
    }
    return ArtifactRecord{path, *bytes, *digest};
}

bool artifactMatchesFile(
    const ArtifactRecord &artifact, const QMap<QString, FileRecord> &files, quint32 mode)
{
    const auto found = files.constFind(artifact.path);
    return found != files.cend() && found->bytes == artifact.bytes
           && found->sha256 == artifact.sha256 && found->mode == mode;
}

Utils::Result<ParsedCompanion> parseCompanionManifest(
    const QByteArray &bytes, const CompilerPythonRuntimeExpectation &expectation)
{
    const Utils::Result<QJsonObject> parsed
        = parseCanonicalObject(bytes, QStringLiteral("Python runtime companion manifest"));
    if (!parsed)
        return Utils::ResultError(parsed.error());
    const QJsonObject root = *parsed;
    static const QSet<QString> rootKeys{
        QStringLiteral("companion_id"),
        QStringLiteral("companion_version"),
        QStringLiteral("contract"),
        QStringLiteral("files"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("host_runtime"),
        QStringLiteral("identity_fields"),
        QStringLiteral("installation"),
        QStringLiteral("limits"),
        QStringLiteral("signature"),
        QStringLiteral("target"),
        QStringLiteral("wheelhouse")};
    if (!hasExactKeys(root, rootKeys)
        || root.value(QStringLiteral("format")).toString() != QLatin1String(companionFormat)
        || root.value(QStringLiteral("format_version")).toInt() != 1
        || root.value(QStringLiteral("companion_id")).toString() != QLatin1String(companionId)
        || root.value(QStringLiteral("companion_version")).toString()
               != expectation.companionVersion) {
        return Utils::ResultError(QStringLiteral("Python runtime companion identity is invalid."));
    }

    const QJsonObject target = root.value(QStringLiteral("target")).toObject();
    static const QSet<QString> targetKeys{
        QStringLiteral("machine"),
        QStringLiteral("minimum_macos_version"),
        QStringLiteral("operating_system"),
        QStringLiteral("python_implementation"),
        QStringLiteral("python_version")};
    if (!hasExactKeys(target, targetKeys)
        || target.value(QStringLiteral("operating_system")).toString() != QLatin1String("macos")
        || target.value(QStringLiteral("machine")).toString() != QLatin1String("arm64")
        || target.value(QStringLiteral("minimum_macos_version")).toString() != QLatin1String("11.0")
        || target.value(QStringLiteral("python_implementation")).toString()
               != QLatin1String("CPython")
        || target.value(QStringLiteral("python_version")).toString() != QLatin1String("3.11.15")) {
        return Utils::ResultError(QStringLiteral("Python runtime companion target is invalid."));
    }

    const QJsonObject limits = root.value(QStringLiteral("limits")).toObject();
    static const QSet<QString> limitKeys{
        QStringLiteral("max_bundle_file_bytes"),
        QStringLiteral("max_bundle_files"),
        QStringLiteral("max_bundle_uncompressed_bytes"),
        QStringLiteral("max_path_bytes"),
        QStringLiteral("max_runtime_file_bytes"),
        QStringLiteral("max_runtime_file_total_bytes"),
        QStringLiteral("max_runtime_members")};
    if (!hasExactKeys(limits, limitKeys)
        || unsignedInteger(limits.value(QStringLiteral("max_bundle_files")))
               != std::optional<quint64>(maximumCompanionFiles)
        || unsignedInteger(limits.value(QStringLiteral("max_bundle_uncompressed_bytes")))
               != std::optional<quint64>(maximumCompanionBytes)
        || unsignedInteger(limits.value(QStringLiteral("max_bundle_file_bytes")))
               != std::optional<quint64>(maximumCompanionFileBytes)
        || unsignedInteger(limits.value(QStringLiteral("max_runtime_members")))
               != std::optional<quint64>(4096)
        || unsignedInteger(limits.value(QStringLiteral("max_runtime_file_bytes")))
               != std::optional<quint64>(maximumRuntimeFileBytes)
        || unsignedInteger(limits.value(QStringLiteral("max_runtime_file_total_bytes")))
               != std::optional<quint64>(128 * 1024 * 1024)
        || unsignedInteger(limits.value(QStringLiteral("max_path_bytes")))
               != std::optional<quint64>(maximumPathBytes)) {
        return Utils::ResultError(QStringLiteral("Python runtime companion limits are invalid."));
    }

    const QJsonObject signature = root.value(QStringLiteral("signature")).toObject();
    static const QSet<QString> signatureKeys{
        QStringLiteral("algorithm"),
        QStringLiteral("domain"),
        QStringLiteral("key_id"),
        QStringLiteral("signature_file")};
    const auto keyId = sha256FromJson(signature.value(QStringLiteral("key_id")));
    const auto expectedKeyId = sha256(expectation.signingPublicKey);
    if (!hasExactKeys(signature, signatureKeys)
        || signature.value(QStringLiteral("algorithm")).toString() != QLatin1String("ed25519")
        || signature.value(QStringLiteral("domain")).toString() != QLatin1String(signatureDomain)
        || signature.value(QStringLiteral("signature_file")).toString()
               != QLatin1String("manifest.sig")
        || !keyId || *keyId != expectedKeyId) {
        return Utils::ResultError(
            QStringLiteral("Python runtime companion signing identity is invalid."));
    }

    const QJsonObject installation = root.value(QStringLiteral("installation")).toObject();
    static const QSet<QString> installationKeys{
        QStringLiteral("atomic_sibling_publish"),
        QStringLiteral("compiler_import_root"),
        QStringLiteral("creates_system_links"),
        QStringLiteral("destination_absolute_and_empty"),
        QStringLiteral("entrypoint"),
        QStringLiteral("forbidden_prefixes"),
        QStringLiteral("implicit_script_directory_allowed"),
        QStringLiteral("modifies_shell_profile"),
        QStringLiteral("network_allowed"),
        QStringLiteral("pip_configuration_allowed"),
        QStringLiteral("requires_root"),
        QStringLiteral("safe_path_required"),
        QStringLiteral("self_test"),
        QStringLiteral("user_site_allowed"),
        QStringLiteral("verifier"),
        QStringLiteral("writes_outside_destination_parent")};
    const QJsonArray forbidden = installation.value(QStringLiteral("forbidden_prefixes")).toArray();
    if (!hasExactKeys(installation, installationKeys)
        || installation.value(QStringLiteral("entrypoint")).toString()
               != QLatin1String("bin/embedlabs-python-relocatable-runtime-install")
        || installation.value(QStringLiteral("verifier")).toString()
               != QLatin1String("bin/embedlabs-python-relocatable-runtime-verify")
        || installation.value(QStringLiteral("self_test")).toString()
               != QLatin1String("bin/embedlabs-python-relocatable-runtime-self-test")
        || installation.value(QStringLiteral("compiler_import_root")).toString()
               != QLatin1String("externally-verified-api068-runtime-root/runtime/igh_osless/tools")
        || !installation.value(QStringLiteral("atomic_sibling_publish")).toBool()
        || !installation.value(QStringLiteral("destination_absolute_and_empty")).toBool()
        || !installation.value(QStringLiteral("safe_path_required")).toBool()
        || installation.value(QStringLiteral("creates_system_links")).toBool(true)
        || installation.value(QStringLiteral("implicit_script_directory_allowed")).toBool(true)
        || installation.value(QStringLiteral("modifies_shell_profile")).toBool(true)
        || installation.value(QStringLiteral("network_allowed")).toBool(true)
        || installation.value(QStringLiteral("pip_configuration_allowed")).toBool(true)
        || installation.value(QStringLiteral("requires_root")).toBool(true)
        || installation.value(QStringLiteral("user_site_allowed")).toBool(true)
        || installation.value(QStringLiteral("writes_outside_destination_parent")).toBool(true)
        || forbidden
               != QJsonArray{
                   QStringLiteral("/Library"),
                   QStringLiteral("/System"),
                   QStringLiteral("/usr"),
                   QStringLiteral("/bin"),
                   QStringLiteral("/sbin")}) {
        return Utils::ResultError(
            QStringLiteral("Python runtime companion installation policy is invalid."));
    }

    const QJsonArray identityFields = root.value(QStringLiteral("identity_fields")).toArray();
    const QStringList requiredIdentityFields{
        QStringLiteral("base_runtime_archive_sha256"),
        QStringLiteral("base_runtime_tree_sha256"),
        QStringLiteral("companion_bundle_sha256"),
        QStringLiteral("companion_id"),
        QStringLiteral("companion_key_id"),
        QStringLiteral("companion_manifest_format"),
        QStringLiteral("companion_manifest_format_version"),
        QStringLiteral("companion_manifest_sha256"),
        QStringLiteral("companion_version"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("identity_sha256"),
        QStringLiteral("installed_file_bytes"),
        QStringLiteral("installed_tree_entries"),
        QStringLiteral("installed_tree_sha256"),
        QStringLiteral("machine"),
        QStringLiteral("package_set_sha256"),
        QStringLiteral("platform"),
        QStringLiteral("python_cache_tag"),
        QStringLiteral("python_executable"),
        QStringLiteral("python_executable_sha256"),
        QStringLiteral("python_implementation"),
        QStringLiteral("python_soabi"),
        QStringLiteral("python_version"),
        QStringLiteral("runtime_release_tag"),
        QStringLiteral("runtime_root"),
        QStringLiteral("safe_path_policy"),
        QStringLiteral("signature_algorithm"),
        QStringLiteral("signature_domain"),
        QStringLiteral("sys_base_prefix"),
        QStringLiteral("sys_path_relative"),
        QStringLiteral("sys_path_sha256"),
        QStringLiteral("sys_prefix"),
        QStringLiteral("wheel_lock_sha256"),
        QStringLiteral("wheel_overlay_tree_sha256"),
        QStringLiteral("wheelhouse_content_sha256")};
    QJsonArray requiredIdentityFieldArray;
    for (const QString &field : requiredIdentityFields)
        requiredIdentityFieldArray.append(field);
    if (!root.value(QStringLiteral("identity_fields")).isArray()
        || identityFields != requiredIdentityFieldArray) {
        return Utils::ResultError(
            QStringLiteral("Python runtime identity field contract is invalid."));
    }

    const QJsonValue filesValue = root.value(QStringLiteral("files"));
    const QJsonArray fileValues = filesValue.toArray();
    if (!filesValue.isArray() || fileValues.isEmpty() || fileValues.size() > maximumCompanionFiles)
        return Utils::ResultError(QStringLiteral("Python runtime companion file list is invalid."));
    QMap<QString, FileRecord> files;
    QSet<QString> foldedPaths;
    QString previous;
    quint64 total = 0;
    for (const QJsonValue &value : fileValues) {
        const QJsonObject object = value.toObject();
        static const QSet<QString> keys{
            QStringLiteral("bytes"),
            QStringLiteral("mode"),
            QStringLiteral("path"),
            QStringLiteral("sha256")};
        const QString path = object.value(QStringLiteral("path")).toString();
        const auto fileBytes = unsignedInteger(object.value(QStringLiteral("bytes")));
        const auto mode = unsignedInteger(object.value(QStringLiteral("mode")));
        const auto digest = sha256FromJson(object.value(QStringLiteral("sha256")));
        if (!hasExactKeys(object, keys) || !isSafeRelativePath(path, false) || !fileBytes
            || *fileBytes > quint64(maximumCompanionFileBytes) || !mode
            || (*mode != 0644 && *mode != 0755) || !digest || files.contains(path)
            || path == QLatin1String("manifest.json") || path == QLatin1String("manifest.sig")
            || foldedPaths.contains(path.toCaseFolded())
            || (!previous.isEmpty() && previous >= path)
            || total > maximumCompanionBytes - *fileBytes) {
            return Utils::ResultError(
                QStringLiteral("Python runtime companion file record is invalid: %1").arg(path));
        }
        files.insert(path, {path, *fileBytes, *digest, quint32(*mode)});
        foldedPaths.insert(path.toCaseFolded());
        previous = path;
        total += *fileBytes;
    }

    const auto contract = artifactRecord(root.value(QStringLiteral("contract")));
    const QJsonObject host = root.value(QStringLiteral("host_runtime")).toObject();
    static const QSet<QString> hostKeys{
        QStringLiteral("archive"),
        QStringLiteral("binary_architectures"),
        QStringLiteral("cpython_license_sha256"),
        QStringLiteral("distribution"),
        QStringLiteral("python_executable"),
        QStringLiteral("release_commit"),
        QStringLiteral("release_tag"),
        QStringLiteral("repository"),
        QStringLiteral("runtime_members"),
        QStringLiteral("tree_manifest"),
        QStringLiteral("tree_sha256"),
        QStringLiteral("upstream_license")};
    const auto archive = artifactRecord(host.value(QStringLiteral("archive")));
    const auto treeManifest = artifactRecord(host.value(QStringLiteral("tree_manifest")));
    const auto upstreamLicense = artifactRecord(host.value(QStringLiteral("upstream_license")));
    const auto treeSha = sha256FromJson(host.value(QStringLiteral("tree_sha256")));
    const auto runtimeMembers = unsignedInteger(host.value(QStringLiteral("runtime_members")));
    if (!contract || !hasExactKeys(host, hostKeys) || !archive || !treeManifest || !upstreamLicense
        || !treeSha || !runtimeMembers || *runtimeMembers == 0 || *runtimeMembers > 4096
        || host.value(QStringLiteral("distribution")).toString()
               != QLatin1String("astral-sh/python-build-standalone install_only")
        || host.value(QStringLiteral("repository")).toString()
               != QLatin1String("https://github.com/astral-sh/python-build-standalone")
        || host.value(QStringLiteral("python_executable")).toString()
               != QLatin1String(pythonRelativePath)
        || host.value(QStringLiteral("release_tag")).toString().isEmpty()
        || !QRegularExpression(QStringLiteral("^[0-9a-f]{40}$"))
                .match(host.value(QStringLiteral("release_commit")).toString())
                .hasMatch()
        || host.value(QStringLiteral("binary_architectures")).toArray()
               != QJsonArray{QStringLiteral("arm64")}
        || !sha256FromJson(host.value(QStringLiteral("cpython_license_sha256")))) {
        return Utils::ResultError(QStringLiteral("Python runtime host manifest is invalid."));
    }

    const QJsonObject wheelhouse = root.value(QStringLiteral("wheelhouse")).toObject();
    static const QSet<QString> wheelhouseKeys{
        QStringLiteral("content_sha256"),
        QStringLiteral("directory"),
        QStringLiteral("distribution_count"),
        QStringLiteral("installation_mode"),
        QStringLiteral("lock"),
        QStringLiteral("manifest")};
    const auto lock = artifactRecord(wheelhouse.value(QStringLiteral("lock")));
    const auto wheelManifest = artifactRecord(wheelhouse.value(QStringLiteral("manifest")));
    const auto wheelContent = sha256FromJson(wheelhouse.value(QStringLiteral("content_sha256")));
    if (!hasExactKeys(wheelhouse, wheelhouseKeys) || !lock || !wheelManifest || !wheelContent
        || wheelhouse.value(QStringLiteral("directory")).toString() != QLatin1String("wheelhouse")
        || wheelhouse.value(QStringLiteral("distribution_count")).toInt() != 7
        || wheelhouse.value(QStringLiteral("installation_mode")).toString()
               != QLatin1String("verified-relative-site-overlay")) {
        return Utils::ResultError(QStringLiteral("Python runtime wheelhouse contract is invalid."));
    }

    const QList<ArtifactRecord> requiredArtifacts{
        *contract, *archive, *treeManifest, *upstreamLicense, *lock, *wheelManifest};
    for (const ArtifactRecord &artifact : requiredArtifacts) {
        if (!artifactMatchesFile(artifact, files, 0644)) {
            return Utils::ResultError(
                QStringLiteral("Python runtime companion artifact is inconsistent: %1")
                    .arg(artifact.path));
        }
    }
    const QMap<QString, quint32> requiredFiles{
        {QStringLiteral("bin/embedlabs-python-relocatable-runtime-install"), 0755},
        {QStringLiteral("bin/embedlabs-python-relocatable-runtime-self-test"), 0755},
        {QStringLiteral("bin/embedlabs-python-relocatable-runtime-verify"), 0755},
        {QStringLiteral("trust/%1.pub").arg(QString::fromLatin1(keyId->value().toHex())), 0644}};
    for (auto it = requiredFiles.cbegin(); it != requiredFiles.cend(); ++it) {
        if (!files.contains(it.key()) || files.value(it.key()).mode != it.value()) {
            return Utils::ResultError(
                QStringLiteral("Python runtime companion required file is absent: %1").arg(it.key()));
        }
    }

    ParsedCompanion result;
    result.files = std::move(files);
    result.runtimeTreeManifestPath = treeManifest->path;
    result.runtimeArchive = *archive;
    result.runtimeTreeSha256 = *treeSha;
    result.runtimeReleaseTag = host.value(QStringLiteral("release_tag")).toString();
    result.wheelLockSha256 = lock->sha256;
    result.wheelhouseContentSha256 = *wheelContent;
    result.signingKeyId = *keyId;
    return result;
}

Utils::Result<ParsedRuntimeTree> parseRuntimeTree(
    const QByteArray &bytes,
    const ParsedCompanion &companion,
    const CompilerPythonRuntimeExpectation &expectation)
{
    const Utils::Result<QJsonObject> parsed
        = parseCanonicalObject(bytes, QStringLiteral("Python base runtime tree manifest"));
    if (!parsed)
        return Utils::ResultError(parsed.error());
    const QJsonObject root = *parsed;
    static const QSet<QString> rootKeys{
        QStringLiteral("archive"),
        QStringLiteral("counts"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("members"),
        QStringLiteral("root_prefix"),
        QStringLiteral("target"),
        QStringLiteral("tree_sha256")};
    const QJsonObject archive = root.value(QStringLiteral("archive")).toObject();
    static const QSet<QString> archiveKeys{
        QStringLiteral("bytes"),
        QStringLiteral("compression"),
        QStringLiteral("path"),
        QStringLiteral("sha256")};
    if (!hasExactKeys(root, rootKeys)
        || root.value(QStringLiteral("format")).toString() != QLatin1String(runtimeTreeFormat)
        || root.value(QStringLiteral("format_version")).toInt() != 1
        || root.value(QStringLiteral("root_prefix")).toString() != QLatin1String("python")
        || !hasExactKeys(archive, archiveKeys)
        || archive.value(QStringLiteral("path")).toString() != companion.runtimeArchive.path
        || unsignedInteger(archive.value(QStringLiteral("bytes")))
               != std::optional<quint64>(companion.runtimeArchive.bytes)
        || sha256FromJson(archive.value(QStringLiteral("sha256")))
               != std::optional(companion.runtimeArchive.sha256)
        || archive.value(QStringLiteral("compression")).toString() != QLatin1String("tar+gzip")) {
        return Utils::ResultError(QStringLiteral("Python base runtime tree identity is invalid."));
    }
    const QJsonObject target = root.value(QStringLiteral("target")).toObject();
    static const QSet<QString> targetKeys{
        QStringLiteral("cache_tag"),
        QStringLiteral("implementation"),
        QStringLiteral("machine"),
        QStringLiteral("minimum_macos_version"),
        QStringLiteral("operating_system"),
        QStringLiteral("python_version"),
        QStringLiteral("soabi")};
    if (!hasExactKeys(target, targetKeys)
        || target.value(QStringLiteral("operating_system")).toString() != QLatin1String("macos")
        || target.value(QStringLiteral("machine")).toString() != QLatin1String("arm64")
        || target.value(QStringLiteral("minimum_macos_version")).toString() != QLatin1String("11.0")
        || target.value(QStringLiteral("implementation")).toString() != QLatin1String("CPython")
        || target.value(QStringLiteral("python_version")).toString() != QLatin1String("3.11.15")
        || target.value(QStringLiteral("cache_tag")).toString() != QLatin1String("cpython-311")
        || target.value(QStringLiteral("soabi")).toString() != QLatin1String("cpython-311-darwin")) {
        return Utils::ResultError(QStringLiteral("Python base runtime target is invalid."));
    }

    const QJsonValue memberValue = root.value(QStringLiteral("members"));
    const QJsonArray members = memberValue.toArray();
    if (!memberValue.isArray() || members.isEmpty() || members.size() > 4096)
        return Utils::ResultError(QStringLiteral("Python base runtime member list is invalid."));
    QString previous;
    QSet<QString> paths;
    QSet<QString> foldedPaths;
    QMap<QString, QString> symlinks;
    qsizetype files = 0;
    quint64 fileBytes = 0;
    qsizetype machoFiles = 0;
    std::optional<Data::RuntimePackageCompilerSha256> pythonSha;
    for (const QJsonValue &value : members) {
        const QJsonObject record = value.toObject();
        const QString path = record.value(QStringLiteral("path")).toString();
        const QString type = record.value(QStringLiteral("type")).toString();
        const auto mtime = unsignedInteger(record.value(QStringLiteral("mtime")));
        if (!isSafeRelativePath(path, true) || !mtime || paths.contains(path)
            || foldedPaths.contains(path.toCaseFolded())
            || (!previous.isEmpty() && previous >= path)) {
            return Utils::ResultError(
                QStringLiteral("Python base runtime member is invalid: %1").arg(path));
        }
        if (type == QLatin1String("file")) {
            static const QSet<QString> fileKeys{
                QStringLiteral("archive_mode"),
                QStringLiteral("bytes"),
                QStringLiteral("installed_mode"),
                QStringLiteral("mtime"),
                QStringLiteral("path"),
                QStringLiteral("sha256"),
                QStringLiteral("type")};
            const auto bytesValue = unsignedInteger(record.value(QStringLiteral("bytes")));
            const auto archiveMode = unsignedInteger(record.value(QStringLiteral("archive_mode")));
            const auto installedMode = unsignedInteger(
                record.value(QStringLiteral("installed_mode")));
            const auto digest = sha256FromJson(record.value(QStringLiteral("sha256")));
            if (!hasExactKeys(record, fileKeys) || !bytesValue
                || *bytesValue > quint64(maximumRuntimeFileBytes) || !archiveMode
                || *archiveMode > 0777 || !installedMode
                || *installedMode != (*archiveMode & ~quint64(0022))
                || (*installedMode != 0644 && *installedMode != 0755) || !digest
                || fileBytes > quint64(128 * 1024 * 1024) - *bytesValue) {
                return Utils::ResultError(
                    QStringLiteral("Python base runtime file record is invalid: %1").arg(path));
            }
            ++files;
            fileBytes += *bytesValue;
            if (path == QLatin1String("bin/python3.11"))
                pythonSha = *digest;
            if (path.endsWith(QLatin1String(".dylib")) || path.endsWith(QLatin1String(".so"))
                || path == QLatin1String("bin/python3.11"))
                ++machoFiles;
        } else if (type == QLatin1String("symlink")) {
            static const QSet<QString> linkKeys{
                QStringLiteral("mtime"),
                QStringLiteral("path"),
                QStringLiteral("target"),
                QStringLiteral("type")};
            const QString targetValue = record.value(QStringLiteral("target")).toString();
            if (!hasExactKeys(record, linkKeys) || !isConfinedSymlink(path, targetValue)) {
                return Utils::ResultError(
                    QStringLiteral("Python base runtime symlink record is invalid: %1").arg(path));
            }
            symlinks.insert(QStringLiteral("python/") + path, targetValue);
        } else {
            return Utils::ResultError(
                QStringLiteral("Python base runtime member type is invalid: %1").arg(path));
        }
        paths.insert(path);
        foldedPaths.insert(path.toCaseFolded());
        previous = path;
    }
    if (!pythonSha || *pythonSha != expectation.pythonExecutableSha256)
        return Utils::ResultError(QStringLiteral("Signed Python executable identity is invalid."));

    const QJsonObject counts = root.value(QStringLiteral("counts")).toObject();
    static const QSet<QString> countKeys{
        QStringLiteral("file_bytes"),
        QStringLiteral("files"),
        QStringLiteral("macho_files"),
        QStringLiteral("members"),
        QStringLiteral("symlinks")};
    if (!hasExactKeys(counts, countKeys)
        || unsignedInteger(counts.value(QStringLiteral("members")))
               != std::optional<quint64>(members.size())
        || unsignedInteger(counts.value(QStringLiteral("files"))) != std::optional<quint64>(files)
        || unsignedInteger(counts.value(QStringLiteral("symlinks")))
               != std::optional<quint64>(symlinks.size())
        || unsignedInteger(counts.value(QStringLiteral("file_bytes")))
               != std::optional<quint64>(fileBytes)
        || unsignedInteger(counts.value(QStringLiteral("macho_files")))
               != std::optional<quint64>(machoFiles)) {
        return Utils::ResultError(QStringLiteral("Python base runtime counters are invalid."));
    }

    const QJsonObject projection{
        {QStringLiteral("format"),
         QStringLiteral("embedlabs-relocatable-python-tree-projection-v1")},
        {QStringLiteral("members"), members}};
    const Utils::Result<QByteArray> projectionBytes = canonicalAsciiJson(projection);
    const auto declaredTreeSha = sha256FromJson(root.value(QStringLiteral("tree_sha256")));
    if (!projectionBytes || !declaredTreeSha || sha256(*projectionBytes) != *declaredTreeSha
        || *declaredTreeSha != companion.runtimeTreeSha256) {
        return Utils::ResultError(QStringLiteral("Python base runtime tree digest is invalid."));
    }
    return ParsedRuntimeTree{std::move(symlinks), *declaredTreeSha};
}

#ifdef Q_OS_UNIX

TreeEntry treeEntry(const struct stat &metadata, PathKind kind)
{
    TreeEntry result;
    result.kind = kind;
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
    acl_t acl = ::acl_get_link_np(QFile::encodeName(path).constData(), ACL_TYPE_EXTENDED);
    if (!acl) {
        if (errno == ENOENT)
            return Utils::ResultOk;
        return Utils::ResultError(
            QStringLiteral("Cannot verify trusted Python runtime path ACL: %1").arg(path));
    }
    struct AclCloser
    {
        acl_t acl = nullptr;
        ~AclCloser() { ::acl_free(acl); }
    } closer{acl};
    acl_entry_t entry = nullptr;
    int entryId = ACL_FIRST_ENTRY;
    while (true) {
        errno = 0;
        if (::acl_get_entry(acl, entryId, &entry) != 0) {
            if (errno == EINVAL)
                return Utils::ResultOk;
            return Utils::ResultError(
                QStringLiteral("Cannot inspect trusted Python runtime path ACL: %1").arg(path));
        }
        acl_tag_t tag = ACL_UNDEFINED_TAG;
        if (::acl_get_tag_type(entry, &tag) != 0 || tag != ACL_EXTENDED_DENY) {
            return Utils::ResultError(
                QStringLiteral("Trusted Python runtime path has a permissive ACL: %1").arg(path));
        }
        entryId = ACL_NEXT_ENTRY;
    }
}
#endif

Utils::Result<TreeEntry> inspectPath(const QString &path, PathKind expectedKind)
{
    struct stat metadata = {};
    if (::lstat(QFile::encodeName(path).constData(), &metadata) != 0)
        return Utils::ResultError(
            QStringLiteral("Trusted Python runtime path is absent: %1").arg(path));
    const bool kindMatches = (expectedKind == PathKind::Directory && S_ISDIR(metadata.st_mode))
                             || (expectedKind == PathKind::RegularFile && S_ISREG(metadata.st_mode))
                             || (expectedKind == PathKind::SymbolicLink
                                 && S_ISLNK(metadata.st_mode));
    if (!kindMatches || (metadata.st_uid != ::geteuid() && metadata.st_uid != 0)) {
        return Utils::ResultError(
            QStringLiteral("Trusted Python runtime path type or owner is unsafe: %1").arg(path));
    }
#ifdef Q_OS_DARWIN
    const Utils::Result<> acl = inspectExtendedAcl(path);
    if (!acl)
        return Utils::ResultError(acl.error());
#endif
    if (expectedKind == PathKind::Directory) {
        constexpr mode_t unsafe = S_ISUID | S_ISGID | S_ISVTX | S_IWGRP | S_IWOTH;
        constexpr mode_t required = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((metadata.st_mode & unsafe) != 0 || (metadata.st_mode & required) != required) {
            return Utils::ResultError(
                QStringLiteral("Trusted Python runtime directory permissions are unsafe: %1")
                    .arg(path));
        }
    } else if (expectedKind == PathKind::RegularFile) {
        if ((metadata.st_mode & (S_IWGRP | S_IWOTH | S_ISUID | S_ISGID | S_ISVTX)) != 0
            || metadata.st_nlink != 1) {
            return Utils::ResultError(
                QStringLiteral("Trusted Python runtime file permissions or links are unsafe: %1")
                    .arg(path));
        }
    } else if (metadata.st_nlink != 1) {
        return Utils::ResultError(
            QStringLiteral("Trusted Python runtime symlink has multiple links: %1").arg(path));
    }
    return treeEntry(metadata, expectedKind);
}

Utils::Result<> inspectAncestorChain(const QString &root)
{
    QString current = root;
    while (true) {
        const Utils::Result<TreeEntry> inspected = inspectPath(current, PathKind::Directory);
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
    const Utils::Result<TreeEntry> beforeResult = inspectPath(path, PathKind::RegularFile);
    if (!beforeResult)
        return Utils::ResultError(beforeResult.error());
    const TreeEntry before = *beforeResult;
    if ((before.mode & 07777) != expectedMode || before.size > quint64(maximumBytes)) {
        return Utils::ResultError(
            QStringLiteral("Trusted Python runtime file mode or size is invalid: %1").arg(path));
    }
    int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(QFile::encodeName(path).constData(), flags);
    if (descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot securely open file: %1").arg(path));
    struct DescriptorCloser
    {
        int descriptor = -1;
        ~DescriptorCloser() { ::close(descriptor); }
    } closer{descriptor};
    struct stat opened = {};
    if (::fstat(descriptor, &opened) != 0 || treeEntry(opened, PathKind::RegularFile) != before) {
        return Utils::ResultError(QStringLiteral("File changed while opening: %1").arg(path));
    }
    QByteArray bytes(qsizetype(before.size), Qt::Uninitialized);
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count
            = ::read(descriptor, bytes.data() + offset, size_t(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return Utils::ResultError(QStringLiteral("Cannot completely read file: %1").arg(path));
        offset += qsizetype(count);
    }
    char trailing = 0;
    ssize_t trailingCount = -1;
    do {
        trailingCount = ::read(descriptor, &trailing, 1);
    } while (trailingCount < 0 && errno == EINTR);
    struct stat finalMetadata = {};
    if (trailingCount != 0 || ::fstat(descriptor, &finalMetadata) != 0
        || treeEntry(finalMetadata, PathKind::RegularFile) != before) {
        return Utils::ResultError(QStringLiteral("File changed while reading: %1").arg(path));
    }
    return bytes;
}

Utils::Result<HashedFile> hashRegularFile(
    const QString &path, qsizetype maximumBytes, quint32 expectedMode)
{
    const Utils::Result<TreeEntry> beforeResult = inspectPath(path, PathKind::RegularFile);
    if (!beforeResult)
        return Utils::ResultError(beforeResult.error());
    const TreeEntry before = *beforeResult;
    if ((before.mode & 07777) != expectedMode || before.size > quint64(maximumBytes)) {
        return Utils::ResultError(
            QStringLiteral("Trusted Python runtime file mode or size is invalid: %1").arg(path));
    }
    int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(QFile::encodeName(path).constData(), flags);
    if (descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot securely open file: %1").arg(path));
    struct DescriptorCloser
    {
        int descriptor = -1;
        ~DescriptorCloser() { ::close(descriptor); }
    } closer{descriptor};
    struct stat opened = {};
    if (::fstat(descriptor, &opened) != 0 || treeEntry(opened, PathKind::RegularFile) != before) {
        return Utils::ResultError(QStringLiteral("File changed while opening: %1").arg(path));
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    quint64 total = 0;
    while (true) {
        ssize_t count = ::read(descriptor, buffer.data(), size_t(buffer.size()));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            return Utils::ResultError(QStringLiteral("Cannot hash file: %1").arg(path));
        if (count == 0)
            break;
        if (total > quint64(maximumBytes) - quint64(count))
            return Utils::ResultError(QStringLiteral("File grew while hashing: %1").arg(path));
        total += quint64(count);
        digest.addData(QByteArrayView(buffer.constData(), qsizetype(count)));
    }
    struct stat finalMetadata = {};
    if (total != before.size || ::fstat(descriptor, &finalMetadata) != 0
        || treeEntry(finalMetadata, PathKind::RegularFile) != before) {
        return Utils::ResultError(QStringLiteral("File changed while hashing: %1").arg(path));
    }
    return HashedFile{total, Data::RuntimePackageCompilerSha256(digest.result())};
}

Utils::Result<QString> readSymbolicLink(const QString &path)
{
    const Utils::Result<TreeEntry> beforeResult = inspectPath(path, PathKind::SymbolicLink);
    if (!beforeResult)
        return Utils::ResultError(beforeResult.error());
    const TreeEntry before = *beforeResult;
    if (before.size == 0 || before.size > quint64(maximumPathBytes))
        return Utils::ResultError(
            QStringLiteral("Symbolic-link target size is invalid: %1").arg(path));
    QByteArray target(qsizetype(before.size) + 1, '\0');
    const ssize_t count
        = ::readlink(QFile::encodeName(path).constData(), target.data(), size_t(target.size()));
    const Utils::Result<TreeEntry> after = inspectPath(path, PathKind::SymbolicLink);
    if (count < 0 || count != ssize_t(before.size) || !after || *after != before) {
        return Utils::ResultError(
            QStringLiteral("Symbolic link changed while reading: %1").arg(path));
    }
    target.resize(qsizetype(count));
    const QString decoded = QFile::decodeName(target);
    if (QFile::encodeName(decoded) != target)
        return Utils::ResultError(
            QStringLiteral("Symbolic-link target encoding is invalid: %1").arg(path));
    return decoded;
}

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

Utils::Result<QMap<QString, TreeEntry>> inspectCompanionTree(
    const QString &root, const QMap<QString, FileRecord> &records)
{
    QSet<QString> expectedFiles{QStringLiteral("manifest.json"), QStringLiteral("manifest.sig")};
    for (auto it = records.cbegin(); it != records.cend(); ++it)
        expectedFiles.insert(it.key());
    const QSet<QString> expectedDirectories = requiredDirectories(expectedFiles);
    QSet<QString> actualFiles;
    QSet<QString> actualDirectories;
    QSet<QString> folded;
    QMap<QString, TreeEntry> snapshot;
    const Utils::Result<TreeEntry> rootEntry = inspectPath(root, PathKind::Directory);
    if (!rootEntry)
        return Utils::ResultError(rootEntry.error());
    snapshot.insert(QString(), *rootEntry);
    const QDir rootDirectory(root);
    QDirIterator iterator(
        root,
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = QDir::fromNativeSeparators(
            rootDirectory.relativeFilePath(absolute));
        if (!isSafeRelativePath(relative, false) || folded.contains(relative.toCaseFolded())) {
            return Utils::ResultError(
                QStringLiteral("Python runtime companion has a colliding path: %1").arg(relative));
        }
        folded.insert(relative.toCaseFolded());
        const QFileInfo information = iterator.fileInfo();
        if (information.isSymLink())
            return Utils::ResultError(
                QStringLiteral("Python runtime companion contains a symlink."));
        const bool directory = information.isDir();
        if ((directory && !expectedDirectories.contains(relative))
            || (!directory && !expectedFiles.contains(relative))) {
            return Utils::ResultError(
                QStringLiteral("Python runtime companion is not the signed closed set."));
        }
        const Utils::Result<TreeEntry> entry
            = inspectPath(absolute, directory ? PathKind::Directory : PathKind::RegularFile);
        if (!entry)
            return Utils::ResultError(entry.error());
        snapshot.insert(relative, *entry);
        (directory ? actualDirectories : actualFiles).insert(relative);
    }
    if (actualFiles != expectedFiles || actualDirectories != expectedDirectories)
        return Utils::ResultError(
            QStringLiteral("Python runtime companion is not the signed closed set."));
    return snapshot;
}

Utils::Result<RuntimeTreeInspection> inspectRuntimeTree(
    const QString &root, const QMap<QString, QString> &signedSymlinks)
{
    const Utils::Result<TreeEntry> rootEntry = inspectPath(root, PathKind::Directory);
    if (!rootEntry)
        return Utils::ResultError(rootEntry.error());
    RuntimeTreeInspection result;
    result.snapshot.insert(QString(), *rootEntry);
    QMap<QString, QJsonObject> records;
    QSet<QString> actualSymlinks;
    QSet<QString> folded;
    QMap<QString, qsizetype> children;
    children.insert(QString(), 0);
    const QDir rootDirectory(root);
    QDirIterator iterator(
        root,
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = QDir::fromNativeSeparators(
            rootDirectory.relativeFilePath(absolute));
        if (!isSafeRelativePath(relative, true) || folded.contains(relative.toCaseFolded())
            || result.snapshot.size() >= maximumRuntimeFilesystemEntries) {
            return Utils::ResultError(
                QStringLiteral("Installed Python runtime has an invalid path: %1").arg(relative));
        }
        folded.insert(relative.toCaseFolded());
        const QString parent = relative.contains(QLatin1Char('/'))
                                   ? relative.left(relative.lastIndexOf(QLatin1Char('/')))
                                   : QString();
        children[parent] = children.value(parent) + 1;
        const QFileInfo information = iterator.fileInfo();
        if (information.isSymLink()) {
            const Utils::Result<TreeEntry> entry = inspectPath(absolute, PathKind::SymbolicLink);
            const Utils::Result<QString> target = readSymbolicLink(absolute);
            if (!entry || !target || !isConfinedSymlink(relative, *target)
                || signedSymlinks.value(relative) != *target
                || !signedSymlinks.contains(relative)) {
                return Utils::ResultError(
                    QStringLiteral("Installed Python runtime symlink is not signed: %1")
                        .arg(relative));
            }
            const QString canonicalTarget
                = QFileInfo(QFileInfo(absolute).dir(), *target).canonicalFilePath();
            const QString rootPrefix = root + QLatin1Char('/');
            if (canonicalTarget.isEmpty()
                || (canonicalTarget != root && !canonicalTarget.startsWith(rootPrefix))) {
                return Utils::ResultError(
                    QStringLiteral("Installed Python runtime symlink escapes its root: %1")
                        .arg(relative));
            }
            result.snapshot.insert(relative, *entry);
            records.insert(
                relative,
                QJsonObject{
                    {QStringLiteral("path"), relative},
                    {QStringLiteral("target"), *target},
                    {QStringLiteral("type"), QStringLiteral("symlink")}});
            actualSymlinks.insert(relative);
            continue;
        }
        if (information.isDir()) {
            const Utils::Result<TreeEntry> entry = inspectPath(absolute, PathKind::Directory);
            if (!entry)
                return Utils::ResultError(entry.error());
            result.snapshot.insert(relative, *entry);
            if (!children.contains(relative))
                children.insert(relative, 0);
            continue;
        }
        const Utils::Result<TreeEntry> entry = inspectPath(absolute, PathKind::RegularFile);
        if (!entry)
            return Utils::ResultError(entry.error());
        const quint32 mode = quint32(entry->mode & 07777);
        const bool identity = relative == QLatin1String(identityRelativePath);
        if ((identity && mode != 0600) || (!identity && mode != 0644 && mode != 0755)) {
            return Utils::ResultError(
                QStringLiteral("Installed Python runtime file mode is invalid: %1").arg(relative));
        }
        const qsizetype limit = identity ? maximumIdentityBytes : maximumRuntimeFileBytes;
        const Utils::Result<HashedFile> hashed = hashRegularFile(absolute, limit, mode);
        if (!hashed)
            return Utils::ResultError(hashed.error());
        result.snapshot.insert(relative, *entry);
        if (!identity) {
            if (result.fileBytes > maximumRuntimeBytes - hashed->bytes)
                return Utils::ResultError(QStringLiteral("Installed Python runtime is too large."));
            result.fileBytes += hashed->bytes;
            records.insert(
                relative,
                QJsonObject{
                    {QStringLiteral("bytes"), double(hashed->bytes)},
                    {QStringLiteral("mode"), int(mode)},
                    {QStringLiteral("path"), relative},
                    {QStringLiteral("sha256"), QString::fromLatin1(hashed->sha256.value().toHex())},
                    {QStringLiteral("type"), QStringLiteral("file")}});
        }
    }
    if (actualSymlinks != QSet<QString>(signedSymlinks.keyBegin(), signedSymlinks.keyEnd()))
        return Utils::ResultError(QStringLiteral("Installed Python runtime symlink set differs."));
    for (auto it = children.cbegin(); it != children.cend(); ++it) {
        if (it.value() == 0) {
            return Utils::ResultError(
                QStringLiteral("Installed Python runtime contains an unsigned empty directory: %1")
                    .arg(it.key()));
        }
    }
    if (!result.snapshot.contains(QString::fromLatin1(pythonRelativePath))
        || !result.snapshot.contains(QString::fromLatin1(identityRelativePath))) {
        return Utils::ResultError(QStringLiteral("Installed Python runtime is incomplete."));
    }
    result.entries = records.size();
    if (result.entries == 0 || result.entries > maximumRuntimeEntries)
        return Utils::ResultError(
            QStringLiteral("Installed Python runtime entry count is invalid."));
    QJsonArray entries;
    for (auto it = records.cbegin(); it != records.cend(); ++it)
        entries.append(it.value());
    const QJsonObject projection{
        {QStringLiteral("entries"), entries},
        {QStringLiteral("format"),
         QStringLiteral("embedlabs-installed-relocatable-runtime-tree-v1")}};
    const Utils::Result<QByteArray> projectionBytes = canonicalAsciiJson(projection);
    if (!projectionBytes)
        return Utils::ResultError(projectionBytes.error());
    result.treeSha256 = sha256(*projectionBytes);
    return result;
}

#endif

Utils::Result<CompilerPythonRuntimeIdentity> parseRuntimeIdentity(
    const QByteArray &bytes,
    const QString &root,
    const ParsedCompanion &companion,
    const CompilerPythonRuntimeExpectation &expectation)
{
    const Utils::Result<QJsonObject> parsed
        = parseCanonicalObject(bytes, QStringLiteral("Installed Python runtime identity"));
    if (!parsed)
        return Utils::ResultError(parsed.error());
    const QJsonObject object = *parsed;
    static const QSet<QString> keys{
        QStringLiteral("base_runtime_archive_sha256"),
        QStringLiteral("base_runtime_tree_sha256"),
        QStringLiteral("companion_bundle_sha256"),
        QStringLiteral("companion_id"),
        QStringLiteral("companion_key_id"),
        QStringLiteral("companion_manifest_format"),
        QStringLiteral("companion_manifest_format_version"),
        QStringLiteral("companion_manifest_sha256"),
        QStringLiteral("companion_version"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("identity_sha256"),
        QStringLiteral("installed_file_bytes"),
        QStringLiteral("installed_tree_entries"),
        QStringLiteral("installed_tree_sha256"),
        QStringLiteral("machine"),
        QStringLiteral("package_set_sha256"),
        QStringLiteral("platform"),
        QStringLiteral("python_cache_tag"),
        QStringLiteral("python_executable"),
        QStringLiteral("python_executable_sha256"),
        QStringLiteral("python_implementation"),
        QStringLiteral("python_soabi"),
        QStringLiteral("python_version"),
        QStringLiteral("runtime_release_tag"),
        QStringLiteral("runtime_root"),
        QStringLiteral("safe_path_policy"),
        QStringLiteral("signature_algorithm"),
        QStringLiteral("signature_domain"),
        QStringLiteral("sys_base_prefix"),
        QStringLiteral("sys_path_relative"),
        QStringLiteral("sys_path_sha256"),
        QStringLiteral("sys_prefix"),
        QStringLiteral("wheel_lock_sha256"),
        QStringLiteral("wheel_overlay_tree_sha256"),
        QStringLiteral("wheelhouse_content_sha256")};
    if (!hasExactKeys(object, keys)
        || object.value(QStringLiteral("format")).toString() != QLatin1String(identityFormat)
        || object.value(QStringLiteral("format_version")).toInt() != 1
        || object.value(QStringLiteral("runtime_root")).toString() != root
        || object.value(QStringLiteral("python_executable")).toString()
               != QDir(root).filePath(QString::fromLatin1(pythonRelativePath))
        || object.value(QStringLiteral("sys_prefix")).toString()
               != QDir(root).filePath(QStringLiteral("python"))
        || object.value(QStringLiteral("sys_base_prefix")).toString()
               != QDir(root).filePath(QStringLiteral("python"))
        || object.value(QStringLiteral("companion_id")).toString() != QLatin1String(companionId)
        || object.value(QStringLiteral("companion_version")).toString()
               != expectation.companionVersion
        || object.value(QStringLiteral("companion_manifest_format")).toString()
               != QLatin1String(companionFormat)
        || object.value(QStringLiteral("companion_manifest_format_version")).toInt() != 1
        || object.value(QStringLiteral("signature_algorithm")).toString() != QLatin1String("ed25519")
        || object.value(QStringLiteral("signature_domain")).toString()
               != QLatin1String(signatureDomain)
        || object.value(QStringLiteral("safe_path_policy")).toString()
               != QLatin1String("required-explicit-signed-import-root")
        || object.value(QStringLiteral("python_version")).toString() != QLatin1String("3.11.15")
        || object.value(QStringLiteral("python_implementation")).toString()
               != QLatin1String("CPython")
        || object.value(QStringLiteral("python_cache_tag")).toString()
               != QLatin1String("cpython-311")
        || object.value(QStringLiteral("python_soabi")).toString()
               != QLatin1String("cpython-311-darwin")
        || object.value(QStringLiteral("platform")).toString() != QLatin1String("macos")
        || object.value(QStringLiteral("machine")).toString() != QLatin1String("arm64")
        || object.value(QStringLiteral("runtime_release_tag")).toString()
               != companion.runtimeReleaseTag) {
        return Utils::ResultError(QStringLiteral("Installed Python runtime identity is invalid."));
    }

    const QJsonArray expectedSysPath{
        QStringLiteral("python/lib/python311.zip"),
        QStringLiteral("python/lib/python3.11"),
        QStringLiteral("python/lib/python3.11/lib-dynload"),
        QStringLiteral("python/lib/python3.11/site-packages"),
        QStringLiteral("python/lib/python3.11/site-packages/embedlabs_api070")};
    const QJsonArray sysPath = object.value(QStringLiteral("sys_path_relative")).toArray();
    const Utils::Result<QByteArray> sysPathBytes = canonicalAsciiJson(sysPath);
    const auto sysPathSha = sha256FromJson(object.value(QStringLiteral("sys_path_sha256")));
    if (sysPath != expectedSysPath || !sysPathBytes || !sysPathSha
        || sha256(*sysPathBytes) != *sysPathSha) {
        return Utils::ResultError(QStringLiteral("Installed Python runtime sys.path is invalid."));
    }

    QJsonObject selfProjection = object;
    selfProjection.remove(QStringLiteral("identity_sha256"));
    const Utils::Result<QByteArray> selfBytes = canonicalAsciiJson(selfProjection);
    const auto identitySha = sha256FromJson(object.value(QStringLiteral("identity_sha256")));
    if (!selfBytes || !identitySha || sha256(*selfBytes) != *identitySha)
        return Utils::ResultError(
            QStringLiteral("Installed Python runtime self-digest is invalid."));
    QJsonObject portableProjection = object;
    portableProjection.remove(QStringLiteral("runtime_root"));
    portableProjection.remove(QStringLiteral("python_executable"));
    portableProjection.remove(QStringLiteral("sys_prefix"));
    portableProjection.remove(QStringLiteral("sys_base_prefix"));
    portableProjection.remove(QStringLiteral("identity_sha256"));
    const Utils::Result<QByteArray> portableBytes = canonicalAsciiJson(portableProjection);
    if (!portableBytes || sha256(*portableBytes) != expectation.portableIdentitySha256) {
        return Utils::ResultError(
            QStringLiteral("Installed Python runtime portable identity is invalid."));
    }

    const auto bundleSha = sha256FromJson(object.value(QStringLiteral("companion_bundle_sha256")));
    const auto manifestSha = sha256FromJson(
        object.value(QStringLiteral("companion_manifest_sha256")));
    const auto keyId = sha256FromJson(object.value(QStringLiteral("companion_key_id")));
    const auto executableSha = sha256FromJson(
        object.value(QStringLiteral("python_executable_sha256")));
    const auto archiveSha = sha256FromJson(
        object.value(QStringLiteral("base_runtime_archive_sha256")));
    const auto treeSha = sha256FromJson(object.value(QStringLiteral("base_runtime_tree_sha256")));
    const auto lockSha = sha256FromJson(object.value(QStringLiteral("wheel_lock_sha256")));
    const auto wheelhouseSha = sha256FromJson(
        object.value(QStringLiteral("wheelhouse_content_sha256")));
    const auto overlaySha = sha256FromJson(
        object.value(QStringLiteral("wheel_overlay_tree_sha256")));
    const auto packageSha = sha256FromJson(object.value(QStringLiteral("package_set_sha256")));
    const auto installedSha = sha256FromJson(object.value(QStringLiteral("installed_tree_sha256")));
    const auto installedEntries = unsignedInteger(
        object.value(QStringLiteral("installed_tree_entries")));
    const auto installedBytes = unsignedInteger(
        object.value(QStringLiteral("installed_file_bytes")));
    if (!bundleSha || *bundleSha != expectation.companionBundleSha256 || !manifestSha
        || *manifestSha != expectation.companionManifestSha256 || !keyId
        || *keyId != companion.signingKeyId || !executableSha
        || *executableSha != expectation.pythonExecutableSha256 || !archiveSha
        || *archiveSha != companion.runtimeArchive.sha256 || !treeSha
        || *treeSha != companion.runtimeTreeSha256 || !lockSha
        || *lockSha != companion.wheelLockSha256 || !wheelhouseSha
        || *wheelhouseSha != companion.wheelhouseContentSha256 || !overlaySha || !packageSha
        || !installedSha || *installedSha != expectation.installedTreeSha256 || !installedEntries
        || *installedEntries == 0 || *installedEntries > maximumRuntimeEntries || !installedBytes
        || *installedBytes == 0 || *installedBytes > maximumRuntimeBytes) {
        return Utils::ResultError(
            QStringLiteral("Installed Python runtime evidence does not match external trust."));
    }

    CompilerPythonRuntimeIdentity result;
    result.companionId = QString::fromLatin1(companionId);
    result.companionVersion = expectation.companionVersion;
    result.pythonVersion = object.value(QStringLiteral("python_version")).toString();
    result.pythonCacheTag = object.value(QStringLiteral("python_cache_tag")).toString();
    result.pythonSoAbi = object.value(QStringLiteral("python_soabi")).toString();
    result.runtimeReleaseTag = companion.runtimeReleaseTag;
    result.companionBundleSha256 = *bundleSha;
    result.companionManifestSha256 = *manifestSha;
    result.companionKeyId = *keyId;
    result.pythonExecutableSha256 = *executableSha;
    result.baseRuntimeArchiveSha256 = *archiveSha;
    result.baseRuntimeTreeSha256 = *treeSha;
    result.wheelLockSha256 = *lockSha;
    result.wheelhouseContentSha256 = *wheelhouseSha;
    result.wheelOverlayTreeSha256 = *overlaySha;
    result.packageSetSha256 = *packageSha;
    result.sysPathSha256 = *sysPathSha;
    result.installedTreeSha256 = *installedSha;
    result.exactIdentitySha256 = *identitySha;
    result.portableIdentitySha256 = expectation.portableIdentitySha256;
    result.installedTreeEntries = qsizetype(*installedEntries);
    result.installedFileBytes = *installedBytes;
    if (!result.isValid())
        return Utils::ResultError(
            QStringLiteral("Installed Python runtime identity is incomplete."));
    return result;
}

} // namespace

Utils::Result<CompilerPythonRuntimeProfile> CompilerPythonRuntimeProfile::loadImpl(
    const Utils::FilePath &installedCompanionRoot,
    const Utils::FilePath &installedRuntimeRoot,
    CompilerPythonRuntimeExpectation expectation)
{
#if !defined(Q_OS_DARWIN) || !defined(Q_PROCESSOR_ARM_64)
    Q_UNUSED(installedCompanionRoot)
    Q_UNUSED(installedRuntimeRoot)
    Q_UNUSED(expectation)
    return Utils::ResultError(
        QStringLiteral("Relocatable Python runtimes require native macOS arm64."));
#else
    if (!expectation.isValid())
        return Utils::ResultError(QStringLiteral("Python runtime external trust is invalid."));
    if (!installedCompanionRoot.isAbsolutePath() || !installedCompanionRoot.scheme().isEmpty()
        || !installedRuntimeRoot.isAbsolutePath() || !installedRuntimeRoot.scheme().isEmpty()) {
        return Utils::ResultError(
            QStringLiteral("Python runtime roots must be absolute local paths."));
    }
    const QString companionRoot = QDir::cleanPath(installedCompanionRoot.path());
    const QString runtimeRoot = QDir::cleanPath(installedRuntimeRoot.path());
    if (companionRoot == runtimeRoot || runtimeRoot.startsWith(companionRoot + QLatin1Char('/'))
        || companionRoot.startsWith(runtimeRoot + QLatin1Char('/'))) {
        return Utils::ResultError(
            QStringLiteral("Python companion and runtime roots must be disjoint."));
    }
    const QString canonicalCompanion = QFileInfo(companionRoot).canonicalFilePath();
    const QString canonicalRuntime = QFileInfo(runtimeRoot).canonicalFilePath();
    if (canonicalCompanion.isEmpty() || canonicalRuntime.isEmpty()
        || QDir::cleanPath(canonicalCompanion) != companionRoot
        || QDir::cleanPath(canonicalRuntime) != runtimeRoot) {
        return Utils::ResultError(
            QStringLiteral("Python runtime root has a symbolic-link ancestor."));
    }
    const Utils::Result<> companionAncestors = inspectAncestorChain(companionRoot);
    const Utils::Result<> runtimeAncestors = inspectAncestorChain(runtimeRoot);
    if (!companionAncestors)
        return Utils::ResultError(companionAncestors.error());
    if (!runtimeAncestors)
        return Utils::ResultError(runtimeAncestors.error());

    const QString manifestPath = QDir(companionRoot).filePath(QStringLiteral("manifest.json"));
    const QString signaturePath = QDir(companionRoot).filePath(QStringLiteral("manifest.sig"));
    const Utils::Result<QByteArray> manifest
        = readRegularFile(manifestPath, maximumManifestBytes, 0644);
    const Utils::Result<QByteArray> signature = readRegularFile(signaturePath, 64, 0644);
    if (!manifest)
        return Utils::ResultError(manifest.error());
    if (!signature)
        return Utils::ResultError(signature.error());
    if (signature->size() != 64 || sha256(*manifest) != expectation.companionManifestSha256)
        return Utils::ResultError(QStringLiteral("Python companion manifest identity is invalid."));
    QByteArray signedBytes(signatureDomain);
    signedBytes.append('\0');
    signedBytes.append(*manifest);
    if (crypto_ed25519_check(
            reinterpret_cast<const std::uint8_t *>(signature->constData()),
            reinterpret_cast<const std::uint8_t *>(expectation.signingPublicKey.constData()),
            reinterpret_cast<const std::uint8_t *>(signedBytes.constData()),
            size_t(signedBytes.size()))
        != 0) {
        return Utils::ResultError(QStringLiteral("Python companion manifest signature is invalid."));
    }
    const Utils::Result<ParsedCompanion> companion = parseCompanionManifest(*manifest, expectation);
    if (!companion)
        return Utils::ResultError(companion.error());

    const Utils::Result<QMap<QString, TreeEntry>> companionBefore
        = inspectCompanionTree(companionRoot, companion->files);
    if (!companionBefore)
        return Utils::ResultError(companionBefore.error());
    const Utils::Result<QByteArray> stableManifest
        = readRegularFile(manifestPath, maximumManifestBytes, 0644);
    const Utils::Result<QByteArray> stableSignature = readRegularFile(signaturePath, 64, 0644);
    if (!stableManifest || !stableSignature || *stableManifest != *manifest
        || *stableSignature != *signature) {
        return Utils::ResultError(
            QStringLiteral("Python companion manifest changed before payload validation."));
    }
    for (auto it = companion->files.cbegin(); it != companion->files.cend(); ++it) {
        const Utils::Result<HashedFile> contents = hashRegularFile(
            QDir(companionRoot).filePath(it.key()), maximumCompanionFileBytes, it->mode);
        if (!contents)
            return Utils::ResultError(contents.error());
        if (contents->bytes != it->bytes || contents->sha256 != it->sha256) {
            return Utils::ResultError(
                QStringLiteral("Python companion file differs from its signed record: %1")
                    .arg(it.key()));
        }
    }
    const QString trustPath = QStringLiteral("trust/%1.pub")
                                  .arg(QString::fromLatin1(companion->signingKeyId.value().toHex()));
    const Utils::Result<QByteArray> trustCopy
        = readRegularFile(QDir(companionRoot).filePath(trustPath), 32, 0644);
    if (!trustCopy || *trustCopy != expectation.signingPublicKey)
        return Utils::ResultError(QStringLiteral("Python companion bundled key is inconsistent."));
    const FileRecord runtimeTreeRecord = companion->files.value(companion->runtimeTreeManifestPath);
    const Utils::Result<QByteArray> runtimeTreeBytes = readRegularFile(
        QDir(companionRoot).filePath(companion->runtimeTreeManifestPath),
        maximumManifestBytes,
        runtimeTreeRecord.mode);
    if (!runtimeTreeBytes || quint64(runtimeTreeBytes->size()) != runtimeTreeRecord.bytes
        || sha256(*runtimeTreeBytes) != runtimeTreeRecord.sha256) {
        return Utils::ResultError(
            QStringLiteral("Python base runtime tree changed during companion validation."));
    }
    const Utils::Result<QMap<QString, TreeEntry>> companionAfter
        = inspectCompanionTree(companionRoot, companion->files);
    if (!companionAfter || *companionAfter != *companionBefore)
        return Utils::ResultError(QStringLiteral("Python companion changed during validation."));

    const Utils::Result<ParsedRuntimeTree> runtimeTree
        = parseRuntimeTree(*runtimeTreeBytes, *companion, expectation);
    if (!runtimeTree)
        return Utils::ResultError(runtimeTree.error());

    const QString identityPath
        = QDir(runtimeRoot).filePath(QString::fromLatin1(identityRelativePath));
    const Utils::Result<QByteArray> identityBytes
        = readRegularFile(identityPath, maximumIdentityBytes, 0600);
    if (!identityBytes)
        return Utils::ResultError(identityBytes.error());
    const Utils::Result<CompilerPythonRuntimeIdentity> identity
        = parseRuntimeIdentity(*identityBytes, runtimeRoot, *companion, expectation);
    if (!identity)
        return Utils::ResultError(identity.error());

    const Utils::Result<RuntimeTreeInspection> runtimeBefore
        = inspectRuntimeTree(runtimeRoot, runtimeTree->symlinks);
    if (!runtimeBefore)
        return Utils::ResultError(runtimeBefore.error());
    const QString pythonPath = QDir(runtimeRoot).filePath(QString::fromLatin1(pythonRelativePath));
    const quint32 pythonMode = quint32(
        runtimeBefore->snapshot.value(QString::fromLatin1(pythonRelativePath)).mode & 07777);
    const Utils::Result<HashedFile> pythonBytes
        = hashRegularFile(pythonPath, maximumRuntimeFileBytes, pythonMode);
    if (!pythonBytes || pythonBytes->sha256 != expectation.pythonExecutableSha256)
        return Utils::ResultError(QStringLiteral("Python executable differs from external trust."));
    if (runtimeBefore->entries != identity->installedTreeEntries
        || runtimeBefore->fileBytes != identity->installedFileBytes
        || runtimeBefore->treeSha256 != identity->installedTreeSha256
        || runtimeBefore->treeSha256 != expectation.installedTreeSha256) {
        return Utils::ResultError(QStringLiteral("Installed Python runtime tree identity differs."));
    }
    const Utils::Result<QByteArray> stableIdentity
        = readRegularFile(identityPath, maximumIdentityBytes, 0600);
    const Utils::Result<RuntimeTreeInspection> runtimeAfter
        = inspectRuntimeTree(runtimeRoot, runtimeTree->symlinks);
    if (!stableIdentity || *stableIdentity != *identityBytes || !runtimeAfter
        || runtimeAfter->snapshot != runtimeBefore->snapshot
        || runtimeAfter->treeSha256 != runtimeBefore->treeSha256
        || runtimeAfter->entries != runtimeBefore->entries
        || runtimeAfter->fileBytes != runtimeBefore->fileBytes) {
        return Utils::ResultError(
            QStringLiteral("Installed Python runtime changed during validation."));
    }

    CompilerPythonRuntimeProfile result;
    result.m_companionRoot = Utils::FilePath::fromString(companionRoot);
    result.m_runtimeRoot = Utils::FilePath::fromString(runtimeRoot);
    result.m_pythonExecutable = Utils::FilePath::fromString(pythonPath);
    result.m_runtimeIdentityFile = Utils::FilePath::fromString(identityPath);
    result.m_expectation = std::move(expectation);
    result.m_identity = *identity;
    return result;
#endif
}

bool CompilerPythonRuntimeExpectation::isValid() const
{
    return isSemanticVersion(companionVersion) && signingPublicKey.size() == 32
           && companionBundleSha256.isValid() && companionManifestSha256.isValid()
           && pythonExecutableSha256.isValid() && installedTreeSha256.isValid()
           && portableIdentitySha256.isValid();
}

bool CompilerPythonRuntimeIdentity::isValid() const
{
    return companionId == QLatin1String(::EtherCAT::ProjectCompiler::companionId)
           && isSemanticVersion(companionVersion) && pythonVersion == QLatin1String("3.11.15")
           && pythonCacheTag == QLatin1String("cpython-311")
           && pythonSoAbi == QLatin1String("cpython-311-darwin") && !runtimeReleaseTag.isEmpty()
           && companionBundleSha256.isValid() && companionManifestSha256.isValid()
           && companionKeyId.isValid() && pythonExecutableSha256.isValid()
           && baseRuntimeArchiveSha256.isValid() && baseRuntimeTreeSha256.isValid()
           && wheelLockSha256.isValid() && wheelhouseContentSha256.isValid()
           && wheelOverlayTreeSha256.isValid() && packageSetSha256.isValid()
           && sysPathSha256.isValid() && installedTreeSha256.isValid()
           && exactIdentitySha256.isValid() && portableIdentitySha256.isValid()
           && installedTreeEntries > 0 && installedTreeEntries <= maximumRuntimeEntries
           && installedFileBytes > 0 && installedFileBytes <= maximumRuntimeBytes;
}

Utils::Result<CompilerPythonRuntimeProfile> CompilerPythonRuntimeProfile::load(
    const Utils::FilePath &installedCompanionRoot,
    const Utils::FilePath &installedRuntimeRoot,
    CompilerPythonRuntimeExpectation expectation)
{
    return loadImpl(installedCompanionRoot, installedRuntimeRoot, std::move(expectation));
}

Utils::Result<> CompilerPythonRuntimeProfile::validateCurrent() const
{
    const Utils::Result<CompilerPythonRuntimeProfile> current
        = load(m_companionRoot, m_runtimeRoot, m_expectation);
    if (!current)
        return Utils::ResultError(current.error());
    if (current->m_companionRoot != m_companionRoot || current->m_runtimeRoot != m_runtimeRoot
        || current->m_pythonExecutable != m_pythonExecutable
        || current->m_runtimeIdentityFile != m_runtimeIdentityFile
        || current->m_identity != m_identity) {
        return Utils::ResultError(
            QStringLiteral("Python runtime identity changed after validation."));
    }
    return Utils::ResultOk;
}

Utils::FilePath CompilerPythonRuntimeProfile::companionRoot() const
{
    return m_companionRoot;
}

Utils::FilePath CompilerPythonRuntimeProfile::runtimeRoot() const
{
    return m_runtimeRoot;
}

Utils::FilePath CompilerPythonRuntimeProfile::pythonExecutable() const
{
    return m_pythonExecutable;
}

Utils::FilePath CompilerPythonRuntimeProfile::runtimeIdentityFile() const
{
    return m_runtimeIdentityFile;
}

const CompilerPythonRuntimeExpectation &CompilerPythonRuntimeProfile::expectation() const
{
    return m_expectation;
}

const CompilerPythonRuntimeIdentity &CompilerPythonRuntimeProfile::identity() const
{
    return m_identity;
}

} // namespace EtherCAT::ProjectCompiler
