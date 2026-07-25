# EtherCAT Core In-Process API

## Scope and version

This document freezes the stage-1 in-process contract between the EtherCAT
plugins. It is API version 2 for the current local Qt Creator 20.0.1 product.
It is not a network protocol, controller ABI, project file format, or Zynq
contract.

The API is implemented by:

- `EtherCATData`, a UI-independent Qt Core library.
- `EtherCATCore`, a Qt Creator plugin that depends only on Core, Utils,
  EtherCATData, and Qt Widgets.

The original stage-1 contract intentionally omitted feature-specific scan
operations, diagnostic samples, and property widgets. They are added only by
their owning serial plugin issue, with a dedicated Core/API change when the
public contract must grow. The Project stage added the first typed extension:
immutable project snapshots and the project lifecycle service contract below.

The Devices stage adds the second typed extension: immutable ESI device
descriptions, repository filtering, original-XML access, and a cancellable
import/index job contract. It still contains no wire protocol or controller
ABI.

The Scan API revision adds typed scan requests, state/progress, immutable Mock
topology snapshots, comparison records, offline-slave values, and the
cancellable `ScanProvider` contract. It still defines no transport, controller
address, message, byte layout, or serialization format.

The Diagnostics API revision adds immutable online snapshots, bounded event
and trend records, explicit stream and EtherCAT states, alarm lifecycle, and a
checked `DiagnosticsProvider` contract. It remains an in-process capability and
defines no controller session, network command, packet, or private ABI.

The controller-connection API revision adds a product-neutral semantic
prerequisite for controller-adapter plugins. API v2 describes a stable
Project/Master scope, provider-owned connection profiles, arbitrary named
transport channels, optional negotiated session/version identity, read-only
state/capability/package/firmware summaries, heartbeat freshness, and
structured errors. It does not require three channels or a session protocol,
and it contains no socket, ECAP frame, message number, CRC, byte layout,
network thread, state-changing command, or bus scan.

The offline-configuration revision adds typed Process Data, Startup, and DC
values plus UI-independent validation and process-image preview algorithms.
The Project format-version-2 revision embeds those values in each offline slave
snapshot and adds checked replacement commands to `ProjectService`.

The structural-node-name revision adds one checked Project-owned command for
renaming an existing Target or Master by stable ID. It is the API prerequisite
for later TwinCAT-inspired General pages; it adds no page, widget, controller
transport, or generic node-mutation surface. The virtual method is appended
after the existing Project service methods so their established vtable slots
do not move.

## Stable identity

`EtherCAT::Data::NodeId` is the only stage-1 cross-plugin node identity.

- A new ID is generated with `NodeId::create()`.
- Persistence uses the lowercase UUID text returned by `toString()` without
  braces.
- Invalid text parses to a null ID.
- Null means no selection or no identity.
- Display names, tree rows, `QModelIndex`, and widget pointers are never IDs.

## Offline configuration domain contract

`ethercatdata/offlineconfiguration.h` defines the configuration contract used
by the later Project and Workbench stages. It contains no QObject, widget,
project document, XML parser, network type, or controller ABI.

The Process Data model represents Sync Managers, selectable RxPDO/TxPDO
assignments, ordered entries, fixed/mandatory/default/predefined metadata,
explicit or automatic bit offsets, and mapping-support markers. Validation
produces structured issue codes and one deterministic input/output process
image. Duplicate assignment, missing or wrong-direction SM, invalid width,
unsupported mapping, duplicate object, overlap, and capacity errors are
calculated once in the domain library rather than in a table widget.

Startup values retain enabled state, explicit order, transition,
index/subindex, typed and raw data type, raw value, and comment. DC values use
nanoseconds in every cycle and shift field and retain mode, AssignActivate,
SYNC0, SYNC1, and potential reference-clock state. Their validators return the
same structured issue type. The full field and validation contract is recorded
in `docs/ethercat-offline-configuration.md`.

## Public services

The plugin registers these owned objects in the Qt Creator object pool during
`initialize()`:

| Service | Contract |
|---|---|
| `SelectionService` | Stores one current `NodeId` and emits old and new IDs only when the value changes. |
| `StateService` | Stores one status contribution per stable source ID and exposes the highest severity. |
| `ProviderRegistry` | Tracks typed EtherCAT Providers dynamically as Qt Creator adds and removes them from the object pool; removal unlinks the Provider before the registry emits its removal signal. |

