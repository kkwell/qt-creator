// Copyright (C) 2026 Kvell

#pragma once

namespace EtherCAT::Diagnostics::Constants {

const char PLUGIN_ID[] = "ethercatdiagnostics";
const char PROVIDER_ID[] = "EtherCAT.Diagnostics.MockProvider";
const char PAGE_PROVIDER_ID[] = "EtherCAT.Diagnostics.Pages";
const char STATUS_SOURCE_ID[] = "EtherCAT.Diagnostics.Status";

const char START_ACTION_ID[] = "EtherCAT.Diagnostics.Start";
const char STOP_ACTION_ID[] = "EtherCAT.Diagnostics.Stop";
const char CONFIG_ACTION_ID[] = "EtherCAT.Diagnostics.Config";
const char FREE_RUN_ACTION_ID[] = "EtherCAT.Diagnostics.FreeRun";
const char RUN_ACTION_ID[] = "EtherCAT.Diagnostics.Run";
const char ACKNOWLEDGE_ACTION_ID[] = "EtherCAT.Diagnostics.Acknowledge";
const char CLEAR_RECOVERED_ACTION_ID[] = "EtherCAT.Diagnostics.ClearRecovered";

const char MASTER_PAGE_ID[] = "EtherCAT.Diagnostics.Master";
const char SLAVES_PAGE_ID[] = "EtherCAT.Diagnostics.Slaves";
const char SLAVE_ONLINE_PAGE_ID[] = "EtherCAT.Diagnostics.SlaveOnline";
const char WKC_PAGE_ID[] = "EtherCAT.Diagnostics.WKC";
const char DC_PAGE_ID[] = "EtherCAT.Diagnostics.DC";
const char PORTS_PAGE_ID[] = "EtherCAT.Diagnostics.Ports";
const char COUNTERS_PAGE_ID[] = "EtherCAT.Diagnostics.Counters";
const char EVENTS_PAGE_ID[] = "EtherCAT.Diagnostics.Events";
const char PERFORMANCE_PAGE_ID[] = "EtherCAT.Diagnostics.Performance";

} // namespace EtherCAT::Diagnostics::Constants
