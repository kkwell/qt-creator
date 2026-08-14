"""Provider-neutral, mock-only controller-tools-v1 gateway."""

from __future__ import annotations

import copy
import json
import threading
import uuid
from collections import deque
from dataclasses import dataclass
from typing import Any, Callable

from .errors import ErrorCode, GatewayFailure
from .repository import MockRepository
from .schema import load_json
from .semantic import (
    ContractValidator,
    canonical_parameters_sha256,
    validation_report,
)


@dataclass(frozen=True)
class Actor:
    user_id: str = "anonymous-local-mock"
    client_id: str = "unknown-ai-client"
    session_id: str = "no-session"


class ReadOnlyGateway:
    """Dispatches a closed allow-list of pure read and validation tools."""

    def __init__(self, repository: MockRepository | None = None):
        self.repository = repository or MockRepository()
        self.validator = ContractValidator(self.repository)
        catalog_path = (
            self.repository.root / "api" / "controller-tools-v1.mcp-tools.json"
        )
        self.catalog = load_json(catalog_path)
        # The standalone reference remains the original nine-tool Mock/read-only
        # implementation. The IDE-only runtime.* extension requires the shared
        # SemanticRuntimeService and is deliberately not emulated here.
        self._tools = {
            tool["name"]: tool
            for tool in self.catalog["tools"]
            if not tool["name"].startswith("runtime.")
        }
        self._audit_events: deque[dict[str, Any]] = deque(maxlen=1024)
        self._audit_lock = threading.Lock()
        self._implementations: dict[
            str, Callable[[dict[str, Any]], dict[str, Any]]
        ] = {
            "controller.list": self._list_controllers,
            "controller.get-capabilities": self._get_capabilities,
            "controller.get-state": self._get_state,
            "controller.get-topology": self._get_topology,
            "controller.get-device": self._get_device,
            "controller.get-diagnostics": self._get_diagnostics,
            "adapter.list": self._list_adapters,
            "artifact.validate": self._validate_artifact,
            "gateway.get-protocol": self._get_protocol,
        }
        if set(self._tools) != set(self._implementations):
            raise ValueError("MCP catalogue and gateway implementation differ")
        unsafe = [
            name
            for name, tool in self._tools.items()
            if not tool.get("annotations", {}).get("readOnlyHint")
            or tool.get("annotations", {}).get("destructiveHint")
        ]
        if unsafe:
            raise ValueError(f"non-read-only tool in issue scope: {unsafe}")

    @property
    def tool_definitions(self) -> list[dict[str, Any]]:
        return copy.deepcopy(list(self._tools.values()))

    @property
    def audit_events(self) -> list[dict[str, Any]]:
        with self._audit_lock:
            return copy.deepcopy(list(self._audit_events))

    def dispatch(
        self,
        tool_name: str,
        arguments: dict[str, Any] | None = None,
        actor: Actor | None = None,
    ) -> dict[str, Any]:
        actor = actor or Actor()
        supplied = copy.deepcopy(arguments or {})
        operation_id = supplied.get("operationId") or str(uuid.uuid4())
        before_hash = self._state_sha256()
        error: GatewayFailure | None = None
        data: dict[str, Any] = {}

        tool = self._tools.get(tool_name)
        if tool is None:
            error = GatewayFailure(
                ErrorCode.NOT_FOUND, f"Unknown tool {tool_name!r}.", "$.name"
            )
        else:
            input_issues = self.repository.schema_registry.validate_inline(
                supplied, tool["inputSchema"]
            )
            if input_issues:
                error = GatewayFailure(
                    ErrorCode.BAD_REQUEST,
                    "Tool arguments do not match the published input schema.",
                    input_issues[0].path,
                    {"issues": [issue.as_dict() for issue in input_issues]},
                )
            else:
                try:
                    data = self._implementations[tool_name](supplied)
                except GatewayFailure as failure:
                    error = failure

        after_hash = self._state_sha256()
        audit = {
            "userId": actor.user_id,
            "aiClientId": actor.client_id,
            "controllerId": self.repository.controller["spec"]["controllerId"],
            "bootId": self.repository.controller["spec"]["bootId"],
            "sessionId": actor.session_id,
            "leaseId": None,
            "operationId": operation_id,
            "tool": tool_name,
            "parametersSha256": canonical_parameters_sha256(supplied),
            "beforeStateSha256": before_hash,
            "afterStateSha256": after_hash,
            "result": "rejected" if error else "succeeded",
            "mock": True,
            "readOnly": True,
        }
        with self._audit_lock:
            self._audit_events.append(copy.deepcopy(audit))

        envelope: dict[str, Any] = {
            "apiVersion": "controller-tools/v1",
            "operationId": operation_id,
            "ok": error is None,
            "data": data,
            "warnings": [
                "Mock-only result; no Product API or EtherCAT hardware was contacted."
            ],
            "audit": audit,
        }
        if error:
            envelope["error"] = error.as_dict()
        return envelope

    def _require_controller(self, arguments: dict[str, Any]) -> dict[str, Any]:
        expected = self.repository.controller["spec"]["controllerId"]
        if arguments.get("controllerId") != expected:
            raise GatewayFailure(
                ErrorCode.NOT_FOUND,
                f"Controller {arguments.get('controllerId')!r} is not configured.",
                "$.controllerId",
            )
        return self.repository.controller["spec"]

    def _list_controllers(self, _: dict[str, Any]) -> dict[str, Any]:
        spec = self.repository.controller["spec"]
        return {
            "controllers": [
                {
                    "controllerId": spec["controllerId"],
                    "bootId": spec["bootId"],
                    "mock": spec["mock"],
                    "readOnly": spec["readOnly"],
                    "serviceState": spec["state"]["serviceState"],
                }
            ]
        }

    def _get_capabilities(self, arguments: dict[str, Any]) -> dict[str, Any]:
        spec = self._require_controller(arguments)
        return {
            "controllerId": spec["controllerId"],
            "bootId": spec["bootId"],
            "capabilitySha256": spec["capabilitySha256"],
            "controller": copy.deepcopy(spec["capabilities"]),
            "gateway": self.repository.protocol_document(),
        }

    def _get_state(self, arguments: dict[str, Any]) -> dict[str, Any]:
        spec = self._require_controller(arguments)
        return {
            "controllerId": spec["controllerId"],
            "bootId": spec["bootId"],
            "state": copy.deepcopy(spec["state"]),
            "sampleSource": "static-mock",
        }

    def _get_topology(self, arguments: dict[str, Any]) -> dict[str, Any]:
        spec = self._require_controller(arguments)
        return {
            "controllerId": spec["controllerId"],
            "bootId": spec["bootId"],
            "topology": copy.deepcopy(spec["topology"]),
            "scanned": False,
        }

    def _get_device(self, arguments: dict[str, Any]) -> dict[str, Any]:
        spec = self._require_controller(arguments)
        position = arguments["position"]
        slave = next(
            (
                item
                for item in spec["topology"]["slaves"]
                if item["position"] == position
            ),
            None,
        )
        device = self.repository.device_at_position(position)
        if slave is None or device is None:
            raise GatewayFailure(
                ErrorCode.NOT_FOUND,
                f"No mock device exists at position {position}.",
                "$.position",
            )
        return {
            "controllerId": spec["controllerId"],
            "topologyIdentity": copy.deepcopy(slave),
            "normalizedDeviceModel": device,
        }

    def _get_diagnostics(self, arguments: dict[str, Any]) -> dict[str, Any]:
        spec = self._require_controller(arguments)
        return {
            "controllerId": spec["controllerId"],
            "bootId": spec["bootId"],
            "diagnostics": copy.deepcopy(spec["diagnostics"]),
            "sampleSource": "static-mock",
        }

    def _list_adapters(self, _: dict[str, Any]) -> dict[str, Any]:
        adapters = [
            {
                "adapterId": adapter["metadata"]["id"],
                "version": adapter["metadata"]["version"],
                "validationState": adapter["spec"]["validationState"],
                "identityScope": copy.deepcopy(adapter["spec"]["identityScope"]),
                "esiSha256": copy.deepcopy(adapter["spec"]["esiSha256"]),
                "realHardwareAllowed": adapter["spec"]["realHardwareAllowed"],
                "manifestSha256": self.repository.adapter_file_sha256[adapter_id],
            }
            for adapter_id, adapter in sorted(self.repository.adapters.items())
        ]
        return {"adapters": adapters}

    def _validate_artifact(self, arguments: dict[str, Any]) -> dict[str, Any]:
        artifact = arguments["artifact"]
        return {
            "artifactKind": artifact.get("kind"),
            "report": validation_report(self.validator.validate(artifact)),
            "stored": False,
            "deployed": False,
        }

    def _get_protocol(self, _: dict[str, Any]) -> dict[str, Any]:
        protocol = self.repository.protocol_document()
        protocol.update(
            {
                "toolCount": len(self._tools),
                "tools": sorted(self._tools),
                "authentication": "not-implemented-loopback-mock-only",
                "unsupported": copy.deepcopy(
                    self.repository.controller["spec"]["operationPolicy"]["forbidden"]
                ),
            }
        )
        return protocol

    def _state_sha256(self) -> str:
        state = json.dumps(
            self.repository.controller["spec"]["state"],
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        import hashlib

        return hashlib.sha256(state).hexdigest()