Consumers retrieve services with
`ExtensionSystem::PluginManager::getObject<T>()`. They must not construct a
second global instance or add a separate service locator.

`StatusSeverity` ordering is `Ready`, `Busy`, `Warning`, then `Error`.
`StateService` rejects a contribution with an invalid source ID. A source owns
its entry and must clear it when its operation ends or its plugin shuts down.

## Provider extension points

All optional EtherCAT capabilities derive from `Provider` and have a globally
unique `Utils::Id`, user-visible name, type, and availability flag.

| Provider type | Owning plugin |
|---|---|
| `ProjectService` | EtherCATProject |
| `DeviceRepositoryProvider` | EtherCATDevices |
| `PropertyPageProvider` | EtherCATWorkbench and optional page contributors |
| `ControllerConnectionProvider` | Independent vendor/protocol adapter plugins |
| `ScanProvider` | EtherCATScan or a future real-controller provider |
| `DiagnosticsProvider` | EtherCATDiagnostics or a future real-controller provider |

Stage 1 froze discovery and lifecycle only. The Project API revision adds typed
project methods before the Project implementation. It deliberately does not
expose generic `QVariant`, byte arrays, network messages, or placeholder methods
for later feature data.

## Controller connection provider contract

`ControllerConnectionProvider` is the GUI-thread, in-process boundary between
Qt Creator pages and one concrete vendor/protocol adapter registered by an
independent plugin. Multiple providers of this kind may coexist. Each provider
exposes profiles for a Project/Master scope, one current immutable
`ControllerConnectionSnapshot`, and three asynchronous operation requests:

- `connectToController(request)`;
- `refreshController()`; and
- `disconnectFromController()`.

Profiles are queried with `connectionProfiles(scope)`. Each operation returns
a `Utils::Result` that says whether the request was accepted, not whether the
asynchronous transport operation later succeeded. Consumers observe
`connectionProfilesChanged()` or `connectionSnapshotChanged()` and re-query
the complete immutable values. A concrete provider must publish every signal
on its GUI thread; background socket or codec work remains private.

An adapter may additionally expose an editable presentation value through
`connectionProfileConfiguration(scope, profileId)` and accept it through
`setConnectionProfileEndpoint(scope, profileId, endpoint)`. The common value
contains only the selected profile ID, an endpoint presentation string, a
placeholder, and an editable flag. Its syntax and persistence remain
provider-owned; the default implementation returns no configuration and
rejects edits. Consumers must not infer TCP, host, port, route, credentials, or
any other vendor-specific transport structure from this optional string.

A `ControllerConnectionScope` contains stable Project and Master IDs. A
provider-owned `ControllerConnectionProfile` adds a stable profile ID, display
name, redacted endpoint summary, configured/supported/default flags, and an
optional configuration issue. The real host, ports, route, credentials,
certificates, serial settings, or other protocol-specific configuration remain
private to that adapter. A connect request therefore carries only the scope
and one profile ID. Null scope/profile IDs, an ID not returned by that Provider
for the scope, or an unconfigured/unsupported profile must be rejected without
starting an operation.

Provider IDs are globally unique; profile IDs are stable only within their
Provider. Invoking a Provider defines the profile-ID namespace, so two
Providers may safely use the same profile ID. The future Workbench page selects
the `{providerId, profileId}` pair explicitly. When multiple connection
providers are installed it must not silently choose the first one, and removal
of a selected provider must produce an unavailable state rather than switching
to another vendor.

The common contract has no vendor enum, protocol selector, fixed endpoint
shape, or Control/Push/Bulk channel enum. It also does not define an outbound
command set. Each adapter owns its private transport, request allow-list,
status decoder, reconnect/resume policy, and any later write authorization.
A new manufacturer or incompatible protocol therefore adds another plugin and
Provider instead of branching in EtherCATCore, Workbench, or an existing
adapter.

Profile and channel IDs are nonempty and stable within their Provider. At most
one profile for a scope is marked default. Display names and endpoint summaries
are presentation only. Every outward-facing profile, channel, and error string,
including configuration issues, endpoint summaries, channel details, code
names, summaries, and diagnostic details, must omit credentials, tokens,
certificate material, private keys, and raw transport errors that may contain
secrets.

Connection states are:

- `Disconnected`: no live channel or operation;
- `Connecting`: the transport is opening channels;
- `Handshaking`: channel role, protocol, SessionId, and BootId are being
  verified;
