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
Project service; their Process Data, CoE, Startup, and DC repository catalogue
views stay read-only. The General page maps physical order, stable NodeId, and
ESI type into read-only slave
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
highest-severity Scan/Diagnostics contribution and a value-only semantic
projection from the preferred Diagnostics Provider through one mode-scoped Qt
Creator status-bar control, while producer plugins retain ownership of their
state. It uses standard icons, preserves explicit `MOCK` labeling for local
snapshots, and uses neutral Provider-reported wording otherwise. The same
Workbench controller now copies public immutable Scan and Diagnostics
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

## Workbench Process Data accessibility boundary

The private `ProcessDataPage` owns the widget and item-model accessibility
metadata for its five tables. The page assigns each `QTableView` a unique
translated name and a concise purpose description. Its private table models
map complete display values to the standard `Qt::AccessibleTextRole` and build
`Qt::AccessibleDescriptionRole`/tooltip strings from the column heading,
optional row Name, full cell value, and existing table-specific guidance.

The generic description helper resolves only values from the current valid
index and its current row. It stores no `QModelIndex`, model item, widget,
Provider, Project snapshot, or object pointer across calls. Existing model
reset paths therefore remain the sole lifecycle boundary. The PDO Assignment
check cell publishes `Assigned` or `Not assigned`, while its existing
`Qt::CheckStateRole`, mandatory/mapping constraints, and ProjectService command
path remain authoritative. Mandatory and unsupported mappings retain their
specific reason before the read-only fallback; only a genuinely editable,
supported assignment advertises selection.

This boundary does not alter splitter geometry, section-resize policy,
selection, editing, configuration validation, Undo/Redo, persistence, or the
Mock/offline source boundary. It adds no public/custom role, API, source file,
dependency, Provider, Project command, thread, timer, network/controller
transport, or physical-hardware behavior. No CMake or qbs description changed.

Qt's standard roles and widget accessible properties are documented at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop>. Qt Creator 20.0
provides explicit widget and model accessibility precedents at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's Process Data table hierarchy remains only the product comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.

## Screen-bound offline topology dialog

The private `EtherCATPage` owns the master Topology dialog, its stack lifetime,
and its read-only table geometry. `ISSUE-WB-TOPOLOGY-DIALOG-BOUNDS-001`, based
on `9e56cfebbd09766ea7ae326b5741acb95fecb274`, removes the table's unbounded
full-content minimum width. It does not move the dialog into a Provider or
introduce a retained dialog object.

The page still calculates the complete ten-column preferred width after
`ResizeToContents`. It activates the dialog layout, expands the preferred
width when that complete content fits, and then bounds the initial size to the
current page screen's `availableGeometry()` with existing `SpacingTokens` as
work-area insets. The page screen is authoritative because the stack dialog
has not yet been shown; this follows the local centered Locator popup pattern.
The resulting rectangle is centered within that same work area. When the full
content is wider, `ElideNone` and `ScrollBarAsNeeded` preserve it behind the
ordinary horizontal viewport instead of enlarging the top-level window.

No screen, dialog, table item, model index, Project snapshot, Provider, or
controller object is stored across calls. The existing modal stack lifetime
and close path remain authoritative. The change does not alter slave ordering,
selection, Project mutation, Undo/Redo, persistence, empty topology behavior,
or the Mock/offline source boundary. It adds no source file, public API,
dependency, Provider, thread, timer, network/controller transport, online
state, physical-port model, or hardware behavior. No CMake or qbs description
changed.

Qt defines the relevant boundaries in
<https://doc.qt.io/qt-6/qheaderview.html#ResizeMode-enum>,
<https://doc.qt.io/qt-6/qscreen.html#availableGeometry-prop>, and
<https://doc.qt.io/qt-6/qabstractscrollarea.html#horizontalScrollBarPolicy-prop>.
Beckhoff's Topology dialog remains a product comparison only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html>.

## Workbench Startup table accessibility boundary

The private `StartupPage` and `StartupTableModel` own the widget and item-model
accessibility metadata for the ordered offline request table. The table has a
translated name and purpose description. Every valid cell maps its complete
current value to Qt's standard `AccessibleTextRole`; the intentionally empty
Enabled display cell instead publishes `Enabled` or `Disabled`, while its
existing `CheckStateRole` remains authoritative.

`AccessibleDescriptionRole` and `ToolTipRole` are built only from the current
valid row: object index/subindex, current column heading, complete cell value,
and guidance derived from the current item flags. A fixed angle-bracketed ESI
request advertises all existing restrictions, a catalogue context remains
read-only, and only a genuinely editable configured-slave cell advertises
editing. No model index, row object, widget, Project snapshot, Provider, or
controller object is retained across the call. Existing model resets remain
the lifecycle boundary.

This boundary does not alter table geometry, section-resize policy, selection,
editing, validation, ProjectService, Undo/Redo, persistence, ESI parsing, or
the offline source boundary. It adds no public/custom role, API, source file,
dependency, Provider, Project command, thread, timer, SDO/network/controller
transport, online state, or physical-hardware behavior. No CMake or qbs
description changed.

Qt's standard roles and widget properties are documented at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop>. Qt Creator 20.0
provides local widget and model precedents at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's Startup ordering, columns, and fixed-request semantics remain only
the product comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

## Workbench CoE dictionary cell accessibility boundary

The private `CoeOnlinePage` object model owns the standard item-role metadata
for its five-column hierarchical object dictionary. Each valid cell maps its
complete current Display value to Qt's standard `AccessibleTextRole`, including
a valid empty `QString` for an empty Unit. `AccessibleDescriptionRole` and
`ToolTipRole` are derived on demand from the current valid index: object
address, heading, complete value, data type, current Mock/offline source,
prototype access boundary, and operation guidance.

The guidance derives editability from the current item flags. Within the
qualified configured-slave context, only a Mock Value cell can advertise a
temporary local edit; offline cells report read-only. Every description states
that no controller connection or SDO transfer occurs. A role query retains no
`QModelIndex` or additional object reference across calls; ownership of the
existing model object tree is unchanged. Existing model resets remain the
lifetime boundary, and clients must resolve a new index after the Mock/offline
source changes.

After a successful Mock Value edit, the private model reports every affected
standard presentation/accessibility role and its existing raw-value role in
one `dataChanged` signal. The edit does not become a ProjectService command or
persistent Project data. Existing selection and Add to Startup behavior remain
consumers of the current object address; this issue changes neither path.

This boundary does not alter dictionary geometry, hierarchy, filtering,
selection, editors, Add to Startup, ProjectService, persistence, ESI parsing,
or the Mock/offline source model. It adds no public/custom role, API, source
file, dependency, Provider, Project command, thread, timer,
SDO/network/controller transport, online state, or physical-hardware behavior.
No CMake or qbs description changed. Repository Device CoE editability was not
qualified by this accessibility boundary and is qualified independently below
by `ISSUE-WB-COE-REPOSITORY-READONLY-001`.

Qt's standard roles and role-specific notification contract are documented at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html#dataChanged>. Beckhoff's CoE
Online table remains only the product comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## Workbench repository Device CoE read-only boundary

The private `CoeOnlinePage` remains the owner of the distinction between an
existing configured slave and a Device Repository catalogue item. Its current
`PropertyPageContext`, the private tree's current offline-slave copy, and the
public Project snapshot determine one value-only Mock editing permission. No
Project, repository object, Provider pointer, `QModelIndex`, or widget crosses
a plugin boundary to represent that permission.

The permission enters `CoeObjectModel::setDefinitions()` in the same
`beginResetModel()` / `endResetModel()` transaction that replaces the object
tree. A repository-to-configured or configured-to-repository context switch
therefore cannot expose old item flags or operation text after the reset;
clients must resolve fresh indexes. The existing private proxy forwards the
new source-model contract and adds no second permission state.

Object capability and view permission remain separate. ESI-derived
`writable`, `WritableRole`, and the visible `RW` flag continue to describe the
object. `flags()` advertises `ItemIsEditable`, and `setData()` accepts
`EditRole`, only when the page supplied configured-slave permission, the Mock
source is active, and the object/value itself is writable and non-synthetic.
Repository Device and offline contexts omit that permission, so their
accessible description and tooltip automatically report read-only through the
existing flag-derived guidance. The existing Add to Startup context check
continues to require a configured slave and Project; repository selection stays
non-mutating.

