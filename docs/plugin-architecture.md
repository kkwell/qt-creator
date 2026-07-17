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
| 4 | `EtherCATWorkbenchPlugin` | In progress | Project, Target, Master, configured-slave, ESI Repository, individual ESI catalogue-device General, master-side ESI insertion, and supported-device drag-and-drop workflows, master/slave EtherCAT views, Alias editing, editable pages, manual offline topology, process-data tree, command/status surfaces, and public Scan/Diagnostics state overlays are complete; remaining UI qualification is open |
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
Selecting an individual repository device routes the existing immutable public
`DeviceDescription` snapshot into a Workbench-private General-page widget. The
widget renders identity, offline configuration coverage, qualification, and
source provenance read-only, retains no Provider or job, and clears itself when
the context no longer resolves. `EtherCATDevices` remains the sole owner of
parsing and source data; no `Internal` header, widget pointer, model index,
public API, or controller behavior crosses the plugin boundary.
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
stable selection. The navigation container now follows Qt Creator's focus-proxy
pattern, so activating the navigation page gives focus to the device tree and
arrow-key movement continues through the existing stable-ID selection bridge.
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
startup-project state; EtherCATProject owns persistence and its undo stack.
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
