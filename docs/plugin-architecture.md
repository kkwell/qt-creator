# EtherCAT Plugin Architecture

## Scope

The first phase builds a TwinCAT-inspired EtherCAT engineering experience on
the current local Qt Creator product. It does not implement a Zynq protocol,
real EtherCAT communication, ECPKG deployment, or a PLC language environment.

All user-visible EtherCAT functionality belongs to product-owned Qt Creator
plugins. The upstream Core, ProjectExplorer, and application bootstrap must
not contain EtherCAT-specific branches.

## Serial delivery rule

Only one EtherCAT feature plugin may be in progress at a time. The next plugin
cannot start until the current plugin passes its build, lifecycle, behavior,
documentation, review, and local-commit gates.

| Order | Component | Current state | Exclusive responsibility |
|---:|---|---|---|
| 0 | Stage-0 governance | Complete | Baseline, policy, build, and architecture evidence |
| 1 | `EtherCATData` | Configuration API complete | Stable values, offline validation, and process-image preview |
| 1 | `EtherCATCorePlugin` | Complete | IDs, public services, selection, extension points, settings |
| 2 | `EtherCATProjectPlugin` | Configuration persistence and structural-name API complete | Version-2 project lifecycle, migration, validation, and Undo/Redo |
| 3 | `EtherCATDevicesPlugin` | Complete | ESI repository and offline device/PDO/DC models |
| 4 | `EtherCATWorkbenchPlugin` | In progress | Project, Target, Master, configured-slave, ESI Repository, individual ESI catalogue-device General, master-side ESI insertion, supported-device drag-and-drop, and explicit active-project selection workflows, master/slave EtherCAT views, Alias editing, editable pages, manual offline topology, process-data tree, command/status surfaces, and public Scan/Diagnostics state overlays are complete; remaining UI qualification is open |
| 5 | `EtherCATScanPlugin` | Complete | Mock scan state machine, snapshots, and configuration diff |
| 6 | `EtherCATDiagnosticsPlugin` | Complete | Mock WKC/DC/link/event diagnostics and trends |

`EtherCATData` is an infrastructure library, not a feature container. Its
offline configuration contract is persisted by EtherCATProject format version
2 and exposed through checked Project service commands. The Phase-1 product is
not Ready until editable pages, complete tree workflows, integration tests,
and the policy-deferred upstream rehearsal pass.

## Dependency direction

```text
Qt Creator Core / ExtensionSystem / Utils / ProjectExplorer
                              ^
                         EtherCATData
                              ^
                    EtherCATCorePlugin
                         ^       ^
              ProjectPlugin  DevicesPlugin
                         ^       ^
                         WorkbenchPlugin
                          ^          ^
                   ScanPlugin  DiagnosticsPlugin
```

Dependencies are one-way and explicit. Core cannot depend on Project,
Devices, Workbench, Scan, or Diagnostics. Scan and Diagnostics cannot include
Workbench private headers.

## Qt Creator integration rules

Every C++ plugin must:

- Derive from `ExtensionSystem::IPlugin`.
- Use the Qt Creator plugin IID and generated metadata.
- Declare plugin dependencies in CMake `PLUGIN_DEPENDS` and qbs `Depends`.
- Keep CMake and qbs target descriptions synchronized.
- Register actions through `Core::ActionManager`.
- Respect `initialize()`, `extensionsInitialized()`, optional
  `delayedInitialize()`, and `aboutToShutdown()`.
- Remove registered objects and stop asynchronous work before destruction.

## Public services and extension points

Hard dependencies may use exported product-owned C++ interfaces. Optional
providers must be published through the Qt Creator object pool and discovered
through `ExtensionSystem::PluginManager`.

The Core API exposes narrowly scoped services and extension points:

- project service
- selection service
- device repository provider
- scan provider
- diagnostics provider
- property-page provider

The exact discovery, lifecycle, and typed Project contract is frozen in
`docs/ethercat-core-api.md`. It contains no network transport, message IDs,
serialization, or Zynq ABI concepts. Later feature-specific typed methods are
added only by a dedicated Core/API change in the owning serial plugin stage.
The typed Diagnostics contract is frozen there and implemented by the local
Mock Diagnostics plugin.

## Cross-plugin data rules

- Persistent identities use stable value IDs, not display text, row numbers,
  `QModelIndex`, widget pointers, or `QTreeWidgetItem` addresses.
- Long-lived cross-plugin data uses immutable snapshots or explicit service
  ownership.
- Optional provider pointers are invalidated when removed from the object
  pool.
