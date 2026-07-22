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
`LastLeftAligned` area. It is visible only in EtherCAT Mode and merges the
existing `StateService` entries with a value-only projection from the
deterministically preferred public Diagnostics Provider. The highest severity
selects a Ready, Busy, Warning, or Fault icon and a short textual state; an
empty service with no active Diagnostics state displays `Offline`.

Local phase-1 Diagnostics snapshots remain explicitly labeled `MOCK`. While
the preferred local Mock stream is running, the compact text distinguishes
`MOCK Config / PREOP`, `MOCK FreeRun / SAFEOP`, and `MOCK Run / OP`. A future
or test Provider that reports `mock=false` uses neutral `Diagnostics` wording
and is identified as Provider-reported only; the Workbench does not infer a
controller or physical-hardware connection from that value.

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

A rejected inline Return leaves the last accepted Mock bytes and Project state
unchanged and keeps the existing CoE feedback label visible as an Error. The
message names the object and distinguishes normalized-empty input, an
incomplete hexadecimal byte, illegal hexadecimal characters, and an applicable
fixed-width mismatch. Reopening the same editor or pressing Escape keeps that
guidance available; a later accepted Return or an explicit selection, source,
refresh, context, or teardown boundary clears it. When Qt accessibility is
available, the page also requests a polite accessibility announcement; this is
an event-path qualification, not a claim of manually verified screen-reader
speech.

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
Undo/Redo reentrancy. The Startup workflow covers the populated repository ESI
catalogue, explicit defaults storage, fixed requests, New/Edit/Delete dialogs,
enable state, ordering, type/value validation, configured-slave manual no-ESI
empty state, and real ProjectService Undo/Redo reentrancy. The CoE Online
workflow covers the
TwinCAT-inspired object hierarchy and controls, ESI/offline/Mock sources,
manual refresh, advanced and Unicode filters, offline, object-level, and
repository-device read-only boundaries, configured-slave raw-value editing,
real-editor rejection feedback for normalized-empty, incomplete-byte,
illegal-hex, and applicable fixed-width input, valid normalized correction,
stale-feedback cleanup, cancelled and confirmed Add to Startup, no-overwrite
behavior, no-ESI state, Project modified state, and Undo. The repository
regression preserves ESI `RW`
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
dictionary. Switching to a different Project, node ID, or node kind clears the
old address and advanced-filter state, so neither can leak between devices.
Same-stable-context refresh continuity is qualified separately by
`ISSUE-WB-COE-SAME-CONTEXT-VIEW-STATE-001`. The Mock/offline source choice,
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

## Repository Device Process Data empty state

`ISSUE-WB-PROCESS-DATA-REPOSITORY-EMPTY-001` is based on local commit
`8818c6c5c48a0e3c7b1e3a770c758401c926e32c`. The repository Device Process
Data page now distinguishes actual imported ESI states instead of treating
every zero-row or invalid catalogue as a valid mapping that should be selected
or added.

| Repository context | Visible and validation state | Recovery and permission |
|---|---|---|
| Supported Device with no parsed PDO | Five tables have zero rows; summary says no ESI Process Data mapping; `InfoLabel` is Information | The Device may be added to a valid offline Project with an empty mapping, but Workbench does not fabricate Sync Managers or PDOs; review the source or import a matching ESI in Device Repository |
| Unsupported Device with no parsed PDO | Zero rows and Warning | Cannot be added; review unsupported structures in Device Repository |
| Supported Device with invalid parsed PDO | Read-only preview and Error with the first detail visible and all details in the tooltip | Cannot be added by the checked Project path; import a corrected matching ESI through Device Repository |
| Unsupported Device with parsed PDO | Read-only preview; Warning when valid, original Error/Warning when invalid | Cannot be added; the support recovery is appended without hiding validation details |
| Removed Device description | Zero rows and unavailable Warning | Return to Device Repository; no Project-add instruction is shown |
| Supported Device with valid parsed PDO | Existing read-only catalogue and Ok validation | Existing select/inspect and offline-Project add guidance remains unchanged |

The Sync Manager, PDO Assignment, PDO List, PDO Content, and Process Image
tables retain their existing base accessible descriptions. Each repository
context appends a localized state, recovery, read-only, and
no-controller/no-network/no-physical-hardware description and exposes the same
text as its tooltip. Reusing the page rebuilds each description from the base,
so empty, unsupported, invalid, and removed text cannot leak into a later valid
Device. Empty model state is represented by real zero `rowCount()` values; no
placeholder rows or synthetic identifiers are created.

Repository browsing stays presentation-only. The page reads the immutable
`DeviceDescription`, derives existing Process Data defaults, and never calls a
Project command. The regression opens a real valid offline Project after the
page-state checks and proves that repeated repository browsing preserves the
complete snapshot, active Project, and Undo/Redo availability. It then proves
the advertised recovery separately: one supported empty Device adds exactly
one Slave whose Sync Manager/PDO lists remain empty and the change is undoable;
unsupported empty/populated Devices and supported-invalid/unsupported-invalid
Devices are rejected while the Project snapshot remains equal by
`ProjectSnapshot` field comparison.

Beckhoff describes Sync Manager, PDO Assignment, PDO List, and PDO Content
views and also documents online download/activation functions:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.
Embed Labs uses only the offline ESI presentation structure; it does not add
PDO download, controller state transitions, online reads, or physical-device
access. Qt documents that `rowCount()` is the model's real row cardinality and
that read-only models reject `setData()`:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#rowCount> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>. Qt documents that an
accessible description should explain what a widget does and must be
localized:
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0 supplies the host precedent for an explicit unavailable state in Type
Hierarchy:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

The first failure-first run compiled the new red assertion against exact
unchanged production blobs `21c608c1e549e36d364cf2f7cfe5e9c9ccbb774a` /
`95d882ba1d2ff295865f5198916a3a77328bafba`. Initialization and cleanup passed,
and the target failed exactly because the old supported-empty summary did not
contain “No ESI Process Data mapping”; target status was 1 under
`/private/tmp/embed-labs-process-data-repository-empty.QFzGu0/failure-first`.
An independent review then identified the supported-but-invalid recovery gap.
The supplemental red run passed initialization/cleanup and failed because the
summary did not contain “validation error”; target status was 1 under the same
root's `review-failure-first` directory.

Final production blobs are
`94832604f9589ee2acdf8b5133ce9c6c91bf328a` /
`ad62cb47a8d5c7d92b3057a6577c31d5f901a82a`; final test blobs are
`f55311d7bcc6fba1b8c178cf5d42d527a07e3c52` /
`956eed21dd14e4a9c69e2e098d70b2024e694e58`. The test-build Workbench plugin
SHA-256 is
`c285a33a68939fac96a638cff311f04ebe6ae433dd0dc040a272d188ac2cacee`.
Focused normal and `QT_SCALE_FACTOR=2` runs each passed 3 events under
`focused-sealed-final-normal` and `focused-sealed-final-2x`. Process Data
companion runs each passed 7 events under `process-data-companion-sealed-normal`
and `process-data-companion-sealed-2x`. Complete Workbench runs each passed 58
events under `workbench-sealed-normal` and `workbench-sealed-2x`. The six
isolated suites under `six-suites-sealed-sequential` passed 109 events: Core
17, Project 12, Devices 8, Workbench 58, Scan 7, and Diagnostics 7. Every test
target exited with status 0; all paths are relative to
`/private/tmp/embed-labs-process-data-repository-empty.QFzGu0`.

Each complete Workbench run retains the known pre-existing ProjectExplorer
TaskHub soft assertion in `testInvalidProjectPresentationAndLifecycle`. It is
also present in preceding qualifications, does not occur in the new focused
test, does not fail a test, and does not change target status.

The full `WITH_TESTS=OFF` product build passed and contains exactly the 16
allow-listed plugin dylibs. Its Workbench plugin SHA-256 is
`42e53a365199b9a734bf94ec10a069552259a3f4d612b3b930186df8f4ff204a`.
Enabled startup observed PID 61420 and disabled startup observed PID 61431
with `-noload EtherCATWorkbench`; both main programs remained continuously
running for 37 seconds under `lifecycle-enabled-sealed` and
`lifecycle-disabled-sealed`. LLDB passed the intentional final SIGTERM directly
to each target, and both exited with target status 15.

An interim test-harness investigation caught a stale `QModelIndex` after the
Project tree reset. LLDB intercepted it before system crash handling; the final
test reacquires every repository index by stable NodeId. The harness also now
stores the result of the mutating Add and Undo calls before applying
`QVERIFY_RESULT`, preventing the macro's diagnostic expression from evaluating
a side effect twice. The final crash audit found no residual Embed Labs/LLDB
process, new matching DiagnosticReports file, or matching
ReportCrash/CrashReporter/diagnosticd unified-log event after
2026-07-19 17:52:11 +0800.

Every executable used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar bypass. No visible main window or system crash
dialog was created. Manual desktop inspection was intentionally not run because
this issue changes text/state only. No CMake or qbs description changed, so qbs
was not run. The unrelated `WITH_TESTS=ON` all-target build was not rerun; the
known EasyBoard test include blocker remains outside this private Workbench
issue.

This issue changes only the existing private `processdatapage.cpp` and
`processdatapage.h`, Workbench test declaration/implementation, and these four
documents. It adds no source file, dependency, public API, Provider,
persistence field, Project command, custom model role, production thread or
timer, controller/network transport, online state, or hardware behavior. No
upstream Core, ProjectExplorer, or application-bootstrap path changed. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

## Repository Device Startup empty state

`ISSUE-WB-STARTUP-REPOSITORY-EMPTY-001` is based on local commit
`24576f40eab24825baea29047cda750136a82819`. The repository Device Startup page
now distinguishes the actual imported ESI state instead of giving every
zero-row catalogue the generic instruction to add the Device and reporting a
valid zero-request configuration.

| Repository context | Visible and validation state | Recovery and permission |
|---|---|---|
| Supported Device with no parsed Startup request | The table has zero real rows; the summary says no ESI Startup request is available; `InfoLabel` is Information | The existing checked topology path may add the Device to a valid offline Project with empty Startup, where requests can later be entered manually; the repository page fabricates none |
| Unsupported Device with no parsed Startup request | Zero rows and Warning | Cannot be added; review unsupported structures in Device Repository |
| Supported Device with invalid parsed Startup | Read-only populated preview and Error; the first validation reason is visible and all existing issues remain in the tooltip | Cannot be added by the checked Project path; import a corrected matching ESI through Device Repository |
| Unsupported Device with invalid parsed Startup | Read-only populated preview and Error; validation and support restrictions are both visible | Cannot be added; review errors and unsupported structures in Device Repository |
| Unsupported Device with otherwise populated Startup | Read-only populated preview; existing validation is retained and the support notice is Warning | Cannot be added; the imported catalogue remains inspectable without granting mutation |
| Removed Device description | Zero rows and unavailable Warning | Return to Device Repository; no Project-add instruction is shown |
| Supported Device with populated Startup | Existing read-only catalogue and existing validation remain; the qualification fixture retains its three source data-type warnings | Existing inspect and offline-Project Add guidance remains; preview never sends a request |

The table keeps its localized base accessible description. Every Device
`setContext()` derives a fresh contextual description and equal tooltip from
that base, so empty, unsupported, invalid, and removed text cannot leak when
the same page is reused for a valid populated Device. A removed Device also
clears the prior validation-detail tooltip. Empty state is represented by a
real zero `rowCount()`; no placeholder row, object address, raw value, stable
identifier, or request is synthesized.

Repository state remains completely read-only. New, Edit, Delete, Move
Up/Down, enable toggles, and Store/Restore are unavailable. For every cell in
both populated-valid and populated-invalid catalogues, editable and checkable
flags remain absent; direct `EditRole` and `CheckStateRole` `setData()` calls
return false and leave display/check values unchanged. Selecting a row changes
only page presentation. The table description and tooltip explicitly disclose
that the preview does not modify a Project, send an SDO, or access a
controller, network, or physical hardware.

Repository browsing itself performs no recovery command. The page reads the
immutable `DeviceDescription`, copies the existing Startup defaults, derives
private availability/support/data/error facts, and presents them. A regression
opens a real offline Project and proves that browsing all six imported Device
states preserves its complete `ProjectSnapshot`, active Project, and Undo/Redo
availability. It then calls the existing checked controller path separately:
one supported-empty Device creates exactly one Slave with zero Startup
requests, Undo restores the complete prior snapshot and establishes Redo;
unsupported empty/populated Devices and supported-invalid/unsupported-invalid
Devices are rejected while the complete snapshot and Undo/Redo state remain
unchanged. A scope guard owns the opened Project immediately so any assertion
failure also clears selection, removes the Project, and drains posted events.

Beckhoff describes Startup as an ordered list of mailbox download requests and
documents their fields and actions:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.
Embed Labs uses that structure only for imported offline ESI presentation and
configured-slave editing; repository preview does not transmit a request.
Qt documents that `rowCount()` is the model's real cardinality and that a
read-only model rejects `setData()`:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#rowCount> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>. Qt's accessible
description contract requires localized purpose/context text:
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0 supplies the host precedent for an explicit unavailable state in Type
Hierarchy:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

The failure-first test was compiled while production remained at exact blobs
`9b3052173109daee22678ae12b98f77725947885` /
`f1c06438065bf252d7cc903b6c671798ca768f64`. Initialization and cleanup passed,
and the new assertion failed exactly because the old supported-empty summary
did not contain “No ESI Startup”; target status was 1 under
`/private/tmp/embed-labs-startup-repository-empty.OSpPLZ/failure-first`.

Final production blobs are
`9a67969dfacba88fe429470b3822330c70dde817` /
`2df41415e73ee3506a9cbff1a6b6fa08e59e7746`; final test blobs are
`5d86453db7cc94fcbf7d8fb2061f772c5e4fedd7` /
`e4ff4f2652dee1dd04bb01d8a54325a879f909dd`. The final test-build Workbench
plugin SHA-256 is
`a78e961f1aa3ee029950201ea9205a6e0f80a5171542ea998eb3f3165e78aed0`.
Focused normal and `QT_SCALE_FACTOR=2` runs each passed 3 events under
`sealed-focused-normal` and `sealed-focused-2x`. Startup companion runs each
passed 8 events under `sealed-startup-companion-normal` and
`sealed-startup-companion-2x`. Complete Workbench runs each passed 59 events
under `sealed-workbench-normal` and `sealed-workbench-2x`. The six isolated
suites under `sealed-six-suites` passed 110 events: Core 17, Project 12,
Devices 8, Workbench 59, Scan 7, and Diagnostics 7. Every test target exited
with status 0; all directories are below the same evidence root.

Each complete Workbench log retains the known pre-existing ProjectExplorer
TaskHub soft assertion in `testInvalidProjectPresentationAndLifecycle`. It is
absent from the new focused test, does not fail a test, and does not alter
target status.

The full `WITH_TESTS=OFF` product build passed and contains exactly the 16
allow-listed plugin dylibs. Its Workbench plugin SHA-256 is
`59715209a6979a974fe3a96403f035bb8c04e57212a070fdcb7e352451a71552`.
Enabled startup observed PID 69283 for 37 consecutive one-second samples;
explicitly disabled startup observed PID 70440 for the same 37 samples with
`-noload EtherCATWorkbench`. LLDB passed the intentional final SIGTERM directly
to each target, and each exited with target status 15 under
`lifecycle-enabled-final` and `lifecycle-disabled-final`.

The final crash audit found no residual Embed Labs/LLDB process, new matching
DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd
event after 2026-07-19 19:31:02 +0800. Every executable used fresh
HOME/settings, cleared inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar bypass. No visible main window or system crash dialog was created. Manual
desktop inspection was intentionally not run because this issue changes
text/state only and all executable acceptance remained offscreen.

This issue changes only the existing private `startuppage.cpp` and
`startuppage.h`, Workbench test declaration/implementation, and these four
documents. It adds no source file, dependency, public API, Provider,
persistence field, Project command, custom model role, production thread or
timer, controller/network transport, SDO execution, online state, or hardware
behavior. Configured-slave editing, validation, persistence, and Undo/Redo are
unchanged. No CMake or qbs description changed, so qbs was not run. The
unrelated `WITH_TESTS=ON` all-target build was not rerun; the known EasyBoard
test include blocker remains outside this private Workbench issue. No upstream
Core, ProjectExplorer, or application-bootstrap path changed. The Workbench
path count remains 44 and the direct upstream Core patch count remains five.

## Repository Device EtherCAT SyncManager empty state

`ISSUE-WB-ETHERCAT-REPOSITORY-EMPTY-001` is based on local commit
`5c88012561d3317b6d689f31668a0a31c2bd66d1`. The repository Device EtherCAT
page now distinguishes the actual imported ESI SyncManager state instead of
describing every existing Device as having defaults and leaving an empty
seven-column table visible.

| Repository context | Visible state | Recovery and permission |
|---|---|---|
| Supported Device with no parsed SyncManager | Explicit no-SyncManager summary; the empty tree is hidden and contains zero rows | Review source and qualification in Device Repository or import a matching ESI; Workbench fabricates no row |
| Unsupported Device with no parsed SyncManager | Explicit no-SyncManager and unsupported disclosure; zero rows and hidden tree | Cannot be added to an offline Project; review support details in Device Repository |
| Unsupported Device with parsed SyncManagers | Existing two-row read-only preview remains visible | Cannot be added; the preview retains Device Repository recovery and grants no edit permission |
| Unresolved/missing Device description | Explicit unavailable summary; zero rows and hidden tree | Return to Device Repository and select an available Device |
| Supported Device with parsed SyncManagers | Existing seven-column, two-row read-only offline preview remains visible | Inspection only; no Project, controller, network, or physical hardware is touched |

The summary has a translated accessible name. Each `setContext()` publishes
the current summary as its accessible description and equal tooltip. The
SyncManager tree receives a fresh localized name, description, and equal
tooltip for the current repository state. Reusing one page across supported
empty, unsupported empty, unsupported populated, unresolved, and supported
populated contexts clears every visible stale row, warning, and recovery
phrase; empty and unresolved states hide the tree. The populated tree
continues to expose the previously qualified seven-column cell roles and
remains non-editable.

This is presentation derived from the existing immutable
`DeviceDescription::syncManagers` and `DeviceSummary::supported` values. The
page does not infer a controller configuration, synthesize a SyncManager,
discover a target, add a Device, or create Project history. The regression
starts with no Project and proves that browsing every state still leaves the
Project list empty.

Beckhoff documents the EtherCAT slave page as a view that lists the current
Sync Manager configuration:
<https://infosys.beckhoff.com/content/1033/tcsystemmanager/1092536331.html>.
Embed Labs uses that structure only as an imported offline ESI preview. Qt's
`QTreeWidget` is an item-model view whose real `topLevelItemCount` reports the
available rows, and Qt exposes localized purpose/context through QWidget
accessibility properties:
<https://doc.qt.io/qt-6/qtreewidget.html#topLevelItemCount-prop> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0 supplies the host precedent for replacing unavailable content with an
explicit message in Type Hierarchy:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

The failure-first test was compiled while production remained at exact blobs
`c637f6fffa956e3e79be20302911b87e52cf41c061294cdce7aee84f78d55f08` /
`e414a1557a76ce5150e8a5f1e2b62131787f2548118699c9d45e7d3f2827f011`.
Initialization and cleanup passed, and the new assertion failed exactly
because the old supported-empty summary did not contain “No ESI SyncManager”;
the target exited with status 1 under `failure-first`.

Final production blobs are
`0f264b16c975d3ae86fddf5d7b979a0b40ab49039a23fd30ea565992460426b3` /
`e414a1557a76ce5150e8a5f1e2b62131787f2548118699c9d45e7d3f2827f011`;
final test blobs are
`944e2ab60c815fd1badf7a453a9acbc10c478ced7ace4c5138e76dda7ba5f5ce` /
`1250eacb1b82f94aeb8cef4ddb27e439bd1e689b684c04fb8ca152764b863abe`.
The final test-build Workbench plugin SHA-256 is
`b5f333199be872e658f7592f070bfac50f76722fe0d4b7b743354902b2bb5b5c`.
Focused normal and `QT_SCALE_FACTOR=2` runs each passed 3 events under
`review-final2-focused-normal` and `review-final2-focused-2x`. Complete
Workbench runs each passed 60 events under `review-final2-workbench-normal`
and `review-final2-workbench-2x`. The six isolated suites under
`review-final2-six-suites` passed 111 events: Core 17, Project 12, Devices 8,
Workbench 60, Scan 7, and Diagnostics 7. Every test target exited with status
0; all paths are relative to
`/private/tmp/embed-labs-ethercat-repository-empty.mBBHJJ`.

Each complete Workbench log retains the known pre-existing ProjectExplorer
TaskHub soft assertion in `testInvalidProjectPresentationAndLifecycle`. It is
absent from the new focused test, does not fail a test, and does not alter
target status.

