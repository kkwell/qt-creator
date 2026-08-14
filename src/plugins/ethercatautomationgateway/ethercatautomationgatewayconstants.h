// Copyright (C) 2026 Kvell

#pragma once

namespace EtherCAT::AutomationGateway::Constants {

constexpr char PLUGIN_ID[] = "ethercatautomationgateway";
constexpr char MCP_MANAGER_ID[] = "ethercat-controller-tools-v1";
constexpr char SETTINGS_GROUP[] = "EtherCATAutomationGateway";
constexpr char SETTINGS_ENABLED[] = "Enabled";
constexpr char SETTINGS_MCP_PORT[] = "McpPort";
constexpr char SETTINGS_REST_PORT[] = "RestPort";
constexpr char SETTINGS_PAGE_ID[] = "EtherCAT.AutomationGateway";
constexpr char LOOPBACK_ADDRESS[] = "127.0.0.1";
constexpr char CONTROLLER_OUTPUT_CHANNEL_ID[] = "EtherCAT.Controller.Output";
constexpr char SETTINGS_PAGE_OBJECT_NAME[] = "EtherCATGatewaySettingsPage";
constexpr char SETTINGS_ENABLED_OBJECT_NAME[] = "EtherCATGatewayEnabledCheckBox";
constexpr char SETTINGS_ADDRESS_OBJECT_NAME[] = "EtherCATGatewayLoopbackAddress";
constexpr char SETTINGS_MCP_PORT_OBJECT_NAME[] = "EtherCATGatewayMcpPort";
constexpr char SETTINGS_REST_PORT_OBJECT_NAME[] = "EtherCATGatewayRestPort";
constexpr char SETTINGS_STATE_OBJECT_NAME[] = "EtherCATGatewayRuntimeState";
constexpr char SETTINGS_MCP_ENDPOINT_OBJECT_NAME[] = "EtherCATGatewayMcpEndpoint";
constexpr char SETTINGS_REST_ENDPOINT_OBJECT_NAME[] = "EtherCATGatewayRestEndpoint";
constexpr char SETTINGS_ERROR_OBJECT_NAME[] = "EtherCATGatewayLastError";
constexpr char SETTINGS_SAFETY_OBJECT_NAME[] = "EtherCATGatewaySafetyNotice";
constexpr char API_VERSION[] = "controller-tools/v1";
constexpr char MCP_PROTOCOL_VERSION[] = "2025-11-25";
constexpr char MCP_RESOURCE_PATH[]
    = ":/ethercatautomationgateway/controller-tools-v1.mcp-tools.json";
constexpr char OPENAPI_RESOURCE_PATH[]
    = ":/ethercatautomationgateway/controller-tools-v1.openapi.json";
constexpr int DEFAULT_MCP_PORT = 8765;
constexpr int DEFAULT_REST_PORT = 8766;

} // namespace EtherCAT::AutomationGateway::Constants
