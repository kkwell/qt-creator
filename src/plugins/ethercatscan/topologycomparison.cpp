// Copyright (C) 2026 Kvell

#include "topologycomparison.h"

#include "ethercatscantr.h"

#include <QSet>

#include <algorithm>

namespace EtherCAT::Scan::Internal {

static bool samePhysicalDevice(
    const Data::OfflineSlaveConfiguration &offline, const Data::ScannedSlave &scanned)
{
    if (offline.identity.vendorId != scanned.identity.vendorId
        || offline.identity.productCode != scanned.identity.productCode) {
        return false;
    }
    if (offline.serialNumber != 0 && scanned.serialNumber != 0)
        return offline.serialNumber == scanned.serialNumber;
    return offline.alias == scanned.alias
           && offline.identity.revisionNumber == scanned.identity.revisionNumber;
}

static QString physicalKey(const Data::ScannedSlave &slave)
{
    if (slave.serialNumber != 0) {
        return QString("%1:%2:serial:%3")
            .arg(slave.identity.vendorId)
            .arg(slave.identity.productCode)
            .arg(slave.serialNumber);
    }
    return QString("%1:%2:revision:%3:alias:%4")
        .arg(slave.identity.vendorId)
        .arg(slave.identity.productCode)
        .arg(slave.identity.revisionNumber)
        .arg(slave.alias);
}

static void appendDifference(
    Data::TopologyComparison *comparison,
    Data::TopologyDifferenceKind kind,
    Data::DifferenceSeverity severity,
    const Data::OfflineSlaveConfiguration *offline,
    const Data::ScannedSlave *scanned,
    const QString &summary,
    const QString &detail)
{
    comparison->differences.append(
        {kind,
         severity,
         offline ? offline->id : Data::NodeId(),
         scanned ? scanned->id : Data::NodeId(),
         offline ? offline->position : -1,
         scanned ? scanned->position : -1,
         summary,
         detail});
}

static void appendFieldDifferences(
    Data::TopologyComparison *comparison,
    const Data::OfflineSlaveConfiguration &offline,
    const Data::ScannedSlave &scanned)
{
    if (offline.identity.vendorId != scanned.identity.vendorId) {
        appendDifference(
            comparison,
            Data::TopologyDifferenceKind::VendorMismatch,
            Data::DifferenceSeverity::Blocking,
            &offline,
            &scanned,
            Tr::tr("Vendor mismatch"),
            Tr::tr("Offline 0x%1, scanned 0x%2")
                .arg(offline.identity.vendorId, 8, 16, QLatin1Char('0'))
                .arg(scanned.identity.vendorId, 8, 16, QLatin1Char('0')));
    }
    if (offline.identity.productCode != scanned.identity.productCode) {
        appendDifference(
            comparison,
            Data::TopologyDifferenceKind::ProductMismatch,
            Data::DifferenceSeverity::Blocking,
            &offline,
            &scanned,
            Tr::tr("Product mismatch"),
            Tr::tr("Offline 0x%1, scanned 0x%2")
                .arg(offline.identity.productCode, 8, 16, QLatin1Char('0'))
                .arg(scanned.identity.productCode, 8, 16, QLatin1Char('0')));
    }
    if (offline.identity.revisionNumber != scanned.identity.revisionNumber) {
        appendDifference(
            comparison,
            Data::TopologyDifferenceKind::RevisionMismatch,
            Data::DifferenceSeverity::Warning,
            &offline,
            &scanned,
            Tr::tr("Revision difference"),
            Tr::tr("Offline 0x%1, scanned 0x%2")
                .arg(offline.identity.revisionNumber, 8, 16, QLatin1Char('0'))
                .arg(scanned.identity.revisionNumber, 8, 16, QLatin1Char('0')));
    }
    if (offline.serialNumber != scanned.serialNumber) {
        appendDifference(
            comparison,
            Data::TopologyDifferenceKind::SerialMismatch,
            Data::DifferenceSeverity::Warning,
            &offline,
            &scanned,
            Tr::tr("Serial Number difference"),
            Tr::tr("Offline %1, scanned %2")
                .arg(offline.serialNumber)
                .arg(scanned.serialNumber));
    }
    if (offline.alias != scanned.alias) {
        appendDifference(
            comparison,
            Data::TopologyDifferenceKind::AliasMismatch,
            Data::DifferenceSeverity::Warning,
            &offline,
            &scanned,
            Tr::tr("Alias difference"),
            Tr::tr("Offline %1, scanned %2").arg(offline.alias).arg(scanned.alias));
    }
}

Data::TopologyComparison compareTopology(
    const Data::ProjectSnapshot &project,
    const Data::NodeId &masterId,
    const Data::ScanSnapshot &snapshot)
{
    Data::TopologyComparison comparison;
    comparison.projectId = project.id;
    comparison.masterId = masterId;

    QList<Data::OfflineSlaveConfiguration> offline;
    for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
        if (slave.masterId == masterId
            && (snapshot.operation != Data::ScanOperation::SelectedBranch
                || snapshot.branchNodeId == masterId
                || snapshot.branchNodeId == slave.id)) {
            offline.append(slave);
        }
    }
    std::sort(offline.begin(), offline.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    QList<Data::ScannedSlave> scanned = snapshot.slaves;
    std::sort(scanned.begin(), scanned.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });

    QSet<int> matchedOffline;
    QSet<int> matchedScanned;
    QSet<QString> physicalDevices;
    for (int scannedIndex = 0; scannedIndex < scanned.size(); ++scannedIndex) {
        const QString key = physicalKey(scanned.at(scannedIndex));
        if (physicalDevices.contains(key)) {
            appendDifference(
                &comparison,
                Data::TopologyDifferenceKind::DuplicateDevice,
                Data::DifferenceSeverity::Blocking,
                nullptr,
                &scanned[scannedIndex],
                Tr::tr("Duplicate scanned device"),
                Tr::tr("The same Mock physical identity appears more than once."));
        }
        physicalDevices.insert(key);
    }

    for (int offlineIndex = 0; offlineIndex < offline.size(); ++offlineIndex) {
        for (int scannedIndex = 0; scannedIndex < scanned.size(); ++scannedIndex) {
            if (matchedScanned.contains(scannedIndex)
                || !samePhysicalDevice(offline.at(offlineIndex), scanned.at(scannedIndex))) {
                continue;
            }
            matchedOffline.insert(offlineIndex);
            matchedScanned.insert(scannedIndex);
            if (offline.at(offlineIndex).position != scanned.at(scannedIndex).position) {
                appendDifference(
                    &comparison,
                    Data::TopologyDifferenceKind::PositionChanged,
                    Data::DifferenceSeverity::Warning,
                    &offline[offlineIndex],
                    &scanned[scannedIndex],
                    Tr::tr("Position changed"),
                    Tr::tr("Offline %1, scanned %2")
                        .arg(offline.at(offlineIndex).position)
                        .arg(scanned.at(scannedIndex).position));
            }
            appendFieldDifferences(
                &comparison, offline.at(offlineIndex), scanned.at(scannedIndex));
            break;
        }
    }

    for (int offlineIndex = 0; offlineIndex < offline.size(); ++offlineIndex) {
        if (matchedOffline.contains(offlineIndex))
            continue;
        for (int scannedIndex = 0; scannedIndex < scanned.size(); ++scannedIndex) {
            if (matchedScanned.contains(scannedIndex)
                || scanned.at(scannedIndex).position != offline.at(offlineIndex).position) {
                continue;
            }
            matchedOffline.insert(offlineIndex);
            matchedScanned.insert(scannedIndex);
            appendFieldDifferences(
                &comparison, offline.at(offlineIndex), scanned.at(scannedIndex));
            break;
        }
    }