The full `WITH_TESTS=OFF` product build passed and contains exactly the 16
allow-listed plugin dylibs. Its Workbench plugin SHA-256 is
`1eee835eec11d74d74b27e0a600306383962040e18391c1cacfbc0adfc7f0835`.
Enabled startup observed PID 50382 for 37 consecutive one-second samples;
explicitly disabled startup observed PID 51949 for the same 37 samples with
`-noload EtherCATWorkbench`. LLDB passed the intentional final SIGTERM directly
to each target, and each exited with target status 15 under
`lifecycle-enabled` and `lifecycle-disabled`.

The final crash audit completed at 2026-07-19 20:30:44 +0800 under
`review-final2-crash-audit`. It found no residual Embed Labs/LLDB process, new
matching DiagnosticReports file, or matching
ReportCrash/CrashReporter/diagnosticd event after 2026-07-19 20:07:00 +0800.
Every final success-path test and lifecycle executable used fresh
HOME/settings, cleared inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar bypass. No visible main window or system crash dialog was created. Manual
desktop inspection was intentionally not run because this issue changes
text/state only and all executable acceptance remained offscreen.

This issue changes only the existing private `ethercatpage.cpp`, Workbench
test declaration/implementation, and these four documents. It adds no source
file, dependency, public API, Provider, persistence field, Project command,
custom model role, production thread or timer, controller/network transport,
online state, or hardware behavior. Configured-slave editing, persistence,
and Undo/Redo are unchanged. No CMake or qbs description changed, so qbs was
not run. The unrelated `WITH_TESTS=ON` all-target build was not rerun; the
known EasyBoard test include blocker remains outside this private Workbench
issue. No upstream Core, ProjectExplorer, or application-bootstrap path
changed. The Workbench path count remains 44 and the direct upstream Core
patch count remains five.

## CoE modal repository-refresh lifecycle

`ISSUE-WB-COE-MODAL-REFRESH-001`, based on local commit
`7395c4ceefe78896496bf3b83d757f5096550e03`, closes a stale-result window in
the existing CoE Online page. A same-identity ESI re-import can publish
`devicesChanged` and rebuild the selected Device or configured-slave page
while either Advanced Settings or Add to Startup is awaiting a response. The
old blocking dialog path could then apply controls from the pre-refresh
Advanced dialog, or read a model index that the refresh had already reset.

Both dialogs now use Qt's asynchronous `open()` lifecycle and are owned by the
private CoE page. Each page context assignment advances a page-local
generation. A same-context repository refresh leaves the dialog available for
the user's response, but a response captured before that refresh is discarded:
Advanced settings cannot restore stale source/range/filter state, and a stale
Yes response cannot add a Startup request. If selection changes remove the
page while a dialog is open, parent ownership deletes both page and dialog;
the page-bound completion connection cannot run afterward.

The valid no-refresh behavior remains unchanged. Accepted Advanced settings
still update the existing local Mock/offline source and filters. Accepted Add
to Startup still uses the checked Project path, but the object address, raw
value, data type, project ID, and slave ID are captured before confirmation.
After a current-context Yes response, the Project and slave are looked up
again before the new request is submitted. This is a UI-lifecycle guard, not a
Provider revision, repository transaction, controller operation, or online
SDO path.

Qt documents that `QDialog::exec()` creates a nested event loop and recommends
asynchronous `open()` because parent deletion during the dialog can otherwise
cause dangerous bugs:
<https://doc.qt.io/qt-6/qdialog.html#exec>. Qt Creator 20.0 uses the same
heap-owned, delete-on-close, asynchronous pattern for its font settings dialog:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L532-L539>.
Beckhoff's CoE Online Advanced Settings describe only the comparable object
source, range, and filter controls:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446522251.html>.

Failure-first qualification compiled both new regressions while production
remained at SHA-256
`e2d896f08b4872e0385d16f88837da185466e7ebde33b8074fb3e198b73af559` /
`885e21c1ebd70e60a4ce5cf8c1f73ec7836ee1bdfbb7c98cb7f5006203c1be78`.
Initialization and cleanup passed. Advanced acceptance after a real second
same-identity ESI import incorrectly restored Offline state, and Add
confirmation after the same producer path changed the Project snapshot; both
targeted runs exited with status 1 under `failure-first-final`.

Final production SHA-256 values are
`6b8738ff5e7f245ddb2ad746aa2058b8452343471bd86e85cbeee29d66be5478` /
`542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952`;
final test values are
`4e99c8e98709136ccc201bbda4f27259a114ccfe6efbcc156bcd95b8bcc2623e` /
`0cce0d1c7555b037c552a34daf9ff7a248fee30b40d6fcd0efd7fb73917019a6`.
Advanced and Add focused runs at normal and 2x scale each passed 3 events.
The existing CoE workflow passed 3 events at both scales. Complete Workbench
runs passed 62 events at each scale, and the six isolated suites passed 113
events: Core 17, Project 12, Devices 8, Workbench 62, Scan 7, and Diagnostics
7. Every final success-path test target exited with status 0 under
`/private/tmp/embed-labs-coe-advanced-refresh.20260719`.

Each complete Workbench log retains the known pre-existing ProjectExplorer
TaskHub soft assertion in `testInvalidProjectPresentationAndLifecycle`. It
does not occur in either new focused test, fail a test, or alter target status.
The full `WITH_TESTS=OFF` product build passed and contains exactly the 16
allow-listed plugin dylibs. Its executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
its Workbench plugin SHA-256 is
`d029befb03a59b8e2c6ef17601b379a988cc87d3607889a31e0d04008545051a`.

Enabled startup observed PID 3445 for 37 consecutive one-second samples, with
the Workbench plugin loaded for 34 samples after three startup samples.
Explicitly disabled startup observed PID 3439 for the same 37 samples with
`-noload EtherCATWorkbench` and zero loaded-plugin samples. Both were ended by
an intentional passed-through SIGTERM with target status 15. Tests and
lifecycle acceptance used fresh HOME/settings, cleared inherited DYLD
variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and only the process-local Touch Bar bypass. No visible main
window was created under this policy. The final crash audit completed at
2026-07-19 21:53:05 +0800 and found no residual Embed Labs/LLDB process, new
matching DiagnosticReports file, or matching crash-service event after
2026-07-19 21:15:56 +0800; this supports the crash-reporter-disabled offscreen
acceptance boundary, not every possible visible desktop launch environment.

This issue changes only the existing private `coeonlinepage.cpp/.h`, Workbench
test declaration/implementation, and these four documents. It adds no source
file, dependency, public API, Provider, persistence field, Project command,
model role, production thread or timer, controller/network transport, online
state, SDO execution, or hardware behavior. No CMake or qbs description
changed, so qbs was not run. The unrelated `WITH_TESTS=ON` all-target build
was not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue. No upstream Core, ProjectExplorer, or
application-bootstrap path changed. The Workbench path count remains 44 and
the direct upstream Core patch count remains five.

## Startup modal Project-refresh lifecycle

`ISSUE-WB-STARTUP-MODAL-REFRESH-001`, based on local commit
`065dfbeac038cb4071336492449292879c69b8ee`, closes a stale-result and page
ownership window in the existing configured-slave Startup page. A Project
change can refresh that page while New, Edit, or Delete is awaiting a user
response. The old blocking path resumed against the rebuilt table and mutable
page selection, so an Edit opened for request A could overwrite refreshed
request B, and a Delete confirmation for A could delete the row selected after
the refresh. Clearing the Workbench selection could also destroy the parent
page while its stack dialog remained inside a nested event loop.

New, Edit, and Delete now use page-owned, delete-on-close dialogs opened with
Qt's asynchronous `open()` lifecycle. Every `setContext()` advances a private
generation, including same-node Project refreshes. Completion captures only
value identifiers and rejects a response from an older generation. A current
completion re-queries the open Project and configured slave, then merges by
the stable Startup request ID; no `QModelIndex`, table row, request pointer, or
configuration reference crosses the asynchronous boundary. Edit preserves the
current request ID and order. Delete recomputes the ordered successor or
predecessor selection from the current configuration, removes the captured
request only, and renumbers the remaining order without reordering the stored
list.

The page's existing ESI-default behavior remains intact. When defaults are
being previewed for a configured slave but are not yet persisted, an accepted
current dialog still uses that effective page configuration, so New/Edit/Delete
continues to store the displayed defaults through the existing validated
Project path. Current no-refresh New/Edit/Delete operations, validation,
selection restoration, persistence, and Undo/Redo remain unchanged. A stale
response is discarded; it does not create a second Project command or alter
the refreshed snapshot.

Qt documents that `QDialog::exec()` creates a nested event loop and recommends
asynchronous `open()` because deleting a dialog's parent during execution can
cause dangerous bugs:
<https://doc.qt.io/qt-6/qdialog.html#exec>. Qt's static `QMessageBox`
documentation carries the corresponding parent-lifetime warning:
<https://doc.qt.io/qt-6/qmessagebox.html#question>. Qt Creator 20.0 uses the
same heap-owned, delete-on-close pattern for a settings dialog:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L532-L539>.
Beckhoff's Startup page supplies only the comparable ordered mailbox-request,
New/Edit/Delete, and fixed-request vocabulary; Embed Labs still performs no
SDO transfer:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

Failure-first qualification compiled the new regression while production
remained at SHA-256
`4422c80aac2d1d61f6604bb3635ac18ea3d070bcfcedfdf0a01ad21ddc9d876c` /
`a54d0c254dd0460d13e13624cf3e7bc796e17eb09ff3019104081f909279a691`
and git blobs `9a67969dfacba88fe429470b3822330c70dde817` /
`2df41415e73ee3506a9cbff1a6b6fa08e59e7746`. Initialization and cleanup
passed, but accepting the old Edit after a real
`ProjectService::setStartupConfiguration()` refresh produced
`Stale dialog value` instead of `Project refresh wins`; the target exited with
status 1 under `/private/tmp/embed-labs-startup-failure-first.GAIxiz`.

Final production SHA-256 values are
`2cf3ebe16293d99758f0501e89cc83def3b46861d15535f11301aa82b2e653d8` /
`a743a90d1dc6e917b1fbc61e474b134b8cd9ca04574373e40a61213c3f76825c`;
final test values are
`69aaa4d79313a2e9b226a1f86b3752d2d60319d1252a2fadddb4f773a390ac8a` /
`669dbf8a72ee85492f4ed543eeab80d4ce65ef34f938258ac1a0936c4731bd10`.
The focused refresh/lifecycle regression passed three events at normal and 2x
scale. Complete Workbench runs passed 63 events at each scale. The six isolated
suites passed 114 events: Core 17, Project 12, Devices 8, Workbench 63, Scan 7,
and Diagnostics 7. Both complete Workbench logs retain the known pre-existing
ProjectExplorer TaskHub soft assertion in the invalid-project test; it does not
occur in the focused regression, fail a test, or change target status 0.

The full `WITH_TESTS=OFF` product build passed and contains exactly the 16
allow-listed plugin dylibs. Its executable SHA-256 remains
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
its Workbench plugin SHA-256 is
`eca1e11f0ca98aa5675b1ee9340f0fdbdaa1d82b204ab0e93cb87751f724cbe1`.
Enabled startup observed PID 28144 for 37 consecutive samples and loaded the
Workbench plugin in all 37. Explicitly disabled startup observed PID 30196 for
37 samples with `-noload EtherCATWorkbench` and zero loaded-plugin samples.
Both were ended by an intentional passed-through SIGTERM with target status
15.

All current success-path tests and lifecycle runs used fresh HOME/settings,
cleared inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar bypass. No visible main window or system crash dialog was created. The
2026-07-19 22:47:52 +0800 crash audit found no residual Embed Labs/LLDB
process, new matching DiagnosticReports file, or matching
ReportCrash/CrashReporter/diagnosticd event after 2026-07-19 22:15:00 +0800.
This evidence supports the bounded offscreen, crash-reporter-disabled
acceptance environment; it does not claim every possible visible desktop
launch can never report an unrelated crash.

This issue changes only the private `startuppage.cpp/.h`, Workbench test
declaration/implementation, and these four documents. It adds no source file,
dependency, public API, Provider, persistence field, Project command, custom
model role, production thread or timer, controller/network transport, online
state, SDO execution, or hardware behavior. No CMake or qbs description
changed, so qbs was not run. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue. No upstream Core, ProjectExplorer, or
application-bootstrap path changed. The Workbench path count remains 44 and
the direct upstream Core patch count remains five.

## Configured-slave tree physical order

`ISSUE-WB-TREE-PHYSICAL-ORDER-001`, based on local commit
`a9525adcf39b11704d04e2b65da3488897c43f56`, aligns the master subtree with
the offline EtherCAT topology already owned by `ProjectService`. Configured
slave rows now follow `OfflineSlaveConfiguration::position` instead of their
case-insensitive display names. Renaming a slave therefore does not move its
row, while Move Up and Move Down immediately produce the same order in the
Project snapshot, source tree, and filtered navigation proxy.

The physical comparator is enabled only when the complete sibling group
consists of Slave nodes that all map to offline slave configurations. A group
with an unmapped Slave preserves the original case-insensitive name ordering.
Equal physical positions use case-insensitive name and then stable `NodeId` as
deterministic tie-breakers. Choosing one complete relation for the whole
sibling group preserves strict weak ordering; the implementation never mixes
physical and name precedence pair by pair.

`ProjectService` remains the sole owner of positions, validation, persistence,
Undo, and Redo. The private Workbench model consumes immutable Project
snapshots and performs no mutation. Stable node IDs continue to restore the
current selection across model reset and proxy filtering. No new model role,
public API, or cross-plugin ownership contract is introduced.

Beckhoff describes EtherCAT Auto Increment addresses as a representation of
physical ring position, with the first, second, and third devices assigned
`0x0000`, `0xffff`, and `0xfffe` respectively:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html>.
Its configuration workflow also presents boxes beneath the selected EtherCAT
device:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
Qt's proxy model keeps the source model as the underlying ordering contract:
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html>. Qt Creator 20.0 likewise
compares semantic priority before display name in its Project tree:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L88-L103>.

Failure-first qualification kept production at SHA-256
`31e486b2b074aa4e6c6fa1c629fc8ea55972f9b29ee82c74175d58711eb7945a`
and git blob `3a901d3c1c2a6a6fa005e66d8f592894050fb6f3`. Initialization and cleanup
passed, but the alphabetically earlier position-1 slave appeared before the
position-0 slave, so the focused target exited with status 1 under
`/private/tmp/embed-labs-tree-physical-order.tIKzsO/failure-first`.

Final source SHA-256 values are
`3211b55cffb8762f82f22d78e340ce517595b2e39257e038cdd2e9274a0947aa`,
`e86aa10e468855b00ca5952748260d0a1a2614c3fd9729922cf394d0c9a65e36`,
and `083670f9dc98e96a2e05f6ededcd842cb7adb1f19f56c4bd3c66783c915e4b15`
for the tree model, Workbench test implementation, and test declaration. Their
git blobs are `aa23ee6dfe4e8b4cc6c7b996250592521f3b5c97`,
`5647490e020596fe38cabc413f169143a275c0bb`, and
`996d452da5784cab2d282ca49810f32b4936bb72`.

The focused physical-order regression and the existing real topology-editing
workflow each passed three events at normal and 2x scale. Complete Workbench
runs passed 64 events at each scale. The six isolated suites passed 115
events: Core 17, Project 12, Devices 8, Workbench 64, Scan 7, and Diagnostics
7. The full `WITH_TESTS=OFF` product build passed and contains exactly the 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`2e182d12b357c467d75b75a406b0cf28344a5eac03e49c514ab5934c272937cd`
and `b046bbf6d9e4a649d6ada08afc7a722344582adf52c127f27700b952d9b7b697`.

Enabled startup observed PID 75471 alive for 37 consecutive samples with the
Workbench plugin loaded in every sample. Explicitly disabled startup observed
PID 78033 alive for 37 samples and a final independent `vmmap` observation
with the plugin absent. Both were ended by an intentional passed-through
SIGTERM with target status 15. Every executable run used fresh HOME/settings,
cleared inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar bypass. No visible main window or crash dialog was created. The
2026-07-22 08:48:12 +0800 audit found no residual Embed Labs/LLDB process, new
matching DiagnosticReports file, or matching crash-service event after
08:44 +0800.

This issue changes only the private `workbenchtreemodel.cpp`, Workbench test
declaration/implementation, and these four documents. It adds no source file,
dependency, public API, Provider, persistence field, Project command, custom
model role, production thread or timer, controller/network transport, online
state, scan, SDO execution, or hardware behavior. No CMake or qbs description
changed, so qbs was not run. The unrelated `WITH_TESTS=ON` all-target build was
not rerun; the known EasyBoard test include blocker remains outside this
private Workbench issue. No upstream Core, ProjectExplorer, or
application-bootstrap path changed. The Workbench path count remains 44 and
the direct upstream Core patch count remains five. All behavior and evidence
in this qualification remain local/offline Mock behavior.

## Topology dialog page and context lifecycle

`ISSUE-WB-TOPOLOGY-DIALOG-PAGE-LIFECYCLE-001`, based on local commit
`e814edf65c95ef3c5701f7655c1ff0237cdeac26`, makes the read-only master
Topology dialog asynchronous and page-owned. Opening Topology returns to the
Workbench event loop immediately. Close or Escape destroys the current
dialog and permits an immediate reopen, while a selection change, project
close, page teardown, or EtherCAT page context refresh also closes the old
snapshot. A Project refresh can therefore no longer leave stale topology rows
visible or delete the page from inside a nested modal event loop.

The private `EtherCATPage` keeps only a `QPointer<QDialog>`. The dialog is
allocated on the heap with the page as parent, uses `Qt::WA_DeleteOnClose`,
and is shown with `open()`. A repeat click raises and activates the existing
dialog. Its `finished` handler clears the pointer only when it still identifies
that exact dialog; this preserves an immediately reopened dialog while the
previous instance awaits deferred deletion. There is no result or apply path:
the dialog remains a read-only snapshot with the existing ten columns,
screen-bounded geometry, accessible cell names, configured physical order,
offline labels, and Mock state.

`EtherCATPage::setContext()` conservatively closes an open topology snapshot
on every page refresh, including Project refreshes and repository or index
refreshes. The table is not live-refreshed in place. This deliberately small
invalidation rule keeps ownership and freshness local to the page without
adding a Project or Provider revision contract. Earlier topology-bounds and
cell-accessibility sections described a stack-local or modal lifetime. Those
lifecycle statements are superseded by this section; their geometry and
accessibility qualification remains valid.

Qt documents that `QDialog::exec()` creates a nested event loop and recommends
asynchronous `open()` because deleting the dialog's parent during `exec()` is
dangerous: <https://doc.qt.io/qt-6/qdialog.html#exec>. Qt object trees retain
the page-to-dialog destruction contract:
<https://doc.qt.io/qt-6/objecttrees.html>. Qt Creator 20.0 uses the same
heap-owned, delete-on-close, asynchronous pattern:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L527-L540>.
Beckhoff documents Topology as a page of the selected EtherCAT master and
distinguishes offline configuration from online topology:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1277974411.html>.

Failure-first qualification kept production unchanged at SHA-256
`0f264b16c975d3ae86fddf5d7b979a0b40ab49039a23fd30ea565992460426b3`
and git blob `125159bcec59bb9cc1cf86f880c15a62d405be8e` for
`ethercatpage.cpp`, and SHA-256
`e414a1557a76ce5150e8a5f1e2b62131787f2548118699c9d45e7d3f2827f011`
and git blob `23b20b352bdb2945c4b57474f605e2fa2c1d9f63` for its header.
The unchanged Workbench tests had SHA-256
`e86aa10e468855b00ca5952748260d0a1a2614c3fd9729922cf394d0c9a65e36`
and `083670f9dc98e96a2e05f6ededcd842cb7adb1f19f56c4bd3c66783c915e4b15`,
with git blobs `5647490e020596fe38cabc413f169143a275c0bb` and
`996d452da5784cab2d282ca49810f32b4936bb72`. Initialization and cleanup
passed, but a real same-master Project refresh left `Project Refresh Wins`
absent while the stale row remained visible. The focused target therefore
failed with status 1 at
`/private/tmp/embed-labs-topology-dialog-lifecycle.70Zcr4/failure-first/focused.log`.

Final SHA-256 values for `ethercatpage.cpp`, `ethercatpage.h`, the Workbench
test implementation, and its declaration are respectively
`2ee8353e49b0f4071d2f43ae3085e5af4b5d78b5035146f41957391e8852cc5d`,
`8551cd7031c8084c4dcd2de962adfa03827561d9c6c5bd94c0c87514dda8e6b4`,
`e1fa20d5cc394c7a6999da8d3274a98b6d0633cc67b5c252968e540ce4bdc2cc`,
and `d3771690ff204fc6eafab7a3c7f4231ad461ec6301d288731ae665d86a2670f9`.
Their git blobs are `910465f3a1dde786add222808d502131f5b6bb9e`,
`dd3751d3b552b6bcb51d8f7aa68e0b9d117e6406`,
`1a81136d36cfd362562d5b275f21d2ca0a564fd6`, and
`a7c062a6ab301bb0de0470b2c6bdb0ab8a0296da`.

