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

`ISSUE-WORKBENCH-CONTROLLER-COMMUNICATION-001` is the first consuming UI issue.
It embeds a provider-neutral Communication page for a selected Master and
exposes only explicit Connect, Refresh, and Disconnect commands. It adds no
new Product API message, control lease, discovery, configuration, or runtime
transition.

Those two issue descriptions remain the historical read-only baseline. The
current local control extension adds typed provider-neutral control requests,
Product API lease/command handling, a read-only Actual Bus result, and
Workbench commissioning controls in the embedded Communication page. Runtime
Run, Pause/Resume, and Controlled Stop use Qt Creator's native lower-left
quick-control area. The headless Provider now accepts an already-built ECPKG
for typed upload/validate/optional-activate. The selected Master's embedded
Deployment page now invokes that semantic API, presents its OperationId,
progress, selectors, and audit events, and sends all operator-facing results
to Application Output. ECPKG construction/signing, SDO/PDO access, firmware
writes, and a vendor-neutral physical-edge graph remain absent.

## Current operator workflow

Connecting and controlling a previously commissioned controller no longer
implies a bus scan. Connect establishes the Product API session, reads the
authoritative snapshots, and requests the exclusive management lease. It does
not change the controller state or topology.

The normal lower-left control path is intentionally short:

- from `SHUTDOWN`, Run restores the exact persistent active package and then
  starts it;
- from `OP_SAFE`, Run starts the already active package;
- from `PAUSED`, Run resumes it; and
- Stop performs Controlled Stop when required and then enters `SHUTDOWN`,
  confirming that cyclic traffic, WKC, and DC are inactive.

None of those paths discovers the bus. **Rescan Bus** is the only command that
invokes topology discovery. It remains an explicit commissioning operation
for a physical-bus change and therefore requires the lease, configuration
mode, `SHUTDOWN`, and no active package.

The product contains the original XB6 and SV630N vendor ESI XML under its
fixed `ethercat/esi` resource directory. On startup it also indexes vendor XML
dropped into the fixed per-user `ethercat/esi/library` directory. Exact
VendorId/ProductCode/Revision matching supplies device names, functional
classification, PDO/startup data, and DC capability without regenerating or
simplifying the source XML.

While connected, the Product API adapter issues a read-only `GetState` every
500 ms when no refresh, deployment, or control operation is active. Current
cycle count, expected/actual WKC, bus state, and DC difference therefore come
from `ControllerState`. Push `PerformanceSnapshot` data supplies exchange
timing, cyclic error counters, and the optional process-input sample window;
its sample capture cycle is not treated as the controller's current cycle.

A terminal connection, handshake, or protocol failure closes every channel
and publishes `Disconnected`; it does not leave a residual Product API
`Failed` session for the UI to treat as connected. Negotiated protocol,
session, controller state, performance, package, firmware, and online topology
evidence are cleared together. The structured failure remains only as
`lastError`, so Application Output can explain the failure while Connect is
immediately available again and Disconnect is disabled.

## Multi-vendor adapter boundary

`EtherCATProductApi` is the first headless adapter and owns only the Embed Labs
Product API v1 implementation. The current local client contract is v1.10,
with bounded v1.9 compatibility for the persistent release24 runtime. The
2026-07-24 hardware record observed a RAM-only v1.10 CPU0 service and a reboot
return to release24/v1.9; this documentation update did not refresh that
observation. ECAP framing, the three sockets, numeric message types, CRC32C,
SessionId/BootId, request correlation, and Product API error codes remain
private to that plugin.

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

The three Control, Push, and Bulk TCP endpoints were reachable from the Mac
during `ISSUE-WORKBENCH-CONTROLLER-COMMUNICATION-001` qualification. This is
only a network prerequisite by itself. The separate 2026-07-24 UI acceptance
record below proves the completed Qt Product API session and must not be
inferred from reachability alone.

## Authoritative protocol sources

