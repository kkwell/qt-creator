# EtherCAT Workbench

## Scope

`EtherCATWorkbench` is the stage-4 engineering-shell plugin. It owns the
EtherCAT mode, left navigation tree, stable selection linkage, central details
container, built-in offline property pages, manual offline-topology commands,
editable project, target, master, and configured-slave General pages, Workbench
commands, the local ESI repository management page, the dedicated read-only
General page for individual ESI catalogue devices, the master-side ESI device
insertion workflow, supported-device drag-and-drop into the active offline
Master,
the TwinCAT-aligned master EtherCAT settings and local topology view, and the
presentation of public Scan/Diagnostics snapshots in the device tree. It does
not parse ESI files, own project persistence, scan a bus, produce diagnostics,
or define a controller protocol.

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
- `Core::ActionManager` and the shared EtherCAT action container for
  once-registered Workbench, Scan, and Diagnostics commands;
- `Core::StatusBarManager` for the mode-scoped engineering status surface;
- `Core::IContext` for EtherCAT-mode command context;
- the exported `EtherCATCore::StateService` for shared Scan/Diagnostics status;
- the ExtensionSystem object pool and `ProviderRegistry` for property pages
  and optional capabilities.

No file under Qt Creator Core, ProjectExplorer, or the application bootstrap
is changed by this plugin.

## Engineering command strip

`ISSUE-WB-TOOLBAR-001` places one compact engineering command strip above the
device tree and details area. This follows the TwinCAT 3 pattern of keeping
configuration-state and scan commands at the top of the I/O workspace, while
remaining a native Qt Creator Mode. The scan placement was compared with
Beckhoff's documented TwinCAT 3 toolbar and tree-context workflow:
<https://infosys.beckhoff.com/content/1033/el125x_el2258/2584719371.html>.
No Beckhoff icon, asset, project format, or proprietary command is copied.

The strip does not create a second set of actions. It mirrors the existing
`EtherCAT.Menu` `QAction` objects, excluding only `Open Workbench` because the
user is already inside that Mode. Menu entries, toolbar buttons, shortcuts,
enabled state, checked state, tooltips, and callbacks therefore share the same
ActionManager registration. Action-added and action-removed events keep the
strip synchronized when an optional plugin is present or absent.

Tree-only commands such as `Locate Unsupported Device`, `Copy Node ID`, and the
offline-topology Add/Remove/Move commands are registered with ActionManager for
context and shortcut consistency but are not added to `EtherCAT.Menu`. They
therefore remain available from the device tree without adding low-frequency
actions to the compact engineering strip.

Workbench never names or includes Scan or Diagnostics implementation details.
When either optional plugin is disabled, its actions are simply missing from
the shared action container and no empty controls are fabricated. When both
are available, their clearly named Mock commands appear automatically.

The strip uses the active Qt style's standard toolbar icon metric and compact
icon-only buttons, with complete command names retained in tooltips. This
keeps all current engineering commands visible in a narrow window and at high
DPI without hard-coded colors, fonts, icon sizes, or pixels. The Workbench
expand and collapse commands now use the same standard Creator icons as the
navigation controls.

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
identity, including provider-supplied difference summaries and details. The
context menu supports expand, collapse, locating the first topology difference,
locating the first warning/error, opening the matching Diagnostics branch,
locating the first unsupported ESI device, adding a supported ESI device to the
active offline master, opening the master-side `Add New Item...` device
selector, setting an inactive project as the Qt Creator active project,
removing or moving a configured slave, and copying the stable node ID.

## Active project navigation state

`ISSUE-WB-NAV-ACTIVE-PROJECT-001` makes the Qt Creator active project visible
when more than one `.ecatproject` is open. The active project root reports
`Active project | Offline`; every other project root continues to report
`Offline`. These are two independent facts: `Active project` identifies Qt
Creator's current startup/engineering project, while `Offline` confirms that
the local project has no controller or runtime connection. Selecting or
focusing an inactive Workbench row does not activate it.

The controller consumes the existing public `ProjectService::projects()`,
`activeProjectId()`, and `activeProjectChanged` contract. The tree model keeps
the immutable project status unchanged and derives the visible active prefix
only for Display, Status, Search, and tooltip roles. An active-project switch
therefore emits bounded `dataChanged` notifications across the affected root
rows instead of resetting the model. Stable selection, persistent model
indexes, and the Details context survive the switch; an existing
`Active project` filter dynamically moves to the newly active branch. The
active offline Master's existing drop target and tooltip move through their
unchanged controller path.

Qt Creator documents one active project among the projects shown in its
Projects tree:
<https://doc.qt.io/qtcreator/creator-projects-view.html>. Beckhoff's
`Active PLC project` describes a different selection inside a TwinCAT project,
and `Activate Configuration` enables or overwrites a runtime configuration:
<https://infosys.beckhoff.com/content/1033/tc3_userinterface/3907516043.html>
and
<https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2953964811.html>.
Neither meaning is claimed here. This issue adds no Activate Configuration,
PLC project, boot project, download, ADS, controller, Config/Run/OP, or online
behavior. The marker issue itself added no Workbench command for changing the
active project; that explicit interaction is qualified separately below.

## Set the active project from Workbench

`ISSUE-WB-NAV-SET-ACTIVE-PROJECT-001` closes the interaction loop for multiple
open `.ecatproject` roots. Right-clicking an inactive project root adds the
context-only `Set as Active Project` command in its own project-operation group,
before `Copy Node ID`. The current active root, Master, configured-slave, ESI,
placeholder, and closed-project contexts do not include the command. Merely
selecting, focusing, or inspecting an inactive project still does not activate
it.

The command has one Workbench-owned ActionManager identity and is deliberately
absent from the shared EtherCAT menu and compact command strip. It has no
default shortcut. The navigation widget contributes the Workbench context while
it owns focus, so the command proxy remains live when `EtherCAT Devices` is
shown from another Qt Creator mode such as Edit. Its tooltip and status text say
that the selected open offline project becomes the Workbench engineering
context and explicitly state that no controller configuration is activated.
This follows Qt Creator 20's Projects-tree terminology and
inactive-project-only visibility rather than reusing ProjectExplorer's private
action, whose callback depends on ProjectExplorer's own current tree node.

The controller resolves the current stable `NodeId` back to a real open Project
snapshot, rejects non-Project, active, stale, closed, and shutdown states, then
calls the existing public `ProjectService::activateProject()` operation. It
retains no Project pointer or model index. A successful switch keeps Selection
and Details on the same project root, emits no model reset, does not modify the
project snapshot or Undo stack, and reuses the active-marker, filter, and
active-Master drop-target handoff documented above. A failed trigger reports
through the existing Message Manager path.

This command is Qt Creator active-project selection only. It is not Beckhoff
`Active PLC project`, `Activate Configuration`, Login, Download, boot-project,
ADS, controller connection, Config/Run/OP, or any online transition. Replacing
Qt Creator Run/Debug controls with future Mock or real controller-state controls
requires a separate issue and a truthful state/transport contract.

## Navigation keyboard focus

`ISSUE-WB-NAV-KEYBOARD-001` completes the activation path for the existing
`QTreeView` keyboard navigation. The Workbench navigation container normally
uses the device tree as its focus proxy. When Qt Creator activates the EtherCAT
navigation page and focuses the factory widget, the tree receives focus
without an extra mouse click. Up/Down then changes the visible row through
`QTreeView`, and the existing current-index bridge publishes the selected
stable `NodeId` through `SelectionService`.

This follows the focus-proxy pattern already used by Qt Creator's Project
Tree, Class View, and Folder Navigation implementations in
`src/plugins/projectexplorer/projecttreewidget.cpp`,
`src/plugins/classview/classviewnavigationwidget.cpp`, and
`src/plugins/coreplugin/foldernavigationwidget.cpp`. It preserves the
tree-centred engineering workflow described by Beckhoff for I/O devices and
EtherCAT master/slave hierarchies:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html> and
<https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/2335733771.html>.
The local focus behavior is a native Qt Creator integration detail; it is not
presented as copied TwinCAT behavior.

The issue adds no shortcut, filter behavior, model role, public API, Provider,
thread, timer, or persistent state. Destruction remains ordinary QWidget child
ownership, and the existing selection/provider cleanup paths are unchanged.

## Navigation filter empty state

`ISSUE-WB-NAV-FILTER-EMPTY-001` completes the visible and accessible feedback
for the existing navigation filter. A non-empty query whose proxy model has no
top-level rows now swaps the tree page for a centred, word-wrapped
`No EtherCAT nodes match the current filter.` state and a keyboard-focusable
`Clear Filter` button. Clearing restores the tree, its current stable `NodeId`,
and direct arrow-key focus. While the empty state is visible, the navigation
container uses the visible recovery button as its focus proxy instead of the
hidden tree.

The filter field, empty state, and recovery action publish translated
accessible names or descriptions. Proxy row insertion, removal, reset,
layout, and data changes all re-evaluate the stacked view, so a matching device
that appears while a query remains active restores the tree. Filtering remains
presentation-only: it does not mutate a Project, ESI repository, Provider, or
`SelectionService`. An external stable selection continues to clear a filter
that hides the selected node and then restores that exact row.

Qt documents a line edit connected to `QSortFilterProxyModel` as the common
filtering pattern, with source changes dynamically re-filtered by default:
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html>. The stacked no-result page
follows Qt Creator's existing Extension Manager pattern. Beckhoff documents a
tree-centred I/O workflow and searchable/filterable device-selection dialogs:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>,
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1096103307.html>, and
<https://infosys.beckhoff.com/content/1033/ps2001-4810-1001/10832046859.html>.
Those Beckhoff pages do not specify a persistent main-tree no-result state;
this feedback is a native Qt Creator usability and accessibility completion,
not copied or claimed TwinCAT behavior.

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
Supported ESI catalogue devices are copy-draggable to the currently active
offline Master. Limited entries are not draggable, and no project other than
the active valid offline project advertises a drop target. This is an append
shortcut over the same checked insertion path described below; it does not
move or remove the repository entry.

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

## Unified tree commands

`ISSUE-WB-CONTEXT-COMMANDS-001` removes the remaining transient device-tree
actions. Expand Device Tree, Collapse Device Tree, Locate First Topology
Difference, Locate First Issue, Open Diagnostics, Locate Unsupported Device,
and Copy Node ID now each have one stable ActionManager command ID. The tree
context menu adds those registered actions directly, and the navigation-header
tool buttons use the same Expand/Collapse actions as their `defaultAction()`.
No page or widget creates an alternative callback or state copy.

This retains the TwinCAT-style left-tree workflow, including the documented
right-click scan entry on I/O Devices:
<https://infosys.beckhoff.com/content/1033/twincat_bsd/5624962827.html>, while
using Qt Creator's native command and context architecture. No Beckhoff menu
asset, command ID, or proprietary implementation is copied.

Context enablement is recalculated from the current tree snapshot immediately
before the menu opens. An unsupported-device command is enabled only when such
a repository entry exists, and Copy Node ID is present only for a real
selectable node. Placeholder rows cannot copy a previous selection's ID.
Outside the popup, model and Selection Service signals keep the same registered
actions synchronized for shortcuts. The update path disables the actions if
the Workbench controller has already been destroyed during shutdown.

## Offline topology editing

`ISSUE-WB-OFFLINE-TOPOLOGY-001` adds the first manual configuration path from
the imported ESI catalogue into the offline EtherCAT master. The interaction
was compared with Beckhoff's documented TwinCAT 3 offline configuration flow,
where devices are appended from an ESI-backed tree command:
<https://infosys.beckhoff.com/content/1033/ps2001-4810-1001/10832046859.html>.
No Beckhoff asset, project format, command ID, or proprietary implementation is
copied.

Right-clicking a supported repository device exposes the dynamically targeted
`Add to "<Project>" / "<Master>"` command described below. The phase-1 project
format contains one master, so the command targets that master in the active
valid project. A new stable slave ID and the next physical position are
allocated, and a case-insensitive unique name is derived from the ESI device
name. The slave keeps the complete identity and repository reference and
receives ESI-derived Process Data, Startup, and first DC-mode defaults. The
same private factory supplies those defaults to the existing property pages,
preventing the creation path and page proposal from drifting.

A configured slave exposes `Remove from Offline Master...`, `Move Offline Slave
Up`, and `Move Offline Slave Down`. Boundary commands are disabled. Moves keep
the selected stable ID and normalize physical positions. Removal selects the
nearest remaining slave or the parent master when the list becomes empty. All
four mutations call the existing public
`ProjectService::replaceOfflineSlaves()` command, so Project validation,
modified state, persistence, Undo, and Redo remain owned by
`EtherCATProject`. Rejected mutations are reported through Qt Creator's Message
Manager.

Each operation has one ActionManager registration reused by the device-tree
context menu and any shortcut. Unsupported ESI entries, invalid projects, and
inapplicable selections cannot enable the corresponding command. These four
low-frequency commands remain context-only and are deliberately excluded from
the compact command strip.

The repository-side command and the drag-and-drop shortcut do not add
multi-selection editing, multiple-master target selection, a bus scan,
controller transport, or online configuration. The master-side selection
workflow is documented below; the other capabilities require separate issues
and must not bypass the same checked Project service boundary.

## Repository quick-add target disclosure

`ISSUE-WB-ESI-QUICK-ADD-TARGET-DISCLOSURE-001` makes the global Device
Repository command disclose the exact offline Project and Master it will
modify. When a valid active project is available, the compact menu text is
`Add to "<Project>" / "<Master>"`. Its tooltip and status text name the same
target and state that the command changes only the local offline project; it
does not contact a controller or physical hardware. With no valid target, the
action returns to `Add to Active Offline Master`, explains why no target is
available, and remains disabled.

The existing ActionManager command is reused by shortcuts and the real Device
Repository context menu. It has `CA_UpdateText`, while its keyboard description
remains the stable `Add ESI Device to Active Offline Master`. Project open,
active-project, rename, and close-fallback signals refresh the presentation.
`Utils::quoteAmpersands()` preserves literal ampersands in menu names, and the
formatting path preserves literal `%1` and `%2` in Project or Master names.

The action keeps a copied `OfflineMasterTarget` containing the Project and
Master stable IDs plus the names that were displayed. The same captured value
is passed to the trigger path. Immediately before mutation, the Workbench
controller rereads the active valid Project and its Master and requires both
stable IDs to match. A target that became stale after an active-project switch
is rejected without modifying either project. A current target still calls the
existing Project-owned replacement command, so validation, modified state,
persistence, Undo, and Redo do not move into Workbench.

Qt Creator documents command attributes for dynamic action presentation and
exposes the command's `QAction` as the user-facing action:
<https://doc.qt.io/qtcreator-extending/actionmanager.html>. Qt documents action
text, tooltip, status text, and doubled ampersands for a literal ampersand:
<https://doc.qt.io/qt-6/qaction.html>. Beckhoff's offline workflow appends a
slave beneath the explicitly selected device in the configuration tree:
<https://infosys.beckhoff.com/content/1033/el331x/1036999947.html> and
<https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/2481604363.html>.
Those references establish target visibility and offline-tree context; the
dynamic wording and stable-ID stale-target guard are Embed Labs Qt-native
behavior.

The failure-first focused run produced 2 passes and 1 expected failure because
the previous fixed action text named neither Project nor Master. Final focused
runs pass 3 tests at normal scale and 3 at `QT_SCALE_FACTOR=2`. They cover no
project, two open projects, active-project switching, Project and Master
renames, literal `%1`, `%2`, and `&`, stale-target rejection with identical
project snapshots, mutation of only the displayed target, Project-owned Undo,
close fallback, final no-project disablement, shared QAction identity, and the
real context menu. The inspected offscreen menu renders are 382 x 190 and
764 x 380 pixels, with no clipping, overlap, or scale drift.

The complete Workbench suite passes 39 tests. The six isolated EtherCAT suites
pass 90 tests: Core 17, Project 12, Devices 8, Workbench 39, Scan 7, and
Diagnostics 7. The `WITH_TESTS=OFF` product build passes and contains exactly
the 16 allow-listed plugin dylibs. Enabled and explicitly Workbench-disabled
product processes each remained stable for 16 seconds before intentional
SIGTERM target status 15, using fresh settings under
`/private/tmp/embed-labs-quick-add-product-enabled-qualified.6Ym6xU/settings`
and
`/private/tmp/embed-labs-quick-add-product-disabled-qualified.mOI49C/settings`.
Every executable qualification was offscreen, explicitly cleared inherited
DYLD variables, and used only the process-local Touch Bar LLDB breakpoint.
Final cleanup found no residual Embed Labs or LLDB process, DiagnosticReports
file, or ReportCrash event after 22:40:03 on 2026-07-18.

This issue changes only existing private Workbench action, controller, test,
and documentation files. It adds no public API, Provider, thread, timer,
persistence, dependency, source file, CMake/qbs entry, network or hardware
behavior, or upstream Core, ProjectExplorer, or application path. Scan and
Diagnostics remain explicitly local Mock capabilities.