    for (int offlineIndex = 0; offlineIndex < offline.size(); ++offlineIndex) {
        if (!matchedOffline.contains(offlineIndex)) {
            appendDifference(
                &comparison,
                Data::TopologyDifferenceKind::Missing,
                Data::DifferenceSeverity::Warning,
                &offline[offlineIndex],
                nullptr,
                Tr::tr("Missing slave"),
                offline.at(offlineIndex).name);
        }
    }
    for (int scannedIndex = 0; scannedIndex < scanned.size(); ++scannedIndex) {
        if (!matchedScanned.contains(scannedIndex)) {
            appendDifference(
                &comparison,
                Data::TopologyDifferenceKind::Added,
                Data::DifferenceSeverity::Information,
                nullptr,
                &scanned[scannedIndex],
                Tr::tr("Added slave"),
                scanned.at(scannedIndex).name);
        }
    }

    const int topologyDifferenceCount = comparison.differences.size();
    appendDifference(
        &comparison,
        Data::TopologyDifferenceKind::PdoConfiguration,
        Data::DifferenceSeverity::Information,
        nullptr,
        nullptr,
        Tr::tr("PDO comparison placeholder"),
        Tr::tr("PDO configuration is not compared in the Mock scan stage."));
    appendDifference(
        &comparison,
        Data::TopologyDifferenceKind::DcConfiguration,
        Data::DifferenceSeverity::Information,
        nullptr,
        nullptr,
        Tr::tr("DC comparison placeholder"),
        Tr::tr("DC configuration is not compared in the Mock scan stage."));

    comparison.exactMatch = topologyDifferenceCount == 0;
    comparison.acceptAllowed = snapshot.complete
                               && std::none_of(
                                   comparison.differences.cbegin(),
                                   comparison.differences.cend(),
                                   [](const Data::TopologyDifference &difference) {
                                       return difference.severity
                                              == Data::DifferenceSeverity::Blocking;
                                   });
    return comparison;
}

QList<Data::OfflineSlaveConfiguration> offlineConfigurationFromScan(
    const Data::ProjectSnapshot &project,
    const Data::NodeId &masterId,
    const Data::ScanSnapshot &snapshot)
{
    QList<Data::OfflineSlaveConfiguration> result;
    result.reserve(snapshot.slaves.size());
    QList<Data::OfflineSlaveConfiguration> offline;
    for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
        if (slave.masterId == masterId)
            offline.append(slave);
    }
    QSet<int> matchedOffline;
    for (const Data::ScannedSlave &slave : snapshot.slaves) {
        Data::NodeId acceptedId = slave.id;
        const Data::OfflineSlaveConfiguration *matchedConfiguration = nullptr;
        for (int index = 0; index < offline.size(); ++index) {
            if (!matchedOffline.contains(index)
                && samePhysicalDevice(offline.at(index), slave)) {
                acceptedId = offline.at(index).id;
                matchedConfiguration = &offline.at(index);
                matchedOffline.insert(index);
                break;
            }
        }
        const bool sameAdapterIdentity
            = matchedConfiguration && matchedConfiguration->identity == slave.identity;
        result.append(
            {acceptedId,
             snapshot.masterId,
             slave.position,
             slave.identity,
             slave.serialNumber,
             slave.alias,
             slave.name,
             slave.deviceDescriptionId,
             matchedConfiguration ? matchedConfiguration->processData
                                  : Data::ProcessDataConfiguration{},
             matchedConfiguration ? matchedConfiguration->startup : Data::StartupConfiguration{},
             matchedConfiguration ? matchedConfiguration->dc : Data::DcConfiguration{},
             sameAdapterIdentity ? matchedConfiguration->esiSha256 : QByteArray{},
             sameAdapterIdentity ? matchedConfiguration->adapterSelection
                                 : Data::DeviceAdapterProjectSelection{},
             sameAdapterIdentity ? matchedConfiguration->manualControlEnvelope
                                 : Data::ManualControlEnvelope{}});
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return result;
}

} // namespace EtherCAT::Scan::Internal
