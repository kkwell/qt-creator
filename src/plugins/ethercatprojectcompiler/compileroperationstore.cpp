// Copyright (C) 2026 Embed Labs

#include "compileroperationstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#ifndef Q_OS_UNIX
#include <QLockFile>
#include <QSaveFile>
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <limits>
#include <optional>
#include <utility>

#ifdef WITH_TESTS
#include <functional>
#include <mutex>
#endif

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace EtherCAT::ProjectCompiler {

struct CompilerStoreRoot
{
#ifdef Q_OS_UNIX
    ~CompilerStoreRoot()
    {
        if (descriptor >= 0)
            ::close(descriptor);
    }

    int descriptor = -1;
    dev_t device = 0;
    ino_t inode = 0;
#endif
};

struct CompilerStoreLock
{
#ifdef Q_OS_UNIX
    ~CompilerStoreLock()
    {
        if (descriptor >= 0)
            ::close(descriptor);
    }

    bool isLocked() const { return descriptor >= 0; }

    int descriptor = -1;
#else
    bool isLocked() const { return lock && lock->isLocked(); }

    std::unique_ptr<QLockFile> lock;
#endif
};

namespace {

constexpr qsizetype maximumCanonicalBytes = 1024 * 1024;
constexpr qsizetype maximumLedgerBytes = 16 * 1024 * 1024;
constexpr qsizetype maximumPackageBytes = 64 * 1024 * 1024;
constexpr qsizetype maximumProvisionedExecutableBytes = 256 * 1024 * 1024;

QByteArray shaHex(const Data::RuntimePackageCompilerSha256 &sha)
{
    return sha.value().toHex();
}

Data::RuntimePackageCompilerSha256 sha256(const QByteArray &bytes)
{
    return Data::RuntimePackageCompilerSha256(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
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

bool isSafeRelativePath(const QString &relativePath)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]{0,255}$"));
    if (!expression.match(relativePath).hasMatch() || QDir::isAbsolutePath(relativePath)
        || relativePath.contains('\\') || QDir::cleanPath(relativePath) != relativePath) {
        return false;
    }
    const QStringList components = relativePath.split('/');
    return std::none_of(components.cbegin(), components.cend(), [](const QString &component) {
        return component.isEmpty() || component == QStringLiteral(".")
               || component == QStringLiteral("..");
    });
}

bool isProviderPath(const Utils::FilePath &root, const Utils::FilePath &path)
{
    if (!root.isAbsolutePath() || !path.isAbsolutePath() || !root.scheme().isEmpty()
        || !path.scheme().isEmpty()) {
        return false;
    }
    const QString cleanRoot = QDir::cleanPath(root.path());
    const QString cleanPath = QDir::cleanPath(path.path());
    return cleanPath.startsWith(cleanRoot + '/') && cleanPath != cleanRoot;
}

enum class DirectoryPolicy { Private, CompilerOwned };

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

#ifdef WITH_TESTS
std::mutex beforeLeafOpenHookMutex;
std::function<void()> beforeLeafOpenHook;

void invokeBeforeLeafOpenHook()
{
    std::function<void()> hook;
    {
        const std::lock_guard<std::mutex> guard(beforeLeafOpenHookMutex);
        hook = std::move(beforeLeafOpenHook);
        beforeLeafOpenHook = {};
    }
    if (hook)
        hook();
}
#else
void invokeBeforeLeafOpenHook() {}
#endif

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

int directoryOpenFlags()
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

Utils::Result<> validateDirectoryDescriptor(
    int descriptor, DirectoryPolicy policy, QStringView description)
{
    struct stat info = {};
    if (::fstat(descriptor, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid()) {
        return Utils::ResultError(
            QStringLiteral("Compiler %1 is not an owner directory.").arg(description));
    }
    const mode_t mode = info.st_mode & 0777;
    if ((policy == DirectoryPolicy::Private && mode != 0700)
        || (policy == DirectoryPolicy::CompilerOwned && (mode & 0022) != 0)) {
        return Utils::ResultError(
            QStringLiteral("Compiler %1 has unsafe permissions.").arg(description));
    }
    return Utils::ResultOk;
}

Utils::Result<QStringList> relativeComponents(
    const Utils::FilePath &root, const Utils::FilePath &path, bool allowRoot)
{
    if (path == root)
        return allowRoot ? Utils::Result<QStringList>(QStringList{})
                         : Utils::Result<QStringList>(Utils::ResultError(
                               QStringLiteral("Compiler path must name a descendant.")));
    if (!isProviderPath(root, path))
        return Utils::ResultError(QStringLiteral("Compiler path escaped its provider root."));
    const QString rootPath = QDir::cleanPath(root.path());
    const QString cleanPath = QDir::cleanPath(path.path());
    const QString relative = cleanPath.mid(rootPath.size() + 1);
    if (!isSafeRelativePath(relative))
        return Utils::ResultError(QStringLiteral("Compiler path contains an unsafe component."));
    return relative.split('/', Qt::SkipEmptyParts);
}

Utils::Result<std::shared_ptr<CompilerStoreRoot>> openCompilerRoot(
    const Utils::FilePath &path, bool create)
{
    if (!path.isAbsolutePath() || !path.scheme().isEmpty()
        || QDir::cleanPath(path.path()) != path.path() || path.path() == QStringLiteral("/")) {
        return Utils::ResultError(QStringLiteral("Compiler root is not a canonical absolute path."));
    }
    const QStringList components = path.path().split('/', Qt::SkipEmptyParts);
    if (components.isEmpty())
        return Utils::ResultError(QStringLiteral("Compiler root path is empty."));

    ScopedDescriptor current(::open("/", directoryOpenFlags()));
    if (current.descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot open the compiler root anchor."));
    for (qsizetype index = 0; index + 1 < components.size(); ++index) {
        const QByteArray component = QFile::encodeName(components.at(index));
        const int next = ::openat(current.descriptor, component.constData(), directoryOpenFlags());
        if (next < 0)
            return Utils::ResultError(QStringLiteral("Cannot traverse the compiler root parent."));
        ::close(current.descriptor);
        current.descriptor = next;
    }

    const QByteArray leaf = QFile::encodeName(components.constLast());
    if (create && ::mkdirat(current.descriptor, leaf.constData(), 0700) != 0 && errno != EEXIST) {
        return Utils::ResultError(QStringLiteral("Cannot create compiler root."));
    }
    if (create) {
        if (const Utils::Result<> synced
            = syncDescriptor(current.descriptor, QStringLiteral("compiler root parent"));
            !synced) {
            return Utils::ResultError(synced.error());
        }
    }
    const int descriptor = ::openat(current.descriptor, leaf.constData(), directoryOpenFlags());
    if (descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot securely open compiler root."));
    auto root = std::make_shared<CompilerStoreRoot>();
    root->descriptor = descriptor;
    struct stat info = {};
    if (::fstat(root->descriptor, &info) != 0 || !S_ISDIR(info.st_mode)
        || info.st_uid != ::geteuid()) {
        return Utils::ResultError(QStringLiteral("Existing compiler root is unsafe."));
    }
    if ((info.st_mode & 0777) != 0700 && ::fchmod(root->descriptor, 0700) != 0)
        return Utils::ResultError(QStringLiteral("Cannot protect compiler root."));
    if (const Utils::Result<> valid = validateDirectoryDescriptor(
            root->descriptor, DirectoryPolicy::Private, QStringLiteral("root"));
        !valid) {
        return Utils::ResultError(valid.error());
    }
    root->device = info.st_dev;
    root->inode = info.st_ino;
    return root;
}

Utils::Result<> validateRootDescriptor(const std::shared_ptr<CompilerStoreRoot> &root)
{
    if (!root || root->descriptor < 0)
        return Utils::ResultError(QStringLiteral("Compiler root has not been initialized."));
    struct stat info = {};
    if (::fstat(root->descriptor, &info) != 0 || !S_ISDIR(info.st_mode)
        || info.st_dev != root->device || info.st_ino != root->inode || info.st_uid != ::geteuid()
        || (info.st_mode & 0777) != 0700) {
        return Utils::ResultError(QStringLiteral("Compiler root descriptor is no longer trusted."));
    }
    return Utils::ResultOk;
}

Utils::Result<std::shared_ptr<ScopedDescriptor>> openDirectoryBelowRoot(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &rootPath,
    const Utils::FilePath &directory,
    DirectoryPolicy policy,
    bool create)
{
    if (const Utils::Result<> valid = validateRootDescriptor(rootHandle); !valid)
        return Utils::ResultError(valid.error());
    const Utils::Result<QStringList> components = relativeComponents(rootPath, directory, true);
    if (!components)
        return Utils::ResultError(components.error());
    const int duplicated = ::dup(rootHandle->descriptor);
    if (duplicated < 0)
        return Utils::ResultError(QStringLiteral("Cannot duplicate compiler root descriptor."));
    auto current = std::make_shared<ScopedDescriptor>(duplicated);
    for (const QString &componentText : *components) {
        const QByteArray component = QFile::encodeName(componentText);
        if (create && ::mkdirat(current->descriptor, component.constData(), 0700) != 0
            && errno != EEXIST) {
            return Utils::ResultError(QStringLiteral("Cannot create compiler store directory."));
        }
        const int next = ::openat(current->descriptor, component.constData(), directoryOpenFlags());
        if (next < 0)
            return Utils::ResultError(
                QStringLiteral("Cannot securely traverse compiler directory."));
        if (create) {
            struct stat info = {};
            if (::fstat(next, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid()) {
                ::close(next);
                return Utils::ResultError(
                    QStringLiteral("Existing compiler store directory is unsafe."));
            }
            if ((info.st_mode & 0777) != 0700 && ::fchmod(next, 0700) != 0) {
                ::close(next);
                return Utils::ResultError(
                    QStringLiteral("Cannot protect compiler store directory."));
            }
            if (const Utils::Result<> synced
                = syncDescriptor(current->descriptor, QStringLiteral("compiler store directory"));
                !synced) {
                ::close(next);
                return Utils::ResultError(synced.error());
            }
        }
        current = std::make_shared<ScopedDescriptor>(next);
        if (const Utils::Result<> valid = validateDirectoryDescriptor(
                current->descriptor,
                create ? DirectoryPolicy::Private : policy,
                QStringLiteral("directory"));
            !valid) {
            return Utils::ResultError(valid.error());
        }
    }
    return current;
}

Utils::Result<std::shared_ptr<ScopedDescriptor>> openParentDirectory(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &rootPath,
    const Utils::FilePath &path,
    DirectoryPolicy policy)
{
    const Utils::Result<QStringList> components = relativeComponents(rootPath, path, false);
    if (!components || components->isEmpty())
        return Utils::ResultError(
            components ? QStringLiteral("Compiler leaf path is empty.") : components.error());
    return openDirectoryBelowRoot(rootHandle, rootPath, path.parentDir(), policy, false);
}

Utils::Result<QByteArray> readLeafFromParent(
    int parentDescriptor,
    const QString &leafName,
    qsizetype maximumBytes,
    std::optional<mode_t> requiredMode = std::nullopt)
{
    invokeBeforeLeafOpenHook();
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const QByteArray leaf = QFile::encodeName(leafName);
    ScopedDescriptor descriptor(::openat(parentDescriptor, leaf.constData(), flags));
    if (descriptor.descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot securely open compiler evidence."));
    struct stat opened = {};
    if (::fstat(descriptor.descriptor, &opened) != 0 || !S_ISREG(opened.st_mode)
        || opened.st_uid != ::geteuid() || opened.st_nlink != 1 || opened.st_size < 0
        || opened.st_size > maximumBytes || (opened.st_mode & 0022) != 0
        || (requiredMode && (opened.st_mode & 0777) != *requiredMode)) {
        return Utils::ResultError(QStringLiteral("Compiler evidence is not a secure bounded file."));
    }
    QByteArray result(qsizetype(opened.st_size), Qt::Uninitialized);
    qsizetype offset = 0;
    while (offset < result.size()) {
        const ssize_t count
            = ::read(descriptor.descriptor, result.data() + offset, size_t(result.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return Utils::ResultError(QStringLiteral("Cannot completely read compiler evidence."));
        offset += qsizetype(count);
    }
    struct stat after = {};
    if (::fstat(descriptor.descriptor, &after) != 0 || after.st_dev != opened.st_dev
        || after.st_ino != opened.st_ino || after.st_size != opened.st_size) {
        return Utils::ResultError(QStringLiteral("Compiler evidence changed while being read."));
    }
    return result;
}

Utils::Result<bool> regularLeafExists(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &rootPath,
    const Utils::FilePath &path,
    DirectoryPolicy policy = DirectoryPolicy::Private)
{
    const Utils::Result<std::shared_ptr<ScopedDescriptor>> parent
        = openParentDirectory(rootHandle, rootPath, path, policy);
    if (!parent)
        return Utils::ResultError(parent.error());
    struct stat info = {};
    const QByteArray leaf = QFile::encodeName(path.fileName());
    if (::fstatat((*parent)->descriptor, leaf.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT)
            return false;
        return Utils::ResultError(QStringLiteral("Cannot inspect compiler evidence leaf."));
    }
    if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1)
        return Utils::ResultError(QStringLiteral("Compiler evidence leaf is unsafe."));
    return true;
}

#endif // Q_OS_UNIX

Utils::Result<bool> compilerLeafExists(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &path,
    DirectoryPolicy policy = DirectoryPolicy::Private)
{
#ifdef Q_OS_UNIX
    return regularLeafExists(rootHandle, root, path, policy);
#else
    Q_UNUSED(rootHandle)
    Q_UNUSED(root)
    Q_UNUSED(policy)
    const QFileInfo info(path.path());
    if (!info.exists())
        return false;
    if (!info.isFile() || info.isSymLink())
        return Utils::ResultError(QStringLiteral("Compiler evidence leaf is unsafe."));
    return true;
#endif
}

Utils::Result<> validatePrivateDirectory(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &path)
{
    if (!path.isAbsolutePath() || !path.scheme().isEmpty())
        return Utils::ResultError(QStringLiteral("Compiler store path must be absolute and local."));
#ifdef Q_OS_UNIX
    const Utils::Result<std::shared_ptr<ScopedDescriptor>> descriptor
        = openDirectoryBelowRoot(rootHandle, root, path, DirectoryPolicy::Private, false);
    return descriptor ? Utils::ResultOk : Utils::ResultError(descriptor.error());
#else
    Q_UNUSED(rootHandle)
    Q_UNUSED(root)
    const QFileInfo info(path.path());
    if (!info.isDir() || info.isSymLink()) {
        return Utils::ResultError(
            QStringLiteral("Compiler store directory is unsafe: %1").arg(path.toUserOutput()));
    }
    return Utils::ResultOk;
#endif
}

Utils::Result<> validateProviderDirectoryChain(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &directory)
{
#ifdef Q_OS_UNIX
    const Utils::Result<std::shared_ptr<ScopedDescriptor>> descriptor
        = openDirectoryBelowRoot(rootHandle, root, directory, DirectoryPolicy::CompilerOwned, false);
    return descriptor ? Utils::ResultOk : Utils::ResultError(descriptor.error());
#else
    if (directory == root)
        return validatePrivateDirectory(rootHandle, root, root);
    if (!isProviderPath(root, directory))
        return Utils::ResultError(QStringLiteral("Compiler path escaped its provider root."));
    Utils::FilePath current = root;
    if (const Utils::Result<> rootResult = validatePrivateDirectory(rootHandle, root, current);
        !rootResult) {
        return rootResult;
    }
    const QString relative = QDir::cleanPath(directory.path()).mid(root.path().size() + 1);
    for (const QString &component : relative.split('/', Qt::SkipEmptyParts)) {
        current /= component;
        if (const Utils::Result<> result = validatePrivateDirectory(rootHandle, root, current);
            !result) {
            return result;
        }
    }
    return Utils::ResultOk;
#endif
}

Utils::Result<> createPrivateDirectory(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &parent,
    const QString &leaf,
    Utils::FilePath *created)
{
    if (!isSafeRelativePath(leaf) || leaf.contains('/'))
        return Utils::ResultError(QStringLiteral("Unsafe compiler store directory name."));
    const Utils::FilePath path = parent / leaf;
#ifdef Q_OS_UNIX
    const Utils::Result<std::shared_ptr<ScopedDescriptor>> parentDescriptor
        = openDirectoryBelowRoot(rootHandle, root, parent, DirectoryPolicy::Private, false);
    if (!parentDescriptor)
        return Utils::ResultError(parentDescriptor.error());
    const QByteArray nativeLeaf = QFile::encodeName(leaf);
    if (::mkdirat((*parentDescriptor)->descriptor, nativeLeaf.constData(), 0700) != 0
        && errno != EEXIST) {
        return Utils::ResultError(QStringLiteral("Cannot create compiler store directory."));
    }
    const int descriptor
        = ::openat((*parentDescriptor)->descriptor, nativeLeaf.constData(), directoryOpenFlags());
    if (descriptor < 0)
        return Utils::ResultError(QStringLiteral("Cannot securely open compiler store directory."));
    ScopedDescriptor opened(descriptor);
    struct stat info = {};
    if (::fstat(opened.descriptor, &info) != 0 || !S_ISDIR(info.st_mode)
        || info.st_uid != ::geteuid()) {
        return Utils::ResultError(QStringLiteral("Existing compiler store path is unsafe."));
    }
    if ((info.st_mode & 0777) != 0700 && ::fchmod(opened.descriptor, 0700) != 0)
        return Utils::ResultError(QStringLiteral("Cannot protect compiler store directory."));
    if (const Utils::Result<> synced
        = syncDescriptor((*parentDescriptor)->descriptor, QStringLiteral("compiler directory"));
        !synced) {
        return synced;
    }
#else
    Q_UNUSED(rootHandle)
    Q_UNUSED(root)
    if (const Utils::Result<> parentResult = validatePrivateDirectory(rootHandle, root, parent);
        !parentResult) {
        return parentResult;
    }
    if (!QDir(parent.path()).mkdir(leaf) && !path.isDir())
        return Utils::ResultError(QStringLiteral("Cannot create compiler store directory."));
    const Utils::Result<> permissions = path.setPermissions(
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (!permissions)
        return permissions;
#endif
    if (const Utils::Result<> result = validatePrivateDirectory(rootHandle, root, path); !result)
        return result;
    if (created)
        *created = path;
    return Utils::ResultOk;
}

Utils::Result<> createPrivateDirectoryPath(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &start,
    const QString &relativeDirectory,
    Utils::FilePath *created)
{
    if (!relativeDirectory.isEmpty() && !isSafeRelativePath(relativeDirectory))
        return Utils::ResultError(QStringLiteral("Unsafe compiler artifact directory path."));
    Utils::FilePath current = start;
    for (const QString &component : relativeDirectory.split('/', Qt::SkipEmptyParts)) {
        Utils::FilePath next;
        if (const Utils::Result<> result
            = createPrivateDirectory(rootHandle, root, current, component, &next);
            !result) {
            return result;
        }
        current = next;
    }
    if (created)
        *created = current;
    return Utils::ResultOk;
}

Utils::Result<QByteArray> readRegularLeaf(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &path,
    qsizetype maximumBytes,
    DirectoryPolicy policy = DirectoryPolicy::Private,
    std::optional<unsigned int> requiredMode = std::nullopt)
{
    if (!path.isAbsolutePath() || !path.scheme().isEmpty() || maximumBytes < 0)
        return Utils::ResultError(QStringLiteral("Unsafe compiler evidence path."));
#ifdef Q_OS_UNIX
    const Utils::Result<std::shared_ptr<ScopedDescriptor>> parent
        = openParentDirectory(rootHandle, root, path, policy);
    if (!parent)
        return Utils::ResultError(parent.error());
    return readLeafFromParent(
        (*parent)->descriptor,
        path.fileName(),
        maximumBytes,
        requiredMode ? std::optional<mode_t>(mode_t(*requiredMode)) : std::nullopt);
#else
    Q_UNUSED(rootHandle)
    Q_UNUSED(root)
    Q_UNUSED(policy)
    const QFileInfo info(path.path());
    if (!info.isFile() || info.isSymLink() || info.size() < 0 || info.size() > maximumBytes)
        return Utils::ResultError(
            QStringLiteral("Compiler evidence is not a bounded regular file."));
    if (requiredMode) {
        const QFile::Permissions expected
            = (*requiredMode & 0400 ? QFile::ReadOwner : QFile::Permissions{})
              | (*requiredMode & 0200 ? QFile::WriteOwner : QFile::Permissions{})
              | (*requiredMode & 0100 ? QFile::ExeOwner : QFile::Permissions{});
        if (info.permissions() != expected)
            return Utils::ResultError(QStringLiteral("Compiler evidence permissions are unsafe."));
    }
    return path.fileContents(maximumBytes);
#endif
}

Utils::Result<> writeAtomicFile(
    const std::shared_ptr<CompilerStoreRoot> &rootHandle,
    const Utils::FilePath &root,
    const Utils::FilePath &path,
    const QByteArray &bytes,
    bool immutable,
    qsizetype maximumBytes,
    unsigned int fileMode = 0600,
    DirectoryPolicy policy = DirectoryPolicy::Private)
{
    if (bytes.isEmpty() || bytes.size() > maximumBytes)
        return Utils::ResultError(QStringLiteral("Compiler evidence exceeds its size boundary."));
#ifdef Q_OS_UNIX
    const Utils::Result<std::shared_ptr<ScopedDescriptor>> parent
        = openParentDirectory(rootHandle, root, path, policy);
    if (!parent)
        return Utils::ResultError(parent.error());
    const QByteArray leaf = QFile::encodeName(path.fileName());
    struct stat existing = {};
    if (::fstatat((*parent)->descriptor, leaf.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        const Utils::Result<QByteArray> current
            = readLeafFromParent((*parent)->descriptor, path.fileName(), maximumBytes);
        if (!current)
            return Utils::ResultError(current.error());
        if (immutable) {
            if (*current != bytes)
                return Utils::ResultError(
                    QStringLiteral("Immutable compiler evidence already differs."));
            // Preserve the inode and timestamps for an already sealed identical artifact.
            return Utils::ResultOk;
        }
    } else if (errno != ENOENT) {
        return Utils::ResultError(QStringLiteral("Cannot inspect compiler evidence destination."));
    }

    static std::atomic<quint64> temporarySequence{0};
    QByteArray temporaryLeaf;
    ScopedDescriptor temporary;
    for (int attempt = 0; attempt < 64; ++attempt) {
        temporaryLeaf = "." + leaf + "." + QByteArray::number(qulonglong(::getpid())) + "."
                        + QByteArray::number(temporarySequence.fetch_add(1)) + ".tmp";
        int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        temporary.descriptor
            = ::openat((*parent)->descriptor, temporaryLeaf.constData(), flags, mode_t(fileMode));
        if (temporary.descriptor >= 0)
            break;
        if (errno != EEXIST)
            return Utils::ResultError(QStringLiteral("Cannot create compiler evidence atomically."));
    }
    if (temporary.descriptor < 0)
        return Utils::ResultError(
            QStringLiteral("Cannot allocate compiler evidence temporary file."));

    auto removeTemporary = [&] {
        if (!temporaryLeaf.isEmpty())
            ::unlinkat((*parent)->descriptor, temporaryLeaf.constData(), 0);
    };
    if (::fchmod(temporary.descriptor, mode_t(fileMode)) != 0) {
        removeTemporary();
        return Utils::ResultError(QStringLiteral("Cannot protect compiler evidence."));
    }
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(
            temporary.descriptor, bytes.constData() + offset, size_t(bytes.size() - offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            removeTemporary();
            return Utils::ResultError(QStringLiteral("Cannot write protected compiler evidence."));
        }
        offset += qsizetype(count);
    }
    if (const Utils::Result<> synced
        = syncDescriptor(temporary.descriptor, QStringLiteral("compiler evidence"));
        !synced) {
        removeTemporary();
        return synced;
    }
    if (::renameat(
            (*parent)->descriptor, temporaryLeaf.constData(), (*parent)->descriptor, leaf.constData())
        != 0) {
        removeTemporary();
        return Utils::ResultError(QStringLiteral("Cannot commit compiler evidence atomically."));
    }
    temporaryLeaf.clear();
    if (const Utils::Result<> synced
        = syncDescriptor((*parent)->descriptor, QStringLiteral("compiler evidence directory"));
        !synced) {
        return synced;
    }
    return Utils::ResultOk;
#else
    Q_UNUSED(rootHandle)
    Q_UNUSED(root)
    Q_UNUSED(policy)
    if (QFileInfo(path.path()).isSymLink())
        return Utils::ResultError(QStringLiteral("Compiler evidence path is a symbolic link."));
    if (path.exists()) {
        const Utils::Result<QByteArray> current
            = readRegularLeaf(rootHandle, root, path, maximumBytes, policy, fileMode);
        if (!current)
            return Utils::ResultError(current.error());
        if (immutable) {
            if (*current != bytes)
                return Utils::ResultError(
                    QStringLiteral("Immutable compiler evidence already differs."));
            return Utils::ResultOk;
        }
    }
    if (const Utils::Result<> parent = validatePrivateDirectory(rootHandle, root, path.parentDir());
        !parent) {
        return parent;
    }

    // QSaveFile is retained on non-Unix. On Unix it would re-resolve ancestor
    // pathnames, so the dirfd/openat/renameat implementation above is required.
    QSaveFile file(path.path());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return Utils::ResultError(QStringLiteral("Cannot create compiler evidence atomically."));
    QFile::Permissions permissions;
    if (fileMode & 0400)
        permissions |= QFile::ReadOwner;
    if (fileMode & 0200)
        permissions |= QFile::WriteOwner;
    if (fileMode & 0100)
        permissions |= QFile::ExeOwner;
    if (!file.setPermissions(permissions) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return Utils::ResultError(QStringLiteral("Cannot write protected compiler evidence."));
    }
    if (!file.flush() || !file.commit())
        return Utils::ResultError(QStringLiteral("Cannot commit compiler evidence atomically."));
    return path.setPermissions(permissions);
#endif
}

bool skipString(QByteArrayView bytes, qsizetype *offset)
{
    if (*offset >= bytes.size() || bytes[*offset] != '"')
        return false;
    ++*offset;
    while (*offset < bytes.size()) {
        const char character = bytes[(*offset)++];
        if (character == '"')
            return true;
        if (character == '\\') {
            if (*offset >= bytes.size())
                return false;
            if (bytes[(*offset)++] == 'u') {
                if (*offset + 4 > bytes.size())
                    return false;
                *offset += 4;
            }
        }
    }
    return false;
}

bool skipValue(QByteArrayView bytes, qsizetype *offset, int depth = 0)
{
    if (depth > 64 || *offset >= bytes.size())
        return false;
    if (bytes[*offset] == '"')
        return skipString(bytes, offset);
    const char opening = bytes[*offset];
    if (opening == '{' || opening == '[') {
        const char closing = opening == '{' ? '}' : ']';
        ++*offset;
        if (*offset < bytes.size() && bytes[*offset] == closing) {
            ++*offset;
            return true;
        }
        while (*offset < bytes.size()) {
            if (opening == '{') {
                if (!skipString(bytes, offset) || *offset >= bytes.size()
                    || bytes[(*offset)++] != ':') {
                    return false;
                }
            }
            if (!skipValue(bytes, offset, depth + 1))
                return false;
            if (*offset < bytes.size() && bytes[*offset] == closing) {
                ++*offset;
                return true;
            }
            if (*offset >= bytes.size() || bytes[(*offset)++] != ',')
                return false;
        }
        return false;
    }
    while (*offset < bytes.size() && bytes[*offset] != ',' && bytes[*offset] != '}'
           && bytes[*offset] != ']') {
        ++*offset;
    }
    return true;
}

std::optional<QByteArray> rootValue(const QByteArray &canonical, QByteArrayView wantedKey)
{
    if (!canonical.endsWith('\n') || canonical.size() < 3 || canonical.front() != '{')
        return std::nullopt;
    const QByteArrayView bytes(canonical.constData(), canonical.size() - 1);
    qsizetype offset = 1;
    while (offset < bytes.size() && bytes[offset] != '}') {
        const qsizetype keyStart = offset;
        if (!skipString(bytes, &offset) || offset >= bytes.size() || bytes[offset++] != ':')
            return std::nullopt;
        const QByteArrayView encodedKey = bytes.sliced(keyStart, offset - keyStart - 1);
        const qsizetype valueStart = offset;
        if (!skipValue(bytes, &offset))
            return std::nullopt;
        const QByteArray wanted = '"' + wantedKey.toByteArray() + '"';
        if (encodedKey == QByteArrayView(wanted))
            return QByteArray(bytes.sliced(valueStart, offset - valueStart));
        if (offset < bytes.size() && bytes[offset] == '}')
            break;
        if (offset >= bytes.size() || bytes[offset++] != ',')
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<QString> rootString(const QByteArray &canonical, QByteArrayView key)
{
    const std::optional<QByteArray> raw = rootValue(canonical, key);
    if (!raw)
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson('[' + *raw + ']', &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size() != 1 || !document.array().first().isString()) {
        return std::nullopt;
    }
    return document.array().first().toString();
}

std::optional<quint64> rootUnsigned(const QByteArray &canonical, QByteArrayView key)
{
    const std::optional<QByteArray> raw = rootValue(canonical, key);
    if (!raw || raw->isEmpty() || !std::all_of(raw->cbegin(), raw->cend(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        return std::nullopt;
    }
    bool ok = false;
    const quint64 value = raw->toULongLong(&ok);
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

QByteArray compileReservation(
    const Data::RuntimePackageCompilerCompileRequest &request,
    const Data::RuntimePackageCompilerSha256 &requestSha256)
{
    QByteArray result = "{\"build_timestamp_ns\":" + QByteArray::number(request.buildTimestampNs);
    result += ",\"configuration_id\":" + QByteArray::number(request.configurationId);
    result += ",\"contract_id\":" + quotedJsonAscii(request.contractIdentity.contractId);
    result += ",\"contract_version\":"
              + QByteArray::number(request.contractIdentity.contractVersion);
    result += ",\"format\":\"ethercat-ide-compiler-reservation-v1\"";
    result += ",\"operation_id\":" + quotedJsonAscii(request.operationId.value());
    result += ",\"request_sha256\":\"" + shaHex(requestSha256) + "\"";
    result += ",\"schema_bundle_sha256\":\"" + shaHex(request.contractIdentity.schemaBundleSha256)
              + "\"";
    result += ",\"state\":\"reserved\"}\n";
    return result;
}

QByteArray configurationIndex(quint64 configurationId, const QString &operationId)
{
    QByteArray result = "{\"configuration_id\":" + QByteArray::number(configurationId);
    result += ",\"format\":\"ethercat-ide-compiler-configuration-index-v1\"";
    result += ",\"operation_id\":" + quotedJsonAscii(operationId) + "}\n";
    return result;
}

QByteArray verifyReservation(const Data::RuntimePackageCompilerVerifyRequest &request)
{
    QByteArray result = "{\"contract_id\":" + quotedJsonAscii(request.contractIdentity.contractId);
    result += ",\"contract_version\":"
              + QByteArray::number(request.contractIdentity.contractVersion);
    result += ",\"format\":\"ethercat-ide-compiler-verify-reservation-v1\"";
    result += ",\"operation_id\":" + quotedJsonAscii(request.operationId.value());
    result += ",\"package_sha256\":\"" + shaHex(request.packageSha256) + "\"";
    result += ",\"schema_bundle_sha256\":\"" + shaHex(request.contractIdentity.schemaBundleSha256)
              + "\"}\n";
    return result;
}

QString evidenceFileName(CompilerCanonicalEvidenceKind kind)
{
    switch (kind) {
    case CompilerCanonicalEvidenceKind::CompilerRecord:
        return {};
    case CompilerCanonicalEvidenceKind::SignRequest:
        return QStringLiteral("sign-request.json");
    case CompilerCanonicalEvidenceKind::SignResponse:
        return QStringLiteral("sign-response.json");
    case CompilerCanonicalEvidenceKind::QueryResponse:
    case CompilerCanonicalEvidenceKind::VerifyResponse:
        return {};
    }
    return {};
}

struct StoredCompileReservation
{
    QString operationId;
    quint64 configurationId = 0;
    quint64 buildTimestampNs = 0;
    QString contractId;
    quint64 contractVersion = 0;
    QByteArray schemaBundleSha256;
    QByteArray requestSha256;
};

Utils::Result<StoredCompileReservation> parseCompileReservation(const QByteArray &bytes)
{
    StoredCompileReservation reservation;
    const auto format = rootString(bytes, "format");
    const auto state = rootString(bytes, "state");
    const auto operation = rootString(bytes, "operation_id");
    const auto configuration = rootUnsigned(bytes, "configuration_id");
    const auto timestamp = rootUnsigned(bytes, "build_timestamp_ns");
    const auto contractId = rootString(bytes, "contract_id");
    const auto contractVersion = rootUnsigned(bytes, "contract_version");
    const auto schema = rootString(bytes, "schema_bundle_sha256");
    const auto request = rootString(bytes, "request_sha256");
    if (!format || *format != QStringLiteral("ethercat-ide-compiler-reservation-v1") || !state
        || *state != QStringLiteral("reserved") || !operation || !configuration || !timestamp
        || !contractId || !contractVersion || !schema || !request) {
        return Utils::ResultError(QStringLiteral("Compiler reservation is malformed."));
    }
    reservation.operationId = *operation;
    reservation.configurationId = *configuration;
    reservation.buildTimestampNs = *timestamp;
    reservation.contractId = *contractId;
    reservation.contractVersion = *contractVersion;
    reservation.schemaBundleSha256 = schema->toLatin1();
    reservation.requestSha256 = request->toLatin1();
    const Data::RuntimePackageCompilerOperationId operationId(reservation.operationId);
    const Data::RuntimePackageCompilerSha256 schemaSha(
        QByteArray::fromHex(reservation.schemaBundleSha256));
    const Data::RuntimePackageCompilerSha256 requestSha(
        QByteArray::fromHex(reservation.requestSha256));
    const Data::RuntimePackageCompilerContractIdentity contract{
        reservation.contractId,
        reservation.contractVersion <= std::numeric_limits<quint32>::max()
            ? quint32(reservation.contractVersion)
            : 0,
        schemaSha,
    };
    static const QRegularExpression shaExpression(QStringLiteral("^[0-9a-f]{64}$"));
    QByteArray exact = "{\"build_timestamp_ns\":"
                       + QByteArray::number(reservation.buildTimestampNs);
    exact += ",\"configuration_id\":" + QByteArray::number(reservation.configurationId);
    exact += ",\"contract_id\":" + quotedJsonAscii(reservation.contractId);
    exact += ",\"contract_version\":" + QByteArray::number(reservation.contractVersion);
    exact += ",\"format\":\"ethercat-ide-compiler-reservation-v1\"";
    exact += ",\"operation_id\":" + quotedJsonAscii(reservation.operationId);
    exact += ",\"request_sha256\":\"" + reservation.requestSha256 + "\"";
    exact += ",\"schema_bundle_sha256\":\"" + reservation.schemaBundleSha256 + "\"";
    exact += ",\"state\":\"reserved\"}\n";
    if (!operationId.isValid() || !contract.isValid() || !requestSha.isValid()
        || reservation.configurationId == 0 || reservation.buildTimestampNs == 0
        || !shaExpression.match(QString::fromLatin1(reservation.schemaBundleSha256)).hasMatch()
        || !shaExpression.match(QString::fromLatin1(reservation.requestSha256)).hasMatch()
        || exact != bytes) {
        return Utils::ResultError(QStringLiteral("Compiler reservation is not exact canonical v1."));
    }
    return reservation;
}

} // namespace

CompilerOperationLease::CompilerOperationLease() = default;
CompilerOperationLease::~CompilerOperationLease() = default;
CompilerOperationLease::CompilerOperationLease(CompilerOperationLease &&) noexcept = default;
CompilerOperationLease &CompilerOperationLease::operator=(CompilerOperationLease &&) noexcept
    = default;

CompilerOperationLease::CompilerOperationLease(
    std::unique_ptr<CompilerStoreLock> lock, QString storeIdentity)
    : m_lock(std::move(lock))
    , m_storeIdentity(std::move(storeIdentity))
{}

bool CompilerOperationLease::isValid() const
{
    return m_lock && m_lock->isLocked();
}

CompilerOperationStore::CompilerOperationStore(Utils::FilePath compilerRoot)
    : m_compilerRoot(std::move(compilerRoot))
    , m_storeIdentity(QDir::cleanPath(m_compilerRoot.path()))
{}

Utils::Result<> CompilerOperationStore::initialize()
{
#ifdef Q_OS_UNIX
    // Resolve platform aliases such as macOS /var once, before establishing the
    // persistent trusted root descriptor. Descendants never use path resolution.
    const QString canonicalParentPath
        = QFileInfo(m_compilerRoot.parentDir().path()).canonicalFilePath();
    if (canonicalParentPath.isEmpty())
        return Utils::ResultError(QStringLiteral("Compiler root parent cannot be canonicalized."));
    m_compilerRoot = Utils::FilePath::fromString(canonicalParentPath) / m_compilerRoot.fileName();
    m_storeIdentity = QDir::cleanPath(m_compilerRoot.path());
#endif
    if (!m_compilerRoot.isAbsolutePath() || !m_compilerRoot.scheme().isEmpty()
        || QDir::cleanPath(m_compilerRoot.path()) != m_compilerRoot.path()) {
        return Utils::ResultError(QStringLiteral("Compiler root is not a canonical absolute path."));
    }
#ifdef Q_OS_UNIX
    const Utils::Result<std::shared_ptr<CompilerStoreRoot>> root
        = openCompilerRoot(m_compilerRoot, true);
    if (!root)
        return Utils::ResultError(root.error());
    m_rootHandle = *root;
#else
    const Utils::FilePath parent = m_compilerRoot.parentDir();
    if (!m_compilerRoot.exists()) {
        if (!parent.isDir() || parent.isSymLink())
            return Utils::ResultError(QStringLiteral("Compiler root parent is unsafe."));
        if (!QDir(parent.path()).mkdir(m_compilerRoot.fileName()))
            return Utils::ResultError(QStringLiteral("Cannot create compiler root."));
    }
    m_rootHandle = std::make_shared<CompilerStoreRoot>();
#endif
    if (const Utils::Result<> validRoot
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, m_compilerRoot);
        !validRoot) {
        return validRoot;
    }
    Utils::FilePath ignored;
    for (const QString &directory :
         {QStringLiteral("operations"),
          QStringLiteral("configurations"),
          QStringLiteral("provisioned")}) {
        if (const Utils::Result<> result = createPrivateDirectory(
                m_rootHandle, m_compilerRoot, m_compilerRoot, directory, &ignored);
            !result) {
            return result;
        }
    }
    const Utils::Result<CompilerOperationLease> lease = acquireLease();
    if (!lease)
        return Utils::ResultError(lease.error());
    const QByteArray emptyLedger
        = "{\"configuration_ids\":{},\"format\":\"ethercat-ide-compiler-ledger-v1\","
          "\"format_version\":1,\"operations\":{},\"signer_responses\":{}}\n";
    const Utils::Result<bool> ledgerExists
        = compilerLeafExists(m_rootHandle, m_compilerRoot, compilerLedger());
    if (!ledgerExists)
        return Utils::ResultError(ledgerExists.error());
    if (!*ledgerExists) {
        return writeAtomicFile(
            m_rootHandle, m_compilerRoot, compilerLedger(), emptyLedger, true, maximumLedgerBytes);
    }
    const Utils::Result<QByteArray> ledger
        = readRegularLeaf(m_rootHandle, m_compilerRoot, compilerLedger(), maximumLedgerBytes);
    if (!ledger)
        return Utils::ResultError(ledger.error());
    const Data::RuntimePackageCompilerCanonicalJson canonical
        = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(*ledger);
    if (!canonical.isValid()
        || rootString(*ledger, "format") != QStringLiteral("ethercat-ide-compiler-ledger-v1")
        || rootUnsigned(*ledger, "format_version") != 1) {
        return Utils::ResultError(QStringLiteral("Compiler ledger is malformed or noncanonical."));
    }
    return sealCompilerLedger(*lease);
}

Utils::Result<CompilerOperationLease> CompilerOperationStore::acquireLease() const
{
    if (const Utils::Result<> store = validateStore(); !store)
        return Utils::ResultError(store.error());
#ifdef Q_OS_UNIX
    auto lock = std::make_unique<CompilerStoreLock>();
    int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    lock->descriptor = ::openat(m_rootHandle->descriptor, ".compiler.lock", flags, 0600);
    struct stat info = {};
    if (lock->descriptor < 0 || ::fstat(lock->descriptor, &info) != 0 || !S_ISREG(info.st_mode)
        || info.st_uid != ::geteuid() || info.st_nlink != 1
        || ((info.st_mode & 0777) != 0600 && ::fchmod(lock->descriptor, 0600) != 0)) {
        return Utils::ResultError(QStringLiteral("Compiler operation lock is unsafe."));
    }
    if (::flock(lock->descriptor, LOCK_EX | LOCK_NB) != 0)
        return Utils::ResultError(QStringLiteral("Compiler operation store is busy."));
#else
    auto lock = std::make_unique<CompilerStoreLock>();
    lock->lock = std::make_unique<QLockFile>((m_compilerRoot / ".compiler.lock").path());
    lock->lock->setStaleLockTime(0);
    if (!lock->lock->tryLock())
        return Utils::ResultError(QStringLiteral("Compiler operation store is busy."));
#endif
    return CompilerOperationLease(std::move(lock), m_storeIdentity);
}

Utils::Result<CompilerOperationPaths> CompilerOperationStore::reserveCompile(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerCompileRequest &request,
    const Data::RuntimePackageCompilerCanonicalJson &canonicalRequest)
{
    if (!ownsLease(lease) || !request.isValid() || !canonicalRequest.isValid())
        return Utils::ResultError(QStringLiteral("Invalid compiler reservation request or lease."));
    if (rootString(canonicalRequest.exactBytes(), "operation_id") != request.operationId.value()
        || rootUnsigned(canonicalRequest.exactBytes(), "configuration_id") != request.configurationId
        || rootUnsigned(canonicalRequest.exactBytes(), "build_timestamp_ns")
               != request.buildTimestampNs) {
        return Utils::ResultError(
            QStringLiteral("Canonical compile request does not match typed input."));
    }
    if (const Utils::Result<> store = validateStore(); !store)
        return Utils::ResultError(store.error());

    const CompilerOperationPaths operationPaths = paths(request.operationId);
    const QByteArray reservationBytes = compileReservation(request, canonicalRequest.sha256());
    const Utils::FilePath operations = m_compilerRoot / "operations";
    Utils::FilePath ignored;
    if (const Utils::Result<> result = createPrivateDirectory(
            m_rootHandle, m_compilerRoot, operations, request.operationId.value(), &ignored);
        !result) {
        return Utils::ResultError(result.error());
    }
    const Utils::Result<bool> verifyExists
        = compilerLeafExists(m_rootHandle, m_compilerRoot, operationPaths.verifyRequest);
    if (!verifyExists)
        return Utils::ResultError(verifyExists.error());
    if (*verifyExists)
        return Utils::ResultError(
            QStringLiteral("Operation ID is already reserved for verification."));

    const Utils::FilePath configurations = m_compilerRoot / "configurations";
    const QString configurationName = QString::number(request.configurationId);
    Utils::FilePath configurationRoot;
    if (const Utils::Result<> result = createPrivateDirectory(
            m_rootHandle, m_compilerRoot, configurations, configurationName, &configurationRoot);
        !result) {
        return Utils::ResultError(result.error());
    }
    const Utils::FilePath configurationReservation = configurationRoot / "reservation.json";
    const Utils::Result<bool> configurationExists
        = compilerLeafExists(m_rootHandle, m_compilerRoot, configurationReservation);
    if (!configurationExists)
        return Utils::ResultError(configurationExists.error());
    if (*configurationExists) {
        const Utils::Result<QByteArray> existing = readRegularLeaf(
            m_rootHandle, m_compilerRoot, configurationReservation, maximumCanonicalBytes);
        if (!existing)
            return Utils::ResultError(existing.error());
        if (*existing != configurationIndex(request.configurationId, request.operationId.value()))
            return Utils::ResultError(QStringLiteral("Configuration ID is already reserved."));
    }

    for (const QString &directory :
         {QStringLiteral("artifacts"), QStringLiteral("output"), QStringLiteral("evidence")}) {
        if (const Utils::Result<> result = createPrivateDirectory(
                m_rootHandle, m_compilerRoot, operationPaths.operationRoot, directory, &ignored);
            !result) {
            return Utils::ResultError(result.error());
        }
    }
    for (const QString &directory :
         {QStringLiteral("inputs"), QStringLiteral("packages"), QStringLiteral("signing_stage")}) {
        if (const Utils::Result<> result = createPrivateDirectory(
                m_rootHandle, m_compilerRoot, operationPaths.outputDir, directory, &ignored);
            !result) {
            return Utils::ResultError(result.error());
        }
    }
    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            operationPaths.operationRoot / "reservation.json",
            reservationBytes,
            true,
            maximumCanonicalBytes);
        !written) {
        return Utils::ResultError(written.error());
    }

    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            configurationReservation,
            configurationIndex(request.configurationId, request.operationId.value()),
            true,
            maximumCanonicalBytes);
        !written) {
        return Utils::ResultError(written.error());
    }
    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            operationPaths.compileRequest,
            canonicalRequest.exactBytes(),
            true,
            maximumCanonicalBytes);
        !written) {
        return Utils::ResultError(written.error());
    }
    if (const Utils::Result<> materialized
        = materializeCompileArtifacts(lease, request, operationPaths);
        !materialized) {
        return Utils::ResultError(materialized.error());
    }
    return operationPaths;
}

Utils::Result<CompilerOperationPaths> CompilerOperationStore::reserveVerify(
    const CompilerOperationLease &lease, const Data::RuntimePackageCompilerVerifyRequest &request)
{
    if (!ownsLease(lease) || !request.isValid())
        return Utils::ResultError(QStringLiteral("Invalid compiler verification reservation."));
    const CompilerOperationPaths operationPaths = paths(request.operationId);
    Utils::FilePath ignored;
    if (const Utils::Result<> result = createPrivateDirectory(
            m_rootHandle,
            m_compilerRoot,
            m_compilerRoot / "operations",
            request.operationId.value(),
            &ignored);
        !result) {
        return Utils::ResultError(result.error());
    }
    const Utils::Result<bool> compileReservationExists = compilerLeafExists(
        m_rootHandle, m_compilerRoot, operationPaths.operationRoot / "reservation.json");
    if (!compileReservationExists)
        return Utils::ResultError(compileReservationExists.error());
    if (*compileReservationExists) {
        return Utils::ResultError(
            QStringLiteral("Operation ID is already reserved for compilation."));
    }
    for (const QString &directory :
         {QStringLiteral("artifacts"), QStringLiteral("output"), QStringLiteral("evidence")}) {
        if (const Utils::Result<> result = createPrivateDirectory(
                m_rootHandle, m_compilerRoot, operationPaths.operationRoot, directory, &ignored);
            !result) {
            return Utils::ResultError(result.error());
        }
    }
    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            operationPaths.verifyRequest,
            verifyReservation(request),
            true,
            maximumCanonicalBytes);
        !written) {
        return Utils::ResultError(written.error());
    }
    if (const Utils::Result<Utils::FilePath> packageResult
        = persistPackage(lease, request.operationId, request.packageBytes, request.packageSha256);
        !packageResult) {
        return Utils::ResultError(packageResult.error());
    }
    return operationPaths;
}

Utils::Result<CompilerOperationPaths> CompilerOperationStore::validateFinalize(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerFinalizeRequest &request) const
{
    if (!ownsLease(lease) || !request.isValid())
        return Utils::ResultError(QStringLiteral("Invalid compiler finalize request or lease."));
    const CompilerOperationPaths operationPaths = paths(request.operationId);
    if (const Utils::Result<> store = validateStore(); !store)
        return Utils::ResultError(store.error());
    if (const Utils::Result<> operationDirectory
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, operationPaths.operationRoot);
        !operationDirectory) {
        return Utils::ResultError(operationDirectory.error());
    }
    const Utils::Result<QByteArray> reservationBytes = readRegularLeaf(
        m_rootHandle,
        m_compilerRoot,
        operationPaths.operationRoot / "reservation.json",
        maximumCanonicalBytes);
    if (!reservationBytes)
        return Utils::ResultError(reservationBytes.error());
    const Utils::Result<StoredCompileReservation> reservation = parseCompileReservation(
        *reservationBytes);
    if (!reservation)
        return Utils::ResultError(reservation.error());
    if (reservation->operationId != request.operationId.value()
        || reservation->configurationId != request.configurationId
        || reservation->contractId != request.contractIdentity.contractId
        || reservation->contractVersion != request.contractIdentity.contractVersion
        || reservation->schemaBundleSha256 != shaHex(request.contractIdentity.schemaBundleSha256)
        || reservation->requestSha256 != shaHex(request.compileRequestSha256)) {
        return Utils::ResultError(
            QStringLiteral("Finalize request does not match its reservation."));
    }
    const Utils::FilePath configurationRoot = m_compilerRoot / "configurations"
                                              / QString::number(request.configurationId);
    if (const Utils::Result<> configurationDirectory
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, configurationRoot);
        !configurationDirectory) {
        return Utils::ResultError(configurationDirectory.error());
    }
    const Utils::Result<QByteArray> configurationReservation = readRegularLeaf(
        m_rootHandle, m_compilerRoot, configurationRoot / "reservation.json", maximumCanonicalBytes);
    if (!configurationReservation
        || *configurationReservation
               != configurationIndex(request.configurationId, request.operationId.value())) {
        return Utils::ResultError(QStringLiteral("Finalize configuration index is inconsistent."));
    }
    const Utils::Result<QByteArray> compileBytes = readRegularLeaf(
        m_rootHandle, m_compilerRoot, operationPaths.compileRequest, maximumCanonicalBytes);
    const Utils::Result<QByteArray> signBytes = readRegularLeaf(
        m_rootHandle, m_compilerRoot, operationPaths.signRequest, maximumCanonicalBytes);
    const Utils::Result<QByteArray> publicKeyBytes
        = readRegularLeaf(m_rootHandle, m_compilerRoot, operationPaths.publicKey, 32);
    if (!compileBytes || !signBytes || !publicKeyBytes)
        return Utils::ResultError(QStringLiteral("Finalize evidence is incomplete or unsafe."));
    if (sha256(*compileBytes) != request.compileRequestSha256
        || sha256(*signBytes) != request.signRequestSha256
        || sha256(*publicKeyBytes) != request.signingKeyIdSha256 || publicKeyBytes->size() != 32
        || rootString(*signBytes, "operation_id") != request.operationId.value()
        || rootString(*signBytes, "manifest_sha256")
               != QString::fromLatin1(shaHex(request.manifestSha256))
        || rootString(*signBytes, "signing_key_id")
               != QString::fromLatin1(shaHex(request.signingKeyIdSha256))
        || rootUnsigned(*signBytes, "policy_revision") != request.signingPolicyRevision) {
        return Utils::ResultError(QStringLiteral("Finalize evidence is stale or mismatched."));
    }
    return operationPaths;
}

Utils::Result<CompilerOperationPaths> CompilerOperationStore::validateQuery(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerQueryRequest &request) const
{
    if (!ownsLease(lease) || !request.isValid())
        return Utils::ResultError(QStringLiteral("Invalid compiler query request or lease."));
    const CompilerOperationPaths operationPaths = paths(request.operationId);
    if (const Utils::Result<> store = validateStore(); !store)
        return Utils::ResultError(store.error());
    if (const Utils::Result<> operationDirectory
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, operationPaths.operationRoot);
        !operationDirectory) {
        return Utils::ResultError(operationDirectory.error());
    }
    const Utils::Result<QByteArray> reservationBytes = readRegularLeaf(
        m_rootHandle,
        m_compilerRoot,
        operationPaths.operationRoot / "reservation.json",
        maximumCanonicalBytes);
    const Utils::Result<QByteArray> compileBytes = readRegularLeaf(
        m_rootHandle, m_compilerRoot, operationPaths.compileRequest, maximumCanonicalBytes);
    if (!reservationBytes || !compileBytes)
        return Utils::ResultError(QStringLiteral("Compiler query reservation is incomplete."));
    const Utils::Result<StoredCompileReservation> reservation = parseCompileReservation(
        *reservationBytes);
    if (!reservation)
        return Utils::ResultError(reservation.error());
    if (reservation->operationId != request.operationId.value()
        || reservation->contractId != request.contractIdentity.contractId
        || reservation->contractVersion != request.contractIdentity.contractVersion
        || reservation->schemaBundleSha256 != shaHex(request.contractIdentity.schemaBundleSha256)
        || reservation->requestSha256 != shaHex(request.compileRequestSha256)
        || sha256(*compileBytes) != request.compileRequestSha256) {
        return Utils::ResultError(QStringLiteral("Compiler query does not match its reservation."));
    }
    const Utils::FilePath configurationRoot = m_compilerRoot / "configurations"
                                              / QString::number(reservation->configurationId);
    if (const Utils::Result<> configurationDirectory
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, configurationRoot);
        !configurationDirectory) {
        return Utils::ResultError(configurationDirectory.error());
    }
    const Utils::Result<QByteArray> configurationReservation = readRegularLeaf(
        m_rootHandle, m_compilerRoot, configurationRoot / "reservation.json", maximumCanonicalBytes);
    if (!configurationReservation
        || *configurationReservation
               != configurationIndex(reservation->configurationId, request.operationId.value())) {
        return Utils::ResultError(
            QStringLiteral("Compiler query configuration index is inconsistent."));
    }
    return operationPaths;
}

Utils::Result<Utils::FilePath> CompilerOperationStore::persistCanonicalResponse(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerOperationId &operationId,
    CompilerCanonicalEvidenceKind kind,
    const Data::RuntimePackageCompilerCanonicalJson &response)
{
    if (!ownsLease(lease) || !operationId.isValid() || !response.isValid()
        || response.exactBytes().size() > maximumCanonicalBytes) {
        return Utils::ResultError(QStringLiteral("Invalid canonical compiler evidence."));
    }
    const CompilerOperationPaths operationPaths = paths(operationId);
    if (const Utils::Result<> directory
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, operationPaths.operationRoot);
        !directory) {
        return Utils::ResultError(directory.error());
    }
    const Utils::FilePath contentAddressed = operationPaths.operationRoot / "evidence"
                                             / (QString::fromLatin1(shaHex(response.sha256()))
                                                + ".json");
    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            contentAddressed,
            response.exactBytes(),
            true,
            maximumCanonicalBytes);
        !written) {
        return Utils::ResultError(written.error());
    }
    const QString fixedName = evidenceFileName(kind);
    if (!fixedName.isEmpty()) {
        const Utils::FilePath fixedPath = operationPaths.operationRoot / fixedName;
        if (const Utils::Result<> written = writeAtomicFile(
                m_rootHandle,
                m_compilerRoot,
                fixedPath,
                response.exactBytes(),
                true,
                maximumCanonicalBytes);
            !written) {
            return Utils::ResultError(written.error());
        }
    }
    if (const Utils::Result<> ledger = sealCompilerLedger(lease); !ledger)
        return Utils::ResultError(ledger.error());
    return contentAddressed;
}