- Background work does not mutate GUI models directly.
- Cross-thread delivery has explicit queued semantics and cancellation.
- Diagnostic production rate is decoupled from GUI refresh rate.
- Plugins never use `findChild()` or QObject-tree traversal to reach another
  plugin's UI.
- Plugins never include another plugin's `Internal` headers.

## TwinCAT-inspired UI ownership

The Workbench plugin owns the existing EtherCAT mode, left device tree,
selection linkage, and details-page host. Project and Devices supply public
data. Scan and Diagnostics contribute commands, pages, and Providers through
Workbench/Core extension points. The ESI Device Repository General page calls
only the public `DeviceRepositoryProvider` import/rebuild/cancel workflow and
renders its progress and result. Devices continues to own XML parsing, indexed
data, job lifetime, and cancellation semantics; Workbench retains only guarded
pointers and adds no protocol, online updater, or private cross-plugin access.
When that public repository is empty, the Workbench model now points the
non-selectable placeholder at the selectable `Device Repository` parent and its
existing `Import ESI Files...` action. This changes only translated model
presentation; it adds no command, Provider call, service contract, persistence,
or cross-plugin ownership.
Selecting an individual repository device routes the existing immutable public
`DeviceDescription` snapshot into a Workbench-private General-page widget. The
widget renders identity, offline configuration coverage, qualification, and
source provenance read-only, retains no Provider or job, and clears itself when
the context no longer resolves. `EtherCATDevices` remains the sole owner of
parsing and source data; no `Internal` header, widget pointer, model index,
public API, or controller behavior crosses the plugin boundary.
The Workbench-private navigation filter uses a stacked tree/no-match
presentation. A non-empty zero-row proxy exposes a translated no-match message
and keyboard-reachable Clear Filter action; the navigation focus proxy follows
the visible recovery control and returns to the tree after clearing. Proxy
changes update that state dynamically, while stable `NodeId` selection,
Project and repository data, and public service contracts remain unchanged.
This is a Qt Creator-native accessibility completion consistent with
Beckhoff's tree-centred engineering flow, not a claim that TwinCAT defines the
same persistent-tree empty state.

The Workbench also consumes the existing public project list, active project
ID, and active-project change signal to derive a private navigation status.
Only the active `.ecatproject` root is presented as
`Active project | Offline`; the stored Project snapshot remains `Offline`, so
Qt Creator startup-project ownership and offline controller state are not
conflated. An active switch updates model data roles without a reset, while
stable selection and Details context remain independent. Tree selection never
activates a project, and the existing active-Master drop target follows the
same ProjectService state. ProjectExplorer continues to own open, active,
handoff, and close lifecycle. Workbench now adds one context-only
`Set as Active Project` ActionManager command for an inactive Project root. Its
controller resolves the selected stable `NodeId` at trigger time and calls the
existing public `ProjectService::activateProject()` operation. It does not reuse
ProjectExplorer's private tree-bound QAction and adds no public role, Provider,
project pointer, persistent field, controller transport, or online state.

