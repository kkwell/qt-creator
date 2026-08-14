// Copyright (C) 2026 Embed Labs

#include "runtimepackageactivationjournalcodec_p.h"

#include "canonicaljson_p.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

using Action = Data::RuntimePackageActivationProviderAction;
using AuditEvent = Data::RuntimePackageActivationAuditEvent;
using AuditKind = Data::RuntimePackageActivationAuditKind;
using Outcome = Data::RuntimePackageActivationOutcome;
using Phase = Data::RuntimePackageActivationPhase;
using Reconciliation = Data::RuntimePackageActivationProviderReconciliation;

constexpr std::string_view journalFormat = "embed-labs.runtime-package-activation-journal-v1";
constexpr quint32 journalFormatVersion = 1;
constexpr qsizetype maximumJournalBytes = qsizetype(128) * 1024 * 1024;
constexpr qsizetype maximumBlobBytes = qsizetype(32) * 1024 * 1024;
constexpr std::size_t maximumAuditEvents = 16384;
constexpr std::size_t maximumControllerEvidence = 16384;
constexpr std::size_t maximumDeploymentAuditEvents = 16384;

Utils::ResultError journalError(const QString &detail)
{
    return Utils::ResultError(
        QString::fromLatin1("Runtime package activation journal error: %1").arg(detail));
}

QString fieldPath(const QString &path, std::string_view field)
{
    return path + QLatin1Char('.') + QString::fromLatin1(field.data(), qsizetype(field.size()));
}

QString arrayPath(const QString &path, std::size_t index)
{
    return path + QString::fromLatin1("[%1]").arg(qulonglong(index));
}

bool containsField(const StrictJson &object, std::string_view field)
{
    return object.find(field) != object.end();
}

bool hasExactFields(
    const StrictJson &object,
    const QString &path,
    std::initializer_list<std::string_view> required,
    QString *error)
{
    if (!object.is_object()) {
        *error = QString::fromLatin1("%1 must be an object").arg(path);
        return false;
    }

    std::set<std::string_view> expected;
    for (std::string_view field : required) {
        expected.insert(field);
        if (!containsField(object, field)) {
            *error = QString::fromLatin1("%1 is required").arg(fieldPath(path, field));
            return false;
        }
    }
    for (auto iterator = object.cbegin(); iterator != object.cend(); ++iterator) {
        if (!expected.contains(iterator.key())) {
            *error = QString::fromLatin1("%1 contains an unknown field")
                         .arg(fieldPath(path, iterator.key()));
            return false;
        }
    }
    return true;
}

std::string utf8(const QString &value)
{
    const QByteArray encoded = value.toUtf8();
    return std::string(encoded.constData(), std::size_t(encoded.size()));
}

bool readString(
    const StrictJson &value,
    const QString &path,
    QString *result,
    QString *error,
    qsizetype maximumCharacters = 32768)
{
    if (!value.is_string()) {
        *error = QString::fromLatin1("%1 must be a string").arg(path);
        return false;
    }
    const std::string &bytes = value.get_ref<const std::string &>();
    const QString decoded = QString::fromUtf8(bytes.data(), qsizetype(bytes.size()));
    if (decoded.toUtf8() != QByteArray(bytes.data(), qsizetype(bytes.size()))
        || decoded.size() > maximumCharacters) {
        *error = QString::fromLatin1("%1 is not canonical bounded UTF-8").arg(path);
        return false;
    }
    *result = decoded;
    return true;
}

StrictJson u64Json(quint64 value)
{
    return utf8(QString::number(value, 16).rightJustified(16, QLatin1Char('0')));
}

bool readU64(const StrictJson &value, const QString &path, quint64 *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 16) || text.size() != 16
        || std::any_of(text.cbegin(), text.cend(), [](QChar character) {
               return !(character >= QLatin1Char('0') && character <= QLatin1Char('9'))
                      && !(character >= QLatin1Char('a') && character <= QLatin1Char('f'));
           })) {
        if (error->isEmpty())
            *error = QString::fromLatin1("%1 must be 16 lowercase hexadecimal digits").arg(path);
        return false;
    }
    bool ok = false;
    const quint64 decoded = text.toULongLong(&ok, 16);
    if (!ok || u64Json(decoded).get_ref<const std::string &>() != utf8(text)) {
        *error = QString::fromLatin1("%1 is not a canonical u64").arg(path);
        return false;
    }
    *result = decoded;
    return true;
}

StrictJson i64Json(qint64 value)
{
    return utf8(QString::number(value));
}

bool readI64(const StrictJson &value, const QString &path, qint64 *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 20) || text.isEmpty()) {
        if (error->isEmpty())
            *error = QString::fromLatin1("%1 must be a canonical signed integer string").arg(path);
        return false;
    }
    bool ok = false;
    const qint64 decoded = text.toLongLong(&ok, 10);
    if (!ok || QString::number(decoded) != text) {
        *error = QString::fromLatin1("%1 is not a canonical signed integer string").arg(path);
        return false;
    }
    *result = decoded;
    return true;
}

bool readUnsigned(
    const StrictJson &value, quint64 maximum, const QString &path, quint64 *result, QString *error)
{
    if (value.is_boolean() || !value.is_number_integer()) {
        *error = QString::fromLatin1("%1 must be an unsigned integer").arg(path);
        return false;
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<StrictJson::number_unsigned_t>();
        if (number <= maximum) {
            *result = quint64(number);
            return true;
        }
    } else {
        const auto number = value.get<StrictJson::number_integer_t>();
        if (number >= 0 && quint64(number) <= maximum) {
            *result = quint64(number);
            return true;
        }
    }
    *error = QString::fromLatin1("%1 is outside its permitted range").arg(path);
    return false;
}

bool readI32(const StrictJson &value, const QString &path, qint32 *result, QString *error)
{
    if (value.is_boolean() || !value.is_number_integer()) {
        *error = QString::fromLatin1("%1 must be a signed 32-bit integer").arg(path);
        return false;
    }
    qint64 number = 0;
    if (value.is_number_unsigned()) {
        const auto unsignedNumber = value.get<StrictJson::number_unsigned_t>();
        if (unsignedNumber > quint64(std::numeric_limits<qint32>::max())) {
            *error = QString::fromLatin1("%1 is outside the signed 32-bit range").arg(path);
            return false;
        }
        number = qint64(unsignedNumber);
    } else {
        number = qint64(value.get<StrictJson::number_integer_t>());
    }
    if (number < std::numeric_limits<qint32>::min() || number > std::numeric_limits<qint32>::max()) {
        *error = QString::fromLatin1("%1 is outside the signed 32-bit range").arg(path);
        return false;
    }
    *result = qint32(number);
    return true;
}

StrictJson bytesJson(QByteArrayView value)
{
    const QByteArray encoded = QByteArray(value).toBase64();
    return std::string(encoded.constData(), std::size_t(encoded.size()));
}

bool readBytes(
    const StrictJson &value,
    const QString &path,
    QByteArray *result,
    QString *error,
    qsizetype maximumBytes = maximumBlobBytes)
{
    QString encoded;
    const qsizetype maximumEncoded = maximumBytes
                                             > (std::numeric_limits<qsizetype>::max() - 4) / 4 * 3
                                         ? std::numeric_limits<qsizetype>::max()
                                         : ((maximumBytes + 2) / 3) * 4;
    if (!readString(value, path, &encoded, error, maximumEncoded))
        return false;
    const QByteArray encodedBytes = encoded.toLatin1();
    if (encodedBytes != encoded.toUtf8()) {
        *error = QString::fromLatin1("%1 must contain ASCII Base64").arg(path);
        return false;
    }
    const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
        encodedBytes, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > maximumBytes
        || decoded.decoded.toBase64() != encodedBytes) {
        *error = QString::fromLatin1("%1 must contain canonical padded Base64").arg(path);
        return false;
    }
    *result = decoded.decoded;
    return true;
}

StrictJson dateTimeJson(const QDateTime &value)
{
    if (!value.isValid())
        return nullptr;
    return utf8(value.toUTC().toString(Qt::ISODateWithMs));
}

bool readDateTime(
    const StrictJson &value, const QString &path, bool optional, QDateTime *result, QString *error)
{
    if (value.is_null()) {
        if (!optional) {
            *error = QString::fromLatin1("%1 must be a UTC date-time").arg(path);
            return false;
        }
        *result = {};
        return true;
    }
    QString text;
    if (!readString(value, path, &text, error, 64))
        return false;
    const QDateTime decoded = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!decoded.isValid() || decoded.offsetFromUtc() != 0
        || decoded.toUTC().toString(Qt::ISODateWithMs) != text) {
        *error = QString::fromLatin1("%1 must be a canonical UTC ISO date-time").arg(path);
        return false;
    }
    *result = decoded.toUTC();
    return true;
}

template<typename Value, typename Encoder>
StrictJson optionalJson(const std::optional<Value> &value, Encoder encoder)
{
    return value ? encoder(*value) : StrictJson(nullptr);
}

StrictJson phaseJson(Phase value)
{
    switch (value) {
    case Phase::Idle:
        return "idle";
    case Phase::Queued:
        return "queued";
    case Phase::VerifyingInputs:
        return "verifying-inputs";
    case Phase::CapturingProject:
        return "capturing-project";
    case Phase::ProbingExistingPackage:
        return "probing-existing-package";
    case Phase::AcquiringControl:
        return "acquiring-control";
    case Phase::DeployingPackage:
        return "deploying-package";
    case Phase::VerifyingRuntimeIdentity:
        return "verifying-runtime-identity";
    case Phase::PersistingEvidence:
        return "persisting-evidence";
    case Phase::CommittingProjectBinding:
        return "committing-project-binding";
    case Phase::ReleasingControl:
        return "releasing-control";
    case Phase::Canceling:
        return "canceling";
    case Phase::Reconciling:
        return "reconciling";
    case Phase::AwaitingReconciliation:
        return "awaiting-reconciliation";
    case Phase::Finished:
        return "finished";
    }
    return nullptr;
}

