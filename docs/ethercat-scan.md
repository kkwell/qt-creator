# EtherCAT Mock Scan

## Scope

`EtherCATScan` is the stage-5 scan workflow plugin. It provides TwinCAT-inspired
scan commands, progress, topology comparison, result acceptance, and an
extensible property page. Every result in this stage is produced locally by a
Mock provider. The plugin opens no socket, enumerates no physical network
interface, contacts no controller, and defines no Zynq or wire protocol.

The UI and every output message use explicit `Mock` or `MOCK SCAN` wording. A
future real implementation must be a separate Provider plugin implementing the
public in-process `ScanProvider` contract.

The local Mock UI is hidden by default in production. Its Provider, actions,
tree nodes, pages, and status contribution are registered only in a
`WITH_TESTS` build or when the process is started explicitly with
`QTC_ETHER_CAT_ENABLE_MOCK_UI=1`. This opt-in does not authorize a physical
scan or turn Mock results into controller evidence.

## Dependencies and ownership

The plugin has hard metadata dependencies on `Core`, `EtherCATCore`,
`EtherCATDevices`, `EtherCATProject`, and `EtherCATWorkbench`. CMake and qbs
declare the same dependency graph. It uses:

- public immutable values from `EtherCATData`;
- `ProjectService`, `SelectionService`, `StateService`, `ScanProvider`, and
  `PropertyPageProvider` from `EtherCATCore`;
- immutable ESI summaries from `EtherCATDevices` when available;
- the public Workbench menu/context IDs and the property-page extension point.

It does not include another plugin's `Internal` headers, access a Workbench
model or widget, retain a `QModelIndex`, or write an EtherCAT project file.
Accepting a result calls the checked `ProjectService` command, so Project owns
validation, modification state, persistence, and Undo/Redo.

## Commands and selection

The EtherCAT menu contains one registered ActionManager command for each
operation:

- Scan Mock Interfaces;
- Scan Mock Slaves;
- Rescan Selected Mock Branch;
- Compare Mock Scan With Project;
- Accept Mock Scan as Offline Configuration;
- Keep Existing Offline Configuration;
- Cancel Mock Scan.

Command enablement is derived from the public stable-ID selection, open project
snapshots, scan state, and comparison result. Buttons on the Mock Scan page use
the same registered actions; they do not create a second command path.

A project or master selection resolves to that project's master. A configured
slave selection is required for a slave-level branch rescan; selecting the
master rescans its complete branch. Invalid or closed IDs are rejected before
the timer starts.

## State machine and snapshots

A slave or branch scan follows:

```text
Idle
-> Preparing
-> ScanningMaster
-> ScanningSlaves
-> BuildingSnapshot
-> Comparing
-> Completed / Cancelled / Failed
```

Interface discovery has no slave topology to compare and therefore follows
`Preparing -> ScanningMaster -> BuildingSnapshot -> Completed`. Its result
cannot be compared as a slave topology or accepted into the offline project.

Only one operation may be active. Completed, Cancelled, and Failed states
remain terminal until explicitly cleared or until the Project that owns the
current request is closed. Progress includes a bounded maximum, current phase,
detail text, and discovered-slave count. A result contains the originating
operation and selected branch ID so comparison and acceptance keep the same
scope.

The local provider uses a single-shot GUI-thread `QTimer`; it owns no worker
thread, future, socket, or process. Owning Project close, plugin shutdown, and
explicit Cancel stop the timer before object removal. Cancel and failure
discard the partial result and never invoke a Project command.

The request and its transient result are scoped to `ScanRequest::projectId`.
Closing an unrelated Project leaves the current operation or terminal state
unchanged. Closing the owning Project cancels an active operation and then
clears the request, progress, result, and error back to `Idle`. Existing
Provider signals empty the Mock Scan difference table, disable Compare,
Accept, Keep Existing, and Cancel, and remove the Scan contribution from
`StateService`. Reopening a file with the same persisted Project ID does not
restore the old result; a new Mock scan must be started explicitly.

## Mock scenarios

The property page offers five deterministic local scenarios:

| Scenario | Behavior |
|---|---|
| Normal | Four local slaves from the ESI summaries or built-in Mock identities |
| Slow | Uses a visible delay between state transitions and supports cancellation |
| Partial failure | Fails after two discovered slaves and publishes an explicit reason |
| Duplicate device | Publishes one duplicate physical identity and a blocking difference |
| Revision mismatch | Reuses current offline identities and increments one revision |

Scan IDs are deterministic UUIDv5 values derived from master and physical
identity. A nonzero Serial Number anchors identity across position changes; a
zero Serial Number falls back to position and revision. Every snapshot sets
`mock=true` and includes an interface description stating that no network
device was accessed.

