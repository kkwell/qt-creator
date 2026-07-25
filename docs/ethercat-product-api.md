# EtherCAT Product API Adapter

## Scope

`EtherCATProductApi` is the first concrete controller-adapter plugin for the
product-owned, multi-vendor `ControllerConnectionProvider` contract. It owns
only the Embed Labs Product API v1 transport. The current client contract is
v1.10, with bounded v1.9 compatibility for the persistent release24
controller runtime. The 2026-07-24 hardware record observed a RAM-only v1.10
CPU0 service and a reboot return to release24/v1.9; this documentation update
did not refresh that observation. A future controller family uses a separate plugin
and Provider instead of adding vendor switches to this plugin, EtherCATCore,
or EtherCATWorkbench.

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
| Control | lease `Heartbeat (0x0109)` |
| Control | `EnterConfigurationMode (0x010b)` |
| Control | `StartFreeRun (0x010c)`, only for negotiated v1.10 with feature bit 11 |
| Control | `StartDc (0x010d)`, only for negotiated v1.10 with feature bit 11 |
| Control | `DiscoverTopology (0x0401)` |
| Control | `RestoreActivePackage (0x0407)` |

Both lists are closed at the private codec/session boundary. A consumer cannot
expand them through a profile, endpoint string, Workbench action, generic
Provider field, or arbitrary numeric message type. A request not listed above
is rejected before a frame is emitted. Reset, SDO/PDO access, package
upload/activation, firmware write, and arbitrary bulk operations remain
excluded.

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
10. Controlled Stop in the same quick-control area sends ControlledStop from
    `RUNNING` or `PAUSED`, then confirms `OP_SAFE` while the package and
    operational bus remain valid.
11. Enter Configuration again and confirm `SHUTDOWN` with no active
    controller package.
12. Release the control lease, then Disconnect.

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

This control extension can restore and run an already persistent package. It
does not build, upload, stage, accept, activate, or modify an ECPKG from the
offline Qt Project. Demonstrating FreeRun or DC mode through Workbench
therefore requires an appropriately deployed package, generic Run, and
separate real-hardware observation.

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
active. A `RUNNING` or `PAUSED` controller must first use Controlled Stop.
When the session still owns the lease in ready `SHUTDOWN` or `OP_SAFE`,
explicit Disconnect sends ReleaseControl and waits for its successful terminal
response before closing the channels. Plugin shutdown makes a bounded
best-effort ReleaseControl attempt only in the same two confirmed safe states
and only when no control operation is active. It does not send ReleaseControl
from `RUNNING`, `PAUSED`, or an unknown state; a best-effort write is not
evidence that the controller accepted the release.

Normal network and control processing uses GUI-thread event-loop objects and
no blocking socket waits. `aboutToShutdown()` completes synchronously after a
safe-state-qualified best-effort ReleaseControl write bounded to 100 ms,
explicit teardown, and object-pool removal.

The adapter preserves controller lease arbitration. `LEASE_BUSY (-10)` is a
typed controller rejection for a write attempted while another session owns
the lease; it does not prevent the losing session from continuing supported
read-only queries.

## Capability and evidence limits

The following remain intentionally unknown or outside the current control
extension:

- Capability payload schema and its numeric limits (`ISSUE-API-012`);
- the first `quint64` in PushHeartbeat and exact flags for some pushed records
  (`ISSUE-API-013`);
- authentication, authorization, or transport encryption;
- physical port-to-port topology edges;
- ECPKG construction, upload, stage, accept, and activation;
- ECPKG construction or changing its validated ECFG/DC mode content from this
  client;
- ESI matching and applying actual topology into the offline Project;
- real Diagnostics Provider mapping; and
- SDO/PDO and firmware writes.

The current English regression passed 71 ProductApi tests and 226 tests across
Workbench, Project, Devices, Core, Scan, Diagnostics, and ProductApi, with one
ProductApi hardware test skipped and no failures. The `WITH_TESTS=OFF` product
build passed with exactly 17 plugin dylibs, and all EtherCAT Simplified Chinese
contexts contain no unfinished or empty translations.

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