bool readPhase(const StrictJson &value, const QString &path, Phase *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 64))
        return false;
    static constexpr std::array values{
        std::pair{"idle", Phase::Idle},
        std::pair{"queued", Phase::Queued},
        std::pair{"verifying-inputs", Phase::VerifyingInputs},
        std::pair{"capturing-project", Phase::CapturingProject},
        std::pair{"probing-existing-package", Phase::ProbingExistingPackage},
        std::pair{"acquiring-control", Phase::AcquiringControl},
        std::pair{"deploying-package", Phase::DeployingPackage},
        std::pair{"verifying-runtime-identity", Phase::VerifyingRuntimeIdentity},
        std::pair{"persisting-evidence", Phase::PersistingEvidence},
        std::pair{"committing-project-binding", Phase::CommittingProjectBinding},
        std::pair{"releasing-control", Phase::ReleasingControl},
        std::pair{"canceling", Phase::Canceling},
        std::pair{"reconciling", Phase::Reconciling},
        std::pair{"awaiting-reconciliation", Phase::AwaitingReconciliation},
        std::pair{"finished", Phase::Finished},
    };
    for (const auto &[name, candidate] : values) {
        if (text == QLatin1StringView(name)) {
            *result = candidate;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unknown activation phase").arg(path);
    return false;
}

StrictJson outcomeJson(Outcome value)
{
    switch (value) {
    case Outcome::Pending:
        return "pending";
    case Outcome::SucceededWithExistingPackage:
        return "succeeded-existing-package";
    case Outcome::SucceededWithActivatedPackage:
        return "succeeded-activated-package";
    case Outcome::Canceled:
        return "canceled";
    case Outcome::FailedWithoutControllerChange:
        return "failed-without-controller-change";
    case Outcome::FailedAfterRollback:
        return "failed-after-rollback";
    case Outcome::FailedControllerChangedWithoutBinding:
        return "failed-controller-changed-without-binding";
    case Outcome::OutcomeUnknown:
        return "outcome-unknown";
    }
    return nullptr;
}

bool readOutcome(const StrictJson &value, const QString &path, Outcome *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 64))
        return false;
    static constexpr std::array values{
        std::pair{"pending", Outcome::Pending},
        std::pair{"succeeded-existing-package", Outcome::SucceededWithExistingPackage},
        std::pair{"succeeded-activated-package", Outcome::SucceededWithActivatedPackage},
        std::pair{"canceled", Outcome::Canceled},
        std::pair{"failed-without-controller-change", Outcome::FailedWithoutControllerChange},
        std::pair{"failed-after-rollback", Outcome::FailedAfterRollback},
        std::pair{
            "failed-controller-changed-without-binding",
            Outcome::FailedControllerChangedWithoutBinding},
        std::pair{"outcome-unknown", Outcome::OutcomeUnknown},
    };
    for (const auto &[name, candidate] : values) {
        if (text == QLatin1StringView(name)) {
            *result = candidate;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unknown activation outcome").arg(path);
    return false;
}

StrictJson actionJson(Action value)
{
    switch (value) {
    case Action::None:
        return "none";
    case Action::AcquireControl:
        return "acquire-control";
    case Action::ReleaseControl:
        return "release-control";
    case Action::DeployPackage:
        return "deploy-package";
    }
    return nullptr;
}

bool readAction(const StrictJson &value, const QString &path, Action *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 32))
        return false;
    if (text == QStringLiteral("none"))
        *result = Action::None;
    else if (text == QStringLiteral("acquire-control"))
        *result = Action::AcquireControl;
    else if (text == QStringLiteral("release-control"))
        *result = Action::ReleaseControl;
    else if (text == QStringLiteral("deploy-package"))
        *result = Action::DeployPackage;
    else {
        *error = QString::fromLatin1("%1 contains an unknown provider action").arg(path);
        return false;
    }
    return true;
}

StrictJson scopeJson(const Data::ControllerConnectionScope &scope)
{
    return {
        {"masterId", utf8(scope.masterId.toString())},
        {"projectId", utf8(scope.projectId.toString())},
    };
}

bool readNodeId(const StrictJson &value, const QString &path, Data::NodeId *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 36))
        return false;
    const Data::NodeId id = Data::NodeId::fromString(text);
    if (id.isNull() || id.toString() != text) {
        *error = QString::fromLatin1("%1 must be a canonical non-null NodeId").arg(path);
        return false;
    }
    *result = id;
    return true;
}

bool readScope(
    const StrictJson &value,
    const QString &path,
    Data::ControllerConnectionScope *result,
    QString *error)
{
    if (!hasExactFields(value, path, {"masterId", "projectId"}, error))
        return false;
    return readNodeId(value.at("projectId"), fieldPath(path, "projectId"), &result->projectId, error)
           && readNodeId(value.at("masterId"), fieldPath(path, "masterId"), &result->masterId, error);
}

StrictJson proofJson(const Data::RuntimeSemanticMappingProof &proof)
{
    const char *trust = "unknown";
    if (proof.trust == Data::RuntimeSemanticMappingTrust::Production)
        trust = "production";
    else if (proof.trust == Data::RuntimeSemanticMappingTrust::Engineering)
        trust = "engineering";
    return {
        {"bindingCount", proof.bindingCount},
        {"formatVersion", proof.formatVersion},
        {"manifestSha256", bytesJson(proof.manifestSha256)},
        {"mappingSha256", bytesJson(proof.mappingSha256)},
        {"packageSha256", bytesJson(proof.packageSha256)},
        {"packageSigned", proof.packageSigned},
        {"resourceRecordsSha256", bytesJson(proof.resourceRecordsSha256)},
        {"resourceSectionSha256", bytesJson(proof.resourceSectionSha256)},
        {"semanticBindingVerified", proof.semanticBindingVerified},
        {"signatureVerified", proof.signatureVerified},
        {"signingKeyIdSha256", bytesJson(proof.signingKeyIdSha256)},
        {"topologySha256", bytesJson(proof.topologySha256)},
        {"trust", trust},
    };
}

bool readProof(
    const StrictJson &value,
    const QString &path,
    Data::RuntimeSemanticMappingProof *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"bindingCount",
             "formatVersion",
             "manifestSha256",
             "mappingSha256",
             "packageSha256",
             "packageSigned",
             "resourceRecordsSha256",
             "resourceSectionSha256",
             "semanticBindingVerified",
             "signatureVerified",
             "signingKeyIdSha256",
             "topologySha256",
             "trust"},
            error)) {
        return false;
    }
    quint64 formatVersion = 0;
    quint64 bindingCount = 0;
    if (!readUnsigned(
            value.at("formatVersion"),
            std::numeric_limits<quint16>::max(),
            fieldPath(path, "formatVersion"),
            &formatVersion,
            error)
        || !readUnsigned(
            value.at("bindingCount"),
            std::numeric_limits<quint32>::max(),
            fieldPath(path, "bindingCount"),
            &bindingCount,
            error)) {
        return false;
    }
    for (std::string_view field :
         {"packageSigned", "signatureVerified", "semanticBindingVerified"}) {
        if (!value.at(field).is_boolean()) {
            *error = QString::fromLatin1("%1 must be a boolean").arg(fieldPath(path, field));
            return false;
        }
    }
    QString trust;
    if (!readString(value.at("trust"), fieldPath(path, "trust"), &trust, error, 16))
        return false;
    Data::RuntimeSemanticMappingTrust trustValue = Data::RuntimeSemanticMappingTrust::Unknown;
    if (trust == QStringLiteral("production"))
        trustValue = Data::RuntimeSemanticMappingTrust::Production;
    else if (trust == QStringLiteral("engineering"))
        trustValue = Data::RuntimeSemanticMappingTrust::Engineering;
    else if (trust != QStringLiteral("unknown")) {
        *error = QString::fromLatin1("%1 contains an unknown trust value")
                     .arg(fieldPath(path, "trust"));
        return false;
    }

    Data::RuntimeSemanticMappingProof proof;
    proof.formatVersion = quint16(formatVersion);
    proof.bindingCount = quint32(bindingCount);
    proof.packageSigned = value.at("packageSigned").get<bool>();
    proof.signatureVerified = value.at("signatureVerified").get<bool>();
    proof.semanticBindingVerified = value.at("semanticBindingVerified").get<bool>();
    proof.trust = trustValue;
    if (!readBytes(
            value.at("packageSha256"),
            fieldPath(path, "packageSha256"),
            &proof.packageSha256,
            error,
            32)
        || !readBytes(
            value.at("manifestSha256"),
            fieldPath(path, "manifestSha256"),
            &proof.manifestSha256,
            error,
            32)
        || !readBytes(
            value.at("mappingSha256"),
            fieldPath(path, "mappingSha256"),
            &proof.mappingSha256,
            error,
            32)
        || !readBytes(
            value.at("resourceRecordsSha256"),
            fieldPath(path, "resourceRecordsSha256"),
            &proof.resourceRecordsSha256,
            error,
            32)
        || !readBytes(
            value.at("resourceSectionSha256"),
            fieldPath(path, "resourceSectionSha256"),
            &proof.resourceSectionSha256,
            error,
            32)
        || !readBytes(
            value.at("topologySha256"),
            fieldPath(path, "topologySha256"),
            &proof.topologySha256,
            error,
            32)
        || !readBytes(
            value.at("signingKeyIdSha256"),
            fieldPath(path, "signingKeyIdSha256"),
            &proof.signingKeyIdSha256,
            error,
            32)) {
        return false;
    }
    if (!proof.isValid()) {
        *error = QString::fromLatin1("%1 is not a valid semantic mapping proof").arg(path);
        return false;
    }
    *result = std::move(proof);
    return true;
}

StrictJson slotJson(Data::ControllerSlot slot)
{
    switch (slot) {
    case Data::ControllerSlot::None:
        return "none";
    case Data::ControllerSlot::A:
        return "a";
    case Data::ControllerSlot::B:
        return "b";
    }
    return nullptr;
}

bool readSlot(
    const StrictJson &value, const QString &path, Data::ControllerSlot *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 8))
        return false;
    if (text == QStringLiteral("none"))
        *result = Data::ControllerSlot::None;
    else if (text == QStringLiteral("a"))
        *result = Data::ControllerSlot::A;
    else if (text == QStringLiteral("b"))
        *result = Data::ControllerSlot::B;
    else {
        *error = QString::fromLatin1("%1 contains an unknown controller slot").arg(path);
        return false;
    }
    return true;
}

StrictJson selectorJson(const Data::ControllerPackageSelector &selector)
{
    return {
        {"configurationId", u64Json(selector.configurationId)},
        {"generation", u64Json(selector.generation)},
        {"slot", slotJson(selector.slot)},
    };
}

bool readSelector(
    const StrictJson &value,
    const QString &path,
    Data::ControllerPackageSelector *result,
    QString *error)
{
    if (!hasExactFields(value, path, {"configurationId", "generation", "slot"}, error)
        || !readSlot(value.at("slot"), fieldPath(path, "slot"), &result->slot, error)
        || !readU64(value.at("generation"), fieldPath(path, "generation"), &result->generation, error)
        || !readU64(
            value.at("configurationId"),
            fieldPath(path, "configurationId"),
            &result->configurationId,
            error)) {
        return false;
    }
    if (!result->isValid()) {
        *error = QString::fromLatin1("%1 is not a valid package selector").arg(path);
        return false;
    }
    return true;
}

StrictJson epochJson(const Data::RuntimeResourceCatalogEpoch &epoch)
{
    return {
        {"activePackageGeneration", u64Json(epoch.activePackageGeneration)},
        {"activePackageSlot", slotJson(epoch.activePackageSlot)},
        {"catalogRevision", u64Json(epoch.catalogRevision)},
        {"configurationId", u64Json(epoch.configurationId)},
        {"controllerBootId", u64Json(epoch.controllerBootId)},
        {"runtimeGeneration", u64Json(epoch.runtimeGeneration)},
        {"topologyGeneration", u64Json(epoch.topologyGeneration)},
        {"topologyIdentity", bytesJson(epoch.topologyIdentity)},
    };
}

