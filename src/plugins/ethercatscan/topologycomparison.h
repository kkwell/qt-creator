// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/scansnapshot.h>

namespace EtherCAT::Scan::Internal {

Data::TopologyComparison compareTopology(
    const Data::ProjectSnapshot &project,
    const Data::NodeId &masterId,
    const Data::ScanSnapshot &snapshot);

QList<Data::OfflineSlaveConfiguration> offlineConfigurationFromScan(
    const Data::ProjectSnapshot &project,
    const Data::NodeId &masterId,
    const Data::ScanSnapshot &snapshot);

} // namespace EtherCAT::Scan::Internal