- `Connected`: all adapter-required channels belong to the same negotiated
  session when the protocol has one, and the initial read-only summaries are
  available for the capabilities supported by that adapter;
- `Degraded`: the adapter's primary query path remains usable while an
  optional event, bulk, or auxiliary channel is unavailable;
- `Disconnecting`: asynchronous shutdown is in progress; and
- `Failed`: Control, protocol compatibility, or session/BootId validation
  failed.

`Provider::isAvailable()` continues to mean that the provider capability is
installed and usable. It is not a connected-state flag. Connection lifecycle
always comes from the snapshot. A Provider may expose profiles while
unavailable so the UI can explain or repair configuration, but it must reject
new connect and refresh operations. Disconnect remains valid while unavailable
so shutdown and partial-initialization cleanup cannot be blocked.

The snapshot contains:

- stable Project/Master scope, selected profile ID, and redacted endpoint
  summary;
- any number of provider-owned named channel states, their negotiated maximum
  payload when meaningful, last activity, and detail;
- negotiated protocol major/minor, real SessionId/BootId, observed lease owner,
  default lease duration, and whether this session owns a lease;
- a local monotonically increasing `sessionGeneration` used to reject callbacks
  from an older connection attempt;
- connected, updated, and last-heartbeat timestamps;
- explicit `readOnly` and `mock` evidence markers;
- semantic ControllerState and Capability values;
- optional active/staged package and firmware lifecycle summaries; and
- one optional structured error.

The `readOnly` marker is a Provider assertion about the currently exposed
connection workflow, not a generic Core command permission. Core neither
derives it from a wire protocol nor offers an arbitrary-message escape hatch;
the concrete adapter remains responsible for enforcing its outbound
allow-list.

After disconnect, a Provider may retain the last scope, profile, redacted
endpoint summary, channel names, and decoded summaries as historical context,
but it must clear the live session/lease identity and heartbeat timestamp.
Only `Connected` or `Degraded` represents a live query path; retained values
must never be presented as current merely because they remain in the snapshot.

The capability value keeps the descriptor SHA-256 and named feature support
instead of exposing a protocol feature-bit mask. State flags, slots, lifecycle,
faults, WKC, timing, and capability limits are decoded semantic fields, not
offsets into a wire record.

An error keeps its source, optional provider-owned channel ID, operation,
optional numeric code,
optional operation result, numeric source detail, code name, RequestId, retry
delay, timestamp, retry disposition, summary, and detail. The source taxonomy
distinguishes client configuration, local network, adapter protocol,
controller-returned status, EtherCAT bus, ESI/device description, and unknown
origin. For `Controller` source, `code` and `operationResult` retain numeric
values returned by that controller protocol when applicable. A local
`Protocol` error does not populate those controller fields. Only explicit
controller-returned evidence may use the `Controller` source; local parsing or
connectivity failures must not be mislabeled as master defects.

The original connection-only Core revision authorized no control lease,
configuration mode, discovery, runtime state transition, package mutation,
SDO/PDO write, or firmware change. The later typed extension adds only
provider-neutral command support/dispatch, progress, and linear Actual Bus
values. The default Provider rejects control; a vendor adapter opts into each
command. Numeric Product API messages, fixed endpoints, channel layout, frame
encoding, and status tables remain private to the independent, headless
`EtherCATProductApi` plugin documented in `docs/ethercat-product-api.md` and
`docs/ethercat-online-controller.md`.

Workbench, not Core, performs one automatic Acquire attempt after an
authoritative connected snapshot identifies the exact
Provider/profile/scope/session generation. The controller remains the lease
authority: one session owns control, other API sessions may continue read-only
queries, and their writes are rejected with `LEASE_BUSY (-10)`. Core exposes
the semantic ownership/error evidence but neither steals a lease nor retries a
vendor command through an arbitrary-message path.

The ProductApi adapter's current auxiliary-channel recovery closes Control,
Push, and Bulk together and attempts a bounded resume of the complete session;
that is an adapter decision, not a `ControllerConnectionProvider` requirement.
Local codec and loopback validation remains non-hardware protocol evidence.
Dated Qt hardware observations are recorded separately in
`docs/ethercat-online-controller.md`; they do not turn this generic Core
contract into hardware evidence. The current English regression passed 19
Core tests and 226 tests across Workbench, Project, Devices, Core, Scan,
Diagnostics, and ProductApi, with one ProductApi hardware test skipped and no
failures.
The `WITH_TESTS=OFF` product build passed with exactly 17 plugin dylibs, and
all EtherCAT Simplified Chinese contexts contain no unfinished or empty
translations.