bool readEpoch(
    const StrictJson &value,
    const QString &path,
    Data::RuntimeResourceCatalogEpoch *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"activePackageGeneration",
             "activePackageSlot",
             "catalogRevision",
             "configurationId",
             "controllerBootId",
             "runtimeGeneration",
             "topologyGeneration",
             "topologyIdentity"},
            error)
        || !readSlot(
            value.at("activePackageSlot"),
            fieldPath(path, "activePackageSlot"),
            &result->activePackageSlot,
            error)
        || !readU64(
            value.at("controllerBootId"),
            fieldPath(path, "controllerBootId"),
            &result->controllerBootId,
            error)
        || !readU64(
            value.at("activePackageGeneration"),
            fieldPath(path, "activePackageGeneration"),
            &result->activePackageGeneration,
            error)
        || !readU64(
            value.at("configurationId"),
            fieldPath(path, "configurationId"),
            &result->configurationId,
            error)
        || !readU64(
            value.at("topologyGeneration"),
            fieldPath(path, "topologyGeneration"),
            &result->topologyGeneration,
            error)
        || !readU64(
            value.at("runtimeGeneration"),
            fieldPath(path, "runtimeGeneration"),
            &result->runtimeGeneration,
            error)
        || !readU64(
            value.at("catalogRevision"),
            fieldPath(path, "catalogRevision"),
            &result->catalogRevision,
            error)
        || !readBytes(
            value.at("topologyIdentity"),
            fieldPath(path, "topologyIdentity"),
            &result->topologyIdentity,
            error,
            256)) {
        return false;
    }
    if (!Data::isCompleteRuntimeSemanticMappingEpoch(*result)) {
        *error = QString::fromLatin1("%1 is not a complete runtime epoch").arg(path);
        return false;
    }
    return true;
}

StrictJson attestationJson(const Data::RuntimeSemanticMappingAttestation &attestation)
{
    return {
        {"epoch", epochJson(attestation.epoch)},
        {"proof", proofJson(attestation.proof)},
        {"receivedAt", dateTimeJson(attestation.receivedAt)},
        {"scope", scopeJson(attestation.scope)},
        {"sessionGeneration", u64Json(attestation.sessionGeneration)},
    };
}

bool readAttestation(
    const StrictJson &value,
    const QString &path,
    Data::RuntimeSemanticMappingAttestation *result,
    QString *error)
{
    if (!hasExactFields(
            value, path, {"epoch", "proof", "receivedAt", "scope", "sessionGeneration"}, error)
        || !readScope(value.at("scope"), fieldPath(path, "scope"), &result->scope, error)
        || !readU64(
            value.at("sessionGeneration"),
            fieldPath(path, "sessionGeneration"),
            &result->sessionGeneration,
            error)
        || !readEpoch(value.at("epoch"), fieldPath(path, "epoch"), &result->epoch, error)
        || !readProof(value.at("proof"), fieldPath(path, "proof"), &result->proof, error)
        || !readDateTime(
            value.at("receivedAt"),
            fieldPath(path, "receivedAt"),
            false,
            &result->receivedAt,
            error)) {
        return false;
    }
    if (!result->isValid()) {
        *error = QString::fromLatin1("%1 is not a valid runtime attestation").arg(path);
        return false;
    }
    return true;
}

StrictJson packageStateJson(Data::ControllerPackageState value)
{
    switch (value) {
    case Data::ControllerPackageState::Unavailable:
        return "unavailable";
    case Data::ControllerPackageState::Empty:
        return "empty";
    case Data::ControllerPackageState::Staged:
        return "staged";
    case Data::ControllerPackageState::Accepted:
        return "accepted";
    case Data::ControllerPackageState::Active:
        return "active";
    case Data::ControllerPackageState::Rejected:
        return "rejected";
    }
    return nullptr;
}

bool readPackageState(
    const StrictJson &value,
    const QString &path,
    Data::ControllerPackageState *result,
    QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 16))
        return false;
    if (text == QStringLiteral("unavailable"))
        *result = Data::ControllerPackageState::Unavailable;
    else if (text == QStringLiteral("empty"))
        *result = Data::ControllerPackageState::Empty;
    else if (text == QStringLiteral("staged"))
        *result = Data::ControllerPackageState::Staged;
    else if (text == QStringLiteral("accepted"))
        *result = Data::ControllerPackageState::Accepted;
    else if (text == QStringLiteral("active"))
        *result = Data::ControllerPackageState::Active;
    else if (text == QStringLiteral("rejected"))
        *result = Data::ControllerPackageState::Rejected;
    else {
        *error = QString::fromLatin1("%1 contains an unknown package state").arg(path);
        return false;
    }
    return true;
}

StrictJson serviceStateJson(Data::ControllerServiceState value)
{
    switch (value) {
    case Data::ControllerServiceState::Unknown:
        return "unknown";
    case Data::ControllerServiceState::Boot:
        return "boot";
    case Data::ControllerServiceState::Configuring:
        return "configuring";
    case Data::ControllerServiceState::SafeOperational:
        return "safe-operational";
    case Data::ControllerServiceState::OperationalSafe:
        return "operational-safe";
    case Data::ControllerServiceState::Running:
        return "running";
    case Data::ControllerServiceState::Stopping:
        return "stopping";
    case Data::ControllerServiceState::Fault:
        return "fault";
    case Data::ControllerServiceState::Recovering:
        return "recovering";
    case Data::ControllerServiceState::Shutdown:
        return "shutdown";
    case Data::ControllerServiceState::Paused:
        return "paused";
    }
    return nullptr;
}

bool readServiceState(
    const StrictJson &value,
    const QString &path,
    Data::ControllerServiceState *result,
    QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 32))
        return false;
    static constexpr std::array values{
        std::pair{"unknown", Data::ControllerServiceState::Unknown},
        std::pair{"boot", Data::ControllerServiceState::Boot},
        std::pair{"configuring", Data::ControllerServiceState::Configuring},
        std::pair{"safe-operational", Data::ControllerServiceState::SafeOperational},
        std::pair{"operational-safe", Data::ControllerServiceState::OperationalSafe},
        std::pair{"running", Data::ControllerServiceState::Running},
        std::pair{"stopping", Data::ControllerServiceState::Stopping},
        std::pair{"fault", Data::ControllerServiceState::Fault},
        std::pair{"recovering", Data::ControllerServiceState::Recovering},
        std::pair{"shutdown", Data::ControllerServiceState::Shutdown},
        std::pair{"paused", Data::ControllerServiceState::Paused},
    };
    for (const auto &[name, candidate] : values) {
        if (text == QLatin1StringView(name)) {
            *result = candidate;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unknown service state").arg(path);
    return false;
}

StrictJson controllerEvidenceJson(const Data::RuntimePackageActivationControllerEvidence &evidence)
{
    return {
        {"bootId", u64Json(evidence.bootId())},
        {"controlLeaseOwnerSessionId", u64Json(evidence.controlLeaseOwnerSessionId())},
        {"evidenceSha256", bytesJson(evidence.evidenceSha256().value())},
        {"mappingAttestation",
         optionalJson(
             evidence.mappingAttestation(),
             [](const auto &attestation) { return attestationJson(attestation); })},
        {"observationSessionId", u64Json(evidence.observationSessionId())},
        {"observedAt", dateTimeJson(evidence.observedAt())},
        {"persistentActive",
         optionalJson(
             evidence.persistentActive(),
             [](const auto &selector) { return selectorJson(selector); })},
        {"runtimePackageState", packageStateJson(evidence.runtimePackageState())},
        {"scope", scopeJson(evidence.scope())},
        {"serviceState", serviceStateJson(evidence.serviceState())},
        {"sessionGeneration", u64Json(evidence.sessionGeneration())},
    };
}

bool readControllerEvidence(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationControllerEvidence *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"bootId",
             "controlLeaseOwnerSessionId",
             "evidenceSha256",
             "mappingAttestation",
             "observationSessionId",
             "observedAt",
             "persistentActive",
             "runtimePackageState",
             "scope",
             "serviceState",
             "sessionGeneration"},
            error)) {
        return false;
    }

    Data::ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    quint64 observationSessionId = 0;
    quint64 leaseOwnerSessionId = 0;
    quint64 bootId = 0;
    std::optional<Data::ControllerPackageSelector> persistentActive;
    Data::ControllerPackageState packageState = Data::ControllerPackageState::Unavailable;
    Data::ControllerServiceState serviceState = Data::ControllerServiceState::Unknown;
    std::optional<Data::RuntimeSemanticMappingAttestation> attestation;
    QDateTime observedAt;
    QByteArray serializedDigest;

    if (!readScope(value.at("scope"), fieldPath(path, "scope"), &scope, error)
        || !readU64(
            value.at("sessionGeneration"),
            fieldPath(path, "sessionGeneration"),
            &sessionGeneration,
            error)
        || !readU64(
            value.at("observationSessionId"),
            fieldPath(path, "observationSessionId"),
            &observationSessionId,
            error)
        || !readU64(
            value.at("controlLeaseOwnerSessionId"),
            fieldPath(path, "controlLeaseOwnerSessionId"),
            &leaseOwnerSessionId,
            error)
        || !readU64(value.at("bootId"), fieldPath(path, "bootId"), &bootId, error)
        || !readPackageState(
            value.at("runtimePackageState"),
            fieldPath(path, "runtimePackageState"),
            &packageState,
            error)
        || !readServiceState(
            value.at("serviceState"), fieldPath(path, "serviceState"), &serviceState, error)
        || !readDateTime(
            value.at("observedAt"), fieldPath(path, "observedAt"), false, &observedAt, error)
        || !readBytes(
            value.at("evidenceSha256"),
            fieldPath(path, "evidenceSha256"),
            &serializedDigest,
            error,
            32)) {
        return false;
    }

    if (!value.at("persistentActive").is_null()) {
        Data::ControllerPackageSelector selector;
        if (!readSelector(
                value.at("persistentActive"),
                fieldPath(path, "persistentActive"),
                &selector,
                error)) {
            return false;
        }
        persistentActive = selector;
    }
    if (!value.at("mappingAttestation").is_null()) {
        Data::RuntimeSemanticMappingAttestation parsed;
        if (!readAttestation(
                value.at("mappingAttestation"),
                fieldPath(path, "mappingAttestation"),
                &parsed,
                error)) {
            return false;
        }
        attestation = parsed;
    }

    Data::RuntimePackageActivationControllerEvidence evidence{
        scope,
        sessionGeneration,
        observationSessionId,
        leaseOwnerSessionId,
        bootId,
        persistentActive,
        packageState,
        serviceState,
        attestation,
        observedAt,
    };
    if (!evidence.isValid() || evidence.evidenceSha256().value() != serializedDigest) {
        *error = QString::fromLatin1("%1 failed controller-evidence integrity validation").arg(path);
        return false;
    }
    *result = std::move(evidence);
    return true;
}

StrictJson identityJson(const Data::RuntimePackageActivationIdentity &identity)
{
    return {
        {"bindingArtifactId", utf8(identity.bindingArtifactId())},
        {"compiledProjectSha256", bytesJson(identity.compiledProjectSha256().value())},
        {"configurationId", u64Json(identity.configurationId())},
        {"documentRevisionToken", bytesJson(identity.documentRevisionToken().value())},
        {"effectiveProjectCompanionSha256",
         bytesJson(identity.effectiveProjectCompanionSha256().value())},
        {"expectedCatalogRevision", u64Json(identity.expectedCatalogRevision())},
        {"expectedMappingProof", proofJson(identity.expectedMappingProof())},
        {"expectedTopologyIdentity", bytesJson(identity.expectedTopologyIdentity())},
        {"operationId", utf8(identity.operationId().value())},
        {"originalBindingToken", bytesJson(identity.originalBindingToken().value())},
        {"packageSha256", bytesJson(identity.packageSha256().value())},
        {"scope", scopeJson(identity.scope())},
        {"targetBindingToken", bytesJson(identity.targetBindingToken().value())},
    };
}

