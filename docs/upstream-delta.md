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
| `src/libs/ethercatdata` | 8 | Product-owned stable identity, project, device, and scan value library |
| `src/plugins/ethercatcore` | 19 | Product-owned Core services and extension points |
| `src/plugins/ethercatproject` | 18 | Product-owned offline project plugin |
| `src/plugins/ethercatdevices` | 12 | Product-owned offline ESI repository plugin |
| `src/plugins/ethercatworkbench` | 20 | Product-owned EtherCAT engineering-shell plugin |
| `src/plugins/ethercatscan` | 16 | Product-owned local Mock scan workflow plugin |

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

Each completed EtherCAT issue must report:

- Direct upstream files modified.
- Product-owned plugin/library files modified.
- Whether the Core patch count increased.
- Why an official extension point was insufficient, if the count increased.

## Remote comparison status

A later upstream comparison or merge may only be performed after an explicit
user request. Until then, the only supported baseline is the current local
commit and its descendants on `embed-labs`.
