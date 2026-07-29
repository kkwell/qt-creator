# EtherCAT Product API Adapter

## Scope

`EtherCATProductApi` is the first concrete controller-adapter plugin for the
product-owned, multi-vendor `ControllerConnectionProvider` contract. It owns
only the Embed Labs Product API v1 transport. The current client contract is
v1.11, with bounded v1.10 and v1.9 compatibility for older controller
runtimes. Product API v1.11 adds capability-gated, compare-and-clear fault
confirmation; it does not turn fault reset into an unconditional controller
reset. The 2026-07-24 hardware record observed a RAM-only v1.10 CPU0 service
and a reboot return to release24/v1.9; this documentation update does not
rewrite or refresh that historical observation. A future controller family
uses a separate plugin and Provider instead of adding vendor switches to this
plugin, EtherCATCore, or EtherCATWorkbench.

The plugin is headless. It registers one connection Provider in the Qt Creator
object pool and publishes immutable semantic snapshots. It does not create a
window, page, action, output pane, project field, or diagnostics stream. The
Workbench Communication page consumes the generic Provider contract without
including this plugin's private headers. Stateful discovery is returned as a
provider-neutral actual-topology snapshot; it never mutates the offline
Project.

## Ownership and dependencies

`EtherCATProductApi` depends on `EtherCATData`, `EtherCATCore`, `Utils`, and
`Qt::Network`. It is the only current EtherCAT plugin that owns:

- ECAP frame encoding, incremental stream parsing, and CRC32C;
- Control, Push, and Bulk `QTcpSocket` instances;
- Product API message types, channel roles, payload limits, and field layouts;
- SessionId, BootId, RequestId, Sequence, timeout, and reconnect correlation;
- control-lease acquisition, renewal, release, and command correlation;
- staged command, topology-result, and restored-package response handling;
- Product API status-name and retry mapping; and
- decoding into the public controller, capability, package, firmware, channel,
  session, heartbeat, control-progress, topology, and structured-error
  summaries.

No socket, byte frame, numeric protocol field, host, port, or controller ABI is
exported through EtherCATData or EtherCATCore. Workbench, Project, Devices,
Scan, and Diagnostics remain transport-free.

## Provider profile and endpoints

The Provider ID is
`EtherCAT.Connection.EmbedLabs.ProductApiV1`. Its default profile has a fixed
UUID so selection remains stable across restarts. The profile's base endpoint
is user-configurable as `IPv4` or `IPv4:basePort` and is stored under
`EtherCAT/ProductApiV1/BaseEndpoint`. Omitting the port selects `15200`; Push
and Bulk are always derived as `basePort + 1` and `basePort + 2`, so the
accepted base-port range is `1..65533`. The production defaults are:

| Channel | Endpoint | Maximum payload |
|---|---|---:|
| Control | `192.168.3.101:15200` | 4,096 bytes |
| Push | `192.168.3.101:15201` | 65,536 bytes |
| Bulk | `192.168.3.101:15202` | 65,536 bytes |

Only the base endpoint summary is published in the generic profile and
snapshot. The Push/Bulk endpoint details never become connection-request
fields. Construction does not connect: a consumer must explicitly call Connect
for a valid Project/Master scope and this Provider's profile.

Endpoint changes are accepted only while the session is `Disconnected` or
`Failed`. A successful change closes no live connection, opens no socket, and
resets stale session, protocol, error, topology, control-progress, and channel
evidence before publishing the new disconnected snapshot. Test-injected
providers do not write user settings.

## Typed outbound request policy

Connect and Refresh use this explicit read-only allow-list:

| Channel | Allowed request |
|---|---|
| All | `HELLO (0x0001)` |
| Control | `GetState (0x0108)` |
| Bulk | `GetCapability (0x0400)` |
| Control | `GetPackageState (0x0403)` |
| Control | `GetFirmwareState (0x0504)`, only for negotiated v1.9 or later with the firmware feature |
| Push | `ResumeEvents (0x0210)`, only when exact replay is negotiated |

After an explicit typed control request, the adapter can additionally emit:

| Channel | Controlled request |
|---|---|
| Control | `AcquireControl (0x0100)` |
| Control | `ReleaseControl (0x0101)` |
| Control | legacy `Start (0x0102)`, retained as a backward-compatible automatic-mode request |
| Control | `Pause (0x0103)` |
| Control | `Resume (0x0104)` |
| Control | `ControlledStop (0x0105)` |
| Control | `ResetFault (0x0107)`, only for negotiated v1.11 with feature bit 12 |
| Control | lease `Heartbeat (0x0109)` |
| Control | `EnterConfigurationMode (0x010b)` |
| Control | `StartFreeRun (0x010c)`, only for negotiated v1.10 with feature bit 11 |
| Control | `StartDc (0x010d)`, only for negotiated v1.10 with feature bit 11 |
| Control | `DiscoverTopology (0x0401)` |
| Control | `RestoreActivePackage (0x0407)` |

The private codec and typed session expose this closed ECPKG deployment
vocabulary:

| Channel | Deployment request |
|---|---|
| Bulk | `BulkBegin (0x0300)` with configuration ID, package size, zero reserved fields, and package mode `1` |
| Bulk | `BulkChunk (0x0301)` with object kind `4`, an offset, and 1 through 65,528 package bytes |
| Bulk | `BulkCommit (0x0302)` or `BulkAbort (0x0303)` with an empty payload |
| Control | `ValidatePackage (0x0404)`, `ActivatePackage (0x0405)`, or `RollbackPackage (0x0406)` with an exact 24-byte slot/generation/configuration selector |

All three tables are closed at the private codec boundary. A consumer cannot
expand them through a profile, endpoint string, Workbench action, generic
Provider field, or arbitrary numeric message type. The generic Provider accepts
only one immutable, already-built ECPKG plus a nonzero ConfigurationId and
client OperationId. Unconditional controller reset, CPU/NIC/PHY recovery,
SDO/PDO access, firmware write, arbitrary bulk objects, package construction,
and signing remain excluded. Workbench's Deployment page calls only this
semantic Provider request and never sees a numeric Product API message.

Connect is not Scan. The adapter's Connect request establishes the transport
and authoritative read-only snapshot; it does not itself enter configuration
mode, inspect or change outputs, scan the bus, mutate the offline Project, or
apply actual devices. The Workbench consumer normally follows a valid
authoritative snapshot with one queued AcquireControl request for the exact
Provider/profile/scope/session generation. Product API lease Heartbeat is sent
only after this session owns the control lease; it is not a read-only
connection keepalive.

The controller grants one exclusive control lease at a time. Other API
sessions may remain connected and continue the read-only requests above, but a
control write from a non-owner is rejected with `LEASE_BUSY (-10)`. Neither
socket connectivity nor a readable snapshot grants write authority.

## Protocol validation

The private codec enforces the exact 64-byte, big-endian ECAP header, magic and
major version, negotiated minor, per-role payload bound, CRC32C, response
flags, and strictly increasing per-connection Sequence before a frame can
update the snapshot. Product API v1.10 adds explicit timing-mode start feature
bit 11; a v1.10 handshake must therefore advertise feature bits `0xfff`.
The 2026-07-24 record observed `0xfff` on the RAM-deployed v1.10 service and
`0x7ff` on the persistent release24 runtime reached after a reboot. The latter
cannot receive `StartFreeRun` or `StartDc`. The parser accepts arbitrary TCP
fragmentation and multiple coalesced frames without allocating the declared
payload before the fixed header passes validation.

Product API v1.11 adds `CONTROLLED_FAULT_RESET` at feature bit 12
(`0x00001000`), making the cumulative current feature mask `0x00001fff`.
HELLO negotiates the lower of the client and server minor versions. A client
negotiated below minor 11, or a minor-11 peer that does not advertise bit 12,
must reject ResetFault locally as `UNSUPPORTED (-14)` and send no request.
The historical v1.10 and v1.9 feature-mask observations above remain dated
evidence and are not upgraded by this contract.

Control creates the session. Push and Bulk join its nonzero SessionId. All
three handshakes must agree on SessionId, BootId, negotiated minor, role, and
role-specific maximum payload. RequestId is one monotonically allocated,
nonzero namespace for the complete session, not three socket-local counters.

Initial connection completion requires:

1. all three channel handshakes;
2. GetState;
3. opaque GetCapability capture;
4. GetPackageState;
5. GetFirmwareState when advertised; and
6. ResumeEvents when exact replay is advertised.

The adapter accepts validated ControllerState, PerformanceSnapshot,
FirmwareProgress, and PushHeartbeat replaceable frames before
ResumeEventsResult. This deliberately avoids the audited reference-client
defect where a legal FirmwareProgress frame aborts subscription.

Stateful commands use one active RequestId and accept only the response form
and stage sequence assigned to that command. Acquire and Release complete on
their terminal stage. Configuration, all three Start forms, Pause, Resume, and
Controlled Stop must progress through the ordered command stages. Discovery
ends with a typed TopologyResult, while Restore ends with the exact typed
PackageState response. An unrelated, repeated, skipped, malformed, or
wrong-response-form frame is a protocol failure rather than command success.

## Controlled fault confirmation

The provider-neutral `ResetFault` command confirms and clears a fault latch;
it is not a device reboot, CPU reset, runtime restart, topology scan, package
change, NIC/PHY recovery, or safety-rated reset. The caller must capture these
two confirmation values from the same displayed ControllerState snapshot:

- the complete nonzero `latched_faults` mask, containing only known fault bits
  0 through 16; and
- the nonzero `last_alarm_sequence`.

The Product API v1.11 Control request is `ResetFault (0x0107)` with a nonzero
RequestId, the current BootId, and this exact 16-byte network-order payload:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `expected_latched_faults` (`u64`) |
| 8 | 4 | `expected_last_alarm_sequence` (`u32`) |
| 12 | 4 | reserved, must be zero |

