// Copyright (C) 2026 Kvell

#pragma once

namespace EtherCAT::AutomationGateway::Constants {

constexpr char PLUGIN_ID[] = "ethercatautomationgateway";
constexpr char MCP_MANAGER_ID[] = "ethercat-controller-tools-v1";
constexpr char SETTINGS_GROUP[] = "EtherCATAutomationGateway";
constexpr char SETTINGS_ENABLED[] = "Enabled";
constexpr char SETTINGS_MCP_PORT[] = "McpPort";
constexpr char SETTINGS_REST_PORT[] = "RestPort";
constexpr char API_VERSION[] = "controller-tools/v1";
constexpr char MCP_PROTOCOL_VERSION[] = "2025-11-25";
constexpr char MCP_RESOURCE_PATH[]
    = ":/ethercatautomationgateway/controller-tools-v1.mcp-tools.json";
constexpr char OPENAPI_RESOURCE_PATH[]
    = ":/ethercatautomationgateway/controller-tools-v1.openapi.json";
constexpr quint16 DEFAULT_MCP_PORT = 8765;
constexpr quint16 DEFAULT_REST_PORT = 8766;

} // namespace EtherCAT::AutomationGateway::Constants