The authoritative files are read-only on
`C:\Users\Kvell\Project\zynq\ethercat-master`:

- `docs\knowledge_base\protocols\product_api_v1.md`
- `docs\knowledge_base\diagnostics\diagnostic_catalog_v1.md`
- `tools\tests\product_firmware_hardware_gate.py`
- `igh_osless\tools\product_api_client.py`

The historical 2026-07-23 v1.9 audit copies have these SHA-256 values:

| File | SHA-256 |
|---|---|
| `product_api_v1.md` | `95bc5ded13b7403322d959b45048564c8bf0f3ae527785a98e73ef3b9d0079ea` |
| `diagnostic_catalog_v1.md` | `46415f76f1f5153a5c523cecfd161fa7828cdd01f33d7b6029f2f5ec62c2e4fc` |
| `product_api_client.py` | `0a6da19e764fb1719661771bf28f91c6080088b348fc3252ecd7b7954eb1ccdd` |
| `product_firmware_hardware_gate.py` | `d8fb11c050dde6fcefa77c0bb5d47243911d7a5e602896e6526edcc64eaee0e6` |

A changed hash requires a new protocol audit before implementation assumptions
are reused. The Windows repository is not modified by this Qt product.

## Product API safety boundary

Product API v1 uses a fixed ECAP frame contract and one SessionId/BootId
across Control, Push, and Bulk. The implementation validates channel role,
negotiated version, payload limit, SessionId, BootId, RequestId, Sequence,
message length, and CRC before publishing a semantic snapshot. A v1.10
session additionally requires explicit timing-mode start feature bit 11, so
the complete required feature mask is `0xfff`. The 2026-07-24 record observed
`0xfff` on the RAM-deployed v1.10 service and `0x7ff` on persistent release24
after a reboot; this documentation update did not re-query either runtime.

The protocol defines these operations as read-only and requiring no control
lease:

- handshake;
- `GetState`;
- `GetCapability`;
- `GetPackageState`;
- `GetTimeCorrelation`;
- `GetFirmwareState`; and
- event subscription/recovery.

Connect and Refresh use this closed outbound read-only allow-list:

| Channel | Current adapter request |
|---|---|
| All required channels | `HELLO (0x0001)` |
| Control | `GetState (0x0108)` |
| Bulk | `GetCapability (0x0400)` |
| Control | `GetPackageState (0x0403)` |
| Control | capability-gated `GetFirmwareState (0x0504)` |
| Push | feature-gated `ResumeEvents (0x0210)` |

`GetTimeCorrelation` is therefore protocol-read-only but is not sent by the
current adapter. The typed control extension additionally allows only:

| Channel | Controlled request |
|---|---|
| Control | `AcquireControl`, `ReleaseControl`, and lease `Heartbeat` |
| Control | `EnterConfigurationMode` |
| Control | `DiscoverTopology` |
| Control | `RestoreActivePackage` |
| Control | v1.10 `StartFreeRun (0x010c)` and `StartDc (0x010d)` |
| Control | backward-compatible automatic `Start (0x0102)`, plus `Pause`, `Resume`, and `ControlledStop` |
| Bulk | ECPKG `BulkBegin`, `BulkChunk`, `BulkCommit`, and safe `BulkAbort` |
| Control | exact-selector `ValidatePackage`, `ActivatePackage`, and recovery `RollbackPackage` |

Neither a generic Provider consumer nor the Workbench Deployment page may add
an arbitrary numeric message outside these lists. Reset, DiscoverModules,
SDO/PDO, firmware writes, and other bulk or controller commands remain
excluded. Workbench supplies one immutable artifact and semantic options to
the Provider; only the vendor adapter emits the listed requests.

The control lease is controller-authoritative and exclusive. After an
authoritative connected snapshot is available, Workbench normally submits one
AcquireControl request for that exact Provider/profile/scope/session
generation; the page's Acquire action is a manual recovery path. Other API
sessions remain free to connect and perform read-only queries, but their
control writes are rejected with `LEASE_BUSY (-10)` while another session owns
the lease. A client must not infer permission from socket connectivity.

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

