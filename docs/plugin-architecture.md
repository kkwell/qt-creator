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
| 2 | `EtherCATProjectPlugin` | Configuration persistence complete | Version-2 project lifecycle, migration, validation, and Undo/Redo |
| 3 | `EtherCATDevicesPlugin` | Complete | ESI repository and offline device/PDO/DC models |
| 4 | `EtherCATWorkbenchPlugin` | In progress | Process Data, Startup, and DC are editable; full tree remains |
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
Workbench/Core extension points. Process Data, ordered Startup requests, and
Distributed Clocks are now editable for configured slaves through the public
Project service; their repository views stay read-only. The tree does not yet
contain the required Inputs, Outputs, RxPDO, TxPDO, and Modules/Channels
branches, so the Workbench completion gate remains open.

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