## Offline-slave removal confirmation

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-CONFIRM-001` prevents a single accidental
activation from immediately deleting a configured slave. The context command
is now `Remove from Offline Master...`; its tooltip and status text state that
the slave's offline Process Data, Startup, and Distributed Clocks
configuration are included. Triggering it opens a plain-text question that
identifies the slave and its one-based position. `No` is both the default and
Escape button, and the consequence text explicitly retains the existing Undo
recovery path.

The Workbench controller captures the stable project, Master, and slave IDs,
plus the displayed name and position, before the question opens. A guarded
controller pointer covers plugin teardown. After `Yes`, the controller rereads
the latest public Project snapshot and requires the current stable selection,
project, Master, and slave to match the captured candidate. If selection
changes, the project closes, or the slave disappears while the question is
open, the request is rejected and no replacement command is submitted. This
prevents a question about slave A from deleting slave B after an intervening
event.

While the project remains open, `No`, Escape, and selection drift leave the
complete Project snapshot, modified state, Selection, and Undo/Redo
availability unchanged. If the project closes while the question is open, no
additional or stale replacement is submitted. A valid `Yes` still uses
`ProjectService::replaceOfflineSlaves()`, so validation, position
normalization, persistence, nearest-node selection repair, Undo, and Redo
remain Project-owned. Undo restores the complete prior slave list, including
stable IDs, identity, Alias, ESI reference, Process Data, Startup, and DC
configuration.

Qt documents `QMessageBox` as the modal standard dialog for a question, with
explicit standard, default, and Escape buttons:
<https://doc.qt.io/qt-6/qmessagebox.html>. The implementation follows the
existing Qt Creator removal pattern in
`src/plugins/projectexplorer/buildsettingspropertiespage.cpp` and
`src/plugins/projectexplorer/runsettingspropertiespage.cpp`, including a
non-destructive default, while adding the Workbench stable-ID revalidation.
It uses non-blocking `open()` and delete-on-close ownership rather than
`exec()`'s nested event loop, following Qt's `QDialog` lifecycle guidance:
<https://doc.qt.io/qt-6/qdialog.html#exec>.
Beckhoff documents that `Remove` deletes the selected I/O device from both the
tree and the configuration:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
That establishes the destructive configuration boundary, not a required
confirmation design. The question and context guard are an Embed Labs
Qt-native safety completion; no TwinCAT asset, wording, project format, or
proprietary behavior is copied.

The failure-first focused run produced 2 passes and 1 expected failure because
the old action deleted immediately and no question appeared. Final focused
runs pass 3 tests at normal scale and 3 at `QT_SCALE_FACTOR=2`. They cover the
plain-text consequence message, names containing literal `%1` and `%2`,
one-based position, default/Escape `No`, cancel state preservation, selection
drift, project close during confirmation, confirmed removal, normalized
position, complete Undo/Redo restoration, shared ActionManager identity, and
real context menus. The inspected offscreen question renders are 400 x 151 and
552 x 422 pixels, with no clipping, overlap, or scale drift.

The complete Workbench suite passes 39 tests. The six isolated EtherCAT suites
pass 90 tests: Core 17, Project 12, Devices 8, Workbench 39, Scan 7, and
Diagnostics 7. The `WITH_TESTS=OFF` product build contains all 16 allow-listed
plugin dylibs. Enabled and explicitly Workbench-disabled product processes
each remained stable far beyond 15 seconds before intentional SIGTERM target
status 15. Every executable qualification was offscreen, explicitly cleared
inherited DYLD variables, and used only the process-local Touch Bar LLDB
breakpoint. Final cleanup found no residual process, DiagnosticReports file,
or ReportCrash event after 21:48:00 on 2026-07-18.

This issue changes only existing private Workbench action, controller, test,
and documentation files. It adds no public API, Provider, thread, timer,
persistence, dependency, source file, CMake/qbs entry, network or hardware
behavior, or upstream Core, ProjectExplorer, or application path. Scan and
Diagnostics remain explicitly local Mock capabilities.

## TwinCAT-style ESI device insertion

`ISSUE-WB-INSERT-DEVICE-001` adds the primary offline insertion path to the
selected EtherCAT master. The interaction was compared with Beckhoff's
documented TwinCAT 3
[Add New Item workflow](https://infosys.beckhoff.com/content/1033/el331x/1036999947.html)
and its description of
[device revision selection](https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html).
No Beckhoff asset, icon, project format, command ID, or proprietary
implementation is copied.

Right-clicking the offline Master exposes one registered, context-only
`Add New Item...` ActionManager command. It opens a Workbench-private ESI
selection dialog built from an immutable `DeviceSummary` snapshot. The dialog
supports case-insensitive search across all identity columns. Its default view
shows only the highest imported revision for each Vendor ID/Product Code pair;
`Show Previous Revisions` exposes older revisions, and `Extended Information`
reveals Vendor ID, Product Code, Revision, and Group columns. Limited or
unsupported entries remain visible with their qualification state but cannot
be added. An empty repository has an explicit empty state and disabled Add
button.

Accepting a supported row passes the selected stable device ID and the
selected stable master ID to the Workbench controller. The existing checked
Project replacement command still owns validation, modified state,
persistence, Undo, and Redo. A stale master, missing ESI entry, or unsupported
entry is rejected without mutation; Cancel likewise leaves the project
unchanged. This keeps the original repository-side quick-add path and the new
master-side TwinCAT-style path on the same factory and Project boundary.

The current project contract does not model EtherCAT ports or a physical
connection graph, so the dialog truthfully appends at the next physical
position and does not invent a TwinCAT port selector. Multi-master routing,
online descriptions, real controller access, network protocols, and public
insertion APIs remain outside this issue.

## ESI device drag-and-drop

`ISSUE-WB-DEVICE-DND-001` adds the goal-required catalogue shortcut without
claiming it is Beckhoff's primary EtherCAT insertion interaction. Beckhoff's
official offline EtherCAT documentation describes appending a device through
the tree command and ESI-backed selection workflow:
<https://infosys.beckhoff.com/content/1033/ps2001-4810-1001/10832046859.html>.
The Workbench retains `Add New Item...` as that primary, explicit path; local
drag-and-drop is a Qt Creator convenience over the same checked operation. No
Beckhoff asset, project format, MIME format, or proprietary behavior is copied.

Only a Supported ESI catalogue row in the first tree column advertises drag.
The private MIME payload contains only its stable `NodeId`, is bounded during
decode, and supports `CopyAction` only. The sole drop target is the exact
Master of the currently active valid offline project. Drops on slaves,
inactive or stale masters, between rows, with MoveAction, or with forged
Limited/unknown IDs are rejected. An accepted drop appends at the next
physical position through the existing ESI configuration factory and checked
Project replacement command. The repository is unchanged, the new slave is
selected by stable ID, and Project modified state, persistence, Undo, and Redo
retain their existing ownership.

This shortcut does not model a port, physical connection graph, arbitrary drop
position, multi-master routing, controller/network transport, or a public
cross-plugin insertion API. Those capabilities require separate data and
provider contracts rather than reinterpretation of a tree drop position.

## ESI repository empty guidance

`ISSUE-WB-ESI-EMPTY-GUIDANCE-001` replaces the repository placeholder's stale
future-stage message with the current recovery path: select `Device Repository`,
then choose `Import ESI Files...`. The placeholder stays enabled for readable
status and tooltip presentation but remains non-selectable; its name and
guidance also remain available to the existing search role. Selecting the
parent continues through the stable repository `NodeId` to the existing General
page and its local import action.

Beckhoff documents that offline device selection is populated from available
ESI descriptions and that ESI XML contains the device descriptions needed for
offline configuration:
<https://infosys.beckhoff.com/content/1033/el331x/1036999947.html> and
<https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10831984011.html>.
That establishes the ESI prerequisite, not this exact empty-state wording or
navigation. The guidance is an Embed Labs Qt Creator usability correction; it
does not copy TwinCAT behavior or add online download, automatic update,
hardware scan, EEPROM description, network, or real EtherCAT behavior.

## ESI Device Repository General page

`ISSUE-WB-ESI-REPOSITORY-001` replaces the repository's generic two-row table
with a dedicated local device-description workflow. Its action names and
information flow were compared with Beckhoff's official description of
[ESI files in TwinCAT](https://infosys.beckhoff.com/content/1033/em7004/1036998411.html)
and the documented
[Reload Device Descriptions action](https://infosys.beckhoff.com/content/1033/el2502/19869660683.html).
No Beckhoff asset, icon, XML extension, project format, or proprietary
implementation is copied.

The page reports repository status, indexed devices, supported and limited
devices, vendors, and distinct referenced source paths. `Import ESI Files...`
opens a multi-file XML picker; local XML files may also be dropped onto the
page. `Reload Device Descriptions` reparses the descriptions already stored by
the Devices plugin. Both operations expose progress, session state, and a
read-only result including requested, imported, updated, duplicate, failed,
and canceled counts. Partial success remains visible, parser errors identify
their source file, and detailed error output is bounded to 100 entries. An
active operation can be canceled explicitly.

Workbench owns only this presentation and retains the repository and active
job through `QPointer`. The public `DeviceRepositoryProvider` continues to own
import, indexing, data, job lifetime, and cancellation semantics; repository
signals refresh the page and the existing device tree. The source-path count
is deliberately labeled `Referenced source paths`: the public contract does
not expose a separate stored-file inventory.

This is a local/offline workflow. It does not contact a controller, an ETG or
vendor website, or an online description service; it does not add XSD
management, `OnlineDescription`, description deletion/overwrite policy, a
network protocol, or a Zynq contract. It adds no public API, dependency,
persistent format, or upstream Qt Creator patch.

## ESI catalogue-device General page

`ISSUE-WB-ESI-DEVICE-GENERAL-001` replaces the generic two-column property
table shown for an individual repository device with a dedicated, scrollable,
read-only General page. Its identity order was compared with Beckhoff's
documented
[EtherCAT slave General tab](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html),
while the catalogue/source boundary follows Beckhoff's description of
[ESI device descriptions](https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10831984011.html).
No Beckhoff asset, icon, XML extension, project format, or proprietary
implementation is copied.

The identity group exposes the imported Name and Type, stable Object Id,
Vendor ID, Product Code, Revision, and Group. Offline configuration coverage
summarizes Sync Managers, RxPDOs, TxPDOs, CoE features, Startup parameters,
and DC modes. Import qualification then reports Supported or Limited status,
warning and unsupported-feature counts, and their complete details. The final
source group shows the recorded source file, SHA-256, and import time. Long
descriptions wrap, immutable values remain selectable, and the complete page
scrolls at normal and high-DPI sizes. If the selected catalogue description
has disappeared, the page shows an explicit unavailable state instead of
retaining stale values.

This is a repository description rather than a configured slave instance, so
the page does not fabricate TwinCAT instance-only Id, Comment, Disabled, or
symbol-generation values. It adds no editor, raw XML viewer, online
description/update workflow, controller access, network protocol, or public
contract. `EtherCATDevices` continues to own parsing, source preservation, and
the immutable `DeviceDescription`; Workbench only renders that public snapshot.

## Offline-project General page

`ISSUE-WB-PROJECT-GENERAL-001` replaces the project's generic property table
with a dedicated identity form and offline-configuration summary. Its
information hierarchy was compared with Beckhoff's documented TwinCAT 3
Project tab:
<https://infosys.beckhoff.com/content/1033/tc3_userinterface/3434440203.html>.
No Beckhoff logo, trademark, icon, project format, control, or proprietary
implementation is copied.

The identity form displays the editable Project name followed by the stable
Project ID and the explicit `Offline EtherCAT Engineering Project` type. The
summary reports the actual persisted format version and creator, validation
and migration state, modified state, offline target and EtherCAT-master names,
and configured-slave count. Missing or stale project contexts are labeled
`Unavailable`; the page never substitutes online or runtime values.

Completing a Project-name edit routes through the Workbench controller to the
existing checked `ProjectService::renameProject()` command. Project owns
whitespace normalization, empty-name rejection, modified state, persistence,
Undo, and Redo. The stable Project ID and selection are retained while the
Workbench tree, ProjectExplorer display name, details title, and form remain
synchronized.

TwinCAT fields that depend on a PLC/ADS runtime, including AMS Port, boot-data
encryption, autostart, symbolic mapping, multi-instance settings, and compiler
definitions, are intentionally absent. A project path is also omitted because
the current public Project service and property-page context do not expose it;
the Workbench does not reach into ProjectExplorer internals or fabricate a
value. Adding it requires a separate Project/Core contract issue. This page
adds no public API, protocol, Provider, persistent field, source-list entry,
dependency, background work, or upstream Qt Creator patch.

## Offline-target General page

`ISSUE-WB-TARGET-GENERAL-001` replaces the target's generic property table with
a dedicated form. Its tab placement and information hierarchy were compared
with Beckhoff's documented TwinCAT 3 target-system General page:
<https://infosys.beckhoff.com/content/1033/tc3_system/5206507659.html>.
No Beckhoff logo, trademark, icon, control, project format, or proprietary
implementation is copied.

The page keeps the documented top target summary, `Choose Target...` action,
and `Version` group containing Engineering, Target, Local, Project, and
`Pin Version`. A platform-standard computer icon replaces the vendor artwork.
Engineering reports the running Qt Creator product version. Project reports
the persisted local project-format version and its `createdBy` value. Target
is explicitly `Not assigned (offline)`, while Local is explicitly
`Not available (phase 1)`; neither value pretends that a runtime was detected.

The target name is the only editable value. Completing an edit routes through
the Workbench controller to the existing checked
`ProjectService::renameStructuralNode()` command. Project normalizes
whitespace, rejects an empty name, and retains modified state, persistence,
Undo, Redo, the stable target ID and selection, and synchronized tree, title,
and form text.

`Choose Target...` and `Pin Version` remain visibly unavailable with tooltips
and accessible descriptions because phase 1 has no target discovery, runtime
selection, connection, or persisted runtime version. They do not save UI-only
state. The page uses the existing `PropertyPageProvider`, Qt Creator style
metrics, and current Project snapshot; it adds no public API, network protocol,
provider, background work, dependency, source-list entry, or upstream patch.

## EtherCAT-master General page

`ISSUE-WB-MASTER-GENERAL-001` replaces the master's generic property table with
a dedicated form. Its tab placement, field order, and interaction hierarchy
were compared with Beckhoff's documented TwinCAT 3 EtherCAT-master General
page:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1569945995.html>.
No Beckhoff asset, trademark, control, project format, or proprietary
implementation is copied.

The upper form retains the documented `Name` and `Id` first row, followed by
`Object Id`, `Type`, `Comment`, `Disabled`, and `Create symbols`. An accepted
`Name` is reflected immediately in the device tree. `Id` is the one-based
master ordinal from the project snapshot, `Object Id` is the stable project
`NodeId`, and `Type` explicitly identifies an EtherCAT master. The layout uses
Qt Creator spacing, palette, font, accessibility, and high-DPI behavior rather
than fixed styling.

The current project contract does not persist Comment, Disabled, or symbol
generation. Those controls remain visibly unavailable with explanatory text
and tooltips; they do not store UI-only state or pretend that configuration was
accepted. Enabling each setting requires a separate Project/data-contract
issue. The offline summary below the reference form reports an explicit
`Not assigned (offline)` cycle-time placeholder, the actual configured-slave
count, and the shared Workbench master status. It never invents a task cycle or
online controller value.

Finishing a master-name edit routes through the Workbench controller to the
checked `ProjectService::renameStructuralNode()` command. Whitespace is
normalized by Project, empty input is rejected and restored, and accepted
changes retain modified state, persistence, Undo, Redo, stable selection, and
synchronized tree, title, and form text. The existing
`PropertyPageProvider` remains the only page integration point; no public API,
network protocol, source-list entry, background work, or upstream Qt Creator
patch is added.

## EtherCAT-master EtherCAT page

`ISSUE-WB-MASTER-ETHERCAT-001` replaces the master's former inline topology
table with a dedicated EtherCAT page. Its field placement, action order, and
cyclic-transfer table were compared with Beckhoff's documented TwinCAT 3
EtherCAT-master EtherCAT tab:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html>.
No Beckhoff asset, dialog, control implementation, project format, or
proprietary behavior is copied.

The upper region retains the documented `NetId`, `Advanced Settings...`,
`Export Configuration File...`, `Sync Unit Assignment...`, and `Topology...`
hierarchy. An ADS NetId is not part of the phase-1 project or any current
Provider, so the read-only field says `Not assigned (offline)`. Advanced
Settings, export, and Sync Unit assignment remain visible but unavailable with
tooltips and accessibility explanations. They do not save UI-only values or
pretend that an ADS route, export format, runtime task, or Sync Unit contract
exists.

`Topology...` is the one functional action because its result can be derived
entirely from the current immutable offline project. Each invocation rereads
the current master snapshot and opens a read-only table containing Position,
Name, Auto Inc Addr, Previous, Port, Vendor, Product, Revision, Alias, and
Status. Predecessors follow configured physical order. Physical ports remain
explicitly `Not modeled`, and each row is labeled `Offline configured` rather
than presenting live bus state. Removing every configured slave produces an
explicit empty dialog instead of retaining stale rows.

The lower table retains the documented Frame, Cmd, Addr, Len, WC, Sync Unit,
Cycle, Utilization, Size/Duration, and Map Id columns. It intentionally contains
zero rows and is accompanied by an explicit explanation: phase 1 has no runtime
task, frame scheduler, or Sync Unit model from which truthful cyclic frames
could be generated. No command, WKC, timing, utilization, ADS value, controller
transport, configuration export, network protocol, background worker, public
API, source-list entry, or upstream Qt Creator patch is added by this issue.

## Configured-slave General page

`ISSUE-WB-SLAVE-GENERAL-001` implements the first configured-slave identity
form. Its field hierarchy was compared with Beckhoff's documented TwinCAT 3
General page:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html>.
No Beckhoff asset, project format, control, or proprietary implementation is
copied.

The form exposes `Name`, `Id`, `Object Id`, and `Type`. `Name` is the only
editable field. `Id` is a one-based presentation of the configured slave's
offline physical order, `Object Id` is the stable project `NodeId`, and `Type`
comes from the matching ESI description. A missing ESI description is reported
as `Unknown ESI device` instead of fabricating a type. The existing identity,
Alias, ESI source, group, support, and import details remain visible in the
read-only property table below the form.

Finishing a name edit trims surrounding whitespace and sends the replacement
through the Workbench controller to the existing checked
`ProjectService::replaceOfflineSlaves()` command. Empty names are rejected and
the displayed value is restored. Accepted edits therefore retain Project-owned
validation, modified state, persistence, Undo, and Redo. The slave's stable ID,
current tree selection, child-node IDs, details title, tree label, and General
form stay synchronized across rename, Undo, and Redo.

TwinCAT's Comment, Disabled, and Create symbols fields are not represented by
the current project contract and are not simulated. Alias remains visible but
read-only here and is edited on the dedicated slave EtherCAT page. No online
value, controller transport, protocol, or public Core API is added.

## Configured-slave EtherCAT page

`ISSUE-WB-SLAVE-ETHERCAT-001` replaces the configured slave's generic
SyncManager-only view with a dedicated offline EtherCAT page. Its upper field
order and address semantics were compared with Beckhoff's documented TwinCAT 3
EtherCAT tab:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html>.
No Beckhoff asset, dialog, project format, or proprietary implementation is
copied.

The page displays `Type`, `Product/Revision`, `Auto Inc Addr`, `EtherCAT Addr`,
`Configured Station Alias`, `Identification Value`, and `Previous Port`,
followed by the existing ESI SyncManager table. Type comes from the matching
ESI description, Product/Revision comes from the persisted slave identity, and
Auto Inc Addr is deterministically derived from physical order: the first slave
is `0x0000`, the second is `0xffff`, then the value decrements for each further
slave. Missing ESI data is explicit and never invents a device type or
SyncManager.

Configured Station Alias is the only editable field. It is bounded to the
persisted 16-bit range, and zero explicitly disables the Alias. Each completed
edit routes through the Workbench controller to the existing checked
`ProjectService::replaceOfflineSlaves()` command, preserving Project validation,
modified state, persistence, stable selection, Undo, and Redo. Repository-device
views remain read-only, while the master uses its dedicated read-only topology
action and cyclic-frame schema.

The current project contract does not contain a fixed EtherCAT address,
identification-check value, or physical port graph. The corresponding fields
therefore say `Automatic at startup`, `Not configured`, or `port not modeled`.
Advanced Settings is visibly disabled. These values are not mapped onto Alias
and no online or controller state is fabricated; enabling them requires a
separate data-contract issue.

## Provider state overlays and issue navigation

`ISSUE-WB-STATE-TREE-001` adds a presentation-only overlay to the existing
offline device tree. The controller discovers available public
`ScanProvider` and `DiagnosticsProvider` objects through the Core registry and
copies only their immutable `ScanResult` and `DiagnosticsSnapshot` values into
the model. Workbench does not include either producer plugin's private headers,
invoke its widgets, or own its state machine.

The interaction follows the documented TwinCAT workflow in which a scan is
compared with the defined offline configuration and identity/revision
differences remain visible in the I/O tree and correction flow:
<https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10832129675.html>.
The error presentation was also compared with Beckhoff's documented diagnostic
states for slave error, invalid identity, missing slave, and link error:
<https://infosys.beckhoff.com/content/1033/el6752/2584310027.html>.
No Beckhoff icon, asset, binary format, or proprietary behavior is copied.

A completed slave scan displays its source explicitly as `MOCK` or `Online`.
An exact comparison reports `topology matches`. Otherwise the master reports
the total difference count, while affected configured slaves report Missing,
Position, Vendor, Product, Revision, Serial, Alias, Duplicate, PDO, or DC
differences. Added or otherwise unmapped scanned nodes aggregate at the master.
The tooltip and search role retain the complete difference summary and detail,
so a compact status never discards the reason.

A Diagnostics snapshot overlays source, RunMode, EtherCAT state, AL-status
text, active alarms, and missing-snapshot state on the same master/slave rows.
Stopped or stale data is labeled as the last observed state rather than current
online truth. Standard Creator information, warning, critical, and success
icons distinguish informational, warning, error, and matched/running states.
The underlying offline status and normal node icon are retained separately and
are restored immediately when a Provider becomes unavailable or is removed.

`Locate First Topology Difference`, `Locate First Issue`, and
`Open Diagnostics` are registered once through `Core::ActionManager`. The
EtherCAT menu, compact command strip, and tree context menu reuse those exact
actions. Locate commands clear an obstructing filter, expand every ancestor,
select the source row, and publish its stable ID through the existing selection
service. Command enabled state follows model changes; no private widget lookup
or cross-plugin `QModelIndex` is used.

## Details and property pages

The details container discovers available `PropertyPageProvider` objects from
the public registry. It sorts pages by priority and stable provider/page IDs,
restores the selected effective page key, and owns every widget returned by a
provider. On ordinary unregister, all provider-owned widgets are destroyed
before `removeObject()` returns. Self-unregister from an active page callback
uses the bounded callback-return lifetime documented below. Provider addition,
availability changes, and removal rebuild the page set without retaining
removed pointers in semantic transaction state.

The built-in provider supplies these stage-4 pages:

- dedicated editable General forms for projects, targets, masters, and
  configured slaves, a local ESI repository import/reload page, and read-only
  General pages for individual ESI devices;
- an editable Alias and read-only offline address form for configured slaves,
  while imported devices retain read-only SyncManager data and the master
  exposes its offline NetId/action hierarchy, local topology dialog, and
  explicit unavailable cyclic-frame state;
- an editable Process Data page for configured slaves, with a read-only ESI
  catalogue view for repository devices;
- a read-only, automatically focused Process Data view for Inputs, Outputs,
  RxPDO, TxPDO, PDO, and PDO Entry tree selections;
- a local-only CoE Online Mock object dictionary with explicit offline view,
  temporary configured-slave Value editing, a read-only repository catalogue,
  and an undoable Add to Startup path;
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
repository-device Process Data, CoE, Startup, and DC catalogue views remain
read-only. A missing ESI match is reported explicitly and does not invent PDO,
Startup, or DC data.

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
including Unicode engineering names. Offline views and repository-device
catalogue contexts are read-only. The repository still displays the ESI `RW`
object capability, but its Value cell does not advertise editing and rejects a
direct model edit.

The Value cell of a locally writable Mock object accepts size-checked raw
hexadecimal edits only in an existing configured-slave context. Editing only
changes the page's transient Mock value. Add to Startup requires explicit
confirmation, appends a new `PS` request without overwriting an existing
request, and submits the complete candidate through
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

Repository-device pages list all parsed ESI operation modes and let the user
select each mode for a read-only local preview. The preview updates the visible
AssignActivate and complete SYNC0/SYNC1 timing only; it creates no Project,
Undo command, persistence, controller request, or network/hardware access.
Re-entering the Device context starts again from the first ESI mode. An empty
configured-slave DC value proposes that first mode without marking the project
modified. Store/Restore ESI Defaults is explicit, and choosing another ESI
operation mode on a configured slave applies that mode's AssignActivate and
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

The command strip is owned by the lazy Mode widget. Destruction removes its
menu event filter and all toolbar associations without deleting or retaining
the ActionManager-owned actions. It introduces no timer, thread, Provider, or
cross-plugin object ownership.

The ESI repository page likewise starts work only through the public Devices
Provider and retains only guarded Provider/job pointers. Its signal receivers
are page-scoped, while the Devices plugin owns the asynchronous job and stored
descriptions. Workbench adds no file parser, worker, timer, or repository
business state.

The individual ESI General page retains no Provider, job, timer, or background
work. It receives one immutable `DeviceDescription` snapshot from the existing
Workbench controller, renders it read-only, and clears all presentation state
when the context is reset or no longer resolves.

The ESI insertion dialog is owned by the lazy Workbench Mode widget and exists
only for the modal interaction. It retains a copied `DeviceSummary` snapshot
and stable IDs; it retains no repository Provider, job, timer, model index, or
cross-plugin object after closing.

The ESI drag source and active-Master drop target are likewise Workbench-local.
The model serializes only a stable device ID into a private MIME payload; it
does not retain a source index or external object. The controller-owned drop
handler reuses the checked insertion operation and is cleared before the tree
model during shutdown. It adds no timer, Provider, background job, public
contract, or persistent owner.

Open-project and active-project lifetime remain owned by ProjectExplorer and
`EtherCATProject`. Workbench observes copied snapshots and the public active
ID only. Closing the active project lets ProjectExplorer hand activity to the
remaining project; closing all projects restores the existing no-project tree
and clears stale selection. The active marker retains no project pointer,
model index, Provider, timer, thread, or persistent state. The context-only
activation command resolves the selected stable ID at trigger time, disables
after handoff or close, and retains no extra lifecycle owner.

The controller watches optional Scan/Diagnostics availability and snapshot
signals through public Provider contracts. Provider removal is handled before
the object leaves the registry: the departing object is excluded, its copied
presentation is cleared or replaced by another available Provider, and no
pointer is retained by the tree model. Diagnostics publication may be frequent,
but `dataChanged` is emitted only for rows whose visible presentation changed.

The plugin owns no background thread, timer, future, file format, or persistent
business state. Imported ESI data remains owned by `EtherCATDevices`; open
project state remains owned by `EtherCATProject`.

## Current limits

The Workbench itself deliberately provides no real bus scan, interface
discovery, online controller state, controller connection, network protocol,
configuration package, PLC language, or code generation. Scan and Diagnostics
remain optional Mock Provider plugins. CoE Online is also a clearly labeled
local interaction Mock; it does not perform SDO information or object access.
The `Online` label is a presentation contract for a future non-Mock Provider,
not evidence that this phase contains such a Provider or controller transport.
Inputs, Outputs, RxPDO, TxPDO, and their
PDO/entry branches now render persisted, validated active process data. Actual
modular ESI profile parsing and project-side module/channel values remain a
separate Devices/data-contract issue; until such source data exists, Modules /
Channels shows an explicit empty state instead of fabricated rows. Advanced
Sync Unit timing semantics also remain pending an independent data-contract
issue. Fixed EtherCAT addresses, identification checks, physical port graphs,
and Advanced Settings are likewise not represented by the current project
contract and remain explicit read-only or unavailable states. ADS/NetId
routing, master configuration export, Sync Unit assignment, runtime task
binding, and cyclic-frame generation are also absent; the master EtherCAT page
shows those boundaries instead of generating placeholder operational data.

## Verification

The focused Workbench suite covers metadata and hard dependencies, mode/action
registration, a 500-device incremental model under
`QAbstractItemModelTester`, activation focus and Down-arrow stable selection,
filtering and two-way stable selection, real ESI
data in Process Data/Startup/DC pages, the repository import/reload/cancel
workflow, individual ESI catalogue
identity/configuration/qualification/source details and unavailable state,
configured-slave topology and ESI-page reuse, manual ESI
add/remove/reorder operations, supported ESI drag-and-drop to the active
offline Master, dynamic property-page removal, and dynamic
Scan/Diagnostics availability and removal. The process-data tree
coverage verifies the exact five-branch order, input/output direction,
active-PDO projection, unique deterministic view IDs, retained source IDs,
empty modular state, recursive filtering, derived Details routing, non-elided
content-sized navigation columns, accessible tree metadata, and 128 configured
slaves. The Process Data workflow additionally covers RxPDO/TxPDO SM selection,
read-only repository and fixed/mandatory mappings, an empty no-ESI state,
ESI-derived initial mapping, assignment and entry edits,
validation rejection, process-image refresh, and real DetailsView plus Project
Undo/Redo reentrancy. The Startup workflow covers the ESI catalogue, explicit
defaults storage, fixed requests, New/Edit/Delete dialogs, enable state,
ordering, type/value validation, manual no-ESI empty state, and real
ProjectService Undo/Redo reentrancy. The CoE Online workflow covers the
TwinCAT-inspired object hierarchy and controls, ESI/offline/Mock sources,
manual refresh, advanced and Unicode filters, offline, object-level, and
repository-device read-only boundaries, configured-slave raw-value editing,
cancelled and confirmed Add to Startup, no-overwrite behavior, no-ESI state,
Project modified state, and Undo. The repository regression preserves ESI `RW`
metadata while rejecting both the editable item flag and direct proxy-model
`setData()`, then switches configured-slave / repository contexts in both
directions to reject stale permissions. Its model is also checked by
`QAbstractItemModelTester`, and the focused flows pass at
`QT_SCALE_FACTOR=2`.
The DC workflow covers two ESI operation
modes, explicit Store/Restore, manual no-ESI configuration, AssignActivate,
SYNC0/SYNC1 enable and nanosecond timing, reference-clock selection, validation
rejection, dependent disable actions, and ProjectService Undo/Redo reentrancy.
The status-surface coverage verifies Offline/Busy/Error priority, Mock labels,
tooltip and drop-down details, standard icon dimensions, content width,
Mode-scoped visibility, and the existing configured/unsupported tree icons.
The command-strip coverage verifies exact shared-action identity, source-menu
ordering, Open-Workbench exclusion, dynamic add/remove behavior, complete
tooltips, standard icon metrics, and all optional-plugin load combinations.
The tree-command coverage opens real popup menus and verifies exact QAction
identity for the eight navigation commands and five offline-topology commands,
shared Expand/Collapse navigation buttons, context-only exclusion from the
command strip, unsupported-device location, stable Node ID copying, placeholder
protection, and enabled-state updates. The topology workflow verifies complete
ESI Process Data/Startup/DC defaults, stable IDs, repeated-device unique names,
position normalization, boundary states, selection repair, and Project
Undo/Redo. The project General workflow verifies the TwinCAT-aligned identity
hierarchy, actual format/creator/validity/migration/modified/topology summary
values, Unicode rename trimming, empty rejection, stale-context handling,
stable selection, synchronized Workbench/ProjectExplorer/title/form updates,
and Project Undo/Redo. The target General workflow verifies the
TwinCAT-aligned target summary and Version hierarchy, actual
engineering/project values, explicit
offline and unavailable runtime values, unavailable target selection/version
pinning, Unicode rename trimming, empty rejection, stable selection,
synchronized tree/title/form updates, and Project Undo/Redo. The
configured-slave General workflow verifies the four identity fields,
ESI-derived and missing-ESI type states, Unicode rename trimming, empty name
rejection, stable selection, synchronized tree/title/form updates, and Project
Undo/Redo. The master General workflow verifies the TwinCAT-aligned field order,
one-based Id, stable Object Id, explicit unavailable settings, cycle
placeholder, actual slave count, shared status, Unicode rename trimming, empty
rejection, stable selection, synchronized tree/title/form updates, and Project
Undo/Redo. The configured-slave EtherCAT workflow verifies ESI type,
Product/Revision, first/second Auto Inc Addr values, configured predecessor,
bounded Alias editing and disable value, modified state, stable selection,
Undo/Redo, missing-ESI behavior, read-only repository behavior, retained master
topology, and retained SyncManager data. The master-side insertion workflow
verifies the registered command in the real Master popup, latest-revision
default, previous-revision and extended-information controls, search,
supported/limited/empty states, explicit stable master/device IDs, cancel and
error no-mutation paths, append identity/defaults, stable selection, and
Project Undo/Redo.
The ESI drag-and-drop workflow verifies the real navigation view and proxy
model configuration, CopyAction-only stable-ID MIME, Supported/Limited
qualification, exact active-Master targeting, rejected forged and misplaced
drops, unchanged repository state, append defaults, stable selection, and
Project Undo/Redo. The focused flow passes at normal and
`QT_SCALE_FACTOR=2`.
The master EtherCAT workflow verifies the TwinCAT-aligned NetId and four-action
hierarchy, disabled unsupported actions and accessibility descriptions, all ten
cyclic-frame headers, the explicit zero-row runtime boundary, current-snapshot
topology population, derived auto-increment address and predecessor, explicit
unmodeled port state, identity/Alias/status values, and the empty topology after
all slaves are removed.
The provider-state coverage uses `QAbstractItemModelTester` and verifies exact
match/difference routing, Missing/Added/Revision/Vendor presentation, warning
and critical icons, full-detail filtering, MOCK Run/OP and SAFEOP/error states,
stable locate/open navigation, command registration, and provider-removal
restoration. It passes 34 tests on the qualified Qt 6.11.0 Release test build.
The navigation keyboard-focus test passes at normal scale and
`QT_SCALE_FACTOR=2`, with 3 passed and 0 failed at each scale. It verifies the
container focus proxy, actual application focus, a real Down-arrow event, and
the resulting stable `NodeId` publication. The navigation-filter empty-state
test passes with 3 tests at both normal scale and `QT_SCALE_FACTOR=2`. It covers
the no-match stack, translated accessibility metadata, a real Space-key clear,
dynamic proxy insertion/removal, visible focus-proxy switching, long Unicode
input, exact stable selection preservation, and external-selection recovery.
Its normal 420 x 480 and 2x 840 x 960 direct renders were inspected without
clipping, overlap, or scale drift. The ESI repository empty-guidance test also
passes with 3 tests at normal scale and `QT_SCALE_FACTOR=2`. It verifies the
exact Display, Status, Search, and tooltip text, the non-selectable placeholder,
stable parent routing, and the real enabled `Import ESI Files...` action. Its
normal 720 x 360 and 2x 1440 x 720 navigation renders were inspected without
clipping, overlap, or scale drift. The active-project lifecycle test passes
with 3 tests at both normal scale and `QT_SCALE_FACTOR=2`. It opens two real
projects and verifies exact status-role consistency, explicit activation,
selection/Details preservation without a model reset, dynamic filter handoff,
active-Master drop-target handoff, active-project close fallback, and final
no-project cleanup. Its normal 720 x 360 and 2x 1440 x 720 direct navigation
renders show both project roots and the textual active state without clipping,
overlap, or scale drift. The focused set-active-project command test also passes
with 3 tests at normal scale and `QT_SCALE_FACTOR=2`. It verifies inactive-root
menu inclusion, active/non-Project/empty exclusion, ActionManager identity,
menu/command-strip exclusion, stable-ID activation, exact Selection and Details
preservation, no model reset, persistent indexes, unchanged Project snapshots,
filter/drop-target handoff, real popup-proxy triggering in Workbench and Edit
mode navigation contexts, active-close fallback, and final cleanup. Its
recorded direct Qt menu renders are 504 x 376 and 1008 x 752, with the project
command in a separate group and no clipping or scale drift. The complete
Workbench suite passes 37 tests on the `QT_QPA_PLATFORM=offscreen`
qualification path; no complete-suite 2x run is claimed. Offscreen execution is
the default automated path so test fixtures do not take desktop focus; no
desktop interaction is claimed for this issue.
The project, target, and master General flows also pass at
`QT_SCALE_FACTOR=2`, and direct normal and 2x widget renders show no overlap,
clipping, or uncontrolled expansion.
The focused configured-slave General and EtherCAT workflows pass at both normal
scale and `QT_SCALE_FACTOR=2`; the combined command-strip and offline-topology
flow also passes at `QT_SCALE_FACTOR=2`.

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

A separate 2520 x 1400 direct Qt render loaded both optional plugins and
inspected the final compact command strip. Refresh, tree expansion, Mock
Diagnostics, Config/FreeRun/Run, alarm, Mock Scan, comparison, acceptance, and
cancel commands all remained visible on one row with normal Creator icon
scale. No text compression or overlap remained. The current desktop was not
manually clicked, so this is a render inspection rather than a manual
interaction claim.

A direct 2400 x 1600 Retina render of the provider-state tree was inspected
with both Mock Providers active. The master displayed Run/OP, error, total
difference count, and Added state; configured slaves displayed Revision and
SAFEOP/Error plus Missing state; the Diagnostics branch displayed Running and
Error. Standard warning/critical icons aligned with normal tree icons, and the
full two-column status remained readable without clipping or overlap.

Direct normal and `QT_SCALE_FACTOR=2` renders inspected the ESI Device
Repository page at a 1100 x 760 logical test size. The 1100 x 760 and
2200 x 1520 outputs retained the title, truthful offline description, six-row
summary, import/reload/cancel controls, progress, partial-success counts, and
parser error without overlap, clipping, or scale drift. These were offscreen Qt
Widget renders; no manual desktop interaction is claimed for this issue.

Direct normal and `QT_SCALE_FACTOR=2` renders also inspected the individual
ESI catalogue-device General page at a 1100 x 760 logical test size. Top and
bottom captures at 1100 x 760 and 2200 x 1520 retained the identity,
configuration coverage, complete CoE details, qualification warnings,
unsupported-feature details, source path, SHA-256, and import time without
overlap, clipping, truncation, or scale drift. These were offscreen Qt Widget
renders; no manual desktop interaction is claimed for this issue.

Direct normal and `QT_SCALE_FACTOR=2` renders inspected the ESI device
selection dialog at a 1000 x 640 logical test size. The 1000 x 640 and
2000 x 1280 outputs retained the description, search field, revision and
extended-information controls, identity columns, qualification icons, selected
status, and Add/Cancel actions without overlap, clipping, or scale drift. These
were offscreen Qt Widget renders; no manual desktop interaction is claimed for
this issue.

A direct 522 x 472 Retina render inspected the registered configured-slave
context menu at `QT_SCALE_FACTOR=2`. The original seven tree commands and the
three slave Remove/Move commands retained their order and grouping; unavailable
locators and the boundary `Move Offline Slave Down` command were visibly
disabled. Every label remained readable without clipping or overlap. The
offscreen macOS menu style omitted action icons by platform policy; QAction icon
presence remains covered by the widget test. The locked desktop prevented a
manual click inspection, so no manual-interaction result is claimed.

A direct 2200 x 1440 Retina render inspected the configured-slave General page.
The renamed Unicode title and Name field, one-based Id, stable Object Id,
ESI-derived Type, and complete read-only property table remained visible
without overlap or clipping. This was an offscreen Qt Widget render; no manual
desktop interaction is claimed for this issue.

A direct 2200 x 1440 Retina render inspected the offline-project General page.
The editable Project name, stable Project ID, explicit offline project type,
format and creator values, validation/migration/modified state, target/master
summary, and configured-slave count remained visible without overlap or
clipping. The same flow was inspected at its normal 1100 x 720 render size.
These were offscreen Qt Widget renders; no manual desktop interaction is
claimed for this issue.

A direct 2200 x 1440 Retina render inspected the offline-target General page.
The editable target name, stable Object Id, standard computer icon, disabled
Choose Target action, Engineering/Target/Local/Project version rows, and
disabled Pin Version state remained visible without overlap or clipping. The
same flow was inspected at its normal 1100 x 720 render size. These were
offscreen Qt Widget renders; no manual desktop interaction is claimed.

A direct 2200 x 1520 Retina render inspected the configured-slave EtherCAT
page. Type, Product/Revision, Auto Inc Addr, the explicit automatic fixed-address
state, editable Configured Station Alias, unavailable identification/port
states, disabled Advanced Settings, and both SyncManager rows remained visible
without overlap or clipping. This was an offscreen Qt Widget render; no manual
desktop interaction is claimed for this issue.

Direct normal and `QT_SCALE_FACTOR=2` renders inspected the EtherCAT-master
EtherCAT page at its 1180 x 760 logical test size and the populated topology
dialog. The NetId field, four-action stack, explicit cyclic-frame state, all ten
frame headers, and all ten topology columns remained readable without overlap,
clipping, or scale drift. These were offscreen Qt Widget renders; no manual
desktop interaction is claimed for this issue.

## Details empty-state lifecycle

`ISSUE-WB-DETAILS-EMPTY-LIFECYCLE-001` makes the integrated right-hand Details
area distinguish two previously conflated states:

- with no open EtherCAT project, it explains that the user can create or open
  an EtherCAT `.ecatproject`, or select Device Repository to inspect local ESI
  descriptions;
- with an open project but no current tree selection, it asks the user to
  select an EtherCAT node to inspect its offline details.

The first state intentionally points to Qt Creator's existing project
workflow. [Qt Creator documents `File > New Project` as the project-wizard
entry](https://doc.qt.io/qtcreator/creator-how-to-use-project-wizards.html) and
[`File > Open Project` as an existing-project
entry](https://doc.qt.io/qtcreator/creator-project-opening.html). Workbench does
not register duplicate create/open actions or own project files. Device
Repository remains a local ESI inspection path and does not imply a controller
connection.

`DetailsView` still consumes only the public `ProjectService::projects()`
snapshot and stable Selection Service `NodeId`. When the private Workbench tree
model resets after project add/remove, an empty Details context is rebuilt from
the current public project list. Selecting a valid node still routes to the
existing provider pages; clearing the selection restores the open-project
guidance; closing the last project restores the no-project guidance. The
provider-unsupported and Diagnostics-unavailable states are unchanged.

The Details container, dynamic title, guidance label, and property tabs now
carry translated accessible names and descriptions. This uses the standard
[`QWidget::accessibleName` and `accessibleDescription`
contract](https://doc.qt.io/qt-6/qwidget.html); no custom accessibility plugin
or platform-specific implementation is added. Beckhoff's published EtherCAT
subscriber workflow likewise presents device-dependent property tabs after a
device selection, including General, EtherCAT, Process Data, and Online for a
simple terminal, but it does not prescribe this product's empty-state text:
[Beckhoff EtherCAT subscriber configuration](https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html).

The dedicated lifecycle test covers no project, project open with no
selection, valid project selection, cleared selection, final project close,
visibility, and accessibility metadata. It passes with 3 tests at normal scale
and 3 tests at `QT_SCALE_FACTOR=2`. Direct 1800 x 1200 Retina renders of both
empty states were inspected without clipping, overlap, or scale drift. The
complete Workbench suite now passes 36 tests, and the six isolated EtherCAT
plugin suites pass 87 tests. All executable qualification runs use
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, and `-no-crashcheck`;
no desktop interaction is claimed.

## Optional Provider availability and Mock identity

`ISSUE-WB-OPTIONAL-PROVIDER-STATE-001` removes an inaccurate capability state
from the device tree and integrated Details area. Workbench previously reduced
each optional Scan or Diagnostics capability to one `available` boolean. Both
an absent Provider and a registered-but-unavailable Provider therefore appeared
as `Plugin not installed`, while an available Provider appeared only as
`Provider available` or `Ready to scan` and discarded the producer's explicit
Mock name.

The Workbench-private presentation now retains three states for each capability:

- `Absent`: no Provider of that kind is registered; the tree says that no
  Provider is registered and that V1 is local Mock only;
- `Unavailable`: a Provider is registered but none is currently available;
- `Available`: at least one registered Provider is available.

For the latter two states, Workbench displays the deterministically selected
Provider's public `displayName`. The production providers publish `Local Mock
EtherCAT scanner` and `Local Mock EtherCAT diagnostics`, so the ready and
unavailable states remain visibly Mock without teaching Workbench producer IDs
or private implementation types. A whitespace-only public name is normalized
to the same neutral `Unnamed Scan Provider` or `Unnamed Diagnostics Provider`
fallback before any tree, Details, tooltip, search, or accessibility projection;
Workbench does not invent a Mock identity when the Provider did not publish one.
If several typed Providers are available,
Workbench selects the same producer for both the copied result/snapshot and the
visible name: a Scan result wins over an active scan, failure, or idle state;
a Diagnostics snapshot wins over an active stream or stopped state; stable
Provider ID breaks ties. If no typed Provider is available, the lowest stable
registered Provider ID supplies the unavailable name. The controller commits
the copied presentation to its private tree model in one refresh, so a removed
or newly active producer cannot leave another producer's name beside stale
data. `scanAvailable()` and `diagnosticsAvailable()` remain the existing
convenience booleans for action and page enablement.

This follows Qt Creator's documented object-pool extension pattern: registered
objects have distinct add and about-to-remove signals, letting clients stop
using an object before it is unregistered
([Qt Creator object pool](https://doc.qt.io/qtcreator-extending/pluginmanager.html)).
Qt Creator also documents installation and activation as separate extension
states ([activate extensions](https://doc.qt.io/qtcreator/creator-how-to-load-extensions.html)),
so Workbench no longer infers installation state from the absence of a public
Provider. It observes only the existing `ProviderRegistry`, `isAvailable()`,
`displayName`, and their signals; it does not inspect `PluginSpec` or add an
installation UI.

The built-in Online and Diagnostics placeholders now distinguish absent and
unavailable Diagnostics Providers and state that no controller protocol or live
hardware state is present. An available Diagnostics Provider that contributes
no property page gets a truthful no-page message rather than an installation
claim. The tree, status/search roles, tooltip, page summary, and accessibility
description consistently project the same Provider state and name. A
Diagnostics request that has started but has not produced a snapshot includes
the selected Provider name and a source-neutral Diagnostics state. A separate
`MOCK` or `Online` source label appears only after the copied snapshot supplies
its `mock` value. Provider availability, activity, result/snapshot,
display-name, removal, and re-addition changes update the presentation without
a model reset. A pure name/source refresh updates existing Details text without
destroying its page widgets; availability changes still rebuild the page set
when required. Copied scan/diagnostic overlays are atomically replaced or
cleared through the same refresh path.

Beckhoff's official workflow defines offline configuration as having no physical
I/O, while `Scan Devices` discovers available I/O. Connecting to a remote target
is an optional preparation step when the controller is not local
([offline to online configurations](https://infosys.beckhoff.com/content/1033/tc3_automationinterface/242741643.html)).
Its Online tab displays actual master/slave and frame state only after a target
connection ([EtherCAT Online tab](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446518411.html)).
The local three-state presentation borrows only that clear information boundary:
it does not reproduce Beckhoff assets, scan hardware, read frames, or imply that
`Available` means a controller is online.

The failure-first focused test compiled and reported the old actual value
`Plugin not installed` against the new absent-state contract, producing 2 passes
and 1 failure. After implementation, the focused lifecycle test passes 3 tests
at normal scale and 3 tests at `QT_SCALE_FACTOR=2`. It covers absent,
registered/unavailable, available without a snapshot, public display-name
changes and blank-name fallback without Details-page reconstruction,
available-without-page guidance,
multi-Provider Scan result and Diagnostics snapshot selection, the
pre-snapshot source-neutral state, stable-ID tie-breaking, removal and re-addition,
fallback-page restoration, stale-overlay cleanup, Search/ToolTip text, stable
selection, and accessibility descriptions. Direct 900 x 600 and 1800 x 1200
offscreen renders of the unavailable Diagnostics page were inspected without
clipping, overlap, or scale drift.

The complete Workbench suite now passes 37 tests. The six isolated suites pass
88 tests: Core 17, Project 12, Devices 8, Workbench 37, Scan 7, and Diagnostics
7. The 16-plugin `WITH_TESTS=OFF` product build and 12-second enabled/disabled
lifecycle checks pass, followed by intentional SIGTERM target status 15. Because
this macOS build's Core initializes Touch Bar even for the offscreen QPA,
executable qualification explicitly clears inherited DYLD variables and uses a
process-local LLDB breakpoint to return from
`Utils::TouchBar::setApplicationTouchBar()`. The breakpoint is debugger state,
not a repository file, product dependency, or normal-launch change. A discarded
weak-symbol interposer had propagated to arm64e `clang`/`clang++` child probes;
their reports named Embed Labs as the responsible parent and caused the
misleading macOS crash dialog. No qualifying run uses that interposer. The clean
breakpoint runs left no process and generated no ReportCrash event or diagnostic
report after 08:14:00 on 2026-07-18. This issue adds no thread, timer, future,
Provider, public API, persistence, dependency, source file, CMake/qbs change,
upstream Core/ProjectExplorer/app path, network access, or physical-hardware
claim.

## Invalid-project presentation and lifecycle

`ISSUE-WB-INVALID-PROJECT-PRESENTATION-001` prevents a corrupt
`.ecatproject` from appearing as a successfully loaded offline topology.
`EtherCATProject` already publishes a public `ProjectSnapshot` with
`valid == false` and the real parser error, but its recovery snapshot also
contains newly generated Project, Target, and Master IDs. Workbench now treats
only the file-derived project name as display identity and does not present the
generated recovery topology as file data.

An invalid project root has the explicit status
`Invalid project | Offline data unavailable`, a standard Creator critical
icon, and the real load error in its tooltip and searchable text. It is also an
eligible result for `Locate First Issue`. Its only child is the enabled but
non-selectable `Project configuration unavailable` recovery row, with the
instruction `Fix the project file and reopen it`. No fallback Target, Master,
slave, Diagnostics node, or drop target is created. `Copy Node ID` is disabled,
omitted from the invalid-root context menu, and guarded in the copy slot, so
the generated recovery Project ID cannot be copied through the Workbench
Node-ID action or shown as a persisted Project ID.

Selection and activation remain separate. Selecting an invalid project still
opens its General page, but the Workbench `Set as Active Project` command is
disabled and its direct controller path returns an explicit invalid-project
error. If ProjectExplorer has independently made the invalid project the
startup project, Workbench reports both facts as
`Active project | Invalid project | Offline data unavailable`; it does not
silently activate another project. Closing the invalid project clears its
stable selection and Details context while leaving another valid project's
tree and active state intact.

The General page keeps the file-derived display name read-only and shows the
full parser error in Validity. Project ID, format, creator, migration,
modified state, Target, Master, and configured-slave count are `Unavailable`
because parsing did not establish those values. A later valid selection
restores the existing editable and read-only field states normally.

This follows Qt Creator's separation between the current project tree and the
explicit `Set as Active Project` operation
([Qt Creator Projects view](https://doc.qt.io/qtcreator/creator-projects-view.html)).
Qt Creator 20.0's own Project model also adds project issues to the tooltip and
uses a standard issue icon
([Qt Creator 20.0 Project model](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L247-L281)).
The Workbench critical severity and exact recovery wording are local product
decisions, not upstream requirements.

Beckhoff documents that configured or scanned I/O devices are represented in
the I/O tree and that device state information is explicit
([Adding an I/O Device](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html),
[EL6752 state](https://infosys.beckhoff.com/content/1033/el6752/2584310027.html)).
Hiding generated fallback topology is a Workbench inference from that truthful
information boundary; Beckhoff does not prescribe this product's placeholder
or text.

The failure-first focused run compiled the new integration test against the
old implementation and produced 2 passes and 1 failure: the invalid root's
actual status was `Offline`. After implementation, the focused test passes 3
tests at normal scale and 3 at `QT_SCALE_FACTOR=2`. It uses
`QAbstractItemModelTester`, opens one real valid and one corrupt project,
checks error search/tooltip/icon/first-issue behavior, the absent fallback
topology and ID-copy path, General invalid-to-valid restoration, activation
command rejection, external active state, close cleanup, and the unaffected
valid project. The source model is checked from pre-open through final close;
the proxy model is checked from construction through filtering, later active
state, and close changes. Qt documents that the tester checks model
consistency after changes but does not replace destructive lifecycle tests
([QAbstractItemModelTester](https://doc.qt.io/qt-6/qabstractitemmodeltester.html)).

The normal and 2x offscreen tree and Details renders are 900 x 600 and
1800 x 1200. They retain the complete invalid status, recovery instruction,
parser error, and `Unavailable` fields without overlap, clipping, or scale
drift. The complete Workbench suite passes 38 tests; the six isolated EtherCAT
suites pass 89 tests: Core 17, Project 12, Devices 8, Workbench 38, Scan 7,
and Diagnostics 7. The current 16-plugin `WITH_TESTS=OFF` product build and
over-15-second enabled/disabled lifecycle checks pass. Every qualifying
product or test executable run is offscreen, clears inherited DYLD variables,
and uses only the process-local Touch Bar LLDB breakpoint. The final checks
found no residual process, DiagnosticReports file, or ReportCrash event after
09:15:00 on 2026-07-18.

The corrupt-project fixture reaches a pre-existing nonfatal ProjectExplorer
TaskHub soft assertion because EtherCATProject's existing load-error task has
an empty TaskHub category. It does not fail or abort the test and is not caused
or hidden by this Workbench change; Project and TaskHub remain outside this
frozen issue. This issue adds no thread, timer, future, Provider, public API,
persistence, dependency, source file, CMake/qbs change, upstream
Core/ProjectExplorer/app path, network access, or physical-hardware claim.

## Project-scoped Diagnostics navigation

`ISSUE-WB-DIAGNOSTICS-CONTEXT-001` closes a cross-project navigation gap in
the private Workbench command path. With one valid and one invalid
`.ecatproject` open, selecting the invalid root used to leave `Open
Diagnostics` enabled and its direct signal could fall back to the valid
project's Diagnostics branch. The action therefore presented a result that
did not belong to the selected project.

The action updater now resolves the current stable `NodeId` through the
existing `SelectionService` and derives its `PropertyPageContext`. For a
non-null project ID, it enables the command only when
`diagnosticsForProject(context.projectId)` has an exact match; a null project
ID follows the projectless rule below. The tree context menu and direct
navigation path use the same rule. An invalid project has no Diagnostics
branch, so the command is disabled and direct execution leaves both selection
and current tree row unchanged. A valid project opens its own Diagnostics
branch. A genuinely projectless selection retains the existing null-project
lookup, which opens the first available Diagnostics branch; this is distinct
from a non-null invalid project ID. A non-null `NodeId` that is not present in
the Workbench model is also disabled and cannot reuse a stale tree row; only a
genuinely null selection or a known projectless node gets projectless
fallback. A temporary context menu on an unselectable placeholder is disabled
while open, then restores the stable selection and action state when it
closes. The action updater also treats a missing `SelectionService` during
controller teardown as disabled state.

This matches Qt Creator's general rule that actions in the Projects view act
on the selected tree item and project
([Qt Creator Projects view](https://doc.qt.io/qtcreator/creator-projects-view.html)).
Qt Creator 20.0 also keeps current-project action state separate from
startup-project action state in its local ProjectExplorer implementation.
Beckhoff exposes its Online tab for the selected EtherCAT device in the I/O
tree
([EtherCAT Online tab](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446518411.html)).
The exact cross-project guard is a local Workbench inference from those
context boundaries; it does not add or imply an online target connection.

The failure-first focused run compiled the new assertions against the old
implementation and produced 2 passes and 1 failure because the base `Open
Diagnostics` action remained enabled for the invalid project. Final focused
runs pass 3 tests at normal scale and 3 at `QT_SCALE_FACTOR=2`. They verify
disabled base/context actions and guarded direct invocation for the invalid
project, disabled/no-op behavior for an unknown non-model `NodeId`, exact
routing to the non-first of two valid projects, retained projectless fallback,
and placeholder-menu state restoration. Direct tree and Details renders at
900 x 600 and 1800 x 1200 were inspected without clipping, overlap, or scale
drift.

Independent diff review then found that an unknown non-model `NodeId` could
leave the old tree row selected. Its added regression first produced 2 passes
and 1 failure because clearing that row emitted a selection change that rewrote
the unknown ID as a genuinely null selection. Blocking that feedback only
while `SelectionService` drives the tree clear preserves the unknown ID for
the command guard; the final focused runs include this path.

Staged review then extended placeholder-menu restoration from Diagnostics to
every node-specific navigation action, including `Copy Node ID`. The first
complete-suite run still contained the older expectation that copy remained
disabled. That assertion returned before its manual widget cleanup, so the
next test's selection update reached the leaked test widget after its
controller model had been destroyed; LLDB caught the resulting
`EXC_BAD_ACCESS`. Updating the expectation to the restored action and tree row
lets cleanup complete. The final focused command test and complete suite both
exit 0, and this test-only early-return path generated no crash report.

The first post-fix focused run passed the functional assertions but LLDB then
caught an `EXC_BAD_ACCESS` while action state was refreshed during plugin
shutdown. A null guard for the already-released `SelectionService` was added;
all final focused and suite tests exit 0. The complete Workbench suite passes
38 tests and the six isolated EtherCAT suites pass 89: Core 17, Project 12,
Devices 8, Workbench 38, Scan 7, and Diagnostics 7. The 16-plugin
`WITH_TESTS=OFF` product build passes. Enabled and explicitly disabled product
runs had no unexpected crash and each stayed stable beyond 15 seconds before
intentional SIGTERM target status 15.

All executable qualification remained offscreen, cleared inherited DYLD
variables, and used only the process-local Touch Bar LLDB breakpoint. The
enabled settings path was
`/private/tmp/embed-labs-diag-context-product-enabled-commit.qdSCy8/settings`;
the disabled path was
`/private/tmp/embed-labs-diag-context-product-disabled-commit.yzNE4o/settings`.
Final cleanup found no residual Embed Labs or LLDB process and no new
DiagnosticReports or ReportCrash event after 18:55 on 2026-07-18. No visible
main window, interposer, repository hook, network, or hardware access was used.
This issue adds no public API, model role, persistence, dependency, source
file, CMake/qbs change, or upstream Core/ProjectExplorer/app path.

## Tree-row accessibility

`ISSUE-WB-TREE-ROW-A11Y-001` completes the standard accessibility contract for
individual items in the Workbench device tree. The navigation widget already
had a translated accessible name and description, but its private item model
returned no row-level `Qt::AccessibleTextRole` or
`Qt::AccessibleDescriptionRole`. Assistive technology therefore could not rely
on the model to obtain the focused cell's name, status, parser error, Mock
Provider state, scan/diagnostic details, or ESI identity.

Each model cell now publishes its current Display text through
`AccessibleTextRole`. `AccessibleDescriptionRole` reuses the complete truthful
tooltip projection: node name, current status, presentation details,
Vendor/Product/Revision identity, and applicable offline drag/drop guidance.
It does not infer information from an icon or add a controller state. Active
project changes and Scan/Diagnostics presentation changes explicitly include
both accessibility roles in `dataChanged`; existing device and drop-target
updates use an empty roles list, which Qt defines as all roles changed.

This follows Qt's standard item-data roles for screen-reader text and item
descriptions:
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Qt also documents the
`dataChanged` empty-role meaning:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#dataChanged>. Beckhoff's I/O
documentation keeps device status/control information in the tree and
documents explicit operational and error states:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html> and
<https://infosys.beckhoff.com/content/1033/el6752/2584310027.html>. Those pages
do not prescribe Qt accessibility roles. The role mapping is an Embed Labs
Qt-native implementation; no Beckhoff asset, wording contract, project format,
or proprietary behavior is copied.

The failure-first focused run produced 2 passes and 1 expected failure because
the Master row returned an empty accessible text. That first assertion was
initially reached after test Providers were registered, so its early return
skipped their normal removal and LLDB contained a test-fixture shutdown access
fault. The final test places the base assertion before registration and adds a
scope cleanup guard, so later assertion failures cannot leave those Providers
in the object pool. The event generated no DiagnosticReports or ReportCrash
entry.

Final focused runs pass 3 tests at normal scale and 3 tests at
`QT_SCALE_FACTOR=2`. They cover name/status cells, full scan and Diagnostics
details, Provider removal, source-model role notifications, the real navigation
proxy, and Mock/offline boundaries. The 1200 x 800 and 2400 x 1600 offscreen
tree renders retain the same readable hierarchy, icons, long textual status,
and horizontal-scroll behavior without overlap or scale drift. These automated
checks prove the Qt model contract; no manual VoiceOver speech result is
claimed.

The complete Workbench suite passes 38 tests and the six isolated EtherCAT
suites pass 89: Core 17, Project 12, Devices 8, Workbench 38, Scan 7, and
Diagnostics 7. The `WITH_TESTS=OFF` product build contains all 16 allow-listed
plugin dylibs. Enabled and explicitly Workbench-disabled product runs stayed
stable beyond 15 seconds before intentional SIGTERM target status 15. Every
qualifying executable ran offscreen, cleared inherited DYLD variables, used
only the process-local Touch Bar LLDB breakpoint, and created no visible
window. Final cleanup found no residual process, DiagnosticReports file, or
ReportCrash event after 20:02 on 2026-07-18.

This issue changes only the existing private tree model, Workbench tests, and
documentation. It adds no public API, custom role, source file, Provider,
thread, timer, persistence, dependency, CMake/qbs entry, network or hardware
behavior, or upstream Core/ProjectExplorer/application change.

## Project-scoped Locate navigation

`ISSUE-WB-LOCATE-CONTEXT-001` keeps `Locate First Topology Difference` and
`Locate First Issue` inside the project that owns the current stable Workbench
selection. Previously both commands searched the complete tree. If Alpha had a
Mock scan difference or issue while the user was inspecting clean project Beta,
the shared menu, command-strip, and context actions remained enabled and moved
the tree selection plus integrated Details area from Beta to Alpha.

The private tree queries now accept an optional project ID. A known selection
with a non-null project ID searches that exact project only. If it has no
matching result, both the base and Workbench-context actions are disabled and a
direct controller request is a no-op. A genuinely null selection or a known
projectless Device Repository/ESI selection retains the existing global
first-available lookup. An unknown non-null stable ID is not projectless: it
clears the stale tree row, disables both commands, and remains unchanged after
direct requests. A placeholder context menu temporarily restricts the actions,
then restores the stable selection, tree row, and project-scoped action state
when it closes. A released Selection Service during shutdown also leaves both
actions disabled.

This follows Qt Creator's documented Projects-view convention that common
actions come from the selected tree item and project:
<https://doc.qt.io/qtcreator/creator-projects-view.html>. The local Qt Creator
20 ProjectExplorer implementation likewise resolves its current project from
the focused Project tree instead of substituting the startup project. Beckhoff
documents that its EtherCAT Online tab becomes available for the EtherCAT
device selected in the I/O tree:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446518411.html>, and
that comparative scans operate on a specified EtherCAT device:
<https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10832129675.html>.
The exact `projectId` guard is an Embed Labs Qt-native multi-project correction;
Beckhoff does not define this private action API, and no TwinCAT asset or
proprietary behavior is copied.

The failure-first focused run produced 2 passes and 1 expected failure because
the topology-difference action remained enabled while clean Beta was selected.
Final focused runs pass 3 tests at normal scale and 3 at
`QT_SCALE_FACTOR=2`. They cover scoped model lookup, disabled base/context
actions, guarded direct requests, exact Alpha routing, unknown non-null IDs,
null and known-projectless fallback, placeholder-menu restriction/restoration,
stable tree/Details context, and real two-project open/close cleanup. Direct
900 x 600 and 1800 x 1200 offscreen tree and Details renders retain both
projects, complete Mock/offline status, and the selected clean project without
clipping, overlap, or scale drift.

The complete Workbench suite passes 39 tests. The six isolated EtherCAT suites
pass 90 tests: Core 17, Project 12, Devices 8, Workbench 39, Scan 7, and
Diagnostics 7. The `WITH_TESTS=OFF` product build contains all 16 allow-listed
plugin dylibs. Enabled and explicitly Workbench-disabled product runs stayed
stable beyond 15 seconds before intentional SIGTERM target status 15. Every
qualifying executable ran offscreen, explicitly cleared inherited DYLD
variables, and used only the process-local Touch Bar LLDB breakpoint. Final
cleanup found no residual process, DiagnosticReports file, or ReportCrash event
after 20:45:00 on 2026-07-18.

This issue changes only existing private Workbench tree-model, navigation,
action-setup, test, and documentation files. It adds no public API, model role,
source file, Provider, thread, timer, persistence, dependency, CMake/qbs entry,
network or hardware behavior, or upstream Core/ProjectExplorer/application
change. Scan and Diagnostics remain explicitly local Mock capabilities.

## Details Provider-removal tab continuity

`ISSUE-WB-DETAILS-PROVIDER-REMOVE-TAB-CONTINUITY-001` keeps a still-valid
Details page selected when an unrelated dynamic `PropertyPageProvider` is
removed. Previously the about-to-remove handler synchronously destroyed every
page and queued a normal rebuild. The rebuild then had no current widget from
which to recover the private `EtherCAT.PageKey`, so it selected the first
built-in page, normally General, even though the selected node and current
Provider B page were both still valid.

At that issue's baseline, Details recorded the departing Provider's value-only
ID, disconnected its availability signal, and captured the current PageKey,
stable context NodeId, and monotonic rebuild generation before destroying
provider-owned widgets. The current ProviderRegistry contract is stronger: it
unlinks the Provider from registry enumeration before emitting
`providerAboutToBeRemoved`, while the object remains in the PluginManager pool
until that notification returns. The departing ID still protects already
queued work and later re-registration of the same ID. Ordinary hosted pages are
detached and destroyed before `removeObject()` returns. If a Provider
self-unregisters inside its own active page callback, its page is removed from
the live-page map immediately. An already-tabbed active widget may stay
attached and alive only until callback return, when it is destroyed; the
Provider/plugin must not unload its destructor or meta-object code before then.

Every PropertyPage Provider removal enters the unified registry refresh path.
It restores the semantic key only when its context is still current and the
page still exists; otherwise the newest user choice or deterministic
first-valid-page fallback remains authoritative. Registered-Provider
availability changes use the same key-preserving path, and signals from an
unregistered but still-live Provider no longer affect Details. This is
in-session continuity, not persisted tab state across restart, project close,
or a different node.

Qt Creator's own Project settings widget captures the old tab before replacing
its panels and restores it afterward
([Qt Creator 20.0 `CentralWidget::setPanels()`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1366-L1385)).
The Workbench uses its semantic Provider/Page key rather than a numeric index
because dynamic providers can change page count and sort order. Qt defines the
current-page and index behavior used here
([QTabWidget](https://doc.qt.io/qt-6/qtabwidget.html)), while Qt Creator defines
the object-pool notification order used for safe synchronous page destruction
([Plugin manager object pool](https://doc.qt.io/qtcreator-extending/pluginmanager.html)).
Beckhoff documents selection-dependent General, EtherCAT, Process Data, and
Online tabs for an EtherCAT terminal
([terminal configuration tabs](https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html)).
Continuity across Provider churn is an Embed Labs Qt-native usability decision;
it is not claimed as a Beckhoff behavior or copied implementation.

The failure-first focused run produced 2 passes and 1 expected failure: after
removing Provider A, the actual current key was the built-in General page while
Provider B's key was expected. Independent diff review then added lifecycle
regressions. The first review run produced 2 passes and 1 failure because
toggling removed Provider A's availability deleted Provider B's saved widget.
After adding only the signal disconnect, the next run produced 2 passes and 1
failure because a switch-away/switch-back sequence restored stale Provider B
over the newer General selection. The generation fix closed that ABA, after
which reentrant review produced 2 passes and 1 failure because a later direct
about-to-remove slot rebuilt Provider A before registry removal. The departing
ID marker closes that same-signal/nested-MetaCall path. Final focused runs pass
3 tests at normal scale and 3 at `QT_SCALE_FACTOR=2`. The test covers two
independent providers,
availability loss/recovery, unrelated-provider removal, selected-provider
removal fallback to the exact first remaining key, unregistered-Provider signal
isolation without B widget recreation, switch-away/switch-back ABA rejection,
same-signal direct-slot re-entry with early MetaCall draining, stable
Selection/Details context, stale widget cleanup, and a scope guard that removes
registered test Providers on every assertion path.
The inspected offscreen renders are 900 x 600 and 1800 x 1200 and show Provider
B selected without clipping, overlap, or scale drift.

The complete Workbench suite passes 39 tests. The six isolated EtherCAT suites
pass 90 tests: Core 17, Project 12, Devices 8, Workbench 39, Scan 7, and
Diagnostics 7. The `WITH_TESTS=OFF` product build contains all 16 allow-listed
plugin dylibs. Enabled and explicitly Workbench-disabled product runs each
remained stable for 16 seconds before intentional SIGTERM target status 15.
Every executable ran offscreen with inherited DYLD variables cleared,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, fresh settings/home directories,
and only the process-local Touch Bar LLDB breakpoint. Final cleanup found no
residual Embed Labs or LLDB process, recent DiagnosticReports file, or
ReportCrash event.

This issue changes only existing private Details implementation/test files and
documentation. It adds no public API, source file, Provider, model role,
thread, timer, persistence, dependency, CMake/qbs entry, network or hardware
behavior, or upstream Core/ProjectExplorer/application change.

## Context-menu selection-drift safety

`ISSUE-WB-CONTEXT-MENU-SELECTION-DRIFT-001` prevents an already open device-
tree context menu from acting on a different stable selection. The popup uses
the existing shared ActionManager commands, while actions such as Move Up and
Move Down intentionally resolve the current Selection Service value when they
are triggered. Previously, changing the selection while `QMenu::exec()` was
active left the old popup open. A command presented for one configured slave
could therefore reorder another configured slave selected by an intervening
Workbench interaction, public Selection Service consumer, or project-lifecycle
event.

The navigation widget now captures the stable Selection Service `NodeId` when
it builds the popup. If that value changes while the menu is open, the menu
closes immediately. The new selection remains authoritative; Workbench does
not write the old value back. The existing post-menu path then synchronizes the
tree and every shared command with that latest selection. The connection is
scoped to the stack-owned menu and disappears with it. A placeholder popup
continues to use the pre-existing stable selection as its baseline, preserving
its temporary command restriction and restoration behavior.

Qt documents that `QMenu::exec()` runs synchronously and still emits the
connected action signals normally:
<https://doc.qt.io/qt-6/qmenu.html#exec>. Qt Creator's ActionManager documents
that `Command::action()` is the shared user-facing action placed in menus and
toolbars and delegates triggering to the active registered action:
<https://doc.qt.io/qtcreator-extending/actionmanager.html>. Qt Creator 20.0's
Project tree similarly retains the context-menu source widget until the popup
hides:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttree.cpp#L337-L395>.
Beckhoff describes a right-click menu on the configured I/O device and defines
Remove against the selected I/O device:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
Closing a stale popup is an Embed Labs Qt-native safety decision; it is not
claimed as copied TwinCAT behavior.

The failure-first focused run used a real popup and three configured slaves.
It opened the middle slave's menu, changed the stable selection to the first
slave, then triggered the still-present Move Down command. The old
implementation changed the complete Project snapshot, producing 2 passes and
1 failure. Final focused runs pass 3 tests at normal scale and 3 tests at
`QT_SCALE_FACTOR=2`; they verify the existing Open Diagnostics action can
change selection from a real popup without the post-menu path restoring the
opening row. They also verify external-drift popup closure, preservation of the
new stable selection, an unchanged Project snapshot, and unchanged Undo/Redo
availability.

The complete Workbench suite passes 39 tests. The six isolated EtherCAT suites
pass 90 tests: Core 17, Project 12, Devices 8, Workbench 39, Scan 7, and
Diagnostics 7. The `WITH_TESTS=OFF` product build contains exactly the 16
allow-listed plugin dylibs. Enabled and explicitly Workbench-disabled product
runs each remained stable beyond 16 seconds before intentional SIGTERM target
status 15. Every executable qualification ran offscreen with inherited DYLD
variables cleared, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, fresh
HOME/settings directories, and only the process-local Touch Bar LLDB
breakpoint. The final 2026-07-19 00:28:45 +0800 audit found no residual Embed
Labs or LLDB process, new DiagnosticReports file, or related ReportCrash event.

This issue changes only existing private Workbench navigation/test files and
documentation. It adds no QAction, public API, source file, model role,
Provider, production thread or timer, persistence, dependency, CMake/qbs
entry, network or hardware behavior, or upstream
Core/ProjectExplorer/application change.

## Navigation expansion continuity

`ISSUE-WB-NAV-EXPANSION-CONTINUITY-001` keeps the user's tree expansion
context stable while the offline Project snapshot changes and while a text
filter is temporarily active. Previously every Project-model reset restored
the stable selection and then unconditionally called `expandToDepth(2)`. A
rename, offline property edit, Undo/Redo, Project lifecycle event, or topology
mutation could therefore reopen an unrelated collapsed Project and collapse a
surviving expanded Slave, RxPDO group, or PDO. Filtering had a second path to
the same state drift: `expandAll()` exposed matches but clearing the filter
left that temporary state in the normal tree.

The navigation widget now owns a private set of expanded, value-type stable
`NodeId` values. It records normal `QTreeView::expanded` and `collapsed`
changes, enters reset protection before the proxy model observes
`modelAboutToBeReset`, removes IDs that no longer exist after `modelReset`,
restores surviving branches, and preserves the established depth-two default
for newly introduced Project structure. The current Selection Service
`NodeId` is restored last, so its ancestor path is deliberately revealed and
the stable selection remains authoritative. No `QModelIndex`, widget, model,
or Provider pointer is retained across reset.

Filter expansion is temporary presentation state. While a filter is nonempty,
accepted branches are expanded without updating the normal expansion set;
this also exposes a matching ESI device that appears after an initial empty
result. Clearing the line edit, using Clear Filter, or selecting a filtered-out
stable node restores the normal tree first and then reveals only the selected
path. The state belongs to one navigation-widget lifetime and is not persisted
across application restarts.

Qt documents that tree items have expanded/collapsed state and emit the
corresponding signals when it changes:
<https://doc.qt.io/qt-6/qtreeview.html#expanded>. Qt also states that a model
reset invalidates previously retrieved model information, including current
and selected indexes:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel>. Qt Creator
20.0's Project tree wires view expansion signals back to its model and restores
semantic expansion data after rebuilding:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttreewidget.cpp#L291-L302>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L486-L496>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L526-L539>.
Workbench follows that identity principle with its existing `NodeId` values;
it does not include or modify ProjectExplorer internals.

Beckhoff documents its configured I/O tree under I/O / Devices, with devices,
Process Image, and status/control inputs and outputs exposed below the device:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>.
The local Project -> Target -> Master -> Slave -> process-data hierarchy
continues to provide the same offline navigation shape. Beckhoff does not
define Qt model-reset or text-filter continuity; stable `NodeId` restoration
is an Embed Labs Qt-native behavior, not claimed TwinCAT parity.

The failure-first focused run created two Projects, kept Alpha collapsed,
expanded an unselected deep RxPDO/PDO path, selected the other Master, and
renamed its snapshot. The old unconditional depth expansion reopened Alpha,
producing 2 passes and 1 failure. Final focused runs pass 3 tests at normal
scale and 3 tests at `QT_SCALE_FACTOR=2`. The regression also proves deep
survivor restoration, new-Project depth-two defaults, temporary-filter
restoration, dynamic late-match visibility, and external Selection-driven
ancestor reveal.

The complete Workbench suite passes 40 tests. The six isolated EtherCAT suites
pass 91 tests: Core 17, Project 12, Devices 8, Workbench 40, Scan 7, and
Diagnostics 7. The final `WITH_TESTS=OFF` product build contains exactly the
16 allow-listed plugin dylibs. Enabled startup remained stable for 16 seconds
with fresh settings under
`/tmp/embed-labs-nav-product-enabled-final.ZyWuX2/settings`; explicitly disabled
startup remained stable for 16 seconds with `-noload EtherCATWorkbench` and
fresh settings under
`/tmp/embed-labs-nav-product-disabled-final.UiBc9t/settings`. Both were ended
intentionally with SIGTERM target status 15.

All executable qualification ran offscreen with inherited DYLD variables
cleared, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, fresh HOME/settings
directories, and only the process-local Touch Bar LLDB breakpoint. The final
2026-07-19 01:06:31 +0800 audit found no residual Embed Labs or LLDB process,
new DiagnosticReports file, or related ReportCrash event. Manual desktop
inspection was not run by design because this issue adds no geometry and no
visible test window was permitted.

This issue changes only existing private Workbench navigation/test files and
documentation. It adds no public API, source file, model role, QAction,
Provider, production thread or timer, persistence, dependency, network,
controller transport, physical-hardware behavior, CMake/qbs entry, or upstream
Core/ProjectExplorer/application change. Scan and Diagnostics remain local
Mock Providers, the Workbench path count remains 44, and the direct Core patch
count remains five.

## Details focus and rebuild continuity

`ISSUE-WB-DETAILS-FOCUS-CONTINUITY-001` is based on local baseline
`0637955df9ab009bb0ee1fcb892dc74e5ede27e7`. It closes the remaining
continuity and lifecycle gaps in the existing Details host. Before this issue,
a context rebuild could recover the semantic tab while replacing its widget,
but keyboard focus from a field such as
`EtherCATProjectGeneralCreatedBy` fell back to the tab bar. Nested Provider
callbacks could also request rebuild and refresh work while an earlier page
mutation was still active, and the old removal order let the registry expose a
departing Provider to another removal slot.

Details now captures focus with one private, value-only token containing the
stable context `NodeId`, semantic `PageKey`, child `objectName`, and rebuild
generation. No `QWidget`, `QModelIndex`, or Provider pointer is stored in that
token. When the same semantic page is recreated for the same context, the host
resolves the new child by object name and restores keyboard focus. A missing,
hidden, disabled, or no-longer-focusable target uses the current page's normal
focus-chain fallback. The empty-state widget remains keyboard reachable.

A single bounded operation pump serializes page rebuilds and refreshes. It
coalesces duplicate requests, preserves semantic transaction anchors through
nested callbacks, and processes at most eight synchronous operations before
posting a continuation. Provider availability changes, context changes,
provider addition/removal, and `updatePage()` re-entry all use this same pump.
Page collections are detached from the live page map before tab mutation, and
active tab mutation suppresses internal selection signals so nested
`hideEvent`, removal, or queued MetaCall delivery cannot observe a stale tab
index or delete the same page twice.

User intent after a bounded-pump yield is newer than the saved transaction.
An actual `QTabBar::currentChanged`, `tabBarClicked`, or focus choice therefore
replaces or cancels the older semantic anchor. Direct focus on the tab bar is
sticky and is not moved back into a page by the continuation. Focus that has
moved anywhere outside Details is likewise never stolen. Internal tab
insertion and restoration do not masquerade as user selection, while the
newest user-selected PageKey remains authoritative across the posted
continuation.

ProviderRegistry now unlinks a departing Provider from registry enumeration
before emitting `providerAboutToBeRemoved`; the object remains in the
PluginManager pool until that notification returns. This makes registry
queries and nested Provider removal from the signal re-entry safe. Ordinary
hosted pages are detached and destroyed before `removeObject()` returns. If a
Provider self-unregisters from its own `pages()`, `createPage()`, or
`updatePage()` callback, no callback-associated widget remains in the live-page
map after removal handling. An already-tabbed active widget may stay attached
and alive only until callback return, when it is destroyed. The Provider/plugin
must remain loaded, including page destructor and Qt meta-object code, through
that callback boundary.

Qt documents tab ownership and current-index behavior in
[QTabWidget](https://doc.qt.io/qt-6/qtabwidget.html), and keyboard-focus
semantics in [QWidget](https://doc.qt.io/qt-6/qwidget.html). Qt Creator 20.0's
Project settings widget provides the read-only precedent for retaining the
semantic current panel while replacing its widgets
([`CentralWidget::setPanels()`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1366-L1385)).
Beckhoff documents the selection-dependent General, EtherCAT, Process Data,
and Online tabs for an EtherCAT terminal
([terminal configuration tabs](https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html)).
Exact Qt focus restoration, bounded callback re-entry, and third-party
Provider lifecycle handling are Embed Labs Qt-native behavior, not copied
TwinCAT implementation.

The failure-first regression reproduced the lost field focus after replacing
one Project context with another that published the same PageKey. The focused
Workbench coverage now exercises exact focus restoration across a new widget,
same PageKey with distinct NodeIds, missing/hidden focus-target fallback,
external-focus preservation, direct tab-bar focus cancellation, empty-state
keyboard focus, nested availability rebuilds, synchronous context/update/
clear/delete re-entry, max-eight-operation yield, and user tab/focus priority
after that yield. Provider lifecycle coverage adds refresh self-trigger
deduplication, consecutive and nested removal, removal from `updatePage()` and
page-hide re-entry, self-unregister of the active callback Provider, immediate
ordinary-page destruction, active-callback deferred destruction, and stale
snapshot exclusion. Core coverage proves unlink-before-signal registry state
and nested removal.

Both normal-scale and `QT_SCALE_FACTOR=2` Workbench runs pass 41 tests. Four
frozen offscreen renders under
`/tmp/embed-labs-details-render-final2.wMQmxh` match the inspected artifacts
exactly and show no clipping, overlap, or scale drift. The complete six-suite
run under `/tmp/embed-labs-six-suites-final2.XeKLz8` passes 92 tests: Core 17,
Project 12, Devices 8, Workbench 41, Scan 7, and Diagnostics 7. The unrelated
all-target `WITH_TESTS=ON` build remains blocked by the pre-existing
`easyboardbrowser.cpp` include of unavailable `extensionmanager_test.h`; the
required EtherCAT targets and suites pass.

The final `WITH_TESTS=OFF` product build passes with exactly the 16 allow-listed
plugin dylibs. Under `/tmp/embed-labs-product-lifecycle-frozen.NIAjdQ`, enabled
startup remained alive for 16 seconds and `-noload EtherCATWorkbench` startup
remained alive for 16 seconds; both ended by intentional SIGTERM with LLDB
target status 15. Cleanup found no residual Embed Labs or LLDB process and no
new Embed Labs crash report. The only running ReportCrash agent pre-dated this
qualification by more than one day and was unrelated.

Every qualified executable above used fresh HOME/settings, cleared inherited
DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar LLDB breakpoint. No visible main window or interposer was used.

This issue changes existing product-owned EtherCATCore registry/test files,
private EtherCATWorkbench Details/test files, and documentation only.
`src/plugins/ethercatcore` is an Embed Labs product plugin, not a direct patch
under Qt Creator's upstream Core, ProjectExplorer, or application bootstrap.
No CMake or qbs file changed, so qbs was not run. No public Provider shape,
source list, dependency, persistence, controller transport, network, or
physical-hardware behavior changes. The Workbench path count remains 44 and
the direct upstream Core patch count remains five.

## CoE filter empty state and accessibility

`ISSUE-WB-COE-FILTER-EMPTY-A11Y-001` is based on local baseline
`6935b103b1adbc3641c28afcd3e683f2772848fb`. It closes the blank-result gap in
the existing CoE Online page. Any active text, dictionary-range, Hide Standard,
or Hide PDO filter that produces zero rows now switches the result area from
the dictionary to an explicit message and keyboard-reachable `Clear Filters`
button. `Add to Startup` is disabled while no object is selected.

The result presentation follows the proxy model, not just line-edit changes.
Rows inserted or removed, model resets, layout changes, and data changes all
refresh the result state, current selection, and action state. This includes
editing the Value of the final matching Mock object so that it immediately
stops matching. The filter field, empty state, message, and clear action expose
accessible names or descriptions appropriate to their widget roles.

Clearing is one guarded transaction: range, Hide Standard, Hide PDO, and text
filters are reset without letting an intermediate proxy result replace the
selection anchor. The page retains only the existing private numeric
`AddressRole`; it retains no `QModelIndex`, view item, model item, or object
pointer across the change. When the address is visible again it is restored;
otherwise the first available row is selected. Keyboard focus returns to the
dictionary. A new page context clears the old address and advanced-filter
state, so neither can leak between devices. The Mock/offline source choice,
current Project context, and Project snapshot are not changed by Clear Filters.

Qt documents dynamic filtering in
[QSortFilterProxyModel](https://doc.qt.io/qt-6/qsortfilterproxymodel.html) and
widget accessibility properties in
[QWidget](https://doc.qt.io/qt-6/qwidget.html). Qt Creator 20.0's private
Extensions browser provides the local presentation precedent for placing a
proxy-backed view and empty placeholder in a stacked widget and reacting to
proxy row changes. Beckhoff's CoE Online object-dictionary presentation remains
the product comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.
The exact atomic clear, dynamic edit handling, address restoration, and
accessibility behavior are Embed Labs Qt-native completions.

The failure-first run under
`/tmp/embed-labs-coe-filter-failure.UBAZUx` passed setup and cleanup but failed
the focused workflow because no explicit empty state existed. Final focused
runs pass 3 tests at normal scale and 3 tests at `QT_SCALE_FACTOR=2` under
`/tmp/embed-labs-coe-filter-locked-normal2.vocRvH` and
`/tmp/embed-labs-coe-filter-locked-2x2.iFfDMA`. Inspected 1100 x 760 and
2200 x 1520 renders have SHA-256 values
`dfcc0dfab520ee5013ea22a561f9f89e1a5b28cbc6c8e11b037db48ebed87440`
and `3563edf87c993042cb51139f358937225722d077487509ea749fa11aa5eea55c`;
neither shows clipping, overlap, or scale drift.

Complete normal-scale and 2x Workbench runs each pass 41 tests under
`/tmp/embed-labs-workbench-post-address-normal.1q6GwZ` and
`/tmp/embed-labs-workbench-post-address-2x.FPJYWy`. The six isolated EtherCAT suites
pass 92 tests: Core 17, Project 12, Devices 8, Workbench 41, Scan 7, and
Diagnostics 7. The final `WITH_TESTS=OFF` product build passes with exactly the
16 allow-listed plugin dylibs. Enabled startup remained alive for 16 seconds
under `/tmp/embed-labs-coe-product-locked-enabled.KY0v5e`; explicitly disabled
startup remained alive for 16 seconds with `-noload EtherCATWorkbench` under
`/tmp/embed-labs-coe-product-locked-disabled.YHhQUP`. Both were ended intentionally
with SIGTERM target status 15.

Every executable qualification used fresh HOME/settings, cleared inherited
DYLD variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint. Cleanup
found no residual Embed Labs or LLDB process and no new Embed Labs diagnostic
report. No visible main window was created.

This issue modifies existing private Workbench page/test files and
documentation only. It adds no source file, public API, model role, Provider,
dependency, persistence field, thread, timer, SDO/controller transport,
network access, or physical-hardware behavior. The existing Add-to-Startup
ProjectService path is unchanged. No CMake or qbs file changed, so qbs was not
run. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

## Insert-device dialog target lifecycle

`ISSUE-WB-INSERT-DIALOG-TARGET-LIFECYCLE-001` is based on local baseline
`a0b68e7313b80e4292a135966f7ead4c4e131656`. The Master-side `Add New
Item...` dialog now captures only the selected Project and Master IDs before
it opens. If that Project starts closing, a different Project becomes active,
the Project becomes invalid, or its published snapshot no longer contains the
same Master, the visible dialog rejects immediately. No device is added and no
late validation error is shown for the stale target.

The three ProjectService notifications are connected with the dialog as the
QObject context, so every connection disappears with the dialog. Rejection is
guarded by dialog visibility; overlapping close and active-project
notifications therefore produce at most one `rejected` signal. The existing
accepted path still resolves and submits through the Project-owned command.

Qt documents the rejected-dialog result and lifetime-aware connection
contracts in [QDialog](https://doc.qt.io/qt-6/qdialog.html) and
[QObject](https://doc.qt.io/qt-6/qobject.html). Qt Creator 20.0 removes
Project-bound UI as soon as `aboutToRemoveProject` is published in
[`ProjectWindow`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1509-L1515).
Beckhoff's comparison flow inserts a device beneath the selected I/O target
([Add New Item / Insert Device](https://infosys.beckhoff.com/content/1033/eap/1521664395.html)).
The exact close-on-target-invalidation behavior is an Embed Labs Qt-native
safety boundary.

The failure-first run reached setup and cleanup but failed the new assertion
because the old dialog remained open after an active-project switch. Final
focused normal-scale and `QT_SCALE_FACTOR=2` runs each pass six tests: setup,
the four target-invalidation rows, and cleanup. The complete Workbench suite
passes 45 tests. The six isolated EtherCAT suites pass 96 tests: Core 17,
Project 12, Devices 8, Workbench 45, Scan 7, and Diagnostics 7. The final
`WITH_TESTS=OFF` product build also passes.

Enabled product startup and startup with `-noload EtherCATWorkbench` each
remained stable for more than 30 seconds. Both were stopped intentionally
under LLDB. The disabled run emitted one non-fatal shared-memory initialization
message and continued through startup. Cleanup found no residual Embed Labs
process and no new Embed Labs diagnostic report. Every executable used fresh
HOME/settings, cleared inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar LLDB breakpoint. No main window or crash dialog became visible to the
user.

This issue changes only existing private Workbench UI/test files and
documentation. It adds no source file, public API, dependency, persistence,
Provider, thread, timer, network/controller transport, or physical-hardware
behavior. No CMake or qbs file changed, so qbs was not run. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

## Process Data table accessibility

`ISSUE-WB-PROCESS-DATA-TABLE-A11Y-001` is based on local baseline
`1ce66e3332e17f0ab3e6e4597734d0339dfad230`. It closes the accessibility and
long-cell recovery gap in the existing Process Data page without changing its
splitters, selection, editing, validation, or persistence behavior.

The Sync Manager, PDO Assignment, PDO List, PDO Content, and Process Image
Preview tables now publish unique translated accessible names and concise
purpose descriptions. Every valid cell exposes the full value through
`Qt::AccessibleTextRole`. Its `Qt::AccessibleDescriptionRole` and tooltip
identify the column, retain the full unelided value, include the row's Name
when available, and preserve the existing selection, editability, automatic
offset, assignment, or absolute bit-range guidance. The otherwise visually
empty PDO Assignment checkbox cell reports `Assigned` or `Not assigned`; its
existing `Qt::CheckStateRole` and edit rules remain authoritative. Assignment
guidance advertises selection only for an editable, supported optional PDO.
Mandatory and unsupported mappings retain their specific reasons; other
derived, catalogue, and read-only contexts report the read-only boundary.

Qt defines the standard item accessibility roles in
[Qt::ItemDataRole](https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum) and widget
accessible metadata in
[QWidget](https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop). Qt Creator
20.0 provides local source precedents for explicit widget names/descriptions
in
[`FancyMainWindow`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247)
and model accessibility descriptions in
[`TerminalPane`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597).
Beckhoff documents the selection-dependent Sync Manager, PDO Assignment, PDO
List, and PDO Content hierarchy in
[Process data](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html).
The combined cell description and full-value tooltip are Embed Labs Qt-native
behavior, not copied TwinCAT implementation.

The failure-first run under
`/private/tmp/embed-labs-process-a11y-failure.ooJLPR` passed setup and cleanup
but failed on the first Sync Manager table because its accessible name was
empty. After the implementation, focused normal-scale and
`QT_SCALE_FACTOR=2` runs each pass three events under
`/private/tmp/embed-labs-process-a11y-final3-normal.kZTfM1` and
`/private/tmp/embed-labs-process-a11y-final3-2x.rpWLPm`. The regression covers all
five table names/descriptions, every populated cell, the assignment checkbox
semantics, complete Unicode PDO/PDO Entry values with 256-character suffixes,
their descriptions and tooltips, valid accessible-text values even for empty
display cells, both assignment states, truthful read-only guidance, retained
mandatory/unsupported reasons, and the process-image bit-range context. It
verifies the standard Qt contract and does not claim a manual VoiceOver
reading.

Complete normal-scale and 2x Workbench runs each pass 46 events under
`/private/tmp/embed-labs-workbench-final2-normal.d8z7g7` and
`/private/tmp/embed-labs-workbench-final2-2x.GACIqx`. The six isolated EtherCAT suites
pass 97 events: Core 17, Project 12, Devices 8, Workbench 46, Scan 7, and
Diagnostics 7. The non-Workbench suite logs are under the matching
`/private/tmp/embed-labs-six-final3-ethercat*` directories. The final
`WITH_TESTS=OFF` build passes and contains exactly the 16 allow-listed plugin
dylibs.

Enabled product startup under
`/private/tmp/embed-labs-product-enabled-final5.G4bRdZ` and startup with
`-noload EtherCATWorkbench` under
`/private/tmp/embed-labs-product-disabled-final5.oaSTLm` each completed delayed
initialization and remained alive for 31 seconds. Both ended through an
intentional SIGTERM to the LLDB-owned target with status 15. The explicitly
disabled run reported one non-fatal shared-memory initialization message and
continued. Cleanup found no residual target or LLDB process and no new Embed
Labs or LLDB DiagnosticReports file.

Every executable used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. This issue changes only the existing private
`processdatapage.cpp`, Workbench test declaration/implementation, and
documentation. It adds no source file, public or custom model role, API,
dependency, Provider, persistence, Project mutation, thread, timer,
network/controller transport, or physical-hardware behavior. No CMake or qbs
file changed, so qbs was not run. The Workbench path count remains 44 and the
direct upstream Core patch count remains five.

## Offline topology dialog screen-bound qualification

`ISSUE-WB-TOPOLOGY-DIALOG-BOUNDS-001` is based on local baseline
`9e56cfebbd09766ea7ae326b5741acb95fecb274`. It closes one bounded Workbench
interaction gap: a valid long Unicode slave name could make the read-only
Topology dialog wider than the current screen.

The existing table still uses ten `ResizeToContents` columns and `ElideNone`,
so the Project values are neither rewritten nor truncated. The dialog now
uses the selected Workbench page's screen and `availableGeometry()` to bound
and center its initial geometry. Short content keeps its natural preferred
width when it fits. Long content keeps the full section widths and is
recovered with the table's horizontal scrollbar. The first and last columns,
the complete long Name and Previous values, and the Close button therefore
remain reachable without forcing the top-level window beyond the screen.

Qt documents the full-content section policy in
[QHeaderView::ResizeToContents](https://doc.qt.io/qt-6/qheaderview.html#ResizeMode-enum),
the work-area boundary in
[QScreen::availableGeometry](https://doc.qt.io/qt-6/qscreen.html#availableGeometry-prop),
and the as-needed scrollbar contract in
[QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#horizontalScrollBarPolicy-prop).
Qt Creator's local centered Locator popup uses the parent widget's screen
before the popup is visible. Beckhoff's
[EtherCAT master page](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html)
remains the product comparison for opening a configured-slave topology view;
Embed Labs still presents offline Project data only and does not claim online
values or physical-port modeling.

The failure-first run under
`/private/tmp/embed-labs-topology-bounds-failure.UNq9D8` passed test setup and
cleanup but failed the new workflow because the old dialog was 13235 logical
pixels wide on an 800-pixel available screen. After the implementation, the
focused normal-scale and `QT_SCALE_FACTOR=2` runs each pass three events under
`/private/tmp/embed-labs-topology-bounds-qualified-normal.3ci48Z` and
`/private/tmp/embed-labs-topology-bounds-qualified-2x.RUpPcy`. Their 784-by-279 and
768-by-558 pixel renders show the horizontally scrolled final columns and an
available Close button. The regression also proves two Chinese/Unicode names
with 512-character payloads are preserved verbatim, the second row's Previous
value retains the first complete name, both scrollbar endpoints expose their
respective edge column, and both the client and frame geometry stay inside the
current screen at both scales.

Complete normal-scale and 2x Workbench runs each pass 47 events under
`/private/tmp/embed-labs-topology-workbench-qualified-normal.P95AUC` and
`/private/tmp/embed-labs-topology-workbench-qualified-2x.OJXAsW`. The six isolated
EtherCAT suites pass 98 events: Core 17, Project 12, Devices 8, Workbench 47,
Scan 7, and Diagnostics 7. The final `WITH_TESTS=OFF` product build passes and
contains exactly the 16 allow-listed plugin dylibs.

Enabled product startup under
`/private/tmp/embed-labs-topology-product-enabled-final2.IuIojz` remained alive
for 33 seconds. Startup with `-noload EtherCATWorkbench` under
`/private/tmp/embed-labs-topology-product-disabled-final.9EKJ0H` remained alive
for 32 seconds; one non-fatal shared-memory initialization message did not
interrupt it. LLDB passed the intentional SIGTERM to each target, which exited
with status 15. Cleanup found no residual target/LLDB process and no new Embed
Labs or LLDB DiagnosticReports file.

Every executable used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. This issue changes only the existing private
`ethercatpage.cpp`, Workbench test declaration/implementation, and
documentation. It adds no source file, public API, dependency, Provider,
persistence, Project mutation, thread, timer, network/controller transport,
online data, physical-port model, or physical-hardware behavior. No CMake or
qbs file changed, so qbs was not run. The Workbench path count remains 44 and
the direct upstream Core patch count remains five.

## Startup table accessibility qualification

`ISSUE-WB-STARTUP-TABLE-A11Y-001` is based on local baseline
`cf3c437147e816320256c1e75b5c67863de77663`. It closes one bounded
accessibility and long-cell recovery gap in the existing offline Startup page
without changing its table geometry, selection, editors, validation,
ProjectService command path, Undo/Redo, or persistence.

`EtherCATStartupTable` now publishes a translated accessible name and a
purpose description. Every valid cell provides a real `QString` through
`Qt::AccessibleTextRole`. `Qt::AccessibleDescriptionRole` and the tooltip
retain the complete object address, column heading, and current unelided value.
The visually empty Enabled cell reports `Enabled` or `Disabled`, while the
existing `Qt::CheckStateRole` and item flags remain authoritative. A 256-byte
raw value and a long Chinese/Japanese/Unicode Comment are therefore recoverable
without widening or wrapping the table.

Operation guidance is derived from the current row and cell flags. An
angle-bracketed fixed ESI request states that it cannot be enabled or disabled,
edited, deleted, or moved. A repository catalogue request reports the current
read-only context. Only a genuinely editable configured-slave field advertises
editing, while the CoE Protocol cell remains explicitly read-only. No
model index, Project snapshot, widget, Provider, or controller object is
retained across a call.

Qt defines the standard item roles in
[Qt::ItemDataRole](https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum) and the
localized widget metadata contract in
[QWidget](https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop). Qt Creator
20.0 provides local precedents for explicit widget accessibility in
[`FancyMainWindow`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247)
and standard model descriptions in
[`TerminalPane`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597).
Beckhoff documents the ordered Transition, Protocol, Index, Data, Comment, and
fixed-request behavior in
[Startup](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html).
The Enabled column and Qt accessibility metadata are Embed Labs Qt-native
offline behavior rather than a TwinCAT implementation claim.

The failure-first run under
`/private/tmp/embed-labs-startup-a11y-failure.kptBc4` passed test setup and
cleanup but failed because the old table's accessible name was empty. After
the implementation and review corrections, focused normal-scale and
`QT_SCALE_FACTOR=2` runs each pass three events under
`/private/tmp/embed-labs-startup-a11y-final-tests.KbokZb`. The regression checks
all nine columns, exact `QString` role types, complete Display/accessible-text
agreement outside Enabled, both check states, editable/read-only/fixed
guidance, complete long raw Data and Unicode Comment values, unchanged flags,
and Project cleanup. This verifies Qt metadata; no manual VoiceOver reading is
claimed.

Complete normal-scale and 2x Workbench runs each pass 48 events under the same
final-test root. The six isolated EtherCAT suites pass 99 events under
`/private/tmp/embed-labs-startup-a11y-six-final.mI2ZSV`: Core 17, Project 12,
Devices 8, Workbench 48, Scan 7, and Diagnostics 7. Every target exited with
status 0. The `WITH_TESTS=OFF` product build passes and contains exactly the 16
allow-listed plugin dylibs.

Enabled product startup under
`/private/tmp/embed-labs-startup-a11y-product-enabled-final.yjyQ90` and startup with
`-noload EtherCATWorkbench` under
`/private/tmp/embed-labs-startup-a11y-product-disabled-final.FFNwfM` each
remained alive for a measured 35 seconds after its target PID was observed.
The evidence files record the PID, UTC/epoch start and termination times, and
the 35-second difference. LLDB passed the intentional SIGTERM to each target,
which exited with status 15. The disabled run emitted one non-fatal
shared-memory initialization message and continued. Cleanup found no residual
target or LLDB process and no new Embed Labs or LLDB DiagnosticReports file.

Every executable used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. This issue changes only the existing private
`startuppage.cpp`, Workbench test declaration/implementation, and
documentation. It adds no source file, public or custom model role, API,
dependency, Provider, persistence field, Project command, thread, timer,
SDO/network/controller transport, online state, or physical-hardware behavior.
No Startup request was sent. No CMake or qbs description changed, so qbs was
not run. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

## CoE dictionary cell accessibility qualification

`ISSUE-WB-COE-DICTIONARY-CELL-A11Y-001` is based on local baseline
`bc6d2e1b45f521fb962790a19370672bbc7ff687`. It closes one bounded
accessibility and long-cell recovery gap in the configured-slave CoE object
dictionary. It does not change the existing table geometry, hierarchy,
selection, filtering, Mock editor, Add to Startup path, ProjectService,
persistence, or offline toggle.

Every valid cell in the Index, Name, Flags, Value, and Unit columns now returns
an actual `QString` through Qt's standard `AccessibleTextRole`, exactly
matching its complete current Display value. This includes a valid empty
`QString` for an empty Unit cell. `AccessibleDescriptionRole` and
`ToolTipRole` provide the object address, column heading, complete unelided
value, data type, Mock/offline source, local-prototype access boundary, and
current operation guidance. The descriptions state that no controller
connection or SDO transfer occurs. Within the qualified configured-slave
context, only an editable Mock Value cell advertises a temporary local edit;
offline cells explicitly report read-only.

The model computes complete text only for Display, accessibility, and tooltip
queries. A successful Mock Value edit now publishes Display, Edit,
AccessibleText, AccessibleDescription, Tooltip, and the existing private raw
value role in the same `dataChanged` notification. The edit remains transient
model state: the selected object, Add to Startup availability, item flags, and
complete Project snapshot stay unchanged. Switching this test fixture to
offline resets the model presentation, restores its original ESI fixture
value, and removes the edit flag; the test resolves a fresh index after that
reset.

Qt defines the standard roles in
[Qt::ItemDataRole](https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum), the
changed-role notification contract in
[QAbstractItemModel::dataChanged](https://doc.qt.io/qt-6/qabstractitemmodel.html#dataChanged),
and the item-view elision policy in
[QAbstractItemView::textElideMode](https://doc.qt.io/qt-6/qabstractitemview.html#textElideMode-prop).
Beckhoff's
[CoE Online](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html)
page remains only the five-column object-dictionary comparison. Embed Labs
continues to show local Mock/offline data and does not claim TwinCAT online
behavior, controller access, or SDO transport.

The frozen final test was run against the unchanged baseline implementation
under `/private/tmp/embed-labs-coe-cell-a11y-failure-frozen.Ija4nk`. Setup and
cleanup passed, and the test failed exactly once because the old Index cell did
not return a `QString` for `AccessibleTextRole`. After restoring the final
implementation, focused normal-scale and `QT_SCALE_FACTOR=2` runs each pass
three events under
`/private/tmp/embed-labs-coe-cell-a11y-focused-frozen-normal.EH7ZAp` and
`/private/tmp/embed-labs-coe-cell-a11y-focused-frozen-2x.P5UqHe`.

The regression imports a unique temporary ESI device and checks every
hierarchical row across all five columns. Its fixture includes a complete
256-byte value and a long Chinese/Japanese/Unicode name containing literal
`%1`, `%2`, `%5`, and `%%` text. It verifies exact standard-role types, full
values, empty Unit handling, operation/source boundaries, the complete role
list after edit, Project immutability, selection and Add to Startup continuity,
and offline reset behavior. This qualifies Qt model metadata; no manual
VoiceOver reading is claimed.

Complete normal-scale and 2x Workbench runs each pass 49 events under
`/private/tmp/embed-labs-coe-cell-a11y-workbench-final-normal.p7cwQT` and
`/private/tmp/embed-labs-coe-cell-a11y-workbench-final-2x.vs1Nst`. The six
isolated EtherCAT suites pass 100 events under
`/private/tmp/embed-labs-coe-cell-a11y-six-suites-final.cmbhBS`: Core 17,
Project 12, Devices 8, Workbench 49, Scan 7, and Diagnostics 7. Every test
target exited with status 0. The full `WITH_TESTS=OFF` product build passes and
contains exactly the 16 allow-listed plugin dylibs.

Enabled product startup under
`/private/tmp/embed-labs-coe-cell-a11y-product-lifecycle-final.I01hmv/enabled`
remained alive for 36 seconds after PID 98426 was observed. Startup with
`-noload EtherCATWorkbench` under the matching `disabled` directory remained
alive for 37 seconds after PID 443 was observed. Each `lifecycle-evidence.txt`
records PID and UTC/epoch boundaries. LLDB passed the intentional SIGTERM to
each target, which exited with status 15. Cleanup found no residual Embed Labs
or LLDB process, no new DiagnosticReports file, and no matching ReportCrash or
CrashReporter unified-log event during the run interval.

Every executable used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. This issue changes only the existing private
`coeonlinepage.cpp`, Workbench test declaration/implementation, and
documentation. It adds no source file, public or custom model role, API,
dependency, Provider, persistence field, Project command, thread, timer,
controller/SDO/network transport, online state, or physical-hardware behavior.
No CMake or qbs description changed, so qbs was not run. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

Device-repository CoE read-only behavior was not qualified by this earlier
accessibility issue. It is qualified separately below by
`ISSUE-WB-COE-REPOSITORY-READONLY-001`; the earlier accessibility evidence and
scope remain unchanged.

## Repository Device CoE read-only qualification

`ISSUE-WB-COE-REPOSITORY-READONLY-001` uses local baseline
`3f4ea273a559506af663ae1edbb5233d31688565`. The private CoE object model now
receives its context-level Mock editing permission in the same reset that
rebuilds object definitions. Only a configured-slave context backed by the
current slave and Project snapshots receives that permission. Repository
Device, offline, missing-slave, and missing-Project contexts remain read-only.

Both `flags()` and `setData()` enforce the same private permission. The ESI
object capability remains independently visible as `RW`; `WritableRole`,
filtering, Update List, Show Offline Data, selection, object hierarchy, and
standard accessibility roles are unchanged. The existing Add to Startup
context gate remains disabled for the repository Device. No controller,
network, SDO transfer, Project mutation, or persistent repository edit is
introduced.

The frozen test compiled against the old implementation and ran under
`/private/tmp/embed-labs-coe-repository-failure-final.VgDNIm`. Initialization and
cleanup passed, and the only failure was the expected repository `6060:00`
Value cell still advertising `Qt::ItemIsEditable`; the target exited with test
status 1. After the minimal implementation, focused normal-scale and
`QT_SCALE_FACTOR=2` runs each passed three events with target status 0 under
`/private/tmp/embed-labs-coe-repository-focused-final.3aAeuW/normal` and
`/private/tmp/embed-labs-coe-repository-focused-final.3aAeuW/2x`.

The regression uses a unique temporary ESI ProductCode and the page's actual
proxy model. It proves configured-slave editability first, then repository
`RW` capability plus read-only flags, direct edit rejection, unchanged Display,
Edit, accessible text, description and tooltip values, disabled Add to Startup,
and complete Project-snapshot immutability. A configured / repository /
configured / repository round trip resolves fresh indexes after each reset and
proves that edit permission neither leaks nor remains stale.

Complete normal-scale and 2x Workbench runs each passed 50 events with target
status 0 under
`/private/tmp/embed-labs-coe-repository-workbench-final.2YRthk/normal` and
`/private/tmp/embed-labs-coe-repository-workbench-final.2YRthk/2x`. The six isolated
EtherCAT suites passed 101 events under
`/private/tmp/embed-labs-coe-repository-six-suites-final.jBXs08`: Core 17, Project
12, Devices 8, Workbench 50, Scan 7, and Diagnostics 7. Every target exited
with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611`, and exactly the 16 allow-listed
plugin dylibs are present. Enabled product startup observed PID 95408 and
remained running for 36.006 seconds under
`/private/tmp/embed-labs-coe-repository-product-enabled-final2.DrnDXO`.
Explicitly disabled startup observed PID 96495 and remained running for 36.005
seconds with `-noload EtherCATWorkbench` under
`/private/tmp/embed-labs-coe-repository-product-disabled-final2.DKy1uv`. The
disabled run emitted one non-fatal shared-memory initialization message. Both
targets were still running immediately before LLDB passed intentional SIGTERM,
and both exited with target status 15.