The Master-side `Add New Item...` command opens a Workbench-private selection
dialog over a copied immutable `DeviceSummary` list. It retains stable
device/master IDs rather than model indexes, and the existing Project service
continues to own validation, mutation, modified state, persistence, and
Undo/Redo. The dialog retains no Devices Provider, job, timer, or cross-plugin
object after closing; no new public contract is required.
The adjacent supported-device drag-and-drop shortcut remains inside the same
Workbench plugin. Its private, bounded MIME payload contains only the stable
device ID, and the controller resolves the exact active offline Master before
calling the same checked insertion path. Neither a `QModelIndex`, widget,
Devices object, nor mutation callback crosses a plugin boundary. Limited,
unknown, stale, non-Master, MoveAction, and between-row drops are rejected, so
this shortcut requires no public API or persistent-format change.
The project General page mirrors the relevant
TwinCAT identity hierarchy and adds the goal-required offline summary using
only public Project snapshots. Its editable name routes through the existing
`ProjectService::renameProject()` command and remains synchronized with
ProjectExplorer, the Workbench tree, title, Undo, and Redo. PLC/ADS-only fields
are hidden, and project path is omitted because the public contract does not
expose it; Workbench neither reaches into ProjectExplorer internals nor
fabricates a value. Configured-slave names, Alias values, Process Data, ordered
Startup requests, and Distributed Clocks are now editable through the checked
Project service; their repository views stay read-only. The General
page maps physical order, stable NodeId, and ESI type into read-only slave
identity fields while keeping name changes Project-owned and undoable. The
target General page mirrors the TwinCAT target summary and Version grouping,
reports only actual engineering/project values, marks runtime selection as
offline/unavailable, and routes its editable Name through the checked Project
undo stack. Target discovery and version pinning remain disabled because they
have no phase-1 provider or persistent contract. The
master General page mirrors the TwinCAT field hierarchy, derives Id and status
from existing snapshots, leaves unsupported Comment/Disabled/symbol settings
explicitly unavailable, and routes its editable Name through the same checked
Project undo stack. The master EtherCAT page mirrors the TwinCAT NetId,
master-action, and cyclic-frame hierarchy. It enables only the topology action,
which rereads the current immutable offline snapshot; ADS/NetId routing,
advanced settings, export, Sync Unit assignment, runtime task binding, and frame
generation remain explicit unavailable states. The configured-slave EtherCAT
page maps identity and physical order into the TwinCAT-style offline field
hierarchy, keeps unsupported fixed-address/identification/port state explicit,
and routes Alias changes through the same Project-owned undo stack. Neither
page adds a protocol, Provider, persistent field, or cross-plugin API. The
Workbench-owned
CoE Online Mock provides a local object-dictionary interaction prototype and
an explicit, undoable Add to Startup path without adding a transport or
background poller. Supported repository devices can now be appended from
either the repository quick action or the selected Master's searchable
TwinCAT-style selector, or copy-dragged onto the active offline Master, and
configured slaves can be removed or reordered through the same checked Project
service and undo stack. A Workbench-private ESI
factory supplies identical Process Data, Startup, and DC defaults to both the
creation path and existing property pages. The Workbench also projects the
shared `EtherCAT.Menu` ActionManager registrations into one compact Mode-level
command strip. It reuses the same actions and discovers optional
Scan/Diagnostics commands without reverse dependencies or private UI access.
EtherCATCore now provides the shared `StateService`; the Workbench renders its
highest-severity Scan/Diagnostics contribution through one mode-scoped Qt
Creator status-bar control, while the producer plugins retain ownership of
their state. It uses standard icons and preserves explicit `MOCK` labeling. The
same Workbench controller now copies public immutable Scan and Diagnostics
snapshots into a presentation-only tree overlay. It exposes topology
differences, live Mock
state, warnings, errors, and ActionManager navigation without depending on
producer-private headers, widgets, item indexes, or state machines. Provider
removal restores the retained offline presentation. The navigation header and
device-tree context menu likewise reuse one ActionManager registration per
command; context-only locate/copy and offline-topology actions remain outside
the compact strip and derive their enabled state from the current model and
stable selection. The inactive-Project-only activation action follows the same
private menu path, remains outside the shared EtherCAT menu and compact strip,
and keeps selection separate from activation. The navigation widget contributes
the Workbench action context while focused, so registered menu proxies remain
live if `EtherCAT Devices` is displayed in another Qt Creator mode. The
navigation container now follows Qt Creator's focus-proxy pattern, so activating
the navigation page gives focus to the device tree and arrow-key movement
continues through the existing stable-ID selection bridge.
EtherCATCore also reserves stable, append-only node kinds for
Inputs, Outputs, RxPDO, TxPDO, PDO,
PDO Entry, Modules, Module, and Channel selections. The Workbench now projects
validated active Process Data into Inputs, Outputs, RxPDO, TxPDO, PDO, and PDO
Entry branches with stable view and source identities. Modules / Channels has
an explicit empty state until the Devices and Project contracts contain real
modular data; no module or channel is fabricated. That data-contract work and
the remaining UI qualification keep the Workbench completion gate open. A
dedicated Core/API prerequisite now exposes checked Target/Master name changes
through `ProjectService`. The Workbench Target and Master General pages now
consume that command without moving persistent state into Workbench or adding
a controller transport.

The completed Project implementation and versioned file contract are recorded
in `docs/ethercat-project-format.md`. ProjectExplorer owns open/close and
startup-project state, including handoff after the active project closes;
EtherCATProject owns persistence and its undo stack. Workbench only observes
the corresponding public snapshots and stable IDs, except that it can request
an explicit active-project change through the public `ProjectService` contract.
The completed Devices repository and its parsing, storage, identity, and
asynchronous-lifecycle boundaries are recorded in
`docs/ethercat-devices-repository.md`.
The completed Workbench layout, model, property-page hosting, optional-provider
lifecycle, and explicit stage limits are recorded in
`docs/ethercat-workbench.md`. The completed local-only scan state machine,
comparison, branch merge, stable-ID reconciliation, and Mock boundaries are
recorded in `docs/ethercat-scan.md`.
The completed local-only diagnostic sampling, bounded histories, alarm
lifecycle, pages, actions, and cleanup rules are recorded in
`docs/ethercat-diagnostics.md`.

The intended information structure is:

```text
EtherCAT Project
└── Target Controller (offline/mock in phase 1)
    └── EtherCAT Master
        ├── Slave
        │   ├── Inputs / Outputs
        │   ├── RxPDO / TxPDO
        │   └── Modules / Channels
        └── Slave
```

Details are contributed as General, EtherCAT, Process Data, CoE mock,
Startup, DC, Online, and Diagnostics pages. Beckhoff assets, ADS, TwinCAT
project formats, and PLC editors are out of scope.

## Workbench Details lifecycle boundary

The integrated Details host owns presentation for the current stable
Workbench selection, including its no-project, no-selection, and no-provider
states. It does not own project creation, project files, navigation data, or
controller state. Zero-project versus open-project guidance is derived from the
public immutable `ProjectService::projects()` list; the private Workbench model
reset is only a refresh trigger after lifecycle changes. Selection continues to
flow through `SelectionService` as a stable `NodeId`, and contributed property
pages remain Provider-owned.

The host exposes translated accessibility metadata through standard QWidget
properties. No accessibility object, QAction, ProjectExplorer object,
`QModelIndex`, transport, timer, or persistent field crosses a plugin boundary.
The create/open wording points to Qt Creator's existing File-menu workflows;
Workbench neither duplicates those actions nor fabricates a project or online
state.

## Workbench optional Provider presentation boundary

Workbench observes optional Scan and Diagnostics capabilities only through the
existing public `ProviderRegistry` and `Provider` contract. Its private
presentation copies three facts: whether any Provider is registered, whether
any registered Provider is available, and the deterministically selected
Provider's public display name. It does not inspect Qt Creator `PluginSpec`, an
extension installation directory, or producer-private types, so it cannot and
does not claim that a missing object means a plugin is not installed.

The private state is `Absent`, `Unavailable`, or `Available`. A typed available
Provider wins over unavailable alternatives. Among available Providers, the
same activity/result score that selects the copied Scan result or Diagnostics
snapshot also selects the public display name; stable Provider ID breaks ties.
Otherwise stable Provider ID ordering chooses the displayed unavailable name.
Production Scan and Diagnostics names contain `Local Mock`, keeping the V1
boundary explicit without introducing a producer ID dependency. Whitespace-only
names are normalized once to a kind-specific neutral `Unnamed ... Provider`
fallback before the copied presentation reaches any consumer; Workbench does
not infer a Mock identity from a missing public name. Availability, display-name,
activity, result, and snapshot signals enter one atomic refresh.
Object-pool about-to-remove handling applies the same exclusion to both the
name and copied data before removal, and re-addition is selected from the
current retained public state.

This is presentation state only. It does not cross a plugin boundary, add a
public enum or service, transfer a Provider pointer into the model, or change
action ownership. `Available` does not mean target connected, hardware online,
or data current. Scan and Diagnostics retain command, page, state-machine,
timer/thread, snapshot, and shutdown ownership; Workbench retains only copied
immutable values plus a copied name/state summary. A pre-snapshot Diagnostics
request remains source-neutral; a separate `MOCK` or `Online` source label
appears only after a copied snapshot supplies that source state. A pure
name/source change refreshes Details text without reconstructing its pages. No
persistence, network, controller protocol, CMake/qbs entry, or upstream Qt
Creator path is added.

## Workbench invalid-project boundary

The Project plugin remains the owner of `.ecatproject` parsing, recovery
snapshots, project tasks, persistence, and startup-project integration.
For this invalid-state decision, Workbench uses the existing immutable
`ProjectSnapshot::valid` and `ProjectSnapshot::error` facts; no new parsing
contract is introduced. The recovery Project ID remains an existing runtime
correlation key, but Workbench does not present or copy it as persisted file
identity; the file-derived name remains display identity. Workbench does not
repair or reinterpret the file, or persist a generated recovery snapshot.

For an invalid snapshot, the private Workbench tree exposes one Project root
and one non-selectable recovery placeholder. It deliberately suppresses the
generated Target, Master, slave, Diagnostics, and drop-target presentation.
The existing Node-ID copy action is unavailable at the invalid root and its
execution slot independently rejects the generated recovery ID.
The private Details page likewise treats every field not established by the
failed parse as unavailable. This is a presentation qualification, not a new
Core or Project contract.

ProjectExplorer continues to own startup-project state. Workbench may reject
its own explicit activation command for an invalid selection, but it does not
override an invalid project that ProjectExplorer has independently made
active. Active and invalid are therefore composable presentation facts. Stable
selection is cleared through the existing controller refresh when the invalid
project closes.