This is a Workbench-private presentation and transient-edit boundary. It does
not change ESI parsing, repository ownership, ProjectService, persistence,
Undo/Redo, filtering, Update List, Show Offline Data, source selection, page
geometry, public/custom roles, Provider contracts, or any controller, SDO,
network, online, or physical-hardware behavior. It adds no source file,
dependency, CMake/qbs entry, or path under upstream Core, ProjectExplorer, or
the application bootstrap. The Workbench path count remains 44 and the direct
upstream Core patch count remains five.

Qt defines `ItemIsEditable` as an item capability and requires an editable
model to implement both `flags()` and `setData()`:
<https://doc.qt.io/qt-6/qt.html#ItemFlag-enum> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html>. Beckhoff's CoE page preserves
the distinction between `RW`/`RO` object metadata and offline values sourced
from the device description:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## Workbench ESI device-selection cell accessibility boundary

The private `EsiDeviceSelectionDialog` owns its `QStandardItemModel`, proxy,
tree, selection, and Add-button lifecycle. The same translated seven-element
column-label list supplies the model headers and per-cell descriptions. Every
new item copies its complete current text into Qt's standard
`AccessibleTextRole`; `AccessibleDescriptionRole` and `ToolTipRole` are value
objects derived at row creation from the column label, complete cell value,
the row's existing Supported/Limited capability, and the offline operation
boundary. No `QModelIndex`, device object, Project snapshot, or controller
object is retained by the metadata.

The private Device row remains the only owner of its existing stable device ID,
support flag, latest-revision flag, icon, and item flags. Supported metadata
describes the existing append path, while Limited metadata truthfully
describes the existing rejection path. The proxy still owns filtering and
sorting, the dialog still selects the first supported visible row, and the
existing accepted signal remains the only entry to Project mutation.

This boundary does not alter visible layout, header resize modes, elision,
filtering, revision visibility, selection, insertion, ProjectService,
persistence, ESI parsing, or dialog ownership. It adds no public/custom role,
API, source file, dependency, Provider, Project command, thread, timer,
controller/network transport, online state, or physical-hardware behavior. No
CMake or qbs description changed. The Workbench path count remains 44 and the
direct upstream Core patch count remains five.

Qt documents the standard item setters and roles at
<https://doc.qt.io/qt-6/qstandarditem.html> and
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Beckhoff's offline append,
search, extended-information, and revision workflow remains only the product
comparison:
<https://infosys.beckhoff.com/content/1033/el331x/1036999947.html> and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.

## Workbench project-scoped Details refresh boundary

`ISSUE-WB-DETAILS-UNRELATED-PROJECT-DRAFT-001` keeps this ownership rule
inside the product-owned Workbench plugin.

The private `DetailsView` owns the mapping from the current stable selection
to its `PropertyPageContext` and hosted page set. `ProjectService` continues to
publish value-only `ProjectSnapshot` changes for every open Project. Details
now consumes that snapshot ID and invokes its existing bounded page-refresh
pump only when the ID equals the current context's Project ID. It reads the
context at delivery time, so the connection stores no stale selection token,
Project pointer, page pointer, or Provider pointer.

This is intentionally a Project ownership boundary, not a dirty-form merge
framework. An unrelated Project cannot call the current page Provider's
`updatePage()` and therefore cannot replace a focused General or DC text draft.
A change to the current Project remains authoritative and continues through
the existing refresh path. Repository contexts have a null Project ID and do
not consume Project changes; their existing DeviceRepository reset/change/
indexing signals remain separate. Project-tree synchronization still observes
all Projects, and selection/model-reset handling remains responsible for close
and invalidation lifecycle.

The filter stays in product-owned `EtherCATWorkbench`. It does not change the
public `ProjectService` signal, `PropertyPageProvider` contract, page ownership,
rebuild generation, focus token, operation pump, Project persistence, Undo/
Redo, or plugin load/unload path. It adds no API, source file, dependency,
Provider, Project command, model role, thread, timer, network/controller
transport, online state, or physical-hardware behavior. No CMake or qbs
description changed. The Workbench path count remains 44 and the direct
upstream Core patch count remains five.

Qt's `QLineEdit` contract explains why an unnecessary `setText()` destroys
draft state, and QObject's typed connection lets the private lambda consume
the changed snapshot:
<https://doc.qt.io/qt-6/qlineedit.html#modified-prop> and
<https://doc.qt.io/qt-6/qobject.html#connect-5>. Qt Creator 20.0's per-Project
settings listeners provide the local ownership precedent:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L580-L619>.
Beckhoff's selected-terminal tabs remain only the product comparison:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.

## Workbench General property tree accessibility boundary

`ISSUE-WB-GENERAL-PROPERTY-TREE-A11Y-001` keeps the contract inside the
product-owned private `GeneralPage`. The page owns the `QTreeWidget`, its
header, and every `QTreeWidgetItem`; `addRow()` derives all metadata from the
same two current display strings used to create the row. No model index,
Project snapshot, device description, slave object, page context, or Provider
pointer is retained by the metadata.

The widget-level name identifies the General property table, while its
description states the read-only offline boundary. Every cell copies its exact
display string into Qt's standard `AccessibleTextRole`. Its standard
`AccessibleDescriptionRole` and tooltip are equal value objects derived from
the visible column heading, row property, complete value, and the same offline
operation boundary. This preserves the existing visible elision while making
complete ESI match, source, and owner-slave values available without a custom
role or alternate data model.

The same private path serves configured-slave General and the real Modules /
Channels context. The latter continues to resolve its owning offline slave
through the existing `PropertyPageContext`; no second ownership or refresh
path is added. Registered future Module/Channel contexts will inherit the same
metadata if those node types are generated later, but this issue claims runtime
coverage only for configured slave and Modules / Channels.

This boundary does not change visible layout, row order, headers, resize
modes, elision, selection, page creation, context ownership, ESI parsing,
ProjectService, persistence, Undo/Redo, or plugin load/unload. It adds no
public/custom role, API, source file, dependency, Provider, Project command,
thread, timer, controller/network transport, online state, or physical-hardware
behavior. No CMake or qbs description changed. The Workbench path count remains
44 and the direct upstream Core patch count remains five.

Qt documents widget accessibility properties and standard item roles at
<https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop> and
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Qt Creator 20.0's
`FancyMainWindow` and Terminal model provide local precedents:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's selected-terminal General tab remains only the product comparison:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.

## Workbench EtherCAT SyncManager cell accessibility boundary

`ISSUE-WB-ETHERCAT-SYNCMANAGER-CELL-A11Y-001` stays inside the product-owned
private `EtherCATPage`. The page already owns its `QTreeWidget`, translated
seven-column header, and ESI-derived rows. Its private
`addSyncManagerRow()` path now derives standard item metadata from the same
current strings used to construct each row. It retains
no model index, ESI object, Project snapshot, slave object, page-context
pointer, or Provider pointer.

The widget name remains concise. Its configured-slave and repository Device
descriptions now state the same read-only local/offline ESI and no-controller,
network, or physical-hardware boundary. Every cell copies its exact display
string into Qt's standard `AccessibleTextRole`. Its standard
`AccessibleDescriptionRole` and tooltip are equal value objects derived from
the current header, SM index/name identity, complete value, and operation
boundary. Empty visual values retain an empty accessible text while the
description explicitly says Empty. A configured-slave to repository Device to
configured-slave test proves that the existing reset/repopulation path owns
the metadata lifecycle and does not leak a prior context.

This is the ESI defaults table on the EtherCAT page, not the richer Process
Data Sync Manager selection model. It remains read-only in both real contexts
and does not adopt Beckhoff's FMMU/SM editing or controller-download behavior.
It does not change headers, geometry, scrolling, selection, context ownership,
ESI parsing, ProjectService, persistence, Undo/Redo, or plugin load/unload. It
adds no public/custom role, API, source file, dependency, Provider, Project
command, thread, timer, controller/network transport, online state, or
physical-hardware behavior. No CMake or qbs description changed. The Workbench
path count remains 44 and the direct upstream Core patch count remains five.

Qt documents the standard item roles and role-specific `QTreeWidgetItem`
storage at <https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qtreewidgetitem.html#setData>. Qt Creator 20.0's
`FancyMainWindow` and Terminal model provide local precedents:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's EtherCAT and FMMU/SM pages remain only the information hierarchy and
field-semantics comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/4981170059.html>.

## Workbench offline topology cell accessibility boundary