Utils::Result<Utils::FilePath> CompilerOperationStore::persistPackage(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerOperationId &operationId,
    const QByteArray &packageBytes,
    const Data::RuntimePackageCompilerSha256 &packageSha256)
{
    if (!ownsLease(lease) || !operationId.isValid() || packageBytes.isEmpty()
        || packageBytes.size() > maximumPackageBytes || !packageSha256.isValid()
        || sha256(packageBytes) != packageSha256) {
        return Utils::ResultError(QStringLiteral("Invalid compiler package evidence."));
    }
    const CompilerOperationPaths operationPaths = paths(operationId);
    const Utils::FilePath contentAddressed = operationPaths.operationRoot / "evidence"
                                             / (QString::fromLatin1(shaHex(packageSha256))
                                                + ".ecpkg");
    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle, m_compilerRoot, contentAddressed, packageBytes, true, maximumPackageBytes);
        !written) {
        return Utils::ResultError(written.error());
    }
    if (const Utils::Result<> written = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            operationPaths.package,
            packageBytes,
            true,
            maximumPackageBytes);
        !written) {
        return Utils::ResultError(written.error());
    }
    if (const Utils::Result<> ledger = sealCompilerLedger(lease); !ledger)
        return Utils::ResultError(ledger.error());
    return contentAddressed;
}