Cleanup found no residual Embed Labs or LLDB process, new DiagnosticReports
file, or matching ReportCrash/CrashReporter unified-log event. Every executable
used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. Manual desktop inspection was not run because this
issue changes no geometry. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker is outside this private
Workbench issue.

This issue changes only the existing private `coeonlinepage.cpp`, Workbench
test declaration/implementation, and documentation. It adds no source file,
public API, dependency, Provider, custom model role, persistence field, Project
command, thread, timer, controller/SDO/network transport, online state, or
physical-hardware behavior. No CMake or qbs description changed, so qbs was not
run. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

Qt defines `ItemIsEditable` as the item-model capability to edit an item and
requires editable models to align `flags()` with `setData()`:
<https://doc.qt.io/qt-6/qt.html#ItemFlag-enum> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html>. Beckhoff distinguishes a CoE
Online object operation from an offline device-description source while
retaining the object's `RW`/`RO` access metadata:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## ESI device-selection cell accessibility qualification

`ISSUE-WB-ESI-SELECTION-CELL-A11Y-001` uses local baseline
`2ffc91065b6d940bd9bc6197eb96d0471dfc4795`. The existing private
`EsiDeviceSelectionDialog` now publishes complete standard Qt accessibility
metadata for every Device, Type, Vendor ID, Product Code, Revision, Group, and
Support cell. `AccessibleTextRole` is an actual `QString` equal to the current
complete Display value. `AccessibleDescriptionRole` and `ToolTipRole` contain
the matching column heading and complete value, Supported or Limited
qualification, whether the row can be appended, and the existing offline
boundary that accesses no controller or network.