The generic Start and explicit DC real-controller lifecycles each passed 3
tests with 0 failures and completed safe cleanup in `SHUTDOWN`/EMPTY with no
lease owner or fault.
Those are adapter and Workbench observations, not Core hardware qualification.
Windows `ISSUE-RT-009` separated a successful LRW cycle from the actual
failure: `OP_REQUEST` for station `0x1002` returned WKC 0/1 on all four
attempts. `cfg812` was not activated and no FreeRun lifecycle ran. The
controller was safely rolled back to `B/12/813`, `OP_SAFE`, WKC 11/11, faults
0, and lease 0. The Windows session continues with `ISSUE-API-016` and the
smallest isolated fix.

The earlier single-instance product observation showed Workbench, Simplified
Chinese, hidden production Mock UI, and the compact tree. The current endpoint
and unified-output revision is covered by 95 Workbench tests plus a three-pass
ProjectExplorer passive-output lifecycle test. No controller connection or
hardware command was executed in this round. None of these adapter/UI
observations changes the generic Core qualification boundary.

## Scan provider contract

`ScanProvider` is a GUI-thread capability with one operation at a time. A
consumer starts a typed request for interface discovery, slave discovery, or a
selected branch; observes the exact Idle/Preparing/ScanningMaster/
ScanningSlaves/BuildingSnapshot/Comparing/terminal state; reads bounded
progress and discovered count; and may cancel or clear the last result.
Completed results contain immutable topology and comparison values. Cancelled
and failed operations are terminal until cleared, and must not modify an
offline project. A snapshot records the originating operation and stable branch
ID, which keeps compare and accept scopes identical. Interface discovery has no
slave topology and cannot be accepted as one. The Provider contract does not
itself accept a result into a project; that remains a checked ProjectService
command in the owning stage.

## Diagnostics provider contract

`DiagnosticsProvider` is the public, GUI-thread interface between diagnostic
pages and either the phase-1 Mock source or a future controller Provider. It
supports one active `{projectId, masterId}` request and exposes:

- `Stopped`, `Starting`, `Running`, `Stopping`, and `Failed` stream states;
- the latest immutable `DiagnosticsSnapshot`, or no value before publication;
- bounded event/alarm history and bounded performance trend history;
- effective event/trend capacities and source/publish periods;
- a checked run-mode request, alarm acknowledgement, and recovered-event clear;
- an explicit last error and idempotent stop operation.

The normal transition is `Stopped -> Starting -> Running -> Stopping ->
Stopped`. A source failure enters `Failed` and emits the single completion
notification for that monitoring session. `stopMonitoring()` then performs
cleanup and returns the Provider to `Stopped`. A second start while not stopped,
an invalid stable ID, a state request while not running, or acknowledgement of
a non-active alarm must return an error instead of silently succeeding.

The snapshot contains the current Mock/real marker, Config/FreeRun/Run mode,
master and slave INIT/PREOP/SAFEOP/OP state, AL Status, expected/actual WKC,
WcState, master/slave ports, link state and interruptions, frame/error counters,
DC synchronization, cycle timing, jitter, deadline margin, and aggregate alarm
counts. Error state is a separate flag from the EtherCAT state so an OP slave
with an AL error cannot be represented as healthy by accident.

`DiagnosticEvent` covers state, WKC, link, frame, DC, cycle, and alarm entries.
Alarm lifecycle is Active, Acknowledged, then Recovered; timestamps and repeat
count remain available after recovery until explicitly cleared. Events and
trend samples are ordered oldest to newest. When a configured capacity is
reached, a Provider discards the oldest record and increments the matching
dropped counter in its next snapshot.

All cycle, jitter, deadline, DC offset, and deviation values use nanoseconds.
Only source and publish periods use milliseconds. Counters are monotonically
nondecreasing within one monitoring session unless a future Provider documents
an explicit source reset.

Snapshot, event, and trend signals are invalidation notifications without large
payloads; consumers re-query the immutable value they need. This permits a
source to sample faster than it publishes UI snapshots. A background producer
may not mutate Provider values or widgets directly: it must aggregate privately
and publish on the GUI thread. The Diagnostics plugin owns the concrete rate
limiting, bounded buffers, Mock scenarios, and property pages.

