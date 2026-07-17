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
| `src/plugins/ethercatworkbench` | 38 | Product-owned EtherCAT engineering-shell plugin |
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

The Workbench offline-topology issue changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private ESI-default
factory files with synchronized CMake/qbs entries, then reuses the existing
public Device repository, Selection service, and checked Project replacement
command for Add/Remove/Move operations. It adds no dependency or path under
upstream Core, ProjectExplorer, or the application bootstrap, so the direct
Core patch count remains five.

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

The configured-slave EtherCAT-page issue also changes only the product-owned
`EtherCATWorkbench` plugin and documentation. It adds two private EtherCAT-page
source files with synchronized CMake/qbs entries, preserves the existing master
and ESI read-only views, and routes Alias changes through the existing checked
Project replacement command. It adds no dependency, public Core API, or path
under upstream Core, ProjectExplorer, or the application bootstrap, so the
direct Core patch count remains five.

Each completed EtherCAT issue must report:

- Direct upstream files modified.
- Product-owned plugin/library files modified.
- Whether the Core patch count increased.
- Why an official extension point was insufficient, if the count increased.

## Remote comparison status

A later upstream comparison or merge may only be performed after an explicit
user request. Until then, the only supported baseline is the current local
commit and its descendants on `embed-labs`.