The final focused lifecycle test passed three events at normal and 2x scale;
the related topology group passed eleven events at each scale. Complete
Workbench runs passed 65 events at each scale. The six isolated suites passed
116 events: Core 17, Project 12, Devices 8, Workbench 65, Scan 7, and
Diagnostics 7. The complete Workbench runs retain the known pre-existing
ProjectExplorer TaskHub soft assertion in the invalid-project path; it did not
occur in the focused lifecycle test and did not fail a test or target.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`10ba46cb43718a7c38170d7c0aaae8030f4f8a1fbb5ceb92f0825a5f0a3ee8d8`
and `5f4fe0b35bb31780000928645fc823af44b7d7993578d88714ef0d07af15e541`.

Enabled product startup observed PID 85263 alive for 37 consecutive samples,
with the Workbench plugin loaded in all 37. Explicitly disabled startup
observed PID 85262 alive for 37 samples with the plugin absent in all 37.
Each was ended by an intentional passed-through SIGTERM with target status
15. Every executable run used fresh HOME/settings, cleared inherited DYLD
variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and only the process-local Touch Bar bypass. No visible main
window was opened. The 2026-07-22 09:27:59 +0800 audit found no residual Embed
Labs/LLDB process, new matching DiagnosticReports file, or matching crash
service event after 09:24 +0800. Evidence is under
`/private/tmp/embed-labs-topology-dialog-lifecycle.70Zcr4`.

This issue changes only the private `ethercatpage.cpp/.h`, Workbench test
declaration/implementation, and these four documents. It adds no source file,
dependency, public API, Core or ProjectExplorer hook, application-bootstrap
change, Provider or ProjectService contract, persistence field, Project
command, model role, production thread or timer, network transport, scan,
online topology, port graph, CRC/state control, SDO execution, or hardware
behavior. No CMake or qbs description changed, so qbs was not run. The
unrelated `WITH_TESTS=ON` all-target build was not rerun; the known EasyBoard
`extensionmanager_test.h` blocker remains outside this private Workbench
issue. All qualification is local/offline Mock evidence, and manual visible
desktop inspection was intentionally not run.

## Offline slave removal project-refresh safety

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-PROJECT-REFRESH-001` is qualified from local
baseline `312e1a9dbacfd621615c87d644b7a46bb5f5a64e`. It closes a destructive
confirmation gap without changing the existing offline deletion command.

The Remove command now captures the complete selected
`OfflineSlaveConfiguration` by value before opening its asynchronous question.
After Yes, the private Workbench controller still re-resolves the current
Project, Master, Slave, and selection by stable ID. Before any mutation it also
requires the current target Slave to equal the captured value exactly. That
comparison covers `id`, `masterId`, `position`, identity, serial number, Alias,
name, device-description ID, Process Data, Startup, DC, and every nested value
inside those structures.

If the same Slave configuration changes while the question is open, stale Yes
returns an explicit error and performs no Project command. The refreshed Slave,
selection, and Undo/Redo availability remain intact, and the user can reopen
the question against the current configuration. A current Yes still removes
the Slave and retains complete Undo/Redo. In the qualified single-Slave case,
selection repairs to the Master; existing multi-Slave behavior selects the
adjacent configured Slave. Escape remains a real no-op and the delete-on-close
question is destroyed safely. Changes to another Project or sibling Slave do
not invalidate the question while the re-resolved target Slave value itself
remains unchanged.

Qt documents asynchronous dialog opening and the nested-loop caution at
<https://doc.qt.io/qt-6/qdialog.html#open> and
<https://doc.qt.io/qt-6/qdialog.html#exec>. QMessageBox question, default, and
Escape behavior is documented at <https://doc.qt.io/qt-6/qmessagebox.html>.
Qt Creator's Project settings pages provide the local host precedent in
`src/plugins/projectexplorer/buildsettingspropertiespage.cpp` and
`runsettingspropertiespage.cpp`; the corresponding official source is at
<https://github.com/qt-creator/qt-creator/tree/v20.0.0/src/plugins/projectexplorer>.
Beckhoff documents that Remove deletes the selected I/O device from the tree
and configuration at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
Those sources do not prescribe this product's additional stale-confirmation
guard.

Failure-first changed only the Workbench test. Production remained at
SHA-256 `5fb5b70a377f57c023bc2c9201d715d5cf6ea8e8639c425b231c68a7f01ce552`
and blob `61c361f0b4c6fd37ca933d47d5852b539118c10a` for
`workbenchcontroller.cpp`, and SHA-256
`d8212f430dc167713504fb2501d1535200fd85d2ff99d8811b59b483963dc693`
and blob `26cb642418687a60608b33183db524d4fc62fa74` for its header. The original
test implementation and declaration were SHA-256
`e1fa20d5cc394c7a6999da8d3274a98b6d0633cc67b5c252968e540ce4bdc2cc`
and `d3771690ff204fc6eafab7a3c7f4231ad461ec6301d288731ae665d86a2670f9`,
with blobs `1a81136d36cfd362562d5b275f21d2ca0a564fd6` and
`a7c062a6ab301bb0de0470b2c6bdb0ab8a0296da`. A real
`ProjectService::setDcConfiguration()` refresh preserved the same Slave ID,
name, position, and selection, but stale Yes deleted it. Initialization and
cleanup passed; the target failed safely with status 1 at the expected size
assertion in
`/private/tmp/embed-labs-remove-project-refresh.ErQwPA/failure-first/semantic2.log`.

Final SHA-256 values for `workbenchcontroller.cpp`, its header, the Workbench
test implementation, and its declaration are respectively
`0e1a3c43132b186240ced01e69558fd847b97d0b97614b1f805dac2a753e0d32`,
`1cd58714e3de3db609886fe0d1dd4b79a66132193c294352c6d9ff69a2e6e9f2`,
`f6727669cf2d44325693853469fa6b5a9fac67fdd09f55e777faebc1293c7700`,
and `b52558c301ddca51c465809597eaad066f68550e9fa889998c4990d7bc500c4e`.
Their git blobs are `2d9af3fe0ede52c5146e4faf5335ef1e716eb134`,
`9805736ecfa23b4597c749270041e9b3bfd03e90`,
`1076daadd6da7063394f2dc924544f3f0da8cb86`, and
`ec075cfc2d14e2e7aa0a1d3b054fa29b7ce419d8`.

The focused test passed three events at normal and 2x scale; the related
removal group passed four events at each scale. Complete Workbench runs passed
66 events at each scale. The six isolated suites passed 117 events: Core 17,
Project 12, Devices 8, Workbench 66, Scan 7, and Diagnostics 7. The complete
Workbench runs retain the known pre-existing ProjectExplorer TaskHub soft
assertion in the invalid-project path; it did not fail a test or target.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`d2690915bbe2e089035deb5fa799386f670ec8050505910cdd26a5cf0d1a2b0c`
and `a0678ed0e865c3e9528756b8b9a9d84e9c0a71d228586d8ecff1fd28c620acf5`.

Enabled product startup observed PID 2618 alive for 37 consecutive samples;
an independent `vmmap` confirmed the Workbench plugin was loaded. Explicitly
disabled startup observed PID 7890 alive for 37 samples with
`-noload EtherCATWorkbench`; an independent `vmmap` confirmed the plugin was
absent. Each process was ended by an intentional passed-through SIGTERM with
target status 15. The disabled run's shared-memory initialization message was
non-fatal. Both used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar bypass. No visible main window was opened.
The 2026-07-22 10:17:55 +0800 audit found no residual Embed Labs/LLDB process,
new matching DiagnosticReports file, or matching crash-service event after
10:12 +0800.

Evidence is under
`/private/tmp/embed-labs-remove-project-refresh.ErQwPA`. This issue changes
only the private controller, its Workbench test declaration/implementation,
and these four documents. It adds no public API, source file, ProjectService
or Provider contract, persistence field, Project command, model role, Core or
ProjectExplorer hook, application-bootstrap change, network transport, scan,
online state, SDO execution, or hardware behavior. No CMake or qbs description
changed, so qbs was not run. The unrelated `WITH_TESTS=ON` all-target build was
not rerun because the known EasyBoard `extensionmanager_test.h` blocker remains
outside this issue. Qualification is local/offline Mock evidence; visible
desktop inspection was intentionally not run so acceptance did not interrupt
desktop use.

## Keyboard context-menu target and anchor

`ISSUE-WB-NAV-KEYBOARD-CONTEXT-TARGET-001` is qualified from local baseline
`38d0d262f972b6bd1f877b6a2306411a1240cbf7`. It fixes one keyboard-only
navigation error. The tree previously converted every context-menu event into
`customContextMenuRequested(QPoint)`, which discarded the event reason. A
keyboard request commonly carries a null local position, so the old handler
looked up viewport `(0, 0)`, changed the current row from the selected Master
to the top Project, changed the stable Selection/Details target, and built the
wrong node-specific menu.

The private navigation widget now observes the original `QContextMenuEvent`
on the tree and its viewport. Mouse requests retain the existing hit-test and
selection behavior. Keyboard and other non-mouse requests keep the current
stable node and anchor the menu to the visible part of its current row. If the
selected row has been scrolled completely out of view, the anchor falls back
to the viewport center. The event is consumed before Qt can emit a duplicate
custom-menu signal; the existing direct signal path remains for current
Workbench callers and tests.

Qt documents both the keyboard reason and the possibility of a null position
for non-mouse context events:
<https://doc.qt.io/qt-6/qcontextmenuevent.html>. The viewport event boundary
is documented at <https://doc.qt.io/qt-6/qabstractscrollarea.html>. Beckhoff
documents Shift+F10 as the context menu for the selected object and describes
selected-object I/O actions at
<https://infosys.beckhoff.com/content/1033/tcplccontrol/925416331.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
The local implementation follows those interaction semantics without copying
branding, assets, engineering formats, or controller communication.

Failure-first changed only the Workbench test. Production
`workbenchnavigation.cpp/.h` remained at SHA-256
`f6179d068288eec4f88a8cc83f56b58bb2dbd12373540703aa86266048fad761`
and
`42b40a81380b510430b93bd89822f7f7afc19bbb1b3085b1c007e2ce260489ee`,
with git blobs `ddc7ffe1809768aec322ef9a379dd0495abc8445` and
`3e1c35466c7c816c4386e61ea11d70eebd5c764e`. A real keyboard context event
changed the current index from Master to Project, failed the intended stable
index assertion, and exited with status 1.

The final test sends real keyboard and mouse `QContextMenuEvent` instances.
It checks the tree and `SelectionService` while each menu is still visible,
verifies the Master-only Insert Device action, verifies that a mouse request
on Target changes both targets and removes that action, and verifies the
viewport-center fallback after the selected Master is scrolled out of view.
Normal and `QT_SCALE_FACTOR=2` focused runs each passed 3 events; the related
navigation/menu/lifecycle group passed 12 at each scale; complete Workbench
runs passed 71 at each scale. Six isolated plugin suites passed 122 events:
Core 17, Project 12, Devices 8, Workbench 71, Scan 7, and Diagnostics 7. The
complete Workbench run retains the known pre-existing ProjectExplorer TaskHub
soft assertion in its invalid-project path; it did not fail a test or target.

Final SHA-256 values for the navigation implementation, navigation header,
Workbench tests, and test declaration are respectively
`84a9aa508d876906a88ff349ef0bd0406bf60f4d6ba7090380d1b834d5ce4b4c`,
`e874a45f55f16a8bd5f1db9113eaaadfc96c36c6ea690bf99fc0740219e45ffc`,
`888473502f6b22880a813847cab934631d73cfabacf1d46aec2907b4344fc9ce`,
and `0f2472d64258d1ff34095274501853c48e66cbdd221193d477e328cb9278fbc7`.
Their git blobs are `8a2c6fc675520a9216815c72f0df6f753d5c5a46`,
`f0ee389be08c1023b25e086b761bc8983e879fbd`,
`2b52792fcd9eea4c64b1838dc9c7f64bf313a93a`, and
`9798981c041d877ce5dc81539df140317ef02a43`.

The full `WITH_TESTS=OFF` product build passed with exactly 16 plugin dylibs.
The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin values are
`5d96db48757190e5ba47df76a9ffffcbdaf237701d7adeec5c9531ba8c1ec563`
and `25595a0f9a338c62de48565a2bd2877fc701ec64b8e2ad415a1c544c758a6ae4`.
Enabled PID 35107 stayed alive for 37 samples with Workbench mapped in all 37.
Explicitly disabled PID 37010 stayed alive for 37 samples with Workbench
absent in all 37. LLDB passed SIGTERM without stopping or notifying, and both
targets recorded status 15. The disabled run's existing shared-memory message
was non-fatal. From 2026-07-22 17:19:38 to 17:24:22 +0800 there was no
residual qualification process, matching new DiagnosticReports file, or
matching crash-service event.

All executable checks used fresh HOME/settings, cleared inherited DYLD
variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and only the process-local Touch Bar bypass. No visible main
window or system crash dialog was opened. Evidence is under
`/private/tmp/embed-labs-wb-nav-keyboard-context-target-001.Du2VdU`. This
issue changes only private navigation implementation/header, Workbench test
implementation/declaration, and these four documents. It adds no public API,
source file, dependency, Project or Provider contract, persistence, model
role, Core/ProjectExplorer/app hook, thread, timer, network, ADS, scan, online,
SDO, ESC/EEPROM, or hardware behavior. No CMake or qbs description changed.
The unrelated `WITH_TESTS=ON` all-target build was not rerun because the known
EasyBoard `extensionmanager_test.h` blocker remains outside this issue.

## Configured Station Alias non-conflicting refresh draft continuity

`ISSUE-WB-ETHERCAT-ALIAS-NONCONFLICTING-REFRESH-DRAFT-001` is based on
local commit `8c374cb68b2ea5844c6ca5a51cf31ed8b2aa14a1`. A configured slave's
Alias editor uses `keyboardTracking(false)` and commits on
`editingFinished`. Before this correction, typing `321` over persisted Alias
`3` and receiving a Repository or current-Project refresh reset the editor to
`3` before the user completed the edit.

The private `EtherCATPage` now preserves only the current Alias editor when
the Project ID, node ID, and node kind are unchanged, the page still
represents a configured slave, the editor is modified or focused, and the
freshly read Alias still equals the page's last authoritative baseline. The
preserved state includes text, modified state, focus, cursor, selection, and
the line editor's local Undo/Redo history. Type, SyncManager, and the other
EtherCAT fields continue to refresh from current Repository and Project data.

An explicit Alias commit uses a scoped force-authority reload. An external
Alias command, Project Undo, changed or invalid context, node switch, and
Project close also reload or destroy the editor from authority. ProjectService
and EtherCATProject remain the owners of validation, persistence, modified
state, Project Undo, and Project Redo. This page-local rule is not autosave, a
cross-node draft cache, merge/conflict UI, or a Project revision/CAS protocol.

Qt documents that disabling `QAbstractSpinBox::keyboardTracking` delays value
and text signals until completion, while `editingFinished` is emitted when
editing finishes through focus loss or Enter:
<https://doc.qt.io/qt-6/qabstractspinbox.html#keyboardTracking-prop> and
<https://doc.qt.io/qt-6/qabstractspinbox.html#editingFinished>. Beckhoff
documents the EtherCAT tab and Configured Station Alias as a 16-bit node
addressing setting:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html>,
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358008331.html>,
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1356630411.html>, and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1257993099.html>.
This qualification persists only local/offline Project configuration; it does
not claim an ESC/EEPROM write or physical-hardware control.

Failure-first changed only the Workbench test. Production
`ethercatpage.cpp/.h` remained at SHA-256
`2ee8353e49b0f4071d2f43ae3085e5af4b5d78b5035146f41957391e8852cc5d`
and `8551cd7031c8084c4dcd2de962adfa03827561d9c6c5bd94c0c87514dda8e6b4`,
with git blobs `910465f3a1dde786add222808d502131f5b6bb9e` and
`dd3751d3b552b6bcb51d8f7aa68e0b9d117e6406`. The real Repository update
left the Project Alias at `3` but changed the draft from expected `321` back
to actual `3`. Init and cleanup passed, the intended assertion failed, and
the target exited with status 1 in `failure-first/test.log`.

The final focused test imports a same-identity ESI update and proves that the
Alias draft and editor state survive while Type and the first SyncManager name
refresh. It proves local editor Undo/Redo, Return commit to `321`, same-Project
rename and Undo with a `456` draft, external Alias `654` authority and Project
Undo back to `321`, and draft destruction across a node switch and Project
close. Focused normal and 2x runs each passed three events. Related normal and
2x runs each passed ten events. Complete Workbench normal and 2x runs each
passed 70 events. Six isolated plugin suites passed 121 events: Core 17,
Project 12, Devices 8, Workbench 70, Scan 7, and Diagnostics 7. All recorded
zero failures and target status 0. The complete Workbench run retains the
known pre-existing ProjectExplorer TaskHub category soft assertion in the
invalid-project path; it did not fail a test or target.

Final SHA-256 values for the page implementation, page header, Workbench test
implementation, and test declaration are respectively
`a6ff4c3a5da7a96061ccf7fd42f7426533a46a3628f43047d5fc93e27ab1fba2`,
`e0517e191523303a45dc7574dbe95f701ddf40c27c18fff27b4e87d11d13d18a`,
`173639291cee4a476f9878b49aff177943a14f8536b68e05ff26a408de3663e0`,
and `0fd6cf8f6dd36e4c5a0fbd713726e4e2075cd2f5231654620dee18e3c683104c`.
Their git blobs are `cfe4498bd3368d61e906149c9019e96a0e4a05ef`,
`9f9e1bfd2957b5667e7d0eb64dc762684c5c5903`,
`204287c739f8c5fca0de6ea1dd4d299006b9eb66`, and
`a5d954d40310141922a3513709700749ce3e2f8d`.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16 plugin
dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`09ce71bb36af84f6a9b5f18681257c06b5e8bfcc88ec08d60d3fa5af61d64cbb`
and `0ea23b46bf78722d6147c0bfc9753313d7412152df814a386e9fc115df21dac3`.

Enabled product startup observed PID 37850 alive for 37 samples with the
Workbench plugin mapped in all 37. Explicitly disabled startup observed PID
41198 alive for 37 samples with `-noload EtherCATWorkbench` and Workbench
absent in all 37. LLDB passed SIGTERM without stop or notification, and both
targets recorded status 15. The disabled run emitted the existing non-fatal
shared-memory initialization message and remained alive for every sample.
Every executable run used fresh HOME/settings, cleared inherited DYLD
variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and only the process-local Touch Bar bypass. No visible main
window was opened. The 2026-07-22 16:34:14 to 16:38:48 +0800 audit found no
residual product/debugger process, new matching DiagnosticReports file, or
matching crash-service event.

Authoritative final evidence is under
`/private/tmp/embed-labs-wb-alias-draft-refresh-001.F7RkQj/final`; the sibling
`failure-first/` directory is the red proof. Earlier `green/` and
`qualification1/` directories are intermediate audit history, while
`final/invalid-*` entries record two intentionally rejected LLDB-driver
attempts that exited before tests ran. This issue changes only private
`ethercatpage.cpp/.h`, the Workbench test declaration/implementation, and
these four documents. It adds no duplicate-Alias policy, public API, source
file, dependency, Project format, persistence field, Project command,
Provider or ProjectService contract, custom model role, production thread or
timer, Core or ProjectExplorer hook, application-bootstrap change, Modules or
Channels behavior, network transport, ADS, scan, online state, fixed address,
Identification, port graph, SDO execution, ESC/EEPROM write, or hardware
behavior. No CMake or qbs description changed, so qbs was not run. The
unrelated `WITH_TESTS=ON` all-target build was not rerun because the known
EasyBoard `extensionmanager_test.h` blocker remains outside this private
Workbench issue. Qualification is local/offline Mock evidence; visible
desktop inspection was intentionally not run so acceptance did not interrupt
desktop use.

## Process Data same-context selection continuity

`ISSUE-WB-PROCESS-DATA-SAME-CONTEXT-SELECTION-001` is qualified from local
baseline `f4da0b18cc48077374ba2528091cdb3aeaad99f5`. It fixes one
user-visible Process Data correctness problem. A Device Repository rebuild or
a change to the current Project previously refreshed the existing page and
silently moved its current Sync Manager and PDO back to the first rows. A user
reviewing a later TxPDO could consequently see RxPDO content and direct the
next edit at the wrong mapping.

`ProcessDataPage` now retains the selected Sync Manager and PDO stable IDs
only when Project ID, node ID, and node kind are unchanged. Its existing model
rebuild resolves those IDs against fresh authoritative data. If either ID no
longer exists, the established deterministic first-row fallback remains in
force. If the selected PDO still exists under a different Sync Manager, its
fresh owning Sync Manager replaces the stale owner selection. Switching
Project, node, or node kind clears the old IDs before the existing derived-node
focusing logic runs. Derived-node focusing now runs only for that genuine
context change, so a manual Sync Manager/PDO choice within the same derived
context also survives a refresh. Repository contents, Project commands,
validation, Process Image layout, Undo/Redo, and editability are unchanged.

Qt documents that a view's current item drives keyboard navigation and focus
indication, and that model reset does not emit the current-item change signals:
<https://doc.qt.io/qt-6/qitemselectionmodel.html>. Beckhoff's Process Data tab
documents that the selected Sync Manager controls the PDO Assignment shown
below it:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10834607243.html>.
The implementation therefore restores product-owned stable identity rather
than retaining a transient `QModelIndex` or row number.

Failure-first changed only `ethercatworkbenchtests.cpp`. Production
`processdatapage.cpp` stayed at SHA-256
`1eb40802141f55cfc4125f160e410300cb174e0d5d098a307d3ad863904cf80c`
and git blob `94832604f9589ee2acdf8b5133ce9c6c91bf328a`. A real repository
`rebuildIndex()` then replaced the selected second Sync Manager with the first
one, failed the stable-ID assertion, and exited the target with status 1.

The final focused workflow passed three events at normal and 2x scale. It
proves stable Sync Manager/PDO and Statusword content across a real repository
rebuild, a same-Project rename, and Undo. It also proves first-row fallback
when the selected mapping disappears, PDO continuity when its owning Sync
Manager changes, restoration of the original owner after Undo, retention of a
resulting valid fallback row, and fresh derived-node focus after a context
switch. After that switch, it manually selects Status in the same derived
RxPDO context and proves a real repository rebuild preserves the selected
stable IDs and Statusword content. The related Process Data and Details-refresh
group passed eight events at each scale.
Complete Workbench runs passed 69 events at each scale. Six isolated suites
passed 120 events: Core 17, Project 12, Devices 8, Workbench 69, Scan 7, and
Diagnostics 7. The authoritative Diagnostics result is its isolated rerun; an
earlier parallel attempt recorded all seven pass events but did not terminate
and is superseded. The complete Workbench run retains the known pre-existing
ProjectExplorer TaskHub soft assertion in the invalid-project path; it did not
fail a test or target.

Final SHA-256 values for the page implementation, Workbench tests, and test
declaration are respectively
`fdefc48d56337bb156feac5152cea699d34872337d8b831aaf3739700f409d0e`,
`4460ba1cd1f38bb5c351f683790e5a09d60d6d84442a27f6b07c9c460cd955d3`,
and `9eca5d0743f50a179e02ff9acb11aa42267d87f3c6e67918c8b83f435318f20a`.
Their git blobs are `4812288c373b047ff36174863b30daa986471c41`,
`04461d6d46f43b3a412466a02a433e2eca9779ec`, and
`25cbc12231cded1ee57165f66973b025afe7befb`.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`0a711961fc9b841e142dbea24bcf62f8320a027956aa5797be03a55c234411e1`
and `bfcc8b3e460f6578a5e3f58d56a57588b7c2fda827f5b85f1708a87b75f644c8`.

Enabled product startup observed PID 71981 alive for 37 consecutive samples,
with Workbench mapped in all 37. Explicitly disabled startup observed PID
74910 alive for 37 samples, with Workbench absent in all 37. LLDB passed
SIGTERM directly without stopping or notifying; both targets recorded status
15. The disabled run emitted the known non-fatal shared-memory initialization
message and nevertheless stayed alive for all 37 samples. Every executable
used fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, a
disabled crash reporter, `-no-crashcheck`, and the process-local Touch Bar
bypass. The 2026-07-22 14:35:44 to 14:42:08 +0800 audit found no residual
product/debugger process, new matching DiagnosticReports file, or matching
crash-service event. No visible main window or system crash dialog was opened.

Authoritative final evidence is under
`/private/tmp/embed-labs-wb-process-data-selection-refresh-001.jPkuu0/final2`;
the two failure-first proofs are under the sibling `failure-first/` and
`derived-failure/` directories. The sibling `final/`, `green/`, `product/`,
root-level product/hash files, and the non-isolated
`final2/six-ethercatdiagnostics/` attempt are superseded intermediate audit
history, not final qualification.
This issue changes only private
`processdatapage.cpp`, the existing Workbench test implementation, and these
four documents. It adds no public API, source file, dependency, model role,
Provider or ProjectService contract, Project format, persistence field,
Project command, Core or ProjectExplorer hook, application-bootstrap change,
production thread or timer, network transport, scan, online state, SDO
execution, or hardware behavior. No CMake or qbs description changed, so qbs
was not run. The unrelated `WITH_TESTS=ON` all-target build was not rerun
because the known EasyBoard `extensionmanager_test.h` blocker remains outside
this private Workbench issue. Qualification is local/offline Mock evidence;
desktop inspection stayed offscreen so acceptance did not interrupt desktop
use.

## Offline-slave removal confirmation invalidation

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-CONFIRM-INVALIDATION-001`, based on local
commit `532517066a612c4f5220484cadb830d0c07256c0`, removes a stale modal
interaction from the configured-slave removal workflow. The question still
uses the captured Project, Master, slave, and complete offline-slave
configuration as its authority. While it is visible, a stable Selection
change, target-Project close, target-slave removal, or any change to that
captured slave now rejects and deletes the question immediately. The user no
longer has to answer a destructive question whose subject is no longer
current.

