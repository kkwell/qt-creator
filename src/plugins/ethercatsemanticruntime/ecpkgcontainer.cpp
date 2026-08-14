// Copyright (C) 2026 Embed Labs

#include "ecpkgcontainer.h"

#include <QCryptographicHash>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr quint32 localHeaderMagic = 0x04034b50;
constexpr quint32 centralHeaderMagic = 0x02014b50;
constexpr quint32 endOfCentralDirectoryMagic = 0x06054b50;
constexpr quint16 versionNeeded = 20;
constexpr quint16 versionMadeBy = 0x0314;
constexpr quint16 canonicalDosDate = 33;
constexpr quint32 canonicalExternalAttributes = 0x81a40000;
constexpr qsizetype localHeaderBytes = 30;
constexpr qsizetype centralHeaderBytes = 46;
constexpr qsizetype endOfCentralDirectoryBytes = 22;
constexpr qsizetype manifestMaximumBytes = 1024 * 1024;
constexpr qsizetype innerPayloadMaximumBytes = 2 * 1024 * 1024;
constexpr qsizetype compileReportMaximumBytes = 8 * 1024 * 1024;
constexpr qsizetype signatureBytes = 64;
constexpr std::size_t v1EntryCount = 6;
constexpr std::size_t v2EntryCount = 7;
constexpr std::size_t signatureEntryIndex = 6;

constexpr std::array<std::string_view, v2EntryCount> entryNames{
    "manifest.json",
    "capability.bin",
    "configuration.ecfg",
    "runtime.erun",
    "compile_report.json",
    "semantic-action-definitions-v1.json",
    "manifest.sig",
};

constexpr std::array<qsizetype, v2EntryCount> entryMaximumBytes{
    manifestMaximumBytes,
    innerPayloadMaximumBytes,
    innerPayloadMaximumBytes,
    innerPayloadMaximumBytes,
    compileReportMaximumBytes,
    compileReportMaximumBytes,
    signatureBytes,
};

bool containsRange(quint64 outerBytes, quint64 offset, quint64 bytes)
{
    return offset <= outerBytes && bytes <= outerBytes - offset;
}

quint16 readLe16(QByteArrayView bytes, quint64 offset)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.data()) + offset;
    return quint16(data[0]) | (quint16(data[1]) << 8);
}

quint32 readLe32(QByteArrayView bytes, quint64 offset)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.data()) + offset;
    return quint32(data[0]) | (quint32(data[1]) << 8) | (quint32(data[2]) << 16)
           | (quint32(data[3]) << 24);
}

bool bytesEqual(QByteArrayView package, quint64 offset, std::string_view expected)
{
    return std::memcmp(
               package.data() + offset, expected.data(), static_cast<std::size_t>(expected.size()))
           == 0;
}

quint32 zipCrc32(QByteArrayView bytes)
{
    quint32 crc = std::numeric_limits<quint32>::max();
    for (char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 lowBitMask = quint32(0) - (crc & 1);
            crc = (crc >> 1) ^ (0xedb88320 & lowBitMask);
        }
    }
    return ~crc;
}

Utils::ResultError invalidContainer(const char *detail)
{
    return Utils::ResultError(QString::fromLatin1("Invalid canonical ECPKG container: %1")
            .arg(QString::fromLatin1(detail)));
}

QByteArray copyRange(QByteArrayView package, quint64 offset, quint32 bytes)
{
    return QByteArray(package.data() + offset, qsizetype(bytes));
}

} // namespace