bool readIdentity(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationIdentity *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"bindingArtifactId",
             "compiledProjectSha256",
             "configurationId",
             "documentRevisionToken",
             "effectiveProjectCompanionSha256",
             "expectedCatalogRevision",
             "expectedMappingProof",
             "expectedTopologyIdentity",
             "operationId",
             "originalBindingToken",
             "packageSha256",
             "scope",
             "targetBindingToken"},
            error)) {
        return false;
    }

    QString operationId;
    QString artifactId;
    Data::ControllerConnectionScope scope;
    QByteArray documentRevision;
    QByteArray originalBinding;
    QByteArray targetBinding;
    quint64 configurationId = 0;
    quint64 catalogRevision = 0;
    QByteArray topologyIdentity;
    QByteArray packageSha;
    QByteArray projectSha;
    QByteArray companionSha;
    Data::RuntimeSemanticMappingProof proof;
    if (!readString(value.at("operationId"), fieldPath(path, "operationId"), &operationId, error, 128)
        || !readScope(value.at("scope"), fieldPath(path, "scope"), &scope, error)
        || !readBytes(
            value.at("documentRevisionToken"),
            fieldPath(path, "documentRevisionToken"),
            &documentRevision,
            error,
            32)
        || !readBytes(
            value.at("originalBindingToken"),
            fieldPath(path, "originalBindingToken"),
            &originalBinding,
            error,
            32)
        || !readBytes(
            value.at("targetBindingToken"),
            fieldPath(path, "targetBindingToken"),
            &targetBinding,
            error,
            32)
        || !readString(
            value.at("bindingArtifactId"),
            fieldPath(path, "bindingArtifactId"),
            &artifactId,
            error,
            256)
        || !readU64(
            value.at("configurationId"), fieldPath(path, "configurationId"), &configurationId, error)
        || !readU64(
            value.at("expectedCatalogRevision"),
            fieldPath(path, "expectedCatalogRevision"),
            &catalogRevision,
            error)
        || !readBytes(
            value.at("expectedTopologyIdentity"),
            fieldPath(path, "expectedTopologyIdentity"),
            &topologyIdentity,
            error,
            256)
        || !readBytes(
            value.at("packageSha256"), fieldPath(path, "packageSha256"), &packageSha, error, 32)
        || !readBytes(
            value.at("compiledProjectSha256"),
            fieldPath(path, "compiledProjectSha256"),
            &projectSha,
            error,
            32)
        || !readBytes(
            value.at("effectiveProjectCompanionSha256"),
            fieldPath(path, "effectiveProjectCompanionSha256"),
            &companionSha,
            error,
            32)
        || !readProof(
            value.at("expectedMappingProof"),
            fieldPath(path, "expectedMappingProof"),
            &proof,
            error)) {
        return false;
    }

    Data::RuntimePackageActivationIdentity identity{
        Data::RuntimePackageActivationOperationId{operationId},
        scope,
        Data::RuntimePackageActivationDocumentRevisionToken{documentRevision},
        Data::RuntimePackageActivationOriginalBindingToken{originalBinding},
        Data::RuntimePackageActivationOriginalBindingToken{targetBinding},
        artifactId,
        configurationId,
        catalogRevision,
        topologyIdentity,
        Data::RuntimePackageActivationSha256{packageSha},
        Data::RuntimePackageActivationSha256{projectSha},
        Data::RuntimePackageActivationSha256{companionSha},
        proof,
    };
    if (!identity.isValid()) {
        *error = QString::fromLatin1("%1 is not a valid activation identity").arg(path);
        return false;
    }
    *result = std::move(identity);
    return true;
}

StrictJson requestJson(const Data::RuntimePackageActivationRequest &request)
{
    return {
        {"compiledProjectSource", bytesJson(request.compiledProjectSource())},
        {"effectiveProjectCompanion", bytesJson(request.effectiveProjectCompanion())},
        {"fingerprint", bytesJson(request.fingerprint().value())},
        {"identity", identityJson(request.identity())},
        {"packageBytes", bytesJson(request.packageBytes())},
        {"rollbackOnActivationFailure", request.rollbackOnActivationFailure()},
    };
}

bool readRequest(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationRequest *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"compiledProjectSource",
             "effectiveProjectCompanion",
             "fingerprint",
             "identity",
             "packageBytes",
             "rollbackOnActivationFailure"},
            error)) {
        return false;
    }
    if (!value.at("rollbackOnActivationFailure").is_boolean()) {
        *error = QString::fromLatin1("%1 must be a boolean")
                     .arg(fieldPath(path, "rollbackOnActivationFailure"));
        return false;
    }

    Data::RuntimePackageActivationIdentity identity;
    QByteArray packageBytes;
    QByteArray projectBytes;
    QByteArray companionBytes;
    QByteArray fingerprint;
    if (!readIdentity(value.at("identity"), fieldPath(path, "identity"), &identity, error)
        || !readBytes(value.at("packageBytes"), fieldPath(path, "packageBytes"), &packageBytes, error)
        || !readBytes(
            value.at("compiledProjectSource"),
            fieldPath(path, "compiledProjectSource"),
            &projectBytes,
            error)
        || !readBytes(
            value.at("effectiveProjectCompanion"),
            fieldPath(path, "effectiveProjectCompanion"),
            &companionBytes,
            error)
        || !readBytes(
            value.at("fingerprint"), fieldPath(path, "fingerprint"), &fingerprint, error, 32)) {
        return false;
    }
    Data::RuntimePackageActivationRequest request{
        identity,
        packageBytes,
        projectBytes,
        companionBytes,
        value.at("rollbackOnActivationFailure").get<bool>(),
    };
    if (!request.isValid() || request.fingerprint().value() != fingerprint) {
        *error = QString::fromLatin1("%1 failed activation-request integrity validation").arg(path);
        return false;
    }
    *result = std::move(request);
    return true;
}

StrictJson commitDispositionJson(Data::RuntimePackageActivationProjectCommitDisposition value)
{
    switch (value) {
    case Data::RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted:
        return "compare-and-set-committed";
    case Data::RuntimePackageActivationProjectCommitDisposition::AlreadyExact:
        return "already-exact";
    }
    return nullptr;
}

bool readCommitDisposition(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationProjectCommitDisposition *result,
    QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 32))
        return false;
    if (text == QStringLiteral("compare-and-set-committed"))
        *result = Data::RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted;
    else if (text == QStringLiteral("already-exact"))
        *result = Data::RuntimePackageActivationProjectCommitDisposition::AlreadyExact;
    else {
        *error = QString::fromLatin1("%1 contains an unknown commit disposition").arg(path);
        return false;
    }
    return true;
}

StrictJson projectCommitJson(const Data::RuntimePackageActivationProjectCommit &commit)
{
    return {
        {"committedAt", dateTimeJson(commit.committedAt())},
        {"disposition", commitDispositionJson(commit.disposition())},
        {"evidenceSha256", bytesJson(commit.evidenceSha256().value())},
        {"originalBinding", bytesJson(commit.originalBinding().value())},
        {"originalDocumentRevision", bytesJson(commit.originalDocumentRevision().value())},
        {"resultingBinding", bytesJson(commit.resultingBinding().value())},
        {"resultingDocumentRevision", bytesJson(commit.resultingDocumentRevision().value())},
    };
}

bool readProjectCommit(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationProjectCommit *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"committedAt",
             "disposition",
             "evidenceSha256",
             "originalBinding",
             "originalDocumentRevision",
             "resultingBinding",
             "resultingDocumentRevision"},
            error)) {
        return false;
    }
    QByteArray originalRevision;
    QByteArray originalBinding;
    QByteArray resultingRevision;
    QByteArray resultingBinding;
    QByteArray evidenceSha;
    Data::RuntimePackageActivationProjectCommitDisposition disposition
        = Data::RuntimePackageActivationProjectCommitDisposition::CompareAndSetCommitted;
    QDateTime committedAt;
    if (!readBytes(
            value.at("originalDocumentRevision"),
            fieldPath(path, "originalDocumentRevision"),
            &originalRevision,
            error,
            32)
        || !readBytes(
            value.at("originalBinding"),
            fieldPath(path, "originalBinding"),
            &originalBinding,
            error,
            32)
        || !readBytes(
            value.at("resultingDocumentRevision"),
            fieldPath(path, "resultingDocumentRevision"),
            &resultingRevision,
            error,
            32)
        || !readBytes(
            value.at("resultingBinding"),
            fieldPath(path, "resultingBinding"),
            &resultingBinding,
            error,
            32)
        || !readCommitDisposition(
            value.at("disposition"), fieldPath(path, "disposition"), &disposition, error)
        || !readDateTime(
            value.at("committedAt"), fieldPath(path, "committedAt"), false, &committedAt, error)
        || !readBytes(
            value.at("evidenceSha256"), fieldPath(path, "evidenceSha256"), &evidenceSha, error, 32)) {
        return false;
    }
    Data::RuntimePackageActivationProjectCommit commit{
        Data::RuntimePackageActivationDocumentRevisionToken{originalRevision},
        Data::RuntimePackageActivationOriginalBindingToken{originalBinding},
        Data::RuntimePackageActivationDocumentRevisionToken{resultingRevision},
        Data::RuntimePackageActivationOriginalBindingToken{resultingBinding},
        disposition,
        committedAt,
    };
    if (!commit.isValid() || commit.evidenceSha256().value() != evidenceSha) {
        *error = QString::fromLatin1("%1 failed project-commit integrity validation").arg(path);
        return false;
    }
    *result = std::move(commit);
    return true;
}

StrictJson cancellationResultJson(Data::RuntimePackageActivationCancellationControllerResult value)
{
    switch (value) {
    case Data::RuntimePackageActivationCancellationControllerResult::Pending:
        return "pending";
    case Data::RuntimePackageActivationCancellationControllerResult::NoControllerMutation:
        return "no-controller-mutation";
    case Data::RuntimePackageActivationCancellationControllerResult::ControllerUnchanged:
        return "controller-unchanged";
    case Data::RuntimePackageActivationCancellationControllerResult::RolledBack:
        return "rolled-back";
    case Data::RuntimePackageActivationCancellationControllerResult::OutcomeUnknown:
        return "outcome-unknown";
    }
    return nullptr;
}

bool readCancellationResult(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationCancellationControllerResult *result,
    QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 32))
        return false;
    if (text == QStringLiteral("pending"))
        *result = Data::RuntimePackageActivationCancellationControllerResult::Pending;
    else if (text == QStringLiteral("no-controller-mutation"))
        *result = Data::RuntimePackageActivationCancellationControllerResult::NoControllerMutation;
    else if (text == QStringLiteral("controller-unchanged"))
        *result = Data::RuntimePackageActivationCancellationControllerResult::ControllerUnchanged;
    else if (text == QStringLiteral("rolled-back"))
        *result = Data::RuntimePackageActivationCancellationControllerResult::RolledBack;
    else if (text == QStringLiteral("outcome-unknown"))
        *result = Data::RuntimePackageActivationCancellationControllerResult::OutcomeUnknown;
    else {
        *error = QString::fromLatin1("%1 contains an unknown cancellation result").arg(path);
        return false;
    }
    return true;
}