The same translated column-label list supplies the visible headers and the
cell descriptions. Supported rows state that they can be appended to the
selected offline EtherCAT Master. Limited rows state that they cannot be
appended until unsupported structures are resolved. This does not change the
existing row flags, icons, identity roles, highest-revision filtering,
sorting, search, extended columns, first-supported selection, Add enablement,
double-click handling, insertion, or dialog geometry. Complete values remain
recoverable even when the view elides them visually.

Qt documents the standard data roles at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and the corresponding
`QStandardItem` setters at
<https://doc.qt.io/qt-6/qstandarditem.html>. Beckhoff's offline-configuration
workflow provides the comparison for selecting an ESI-described device,
searching the catalogue, showing extended information, and choosing latest or
previous revisions:
<https://infosys.beckhoff.com/content/1033/el331x/1036999947.html> and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.
Embed Labs remains an offline Project editor and does not claim TwinCAT
controller or network behavior.

The frozen test ran first against the unchanged implementation under
`/private/tmp/embed-labs-esi-selection-cell-a11y-failure.fHPgX3`. Setup and
cleanup passed, and the test failed because the old cell returned no
`QString` for `AccessibleTextRole`; the target exited with status 1. After the
minimal implementation, the focused normal-scale run passed three events
under `/private/tmp/embed-labs-esi-selection-cell-a11y-pass-1.R4JfjQ`, and the
explicit `QT_SCALE_FACTOR=2` run passed three events under
`/private/tmp/embed-labs-esi-selection-cell-a11y-focused-2x.sgxlpQ`. The
fixture verifies all 14 cells across Supported and Limited rows, exact role
types, complete Unicode Device/Type/Group values, literal `%1`, `%2`, and `%%`
recovery, truthful operation boundaries, tooltip equality, and unchanged Add
enablement. This qualifies standard Qt metadata; no manual VoiceOver reading
is claimed.

