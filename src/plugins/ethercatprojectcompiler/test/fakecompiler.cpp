// Copyright (C) 2026 Embed Labs

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

using JsonMembers = QMap<QString, QByteArray>;

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray jsonString(const QString &value)
{
    QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    encoded.remove(0, 1);
    encoded.chop(1);
    return encoded;
}

QByteArray jsonObject(const JsonMembers &members)
{
    QByteArray result{"{"};
    bool first = true;
    for (auto it = members.cbegin(); it != members.cend(); ++it) {
        if (!first)
            result += ',';
        first = false;
        result += jsonString(it.key());
        result += ':';
        result += it.value();
    }
    result += '}';
    return result;
}

QByteArray jsonArray(const QList<QByteArray> &values)
{
    QByteArray result{"["};
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (index)
            result += ',';
        result += values.at(index);
    }
    result += ']';
    return result;
}

std::optional<QByteArray> canonicalValue(const QJsonValue &value, int depth = 0)
{
    if (depth > 128)
        return std::nullopt;
    if (value.isNull() || value.isUndefined())
        return QByteArray("null");
    if (value.isBool())
        return value.toBool() ? QByteArray("true") : QByteArray("false");
    if (value.isString())
        return jsonString(value.toString());
    if (value.isDouble()) {
        const double number = value.toDouble();
        constexpr double maximumExactInteger = 9007199254740991.0;
        if (!std::isfinite(number) || std::floor(number) != number || number < -maximumExactInteger
            || number > maximumExactInteger) {
            return std::nullopt;
        }
        return QString::number(qint64(number)).toLatin1();
    }
    if (value.isArray()) {
        QList<QByteArray> items;
        for (const QJsonValue &item : value.toArray()) {
            const auto encoded = canonicalValue(item, depth + 1);
            if (!encoded)
                return std::nullopt;
            items.append(*encoded);
        }
        return jsonArray(items);
    }
    if (value.isObject()) {
        JsonMembers members;
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const auto encoded = canonicalValue(it.value(), depth + 1);
            if (!encoded)
                return std::nullopt;
            members.insert(it.key(), *encoded);
        }
        return jsonObject(members);
    }
    return std::nullopt;
}

QByteArray canonicalDocument(const JsonMembers &members)
{
    return jsonObject(members) + '\n';
}

bool skipString(QByteArrayView bytes, qsizetype *offset)
{
    if (*offset >= bytes.size() || bytes[*offset] != '"')
        return false;
    ++*offset;
    while (*offset < bytes.size()) {
        const char character = bytes[*offset];
        ++*offset;
        if (character == '"')
            return true;
        if (character != '\\')
            continue;
        if (*offset >= bytes.size())
            return false;
        const char escaped = bytes[*offset];
        ++*offset;
        if (escaped == 'u') {
            if (*offset + 4 > bytes.size())
                return false;
            *offset += 4;
        }
    }
    return false;
}

bool skipValue(QByteArrayView bytes, qsizetype *offset, int depth = 0)
{
    if (depth > 128 || *offset >= bytes.size())
        return false;
    const char first = bytes[*offset];
    if (first == '"')
        return skipString(bytes, offset);
    if (first == '{') {
        ++*offset;
        if (*offset < bytes.size() && bytes[*offset] == '}') {
            ++*offset;
            return true;
        }
        while (*offset < bytes.size()) {
            if (!skipString(bytes, offset) || *offset >= bytes.size() || bytes[*offset] != ':')
                return false;
            ++*offset;
            if (!skipValue(bytes, offset, depth + 1))
                return false;
            if (*offset < bytes.size() && bytes[*offset] == '}') {
                ++*offset;
                return true;
            }
            if (*offset >= bytes.size() || bytes[*offset] != ',')
                return false;
            ++*offset;
        }
        return false;
    }
    if (first == '[') {
        ++*offset;
        if (*offset < bytes.size() && bytes[*offset] == ']') {
            ++*offset;
            return true;
        }
        while (*offset < bytes.size()) {
            if (!skipValue(bytes, offset, depth + 1))
                return false;
            if (*offset < bytes.size() && bytes[*offset] == ']') {
                ++*offset;
                return true;
            }
            if (*offset >= bytes.size() || bytes[*offset] != ',')
                return false;
            ++*offset;
        }
        return false;
    }
    while (*offset < bytes.size()) {
        const char character = bytes[*offset];
        if (character == ',' || character == '}' || character == ']')
            break;
        ++*offset;
    }
    return true;
}