The implementation changes no public API, model role, service, Provider,
metadata, dependency, source list, persistence format, CMake/qbs entry, or
upstream Core, ProjectExplorer, or application path. The Workbench path count
remains 44 and the direct Core patch count remains five.

## Workbench project-scoped Diagnostics navigation boundary

Workbench remains a private consumer of the existing copied tree and stable
selection state. `Open Diagnostics` resolves the current `NodeId` through the
existing `SelectionService`, derives the selected project's context, and
selects a Diagnostics index only for that exact non-null project ID. An
invalid project owns no Diagnostics node, so action state and direct execution
both reject cross-project fallback. A null project ID still means the existing
projectless first-available lookup only when the stable selection itself is
null or belongs to a known projectless node. An unknown non-null `NodeId`
clears the stale tree row and is disabled rather than treated as projectless.

The global action updater, Workbench context menu, and navigation slot share
this boundary without adding a command, model role, signal, service, or public
interface. A placeholder context menu temporarily disables the action, then
restores the stable selection and action state when it closes. The updater
explicitly disables the action when `SelectionService` has already been
released during controller shutdown. The Diagnostics plugin continues to own
Provider state, snapshots, commands, pages, timers, threads, and teardown;
Workbench only selects an existing copied tree node.

The implementation changes no public API, persistence format, metadata,
dependency, source list, CMake/qbs entry, or upstream Core, ProjectExplorer,
or application path. The Workbench path count remains 44 and the direct Core
patch count remains five.

## Workbench tree-row accessibility boundary

The Workbench-private tree model now projects each cell's existing Display text
through Qt's standard `AccessibleTextRole` and its complete truthful tooltip
through `AccessibleDescriptionRole`. This makes the current name, status,
parser error, optional-Provider identity, Mock scan/diagnostic details, ESI
identity, and offline drag/drop guidance available through the item-model
contract without adding a custom role or a second presentation state.

Active-project and Provider-overlay updates advertise the two roles in their
existing explicit `dataChanged` lists. Device identity/name and drop-target
updates already emit an empty role list, which Qt defines as all roles changed.
The existing `QSortFilterProxyModel` forwards the roles to the navigation view.
No accessibility object, Provider pointer, project object, `QModelIndex`,
widget, or translated string crosses a plugin boundary.

This issue changes no public API, source list, CMake/qbs entry, dependency,
metadata, persistence, QAction, Provider, thread, timer, network or controller
behavior, or upstream Core, ProjectExplorer, or application path. The
Workbench path count remains 44 and the direct Core patch count remains five.

## Workbench project-scoped Locate boundary

`Locate First Topology Difference` and `Locate First Issue` remain private
Workbench presentation commands over copied tree state. The current stable
selection is resolved through the existing `SelectionService` and private
`PropertyPageContext`. A known non-null project ID restricts each model lookup
to that exact project; no matching row means disabled action state and a direct
request no-op. A genuinely null selection or a known projectless repository
node retains global lookup, while an unknown non-null ID is rejected instead
of being reinterpreted as projectless.

The ActionManager source actions, active Workbench-context proxies, tree
context menu, command strip, and direct navigation slots share this rule. A
placeholder menu temporarily restricts both commands and restores the stable
selection's scoped state when it closes. Controller teardown disables the
actions if the Selection Service has already been released. The tree model
continues to hold only copied immutable Scan/Diagnostics presentation and
stable IDs; neither Provider pointer nor `QModelIndex` crosses a plugin
boundary.

This is an Embed Labs Qt-native multi-project guard inferred from Qt Creator's
selected-project action convention and Beckhoff's selected-I/O-device
workflows. It adds no public API, model role, QAction, source file, dependency,
persistent format, Provider, controller transport, online or hardware behavior,
CMake/qbs entry, or upstream Core, ProjectExplorer, or application path. The
Workbench path count remains 44 and the direct Core patch count remains five.

## Workbench offline-slave removal confirmation boundary

The configured-slave removal question and its stable candidate remain private
to `EtherCATWorkbench`. The action layer captures copied project, Master, and
slave IDs together with the displayed name and position before opening a
standard `QMessageBox`. It retains only a `QPointer` to the controller while
the modal interaction is active; no Project object, Provider pointer,
`QModelIndex`, or widget pointer crosses a plugin boundary.

After affirmative confirmation, the controller resolves the current stable
Selection and latest immutable Project snapshot again. It accepts the command
only if selection, project, Master, and slave still match the captured IDs.
Selection drift, project close, slave removal, controller teardown, and stale
context are rejected without calling the Project mutation service. This keeps
the consequence described by the question attached to the exact configuration
candidate and prevents an intervening event from retargeting the removal.