The adapter sends it only when the same live Session owns the exclusive lease,
the authoritative service state is exactly `FAULT`, `current_faults == 0`,
the full latched mask is still nonzero, and no other operation is pending.
`RUNNING`, `PAUSED`, transitional states, an active current fault, an
incomplete confirmation, or a changed snapshot are rejected before dispatch.
If a fresh snapshot already has `latched_faults == 0`, the adapter treats the
request as an idempotent local no-op and sends nothing.

CPU1 owns the atomic compare-and-clear. The request succeeds only if
`current_faults` remains zero and both the complete latched mask and alarm
sequence still match. Its CommandStatus sequence is stages
`1 NETWORK_RECEIVED`, `2 CPU0_VALIDATED`, `3 CPU1_ACCEPTED`, and
`4 STATE_COMPLETED`; only stage 4 has `final=1`. Successful stage-4 detail is
the sequence of the committed AlarmCleared event.

The strict success evidence is one 64-byte `AlarmCleared (0x0208)` event
committed before the response snapshot:

- code `FAULT_CLEARED (5)`, severity `INFO (1)`, source `SERVICE (1)`;
- flags exactly `CLEARED (0x2)`;
- `fault_mask` equal to the confirmed complete mask;
- `detail0`, `detail1`, `detail2`, and `reserved0` all zero; and
- the next nonzero sequence after the confirmed alarm sequence.

The terminal snapshot must have both fault masks zero. It is `OP_SAFE` only
when the safe cyclic runtime was already executing; otherwise it is
`SHUTDOWN` with no active runtime. ResetFault never starts a management-loop
real-time task or changes the application, package, selectors, or topology.

A v1.10 legacy empty ResetFault receives terminal `UNSUPPORTED (-14)` with
`ERR_UNSUPPORTED (-7)`. A malformed v1.11 payload receives
`BAD_MESSAGE (-6)` with `ERR_ARGUMENT (-4)`. A still-active cause receives
stage-3 `CPU1_REJECTED (-15)` with `ERR_FAULT_ACTIVE (-6)` and the current
fault mask in detail. A changed confirmation receives the same API status
with `ERR_STALE_CONFIRMATION (-9)` and the latest alarm sequence in detail.
A disallowed service state receives `CPU1_REJECTED (-15)` with
`ERR_STATE (-3)`. The existing terminal-response cache makes a repeated
RequestId idempotent; a new RequestId with old confirmation values is stale.

Windows `ISSUE-API-030` froze this contract after localhost Product API and
OS-less regression. The Qt-side work described here is offline protocol and
UI integration evidence only at this point: no real controller ResetFault
request, AlarmCleared event, or post-reset hardware state has yet been
observed.

## Transactional package deployment

The headless Provider now implements one asynchronous package transaction at a
time. It requires a live same-BootId session, the exclusive control lease,
transactional-bulk capability, a ready controller in `SHUTDOWN`, and no
concurrent refresh, control command, or deployment. The caller supplies exact
ECPKG bytes; the adapter never constructs, rewrites, signs, trusts, or infers
package content. The client enforces the v1.10 ECPKG size bound of 16 MiB
before opening a Bulk transaction.

The v1.10 line sequence is:

1. `BulkBegin`, every `BulkChunk`, `BulkCommit`, and `BulkAbort` each receive
   exactly one terminal `BulkStatus`.
2. Successful Commit must return the exact staged slot, generation, and
   ConfigurationId.
3. Validate and optional Activate each receive ordered `CommandStatus` stages
   1 through 4 with `final=0`, followed by one typed `PackageState`.
4. A failed package command receives one terminal failed `CommandStatus` and
   no `PackageState`.

Upload uses contiguous chunks no larger than 65,528 bytes. Cancel is accepted
only before validation and is completed only after `BulkAbort` succeeds. A
bulk failure after a successful Begin also attempts Abort while that Bulk
connection remains usable. A disconnect discards the connection-local upload;
the adapter never resumes at an old byte offset.

Product API v1.10 has no generic OperationId. The adapter binds the caller's
OperationId to an artifact/configuration/options SHA-256 fingerprint in a
bounded 32-entry in-memory journal. An identical replay returns the stored
semantic result without another controller mutation; reusing the same ID with
different arguments is rejected. This journal is client evidence only and
does not survive process restart. A historical replay cannot replace the
visible state of a different deployment that is still active. Wire correlation
remains SessionId/BootId/RequestId/message type.

A missing transport or protocol confirmation becomes `OutcomeUnknown`; the
mutation is not replayed. The complete session reconnect path obtains a fresh
GetState/GetPackageState snapshot so an operator can compare exact staged and
active selectors. If Activate returns an explicit terminal failure and the
caller requested recovery, the adapter first queries PackageState, then sends
Rollback only when the exact current active selector and previously confirmed
fallback are both known. If stages 1 through 4 complete but the terminal
PackageState is erroneous or incoherent, the adapter refreshes state and does
not issue a speculative rollback.

This implementation follows the Windows authority commit
`6af2f4878f5d40c47a7cd2ffff5ace932efc0c2b`
(`docs: freeze Product API ECPKG deployment contract`). The audited
`product_api_v1.md` SHA-256 is
`dc3fa69c97b3e36ff3ce51aef7e0610e9e79f680953412a8b7af0337d411afbb`.
The current qualification is loopback/offline only: no ECPKG was sent to the
real controller.

