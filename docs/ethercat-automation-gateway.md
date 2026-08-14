# EtherCAT Automation Gateway

## Issue and evidence boundary

`ISSUE-IDE-AUTOMATION-GATEWAY-001` added the first automation boundary inside
the Embed Labs IDE. `ISSUE-IDE-AUTOMATION-GATEWAY-002` makes that plugin
discoverable in the product, adds an explicit runtime listener transaction and
settings page, and adds a real loopback process-level client probe. The Phase-2
implementation baseline is
`94453831bef3f97ca264832277ea725ed17785a2`.

The legacy `controller.*` views remain offline and Mock-only. The additive
selected-topology view may project an explicitly selected Real-controller or
Mock-scan evidence value that Workbench has already read, but it does not
connect a controller, acquire a lease, scan a bus, apply configuration, deploy
a package, start a task, or command motion. The checked-in
`ethercat-ai-controller` contracts and Mock artifacts are protocol input; its
Python Gateway is not started and is not an IDE state source.

## Ownership

The IDE remains the only source of EtherCAT state:

```text
ProjectService + Workbench exact selection + Provider snapshots
                             |
                             v
               TopologyService (read-through)
                             |
                             v
           WorkbenchAutomationService (value copies)
                             |
                             v
           AutomationDispatcher + OperationId journal
                       /                 \
        dedicated MCP Server          REST/OpenAPI
```

`AutomationService` returns one fresh `AutomationContextSnapshot` value for
each valid Project/Master pair. The value may contain the IDE project, selected
connection profile presentation, immutable connection snapshot, selected
Workbench scan result, selected diagnostics result, ESI description values,
and at most one value-only topology lookup for each explicitly selected Real
and Mock source. It contains no Provider pointer, callback, socket, or control
method. Closing a project or removing/updating a Provider value therefore
changes the next Gateway result immediately; the Gateway has no controller,
topology, lease, or project cache.

Workbench resolves each exact `source + providerId + project/master scope`
selection through `TopologyService` while constructing the read-only value.
The Gateway itself consumes only `AutomationService`; it cannot name, look up,
or cast a concrete Provider and has no dependency on `EtherCATProductApi`.

## Protocols

The contract versions are:

- API envelope and existing tool behavior: `controller-tools/v1`
- additive negotiated contract revision: `controller-tools/v1.1`
- selected topology payload: `selected-topology-evidence/v1`
- MCP: Streamable HTTP, protocol `2025-11-25`
- REST description: OpenAPI `3.1.1`
- artifact envelope: `controller.embed-labs.dev/v1`

Clients negotiate the closed tool catalog with `gateway.get-protocol`. A
client that only understands `controller-tools/v1` may keep using the original
tools unchanged. A client must observe contract revision
`controller-tools/v1.1` and the advertised `selectedTopologyEvidence`
capability before calling the additive `topology.list-selected` tool or its
REST route.

The plugin is enabled by default so a clean product profile discovers its
settings page without a plugin-manager enable/restart cycle. Its listeners are
still disabled by default. Loading the plugin therefore opens no network port.
When the user explicitly enables the service, it binds only IPv4 loopback. MCP
and REST use separate loopback ports so the dedicated ordinary `Mcp::Server`
cannot inherit the IDE-wide auto-registering tool set. Both transports call one
Dispatcher and one bounded OperationId journal.

## Product settings and listener transaction

`Preferences > EtherCAT > Automation Gateway` exposes:

- one explicit enable checkbox;
- fixed, read-only `127.0.0.1`;
- MCP and REST port settings, where `0` requests an automatically allocated
  port and equal non-zero ports are rejected;
- runtime state and the actual MCP/REST endpoints;
- the last start error; and
- a permanent warning that legacy controller views are Mock-only/read-only and
  selected topology evidence is explicit-source/read-only and never scans.

Applying the page starts or stops the listeners immediately. No IDE restart is
required. Enabling is committed to IDE settings only after the MCP listener,
REST listener, and optional IDE MCP registry publication all succeed. Failure
closes both sockets and persists the safe disabled state. Disabling the service,
unloading the plugin, or closing the IDE releases both ports. A successful
explicit enable remains an opt-in preference for the next IDE start.

`GatewayRuntimeController` owns only the `GatewayServer`, accepted listener
configuration, lifecycle state, and last error. It has no Provider pointer and
does not copy a project, controller connection, lease, topology, scan, ESI, or
diagnostics value. The listener lifecycle is not a second EtherCAT state
machine.

Start, stop, and failure events are written with the `[AI Gateway]` prefix to
the existing `EtherCAT Controller` Application Output channel. A failure may
reveal that existing output pane, but no modal dialog, new window, or status-bar
controller is created.

The MCP catalog is closed to these 14 tools:

| Tool | Initial IDE behavior |
|---|---|
| `controller.list` | Lists Mock contexts already present in the IDE; never discovers a network. |
| `controller.get-capabilities` | Reads copied Mock-controller capability values and the Gateway boundary. |
| `controller.get-state` | Reads the copied safe Mock-controller state projection. |
| `controller.get-topology` | Reads topology already present in one Mock context; never starts Scan. |
| `controller.get-device` | Reads one safe identity/ESI summary from one Mock context by topology position. |
| `controller.get-diagnostics` | Reads a bounded Mock diagnostics summary. |
| `topology.list-selected` | Lists value-only status for each exact Real/Mock selection and includes slave evidence only while it is Fresh; never selects a Provider or starts Scan. |
| `runtime.get-context` | Reads one verified signed semantic runtime context. |
| `runtime.read` | Reads bounded typed resources from the verified context. |
| `runtime.operation.get` | Reads one semantic operation record. |
| `runtime.operation.request` | Submits a signed semantic action intent that still requires separate IDE approval. |
| `adapter.list` | Reports that no IDE Adapter Registry semantic service exists; checked-in Mock manifests are not runtime state. |
| `artifact.validate` | Performs pure v1 envelope/kind structural checks; it neither stores nor deploys. |
| `gateway.get-protocol` | Reports versions, tools, real-time boundary, and unavailable mutations. |

REST exposes the equivalent versioned routes and the checked-in OpenAPI
document. The selected-topology route is
`GET /api/controller-tools/v1/topologies/selected`. A non-loopback browser
`Origin` is rejected by both transports even when MCP CORS response headers are
disabled.

`topology.list-selected` returns `source`, `lookupStatus`, `freshness` and
`current` for every published exact selection. Only `Fresh` current evidence
contains `slaves`, `observedAt`, `complete`, `generationHash` and
`evidenceHash`. Provider removal, stale/incomplete evidence, scope mismatch or
other lookup failure remains a status-only record: the Gateway does not retain
or fall back to an older slave list. Real and Mock records remain separate and
neither is an automatic substitute for the other. One response is limited to
512 records, 4096 slaves across all records and 2 MiB of compact JSON. Invalid
slave fields, an incomplete Real result, or any limit violation fails the whole
request closed instead of publishing a partial topology.

## Operation and audit contract

Every dispatch has an OperationId. If omitted for a read, the Gateway creates
one. The journal binds an OperationId to the canonical SHA-256 of the tool and
parameters:

- the same ID and parameters return the original response across MCP and REST;
- the same ID with different parameters returns
  `CT010_BAD_REQUEST` with `operation-id-conflict`;
- reads record equal before/after snapshot hashes;
- MCP audit records carry the MCP server SessionId and client name/version;
- audit identity is evidence only and grants no controller authority; and
- the in-memory replay journal is limited to 1024 entries and 8 MiB of compact
  response JSON, evicting the oldest complete entries without retaining an
  unbounded topology response.

IP changes, connect, lease, scan, configuration apply, deployment, and motion
are not MCP tools. The corresponding defensive REST routes return HTTP 403,
`CT008_READ_ONLY`, and `approval-required`. They do not call a connection,
control, scan, write, PDO, SDO, register, memory, Shell, or CPU1 interface.

## Safe projection

Responses omit Provider IDs and class names, raw Session/Request/generation
fields, Product API channel names, transport error details, source file paths,
raw startup SDO data, raw object indices, and unbounded process-data access.
They expose stable IDE Project/Master identity, safe state/capability summaries,
topology identity, ESI hashes and size/count summaries, and bounded diagnostics.
The selected-topology extension exposes source/freshness/status plus
domain-separated generation and evidence hashes; it never exposes the internal
selection identity used to calculate those hashes.

The Gateway never participates in the 125 us loop. It generates no cyclic
frame and has no CPU1 or FPGA interface.

## Offline tests

The plugin QtTest suite covers:

- default-off listeners and the exact 14-tool catalog;
- shared snapshot hashes, source update, project-close invalidation, and no
  Gateway snapshot cache;
- exact Real/Mock selected-topology value projection, deterministic ordering,
  Fresh-only slave evidence and status-only invalidation;
- duplicate/invalid context rejection, bounded context count, no fallback and
  zero scan/Provider calls from the Gateway;
- cross-MCP/REST OperationId replay and parameter conflict;
- stable denial of every initial mutation with zero fake Provider calls;
- vendor/private-data redaction;
- non-loopback Origin rejection;
- MCP SessionId/clientInfo audit;
- listener stop/reuse and atomic rollback after partial bind failure;
- structural artifact validation; and
- CMake/qbs source and dependency synchronization.

Phase 2 additionally covers:

- plugin discovery with default-off listeners;
- stable settings object names and accessible names;
- port `0`, equal-port rejection, immediate Apply, safe persistence, restart,
  shutdown release, and atomic rollback after either bind fails;
- English source strings and Simplified Chinese settings translations; and
- a standard-library Python client process that performs MCP initialize,
  `tools/list`, all controller read views and REST equivalents against a real
  loopback `GatewayServer`, including cross-transport OperationId replay,
  audit identity, conflict handling, and all seven read-only mutation denials.

The Python program is a client/probe only. It contains no listener, server,
sidecar, Provider, or Mock state. In the QtTest it reads a test-only
`AutomationService` substitute. Final product acceptance must point the same
probe at the single IDE process after existing Workbench Scan and Diagnostics
Mock workflows have created the IDE-owned values.

These are unit/loopback results only. Real-selected topology test values prove
the source-separated projection contract, not a live Product API connection or
hardware scan. The source task owns final product startup, the unique live
controller binding, real controller tests, and hardware acceptance.