`ISSUE-WB-TOPOLOGY-CELL-A11Y-001` stays inside the product-owned private
`EtherCATPage::showMasterTopology()` path. The page owns the stack-local
dialog, `QTreeWidget`, translated ten-column header, and every
`QTreeWidgetItem`. Each row derives its standard item metadata from the same
current strings used for Display. No dialog, table, item, model index, Project
snapshot, slave object, page-context pointer, or Provider pointer is retained
after the modal call returns.

The widget description identifies read-only configured slave order and
identities from the current offline Project. Every cell copies its exact
complete display string into Qt's standard `AccessibleTextRole`.
`AccessibleDescriptionRole` and `ToolTipRole` are equal value objects derived
from the translated column heading, configured Position, complete slave Name,
complete value, and the same offline boundary. The description explicitly
states that physical ports are not modeled and that no controller, network,
or physical hardware is accessed.

This table remains distinct from the member SyncManager ESI-defaults tree and
the richer Process Data models. Position and Auto Inc Addr remain offline
configuration values or derivations. Previous remains predecessor-list
context, not verified wiring; Port remains `Not modeled`; Status remains
`Offline configured`, not an online AL state. No shared accessibility API,
custom model role, or second data source is introduced.

The earlier screen-bound Topology contract continues to own preferred size,
`ResizeToContents`, `ElideNone`, horizontal scrolling, available-screen
geometry, centering, modal stack lifetime, and Close behavior. This issue
changes none of those paths, nor slave ordering, Project mutation, Undo/Redo,
persistence, empty-topology behavior, or selection.

Qt documents the standard string roles and role-specific item storage at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qtreewidgetitem.html#setData>. Beckhoff documents that
its selected-master Topology can present both configured and online data:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1277974411.html>.
Embed Labs deliberately retains only the current offline Project presentation.

This boundary adds no public or custom role, API, source file, dependency,
Provider, Project command, persistence field, production thread or timer,
network/controller transport, online state, physical-port model, hardware
behavior, CMake/qbs entry, or path under upstream Core, ProjectExplorer, or
the application bootstrap. The Workbench path count remains 44 and the direct
upstream Core patch count remains five.

## Workbench repository Device DC mode-preview boundary

`ISSUE-WB-DC-REPOSITORY-MODE-PREVIEW-001` remains entirely inside the
product-owned private `DcPage`. `DeviceRepositoryProvider` continues to own
and publish the immutable ESI description and its parsed DC modes. The page
copies those values into its existing `m_esiModes` and presentation-only
`m_configuration`; no new value or object crosses a plugin boundary.

The operation-mode selector is enabled for preview only when the current
`PropertyPageContext` is a repository `Device` and the current ESI snapshot
contains a mode. Its line editor remains read-only, and every field that could
imply configuration remains disabled or read-only. A user activation copies
`dcConfigurationFromMode()` into the current page value and rebuilds the
visible fields. The Device branch returns before `submitConfiguration()`, so
it cannot discover or call `ProjectService`, create Undo history, persist a
selection, or issue controller/network/hardware work. Page/context teardown
destroys that transient selection; a recreated Device page derives the first
mode again from the immutable snapshot.

Configured-slave ownership is unchanged. A valid configured slave remains the
only editable context, preserves the potential-reference-clock value when an
ESI mode is selected, validates the candidate, and routes it through the
existing public checked Project command. Invalid Project and unrelated
contexts do not acquire repository preview permission.

Qt's `QComboBox::activated()` signal supplies the user-interaction boundary,
and the existing QObject/page lifetime remains the reset boundary:
<https://doc.qt.io/qt-6/qcombobox.html>. Qt's localized widget description
contract is used only to disclose the preview behavior:
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Beckhoff's
Distributed Clock page remains only the field and operation-mode comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, thread, timer, controller/network
transport, online state, or physical-hardware behavior. No CMake or qbs
description changed. No path under upstream Core, ProjectExplorer, or the
application bootstrap changed. The Workbench path count remains 44 and the
direct upstream Core patch count remains five.

## Workbench repository Device DC empty-state boundary

`ISSUE-WB-DC-REPOSITORY-EMPTY-001` remains entirely inside the product-owned
private `DcPage`. `DeviceRepositoryProvider` continues to own the immutable ESI
description and parsed `dcModes`; the page derives the empty presentation from
the existing copied list only while private availability and support bits
confirm whether the repository description still exists and may enter the
offline topology. No availability/support state, synthetic mode, configuration
object, or ownership relationship crosses a plugin boundary.

A repository Device with no parsed modes keeps the selector at zero items and
index `-1`, disabled, and read-only. The visible summary, informational status,
localized selector description, and tooltip now agree that no ESI Distributed
Clocks operation mode is available. They disclose that manual timing belongs
in an offline Project and that the repository page does not access a
controller, network, or physical hardware. The page does not convert the empty
state into a validation error or an online-capability claim.

A zero-mode Device whose ESI description contains unsupported structures stays
separate from that supported recovery. The page shows a warning, states that
the existing topology gate will not add it to an offline Project, and returns
the user to Device Repository support details. It does not pretend that manual
Project entry is available.

An unsupported Device with parsed modes retains local read-only preview but
does not inherit the supported Project-add instruction. The summary, selector
accessibility text, and tooltip disclose the topology rejection and Device
Repository recovery. With a valid mode, validation shows the support warning;
with an invalid mode, it preserves the configuration Error or Warning and
appends the same recovery notice. Preview never grants Project mutation.

A removed or unavailable ESI description is kept separate from a valid Device
with zero modes. That error state shows a warning and Device Repository
recovery, never an offline Project add instruction. It grants the same zero
permissions and does not create a placeholder repository object.

Repository-empty state grants no permission. Every configuration control
remains disabled or read-only, `selectEsiMode()` has no item to activate, and
the Device context cannot reach `submitConfiguration()` or
`setDcConfiguration()`; the read-only Project lookup in `setContext()` cannot
produce a ProjectService mutation or Undo command.
Page teardown remains the complete presentation-lifetime boundary. A valid
configured slave remains the only editable context and continues to own manual
entry, validation, checked Project commands, persistence, and Undo/Redo.

Qt specifies that an empty `QComboBox` has count zero and current index `-1`:
<https://doc.qt.io/qt-6/qcombobox.html>. Qt Creator 20.0's
Type Hierarchy widget supplies the host precedent for an explicit unavailable
state:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.
Beckhoff's Distributed Clock page remains only the field and operation-mode
comparison and describes selection when modes are offered:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, thread, timer, controller/network
transport, online state, or physical-hardware behavior. No CMake or qbs
description changed. No path under upstream Core, ProjectExplorer, or the
application bootstrap changed. The Workbench path count remains 44 and the
direct upstream Core patch count remains five.

## Workbench repository Device Process Data empty-state boundary

`ISSUE-WB-PROCESS-DATA-REPOSITORY-EMPTY-001` remains inside the
product-owned private `ProcessDataPage`. `DeviceRepositoryProvider` continues
to own and publish the immutable parsed `DeviceDescription`. The page copies
the existing ESI defaults and derives four private presentation facts only:
description availability, parser support, parsed PDO availability, and whether
the derived mapping has validation errors. None is added to a public interface,
Provider contract, shared data type, or persistence format.

The repository Device branch remains read-only. Its five existing models use
their real empty or populated data; no Sync Manager, PDO, entry, stable ID, or
placeholder row is synthesized. The base accessible description of each table
is stored once when the private widget is configured. Every `setContext()`
rebuilds contextual description and tooltip text from that base, which makes
page reuse a complete cleanup boundary for empty, unsupported, invalid,
removed, and valid states.

Supported empty mappings and valid mappings remain distinct. The empty state
may enter the existing offline topology with an empty configuration, but the
repository page neither performs that Add nor grants edit permission. Invalid
Process Data cannot enter the checked Project path even when the ESI parser
marked the description structurally supported; the page therefore points to a
corrected ESI import instead of advertising Add. Unsupported and removed
descriptions retain their existing topology rejection and Device Repository
recovery.

The validation label continues to present the existing
`ConfigurationValidation` output. The underlying validator, issue codes,
problem severities, and Project validation rules do not change. Repository-page
presentation is intentionally more explicit: empty supported mappings use
Information, unsupported valid previews use Warning, valid supported mappings
use Ok, and any configuration error remains Error. Recovery text is appended;
the first detail stays in the label and the complete existing issue list stays
in its tooltip.

The regression's Project operations prove the wording but do not add a new
production path. A real supported empty Device is passed to the existing
`WorkbenchController::addDeviceToMaster()` and existing Project Undo command.
Unsupported and invalid inputs are passed to the same checked boundary and are
rejected. `ProcessDataPage` itself never discovers an active Master, calls the
controller Add path, submits configuration, creates Undo history, or persists
repository selection.

