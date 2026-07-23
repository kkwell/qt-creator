# EtherCAT Online Controller Integration

## Scope

This document is the engineering handoff for the staged online EtherCAT
integration. It separates five evidence classes:

- offline project configuration;
- local Mock scan and diagnostics;
- read-only real-controller communication;
- stateful discovery under a control lease; and
- controlled writes such as package activation, SDO/PDO output, run control,
  or firmware changes.

`ISSUE-CORE-CONTROLLER-CONNECTION-API-001` adds only the in-process semantic
contract required by a future controller plugin. It adds no socket, protocol
codec, UI, bus scan, state transition, or hardware execution.

`ISSUE-CORE-CONTROLLER-PROVIDER-PROFILE-002` revises that semantic contract
before a concrete plugin consumes it. A controller manufacturer or wire
protocol is represented by an independent Provider plugin with provider-owned
profiles and arbitrary named channels. Protocol-specific endpoint data never
enters the common Workbench/Core request.

`ISSUE-ONLINE-PRODUCTAPI-READONLY-ADAPTER-001` implements the first concrete,
headless Provider. It owns the Embed Labs ECAP codec and three-channel Qt
Network lifecycle, but adds no UI, scan, control lease, state change, or
hardware-write operation.

## Multi-vendor adapter boundary

`EtherCATProductApi` is the first headless adapter and owns only the Embed Labs
Product API v1.9 implementation. ECAP framing, the three sockets, numeric
message types, CRC32C, SessionId/BootId, request correlation, and Product API
error codes remain private to that plugin.

Future controller protocols use independent plugins that register their own
`ControllerConnectionProvider`. Workbench consumes only provider/profile IDs
and semantic snapshots. It must not downcast a Provider, branch on vendor
names, manufacture protocol defaults, or silently switch to another Provider
when the selected one disappears.

## Embed Labs Product API endpoints

| Channel | Endpoint | Intended use |
|---|---|---|
| Control | `192.168.3.101:15200` | Handshake, state, commands, and read-only queries |
| Push | `192.168.3.101:15201` | State, alarm, performance, firmware, and heartbeat push |
| Bulk | `192.168.3.101:15202` | Capability and bulk transfer |
| Base endpoint | `192.168.3.101:15200` | User-facing Qt/Python connection value |

The `EtherCATProductApi` adapter owns these defaults and all
`Qt::Network` use. `EtherCATData`, `EtherCATCore`, Project, Devices,
Workbench, Scan, and Diagnostics do not own sockets or ECAP frames.

## Authoritative protocol sources

The authoritative files are read-only on
`C:\Users\Kvell\Project\zynq\ethercat-master`:

- `docs\knowledge_base\protocols\product_api_v1.md`
- `docs\knowledge_base\diagnostics\diagnostic_catalog_v1.md`
- `tools\tests\product_firmware_hardware_gate.py`
- `igh_osless\tools\product_api_client.py`

The 2026-07-23 audit copies have these SHA-256 values:

| File | SHA-256 |
|---|---|
| `product_api_v1.md` | `95bc5ded13b7403322d959b45048564c8bf0f3ae527785a98e73ef3b9d0079ea` |
| `diagnostic_catalog_v1.md` | `46415f76f1f5153a5c523cecfd161fa7828cdd01f33d7b6029f2f5ec62c2e4fc` |
| `product_api_client.py` | `0a6da19e764fb1719661771bf28f91c6080088b348fc3252ecd7b7954eb1ccdd` |
| `product_firmware_hardware_gate.py` | `d8fb11c050dde6fcefa77c0bb5d47243911d7a5e602896e6526edcc64eaee0e6` |

A changed hash requires a new protocol audit before implementation assumptions
are reused. The Windows repository is not modified by this Qt product.

## Product API safety boundary

Product API v1.9 uses a fixed ECAP frame contract and one SessionId/BootId
across Control, Push, and Bulk. The implementation validates channel
role, negotiated version, payload limit, SessionId, BootId, RequestId,
Sequence, message length, and CRC before publishing a semantic snapshot.

The protocol defines these operations as read-only and requiring no control
lease:

- handshake;
- `GetState`;
- `GetCapability`;
- `GetPackageState`;
- `GetTimeCorrelation`;
- `GetFirmwareState`; and
- event subscription/recovery.

The current Qt adapter deliberately exposes a smaller, closed outbound
allow-list:

