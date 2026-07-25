"""Stable controller-tools-v1 error codes and recovery guidance."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class ErrorCode(str, Enum):
    FORMAT_INVALID = "CT000_FORMAT_INVALID"
    UNKNOWN_DEVICE = "CT001_UNKNOWN_DEVICE"
    ESI_CONFLICT = "CT002_ESI_CONFLICT"
    PDO_CAPACITY_EXCEEDED = "CT003_PDO_CAPACITY_EXCEEDED"
    CYCLE_INFEASIBLE = "CT004_CYCLE_INFEASIBLE"
    DANGEROUS_MOTION = "CT005_DANGEROUS_MOTION"
    ADAPTER_UNVERIFIED = "CT006_ADAPTER_UNVERIFIED"
    REFERENCE_NOT_FOUND = "CT007_REFERENCE_NOT_FOUND"
    READ_ONLY = "CT008_READ_ONLY"
    NOT_FOUND = "CT009_NOT_FOUND"
    BAD_REQUEST = "CT010_BAD_REQUEST"
    PROTOCOL_UNSUPPORTED = "CT011_PROTOCOL_UNSUPPORTED"
    AUTH_NOT_IMPLEMENTED = "CT012_AUTH_NOT_IMPLEMENTED"


RECOVERY_SUGGESTIONS: dict[ErrorCode, str] = {
    ErrorCode.FORMAT_INVALID: (
        "Correct the fields reported by the JSON Schema validator and resubmit "
        "the same immutable document."
    ),
    ErrorCode.UNKNOWN_DEVICE: (
        "Import the exact ESI, provide a versioned adapter for the reported "
        "identity, then repeat offline validation."
    ),
    ErrorCode.ESI_CONFLICT: (
        "Select one exact ESI SHA-256 for the identity or narrow the adapter "
        "identity scope; never choose a nearest revision."
    ),
    ErrorCode.PDO_CAPACITY_EXCEEDED: (
        "Reduce selected PDO entries or use a controller whose declared input "
        "and output capacities satisfy the project."
    ),
    ErrorCode.CYCLE_INFEASIBLE: (
        "Increase cycleUs to at least the suggested cycle or reduce frames, "
        "process data, and task WCET."
    ),
    ErrorCode.DANGEROUS_MOTION: (
        "Constrain position, velocity, acceleration, deceleration, and timeout "
        "inside the approved motion envelope."
    ),
    ErrorCode.ADAPTER_UNVERIFIED: (
        "Keep the adapter mock/offline-only until deterministic validation, "
        "test evidence, identity scope, ESI hash, and signature are complete."
    ),
    ErrorCode.REFERENCE_NOT_FOUND: (
        "Provide the exact referenced controller, device model, adapter, "
        "intent, or artifact version."
    ),
    ErrorCode.READ_ONLY: (
        "Use a future explicitly authorized state-changing API; this gateway "
        "intentionally exposes no controller write path."
    ),
    ErrorCode.NOT_FOUND: "Refresh read-only inventory and use an existing identifier.",
    ErrorCode.BAD_REQUEST: "Correct the request fields and retry with a new operation ID.",
    ErrorCode.PROTOCOL_UNSUPPORTED: (
        "Negotiate MCP 2025-11-25 or use the versioned REST controller-tools/v1 API."
    ),
    ErrorCode.AUTH_NOT_IMPLEMENTED: (
        "Do not expose this mock server beyond loopback; production use requires "
        "OAuth audience validation and controller policy binding."
    ),
}


@dataclass(frozen=True)
class GatewayFailure(Exception):
    code: ErrorCode
    message: str
    path: str = "$"
    details: dict[str, object] | None = None

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "code": self.code.value,
            "message": self.message,
            "path": self.path,
            "recovery": RECOVERY_SUGGESTIONS[self.code],
        }
        if self.details:
            result["details"] = self.details
        return result