## Real control lifecycle

The current typed lifecycle is:

1. Connect and finish the three-channel read-only refresh.
2. The Workbench consumer automatically requests the control lease for that
   authoritative session generation. Its Acquire action is a manual retry
   path. The default request is 30,000 ms, within the protocol range of 1
   through 30,000 ms.
3. Enter Configuration and confirm `SHUTDOWN` with no active controller
   package.
4. Scan the bus with DiscoverTopology. The default request starts at station
   address `0x1001` with capacity 64.
5. Keep the returned actual topology separate from the offline Project.
6. Restore the exact persistent active package by slot, generation, and
   configuration ID.
7. Confirm `OP_SAFE`, the restored active package, an operational OP bus,
   nonzero matching expected/actual working counters, and zero current and
   latched faults.
8. In `OP_SAFE`, the Workbench lower-left Run control sends automatic-mode
   `Start (0x0102)` and confirms `RUNNING` with the same active-package, bus,
   WKC, and fault conditions. The active package determines FreeRun versus
   Distributed Clocks. In `PAUSED`, Run sends Resume.
9. The lower-left Debug control sends Pause from `RUNNING` and Resume from
   `PAUSED`, confirming the resulting authoritative state.
10. Controlled Stop remains an explicit operator command. When requested, it
    sends ControlledStop from `RUNNING` or `PAUSED`, then confirms `OP_SAFE`
    while the package and operational bus remain valid.
11. A commissioning workflow that intends to stop may enter Configuration
    again and confirm `SHUTDOWN` with no active controller package.
12. Release the management lease, then Disconnect. Releasing from `RUNNING`
    or `PAUSED` does not stop or reload the autonomous cyclic task.

Acquire, Configuration, Restore, either explicit Start, legacy Start, Pause,
Resume, and Controlled Stop remain Pending while the adapter performs an
authoritative GetState/GetCapability/GetPackageState refresh, plus the
negotiated firmware and event-resume queries. They are reported Succeeded only
after their postconditions are confirmed. Discovery succeeds only after its
ordered stages and typed TopologyResult; Release succeeds on its terminal
response. A controller acceptance followed by a mismatching refreshed state
is reported as a failed control operation and degrades the connection
snapshot.

Discovery is allowed only while this session owns the lease, the controller is
ready and in `SHUTDOWN`, the package summary is present, and the controller
package is not Active. Restore does not guess a package: it reuses the exact
persistent selector observed before Configuration. The actual topology
contains linear scan-order records only; the protocol does not expose physical
port-to-port edges.

### Explicit FreeRun and Distributed Clocks semantics

Product API v1.10 defines `StartFreeRun (0x010c)` and `StartDc (0x010d)`.
Feature bit 11 is the capability gate for both commands. The active ECPKG is
also authoritative, but its actual mode is derived from validated ECFG/DC
content: zero configured DC slaves means FreeRun, while one or more valid DC
slaves means Distributed Clocks. A legacy manifest may omit timing metadata
and imply requested `auto`; manifest metadata and `DC_LOCKED` do not classify
the actual package mode. The Qt client never rewrites package content at
start time.

Both explicit commands share the normal startup gate: `OP_SAFE`, an active
package tied to the current BootId, an operational bus with the OP AL-state
bit, a nonzero matching WKC, and no current or latched faults. In addition,
the selected command must match the mode classified from the active package's
validated ECFG/DC content. `StartFreeRun` requests mode 1 and `StartDc`
requests mode 2.
`distributedClocksLocked` and the reported DC difference remain runtime
observations; the client must not infer the package mode from DC lock.

A mode mismatch is a terminal stage-2 `TIMING_MODE_MISMATCH (-35)` controller
result. Its 64-bit detail is
`(requested_mode << 32) | actual_mode`: the high 32 bits are the requested
mode and the low 32 bits are the active package's actual mode, with FreeRun
`1` and DC `2`. The adapter preserves and reports this detail instead of
reclassifying the command as a transport or protocol failure.

`Start (0x0102)` remains the automatic-mode request on v1.10 and older
runtimes. Workbench uses it through Qt Creator's native Run control and
exposes no independent FreeRun or DC button. `StartFreeRun` and `StartDc`
remain typed protocol capabilities for external clients that deliberately
request an explicit mode; they are not the Workbench runtime-control path.

This control extension can restore and run an already persistent package. The
headless Provider can also upload, validate, and optionally activate an
already-built immutable ECPKG through its semantic deployment API. Workbench
exposes that API on the selected Master's Deployment page with an explicit
OperationId, configuration ID, activation and rollback options, bounded
progress, and audit events. The Qt Project still cannot construct or sign the
package. Demonstrating FreeRun or DC mode therefore still requires an
appropriately built and signed package, generic Run, and separate
real-hardware observation.

## Semantic and error boundary

ControllerState, PackageState, and FirmwareState records are decoded only
after their complete public invariants pass. Capability has no published
payload schema, so the adapter hashes the opaque payload and maps only HELLO
feature bits. Numeric capacity, cycle, frame, and process-image limits remain
zero/unknown rather than being guessed from one controller.