Utils::Result<Utils::FilePath> CompilerOperationStore::persistFileEvidence(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerOperationId &operationId,
    const Utils::FilePath &sourceFile,
    const Data::RuntimePackageCompilerSha256 &expectedSha256,
    qsizetype maximumBytes)
{
    if (!ownsLease(lease) || !operationId.isValid() || !expectedSha256.isValid()
        || maximumBytes <= 0 || maximumBytes > maximumPackageBytes
        || !isProviderPath(operationRoot(operationId), sourceFile)) {
        return Utils::ResultError(QStringLiteral("Invalid compiler file evidence request."));
    }
    const Utils::Result<QByteArray> bytes = readProviderFile(lease, sourceFile, maximumBytes);
    if (!bytes)
        return Utils::ResultError(bytes.error());
    if (bytes->isEmpty() || sha256(*bytes) != expectedSha256)
        return Utils::ResultError(QStringLiteral("Compiler file evidence digest does not match."));

    const Utils::FilePath contentAddressed = operationRoot(operationId) / "evidence"
                                             / (QString::fromLatin1(shaHex(expectedSha256))
                                                + ".bin");
    if (const Utils::Result<> persisted
        = writeAtomicFile(m_rootHandle, m_compilerRoot, contentAddressed, *bytes, true, maximumBytes);
        !persisted) {
        return Utils::ResultError(persisted.error());
    }
    if (const Utils::Result<> sealed = writeAtomicFile(
            m_rootHandle,
            m_compilerRoot,
            sourceFile,
            *bytes,
            true,
            maximumBytes,
            0600,
            DirectoryPolicy::CompilerOwned);
        !sealed) {
        return Utils::ResultError(sealed.error());
    }
    return contentAddressed;
}

