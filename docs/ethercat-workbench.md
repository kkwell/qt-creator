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

The built-in provider supplies these stage-4 pages:

- General for projects, targets, masters, configured slaves, the repository,
  and ESI devices;
- EtherCAT/SyncManager data for imported devices and the offline master;
- an editable Process Data page for configured slaves, with a read-only ESI
  catalogue view for repository devices;
- an editable Startup request list for configured slaves, with a read-only ESI
  catalogue view for repository devices;
- read-only DC with mode and nanosecond timing defaults;
- explicit Online and Diagnostics unavailable pages while the Diagnostics
  capability is absent.

A configured slave retains its scanned Identity, position, Serial Number,
Alias, and optional stable ESI description ID in the Project snapshot. When
that ESI entry is available, the same SyncManager, Process Data, Startup, and
DC read-only pages used by the repository device are reused. A missing ESI
match is reported explicitly and does not invent PDO, Startup, or DC data.

### Process Data workflow

The Process Data page follows the TwinCAT 3 information flow without copying
Beckhoff assets or formats. The workflow was compared with Beckhoff's official
TwinCAT 3 Process Data page documentation:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.
Its adjustable layout contains:

```text
Sync Manager                 PDO List
PDO Assignment               PDO Content
             Process Image Preview
```

Selecting a Sync Manager filters PDO Assignment and PDO List. Selecting a PDO
in either table keeps the two selections synchronized and displays its entries
in PDO Content. The tables expose SM direction and size, current assignment,
PDO index and byte.bit size, F/M flags, default selection, optional predefined
group, entry index/subindex, bit width, requested bit offset, type, and
direction. The derived process-image table displays output and input entries
with absolute byte.bit offsets, source PDO, and SM.

Repository-device pages are always read-only. A configured slave whose stored
Process Data is empty shows a deterministic initial mapping derived from its
current ESI description without marking the project modified. Fixed and
mandatory PDOs are selected first; otherwise the first supported PDO for each
SM is selected. The user must explicitly store that mapping or make an edit
before it enters the project. Stable SM/PDO/entry IDs are derived from the
configured slave ID, so the same proposal is regenerated until it is
persisted. Unsupported or contradictory ESI assignments remain visible but
cannot be selected silently.

For configured slaves, optional PDO assignments are checkable and non-fixed
PDO content supports editing index, subindex, name, bit width, bit offset, and
data type. Fixed content and mandatory assignment constraints remain
read-only. Every candidate is passed to
`validateProcessDataConfiguration()` and then to the public
`ProjectService::setProcessDataConfiguration()` command. A rejected candidate
shows the first error and preserves the current project and undo history. An
accepted candidate is one project Undo/Redo command. Store/Restore ESI
Defaults uses the same checked command path.

### Startup workflow

The Startup page follows Beckhoff's documented TwinCAT 3 request-list workflow
without copying Beckhoff assets or formats. The reference page is:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.
The central list shows enabled state, explicit order, transition, CoE protocol,
index, subindex, data type, raw hexadecimal data, and comment. The adjacent
actions provide Move Up, Move Down, New, Delete, and Edit operations. Inline
editing remains available for individual non-fixed fields, and Delete requires
an explicit confirmation before it creates the undoable removal command.

Requests are displayed in configured execution order. Move actions produce a
contiguous explicit order, while direct order edits are checked for duplicate
enabled positions. An angle-bracketed ESI transition such as `<PS>` is shown
as a fixed request and cannot be edited, deleted, enabled/disabled, or moved.
Moving another row across a fixed request is also prohibited.

Repository-device pages are always read-only. As with Process Data, an empty
configured-slave Startup value first shows deterministic stable-ID ESI defaults
without marking the project modified. Store/Restore ESI Defaults is explicit.
A configured slave without an ESI match can still create fully manual offline
CoE requests through the New dialog.

Every table or dialog candidate is passed to
`validateStartupConfiguration()` and then to the public
`ProjectService::setStartupConfiguration()` command. Invalid transition,
index, order, type/value width, or raw hex input is rejected without changing
the project or its history. Each accepted add, edit, delete, enable, reorder,
or defaults action is one Project Undo/Redo command. No request is transmitted
and no Mock or online value is copied automatically.

When an available Diagnostics provider appears, the built-in Online and
Diagnostics placeholders are withdrawn so the Diagnostics plugin can
contribute its live Mock pages through the public extension point. If the
Provider becomes unavailable or is removed, the placeholders return. The tree
similarly changes its Diagnostics status and its no-slaves scan hint when
optional Diagnostics or Scan providers appear or disappear.

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

The Workbench itself deliberately provides no real bus scan, interface
discovery, online controller state, controller connection, network protocol,
configuration package, PLC language, or code generation. Scan and Diagnostics
remain optional Mock Provider plugins. DC is still read-only in Workbench, and
the full Inputs/Outputs/RxPDO/TxPDO/Modules tree branches remain pending
independent issues.

## Verification

The focused Workbench suite covers metadata and hard dependencies, mode/action
registration, a 500-device incremental model under
`QAbstractItemModelTester`, filtering and two-way stable selection, real ESI
data in Process Data/Startup/DC pages, configured-slave topology and ESI-page
reuse, dynamic property-page removal, and dynamic Scan/Diagnostics availability
and removal. The Process Data workflow additionally covers RxPDO/TxPDO SM
selection, read-only repository and fixed/mandatory mappings, an empty
no-ESI state, ESI-derived initial mapping, assignment and entry edits,
validation rejection, process-image refresh, and real DetailsView plus Project
Undo/Redo reentrancy. The Startup workflow covers the ESI catalogue, explicit
defaults storage, fixed requests, New/Edit/Delete dialogs, enable state,
ordering, type/value validation, manual no-ESI empty state, and real
ProjectService Undo/Redo reentrancy. It passes 11 tests on the qualified Qt
6.11.0 Release test build.

The normal 16-plugin product build and enabled/disabled startup smoke are
recorded in `docs/compatibility-matrix.md`. The current Startup issue was also
inspected in a real EtherCAT Mode desktop session: the table, action column,
fixed request, and New dialog rendered without clipping or layout defects.
