// Copyright (C) 2026 Embed Labs

#include "deviceadapterauthorizationprovenance.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace EtherCAT::Data {

namespace {

constexpr char bindingDomainV1[]
    = "embed-labs.ethercat-device-adapter-authorization-binding/v1";
constexpr char bindingDomainV2[]
    = "embed-labs.ethercat-device-adapter-authorization-binding/v2";
constexpr char adapterSchemaVersionV4[] = "embed-labs.device-adapter/v4";
constexpr char lowercaseHex[] = "0123456789abcdef";

static bool isValidSha256(const QByteArray &sha256)
{
    return sha256.size() == 32
           && std::any_of(sha256.cbegin(), sha256.cend(), [](char byte) { return byte != 0; });
}

static bool isStableIdentifier(const QString &text)
{
    static const QRegularExpression pattern(
        QString::fromLatin1("^[A-Za-z0-9][A-Za-z0-9._:/{}-]{0,255}$"));
    return pattern.match(text).hasMatch();
}

static const char *bindingDomain(DeviceAdapterAuthorizationVersion version)
{
    switch (version) {
    case DeviceAdapterAuthorizationVersion::V1:
        return bindingDomainV1;
    case DeviceAdapterAuthorizationVersion::V2:
        return bindingDomainV2;
    case DeviceAdapterAuthorizationVersion::None:
        return nullptr;
    }
    return nullptr;
}

static bool contractMatchesAuthorization(
    DeviceAdapterContractVersion contractVersion,
    DeviceAdapterAuthorizationVersion authorizationVersion)
{
    return (contractVersion == DeviceAdapterContractVersion::V3
            && authorizationVersion == DeviceAdapterAuthorizationVersion::V1)
           || (contractVersion == DeviceAdapterContractVersion::V4
               && authorizationVersion == DeviceAdapterAuthorizationVersion::V2);
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
    switch (qualification) {
    case DeviceControlActionQualification::Unqualified:
        return "unqualified";
    case DeviceControlActionQualification::Qualified:
        return "qualified";
    }
    return {};
}

static QJsonArray stringArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values)
        result.append(value);
    return result;
}

static void appendCanonicalString(const QString &value, QByteArray *result)
{
    const auto appendEscapedCodeUnit = [result](quint16 codeUnit) {
        result->append("\\u");
        result->append(lowercaseHex[(codeUnit >> 12) & 0xf]);
        result->append(lowercaseHex[(codeUnit >> 8) & 0xf]);
        result->append(lowercaseHex[(codeUnit >> 4) & 0xf]);
        result->append(lowercaseHex[codeUnit & 0xf]);
    };
    result->append('"');
    for (QChar character : value) {
        const quint16 codeUnit = character.unicode();
        switch (codeUnit) {
        case '"':
            result->append("\\\"");
            continue;
        case '\\':
            result->append("\\\\");
            continue;
        case '\b':
            result->append("\\b");
            continue;
        case '\f':
            result->append("\\f");
            continue;
        case '\n':
            result->append("\\n");
            continue;
        case '\r':
            result->append("\\r");
            continue;
        case '\t':
            result->append("\\t");
            continue;
        default:
            break;
        }
        if (codeUnit < 0x20 || codeUnit == 0x7f) {
            result->append("\\u00");
            result->append(lowercaseHex[(codeUnit >> 4) & 0xf]);
            result->append(lowercaseHex[codeUnit & 0xf]);
            continue;
        }
        if (codeUnit <= 0x7e) {
            result->append(char(codeUnit));
            continue;
        }
        appendEscapedCodeUnit(codeUnit);
    }
    result->append('"');
}

static bool appendCanonicalJson(const QJsonValue &value, QByteArray *result)
{
    if (value.isNull() || value.isUndefined()) {
        result->append("null");
        return true;
    }
    if (value.isBool()) {
        result->append(value.toBool() ? "true" : "false");
        return true;
    }
    if (value.isString()) {
        appendCanonicalString(value.toString(), result);
        return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        constexpr double largestExactlyRepresentableInteger = 9007199254740991.0;
        if (!std::isfinite(number) || std::trunc(number) != number
            || std::abs(number) > largestExactlyRepresentableInteger) {
            return false;
        }
        result->append(QByteArray::number(qint64(number)));
        return true;
    }
    if (value.isArray()) {
        result->append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index != 0)
                result->append(',');
            if (!appendCanonicalJson(array.at(index), result))
                return false;
        }
        result->append(']');
        return true;
    }
    if (value.isObject()) {
        result->append('{');
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
            return left.toUtf8() < right.toUtf8();
        });
        for (qsizetype index = 0; index < keys.size(); ++index) {
            if (index != 0)
                result->append(',');
            const QString &key = keys.at(index);
            appendCanonicalString(key, result);
            result->append(':');
            if (!appendCanonicalJson(object.value(key), result))
                return false;
        }
        result->append('}');
        return true;
    }
    return false;
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
        "adapterSha256", QString::fromLatin1(manifest.controllerAdapterTarget.adapterSha256.toHex()));
    target.insert(
        "esiSha256", QString::fromLatin1(manifest.controllerAdapterTarget.esiSha256.toHex()));

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
    if (manifest.contractVersion == DeviceAdapterContractVersion::V4) {
        QJsonArray definitions;
        for (const DeviceParameterDefinition &definition : manifest.parameterDefinitions) {
            QJsonObject item;
            item.insert("id", definition.id);
            item.insert(
                "definitionSha256", QString::fromLatin1(definition.definitionSha256.toHex()));
            definitions.append(item);
        }
        result.insert("schemaVersion", adapterSchemaVersionV4);
        result.insert("parameterDefinitions", definitions);
    }
    return result;
}

} // namespace