Errors retain their source:

- invalid profile or scope is `ClientConfiguration`;
- socket, DNS, connection, and response timeout is `Network`;
- framing, CRC, version, size, flags, Sequence, identity, and correlation is
  `Protocol`; and
- only an explicitly decoded Product API signed status is `Controller`.

The v1.9 response-form allow-list baseline is synchronized with controller
`ISSUE-API-014` at Windows commit
`e23722da4265311f09a0d89125b76f0f6bfec93e`:

| Response form | Accepted signed status codes |
|---|---|
| CommandStatus | `0, -6, -7, -8, -9, -10, -11, -12, -14, -15, -16, -17, -19, -20, -21, -22, -23, -24` |
| CommandStatus v1.10 explicit-start extension | `-35` (`TIMING_MODE_MISMATCH`, terminal stage 2) |
| BulkStatus | `0, -4, -6, -7, -8, -9, -11, -12, -15, -16, -20, -21, -22, -23, -24` |
| PackageState | `0, -6, -7, -8, -12, -16, -17, -21, -22, -23, -24` |
| FirmwareState | `0, -6, -7, -8, -12, -14, -16, -28, -29, -30, -31, -34` |
| FirmwareStatus | `0, -6, -7, -8, -9, -11, -12, -14, -16, -27, -28, -29, -30, -31, -32, -33, -34` |

In particular, `UNSUPPORTED (-14)` is valid in CommandStatus, the general
envelope errors `BAD_MESSAGE (-6)`, `BAD_SESSION (-7)`, `STALE_BOOT (-8)`,
`BAD_SEQUENCE (-12)`, and `INTERNAL (-16)` remain typed PackageState errors,
and `BAD_SEQUENCE (-12)` remains a typed FirmwareState error. A signed status
not assigned to its actual response form is rejected as a protocol error
instead of being widened into a generic controller error.

Product API v1.10 additionally assigns `TIMING_MODE_MISMATCH (-35)` to
CommandStatus for the explicit start commands. It is terminal at stage 2 and
uses the structured requested/actual mode detail described above. The
v1.9-aligned status table remains the historical `ISSUE-API-014` baseline; it
must not be read as evidence that the v1.10 controller runtime has been
deployed or exercised.

Provider-facing text is translated and must contain no credentials, tokens,
certificates, private keys, or raw secret-bearing transport diagnostics.

## Lifecycle

Every manual Connect, full-session reconnect, Disconnect, and shutdown changes
the local session generation. Socket, timeout, and retry callbacks also carry
their channel epoch and cannot publish after either identity becomes stale.
Retries use a finite, capped delay sequence; session-capacity rejection never
turns `retry-after=0` into an immediate loop.

An auxiliary Push or Bulk failure currently invalidates the semantic
generation, closes Control, Push, and Bulk, and attempts a bounded resume of
the complete three-channel session. It does not reconnect one auxiliary socket
in isolation. A Control/session/Boot identity failure follows the same
full-session path. The recovery HELLO carries the existing SessionId; a
different or inconsistent resumed identity is rejected instead of being
published as the old session. The last alarm checkpoint is retained across
transport recovery only while the resumed SessionId and BootId remain the
same, so `ResumeEvents` continues after the last accepted sequence rather than
silently replaying from zero. A controller-declared replay failure, a detected
live sequence gap, a new identity, terminal protocol failure, explicit
Disconnect, or shutdown clears that checkpoint. Explicit Disconnect and
plugin shutdown stop every timeout and retry, clear pending requests, abort
all three sockets, and publish no later callback.

Disconnect is rejected while a control command or its confirming refresh is
active. Otherwise, when the session owns the lease, explicit Disconnect sends
ReleaseControl and waits for its successful terminal response before closing
the channels. ReleaseControl manages command authority only: it is allowed
without a ready ControllerState snapshot and does not send ControlledStop,
enter Configuration, clear the active package, or stop a `RUNNING`/`PAUSED`
cyclic task. Plugin shutdown makes the same bounded best-effort
ReleaseControl attempt whenever no control operation is active. A best-effort
write is not evidence that the controller accepted the release.

Normal network and control processing uses GUI-thread event-loop objects and
no blocking socket waits. `aboutToShutdown()` completes synchronously after a
management-lease best-effort ReleaseControl write bounded to 100 ms, explicit
teardown, and object-pool removal.

The adapter preserves controller lease arbitration. `LEASE_BUSY (-10)` is a
typed controller rejection for a write attempted while another session owns
the lease; it does not prevent the losing session from continuing supported
read-only queries.

The lease is deliberately not a runtime watchdog. If a configured task is
already `RUNNING`, clean ReleaseControl, transport loss, client-process exit,
or heartbeat expiry revokes that client's management authority but must not
change the task, package, bus operation, working counter, or cycle counter.
Actual EtherCAT, WKC, DC, CPU1, watchdog, and controller safety faults remain
independent and retain their existing controller-owned stop policy. A later
session may acquire the free lease, observe the same running instance, and
issue Pause, Resume, or ControlledStop. Controller deployment and real
hardware evidence for this contract remain gated on Windows
`ISSUE-API-021`.

