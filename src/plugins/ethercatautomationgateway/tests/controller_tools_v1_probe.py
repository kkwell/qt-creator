#!/usr/bin/env python3
"""Process-level client probe for the IDE-owned controller-tools/v1 Gateway."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any


MCP_PROTOCOL_VERSION = "2025-11-25"
EXPECTED_TOOLS = sorted(
    [
        "adapter.list",
        "artifact.validate",
        "controller.get-capabilities",
        "controller.get-device",
        "controller.get-diagnostics",
        "controller.get-state",
        "controller.get-topology",
        "controller.list",
        "gateway.get-protocol",
        "runtime.get-context",
        "runtime.operation.get",
        "runtime.operation.request",
        "runtime.read",
    ]
)


class ProbeFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class HttpResult:
    status: int
    headers: Any
    body: dict[str, Any]


def http_json(
    url: str,
    method: str,
    payload: dict[str, Any] | None,
    headers: dict[str, str],
    timeout: float,
) -> HttpResult:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
            return HttpResult(response.status, response.headers, json.loads(raw))
    except urllib.error.HTTPError as error:
        raw = error.read()
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            body = {"raw": raw.decode("utf-8", errors="replace")}
        return HttpResult(error.code, error.headers, body)


class McpClient:
    def __init__(self, endpoint: str, timeout: float) -> None:
        self.endpoint = endpoint
        self.timeout = timeout
        self.session_id = ""
        self.request_id = 0

    def _headers(self) -> dict[str, str]:
        headers = {
            "Accept": "application/json, text/event-stream",
            "Content-Type": "application/json",
            "Origin": "http://localhost",
            "mcp-protocol-version": MCP_PROTOCOL_VERSION,
        }
        if self.session_id:
            headers["mcp-session-id"] = self.session_id
        return headers

    def rpc(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        self.request_id += 1
        result = http_json(
            self.endpoint,
            "POST",
            {
                "jsonrpc": "2.0",
                "id": self.request_id,
                "method": method,
                "params": params,
            },
            self._headers(),
            self.timeout,
        )
        if result.status != 200:
            raise ProbeFailure(f"MCP {method} returned HTTP {result.status}: {result.body}")
        session = result.headers.get("mcp-session-id")
        if session:
            self.session_id = session
        if "error" in result.body:
            raise ProbeFailure(f"MCP {method} returned JSON-RPC error: {result.body['error']}")
        return result.body

    def initialize(self) -> None:
        response = self.rpc(
            "initialize",
            {
                "protocolVersion": MCP_PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {
                    "name": "controller-tools-v1-probe",
                    "version": "1.0",
                },
            },
        )
        if not self.session_id:
            raise ProbeFailure("MCP initialize did not return a session ID")
        negotiated = response.get("result", {}).get("protocolVersion")
        if negotiated != MCP_PROTOCOL_VERSION:
            raise ProbeFailure(f"unexpected MCP version: {negotiated!r}")

    def tools(self) -> list[str]:
        response = self.rpc("tools/list", {})
        return sorted(
            tool.get("name", "")
            for tool in response.get("result", {}).get("tools", [])
        )

    def call(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        response = self.rpc(
            "tools/call",
            {
                "name": name,
                "arguments": arguments,
            },
        )
        structured = response.get("result", {}).get("structuredContent")
        if not isinstance(structured, dict):
            raise ProbeFailure(f"MCP {name} returned no structuredContent")
        return structured


def expect_ok(envelope: dict[str, Any], operation: str) -> dict[str, Any]:
    if not envelope.get("ok"):
        raise ProbeFailure(f"{operation} failed: {envelope.get('error')}")
    audit = envelope.get("audit", {})
    if not audit.get("requestHash"):
        raise ProbeFailure(f"{operation} has no audit request hash")
    before = audit.get("beforeStateHash")
    after = audit.get("afterStateHash")
    if before != after:
        raise ProbeFailure(f"{operation} changed IDE state")
    return envelope.get("data", {})


def rest_get(
    rest_base: str,
    path: str,
    operation_id: str,
    timeout: float,
) -> dict[str, Any]:
    separator = "&" if "?" in path else "?"
    url = (
        f"{rest_base}{path}{separator}"
        + urllib.parse.urlencode({"operationId": operation_id})
    )
    result = http_json(
        url,
        "GET",
        None,
        {
            "Origin": "http://localhost",
            "User-Agent": "controller-tools-v1-probe/1.0",
            "x-session-id": "probe-rest-session",
        },
        timeout,
    )
    if result.status != 200:
        raise ProbeFailure(f"REST GET {path} returned {result.status}: {result.body}")
    return result.body


def rest_post(
    rest_base: str,
    path: str,
    payload: dict[str, Any],
    timeout: float,
) -> dict[str, Any]:
    result = http_json(
        f"{rest_base}{path}",
        "POST",
        payload,
        {
            "Content-Type": "application/json",
            "Origin": "http://localhost",
            "User-Agent": "controller-tools-v1-probe/1.0",
            "x-session-id": "probe-rest-session",
        },
        timeout,
    )
    if result.status != 200:
        raise ProbeFailure(f"REST POST {path} returned {result.status}: {result.body}")
    return result.body


def compare_read(
    mcp: McpClient,
    rest_base: str,
    tool: str,
    path: str,
    arguments: dict[str, Any],
    operation_id: str,
    timeout: float,
) -> dict[str, Any]:
    rest = rest_get(rest_base, path, operation_id, timeout)
    mcp_arguments = dict(arguments)
    mcp_arguments["operationId"] = operation_id
    replay = mcp.call(tool, mcp_arguments)
    if replay != rest:
        raise ProbeFailure(f"{tool} differs across REST and MCP replay")
    return expect_ok(rest, tool)


def compare_post(
    mcp: McpClient,
    rest_base: str,
    tool: str,
    path: str,
    arguments: dict[str, Any],
    operation_id: str,
    timeout: float,
) -> dict[str, Any]:
    rest_arguments = dict(arguments)
    rest_arguments.pop("controllerId", None)
    rest_arguments["operationId"] = operation_id
    rest = rest_post(rest_base, path, rest_arguments, timeout)
    mcp_arguments = dict(arguments)
    mcp_arguments["operationId"] = operation_id
    replay = mcp.call(tool, mcp_arguments)
    if replay != rest:
        raise ProbeFailure(f"{tool} differs across REST and MCP replay")
    return expect_ok(rest, tool)


def reject_mutations(
    rest_base: str, controller_id: str, timeout: float
) -> None:
    encoded = urllib.parse.quote(controller_id, safe="")
    mutations = [
        ("PUT", f"/api/controller-tools/v1/controllers/{encoded}/ip"),
        ("POST", f"/api/controller-tools/v1/controllers/{encoded}/connect"),
        ("POST", f"/api/controller-tools/v1/controllers/{encoded}/lease"),
        ("POST", f"/api/controller-tools/v1/controllers/{encoded}/scan"),
        ("POST", f"/api/controller-tools/v1/controllers/{encoded}/configuration"),
        ("POST", f"/api/controller-tools/v1/controllers/{encoded}/deploy"),
        ("POST", f"/api/controller-tools/v1/controllers/{encoded}/motion"),
    ]
    for index, (method, path) in enumerate(mutations):
        result = http_json(
            f"{rest_base}{path}",
            method,
            {"operationId": f"probe-deny-{index}"},
            {
                "Content-Type": "application/json",
                "Origin": "http://localhost",
                "User-Agent": "controller-tools-v1-probe/1.0",
                "x-session-id": "probe-rest-session",
            },
            timeout,
        )
        error = result.body.get("error", {})
        details = error.get("details", {})
        audit = result.body.get("audit", {})
        if (
            result.status != 403
            or error.get("code") != "CT008_READ_ONLY"
            or details.get("reason") != "approval-required"
            or details.get("providerCalls") != 0
            or audit.get("decision") != "deny"
            or audit.get("sessionId") != "probe-rest-session"
            or not audit.get("requestHash")
        ):
            raise ProbeFailure(f"mutation {path} was not safely rejected: {result}")


def run_probe(mcp_url: str, rest_url: str, timeout: float) -> dict[str, Any]:
    mcp = McpClient(mcp_url, timeout)
    mcp.initialize()
    tools = mcp.tools()
    if tools != EXPECTED_TOOLS:
        raise ProbeFailure(f"closed tool catalog mismatch: {tools}")

    protocol = mcp.call(
        "gateway.get-protocol", {"operationId": "probe-mcp-audit"}
    )
    protocol_data = expect_ok(protocol, "gateway.get-protocol")
    if (
        protocol_data.get("apiVersion") != "controller-tools/v1"
        or protocol_data.get("mcpProtocolVersion") != MCP_PROTOCOL_VERSION
        or not protocol_data.get("controllerViews", {}).get("mockOnly")
        or not protocol_data.get("controllerViews", {}).get("readOnly")
        or not protocol_data.get("semanticRuntime", {}).get("available")
        or not protocol_data.get("semanticRuntime", {}).get(
            "operationIntentSubmission"
        )
        or not protocol_data.get("semanticRuntime", {}).get("approvalRequired")
        or protocol_data.get("semanticRuntime", {}).get("automationCanApprove")
        or protocol_data.get("semanticRuntime", {}).get("directProviderCalls")
    ):
        raise ProbeFailure("protocol boundary changed")
    protocol_audit = protocol.get("audit", {})
    if (
        protocol_audit.get("sessionId") != mcp.session_id
        or protocol_audit.get("client", {}).get("name")
        != "controller-tools-v1-probe"
        or protocol_audit.get("client", {}).get("version") != "1.0"
    ):
        raise ProbeFailure("MCP SessionId/clientInfo audit is incomplete")

    listed = compare_read(
        mcp,
        rest_url,
        "controller.list",
        "/api/controller-tools/v1/controllers",
        {},
        "probe-cross-list",
        timeout,
    )
    controllers = listed.get("controllers", [])
    if not controllers:
        raise ProbeFailure(
            "no IDE-owned Mock controller is available; prepare Workbench Mock state first"
        )
    controller_id = controllers[0].get("controllerId", "")
    if not controller_id:
        raise ProbeFailure("controller.list returned an empty controllerId")
    encoded = urllib.parse.quote(controller_id, safe="")

    capabilities = compare_read(
        mcp,
        rest_url,
        "controller.get-capabilities",
        f"/api/controller-tools/v1/controllers/{encoded}/capabilities",
        {"controllerId": controller_id},
        "probe-cross-capabilities",
        timeout,
    )
    state = compare_read(
        mcp,
        rest_url,
        "controller.get-state",
        f"/api/controller-tools/v1/controllers/{encoded}/state",
        {"controllerId": controller_id},
        "probe-cross-state",
        timeout,
    )
    topology = compare_read(
        mcp,
        rest_url,
        "controller.get-topology",
        f"/api/controller-tools/v1/controllers/{encoded}/topology",
        {"controllerId": controller_id},
        "probe-cross-topology",
        timeout,
    )
    slaves = topology.get("topology", {}).get("slaves", [])
    if not slaves:
        raise ProbeFailure("Mock topology has no device for controller.get-device")
    position = slaves[0].get("position")
    if not isinstance(position, int):
        raise ProbeFailure("Mock topology returned an invalid device position")
    device = compare_read(
        mcp,
        rest_url,
        "controller.get-device",
        f"/api/controller-tools/v1/controllers/{encoded}/devices/{position}",
        {"controllerId": controller_id, "position": position},
        "probe-cross-device",
        timeout,
    )
    diagnostics = compare_read(
        mcp,
        rest_url,
        "controller.get-diagnostics",
        f"/api/controller-tools/v1/controllers/{encoded}/diagnostics",
        {"controllerId": controller_id},
        "probe-cross-diagnostics",
        timeout,
    )

    semantic_context_data = compare_read(
        mcp,
        rest_url,
        "runtime.get-context",
        f"/api/controller-tools/v1/runtime/{encoded}/context",
        {"controllerId": controller_id},
        "probe-cross-runtime-context",
        timeout,
    )
    semantic_context = semantic_context_data.get("context", {})
    if (
        not semantic_context.get("complete")
        or not semantic_context.get("bindingVerified")
        or not semantic_context.get("contextHash")
    ):
        raise ProbeFailure("semantic runtime context is not verified")
    signals = semantic_context.get("signals", [])
    if not signals:
        raise ProbeFailure("semantic runtime context contains no signal")
    signal = signals[0]
    device_id = signal.get("deviceId", "")
    signal_id = signal.get("signalId", "")
    context_hash = semantic_context.get("contextHash", "")
    if not device_id or not signal_id:
        raise ProbeFailure("semantic signal identity is incomplete")
    semantic_encoded = json.dumps(
        semantic_context_data, sort_keys=True, separators=(",", ":")
    )
    forbidden_semantic_tokens = [
        "resourceId",
        "componentInstanceId",
        "consistencyGroupId",
        "processImage",
        "pdo",
        "127.0.0.1",
    ]
    if any(token in semantic_encoded for token in forbidden_semantic_tokens):
        raise ProbeFailure("semantic projection exposed a private runtime binding")

    semantic_read = compare_post(
        mcp,
        rest_url,
        "runtime.read",
        f"/api/controller-tools/v1/runtime/{encoded}/read",
        {
            "controllerId": controller_id,
            "deviceId": device_id,
            "signalId": signal_id,
            "contextHash": context_hash,
        },
        "probe-cross-runtime-read",
        timeout,
    )
    semantic_signal = semantic_read.get("signal", {})
    if (
        semantic_signal.get("quality") != "good"
        or semantic_signal.get("captureCycle") != "101"
    ):
        raise ProbeFailure("semantic runtime read lost quality or capture cycle")

    semantic_operation = compare_post(
        mcp,
        rest_url,
        "runtime.operation.request",
        f"/api/controller-tools/v1/runtime/{encoded}/operations",
        {
            "controllerId": controller_id,
            "deviceId": device_id,
            "signalId": signal_id,
            "value": False,
            "parameters": {},
            "ttlMs": 200,
            "contextHash": context_hash,
        },
        "probe-cross-runtime-operation",
        timeout,
    ).get("operation", {})
    if (
        semantic_operation.get("state") != "approval-required"
        or not semantic_operation.get("approvalChallenge")
        or set(semantic_operation)
        != {"operationId", "state", "approvalChallenge"}
    ):
        raise ProbeFailure("semantic operation bypassed approval")

    target_operation_id = semantic_operation.get("operationId", "")
    semantic_operation_read = compare_read(
        mcp,
        rest_url,
        "runtime.operation.get",
        "/api/controller-tools/v1/runtime/operations/"
        + urllib.parse.quote(target_operation_id, safe=""),
        {"targetOperationId": target_operation_id},
        "probe-cross-runtime-operation-get",
        timeout,
    ).get("operation", {})
    if semantic_operation_read != semantic_operation:
        raise ProbeFailure("semantic operation record differs across the shared service")

    conflict = mcp.call(
        "controller.get-state",
        {
            "controllerId": "ide:different:controller",
            "operationId": "probe-cross-state",
        },
    )
    conflict_error = conflict.get("error", {})
    if (
        conflict.get("ok")
        or conflict_error.get("code") != "CT010_BAD_REQUEST"
        or conflict_error.get("details", {}).get("reason")
        != "operation-id-conflict"
    ):
        raise ProbeFailure("OperationId parameter conflict was not rejected")

    reject_mutations(rest_url, controller_id, timeout)

    return {
        "ok": True,
        "apiVersion": protocol_data["apiVersion"],
        "mcpProtocolVersion": protocol_data["mcpProtocolVersion"],
        "toolCount": len(tools),
        "controllerId": controller_id,
        "position": position,
        "contextHash": state.get("contextHash"),
        "topologySource": topology.get("topology", {}).get("source"),
        "diagnosticsAvailable": diagnostics.get("diagnostics", {}).get("available"),
        "capabilitiesAvailable": capabilities.get("controller", {}).get("available"),
        "deviceAvailable": bool(device.get("device")),
        "semanticContextVerified": semantic_context.get("bindingVerified"),
        "semanticQuality": semantic_signal.get("quality"),
        "semanticOperationState": semantic_operation.get("state"),
        "mutationsRejected": 7,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Client-only process probe for controller-tools/v1"
    )
    parser.add_argument("--mcp-url", required=True)
    parser.add_argument("--rest-url", required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    try:
        result = run_probe(
            args.mcp_url.rstrip("/") + "/",
            args.rest_url.rstrip("/"),
            args.timeout,
        )
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        return 1

    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
