// Copyright (C) 2026 Embed Labs

#include "compilerprovisioningprofile.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <limits>
#include <optional>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace EtherCAT::ProjectCompiler {

namespace {

constexpr qsizetype maximumProfileBytes = 64 * 1024;
constexpr qsizetype maximumExecutableBytes = 256 * 1024 * 1024;

Utils::Result<QByteArray> readRegularLeaf(
    const Utils::FilePath &path, qsizetype maximumBytes, bool requireExecutable)
{
    if (!path.isAbsolutePath() || !path.scheme().isEmpty())
        return Utils::ResultError(QStringLiteral("Provisioned path is not an absolute local path."));

#ifdef Q_OS_UNIX
    const QByteArray nativePath = QFile::encodeName(path.path());
    struct stat before = {};
    if (::lstat(nativePath.constData(), &before) != 0 || !S_ISREG(before.st_mode)
        || before.st_nlink != 1 || (before.st_uid != ::geteuid() && before.st_uid != 0)
        || (before.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return Utils::ResultError(
            QStringLiteral("Provisioned path is not a regular non-symlink file: %1")
                .arg(path.toUserOutput()));
    }
    if (before.st_size < 0 || before.st_size > maximumBytes) {
        return Utils::ResultError(
            QStringLiteral("Provisioned file exceeds its size limit: %1").arg(path.toUserOutput()));
    }
    if (requireExecutable && !(before.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return Utils::ResultError(
            QStringLiteral("Provisioned compiler is not executable: %1").arg(path.toUserOutput()));
    }

    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(nativePath.constData(), flags);
    if (descriptor < 0) {
        return Utils::ResultError(
            QStringLiteral("Cannot securely open provisioned file: %1").arg(path.toUserOutput()));
    }
    struct DescriptorCloser
    {
        int descriptor;
        ~DescriptorCloser() { ::close(descriptor); }
    } closer{descriptor};

    struct stat opened = {};
    const auto sameStableMetadata = [](const struct stat &left, const struct stat &right) {
        if (left.st_dev != right.st_dev || left.st_ino != right.st_ino
            || left.st_size != right.st_size || left.st_mode != right.st_mode
            || left.st_uid != right.st_uid || left.st_nlink != right.st_nlink) {
            return false;
        }
#ifdef Q_OS_DARWIN
        return left.st_mtimespec.tv_sec == right.st_mtimespec.tv_sec
               && left.st_mtimespec.tv_nsec == right.st_mtimespec.tv_nsec
               && left.st_ctimespec.tv_sec == right.st_ctimespec.tv_sec
               && left.st_ctimespec.tv_nsec == right.st_ctimespec.tv_nsec;
#else
        return left.st_mtim.tv_sec == right.st_mtim.tv_sec
               && left.st_mtim.tv_nsec == right.st_mtim.tv_nsec
               && left.st_ctim.tv_sec == right.st_ctim.tv_sec
               && left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
#endif
    };
    if (::fstat(descriptor, &opened) != 0 || !S_ISREG(opened.st_mode)
        || !sameStableMetadata(before, opened)) {
        return Utils::ResultError(QStringLiteral("Provisioned file changed while it was opened: %1")
                                      .arg(path.toUserOutput()));
    }

    QByteArray bytes;
    bytes.resize(qsizetype(opened.st_size));
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count
            = ::read(descriptor, bytes.data() + offset, size_t(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            return Utils::ResultError(QStringLiteral("Cannot completely read provisioned file: %1")
                                          .arg(path.toUserOutput()));
        }
        offset += qsizetype(count);
    }
    struct stat finalDescriptor = {};
    struct stat finalPath = {};
    if (::fstat(descriptor, &finalDescriptor) != 0
        || ::lstat(nativePath.constData(), &finalPath) != 0
        || !sameStableMetadata(before, finalDescriptor)
        || !sameStableMetadata(before, finalPath)) {
        return Utils::ResultError(
            QStringLiteral("Provisioned file changed while it was read: %1")
                .arg(path.toUserOutput()));
    }
    return bytes;
#else
    const QFileInfo info(path.path());
    if (!info.isFile() || info.isSymLink() || info.size() < 0 || info.size() > maximumBytes
        || (requireExecutable && !info.isExecutable())) {
        return Utils::ResultError(
            QStringLiteral("Provisioned path is not a permitted regular file: %1")
                .arg(path.toUserOutput()));
    }
    return path.fileContents(maximumBytes);
#endif
}

QByteArray quotedJsonAscii(const QString &value)
{
    QByteArray result(1, '"');
    for (const QChar character : value) {
        const ushort code = character.unicode();
        switch (code) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (code < 0x20 || code > 0x7f) {
                result += "\\u";
                result += QByteArray::number(code, 16).rightJustified(4, '0');
            } else {
                result += char(code);
            }
        }
    }
    result += '"';
    return result;
}

std::optional<Data::RuntimePackageCompilerSha256> shaFromHex(const QString &value)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{64}$"));
    if (!expression.match(value).hasMatch())
        return std::nullopt;
    Data::RuntimePackageCompilerSha256 sha(QByteArray::fromHex(value.toLatin1()));
    return sha.isValid() ? std::optional<Data::RuntimePackageCompilerSha256>(sha) : std::nullopt;
}

QByteArray canonicalProfile(
    const QString &contractId,
    quint32 contractVersion,
    const QString &executablePath,
    const QString &executableSha256,
    const QString &publicKeyPath,
    const QString &publicKeySha256,
    const QString &schemaBundleSha256)
{
    QByteArray bytes;
    bytes += "{\"contract_id\":" + quotedJsonAscii(contractId);
    bytes += ",\"contract_version\":" + QByteArray::number(contractVersion);
    bytes += ",\"executable_path\":" + quotedJsonAscii(executablePath);
    bytes += ",\"executable_sha256\":" + quotedJsonAscii(executableSha256);
    bytes += ",\"format\":\"ethercat-ide-compiler-provisioning-v1\"";
    bytes += ",\"format_version\":1";
    bytes += ",\"production_public_key_path\":" + quotedJsonAscii(publicKeyPath);
    bytes += ",\"production_public_key_sha256\":" + quotedJsonAscii(publicKeySha256);
    bytes += ",\"schema_bundle_sha256\":" + quotedJsonAscii(schemaBundleSha256) + "}\n";
    return bytes;
}

} // namespace

