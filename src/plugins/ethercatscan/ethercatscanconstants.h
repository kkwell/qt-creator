// Copyright (C) 2026 Kvell

#pragma once

namespace EtherCAT::Scan::Constants {

const char PLUGIN_ID[] = "ethercatscan";
const char PROVIDER_ID[] = "EtherCAT.Scan.MockProvider";
const char PAGE_PROVIDER_ID[] = "EtherCAT.Scan.Pages";
const char PAGE_ID[] = "EtherCAT.Scan.Page";
const char STATUS_SOURCE_ID[] = "EtherCAT.Scan.Status";

const char SCAN_INTERFACES_ACTION_ID[] = "EtherCAT.Scan.Interfaces";
const char SCAN_SLAVES_ACTION_ID[] = "EtherCAT.Scan.Slaves";
const char RESCAN_BRANCH_ACTION_ID[] = "EtherCAT.Scan.RescanBranch";
const char COMPARE_ACTION_ID[] = "EtherCAT.Scan.Compare";
const char ACCEPT_ACTION_ID[] = "EtherCAT.Scan.Accept";
const char KEEP_ACTION_ID[] = "EtherCAT.Scan.KeepExisting";
const char CANCEL_ACTION_ID[] = "EtherCAT.Scan.Cancel";

} // namespace EtherCAT::Scan::Constants
