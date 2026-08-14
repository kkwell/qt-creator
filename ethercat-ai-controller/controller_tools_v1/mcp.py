"""Minimal MCP 2025-11-25 Streamable HTTP JSON-response dispatcher."""

from __future__ import annotations

import json
import secrets
import threading
from dataclasses import dataclass
from typing import Any

from .gateway import Actor, ReadOnlyGateway


MCP_PROTOCOL_VERSION = "2025-11-25"


@dataclass
class McpSession:
    session_id: str
    client_name: str
    client_version: str
    initialized: bool = False


@dataclass(frozen=True)
class McpResponse:
    status: int
    body: dict[str, Any] | None
    headers: dict[str, str]


class McpDispatcher:
    def __init__(self, gateway: ReadOnlyGateway):
        self.gateway = gateway
        self._sessions: dict[str, McpSession] = {}
        self._lock = threading.Lock()

    @property
    def session_count(self) -> int:
        with self._lock:
            return len(self._sessions)

    def handle(
        self,
        message: Any,
        *,
        session_id: str | None,
        protocol_version: str | None,
    ) -> McpResponse:
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
            return self._protocol_error(None, -32600, "Invalid JSON-RPC request.", 400)
        method = message.get("method")
        request_id = message.get("id")
        is_notification = "id" not in message
        if not isinstance(method, str):
            return self._protocol_error(
                request_id, -32600, "JSON-RPC method must be a string.", 400
            )

        if method == "initialize":
            if is_notification:
                return self._protocol_error(
                    None, -32600, "initialize must be a request.", 400
                )
            return self._initialize(message, session_id)

        session = self._session(session_id)
        if session is None:
            return self._protocol_error(
                request_id,
                -32001,
                "A valid MCP-Session-Id is required after initialize.",
                404 if session_id else 400,
            )
        if protocol_version != MCP_PROTOCOL_VERSION:
            return self._protocol_error(
                request_id,
                -32602,
                f"MCP-Protocol-Version must be {MCP_PROTOCOL_VERSION}.",
                400,
                {"supported": [MCP_PROTOCOL_VERSION]},
            )

        if method == "notifications/initialized":
            if not is_notification:
                return self._protocol_error(
                    request_id,
                    -32600,
                    "notifications/initialized must not contain an id.",
                    400,
                )
            with self._lock:
                current = self._sessions.get(session.session_id)
                if current:
                    current.initialized = True
            return McpResponse(202, None, {})

        if not session.initialized:
            return self._protocol_error(
                request_id,
                -32002,
                "Send notifications/initialized before using server capabilities.",
                400,
            )

        if is_notification:
            if method in {"notifications/cancelled", "notifications/progress"}:
                return McpResponse(202, None, {})
            return McpResponse(202, None, {})

        if method == "ping":
            return self._success(request_id, {})
        if method == "tools/list":
            params = message.get("params", {})
            if params is not None and not isinstance(params, dict):
                return self._protocol_error(
                    request_id, -32602, "tools/list params must be an object.", 400
                )
            if params and params.get("cursor") is not None:
                return self._protocol_error(
                    request_id,
                    -32602,
                    "This fixed tool catalogue does not paginate.",
                    400,
                )
            return self._success(request_id, {"tools": self.gateway.tool_definitions})
        if method == "tools/call":
            return self._call_tool(request_id, message.get("params"), session)
        return self._protocol_error(
            request_id, -32601, f"Method {method!r} is not supported.", 200
        )

    def close_session(self, session_id: str | None) -> bool:
        if not session_id:
            return False
        with self._lock:
            return self._sessions.pop(session_id, None) is not None

    def _initialize(
        self, message: dict[str, Any], supplied_session_id: str | None
    ) -> McpResponse:
        request_id = message.get("id")
        if supplied_session_id:
            return self._protocol_error(
                request_id,
                -32602,
                "Initialize must not reuse an MCP session.",
                400,
            )
        params = message.get("params")
        if not isinstance(params, dict):
            return self._protocol_error(
                request_id, -32602, "Initialize params are required.", 400
            )
        requested = params.get("protocolVersion")
        client_info = params.get("clientInfo")
        capabilities = params.get("capabilities")
        if requested != MCP_PROTOCOL_VERSION:
            return self._protocol_error(
                request_id,
                -32602,
                f"Only MCP {MCP_PROTOCOL_VERSION} is supported.",
                400,
                {"supported": [MCP_PROTOCOL_VERSION]},
            )
        if not isinstance(client_info, dict) or not isinstance(
            client_info.get("name"), str
        ):
            return self._protocol_error(
                request_id, -32602, "clientInfo.name is required.", 400
            )
        if not isinstance(capabilities, dict):
            return self._protocol_error(
                request_id, -32602, "capabilities must be an object.", 400
            )
        session_id = secrets.token_urlsafe(32)
        session = McpSession(
            session_id=session_id,
            client_name=client_info["name"][:128],
            client_version=str(client_info.get("version", "unknown"))[:64],
        )
        with self._lock:
            self._sessions[session_id] = session
        result = {
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {
                "name": "ethercat-controller-tools-v1",
                "version": "0.1.0",
                "description": "Mock-only read-only EtherCAT Controller AI Gateway",
            },
            "instructions": (
                "All tools are mock-only and read-only. No result is real hardware "
                "evidence. No lease, scan, deployment, start, or motion tool exists."
            ),
        }
        response = self._success(request_id, result)
        return McpResponse(
            response.status,
            response.body,
            {"MCP-Session-Id": session_id},
        )

    def _call_tool(
        self, request_id: Any, params: Any, session: McpSession
    ) -> McpResponse:
        if not isinstance(params, dict):
            return self._protocol_error(
                request_id, -32602, "tools/call params must be an object.", 400
            )
        name = params.get("name")
        arguments = params.get("arguments", {})
        if not isinstance(name, str):
            return self._protocol_error(
                request_id, -32602, "tools/call name is required.", 400
            )
        known_names = {tool["name"] for tool in self.gateway.tool_definitions}
        if name not in known_names:
            return self._protocol_error(
                request_id, -32602, f"Unknown tool {name!r}.", 200
            )
        if not isinstance(arguments, dict):
            return self._protocol_error(
                request_id, -32602, "Tool arguments must be an object.", 400
            )
        envelope = self.gateway.dispatch(
            name,
            arguments,
            Actor(
                user_id="mcp-user-unavailable",
                client_id=f"{session.client_name}/{session.client_version}",
                session_id=session.session_id,
            ),
        )
        text = json.dumps(
            envelope, sort_keys=True, ensure_ascii=False, separators=(",", ":")
        )
        result = {
            "content": [{"type": "text", "text": text}],
            "structuredContent": envelope,
            "isError": not envelope["ok"],
        }
        return self._success(request_id, result)

    def _session(self, session_id: str | None) -> McpSession | None:
        if not session_id:
            return None
        with self._lock:
            return self._sessions.get(session_id)

    @staticmethod
    def _success(request_id: Any, result: dict[str, Any]) -> McpResponse:
        return McpResponse(
            200,
            {"jsonrpc": "2.0", "id": request_id, "result": result},
            {},
        )

    @staticmethod
    def _protocol_error(
        request_id: Any,
        code: int,
        message: str,
        status: int,
        data: dict[str, Any] | None = None,
    ) -> McpResponse:
        error: dict[str, Any] = {"code": code, "message": message}
        if data is not None:
            error["data"] = data
        return McpResponse(
            status,
            {"jsonrpc": "2.0", "id": request_id, "error": error},
            {},
        )
