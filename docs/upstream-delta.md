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
| `src/libs/ethercatdata` | 9 | Product-owned stable identity, project, device, scan, and diagnostics values |
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
Qt Creator `StatusBarManager` and product-owned `StateService`, adds no plugin
dependency, and keeps CMake and qbs source lists synchronized. No upstream
Core, ProjectExplorer, or application path changes, so the direct Core patch
count remains five.

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

## Remote comparison status

A later upstream comparison or merge may only be performed after an explicit
user request. Until then, the only supported baseline is the current local
commit and its descendants on `embed-labs`.