The private action listens only for the existing Selection and Project
lifecycle signals and keeps the dialog itself as each connection context. A
Project change is relevant only when it is for the captured Project and a
fresh controller lookup no longer returns the exact captured slave
configuration. Changes to another Project, or sibling-only changes that leave
the captured candidate unchanged, therefore leave the current question open.
A current `Yes`, `No`, and Escape retain the existing behavior; the
controller's complete validation after `Yes` remains as defense in depth, and
the Project-owned mutation, modified state, persistence, nearest-node
selection repair, Undo, and Redo paths are unchanged.

Failure-first changed only the Workbench test. With production
`ethercatworkbenchplugin.cpp` unchanged at SHA-256
`03adb384bfd2e0e31606d9dd8294629718b1061c192764584db63198b25480b4`,
changing the captured slave's DC configuration left the question alive, so
the required null-guard assertion failed and the target exited with status 1.
The final focused normal and 2x runs each passed three events; the related
topology/lifecycle runs each passed four. Complete Workbench normal and 2x
runs each passed 69 events. The six isolated suites passed 120 events: Core
17, Project 12, Devices 8, Workbench 69, Scan 7, and Diagnostics 7. The full
Workbench paths retain the known pre-existing ProjectExplorer TaskHub
category soft assertion in the invalid-project fixture; it did not fail a
test or target.

The complete `WITH_TESTS=OFF` product build passed with exactly the 16
allow-listed plugin dylibs. The product executable, product Workbench plugin,
and test Workbench plugin SHA-256 values are respectively
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`3a0d246f1fb331d7691311dd940c3046e87a151a2901b3b876285c9b2a77b221`,
and `9c95c1793d96fbc02aecae643a074d8870b5b05bb8e7c1e7ec36931d365b423f`.
Enabled PID 18133 remained alive for 37 samples with Workbench mapped in all
37; explicitly disabled PID 21782 remained alive for 37 samples with
Workbench absent in all 37. Passed-through SIGTERM produced target status 15
for both. The 2026-07-22 13:25:01 to 13:42:51 +0800 audit found no residual
product/LLDB process, new matching DiagnosticReports file, or matching crash
service event. All final passing qualification and product lifecycle runs
were offscreen with fresh HOME/settings, cleared inherited DYLD variables,
crash reporting disabled, and the process-local Touch Bar bypass, so no
visible main window or system crash dialog interrupted desktop use.
The exact outer launcher environment and enabled/disabled target arguments
are retained in `product/lifecycle-command-manifest.txt` under the evidence
root.

Qt documents asynchronous `open()`, `finished`, `reject()`, and
delete-on-close dialog ownership at <https://doc.qt.io/qt-6/qdialog.html>,
the standard question surface at <https://doc.qt.io/qt-6/qmessagebox.html>,
and context-owned signal connections at
<https://doc.qt.io/qt-6/qobject.html#connect>. Beckhoff documents that Remove
deletes the selected I/O device from the tree and configuration, but does not
define this private stale-question policy:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.

Evidence is under
`/private/tmp/embed-labs-wb-remove-confirm-invalidation-001`; `final2/` is the
authoritative final test set, while earlier development-iteration logs are
retained only for audit history. The issue changes only private Workbench
action setup, Workbench test
declaration/implementation, and these four documents. It adds no public API,
source file, dependency, model role, Provider or ProjectService contract,
Project format, persistence field, Project command, Core or ProjectExplorer
hook, application-bootstrap change, production thread or timer, network,
scan, online, SDO, or hardware behavior. No CMake or qbs description changed,
so qbs was not run. Qualification remains truthful local/offline Mock
evidence.

## Add New Item asynchronous dialog lifecycle

`ISSUE-WB-INSERT-DIALOG-ASYNC-LIFECYCLE-001` is qualified from local baseline
`617505a08cb2055d6042a2580189182ffd39658a`. It removes the nested modal event
loop from the selected-master `Add New Item...` workflow without changing the
ESI selector's content or the existing Project mutation.

The mode widget now owns one heap-allocated `EsiDeviceSelectionDialog` through
a guarded pointer. The dialog uses `Qt::WA_DeleteOnClose` and `open()`, so the
action returns before modal events are processed. A repeated action raises the
current selector instead of creating a second window. Reject or accept clears
the retained identity before deferred deletion, and mode teardown can delete
its child without a stack object or nested event loop remaining active.

The existing target guards remain authoritative. Closing the target Project,
switching the active Project, invalidating that Project, or removing the target
Master rejects the selector. Accepted selection still enters
`WorkbenchController::addDeviceToMaster()`, which revalidates the stable Master
ID and delegates the checked Project command to `ProjectService`. The guarded
controller pointer makes a late completion a safe no-op during plugin
shutdown.

Qt documents `open()` and warns that `exec()` creates a discouraged nested
event loop whose parent deletion can cause dangerous bugs:
<https://doc.qt.io/qt-6/qdialog.html#open> and
<https://doc.qt.io/qt-6/qdialog.html#exec>. Qt Creator 20.0 uses the same
heap-owned, delete-on-close, asynchronous pattern in
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L527-L540>.
Beckhoff documents the offline Add New Item and device/revision selection
workflow at
<https://infosys.beckhoff.com/content/1033/ps2001-4810-1001/10832046859.html>
and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>;
those sources do not prescribe Qt dialog ownership.

Failure-first changed only the Workbench test. Production remained at
SHA-256 `24b27a931b9b66a70b0d777f847ce142db0a7d2b2871e4e1d75d13b6b77210db`
and git blob `ef98dbeebe74ae26bb8dfca23725cac8e81e3df9` for
`workbenchmode.cpp`. The original Workbench test implementation and
declaration were SHA-256
`f6727669cf2d44325693853469fa6b5a9fac67fdd09f55e777faebc1293c7700`
and `b52558c301ddca51c465809597eaad066f68550e9fa889998c4990d7bc500c4e`,
with git blobs `1076daadd6da7063394f2dc924544f3f0da8cb86` and
`ec075cfc2d14e2e7aa0a1d3b054fa29b7ce419d8`. With a real Workbench mode,
Project, Master selection, navigation context, and global action, the old
`exec()` implementation processed the observer before the action returned.
Initialization and cleanup passed; the target failed safely with status 1 at
the intended return-order assertion in
`/private/tmp/embed-labs-insert-dialog-async.aVAVS1/failure-first-semantic/test.log`.

Final SHA-256 values for `workbenchmode.cpp`, the Workbench test
implementation, and its declaration are respectively
`f7c7dec383d780c6ff4808effaac7965769b12887b431b876bec16bcfcf59b82`,
`fa8b490eef12c856ef2a28227edbbded7a2b3116a5ff118292fd706dbcb28fba`,
and `d1b1edff100e48f0a2954e16371aa5b493edbfdfc690a59cb4824297e0a89b56`.
Their git blobs are `43bfa50de23da414ca2f5a64490157c21fc855a7`,
`95a868337be859661733bf296f151d1c915ae8c8`, and
`da16b5b302abee755cbaf232dcbdca39c00d74ff`.

The focused lifecycle test passed three events at normal and 2x scale. The
related insertion, cancellation, four target-invalidation rows, and
single-instance lifecycle group passed eight events at each scale. Complete
Workbench runs passed 67 events at each scale. The six isolated suites passed
118 events: Core 17, Project 12, Devices 8, Workbench 67, Scan 7, and
Diagnostics 7. The complete Workbench runs retain the known pre-existing
ProjectExplorer TaskHub soft assertion in the invalid-project path; it is
absent from the focused test and did not fail a test or target.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`f07f0992512796d39f9326afe973d1098698a8be05cb0b376724765ed24a8742`
and `90f12f71951615032519130801d54d37c04390ec104b7efa7e0cc39b30001b21`.

Enabled product startup observed PID 94229 alive for 37 consecutive samples;
an independent `vmmap` confirmed the Workbench plugin was loaded. Explicitly
disabled startup observed PID 98352 alive for 37 samples with
`-noload EtherCATWorkbench`; an independent `vmmap` confirmed the plugin was
absent. Each process was ended by an intentional passed-through SIGTERM with
target status 15. The disabled run's shared-memory initialization message was
non-fatal. Both used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar bypass. No visible main window was opened.
The 2026-07-22 10:58:33 +0800 audit found no residual Embed Labs/LLDB process,
new matching DiagnosticReports file, or matching crash-service event after
10:52:30 +0800.

Evidence is under
`/private/tmp/embed-labs-insert-dialog-async.aVAVS1`. This issue changes only
the private `workbenchmode.cpp`, Workbench test declaration/implementation,
and these four documents. It adds no public API, source file, dependency,
ProjectService or Provider contract, persistence field, Project command,
model role, Core or ProjectExplorer hook, application-bootstrap change,
production thread or timer, network transport, scan, online state, SDO
execution, or hardware behavior. No CMake or qbs description changed, so qbs
was not run. The unrelated `WITH_TESTS=ON` all-target build was not rerun
because the known EasyBoard `extensionmanager_test.h` blocker remains outside
this private Workbench issue. Qualification is local/offline Mock evidence;
visible desktop inspection was intentionally not run so acceptance did not
interrupt desktop use.

## General non-conflicting refresh draft continuity

`ISSUE-WB-GENERAL-NONCONFLICTING-REFRESH-DRAFT-001` is qualified from local
baseline `2fe30622dd971c9c3afd91e78e5addb2fa988340`. It fixes one user-visible
Workbench form problem: a Device Repository reset, change, or indexing-state
signal refreshed every Details page, and General then hid, cleared, and
repopulated its name editor even when the selected node and persisted name had
not changed. A Project, Target, Master, or configured Slave draft could
therefore disappear before `editingFinished` submitted it.

General now records only the last authoritative name and its stable
`projectId`, `nodeId`, and node kind. A refresh preserves the current editor
only when all three identities still match, the editor is writable and either
modified or focused, and the newly read persisted name still equals that
baseline. Focus is a conservative guard against disturbing a possible active
input-method preedit that has not yet changed `modified`; the regression
directly proves this clean-focused branch, not platform IME composition. In
that case the page skips hiding, clearing, and `setText()` for that one editor
while continuing to refresh every other General field. This also lets a
configured Slave retain its draft while its ESI type, match, source, and other
read-only metadata update.

A real same-field change remains authoritative. Project and structural names
are re-read from `ProjectService`; the configured Slave name is re-read from
the current offline topology. External rename, Undo, Redo, node switch,
invalid or unavailable context, and project close do not restore an old
draft. Each explicit editor commit first clears the local modified flag and
holds a scoped force-authority guard across the existing checked rename call,
its synchronous signals, and the final context reload. An empty rejected name
or a trimmed no-op therefore returns to the persisted value instead of being
protected by focus. This is a page-private baseline check, not autosave,
cross-node draft persistence, a revision/CAS protocol, or a general
conflict-merging framework.

Qt documents that `QLineEdit::setText()` clears selection and Undo/Redo state,
moves the cursor, and resets `modified`, while user edits set `modified`:
<https://doc.qt.io/qt-6/qlineedit.html#text-prop> and
<https://doc.qt.io/qt-6/qlineedit.html#modified-prop>. Qt Creator 20.0 has the
same persisted-versus-editable distinction in `BaseAspect::value()`,
`volatileValue()`, and `isDirty()`, and applies string edits on
`editingFinished`:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/aspects.h#L328-L370>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/aspects.cpp#L1253-L1320>.
Beckhoff's General-tab documentation identifies editable Name plus Id, Object
Id, and Type fields but does not define a Qt refresh-conflict policy:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html>.

Failure-first changed only the Workbench test. Production `generalpage.cpp`
and `.h` remained at SHA-256
`a8bee2a329e954482ede4130e3005e70e46888251617acc9d9b23a9378f034fc`
and `c7c5b44b0bc7e9c3bbf9acf22b9a102e32e38e4d67bd50954804ded43a330f92`,
with git blobs `b07a339f5512acac70a2c80357183041e250e3c2` and
`f72ee71744e19d99f4a950bb514987282260e0d0`. A real imported ESI and
`rebuildIndex()` delivered `indexingChanged(true)`, `devicesReset`, and
`indexingChanged(false)`; the old page replaced a long focused Unicode draft
containing literal `%1`, `%2`, and `%%` with `General Draft Project`.
Initialization and cleanup passed, while the intended text assertion failed
safely and the target exited with status 1 in `failure-first.log`.

The final test covers all four name editors. Project, Target, and Master use
real repository rebuilds; configured Slave uses a same-identity ESI update and
proves the Type field changes while its draft text, modified state, focus,
cursor, selection, and local Undo state remain intact. A clean focused Project
editor covers the focus gate intended to avoid disturbing a possible preedit;
it does not synthesize a platform input method. Another Project summary field
proves non-name data remains fresh. Each row executes the preserved local
Undo/Redo history, then performs a real authoritative rename. The final Slave
row also proves Project Undo/Redo, trimmed no-op and empty rejection, node
switch, and Project close override or destroy the draft correctly.

Final SHA-256 values for `generalpage.cpp`, `generalpage.h`, the Workbench test
implementation, and its declaration are respectively
`51195c26f5db3936f2e0ba0935d2564d3b8a0bf2ca95c86328de7d39e6ee16e6`,
`93760569091e384acb8a68451b8c3ae8c2f6ce2f744bbcab700780bc1423aaca`,
`b37584e021ef2c81cd2500d46a14f2f2f8f7d77e69908f5d613d1311f44e8123`,
and `daa78bddcef5def77301b8c610266f1d9ac78bdea7c04e1a20e0ea1e7b8e8ace`.
Their git blobs are `e2cce8dd0a1fe6f5837de15492ead898246bffb8`,
`051abe8c5e068d59351cdd8ea7ceba13607f2558`,
`4f4d9fd4d17300be8761cbebc696cf3237f6f8ff`, and
`c35e54d7c2e36b24c13a7d4790458d12957c6e7f`.

Focused normal and 2x runs each passed three events. The related General,
repository, Details-empty-state, and focus-continuity group passed eleven
events at each scale. Complete Workbench runs passed 68 events at each scale.
The six isolated suites passed 119 events: Core 17, Project 12, Devices 8,
Workbench 68, Scan 7, and Diagnostics 7. The complete Workbench runs retain
the known pre-existing ProjectExplorer TaskHub soft assertion in the invalid
project path; it is absent from the focused test and did not fail a test or
target.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`03859197305b62f9b06e42e59c9253eefa34cbd112644645d9b0ae413de7fa76`
and `3ff8a25cd361749e91deff31bf5e41331935b171a612f48cbd7bb61470944d12`.

Enabled product startup observed PID 62199 alive for 37 consecutive samples
after Workbench became ready, with the plugin loaded in all 37. Explicitly
disabled startup observed PID 63311 alive for 37 samples after Core became
ready, with Workbench absent in all 37. LLDB passed SIGTERM directly without
stopping or notifying, and both targets recorded status 15. Every executable
run used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar bypass. No visible main window was opened.
Both lifecycle directories retain all 37 timestamped raw samples as well as
their summaries and LLDB logs. The 2026-07-22 12:14:15 +0800 audit found no
residual Embed Labs or LLDB
process, new matching DiagnosticReports file, or matching crash-service event.

Evidence is under
`/private/tmp/embed-labs-general-draft-final.bvD4CU`. This issue changes only
the private `generalpage.cpp/.h`, Workbench test declaration/implementation,
and these four documents. It changes no repository signal, Details refresh
route, public API, source file, dependency, ProjectService or Provider
contract, Project format, persistence field, Project command, model role,
Core or ProjectExplorer hook, application bootstrap, production thread or
timer, network transport, scan, online state, SDO execution, or hardware
behavior. No CMake or qbs description changed, so qbs was not run. The
unrelated `WITH_TESTS=ON` all-target build was not rerun because the known
EasyBoard `extensionmanager_test.h` blocker remains outside this private
Workbench issue. Qualification is local/offline Mock evidence; visible desktop
inspection was intentionally not run so acceptance did not interrupt desktop
use.

## ESI revision-toggle selection continuity

