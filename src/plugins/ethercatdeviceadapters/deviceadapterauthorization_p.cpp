// Copyright (C) 2026 Embed Labs

#include "deviceadapterauthorization_p.h"

#include <ethercatdata/runtimepackagecompiler.h>

#include <monocypher-ed25519.h>

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>

namespace EtherCAT::DeviceAdapters::Internal {

using namespace Data;

namespace {

constexpr char policyFormat[] = "embed-labs.ethercat-device-adapter-authorization-policy-v1";
constexpr char authorizationFormatV1[] = "embed-labs.ethercat-device-adapter-authorization-v1";
constexpr char authorizationFormatV2[] = "embed-labs.ethercat-device-adapter-authorization-v2";
constexpr char canonicalization[] = "kvell-json-ascii-sorted-compact-lf-v1";
constexpr char adapterSchemaVersionV4[] = "embed-labs.device-adapter/v4";
constexpr char policySignatureDomain[]
    = "embed-labs.ethercat-device-adapter-authorization-policy/v1";
constexpr char authorizationSignatureDomainV1[]
    = "embed-labs.ethercat-device-adapter-authorization/v1";
constexpr char authorizationSignatureDomainV2[]
    = "embed-labs.ethercat-device-adapter-authorization/v2";
constexpr char authorizationProvenanceSetDomain[]
    = "embed-labs.ethercat-device-adapter-authorization-provenance-set/v1";
constexpr qsizetype ed25519PublicKeyBytes = 32;
constexpr qsizetype ed25519SignatureBytes = 64;
constexpr qsizetype maximumDocumentBytes = 1024 * 1024;
constexpr quint32 minimumPolicyRevision = 1;

enum class AuthorizationVersion { V1, V2 };

struct Signer
{
    QByteArray keyId;
    QByteArray publicKey;
    QStringList adapterIds;
    QStringList decisions;
};

struct Policy
{
    QString id;
    quint32 revision = 0;
    QByteArray rootKeyId;
    QHash<QByteArray, Signer> signers;
    QSet<QByteArray> revokedSignerKeyIds;
    QSet<QString> revokedAuthorizationIds;
    QByteArray canonicalBytes;
    QByteArray signature;
    QByteArray canonicalSha256;
    QString sourcePath;
};

struct Authorization
{
    AuthorizationVersion version = AuthorizationVersion::V1;
    QString id;
    QString policyId;
    quint32 policyRevision = 0;
    QByteArray signerKeyId;
    QString decision;
    QJsonObject adapter;
    QByteArray canonicalBytes;
    QByteArray signature;
    QString targetKey;
    QString sourcePath;
};

struct RootKeyMaterialIdentity
{
    QByteArray keyId;
    QByteArray publicKey;
};

struct SignedDocumentMaterialIdentity
{
    QByteArray documentSha256;
    QByteArray signatureSha256;
};

static bool fail(QString *error, const QString &message)
{
    *error = message;
    return false;
}

static bool exactKeys(
    const QJsonObject &object, const QStringList &keys, const QString &context, QString *error)
{
    QStringList actual = object.keys();
    QStringList expected = keys;
    actual.sort();
    expected.sort();
    if (actual == expected)
        return true;
    return fail(error, QString("%1 has an invalid field set").arg(context));
}

static bool stableIdentifier(const QString &value)
{
    static const QRegularExpression pattern(
        QString::fromLatin1("^[A-Za-z0-9][A-Za-z0-9._:/{}-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

static bool parameterIdentifier(const QString &value)
{
    static const QRegularExpression pattern(
        QString::fromLatin1("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

static bool parseString(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QString *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().isEmpty())
        return fail(error, QString("%1.%2 must be a non-empty string").arg(context, key));
    *result = value.toString();
    return true;
}

static bool parseConstantString(
    const QJsonObject &object,
    const QString &key,
    const QString &expected,
    const QString &context,
    QString *error)
{
    QString value;
    return parseString(object, key, context, &value, error)
           && (value == expected || fail(error, QString("%1.%2 is unsupported").arg(context, key)));
}

static bool parseUnsigned(
    const QJsonObject &object,
    const QString &key,
    quint32 minimum,
    const QString &context,
    quint32 *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number < minimum || number > std::numeric_limits<quint32>::max()
        || number != std::trunc(number)) {
        return fail(error, QString("%1.%2 must be an unsigned integer").arg(context, key));
    }
    *result = quint32(number);
    return true;
}

static bool parseHex(
    const QJsonObject &object,
    const QString &key,
    qsizetype bytes,
    const QString &context,
    QByteArray *result,
    QString *error)
{
    QString value;
    if (!parseString(object, key, context, &value, error))
        return false;
    static const QRegularExpression lowercaseHex(QString::fromLatin1("^[0-9a-f]+$"));
    if (value.size() != bytes * 2 || !lowercaseHex.match(value).hasMatch())
        return fail(error, QString("%1.%2 must be lowercase hexadecimal").arg(context, key));
    *result = QByteArray::fromHex(value.toLatin1());
    return result->size() == bytes;
}

static bool parseSortedStrings(
    const QJsonObject &object,
    const QString &key,
    const QString &context,
    QStringList *result,
    QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray())
        return fail(error, QString("%1.%2 must be an array").arg(context, key));
    result->clear();
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty())
            return fail(error, QString("%1.%2 contains a non-string value").arg(context, key));
        result->append(item.toString());
    }
    if (!std::is_sorted(result->cbegin(), result->cend(), std::less<QString>())
        || std::adjacent_find(result->cbegin(), result->cend()) != result->cend()) {
        return fail(error, QString("%1.%2 must be unique and sorted").arg(context, key));
    }
    return true;
}

static bool isContainedRegularFile(
    const QString &path, const QString &canonicalRoot, QString *canonicalPath, QString *error)
{
    const QFileInfo info(path);
    const QString resolved = info.canonicalFilePath();
    const QString relative = QDir(canonicalRoot).relativeFilePath(resolved);
    if (info.isSymLink() || !info.isFile() || resolved.isEmpty() || QDir::isAbsolutePath(relative)
        || relative == ".." || relative.startsWith("../")) {
        return fail(
            error,
            QString("%1: file must be regular, non-symlink, and contained by its root").arg(path));
    }
    *canonicalPath = resolved;
    return true;
}

static std::optional<QByteArray> readStableFile(
    const QString &path, const QString &canonicalRoot, qsizetype maximumBytes, QString *error)
{
    QString canonicalPath;
    if (!isContainedRegularFile(path, canonicalRoot, &canonicalPath, error))
        return std::nullopt;
    const QFileInfo before(path);
    if (before.size() < 0 || before.size() > maximumBytes) {
        fail(error, QString("%1: file exceeds its size limit").arg(path));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.isSequential()) {
        fail(error, QString("%1: cannot open a stable regular file").arg(path));
        return std::nullopt;
    }
    const QByteArray bytes = file.read(maximumBytes + 1);
    const bool ended = file.atEnd();
    file.close();
    const QFileInfo after(path);
    if (!ended || bytes.size() != before.size() || after.size() != before.size()
        || after.lastModified() != before.lastModified()
        || after.canonicalFilePath() != canonicalPath || after.isSymLink() || !after.isFile()) {
        fail(error, QString("%1: file changed while it was being read").arg(path));
        return std::nullopt;
    }
    return bytes;
}

static std::optional<QPair<QByteArray, QJsonObject>> readCanonicalObject(
    const QString &path, const QString &canonicalRoot, QString *error)
{
    const std::optional<QByteArray> bytes
        = readStableFile(path, canonicalRoot, maximumDocumentBytes, error);
    if (!bytes)
        return std::nullopt;
    const RuntimePackageCompilerCanonicalJson canonical
        = RuntimePackageCompilerCanonicalJson::fromExactBytes(*bytes);
    if (!canonical.isValid()) {
        fail(
            error,
            QString("%1: JSON is not kvell-json-ascii-sorted-compact-lf-v1 canonical").arg(path));
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(*bytes);
    if (!document.isObject()) {
        fail(error, QString("%1: canonical document must be an object").arg(path));
        return std::nullopt;
    }
    return QPair<QByteArray, QJsonObject>(*bytes, document.object());
}

static std::optional<QByteArray> readDetachedSignature(
    const QString &jsonPath, const QString &canonicalRoot, QString *error)
{
    QString signaturePath = jsonPath;
    signaturePath.chop(5);
    signaturePath.append(".sig");
    const std::optional<QByteArray> signature
        = readStableFile(signaturePath, canonicalRoot, ed25519SignatureBytes, error);
    if (!signature || signature->size() != ed25519SignatureBytes) {
        if (error->isEmpty())
            fail(error, QString("%1: detached signature is missing").arg(jsonPath));
        else
            *error = QString("%1: %2").arg(jsonPath, *error);
        if (signature && signature->size() != ed25519SignatureBytes)
            *error
                = QString("%1: detached signature must contain exactly 64 raw bytes").arg(jsonPath);
        return std::nullopt;
    }
    return signature;
}

static QByteArray signedMessage(const char *domain, const QByteArray &canonicalBytes)
{
    QByteArray message(domain);
    message.append('\0');
    message.append(canonicalBytes);
    return message;
}

static void appendUint32(QByteArray *bytes, quint32 value)
{
    bytes->append(char((value >> 24) & 0xff));
    bytes->append(char((value >> 16) & 0xff));
    bytes->append(char((value >> 8) & 0xff));
    bytes->append(char(value & 0xff));
}

static QByteArray canonicalAuthorizationSetSha256(
    const QHash<QByteArray, QByteArray> &rootKeys,
    QList<SignedDocumentMaterialIdentity> policyIdentities,
    QList<SignedDocumentMaterialIdentity> authorizationIdentities)
{
    QList<RootKeyMaterialIdentity> roots;
    roots.reserve(rootKeys.size());
    for (auto iterator = rootKeys.cbegin(); iterator != rootKeys.cend(); ++iterator)
        roots.append({iterator.key(), iterator.value()});
    std::sort(roots.begin(), roots.end(), [](const auto &left, const auto &right) {
        return std::tie(left.keyId, left.publicKey) < std::tie(right.keyId, right.publicKey);
    });
    const auto documentLess = [](const auto &left, const auto &right) {
        return std::tie(left.documentSha256, left.signatureSha256)
               < std::tie(right.documentSha256, right.signatureSha256);
    };
    std::sort(policyIdentities.begin(), policyIdentities.end(), documentLess);
    std::sort(authorizationIdentities.begin(), authorizationIdentities.end(), documentLess);

    QByteArray input(authorizationProvenanceSetDomain);
    input.append('\0');
    appendUint32(&input, quint32(roots.size()));
    for (const RootKeyMaterialIdentity &root : std::as_const(roots)) {
        input.append(root.keyId);
        input.append(root.publicKey);
    }
    appendUint32(&input, quint32(policyIdentities.size()));
    for (const SignedDocumentMaterialIdentity &identity : std::as_const(policyIdentities)) {
        input.append(identity.documentSha256);
        input.append(identity.signatureSha256);
    }
    appendUint32(&input, quint32(authorizationIdentities.size()));
    for (const SignedDocumentMaterialIdentity &identity : std::as_const(authorizationIdentities)) {
        input.append(identity.documentSha256);
        input.append(identity.signatureSha256);
    }
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}

static DeviceAdapterAuthorizationVersion dataAuthorizationVersion(AuthorizationVersion version)
{
    return version == AuthorizationVersion::V1 ? DeviceAdapterAuthorizationVersion::V1
                                               : DeviceAdapterAuthorizationVersion::V2;
}

static bool verifySignature(
    const QByteArray &signature, const QByteArray &publicKey, const QByteArray &message)
{
    return signature.size() == ed25519SignatureBytes && publicKey.size() == ed25519PublicKeyBytes
           && crypto_ed25519_check(
                  reinterpret_cast<const uint8_t *>(signature.constData()),
                  reinterpret_cast<const uint8_t *>(publicKey.constData()),
                  reinterpret_cast<const uint8_t *>(message.constData()),
                  size_t(message.size()))
                  == 0;
}

static bool appendNestedSymbolicLinkDiagnostics(
    const Utils::FilePath &root, const QString &kind, QStringList *diagnostics)
{
    if (root.isEmpty() || !root.exists() || !root.isDir())
        return false;
    bool found = false;
    QDirIterator iterator(
        root.toFSPathString(),
        QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (info.isSymLink()) {
            found = true;
            diagnostics->append(QString("%1: symbolic-link %2 are forbidden").arg(path, kind));
        }
    }
    return found;
}

static QHash<QByteArray, QByteArray> loadRootKeys(
    const Utils::FilePath &trustRoot,
    bool *materialPathClosureComplete,
    QStringList *diagnostics)
{
    QHash<QByteArray, QByteArray> result;
    QSet<QByteArray> conflicts;
    if (trustRoot.isEmpty() || !trustRoot.exists() || !trustRoot.isDir())
        return result;
    const QFileInfo trustRootInfo(trustRoot.toFSPathString());
    if (trustRootInfo.isSymLink() || trustRootInfo.canonicalFilePath().isEmpty()) {
        *materialPathClosureComplete = false;
        diagnostics->append(
            QString("%1: authorization trust root must not be a symbolic link")
                .arg(trustRoot.toUserOutput()));
        return result;
    }
    if (appendNestedSymbolicLinkDiagnostics(trustRoot, "trust-root entries", diagnostics))
        *materialPathClosureComplete = false;
    const QString canonicalTrustRoot = trustRootInfo.canonicalFilePath();
    QDirIterator iterator(
        trustRoot.toFSPathString(),
        {"*.pub"},
        QDir::Files | QDir::NoSymLinks | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QString error;
        const std::optional<QByteArray> publicKey
            = readStableFile(path, canonicalTrustRoot, ed25519PublicKeyBytes, &error);
        if (!publicKey || publicKey->size() != ed25519PublicKeyBytes) {
            diagnostics->append(
                error.isEmpty()
                    ? QString("%1: trust key must contain exactly 32 raw bytes").arg(path)
                    : error);
            continue;
        }
        const QByteArray keyId = QCryptographicHash::hash(*publicKey, QCryptographicHash::Sha256);
        const QString expectedName = QString::fromLatin1(keyId.toHex()) + ".pub";
        if (QFileInfo(path).fileName() != expectedName) {
            diagnostics->append(
                QString("%1: trust key must be raw32 and named by its SHA-256").arg(path));
            continue;
        }
        if (result.contains(keyId)) {
            conflicts.insert(keyId);
            diagnostics->append(QString("%1: duplicate authorization root key").arg(path));
            continue;
        }
        result.insert(keyId, *publicKey);
    }
    for (const QByteArray &keyId : std::as_const(conflicts))
        result.remove(keyId);
    return result;
}

static std::optional<Policy> parsePolicy(
    const QString &path,
    const QString &canonicalRoot,
    const QHash<QByteArray, QByteArray> &rootKeys,
    QString *error)
{
    const std::optional<QPair<QByteArray, QJsonObject>> canonical
        = readCanonicalObject(path, canonicalRoot, error);
    const std::optional<QByteArray> signature = readDetachedSignature(path, canonicalRoot, error);
    if (!canonical || !signature)
        return std::nullopt;
    const std::optional<QPair<QByteArray, QJsonObject>> confirmedCanonical
        = readCanonicalObject(path, canonicalRoot, error);
    const std::optional<QByteArray> confirmedSignature
        = readDetachedSignature(path, canonicalRoot, error);
    if (!confirmedCanonical || !confirmedSignature || confirmedCanonical->first != canonical->first
        || *confirmedSignature != *signature) {
        fail(error, QString("%1: policy JSON or signature changed during verification").arg(path));
        return std::nullopt;
    }
    const QJsonObject object = canonical->second;
    const QString context = QFileInfo(path).fileName();
    if (!exactKeys(
            object,
            {"format",
             "formatVersion",
             "canonicalization",
             "policyId",
             "policyRevision",
             "rootKeyId",
             "signers",
             "revokedSignerKeyIds",
             "revokedAuthorizationIds"},
            context,
            error)
        || !parseConstantString(object, "format", policyFormat, context, error)
        || !parseConstantString(object, "canonicalization", canonicalization, context, error)) {
        return std::nullopt;
    }

    Policy policy;
    policy.sourcePath = path;
    policy.canonicalBytes = canonical->first;
    policy.signature = *signature;
    policy.canonicalSha256 = QCryptographicHash::hash(canonical->first, QCryptographicHash::Sha256);
    if (!parseUnsigned(object, "formatVersion", 1, context, &policy.revision, error)
        || policy.revision != 1 || !parseString(object, "policyId", context, &policy.id, error)
        || !stableIdentifier(policy.id)
        || !parseUnsigned(object, "policyRevision", 1, context, &policy.revision, error)
        || !parseHex(object, "rootKeyId", ed25519PublicKeyBytes, context, &policy.rootKeyId, error)) {
        return std::nullopt;
    }
    const auto rootKey = rootKeys.constFind(policy.rootKeyId);
    if (rootKey == rootKeys.cend()) {
        fail(error, QString("%1: rootKeyId is not trusted").arg(context));
        return std::nullopt;
    }
    if (!verifySignature(
            *signature, *rootKey, signedMessage(policySignatureDomain, canonical->first))) {
        fail(error, QString("%1: root policy signature is invalid").arg(context));
        return std::nullopt;
    }

    const QJsonValue signersValue = object.value("signers");
    if (!signersValue.isArray() || signersValue.toArray().isEmpty()) {
        fail(error, QString("%1.signers must be a non-empty array").arg(context));
        return std::nullopt;
    }
    QByteArray previousKeyId;
    for (qsizetype index = 0; index < signersValue.toArray().size(); ++index) {
        const QJsonValue value = signersValue.toArray().at(index);
        const QString signerContext = QString("%1.signers[%2]").arg(context).arg(index);
        if (!value.isObject()
            || !exactKeys(
                value.toObject(),
                {"keyId", "publicKey", "adapterIds", "decisions"},
                signerContext,
                error)) {
            return std::nullopt;
        }
        const QJsonObject signerObject = value.toObject();
        Signer signer;
        if (!parseHex(signerObject, "keyId", ed25519PublicKeyBytes, signerContext, &signer.keyId, error)
            || !parseHex(
                signerObject,
                "publicKey",
                ed25519PublicKeyBytes,
                signerContext,
                &signer.publicKey,
                error)
            || signer.keyId != QCryptographicHash::hash(signer.publicKey, QCryptographicHash::Sha256)
            || !parseSortedStrings(signerObject, "adapterIds", signerContext, &signer.adapterIds, error)
            || signer.adapterIds.isEmpty()
            || std::any_of(
                signer.adapterIds.cbegin(),
                signer.adapterIds.cend(),
                [](const QString &id) { return !stableIdentifier(id); })
            || !parseSortedStrings(signerObject, "decisions", signerContext, &signer.decisions, error)
            || signer.decisions.isEmpty()
            || std::any_of(
                signer.decisions.cbegin(), signer.decisions.cend(), [](const QString &decision) {
                    return decision != "allow" && decision != "deny";
                })) {
            if (error->isEmpty())
                fail(error, QString("%1 is invalid").arg(signerContext));
            return std::nullopt;
        }
        if ((!previousKeyId.isEmpty() && signer.keyId <= previousKeyId)
            || policy.signers.contains(signer.keyId)) {
            fail(error, QString("%1.signers must be unique and keyId-sorted").arg(context));
            return std::nullopt;
        }
        previousKeyId = signer.keyId;
        policy.signers.insert(signer.keyId, signer);
    }

    QStringList revokedSignerIds;
    QStringList revokedAuthorizationIds;
    if (!parseSortedStrings(object, "revokedSignerKeyIds", context, &revokedSignerIds, error)
        || !parseSortedStrings(
            object, "revokedAuthorizationIds", context, &revokedAuthorizationIds, error)) {
        return std::nullopt;
    }
    for (const QString &value : std::as_const(revokedSignerIds)) {
        const QByteArray keyId = QByteArray::fromHex(value.toLatin1());
        if (value.size() != 64 || keyId.size() != ed25519PublicKeyBytes
            || QString::fromLatin1(keyId.toHex()) != value) {
            fail(error, QString("%1.revokedSignerKeyIds is invalid").arg(context));
            return std::nullopt;
        }
        policy.revokedSignerKeyIds.insert(keyId);
    }
    for (const QString &value : std::as_const(revokedAuthorizationIds)) {
        if (!stableIdentifier(value)) {
            fail(error, QString("%1.revokedAuthorizationIds is invalid").arg(context));
            return std::nullopt;
        }
        policy.revokedAuthorizationIds.insert(value);
    }
    return policy;
}

static QString authorizationTargetKey(const QJsonObject &adapter)
{
    return adapter.value("id").toString() + '\n' + adapter.value("version").toString() + '\n'
           + adapter.value("contentSha256").toString();
}

static bool validateAdapterBindingShape(
    const QJsonObject &adapter,
    AuthorizationVersion authorizationVersion,
    const QString &context,
    QString *error)
{
    QStringList keys{
        "id",
        "version",
        "contentSha256",
        "qualification",
        "match",
        "controllerAdapterTarget",
        "processDataProfiles",
        "actions",
        "evidenceSha256",
    };
    if (authorizationVersion == AuthorizationVersion::V2) {
        keys.append("schemaVersion");
        keys.append("parameterDefinitions");
    }
    if (!exactKeys(adapter, keys, context, error)) {
        return false;
    }
    QString id;
    QString version;
    QByteArray hash;
    const QJsonValue evidence = adapter.value("evidenceSha256");
    if (!parseString(adapter, "id", context, &id, error) || !stableIdentifier(id)
        || !parseString(adapter, "version", context, &version, error)
        || !parseHex(adapter, "contentSha256", 32, context, &hash, error) || !evidence.isString()
        || (!evidence.toString().isEmpty()
            && (evidence.toString().size() != 64
                || QByteArray::fromHex(evidence.toString().toLatin1()).size() != 32
                || QString::fromLatin1(QByteArray::fromHex(evidence.toString().toLatin1()).toHex())
                       != evidence.toString()))) {
        if (error->isEmpty())
            fail(error, QString("%1.evidenceSha256 is not canonical").arg(context));
        return false;
    }
    const QJsonValue match = adapter.value("match");
    const QJsonValue target = adapter.value("controllerAdapterTarget");
    if (!match.isObject()
        || !exactKeys(
            match.toObject(),
            {"vendorId", "productCode", "minimumRevision", "maximumRevision", "exactEsiSha256"},
            context + ".match",
            error)
        || !target.isObject()
        || !exactKeys(
            target.toObject(),
            {"adapterId", "adapterVersion", "adapterSha256", "esiSha256"},
            context + ".controllerAdapterTarget",
            error)) {
        return false;
    }
    const QJsonValue profiles = adapter.value("processDataProfiles");
    const QJsonValue actions = adapter.value("actions");
    if (!profiles.isArray() || !actions.isArray())
        return fail(error, QString("%1 profile and action closures must be arrays").arg(context));
    QString previous;
    for (qsizetype index = 0; index < profiles.toArray().size(); ++index) {
        const QJsonValue value = profiles.toArray().at(index);
        const QString itemContext = QString("%1.processDataProfiles[%2]").arg(context).arg(index);
        if (!value.isObject()
            || !exactKeys(
                value.toObject(),
                {"id",
                 "signedPdoProfileId",
                 "signedDcProfileId",
                 "rxPdoIndices",
                 "txPdoIndices",
                 "requiredSignals"},
                itemContext,
                error)) {
            return false;
        }
        const QString current = value.toObject().value("id").toString();
        if (!stableIdentifier(current) || (!previous.isEmpty() && current <= previous))
            return fail(error, QString("%1 profiles are not a strict closure").arg(context));
        previous = current;
    }
    previous.clear();
    for (qsizetype index = 0; index < actions.toArray().size(); ++index) {
        const QJsonValue value = actions.toArray().at(index);
        const QString itemContext = QString("%1.actions[%2]").arg(context).arg(index);
        if (!value.isObject()
            || !exactKeys(
                value.toObject(),
                {"id",
                 "enabled",
                 "qualification",
                 "disabledReason",
                 "requiresDc",
                 "expectedSignedDefinitionSha256",
                 "signedPdoProfileIds"},
                itemContext,
                error)) {
            return false;
        }
        const QString current = value.toObject().value("id").toString();
        if (!stableIdentifier(current) || (!previous.isEmpty() && current <= previous))
            return fail(error, QString("%1 actions are not a strict closure").arg(context));
        previous = current;
    }
    if (authorizationVersion == AuthorizationVersion::V2) {
        if (!parseConstantString(
                adapter, "schemaVersion", adapterSchemaVersionV4, context, error)) {
            return false;
        }
        const QJsonValue definitions = adapter.value("parameterDefinitions");
        if (!definitions.isArray()
            || definitions.toArray().size() > maximumDeviceParameterDefinitionsPerAdapter) {
            return fail(
                error,
                QString("%1.parameterDefinitions must be a bounded array").arg(context));
        }
        previous.clear();
        for (qsizetype index = 0; index < definitions.toArray().size(); ++index) {
            const QJsonValue value = definitions.toArray().at(index);
            const QString itemContext = QString("%1.parameterDefinitions[%2]")
                                            .arg(context)
                                            .arg(index);
            if (!value.isObject()
                || !exactKeys(
                    value.toObject(), {"id", "definitionSha256"}, itemContext, error)) {
                return false;
            }
            QString id;
            QByteArray definitionSha256;
            if (!parseString(value.toObject(), "id", itemContext, &id, error)
                || !parameterIdentifier(id)
                || !parseHex(
                    value.toObject(),
                    "definitionSha256",
                    32,
                    itemContext,
                    &definitionSha256,
                    error)
                || std::all_of(
                    definitionSha256.cbegin(),
                    definitionSha256.cend(),
                    [](char byte) { return byte == 0; })
                || (!previous.isEmpty() && id <= previous)) {
                if (error->isEmpty()) {
                    fail(
                        error,
                        QString("%1.parameterDefinitions is not a strict closure").arg(context));
                }
                return false;
            }
            previous = id;
        }
    }
    return true;
}

static std::optional<Authorization> parseAuthorization(
    const QString &path, const QString &canonicalRoot, QString *error)
{
    const std::optional<QPair<QByteArray, QJsonObject>> canonical
        = readCanonicalObject(path, canonicalRoot, error);
    const std::optional<QByteArray> signature = readDetachedSignature(path, canonicalRoot, error);
    if (!canonical || !signature)
        return std::nullopt;
    const std::optional<QPair<QByteArray, QJsonObject>> confirmedCanonical
        = readCanonicalObject(path, canonicalRoot, error);
    const std::optional<QByteArray> confirmedSignature
        = readDetachedSignature(path, canonicalRoot, error);
    if (!confirmedCanonical || !confirmedSignature || confirmedCanonical->first != canonical->first
        || *confirmedSignature != *signature) {
        fail(
            error,
            QString("%1: authorization JSON or signature changed during verification").arg(path));
        return std::nullopt;
    }
    const QJsonObject object = canonical->second;
    const QString context = QFileInfo(path).fileName();
    QString format;
    if (!exactKeys(
            object,
            {"format",
             "formatVersion",
             "canonicalization",
             "authorizationId",
             "policyId",
             "policyRevision",
             "signerKeyId",
             "decision",
             "adapter"},
            context,
            error)
        || !parseString(object, "format", context, &format, error)
        || !parseConstantString(object, "canonicalization", canonicalization, context, error)) {
        return std::nullopt;
    }
    Authorization authorization;
    quint32 expectedFormatVersion = 0;
    if (format == authorizationFormatV1) {
        authorization.version = AuthorizationVersion::V1;
        expectedFormatVersion = 1;
    } else if (format == authorizationFormatV2) {
        authorization.version = AuthorizationVersion::V2;
        expectedFormatVersion = 2;
    } else {
        fail(error, QString("%1.format is unsupported").arg(context));
        return std::nullopt;
    }
    authorization.sourcePath = path;
    authorization.canonicalBytes = canonical->first;
    authorization.signature = *signature;
    quint32 formatVersion = 0;
    if (!parseUnsigned(object, "formatVersion", 1, context, &formatVersion, error))
        return std::nullopt;
    if (formatVersion != expectedFormatVersion) {
        fail(
            error,
            QString("%1.formatVersion must equal %2 for its format")
                .arg(context)
                .arg(expectedFormatVersion));
        return std::nullopt;
    }
    if (!parseString(object, "authorizationId", context, &authorization.id, error)
        || !stableIdentifier(authorization.id)
        || !parseString(object, "policyId", context, &authorization.policyId, error)
        || !stableIdentifier(authorization.policyId)
        || !parseUnsigned(object, "policyRevision", 1, context, &authorization.policyRevision, error)
        || !parseHex(
            object, "signerKeyId", ed25519PublicKeyBytes, context, &authorization.signerKeyId, error)
        || !parseString(object, "decision", context, &authorization.decision, error)
        || (authorization.decision != "allow" && authorization.decision != "deny")) {
        if (error->isEmpty())
            fail(error, QString("%1 is invalid").arg(context));
        return std::nullopt;
    }
    const QJsonValue adapter = object.value("adapter");
    if (!adapter.isObject()
        || !validateAdapterBindingShape(
            adapter.toObject(), authorization.version, context + ".adapter", error)) {
        return std::nullopt;
    }
    authorization.adapter = adapter.toObject();
    authorization.targetKey = authorizationTargetKey(authorization.adapter);
    return authorization;
}

static QString manifestTargetKey(const DeviceAdapterManifest &manifest)
{
    return manifest.id.value + '\n' + manifest.version + '\n'
           + QString::fromLatin1(manifest.contentSha256.toHex());
}

} // namespace

void applyDeviceAdapterAuthorizations(
    const DeviceAdapterAuthorizationRoots &roots,
    QList<DeviceAdapterManifest> *manifests,
    QHash<QString, AcceptedDeviceAdapterPolicy> *acceptedPolicies,
    QStringList *diagnostics,
    DeviceAdapterAuthorizationEvaluation *evaluation)
{
    diagnostics->clear();
    *evaluation = {};
    for (DeviceAdapterManifest &manifest : *manifests) {
        manifest.signatureVerified = false;
        manifest.realHardwareAllowed = false;
    }
    if (roots.authorizationRoot.isEmpty() || !roots.authorizationRoot.exists()
        || !roots.authorizationRoot.isDir()) {
        return;
    }

    const QFileInfo authorizationRootInfo(roots.authorizationRoot.toFSPathString());
    if (authorizationRootInfo.isSymLink()
        || authorizationRootInfo.canonicalFilePath().isEmpty()) {
        diagnostics->append(
            QString("%1: authorization root must not be a symbolic link")
                .arg(roots.authorizationRoot.toUserOutput()));
        return;
    }
    const QString canonicalAuthorizationRoot = authorizationRootInfo.canonicalFilePath();
    bool materialPathClosureComplete = true;
    const QHash<QByteArray, QByteArray> rootKeys
        = loadRootKeys(roots.trustRoot, &materialPathClosureComplete, diagnostics);
    int rootKeyPathCount = 0;
    if (!roots.trustRoot.isEmpty() && roots.trustRoot.exists() && roots.trustRoot.isDir()) {
        QDirIterator rootKeyIterator(
            roots.trustRoot.toFSPathString(),
            {"*.pub"},
            QDir::Files | QDir::NoSymLinks | QDir::Hidden,
            QDirIterator::Subdirectories);
        while (rootKeyIterator.hasNext()) {
            rootKeyIterator.next();
            ++rootKeyPathCount;
        }
    }
    materialPathClosureComplete
        = !appendNestedSymbolicLinkDiagnostics(
              roots.authorizationRoot, "authorization-root entries", diagnostics)
          && materialPathClosureComplete;
    QList<QString> policyPaths;
    QDirIterator policyIterator(
        roots.authorizationRoot.toFSPathString(),
        {"*.policy.json"},
        QDir::Files | QDir::NoSymLinks | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (policyIterator.hasNext())
        policyPaths.append(policyIterator.next());
    policyPaths.sort();
    int policySignaturePathCount = 0;
    QDirIterator policySignatureIterator(
        roots.authorizationRoot.toFSPathString(),
        {"*.policy.sig"},
        QDir::Files | QDir::NoSymLinks | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (policySignatureIterator.hasNext()) {
        policySignatureIterator.next();
        ++policySignaturePathCount;
    }

    QHash<QString, QList<Policy>> policiesById;
    QList<SignedDocumentMaterialIdentity> policyIdentities;
    for (const QString &path : std::as_const(policyPaths)) {
        QString error;
        const std::optional<Policy> policy
            = parsePolicy(path, canonicalAuthorizationRoot, rootKeys, &error);
        if (!policy) {
            diagnostics->append(error);
            continue;
        }
        policyIdentities.append(
            {policy->canonicalSha256,
             QCryptographicHash::hash(policy->signature, QCryptographicHash::Sha256)});
        policiesById[policy->id].append(*policy);
    }

    QHash<QString, Policy> policies;
    for (auto iterator = policiesById.cbegin(); iterator != policiesById.cend(); ++iterator) {
        QList<Policy> candidates = iterator.value();
        std::sort(candidates.begin(), candidates.end(), [](const Policy &left, const Policy &right) {
            return left.revision > right.revision;
        });
        if (candidates.size() > 1 && candidates.at(0).revision == candidates.at(1).revision) {
            diagnostics->append(
                QString("authorization policy %1 has a duplicate revision").arg(iterator.key()));
            continue;
        }
        const Policy selected = candidates.constFirst();
        const auto accepted = acceptedPolicies->constFind(selected.id);
        const quint32 acceptedRevision = accepted == acceptedPolicies->cend()
                                             ? minimumPolicyRevision
                                             : accepted->revision;
        if (selected.revision < acceptedRevision) {
            diagnostics->append(QString("authorization policy %1 revision %2 is a rollback below %3")
                                    .arg(selected.id)
                                    .arg(selected.revision)
                                    .arg(acceptedRevision));
            continue;
        }
        if (accepted != acceptedPolicies->cend() && selected.revision == accepted->revision
            && (selected.canonicalSha256 != accepted->canonicalSha256
                || selected.rootKeyId != accepted->rootKeyId)) {
            diagnostics->append(QString("authorization policy %1 revision %2 changed identity")
                                    .arg(selected.id)
                                    .arg(selected.revision));
            continue;
        }
        acceptedPolicies
            ->insert(selected.id, {selected.revision, selected.canonicalSha256, selected.rootKeyId});
        policies.insert(selected.id, selected);
    }

    QList<QString> authorizationPaths;
    QDirIterator authorizationIterator(
        roots.authorizationRoot.toFSPathString(),
        {"*.authorization.json"},
        QDir::Files | QDir::NoSymLinks | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (authorizationIterator.hasNext())
        authorizationPaths.append(authorizationIterator.next());
    authorizationPaths.sort();
    int authorizationSignaturePathCount = 0;
    QDirIterator authorizationSignatureIterator(
        roots.authorizationRoot.toFSPathString(),
        {"*.authorization.sig"},
        QDir::Files | QDir::NoSymLinks | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (authorizationSignatureIterator.hasNext()) {
        authorizationSignatureIterator.next();
        ++authorizationSignaturePathCount;
    }

    QHash<QString, QList<Authorization>> authorizationsByTarget;
    QHash<QString, int> authorizationIdCounts;
    QList<SignedDocumentMaterialIdentity> authorizationIdentities;
    for (const QString &path : std::as_const(authorizationPaths)) {
        QString error;
        const std::optional<Authorization> authorization
            = parseAuthorization(path, canonicalAuthorizationRoot, &error);
        if (!authorization) {
            diagnostics->append(error);
            continue;
        }
        authorizationIdentities.append(
            {QCryptographicHash::hash(
                 authorization->canonicalBytes, QCryptographicHash::Sha256),
             QCryptographicHash::hash(
                 authorization->signature, QCryptographicHash::Sha256)});
        authorizationIdCounts[authorization->id] += 1;
        const auto policy = policies.constFind(authorization->policyId);
        if (policy == policies.cend() || policy->revision != authorization->policyRevision) {
            diagnostics->append(
                QString("%1: authorization policy identity or revision is unavailable").arg(path));
            continue;
        }
        const auto signer = policy->signers.constFind(authorization->signerKeyId);
        if (signer == policy->signers.cend()) {
            diagnostics->append(QString("%1: authorization signer is outside policy").arg(path));
            continue;
        }
        if (policy->revokedSignerKeyIds.contains(authorization->signerKeyId)
            || policy->revokedAuthorizationIds.contains(authorization->id)) {
            diagnostics->append(QString("%1: authorization or signer is revoked").arg(path));
            continue;
        }
        const QString adapterId = authorization->adapter.value("id").toString();
        if (!signer->adapterIds.contains(adapterId)
            || !signer->decisions.contains(authorization->decision)) {
            diagnostics->append(QString("%1: authorization exceeds signer scope").arg(path));
            continue;
        }
        if (!verifySignature(
                authorization->signature,
                signer->publicKey,
                signedMessage(
                    authorization->version == AuthorizationVersion::V1
                        ? authorizationSignatureDomainV1
                        : authorizationSignatureDomainV2,
                    authorization->canonicalBytes))) {
            diagnostics->append(QString("%1: authorization signature is invalid").arg(path));
            continue;
        }
        authorizationsByTarget[authorization->targetKey].append(*authorization);
    }

    evaluation->materialIdentityComplete
        = materialPathClosureComplete && rootKeys.size() == rootKeyPathCount
          && policyIdentities.size() == policyPaths.size()
          && policySignaturePathCount == policyPaths.size()
          && authorizationIdentities.size() == authorizationPaths.size()
          && authorizationSignaturePathCount == authorizationPaths.size();
    if (evaluation->materialIdentityComplete) {
        evaluation->authorizationSetSha256 = canonicalAuthorizationSetSha256(
            rootKeys, policyIdentities, authorizationIdentities);
    }

    for (auto iterator = authorizationsByTarget.cbegin(); iterator != authorizationsByTarget.cend();
         ++iterator) {
        const QList<Authorization> candidates = iterator.value();
        if (candidates.size() != 1 || authorizationIdCounts.value(candidates.constFirst().id) != 1) {
            diagnostics->append(
                QString("adapter authorization target %1 is duplicate or conflicting")
                    .arg(iterator.key().section('\n', 0, 1).replace('\n', ' ')));
            continue;
        }
        const Authorization &authorization = candidates.constFirst();
        const auto manifest = std::find_if(
            manifests->begin(),
            manifests->end(),
            [&authorization](const DeviceAdapterManifest &item) {
                return manifestTargetKey(item) == authorization.targetKey;
            });
        if (manifest == manifests->end()) {
            diagnostics->append(QString("%1: authorization does not match an installed adapter")
                                    .arg(authorization.sourcePath));
            continue;
        }
        const DeviceAdapterAuthorizationVersion authorizationVersion
            = dataAuthorizationVersion(authorization.version);
        const std::optional<CanonicalDeviceAdapterAuthorizationBinding> binding
            = canonicalDeviceAdapterAuthorizationBinding(*manifest, authorizationVersion);
        const QJsonDocument bindingDocument
            = binding ? QJsonDocument::fromJson(binding->exactBytes) : QJsonDocument{};
        if (!binding || !binding->isValid() || !bindingDocument.isObject()
            || authorization.adapter != bindingDocument.object()) {
            diagnostics->append(
                QString("%1: authorization binding does not exactly match its Adapter version")
                    .arg(authorization.sourcePath));
            continue;
        }
        if (authorization.decision == "allow") {
            manifest->signatureVerified = true;
            manifest->realHardwareAllowed = true;
            const Policy &policy = policies.value(authorization.policyId);
            DeviceAdapterAuthorizationProvenance provenance;
            provenance.adapterContractVersion = manifest->contractVersion;
            provenance.authorizationVersion = authorizationVersion;
            provenance.decision = DeviceAdapterAuthorizationDecision::Allow;
            provenance.adapterId = manifest->id;
            provenance.adapterVersion = manifest->version;
            provenance.adapterContentSha256 = manifest->contentSha256;
            provenance.adapterBindingSha256 = binding->sha256;
            provenance.authorizationId = authorization.id;
            provenance.authorizationDocumentSha256 = QCryptographicHash::hash(
                authorization.canonicalBytes, QCryptographicHash::Sha256);
            provenance.authorizationSignatureSha256 = QCryptographicHash::hash(
                authorization.signature, QCryptographicHash::Sha256);
            provenance.policyId = policy.id;
            provenance.policyRevision = policy.revision;
            provenance.policyDocumentSha256 = policy.canonicalSha256;
            provenance.policySignatureSha256
                = QCryptographicHash::hash(policy.signature, QCryptographicHash::Sha256);
            provenance.rootKeyId = policy.rootKeyId;
            provenance.signerKeyId = authorization.signerKeyId;
            evaluation->provenances.append(provenance);
        }
    }

    std::sort(
        evaluation->provenances.begin(),
        evaluation->provenances.end(),
        [](const DeviceAdapterAuthorizationProvenance &left,
           const DeviceAdapterAuthorizationProvenance &right) {
            return std::tie(left.adapterId.value, left.adapterVersion, left.adapterContentSha256)
                   < std::tie(
                       right.adapterId.value,
                       right.adapterVersion,
                       right.adapterContentSha256);
        });

    if (!diagnostics->isEmpty()) {
        evaluation->provenances.clear();
        for (DeviceAdapterManifest &manifest : *manifests) {
            manifest.signatureVerified = false;
            manifest.realHardwareAllowed = false;
        }
    }
}

} // namespace EtherCAT::DeviceAdapters::Internal
