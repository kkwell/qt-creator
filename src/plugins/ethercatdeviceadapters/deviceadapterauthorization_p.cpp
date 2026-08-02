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

namespace EtherCAT::DeviceAdapters::Internal {

using namespace Data;

namespace {

constexpr char policyFormat[] = "embed-labs.ethercat-device-adapter-authorization-policy-v1";
constexpr char authorizationFormat[] = "embed-labs.ethercat-device-adapter-authorization-v1";
constexpr char canonicalization[] = "kvell-json-ascii-sorted-compact-lf-v1";
constexpr char policySignatureDomain[]
    = "embed-labs.ethercat-device-adapter-authorization-policy/v1";
constexpr char authorizationSignatureDomain[]
    = "embed-labs.ethercat-device-adapter-authorization/v1";
constexpr qsizetype ed25519PublicKeyBytes = 32;
constexpr qsizetype ed25519SignatureBytes = 64;
constexpr qsizetype maximumDocumentBytes = 1024 * 1024;
constexpr quint32 minimumPolicyRevision = 1;

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
    QByteArray canonicalSha256;
    QString sourcePath;
};

struct Authorization
{
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

static void appendSymbolicLinkDiagnostics(
    const Utils::FilePath &root,
    const QStringList &nameFilters,
    const QString &kind,
    QStringList *diagnostics)
{
    if (root.isEmpty() || !root.exists() || !root.isDir())
        return;
    QDirIterator iterator(
        root.toFSPathString(),
        QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (info.isSymLink() && QDir::match(nameFilters, info.fileName()))
            diagnostics->append(QString("%1: symbolic-link %2 are forbidden").arg(path, kind));
    }
}

static QHash<QByteArray, QByteArray> loadRootKeys(
    const Utils::FilePath &trustRoot, QStringList *diagnostics)
{
    QHash<QByteArray, QByteArray> result;
    QSet<QByteArray> conflicts;
    if (trustRoot.isEmpty() || !trustRoot.exists() || !trustRoot.isDir())
        return result;
    const QFileInfo trustRootInfo(trustRoot.toFSPathString());
    if (trustRootInfo.isSymLink() || trustRootInfo.canonicalFilePath().isEmpty()) {
        diagnostics->append(
            QString("%1: authorization trust root must not be a symbolic link")
                .arg(trustRoot.toUserOutput()));
        return result;
    }
    appendSymbolicLinkDiagnostics(trustRoot, {"*.pub"}, "trust keys", diagnostics);
    const QString canonicalTrustRoot = trustRootInfo.canonicalFilePath();
    QDirIterator iterator(
        trustRoot.toFSPathString(),
        {"*.pub"},
        QDir::Files | QDir::NoSymLinks,
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
    const QJsonObject &adapter, const QString &context, QString *error)
{
    if (!exactKeys(
            adapter,
            {"id",
             "version",
             "contentSha256",
             "qualification",
             "match",
             "controllerAdapterTarget",
             "processDataProfiles",
             "actions",
             "evidenceSha256"},
            context,
            error)) {
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
        || !parseConstantString(object, "format", authorizationFormat, context, error)
        || !parseConstantString(object, "canonicalization", canonicalization, context, error)) {
        return std::nullopt;
    }
    Authorization authorization;
    authorization.sourcePath = path;
    authorization.canonicalBytes = canonical->first;
    authorization.signature = *signature;
    quint32 formatVersion = 0;
    if (!parseUnsigned(object, "formatVersion", 1, context, &formatVersion, error)
        || formatVersion != 1
        || !parseString(object, "authorizationId", context, &authorization.id, error)
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
        || !validateAdapterBindingShape(adapter.toObject(), context + ".adapter", error)) {
        return std::nullopt;
    }
    authorization.adapter = adapter.toObject();
    authorization.targetKey = authorizationTargetKey(authorization.adapter);
    return authorization;
}

static QString qualificationString(DeviceAdapterQualification qualification)
{
    switch (qualification) {
    case DeviceAdapterQualification::Unqualified:
        return "unqualified";
    case DeviceAdapterQualification::Candidate:
        return "candidate";
    case DeviceAdapterQualification::Qualified:
        return "qualified";
    case DeviceAdapterQualification::MockOnly:
        return "mock-only";
    case DeviceAdapterQualification::Revoked:
        return "revoked";
    }
    return {};
}

static QString actionQualificationString(DeviceControlActionQualification qualification)
{
    return QString::fromLatin1(
        qualification == DeviceControlActionQualification::Qualified ? "qualified" : "unqualified");
}

static QJsonArray stringArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values)
        result.append(value);
    return result;
}

static QJsonObject adapterBinding(const DeviceAdapterManifest &manifest)
{
    QJsonObject match;
    match.insert("vendorId", qint64(manifest.match.vendorId));
    match.insert("productCode", qint64(manifest.match.productCode));
    match.insert("minimumRevision", qint64(manifest.match.minimumRevision));
    match.insert("maximumRevision", qint64(manifest.match.maximumRevision));
    match.insert("exactEsiSha256", QString::fromLatin1(manifest.match.exactEsiSha256.toHex()));

    QJsonObject target;
    target.insert("adapterId", manifest.controllerAdapterTarget.adapterId);
    target.insert("adapterVersion", manifest.controllerAdapterTarget.adapterVersion);
    target.insert(
        "adapterSha256",
        QString::fromLatin1(manifest.controllerAdapterTarget.adapterSha256.toHex()));
    target
        .insert("esiSha256", QString::fromLatin1(manifest.controllerAdapterTarget.esiSha256.toHex()));

    QJsonArray profiles;
    for (const ProcessDataProfile &profile : manifest.processDataProfiles) {
        QJsonObject item;
        item.insert("id", profile.id);
        item.insert("signedPdoProfileId", profile.signedPdoProfileId);
        item.insert(
            "signedDcProfileId",
            profile.signedDcProfileId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                : QJsonValue(profile.signedDcProfileId));
        QJsonArray rx;
        for (quint16 index : profile.rxPdoIndices)
            rx.append(index);
        QJsonArray tx;
        for (quint16 index : profile.txPdoIndices)
            tx.append(index);
        QStringList required;
        for (const SemanticSignalId &id : profile.requiredSignals)
            required.append(id.value);
        item.insert("rxPdoIndices", rx);
        item.insert("txPdoIndices", tx);
        item.insert("requiredSignals", stringArray(required));
        profiles.append(item);
    }

    QJsonArray actions;
    for (const DeviceControlAction &action : manifest.controlActions) {
        QJsonObject item;
        item.insert("id", action.id.value);
        item.insert("enabled", action.enabled);
        item.insert("qualification", actionQualificationString(action.signedQualification));
        item.insert(
            "disabledReason",
            action.disabledReason.isEmpty() ? QJsonValue(QJsonValue::Null)
                                            : QJsonValue(action.disabledReason));
        item.insert("requiresDc", action.requiresDc);
        item.insert(
            "expectedSignedDefinitionSha256",
            QString::fromLatin1(action.expectedSignedDefinitionSha256.toHex()));
        item.insert("signedPdoProfileIds", stringArray(action.signedPdoProfileIds));
        actions.append(item);
    }

    QJsonObject result;
    result.insert("id", manifest.id.value);
    result.insert("version", manifest.version);
    result.insert("contentSha256", QString::fromLatin1(manifest.contentSha256.toHex()));
    result.insert("qualification", qualificationString(manifest.qualification));
    result.insert("match", match);
    result.insert("controllerAdapterTarget", target);
    result.insert("processDataProfiles", profiles);
    result.insert("actions", actions);
    result.insert("evidenceSha256", QString::fromLatin1(manifest.evidenceSha256.toHex()));
    return result;
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
    QStringList *diagnostics)
{
    diagnostics->clear();
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
    const QHash<QByteArray, QByteArray> rootKeys = loadRootKeys(roots.trustRoot, diagnostics);
    appendSymbolicLinkDiagnostics(roots.authorizationRoot, {"*.policy.json"}, "policies", diagnostics);
    appendSymbolicLinkDiagnostics(
        roots.authorizationRoot, {"*.authorization.json"}, "authorizations", diagnostics);
    QList<QString> policyPaths;
    QDirIterator policyIterator(
        roots.authorizationRoot.toFSPathString(),
        {"*.policy.json"},
        QDir::Files | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (policyIterator.hasNext())
        policyPaths.append(policyIterator.next());
    policyPaths.sort();

    QHash<QString, QList<Policy>> policiesById;
    for (const QString &path : std::as_const(policyPaths)) {
        QString error;
        const std::optional<Policy> policy
            = parsePolicy(path, canonicalAuthorizationRoot, rootKeys, &error);
        if (!policy) {
            diagnostics->append(error);
            continue;
        }
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
        QDir::Files | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (authorizationIterator.hasNext())
        authorizationPaths.append(authorizationIterator.next());
    authorizationPaths.sort();

    QHash<QString, QList<Authorization>> authorizationsByTarget;
    QHash<QString, int> authorizationIdCounts;
    for (const QString &path : std::as_const(authorizationPaths)) {
        QString error;
        const std::optional<Authorization> authorization
            = parseAuthorization(path, canonicalAuthorizationRoot, &error);
        if (!authorization) {
            diagnostics->append(error);
            continue;
        }
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
                signedMessage(authorizationSignatureDomain, authorization->canonicalBytes))) {
            diagnostics->append(QString("%1: authorization signature is invalid").arg(path));
            continue;
        }
        authorizationsByTarget[authorization->targetKey].append(*authorization);
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
        if (manifest->contractVersion != DeviceAdapterContractVersion::V3
            || authorization.adapter != adapterBinding(*manifest)) {
            diagnostics->append(
                QString("%1: authorization binding does not exactly match the V3 adapter")
                    .arg(authorization.sourcePath));
            continue;
        }
        if (authorization.decision == "allow") {
            manifest->signatureVerified = true;
            manifest->realHardwareAllowed = true;
        }
    }

    if (!diagnostics->isEmpty()) {
        for (DeviceAdapterManifest &manifest : *manifests) {
            manifest.signatureVerified = false;
            manifest.realHardwareAllowed = false;
        }
    }
}

} // namespace EtherCAT::DeviceAdapters::Internal