## Project service contract

`ProjectService` is an abstract, GUI-thread service implemented by the
EtherCATProject plugin. It exposes immutable `ProjectSnapshot` values for all
open EtherCAT projects and one active project ID. A snapshot contains project
metadata, stable Project/Target/Master/Slave node identities, and checked
offline-slave Identity, position, Alias, Serial Number, and optional ESI
description links. Each offline slave also owns immutable Process Data,
Startup, and DC configuration values. It does not contain ESI XML, scan
execution state, online state, or diagnostic data.

The service owns the cross-plugin commands for project activation, project
rename, Target/Master structural-node rename, offline-slave replacement,
Process Data replacement, Startup replacement, DC replacement, save, undo,
and redo. Structural-node rename accepts only an existing Target or Master
stable ID; Project continues to use `renameProject`, and Slave names continue
to belong to offline-slave replacement. It trims accepted names, rejects an
empty name or any other node kind, and treats an unchanged normalized name as
a no-op. Topology replacement is scoped to a known master. Configuration
replacement is scoped to a known slave. All mutation paths validate stable IDs
and applicable domain rules before entering the same project Undo/Redo stack.
Commands return `Utils::Result` so a consumer cannot mistake a rejected command
for success.
`projectAdded`,
`projectAboutToBeRemoved`, `projectChanged`, and `activeProjectChanged` are the
only cross-plugin lifecycle notifications. Consumers must re-query a snapshot
after a notification and must not retain ProjectExplorer or document pointers.

ProjectExplorer remains the owner of open/close and startup-project lifecycle.
The service mirrors that state; it does not create a second project registry.
The project file format, atomic save, migration, and undo stack belong to the
Project plugin and are not part of this Core API.

## Device repository contract

`DeviceRepositoryProvider` is an abstract, GUI-thread Provider implemented by
the EtherCATDevices plugin. It exposes immutable summaries and full device
descriptions containing:

- Vendor ID, Product Code, Revision Number, name, type, and group;
- SyncManager definitions and directions;
- RxPDO/TxPDO definitions, entries, bit lengths, and common data types;
- CoE capability flags and startup parameters;
- DC operation modes and timing defaults;
- source path, SHA-256, import time, warnings, and unsupported-feature markers.

Callers search with `DeviceFilter`, resolve a full description by stable
`NodeId`, and may request the exact original XML for forward compatibility.
They do not receive repository model indexes or parser objects.

`importFiles()` and `rebuildIndex()` return a provider-owned
`DeviceImportJob`. Jobs publish Pending, Running, Canceling, and Finished
states, bounded progress, and one immutable `DeviceImportResult`. `cancel()`
is idempotent. `finish()` is terminal and can emit only once. The Provider
emits repository reset/change signals only after worker results have returned
to the GUI thread.

Job pointers are guarded QObject references. Consumers must stop using a job
after its `destroyed` signal and must not retain a Provider after object-pool
removal. XML parsing, repository storage, deduplication, and stable-ID mapping
belong to EtherCATDevices, not Core.

Providers register themselves with `PluginManager::addObject()` only after
they are initialized and remove themselves before destruction. Consumers
listen to `ProviderRegistry::providerAdded` and
`providerAboutToBeRemoved`. On the PluginManager object-pool removal
notification, `ProviderRegistry` first unlinks the Provider from its guarded
list and then emits `providerAboutToBeRemoved`. Registry queries and nested
removals from that signal therefore cannot rediscover the departing Provider,
although the object remains in the PluginManager object pool until the
notification returns. Consumers may use the signal argument only during the
direct callback and do not retain it afterward.

## Workbench property-page contract

`PropertyPageProvider` is the public, optional extension point used by the
Workbench details container. It receives an immutable `PropertyPageContext`
with a stable project ID, node ID, `WorkbenchNodeKind`, and display name. It
returns ordered `PropertyPageDescriptor` values and creates a `QWidget` only
when the host asks for that page.