Beckhoff's Process Data documentation supplies the SM/PDO presentation
comparison but includes download and controller state-transition capabilities
that remain excluded here:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.
Qt's `QAbstractItemModel` and `QWidget::accessibleDescription` contracts supply
the zero-row, rejected-mutation, and contextual accessibility boundaries:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#rowCount>,
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>, and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, thread, timer, controller/network
transport, online state, or physical-hardware behavior. No CMake or qbs
description changed. No path under upstream Core, ProjectExplorer, or the
application bootstrap changed. The Workbench path count remains 44 and the
direct upstream Core patch count remains five.

## Workbench repository Device Startup empty-state boundary

`ISSUE-WB-STARTUP-REPOSITORY-EMPTY-001` stays inside the product-owned private
`StartupPage`. `DeviceRepositoryProvider` continues to own and publish the
immutable parsed `DeviceDescription`; the page copies the existing Startup
defaults and derives four private presentation facts only: description
availability, parser support, parsed-request availability, and whether the
current copied configuration has validation errors. None crosses a plugin
interface or enters a shared data or persistence contract.

The repository Device branch remains a view. Empty and populated models expose
their real copied parameters; no request, object address, raw value, order,
stable ID, or placeholder row is created. The table's localized base accessible
description is stored once by the private widget. Each `setContext()` rebuilds
contextual description and tooltip text from that base, making page reuse the
cleanup boundary for supported/unsupported empty, supported/unsupported
invalid, removed, and populated-valid states. Validation details are rebuilt
from the current configuration and cannot survive a removed or later valid
context.

Supported empty and invalid Startup are deliberately different. A supported
empty Device may enter the existing offline topology with an empty Startup
list; the repository page neither performs that Add nor enables an editing
control. A supported-invalid Device remains rejected by the existing Project
validation boundary and points to corrected ESI import. Unsupported empty,
populated, and invalid Devices retain the existing topology rejection and
Device Repository support recovery. A removed description cannot advertise an
offline Project path.

The validation label preserves `validateStartupConfiguration()` output and
does not alter issue codes, severities, messages, or Project validation.
Supported empty uses Information; unsupported empty uses Warning; invalid
preview remains Error; unsupported populated preview adds the support warning
without hiding existing diagnostics. A supported populated catalogue retains
its existing Warning state; the qualified three-request fixture preserves all
three source data-type warnings. The first error remains visible while the full
existing issue list remains in the validation tooltip.

Repository mutation has two independent private gates. `m_editable` keeps New,
Edit, Delete, Move Up/Down, enable, and Store/Restore unavailable; the table
model continues to reject direct EditRole and CheckStateRole mutation. The
contextual description states that selection affects presentation only and no
Project, SDO, controller, network, or physical hardware is touched.

The regression's recovery operations prove wording at the existing boundary;
they do not add a production path. It invokes the existing
`WorkbenchController::addDeviceToMaster()` and Project Undo command after the
repository presentation checks. `StartupPage` itself never finds an active
Master, adds a Slave, creates Undo history, persists repository selection, or
sends a request. A test-local scope guard owns the opened Project and cleans it
on every return path; it adds no production ownership or lifecycle object.

Beckhoff's Startup documentation supplies only the ordered-request/field
comparison and includes mailbox execution that remains excluded:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.
Qt's model-cardinality, rejected-mutation, and localized contextual-description
contracts define the view boundary:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#rowCount>,
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>, and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, thread, timer, controller/network
transport, SDO execution, online state, or physical-hardware behavior.
Configured-slave editing, validation, Project submission, persistence, and
Undo/Redo remain the existing path. No CMake or qbs description changed. No
path under upstream Core, ProjectExplorer, or the application bootstrap
changed. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

## Workbench repository Device EtherCAT empty-state boundary

`ISSUE-WB-ETHERCAT-REPOSITORY-EMPTY-001` stays inside the product-owned
private `EtherCATPage`. `DeviceRepositoryProvider` continues to own and
publish the immutable parsed `DeviceDescription`; the page reads the existing
SyncManager list and parser support flag only. Neither fact crosses a plugin
interface or enters a Project or persistence contract.

The repository Device branch has five presentation states: supported empty,
unsupported empty, supported populated, unsupported populated, and an
unresolved/missing description. Empty and unresolved states use an explicit
summary, keep a real zero row count, and hide the table rather than showing a
header-only view. Populated states retain the existing seven-column read-only
ESI table. An unsupported Device remains inspectable but cannot gain topology
permission; its recovery stays in Device Repository.

Summary and tree metadata are rebuilt from the current context on every
update. The summary's localized accessible description and tooltip equal its
visible text. The tree's localized description and tooltip identify the
read-only offline ESI source plus the Project/controller/network/hardware
boundary. Reset clears rows and metadata before the new state is applied, and
empty states hide the table, so one private page instance cannot leak a prior
unsupported, unavailable, or empty visible state into the next Device.

The page does not synthesize a SyncManager, validate or add a Device, discover
a target, create Project history, or access a controller. Configured-slave
Alias editing remains the existing Project-owned path and is outside this
repository-only issue. The regression begins with no Project and keeps that
list empty while all repository contexts are browsed.

Beckhoff supplies only the structural comparison that an EtherCAT slave page
lists its Sync Manager configuration:
<https://infosys.beckhoff.com/content/1033/tcsystemmanager/1092536331.html>.
Qt supplies the real row-cardinality and contextual accessibility contracts:
<https://doc.qt.io/qt-6/qtreewidget.html#topLevelItemCount-prop> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0's Type Hierarchy unavailable label is the nearest host-side empty-state
precedent:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, thread, timer, controller/network
transport, online state, or physical-hardware behavior. No CMake or qbs
description changed. No path under upstream Core, ProjectExplorer, or the
application bootstrap changed. The Workbench path count remains 44 and the
direct upstream Core patch count remains five.

## Workbench CoE modal refresh boundary

`ISSUE-WB-COE-MODAL-REFRESH-001` remains inside the product-owned private
`CoeOnlinePage`. Repository `devicesChanged` handling continues through the
existing Details context path; no Provider API, repository revision, or
cross-plugin lifecycle contract is introduced.

Every `setContext()` advances a page-local generation. Advanced Settings and
Add to Startup use page-owned, delete-on-close dialogs opened asynchronously.
Their completion receivers are the page itself, so deleting the page also
deletes the dialog and disconnects its pending completion. A completion from
an older generation is ignored. Same-identity ESI refresh does not force-close
the dialog; it invalidates only the result that was based on the old context.

Advanced reads its controls only for an accepted current generation. Add
captures scalar object data plus Project/slave identifiers before showing its
confirmation, then re-queries the current Project snapshot after a
current-generation Yes response. It never carries a `QModelIndex` across the
asynchronous boundary. Existing Project validation, command submission,
persistence, Undo/Redo, and local Mock ownership are unchanged.

Qt documents the nested-event-loop and parent-lifetime risks of
`QDialog::exec()` and recommends asynchronous `open()`:
<https://doc.qt.io/qt-6/qdialog.html#exec>. Qt Creator 20.0's font settings
dialog is the host-side heap-owned, delete-on-close precedent:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L532-L539>.
Beckhoff documents the comparable CoE source/range/filter vocabulary only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446522251.html>.

This boundary adds no public API, source file, dependency, Provider,
persistence field, Project command, custom model role, production thread or
timer, controller/network transport, online state, SDO execution, or hardware
behavior. No CMake or qbs description changed. No path under upstream Core,
ProjectExplorer, or the application bootstrap changed. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

## Workbench Startup modal refresh boundary

`ISSUE-WB-STARTUP-MODAL-REFRESH-001` remains inside the product-owned private
`StartupPage`. `ProjectService` continues to own Project snapshots, validation,
Undo/Redo, persistence, and `projectChanged`; the page adds no revision token
or cross-plugin lifecycle API.

Every page context assignment increments a private generation. New, Edit, and
Delete use page-owned delete-on-close dialogs and page-bound completion
connections. The page and dialog are destroyed together when Details removes
the page, and QObject connection ownership prevents a completion from running
after page destruction. A same-node Project refresh may leave the dialog
visible, but it invalidates the response derived from the previous context.

