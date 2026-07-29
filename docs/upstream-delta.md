# Local Product Delta Register

## Purpose

This register identifies product changes that can make future Qt Creator
maintenance expensive. The current local `embed-labs` branch remains the
authoritative baseline. This document does not authorize a remote update.

## Recorded comparison point

The local branch contains a merge of Qt Creator 20.0 commit
`11ba5cec09dce75db4bc948d98055e338ff59576`. A remote-tracking reference with
the same commit was observed during the initial audit. All counts below are
relative to that recorded comparison point.

## Delta by area

| Area | Approximate changed paths | Maintenance classification |
|---|---:|---|
| Historical changelogs under `dist` | 118 | Product cleanup; high path count, low runtime risk |
| `src/plugins/easyboard` | 37 | Product-owned plugin |
| `src/app` | 28 | Branding, icons, splash, and application identity |
| `src/plugins/coreplugin` | 5 | Direct upstream Core intrusion; review target |
| Plugin top-level CMake/qbs | 2 | Required to register EasyBoard |
| Branding/qbs support | 2 | Product identity support |
| `.gitignore` | 1 | Product repository policy |
| `src/libs/ethercatdata` | 10 | Product-owned stable identity, project, device, scan, diagnostics, and controller-connection values |
| `src/plugins/ethercatcore` | 19 | Product-owned Core services and extension points |
| `src/plugins/ethercatproject` | 18 | Product-owned offline project plugin |
| `src/plugins/ethercatdevices` | 12 | Product-owned offline ESI repository plugin |
| `src/plugins/ethercatworkbench` | 44 | Product-owned EtherCAT engineering-shell plugin |
| `src/plugins/ethercatscan` | 16 | Product-owned local Mock scan workflow plugin |
| `src/plugins/ethercatdiagnostics` | 16 | Product-owned local Mock diagnostics plugin |

## Direct Core intrusion

The current product adds EasyBoard-specific concepts to the upstream Core
plugin:

- `Core::Constants::MODE_EASYBOARD`
- `Core::Constants::P_MODE_EASYBOARD`
- `Core::Constants::C_EASYBOARD_MODE`
- `Core::IEasyBoardPage`
- Core CMake and qbs source-list changes

The EasyBoard plugin currently includes this Core-specific page interface,
while its page registration methods are effectively empty. This is a known
maintenance hotspot. It must not be copied for EtherCAT.

EtherCAT extension points must live in `EtherCATCorePlugin` or a product-owned
library. No EtherCAT constant, page type, mode ID, or service may be added to
the upstream Core plugin without a separately approved ADR.

## Application and branding delta

Application-level changes include:

- Embed Labs product name and version metadata.
- macOS, Windows, and general application icons.
- Splash-screen removal.
- Application resource and bundle metadata.

These are intentional product changes. They should remain concentrated in
branding and application resource files. EtherCAT features must not add new
logic to `src/app/main.cpp` or the application bootstrap path.

## Existing EasyBoard plugin

`EasyBoard` is a product-owned plugin for board discovery, deployment, and
execution. It depends on Core, Debugger, ProjectExplorer, and RemoteLinux and
contains SSDP/UDP-related code.

For the EtherCAT first phase:

- Preserve its source and history.
- Do not add EtherCAT code to it.
- Do not use its network behavior as an EtherCAT protocol.
- Do not copy its Core intrusion pattern.
- Treat its product visibility as a separate keep/hide/remove decision.

No EasyBoard deletion is authorized by this register. Hiding or removing it
requires a dedicated local issue with an explicit migration and regression
check.

## Core patch budget

The stage-0 baseline contains five changed or added Core paths associated with
EasyBoard. The EtherCAT program has a zero-new-Core-path budget.

Stage 1 adds no path under `src/plugins/coreplugin` or `src/app`. It registers
one product library and one product plugin through the standard top-level
CMake and qbs lists. The direct Core patch count therefore remains five.

Stage 2 adds the product-owned `EtherCATProject` plugin. Its project type,
MIME metadata, wizard, ProjectManager integration, document, persistence, and
tests remain inside the plugin. The only shared source-list changes are the
top-level CMake and qbs plugin entries. It adds no path under
`src/plugins/coreplugin`, `src/plugins/projectexplorer`, or `src/app`, so the
direct Core patch count remains five.

Stage 3 adds the product-owned `EtherCATDevices` plugin and extends only the
product-owned data/Core contracts required for immutable ESI descriptions and
cancellable repository jobs. Stage 4 adds the product-owned
`EtherCATWorkbench` plugin and the product-owned property-page contract. Both
plugins use standard top-level CMake/qbs registration and public Qt Creator
extension APIs. Neither stage changes `src/plugins/coreplugin`,
`src/plugins/projectexplorer`, or `src/app`, so the direct Core patch count
remains five.

The stage-5 Scan API issue adds only product-owned immutable data types and a
typed in-process `ScanProvider` contract. Compatibility adjustments remain in
existing EtherCAT plugin files; no Qt Creator upstream Core or application
path is added, so the direct Core patch count remains five.

The stage-5 Project support issue adds the checked offline-slave command and a
compatible version-1 project extension entirely inside product-owned Core/Data
contracts and `EtherCATProject`. It changes no upstream ProjectExplorer or Core
path, so the direct Core patch count remains five.

The matching Workbench adaptation reads those public snapshots, renders
configured slaves, and reuses public ESI descriptions. It remains entirely in
the product-owned `EtherCATWorkbench` plugin, so the direct Core patch count
remains five.

The stage-5 Scan implementation adds one product-owned plugin through the
standard CMake/qbs lists and extends only the product-owned scan snapshot. It
uses ActionManager, object-pool Providers, ProjectService commands, and the
Workbench property-page extension point. It changes no Qt Creator upstream
Core, ProjectExplorer, or application path, so the direct Core patch count
remains five.

The stage-6 Diagnostics API issue adds only product-owned immutable diagnostic
values and typed methods/signals to the existing `DiagnosticsProvider`. The
Workbench compatibility test uses the public abstract contract. No Qt Creator
upstream Core, ProjectExplorer, or application path changes, so the direct Core
patch count remains five.

The stage-6 Diagnostics implementation adds one product-owned plugin through
the standard top-level CMake and qbs plugin lists. It uses the typed public
Provider, Project, selection, status, property-page, menu, and context
contracts. Its worker thread produces local numeric Mock samples and contains
no controller or network access. It changes no Qt Creator upstream Core,
ProjectExplorer, or application path, so the direct Core patch count remains
five.

The offline-configuration Core/API issue adds two product-owned
`EtherCATData` files, synchronized CMake/qbs source entries, Core contract
tests, and documentation. It changes no Qt Creator upstream Core,
ProjectExplorer, or application path, so the direct Core patch count remains
five.

The EtherCATProject format-version-2 issue extends only product-owned public
data, Core service declarations, Project implementation/tests, and
documentation. It persists Process Data, Startup, and DC values through the
existing public ProjectExplorer and IDocument lifecycle. It changes no Qt
Creator upstream Core, ProjectExplorer, or application path, so the direct
Core patch count remains five.

The Project structural-node-name Core/API issue extends only the product-owned
`EtherCATCore` service declaration, `EtherCATProject` implementation/tests, and
documentation. It adds a checked Target/Master rename command to the existing
Project document and Undo/Redo lifecycle without changing a source list,
plugin dependency, upstream Core, ProjectExplorer, or application path. The
direct Core patch count remains five.

The editable Process Data page issue changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It uses the existing property
page extension point, immutable ESI/Project snapshots, public Project service,
Qt item models, and `Core::MiniSplitter`. It adds no path under upstream Core,
ProjectExplorer, or the application bootstrap, so the direct Core patch count
remains five.

The editable Startup page issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It reuses the existing immutable
Startup values, ESI descriptions, property-page extension point, domain
validator, and public Project service. No upstream Core, ProjectExplorer, or
application bootstrap path changes, so the direct Core patch count remains
five.

The editable Distributed Clocks page issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It reuses existing immutable DC
values and ESI modes, the property-page extension point, the domain validator,
and the public Project service. No upstream Core, ProjectExplorer, or
application bootstrap path changes, so the direct Core patch count remains
five.

The Workbench derived-node Core/API issue extends only the product-owned
`EtherCATCore` public enum, its contract tests, and documentation. Existing
numeric values are frozen and the new process-image, PDO, module, and channel
kinds are appended. No upstream Core, ProjectExplorer, or application path
changes, so the direct Core patch count remains five.

The Workbench process-data tree issue changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It projects validated Project
snapshots into stable Inputs, Outputs, RxPDO, TxPDO, PDO, PDO Entry, and
Modules / Channels navigation nodes, and routes them through the existing
selection and property-page extension points. No upstream Core,
ProjectExplorer, or application path changes, so the direct Core patch count
remains five.

The Workbench CoE Online Mock issue changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds a private local object
model, page, filters, and an explicit call through the existing public Project
service; CMake and qbs list the same two new source files. It adds no plugin
dependency, controller transport, upstream Core, ProjectExplorer, or
application path change, so the direct Core patch count remains five.

The Workbench unified-status issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It uses the existing public
Qt Creator `StatusBarManager`, product-owned `StateService`, and public
Diagnostics Provider values, adds no plugin dependency, and keeps CMake and
qbs source lists synchronized. No upstream Core, ProjectExplorer, or
application path changes, so the direct Core patch count remains five.

The Workbench command-strip issue changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It uses Qt Creator's public
`ActionManager`/`ActionContainer` registrations and a standard `QToolBar`,
reusing the same actions without naming optional plugin commands or accessing
private UI. CMake and qbs list the same two new source files. No upstream Core,
ProjectExplorer, or application path changes, so the direct Core patch count
remains five.

The Workbench provider-state tree issue changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It consumes the
already exported immutable Scan/Diagnostics Provider values, uses standard
Creator model roles/icons and ActionManager commands, and adds no source-list
entry or plugin dependency. It does not change Scan/Diagnostics producers,
upstream Core, ProjectExplorer, or application paths, so the direct Core patch
count remains five.

The Workbench context-command issue also changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It replaces
transient tree/navigation actions with public ActionManager registrations and
reuses their QAction objects in the existing navigation factory and context
menu. It adds no source, dependency, upstream Core, ProjectExplorer, or
application path, so the direct Core patch count remains five.

The Workbench navigation-keyboard issue changes only the existing private
navigation widget, its tests, and documentation. It applies Qt Creator's
standard QWidget focus-proxy pattern to the existing tree and reuses the
current stable-ID selection bridge. It adds no source-list entry, CMake/qbs
change, dependency, public API, Provider, persistent data, upstream Core,
ProjectExplorer, or application path, so the Workbench path count remains 44
and the direct Core patch count remains five.

The Workbench navigation-filter empty-state issue also changes only that
existing private navigation widget, its tests, and documentation. It adds a
Workbench-local stacked no-match page, clear action, dynamic proxy-state
updates, and translated accessibility metadata without changing the model,
stable-ID service, Project or repository data, or optional Providers. It adds
no source-list entry, CMake/qbs change, dependency, public API, persistent
format, upstream Core, ProjectExplorer, or application path, so the Workbench
path count remains 44 and the direct Core patch count remains five.

The Workbench active-project navigation issue changes only the existing private
tree model, controller, tests, and documentation. It derives the visible marker
from the already public Project snapshots and active project ID, emits bounded
data changes without resetting stable selection, and reuses the existing
active-Master drop-target path. ProjectExplorer and EtherCATProject retain
startup-project, open/close, handoff, and persistence ownership. It adds no
source file, CMake/qbs entry, dependency, public API, model role, Provider,
persistent field, controller transport, upstream Core, ProjectExplorer, or
application path. The Workbench path count remains 44 and the direct Core patch
count remains five.

The Workbench set-active-project issue also changes only existing private
Workbench action registration, controller, navigation, tests, and
documentation. It routes the selected stable Project ID through the already
public `ProjectService::activateProject()` contract and attaches the existing
Workbench action context to the navigation widget so its registered proxy stays
live outside the Workbench mode. It adds no source file, CMake/qbs entry,
dependency, public API, model role, Provider, persistent field, controller
transport, upstream Core, ProjectExplorer, or application path. The Workbench
path count remains 44 and the direct Core patch count remains five.

The Workbench offline-topology issue changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private ESI-default
factory files with synchronized CMake/qbs entries, then reuses the existing
public Device repository, Selection service, and checked Project replacement
command for Add/Remove/Move operations. It adds no dependency or path under
upstream Core, ProjectExplorer, or the application bootstrap, so the direct
Core patch count remains five.

The Workbench ESI-repository issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private page source
files with synchronized CMake/qbs entries and reuses the existing public
`DeviceRepositoryProvider` import, rebuild, progress, result, and cancel
contracts. Devices retains parser, repository-data, and asynchronous-job
ownership. No plugin dependency, public API, controller protocol, upstream
Core, ProjectExplorer, or application path is added, so the direct Core patch
count remains five.

The Workbench ESI empty-guidance issue changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It replaces a stale
placeholder status with a translated pointer to the existing repository page
and import action. It adds no source-list entry, CMake/qbs change, dependency,
public API, persistent format, Provider call, controller protocol, upstream
Core, ProjectExplorer, or application path. The Workbench path count remains 44
and the direct Core patch count remains five.

The Workbench ESI catalogue-device General issue likewise changes only the
product-owned `EtherCATWorkbench` plugin and documentation. It adds two private
page source files with synchronized CMake/qbs entries and renders the existing
public immutable `DeviceDescription` snapshot. Devices retains parsing and
source-data ownership. No plugin dependency, public API, persistent format,
controller protocol, upstream Core, ProjectExplorer, or application path is
added, so the direct Core patch count remains five.

The Workbench ESI insertion issue likewise changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private selection
dialog source files with synchronized CMake/qbs entries, consumes the existing
immutable `DeviceSummary` catalogue, and routes explicit stable device/master
IDs through the existing checked Project service. It adds no plugin dependency,
public API, persistent format, controller protocol, upstream Core,
ProjectExplorer, or application path, so the direct Core patch count remains
five.

The Workbench ESI drag-and-drop issue changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It adds a private
stable-ID MIME path and a controller-owned callback that reuses the existing
checked insertion operation. It adds no source file, CMake/qbs entry, plugin
dependency, public API, persistent format, controller protocol, upstream Core,
ProjectExplorer, or application path. The Workbench path count remains 44 and
the direct Core patch count remains five.

The configured-slave General-page issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private General-page
source files with synchronized CMake/qbs entries and routes name changes through
the existing checked Project replacement command. It adds no dependency,
public Core API, or path under upstream Core, ProjectExplorer, or the application
bootstrap, so the direct Core patch count remains five.

The EtherCAT-master General-page issue changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It reuses the
existing property-page provider and checked structural-name command, adds no
source, dependency, public API, or path under upstream Core, ProjectExplorer,
or the application bootstrap, so the direct Core patch count remains five.