StrictJson cancellationJson(const Data::RuntimePackageActivationCancellation &cancellation)
{
    return {
        {"controllerResult", cancellationResultJson(cancellation.controllerResult())},
        {"evidenceSha256",
         optionalJson(
             cancellation.evidenceSha256(),
             [](const auto &digest) { return bytesJson(digest.value()); })},
        {"expectedBinding", bytesJson(cancellation.expectedBinding().value())},
        {"expectedDocumentRevision", bytesJson(cancellation.expectedDocumentRevision().value())},
        {"expectedRecordRevision", u64Json(cancellation.expectedRecordRevision())},
        {"requestFingerprint", bytesJson(cancellation.requestFingerprint().value())},
        {"requestedAt", dateTimeJson(cancellation.requestedAt())},
        {"requestedPhase", phaseJson(cancellation.requestedPhase())},
    };
}

bool readCancellation(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationCancellation *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"controllerResult",
             "evidenceSha256",
             "expectedBinding",
             "expectedDocumentRevision",
             "expectedRecordRevision",
             "requestFingerprint",
             "requestedAt",
             "requestedPhase"},
            error)) {
        return false;
    }
    quint64 revision = 0;
    QByteArray documentRevision;
    QByteArray binding;
    QByteArray fingerprint;
    Phase phase = Phase::Idle;
    Data::RuntimePackageActivationCancellationControllerResult controllerResult
        = Data::RuntimePackageActivationCancellationControllerResult::Pending;
    std::optional<Data::RuntimePackageActivationSha256> evidenceSha;
    QDateTime requestedAt;
    if (!readU64(
            value.at("expectedRecordRevision"),
            fieldPath(path, "expectedRecordRevision"),
            &revision,
            error)
        || !readBytes(
            value.at("expectedDocumentRevision"),
            fieldPath(path, "expectedDocumentRevision"),
            &documentRevision,
            error,
            32)
        || !readBytes(
            value.at("expectedBinding"), fieldPath(path, "expectedBinding"), &binding, error, 32)
        || !readBytes(
            value.at("requestFingerprint"),
            fieldPath(path, "requestFingerprint"),
            &fingerprint,
            error,
            32)
        || !readPhase(value.at("requestedPhase"), fieldPath(path, "requestedPhase"), &phase, error)
        || !readCancellationResult(
            value.at("controllerResult"),
            fieldPath(path, "controllerResult"),
            &controllerResult,
            error)
        || !readDateTime(
            value.at("requestedAt"), fieldPath(path, "requestedAt"), false, &requestedAt, error)) {
        return false;
    }
    if (!value.at("evidenceSha256").is_null()) {
        QByteArray digest;
        if (!readBytes(
                value.at("evidenceSha256"), fieldPath(path, "evidenceSha256"), &digest, error, 32)) {
            return false;
        }
        evidenceSha = Data::RuntimePackageActivationSha256{digest};
    }
    Data::RuntimePackageActivationCancellation cancellation{
        revision,
        Data::RuntimePackageActivationDocumentRevisionToken{documentRevision},
        Data::RuntimePackageActivationOriginalBindingToken{binding},
        Data::RuntimePackageActivationSha256{fingerprint},
        phase,
        controllerResult,
        evidenceSha,
        requestedAt,
    };
    if (!cancellation.isValid()) {
        *error = QString::fromLatin1("%1 is not a valid activation cancellation").arg(path);
        return false;
    }
    *result = std::move(cancellation);
    return true;
}

StrictJson controllerOperationJson(Data::ControllerOperation value)
{
    using Operation = Data::ControllerOperation;
    switch (value) {
    case Operation::None:
        return "none";
    case Operation::Connect:
        return "connect";
    case Operation::Handshake:
        return "handshake";
    case Operation::QueryState:
        return "query-state";
    case Operation::QueryCapability:
        return "query-capability";
    case Operation::QueryPackageState:
        return "query-package-state";
    case Operation::QueryFirmwareState:
        return "query-firmware-state";
    case Operation::QueryRuntimeResourceCatalog:
        return "query-runtime-resource-catalog";
    case Operation::QueryRuntimeResourceSnapshot:
        return "query-runtime-resource-snapshot";
    case Operation::QueryAxisParameterEvidence:
        return "query-axis-parameter-evidence";
    case Operation::QueryRuntimeSemanticMappingAttestation:
        return "query-runtime-semantic-mapping-attestation";
    case Operation::QueryRuntimeOutputGroupPolicy:
        return "query-runtime-output-group-policy";
    case Operation::QueryRuntimeOutputTransactionState:
        return "query-runtime-output-transaction-state";
    case Operation::ApplyRuntimeOutputTransaction:
        return "apply-runtime-output-transaction";
    case Operation::SubscribeEvents:
        return "subscribe-events";
    case Operation::Refresh:
        return "refresh";
    case Operation::Disconnect:
        return "disconnect";
    case Operation::AcquireControl:
        return "acquire-control";
    case Operation::ReleaseControl:
        return "release-control";
    case Operation::Heartbeat:
        return "heartbeat";
    case Operation::EnterConfigurationMode:
        return "enter-configuration-mode";
    case Operation::DiscoverTopology:
        return "discover-topology";
    case Operation::RestoreActivePackage:
        return "restore-active-package";
    case Operation::Start:
        return "start";
    case Operation::StartFreeRun:
        return "start-free-run";
    case Operation::StartDistributedClocks:
        return "start-distributed-clocks";
    case Operation::Pause:
        return "pause";
    case Operation::Resume:
        return "resume";
    case Operation::ControlledStop:
        return "controlled-stop";
    case Operation::ResetFault:
        return "reset-fault";
    case Operation::UploadPackage:
        return "upload-package";
    case Operation::AbortPackageUpload:
        return "abort-package-upload";
    case Operation::ValidatePackage:
        return "validate-package";
    case Operation::ActivatePackage:
        return "activate-package";
    case Operation::RollbackPackage:
        return "rollback-package";
    }
    return nullptr;
}

bool readControllerOperation(
    const StrictJson &value, const QString &path, Data::ControllerOperation *result, QString *error)
{
    using Operation = Data::ControllerOperation;
    QString text;
    if (!readString(value, path, &text, error, 64))
        return false;
    static constexpr std::array values{
        std::pair{"none", Operation::None},
        std::pair{"connect", Operation::Connect},
        std::pair{"handshake", Operation::Handshake},
        std::pair{"query-state", Operation::QueryState},
        std::pair{"query-capability", Operation::QueryCapability},
        std::pair{"query-package-state", Operation::QueryPackageState},
        std::pair{"query-firmware-state", Operation::QueryFirmwareState},
        std::pair{"query-runtime-resource-catalog", Operation::QueryRuntimeResourceCatalog},
        std::pair{"query-runtime-resource-snapshot", Operation::QueryRuntimeResourceSnapshot},
        std::pair{"query-axis-parameter-evidence", Operation::QueryAxisParameterEvidence},
        std::pair{
            "query-runtime-semantic-mapping-attestation",
            Operation::QueryRuntimeSemanticMappingAttestation},
        std::pair{"query-runtime-output-group-policy", Operation::QueryRuntimeOutputGroupPolicy},
        std::pair{
            "query-runtime-output-transaction-state", Operation::QueryRuntimeOutputTransactionState},
        std::pair{"apply-runtime-output-transaction", Operation::ApplyRuntimeOutputTransaction},
        std::pair{"subscribe-events", Operation::SubscribeEvents},
        std::pair{"refresh", Operation::Refresh},
        std::pair{"disconnect", Operation::Disconnect},
        std::pair{"acquire-control", Operation::AcquireControl},
        std::pair{"release-control", Operation::ReleaseControl},
        std::pair{"heartbeat", Operation::Heartbeat},
        std::pair{"enter-configuration-mode", Operation::EnterConfigurationMode},
        std::pair{"discover-topology", Operation::DiscoverTopology},
        std::pair{"restore-active-package", Operation::RestoreActivePackage},
        std::pair{"start", Operation::Start},
        std::pair{"start-free-run", Operation::StartFreeRun},
        std::pair{"start-distributed-clocks", Operation::StartDistributedClocks},
        std::pair{"pause", Operation::Pause},
        std::pair{"resume", Operation::Resume},
        std::pair{"controlled-stop", Operation::ControlledStop},
        std::pair{"reset-fault", Operation::ResetFault},
        std::pair{"upload-package", Operation::UploadPackage},
        std::pair{"abort-package-upload", Operation::AbortPackageUpload},
        std::pair{"validate-package", Operation::ValidatePackage},
        std::pair{"activate-package", Operation::ActivatePackage},
        std::pair{"rollback-package", Operation::RollbackPackage},
    };
    for (const auto &[name, candidate] : values) {
        if (text == QLatin1StringView(name)) {
            *result = candidate;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unknown controller operation").arg(path);
    return false;
}

StrictJson deploymentStateJson(Data::ControllerPackageDeploymentState value)
{
    using State = Data::ControllerPackageDeploymentState;
    switch (value) {
    case State::Idle:
        return "idle";
    case State::Uploading:
        return "uploading";
    case State::Committing:
        return "committing";
    case State::Validating:
        return "validating";
    case State::Activating:
        return "activating";
    case State::RollingBack:
        return "rolling-back";
    case State::Canceling:
        return "canceling";
    case State::Succeeded:
        return "succeeded";
    case State::Canceled:
        return "canceled";
    case State::Failed:
        return "failed";
    case State::OutcomeUnknown:
        return "outcome-unknown";
    }
    return nullptr;
}

bool readDeploymentState(
    const StrictJson &value,
    const QString &path,
    Data::ControllerPackageDeploymentState *result,
    QString *error)
{
    using State = Data::ControllerPackageDeploymentState;
    QString text;
    if (!readString(value, path, &text, error, 32))
        return false;
    static constexpr std::array values{
        std::pair{"idle", State::Idle},
        std::pair{"uploading", State::Uploading},
        std::pair{"committing", State::Committing},
        std::pair{"validating", State::Validating},
        std::pair{"activating", State::Activating},
        std::pair{"rolling-back", State::RollingBack},
        std::pair{"canceling", State::Canceling},
        std::pair{"succeeded", State::Succeeded},
        std::pair{"canceled", State::Canceled},
        std::pair{"failed", State::Failed},
        std::pair{"outcome-unknown", State::OutcomeUnknown},
    };
    for (const auto &[name, candidate] : values) {
        if (text == QLatin1StringView(name)) {
            *result = candidate;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unknown deployment state").arg(path);
    return false;
}

StrictJson deploymentOutcomeJson(Data::RuntimePackageActivationDeploymentOutcome value)
{
    using DeploymentOutcome = Data::RuntimePackageActivationDeploymentOutcome;
    switch (value) {
    case DeploymentOutcome::Succeeded:
        return "succeeded";
    case DeploymentOutcome::Failed:
        return "failed";
    case DeploymentOutcome::OutcomeUnknown:
        return "outcome-unknown";
    }
    return nullptr;
}

bool readDeploymentOutcome(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationDeploymentOutcome *result,
    QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 32))
        return false;
    if (text == QStringLiteral("succeeded"))
        *result = Data::RuntimePackageActivationDeploymentOutcome::Succeeded;
    else if (text == QStringLiteral("failed"))
        *result = Data::RuntimePackageActivationDeploymentOutcome::Failed;
    else if (text == QStringLiteral("outcome-unknown"))
        *result = Data::RuntimePackageActivationDeploymentOutcome::OutcomeUnknown;
    else {
        *error = QString::fromLatin1("%1 contains an unknown deployment outcome").arg(path);
        return false;
    }
    return true;
}

StrictJson optionalU64Json(const std::optional<quint64> &value)
{
    return value ? u64Json(*value) : StrictJson(nullptr);
}

StrictJson optionalI32Json(const std::optional<qint32> &value)
{
    return value ? StrictJson(*value) : StrictJson(nullptr);
}

bool readOptionalU64(
    const StrictJson &value, const QString &path, std::optional<quint64> *result, QString *error)
{
    if (value.is_null()) {
        result->reset();
        return true;
    }
    quint64 parsed = 0;
    if (!readU64(value, path, &parsed, error))
        return false;
    *result = parsed;
    return true;
}

bool readOptionalI32(
    const StrictJson &value, const QString &path, std::optional<qint32> *result, QString *error)
{
    if (value.is_null()) {
        result->reset();
        return true;
    }
    qint32 parsed = 0;
    if (!readI32(value, path, &parsed, error))
        return false;
    *result = parsed;
    return true;
}

StrictJson deploymentAuditEventJson(const Data::ControllerPackageDeploymentAuditEvent &event)
{
    return {
        {"detail", utf8(event.detail)},
        {"occurredAt", dateTimeJson(event.occurredAt)},
        {"operation", controllerOperationJson(event.operation)},
        {"operationResult", optionalI32Json(event.operationResult)},
        {"requestId", optionalU64Json(event.requestId)},
        {"sequence", u64Json(event.sequence)},
        {"status", optionalI32Json(event.status)},
    };
}

bool readDeploymentAuditEvent(
    const StrictJson &value,
    const QString &path,
    Data::ControllerPackageDeploymentAuditEvent *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"detail", "occurredAt", "operation", "operationResult", "requestId", "sequence", "status"},
            error)
        || !readU64(value.at("sequence"), fieldPath(path, "sequence"), &result->sequence, error)
        || !readControllerOperation(
            value.at("operation"), fieldPath(path, "operation"), &result->operation, error)
        || !readOptionalU64(
            value.at("requestId"), fieldPath(path, "requestId"), &result->requestId, error)
        || !readOptionalI32(value.at("status"), fieldPath(path, "status"), &result->status, error)
        || !readOptionalI32(
            value.at("operationResult"),
            fieldPath(path, "operationResult"),
            &result->operationResult,
            error)
        || !readString(value.at("detail"), fieldPath(path, "detail"), &result->detail, error, 2048)
        || !readDateTime(
            value.at("occurredAt"),
            fieldPath(path, "occurredAt"),
            false,
            &result->occurredAt,
            error)) {
        return false;
    }
    return true;
}