`ISSUE-WB-ESI-REVISION-TOGGLE-SELECTION-001` is qualified from local baseline
`42110b4e990baae92fbed37f38ab6aa274b34657`. It fixes one user-visible Add
New Item correctness problem. The dialog previously reselected the first
supported row every time `Show Previous Revisions` changed. A user who had
selected another current revision could therefore reveal the older rows,
leave Add enabled, and unknowingly append a different device.

The dialog now captures the selected device's stable `Data::NodeId` before
the revision filter changes. After the proxy refresh it restores that device
when the row remains visible. If the device is no longer visible, or there
was no valid selection, the existing fallback still chooses the first
supported visible row and then the first visible limited row. Text filtering
still intentionally chooses the first supported matching row, or the first
visible limited row when no supported match exists. Sorting, revision
qualification, the limited-device guard, status text, Add/Cancel behavior,
and the existing controller mutation and Undo/Redo path are unchanged.

Qt documents that `QItemSelectionModel` owns the view's current item and that
`setCurrentIndex()` replaces it:
<https://doc.qt.io/qt-6/qitemselectionmodel.html>. Its proxy model performs
only the existing row sorting and filtering:
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html>. Beckhoff documents that
offline insertion normally offers the latest revision, that older revisions
must be explicitly revealed and selected, and that the selected revision is
then appended:
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.
The general Add New Item confirmation contract is at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1096103307.html>.

Failure-first changed only the Workbench test. Production
`esideviceselectiondialog.cpp/.h` remained at SHA-256
`8645fe5679f3155d09a425fc80eabb0cf185f186aaec6706d3f387e1d4291816` and
`dfa2a65a2702b38c15da210d99ae0adbe516a639d72238e0061d8c514f7b7b3a`,
with git blobs `d603080f1fd9b9a6c34d8f2ced8bf0cf3f26e36d` and
`03d7ea0b6a7acbf8582e7e8e43544e90c4499fe2`. The test selected a supported
`Zulu Current` row while `Alpha Current` sorted first, enabled previous
revisions, and observed the old code replace the selected ID with Alpha. The
intended ID assertion failed safely and the target exited with status 1 in
`failure-first/test.log`.

The final focused test proves that the current revision remains selected,
the previous revision appears, Add remains enabled, and accepting the dialog
retains the same stable device ID. Focused normal and 2x runs each passed
three events. The selection, insertion, accessibility, target-lifecycle, and
asynchronous-dialog related group passed ten events at each scale. Complete
Workbench runs passed 69 events at each scale. The six isolated suites passed
120 events: Core 17, Project 12, Devices 8, Workbench 69, Scan 7, and
Diagnostics 7. The complete Workbench runs retain the known pre-existing
ProjectExplorer TaskHub soft assertion in the invalid-project path; it did
not fail a test or target.

Final SHA-256 values for the dialog implementation, dialog header, Workbench
test implementation, and test declaration are respectively
`fd52d33b1bdc7d8b9aa5ce88ddcfe863c16b20054e8d85518ef693a5196148f5`,
`472a6169ba2dfacea350a41b39ce4859b7cb8370244fd60a8c759cbf6c1181fc`,
`3259aa89d7d388ba1a212e7fef9a24f0c3004a29e00e10259f0196c1e8d4fa53`,
and `8fbe321e0d55aaddb76d51e44c70a7ee34282462753fc95805a46245700b2ccb`.
Their git blobs are `5adb883e4c6bb09ebcd9772ab762261523010069`,
`23eac3adaf0335b3590a28a9da7dbc81378e0968`,
`da747581823bb9366ecbe12140932b872b55e0f0`, and
`272cf0b738477b17f62c7e8da409a1e726a4363a`.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16
allow-listed plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`759d4904b9a5dae8734e3aaf2fb3eefa10f47e60facdf9003610655b92a89838`
and `5514510a42c91149247a90edd4ea7d34bd7db846745e4bb3cecf08bf890edaa8`.

Enabled product startup observed PID 40382 alive for 37 consecutive samples,
with the Workbench plugin loaded in all 37. Explicitly disabled startup
observed PID 42145 alive for 37 consecutive samples, with Workbench absent in
all 37. LLDB passed SIGTERM directly without
stopping or notifying, and both targets recorded status 15. Every executable
run used fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and
only the process-local Touch Bar bypass. No visible main window was opened.
The 2026-07-22 12:39:57 to 12:47:33 +0800 product-lifecycle audit found no
residual product or LLDB process, new matching DiagnosticReports file, or
matching crash-service event.

Evidence is under
`/private/tmp/embed-labs-esi-revision-selection.pcE5YX`. This issue changes
only private `esideviceselectiondialog.cpp/.h`, the Workbench test
declaration/implementation, and these four documents. It adds no public API,
source file, dependency, Provider or ProjectService contract, Project format,
persistence field, Project command, custom model role, Core or
ProjectExplorer hook, application-bootstrap change, production thread or
timer, network transport, scan, online state, SDO execution, or hardware
behavior. No CMake or qbs description changed, so qbs was not run. The
unrelated `WITH_TESTS=ON` all-target build was not rerun because the known
EasyBoard `extensionmanager_test.h` blocker remains outside this private
Workbench issue. Qualification is local/offline Mock evidence; visible
desktop inspection was intentionally not run so acceptance did not interrupt
desktop use.

## Preferred Diagnostics status projection

`ISSUE-WB-STATUS-PREFERRED-DIAGNOSTICS-001`, based on local commit
`0524d2dfbb14cacd6f25c584ede4bcce9fe416c4`, closes the inconsistency where the
Diagnostics commands and tree showed Config, FreeRun, or Run/OP while the
status control collapsed every healthy mode to `MOCK Ready`. The status now
projects the already selected preferred Diagnostics Provider as `MOCK Config /
PREOP`, `MOCK FreeRun / SAFEOP`, or `MOCK Run / OP`. Beckhoff's Config/FreeRun
status-bar and OP descriptions are used only as terminology and interaction
references:
<https://infosys.beckhoff.com/content/1033/el6201/1037001483.html>.

Starting and Stopping map to Busy, Failed to Fault, and an active alarm or
Master error to Warning. A higher-severity `StateService` contribution remains
authoritative; at equal severity, a non-Mock Provider keeps neutral
`Diagnostics` wording rather than being relabeled as local Mock. The tooltip,
disabled menu entry, and accessible description
retain the preferred Provider name, reported mode, Project/Master identifiers,
and the local-Mock or Provider-reported boundary. A non-Mock snapshot is never
presented as proof of a controller, transport, online transition, or physical
hardware. Provider availability, availability recovery, deterministic
replacement, and removal are reflected immediately; signals from a removed
Provider no longer affect the status.

Failure-first changed only the new Workbench test while the five production
files retained their original SHA-256 values. The target exited with status 1
after observing `MOCK Ready` where `MOCK Config / PREOP` was required. Final
focused runs passed three events twice at normal scale and once at 2x; related
runs passed eight events on the same matrix; complete Workbench runs passed 72
events on all three runs. Six isolated plugin suites passed 123 events: Core
17, Project 12, Devices 8, Workbench 72, Scan 7, and Diagnostics 7. The known
pre-existing ProjectExplorer TaskHub soft assertion remains limited to the
invalid-project test path and did not fail a test or target.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16 plugin
dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`f99970b4ee69e009236b0156168caa29e5c2944f7ea7de803d357dc55eeacf91`
and `20857c91a8e92157677c7c527b6df2a9572e4570ceeb4f038d2c62d6ab9e18ff`.

Enabled product PID 5129 remained alive for 37 consecutive post-ready
samples with Workbench mapped in all 37. Explicitly disabled PID 7036
remained alive for 37 samples with `-noload EtherCATWorkbench` and Workbench
absent in all 37. LLDB passed intentional SIGTERM without stopping or
notifying; both targets recorded status 15. Every executable run used fresh
HOME/settings, cleared inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar bypass. The 2026-07-22 18:30:36 to 18:33:08 +0800 audit found no residual
qualification process, new matching DiagnosticReports file, or matching crash
service event. No visible main window or manual desktop inspection was used.

Evidence is under
`/private/tmp/embed-labs-wb-status-preferred-diagnostics-001.urIOBN`. The issue
changes only seven existing private Workbench implementation/test files and
these four documents. It adds no public API, source file, dependency,
Provider or ProjectService contract, Project format, persistence field,
Project command, model role, Core or ProjectExplorer hook, application
bootstrap change, production thread or timer, network, ADS, scan, online
state, SDO execution, or hardware behavior. No CMake or qbs description
changed, so qbs was not run. The unrelated `WITH_TESTS=ON` all-target build
was not rerun because the known EasyBoard `extensionmanager_test.h` blocker
remains outside this private Workbench issue.

## Distributed Clocks non-conflicting refresh draft continuity

`ISSUE-WB-DC-NONCONFLICTING-REFRESH-DRAFT-001`, based on local commit
`304eaf73d4020b59abba15116868bcb8f50cc9f8`, prevents a same-context Project
or ESI Repository refresh from replacing an uncommitted Distributed Clocks
editor value when that field's Project authority did not change. The earlier
page rebuilt every DC control on each refresh, so a configured-slave draft
could be lost before `editingFinished`.

The delayed editors are Operation Mode name, AssignActivate, SYNC0 cycle and
shift, and SYNC1 cycle and shift. `Enable DC`, the SYNC0/SYNC1 enable boxes,
and Potential Reference Clock remain immediate Project commands; they are not
part of the six-field draft set. Each delayed field independently survives
only while all of these conditions hold:

- the Project ID, node ID, and configured-slave kind are unchanged;
- the Project is still valid and the page remains editable;
- the editor is modified or focused; and
- the fresh authoritative value for that field equals its last baseline.

This is intentionally a field-level decision, not an equality check over the
whole `DcConfiguration`. Project rename, a same-identity ESI update, or an
immediate reference-clock command can therefore refresh other authority while
preserving non-conflicting drafts. If one field changes externally, that
field reloads while eligible sibling drafts remain intact. A draft is never
written to the Project until its own explicit commit.

Every editor commit starts from the latest authoritative configuration and
forces that field to reload across synchronous Project signals, successful or
no-op normalization, parse rejection, validation rejection, and service
failure. Selecting an ESI operation mode and restoring ESI defaults force all
six delayed fields to authority. External DC changes, Project Undo/Redo, a
node switch, invalid context, and Project close also reload or destroy drafts
from current authority. No draft is cached across nodes.

While an Operation Mode draft is retained, its visible combo-box model is
also retained so focus, selection, cursor, and local line-edit Undo/Redo state
are not destroyed. After an ESI refresh, selection is resolved against the
current ESI list by the complete `DcModeDescription` value rather than display
name. This preserves the distinction between two modes both named `Sync0`.
After submission and later refresh, duplicate display names are also
disambiguated from the Project's mode-owned values, so the combo remains on
the row that supplied the current AssignActivate and timing values. A unique
name retains the established name-based presentation; multiple same-name rows
with no complete value match are represented as a custom value rather than
guessing the first row. If a deferred old value no longer exists, the page
reloads the current Project mode and asks the user to select a current ESI
mode. Potential Reference Clock is not part of `DcModeDescription`; an ESI
mode selection retains its latest Project value.

Qt documents that `QLineEdit::setText()` clears selection and undo/redo,
moves the cursor, and resets the modified state, which is why a preserved
editor is not mutated during refresh:
<https://doc.qt.io/qt-6/qlineedit.html>. The editable combo behavior follows
Qt's `QComboBox` contract:
<https://doc.qt.io/qt-6/qcombobox.html>. Beckhoff's Distributed Clocks pages
are terminology and interaction references only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>,
<https://infosys.beckhoff.com/content/1033/tcsystemmanager/1092594187.html>,
and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2469120395.html>.

Failure-first changed only the new Workbench test while `dcpage.cpp/.h`
retained SHA-256 values
`b1b8c7992332e690f51324abfad22a95edac5d7835715ae8120e36057478359d`
and
`eeb96e55cb546187a277e79f4c213148ad153a09ea844bfd1fcfca6540137376`.
The old page displayed authoritative SYNC0 cycle `125000` instead of draft
`130000` and exited with status 1. A review-derived red test then kept the
intermediate production SHA-256 values
`c2df72b72419cc6cffaaa61e7343d5d4f9499a6f530e0178f8655af3b312dc7e`
and
`94ea851d21149b5ce52a07ecfa23ebc7cb0badc145691ae52cf3447bb4002a46`
unchanged and proved that a correctly persisted second same-name mode was
highlighted as index 0 instead of 1; that target also exited with status 1.
Final focused runs passed three events
twice at normal scale and once at 2x. Related runs passed nine events on the
same matrix, and complete Workbench runs passed 73 events on all three runs.
The six isolated suites passed 124 events: Core 17, Project 12, Devices 8,
Workbench 73, Scan 7, and Diagnostics 7. Complete Workbench runs retain the
known pre-existing ProjectExplorer TaskHub soft assertion in the
invalid-project path; it did not fail a test or target.

The test directly covers all six drafts surviving together; Project rename;
same-identity ESI refresh; sibling-authority isolation across immediate and
delayed commits; trim-equivalent no-op normalization; invalid input; duplicate
mode names; the second same-name row remaining current after submission and a
later Project refresh; node switch; and Project close. The external conflict
and Project Undo/Redo assertions directly exercise SYNC1 cycle while a SYNC1
shift draft survives. The Operation Mode editor additionally verifies Unicode
and literal `%1` text, focus, selection, cursor, and real line-edit Undo/Redo.
It does not claim native IME-composition coverage.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16 plugin
dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`93d405a9393b10057fe992d08c7c883f587bcc68e373513799d00f7636f489c3`
and
`93069046bdf0bbac3c6e67984dcd1a0c85ec61826a15ac527ad7c1198efa9ba4`.

Enabled product PID 79153 remained alive for 37 consecutive samples with
Workbench mapped in all 37. Explicitly disabled PID 81410 remained alive for
37 samples with `-noload EtherCATWorkbench` and Workbench absent in all 37.
LLDB passed intentional SIGTERM without stopping or notifying; both targets
recorded status 15. Every executable run used fresh HOME/settings, cleared
inherited DYLD variables, `QT_QPA_PLATFORM=offscreen`,
`CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch
Bar bypass. No visible main window was opened.

The explicitly disabled run emitted the existing shared-memory initialization
message, then remained alive through all 37 samples and produced no crash
artifact or service event.

An intermediate test-driver mistake used the ASCII-only QTest key helper for
Unicode and triggered a Qt Test assertion. Its generated report was moved to
the evidence directory under `quarantined-test-harness-reports/`; this was not
a product-path failure. After replacing that input path, no later run produced
another assertion crash report; expected failure-first and test-development
failures remained ordinary status returns, and the final matrices exited 0.
The final 2026-07-22 19:39:27 to 19:44:10 +0800 product and post-documentation
audit
found no residual qualification process, new matching DiagnosticReports file,
matching crash-service event, or system crash dialog.

Evidence is under
`/private/tmp/embed-labs-wb-dc-draft-refresh-001.6ifOHJ`. This issue changes
only private `dcpage.cpp/.h`, the Workbench test declaration/implementation,
and these four documents. It adds no public API, source file, dependency,
Provider or ProjectService contract, Project format, persistence field,
Project command, model role, Core or ProjectExplorer hook, application
bootstrap change, production thread or timer, network, ADS, scan, online
state, SDO execution, ESC/EEPROM write, or hardware behavior. No CMake or qbs
description changed, so qbs was not run. This is offline Project configuration
editing continuity, not real-time DC or hardware-clock qualification.

## CoE same-context view-state continuity

`ISSUE-WB-COE-SAME-CONTEXT-VIEW-STATE-001`, based on local commit
`2e0bc521205d130ea2d56fe6b88901185977d8c2`, keeps the CoE page stable while
fresh Project or ESI data is rendered for the same logical node. A stable
context is non-`None` and has the same Project ID, node ID, and node kind.
Within that boundary the page keeps the text filter, its focus, cursor,
selection and Undo availability, the Advanced range and Hide flags, the
Mock/offline choice, the Mock sample generation, and a scalar selected-object
address.

The model is still rebuilt from the latest Project and Repository snapshots.
A Project rename directly preserved Unicode and literal `%1` filter text,
focus, selection, cursor, Undo availability, Profile-specific plus Hide
Standard filtering, Mock sample 2, and the selected `6060:00` address. A
same-identity ESI update then preserved the filter, offline source, Advanced
state, and sample generation while exposing the refreshed `6072:00` ESI
comment. The page therefore retains view state, not stale object definitions.

The selection anchor is the page-owned numeric address, never a
`QModelIndex`, model item, Repository object, or Project object. If that
address still exists in the rebuilt source model, a temporary proxy filter
may hide it without replacing the semantic anchor with the fallback row.
Clearing the filter restores it; the qualification covers
`6060:00 -> 6072:00 visible -> refresh -> clear -> 6060:00`. If the address
has actually disappeared, the page adopts the current valid fallback row or
clears the anchor.

Switching Project, node ID, or node kind resets the filters, source choice,
sample generation, and selection state. The existing workflow directly
verifies this reset on the same page instance; the new lifecycle test also
switches away so the page is destroyed, re-enters with an empty filter,
offline disabled and sample 0, and then verifies Project close destroys it.
There is no cross-node cache. Feedback is still cleared on every refresh.
That view-state issue did not retain manually edited Mock values. The separate
`ISSUE-WB-COE-NONCONFLICTING-MOCK-VALUE-REFRESH-001` boundary below now
retains only accepted edits whose fresh object authority is still compatible.

Every `setContext()` still increments the private context generation. An old
Advanced or Add-to-Startup response is therefore rejected after any refresh,
including a same-stable-context refresh. This continuity is not signal
suppression, a Repository revision or CAS protocol, persistence, or a generic
view-state service.

Qt's [QAbstractItemModel](https://doc.qt.io/qt-6/qabstractitemmodel.html),
[QSortFilterProxyModel](https://doc.qt.io/qt-6/qsortfilterproxymodel.html), and
[QItemSelectionModel](https://doc.qt.io/qt-6/qitemselectionmodel.html)
contracts inform the model rebuild, proxy mapping, and explicit scalar-anchor
handling. Beckhoff's
[CoE Online](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html)
and
[extended CoE Online](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446522251.html)
pages are terminology and interaction comparisons for the object list,
Update List, source selection, ranges, and Hide filters. They do not specify
this Qt refresh-continuity behavior, which is local to Embed Labs.

Failure-first changed only the new test while `coeonlinepage.cpp/.h` retained
SHA-256 values
`6b8738ff5e7f245ddb2ad746aa2058b8452343471bd86e85cbeee29d66be5478`
and
`542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952`.
The old page cleared the expected `Mode %1 / 模式` filter to an empty string
after Project rename and exited with status 1. Review then exposed a hidden
anchor defect while the intermediate implementation/header SHA-256 values
remained
`783b87d4e3ee23dd7080c8c3a3777c5b31cfcc1dfc48f5729258481684f0f5af`
and
`542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952`:
after refresh and filter clear the page selected `6072:00` instead of
`6060:00`; that target also exited with status 1.

Final focused runs passed three events twice at normal scale and once at 2x.
Related CoE runs passed eight events at normal scale and 2x. Complete
Workbench runs passed 74 events twice at normal scale and once at 2x. The six
isolated suites passed 125 events: Core 17, Project 12, Devices 8, Workbench
74, Scan 7, and Diagnostics 7. All final targets exited 0. Complete Workbench
runs retain the known pre-existing ProjectExplorer TaskHub soft assertion in
the invalid-project path; it did not fail a test or target.

The final implementation, Workbench tests, and test declaration SHA-256 values
are
`2faebb9f5095f4540a7dd1f70fc9dcbaf06958ed6101b524bc66615750da69a4`,
`a7929f7af05a4a64326b993a7bd26993711c1836375682bb99682810bac72e08`,
and
`eae31102217c668bc4e05849e943fedee8f48912629da5103d268bbeebaa60a4`.
Their git blobs are `253744dc05c01ff3a9f0b2fa754bc9cf5e6291cc`,
`bbae1e0c1ffe70b18c7461eb2ea4c4998ecaf998`, and
`c10d27e7c7800630308f7f7212d19f5a0f861f22`.

The full `WITH_TESTS=OFF` product build passed and contains exactly 16 plugin
dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`68f547b1b7256e1ff1017b06ef50335d24c4ebd6c3ef610ddf8bbd176df4400e`
and
`3c0878e9e5065cd2b4258f16ebd403700bc02f48c08e022095fa1a092e193068`.

Enabled product PID 65666 remained alive for 37 consecutive samples with
Workbench mapped in all 37. Explicitly disabled PID 68023 remained alive for
37 samples with `-noload EtherCATWorkbench` and Workbench absent in all 37.
LLDB passed intentional SIGTERM without stopping or notifying, and both
targets recorded status 15. Fresh HOME/settings, cleared inherited DYLD
variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and the process-local Touch Bar bypass kept the acceptance
invisible and non-interrupting. The disabled run emitted the existing
shared-memory initialization message but remained alive for all samples.
The 2026-07-22 20:21:05 to 20:23:56 +0800 audit found no residual process,
new matching DiagnosticReports file, matching crash-service event, visible
main window, or system crash dialog.

Evidence is under
`/private/tmp/embed-labs-wb-coe-view-state-001.4vssYQ`. The issue changes only
private `coeonlinepage.cpp`, the Workbench test declaration/implementation,
and these four documents. It adds no public API, source file, dependency,
Provider or ProjectService contract, Project format, persistence field,
Project command, model role, Core or ProjectExplorer hook, application
bootstrap change, thread, timer, network, ADS, scan, online state, SDO
execution, or hardware behavior. No CMake or qbs description changed, so qbs
was not run. The unrelated `WITH_TESTS=ON` all-target build was not rerun
because the known EasyBoard `extensionmanager_test.h` blocker remains outside
this private Workbench issue. This is local/offline Mock view continuity, not
an online CoE, controller, transport, or physical-device qualification.

## Startup non-conflicting refresh inline-draft continuity

`ISSUE-WB-STARTUP-NONCONFLICTING-REFRESH-DRAFT-001`, based on local commit
`a835d3340e0a3d644928adb445a8c916fda17e23`, keeps one active configured-slave
Startup table editor usable while fresh authority is rendered for the same
Project, node ID, and configured-slave kind. Preservation additionally
requires an editable Project, the same non-null request ID, a non-fixed
request, an unchanged value for the edited field, and the same stored-versus-
ESI-proposal source. The source transition caused by that editor's own first
commit is handled inside the commit guard.

`StartupTableModel` sorts the fresh request values and, only when both the old
and new lists have unique non-null stable IDs, synchronizes them with row
remove, insert, and move notifications. It publishes fresh sibling-cell data
without sending `dataChanged()` for the active edit cell. Qt item views ignore
the supplied roles when deciding whether to call `setEditorData()`, so even a
metadata-only notification for that index could otherwise replace the draft.
Accessibility, description, and tooltip changes for that cell are deferred
until its editor is destroyed and are then located again by stable request ID.

The editor widget itself remains owned by the table. Its text, modified state,
focus, selection, cursor, and line-edit Undo history therefore survive Project
rename, Repository re-index, sibling-field refresh, and structural insertion,
removal, or movement before its request. A synchronous
`ProjectService::projectChanged` during inline submission is covered by a
page-private commit scope, so the submitted cell is refreshed without a model
reset and the latest sibling authority is retained.

A same-field external change, fixed/read-only transition, different Project or
node, invalid context, removed request, or unrelated ESI/source transition
closes the editor with `RevertModelCache` and performs the authoritative reset.
There is no cross-node draft cache. Page destruction first removes the editor
from the item view and then synchronously destroys any editor left behind by
the delegate's default `deleteLater()` behavior. This prevents a deferred
editor from outliving the page; teardown never commits its pending text.

The implementation follows Qt's
[model structural-change contract](https://doc.qt.io/qt-6/qabstractitemmodel.html),
[delegate editor contract](https://doc.qt.io/qt-6/qabstractitemdelegate.html),
[item-view reset behavior](https://doc.qt.io/qt-6/qabstractitemview.html#reset),
and [line-edit state contract](https://doc.qt.io/qt-6/qlineedit.html).
Beckhoff's
[Startup page](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html)
is used only as the ordered mailbox-request interaction comparison. It does
not define this local Qt refresh-continuity behavior.

The authoritative failure-first run changed only the new test while the old
production `startuppage.cpp/.h` SHA-256 values were
`2cf3ebe16293d99758f0501e89cc83def3b46861d15535f11301aa82b2e653d8` and
`a743a90d1dc6e917b1fbc61e474b134b8cd9ca04574373e40a61213c3f76825c`.
The old model emitted two resets instead of zero and the target exited 1.
Final implementation/header SHA-256 values are
`99902dbe8615dedc25380b8bbb7117ad54da9d092ec3e12bcf97afba57c5235f` and
`01c85b28ca3b487f425c03380e4f1e6662574512a377c05344cd6157093e6eb6`.

Final focused runs passed three events at normal and 2x scale. Startup-related
runs passed seven events at both scales. Complete Workbench runs passed 75
events at both scales. The isolated suites passed 126 events: Core 17,
Project 12, Devices 8, Workbench 75, Scan 7, and Diagnostics 7. Every target
exited 0. Complete Workbench retains the known pre-existing ProjectExplorer
TaskHub soft assertion in the invalid-project path; it did not fail a test or
target.

The `WITH_TESTS=OFF` product Workbench target built successfully. Enabled PID
23631 remained alive for 37 samples with Workbench mapped in all 37; explicitly
disabled PID 25473 remained alive for 37 samples with
`-noload EtherCATWorkbench` and Workbench absent in all 37. LLDB passed the
intentional SIGTERM through and both targets recorded status 15. From
2026-07-22 21:37:00 to 21:39:28 +0800 there was no residual matching process,
new Embed Labs diagnostic report, matching crash-service event, visible main
window, or system crash dialog.

Every executable used a fresh HOME/settings path, cleared inherited DYLD
variables, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`,
`-no-crashcheck`, and the process-local Touch Bar bypass. Visible/manual UI
inspection was not run because runtime acceptance had to remain invisible and
must not pause the user's main program.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-startup-draft-001.XNDmkO`; its manifest identifies
the one valid red proof and superseded intermediate logs. The issue changes
only private `startuppage.cpp/.h`, the existing Workbench test declaration and
implementation, and these four documents. It adds no generic table or draft
service, public API, source file, dependency, Project format, persistence
field, Provider or ProjectService contract, Project command, model role,
thread, timer, Core or ProjectExplorer hook, application bootstrap change,
network, ADS, scan, online state, SDO execution, or hardware behavior. Modules
and Channels remain a separate data/API chain and are not completed here.
`EtherCATWorkbenchPlugin` remains In progress. No CMake or qbs description
changed, so qbs was not run.