The offline-project General-page issue changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It reuses the
existing property-page provider, immutable Project snapshot, and public
`ProjectService::renameProject()` command. It adds no source-list entry,
dependency, persistent field, public API, controller transport, or path under
upstream Core, ProjectExplorer, or the application bootstrap, so the direct
Core patch count remains five.

The EtherCAT-master EtherCAT-page issue also changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It reuses the
existing property-page provider and immutable offline Project snapshot for a
local topology dialog. Unsupported ADS/NetId routing, export, Sync Unit, and
runtime-frame functions remain explicit unavailable states. It adds no source,
dependency, persistent field, public API, controller transport, or path under
upstream Core, ProjectExplorer, or the application bootstrap, so the direct
Core patch count remains five.

The offline-target General-page issue also changes only existing files in the
product-owned `EtherCATWorkbench` plugin and documentation. It reuses the
existing property-page provider, checked structural-name command, public
`Core::ICore` version API, and standard Qt style icon. Unsupported target
discovery and runtime-version controls remain explicit disabled states. It adds
no source, dependency, public API, target transport, or path under upstream
Core, ProjectExplorer, or the application bootstrap, so the direct Core patch
count remains five.

The configured-slave EtherCAT-page issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private EtherCAT-page
source files with synchronized CMake/qbs entries, preserves the existing master
and ESI read-only views, and routes Alias changes through the existing checked
Project replacement command. It adds no dependency, public Core API, or path
under upstream Core, ProjectExplorer, or the application bootstrap, so the
direct Core patch count remains five.

The Workbench Details empty-state lifecycle issue changes only existing private
`EtherCATWorkbench` Details and test files plus documentation. It distinguishes
zero-project and open-project/no-selection guidance from the public Project
snapshot list, refreshes after the private tree model lifecycle signal, and
adds standard translated QWidget accessibility metadata. It adds no source
file, CMake/qbs entry, dependency, public API, model role, Provider, persistent
field, QAction, controller transport, or path under upstream Core,
ProjectExplorer, or the application bootstrap. The Workbench path count remains
44 and the direct Core patch count remains five.

The Workbench optional-Provider presentation issue changes only existing
private controller, tree, built-in page, Details, and test files in the
product-owned `EtherCATWorkbench` plugin plus documentation. It derives
absent, registered/unavailable, and available presentation from the existing
public `ProviderRegistry`, `Provider::isAvailable()`, display name, and object
pool lifecycle signals. Workbench uses one private, atomic producer selection
for the copied result/snapshot and normalized displayed name, including blank
name fallback, activity changes, removal, and re-addition; no producer pointer
crosses into the tree model. It
adds no Provider, public API, source file, dependency, persistent field,
QAction, model role, controller transport, network behavior, CMake/qbs entry,
or path under upstream Core, ProjectExplorer, or the application bootstrap.
The Workbench path count remains 44 and the direct Core patch count remains
five.

The Workbench invalid-project presentation issue changes only existing private
tree-model, controller, navigation/action-setup, General-page, and test files
in the product-owned `EtherCATWorkbench` plugin plus documentation. It consumes
the existing public `ProjectSnapshot::valid` and `ProjectSnapshot::error`
values, hides generated recovery topology and ID-copy path, rejects only the
Workbench-owned activation command, and preserves ProjectExplorer ownership
of startup-project state and close fallback. It adds no public API, model role,
source file, dependency, persistent field, QAction, Provider, controller
transport, network behavior, CMake/qbs entry, or path under upstream Core,
ProjectExplorer, or the application bootstrap. The Workbench path count
remains 44 and the direct Core patch count remains five.

The Workbench project-scoped Diagnostics navigation issue changes only the
existing private action setup, navigation widget, integration test, and
documentation. It derives the selected project from the existing stable
selection context, requires an exact Diagnostics match for a non-null project
ID, rejects unknown non-null selections, preserves the existing genuinely
null-selection and known-projectless-node first-available lookup, restores
stable action state after a placeholder context menu, and guards action refresh
after `SelectionService` teardown. It adds no public API, model role, QAction,
source file, dependency, persistent field, Provider, controller transport,
online or hardware behavior, CMake/qbs entry, or path under upstream Core,
ProjectExplorer, or the application bootstrap. The Workbench path count
remains 44 and the direct Core patch count remains five.

The Workbench tree-row accessibility issue changes only the existing private
tree model, Workbench integration test, and documentation. It maps existing
Display and tooltip content onto Qt's standard accessibility item roles and
includes those roles in active-project and Provider-overlay data-change
notifications. It adds no custom or public model role, source file, dependency,
persistent field, Provider, QAction, thread, timer, controller transport,
online or hardware behavior, CMake/qbs entry, or path under upstream Core,
ProjectExplorer, or the application bootstrap. The Workbench path count
remains 44 and the direct Core patch count remains five.

The Workbench project-scoped Locate issue changes only the existing private
tree model, navigation widget, action setup, Workbench integration test, and
documentation. It filters first-difference and first-issue lookup by the
selected stable context's project ID, rejects unknown non-null selections,
retains the existing null/known-projectless global lookup, and restores scoped
action state after a placeholder context menu. It adds no public API, model
role, QAction, source file, dependency, persistent field, Provider, thread,
timer, controller transport, online or hardware behavior, CMake/qbs entry, or
path under upstream Core, ProjectExplorer, or the application bootstrap. The
Workbench path count remains 44 and the direct Core patch count remains five.

The Workbench offline-slave removal confirmation issue changes only the
existing private action setup, controller, Workbench integration test, and
documentation. It captures copied project, Master, and slave IDs before the
question opens; uses an explicit non-destructive default and Escape action;
and revalidates the current stable Selection and latest Project snapshot
before submitting the existing Project-owned replacement command. Selection
drift, project close, controller teardown, and a stale slave are rejected
without mutation. It adds no public API, source file, dependency, persistent
field, Provider, thread, timer, controller transport, online or hardware
behavior, CMake/qbs entry, or path under upstream Core, ProjectExplorer, or
the application bootstrap. The Workbench path count remains 44 and the direct
Core patch count remains five.

The Workbench repository quick-add target disclosure issue changes only the
existing private action setup, controller, Workbench integration test, and
documentation. The action displays copied Project and Master names together
with their stable IDs, refreshes on active-project lifecycle changes, and
passes the same displayed target to the controller. The controller resolves
the current active valid Project and Master again and rejects mismatched IDs
before calling the existing Project-owned replacement command. It adds no
public API, source file, dependency, persistent field, Provider, thread,
timer, controller transport, online or hardware behavior, CMake/qbs entry, or
path under upstream Core, ProjectExplorer, or the application bootstrap. The
Workbench path count remains 44 and the direct Core patch count remains five.

The historical Workbench Details Provider-removal continuity issue changed
only the existing private Details implementation, Workbench integration test,
and documentation. Its semantic Provider/Page key and departing-ID guard
remain in use, but the current product-owned ProviderRegistry now unlinks a
departing Provider before emitting `providerAboutToBeRemoved`; the object
itself remains in the PluginManager pool until that notification returns.
Registry queries and nested removals therefore cannot rediscover it. Ordinary
hosted pages are destroyed before `removeObject()` returns. For self-unregister
inside the Provider's active page callback, the page leaves the live-page map
immediately. An already-tabbed active widget may remain attached and alive
until callback return, when it is destroyed; the Provider/plugin must keep the
page destructor and Qt meta-object code loaded through that boundary.

The unified refresh path restores a captured key only while its context and
page remain current; otherwise it honors the newest semantic/user state. A
later `providerAdded` for the same ID clears the departing marker. The semantic
refresh transaction retains no widget or Provider pointer. Short-lived
`QPointer` guards are revalidated immediately after callbacks and do not
outlive the enclosing host operation. Details rejects switch-away/switch-back
ABA state and adds no persisted tab state.

Qt Creator 20.0's Project settings widget follows the same general continuity
principle by saving its current tab index before replacing panels and restoring
the index afterward
(<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1366-L1385>).
Workbench uses a semantic key because dynamic Provider insertion/removal can
change numeric indexes. Qt's `QTabWidget` contract and Qt Creator object-pool
notification order are documented at
<https://doc.qt.io/qt-6/qtabwidget.html> and
<https://doc.qt.io/qtcreator-extending/pluginmanager.html>. Beckhoff documents
selection-dependent EtherCAT terminal property tabs at
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>,
but does not specify third-party Provider churn. The exact PageKey/context
guard is therefore an Embed Labs Qt-native delta, not copied Beckhoff behavior.

The issue adds no public API, model role, source file, Provider, dependency,
persistent field, thread, timer, controller transport, online or hardware
behavior, CMake/qbs entry, or path under upstream Core, ProjectExplorer, or
the application bootstrap. The Workbench path count remains 44 and the direct
Core patch count remains five.

The Workbench context-menu selection-drift issue changes only the existing
private navigation implementation, Workbench integration test, and
documentation. A real popup captures the current stable Selection Service
`NodeId` and closes when a different ID is published, preventing shared
ActionManager commands presented for an old row from resolving a later row as
their mutation target. It preserves the new selection and reuses the existing
post-menu action-state synchronization; it does not copy actions, change
shortcuts, or add per-command target parameters.

