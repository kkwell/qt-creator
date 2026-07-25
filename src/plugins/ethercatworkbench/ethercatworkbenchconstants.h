// Copyright (C) 2026 Kvell

#pragma once

namespace EtherCAT::Workbench::Constants {

const char PLUGIN_ID[] = "ethercatworkbench";
const char MODE_ID[] = "EtherCAT.Workbench.Mode";
const char CONTEXT_ID[] = "EtherCAT.Workbench.Context";
const char CONTROLLER_CONTROL_CONTEXT_ID[] = "EtherCAT.ControllerControl.Context";
const char CONTROLLER_OUTPUT_CHANNEL_ID[] = "EtherCAT.Controller.Output";
const char NAVIGATION_ID[] = "EtherCAT.Workbench.Tree";
const char MENU_ID[] = "EtherCAT.Menu";
const char OPEN_ACTION_ID[] = "EtherCAT.Workbench.Open";
const char REFRESH_ACTION_ID[] = "EtherCAT.Workbench.Refresh";
const char CONNECT_CONTROLLER_ACTION_ID[] = "EtherCAT.Workbench.ConnectController";
const char REFRESH_CONTROLLER_ACTION_ID[] = "EtherCAT.Workbench.RefreshController";
const char DISCONNECT_CONTROLLER_ACTION_ID[] = "EtherCAT.Workbench.DisconnectController";
const char DEBUG_ACTION_ID[] = "Debugger.Debug";
const char CONTROLLED_STOP_ACTION_ID[] = "EtherCAT.Workbench.ControlledStop";
const char EXPAND_ACTION_ID[] = "EtherCAT.Workbench.ExpandAll";
const char COLLAPSE_ACTION_ID[] = "EtherCAT.Workbench.CollapseAll";
const char LOCATE_DIFFERENCE_ACTION_ID[] = "EtherCAT.Workbench.LocateDifference";
const char LOCATE_ISSUE_ACTION_ID[] = "EtherCAT.Workbench.LocateIssue";
const char OPEN_DIAGNOSTICS_ACTION_ID[] = "EtherCAT.Workbench.OpenDiagnostics";
const char LOCATE_UNSUPPORTED_DEVICE_ACTION_ID[] = "EtherCAT.Workbench.LocateUnsupportedDevice";
const char COPY_NODE_ID_ACTION_ID[] = "EtherCAT.Workbench.CopyNodeId";
const char SET_ACTIVE_PROJECT_ACTION_ID[] = "EtherCAT.Workbench.SetActiveProject";
const char INSERT_DEVICE_ACTION_ID[] = "EtherCAT.Workbench.InsertDevice";
const char ADD_DEVICE_TO_MASTER_ACTION_ID[] = "EtherCAT.Workbench.AddDeviceToMaster";
const char REMOVE_OFFLINE_SLAVE_ACTION_ID[] = "EtherCAT.Workbench.RemoveOfflineSlave";
const char MOVE_OFFLINE_SLAVE_UP_ACTION_ID[] = "EtherCAT.Workbench.MoveOfflineSlaveUp";
const char MOVE_OFFLINE_SLAVE_DOWN_ACTION_ID[] = "EtherCAT.Workbench.MoveOfflineSlaveDown";

const char BUILTIN_PAGE_PROVIDER_ID[] = "EtherCAT.Workbench.BuiltinPages";
const char GENERAL_PAGE_ID[] = "EtherCAT.Workbench.General";
const char COMMUNICATION_PAGE_ID[] = "EtherCAT.Workbench.Communication";
const char ETHERCAT_PAGE_ID[] = "EtherCAT.Workbench.EtherCAT";
const char PROCESS_DATA_PAGE_ID[] = "EtherCAT.Workbench.ProcessData";
const char COE_ONLINE_PAGE_ID[] = "EtherCAT.Workbench.CoEOnline";
const char STARTUP_PAGE_ID[] = "EtherCAT.Workbench.Startup";
const char DC_PAGE_ID[] = "EtherCAT.Workbench.DC";
const char ONLINE_PAGE_ID[] = "EtherCAT.Workbench.Online";
const char DIAGNOSTICS_PAGE_ID[] = "EtherCAT.Workbench.Diagnostics";

constexpr int MODE_PRIORITY = 78;

} // namespace EtherCAT::Workbench::Constants