The Workbench owns created page widgets. The Provider refreshes a page only
through `updatePage()` and must not retain the context, a tree index, or a
pointer to Workbench-private UI. For an ordinary unregister, the host detaches
and destroys every hosted widget from that Provider before
`PluginManager::removeObject()` returns. If a Provider unregisters itself from
inside one of its own `pages()`, `createPage()`, or `updatePage()` callbacks,
no callback-associated widget remains in the host's live-page map after removal
handling. An already-tabbed active widget may remain attached and alive only
until the active callback returns, when the host destroys it. The Provider and
its plugin must therefore remain loaded through callback return; in particular,
they must not unload code used by the page destructor or Qt meta-object before
that boundary. Page IDs must be stable and unique within one Provider; the
effective host key is Provider ID plus page ID.

`WorkbenchNodeKind` is limited to UI-neutral selections used by the Workbench.
Its numeric values are a public compatibility contract for local plugins:
existing values must never be reordered or reused, and later values may only
be appended. Consumers must tolerate unknown future values and must not persist
an enum value as domain data.

| Value | Kind | Selection meaning |
|---:|---|---|
| 0 | `None` | No valid Workbench selection |
| 1 | `Project` | Open EtherCAT project |
| 2 | `Target` | Offline target/controller shell |
| 3 | `Master` | EtherCAT master configuration |
| 4 | `DeviceRepository` | Imported ESI repository root |
| 5 | `Device` | Read-only imported ESI device revision |
| 6 | `ConfiguredSlave` | Offline slave instance in a project |
| 7 | `Diagnostics` | Diagnostics capability branch |
| 8 | `Placeholder` | Explanatory row without a domain selection |
| 9 | `ProcessInputs` | Slave-to-controller process-image branch |
| 10 | `ProcessOutputs` | Controller-to-slave process-image branch |
| 11 | `RxPdoGroup` | PDOs received by a slave |
| 12 | `TxPdoGroup` | PDOs transmitted by a slave |
| 13 | `Pdo` | One concrete PDO mapping |
| 14 | `PdoEntry` | One mapped process-data entry |
| 15 | `Modules` | Modular-device branch |
| 16 | `Module` | One ESI-backed module instance |
| 17 | `Channel` | One ESI-backed channel |

The derived kinds reserve stable routing identities for the TwinCAT-inspired
Inputs, Outputs, RxPDO, TxPDO, and Modules/Channels hierarchy. They do not add
tree rows by themselves and do not authorize fabricated module or channel
data: those nodes must be backed by project/ESI values. The enum carries no
scan result, online state, wire protocol, or controller ABI. Later plugins
contribute pages by subclassing the public interface; they do not modify the
Workbench tree or include Workbench private headers.

The stage-4 Workbench implementation listens to property-provider addition,
availability changes, and removal. It identifies a hosted page by Provider ID
plus page ID and applies the ordinary and active-callback destruction
boundaries above. It also observes the availability of Scan and Diagnostics
capability providers: this controls only local placeholder visibility and tree
status, and does not add a scan algorithm, diagnostic payload, transport, or
serialization contract.

Provider IDs are globally unique across all Provider kinds. If two live
objects use the same ID, only the first one is published by the registry. Such
a collision is a plugin defect, not a selection mechanism.

## Thread and ownership rules

- Core services and Providers have GUI-thread affinity.
- Mutating `SelectionService` or `StateService` from another thread is an API
  violation guarded by `QTC_ASSERT`.
- Background work must deliver immutable data back to the GUI thread before
  changing these services.
- A concrete controller plugin may own private sockets, workers, codecs, and
  timers, but it may publish only immutable semantic connection snapshots on
  the Provider's GUI thread. Disconnect, plugin removal, and shutdown must
  invalidate older connection generations before those resources are released.
- `ProviderRegistry` stores guarded pointers. On the object-pool removal
  notification it unlinks the Provider before emitting
  `providerAboutToBeRemoved`, so registry queries and nested removal are
  re-entry safe while PluginManager still owns the notification lifetime.
- `aboutToShutdown()` removes the registry, state service, and selection
  service in reverse registration order, after clearing owned state.
- Cleanup is idempotent, so normal shutdown followed by plugin destruction
  cannot unregister an object twice.

## Settings and public IDs

The plugin owns the `Z.EtherCAT` settings category and the
`EtherCAT.General` page. Stage-1 settings are:

- show advanced EtherCAT properties, default off;
- maximum recent diagnostic events, default 1,000 and bounded from 100 to
  100,000.

`EtherCAT.Context`, `EtherCAT.Menu`, `Z.EtherCAT`, and `EtherCAT.General` are
reserved public IDs. Workbench owns the EtherCAT mode and visible menu actions;
Core does not create an empty product mode or an empty menu.

