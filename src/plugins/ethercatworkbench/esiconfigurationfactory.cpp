// Copyright (C) 2026 Kvell

#include "esiconfigurationfactory.h"

#include "ethercatworkbenchtr.h"

#include <QUuid>

#include <algorithm>
#include <iterator>

namespace EtherCAT::Workbench::Internal {

static Data::NodeId derivedId(
    const Data::NodeId &ownerId, const QUuid &namespaceId, const QString &key)
{
    const QByteArray name = ownerId.toString().toUtf8() + ':' + key.toUtf8();
    return Data::NodeId::fromString(QUuid::createUuidV5(namespaceId, name).toString());
}

static Data::SyncManagerDirection expectedSyncManagerDirection(Data::PdoDirection direction)
{
    return direction == Data::PdoDirection::Rx ? Data::SyncManagerDirection::MasterToSlave
                                               : Data::SyncManagerDirection::SlaveToMaster;
}

Data::ProcessDataConfiguration processDataDefaultsFromDevice(
    const Data::DeviceDescription &device, const Data::NodeId &ownerId)
{
    static const QUuid namespaceId("{fb23a9ad-4932-57e0-b301-9c581ad7a953}");
    Data::ProcessDataConfiguration configuration;
    for (int row = 0; row < device.syncManagers.size(); ++row) {
        const Data::SyncManagerDescription &source = device.syncManagers.at(row);
        configuration.syncManagers.append(
            {derivedId(ownerId, namespaceId, QString("sm:%1:%2").arg(source.index).arg(row)),
             source.index,
             source.name,
             source.direction,
             source.enabled,
             source.defaultSize});
    }

    const auto appendPdos =
        [&configuration,
         &ownerId](const QList<Data::PdoDescription> &sources, Data::PdoDirection direction) {
            for (int pdoRow = 0; pdoRow < sources.size(); ++pdoRow) {
                const Data::PdoDescription &source = sources.at(pdoRow);
                Data::PdoConfiguration pdo;
                pdo.id = derivedId(
                    ownerId,
                    namespaceId,
                    QString("pdo:%1:%2:%3")
                        .arg(direction == Data::PdoDirection::Rx ? "rx" : "tx")
                        .arg(source.index)
                        .arg(pdoRow));
                pdo.index = source.index;
                pdo.name = source.name;
                pdo.direction = direction;
                pdo.syncManager = source.syncManager;
                pdo.fixed = source.fixed;
                pdo.mandatory = source.mandatory;
                for (int entryRow = 0; entryRow < source.entries.size(); ++entryRow) {
                    const Data::PdoEntryDescription &sourceEntry = source.entries.at(entryRow);
                    pdo.entries.append(
                        {derivedId(
                             ownerId,
                             namespaceId,
                             QString("entry:%1:%2:%3")
                                 .arg(pdo.id.toString())
                                 .arg(sourceEntry.index)
                                 .arg(entryRow)),
                         sourceEntry.index,
                         sourceEntry.subIndex,
                         sourceEntry.name,
                         sourceEntry.bitLength,
                         sourceEntry.dataType,
                         sourceEntry.rawDataType,
                         -1,
                         true,
                         false});
                }
                configuration.pdos.append(pdo);
            }
        };
    appendPdos(device.rxPdos, Data::PdoDirection::Rx);
    appendPdos(device.txPdos, Data::PdoDirection::Tx);

    const auto findSyncManager = [&configuration](int index) {
        return std::find_if(
            configuration.syncManagers.begin(),
            configuration.syncManagers.end(),
            [index](const auto &syncManager) { return syncManager.index == index; });
    };
    const auto nextSyncManagerIndex = [&configuration] {
        int result = 0;
        for (const Data::SyncManagerConfiguration &syncManager : configuration.syncManagers)
            result = qMax(result, syncManager.index + 1);
        return result;
    };

    for (Data::PdoConfiguration &pdo : configuration.pdos) {
        const Data::SyncManagerDirection expected = expectedSyncManagerDirection(pdo.direction);
        auto syncManager = findSyncManager(pdo.syncManager);
        if (syncManager != configuration.syncManagers.end()
            && syncManager->direction == Data::SyncManagerDirection::Unknown) {
            syncManager->direction = expected;
        }
        if (syncManager == configuration.syncManagers.end() || syncManager->direction != expected) {
            pdo.mappingSupported = false;
            syncManager = std::find_if(
                configuration.syncManagers.begin(),
                configuration.syncManagers.end(),
                [expected](const auto &candidate) { return candidate.direction == expected; });
            if (syncManager == configuration.syncManagers.end()) {
                const int index = nextSyncManagerIndex();
                configuration.syncManagers.append(
                    {derivedId(ownerId, namespaceId, QString("synthetic-sm:%1").arg(index)),
                     index,
                     expected == Data::SyncManagerDirection::MasterToSlave
                         ? Tr::tr("Unassigned outputs")
                         : Tr::tr("Unassigned inputs"),
                     expected,
                     true,
                     0});
                syncManager = std::prev(configuration.syncManagers.end());
            }
            pdo.syncManager = syncManager->index;
        }
        syncManager->enabled = true;
        if (pdo.index == 0 || pdo.entries.isEmpty())
            pdo.mappingSupported = false;
        pdo.selected = pdo.mappingSupported && (pdo.fixed || pdo.mandatory);
    }

    for (const Data::SyncManagerConfiguration &syncManager : configuration.syncManagers) {
        const auto firstSupported = std::find_if(
            configuration.pdos.begin(), configuration.pdos.end(), [&syncManager](const auto &pdo) {
                return pdo.syncManager == syncManager.index && pdo.mappingSupported;
            });
        if (firstSupported == configuration.pdos.end())
            continue;
        const bool haveDefault = std::any_of(
            configuration.pdos.cbegin(), configuration.pdos.cend(), [&syncManager](const auto &pdo) {
                return pdo.syncManager == syncManager.index && pdo.selected;
            });
        if (!haveDefault)
            firstSupported->selected = true;
    }
    for (Data::PdoConfiguration &pdo : configuration.pdos)
        pdo.defaultSelected = pdo.selected;
    return configuration;
}

Data::StartupConfiguration startupDefaultsFromDevice(
    const Data::DeviceDescription &device, const Data::NodeId &ownerId)
{
    static const QUuid namespaceId("{2851c5bd-f1de-5f43-97fa-501589ddf4ad}");
    Data::StartupConfiguration configuration;
    configuration.parameters.reserve(device.startupParameters.size());
    for (int row = 0; row < device.startupParameters.size(); ++row) {
        const Data::StartupParameterDescription &source = device.startupParameters.at(row);
        configuration.parameters.append(
            {derivedId(
                 ownerId,
                 namespaceId,
                 QString("startup:%1:%2:%3:%4")
                     .arg(row)
                     .arg(source.transition)
                     .arg(source.index)
                     .arg(source.subIndex)),
             true,
             row,
             source.transition,
             source.index,
             source.subIndex,
             Data::EtherCATDataType::Unknown,
             {},
             source.data,
             source.comment});
    }
    return configuration;
}

Data::DcConfiguration dcConfigurationFromMode(const Data::DcModeDescription &mode)
{
    Data::DcConfiguration configuration;
    configuration.enabled = true;
    configuration.modeName = mode.name;
    configuration.assignActivate = mode.assignActivate;
    configuration.sync0.enabled = mode.cycleTimeSync0Ns > 0;
    configuration.sync0.cycleTimeNs = mode.cycleTimeSync0Ns;
    configuration.sync0.shiftTimeNs = mode.shiftTimeSync0Ns;
    configuration.sync1.enabled = mode.cycleTimeSync1Ns > 0;
    configuration.sync1.cycleTimeNs = mode.cycleTimeSync1Ns;
    configuration.sync1.shiftTimeNs = mode.shiftTimeSync1Ns;
    return configuration;
}

Data::DcConfiguration dcDefaultsFromDevice(const Data::DeviceDescription &device)
{
    return device.dcModes.isEmpty() ? Data::DcConfiguration{}
                                    : dcConfigurationFromMode(device.dcModes.first());
}

Data::OfflineSlaveConfiguration offlineSlaveFromDevice(
    const Data::DeviceDescription &device,
    const Data::NodeId &slaveId,
    const Data::NodeId &masterId,
    int position,
    const QString &name)
{
    return {
        slaveId,
        masterId,
        position,
        device.summary.identity,
        0,
        0,
        name,
        device.summary.id,
        processDataDefaultsFromDevice(device, slaveId),
        startupDefaultsFromDevice(device, slaveId),
        dcDefaultsFromDevice(device)};
}

} // namespace EtherCAT::Workbench::Internal
