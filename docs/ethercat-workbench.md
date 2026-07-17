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

Right-clicking a supported repository device exposes `Add to Active Offline
Master`. The phase-1 project format contains one master, so the command targets
that master in the active valid project. A new stable slave ID and the next
physical position are allocated, and a case-insensitive unique name is derived
from the ESI device name. The slave keeps the complete identity and repository
reference and receives ESI-derived Process Data, Startup, and first DC-mode
defaults. The same private factory supplies those defaults to the existing
property pages, preventing the creation path and page proposal from drifting.

A configured slave exposes `Remove from Offline Master`, `Move Offline Slave
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
provider. All provider-owned widgets are destroyed before the provider leaves
the object pool. Provider addition, availability changes, and removal rebuild
the page set without retaining removed pointers.

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
Workbench suite passes 35 tests on the `QT_QPA_PLATFORM=offscreen`
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