Utils::Result<Utils::FilePath> CompilerOperationStore::pinProvisionedFile(
    const CompilerOperationLease &lease,
    const QByteArray &exactBytes,
    const Data::RuntimePackageCompilerSha256 &expectedSha256,
    CompilerProvisionedFileKind kind)
{
    const bool executable = kind == CompilerProvisionedFileKind::Executable;
    const qsizetype maximumBytes = executable ? maximumProvisionedExecutableBytes : 32;
    if (!ownsLease(lease) || exactBytes.isEmpty() || exactBytes.size() > maximumBytes
        || (!executable && exactBytes.size() != 32) || !expectedSha256.isValid()
        || sha256(exactBytes) != expectedSha256) {
        return Utils::ResultError(QStringLiteral("Invalid provisioned compiler file."));
    }
    if (const Utils::Result<> store = validateStore(); !store)
        return Utils::ResultError(store.error());

    const Utils::FilePath provisionedRoot = m_compilerRoot / "provisioned";
    Utils::FilePath digestRoot;
    if (const Utils::Result<> directory = createPrivateDirectory(
            m_rootHandle,
            m_compilerRoot,
            provisionedRoot,
            QString::fromLatin1(shaHex(expectedSha256)),
            &digestRoot);
        !directory) {
        return Utils::ResultError(directory.error());
    }
    const Utils::FilePath pinned = digestRoot
                                   / (executable ? QStringLiteral("compiler")
                                                 : QStringLiteral("production-public-key.bin"));
    const unsigned int mode = executable ? 0500 : 0400;
    if (const Utils::Result<> written
        = writeAtomicFile(m_rootHandle, m_compilerRoot, pinned, exactBytes, true, maximumBytes, mode);
        !written) {
        return Utils::ResultError(written.error());
    }
    if (const Utils::Result<> valid
        = validatePinnedProvisionedFile(lease, pinned, expectedSha256, kind);
        !valid) {
        return Utils::ResultError(valid.error());
    }
    return pinned;
}

