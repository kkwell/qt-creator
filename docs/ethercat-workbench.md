# EtherCAT Workbench

## Scope

`EtherCATWorkbench` is the stage-4 engineering-shell plugin. It owns the
EtherCAT mode, left navigation tree, stable selection linkage, central details
container, built-in offline property pages, and Workbench commands. It does not
parse ESI files, own project persistence, scan a bus, produce diagnostics, or
define a controller protocol.

The layout follows the information hierarchy of common EtherCAT engineering
tools without copying Beckhoff assets, TwinCAT project formats, or proprietary
behavior.

## Dependencies and ownership

The plugin has hard metadata dependencies on `Core`, `EtherCATCore`,
`EtherCATProject`, and `EtherCATDevices`. It consumes only their exported
services and immutable `EtherCATData` snapshots. It does not include another
plugin's `Internal` headers or pass Qt model indexes across plugin boundaries.

The Qt Creator integration uses only public plugin mechanisms:

- `Core::IMode` for the EtherCAT mode;
- `Core::INavigationWidgetFactory` and `NavigationWidgetPlaceHolder` for the
  left navigation area;
- `Core::OutputPanePlaceHolder` for the lower output area;
- `Core::ActionManager` for the Workbench, refresh, expand, and collapse
  commands;
- `Core::IContext` for EtherCAT-mode command context;
- the ExtensionSystem object pool and `ProviderRegistry` for property pages
  and optional capabilities.

No file under Qt Creator Core, ProjectExplorer, or the application bootstrap
is changed by this plugin.

## Device tree

The two-column tree displays name and an explicit textual status. It contains:

```text
open EtherCAT projects
└── offline target
    └── EtherCAT master
        ├── configured offline slaves
        ├── diagnostics capability
        └── no configured slaves / scan capability (only when empty)

device repository
└── imported ESI device revisions
```

Every selectable node carries a stable `NodeId`, node kind, and project ID.
The tree never publishes a `QModelIndex` as identity. The selection service is
bidirectional: tree selection publishes the stable ID, and an external stable
selection expands and locates the corresponding row. If an active filter hides
that external selection, the filter is cleared so the selection remains
visible.

Search covers display name, status, device group, and Vendor/Product/Revision
identity. The context menu supports expand, collapse, locating the first
unsupported ESI device, and copying the stable node ID. Keyboard navigation is
provided by `QTreeView`.

Project topology changes use a bounded model reset. Accepted offline slaves are
read from immutable Project snapshots, appear below their master with stable
IDs and position-independent selection, and replace the empty scan placeholder.
Device repository changes use row insert, remove, move, and data-change
notifications; a 500-device test guards against unnecessary repository resets.

## Details and property pages

The details container discovers available `PropertyPageProvider` objects from
the public registry. It sorts pages by priority and stable provider/page IDs,
restores the selected effective page key, and owns every widget returned by a
provider. All provider-owned widgets are destroyed before the provider leaves
the object pool. Provider addition, availability changes, and removal rebuild
the page set without retaining removed pointers.

The built-in provider supplies read-only stage-4 pages:

- General for projects, targets, masters, configured slaves, the repository,
  and ESI devices;
- EtherCAT/SyncManager data for imported devices and the offline master;
- Process Data with RxPDO, TxPDO, entry, type, bit-width, and SM data;
- Startup with ESI CoE initialization records;
- DC with mode and nanosecond timing defaults;
- explicit Online and Diagnostics unavailable pages while the Diagnostics
  capability is absent.

A configured slave retains its scanned Identity, position, Serial Number,
Alias, and optional stable ESI description ID in the Project snapshot. When
that ESI entry is available, the same SyncManager, Process Data, Startup, and
DC read-only pages used by the repository device are reused. A missing ESI
match is reported explicitly and does not invent PDO or DC data.

When an available Diagnostics provider appears, the built-in Online and
Diagnostics placeholders are withdrawn so the later plugin can contribute the
real Mock pages through the public extension point. If it becomes unavailable
or is removed, the placeholders return. The tree similarly changes its
Diagnostics status and its no-slaves scan hint when optional Diagnostics or
Scan providers appear or disappear.

## Lifecycle

Initialization creates the controller, registers the built-in page provider,
registers the navigation factory and mode, then installs actions. Shutdown is
idempotent and proceeds in reverse UI ownership order: mode, navigation
factory, page provider, then controller. The controller disconnects project,
repository, and provider signals before clearing its model.

The plugin owns no background thread, timer, future, file format, or persistent
business state. Imported ESI data remains owned by `EtherCATDevices`; open
project state remains owned by `EtherCATProject`.

## Current limits

This stage deliberately provides no bus scan, interface discovery, online
state, WKC/DC/link samples, alarm stream, controller connection, network
protocol, configuration package, PLC language, or code generation. Stage 5
must add Mock scanning through public Core/Workbench extension points and must
not include Workbench-private headers. Stage 6 follows the same rule for Mock
diagnostics.

## Verification

The focused Workbench suite covers metadata and hard dependencies, mode/action
registration, a 500-device incremental model under
`QAbstractItemModelTester`, filtering and two-way stable selection, real ESI
data in Process Data/Startup/DC pages, configured-slave topology and ESI-page
reuse, dynamic property-page removal, and dynamic Scan/Diagnostics availability
and removal. It passes 9 tests on the qualified Qt 6.11.0 Release test build.

The normal 14-plugin product build and enabled/disabled startup smoke are
recorded in `docs/compatibility-matrix.md`. A visual desktop inspection was
attempted but could not run while the Mac session was locked; automated widget
and mode tests remain the UI evidence for this stage.
