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
- `Core::StatusBarManager` for the mode-scoped engineering status surface;
- `Core::IContext` for EtherCAT-mode command context;
- the exported `EtherCATCore::StateService` for shared Scan/Diagnostics status;
- the ExtensionSystem object pool and `ProviderRegistry` for property pages
  and optional capabilities.

No file under Qt Creator Core, ProjectExplorer, or the application bootstrap
is changed by this plugin.

## Unified status surface

The Workbench registers one Qt Creator status-bar control in the standard
`LastLeftAligned` area. It is visible only in EtherCAT Mode and consumes the
existing `StateService` entries published by the Mock Scan and Diagnostics
workflows. The highest-severity entry selects a Ready, Busy, Warning, or Fault
icon and a short textual state. Every active phase-1 state remains explicitly
labeled `MOCK` in every UI language; an empty service displays `Offline`.

The button tooltip and drop-down list retain every contributing summary and
detail, so the compact visible state does not discard its source information.
The control uses Qt Creator icons, the active style's small-icon metric, and
its current `sizeHint()` rather than hard-coded colors, fonts, or pixels. It
therefore remains readable in the constrained status bar and at high DPI.
Switching to another Mode hides the control without clearing shared state.

## Device tree

The two-column tree displays name and an explicit textual status. It contains:

```text
open EtherCAT projects
└── offline target
    └── EtherCAT master
        ├── configured offline slave
        │   ├── Inputs
        │   │   └── active TxPDO process-image entries
        │   ├── Outputs
        │   │   └── active RxPDO process-image entries
        │   ├── RxPDO
        │   │   └── active PDO
        │   │       └── PDO entries
        │   ├── TxPDO
        │   │   └── active PDO
        │   │       └── PDO entries
        │   └── Modules / Channels
        │       └── explicit empty state when no modular data exists
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

Both columns resize to their visible contents and node text is not elided.
This gives the hierarchical name priority in Qt Creator's narrow navigation
area while retaining the explicit status column through standard horizontal
scrolling. The tree also publishes a translated accessible name and
description; macOS accessibility exposes the tree and all five process-data
branches.

Project topology changes use a bounded model reset. Accepted offline slaves are
read from immutable Project snapshots, appear below their master with stable
IDs and position-independent selection, and replace the empty scan placeholder.
Device repository changes use row insert, remove, move, and data-change
notifications; a 500-device test guards against unnecessary repository resets.

The slave subtree follows the process-data hierarchy described by Beckhoff for
TwinCAT 3 I/O devices and process data:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>,
<https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/2335733771.html>,
and
<https://infosys.beckhoff.com/content/1033/epp3356-0022/1652713483.html>.
Only selected PDOs are projected into the tree. From the controller's point of
view, Inputs are active TxPDO entries received from a slave and Outputs are
active RxPDO entries transmitted to a slave. The status column exposes counts,
sizes, addresses, Sync Manager assignment, types, and bit widths without
requiring the details pane to identify a row.

Every projected group, PDO, and entry has a deterministic view `NodeId`. It
also retains the stable source-domain ID of its slave, PDO, or entry. This keeps
selection unambiguous when the same source PDO IDs occur on multiple slaves,
and it lets Details resolve the owning slave without copying complete slave
configurations into every tree node. Renaming a slave does not change any child
view ID. Empty groups have explicit, non-selectable placeholder rows. The model
is covered with 128 configured slaves as well as the 500-device repository.

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
- a read-only, automatically focused Process Data view for Inputs, Outputs,
  RxPDO, TxPDO, PDO, and PDO Entry tree selections;
- a local-only CoE Online Mock object dictionary with explicit offline view and
  an undoable Add to Startup path;
- General information for Modules / Channels, Module, and Channel selections;
- an editable Startup request list for configured slaves, with a read-only ESI
  catalogue view for repository devices;
- an editable Distributed Clocks page for configured slaves, with a read-only
  ESI operation-mode catalogue for repository devices;
- explicit Online and Diagnostics unavailable pages while the Diagnostics
  capability is absent.

A configured slave retains its scanned Identity, position, Serial Number,
Alias, and optional stable ESI description ID in the Project snapshot. When
that ESI entry is available, the configured slave reuses its SyncManager,
Process Data, Startup, and DC descriptions as editable offline proposals. The
repository-device views remain read-only. A missing ESI match is reported
explicitly and does not invent PDO, Startup, or DC data.

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

Selecting a process-data group, PDO, or entry in the navigation tree opens the
same Process Data representation and focuses its owning SM/PDO. These derived
views are intentionally read-only: assignment and entry editing remains on the
configured-slave Process Data page. This prevents an edit from removing the
currently selected derived node while its details page is handling the action,
while preserving one checked Project command path for all mutations.

### CoE Online Mock workflow

The CoE Online page follows the information structure documented for the
TwinCAT 3 CoE Online tab without copying Beckhoff assets, protocols, or project
formats. The reference page is:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.
It presents a hierarchical object dictionary with Index, Name, Flags, Value,
and Unit columns, together with Update List, Advanced, Add to Startup, Auto
Update, Single Update, Show Offline Data, source, and Module OD controls.

Every displayed online value is visibly marked as `MOCK DATA`. The page builds
its local dictionary from the configured identity, ESI Startup proposals,
persisted Startup requests, and PDO entries. Identity values use conventional
little-endian numeric formatting and subindices appear below their main object.
The access flags are an engineering interaction prototype, not device access
rights discovered through SDO information. No controller, network, ADS, AoE,
or EtherCAT transfer occurs.

Update List advances a deterministic local sample. Auto Update stays disabled
until a future controller Provider exists, so this Workbench issue introduces
no polling timer or background task. Advanced switches between local Mock and
offline device-description values, selects an object-index range, and can hide
standard or PDO objects. The page also supports recursive text filtering,
including Unicode engineering names. Offline and repository-device views are
read-only.

The Value cell of a locally writable Mock object accepts size-checked raw
hexadecimal edits. Editing only changes the page's transient Mock value. Add to
Startup requires explicit confirmation, appends a new `PS` request without
overwriting an existing request, and submits the complete candidate through
`ProjectService::setStartupConfiguration()`. The resulting project change is
undoable. Cancel, invalid hex, wrong width, read-only objects, offline data, and
repository-device contexts leave the project unchanged.

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

### Distributed Clocks workflow

The DC page follows the structure documented for the TwinCAT 3 Distributed
Clocks page without copying Beckhoff assets or project formats. The reference
page is:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.
It groups Operation Mode, Enable, and AssignActivate under Cyclic Mode; gives
SYNC0 and SYNC1 separate enable, cycle, and shift controls; and exposes the
potential-reference-clock choice. Every cycle and shift value is displayed,
edited, and stored explicitly in nanoseconds.

Repository-device pages list all parsed ESI operation modes but stay
read-only. An empty configured-slave DC value proposes the first ESI mode
without marking the project modified. Store/Restore ESI Defaults is explicit,
and choosing another ESI operation mode applies that mode's AssignActivate and
complete SYNC0/SYNC1 timing as one checked command. A configured slave without
an ESI match can enter a manual operation-mode name and timing.

Every candidate is passed to `validateDcConfiguration()` and then to the
public `ProjectService::setDcConfiguration()` command. Invalid numeric input,
an empty enabled mode, an out-of-range AssignActivate or cycle, a shift outside
one cycle, SYNC without DC, and SYNC1 without SYNC0 are rejected without
changing the Project or Undo history. Disabling DC disables both signals in
one command; disabling SYNC0 also disables SYNC1. Each accepted mode, enable,
timing, reference-clock, or defaults change is one Project Undo/Redo command.

TwinCAT's advanced Sync Unit cycle-source/multiplier fields and calculated
input-reference timing are not represented by the current Phase-1 domain
model. They are deliberately not simulated by the UI and require a separate
data-contract issue before they can be added.

When an available Diagnostics provider appears, the built-in Online and
Diagnostics placeholders are withdrawn so the Diagnostics plugin can
contribute its live Mock pages through the public extension point. If the
Provider becomes unavailable or is removed, the placeholders return. The tree
similarly changes its Diagnostics status and its no-slaves scan hint when
optional Diagnostics or Scan providers appear or disappear.

## Lifecycle

Initialization creates the controller, registers the built-in page provider,
registers the navigation factory and mode, then installs actions and the
mode-scoped status control. Shutdown is idempotent and proceeds in reverse UI
ownership order: status control, mode, navigation factory, page provider, then
controller. The controller disconnects project, repository, and provider
signals before clearing its model.

The plugin owns no background thread, timer, future, file format, or persistent
business state. Imported ESI data remains owned by `EtherCATDevices`; open
project state remains owned by `EtherCATProject`.

## Current limits

The Workbench itself deliberately provides no real bus scan, interface
discovery, online controller state, controller connection, network protocol,
configuration package, PLC language, or code generation. Scan and Diagnostics
remain optional Mock Provider plugins. CoE Online is also a clearly labeled
local interaction Mock; it does not perform SDO information or object access.
Inputs, Outputs, RxPDO, TxPDO, and their
PDO/entry branches now render persisted, validated active process data. Actual
modular ESI profile parsing and project-side module/channel values remain a
separate Devices/data-contract issue; until such source data exists, Modules /
Channels shows an explicit empty state instead of fabricated rows. Advanced
Sync Unit timing semantics also remain pending an independent data-contract
issue.

## Verification

The focused Workbench suite covers metadata and hard dependencies, mode/action
registration, a 500-device incremental model under
`QAbstractItemModelTester`, filtering and two-way stable selection, real ESI
data in Process Data/Startup/DC pages, configured-slave topology and ESI-page
reuse, dynamic property-page removal, and dynamic Scan/Diagnostics availability
and removal. The process-data tree coverage verifies the exact five-branch
order, input/output direction, active-PDO projection, unique deterministic view
IDs, retained source IDs, empty modular state, recursive filtering, derived
Details routing, non-elided content-sized navigation columns, accessible tree
metadata, and 128 configured slaves. The Process Data workflow
additionally covers RxPDO/TxPDO SM
selection, read-only repository and fixed/mandatory mappings, an empty
no-ESI state, ESI-derived initial mapping, assignment and entry edits,
validation rejection, process-image refresh, and real DetailsView plus Project
Undo/Redo reentrancy. The Startup workflow covers the ESI catalogue, explicit
defaults storage, fixed requests, New/Edit/Delete dialogs, enable state,
ordering, type/value validation, manual no-ESI empty state, and real
ProjectService Undo/Redo reentrancy. The CoE Online workflow covers the
TwinCAT-inspired object hierarchy and controls, ESI/offline/Mock sources,
manual refresh, advanced and Unicode filters, read-only boundaries, raw-value
editing, cancelled and confirmed Add to Startup, no-overwrite behavior, no-ESI
state, Project modified state, and Undo. Its model is also checked by
`QAbstractItemModelTester` and the focused flow passes at `QT_SCALE_FACTOR=2`.
The DC workflow covers two ESI operation
modes, explicit Store/Restore, manual no-ESI configuration, AssignActivate,
SYNC0/SYNC1 enable and nanosecond timing, reference-clock selection, validation
rejection, dependent disable actions, and ProjectService Undo/Redo reentrancy.
The status-surface coverage verifies Offline/Busy/Error priority, Mock labels,
tooltip and drop-down details, standard icon dimensions, content width,
Mode-scoped visibility, and the existing configured/unsupported tree icons.
It passes 16 tests on the qualified Qt 6.11.0 Release test build; the focused
status flow also passes at `QT_SCALE_FACTOR=2`.

The normal 16-plugin product build and enabled/disabled startup smoke are
recorded in `docs/compatibility-matrix.md`. A populated real EtherCAT Mode
desktop session was inspected with Inputs/Statusword, Outputs/Controlword,
RxPDO/Drive Command, TxPDO/Drive Status, their entries, and the explicit empty
Modules / Channels state expanded simultaneously. Node names remained readable
in the narrow Qt Creator navigation area, icons used the normal Creator visual
scale, and the complete tree remained visible to macOS accessibility. The
previous DC desktop inspection also confirmed Cyclic Mode, SYNC0, SYNC1,
validation, units, and reference-clock controls without clipping. A direct
Qt 6.11 Widget render of the CoE page was inspected at Retina resolution: the
control grid, object hierarchy, values, and bilingual long name had no overlap
or clipping. The page could not receive a full desktop interaction inspection
because macOS was locked; no main-window click result is claimed.

A direct 2520 x 1400 Qt main-window render of the status issue was also
inspected. `MOCK Fault` remained fully visible beside a standard-sized Creator
critical icon in `LastLeftAligned`, without overlapping the output controls or
right-corner widgets. Computer Use could not perform desktop clicks because
macOS remained locked, so only the direct Qt render is claimed for this issue.