Qt documents that `QMenu::exec()` emits connected action signals normally
(<https://doc.qt.io/qt-6/qmenu.html#exec>) and Qt Creator documents that the
user-facing `Command::action()` is shared by menus and toolbars
(<https://doc.qt.io/qtcreator-extending/actionmanager.html>). Qt Creator 20.0
retains Project-tree context-menu focus until the popup hides
(<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttree.cpp#L337-L395>).
Beckhoff binds its configured-I/O context menu to the selected device
(<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>).
Close-on-drift is an Embed Labs Qt-native safety decision, not copied Beckhoff
behavior.

The issue adds no public API, model role, QAction, source file, Provider,
dependency, persistent field, production thread or timer, controller
transport, online or hardware behavior, CMake/qbs entry, or path under
upstream Core, ProjectExplorer, or the application bootstrap. The Workbench
path count remains 44 and the direct Core patch count remains five.

The Workbench navigation expansion-continuity issue changes only the existing
private navigation implementation, Workbench test declaration/implementation,
and documentation. It records normal expanded branches by the existing stable
`NodeId`, restores surviving IDs after a source-model reset, preserves the
depth-two default for newly introduced Project structure, and isolates
filter-driven expansion from the normal tree state. Selection Service remains
authoritative and its current node's ancestor path is revealed after
restoration.

Qt's `QTreeView` expansion signals and model-reset invalidation rules, plus Qt
Creator 20.0's local Project-tree semantic expansion cache, were inspected as
read-only precedents:
<https://doc.qt.io/qt-6/qtreeview.html#expanded>,
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttreewidget.cpp#L291-L302>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L486-L496>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L526-L539>.
No ProjectExplorer file or private API was modified or reused. Beckhoff's I/O
device tree description is only the Master/Slave/process-data hierarchy
comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>.
The exact reset/filter continuity rule is an Embed Labs Qt-native delta.

The issue adds no public API, model role, QAction, source file, Provider,
dependency, persistent field, thread, timer, network/controller behavior,
physical-hardware behavior, CMake/qbs entry, or path under upstream Core,
ProjectExplorer, or the application bootstrap. The Workbench path count
remains 44 and the direct Core patch count remains five.

Each completed EtherCAT issue must report:

- Direct upstream files modified.
- Product-owned plugin/library files modified.
- Whether the Core patch count increased.
- Why an official extension point was insufficient, if the count increased.

The Workbench Details focus-continuity issue, based on
`0637955df9ab009bb0ee1fcb892dc74e5ede27e7`, changes existing files in the
product-owned `EtherCATCore` and `EtherCATWorkbench` plugins plus
documentation. EtherCATCore unlinks a departing Provider before emitting its
registry removal signal and tests nested removal. EtherCATWorkbench adds the
value-only `{NodeId, PageKey, objectName, generation}` focus token, one bounded
rebuild/refresh pump, user tab/focus priority after a yield, and guarded nested
page-removal/self-unregister handling.

The Provider object remains in PluginManager's pool until its object-removal
notification returns, but it is already absent from ProviderRegistry queries
inside `providerAboutToBeRemoved`. Ordinary hosted pages are destroyed before
`removeObject()` returns. When a Provider unregisters from its own
`pages()`, `createPage()`, or `updatePage()` callback, no callback-associated
widget remains in the live-page map after removal handling. An already-tabbed
active widget may remain attached and alive until callback return, when it is
destroyed; the Provider/plugin must keep its page destructor and Qt meta-object
code loaded through that boundary.

No file under Qt Creator's upstream Core, ProjectExplorer, or application
bootstrap changes. In particular, `src/plugins/ethercatcore` is an Embed Labs
product plugin and is not counted as an upstream Core patch. No CMake or qbs
description changes, so qbs was not run. The Workbench path count remains 44
and the direct upstream Core patch count remains five.

The Workbench CoE filter empty-state and accessibility issue, based on
`6935b103b1adbc3641c28afcd3e683f2772848fb`, changes only the existing
product-owned `coeonlinepage.h`, `coeonlinepage.cpp`,
`ethercatworkbenchtests.cpp`, and documentation. It adds an explicit
proxy-driven zero-result presentation, accessible filter/result metadata,
atomic filter clearing, and private value-only object-address restoration.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, controller transport, network behavior, or physical-hardware
behavior changes. No file under Qt Creator's upstream Core, ProjectExplorer,
or application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

The Workbench insert-device dialog target-lifecycle issue, based on
`a0b68e7313b80e4292a135966f7ead4c4e131656`, changes only the existing
product-owned `workbenchmode.cpp`, Workbench test declaration/implementation,
and documentation. It captures the stable Project and Master IDs at dialog
creation and rejects the dialog when ProjectService reports target closure,
active-Project drift, Project invalidation, or removal of the target Master.
The existing accepted Project mutation path is unchanged.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence, controller transport, network behavior, or
physical-hardware behavior changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

The Workbench Process Data table accessibility issue, based on
`1ce66e3332e17f0ab3e6e4597734d0339dfad230`, changes only the existing
product-owned `processdatapage.cpp`, Workbench test declaration/implementation,
and documentation. It adds unique table widget names/descriptions and consumes
only Qt's standard `AccessibleTextRole` and `AccessibleDescriptionRole` for
full current-cell values, row/column context, and existing guidance. It adds no
custom role or retained model index.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence, Project command, controller transport, network
behavior, or physical-hardware behavior changes. No file under Qt Creator's
upstream Core, ProjectExplorer, or application bootstrap changes, so the
direct Core patch budget does not increase and no extension-point exception is
required. The Workbench path count remains 44 and the direct upstream Core
patch count remains five.

The Workbench topology dialog bounds issue, based on
`9e56cfebbd09766ea7ae326b5741acb95fecb274`, changes only the existing
product-owned `ethercatpage.cpp`, Workbench test declaration/implementation,
and documentation. It replaces the private table's unbounded full-content
minimum width with a preferred dialog geometry bounded and centered against
the current Workbench page screen. The existing full section sizes,
`ElideNone`, horizontal scrolling, offline values, and modal ownership remain
inside the Workbench plugin.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence, Project command, controller transport, online data,
physical-port model, network behavior, or physical-hardware behavior changes.
No file under Qt Creator's upstream Core, ProjectExplorer, or application
bootstrap changes, so the direct Core patch budget does not increase and no
extension-point exception is required. The Workbench path count remains 44
and the direct upstream Core patch count remains five.

The Workbench Startup table accessibility issue, based on
`cf3c437147e816320256c1e75b5c67863de77663`, changes only the existing
product-owned `startuppage.cpp`, Workbench test declaration/implementation,
and documentation. It adds translated widget metadata and uses only Qt's
standard `AccessibleTextRole` and `AccessibleDescriptionRole` for complete
current values, object/column context, and existing fixed/read-only/editable
guidance. The visual Enabled cell remains empty and its existing
`CheckStateRole` remains authoritative.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, thread, timer, SDO/controller
transport, online state, network behavior, or physical-hardware behavior
changes. No file under Qt Creator's upstream Core, ProjectExplorer, or
application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

The Workbench CoE dictionary cell accessibility issue, based on
`bc6d2e1b45f521fb962790a19370672bbc7ff687`, changes only the existing
product-owned `coeonlinepage.cpp`, Workbench test declaration/implementation,
and documentation. It uses Qt's standard `AccessibleTextRole` and
`AccessibleDescriptionRole` for complete five-column current values,
object/column context, Mock/offline source, and truthful operation/transport
guidance. A transient Mock Value edit now reports the corresponding standard
presentation/accessibility roles without adding a custom public role or
Project command.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, thread, timer, SDO/controller
transport, online state, network behavior, or physical-hardware behavior
changes. No file under Qt Creator's upstream Core, ProjectExplorer, or
application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.
Repository Device CoE editability was not included in that accessibility
delta; it is recorded independently below.

The Workbench repository Device CoE read-only issue, based on
`3f4ea273a559506af663ae1edbb5233d31688565`, changes only the existing
product-owned `coeonlinepage.cpp`, Workbench test declaration/implementation,
and documentation. It passes a private context-level Mock editing permission
into the object model during the existing definition reset. Both item flags
and direct `EditRole` mutation require that permission. Repository objects
retain their ESI-derived `RW` capability metadata while their catalogue Value
cells remain read-only; configured-slave transient Mock editing remains
available.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, thread,
timer, SDO/controller transport, online state, network behavior, or
physical-hardware behavior changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

The Workbench ESI device-selection cell accessibility issue, based on
`2ffc91065b6d940bd9bc6197eb96d0471dfc4795`, changes only the existing
product-owned `esideviceselectiondialog.cpp`, Workbench test
declaration/implementation, and documentation. It uses Qt's standard
`AccessibleTextRole` and `AccessibleDescriptionRole` for complete seven-column
current values, column context, Supported/Limited qualification, and truthful
offline append/controller/network guidance. Tooltips expose the same complete
recovery text without changing the dialog's visual geometry or operation.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, thread,
timer, controller transport, online state, network behavior, or
physical-hardware behavior changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

`ISSUE-WB-DETAILS-UNRELATED-PROJECT-DRAFT-001`, based on
`e5ded42f8dd3d3d861de830e3408de12f1847334`, changes only the existing
product-owned `detailsview.cpp`, Workbench test declaration/implementation,
and documentation. The private `ProjectService::projectChanged` connection
now consumes the signal's value-only snapshot and refreshes hosted pages only
when that Project owns the current Details context. Unrelated Project changes
therefore preserve current draft text, modified state, focus, selection, and
context; current-Project changes still refresh through the existing path.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, model role, thread, timer,
controller transport, online state, network behavior, or physical-hardware
behavior changes. No file under Qt Creator's upstream Core, ProjectExplorer,
or application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

`ISSUE-WB-GENERAL-PROPERTY-TREE-A11Y-001`, based on
`2b956da48104c97414da33190b9772e033969ee7`, changes only the existing
product-owned `generalpage.cpp`, Workbench test declaration/implementation,
and documentation. The private General Property/Value tree now publishes a
widget name/description and uses Qt's standard `AccessibleTextRole` and
`AccessibleDescriptionRole` for complete current values, column/property
context, and the existing read-only offline/controller/network/hardware
boundary. Its equal tooltip provides the same complete-value recovery without
changing visible elision or geometry.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, thread,
timer, controller transport, online state, network behavior, or
physical-hardware behavior changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

`ISSUE-WB-ETHERCAT-SYNCMANAGER-CELL-A11Y-001`, based on
`5cd0bade8355565a07969d97b49965bce93503b2`, changes only the existing
product-owned private `ethercatpage.cpp` and `ethercatpage.h`, Workbench test
declaration/implementation, and documentation. The configured-slave and
repository Device EtherCAT pages now describe their existing SyncManager table
as read-only offline ESI data. Every seven-column cell uses Qt's standard
`AccessibleTextRole` and `AccessibleDescriptionRole` for its complete current
display value, column and SM identity, plus the controller/network/hardware
boundary. Its equal tooltip provides complete-value recovery without changing
the existing visible layout, horizontal scrolling, or operation.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, thread, timer,
controller transport, online state, network behavior, or physical-hardware
behavior changes. No file under Qt Creator's upstream Core, ProjectExplorer,
or application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

`ISSUE-WB-TOPOLOGY-CELL-A11Y-001`, based on
`23d67736335450d4fce299434f0e13024758cf2f`, changes only the existing
product-owned private `ethercatpage.cpp`, Workbench test
declaration/implementation, and documentation. The stack-local master
Topology table now identifies its current offline Project boundary and uses
Qt's standard `AccessibleTextRole` and `AccessibleDescriptionRole` for every
complete ten-column display value, configured slave identity, and truthful
operation/source context. Its equal tooltip provides the same complete-value
recovery without changing the previously qualified dialog geometry or
scrolling.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, production
thread or timer, controller transport, online state, physical-port model,
network behavior, or physical-hardware behavior changes. No file under Qt
Creator's upstream Core, ProjectExplorer, or application bootstrap changes,
so the direct Core patch budget does not increase and no extension-point
exception is required. The Workbench path count remains 44 and the direct
upstream Core patch count remains five.

`ISSUE-WB-DC-REPOSITORY-MODE-PREVIEW-001`, based on
`266325f933f0072c61ccec29ec1059d7e1610a29`, changes only the existing
product-owned private `dcpage.cpp`, Workbench test declaration/implementation,
and documentation. The ESI Repository Device DC selector now exposes every
already-parsed operation mode for a keyboard-accessible read-only preview.
Selection updates only the page's transient presentation value and returns
before the existing configured-slave `ProjectService` submission path. All
actual configuration controls remain read-only or disabled, and recreating the
Device context restores the first ESI mode.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, model role, production thread or
timer, controller/network transport, online state, or physical-hardware
behavior changes. No file under Qt Creator's upstream Core, ProjectExplorer,
or application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

`ISSUE-WB-DC-REPOSITORY-EMPTY-001`, based on
`f60f0fc0939b3945c4026851e9f4e86e15e2d9be`, changes only the existing
product-owned private `dcpage.cpp` and `dcpage.h`, Workbench test
declaration/implementation, and documentation. Repository Devices whose
imported ESI descriptions contain no DC mode now expose a truthful explicit
empty summary and informational status instead of selection guidance and a
false valid-configuration state.
The zero-item selector remains disabled/read-only and its localized
description/tooltip states the offline Project recovery and no-controller,
network, or physical-hardware boundary.

The private page also distinguishes a removed ESI description from a valid
zero-mode Device. The removed-description warning returns the user to Device
Repository and does not advertise an impossible offline Project add path.

It also distinguishes a supported zero-mode Device from one whose ESI contains
unsupported structures. The latter receives a warning and Device Repository
support-review recovery because the existing offline topology gate rejects it;
the page does not advertise an impossible Project-add path.

Unsupported Devices that still expose DC modes retain local read-only preview,
but the summary, validation, selector description, and tooltip no longer
advertise Project addition. A valid preview shows the support warning; an
invalid preview keeps its configuration diagnostic and appends the cannot-add
and Device Repository recovery.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, model role, production thread or
timer, controller/network transport, online state, or physical-hardware
behavior changes. No file under Qt Creator's upstream Core, ProjectExplorer,
or application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

`ISSUE-WB-PROCESS-DATA-REPOSITORY-EMPTY-001`, based on
`8818c6c5c48a0e3c7b1e3a770c758401c926e32c`, changes only the existing
product-owned private `processdatapage.cpp` and `processdatapage.h`, Workbench
test declaration/implementation, and documentation. Repository Device Process
Data now distinguishes supported/unsupported empty mappings, supported or
unsupported invalid previews, removed descriptions, and the existing valid
catalogue. Zero-row tables expose an explicit empty state rather than select
guidance or a false valid-configuration result.

The five existing tables remain read-only and receive contextual localized
accessible descriptions and equal tooltips. The page does not fabricate an
SM/PDO, mutate a Project, add a Device, change validation, or introduce any
online/controller/hardware function. Existing checked topology and Project
validation paths continue to own Add, rejection, and Undo.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, production
thread or timer, controller/network transport, online state, or physical
hardware behavior changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

`ISSUE-WB-STARTUP-REPOSITORY-EMPTY-001`, based on
`24576f40eab24825baea29047cda750136a82819`, changes only the existing
product-owned private `startuppage.cpp` and `startuppage.h`, Workbench test
declaration/implementation, and documentation. Repository Device Startup now
distinguishes supported/unsupported empty catalogues,
supported/unsupported invalid previews, removed descriptions, and the existing
populated catalogue. A zero-row table exposes a truthful explicit empty state
instead of generic Add guidance and a false valid-zero-request result.

The existing table remains read-only and receives a context-specific localized
accessible description and equal tooltip. Empty state uses real zero model
rows. The page does not fabricate a Startup request, modify a Project, add a
Device, change validation, send an SDO, or introduce controller/network/
hardware behavior. Existing checked topology and Project validation paths
continue to own Add, rejection, Undo, and Redo.

The final regression uses six real imported ESI contexts and a removed Device,
rejects direct EditRole/CheckStateRole mutation, proves page-reuse cleanup, and
proves complete Project snapshot plus Undo/Redo isolation. The supported-empty
recovery uses the existing Add path and is undone; all unsupported and invalid
cases use the existing rejection paths. The test-only Project scope guard
ensures cleanup on assertion failure and changes no production ownership.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, production
thread or timer, controller/network transport, online state, or physical
hardware behavior changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

`ISSUE-WB-ETHERCAT-REPOSITORY-EMPTY-001`, based on
`5c88012561d3317b6d689f31668a0a31c2bd66d1`, changes only the existing
product-owned private `ethercatpage.cpp`, Workbench test
declaration/implementation, and documentation. Repository Device EtherCAT
pages now distinguish supported/unsupported zero-SyncManager descriptions,
supported/unsupported populated previews, and unresolved/missing
descriptions. Empty and unresolved states expose explicit summaries and real
zero rows without leaving a header-only tree or fabricating a SyncManager.

Current summary and tree accessibility descriptions plus equal tooltips are
rebuilt on every context update, so page reuse removes stale empty,
unsupported, and unavailable state. Existing populated cell roles and
read-only behavior remain unchanged. The page adds no Project operation,
controller transport, online state, network access, or hardware behavior.

No source file, dependency, CMake/qbs entry, public API, Project format,
Provider, persistence field, Project command, custom model role, production
thread, or timer changes. No file under Qt Creator's upstream Core,
ProjectExplorer, or application bootstrap changes, so the direct Core patch
budget does not increase and no extension-point exception is required. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

`ISSUE-WB-COE-MODAL-REFRESH-001`, based on
`7395c4ceefe78896496bf3b83d757f5096550e03`, changes only the existing
product-owned private `coeonlinepage.cpp/.h`, Workbench test
declaration/implementation, and documentation. CoE Advanced Settings and Add
to Startup now use page-owned asynchronous dialogs plus a page-local context
generation, so a real same-identity ESI refresh cannot apply a result created
against the prior page context. Add also captures scalar object data and IDs
before confirmation and never retains a model index across that boundary.

The change introduces no Provider revision, public API, source file,
dependency, CMake/qbs entry, Project format, persistence field, Project
command, custom model role, production thread or timer, controller/network
transport, online state, SDO execution, or hardware behavior. No file under
Qt Creator's upstream Core, ProjectExplorer, or application bootstrap changes,
so the direct Core patch budget does not increase and no extension-point
exception is required. The Workbench path count remains 44 and the direct
upstream Core patch count remains five.

`ISSUE-WB-STARTUP-MODAL-REFRESH-001`, based on
`065dfbeac038cb4071336492449292879c69b8ee`, changes only the existing
product-owned private `startuppage.cpp/.h`, Workbench test
declaration/implementation, and documentation. Startup New, Edit, and Delete
now use page-owned asynchronous dialogs plus a page-local context generation,
so a Project refresh cannot apply a response created against the previous
page state. Current responses re-query the Project and slave and merge by
stable request ID; no model index, row, pointer, or snapshot reference crosses
the dialog boundary.

The change introduces no ProjectService or Provider revision, public API,
source file, dependency, CMake/qbs entry, Project format, persistence field,
Project command, custom model role, production thread or timer,
controller/network transport, online state, SDO execution, or hardware
behavior. No file under Qt Creator's upstream Core, ProjectExplorer, or
application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

`ISSUE-WB-TREE-PHYSICAL-ORDER-001`, based on
`a9525adcf39b11704d04e2b65da3488897c43f56`, changes only the existing
product-owned private `workbenchtreemodel.cpp`, Workbench test
declaration/implementation, and documentation. Master children that form a
complete configured-Slave sibling group now follow the offline Project
position; rename no longer changes physical display order, and existing Move
Up/Down commands immediately agree with the visible source and proxy rows.
Incomplete groups preserve the original name ordering, while equal positions
use deterministic private tie-breakers.

The change introduces no ProjectService or Provider revision, public API,
source file, dependency, CMake/qbs entry, Project format, persistence field,
Project command, custom model role, production thread or timer,
controller/network transport, online state, scan, SDO execution, or hardware
behavior. No file under Qt Creator's upstream Core, ProjectExplorer, or
application bootstrap changes, so the direct Core patch budget does not
increase and no extension-point exception is required. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

`ISSUE-WB-TOPOLOGY-DIALOG-PAGE-LIFECYCLE-001`, based on
`e814edf65c95ef3c5701f7655c1ff0237cdeac26`, changes only the existing
product-owned private `ethercatpage.cpp/.h`, Workbench test
declaration/implementation, and documentation. The read-only master Topology
dialog is now page-owned, delete-on-close, and asynchronous. Any page context
refresh closes the old snapshot; repeat activation raises the current
instance; Close, immediate reopen, selection change, project close, and page
teardown no longer depend on a nested modal event loop. Earlier topology
bounds and cell-accessibility lifetime descriptions are superseded, while
their geometry and accessibility results remain valid.

The change introduces no ProjectService or Provider revision, public API,
source file, dependency, CMake/qbs entry, Project format, persistence field,
Project command, custom model role, production thread or timer,
controller/network transport, online topology, port graph, CRC/state control,
scan, SDO execution, or hardware behavior. No file under Qt Creator's upstream
Core, ProjectExplorer, or application bootstrap changes, so the direct Core
patch budget does not increase and no extension-point exception is required.
The Workbench path count remains 44 and the direct upstream Core patch count
remains five. Qualification used only local/offline Mock state and offscreen
product lifecycle runs; no remote comparison or publication occurred.

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-PROJECT-REFRESH-001`, based on
`312e1a9dbacfd621615c87d644b7a46bb5f5a64e`, changes only the existing
product-owned private `workbenchcontroller.cpp/.h`, Workbench test
declaration/implementation, and documentation. The removal question now owns
the complete target Slave configuration by value. Yes re-resolves stable IDs
and selection, then rejects a response if that same target configuration has
changed. The refreshed Project, selection, and Undo/Redo state remain intact
until the user confirms the current value. Another Project or sibling Slave
does not invalidate the response while the target Slave value remains
unchanged.

The change introduces no revision/generation or CAS framework, public API,
source file, dependency, CMake/qbs entry, Project format, persistence field,
Provider or ProjectService contract, Project command, custom model role,
production thread or timer, Core or ProjectExplorer hook,
application-bootstrap change, controller/network transport, online state,
scan, SDO execution, or hardware behavior. The Workbench path count remains
44 and the direct upstream Core patch count remains five. Qualification used
only local/offline Mock state plus offscreen enabled/disabled product
lifecycle runs. No visible main window, remote comparison, fetch, pull, merge,
rebase, push, or publication occurred.

`ISSUE-WB-INSERT-DIALOG-ASYNC-LIFECYCLE-001`, based on
`617505a08cb2055d6042a2580189182ffd39658a`, changes only the existing
product-owned private `workbenchmode.cpp`, Workbench test
declaration/implementation, and documentation. The Master Add New Item
selector is now mode-owned, delete-on-close, asynchronous, and single-instance.
Existing stable-ID target invalidation and the controller/ProjectService
mutation path remain in force; no selector content or persistence behavior
changes.

The change introduces no public API, source file, dependency, CMake/qbs entry,
Project format, persistence field, Provider or ProjectService contract,
Project command, custom model role, production thread or timer, Core or
ProjectExplorer hook, application-bootstrap change, controller/network
transport, online state, scan, SDO execution, or hardware behavior. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five. Qualification used only local/offline Mock state plus offscreen
enabled/disabled product lifecycle runs. No visible main window, remote
comparison, fetch, pull, merge, rebase, push, or publication occurred.

`ISSUE-WB-GENERAL-NONCONFLICTING-REFRESH-DRAFT-001`, based on
`2fe30622dd971c9c3afd91e78e5addb2fa988340`, changes only the existing
product-owned private `generalpage.cpp/.h`, Workbench test
declaration/implementation, and documentation. The page now retains a
writable modified or focused name editor across a same-stable-node refresh
only when the newly read persisted name still equals its last baseline. Focus
conservatively avoids disturbing a possible input-method preedit before
`modified` changes; qualification directly covers the clean-focused gate, not
platform IME composition. The page leaves that editor untouched while
refreshing the other General fields. Identity and authority checks handle real
name changes, Undo, Redo, invalid or switched context, and project close. A
scoped force-authority guard handles explicit rejected commits and trimmed
no-ops.

The change introduces no generic dirty-form or conflict framework, autosave,
cross-node draft cache, revision/generation or CAS protocol, public API,
source file, dependency, CMake/qbs entry, Project format, persistence field,
Provider or ProjectService contract, Project command, custom model role,
production thread or timer, Core or ProjectExplorer hook,
application-bootstrap change, controller/network transport, online state,
scan, SDO execution, or hardware behavior. Device Repository signals and
Details refresh routing are unchanged. The Workbench path count remains 44
and the direct upstream Core patch count remains five. Qualification used only
local/offline Mock state plus offscreen enabled/disabled product lifecycle
runs with passed-through SIGTERM. No visible main window, remote comparison,
fetch, pull, merge, rebase, push, or publication occurred.

`ISSUE-WB-ESI-REVISION-TOGGLE-SELECTION-001`, based on
`42110b4e990baae92fbed37f38ab6aa274b34657`, changes only the existing
product-owned private `esideviceselectiondialog.cpp/.h`, Workbench test
declaration/implementation, and documentation. The dialog now restores a
selected stable device ID after its previous-revision filter changes whenever
that device remains visible. If it is hidden or absent, the established
first-supported/first-visible fallback remains unchanged. Text filtering,
revision comparison, repository ownership, Add validation, and controller
mutation are unchanged.

The change introduces no public API, source file, dependency, CMake/qbs
entry, Project format, persistence field, Provider or ProjectService
contract, Project command, custom model role, production thread or timer,
Core or ProjectExplorer hook, application-bootstrap change,
controller/network transport, online state, scan, SDO execution, or hardware
behavior. The Workbench path count remains 44 and the direct upstream Core
patch count remains five. Qualification used only local/offline Mock state
plus offscreen enabled/disabled product lifecycle runs with passed-through
SIGTERM. No visible main window, remote comparison, fetch, pull, merge,
rebase, push, or publication occurred.

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-CONFIRM-INVALIDATION-001`, based on
`532517066a612c4f5220484cadb830d0c07256c0`, changes only the existing
product-owned private Workbench action setup, Workbench test
declaration/implementation, and documentation. A configured-slave removal
question now rejects itself when its stable Selection changes, its Project is
being removed, or its complete captured slave configuration is no longer
current. Changes to another Project, or sibling-only changes that leave the
captured candidate unchanged, remain non-conflicting. The existing controller
revalidation and Project-owned removal/Undo/Redo path remain unchanged.

The change introduces no public API, source file, dependency, CMake/qbs entry,
Project format, persistence field, Provider or ProjectService contract,
Project command, custom model role, production thread or timer, Core or
ProjectExplorer hook, application-bootstrap change, controller/network
transport, online state, scan, SDO execution, or hardware behavior. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five. Qualification used only local/offline Mock state plus offscreen
enabled/disabled product lifecycle runs with passed-through SIGTERM. No
visible main window, remote comparison, fetch, pull, merge, rebase, push, or
publication occurred.

`ISSUE-WB-PROCESS-DATA-SAME-CONTEXT-SELECTION-001`, based on
`f4da0b18cc48077374ba2528091cdb3aeaad99f5`, changes only the existing
product-owned private `processdatapage.cpp`, Workbench test implementation,
and documentation. A same-Project, same-node, same-kind refresh now re-resolves
the selected Sync Manager and PDO stable IDs in fresh models. Missing IDs use
the existing first-row fallback; a retained PDO follows its fresh owning Sync
Manager; a context change clears the old IDs before the existing derived-node
focus rule runs. That derived focus is not reapplied during a same-context
refresh, so a later manual selection remains current.

The change introduces no generic selection service, cross-node cache,
persistent-index contract, repository signal suppression, refresh-reason API,
Project revision or conflict protocol, public API, source file, dependency,
CMake/qbs entry, Project format, persistence field, Provider or ProjectService
contract, Project command, custom model role, production thread or timer, Core
or ProjectExplorer hook, application-bootstrap change, controller/network
transport, online state, scan, SDO execution, or hardware behavior. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five. Qualification used only local/offline Mock state plus offscreen
enabled/disabled product lifecycle runs with passed-through SIGTERM. No
visible main window, remote comparison, fetch, pull, merge, rebase, push, or
publication occurred. Authoritative final evidence is under
`/private/tmp/embed-labs-wb-process-data-selection-refresh-001.jPkuu0/final2`;
the sibling `failure-first/` and `derived-failure/` directories contain the
two red proofs.

`ISSUE-WB-ETHERCAT-ALIAS-NONCONFLICTING-REFRESH-DRAFT-001`, based on
`8c374cb68b2ea5844c6ca5a51cf31ed8b2aa14a1`, changes only the existing
product-owned private `ethercatpage.cpp/.h`, Workbench test
declaration/implementation, and documentation. A modified or focused Alias
draft now survives a same-Project, same-node, same-kind refresh only while the
fresh authoritative Alias still equals the page's last baseline. The Alias
editor state is retained while Type, SyncManager, and other EtherCAT data
refresh. An explicit commit, external Alias change, Project Undo, node switch,
invalid context, and Project close always restore or destroy the draft from
current authority.

The change introduces no duplicate-Alias policy, generic dirty-form or
conflict framework, autosave, cross-node cache, Project revision/generation
or CAS protocol, public API, source file, dependency, CMake/qbs entry, Project
format, persistence field, Provider or ProjectService contract, Project
command, custom model role, production thread or timer, Core or
ProjectExplorer hook, application-bootstrap change, controller/network
transport, ADS, online state, scan, Modules/Channels behavior, fixed address,
Identification, port graph, SDO execution, ESC/EEPROM write, or hardware
behavior. The Workbench path count remains 44 and the direct upstream Core
patch count remains five. Qualification used only local/offline Mock state
plus offscreen enabled/disabled product lifecycle runs with passed-through
SIGTERM. No visible main window, remote comparison, fetch, pull, merge,
rebase, push, or publication occurred. Authoritative final evidence is under
`/private/tmp/embed-labs-wb-alias-draft-refresh-001.F7RkQj/final`; the sibling
`failure-first/` directory contains the red proof.

## Remote comparison status

A later upstream comparison or merge may only be performed after an explicit
user request. Until then, the only supported baseline is the current local
commit and its descendants on `embed-labs`.

## Local keyboard context-menu targeting delta

`ISSUE-WB-NAV-KEYBOARD-CONTEXT-TARGET-001` is a private Workbench correction
on local baseline `38d0d262f972b6bd1f877b6a2306411a1240cbf7`. It preserves the
original `QContextMenuEvent::reason()` before Qt's custom-context-menu signal
can erase that distinction. Mouse hit testing remains unchanged. Keyboard and
other non-mouse events keep the current stable node, use the visible portion
of its row as the anchor, and use the viewport center when that row is outside
the visible viewport.

The behavior follows Qt's documented keyboard context-event and abstract
scroll-area contracts:
<https://doc.qt.io/qt-6/qcontextmenuevent.html> and
<https://doc.qt.io/qt-6/qabstractscrollarea.html>. It also matches Beckhoff's
documented Shift+F10 selected-object convention at
<https://infosys.beckhoff.com/content/1033/tcplccontrol/925416331.html>.
No Beckhoff code, binary interface, engineering format, icon, or branding was
copied.

The local delta changes only `workbenchnavigation.cpp/.h`, the Workbench test
declaration/implementation, and four evidence documents. It introduces no
public API, source file, dependency, build-system entry, Project format,
persistence field, Provider/ProjectService contract, Project command, custom
model role, production thread/timer, direct upstream Core patch, Core or
ProjectExplorer hook, app bootstrap, network, ADS, scan, online state, SDO,
ESC/EEPROM, or hardware behavior. No CMake or qbs description changed. No
remote comparison, fetch, pull, merge, rebase, push, PR, or publication was
performed. The supported baseline remains the local `embed-labs` history.
Authoritative evidence is under
`/private/tmp/embed-labs-wb-nav-keyboard-context-target-001.Du2VdU`.

## Local preferred Diagnostics status projection delta

`ISSUE-WB-STATUS-PREFERRED-DIAGNOSTICS-001` is a private Workbench correction
on local baseline `0524d2dfbb14cacd6f25c584ede4bcce9fe416c4`. The existing
deterministically preferred Diagnostics Provider is copied into a private
value presentation so the status control distinguishes Config/PREOP,
FreeRun/SAFEOP, and Run/OP instead of collapsing every healthy mode to Ready.
Shared `StateService` severity remains authoritative when it is higher. Mock
and Provider-reported source boundaries remain explicit; no status value is
treated as proof of transport, controller, online, or hardware state.

The local delta changes only seven existing private Workbench
implementation/test files and four evidence documents. It introduces no new
path, public API, source file, dependency, build-system entry, Project format,
persistence field, Provider/ProjectService contract, Project command, custom
model role, production thread/timer, direct upstream Core patch, Core or
ProjectExplorer hook, app bootstrap, network, ADS, scan, online transition,
SDO, ESC/EEPROM, or hardware behavior. The Workbench path count remains 44 and
the direct upstream Core patch count remains five. No CMake or qbs description
changed, so qbs was not run. The unrelated `WITH_TESTS=ON` all-target build
was not rerun because the known EasyBoard `extensionmanager_test.h` blocker
remains outside this issue.

Qualification used local/offline Mock and explicitly Provider-reported values
plus offscreen enabled/disabled product lifecycle runs with passed-through
SIGTERM. No visible main window, remote comparison, fetch, pull, merge,
rebase, push, PR, or publication occurred. The supported baseline remains the
local `embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-status-preferred-diagnostics-001.urIOBN`.

## Local DC non-conflicting refresh draft delta

`ISSUE-WB-DC-NONCONFLICTING-REFRESH-DRAFT-001` is a private Workbench
correction on local baseline
`304eaf73d4020b59abba15116868bcb8f50cc9f8`. A modified or focused delayed DC
editor now survives a same-Project, same-node, configured-slave refresh only
while its own fresh authority equals its previous baseline. The six fields
are Operation Mode name, AssignActivate, SYNC0 cycle/shift, and SYNC1
cycle/shift. Immediate enable and reference-clock commands remain unchanged.

Operation Mode model refresh can be deferred with its draft. A subsequent ESI
selection maps the complete visible `DcModeDescription` value into the current
Repository list, so duplicate display names are not treated as identity.
Rebuild likewise disambiguates multiple same-name rows from the Project's
mode-owned values and presents a custom value rather than guessing when none
matches. An explicit commit, no-op normalization, input rejection, external
field change, Project Undo/Redo, node switch, invalid context, and Project
close reload or destroy affected draft state from current authority.

The local delta changes only private `dcpage.cpp/.h`, the Workbench test
declaration/implementation, and four evidence documents. It introduces no new
path, public API, source file, dependency, CMake/qbs entry, Project format,
persistence field, Provider/ProjectService contract, Project command, custom
model role, production thread/timer, direct upstream Core patch, Core or
ProjectExplorer hook, application bootstrap, network, ADS, scan, online
state, SDO, ESC/EEPROM, or hardware behavior. The Workbench path count remains
44 and the direct upstream Core patch count remains five. No CMake or qbs
description changed, so qbs was not run.

This is not a generic dirty-form, autosave, Repository-signal suppression,
cross-node cache, Project revision/generation or CAS protocol, merge/conflict
UI, or operation-mode identity scheme. Qualification used local ESI and
offline Project data plus offscreen enabled/disabled product lifecycle runs;
it did not exercise actual Distributed Clocks synchronization, a real-time
period, controller connection, network transport, or physical hardware.

No visible main window, remote comparison, fetch, pull, merge, rebase, push,
PR, or publication occurred. The supported baseline remains the local
`embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-dc-draft-refresh-001.6ifOHJ`; the failure-first
proof is under its `failure-first/` directory, the duplicate-mode red proof is
under `derived-failure/`, and the isolated intermediate test-harness report is
under `product/quarantined-test-harness-reports/`.

## Local CoE same-context view-state delta

`ISSUE-WB-COE-SAME-CONTEXT-VIEW-STATE-001` is a private Workbench correction
on local baseline `2e0bc521205d130ea2d56fe6b88901185977d8c2`. A non-`None`
CoE page retains page-owned view state only while the Project ID, node ID, and
node kind are all unchanged. Text-filter editor state, Advanced filters,
Mock/offline choice, Mock sample generation, and one scalar selected-object
address can therefore survive a Project or same-identity ESI refresh.

The source model still rebuilds from current Project and Repository data. A
retained address is restored only as a value lookup in the fresh source model;
no `QModelIndex`, model item, Repository object, or Project object is retained.
If a proxy filter temporarily hides the address, its semantic anchor remains;
if the source object is removed, the page adopts the valid fallback or clears
the anchor. A genuine context change resets state and establishes no
cross-node cache.

The private context generation still increments on every refresh, so stale
Advanced and Add-to-Startup responses remain invalid. Feedback is cleared.
That view-state issue did not preserve transient manually edited Mock values;
their separate compatible-authority boundary is defined by
`ISSUE-WB-COE-NONCONFLICTING-MOCK-VALUE-REFRESH-001` below. This is not a
generic view-state service, signal-suppression change, refresh-reason API,
Repository revision, CAS or merge protocol, or persistence mechanism.

The local delta changes only private `coeonlinepage.cpp`, the Workbench test
declaration/implementation, and four evidence documents. It introduces no new
path, header, public API, source file, dependency, CMake/qbs entry, Project
format, persistence field, Provider/ProjectService contract, Project command,
custom model role, production thread/timer, direct upstream Core patch, Core
or ProjectExplorer hook, application bootstrap, network, ADS, scan, online
state, SDO, or hardware behavior. The Workbench path count remains 44 and the
direct upstream Core patch count remains five. No CMake or qbs description
changed, so qbs was not run.

Qualification used local ESI, offline Project, and Mock data plus offscreen
enabled/disabled product lifecycle runs with passed-through SIGTERM. It did
not exercise online CoE, a controller connection, network transport, SDO, or
physical hardware. No visible main window, remote comparison, fetch, pull,
merge, rebase, push, PR, or publication occurred. The supported baseline
remains the local `embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-view-state-001.4vssYQ`.

## Local Startup non-conflicting refresh inline-draft delta

`ISSUE-WB-STARTUP-NONCONFLICTING-REFRESH-DRAFT-001` is a private Workbench
correction on local baseline `a835d3340e0a3d644928adb445a8c916fda17e23`.
One active configured-slave Startup editor survives a same-Project,
same-node, same-kind refresh only while its stable request ID remains present,
editable, non-fixed, and unchanged in the edited field. The model applies
fresh row removal, insertion, movement, and sibling-cell updates by stable ID
without reloading the active cell.

An inline commit remains a checked `ProjectService` command and is protected
only against its synchronous refresh re-entry. External same-field changes,
read-only or source changes, removed requests, node switches, and Project
close discard the local draft and restore current authority. Page teardown
closes and releases an active editor without committing it. There is no
cross-node cache, generic table synchronization service, autosave, Project
revision, CAS, merge, or conflict UI.

The local delta changes only existing private `startuppage.cpp/.h`, the
Workbench test declaration/implementation, and four evidence documents. It
introduces no new path, public API, source file, dependency, CMake/qbs entry,
Project format, persistence field, Provider/ProjectService contract, Project
command, public model role, production thread/timer, direct upstream Core
patch, Core or ProjectExplorer hook, application bootstrap, network, ADS,
scan, online state, SDO, ESC/EEPROM, or hardware behavior. The Workbench path
count remains 44 and the direct upstream Core patch count remains five.

Qualification used local ESI/offline Project values plus offscreen
enabled/disabled product lifecycle runs with passed-through SIGTERM. No
visible/manual UI inspection, remote comparison, fetch, pull, merge, rebase,
push, PR, or publication occurred. The supported baseline remains the local
`embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-startup-draft-001.XNDmkO`.

## Local Process Data non-conflicting refresh inline-draft delta

`ISSUE-WB-PROCESS-DATA-NONCONFLICTING-REFRESH-DRAFT-001` is a private
Workbench correction on local baseline
`31b4fb48bd564caa3ad5bd2a0947aa5e9800b38d`. One active configured-slave PDO
Content editor survives a same-Project, same-node, same-kind refresh only
while its unique stable PDO and entry IDs remain present, the mapping remains
editable, its own field authority is unchanged, and the stored/ESI-proposal
source is compatible.

The model applies fresh row removal, insertion, movement, and sibling-cell
updates by stable entry ID without reloading the active cell. The existing
Project command remains authoritative. The editor's own synchronous first
commit may cross from ESI proposal to stored configuration under a private
scope; unrelated source changes and external same-field conflicts discard the
draft. A deferred post-editor `DisplayRole` update refreshes an automatic Bit
Offset after sibling layout changes. Read-only/fixed state, invalid identity,
removed rows, context changes, Details-page removal, and teardown also use
revert semantics. Ordinary page hiding does not intentionally discard a draft
while the page remains in its Details stack.

The local delta changes only existing private `processdatapage.cpp/.h`, the
Workbench test declaration/implementation, and four evidence documents. It
introduces no new path, public API, source file, dependency, CMake/qbs entry,
Project format, persistence field, Provider/ProjectService contract, Project
command, public model role, production thread/timer, direct upstream Core
patch, Core or ProjectExplorer hook, application bootstrap, network, ADS,
scan, online state, CoE/SDO execution, ESC/EEPROM, PLC, controller, or hardware
behavior. It is not a generic table/draft service, multiple- or
persistent-editor feature, cross-node cache, autosave, Project revision, CAS,
merge, or conflict UI.

Qualification used local ESI/offline Project data plus offscreen
enabled/disabled main-program lifecycle runs with passed-through SIGTERM. No
visible/manual UI inspection, remote comparison, fetch, pull, merge, rebase,
push, PR, or publication occurred. The supported baseline remains the local
`embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-process-data-draft-001.lx7VcX`.

## Local CoE non-conflicting Mock-value refresh delta

`ISSUE-WB-COE-NONCONFLICTING-MOCK-VALUE-REFRESH-001` is a private Workbench
correction on local baseline
`925b14062f93403cfc9f9552dcaec1e3015ba662`. An accepted Mock override can
survive only within the same Project, configured-slave node, and node kind,
at the same scalar object address, while offline bytes and width, parsed and
raw type, writable authority, and process-data role remain compatible.

Project rename and same-identity ESI metadata or sibling refreshes preserve
eligible bytes while rendering fresh authority. Offline view exposes baseline
bytes and returning to Mock exposes the override. Add to Startup consumes the
current bytes through its existing checked command path. Explicit Update List
is the clear boundary in both Mock and Offline view. Empty offline baselines
can retain an already accepted non-empty arbitrary width, but Update List
returns them to empty. Authority conflict, object removal, context change, and
page teardown discard the override. Uncommitted editor text was excluded from
that delta and is now covered by the separate local inline-draft delta below.

This is an existing-path, cpp-private value replay, not an upstream Core
change, generic cache, Repository revision, merge/CAS protocol, persistence
format, or public interface. The Workbench path count remains 44 and the
direct upstream Core patch count remains five. The local delta changes only
existing `coeonlinepage.cpp/.h`, the Workbench test declaration/implementation,
and four evidence documents. It introduces no source file, dependency,
CMake/qbs entry, Project format or persistence field, Provider/ProjectService
contract, Project command, public model role, production thread/timer, Core or
ProjectExplorer hook, application bootstrap, network, ADS, scan, online CoE,
SDO, controller, or hardware behavior.

Qualification used local ESI/offline Project and Mock data. Focused and
related CoE tests passed at normal and 2x scale; two complete Workbench runs at
each scale passed 77 events per run; the six isolated suites passed 128 events.
The complete `WITH_TESTS=OFF` product build passed with 16 plugin dylibs.
Enabled and explicitly disabled main-program runs each stayed alive for 37
samples, passed intentional SIGTERM through as status 15, left no residual
process, and produced no new matching diagnostic report or crash-service
event. All execution was offscreen with fresh HOME/settings and disabled crash
reporting; no visible/manual UI inspection was run.

No remote comparison, fetch, pull, merge, rebase, push, PR, or publication
occurred. The supported baseline remains the local `embed-labs` history.
Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-mock-value-refresh-001.Z9NXQ5`.

## Local CoE non-conflicting refresh inline-draft delta

`ISSUE-WB-COE-NONCONFLICTING-INLINE-DRAFT-REFRESH-001` is a private
Workbench correction on local baseline
`d26f695ec7581b7864e87577d0b935997a30546d`. One active configured-slave CoE
Value editor survives a same-Project, same-node, same-kind refresh only while
the final target retains the same writable non-synthetic object address,
offline and Mock bytes, parsed/raw type, process-data role, edited-state
provenance, active-root child count, and continued visibility through the
current proxy filter. If refreshed metadata filters the row out, editor
preservation is outside this delta.

The private model builds the final target dictionary before deciding whether
to synchronize. Compatible roots and children use address-based row-removal
and row-insertion notifications; matching items remain allocated and the
active Value column is omitted from data-change notifications. A defensive
move path is present but is neither reached by sorted production definitions
nor qualified by this delta. Fresh sibling and non-Value metadata still
render. Escape retains default revert semantics. Return remains inside the
page-local Mock boundary but is now checked by the cpp-local delegate: valid
input is accepted, while rejected input preserves accepted bytes and presents
local feedback. A Mock edit never mutates the Project.

Update List, Show Offline, authority conflict or read-only transition, object
removal, node/context change, and page teardown revert the editor and use the
normal reset/authority path. The page destructor prevents pending text from
being committed during deferred editor deletion. This is not a multiple- or
persistent-editor feature, generic tree synchronizer, cross-node cache,
autosave, persistence, revision, CAS, merge, or conflict UI.

The local delta changes only existing private `coeonlinepage.cpp/.h`, the
Workbench test declaration/implementation, and four evidence documents. It
introduces no new path, public API, source file, dependency, CMake/qbs entry,
Project format or persistence field, Provider/ProjectService contract,
Project command, public model role, production thread/timer, direct upstream
Core patch, Core or ProjectExplorer hook, application bootstrap, network, ADS,
scan, online CoE, SDO, ESC/EEPROM, PLC, controller, or hardware behavior. The
Workbench path count remains 44 and the direct upstream Core patch count
remains five.

Qualification uses local ESI/offline Project and Mock state plus offscreen
tests and enabled/disabled main-program lifecycle runs with passed-through
SIGTERM. Focused 1x/2x tests passed, four complete Workbench runs each passed
78 events, and the six isolated EtherCAT suites passed 129 events. The full
`WITH_TESTS=OFF` product build contains 16 plugin dylibs. Enabled and explicit
`-noload EtherCATWorkbench` runs each remained alive for 37 of 37 samples;
Workbench was loaded in all enabled samples and no disabled sample. Both ended
with expected target status 15, with zero residual processes, new Embed Labs
diagnostic reports, or matching crash-service events. No visible/manual UI
inspection, remote comparison, fetch, pull, merge, rebase, push, PR, or
publication is part of this delta. The supported baseline remains the local
`embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-inline-draft-001.177q7M`.

## Local CoE Mock edit-rejection feedback delta

`ISSUE-WB-COE-MOCK-EDIT-REJECTION-FEEDBACK-001` is a private Workbench
correction on local baseline
`8a0373032093f05ba57af57d70a8cb1bfae404e4`. The cpp-local CoE model now
shares one detailed parser/validation result with the checked delegate Return
path. Empty, incomplete-byte, illegal-hex, and fixed-width input failures
leave accepted Mock bytes unchanged and appear on the page-owned CoE feedback
surface after the transient editor closes.

Reopening the same object and Escape retain current guidance. Valid Return or
an explicit selection, source, Update List, context, Repository, or teardown
boundary clears it. Valid normalized input still commits once through the
private source model, empty baselines retain their established arbitrary-width
rule, and Mock editing never mutates the Project or its Undo stack. Feedback
metadata remains accessible; a polite announcement event is conditional on
Qt accessibility support and does not imply manual VoiceOver or speech
verification.

The local delta changes only existing private `coeonlinepage.cpp/.h`, the
Workbench test declaration/implementation, and four evidence documents. It
introduces no new path, public API or role, source file, dependency,
CMake/qbs entry, Project format or persistence field, Project mutation,
Modules or Channels chain, Provider/ProjectService contract, Project command,
production thread or timer, direct upstream Core patch, Core or
ProjectExplorer hook, application bootstrap, network, ADS, scan, online CoE,
SDO, controller, PLC, or hardware behavior.

Final focused 1x/2x and related CoE 1x/2x tests passed 3 and 11 events per
run. Four complete Workbench runs passed 79 events each, and the six isolated
EtherCAT suites passed 130 events. The `WITH_TESTS=OFF` Workbench and complete
product builds passed with exactly 16 plugin dylibs. Invisible enabled and
explicit `-noload EtherCATWorkbench` runs each remained alive for 37 samples,
with the plugin loaded in 37 and 0 samples respectively; both ended by intentional
passed-through SIGTERM with expected status 15. The final audit found zero
residual processes, matching diagnostic reports, or crash-service events.

No visible/manual UI inspection, online/SDO/controller/network/hardware
exercise, remote comparison, fetch, pull, merge, rebase, push, PR, or
publication is part of this delta. No CMake or qbs description changed, so
qbs was not run. The supported baseline remains the local `embed-labs`
history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-coe-edit-feedback-001.vl5Suz`.

## Local visible EtherCAT identity filter delta

`ISSUE-WB-NAV-FILTER-VISIBLE-IDENTITY-001` is a private Workbench correction
on local baseline `c6f48495263f38a292a780f0955468bf183c692a`. The device
search projection now mirrors the zero-padded `0x........` Vendor, Product,
and Revision values already exposed by the tooltip. A user can paste a
visible Product value such as `0x0000102a` into the navigation filter without
falling into the no-match state. Former unprefixed searches remain valid.

The local delta changes one existing format string in
`workbenchtreemodel.cpp`, one focused Workbench test declaration and
implementation, and four evidence documents. It introduces no new path,
public API or role, source file, dependency, CMake/qbs entry, Project format
or persistence field, Project or Repository mutation, Provider/ProjectService
contract, Project command, direct upstream Core patch, Core or
ProjectExplorer hook, application bootstrap, production thread or timer,
network, ADS, scan, online state, CoE/SDO, PLC, controller, or hardware
behavior.

Focused and navigation-related tests passed at normal and 2x scale. Four
complete Workbench runs passed 80 events each, and the six isolated EtherCAT
suites passed 131 events. The `WITH_TESTS=OFF` Workbench and complete product
builds passed with exactly 16 plugin dylibs. Enabled and explicit
`-noload EtherCATWorkbench` product runs each remained alive for 37 samples;
Workbench was loaded in 37 and 0 samples respectively. Both ended by
intentional passed-through SIGTERM with expected status 15, with zero
residual process, new matching diagnostic report, or crash-service event.

No visible/manual UI inspection, remote comparison, fetch, pull, merge,
rebase, push, PR, or publication is part of this delta. No CMake or qbs
description changed, so qbs was not run. The supported baseline remains the
local `embed-labs` history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-nav-visible-identity-001.2G4bre`.

## Local blank-area context-menu delta

`ISSUE-WB-NAV-MOUSE-BLANK-CONTEXT-001` is a private Workbench correction on
local baseline `142225dc80ec87032f110d96e4408f425477ff65`. A real mouse request
that lands inside unused tree viewport space now uses an empty menu context
instead of the old current node. The stable selection remains untouched, but
node-specific commands are absent and disabled while the popup exists, then
restored from the unchanged selection when it closes.

Valid mouse hits continue to select their stable node. Keyboard events and the
existing out-of-viewport `(-1, -1)` direct-signal convention continue to use
the current node. Expand, Collapse, and tree-wide actions remain available on
blank space. The implementation follows Qt's separate hit-test/current-index
contracts at <https://doc.qt.io/qt-6/qabstractitemview.html#indexAt> and
<https://doc.qt.io/qt-6/qabstractitemview.html#currentIndex-prop>, and matches
the selected-object scope in Beckhoff's documented Add New Item workflows
without importing its code, formats, assets, branding, or communication stack.

The local delta changes only `workbenchnavigation.cpp`, the existing Workbench
test implementation, and four evidence documents. It introduces no public API
or model role, source file, dependency, CMake/qbs entry, Project or Repository
mutation, Project format, persistence field, Provider/ProjectService contract,
Project command, direct upstream Core patch, Core or ProjectExplorer hook,
application bootstrap, production thread or timer, network, ADS, scan, online
state, CoE/SDO, PLC, controller, or hardware behavior.

Focused and related tests passed at normal and 2x scale. Four complete
Workbench runs passed 80 events each, and the six isolated EtherCAT suites
passed 131 events. The `WITH_TESTS=OFF` Workbench and complete product builds
passed with 16 plugin dylibs. Enabled and explicit-disabled product runs each
remained alive for 37 samples, with Workbench mapped in 37 and 0 samples. Both
ended with expected status 15 and produced no residual process, new matching
diagnostic report, or matching crash-service event.

No visible/manual UI inspection, remote comparison, fetch, pull, merge,
rebase, push, PR, or publication is part of this delta. No CMake or qbs
description changed. The supported baseline remains the local `embed-labs`
history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-nav-blank-context-001.oYIrwR`.

## Local General name-rejection feedback delta

`ISSUE-WB-GENERAL-RENAME-REJECTION-FEEDBACK-001` is a private Workbench
correction on local baseline
`6d13931b6d595b4c710227dc0aa7fa0a4584c104`. Empty Project, offline Target,
Master, and configured Slave name submissions still fail through the existing
Workbench controller and Project-service paths and restore their authoritative
values, but the active General page now presents the existing contextual error
instead of relying only on General Messages.

New input clears the page-owned error label, and a successful path remains
clear; refresh, context change, Project close, and teardown clear or destroy
it. The label mirrors its visible error through accessible description and
tooltips and conditionally requests one polite accessibility announcement.
This does not change Project validation, accepted values, Undo/Redo,
selection, persistence, or any public service contract, and it does not claim
manual VoiceOver or audible speech verification.

The local delta changes only existing `generalpage.cpp/.h`, the Workbench test
declaration/implementation, and four documents. It introduces no new source
path, public API or model role, dependency, CMake/qbs entry, Project format or
persistence field, Provider/ProjectService contract, Project command, direct
upstream Core patch, Core or ProjectExplorer hook, application bootstrap,
production thread or timer, network, ADS, scan, online state, CoE/SDO,
controller, PLC, or hardware behavior.

The authoritative red test kept both production files at the baseline hashes,
failed on the absent page feedback, and exited 1. An initial typed-child compile
failure and malformed test selector were superseded harness attempts. Focused
and related normal/2x tests, four complete Workbench runs, and all six isolated
suites passed; the isolated total is 132. A parallel Scan exit-handshake stall
was superseded by its sequential seven-pass status-0 run.

The `WITH_TESTS=OFF` Workbench and complete product builds passed with exactly
16 plugin dylibs. Enabled and explicit-disabled invisible product runs each
stayed alive for 37 samples, with Workbench mapped in 37 and 0. Both ended with
expected status 15. A fail-closed system-log query and explicit before/after
DiagnosticReports difference found no matching crash event, new Embed Labs
report, or residual process.

No visible/manual UI inspection, online/SDO/controller/network/hardware
exercise, remote comparison, fetch, pull, merge, rebase, push, PR, or
publication is part of this delta. No CMake or qbs description changed, so
qbs was not run. The supported baseline remains the local `embed-labs`
history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-general-rename-feedback-001.M4aWr4`.

## Local Mock Scan owning-Project lifecycle delta

`ISSUE-WB-SCAN-PROJECT-LIFECYCLE-001` is a product-owned correction on local
baseline `e1855127275d89914626a632674e08cb88109d98`. The private Mock Scan
provider now discards a request and all transient scan state when the Project
identified by `ScanRequest::projectId` closes. Closing a different Project is
ignored. Active work is cancelled before clearing; Completed, Failed, and
Cancelled states reuse the same clear path and end at `Idle`.

Existing Provider signals remove the local result from the Scan difference
page, ActionManager commands, shared status, and Workbench presentation. A
same-ID Project reopen stays clean and supports a new explicit Mock scan. The
change does not mutate Project data, persistence, or Undo/Redo and does not
introduce a cross-plugin object pointer or model index.

The local source delta is limited to:

- `src/plugins/ethercatscan/mockscanprovider.cpp`;
- the existing `ethercatscantests.cpp/.h` declaration and implementation;
- the four required evidence documents and directly related
  `docs/ethercat-scan.md`.

No file in Qt Creator Core, ProjectExplorer, or the application bootstrap is
modified. The direct Core patch count remains five. There is no new public
API or Provider method, Project format or command, dependency, source file,
thread, timer, network transport, controller protocol, build-system entry, or
upstream integration surface. CMake/qbs stay unchanged.

Failure-first preserved the three relevant production hashes, reproduced
`Completed(6)` after owner close instead of `Idle(0)`, and exited 1 after
passing initialization and cleanup. Final focused and related normal/2x runs,
four complete Workbench runs, and all six isolated suites passed; the isolated
total is 136. The `WITH_TESTS=OFF` Scan and full product builds passed with 16
plugin dylibs.

Invisible enabled and explicit `-noload EtherCATWorkbench` product runs each
stayed alive for 37 samples. Workbench and Scan mapped in 37/37 and 0/37
samples respectively, and each run ended with expected status 15. The
fail-closed audit found zero residual process, new Embed Labs diagnostic
report, or matching crash-service event. No visible UI, real interface,
network, ADS, online scan, SDO, controller, PLC, Zynq, or hardware execution
was used or claimed.

No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was
performed. The supported source of truth remains the local `embed-labs`
history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-scan-project-lifecycle-001.9fVQNz`.

## Local Process Data edit-rejection feedback delta

`ISSUE-WB-PROCESS-DATA-EDIT-REJECTION-FEEDBACK-001` is a private Workbench
correction on local baseline `037bd681e7ce17bd665d345f83a64b49a28a2428`.
Invalid real-editor submissions for PDO Content Index, Subindex, Bits, Bit
Offset, and Name now explain the accepted syntax or range in the existing
Process Data validation strip. Parsed candidates that fail full configuration
validation report its first error with `Change not applied.`.

The table model keeps only a private one-shot result. Real delegates consume
it and ask the page to present feedback; rejected direct programmatic
`setData()` publishes no feedback or announcement, while valid direct writes
retain their existing Project path. Rejection leaves Project data, Undo/Redo,
accepted model values, and notifications unchanged. The page mirrors the
visible error through accessible description and tooltips and requests one
optional polite announcement. Selection, successful submission, refresh, and
teardown clear the transient state.

The delta also gives the Process Data Type delegate a unique
`ProcessDataTypeDelegate` name. A pre-final LLDB/MallocScribble run proved that
its former `DataTypeDelegate` name collided with Startup's different class at
link time and could apply the wrong destructor during page teardown. The
rename produces separate local symbols and modifies no Startup source or
public contract.

The local source delta is limited to:

- `src/plugins/ethercatworkbench/processdatapage.cpp/.h`;
- the existing Workbench test declaration and implementation;
- the four required evidence documents.

There is no new file, public API or role, dependency, Project format or
persistence field, Provider/ProjectService contract, Project command, direct
upstream Core patch, Core/ProjectExplorer hook, application bootstrap,
production thread or timer, network, ADS, scan, online state, CoE/SDO, PLC,
controller, or hardware behavior. CMake and qbs descriptions are unchanged.

Failure-first reproduced the unexplained blank-Name Return path and exited 1
while production hashes stayed fixed. After the minimal implementation and
delegate-name correction, focused and related tests passed at normal and 2x
scale; four complete Workbench runs passed 82 events each, and the six
isolated EtherCAT suites passed 137 events. The Qt 6.11.0 `WITH_TESTS=OFF`
Workbench and complete product builds passed with exactly 16 plugin dylibs.

Invisible enabled and explicit `-noload EtherCATWorkbench` product runs each
stayed alive for 37 samples. Workbench and Scan mapped in 37/37 and 0/37
samples respectively, and each ended with expected status 15. The fail-closed
audit found zero residual process, new Embed Labs diagnostic report, or
matching crash-service event. No visible UI, real interface, network, online
SDO, controller, PLC, Zynq, or hardware execution was used or claimed.

Qt's model, delegate, and announcement references are
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>,
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's
Process Data page is referenced only for PDO terminology at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.
No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was
performed. The supported source of truth remains the local `embed-labs`
history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-process-data-edit-feedback-001.seBJey`.

## Local Startup edit-rejection feedback delta

`ISSUE-WB-STARTUP-INLINE-EDIT-REJECTION-FEEDBACK-001` is a private Workbench
correction on local baseline `79b654f5cd57116e42777bb2f09559b87bc98c52`.
Invalid real-editor submissions in the Startup table now explain their exact
local parse or complete-configuration error in the existing validation strip.
This corrects the prior `Data=0G` path, which restored the accepted value but
reported the unrelated even-digit error.

The table model stores a private one-shot result. Real line, combo, spin, and
checkbox delegate transactions consume it; rejected direct programmatic
`setData()` publishes no page feedback or announcement. Valid direct writes
retain the existing Project command path. Rejection preserves accepted model
values, the complete Project snapshot, Undo/Redo, current cell, and model and
Project notifications.

The page reuses its existing `Utils::InfoLabel`, mirrors the error through its
accessible description and both tooltips, and conditionally requests one
Polite announcement. Selection, a successful same-cell edit, Undo/Redo,
same-context refresh, and Project close restore or destroy the transient
state. The modal New/Edit Startup dialog is not changed by this issue.

The local source delta is limited to:

- `src/plugins/ethercatworkbench/startuppage.cpp/.h`;
- the existing Workbench test declaration and implementation; and
- the four required evidence documents.

There is no new file, public API or role, dependency, Project format or
persistence field, Provider/ProjectService contract, Project command, direct
upstream Core patch, Core or ProjectExplorer hook, application bootstrap,
production thread or timer, network, ADS, scan, online state, CoE/SDO, PLC,
controller, Zynq, or hardware behavior. CMake and qbs descriptions are
unchanged; the existing direct Core patch count does not change.

Failure-first retained both production Startup hashes, reproduced the wrong
message through a real editor, and exited 1. Final focused and Startup-related
tests passed at normal and 2x scale; four complete Workbench runs passed 83
events each, and the six isolated EtherCAT suites passed 138 events. The Qt
6.11.0 `WITH_TESTS=OFF` Workbench and complete product builds passed with
exactly 16 plugin dylibs.

Invisible enabled and explicit `-noload EtherCATWorkbench` product runs each
stayed alive for 37 samples. Workbench mapped in 37/37 and 0/37 samples,
respectively, and each ended with expected status 15. The fail-closed audit
found zero residual process, new Embed Labs diagnostic report, or matching
crash-service event. Fresh HOME/settings, offscreen Qt, disabled crash
reporting, cleared DYLD variables, `-no-crashcheck`, and the process-local
Touch Bar bypass kept main-program acceptance invisible; no visible/manual UI
or VoiceOver inspection was performed.

Qt's model, delegate, and announcement references are
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>,
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's
Startup page is referenced only for ordered-request and fixed-item terminology
at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.
No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was
performed. The supported source of truth remains the local `embed-labs`
history. Authoritative evidence is under
`/private/tmp/embed-labs-wb-startup-edit-feedback-001.QUiKKW`.

## Distributed Clocks edit-rejection feedback delta

`ISSUE-WB-DC-EDIT-REJECTION-FEEDBACK-001` adds one bounded local delta to the
private EtherCAT Workbench DC page:

- the validation strip has a stable accessible name;
- rejected Operation Mode, AssignActivate, timing, and checkbox candidates
  publish the concise summary visibly and the complete reason set on accessible
  and tooltip surfaces;
- multi-error candidates retain the concise visible summary and append every
  remaining reason to accessible, tooltip, and announcement feedback;
- one conditional Polite announcement is requested per rejection;
- user correction and existing authoritative reload paths clear the transient
  feedback without changing accepted Project state.

The failure-first executable retained unchanged production hashes and failed
only the missing accessible-name assertion with target status 1. Final focused
and DC-related runs passed at normal and 2x scale; the complete Workbench suite
passed twice per scale with 84 events per run. Six isolated suites passed 139
events with no failures. The `WITH_TESTS=OFF` Workbench and full product builds
passed with 16 plugin dylibs. Enabled and explicit-disabled final product runs
were alive for 37/37 samples, with Workbench mapped 37 and 0 times respectively;
artifact identity remained unchanged and crash-dialog audit counts were zero.
An independent staged review found and corrected an intermediate loss of
second-and-later validation reasons; the final two-error test and every
affected qualification gate were rerun after that correction.

The delta changes `dcpage.cpp`, `dcpage.h`, the existing Workbench test source
and header, and the four qualification documents. It does not change CMake or
qbs, public API/model roles, EtherCATData, EtherCATCore, EtherCATProject,
Project format/persistence, Provider contracts, commands, ProjectExplorer,
application bootstrap, networking, scan, ADS, SDO, PLC, controller, or hardware
behavior. No remote comparison or import was performed; local baseline
`cf697da20467541b7d01e636e8ae2267969ae24a` is authoritative.

The final evidence intentionally makes no claim for visible UI inspection,
manual VoiceOver/audible output, real EtherCAT Distributed Clocks behavior, or
physical hardware. It qualifies only the offline editor feedback and the
existing Mock regression boundary. Evidence is under
`/private/tmp/embed-labs-wb-dc-edit-feedback-001.SmAI4Q`.

Qt references:

- <https://doc.qt.io/qt-6/qlineedit.html#editingFinished>
- <https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>
- <https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>

Beckhoff terminology reference:

- <https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>

## Local navigation native-Find delta

`ISSUE-WB-NAV-NATIVE-FIND-001` adds one bounded local Workbench integration on
baseline `9d33e6535bcb140b1fb0e2d2b66e61e7797c3bf6`:

- the existing left tree is hosted in Qt Creator's standard
  `Core::ItemViewFind` searchable wrapper;
- native Find walks visible Name/Status cells and propagates a match through the
  existing stable-`NodeId` selection path;
- the permanent Workbench filter remains independent and keeps its broader
  private identity/detail matching semantics;
- structural proxy-model changes clear the incremental search anchor before an
  index can become stale; and
- a private finder suppresses the inherited, locally broken Whole Words flag
  without changing Core; and
- navigation destruction releases both finder and tree without a residual
  object.

There is no new shortcut, ActionManager command, Locator integration,
replacement support, or search-history implementation. The wrapper consumes an
existing exported Core API and follows the local AutoTest precedent. No direct
Core, ProjectExplorer, or application patch was made. The source delta is
limited to `workbenchnavigation.cpp/.h`, the existing Workbench test source and
header, and these four qualification documents. CMake and qbs already express
the Core dependency and did not change.

Failure-first retained unchanged production hashes and failed only because the
tree had no aggregated `IFindSupport`, with target status 1. Final focused and
ten-test navigation groups passed at normal and 2x scale. The complete
Workbench suite passed twice per scale with 85 events per run. Six isolated
suites passed 140 events, and the `WITH_TESTS=OFF` complete product built with
exactly 16 plugin dylibs.

The focused test also triggers the registered current-document Find command and
verifies that Core attaches its visible `findEdit` to the local placeholder. A
three-device reorder distinguishes a reset incremental anchor from a stale
ordinary `QModelIndex`. One initial concurrent Diagnostics run emitted two
timer-thread teardown warnings; two isolated reruns and the final sequential
six-suite run passed without either warning, and no Diagnostics source changed.

Enabled and explicit `-noload EtherCATWorkbench` product starts remained alive
for 37/37 samples, with Workbench mapped in 37 and 0 samples respectively.
Both were intentionally terminated through passed-through SIGTERM status 15.
Artifact identity remained fixed, and residual-process, new-diagnostic-report,
and matching crash-service-event counts were all zero. The acceptance used
fresh HOME/settings, offscreen Qt, disabled crash reporting, cleared inherited
DYLD variables, `-no-crashcheck`, and a process-local Touch Bar bypass.

The delta adds no public API/model role, source list, plugin dependency, Project
format or persistence, Provider contract, Project command, thread, timer,
future, network, ADS, scan, online CoE/SDO, PLC, controller, Zynq, or hardware
behavior. It qualifies only local Mock/offline navigation. No visible/manual
Find-toolbar or VoiceOver inspection and no real EtherCAT or hardware execution
is claimed. No remote comparison, fetch, pull, merge, rebase, push, PR, or
publication was performed. The local `embed-labs` history remains authoritative.
Evidence is under `/private/tmp/embed-labs-wb-nav-native-find-001.i4nnnB`.

Qt Creator references:

- <https://doc.qt.io/qtcreator/creator-editor-finding.html>
- <https://doc.qt.io/qtcreator/creator-how-to-view-output.html>
- <https://code.qt.io/cgit/qt-creator/qt-creator.git/tree/src/plugins/coreplugin/find/itemviewfind.h?h=20.0>

Beckhoff terminology reference:

- <https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>

## Bilingual and compact-navigation delta

`ISSUE-WB-I18N-COMPACT-NAV-001` adds one bounded product-language and
Workbench-presentation delta on local baseline
`2af6b18704b13f9b2cb2a06af18b4dfcfb658c7d`:

- the existing `qtcreator_zh_CN.ts` gains complete translations for the six
  product-owned EtherCAT contexts and all 1249 current unique source messages;
- the Workbench tree stores parallel full and compact status presentation;
- the status column displays the compact form while tooltip, accessibility,
  status, identity, and Provider detail surfaces retain complete information;
- both tree columns share the available viewport and right-elide long English
  or Chinese text instead of expanding to content width;
- Filter and native Find share one column-zero private search corpus containing
  name, full/compact status, identity, and detail-only text; and
- stable-`NodeId` selection, Project snapshots, Providers, and every existing
  Mock/offline capability boundary remain unchanged.

The language selector, persisted override, translator installation, and restart
prompt are existing upstream Qt Creator behavior. No file under `src/app`,
`src/plugins/coreplugin`, or `src/plugins/projectexplorer` changed, so the
direct Core patch count and application-bootstrap delta do not increase.

The source delta is limited to the existing shared Chinese translation file,
four private Workbench implementation/test files, and the four qualification
documents. It adds no public API, plugin dependency, source list, Project
format, persistence field, command, Provider contract, thread, timer, network,
ADS, real scan, online CoE/SDO, controller, PLC, Zynq, or hardware behavior.
CMake and qbs descriptions remain unchanged.

Failure-first, normal/2x focused testing, the 85-event complete Workbench suite,
the 140-event sequential six-plugin regression, translation extraction and
compilation, and the Qt 6.11.0 `WITH_TESTS=OFF` product build passed. The
generated product has 16 plugin dylibs and a Simplified Chinese catalogue that
round-trips all six EtherCAT contexts.

At the user's explicit request, the normal product was not launched during this
issue. Visible language switching and enabled/disabled lifecycle smoke for the
current artifact are intentionally not claimed. No remote comparison, fetch,
pull, merge, rebase, push, PR, or publication was performed. Evidence is under
`/private/tmp/embed-labs-i18n-compact`.

## Controller connection Core/API delta

`ISSUE-CORE-CONTROLLER-CONNECTION-API-001` starts from local baseline
`06538be209e26ff0b8a4b8ad4f231a7d15c0631b`. It adds one product-owned
`EtherCATData` header, synchronized Data CMake/qbs entries, one appended
EtherCATCore Provider kind and abstract Provider, metatype registration,
existing Core contract-test coverage, and documentation.

The public values contain decoded connection semantics only: stable
Project/Master scope, endpoint, three-channel/session state, read-only
controller/capability/package/firmware summaries, heartbeat freshness, and
source-classified errors. They contain no socket, ECAP frame, message ID, CRC,
wire offset, background worker, network policy, or controller implementation.

No path under upstream `src/plugins/coreplugin`, ProjectExplorer, or `src/app`
changes. The direct Core intrusion count remains five. The product still
contains 16 plugin dylibs because the concrete `EtherCATProductApi` plugin is a
later issue. No Project format, Workbench page, command, scan, diagnostic
source, controller state, or hardware behavior changes in this prerequisite.

The new Data header is listed identically in CMake and qbs. qbs execution
remains unavailable, so synchronization is source-reviewed rather than claimed
as an executed qbs build. No remote comparison, fetch, pull, merge, rebase,
push, PR, or publication is performed.

The focused Core suite passed 18 events and the sequential six-plugin
regression passed 141. The `WITH_TESTS=OFF` product built 16 plugin dylibs.
Enabled and `-noload EtherCATCore` offscreen samples remained alive 10/10 with
Core mapped 10/10 and 0/10 respectively; both exited through the intentional
SIGTERM target status 15 with no residual qualification process or new
Embed Labs DiagnosticReport.

## Multi-vendor controller Provider/Profile API delta

`ISSUE-CORE-CONTROLLER-PROVIDER-PROFILE-002` starts from local baseline
`a6bcd2dfe813e16ccc62fd61afa6b41473b6542e`. It revises only the existing
product-owned EtherCATData connection values, EtherCATCore abstract Provider,
metatype registration, fake contract tests, and product documentation.

The revision removes Product-API-specific host plus Control/Push/Bulk ports
from the cross-plugin request. A concrete vendor Provider now owns stable
connection profiles and all private endpoint configuration. The common request
contains only a Project/Master scope and profile ID; the snapshot contains a
redacted endpoint summary and arbitrary provider-named channels. Two fake
providers exercise coexistence, safe Provider-scoped profile-ID reuse,
unknown-profile rejection, non-TCP summaries, availability and profile
configuration/support gates, arbitrary channel counts, and independent
removal. The public text contract requires every profile, channel, and error
diagnostic to omit secrets.

No path under upstream `src/plugins/coreplugin`, ProjectExplorer, or `src/app`
changes. `ProviderRegistry` is unchanged, the direct Core intrusion count
remains five, and no concrete adapter, Qt Network dependency, socket, protocol
frame, page, action, Project format, Scan/Diagnostics source, controller
connection, or hardware operation is added. No CMake/qbs source list changes,
so their paired descriptions remain synchronized and qbs is not run.

Failure-first compilation, the 18-event focused Core suite, the 141-event
sequential six-plugin regression, and the complete Qt 6.11.0
`WITH_TESTS=OFF` 16-plugin product build passed. Enabled and
`-noload EtherCATCore` offscreen lifecycle samples remained alive 10/10 with
Core mapped 10/10 and 0/10 respectively. Both intentional SIGTERM exits
returned status 15, with no matching residual process or new Embed Labs
DiagnosticReport. Evidence is under
`/private/tmp/embed-labs-i18n-compact/controller-profile-v2`.

## Historical read-only Product API adapter delta

`ISSUE-ONLINE-PRODUCTAPI-READONLY-ADAPTER-001` adds the product-owned
`EtherCATProductApi` plugin as the first concrete implementation of the
generic `ControllerConnectionProvider` contract. The plugin depends on
EtherCATData, EtherCATCore, Utils, and Qt Network; its CMake and qbs
descriptions list the same target, sources, and dependencies.

This section records that issue's original read-only scope and dated
qualification. The typed controller-control lifecycle delta below supersedes
its control exclusions.

The adapter owns only the Embed Labs Product API v1.9 protocol. ECAP framing,
CRC32C, numeric message and status values, Control/Push/Bulk sockets, endpoint
defaults, SessionId/BootId/RequestId/Sequence correlation, timeout, retry, and
resume behavior stay private to that plugin. A later controller family uses a
separate adapter plugin and globally unique Provider ID. EtherCATCore and
Workbench gain no vendor enum, protocol switch, fixed channel count, host,
port, credential, or byte-layout dependency.

The current outbound policy is a closed read-only allow-list: HELLO on each
required channel, GetState, GetCapability, GetPackageState, capability-gated
GetFirmwareState, and feature-gated ResumeEvents. The adapter sends no control
lease, control heartbeat, discovery, SDO/PDO, state transition, package
mutation, firmware mutation, or other write request. Connect and Refresh
therefore cannot scan the bus or mutate the offline Project.

Control creates one Product API session and Push/Bulk join it. The current
recovery policy does not reconnect an auxiliary socket independently: loss of
Push or Bulk invalidates the local semantic generation, closes all three
channels, and starts a bounded full-session resume sequence across Control,
Push, and Bulk. A matching resumed SessionId/BootId retains the last alarm
checkpoint so replay continues from the last accepted sequence; replay
rejection, a live sequence gap, a new identity, terminal failure, explicit
Disconnect, or shutdown clears it. That policy is ProductApi-private and does
not constrain a future Provider whose protocol has one channel, another
channel layout, or no session-resume concept.

The delta adds no Workbench page, action, Project format field, Scan provider,
Diagnostics provider, controlled discovery, or path under upstream
`src/plugins/coreplugin`, ProjectExplorer, or `src/app`. Local codec and
loopback checks are Mock protocol evidence. Final local qualification passed
36 focused ProductApi events and 177 sequential events across the seven
EtherCAT suites. Translation validation covered 1,301 messages with no
unfinished or empty entries, and the complete Qt 6.11.0 `WITH_TESTS=OFF`
product build passed with exactly 17 plugin dylibs. Enabled and
`-noload EtherCATProductApi` offscreen lifecycle checks stayed alive for 10/10
samples with the adapter mapped 10/10 and 0/10 respectively, then left no
matching process or new Embed Labs DiagnosticReport after intentional
termination. These results do not establish that the Qt adapter connected to
the real controller; that hardware qualification remains a later,
explicitly authorized issue.

## Typed controller-control lifecycle delta

The current local extension widens only product-owned EtherCAT semantic and
adapter surfaces. `EtherCATData` adds typed control request/progress and
linear topology values. `EtherCATCore::ControllerConnectionProvider` adds an
opt-in command-support query and typed execution entry point whose default
rejects control. No Product API numeric message, ECAP frame, socket, endpoint,
or vendor enum enters the shared API.

`EtherCATProductApi` privately adds Acquire/Release/lease Heartbeat,
Configuration, DiscoverTopology, exact active-package restore, Product API
v1.10 `StartFreeRun (0x010c)` and `StartDc (0x010d)`,
Pause/Resume/ControlledStop, ordered response-stage validation,
command-postcondition refresh, and safe disconnect handling. Legacy
`Start (0x0102)` remains the automatic-mode request on v1.10 and older
runtimes. Workbench uses it through Qt Creator's native lower-left Run
control. The Communication page embeds connection, automatic/manual Acquire,
Configuration, Scan, Restore/Release, progress, read-only Actual Bus, and
authoritative information. It has no independent FreeRun, DC Run, Pause,
Resume, or Stop buttons. Debug maps Running to Pause and Paused to Resume; Run
maps OP_SAFE to Start and Paused to Resume; Controlled Stop is added beside
those native controls. This delta does not add a standalone window, change the
Project format, or mutate offline topology after a scan.

After the authoritative connected snapshot identifies the exact
Provider/profile/scope/session generation, Workbench queues one Acquire
attempt. The page's Acquire action remains a manual recovery route. The
controller lease is exclusive: other API sessions retain read-only access, but
their writes are rejected with `LEASE_BUSY (-10)` while another session owns
control.

Product API v1.10 requires explicit timing-mode start feature bit 11, making
the complete required feature mask `0xfff`. The historical 2026-07-24 record
observed `0xfff` on the RAM-deployed controller and `0x7ff` after a reboot into
persistent release24/v1.9.
Generic Workbench Run retains the common `OP_SAFE`, current-Boot
active-package, OP bus, nonzero matching WKC, and zero-fault gate. The
controller classifies the active package's actual mode from validated ECFG/DC
content: zero configured DC slaves means FreeRun and one or more valid DC
slaves means DC. Run never rewrites the package. Explicit StartFreeRun/StartDc
remain private adapter capabilities for external clients, not Workbench mode
buttons. DC lock is runtime observation and is not used to infer package mode.

A mismatch is terminal stage-2 `TIMING_MODE_MISMATCH (-35)`. Its detail is
`(requested_mode << 32) | actual_mode`, with the requested mode in the high
32 bits and the active-package actual mode in the low 32 bits. The client
preserves that controller detail. ECPKG construction, upload, activation,
ECFG/DC editing, and offline-to-actual Apply remain outside this delta.

Local Mock Scan/Diagnostics UI is hidden by default in production. It is
registered only in a `WITH_TESTS` build or when
`QTC_ETHER_CAT_ENABLE_MOCK_UI=1` is set for that process.

The current English regression passed Workbench 95, Project 15, Devices 8,
Core 19, Scan 11, Diagnostics 7, and ProductApi 71: 226 passed, 0 failed, and
1 ProductApi hardware test skipped. The `WITH_TESTS=OFF` product build passed
with exactly 17 plugin dylibs, and all EtherCAT Simplified Chinese contexts
contain no unfinished or empty translations.

The generic Start and explicit DC real-controller lifecycles each passed 3
tests with 0 failures and completed safe cleanup in `SHUTDOWN`/EMPTY with no
lease owner or fault. Windows `ISSUE-RT-009` separated a successful LRW cycle
from the actual failure: `OP_REQUEST` for station `0x1002` returned WKC 0/1 on
all four attempts. `cfg812` was not activated and no FreeRun lifecycle ran.
The controller was safely rolled back to `B/12/813`, `OP_SAFE`, WKC 11/11,
faults 0, and lease 0. The Windows session continues with `ISSUE-API-016` and
the smallest isolated fix.

The earlier single-instance product observation showed Workbench, Simplified
Chinese, hidden production Mock UI, and the compact tree. The current endpoint
and unified-output revision is covered by 95 Workbench tests and a three-pass
ProjectExplorer passive-output lifecycle test. No controller connection or
hardware command was executed in this round.

Historical 2026-07-24 evidence records a headless production-Provider run
against the RAM-deployed v1.10 controller that reached AcquireControl,
Configuration, and a three-slave DiscoverTopology, then safely released and
disconnected. Exact restore of `A/11/810` returned typed
`CAPABILITY_MISMATCH (-20)`, so that run did not qualify Start, Pause/Resume,
ControlledStop, or cyclic FreeRun/DC behavior.

## Explicit rescan, managed ESI, and live telemetry delta

This local product delta changes only the product-owned EtherCAT data,
Devices, ProductApi, and Workbench surfaces plus shared product resources,
translations, and qualification documents. It adds no upstream Qt Creator
Core, ProjectExplorer, or application-bootstrap change.

Normal controller operation no longer includes topology discovery. Connect
still establishes the selected Provider/profile session and queues the
generation-bound exclusive lease request, but it never scans or changes
controller state. Quick Run from `SHUTDOWN` now performs the bounded
`RestoreActivePackage -> Start` sequence. Run from `OP_SAFE` and `PAUSED`
retains Start and Resume. Only the explicit **Rescan Bus** action sends
DiscoverTopology; the existing lease, ready `SHUTDOWN`, and inactive-package
gates remain unchanged.

The fixed product resource directory contains the unmodified vendor XML for
XB6 and SV630N. Their SHA-256 values are
`5b0bfbfffdfde1fd293589deb4a1c59f974aa79dcd9206ac0a695ab00f395bf7`
and
`e6f39fd4e0f8801c83ec3ac796e138fe3ee1566cb94bb28285b93538fdb9e4a1`.
Devices indexes those resources and XML in the fixed writable
`ethercat/esi/library` user directory. Exact identity matching supplies the
vendor name and capability; unknown devices remain explicitly unknown.
Workbench adds function-specific device icons and persists the user-resized
two-column navigation header.

`EtherCATData` adds a decoded performance summary without exposing Product API
message IDs or wire offsets. `EtherCATProductApi` privately validates the exact
v1.1, v1.2-v1.6, and v1.7-v1.10 PerformanceSnapshot sizes, response envelope,
reserved bytes, sample flags, bounds, freshness, completeness, padding, and
legacy-window consistency. It also schedules a 500 ms read-only GetState query
only while the session is live and no refresh, control, or deployment
operation is active. Workbench uses ControllerState for current cycle, WKC,
AL/bus, and DC data and PerformanceSnapshot for timing, cyclic counters, and
sample-capture evidence.

CMake and qbs resource lists are synchronized. Offline qualification includes
the 79-pass ProductApi suite with one real-hardware test skipped, the 9-pass
Devices suite with exact vendor-file assertions, and six focused Workbench
tests covering fast restart, no automatic discovery, functional/state icons,
and live telemetry. No Product API, lease, scan, state, package, JTAG, CPU0,
CPU1, FPGA, or other controller request was made during this delta.

## Startup presentation and failed-connection reset delta

Opening or restoring a single valid EtherCAT project now preserves a bounded
navigation width, selects its Master, and displays the property tabs at the
same time. Splitters remain user-resizable, while the configuration area and
upper details pane cannot collapse behind the tree or Application Output.

A terminal Product API connection, handshake, timeout, or protocol failure now
closes all three channels, clears live controller identity and online evidence,
and publishes `Disconnected`. The structured `lastError` remains available to
Application Output, but the UI no longer retains a connected-looking failed
session.

Offline qualification passed the focused Workbench startup-presentation and
communication-page tests with 3 passes each, plus the complete ProductApi
suite with 79 passes, 0 failures, and 1 real-hardware test skipped. No Product
API, lease, scan, state, package, JTAG, CPU0, CPU1, FPGA, or other controller
request was made during this delta.

## Concise controller output and fault decoding delta

Workbench Application Output now records terminal controller events instead
of transport progress. Connecting, Handshaking, Disconnecting, pending
control stages, and intermediate deployment stages are silent. Terminal
connection errors retain the provider summary, symbolic and numeric status,
and distinct diagnostic detail.

`EtherCATData` now names 17 provider-neutral controller-fault categories. The
ProductApi adapter validates the complete v1.10 wire mask against that shared
contract, and Workbench turns only the shared semantics into short names and
recovery guidance. Current and latched masks remain separate and retain their
exact hexadecimal values; the output also includes the latest alarm sequence
and nonzero command result. A cleared current mask with retained latched
evidence explicitly asks for confirmation before reset. AL `OP` and matching
WKC remain useful bus evidence but never hide a current or historical fault.

Focused offscreen qualification covers silent Connecting/Handshaking and
pending control/deployment stages, full error preservation, current/latched
fault decoding, heartbeat deduplication, and terminal control results. This
delta used no Product API, lease, scan, state, package, JTAG, CPU0, CPU1,
FPGA, or other controller request.

## Product API v1.11 controlled fault confirmation delta

This local delta extends the existing provider-neutral control vocabulary with
`ResetFault`. Shared `EtherCATData` carries only the semantic command and the
two displayed-snapshot confirmation values: the complete expected latched
fault mask and expected last alarm sequence. No Product API message number,
feature bit, byte layout, channel, or vendor status crosses into Workbench or
the generic Provider contract. Providers that do not opt in continue to reject
the command.

`EtherCATProductApi` privately implements the Windows `ISSUE-API-030` frozen
Product API v1.11 contract:

- HELLO negotiates minor 11; ResetFault capability requires feature bit 12
  `CONTROLLED_FAULT_RESET (0x00001000)`, while a complete v1.11
  implementation advertises the cumulative feature mask `0x00001fff`;
- a peer below minor 11 or missing bit 12 is rejected locally as
  `UNSUPPORTED (-14)` and receives no ResetFault request;
- Control message `ResetFault (0x0107)` has exactly 16 network-order bytes:
  `u64 expected_latched_faults`, `u32 expected_last_alarm_sequence`, and a
  zero `u32` reserved field;
- dispatch requires the current BootId, a nonzero RequestId, the Session that
  owns the exclusive lease, service state exactly `FAULT`,
  `current_faults == 0`, and an unchanged nonzero full latched mask plus
  nonzero alarm sequence from one displayed ControllerState snapshot; and
- CPU1 atomically compares and clears the latch. Ordered CommandStatus stages
  1 through 4 end only at final stage 4, whose detail identifies the strict
  AlarmCleared event committed before the response snapshot.

A ResetFault request that reaches stage 4 succeeds only with zero current and
latched masks plus `AlarmCleared (0x0208)`, code `FAULT_CLEARED (5)`, severity
`INFO (1)`, source `SERVICE (1)`, flags exactly `CLEARED (0x2)`, the confirmed
mask, and zero detail/reserved fields. A changed mask or alarm sequence is
stale, an active cause is rejected, and a disallowed runtime state is rejected.
ResetFault does not reboot or recover the controller, rescan, alter
selectors/packages, start/stop the application, or perform NIC/PHY/CPU
recovery. If a refreshed snapshot already has no latch, the adapter completes
locally without sending.

Workbench registers one **Confirm / Reset Fault** ActionManager command and
reuses it in the embedded Communication page, the top engineering command
strip, and the selected EtherCAT Master context menu. The shared gate prevents
use in `RUNNING`, `PAUSED`, transitional states, without the lease, while a
cause is active, or after the displayed confirmation becomes stale.

The specialized Application Output failure record takes precedence over the
generic fault summary. It retains symbolic and numeric API status, signed
operation result, channel, detail, stage/final or outcome-unconfirmed state,
current and latched fault names and hexadecimal masks, the latest matching
AlarmEvent when retained, or otherwise the pre-reset confirmation mask and
alarm checkpoint, and corrective action. A stage-4 wire success is not reported
without the zero-mask postcondition and matching AlarmCleared evidence. The
fresh zero-latch stage-0 local no-op is the explicit exception: it sends
nothing and reports that no controller request was sent.

Windows froze this contract after complete localhost Product API and OS-less
regression. On 2026-07-29 the production Qt adapter headlessly confirmed
Product API v1.11/feature mask `0x00001fff` on BootId
`0x4f535dcbef09ea55` and passed the real DC lifecycle through final
`SHUTDOWN`, lease release, and disconnect. No visible product was launched.
The healthy real snapshot had fault masks `0/0`, so no real-controller
ResetFault request, AlarmCleared response, or recovered hardware state is
claimed.

## Data-driven XB6 and SV630N adapter package delta

The independent `EtherCATDeviceAdapters` plugin now provides the first
concrete implementation of the provider-neutral device-adapter contract. It
loads strict versioned package data from the shared EtherCAT resource tree and
selects a package only from the exact identity, revision, ESI SHA-256, process
data profile, and modular assignment. The resolver binds semantic signals to
the Project process-image description; it owns no Product API transport and
does not read or write live process data.

The XB6 Candidate package preserves the coupler separately from installed
modules and contains four exact 16-channel DI/DO ModuleIdent templates.
Slot-relative signal IDs and PDO/object indexes are expanded from explicit
module assignments. The SV630N Candidate package describes the DC-only
RxPDO `0x1702` and TxPDO `0x1b04` velocity profile plus finite CiA 402
preparation and stop plans. Manufacturer identities, PDO indexes, and action
details remain package data and do not appear as branches in Core, Workbench,
ProductApi, or the controller runtime.

Both packages are unsigned Candidates with real-hardware permission disabled.
All writable signals have manual control disabled, no safe output value is
asserted, and both SV630N actions are disabled. This round therefore claims
only deterministic package loading and offline semantic binding. The focused
Qt 6.11 offscreen suite passed 13 tests with no failures, and the complete
Core suite passed 22 tests. The `WITH_TESTS=OFF` product targets built with
the new plugin and copied packages present in the app resource tree. No
visible GUI or controller connection was used.

## Provider-neutral Runtime Resource contract delta

`EtherCATData` now defines a transport-independent, read-only Runtime Resource
catalog and snapshot. Opaque resource/component/parent/consistency-group IDs
are bound to a complete epoch containing BootId, active package selector,
configuration identity, topology/runtime generations, catalog revision, and
opaque topology identity. Descriptors retain primitive type, bit width,
direction, access, internal process-image coordinates, quality mask, and an
optional typed safe value. Snapshots retain capture cycle, controller
timestamp, typed values and quality, including opaque representations for
unknown future types.

`ControllerConnectionProvider` gains an optional catalog/snapshot/refresh
surface and two change signals. Its default remains fail-closed:
`supportsRuntimeResources()` is false, values are absent, and refresh returns
an explicit unsupported error. No output transaction, manual-control command,
Product API message, Workbench page, semantic adapter join, or hardware path
is added in this delta. Names, ordinals, process-image offsets, and vendor
identity are explicitly not valid semantic binding keys.

The Qt 6.11 offscreen Core suite passed 23 tests with no failures. CMake and
qbs source lists remain synchronized. No visible GUI or controller connection
was used.

## Product API v1.12 Runtime Resource adapter delta

`EtherCATProductApi` now opts into the generic read-only Runtime Resource
surface when HELLO negotiates minor 12 and optional feature bit 13
(`0x00002000`, cumulative mask `0x00003fff`). Missing bit 13 does not reject
the session: refresh is rejected locally and sends nothing. A supported
refresh uses the existing joined Bulk channel without acquiring a lease,
pages `QueryResourceTable (0x040b)` to one frozen full epoch, and requests
`GetResourceSnapshot (0x040c)` for at most 64 strictly ordered IDs from one
capture cycle.

All provider-owned IDs stay opaque and are retained with BootId, package
selector/generation, ConfigurationId, topology/runtime generations, catalog
revision, and topology identity. A catalog larger than 64 is retained while
the current value snapshot is marked `complete=false`; captures from different
cycles are never combined. Typed resource failures are decoded, and the
API-034 server's current pre-dispatch `BulkStatus` response is accepted only
as matching failure evidence.

The wire contract exposes no SemanticBindingId or authenticated
`SemanticSignalId` mapping. No join is inferred from names, vendor identity,
ordinal, station/module position, object indexes, or process-image offsets.
This delta adds no Workbench page, output transaction, manual control, or
hardware claim. Validation is limited to the headless codec/session loopback;
the existing v1.11 real-controller results are unchanged.

The Qt 6.11 offscreen ProductApi suite passed 105 tests with no failures and
one hardware-only skip. The Core suite passed 23 tests with no failures, and
the non-test ProductApi target built successfully. No visible GUI or controller
connection was used.