StrictJson deploymentProgressJson(const Data::ControllerPackageDeploymentProgress &progress)
{
    StrictJson audit = StrictJson::array();
    for (const Data::ControllerPackageDeploymentAuditEvent &event : progress.audit)
        audit.push_back(deploymentAuditEventJson(event));
    return {
        {"artifactSha256", bytesJson(progress.artifactSha256)},
        {"audit", std::move(audit)},
        {"candidate",
         optionalJson(
             progress.candidate, [](const auto &selector) { return selectorJson(selector); })},
        {"completedAt", dateTimeJson(progress.completedAt)},
        {"detail", utf8(progress.detail)},
        {"operationId", utf8(progress.operationId)},
        {"operationResult", optionalI32Json(progress.operationResult)},
        {"previousActive",
         optionalJson(
             progress.previousActive, [](const auto &selector) { return selectorJson(selector); })},
        {"startedAt", dateTimeJson(progress.startedAt)},
        {"state", deploymentStateJson(progress.state)},
        {"status", optionalI32Json(progress.status)},
        {"totalBytes", i64Json(progress.totalBytes)},
        {"transferredBytes", i64Json(progress.transferredBytes)},
    };
}

bool readDeploymentProgress(
    const StrictJson &value,
    const QString &path,
    Data::ControllerPackageDeploymentProgress *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"artifactSha256",
             "audit",
             "candidate",
             "completedAt",
             "detail",
             "operationId",
             "operationResult",
             "previousActive",
             "startedAt",
             "state",
             "status",
             "totalBytes",
             "transferredBytes"},
            error)
        || !readString(
            value.at("operationId"), fieldPath(path, "operationId"), &result->operationId, error, 128)
        || !readBytes(
            value.at("artifactSha256"),
            fieldPath(path, "artifactSha256"),
            &result->artifactSha256,
            error,
            32)
        || !readDeploymentState(value.at("state"), fieldPath(path, "state"), &result->state, error)
        || !readI64(value.at("totalBytes"), fieldPath(path, "totalBytes"), &result->totalBytes, error)
        || !readI64(
            value.at("transferredBytes"),
            fieldPath(path, "transferredBytes"),
            &result->transferredBytes,
            error)
        || !readOptionalI32(value.at("status"), fieldPath(path, "status"), &result->status, error)
        || !readOptionalI32(
            value.at("operationResult"),
            fieldPath(path, "operationResult"),
            &result->operationResult,
            error)
        || !readString(value.at("detail"), fieldPath(path, "detail"), &result->detail, error, 2048)
        || !readDateTime(
            value.at("startedAt"), fieldPath(path, "startedAt"), true, &result->startedAt, error)
        || !readDateTime(
            value.at("completedAt"),
            fieldPath(path, "completedAt"),
            true,
            &result->completedAt,
            error)) {
        return false;
    }
    if (!value.at("candidate").is_null()) {
        Data::ControllerPackageSelector selector;
        if (!readSelector(value.at("candidate"), fieldPath(path, "candidate"), &selector, error)) {
            return false;
        }
        result->candidate = selector;
    }
    if (!value.at("previousActive").is_null()) {
        Data::ControllerPackageSelector selector;
        if (!readSelector(
                value.at("previousActive"), fieldPath(path, "previousActive"), &selector, error)) {
            return false;
        }
        result->previousActive = selector;
    }
    const StrictJson &audit = value.at("audit");
    if (!audit.is_array() || audit.size() > maximumDeploymentAuditEvents) {
        *error = QString::fromLatin1("%1 must be a bounded array").arg(fieldPath(path, "audit"));
        return false;
    }
    result->audit.reserve(qsizetype(audit.size()));
    for (std::size_t index = 0; index < audit.size(); ++index) {
        Data::ControllerPackageDeploymentAuditEvent event;
        if (!readDeploymentAuditEvent(
                audit.at(index), arrayPath(fieldPath(path, "audit"), index), &event, error)) {
            return false;
        }
        result->audit.append(std::move(event));
    }
    return true;
}

StrictJson deploymentEvidenceJson(const Data::RuntimePackageActivationDeploymentEvidence &evidence)
{
    return {
        {"artifactSha256", bytesJson(evidence.artifactSha256().value())},
        {"evidenceSha256", bytesJson(evidence.evidenceSha256().value())},
        {"internalAuditSha256", bytesJson(evidence.internalAuditSha256().value())},
        {"observedAt", dateTimeJson(evidence.observedAt())},
        {"outcome", deploymentOutcomeJson(evidence.outcome())},
        {"progress", deploymentProgressJson(evidence.progress())},
        {"providerBootId", u64Json(evidence.providerBootId())},
        {"providerSessionGeneration", u64Json(evidence.providerSessionGeneration())},
        {"providerSessionId", u64Json(evidence.providerSessionId())},
    };
}

bool readDeploymentEvidence(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationDeploymentEvidence *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"artifactSha256",
             "evidenceSha256",
             "internalAuditSha256",
             "observedAt",
             "outcome",
             "progress",
             "providerBootId",
             "providerSessionGeneration",
             "providerSessionId"},
            error)) {
        return false;
    }
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    Data::ControllerPackageDeploymentProgress progress;
    QDateTime observedAt;
    QByteArray artifactSha;
    QByteArray auditSha;
    QByteArray evidenceSha;
    Data::RuntimePackageActivationDeploymentOutcome serializedOutcome
        = Data::RuntimePackageActivationDeploymentOutcome::OutcomeUnknown;
    if (!readU64(
            value.at("providerSessionGeneration"),
            fieldPath(path, "providerSessionGeneration"),
            &sessionGeneration,
            error)
        || !readU64(
            value.at("providerSessionId"), fieldPath(path, "providerSessionId"), &sessionId, error)
        || !readU64(value.at("providerBootId"), fieldPath(path, "providerBootId"), &bootId, error)
        || !readDeploymentProgress(value.at("progress"), fieldPath(path, "progress"), &progress, error)
        || !readDeploymentOutcome(
            value.at("outcome"), fieldPath(path, "outcome"), &serializedOutcome, error)
        || !readDateTime(
            value.at("observedAt"), fieldPath(path, "observedAt"), false, &observedAt, error)
        || !readBytes(
            value.at("artifactSha256"), fieldPath(path, "artifactSha256"), &artifactSha, error, 32)
        || !readBytes(
            value.at("internalAuditSha256"),
            fieldPath(path, "internalAuditSha256"),
            &auditSha,
            error,
            32)
        || !readBytes(
            value.at("evidenceSha256"), fieldPath(path, "evidenceSha256"), &evidenceSha, error, 32)) {
        return false;
    }
    Data::RuntimePackageActivationDeploymentEvidence
        evidence{sessionGeneration, sessionId, bootId, progress, observedAt};
    if (!evidence.isValid() || evidence.outcome() != serializedOutcome
        || evidence.artifactSha256().value() != artifactSha
        || evidence.internalAuditSha256().value() != auditSha
        || evidence.evidenceSha256().value() != evidenceSha) {
        *error = QString::fromLatin1("%1 failed deployment-evidence integrity validation").arg(path);
        return false;
    }
    *result = std::move(evidence);
    return true;
}

StrictJson auditKindJson(AuditKind value)
{
    switch (value) {
    case AuditKind::IntentPersisted:
        return "intent-persisted";
    case AuditKind::PhaseTransition:
        return "phase-transition";
    case AuditKind::ProviderRequestSent:
        return "provider-request-sent";
    case AuditKind::ProviderTerminalResponse:
        return "provider-terminal-response";
    case AuditKind::ProviderOutcomeReconciled:
        return "provider-outcome-reconciled";
    case AuditKind::ControlLeaseReconciled:
        return "control-lease-reconciled";
    case AuditKind::DeploymentEvidenceCaptured:
        return "deployment-evidence-captured";
    case AuditKind::ControllerEvidenceCaptured:
        return "controller-evidence-captured";
    case AuditKind::ProjectCompareAndSet:
        return "project-compare-and-set";
    case AuditKind::CancellationRequested:
        return "cancellation-requested";
    case AuditKind::ReconciliationStarted:
        return "reconciliation-started";
    case AuditKind::Completed:
        return "completed";
    }
    return nullptr;
}

bool readAuditKind(const StrictJson &value, const QString &path, AuditKind *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 48))
        return false;
    static constexpr std::array values{
        std::pair{"intent-persisted", AuditKind::IntentPersisted},
        std::pair{"phase-transition", AuditKind::PhaseTransition},
        std::pair{"provider-request-sent", AuditKind::ProviderRequestSent},
        std::pair{"provider-terminal-response", AuditKind::ProviderTerminalResponse},
        std::pair{"provider-outcome-reconciled", AuditKind::ProviderOutcomeReconciled},
        std::pair{"control-lease-reconciled", AuditKind::ControlLeaseReconciled},
        std::pair{"deployment-evidence-captured", AuditKind::DeploymentEvidenceCaptured},
        std::pair{"controller-evidence-captured", AuditKind::ControllerEvidenceCaptured},
        std::pair{"project-compare-and-set", AuditKind::ProjectCompareAndSet},
        std::pair{"cancellation-requested", AuditKind::CancellationRequested},
        std::pair{"reconciliation-started", AuditKind::ReconciliationStarted},
        std::pair{"completed", AuditKind::Completed},
    };
    for (const auto &[name, candidate] : values) {
        if (text == QLatin1StringView(name)) {
            *result = candidate;
            return true;
        }
    }
    *error = QString::fromLatin1("%1 contains an unknown audit kind").arg(path);
    return false;
}