Complete normal-scale and 2x Workbench runs each passed 51 events with target
status 0 under `/private/tmp/embed-labs-workbench-full-1.pA3kEO` and
`/private/tmp/embed-labs-esi-selection-cell-a11y-full-2x.wgcUmB`. The six
isolated EtherCAT suites passed 102 events under
`/private/tmp/embed-labs-six-suite.2xoXwk`: Core 17, Project 12, Devices 8,
Workbench 51, Scan 7, and Diagnostics 7. Every target exited with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611`, and exactly the 16 allow-listed
plugin dylibs are present. Enabled product startup observed PID 50789 and
remained running for 36.011 seconds after observation. Explicitly disabled
startup observed PID 51907 and remained running for 36.002 seconds with
`-noload EtherCATWorkbench`. Evidence for both runs is under
`/private/tmp/embed-labs-esi-selection-cell-a11y-lifecycle.2erfTD`. Each target
was still running before LLDB passed intentional SIGTERM and exited with target
status 15.

Cleanup found no residual target or LLDB process, new DiagnosticReports file,
or matching ReportCrash/CrashReporter unified-log event. Every executable used
fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. Manual desktop inspection was not run because this
issue changes no geometry. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue.

This issue changes only the existing private `esideviceselectiondialog.cpp`,
Workbench test declaration/implementation, and documentation. It adds no
source file, public API, dependency, Provider, custom model role, persistence
field, Project command, thread, timer, controller/network transport, online
state, or physical-hardware behavior. No CMake or qbs description changed, so
qbs was not run. The Workbench path count remains 44 and the direct upstream
Core patch count remains five.

## Project-scoped Details draft continuity qualification

`ISSUE-WB-DETAILS-UNRELATED-PROJECT-DRAFT-001` uses local baseline
`e5ded42f8dd3d3d861de830e3408de12f1847334`. Previously, the private
`DetailsView` discarded the `ProjectSnapshot` carried by every
`ProjectService::projectChanged` signal and refreshed every page for the
current selection. Editing Project A while Project B changed therefore called
`GeneralPage::setContext()` and the other current page updates with Project
A's persisted snapshot. An uncommitted Project A name was silently replaced
before its `editingFinished` commit path ran.

The Details connection now reads the changed Project ID and refreshes page
contents only when it equals the current `PropertyPageContext::projectId`.
The comparison is evaluated when the signal arrives, not captured when the
connection is created, so selection changes retain the correct ownership
boundary. Project and configured-slave contexts continue to refresh for their
own Project changes, including external edits and Undo/Redo. Repository
contexts have no Project ID and do not refresh merely because an unrelated
Project changed. Tree synchronization, selection, page ownership, Provider
lifecycle, and Project close handling remain unchanged.

Qt documents that user edits set `QLineEdit::modified`, while `setText()`
resets that flag and replaces the text:
<https://doc.qt.io/qt-6/qlineedit.html#modified-prop>. Qt's typed
signal/functor connection permits the receiver-context lambda to consume the
signal argument:
<https://doc.qt.io/qt-6/qobject.html#connect-5>. Qt Creator 20.0's Project
settings implementation keeps change listeners on their owning Project item
rather than treating every Project event as current-page data:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L580-L619>.
Beckhoff's TwinCAT comparison is likewise selection-scoped: the terminal
selected in Solution Explorer determines the available configuration tabs:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.
These sources guide event scoping only; Embed Labs remains a local Mock/offline
Project editor and does not claim TwinCAT runtime behavior.

The final frozen regression ran against the unchanged baseline implementation
under `/private/tmp/embed-labs-details-draft-failure-final.4j1lxv`. Test setup
and cleanup passed, and the test failed exactly where a long Unicode Project A
draft containing literal `%1` had been replaced with Project A's persisted
name after Project B was renamed. The target exited with test status 1.

After the minimal implementation, focused normal-scale and
`QT_SCALE_FACTOR=2` runs each passed three events with target status 0 under
`/private/tmp/embed-labs-details-draft-focused-frozen-final.Y5vcy2`. The test
opens two real `.ecatproject` files, keeps Project A selected and its General
name editor focused, changes Project B through the real ProjectService, and
proves Project A's text, modified flag, focus, selection, context, title, and
persisted snapshot remain unchanged. It then changes Project A and proves the
same editor and Details title still refresh to the new persisted value, with
the modified flag cleared.

Complete normal-scale and 2x Workbench runs each passed 52 events under
`/private/tmp/embed-labs-details-draft-workbench-final.ygJUZO`. The six
isolated EtherCAT suites passed 103 events under
`/private/tmp/embed-labs-details-draft-six-suites-final.MG71MB`: Core 17,
Project 12, Devices 8, Workbench 52, Scan 7, and Diagnostics 7. Every target
exited with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611`, which contains exactly the 16
allow-listed plugin dylibs. Enabled product startup observed PID 1913 and
remained running for 36.010 seconds. Explicitly disabled startup observed PID
2968 and remained running for 36.011 seconds with
`-noload EtherCATWorkbench`. Evidence for both runs is under
`/private/tmp/embed-labs-details-draft-lifecycle-final.Zu4F0t`; each target was
running immediately before LLDB passed intentional SIGTERM and exited with
target status 15.