## Controlled commissioning flow

The current local implementation separates ordinary runtime control from the
explicit commissioning path:

1. Connect and complete the read-only three-channel refresh.
2. Workbench automatically requests AcquireControl for that authoritative
   session snapshot; use the Communication-page Acquire action only when a
   manual retry is needed.
3. For an unchanged physical bus, use Run directly. From `SHUTDOWN`, Workbench
   restores the exact persistent slot, generation, and configuration ID and
   then starts it. No Configuration or discovery command is sent.
4. Only after the operator invokes **Rescan Bus**, enter configuration mode,
   confirm ready `SHUTDOWN` with no active package, and run
   DiscoverTopology. Preserve its result as read-only Current Bus evidence.
5. After commissioning, restore the exact persistent package and confirm
   `OP_SAFE`, current-Boot activation, OP bus state, nonzero matching WKC, and
   no current or latched faults.
6. In `OP_SAFE`, Run sends automatic-mode `Start (0x0102)`; the active package
   determines FreeRun versus Distributed Clocks. In `PAUSED`, Run sends Resume.
7. Debug pauses from `RUNNING` or resumes from `PAUSED`, with authoritative
   state confirmation.
8. Stop performs Controlled Stop when needed and then Configuration, finally
   confirming `SHUTDOWN`, no active application, zero WKC/AL state, and DC off.
9. ReleaseControl, then Disconnect. Releasing a management lease from
   `RUNNING` or `PAUSED` leaves the configured cyclic task autonomous.

The client does not report a state-changing command Succeeded merely because
the controller accepted its final command stage. It performs an authoritative
state/package refresh and verifies the command-specific postcondition.
Discovery completes only with a typed TopologyResult; Restore completes only
with the matching typed PackageState response.

Disconnect is allowed from `RUNNING` or `PAUSED` when no command is pending.
It performs ReleaseControl first and tears down the three channels only after
the release succeeds. ReleaseControl changes only management ownership; it
does not imply ControlledStop or Configuration. Process shutdown makes a
bounded best-effort release attempt whenever no control operation is active,
including when the last state is running or unavailable, and cannot claim
controller acknowledgement.

The controller runtime is autonomous after a valid package reaches
`RUNNING`. Loss of the supervisory connection or its heartbeat clears the
lease owner and rejects stale writes, but does not stop the EtherCAT cycle or
reset the package when bus and hardware health remain valid. A later session
may acquire control and manage the same running instance. Real-controller
qualification of this rule waits for the CPU0-only `ISSUE-API-021` deployment;
the IDE-side state machine and offline tests do not constitute hardware
evidence.

### Package-determined FreeRun and Distributed Clocks

Product API v1.10 makes the requested timing mode explicit:
`StartFreeRun (0x010c)` requests FreeRun and `StartDc (0x010d)` requests
Distributed Clocks. Both require negotiated v1.10 and feature bit 11. The
active ECPKG remains authoritative, but its actual mode is classified from
validated ECFG/DC content: zero configured DC slaves means FreeRun, while one
or more valid DC slaves means DC. A legacy manifest may omit timing metadata
and imply requested `auto`; manifest metadata and `DC_LOCKED` do not classify
the actual package mode. Starting does not rewrite the package.

The common real-hardware start gate is exactly:

- controller state `OP_SAFE` and ready;
- active package associated with the current BootId;
- bus operational with the EtherCAT OP AL-state bit;
- expected WKC nonzero and actual WKC equal to expected; and
- current and latched faults both zero.

An external API client that selects an explicit command must match the mode
classified from the active package's validated ECFG/DC content. A mismatch
returns terminal stage-2 `TIMING_MODE_MISMATCH (-35)`. Its 64-bit detail is
`(requested_mode << 32) | actual_mode`, with the requested value in the high
32 bits, the active package value in the low 32 bits, FreeRun `1`, and DC `2`.
The adapter reports this controller detail without treating it as a network or
framing failure.