## Core API verification

The focused plugin test covers:

- generated metadata and the three object-pool services;
- `NodeId` generation, parsing, equality, and hashing;
- selection change suppression and clearing;
- state contribution validation and severity aggregation;
- dynamic Provider addition, availability, unlink-before-signal removal, and
  nested removal re-entry;
- typed controller scope/profile/request/snapshot value semantics, unknown,
  incomplete, or unsupported profile and stable-scope rejection, connection
  availability transitions,
  arbitrary named channels, session evidence, structured errors, refresh,
  idempotent disconnect, and multiple-provider registry filtering;
- typed scan request, state, progress, cancellation, and reset behavior;
- typed diagnostics snapshots, stream transitions, mode request, bounded-data
  metadata, alarm acknowledgement/recovery, and stop/failure behavior;
- Process Data selection, automatic/explicit offset preview, duplicate,
  overlap, width, direction, support, and capacity validation;
- Startup order/raw-value validation and nanosecond DC cycle/shift validation;
- frozen Workbench node-kind values and derived PDO/module selection contexts;
- settings-page registration.

For `ISSUE-WB-DETAILS-FOCUS-CONTINUITY-001`, based on
`0637955df9ab009bb0ee1fcb892dc74e5ede27e7`, the focused EtherCATCore suite
passes all 17 tests as part of the 92-test six-plugin run under
`/tmp/embed-labs-six-suites-final2.XeKLz8`. Its new coverage proves that the
Provider is absent from registry queries inside its removal signal and that a
nested removal does not invalidate the outer operation. The change remains in
the product-owned EtherCATCore registry/test files; it adds no upstream Qt
Creator Core patch, public Provider method, source-list entry, or CMake/qbs
change. qbs was therefore not run.

The focused build target and test execution are limited to Core and
EtherCATCore. Enabling tests for the complete existing product currently
exposes an unrelated EasyBoard baseline error: `easyboardbrowser.cpp` includes
the unavailable `extensionmanager_test.h` when `WITH_TESTS` is on. The normal
product build with tests off succeeds and is the integration-build evidence
for stage 1.

For `ISSUE-CORE-CONTROLLER-CONNECTION-API-001`, based on local commit
`06538be209e26ff0b8a4b8ad4f231a7d15c0631b`, failure-first compilation stopped
at the intentionally missing `ethercatdata/controllerconnection.h`. After the
minimal Data/Core implementation, the Qt 6.11.0 Release EtherCATCore suite
passed all 18 events under offscreen, crash-reporter-disabled LLDB supervision.
The fake Provider proves only the in-process contract. No Qt socket, Product API
frame, controller connection, discovery, or hardware result is claimed by this
test.

The final sequential isolated regression passed 141 events: Core 18, Project
12, Devices 8, Workbench 85, Scan 11, and Diagnostics 7. The complete
`WITH_TESTS=OFF` product build passed with 16 plugin dylibs. Enabled and
`-noload EtherCATCore` offscreen lifecycle runs each remained alive for 10/10
samples; Core was mapped for 10/10 enabled samples and 0/10 disabled samples,
and both stopped through intentional target status 15. No matching residual
qualification process or new Embed Labs DiagnosticReport remained.

For `ISSUE-CORE-CONTROLLER-PROVIDER-PROFILE-002`, based on local commit
`a6bcd2dfe813e16ccc62fd61afa6b41473b6542e`, failure-first compilation stopped
at the intentionally missing scope/profile types and provider methods. The
final Qt 6.11.0 Core suite passed all 18 events. Two fake providers prove
provider-scoped profile selection and safe profile-ID reuse,
unknown/incomplete/unsupported profile rejection, availability gates,
arbitrary named channels, a non-TCP endpoint summary, coexistence, and removal
isolation.

The sequential six-plugin regression passed the same 141 events, and the
complete `WITH_TESTS=OFF` product build passed with 16 plugin dylibs. Enabled
and `-noload EtherCATCore` offscreen lifecycle runs each remained alive for
10/10 samples; Core was mapped for 10/10 enabled samples and 0/10 disabled
samples. Both stopped through intentional target status 15, while the existing
visible product process remained untouched. No matching residual
qualification process or new Embed Labs DiagnosticReport remained. Evidence
is under
`/private/tmp/embed-labs-i18n-compact/controller-profile-v2`.
