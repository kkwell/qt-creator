// Copyright (C) 2026 Kvell

#include "offlineconfiguration.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>

namespace EtherCAT::Data {

namespace {

struct BitRange
{
    qint64 begin = 0;
    qint64 end = 0;
};

static ConfigurationIssue issue(
    ConfigurationIssueCode code,
    const NodeId &sourceId,
    const QString &field,
    const QString &message,
    ConfigurationIssueSeverity severity = ConfigurationIssueSeverity::Error)
{
    return {code, severity, sourceId, field, message};
}

static int fixedDataTypeBitLength(EtherCATDataType dataType)
{
    switch (dataType) {
    case EtherCATDataType::Boolean:
        return 1;
    case EtherCATDataType::Integer8:
    case EtherCATDataType::UnsignedInteger8:
        return 8;
    case EtherCATDataType::Integer16:
    case EtherCATDataType::UnsignedInteger16:
        return 16;
    case EtherCATDataType::Integer32:
    case EtherCATDataType::UnsignedInteger32:
    case EtherCATDataType::Real32:
        return 32;
    case EtherCATDataType::Integer64:
    case EtherCATDataType::UnsignedInteger64:
    case EtherCATDataType::Real64:
        return 64;
    case EtherCATDataType::Unknown:
    case EtherCATDataType::VisibleString:
    case EtherCATDataType::OctetString:
        return 0;
    }
    return 0;
}

static SyncManagerDirection expectedSyncManagerDirection(PdoDirection direction)
{
    return direction == PdoDirection::Rx ? SyncManagerDirection::MasterToSlave
                                         : SyncManagerDirection::SlaveToMaster;
}

static ProcessImageDirection &imageDirection(ProcessImagePreview &preview, PdoDirection direction)
{
    return direction == PdoDirection::Rx ? preview.outputs : preview.inputs;
}

static QList<BitRange> &imageRanges(
    QList<BitRange> &inputRanges, QList<BitRange> &outputRanges, PdoDirection direction)
{
    return direction == PdoDirection::Rx ? outputRanges : inputRanges;
}

static QString pdoKey(const PdoConfiguration &pdo)
{
    return QString::number(int(pdo.direction)) + ':' + QString::number(pdo.index);
}

static QString entryKey(const PdoConfiguration &pdo, const PdoEntryConfiguration &entry)
{
    return QString::number(int(pdo.direction)) + ':' + QString::number(entry.index) + ':'
           + QString::number(entry.subIndex);
}

static bool rangesOverlap(const BitRange &left, const BitRange &right)
{
    return left.begin < right.end && right.begin < left.end;
}

static void validateDcSignal(
    const DcSignalConfiguration &signal, const QString &name, QList<ConfigurationIssue> &issues)
{
    if (!signal.enabled)
        return;

    constexpr qint64 maximumCycleTimeNs = std::numeric_limits<quint32>::max();
    if (signal.cycleTimeNs <= 0 || signal.cycleTimeNs > maximumCycleTimeNs) {
        issues.append(issue(
            ConfigurationIssueCode::InvalidDcCycle,
            {},
            name + ".cycleTimeNs",
            name + " cycle time must be between 1 and 4294967295 ns."));
        return;
    }
    if (signal.shiftTimeNs < -signal.cycleTimeNs || signal.shiftTimeNs > signal.cycleTimeNs) {
        issues.append(issue(
            ConfigurationIssueCode::DcShiftOutOfRange,
            {},
            name + ".shiftTimeNs",
            name + " shift time must stay within one cycle in either direction."));
    }
}

} // namespace

bool ConfigurationValidation::hasErrors() const
{
    return std::any_of(issues.cbegin(), issues.cend(), [](const ConfigurationIssue &entry) {
        return entry.severity == ConfigurationIssueSeverity::Error;
    });
}

ConfigurationValidation validateProcessDataConfiguration(
    const ProcessDataConfiguration &configuration)
{
    ConfigurationValidation validation;
    QHash<int, SyncManagerConfiguration> syncManagers;
    for (const SyncManagerConfiguration &syncManager : configuration.syncManagers) {
        if (syncManagers.contains(syncManager.index)) {
            validation.issues.append(issue(
                ConfigurationIssueCode::DuplicateSyncManager,
                syncManager.id,
                "syncManagers",
                QString("Sync Manager %1 is configured more than once.").arg(syncManager.index)));
            continue;
        }
        syncManagers.insert(syncManager.index, syncManager);
    }

    QSet<QString> pdoAssignments;
    QSet<QString> mappedEntries;
    QHash<int, qint64> syncManagerBitSizes;
    QList<BitRange> inputRanges;
    QList<BitRange> outputRanges;

    for (const PdoConfiguration &pdo : configuration.pdos) {
        if (pdo.mandatory && !pdo.selected) {
            validation.issues.append(issue(
                ConfigurationIssueCode::MandatoryPdoNotSelected,
                pdo.id,
                "selected",
                QString("Mandatory PDO 0x%1 must remain selected.")
                    .arg(pdo.index, 4, 16, QLatin1Char('0'))));
        }

        const auto syncManager = syncManagers.constFind(pdo.syncManager);
        NodeId syncManagerId;
        if (syncManager == syncManagers.cend()) {
            validation.issues.append(issue(
                ConfigurationIssueCode::MissingSyncManager,
                pdo.id,
                "syncManager",
                QString("PDO 0x%1 references missing Sync Manager %2.")
                    .arg(pdo.index, 4, 16, QLatin1Char('0'))
                    .arg(pdo.syncManager)));
        } else {
            syncManagerId = syncManager->id;
            if (!syncManager->enabled) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::DisabledSyncManager,
                    pdo.id,
                    "syncManager",
                    QString("PDO 0x%1 references disabled Sync Manager %2.")
                        .arg(pdo.index, 4, 16, QLatin1Char('0'))
                        .arg(pdo.syncManager)));
            }
            if (syncManager->direction != expectedSyncManagerDirection(pdo.direction)) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::SyncManagerDirectionMismatch,
                    pdo.id,
                    "syncManager",
                    QString("PDO 0x%1 direction does not match Sync Manager %2.")
                        .arg(pdo.index, 4, 16, QLatin1Char('0'))
                        .arg(pdo.syncManager)));
            }
        }
        if (!pdo.selected)
            continue;

        if (pdo.index == 0) {
            validation.issues.append(issue(
                ConfigurationIssueCode::InvalidPdoIndex,
                pdo.id,
                "index",
                "A selected PDO must have a non-zero index."));
        }
        if (pdo.entries.isEmpty()) {
            validation.issues.append(issue(
                ConfigurationIssueCode::EmptyPdo,
                pdo.id,
                "entries",
                QString("Selected PDO 0x%1 has no entries.")
                    .arg(pdo.index, 4, 16, QLatin1Char('0'))));
        }
        if (!pdo.mappingSupported) {
            validation.issues.append(issue(
                ConfigurationIssueCode::UnsupportedPdoMapping,
                pdo.id,
                "mappingSupported",
                QString("PDO 0x%1 uses a mapping that is not supported.")
                    .arg(pdo.index, 4, 16, QLatin1Char('0'))));
        }

        const QString assignmentKey = pdoKey(pdo);
        if (pdoAssignments.contains(assignmentKey)) {
            validation.issues.append(issue(
                ConfigurationIssueCode::DuplicatePdoAssignment,
                pdo.id,
                "index",
                QString("PDO 0x%1 is assigned more than once in the same direction.")
                    .arg(pdo.index, 4, 16, QLatin1Char('0'))));
        } else {
            pdoAssignments.insert(assignmentKey);
        }

        ProcessImageDirection &image = imageDirection(validation.processImage, pdo.direction);
        QList<BitRange> &ranges = imageRanges(inputRanges, outputRanges, pdo.direction);
        for (const PdoEntryConfiguration &entry : pdo.entries) {
            if (!entry.mappingSupported) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::UnsupportedPdoMapping,
                    entry.id,
                    "mappingSupported",
                    QString("PDO entry 0x%1:%2 uses an unsupported mapping.")
                        .arg(entry.index, 4, 16, QLatin1Char('0'))
                        .arg(entry.subIndex)));
            }
            if (entry.bitLength <= 0) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::InvalidPdoEntryBitLength,
                    entry.id,
                    "bitLength",
                    "PDO entry bit length must be greater than zero."));
                continue;
            }

            const int expectedBitLength = fixedDataTypeBitLength(entry.dataType);
            if (expectedBitLength > 0 && expectedBitLength != entry.bitLength) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::DataTypeBitLengthMismatch,
                    entry.id,
                    "bitLength",
                    QString("PDO entry bit length %1 does not match its %2-bit data type.")
                        .arg(entry.bitLength)
                        .arg(expectedBitLength)));
            } else if (entry.dataType == EtherCATDataType::Unknown && !entry.padding) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::UnknownDataType,
                    entry.id,
                    "dataType",
                    "PDO entry data type is unknown; the configured bit length is used.",
                    ConfigurationIssueSeverity::Warning));
            }

            if (!entry.padding) {
                const QString mappedEntryKey = entryKey(pdo, entry);
                if (mappedEntries.contains(mappedEntryKey)) {
                    validation.issues.append(issue(
                        ConfigurationIssueCode::DuplicatePdoEntry,
                        entry.id,
                        "index",
                        QString("PDO entry 0x%1:%2 is mapped more than once.")
                            .arg(entry.index, 4, 16, QLatin1Char('0'))
                            .arg(entry.subIndex)));
                } else {
                    mappedEntries.insert(mappedEntryKey);
                }
            }

            qint64 bitOffset = entry.requestedBitOffset;
            if (bitOffset < -1) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::InvalidProcessImageOffset,
                    entry.id,
                    "requestedBitOffset",
                    "Requested process-image offset must be -1 for automatic layout or "
                    "non-negative."));
                bitOffset = -1;
            }
            if (bitOffset == -1)
                bitOffset = image.bitSize;
            if (bitOffset > std::numeric_limits<qint64>::max() - entry.bitLength) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::InvalidProcessImageOffset,
                    entry.id,
                    "requestedBitOffset",
                    "Requested process-image range exceeds the supported offset range."));
                continue;
            }

            const BitRange range{bitOffset, bitOffset + entry.bitLength};
            if (std::any_of(ranges.cbegin(), ranges.cend(), [&range](const BitRange &existing) {
                    return rangesOverlap(range, existing);
                })) {
                validation.issues.append(issue(
                    ConfigurationIssueCode::ProcessImageOverlap,
                    entry.id,
                    "requestedBitOffset",
                    QString("Process-image range [%1, %2) overlaps an existing entry.")
                        .arg(range.begin)
                        .arg(range.end)));
            }
            ranges.append(range);
            image.bitSize = std::max(image.bitSize, range.end);
            image.entries.append(
                {pdo.id,
                 entry.id,
                 syncManagerId,
                 pdo.index,
                 entry.index,
                 entry.subIndex,
                 entry.name,
                 pdo.direction,
                 pdo.syncManager,
                 bitOffset,
                 entry.bitLength,
                 bitOffset / 8,
                 int(bitOffset % 8),
                 entry.dataType});
            syncManagerBitSizes[pdo.syncManager] += entry.bitLength;
        }
    }

    validation.processImage.inputs.byteSize = (validation.processImage.inputs.bitSize + 7) / 8;
    validation.processImage.outputs.byteSize = (validation.processImage.outputs.bitSize + 7) / 8;
    for (auto syncManager = syncManagers.cbegin(); syncManager != syncManagers.cend();
         ++syncManager) {
        if (syncManager->sizeLimitBytes <= 0)
            continue;
        const qint64 sizeBytes = (syncManagerBitSizes.value(syncManager.key()) + 7) / 8;
        if (sizeBytes > syncManager->sizeLimitBytes) {
            validation.issues.append(issue(
                ConfigurationIssueCode::SyncManagerSizeExceeded,
                syncManager->id,
                "syncManagers",
                QString("Sync Manager %1 uses %2 bytes but is limited to %3 bytes.")
                    .arg(syncManager.key())
                    .arg(sizeBytes)
                    .arg(syncManager->sizeLimitBytes)));
        }
    }
    return validation;
}