DC lock and DC difference remain observable evidence; neither identifies the
package timing mode. The current Qt extension cannot build, sign, or change
ECPKG content. Its headless adapter can transfer and activate already-built
bytes, but Workbench cannot select such an artifact yet. A FreeRun or DC
demonstration therefore still requires a matching package to exist on the
controller before Restore and the generic Workbench Run action.

Legacy `Start (0x0102)` remains a backward-compatible automatic-mode request
on v1.10 and older runtimes. Workbench uses it through the standard Run
control; there are no independent FreeRun or DC buttons. The running mode is
evidence from the active package and controller snapshot, not a UI selection.

## Historical read-only real-controller evidence

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

This proves only the reference-client read-only path. By itself it was not
evidence that the Qt product connected to hardware; the later UI acceptance
below is the independent Qt evidence.

## 2026-07-24 Qt read-only UI hardware acceptance

The real-controller flow was exercised through the embedded Workbench
Communication page with Provider `Embed Labs Product API`, Profile `v1.9`, and
endpoint `192.168.3.101:15200`.

| Step | Observed evidence |
|---|---|
| Connect | Succeeded; negotiated protocol v1.9 with `sessionGeneration=1`, `sessionId=10990663912902164094`, and `bootId=5715996203977591977` |
| Lease and controller | No lease held, owner 0; controller `SHUTDOWN`/关停, ready yes, WKC 0/0, DC lock no, OP no, fault `0x0` |
| Channels | Control, Push, and Bulk all Connected; reported limits were 4,096, 65,536, and 65,536 bytes |
| Refresh | Succeeded; the displayed update time changed to `12:18` |
| Disconnect | Succeeded; Control, Push, and Bulk all became Disconnected |
| Process cleanup | Product exit status 0, no matching residual process, and no new Embed Labs DiagnosticReports file |
| Safety | No Scan, configuration write, controller-state transition, FreeRun, DC mode, Run, or Stop was invoked |

One client presentation defect was observed: the status bar briefly continued
to show Disconnected while the Communication page and Provider snapshot showed
the connected three-channel session. The client correction now projects
Provider connection state into the unified status control, and automated
regression covers Connected, Degraded, and Disconnect-to-Offline presentation.
A second real-controller UI revalidation passed on 2026-07-24: the status bar
showed Handshaking, then `Embed Labs Product API — Connected` with
real-controller read-only evidence, and returned to Disconnected after
explicit Disconnect.

## Historical 2026-07-24 headless controlled-flow hardware evidence

At the time of this run, the RAM-deployed service negotiated Product API v1.10
and feature bits `0xfff` independently on Control, Push, and Bulk. The channels
reported a common BootId `5715996203977591977`, the preflight lease owner was
zero, and the controller was ready, fault-free, and in `SHUTDOWN` with WKC
`0/0`. This deployment is not persistent: rebooting the controller returns to
the release24/v1.9 service.

The persistent selector is `A/11/810`. Its trusted package SHA-256 is
`0d633848c064a5828083448ec2a6edff11251cb7159e93829f00f0dcd032aef6`.
Its legacy manifest omits timing metadata, while validated ECFG contains two
DC records, so the controller classifies it as DC. `B/10/810` is also DC and
its signing key is no longer trusted. No usable FreeRun package is currently
available. Automatic boot restore previously failed with result `-18`,
followed by safe recovery result `-1009`; the controller remained in
`SHUTDOWN` with the CPU1 runtime package inactive.

The 2026-07-24 headless Qt hardware run produced this evidence:

- the v1.10 three-channel connection, capability gate, lease acquisition, and
  Configuration transition succeeded;
- the first scan exposed a Qt decoder defect because TopologyResult offset 20
  contains `combined_al_state`, not a zero reserved field;
