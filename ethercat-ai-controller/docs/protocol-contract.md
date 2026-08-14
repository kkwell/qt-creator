# controller-tools-v1 Protocol Contract

## Version negotiation

The independent version axes are:

- REST/MCP tool surface: `controller-tools/v1`;
- data contracts: `controller.embed-labs.dev/v1`;
- MCP protocol: `2025-11-25`;
- OpenAPI document: `3.1.1`; and
- JSON Schema dialect: Draft `2020-12`.

A client must use the explicit versions. Minor Product API compatibility is
an implementation detail behind a future provider bridge and is never inferred
from the Gateway version.

## MCP Streamable HTTP

The server uses `/mcp` as its single MCP endpoint. It implements the
MCP 2025-11-25 JSON response mode:

1. `POST initialize` returns a cryptographically random `MCP-Session-Id`.
2. The client sends `notifications/initialized`.
3. Subsequent requests include both `MCP-Session-Id` and
   `MCP-Protocol-Version: 2025-11-25`.
4. `tools/list` returns the fixed nine-tool catalogue.
5. `tools/call` returns both JSON text and `structuredContent`.
6. Tool validation/rejection is returned as `isError: true`; JSON-RPC
   framing, unknown tools, and unknown methods use protocol errors.
7. `DELETE /mcp` terminates the mock session.

Each POST must accept both `application/json` and `text/event-stream`, as
required by Streamable HTTP. This skeleton always returns one JSON object.
`GET /mcp` returns `405`, which explicitly declines an SSE listening stream.

Incoming `Origin` is limited to HTTP(S) loopback origins. The server refuses a
non-loopback bind address.

## REST mapping

| Tool | REST operation |
|---|---|
| `gateway.get-protocol` | `GET /api/controller-tools/v1/protocol` |
| `controller.list` | `GET /api/controller-tools/v1/controllers` |
| `controller.get-capabilities` | `GET .../controllers/{id}/capabilities` |
| `controller.get-state` | `GET .../controllers/{id}/state` |
| `controller.get-topology` | `GET .../controllers/{id}/topology` |
| `controller.get-device` | `GET .../controllers/{id}/devices/{position}` |
| `controller.get-diagnostics` | `GET .../controllers/{id}/diagnostics` |
| `adapter.list` | `GET /api/controller-tools/v1/adapters` |
| `artifact.validate` | `POST /api/controller-tools/v1/artifacts/validate` |

The validation POST is pure: it stores nothing and performs no controller
operation. There is no route for scan, lease, configuration, SDO/PDO write,
package, activation, start, stop, or motion.

## Capability negotiation

MCP initialization advertises only `tools`. The
`controller.get-capabilities` tool returns two independent sets:

- the mock controller's declared EtherCAT limits and features; and
- the Gateway protocol, format versions, mock/read-only state, and absence of
  state-changing tools.

Unknown numeric capacity is never replaced with a guessed board value in a
production implementation.

## Result envelope and audit fields

REST and structured MCP results share:

```json
{
  "apiVersion": "controller-tools/v1",
  "operationId": "caller-or-server-id",
  "ok": true,
  "data": {},
  "warnings": [],
  "audit": {}
}
```

The audit record includes user, AI client, ControllerId, BootId, SessionId,
lease ID, OperationId, tool, parameter hash, before-state hash, after-state
hash, result, mock flag, and read-only flag. In this issue `leaseId` is always
`null`, and before/after state hashes are equal.

The in-memory audit ring and returned audit object are test evidence, not a
production durable audit log.

## Idempotency and transaction semantics

All tools are read-only and idempotent. A supplied `operationId` is echoed, and
identical mock reads produce identical envelopes. `artifact.validate` is a
deterministic pure function over the artifact plus checked-in registry.

There is no mutating transaction in v1 of this skeleton. A future mutating
surface must not reuse this lightweight behavior; it requires durable
OperationId/idempotency storage, lease binding, expected generation,
prepare/commit/abort state, atomic activation, and rollback evidence.

## Authentication status

Production authentication is intentionally not implemented. The mock server:

- binds only to loopback;
- accepts optional REST actor headers only for explicit test audit labeling;
- labels missing MCP user identity as unavailable; and
- reports authentication as `not-implemented-loopback-mock-only`.

Those headers are not authentication. A production HTTP deployment must use
OAuth resource/audience validation, must not pass through the inbound token to
Product API, and must bind the verified user/client identity into controller
policy and audit.

## Stable errors

| Code | Meaning | Recovery |
|---|---|---|
| `CT000_FORMAT_INVALID` | Schema/domain format failed | Correct reported fields |
| `CT001_UNKNOWN_DEVICE` | No exact device/adapter identity | Import exact ESI or add adapter |
| `CT002_ESI_CONFLICT` | Ambiguous identity/ESI scope | Resolve exact hash and priority |
| `CT003_PDO_CAPACITY_EXCEEDED` | PI or mapping exceeds capacity | Reduce PDOs or select capable controller |
| `CT004_CYCLE_INFEASIBLE` | Cycle/frame/WCET budget fails | Use suggested larger cycle or reduce load |
| `CT005_DANGEROUS_MOTION` | Motion exceeds approved envelope | Constrain all motion and timeout fields |
| `CT006_ADAPTER_UNVERIFIED` | Adapter cannot cross mock boundary | Add evidence, review, signature |
| `CT007_REFERENCE_NOT_FOUND` | Versioned reference is missing/stale | Refresh and select exact reference |
| `CT008_READ_ONLY` | Requested state claim/action is excluded | Use a future authorized API |
| `CT009_NOT_FOUND` | Tool/controller/device absent | Refresh inventory |
| `CT010_BAD_REQUEST` | Tool input schema failed | Correct arguments |
| `CT011_PROTOCOL_UNSUPPORTED` | Version cannot be negotiated | Use a supported version |
| `CT012_AUTH_NOT_IMPLEMENTED` | Production auth boundary absent | Keep loopback-only |

Each error has an actionable recovery string. The Gateway never converts an
unknown device, ESI conflict, or vendor-private gap into a guessed control
sequence.