## Hardware-gated acceptance modes

`testHardwareControlLifecycle` remains disabled unless
`QTC_ETHER_CAT_PRODUCT_API_HARDWARE=1` is explicitly set. Its required
`QTC_ETHER_CAT_TIMING_MODE` accepts:

- `auto`, `free_run`, or `dc` for the complete package-dependent lifecycle;
- `snapshot_only` to connect, record the authoritative three-channel
  session/controller/package snapshot, and disconnect without acquiring the
  control lease or issuing a controller command;
- `scan_only` for an identity-discovery acceptance that never restores or
  starts a package; or
- `free_run_rejection` for the exact DC-only-package rejection contract; or
- `fault_reset` for one already-diagnosed, cause-cleared `FAULT` snapshot.

`fault_reset` refuses before lease acquisition unless the negotiated session
is v1.11 with feature bit 12 and the authoritative snapshot is exactly
`FAULT`, `current_faults == 0`, with a nonzero full latched mask and alarm
checkpoint. It captures the complete pre-reset package/runtime state, acquires
the lease only when it can classify either an Active safe cyclic runtime or a
stopped management runtime with no Active package, sends only the confirmed
ResetFault, and requires stages 1 through 4, the strict AlarmCleared event,
zero terminal masks, and the unique expected `OP_SAFE` or `SHUTDOWN` outcome.
It then releases and disconnects. Failure
cleanup never scans, changes packages, enters Configuration, stops a runtime,
or attempts another reset; it only releases an owned management lease and
disconnects.

`free_run_rejection` restores the exact active package, issues
`StartFreeRun`, and requires a terminal stage-2
`TIMING_MODE_MISMATCH (-35)`. It also requires the controller to retain the
same fault-free `OP_SAFE` package state with no active application. The test
then returns to `SHUTDOWN/EMPTY`, releases the management lease, and
disconnects. An unexpected FreeRun start, a different rejection, or any
state/fault drift fails the acceptance.

`scan_only` fails its preflight before lease acquisition unless the
authoritative controller snapshot is ready `SHUTDOWN`, has no current lease
owner, has no current or latched fault, has no active application or bus, has
zero AL/WKC evidence, and has no Active runtime package. A successful run
performs only Connect, AcquireControl,
EnterConfigurationMode, DiscoverTopology, ReleaseControl, and Disconnect. It
requires a non-empty complete topology and confirms the same Session/Boot
epoch throughout. The normal failure cleanup may release an owned lease and
disconnect, but with this mode it never issues RestoreActivePackage, Start,
Pause, Resume, or ControlledStop.

This mode exists so a stale persistent selector does not become a prerequisite
for observing the currently wired modules. It remains a real hardware test,
not a production auto-scan path, and its output must be retained before any ESI
matching or package construction claim.

### 2026-07-26 scan-only evidence

Windows `ISSUE-API-022` replaced only the RAM-resident CPU0 Product API
service with the combined API-021/API-022 candidate. The new process reported
`startup_policy=observe-only`, `startup_cpu1_commands=0`, and
`boot_restore=5`. Two post-deployment snapshots matched the three
pre-deployment snapshots for BootId, persistent selector `B/12/813`, CPU1
package `REJECTED/-2/sequence 38`, service/bus/AL/WKC/cycle state, faults, and
zero lease ownership.

The first `scan_only` run then connected to all three v1.10 channels, acquired
a 30-second lease, and entered Configuration. DiscoverTopology reached stages
1 and 2 but was rejected by CPU1 at stage 3 with
`CPU1_REJECTED (-15)` and operation result `-3`. The test refreshed the
authoritative snapshot, released its lease, and disconnected. Final evidence
remained fault-free `SHUTDOWN`, AL/WKC `0/0`, zero lease ownership, and the
same package selector and CPU1 package state. It issued no Restore or runtime
command.

This run proves the client-side scan-only boundary and cleanup, but it does
not qualify discovery or provide current topology evidence. Controller
`ISSUE-API-023` owns the rejected CPU1 discovery path; no ESI matching,
project update, package construction, or DC claim may use this failed run.
The field bus was subsequently changed from the earlier virtual-slave setup
to real EtherCAT modules, so all historical slave identities and WKC values
are stale. Only a new successful physical-bus scan may seed the Project.

### 2026-07-27 real-slave scan and DC lifecycle evidence

Controller `ISSUE-API-025` made a successful EnterConfigurationMode require
both `SHUTDOWN` and a non-Active CPU1 runtime package. `ISSUE-API-026`
preserved that fail-closed check while making `SHUTDOWN + EMPTY`
EnterConfigurationMode idempotent across a new CPU1 BootId. The API-026
implementation commit is
`46ab1d4dc7f8c55a173ad0d3960bab7571662490`; the guarded deployment evidence
commit is `e78dba85d4e94123beec47bff670ee5d1e682a9f`. Its RAM-only candidates were:

- CPU0: 3,441,248 bytes, SHA-256
  `cc2d135e839473868f0579c604398244b93aec2675c083cff04863991084daf8`;
