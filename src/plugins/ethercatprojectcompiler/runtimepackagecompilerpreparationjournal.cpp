// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilerpreparationjournal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <limits>
#include <memory>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace EtherCAT::ProjectCompiler {

namespace {

constexpr qsizetype maximumJournalBytes = 4 * 1024 * 1024;
constexpr qsizetype maximumDetachedResponseBytes = 1024 * 1024;
constexpr qsizetype maximumJournalRecords = 512;
constexpr auto journalName = "preparation-journal-v1.json";
constexpr auto lockName = ".preparation-journal-v1.lock";

const QSet<QString> topLevelKeys{
    QStringLiteral("format"),
    QStringLiteral("format_version"),
    QStringLiteral("sequence"),
    QStringLiteral("records"),
};

const QSet<QString> recordKeys{
    QStringLiteral("activation_operation_id"),
    QStringLiteral("build_timestamp_ns"),
    QStringLiteral("compile_operation_id"),
    QStringLiteral("compile_result_sha256"),
    QStringLiteral("compiler_provider_id"),
    QStringLiteral("configuration_id"),
    QStringLiteral("contract_id"),
    QStringLiteral("contract_version"),
    QStringLiteral("detail"),
    QStringLiteral("detached_signing_response_base64"),
    QStringLiteral("detached_signing_response_sha256"),
    QStringLiteral("finalize_result_sha256"),
    QStringLiteral("package_sha256"),
    QStringLiteral("phase"),
    QStringLiteral("revision"),
    QStringLiteral("schema_bundle_sha256"),
    QStringLiteral("sign_request_sha256"),
    QStringLiteral("start_request_fingerprint"),
    QStringLiteral("verify_operation_id"),
    QStringLiteral("verify_result_sha256"),
};

bool objectHasExactKeys(const QJsonObject &object, const QSet<QString> &expected)
{
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        actual.insert(it.key());
    return actual == expected;
}

QString unsignedString(quint64 value)
{
    return QString::number(value);
}

std::optional<quint64> unsignedValue(const QJsonObject &object, QStringView key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
        return std::nullopt;
    const QString text = value.toString();
    if (text.isEmpty() || (text.size() > 1 && text.startsWith(QLatin1Char('0'))))
        return std::nullopt;
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, 10);
    if (!ok || QString::number(parsed) != text)
        return std::nullopt;
    return parsed;
}

QString shaString(const Data::RuntimePackageCompilerSha256 &sha)
{
    return QString::fromLatin1(sha.value().toHex());
}

std::optional<Data::RuntimePackageCompilerSha256> shaValue(
    const QJsonObject &object, QStringView key, bool allowEmpty)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
        return std::nullopt;
    const QString text = value.toString();
    if (allowEmpty && text.isEmpty())
        return Data::RuntimePackageCompilerSha256{};
    static const QRegularExpression lowerSha(QStringLiteral("^[0-9a-f]{64}$"));
    if (!lowerSha.match(text).hasMatch())
        return std::nullopt;
    Data::RuntimePackageCompilerSha256 result{QByteArray::fromHex(text.toLatin1())};
    return result.isValid() ? std::optional(result) : std::nullopt;
}

QString optionalShaString(const std::optional<Data::RuntimePackageCompilerSha256> &sha)
{
    return sha ? shaString(*sha) : QString{};
}

std::optional<std::optional<Data::RuntimePackageCompilerSha256>> optionalShaValue(
    const QJsonObject &object, QStringView key)
{
    const std::optional<Data::RuntimePackageCompilerSha256> value = shaValue(object, key, true);
    if (!value)
        return std::nullopt;
    if (!value->isValid())
        return std::optional<Data::RuntimePackageCompilerSha256>{};
    return std::optional<Data::RuntimePackageCompilerSha256>{*value};
}

QString phaseString(Core::RuntimePackageCompilerPreparationPhase phase)
{
    using Phase = Core::RuntimePackageCompilerPreparationPhase;
    switch (phase) {
    case Phase::Reserved:
        return QStringLiteral("reserved");
    case Phase::Compiling:
        return QStringLiteral("compiling");
    case Phase::AwaitingDetachedSignature:
        return QStringLiteral("awaiting_detached_signature");
    case Phase::Finalizing:
        return QStringLiteral("finalizing");
    case Phase::Verifying:
        return QStringLiteral("verifying");
    case Phase::AssemblingProof:
        return QStringLiteral("assembling_proof");
    case Phase::Ready:
        return QStringLiteral("ready");
    case Phase::CancelRequested:
        return QStringLiteral("cancel_requested");
    case Phase::ReconciliationRequired:
        return QStringLiteral("reconciliation_required");
    case Phase::Canceled:
        return QStringLiteral("canceled");
    case Phase::Failed:
        return QStringLiteral("failed");
    case Phase::Idle:
        return {};
    }
    return {};
}