std::optional<QByteArray> rootValue(const QByteArray &canonical, QByteArrayView wantedKey)
{
    if (canonical.size() < 3 || canonical.front() != '{' || !canonical.endsWith('\n'))
        return std::nullopt;
    const QByteArrayView bytes(canonical.constData(), canonical.size() - 1);
    qsizetype offset = 1;
    if (bytes[offset] == '}')
        return std::nullopt;
    while (offset < bytes.size()) {
        const qsizetype keyStart = offset;
        if (!skipString(bytes, &offset) || offset >= bytes.size() || bytes[offset] != ':')
            return std::nullopt;
        const QByteArrayView encodedKey = bytes.sliced(keyStart, offset - keyStart);
        ++offset;
        const qsizetype valueStart = offset;
        if (!skipValue(bytes, &offset))
            return std::nullopt;
        QByteArray wantedEncoded(1, '"');
        wantedEncoded.append(wantedKey.data(), wantedKey.size());
        wantedEncoded += '"';
        if (encodedKey == QByteArrayView(wantedEncoded))
            return QByteArray(bytes.sliced(valueStart, offset - valueStart));
        if (offset < bytes.size() && bytes[offset] == '}')
            return std::nullopt;
        if (offset >= bytes.size() || bytes[offset] != ',')
            return std::nullopt;
        ++offset;
    }
    return std::nullopt;
}

std::optional<quint64> unsignedRootValue(const QByteArray &canonical, QByteArrayView key)
{
    const auto raw = rootValue(canonical, key);
    if (!raw || raw->isEmpty() || !std::all_of(raw->cbegin(), raw->cend(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        return std::nullopt;
    }
    bool ok = false;
    const quint64 value = raw->toULongLong(&ok);
    return ok ? std::optional<quint64>{value} : std::nullopt;
}

bool readFile(const QString &path, QByteArray *bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *bytes = file.readAll();
    return file.error() == QFile::NoError;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size() && file.flush();
}

void appendObservation(const QString &path, const QByteArray &line)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(line);
        file.flush();
    }
}

struct Arguments
{
    QString command;
    QMap<QString, QString> values;
};

std::optional<Arguments> parseArguments(
    const QStringList &arguments,
    const QMap<QString, QSet<QString>> &required,
    const QMap<QString, QSet<QString>> &optional = {})
{
    if (arguments.size() < 2 || !required.contains(arguments.at(1)))
        return std::nullopt;
    Arguments result{arguments.at(1), {}};
    const QSet<QString> allowed = required.value(result.command) | optional.value(result.command);
    for (qsizetype index = 2; index < arguments.size(); index += 2) {
        if (index + 1 >= arguments.size() || !arguments.at(index).startsWith("--"))
            return std::nullopt;
        const QString name = arguments.at(index).mid(2);
        if (!allowed.contains(name) || result.values.contains(name))
            return std::nullopt;
        result.values.insert(name, arguments.at(index + 1));
    }
    for (const QString &name : required.value(result.command)) {
        if (!result.values.contains(name) || result.values.value(name).isEmpty())
            return std::nullopt;
    }
    return result;
}

QByteArray failureDocument(
    const QString &code,
    const QString &stage,
    const QString &path,
    const QString &message,
    bool retryable = false)
{
    const QByteArray diagnostic = jsonObject({
        {QStringLiteral("code"), jsonString(code)},
        {QStringLiteral("format"),
         jsonString(QStringLiteral("ethercat-ide-compiler-diagnostic-v1"))},
        {QStringLiteral("message"), jsonString(message)},
        {QStringLiteral("path"), jsonString(path)},
        {QStringLiteral("retryable"), retryable ? QByteArray("true") : QByteArray("false")},
        {QStringLiteral("severity"), jsonString(QStringLiteral("error"))},
        {QStringLiteral("stage"), jsonString(stage)},
    });
    return canonicalDocument({
        {QStringLiteral("diagnostics"), jsonArray({diagnostic})},
        {QStringLiteral("status"), jsonString(QStringLiteral("fail"))},
    });
}

int fail(const QByteArray &document)
{
    QFile error;
    if (!error.open(stderr, QIODevice::WriteOnly))
        return 1;
    error.write(document);
    error.flush();
    return 1;
}

int succeed(const QByteArray &document, bool extraObject = false)
{
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly))
        return 1;
    output.write(document);
    if (extraObject)
        output.write("{}\n");
    output.flush();
    return 0;
}

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

