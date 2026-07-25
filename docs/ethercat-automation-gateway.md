# EtherCAT Automation Gateway

## Issue and evidence boundary

`ISSUE-IDE-AUTOMATION-GATEWAY-001` adds the first automation boundary inside
the Embed Labs IDE. Its implementation baseline is
`857320a7e2018974fb702b32d92361fcd4dee635`.

This issue is offline and Mock-only. It does not connect a controller, acquire
a lease, scan a bus, apply configuration, deploy a package, start a task, or
command motion. The checked-in `ethercat-ai-controller` contracts and Mock
artifacts are embedded as protocol input; its Python Gateway is not started and
is not an IDE state source.

## Ownership

The IDE remains the only source of EtherCAT state:

```text
ProjectService + Workbench selection + Provider snapshots
                         |
                         v
        WorkbenchAutomationService (fresh value copies)
                         |
                         v
        AutomationDispatcher + OperationId journal
                    /                 \
     dedicated MCP Server          REST/OpenAPI
```

`AutomationService` returns one fresh `AutomationContextSnapshot` value for
each valid Project/Master pair. The value may contain the IDE project, selected
connection profile presentation, immutable connection snapshot, selected
Workbench scan result, selected diagnostics result, and ESI description
values. It contains no Provider pointer, callback, socket, or control method.
Closing a project or removing/updating a Provider value therefore changes the
next Gateway result immediately; the Gateway has no controller, topology,
lease, or project cache.

The Workbench adapter may call Providers while constructing a read-only value
copy. The Gateway itself depends only on `AutomationService` and cannot name or
cast a concrete Provider. It has no dependency on `EtherCATProductApi`.

## Protocols

The contract versions are:

- API: `controller-tools/v1`
- MCP: Streamable HTTP, protocol `2025-11-25`
- REST description: OpenAPI `3.1.1`
- artifact envelope: `controller.embed-labs.dev/v1`

The plugin manifest and its listeners are disabled by default. When explicitly
enabled, the service binds only IPv4 loopback. MCP and REST use separate
loopback ports so the dedicated ordinary `Mcp::Server` cannot inherit the
IDE-wide auto-registering tool set. Both transports call one Dispatcher and
one bounded OperationId journal.

The MCP catalog is closed to these nine tools:

| Tool | Initial IDE behavior |
|---|---|
| `controller.list` | Lists Mock contexts already present in the IDE; never discovers a network. |
| `controller.get-capabilities` | Reads copied controller capability values and the Gateway boundary. |
| `controller.get-state` | Reads the copied safe controller-state projection. |
| `controller.get-topology` | Reads the existing Workbench scan, controller topology, or offline project value; never starts Scan. |
| `controller.get-device` | Reads one safe identity/ESI summary by topology position. |
| `controller.get-diagnostics` | Reads a bounded diagnostics summary. |
| `adapter.list` | Reports that no IDE Adapter Registry semantic service exists; checked-in Mock manifests are not runtime state. |
| `artifact.validate` | Performs pure v1 envelope/kind structural checks; it neither stores nor deploys. |
| `gateway.get-protocol` | Reports versions, tools, real-time boundary, and unavailable mutations. |

REST exposes the equivalent versioned read routes and the checked-in OpenAPI
document. A non-loopback browser `Origin` is rejected by both transports even
when MCP CORS response headers are disabled.

## Operation and audit contract

Every dispatch has an OperationId. If omitted for a read, the Gateway creates
one. The journal binds an OperationId to the canonical SHA-256 of the tool and
parameters:

- the same ID and parameters return the original response across MCP and REST;
- the same ID with different parameters returns
  `CT010_BAD_REQUEST` with `operation-id-conflict`;
- reads record equal before/after snapshot hashes;
- MCP audit records carry the MCP server SessionId and client name/version;
- audit identity is evidence only and grants no controller authority.

IP changes, connect, lease, scan, configuration apply, deployment, and motion
are not MCP tools. The corresponding defensive REST routes return HTTP 403,
`CT008_READ_ONLY`, and `approval-required`. They do not call a connection,
control, scan, write, PDO, SDO, register, memory, Shell, or CPU1 interface.

## Safe projection

Responses omit Provider IDs and class names, Product API channel names,
transport error details, source file paths, raw startup SDO data, raw object
indices, and unbounded process-data access. They expose stable IDE
Project/Master identity, safe state/capability summaries, topology identity,
ESI hashes and size/count summaries, and bounded diagnostics.

The Gateway never participates in the 125 us loop. It generates no cyclic
frame and has no CPU1 or FPGA interface.

## Offline tests

The plugin QtTest suite covers:

- default-off listeners and the exact nine-tool catalog;
- shared snapshot hashes, source update, project-close invalidation, and no
  Gateway snapshot cache;
- cross-MCP/REST OperationId replay and parameter conflict;
- stable denial of every initial mutation with zero fake Provider calls;
- vendor/private-data redaction;
- non-loopback Origin rejection;
- MCP SessionId/clientInfo audit;
- listener stop/reuse and atomic rollback after partial bind failure;
- structural artifact validation; and
- CMake/qbs source and dependency synchronization.

These are offline/Mock results only. The source task owns final product
startup, the unique live controller binding, real controller tests, and
hardware acceptance.