Utils::Result<EcpkgContainer> parseCanonicalEcpkgContainer(
    QByteArrayView package, qsizetype maximumPackageBytes)
{
    if (maximumPackageBytes <= 0)
        return invalidContainer("the package size limit is invalid");
    if (package.size() <= 0)
        return invalidContainer("the package is empty");
    if (package.size() > defaultMaximumEcpkgContainerBytes)
        return invalidContainer("the package exceeds the ECPKG size limit");
    if (package.size() > maximumPackageBytes)
        return invalidContainer("the package exceeds the configured size limit");
    if (quint64(package.size()) > std::numeric_limits<quint32>::max())
        return invalidContainer("the package cannot be represented by classic ZIP");

    const quint64 packageBytes = quint64(package.size());
    if (packageBytes < endOfCentralDirectoryBytes)
        return invalidContainer("the end-of-central-directory record is truncated");

    const quint64 endOffset = packageBytes - endOfCentralDirectoryBytes;
    const quint16 entryCount = readLe16(package, endOffset + 8);
    if (readLe32(package, endOffset) != endOfCentralDirectoryMagic
        || readLe16(package, endOffset + 4) != 0 || readLe16(package, endOffset + 6) != 0
        || (entryCount != v1EntryCount && entryCount != v2EntryCount)
        || readLe16(package, endOffset + 10) != entryCount
        || readLe16(package, endOffset + 20) != 0) {
        return invalidContainer("the end-of-central-directory record is noncanonical");
    }

    const quint32 centralBytes = readLe32(package, endOffset + 12);
    const quint32 centralOffset = readLe32(package, endOffset + 16);
    if (centralBytes == std::numeric_limits<quint32>::max()
        || centralOffset == std::numeric_limits<quint32>::max()
        || !containsRange(endOffset, centralOffset, centralBytes)
        || quint64(centralOffset) + centralBytes != endOffset) {
        return invalidContainer("the central-directory range is invalid");
    }

    std::array<QByteArray, v2EntryCount> payloads;
    quint64 centralCursor = centralOffset;
    quint64 expectedLocalOffset = 0;

    for (std::size_t index = 0; index < entryCount; ++index) {
        const std::size_t entryIndex = entryCount == v1EntryCount && index == v1EntryCount - 1
                                           ? signatureEntryIndex
                                           : index;
        const std::string_view expectedName = entryNames[entryIndex];
        if (!containsRange(endOffset, centralCursor, centralHeaderBytes))
            return invalidContainer("a central-directory header is truncated");

        const quint32 centralCrc = readLe32(package, centralCursor + 16);
        const quint32 centralCompressedBytes = readLe32(package, centralCursor + 20);
        const quint32 centralUncompressedBytes = readLe32(package, centralCursor + 24);
        const quint16 centralNameBytes = readLe16(package, centralCursor + 28);
        const quint32 localOffset = readLe32(package, centralCursor + 42);

        if (readLe32(package, centralCursor) != centralHeaderMagic
            || readLe16(package, centralCursor + 4) != versionMadeBy
            || readLe16(package, centralCursor + 6) != versionNeeded
            || readLe16(package, centralCursor + 8) != 0
            || readLe16(package, centralCursor + 10) != 0
            || readLe16(package, centralCursor + 12) != 0
            || readLe16(package, centralCursor + 14) != canonicalDosDate
            || centralCompressedBytes != centralUncompressedBytes || centralUncompressedBytes == 0
            || centralCompressedBytes == std::numeric_limits<quint32>::max()
            || readLe16(package, centralCursor + 30) != 0
            || readLe16(package, centralCursor + 32) != 0
            || readLe16(package, centralCursor + 34) != 0
            || readLe16(package, centralCursor + 36) != 0
            || readLe32(package, centralCursor + 38) != canonicalExternalAttributes
            || localOffset == std::numeric_limits<quint32>::max()) {
            return invalidContainer("a central-directory header is noncanonical");
        }

        if (centralNameBytes != expectedName.size()
            || !containsRange(endOffset, centralCursor + centralHeaderBytes, centralNameBytes)
            || !bytesEqual(package, centralCursor + centralHeaderBytes, expectedName)) {
            return invalidContainer("the central-directory entry name or order is invalid");
        }
        if (centralUncompressedBytes > entryMaximumBytes[entryIndex])
            return invalidContainer("an entry exceeds its canonical size limit");
        if (entryIndex == signatureEntryIndex && centralUncompressedBytes != signatureBytes)
            return invalidContainer("manifest.sig is not exactly 64 bytes");
        if (localOffset != expectedLocalOffset
            || !containsRange(centralOffset, localOffset, localHeaderBytes + centralNameBytes)) {
            return invalidContainer("a local entry offset is noncanonical");
        }

        const quint32 localCrc = readLe32(package, quint64(localOffset) + 14);
        const quint32 localCompressedBytes = readLe32(package, quint64(localOffset) + 18);
        const quint32 localUncompressedBytes = readLe32(package, quint64(localOffset) + 22);
        const quint16 localNameBytes = readLe16(package, quint64(localOffset) + 26);

        if (readLe32(package, localOffset) != localHeaderMagic
            || readLe16(package, quint64(localOffset) + 4) != versionNeeded
            || readLe16(package, quint64(localOffset) + 6) != 0
            || readLe16(package, quint64(localOffset) + 8) != 0
            || readLe16(package, quint64(localOffset) + 10) != 0
            || readLe16(package, quint64(localOffset) + 12) != canonicalDosDate
            || localCrc != centralCrc || localCompressedBytes != centralCompressedBytes
            || localUncompressedBytes != centralUncompressedBytes
            || localNameBytes != centralNameBytes
            || readLe16(package, quint64(localOffset) + 28) != 0
            || !bytesEqual(package, quint64(localOffset) + localHeaderBytes, expectedName)) {
            return invalidContainer("a local entry header disagrees with the central directory");
        }

        const quint64 dataOffset = quint64(localOffset) + localHeaderBytes + localNameBytes;
        if (!containsRange(centralOffset, dataOffset, centralUncompressedBytes))
            return invalidContainer("an entry payload range is invalid");

        const QByteArrayView
            payload(package.data() + dataOffset, qsizetype(centralUncompressedBytes));
        if (zipCrc32(payload) != centralCrc)
            return invalidContainer("an entry payload CRC32 is invalid");

        payloads[entryIndex] = copyRange(package, dataOffset, centralUncompressedBytes);
        expectedLocalOffset = dataOffset + centralUncompressedBytes;
        centralCursor += centralHeaderBytes + centralNameBytes;
    }

    if (expectedLocalOffset != centralOffset)
        return invalidContainer("the local entries are not contiguous");
    if (centralCursor != endOffset)
        return invalidContainer("the central-directory entries are not contiguous");

    EcpkgContainer result;
    result.packageSha256 = QCryptographicHash::hash(package, QCryptographicHash::Sha256);
    result.manifestJson = std::move(payloads[0]);
    result.capabilityBin = std::move(payloads[1]);
    result.configurationEcfg = std::move(payloads[2]);
    result.runtimeErun = std::move(payloads[3]);
    result.compileReportJson = std::move(payloads[4]);
    result.semanticActionDefinitionsJson = std::move(payloads[5]);
    result.manifestSignature = std::move(payloads[6]);
    return result;
}

} // namespace EtherCAT::SemanticRuntime::Internal