- after correcting and regression-testing that decoder, DiscoverTopology
  completed all four stages and returned three slaves at `0x1001`, `0x1002`,
  and `0x1003`;
- exact RestoreActivePackage for `A/11/810` then returned terminal stage 3
  `CAPABILITY_MISMATCH (-20)`, CPU1 result `-4`, and detail `11`;
- authoritative cleanup confirmed fault-free `SHUTDOWN`, released the control
  lease, and disconnected all three channels; and
- the online Capability descriptor is 96 bytes with SHA-256
  `74ea5e67b3e1d7ba575339b636abb5432ecf6790d3504d32ce1756bcfec49568`.

This is real evidence for Connect, AcquireControl, Configuration,
DiscoverTopology, ReleaseControl, and cleanup. It does not qualify Restore,
StartDc, StartFreeRun, Pause, Resume, or ControlledStop. The Windows
controller task must compare the online Capability descriptor with the
package-bound descriptor and rebuild or correct the package before the
remaining lifecycle can run.

FreeRun or DC can be claimed only when the active package's validated ECFG/DC
classification, the matching explicit v1.10 command, and corresponding
runtime evidence are all recorded. DC lock alone does not identify either
mode.

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

Product API v1 does not provide authentication, authorization, or transport
encryption. It is suitable only on an isolated management network. Broader
deployment requires a separately versioned security design and cannot be
claimed as a Qt client-only fix.

### Stateful discovery precondition and restore boundary

A persistent selector can exist while the CPU1 runtime package is inactive.
Discovery still requires `SHUTDOWN` with no active controller package. The
client retains the exact persistent selector, enters Configuration, confirms
that the runtime package is not Active, scans, and then restores that exact
selector.

The 2026-07-24 controller evidence qualified the Configuration and discovery
portion of this sequence. Restore of `A/11/810` remained blocked by the
controller's typed `CAPABILITY_MISMATCH (-20)` result. That rejection is
preserved as a controller/package compatibility issue rather than being
reclassified as a scan or transport failure.

### Qt adapter status and remaining product gaps

The headless Qt adapter now implements private ECAP framing, three asynchronous
channels, the closed read-only and typed-control request lists, lease
Heartbeat, staged command correlation, command postcondition refresh,
TopologyResult, exact active-package restore, bounded reconnect, and
generation-based cleanup. An auxiliary Push or Bulk loss currently tears down
Control, Push, and Bulk together and attempts a bounded resume of the complete
Product API session; it is not an independent single-channel reconnect.

Local source and loopback behavior remain non-hardware evidence. The
2026-07-24 headless run above separately verifies the real leased
Configuration and DiscoverTopology path through the production Provider, but
not restore or runtime transitions.

The current Qt product still lacks:

- ECPKG construction, signing, and editing of validated ECFG/DC content;
- serialization of an applied offline Project into a validated controller
  package and proof that the deployed package matches that Project;
- an embedded Project/Actual/Overlay topology page; and
- real Push/Bulk diagnostics mapping.

The current local implementation does contain typed leased discovery and a
separate read-only linear Actual Bus tree. It also provides an explicit
undoable `Apply Current Bus to Project` engineering command. That command
matches each actual identity against the local ESI repository, preserves
customized configuration only when the same identity remains at the same
position, and retains unknown devices without inventing configuration. It
changes only the local Project and never writes to the controller. Its current
evidence is a deterministic Provider regression; real-controller application
must follow a fresh safe scan.

### Embedded Communication page boundary

`ISSUE-WORKBENCH-CONTROLLER-COMMUNICATION-001` consumes only
`ControllerConnectionProvider`. The Workbench page displays the selected
Provider and profile, redacted endpoint summary, connection state,
provider-named channels, negotiated session/version when present, read-only
controller summaries, freshness, and structured errors from the immutable
snapshot. Workbench does not downcast the Provider or assume
Control/Push/Bulk; another adapter may publish a different channel topology.