A current response re-queries the Project and slave before mutation. New
appends a fresh stable request ID and recomputes order. Edit and Delete find the
captured request ID in the current effective Startup configuration and recheck
the fixed-request rule. Delete derives its next selection from the current
ordered list. The only page state reused is the existing effective ESI-default
preview for a current generation; this preserves the established behavior in
which editing displayed defaults stores them through the Project command path.

No `QModelIndex`, table row, request pointer, Project snapshot reference, or
configuration reference crosses the asynchronous boundary. No dialog owns a
Project transaction, and no page-side retry or merge framework is introduced.
Stale acceptance is a no-op; current acceptance still enters the existing
validated Project command and Undo/Redo history.

Qt documents the nested-event-loop and parent-lifetime risks of
`QDialog::exec()` and recommends asynchronous `open()`:
<https://doc.qt.io/qt-6/qdialog.html#exec>. The static QMessageBox parent
warning is documented at
<https://doc.qt.io/qt-6/qmessagebox.html#question>. Qt Creator 20.0 provides
the heap-owned, delete-on-close host precedent:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L532-L539>.
Beckhoff defines the comparable ordered Startup request and fixed-entry
vocabulary only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, production thread or timer,
controller/network transport, online state, SDO execution, or hardware
behavior. No CMake or qbs description changed. No path under upstream Core,
ProjectExplorer, or the application bootstrap changed. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

## Workbench configured-slave tree order boundary

`ISSUE-WB-TREE-PHYSICAL-ORDER-001` remains inside the product-owned private
`WorkbenchTreeModel`. `ProjectService` continues to own the authoritative
`ProjectSnapshot`, configured-slave positions, validation, persistence,
Undo/Redo, and project change notification. The Workbench layer only projects
that immutable state into its navigation tree.

During a rebuild, the model creates a private ID-to-position lookup from the
current snapshot. A sibling set uses the physical-position comparator only if
the entire set consists of Slave nodes with matching offline configurations.
That comparator orders by position, case-insensitive name, and stable `NodeId`.
Any incomplete set uses the original case-insensitive name comparator for the
whole set. These are two complete comparison modes, so there is no pairwise
mix of precedence and the sort retains a strict weak ordering.

The existing reset lifecycle, node identities, and navigation proxy remain the
only update path. Stable IDs restore selection after reset, and filtering
preserves the source model's physical order. Move Up and Move Down continue to
enter the existing checked Project command path; the tree does not mutate a
position, add a command, or own Undo/Redo.

Beckhoff's Auto Increment address description provides the physical-ring
semantics used for the UI projection:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html>.
Qt documents model reset at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel> and proxy
source-model behavior at
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html>. Qt Creator 20.0's Project
tree comparator likewise places semantic priority ahead of name:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L88-L103>.

This boundary adds no public API, source file, dependency, Provider, Project
command, persistence field, model role, production thread or timer,
controller/network transport, online state, scan, SDO execution, or hardware
behavior. No CMake or qbs description changed. No path under upstream Core,
ProjectExplorer, or the application bootstrap changed. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

## Workbench topology dialog page-lifecycle boundary

`ISSUE-WB-TOPOLOGY-DIALOG-PAGE-LIFECYCLE-001`, based on
`e814edf65c95ef3c5701f7655c1ff0237cdeac26`, remains entirely inside the
product-owned private `EtherCATPage`. The page allocates its read-only
Topology dialog on the heap, parents it to itself, enables
`Qt::WA_DeleteOnClose`, stores only a `QPointer<QDialog>`, and calls `open()`.
The `finished` callback clears that pointer only while it still identifies the
same dialog. This makes Close/Escape and immediate reopen safe across deferred
deletion, and page destruction retains normal QObject child cleanup.

`setContext()` is the sole freshness boundary: every EtherCAT page context
refresh closes the current snapshot before applying the new context. This
includes same-master Project refreshes and repository/index refreshes. It is
conservative invalidation, not a live table update, and it adds no result,
apply, generation, ProjectService, or Provider contract. Repeat activation
raises the existing snapshot. Selection change, project close, and page
teardown destroy it without a nested event loop.

Qt explicitly recommends asynchronous `open()` over the nested event loop of
`exec()` and warns about parent deletion during `exec()`:
<https://doc.qt.io/qt-6/qdialog.html#exec>. QObject ownership is documented at
<https://doc.qt.io/qt-6/objecttrees.html>, and Qt Creator 20.0 provides the
matching page-owned, delete-on-close precedent:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L527-L540>.
Beckhoff's selected-master Topology page and its offline/online distinction
remain the product comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1277974411.html>.

Earlier bounds and cell-accessibility documentation described the Topology
dialog as stack-local or modal. This section supersedes only those lifetime
statements. The ten-column schema, screen-bounded geometry, accessible cell
names, configured physical order, Close/Escape behavior, and offline/Mock
values remain unchanged and qualified.

This boundary changes only `ethercatpage.cpp/.h`, Workbench tests, and the
four evidence documents. It adds no public API, source file, dependency,
Provider or ProjectService revision, persistence field, Project command,
model role, production thread or timer, Core or ProjectExplorer hook,
application-bootstrap path, network transport, online topology, port graph,
CRC/state control, scan, SDO execution, or hardware behavior. No CMake or qbs
description changed. All runtime evidence is local/offline Mock evidence; the
full product lifecycle was qualified offscreen with the plugin enabled and
explicitly disabled, without a visible main window or new crash record.