std::optional<QJsonObject> objectDocument(const QByteArray &bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

struct CompilerLedger
{
    QJsonObject configurationIds;
    QJsonObject operations;
    QJsonObject signerResponses;
};

std::optional<CompilerLedger> exactCompilerLedger(const QByteArray &bytes)
{
    const std::optional<QJsonObject> document = objectDocument(bytes);
    if (!document || document->size() != 5
        || document->value(QStringLiteral("format")).toString()
               != QStringLiteral("ethercat-ide-compiler-ledger-v1")
        || !document->value(QStringLiteral("format_version")).isDouble()
        || document->value(QStringLiteral("format_version")).toInteger() != 1
        || !document->value(QStringLiteral("configuration_ids")).isObject()
        || !document->value(QStringLiteral("operations")).isObject()
        || !document->value(QStringLiteral("signer_responses")).isObject()) {
        return std::nullopt;
    }
    const QSet<QString> expectedKeys{
        QStringLiteral("configuration_ids"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("operations"),
        QStringLiteral("signer_responses"),
    };
    QSet<QString> actualKeys;
    for (auto it = document->constBegin(); it != document->constEnd(); ++it)
        actualKeys.insert(it.key());
    const std::optional<QByteArray> canonical = canonicalValue(QJsonValue(*document));
    if (actualKeys != expectedKeys || !canonical || *canonical + '\n' != bytes)
        return std::nullopt;

    CompilerLedger result{
        document->value(QStringLiteral("configuration_ids")).toObject(),
        document->value(QStringLiteral("operations")).toObject(),
        document->value(QStringLiteral("signer_responses")).toObject(),
    };
    for (auto it = result.configurationIds.constBegin();
         it != result.configurationIds.constEnd();
         ++it) {
        if (it.key().isEmpty() || !it.value().isString() || it.value().toString().isEmpty())
            return std::nullopt;
    }
    for (auto it = result.operations.constBegin(); it != result.operations.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isObject())
            return std::nullopt;
    }
    for (auto it = result.signerResponses.constBegin();
         it != result.signerResponses.constEnd();
         ++it) {
        if (it.key().isEmpty() || !it.value().isObject())
            return std::nullopt;
    }
    return result;
}

QByteArray digestObject(const QJsonValue &value)
{
    const auto canonical = canonicalValue(value);
    return canonical ? sha256(*canonical + '\n') : QByteArray();
}

std::optional<QByteArray> effectiveCompanion(
    const QJsonObject &request,
    const QByteArray &configurationId,
    const QByteArray &compiledProjectSha,
    const QByteArray &intentSha,
    const QByteArray &topologySha,
    const QByteArray &targetSha,
    const QByteArray &adapterSha)
{
    const QJsonObject snapshot = request.value(QStringLiteral("project_snapshot")).toObject();
    const QJsonArray requestDevices = snapshot.value(QStringLiteral("devices")).toArray();
    QList<QByteArray> devices;
    for (const QJsonValue &value : requestDevices) {
        const QJsonObject device = value.toObject();
        const QJsonObject target
            = device.value(QStringLiteral("controller_adapter_target")).toObject();
        const QJsonObject bindingIdentity{
            {QStringLiteral("component_binding_ids"),
             device.value(QStringLiteral("component_binding_ids"))},
            {QStringLiteral("project_device_id"), device.value(QStringLiteral("project_device_id"))},
            {QStringLiteral("semantic_action_binding_ids"),
             device.value(QStringLiteral("semantic_action_binding_ids"))},
            {QStringLiteral("semantic_binding_ids"),
             device.value(QStringLiteral("semantic_binding_ids"))},
        };
        const auto position = canonicalValue(device.value(QStringLiteral("position")));
        const auto station = canonicalValue(device.value(QStringLiteral("station_address")));
        if (!position || !station)
            return std::nullopt;
        devices.append(jsonObject({
            {QStringLiteral("adapter_id"),
             jsonString(target.value(QStringLiteral("adapter_id")).toString())},
            {QStringLiteral("adapter_sha256"),
             jsonString(target.value(QStringLiteral("adapter_sha256")).toString())},
            {QStringLiteral("adapter_version"),
             jsonString(target.value(QStringLiteral("adapter_version")).toString())},
            {QStringLiteral("binding_identity_sha256"),
             jsonString(QString::fromLatin1(digestObject(bindingIdentity)))},
            {QStringLiteral("dc_sha256"),
             jsonString(QString::fromLatin1(digestObject(device.value(QStringLiteral("dc")))))},
            {QStringLiteral("esi_sha256"),
             jsonString(device.value(QStringLiteral("esi_sha256")).toString())},
            {QStringLiteral("identity_sha256"),
             jsonString(QString::fromLatin1(digestObject(device.value(QStringLiteral("identity")))))},
            {QStringLiteral("manual_envelope_sha256"),
             jsonString(
                 QString::fromLatin1(digestObject(device.value(QStringLiteral("manual_envelope")))))},
            {QStringLiteral("module_assignments_sha256"),
             jsonString(
                 QString::fromLatin1(
                     digestObject(device.value(QStringLiteral("module_assignments")))))},
            {QStringLiteral("pdo_selection_sha256"),
             jsonString(QString::fromLatin1(digestObject(device.value(QStringLiteral("pdo")))))},
            {QStringLiteral("position"), *position},
            {QStringLiteral("project_device_id"),
             jsonString(device.value(QStringLiteral("project_device_id")).toString())},
            {QStringLiteral("slave_node_id"),
             jsonString(device.value(QStringLiteral("slave_node_id")).toString())},
            {QStringLiteral("startup_sdo_sha256"),
             jsonString(
                 QString::fromLatin1(digestObject(device.value(QStringLiteral("startup_sdos")))))},
            {QStringLiteral("station_address"), *station},
        }));
    }
    const auto documentRevision = canonicalValue(
        snapshot.value(QStringLiteral("document_revision")));
    const auto master = canonicalValue(snapshot.value(QStringLiteral("master")));
    if (!documentRevision || !master || devices.isEmpty())
        return std::nullopt;
    return canonicalDocument({
        {QStringLiteral("adapter_bundle_sha256"), jsonString(QString::fromLatin1(adapterSha))},
        {QStringLiteral("compiled_project_sha256"),
         jsonString(QString::fromLatin1(compiledProjectSha))},
        {QStringLiteral("configuration_id"), configurationId},
        {QStringLiteral("devices"), jsonArray(devices)},
        {QStringLiteral("document_revision"), *documentRevision},
        {QStringLiteral("format"),
         jsonString(QStringLiteral("ethercat-effective-project-companion-v1"))},
        {QStringLiteral("format_version"), QByteArray("1")},
        {QStringLiteral("intent_sha256"), jsonString(QString::fromLatin1(intentSha))},
        {QStringLiteral("master"), *master},
        {QStringLiteral("master_node_id"),
         jsonString(snapshot.value(QStringLiteral("master_node_id")).toString())},
        {QStringLiteral("project_id"),
         jsonString(snapshot.value(QStringLiteral("project_id")).toString())},
        {QStringLiteral("target_profile_sha256"), jsonString(QString::fromLatin1(targetSha))},
        {QStringLiteral("topology_evidence_sha256"), jsonString(QString::fromLatin1(topologySha))},
    });
}

QByteArray compilerRecord(
    const QByteArray &configurationId,
    const QString &state,
    const QString &outputDirectory,
    const QByteArray &manifestSha,
    const QByteArray &signRequestSha,
    const QByteArray &packageSha = {})
{
    JsonMembers members{
        {QStringLiteral("configuration_id"), configurationId},
        {QStringLiteral("output_dir"), jsonString(outputDirectory)},
        {QStringLiteral("state"), jsonString(state)},
    };
    if (!manifestSha.isEmpty())
        members
            .insert(QStringLiteral("manifest_sha256"), jsonString(QString::fromLatin1(manifestSha)));
    if (!signRequestSha.isEmpty())
        members.insert(
            QStringLiteral("sign_request_sha256"), jsonString(QString::fromLatin1(signRequestSha)));
    if (!packageSha.isEmpty())
        members.insert(QStringLiteral("package_sha256"), jsonString(QString::fromLatin1(packageSha)));
    return jsonObject(members);
}

QByteArray ledgerDocument(
    const QString &operationId,
    const QByteArray &configurationId,
    const QByteArray &record,
    const QByteArray &signerResponse = QByteArray("null"))
{
    return canonicalDocument({
        {QStringLiteral("configuration_ids"),
         jsonObject({{QString::fromLatin1(configurationId), jsonString(operationId)}})},
        {QStringLiteral("format"), jsonString(QStringLiteral("ethercat-ide-compiler-ledger-v1"))},
        {QStringLiteral("format_version"), QByteArray("1")},
        {QStringLiteral("operations"), jsonObject({{operationId, record}})},
        {QStringLiteral("signer_responses"),
         signerResponse == QByteArray("null") ? QByteArray("{}")
                                              : jsonObject({{operationId, signerResponse}})},
    });
}

int compileCommand(const Arguments &arguments)
{
    QByteArray requestBytes;
    if (!readFile(arguments.values.value(QStringLiteral("request")), &requestBytes))
        return fail(failureDocument("ECOMP-TEST-READ", "input", "$.request", "request unreadable"));
    const auto request = objectDocument(requestBytes);
    const auto configurationId = rootValue(requestBytes, QByteArrayView("configuration_id"));
    const auto configurationValue = unsignedRootValue(requestBytes, "configuration_id");
    if (!request || !configurationId || !configurationValue || *configurationValue == 0)
        return fail(failureDocument("ECOMP-TEST-REQUEST", "input", "$.request", "request invalid"));
    const QString operationId = request->value(QStringLiteral("operation_id")).toString();
    const QString ledgerPath = arguments.values.value(QStringLiteral("ledger"));
    appendObservation(
        ledgerPath + QStringLiteral(".calls"),
        QByteArray("compile ") + operationId.toLatin1() + '\n');
    if (operationId.endsWith(QStringLiteral("0002"))) {
        return fail(failureDocument(
            "ECOMP-TEST-DOMAIN",
            "configuration",
            "$.operation_id",
            "requested deterministic domain failure"));
    }
    if (operationId.endsWith(QStringLiteral("0004")))
        QThread::msleep(5000);

    const QString outputDirectory = absolutePath(
        arguments.values.value(QStringLiteral("output-dir")));
    const QJsonObject artifacts = request->value(QStringLiteral("artifacts")).toObject();
    const QJsonObject targetDescriptor
        = artifacts.value(QStringLiteral("target_profile")).toObject();
    const QString targetPath = QDir(arguments.values.value(QStringLiteral("artifact-root")))
                                   .filePath(
                                       targetDescriptor.value(QStringLiteral("path")).toString());
    QByteArray targetBytes;
    if (!readFile(targetPath, &targetBytes)
        || sha256(targetBytes)
               != targetDescriptor.value(QStringLiteral("sha256")).toString().toLatin1()) {
        return fail(failureDocument(
            "ECOMP-TEST-TARGET", "capability", "$.artifacts.target_profile", "target invalid"));
    }
    const auto target = objectDocument(targetBytes);
    const auto projectData = canonicalValue(request->value(QStringLiteral("project_snapshot")));
    if (!target || !projectData)
        return fail(failureDocument("ECOMP-TEST-JSON", "input", "$.request", "JSON invalid"));

    const QByteArray projectDocument = *projectData + '\n';
    const QByteArray compiledProjectSha = sha256(projectDocument);
    const QByteArray intentSha = sha256(projectDocument);
    const QByteArray targetSha
        = targetDescriptor.value(QStringLiteral("sha256")).toString().toLatin1();
    const QByteArray adapterSha = artifacts.value(QStringLiteral("adapter_bundle"))
                                      .toObject()
                                      .value(QStringLiteral("sha256"))
                                      .toString()
                                      .toLatin1();
    const QByteArray topologySha = artifacts.value(QStringLiteral("topology_evidence"))
                                       .toObject()
                                       .value(QStringLiteral("sha256"))
                                       .toString()
                                       .toLatin1();
    const auto companion = effectiveCompanion(
        *request,
        *configurationId,
        compiledProjectSha,
        intentSha,
        topologySha,
        targetSha,
        adapterSha);
    if (!companion)
        return fail(failureDocument(
            "ECOMP-TEST-COMPANION", "configuration", "$.project_snapshot", "companion invalid"));
    const QByteArray companionSha = sha256(*companion);
    const QByteArray report = canonicalDocument({
        {QStringLiteral("format"), jsonString(QStringLiteral("fake-api042-compile-report-v1"))},
        {QStringLiteral("ide_project_compiler_contract"),
         jsonObject({
             {QStringLiteral("adapter_bundle_sha256"), jsonString(QString::fromLatin1(adapterSha))},
             {QStringLiteral("effective_project_companion"), QByteArray(*companion).chopped(1)},
             {QStringLiteral("effective_project_companion_sha256"),
              jsonString(QString::fromLatin1(companionSha))},
             {QStringLiteral("format"),
              jsonString(QStringLiteral("ethercat-ide-project-compiler-evidence-v1"))},
             {QStringLiteral("intent_sha256"), jsonString(QString::fromLatin1(intentSha))},
             {QStringLiteral("target_profile_sha256"), jsonString(QString::fromLatin1(targetSha))},
             {QStringLiteral("topology_evidence_sha256"),
              jsonString(QString::fromLatin1(topologySha))},
         })},
    });
    const QByteArray reportSha = sha256(report);
    const QByteArray manifest = canonicalDocument({
        {QStringLiteral("compile_report_sha256"), jsonString(QString::fromLatin1(reportSha))},
        {QStringLiteral("compiled_project_sha256"),
         jsonString(QString::fromLatin1(compiledProjectSha))},
        {QStringLiteral("configuration_id"), *configurationId},
        {QStringLiteral("effective_project_companion_sha256"),
         jsonString(QString::fromLatin1(companionSha))},
        {QStringLiteral("format"), jsonString(QStringLiteral("fake-api042-manifest-v2"))},
        {QStringLiteral("format_version"), QByteArray("2")},
        {QStringLiteral("operation_id"), jsonString(operationId)},
    });
    const QByteArray manifestSha = sha256(manifest);
    const QByteArray policyRevision
        = QString::number(quint64(target->value(QStringLiteral("policy_revision")).toDouble()))
              .toLatin1();
    const QByteArray signRequest = canonicalDocument({
        {QStringLiteral("effective_project_companion_sha256"),
         jsonString(QString::fromLatin1(companionSha))},
        {QStringLiteral("format"), jsonString(QStringLiteral("ethercat-ecpkg-sign-request-v1"))},
        {QStringLiteral("format_version"), QByteArray("1")},
        {QStringLiteral("intent_sha256"), jsonString(QString::fromLatin1(intentSha))},
        {QStringLiteral("manifest_sha256"), jsonString(QString::fromLatin1(manifestSha))},
        {QStringLiteral("operation_id"), jsonString(operationId)},
        {QStringLiteral("policy_revision"), policyRevision},
        {QStringLiteral("signing_key_id"),
         jsonString(target->value(QStringLiteral("signing_key_id")).toString())},
        {QStringLiteral("target_profile_sha256"), jsonString(QString::fromLatin1(targetSha))},
    });
    const QByteArray signRequestSha = sha256(signRequest);

    if (!writeFile(QDir(outputDirectory).filePath(QStringLiteral("project.json")), projectDocument)
        || !writeFile(
            QDir(outputDirectory).filePath(QStringLiteral("packages/compile_report.json")), report)
        || !writeFile(
            QDir(outputDirectory).filePath(QStringLiteral("effective-project-companion-v1.json")),
            *companion)
        || !writeFile(
            QDir(outputDirectory).filePath(QStringLiteral("signing_stage/manifest.json")), manifest)
        || !writeFile(
            QDir(outputDirectory).filePath(QStringLiteral("sign-request.json")), signRequest)) {
        return fail(
            failureDocument("ECOMP-TEST-WRITE", "internal", "$.output_dir", "output write failed"));
    }
    const QByteArray result = canonicalDocument({
        {QStringLiteral("adapter_bundle_sha256"), jsonString(QString::fromLatin1(adapterSha))},
        {QStringLiteral("compile_report_sha256"), jsonString(QString::fromLatin1(reportSha))},
        {QStringLiteral("compiled_project_sha256"),
         jsonString(QString::fromLatin1(compiledProjectSha))},
        {QStringLiteral("configuration_id"), *configurationId},
        {QStringLiteral("effective_project_companion_sha256"),
         jsonString(QString::fromLatin1(companionSha))},
        {QStringLiteral("format"),
         jsonString(QStringLiteral("ethercat-ide-project-compiler-result-v1"))},
        {QStringLiteral("format_version"), QByteArray("1")},
        {QStringLiteral("intent_sha256"), jsonString(QString::fromLatin1(intentSha))},
        {QStringLiteral("manifest_sha256"), jsonString(QString::fromLatin1(manifestSha))},
        {QStringLiteral("operation_id"), jsonString(operationId)},
        {QStringLiteral("output_dir"), jsonString(outputDirectory)},
        {QStringLiteral("sign_request_sha256"), jsonString(QString::fromLatin1(signRequestSha))},
        {QStringLiteral("status"), jsonString(QStringLiteral("awaiting_signature"))},
        {QStringLiteral("target_profile_sha256"), jsonString(QString::fromLatin1(targetSha))},
    });
    if (!writeFile(QDir(outputDirectory).filePath(QStringLiteral("compile-result.json")), result)) {
        return fail(
            failureDocument("ECOMP-TEST-WRITE", "internal", "$.output_dir", "result write failed"));
    }
    writeFile(
        ledgerPath,
        ledgerDocument(
            operationId,
            *configurationId,
            compilerRecord(
                *configurationId, "prepared", outputDirectory, manifestSha, signRequestSha)));
    return succeed(result, operationId.endsWith(QStringLiteral("0003")));
}

int finalizeCommand(const Arguments &arguments)
{
    QByteArray requestBytes;
    QByteArray responseBytes;
    if (!readFile(arguments.values.value(QStringLiteral("request")), &requestBytes)
        || !readFile(arguments.values.value(QStringLiteral("sign-response")), &responseBytes)) {
        return fail(
            failureDocument("ECOMP-TEST-READ", "signing", "$.sign_response", "input unreadable"));
    }
    const auto request = objectDocument(requestBytes);
    const auto response = objectDocument(responseBytes);
    const auto configurationId = rootValue(requestBytes, QByteArrayView("configuration_id"));
    if (!request || !response || !configurationId)
        return fail(
            failureDocument("ECOMP-TEST-FINALIZE", "signing", "$.sign_response", "input invalid"));
    const QString operationId = request->value(QStringLiteral("operation_id")).toString();
    const QString ledgerPath = arguments.values.value(QStringLiteral("ledger"));
    appendObservation(
        ledgerPath + QStringLiteral(".calls"),
        QByteArray("finalize ") + operationId.toLatin1() + '\n');

    const QString outputDirectory = absolutePath(
        arguments.values.value(QStringLiteral("output-dir")));
    QByteArray signRequestBytes;
    QByteArray manifestBytes;
    QByteArray companionBytes;
    if (!readFile(
            QDir(outputDirectory).filePath(QStringLiteral("sign-request.json")), &signRequestBytes)
        || !readFile(
            QDir(outputDirectory).filePath(QStringLiteral("signing_stage/manifest.json")),
            &manifestBytes)
        || !readFile(
            QDir(outputDirectory).filePath(QStringLiteral("effective-project-companion-v1.json")),
            &companionBytes)) {
        return fail(
            failureDocument("ECOMP-TEST-STATE", "signing", "$.output_dir", "compile state missing"));
    }
    const auto signRequest = objectDocument(signRequestBytes);
    QJsonObject receiptProjection = *response;
    receiptProjection.remove(QStringLiteral("receipt_sha256"));
    const auto canonicalResponse = canonicalValue(*response);
    const auto canonicalReceiptProjection = canonicalValue(receiptProjection);
    if (!signRequest || !canonicalResponse || *canonicalResponse + '\n' != responseBytes
        || !canonicalReceiptProjection
        || response->value(QStringLiteral("operation_id")).toString() != operationId
        || response->value(QStringLiteral("request_sha256")).toString().toLatin1()
               != sha256(signRequestBytes)
        || response->value(QStringLiteral("manifest_sha256")).toString()
               != signRequest->value(QStringLiteral("manifest_sha256")).toString()
        || response->value(QStringLiteral("signing_key_id")).toString()
               != signRequest->value(QStringLiteral("signing_key_id")).toString()
        || response->value(QStringLiteral("policy_revision")).toDouble()
               != signRequest->value(QStringLiteral("policy_revision")).toDouble()
        || response->value(QStringLiteral("receipt_sha256")).toString().toLatin1()
               != sha256(*canonicalReceiptProjection + '\n')) {
        return fail(
            failureDocument("ECOMP-TEST-STALE", "signing", "$.sign_response", "response stale"));
    }
    const QByteArray receiptSha
        = response->value(QStringLiteral("receipt_sha256")).toString().toLatin1();
    const QByteArray manifestSha = sha256(manifestBytes);
    const QString packagePath = absolutePath(arguments.values.value(QStringLiteral("output")));
    const QByteArray package = canonicalDocument({
        {QStringLiteral("adapter_bundle_sha256"),
         jsonString(request->value(QStringLiteral("artifacts"))
                        .toObject()
                        .value(QStringLiteral("adapter_bundle"))
                        .toObject()
                        .value(QStringLiteral("sha256"))
                        .toString())},
        {QStringLiteral("configuration_id"), *configurationId},
        {QStringLiteral("effective_project_companion_sha256"),
         jsonString(QString::fromLatin1(sha256(companionBytes)))},
        {QStringLiteral("format"), jsonString(QStringLiteral("fake-ecpkg-v2"))},
        {QStringLiteral("format_version"), QByteArray("2")},
        {QStringLiteral("intent_sha256"),
         jsonString(signRequest->value(QStringLiteral("intent_sha256")).toString())},
        {QStringLiteral("manifest_sha256"), jsonString(QString::fromLatin1(manifestSha))},
        {QStringLiteral("operation_id"), jsonString(operationId)},
        {QStringLiteral("signing_receipt_sha256"), jsonString(QString::fromLatin1(receiptSha))},
        {QStringLiteral("target_profile_sha256"),
         jsonString(signRequest->value(QStringLiteral("target_profile_sha256")).toString())},
        {QStringLiteral("topology_evidence_sha256"),
         jsonString(request->value(QStringLiteral("artifacts"))
                        .toObject()
                        .value(QStringLiteral("topology_evidence"))
                        .toObject()
                        .value(QStringLiteral("sha256"))
                        .toString())},
    });
    if (!writeFile(packagePath, package))
        return fail(
            failureDocument("ECOMP-TEST-PACKAGE", "internal", "$.output", "package write failed"));
    const QByteArray packageSha = sha256(package);
    const QByteArray result = canonicalDocument({
        {QStringLiteral("configuration_id"), *configurationId},
        {QStringLiteral("format"),
         jsonString(QStringLiteral("ethercat-ide-project-compiler-result-v1"))},
        {QStringLiteral("format_version"), QByteArray("1")},
        {QStringLiteral("manifest_sha256"), jsonString(QString::fromLatin1(manifestSha))},
        {QStringLiteral("operation_id"), jsonString(operationId)},
        {QStringLiteral("package_bytes"), QString::number(package.size()).toLatin1()},
        {QStringLiteral("package_path"), jsonString(packagePath)},
        {QStringLiteral("package_sha256"), jsonString(QString::fromLatin1(packageSha))},
        {QStringLiteral("signing_receipt_sha256"), jsonString(QString::fromLatin1(receiptSha))},
        {QStringLiteral("status"), jsonString(QStringLiteral("complete"))},
    });
    const auto responseCanonical = canonicalValue(*response);
    writeFile(
        ledgerPath,
        ledgerDocument(
            operationId,
            *configurationId,
            compilerRecord(
                *configurationId,
                "finalized",
                outputDirectory,
                manifestSha,
                sha256(signRequestBytes),
                packageSha),
            responseCanonical ? *responseCanonical : QByteArray("null")));
    return succeed(result);
}

int queryCommand(const Arguments &arguments)
{
    const QString operationId = arguments.values.value(QStringLiteral("operation-id"));
    const QString ledgerPath = arguments.values.value(QStringLiteral("ledger"));
    appendObservation(
        ledgerPath + QStringLiteral(".calls"), QByteArray("query ") + operationId.toLatin1() + '\n');
    appendObservation(ledgerPath + QStringLiteral(".query-marker"), operationId.toLatin1() + '\n');
    QByteArray ledgerBytes;
    QByteArray compiler = jsonObject({
        {QStringLiteral("state"), jsonString(QStringLiteral("query_only"))},
    });
    QByteArray signer("null");
    bool hasCompilerRecord = false;
    bool hasSignerResponse = false;
    if (!readFile(ledgerPath, &ledgerBytes)) {
        return fail(failureDocument(
            QStringLiteral("ECOMP-TEST-LEDGER"),
            QStringLiteral("internal"),
            QStringLiteral("$.ledger"),
            QStringLiteral("Compiler ledger is unreadable")));
    }
    const std::optional<CompilerLedger> ledger = exactCompilerLedger(ledgerBytes);
    if (!ledger) {
        return fail(failureDocument(
            QStringLiteral("ECOMP-TEST-LEDGER"),
            QStringLiteral("internal"),
            QStringLiteral("$.ledger"),
            QStringLiteral("Compiler ledger is invalid")));
    }
    const QJsonValue record = ledger->operations.value(operationId);
    if (record.isObject()) {
        const std::optional<QByteArray> encoded = canonicalValue(record);
        if (!encoded) {
            return fail(failureDocument(
                QStringLiteral("ECOMP-TEST-LEDGER"),
                QStringLiteral("internal"),
                QStringLiteral("$.ledger.operations"),
                QStringLiteral("Compiler ledger operation is invalid")));
        }
        compiler = *encoded;
        hasCompilerRecord = true;
    }
    const QJsonValue response = ledger->signerResponses.value(operationId);
    if (response.isObject()) {
        const std::optional<QByteArray> encoded = canonicalValue(response);
        if (!encoded) {
            return fail(failureDocument(
                QStringLiteral("ECOMP-TEST-LEDGER"),
                QStringLiteral("internal"),
                QStringLiteral("$.ledger.signer_responses"),
                QStringLiteral("Compiler ledger signer response is invalid")));
        }
        signer = *encoded;
        hasSignerResponse = true;
    }
    if (!hasCompilerRecord && !hasSignerResponse) {
        const bool hasConfigurationReservation = std::any_of(
            ledger->configurationIds.constBegin(),
            ledger->configurationIds.constEnd(),
            [&operationId](const QJsonValue &value) { return value.toString() == operationId; });
        if (hasConfigurationReservation) {
            return fail(failureDocument(
                QStringLiteral("ECOMP-TEST-LEDGER"),
                QStringLiteral("internal"),
                QStringLiteral("$.ledger.configuration_ids"),
                QStringLiteral("Compiler ledger reservation is incomplete")));
        }
        return fail(failureDocument(
            QStringLiteral("ECOMP-OPERATION-UNKNOWN"),
            QStringLiteral("configuration"),
            QStringLiteral("$.operation_id"),
            QStringLiteral("OperationId is unknown")));
    }
    return succeed(canonicalDocument({
        {QStringLiteral("compiler"), compiler},
        {QStringLiteral("format"),
         jsonString(QStringLiteral("ethercat-ide-compiler-operation-state-v1"))},
        {QStringLiteral("operation_id"), jsonString(operationId)},
        {QStringLiteral("signer_response"), signer},
    }));
}

int verifyCommand(const Arguments &arguments)
{
    QByteArray packageBytes;
    if (!readFile(arguments.values.value(QStringLiteral("package")), &packageBytes))
        return fail(
            failureDocument("ECOMP-TEST-PACKAGE", "input", "$.package", "package unreadable"));
    const auto package = objectDocument(packageBytes);
    const auto configurationId = rootValue(packageBytes, QByteArrayView("configuration_id"));
    if (!package || !configurationId)
        return fail(
            failureDocument("ECOMP-TEST-PACKAGE", "security", "$.package", "package invalid"));
    if (arguments.values.contains(QStringLiteral("companion"))) {
        QByteArray companion;
        if (!readFile(arguments.values.value(QStringLiteral("companion")), &companion)
            || sha256(companion)
                   != package->value(QStringLiteral("effective_project_companion_sha256"))
                          .toString()
                          .toLatin1()) {
            return fail(
                failureDocument("ECOMP-TEST-COMPANION", "security", "$.companion", "companion stale"));
        }
    }
    return succeed(canonicalDocument({
        {QStringLiteral("adapter_bundle_sha256"),
         jsonString(package->value(QStringLiteral("adapter_bundle_sha256")).toString())},
        {QStringLiteral("configuration_id"), *configurationId},
        {QStringLiteral("effective_project_companion_sha256"),
         jsonString(package->value(QStringLiteral("effective_project_companion_sha256")).toString())},
        {QStringLiteral("format"),
         jsonString(QStringLiteral("ethercat-ide-project-compiler-verification-v1"))},
        {QStringLiteral("intent_sha256"),
         jsonString(package->value(QStringLiteral("intent_sha256")).toString())},
        {QStringLiteral("manifest_format_version"), QByteArray("2")},
        {QStringLiteral("package_sha256"), jsonString(QString::fromLatin1(sha256(packageBytes)))},
        {QStringLiteral("status"), jsonString(QStringLiteral("pass"))},
        {QStringLiteral("target_profile_sha256"),
         jsonString(package->value(QStringLiteral("target_profile_sha256")).toString())},
        {QStringLiteral("topology_evidence_sha256"),
         jsonString(package->value(QStringLiteral("topology_evidence_sha256")).toString())},
    }));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QMap<QString, QSet<QString>> required{
        {QStringLiteral("compile"),
         {QStringLiteral("request"),
          QStringLiteral("artifact-root"),
          QStringLiteral("output-dir"),
          QStringLiteral("ledger")}},
        {QStringLiteral("finalize"),
         {QStringLiteral("request"),
          QStringLiteral("output-dir"),
          QStringLiteral("ledger"),
          QStringLiteral("sign-response"),
          QStringLiteral("public-key"),
          QStringLiteral("output")}},
        {QStringLiteral("query"), {QStringLiteral("operation-id"), QStringLiteral("ledger")}},
        {QStringLiteral("verify"), {QStringLiteral("package"), QStringLiteral("public-key")}},
    };
    const QMap<QString, QSet<QString>> optional{
        {QStringLiteral("verify"), {QStringLiteral("companion")}},
    };
    const auto arguments = parseArguments(application.arguments(), required, optional);
    if (!arguments)
        return fail(failureDocument("ECOMP-TEST-ARGV", "input", "$.argv", "arguments invalid"));
    if (arguments->command == QStringLiteral("compile"))
        return compileCommand(*arguments);
    if (arguments->command == QStringLiteral("finalize"))
        return finalizeCommand(*arguments);
    if (arguments->command == QStringLiteral("query"))
        return queryCommand(*arguments);
    return verifyCommand(*arguments);
}