## CoE non-conflicting inline-draft continuity

`ISSUE-WB-COE-NONCONFLICTING-INLINE-DRAFT-REFRESH-001`, based on local
commit `d26f695ec7581b7864e87577d0b935997a30546d`, keeps one active Value
editor usable across a refresh of the same configured slave. The page tracks
the editor by scalar object address, never by row or `QModelIndex`. It first
builds the complete target dictionary, including the current Mock generation
and every compatible accepted override, and then compares the active object's
old and target authority.

Preservation requires the same non-null Project ID, node ID, and configured-
slave node kind; an editable Mock source; the same writable, non-synthetic
object; unchanged offline and final Mock bytes, parsed and raw data type,
process-data role, edited-state provenance, and active-root child count. Name,
unit, and sibling-object metadata may refresh while the active row remains
visible through the current filter. A sibling object may be inserted or
removed without changing the active address. Preservation is not claimed when
fresh metadata filters the active row out of the proxy view; normal view
behavior may then close its editor.

When the gate holds, the private tree model synchronizes roots and children by
object address with row remove and insert notifications. Matching
`CoeObjectItem` instances remain allocated, their parent and row metadata are
updated inside the corresponding begin/end notification, and the active Value
column is excluded from `dataChanged()`. A defensive move path exists inside
the private synchronizer, but sorted production definitions do not currently
exercise or qualify it. The same `QLineEdit` therefore keeps its uncommitted
text, modified state, focus, selection, cursor position, and Undo history.
Fresh sibling and non-Value metadata remain visible, and a persistent index
follows the active object when an earlier sibling is added or removed.

Valid Return still commits only to the page-local Mock model. A rejected Return
leaves the last accepted bytes intact and routes a private validation result to
the separate edit-rejection feedback boundary. Escape restores the last
accepted Mock bytes while retaining any current rejection guidance. None of
these paths writes the Project or creates a Project Undo command. Update List
and Show Offline are deliberate clear boundaries.
An offline/final-Mock value or width conflict, parsed/raw type change,
writable or process-data change, active-root structural conflict, object
removal, context change, and page destruction close the editor with revert
semantics and render fresh authority. The page destructor also synchronously
releases an editor left queued for deferred deletion, so teardown cannot
commit pending text.

This is a single-editor, page-private synchronization boundary. It is not a
persistent-editor feature, generic tree synchronizer, cross-node draft cache,
autosave mechanism, Repository revision, Project revision, CAS/merge service,
or conflict UI. It adds no public API, Project field, persistence, Provider or
ProjectService contract, Project command, public model role, production
thread/timer, source file, dependency, CMake/qbs entry, Core or
ProjectExplorer hook, application bootstrap, network, ADS, scan, online CoE,
SDO, controller, PLC, or hardware behavior.

Qt documents that model reset invalidates current/selected data and resets
attached views, while an item-view reset closes open editors without
committing their changes. The implementation therefore avoids reset only for
the compatible active address. It retains the existing editor close/revert
lifecycle while the cpp-local delegate checks the Return result:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel>,
<https://doc.qt.io/qt-6/qabstractitemview.html#reset>,
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and
<https://doc.qt.io/qt-6.8/qlineedit.html>. Beckhoff's CoE page remains a
terminology and interaction reference for Value, RW/RO, Offline value, and
Update List only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

Final qualification rebuilt the test and `WITH_TESTS=OFF` product trees.
The focused test passed at 1x and 2x scaling, and four complete Workbench runs
each passed 78 events. The six isolated EtherCAT suites passed 129 events:
Core 17, Project 12, Devices 8, Workbench 78, Scan 7, and Diagnostics 7. The
product app contains the expected 16 plugin dylibs. Enabled and explicit
`-noload EtherCATWorkbench` lifecycle runs each remained alive for 37 of 37
one-second samples; the Workbench dylib was present in all enabled samples and
none of the disabled samples. Passed-through SIGTERM ended both runs with
status 15, leaving zero residual processes, new Embed Labs diagnostic reports,
or matching crash-service events. All runs used an offscreen platform, fresh
HOME/settings, and disabled crash reporting, so no visible main window or
manual UI inspection was involved. Evidence is under
`/private/tmp/embed-labs-wb-coe-inline-draft-001.177q7M`.

## CoE non-conflicting Mock-value refresh continuity

`ISSUE-WB-COE-NONCONFLICTING-MOCK-VALUE-REFRESH-001`, based on local commit
`925b14062f93403cfc9f9552dcaec1e3015ba662`, keeps an accepted local Mock
value across a refresh of the same configured slave. Preservation requires
the same non-`None` Project ID, node ID, and node kind, the same scalar object
address, a writable non-synthetic object, and unchanged offline bytes and
width, parsed data type, raw data type, writable flag, and process-data role.
Only values already accepted by the model's `setData()` path qualify for that
override replay. The separate active-editor boundary now covers text that has
not yet been committed.

A real Project rename and a same-identity ESI metadata or sibling-object update
therefore keep the local override while the page renders fresh Project and ESI
authority elsewhere. Offline view always shows the authoritative offline
bytes; returning to Mock view reveals the retained override. Add to Startup
reads those current Mock bytes after confirmation, but the edit itself never
changes or persists the Project.

Explicit Update List is the deliberate clear boundary. In Mock view it drops
all accepted overrides before generating the next sample; in Offline view it
rebuilds without exporting overrides, so returning to Mock also shows a fresh
sample. An empty offline baseline is valid: any non-empty byte sequence already
accepted by `setData()` may survive while the baseline remains empty and all
other authority matches, while Update List clears it back to empty. A changed
offline value or width, parsed or raw type, writable or process-data role,
object removal, genuine context switch, or page destruction discards the
override.