- CPU1: 134,496 bytes, SHA-256
  `9a4076716dd8c486a3004bafa13fe974b5af5e2aaa3e63139e4f6ef152c9513d`.

The guarded deployment changed the controller BootId from
`0x4f536c2944dc0639` to `0x4f536f408aafbc51` and the CPU0 PID from 819 to
1040. The Product API listener recovered in about 0.26 seconds. Two
independent post-deployment snapshots agreed on Product API 1.10/features
`0xfff`, `SHUTDOWN`, runtime `EMPTY/boot0`, persistent staged/active
`B/12/813`, lease owner 0, AL/WKC `0/0`, cycle 0, and no current or latched
fault. Startup remained `observe-only` with `startup_cpu1_commands=0`.

The production Qt provider then completed a headless `scan_only` acceptance
against the physical bus. All commands reached terminal success and the
discovered SII identities were:

| Position | Station | Vendor | Product | Revision | Serial |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x1001` | `0x00884443` | `0x000000b6` | `0x00000001` | `0x00000000` |
| 1 | `0x1002` | `0x00100000` | `0x000c0112` | `0x00010000` | `0x00000000` |
| 2 | `0x1003` | `0x00100000` | `0x000c0112` | `0x00010000` | `0x00000000` |

The same provider next completed the exact DC lifecycle for persistent
selector `B/12/813`:

1. AcquireControl and idempotent EnterConfigurationMode.
2. DiscoverTopology with the same three physical slaves.
3. RestoreActivePackage to `OP_SAFE`, AL `OP`, WKC `11/11`,
   `dcLocked=true`, and faults `0/0`.
4. StartDistributedClocks to `RUNNING`, AL `OP`, WKC `11/11`,
   `dcLocked=true`, and faults `0/0`.
5. Pause to `PAUSED`, then Resume to `RUNNING`, retaining AL `OP`,
   WKC `11/11`, DC lock, and zero faults.
6. ControlledStop to `OP_SAFE`, then EnterConfigurationMode to
   `SHUTDOWN/EMPTY`.
7. ReleaseControl and Disconnect with lease owner 0, AL/WKC `0/0`, and
   faults `0/0`.

Both hardware-gated test processes exited with status 0 and reported three
Qt test passes with no failure or skip. The retained raw logs are:

- `/private/tmp/embed-labs-productapi-real-scan.5mkn2z/lldb.log`,
  SHA-256
  `18191c2db83beba2ecc838b1f46fc5797877b9f7501da62d31b80e53e7fba930`;
- `/private/tmp/embed-labs-productapi-dc.EleKoG/lldb.log`, SHA-256
  `7ecdf43340c31402684cbcbf56fa42ad6b0c63111574a4e63f07699bbc1aac19`.

This evidence qualifies the current three-slave scan and the signed cfg813
DC lifecycle through the production provider. It does not qualify FreeRun:
cfg813 is the DC package and no FreeRun command was issued. The API-026
controller deployment is RAM-only and a controller reboot still requires a
fresh runtime-image qualification before repeating this acceptance.

### 2026-07-27 DC-only topology FreeRun and DC qualification

Controller `ISSUE-API-027`, commit
`012a41c5a3e160067888048cfe84bc98eba65091`, audited the complete XB6 plus
two SV630N topology without accessing hardware. Both the V13 and V16 ESI
declare only `DC-Synchron`, with `AssignActivate 0x0300`. The SV630N object
dictionary and vendor manual report synchronization type 2 and supported-type
mask `0x0004` for both `0x1c32` and `0x1c33`. These values advertise DC
Sync0 only: neither FreeRun bit 0 nor SM-synchronous bit 1 is set.

Changing the cycle from 125 us to 250 us, 500 us, or 1 ms cannot add an
unsupported synchronization mode. The old cfg812 is therefore a negative
artifact, not a deployable FreeRun candidate. It has no DC records, has an
expected process WKC of 9 rather than cfg813's 11, and previously failed the
station `0x1002` OP request. Current inspection and verification reject it
with `compile_report_timing_requirement_conflict` and
`required_dc_positions=[1,2]`. No FreeRun package was generated, signed,
uploaded, or activated.

The production Qt provider then completed the formal
`free_run_rejection` acceptance against the physical topology:

1. AcquireControl, enter Configuration, and discover the same three slaves.
2. Restore signed DC selector `B/12/813` to fault-free `OP_SAFE`, AL `OP`,
   WKC `11/11`, and a locked DC clock.
3. Issue the typed `StartFreeRun` command and receive the exact terminal
   stage-2 `TIMING_MODE_MISMATCH (-35)`.
4. Confirm that the application did not start and that `OP_SAFE`, WKC
   `11/11`, DC lock, the active package, and zero faults were preserved.
5. Return to `SHUTDOWN/EMPTY`, release the lease, and disconnect.

The same final binary then repeated the positive DC lifecycle:
`StartDc -> RUNNING -> PAUSED -> RUNNING -> ControlledStop`. Every runtime
gate retained AL `OP`, WKC `11/11`, DC lock, and zero current or latched
faults. Cleanup again reached `SHUTDOWN/EMPTY`, lease owner 0, AL/WKC `0/0`,
and disconnected.

Both final hardware-gated processes exited with status 0 and reported three
Qt test passes with no failure or skip. The retained logs are:

- `/private/tmp/embed-labs-productapi-freerun-rejection.0kdNSR/lldb.log`,
  SHA-256
  `13b81eba44d0246becf71ff605fc11c5dfb4be6202316bf0127f1a4d9390acc0`;
- `/private/tmp/embed-labs-productapi-dc-qualification.PFUip9/lldb.log`,
  SHA-256
  `97c724d8da9265d16991f3052732c0fd1c5aaaf093d2e43818625154195d45bd`.

This completes FreeRun capability and guard validation for the current
physical topology, but it is deliberately not a positive FreeRun `RUNNING`
claim: the two SV630N slaves are DC-only. A positive FreeRun runtime test
would require a physically different topology whose complete ESI capability
set permits FreeRun. The API-026 deployment remains RAM-only, so a controller
reboot invalidates this runtime qualification until the candidate images are
qualified again.

## Capability and evidence limits

The following remain intentionally unknown or outside the current control
extension:

- Capability payload schema and its numeric limits (`ISSUE-API-012`);
- the first `quint64` in PushHeartbeat and exact flags for some pushed records
  (`ISSUE-API-013`);
- authentication, authorization, or transport encryption;
- physical port-to-port topology edges;
- ECPKG construction, signing, and changing validated ECFG/DC content;
- ESI matching and applying actual topology into the offline Project;
- real Diagnostics Provider mapping; and
- SDO/PDO and firmware writes.

The focused Qt 6.11 ProductApi regression passed 77 rows, with one
real-controller lifecycle row skipped by its explicit environment gate and no
failures. EtherCATCore passed 20 rows. The two Workbench tests that consume
controller output passed individually, including the package deployment
progress, error-level, and duplicate-suppression assertions. The full
Workbench suite is not claimed for this issue because an unrelated navigation
test still fails before the deployment-output test. The `WITH_TESTS=OFF`
EtherCATCore, ProductApi, and Workbench product targets passed.

The generic Start and explicit DC real-controller lifecycles each passed 3
tests with 0 failures and completed safe cleanup in `SHUTDOWN`/EMPTY with no
lease owner or fault. Windows `ISSUE-RT-009` separated a successful LRW cycle
from the actual failure: `OP_REQUEST` for station `0x1002` returned WKC 0/1 on
all four attempts. `cfg812` was not activated and no FreeRun lifecycle ran.
The controller was safely rolled back to `B/12/813`, `OP_SAFE`, WKC 11/11,
faults 0, and lease 0. The Windows session continues with `ISSUE-API-016` and
the smallest isolated fix.

The earlier single-instance product observation showed Workbench, Simplified
Chinese, hidden production Mock UI, and the compact tree. The current endpoint
and unified-output revision is covered by 95 Workbench tests, 71 ProductApi
tests, and a three-pass ProjectExplorer passive-output lifecycle test. The
hardware-gated ProductApi test remained skipped and no controller command was
executed in this round.

The historical 2026-07-24 real-controller preflight negotiated Product API
v1.10 and feature bits `0xfff` independently on Control, Push, and Bulk with
one SessionId and BootId. That headless hardware run completed AcquireControl, entered
Configuration, and discovered three slaves at station addresses `0x1001`
through `0x1003`. The first run exposed a Qt decoder defect: TopologyResult
offset 20 contains the combined AL state, not a zero reserved field. The
decoder and regression fixture now preserve and validate that field, and the
real scan passes.

That same historical run was blocked at exact restore of persistent selector
`A/11/810`.
The controller returned terminal stage 3 `CAPABILITY_MISMATCH (-20)`, CPU1
result `-4`, and detail `11`; it remained fault-free in `SHUTDOWN`, released
the lease, and disconnected. Therefore this evidence does not yet qualify
Restore, Start DC, Start FreeRun, Pause, Resume, or Controlled Stop. The
v1.10 service is RAM-only and must not be described as persistent.

The following historical qualification belongs to the earlier read-only
adapter baseline:

- 36 focused ProductApi loopback events, including valid same-session resume
  and invalid alarm-checkpoint reset;
- 177 sequential events across Core, Project, Devices, Workbench, Scan,
  Diagnostics, and ProductApi;
- 1,301 messages across the seven EtherCAT translation contexts, including 52
  ProductApi messages, with no unfinished or empty translations;
- `lrelease` and a complete Qt 6.11.0 `WITH_TESTS=OFF` product build containing
  exactly 17 plugin dylibs; and
- enabled and `-noload EtherCATProductApi` offscreen lifecycle checks, each
  alive for 10/10 samples with the plugin mapped 10/10 and 0/10 respectively,
  followed by intentional SIGTERM status 15, no matching residual process,
  and no new Embed Labs DiagnosticReport.

Those results do not qualify the later control extension. The current control
tests, product build, translations, and generic Start/explicit DC
real-hardware evidence are reported above with their remaining FreeRun and
visible-UI limits.