Cleanup found no residual target or LLDB process, new DiagnosticReports file,
or matching ReportCrash/CrashReporter unified-log event. Every executable used
fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. Manual desktop inspection was not run because this
issue changes no geometry. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue.

This issue changes only the existing private `detailsview.cpp`, Workbench test
declaration/implementation, and documentation. It intentionally does not add a
same-Project dirty/conflict merge policy: a current Project change remains
authoritative and refreshes current pages. It adds no source file, public API,
dependency, Provider, model role, persistence field, Project command, thread,
timer, controller/network transport, online state, or physical-hardware
behavior. No CMake or qbs description changed, so qbs was not run. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

## General property tree accessibility qualification

`ISSUE-WB-GENERAL-PROPERTY-TREE-A11Y-001` uses local baseline
`2b956da48104c97414da33190b9772e033969ee7`. Previously, the private General
page exposed its Property/Value tree visually but gave neither the widget nor
its cells explicit accessibility metadata. Long ESI names, source paths, and
owner-slave names could be visually elided without a standard item-role path
that recovered the complete value and its offline boundary.

The existing private `GeneralPage` tree now has a concise accessible name and
a description that identifies it as read-only offline data. Every Property and
Value cell publishes an actual `QString` through `AccessibleTextRole` equal to
its current display value. `AccessibleDescriptionRole` and `ToolTipRole`
publish the same complete description, including the visible column heading,
property name, complete value, and the fact that no controller, network, or
physical hardware is accessed. Empty values are described explicitly; visual
layout, elision, row order, context ownership, and Project behavior are
unchanged.