bool CanonicalDeviceAdapterAuthorizationBinding::isValid() const
{
    const char *domain = bindingDomain(authorizationVersion);
    if (!domain || exactBytes.isEmpty()
        || std::any_of(exactBytes.cbegin(), exactBytes.cend(), [](char byte) {
               return quint8(byte) >= 0x7f;
           })) {
        return false;
    }
    QByteArray input(domain);
    input.append('\0');
    input.append(exactBytes);
    return isValidSha256(sha256)
           && QCryptographicHash::hash(input, QCryptographicHash::Sha256) == sha256;
}

bool DeviceAdapterAuthorizationProvenance::isValid() const
{
    return contractMatchesAuthorization(adapterContractVersion, authorizationVersion)
           && decision == DeviceAdapterAuthorizationDecision::Allow
           && isStableIdentifier(adapterId.value) && !adapterVersion.isEmpty()
           && isStableIdentifier(authorizationId) && isStableIdentifier(policyId) && policyRevision
           && isValidSha256(adapterContentSha256) && isValidSha256(adapterBindingSha256)
           && isValidSha256(authorizationDocumentSha256)
           && isValidSha256(authorizationSignatureSha256)
           && isValidSha256(policyDocumentSha256) && isValidSha256(policySignatureSha256)
           && isValidSha256(rootKeyId) && isValidSha256(signerKeyId);
}

bool DeviceAdapterAuthorizationProvenanceSnapshot::isValid() const
{
    if (formatVersion != 1 || !generation)
        return false;
    const bool hasSetIdentity = isValidSha256(authorizationSetSha256);
    if (state == DeviceAdapterAuthorizationProvenanceState::NotInstalled)
        return authorizationSetSha256.isEmpty() && records.isEmpty();
    if (state == DeviceAdapterAuthorizationProvenanceState::ValidationFailed)
        return (authorizationSetSha256.isEmpty() || hasSetIdentity) && records.isEmpty();
    if (!hasSetIdentity)
        return false;
    if (state == DeviceAdapterAuthorizationProvenanceState::Denied)
        return records.isEmpty();
    if (state != DeviceAdapterAuthorizationProvenanceState::Authorized || records.isEmpty())
        return false;

    std::tuple<QString, QString, QByteArray> previous;
    bool hasPrevious = false;
    for (const DeviceAdapterAuthorizationProvenance &record : records) {
        if (!record.isValid())
            return false;
        const auto key
            = std::make_tuple(record.adapterId.value, record.adapterVersion, record.adapterContentSha256);
        if (hasPrevious && key <= previous)
            return false;
        previous = key;
        hasPrevious = true;
    }
    return true;
}

std::optional<CanonicalDeviceAdapterAuthorizationBinding>
canonicalDeviceAdapterAuthorizationBinding(
    const DeviceAdapterManifest &manifest,
    DeviceAdapterAuthorizationVersion authorizationVersion)
{
    const char *domain = bindingDomain(authorizationVersion);
    if (!domain || !contractMatchesAuthorization(manifest.contractVersion, authorizationVersion))
        return std::nullopt;

    QByteArray exactBytes;
    if (!appendCanonicalJson(adapterBinding(manifest), &exactBytes) || exactBytes.isEmpty()) {
        return std::nullopt;
    }

    QByteArray input(domain);
    input.append('\0');
    input.append(exactBytes);
    CanonicalDeviceAdapterAuthorizationBinding result;
    result.authorizationVersion = authorizationVersion;
    result.exactBytes = exactBytes;
    result.sha256 = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
    return result.isValid() ? std::optional(result) : std::nullopt;
}

} // namespace EtherCAT::Data
