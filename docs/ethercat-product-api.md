# EtherCAT Product API Adapter

## Scope

`EtherCATProductApi` is the first concrete controller-adapter plugin for the
product-owned, multi-vendor `ControllerConnectionProvider` contract. It owns
only the Embed Labs Product API v1.9 transport. A future controller family
uses a separate plugin and Provider instead of adding vendor switches to this
plugin, EtherCATCore, or EtherCATWorkbench.

The plugin is headless. It registers one connection Provider in the Qt Creator
object pool and publishes immutable semantic snapshots. It does not create a
window, page, action, output pane, project field, scan result, or diagnostics
stream. The later Communication-page issue consumes the generic Provider
contract without including this plugin's private headers.

## Ownership and dependencies

`EtherCATProductApi` depends on `EtherCATData`, `EtherCATCore`, `Utils`, and
`Qt::Network`. It is the only current EtherCAT plugin that owns:

- ECAP frame encoding, incremental stream parsing, and CRC32C;
- Control, Push, and Bulk `QTcpSocket` instances;
- Product API message types, channel roles, payload limits, and field layouts;
- SessionId, BootId, RequestId, Sequence, timeout, and reconnect correlation;
- Product API status-name and retry mapping; and
- decoding into the public controller, capability, package, firmware, channel,
  session, heartbeat, and structured-error summaries.

No socket, byte frame, numeric protocol field, host, port, or controller ABI is
exported through EtherCATData or EtherCATCore. Workbench, Project, Devices,
Scan, and Diagnostics remain transport-free.

## Provider profile and endpoints

The Provider ID is
`EtherCAT.Connection.EmbedLabs.ProductApiV1`. Its default profile has a fixed
UUID so selection remains stable across restarts. The production defaults are:

| Channel | Endpoint | Maximum payload |
|---|---|---:|
| Control | `192.168.3.101:15200` | 4,096 bytes |
| Push | `192.168.3.101:15201` | 65,536 bytes |
| Bulk | `192.168.3.101:15202` | 65,536 bytes |

Only the base endpoint summary is published in the generic profile and
snapshot. The Push/Bulk endpoint details never become connection-request
fields. Construction does not connect: a consumer must explicitly call Connect
for a valid Project/Master scope and this Provider's profile.

## Strict read-only wire policy

The adapter has an explicit outbound allow-list:

| Channel | Allowed request |
|---|---|
| All | `HELLO (0x0001)` |
| Control | `GetState (0x0108)` |
| Bulk | `GetCapability (0x0400)` |
| Control | `GetPackageState (0x0403)` |
| Control | `GetFirmwareState (0x0504)`, only for negotiated v1.9 with the firmware feature |
| Push | `ResumeEvents (0x0210)`, only when exact replay is negotiated |

This list is closed at the private codec/session boundary. A consumer cannot
expand it through a profile, endpoint string, Workbench action, generic
Provider field, or arbitrary numeric message type. A request not listed above
is rejected before a frame is emitted.

The plugin never sends AcquireControl, Control Heartbeat, Start, Pause,
Resume, Stop, Reset, discovery, SDO, PDO, package, firmware-write, or other
mutating requests. Product API Control Heartbeat renews a control lease and is
therefore not a read-only keepalive. The adapter only consumes the inbound
replaceable PushHeartbeat and records the local receipt time.

Connect is not Scan. A connection does not acquire a lease, enter
configuration mode, inspect or change outputs, scan the bus, mutate the
offline project, or apply actual devices.

## Protocol validation

The private codec enforces the exact 64-byte, big-endian ECAP header, magic and
major version, negotiated minor, per-role payload bound, CRC32C, response
flags, and strictly increasing per-connection Sequence before a frame can
update the snapshot. The parser accepts arbitrary TCP fragmentation and
multiple coalesced frames without allocating the declared payload before the
fixed header passes validation.

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

The response-form allow-lists are synchronized with controller
`ISSUE-API-014` at Windows commit
`e23722da4265311f09a0d89125b76f0f6bfec93e`:

| Response form | Accepted signed status codes |
|---|---|
| CommandStatus | `0, -6, -7, -8, -9, -10, -11, -12, -14, -15, -16, -17, -19, -20, -21, -22, -23, -24` |
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
all three sockets, and publish no later callback. Because this read-only issue
never owns a control lease, cleanup sends neither ReleaseControl nor a lease
heartbeat.

The plugin uses only GUI-thread event-loop objects and performs no blocking
socket waits. `aboutToShutdown()` therefore completes synchronously after
explicit teardown and object-pool removal.

## Capability and evidence limits

The following remain intentionally unknown or outside this issue:

- Capability payload schema and its numeric limits (`ISSUE-API-012`);
- the first `quint64` in PushHeartbeat and exact flags for some pushed records
  (`ISSUE-API-013`);
- authentication, authorization, or transport encryption;
- physical port-to-port topology edges;
- Stateful Discovery, control leases, and configuration mode;
- Current Bus (Actual), ESI matching, Apply, and topology UI;
- real Diagnostics Provider mapping; and
- every controlled write.

Local codec and loopback validation is Mock protocol evidence only. No real
Qt-to-controller hardware run is claimed. The Windows Python-client
observations in `ethercat-online-controller.md` remain a separate Read-only
Real evidence class until a later explicitly authorized Qt hardware
qualification.

The final local issue qualification passed:

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

These results qualify the local implementation and its loopback contract.
They do not claim a real Qt connection to the controller.