StrictJson reconciliationJson(Reconciliation value)
{
    switch (value) {
    case Reconciliation::None:
        return "none";
    case Reconciliation::Applied:
        return "applied";
    case Reconciliation::NotApplied:
        return "not-applied";
    }
    return nullptr;
}

bool readReconciliation(
    const StrictJson &value, const QString &path, Reconciliation *result, QString *error)
{
    QString text;
    if (!readString(value, path, &text, error, 16))
        return false;
    if (text == QStringLiteral("none"))
        *result = Reconciliation::None;
    else if (text == QStringLiteral("applied"))
        *result = Reconciliation::Applied;
    else if (text == QStringLiteral("not-applied"))
        *result = Reconciliation::NotApplied;
    else {
        *error = QString::fromLatin1("%1 contains an unknown reconciliation value").arg(path);
        return false;
    }
    return true;
}

StrictJson auditEventJson(const AuditEvent &event)
{
    return {
        {"code", utf8(event.code())},
        {"detail", utf8(event.detail())},
        {"evidenceSha256",
         optionalJson(
             event.evidenceSha256(), [](const auto &digest) { return bytesJson(digest.value()); })},
        {"kind", auditKindJson(event.kind())},
        {"occurredAt", dateTimeJson(event.occurredAt())},
        {"outcome", outcomeJson(event.outcome())},
        {"phase", phaseJson(event.phase())},
        {"providerAction", actionJson(event.providerAction())},
        {"providerBootId", u64Json(event.providerBootId())},
        {"providerOperationId", utf8(event.providerOperationId())},
        {"providerOperationResult", optionalI32Json(event.providerOperationResult())},
        {"providerReconciliation", reconciliationJson(event.providerReconciliation())},
        {"providerRequestId", optionalU64Json(event.providerRequestId())},
        {"providerSessionGeneration", u64Json(event.providerSessionGeneration())},
        {"providerSessionId", u64Json(event.providerSessionId())},
        {"providerStatus", optionalI32Json(event.providerStatus())},
        {"sequence", u64Json(event.sequence())},
    };
}

bool readAuditEvent(const StrictJson &value, const QString &path, AuditEvent *result, QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"code",
             "detail",
             "evidenceSha256",
             "kind",
             "occurredAt",
             "outcome",
             "phase",
             "providerAction",
             "providerBootId",
             "providerOperationId",
             "providerOperationResult",
             "providerReconciliation",
             "providerRequestId",
             "providerSessionGeneration",
             "providerSessionId",
             "providerStatus",
             "sequence"},
            error)) {
        return false;
    }
    quint64 sequence = 0;
    AuditKind kind = AuditKind::IntentPersisted;
    Phase phase = Phase::Idle;
    Outcome outcome = Outcome::Pending;
    Action action = Action::None;
    Reconciliation reconciliation = Reconciliation::None;
    QString operationId;
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    std::optional<quint64> requestId;
    std::optional<qint32> status;
    std::optional<qint32> operationResult;
    std::optional<Data::RuntimePackageActivationSha256> evidenceSha;
    QString code;
    QString detail;
    QDateTime occurredAt;
    if (!readU64(value.at("sequence"), fieldPath(path, "sequence"), &sequence, error)
        || !readAuditKind(value.at("kind"), fieldPath(path, "kind"), &kind, error)
        || !readPhase(value.at("phase"), fieldPath(path, "phase"), &phase, error)
        || !readOutcome(value.at("outcome"), fieldPath(path, "outcome"), &outcome, error)
        || !readAction(value.at("providerAction"), fieldPath(path, "providerAction"), &action, error)
        || !readReconciliation(
            value.at("providerReconciliation"),
            fieldPath(path, "providerReconciliation"),
            &reconciliation,
            error)
        || !readString(
            value.at("providerOperationId"),
            fieldPath(path, "providerOperationId"),
            &operationId,
            error,
            128)
        || !readU64(
            value.at("providerSessionGeneration"),
            fieldPath(path, "providerSessionGeneration"),
            &sessionGeneration,
            error)
        || !readU64(
            value.at("providerSessionId"), fieldPath(path, "providerSessionId"), &sessionId, error)
        || !readU64(value.at("providerBootId"), fieldPath(path, "providerBootId"), &bootId, error)
        || !readOptionalU64(
            value.at("providerRequestId"), fieldPath(path, "providerRequestId"), &requestId, error)
        || !readOptionalI32(
            value.at("providerStatus"), fieldPath(path, "providerStatus"), &status, error)
        || !readOptionalI32(
            value.at("providerOperationResult"),
            fieldPath(path, "providerOperationResult"),
            &operationResult,
            error)
        || !readString(value.at("code"), fieldPath(path, "code"), &code, error, 128)
        || !readString(value.at("detail"), fieldPath(path, "detail"), &detail, error, 2048)
        || !readDateTime(
            value.at("occurredAt"), fieldPath(path, "occurredAt"), false, &occurredAt, error)) {
        return false;
    }
    if (!value.at("evidenceSha256").is_null()) {
        QByteArray digest;
        if (!readBytes(
                value.at("evidenceSha256"), fieldPath(path, "evidenceSha256"), &digest, error, 32)) {
            return false;
        }
        evidenceSha = Data::RuntimePackageActivationSha256{digest};
    }
    AuditEvent event{
        sequence,
        kind,
        phase,
        outcome,
        action,
        reconciliation,
        operationId,
        sessionGeneration,
        sessionId,
        bootId,
        requestId,
        status,
        operationResult,
        evidenceSha,
        code,
        detail,
        occurredAt,
    };
    if (!event.isValid()) {
        *error = QString::fromLatin1("%1 is not a valid activation audit event").arg(path);
        return false;
    }
    *result = std::move(event);
    return true;
}

StrictJson recordJson(const Data::RuntimePackageActivationRecord &record)
{
    StrictJson audit = StrictJson::array();
    for (const AuditEvent &event : record.audit())
        audit.push_back(auditEventJson(event));
    StrictJson history = StrictJson::array();
    for (const Data::RuntimePackageActivationControllerEvidence &evidence :
         record.controllerEvidenceHistory()) {
        history.push_back(controllerEvidenceJson(evidence));
    }
    return {
        {"afterController",
         optionalJson(
             record.afterController(),
             [](const auto &evidence) { return controllerEvidenceJson(evidence); })},
        {"audit", std::move(audit)},
        {"beforeController",
         optionalJson(
             record.beforeController(),
             [](const auto &evidence) { return controllerEvidenceJson(evidence); })},
        {"cancellation",
         optionalJson(
             record.cancellation(),
             [](const auto &cancellation) { return cancellationJson(cancellation); })},
        {"completedAt", dateTimeJson(record.completedAt())},
        {"controllerEvidenceHistory", std::move(history)},
        {"deploymentEvidence",
         optionalJson(
             record.deploymentEvidence(),
             [](const auto &evidence) { return deploymentEvidenceJson(evidence); })},
        {"detail", utf8(record.detail())},
        {"identity", identityJson(record.identity())},
        {"outcome", outcomeJson(record.outcome())},
        {"phase", phaseJson(record.phase())},
        {"projectCommit",
         optionalJson(
             record.projectCommit(), [](const auto &commit) { return projectCommitJson(commit); })},
        {"requestFingerprint", bytesJson(record.requestFingerprint().value())},
        {"revision", u64Json(record.revision())},
        {"rollbackOnActivationFailure", record.rollbackOnActivationFailure()},
        {"startedAt", dateTimeJson(record.startedAt())},
        {"updatedAt", dateTimeJson(record.updatedAt())},
    };
}

bool readOptionalControllerEvidence(
    const StrictJson &value,
    const QString &path,
    std::optional<Data::RuntimePackageActivationControllerEvidence> *result,
    QString *error)
{
    if (value.is_null()) {
        result->reset();
        return true;
    }
    Data::RuntimePackageActivationControllerEvidence evidence;
    if (!readControllerEvidence(value, path, &evidence, error))
        return false;
    *result = std::move(evidence);
    return true;
}

bool readOptionalProjectCommit(
    const StrictJson &value,
    const QString &path,
    std::optional<Data::RuntimePackageActivationProjectCommit> *result,
    QString *error)
{
    if (value.is_null()) {
        result->reset();
        return true;
    }
    Data::RuntimePackageActivationProjectCommit commit;
    if (!readProjectCommit(value, path, &commit, error))
        return false;
    *result = std::move(commit);
    return true;
}