The actual mutation remains the existing checked
`ProjectService::replaceOfflineSlaves()` command. EtherCATProject therefore
continues to own validation, modified state, persistence, position
normalization, Undo, and Redo. Workbench owns only confirmation presentation,
stable-context validation, and post-command selection repair. `No` and Escape
are non-mutating; the closed-project path causes no second or stale mutation.

The implementation changes no public service, data type, persistent format,
metadata, dependency, source list, CMake/qbs entry, Provider, thread, timer,
network/controller behavior, or upstream Core, ProjectExplorer, or application
path. The Workbench path count remains 44 and the direct Core patch count
remains five.

## Workbench repository quick-add target boundary

The Device Repository quick-add presentation and its
`OfflineMasterTarget` value remain private to
`EtherCATWorkbench::Internal`. The value contains copied Project and Master
stable IDs plus their display names. It does not retain or pass a Project
object, Provider pointer, `QModelIndex`, or widget pointer across a plugin
boundary.

The existing ActionManager action owns the optional target that it currently
displays. Project open, active-project, rename, and close-fallback events
replace that copied value and refresh the text, tooltip, status text, and
enabled state. Triggering the action passes the same displayed value to the
controller; it does not independently choose a destination from a later UI
selection.

Immediately before mutation, the controller resolves the current active valid
Project and its Master from the public Project service again. Both stable IDs
must match the displayed target. A stale target is rejected without mutation,
preventing an action that names Project A from writing Project B after an
intervening active-project change. Names remain presentation data only and are
not identity keys.

The actual mutation remains the existing checked Project replacement command.
EtherCATProject therefore continues to own validation, modified state,
persistence, slave-position normalization, Undo, and Redo. Workbench owns only
target presentation, stable-context revalidation, and post-command selection
repair. Optional Scan and Diagnostics Providers do not participate in target
selection and remain local Mock capabilities.

The implementation changes no public service or public data type, persistent
format, metadata, dependency, source list, CMake/qbs entry, Provider, thread,
timer, network/controller behavior, or upstream Core, ProjectExplorer, or
application path. The Workbench path count remains 44 and the direct Core patch
count remains five.

## Workbench Details Provider-removal continuity boundary

Details page continuity remains private to
`EtherCATWorkbench::Internal::DetailsView`. Each created page carries the
existing private, value-only `EtherCAT.PageKey`, formed from the publishing
Provider ID and page ID. The value is presentation identity only; it is not a
public contract or persisted setting.

On object-pool removal, ProviderRegistry first unlinks the departing Provider
from its guarded registry and then emits `providerAboutToBeRemoved`; the object
itself remains in the PluginManager pool until that notification returns.
Details disconnects the Provider's availability signal and retains its
value-only ID in a private exclusion set for already queued work. Registry
queries and nested removal therefore cannot recreate the departing page. An
ordinary hosted page is detached and destroyed before `removeObject()`
returns. If the Provider self-unregisters inside its own active page callback,
the page is removed from the live-page map immediately. An already-tabbed
active widget may remain attached and alive only until that callback returns,
when it is destroyed. The Provider/plugin must remain loaded, including page
destructor and Qt meta-object code, through that boundary.

Every PropertyPage Provider removal enters the Details-owned unified refresh
path. It restores the copied key only if the context is still current and a
currently registered and available Provider still publishes that page. If any
intervening operation or user choice occurred, the refresh uses the newest
semantic state instead of applying stale switch-away/switch-back ABA state.
The value-only departing marker remains authoritative until a later
`providerAdded` for that ID clears it. If the selected page itself disappeared,
the normal first-page fallback remains authoritative.

Provider addition and availability changes continue to use the same private
rebuild pipeline and semantic key. No selection is written, and no Project,
device, Scan, Diagnostics, controller, or persistence service participates.
The test Providers are guarded so an early test assertion cannot leave an
object-pool pointer alive during plugin teardown.

The implementation changes no public service or data type, persistent format,
metadata, dependency, source list, CMake/qbs entry, Provider implementation,
thread, timer, network/controller behavior, or upstream Core,
ProjectExplorer, or application path. The Workbench path count remains 44 and
the direct Core patch count remains five.

## Workbench context-menu selection boundary

The Workbench device-tree popup remains a private view over the existing
ActionManager commands and stable Selection Service. Each menu captures only
the opening selection's value-type `NodeId`. If Selection Service publishes a
different ID while the popup is active, the stack-owned menu closes. The new
selection remains current, and the existing post-menu synchronization updates
the tree and shared command state from that value.