Provider and profile selection are explicit for the selected Project/Master
scope. More than one installed Provider never causes first-provider
selection, and removing the selected Provider leaves an unavailable selection
instead of switching controller vendors. The pair is not persisted in the
current project format.

Connect, Refresh, and Disconnect are shared ActionManager commands. The
Connect transport request and its initial refresh use the selected
Provider/profile and the read-only allow-list. Once an authoritative session
snapshot is available, Workbench queues automatic AcquireControl for that
exact session generation. Refresh asks the same Provider for a new snapshot.
Disconnect explicitly asks that Provider to clean up; it remains distinct from
merely navigating away from the page. Opening or closing the page, selecting
another node, switching property pages, opening a Project, or starting the
application never connects or disconnects automatically.

The Communication page presents Acquire, Configuration, Scan Bus, Restore
Package, and Release, together with connection actions, an inline base-endpoint
editor, Actual Bus, and authoritative information. Acquire is normally
automatic and remains visible for necessary manual recovery. The page has no
FreeRun, DC Run, Pause, Resume, or Stop button.
Run/Pause/Resume/Controlled Stop are routed through Qt Creator's native
lower-left quick-control area. The page and quick controls cannot submit an
arbitrary numeric Product API message.

The base endpoint accepts `IPv4` or `IPv4:basePort`; omitted ports use `15200`
and ProductApi derives Push/Bulk as the next two ports. Saving is a local
validated settings change and never connects. The editor is disabled while a
connection is active.

Connection banners, safety prose, command progress, and errors are not rendered
as separate Communication-page labels. They are written to the dedicated
**EtherCAT Controller** tab in **Application Output**. That channel is passive,
does not create a run task, and deduplicates semantic connection, lease,
service, command, topology, and error changes while ignoring heartbeat
timestamps. Controller connection state is likewise absent from the global
status bar; the lower-left actions provide the compact actionable projection.

Closing a Project is the sole automatic connection cleanup. Workbench clears every
in-memory controller selection belonging to that Project and requests
Disconnect from every Provider whose current snapshot belongs to it and is
not already Disconnected or Disconnecting. An unrelated Project close does
not affect the selected or connected scope. Cleanup releases only the
management lease; it never inserts ControlledStop or Configuration before
Disconnect, so an autonomous runtime continues.

If a Provider rejects that Disconnect request, Workbench performs no more than
five total Disconnect attempts. The retry identity is the Provider
registration epoch plus the exact connection scope and `sessionGeneration`
captured at Project close. Provider removal/re-registration, a scope change,
or a new session generation makes the old retry stale and cancels it, so
cleanup cannot disconnect a replacement Provider instance or a newer session.

If all five attempts are rejected, automatic cleanup stops and reports the
manual recovery path. The user can select the residual adapter explicitly from
the Communication page of any open EtherCAT Master and invoke Disconnect. A
residual Failed session remains visible with this cleanup guidance; it is not
reported as already disconnected.

The page authorizes commissioning writes only after the session owns the
exclusive lease. Its Scan Bus result is separate from the local Mock Scan
Provider and never changes the offline Project. Mock Scan/Diagnostics UI is
hidden by default in production and is registered only in a `WITH_TESTS` build
or with `QTC_ETHER_CAT_ENABLE_MOCK_UI=1`. The page still does not implement
ECPKG construction/signing, invocation of the headless upload/activation API,
ECFG/DC editing, SDO/PDO access, firmware writes, or real Diagnostics Provider
mapping.

The following qualification is the historical read-only Communication issue
and does not qualify the later control extension. It is retained as dated
evidence:

- Communication-focused behavior passed 6/6 at normal scale and 6/6 at 2x;
- the complete Workbench suite passed 89/89;
- the other six isolated plugin suites passed 92/92, for a seven-suite total
  of 181/181;