## Topology comparison

The single domain comparison function is used by the Provider, workflow, and
UI. It first matches a physical device independently of position, then checks
same-position identity mismatches, and finally reports missing and added
slaves. It produces:

- Added as information;
- Missing, position, Revision, Serial Number, and Alias differences as warnings;
- Vendor, Product, and duplicate physical identities as blocking items;
- explicit informational PDO and DC comparison placeholders.

`exactMatch` ignores the two declared placeholders. Acceptance is allowed only
for a complete snapshot without a blocking difference. PDO and DC are not
silently presented as compared in this stage.

## Acceptance and stable IDs

Accept is always an explicit user command. A complete slave scan replaces the
master's offline slave list through `ProjectService`. A slave-level branch scan
merges only that branch and leaves all other configured slaves unchanged. A
master-level branch scan has full-master scope.

Reconciliation preserves the existing offline `NodeId` when a scanned physical
device matches it. New devices receive their deterministic scan ID. This keeps
tree selection stable across Revision, Alias, and position changes. After the
Project command succeeds, the result is compared again against the new snapshot
and becomes an exact match. Each acceptance is one Project Undo command; branch
acceptance and full-topology acceptance can be undone and redone independently.

Keep Existing clears the scan result without changing the project. Interface,
partial, cancelled, incomplete, non-Mock, and blocking results cannot be
accepted.

## UI and lifecycle

Masters and configured slaves receive a `Mock Scan` property page through the
public page Provider. It shows an explicit simulation banner, selected stable
ID, scenario, shared actions, state, progress, and one difference table. Layout,
font, margins, and spacing use Qt Creator `StyleHelper` tokens and the active
palette; no Beckhoff asset, fixed color, or copied TwinCAT material is used.

Initialization registers the scan Provider, constructs the command workflow,
then registers the property-page Provider. Shutdown removes the page Provider,
cancels the workflow, marks the scan Provider unavailable, removes it from the
object pool, and destroys all objects. Each step is idempotent.

The owning-Project connection is context-bound to the provider, and an active
close stops the existing timer before state is cleared. Qt documents these
mechanisms in [`QObject::connect()`](https://doc.qt.io/qt-6/qobject.html#connect)
and [`QTimer::stop()`](https://doc.qt.io/qt-6/qtimer.html#stop). Beckhoff's
[scan and offline-comparison workflow](https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10832129675.html)
anchors only the familiar scan/compare concepts. Project-close invalidation is
an Embed Labs stale-state prevention rule, not copied TwinCAT behavior.

## Verification

The focused suite covers metadata and dependency resolution, object-pool
registration, all commands and page widgets, exact/reordered/added/missing
comparison, blocking identity and duplicate cases, complete state order,
interface isolation, normal and slow operation, cancellation, partial failure,
shutdown cancellation, Revision mismatch, selected-branch merge, stable IDs,
and Project Undo/Redo. The owning-Project lifecycle matrix uses two real open
Projects and covers Active, Completed, Failed, and Cancelled states. It proves
that closing the unrelated Project emits no Provider state/result/finished
signal, while closing the owner clears progress/result/error, the difference
table, commands, and status. Each row reopens the same persisted Project ID,
starts a new Mock scan successfully, and preserves Project and Undo/Redo data.

Current build, focused/regression test, plugin-enabled/disabled startup, and
known test limitations are recorded in `docs/compatibility-matrix.md`.

The current English regression passed 11 Scan tests and 226 tests across the
seven EtherCAT suites, with one ProductApi hardware test skipped and no
failures. The
`WITH_TESTS=OFF` product build passed with exactly 17 plugin dylibs, and all
EtherCAT Simplified Chinese contexts contain no unfinished or empty
translations. The generic Start and explicit DC real-controller lifecycle
results belong to ProductApi/Workbench and do not qualify this Mock-only Scan
Provider. Windows `ISSUE-RT-009` isolated the current `cfg812` failure after a
successful LRW cycle: `OP_REQUEST` for station `0x1002` returned WKC 0/1 on
all four attempts. `cfg812` was not activated, no FreeRun lifecycle ran, and
the controller was safely rolled back to `B/12/813`, `OP_SAFE`, WKC 11/11,
faults 0, and lease 0. The Windows session continues with `ISSUE-API-016` and
the smallest isolated fix.

The earlier single-instance product observation showed Workbench, Simplified
Chinese, hidden production Mock UI, and the compact tree. The current
endpoint/output round used widget-level Workbench and passive-output tests
without connecting a controller. These observations do not qualify this
Mock-only Scan Provider.