bool readRecord(
    const StrictJson &value,
    const QString &path,
    Data::RuntimePackageActivationRecord *result,
    QString *error)
{
    if (!hasExactFields(
            value,
            path,
            {"afterController",
             "audit",
             "beforeController",
             "cancellation",
             "completedAt",
             "controllerEvidenceHistory",
             "deploymentEvidence",
             "detail",
             "identity",
             "outcome",
             "phase",
             "projectCommit",
             "requestFingerprint",
             "revision",
             "rollbackOnActivationFailure",
             "startedAt",
             "updatedAt"},
            error)) {
        return false;
    }
    if (!value.at("rollbackOnActivationFailure").is_boolean()) {
        *error = QString::fromLatin1("%1 must be a boolean")
                     .arg(fieldPath(path, "rollbackOnActivationFailure"));
        return false;
    }

    Data::RuntimePackageActivationIdentity identity;
    QByteArray requestFingerprint;
    quint64 revision = 0;
    Phase phase = Phase::Idle;
    Outcome outcome = Outcome::Pending;
    QString detail;
    QDateTime startedAt;
    QDateTime updatedAt;
    QDateTime completedAt;
    std::optional<Data::RuntimePackageActivationControllerEvidence> before;
    std::optional<Data::RuntimePackageActivationControllerEvidence> after;
    std::optional<Data::RuntimePackageActivationProjectCommit> projectCommit;
    std::optional<Data::RuntimePackageActivationCancellation> cancellation;
    std::optional<Data::RuntimePackageActivationDeploymentEvidence> deploymentEvidence;
    QList<AuditEvent> audit;
    QList<Data::RuntimePackageActivationControllerEvidence> history;
    if (!readIdentity(value.at("identity"), fieldPath(path, "identity"), &identity, error)
        || !readBytes(
            value.at("requestFingerprint"),
            fieldPath(path, "requestFingerprint"),
            &requestFingerprint,
            error,
            32)
        || !readU64(value.at("revision"), fieldPath(path, "revision"), &revision, error)
        || !readPhase(value.at("phase"), fieldPath(path, "phase"), &phase, error)
        || !readOutcome(value.at("outcome"), fieldPath(path, "outcome"), &outcome, error)
        || !readString(value.at("detail"), fieldPath(path, "detail"), &detail, error, 2048)
        || !readDateTime(value.at("startedAt"), fieldPath(path, "startedAt"), false, &startedAt, error)
        || !readDateTime(value.at("updatedAt"), fieldPath(path, "updatedAt"), false, &updatedAt, error)
        || !readDateTime(
            value.at("completedAt"), fieldPath(path, "completedAt"), true, &completedAt, error)
        || !readOptionalControllerEvidence(
            value.at("beforeController"), fieldPath(path, "beforeController"), &before, error)
        || !readOptionalControllerEvidence(
            value.at("afterController"), fieldPath(path, "afterController"), &after, error)
        || !readOptionalProjectCommit(
            value.at("projectCommit"), fieldPath(path, "projectCommit"), &projectCommit, error)) {
        return false;
    }
    if (!value.at("cancellation").is_null()) {
        Data::RuntimePackageActivationCancellation parsed;
        if (!readCancellation(
                value.at("cancellation"), fieldPath(path, "cancellation"), &parsed, error)) {
            return false;
        }
        cancellation = std::move(parsed);
    }
    if (!value.at("deploymentEvidence").is_null()) {
        Data::RuntimePackageActivationDeploymentEvidence parsed;
        if (!readDeploymentEvidence(
                value.at("deploymentEvidence"),
                fieldPath(path, "deploymentEvidence"),
                &parsed,
                error)) {
            return false;
        }
        deploymentEvidence = std::move(parsed);
    }

    const StrictJson &auditJson = value.at("audit");
    if (!auditJson.is_array() || auditJson.empty() || auditJson.size() > maximumAuditEvents) {
        *error = QString::fromLatin1("%1 must be a nonempty bounded array")
                     .arg(fieldPath(path, "audit"));
        return false;
    }
    audit.reserve(qsizetype(auditJson.size()));
    for (std::size_t index = 0; index < auditJson.size(); ++index) {
        AuditEvent event;
        if (!readAuditEvent(
                auditJson.at(index), arrayPath(fieldPath(path, "audit"), index), &event, error)) {
            return false;
        }
        audit.append(std::move(event));
    }

    const StrictJson &historyJson = value.at("controllerEvidenceHistory");
    if (!historyJson.is_array() || historyJson.size() > maximumControllerEvidence) {
        *error = QString::fromLatin1("%1 must be a bounded array")
                     .arg(fieldPath(path, "controllerEvidenceHistory"));
        return false;
    }
    history.reserve(qsizetype(historyJson.size()));
    for (std::size_t index = 0; index < historyJson.size(); ++index) {
        Data::RuntimePackageActivationControllerEvidence evidence;
        if (!readControllerEvidence(
                historyJson.at(index),
                arrayPath(fieldPath(path, "controllerEvidenceHistory"), index),
                &evidence,
                error)) {
            return false;
        }
        history.append(std::move(evidence));
    }

    Data::RuntimePackageActivationRecord record{
        identity,
        Data::RuntimePackageActivationSha256{requestFingerprint},
        value.at("rollbackOnActivationFailure").get<bool>(),
        revision,
        phase,
        outcome,
        audit,
        detail,
        startedAt,
        updatedAt,
        completedAt,
        before,
        after,
        projectCommit,
        cancellation,
        deploymentEvidence,
        history,
    };
    if (!record.isValid()) {
        *error = QString::fromLatin1("%1 is not a valid strict activation record").arg(path);
        return false;
    }
    *result = std::move(record);
    return true;
}

struct ProviderRequestKey
{
    Action action = Action::None;
    QString operationId;
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    std::optional<quint64> requestId;

    friend bool operator==(const ProviderRequestKey &, const ProviderRequestKey &) = default;
};

ProviderRequestKey providerRequestKey(const AuditEvent &event)
{
    return {
        event.providerAction(),
        event.providerOperationId(),
        event.providerSessionGeneration(),
        event.providerSessionId(),
        event.providerBootId(),
        event.providerRequestId(),
    };
}

std::optional<Action> deriveOutstandingProviderAction(
    const Data::RuntimePackageActivationRecord &record)
{
    QList<ProviderRequestKey> outstanding;
    for (const AuditEvent &event : record.audit()) {
        if (event.kind() == AuditKind::ProviderRequestSent) {
            outstanding.append(providerRequestKey(event));
            continue;
        }
        if (event.kind() != AuditKind::ProviderTerminalResponse
            && event.kind() != AuditKind::ProviderOutcomeReconciled) {
            continue;
        }
        const ProviderRequestKey key = providerRequestKey(event);
        const auto found = std::find(outstanding.cbegin(), outstanding.cend(), key);
        if (found == outstanding.cend())
            return std::nullopt;
        outstanding.erase(found);
    }
    if (outstanding.size() > 1)
        return std::nullopt;
    return outstanding.isEmpty() ? std::optional(Action::None)
                                 : std::optional(outstanding.constFirst().action);
}

StrictJson controllerEvidenceArrayJson(
    const QList<Data::RuntimePackageActivationControllerEvidence> &history)
{
    StrictJson array = StrictJson::array();
    for (const Data::RuntimePackageActivationControllerEvidence &evidence : history)
        array.push_back(controllerEvidenceJson(evidence));
    return array;
}

bool readControllerEvidenceArray(
    const StrictJson &value,
    const QString &path,
    QList<Data::RuntimePackageActivationControllerEvidence> *result,
    QString *error)
{
    if (!value.is_array() || value.size() > maximumControllerEvidence) {
        *error = QString::fromLatin1("%1 must be a bounded array").arg(path);
        return false;
    }
    result->reserve(qsizetype(value.size()));
    for (std::size_t index = 0; index < value.size(); ++index) {
        Data::RuntimePackageActivationControllerEvidence evidence;
        if (!readControllerEvidence(value.at(index), arrayPath(path, index), &evidence, error))
            return false;
        result->append(std::move(evidence));
    }
    return true;
}

StrictJson journalJson(const RuntimePackageActivationJournalState &state)
{
    return {
        {"afterController",
         optionalJson(
             state.afterController,
             [](const auto &evidence) { return controllerEvidenceJson(evidence); })},
        {"beforeController",
         optionalJson(
             state.beforeController,
             [](const auto &evidence) { return controllerEvidenceJson(evidence); })},
        {"controllerEvidenceHistory", controllerEvidenceArrayJson(state.controllerEvidenceHistory)},
        {"format", journalFormat},
        {"formatVersion", journalFormatVersion},
        {"outstandingProviderAction", actionJson(state.outstandingProviderAction)},
        {"persistedDocumentRevision",
         optionalJson(
             state.persistedDocumentRevision,
             [](const auto &token) { return bytesJson(token.value()); })},
        {"phase", phaseJson(state.phase)},
        {"projectCommit",
         optionalJson(
             state.projectCommit, [](const auto &commit) { return projectCommitJson(commit); })},
        {"record", recordJson(state.record)},
        {"request", requestJson(state.request)},
    };
}

bool readFormat(const StrictJson &value, const QString &path, QString *error)
{
    QString format;
    if (!readString(value, path, &format, error, 64))
        return false;
    if (format != QLatin1StringView(journalFormat.data(), journalFormat.size())) {
        *error = QString::fromLatin1("%1 contains an unsupported journal format").arg(path);
        return false;
    }
    return true;
}

} // namespace

bool RuntimePackageActivationJournalState::isValid() const
{
    if (!request.isValid() || !record.isValid() || phase != record.phase()
        || request.identity() != record.identity()
        || request.fingerprint() != record.requestFingerprint()
        || request.rollbackOnActivationFailure() != record.rollbackOnActivationFailure()
        || beforeController != record.beforeController()
        || afterController != record.afterController()
        || controllerEvidenceHistory != record.controllerEvidenceHistory()
        || projectCommit != record.projectCommit()
        || (persistedDocumentRevision
            && (!persistedDocumentRevision->isValid() || !projectCommit))) {
        return false;
    }
    const std::optional<Action> derived = deriveOutstandingProviderAction(record);
    return derived && *derived == outstandingProviderAction;
}

Utils::Result<QByteArray> serializeRuntimePackageActivationJournal(
    const RuntimePackageActivationJournalState &state)
{
    if (!state.isValid())
        return journalError(QStringLiteral("the journal state is internally inconsistent"));
    Utils::Result<QByteArray> serialized
        = serializeCanonicalJson(journalJson(state), maximumJournalBytes);
    if (!serialized)
        return journalError(serialized.error());
    return serialized;
}

Utils::Result<RuntimePackageActivationJournalState> parseRuntimePackageActivationJournal(
    QByteArrayView bytes)
{
    Utils::Result<StrictJson> parsed = parseCanonicalJson(bytes, maximumJournalBytes);
    if (!parsed)
        return journalError(parsed.error());

    const StrictJson &root = *parsed;
    QString error;
    if (!hasExactFields(
            root,
            QStringLiteral("$"),
            {"afterController",
             "beforeController",
             "controllerEvidenceHistory",
             "format",
             "formatVersion",
             "outstandingProviderAction",
             "persistedDocumentRevision",
             "phase",
             "projectCommit",
             "record",
             "request"},
            &error)
        || !readFormat(root.at("format"), QStringLiteral("$.format"), &error)) {
        return journalError(error);
    }
    quint64 formatVersion = 0;
    if (!readUnsigned(
            root.at("formatVersion"),
            std::numeric_limits<quint32>::max(),
            QStringLiteral("$.formatVersion"),
            &formatVersion,
            &error)
        || formatVersion != journalFormatVersion) {
        if (error.isEmpty())
            error = QStringLiteral("$.formatVersion is unsupported");
        return journalError(error);
    }

    RuntimePackageActivationJournalState state;
    if (!readRequest(root.at("request"), QStringLiteral("$.request"), &state.request, &error)
        || !readPhase(root.at("phase"), QStringLiteral("$.phase"), &state.phase, &error)
        || !readAction(
            root.at("outstandingProviderAction"),
            QStringLiteral("$.outstandingProviderAction"),
            &state.outstandingProviderAction,
            &error)
        || !readOptionalControllerEvidence(
            root.at("beforeController"),
            QStringLiteral("$.beforeController"),
            &state.beforeController,
            &error)
        || !readOptionalControllerEvidence(
            root.at("afterController"),
            QStringLiteral("$.afterController"),
            &state.afterController,
            &error)
        || !readControllerEvidenceArray(
            root.at("controllerEvidenceHistory"),
            QStringLiteral("$.controllerEvidenceHistory"),
            &state.controllerEvidenceHistory,
            &error)
        || !readOptionalProjectCommit(
            root.at("projectCommit"), QStringLiteral("$.projectCommit"), &state.projectCommit, &error)
        || !readRecord(root.at("record"), QStringLiteral("$.record"), &state.record, &error)) {
        return journalError(error);
    }
    if (!root.at("persistedDocumentRevision").is_null()) {
        QByteArray token;
        if (!readBytes(
                root.at("persistedDocumentRevision"),
                QStringLiteral("$.persistedDocumentRevision"),
                &token,
                &error,
                32)) {
            return journalError(error);
        }
        state.persistedDocumentRevision = Data::RuntimePackageActivationDocumentRevisionToken{token};
    }
    if (!state.isValid())
        return journalError(QStringLiteral("decoded state failed cross-field validation"));

    // Reject any serializer/parser drift even if the semantic values happen to
    // compare equal. Recovery consumes one exact, canonical v1 representation.
    Utils::Result<QByteArray> reserialized = serializeRuntimePackageActivationJournal(state);
    if (!reserialized || *reserialized != bytes) {
        return journalError(
            QStringLiteral("decoded state does not round-trip to the exact canonical bytes"));
    }
    return state;
}

} // namespace EtherCAT::SemanticRuntime::Internal