QList<ConfigurationIssue> validateStartupConfiguration(const StartupConfiguration &configuration)
{
    QList<ConfigurationIssue> issues;
    QSet<int> enabledOrders;
    for (const StartupParameterConfiguration &parameter : configuration.parameters) {
        if (parameter.order < 0) {
            issues.append(issue(
                ConfigurationIssueCode::InvalidStartupOrder,
                parameter.id,
                "order",
                "Startup order must be non-negative."));
        } else if (parameter.enabled) {
            if (enabledOrders.contains(parameter.order)) {
                issues.append(issue(
                    ConfigurationIssueCode::DuplicateStartupOrder,
                    parameter.id,
                    "order",
                    QString("Enabled Startup order %1 is used more than once.")
                        .arg(parameter.order)));
            } else {
                enabledOrders.insert(parameter.order);
            }
        }

        if (parameter.transition.trimmed().isEmpty()) {
            issues.append(issue(
                ConfigurationIssueCode::MissingStartupTransition,
                parameter.id,
                "transition",
                "Startup transition must be specified."));
        }
        if (parameter.index == 0) {
            issues.append(issue(
                ConfigurationIssueCode::InvalidStartupIndex,
                parameter.id,
                "index",
                "Startup object index must be non-zero."));
        }
        if (!parameter.enabled)
            continue;

        const int expectedBitLength = fixedDataTypeBitLength(parameter.dataType);
        const int expectedByteLength = expectedBitLength > 0 ? (expectedBitLength + 7) / 8 : 0;
        if (parameter.rawValue.isEmpty()
            || (expectedByteLength > 0 && parameter.rawValue.size() != expectedByteLength)) {
            issues.append(issue(
                ConfigurationIssueCode::InvalidStartupValueSize,
                parameter.id,
                "rawValue",
                expectedByteLength > 0
                    ? QString("Startup raw value must contain exactly %1 byte(s).")
                          .arg(expectedByteLength)
                    : QString("Startup raw value must not be empty.")));
        }
        if (parameter.dataType == EtherCATDataType::Unknown) {
            issues.append(issue(
                ConfigurationIssueCode::UnknownDataType,
                parameter.id,
                "dataType",
                "Startup data type is unknown; the raw value is preserved.",
                ConfigurationIssueSeverity::Warning));
        }
    }
    return issues;
}

QList<ConfigurationIssue> validateDcConfiguration(const DcConfiguration &configuration)
{
    QList<ConfigurationIssue> issues;
    if (!configuration.enabled) {
        if (configuration.sync0.enabled || configuration.sync1.enabled) {
            issues.append(issue(
                ConfigurationIssueCode::DcSignalWithoutMode,
                {},
                "enabled",
                "SYNC signals cannot be enabled while Distributed Clocks are disabled."));
        }
        return issues;
    }

    if (configuration.modeName.trimmed().isEmpty()) {
        issues.append(issue(
            ConfigurationIssueCode::MissingDcMode,
            {},
            "modeName",
            "An enabled Distributed Clocks configuration requires a mode."));
    }
    if (configuration.sync1.enabled && !configuration.sync0.enabled) {
        issues.append(issue(
            ConfigurationIssueCode::DcSync1RequiresSync0,
            {},
            "sync1.enabled",
            "SYNC1 requires SYNC0 to be enabled."));
    }
    validateDcSignal(configuration.sync0, "SYNC0", issues);
    validateDcSignal(configuration.sync1, "SYNC1", issues);
    return issues;
}

} // namespace EtherCAT::Data