Utils::Result<> CompilerOperationStore::validatePinnedProvisionedFile(
    const CompilerOperationLease &lease,
    const Utils::FilePath &file,
    const Data::RuntimePackageCompilerSha256 &expectedSha256,
    CompilerProvisionedFileKind kind) const
{
    const bool executable = kind == CompilerProvisionedFileKind::Executable;
    const QString leafName = executable ? QStringLiteral("compiler")
                                        : QStringLiteral("production-public-key.bin");
    const Utils::FilePath expectedPath = m_compilerRoot / "provisioned"
                                         / QString::fromLatin1(shaHex(expectedSha256)) / leafName;
    const qsizetype maximumBytes = executable ? maximumProvisionedExecutableBytes : 32;
    const unsigned int mode = executable ? 0500 : 0400;
    if (!ownsLease(lease) || !expectedSha256.isValid()
        || QDir::cleanPath(file.path()) != expectedPath.path()) {
        return Utils::ResultError(QStringLiteral("Pinned compiler path is not content-addressed."));
    }
    const Utils::Result<QByteArray> bytes = readRegularLeaf(
        m_rootHandle, m_compilerRoot, file, maximumBytes, DirectoryPolicy::Private, mode);
    if (!bytes || bytes->isEmpty() || (!executable && bytes->size() != 32)
        || sha256(*bytes) != expectedSha256) {
        return Utils::ResultError(QStringLiteral("Pinned compiler file is stale or unsafe."));
    }
    return Utils::ResultOk;
}