## Workbench stale offline-slave removal boundary

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-PROJECT-REFRESH-001`, based on
`312e1a9dbacfd621615c87d644b7a46bb5f5a64e`, remains entirely inside the
product-owned private `WorkbenchController`. Its asynchronous question
candidate owns a value snapshot of the complete
`OfflineSlaveConfiguration`; it owns no Project reference, model index,
widget pointer, or transaction.

After Yes, the controller re-resolves the current Project, Master, Slave, and
selection using their stable IDs. It exact-compares the current target against
the captured value before using the existing checked replacement command.
The comparison includes ID, Master, physical position, identity, serial,
Alias, name, device-description ID, Process Data, Startup, DC, and their
nested values. A target mismatch is an explicit no-op that requires a new
confirmation. An unrelated Project or sibling change is outside the compared
target and does not invalidate the response while the target value remains
unchanged.

This is deliberately not a Project revision, generation counter, CAS,
transaction, lock, retry, merge, or live-dialog refresh framework.
`ProjectService` remains authoritative for validation, persistence,
notifications, and Undo/Redo. Current confirmation still enters the existing
remove path, while stale confirmation cannot delete a refreshed
configuration. Escape remains a no-op, and delete-on-close ownership remains
inside the existing Workbench question flow.

Qt's asynchronous dialog and QMessageBox contracts are documented at
<https://doc.qt.io/qt-6/qdialog.html> and
<https://doc.qt.io/qt-6/qmessagebox.html>. Qt Creator's Project settings pages
provide the local host pattern, with official source at
<https://github.com/qt-creator/qt-creator/tree/v20.0.0/src/plugins/projectexplorer>.
Beckhoff documents the destructive Remove operation at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.

The boundary changes only `workbenchcontroller.cpp/.h`, the Workbench test
declaration/implementation, and four evidence documents. It adds no public
API, source file, dependency, Provider or ProjectService contract, Project
format, persistence field, Project command, custom model role, production
thread or timer, Core or ProjectExplorer hook, application-bootstrap path,
network transport, scan, online state, SDO execution, or hardware behavior.
No CMake or qbs description changed. Qualification is local/offline Mock and
the product lifecycle was exercised offscreen with the plugin enabled and
explicitly disabled, without a visible main window or new crash record.

## Workbench Add New Item dialog ownership boundary

`ISSUE-WB-INSERT-DIALOG-ASYNC-LIFECYCLE-001`, based on
`617505a08cb2055d6042a2580189182ffd39658a`, remains entirely inside the
product-owned private `WorkbenchModeWidget`. The mode owns at most one
`EsiDeviceSelectionDialog` through `QPointer`; it allocates the selector on the
heap, marks it delete-on-close, and opens it asynchronously. Repeat activation
raises the same instance. Completion clears only the matching retained
identity, and mode teardown owns ordinary QObject child destruction without a
nested event loop.

Project and device repositories remain authoritative. The dialog owns only
its presentation snapshot and stable target IDs. Existing signals reject it
when the active Project switches, the target Project closes or becomes
invalid, or the Master disappears. Accepted selection continues through the
existing controller revalidation and `ProjectService` command, while a guarded
controller makes shutdown completion a no-op. This is not a dialog manager,
Project revision, transaction, live repository refresh, or new service
contract.

Qt's asynchronous guidance and parent-deletion warning are documented at
<https://doc.qt.io/qt-6/qdialog.html>. Qt Creator 20.0's local ownership
precedent is at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L527-L540>.
Beckhoff's device-selection workflow does not prescribe the Qt ownership
mechanism.

The boundary changes only `workbenchmode.cpp`, the Workbench test
declaration/implementation, and four evidence documents. It adds no public
API, source file, dependency, Provider or ProjectService contract, Project
format, persistence field, Project command, custom model role, production
thread or timer, Core or ProjectExplorer hook, application-bootstrap path,
network transport, scan, online state, SDO execution, or hardware behavior.
No CMake or qbs description changed. Qualification is local/offline Mock and
the product lifecycle was exercised offscreen with the plugin enabled and
explicitly disabled, without a visible main window or new crash record.

## Workbench General draft-baseline boundary

`ISSUE-WB-GENERAL-NONCONFLICTING-REFRESH-DRAFT-001`, based on
`2fe30622dd971c9c3afd91e78e5addb2fa988340`, remains entirely inside the
product-owned private `GeneralPage`. `DetailsView` continues to route
`devicesReset`, `devicesChanged`, and `indexingChanged` into page refreshes;
the Device Repository provider, Project provider, and every other property
page retain their existing freshness behavior.

General owns a single last-loaded name baseline keyed by stable Project ID,
node ID, and node kind. On a same-node refresh it compares that baseline with
the current authority from `ProjectService` or the configured offline Slave.
Only an editable editor that is modified or focused, with unchanged authority,
is left in place. Focus is a conservative guard against disturbing a possible
input-method preedit before `modified` changes; the regression proves the
clean-focused gate rather than synthesizing platform IME composition. Its
parent remains visible and that editor is not cleared or passed to `setText()`;
the rest of the page still resets and repopulates. This preserves the actual
QLineEdit draft state while allowing ESI-derived read-only fields to refresh.

`ProjectService` remains the sole authority for Project and structural names,
persistence, signals, and Undo/Redo. The existing controller command remains
the authority for configured-Slave rename validation and topology
replacement. A changed authoritative name always replaces the draft. Explicit
commit first marks the editor clean and holds a scoped force-authority guard
around the existing command, its synchronous signals, and the final context
reload. Rejected empty input and trimmed no-ops therefore converge on the
persisted value despite focus. A context switch, invalid/unavailable project,
project close, or different stable ID cannot reuse the old baseline.

This mechanism is deliberately not a cross-page dirty-form service, autosave,
cross-node draft cache, Project revision, generation counter, CAS, merge,
conflict UI, repository refresh-reason API, or Details signal filter. It adds
no public type and no state to a Project file. The earlier project-scoped
Details filter still prevents an unrelated Project from refreshing the page;
this new boundary only narrows same-page refresh behavior when the editable
name itself has no conflict.

Qt's QLineEdit contract explains why avoiding `setText()` matters: it resets
modified state and clears selection and Undo/Redo state:
<https://doc.qt.io/qt-6/qlineedit.html#text-prop>. Qt Creator 20.0's
`BaseAspect` separates persisted value from volatile edit value and exposes
`isDirty()`:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/aspects.h#L328-L370>.
Beckhoff's General tab supplies the comparable Name/Id/Object Id/Type product
surface, not the private Qt conflict policy:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html>.

The boundary changes only `generalpage.cpp/.h`, the Workbench test
declaration/implementation, and four evidence documents. It adds no public
API, source file, dependency, Provider or ProjectService contract, Project
format, persistence field, Project command, custom model role, production
thread or timer, Core or ProjectExplorer hook, application-bootstrap path,
network transport, scan, online state, SDO execution, or hardware behavior.
No CMake or qbs description changed. Qualification is local/offline Mock and
the full product lifecycle was exercised offscreen with Workbench enabled and
explicitly disabled, using passed-through SIGTERM and producing no visible
main window or new crash record.

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

## Workbench ESI revision-selection boundary

`ISSUE-WB-ESI-REVISION-TOGGLE-SELECTION-001`, based on
`42110b4e990baae92fbed37f38ab6aa274b34657`, remains entirely inside the
product-owned private `EsiDeviceSelectionDialog`. The ESI repository remains
the authority for device summaries and stable IDs; the Workbench controller
remains the authority for appending the accepted device to the selected
offline Master.

The dialog's revision proxy still decides which rows are visible. Immediately
before `Show Previous Revisions` changes that filter, the dialog records the
current device ID. Its existing selection helper first looks for that ID in
the refreshed proxy and restores it if visible, then applies the established
first-supported/first-visible fallback. This is a local interaction-state
rule. It does not retain an index across a model reset, cache selections
between dialogs, alter revision comparison, or override filtering when the
selected revision becomes hidden.

Qt's selection model makes the current index the item used for navigation and
focus, while Beckhoff's offline workflow makes explicit revision selection
part of the item that is appended:
<https://doc.qt.io/qt-6/qitemselectionmodel.html> and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.
The implementation therefore preserves stable device identity across the
visibility-only toggle instead of relying on the proxy row number.

The boundary changes only `esideviceselectiondialog.cpp/.h`, the Workbench
test declaration/implementation, and four evidence documents. It adds no
public API, source file, dependency, model role, Provider or ProjectService
contract, persistence or Project command, Core or ProjectExplorer hook,
application bootstrap, thread, timer, network, scan, online, SDO, or hardware
behavior. No CMake or qbs description changed. Qualification is local/offline
Mock and includes full offscreen enabled/disabled product lifecycle evidence
with passed-through SIGTERM and no new crash record.

## Workbench removal-question invalidation boundary

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-CONFIRM-INVALIDATION-001`, based on
`532517066a612c4f5220484cadb830d0c07256c0`, remains entirely within the
product-owned private `EtherCATWorkbench` action layer. The removal question
captures copied Project, Master, and slave IDs plus the complete
`OfflineSlaveConfiguration`. Its lifetime is now subscribed to the existing
`SelectionService::currentNodeChanged`,
`ProjectService::projectAboutToBeRemoved`, and
`ProjectService::projectChanged` signals, with the question as the QObject
connection context and a guarded pointer as the close target.

Selection drift and target-Project removal reject immediately. A change to
the captured Project asks the existing Workbench controller for a fresh
removal candidate and keeps the question only when the same stable Project,
Master, slave, and complete configuration are still current. This comparison
keeps only sibling changes that leave the captured candidate unchanged
non-conflicting. Changes to an unrelated Project are ignored. The signal path
only shortens a stale dialog's lifetime; it does not submit, replace, retarget,
or merge a Project command.

The existing affirmative path still revalidates the complete candidate before
calling `ProjectService::replaceOfflineSlaves()`. EtherCATProject therefore
continues to own validation, modified state, persistence, position
normalization, Undo, and Redo. Workbench continues to own only presentation,
stable-context validation, and post-command selection repair. No QObject,
Project object, Provider pointer, `QModelIndex`, or mutable model data crosses
a plugin boundary.

The change adds no public service or data type, source file, dependency,
metadata, persistent format, model role, Provider, Project command,
production thread or timer, Core or ProjectExplorer hook,
application-bootstrap change, controller/network transport, online state,
scan, SDO execution, or hardware behavior. No CMake or qbs entry changed. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five. Qualification uses local/offline Mock state and offscreen
enabled/disabled product lifecycle runs only.

## Workbench Process Data refresh-selection boundary

`ISSUE-WB-PROCESS-DATA-SAME-CONTEXT-SELECTION-001`, based on
`f4da0b18cc48077374ba2528091cdb3aeaad99f5`, remains entirely inside the
product-owned private `ProcessDataPage`. `DetailsView` continues to route
Device Repository and current-Project changes into page refreshes. Repository
and Project providers remain authoritative for their data and signals.

Before replacing its context snapshot, the page compares stable Project ID,
node ID, and node kind. Only the same stable context retains the selected Sync
Manager and PDO IDs. The existing rebuild resolves those IDs in the new models
and falls back to the first available rows when an ID disappeared. If the PDO
still exists but its owning Sync Manager changed, the fresh PDO relationship
selects that owner. A context switch clears the IDs, after which existing
PDO/PDO-entry derived-node focus runs. That focus is not reapplied on a refresh
of the same derived context, so it cannot override the user's later manual
selection. No `QModelIndex`, row number, repository object, or mutable Project
snapshot is retained across refresh.