This is necessary because the popup contains the same `Command::action()`
objects used by other Workbench surfaces, while their registered callbacks
intentionally resolve the current selection at trigger time. Keeping a popup
from an older selection open would let its presentation and the callback's
mutation target diverge. Closing it preserves QAction identity, shortcuts,
toolbars, context registration, and current-selection semantics without adding
per-popup actions or stable mutation candidates to every controller command.

The connection uses the menu as its QObject context, retains no service or
widget pointer after the menu is destroyed, and does not block selection
delivery. A placeholder menu uses the pre-existing stable selection rather
than the placeholder row as its baseline, preserving the established
temporary restriction/restoration path. The removal confirmation retains its
separate stable-candidate revalidation after the popup command is chosen.

Qt documents `QMenu::exec()` and normal action-signal delivery at
<https://doc.qt.io/qt-6/qmenu.html#exec>, and Qt Creator documents the shared
ActionManager command action at
<https://doc.qt.io/qtcreator-extending/actionmanager.html>. Qt Creator 20.0's
Project tree retains context-menu focus until the popup hides:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttree.cpp#L337-L395>.
Beckhoff documents the selected configured I/O device as the target of its
right-click context menu:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
The exact close-on-selection-drift behavior is an Embed Labs Qt-native safety
boundary rather than a copied TwinCAT contract.

This issue changes no public API, model role, QAction registration, source
list, dependency, metadata, persistence, Provider, production thread or
timer, network/controller behavior, CMake/qbs entry, or upstream Core,
ProjectExplorer, or application path. The Workbench path count remains 44
and the direct Core patch count remains five.

## Workbench navigation expansion-state boundary

Expansion state belongs entirely to the private
`EtherCAT::Workbench::Internal::WorkbenchNavigationWidget`. Its identity key is
the existing value-type `Data::NodeId`; no `QModelIndex`, view, source model,
proxy model, or Provider pointer crosses a reset. `QTreeView::expanded` and
`collapsed` update the widget-local normal-state set.

`modelAboutToBeReset` and `modelReset` form the capture and restoration
boundary. The about-to-reset connection is established before the proxy model
observes the source model, so reset-generated view changes cannot overwrite
the saved normal state. After reset, deleted IDs are discarded, surviving IDs
are restored, new Project structure retains the established depth-two default,
and Selection Service is applied last. Selection Service remains the only
authority for which stable node is current; making its ancestor path visible
is an intentional view effect, not a selection write.

Filtering uses a separate temporary expansion mode. Filter-driven
`expandAll()` calls and late matching rows do not enter the normal-state set.
When filtering ends, the widget restores the normal set before revealing any
externally selected node. The set is neither a cross-plugin service nor a
persisted application setting and disappears with the navigation widget.

Qt defines the expansion signals and model-reset invalidation boundaries at
<https://doc.qt.io/qt-6/qtreeview.html#expanded> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel>. Qt Creator's
Project tree provides a local architectural precedent by wiring expansion
signals to semantic model data and requesting expansion after rebuild:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttreewidget.cpp#L291-L302>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L486-L496>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L526-L539>.
Workbench reuses no ProjectExplorer class or private API.

Beckhoff's I/O / Devices tree is used only as the user-facing hierarchy
comparison for devices, Process Image, and status/control inputs and outputs:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>.
The actual Workbench tree remains a local offline Project snapshot. Scan and
Diagnostics remain public local Mock Provider snapshots and do not own or
persist expansion state.

This boundary adds no service, role, Provider, public API, persistent field,
thread, timer, dependency, network/controller transport, CMake/qbs entry, or
path under upstream Core, ProjectExplorer, or the application bootstrap. The
Workbench path count remains 44 and the direct Core patch count remains five.

## Workbench Details focus and operation boundary

Details focus continuity remains private to
`EtherCATWorkbench::Internal::DetailsView`. Its restoration token contains
only the current context `NodeId`, semantic Provider/Page key, child
`objectName`, rebuild generation, and active/cancelled state. It never retains
a page pointer, child pointer, `QModelIndex`, or Provider pointer across a page
replacement. A recreated semantic page may resolve its new focus child by
object name; an invalid target uses the normal page focus chain.

One Details-owned bounded operation pump serializes rebuild and refresh work.
It preserves value-only transaction anchors through nested Provider callbacks,
coalesces duplicate requests, and yields to one posted continuation after at
most eight synchronous operations. Page-map transfer happens before tab
mutation, and internal insertion/restoration signals are suppressed while the
host owns that mutation. Nested context changes, provider availability,
`updatePage()`, page hide/destruction, object-pool signals, and early MetaCall
draining therefore cannot reuse stale tab indexes or delete a page twice.

