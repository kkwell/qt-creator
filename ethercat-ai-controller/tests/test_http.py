from __future__ import annotations

import http.client
import json
import threading
import unittest

from controller_tools_v1.http_server import create_server
from controller_tools_v1.mcp import MCP_PROTOCOL_VERSION


class HttpServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server = create_server("127.0.0.1", 0)
        cls.host, cls.port = cls.server.server_address[:2]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def request(
        self,
        method: str,
        path: str,
        body=None,
        headers: dict[str, str] | None = None,
    ):
        payload = None
        merged = dict(headers or {})
        if body is not None:
            payload = json.dumps(body).encode("utf-8")
            merged.setdefault("Content-Type", "application/json")
        connection = http.client.HTTPConnection(self.host, self.port, timeout=2)
        connection.request(method, path, body=payload, headers=merged)
        response = connection.getresponse()
        response_body = response.read()
        result_headers = dict(response.getheaders())
        connection.close()
        document = json.loads(response_body) if response_body else None
        return response.status, result_headers, document

    def test_health_and_rest_are_loopback_mock_only(self) -> None:
        status, _, health = self.request("GET", "/healthz")
        self.assertEqual(status, 200)
        self.assertTrue(health["mock"])
        self.assertFalse(health["hardwareConnected"])

        status, _, controllers = self.request(
            "GET", "/api/controller-tools/v1/controllers"
        )
        self.assertEqual(status, 200)
        self.assertTrue(controllers["ok"])
        self.assertTrue(controllers["audit"]["readOnly"])

    def test_rest_validation_does_not_store_invalid_artifact(self) -> None:
        artifact = self.server.gateway.repository.intent_copy()
        artifact["spec"]["program"]["nodes"][0]["timeoutMs"] = 0
        status, _, result = self.request(
            "POST",
            "/api/controller-tools/v1/artifacts/validate",
            {"artifact": artifact},
        )
        self.assertEqual(status, 200)
        self.assertTrue(result["ok"])
        self.assertFalse(result["data"]["report"]["valid"])
        self.assertFalse(result["data"]["stored"])

    def test_non_loopback_origin_is_rejected(self) -> None:
        status, _, result = self.request(
            "GET",
            "/healthz",
            headers={"Origin": "https://attacker.example"},
        )
        self.assertEqual(status, 403)
        self.assertEqual(result["error"]["code"], -32000)

    def test_mcp_get_declines_sse(self) -> None:
        status, _, result = self.request(
            "GET", "/mcp", headers={"Accept": "text/event-stream"}
        )
        self.assertEqual(status, 405)
        self.assertIn("not implemented", result["error"])

    def test_mcp_streamable_http_json_lifecycle(self) -> None:
        accept = "application/json, text/event-stream"
        status, headers, initialized = self.request(
            "POST",
            "/mcp",
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": MCP_PROTOCOL_VERSION,
                    "capabilities": {},
                    "clientInfo": {"name": "http-test", "version": "1.0"},
                },
            },
            {"Accept": accept},
        )
        self.assertEqual(status, 200)
        session_id = headers["MCP-Session-Id"]
        self.assertEqual(
            initialized["result"]["protocolVersion"], MCP_PROTOCOL_VERSION
        )
        common = {
            "Accept": accept,
            "MCP-Session-Id": session_id,
            "MCP-Protocol-Version": MCP_PROTOCOL_VERSION,
        }
        status, _, body = self.request(
            "POST",
            "/mcp",
            {
                "jsonrpc": "2.0",
                "method": "notifications/initialized",
                "params": {},
            },
            common,
        )
        self.assertEqual(status, 202)
        self.assertIsNone(body)
        status, _, listed = self.request(
            "POST",
            "/mcp",
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
            common,
        )
        self.assertEqual(status, 200)
        self.assertEqual(len(listed["result"]["tools"]), 9)
        status, _, _ = self.request("DELETE", "/mcp", headers=common)
        self.assertEqual(status, 204)

    def test_mock_server_refuses_non_loopback_bind(self) -> None:
        with self.assertRaisesRegex(ValueError, "loopback"):
            create_server("0.0.0.0", 0)


if __name__ == "__main__":
    unittest.main()