Utils::Result<QByteArray> readProvisionedRegularLeaf(
    const Utils::FilePath &path, qsizetype maximumBytes, bool requireExecutable)
{
    return readRegularLeaf(path, maximumBytes, requireExecutable);
}

Utils::Result<CompilerProvisioningProfile> CompilerProvisioningProfile::load(
    const Utils::FilePath &profileFile)
{
    const Utils::Result<QByteArray> profileBytes
        = readRegularLeaf(profileFile, maximumProfileBytes, false);
    if (!profileBytes)
        return Utils::ResultError(profileBytes.error());

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*profileBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return Utils::ResultError(QStringLiteral("Compiler provisioning JSON is malformed."));

    const QJsonObject object = document.object();
    const QSet<QString> expectedKeys{
        QStringLiteral("contract_id"),
        QStringLiteral("contract_version"),
        QStringLiteral("executable_path"),
        QStringLiteral("executable_sha256"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("production_public_key_path"),
        QStringLiteral("production_public_key_sha256"),
        QStringLiteral("schema_bundle_sha256"),
    };
    const QStringList actualKeyList = object.keys();
    if (QSet<QString>(actualKeyList.cbegin(), actualKeyList.cend()) != expectedKeys
        || object.value("format").toString()
               != QStringLiteral("ethercat-ide-compiler-provisioning-v1")
        || object.value("format_version").toDouble() != 1
        || !object.value("format_version").isDouble()
        || !object.value("contract_version").isDouble()) {
        return Utils::ResultError(
            QStringLiteral("Compiler provisioning JSON has an invalid shape."));
    }

    const double contractVersionNumber = object.value("contract_version").toDouble();
    if (contractVersionNumber < 1 || contractVersionNumber > std::numeric_limits<quint32>::max()
        || contractVersionNumber != quint32(contractVersionNumber)) {
        return Utils::ResultError(QStringLiteral("Compiler contract version is invalid."));
    }
    const quint32 contractVersion = quint32(contractVersionNumber);
    const QString contractId = object.value("contract_id").toString();
    const QString executablePath = object.value("executable_path").toString();
    const QString executableHashText = object.value("executable_sha256").toString();
    const QString publicKeyPath = object.value("production_public_key_path").toString();
    const QString publicKeyHashText = object.value("production_public_key_sha256").toString();
    const QString schemaHashText = object.value("schema_bundle_sha256").toString();
    const auto executableHash = shaFromHex(executableHashText);
    const auto publicKeyHash = shaFromHex(publicKeyHashText);
    const auto schemaHash = shaFromHex(schemaHashText);
    Data::RuntimePackageCompilerContractIdentity contract{
        contractId, contractVersion, schemaHash.value_or(Data::RuntimePackageCompilerSha256())};
    if (!executableHash || !publicKeyHash || !schemaHash || !contract.isValid())
        return Utils::ResultError(QStringLiteral("Compiler provisioning identity is invalid."));

    const QByteArray canonical = canonicalProfile(
        contractId,
        contractVersion,
        executablePath,
        executableHashText,
        publicKeyPath,
        publicKeyHashText,
        schemaHashText);
    if (*profileBytes != canonical) {
        return Utils::ResultError(
            QStringLiteral("Compiler provisioning JSON is not exact canonical v1 JSON."));
    }

    const Utils::FilePath executable = Utils::FilePath::fromString(executablePath);
    const Utils::FilePath publicKey = Utils::FilePath::fromString(publicKeyPath);
    const Utils::Result<QByteArray> executableBytes
        = readRegularLeaf(executable, maximumExecutableBytes, true);
    if (!executableBytes)
        return Utils::ResultError(executableBytes.error());
    const Utils::Result<QByteArray> keyBytes = readRegularLeaf(publicKey, 32, false);
    if (!keyBytes)
        return Utils::ResultError(keyBytes.error());
    if (keyBytes->size() != 32)
        return Utils::ResultError(QStringLiteral("Production public key must be exactly 32 bytes."));

    const Data::RuntimePackageCompilerSha256 actualExecutableHash(
        QCryptographicHash::hash(*executableBytes, QCryptographicHash::Sha256));
    const Data::RuntimePackageCompilerSha256 actualPublicKeyHash(
        QCryptographicHash::hash(*keyBytes, QCryptographicHash::Sha256));
    if (actualExecutableHash != *executableHash || actualPublicKeyHash != *publicKeyHash) {
        return Utils::ResultError(
            QStringLiteral("Provisioned compiler or production key digest does not match."));
    }

    CompilerProvisioningProfile result;
    result.m_profileFile = profileFile;
    result.m_executable = executable;
    result.m_productionPublicKey = publicKey;
    result.m_contractIdentity = contract;
    result.m_profileSha256 = Data::RuntimePackageCompilerSha256(
        QCryptographicHash::hash(*profileBytes, QCryptographicHash::Sha256));
    result.m_executableSha256 = actualExecutableHash;
    result.m_productionPublicKeySha256 = actualPublicKeyHash;
    result.m_exactProfileBytes = *profileBytes;
    result.m_exactExecutableBytes = *executableBytes;
    result.m_exactProductionPublicKeyBytes = *keyBytes;
    return result;
}

Utils::Result<> CompilerProvisioningProfile::validateCurrent() const
{
    const Utils::Result<CompilerProvisioningProfile> current = load(m_profileFile);
    if (!current)
        return Utils::ResultError(current.error());
    if (current->m_exactProfileBytes != m_exactProfileBytes
        || current->m_profileSha256 != m_profileSha256
        || current->m_executableSha256 != m_executableSha256
        || current->m_productionPublicKeySha256 != m_productionPublicKeySha256
        || current->m_exactExecutableBytes != m_exactExecutableBytes
        || current->m_exactProductionPublicKeyBytes != m_exactProductionPublicKeyBytes
        || current->m_contractIdentity != m_contractIdentity) {
        return Utils::ResultError(QStringLiteral("Compiler provisioning changed after validation."));
    }
    return validatePinnedFiles();
}

Utils::Result<> CompilerProvisioningProfile::validatePinnedFiles() const
{
    const Utils::Result<QByteArray> executableBytes
        = readRegularLeaf(m_executable, maximumExecutableBytes, true);
    if (!executableBytes)
        return Utils::ResultError(executableBytes.error());
    const Utils::Result<QByteArray> keyBytes = readRegularLeaf(m_productionPublicKey, 32, false);
    if (!keyBytes)
        return Utils::ResultError(keyBytes.error());
    if (*executableBytes != m_exactExecutableBytes || *keyBytes != m_exactProductionPublicKeyBytes
        || Data::RuntimePackageCompilerSha256{QCryptographicHash::hash(
               *executableBytes, QCryptographicHash::Sha256)}
               != m_executableSha256
        || Data::RuntimePackageCompilerSha256{QCryptographicHash::hash(
               *keyBytes, QCryptographicHash::Sha256)}
               != m_productionPublicKeySha256) {
        return Utils::ResultError(
            QStringLiteral("Pinned compiler or production key changed after validation."));
    }
    return Utils::ResultOk;
}

Utils::Result<> CompilerProvisioningProfile::usePinnedFiles(
    const Utils::FilePath &executable, const Utils::FilePath &productionPublicKey)
{
    const Utils::FilePath previousExecutable = m_executable;
    const Utils::FilePath previousPublicKey = m_productionPublicKey;
    m_executable = executable;
    m_productionPublicKey = productionPublicKey;
    if (const Utils::Result<> valid = validatePinnedFiles(); !valid) {
        m_executable = previousExecutable;
        m_productionPublicKey = previousPublicKey;
        return valid;
    }
    return Utils::ResultOk;
}

Utils::FilePath CompilerProvisioningProfile::profileFile() const
{
    return m_profileFile;
}
Utils::FilePath CompilerProvisioningProfile::executable() const
{
    return m_executable;
}
Utils::FilePath CompilerProvisioningProfile::productionPublicKey() const
{
    return m_productionPublicKey;
}
Data::RuntimePackageCompilerContractIdentity CompilerProvisioningProfile::contractIdentity() const
{
    return m_contractIdentity;
}
Data::RuntimePackageCompilerSha256 CompilerProvisioningProfile::profileSha256() const
{
    return m_profileSha256;
}
Data::RuntimePackageCompilerSha256 CompilerProvisioningProfile::executableSha256() const
{
    return m_executableSha256;
}
Data::RuntimePackageCompilerSha256 CompilerProvisioningProfile::productionPublicKeySha256() const
{
    return m_productionPublicKeySha256;
}
const QByteArray &CompilerProvisioningProfile::exactExecutableBytes() const
{
    return m_exactExecutableBytes;
}
const QByteArray &CompilerProvisioningProfile::exactProductionPublicKeyBytes() const
{
    return m_exactProductionPublicKeyBytes;
}

} // namespace EtherCAT::ProjectCompiler
