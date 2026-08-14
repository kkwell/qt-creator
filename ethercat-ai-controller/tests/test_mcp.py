from __future__ import annotations

import unittest

from controller_tools_v1 import MockRepository, ReadOnlyGateway
from controller_tools_v1.mcp import MCP_PROTOCOL_VERSION, McpDispatcher


class McpTests(unittest.TestCase):
    def setUp(self) -> None:
        self.gateway = ReadOnlyGateway(MockRepository())
        self.mcp = McpDispatcher(self.gateway)

    def initialize(self) -> str:
        response = self.mcp.handle(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": MCP_PROTOCOL_VERSION,
                    "capabilities": {},
                    "clientInfo": {"name": "unit-test", "version": "1.0"},
                },
            },
            session_id=None,
            protocol_version=None,
        )
        self.assertEqual(response.status, 200)
        self.assertEqual(
            response.body["result"]["protocolVersion"], MCP_PROTOCOL_VERSION
        )
        return response.headers["MCP-Session-Id"]

    def initialize_session(self) -> str:
        session_id = self.initialize()
        response = self.mcp.handle(
            {
                "jsonrpc": "2.0",
                "method": "notifications/initialized",
                "params": {},
            },
            session_id=session_id,
            protocol_version=MCP_PROTOCOL_VERSION,
        )
        self.assertEqual(response.status, 202)
        self.assertIsNone(response.body)
        return session_id

    def test_lifecycle_and_tools_list(self) -> None:
        session_id = self.initialize_session()
        response = self.mcp.handle(
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
            session_id=session_id,
            protocol_version=MCP_PROTOCOL_VERSION,
        )
        self.assertEqual(response.status, 200)
        self.assertEqual(len(response.body["result"]["tools"]), 9)

    def test_tool_call_returns_text_and_structured_content(self) -> None:
        session_id = self.initialize_session()
        response = self.mcp.handle(
            {
                "jsonrpc": "2.0",
                "id": "call-1",
                "method": "tools/call",
                "params": {
                    "name": "controller.list",
                    "arguments": {"operationId": "mcp-operation-001"},
                },
            },
            session_id=session_id,
            protocol_version=MCP_PROTOCOL_VERSION,
        )
        result = response.body["result"]
        self.assertFalse(result["isError"])
        self.assertEqual(result["content"][0]["type"], "text")
        self.assertEqual(
            result["structuredContent"]["audit"]["sessionId"], session_id
        )

    def test_tool_execution_error_stays_inside_tool_result(self) -> None:
        session_id = self.initialize_session()
        response = self.mcp.handle(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": "controller.get-state",
                    "arguments": {"controllerId": "missing.controller"},
                },
            },
            session_id=session_id,
            protocol_version=MCP_PROTOCOL_VERSION,
        )
        self.assertEqual(response.status, 200)
        self.assertTrue(response.body["result"]["isError"])
        self.assertFalse(response.body["result"]["structuredContent"]["ok"])

    def test_unknown_tool_is_protocol_error(self) -> None:
        session_id = self.initialize_session()
        response = self.mcp.handle(
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {"name": "controller.deploy", "arguments": {}},
            },
            session_id=session_id,
            protocol_version=MCP_PROTOCOL_VERSION,
        )
        self.assertEqual(response.body["error"]["code"], -32602)

    def test_capabilities_require_initialized_notification(self) -> None:
        session_id = self.initialize()
        response = self.mcp.handle(
            {"jsonrpc": "2.0", "id": 5, "method": "tools/list", "params": {}},
            session_id=session_id,
            protocol_version=MCP_PROTOCOL_VERSION,
        )
        self.assertEqual(response.status, 400)
        self.assertEqual(response.body["error"]["code"], -32002)

    def test_subsequent_request_requires_protocol_header(self) -> None:
        session_id = self.initialize_session()
        response = self.mcp.handle(
            {"jsonrpc": "2.0", "id": 6, "method": "ping"},
            session_id=session_id,
            protocol_version=None,
        )
        self.assertEqual(response.status, 400)
        self.assertEqual(response.body["error"]["code"], -32602)

    def test_unsupported_initialize_version_is_rejected(self) -> None:
        response = self.mcp.handle(
            {
                "jsonrpc": "2.0",
                "id": 7,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {},
                    "clientInfo": {"name": "old-client", "version": "1"},
                },
            },
            session_id=None,
            protocol_version=None,
        )
        self.assertEqual(response.status, 400)
        self.assertEqual(response.body["error"]["data"]["supported"], ["2025-11-25"])

    def test_session_can_be_terminated(self) -> None:
        session_id = self.initialize_session()
        self.assertTrue(self.mcp.close_session(session_id))
        self.assertFalse(self.mcp.close_session(session_id))
        self.assertEqual(self.mcp.session_count, 0)


if __name__ == "__main__":
    unittest.main()