Utils::Result<QByteArray> CompilerOperationStore::readProviderFile(
    const CompilerOperationLease &lease, const Utils::FilePath &file, qsizetype maximumBytes) const
{
    if (!ownsLease(lease) || maximumBytes < 0 || maximumBytes > maximumPackageBytes
        || !isProviderPath(m_compilerRoot, file)) {
        return Utils::ResultError(QStringLiteral("Compiler attempted to read an unsafe path."));
    }
    if (const Utils::Result<> ancestors
        = validateProviderDirectoryChain(m_rootHandle, m_compilerRoot, file.parentDir());
        !ancestors) {
        return Utils::ResultError(ancestors.error());
    }
    return readRegularLeaf(
        m_rootHandle, m_compilerRoot, file, maximumBytes, DirectoryPolicy::CompilerOwned);
}

Utils::FilePath CompilerOperationStore::compilerRoot() const
{
    return m_compilerRoot;
}
Utils::FilePath CompilerOperationStore::compilerLedger() const
{
    return m_compilerRoot / "compiler-ledger.json";
}
Utils::FilePath CompilerOperationStore::operationRoot(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return m_compilerRoot / "operations" / operationId.value();
}
Utils::FilePath CompilerOperationStore::artifactRoot(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "artifacts";
}
Utils::FilePath CompilerOperationStore::outputDir(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "output";
}
Utils::FilePath CompilerOperationStore::compileRequest(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "compile-request.json";
}
Utils::FilePath CompilerOperationStore::verifyRequest(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "verify-request.json";
}
Utils::FilePath CompilerOperationStore::signRequest(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "sign-request.json";
}
Utils::FilePath CompilerOperationStore::signResponse(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "sign-response.json";
}
Utils::FilePath CompilerOperationStore::publicKey(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "production-public-key.bin";
}
Utils::FilePath CompilerOperationStore::package(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return operationRoot(operationId) / "package.ecpkg";
}