- the Qt 6.11.0 `WITH_TESTS=OFF` product build passed;
- enabled and explicitly disabled Workbench lifecycle checks passed; and
- `qtcreator_zh_CN.qm` generation and every newly added Communication
  translation check passed.

The reachable three TCP ports remain only network evidence by themselves. The
2026-07-24 UI acceptance separately verified real Qt
Connect/Refresh/Disconnect, clean three-channel teardown, and clean process
exit. The observed status-bar Disconnected presentation defect is fixed in the
client and covered by automated regression. Its second real-controller UI
revalidation also passed, including Handshaking, Connected, and
Disconnect-to-Offline presentation.

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
   qualified; later real read-only hardware use verified through Workbench
4. `ISSUE-WORKBENCH-CONTROLLER-COMMUNICATION-001` — provider-neutral embedded
   Communication page and explicit Connect/Refresh/Disconnect; locally
   qualified and real read-only hardware flow verified; status-bar client
   correction passed automated regression and second real-controller UI
   revalidation
5. Provider-neutral typed control contract, automatic Acquire, embedded
   commissioning surface, and native quick-control routing — implemented; the
   current English regression passed all seven EtherCAT suites
6. Product API v1.10 explicit StartFreeRun/StartDc protocol capability, plus
   lease/Configuration/Discover/Restore/Start/Pause/Resume/Stop state machine
   and read-only Actual Bus tree — implemented; generic Start and explicit DC
   real-controller lifecycles passed; `cfg812` was not activated and no FreeRun
   lifecycle ran
7. Delegated Windows deployment of the v1.10 runtime and confirmation of
   feature bits `0xfff` — confirmed as a RAM-only deployment; persistent
   integration remains pending
8. Explicit controlled hardware acceptance with complete recovery and cleanup
   evidence — generic Start and explicit DC each passed 3 tests with 0 failures
   and ended safely in `SHUTDOWN`/EMPTY with no lease owner or fault. Windows
   `ISSUE-RT-009` then separated a successful LRW cycle from the actual
   `cfg812` failure: `OP_REQUEST` for station `0x1002` returned WKC 0/1 on all
   four attempts. The controller was safely rolled back to `B/12/813`,
   `OP_SAFE`, WKC 11/11, faults 0, and lease 0. The Windows session continues
   with `ISSUE-API-016` and the smallest isolated fix
9. ECPKG construction/signing and Workbench deployment with explicit
   validated ECFG/DC mode content — headless upload/validate/activate, safe
   rollback, and the prebuilt-package Deployment page are implemented and
   loopback-qualified; the builder, signing pipeline, and real-controller
   deployment remain pending
10. ESI match/import/re-match — implemented for explicit Current Bus apply;
    an unknown identity remains unconfigured until matching XML is imported
11. Project Configuration versus Current Bus Apply — implemented as an
    undoable local-project action; an embedded Overlay view remains pending
12. Embedded Project/Actual/Overlay topology
13. Real diagnostics Provider

Completed issues remain local on `embed-labs`. Test and hardware claims above
are limited to commands actually observed. The current English regression
passed Workbench 95, Project 15, Devices 8, Core 19, Scan 11, Diagnostics 7,
and ProductApi 71: 226 passed, 0 failed, and 1 ProductApi hardware test
skipped. The
`WITH_TESTS=OFF` product build passed with exactly 17 plugin dylibs, and all
EtherCAT Simplified Chinese contexts contain no unfinished or empty
translations. `cfg812` was not activated and no FreeRun lifecycle ran.

The earlier single-instance product observation showed Workbench, Simplified
Chinese, hidden production Mock UI, and the compact tree. The current endpoint
and unified-output revision is covered by widget-level Workbench and passive
Application Output tests. No controller connection or hardware command was
executed in this round. No Qt product change is pushed to a remote repository.

The exact adapter ownership, outbound allow-list, unknown-field policy, and
lifecycle contract are in `docs/ethercat-product-api.md`.