Qt defines `AccessibleTextRole` and `AccessibleDescriptionRole` as the
standard item-model accessibility data roles:
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Its QWidget contract
distinguishes a concise accessible name from a contextual accessible
description:
<https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0 provides both widget-level and model-role precedents:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's selected-terminal General tab documents the comparable information
hierarchy of Name, Id, Type, Comment, Disabled, and symbol settings:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.
That comparison guides information context only; Embed Labs remains an offline
Project editor and does not claim TwinCAT controller or network behavior.

The frozen regression first ran against the unchanged implementation under
`/private/tmp/embed-labs-general-property-a11y-failure-frozen.0HPnE7`. Setup
and cleanup passed, and the test failed exactly because the General property
tree accessible name was empty; the target exited with status 1. After the
minimal implementation, focused normal-scale and `QT_SCALE_FACTOR=2` runs each
passed three events with target status 0 under
`/private/tmp/embed-labs-general-property-a11y-focused-final.E1aBys`.

The focused test imports a real ESI description and exercises both the real
configured-slave General context and its model-generated Modules / Channels
context. It verifies every cell's exact role type and value, complete long
Unicode ESI/source/owner text, literal `%1`, `%2`, and `%%` recovery, header
and property context, tooltip equality, and read-only/offline/controller/
network/hardware boundaries. The offscreen normal render is 1100 by 720 with
SHA-256
`b4a03964e53a857323e44dc18a8614260a607c215a3509b8aa057011ebb731d0`;
the 2x render is 2200 by 1440 with SHA-256
`661d4ac799a3617fc94d8c8c0b3319d2d5731f486b0839628321efc447090afe`.
Both renders were inspected for readability, overlap, and scale drift. Visual
elision of intentionally huge values is preserved while the complete value is
available through standard metadata. This qualifies Qt metadata; no manual
VoiceOver reading is claimed.

Complete normal-scale and 2x Workbench runs each passed 53 events with target
status 0 under
`/private/tmp/embed-labs-general-property-a11y-workbench-final.D2MHZA`. The six
isolated EtherCAT suites passed 104 events under
`/private/tmp/embed-labs-general-property-a11y-six-suites-final.JfEsWm`: Core
17, Project 12, Devices 8, Workbench 53, Scan 7, and Diagnostics 7. Every
target exited with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611`, which contains exactly the 16
allow-listed plugin dylibs. Enabled product startup observed PID 78919 and
remained running for 36.005 seconds. Explicitly disabled startup observed PID
78918 and remained running for 36.006 seconds with
`-noload EtherCATWorkbench`. Evidence for both runs is under
`/private/tmp/embed-labs-general-property-a11y-lifecycle-final.WKCTbC`; each
target was running immediately before LLDB passed intentional SIGTERM and
exited with target status 15.

Cleanup found no residual target or LLDB process, new DiagnosticReports file,
or matching ReportCrash/CrashReporter unified-log event. Every executable used
fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue.

This issue changes only the existing private `generalpage.cpp`, Workbench test
declaration/implementation, and documentation. It adds no source file, public
API, dependency, Provider, custom model role, persistence field, Project
command, thread, timer, controller/network transport, online state, or
physical-hardware behavior. No CMake or qbs description changed, so qbs was
not run. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

## EtherCAT SyncManager cell accessibility qualification

`ISSUE-WB-ETHERCAT-SYNCMANAGER-CELL-A11Y-001` uses local baseline
`5cd0bade8355565a07969d97b49965bce93503b2`. This is the seven-column ESI
SyncManager defaults table on the EtherCAT property page, not the separately
qualified Process Data Sync Manager table. Previously, its widget had a name
and short description, but each `QTreeWidgetItem` exposed only display text.
Assistive technology and users hovering a visually long value therefore had
no explicit standard role that recovered the complete value and its local
offline boundary.

The existing private table now describes both configured-slave and repository
Device contexts as read-only offline ESI data that accesses no controller,
network, or physical hardware. Every SM, Name, Direction, Address, Size,
Control, and Enabled cell publishes an actual `QString` through
`AccessibleTextRole` equal to its complete Display value.
`AccessibleDescriptionRole` and `ToolTipRole` publish equal descriptions with
the visible column heading, SM index/name identity, complete value, ESI
source, and the same read-only offline boundary. Empty values retain an empty
accessible text and are described explicitly as Empty. Existing headers,
row order, resize modes, horizontal scrolling, selection, ESI parsing, and
Project behavior are unchanged.

Qt defines `ToolTipRole`, `AccessibleTextRole`, and
`AccessibleDescriptionRole` as standard `QString` item data and lets a
`QTreeWidgetItem` store role-specific values:
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qtreewidgetitem.html#setData>. Qt Creator 20.0 provides
widget- and model-level accessibility precedents in `FancyMainWindow` and the
Terminal model:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff documents the comparable selected-slave EtherCAT information context
and the field semantics of SyncManager start, length, and configuration data:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/4981170059.html>.
Those sources guide information hierarchy and field meaning only; Embed Labs
does not claim TwinCAT's controller configuration or online behavior.

The frozen regression ran against the unchanged production implementation
under
`/private/tmp/embed-labs-syncmanager-a11y-failure-final.eumVVB`. Setup and
cleanup passed, and the test failed exactly because configured-slave row 0 / SM
returned no `QString` through `AccessibleTextRole`; the target exited with
status 1. After the minimal implementation, focused normal-scale and
`QT_SCALE_FACTOR=2` runs each passed three events with target status 0 under
`/private/tmp/embed-labs-syncmanager-a11y-focused-final.cR27sE`.

The focused regression imports a real ESI file with a unique identity and a
long Chinese/Japanese/Unicode Outputs name containing literal `%1`, `%2`, and
`%%`. A second legal SM has an empty name and Unknown direction, proving that
its accessible text remains an empty `QString` while its description states
`unnamed` and `Empty`. The test switches the same real property page from a
configured-slave tree context to a repository Device tree context and back,
then verifies all two-by-seven cells on every pass: exact role type and value,
complete SM identity/value recovery, tooltip equality,
read-only/offline/ESI/controller/network/physical-hardware boundaries, and
non-editability. The normal offscreen render is 1100 by 720
with SHA-256
`24f70cf9b5fcbaf3d9757b495f553e12ce6b638fd64776ff59af5279c1ba78ba`;
the 2x render is 2200 by 1440 with SHA-256
`9247cb4e3560c385976f94077a47772182d1520c1fe3e1519ceefb8fc3c519a0`.
Both were inspected for readability, overlap, and scale drift. The deliberate
long-name horizontal extent remains reachable through the existing scroll
bar, while the complete value is available through standard metadata. This
qualifies Qt metadata; no manual VoiceOver reading is claimed.

Complete normal-scale and 2x Workbench runs each passed 54 events with target
status 0 under
`/private/tmp/embed-labs-syncmanager-a11y-workbench-final.RVRsLZ`. The six isolated
EtherCAT suites passed 105 events under
`/private/tmp/embed-labs-syncmanager-a11y-six-suites-final.k5a0Wn`: Core 17, Project
12, Devices 8, Workbench 54, Scan 7, and Diagnostics 7. Every target exited
with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611`, which contains exactly the 16
allow-listed plugin dylibs. Enabled product startup observed PID 86456 and
remained running for 36.004 seconds. Explicitly disabled startup observed PID
88370 and remained running for 36.001 seconds with
`-noload EtherCATWorkbench`. Evidence for both runs is under
`/private/tmp/embed-labs-syncmanager-a11y-lifecycle-final.yshQnN`; each target was
running immediately before LLDB passed intentional SIGTERM and exited with
target status 15. The disabled run emitted one non-fatal shared-memory
initialization message and then remained running for the full observation
period; it was not a crash or early exit.