bool CompilerOperationStore::ownsLease(const CompilerOperationLease &lease) const
{
    return lease.isValid() && lease.m_storeIdentity == m_storeIdentity;
}

CompilerOperationPaths CompilerOperationStore::paths(
    const Data::RuntimePackageCompilerOperationId &operationId) const
{
    return {
        m_compilerRoot,
        compilerLedger(),
        operationRoot(operationId),
        artifactRoot(operationId),
        outputDir(operationId),
        compileRequest(operationId),
        verifyRequest(operationId),
        signRequest(operationId),
        signResponse(operationId),
        publicKey(operationId),
        package(operationId),
    };
}

Utils::Result<> CompilerOperationStore::validateStore() const
{
    if (const Utils::Result<> root
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, m_compilerRoot);
        !root) {
        return root;
    }
    if (const Utils::Result<> operations
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, m_compilerRoot / "operations");
        !operations) {
        return operations;
    }
    if (const Utils::Result<> configurations
        = validatePrivateDirectory(m_rootHandle, m_compilerRoot, m_compilerRoot / "configurations");
        !configurations) {
        return configurations;
    }
    return validatePrivateDirectory(m_rootHandle, m_compilerRoot, m_compilerRoot / "provisioned");
}

Utils::Result<> CompilerOperationStore::materializeCompileArtifacts(
    const CompilerOperationLease &lease,
    const Data::RuntimePackageCompilerCompileRequest &request,
    const CompilerOperationPaths &operationPaths)
{
    if (!ownsLease(lease))
        return Utils::ResultError(QStringLiteral("Compiler artifact lease is invalid."));
    QList<Data::RuntimePackageCompilerSourceArtifact> artifacts{
        request.sourceArtifacts.topologyEvidence,
        request.sourceArtifacts.targetProfile,
        request.sourceArtifacts.targetProfileSignature,
        request.sourceArtifacts.productionPublicKey,
        request.sourceArtifacts.adapterBundle,
        request.sourceArtifacts.policyTemplate,
        request.sourceArtifacts.controllerFeatures,
        request.sourceArtifacts.runtimeSource,
    };
    for (const Data::RuntimePackageCompilerDeviceSourceEvidence &device :
         request.deviceSourceEvidence) {
        artifacts.append(device.originalEsi);
        artifacts.append(device.adapterSourceFile);
    }
    QHash<QString, Data::RuntimePackageCompilerSha256> seen;
    for (const Data::RuntimePackageCompilerSourceArtifact &artifact : std::as_const(artifacts)) {
        if (!artifact.isValid() || !isSafeRelativePath(artifact.relativePath)
            || artifact.exactBytes.size() > maximumPackageBytes) {
            return Utils::ResultError(QStringLiteral("Compiler source artifact path is unsafe."));
        }
        const auto previous = seen.constFind(artifact.relativePath);
        if (previous != seen.cend() && *previous != artifact.sha256)
            return Utils::ResultError(QStringLiteral("Compiler source artifact path collides."));
        seen.insert(artifact.relativePath, artifact.sha256);

        const QString parentRelative = QFileInfo(artifact.relativePath).path();
        Utils::FilePath parent = operationPaths.artifactRoot;
        if (parentRelative != QStringLiteral(".")) {
            if (const Utils::Result<> created = createPrivateDirectoryPath(
                    m_rootHandle,
                    m_compilerRoot,
                    operationPaths.artifactRoot,
                    parentRelative,
                    &parent);
                !created) {
                return created;
            }
        }
        const Utils::FilePath destination = parent / QFileInfo(artifact.relativePath).fileName();
        if (!isProviderPath(m_compilerRoot, destination))
            return Utils::ResultError(QStringLiteral("Compiler artifact escaped its root."));
        if (const Utils::Result<> written = writeAtomicFile(
                m_rootHandle,
                m_compilerRoot,
                destination,
                artifact.exactBytes,
                true,
                maximumPackageBytes);
            !written) {
            return written;
        }
    }
    return writeAtomicFile(
        m_rootHandle,
        m_compilerRoot,
        operationPaths.publicKey,
        request.sourceArtifacts.productionPublicKey.exactBytes,
        true,
        32);
}