The private model exports a value-only map keyed by object address before its
normal reset, installs current definitions, and then replays only compatible
entries. It retains no `QModelIndex`, model item, Project object, Repository
object, or cross-node cache. Both source and proxy models are checked by
`QAbstractItemModelTester`. This follows Qt's
[model reset contract](https://doc.qt.io/qt-6/qabstractitemmodel.html).
Beckhoff's
[CoE Online page](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html)
is used only for object-value, RW/RO, Offline-value, and Update List
interaction terminology; it does not define this local Mock continuity.

Failure-first changed only the new Workbench test while production
`coeonlinepage.cpp/.h` retained SHA-256 values
`2faebb9f5095f4540a7dd1f70fc9dcbaf06958ed6101b524bc66615750da69a4`
and
`542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952`,
with baseline blobs `253744dc05c01ff3a9f0b2fa754bc9cf5e6291cc` and
`e916c21579d01c6558e839632c840c7fc2d2c5af`. After a real Project rename,
the old page returned `08` instead of the accepted `5A`; the target exited 1.

Final focused runs passed three events at normal and 2x scale. Related CoE
runs passed nine events at both scales. Two complete Workbench runs at each
scale passed 77 events each. The isolated suites passed 128 events: Core 17,
Project 12, Devices 8, Workbench 77, Scan 7, and Diagnostics 7. Every target
exited 0. Complete Workbench retains the known pre-existing ProjectExplorer
TaskHub soft assertion in the invalid-project path; it did not fail a test or
target.

Final implementation/header SHA-256 values are
`4f6a83237a8be1158799242f5e26f31490ee7559349c1aabc9fddc0d95c5fe0d`
and
`ef599680bc35e50556b8f222b06eb7b2672dfac01324ef5993e0a857d5ddb66a`;
test implementation/header values are
`e996ee623817b12b7f0a062506f797e6e32f4d095bb0fa656126e58a6c906422`
and
`7494c2ea202151a14df773805f503e397a42fd284a5dba1a32ec9bb2b9d53e49`.

The complete `WITH_TESTS=OFF` product build passed and contains 16 plugin
dylibs. Enabled PID 96279 remained alive for 37 samples with Workbench mapped
in all 37; explicitly disabled PID 97897 remained alive for 37 samples with
`-noload EtherCATWorkbench` and Workbench absent in all 37. LLDB passed the
intentional SIGTERM through and both targets recorded status 15. From
2026-07-22 23:34:30 to 23:36:52 +0800 there was no residual qualification
process, new Embed Labs diagnostic report, or matching crash-service event.
Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, disabled
crash reporting, `-no-crashcheck`, and the process-local Touch Bar bypass kept
main-program acceptance invisible and non-interrupting. The disabled run's
known shared-memory initialization message was non-fatal.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-mock-value-refresh-001.Z9NXQ5`. The issue
changes only private `coeonlinepage.cpp/.h`, the existing Workbench test
declaration and implementation, and these four documents. It adds no generic
model or cache service, public API or role, source file, dependency, Project
format or persistence field, Provider or ProjectService contract, Project
command, thread, timer, Core or ProjectExplorer hook, application bootstrap,
network, ADS, scan, online CoE, SDO, controller, or hardware behavior. No
CMake or qbs description changed, so qbs was not run.

## Process Data non-conflicting refresh inline-draft continuity

`ISSUE-WB-PROCESS-DATA-NONCONFLICTING-REFRESH-DRAFT-001`, based on local
commit `31b4fb48bd564caa3ad5bd2a0947aa5e9800b38d`, keeps one active PDO Content
editor usable while fresh authority is rendered for the same Project,
configured slave, selected PDO, and entry. Preservation requires an editable
page, unique non-null PDO and entry IDs, unchanged authority for the edited
field, unchanged stored-versus-ESI-proposal source, and continued mapping
support. Index, Subindex, Bits, Bit Offset, Name, and Type each compare only
their own stored authority.

The private table model synchronizes current entries with fresh entries by
stable ID using row removal, insertion, and movement notifications. Sibling
cells and rows render current Project or Repository authority, while
`dataChanged()` deliberately excludes the active cell until the editor is
committed or destroyed. Qt item views otherwise reload an open editor for any
`dataChanged()` range covering its index, regardless of the supplied roles.
This keeps the same editor widget and therefore its text, modified state,
focus, selection, cursor, and line-edit Undo history across eligible refreshes.
When a sibling layout change alters an automatic Bit Offset display, the
deferred post-editor notification includes `DisplayRole` as well as the
accessibility and tooltip roles, so the closed cell renders its new
`Auto (N)` value without reloading the active editor.

Return commits through the existing checked `ProjectService` path. A private
commit guard allows the synchronous stored-source transition caused by the
editor's own first commit and includes the active cell in the committed
notification. The candidate is located by stable PDO and entry IDs rather
than a stale row, so a fresh sibling inserted before the editor is retained.
The initial clean-to-dirty Project transition currently publishes two
identical `projectChanged` snapshots through the existing Project document;
the test proves they carry the same stored configuration and that exactly one
Undo step returns to the unstored ESI proposal. This issue does not change the
Project document notification contract.

An external change to the edited field, a fixed or read-only transition,
missing or duplicate IDs, a removed PDO or entry, a different Project or node,
an unrelated source transition, or page removal discards the transient editor
with revert semantics and renders current authority. Ordinary tab, mode, or
window hiding does not deliberately discard the draft while the page remains
in its Details stack. Page destruction synchronously releases an editor left
queued for deferred deletion, so teardown cannot commit pending text or leave
an editor behind. There is no cross-node cache, and persistent editors opened
through `openPersistentEditor()` are not part of this boundary.

The behavior follows Qt's
[model reset contract](https://doc.qt.io/qt-6.8/qabstractitemmodel.html#beginResetModel),
[item-view reset contract](https://doc.qt.io/qt-6.8/qabstractitemview.html#reset),
[persistent-index contract](https://doc.qt.io/qt-6.8/qpersistentmodelindex.html),
[row-move contract](https://doc.qt.io/qt-6.8/qabstractitemmodel.html#beginMoveRows),
and [line-edit text contract](https://doc.qt.io/qt-6.8/qlineedit.html#text-prop).
Beckhoff's
[Process Data page](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html)
is used only for the Sync Manager, PDO, entry, and fixed-mapping interaction
comparison. It does not define this local Qt editor-continuity behavior.

Failure-first changed only the new test while production
`processdatapage.cpp/.h` retained SHA-256 values
`fdefc48d56337bb156feac5152cea699d34872337d8b831aaf3739700f409d0e` and
`55c4cf8b5978f3948fdc42eed8658b5791def9fda57a99cfb79902aa15562bcc`,
with baseline blobs `4812288c373b047ff36174863b30daa986471c41` and
`ad62cb47a8d5c7d92b3057a6577c31d5f901a82a`. The old model emitted three
resets instead of zero during the real Repository rebuild and exited 1.

Independent review then exposed a derived display-notification defect while
the intermediate implementation/header SHA-256 values were
`02de2a77eddf28340038366d94cda6b50350f1efd05302d522b1bd3ff1f80fc4` and
`ab7d21d3820117780abc31c232e99b7541fd38138378d4ab2fce4deffdf786ad`.
After a prefix Entry changed the automatic offset while its editor was open,
Escape closed the editor but no covering `DisplayRole` notification was
observed; the target exited 1.

Final implementation/header SHA-256 values are
`229c2660bddc89c6bf7d5cafb481fb6a646ec672ea777cc915421aa9402ad955` and
`ab7d21d3820117780abc31c232e99b7541fd38138378d4ab2fce4deffdf786ad`;
their git blobs are `f4e3bf0c571e72607c4a3ecd88fb6541dbb3b275` and
`dc86720292c451a968b14554b8618513b5f1938d`.

Final focused runs passed three events at normal and 2x scale. Process
Data-related runs passed eight events at both scales. Complete Workbench runs
passed 76 events at both scales. The isolated suites passed 127 events: Core
17, Project 12, Devices 8, Workbench 76, Scan 7, and Diagnostics 7. Every
target exited 0. Complete Workbench retains the known pre-existing
ProjectExplorer TaskHub soft assertion in the invalid-project path; it did not
fail a test or target.

The `WITH_TESTS=OFF` product Workbench target built successfully and the
bundle contains 16 plugin dylibs. The executable SHA-256 is
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`;
the product and test Workbench plugin SHA-256 values are
`61f62e15fd6718c982dccae52a02f81ddb8186e7be568757dec10dd2f4ec5af0`
and
`add102b915212c371528217f3867a61409a5cdc889e9d452196f9bcb16b9e3d5`.

Enabled PID 334 remained alive for 37 samples with Workbench mapped in all
37. Explicitly disabled PID 1849 remained alive for 37 samples with
`-noload EtherCATWorkbench` and Workbench absent in all 37. LLDB passed the
intentional SIGTERM through and both targets recorded status 15. From
2026-07-22 22:51:29 to 22:53:50 +0800 there was no residual qualification
process, new matching DiagnosticReports file, or matching crash-service event.
Fresh HOME/settings, cleared inherited DYLD variables,
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`,
and the process-local Touch Bar bypass kept the complete main-program runtime
acceptance invisible and non-interrupting. No visible/manual UI inspection
was run.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-process-data-draft-001.lx7VcX`. The issue changes
only private `processdatapage.cpp/.h`, the existing Workbench test declaration
and implementation, and these four documents. It adds no generic editor or
table service, public API, source file, dependency, Project format,
persistence field, Provider or ProjectService contract, Project command,
public model role, thread, timer, Core or ProjectExplorer hook, application
bootstrap change, network, ADS, scan, online state, CoE/SDO execution, PLC,
controller, or hardware behavior. Numeric editor paths are retained but the
new direct editor test covers Name, Type, and automatic Bit Offset; Index,
Subindex, and Bits are not repeated editor-by-editor. Structural preservation
is directly exercised with sibling insertion, not a complete remove/move
matrix. Same-context fixed/read-only, missing/duplicate-ID, and removed
PDO/entry transitions are implementation gate paths rather than direct test
cases; the direct read-only evidence is the context switch to a derived page.
Modules and Channels remain a separate data/API chain.
`EtherCATWorkbenchPlugin` remains In progress. No CMake or qbs description
changed, so qbs was not run.

## CoE Mock-value edit-rejection feedback

`ISSUE-WB-COE-MOCK-EDIT-REJECTION-FEEDBACK-001`, based on local baseline
`8a0373032093f05ba57af57d70a8cb1bfae404e4`, makes a rejected CoE Mock
Value submission visible without changing the accepted value. A cpp-local
validation result is shared by `CoeObjectModel::setData()` and the checked
delegate Return path, so empty input, an incomplete final byte, illegal hex,
and a fixed-width mismatch receive distinct address-specific guidance. A
rejected Return closes the transient editor and leaves the prior bytes and
Project state intact; it does not emit a source or proxy data change.

The page reuses its private CoE operation-feedback surface. Reopening the same
object and then pressing Escape retains the current rejection guidance. A
valid Return, explicit object selection change, Mock/Offline source change,
Update List, context change, Repository refresh, or page teardown clears it.
Valid input still accepts the existing optional `0x` prefix and embedded
spaces, underscores, and colons, and an object with an empty offline baseline
still accepts a non-empty complete byte sequence of arbitrary width. A valid
submission commits once through the source model, reaches the proxy once, and
does not write the Project or create an Undo command.

The visible feedback has a stable accessible name and current description and
tooltip. When Qt accessibility support is enabled, the page also emits a
polite `QAccessibleAnnouncementEvent`; the guarded test covers that event
contract but does not claim manual VoiceOver validation or audible speech.
This follows Qt's documented
[model edit result](https://doc.qt.io/qt-6/qabstractitemmodel.html#setData)
and
[delegate submission](https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData)
contracts. Qt documents the optional announcement event at
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's
[CoE Online page](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html)
is used only for Value, RW/RO, Offline-value, and Update List terminology; it
does not define this local validation wording or accessibility behavior.

Failure-first changed only the real-editor test while production
`coeonlinepage.cpp` retained SHA-256
`7f0c8d977575d87f7ad08c6a94fcd0708f03c2d59f5e6891aa719828c4688d15`
and git blob `8351fff476b0edfed51a09f11c9185dfe81f9c7c`, and
`coeonlinepage.h` retained SHA-256
`8efa640a8cf8fad46d396dd22269c7c1a5ea68a448179cd67745e6cb1fc433e2`
and blob `ee362007d7152e33df3eb8e10f9655a563ab200f`. Initialization and
cleanup passed, but the old delegate closed after invalid Return with no
visible feedback; the accepted `6060:00` bytes remained `08`, and Project,
Undo, and Redo state remained unchanged. The authoritative focused target
therefore reported two passes, one expected failure, and status 1.

The earlier `failure-first/build.log` was superseded because its test assumed
`Utils::InfoLabel` supplied its own `Q_OBJECT`; `failure-first/build-valid.log`
precedes the authoritative red test. `green/build-expanded.log` records a
superseded test-only QStringBuilder compile error. The first green build
command created no log because its tee directory did not yet exist, even
though the target built; `green/build-first-valid.log` records the valid build
result. No superseded attempt is counted as qualification evidence.

Final focused runs at 1x and 2x scaling each passed three events. Related CoE
runs at both scales each passed 11 events. Four complete Workbench runs each
passed 79 events. The six isolated EtherCAT suites passed 130 events: Core
17, Project 12, Devices 8, Workbench 79, Scan 7, and Diagnostics 7. Every
final test target exited 0. Complete Workbench still reports the known
pre-existing ProjectExplorer TaskHub soft assertion in its invalid-project
path, and the rejection test deliberately triggers warnings while attempting
to edit non-editable cells; neither condition failed a test or target.

Final production implementation/header SHA-256 values are
`2af4e22ce91f194055698817f3c3504caf2ae28696ed3425fd52ed86e6799454`
and
`72d57a183c1bb8455b9b823a4ea1c1b838ac037e19cc3bdb9224083c43f38d62`,
with git blobs `4cc42ba6a5c491cd29ebe4b6b7154aaba04a0be3` and
`5c677b2f40018f430e8c454fe6ed7b62b77bbc1d`. Test implementation/header
SHA-256 values are
`f0c53efc78c78d66a5e9631fea47fded4c022139de36fe102a5719ec9743d800`
and
`bbb38750f9c380f07ed285633c690a3e917af2afde1dccea91acc254c9db8c24`,
with blobs `61c554e8c9f32fcd44054bd7d47e047dc9122345` and
`ced27e5b83c9c0031ce62b409e710141f56a1a81`.

The `WITH_TESTS=OFF` product Workbench target and complete product build both
passed, and the bundle contains exactly 16 plugin dylibs. The executable,
product Workbench, and test Workbench SHA-256 values are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`d6c5becbcdae5850f8238ec8bb873c35e9a5dafe71ed634e2475aa7abd3b551e`,
and
`cd18428efb63c8ea668287dee1c1cc045a1a420fea3f7e622ccbf435a45f8abd`.
Enabled PID 36133 remained alive for 37 of 37 samples with Workbench loaded in
all 37; explicit `-noload EtherCATWorkbench` PID 37563 remained alive for 37
of 37 samples with Workbench loaded in zero samples. Passed-through
intentional SIGTERM ended both runs with expected target status 15. The audit
from
2026-07-23 01:23:54 to 01:26:15 +0800 found zero residual qualification
processes, new matching diagnostic reports, or matching crash-service events.
Fresh HOME/settings, offscreen Qt, disabled crash reporting, and the
process-local launch safeguards kept main-program acceptance invisible and
non-interrupting; no visible/manual UI inspection was run.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-edit-feedback-001.vl5Suz`. The issue changes
only private CoE page/model/delegate behavior, the existing Workbench test
declaration and implementation, and these four documents. It adds no public
API or role, source file, dependency, Project format, persistence field,
Provider or ProjectService contract, Project command, Modules or Channels
data chain, Core or ProjectExplorer hook, application bootstrap, production
thread or timer, network, ADS, scan, online CoE, SDO, controller, PLC, or
hardware behavior. No CMake or qbs description changed, so qbs was not run.
`EtherCATWorkbenchPlugin` remains In progress.

## Visible EtherCAT identity filtering

`ISSUE-WB-NAV-FILTER-VISIBLE-IDENTITY-001`, based on local baseline
`c6f48495263f38a292a780f0955468bf183c692a`, makes the navigation filter
accept the hexadecimal identity text already shown for Repository devices.
Pasting `0x0000102a` from a device tooltip now keeps that device and its
Repository ancestor visible instead of showing the no-match page. Vendor,
Product, and Revision use the same zero-padded `0x........` representation in
the private search role and the visible tooltip. The complete legacy
Vendor/Product/Revision/group segment remains unchanged ahead of the new
tokens, so both single-field and compound unprefixed searches stay compatible.

The filter remains read-only navigation state. It does not change the stable
selection, Project context, offline data, Undo stack, Repository contents, or
Mock state. The existing `QSortFilterProxyModel` still owns fixed-string,
case-insensitive, recursive matching; this change only aligns the source
model's existing private `SearchTextRole` with text users can see and copy.
Qt documents that a proxy filter reads its configured role and that recursive
filtering keeps matching descendants' ancestors visible at
<https://doc.qt.io/qt-6.11/qsortfilterproxymodel.html>. Beckhoff defines
Vendor ID, Product Code, and Revision Number as EtherCAT slave identity
fields at
<https://infosys.beckhoff.com/content/1033/tcplclib_tc2_ethercat/57119371.html>;
that reference defines the field meaning, not this local filter interaction.

Failure-first changed only the new Workbench test while production
`workbenchtreemodel.cpp` retained SHA-256
`3211b55cffb8762f82f22d78e340ce517595b2e39257e038cdd2e9274a0947aa`
and git blob `aa23ee6dfe4e8b4cc6c7b996250592521f3b5c97`. Initialization and cleanup
passed, but filtering with the tooltip's exact `0x0000102a` text hid the
device; the target therefore recorded two passes, one expected failure, and
status 1. The final implementation retains the complete legacy identity/group
segment and appends the three visible `0x` identity tokens.

Final focused runs at 1x and 2x each passed three events. Navigation-related
runs at both scales each passed six events. Four complete Workbench runs each
passed 80 events. The six isolated EtherCAT suites passed 131 events: Core
17, Project 12, Devices 8, Workbench 80, Scan 7, and Diagnostics 7. Every
final test target exited 0. Complete Workbench retains the known pre-existing
ProjectExplorer TaskHub category soft assertion in the invalid-project path;
it did not fail a test or target. Final implementation/test/test-header
SHA-256 values are
`96d94e79cb5c938486ac9e81da0d7776614d6e2eb38141ed14a9f3f7fb9c7e71`,
`47e92628cce83e4ebc0874e4d5882ad112b397b1fa45e6fa0ce0a762fbdc481c`,
and
`16070acc603d9394dd1c49fb38c333ce2b6d6e7e15335c4e9bbca08a35ddbf39`;
their git blobs are `042a51ec04dd84371cde46de18c7e6f98e00f348`,
`0daf1f517d8256e193dd372f6bf82767ab72071b`, and
`8f2940e28febba3cfa5bdafde2d289c6fab24f92`.

Test qualification used Qt 6.11.0 Release in
`qt-creator-build-ethercat-core-qt611`; product qualification used
`qt-creator-build-ethercat-product-qt611`.
The `WITH_TESTS=OFF` Workbench target and complete product build passed, and
the bundle still contains exactly 16 plugin dylibs. Executable, product
Workbench, and test Workbench SHA-256 values are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`2d31365c7181be49486199c403117a6e439fcd6f018bf6a68bad600facb6ae45`,
and
`74b736da82d453ab68effd574c56b7e4a37cf27fdd53d206b1bbf308b531bff2`.
Enabled and explicit `-noload EtherCATWorkbench` product runs each remained
alive for 37 of 37 samples; Workbench was mapped in 37 and 0 samples
respectively. Both ended by intentional passed-through SIGTERM with expected
status 15. The audit from 2026-07-23 02:04:44 to 02:07:05 +0800 found zero
residual qualification processes, new matching diagnostic reports, or
matching crash-service events. All execution used fresh HOME/settings,
offscreen Qt, cleared inherited DYLD variables, disabled crash reporting,
`-no-crashcheck`, and a process-local Touch Bar bypass; no visible/manual UI
inspection was run. The explicit-disabled launch emitted the known non-fatal
`QSharedMemory::handle: doesn't exist` initialization message, then remained
alive for all 37 samples and ended with the expected status 15.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-nav-visible-identity-001.2G4bre`. The issue changes
only private Workbench search text, its test declaration/implementation, and
these four documents. It adds no public API or model role, source file,
dependency, Project format, persistence field, Provider or ProjectService
contract, Project command, Core or ProjectExplorer hook, application
bootstrap, thread, timer, network, ADS, scan, online state, CoE/SDO, PLC,
controller, or hardware behavior. No CMake or qbs description changed, so qbs
was not run. `EtherCATWorkbenchPlugin` remains In progress.

## Blank-area mouse context menu

`ISSUE-WB-NAV-MOUSE-BLANK-CONTEXT-001`, based on local baseline
`142225dc80ec87032f110d96e4408f425477ff65`, prevents a mouse request on
unused tree space from borrowing the previously selected node's menu. The old
handler changed the current index only when `QTreeView::indexAt()` returned a
valid row, then always built the menu from `currentIndex()`. Consequently, a
blank-area right click after selecting a Master could still expose Insert
Device and Copy Node ID; a configured slave could similarly leak its remove
and move commands into empty space.

The private navigation widget now keeps a separate menu-context index. A valid
mouse hit retains the existing behavior and synchronizes both the tree and
stable `SelectionService` target. An invalid point inside the viewport builds
an empty context while preserving the real current index and stable selection.
Expand, Collapse, and other tree-wide commands remain available; Insert, Add,
Remove, Move, Set Active Project, and Copy are absent and temporarily disabled.
Closing the menu restores every command to the state derived from the current
stable selection. Keyboard context events still use the current row. The
existing internal `customContextMenuRequested(QPoint(-1, -1))` convention is
also preserved because its sentinel lies outside the viewport.

Qt documents `indexAt()` as the item-view hit test and separately exposes the
current index at <https://doc.qt.io/qt-6/qabstractitemview.html#indexAt> and
<https://doc.qt.io/qt-6/qabstractitemview.html#currentIndex-prop>. The local
Qt Creator Project tree likewise passes a null node when a mouse point does
not hit an index. Beckhoff documents Add New Item from the selected Devices
container or EtherCAT device context at
<https://infosys.beckhoff.com/content/1033/xts_software/11342363403.html> and
<https://infosys.beckhoff.com/content/1033/epioconfiguration/6519655307.html>.
Those references define interaction context only; no Beckhoff code, format,
asset, branding, communication stack, or hardware behavior is copied.

Failure-first changed only the existing Workbench test. Production
`workbenchnavigation.cpp` retained SHA-256
`84a9aa508d876906a88ff349ef0bd0406bf60f4d6ba7090380d1b834d5ce4b4c`
and git blob `8a2c6fc675520a9216815c72f0df6f753d5c5a46`. A real mouse
`QContextMenuEvent` targeted a point proved to be inside the viewport with an
invalid `indexAt()` result. The old implementation placed a node-specific
action in that menu, so the focused target passed initialization and cleanup,
failed the behavior test, and exited with status 1.

The final test retains the existing real keyboard and valid-row mouse cases,
then checks a real blank-area mouse request at normal and 2x scale. It verifies
that the current tree index and stable node do not drift, generic tree actions
remain, all seven node-specific commands are absent and disabled while the
menu is open, and their enabled states are restored exactly after close. The
view-edge point is derived from the first invalid `indexAt()` result rather
than a hard-coded coordinate; menu placement is left to the window system.

Authoritative focused runs passed 3 events at each scale. The related
navigation/menu/lifecycle group passed 12 events at each scale. Two complete
Workbench runs at each scale passed 80 events per run. Six isolated plugin
suites passed 131 events: Core 17, Project 12, Devices 8, Workbench 80, Scan 7,
and Diagnostics 7. Every authoritative target exited 0. Complete Workbench
retains the known pre-existing ProjectExplorer TaskHub category soft assertion
in its invalid-project path; it did not fail a test or target.

Final SHA-256 values for the navigation implementation, Workbench tests, and
unchanged test header are
`be3fb065bc0b9351314eb98ac4e315482159a28e6527d121c1958fc398479da3`,
`9deeac93fd33988f17be1fde20fe51b8ca794fc3309e113c19cae93fa7f07ad4`,
and
`16070acc603d9394dd1c49fb38c333ce2b6d6e7e15335c4e9bbca08a35ddbf39`;
their git blobs are `cd0ce1ad326832165cf99abf7ed788ee68289f78`,
`b92893fbd3bd1a890caaaa0b4f57f2c1e51c9dc3`, and
`8f2940e28febba3cfa5bdafde2d289c6fab24f92`.

Qualification used Qt 6.11.0 Release in
`qt-creator-build-ethercat-core-qt611`. The `WITH_TESTS=OFF` Workbench target
and complete product build passed in `qt-creator-build-ethercat-product-qt611`,
whose bundle contains exactly 16 plugin dylibs. Executable, product Workbench,
and test Workbench SHA-256 values are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`b5d6e148b3855adfcea5a835464e90539d848e4469259c91420c29c6cdb55373`,
and
`081f8dbef2464f8f5de1dd5f3a53170d658e2281c597a2a01c71b355d87dd003`.

Enabled and explicit `-noload EtherCATWorkbench` product runs each remained
alive for 37 of 37 samples; Workbench was mapped in 37 and 0 samples
respectively. Both ended by intentional passed-through SIGTERM with expected
status 15. The audit from 2026-07-23 02:42:29 to 02:44:52 +0800 found no
residual qualification process, new matching DiagnosticReports file, or
matching crash-service event. Fresh HOME/settings, offscreen Qt, cleared
inherited DYLD variables, disabled crash reporting, `-no-crashcheck`, and a
process-local Touch Bar bypass kept both runs invisible and non-interrupting.
The explicitly disabled run emitted the known non-fatal shared-memory message,
then stayed alive for every sample and passed the audit.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-nav-blank-context-001.oYIrwR`. This issue changes
only private navigation behavior, its existing Workbench test, and these four
documents. It adds no public API or model role, source file, dependency,
Project or Repository mutation, Project format, persistence field, Provider
or ProjectService contract, Project command, Core or ProjectExplorer hook,
application bootstrap, production thread or timer, network, ADS, scan, online
state, CoE/SDO, PLC, controller, or hardware behavior. No CMake or qbs
description changed, so qbs was not run. The unrelated `WITH_TESTS=ON`
all-target build was not rerun because the known EasyBoard
`extensionmanager_test.h` blocker remains outside this issue.

## General name rejection feedback

`ISSUE-WB-GENERAL-RENAME-REJECTION-FEEDBACK-001`, based on local baseline
`6d13931b6d595b4c710227dc0aa7fa0a4584c104`, makes rejected General-page
name edits visible where they are made. Project, offline Target, Master, and
configured Slave already rejected an empty name and restored the accepted
value, but the reason was written only as a flashing General Messages entry.
The Details page itself provided no explanation, so Return appeared to do
nothing.

The private `GeneralPage` now owns one error `Utils::InfoLabel` below its
summary. A rejected Return still uses the existing Workbench controller and
Project-service paths as the validation and mutation authorities. The page
records the existing contextual error, reloads its authoritative context, and
only then shows the feedback, so the reload cannot erase it. The accepted
name, complete Project snapshot,
Undo/Redo state, and stable selection remain unchanged. The four messages are:

- `Cannot rename the EtherCAT project: Project name cannot be empty.`
- `Cannot rename the offline target: EtherCAT target and master names cannot
  be empty.`
- `Cannot rename the EtherCAT master: EtherCAT target and master names cannot
  be empty.`
- `Cannot rename the offline slave: Offline slave name cannot be empty.`

New user input clears the complete feedback state, and a successful path
remains clear. Same-context refresh, context switch, Project close, or page
destruction also clears or destroys it, so an error cannot leak to another
object. The existing General Messages write remains for diagnostic
continuity. No validation rule is duplicated in the page.

The label has the stable accessible name `General name edit feedback`; its
current error is also exposed through accessible description, normal tooltip,
and additional tooltip. When Qt accessibility support is available, rejection
requests one polite `QAccessibleAnnouncementEvent`. Qt defines that event as a
request to assistive technology, not a guarantee of audible speech, at
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. The QWidget
accessible-description contract is at
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. No manual
VoiceOver or audible-speech claim is made.

Beckhoff documents the Project name on the Project tab at
<https://infosys.beckhoff.com/content/1033/tc3_userinterface/3434440203.html>,
the EtherCAT Master name reflected in the tree at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1569945995.html>, and
the Slave General name at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html>.
These references anchor only familiar Project/Master/Slave General-page and
tree terminology. The local offline Target and all rejection-feedback,
validation, accessibility, and lifecycle behavior remain Embed Labs code.

Failure-first changed only the Workbench test declaration and implementation.
Production `generalpage.cpp` retained SHA-256
`51195c26f5db3936f2e0ba0935d2564d3b8a0bf2ca95c86328de7d39e6ee16e6`
and git blob `e2cce8dd0a1fe6f5837de15492ead898246bffb8`;
`generalpage.h` retained SHA-256
`93760569091e384acb8a68451b8c3ae8c2f6ce2f744bbcab700780bc1423aaca`
and blob `051abe8c5e068d59351cdd8ea7ceba13607f2558`. The authoritative
red run passed initialization and cleanup, failed because the page had no
feedback widget, and exited 1.

Two earlier harness attempts are not failure-first evidence: a typed
`findChild<Utils::InfoLabel *>` did not compile because `InfoLabel` has no
`Q_OBJECT`, and an incorrectly formed `-test` selector was parsed as a plugin
name and exited 255. Both were superseded by the compiling object-name lookup
and correct Workbench selector above. A first parallel isolation wrapper also
used zsh's reserved `status` name and was replaced with `result`. Its first
Scan retry emitted all seven pass events but its LLDB exit handshake stalled;
only that qualification chain was terminated. The final sequential Scan run
passed seven events and exited 0.

The final integrated test uses real `QLineEdit` focus, key input, and Return
for all four contexts. It verifies exact restored names and messages, zero
Project change signal, complete Project snapshot equality, unchanged
Undo/Redo and selection, visible error metadata, exactly one polite
announcement, same-context clearing, edit clearing, a successful path that
remains clear, context-switch clearing or destruction, and Project-close
destruction. Focused normal and 2x runs passed 3 events each. The related
General/context lifecycle group
passed 10 events at each scale. Two complete Workbench runs at each scale
passed 81 events per run. Six isolated suites passed 132 events: Core 17,
Project 12, Devices 8, Workbench 81, Scan 7, and Diagnostics 7. Every
authoritative target exited 0.

Final SHA-256 values for `generalpage.cpp`, `generalpage.h`, the Workbench
tests, and test header are
`ec0c1b04f6600b7e220d2ba49f52211d32fc55e9419daee24453db43bb93bbb6`,
`e2d7e95eb78f39e758afe5efe599627d1e48888bd877e85cd9abbd17936a0026`,
`af57ecdbd2858beb8cb1d834627b8c8eaa655a67c0621f594b11a8fc27e78dbf`,
and
`4352a7df522ecbb2deb147ea15234070b025b7b310cc480807698a5abb08acbb`;
their git blobs are `74b7a4e1daeb01026eb1eb696e4a46d5d7eb20dd`,
`4c77773da4de1e45d5c81ae179ce4e9ce9a368bd`,
`e431f9014d90eeba9c48b2e915ba502ad434b89e`, and
`3839b41791da3d01e58482dde078e00811e88a03`.

Qualification used Qt 6.11.0 Release in
`qt-creator-build-ethercat-core-qt611`. The `WITH_TESTS=OFF` Workbench target
and complete product build passed in
`qt-creator-build-ethercat-product-qt611`, whose bundle contains exactly 16
plugin dylibs. Executable, product Workbench, and test Workbench SHA-256 values
are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`cdc247778382d9ffa7ec655aa1538011d8184bae5ad9403450baa0109562f8e6`,
and
`98459d3f9c915105deef5080935ac481a56c31f4bd4d1185c1ee362697d9a1b2`.

Enabled and explicit `-noload EtherCATWorkbench` product runs each remained
alive for 37 of 37 samples; Workbench was mapped in 37 and 0 samples
respectively. Both ended by intentional passed-through SIGTERM with expected
status 15. Fresh HOME/settings, offscreen Qt, disabled crash reporting,
`-no-crashcheck`, cleared inherited DYLD variables, and a process-local Touch
Bar bypass kept acceptance invisible and non-interrupting. The disabled run's
known non-fatal shared-memory initialization message did not affect any
sample. The 2026-07-23 03:23:45 to 03:26:10 +0800 audit found zero residual
qualification processes. A fail-closed repeat of the same system-log window
verified query status 0 and its standard header, with zero matching crash-
service events; readable before/after DiagnosticReports snapshots each had
166 entries and their explicit difference contained no Embed Labs report.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-general-rename-feedback-001.M4aWr4`. This issue
changes only private General-page UI, its existing Workbench test declaration
and implementation, and these four documents. It adds no public API or model
role, source file, dependency, Project format or persistence field,
Provider/ProjectService contract, Project command, Core or ProjectExplorer
hook, application bootstrap, production thread or timer, network, ADS, scan,
online state, CoE/SDO, PLC, controller, or hardware behavior. No visible or
manual UI inspection, remote comparison, fetch, pull, merge, rebase, push, PR,
or publication is part of this issue. No CMake or qbs description changed, so
qbs was not run.

## Mock Scan owning-Project lifecycle cleanup

`ISSUE-WB-SCAN-PROJECT-LIFECYCLE-001`, based on local baseline
`e1855127275d89914626a632674e08cb88109d98`, prevents a Mock scan from
outliving the Project that owns its request. Previously, closing a Project
after a completed scan left the Provider in `Completed`. Compare, Accept, and
Keep Existing could remain enabled, the mode status could continue to report
the old Mock result, and reopening the same file could project the stale
stable IDs back into the Workbench tree.

`EtherCATScan` owns the correction. Its private `MockScanProvider` compares
`projectAboutToBeRemoved` with the current `ScanRequest::projectId`. An
unrelated Project close is ignored. A matching close first cancels active
work, then reuses the existing clear path so the request, timer, progress,
result, and error converge to `Idle`. Existing public Provider signals refresh
the shared commands, `StateService`, Mock Scan property page, and Workbench
projection; no Workbench production source was changed.

The user-visible result is deterministic for Active, Completed, Failed, and
Cancelled states. An unrelated Project can close without changing the current
scan or emitting a Provider state, result, or finished signal. Closing the
owner while another Project remains open clears the difference table, disables
Compare, Accept, Keep Existing, and Cancel, and removes the Scan status entry.
An active close emits `Cancelled` before final `Idle` and stops the timer. A
same-ID reopen starts clean and can complete a new explicit Mock scan. Scan
state never mutates Project configuration or creates Undo/Redo history.

Qt documents context-bound signal delivery at
<https://doc.qt.io/qt-6/qobject.html#connect>, timer stopping at
<https://doc.qt.io/qt-6/qtimer.html#stop>, and ActionManager's user-visible
command-state reflection at
<https://doc.qt.io/qtcreator-extending/actionmanager.html>. Beckhoff's
documented scan/offline comparison is referenced only for familiar workflow
terminology:
<https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10832129675.html>.
The owning-Project invalidation rule is local Embed Labs behavior; it does not
claim TwinCAT compatibility beyond that terminology.

Failure-first changed only the Scan test declaration and implementation.
Production `mockscanprovider.cpp`, its header, and `scanworkflow.cpp` retained
SHA-256 values
`aa1e691cf024e36342add1a4d5c83158349d45b66e9f218af26b3a4980f23ff8`,
`e7339e300db0ac1d58ea7cf7b6a4481da1a4913604148ebaf0f718d6863aef27`,
and
`de519d6a76912c71ddc5ac710b8e5906a3737f8a89b318775db569bf0eff6b10`.
Initialization and cleanup passed; after the owner closed, the old provider
remained `Completed` (`6`) instead of `Idle` (`0`), and the target exited 1.
An earlier attempt to instantiate private Workbench implementation classes in
the Scan binary failed to link and is not red evidence. A later full-snapshot
equality check across Project format migration was replaced by direct
configuration, stable-ID, and Undo/Redo assertions; it is not counted as a
product failure.

The final data-driven test covers all four lifecycle states with two real open
Projects per row, required state and finished-signal semantics, command and
status state, the
difference page, same-ID reopen, a successful new scan, and Project isolation.
Focused normal/2x runs each passed 6 events; related Scan runs each passed 9.
Two complete Workbench runs at each scale passed 81 events per run. The six
isolated suites passed 136 events: Core 17, Project 12, Devices 8, Workbench
81, Scan 11, and Diagnostics 7; every authoritative target exited 0.

Final SHA-256 values for `mockscanprovider.cpp`, Scan tests, and test header are
`61d83c8409f0a9aded41507a77036fb9128307bfde9beafeb14b16b18a8d48ad`,
`93aac13f59ec72d173b8a7e3dcf32c7fcc8fb6b45295bfd0d7fb3c3d398c07f7`,
and
`985582e5552d25464863bc2191b90e7820d1a374e02f28e637f562b2a5b3c6e3`;
their git blobs are `e9ea00afa5470abaca43854ffa1057c4537db8c1`,
`11d5e1ee9e5a89f63bea46fd743068b4bd5caa32`, and
`484c4b973c540e497b4734d33f1033eae09c59d6`.

Qualification used Qt 6.11.0 Release in
`qt-creator-build-ethercat-core-qt611`. The `WITH_TESTS=OFF` Scan target and
complete product passed in `qt-creator-build-ethercat-product-qt611`, whose
bundle contains exactly 16 plugin dylibs. Executable, product Scan, and test
Scan SHA-256 values are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`e2c4c797283a9cb5f345bc6533214541a1217cf6f5194fc7a422be737699b274`,
and
`0b4140211077357d0894335af00027604ea7a7bfc539b5ebe2f67e4be5119f63`.

Invisible enabled and explicit `-noload EtherCATWorkbench` product runs each
stayed alive for 37/37 samples. Workbench and Scan were mapped in 37/37
enabled samples and 0/37 disabled samples. Both ended by intentional
passed-through SIGTERM with expected status 15. The fail-closed audit from
2026-07-23 04:14:08 to 04:16:34 +0800 found no residual qualification
process, new Embed Labs DiagnosticReports file, or matching crash-service
event. Fresh HOME/settings, offscreen Qt, disabled crash reporting,
`-no-crashcheck`, cleared inherited DYLD variables, and the process-local
Touch Bar bypass kept acceptance invisible and non-interrupting.
The explicit-disabled run emitted the known non-fatal shared-memory
initialization message, remained alive for all samples, and produced no crash
artifact or service event.

Authoritative evidence is under
`/private/tmp/embed-labs-wb-scan-project-lifecycle-001.9fVQNz`. This issue
changes one private Scan implementation, its existing test declaration and
implementation, the four required evidence documents, and the directly
related Scan document. It adds no public API or model role, source file,
dependency, Project format or command, persistence field, Core or
ProjectExplorer hook, application bootstrap, production thread, network,
ADS, online scan, CoE/SDO, PLC, controller, Zynq, or hardware behavior. No
CMake or qbs description changed, so qbs was not run. No visible/manual UI
inspection, remote comparison, fetch, pull, merge, rebase, push, PR, or
publication was performed.

## Process Data inline-edit rejection feedback

`ISSUE-WB-PROCESS-DATA-EDIT-REJECTION-FEEDBACK-001`, based on local baseline
`037bd681e7ce17bd665d345f83a64b49a28a2428`, explains why a PDO Content edit
was not applied. Previously, the real editor could close and restore the
accepted value while the validation strip still said that the configuration
was valid. The private Process Data page now reports these candidate errors:

- Index must be a decimal or `0x`-prefixed hexadecimal integer from 1 to
  65535;
- Subindex must use the same number formats and remain from 0 to 255;
- Bits must be a positive integer;
- Bit Offset must be `Auto` or an integer greater than or equal to -1;
- Name cannot be empty.

A parsed candidate that fails complete Process Data validation reports the
first domain error with the same `Change not applied.` prefix. For example,
changing a 16-bit `UINT` entry to 8 bits reports the bit-length/type mismatch.
Read-only and Project-service rejection strings are also routed through the
same delegate result path, but those defensive branches were code-reviewed
rather than fault-injected in this issue.

Feedback is deliberately a real-editor behavior. `PdoContentTableModel`
records a private rejection reason while returning the normal `setData()`
result. `ProcessDataItemDelegate::setModelData()` consumes it after a real
delegate submission and asks the page to show it. Direct programmatic
`setData()` rejection remains presentation-silent, including while an editor
is open; valid direct writes retain their existing Project submission path.
This keeps rejected model calls from impersonating user input and preserves
the accepted Project snapshot, Undo/Redo availability, and model notifications
on every rejected candidate.

The existing validation `InfoLabel` becomes an error surface with stable
accessible name `Process Data edit feedback`; visible text, accessible
description, normal tooltip, and additional tooltip agree. Each real rejection
requests one polite `QAccessibleAnnouncementEvent`. This verifies the event
path only and is not a manual VoiceOver or audible-speech claim. Moving to a
different PDO Content cell, Sync Manager, or PDO, a successful submission,
normal context refresh, or Project close clears or destroys the transient
state. Content-cell and Sync-Manager clearing are exercised directly; the PDO
selection branch reuses the same private clear method and was code-reviewed.

Qt specifies the model return contract at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>, the void delegate
commit hook at
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and the
optional announcement event at
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Qt Creator's
`Utils::InfoLabel` and validation precedent are at
<https://code.qt.io/cgit/qt-creator/qt-creator.git/tree/src/libs/utils/infolabel.h?h=20.0>
and
<https://code.qt.io/cgit/qt-creator/qt-creator.git/tree/src/libs/utils/projectintropage.cpp?h=20.0#n193>.
Beckhoff's Process Data page is used only for PDO List/PDO Content and edit
workflow terminology:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.
It does not define these local messages or accessibility behavior.

Failure-first changed only the Workbench test declaration and implementation.
Production `processdatapage.cpp/.h` retained SHA-256 values
`229c2660bddc89c6bf7d5cafb481fb6a646ec672ea777cc915421aa9402ad955`
and
`ab7d21d3820117780abc31c232e99b7541fd38138378d4ab2fce4deffdf786ad`.
Initialization and cleanup passed, the real blank-Name Return path left the
label at `InfoLabel::Ok` instead of `Error`, and the target exited 1.

Two intermediate failures are not final green evidence. A test-only Project-
close assertion initially retained a raw label pointer after widget teardown;
it was replaced by `QPointer`. More importantly, a pre-final related run
reproduced a real `EXC_BAD_ACCESS` while destroying Startup's
`DataTypeDelegate`. LLDB with `MallocScribble` proved that Startup and Process
Data had different namespace-scope classes with the same linker-visible name,
so the Process Data class was renamed `ProcessDataTypeDelegate`. The old crash
logs are superseded by a post-fix MallocScribble run, normal/2x focused and
related runs, and all four complete Workbench runs.

Final focused normal/2x runs passed 3 events each; related Process Data runs
passed 9 each. Two complete Workbench runs at each scale passed 82 events per
run. Six isolated suites passed 137 events: Core 17, Project 12, Devices 8,
Workbench 82, Scan 11, and Diagnostics 7. Every authoritative target exited
0. Final SHA-256 values for `processdatapage.cpp`, its header, Workbench tests,
and the test header are
`00cb248c93611ff911941f2af9632d2f4abbe19a56b8c13ca6a7ac2c75507eb0`,
`a489a2cde89a7a13e993427c2ee342d6d1b4e0342d65618f06c3d5904b12bba4`,
`37311df512697533f4bdc639a760b80c26479898d1669efcac47599c82d81948`,
and
`e0860f1940a699c8c1f53cd0694b5918f7b88792f6fe27e1b08a4cdbdd1c1d70`;
their git blobs are `b38e47a0be15a0ca5c695378b5b1c1bdb5394f84`,
`e436385dc4881c138bad1522037fe58397bc75ea`,
`d5cc3ec1986c9d685bbeed29e5386914d3f88cf8`, and
`a1ed3404b855a1bf0969546293f559051448622f`.

The Qt 6.11.0 `WITH_TESTS=OFF` Workbench target and complete product build
passed with exactly 16 plugin dylibs. Executable, product Workbench, and test
Workbench SHA-256 values are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`06e77fefb9451cdae1fd1912e4cb982aeecf2365a2a99c59077d8372b60b2763`,
and
`63829550a2be2a9516737c58589af97780640c2d88595d2a0805a6ec2752dd33`.
Invisible enabled and explicit `-noload EtherCATWorkbench` product runs each
remained alive for 37/37 samples. Workbench and Scan mapped in 37/37 enabled
samples and 0/37 disabled samples; each run ended by intentional passed-through
SIGTERM with status 15. The 2026-07-23 05:08:22 to 05:10:47 +0800 audit found
zero residual process, new Embed Labs DiagnosticReports file, or matching
crash-service event.

Evidence is under
`/private/tmp/embed-labs-wb-process-data-edit-feedback-001.seBJey`. This issue
changes only private Process Data implementation, its existing Workbench test
declaration/implementation, and these four documents. It adds no public API or
model role, source file, dependency, Project format or persistence field,
Provider/ProjectService contract, Project command, Core or ProjectExplorer
hook, application bootstrap, production thread or timer, network, ADS, scan,
online state, CoE/SDO, PLC, controller, or hardware behavior. No CMake or qbs
description changed, so qbs was not run. No visible/manual UI inspection,
remote operation, fetch, pull, merge, rebase, push, PR, or publication was
performed.

## Startup inline-edit rejection feedback

`ISSUE-WB-STARTUP-INLINE-EDIT-REJECTION-FEEDBACK-001`, based on local
baseline `79b654f5cd57116e42777bb2f09559b87bc98c52`, explains a rejected
Startup table edit in the existing validation strip. Previously, entering
`0G` in the real Data editor restored `08` but incorrectly reported that the
value needed an even number of hexadecimal digits. The private page now
reports the reason produced by the actual rejected candidate:

- Data distinguishes a non-hexadecimal character, an odd digit count, and an
  empty value rejected by complete Startup validation;
- Transition explains that angle brackets are reserved for fixed ESI
  requests;
- Order, Index, and Subindex report their accepted decimal or `0x`-prefixed
  hexadecimal ranges;
- Type changes report complete-configuration size mismatches;
- a zero object Index reports the existing non-zero requirement; and
- enabling a disabled request with empty Data reports the validation error.

The table model retains only a private one-shot rejection string. The normal,
Transition, and Type delegates consume it after a real `setModelData()`
submission. The Enabled checkbox uses the same delegate boundary through
`editorEvent()`. Rejected direct programmatic `setData()` calls remain
presentation-silent and request no announcement; valid direct writes keep
their existing Project and Undo/Redo path. Rejection preserves the accepted
cell, complete Project snapshot, Undo/Redo availability, stable selection,
`projectChanged`, and model `dataChanged` state.

The existing Startup validation `Utils::InfoLabel` becomes an Error surface
with stable accessible name `Startup edit feedback`. Its visible text,
accessible description, normal tooltip, and additional tooltip agree. When
Qt accessibility is enabled, each real rejection requests one Polite
`QAccessibleAnnouncementEvent`.
Changing cells, successfully resubmitting the same cell, a same-Project
refresh, Undo/Redo, and Project close restore or destroy the transient state.
The event request is automated offscreen evidence only; no manual VoiceOver,
audible speech, or visible desktop inspection is claimed. The modal New/Edit
`StartupParameterDialog` is outside this table-inline issue.

Qt defines the model return contract at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>, the delegate commit
hook at
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and the
optional announcement event at
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's
Startup page supplies ordered-request, field, and fixed-item terminology only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.
It does not define these local messages, delegate transaction, or
accessibility behavior.

Failure-first changed only the Workbench test declaration and implementation.
Production `startuppage.cpp/.h` retained SHA-256 values
`99902dbe8615dedc25380b8bbb7117ad54da9d092ec3e12bcf97afba57c5235f`
and
`01c85b28ca3b487f425c03380e4f1e6662574512a377c05344cd6157093e6eb6`.
Initialization and cleanup passed, the exact real-editor message comparison
failed, and the target exited 1.

Final focused normal/2x runs passed 3 events each; Startup-related runs passed
9 each. Two complete Workbench runs at each scale passed 83 events per run.
Six isolated suites passed 138 events: Core 17, Project 12, Devices 8,
Workbench 83, Scan 11, and Diagnostics 7; every target exited 0. Final
SHA-256 values for `startuppage.cpp`, its header, Workbench tests, and the test
header are
`5d6bc7c41dfa8b3cef0925fba090d27960cdd44a876bb666c69ff390b894731e`,
`2c47a50f70ac55ae3a709afe38e9ca2d63e310a6eaffb1ced9fa39463a9bfdc0`,
`87d3c47b3f7bbf85fc1cdc724d70c4ffe62bb98cc9d333b9b41db1164bcd0fcc`,
and
`ab61a8eae20015b5a80d2bc832f0f27a1f05edc9d4ede201f1524f3d298bb28f`;
their git blobs are `e7c4d58d6d8fb737363444a4daff4e78f0c9f069`,
`7e7b56533ad09b3c2a70cc5423aa69f1e85c26f7`,
`5cf94b7853ea7f710aeb9690666d4a37e9010fe2`, and
`8e28aa00ce6d2cbbb71b74b20e788bd56610c3b2`.

Qt 6.11.0 Release qualification used
`qt-creator-build-ethercat-core-qt611`. The `WITH_TESTS=OFF` Workbench target
and complete product passed in `qt-creator-build-ethercat-product-qt611` with
exactly 16 plugin dylibs. Executable, product Workbench, and test Workbench
SHA-256 values are
`c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`,
`5dbc4a74f2730d496068f26806ef1c2ff53dd7a14bb1bbd224ec327db84b48aa`,
and
`a9c276d76b51e942e6d5fa5e4faed72f30e8406b89a0340abface59698ff147a`.

Invisible enabled and explicit `-noload EtherCATWorkbench` product runs each
stayed alive for 37/37 samples. Workbench mapped in 37/37 enabled samples and
0/37 disabled samples; both ended by intentional passed-through SIGTERM with
expected status 15. The 2026-07-23 06:23:25 to 06:25:48 +0800 audit found no
residual process, new Embed Labs DiagnosticReports file, or matching
ReportCrash, CrashReporter, or diagnosticd event. Fresh HOME/settings,
offscreen Qt, disabled crash reporting, cleared inherited DYLD variables,
`-no-crashcheck`, and the process-local Touch Bar bypass kept main-program
acceptance invisible and non-interrupting. Pre/post lifecycle SHA-256, mtime,
size, and Mach-O UUID manifests prove that the qualified executable and
Workbench dylib stayed byte-identical to the final product artifacts.

Evidence is under
`/private/tmp/embed-labs-wb-startup-edit-feedback-001.QUiKKW`. This issue
changes only private Startup implementation, the existing Workbench test
declaration/implementation, and these four documents. It adds no public API
or model role, source file, dependency, Project format or persistence field,
Provider/ProjectService contract, Project command, Core or ProjectExplorer
hook, application bootstrap, production thread or timer, network, ADS, scan,
online state, CoE/SDO, PLC, controller, Zynq, or hardware behavior. No CMake
or qbs description changed, so qbs was not run. No remote comparison, fetch,
pull, merge, rebase, push, PR, or publication was performed.