| Channel | Current adapter request |
|---|---|
| All required channels | `HELLO (0x0001)` |
| Control | `GetState (0x0108)` |
| Bulk | `GetCapability (0x0400)` |
| Control | `GetPackageState (0x0403)` |
| Control | capability-gated `GetFirmwareState (0x0504)` |
| Push | feature-gated `ResumeEvents (0x0210)` |

`GetTimeCorrelation` is therefore protocol-read-only but is not sent by the
current adapter. Neither a generic Provider consumer nor a Workbench action
may add a numeric message outside this list. AcquireControl, Control Heartbeat,
discovery, SDO/PDO, state transitions, package operations, firmware writes,
and every other command remain excluded.

`DiscoverTopology` and `DiscoverModules` are not ordinary read-only refreshes.
They require:

1. a current control lease;
2. explicit entry into configuration mode;
3. confirmed `SHUTDOWN`;
4. the discovery command and its staged completion;
5. exit/cleanup and lease release; and
6. before/after state, BootId, package, fault, and lease-owner comparison.

The current protocol contract additionally requires no active package for
discovery. Connecting, opening a page, refreshing, or starting the product must
never trigger this workflow implicitly.

## Read-only real-controller evidence

On 2026-07-23 the authoritative Windows Python client was used against the
three controller endpoints. No lease was acquired, no state was changed, and
no discovery or write was requested.

Observed:

- Product API v1.9 handshake succeeded.
- Control and Bulk reported the same BootId.
- Feature bits were `0x7ff`.
- The default control lease was 5,000 ms and the lease owner was zero.
- Service state was `SHUTDOWN`; status contained `READY`.
- Aggregate severity, current faults, and latched faults were zero.
- Expected/actual WKC and cycle count were zero.
- Capability reported 64 slaves, 125,000 ns minimum cycle, eight cyclic
  frames, and 4,096-byte process input/output limits.
- Firmware state was `CONFIRMED`, active slot A, with no failure.
- A persistent active configuration package exists.

This proves only the reference-client read-only path. It is not evidence that
the Qt product already connects to hardware.

## Confirmed issues and handoff

### Client defect: firmware push before event-resume result (`ISSUE-API-011`)

The reference Python client's `resume_events(0)` can fail with:

`ProductApiError: unexpected frame before ResumeEventsResult`

A manual read showed legal RequestId-zero replaceable frames before the result:
ControllerState, PerformanceSnapshot, FirmwareProgress, and PushHeartbeat.
Product API v1.9 explicitly permits the firmware progress snapshot on a new
subscription. `_consume_replaceable_push()` handles the other replaceable
frames but omits FirmwareProgress.

Recommended controller-client repository change:

1. accept `MSG_FIRMWARE_PROGRESS` in `_consume_replaceable_push()`;
2. validate it through `decode_firmware_state(frame)`;
3. return `True`; and
4. add a regression where FirmwareProgress precedes ResumeEventsResult.

This is a confirmed reference-client defect, not evidence of a server defect.

### Protocol contract gap: opaque Capability payload (`ISSUE-API-012`)

The authoritative protocol defines `GetCapability` (`0x0400`) and Capability
(`0x0480`) but does not define a payload size, version, field layout, or strict
decoder. The audited Python client likewise returns only opaque bytes.

The observed 64-slave, 125,000-ns, eight-frame, and 4,096-byte limits therefore
remain hardware evidence, not a wire-layout contract that the Qt adapter may
guess. Until the Windows controller task publishes a versioned Capability
descriptor schema and golden fixture, the adapter may validate and hash the
opaque response but must leave decoded `ControllerCapabilitySummary` limits
unknown.

The same documentation audit, tracked as `ISSUE-API-013`, found that the first
`quint64` in the 16-byte PushHeartbeat payload is not defined. Only its
replaceable response flags and second BootId field are currently authoritative.
ControllerState and PerformanceSnapshot push flags also require an explicit
contract statement.

### Response-form status alignment (`ISSUE-API-014`)

The coordinated Windows controller task found that recognizable errors from
GetPackageState were being degraded into CommandStatus and that
`UNSUPPORTED (-14)` was documented as reserved even though the service can
deliver it. Controller commit
`e23722da4265311f09a0d89125b76f0f6bfec93e` aligns the protocol document,
reference client, server, and tests.

