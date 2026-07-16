# EtherCAT Core In-Process API

## Scope and version

This document freezes the stage-1 in-process contract between the EtherCAT
plugins. It is API version 1 for the current local Qt Creator 20.0.1 product.
It is not a network protocol, controller ABI, project file format, or Zynq
contract.

The API is implemented by:

- `EtherCATData`, a UI-independent Qt Core library.
- `EtherCATCore`, a Qt Creator plugin that depends only on Core, Utils,
  EtherCATData, and Qt Widgets.

Feature-specific ESI queries, scan operations, diagnostic samples, and property
widgets are intentionally absent. They may only be added by the owning serial
plugin issue, with a dedicated Core/API change if the public contract must
grow. The Project stage adds the first such typed extension: immutable project
snapshots and the project lifecycle service contract described below.

## Stable identity

`EtherCAT::Data::NodeId` is the only stage-1 cross-plugin node identity.

- A new ID is generated with `NodeId::create()`.
- Persistence uses the lowercase UUID text returned by `toString()` without
  braces.
- Invalid text parses to a null ID.
- Null means no selection or no identity.
- Display names, tree rows, `QModelIndex`, and widget pointers are never IDs.

## Public services

The plugin registers these owned objects in the Qt Creator object pool during
`initialize()`:

| Service | Contract |
|---|---|
| `SelectionService` | Stores one current `NodeId` and emits old and new IDs only when the value changes. |
| `StateService` | Stores one status contribution per stable source ID and exposes the highest severity. |
| `ProviderRegistry` | Tracks typed EtherCAT Providers dynamically as Qt Creator adds and removes them from the object pool. |

Consumers retrieve services with
`ExtensionSystem::PluginManager::getObject<T>()`. They must not construct a
second global instance or add a separate service locator.

`StatusSeverity` ordering is `Ready`, `Busy`, `Warning`, then `Error`.
`StateService` rejects a contribution with an invalid source ID. A source owns
its entry and must clear it when its operation ends or its plugin shuts down.

## Provider extension points

All optional EtherCAT capabilities derive from `Provider` and have a globally
unique `Utils::Id`, user-visible name, type, and availability flag.

| Provider type | Owning future plugin |
|---|---|
| `ProjectService` | EtherCATProject |
| `DeviceRepositoryProvider` | EtherCATDevices |
| `PropertyPageProvider` | EtherCATWorkbench and optional page contributors |
| `ScanProvider` | EtherCATScan or a future real-controller provider |
| `DiagnosticsProvider` | EtherCATDiagnostics or a future real-controller provider |

Stage 1 froze discovery and lifecycle only. The Project API revision adds typed
project methods before the Project implementation. It deliberately does not
expose generic `QVariant`, byte arrays, network messages, or placeholder methods
for later feature data.

## Project service contract

`ProjectService` is an abstract, GUI-thread service implemented by the
EtherCATProject plugin. It exposes immutable `ProjectSnapshot` values for all
open EtherCAT projects and one active project ID. A snapshot contains project
metadata plus the stable Project, Target, and Master node identities; it does
not contain ESI, PDO, scan, or diagnostic data.

The service owns the cross-plugin commands for project activation, rename,
save, undo, and redo. Commands return `Utils::Result` so a consumer cannot
mistake a rejected command for success. `projectAdded`,
`projectAboutToBeRemoved`, `projectChanged`, and `activeProjectChanged` are the
only cross-plugin lifecycle notifications. Consumers must re-query a snapshot
after a notification and must not retain ProjectExplorer or document pointers.

ProjectExplorer remains the owner of open/close and startup-project lifecycle.
The service mirrors that state; it does not create a second project registry.
The project file format, atomic save, migration, and undo stack belong to the
Project plugin and are not part of this Core API.

Providers register themselves with `PluginManager::addObject()` only after
they are initialized and remove themselves before destruction. Consumers
listen to `ProviderRegistry::providerAdded` and
`providerAboutToBeRemoved`; they do not retain a Provider pointer after the
removal signal.

Provider IDs are globally unique across all Provider kinds. If two live
objects use the same ID, only the first one is published by the registry. Such
a collision is a plugin defect, not a selection mechanism.

## Thread and ownership rules

- Core services and Providers have GUI-thread affinity.
- Mutating `SelectionService` or `StateService` from another thread is an API
  violation guarded by `QTC_ASSERT`.
- Background work must deliver immutable data back to the GUI thread before
  changing these services.
- `ProviderRegistry` stores guarded pointers and removes a Provider on the
  object-pool removal notification.
- `aboutToShutdown()` removes the registry, state service, and selection
  service in reverse registration order, after clearing owned state.
- Cleanup is idempotent, so normal shutdown followed by plugin destruction
  cannot unregister an object twice.

## Settings and public IDs

The plugin owns the `Z.EtherCAT` settings category and the
`EtherCAT.General` page. Stage-1 settings are:

- show advanced EtherCAT properties, default off;
- maximum recent diagnostic events, default 1,000 and bounded from 100 to
  100,000.

`EtherCAT.Context`, `EtherCAT.Menu`, `Z.EtherCAT`, and `EtherCAT.General` are
reserved public IDs. Workbench owns the future mode and visible menu actions;
Core does not create an empty product mode or an empty menu.

## Stage-1 verification

The focused plugin test covers:

- generated metadata and the three object-pool services;
- `NodeId` generation, parsing, equality, and hashing;
- selection change suppression and clearing;
- state contribution validation and severity aggregation;
- dynamic Provider addition, availability, and removal;
- settings-page registration.

The focused build target and test execution are limited to Core and
EtherCATCore. Enabling tests for the complete existing product currently
exposes an unrelated EasyBoard baseline error: `easyboardbrowser.cpp` includes
the unavailable `extensionmanager_test.h` when `WITH_TESTS` is on. The normal
product build with tests off succeeds and is the integration-build evidence
for stage 1.