std::optional<Core::RuntimePackageCompilerPreparationPhase> phaseValue(QStringView text)
{
    using Phase = Core::RuntimePackageCompilerPreparationPhase;
    if (text == QStringLiteral("reserved"))
        return Phase::Reserved;
    if (text == QStringLiteral("compiling"))
        return Phase::Compiling;
    if (text == QStringLiteral("awaiting_detached_signature"))
        return Phase::AwaitingDetachedSignature;
    if (text == QStringLiteral("finalizing"))
        return Phase::Finalizing;
    if (text == QStringLiteral("verifying"))
        return Phase::Verifying;
    if (text == QStringLiteral("assembling_proof"))
        return Phase::AssemblingProof;
    if (text == QStringLiteral("ready"))
        return Phase::Ready;
    if (text == QStringLiteral("cancel_requested"))
        return Phase::CancelRequested;
    if (text == QStringLiteral("reconciliation_required"))
        return Phase::ReconciliationRequired;
    if (text == QStringLiteral("canceled"))
        return Phase::Canceled;
    if (text == QStringLiteral("failed"))
        return Phase::Failed;
    return std::nullopt;
}

QJsonObject encodeEntry(const RuntimePackageCompilerPreparationJournalEntry &entry)
{
    return {
        {QStringLiteral("activation_operation_id"), entry.activationOperationId.value()},
        {QStringLiteral("build_timestamp_ns"), unsignedString(entry.buildTimestampNs)},
        {QStringLiteral("compile_operation_id"), entry.compileOperationId.value()},
        {QStringLiteral("compile_result_sha256"), optionalShaString(entry.compileResultSha256)},
        {QStringLiteral("compiler_provider_id"), entry.compilerProviderId},
        {QStringLiteral("configuration_id"), unsignedString(entry.configurationId)},
        {QStringLiteral("contract_id"), entry.contractIdentity.contractId},
        {QStringLiteral("contract_version"), int(entry.contractIdentity.contractVersion)},
        {QStringLiteral("detail"), entry.detail},
        {QStringLiteral("detached_signing_response_base64"),
         entry.detachedSigningResponse
             ? QString::fromLatin1(entry.detachedSigningResponse->exactBytes().toBase64())
             : QString{}},
        {QStringLiteral("detached_signing_response_sha256"),
         entry.detachedSigningResponse ? shaString(entry.detachedSigningResponse->sha256())
                                       : QString{}},
        {QStringLiteral("finalize_result_sha256"), optionalShaString(entry.finalizeResultSha256)},
        {QStringLiteral("package_sha256"), optionalShaString(entry.packageSha256)},
        {QStringLiteral("phase"), phaseString(entry.phase)},
        {QStringLiteral("revision"), unsignedString(entry.revision)},
        {QStringLiteral("schema_bundle_sha256"),
         shaString(entry.contractIdentity.schemaBundleSha256)},
        {QStringLiteral("sign_request_sha256"), optionalShaString(entry.signRequestSha256)},
        {QStringLiteral("start_request_fingerprint"), shaString(entry.startRequestFingerprint)},
        {QStringLiteral("verify_operation_id"), entry.verifyOperationId.value()},
        {QStringLiteral("verify_result_sha256"), optionalShaString(entry.verifyResultSha256)},
    };
}