Cleanup found no residual target or LLDB process, new DiagnosticReports file,
or matching ReportCrash/CrashReporter unified-log event. Every executable used
fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
crash dialog was created. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue.

This issue changes only the existing private `ethercatpage.cpp` and
`ethercatpage.h`, Workbench
test declaration/implementation, and documentation. It adds no source file,
public API, dependency, Provider, custom model role, persistence field,
Project command, thread, timer, controller/network transport, online state, or
physical-hardware behavior. No CMake or qbs description changed, so qbs was
not run. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

## Offline topology cell accessibility

`ISSUE-WB-TOPOLOGY-CELL-A11Y-001` uses local baseline
`23d67736335450d4fce299434f0e13024758cf2f`. It closes the remaining
standard-item metadata gap in the existing stack-local master Topology dialog.
It does not change the separately qualified dialog bounds, horizontal
scrolling, modal lifetime, configured order, or displayed values.

The Topology table now identifies its contents as read-only configured data
from the current offline Project. Every Position, Name, Auto Inc Addr,
Previous, Port, Vendor, Product, Revision, Alias, and Status cell publishes an
actual `QString` through `AccessibleTextRole` equal to its complete current
Display value. `AccessibleDescriptionRole` and `ToolTipRole` are equal and
contain the column heading, configured position, complete slave name, complete
cell value, and the same offline/controller/network/physical-hardware
boundary. The Port remains `Not modeled`, Previous remains configured-list
context rather than verified physical wiring, and `Offline configured` remains
a configuration status rather than an online EtherCAT state.

Qt defines the three standard string roles in
[Qt::ItemDataRole](https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum) and lets
`QTreeWidgetItem` store role-specific values through
[setData()](https://doc.qt.io/qt-6/qtreewidgetitem.html#setData). Qt Creator
20.0 provides local widget and model accessibility precedents in
[`FancyMainWindow`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247)
and
[`TerminalPane`](https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597).
Beckhoff documents that the selected master's Topology action displays
configured slaves and that its larger product can also show online data
([master EtherCAT page](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html),
[Topology dialog](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1277974411.html)).
Embed Labs deliberately qualifies only the local offline Project view and does
not claim found devices, runtime state, controller access, or physical-port
verification.

The frozen regression ran first against the unchanged production
implementation under
`/private/tmp/embed-labs-topology-cell-a11y-failure.z1vqwP`. Initialization and
cleanup passed, and the test failed exactly once because row 0 / Position did
not return a `QString` through `AccessibleTextRole`; the target exited with
test status 1. After the minimal implementation, focused normal-scale and
`QT_SCALE_FACTOR=2` runs each passed three events with target status 0 under
`/private/tmp/embed-labs-topology-cell-a11y-focused-final.PDcyTx`.

The focused regression opens a real `.ecatproject`, selects its real Master,
uses the actual Details page and modal Topology action, and verifies all 20
cells across two configured slaves. Its long Chinese/Japanese/Unicode names
contain literal `%1`, `%2`, and `%%`; the second row's Previous cell preserves
the complete first name. It also verifies exact role types and values, all ten
headers and displayed values, row identity, tooltip equality, non-editability,
and every read-only/offline/Project/controller/network/physical-port/hardware
boundary. The inspected normal render is 784 by 279 with SHA-256
`e5abaf35897884abea0ed9dd9c582d7aa7fa1746f139c415209675f51c2ba014`;
the 2x render is 768 by 558 with SHA-256
`fdedc08c0be16825085bcb03a3b7d962c137b9b4b3df97af2f6b29e994bef7d9`.
Both retain the existing horizontal recovery and usable Close button without
overlap or scale drift. This qualifies Qt metadata; no manual VoiceOver
reading is claimed.

Complete normal-scale and 2x Workbench runs each passed 55 events with target
status 0 under
`/private/tmp/embed-labs-topology-cell-a11y-workbench-final.BFPQdu`. The six
isolated EtherCAT suites passed 106 events under
`/private/tmp/embed-labs-topology-cell-a11y-six-suites-final.HcMvzr`: Core 17,
Project 12, Devices 8, Workbench 55, Scan 7, and Diagnostics 7. Every target
exited with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611`, which contains exactly the 16
allow-listed plugin dylibs. Enabled product startup observed PID 98032 and
remained running for 36 seconds after observation under the `enabled-evidence-1310`
directory. Explicitly disabled startup observed PID 99181 and remained running
for 36 seconds after observation with `-noload EtherCATWorkbench` under
`disabled-evidence-1312`. Both directories are under
`/private/tmp/embed-labs-topology-cell-a11y-product-lifecycle-final.TaMURB`;
each target was running immediately before LLDB passed intentional SIGTERM and
exited with target status 15. The disabled run emitted one non-fatal
shared-memory initialization message and continued through the full interval.

Cleanup found no residual Embed Labs or LLDB process, new DiagnosticReports
file, or matching ReportCrash/CrashReporter unified-log event after
2026-07-19 13:09:58 +0800. Every executable used fresh HOME/settings, cleared
inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar LLDB breakpoint. No visible main window or crash dialog was created.

This issue changes only the existing private `ethercatpage.cpp`, Workbench
test declaration/implementation, and documentation. It adds no source file,
public API, dependency, Provider, custom model role, persistence field,
Project command, production thread or timer, controller/network transport,
online state, physical-port model, or physical-hardware behavior. No CMake or
qbs description changed, so qbs was not run. The unrelated `WITH_TESTS=ON`
all-target build was not rerun; the known EasyBoard test include blocker
remains outside this private Workbench issue. The Workbench path count remains
44 and the direct upstream Core patch count remains five.

## Repository Device DC mode preview

`ISSUE-WB-DC-REPOSITORY-MODE-PREVIEW-001` uses local baseline
`266325f933f0072c61ccec29ec1059d7e1610a29`. It closes a direct gap between
the existing DC-page contract and its visible repository behavior. The ESI
parser already supplied two operation modes and the page already inserted both
into its `QComboBox`, but every non-editable context disabled the entire
selector. A repository user could therefore see only the first mode and could
not inspect the second mode's AssignActivate or SYNC0/SYNC1 values.

The private `DcPage` now enables the operation-mode selector only when the
current context is an ESI Repository Device with at least one parsed mode, or
when the existing configured-slave page is genuinely editable. In the
repository case the combo's line editor remains read-only, and every Enable,
AssignActivate, SYNC0, SYNC1, and potential-reference-clock control remains
disabled or read-only. Activating another ESI item copies
`dcConfigurationFromMode()` into the page's existing transient presentation
value and rebuilds the controls. It returns before `submitConfiguration()` and
therefore cannot call `ProjectService`, create Undo history, or persist the
selection. Clearing and restoring the Device context reconstructs the page
from the immutable ESI description and returns to the first mode.

The visible summary and the selector's localized accessible description and
tooltip state that this is a read-only offline preview that does not modify a
Project or access a controller, network, or physical hardware. Configured-
slave selection remains on the original checked Project command path. Invalid
Project and other non-editable contexts remain disabled and gain no preview
permission.

Qt documents that `QComboBox::activated()` represents a user choice and that
the combo stores and exposes its items through the model/view framework:
<https://doc.qt.io/qt-6/qcombobox.html>. Qt also defines a localized widget
description as contextual information for assistive technology:
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0's plugin architecture remains the host boundary; the change stays in the
existing product plugin and uses no application bootstrap or upstream Core
patch:
<https://doc.qt.io/qtcreator-extending/creating-plugins.html>. Beckhoff's
Distributed Clock page is only the user-facing field and operation-mode
comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.
Embed Labs continues to omit Sync Unit task-cycle derivation and all online
controller behavior.

The failure-first test was compiled against the exact baseline production
`dcpage.cpp` blob `487c6d3e368925833d7e907d281c92c250d89fc2`. Initialization
and cleanup passed, and the target failed exactly because the repository mode
selector was disabled; target status was 1. Its complete build, source-blob,
LLDB, and status evidence is under
`/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6/failure`. After the
minimal implementation, focused normal-scale and `QT_SCALE_FACTOR=2` runs each
passed three events with target status 0 under the same root's
`focused-normal` and `focused-2x` directories. The companion
`testBuiltInDevicePages` normal and 2x runs also passed under `builtins-normal`
and `builtins-2x`.

The focused regression imports a real unique ESI Device with `Sync0` and
`Sync0 + Sync1`, opens its real Details/DC page, focuses the enabled read-only
selector, and sends an actual keyboard Down event. It verifies the second
mode's `0x0700` AssignActivate, SYNC0 `250000 / -1000`, and SYNC1
`500000 / 1000` values; all mutation controls remain read-only or disabled;
the selector description contains the read-only boundary; no Project exists;
and re-entering the Device restores `Sync0`. An interim version of the reset
assertion retained a destroyed test-only combo pointer. LLDB caught that
regression-harness error in-process; the test now uses `QPointer`, waits for
destruction, and resolves the recreated control before checking reset. The
persisted full-turn audit from 13:30 found no new DiagnosticReports file or
matching ReportCrash/CrashReporter/diagnosticd event, and all subsequent
focused and complete runs passed.

Complete normal-scale and 2x Workbench runs each passed 56 events with target
status 0 under `workbench-normal` and `workbench-2x` in the same final evidence
root. The six isolated EtherCAT suites passed 107 events under `six-suites`:
Core 17, Project 12, Devices 8, Workbench 56, Scan 7, and Diagnostics 7. Every
target exited with status 0.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611` and contains exactly the 16
allow-listed plugin dylibs. Enabled product startup observed PID 21400 and
remained running for 37 seconds. Explicitly disabled startup observed PID
28526 and remained running for 37 seconds with
`-noload EtherCATWorkbench`. Evidence is under
`/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6`; each
target was running immediately before LLDB passed intentional SIGTERM and
exited with target status 15. The disabled run emitted one non-fatal shared-
memory initialization message and remained alive for the complete interval.

Final cleanup found no residual Embed Labs or LLDB process, new
DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd
unified-log event after 2026-07-19 13:56:24 +0800; the broader persisted audit
from 13:30 is also empty. Every executable used fresh
HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
system crash dialog was created.

This issue changes only the existing private `dcpage.cpp`, Workbench test
declaration/implementation, and documentation. It adds no source file, public
API, dependency, Provider, persistence field, Project command, model role,
production thread or timer, controller/network transport, online state, or
physical-hardware behavior. No CMake or qbs description changed, so qbs was
not run. The unrelated `WITH_TESTS=ON` all-target build was not rerun; the
known EasyBoard test include blocker remains outside this private Workbench
issue. The Workbench path count remains 44 and the direct upstream Core patch
count remains five.

## Repository Device DC empty state

`ISSUE-WB-DC-REPOSITORY-EMPTY-001` uses local baseline
`f60f0fc0939b3945c4026851e9f4e86e15e2d9be`. It qualifies the inverse of the
repository mode-preview path. An imported Device whose valid ESI description
contains no `<Dc>` modes previously displayed “Select an operation mode” and
the green “offline configuration is valid” state even though the selector had
zero items and was disabled. The visible instructions, validation state, and
actual control state therefore disagreed.

The private `DcPage` now records whether the current repository Device still
has an ESI description and whether that description is supported. It derives a
dedicated repository-empty presentation only when the description exists and
`m_esiModes.isEmpty()`. For a supported Device, the summary says that no ESI
Distributed Clocks operation mode is available and directs users to an offline
Project for manual timing. `showValidation()` uses the existing informational
`InfoLabel` type and says that there is no mode to preview rather than claiming
that an absent configuration is valid. The selector remains at count zero,
index `-1`, disabled, and read-only. Its localized accessible description and
equal tooltip disclose the same empty state and the
no-controller/no-network/no-physical-hardware boundary. Every configuration
field remains disabled or read-only.

An imported Device with unsupported ESI structures and zero modes is a warning
state, not the supported recovery above. Because the existing offline topology
gate rejects that Device, the summary, validation, accessible description, and
tooltip explicitly say that it cannot be added to an offline Project and send
the user to Device Repository to review its support details.

An unsupported Device that still contains DC modes may continue to preview
them locally, but it receives the same cannot-add and Device Repository
disclosure in the summary, validation, accessible description, and tooltip. A
valid mode produces a support warning; an invalid mode retains its configuration
error or warning and appends the support recovery instead of hiding either
diagnostic.

A stale Device context whose ESI description was removed is a distinct error
state. It directs the user back to Device Repository, shows an unavailable
warning instead of the valid empty-Device recovery, and never suggests adding
the missing Device to a Project. Its selector and all configuration controls
remain disabled/read-only with the same no-controller/no-network/no-hardware
boundary.

The repository-empty branch does not synthesize a mode, create a placeholder
Project configuration, or grant mutation permission. Adding a supported Device
to a valid offline Project continues to expose the existing manual
operation-mode entry path; an unsupported Device remains rejected by the
existing topology gate. Repository page teardown destroys all presentation
state; entering the same Device again reconstructs the same empty state from
the immutable ESI description. The existing repository Device with modes
remains keyboard-previewable, and configured-slave validation, Project
submission, Undo/Redo, and persistence are unchanged.

Qt documents that `QComboBox::count()` is zero and its current index is `-1`
when the combo is empty: <https://doc.qt.io/qt-6/qcombobox.html>. Qt's localized
contextual description contract is documented at
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0 uses an explicit “No type hierarchy available” label for its own empty
widget state rather than presenting an unavailable action:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.
Beckhoff says an operation mode can be selected when a slave offers several
modes:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.
Embed Labs therefore keeps selection guidance only for non-empty imported ESI
mode lists and adds no Sync Unit task-cycle or online-controller behavior.

The final frozen regression at `ethercatworkbenchtests.cpp` blob
`565efe921bf081d0189e87d7903f789ffb86ddbd` and declaration blob
`c37bef9044421f46e9e2fca43604065e9d729b7a` was compiled against exact
baseline production blobs `461e9e9dd8920e38c0583055ea52ee52894a2ab6`
for `dcpage.cpp` and `6cbde2504622c7676a7f8336844d155ce914e76b`
for `dcpage.h`. Initialization and cleanup passed, and the target failed
exactly because the old summary still said “Select an operation mode”; target
status was 1. Complete source-blob, build, LLDB, and status evidence is under
`/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu/failure-seal`.

After the minimal implementation, production blobs
`7038760e04a580e725926ae25a5c0e7ba491cf0c` /
`a63c58b599ab0ef4327ca7c5322fc3c1fb54fbd1` and the frozen test blobs produced
test-plugin SHA-256
`a963dd82d883ffd6d9e43d383aa8f9da6da4b3258088918265497dac6155c939`.
Focused normal-scale and `QT_SCALE_FACTOR=2` runs each passed three events with
target status 0 under the same root's `seal-focused-normal` and
`seal-focused-2x` directories. The companion repository mode-preview
regression also passed three events at both scales under
`seal-preview-normal` and `seal-preview-2x`. The empty-state regression
imports a real unique ESI Device after removing its `<Dc>` element, opens the
real Details/DC page, and verifies the explicit summary and informational
status, zero-item disabled/read-only selector, complete tooltip/accessibility
boundary, all read-only mutation controls, an empty Project list, and context
teardown and reconstruction. A random removed Device ID additionally verifies
the separate unavailable warning, Device Repository recovery, and the absence
of an invalid offline-Project instruction. A second real imported ESI Device
adds an unsupported `<Modules>` structure and verifies the warning, impossible
Project-add disclosure, and Device Repository support-review recovery. A third
unsupported Device retains two real DC modes: its first mode verifies the
support warning, while its deliberately invalid second mode verifies that the
configuration error and support recovery are both preserved.

Complete normal-scale and 2x Workbench runs each passed 57 events with target
status 0 under `seal-workbench-normal` and `seal-workbench-2x`. The six isolated
EtherCAT suites passed 108 events under `seal-six-suites`: Core
17, Project 12, Devices 8, Workbench 57, Scan 7, and Diagnostics 7. Every
target exited with status 0.

Each complete Workbench log retains the pre-existing ProjectExplorer TaskHub
soft assertion emitted by `testInvalidProjectPresentationAndLifecycle`; the
same diagnostic is present in the preceding Workbench qualification logs. It
does not occur in the new DC regression, fail a test, or change target status,
and remains outside this single-issue DC presentation change.

The full `WITH_TESTS=OFF` product build passed in
`qt-creator-build-ethercat-product-qt611` and contains exactly the 16
allow-listed plugin dylibs; its Workbench plugin SHA-256 is
`0d31da4ed84c640897b00380ff1693549a4347d8070cc3f3bff075db5d06b09f`.
Enabled product startup observed PID 45433 and remained running for 37 seconds
under `seal-lifecycle-enabled`. Explicitly disabled startup observed PID 45427
and remained running for 37 seconds with `-noload EtherCATWorkbench` under
`seal-lifecycle-disabled`. Evidence is under
`/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu`; each target was
running immediately before LLDB passed intentional SIGTERM and exited with
target status 15.

Final cleanup found no residual Embed Labs or LLDB process, new
DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd
unified-log event after 2026-07-19 17:14:35 +0800. Every executable used fresh
HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar LLDB breakpoint. No visible main window or
system crash dialog was created.

This issue changes only the existing private `dcpage.cpp` and `dcpage.h`,
Workbench test declaration/implementation, and documentation. It adds no
source file, public API, dependency, Provider, persistence field, Project
command, model role, production thread or timer, controller/network transport,
online state, or physical-hardware behavior. No CMake or qbs description
changed, so qbs was not run. Visual/manual desktop inspection was intentionally
not run because the issue changes text/state only and all executable
qualification stayed offscreen. The unrelated `WITH_TESTS=ON` all-target build
was not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue. The Workbench path count remains 44 and the direct
upstream Core patch count remains five.
