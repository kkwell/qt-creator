// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/devicedescription.h>
#include <ethercatdata/projectsnapshot.h>

namespace EtherCAT::Workbench::Internal {

Data::ProcessDataConfiguration processDataDefaultsFromDevice(
    const Data::DeviceDescription &device, const Data::NodeId &ownerId);
Data::StartupConfiguration startupDefaultsFromDevice(
    const Data::DeviceDescription &device, const Data::NodeId &ownerId);
Data::DcConfiguration dcConfigurationFromMode(const Data::DcModeDescription &mode);
Data::DcConfiguration dcDefaultsFromDevice(const Data::DeviceDescription &device);
Data::OfflineSlaveConfiguration offlineSlaveFromDevice(
    const Data::DeviceDescription &device,
    const Data::NodeId &slaveId,
    const Data::NodeId &masterId,
    int position,
    const QString &name);

} // namespace EtherCAT::Workbench::Internal