Utils::Result<RuntimePackageCompilerPreparationJournalEntry> decodeEntry(const QJsonValue &value)
{
    if (!value.isObject())
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal record is not an object."));
    const QJsonObject object = value.toObject();
    if (!objectHasExactKeys(object, recordKeys)) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal record fields are invalid."));
    }

    const QList<QString> requiredStringFields{
        QStringLiteral("activation_operation_id"),
        QStringLiteral("build_timestamp_ns"),
        QStringLiteral("compile_operation_id"),
        QStringLiteral("compile_result_sha256"),
        QStringLiteral("compiler_provider_id"),
        QStringLiteral("configuration_id"),
        QStringLiteral("contract_id"),
        QStringLiteral("detail"),
        QStringLiteral("detached_signing_response_base64"),
        QStringLiteral("detached_signing_response_sha256"),
        QStringLiteral("finalize_result_sha256"),
        QStringLiteral("package_sha256"),
        QStringLiteral("phase"),
        QStringLiteral("revision"),
        QStringLiteral("schema_bundle_sha256"),
        QStringLiteral("sign_request_sha256"),
        QStringLiteral("start_request_fingerprint"),
        QStringLiteral("verify_operation_id"),
        QStringLiteral("verify_result_sha256"),
    };
    if (std::any_of(
            requiredStringFields.cbegin(), requiredStringFields.cend(), [&](const QString &key) {
                return !object.value(key).isString();
            })) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal record types are invalid."));
    }
    const QJsonValue contractVersionValue = object.value(QStringLiteral("contract_version"));
    const qint64 contractVersion = contractVersionValue.toInteger(-1);
    if (!contractVersionValue.isDouble() || contractVersion <= 0
        || quint64(contractVersion) > std::numeric_limits<quint32>::max()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal contract version is invalid."));
    }

    const auto fingerprint = shaValue(object, QStringLiteral("start_request_fingerprint"), false);
    const auto schema = shaValue(object, QStringLiteral("schema_bundle_sha256"), false);
    const auto configurationId = unsignedValue(object, QStringLiteral("configuration_id"));
    const auto buildTimestampNs = unsignedValue(object, QStringLiteral("build_timestamp_ns"));
    const auto revision = unsignedValue(object, QStringLiteral("revision"));
    const auto phase = phaseValue(object.value(QStringLiteral("phase")).toString());
    const auto compileResult = optionalShaValue(object, QStringLiteral("compile_result_sha256"));
    const auto signRequest = optionalShaValue(object, QStringLiteral("sign_request_sha256"));
    const auto finalizeResult = optionalShaValue(object, QStringLiteral("finalize_result_sha256"));
    const auto package = optionalShaValue(object, QStringLiteral("package_sha256"));
    const auto verifyResult = optionalShaValue(object, QStringLiteral("verify_result_sha256"));
    if (!fingerprint || !schema || !configurationId || !buildTimestampNs || !revision || !phase
        || !compileResult || !signRequest || !finalizeResult || !package || !verifyResult) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal record values are invalid."));
    }

    const QJsonValue responseBytesValue = object.value(
        QStringLiteral("detached_signing_response_base64"));
    const QJsonValue responseShaValue = object.value(
        QStringLiteral("detached_signing_response_sha256"));
    if (!responseBytesValue.isString() || !responseShaValue.isString()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation detached response fields are invalid."));
    }
    const QString responseBase64 = responseBytesValue.toString();
    const QString responseSha = responseShaValue.toString();
    std::optional<Data::RuntimePackageCompilerCanonicalJson> response;
    if (responseBase64.isEmpty() != responseSha.isEmpty()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation detached response is incomplete."));
    }
    if (!responseBase64.isEmpty()) {
        const QByteArray encoded = responseBase64.toLatin1();
        const QByteArray bytes
            = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (bytes.isEmpty() || bytes.size() > maximumDetachedResponseBytes
            || bytes.toBase64() != encoded) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation detached response encoding is invalid."));
        }
        const auto expectedSha
            = shaValue(object, QStringLiteral("detached_signing_response_sha256"), false);
        Data::RuntimePackageCompilerCanonicalJson canonical
            = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(bytes);
        if (!expectedSha || !canonical.isValid() || canonical.sha256() != *expectedSha) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation detached response digest is invalid."));
        }
        response = std::move(canonical);
    }

    RuntimePackageCompilerPreparationJournalEntry result{
        Data::RuntimePackageCompilerOperationId{
            object.value(QStringLiteral("compile_operation_id")).toString()},
        *fingerprint,
        Data::RuntimePackageCompilerOperationId{
            object.value(QStringLiteral("verify_operation_id")).toString()},
        Data::RuntimePackageActivationOperationId{
            object.value(QStringLiteral("activation_operation_id")).toString()},
        object.value(QStringLiteral("compiler_provider_id")).toString(),
        {object.value(QStringLiteral("contract_id")).toString(), quint32(contractVersion), *schema},
        *configurationId,
        *buildTimestampNs,
        *revision,
        *phase,
        *compileResult,
        *signRequest,
        std::move(response),
        *finalizeResult,
        *package,
        *verifyResult,
        object.value(QStringLiteral("detail")).toString(),
    };
    if (!result.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation journal record is invalid."));
    return result;
}

