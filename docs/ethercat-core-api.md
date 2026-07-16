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

The original stage-1 contract intentionally omitted feature-specific scan
operations, diagnostic samples, and property widgets. They are added only by
their owning serial plugin issue, with a dedicated Core/API change when the
public contract must grow. The Project stage added the first typed extension:
immutable project snapshots and the project lifecycle service contract below.

The Devices stage adds the second typed extension: immutable ESI device
descriptions, repository filtering, original-XML access, and a cancellable
import/index job contract. It still contains no wire protocol or controller
ABI.

The Scan API revision adds typed scan requests, state/progress, immutable Mock
topology snapshots, comparison records, offline-slave values, and the
cancellable `ScanProvider` contract. It still defines no transport, controller
address, message, byte layout, or serialization format.

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

`ScanProvider` is a GUI-thread capability with one operation at a time. A
consumer starts a typed request for interface discovery, slave discovery, or a
selected branch; observes the exact Idle/Preparing/ScanningMaster/
ScanningSlaves/BuildingSnapshot/Comparing/terminal state; reads bounded
progress and discovered count; and may cancel or clear the last result.
Completed results contain immutable topology and comparison values. Cancelled
and failed operations are terminal until cleared, and must not modify an
offline project. The Provider contract does not itself accept a result into a
project; that remains a checked ProjectService command in the owning stage.

## Project service contract

`ProjectService` is an abstract, GUI-thread service implemented by the
EtherCATProject plugin. It exposes immutable `ProjectSnapshot` values for all
open EtherCAT projects and one active project ID. A snapshot contains project
metadata, stable Project/Target/Master/Slave node identities, and checked
offline-slave Identity, position, Alias, Serial Number, and optional ESI
description links. It does not contain ESI XML, scan execution state, PDO,
online state, or diagnostic data.

The service owns the cross-plugin commands for project activation, rename,
offline-slave replacement, save, undo, and redo. Replacement is scoped to a
known master, validates all stable IDs, positions, names, and required Identity
values, and enters the same project Undo/Redo stack. Commands return
`Utils::Result` so a consumer cannot
mistake a rejected command for success. `projectAdded`,
`projectAboutToBeRemoved`, `projectChanged`, and `activeProjectChanged` are the
only cross-plugin lifecycle notifications. Consumers must re-query a snapshot
after a notification and must not retain ProjectExplorer or document pointers.

ProjectExplorer remains the owner of open/close and startup-project lifecycle.
The service mirrors that state; it does not create a second project registry.
The project file format, atomic save, migration, and undo stack belong to the
Project plugin and are not part of this Core API.

## Device repository contract

`DeviceRepositoryProvider` is an abstract, GUI-thread Provider implemented by
the EtherCATDevices plugin. It exposes immutable summaries and full device
descriptions containing:

- Vendor ID, Product Code, Revision Number, name, type, and group;
- SyncManager definitions and directions;
- RxPDO/TxPDO definitions, entries, bit lengths, and common data types;
- CoE capability flags and startup parameters;
- DC operation modes and timing defaults;
- source path, SHA-256, import time, warnings, and unsupported-feature markers.

Callers search with `DeviceFilter`, resolve a full description by stable
`NodeId`, and may request the exact original XML for forward compatibility.
They do not receive repository model indexes or parser objects.

`importFiles()` and `rebuildIndex()` return a provider-owned
`DeviceImportJob`. Jobs publish Pending, Running, Canceling, and Finished
states, bounded progress, and one immutable `DeviceImportResult`. `cancel()`
is idempotent. `finish()` is terminal and can emit only once. The Provider
emits repository reset/change signals only after worker results have returned
to the GUI thread.

Job pointers are guarded QObject references. Consumers must stop using a job
after its `destroyed` signal and must not retain a Provider after object-pool
removal. XML parsing, repository storage, deduplication, and stable-ID mapping
belong to EtherCATDevices, not Core.

Providers register themselves with `PluginManager::addObject()` only after
they are initialized and remove themselves before destruction. Consumers
listen to `ProviderRegistry::providerAdded` and
`providerAboutToBeRemoved`; they do not retain a Provider pointer after the
removal signal.

## Workbench property-page contract

`PropertyPageProvider` is the public, optional extension point used by the
Workbench details container. It receives an immutable `PropertyPageContext`
with a stable project ID, node ID, `WorkbenchNodeKind`, and display name. It
returns ordered `PropertyPageDescriptor` values and creates a `QWidget` only
when the host asks for that page.

The Workbench owns created page widgets. The Provider refreshes a page only
through `updatePage()` and must not retain the context, a tree index, or a
pointer to Workbench-private UI. The host destroys all widgets from a Provider
before that Provider leaves the object pool. Page IDs must be stable and
unique within one Provider; the effective host key is Provider ID plus page
ID.

`WorkbenchNodeKind` is deliberately limited to UI-neutral selections used by
the first-phase workbench: project, target, master, device repository, device,
diagnostics, and explicit placeholder. It carries no scan result, online
state, wire protocol, or controller ABI. Later plugins contribute pages by
subclassing this interface; they do not modify the Workbench tree or include
Workbench private headers.

The stage-4 Workbench implementation listens to property-provider addition,
availability changes, and removal. It destroys provider-owned widgets before
removal and identifies a hosted page by Provider ID plus page ID. It also
observes the availability of Scan and Diagnostics capability providers: this
controls only local placeholder visibility and tree status, and does not add a
scan algorithm, diagnostic payload, transport, or serialization contract.

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
reserved public IDs. Workbench owns the EtherCAT mode and visible menu actions;
Core does not create an empty product mode or an empty menu.

## Core API verification

The focused plugin test covers:

- generated metadata and the three object-pool services;
- `NodeId` generation, parsing, equality, and hashing;
- selection change suppression and clearing;
- state contribution validation and severity aggregation;
- dynamic Provider addition, availability, and removal;
- typed scan request, state, progress, cancellation, and reset behavior;
- settings-page registration.

The focused build target and test execution are limited to Core and
EtherCATCore. Enabling tests for the complete existing product currently
exposes an unrelated EasyBoard baseline error: `easyboardbrowser.cpp` includes
the unavailable `extensionmanager_test.h` when `WITH_TESTS` is on. The normal
product build with tests off succeeds and is the integration-build evidence
for stage 1.