The saved transaction does not outrank user interaction after a yield. A real
tab `currentChanged`, `tabBarClicked`, or focus choice updates or cancels the
anchor. Direct focus on the tab bar remains there, and any focus outside
Details remains external. The continuation restores only semantic state that
is still current; it never writes an earlier focus or page choice over a newer
one.

ProviderRegistry's lifecycle boundary is product-owned Core API. It removes a
departing Provider from registry enumeration before emitting
`providerAboutToBeRemoved`, while PluginManager retains the object until the
notification returns. Registry queries and nested removal cannot rediscover
the Provider. Details destroys ordinary hosted pages before `removeObject()`
returns. For self-unregister inside `pages()`, `createPage()`, or
`updatePage()` callback, no callback-associated widget remains in the live-page
map after removal handling. An already-tabbed active widget may stay attached
and alive only until callback return, when it is destroyed. The Provider/plugin
must keep the page destructor and Qt meta-object code loaded through that
boundary.

The changes remain within existing product-owned EtherCATCore registry/test
and private EtherCATWorkbench Details/test files. They add no public Provider
shape, persistent state, source list, dependency, thread, timer,
network/controller behavior, or physical-hardware claim. No CMake or qbs file
changed, so qbs was not run. `src/plugins/ethercatcore` is not an upstream Qt
Creator Core path. The Workbench path count remains 44 and the direct upstream
Core patch count remains five.

## Workbench CoE filter-result boundary

The CoE empty state, filter model, and Clear Filters action are owned entirely
by the private `EtherCAT::Workbench::Internal::CoeOnlinePage`. One result
refresh path follows proxy row insertion/removal, model reset, layout change,
data change, text filtering, and advanced-filter changes. The stacked result
area therefore cannot treat a model-driven zero-row transition as an ordinary
blank dictionary.

Selection continuity stores only the existing private numeric `AddressRole`.
It retains no `QModelIndex`, source/proxy item, view, or QObject pointer across
a result mutation. Text, range, Hide Standard, and Hide PDO state are cleared
as one guarded presentation transaction. The Mock/offline source mode,
Property-page context, and Project snapshot remain owned by their existing
components and are not changed by that action.

CoE remains a local Mock/offline prototype. Its existing ProjectService
Add-to-Startup path is unchanged, and this boundary introduces no public
service, Provider, role, persistence field, source file, dependency, thread,
timer, SDO/controller transport, network access, or physical-hardware claim.
No CMake or qbs description changed.

## Workbench insert-device target lifecycle boundary

The private Master-side insert dialog retains only the stable Project and
Master IDs that identified its target when it opened. ProjectService remains
the sole Project-lifecycle owner. `projectAboutToBeRemoved`,
`activeProjectChanged`, and matching `projectChanged` notifications invalidate
the open dialog when the target Project closes, loses validity or activity, or
no longer publishes the same Master.

Each notification connection uses the dialog as its QObject context and
therefore cannot outlive the stack-scoped dialog. A visibility guard makes
overlapping lifecycle notifications idempotent. Rejection exits before the
existing add command and retains no Project snapshot, model index, widget, or
service pointer for later mutation. The accepted path and Project-owned
revalidation remain unchanged.

Qt's rejected-dialog and context-object connection contracts are documented
at <https://doc.qt.io/qt-6/qdialog.html> and
<https://doc.qt.io/qt-6/qobject.html>. Qt Creator 20.0 provides a local
Project-lifecycle precedent by removing Project-bound UI on
`aboutToRemoveProject`:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1509-L1515>.
Beckhoff's selected-target insertion flow is only the product comparison:
<https://infosys.beckhoff.com/content/1033/eap/1521664395.html>.

This boundary adds no public service or API, source file, dependency,
persistence field, Provider, thread, timer, network/controller transport,
physical-hardware behavior, CMake/qbs entry, or path under upstream Core,
ProjectExplorer, or the application bootstrap. The Workbench path count
remains 44 and the direct upstream Core patch count remains five.

## Existing EasyBoard isolation

EasyBoard is not an EtherCAT plugin and must not become a shared container for
new EtherCAT behavior. Its existing Core extension point is a recorded legacy
delta, not the architecture template for new work.

## Per-plugin completion gate

A plugin is complete only when:

- Its ownership and public interfaces are documented.
- CMake, qbs, metadata, and dependencies agree.
- It loads, can be disabled where applicable, and shuts down cleanly.
- Normal, empty, error, cancellation, and cleanup paths are tested.
- Thread, timer, future, QObject ownership, and provider-removal behavior are
  understood and tested.
- No later plugin functionality was implemented early.
- No unrecorded upstream Core patch was added.
- The change is reviewed and committed locally on `embed-labs`.

The next plugin remains pending if any gate is missing.