QByteArray encodeState(const RuntimePackageCompilerPreparationJournalState &state)
{
    QJsonArray records;
    for (const RuntimePackageCompilerPreparationJournalEntry &entry : state.entries)
        records.append(encodeEntry(entry));
    const QJsonObject root{
        {QStringLiteral("format"),
         QStringLiteral("embed-labs-runtime-package-preparation-journal-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("sequence"), unsignedString(state.sequence)},
        {QStringLiteral("records"), records},
    };
    QByteArray result = QJsonDocument(root).toJson(QJsonDocument::Compact);
    result.append('\n');
    return result;
}

Utils::Result<RuntimePackageCompilerPreparationJournalState> decodeState(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > maximumJournalBytes || !bytes.endsWith('\n'))
        return Utils::ResultError(QStringLiteral("Compiler preparation journal size is invalid."));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return Utils::ResultError(QStringLiteral("Compiler preparation journal JSON is invalid."));
    const QJsonObject root = document.object();
    if (!objectHasExactKeys(root, topLevelKeys)
        || root.value(QStringLiteral("format")).toString()
               != QStringLiteral("embed-labs-runtime-package-preparation-journal-v1")
        || root.value(QStringLiteral("format_version")).toInt() != 1
        || !root.value(QStringLiteral("records")).isArray()) {
        return Utils::ResultError(QStringLiteral("Compiler preparation journal header is invalid."));
    }
    const auto sequence = unsignedValue(root, QStringLiteral("sequence"));
    const QJsonArray array = root.value(QStringLiteral("records")).toArray();
    if (!sequence || array.size() > maximumJournalRecords)
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal limits are invalid."));

    RuntimePackageCompilerPreparationJournalState result;
    result.sequence = *sequence;
    result.entries.reserve(array.size());
    for (const QJsonValue &value : array) {
        const Utils::Result<RuntimePackageCompilerPreparationJournalEntry> entry = decodeEntry(
            value);
        if (!entry)
            return Utils::ResultError(entry.error());
        result.entries.append(*entry);
    }
    if (!result.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation journal state is invalid."));
    if (encodeState(result) != bytes) {
        return Utils::ResultError(QStringLiteral("Compiler preparation journal is not canonical."));
    }
    return result;
}

#ifdef Q_OS_UNIX

struct ScopedDescriptor
{
    explicit ScopedDescriptor(int value = -1)
        : descriptor(value)
    {}
    ~ScopedDescriptor()
    {
        if (descriptor >= 0)
            ::close(descriptor);
    }

    ScopedDescriptor(const ScopedDescriptor &) = delete;
    ScopedDescriptor &operator=(const ScopedDescriptor &) = delete;

    int descriptor = -1;
};

struct LockedRoot
{
    ScopedDescriptor root;
    ScopedDescriptor lock;
};

int directoryFlags()
{
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

int noFollowFlag()
{
#ifdef O_NOFOLLOW
    return O_NOFOLLOW;
#else
    return 0;
#endif
}

bool sameTimestampMetadata(const struct stat &left, const struct stat &right)
{
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
}

bool isSafeJournalLeaf(const struct stat &info)
{
    return S_ISREG(info.st_mode) && info.st_uid == ::geteuid() && info.st_nlink == 1
           && (info.st_mode & 0777) == 0600 && info.st_size > 0
           && info.st_size <= maximumJournalBytes;
}

Utils::Result<> syncDescriptor(int descriptor, QStringView description)
{
    int result = -1;
    do {
        result = ::fsync(descriptor);
    } while (result != 0 && errno == EINTR);
    if (result != 0)
        return Utils::ResultError(QStringLiteral("Cannot synchronize %1.").arg(description));
    return Utils::ResultOk;
}

Utils::Result<std::unique_ptr<LockedRoot>> lockRoot(const Utils::FilePath &rootPath, bool create)
{
    if (!rootPath.isAbsolutePath() || !rootPath.scheme().isEmpty()
        || QDir::cleanPath(rootPath.path()) != rootPath.path()
        || rootPath.path() == QStringLiteral("/")) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal root is not a canonical absolute path."));
    }
    if (create && !QDir().mkpath(rootPath.path())) {
        return Utils::ResultError(
            QStringLiteral("Cannot create compiler preparation journal root."));
    }

    struct stat linkInfo = {};
    const QByteArray rootName = QFile::encodeName(rootPath.path());
    if (::lstat(rootName.constData(), &linkInfo) != 0 || !S_ISDIR(linkInfo.st_mode)
        || S_ISLNK(linkInfo.st_mode) || linkInfo.st_uid != ::geteuid()) {
        return Utils::ResultError(QStringLiteral("Compiler preparation journal root is unsafe."));
    }
    if ((linkInfo.st_mode & 0777) != 0700 && ::chmod(rootName.constData(), 0700) != 0) {
        return Utils::ResultError(
            QStringLiteral("Cannot protect compiler preparation journal root."));
    }

    auto result = std::make_unique<LockedRoot>();
    result->root.descriptor = ::open(rootName.constData(), directoryFlags());
    if (result->root.descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot open compiler preparation journal root."));
    struct stat rootInfo = {};
    if (::fstat(result->root.descriptor, &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode)
        || rootInfo.st_dev != linkInfo.st_dev || rootInfo.st_ino != linkInfo.st_ino
        || rootInfo.st_uid != ::geteuid() || (rootInfo.st_mode & 0777) != 0700) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal root changed during validation."));
    }

    result->lock.descriptor = ::openat(
        result->root.descriptor, lockName, O_RDWR | O_CREAT | O_CLOEXEC | noFollowFlag(), 0600);
    if (result->lock.descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot open compiler preparation journal lock."));
    struct stat lockInfo = {};
    if (::fstat(result->lock.descriptor, &lockInfo) != 0 || !S_ISREG(lockInfo.st_mode)
        || lockInfo.st_uid != ::geteuid() || lockInfo.st_nlink != 1
        || (lockInfo.st_mode & 0777) != 0600) {
        return Utils::ResultError(QStringLiteral("Compiler preparation journal lock is unsafe."));
    }
    int locked = -1;
    do {
        locked = ::flock(result->lock.descriptor, LOCK_EX);
    } while (locked != 0 && errno == EINTR);
    if (locked != 0)
        return Utils::ResultError(QStringLiteral("Cannot lock compiler preparation journal."));
    return result;
}

Utils::Result<QByteArray> readJournal(int rootDescriptor, bool allowMissing)
{
    ScopedDescriptor file(
        ::openat(rootDescriptor, journalName, O_RDONLY | O_CLOEXEC | noFollowFlag()));
    if (file.descriptor < 0) {
        if (allowMissing && errno == ENOENT)
            return QByteArray{};
        return Utils::ResultError(QStringLiteral("Cannot open compiler preparation journal."));
    }
    struct stat info = {};
    if (::fstat(file.descriptor, &info) != 0 || !isSafeJournalLeaf(info)) {
        return Utils::ResultError(QStringLiteral("Compiler preparation journal leaf is unsafe."));
    }
    QByteArray bytes(qsizetype(info.st_size), Qt::Uninitialized);
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count
            = ::read(file.descriptor, bytes.data() + offset, size_t(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return Utils::ResultError(QStringLiteral("Cannot read compiler preparation journal."));
        offset += qsizetype(count);
    }
    struct stat after = {};
    if (::fstat(file.descriptor, &after) != 0 || !isSafeJournalLeaf(after)
        || after.st_dev != info.st_dev || after.st_ino != info.st_ino
        || after.st_mode != info.st_mode || after.st_uid != info.st_uid
        || after.st_gid != info.st_gid || after.st_nlink != info.st_nlink
        || after.st_size != info.st_size || !sameTimestampMetadata(after, info)) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal changed while reading."));
    }
    return bytes;
}

Utils::Result<> writeJournal(int rootDescriptor, const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > maximumJournalBytes)
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal write is too large."));
    static std::atomic<quint64> temporarySequence = 0;
    const QByteArray temporaryName = QByteArray(".preparation-journal-v1.tmp.")
                                     + QByteArray::number(::getpid()) + '.'
                                     + QByteArray::number(temporarySequence.fetch_add(1) + 1);
    ScopedDescriptor file(
        ::openat(
            rootDescriptor,
            temporaryName.constData(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | noFollowFlag(),
            0600));
    if (file.descriptor < 0)
        return Utils::ResultError(
            QStringLiteral("Cannot create compiler preparation journal update."));
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count
            = ::write(file.descriptor, bytes.constData() + offset, size_t(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            ::unlinkat(rootDescriptor, temporaryName.constData(), 0);
            return Utils::ResultError(
                QStringLiteral("Cannot write compiler preparation journal update."));
        }
        offset += qsizetype(count);
    }
    if (const Utils::Result<> synced
        = syncDescriptor(file.descriptor, QStringLiteral("compiler preparation journal update"));
        !synced) {
        ::unlinkat(rootDescriptor, temporaryName.constData(), 0);
        return Utils::ResultError(synced.error());
    }
    struct stat info = {};
    if (::fstat(file.descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()
        || info.st_nlink != 1 || (info.st_mode & 0777) != 0600) {
        ::unlinkat(rootDescriptor, temporaryName.constData(), 0);
        return Utils::ResultError(QStringLiteral("Compiler preparation journal update is unsafe."));
    }
    if (::renameat(rootDescriptor, temporaryName.constData(), rootDescriptor, journalName) != 0) {
        ::unlinkat(rootDescriptor, temporaryName.constData(), 0);
        return Utils::ResultError(
            QStringLiteral("Cannot commit compiler preparation journal update."));
    }
    return syncDescriptor(rootDescriptor, QStringLiteral("compiler preparation journal directory"));
}

#else

struct LockedRoot
{
    std::unique_ptr<QLockFile> lock;
};

Utils::Result<std::unique_ptr<LockedRoot>> lockRoot(const Utils::FilePath &rootPath, bool create)
{
    if (!rootPath.isAbsolutePath() || !rootPath.scheme().isEmpty()
        || QDir::cleanPath(rootPath.path()) != rootPath.path()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal root is not a canonical absolute path."));
    }
    if (create && !QDir().mkpath(rootPath.path()))
        return Utils::ResultError(
            QStringLiteral("Cannot create compiler preparation journal root."));
    const QFileInfo rootInfo(rootPath.path());
    if (!rootInfo.isDir() || rootInfo.isSymLink())
        return Utils::ResultError(QStringLiteral("Compiler preparation journal root is unsafe."));
    auto result = std::make_unique<LockedRoot>();
    result->lock = std::make_unique<QLockFile>(
        (rootPath / QString::fromLatin1(lockName)).toFSPathString());
    result->lock->setStaleLockTime(0);
    if (!result->lock->tryLock())
        return Utils::ResultError(QStringLiteral("Cannot lock compiler preparation journal."));
    return result;
}

Utils::Result<QByteArray> readJournal(const Utils::FilePath &root, bool allowMissing)
{
    QFile file((root / QString::fromLatin1(journalName)).toFSPathString());
    if (!file.exists() && allowMissing)
        return QByteArray{};
    if (!file.open(QIODevice::ReadOnly) || file.isSequential() || file.size() <= 0
        || file.size() > maximumJournalBytes) {
        return Utils::ResultError(QStringLiteral("Cannot open compiler preparation journal."));
    }
    return file.readAll();
}

Utils::Result<> writeJournal(const Utils::FilePath &root, const QByteArray &bytes)
{
    QSaveFile file((root / QString::fromLatin1(journalName)).toFSPathString());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return Utils::ResultError(
            QStringLiteral("Cannot commit compiler preparation journal update."));
    return Utils::ResultOk;
}

#endif

Utils::Result<RuntimePackageCompilerPreparationJournalState> loadLocked(
    const Utils::FilePath &root, bool allowMissing, bool create)
{
    const Utils::Result<std::unique_ptr<LockedRoot>> locked = lockRoot(root, create);
    if (!locked)
        return Utils::ResultError(locked.error());
#ifdef Q_OS_UNIX
    const Utils::Result<QByteArray> bytes = readJournal((*locked)->root.descriptor, allowMissing);
#else
    const Utils::Result<QByteArray> bytes = readJournal(root, allowMissing);
#endif
    if (!bytes)
        return Utils::ResultError(bytes.error());
    if (bytes->isEmpty())
        return RuntimePackageCompilerPreparationJournalState{};
    return decodeState(*bytes);
}

Utils::Result<RuntimePackageCompilerPreparationJournalState> commitLocked(
    const Utils::FilePath &root,
    const RuntimePackageCompilerPreparationJournalEntry &entry,
    quint64 expectedSequence,
    const std::optional<RuntimePackageCompilerPreparationJournalEntry> &expectedEntry)
{
    const Utils::Result<std::unique_ptr<LockedRoot>> locked = lockRoot(root, true);
    if (!locked)
        return Utils::ResultError(locked.error());
#ifdef Q_OS_UNIX
    const Utils::Result<QByteArray> bytes = readJournal((*locked)->root.descriptor, true);
#else
    const Utils::Result<QByteArray> bytes = readJournal(root, true);
#endif
    if (!bytes)
        return Utils::ResultError(bytes.error());
    const Utils::Result<RuntimePackageCompilerPreparationJournalState> loaded
        = bytes->isEmpty()
              ? Utils::Result<
                    RuntimePackageCompilerPreparationJournalState>{RuntimePackageCompilerPreparationJournalState{}}
              : decodeState(*bytes);
    if (!loaded)
        return Utils::ResultError(loaded.error());
    RuntimePackageCompilerPreparationJournalState next = *loaded;
    if (next.sequence != expectedSequence
        || expectedSequence == std::numeric_limits<quint64>::max()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal changed concurrently."));
    }
    const auto found = std::find_if(next.entries.begin(), next.entries.end(), [&](const auto &item) {
        return item.compileOperationId == entry.compileOperationId;
    });
    if (expectedEntry) {
        if (found == next.entries.end() || *found != *expectedEntry
            || entry.revision != expectedEntry->revision + 1) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation journal predecessor changed concurrently."));
        }
        const auto preservesOptional = [](const auto &before, const auto &after) {
            return !before || (after && *after == *before);
        };
        if (entry.compileOperationId != found->compileOperationId
            || entry.startRequestFingerprint != found->startRequestFingerprint
            || entry.verifyOperationId != found->verifyOperationId
            || entry.activationOperationId != found->activationOperationId
            || entry.compilerProviderId != found->compilerProviderId
            || entry.contractIdentity != found->contractIdentity
            || entry.configurationId != found->configurationId
            || entry.buildTimestampNs != found->buildTimestampNs
            || !preservesOptional(found->compileResultSha256, entry.compileResultSha256)
            || !preservesOptional(found->signRequestSha256, entry.signRequestSha256)
            || !preservesOptional(found->detachedSigningResponse, entry.detachedSigningResponse)
            || !preservesOptional(found->finalizeResultSha256, entry.finalizeResultSha256)
            || !preservesOptional(found->packageSha256, entry.packageSha256)
            || !preservesOptional(found->verifyResultSha256, entry.verifyResultSha256)) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation durable evidence cannot be replaced."));
        }
        *found = entry;
    } else {
        if (found != next.entries.end() || entry.revision != 1) {
            return Utils::ResultError(
                QStringLiteral("Compiler preparation journal OperationId is already reserved."));
        }
        if (next.entries.size() >= maximumJournalRecords)
            return Utils::ResultError(QStringLiteral("Compiler preparation journal is full."));
        next.entries.append(entry);
    }
    ++next.sequence;
    std::sort(next.entries.begin(), next.entries.end(), [](const auto &left, const auto &right) {
        return left.compileOperationId.value() < right.compileOperationId.value();
    });
    if (!next.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation journal update is invalid."));
    const QByteArray encoded = encodeState(next);
#ifdef Q_OS_UNIX
    const Utils::Result<> written = writeJournal((*locked)->root.descriptor, encoded);
#else
    const Utils::Result<> written = writeJournal(root, encoded);
#endif
    if (!written)
        return Utils::ResultError(written.error());
    return next;
}

} // namespace

bool RuntimePackageCompilerPreparationJournalEntry::isValid() const
{
    static const QRegularExpression providerId(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    if (!compileOperationId.isValid() || !startRequestFingerprint.isValid()
        || !verifyOperationId.isValid() || !activationOperationId.isValid()
        || !providerId.match(compilerProviderId).hasMatch() || !contractIdentity.isValid()
        || configurationId == 0 || buildTimestampNs == 0 || revision == 0
        || phase == Core::RuntimePackageCompilerPreparationPhase::Idle
        || phaseString(phase).isEmpty() || (!detail.isEmpty() && detail != detail.trimmed())) {
        return false;
    }
    const QString &compileId = compileOperationId.value();
    if (compileId == verifyOperationId.value() || compileId == activationOperationId.value()
        || verifyOperationId.value() == activationOperationId.value()) {
        return false;
    }
    const auto validOptionalSha = [](const auto &value) { return !value || value->isValid(); };
    if (!validOptionalSha(compileResultSha256) || !validOptionalSha(signRequestSha256)
        || !validOptionalSha(finalizeResultSha256) || !validOptionalSha(packageSha256)
        || !validOptionalSha(verifyResultSha256) || (signRequestSha256 && !compileResultSha256)
        || (detachedSigningResponse && !signRequestSha256)
        || (detachedSigningResponse && !detachedSigningResponse->isValid())
        || (finalizeResultSha256 && (!compileResultSha256 || !detachedSigningResponse))
        || (packageSha256 && !finalizeResultSha256) || (verifyResultSha256 && !packageSha256)) {
        return false;
    }
    const bool needsDetail
        = phase == Core::RuntimePackageCompilerPreparationPhase::ReconciliationRequired
          || phase == Core::RuntimePackageCompilerPreparationPhase::Canceled
          || phase == Core::RuntimePackageCompilerPreparationPhase::Failed;
    return needsDetail ? !detail.isEmpty() : detail.isEmpty();
}

bool RuntimePackageCompilerPreparationJournalState::isValid() const
{
    if ((sequence == 0) != entries.isEmpty() || entries.size() > maximumJournalRecords)
        return false;
    QString previous;
    for (const RuntimePackageCompilerPreparationJournalEntry &entry : entries) {
        if (!entry.isValid()
            || (!previous.isEmpty() && previous >= entry.compileOperationId.value()))
            return false;
        previous = entry.compileOperationId.value();
    }
    return true;
}

RuntimePackageCompilerPreparationJournal::RuntimePackageCompilerPreparationJournal(
    Utils::FilePath root)
    : m_root(std::move(root))
{}

Utils::Result<RuntimePackageCompilerPreparationJournalState>
RuntimePackageCompilerPreparationJournal::initialize()
{
    const Utils::Result<std::unique_ptr<LockedRoot>> locked = lockRoot(m_root, true);
    if (!locked)
        return Utils::ResultError(locked.error());
#ifdef Q_OS_UNIX
    const Utils::Result<QByteArray> bytes = readJournal((*locked)->root.descriptor, true);
#else
    const Utils::Result<QByteArray> bytes = readJournal(m_root, true);
#endif
    if (!bytes)
        return Utils::ResultError(bytes.error());
    if (!bytes->isEmpty())
        return decodeState(*bytes);
    const RuntimePackageCompilerPreparationJournalState initial;
    const QByteArray encoded = encodeState(initial);
#ifdef Q_OS_UNIX
    const Utils::Result<> written = writeJournal((*locked)->root.descriptor, encoded);
#else
    const Utils::Result<> written = writeJournal(m_root, encoded);
#endif
    if (!written)
        return Utils::ResultError(written.error());
    return initial;
}

Utils::Result<RuntimePackageCompilerPreparationJournalState>
RuntimePackageCompilerPreparationJournal::load() const
{
    return loadLocked(m_root, false, false);
}

Utils::Result<RuntimePackageCompilerPreparationJournalState>
RuntimePackageCompilerPreparationJournal::commit(
    const RuntimePackageCompilerPreparationJournalEntry &entry,
    quint64 expectedSequence,
    std::optional<RuntimePackageCompilerPreparationJournalEntry> expectedEntry)
{
    if (!entry.isValid())
        return Utils::ResultError(QStringLiteral("Compiler preparation journal update is invalid."));
    if (expectedEntry && !expectedEntry->isValid()) {
        return Utils::ResultError(
            QStringLiteral("Compiler preparation journal predecessor is invalid."));
    }
    return commitLocked(m_root, entry, expectedSequence, expectedEntry);
}

Utils::FilePath RuntimePackageCompilerPreparationJournal::root() const
{
    return m_root;
}

Utils::FilePath RuntimePackageCompilerPreparationJournal::journalFile() const
{
    return m_root / QString::fromLatin1(journalName);
}

} // namespace EtherCAT::ProjectCompiler