The Qt adapter now follows the forwarded exact response-form status sets.
CommandStatus accepts `UNSUPPORTED`; PackageState retains BAD_MESSAGE,
BAD_SESSION, STALE_BOOT, BAD_SEQUENCE, and INTERNAL as controller-originated
typed errors; FirmwareState retains BAD_SEQUENCE. Statuses that do not belong
to the specific response form remain protocol errors. The controller task
reported no hardware interaction for this documentation and host-test change.

### Master API gap: no physical topology edges

The current topology result exposes position, station address, AL state,
VendorId, ProductCode, RevisionNo, and SerialNo. Link diagnostics provide
per-port counters, but neither response identifies parent/child ports, peer
nodes, or physical edges.

The Qt product can therefore render only a truthful linear scan order.
Authoritative branch, star, or port-to-port topology requires a versioned
master API containing at least:

- node identity;
- local port;
- peer node identity;
- peer port; and
- link/edge validity or generation.

Until that ABI exists, the UI must not infer a graph from station position.

### Master security gap

Product API v1.9 does not provide authentication, authorization, or transport
encryption. It is suitable only on an isolated management network. Broader
deployment requires a separately versioned security design and cannot be
claimed as a Qt client-only fix.

### Stateful discovery risk, not yet a defect

The current controller has an active package while the discovery contract
requires no active package and a leased transition to `SHUTDOWN`. This is a
safety precondition conflict that must be resolved and explicitly authorized
before a real scan. It is not classified as a master defect without a failed
request and controller-returned diagnostic evidence.

### Qt adapter status and remaining product gaps

The headless Qt adapter now implements private ECAP framing, three asynchronous
channels, the read-only request allow-list, semantic connection snapshots,
bounded reconnect, and generation-based cleanup. An auxiliary Push or Bulk
loss currently tears down Control, Push, and Bulk together and attempts a
bounded resume of the complete Product API session; it is not an independent
single-channel reconnect. Local codec and loopback validation is Mock protocol
evidence only; no real Qt-controller result is claimed.

The current Qt product still lacks:

- an embedded Communication page and Connect/Disconnect actions;
- Provider-neutral real/Mock scan routing;
- the leased discovery state machine;
- a separate Current Bus (Actual) tree;
- ESI re-match and config/actual Apply;
- an embedded Project/Actual/Overlay topology page; and
- real Push/Bulk diagnostics mapping.

## CODESYS-informed workflow

The product follows the useful CODESYS separation without copying its visual
assets or window layout:

- install XML through a software-managed
  [Device Repository](https://content.helpme-codesys.com/en/CODESYS%20Development%20System/_cds_cmd_device_repository.html);
- run an explicit
  [Scan for Devices](https://content.helpme-codesys.com/en/CODESYS%20EtherCAT/_ecat_cmd_scan_devices.html)
  on the selected master;
- retain the actual result separately and compare it with the project before
  applying changes, as in the official
  [EtherCAT commissioning flow](https://content.helpme-codesys.com/en/CODESYS%20EtherCAT/_ecat_tutorial.html);
- preserve an unknown actual device when its ESI is absent, then import and
  re-match instead of guessing; and
- keep the left device tree responsible for selection while the right side
  hosts embedded editors and topology.

The existing software-managed ESI directory is
`Core::ICore::userResourcePath()/ethercat/esi`. Imported XML is validated,
copied to content-addressed storage, and indexed; users should not mutate that
internal directory directly.

## Delivery order

1. `ISSUE-CORE-CONTROLLER-CONNECTION-API-001` — implemented prerequisite
2. `ISSUE-CORE-CONTROLLER-PROVIDER-PROFILE-002` — implemented prerequisite
3. `ISSUE-ONLINE-PRODUCTAPI-READONLY-ADAPTER-001` — implemented and locally
   qualified; real Qt hardware qualification pending
4. provider-neutral embedded Communication page and Connect/Disconnect
5. Provider-neutral Scan workflow
6. Product API discovery state machine
7. ESI match/import/re-match
8. Project Configuration versus Current Bus tree and Apply
9. Embedded Project/Actual/Overlay topology
10. Real diagnostics Provider

Each issue is independently tested and committed locally on `embed-labs`.
No Qt product commit is pushed to a remote repository.

The exact adapter ownership, outbound allow-list, unknown-field policy, and
lifecycle contract are in `docs/ethercat-product-api.md`.