Qt's selection model identifies the current item used for keyboard navigation
and focus indication, while Beckhoff's Process Data surface makes the selected
Sync Manager the owner of the displayed PDO Assignment:
<https://doc.qt.io/qt-6/qitemselectionmodel.html> and
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10834607243.html>.
This private identity rule does not establish a generic selection service,
cross-node cache, persistent-index contract, repository signal filter,
refresh-reason API, Project revision, merge, or conflict protocol.

The boundary changes only `processdatapage.cpp`, the existing Workbench test
implementation, and four evidence documents. It adds no public API, source
file, dependency, model role, Provider or ProjectService contract, Project
format, persistence field, Project command, production thread or timer, Core
or ProjectExplorer hook, application-bootstrap path, network transport, scan,
online state, SDO execution, or hardware behavior. No CMake or qbs description
changed. Qualification is local/offline Mock and includes offscreen enabled
and explicitly disabled product lifecycle evidence with no visible main window
or new crash record. Authoritative evidence is under
`/private/tmp/embed-labs-wb-process-data-selection-refresh-001.jPkuu0/final2`;
the two red proofs are under the sibling `failure-first/` and
`derived-failure/` directories.

## Workbench EtherCAT Alias draft-baseline boundary

`ISSUE-WB-ETHERCAT-ALIAS-NONCONFLICTING-REFRESH-DRAFT-001`, based on
`8c374cb68b2ea5844c6ca5a51cf31ed8b2aa14a1`, remains entirely inside the
product-owned private `EtherCATPage`. `DetailsView` continues to route Device
Repository and current-Project changes into page refreshes; Repository and
Project providers remain authoritative for the values and signals they own.

Before replacing its context snapshot, the page compares the stable Project
ID, node ID, and node kind and checks the new authoritative Alias against its
last baseline. Only a modified or focused Alias editor in that unchanged
context is left untouched. The rest of the EtherCAT surface refreshes from
fresh data. The page retains no cross-plugin QObject, Project object,
`QModelIndex`, mutable Repository entry, or Project snapshot. Its private
spin-box subclass only exposes the widget-owned line editor needed to preserve
text, focus, selection, cursor, and local editor Undo/Redo state.

ProjectService and EtherCATProject continue to own Alias validation,
persistence, Project modified state, Undo, and Redo. A scoped force-authority
path surrounds explicit commits, including synchronous refresh, rejection,
and no-op normalization. External Alias changes, Project Undo, context
switches, invalid contexts, and Project close therefore always replace or
destroy the draft from current authority. This is not a generic dirty-form,
autosave, cross-node cache, merge/conflict, Project revision, generation, or
CAS protocol.

The boundary changes only private `ethercatpage.cpp/.h`, the existing
Workbench test declaration/implementation, and four evidence documents. It
adds no public API, source file, dependency, model role, Project format,
persistence field, Provider or ProjectService contract, Project command,
production thread or timer, Core or ProjectExplorer hook,
application-bootstrap path, network transport, scan, online state, ADS, SDO
execution, ESC/EEPROM write, or hardware behavior. No CMake or qbs description
changed. Qualification is local/offline Mock and includes offscreen enabled
and explicitly disabled product lifecycle evidence with no visible main
window or new crash record. Authoritative evidence is under
`/private/tmp/embed-labs-wb-alias-draft-refresh-001.F7RkQj/final`; the red
proof is under the sibling `failure-first/` directory.

## Workbench keyboard context-event boundary

`ISSUE-WB-NAV-KEYBOARD-CONTEXT-TARGET-001`, based on
`38d0d262f972b6bd1f877b6a2306411a1240cbf7`, remains entirely inside the
product-owned private `WorkbenchNavigationWidget`. The widget filters the
original tree/viewport `QContextMenuEvent` only to retain its Mouse versus
Keyboard/Other reason. Mouse requests still resolve a proxy index at the
requested viewport point and publish that stable NodeId through the existing
`SelectionService`. Non-mouse requests do not write Selection; they use the
current proxy index solely to build the existing ActionManager-backed menu.

The keyboard anchor is derived from `QTreeView::visualRect()` intersected with
the viewport. An empty intersection uses the viewport center. No index, event,
menu, or QObject crosses a plugin boundary, and no row or coordinate is
persisted. Consuming the real event prevents duplicate menu delivery; the
existing custom-signal connection remains a local compatibility path. Command
registration, enablement authority, Project mutation, Details routing, and
selection cleanup remain unchanged.

Qt's reason/position and viewport contracts are at
<https://doc.qt.io/qt-6/qcontextmenuevent.html> and
<https://doc.qt.io/qt-6/qabstractscrollarea.html>. The result matches the
selected-object keyboard interaction described by Beckhoff without importing
its formats, assets, ADS stack, or hardware behavior.

The boundary changes only private `workbenchnavigation.cpp/.h`, the existing
Workbench test declaration/implementation, and four evidence documents. It
adds no public API, source file, dependency, model role, persistence field,
Provider or ProjectService contract, Project command, Core or ProjectExplorer
hook, application-bootstrap change, production thread or timer, network,
scan, online, SDO, or hardware behavior. No CMake or qbs entry changed.
Qualification is local/offline Mock and includes normal/2x offscreen tests
plus enabled/disabled product lifecycle evidence with no visible main window
or new crash record. Evidence is under
`/private/tmp/embed-labs-wb-nav-keyboard-context-target-001.Du2VdU`.

## Workbench preferred Diagnostics status boundary

`ISSUE-WB-STATUS-PREFERRED-DIAGNOSTICS-001`, based on
`0524d2dfbb14cacd6f25c584ede4bcce9fe416c4`, remains inside the product-owned
private Workbench controller and status widget. `WorkbenchController` uses the
existing deterministic optional-Provider selection and copies only the
preferred Diagnostics presentation, stream/request, run mode, Master state,
Mock flag, Master error, and active-alarm count into the value-only
`DiagnosticsStatusPresentation`. The status widget retains no Provider
pointer, model index, sample series, mutable snapshot, or transport object.

The dedicated `diagnosticsStatusChanged` signal refreshes only status
presentation. Periodic semantic changes are not misrepresented as Provider
availability or Details-page lifecycle changes. Provider registry ownership,
object-pool add/remove ordering, producer sampling, and optional-plugin
selection remain unchanged. Qt Creator's object-pool lifetime contract is
documented at <https://doc.qt.io/qtcreator-extending/pluginmanager.html>.

`WorkbenchStatusWidget` merges the copied Provider severity with the shared
`StateService`; the highest severity wins, while a healthy running Provider
supplies the mode-specific Ready text. At equal non-Ready severity, a non-Mock
Provider keeps neutral `Diagnostics` wording; a strictly higher StateService
entry remains authoritative. Plugin destruction still deletes the
status widget before its controller. Local Mock values are explicit, and a
non-Mock value is described only as Provider-reported, never as an inferred
controller or hardware connection.

The boundary changes only seven existing private Workbench
implementation/test files and four evidence documents. It adds no public API,
source file, dependency, Provider/ProjectService contract, Project command,
model role, persistence field, thread, timer, Core or ProjectExplorer hook,
application-bootstrap change, network transport, ADS, scan, online transition,
SDO execution, or hardware behavior. No CMake or qbs entry changed.
Qualification is local/offline Mock or explicitly Provider-reported data and
includes normal/2x offscreen tests plus enabled/disabled product lifecycle
evidence with no visible main window or new crash record. Evidence is under
`/private/tmp/embed-labs-wb-status-preferred-diagnostics-001.urIOBN`.

## Workbench DC field-authority draft-baseline boundary

`ISSUE-WB-DC-NONCONFLICTING-REFRESH-DRAFT-001`, based on local commit
`304eaf73d4020b59abba15116868bcb8f50cc9f8`, remains entirely inside the
product-owned private `DcPage`. `ProjectService` and EtherCATProject continue
to own validation, persistence, Project modified state, Undo, and Redo.

The page retains the stable Project ID, node ID, configured-slave kind, and
the last authoritative `DcConfiguration`. On each refresh it independently
compares Operation Mode name, AssignActivate, SYNC0 cycle/shift, and SYNC1
cycle/shift with that baseline before replacing the baseline with the fresh
snapshot. Only an active editor whose own authority is unchanged may retain
its widget state. The immediate enable and potential-reference controls are
always rendered from fresh authority.

The private `DraftFields` force mask covers explicit commit, no-op,
rejection, service failure, ESI mode selection, and Restore Defaults. It
controls only which widgets must reload; it does not create a public Project
revision, generation, CAS, merge, or conflict protocol. Synchronous
ProjectService signals cannot preserve a field that is being explicitly
committed because the force scope remains active through the complete service
call and authoritative reload.