Utils::Result<> CompilerOperationStore::sealCompilerLedger(const CompilerOperationLease &lease) const
{
    if (!ownsLease(lease))
        return Utils::ResultError(QStringLiteral("Compiler ledger lease is invalid."));
    const Utils::Result<QByteArray> ledger
        = readRegularLeaf(m_rootHandle, m_compilerRoot, compilerLedger(), maximumLedgerBytes);
    if (!ledger)
        return Utils::ResultError(ledger.error());
    const Data::RuntimePackageCompilerCanonicalJson canonical
        = Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(*ledger);
    if (!canonical.isValid()
        || rootString(*ledger, "format") != QStringLiteral("ethercat-ide-compiler-ledger-v1")
        || rootUnsigned(*ledger, "format_version") != 1) {
        return Utils::ResultError(QStringLiteral("Compiler ledger is malformed or noncanonical."));
    }
    return writeAtomicFile(
        m_rootHandle, m_compilerRoot, compilerLedger(), *ledger, false, maximumLedgerBytes);
}

#ifdef WITH_TESTS
void CompilerOperationStore::setBeforeLeafOpenHookForTest(std::function<void()> hook)
{
#ifdef Q_OS_UNIX
    const std::lock_guard<std::mutex> guard(beforeLeafOpenHookMutex);
    beforeLeafOpenHook = std::move(hook);
#else
    Q_UNUSED(hook)
#endif
}
#endif

} // namespace EtherCAT::ProjectCompiler
