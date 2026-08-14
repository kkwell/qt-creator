"""Loopback-only HTTP facade for REST and MCP."""

from __future__ import annotations

import ipaddress
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import unquote, urlparse

from .gateway import Actor, ReadOnlyGateway
from .mcp import McpDispatcher, McpResponse
from .schema import load_json


MAX_REQUEST_BYTES = 2 * 1024 * 1024


def is_loopback_host(host: str) -> bool:
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def valid_origin(origin: str | None) -> bool:
    if origin is None:
        return True
    parsed = urlparse(origin)
    if parsed.scheme not in {"http", "https"}:
        return False
    return bool(parsed.hostname and is_loopback_host(parsed.hostname))


class GatewayHttpServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        server_address: tuple[str, int],
        gateway: ReadOnlyGateway,
    ):
        self.gateway = gateway
        self.mcp = McpDispatcher(gateway)
        super().__init__(server_address, GatewayRequestHandler)


class GatewayRequestHandler(BaseHTTPRequestHandler):
    server: GatewayHttpServer
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        if not self._origin_allowed():
            return
        path = urlparse(self.path).path
        if path == "/healthz":
            self._send_json(
                200,
                {
                    "status": "ok",
                    "mock": True,
                    "readOnly": True,
                    "hardwareConnected": False,
                },
            )
            return
        if path == "/mcp":
            self._send_json(
                405,
                {
                    "error": "SSE listening is not implemented by this minimal server."
                },
                {"Allow": "GET, POST, DELETE"},
            )
            return
        prefix = "/api/controller-tools/v1"
        if path == f"{prefix}/openapi.json":
            document = load_json(
                self.server.gateway.repository.root
                / "api"
                / "controller-tools-v1.openapi.json"
            )
            self._send_json(200, document)
            return

        tool_name: str | None = None
        arguments: dict[str, Any] = {}
        if path == f"{prefix}/protocol":
            tool_name = "gateway.get-protocol"
        elif path == f"{prefix}/controllers":
            tool_name = "controller.list"
        elif path == f"{prefix}/adapters":
            tool_name = "adapter.list"
        else:
            segments = [unquote(segment) for segment in path.split("/") if segment]
            base = ["api", "controller-tools", "v1", "controllers"]
            if segments[:4] == base and len(segments) >= 6:
                controller_id = segments[4]
                leaf = segments[5]
                arguments["controllerId"] = controller_id
                if len(segments) == 6 and leaf == "capabilities":
                    tool_name = "controller.get-capabilities"
                elif len(segments) == 6 and leaf == "state":
                    tool_name = "controller.get-state"
                elif len(segments) == 6 and leaf == "topology":
                    tool_name = "controller.get-topology"
                elif len(segments) == 6 and leaf == "diagnostics":
                    tool_name = "controller.get-diagnostics"
                elif len(segments) == 7 and leaf == "devices":
                    try:
                        arguments["position"] = int(segments[6], 10)
                    except ValueError:
                        self._send_json(400, {"error": "position must be an integer"})
                        return
                    tool_name = "controller.get-device"
        if tool_name is None:
            self._send_json(404, {"error": "route not found"})
            return
        self._dispatch_rest(tool_name, arguments)

    def do_POST(self) -> None:
        if not self._origin_allowed():
            return
        path = urlparse(self.path).path
        if path == "/mcp":
            self._post_mcp()
            return
        if path == "/api/controller-tools/v1/artifacts/validate":
            body = self._read_json_body()
            if body is None:
                return
            if not isinstance(body, dict):
                self._send_json(400, {"error": "request body must be an object"})
                return
            self._dispatch_rest("artifact.validate", body)
            return
        self._send_json(404, {"error": "route not found"})

    def do_DELETE(self) -> None:
        if not self._origin_allowed():
            return
        if urlparse(self.path).path != "/mcp":
            self._send_json(404, {"error": "route not found"})
            return
        session_id = self.headers.get("MCP-Session-Id")
        if self.server.mcp.close_session(session_id):
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self._send_json(404, {"error": "MCP session not found"})

    def _post_mcp(self) -> None:
        content_type = self.headers.get("Content-Type", "")
        accept = self.headers.get("Accept", "").lower()
        if not content_type.lower().startswith("application/json"):
            self._send_json(415, {"error": "Content-Type must be application/json"})
            return
        if "application/json" not in accept or "text/event-stream" not in accept:
            self._send_json(
                406,
                {
                    "error": (
                        "Accept must list application/json and text/event-stream."
                    )
                },
            )
            return
        message = self._read_json_body()
        if message is None:
            return
        response = self.server.mcp.handle(
            message,
            session_id=self.headers.get("MCP-Session-Id"),
            protocol_version=self.headers.get("MCP-Protocol-Version"),
        )
        self._send_mcp_response(response)

    def _send_mcp_response(self, response: McpResponse) -> None:
        if response.body is None:
            self.send_response(response.status)
            for key, value in response.headers.items():
                self.send_header(key, value)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self._send_json(response.status, response.body, response.headers)

    def _dispatch_rest(
        self, tool_name: str, arguments: dict[str, Any]
    ) -> None:
        actor = Actor(
            user_id=self.headers.get("X-User-Id", "anonymous-local-mock")[:128],
            client_id=self.headers.get("X-AI-Client-Id", "rest-client")[:128],
            session_id=self.headers.get("X-Session-Id", "rest-stateless")[:128],
        )
        envelope = self.server.gateway.dispatch(tool_name, arguments, actor)
        status = 200
        if not envelope["ok"]:
            code = envelope["error"]["code"]
            status = 404 if code == "CT009_NOT_FOUND" else 400
        self._send_json(status, envelope)

    def _read_json_body(self) -> Any | None:
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._send_json(400, {"error": "invalid Content-Length"})
            return None
        if content_length <= 0 or content_length > MAX_REQUEST_BYTES:
            self._send_json(
                413 if content_length > MAX_REQUEST_BYTES else 400,
                {"error": "request body size is invalid"},
            )
            return None
        payload = self.rfile.read(content_length)
        try:
            return json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._send_json(400, {"error": "request body is not valid UTF-8 JSON"})
            return None

    def _origin_allowed(self) -> bool:
        origin = self.headers.get("Origin")
        if valid_origin(origin):
            return True
        self._send_json(
            403,
            {
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32000, "message": "Origin is not allowed."},
            },
        )
        return False

    def _send_json(
        self,
        status: int,
        document: Any,
        headers: dict[str, str] | None = None,
    ) -> None:
        payload = json.dumps(
            document, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format: str, *args: Any) -> None:
        return


def create_server(
    host: str,
    port: int,
    gateway: ReadOnlyGateway | None = None,
) -> GatewayHttpServer:
    if not is_loopback_host(host):
        raise ValueError("The mock gateway may bind only to a loopback address.")
    if not 0 <= port <= 65535:
        raise ValueError("Port must be in 0..65535.")
    return GatewayHttpServer((host, port), gateway or ReadOnlyGateway())