When an Operation Mode draft defers a combo-model rebuild,
`m_visibleEsiModes` keeps page-owned value copies for the currently displayed
rows. A later selection maps the complete old `DcModeDescription` value to the
fresh Repository list. This is not a new stable ESI mode ID, and a removed or
changed value is rejected rather than guessed from a duplicate display name.
When rebuilding the combo, a unique mode name keeps the established
presentation. Multiple same-name rows are disambiguated by the Project's
complete mode-owned name, AssignActivate, and SYNC timing values; enable flags
and Potential Reference Clock are not members of `DcModeDescription`. With no
complete match, the Project name is shown as a custom value instead of
highlighting an unrelated same-name row.

The page retains no Project QObject, Repository object, `QModelIndex`, mutable
Repository entry, or cross-node draft. Repository Device DC pages remain
read-only and do not establish an editable baseline. Existing Details and
Repository signal routing is unchanged; there is no new thread, timer,
Provider, or service contract.

The boundary changes only private `dcpage.cpp/.h`, the existing Workbench test
declaration/implementation, and four evidence documents. It adds no public
API, source file, dependency, Project format, persistence field, Project
command, model role, Core or ProjectExplorer hook, application-bootstrap
change, network transport, ADS, scan, online transition, SDO execution,
ESC/EEPROM write, or hardware behavior. No CMake or qbs entry changed.
Qualification is local ESI/offline Project data plus offscreen product
lifecycle evidence. Authoritative evidence is under
`/private/tmp/embed-labs-wb-dc-draft-refresh-001.6ifOHJ`.

## Workbench CoE same-context view-state boundary

`ISSUE-WB-COE-SAME-CONTEXT-VIEW-STATE-001` remains inside the private
`CoeOnlinePage`. A non-`None` context is stable only when its Project ID, node
ID, and node kind all match the prior context. That gate lets the existing
page-owned widgets keep filter editor state, Advanced options, source choice,
and Mock sample generation while the page-owned scalar object address remains
the semantic selection anchor.

The anchor is a `quint32` value. The page retains no `QModelIndex`, model item,
Repository entry, Project QObject, or service object across rebuilds. Current
Project and ESI definitions still replace the source model on every context
update. An internal result-change guard prevents the model reset and proxy
fallback selection from replacing an address that still exists in the fresh
source model, even when the proxy currently hides it. If the address no longer
exists, the current valid fallback is adopted or the anchor is cleared.

A different Project, node ID, or node kind resets all page-owned CoE view
state, so this does not create a cross-node cache. Feedback is transient and
cleared on refresh; manually edited Mock values are rebuilt from current
definitions. The context generation continues to increment on every
`setContext()` call, including same-stable-context refresh, so stale Advanced
and Add-to-Startup dialog responses remain invalid.

This is not a generic view-state service, Repository signal suppression,
refresh-reason API, Project revision or generation contract, CAS or merge
protocol, persistence format, or Provider behavior. Existing Details routing,
ProjectService commands, Repository signals, public API, and model roles are
unchanged.

The delta changes private `coeonlinepage.cpp`, the existing Workbench test
declaration/implementation, and four evidence documents. It adds no header,
source file, dependency, source-list entry, CMake/qbs entry, Project format,
thread, timer, network, ADS, scan, online state, SDO, or hardware behavior.
`EtherCATWorkbenchPlugin` remains In progress. Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-view-state-001.4vssYQ`.

## Workbench Startup inline-editor draft-baseline boundary

`ISSUE-WB-STARTUP-NONCONFLICTING-REFRESH-DRAFT-001`, based on local commit
`a835d3340e0a3d644928adb445a8c916fda17e23`, remains entirely inside the
product-owned private `StartupPage`, its model, editor-tracking delegates, and
table view. EtherCATProject and `ProjectService` remain the authority for
validation, persistence, modified state, Undo, and Redo.

The page owns only value anchors: Project ID, node ID, node kind, stable
Startup request ID, edited column, and the last authoritative request value.
It retains no Project or Repository object, service object, model index, table
row, or request pointer across refreshes. A draft is eligible only in the same
editable configured-slave context while its own fresh field authority and
source state remain unchanged.

The private model uses unique non-null request IDs for granular row remove,
insert, move, and sibling-cell updates. It deliberately excludes the active
cell from refresh notifications until the editor closes, because Qt item views
may reload editor data regardless of the signal's role list. A page-private
commit scope handles synchronous Project refresh during inline submission.
Conflicts, read-only/source changes, removed requests, node changes, and
Project close discard the draft and render current authority.

Editor destruction remains table-owned during normal operation. The page
destructor first closes the active editor with revert semantics and then
synchronously releases any instance left queued by the delegate. No pending
text is submitted during page teardown, and no deferred editor outlives its
page. This is a private lifetime guard, not a general delegate policy.

The boundary changes only existing private `startuppage.cpp/.h`, the Workbench
test declaration/implementation, and four evidence documents. It adds no
generic table or draft service, public API, source file, dependency,
Provider/ProjectService contract, Project format, persistence field, Project
command, model role, Core or ProjectExplorer hook, application-bootstrap
change, thread, timer, network, ADS, scan, online state, SDO, or hardware
behavior. No CMake or qbs entry changed. Modules and Channels remain a
separate data/API chain. `EtherCATWorkbenchPlugin` remains In progress.
Authoritative evidence is under
`/private/tmp/embed-labs-wb-startup-draft-001.XNDmkO`.

## Workbench Process Data inline-editor authority boundary

`ISSUE-WB-PROCESS-DATA-NONCONFLICTING-REFRESH-DRAFT-001`, based on local
commit `31b4fb48bd564caa3ad5bd2a0947aa5e9800b38d`, remains entirely inside the
product-owned private `ProcessDataPage`, its PDO Content model, delegates, and
table view. EtherCATProject and `ProjectService` continue to own validation,
persistence, modified state, Undo, and Redo.

The page keeps value-only anchors for the current context, selected PDO,
stable entry ID, edited column, source state, and last authoritative entry.
It retains no Project or Repository object, service object, table row, or
model item as authority. The table view's internal persistent index continues
to follow an editor through granular sibling-row changes; the feature itself
does not expose a new persistent index or public model role.

Preservation requires the same Project, configured-slave node and kind,
selected unique non-null PDO ID, unique non-null entry ID, editable mapping,
unchanged field authority, and compatible stored/ESI-proposal source. Field
authority is Index, Subindex, bit length, requested bit offset, Name, or the
combined Type/raw-Type/bit-length value, according to the active column.
Other fields and sibling rows always update from the fresh configuration.

When the gate holds, the model applies `beginRemoveRows()`,
`beginInsertRows()`, and `beginMoveRows()` by stable entry ID instead of a
model reset. It excludes the active cell from `dataChanged()` during a
non-conflicting refresh because Qt's item view may call `setEditorData()` for
every covered open editor without filtering on the role list. Metadata for
that cell is published after editor destruction. Automatic Bit Offset also
adds `DisplayRole` to that deferred notification because fresh sibling layout
can change its `Auto (N)` presentation while the requested offset remains
unchanged. The committed path includes the active cell and uses a scoped
source-transition guard only for its own synchronous `ProjectService`
re-entry.

Conflict, missing or duplicate identity, unsupported or fixed mapping,
read-only state, removed PDO/entry, unrelated source change, context change,
or Details-page removal closes the transient editor with revert semantics.
Ordinary tab, mode, and window hiding leaves it alone while its page remains
in the same Details stack. The page destructor closes and synchronously
releases any delegate editor queued for deferred deletion. The boundary does
not support multiple or explicitly persistent editors, cross-node caching, a
generic table synchronizer, autosave, Project revision, CAS, merge, or
conflict UI.

The local delta changes only existing private `processdatapage.cpp/.h`, the
Workbench test declaration/implementation, and four evidence documents. It
adds no public API, source file, dependency, Provider/ProjectService contract,
Project format, persistence field, Project command, public model role, Core
or ProjectExplorer hook, application-bootstrap change, production thread or
timer, network, ADS, scan, online state, CoE/SDO execution, PLC, controller,
or hardware behavior. No CMake or qbs entry changed. Qualification used local
ESI/offline Project data with normal/2x offscreen tests and enabled/disabled
product lifecycle evidence. Authoritative evidence is under
`/private/tmp/embed-labs-wb-process-data-draft-001.lx7VcX`.
